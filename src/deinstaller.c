/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2021-2022 V10lator <v10lator@myway.de>                    *
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

#include <crypto.h>
#include <deinstaller.h>
#include <filesystem.h>
#include <localisation.h>
#include <menu/utils.h>
#include <osdefs.h>
#include <renderer.h>
#include <state.h>
#include <ticket.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#include <coreinit/thread.h>
#pragma GCC diagnostic pop

bool deinstall(MCPTitleListType *title, const char *name, bool channelHaxx, bool skipEnd)
{
    startNewFrame();
    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, localise("Uninstalling"));
    strcat(toFrame, " ");
    strcat(toFrame, name);
    textToFrame(0, 0, toFrame);
    textToFrame(1, 0, localise("Preparing..."));
    writeScreenLog(2);
    drawFrame();
    showFrame();

    size_t titleSize = getDirsize(title->path);
    MCPInstallTitleInfo info __attribute__((__aligned__(0x40)));
    McpData data;
    glueMcpData(&info, &data);

    debugPrintf("Deleting %s", title->path);
    OSTick t = OSGetTick();
    if(!channelHaxx)
        disableShutdown();

    MCPError err = MCP_DeleteTitleAsync(mcpHandle, title->path, &info);
    if(err != 0)
    {
        debugPrintf("Err1: %#010x (%d)", err, err);
        if(!channelHaxx)
            enableShutdown();
        return false;
    }

    if(channelHaxx)
    {
        OSSleepTicks(OSSecondsToTicks(10));
        return true;
    }

    showMcpProgress(&data, name, false);
    deleteTicket(title->titleId);
    enableShutdown();
    t = OSGetTick() - t;
    addEntropy(&t, sizeof(OSTick));

    freeSpace(getDevFromPath(title->path), titleSize);
    addToScreenLog("Deinstallation finished!");

    if(!skipEnd)
        showFinishedScreen(name, FINISHING_OPERATION_DEINSTALL);

    return true;
}
