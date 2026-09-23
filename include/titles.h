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

#pragma once

#include <wut-fixups.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <gtitles.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_TITLENAME_LENGTH 256 // incl '\0'
#define MCP_REGION_UNKNOWN   0

    typedef enum
    {
        TID_HIGH_GAME = 0x00050000,
        TID_HIGH_DEMO = 0x00050002,
        TID_HIGH_SYSTEM_APP = 0x00050010,
        TID_HIGH_SYSTEM_DATA = 0x0005001B,
        TID_HIGH_SYSTEM_APPLET = 0x00050030,
        TID_HIGH_VWII_IOS = 0x00000007,
        TID_HIGH_VWII_SYSTEM_APP = 0x00070002,
        TID_HIGH_VWII_SYSTEM = 0x00070008,
        TID_HIGH_DLC = 0x0005000C,
        TID_HIGH_UPDATE = 0x0005000E,
    } TID_HIGH;

#define getTidHighFromTid(tid) ((uint32_t)(tid >> 32))

    const TitleEntry *getTitleEntries(TITLE_CATEGORY cat);
    const TitleEntry *getTitleEntryByTid(uint64_t tid);
    const char *tid2name(const char *tid);
    bool name2tid(const char *name, char *out);

#define isGame(tid)   (getTidHighFromTid(tid) == TID_HIGH_GAME)
#define isDLC(tid)    (getTidHighFromTid(tid) == TID_HIGH_DLC)
#define isUpdate(tid) (getTidHighFromTid(tid) == TID_HIGH_UPDATE)
#define isDemo(tid)   (getTidHighFromTid(tid) == TID_HIGH_DEMO)

// Title IDs only ever differ in the nibble that marks game (0x0) / update (0xE) / DLC (0xC)
#define TID_TO_BASE(tid)    (((uint64_t)(tid)) & 0xFFFFFFF0FFFFFFFFULL)
#define BASE_TO_UPDATE(tid) (TID_TO_BASE(tid) | 0x0000000E00000000ULL)
#define BASE_TO_DLC(tid)    (TID_TO_BASE(tid) | 0x0000000C00000000ULL)
// Derive the main game's TID: the demo type sits in bits 32-35
// (TID_HIGH_DEMO 0x00050002 vs TID_HIGH_GAME 0x00050000) and the
// lowest nibble of a demo TID differs from the retail one as well.
#define DEMO_TO_GAME(tid)  (((uint64_t)(tid)) & 0xFFFFFFF0FFFFFFF0ULL)

#define getH3size(appSize) (ceil(((double)appSize) / 0x1000000D) * 0x14)

#ifdef __cplusplus
}
#endif
