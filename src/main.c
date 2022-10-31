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

#include <cfw.h>
#include <config.h>
#include <crypto.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <ioQueue.h>
#include <jailbreak.h>
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
#include <staticMem.h>
#include <thread.h>
#include <ticket.h>
#include <titles.h>
#include <updater.h>
#include <utils.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <coreinit/filesystem.h>
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
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <whb/crash.h>

static void drawLoadingScreen(const char *toScreenLog, const char *loadingMsg)
{
    addToScreenLog(toScreenLog);
    startNewFrame();
    textToFrame(0, 0, loadingMsg);
    writeScreenLog(1);
    drawFrame();
}

static void innerMain(bool validCfw)
{
    OSThread *mainThread = OSGetCurrentThread();
    OSSetThreadName(mainThread, "NUSspli");
#ifdef NUSSPLI_DEBUG
    OSSetThreadStackUsage(mainThread);
#endif

    if(validCfw)
    {
        addEntropy(&(mainThread->id), sizeof(uint16_t));
        addEntropy(mainThread->stackStart, 4);

        checkStacks("main");
    }

    KPADInit();
    WPADEnableURCC(true);

    if(initStaticMem())
    {
        if(initFS())
        {
            if(initRenderer())
            {
                readInput(); // bug #95
                char *lerr = NULL;
                if(validCfw)
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
                                            drawLoadingScreen("I/O thread initialized!", "Loading config...");
                                            if(initConfig())
                                            {
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
                                                            checkStacks("main");
                                                            debugPrintf("Deinitializing libraries...");
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
                                            }
                                            else
                                                lerr = "Couldn't load config file!\n\nMost likely your SD card is write locked!";

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
                    lerr = "Unsupported environment.\nEither you're not using Tiramisu/Aroma or your Tiramisu version is out of date.";

                if(lerr != NULL)
                {
                    drawErrorFrame(lerr, ANY_RETURN);
                    showFrame();

                    while(!(vpad.trigger))
                        showFrame();

                    drawByeFrame();
                }

                shutdownRenderer();
                gettextCleanUp();
                debugPrintf("SDL closed");
            }

            deinitFS();
            debugPrintf("Filesystem closed");
        }
        else
            debugPrintf("Error initializing filesystem!");

        shutdownStaticMem();
    }
    else
        debugPrintf("Error inititalizing static memory!");

    debugPrintf("Clearing screen log");
    clearScreenLog();
    KPADShutdown();
}

int main()
{
    initState();

#ifdef NUSSPLI_HBL
    bool jailbreaking;
    uint64_t tid = OSGetTitleID();
#endif
    if(cfwValid())
    {
#ifdef NUSSPLI_HBL
        jailbreaking = !isCemu() && !isAroma() && (tid & 0xFFFFFFFFFFFFF0FF) == 0x000500101004A000; // Mii Maker
        if(jailbreaking)
            jailbreaking = jailbreak();

        if(!jailbreaking)
#endif
            innerMain(true);
    }
    else
    {
        innerMain(false);
#ifdef NUSSPLI_HBL
        jailbreaking = false;
#endif
    }

    deinitCfw();

#ifdef NUSSPLI_DEBUG
    checkStacks("main");
    debugPrintf("Bye!");
    shutdownDebug();
#endif

    if(app != APP_STATE_STOPPED)
    {
#ifdef NUSSPLI_HBL
        if(isAroma())
            SYSLaunchMenu();
        else if(!jailbreaking)
        {
            if((tid & 0xFFFFFFFFFFFFF0FF) == 0x000500101004E000) // Health & Safety
            {
                tid &= 0xFFFFFFFFFFFF0FFF;
                tid |= 0x000000000000A000;
                _SYSLaunchTitleWithStdArgsInNoSplash(tid, NULL);
            }
            else
                SYSRelaunchTitle(0, NULL);
        }
#else
        SYSLaunchMenu();
#endif

        if(app == APP_STATE_HOME)
        {
            app = APP_STATE_RUNNING;
            while(AppRunning(true))
                ;
        }

        if(app == APP_STATE_STOPPING)
            ProcUIDrawDoneRelease();

        int ps;
        do
        {
            ps = ProcUIProcessMessages(true);
            if(ps == PROCUI_STATUS_RELEASE_FOREGROUND)
                ProcUIDrawDoneRelease();
        } while(ps != PROCUI_STATUS_EXITING);
    }

    deinitState();
    ProcUIShutdown();
    return 0;
}
