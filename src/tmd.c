/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2024 V10lator <v10lator@myway.de>                    *
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

#include <crypto.h>
#include <file.h>
#include <filesystem.h>
#include <ioQueue.h>
#include <menu/utils.h>
#include <renderer.h>
#include <staticMem.h>
#include <tmd.h>
#include <utils.h>

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include <coreinit/filesystem_fsa.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>

#include <mbedtls/sha256.h>

// This uses informations from https://github.com/Maschell/nuspacker
TMD_STATE verifyTmd(const TMD *tmd, size_t size)
{
    if(size >= sizeof(TMD) + (sizeof(TMD_CONTENT) * 9)) // Minimal title.tmd size
    {
        if(tmd->num_contents == tmd->content_infos[0].count) // Validate num_contents
        {
            if(tmd->num_contents) // Check for at least 1 contents (.app files. Some system titles seem to have 1 only)
            {
                if(size == (sizeof(TMD) + 0x700) + (sizeof(TMD_CONTENT) * tmd->num_contents) || // Most title.tmd files have a certificate attached to the end. This certificate is 0x700 bytes long.
                    size == sizeof(TMD) + (sizeof(TMD_CONTENT) * tmd->num_contents)) // Some (like ones made with NUSPacker) don't have a certificate attached through.
                {
                    // Teconmoon workaround
                    bool teconmoon = true;
                    for(int i = 0; i < 8; ++i)
                    {
                        if(tmd->hash[i] != 0)
                        {
                            teconmoon = false;
                            break;
                        }
                    }

                    if(!teconmoon)
                    {
                        // Validate TMD hash
                        uint32_t hash[8];
                        uint8_t *ptr = ((uint8_t *)tmd) + (sizeof(TMD) - (sizeof(TMD_CONTENT_INFO) * 64));
                        mbedtls_sha256(ptr, sizeof(TMD_CONTENT_INFO) * 64, (unsigned char *)hash, 0);
                        for(int i = 0; i < 8; ++i)
                        {
                            if(hash[i] != tmd->hash[i])
                            {
                                debugPrintf("Invalid title.tmd file (tmd hash mismatch)");
                                return TMD_STATE_BAD;
                            }
                        }

                        // Validate content hash
                        ptr += sizeof(TMD_CONTENT_INFO) * 64;
                        mbedtls_sha256(ptr, sizeof(TMD_CONTENT) * tmd->num_contents, (unsigned char *)hash, 0);
                        for(int i = 0; i < 8; ++i)
                        {
                            if(hash[i] != tmd->content_infos[0].hash[i])
                            {
                            invalidContentHash:
                                debugPrintf("Invalid title.tmd file (content hash mismatch)");
                                return TMD_STATE_BAD;
                            }
                        }
                    }
                    else // Teconmoon hashes are all zeroes
                    {
                        for(int i = 0; i < 8; ++i)
                            if(tmd->content_infos[0].hash[i] != 0)
                                goto invalidContentHash;

                        return TMD_STATE_TECONMOON;
                    }

                    // Validate content
                    for(int i = 0; i < tmd->num_contents; ++i)
                    {
                        // Validate content index
                        if(tmd->contents[i].index != i)
                        {
                            debugPrintf("Invalid title.tmd file (content: %d, index: %u)", i, tmd->contents[i].index);
                            return TMD_STATE_BAD;
                        }
                        // Validate content type
                        if(!((tmd->contents[i].type & TMD_CONTENT_TYPE_CONTENT) && (tmd->contents[i].type & TMD_CONTENT_TYPE_ENCRYPTED)))
                        {
                            debugPrintf("Invalid title.tmd file (content: %u, type: 0x%04X)", i, tmd->contents[i].type);
                            return TMD_STATE_BAD;
                        }
                        // Validate content size
                        if(tmd->contents[i].size == 0 || tmd->contents[i].size > (uint64_t)1024 * 1024 * 1024 * 4)
                        {
                            debugPrintf("Invalid title.tmd file (content: %d, size: %llu)", i, tmd->contents[i].size);
                            return TMD_STATE_BAD;
                        }
                    }

                    return TMD_STATE_GOOD;
                }
                else
                    debugPrintf("Wrong title.tmd filesize (num_contents: %u, filesize: 0x%X)", tmd->num_contents, size);
            }
            else
                debugPrintf("Invalid title.tmd file (num_contents: %u)", tmd->num_contents);
        }
        else
            debugPrintf("Invalid title.tmd file (num_contents: %u, info count: %u)", tmd->num_contents, tmd->content_infos[0].count);
    }
    else
        debugPrintf("Wrong title.tmd filesize: 0x%X", size);

    return TMD_STATE_BAD;
}

/*
 * Teconmoons Injetor bundles a exe version of NUSPacker.
 * It looks like converting the jar to an exe file slightly
 * corrupted the binary, so the title.tmd files created are
 * slightly off. We fix them here.
 */
static bool fixTMD(const char *path, TMD *tmd, size_t size)
{
    // Fix content hash
    uint32_t hash[8];
    uint8_t *ptr = ((uint8_t *)tmd) + sizeof(TMD);
    mbedtls_sha256(ptr, sizeof(TMD_CONTENT) * tmd->num_contents, (unsigned char *)hash, 0);
    for(int i = 0; i < 8; ++i)
        tmd->content_infos[0].hash[i] = hash[i];

    // Fix tmd hash
    ptr -= sizeof(TMD_CONTENT_INFO) * 64;
    mbedtls_sha256(ptr, sizeof(TMD_CONTENT_INFO) * 64, (unsigned char *)hash, 0);
    for(int i = 0; i < 8; ++i)
        tmd->hash[i] = hash[i];

    // Verify the new tmd
    if(verifyTmd(tmd, size) == TMD_STATE_GOOD)
    {
        // Write fixed file to disc
        FSAFileHandle file = openFile(path, "w", 0);
        if(file != 0)
        {
            // Write fixed file to disc
            addToIOQueue(tmd, 1, size, file);
            addToIOQueue(NULL, 0, 0, file);

            return true;
        }
    }

    return false;
}

TMD *getTmd(const char *dir, bool allowNoIntro)
{
    size_t ss = strlen(dir);
    char *path = MEMAllocFromDefaultHeap(ss + (strlen("/title.tmd") + 1));
    TMD *tmd = NULL;
    if(path != NULL)
    {
        OSBlockMove(path, dir, ss, false);
        OSBlockMove(path + ss, "/title.tmd", strlen("/title.tmd") + 1, false);

        size_t s = readFile(path, (void **)&tmd);
        if(tmd == NULL && allowNoIntro)
        {
            OSBlockMove(path + ss, "/tmd", strlen("/tmd") + 1, false);
            s = readFile(path, (void **)&tmd);
        }

        if(tmd != NULL)
        {
            switch(verifyTmd(tmd, s))
            {
                case TMD_STATE_BAD:
                    MEMFreeToDefaultHeap(tmd);
                    tmd = NULL;
                case TMD_STATE_GOOD:
                    break;
                case TMD_STATE_TECONMOON:
                    debugPrintf("Teconmoon title.tmd file detected, fixing...");
                    if(!fixTMD(path, tmd, s))
                    {
                        MEMFreeToDefaultHeap(tmd);
                        tmd = NULL;
                    }
                    break;
            }
        }

        MEMFreeToDefaultHeap(path);
    }

    return tmd;
}
