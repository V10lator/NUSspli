/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2026 V10lator <v10lator@myway.de>                       *
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
 * with this program.  If not, see <http://www.gnu.org/licenses/>.        *
 ***************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // States of an asynchronous close, see acGetCloseStatus() below.
    enum
    {
        AC_STATUS_FAILED = -1,
        AC_STATUS_OK = 0,
        AC_STATUS_PROCESSING = 1,
    };

    // The declaration of ACGetCloseStatus() we compile against has no
    // out-parameter, so from C there is no way to reach the state it reports.
    // This writes that state to status and returns the result of the query.
    int32_t acGetCloseStatus(int32_t *status);

#ifdef __cplusplus
}
#endif