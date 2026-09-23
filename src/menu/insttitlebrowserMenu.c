/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
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

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include <deinstaller.h>
#include <file.h>
#include <input.h>
#include <localisation.h>
#include <menu/insttitlebrowser.h>
#include <menu/utils.h>
#include <osdefs.h>
#include <renderer.h>
#include <state.h>
#include <thread.h>
#include <titles.h>
#include <ui.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <nn/acp/title.h>
#pragma GCC diagnostic pop

#define MAX_ITITLEBROWSER_LINES        (MAX_LINES - 3)
#define MAX_ITITLEBROWSER_TITLE_LENGTH (MAX_TITLENAME_LENGTH >> 1)

typedef struct
{
    char name[MAX_ITITLEBROWSER_TITLE_LENGTH];
    MCPRegion region;
    bool isDlc;
    bool isUpdate;
    DEVICE_TYPE dt;
    spinlock lock;
    bool ready;
} INST_META;

typedef enum
{
    ASYNC_STATE_EXIT = 0,
    ASYNC_STATE_FWD,
    ASYNC_STATE_BKWD
} ASYNC_STATE;

static volatile INST_META *installedTitles;
static MCPTitleListType *ititleEntries;
static size_t ititleEntrySize;
static volatile ASYNC_STATE asyncState;

// The old loop locals: a fresh visit starts at the top while returning
// from the uninstall confirmation keeps the position (the original jumped
// back to loopEntry instead of restarting), so they are reset in
// ititleBrowserMenu() and not in an enter handler.
static size_t itbCursor;
static size_t itbPos;
static bool itbMov;
static uint32_t itbOldHold;
static size_t itbFrameCount;
static bool itbDpadAction;
static void *itbOverlay;

static volatile INST_META *getInstalledTitle(size_t index, bool block)
{
    volatile INST_META *title = installedTitles + index;
    if(title->ready)
        return title;

    if(block)
    {
        spinLock(title->lock);
    }
    else if(!spinTryLock(title->lock))
        return NULL;

    if(!title->ready)
    {
        MCPTitleListType *list = ititleEntries + index;
        switch(list->indexedDevice[0])
        {
            case 'u':
                title->dt = DEVICE_TYPE_USB;
                break;
            case 'm':
                title->dt = DEVICE_TYPE_NAND;
                break;
            default: // TODO: bt. drh, slc
                title->dt = DEVICE_TYPE_UNKNOWN;
        }

        const TitleEntry *e = getTitleEntryByTid(list->titleId);
        if(e)
        {
            strncpy((char *)title->name, e->name, MAX_ITITLEBROWSER_TITLE_LENGTH - 1);
            title->name[MAX_ITITLEBROWSER_TITLE_LENGTH - 1] = '\0';

            title->region = e->region;
            title->isDlc = isDLC(list->titleId);
            title->isUpdate = isUpdate(list->titleId);
            goto finishExit;
        }

        switch(getTidHighFromTid(list->titleId))
        {
            case TID_HIGH_UPDATE:
                title->isDlc = false;
                title->isUpdate = true;
                break;
            case TID_HIGH_DLC:
                title->isDlc = true;
                title->isUpdate = false;
                break;
            default:
                title->isDlc = title->isUpdate = false;
        }

        ACPMetaXml meta __attribute__((__aligned__(0x40)));
        if(ACPGetTitleMetaXmlByTitleListType(list, &meta) == ACP_RESULT_SUCCESS)
        {
            size_t len = strlen(meta.longname_en);
            if(++len <= MAX_ITITLEBROWSER_TITLE_LENGTH) // len includes the terminator, 128 bytes fit exactly
            {
                if(strcmp(meta.longname_en, "Long Title Name (EN)"))
                {
                    OSBlockMove((void *)title->name, meta.longname_en, len, false);
                    for(char *buf = (char *)title->name; *buf != '\0'; ++buf)
                        if(*buf == '\n')
                            *buf = ' ';

                    title->region = meta.region;
                    goto finishExit;
                }
            }
        }

        hex(list->titleId, 16, (char *)title->name);
        title->region = MCP_REGION_UNKNOWN;

    finishExit:
        title->ready = true;
    }

    spinReleaseLock(title->lock);
    return title;
}

static int asyncTitleLoader(int argc, const char **argv)
{
    (void)argc;
    (void)argv;

    size_t min = MAX_ITITLEBROWSER_LINES >> 1;
    size_t max = ititleEntrySize - 1;
    size_t cur;

    // A worker thread, not a UI loop: the stop signal is ASYNC_STATE_EXIT,
    // set in ititleBrowserMenu's cleanup.
    while(min <= max && AppRunning(false))
    {
        switch(asyncState)
        {
            case ASYNC_STATE_FWD:
                cur = min++;
                break;
            case ASYNC_STATE_BKWD:
                cur = max--;
                break;
            case ASYNC_STATE_EXIT:
                goto asyncExit;
        }

        getInstalledTitle(cur, false); // cur is initialised, don't listen to the compiler!
    }

asyncExit:
    return 0;
}

static void renderITBMenu()
{
    startNewFrame();
    boxToFrame(0, MAX_LINES - 2);

    char toFrame[512];
    strcpy(toFrame, localise("Press " BUTTON_PLUS " to launch"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_MINUS " to delete"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_B " to return"));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toFrame);

    size_t max = ititleEntrySize - itbPos;
    if(max > MAX_ITITLEBROWSER_LINES)
        max = MAX_ITITLEBROWSER_LINES;

    volatile INST_META *im;
    for(size_t i = 0, l = 1; i < max; ++i, ++l)
    {
        im = getInstalledTitle(itbPos + i, true);
        if(im->isDlc)
            strcpy(toFrame, "[DLC] ");
        else if(im->isUpdate)
            strcpy(toFrame, "[UPD] ");
        else
            toFrame[0] = '\0';

        if(itbCursor == i)
            arrowToFrame(l, 1);

        deviceToFrame(l, 4, im->dt);
        flagToFrame(l, 7, im->region);
        strcat(toFrame, (const char *)im->name);
        textToFrameCut(l, 10, toFrame, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * 11));
    }
}

static OSThread *initITBMenu()
{
    int32_t r = MCP_TitleCount(mcpHandle);
    if(r > 0)
    {
        uint32_t s = sizeof(MCPTitleListType) * (uint32_t)r;
        ititleEntries = (MCPTitleListType *)MEMAllocFromDefaultHeapEx(s, 0x40);
        if(ititleEntries)
        {
            r = MCP_TitleList(mcpHandle, &s, ititleEntries, s);
            if(r >= 0)
            {
                installedTitles = (INST_META *)MEMAllocFromDefaultHeap(s * sizeof(INST_META));
                if(installedTitles)
                {
                    for(size_t i = 0; i < s; ++i)
                    {
                        spinCreateLock(installedTitles[i].lock, SPINLOCK_FREE);
                        installedTitles[i].ready = false;
                    }

                    ititleEntrySize = s;
                    asyncState = ASYNC_STATE_FWD;
                    OSThread *ret = startThread("NUSspli title loader", THREAD_PRIORITY_MEDIUM, STACKSIZE_MEDIUM, asyncTitleLoader, 0, NULL, OS_THREAD_ATTRIB_AFFINITY_CPU0);
                    if(ret)
                        return ret;

                    MEMFreeToDefaultHeap((void *)installedTitles);
                }
                else
                    debugPrintf("Insttitlebrowser: OUT OF MEMORY!");
            }
            else
                debugPrintf("Insttitlebrowser: MCP_TitleList() returned %d", r);

            MEMFreeToDefaultHeap(ititleEntries);
        }
        else
            debugPrintf("Insttitlebrowser: OUT OF MEMORY!");
    }
    else
        debugPrintf("Insttitlebrowser: MCP_TitleCount() returned %d", r);

    return NULL;
}

// The uninstall confirmation stays an overlay on the frame below, so it is
// pushed while the list screen is still on the stack (A = yes, B = no).
static void updateITBConfirm()
{
    if(vpad.trigger & VPAD_BUTTON_B)
        uiPop();
    else if(vpad.trigger & VPAD_BUTTON_A)
    {
        uiSetResult(1);
        uiPop();
    }
}

static void leaveITBConfirm()
{
    uiHideOverlay(itbOverlay);
    itbOverlay = NULL;
}

static const UIScreen itbConfirmScreen = {
    .name = "uninstall confirmation",
    .buttons = VPAD_BUTTON_A | VPAD_BUTTON_B,
    .update = updateITBConfirm,
    .leave = leaveITBConfirm,
};

static void updateITBMenu()
{
    if(vpad.trigger & VPAD_BUTTON_PLUS)
    {
        launchTitle(ititleEntries + itbCursor + itbPos);
        uiPop();
        return;
    }

    if(vpad.trigger & VPAD_BUTTON_MINUS)
    {
        volatile INST_META *im = installedTitles + itbCursor + itbPos;
        char toFrame[512];
        strcpy(toFrame, localise("Do you really want to uninstall"));
        strcat(toFrame, "\n");
        strcat(toFrame, (char *)im->name);
        strcat(toFrame, "\n");
        strcat(toFrame, localise("from your"));
        strcat(toFrame, " ");
        strcat(toFrame, im->dt == DEVICE_TYPE_USB ? "USB" : im->dt == DEVICE_TYPE_NAND ? "NAND"
                                                                                       : localise("unknown"));
        strcat(toFrame, " ");
        strcat(toFrame, localise("drive?"));
        strcat(toFrame, "\n\n" BUTTON_A " ");
        strcat(toFrame, localise("Yes"));
        strcat(toFrame, " || " BUTTON_B " ");
        strcat(toFrame, localise("No"));

        itbOverlay = uiShowOverlay(toFrame);
        if(itbOverlay == NULL)
        {
            uiPop(); // the original bailed out to instExit
            return;
        }

        if(uiModal(&itbConfirmScreen, NULL) == 0)
            return; // B (or the app stopped): back to the list

        MCPTitleListType *entry = ititleEntries + itbCursor + itbPos;
        if(checkSystemTitleFromListType(entry, true) && AppRunning(true)) // entry is initialised, the compiler just can't follow
        {
            deinstall(entry, (const char *)im->name, false, false);
            uiPop(); // done, the original fell through to instExit
        }

        return; // declined: keep the list (the original jumped to loopEntry)
    }

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        uiPop();
        return;
    }

    if(vpad.hold & VPAD_BUTTON_UP)
    {
        if(itbOldHold != VPAD_BUTTON_UP)
        {
            asyncState = ASYNC_STATE_BKWD;
            itbOldHold = VPAD_BUTTON_UP;
            itbFrameCount = DPAD_COOLDOWN_FRAMES;
            itbDpadAction = true;
        }
        else if(itbFrameCount == 0)
            itbDpadAction = true;
        else
        {
            --itbFrameCount;
            itbDpadAction = false;
        }

        if(itbDpadAction)
        {
            uiInvalidate();
            if(itbCursor)
                itbCursor--;
            else
            {
                if(itbMov)
                {
                    if(itbPos)
                    {
                        itbPos--;
                    }
                    else
                    {
                        itbCursor = MAX_ITITLEBROWSER_LINES - 1;
                        itbPos = ititleEntrySize - MAX_ITITLEBROWSER_LINES;
                    }
                }
                else
                    itbCursor = ititleEntrySize - 1;
            }
        }
    }
    else if(vpad.hold & VPAD_BUTTON_DOWN)
    {
        if(itbOldHold != VPAD_BUTTON_DOWN)
        {
            asyncState = ASYNC_STATE_FWD;
            itbOldHold = VPAD_BUTTON_DOWN;
            itbFrameCount = DPAD_COOLDOWN_FRAMES;
            itbDpadAction = true;
        }
        else if(itbFrameCount == 0)
            itbDpadAction = true;
        else
        {
            --itbFrameCount;
            itbDpadAction = false;
        }

        if(itbDpadAction)
        {
            uiInvalidate();
            if(itbCursor + itbPos >= ititleEntrySize - 1 || itbCursor >= MAX_ITITLEBROWSER_LINES - 1)
            {
                if(!itbMov || ++itbPos + itbCursor >= ititleEntrySize)
                    itbCursor = itbPos = 0;
            }
            else
                ++itbCursor;
        }
    }
    else if(itbMov)
    {
        if(vpad.hold & VPAD_BUTTON_RIGHT)
        {
            if(itbOldHold != VPAD_BUTTON_RIGHT)
            {
                asyncState = ASYNC_STATE_FWD;
                itbOldHold = VPAD_BUTTON_RIGHT;
                itbFrameCount = DPAD_COOLDOWN_FRAMES;
                itbDpadAction = true;
            }
            else if(itbFrameCount == 0)
                itbDpadAction = true;
            else
            {
                --itbFrameCount;
                itbDpadAction = false;
            }

            if(itbDpadAction)
            {
                uiInvalidate();
                itbPos += MAX_ITITLEBROWSER_LINES;
                if(itbPos >= ititleEntrySize)
                    itbPos = 0;
                itbCursor = 0;
            }
        }
        else if(vpad.hold & VPAD_BUTTON_LEFT)
        {
            if(itbOldHold != VPAD_BUTTON_LEFT)
            {
                asyncState = ASYNC_STATE_BKWD;
                itbOldHold = VPAD_BUTTON_LEFT;
                itbFrameCount = DPAD_COOLDOWN_FRAMES;
                itbDpadAction = true;
            }
            else if(itbFrameCount == 0)
                itbDpadAction = true;
            else
            {
                --itbFrameCount;
                itbDpadAction = false;
            }

            if(itbDpadAction)
            {
                uiInvalidate();
                if(itbPos >= MAX_ITITLEBROWSER_LINES)
                    itbPos -= MAX_ITITLEBROWSER_LINES;
                else
                    itbPos = ititleEntrySize - MAX_ITITLEBROWSER_LINES;
                itbCursor = 0;
            }
        }
    }

    if(itbOldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)))
        itbOldHold = 0;
}

static const UIScreen itbScreen = {
    .name = "installed titles",
    .buttons = VPAD_BUTTON_B | VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT | VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS,
    .update = updateITBMenu,
    .render = renderITBMenu,
};

void ititleBrowserMenu()
{
    OSThread *bgt = initITBMenu();
    if(!bgt)
        return;

    // Fresh visit: the loop locals of the original start over, going back
    // from the confirmation (done inside updateITBMenu) keeps them.
    itbCursor = 0;
    itbPos = 0;
    itbMov = ititleEntrySize > MAX_ITITLEBROWSER_LINES;
    itbOldHold = VPAD_BUTTON_RIGHT;
    itbFrameCount = DPAD_COOLDOWN_FRAMES;

    uiModal(&itbScreen, NULL); // blocks until B, launch or deinstall

    asyncState = ASYNC_STATE_EXIT;
    stopThread(bgt, NULL);
    MEMFreeToDefaultHeap(ititleEntries);
    MEMFreeToDefaultHeap((void *)installedTitles);
}
