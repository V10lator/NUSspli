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

#include <file.h>
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

    typedef enum
    {
        FINISHING_OPERATION_INSTALL,
        FINISHING_OPERATION_DEINSTALL,
        FINISHING_OPERATION_DOWNLOAD,
        FINISHING_OPERATION_QUEUE
    } FINISHING_OPERATION;

    void addToScreenLog(const char *str, ...);
    void clearScreenLog();
    void writeScreenLog(int line);
    void drawErrorFrame(const char *text, ErrorOptions option);
    void showErrorFrame(const char *text);
    bool checkSystemTitle(uint64_t tid, MCPRegion region, bool deinstall);
    bool checkSystemTitleFromEntry(const TitleEntry *entry, bool deinstall);
    bool checkSystemTitleFromTid(uint64_t tid, bool deinstall);
    bool checkSystemTitleFromListType(MCPTitleListType *entry, bool deinstall);
    const char *prettyDir(const char *dir);
    void showFinishedScreen(const char *titleName, FINISHING_OPERATION op);
    void showNoSpaceOverlay(NUSDEV dev);
    void humanize(uint64_t size, char *out);
    void getFreeSpaceString(NUSDEV dev, char *out);
    bool showExitOverlay();

#ifdef __cplusplus
}
#endif
