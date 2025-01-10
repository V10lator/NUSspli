/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2024 V10lator <v10lator@myway.de>                    *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
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
#include <string.h>

#include <config.h>
#include <crypto.h>
#include <ioQueue.h>
#include <localisation.h>
#include <menu/utils.h>
#include <renderer.h>
#include <romfs.h>
#include <staticMem.h>
#include <utils.h>

#include <jansson.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>
#include <coreinit/userconfig.h>
#pragma GCC diagnostic pop

#define CONFIG_VERSION   2

#define LANG_JAP         "Japanese"
#define LANG_ENG         "English"
#define LANG_FRE         "French"
#define LANG_GER         "German"
#define LANG_ITA         "Italian"
#define LANG_SPA         "Spanish"
#define LANG_CHI         "Chinese"
#define LANG_KOR         "Korean"
#define LANG_DUT         "Dutch"
#define LANG_POR         "Portuguese"
#define LANG_POR_BR      "Brazilian Portuguese"
#define LANG_RUS         "Russian"
#define LANG_TCH         "Traditional chinese"
#define LANG_TUR         "Turkish"
#define LANG_WEL         "Welsh"
#define LANG_SYS         "System settings"

#define SET_EUR          "Europe"
#define SET_USA          "USA"
#define SET_JPN          "Japan"
#define SET_ALL          "All"

#define NOTIF_RUMBLE     "Rumble"
#define NOTIF_LED        "LED"
#define NOTIF_BOTH       "Rumble + LED"
#define NOTIF_NONE       "None"

#define LOCALE_EXTENSION ".json"

static bool changed = false;
static bool checkForUpdates = true;
static bool autoResume = true;
static Swkbd_LanguageType lang = Swkbd_LanguageType__Invalid;
static Swkbd_LanguageType sysLang;
static Swkbd_LanguageType menuLang = Swkbd_LanguageType__English;
static bool dlToUSB = true;
static MCPRegion regionSetting = MCP_REGION_EUROPE | MCP_REGION_USA | MCP_REGION_JAPAN;
static NOTIF_METHOD notifSetting = NOTIF_METHOD_RUMBLE | NOTIF_METHOD_LED;

static inline void intSetMenuLanguage()
{
    locCleanUp();
    Swkbd_LanguageType language = menuLang == Swkbd_LanguageType__Invalid ? sysLang : menuLang;
    if(language == Swkbd_LanguageType__English)
        return;

    const char *lp = getLanguageString(language);
    char locale_path[(sizeof(ROMFS_PATH "locale/") + sizeof(LOCALE_EXTENSION) - 1) + strlen(lp)];
    strcpy(locale_path, ROMFS_PATH "locale/");
    strcpy(locale_path + (sizeof(ROMFS_PATH "locale/") - 1), lp);
    strcpy(locale_path + (sizeof(ROMFS_PATH "locale/") - 1) + strlen(lp), LOCALE_EXTENSION);
    locLoadLanguage(locale_path);
}

Swkbd_LanguageType stringToLanguageType(const char *language)
{
    if(strcmp(language, LANG_JAP) == 0)
        return Swkbd_LanguageType__Japanese;
    if(strcmp(language, LANG_ENG) == 0)
        return Swkbd_LanguageType__English;
    if(strcmp(language, LANG_FRE) == 0)
        return Swkbd_LanguageType__French;
    if(strcmp(language, LANG_GER) == 0)
        return Swkbd_LanguageType__German;
    if(strcmp(language, LANG_ITA) == 0)
        return Swkbd_LanguageType__Italian;
    if(strcmp(language, LANG_SPA) == 0)
        return Swkbd_LanguageType__Spanish;
    if(strcmp(language, LANG_CHI) == 0)
        return Swkbd_LanguageType__Chinese1;
    if(strcmp(language, LANG_KOR) == 0)
        return Swkbd_LanguageType__Korean;
    if(strcmp(language, LANG_DUT) == 0)
        return Swkbd_LanguageType__Dutch;
    if(strcmp(language, LANG_POR) == 0)
        return Swkbd_LanguageType__Portuguese;
    if(strcmp(language, LANG_POR_BR) == 0)
        return Swkbd_LanguageType__Portuguese_BR;
    if(strcmp(language, LANG_RUS) == 0)
        return Swkbd_LanguageType__Russian;
    if(strcmp(language, LANG_TCH) == 0)
        return Swkbd_LanguageType__Chinese2;
    if(strcmp(language, LANG_TUR) == 0)
        return Swkbd_LanguageType__Turkish;
    if(strcmp(language, LANG_WEL) == 0)
        return Swkbd_LanguageType__Welsh;

    return Swkbd_LanguageType__Invalid;
}

void initConfig()
{
    debugPrintf("Initializing config file...");

    UCHandle handle = UCOpen();
    if(handle >= 0)
    {
        UCSysConfig settings __attribute__((__aligned__(0x40))) = {
            .name = "cafe.language",
            .access = 0,
            .dataType = UC_DATATYPE_UNSIGNED_INT,
            .error = UC_ERROR_OK,
            .dataSize = sizeof(Swkbd_LanguageType),
            .data = &sysLang,
        };

        UCError err = UCReadSysConfig(handle, 1, &settings);
        UCClose(handle);
        if(err != UC_ERROR_OK)
        {
            debugPrintf("Error reading UC: %d!", err);
            sysLang = Swkbd_LanguageType__English;
        }
        else
            debugPrintf("System language found: %s", getLanguageString(sysLang));
    }
    else
    {
        debugPrintf("Error opening UC: %d", handle);
        sysLang = Swkbd_LanguageType__English;
    }

    menuLang = Swkbd_LanguageType__Invalid;

    if(!fileExists(CONFIG_PATH))
    {
        addToScreenLog("Config file not found, using defaults!");
        goto error;
    }

    OSTime t = OSGetTime();
    void *buf;
    size_t bufSize = readFile(CONFIG_PATH, &buf);
    if(buf == NULL)
    {
        addToScreenLog("Error loading config file, using defaults!");
        goto error;
    }

#ifdef NUSSPLI_DEBUG
    json_error_t jerr;
    json_t *json = json_loadb(buf, bufSize, 0, &jerr);
#else
    json_t *json = json_loadb(buf, bufSize, 0, NULL);
#endif
    if(json == NULL)
    {
        MEMFreeToDefaultHeap(buf);
        debugPrintf("json_loadb() failed: %s!", jerr.text);
        addToScreenLog("Error parsing config file, using defaults!");
        goto error;
    }

    json_t *configEntry = json_object_get(json, "File Version");
    if(configEntry != NULL && json_is_integer(configEntry))
    {
        int v = json_integer_value(configEntry);
        if(v < 2)
        {
            addToScreenLog("Old config file updating...");
            configEntry = json_object_get(json, "Language");
            if(configEntry != NULL && json_is_string(configEntry))
                lang = stringToLanguageType(json_string_value(configEntry));

            menuLang = Swkbd_LanguageType__Invalid;
            changed = true;
        }
        else
        {
            configEntry = json_object_get(json, "Keyboard language");
            if(configEntry != NULL && json_is_string(configEntry))
                lang = stringToLanguageType(json_string_value(configEntry));
            else
                changed = true;

            configEntry = json_object_get(json, "Menu language");
            if(configEntry != NULL && json_is_string(configEntry))
                menuLang = stringToLanguageType(json_string_value(configEntry));
            else
                changed = true;
        }
    }
    else
    {
        addToScreenLog("Config file version not found!");
        menuLang = Swkbd_LanguageType__Invalid;
        changed = true;
    }

    intSetMenuLanguage();

    configEntry = json_object_get(json, "Check for updates");
    if(configEntry != NULL && json_is_boolean(configEntry))
        checkForUpdates = json_is_true(configEntry);
    else
    {
        addToScreenLog("Update check setting not found!");
        changed = true;
    }

    configEntry = json_object_get(json, "Auto resume failed downloads");
    if(configEntry != NULL && json_is_boolean(configEntry))
        autoResume = json_is_true(configEntry);
    else
    {
        addToScreenLog("Auto resume setting not found!");
        changed = true;
    }

    configEntry = json_object_get(json, "Region");
    if(configEntry != NULL && json_is_string(configEntry))
    {
        if(strcmp(json_string_value(configEntry), SET_EUR) == 0)
            regionSetting = MCP_REGION_EUROPE;
        else if(strcmp(json_string_value(configEntry), SET_USA) == 0)
            regionSetting = MCP_REGION_USA;
        else if(strcmp(json_string_value(configEntry), SET_JPN) == 0)
            regionSetting = MCP_REGION_JAPAN;
        else
            regionSetting = MCP_REGION_EUROPE | MCP_REGION_USA | MCP_REGION_JAPAN;
    }
    else
    {
        addToScreenLog("Region setting not found!");
        changed = true;
    }

    configEntry = json_object_get(json, "Download to USB");
    if(configEntry != NULL && json_is_boolean(configEntry))
        dlToUSB = json_is_true(configEntry);
    else
    {
        addToScreenLog("Download to setting not found!");
        changed = true;
    }

    configEntry = json_object_get(json, "Notification method");
    if(configEntry != NULL && json_is_string(configEntry))
    {
        if(strcmp(json_string_value(configEntry), NOTIF_RUMBLE) == 0)
            notifSetting = NOTIF_METHOD_RUMBLE;
        else if(strcmp(json_string_value(configEntry), NOTIF_LED) == 0)
            notifSetting = NOTIF_METHOD_LED;
        else if(strcmp(json_string_value(configEntry), NOTIF_NONE) == 0)
            notifSetting = NOTIF_METHOD_NONE;
        else
            notifSetting = NOTIF_METHOD_RUMBLE | NOTIF_METHOD_LED;
    }
    else
    {
        addToScreenLog("Notification setting not found!");
        changed = true;
    }

    configEntry = json_object_get(json, "Seed");
    if(configEntry != NULL && json_is_integer(configEntry))
    {
        int ent = (int)json_integer_value(configEntry);
        addEntropy(&ent, 4);
    }

    json_decref(json);
    MEMFreeToDefaultHeap(buf);
    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));

    addToScreenLog("Config file loaded!");
    return;

error:
    changed = true; // trigger a save on app exit
    intSetMenuLanguage();
}

const char *getLanguageString(Swkbd_LanguageType language)
{
    switch(language)
    {
        case Swkbd_LanguageType__Japanese:
            return LANG_JAP;
        case Swkbd_LanguageType__English:
            return LANG_ENG;
        case Swkbd_LanguageType__French:
            return LANG_FRE;
        case Swkbd_LanguageType__German:
            return LANG_GER;
        case Swkbd_LanguageType__Italian:
            return LANG_ITA;
        case Swkbd_LanguageType__Spanish:
            return LANG_SPA;
        case Swkbd_LanguageType__Chinese1:
            return LANG_CHI;
        case Swkbd_LanguageType__Korean:
            return LANG_KOR;
        case Swkbd_LanguageType__Dutch:
            return LANG_DUT;
        case Swkbd_LanguageType__Portuguese:
            return LANG_POR;
        case Swkbd_LanguageType__Portuguese_BR:
            return LANG_POR_BR;
        case Swkbd_LanguageType__Russian:
            return LANG_RUS;
        case Swkbd_LanguageType__Chinese2:
            return LANG_TCH;
        case Swkbd_LanguageType__Turkish:
            return LANG_TUR;
        case Swkbd_LanguageType__Welsh:
            return LANG_WEL;
        default:
            return LANG_SYS;
    }
}

const char *getNotificationString(NOTIF_METHOD method)
{
    switch((int)method)
    {
        case NOTIF_METHOD_RUMBLE:
            return NOTIF_RUMBLE;
        case NOTIF_METHOD_LED:
            return NOTIF_LED;
        case NOTIF_METHOD_NONE:
            return NOTIF_NONE;
        default:
            return NOTIF_BOTH;
    }
}

static inline bool setValue(json_t *config, const char *key, json_t *value)
{
    if(value == NULL)
        return false;

    bool ret = !json_object_set(config, key, value);
    json_decref(value);
    if(!ret)
        debugPrintf("Error setting %s", key);

    return ret;
}

void saveConfig(bool force)
{
    debugPrintf("saveConfig()");
    if(!changed && !force)
        return;

    json_t *config = json_object();
    if(config != NULL)
    {
        json_t *value = json_integer(CONFIG_VERSION);
        if(setValue(config, "File Version", value))
        {
            value = checkForUpdates ? json_true() : json_false();
            if(setValue(config, "Check for updates", value))
            {
                value = autoResume ? json_true() : json_false();
                if(setValue(config, "Auto resume failed downloads", value))
                {
                    value = json_string(getLanguageString(menuLang));
                    if(setValue(config, "Menu language", value))
                    {
                        value = json_string(getLanguageString(lang));
                        if(setValue(config, "Keyboard language", value))
                        {
                            value = json_string(getFormattedRegion(getRegion()));
                            if(setValue(config, "Region", value))
                            {
                                value = dlToUSB ? json_true() : json_false();
                                if(setValue(config, "Download to USB", value))
                                {
                                    value = json_string(getNotificationString(getNotificationMethod()));
                                    if(setValue(config, "Notification method", value))
                                    {
                                        uint32_t entropy;
                                        NUSrng(NULL, (unsigned char *)&entropy, 4);
                                        value = json_integer(entropy);
                                        if(setValue(config, "Seed", value))
                                        {
                                            char *json = json_dumps(config, JSON_INDENT(4));
                                            if(json != NULL)
                                            {
                                                entropy = strlen(json);
                                                flushIOQueue();
                                                FSAFileHandle f = openFile(CONFIG_PATH, "w", 0);
                                                if(f != 0)
                                                {
                                                    addToIOQueue(json, 1, entropy, f);
                                                    addToIOQueue(NULL, 0, 0, f);
                                                    changed = false;
                                                }
                                                else
                                                    showErrorFrame(localise("Couldn't save config file!\nYour SD card might be write locked."));

                                                MEMFreeToDefaultHeap(json);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        json_decref(config);
    }
    else
        debugPrintf("config == NULL");

    return;
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

const char *getFormattedRegion(MCPRegion region)
{
    if(region & MCP_REGION_EUROPE)
    {
        if(region & MCP_REGION_USA)
            return region & MCP_REGION_JAPAN ? SET_ALL : "USA/Europe";

        return region & MCP_REGION_JAPAN ? "Europe/Japan" : SET_EUR;
    }

    if(region & MCP_REGION_USA)
        return region & MCP_REGION_JAPAN ? "USA/Japan" : SET_USA;

    return region & MCP_REGION_JAPAN ? SET_JPN : "Unknown";
}

bool dlToUSBenabled()
{
    return dlToUSB;
}

void setDlToUSB(bool toUSB)
{
    if(dlToUSB == toUSB)
        return;

    dlToUSB = toUSB;
    changed = true;
}

MCPRegion getRegion()
{
    return regionSetting;
}

void setRegion(MCPRegion region)
{
    if(region == regionSetting)
        return;

    regionSetting = region;
    changed = true;
}

Swkbd_LanguageType getMenuLanguage()
{
    return menuLang;
}

void setMenuLanguage(Swkbd_LanguageType language)
{
    if(menuLang == language)
        return;

    menuLang = language;
    intSetMenuLanguage();
    changed = true;
}

Swkbd_LanguageType getKeyboardLanguage()
{
    return lang == Swkbd_LanguageType__Invalid ? sysLang : lang;
}

Swkbd_LanguageType getUnfilteredLanguage()
{
    return lang;
}

void setKeyboardLanguage(Swkbd_LanguageType language)
{
    if(lang == language)
        return;

    lang = language;
    changed = true;

    SWKBD_Shutdown();
    debugPrintf("CA");
    pauseRenderer();
    debugPrintf("CB");
    resumeRenderer();
    debugPrintf("CC");
    //	SWKBD_Init();
    debugPrintf("CD");
}

NOTIF_METHOD getNotificationMethod()
{
    return notifSetting;
}

void setNotificationMethod(NOTIF_METHOD method)
{
    if(notifSetting == method)
        return;

    notifSetting = method;
    changed = true;
}
