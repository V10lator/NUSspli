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

#include <wut-fixups.h>

#include <stdbool.h>
#include <stdio.h>

#include <coreinit/core.h>
#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <crypto.h>
#include <file.h>
#include <ioQueue.h>
#include <renderer.h>
#include <thread.h>
#include <utils.h>

#define IOT_STACK_SIZE       0x1000
#define IO_MAX_FILE_BUFFER   (1024 * 1024) // 1 MB
#define MAX_IO_QUEUE_ENTRIES (64 * (IO_MAX_FILE_BUFFER / (1024 * 1024))) // 64 MB

typedef struct WUT_PACKED
{
    volatile FSFileHandle *file;
    volatile size_t size;
    volatile uint8_t *buf;
} WriteQueueEntry;

static OSThread *ioThread;
static volatile bool ioRunning = false;

static WriteQueueEntry *queueEntries;
static volatile uint32_t activeReadBuffer;
static volatile uint32_t activeWriteBuffer;

static volatile FSStatus fwriteErrno = FS_STATUS_OK;
static volatile int fwriteOverlay = -1;

static int ioThreadMain(int argc, const char **argv)
{
    FSCmdBlock cmdBlk;
    FSInitCmdBlock(&cmdBlk);
    FSSetCmdPriority(&cmdBlk, 1);

    FSStatus err;
    uint32_t asl = activeWriteBuffer;
    WriteQueueEntry *entry = queueEntries + asl;

    while(ioRunning)
    {
        if(entry->file == NULL)
        {
            OSSleepTicks(OSMillisecondsToTicks(2));
            continue;
        }

        if(entry->size) // WRITE command
        {
            err = FSWriteFile(__wut_devoptab_fs_client, &cmdBlk, (uint8_t *)entry->buf, entry->size, 1, *entry->file, 0, FS_ERROR_FLAG_ALL);
            if(err != 1)
                goto ioError;

            entry->size = 0;
        }
        else // Close command
        {
            OSTime t = OSGetTime();
            err = FSCloseFile(__wut_devoptab_fs_client, &cmdBlk, *entry->file, FS_ERROR_FLAG_ALL);
            if(err != FS_STATUS_OK)
                goto ioError;

            t = OSGetTime() - t;
            addEntropy(&t, sizeof(OSTime));
        }

        if(++asl == MAX_IO_QUEUE_ENTRIES)
            asl = 0;

        activeWriteBuffer = asl;
        entry->file = NULL;
        entry = queueEntries + asl;
    }

    return 0;

ioError:
    fwriteErrno = err;
    return 1;
}

bool initIOThread()
{
    queueEntries = MEMAllocFromDefaultHeap(MAX_IO_QUEUE_ENTRIES * sizeof(WriteQueueEntry));
    if(queueEntries != NULL)
    {
        uint8_t *buf = MEMAllocFromDefaultHeapEx(MAX_IO_QUEUE_ENTRIES * IO_MAX_FILE_BUFFER, 0x40);
        if(buf != NULL)
        {
            for(int i = 0; i < MAX_IO_QUEUE_ENTRIES; ++i, buf += IO_MAX_FILE_BUFFER)
            {
                queueEntries[i].file = NULL;
                queueEntries[i].size = 0;
                queueEntries[i].buf = buf;
            }

            activeReadBuffer = activeWriteBuffer = 0;
            ioRunning = true;

            ioThread = startThread("NUSspli I/O", THREAD_PRIORITY_HIGH, IOT_STACK_SIZE, ioThreadMain, 0, NULL, OS_THREAD_ATTRIB_AFFINITY_CPU0); // We move this to core 0 for maximum performance. Later on move it back to core 1 as we want download threads on core 0 and 2.
            if(ioThread != NULL)
                return true;

            ioRunning = false;
            MEMFreeToDefaultHeap(buf);
        }

        MEMFreeToDefaultHeap(queueEntries);
    }

    return false;
}

bool checkForQueueErrors()
{
    if(fwriteErrno != FS_STATUS_OK)
    {
        if(fwriteOverlay == -1 && OSIsMainCore())
        {
            char errMsg[1024];
            sprintf(errMsg, "Write error:\n%s\n\nThis is an unrecoverable error!", translateFSErr(fwriteErrno));
            fwriteOverlay = addErrorOverlay(errMsg);
        }
        return true;
    }
    return false;
}

void shutdownIOThread()
{
    if(!ioRunning)
        return;

    flushIOQueue();

    ioRunning = false;
#ifdef NUSSPLI_DEBUG
    int ret;
    stopThread(ioThread, &ret);
    debugPrintf("I/O thread returned: %d", ret);
#else
    stopThread(ioThread, NULL);
#endif
    MEMFreeToDefaultHeap((void *)queueEntries[0].buf);
    MEMFreeToDefaultHeap(queueEntries);
}

#ifdef NUSSPLI_DEBUG
bool queueStalled = false;
#endif
size_t addToIOQueue(const void *buf, size_t size, size_t n, FSFileHandle *file)
{
    if(checkForQueueErrors())
        return 0;

    WriteQueueEntry *entry;

retryAddingToQueue:
    entry = queueEntries + activeReadBuffer;
    if(entry->file != NULL)
    {
#ifdef NUSSPLI_DEBUG
        if(!queueStalled)
        {
            debugPrintf("Waiting for free slot...");
            queueStalled = true;
        }
#endif
        if(checkForQueueErrors())
            return 0;

        goto retryAddingToQueue; // We use goto here instead of just calling addToIOQueue again to not overgrow the stack.
    }

#ifdef NUSSPLI_DEBUG
    if(queueStalled)
    {
        debugPrintf("Slot free!");
        queueStalled = false;
    }
#endif

    if(buf != NULL)
    {
        size *= n;
        if(size == 0)
            return 0;

        size_t ns = entry->size + size;
        if(ns > IO_MAX_FILE_BUFFER)
        {
            ns = IO_MAX_FILE_BUFFER - entry->size;
            OSBlockMove((void *)(entry->buf + entry->size), buf, ns, false);
            entry->size = IO_MAX_FILE_BUFFER;

            // TODO: Deduplicate code
            entry->file = file;
            if(++activeReadBuffer == MAX_IO_QUEUE_ENTRIES)
                activeReadBuffer = 0;

            size -= ns;
            const uint8_t *newPtr = buf;
            newPtr += ns;
            addToIOQueue((const void *)newPtr, 1, size, file);
            return n;
        }

        OSBlockMove((void *)(entry->buf + entry->size), buf, size, false);
        entry->size = ns;
        if(ns != IO_MAX_FILE_BUFFER) // ns < IO_MAX_FILE_BUFFER
            return n;
    }
    else if(entry->size != 0)
    {
        // TODO: Deduplicate code
        entry->file = file;
        if(++activeReadBuffer == MAX_IO_QUEUE_ENTRIES)
            activeReadBuffer = 0;

        entry = queueEntries + activeReadBuffer;
    }

    entry->file = file;
    if(++activeReadBuffer == MAX_IO_QUEUE_ENTRIES)
        activeReadBuffer = 0;

    return n;
}

void flushIOQueue()
{
    if(checkForQueueErrors() || queueEntries[activeWriteBuffer].file == NULL)
        return;

    int ovl = addErrorOverlay("Flushing queue, please wait...");
    debugPrintf("Flushing...");

    while(queueEntries[activeWriteBuffer].file != NULL)
        if(checkForQueueErrors())
            break;

    removeErrorOverlay(ovl);
    checkForQueueErrors();
}

FSFileHandle *openFile(const char *path, const char *mode, size_t filesize)
{
    if(checkForQueueErrors())
        return NULL;

    OSTime t = OSGetTime();
    FSFileHandle *ret = MEMAllocFromDefaultHeap(sizeof(FSFileHandle));
    if(ret == NULL)
        return NULL;

    char *newPath = getStaticPathBuffer(0);
    strcpy(newPath, path);

    if(filesize != 0 && strncmp(NUSDIR_SD, newPath, strlen(NUSDIR_SD)) == 0)
        filesize = 0;

    FSStatus s = FSOpenFileEx(__wut_devoptab_fs_client, getCmdBlk(), newPath, mode, 0x660, filesize == 0 ? FS_OPEN_FLAG_NONE : FS_OPEN_FLAG_PREALLOC_SIZE, filesize, ret, FS_ERROR_FLAG_ALL);
    if(s == FS_STATUS_OK)
    {
        t = OSGetTime() - t;
        addEntropy(&t, sizeof(OSTime));
        return ret;
    }

    MEMFreeToDefaultHeap(ret);
    debugPrintf("Error opening %s: %s!", path, translateFSErr(s));
    return NULL;
}
