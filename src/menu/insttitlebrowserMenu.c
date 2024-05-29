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
#include <staticMem.h>
#include <thread.h>
#include <titles.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <nn/acp/title.h>
#pragma GCC diagnostic pop

#define MAX_ITITLEBROWSER_LINES        (MAX_LINES - 3)
#define MAX_ITITLEBROWSER_TITLE_LENGTH (MAX_TITLENAME_LENGTH >> 1)
#define DPAD_COOLDOWN_FRAMES           30 // half a second at 60 FPS

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
            if(++len < MAX_ITITLEBROWSER_TITLE_LENGTH)
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

static void drawITBMenuFrame(const size_t pos, const size_t cursor)
{
    startNewFrame();
    boxToFrame(0, MAX_LINES - 2);

    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, localise("Press " BUTTON_PLUS " to launch"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_MINUS " to delete"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_B " to return"));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toFrame);

    size_t max = ititleEntrySize - pos;
    if(max > MAX_ITITLEBROWSER_LINES)
        max = MAX_ITITLEBROWSER_LINES;

    volatile INST_META *im;
    for(size_t i = 0, l = 1; i < max; ++i, ++l)
    {
        im = getInstalledTitle(pos + i, true);
        if(im->isDlc)
            strcpy(toFrame, "[DLC] ");
        else if(im->isUpdate)
            strcpy(toFrame, "[UPD] ");
        else
            toFrame[0] = '\0';

        if(cursor == i)
            arrowToFrame(l, 1);

        deviceToFrame(l, 4, im->dt);
        flagToFrame(l, 7, im->region);
        strcat(toFrame, (const char *)im->name);
        textToFrameCut(l, 10, toFrame, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * 11));
    }

    drawFrame();
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
                    OSThread *ret = startThread("NUSspli title loader", THREAD_PRIORITY_MEDIUM, STACKSIZE_MEDIUM, asyncTitleLoader, 0, NULL, OS_THREAD_ATTRIB_AFFINITY_CPU2);
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

void ititleBrowserMenu()
{
    OSThread *bgt = initITBMenu();
    if(!bgt)
        return;

    size_t cursor = 0;
    size_t pos = 0;

    bool mov;
    bool redraw = true;
    MCPTitleListType *entry;
    uint32_t oldHold = VPAD_BUTTON_RIGHT;
    size_t frameCount = DPAD_COOLDOWN_FRAMES;
    bool dpadAction;

loopEntry:
    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            redraw = true;

        if(redraw)
        {
            drawITBMenuFrame(pos, cursor);
            mov = ititleEntrySize > MAX_ITITLEBROWSER_LINES;
            redraw = false;
        }
        showFrame();

        if(vpad.trigger & VPAD_BUTTON_PLUS)
        {
            launchTitle(ititleEntries + cursor + pos);
            goto instExit;
        }

        if(vpad.trigger & VPAD_BUTTON_MINUS)
        {
            entry = ititleEntries + cursor + pos;
            break;
        }

        if(vpad.trigger & VPAD_BUTTON_B)
            goto instExit;

        if(vpad.hold & VPAD_BUTTON_UP)
        {
            if(oldHold != VPAD_BUTTON_UP)
            {
                asyncState = ASYNC_STATE_BKWD;
                oldHold = VPAD_BUTTON_UP;
                frameCount = DPAD_COOLDOWN_FRAMES;
                dpadAction = true;
            }
            else if(frameCount == 0)
                dpadAction = true;
            else
            {
                --frameCount;
                dpadAction = false;
            }

            if(dpadAction)
            {
                if(cursor)
                    cursor--;
                else
                {
                    if(mov)
                    {
                        if(pos)
                        {
                            pos--;
                        }
                        else
                        {
                            cursor = MAX_ITITLEBROWSER_LINES - 1;
                            pos = ititleEntrySize - MAX_ITITLEBROWSER_LINES;
                        }
                    }
                    else
                        cursor = ititleEntrySize - 1;
                }

                redraw = true;
            }
        }
        else if(vpad.hold & VPAD_BUTTON_DOWN)
        {
            if(oldHold != VPAD_BUTTON_DOWN)
            {
                asyncState = ASYNC_STATE_FWD;
                oldHold = VPAD_BUTTON_DOWN;
                frameCount = DPAD_COOLDOWN_FRAMES;
                dpadAction = true;
            }
            else if(frameCount == 0)
                dpadAction = true;
            else
            {
                --frameCount;
                dpadAction = false;
            }

            if(dpadAction)
            {
                if(cursor + pos >= ititleEntrySize - 1 || cursor >= MAX_ITITLEBROWSER_LINES - 1)
                {
                    if(!mov || ++pos + cursor >= ititleEntrySize)
                        cursor = pos = 0;
                }
                else
                    ++cursor;

                redraw = true;
            }
        }
        else if(mov)
        {
            if(vpad.hold & VPAD_BUTTON_RIGHT)
            {
                if(oldHold != VPAD_BUTTON_RIGHT)
                {
                    asyncState = ASYNC_STATE_FWD;
                    oldHold = VPAD_BUTTON_RIGHT;
                    frameCount = DPAD_COOLDOWN_FRAMES;
                    dpadAction = true;
                }
                else if(frameCount == 0)
                    dpadAction = true;
                else
                {
                    --frameCount;
                    dpadAction = false;
                }

                if(dpadAction)
                {
                    pos += MAX_ITITLEBROWSER_LINES;
                    if(pos >= ititleEntrySize)
                        pos = 0;
                    cursor = 0;
                    redraw = true;
                }
            }
            else if(vpad.hold & VPAD_BUTTON_LEFT)
            {
                if(oldHold != VPAD_BUTTON_LEFT)
                {
                    asyncState = ASYNC_STATE_BKWD;
                    oldHold = VPAD_BUTTON_LEFT;
                    frameCount = DPAD_COOLDOWN_FRAMES;
                    dpadAction = true;
                }
                else if(frameCount == 0)
                    dpadAction = true;
                else
                {
                    --frameCount;
                    dpadAction = false;
                }

                if(dpadAction)
                {
                    if(pos >= MAX_ITITLEBROWSER_LINES)
                        pos -= MAX_ITITLEBROWSER_LINES;
                    else
                        pos = ititleEntrySize - MAX_ITITLEBROWSER_LINES;
                    cursor = 0;
                    redraw = true;
                }
            }
        }

        if(oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)))
            oldHold = 0;
    }

    if(AppRunning(true))
    {
        volatile INST_META *im = installedTitles + cursor + pos;
        char *toFrame = getToFrameBuffer();
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

        void *r = addErrorOverlay(toFrame);
        if(r == NULL)
            goto instExit;

        while(AppRunning(true))
        {
            showFrame();

            if(vpad.trigger & VPAD_BUTTON_B)
            {
                removeErrorOverlay(r);
                goto loopEntry;
            }
            if(vpad.trigger & VPAD_BUTTON_A)
                break;
        }

        removeErrorOverlay(r);

        if(checkSystemTitleFromListType(entry, true) && AppRunning(true)) // entry is initialised, the compiler just can't follow
            deinstall(entry, (const char *)im->name, false, false);
        else
            goto loopEntry;
    }

instExit:
    asyncState = ASYNC_STATE_EXIT;
    stopThread(bgt, NULL);
    MEMFreeToDefaultHeap(ititleEntries);
    MEMFreeToDefaultHeap((void *)installedTitles);
}
