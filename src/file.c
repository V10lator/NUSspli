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
#include <staticMem.h>
#include <tmd.h>
#include <utils.h>

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include <coreinit/filesystem.h>
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
            if(entry.name[0] == '.')
                continue;

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
                if(entry.name[0] == '.')
                    continue;

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

size_t readFileNew(const char *path, void **buffer)
{
    size_t filesize = getFilesize(path);
    if(filesize != -1)
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

                debugPrintf("Error reading %s: %s!", path, translateFSErr(err));
                MEMFreeToDefaultHeap(*buffer);
            }
            else
                debugPrintf("Error creating buffer!");

            FSACloseFile(getFSAClient(), handle);
        }
        else
            debugPrintf("Error opening %s: %s!", path, translateFSErr(err));
    }
    else
        debugPrintf("Error getting filesize for %s!", path);

    *buffer = NULL;
    return 0;
}

#ifdef NUSSPLI_HBL
static size_t getFilesizeOld(FILE *fp)
{
    struct stat info;
    OSTime t = OSGetTime();

    if(fstat(fileno(fp), &info) == -1)
        return -1;

    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));

    return (size_t)(info.st_size);
}

size_t readFile(const char *path, void **buffer)
{
    if(strncmp("romfs:/", path, strlen("romfs:/")) != 0)
        return readFileNew(path, buffer);

    FILE *file = fopen(path, "rb");
    if(file != NULL)
    {
        size_t filesize = getFilesizeOld(file);
        if(filesize != -1)
        {
            *buffer = MEMAllocFromDefaultHeapEx(FS_ALIGN(filesize), 0x40);
            if(*buffer != NULL)
            {
                if(fread(*buffer, filesize, 1, file) == 1)
                {
                    fclose(file);
                    return filesize;
                }

                debugPrintf("Error reading %s!", path);
                MEMFreeToDefaultHeap(*buffer);
            }
            else
                debugPrintf("Error creating buffer!");
        }
        else
            debugPrintf("Error getting filesize for %s!", path);

        fclose(file);
    }
    else
        debugPrintf("Error opening %s!", path);

    *buffer = NULL;
    return 0;
}
#endif

// This uses informations from https://github.com/Maschell/nuspacker
bool verifyTmd(const TMD *tmd, size_t size)
{
    if(size >= sizeof(TMD) + (sizeof(TMD_CONTENT) * 9)) // Minimal title.tmd size
    {
        if(tmd->num_contents == tmd->content_infos[0].count) // Validate num_contents
        {
            if(tmd->num_contents > 8) // Check for at least 9 contents (.app files)
            {
                if(size == (sizeof(TMD) + 0x700) + (sizeof(TMD_CONTENT) * tmd->num_contents) || // Most title.tmd files have a certificate attached to the end. This certificate is 0x700 bytes long.
                    size == sizeof(TMD) + (sizeof(TMD_CONTENT) * tmd->num_contents)) // Some (like ones made with NUSPacker) don't have a certificate attached through.
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
                            return false;
                        }
                    }

                    // Validate content hash
                    ptr += sizeof(TMD_CONTENT_INFO) * 64;
                    mbedtls_sha256(ptr, sizeof(TMD_CONTENT) * tmd->num_contents, (unsigned char *)hash, 0);
                    for(int i = 0; i < 8; ++i)
                    {
                        if(hash[i] != tmd->content_infos[0].hash[i])
                        {
                            debugPrintf("Invalid title.tmd file (content hash mismatch)");
                            return false;
                        }
                    }

                    // Validate content
                    for(int i = 0; i < tmd->num_contents; ++i)
                    {
                        // Validate content index
                        if(tmd->contents[i].index != i)
                        {
                            debugPrintf("Invalid title.tmd file (content: %d, index: %u)", i, tmd->contents[i].index);
                            return false;
                        }
                        // Validate content type
                        if(!((tmd->contents[i].type & TMD_CONTENT_TYPE_CONTENT) && (tmd->contents[i].type & TMD_CONTENT_TYPE_ENCRYPTED)))
                        {
                            debugPrintf("Invalid title.tmd file (content: %u, type: 0x%04X)", i, tmd->contents[i].type);
                            return false;
                        }
                        // Validate content size
                        if(tmd->contents[i].size < 32 * 1024 || tmd->contents[i].size > (uint64_t)1024 * 1024 * 1024 * 4)
                        {
                            debugPrintf("Invalid title.tmd file (content: %d, size: %llu)", i, tmd->contents[i].size);
                            return false;
                        }
                    }

                    return true;
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

    return false;
}

TMD *getTmd(const char *dir)
{
    size_t s = strlen(dir);
    char *path = MEMAllocFromDefaultHeap(s + (strlen("title.tmd") + 1));
    if(path != NULL)
    {
        OSBlockMove(path, dir, s, false);
        strcpy(path + s, "title.tmd");

        TMD *tmd;
        s = readFile(path, (void **)&tmd);
        MEMFreeToDefaultHeap(path);

        if(tmd != NULL)
        {
            if(verifyTmd(tmd, s))
                return tmd;

            MEMFreeToDefaultHeap(tmd);
        }
    }

    return NULL;
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
