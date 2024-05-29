/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2021 V10lator <v10lator@myway.de>                    *
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

#pragma once

#include <wut-fixups.h>

#include <stdbool.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <padscore/wpad.h>
#include <vpad/input.h>
#pragma GCC diagnostic pop

#define BUTTON_A          "\uE000"
#define BUTTON_B          "\uE001"
#define BUTTON_X          "\uE002"
#define BUTTON_Y          "\uE003"

#define BUTTON_PLUS       "\uE045"
#define BUTTON_MINUS      "\uE046"

#define BUTTON_LEFT       "\uE07B"
#define BUTTON_RIGHT      "\uE07C"
#define BUTTON_UP         "\uE079"
#define BUTTON_DOWN       "\uE07A"

#define BUTTON_LEFT_RIGHT "\uE07E"

#define BUTTON_HOME       "\uE044"

#define BUTTON_POWER      "\uE040"

#ifdef __cplusplus
extern "C"
{
#endif

    extern VPADStatus vpad;

    typedef enum
    {
        CHECK_NONE, // No check
        CHECK_NUMERICAL, // Only numbers
        CHECK_HEXADECIMAL, // Only hex
        CHECK_ALPHANUMERICAL, // Only letters or numbers
        CHECK_URL,
    } KeyboardChecks;

    typedef enum
    {
        KEYBOARD_TYPE_RESTRICTED,
        KEYBOARD_TYPE_NORMAL
    } KeyboardType;

    typedef enum
    {
        KEYBOARD_LAYOUT_NORMAL = 0,
        KEYBOARD_LAYOUT_TID = 2,
    } KeyboardLayout;

    typedef enum
    {
        CT_WPAD_0 = WPAD_CHAN_0,
        CT_WPAD_1 = WPAD_CHAN_1,
        CT_WPAD_2 = WPAD_CHAN_2,
        CT_WPAD_3 = WPAD_CHAN_3,
        CT_VPAD_0 = 4 + VPAD_CHAN_0,
    } ControllerType;

    bool SWKBD_Init() __attribute__((__cold__));
    void SWKBD_Shutdown() __attribute__((__cold__));

    void readInput() __attribute__((__hot__));
    bool showKeyboard(KeyboardLayout layout, KeyboardType type, char *output, KeyboardChecks check, int maxlength, bool limit, const char *input, const char *okStr);

#ifdef __cplusplus
}
#endif
