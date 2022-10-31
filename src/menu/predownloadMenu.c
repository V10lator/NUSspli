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

#ifndef NUSSPLI_LITE

#include <wut-fixups.h>

#include <config.h>
#include <deinstaller.h>
#include <downloader.h>
#include <filesystem.h>
#include <input.h>
#include <menu/predownload.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <state.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <coreinit/filesystem.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>

#define PD_MENU_ENTRIES 5

static int cursorPos = 15;
static OPERATION operation = OPERATION_DOWNLOAD_INSTALL;
static bool keepFiles = true;

static inline bool isInstalled(const TitleEntry *entry, MCPTitleListType *out)
{
    if(out == NULL)
    {
        MCPTitleListType titleList __attribute__((__aligned__(0x40)));
        return MCP_GetTitleInfo(mcpHandle, entry->tid, &titleList) == 0;
    }
    return MCP_GetTitleInfo(mcpHandle, entry->tid, out) == 0;
}

static void drawPDMenuFrame(const TitleEntry *entry, const char *titleVer, uint64_t size, bool installed, const char *folderName, bool usbMounted, NUSDEV dlDev, NUSDEV instDev)
{
    startNewFrame();

    textToFrame(0, 0, gettext("Name:"));

    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, entry->name);
    char tid[17];
    hex(entry->tid, 16, tid);
    strcat(toFrame, " [");
    strcat(toFrame, tid);
    strcat(toFrame, "]");
    int line = textToFrameMultiline(0, ALIGNED_CENTER, toFrame, MAX_CHARS - 33); // TODO

    char *bs;
    float fsize;
    if(size > 1024 * 1024 * 1024)
    {
        bs = "GB";
        fsize = ((float)(size / 1024 / 1024)) / 1024.0F;
    }
    else if(size > 1024 * 1924)
    {
        bs = "MB";
        fsize = ((float)(size / 1024)) / 1024.0F;
    }
    else if(size > 1024)
    {
        bs = "KB";
        fsize = ((float)size) / 1024.0F;
    }
    else
    {
        bs = "B";
        fsize = (float)size;
    }

    sprintf(toFrame, "%.02f %s", fsize, bs);

    textToFrame(++line, 0, gettext("Region:"));
    flagToFrame(++line, 3, entry->region);
    textToFrame(line, 7, gettext(getFormattedRegion(entry->region)));

    textToFrame(++line, 0, gettext("Size:"));
    textToFrame(++line, 3, toFrame);

    strcpy(toFrame, gettext("Provided title version"));
    strcat(toFrame, " [");
    strcat(toFrame, gettext("Only numbers"));
    strcat(toFrame, "]:");
    textToFrame(++line, 0, toFrame);

    if(titleVer[0] == '\0')
    {
        toFrame[0] = '<';
        strcpy(toFrame + 1, gettext("LATEST"));
        strcat(toFrame, ">");
        textToFrame(++line, 3, toFrame);
    }
    else
        textToFrame(++line, 3, titleVer);

    strcpy(toFrame, gettext("Custom folder name"));
    strcat(toFrame, " [");
    strcat(toFrame, gettext("ASCII only"));
    strcat(toFrame, "]:");
    textToFrame(++line, 0, toFrame);
    textToFrame(++line, 3, folderName);

    line = MAX_LINES;

    arrowToFrame(cursorPos, 0);

    strcpy(toFrame, gettext(BUTTON_MINUS " to add to the queue"));
    if(installed)
    {
        strcat(toFrame, " || ");
        strcat(toFrame, gettext(BUTTON_Y " to uninstall"));
    }
    textToFrame(--line, ALIGNED_CENTER, toFrame);

    strcpy(toFrame, gettext("Press " BUTTON_B " to return"));
    strcat(toFrame, " || ");
    strcat(toFrame, gettext(BUTTON_PLUS " to start"));
    textToFrame(--line, ALIGNED_CENTER, toFrame);

    lineToFrame(--line, SCREEN_COLOR_WHITE);

    textToFrame(--line, 4, gettext("Set custom name to the download folder"));
    textToFrame(--line, 4, gettext("Set title version"));

    if(operation == OPERATION_DOWNLOAD)
        keepFiles = true;
    else
    {
        switch((int)dlDev)
        {
            case NUSDEV_USB01:
            case NUSDEV_USB02:
            case NUSDEV_MLC:
                keepFiles = false;
        }
    }

    strcpy(toFrame, gettext("Keep downloaded files:"));
    strcat(toFrame, " ");
    strcat(toFrame, gettext(keepFiles ? "Yes" : "No"));
    if(dlDev == NUSDEV_SD && operation == OPERATION_DOWNLOAD_INSTALL)
        textToFrame(--line, 4, gettext(toFrame));
    else
        textToFrameColored(--line, 4, gettext(toFrame), SCREEN_COLOR_WHITE_TRANSP);

    strcpy(toFrame, gettext("Download to:"));
    strcat(toFrame, " ");
    switch((int)dlDev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            strcat(toFrame, "USB");
            break;
        case NUSDEV_SD:
            strcat(toFrame, "SD");
            break;
        case NUSDEV_MLC:
            strcat(toFrame, "NAND");
    }
    textToFrame(--line, 4, gettext(toFrame));

    strcpy(toFrame, gettext("Operation:"));
    strcat(toFrame, " ");
    switch((int)operation)
    {
        case OPERATION_DOWNLOAD:
            strcat(toFrame, gettext("Download only"));
            break;
        case OPERATION_DOWNLOAD_INSTALL:
            strcat(toFrame, gettext("Install"));
            break;
    }
    textToFrame(--line, 4, toFrame);

    strcpy(toFrame, gettext("Install to:"));
    strcat(toFrame, " ");
    switch((int)instDev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            strcat(toFrame, "USB");
            break;
        case NUSDEV_MLC:
            strcat(toFrame, "NAND");
            break;
    }
    if(operation == OPERATION_DOWNLOAD_INSTALL)
        textToFrame(--line, 4, toFrame);
    else
        textToFrameColored(--line, 4, toFrame, SCREEN_COLOR_WHITE_TRANSP);

    lineToFrame(--line, SCREEN_COLOR_WHITE);

    drawFrame();
}

static void *drawPDWrongDeviceFrame(NUSDEV dev)
{
    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, gettext("The main game is installed to"));
    strcat(toFrame, " ");
    strcat(toFrame, dev & NUSDEV_USB ? "USB" : "NAND");
    strcat(toFrame, "\n");
    strcat(toFrame, "Do you want to change the target device to this?");
    strcat(toFrame, "\n\n" BUTTON_A " ");
    strcat(toFrame, gettext("Yes"));
    strcat(toFrame, " || " BUTTON_B " ");
    strcat(toFrame, gettext("No"));

    return addErrorOverlay(toFrame);
}

static void *drawPDMainGameFrame(const TitleEntry *entry)
{
    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, entry->name);
    strcat(toFrame, "\n");
    strcat(toFrame, gettext(isDLC(entry->tid) ? "is DLC." : (isUpdate(entry->tid) ? "is a update." : "is a demo.")));
    strcat(toFrame, "\n\n" BUTTON_A " ");
    strcat(toFrame, gettext(operation == OPERATION_DOWNLOAD_INSTALL ? "Install main game" : "Download main game"));
    strcat(toFrame, " || " BUTTON_B " ");
    strcat(toFrame, gettext("Continue"));

    return addErrorOverlay(toFrame);
}

static void *drawPDUpdateFrame(const TitleEntry *entry)
{
    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, entry->name);
    strcat(toFrame, "\n");
    strcat(toFrame, gettext("Has an update available."));
    strcat(toFrame, "\n\n" BUTTON_A " ");
    strcat(toFrame, gettext(operation == OPERATION_DOWNLOAD_INSTALL ? "Install the update, too" : "Download the update, too"));
    strcat(toFrame, " || " BUTTON_B " ");
    strcat(toFrame, gettext("Continue"));

    return addErrorOverlay(toFrame);
}

static inline void changeTitleVersion(char *buf)
{
    if(!showKeyboard(KEYBOARD_LAYOUT_TID, KEYBOARD_TYPE_RESTRICTED, buf, CHECK_NUMERICAL, 5, false, buf, NULL))
        buf[0] = '\0';
}

static inline void changeFolderName(char *buf)
{
    if(!showKeyboard(KEYBOARD_LAYOUT_TID, KEYBOARD_TYPE_NORMAL, buf, CHECK_ALPHANUMERICAL, FS_MAX_PATH - 11, false, buf, NULL))
        buf[0] = '\0';
}

static inline void switchInstallDevice(NUSDEV *dev)
{
    switch((int)*dev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            *dev = NUSDEV_MLC;
            break;
        case NUSDEV_MLC:
            *dev = getUSB();
            if(!*dev)
                *dev = NUSDEV_MLC;
            break;
    }
}

static inline void switchOperation()
{
    switch((int)operation)
    {
        case OPERATION_DOWNLOAD_INSTALL:
            operation = OPERATION_DOWNLOAD;
            break;
        case OPERATION_DOWNLOAD:
            operation = OPERATION_DOWNLOAD_INSTALL;
            break;
    }
}

static inline void switchDownloadDevice(NUSDEV *dev)
{
    bool toUSB = false;

    if(vpad.trigger & VPAD_BUTTON_LEFT)
    {
        switch((int)*dev)
        {
            case NUSDEV_USB01:
            case NUSDEV_USB02:
                *dev = NUSDEV_MLC;
                break;
            case NUSDEV_MLC:
                *dev = NUSDEV_SD;
                break;
            case NUSDEV_SD:
                *dev = getUSB();
                if(!*dev)
                    *dev = NUSDEV_MLC;
                else
                    toUSB = true;
                break;
        }
    }
    else
    {
        switch((int)*dev)
        {
            case NUSDEV_USB01:
            case NUSDEV_USB02:
                *dev = NUSDEV_SD;
                break;
            case NUSDEV_SD:
                *dev = NUSDEV_MLC;
                break;
            case NUSDEV_MLC:
                *dev = getUSB();
                if(!*dev)
                    *dev = NUSDEV_SD;
                else
                    toUSB = true;
                break;
        }
    }

    setDlToUSB(toUSB);
}

static bool addToOpQueue(const TitleEntry *entry, const char *titleVer, const char *folderName, NUSDEV dlDev, NUSDEV instDev)
{
    TitleData *titleInfo = MEMAllocFromDefaultHeap(sizeof(TitleData));
    int ret = false;
    if(titleInfo != NULL)
    {
        titleInfo->tmd = MEMAllocFromDefaultHeap(getRamBufSize());
        if(titleInfo->tmd != NULL)
        {
            OSBlockMove(titleInfo->tmd, getRamBuf(), getRamBufSize(), false);
            titleInfo->tmdSize = getRamBufSize();
            titleInfo->entry = entry;
            strcpy(titleInfo->titleVer, titleVer);
            strcpy(titleInfo->folderName, folderName);
            titleInfo->operation = operation;
            titleInfo->dlDev = dlDev;
            titleInfo->toUSB = instDev & NUSDEV_USB;
            titleInfo->keepFiles = keepFiles;

            ret = addToQueue(titleInfo);
            if(ret == 1)
                return true;

            MEMFreeToDefaultHeap(titleInfo->tmd);
        }

        MEMFreeToDefaultHeap(titleInfo);
    }

    return ret;
}

bool predownloadMenu(const TitleEntry *entry)
{
    MCPTitleListType titleList __attribute__((__aligned__(0x40)));
    bool installed = isInstalled(entry, &titleList);
    NUSDEV usbMounted = getUSB();
    NUSDEV dlDev = usbMounted && dlToUSBenabled() ? usbMounted : NUSDEV_SD;
    NUSDEV instDev = usbMounted ? usbMounted : NUSDEV_MLC;
    char folderName[FS_MAX_PATH - 11];
    char titleVer[33];
    folderName[0] = titleVer[0] = '\0';
    TMD *tmd;
    uint64_t dls;
    bool redraw;
    bool toQueue;
    char tid[17];
    char downloadUrl[256];
    bool autoAddToQueue = false;
    bool autoStartQueue = false;

downloadTMD:
    clearRamBuf();
    hex(entry->tid, 16, tid);

    debugPrintf("Downloading TMD...");
    strcpy(downloadUrl, DOWNLOAD_URL);
    strcat(downloadUrl, tid);
    strcat(downloadUrl, "/tmd");

    if(strlen(titleVer) > 0)
    {
        strcat(downloadUrl, ".");
        strcat(downloadUrl, titleVer);
    }

    if(downloadFile(downloadUrl, "title.tmd", NULL, (FileType)(FILE_TYPE_TMD | FILE_TYPE_TORAM), false))
    {
        clearRamBuf();
        debugPrintf("Error downloading TMD");
        saveConfig(false);
        return true;
    }

    tmd = (TMD *)getRamBuf();
    if(!verifyTmd(tmd, getRamBufSize()))
    {
        clearRamBuf();
        saveConfig(false);

        showErrorFrame(gettext("Invalid title.tmd file!"));
        return true;
    }

    dls = 0;
    for(uint16_t i = 0; i < tmd->num_contents; ++i)
    {
        if((tmd->contents[i].type & 0x0003) == 0x0003)
            dls += 20;

        dls += tmd->contents[i].size;
    }

naNedNa:
    toQueue = autoAddToQueue;
    if(!toQueue)
    {
        redraw = true;

        while(AppRunning(true))
        {
            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
                redraw = true;

            if(redraw)
            {
                drawPDMenuFrame(entry, titleVer, dls, installed, folderName, usbMounted, dlDev, instDev);
                redraw = false;
            }
            showFrame();

            if(vpad.trigger & VPAD_BUTTON_B)
            {
                clearRamBuf();
                saveConfig(false);
                return true;
            }

            if(vpad.trigger & (VPAD_BUTTON_RIGHT | VPAD_BUTTON_LEFT | VPAD_BUTTON_A))
            {
                switch(cursorPos)
                {
                    case 15: // TODO: Change hardcoded numbers to something prettier
                        if(operation == OPERATION_DOWNLOAD_INSTALL)
                            switchInstallDevice(&instDev);
                        break;
                    case 16:
                        switchOperation();
                        break;
                    case 17:
                        switchDownloadDevice(&dlDev);
                        break;
                    case 18:
                        if(dlDev == NUSDEV_SD && operation == OPERATION_DOWNLOAD_INSTALL)
                            keepFiles = !keepFiles;
                        break;
                    case 19:
                        changeTitleVersion(titleVer);
                        goto downloadTMD;
                    case 20:
                        changeFolderName(folderName);
                        break;
                }

                redraw = true;
            }
            else if(vpad.trigger & VPAD_BUTTON_DOWN)
            {
                if(++cursorPos == 21) // TODO: Change hardcoded numbers to something prettier
                    cursorPos = 15;

                redraw = true;
            }
            else if(vpad.trigger & VPAD_BUTTON_UP)
            {
                if(--cursorPos == 14) // TODO: Change hardcoded numbers to something prettier
                    cursorPos = 20;

                redraw = true;
            }

            if(vpad.trigger & VPAD_BUTTON_PLUS)
                break;

            if(vpad.trigger & VPAD_BUTTON_MINUS)
            {
                toQueue = true;
                break;
            }

            if(installed && vpad.trigger & VPAD_BUTTON_Y)
            {
                clearRamBuf();
                saveConfig(false);
                deinstall(&titleList, entry->name, false, false);
                return false;
            }
        }
    }

    bool ret = false;
    if(!AppRunning(true))
        goto exitPDM;

    if(!autoAddToQueue)
    {
        if(dlDev == NUSDEV_MLC)
        {
            void *ovl = addErrorOverlay(gettext(
                "Downloading to NAND is dangerous,\n"
                "it could brick your Wii U!\n\n"

                "Are you sure you want to do this?"));

            if(ovl == NULL)
                goto exitPDM;

            while(AppRunning(true))
            {
                if(app == APP_STATE_BACKGROUND)
                    continue;

                showFrame();

                if(vpad.trigger & VPAD_BUTTON_B)
                {
                    removeErrorOverlay(ovl);
                    goto naNedNa;
                }
                if(vpad.trigger & VPAD_BUTTON_A)
                    break;
            }

            removeErrorOverlay(ovl);
            if(!AppRunning(true))
                goto exitPDM;
        }

        if(isDemo(entry->tid))
        {
            uint64_t t = entry->tid;
            t &= 0xFFFFFFF0FFFFFFF0; // TODO
            const TitleEntry *te = getTitleEntryByTid(t);
            if(te != NULL && te->key != TITKE_KEY_MAGIC)
            {
                void *ovl = drawPDMainGameFrame(entry);
                if(ovl == NULL)
                    goto exitPDM;

                while(AppRunning(true))
                {
                    if(app == APP_STATE_BACKGROUND)
                        continue;

                    showFrame();

                    if(vpad.trigger & VPAD_BUTTON_B)
                        break;
                    if(vpad.trigger & VPAD_BUTTON_A)
                    {
                        removeErrorOverlay(ovl);
                        clearRamBuf();
                        entry = te;
                        goto downloadTMD;
                    }
                }

                removeErrorOverlay(ovl);
                if(!AppRunning(true))
                    goto exitPDM;
            }
        }
        else if(isDLC(entry->tid) || isUpdate(entry->tid))
        {
            MCPTitleListType tl __attribute__((__aligned__(0x40)));
            uint64_t t = entry->tid & 0xFFFFFFF0FFFFFFFF;
            if(MCP_GetTitleInfo(mcpHandle, t, &tl) == 0)
            {
                if(operation == OPERATION_DOWNLOAD_INSTALL)
                {
                    NUSDEV toDev = tl.indexedDevice[0] == 'u' ? NUSDEV_USB : NUSDEV_MLC;
                    if(!(toDev & instDev))
                    {
                        void *ovl = drawPDWrongDeviceFrame(toDev);
                        if(ovl == NULL)
                            goto exitPDM;

                        while(AppRunning(true))
                        {
                            if(app == APP_STATE_BACKGROUND)
                                continue;

                            showFrame();

                            if(vpad.trigger & VPAD_BUTTON_B)
                                break;
                            if(vpad.trigger & VPAD_BUTTON_A)
                            {
                                instDev = toDev;
                                if(instDev == NUSDEV_USB)
                                    instDev = usbMounted;

                                break;
                            }
                        }

                        removeErrorOverlay(ovl);
                        if(!AppRunning(true))
                            goto exitPDM;
                    }
                }
            }
            else // main game not installed
            {
                const TitleEntry *te = getTitleEntryByTid(t);
                if(te != NULL && te->key != TITKE_KEY_MAGIC)
                {
                    void *ovl = drawPDMainGameFrame(entry);
                    if(ovl == NULL)
                        goto exitPDM;

                    while(AppRunning(true))
                    {
                        if(app == APP_STATE_BACKGROUND)
                            continue;

                        showFrame();

                        if(vpad.trigger & VPAD_BUTTON_B)
                            break;
                        if(vpad.trigger & VPAD_BUTTON_A)
                        {
                            removeErrorOverlay(ovl);

                            if(!checkFreeSpace(dlDev, dls))
                            {
                                if(AppRunning(true))
                                    goto naNedNa;

                                goto exitPDM;
                            }

                            if(!addToOpQueue(entry, titleVer, folderName, dlDev, instDev))
                                goto exitPDM;

                            clearRamBuf();
                            entry = te;
                            autoAddToQueue = true;

                            if(!toQueue)
                                autoStartQueue = true;

                            goto downloadTMD;
                        }
                    }

                    removeErrorOverlay(ovl);
                    if(!AppRunning(true))
                        goto exitPDM;
                }
            }
        }
        else if(isGame(entry->tid))
        {
            uint64_t t = entry->tid | 0x0000000E00000000;
            const TitleEntry *te = getTitleEntryByTid(t);
            if(te != NULL) // Update available
            {
                MCPTitleListType tl __attribute__((__aligned__(0x40)));
                if(MCP_GetTitleInfo(mcpHandle, t, &tl) != 0) // Update not installed
                {
                    void *ovl = drawPDUpdateFrame(entry);
                    if(ovl == NULL)
                        goto exitPDM;

                    while(AppRunning(true))
                    {
                        if(app == APP_STATE_BACKGROUND)
                            continue;

                        showFrame();

                        if(vpad.trigger & VPAD_BUTTON_B)
                            break;
                        if(vpad.trigger & VPAD_BUTTON_A)
                        {
                            removeErrorOverlay(ovl);

                            if(!checkFreeSpace(dlDev, dls))
                            {
                                if(AppRunning(true))
                                    goto naNedNa;

                                goto exitPDM;
                            }

                            if(!addToOpQueue(entry, titleVer, folderName, dlDev, instDev))
                                goto exitPDM;

                            clearRamBuf();
                            entry = te;
                            autoAddToQueue = true;

                            if(!toQueue)
                                autoStartQueue = true;

                            goto downloadTMD;
                        }
                    }

                    removeErrorOverlay(ovl);
                    if(!AppRunning(true))
                        goto exitPDM;
                }
            }
        }
    }

    startNewFrame();
    textToFrame(0, 0, gettext("Preparing the download of"));
    textToFrame(1, 3, entry->name);
    writeScreenLog(2);
    drawFrame();
    showFrame();

    saveConfig(false);

    if(!checkFreeSpace(dlDev, dls))
    {
        if(AppRunning(true))
            goto naNedNa;

        goto exitPDM;
    }

    if(!AppRunning(true))
        goto exitPDM;

    if(toQueue)
    {
        ret = addToOpQueue(entry, titleVer, folderName, dlDev, instDev);
        if(ret && autoStartQueue)
            ret = !proccessQueue();
    }
    else if(checkSystemTitleFromEntry(entry))
    {
        disableApd();
        ret = !downloadTitle(tmd, getRamBufSize(), entry, titleVer, folderName, operation == OPERATION_DOWNLOAD_INSTALL, dlDev, instDev & NUSDEV_USB, keepFiles);
        if(!ret)
            showFinishedScreen(entry->name, operation == OPERATION_DOWNLOAD_INSTALL ? FINISHING_OPERATION_INSTALL : FINISHING_OPERATION_DOWNLOAD);

        enableApd();
    }
    else
        ret = true;

exitPDM:
    clearRamBuf();
    return ret;
}

#endif // ifndef NUSSPLI_LITE
