/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
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

#include <input.h>
#include <installer.h>
#include <localisation.h>
#include <menu/config.h>
#include <menu/filebrowser.h>
#include <menu/installer.h>
#include <menu/insttitlebrowser.h>
#include <menu/main.h>
#include <menu/titlebrowser.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <staticMem.h>
#include <ticket.h>
#include <utils.h>

#include <coreinit/memdefaultheap.h>

#include <string.h>

static void drawMainMenuFrame()
{
    startNewFrame();
    boxToFrame(0, 5);
    textToFrame(1, ALIGNED_CENTER, "NUSspli");
    textToFrame(3, ALIGNED_CENTER, "NUS simple packet loader/installer [" NUSSPLI_VERSION "]");

    textToFrame(4, ALIGNED_CENTER, NUSSPLI_COPYRIGHT);

    textToFrame(12, 0, gettext("Press " BUTTON_A " to download content"));
    textToFrame(13, 0, gettext("Press " BUTTON_X " to install content"));
    textToFrame(14, 0, gettext("Press " BUTTON_Y " to generate a fake <title.tik> file"));
    textToFrame(15, 0, gettext("Press " BUTTON_RIGHT " to uninstall a title"));
    textToFrame(16, 0, gettext("Press " BUTTON_LEFT " for options"));
    textToFrame(17, 0, gettext("Press " BUTTON_HOME " or " BUTTON_B " to exit"));

    textToFrame(8, MAX_CHARS - 27, gettext("Developers:"));
    textToFrame(9, MAX_CHARS - 26, "• DaThinkingChair");
    textToFrame(10, MAX_CHARS - 26, "• Pokes303");
    textToFrame(11, MAX_CHARS - 26, "• V10lator");

    textToFrame(13, MAX_CHARS - 27, gettext("Thanks to:"));
    textToFrame(14, MAX_CHARS - 26, "• E1ite007");
    textToFrame(15, MAX_CHARS - 26, "• SDL");
    textToFrame(16, MAX_CHARS - 26, "• WUP installer");

    textToFrame(18, MAX_CHARS - 27, gettext("Beta testers:"));
    textToFrame(19, MAX_CHARS - 26, "• jacobsson");
    textToFrame(20, MAX_CHARS - 26, "• LuckyDingo");
    textToFrame(21, MAX_CHARS - 26, "• Vague Rant");
    drawFrame();
}

void mainMenu()
{
    bool redraw = true;
    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            redraw = true;

        if(redraw)
        {
            drawMainMenuFrame();
            redraw = false;
        }
        showFrame();

        if(vpad.trigger & VPAD_BUTTON_A)
        {
            titleBrowserMenu();
            redraw = true;
        }
        else if(vpad.trigger & VPAD_BUTTON_X)
        {
            char *dir = fileBrowserMenu();
            if(!AppRunning(true))
                return;

            if(dir != NULL)
                installerMenu(dir);

            redraw = true;
        }
        else if(vpad.trigger & VPAD_BUTTON_LEFT)
        {
            configMenu();
            redraw = true;
        }
        else if(vpad.trigger & VPAD_BUTTON_Y)
        {
            generateFakeTicket();
            redraw = true;
        }
        else if(vpad.trigger & VPAD_BUTTON_RIGHT)
        {
            ititleBrowserMenu();
            redraw = true;
        }
        else if(vpad.trigger & VPAD_BUTTON_B)
        {
            drawByeFrame();
            return;
        }
    }
}
