/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.             *
 ***************************************************************************/

#include <wut-fixups.h>

#include <config.h>
#include <crypto.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <iosuhaxx.h>
#include <ioQueue.h>
#include <jailbreak.h>
#include <notifications.h>
#include <osdefs.h>
#include <otp.h>
#include <renderer.h>
#include <romfs-wiiu.h>
#include <sanity.h>
#include <staticMem.h>
#include <state.h>
#include <thread.h>
#include <ticket.h>
#include <titles.h>
#include <updater.h>
#include <utils.h>
#include <menu/download.h>
#include <menu/main.h>
#include <menu/utils.h>

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
#include <iosuhax.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <whb/crash.h>

char *lerr = NULL;

static void checkError() {
	if(lerr != NULL)
	{
		drawErrorFrame(lerr, ANY_RETURN);
		showFrame();

		while(!(vpad.trigger))
			showFrame();
	}
}

static void innerMain(bool validCfw)
{
	OSThread *mainThread = OSGetCurrentThread();
	OSSetThreadName(mainThread, "NUSspli");
#ifdef NUSSPLI_HBL
	OSSetThreadStackUsage(mainThread);
#endif

	if(validCfw)
	{
		addEntropy(&(mainThread->id), sizeof(uint16_t));
		addEntropy(mainThread->stackStart, 4);

		checkStacks("main");
	}

	FSInit();
#ifdef NUSSPLI_HBL
	romfsInit();
#endif

	KPADInit();
	WPADEnableURCC(true);

	if(initStaticMem())
	{
		if(initRenderer())
		{
			readInput(); // bug #95
			if(!validCfw) {
				lerr = "Unsupported environment.\nPlease update to Tiramisu.\nSee https://wiiu.hacks.guide";
				checkError();
			}
			
			if(OSSetThreadPriority(mainThread, THREAD_PRIORITY_HIGH))
				addToScreenLog("Changed main thread priority!");
			else
				addToScreenLog("WARNING: Error changing main thread priority!");

			startNewFrame();
			textToFrame(0, 0, "Loading OpenSSL...");
			writeScreenLog(1);
			drawFrame();
			if(!initCrypto()) {
				lerr = "Couldn't initialize OpenSSL!";
				deinitCrypto();
				debugPrintf("OpenSSL closed");
				checkError();
			}

			addToScreenLog("OpenSSL initialized!");

			startNewFrame();
			textToFrame(0, 0, "Loading MCP...");
			writeScreenLog(1);
			drawFrame();

			mcpHandle = MCP_Open();
			if(mcpHandle == 0)
			{
				lerr = "Couldn't initialize MCP!";
				MCP_Close(mcpHandle);
				debugPrintf("MCP closed");
				checkError();
			}
			addToScreenLog("MCP initialized!");

			startNewFrame();
			textToFrame(0, 0, "Checking sanity...");
			writeScreenLog(1);
			drawFrame();

			if(!sanityCheck())
			{
				lerr = "No support for rebrands, use original NUSspli!";
				checkError();
			}
			addToScreenLog("Sanity checked!");

			startNewFrame();
			textToFrame(0, 0, "Initializing notification system...");
			writeScreenLog(1);
			drawFrame();

			if(!initNotifications())
			{
				lerr = "Couldn't initialize notification system!";
				deinitNotifications();
				debugPrintf("Notification system closed");
				checkError();
			}

			addToScreenLog("Notification system initialized!");

			startNewFrame();
			textToFrame(0, 0, "Loading downloader...");
			writeScreenLog(1);
			drawFrame();

			if(!initDownloader())
			{
				lerr = "Couldn't initialize downloader!";
				deinitDownloader();
				checkError();
			}
			addToScreenLog("Downloader initialized!");

			startNewFrame();
			textToFrame(0, 0, "Loading I/O thread...");
			writeScreenLog(1);
			drawFrame();

			if(!initIOThread())
			{
				shutdownIOThread();
				debugPrintf("I/O thread closed");
				lerr = "Couldn't load I/O thread!";
				checkError();
			}
			addToScreenLog("I/O thread initialized!");

			startNewFrame();
			textToFrame(0, 0, "Loading config...");
			writeScreenLog(1);
			drawFrame();

			if(!initConfig())
			{
				saveConfig(false);
				lerr = "Couldn't load config file!\n\nMost likely your SD card is write locked!";
				checkError();
			}
			addToScreenLog("Config loaded!");
			startNewFrame();
			textToFrame(0, 0, "Loading SWKBD...");
			writeScreenLog(1);
			drawFrame();

			if(!SWKBD_Init())
			{
				SWKBD_Shutdown();
				debugPrintf("SWKBD closed");
				lerr = "Couldn't initialize SWKBD!";
				checkError();
			}
			addToScreenLog("SWKBD initialized!");
			startNewFrame();
			textToFrame(0, 0, "Loading menu...");
			writeScreenLog(1);
			drawFrame();

			checkStacks("main()");

			if(!updateCheck())
			{
				checkStacks("main");
				mainMenu(); // main loop
				checkStacks("main");
				debugPrintf("Deinitializing libraries...");
			}

			unmountAll();
			closeIOSUhax();

			checkError();

			shutdownRenderer();
			debugPrintf("SDL closed");
		}

		shutdownStaticMem();
	}
	else
		debugPrintf("Error inititalizing static memory!");

	debugPrintf("Clearing screen log");
	clearScreenLog();
	KPADShutdown();

	debugPrintf("Shutting down filesystem");
#ifdef NUSSPLI_HBL
	romfsExit();
#endif
	FSShutdown();
}

static bool cfwValid()
{
	int handle = IOS_Open("/dev/mcp", IOS_OPEN_READ);
	bool ret = handle >= 0;
	if(ret)
	{
		char dummy[0x100];
		int in = 0xF9; // IPC_CUSTOM_COPY_ENVIRONMENT_PATH
		ret = IOS_Ioctl(handle, 100, &in, sizeof(in), dummy, sizeof(dummy)) == IOS_ERROR_OK;
		if(ret)
		{
			ret = openIOSUhax();
			if(ret)
			{
				ret = IOSUHAX_read_otp((uint8_t *)dummy, 1) >= 0;
				closeIOSUhax();
			}
		}

		IOS_Close(handle);
	}

	return ret;
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
		jailbreaking = !isAroma() && (tid & 0xFFFFFFFFFFFFF0FF) == 0x000500101004A000; // Mii Maker
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

#ifdef NUSSPLI_DEBUG
	checkStacks("main");
	debugPrintf("Bye!");
	shutdownDebug();
#endif
	
	if(app != APP_STATE_STOPPED)
	{
#ifdef NUSSPLI_HBL
		if(jailbreaking)
		{
			tid &= 0xFFFFFFFFFFFF0FFF;
			tid |= 0x000000000000E000;
		}
		else if(!isAroma() && (tid & 0xFFFFFFFFFFFFF0FF) == 0x000500101004E000) // Health & Safety
		{
			tid &= 0xFFFFFFFFFFFF0FFF;
			tid |= 0x000000000000A000;
		}

		_SYSLaunchTitleWithStdArgsInNoSplash(tid, NULL);
#else
		SYSLaunchMenu();
#endif
		if(app == APP_STATE_HOME)
		{
			app = APP_STATE_RUNNING;
			while(AppRunning())
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
		}
		while(ps != PROCUI_STATUS_EXITING);
	}
	
	ProcUIShutdown();
	return 0;
}
