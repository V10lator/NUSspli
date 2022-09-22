/***************************************************************************
 * This file is part of NUSspli.                                           *
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

#include <file.h>
#include <input.h>
#include <installer.h>
#include <localisation.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <tmd.h>
#include <utils.h>

#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>

#include <string.h>

static void drawInstallerMenuFrame(const char *name, NUSDEV dev, bool keepFiles)
{
    startNewFrame();

    textToFrame(0, 0, name);

    lineToFrame(MAX_LINES - 6, SCREEN_COLOR_WHITE);
    textToFrame(MAX_LINES - 5, 0, gettext("Press " BUTTON_A " to install to USB"));
    textToFrame(MAX_LINES - 4, 0, gettext("Press " BUTTON_X " to install to NAND"));
    textToFrame(MAX_LINES - 3, 0, gettext("Press " BUTTON_B " to return"));

    lineToFrame(MAX_LINES - 2, SCREEN_COLOR_WHITE);
    if(dev != NUSDEV_SD)
        textToFrame(MAX_LINES - 1, 0, gettext("WARNING: Files on USB/NAND will always be deleted after installing!"));
    else
    {
        char *toFrame = getToFrameBuffer();
        strcpy(toFrame, "Press " BUTTON_LEFT " to ");
        strcat(toFrame, keepFiles ? "delete" : "keep");
        strcat(toFrame, " files after the installation");
        textToFrame(MAX_LINES - 1, 0, gettext(toFrame));
    }

    drawFrame();
}

static NUSDEV getDevFromPath(const char *path)
{
    if(strncmp(NUSDIR_SD, path, strlen(NUSDIR_SD)) == 0)
        return NUSDEV_SD;
    if(strncmp(NUSDIR_USB1, path, strlen(NUSDIR_USB1)) == 0)
        return NUSDEV_USB01;
    if(strncmp(NUSDIR_USB2, path, strlen(NUSDIR_USB2)) == 0)
        return NUSDEV_USB02;
    if(strncmp(NUSDIR_MLC, path, strlen(NUSDIR_MLC)) == 0)
        return NUSDEV_MLC;

    return NUSDEV_NONE;
}

void installerMenu(const char *dir)
{
    NUSDEV dev = getDevFromPath(dir);
    bool keepFiles = dev == NUSDEV_SD;
    const char *nd = prettyDir(dir);

    drawInstallerMenuFrame(nd, dev, keepFiles);

    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            drawInstallerMenuFrame(nd, dev, keepFiles);

        showFrame();

        if((vpad.trigger & VPAD_BUTTON_A) || (vpad.trigger & VPAD_BUTTON_X))
        {
            TMD *tmd = getTmd(dir);
            if(tmd != NULL)
            {
                if(checkSystemTitleFromTid(tmd->tid))
                {
                    if(install(nd, false, dev, dir, vpad.trigger & VPAD_BUTTON_A, keepFiles, tmd))
                        showFinishedScreen(nd, FINISHING_OPERATION_INSTALL);
                }

                MEMFreeToDefaultHeap(tmd);
            }
            else
            {
                drawErrorFrame(gettext("Invalid title.tmd file!"), ANY_RETURN);

                while(AppRunning(true))
                {
                    if(app == APP_STATE_BACKGROUND)
                        continue;
                    if(app == APP_STATE_RETURNING)
                        drawErrorFrame(gettext("Invalid title.tmd file!"), ANY_RETURN);

                    showFrame();
                    if(vpad.trigger)
                        break;
                }
            }

            return;
        }
        if(vpad.trigger & VPAD_BUTTON_B)
            return;

        if(vpad.trigger & VPAD_BUTTON_LEFT && dev == NUSDEV_SD)
        {
            keepFiles = !keepFiles;
            drawInstallerMenuFrame(nd, dev, keepFiles);
        }
    }
}
