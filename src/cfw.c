/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2024 V10lator <v10lator@myway.de>                    *
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

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include <state.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/title.h>
#include <mocha/mocha.h>
#include <rpxloader/rpxloader.h>
#pragma GCC diagnostic pop

#define VALUE_A 0xE3A00000 // mov r0, #0
#define VALUE_B 0xE12FFF1E // bx lr

#define CFW_ERR "Unsupported environment.\nEither you're not using Tiramisu/Aroma or your Tiramisu version is out of date.\n\n"

static bool mochaReady = false;
static const uint32_t addys[6] = {
    // Cached cert check
    0x05054D6C,
    0x05054D70,
    // Cert verification
    0x05052A90,
    0x05052A94,
    // IOSC_VerifyPubkeySign
    0x05052C44,
    0x05052C48,
};
static int oi = 0;
static uint32_t origValues[6];
static char *cfwError = NULL;

static const char *printCfwError(const char *str, ...)
{
    cfwError = MEMAllocFromDefaultHeap(sizeof(char) * 1024);
    if(cfwError == NULL)
        return CFW_ERR;

    OSBlockMove(cfwError, CFW_ERR, sizeof(CFW_ERR) - 1, false);

    va_list va;
    va_start(va, str);
    vsnprintf(cfwError + (sizeof(CFW_ERR) - 1), 1024 - sizeof(CFW_ERR), str, va);
    va_end(va);

    return cfwError;
}

const char *cfwValid()
{
    MochaUtilsStatus s = Mocha_InitLibrary();
    mochaReady = s == MOCHA_RESULT_SUCCESS;
    if(!mochaReady)
        return printCfwError("Can't init libmocha: 0x%8X", s);

    WiiUConsoleOTP otp;
    s = Mocha_ReadOTP(&otp);
    if(s != MOCHA_RESULT_SUCCESS)
        return printCfwError("Can't acces OTP: %s", Mocha_GetStatusStr(s));

    MochaRPXLoadInfo info = {
        .target = 0xDEADBEEF,
        .filesize = 0,
        .fileoffset = 0,
        .path = "dummy"
    };

    s = Mocha_LaunchRPX(&info);
    if(s == MOCHA_RESULT_UNSUPPORTED_API_VERSION || s == MOCHA_RESULT_UNSUPPORTED_COMMAND)
        return printCfwError("Can't dummy load RPX: %s", Mocha_GetStatusStr(s));

    if(!isChannel())
    {
        char path[FS_MAX_PATH];
        RPXLoaderStatus rs = RPXLoader_GetPathOfRunningExecutable(path, FS_MAX_PATH);
        if(rs != RPX_LOADER_RESULT_SUCCESS)
            return printCfwError("RPXLoader error: %s", RPXLoader_GetStatusStr(rs));
    }

    for(; oi < 6; ++oi)
    {
        s = Mocha_IOSUKernelRead32(addys[oi], origValues + oi);
        if(s != MOCHA_RESULT_SUCCESS)
            goto restoreIOSU;

        s = Mocha_IOSUKernelWrite32(addys[oi], oi % 2 == 0 ? VALUE_A : VALUE_B);
        if(s != MOCHA_RESULT_SUCCESS)
            goto restoreIOSU;

        continue;
    restoreIOSU:
        for(--oi; oi >= 0; --oi)
            Mocha_IOSUKernelWrite32(addys[oi], origValues[oi]);

        return printCfwError("libmocha error: %s", Mocha_GetStatusStr(s));
    }

    return NULL;
}

void deinitCfw()
{
    if(mochaReady)
    {
        for(int i = 0; i < oi; ++i)
            Mocha_IOSUKernelWrite32(addys[i], origValues[i]);

        Mocha_DeInitLibrary();
    }

    if(cfwError != NULL)
        MEMFreeToDefaultHeap(cfwError);
}
