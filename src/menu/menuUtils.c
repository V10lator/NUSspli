/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
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

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <coreinit/filesystem.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>

#include <file.h>
#include <input.h>
#include <list.h>
#include <localisation.h>
#include <menu/utils.h>
#include <notifications.h>
#include <renderer.h>
#include <state.h>
#include <stdio.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

static LIST *logList = NULL;

void addToScreenLog(const char *str, ...)
{
    if(logList == NULL)
    {
        logList = createList();
        if(logList == NULL)
            return;
    }

    char *line;
    if(getListSize(logList) == MAX_LINES - 2)
        line = wrapFirstEntry(logList);
    else
    {
        line = MEMAllocFromDefaultHeap(MAX_CHARS + 2);
        if(line == NULL)
            return;

        if(!addToListEnd(logList, line))
        {
            MEMFreeToDefaultHeap(line);
            return;
        }
    }

    va_list va;
    va_start(va, str);
    vsnprintf(line, MAX_CHARS + 2, str, va);
    va_end(va);

    debugPrintf(line);
}

void clearScreenLog()
{
    destroyList(logList, true);
    logList = NULL;
}

void writeScreenLog(int line)
{
    lineToFrame(line, SCREEN_COLOR_WHITE);
    if(logList == NULL)
        return;

    char *text;
    int i = line;
    forEachListEntry(logList, text)
    {
        if(i == 1)
            textToFrame(++line, 0, text);
        else
            --i;
    }
}

void drawErrorFrame(const char *text, ErrorOptions option)
{
    colorStartNewFrame(SCREEN_COLOR_RED);

    char *l;
    size_t size;
    int line = -1;
    while(text)
    {
        l = strchr(text, '\n');
        ++line;
        size = l == NULL ? strlen(text) : (l - text);
        if(size > 0)
        {
            char tmp[size + 1];
            for(int i = 0; i < size; ++i)
                tmp[i] = text[i];

            tmp[size] = '\0';
            textToFrame(line, 0, tmp);
        }

        text = l == NULL ? NULL : (l + 1);
    }

    line = MAX_LINES;

    if(option == ANY_RETURN)
        textToFrame(--line, 0, gettext("Press any key to return"));
    else
    {
        if(option & B_RETURN)
            textToFrame(--line, 0, gettext("Press " BUTTON_B " to return"));

        if(option & Y_RETRY)
            textToFrame(--line, 0, gettext("Press " BUTTON_Y " to retry"));

        if(option & A_CONTINUE)
            textToFrame(--line, 0, gettext("Press " BUTTON_A " to continue"));
    }

    lineToFrame(--line, SCREEN_COLOR_WHITE);
    textToFrame(--line, 0, "NUSspli v" NUSSPLI_VERSION);
    drawFrame();
}

void showErrorFrame(const char *text)
{
    drawErrorFrame(text, ANY_RETURN);

    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            drawErrorFrame(text, ANY_RETURN);

        showFrame();

        if(vpad.trigger)
            break;
    }
}

bool checkSystemTitle(uint64_t tid, MCPRegion region)
{
    switch(getTidHighFromTid(tid))
    {
        case TID_HIGH_SYSTEM_APP:
        case TID_HIGH_SYSTEM_DATA:
        case TID_HIGH_SYSTEM_APPLET:
            break;
        default:
            return true;
    }

    MCPSysProdSettings settings __attribute__((__aligned__(0x40)));
    MCPError err = MCP_GetSysProdSettings(mcpHandle, &settings);
    if(err)
    {
        debugPrintf("Error reading settings: %d!", err);
        settings.game_region = 0;
    }

    debugPrintf("Console region: 0x%08X", settings.game_region);
    debugPrintf("Title region: 0x%08X", region);
    switch(settings.game_region)
    {
        case MCP_REGION_EUROPE:
            if(region & MCP_REGION_EUROPE)
                return true;
            break;
        case MCP_REGION_USA:
            if(region & MCP_REGION_USA)
                return true;
            break;
        case MCP_REGION_JAPAN:
            if(region & MCP_REGION_JAPAN)
                return true;
            break;
        default:
            // TODO: MCP_REGION_CHINA, MCP_REGION_KOREA, MCP_REGION_TAIWAN
            debugPrintf("Unknwon region: %d", settings.game_region);
            return true;
    }

    char *toFrame = getToFrameBuffer();
    sprintf(toFrame,
        "%s\n\n" BUTTON_A " %s || " BUTTON_B "%s",
        gettext("This is a reliable way to brick your console!\nAre you sure you want to do that?"),
        gettext("Yes"),
        gettext("No"));

    void *ovl = addErrorOverlay(toFrame);
    if(ovl == NULL)
        return false;

    bool ret = true;
    while(AppRunning(true))
    {
        showFrame();

        if(vpad.trigger & VPAD_BUTTON_A)
            break;
        if(vpad.trigger & VPAD_BUTTON_B)
        {
            ret = false;
            break;
        }
    }

    removeErrorOverlay(ovl);
    if(ret)
    {
        sprintf(toFrame,
            "%s\n\n" BUTTON_A " %s || " BUTTON_B "%s",
            gettext("Are you really sure you want to brick your Wii U?"),
            gettext("Yes"),
            gettext("No"));

        ovl = addErrorOverlay(toFrame);
        if(ovl == NULL)
            return false;

        while(AppRunning(true))
        {
            showFrame();

            if(vpad.trigger & VPAD_BUTTON_A)
                break;
            if(vpad.trigger & VPAD_BUTTON_B)
            {
                ret = false;
                break;
            }
        }
        removeErrorOverlay(ovl);
    }

    if(ret)
    {
        sprintf(toFrame,
            "%s\n\n" BUTTON_A " %s || " BUTTON_B "%s",
            gettext("You're on your own doing this,\ndo you understand the consequences?"),
            gettext("Yes"),
            gettext("No"));

        ovl = addErrorOverlay(toFrame);
        if(ovl == NULL)
            return false;

        while(AppRunning(true))
        {
            showFrame();

            if(vpad.trigger & VPAD_BUTTON_A)
                break;
            if(vpad.trigger & VPAD_BUTTON_B)
            {
                ret = false;
                break;
            }
        }
        removeErrorOverlay(ovl);
    }

    return ret;
}

bool checkSystemTitleFromEntry(const TitleEntry *entry)
{
    return checkSystemTitle(entry->tid, entry->region);
}

bool checkSystemTitleFromTid(uint64_t tid)
{
    const TitleEntry *entry = getTitleEntryByTid(tid);
    return entry == NULL ? true : checkSystemTitle(tid, entry->region);
}

bool checkSystemTitleFromListType(MCPTitleListType *entry)
{
    const TitleEntry *e = getTitleEntryByTid(entry->titleId);
    return e == NULL ? true : checkSystemTitle(entry->titleId, e->region);
}

const char *prettyDir(const char *dir)
{
    static char ret[FS_MAX_PATH];

    if(strncmp(NUSDIR_USB1, dir, strlen(NUSDIR_USB1)) == 0 || strncmp(NUSDIR_USB2, dir, strlen(NUSDIR_USB2)) == 0)
    {
        dir += strlen(NUSDIR_USB1);
        strcpy(ret, "USB:/");
    }
    else if(strncmp(NUSDIR_SD, dir, strlen(NUSDIR_SD)) == 0)
    {
        dir += strlen(NUSDIR_SD);
        strcpy(ret, "SD:/");
    }
    else if(strncmp(NUSDIR_MLC, dir, strlen(NUSDIR_MLC)) == 0)
    {
        dir += strlen(NUSDIR_MLC);
        strcpy(ret, "NAND:/");
    }
    else
        return dir;

    strcat(ret, dir);
    return ret;
}

static inline void drawFinishedScreen(const char *titleName, const char *text, FINISHING_OPERATION op)
{
    colorStartNewFrame(SCREEN_COLOR_D_GREEN);
    int i = 0;
    if(op != FINISHING_OPERATION_QUEUE)
        textToFrame(i++, 0, titleName);

    textToFrame(i++, 0, text);
    writeScreenLog(i++);
    drawFrame();
}

void showFinishedScreen(const char *titleName, FINISHING_OPERATION op)
{
    const char *text;
    switch(op)
    {
        case FINISHING_OPERATION_INSTALL:
            text = gettext("Installed successfully!");
            break;
        case FINISHING_OPERATION_DEINSTALL:
            text = gettext("Uninstalled successfully!");
            break;
        case FINISHING_OPERATION_DOWNLOAD:
            text = gettext("Downloaded successfully!");
            break;
        case FINISHING_OPERATION_QUEUE:
            text = gettext("Queue finished successfully!");
            break;
    }

    drawFinishedScreen(titleName, text, op);
    startNotification();

    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            drawFinishedScreen(titleName, text, op);

        showFrame();

        if(vpad.trigger)
            break;
    }

    stopNotification();
}

void showNoSpaceOverlay(NUSDEV dev)
{
    const char *nd;
    switch((int)dev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
        case NUSDEV_USB:
            nd = "USB";
            break;
        case NUSDEV_SD:
            nd = "SD";
            break;
        case NUSDEV_MLC:
            nd = "MLC";
    }

    char *toFrame = getToFrameBuffer();
    sprintf(toFrame, "%s  %s\n\n%s", gettext("Not enough free space on"), nd, gettext("Press any key to return"));

    void *ovl = addErrorOverlay(toFrame);
    if(ovl != NULL)
    {
        while(AppRunning(true))
        {
            showFrame();

            if(vpad.trigger)
                break;
        }

        removeErrorOverlay(ovl);
    }
}
