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
#include <whb/gfx.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include <crypto.h>
#include <file.h>
#include <input.h>
#include <renderer.h>
#include <romfs.h>
#include <swkbd_wrapper.h>
#include <utils.h>
#include <menu/utils.h>

#define SSAA            8
#define MAX_OVERLAYS    8

static const SDL_Point screen = { .x = 1280, .y = 720 };
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static FC_Font *font = NULL;
static Mix_Chunk *backgroundMusic = NULL;

static int32_t spaceWidth;

static SDL_Texture *frameBuffer;
static SDL_Texture *errorOverlay[MAX_OVERLAYS];
static SDL_Texture *arrowTex;
static SDL_Texture *checkmarkTex;
static SDL_Texture *tabTex;
static SDL_Texture *flagTex[6];
static SDL_Texture *barTex;
static SDL_Texture *bgTex;
static SDL_Texture *byeTex;

#define screenColorToSDLcolor(color) (SDL_Color){ .a = color & 0xFFu, .b = (color & 0x0000FF00u) >> 8, .g = (color & 0x00FF0000u) >> 16, .r = (color & 0xFF000000u) >> 24 }

void textToFrameCut(int line, int column, const char *str, int maxWidth)
{
	if(font == NULL)
		return;

	SDL_Rect text;
	text.w = FC_GetWidth(font, str);

	int i = strlen(str);
	char cpy[i + 1];

	if(maxWidth != 0 && text.w > maxWidth)
	{
		strcpy(cpy, str);
		cpy[i--] = '\0';
		cpy[i--] = '.';
		cpy[i--] = '.';
		cpy[i] = '.';
		text.w = FC_GetWidth(font, cpy);

		while(text.w > maxWidth)
		{
			cpy[i + 2] = '\0';
			cpy[--i] = '.';
			text.w = FC_GetWidth(font, cpy);
		}

		str = cpy;
	}

	text.h = FONT_SIZE;
	if(text.w == 0 || text.h == 0)
		return;

	line += 1;
	line *= FONT_SIZE;
	line -= 7;
	text.y = line;

	switch(column)
	{
		case ALIGNED_CENTER:
			text.x = (screen.x >> 1) - (text.w >> 1);
			break;
		case ALIGNED_RIGHT:
			text.x = screen.x - text.w - FONT_SIZE;
			break;
		default:
			column *= spaceWidth;
			text.x = column + FONT_SIZE;
	}

	FC_DrawBoxColor(font, renderer, text, screenColorToSDLcolor(SCREEN_COLOR_WHITE), str);
}

void lineToFrame(int column, uint32_t color)
{
	if(font == NULL)
		return;

	SDL_Rect line =
	{
		.x = FONT_SIZE,
		.y = ((column + 1) * FONT_SIZE) + ((FONT_SIZE >> 1) - 1),
		.w = screen.x - (FONT_SIZE << 1),
		.h = 3,
	};

	SDL_Color co = screenColorToSDLcolor(color);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
	SDL_RenderFillRect(renderer, &line);
}

void boxToFrame(int lineStart, int lineEnd)
{
	if(font == NULL)
		return;

	int ty = ((lineStart + 1) * FONT_SIZE) + ((FONT_SIZE >> 1) - 1);
	int tw = screen.x - (FONT_SIZE << 1);
	SDL_Rect box =
	{
		.x = FONT_SIZE,
		.y = ty,
		.w = tw,
		.h = 3,
	};
	SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_GRAY);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
	
	// Horizontal lines
	SDL_RenderFillRect(renderer, &box);

	box.y = ((lineEnd + 1) * FONT_SIZE) + ((FONT_SIZE >> 1) - 1) - 3,
	SDL_RenderFillRect(renderer, &box);
	
	// Vertical lines
	box.y = ty;
	box.w = 3;
	box.h = (lineEnd - lineStart) * FONT_SIZE;
	SDL_RenderFillRect(renderer, &box);

	box.x += tw - 3;
	SDL_RenderFillRect(renderer, &box);
	
	// Background - we paint it on top of the gray lines as they look better that way
	box.x = FONT_SIZE + 2;
	box.y += 2;
	box.w = tw;
	box.w -= 3;
	box.h -= 3;
	co = screenColorToSDLcolor(SCREEN_COLOR_BLACK);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 64);
	SDL_RenderFillRect(renderer, &box);
}

void barToFrame(int line, int column, uint32_t width, double progress)
{
	if(font == NULL)
		return;
	
	SDL_Rect box =
	{
		.x = FONT_SIZE + (column * spaceWidth),
		.y = ((line + 1) * FONT_SIZE) - 2,
		.w = ((int)width) * spaceWidth,
		.h = FONT_SIZE,
	};
	SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_GRAY);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
	SDL_RenderFillRect(renderer, &box);

	box.x += 2;
	box.y += 2;
	box.h -= 4;
	int w = box.w - 4;

	char text[5];
	sprintf(text, "%d%%%%", (int)progress);

	progress /= 100.0D;
	progress *= w;
	box.w = progress;
	
	SDL_RenderCopy(renderer, barTex, NULL, &box);
	
	box.x += box.w;
	box.w = w - box.w;
	
	co = screenColorToSDLcolor(SCREEN_COLOR_BLACK);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 64);
	SDL_RenderFillRect(renderer, &box);
	
	textToFrame(line, column + (width >> 1) - (strlen(text) >> 1), text);
}

void arrowToFrame(int line, int column)
{
	if(font == NULL)
		return;
	
	line += 1;
	line *= FONT_SIZE;
	column *= spaceWidth;
	column += spaceWidth;
	
	SDL_Rect arrow =
	{
		.x = column + FONT_SIZE,
		.y = line,
	};
	SDL_QueryTexture(arrowTex, NULL, NULL, &(arrow.w), &(arrow.h));
	SDL_RenderCopy(renderer, arrowTex, NULL, &arrow);
}

void checkmarkToFrame(int line, int column)
{
	if(font == NULL)
		return;
	
	line += 1;
	line *= FONT_SIZE;
	column *= spaceWidth;
	column += spaceWidth >> 1;

	SDL_Rect cm =
	{
		.x = column + FONT_SIZE,
		.y = line,
	};
	SDL_QueryTexture(checkmarkTex, NULL, NULL, &(cm.w), &(cm.h));
	SDL_RenderCopy(renderer, checkmarkTex, NULL, &cm);
}

static inline SDL_Texture *getFlagData(TITLE_REGION flag)
{
	switch(flag)
	{
		case TITLE_REGION_ALL:
			return flagTex[0];
		case TITLE_REGION_EUR:
			return flagTex[1];
		case TITLE_REGION_USA:
			return flagTex[2];
		case TITLE_REGION_JAP:
			return flagTex[3];
		case TITLE_REGION_EUR | TITLE_REGION_USA:
			return flagTex[4];
		default:
			return flagTex[5];
	}
}

void flagToFrame(int line, int column, TITLE_REGION flag)
{
	if(font == NULL)
		return;
	
	line += 1;
	line *= FONT_SIZE;
	column *= spaceWidth;
	column += spaceWidth >> 1;
	
	SDL_Texture *fd = getFlagData(flag);
	SDL_Rect fl =
	{
		.x = column + FONT_SIZE,
		.y = line,
	};
	SDL_QueryTexture(fd, NULL, NULL, &(fl.w), &(fl.h));
	SDL_RenderCopy(renderer, fd, NULL, &fl);
}

void tabToFrame(int line, int column, char *label, bool active)
{
	if(font == NULL)
		return;
	
	line *= FONT_SIZE;
	line += 20;
	column *= 240;
	column += 13;
	
	SDL_Rect rect =
	{
		.x = column + FONT_SIZE,
		.y = line,
	};
	SDL_QueryTexture(tabTex, NULL, NULL, &(rect.w), &(rect.h));
	SDL_RenderCopy(renderer, tabTex, NULL, &rect);

	rect.x += rect.w >> 1;
	rect.x -= FC_GetWidth(font, label) >> 1;
	rect.y += 20;
	rect.y -= FONT_SIZE >> 1;

	SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_WHITE);
	if(!active)
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
	for( ; i < MAX_OVERLAYS + 1; i++)
		if(i < MAX_OVERLAYS && errorOverlay[i] == NULL)
			break;

	if(i == MAX_OVERLAYS)
		return -2;

	int w = FC_GetWidth(font, err);
	int h = FC_GetColumnHeight(font, w, err);
	if(w == 0 || h == 0)
		return -4;

	errorOverlay[i] = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, screen.x, screen.y);
	if(errorOverlay[i] == NULL)
		return -5;

	SDL_SetTextureBlendMode(errorOverlay[i], SDL_BLENDMODE_BLEND);
	SDL_SetRenderTarget(renderer, errorOverlay[i]);

	SDL_Color co = screenColorToSDLcolor(SCREEN_COLOR_BLACK);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, 0xC0);
	SDL_RenderClear(renderer);

	int x = (screen.x >> 1) - (w >> 1);
	int y = (screen.y >> 1) - (h >> 1);

	SDL_Rect text =
	{
		.x = x,
		.y = y,
		.w = w,
		.h = h,
	};

	text.x -= FONT_SIZE >> 1;
	text.y -= FONT_SIZE >> 1;
	text.w += FONT_SIZE;
	text.h += FONT_SIZE;
	co = screenColorToSDLcolor(SCREEN_COLOR_RED);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
	SDL_RenderFillRect(renderer, &text);

	SDL_Rect text2 = text; // For some reason transparent pixels will corrupt if we don't copy
	text2.x += 2;
	text2.y += 2;
	text2.w -= 4;
	text2.h -= 4;
	co = screenColorToSDLcolor(SCREEN_COLOR_D_RED);
	SDL_SetRenderDrawColor(renderer, co.r, co.g, co.b, co.a);
	SDL_RenderFillRect(renderer, &text2);

	text2.x = x;
	text2.y = y;
	text2.w = w;
	text2.h = h;
	FC_DrawBoxColor(font, renderer, text2, screenColorToSDLcolor(SCREEN_COLOR_WHITE), err);

	SDL_SetRenderTarget(renderer, frameBuffer);
	drawFrame();
	return i;
}

void removeErrorOverlay(int id)
{
	OSTick t = OSGetTick();
	addEntropy(&t, sizeof(OSTick));
	if(id < 0 || id >= MAX_OVERLAYS || errorOverlay[id] == NULL)
		return;
	
	SDL_DestroyTexture(errorOverlay[id]);
	errorOverlay[id] = NULL;
	drawFrame();
}

static inline bool loadTexture(const char *path, SDL_Texture **out)
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
		SDL_RWops *rw = SDL_RWFromMem(ttf, size);
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

			bgTex = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, screen.x, screen.y);
			SDL_SetRenderTarget(renderer, bgTex);
			SDL_RenderCopy(renderer, tt, NULL, NULL);
			SDL_DestroyTexture(tt);

			SDL_SetRenderTarget(renderer, frameBuffer);

			const char *tex;
			for(int i = 0; i < 6; i++)
			{
				switch(i)
				{
					case 0:
						tex = ROMFS_PATH "textures/flags/multi.png";
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
						tex = ROMFS_PATH "textures/flags/unk.png";
						break;
				}
				loadTexture(tex, flagTex + i);
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

void initRenderer()
{
	if(font != NULL)
		return;

	for(int i = 0; i < MAX_OVERLAYS; i++)
		errorOverlay[i] = NULL;
	
	if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) != 0)
	{
		debugPrintf("SDL init error: %s", SDL_GetError());
		return;
	}

	window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, screen.x, screen.y, 0);
	if(window == NULL)
		return;

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if(renderer == NULL)
		return;

	if(frameBuffer == NULL)
		frameBuffer = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET, screen.x, screen.y);

	if(frameBuffer == NULL)
		return;

	SDL_SetRenderTarget(renderer, frameBuffer);

	OSTime t = OSGetSystemTime();
	if(Mix_Init(MIX_INIT_MP3))
	{
		FILE *f = fopen(ROMFS_PATH "audio/bg.mp3", "rb");
		if(f != NULL)
		{
			size_t fs = getFilesize(f);
			void *buf = MEMAllocFromDefaultHeap(fs);
			if(buf != NULL)
			{
				if(fread(buf, fs, 1, f) == 1)
				{
					if(Mix_OpenAudio(22050, AUDIO_S16MSB, 2, 4096) == 0)
					{
						SDL_RWops *rw = SDL_RWFromMem(buf, fs);
						backgroundMusic = Mix_LoadWAV_RW(rw, true);
						if(backgroundMusic != NULL)
						{
							Mix_VolumeChunk(backgroundMusic, 15);
							if(Mix_PlayChannel(0, backgroundMusic, -1) == 0)
								goto audioRunning;

							Mix_FreeChunk(backgroundMusic);
							backgroundMusic = NULL;
						}
						SDL_RWclose(rw);
						Mix_CloseAudio();
					}
				}
				MEMFreeToDefaultHeap(buf);
			}
			fclose(f);
		}
	}

audioRunning:
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	loadTexture(ROMFS_PATH "textures/goodbye.png", &byeTex);

	t = OSGetSystemTime() - t;
	addEntropy(&t, sizeof(OSTime));
	
	TTF_Init();
	resumeRenderer();
	if(font == NULL)
	{
		debugPrintf("SDL error!");
		return;
	}

	addToScreenLog("SDL initialized!");
	startNewFrame();
	textToFrame(0, 0, "Loading...");
	writeScreenLog();
	drawFrame();
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
	
	for(int i = 0; i < 6; i++)
		SDL_DestroyTexture(flagTex[i]);
	
	font = NULL;
}

void shutdownRenderer()
{
	for(int i = 0; i < MAX_OVERLAYS; i++)
		removeErrorOverlay(i);
	
	if(font != NULL)
	{
		SDL_SetRenderTarget(renderer, NULL);
		colorStartNewFrame(SCREEN_COLOR_BLUE);

		SDL_Rect bye;
		SDL_QueryTexture(byeTex, NULL, NULL, &(bye.w), &(bye.h));
		bye.x = (screen.x >> 1) - (bye.w >> 1);
		bye.y = (screen.y >> 1) - (bye.h >> 1);

		SDL_RenderCopy(renderer, byeTex, NULL, &bye);
		SDL_RenderPresent(renderer);
		clearFrame();
		pauseRenderer();
	}

	if(frameBuffer != NULL)
	{
		SDL_DestroyTexture(frameBuffer);
		frameBuffer = NULL;
	}

	if(renderer != NULL)
	{
		SDL_DestroyRenderer(renderer);
		renderer = NULL;
	}

	if(window != NULL)
	{
		SDL_DestroyWindow(window);
		window = NULL;
	}
	
	if(backgroundMusic != NULL)
	{
		debugPrintf("Stopping background music");
		Mix_HaltChannel(0);
		Mix_FreeChunk(backgroundMusic);
		backgroundMusic = NULL;

	}

	int fr;
	uint16_t fo;
	int ch;
	int c = Mix_QuerySpec(&fr, &fo, &ch);
	for(int i = 0; i < c; i++)
		Mix_CloseAudio();

// TODO:
	TTF_Quit();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
//	SDL_QuitSubSystem(SDL_INIT_VIDEO);
//	SDL_Quit();
}

void colorStartNewFrame(uint32_t color)
{
	if(font == NULL)
		return;
	
	clearFrame();
	
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
	WHBGfxBeginRender();
	readInput();
}

#define predrawFrame()									\
	if(font == NULL)									\
		return;											\
														\
	SDL_SetRenderTarget(renderer, NULL);				\
	SDL_RenderCopy(renderer, frameBuffer, NULL, NULL);

#define postdrawFrame()												\
	for(int i = 0; i < MAX_OVERLAYS; i++)							\
		if(errorOverlay[i] != NULL)									\
			SDL_RenderCopy(renderer, errorOverlay[i], NULL, NULL);	\
																	\
	SDL_RenderPresent(renderer);									\
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