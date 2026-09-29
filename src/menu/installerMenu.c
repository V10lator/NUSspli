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
#include <ui.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

// The menu state: dir/tmd/entry/name are (re)set while the screen is
// pushed (see installerLoadTmd), cursorPos keeps the legacy behaviour of
// staying where the user left it the last time.
static int cursorPos = MAX_LINES - 5;
static const char *dir;
static const TMD *tmd;
static const TitleEntry *entry;
static const char *nd;
static NUSDEV dev;
static NUSDEV toDev;
static NUSDEV usbMounted;
static bool keepFiles;

// Returns what addToQueue() answered: 1 = the entry went to the queue
// together with the TMD, 2 and 3 = the title is already queued and
// nothing was taken over, 0 = the entry could not be allocated.
static int addToOpQueue(const TitleEntry *titleEntry, const char *folder, const TMD *titleTmd, NUSDEV fromDev, bool toUSB, bool keepDlFiles)
{
    TitleData *titleInfo = MEMAllocFromDefaultHeap(sizeof(TitleData));
    if(titleInfo == NULL)
        return 0;

    // The folder comes from the file browser and may run to FS_MAX_PATH,
    // while this field gives up its last eleven bytes for the "title.tmd"
    // that gets appended to the name later on. Copying a path that long
    // runs past the end of the field and into dlDev/toUSB/keepFiles behind
    // it, so drop the entry the way a failed allocation does - the caller
    // pops the menu, whose leave handler releases the TMD and the folder.
    // This is the guard ticket.c keeps for the same path buffer.
    if(strlen(folder) + 1 > sizeof(titleInfo->folderName))
    {
        debugPrintf("Path too long: %s", folder);
        MEMFreeToDefaultHeap(titleInfo);
        return 0;
    }

    titleInfo->tmd = (TMD *)titleTmd;
    titleInfo->rambuf = NULL;
    titleInfo->operation = OPERATION_INSTALL;
    titleInfo->entry = titleEntry;
    strcpy(titleInfo->folderName, folder);
    titleInfo->dlDev = fromDev;
    titleInfo->toUSB = toUSB;
    titleInfo->keepFiles = keepDlFiles;

    int ret = addToQueue(titleInfo);
    if(ret == 1)
        return ret;

    MEMFreeToDefaultHeap(titleInfo);

    // 2 = already queued for install, 3 = already queued for download: not an error
    if(ret == 2 || ret == 3)
    {
        // A folder that is not in the database has no name: say what the
        // browser showed instead of feeding NULL to the format string.
        addToScreenLog("\"%s\" is already queued", titleEntry == NULL ? prettyDir(folder) : titleEntry->name);
    }

    return ret;
}

// refreshDir (pickNewFolder == false) or grabNewDir (pickNewFolder == true):
// load the title.tmd of the current folder, falling back to picking a new
// one until it has a valid tmd. Returns false when the menu has to be left
// because the user cancelled or the app stopped.
static bool installerLoadTmd(bool pickNewFolder)
{
    while(true)
    {
        if(!pickNewFolder)
        {
            tmd = getTmd(dir, true);
            if(tmd != NULL)
            {
                dev = getDevFromPath(dir);
                if(dev != NUSDEV_SD)
                    keepFiles = false;

                entry = getTitleEntryByTid(tmd->tid);
                nd = entry == NULL ? prettyDir(dir) : entry->name;
                return true;
            }

            showErrorFrame(localise("Invalid title.tmd file!"));
        }

        pickNewFolder = false;

        // grabNewDir
        MEMFreeToDefaultHeap((char *)dir);
        dir = NULL;
        if(!AppRunning(true))
            return false;

        dir = fileBrowserMenu(true, true);
        if(dir == NULL)
            return false;
    }
}

static void renderInstallerFrame()
{
    // installerLoadTmd leaves tmd NULL while the file browser or an error
    // dialog below this screen finishes: keep the old picture then. The
    // legacy code didn't redraw the menu in that state either.
    if(tmd == NULL)
        return;

    startNewFrame();
    textToFrame(0, 0, localise("Name:"));

    char toFrame[512];
    strcpy(toFrame, nd);
    char tid[17];
    hex(tmd->tid, 16, tid);
    strcat(toFrame, " [");
    strcat(toFrame, tid);
    strcat(toFrame, "]");
    int line = textToFrameMultiline(0, ALIGNED_CENTER, toFrame, MAX_CHARS - 33); // 33 below full width: centred that leaves ~16 chars of margin per side so the name can never overlap the "Name:" label at the left edge of line 0

    uint64_t size = 0;
    for(uint16_t i = 0; i < tmd->num_contents; ++i)
        size += tmd->contents[i].size;

    humanize(size, toFrame);

    MCPRegion region = entry == NULL ? MCP_REGION_UNKNOWN : entry->region;

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
    // The parts are localised on their own: the composed line is not a key,
    // so looking it up can never hit and would only return it unchanged.
    if(dev == NUSDEV_SD)
        textToFrame(MAX_LINES - 4, 4, toFrame);
    else
        textToFrameColored(MAX_LINES - 4, 4, toFrame, SCREEN_COLOR_WHITE_TRANSP);

    lineToFrame(MAX_LINES - 3, SCREEN_COLOR_WHITE);

    strcpy(toFrame, localise("Press " BUTTON_B " to return"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_PLUS " to start"));
    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, toFrame);

    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, localise(BUTTON_MINUS " to add to the queue"));
}

static void leaveInstallerMenu()
{
    if(tmd != NULL)
    {
        MEMFreeToDefaultHeap((TMD *)tmd);
        tmd = NULL;
    }

    if(dir != NULL)
    {
        MEMFreeToDefaultHeap((char *)dir);
        dir = NULL;
    }
}

static void updateInstallerMenu()
{
    if(vpad.trigger & VPAD_BUTTON_B)
    {
        MEMFreeToDefaultHeap((TMD *)tmd);
        tmd = NULL;
        if(!installerLoadTmd(true))
            uiPop();

        return;
    }

    if(vpad.trigger & VPAD_BUTTON_PLUS)
    {
        bool finished = false;
        if(checkSystemTitleFromTid(tmd->tid, false))
        {
            disableApd();
            finished = install(nd, false, dev, dir, toDev & NUSDEV_USB, keepFiles, tmd);
            enableApd();
        }

        // The old break left the menu: release title.tmd and the folder
        // first, then celebrate - nd is a stable copy (entry name or
        // prettyDir's buffer), so the finished dialog shows as before
        // without a frame of the closing menu in between.
        uiPop();
        if(finished)
            showFinishedScreen(nd, FINISHING_OPERATION_INSTALL);

        return;
    }
    else if(vpad.trigger & VPAD_BUTTON_MINUS)
    {
        int ret = addToOpQueue(entry, dir, tmd, dev, toDev & NUSDEV_USB, keepFiles);
        if(ret == 0)
        {
            uiPop();
            return;
        }

        // grabNewDir: pick a new folder, and the TMD is not ours anymore -
        // the queue took it over together with the entry, or it was just
        // dropped here the way the B path drops it.
        if(ret != 1)
            MEMFreeToDefaultHeap((TMD *)tmd);

        tmd = NULL;
        if(!installerLoadTmd(true))
            uiPop();

        return;
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
    }
    else if(vpad.trigger & VPAD_BUTTON_DOWN)
    {
        if(++cursorPos == MAX_LINES - 3)
            cursorPos = MAX_LINES - 5;
    }
    else if(vpad.trigger & VPAD_BUTTON_UP)
    {
        if(--cursorPos == MAX_LINES - 6)
            cursorPos = MAX_LINES - 4;
    }
}

static const UIScreen installerScreen = {
    .name = "installer",
    .buttons = VPAD_BUTTON_A | VPAD_BUTTON_B | VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT | VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS,
    .update = updateInstallerMenu,
    .render = renderInstallerFrame,
    .leave = leaveInstallerMenu,
};

void installerMenu()
{
    dir = fileBrowserMenu(true, true);
    if(dir == NULL)
        return;

    if(!AppRunning(true))
    {
        MEMFreeToDefaultHeap((char *)dir);
        dir = NULL;
        return;
    }

    dev = getDevFromPath(dir);
    keepFiles = dev == NUSDEV_SD;

    toDev = getUSB();
    usbMounted = toDev & NUSDEV_USB;
    if(!usbMounted)
        toDev = NUSDEV_MLC;

    if(!installerLoadTmd(false))
        return;

    uiModal(&installerScreen, NULL);
}
