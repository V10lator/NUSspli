/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2024 V10lator <v10lator@myway.de>                    *
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

#include <string.h>

#include <config.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <localisation.h>
#include <menu/filebrowser.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <state.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

static int cursorPos = MAX_LINES - 5;

static bool addToOpQueue(const TitleEntry *entry, const char *dir, const TMD *tmd, NUSDEV fromDev, bool toUSB, bool keepFiles)
{
    TitleData *titleInfo = MEMAllocFromDefaultHeap(sizeof(TitleData));
    if(titleInfo == NULL)
        return false;

    titleInfo->tmd = (TMD *)tmd;
    titleInfo->rambuf = NULL;
    titleInfo->operation = OPERATION_INSTALL;
    titleInfo->entry = entry;
    strcpy(titleInfo->folderName, dir);
    titleInfo->dlDev = fromDev;
    titleInfo->toUSB = toUSB;
    titleInfo->keepFiles = keepFiles;

    int ret = addToQueue(titleInfo);
    if(ret == 1)
        return true;

    MEMFreeToDefaultHeap(titleInfo);

    MEMFreeToDefaultHeap((TMD *)tmd);
    return ret;
}

static void drawInstallerMenuFrame(const char *name, NUSDEV dev, NUSDEV toDev, bool usbMounted, bool keepFiles, MCPRegion region, const TMD *tmd)
{
    startNewFrame();
    textToFrame(0, 0, localise("Name:"));

    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, name);
    char tid[17];
    hex(tmd->tid, 16, tid);
    strcat(toFrame, " [");
    strcat(toFrame, tid);
    strcat(toFrame, "]");
    int line = textToFrameMultiline(0, ALIGNED_CENTER, toFrame, MAX_CHARS - 33); // TODO

    uint64_t size = 0;
    for(uint16_t i = 0; i < tmd->num_contents; ++i)
        size += tmd->contents[i].size;

    humanize(size, toFrame);

    textToFrame(++line, 0, localise("Region:"));
    flagToFrame(++line, 3, region);
    textToFrame(line, 7, localise(getFormattedRegion(region)));

    textToFrame(++line, 0, localise("Size:"));
    textToFrame(++line, 3, toFrame);

    lineToFrame(MAX_LINES - 6, SCREEN_COLOR_WHITE);
    arrowToFrame(cursorPos, 0);

    strcpy(toFrame, localise("Install to:"));
    strcat(toFrame, " ");
    switch((int)toDev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            strcat(toFrame, "USB");
            break;
        case NUSDEV_MLC:
            strcat(toFrame, "NAND");
            break;
    }

    getFreeSpaceString(toDev, toFrame + strlen(toFrame));

    if(usbMounted)
        textToFrame(MAX_LINES - 5, 4, toFrame);
    else
        textToFrameColored(MAX_LINES - 5, 4, toFrame, SCREEN_COLOR_WHITE_TRANSP);

    strcpy(toFrame, localise("Keep downloaded files:"));
    strcat(toFrame, " ");
    strcat(toFrame, localise(keepFiles ? "Yes" : "No"));
    if(dev == NUSDEV_SD)
        textToFrame(MAX_LINES - 4, 4, localise(toFrame));
    else
        textToFrameColored(MAX_LINES - 4, 4, localise(toFrame), SCREEN_COLOR_WHITE_TRANSP);

    lineToFrame(MAX_LINES - 3, SCREEN_COLOR_WHITE);

    strcpy(toFrame, localise("Press " BUTTON_B " to return"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_PLUS " to start"));
    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, toFrame);

    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, localise(BUTTON_MINUS " to add to the queue"));

    drawFrame();
}

void installerMenu()
{
    const char *dir = fileBrowserMenu(true, true);
    if(dir == NULL)
        return;

    if(!AppRunning(true))
    {
        MEMFreeToDefaultHeap((char *)dir);
        return;
    }

    NUSDEV dev = getDevFromPath(dir);
    bool keepFiles = dev == NUSDEV_SD;

    NUSDEV toDev = getUSB();
    NUSDEV usbMounted = toDev & NUSDEV_USB;
    if(!usbMounted)
        toDev = NUSDEV_MLC;

    const TMD *tmd;
    const TitleEntry *entry;
    const char *nd;
    bool redraw;

refreshDir:
    tmd = getTmd(dir, true);
    if(tmd == NULL)
    {
        showErrorFrame(localise("Invalid title.tmd file!"));
        goto grabNewDir;
    }

    dev = getDevFromPath(dir);
    if(dev != NUSDEV_SD)
        keepFiles = false;

    entry = getTitleEntryByTid(tmd->tid);
    nd = entry == NULL ? prettyDir(dir) : entry->name;

    redraw = true;

    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            redraw = true;

        if(redraw)
        {
            drawInstallerMenuFrame(nd, dev, toDev, usbMounted, keepFiles, entry == NULL ? MCP_REGION_UNKNOWN : entry->region, tmd);
            redraw = false;
        }
        showFrame();

        if(vpad.trigger & VPAD_BUTTON_B)
        {
            MEMFreeToDefaultHeap((TMD *)tmd);
            goto grabNewDir;
        }

        if(vpad.trigger & VPAD_BUTTON_PLUS)
        {
            if(checkSystemTitleFromTid(tmd->tid, false))
            {
                disableApd();
                redraw = install(nd, false, dev, dir, toDev & NUSDEV_USB, keepFiles, tmd);
                enableApd();
                if(redraw)
                    showFinishedScreen(nd, FINISHING_OPERATION_INSTALL);
            }

            break;
        }
        else if(vpad.trigger & VPAD_BUTTON_MINUS)
        {
            if(!addToOpQueue(entry, dir, tmd, dev, toDev & NUSDEV_USB, keepFiles))
                break;

            goto grabNewDir;
        }
        else if(vpad.trigger & (VPAD_BUTTON_A | VPAD_BUTTON_RIGHT | VPAD_BUTTON_LEFT))
        {
            switch(cursorPos)
            {
                case MAX_LINES - 5:
                    if(usbMounted)
                    {
                        if(toDev & NUSDEV_USB)
                            toDev = NUSDEV_MLC;
                        else
                            toDev = usbMounted;
                    }
                    break;
                case MAX_LINES - 4:
                    if(dev == NUSDEV_SD)
                        keepFiles = !keepFiles;
                    break;
            }

            redraw = true;
        }
        else if(vpad.trigger & VPAD_BUTTON_DOWN)
        {
            if(++cursorPos == MAX_LINES - 3)
                cursorPos = MAX_LINES - 5;

            redraw = true;
        }
        else if(vpad.trigger & VPAD_BUTTON_UP)
        {
            if(--cursorPos == MAX_LINES - 6)
                cursorPos = MAX_LINES - 4;

            redraw = true;
        }
    }

    MEMFreeToDefaultHeap((TMD *)tmd);
    MEMFreeToDefaultHeap((char *)dir);
    return;

grabNewDir:
    MEMFreeToDefaultHeap((char *)dir);
    if(AppRunning(true))
    {
        dir = fileBrowserMenu(true, true);
        if(dir != NULL)
            goto refreshDir;
    }
}
