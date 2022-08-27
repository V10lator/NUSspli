/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2021-2022 V10lator <v10lator@myway.de>                    *
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

#ifndef NUSSPLI_HBL

#include <wut-fixups.h>

#include <stdbool.h>
#include <stdint.h>

#include <crypto.h>
#include <file.h>
#include <state.h>
#include <staticMem.h>
#include <utils.h>

#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/time.h>
#include <coreinit/title.h>

#define MD5_FILES 6

static const uint8_t md5[MD5_FILES][16] = {
    { 0x88, 0xbd, 0xe1, 0xac, 0xdd, 0xf1, 0x9e, 0x08, 0xfb, 0x1a, 0xe9, 0xf6, 0x6f, 0x97, 0x03, 0xe7 },
    { 0x33, 0xe1, 0xd6, 0x7f, 0x62, 0xc9, 0xf4, 0x48, 0xc5, 0xc0, 0x95, 0xe5, 0x95, 0xaa, 0x6f, 0xc6 },
    { 0xa7, 0xac, 0xd1, 0x88, 0x3e, 0x81, 0x6f, 0xab, 0x90, 0xff, 0x7e, 0x21, 0xa2, 0xae, 0xbb, 0xd2 },
    { 0x70, 0x56, 0xac, 0x62, 0x63, 0x45, 0x6a, 0x2e, 0x1d, 0x2b, 0x99, 0x2a, 0xee, 0x82, 0x22, 0x71 },
    { 0xc8, 0x85, 0xa1, 0x65, 0x3d, 0x16, 0x71, 0xb0, 0xaa, 0xd3, 0xf4, 0xf6, 0xeb, 0xdd, 0xab, 0x38 },
    { 0x0b, 0x77, 0xc7, 0x96, 0x6d, 0x1a, 0xbb, 0xc3, 0xc9, 0x15, 0x1a, 0xe4, 0xfa, 0x4a, 0x6d, 0x53 },
};

static const char *md5File[MD5_FILES] = {
    "iconTex.tga",
    "bootDrcTex.tga",
    "BootTvTex.tga",
    "bootLogoTex.tga",
    "bootMovie.h264",
    "bootSound.btsnd",
};

bool sanityCheck()
{
    if(isChannel())
    {
        MCPTitleListType title;
        OSTime t = OSGetTime();
        if(MCP_GetTitleInfo(mcpHandle, OSGetTitleID(), &title))
        {
            debugPrintf("Sanity error: Can't get MCPTitleListType");
            return false;
        }

        if(strlen(title.path) != 46)
        {
            debugPrintf("Sanity error: Incorrect length of path");
            return false;
        }

        char *newPath = getStaticPathBuffer(0);
        size_t s;
        void *buf;
        uint8_t m[16];

        strcpy(newPath, title.path);
        strcat(newPath, "/meta/");
        char *pr = newPath + strlen(newPath);

        for(int i = 0; i < MD5_FILES; ++i)
        {
            strcpy(pr, md5File[i]);

            s = readFile(newPath, &buf);
            if(buf == NULL)
                return false;

            getMD5(buf, s, m);
            for(int j = 0; j < 16; ++j)
            {
                if(m[j] != md5[i][j])
                {
                    debugPrintf("Sanity error: Data integrity error");
                    MEMFreeToDefaultHeap(buf);
                    return false;
                }
            }

            MEMFreeToDefaultHeap(buf);
        }

        t = OSGetTime() - t;
        addEntropy(&t, sizeof(OSTime));
        return true;
    }

    // else / isAroma()
    // TODO
    return true;
}

#endif
