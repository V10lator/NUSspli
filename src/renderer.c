/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2021 V10lator <v10lator@myway.de>                    *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.             *
 ***************************************************************************/

#include <wut-fixups.h>

#include <gx2/enum.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_surface.h>
#include <SDL_FontCache.h>

#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <gx2/event.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include <crypto.h>
#include <file.h>
#include <input.h>
#include <osdefs.h>
#include <renderer.h>
#include <romfs.h>
#include <staticMem.h>
#include <swkbd_wrapper.h>
#include <utils.h>
#include <menu/utils.h>

#define SSAA            8
#define MAX_OVERLAYS    8
#define SCREEN_X		1280
#define SCREEN_Y		720
#define SDL_RECTS		512

typedef struct
{
	SDL_Texture *tex;
	SDL_Rect rect[2];
} ErrorOverlay;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static FC_Font *font = NULL;
static void *bgmBuffer = NULL;
static Mix_Chunk *backgroundMusic = NULL;

static int32_t spaceWidth;

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

static SDL_Rect rect[SDL_RECTS];
static SDL_Rect *curRect;

#define screenColorToSDLcolor(color) (SDL_Color){ .a = color & 0xFFu, .b = (color & 0x0000FF00u) >> 8, .g = (color & 0x00FF0000u) >> 16, .r = (color & 0xFF000000u) >> 24 }

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
			text.x = (SCREEN_X >> 1) - (text.w >> 1);
			break;
		case ALIGNED_RIGHT:
			text.x = SCREEN_X - text.w - FONT_SIZE;
			break;
		default:
			column *= spaceWidth;
			text.x = column + FONT_SIZE;
	}

	FC_DrawBox(font, renderer, text, str);
}

void lineToFrame(int column, uint32_t color)
{
	if(font == NULL)
		return;

	++column;
	column *= FONT_SIZE;
	curRect->x = FONT_SIZE;
	curRect->y = column + ((FONT_SIZE >> 1) - 1);
	curRect->w = SCREEN_X - (FONT_SIZE << 1);
	curRect->h = 3;

	SDL_Color co = screenColorToSDLcolor(color);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
	SDL_RenderFillRect(renderer, curRect);
	++curRect;
}

void boxToFrame(int lineStart, int lineEnd)
{
	if(font == NULL)
		return;

	int ty = ((++lineStart) * FONT_SIZE) + ((FONT_SIZE >> 1) - 1);
	int tw = SCREEN_X - (FONT_SIZE << 1);
	curRect->x = FONT_SIZE;
	curRect->y = ty;
	curRect->w = tw;
	curRect->h = 3;

	SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_GRAY);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);

	// Horizontal lines
	SDL_RenderFillRect(renderer, curRect);
	++curRect;

	curRect->x = FONT_SIZE;
	curRect->y = ((++lineEnd) * FONT_SIZE) + ((FONT_SIZE >> 1) - 1) - 3;
	curRect->w = tw;
	curRect->h = 3;

	SDL_RenderFillRect(renderer, curRect);
	++curRect;
	
	// Vertical lines
	curRect->x = FONT_SIZE;
	curRect->y = ty;
	curRect->w = 3;
	int h = (--lineEnd - --lineStart) * FONT_SIZE;
	curRect->h = h;
	SDL_RenderFillRect(renderer, curRect);
	++curRect;

	curRect->x = (FONT_SIZE - 3) + tw;
	curRect->y = ty;
	curRect->w = 3;
	curRect->h = h;
	SDL_RenderFillRect(renderer, curRect);
	++curRect;
	
	// Background - we paint it on top of the gray lines as they look better that way
	curRect->x = FONT_SIZE + 2;
	curRect->y = ty + 2;
	curRect->w = tw - 3;
	curRect->h = h - 3;
	co = screenColorToSDLcolor(SCREEN_COLOR_BLACK);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 64);
	SDL_RenderFillRect(renderer, curRect);
	++curRect;
}

void barToFrame(int line, int column, uint32_t width, double progress)
{
	if(font == NULL)
		return;
	
	curRect->x = FONT_SIZE + (column * spaceWidth);
	curRect->y = ((++line ) * FONT_SIZE) - 2;
	curRect->w = ((int)width) * spaceWidth;
	curRect->h = FONT_SIZE;

	SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_GRAY);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
	SDL_RenderFillRect(renderer, curRect);
	int x = curRect->x * 2;
	int y = curRect->y * 2;
	int w = curRect->w - 4;
	++curRect;

	curRect->x = x;
	curRect->y = y;
	curRect->h =  FONT_SIZE - 4;

	char text[5];
	sprintf(text, "%d%%%%", (int)progress);

	progress /= 100.0D;
	progress *= w;
	x = progress;
	curRect->w = x;
	
	SDL_RenderCopy(renderer, barTex, NULL, curRect);
	++curRect;
	
	curRect->x += x;
	curRect->w = w - x;
	
	co = screenColorToSDLcolor(SCREEN_COLOR_BLACK);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 64);
	SDL_RenderFillRect(renderer, curRect);
	++curRect;
	
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
	
	curRect->x = column + FONT_SIZE,
	curRect->y = line,

	SDL_QueryTexture(arrowTex, NULL, NULL, &(curRect->w), &(curRect->h));
	SDL_RenderCopy(renderer, arrowTex, NULL, curRect);
	++curRect;
}

void checkmarkToFrame(int line, int column)
{
	if(font == NULL)
		return;
	
	++line;
	line *= FONT_SIZE;
	column *= spaceWidth;
	column += spaceWidth >> 1;

	curRect->x = column + FONT_SIZE,
	curRect->y = line,

	SDL_QueryTexture(checkmarkTex, NULL, NULL, &(curRect->w), &(curRect->h));
	SDL_RenderCopy(renderer, checkmarkTex, NULL, curRect);
	++curRect;
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
	curRect->x = column + FONT_SIZE,
	curRect->y = line,

	SDL_QueryTexture(fd, NULL, NULL, &(curRect->w), &(curRect->h));
	SDL_RenderCopy(renderer, fd, NULL, curRect);
	++curRect;
}

void deviceToFrame(int line, int column, DEVICE_TYPE dev)
{
	if(font == NULL)
		return;

	++line;
	line *= FONT_SIZE;
	column *= spaceWidth;
	column += spaceWidth >> 1;

	curRect->x = column + FONT_SIZE;
	curRect->y = line;

	SDL_QueryTexture(deviceTex[dev], NULL, NULL, &(curRect->w), &(curRect->h));
	SDL_RenderCopy(renderer, deviceTex[dev], NULL, curRect);
	++curRect;
}

void tabToFrame(int line, int column, const char *label, bool active)
{
	if(font == NULL)
		return;
	
	line *= FONT_SIZE;
	line += 20;
	column *= 240;
	column += 13;
	
	int x = column + FONT_SIZE;
	curRect->x = x;
	curRect->y = line;

	int w;
	SDL_QueryTexture(tabTex, NULL, NULL, &w, &(curRect->h));
	curRect->w = w;
	SDL_RenderCopy(renderer, tabTex, NULL, curRect);
	++curRect;

	curRect->x = x + (w >> 1);
	curRect->x -= FC_GetWidth(font, label) >> 1;
	curRect->y = line + 20;
	curRect->y -= FONT_SIZE >> 1;

	if(active)
	{
		FC_DrawBox(font, renderer, *curRect, label);
		return;
	}

	SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_WHITE);
	co.a = 159;
	FC_DrawBoxColor(font, renderer, *curRect, co, label);
}

int addErrorOverlay(const char *err)
{
/*	OSTick t = OSGetTick();
	addEntropy(&t, sizeof(OSTick));
	if(font == NULL)
		return -1;

	int i = 0;
	for( ; i < MAX_OVERLAYS + 1; ++i)
		if(i < MAX_OVERLAYS && errorOverlay[i].tex == NULL)
			break;

	if(i == MAX_OVERLAYS)
		return -2;

	int w = FC_GetWidth(font, err);
	int h = FC_GetColumnHeight(font, w, err);
	if(w == 0 || h == 0)
		return -4;

	errorOverlay[i].tex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, SCREEN_X, SCREEN_Y);
	if(errorOverlay[i].tex  == NULL)
		return -5;

	SDL_SetTextureBlendMode(errorOverlay[i].tex, SDL_BLENDMODE_BLEND);
	SDL_SetRenderTarget(renderer, errorOverlay[i].tex);

	SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_BLACK);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 0xC0);
	SDL_RenderClear(renderer);

	int x = (SCREEN_X >> 1) - (w >> 1);
	int y = (SCREEN_Y >> 1) - (h >> 1);

	errorOverlay[i].rect[0].x = x - (FONT_SIZE >> 1);
	errorOverlay[i].rect[0].y = y - (FONT_SIZE >> 1);
	errorOverlay[i].rect[0].w = w + FONT_SIZE;
	errorOverlay[i].rect[0].h = h + FONT_SIZE;
	co = screenColorToSDLcolor(SCREEN_COLOR_RED);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
	SDL_RenderFillRect(renderer, errorOverlay[i].rect);

	errorOverlay[i].rect[1].x = x - (FONT_SIZE >> 1) + 2;
	errorOverlay[i].rect[1].y = y - (FONT_SIZE >> 1) + 2;
	errorOverlay[i].rect[1].w = w + (FONT_SIZE - 4);
	errorOverlay[i].rect[1].h = h + (FONT_SIZE - 4);
	co = screenColorToSDLcolor(SCREEN_COLOR_D_RED);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
	SDL_RenderFillRect(renderer, errorOverlay[i].rect + 1);

	curRect->x = x;
	curRect->y = y;
	curRect->w = w;
	curRect->h = h;
	FC_DrawBox(font, renderer, *curRect, err);

	SDL_SetRenderTarget(renderer, frameBuffer);
	drawFrame();
	return i;
*/
	drawFrame();
	return 0;
}

void removeErrorOverlay(int id)
{
/*	OSTick t = OSGetTick();
	addEntropy(&t, sizeof(OSTick));
	if(id < 0 || id >= MAX_OVERLAYS || errorOverlay[id].tex == NULL)
		return;
	
	SDL_DestroyTexture(errorOverlay[id].tex);
	errorOverlay[id].tex = NULL;*/
	drawFrame();
}

static bool loadTexture(const char *path, SDL_Texture **out)
{
	SDL_Surface *surface = IMG_Load_RW(SDL_RWFromFile(path, "rb"), SDL_TRUE);
	*out = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_FreeSurface(surface);
	return *out != NULL;
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
			loadTexture(ROMFS_PATH "textures/arrow.png", &arrowTex); //TODO: Error handling...
			loadTexture(ROMFS_PATH "textures/checkmark.png", &checkmarkTex);
			loadTexture(ROMFS_PATH "textures/tab.png", &tabTex);

			barTex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, 2, 1);
			SDL_SetRenderTarget(renderer, barTex);
			SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_GREEN);
			SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
			SDL_RenderClear(renderer);
			co = screenColorToSDLcolor(SCREEN_COLOR_D_GREEN);
			SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
			SDL_Rect r = { .x = 1, .y = 0, .w = 1, .h = 1, };
			SDL_RenderFillRect(renderer, &r);
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
			SDL_RenderFillRect(renderer, &r);
			// Bottom right
			SDL_SetRenderDrawColor(renderer, 0x61, 0x0a, 0xFF, 0xFF);
			r.y = 1;
			SDL_RenderFillRect(renderer, &r);
			// Bottom left
			SDL_SetRenderDrawColor(renderer, 0x83, 0x18, 0xFF, 0xFF);
			r.x = 0;
			SDL_RenderFillRect(renderer, &r);

			bgTex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, SCREEN_X, SCREEN_Y);
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

		window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_X, SCREEN_Y, 0);
		if(window)
		{
			renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
			if(renderer)
			{
				frameBuffer = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, SCREEN_X, SCREEN_Y);
				if(frameBuffer != NULL)
				{
					SDL_SetRenderTarget(renderer, frameBuffer);

					OSTime t = OSGetSystemTime();
					if(Mix_Init(MIX_INIT_MP3))
					{
						FILE *f = fopen(ROMFS_PATH "audio/bg.mp3", "rb");
						if(f != NULL)
						{
							size_t fs = getFilesize(f);
							bgmBuffer = MEMAllocFromDefaultHeap(fs);
							if(bgmBuffer != NULL)
							{
								if(fread(bgmBuffer, fs, 1, f) == 1)
								{
									if(Mix_OpenAudio(22050, AUDIO_S16MSB, 2, 4096) == 0)
									{
										SDL_RWops *rw = SDL_RWFromMem(bgmBuffer, fs);
										backgroundMusic = Mix_LoadWAV_RW(rw, true);
										if(backgroundMusic != NULL)
										{
											Mix_VolumeChunk(backgroundMusic, 15);
											if(Mix_PlayChannel(0, backgroundMusic, -1) == 0)
											{
												fclose(f);
												goto audioRunning;
											}

											Mix_FreeChunk(backgroundMusic);
											backgroundMusic = NULL;
										}
										SDL_RWclose(rw);
										Mix_CloseAudio();
									}
								}
								MEMFreeToDefaultHeap(bgmBuffer);
								bgmBuffer = NULL;
							}
							fclose(f);
						}
					}

audioRunning:
					SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
					SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

					GX2SetTVGamma(2.0f);
					GX2SetDRCGamma(1.0f);

					loadTexture(ROMFS_PATH "textures/goodbye.png", &byeTex);

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
	bye.x = (SCREEN_X >> 1) - (bye.w >> 1);
	bye.y = (SCREEN_Y >> 1) - (bye.h >> 1);

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
	curRect = rect;

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

#define predrawFrame()									\
	if(font == NULL)									\
		return;											\
														\
	SDL_SetRenderTarget(renderer, NULL);				\
	SDL_RenderCopy(renderer, frameBuffer, NULL, NULL);

#define postdrawFrame()													\
	SDL_RenderPresent(renderer);										\
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
