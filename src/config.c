/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020 V10lator <v10lator@myway.de>                         *
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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <config.h>
#include <file.h>
#include <input.h>
#include <renderer.h>
#include <utils.h>
#include "cJSON/cJSON.h"
#include "menu/utils.h"

#define CONFIG_VERSION 1

bool changed = false;
bool useTitleDB = true;
bool checkForUpdates = true;
bool autoResume = true;
int configInitTries = 0;

bool initConfig()
{
	if(++configInitTries == 10)
	{
		debugPrintf("Giving up!");
		return false;
	}
	
	debugPrintf("Initializing config file...");
	
	if(!fileExists(CONFIG_PATH))
	{
		debugPrintf("\tFile not found!");
		changed = true;
		if(!saveConfig())
			return false;
	}
	
	FILE *fp = fopen(CONFIG_PATH, "rb");
	if(fp == NULL)
		return false;
	
	long fileSize = getFilesize(fp);
	bool ret = fileSize > 0;
	if(!ret)
		fileSize = 1;
	char fileContent[fileSize];
	ret = ret && fread(fileContent, 1, fileSize, fp) == fileSize;
	fclose(fp);
	if(!ret)
		return false;
	
	cJSON *json = cJSON_ParseWithLength(fileContent, fileSize);
	if(json == NULL)
		return false;
	
	cJSON *configEntry = cJSON_GetObjectItemCaseSensitive(json, "Use online title DB");
	if(configEntry != NULL && cJSON_IsBool(configEntry))
		useTitleDB = cJSON_IsTrue(configEntry);
	else
		changed = true;
	
	configEntry = cJSON_GetObjectItemCaseSensitive(json, "Check for updates");
	if(configEntry != NULL && cJSON_IsBool(configEntry))
		checkForUpdates = cJSON_IsTrue(configEntry);
	else
		changed = true;
	
	configEntry = cJSON_GetObjectItemCaseSensitive(json, "Auto resume failed downloads");
	if(configEntry != NULL && cJSON_IsBool(configEntry))
		autoResume = cJSON_IsTrue(configEntry);
	else
		changed = true;
	
	cJSON_Delete(json);
	
	if(changed)
		saveConfig();
	
	addToScreenLog("Config file loaded!");
	startNewFrame();
	textToFrame(0, 0, "Loading...");
	writeScreenLog();
	drawFrame();
	showFrame();
	return true;
}

bool saveConfig()
{
	debugPrintf("saveConfig()");
	if(!changed)
		return true;
	
	cJSON *config = cJSON_CreateObject();
	if(config == NULL )
		return false;
	
	cJSON *entry = cJSON_CreateNumber(CONFIG_VERSION);
	if(entry == NULL)
	{
		cJSON_Delete(config);
		return false;;
	}
	cJSON_AddItemToObject(config, "File Version", entry);
	
	entry = cJSON_CreateBool(useTitleDB);
	if(entry == NULL)
	{
		cJSON_Delete(config);
		return false;
	}
	cJSON_AddItemToObject(config, "Use online title DB", entry);
	
	entry = cJSON_CreateBool(checkForUpdates);
	if(entry == NULL)
	{
		cJSON_Delete(config);
		return false;
	}
	cJSON_AddItemToObject(config, "Check for updates", entry);
	
	entry = cJSON_CreateBool(autoResume);
	if(entry == NULL)
	{
		cJSON_Delete(config);
		return false;
	}
	cJSON_AddItemToObject(config, "Auto resume failed downloads", entry);
	
	char *configString = cJSON_Print(config);
	cJSON_Delete(config);
	if(configString == NULL)
		return false;
	
	FILE *fp = fopen(CONFIG_PATH, "w");
	if(fp == NULL)
		return false;
	
	fwrite(configString, strlen(configString), 1, fp);
	debugPrintf("Config written!");
	fclose(fp);
	
	changed = false;
	return true;
}

bool useOnlineTitleDB()
{
	return useTitleDB;
}

void setUseOnlineTitleDB(bool use)
{
	if(useTitleDB == use)
		return;
	
	useTitleDB = use;
	changed = true;
}

bool updateCheckEnabled()
{
	return checkForUpdates;
}

void setUpdateCheck(bool enabled)
{
	if(checkForUpdates == enabled)
		return;
	
	checkForUpdates = enabled;
	changed = true;
}

bool autoResumeEnabled()
{
	return autoResume;
}

void setAutoResume(bool enabled)
{
	if(autoResume == enabled)
		return;
	
	autoResume = enabled;
	changed = true;
}
