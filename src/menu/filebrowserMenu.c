/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
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

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <crypto.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <list.h>
#include <localisation.h>
#include <menu/filebrowser.h>
#include <menu/queue.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <state.h>
#include <ui.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define MAX_FILEBROWSER_LINES (MAX_LINES - 5)

// Why the list screen popped itself
typedef enum
{
    FB_CLOSE, // B, the queue menu or shutdown: leave the browser
    FB_REFRESH_VOLUME, // X: the device changed, rebuild the volume root
    FB_REFRESH_DIR, // ascend/descend: rebuild the directory list
    FB_SELECT, // a folder holding a title was picked
} FB_ACTION;

static NUSDEV activeDevice = NUSDEV_NONE;
static char presavedPath[FS_MAX_PATH]; // Last visited directory, restored on the next visit when it still belongs to the active device

// State of the running browser, reset on every open
static char *fbPath;
static LIST *fbFolders;
static bool fbInstallMenu;
static bool fbAllowNoIntro;
static NUSDEV fbUsbMounted;
static size_t fbCursor;
static size_t fbPos;
static bool fbMov;
static uint32_t fbOldHold;
static size_t fbFrameCount;
static FB_ACTION fbAction;
static char *fbResult;

static void drawFBMenuFrame(const char *path, LIST *folders, size_t pos, const size_t cursor, bool usbMounted, bool installMenu, bool showQueue)
{
    startNewFrame();
    textToFrame(0, 6, localise("Select a folder:"));

    boxToFrame(1, MAX_LINES - 3);

    char toWrite[FS_MAX_PATH + 256];
    strcpy(toWrite, localise("Press " BUTTON_A " to select"));
    strcat(toWrite, " || ");
    strcat(toWrite, localise(BUTTON_B " to return"));
    strcat(toWrite, " || ");

    char lbuf[64];
    strcpy(lbuf, BUTTON_X " to switch to ");
    strcat(lbuf, activeDevice == NUSDEV_USB ? "SD" : activeDevice == NUSDEV_SD ? "NAND"
            : usbMounted                                                       ? "USB"
                                                                               : "SD");
    strcat(toWrite, localise(lbuf));
    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, toWrite);

    if(showQueue)
    {
        strcpy(toWrite, localise(BUTTON_MINUS " to open the queue"));
        strcat(toWrite, " || ");
        strcat(toWrite, localise("Searching on"));
    }
    else
        strcpy(toWrite, localise("Searching on"));

    strcat(toWrite, " => ");
    strcat(toWrite, prettyDir(path));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toWrite);

    char *folder;
    char fp[FS_MAX_PATH];
    size_t i = 0;
    showQueue = false;

    forEachListEntry(folders, folder)
    {
        if(pos)
        {
            --pos;
            continue;
        }

        if(cursor == i)
            arrowToFrame(i + 2, 1);

        if(installMenu)
        {
            // The full path decides the match, so build it in one bounded
            // step: a folder that does not fit behind the current path is
            // simply not matched instead of overflowing the buffer.
            snprintf(fp, sizeof(fp), "%s%s", path, folder);
            showQueue = isQueued(fp);
        }

        if(showQueue)
            textToFrameColored(i + 2, 5, folder, SCREEN_COLOR_YELLOW);
        else
            textToFrame(i + 2, 5, folder);

        if(++i == MAX_FILEBROWSER_LINES)
            break;
    }
}

static inline bool fbShowQueue()
{
    return fbInstallMenu ? getListSize(getTitleQueue()) : false;
}

static void renderFBMenu()
{
    drawFBMenuFrame(fbPath, fbFolders, fbPos, fbCursor, fbUsbMounted, fbInstallMenu, fbShowQueue());
}

static void enterFBMenu(void *param)
{
    (void)param;
    fbCursor = 0;
    fbPos = 0;
    fbMov = getListSize(fbFolders) >= MAX_FILEBROWSER_LINES;
    fbOldHold = 0;
    fbFrameCount = 0;
    fbResult = NULL;
    fbAction = FB_CLOSE;
}

static void updateFBMenu()
{
    bool dpadAction;

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        fbAction = FB_CLOSE;
        uiPop();
        return;
    }

    if(vpad.trigger & VPAD_BUTTON_A)
    {
        if(fbCursor + fbPos == 0)
        {
            char *last = strstr(fbPath + (sizeof("/vol/") - 1), "/");
            char *cur = strstr(last + 1, "/");
            if(cur != NULL)
            {
                char *next = strstr(cur + 1, "/");
                while(next != NULL)
                {
                    last = cur;
                    cur = next;
                    next = strstr(cur + 1, "/");
                }

                *++last = '\0';
                fbAction = FB_REFRESH_DIR;
                uiPop();
                return;
            }
        }
        else
        {
            const char *folder = getContent(fbFolders, fbCursor + fbPos);

            // "title.tmd" gets written behind the folder name to test it,
            // so both have to fit into the FS_MAX_PATH buffer: a folder
            // whose path does not fit is not entered instead of running
            // past the end of it.
            size_t fbLen = strlen(fbPath);
            size_t len = fbLen + strlen(folder);
            if(len + sizeof("title.tmd") > FS_MAX_PATH)
            {
                debugPrintf("Path too long: %s%s", fbPath, folder);
                fbAction = FB_REFRESH_DIR;
                uiPop();
                return;
            }

            strcpy(fbPath + fbLen, folder);
            strcpy(fbPath + len, "title.tmd");

            bool found = fileExists(fbPath);
            fbPath[len] = '\0';
            if(!found && fbAllowNoIntro)
            {
                // No intro
                strcpy(fbPath + len, "tmd");
                found = fileExists(fbPath);
                fbPath[len] = '\0';
            }

            if(found)
            {
                OSBlockMove(presavedPath, fbPath, fbLen, false);
                presavedPath[fbLen] = '\0';
                destroyList(fbFolders, true);
                fbFolders = NULL;
                fbResult = fbPath;
                fbAction = FB_SELECT;
                uiPop();
                return;
            }

            fbAction = FB_REFRESH_DIR;
            uiPop();
            return;
        }
    }

    if(vpad.hold & VPAD_BUTTON_UP)
    {
        if(fbOldHold != VPAD_BUTTON_UP)
        {
            fbOldHold = VPAD_BUTTON_UP;
            fbFrameCount = DPAD_COOLDOWN_FRAMES;
            dpadAction = true;
        }
        else if(fbFrameCount == 0)
            dpadAction = true;
        else
        {
            --fbFrameCount;
            dpadAction = false;
        }

        if(dpadAction)
        {
            uiInvalidate();
            if(fbCursor)
                fbCursor--;
            else
            {
                if(fbMov)
                {
                    if(fbPos)
                        fbPos--;
                    else
                    {
                        fbCursor = MAX_FILEBROWSER_LINES - 1;
                        fbPos = getListSize(fbFolders) - MAX_FILEBROWSER_LINES;
                    }
                }
                else
                    fbCursor = getListSize(fbFolders) - 1;
            }
        }
    }
    else if(vpad.hold & VPAD_BUTTON_DOWN)
    {
        if(fbOldHold != VPAD_BUTTON_DOWN)
        {
            fbOldHold = VPAD_BUTTON_DOWN;
            fbFrameCount = DPAD_COOLDOWN_FRAMES;
            dpadAction = true;
        }
        else if(fbFrameCount == 0)
            dpadAction = true;
        else
        {
            --fbFrameCount;
            dpadAction = false;
        }

        if(dpadAction)
        {
            uiInvalidate();
            if(fbCursor + fbPos >= getListSize(fbFolders) - 1 || fbCursor >= MAX_FILEBROWSER_LINES - 1)
            {
                if(!fbMov || ++fbPos + fbCursor >= getListSize(fbFolders))
                    fbCursor = fbPos = 0;
            }
            else
                ++fbCursor;
        }
    }
    else if(fbMov)
    {
        if(vpad.hold & VPAD_BUTTON_RIGHT)
        {
            if(fbOldHold != VPAD_BUTTON_RIGHT)
            {
                fbOldHold = VPAD_BUTTON_RIGHT;
                fbFrameCount = DPAD_COOLDOWN_FRAMES;
                dpadAction = true;
            }
            else if(fbFrameCount == 0)
                dpadAction = true;
            else
            {
                --fbFrameCount;
                dpadAction = false;
            }

            if(dpadAction)
            {
                uiInvalidate();
                fbPos += MAX_FILEBROWSER_LINES;
                if(fbPos >= getListSize(fbFolders))
                    fbPos = 0;
                fbCursor = 0;
            }
        }
        else if(vpad.hold & VPAD_BUTTON_LEFT)
        {
            if(fbOldHold != VPAD_BUTTON_LEFT)
            {
                fbOldHold = VPAD_BUTTON_LEFT;
                fbFrameCount = DPAD_COOLDOWN_FRAMES;
                dpadAction = true;
            }
            else if(fbFrameCount == 0)
                dpadAction = true;
            else
            {
                --fbFrameCount;
                dpadAction = false;
            }

            if(dpadAction)
            {
                uiInvalidate();
                if(fbPos >= MAX_FILEBROWSER_LINES)
                    fbPos -= MAX_FILEBROWSER_LINES;
                else
                    fbPos = getListSize(fbFolders) - MAX_FILEBROWSER_LINES;
                fbCursor = 0;
            }
        }
    }

    if(vpad.trigger & VPAD_BUTTON_MINUS && fbShowQueue())
    {
        if(queueMenu())
        {
            fbAction = FB_CLOSE;
            uiPop();
            return;
        }
    }

    if(vpad.trigger & VPAD_BUTTON_X)
    {
        switch((int)activeDevice)
        {
            case NUSDEV_USB:
                activeDevice = NUSDEV_SD;
                break;
            case NUSDEV_SD:
                activeDevice = NUSDEV_MLC;
                break;
            case NUSDEV_MLC:
                activeDevice = fbUsbMounted ? NUSDEV_USB : NUSDEV_SD;
        }

        fbAction = FB_REFRESH_VOLUME;
        uiPop();
        return;
    }

    if(fbOldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)))
        fbOldHold = 0;
}

static const UIScreen fbScreen = {
    .name = "file browser",
    .buttons = VPAD_BUTTON_A | VPAD_BUTTON_B | VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT | VPAD_BUTTON_X | VPAD_BUTTON_MINUS,
    .enter = enterFBMenu,
    .update = updateFBMenu,
    .render = renderFBMenu,
};

char *fileBrowserMenu(bool installMenu, bool allowNoIntro)
{
    fbInstallMenu = installMenu;
    fbAllowNoIntro = allowNoIntro;

    fbPath = MEMAllocFromDefaultHeap(FS_MAX_PATH);
    if(fbPath == NULL)
        return NULL;

    fbFolders = createList();
    if(fbFolders == NULL)
    {
        MEMFreeToDefaultHeap(fbPath);
        fbPath = NULL;
        return NULL;
    }

    fbUsbMounted = getUSB();
    if(activeDevice == NUSDEV_NONE)
        activeDevice = fbUsbMounted ? NUSDEV_USB : NUSDEV_SD;

    FSADirectoryHandle dir;

refreshVOlList:
    strcpy(fbPath, (activeDevice & NUSDEV_USB) ? (fbUsbMounted == NUSDEV_USB01 ? INSTALL_DIR_USB1 : INSTALL_DIR_USB2) : (activeDevice == NUSDEV_SD ? INSTALL_DIR_SD : INSTALL_DIR_MLC));

    // Restore the last visited directory if it still belongs to the active device and exists
    const char *devRoot = (activeDevice & NUSDEV_USB) ? (fbUsbMounted == NUSDEV_USB01 ? NUSDIR_USB1 : NUSDIR_USB2) : (activeDevice == NUSDEV_SD ? NUSDIR_SD : NUSDIR_MLC);
    if(strncmp(presavedPath, devRoot, strlen(devRoot)) == 0 && dirExists(presavedPath))
        strcpy(fbPath, presavedPath);

    if(activeDevice == NUSDEV_SD)
        checkSpaceThread(); // To show the waiting for SD overlay

refreshDirList:
    OSTime t = OSGetTime();
    clearList(fbFolders, true);
    char *name = MEMAllocFromDefaultHeap(sizeof("../"));
    if(name == NULL)
        goto exitFailure;

    OSBlockMove(name, "../", sizeof("../"), false);
    if(!addToListEnd(fbFolders, name))
    {
        MEMFreeToDefaultHeap(name);
        goto exitFailure;
    }

    if(FSAOpenDir(getFSAClient(), fbPath, &dir) == FS_ERROR_OK)
    {
        size_t len;
        FSADirectoryEntry entry;
        while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
            if(entry.info.flags & FS_STAT_DIRECTORY) // Check if it's a directory
            {
                len = strlen(entry.name);
                name = MEMAllocFromDefaultHeap(len + 2);
                if(name == NULL)
                {
                    FSACloseDir(getFSAClient(), dir);
                    goto exitFailure;
                }

                OSBlockMove(name, entry.name, len, false);
                name[len] = '/';
                name[++len] = '\0';
                if(!addToListEnd(fbFolders, name))
                {
                    MEMFreeToDefaultHeap(name);
                    FSACloseDir(getFSAClient(), dir);
                    goto exitFailure;
                }
            }

        FSACloseDir(getFSAClient(), dir);
    }

    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));

    uiModal(&fbScreen, NULL);

    if(fbAction == FB_REFRESH_VOLUME)
        goto refreshVOlList;

    if(fbAction == FB_REFRESH_DIR)
        goto refreshDirList;

    if(fbAction == FB_SELECT)
    {
        fbPath = NULL;
        return fbResult;
    }

exitFailure:
    strcpy(presavedPath, fbPath);
    destroyList(fbFolders, true);
    MEMFreeToDefaultHeap(fbPath);
    fbFolders = NULL;
    fbPath = NULL;
    return NULL;
}
