/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2025 V10lator <v10lator@myway.de>                         *
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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <config.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <localisation.h>
#include <menu/missingcontent.h>
#include <menu/predownload.h>
#include <menu/queue.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <state.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

#define MAX_MC_LINES         (MAX_LINES - 3)
#define DPAD_COOLDOWN_FRAMES 30 // half a second at 60 FPS

typedef struct
{
    const TitleEntry *entry;
    bool isDlc; // else it's an update
    bool toUSB; // Where the base game is installed, updates/DLC must follow
} MISSING_ENTRY;

static MISSING_ENTRY *missingEntries;
static size_t missingEntrySize;

static void addMissingCandidate(const MCPTitleListType *list, uint32_t count, uint32_t index, bool isDlc)
{
    uint64_t tid = isDlc ? BASE_TO_DLC(list[index].titleId) : BASE_TO_UPDATE(list[index].titleId);

    for(size_t i = 0; i < missingEntrySize; ++i)
        if(missingEntries[i].entry->tid == tid)
            return; // Already found (e.g. installed on both USB and NAND)

    const TitleEntry *e = getTitleEntryByTid(tid);
    if(e == NULL || e->key == TITLE_KEY_MAGIC) // Not a real, downloadable title
        return;

    for(uint32_t i = 0; i < count; ++i)
        if(list[i].titleId == tid) // Already installed
            return;

    missingEntries[missingEntrySize].entry = e;
    missingEntries[missingEntrySize].isDlc = isDlc;
    missingEntries[missingEntrySize].toUSB = list[index].indexedDevice[0] == 'u';
    ++missingEntrySize;
}

// Returns false on error, true on success (missingEntrySize might still be 0)
static bool scanForMissingContent()
{
    int32_t r = MCP_TitleCount(mcpHandle);
    if(r <= 0)
    {
        debugPrintf("Missingcontent: MCP_TitleCount() returned %d", r);
        return false;
    }

    uint32_t s = sizeof(MCPTitleListType) * (uint32_t)r;
    MCPTitleListType *list = (MCPTitleListType *)MEMAllocFromDefaultHeapEx(s, 0x40);
    if(list == NULL)
    {
        debugPrintf("Missingcontent: OUT OF MEMORY!");
        return false;
    }

    if(MCP_TitleList(mcpHandle, &s, list, s) < 0)
    {
        debugPrintf("Missingcontent: MCP_TitleList() failed");
        MEMFreeToDefaultHeap(list);
        return false;
    }

    // Every installed game contributes at most one missing update and one missing DLC entry
    missingEntries = (MISSING_ENTRY *)MEMAllocFromDefaultHeap(sizeof(MISSING_ENTRY) * s * 2);
    if(missingEntries == NULL)
    {
        debugPrintf("Missingcontent: OUT OF MEMORY!");
        MEMFreeToDefaultHeap(list);
        return false;
    }

    missingEntrySize = 0;
    for(size_t i = 0; i < s; ++i)
    {
        if(!isGame(list[i].titleId))
            continue;

        addMissingCandidate(list, s, i, false);
        addMissingCandidate(list, s, i, true);
    }

    MEMFreeToDefaultHeap(list);
    return true;
}

static void drawMCMenuFrame(const size_t pos, const size_t cursor)
{
    startNewFrame();
    boxToFrame(0, MAX_LINES - 2);

    char toFrame[512];
    strcpy(toFrame, localise("Press " BUTTON_A " to select"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_PLUS " to queue all"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_B " to return"));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toFrame);

    size_t max = missingEntrySize - pos;
    if(max > MAX_MC_LINES)
        max = MAX_MC_LINES;

    const MISSING_ENTRY *me;
    for(size_t i = 0, l = 1; i < max; ++i, ++l)
    {
        me = missingEntries + pos + i;
        if(cursor == i)
            arrowToFrame(l, 1);

        flagToFrame(l, 4, me->entry->region);
        OSBlockMove(toFrame, me->isDlc ? "[DLC] " : "[UPD] ", sizeof("[DLC] "), false);
        OSBlockMove(toFrame + sizeof("[DLC] ") - 1, me->entry->name, strlen(me->entry->name) + 1, false);
        textToFrameCut(l, 7, toFrame, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * 8));
    }

    drawFrame();
}

static bool addOneToQueue(const MISSING_ENTRY *me)
{
    const TitleEntry *entry = me->entry;
    RAMBUF *rambuf = allocRamBuf();
    if(rambuf == NULL)
        return false;

    char tid[17];
    hex(entry->tid, 16, tid);

    char downloadUrl[256];
    strcpy(downloadUrl, DOWNLOAD_URL);
    strcat(downloadUrl, tid);
    strcat(downloadUrl, "/tmd");

    if(downloadFile(downloadUrl, "title.tmd", NULL, (FileType)(FILE_TYPE_TMD | FILE_TYPE_TORAM), false, NULL, rambuf))
    {
        freeRamBuf(rambuf);
        addToScreenLog("Error downloading the TMD of \"%s\"", entry->name);
        return false;
    }

    TMD *tmd = (TMD *)rambuf->buf;
    if(verifyTmd(tmd, rambuf->size) != TMD_STATE_GOOD)
    {
        freeRamBuf(rambuf);
        addToScreenLog("Invalid title.tmd for \"%s\"", entry->name);
        return false;
    }

    NUSDEV usbMounted = getUSB();
    TitleData *titleInfo = MEMAllocFromDefaultHeap(sizeof(TitleData));
    if(titleInfo == NULL)
    {
        freeRamBuf(rambuf);
        addToScreenLog("OUT OF MEMORY!");
        return false;
    }

    titleInfo->tmd = tmd;
    titleInfo->tmdSize = rambuf->size;
    titleInfo->rambuf = rambuf;
    titleInfo->entry = entry;
    titleInfo->titleVer[0] = '\0';
    titleInfo->folderName[0] = '\0';
    titleInfo->operation = OPERATION_DOWNLOAD_INSTALL;
    titleInfo->dlDev = usbMounted && dlToUSBenabled() ? usbMounted : NUSDEV_SD;
    titleInfo->toUSB = me->toUSB;
    titleInfo->keepFiles = true;

    int ret = addToQueue(titleInfo);
    if(ret == 1)
        return true;

    MEMFreeToDefaultHeap(titleInfo);
    freeRamBuf(rambuf);

    // 0 = out of memory, 2 = already queued for install, 3 = already queued for download
    if(ret == 2 || ret == 3)
    {
        addToScreenLog("\"%s\" is already queued", entry->name);
        return true;
    }

    addToScreenLog("Failed queueing \"%s\"", entry->name);
    return false;
}

static inline void drawQFrame()
{
    startNewFrame();
    textToFrame(0, 0, localise("Queueing missing content..."));
    writeScreenLog(1);
    drawFrame();
    showFrame();
}

static bool queueAllMissing()
{
    clearScreenLog();
    drawQFrame();

    size_t queued = 0;
    size_t skipped = 0;
    for(size_t i = 0; i < missingEntrySize; ++i)
    {
        if(addOneToQueue(missingEntries + i))
            ++queued;
        else if(AppRunning(true))
        {
            drawQFrame();
            ++skipped;
        }
        else
            return true;
    }

    char toFrame[256];
    snprintf(toFrame, sizeof(toFrame), "%s: %zu, %s: %zu", localise("Queued"), queued, localise("Skipped"), skipped);

    void *ovl = addErrorOverlay(toFrame);
    if(ovl == NULL)
        return true;

    while(AppRunning(true))
    {
        showFrame();

        if(vpad.trigger)
            break;
    }

    removeErrorOverlay(ovl);

    if(!AppRunning(true))
        return true;

    if(queued != 0)
        return queueMenu();

    return false;
}

static void drawNMCscreen()
{
    colorStartNewFrame(SCREEN_COLOR_D_GREEN);
    textToFrame(0, 0, localise("No missing content found"));
    textToFrame(2, 0, localise("Press " BUTTON_B " to return"));
    drawFrame();
}

static inline void showNMCscreen()
{
    drawNMCscreen();

    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            drawNMCscreen();

        showFrame();

        if(vpad.trigger & VPAD_BUTTON_B)
            break;
    }
}

void missingContentMenu()
{
    bool firstRun = true;
entry:
    startNewFrame();
    textToFrame(0, ALIGNED_CENTER, localise("Searching for missing content..."));
    drawFrame();
    showFrame();

    if(!scanForMissingContent())
    {
        showErrorFrame(localise("Error scanning for installed titles!"));
        return;
    }

    if(missingEntrySize == 0)
    {
        MEMFreeToDefaultHeap(missingEntries);
        if(firstRun)
            showNMCscreen();

        return;
    }

    size_t cursor = 0;
    size_t pos = 0;
    bool mov;
    bool redraw = true;
    uint32_t oldHold = 0;
    size_t frameCount = 0;
    bool dpadAction;

    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            redraw = true;

        if(redraw)
        {
            drawMCMenuFrame(pos, cursor);
            mov = missingEntrySize > MAX_MC_LINES;
            redraw = false;
        }
        showFrame();

        if(vpad.trigger & VPAD_BUTTON_A)
        {
            if(!predownloadMenu(missingEntries[cursor + pos].entry, missingEntries[cursor + pos].toUSB ? NUSDEV_USB : NUSDEV_MLC))
            {
                MEMFreeToDefaultHeap(missingEntries);
                firstRun = false;
                goto entry;
            }

            redraw = true;
            continue;
        }

        if(vpad.trigger & VPAD_BUTTON_PLUS)
        {
            if(queueAllMissing()) // Trigger rescan in case the user removed items from the queue
            {
                MEMFreeToDefaultHeap(missingEntries);
                firstRun = false;
                goto entry;
            }

            redraw = true;
            continue;
        }

        if(vpad.trigger & VPAD_BUTTON_B)
            break;

        if(vpad.hold & VPAD_BUTTON_UP)
        {
            if(oldHold != VPAD_BUTTON_UP)
            {
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
                            pos--;
                        else
                        {
                            cursor = MAX_MC_LINES - 1;
                            pos = missingEntrySize - MAX_MC_LINES;
                        }
                    }
                    else
                        cursor = missingEntrySize - 1;
                }

                redraw = true;
            }
        }
        else if(vpad.hold & VPAD_BUTTON_DOWN)
        {
            if(oldHold != VPAD_BUTTON_DOWN)
            {
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
                if(cursor + pos >= missingEntrySize - 1 || cursor >= MAX_MC_LINES - 1)
                {
                    if(!mov || ++pos + cursor >= missingEntrySize)
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
                    pos += MAX_MC_LINES;
                    if(pos >= missingEntrySize)
                        pos = 0;
                    cursor = 0;
                    redraw = true;
                }
            }
            else if(vpad.hold & VPAD_BUTTON_LEFT)
            {
                if(oldHold != VPAD_BUTTON_LEFT)
                {
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
                    if(pos >= MAX_MC_LINES)
                        pos -= MAX_MC_LINES;
                    else
                        pos = missingEntrySize - MAX_MC_LINES;
                    cursor = 0;
                    redraw = true;
                }
            }
        }

        if(oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)))
            oldHold = 0;
    }

    MEMFreeToDefaultHeap(missingEntries);
}
