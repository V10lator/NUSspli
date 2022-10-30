/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
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

#include <coreinit/title.h>
#include <mocha/mocha.h>

static bool mochaReady = false;
static bool cemu = false;

bool cfwValid()
{
    cemu = OSGetTitleID() == 0x0000000000000000;
    if(cemu)
        return true;

    mochaReady = Mocha_InitLibrary() == MOCHA_RESULT_SUCCESS;
    bool ret = mochaReady;
    if(ret)
    {
        WiiUConsoleOTP otp;
        ret = Mocha_ReadOTP(&otp) == MOCHA_RESULT_SUCCESS;
        if(ret)
        {
            MochaRPXLoadInfo info = {
                .target = 0xDEADBEEF,
                .filesize = 0,
                .fileoffset = 0,
                .path = "dummy"
            };

            MochaUtilsStatus s = Mocha_LaunchRPX(&info);
            ret = s != MOCHA_RESULT_UNSUPPORTED_API_VERSION && s != MOCHA_RESULT_UNSUPPORTED_COMMAND;
        }
    }

    return ret;
}

bool isCemu()
{
    return cemu;
}

void deinitCfw()
{
    if(mochaReady)
        Mocha_DeInitLibrary();
}
