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

#include <string.h>

#include <input.h>
#include <localisation.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <staticMem.h>
#include <updater.h>
#include <utils.h>

static void drawUpdateMenuFrame(const char *newVersion)
{
    startNewFrame();
    boxToFrame(0, 5);
    textToFrame(1, ALIGNED_CENTER, "NUSspli");
    char *toScreen = getToFrameBuffer();
    strcpy(toScreen, "NUS simple packet loader/installer [");
    strcat(toScreen, NUSSPLI_VERSION);
    strcat(toScreen, "]");
    textToFrame(3, ALIGNED_CENTER, toScreen);

    textToFrame(4, ALIGNED_CENTER, NUSSPLI_COPYRIGHT);

    textToFrame(7, 0, localise("Update available!"));
    lineToFrame(MAX_LINES - 3, SCREEN_COLOR_WHITE);
    strcpy(toScreen, localise("Press " BUTTON_A " to update to"));
    strcat(toScreen, " ");
    strcat(toScreen, newVersion);
    textToFrame(MAX_LINES - 2, 0, toScreen);
    textToFrame(MAX_LINES - 1, 0, localise("Press " BUTTON_B " to cancel"));
    drawFrame();
}

bool updateMenu(const char *newVersion, NUSSPLI_TYPE type)
{
    drawUpdateMenuFrame(newVersion);

    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            drawUpdateMenuFrame(newVersion);

        showFrame();

        if(vpad.trigger & VPAD_BUTTON_A)
        {
            if(update(newVersion, type))
            {
                relaunch();
                return true;
            }
            else
                break;
        }
        if(vpad.trigger & VPAD_BUTTON_B)
            break;
    }

    return false;
}
