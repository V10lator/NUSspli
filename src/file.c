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

#include <file.h>
#include <ioQueue.h>
#include <staticMem.h>
#include <tmd.h>
#include <utils.h>

#include <crypto.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>

static FSCmdBlock cmdBlk;

FSCmdBlock *getCmdBlk()
{
    return &cmdBlk;
}

void writeVoidBytes(FSFileHandle *fp, uint32_t len)
{
    uint8_t bytes[len];
    OSBlockSet(bytes, 0, len);
    addToIOQueue(bytes, 1, len, fp);
}

void writeCustomBytes(FSFileHandle *fp, const char *str)
{
    if(str[0] == '0' && str[1] == 'x')
        str += 2;

    size_t size = strlen(str) >> 1;
    uint8_t bytes[size];
    hexToByte(str, bytes);
    addToIOQueue(bytes, 1, size, fp);
}

void writeRandomBytes(FSFileHandle *fp, uint32_t len)
{
    uint8_t bytes[len];
    osslBytes(bytes, len);
    addToIOQueue(bytes, 1, len, fp);
}

/*
 * The header is meaned to be human (hex editor) and machine readable. The format is:
 *  - Magic 32 bit value (defined by Nintendo)
 *  - 0x00010203040506070809 As a magic value / metadata detection
 *  - 2 bit spacing
 *  - 64 bit "NUSspli\0" string
 *  - 64 bit padding
 *  - 64 bit version string ending with 0x00
 *  - 64 bit padding
 *  - Max 64 bit file type string ending with 0x00

 *  - Padding till 0x000000EE / area reserved for future use
 *  - uint8_t storing the meta version number (currently 1, so 00000001 or 0x01)
 *  - 128 + 32 random bits marking the end of the header usable area
 *  - 256 + 128 + 64 + 32 bit padding (defined by Nintendo / end of header)
 */
void writeHeader(FSFileHandle *fp, FileType type)
{
    writeCustomBytes(fp, "0x00010004000102030405060708090000"); // Magic 32 bit value + our magic value + padding
    writeCustomBytes(fp, "0x4E555373706C69"); // "NUSspli"
    writeVoidBytes(fp, 0x9);
    int vl = strlen(NUSSPLI_VERSION);
    addToIOQueue(NUSSPLI_VERSION, 1, vl, fp);

    writeVoidBytes(fp, 0x10 - vl);
    char *cb;
    int v;
    switch(type)
    {
    case FILE_TYPE_TIK:
        cb = "0x5469636B6574"; // Ticket
        v = 0xB9;
        break;
    case FILE_TYPE_CERT:
        cb = "0x4365727469666963617465"; // "Certificate"
        v = 0xB4;
        break;
    default:
        cb = "00";
        v = 0xBE;
    }

    writeCustomBytes(fp, cb);
    writeVoidBytes(fp, v);
    writeCustomBytes(fp, "0x01"); // TODO: Don't hardcode in here
    writeRandomBytes(fp, 0x14);
    writeVoidBytes(fp, 0x3C);
}

bool fileExists(const char *path)
{
    FSStat stat;
    return FSGetStat(__wut_devoptab_fs_client, &cmdBlk, path, &stat, FS_ERROR_FLAG_ALL) == FS_STATUS_OK;
}

bool dirExists(const char *path)
{
    FSStat stat;
    return FSGetStat(__wut_devoptab_fs_client, &cmdBlk, path, &stat, FS_ERROR_FLAG_ALL) == FS_STATUS_OK && (stat.flags & FS_STAT_DIRECTORY);
}

void removeDirectory(const char *path)
{
    size_t len = strlen(path);
    char *newPath = getStaticPathBuffer(0);
    strcpy(newPath, path);

    if(newPath[len - 1] != '/')
    {
        newPath[len] = '/';
        newPath[++len] = '\0';
    }

    char *inSentence = newPath + len;
    FSDirectoryHandle dir;
    OSTime t = OSGetTime();
    if(FSOpenDir(__wut_devoptab_fs_client, &cmdBlk, newPath, &dir, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
    {
        FSDirectoryEntry entry;
        while(FSReadDir(__wut_devoptab_fs_client, &cmdBlk, dir, &entry, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
        {
            strcpy(inSentence, entry.name);
            if(entry.info.flags & FS_STAT_DIRECTORY)
                removeDirectory(newPath);
            else
                FSRemove(__wut_devoptab_fs_client, &cmdBlk, newPath, FS_ERROR_FLAG_ALL);
        }
        FSCloseDir(__wut_devoptab_fs_client, &cmdBlk, dir, FS_ERROR_FLAG_ALL);
        newPath[len - 1] = '\0';
        FSRemove(__wut_devoptab_fs_client, &cmdBlk, newPath, FS_ERROR_FLAG_ALL);
    }
    else
        debugPrintf("Path \"%s\" not found!", newPath);

    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));
}

FSStatus moveDirectory(const char *src, const char *dest)
{
    size_t len = strlen(src) + 1;
    char *newSrc = getStaticPathBuffer(0);
    if(newSrc != src)
        OSBlockMove(newSrc, src, len, false);

    char *inSrc = newSrc + --len;
    if(*--inSrc != '/')
        *++inSrc = '/';

    ++inSrc;

    OSTime t = OSGetTime();
    FSDirectoryHandle dir;
    FSStatus ret = FSOpenDir(__wut_devoptab_fs_client, &cmdBlk, newSrc, &dir, FS_ERROR_FLAG_ALL);

    if(ret == FS_STATUS_OK)
    {
        len = strlen(dest) + 1;
        char *newDest = getStaticPathBuffer(1);
        if(newDest != dest)
            OSBlockMove(newDest, dest, len, false);

        ret = createDirectory(newDest);
        if(ret == FS_STATUS_OK)
        {
            char *inDest = newDest + --len;
            if(*--inDest != '/')
                *++inDest = '/';

            ++inDest;

            FSDirectoryEntry entry;
            while(ret == FS_STATUS_OK && FSReadDir(__wut_devoptab_fs_client, &cmdBlk, dir, &entry, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
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
                    ret = FSRename(__wut_devoptab_fs_client, &cmdBlk, newSrc, newDest, FS_ERROR_FLAG_ALL);
                }
            }
        }

        FSCloseDir(__wut_devoptab_fs_client, &cmdBlk, dir, FS_ERROR_FLAG_ALL);
        *--inSrc = '\0';
        FSRemove(__wut_devoptab_fs_client, &cmdBlk, newSrc, FS_ERROR_FLAG_ALL);

        t = OSGetTime() - t;
        addEntropy(&t, sizeof(OSTime));
    }

    return ret;
}

// There are no files > 4 GB on the Wii U, so size_t should be more than enough.
size_t getFilesize(const char *path)
{
    char *newPath = getStaticPathBuffer(0);
    strcpy(newPath, path);

    FSStat stat;
    OSTime t = OSGetTime();

    if(FSGetStat(__wut_devoptab_fs_client, &cmdBlk, newPath, &stat, FS_ERROR_FLAG_ALL) != FS_STATUS_OK)
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
        FSFileHandle handle;
        path = getStaticPathBuffer(0); // getFilesize() setted it for us
        if(FSOpenFile(__wut_devoptab_fs_client, &cmdBlk, path, "r", &handle, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
        {
            *buffer = MEMAllocFromDefaultHeapEx(FS_ALIGN(filesize), 0x40);
            if(*buffer != NULL)
            {
                if(FSReadFile(__wut_devoptab_fs_client, &cmdBlk, *buffer, filesize, 1, handle, 0, FS_ERROR_FLAG_ALL) == 1)
                {
                    FSCloseFile(__wut_devoptab_fs_client, &cmdBlk, handle, FS_ERROR_FLAG_ALL);
                    return filesize;
                }

                debugPrintf("Error reading %s!", path);
                MEMFreeToDefaultHeap(*buffer);
            }
            else
                debugPrintf("Error creating buffer!");

            FSCloseFile(__wut_devoptab_fs_client, &cmdBlk, handle, FS_ERROR_FLAG_ALL);
        }
        else
            debugPrintf("Error opening %s!", path);
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
                    if(getSHA256(ptr, sizeof(TMD_CONTENT_INFO) * 64, hash))
                    {
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
                        if(getSHA256(ptr, sizeof(TMD_CONTENT) * tmd->num_contents, hash))
                        {
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
                            debugPrintf("Error calculating content hash for title.tmd file!");
                    }
                    else
                        debugPrintf("Error calculating tmd hash for title.tmd file!");
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

FSStatus createDirectory(const char *path)
{
    OSTime t = OSGetTime();
    FSStatus stat = FSMakeDir(__wut_devoptab_fs_client, &cmdBlk, path, FS_ERROR_FLAG_ALL);
    if(stat == FS_STATUS_OK)
    {
        t = OSGetTime() - t;
        addEntropy(&t, sizeof(OSTime));
    }

    return stat;
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
            return dirExists(d) ? true : createDirectory(d) == FS_STATUS_OK;

        *needle = '\0';
        if(!dirExists(d) && createDirectory(d) != FS_STATUS_OK)
            return false;

        *needle = '/';
        ++needle;
    } while(*needle != '\0');

    return true;
}

const char *translateFSErr(FSStatus err)
{
    switch(err)
    {
    case FS_STATUS_PERMISSION_ERROR:
        return "Permission error (read only filesystem?)";
    case FS_STATUS_MEDIA_ERROR:
    case FS_STATUS_CORRUPTED:
    case FS_STATUS_ACCESS_ERROR:
        return "Filesystem error";
    case FS_STATUS_NOT_FOUND:
        return "Not found";
    case FS_STATUS_NOT_FILE:
        return "Not a file";
    case FS_STATUS_NOT_DIR:
        return "Not a folder";
    case FS_STATUS_FILE_TOO_BIG:
    case FS_STATUS_STORAGE_FULL:
        return "Not enough free space";
    default:
        break;
    }

    static char ret[32];
    sprintf(ret, "Unknown error: %d", err);
    return ret;
}
