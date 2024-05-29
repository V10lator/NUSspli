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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <crypto.h>
#include <file.h>
#include <input.h>
#include <list.h>
#include <menu/utils.h>
#include <osdefs.h>
#include <renderer.h>
#include <romfs.h>
#include <staticMem.h>
#include <swkbd_wrapper.h>
#include <thread.h>
#include <utils.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_surface.h>
#include <SDL_FontCache.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <gx2/enum.h>
#include <gx2/event.h>
#pragma GCC diagnostic pop

#define SSAA         8
#define MAX_OVERLAYS 8
#define SDL_RECTS    512

typedef struct
{
    SDL_Texture *tex;
    SDL_Rect rect[2];
} ErrorOverlay;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static FC_Font *font = NULL;
static void *bgmBuffer = NULL;
static Mix_Music *backgroundMusic = NULL;

static int32_t spaceWidth;

static SDL_Texture *frameBuffer;
static SDL_Texture *defaultTex = NULL;
static SDL_Texture *arrowTex;
static SDL_Texture *checkmarkTex;
static SDL_Texture *tabTex;
static SDL_Texture *flagTex[8];
static SDL_Texture *deviceTex[4];
static SDL_Texture *barTex;
static SDL_Texture *bgTex;
static SDL_Texture *byeTex;
static SDL_Rect byeRect;

static LIST *rectList;
static LIST *errorOverlayList;

static inline SDL_Rect *createRect()
{
    SDL_Rect *ret = MEMAllocFromDefaultHeap(sizeof(SDL_Rect));
    if(!ret)
        return NULL;

    if(!addToListEnd(rectList, ret))
    {
        MEMFreeToDefaultHeap(ret);
        return NULL;
    }

    return ret;
}

#define internalTextToFrame()                                 \
    {                                                         \
        ++line;                                               \
        line *= FONT_SIZE;                                    \
        line -= 7;                                            \
        int w = FC_GetWidth(font, str);                       \
                                                              \
        if(maxWidth != 0 && w > maxWidth)                     \
        {                                                     \
            size_t i = strlen(str);                           \
            char *lineBuffer = (char *)getStaticLineBuffer(); \
            char *tmp = lineBuffer;                           \
            OSBlockMove(tmp, str, i + 1, false);              \
            tmp += i;                                         \
                                                              \
            *--tmp = '\0';                                    \
            *--tmp = '.';                                     \
            *--tmp = '.';                                     \
            *--tmp = '.';                                     \
                                                              \
            char *tmp2;                                       \
            w = FC_GetWidth(font, lineBuffer);                \
            while(w > maxWidth)                               \
            {                                                 \
                tmp2 = tmp;                                   \
                *--tmp = '.';                                 \
                ++tmp2;                                       \
                *++tmp2 = '\0';                               \
                w = FC_GetWidth(font, lineBuffer);            \
            }                                                 \
                                                              \
            if(*--tmp == ' ')                                 \
            {                                                 \
                *tmp = '.';                                   \
                tmp[3] = '\0';                                \
            }                                                 \
                                                              \
            str = lineBuffer;                                 \
        }                                                     \
                                                              \
        switch(column)                                        \
        {                                                     \
            case ALIGNED_CENTER:                              \
                column = (SCREEN_WIDTH >> 1) - (w >> 1);      \
                break;                                        \
            case ALIGNED_RIGHT:                               \
                column = SCREEN_WIDTH - w - FONT_SIZE;        \
                break;                                        \
            default:                                          \
                column *= spaceWidth;                         \
                column += FONT_SIZE;                          \
        }                                                     \
    }

void textToFrameCut(int line, int column, const char *str, int maxWidth)
{
    if(font == NULL)
        return;

    internalTextToFrame();
    FC_Draw(font, renderer, column, line, str);
}

void textToFrameColoredCut(int line, int column, const char *str, SCREEN_COLOR color, int maxWidth)
{
    if(font == NULL)
        return;

    internalTextToFrame();
    FC_DrawColor(font, renderer, column, line, color, str);
}

int textToFrameMultiline(int x, int y, const char *text, size_t len)
{
    if(font == NULL || !len)
        return 0;

    size_t fl = FC_GetWidth(font, text) / spaceWidth;
    if(fl <= len)
    {
        textToFrame(x, y, text);
        return 1;
    }

    char *p = getStaticLineBuffer();
    size_t l = strlen(text);
    OSBlockMove(p, text, l + 1, false);

    char *t;
    char o;
    int lines = 1;
    while(fl > len)
    {
        for(char *i = p + l; i > p; --i)
        {
            o = *i;
            *i = '\0';
            if(((size_t)FC_GetWidth(font, p) / spaceWidth) <= len)
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
            l = strlen(p);
        }

        fl = FC_GetWidth(font, p) / spaceWidth;
    }

    textToFrame(x, y, p);
    return lines;
}

void lineToFrame(int column, SCREEN_COLOR color)
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

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, rect);
}

void boxToFrame(int lineStart, int lineEnd)
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

    // Horizontal lines
    lineToFrame(lineStart, SCREEN_COLOR_GRAY);
    lineToFrame(lineEnd, SCREEN_COLOR_GRAY);

    // Vertical lines
    rect[0]->w = 3;
    rect[0]->h = (lineEnd - lineStart) * FONT_SIZE;
    rect[0]->x = FONT_SIZE;
    rect[0]->y = ((++lineStart) * FONT_SIZE) + ((FONT_SIZE >> 1) - 1);
    SDL_RenderFillRect(renderer, rect[0]);

    rect[1]->x = (SCREEN_WIDTH - (FONT_SIZE << 1) + FONT_SIZE) - 3;
    rect[1]->y = rect[0]->y;
    rect[1]->w = 3;
    rect[1]->h = rect[0]->h;
    SDL_RenderFillRect(renderer, rect[1]);

    // Background - we paint it on top of the gray lines as they look better that way
    rect[2]->x = FONT_SIZE + 2;
    rect[2]->y = rect[0]->y + 2;
    rect[2]->w = SCREEN_WIDTH - (FONT_SIZE << 1) - 3;
    rect[2]->h = rect[0]->h - 3;
    SCREEN_COLOR co = SCREEN_COLOR_BLACK;
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 64);
    SDL_RenderFillRect(renderer, rect[2]);
}

void barToFrame(int line, int column, uint32_t width, float progress)
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

    SDL_Color co = SCREEN_COLOR_GRAY;
    SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
    SDL_RenderFillRect(renderer, rect[0]);

    rect[1]->x = rect[0]->x + 2;
    rect[1]->y = rect[0]->y + 2;
    rect[1]->h = FONT_SIZE - 4;
    rect[2]->w = rect[0]->w - 4;

    char text[8];
    sprintf(text, "%d%%%%", (int)(progress * 100.0f));

    progress *= rect[2]->w;
    rect[1]->w = progress;

    SDL_RenderCopy(renderer, barTex, NULL, rect[1]);

    rect[2]->x = rect[1]->x + rect[1]->w;
    rect[2]->y = rect[1]->y;
    rect[2]->w -= rect[1]->w;
    rect[2]->h = FONT_SIZE - 4;

    co = SCREEN_COLOR_BLACK;
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

    column = curRect->x + (curRect->w >> 1) - (FC_GetWidth(font, label) >> 1);
    line += 20 - (FONT_SIZE >> 1);

    if(active)
    {
        FC_Draw(font, renderer, column, line, label);
        return;
    }

    FC_DrawColor(font, renderer, column, line, SCREEN_COLOR_WHITE_TRANSP, label);
}

void *addErrorOverlay(const char *err)
{
    OSTick t = OSGetTick();
    addEntropy(&t, sizeof(OSTick));
    if(font == NULL)
        return NULL;

    ErrorOverlay *overlay = MEMAllocFromDefaultHeap(sizeof(ErrorOverlay));
    if(overlay == NULL)
        return NULL;

    SDL_Rect rec = { .w = FC_GetWidth(font, err) };
    rec.h = FC_GetColumnHeight(font, rec.w, err);
    if(rec.w != 0 && rec.h != 0)
    {
        overlay->tex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, SCREEN_HEIGHT);
        if(overlay->tex != NULL)
        {
            if(addToListEnd(errorOverlayList, overlay))
            {
                SDL_SetTextureBlendMode(overlay->tex, SDL_BLENDMODE_BLEND);
                SDL_SetRenderTarget(renderer, overlay->tex);

                SDL_Color co = SCREEN_COLOR_BLACK;
                SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 0xC0);
                SDL_RenderClear(renderer);

                rec.x = (SCREEN_WIDTH >> 1) - (rec.w >> 1);
                rec.y = (SCREEN_HEIGHT >> 1) - (rec.h >> 1);

                SDL_Rect *rect = overlay->rect;

                rect->x = rec.x - (FONT_SIZE >> 1);
                rect->y = rec.y - (FONT_SIZE >> 1);
                rect->w = rec.w + FONT_SIZE;
                rect->h = rec.h + FONT_SIZE;
                co = SCREEN_COLOR_RED;
                SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
                SDL_RenderFillRect(renderer, rect);

                SDL_Rect * or = rect;
                ++rect;
                rect->x = or->x + 2;
                rect->y = or->y + 2;
                rect->w = rec.w + (FONT_SIZE - 4);
                rect->h = rec.h + (FONT_SIZE - 4);
                co = SCREEN_COLOR_D_RED;
                SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
                SDL_RenderFillRect(renderer, rect);

                FC_DrawBox(font, renderer, rec, err);

                SDL_SetRenderTarget(renderer, frameBuffer);

                drawFrame();
                return overlay;
            }

            SDL_DestroyTexture(overlay->tex);
        }
    }

    MEMFreeToDefaultHeap(overlay);
    return NULL;
}

void removeErrorOverlay(void *overlay)
{
    if(font == NULL || overlay == NULL)
        return;

    OSTick t = OSGetTick();
    addEntropy(&t, sizeof(OSTick));

    removeFromList(errorOverlayList, overlay);
    drawFrame();
    SDL_DestroyTexture(((ErrorOverlay *)overlay)->tex);
    MEMFreeToDefaultHeap(overlay);
}

static inline void loadDefaultTexture()
{
    if(defaultTex != NULL)
        return;

    defaultTex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, 7, 11);
    if(defaultTex == NULL)
    {
        debugPrintf("Couldn't load default texture!");
        return;
    }

    SDL_SetRenderTarget(renderer, defaultTex);
    SDL_SetRenderDrawColor(renderer, 0xFF, 0x00, 0xFF, 0xFF);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xFF);

    SDL_RenderDrawPoint(renderer, 2, 2);
    SDL_RenderDrawPoint(renderer, 2, 3);
    SDL_RenderDrawPoint(renderer, 3, 4);
    SDL_RenderDrawPoint(renderer, 3, 5);

    SDL_RenderDrawPoint(renderer, 4, 6);

    SDL_RenderDrawPoint(renderer, 5, 7);
    SDL_RenderDrawPoint(renderer, 5, 8);
    SDL_RenderDrawPoint(renderer, 6, 9);
    SDL_RenderDrawPoint(renderer, 6, 10);

    SDL_RenderDrawPoint(renderer, 6, 2);
    SDL_RenderDrawPoint(renderer, 6, 3);
    SDL_RenderDrawPoint(renderer, 5, 4);
    SDL_RenderDrawPoint(renderer, 5, 5);

    SDL_RenderDrawPoint(renderer, 3, 7);
    SDL_RenderDrawPoint(renderer, 3, 8);
    SDL_RenderDrawPoint(renderer, 2, 9);
    SDL_RenderDrawPoint(renderer, 2, 10);

    SDL_SetRenderTarget(renderer, frameBuffer);
}

static bool loadTexture(const char *path, SDL_Texture **out)
{
    void *buffer;
    size_t fs = readFile(path, &buffer);
    *out = defaultTex;
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
                if(*out == NULL)
                {
                    *out = defaultTex;
                    debugPrintf("Error creating texture!");
                }
            }
            else
                debugPrintf("Error creating surface!");
        }
        else
            debugPrintf("Error creating SDL_WRops!");

        MEMFreeToDefaultHeap(buffer);
    }

    return *out != defaultTex;
}

void resumeRenderer()
{
    if(font != NULL)
        return;

    loadDefaultTexture();

    void *ttf;
    size_t size;
    OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &ttf, &size);
    font = FC_CreateFont();
    if(font != NULL)
    {
        SDL_RWops *rw = SDL_RWFromConstMem(ttf, size);
        if(FC_LoadFont_RW(font, renderer, rw, 1, FONT_SIZE, SCREEN_COLOR_WHITE, TTF_STYLE_NORMAL))
        {
            FC_GlyphData spaceGlyph;
            FC_GetGlyphData(font, &spaceGlyph, ' ');
            spaceWidth = spaceGlyph.rect.w;

            OSTime t = OSGetSystemTime();
            loadTexture(ROMFS_PATH "textures/goodbye.png", &byeTex);
            SDL_QueryTexture(byeTex, NULL, NULL, &(byeRect.w), &(byeRect.h));
            byeRect.x = (SCREEN_WIDTH >> 1) - (byeRect.w >> 1);
            byeRect.y = (SCREEN_HEIGHT >> 1) - (byeRect.h >> 1);

            loadTexture(ROMFS_PATH "textures/arrow.png", &arrowTex);
            loadTexture(ROMFS_PATH "textures/checkmark.png", &checkmarkTex);
            loadTexture(ROMFS_PATH "textures/tab.png", &tabTex);

            barTex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, 2, 1);
            SDL_SetRenderTarget(renderer, barTex);
            SDL_Color co = SCREEN_COLOR_GREEN;
            SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
            SDL_RenderClear(renderer);
            co = SCREEN_COLOR_D_GREEN;
            SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
            SDL_RenderDrawPoint(renderer, 2, 1);

            bgTex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, 2, 2);
            SDL_SetRenderTarget(renderer, bgTex);
            // Top left
            SDL_SetRenderDrawColor(renderer, 0x91, 0x1E, 0xFF, 0xFF);
            SDL_RenderClear(renderer);
            // Top right
            SDL_SetRenderDrawColor(renderer, 0x52, 0x05, 0xFF, 0xFF);
            SDL_RenderDrawPoint(renderer, 2, 1);
            // Bottom right
            SDL_SetRenderDrawColor(renderer, 0x61, 0x0a, 0xFF, 0xFF);
            SDL_RenderDrawPoint(renderer, 2, 2);
            // Bottom left
            SDL_SetRenderDrawColor(renderer, 0x83, 0x18, 0xFF, 0xFF);
            SDL_RenderDrawPoint(renderer, 1, 2);

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

            for(int i = 0; i < 4; i++)
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
                        tex = ROMFS_PATH "textures/dev/sd.png";
                        break;
                    case 3:
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

static inline void quitSDL()
{
    if(backgroundMusic != NULL)
    {
        debugPrintf("Stopping background music");
        Mix_HaltMusic();
        OSSleepTicks(OSMillisecondsToTicks(20));
        Mix_FreeMusic(backgroundMusic);
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

    rectList = createList();
    if(rectList == NULL)
        return false;

    errorOverlayList = createList();
    if(errorOverlayList != NULL)
    {
        if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) == 0)
        {
            window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
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
                                if(Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, MIX_DEFAULT_CHANNELS, 4096) == 0)
                                {
                                    SDL_RWops *rw = SDL_RWFromMem(bgmBuffer, fs);
                                    backgroundMusic = Mix_LoadMUS_RW(rw, true);
                                    if(backgroundMusic != NULL)
                                    {
                                        Mix_VolumeMusic(SDL_MIX_MAXVOLUME * 0.15);
                                        Mix_PlayMusic(backgroundMusic, -1);
                                        if(Mix_PlayMusic(backgroundMusic, -1) == 0)
                                            goto audioRunning;

                                        Mix_FreeMusic(backgroundMusic);
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

        destroyList(errorOverlayList, true);
    }

    destroyList(rectList, true);
    return false;
}

static inline void destroyTex(SDL_Texture *tex)
{
    if(tex != defaultTex)
        SDL_DestroyTexture(tex);
}

void pauseRenderer()
{
    if(font == NULL)
        return;

    destroyTex(arrowTex);
    destroyTex(checkmarkTex);
    destroyTex(tabTex);
    destroyTex(barTex);
    destroyTex(bgTex);
    destroyTex(byeTex);

    for(int i = 0; i < 8; ++i)
        destroyTex(flagTex[i]);

    for(int i = 0; i < 4; i++)
        destroyTex(deviceTex[i]);

    if(defaultTex != NULL)
    {
        SDL_DestroyTexture(defaultTex);
        defaultTex = NULL;
    }

    FC_FreeFont(font);
    font = NULL;
}

void drawByeFrame()
{
    if(font == NULL)
        return;

    startNewFrame();
    SDL_RenderCopy(renderer, byeTex, NULL, &byeRect);
    if(!Swkbd_IsReady() || Swkbd_IsHidden())
        drawFrame();
}

void shutdownRenderer()
{
    if(font == NULL)
        return;

    ErrorOverlay *overlay;
    forEachListEntry(errorOverlayList, overlay)
        SDL_DestroyTexture(overlay->tex);

    destroyList(errorOverlayList, true);

    pauseRenderer();

    SDL_DestroyTexture(frameBuffer);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    quitSDL();
    destroyList(rectList, true);
}

void colorStartNewFrame(SCREEN_COLOR color)
{
    if(font == NULL)
        return;

    if(*(uint32_t *)&(color.r) == *(uint32_t *)&(SCREEN_COLOR_BLUE.r))
        SDL_RenderCopy(renderer, bgTex, NULL, NULL);
    else
    {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderClear(renderer);
    }

    clearList(rectList, true);
}

void showFrame()
{
    if(font == NULL)
        return;

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

#define postdrawFrame()                                     \
    ErrorOverlay *overlay;                                  \
    forEachListEntry(errorOverlayList, overlay)             \
        SDL_RenderCopy(renderer, overlay->tex, NULL, NULL); \
                                                            \
    SDL_RenderPresent(renderer);                            \
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
