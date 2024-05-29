/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2023 V10lator <v10lator@myway.de>                    *
 * Copyright (c) 2022-2023 Xpl0itU <DaThinkingChair@protonmail.com>        *
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

#include <stdlib.h>

#include <config.h>
#include <messages.h>
#include <osdefs.h>
#include <thread.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/messagequeue.h>
#include <nn/acp/drcled_c.h>
#include <padscore/wpad.h>
#include <vpad/input.h>
#pragma GCC diagnostic pop

#define RUMBLE_QUEUE_SIZE 2
#define LED_ON            1
#define LED_OFF           0

static OSThread *rumbleThread;
static const uint8_t pattern[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static OSMessageQueue rumble_queue;
static OSMessage rumble_msg[2];
static uint32_t pId;

static int rumbleThreadMain(int argc, const char **argv)
{
    (void)argc;
    (void)argv;

    OSMessage msg;

    do
    {
        OSReceiveMessage(&rumble_queue, &msg, OS_MESSAGE_FLAGS_BLOCKING);
        if(msg.message == NUSSPLI_MESSAGE_NONE)
        {
            for(WPADChan j = 0; j < 4; ++j)
                WPADControlMotor(j, 1);

            OSSleepTicks(OSSecondsToTicks(1));

            for(WPADChan j = 0; j < 4; ++j)
                WPADControlMotor(j, 0);
        }
    } while(msg.message != NUSSPLI_MESSAGE_EXIT);

    return 0;
}

bool initNotifications()
{
    pId = GetPersistentId();
    OSInitMessageQueueEx(&rumble_queue, rumble_msg, RUMBLE_QUEUE_SIZE, "NUSspli rumble queue");
    rumbleThread = startThread("NUSspli Rumble", THREAD_PRIORITY_LOW, STACKSIZE_SMALL, rumbleThreadMain, 0, NULL, OS_THREAD_ATTRIB_AFFINITY_ANY);
    return rumbleThread != NULL;
}

void deinitNotifications()
{
    if(rumbleThread != NULL)
    {
        OSMessage msg = { .message = NUSSPLI_MESSAGE_EXIT };
        OSSendMessage(&rumble_queue, &msg, OS_MESSAGE_FLAGS_BLOCKING);
        stopThread(rumbleThread, NULL);
    }
}

void startNotification()
{
    if(getNotificationMethod() & NOTIF_METHOD_RUMBLE)
    {
        OSMessage msg = { .message = NUSSPLI_MESSAGE_NONE };
        OSSendMessage(&rumble_queue, &msg, OS_MESSAGE_FLAGS_NONE);
        VPADControlMotor(VPAD_CHAN_0, (uint8_t *)pattern, 120);
    }
    if(getNotificationMethod() & NOTIF_METHOD_LED)
        ACPTurnOnDrcLed(pId, LED_ON);
}

void stopNotification()
{
    if(getNotificationMethod() & NOTIF_METHOD_LED)
        ACPTurnOnDrcLed(pId, LED_OFF);
}
