/***************************************************************************
 * This file is part of NUSspli.                                           *
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

#include <gx2/enum.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_surface.h>
#include <SDL_FontCache.h>

#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/event.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include <crypto.h>
#include <file.h>
#include <input.h>
#include <menu/utils.h>
#include <osdefs.h>
#include <renderer.h>
#include <romfs.h>
#include <staticMem.h>
#include <swkbd_wrapper.h>
#include <utils.h>

#define SSAA         8
#define MAX_OVERLAYS 8
#define SDL_RECTS    512

typedef struct
{
    SDL_Texture *tex;
    SDL_Rect rect[2];
} ErrorOverlay;

struct SDL_Rect_Entry;
typedef struct SDL_Rect_Entry SDL_Rect_Entry;
struct SDL_Rect_Entry
{
    SDL_Rect rect;
    SDL_Rect_Entry *next;
};

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static FC_Font *font = NULL;
static void *bgmBuffer = NULL;
static Mix_Chunk *backgroundMusic = NULL;

static int32_t spaceWidth;

static const SDL_Rect barRect = {
    .x = 1,
    .y = 0,
    .w = 1,
    .h = 1,
};

static SDL_Texture *frameBuffer;
static ErrorOverlay errorOverlay[MAX_OVERLAYS];
static SDL_Texture *arrowTex;
static SDL_Texture *checkmarkTex;
static SDL_Texture *tabTex;
static SDL_Texture *flagTex[8];
static SDL_Texture *deviceTex[3];
static SDL_Texture *barTex;
static SDL_Texture *bgTex;
static SDL_Texture *byeTex;

static SDL_Rect_Entry *rectList = NULL;

#define screenColorToSDLcolor(color) \
    (SDL_Color) { .a = color & 0xFFu, .b = (color & 0x0000FF00u) >> 8, .g = (color & 0x00FF0000u) >> 16, .r = (color & 0xFF000000u) >> 24 }

static inline SDL_Rect *createRect()
{
    SDL_Rect_Entry *ret = MEMAllocFromDefaultHeap(sizeof(SDL_Rect_Entry));
    if(!ret)
        return NULL;

    ret->next = NULL;

    if(rectList == NULL)
        rectList = ret;
    else
    {
        SDL_Rect_Entry *cur = rectList;
        while(cur->next != NULL)
            cur = cur->next;

        cur->next = ret;
    }

    return &(ret->rect);
}

void textToFrameCut(int line, int column, const char *str, int maxWidth)
{
    if(font == NULL)
        return;

    ++line;
    line *= FONT_SIZE;
    line -= 7;
    SDL_Rect text = { .w = FC_GetWidth(font, str), .h = FONT_SIZE, .y = line };

    if(maxWidth != 0 && text.w > maxWidth)
    {
        int i = strlen(str);
        char *lineBuffer = (char *)getStaticLineBuffer();
        char *tmp = lineBuffer;
        OSBlockMove(tmp, str, i, false);
        tmp += i;

        *--tmp = '\0';
        *--tmp = '.';
        *--tmp = '.';
        *--tmp = '.';

        char *tmp2;
        text.w = FC_GetWidth(font, lineBuffer);
        while(text.w > maxWidth)
        {
            tmp2 = tmp;
            *--tmp = '.';
            ++tmp2;
            *++tmp2 = '\0';
            text.w = FC_GetWidth(font, lineBuffer);
        }

        if(*--tmp == ' ')
        {
            *tmp = '.';
            tmp[3] = '\0';
        }

        str = lineBuffer;
    }

    switch(column)
    {
    case ALIGNED_CENTER:
        text.x = (SCREEN_WIDTH >> 1) - (text.w >> 1);
        break;
    case ALIGNED_RIGHT:
        text.x = SCREEN_WIDTH - text.w - FONT_SIZE;
        break;
    default:
        column *= spaceWidth;
        text.x = column + FONT_SIZE;
    }

    FC_DrawBox(font, renderer, text, str);
}

int textToFrameMultiline(int x, int y, const char *text, size_t len)
{
    if(len < 1)
        return 0;

    uint16_t fl = FC_GetWidth(font, text) / spaceWidth;
    if(fl <= len)
    {
        textToFrame(x, y, text);
        return 1;
    }

    size_t l = strlen(text);
    char tmp[++l];
    OSBlockMove(tmp, text, l, false);

    int lines = 0;
    char *p = tmp;
    char *t;
    char o;

    while(fl > len)
    {
        l = strlen(p);
        for(char *i = p + l; i > p; --i, --l)
        {
            o = *i;
            *i = '\0';
            if(FC_GetWidth(font, p) / spaceWidth <= len)
            {
                t = strrchr(p, ' ');
                if(t != NULL)
                {
                    *t = '\0';
                    *i = o;
                }
                else
                    t = i;

                textToFrame(x, y, p);
                ++lines;
                ++x;
                p = ++t;
                break;
            }

            *i = o;
        }

        fl = FC_GetWidth(font, p) / spaceWidth;
    }

    textToFrame(x, y, p);
    return ++lines;
}

void lineToFrame(int column, uint32_t color)
{
    if(font == NULL)
        return;

    ++column;
    column *= FONT_SIZE;

    SDL_Rect *rect = createRect();
    if(rect == NULL)
        return;

    rect->x = FONT_SIZE;
    rect->y = column + ((FONT_SIZE >> 1) - 1);
    rect->w = SCREEN_WIDTH - (FONT_SIZE << 1);
    rect->h = 3;

    SDL_Color co = screenColorToSDLcolor(color);
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
    SDL_RenderFillRect(renderer, rect);
}

void boxToFrame(int lineStart, int lineEnd)
{
    if(font == NULL)
        return;

    SDL_Rect *rect[5];
    for(int i = 0; i < 5; i++)
    {
        rect[i] = createRect();
        if(rect[i] == NULL)
            return;
    }

    rect[0]->x = FONT_SIZE;
    rect[0]->y = ((++lineStart) * FONT_SIZE) + ((FONT_SIZE >> 1) - 1);
    rect[0]->w = SCREEN_WIDTH - (FONT_SIZE << 1);
    rect[0]->h = 3;

    SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_GRAY);
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);

    // Horizontal lines
    SDL_RenderFillRect(renderer, rect[0]);

    rect[1]->x = FONT_SIZE;
    rect[1]->y = ((++lineEnd) * FONT_SIZE) + ((FONT_SIZE >> 1) - 1) - 3;
    rect[1]->w = rect[0]->w;
    rect[1]->h = 3;

    SDL_RenderFillRect(renderer, rect[1]);

    // Vertical lines
    rect[2]->x = FONT_SIZE;
    rect[2]->y = rect[0]->y;
    rect[2]->w = 3;
    rect[2]->h = (--lineEnd - --lineStart) * FONT_SIZE;
    SDL_RenderFillRect(renderer, rect[2]);

    rect[3]->x = (FONT_SIZE - 3) + rect[0]->w;
    rect[3]->y = rect[0]->y;
    rect[3]->w = 3;
    rect[3]->h = rect[2]->h;
    SDL_RenderFillRect(renderer, rect[3]);

    // Background - we paint it on top of the gray lines as they look better that way
    rect[4]->x = FONT_SIZE + 2;
    rect[4]->y = rect[0]->y + 2;
    rect[4]->w = rect[0]->w - 3;
    rect[4]->h = rect[2]->h - 3;
    co = screenColorToSDLcolor(SCREEN_COLOR_BLACK);
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 64);
    SDL_RenderFillRect(renderer, rect[4]);
}

void barToFrame(int line, int column, uint32_t width, double progress)
{
    if(font == NULL)
        return;

    SDL_Rect *rect[3];
    for(int i = 0; i < 3; i++)
    {
        rect[i] = createRect();
        if(rect[i] == NULL)
            return;
    }

    rect[0]->x = FONT_SIZE + (column * spaceWidth);
    rect[0]->y = ((++line) * FONT_SIZE) - 2;
    rect[0]->w = ((int)width) * spaceWidth;
    rect[0]->h = FONT_SIZE;

    SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_GRAY);
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
    SDL_RenderFillRect(renderer, rect[0]);

    rect[1]->x = rect[0]->x + 2;
    rect[1]->y = rect[0]->y + 2;
    rect[1]->h = FONT_SIZE - 4;
    rect[2]->w = rect[0]->w - 4;

    char text[5];
    sprintf(text, "%d%%%%", (int)progress);

    progress /= 100.0D;
    progress *= rect[2]->w;
    rect[1]->w = progress;

    SDL_RenderCopy(renderer, barTex, NULL, rect[1]);

    rect[2]->x = rect[1]->x + rect[1]->w;
    rect[2]->y = rect[1]->y;
    rect[2]->w -= rect[1]->w;
    rect[2]->h = FONT_SIZE - 4;

    co = screenColorToSDLcolor(SCREEN_COLOR_BLACK);
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 64);
    SDL_RenderFillRect(renderer, rect[2]);

    textToFrame(--line, column + (width >> 1) - (strlen(text) >> 1), text);
}

void arrowToFrame(int line, int column)
{
    if(font == NULL)
        return;

    ++line;
    line *= FONT_SIZE;
    column *= spaceWidth;
    column += spaceWidth;

    SDL_Rect *rect = createRect();
    if(rect == NULL)
        return;

    rect->x = column + FONT_SIZE,
    rect->y = line,

    SDL_QueryTexture(arrowTex, NULL, NULL, &(rect->w), &(rect->h));
    SDL_RenderCopy(renderer, arrowTex, NULL, rect);
}

void checkmarkToFrame(int line, int column)
{
    if(font == NULL)
        return;

    ++line;
    line *= FONT_SIZE;
    column *= spaceWidth;
    column += spaceWidth >> 1;

    SDL_Rect *rect = createRect();
    if(rect == NULL)
        return;

    rect->x = column + FONT_SIZE,
    rect->y = line,

    SDL_QueryTexture(checkmarkTex, NULL, NULL, &(rect->w), &(rect->h));
    SDL_RenderCopy(renderer, checkmarkTex, NULL, rect);
}

static inline SDL_Texture *getFlagData(MCPRegion flag)
{
    if(flag & MCP_REGION_EUROPE)
    {
        if(flag & MCP_REGION_USA)
            return flagTex[flag & MCP_REGION_JAPAN ? 7 : 4];

        return flagTex[flag & MCP_REGION_JAPAN ? 5 : 1];
    }

    if(flag & MCP_REGION_USA)
        return flagTex[flag & MCP_REGION_JAPAN ? 6 : 2];

    if(flag & MCP_REGION_JAPAN)
        return flagTex[3];

    return flagTex[0];
}

void flagToFrame(int line, int column, MCPRegion flag)
{
    if(font == NULL)
        return;

    ++line;
    line *= FONT_SIZE;
    column *= spaceWidth;
    column += spaceWidth >> 1;

    SDL_Texture *fd = getFlagData(flag);

    SDL_Rect *rect = createRect();
    if(rect == NULL)
        return;

    rect->x = column + FONT_SIZE,
    rect->y = line,
    SDL_QueryTexture(fd, NULL, NULL, &(rect->w), &(rect->h));
    SDL_RenderCopy(renderer, fd, NULL, rect);
}

void deviceToFrame(int line, int column, DEVICE_TYPE dev)
{
    if(font == NULL)
        return;

    ++line;
    line *= FONT_SIZE;
    column *= spaceWidth;
    column += spaceWidth >> 1;

    SDL_Rect *rect = createRect();
    if(rect == NULL)
        return;

    rect->x = column + FONT_SIZE;
    rect->y = line;

    SDL_QueryTexture(deviceTex[dev], NULL, NULL, &(rect->w), &(rect->h));
    SDL_RenderCopy(renderer, deviceTex[dev], NULL, rect);
}

void tabToFrame(int line, int column, const char *label, bool active)
{
    if(font == NULL)
        return;

    line *= FONT_SIZE;
    line += 20;
    column *= 240;
    column += 13;

    SDL_Rect *curRect = createRect();
    if(curRect == NULL)
        return;

    curRect->x = column + FONT_SIZE;
    curRect->y = line;

    SDL_QueryTexture(tabTex, NULL, NULL, &(curRect->w), &(curRect->h));
    SDL_RenderCopy(renderer, tabTex, NULL, curRect);

    SDL_Rect rect = { .x = curRect->x + (curRect->w >> 1), .y = line + 20, .w = curRect->w, .h = curRect->h };
    rect.x -= FC_GetWidth(font, label) >> 1;
    rect.y -= FONT_SIZE >> 1;

    if(active)
    {
        FC_DrawBox(font, renderer, rect, label);
        return;
    }

    SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_WHITE);
    co.a = 159;
    FC_DrawBoxColor(font, renderer, rect, co, label);
}

int addErrorOverlay(const char *err)
{
    OSTick t = OSGetTick();
    addEntropy(&t, sizeof(OSTick));
    if(font == NULL)
        return -1;

    int i = 0;
    for(; i < MAX_OVERLAYS + 1; ++i)
        if(i < MAX_OVERLAYS && errorOverlay[i].tex == NULL)
            break;

    if(i == MAX_OVERLAYS)
        return -2;

    SDL_Rect rec = { .w = FC_GetWidth(font, err) };
    rec.h = FC_GetColumnHeight(font, rec.w, err);
    if(rec.w == 0 || rec.h == 0)
        return -4;

    errorOverlay[i].tex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, SCREEN_HEIGHT);
    if(errorOverlay[i].tex == NULL)
        return -5;

    SDL_SetTextureBlendMode(errorOverlay[i].tex, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(renderer, errorOverlay[i].tex);

    SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_BLACK);
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 0xC0);
    SDL_RenderClear(renderer);

    rec.x = (SCREEN_WIDTH >> 1) - (rec.w >> 1);
    rec.y = (SCREEN_HEIGHT >> 1) - (rec.h >> 1);

    SDL_Rect *rect = errorOverlay[i].rect;

    rect->x = rec.x - (FONT_SIZE >> 1);
    rect->y = rec.y - (FONT_SIZE >> 1);
    rect->w = rec.w + FONT_SIZE;
    rect->h = rec.h + FONT_SIZE;
    co = screenColorToSDLcolor(SCREEN_COLOR_RED);
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_Rect * or = rect;
    ++rect;
    rect->x = or->x + 2;
    rect->y = or->y + 2;
    rect->w = rec.w + (FONT_SIZE - 4);
    rect->h = rec.h + (FONT_SIZE - 4);
    co = screenColorToSDLcolor(SCREEN_COLOR_D_RED);
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
    SDL_RenderFillRect(renderer, rect);

    FC_DrawBox(font, renderer, rec, err);

    SDL_SetRenderTarget(renderer, frameBuffer);
    drawFrame();
    return i;
}

void removeErrorOverlay(int id)
{
    OSTick t = OSGetTick();
    addEntropy(&t, sizeof(OSTick));
    if(id < 0 || id >= MAX_OVERLAYS || errorOverlay[id].tex == NULL)
        return;

    SDL_DestroyTexture(errorOverlay[id].tex);
    errorOverlay[id].tex = NULL;
    drawFrame();
}

static bool loadTexture(const char *path, SDL_Texture **out)
{
    bool ret = false;
    void *buffer;
    size_t fs = readFile(path, &buffer);
    if(buffer != NULL)
    {
        SDL_RWops *rw = SDL_RWFromMem(buffer, fs);
        if(rw != NULL)
        {
            SDL_Surface *surface = IMG_Load_RW(rw, SDL_TRUE);
            if(surface != NULL)
            {
                *out = SDL_CreateTextureFromSurface(renderer, surface);
                SDL_FreeSurface(surface);
                if(*out != NULL)
                    ret = true;
                else
                    debugPrintf("Error creating texture!");
            }
            else
                debugPrintf("Error creating surface!");
        }
        else
            debugPrintf("Error creating SDL_WRops!");

        MEMFreeToDefaultHeap(buffer);
    }

    return ret;
}

void resumeRenderer()
{
    if(font != NULL)
        return;

    void *ttf;
    size_t size;
    OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &ttf, &size);
    font = FC_CreateFont();
    if(font != NULL)
    {
        SDL_RWops *rw = SDL_RWFromConstMem(ttf, size);
        if(FC_LoadFont_RW(font, renderer, rw, 1, FONT_SIZE, screenColorToSDLcolor(SCREEN_COLOR_WHITE), TTF_STYLE_NORMAL))
        {
            FC_GlyphData spaceGlyph;
            FC_GetGlyphData(font, &spaceGlyph, ' ');
            spaceWidth = spaceGlyph.rect.w;

            OSTime t = OSGetSystemTime();
            loadTexture(ROMFS_PATH "textures/arrow.png", &arrowTex); // TODO: Error handling...
            loadTexture(ROMFS_PATH "textures/checkmark.png", &checkmarkTex);
            loadTexture(ROMFS_PATH "textures/tab.png", &tabTex);

            barTex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, 2, 1);
            SDL_SetRenderTarget(renderer, barTex);
            SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_GREEN);
            SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
            SDL_RenderClear(renderer);
            co = screenColorToSDLcolor(SCREEN_COLOR_D_GREEN);
            SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
            SDL_RenderFillRect(renderer, &barRect);
            // TODO: This doesn't work for some SDL bug reason
            //			SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
            //			SDL_RenderDrawPoint(renderer, 1, 0);

            SDL_Texture *tt = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, 2, 2);
            SDL_SetRenderTarget(renderer, tt);
            // Top left
            SDL_SetRenderDrawColor(renderer, 0x91, 0x1E, 0xFF, 0xFF);
            SDL_RenderClear(renderer);
            // Top right
            SDL_SetRenderDrawColor(renderer, 0x52, 0x05, 0xFF, 0xFF);
            const SDL_Rect r1 = {
                .x = 1,
                .y = 0,
                .w = 1,
                .h = 1,
            };
            SDL_RenderFillRect(renderer, &r1);
            // Bottom right
            SDL_SetRenderDrawColor(renderer, 0x61, 0x0a, 0xFF, 0xFF);
            const SDL_Rect r2 = {
                .x = 1,
                .y = 1,
                .w = 1,
                .h = 1,
            };
            SDL_RenderFillRect(renderer, &r2);
            // Bottom left
            SDL_SetRenderDrawColor(renderer, 0x83, 0x18, 0xFF, 0xFF);
            const SDL_Rect r3 = {
                .x = 0,
                .y = 1,
                .w = 1,
                .h = 1,
            };
            SDL_RenderFillRect(renderer, &r3);

            bgTex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, SCREEN_HEIGHT);
            SDL_SetRenderTarget(renderer, bgTex);
            SDL_RenderCopy(renderer, tt, NULL, NULL);
            SDL_DestroyTexture(tt);

            SDL_SetRenderTarget(renderer, frameBuffer);

            const char *tex;
            for(int i = 0; i < 8; ++i)
            {
                switch(i)
                {
                case 0:
                    tex = ROMFS_PATH "textures/flags/unk.png";
                    break;
                case 1:
                    tex = ROMFS_PATH "textures/flags/eur.png";
                    break;
                case 2:
                    tex = ROMFS_PATH "textures/flags/usa.png";
                    break;
                case 3:
                    tex = ROMFS_PATH "textures/flags/jap.png";
                    break;
                case 4:
                    tex = ROMFS_PATH "textures/flags/eurUsa.png";
                    break;
                case 5:
                    tex = ROMFS_PATH "textures/flags/eurJap.png";
                    break;
                case 6:
                    tex = ROMFS_PATH "textures/flags/usaJap.png";
                    break;
                case 7:
                    tex = ROMFS_PATH "textures/flags/multi.png";
                    break;
                }
                loadTexture(tex, flagTex + i);
            }

            for(int i = 0; i < 3; i++)
            {
                switch(i)
                {
                case 0:
                    tex = ROMFS_PATH "textures/dev/unk.png";
                    break;
                case 1:
                    tex = ROMFS_PATH "textures/dev/usb.png";
                    break;
                case 2:
                    tex = ROMFS_PATH "textures/dev/nand.png";
                    break;
                }
                loadTexture(tex, deviceTex + i);
            }

            t = OSGetSystemTime() - t;
            addEntropy(&t, sizeof(OSTime));
            return;
        }

        debugPrintf("Font: Error loading RW!");
        SDL_RWclose(rw);
    }
    else
        debugPrintf("Font: Error loading!");

    FC_FreeFont(font);
    font = NULL;
}

static inline void quitSDL() __attribute__((__cold__));
static inline void quitSDL()
{
    if(backgroundMusic != NULL)
    {
        debugPrintf("Stopping background music");
        Mix_HaltChannel(0);
        Mix_FreeChunk(backgroundMusic);
        Mix_CloseAudio();
        backgroundMusic = NULL;
    }
    if(bgmBuffer != NULL)
    {
        MEMFreeToDefaultHeap(bgmBuffer);
        bgmBuffer = NULL;
    }

    // TODO:
    if(TTF_WasInit())
        TTF_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    //	SDL_QuitSubSystem(SDL_INIT_VIDEO);
    //	SDL_Quit();
}

bool initRenderer()
{
    if(font)
        return true;

    for(int i = 0; i < MAX_OVERLAYS; ++i)
        errorOverlay[i].tex = NULL;

    if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) == 0)
    {
        window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
        if(window)
        {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
            if(renderer)
            {
                frameBuffer = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, SCREEN_HEIGHT);
                if(frameBuffer != NULL)
                {
                    SDL_SetRenderTarget(renderer, frameBuffer);

                    OSTime t = OSGetSystemTime();
                    if(Mix_Init(MIX_INIT_MP3))
                    {
                        size_t fs = readFile(ROMFS_PATH "audio/bg.mp3", &bgmBuffer);
                        if(bgmBuffer != NULL)
                        {
                            if(Mix_OpenAudio(22050, AUDIO_S16MSB, 2, 4096) == 0)
                            {
                                SDL_RWops *rw = SDL_RWFromMem(bgmBuffer, fs);
                                backgroundMusic = Mix_LoadWAV_RW(rw, true);
                                if(backgroundMusic != NULL)
                                {
                                    Mix_VolumeChunk(backgroundMusic, 15);
                                    if(Mix_PlayChannel(0, backgroundMusic, -1) == 0)
                                        goto audioRunning;

                                    Mix_FreeChunk(backgroundMusic);
                                    backgroundMusic = NULL;
                                }

                                Mix_CloseAudio();
                            }

                            MEMFreeToDefaultHeap(bgmBuffer);
                            bgmBuffer = NULL;
                        }
                    }
                audioRunning:
                    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

                    GX2SetTVGamma(2.0f);
                    GX2SetDRCGamma(1.0f);

                    if(loadTexture(ROMFS_PATH "textures/goodbye.png", &byeTex))
                    {
                        t = OSGetSystemTime() - t;
                        addEntropy(&t, sizeof(OSTime));

                        TTF_Init();
                        resumeRenderer();
                        if(font != NULL)
                        {
                            addToScreenLog("SDL initialized!");
                            startNewFrame();
                            textToFrame(0, 0, "Loading...");
                            writeScreenLog(1);
                            drawFrame();
                            return true;
                        }

                        pauseRenderer();
                    }
                    else
                        debugPrintf("Can't find goodbye texture!");

                    SDL_DestroyTexture(frameBuffer);
                }

                SDL_DestroyRenderer(renderer);
            }

            SDL_DestroyWindow(window);
        }

        quitSDL();
    }
    else
        debugPrintf("SDL init error: %s", SDL_GetError());

    return false;
}

#define clearFrame()

void pauseRenderer()
{
    if(font == NULL)
        return;

    clearFrame();

    FC_FreeFont(font);
    SDL_DestroyTexture(arrowTex);
    SDL_DestroyTexture(checkmarkTex);
    SDL_DestroyTexture(tabTex);
    SDL_DestroyTexture(barTex);
    SDL_DestroyTexture(bgTex);

    for(int i = 0; i < 8; ++i)
        SDL_DestroyTexture(flagTex[i]);

    font = NULL;
}

void shutdownRenderer()
{
    if(font == NULL)
        return;

    for(int i = 0; i < MAX_OVERLAYS; ++i)
        removeErrorOverlay(i);

    SDL_SetRenderTarget(renderer, NULL);
    colorStartNewFrame(SCREEN_COLOR_BLUE);

    SDL_Rect bye;
    SDL_QueryTexture(byeTex, NULL, NULL, &(bye.w), &(bye.h));
    bye.x = (SCREEN_WIDTH >> 1) - (bye.w >> 1);
    bye.y = (SCREEN_HEIGHT >> 1) - (bye.h >> 1);

    SDL_RenderCopy(renderer, byeTex, NULL, &bye);
    SDL_RenderPresent(renderer);
    clearFrame();
    pauseRenderer();

    SDL_DestroyTexture(frameBuffer);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    quitSDL();
}

void colorStartNewFrame(uint32_t color)
{
    if(font == NULL)
        return;

    clearFrame();

    if(rectList != NULL)
    {
        SDL_Rect_Entry *current = rectList;
        SDL_Rect_Entry *next;
        do
        {
            next = current->next;
            MEMFreeToDefaultHeap(current);
            current = next;
        } while(current != NULL);

        rectList = NULL;
    }

    if(color == SCREEN_COLOR_BLUE)
        SDL_RenderCopy(renderer, bgTex, NULL, NULL);
    else
    {
        SDL_Color co = screenColorToSDLcolor(color);
        SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
        SDL_RenderClear(renderer);
    }
}

void showFrame()
{
    // Contrary to VSync enabled SDL we use GX2WaitForVsync() directly instead of
    // WHBGFX WHBGfxBeginRender() for VSync as WHBGfxBeginRender() produces frames
    // way shorter than 16 ms sometimes, confusing frame counting timers
    GX2WaitForVsync();
    readInput();
}

#define predrawFrame()                   \
    if(font == NULL)                     \
        return;                          \
                                         \
    SDL_SetRenderTarget(renderer, NULL); \
    SDL_RenderCopy(renderer, frameBuffer, NULL, NULL);

#define postdrawFrame()                                                \
    for(int i = 0; i < MAX_OVERLAYS; ++i)                              \
        if(errorOverlay[i].tex != NULL)                                \
            SDL_RenderCopy(renderer, errorOverlay[i].tex, NULL, NULL); \
                                                                       \
    SDL_RenderPresent(renderer);                                       \
    SDL_SetRenderTarget(renderer, frameBuffer);

// We need to draw the DRC before the TV, else the DRC is always one frame behind
void drawFrame()
{
    predrawFrame();
    postdrawFrame();
}

void drawKeyboard(bool tv)
{
    predrawFrame();

    if(tv)
        Swkbd_DrawTV();
    else
        Swkbd_DrawDRC();

    postdrawFrame();
    showFrame();
}

uint32_t getSpaceWidth()
{
    return spaceWidth;
}
