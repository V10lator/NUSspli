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
#include <crypto.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <localisation.h>
#include <menu/bootScreen.h>
#include <menu/main.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <thread.h>
#include <titles.h>
#include <ui.h>
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
            const char *lerr = cfwError;
            if(lerr == NULL)
            {
                if(OSSetThreadPriority(mainThread, THREAD_PRIORITY_HIGH))
                    addToScreenLog("Changed main thread priority!");
                else
                    addToScreenLog("WARNING: Error changing main thread priority!");

                pushBootScreen();
                uiRun(); // the boot steps and then the menu: the one loop
                lerr = bootError();
            }

            if(lerr != NULL)
            {
                drawErrorFrame(lerr, ANY_RETURN);
                uiWaitKey();
                uiDrawByeFrame();
            }

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

            // The pyramid of calls unwound itself by returning from every
            // nested level. The boot table does the same by being walked
            // backwards over the steps that actually ran.
            undoBootSteps();

            // The tear down above still writes files, and a write error
            // the I/O thread posted there has no frame left to show it.
            uiDrainEvents();

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
