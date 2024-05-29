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

#include <stdbool.h>

#include <file.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#pragma GCC diagnostic pop

#ifdef __cplusplus
extern "C"
{
#endif

    bool initFS(bool validCfw) __attribute__((__cold__));
    void initFSSpace() __attribute__((__cold__));
    void checkSpaceThread();
    void deinitFS(bool validCfw) __attribute__((__cold__));
    FSAClientHandle getFSAClient() __attribute__((__hot__));
    NUSDEV getUSB();
    void claimSpace(NUSDEV dev, uint64_t size);
    void freeSpace(NUSDEV dev, uint64_t size);
    uint64_t getFreeSpace(NUSDEV dev);
    bool checkFreeSpace(NUSDEV dev, uint64_t size);
    uint64_t getSpace(NUSDEV dev);

#ifdef __cplusplus
}
#endif
