/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.             *
 ***************************************************************************/

#include <wut-fixups.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include <coreinit/core.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <renderer.h>
#include <crypto.h>
#include <file.h>
#include <ioQueue.h>
#include <thread.h>
#include <utils.h>

#define IOT_STACK_SIZE		0x2000
#define MAX_IO_QUEUE_ENTRIES	((512 * 1024 * 1024) / IO_BUFSIZE) // 512 MB
#define IO_MAX_FILE_BUFFER	(1024 * 1024) // 1 MB

typedef struct WUT_PACKED
{
	NUSFILE *file;
	size_t size;
	uint8_t buf[IO_BUFSIZE];
} WriteQueueEntry;

static OSThread *ioThread;
static volatile bool ioRunning = false;
static spinlock ioWriteLock;

static volatile WriteQueueEntry *queueEntries;
static volatile uint32_t activeReadBuffer;
static volatile uint32_t activeWriteBuffer;

static volatile int fwriteErrno = 0;
static volatile int fwriteOverlay = -1;

static int ioThreadMain(int argc, const char **argv)
{
	spinReleaseLock(ioWriteLock);
	debugPrintf("I/O queue running!");

	uint32_t asl;
	volatile WriteQueueEntry *entry;
	while(ioRunning && !fwriteErrno)
	{
		asl = activeWriteBuffer;
		entry = queueEntries + asl;
		if(entry->file == NULL)
		{
			OSSleepTicks(256);
			continue;
		}

		if(entry->size != 0) // WRITE command
		{
			if(fwrite((void *)entry->buf, entry->size, 1, entry->file->fd) != 1)
			{
				fwriteErrno = errno;
				debugPrintf("fwrite() error: %d / 0x%08X / %u", fwriteErrno, entry->file->fd, entry->size);
			}
		}
		else // Close command
		{
			debugPrintf("File close: 0x%08X", entry->file->fd);
			OSTime t = OSGetTime();
			if(fclose(entry->file->fd))
			{
				fwriteErrno = errno;
				debugPrintf("fclose() error: %d / 0x%08X", fwriteErrno, entry->file->fd);
			}
			MEMFreeToDefaultHeap(entry->file->buffer);
			MEMFreeToDefaultHeap((void *)entry->file);
			t = OSGetTime() - t;
			addEntropy(&t, sizeof(OSTime));
		}

		if(++asl == MAX_IO_QUEUE_ENTRIES)
			asl = 0;

		activeWriteBuffer = asl;
		entry->file = NULL;
	}
	
	return 0;
}

bool initIOThread()
{
	queueEntries = MEMAllocFromDefaultHeap(MAX_IO_QUEUE_ENTRIES * sizeof(WriteQueueEntry));
	if(queueEntries != NULL)
	{
		for(int i = 0; i < MAX_IO_QUEUE_ENTRIES; ++i)
			queueEntries[i].file = NULL;

		spinCreateLock(ioWriteLock, true);
		activeReadBuffer = activeWriteBuffer = 0;
		ioRunning = true;

		ioThread = startThread("NUSspli I/O", THREAD_PRIORITY_HIGH, IOT_STACK_SIZE, ioThreadMain, 0, NULL, OS_THREAD_ATTRIB_AFFINITY_CPU0); // We move this to core 0 for maximum performance. Later on move it back to core 1 as we want download threads on core 0 and 2.
		if(ioThread != NULL)
			return true;

		MEMFreeToDefaultHeap((void *)queueEntries);
	}
	return false;
}

bool checkForQueueErrors()
{
	if(fwriteErrno)
	{
		if(fwriteOverlay == -1 && OSIsMainCore())
		{
			OSSleepTicks(OSMillisecondsToTicks(20)); // Lazy race condition prevention
			char errMsg[1024];
			sprintf(errMsg, "Write error:\n%s\n\nThis is an unrecoverable error!", errnoToString(fwriteErrno));
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

	if(!checkForQueueErrors())
	{
		spinLock(ioWriteLock);

		while(queueEntries[activeWriteBuffer].file != NULL)
			;
	}
	
	ioRunning = false;
#ifdef NUSSPLI_DEBUG
	int ret;
	stopThread(ioThread, &ret);
	debugPrintf("I/O thread returned: %d", ret);
#else
	stopThread(ioThread, NULL);
#endif
	MEMFreeToDefaultHeap((void *)queueEntries);
}

#ifdef NUSSPLI_DEBUG
bool queueStalled = false;
#endif
size_t addToIOQueue(const void *buf, size_t size, size_t n, NUSFILE *file)
{
/*	if(checkForQueueErrors())
		return 0;

    volatile WriteQueueEntry *entry;
		
retryAddingToQueue:
	
	while(!spinTryLock(ioWriteLock))
		if(!ioRunning)
			return 0;
	
    entry = queueEntries + activeReadBuffer;
	if(entry->file != NULL)
	{
        spinReleaseLock(ioWriteLock);
#ifdef NUSSPLI_DEBUG
		if(!queueStalled)
		{
			debugPrintf("Waiting for free slot...");
			queueStalled = true;
		}
#endif
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
		{
			n = 0;
			goto queueExit;
		}

		if(size > IO_BUFSIZE)
		{
            spinReleaseLock(ioWriteLock);
			debugPrintf("size > %i (%i)", IO_BUFSIZE, size);
			addToIOQueue(buf, 1, IO_BUFSIZE, file);
			const uint8_t *newPtr = buf;
			newPtr += IO_BUFSIZE;
			addToIOQueue((const void *)newPtr, 1, size - IO_BUFSIZE, file);
			return n;
		}
		
		OSBlockMove((void *)entry->buf, buf, size, false);
		entry->size = size;
	}
	else
		entry->size = 0;
	
	entry->file = file;
	
	if(++activeReadBuffer == MAX_IO_QUEUE_ENTRIES)
		activeReadBuffer = 0;

queueExit:
	spinReleaseLock(ioWriteLock);

	*/

	if(buf != NULL)
		return size * n;

	fclose(entry->file->fd))
	MEMFreeToDefaultHeap(entry->file->buffer);
	MEMFreeToDefaultHeap((void *)entry->file);
	return 0;
}

void flushIOQueue()
{
	if(checkForQueueErrors())
		return;

	int ovl = addErrorOverlay("Flushing queue, please wait...");
	debugPrintf("Flushing...");
	spinLock(ioWriteLock);

	while(queueEntries[activeWriteBuffer].file)
	{
		OSSleepTicks(1024);
		if(checkForQueueErrors())
			break;
	}

	spinReleaseLock(ioWriteLock);
	removeErrorOverlay(ovl);

	checkForQueueErrors();
}

NUSFILE *openFile(const char *path, const char *mode)
{
	if(checkForQueueErrors())
		return NULL;

	NUSFILE *ret = MEMAllocFromDefaultHeap(sizeof(NUSFILE));
	if(ret == NULL)
		return NULL;
	
	OSTime t = OSGetTime();
	ret->fd = fopen(path, mode);
	if(ret->fd != NULL)
	{
		t = OSGetTime() - t;
		addEntropy(&t, sizeof(OSTime));
		ret->buffer = MEMAllocFromDefaultHeapEx(IO_MAX_FILE_BUFFER, 0x40);
		if(ret->buffer != NULL)
		{
			if(setvbuf(ret->fd, ret->buffer, _IOFBF, IO_MAX_FILE_BUFFER) != 0)
				debugPrintf("Error setting buffer!");

			debugPrintf("File open: 0x%08X", ret->fd);
			return ret;
		}
		fclose(ret->fd);
	}
	MEMFreeToDefaultHeap(ret);
	return NULL;
}
