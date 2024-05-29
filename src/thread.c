/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2021 V10lator <v10lator@myway.de>                    *
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
#include <thread.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#pragma GCC diagnostic pop

OSThread *prepareThread(const char *name, THREAD_PRIORITY priority, size_t stacksize, OSThreadEntryPointFn mainfunc, int argc, char *argv, OSThreadAttributes attribs)
{
    if(name == NULL)
        return NULL;

    uint8_t *thread = MEMAllocFromDefaultHeapEx(sizeof(OSThread) + stacksize, 8);
    if(thread != NULL)
    {
        OSThread *ost = (OSThread *)thread;
        if(OSCreateThread(ost, mainfunc, argc, argv, thread + stacksize + sizeof(OSThread), stacksize, priority, attribs))
        {
            OSSetThreadName(ost, name);
#ifdef NUSSPLI_DEBUG
            if(!OSSetThreadStackUsage(ost))
                debugPrintf("Tracking stack usage failed for %s", name);
#endif
            return ost;
        }

        MEMFreeToDefaultHeap(thread);
    }

    return NULL;
}

// Our current implementation glues the threads stack to the OSThread, returning something 100% OSThread compatible
OSThread *startThread(const char *name, THREAD_PRIORITY priority, size_t stacksize, OSThreadEntryPointFn mainfunc, int argc, char *argv, OSThreadAttributes attribs)
{
    OSTime t;
    addEntropy(&t, sizeof(OSTime));
    t = OSGetSystemTime();
    OSThread *thread = prepareThread(name, priority, stacksize, mainfunc, argc, argv, attribs);
    if(thread == NULL)
        return NULL;

    OSResumeThread(thread);
    t = OSGetSystemTime() - t;
    addEntropy(&t, sizeof(OSTime));
    addEntropy(&(thread->id), sizeof(uint16_t));
    return thread;
}
