/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2024 V10lator <v10lator@myway.de>                    *
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

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <config.h>
#include <deinstaller.h>
#include <downloader.h>
#include <filesystem.h>
#include <input.h>
#include <menu/predownload.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <state.h>
#include <titles.h>
#include <tmd.h>
#include <ui.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define PD_MENU_ENTRIES 5

typedef enum
{
    PD_LINE_INSTALL_DEVICE = 15,
    PD_LINE_OPERATION = 16,
    PD_LINE_DOWNLOAD_DEVICE = 17,
    PD_LINE_KEEP_FILES = 18,
    PD_LINE_TITLE_VERSION = 19,
    PD_LINE_FOLDER_NAME = 20,
} PD_LINE;

static int cursorPos = PD_LINE_INSTALL_DEVICE;
static OPERATION operation = OPERATION_DOWNLOAD_INSTALL;
static bool keepFiles = true;
static NUSDEV dlDev = NUSDEV_NONE;
static NUSDEV instDev = NUSDEV_NONE;

// State of the current visit: everything the original kept in the locals
// of predownloadMenu(), reset by the wrapper before the screen is pushed.
static const TitleEntry *pdEntry;
static char pdFolderName[FS_MAX_PATH - 11];
static char pdTitleVer[33];
static MCPTitleListType pdTitleList __attribute__((__aligned__(0x40)));
static RAMBUF *pdRambuf;
static TMD *pdTmd;
static uint64_t pdDls;
static NUSDEV pdUsbMounted;
static NUSDEV pdForcedInstDev;
static bool pdInstalled;
static bool pdMenuActive;
static bool pdToQueue;
static bool pdAutoAddToQueue;
static bool pdAutoStartQueue;
static void *pdOverlay;

static inline bool isInstalled(const TitleEntry *entry, MCPTitleListType *out)
{
    if(out == NULL)
    {
        MCPTitleListType titleList __attribute__((__aligned__(0x40)));
        return MCP_GetTitleInfo(mcpHandle, entry->tid, &titleList) == 0;
    }
    return MCP_GetTitleInfo(mcpHandle, entry->tid, out) == 0;
}

static void renderPDMenu()
{
    startNewFrame();

    textToFrame(0, 0, localise("Name:"));

    char toFrame[512];
    strcpy(toFrame, pdEntry->name);
    char tid[17];
    hex(pdEntry->tid, 16, tid);
    strcat(toFrame, " [");
    strcat(toFrame, tid);
    strcat(toFrame, "]");
    int line = textToFrameMultiline(0, ALIGNED_CENTER, toFrame, MAX_CHARS - 33); // 33 below full width: centred that leaves ~16 chars of margin per side so the name can never overlap the "Name:" label at the left edge of line 0

    humanize(pdDls, toFrame);

    textToFrame(++line, 0, localise("Region:"));
    flagToFrame(++line, 3, pdEntry->region);
    textToFrame(line, 7, localise(getFormattedRegion(pdEntry->region)));

    textToFrame(++line, 0, localise("Size:"));
    textToFrame(++line, 3, toFrame);

    strcpy(toFrame, localise("Provided title version"));
    strcat(toFrame, " [");
    strcat(toFrame, localise("Only numbers"));
    strcat(toFrame, "]:");
    textToFrame(++line, 0, toFrame);

    if(pdTitleVer[0] == '\0')
    {
        toFrame[0] = '<';
        strcpy(toFrame + 1, localise("LATEST"));
        strcat(toFrame, ">");
        textToFrame(++line, 3, toFrame);
    }
    else
        textToFrame(++line, 3, pdTitleVer);

    strcpy(toFrame, localise("Custom folder name"));
    strcat(toFrame, " [");
    strcat(toFrame, localise("ASCII only"));
    strcat(toFrame, "]:");
    textToFrame(++line, 0, toFrame);
    textToFrame(++line, 3, pdFolderName);

    line = MAX_LINES;

    arrowToFrame(cursorPos, 0);

    strcpy(toFrame, localise(BUTTON_MINUS " to add to the queue"));
    if(pdInstalled)
    {
        strcat(toFrame, " || ");
        strcat(toFrame, localise(BUTTON_Y " to uninstall"));
    }
    textToFrame(--line, ALIGNED_CENTER, toFrame);

    strcpy(toFrame, localise("Press " BUTTON_B " to return"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_PLUS " to start"));
    textToFrame(--line, ALIGNED_CENTER, toFrame);

    lineToFrame(--line, SCREEN_COLOR_WHITE);

    textToFrame(--line, 4, localise("Set custom name to the download folder"));
    textToFrame(--line, 4, localise("Set title version"));

    if(operation == OPERATION_DOWNLOAD)
        keepFiles = true;
    else
    {
        switch((int)dlDev)
        {
            case NUSDEV_USB01:
            case NUSDEV_USB02:
            case NUSDEV_MLC:
                keepFiles = false;
        }
    }

    strcpy(toFrame, localise("Keep downloaded files:"));
    strcat(toFrame, " ");
    strcat(toFrame, localise(keepFiles ? "Yes" : "No"));
    // Localised on its own above, the composed line is not a key itself.
    if(dlDev == NUSDEV_SD && operation == OPERATION_DOWNLOAD_INSTALL)
        textToFrame(--line, 4, toFrame);
    else
        textToFrameColored(--line, 4, toFrame, SCREEN_COLOR_WHITE_TRANSP);

    strcpy(toFrame, localise("Download to:"));
    strcat(toFrame, " ");
    switch((int)dlDev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            strcat(toFrame, "USB");
            break;
        case NUSDEV_SD:
            strcat(toFrame, "SD");
            break;
        case NUSDEV_MLC:
            strcat(toFrame, "NAND");
    }

    getFreeSpaceString(dlDev, toFrame + strlen(toFrame));
    textToFrame(--line, 4, toFrame);

    strcpy(toFrame, localise("Operation:"));
    strcat(toFrame, " ");
    switch((int)operation)
    {
        case OPERATION_DOWNLOAD:
            strcat(toFrame, localise("Download only"));
            break;
        case OPERATION_DOWNLOAD_INSTALL:
            strcat(toFrame, localise("Install"));
            break;
    }
    if(pdForcedInstDev == NUSDEV_NONE)
        textToFrame(--line, 4, toFrame);
    else
        textToFrameColored(--line, 4, toFrame, SCREEN_COLOR_WHITE_TRANSP);

    strcpy(toFrame, localise("Install to:"));
    strcat(toFrame, " ");
    switch((int)instDev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            strcat(toFrame, "USB");
            break;
        case NUSDEV_MLC:
            strcat(toFrame, "NAND");
            break;
    }

    getFreeSpaceString(instDev, toFrame + strlen(toFrame));

    if(pdForcedInstDev == NUSDEV_NONE && operation == OPERATION_DOWNLOAD_INSTALL)
        textToFrame(--line, 4, toFrame);
    else
        textToFrameColored(--line, 4, toFrame, SCREEN_COLOR_WHITE_TRANSP);

    lineToFrame(--line, SCREEN_COLOR_WHITE);
}

static void *drawPDWrongDeviceFrame(NUSDEV dev)
{
    char toFrame[512];
    strcpy(toFrame, localise("The main game is installed to"));
    strcat(toFrame, " ");
    strcat(toFrame, dev & NUSDEV_USB ? "USB" : "NAND");
    strcat(toFrame, "\n");
    strcat(toFrame, localise("Do you want to change the target device to this?"));
    strcat(toFrame, "\n\n" BUTTON_A " ");
    strcat(toFrame, localise("Yes"));
    strcat(toFrame, " || " BUTTON_B " ");
    strcat(toFrame, localise("No"));

    return uiShowOverlay(toFrame);
}

static void *drawPDMainGameFrame(const TitleEntry *entry)
{
    char toFrame[512];
    strcpy(toFrame, entry->name);
    strcat(toFrame, "\n");
    strcat(toFrame, localise(isDLC(entry->tid) ? "is DLC." : (isUpdate(entry->tid) ? "is a update." : "is a demo.")));
    strcat(toFrame, "\n\n" BUTTON_A " ");
    strcat(toFrame, localise(operation == OPERATION_DOWNLOAD_INSTALL ? "Install main game" : "Download main game"));
    strcat(toFrame, " || " BUTTON_B " ");
    strcat(toFrame, localise("Continue"));

    return uiShowOverlay(toFrame);
}

static void *drawPDUpdateFrame(const TitleEntry *entry)
{
    char toFrame[512];
    strcpy(toFrame, entry->name);
    strcat(toFrame, "\n");
    strcat(toFrame, localise("Has an update available."));
    strcat(toFrame, "\n\n" BUTTON_A " ");
    strcat(toFrame, localise(operation == OPERATION_DOWNLOAD_INSTALL ? "Install the update, too" : "Download the update, too"));
    strcat(toFrame, " || " BUTTON_B " ");
    strcat(toFrame, localise("Continue"));

    return uiShowOverlay(toFrame);
}

static inline void changeTitleVersion(char *buf)
{
    if(!showKeyboard(KEYBOARD_MODE_TID, KEYBOARD_TYPE_RESTRICTED, buf, CHECK_NUMERICAL, 5, false, buf, NULL))
        buf[0] = '\0';
}

static inline void changeFolderName(char *buf)
{
    if(!showKeyboard(KEYBOARD_MODE_TID, KEYBOARD_TYPE_NORMAL, buf, CHECK_ALPHANUMERICAL, MAX_FOLDER_NAME_LENGTH, false, buf, NULL))
        buf[0] = '\0';
}

static inline void switchInstallDevice()
{
    switch((int)instDev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            instDev = NUSDEV_MLC;
            break;
        case NUSDEV_MLC:
            instDev = getUSB();
            if(!instDev)
                instDev = NUSDEV_MLC;
            break;
    }
}

static inline void switchOperation()
{
    switch((int)operation)
    {
        case OPERATION_DOWNLOAD_INSTALL:
            operation = OPERATION_DOWNLOAD;
            break;
        case OPERATION_DOWNLOAD:
            operation = OPERATION_DOWNLOAD_INSTALL;
            break;
    }
}

static inline void switchDownloadDevice()
{
    bool toUSB = false;

    if(vpad.trigger & VPAD_BUTTON_LEFT)
    {
        switch((int)dlDev)
        {
            case NUSDEV_USB01:
            case NUSDEV_USB02:
                dlDev = NUSDEV_MLC;
                break;
            case NUSDEV_MLC:
                dlDev = NUSDEV_SD;
                break;
            case NUSDEV_SD:
                dlDev = getUSB();
                if(!dlDev)
                    dlDev = NUSDEV_MLC;
                else
                    toUSB = true;
                break;
        }
    }
    else
    {
        switch((int)dlDev)
        {
            case NUSDEV_USB01:
            case NUSDEV_USB02:
                dlDev = NUSDEV_SD;
                break;
            case NUSDEV_SD:
                dlDev = NUSDEV_MLC;
                break;
            case NUSDEV_MLC:
                dlDev = getUSB();
                if(!dlDev)
                    dlDev = NUSDEV_SD;
                else
                    toUSB = true;
                break;
        }
    }

    setDlToUSB(toUSB);
}

static bool addToOpQueue(RAMBUF *rambuf, const TitleEntry *entry, const char *titleVer, const char *folderName)
{
    TitleData *titleInfo = MEMAllocFromDefaultHeap(sizeof(TitleData));
    int ret = false;
    if(titleInfo != NULL)
    {
        titleInfo->tmd = (TMD *)rambuf->buf;
        titleInfo->tmdSize = rambuf->size;
        titleInfo->rambuf = rambuf;
        titleInfo->entry = entry;
        strcpy(titleInfo->titleVer, titleVer);
        strcpy(titleInfo->folderName, folderName);
        titleInfo->operation = operation;
        titleInfo->dlDev = dlDev;
        titleInfo->toUSB = instDev & NUSDEV_USB;
        titleInfo->keepFiles = keepFiles;

        ret = addToQueue(titleInfo);
        if(ret == 1)
            return true;

        MEMFreeToDefaultHeap(titleInfo);

        // 2 = already queued for install, 3 = already queued for download: the queue owns
        // neither our TitleData nor our rambuf, so drop the rambuf ourselves but report
        // success so the caller doesn't free it a second time.
        if(ret == 2 || ret == 3)
        {
            freeRamBuf(rambuf);
            addToScreenLog("\"%s\" is already queued", entry->name);
            return true;
        }
    }

    return ret;
}

// Leaves the settings screen and hands the result back to the wrapper.
static void pdFinish(int result)
{
    uiSetResult(result);
    uiPop();
}

// Original label naNedNa: toQueue follows autoAddToQueue. Without auto
// queueing the settings menu comes back, otherwise the post-menu phase
// continues right away. Returns true when the post phase continues.
static bool pdNaNedNa()
{
    pdToQueue = pdAutoAddToQueue;
    pdMenuActive = !pdToQueue;
    return pdToQueue;
}

// Original label downloadTMD: fetches the TMD into pdRambuf and fills
// pdTmd/pdDls. On failure the buffers are dropped and false is returned;
// the original then returned true from predownloadMenu.
static bool pdFetchTmd()
{
    if(pdRambuf != NULL)
    {
        freeRamBuf(pdRambuf);
        pdRambuf = NULL;
    }

    pdRambuf = allocRamBuf();
    if(pdRambuf == NULL)
        return false;

    char tid[17];
    hex(pdEntry->tid, 16, tid);

    debugPrintf("Downloading TMD...");
    char downloadUrl[256];
    strcpy(downloadUrl, DOWNLOAD_URL);
    strcat(downloadUrl, tid);
    strcat(downloadUrl, "/tmd");

    if(strlen(pdTitleVer) > 0)
    {
        strcat(downloadUrl, ".");
        strcat(downloadUrl, pdTitleVer);
    }

    if(downloadFile(downloadUrl, "title.tmd", NULL, (FileType)(FILE_TYPE_TMD | FILE_TYPE_TORAM), false, NULL, pdRambuf))
    {
        freeRamBuf(pdRambuf);
        pdRambuf = NULL;
        debugPrintf("Error downloading TMD");
        saveConfig(false);
        return false;
    }

    pdTmd = (TMD *)pdRambuf->buf;
    if(verifyTmd(pdTmd, pdRambuf->size) != TMD_STATE_GOOD)
    {
        freeRamBuf(pdRambuf);
        pdRambuf = NULL;
        saveConfig(false);
        showErrorFrame(localise("Invalid title.tmd file!"));
        return false;
    }

    pdDls = 0;
    for(uint16_t i = 0; i < pdTmd->num_contents; ++i)
    {
        if(pdTmd->contents[i].type & TMD_CONTENT_TYPE_HASHED)
            pdDls += getH3size(pdTmd->contents[i].size);

        pdDls += pdTmd->contents[i].size;
    }

    return true;
}

// The pre-selection questions stay overlays on the frame below, so they
// are pushed while the settings screen is still on the stack (A/B).
static void updatePDAsk()
{
    if(vpad.trigger & VPAD_BUTTON_B)
        uiPop();
    else if(vpad.trigger & VPAD_BUTTON_A)
    {
        uiSetResult(1);
        uiPop();
    }
}

static void leavePDAsk()
{
    uiHideOverlay(pdOverlay);
    pdOverlay = NULL;
}

static const UIScreen pdAskScreen = {
    .name = "predownload question",
    .buttons = VPAD_BUTTON_A | VPAD_BUTTON_B,
    .update = updatePDAsk,
    .leave = leavePDAsk,
};

// A/B question drawn as an overlay on the frame below. Returns 1 on A,
// 0 on B and -1 when there is no overlay or the app stopped (give up,
// the original jumped to exitPDM in both cases). The overlay is gone
// when this returns.
static int pdAskAnswer(void *ovl)
{
    if(ovl == NULL)
        return -1;

    pdOverlay = ovl;
    int ret = uiModal(&pdAskScreen, NULL);
    if(!AppRunning(true))
        return -1;

    return ret;
}

// The code behind the original settings loop: the pre-selection dialogs,
// the "preparing" frame and the download/queue itself. The labels keep the
// original jump structure (runPost, downloadTMD, naNedNa; the old exitPDM
// exits became pdFinish() calls).
static void pdRunPost()
{
    bool ret = false;

runPost:
    if(!AppRunning(true))
    {
        pdFinish(0);
        return;
    }

    if(!pdAutoAddToQueue)
    {
        if(dlDev == NUSDEV_MLC)
        {
            char txt[512];
            sprintf(txt,
                "%s\n\n" BUTTON_A " %s || " BUTTON_B " %s",
                localise(
                    "Downloading to NAND is dangerous,\n"
                    "it could brick your Wii U!\n\n"

                    "Are you sure you want to do this?"),
                localise("Yes"),
                localise("No"));

            int ans = pdAskAnswer(uiShowOverlay(txt));
            if(ans < 0)
            {
                pdFinish(0);
                return;
            }

            if(!ans) // B: back to the settings menu
                goto naNedNa;
        }

        if(isDemo(pdEntry->tid))
        {
            uint64_t t = DEMO_TO_GAME(pdEntry->tid);
            const TitleEntry *te = getTitleEntryByTid(t);
            if(te != NULL && te->key != TITLE_KEY_MAGIC)
            {
                int ans = pdAskAnswer(drawPDMainGameFrame(pdEntry));
                if(ans < 0)
                {
                    pdFinish(0);
                    return;
                }

                if(ans)
                {
                    pdEntry = te;
                    goto downloadTMD;
                }
            }
        }
        else if(!pdForcedInstDev && (isDLC(pdEntry->tid) || isUpdate(pdEntry->tid)))
        {
            MCPTitleListType tl __attribute__((__aligned__(0x40)));
            uint64_t t = TID_TO_BASE(pdEntry->tid);
            if(MCP_GetTitleInfo(mcpHandle, t, &tl) == 0)
            {
                if(operation == OPERATION_DOWNLOAD_INSTALL)
                {
                    NUSDEV toDev = tl.indexedDevice[0] == 'u' ? NUSDEV_USB : NUSDEV_MLC;
                    if(!(toDev & instDev))
                    {
                        int ans = pdAskAnswer(drawPDWrongDeviceFrame(toDev));
                        if(ans < 0)
                        {
                            pdFinish(0);
                            return;
                        }

                        if(ans)
                        {
                            instDev = toDev;
                            if(instDev == NUSDEV_USB)
                                instDev = pdUsbMounted;
                        }
                    }
                }
            }
            else // main game not installed
            {
                const TitleEntry *te = getTitleEntryByTid(t);
                if(te != NULL && te->key != TITLE_KEY_MAGIC)
                {
                    int ans = pdAskAnswer(drawPDMainGameFrame(pdEntry));
                    if(ans < 0)
                    {
                        pdFinish(0);
                        return;
                    }

                    if(ans)
                    {
                        if(!checkFreeSpace(dlDev, pdDls))
                        {
                            if(AppRunning(true))
                                goto naNedNa;

                            pdFinish(0);
                            return;
                        }

                        if(!addToOpQueue(pdRambuf, pdEntry, pdTitleVer, pdFolderName))
                        {
                            pdFinish(0);
                            return;
                        }

                        pdRambuf = NULL;
                        pdEntry = te;
                        pdAutoAddToQueue = true;

                        if(!pdToQueue)
                            pdAutoStartQueue = true;

                        goto downloadTMD;
                    }
                }
            }
        }
        else if(isGame(pdEntry->tid))
        {
            uint64_t t = BASE_TO_UPDATE(pdEntry->tid);
            const TitleEntry *te = getTitleEntryByTid(t);
            if(te != NULL) // Update available
            {
                MCPTitleListType tl __attribute__((__aligned__(0x40)));
                if(MCP_GetTitleInfo(mcpHandle, t, &tl) != 0) // Update not installed
                {
                    int ans = pdAskAnswer(drawPDUpdateFrame(pdEntry));
                    if(ans < 0)
                    {
                        pdFinish(0);
                        return;
                    }

                    if(ans)
                    {
                        if(!checkFreeSpace(dlDev, pdDls))
                        {
                            if(AppRunning(true))
                                goto naNedNa;

                            pdFinish(0);
                            return;
                        }

                        if(!addToOpQueue(pdRambuf, pdEntry, pdTitleVer, pdFolderName))
                        {
                            pdFinish(0);
                            return;
                        }

                        pdRambuf = NULL;
                        pdEntry = te;
                        pdAutoAddToQueue = true;

                        if(!pdToQueue)
                            pdAutoStartQueue = true;

                        goto downloadTMD;
                    }
                }
            }
        }
    }

    startNewFrame();
    textToFrame(0, 0, localise("Preparing the download of"));
    textToFrame(1, 3, pdEntry->name);
    writeScreenLog(2);
    drawFrame();

    saveConfig(false);

    if(!checkFreeSpace(dlDev, pdDls))
    {
        if(AppRunning(true))
            goto naNedNa;

        pdFinish(0);
        return;
    }

    if(!AppRunning(true))
    {
        pdFinish(0);
        return;
    }

    if(pdToQueue)
    {
        ret = addToOpQueue(pdRambuf, pdEntry, pdTitleVer, pdFolderName);
        if(ret)
        {
            pdRambuf = NULL;
            if(pdAutoStartQueue)
            {
                disableApd();
                ret = !proccessQueue();
                enableApd();
                if(!ret)
                    showFinishedScreen(pdEntry->name, operation == OPERATION_DOWNLOAD_INSTALL ? FINISHING_OPERATION_INSTALL : FINISHING_OPERATION_DOWNLOAD);
            }
        }
    }
    else if(checkSystemTitleFromEntry(pdEntry, false))
    {
        disableApd();
        ret = !downloadTitle(pdTmd, pdRambuf->size, pdEntry, pdTitleVer, pdFolderName, operation == OPERATION_DOWNLOAD_INSTALL, dlDev, instDev & NUSDEV_USB, keepFiles, NULL);
        enableApd();
        if(!ret)
            showFinishedScreen(pdEntry->name, operation == OPERATION_DOWNLOAD_INSTALL ? FINISHING_OPERATION_INSTALL : FINISHING_OPERATION_DOWNLOAD);
    }
    else
        ret = true;

    // Was label exitPDM: the wrapper frees what is left of pdRambuf.
    pdFinish(ret);
    return;

downloadTMD:
    if(!pdFetchTmd())
    {
        pdFinish(1);
        return;
    }

naNedNa:
    if(!pdNaNedNa())
        return; // the settings menu runs again next frame

    goto runPost;
}

static void updatePDMenu()
{
    if(pdMenuActive)
    {
        if(vpad.trigger & VPAD_BUTTON_B)
        {
            freeRamBuf(pdRambuf);
            pdRambuf = NULL;
            saveConfig(false);
            pdFinish(1);
            return;
        }

        if(vpad.trigger & (VPAD_BUTTON_RIGHT | VPAD_BUTTON_LEFT | VPAD_BUTTON_A))
        {
            switch(cursorPos)
            {
                case PD_LINE_INSTALL_DEVICE:
                    if(operation == OPERATION_DOWNLOAD_INSTALL && pdForcedInstDev == NUSDEV_NONE)
                        switchInstallDevice();
                    break;
                case PD_LINE_OPERATION:
                    if(pdForcedInstDev == NUSDEV_NONE)
                        switchOperation();
                    break;
                case PD_LINE_DOWNLOAD_DEVICE:
                    switchDownloadDevice();
                    break;
                case PD_LINE_KEEP_FILES:
                    if(dlDev == NUSDEV_SD && operation == OPERATION_DOWNLOAD_INSTALL)
                        keepFiles = !keepFiles;
                    break;
                case PD_LINE_TITLE_VERSION:
                    changeTitleVersion(pdTitleVer);
                    if(!pdFetchTmd())
                    {
                        pdFinish(1);
                        return;
                    }

                    if(pdNaNedNa()) // never true: the menu only runs without auto queueing
                        pdRunPost();
                    return;
                case PD_LINE_FOLDER_NAME:
                    changeFolderName(pdFolderName);
                    break;
            }
        }
        else if(vpad.trigger & VPAD_BUTTON_DOWN)
        {
            if(++cursorPos == PD_LINE_FOLDER_NAME + 1)
                cursorPos = PD_LINE_INSTALL_DEVICE;
        }
        else if(vpad.trigger & VPAD_BUTTON_UP)
        {
            if(--cursorPos == PD_LINE_INSTALL_DEVICE - 1)
                cursorPos = PD_LINE_FOLDER_NAME;
        }

        if(vpad.trigger & VPAD_BUTTON_PLUS)
            pdMenuActive = false;
        else if(vpad.trigger & VPAD_BUTTON_MINUS)
        {
            pdToQueue = true;
            pdMenuActive = false;
        }
        else
        {
            if(pdInstalled && vpad.trigger & VPAD_BUTTON_Y)
            {
                if(checkSystemTitleFromListType(&pdTitleList, true))
                {
                    freeRamBuf(pdRambuf);
                    pdRambuf = NULL;

                    // Same guard as in insttitlebrowserMenu: nothing is
                    // removed while the app is on its way down. The answer of
                    // the confirmation dialogs comes from before them, so the
                    // deletion asks the state one more time right in front of
                    // itself.
                    if(AppRunning(true))
                    {
                        saveConfig(false);
                        deinstall(&pdTitleList, pdEntry->name, false, false);
                    }

                    pdFinish(0);
                    return;
                }
            }

            return; // nothing that leaves the menu: next frame
        }
    }

    pdRunPost();
}

static const UIScreen pdScreen = {
    .name = "predownload",
    .buttons = VPAD_BUTTON_A | VPAD_BUTTON_B | VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT | VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS | VPAD_BUTTON_Y,
    .update = updatePDMenu,
    .render = renderPDMenu,
};

bool predownloadMenu(const TitleEntry *entry, NUSDEV forcedInstDev)
{
    pdEntry = entry;
    pdForcedInstDev = forcedInstDev;
    pdFolderName[0] = pdTitleVer[0] = '\0';
    pdRambuf = NULL;
    pdTmd = NULL;
    pdAutoAddToQueue = false;
    pdAutoStartQueue = false;
    pdInstalled = isInstalled(entry, &pdTitleList);

    NUSDEV usbMounted = getUSB();
    pdUsbMounted = usbMounted;
    if(dlDev == NUSDEV_NONE)
        dlDev = usbMounted && dlToUSBenabled() ? usbMounted : NUSDEV_SD;
    if(forcedInstDev == NUSDEV_NONE)
    {
        if(instDev == NUSDEV_NONE)
            instDev = usbMounted ? usbMounted : NUSDEV_MLC;
    }
    else
    {
        instDev = forcedInstDev;
        operation = OPERATION_DOWNLOAD_INSTALL;
    }

    if(!pdFetchTmd())
        return true;

    pdNaNedNa(); // first naNedNa: autoAddToQueue is false, the menu runs

    int ret = uiModal(&pdScreen, NULL);

    if(pdRambuf != NULL)
    {
        freeRamBuf(pdRambuf);
        pdRambuf = NULL;
    }

    return ret != 0;
}
