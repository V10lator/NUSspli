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

#include <config.h>
#include <crypto.h>
#include <downloader.h>
#include <filesystem.h>
#include <input.h>
#include <ioQueue.h>
#include <menu/bootScreen.h>
#include <menu/main.h>
#include <menu/utils.h>
#include <notifications.h>
#include <queue.h>
#include <renderer.h>
#include <sanity.h>
#include <ui.h>
#include <updater.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#pragma GCC diagnostic pop

static const char *bootFailure; // message of the step that stopped the boot
static int bootStep; // number of the step that runs next
static bool bootShown; // the loading message of bootStep reached the screen

// One boot step of the table below: loading is on screen while run
// executes, done is the screen log line it leaves behind and error the
// message the caller shows when run fails (NULL for a step that cannot
// fail). undo tears the step down again while the boot unwinds, so the
// table gets walked backwards after the loop is gone.
typedef struct BootStep
{
    const char *loading;
    const char *done;
    const char *error;
    bool (*run)(void);
    void (*undo)(void);
} BootStep;

static bool runMcp(void)
{
    mcpHandle = MCP_Open();
    return mcpHandle != 0;
}

static void undoMcp(void)
{
    MCP_Close(mcpHandle);
    debugPrintf("MCP closed");
}

static bool runConfig(void)
{
    initConfig();
    return true;
}

static const BootStep bootSteps[] = {
    { "Loading Crypto...", "Crypto initialized!", "Couldn't initialize Crypto!", initCrypto, deinitCrypto },
    { "Loading MCP...", "MCP initialized!", "Couldn't initialize MCP!", runMcp, undoMcp },
    { "Checking sanity...", "Sanity checked!", "No support for rebrands, use original NUSspli!", sanityCheck, NULL },
    { "Loading notification system...", "Notification system initialized!", "Couldn't initialize notification system!", initNotifications, deinitNotifications },
    { "Loading downloader...", "Downloader initialized!", "Couldn't initialize downloader!", initDownloader, deinitDownloader },
    { "Loading I/O thread...", "I/O thread initialized!", "Couldn't load I/O thread!", initIOThread, shutdownIOThread },
    { "Loading config...", "Config loaded!", NULL, runConfig, NULL },
    { "Loading SWKBD...", "SWKBD initialized!", "Couldn't initialize SWKBD!", SWKBD_Init, SWKBD_Shutdown },
    { "Loading menu...", NULL, "Couldn't initialize queue!", initQueue, shutdownQueue },
};

#define NUM_BOOT_STEPS ((int)(sizeof(bootSteps) / sizeof(bootSteps[0])))

static void enterBootScreen(void *param)
{
    (void)param;
    bootFailure = NULL;
    bootStep = 0;
    bootShown = false;
}

static void updateBootScreen()
{
    if(bootStep < NUM_BOOT_STEPS)
    {
        // The frame after a step paints its successor, so wait for that
        // paint instead of running into a message the user never saw.
        if(!bootShown)
            return;

        const BootStep *step = &bootSteps[bootStep];
        if(!step->run())
        {
            bootFailure = step->error;
            uiExit(); // the caller unwinds the steps that ran
            return;
        }

        if(step->done != NULL)
            addToScreenLog(step->done);

        ++bootStep;
        bootShown = false;
        uiInvalidate(); // the next loading message replaces this one
        return;
    }

    // Everything is up: this is where the old boot ran the update check
    // before entering the menu.
    checkStacks("main()");
    if(updateCheck())
    {
        uiExit(); // the updater relaunched us, there is no menu to push
        return;
    }

    // The boot screen is transient: hand the stack over to the root screen
    // instead of sitting below it for the rest of the app life.
    checkStacks("main");
    uiPop();
    mainMenu();
}

static void renderBootScreen()
{
    // The picture drawLoadingScreen painted: the message of the step that
    // runs next, or of the last step while the update check talks to the
    // server, above the screen log. uiFrame presents it.
    const BootStep *step = &bootSteps[bootStep < NUM_BOOT_STEPS ? bootStep : NUM_BOOT_STEPS - 1];

    startNewFrame();
    textToFrame(0, 0, step->loading);
    writeScreenLog(1);
    bootShown = true;
}

static const UIScreen bootScreen = {
    .name = "boot",
    .enter = enterBootScreen,
    .update = updateBootScreen,
    .render = renderBootScreen,
};

void pushBootScreen()
{
    uiPush(&bootScreen, NULL);
}

const char *bootError()
{
    return bootFailure;
}

void undoBootSteps()
{
    for(int i = bootStep - 1; i >= 0; --i)
    {
        if(bootSteps[i].undo != NULL)
            bootSteps[i].undo();
    }
}
