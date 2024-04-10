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

#include <file.h>
#include <queue.h>
#include <titles.h>
#include <tmd.h>

#include <curl/curl.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const char *name;
        uint16_t contents; // Contents count
        uint16_t dcontent; // Actual content number
        curl_off_t dlnow;
        curl_off_t dltotal;
        uint32_t eta;
        size_t cs;
    } downloadData;

    typedef struct
    {
        char *buf;
        size_t size;
    } RAMBUF;

#define DOWNLOAD_URL "https://ccs.wup.shop.nintendo.net/ccs/download/"

    bool initDownloader() __attribute__((__cold__));
    void deinitDownloader() __attribute__((__cold__));
    int downloadFile(const char *url, char *file, downloadData *data, FileType type, bool resume, QUEUE_DATA *queueData, RAMBUF *rambuf) __attribute__((__hot__));
    bool downloadTitle(const TMD *tmd, size_t tmdSize, const TitleEntry *titleEntry, const char *titleVer, char *folderName, bool inst, NUSDEV dlDev, bool toUSB, bool keepFiles, QUEUE_DATA *queueData);
    RAMBUF *allocRamBuf();
    void freeRamBuf(RAMBUF *rambuf);

#ifdef __cplusplus
}
#endif
