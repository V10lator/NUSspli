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
#include <stdint.h>

#include <crypto.h>
#include <deinstaller.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <ioQueue.h>
#include <localisation.h>
#include <menu/utils.h>
#include <no-intro.h>
#include <renderer.h>
#include <state.h>
#include <staticMem.h>
#include <ticket.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/mcp.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>
#pragma GCC diagnostic pop

#define IMPORTDIR_USB1 (NUSDIR_USB1 "usr/import/")
#define IMPORTDIR_USB2 (NUSDIR_USB2 "usr/import/")
#define IMPORTDIR_MLC  (NUSDIR_MLC "usr/import/")

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

    FSADirectoryHandle dir;
    char importPath[sizeof(IMPORTDIR_MLC) + 8];
    OSBlockMove(importPath, toUsb ? (getUSB() == NUSDEV_USB01 ? IMPORTDIR_USB1 : IMPORTDIR_USB2) : IMPORTDIR_MLC, sizeof(IMPORTDIR_MLC), false);

    if(FSAOpenDir(getFSAClient(), importPath, &dir) == FS_ERROR_OK)
    {
        importPath[sizeof(IMPORTDIR_MLC) + 7] = '\0';
        FSADirectoryEntry entry;

        while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
        {
            if(!(entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 8)
                continue;

            OSBlockMove(importPath + (sizeof(IMPORTDIR_MLC) - 1), entry.name, 8, false);
            removeDirectory(importPath);
        }

        FSACloseDir(getFSAClient(), dir);
    }
}

bool install(const char *game, bool hasDeps, NUSDEV dev, const char *path, bool toUsb, bool keepFiles, const TMD *tmd)
{
    if(tmd != NULL)
    {
        MCPTitleListType titleEntry __attribute__((__aligned__(0x40)));
        if(MCP_GetTitleInfo(mcpHandle, tmd->tid, &titleEntry) == 0)
            deinstall(&titleEntry, game, false, true);
    }

    startNewFrame();
    char *toScreen = getToFrameBuffer();
    strcpy(toScreen, localise("Installing"));
    strcat(toScreen, " ");
    strcat(toScreen, game);
    textToFrame(0, 0, toScreen);
    barToFrame(1, 0, 40, 0.0f);
    textToFrame(1, 41, localise("Preparing. This might take some time. Please be patient."));
    writeScreenLog(2);
    drawFrame();
    showFrame();
    flushIOQueue(); // Make sure all game files are on disc

    TMD *tmd2;
    if(tmd != NULL)
        tmd2 = (TMD *)tmd;
    else
    {
        tmd2 = getTmd(path, false);
        if(tmd2 == NULL)
            goto noTmd;
    }

    uint64_t size = 0;
    for(uint16_t i = 0; i < tmd2->num_contents; ++i)
        size += tmd2->contents[i].size;

    if(!checkFreeSpace(toUsb ? getUSB() : NUSDEV_MLC, size))
        return !(AppRunning(true));

    // No-intro
    char *tmpPath = getStaticPathBuffer(1);
    size_t s = strlen(path);
    OSBlockMove(tmpPath, path, s, false);
    OSBlockMove(tmpPath + s, "title.tmd", sizeof("title.tmd"), false);
    NO_INTRO_DATA *noIntro;
    if(fileExists(tmpPath))
        noIntro = NULL;
    else
    {
        noIntro = transformNoIntro(path);
        if(noIntro == NULL)
        {
            const char *err = localise("Error transforming no-image set");
            addToScreenLog("Installation failed!");
            showErrorFrame(err);
            return false;
        }
    }

    // Fix tickets of broken NUSspli versions
    if(isDLC(tmd2->tid))
    {
        OSBlockMove(tmpPath + s, "title.tik", sizeof("title.tik"), false);
        TICKET *tik;
        s = readFile(tmpPath, (void **)&tik);
        if(tik != NULL && hasMagicHeader(tik) && strcmp(tik->header.app, "NUSspli") == 0)
        {
            char *needle = strstr(tik->header.app_version, ".");
            if(needle)
            {
                *needle = '\0';
                if(atoi(tik->header.app_version) == 1) // Major version
                {
                    char *betaNeedle = strstr(++needle, "-");
                    if(betaNeedle)
                        *betaNeedle = '\0';

                    int minor = atoi(needle);
                    if(minor >= 113 && minor < 125)
                    {
                        debugPrintf("Broken ticket detected, fixing...");
                        if(generateTik(tmpPath, tmd2))
                        {
                            if(noIntro != NULL)
                                revertNoIntro(noIntro);

                            return install(game, hasDeps, dev, path, toUsb, keepFiles, tmd2);
                        }
                        else
                            debugPrintf("Error fixing ticket!");
                    }
                }
            }
        }
    }

    MCPInstallTitleInfo info __attribute__((__aligned__(0x40)));
    McpData data;

    // Let's see if MCP is able to parse the TMD...
    OSTime t = OSGetSystemTime();
    data.err = MCP_InstallGetInfo(mcpHandle, path, (MCPInstallInfo *)&info);
    t = OSGetSystemTime() - t;
    addEntropy(&t, sizeof(OSTime));
    if(data.err != 0)
    {
        if(noIntro != NULL)
            revertNoIntro(noIntro);

        switch(data.err)
        {
            case 0xfffbf3e2:
            noTmd:
                sprintf(toScreen, "%s \"%s\"", localise("No title.tmd found at"), path);
                break;
            case 0xfffbfc17:
                sprintf(toScreen, "%s \"%s\"", localise("Internal error installing"), path);
                break;
            default:
                sprintf(toScreen, "%s \"%s\" %s: %#010x", localise("Error getting info for"), path, localise("from MCP"), data.err);
        }

        debugPrintf(toScreen);
        addToScreenLog("Installation failed!");
        showErrorFrame(toScreen);
        return false;
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
        if(noIntro != NULL)
            revertNoIntro(noIntro);

        const char *err = localise(toUsb ? "Error opening USB device" : "Error opening internal memory");
        addToScreenLog("Installation failed!");
        showErrorFrame(err);
        return false;
    }

    // Just some debugging stuff
    debugPrintf("Path: %s (%d)", path, strlen(path));

    // Last preparing step...
    glueMcpData(&info, &data);

    // Start the installation process
    t = OSGetSystemTime();
    disableShutdown();
    MCPError err = MCP_InstallTitleAsync(mcpHandle, path, &info);

    if(err != 0)
    {
        if(noIntro != NULL)
            revertNoIntro(noIntro);

        sprintf(toScreen, "%s \"%s\": %#010x", localise("Error starting async installation of"), path, data.err);
        debugPrintf(toScreen);
        addToScreenLog("Installation failed!");
        showErrorFrame(toScreen);
        enableShutdown();
        return false;
    }

    showMcpProgress(&data, game, true);
    enableShutdown();
    t = OSGetSystemTime() - t;
    addEntropy(&t, sizeof(OSTime));

    // MCP thread finished. Let's see if we got any error - TODO: This is a 1:1 copy&paste from WUP Installer GX2 which itself stole it from WUP Installer Y mod which got it from WUP Installer minor edit by Nexocube who got it from WUP installer JHBL Version by Dimrok who portet it from the ASM of WUP Installer. So I think it's time for something new... ^^
    if(data.err != 0)
    {
        if(keepFiles && noIntro != NULL)
            revertNoIntro(noIntro);

        debugPrintf("Installation failed with result: %#010x", data.err);
        strcpy(toScreen, localise("Installation failed!"));
        strcat(toScreen, "\n\n");
        switch(data.err)
        {
            case CUSTOM_MCP_ERROR_CANCELLED:
                cleanupCancelledInstallation(dev, path, toUsb, keepFiles);
                // The fallthrough here is by design, don't listen to the compiler!
            case CUSTOM_MCP_ERROR_EOM:
                return true;
            case 0xFFFCFFE9:
                if(hasDeps)
                {
                    strcat(toScreen, "Install the main game to the same storage medium first");
                    if(toUsb)
                    {
                        strcat(toScreen, "\n");
                        strcat(toScreen, localise("Also make sure there is no error with the USB drive"));
                    }
                }
                else if(toUsb)
                    strcat(toScreen, localise("Possible USB error"));
                break;
            case 0xFFFBF446:
            case 0xFFFBF43F:
                strcat(toScreen, localise("Possible missing or bad title.tik file"));
                break;
            case 0xFFFBF440:
                strcat(toScreen, localise("Missing title.cert file"));
                break;
            case 0xFFFBF441:
                strcat(toScreen, localise("Possible incorrect console for DLC title.tik file"));
                break;
            case 0xFFFBF442:
                strcat(toScreen, localise("Invalid title.cert file"));
                break;
            case 0xFFFCFFE4:
                strcat(toScreen, localise("Not enough free space on target device"));
                break;
            case 0xFFFFF825:
            case 0xFFFFF82E:
                strcat(toScreen, localise("Files might be corrupt or bad storage medium.\nTry redownloading files or reformat/replace target device"));
                break;
            default:
                if((data.err & 0xFFFF0000) == 0xFFFB0000)
                {
                    if(dev & NUSDEV_USB)
                    {
                        strcat(toScreen, localise("Possible USB failure. Check your drives power source."));
                        strcat(toScreen, "\n");
                    }

                    strcat(toScreen, localise("Files might be corrupt"));
                }
                else
                    sprintf(toScreen + strlen(toScreen), "%s: %#010x", localise("Unknown Error"), data.err);
        }

        addToScreenLog("Installation failed!");
        showErrorFrame(toScreen);
        return false;
    }

    claimSpace(toUsb ? getUSB() : NUSDEV_MLC, size);

    if(keepFiles && noIntro != NULL)
        revertNoIntro(noIntro);

    addToScreenLog("Installation finished!");

    if(!keepFiles && dev == NUSDEV_SD)
    {
#ifdef NUSSPLI_DEBUG
        debugPrintf("Removing installation files...");
        FSError ret =
#endif
            removeDirectory(path);
#ifdef NUSSPLI_DEBUG
        if(ret != FS_ERROR_OK)
            debugPrintf("Couldn't remove installation files from SD card: %s", translateFSErr(ret));
#endif
    }

    return true;
}
