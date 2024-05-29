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

#ifndef NUSSPLI_LITE

#include <wut-fixups.h>

#include <stdint.h>

#include <input.h>
#include <menu/download.h>
#include <menu/predownload.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <staticMem.h>
#include <titles.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#pragma GCC diagnostic pop

bool downloadMenu()
{
    char titleID[17];
    char titleVer[33];
    char folderName[FS_MAX_PATH - 11];
    titleID[0] = titleVer[0] = folderName[0] = '\0';

    if(!showKeyboard(KEYBOARD_LAYOUT_TID, KEYBOARD_TYPE_RESTRICTED, titleID, CHECK_HEXADECIMAL, 16, true, "00050000101", NULL))
        return false;

    if(!AppRunning(true))
        return true;

    toLowercase(titleID);
    uint64_t tid;
    hexToByte(titleID, (uint8_t *)&tid);

    const TitleEntry *entry = getTitleEntryByTid(tid);
    const TitleEntry e = { .name = "UNKNOWN", .tid = tid, .region = MCP_REGION_UNKNOWN, .key = 99 };
    if(entry == NULL)
        entry = &e;

    predownloadMenu(entry);
    return true;
}

#endif // ifndef NUSSPLI_LITE
