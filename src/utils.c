/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
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
#include <input.h>
#include <localisation.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <utils.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <coreinit/atomic.h>
#include <coreinit/ios.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>

int mcpHandle;

void hex(uint64_t i, int digits, char *out)
{
    char x[8]; // max 99 digits!
    sprintf(x, "%%0%illx", digits);
    sprintf(out, x, i);
}

void getSpeedString(double bytePerSecond, char *out)
{
    double bitPerSecond = bytePerSecond * 8.0D;

    if(bitPerSecond < 1024.0D)
        sprintf(out, "%.2f b/s (", bitPerSecond);
    else if(bitPerSecond < 1024.0D * 1024.0D)
        sprintf(out, "%.2f Kb/s (", bitPerSecond / 1024.0D);
    else
        sprintf(out, "%.2f Mb/s (", bitPerSecond / (1024.0D * 1024.0D));

    out += strlen(out);

    if(bytePerSecond < 1024.0D)
        sprintf(out, "%.2f B/s)", bytePerSecond);
    else if(bytePerSecond < 1024.0D * 1024.0D)
        sprintf(out, "%.2f KB/s)", bytePerSecond / 1024.0D);
    else
        sprintf(out, "%.2f MB/s)", bytePerSecond / (1024.0D * 1024.0D));
}

void secsToTime(uint32_t seconds, char *out)
{
    uint32_t minute, hour;

    hour = seconds / 3600;
    hour = hour % 3600;
    minute = seconds / 60;
    minute = minute % 60;
    seconds = seconds % 60;

    bool visible = false;
    const char *i10n;

    if(hour)
    {
        sprintf(out, "%u ", hour);
        out += strlen(out);
        i10n = gettext("hours");
        strcpy(out, i10n);
        out += strlen(i10n);
        strcpy(out++, " ");
        visible = true;
    }
    if(minute || visible)
    {
        sprintf(out, "%02u ", minute);
        out += 3;
        i10n = gettext("minutes");
        strcpy(out, i10n);
        out += strlen(i10n);
        strcpy(out++, " ");
        visible = true;
    }

    if(seconds || visible)
    {
        sprintf(out, "%02u ", seconds);
        out += 3;
        i10n = gettext("seconds");
        strcpy(out, i10n);
        out += strlen(i10n);
        strcpy(out, " ");
    }
    else
        strcpy(out, "N/A");
}

uint8_t charToByte(char c)
{
    if(isNumber(c))
        return c - '0';
    if(isLowercaseHexa(c))
        return c - 'a' + 0xA;
    if(isUppercaseHexa(c))
        return c - 'A' + 0xA;
    return 0xFF;
}

void hexToByte(const char *hex, uint8_t *out)
{
    for(int i = 0; *hex != '\0' && i < 64; out[i++] |= charToByte(*hex++))
        out[i] = charToByte(*hex++) << 4;
}

static void mcpCallback(MCPError err, void *rawData)
{
    McpData *data = (McpData *)rawData;
    if(data->err == 0)
        data->err = err;
    data->processing = false;
}

void glueMcpData(MCPInstallTitleInfo *info, McpData *data)
{
    data->processing = true;
    data->err = 0;
    uint32_t *ptr = (uint32_t *)info;
    *ptr = (uint32_t)mcpCallback;
    *++ptr = (uint32_t)data;
}

void showMcpProgress(McpData *data, const char *game, const bool inst)
{
    MCPInstallProgress *progress = MEMAllocFromDefaultHeapEx(sizeof(MCPInstallProgress), 0x40);
    if(progress == NULL)
    {
        debugPrintf("Error allocating memory!");
        data->err = CUSTOM_MCP_ERROR_EOM;
        return;
    }

    progress->inProgress = 0;
    char multiplierName[3];
    int multiplier = 0;
    char *toScreen = getToFrameBuffer();
    MCPError err;
    OSTime lastSpeedCalc = 0;
    OSTime now;
    uint64_t lsp = 0;
    char speedBuf[32];
    speedBuf[0] = '\0';
    int ovl = -1;

    while(data->processing)
    {
        err = MCP_InstallGetProgress(mcpHandle, progress);
        if(err == IOS_ERROR_OK)
        {
            if(progress->inProgress == 1 && progress->sizeTotal != 0 && data->err != CUSTOM_MCP_ERROR_CANCELLED)
            {
                if(multiplier == 0)
                {
                    if(progress->sizeTotal < 1 << 10)
                    {
                        multiplier = 1;
                        strcpy(multiplierName, "B");
                    }
                    else if(progress->sizeTotal < 1 << 20)
                    {
                        multiplier = 1 << 10;
                        strcpy(multiplierName, "KB");
                    }
                    else if(progress->sizeTotal < 1 << 30)
                    {
                        multiplier = 1 << 20;
                        strcpy(multiplierName, "MB");
                    }
                    else
                    {
                        multiplier = 1 << 30;
                        strcpy(multiplierName, "GB");
                    }
                }
                startNewFrame();
                strcpy(toScreen, gettext(inst ? "Installing" : "Uninstalling"));
                strcat(toScreen, " ");
                strcat(toScreen, game);
                textToFrame(0, 0, toScreen);
                double prg = (double)progress->sizeProgress;
                barToFrame(1, 0, 40, prg * 100.0f / progress->sizeTotal);
                sprintf(toScreen, "%.2f / %.2f %s", prg / multiplier, ((double)progress->sizeTotal) / multiplier, multiplierName);
                textToFrame(1, 41, toScreen);

                if(progress->sizeProgress != 0)
                {
                    now = OSGetSystemTime();
                    if(OSTicksToMilliseconds(now - lastSpeedCalc) > 333)
                    {
                        getSpeedString(prg - lsp, speedBuf);
                        lsp = prg;
                        lastSpeedCalc = now;
                    }
                    textToFrame(1, ALIGNED_RIGHT, speedBuf);
                }

                writeScreenLog(2);
                drawFrame();
            }
        }
        else
            debugPrintf("MCP_InstallGetProgress() returned %#010x", err);

        showFrame();

        if(inst)
        {
            if(ovl == -1)
            {
                if(vpad.trigger & VPAD_BUTTON_B)
                {
                    sprintf(toScreen, "%s\n\n" BUTTON_A " %s || " BUTTON_B " %s", gettext("Do you really want to cancel?"), gettext("Yes"), gettext("No"));
                    ovl = addErrorOverlay(toScreen);
                }
            }
            else if(ovl >= 0)
            {
                if(vpad.trigger & VPAD_BUTTON_A)
                {
                    removeErrorOverlay(ovl);
                    ovl = -2;

                    startNewFrame();
                    textToFrame(0, 0, gettext("Cancelling installation."));
                    textToFrame(1, 0, gettext("Please wait..."));
                    writeScreenLog(2);
                    drawFrame();
                    showFrame();

                    MCP_InstallTitleAbort(mcpHandle);
                    data->err = CUSTOM_MCP_ERROR_CANCELLED;
                }
                else if(vpad.trigger & VPAD_BUTTON_B)
                {
                    removeErrorOverlay(ovl);
                    ovl = -1;
                }
            }
        }
    }

    if(ovl >= 0)
        removeErrorOverlay(ovl);

    MEMFreeToDefaultHeap(progress);
}

#ifdef NUSSPLI_DEBUG

#include <stdarg.h>

#include <thread.h>

#include <coreinit/fastmutex.h>
#include <coreinit/time.h>
#include <whb/log.h>

static const char days[7][4] = {
    "Sun",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat",
};

static const char months[12][4] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Nov",
    "Dez",
};

static spinlock debugLock;

void debugInit()
{
    spinCreateLock(debugLock, SPINLOCK_FREE);
    WHBLogUdpInit();
}

void debugPrintf(const char *str, ...)
{
    spinLock(debugLock);
    static char newStr[512];

    OSCalendarTime now;
    OSTicksToCalendarTime(OSGetTime(), &now);
    sprintf(newStr, "%s %02d %s %d %02d:%02d:%02d.%03d\t", days[now.tm_wday], now.tm_mday, months[now.tm_mon], now.tm_year, now.tm_hour, now.tm_min, now.tm_sec, now.tm_msec);
    size_t tss = strlen(newStr);

    va_list va;
    va_start(va, str);
    vsnprintf(newStr + tss, 511 - tss, str, va);
    va_end(va);

    WHBLogPrint(newStr);
    spinReleaseLock(debugLock);
}

void checkStacks(const char *src)
{
    debugPrintf("%s: Checking thread stacks...", src);
    OSCheckActiveThreads();
    OSThread *trd = OSGetCurrentThread();
    debugPrintf("%s: 0x%08X/0x%08X", src, OSCheckThreadStackUsage(trd), ((uint32_t)trd->stackStart) - ((uint32_t)trd->stackEnd));
}

#endif // ifdef NUSSPLI_DEBUG
