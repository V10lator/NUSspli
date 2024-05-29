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

#pragma once

#include <wut-fixups.h>

#include <stdbool.h>

#include <tmd.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#pragma GCC diagnostic pop

#define NUSDIR_SD        "/vol/app_sd/"
#define NUSDIR_USB1      "/vol/storage_usb01/"
#define NUSDIR_USB2      "/vol/storage_usb02/"
#define NUSDIR_MLC       "/vol/storage_mlc01/"

#define INSTALL_DIR_SD   NUSDIR_SD "install/"
#define INSTALL_DIR_USB1 NUSDIR_USB1 "install/"
#define INSTALL_DIR_USB2 NUSDIR_USB2 "install/"
#define INSTALL_DIR_MLC  NUSDIR_MLC "install/"
#define IO_BUFSIZE       (128 * 1024) // 128 KB

#define FS_ALIGN(x)      ((x + 0x3F) & ~(0x3F))

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        // Real file types, specify exactly one!
        FILE_TYPE_TMD = 1, // 00000001
        FILE_TYPE_TIK = 1 << 1, // 00000010
        FILE_TYPE_CERT = 1 << 2, // 00000100
        FILE_TYPE_APP = 1 << 3, // 00001000
        FILE_TYPE_H3 = 1 << 4, // 00010000
        FILE_TYPE_JSON = 1 << 5, // 00100000

        // Extra flags, OR them to the real file type.
        FILE_TYPE_TORAM = 1 << 6, // 01000000
    } FileType;

    typedef enum
    {
        NUSDEV_NONE = 0x00,
        NUSDEV_USB01 = 0x01,
        NUSDEV_USB02 = 0x02,
        NUSDEV_USB = NUSDEV_USB01 | NUSDEV_USB02,
        NUSDEV_SD = 0x04,
        NUSDEV_MLC = 0x08,
    } NUSDEV;

    typedef enum
    {
        TMD_STATE_GOOD,
        TMD_STATE_BAD,
        TMD_STATE_TECONMOON,
    } TMD_STATE;

    bool fileExists(const char *path) __attribute__((__hot__));
    bool dirExists(const char *path) __attribute__((__hot__));
    FSError removeDirectory(const char *path) __attribute__((__hot__));
    FSError moveDirectory(const char *src, const char *dest);
    FSError createDirectory(const char *path);
    bool createDirRecursive(const char *dir) __attribute__((__hot__));
    const char *translateFSErr(FSError err) __attribute__((__cold__));
    size_t getFilesize(const char *path) __attribute__((__hot__));
    NUSDEV getDevFromPath(const char *path);
    size_t readFile(const char *path, void **buffer) __attribute__((__hot__));
    size_t getDirsize(const char *path);
    TMD *getTmd(const char *dir, bool allowNoIntro);
    TMD_STATE verifyTmd(const TMD *tmd, size_t size) __attribute__((__hot__));

#ifdef __cplusplus
}
#endif
