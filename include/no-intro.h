/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2023 V10lator <v10lator@myway.de>                         *
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

#include <ticket.h>
#include <tmd.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        char *path;
        bool hadTicket;
        bool tmdFound;
        uint32_t ac;
    } NO_INTRO_DATA;

    void destroyNoIntroData(NO_INTRO_DATA *data);
    void revertNoIntro(NO_INTRO_DATA *data);
    NO_INTRO_DATA *transformNoIntro(const char *path);

#ifdef __cplusplus
}
#endif
