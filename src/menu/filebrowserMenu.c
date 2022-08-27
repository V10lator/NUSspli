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

#include <crypto.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <localisation.h>
#include <menu/filebrowser.h>
#include <renderer.h>
#include <state.h>

#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>

#include <dirent.h>
#include <stdbool.h>
#include <string.h>

#define MAX_FILEBROWSER_LINES (MAX_LINES - 5)

static void drawFBMenuFrame(char **folders, size_t foldersSize, const size_t pos, const size_t cursor, const NUSDEV activeDevice, bool usbMounted)
{
    startNewFrame();
    textToFrame(0, 6, gettext("Select a folder:"));

    boxToFrame(1, MAX_LINES - 3);

    char *toWrite = getToFrameBuffer();
    strcpy(toWrite, gettext("Press " BUTTON_A " to select"));
    strcat(toWrite, " || ");
    strcat(toWrite, gettext(BUTTON_B " to return"));
    strcat(toWrite, " || ");

    char *l = getStaticLineBuffer();
    strcpy(l, BUTTON_X " to switch to ");
    strcat(l, activeDevice == NUSDEV_USB ? "SD" : activeDevice == NUSDEV_SD ? "NAND"
            : usbMounted                                                    ? "USB"
                                                                            : "SD");
    strcat(toWrite, gettext(l));
    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, toWrite);

    strcpy(toWrite, gettext("Searching on"));
    strcat(toWrite, " => ");
    strcat(toWrite, activeDevice == NUSDEV_USB ? "USB" : activeDevice == NUSDEV_SD ? "SD"
                                                                                   : "NAND");
    strcat(toWrite, ":/install/");
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toWrite);

    foldersSize -= pos;
    if(foldersSize > MAX_FILEBROWSER_LINES)
        foldersSize = MAX_FILEBROWSER_LINES;

    for(size_t i = 0; i < foldersSize; ++i)
    {
        textToFrame(i + 2, 5, folders[i + pos]);
        if(cursor == i)
            arrowToFrame(i + 2, 1);
    }
    drawFrame();
}

char *fileBrowserMenu()
{
    size_t max_folders = 64;
    char **folders = MEMAllocFromDefaultHeap(sizeof(char *) * max_folders);
    if(folders == NULL)
        return false;

    const char sf[3] = "./";
    folders[0] = (char *)sf;

    size_t foldersSize = 1;
    size_t cursor, pos;
    NUSDEV usbMounted = getUSB();
    NUSDEV activeDevice = usbMounted ? NUSDEV_USB : NUSDEV_SD;
    bool mov;
    FSDirectoryHandle dir;
    char *ret = NULL;
    char *path;

refreshDirList:
    OSTime t = OSGetTime();
    for(int i = 1; i < foldersSize; ++i)
        MEMFreeToDefaultHeap(folders[i]);
    foldersSize = 1;
    cursor = pos = 0;

    path = getStaticPathBuffer(2);
    strcpy(path, (activeDevice & NUSDEV_USB) ? (usbMounted == NUSDEV_USB01 ? INSTALL_DIR_USB1 : INSTALL_DIR_USB2) : (activeDevice == NUSDEV_SD ? INSTALL_DIR_SD : INSTALL_DIR_MLC));
    if(FSOpenDir(__wut_devoptab_fs_client, getCmdBlk(), path, &dir, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
    {
        size_t len;
        FSDirectoryEntry entry;
        while(FSReadDir(__wut_devoptab_fs_client, getCmdBlk(), dir, &entry, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
            if(entry.info.flags & FS_STAT_DIRECTORY) // Check if it's a directory
            {
                if(foldersSize == max_folders)
                {
                    max_folders += 64;
                    if(max_folders < foldersSize) // buffer overrun, shouldn't happen
                    {
                        FSCloseDir(__wut_devoptab_fs_client, getCmdBlk(), dir, FS_ERROR_FLAG_ALL);
                        goto exitFileBrowserMenu;
                    }

                    char **tf = MEMAllocFromDefaultHeap(sizeof(char *) * max_folders);
                    if(tf == NULL)
                    {
                        FSCloseDir(__wut_devoptab_fs_client, getCmdBlk(), dir, FS_ERROR_FLAG_ALL);
                        goto exitFileBrowserMenu;
                    }

                    OSBlockMove(tf, folders, sizeof(char *) * foldersSize, false);
                    MEMFreeToDefaultHeap(folders);
                    folders = tf;
                }

                len = strlen(entry.name);
                folders[foldersSize] = MEMAllocFromDefaultHeap(len + 2);
                if(folders[foldersSize] == NULL)
                {
                    FSCloseDir(__wut_devoptab_fs_client, getCmdBlk(), dir, FS_ERROR_FLAG_ALL);
                    goto exitFileBrowserMenu;
                }

                OSBlockMove(folders[foldersSize], entry.name, len, false);
                folders[foldersSize][len] = '/';
                folders[foldersSize][++len] = '\0';
                ++foldersSize;
            }

        FSCloseDir(__wut_devoptab_fs_client, getCmdBlk(), dir, FS_ERROR_FLAG_ALL);
    }

    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));

    drawFBMenuFrame(folders, foldersSize, pos, cursor, activeDevice, usbMounted);

    mov = foldersSize >= MAX_FILEBROWSER_LINES;
    bool redraw = false;
    uint32_t oldHold = 0;
    size_t frameCount = 0;
    bool dpadAction;
    while(AppRunning())
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            drawFBMenuFrame(folders, foldersSize, pos, cursor, activeDevice, usbMounted);

        showFrame();
        if(vpad.trigger & VPAD_BUTTON_B)
            goto exitFileBrowserMenu;
        if(vpad.trigger & VPAD_BUTTON_A)
        {
            if(foldersSize != 1)
            {
                ret = getStaticPathBuffer(2);
                strcpy(ret, (activeDevice & NUSDEV_USB) ? (usbMounted == NUSDEV_USB01 ? INSTALL_DIR_USB1 : INSTALL_DIR_USB2) : (activeDevice == NUSDEV_SD ? INSTALL_DIR_SD : INSTALL_DIR_MLC));
                strcat(ret, folders[cursor + pos]);
                goto exitFileBrowserMenu;
            }
        }

        if(vpad.hold & VPAD_BUTTON_UP)
        {
            if(oldHold != VPAD_BUTTON_UP)
            {
                oldHold = VPAD_BUTTON_UP;
                frameCount = 30;
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
                            cursor = MAX_FILEBROWSER_LINES - 1;
                            pos = foldersSize - MAX_FILEBROWSER_LINES;
                        }
                    }
                    else
                        cursor = foldersSize - 1;
                }

                redraw = true;
            }
        }
        else if(vpad.hold & VPAD_BUTTON_DOWN)
        {
            if(oldHold != VPAD_BUTTON_DOWN)
            {
                oldHold = VPAD_BUTTON_DOWN;
                frameCount = 30;
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
                if(cursor + pos >= foldersSize - 1 || cursor >= MAX_FILEBROWSER_LINES - 1)
                {
                    if(!mov || ++pos + cursor >= foldersSize)
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
                    frameCount = 30;
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
                    pos += MAX_FILEBROWSER_LINES;
                    if(pos >= foldersSize)
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
                    frameCount = 30;
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
                    if(pos >= MAX_FILEBROWSER_LINES)
                        pos -= MAX_FILEBROWSER_LINES;
                    else
                        pos = foldersSize - MAX_FILEBROWSER_LINES;
                    cursor = 0;
                    redraw = true;
                }
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
                activeDevice = usbMounted ? NUSDEV_USB : NUSDEV_SD;
            }
            goto refreshDirList;
        }

        if(oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)))
            oldHold = 0;

        if(redraw)
        {
            drawFBMenuFrame(folders, foldersSize, pos, cursor, activeDevice, usbMounted);
            redraw = false;
        }
    }

exitFileBrowserMenu:
    for(int i = 1; i < foldersSize; ++i)
        MEMFreeToDefaultHeap(folders[i]);

    MEMFreeToDefaultHeap(folders);
    return ret;
}
