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

#include <string.h>

#include <input.h>
#include <installer.h>
#include <localisation.h>
#include <menu/config.h>
#include <menu/installer.h>
#include <menu/insttitlebrowser.h>
#include <menu/logs.h>
#include <menu/main.h>
#include <menu/titlebrowser.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <staticMem.h>
#include <ticket.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

static int cursorPos = 11;

static void drawMainMenuFrame()
{
    startNewFrame();
    boxToFrame(0, 5);
    textToFrame(1, ALIGNED_CENTER, "NUSspli");
    textToFrame(3, ALIGNED_CENTER,
#ifndef NUSSPLI_LITE
        "NUS simple packet loader/installer"
#else
        "Lite"
#endif
        " [" NUSSPLI_VERSION "]");

    textToFrame(4, ALIGNED_CENTER, NUSSPLI_COPYRIGHT);

    arrowToFrame(cursorPos, 0);

    int line = 11;
#ifndef NUSSPLI_LITE
    textToFrame(line++, 4, localise("Download content"));
#endif
    textToFrame(line++, 4, localise("Install content"));
#ifndef NUSSPLI_LITE
    textToFrame(line++, 4, localise("Generate a fake <title.tik> file"));
#endif
    textToFrame(line++, 4, localise("Browse installed titles"));
    textToFrame(line++, 4, localise("Options"));
    textToFrame(line++, 4, localise("Logs"));

    textToFrame(7, MAX_CHARS - 27, localise("Developers:"));
    textToFrame(8, MAX_CHARS - 26, "• DaThinkingChair");
    textToFrame(9, MAX_CHARS - 26, "• Pokes303");
    textToFrame(10, MAX_CHARS - 26, "• V10lator");

    textToFrame(12, MAX_CHARS - 27, localise("Thanks to:"));
    textToFrame(13, MAX_CHARS - 26, "• E1ite007");
    textToFrame(14, MAX_CHARS - 26, "• SDL");
    textToFrame(15, MAX_CHARS - 26, "• WUP installer");

    textToFrame(17, MAX_CHARS - 27, localise("Beta testers:"));
    textToFrame(18, MAX_CHARS - 26, "• jacobsson");
    textToFrame(19, MAX_CHARS - 26, "• LuckyDingo");
    textToFrame(20, MAX_CHARS - 26, "• Vague Rant");

    lineToFrame(MAX_LINES - 2, SCREEN_COLOR_WHITE);
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, localise("Press " BUTTON_HOME " or " BUTTON_B " to exit"));

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

        if(vpad.trigger & VPAD_BUTTON_B)
        {
            drawByeFrame();
            return;
        }

        if(vpad.trigger & VPAD_BUTTON_A)
        {
            switch(cursorPos)
            {
                case 11:
#ifndef NUSSPLI_LITE
                    titleBrowserMenu();
                    break;
                case 12:
#endif
                    installerMenu();
                    break;
#ifndef NUSSPLI_LITE
                case 13:
                    generateFakeTicket();
                    break;
                case 14:
#else
                case 12:
#endif
                    ititleBrowserMenu();
                    break;
#ifndef NUSSPLI_LITE
                case 15:
#else
                case 13:
#endif
                    configMenu();
                    break;
#ifndef NUSSPLI_LITE
                case 16:
#else
                case 14:
#endif
                    logsMenu();
                    break;
            }

            redraw = true;
        }
        else if(vpad.trigger & VPAD_BUTTON_DOWN)
        {
#ifndef NUSSPLI_LITE
            if(++cursorPos == 17)
#else
            if(++cursorPos == 15)
#endif
                cursorPos = 11;

            redraw = true;
        }
        else if(vpad.trigger & VPAD_BUTTON_UP)
        {
            if(--cursorPos == 10)
#ifndef NUSSPLI_LITE
                cursorPos = 16;
#else
                cursorPos = 14;
#endif

            redraw = true;
        }
    }
}
