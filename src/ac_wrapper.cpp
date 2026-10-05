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

#include <ac_wrapper.h>

#include <nn/ac/ac_cpp.h>

// ACGetCloseStatus() takes the parameter as well, the declaration we get just
// does not show it. The C++ declaration next to it does.
extern "C" int32_t acGetCloseStatus(int32_t *status)
{
    NNResult result = nn::ac::GetCloseStatus((nn::ac::Status *)status);
    return result.value;
}