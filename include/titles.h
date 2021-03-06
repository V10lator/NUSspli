/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020 V10lator <v10lator@myway.de>                         *
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

#ifndef NUSSPLI_TITLES_H
#define NUSSPLI_TITLES_H

#include <wut-fixups.h>

#ifdef __cplusplus
	extern "C" {
#endif

typedef enum
{
	TID_HIGH_GAME =				0x00050000,
	TID_HIGH_DEMO =				0x00050002,
	TID_HIGH_SYSTEM_APP =		0x00050010,
	TID_HIGH_SYSTEM_DATA =		0x0005001B,
	TID_HIGH_SYSTEM_APPLET =	0x00050030,
	TID_HIGH_VWII_IOS =			0x00000007,
	TID_HIGH_VWII_SYSTEM_APP =	0x00070002,
	TID_HIGH_VWII_SYSTEM =		0x00070008,
	TID_HIGH_DLC =				0x0005000C,
	TID_HIGH_UPDATE =			0x0005000E,
} TID_HIGH;

char *tid2name(const char *tid);
bool initTitles();
void clearTitles();

// TODO

#ifdef __cplusplus
	}
#endif

#endif // ifndef NUSSPLI_TITLES_H
