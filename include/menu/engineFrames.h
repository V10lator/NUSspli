/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2026 V10lator <v10lator@myway.de>                         *
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

#pragma once

#include <wut-fixups.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <downloader.h>
#include <menu/utils.h>
#include <queue.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * The frames and dialogs the engine used to draw itself.
     *
     * Everything in here lives on the UI side of the boundary: the engine
     * calls these helpers, or hands a view to uiPump() so the callback
     * below fills the frame, but it never touches renderer.h itself.
     */

    // One-shot status frame: title line, optional second line and progress
    // bar, optional screen log, presented like the old inline blocks.
    // logLine below zero leaves the screen log out of the frame.
    void showStatusFrame(const char *line0, const char *line1, bool bar, int logLine);

    // Frame callbacks for uiPump(): they fill the frame, uiPump presents it.
    typedef struct McpProgressView
    {
        const char *game;
        bool inst;
        uint64_t sizeProgress;
        uint64_t sizeTotal;
        float ratio;
        const char *speed;
    } McpProgressView;
    void drawMcpProgressFrame(void *ctx);

    typedef struct DLProgressView
    {
        downloadData *data; // May be NULL for downloads without title info
        QUEUE_DATA *queueData; // May be NULL
        const char *name;
        size_t dlnow;
        size_t dltotal; // 0 means preparing, the frame then shows the preparing text
        float bps;
        uint32_t fileEta;
        bool preparing;
    } DLProgressView;
    void drawDownloadProgressFrame(void *ctx);

    // Blocking dialogs, converted from the old while(AppRunning) loops.
    // true means retry (Y press or countdown expiry), false means return
    // (B press or the app stopping).
    bool showNetworkError(const char *err);

    // A_PRESS continues with A_CONTINUE, B browses again with B_RETURN and
    // 0 means the app stopped while the dialog was up.
    ErrorOptions showTicketConfirmDialog(uint64_t titleID);
    void showTicketGeneratedDialog(const char *dir);

#ifdef __cplusplus
}
#endif
