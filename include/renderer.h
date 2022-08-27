/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2021 V10lator <v10lator@myway.de>                    *
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

#pragma once

#include <wut-fixups.h>

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#include <staticMem.h>
#include <titles.h>

#include <coreinit/mcp.h>

#define SCREEN_WIDTH         1280
#define SCREEN_HEIGHT        720

#define FONT_SIZE            28

#define SCREEN_COLOR_BG1     0x911EFFFF
#define SCREEN_COLOR_BG2     0x8318FFFF
#define SCREEN_COLOR_BG3     0x610AFFFF
#define SCREEN_COLOR_BG4     0x5205FFFF

#define SCREEN_COLOR_BLACK   0x000000FF
#define SCREEN_COLOR_WHITE   0xFFFFFFFF
#define SCREEN_COLOR_D_RED   0x800000FF
#define SCREEN_COLOR_RED     0xFF0000FF
#define SCREEN_COLOR_D_GREEN 0x008000FF
#define SCREEN_COLOR_GREEN   0x00FF00FF
#define SCREEN_COLOR_D_BLUE  0x005780FF
#define SCREEN_COLOR_BLUE    0x00D6FFFF
#define SCREEN_COLOR_LILA    0xFF00FFFF
#define SCREEN_COLOR_BROWN   0x361C0AFF
#define SCREEN_COLOR_GRAY    0x6A6A6AFF

// These are with Nintendo font at size 24:
#define MAX_CHARS            124 // TODO: This is here for historical reasons and only valid for spaces now
#define MAX_LINES            24

#define ALIGNED_RIGHT        MAX_CHARS
#define ALIGNED_CENTER       (MAX_CHARS + 1)

#define FRAMERATE_60FPS      1
#define FRAMERATE_30FPS      2

#define TO_FRAME_BUFFER_SIZE (1024 * 1024)

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        DEVICE_TYPE_UNKNOWN = 0,
        DEVICE_TYPE_USB = 1,
        DEVICE_TYPE_NAND = 2
    } DEVICE_TYPE;

    bool initRenderer();
    void shutdownRenderer() __attribute__((__cold__));
    void pauseRenderer();
    void resumeRenderer();
    void colorStartNewFrame(uint32_t color);
    void showFrame() __attribute__((__hot__));
    void drawFrame();
    void drawKeyboard(bool tv);
    void textToFrameCut(int line, int column, const char *str, int maxWidth) __attribute__((__hot__));
    int textToFrameMultiline(int x, int y, const char *text, size_t len);
    void lineToFrame(int column, uint32_t color);
    void boxToFrame(int lineStart, int lineEnd);
    void barToFrame(int line, int column, uint32_t width, double progress);
    void arrowToFrame(int line, int column);
    void checkmarkToFrame(int line, int column);
    void tabToFrame(int line, int column, const char *label, bool active);
    void flagToFrame(int line, int column, MCPRegion flag);
    void deviceToFrame(int line, int column, DEVICE_TYPE dev);
    int addErrorOverlay(const char *err);
    void removeErrorOverlay(int id);
    uint32_t getSpaceWidth();

#ifdef __cplusplus
}
#endif

#define startNewFrame()                colorStartNewFrame(SCREEN_COLOR_BLUE)
#define textToFrame(line, column, str) textToFrameCut(line, column, str, column == 0 ? SCREEN_WIDTH - (FONT_SIZE * 2) : 0)
#define getToFrameBuffer()             getStaticScreenBuffer()
