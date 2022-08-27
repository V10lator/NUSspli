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

#include <dirent.h>
#include <stdbool.h>
#include <string.h>

#include <coreinit/ios.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <crypto.h>
#include <deinstaller.h>
#include <file.h>
#include <input.h>
#include <localisation.h>
#include <menu/utils.h>
#include <notifications.h>
#include <osdefs.h>
#include <renderer.h>
#include <state.h>
#include <utils.h>

bool deinstall(MCPTitleListType *title, const char *name, bool channelHaxx, bool skipEnd)
{
    startNewFrame();
    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, gettext("Uninstalling"));
    strcat(toFrame, " ");
    strcat(toFrame, name);
    textToFrame(0, 0, toFrame);
    textToFrame(1, 0, gettext("Preparing..."));
    writeScreenLog(2);
    drawFrame();
    showFrame();

    MCPInstallTitleInfo *info = MEMAllocFromDefaultHeapEx(sizeof(MCPInstallTitleInfo), 0x40);
    if(info == NULL)
        return false;

    McpData data;
    glueMcpData(info, &data);

    // err = MCP_UninstallTitleAsync(mcpHandle, title->path, info);
    //  The above crashes MCP, so let's leave WUT:
    debugPrintf("Deleting %s", title->path);
    OSTick t = OSGetTick();
    if(!channelHaxx)
        disableShutdown();

    MCPError err = MCP_DeleteTitleAsync(mcpHandle, title->path, info);
    if(err != 0)
    {
        MEMFreeToDefaultHeap(info);
        debugPrintf("Err1: %#010x (%d)", err, err);
        if(!channelHaxx)
            enableShutdown();
        return false;
    }

    if(channelHaxx)
    {
        MEMFreeToDefaultHeap(info);
        return true;
    }

    showMcpProgress(&data, name, false);
    MEMFreeToDefaultHeap(info);
    enableShutdown();
    t = OSGetTick() - t;
    addEntropy(&t, sizeof(OSTick));
    addToScreenLog("Deinstallation finished!");

    if(!skipEnd)
    {
        startNotification();

        colorStartNewFrame(SCREEN_COLOR_D_GREEN);
        textToFrame(0, 0, name);
        textToFrame(1, 0, gettext("Uninstalled successfully!"));
        writeScreenLog(2);
        drawFrame();

        while(AppRunning())
        {
            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
            {
                colorStartNewFrame(SCREEN_COLOR_D_GREEN);
                textToFrame(0, 0, name);
                textToFrame(1, 0, gettext("Uninstalled successfully!"));
                writeScreenLog(2);
                drawFrame();
            }

            showFrame();

            if(vpad.trigger)
                break;
        }

        stopNotification();
    }
    return true;
}
