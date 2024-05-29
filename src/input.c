/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 3 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include <wut-fixups.h>

#include <config.h>
#include <crypto.h>
#include <input.h>
#include <menu/utils.h>
#include <messages.h>
#include <renderer.h>
#include <state.h>
#include <swkbd_wrapper.h>
#include <thread.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memory.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>
#pragma GCC diagnostic pop

#define SWKBD_QUEUE_SIZE 8

// WIP. This need a better implementation

FSClient swkbdFsclient;
VPADStatus vpad;
static const KPADStatus kpad[4];
static const Swkbd_ControllerInfo controllerInfo = {
    .vpad = &vpad,
    .kpad[0] = kpad,
    .kpad[1] = kpad + 1,
    .kpad[2] = kpad + 2,
    .kpad[3] = kpad + 3
};

static ControllerType lastUsedController;

static void *io = NULL;

static Swkbd_CreateArg createArg = { .workMemory = NULL, .unk_0x08 = 0 };
static Swkbd_AppearArg appearArg;

static OSMessageQueue swkbd_queue;
static OSMessage swkbd_msg[SWKBD_QUEUE_SIZE];

static OSTime lastButtonPress = 0;

typedef struct
{
    size_t globalMaxlength;
    bool globalLimit;
    bool okButtonEnabled;
    OSThread *calcThread;
} SWKBD_Args;

static bool isUrl(char c)
{
    return isNumber(c) || isLowercase(c) || isUppercase(c) || c == '.' || c == '/' || c == ':' || c == '%' || c == '-' || c == '_'; // TODO
}

typedef bool (*checkingFunction)(char);

static int calcThreadMain(int argc, const char **argv)
{
    (void)argc;
    (void)argv;

    OSMessage msg;
    do
    {
        OSReceiveMessage(&swkbd_queue, &msg, OS_MESSAGE_FLAGS_BLOCKING);
        if(msg.message == NUSSPLI_MESSAGE_NONE)
            Swkbd_CalcSubThreadFont();
    } while(msg.message != NUSSPLI_MESSAGE_EXIT);

    return 0;
}

static void SWKBD_Render(SWKBD_Args *args, KeyboardChecks check)
{
    char *inputFormString = Swkbd_GetInputFormString();
    if(inputFormString != NULL)
    {
        size_t len = strlen(inputFormString);
        if(len != 0 && check != CHECK_NONE && check != CHECK_NUMERICAL)
        {
            checkingFunction cf;
            switch(check)
            {
                case CHECK_HEXADECIMAL:
                    cf = &isHexa;
                    break;
                case CHECK_ALPHANUMERICAL:
                    cf = &isAllowedInFilename;
                    break;
                case CHECK_URL:
                    cf = &isUrl;
                    break;
                default:
                    // DEAD CODE
                    debugPrintf("0xDEADC0DE: %d", check);
                    return;
            }

            for(len = 0; inputFormString[len] != '\0'; ++len)
                if(!cf(inputFormString[len]))
                {
                    inputFormString[len] = '\0';
                    Swkbd_SetInputFormString(inputFormString);
                    break;
                }
        }

        args->okButtonEnabled = args->globalLimit ? len == args->globalMaxlength : len <= args->globalMaxlength;
    }
    else
        args->okButtonEnabled = false;

    Swkbd_SetEnableOkButton(args->okButtonEnabled);

    Swkbd_Calc(&controllerInfo);

    if(Swkbd_IsNeedCalcSubThreadFont())
    {
        OSMessage msg = { .message = NUSSPLI_MESSAGE_NONE };
        OSSendMessage(&swkbd_queue, &msg, OS_MESSAGE_FLAGS_NONE);
    }

    drawKeyboard(lastUsedController != CT_VPAD_0);
}

static bool SWKBD_Show(SWKBD_Args *args, KeyboardLayout layout, KeyboardType type, int maxlength, bool limit, const char *okStr)
{
    debugPrintf("SWKBD_Show()");
    if(!Swkbd_IsHidden())
    {
        debugPrintf("...while visible!!!");
        return false;
    }

    args->calcThread = startThread("NUSspli SWKBD font calculator", THREAD_PRIORITY_MEDIUM, STACKSIZE_SMALL, calcThreadMain, 0, NULL, OS_THREAD_ATTRIB_AFFINITY_ANY);
    if(args->calcThread == NULL)
    {
        debugPrintf("SWKBD: Can't spawn calc thread!");
        return false;
    }

    if(okStr)
    {
        size_t strLen = strlen(okStr);
        appearArg.keyboardArg.configArg.str = MEMAllocFromDefaultHeap(sizeof(char16_t) * ++strLen);
        if(appearArg.keyboardArg.configArg.str)
            for(size_t i = 0; i < strLen; ++i)
                appearArg.keyboardArg.configArg.str[i] = okStr[i];
    }
    else
        appearArg.keyboardArg.configArg.str = NULL;

    // Show the keyboard
    appearArg.keyboardArg.configArg.languageType = getKeyboardLanguage();
    switch(appearArg.keyboardArg.configArg.languageType)
    {
        case Swkbd_LanguageType__Japanese:
            appearArg.keyboardArg.configArg.languageType2 = Swkbd_LanguageType2__Japanese;
            break;
        case Swkbd_LanguageType__French:
            appearArg.keyboardArg.configArg.languageType2 = Swkbd_LanguageType2__French;
            break;
        case Swkbd_LanguageType__German:
            appearArg.keyboardArg.configArg.languageType2 = Swkbd_LanguageType2__German;
            break;
        case Swkbd_LanguageType__Italian:
            appearArg.keyboardArg.configArg.languageType2 = Swkbd_LanguageType2__Italian;
            break;
        case Swkbd_LanguageType__Spanish:
            appearArg.keyboardArg.configArg.languageType2 = Swkbd_LanguageType2__Spanish;
            break;
        case Swkbd_LanguageType__Dutch:
            appearArg.keyboardArg.configArg.languageType2 = Swkbd_LanguageType2__Dutch;
            break;
        case Swkbd_LanguageType__Portuguese:
            appearArg.keyboardArg.configArg.languageType2 = Swkbd_LanguageType2__Portuguese;
            break;
        case Swkbd_LanguageType__Russian:
            appearArg.keyboardArg.configArg.languageType2 = Swkbd_LanguageType2__Russian;
            break;
        default:
            appearArg.keyboardArg.configArg.languageType2 = Swkbd_LanguageType2__English;
    }

    appearArg.keyboardArg.configArg.controllerType = lastUsedController;
    appearArg.keyboardArg.configArg.keyboardMode = layout;

    appearArg.inputFormArg.type = type;
    args->globalMaxlength = appearArg.inputFormArg.maxTextLength = maxlength;

    bool kbdVisible = Swkbd_AppearInputForm(&appearArg);
    debugPrintf("Swkbd_AppearInputForm(): %s", kbdVisible ? "true" : "false");

    if(!kbdVisible)
        return false;

    args->globalLimit = limit;
    VPADSetSensorBar(VPAD_CHAN_0, true);

    debugPrintf("nn::swkbd::AppearInputForm success");
    return true;
}

static void SWKBD_Hide(SWKBD_Args *args)
{
    debugPrintf("SWKBD_Hide()");
    if(Swkbd_IsHidden())
    {
        debugPrintf("...while invisible!!!");
        return;
    }

    VPADSetSensorBar(VPAD_CHAN_0, false);
    Swkbd_DisappearInputForm();

    // DisappearInputForm() wants to render a fade out animation
    while(!Swkbd_IsHidden())
        SWKBD_Render(args, CHECK_NONE);

    OSMessage msg = { .message = NUSSPLI_MESSAGE_EXIT };
    OSSendMessage(&swkbd_queue, &msg, OS_MESSAGE_FLAGS_BLOCKING);
    stopThread(args->calcThread, NULL);

    if(appearArg.keyboardArg.configArg.str)
        MEMFreeToDefaultHeap(appearArg.keyboardArg.configArg.str);
}

bool SWKBD_Init()
{
    debugPrintf("SWKBD_Init()");

    createArg.fsClient = &swkbdFsclient;
    if(FSAddClient(createArg.fsClient, 0) != FS_STATUS_OK)
        return false;

    createArg.workMemory = MEMAllocFromDefaultHeap(Swkbd_GetWorkMemorySize(0));
    if(createArg.workMemory == NULL)
    {
        FSDelClient(createArg.fsClient, 0);
        return false;
    }

    OSBlockSet(swkbd_msg, 0, sizeof(OSMessage) * SWKBD_QUEUE_SIZE);
    OSInitMessageQueueEx(&swkbd_queue, swkbd_msg, SWKBD_QUEUE_SIZE, "NUSspli SWKBD calc queue");

    switch(getKeyboardLanguage())
    {
        case Swkbd_LanguageType__Japanese:
            createArg.regionType = Swkbd_RegionType__Japan;
            break;
        case Swkbd_LanguageType__English:
            createArg.regionType = Swkbd_RegionType__USA;
            break;
        case Swkbd_LanguageType__Chinese1:
            createArg.regionType = Swkbd_RegionType__China;
            break;
        case Swkbd_LanguageType__Korean:
            createArg.regionType = Swkbd_RegionType__Korea;
            break;
        case Swkbd_LanguageType__Chinese2:
            createArg.regionType = Swkbd_RegionType__Taiwan;
            break;
        default:
            createArg.regionType = Swkbd_RegionType__Europe;
            break;
    }

    if(Swkbd_Create(&createArg))
    {
        OSBlockSet(&appearArg, 0, sizeof(Swkbd_AppearArg));
        appearArg.keyboardArg.configArg.accessFlags = 0xFFFFFFFF;
        appearArg.keyboardArg.configArg.unk_0x14 = -1;
        appearArg.keyboardArg.configArg.framerate = FRAMERATE_60FPS;
        appearArg.keyboardArg.configArg.showCursor = true;
        appearArg.keyboardArg.configArg.unk_0xA4 = -1;
        appearArg.keyboardArg.configArg.disableNewLine = true;

        appearArg.keyboardArg.receiverArg.unk_0x14 = 2;

        appearArg.inputFormArg.unk_0x04 = -1;
        appearArg.inputFormArg.initialText = NULL;
        appearArg.inputFormArg.hintText = NULL;
        appearArg.inputFormArg.pwMode = Swkbd_PW_mode__None;
        appearArg.inputFormArg.unk_0x18 = 0x00008000; // Monospace seperator after 16 chars (for 32 char keys)
        appearArg.inputFormArg.drawInput0Cursor = true;
        appearArg.inputFormArg.higlightInitialText = true;
        appearArg.inputFormArg.showCopyPasteButtons = true;

        return true;
    }

    MEMFreeToDefaultHeap(createArg.workMemory);
    FSDelClient(createArg.fsClient, 0);
    return false;
}

void SWKBD_Shutdown()
{
    Swkbd_Destroy();
    MEMFreeToDefaultHeap(createArg.workMemory);
    FSDelClient(createArg.fsClient, 0);
}

void readInput()
{
    VPADReadError vError;
    VPADRead(VPAD_CHAN_0, &vpad, 1, &vError);
    bool kbdHidden = Swkbd_IsReady() ? Swkbd_IsHidden() : true;

    if(vError != VPAD_READ_SUCCESS)
        OSBlockSet(&vpad, 0, sizeof(VPADStatus));
    else if(vpad.trigger)
    {
        vpad.trigger &= ~(VPAD_STICK_R_EMULATION_LEFT | VPAD_STICK_R_EMULATION_RIGHT | VPAD_STICK_R_EMULATION_UP | VPAD_STICK_R_EMULATION_DOWN | VPAD_BUTTON_HOME);

        if(vpad.trigger & VPAD_STICK_L_EMULATION_UP)
            vpad.trigger |= VPAD_BUTTON_UP;
        if(vpad.trigger & VPAD_STICK_L_EMULATION_DOWN)
            vpad.trigger |= VPAD_BUTTON_DOWN;
        if(vpad.trigger & VPAD_STICK_L_EMULATION_LEFT)
            vpad.trigger |= VPAD_BUTTON_LEFT;
        if(vpad.trigger & VPAD_STICK_L_EMULATION_RIGHT)
            vpad.trigger |= VPAD_BUTTON_RIGHT;

        if(vpad.hold & VPAD_STICK_L_EMULATION_UP)
            vpad.hold |= VPAD_BUTTON_UP;
        if(vpad.hold & VPAD_STICK_L_EMULATION_DOWN)
            vpad.hold |= VPAD_BUTTON_DOWN;
        if(vpad.hold & VPAD_STICK_L_EMULATION_LEFT)
            vpad.hold |= VPAD_BUTTON_LEFT;
        if(vpad.hold & VPAD_STICK_L_EMULATION_RIGHT)
            vpad.hold |= VPAD_BUTTON_RIGHT;

        if(vpad.trigger && kbdHidden)
            lastUsedController = CT_VPAD_0;
    }

    bool altCon = false;
    uint32_t controllerType;
    int32_t controllerProbe;
    uint32_t oldV, oldH;
    KPADStatus *kps = ((KPADStatus *)kpad) + 4;
    KPADError kerr;
    int i = 4;
    bool cont;
    while(i)
    {
        --kps;
        controllerProbe = WPADProbe(--i, &controllerType);
        if(controllerProbe == 0)
        {
            altCon = true;
            KPADReadEx(i, kps, 1, &kerr);
            if(kerr != KPAD_ERROR_OK)
                goto kpadReadError;
        }
        else
            goto kpadReadError;

        oldV = vpad.trigger;
        oldH = vpad.hold;
        cont = false;

        if(controllerType == WPAD_EXT_PRO_CONTROLLER || // With a simple input like ours we're able to handle Wii u pro as Wii classic controllers.
            controllerType == WPAD_EXT_CLASSIC || controllerType == WPAD_EXT_MPLUS_CLASSIC)
        {
            if(kps->classic.trigger)
            {
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_A)
                    vpad.trigger |= VPAD_BUTTON_A;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_B)
                    vpad.trigger |= VPAD_BUTTON_B;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_X)
                    vpad.trigger |= VPAD_BUTTON_X;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_Y)
                    vpad.trigger |= VPAD_BUTTON_Y;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_UP)
                    vpad.trigger |= VPAD_BUTTON_UP;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_DOWN)
                    vpad.trigger |= VPAD_BUTTON_DOWN;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_LEFT)
                    vpad.trigger |= VPAD_BUTTON_LEFT;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_RIGHT)
                    vpad.trigger |= VPAD_BUTTON_RIGHT;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_PLUS)
                    vpad.trigger |= VPAD_BUTTON_PLUS;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_MINUS)
                    vpad.trigger |= VPAD_BUTTON_MINUS;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_R)
                    vpad.trigger |= VPAD_BUTTON_R;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_L)
                    vpad.trigger |= VPAD_BUTTON_L;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_ZR)
                    vpad.trigger |= VPAD_BUTTON_ZR;
                if(kps->classic.trigger & WPAD_CLASSIC_BUTTON_ZL)
                    vpad.trigger |= VPAD_BUTTON_ZL;

                if(kbdHidden && vpad.trigger != oldV)
                    lastUsedController = i;

                cont = true;
            }

            if(kps->classic.hold)
            {
                if(kps->classic.hold & WPAD_CLASSIC_BUTTON_UP)
                    vpad.hold |= VPAD_BUTTON_UP;
                if(kps->classic.hold & WPAD_CLASSIC_BUTTON_DOWN)
                    vpad.hold |= VPAD_BUTTON_DOWN;
                if(kps->classic.hold & WPAD_CLASSIC_BUTTON_LEFT)
                    vpad.hold |= VPAD_BUTTON_LEFT;
                if(kps->classic.hold & WPAD_CLASSIC_BUTTON_RIGHT)
                    vpad.hold |= VPAD_BUTTON_RIGHT;

                if(kbdHidden && vpad.hold != oldH)
                    lastUsedController = i;

                cont = true;
            }

            if(cont)
                continue;
        }

        if(kps->trigger)
        {
            if(kps->trigger & WPAD_BUTTON_A)
                vpad.trigger |= VPAD_BUTTON_A;
            if(kps->trigger & WPAD_BUTTON_B)
                vpad.trigger |= VPAD_BUTTON_B;
            if(kps->trigger & WPAD_BUTTON_1)
                vpad.trigger |= VPAD_BUTTON_X;
            if(kps->trigger & WPAD_BUTTON_2)
                vpad.trigger |= VPAD_BUTTON_Y;
            if(kps->trigger & WPAD_BUTTON_UP)
                vpad.trigger |= VPAD_BUTTON_UP;
            if(kps->trigger & WPAD_BUTTON_DOWN)
                vpad.trigger |= VPAD_BUTTON_DOWN;
            if(kps->trigger & WPAD_BUTTON_LEFT)
                vpad.trigger |= VPAD_BUTTON_LEFT;
            if(kps->trigger & WPAD_BUTTON_RIGHT)
                vpad.trigger |= VPAD_BUTTON_RIGHT;
            if(kps->trigger & WPAD_BUTTON_PLUS)
                vpad.trigger |= VPAD_BUTTON_PLUS;
            if(kps->trigger & WPAD_BUTTON_MINUS)
                vpad.trigger |= VPAD_BUTTON_MINUS;
            if(kps->trigger & WPAD_BUTTON_Z)
                vpad.trigger |= VPAD_BUTTON_ZR;
            if(kps->trigger & WPAD_BUTTON_C)
                vpad.trigger |= VPAD_BUTTON_ZL;

            if(kbdHidden && vpad.trigger != oldV)
                lastUsedController = i;
        }

        if(kps->hold)
        {
            if(kps->hold & WPAD_BUTTON_UP)
                vpad.hold |= VPAD_BUTTON_UP;
            if(kps->hold & WPAD_BUTTON_DOWN)
                vpad.hold |= VPAD_BUTTON_DOWN;
            if(kps->hold & WPAD_BUTTON_LEFT)
                vpad.hold |= VPAD_BUTTON_LEFT;
            if(kps->hold & WPAD_BUTTON_RIGHT)
                vpad.hold |= VPAD_BUTTON_RIGHT;

            if(kbdHidden && vpad.hold != oldH)
                lastUsedController = i;
        }

        continue;

    kpadReadError:
        if(controllerProbe != -1)
            altCon = true;

        OSBlockSet(kps, 0, sizeof(KPADStatus));
    }

    if(vpad.trigger != 0)
    {
        OSTime t = OSGetSystemTime() - lastButtonPress;
        addEntropy(&t, sizeof(OSTime));
        lastButtonPress = t;
    }

    if(!altCon && vError == VPAD_READ_INVALID_CONTROLLER)
    {
        if(io == NULL)
            io = addErrorOverlay("No Controller connected!");
    }
    else if(io != NULL)
    {
        removeErrorOverlay(io);
        io = NULL;
    };
}

bool showKeyboard(KeyboardLayout layout, KeyboardType type, char *output, KeyboardChecks check, int maxlength, bool limit, const char *input, const char *okStr)
{
    debugPrintf("Initialising SWKBD");

    if(maxlength < 1)
        return false;

    SWKBD_Args args;

    if(!SWKBD_Show(&args, layout, type, maxlength, limit, okStr))
    {
        drawErrorFrame("Error showing SWKBD:\nnn::swkbd::AppearInputForm failed", ANY_RETURN);

        while(AppRunning(true))
        {
            showFrame();
            if(vpad.trigger)
                break;
        }

        return false;
    }
    debugPrintf("SWKBD initialised successfully");

    if(input != NULL)
        Swkbd_SetInputFormString(input);

    bool close = false;
    OSTime t = OSGetSystemTime();
    while(AppRunning(true))
    {
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpad.tpFiltered1, &vpad.tpNormal);
        vpad.tpFiltered2 = vpad.tpNormal = vpad.tpFiltered1;
        SWKBD_Render(&args, check);
        //		sleepTillFrameEnd();

        if(args.okButtonEnabled && (Swkbd_IsDecideOkButton(&close) || vpad.trigger & VPAD_BUTTON_PLUS))
        {
            debugPrintf("SWKBD Ok button pressed");
            strcpy(output, Swkbd_GetInputFormString());
            close = true;
            break;
        }

        close = vpad.trigger & VPAD_BUTTON_B || vpad.trigger & VPAD_BUTTON_MINUS;
        if(close)
        {
            char *inputFormString = Swkbd_GetInputFormString();
            if(inputFormString != NULL)
                close = strlen(inputFormString) == 0;
        }

        if(close || Swkbd_IsDecideCancelButton(&close))
        {
            debugPrintf("SWKBD Cancel button pressed");
            close = false;
            break;
        }

        close = false;
    }

    SWKBD_Hide(&args);
    t = OSGetSystemTime() - t;
    addEntropy(&t, sizeof(OSTime));
    return close;
}
