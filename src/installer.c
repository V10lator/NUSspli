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

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
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
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <ioQueue.h>
#include <localisation.h>
#include <menu/utils.h>
#include <notifications.h>
#include <renderer.h>
#include <state.h>
#include <staticMem.h>
#include <utils.h>

static void cleanupCancelledInstallation(NUSDEV dev, const char *path, bool toUsb, bool keepFiles)
{
    debugPrintf("Cleaning up...");

    switch(dev)
    {
    case NUSDEV_USB01:
    case NUSDEV_USB02:
    case NUSDEV_MLC:
        keepFiles = false;
    default:
        break;
    }

    if(!keepFiles)
        removeDirectory(path);

    FSDirectoryHandle dir;
    char *importPath = getStaticPathBuffer(2);
    strcpy(importPath, toUsb ? (getUSB() == NUSDEV_USB01 ? NUSDIR_USB1 "usr/import/" : NUSDIR_USB2 "usr/import/") : NUSDIR_MLC "usr/import/");

    if(FSOpenDir(__wut_devoptab_fs_client, getCmdBlk(), importPath, &dir, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
    {
        char *ptr = importPath + strlen(importPath);
        FSDirectoryEntry entry;

        while(FSReadDir(__wut_devoptab_fs_client, getCmdBlk(), dir, &entry, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
        {
            if(entry.name[0] == '.' || !(entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 8)
                continue;

            strcpy(ptr, entry.name);
            removeDirectory(importPath);
        }

        FSCloseDir(__wut_devoptab_fs_client, getCmdBlk(), dir, FS_ERROR_FLAG_ALL);
    }
}

bool install(const char *game, bool hasDeps, NUSDEV dev, const char *path, bool toUsb, bool keepFiles, uint64_t tid)
{
    if(tid != 0)
    {
        MCPTitleListType titleEntry;
        if(MCP_GetTitleInfo(mcpHandle, tid, &titleEntry) == 0)
            deinstall(&titleEntry, game, false, true);
    }

    startNewFrame();
    char *toScreen = getToFrameBuffer();
    strcpy(toScreen, gettext("Installing"));
    strcat(toScreen, " ");
    strcat(toScreen, game);
    textToFrame(0, 0, toScreen);
    barToFrame(1, 0, 40, 0);
    textToFrame(1, 41, gettext("Preparing. This might take some time. Please be patient."));
    writeScreenLog(2);
    drawFrame();
    showFrame();

    MCPInstallTitleInfo *info = MEMAllocFromDefaultHeapEx(sizeof(MCPInstallTitleInfo), 0x40);
    if(info == NULL)
        return false;

    McpData data;
    flushIOQueue(); // Make sure all game files are on disc

    // Let's see if MCP is able to parse the TMD...
    OSTime t = OSGetSystemTime();
    data.err = MCP_InstallGetInfo(mcpHandle, path, (MCPInstallInfo *)info);
    t = OSGetSystemTime() - t;
    addEntropy(&t, sizeof(OSTime));
    if(data.err != 0)
    {
        switch(data.err)
        {
        case 0xfffbf3e2:
            sprintf(toScreen, "%s \"%s\"", gettext("No title.tmd found at"), path);
            break;
        case 0xfffbfc17:
            sprintf(toScreen,
                "%s \"%s\""
#ifdef NUSSPLI_HBL
                "\n%s"
#endif
                ,
                gettext("Internal error installing"), path
#ifdef NUSSPLI_HBL
                ,
                gettext("We're supporting HBL on Tiramisu and HBLC v2.1 fix by Gary only!")
#endif
            );
            break;
        default:
            sprintf(toScreen, "%s \"%s\" %s: %#010x", gettext("Error getting info for"), path, gettext("from MCP"), data.err);
        }

        debugPrintf(toScreen);
        addToScreenLog("Installation failed!");
        drawErrorFrame(toScreen, ANY_RETURN);

        while(AppRunning())
        {
            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
                drawErrorFrame(toScreen, ANY_RETURN);

            showFrame();

            if(vpad.trigger)
                break;
        }
        goto installError;
    }

    // Allright, let's set if we want to install to USB or NAND
    MCPInstallTarget target = toUsb ? MCP_INSTALL_TARGET_USB : MCP_INSTALL_TARGET_MLC;

    data.err = MCP_InstallSetTargetDevice(mcpHandle, target);
    if(data.err == 0)
    {
        if(toUsb && getUSB() == NUSDEV_USB02)
            data.err = MCP_InstallSetTargetUsb(mcpHandle, ++target);
    }

    if(data.err != 0)
    {
        const char *err = gettext(toUsb ? "Error opening USB device" : "Error opening internal memory");
        addToScreenLog("Installation failed!");
        drawErrorFrame(err, ANY_RETURN);

        while(AppRunning())
        {
            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
                drawErrorFrame(err, ANY_RETURN);

            showFrame();

            if(vpad.trigger)
                break;
        }
        goto installError;
    }

    // Just some debugging stuff
    debugPrintf("Path: %s (%d)", path, strlen(path));

    // Last preparing step...
    glueMcpData(info, &data);

    // Start the installation process
    t = OSGetSystemTime();
    disableShutdown();
    MCPError err = MCP_InstallTitleAsync(mcpHandle, path, info);

    if(err != 0)
    {
        sprintf(toScreen, "%s \"%s\": %#010x", gettext("Error starting async installation of"), path, data.err);
        debugPrintf(toScreen);
        addToScreenLog("Installation failed!");
        drawErrorFrame(toScreen, ANY_RETURN);

        while(AppRunning())
        {
            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
                drawErrorFrame(toScreen, ANY_RETURN);

            showFrame();

            if(vpad.trigger)
                break;
        }
        enableShutdown();
        goto installError;
    }

    showMcpProgress(&data, game, true);
    enableShutdown();
    t = OSGetSystemTime() - t;
    addEntropy(&t, sizeof(OSTime));
    MEMFreeToDefaultHeap(info);

    // MCP thread finished. Let's see if we got any error - TODO: This is a 1:1 copy&paste from WUP Installer GX2 which itself stole it from WUP Installer Y mod which got it from WUP Installer minor edit by Nexocube who got it from WUP installer JHBL Version by Dimrok who portet it from the ASM of WUP Installer. So I think it's time for something new... ^^
    if(data.err != 0)
    {
        debugPrintf("Installation failed with result: %#010x", data.err);
        strcpy(toScreen, gettext("Installation failed!"));
        strcat(toScreen, "\n\n");
        switch(data.err)
        {
        case CUSTOM_MCP_ERROR_CANCELLED:
            cleanupCancelledInstallation(dev, path, toUsb, keepFiles);
        case CUSTOM_MCP_ERROR_EOM:
            return true;
        case 0xFFFCFFE9:
            if(hasDeps)
            {
                strcat(toScreen, "Install the main game to the same storage medium first");
                if(toUsb)
                {
                    strcat(toScreen, "\n");
                    strcat(toScreen, gettext("Also make sure there is no error with the USB drive"));
                }
            }
            else if(toUsb)
                strcat(toScreen, gettext("Possible USB error"));
            break;
        case 0xFFFBF446:
        case 0xFFFBF43F:
            strcat(toScreen, gettext("Possible missing or bad title.tik file"));
            break;
        case 0xFFFBF441:
            strcat(toScreen, gettext("Possible incorrect console for DLC title.tik file"));
            break;
        case 0xFFFCFFE4:
            strcat(toScreen, gettext("Not enough free space on target device"));
            break;
        case 0xFFFFF825:
        case 0xFFFFF82E:
            strcat(toScreen, gettext("Files might be corrupt or bad storage medium.\nTry redownloading files or reformat/replace target device"));
            break;
        default:
            if((data.err & 0xFFFF0000) == 0xFFFB0000)
            {
                if(dev & NUSDEV_USB)
                {
                    strcat(toScreen, gettext("Possible USB failure. Check your drives power source."));
                    strcat(toScreen, "\n");
                }

                strcat(toScreen, gettext("Files might be corrupt"));
            }
            else
                sprintf(toScreen + strlen(toScreen), "%s: %#010x", gettext("Unknown Error"), data.err);
        }

        addToScreenLog("Installation failed!");
        drawErrorFrame(toScreen, ANY_RETURN);

        while(AppRunning())
        {
            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
                drawErrorFrame(toScreen, ANY_RETURN);

            showFrame();

            if(vpad.trigger)
                break;
        }
        return false;
    }

    addToScreenLog("Installation finished!");

    if(!keepFiles && dev == NUSDEV_SD)
    {
        debugPrintf("Removing installation files...");
        removeDirectory(path);
    }

    colorStartNewFrame(SCREEN_COLOR_D_GREEN);
    textToFrame(0, 0, game);
    textToFrame(1, 0, gettext("Installed successfully!"));
    writeScreenLog(2);
    drawFrame();

    startNotification();

    while(AppRunning())
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
        {
            colorStartNewFrame(SCREEN_COLOR_D_GREEN);
            textToFrame(0, 0, game);
            textToFrame(1, 0, gettext("Installed successfully!"));
            writeScreenLog(2);
            drawFrame();
        }

        showFrame();

        if(vpad.trigger)
            break;
    }

    stopNotification();
    return true;

installError:
    MEMFreeToDefaultHeap(info);
    return false;
}
