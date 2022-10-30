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

#include <file.h>

#include <coreinit/filesystem_fsa.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool initFS() __attribute__((__cold__));
    void deinitFS() __attribute__((__cold__));
    FSAClientHandle getFSAClient() __attribute__((__hot__));
    NUSDEV getUSB();
    bool checkFreeSpace(NUSDEV dlDev, uint64_t size);

#ifdef __cplusplus
}
#endif
