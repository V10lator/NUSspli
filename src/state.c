/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
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

#include <stdbool.h>

#include <cfw.h>
#include <crypto.h>
#include <exception.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/energysaver.h>
#include <coreinit/foreground.h>
#include <coreinit/mcp.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <nn/acp/client.h>
#include <nn/acp/title.h>
#include <proc_ui/procui.h>
#include <rpxloader/rpxloader.h>
#include <sysapp/launch.h>
#pragma GCC diagnostic pop

volatile APP_STATE app;
static bool shutdownEnabled = true;
static bool channel;
static bool aroma;
static bool apdEnabled;
static uint32_t apdDisabledCount = 0;
static bool launching = false;

void enableApd()
{
    if(!apdEnabled)
        return;

    if(apdDisabledCount == 0)
    {
        debugPrintf("Tried to enable APD while already enabled!");
        return;
    }

    debugPrintf("enableApd(): apdDisabledCount = %u", apdDisabledCount);

    if(--apdDisabledCount == 0)
    {
        if(IMEnableAPD() == 0)
            debugPrintf("APD enabled!");
        else
            debugPrintf("Error enabling APD!");
    }
}

void disableApd()
{
    if(!apdEnabled)
        return;

    if(apdDisabledCount++ == 0)
    {
        if(IMDisableAPD() == 0)
            debugPrintf("APD disabled!");
        else
            debugPrintf("Error disabling APD!");
    }

    debugPrintf("APD disable request #%u", apdDisabledCount);
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

bool isChannel()
{
    return channel;
}

uint32_t homeButtonCallback(void *dummy)
{
    if(shutdownEnabled && dummy == (void *)false && showExitOverlay())
    {
        shutdownEnabled = false;
        app = APP_STATE_HOME;
        drawByeFrame();
    }

    return 0;
}

void initState()
{
    ProcUIInit(&OSSavesDone_ReadyToRelease);
    initExceptionHandler();
    OSTime t = OSGetTime();

    app = APP_STATE_RUNNING;

    debugInit();
    debugPrintf("NUSspli " NUSSPLI_VERSION);

    ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED, &homeButtonCallback, (void *)false, 100);
    OSEnableHomeButtonMenu(false);
    ACPInitialize();

    aroma = RPXLoader_InitLibrary() == RPX_LOADER_RESULT_SUCCESS;
    channel = OSGetTitleID() == 0x0005000010155373;

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

    if(apdDisabledCount != 0)
    {
        debugPrintf("APD disabled while exiting!");
        apdDisabledCount = 1;
        enableApd();
    }

    deinitExceptionHandler();
    ACPFinalize();
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
                return false;
            case PROCUI_STATUS_RELEASE_FOREGROUND:
                // Exit with power button
                app = APP_STATE_STOPPING;
                drawByeFrame();
                return false;
            default:
                // Normal loop execution
                break;
        }
    }

    return true;
}

void launchTitle(MCPTitleListType *title)
{
    launching = true;
    ACPAssignTitlePatch(title);
    _SYSLaunchTitleWithStdArgsInNoSplash(title->titleId, NULL);
}

void relaunch()
{
    launching = true;
    SYSRelaunchTitle(0, NULL);
}

bool launchingTitle()
{
    return launching;
}
