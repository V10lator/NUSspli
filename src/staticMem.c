/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2022 V10lator <v10lator@myway.de>                         *
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

#include <stdint.h>

#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>

#include <renderer.h>
#include <staticMem.h>
#include <titles.h>

static char *staticMemToFrameBuffer;
static char *staticMemLineBuffer;
static char *staticMemPathBuffer[4];

bool initStaticMem()
{
    staticMemToFrameBuffer = MEMAllocFromDefaultHeap(TO_FRAME_BUFFER_SIZE);
    if(staticMemToFrameBuffer != NULL)
    {
        staticMemLineBuffer = MEMAllocFromDefaultHeap(TO_FRAME_BUFFER_SIZE);
        if(staticMemLineBuffer != NULL)
        {
            for(int i = 0; i < 4; ++i)
            {
                staticMemPathBuffer[i] = MEMAllocFromDefaultHeapEx(FS_MAX_PATH, 0x40); // Alignmnt is important for MCP!
                if(staticMemPathBuffer[i] == NULL)
                {
                    for(--i; i >= 0; --i)
                        MEMFreeToDefaultHeap(staticMemPathBuffer[i]);

                    goto staticFailure;
                }
            }

            return true;

        staticFailure:
            MEMFreeToDefaultHeap(staticMemLineBuffer);
        }

        MEMFreeToDefaultHeap(staticMemToFrameBuffer);
    }

    return false;
}

void shutdownStaticMem()
{
    MEMFreeToDefaultHeap(staticMemToFrameBuffer);
    MEMFreeToDefaultHeap(staticMemLineBuffer);
    for(int i = 0; i < 4; ++i)
        MEMFreeToDefaultHeap(staticMemPathBuffer[i]);
}

char *getStaticScreenBuffer()
{
    return staticMemToFrameBuffer;
}

char *getStaticLineBuffer()
{
    return staticMemLineBuffer;
}

/*
 * Path 0 and 1 for file operations.
 * 2 and 3 for anything else.
 */
char *getStaticPathBuffer(uint32_t i)
{
    return staticMemPathBuffer[i];
}
