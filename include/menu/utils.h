/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019 Pokes303                                             *
 * Copyright (c) 2020 V10lator <v10lator@myway.de>                         *
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

#include <titles.h>

#include <coreinit/mcp.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        ANY_RETURN = 0xFFFFFFFF,
        B_RETURN = 1,
        A_CONTINUE = 1 << 1,
        Y_RETRY = 1 << 2,
    } ErrorOptions;

    void addToScreenLog(const char *str, ...);
    void clearScreenLog();
    void writeScreenLog(int line);
    void drawErrorFrame(const char *text, ErrorOptions option);
    bool checkSystemTitle(uint64_t tid, MCPRegion region);
    bool checkSystemTitleFromEntry(const TitleEntry *entry);
    bool checkSystemTitleFromTid(uint64_t tid);
    bool checkSystemTitleFromListType(MCPTitleListType *entry);
    const char *prettyDir(const char *dir);

#ifdef __cplusplus
}
#endif
