/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2021 V10lator <v10lator@myway.de>                    *
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

// This is to fix some WUT bugs.
// Inclde it in every other source and header files and DO NOT GUARD IT with ifdefs!

#pragma once

//#ifndef FD_SETSIZE
#define FD_SETSIZE 32
//#endif

#ifdef __cplusplus
extern "C"
{
#endif

    // // Functions from https://github.com/devkitPro/wut/blob/master/libraries/wutsocket/wut_socket_common.c
    void __attribute__((weak)) __init_wut_socket();
    void __attribute__((weak)) __fini_wut_socket();

#ifdef __cplusplus
}
#endif
