/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
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

#include <crypto.h>
#include <ioQueue.h>
#include <renderer.h>
#include <state.h>
#include <utils.h>

#include <coreinit/core.h>
#include <coreinit/energysaver.h>
#include <coreinit/foreground.h>
#include <coreinit/ios.h>
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <proc_ui/procui.h>
#include <rpxloader/rpxloader.h>

#include <stdbool.h>

APP_STATE app;
static bool shutdownEnabled = true;
#ifndef NUSSPLI_HBL
static bool channel;
#endif
static bool aroma;
static bool apdEnabled;
static bool apdDisabled = false;

void enableApd()
{
    if(!apdEnabled || !apdDisabled)
        return;

    if(IMEnableAPD() == 0)
    {
        apdDisabled = false;
        debugPrintf("APD enabled!");
    }
    else
        debugPrintf("Error enabling APD!");
}

void disableApd()
{
    if(!apdEnabled || apdDisabled)
        return;

    if(IMDisableAPD() == 0)
    {
        apdDisabled = true;
        debugPrintf("APD disabled!");
    }
    else
        debugPrintf("Error disabling APD!");
}

void enableShutdown()
{
    if(shutdownEnabled)
        return;

    enableApd();
    shutdownEnabled = true;
    debugPrintf("Home key enabled!");
}
void disableShutdown()
{
    if(!shutdownEnabled)
        return;

    disableApd();
    shutdownEnabled = false;
    debugPrintf("Home key disabled!");
}

bool isAroma()
{
    return aroma;
}

#ifndef NUSSPLI_HBL
bool isChannel()
{
    return channel;
}
#endif

uint32_t homeButtonCallback(void *dummy)
{
    if(shutdownEnabled)
    {
        shutdownEnabled = false;
        app = APP_STATE_HOME;
    }

    return 0;
}

void initState()
{
    ProcUIInit(&OSSavesDone_ReadyToRelease);
    OSTime t = OSGetTime();

    app = APP_STATE_RUNNING;

    debugInit();
    debugPrintf("NUSspli " NUSSPLI_VERSION);

    ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED, &homeButtonCallback, NULL, 100);
    OSEnableHomeButtonMenu(false);

    aroma = RPXLoader_InitLibrary() == RPX_LOADER_RESULT_SUCCESS;
#ifndef NUSSPLI_HBL
    channel = OSGetTitleID() == 0x0005000010155373;
#endif

    uint32_t ime;
    if(IMIsAPDEnabledBySysSettings(&ime) == 0)
        apdEnabled = ime == 1;
    else
    {
        debugPrintf("Couldn't read APD sys setting!");
        apdEnabled = false;
    }
    debugPrintf("APD enabled by sys settings: %s (%d)", apdEnabled ? "true" : "false", (uint32_t)ime);
    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));
}

void deinitState()
{
    if(aroma)
        RPXLoader_DeInitLibrary();
}

bool AppRunning(bool mainthread)
{
    if(app == APP_STATE_STOPPING || app == APP_STATE_HOME || app == APP_STATE_STOPPED)
        return false;

    if(mainthread)
    {
        switch(ProcUIProcessMessages(true))
        {
            case PROCUI_STATUS_EXITING:
                // Real exit request from CafeOS
                app = APP_STATE_STOPPED;
                break;
            case PROCUI_STATUS_RELEASE_FOREGROUND:
                // Exit with power button
                shutdownRenderer();
                app = APP_STATE_STOPPING;
                break;
            default:
                // Normal loop execution
                break;
        }
    }

    return true;
}
