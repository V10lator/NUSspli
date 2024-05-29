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

#include <exception.h>
#include <thread.h>
#include <utils.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/context.h>
#include <coreinit/core.h>
#include <coreinit/debug.h>
#include <coreinit/exception.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memorymap.h>
#include <coreinit/thread.h>
#pragma GCC diagnostic pop

#define CRASH_BUFSIZE 0x800

typedef struct BACKTRACK_LIST BACKTRACK_LIST;
struct BACKTRACK_LIST
{
    BACKTRACK_LIST *next;
    uint32_t stackPointer;
};

static OSThread *crashThread = NULL;
static volatile const char *crashType;
static volatile OSContext *crashContext;

static inline const char *getFun(uint32_t addy)
{
    if(!OSIsAddressValid(addy))
        return "<unknown>";

    static char fun[64];
    uint32_t ret = OSGetSymbolName(addy, fun, 64);
    snprintf(fun + strlen(fun), 64 - strlen(fun), "+0x%X", addy - ret);
    return fun;
}

static int exceptionHandlerThread(int argc, const char **argv)
{
    (void)argc;
    (void)argv;

    char buf[CRASH_BUFSIZE];
    snprintf(buf, CRASH_BUFSIZE,
        "An error has occured. Press POWER for 4 seconds to shutdown.\n"
        "--------------------------------------------------------------\n"
        "Core%d: %s exception at 0x%08X (SRR0) | DAR: 0x%08X\n"
        "Tag  : 0x%016llX (expected 0x%016llX)\n"
        "SRR1 : 0x%08X | DSISR: 0x%08X\n"
        "SRR0 : 0x%08X %s\n"
        "LR0  : 0x%08X %s",
        OSGetCoreId(), crashType, crashContext->srr0, crashContext->dar,
        crashContext->tag, OS_CONTEXT_TAG,
        crashContext->srr1, crashContext->dsisr,
        crashContext->srr0, getFun(crashContext->srr0),
        crashContext->lr, getFun(crashContext->lr));

#ifdef NUSSPLI_DEBUG
    char *ptr = buf;
    char *needle = strstr(ptr, "\n");
    while(needle != NULL)
    {
        *needle = '\0';
        debugPrintf(ptr);
        *needle = '\n';
        ptr = needle + 1;
        if(*ptr == '\0')
            goto debugFinished;

        needle = strstr(ptr, "\n");
    }

    debugPrintf(ptr);
debugFinished:
#endif

    int i = 0;
    for(BACKTRACK_LIST *backtrack = (BACKTRACK_LIST *)crashContext->gpr[1]; backtrack != NULL; backtrack = backtrack->next)
    {
        ++i;
        const char *fn = getFun(backtrack->stackPointer);
#ifdef NUSSPLI_DEBUG
        debugPrintf("LR%-3d: 0x%08X %s", i, backtrack->stackPointer, fn);
        if(i < 13)
        {
#endif
            size_t len = strlen(buf);
            if(CRASH_BUFSIZE - 1 - len > 2)
            {
                buf[len++] = '\n';
                snprintf(buf + len, CRASH_BUFSIZE - len, "LR%-3d: 0x%08X %s", i, backtrack->stackPointer, fn);
            }
#ifdef NUSSPLI_DEBUG
        }
#else
        if(i == 12)
            break;
#endif
    }

#ifdef NUSSPLI_DEBUG
    for(i = 0; i < 32; ++i)
        debugPrintf("r%-2d: 0x%08X", i, crashContext->gpr[i]);
#endif

    OSFatal(buf);
    return 0;
}

static inline bool internalExceptionHandler(OSContext *ctx)
{
    crashContext = ctx;
    uint32_t aff;
    switch(OSGetCoreId())
    {
        case 0:
            aff = OS_THREAD_ATTRIB_AFFINITY_CPU0;
            break;
        case 1:
            aff = OS_THREAD_ATTRIB_AFFINITY_CPU1;
            break;
        default:
            aff = OS_THREAD_ATTRIB_AFFINITY_CPU2;
            break;
    }

    OSSetThreadAffinity(crashThread, aff);
    OSResumeThread(crashThread);
    return true;
}

static BOOL dsiExceptionHandler(OSContext *ctx)
{
    crashType = "DSI";
    return internalExceptionHandler(ctx);
}

static BOOL isiExceptionHandler(OSContext *ctx)
{
    crashType = "ISI";
    return internalExceptionHandler(ctx);
}

static BOOL proExceptionHandler(OSContext *ctx)
{
    crashType = "PROG";
    return internalExceptionHandler(ctx);
}

static BOOL defaultExceptionHandler(OSContext *ctx)
{
    (void)ctx;
    return false;
}

bool initExceptionHandler()
{
    crashThread = prepareThread("NUSspli exception handler", THREAD_PRIORITY_EXCEPTION, STACKSIZE_SMALL, exceptionHandlerThread, 0, NULL, OS_THREAD_ATTRIB_DETACHED);
    if(crashThread == NULL)
        return false;

    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, dsiExceptionHandler);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, isiExceptionHandler);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM, proExceptionHandler);
    return true;
}

void deinitExceptionHandler()
{
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, defaultExceptionHandler);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, defaultExceptionHandler);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM, defaultExceptionHandler);
    MEMFreeToDefaultHeap(crashThread);
}
