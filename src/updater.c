/***************************************************************************
 * This file is part of NUSspli.                                           *
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
#include <deinstaller.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <ioQueue.h>
#include <notifications.h>
#include <osdefs.h>
#include <renderer.h>
#include <state.h>
#include <utils.h>
#include <menu/update.h>
#include <menu/utils.h>

#include <coreinit/dynload.h>
#include <coreinit/filesystem.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>

#include <ioapi.h>
#include <unzip.h>

#include <errno.h>
#include <file.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <jansson.h>

#define UPDATE_DOWNLOAD_URL "https://github.com/V10lator/NUSspli/releases/download/v"
#define UPDATE_GITHUB_API "https://api.github.com/repos/V10lator/NUSspli/releases/latest"
#define UPDATE_TEMP_FOLDER NUSDIR_SD "/NUSspli_temp/"
#define UPDATE_AROMA_FOLDER NUSDIR_SD "/wiiu/apps/"
#define UPDATE_AROMA_FILE "NUSspli.wuhb"
#define UPDATE_HBL_FOLDER NUSDIR_SD "/wiiu/apps/NUSspli"

#ifdef NUSSPLI_DEBUG
	#define NUSSPLI_DLVER "-DEBUG"
#else
	#define NUSSPLI_DLVER ""
#endif

static void removeChars(char* s, char c)
{
    int j, n = strlen(s);
    for (int i = j = 0; i < n; i++)
        if (s[i] != c)
            s[j++] = s[i];
 
    s[j] = '\0';
}

void showUpdateError(const char* msg)
{
	enableShutdown();
	drawErrorFrame(msg, ANY_RETURN);
	while(AppRunning())
	{
		if(app == APP_STATE_BACKGROUND)
			continue;
		if(app == APP_STATE_RETURNING)
			drawErrorFrame(msg, ANY_RETURN);
		
		showFrame();
		
		if(vpad.trigger)
			return;
	}
}

void showUpdateErrorf(const char *msg, ...)
{
	va_list va;
	va_start(va, msg);
	char newMsg[2048];
	vsnprintf(newMsg, 2047, msg, va);
	va_end(va);
	newMsg[2047] = '\0';
	showUpdateError(newMsg);
}

bool updateCheck()
{
	if(!updateCheckEnabled())
		return false;
	
	startNewFrame();
	textToFrame(0, 0, "Preparing download");
	writeScreenLog(1);
	drawFrame();
	showFrame();

	bool ret = false;
	if(downloadFile(UPDATE_GITHUB_API, "JSON", NULL, FILE_TYPE_JSON | FILE_TYPE_TORAM, false) == 0)
	{
		startNewFrame();
		textToFrame(0, 0, "Parsing JSON");
		writeScreenLog(1);
		drawFrame();
		showFrame();

		json_t *json = json_loadb(getRamBuf(), getRamBufSize(), 0, NULL);
		if(json != NULL)
		{
			json_t *jsonObj = json_object_get(json, "name");
			json_t *jsonAssetsObj = json_object_get(json, "assets");
			if(jsonObj != NULL && json_is_string(jsonObj))
			{
				char serverVersionChar[24];
				strcpy(serverVersionChar, json_string_value(jsonObj) + 1);
				removeChars(serverVersionChar, '.');
				const int serverVersion = atoi(serverVersionChar);

				char *currentVersionChar = NUSSPLI_VERSION;
				removeChars(currentVersionChar, '.');
				const int currentVersion = atoi(currentVersionChar);

				debugPrintf("currentVersion: %i", currentVersion);
				debugPrintf("serverVersion: %i", serverVersion);

				char newVerZipName[256];
				if(serverVersion > currentVersion)
				{
					const char *newVer = json_string_value(jsonObj) + 1;
					strcat(newVerZipName, "NUSspli-");
					strcat(newVerZipName, newVer);
				#ifdef NUSSPLI_HBL
					strcat(newVerZipName, "-HBL.zip");
#else
					strcat(newVerZipName, isAroma() ? "-Aroma.zip" : "-Channel.zip");
#endif

					size_t index;
					json_t *value;
					bool foundZip = false;
					json_array_foreach(jsonAssetsObj, index, value)
					{
						char serverZipName[256];
						json_t *serverZipNameObj = json_object_get(value, "name");
						if(serverZipNameObj != NULL && json_is_string(serverZipNameObj))
						{
							strcpy(serverZipName, json_string_value(serverZipNameObj));
							if(strcmp(newVerZipName, serverZipName) == 0)
							{
								foundZip = true;
								break;
							}
						}
					}
					#ifdef NUSSPLI_HBL
					if(foundZip)
					{
						ret = updateMenu(newVer, NUSSPLI_TYPE_HBL);
					}
					else
					{
						showUpdateErrorf("%s not found in the server, perhaps this version is deprecated?", newVerZipName);
					}
#else
					if(foundZip)
					{
						ret = updateMenu(newVer, isAroma() ? NUSSPLI_TYPE_AROMA : NUSSPLI_TYPE_CHANNEL);
					}
					else
					{
						showUpdateErrorf("%s not found in the server, perhaps this version is deprecated?", newVerZipName);
					}
#endif
				}
			}
			else
				debugPrintf("Invalid JSON data");

			json_decref(json);
		}
		else
			debugPrintf("Invalid JSON data");
	}
	else
		debugPrintf("Error downloading %s", UPDATE_GITHUB_API);

	clearRamBuf();
	return ret;
}

static voidpf ZCALLBACK nus_zopen(voidpf opaque, const char* filename, int mode)
{
	// STUB
	return (voidpf)filename;
}

static uLong ZCALLBACK nus_zread(voidpf opaque, voidpf stream, void* buf, uLong size)
{
	OSBlockMove(buf, getRamBuf() + *((long *)(stream)), size, false);
	*((long *)(stream)) += size;
	return size;
}

static uLong ZCALLBACK nus_zwrite(voidpf opaque, voidpf stream, const void* buf, uLong size)
{
	// STUB
	return size;
}

static long ZCALLBACK nus_ztell(voidpf opaque, voidpf stream)
{
	return *((long *)(stream));
}

static long ZCALLBACK nus_zseek(voidpf opaque, voidpf stream, uLong offset, int origin)
{
	switch(origin)
	{
		case ZLIB_FILEFUNC_SEEK_CUR:
			*((long *)(stream)) += offset;
			break;
		case ZLIB_FILEFUNC_SEEK_END:
			*((long *)(stream)) = getRamBufSize() + offset;
			break;
		case ZLIB_FILEFUNC_SEEK_SET :
			*((long *)(stream)) = offset;
			break;
	}

	return 0;
}

static int ZCALLBACK nus_zstub(voidpf opaque, voidpf stream)
{
	// STUB
	return 0;
}

static bool unzipUpdate()
{
	long zPos = 0;
	zlib_filefunc_def rbfd = {
		.zopen_file = nus_zopen,
		.zread_file = nus_zread,
		.zwrite_file = nus_zwrite,
		.ztell_file = nus_ztell,
		.zseek_file = nus_zseek,
		.zclose_file = nus_zstub,
		.zerror_file = nus_zstub,
		.opaque = NULL
	};
	bool ret = false;
	unzFile zip = unzOpen2((const char *)&zPos, &rbfd);
	if(zip != NULL)
	{
		unz_global_info zipInfo;
		if(unzGetGlobalInfo(zip, &zipInfo) == UNZ_OK)
		{
			uint8_t *buf = MEMAllocFromDefaultHeap(IO_BUFSIZE);
			if(buf != NULL)
			{
				char *zipFileName = getStaticPathBuffer(1);
				unz_file_info zipFileInfo;
				char *needle;
				char *fileName = getStaticPathBuffer(2);
				strcpy(fileName, UPDATE_TEMP_FOLDER);
				char *fnp = fileName + strlen(UPDATE_TEMP_FOLDER);
				NUSFILE *file;
				size_t extracted;
				char *lastSlash;
				char *lspp;

				char *path =  getStaticPathBuffer(0);
				ret = true;
				do
				{
					if(unzGetCurrentFileInfo(zip, &zipFileInfo, zipFileName, 256, NULL, 0, NULL, 0) == UNZ_OK)
					{
						if(unzOpenCurrentFile(zip) == UNZ_OK)
						{
							needle = strchr(zipFileName, '/');
							if(needle != NULL)
							{
								lastSlash = needle;
								lspp = needle + 1;
								needle = strchr(lspp, '/');
								while(needle != NULL)
								{
									lastSlash = needle;
									lspp = needle + 1;
									needle = strchr(lspp, '/');
								}

								if(lastSlash[1] == '\0')
								{
									unzCloseCurrentFile(zip);
									continue;
								}

								lastSlash[0] = '\0';
								strcpy(path, zipFileName);
								strcat(path, "/");
								strcpy(fnp, path);
								strcpy(zipFileName, lastSlash + 1);

								if(!createDirRecursive(fileName))
								{
									showUpdateErrorf("Error creating directory: %s", prettyDir(fileName));
									ret = false;
								}
							}
							else
								path[0] = '\0';

							if(ret)
							{
								sprintf(fnp, "%s%s", path, zipFileName);
								file = openFile(fileName, "wb");
								if(file != NULL)
								{
									while(ret)
									{
										extracted = unzReadCurrentFile(zip, buf, IO_BUFSIZE);
										if(extracted < 0)
										{
											showUpdateErrorf("Error extracting file: %s", prettyDir(fileName));
											ret = false;
											break;
										}

										if(extracted != 0)
										{
											if(addToIOQueue(buf, 1, extracted, file) != extracted)
											{
												showUpdateErrorf("Error writing file: %s", prettyDir(fileName));
												ret = false;
												break;
											}
										}
										else
											break;
									}

									addToIOQueue(NULL, 0, 0, file);
								}
								else
								{
									showUpdateErrorf("Error opening file: %s", prettyDir(fileName));
									ret = false;
								}
							}

							unzCloseCurrentFile(zip);
						}
						else
						{
							showUpdateError("Error opening zip file");
							ret = false;
						}
					}
					else
					{
						showUpdateError("Error extracting zip");
						ret = false;
					}
				}
				while(ret && unzGoToNextFile(zip) == UNZ_OK);

				MEMFreeToDefaultHeap(buf);
			}
			else
				showUpdateError("Error allocating memory");
		}
		else
			showUpdateError("Error getting zip info");

		unzClose(zip);
	}
	else
		showUpdateError("Error opening zip!");

	return ret;
}

void update(const char *newVersion, NUSSPLI_TYPE type)
{
#ifndef NUSSPLI_HBL
	OSDynLoad_Module mod;
	int(*RL_UnmountCurrentRunningBundle)();
	if(!isChannel() && isAroma())
	{
		OSDynLoad_Error err = OSDynLoad_Acquire("homebrew_rpx_loader", &mod);
		if(err == OS_DYNLOAD_OK)
		{
			err = OSDynLoad_FindExport(mod, false, "RL_UnmountCurrentRunningBundle", (void **)&RL_UnmountCurrentRunningBundle);
			if(err != OS_DYNLOAD_OK)
				OSDynLoad_Release(mod);
		}
		if(err != OS_DYNLOAD_OK)
		{
			showUpdateError("Aroma version too old to allow auto-updates");
			return;
		}
	}
#endif

	disableShutdown();
	removeDirectory(UPDATE_TEMP_FOLDER);
	NUSFS_ERR err = createDirectory(UPDATE_TEMP_FOLDER, 777);
	if(err != NUSFS_ERR_NOERR)
	{
		char *toScreen = getToFrameBuffer();
		strcpy(toScreen, "Error creating temporary directory!\n\n");
		const char *errStr = translateNusfsErr(err);
		if(errStr == NULL)
			sprintf(toScreen + strlen(toScreen), "%d", err);
		else
			strcat(toScreen, errStr);

		showUpdateError(toScreen);
		goto updateError;
	}

	char *path =  getStaticPathBuffer(0);
	strcpy(path, UPDATE_DOWNLOAD_URL);
	strcpy(path + strlen(UPDATE_DOWNLOAD_URL), newVersion);
	strcat(path, "/NUSspli-");
	strcat(path, newVersion);

	switch(type)
	{
		case NUSSPLI_TYPE_AROMA:
			strcat(path, "-Aroma");
			break;
		case NUSSPLI_TYPE_CHANNEL:
			strcat(path, "-Channel");
			break;
		case NUSSPLI_TYPE_HBL:
			strcat(path, "-HBL");
			break;
		default:
			showUpdateError("Internal error!");
			goto updateError;
	}

	strcat(path, NUSSPLI_DLVER ".zip");

	if(downloadFile(path, "NUSspli.zip", NULL, FILE_TYPE_JSON | FILE_TYPE_TORAM, false) != 0)
	{
		clearRamBuf();
		showUpdateErrorf("Error downloading %s", path);
		goto updateError;
	}

	startNewFrame();
	textToFrame(0, 0, "Updating, please wait...");
	writeScreenLog(1);
	drawFrame();
	showFrame();

	if(!unzipUpdate())
	{
		clearRamBuf();
		goto updateError;
	}

	clearRamBuf();
	bool toUSB = getUSB() != NUSDEV_NONE;

	// Uninstall currently running type/version
#ifdef NUSSPLI_HBL
	if(!dirExists(UPDATE_HBL_FOLDER))
	{
		showUpdateError("Couldn't find NUSspli folder on the SD card");
		goto updateError;
	}

	removeDirectory(UPDATE_HBL_FOLDER);
#else
	if(isChannel())
	{
		MCPTitleListType ownInfo;
		MCPError err = MCP_GetOwnTitleInfo(mcpHandle, &ownInfo);
		if(err != 0)
		{
			showUpdateErrorf("Error getting own title info: %#010x", err);
			goto updateError;
		}

		if(toUSB)
			toUSB = ownInfo.indexedDevice[0] == 'u';

		deinstall(&ownInfo, "NUSspli v" NUSSPLI_VERSION, true, false);
		OSSleepTicks(OSSecondsToTicks(10)); // channelHaxx...
	}
	else if(isAroma())
	{
		if(!fileExists(UPDATE_AROMA_FOLDER UPDATE_AROMA_FILE))
		{
			showUpdateError("Couldn't find NUSspli file on the SD card");
			goto updateError;
		}

		RL_UnmountCurrentRunningBundle();
		OSDynLoad_Release(mod);
		remove(UPDATE_AROMA_FOLDER UPDATE_AROMA_FILE);
	}
#endif

	// Install new type/version
	flushIOQueue();
	switch(type)
	{
		case NUSSPLI_TYPE_AROMA:
			rename(UPDATE_TEMP_FOLDER UPDATE_AROMA_FILE, UPDATE_AROMA_FOLDER UPDATE_AROMA_FILE);
			break;
		case NUSSPLI_TYPE_CHANNEL:
			strcpy(path, UPDATE_TEMP_FOLDER);
			strcpy(path + strlen(UPDATE_TEMP_FOLDER), "NUSspli");

			install("Update", false, NUSDEV_SD, path, toUSB, true, 0);
			break;
		case NUSSPLI_TYPE_HBL:
#ifdef NUSSPLI_DEBUG
			NUSFS_ERR err =
#endif
			moveDirectory(UPDATE_TEMP_FOLDER "NUSspli", UPDATE_HBL_FOLDER);
#ifdef NUSSPLI_DEBUG
			if(err != NUSFS_ERR_NOERR)
				debugPrintf("Error moving directory: %s", translateNusfsErr(err));
#endif
			break;
	}

	removeDirectory(UPDATE_TEMP_FOLDER);
	enableShutdown();
	startNotification();
	colorStartNewFrame(SCREEN_COLOR_D_GREEN);
	textToFrame(0, 0, "Update");
	textToFrame(1, 0, "Installed successfully!");
	writeScreenLog(2);
	drawFrame();

	while(AppRunning())
	{
		if(app == APP_STATE_BACKGROUND)
			continue;
		if(app == APP_STATE_RETURNING)
		{
			colorStartNewFrame(SCREEN_COLOR_D_GREEN);
			textToFrame(0, 0, "Update");
			textToFrame(1, 0, "Installed successfully!");
			writeScreenLog(2);
			drawFrame();
		}

		showFrame();

		if(vpad.trigger)
			break;;
	}

	stopNotification();
	return;

updateError:
#ifndef NUSSPLI_HBL
	if(!isChannel() && isAroma())
		OSDynLoad_Release(mod);
#endif
	removeDirectory(UPDATE_TEMP_FOLDER);
}
