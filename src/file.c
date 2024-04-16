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

#include <wut-fixups.h>

#include <crypto.h>
#include <file.h>
#include <filesystem.h>
#include <ioQueue.h>
#include <menu/utils.h>
#include <renderer.h>
#include <staticMem.h>
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

bool fileExists(const char *path)
{
    FSAStat stat;
    return FSAGetStat(getFSAClient(), path, &stat) == FS_ERROR_OK;
}

bool dirExists(const char *path)
{
    FSAStat stat;
    return FSAGetStat(getFSAClient(), path, &stat) == FS_ERROR_OK && (stat.flags & FS_STAT_DIRECTORY);
}

FSError removeDirectory(const char *path)
{
    size_t len = strlen(path);
    char *newPath = getStaticPathBuffer(0);
    if(newPath != path)
        OSBlockMove(newPath, path, len + 1, false);

    if(newPath[len - 1] != '/')
    {
        newPath[len] = '/';
        newPath[++len] = '\0';
    }

    char *inSentence = newPath + len;
    FSADirectoryHandle dir;
    OSTime t = OSGetTime();
    FSError ret = FSAOpenDir(getFSAClient(), newPath, &dir);
    if(ret == FS_ERROR_OK)
    {
        FSADirectoryEntry entry;
        while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
        {
            strcpy(inSentence, entry.name);
            if(entry.info.flags & FS_STAT_DIRECTORY)
                ret = removeDirectory(newPath);
            else
                ret = FSARemove(getFSAClient(), newPath);

            if(ret != FS_ERROR_OK)
                break;
        }

        FSACloseDir(getFSAClient(), dir);
        if(ret == FS_ERROR_OK)
        {
            newPath[--len] = '\0';
            ret = FSARemove(getFSAClient(), newPath);
        }
    }
    else
        debugPrintf("Path \"%s\" not found!", newPath);

    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));
    return ret;
}

FSError moveDirectory(const char *src, const char *dest)
{
    size_t len = strlen(src) + 1;
    char *newSrc = getStaticPathBuffer(0);
    if(newSrc != src)
        OSBlockMove(newSrc, src, len, false);

    char *inSrc = newSrc + --len;
    if(*--inSrc != '/')
    {
        *++inSrc = '/';
        *++inSrc = '\0';
    }
    else
        ++inSrc;

    OSTime t = OSGetTime();
    FSADirectoryHandle dir;
    FSError ret = FSAOpenDir(getFSAClient(), newSrc, &dir);

    if(ret == FS_ERROR_OK)
    {
        len = strlen(dest) + 1;
        char *newDest = getStaticPathBuffer(1);
        if(newDest != dest)
            OSBlockMove(newDest, dest, len, false);

        ret = createDirectory(newDest);
        if(ret == FS_ERROR_OK)
        {
            char *inDest = newDest + --len;
            if(*--inDest != '/')
                *++inDest = '/';

            ++inDest;

            FSADirectoryEntry entry;
            while(ret == FS_ERROR_OK && FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
            {
                len = strlen(entry.name);
                OSBlockMove(inSrc, entry.name, ++len, false);
                OSBlockMove(inDest, entry.name, len, false);

                if(entry.info.flags & FS_STAT_DIRECTORY)
                {
                    debugPrintf("\tmoveDirectory('%s', '%s')", newSrc, newDest);
                    ret = moveDirectory(newSrc, newDest);
                }
                else
                {
                    debugPrintf("\trename('%s', '%s')", newSrc, newDest);
                    ret = FSARename(getFSAClient(), newSrc, newDest);
                }
            }
        }

        FSACloseDir(getFSAClient(), dir);
        *--inSrc = '\0';
        FSARemove(getFSAClient(), newSrc);

        t = OSGetTime() - t;
        addEntropy(&t, sizeof(OSTime));
    }
    else
        debugPrintf("Error opening %s", newSrc);

    return ret;
}

// There are no files > 4 GB on the Wii U, so size_t should be more than enough.
size_t getFilesize(const char *path)
{
    char *newPath = getStaticPathBuffer(0);
    strcpy(newPath, path);

    FSAStat stat;
    OSTime t = OSGetTime();

    if(FSAGetStat(getFSAClient(), newPath, &stat) != FS_ERROR_OK)
        return -1;

    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));

    return stat.size;
}

size_t readFile(const char *path, void **buffer)
{
    char *toScreen = getToFrameBuffer();
    toScreen[0] = '\0';
    size_t filesize = getFilesize(path);
    if(filesize != (size_t)-1)
    {
        FSAFileHandle handle;
        path = getStaticPathBuffer(0); // getFilesize() setted it for us
        FSError err = FSAOpenFileEx(getFSAClient(), path, "r", 0x000, 0, 0, &handle);
        if(err == FS_ERROR_OK)
        {
            *buffer = MEMAllocFromDefaultHeapEx(FS_ALIGN(filesize), 0x40);
            if(*buffer != NULL)
            {
                err = FSAReadFile(getFSAClient(), *buffer, filesize, 1, handle, 0);
                if(err == 1)
                {
                    FSACloseFile(getFSAClient(), handle);
                    return filesize;
                }

                sprintf(toScreen, "Error reading %s: %s!", path, translateFSErr(err));
                MEMFreeToDefaultHeap(*buffer);
            }
            else
                debugPrintf("Error creating buffer!");

            FSACloseFile(getFSAClient(), handle);
        }
        else
            sprintf(toScreen, "Error opening %s: %s!", path, translateFSErr(err));
    }
    else
        sprintf(toScreen, "Error getting filesize for %s!", path);

    *buffer = NULL;
    if(toScreen[0] != '\0')
        addToScreenLog(toScreen);

    return 0;
}

size_t getDirsize(const char *path)
{
    char *newPath = MEMAllocFromDefaultHeap(FS_MAX_PATH);
    if(newPath == NULL)
        return 0;

    size_t ret = 0;
    size_t start = strlen(path);
    if(start == 0)
    {
        strcpy(newPath, path);
        if(newPath[start - 1] != '/')
        {
            newPath[start++] = '/';
            newPath[start] = '\0';
        }

        OSTime t = OSGetTime();
        FSADirectoryHandle dir;
        FSADirectoryEntry entry;

        if(FSAOpenDir(getFSAClient(), path, &dir) == FS_ERROR_OK)
        {
            while(ret == FS_ERROR_OK && FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
            {
                strcpy(newPath + start, entry.name);
                ret += entry.info.flags & FS_STAT_DIRECTORY ? getDirsize(newPath) : entry.info.size;
            }

            FSACloseDir(getFSAClient(), dir);
        }

        t = OSGetTime() - t;
        addEntropy(&t, sizeof(OSTime));
    }

    MEMFreeToDefaultHeap(newPath);
    return ret;
}

FSError createDirectory(const char *path)
{
    OSTime t = OSGetTime();
    FSError err = FSAMakeDir(getFSAClient(), path, 0x660);
    if(err != FS_ERROR_OK)
    {
        t = OSGetTime() - t;
        addEntropy(&t, sizeof(OSTime));
    }

    return err;
}

bool createDirRecursive(const char *dir)
{
    size_t len = strlen(dir);
    char d[++len];
    OSBlockMove(d, dir, len, false);

    char *needle = d;
    if(strncmp(NUSDIR_SD, d, strlen(NUSDIR_SD)) == 0)
        needle += strlen(NUSDIR_SD);
    else
        needle += strlen(NUSDIR_MLC);

    do
    {
        needle = strchr(needle, '/');
        if(needle == NULL)
            return dirExists(d) ? true : createDirectory(d) == FS_ERROR_OK;

        *needle = '\0';
        if(!dirExists(d) && createDirectory(d) != FS_ERROR_OK)
            return false;

        *needle = '/';
        ++needle;
    } while(*needle != '\0');

    return true;
}

const char *translateFSErr(FSError err)
{
    switch(err)
    {
        case FS_ERROR_PERMISSION_ERROR:
        case FS_ERROR_WRITE_PROTECTED:
            return "Permission error (read only filesystem?)";
        case FS_ERROR_MEDIA_ERROR:
        case FS_ERROR_DATA_CORRUPTED:
        case FS_ERROR_ACCESS_ERROR:
            return "Filesystem error";
        case FS_ERROR_NOT_FOUND:
            return "Not found";
        case FS_ERROR_NOT_FILE:
            return "Not a file";
        case FS_ERROR_NOT_DIR:
            return "Not a folder";
        case FS_ERROR_FILE_TOO_BIG:
        case FS_ERROR_STORAGE_FULL:
            return "Not enough free space";
        case FS_ERROR_ALREADY_OPEN:
            return "File held open by another process";
        case FS_ERROR_ALREADY_EXISTS:
            return "File exists";
        default:
            break;
    }

    static char ret[1024];
    sprintf(ret, "Unknown error: %s (%d)", FSAGetStatusStr(err), err);
    return ret;
}

NUSDEV getDevFromPath(const char *path)
{
    if(strncmp(NUSDIR_SD, path, strlen(NUSDIR_SD)) == 0)
        return NUSDEV_SD;
    if(strncmp(NUSDIR_USB1, path, strlen(NUSDIR_USB1)) == 0)
        return NUSDEV_USB01;
    if(strncmp(NUSDIR_USB2, path, strlen(NUSDIR_USB2)) == 0)
        return NUSDEV_USB02;
    if(strncmp(NUSDIR_MLC, path, strlen(NUSDIR_MLC)) == 0)
        return NUSDEV_MLC;

    return NUSDEV_NONE;
}
