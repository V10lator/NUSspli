/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
 * Copyright (c) 2022 V10lator <v10lator@myway.de>                         *
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
#include <list.h>
#include <localisation.h>
#include <menu/main.h>
#include <menu/queue.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <state.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define MAX_ENTRIES (MAX_LINES - 4)
#ifndef NUSSPLI_LITE
#define SPACER     7
#define SPACER_END 14
#else
#define SPACER     4
#define SPACER_END 17
#endif

static void drawQueueMenu(LIST *titleQueue, size_t cursor, size_t pos)
{
    startNewFrame();
    boxToFrame(0, MAX_LINES - 3);

    char *toScreen = getToFrameBuffer();
    size_t i = 0;
    int p;
    TitleData *data;
    MCPRegion region;

    forEachListEntry(titleQueue, data)
    {
        if(pos)
        {
            --pos;
            continue;
        }

        if(cursor == i++)
            arrowToFrame(i, 1);

#ifndef NUSSPLI_LITE
        if(data->operation & OPERATION_DOWNLOAD)
        {
            switch(data->dlDev)
            {
                case NUSDEV_SD:
                    deviceToFrame(i, 4, DEVICE_TYPE_SD);
                    break;
                case NUSDEV_MLC:
                    deviceToFrame(i, 4, DEVICE_TYPE_NAND);
                    break;
                default:
                    deviceToFrame(i, 4, DEVICE_TYPE_USB);
                    break;
            }
        }

        if(data->operation & OPERATION_INSTALL)
#endif
            deviceToFrame(i, SPACER, data->toUSB ? DEVICE_TYPE_USB : DEVICE_TYPE_NAND);

        if(isDLC(data->tmd->tid))
        {
            p = sizeof("[DLC] ") - 1;
            OSBlockMove(toScreen, "[DLC] ", p, false);
        }
        else if(isUpdate(data->tmd->tid))
        {
            p = sizeof("[UPD] ") - 1;
            OSBlockMove(toScreen, "[UPD] ", p, false);
        }
        else
            p = 0;

        if(data->entry == NULL)
        {
            region = MCP_REGION_UNKNOWN;
            strcpy(toScreen + p, prettyDir(data->folderName));
        }
        else
        {
            region = data->entry->region;
            strcpy(toScreen + p, data->entry->name);
        }

        flagToFrame(i, SPACER + 3, region);
        textToFrameCut(i, SPACER + 6, toScreen, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * SPACER_END));

        if(i == MAX_ENTRIES)
            break;
    }

    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, localise("Press " BUTTON_B " to return"));

    strcpy(toScreen, localise(BUTTON_PLUS " to start the queue"));
    strcat(toScreen, " || ");
    strcat(toScreen, localise(BUTTON_MINUS " to delete an item"));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toScreen);

    drawFrame();
}

bool queueMenu()
{
    uint32_t oldHold = 0;
    bool dpadAction;
    size_t frameCount = 0;
    size_t cursor = 0;
    size_t pos = 0;
    bool redraw = true;
    LIST *titleQueue = getTitleQueue();
    bool mov = getListSize(titleQueue) >= MAX_ENTRIES;

    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            redraw = true;

        if(redraw)
        {
            drawQueueMenu(titleQueue, cursor, pos);
            redraw = false;
        }
        showFrame();

        if(vpad.trigger & VPAD_BUTTON_B)
            return false;

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
                    --cursor;
                else if(pos)
                    --pos;

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
                if(cursor < getListSize(titleQueue) - pos - 1 && cursor < MAX_ENTRIES - 1)
                    ++cursor;
                else if(mov && cursor + ++pos == getListSize(titleQueue))
                    --pos;

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
                    pos += MAX_ENTRIES;
                    if(pos >= getListSize(titleQueue))
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
                    if(pos >= MAX_ENTRIES)
                        pos -= MAX_ENTRIES;
                    else
                        pos = getListSize(titleQueue) - MAX_ENTRIES;
                    cursor = 0;
                    redraw = true;
                }
            }
        }

        if(vpad.trigger & VPAD_BUTTON_PLUS)
        {
            if(proccessQueue())
            {
                showFinishedScreen(NULL, FINISHING_OPERATION_QUEUE);
                return true;
            }

            redraw = true;
        }

        if(vpad.trigger & VPAD_BUTTON_MINUS)
        {
            removeFromQueue(cursor + pos);
            if(getListSize(titleQueue) == 0)
                return false;

            if(cursor + pos == getListSize(titleQueue))
            {
                if(cursor)
                    --cursor;
                else if(pos)
                    --pos;
            }

            mov = getListSize(titleQueue) >= MAX_ENTRIES;
            redraw = true;
        }

        if(oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)))
            oldHold = 0;
    }

    return true;
}
