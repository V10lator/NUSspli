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

#include <utils.h>

#define VALUE_A 0xE3A00000 // mov r0, #0
#define VALUE_B 0xE12FFF1E // bx lr
#define VALUE_C 0x20004770 // mov r0, #0; bx lr
#define VALUE_D 0x20002000 // mov r0, #0; mov r0, #0

static bool mochaReady = false;
static bool cemu = false;
static const uint32_t addys[8] = {
    0x05054D6C,
    0x05054D70,

    0x05052A90,
    0x05052A94,

    0x05014CAC,

    0x05052C44,
    0x05052C48,

    0x0500A818,
};
static const uint32_t patchValues[8] = {
    VALUE_A,
    VALUE_B,

    VALUE_A,
    VALUE_B,

    VALUE_C,

    VALUE_A,
    VALUE_B,

    VALUE_D,
};
static uint32_t origValues[8];

extern FSClient *__wut_devoptab_fs_client;

bool cfwValid()
{
    cemu = OSGetTitleID() == 0x0000000000000000;
    if(cemu)
        return true;

    mochaReady = Mocha_InitLibrary() == MOCHA_RESULT_SUCCESS;
    bool ret = mochaReady;
    if(ret)
    {
        ret = Mocha_UnlockFSClient(__wut_devoptab_fs_client) == MOCHA_RESULT_SUCCESS;
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
                if(ret)
                {
                    for(int i = 0; i < 8; ++i)
                    {
                        s = Mocha_IOSUKernelRead32(addys[i], origValues + i);
                        if(s != MOCHA_RESULT_SUCCESS && --i >= 0)
                            goto restoreIOSU;

                        s = Mocha_IOSUKernelWrite32(addys[i], patchValues[i]);
                        if(s != MOCHA_RESULT_SUCCESS)
                            goto restoreIOSU;

                        continue;
                    restoreIOSU:
                        for(; i >= 0; --i)
                            Mocha_IOSUKernelWrite32(addys[i], origValues[i]);

                        debugPrintf("libmocha error: %s", Mocha_GetStatusStr(s));
                        return false;
                    }
                }
            }
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
    {
        for(int i = 0; i < 8; ++i)
            Mocha_IOSUKernelWrite32(addys[i], origValues[i]);

        Mocha_DeInitLibrary();
    }
}