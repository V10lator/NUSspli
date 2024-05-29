/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2023 V10lator <v10lator@myway.de>                         *
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

#include <stdbool.h>

#include <file.h>
#include <filesystem.h>
#include <ioQueue.h>
#include <no-intro.h>
#include <ticket.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

void destroyNoIntroData(NO_INTRO_DATA *data)
{
    MEMFreeToDefaultHeap(data->path);
    MEMFreeToDefaultHeap(data);
}

void revertNoIntro(NO_INTRO_DATA *data)
{
    char *newPath = MEMAllocFromDefaultHeap(FS_MAX_PATH);
    if(newPath == NULL)
    {
        debugPrintf("EOM!");
        return;
    }

    size_t s = strlen(data->path);
    OSBlockMove(newPath, data->path, s + 1, false);
    char *dataP = data->path + s;
    char *toP = newPath + s;

    OSBlockMove(dataP, "title.tik", sizeof("title.tik"), false);
    FSError ret;

    flushIOQueue();
    if(!data->hadTicket)
    {
        ret = FSARemove(getFSAClient(), data->path);
        if(ret != FS_ERROR_OK)
            debugPrintf("Can't remove %s: %s", data->path, translateFSErr(ret));
    }
    else
    {
        OSBlockMove(toP, "cetk", sizeof("cetk"), false);
        ret = FSARename(getFSAClient(), data->path, newPath);
        if(ret != FS_ERROR_OK)
            debugPrintf("Can't move %s to %s: %s", data->path, newPath, translateFSErr(ret));
    }

    OSBlockMove(dataP, "title.cert", sizeof("title.cert"), false);
    ret = FSARemove(getFSAClient(), data->path);
    if(ret != FS_ERROR_OK)
        debugPrintf("Can't remove %s: %s", data->path, translateFSErr(ret));

    OSBlockMove(dataP, "title.tmd", sizeof("title.tmd"), false);
    OSBlockMove(toP, "tmd", sizeof("tmd"), false);
    ret = FSARename(getFSAClient(), data->path, newPath);
    if(ret != FS_ERROR_OK)
        debugPrintf("Can't move %s to %s: %s", data->path, newPath, translateFSErr(ret));

    // TODO: Rename .app files
    *dataP = '\0';
    FSADirectoryHandle dir;
    ret = FSAOpenDir(getFSAClient(), data->path, &dir);
    if(ret == FS_ERROR_OK)
    {
        FSADirectoryEntry entry;
        toP[8] = '\0';
        while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
        {
            if((entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 12 || strcmp(entry.name + 8, ".app") != 0)
                continue;

            OSBlockMove(dataP, entry.name, 13, false);
            OSBlockMove(toP, entry.name, 8, false);
            if(FSARename(getFSAClient(), data->path, newPath) != FS_ERROR_OK)
                debugPrintf("Can't move %s to %s: %s", data->path, newPath, translateFSErr(ret));
        }

        FSACloseDir(getFSAClient(), dir);
    }
    else
        debugPrintf("Can't open %s: %s", data->path, translateFSErr(ret));

    destroyNoIntroData(data);
    MEMFreeToDefaultHeap(newPath);
}

NO_INTRO_DATA *transformNoIntro(const char *path)
{
    NO_INTRO_DATA *data = MEMAllocFromDefaultHeap(sizeof(NO_INTRO_DATA));
    if(data == NULL)
    {
        debugPrintf("EOM!");
        return NULL;
    }

    data->path = MEMAllocFromDefaultHeap(FS_MAX_PATH);
    if(data->path == NULL)
    {
        MEMFreeToDefaultHeap(data);
        debugPrintf("EOM!");
        return NULL;
    }

    char *pathTo = MEMAllocFromDefaultHeap(FS_MAX_PATH);
    if(pathTo == NULL)
    {
        destroyNoIntroData(data);
        debugPrintf("EOM!");
        return NULL;
    }

    size_t s = strlen(path);
    OSBlockMove(data->path, path, s, false);
    if(data->path[s - 1] != '/')
        data->path[s++] = '/';

    data->path[s] = '\0';

    OSBlockMove(pathTo, data->path, ++s, false);

    FSADirectoryHandle dir;
    FSError ret = FSAOpenDir(getFSAClient(), data->path, &dir);
    if(ret != FS_ERROR_OK)
    {
        debugPrintf("Can't open %s: %s", data->path, translateFSErr(ret));
        destroyNoIntroData(data);
        MEMFreeToDefaultHeap(pathTo);
        return NULL;
    }

    FSADirectoryEntry entry;
    char *fromP = data->path + --s;
    char *toP = pathTo + s;

    data->hadTicket = false;
    data->tmdFound = false;
    data->ac = 0;

    while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
    {
        if(entry.info.flags & FS_STAT_DIRECTORY)
            continue;

        strcpy(fromP, entry.name);
        if(strcmp(entry.name, "tmd") == 0)
        {
            OSBlockMove(fromP, "tmd", sizeof("tmd"), false);
            OSBlockMove(toP, "title.tmd", sizeof("title.tmd"), false);
            ret = FSARename(getFSAClient(), data->path, pathTo);
            if(ret != FS_ERROR_OK)
            {
                debugPrintf("Can't move %s to %s: %s", data->path, pathTo, translateFSErr(ret));
                goto transformError1;
            }

            data->tmdFound = true;
        }
        else if(strcmp(entry.name, "cetk") == 0)
        {
            OSBlockMove(fromP, "cetk", sizeof("cetk"), false);
            OSBlockMove(toP, "title.tik", sizeof("title.tik"), false);
            ret = FSARename(getFSAClient(), data->path, pathTo);
            if(ret != FS_ERROR_OK)
            {
                debugPrintf("Can't move %s to %s: %s", data->path, pathTo, translateFSErr(ret));
                goto transformError1;
            }

            data->hadTicket = true;
        }
        else if(strlen(entry.name) == 8)
        {
            OSBlockMove(fromP, entry.name, 9, false);
            OSBlockMove(toP, entry.name, 8, false);
            OSBlockMove(toP + 8, ".app", sizeof(".app"), false);
            ret = FSARename(getFSAClient(), data->path, pathTo);
            if(ret != FS_ERROR_OK)
            {
                debugPrintf("Can't move %s to %s: %s", data->path, pathTo, translateFSErr(ret));
                goto transformError1;
            }

            data->ac++;
        }
    }

    FSACloseDir(getFSAClient(), dir);
    MEMFreeToDefaultHeap(pathTo);
    fromP = '\0';

    if(!data->tmdFound || !data->ac)
    {
        revertNoIntro(data);
        return NULL;
    }

#ifndef NUSSPLI_LITE
    TMD *tmd = getTmd(data->path, false);
    if(tmd == NULL)
        goto transformError2;

    OSBlockMove(fromP, "title.tik", sizeof("title.tik"), false);
    if(!data->hadTicket)
    {
        debugPrintf("Creating ticket at at %s", data->path);
        if(!generateTik(data->path, getTitleEntryByTid(tmd->tid), tmd))
        {
            debugPrintf("Error creating ticket at %s", data->path);
            goto transformError2;
        }
    }

    OSBlockMove(fromP, "title.cert", sizeof("title.cert"), false);
    debugPrintf("Creating cert at %s", data->path);
    if(!generateCert(tmd, NULL, 0, data->path))
    {
        debugPrintf("Error creating cert at %s", data->path);
        goto transformError2;
    }
#endif

    *fromP = '\0';
    return data;

transformError1:
    MEMFreeToDefaultHeap(pathTo);
    FSACloseDir(getFSAClient(), dir);
transformError2:
    *fromP = '\0';
    revertNoIntro(data);
    return NULL;
}
