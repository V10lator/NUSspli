/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
 * Copyright (c) 2022-2024 V10lator <v10lator@myway.de>                    *
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
#include <ui.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define MAX_ENTRIES (MAX_LINES - 4)
#define SPACER      7
#define SPACER_END  14

static LIST *titleQueue;
static uint32_t oldHold;
static size_t frameCount;
static size_t cursor;
static size_t pos;
static bool mov;

static void enterQueueMenu(void *param)
{
    (void)param;
    oldHold = 0;
    frameCount = 0;
    cursor = 0;
    pos = 0;
    titleQueue = getTitleQueue();
    mov = getListSize(titleQueue) >= MAX_ENTRIES;
}

static void renderQueueMenu()
{
    startNewFrame();
    boxToFrame(0, MAX_LINES - 3);

    char toScreen[FS_MAX_PATH + 256];
    size_t i = 0;
    size_t drawPos = pos; // local copy: the paging state must survive rendering
    int p;
    TitleData *data;
    MCPRegion region;

    forEachListEntry(titleQueue, data)
    {
        if(drawPos)
        {
            --drawPos;
            continue;
        }

        if(cursor == i++)
            arrowToFrame(i, 1);

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
}

static void updateQueueMenu()
{
    bool dpadAction;

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        uiSetResult(0);
        uiPop();
        return;
    }

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
            uiInvalidate();
            if(cursor)
                --cursor;
            else if(pos)
                --pos;
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
            uiInvalidate();
            if(cursor < getListSize(titleQueue) - pos - 1 && cursor < MAX_ENTRIES - 1)
                ++cursor;
            else if(mov && cursor + ++pos == getListSize(titleQueue))
                --pos;
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
                uiInvalidate();
                pos += MAX_ENTRIES;
                if(pos >= getListSize(titleQueue))
                    pos = 0;

                cursor = 0;
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
                uiInvalidate();
                if(pos >= MAX_ENTRIES)
                    pos -= MAX_ENTRIES;
                else // Wrap to the last page RIGHT paging can reach, so paging can't oscillate
                    pos = ((getListSize(titleQueue) - 1) / MAX_ENTRIES) * MAX_ENTRIES;
                cursor = 0;
            }
        }
    }

    if(vpad.trigger & VPAD_BUTTON_PLUS)
    {
        if(proccessQueue())
        {
            showFinishedScreen(NULL, FINISHING_OPERATION_QUEUE);
            uiSetResult(1);
            uiPop();
            return;
        }

        // The queue may have shrunk on a mid-queue failure: re-clamp the view
        size_t size = getListSize(titleQueue);
        if(size == 0)
        {
            uiSetResult(0);
            uiPop();
            return;
        }

        mov = size >= MAX_ENTRIES;
        if(pos >= size)
            pos = 0;
        if(cursor + pos >= size)
            cursor = size - pos - 1;
    }

    if(vpad.trigger & VPAD_BUTTON_MINUS)
    {
        removeFromQueue(cursor + pos);
        if(getListSize(titleQueue) == 0)
        {
            uiSetResult(0);
            uiPop();
            return;
        }

        if(cursor + pos == getListSize(titleQueue))
        {
            if(cursor)
                --cursor;
            else if(pos)
                --pos;
        }

        mov = getListSize(titleQueue) >= MAX_ENTRIES;
    }

    if(oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)))
        oldHold = 0;
}

static const UIScreen queueScreen = {
    .name = "queue",
    .buttons = VPAD_BUTTON_B | VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT | VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS,
    .enter = enterQueueMenu,
    .update = updateQueueMenu,
    .render = renderQueueMenu,
};

bool queueMenu()
{
    return uiModal(&queueScreen, NULL);
}
