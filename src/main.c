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

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <cfw.h>
#include <config.h>
#include <crypto.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <ioQueue.h>
#include <localisation.h>
#include <menu/download.h>
#include <menu/main.h>
#include <menu/utils.h>
#include <notifications.h>
#include <osdefs.h>
#include <otp.h>
#include <queue.h>
#include <renderer.h>
#include <sanity.h>
#include <state.h>
#include <thread.h>
#include <ticket.h>
#include <titles.h>
#include <updater.h>
#include <utils.h>

#include <SDL2/SDL.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/foreground.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <mocha/mocha.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#pragma GCC diagnostic pop

static void drawLoadingScreen(const char *toScreenLog, const char *loadingMsg)
{
    addToScreenLog(toScreenLog);
    startNewFrame();
    textToFrame(0, 0, loadingMsg);
    writeScreenLog(1);
    drawFrame();
}

static void innerMain()
{
    OSThread *mainThread = OSGetCurrentThread();
    OSSetThreadName(mainThread, "NUSspli");
#ifdef NUSSPLI_DEBUG
    OSSetThreadStackUsage(mainThread);
#endif

    const char *cfwError = cfwValid();
    if(cfwError == NULL)
    {
        addEntropy(&(mainThread->id), sizeof(uint16_t));
        addEntropy(mainThread->stackStart, 4);

        checkStacks("main");
    }

    KPADInit();
    WPADEnableURCC(true);

    if(initFS(cfwError == NULL))
    {
        if(initRenderer())
        {
            initProcUICallbacks(); // ProcUI only runs once SDL claimed it
            readInput(); // bug #95
            char *lerr = NULL;
            if(cfwError == NULL)
            {
                if(OSSetThreadPriority(mainThread, THREAD_PRIORITY_HIGH))
                    addToScreenLog("Changed main thread priority!");
                else
                    addToScreenLog("WARNING: Error changing main thread priority!");

                startNewFrame();
                textToFrame(0, 0, "Loading Crypto...");
                writeScreenLog(1);
                drawFrame();

                if(initCrypto())
                {
                    drawLoadingScreen("Crypto initialized!", "Loading MCP...");
                    mcpHandle = MCP_Open();
                    if(mcpHandle != 0)
                    {
                        drawLoadingScreen("MCP initialized!", "Checking sanity...");
                        if(sanityCheck())
                        {
                            drawLoadingScreen("Sanity checked!", "Loading notification system...");
                            if(initNotifications())
                            {
                                drawLoadingScreen("Notification system initialized!", "Loading downloader...");
                                if(initDownloader())
                                {
                                    drawLoadingScreen("Downloader initialized!", "Loading I/O thread...");
                                    if(initIOThread())
                                    {
                                        initFSSpace();
                                        drawLoadingScreen("I/O thread initialized!", "Loading config...");
                                        initConfig();
                                        drawLoadingScreen("Config loaded!", "Loading SWKBD...");
                                        if(SWKBD_Init())
                                        {
                                            drawLoadingScreen("SWKBD initialized!", "Loading menu...");
                                            if(initQueue())
                                            {
                                                checkStacks("main()");
                                                if(!updateCheck())
                                                {
                                                    checkStacks("main");
                                                    mainMenu(); // main loop
                                                    drawByeFrame();
                                                    checkStacks("main");
                                                    debugPrintf("Deinitializing libraries...");

                                                    if(app == APP_STATE_STOPPING)
                                                    {
                                                        // Power button: SDL deferred ProcUIDrawDoneRelease() into the
                                                        // next pump so the background events can be processed first,
                                                        // but the menu loop stopped pumping with APP_STATE_STOPPING.
                                                        // Run the deferred step, else CafeOS never completes the
                                                        // release handshake and never reports PROCUI_STATUS_EXITING
                                                        // -- SDL_Quit() would wait for it forever.
                                                        SDL_PumpEvents();
                                                    }
                                                }
                                                else
                                                    drawByeFrame();

                                                shutdownQueue();
                                            }
                                            else
                                                lerr = "Couldn't initialize queue!";

                                            SWKBD_Shutdown();
                                            debugPrintf("SWKBD closed");
                                        }
                                        else
                                            lerr = "Couldn't initialize SWKBD!";

                                        saveConfig(false);
                                        shutdownIOThread();
                                        debugPrintf("I/O thread closed");
                                    }
                                    else
                                        lerr = "Couldn't load I/O thread!";

                                    deinitDownloader();
                                }
                                else
                                    lerr = "Couldn't initialize downloader!";

                                deinitNotifications();
                                debugPrintf("Notification system closed");
                            }
                            else
                                lerr = "Couldn't initialize notification system!";
                        }
                        else
                            lerr = "No support for rebrands, use original NUSspli!";

                        MCP_Close(mcpHandle);
                        debugPrintf("MCP closed");
                    }
                    else
                        lerr = "Couldn't initialize MCP!";

                    deinitCrypto();
                    debugPrintf("Crypto closed");
                }
                else
                    lerr = "Couldn't initialize Crypto!";
            }
            else
                lerr = (char *)cfwError;

            if(lerr != NULL)
            {
                drawErrorFrame(lerr, ANY_RETURN);
                showFrame();

                while(!(vpad.trigger) && AppRunning(true))
                    showFrame();

                drawByeFrame();
            }

            if(cfwError == NULL)
                checkSpaceThread();

            shutdownRenderer();
            locCleanUp();
            debugPrintf("SDL closed");
        }

        deinitFS(cfwError == NULL);
        debugPrintf("Filesystem closed");
    }
    else
        debugPrintf("Error initializing filesystem!");

    debugPrintf("Clearing screen log");
    clearScreenLog();
    KPADShutdown();
}

int main()
{
    initState();
    innerMain();

    deinitCfw();

#ifdef NUSSPLI_DEBUG
    checkStacks("main");
    debugPrintf("Bye!");
    shutdownDebug();
#endif

    if(launchingTitle() && app != APP_STATE_STOPPED)
    {
        // A title launch is pending: wait for CafeOS to report EXITING so the
        // hand-off to the Wii U menu inside SDL_Quit() cannot cancel it.
        SDL_Event event;
        bool sdlQuit = false;
        while(!sdlQuit)
        {
            SDL_PumpEvents();
            while(SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT))
            {
                if(event.type == SDL_QUIT)
                    sdlQuit = true;
            }
        }
    }

    // SDL claimed ProcUI while initializing the video driver: SDL_Quit() hands
    // back to the Wii U menu, drains ProcUI until EXITING, tears GX2 down and
    // shuts ProcUI down.
    SDL_Quit();
    deinitState();
    return 0;
}
