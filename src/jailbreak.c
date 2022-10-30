/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2022 V10lator <v10lator@myway.de>                         *
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

#ifdef NUSSPLI_HBL

#include <wut-fixups.h>

#include <stdbool.h>

#include <jailbreak.h>
#include <utils.h>

#include <mocha/mocha.h>

bool jailbreak()
{
    MochaRPXLoadInfo info = {
        .target = LOAD_RPX_TARGET_SD_CARD,
        .filesize = 0,
        .fileoffset = 0,
        .path = "wiiu/apps/NUSspli/NUSspli.rpx",
    };

    MochaUtilsStatus ret = Mocha_LaunchRPX(&info);
    if(ret == MOCHA_RESULT_SUCCESS)
        return true;

    debugPrintf("Mocha_LaunchRPX failed: 0x%08X", ret);
    return false;
}

#endif // ifdef NUSSPLI_HBL
