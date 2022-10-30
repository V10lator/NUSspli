/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
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

#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>
#include <coreinit/userconfig.h>

#include <jansson.h>

#define CONFIG_VERSION 2

#define LANG_JAP       "Japanese"
#define LANG_ENG       "English"
#define LANG_FRE       "French"
#define LANG_GER       "German"
#define LANG_ITA       "Italian"
#define LANG_SPA       "Spanish"
#define LANG_CHI       "Chinese"
#define LANG_KOR       "Korean"
#define LANG_DUT       "Dutch"
#define LANG_POR       "Portuguese"
#define LANG_RUS       "Russian"
#define LANG_TCH       "Traditional chinese"
#define LANG_SYS       "System settings"

#define SET_EUR        "Europe"
#define SET_USA        "USA"
#define SET_JPN        "Japan"
#define SET_ALL        "All"

#define NOTIF_RUMBLE   "Rumble"
#define NOTIF_LED      "LED"
#define NOTIF_BOTH     "Rumble + LED"
#define NOTIF_NONE     "None"

static bool changed = false;
static bool saveFailed = false;
static bool checkForUpdates = true;
static bool autoResume = true;
static Swkbd_LanguageType lang = Swkbd_LanguageType__Invalid;
static Swkbd_LanguageType sysLang;
static MENU_LANGUAGE menuLang = MENU_LANGUAGE_ENGLISH;
#ifndef NUSSPLI_LITE
static bool dlToUSB = true;
static MCPRegion regionSetting = MCP_REGION_EUROPE | MCP_REGION_USA | MCP_REGION_JAPAN;
#endif
static NOTIF_METHOD notifSetting = NOTIF_METHOD_RUMBLE | NOTIF_METHOD_LED;

const char *menuLangToString(MENU_LANGUAGE lang)
{
    switch(lang)
    {
        case MENU_LANGUAGE_ENGLISH:
            return LANG_ENG;
        case MENU_LANGUAGE_GERMAN:
            return LANG_GER;
        case MENU_LANGUAGE_SPANISH:
            return LANG_SPA;
        case MENU_LANGUAGE_PORTUGUESE:
            return LANG_POR;
        default:
            return LANG_SYS;
    }
}

static inline MENU_LANGUAGE sysLangToMenuLang(Swkbd_LanguageType lang)
{
    switch(lang)
    {
        case Swkbd_LanguageType__German:
            return MENU_LANGUAGE_GERMAN;
        case Swkbd_LanguageType__Spanish:
            return MENU_LANGUAGE_SPANISH;
        case Swkbd_LanguageType__Portuguese:
            return MENU_LANGUAGE_PORTUGUESE;
        default:
            return MENU_LANGUAGE_ENGLISH;
    }
}

static MENU_LANGUAGE stringToMenuLang(const char *lang)
{
    if(strcmp(lang, LANG_ENG) == 0)
        return MENU_LANGUAGE_ENGLISH;
    if(strcmp(lang, LANG_GER) == 0)
        return MENU_LANGUAGE_GERMAN;
    if(strcmp(lang, LANG_SPA) == 0)
        return MENU_LANGUAGE_SPANISH;
    if(strcmp(lang, LANG_POR) == 0)
        return MENU_LANGUAGE_PORTUGUESE;

    return sysLangToMenuLang(sysLang);
}

#define LOCALE_PATH      ROMFS_PATH "locale/"
#define LOCALE_EXTENSION ".json"
static inline const char *getLocalisationFile(MENU_LANGUAGE lang)
{
    if(lang == MENU_LANGUAGE_ENGLISH)
        return NULL;

    char *ret = getStaticPathBuffer(2);
    OSBlockMove(ret, LOCALE_PATH, strlen(LOCALE_PATH), false);
    strcpy(ret + strlen(LOCALE_PATH), menuLangToString(lang));
    strcat(ret + strlen(LOCALE_PATH), LOCALE_EXTENSION);

    return ret;
}

static inline void intSetMenuLanguage(MENU_LANGUAGE lang)
{
    gettextCleanUp();
    const char *path = getLocalisationFile(lang);
    if(path != NULL)
        gettextLoadLanguage(path);
}

bool initConfig()
{
    debugPrintf("Initializing config file...");

    UCHandle handle = UCOpen();
    if(handle >= 0)
    {
        UCSysConfig settings __attribute__((__aligned__(0x40))) = {
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

    menuLang = sysLang;

    if(!fileExists(CONFIG_PATH))
    {
        addToScreenLog("Config file not found, using defaults!");
        changed = true; // trigger a save on app exit
        intSetMenuLanguage(menuLang);
        return true;
    }

    OSTime t = OSGetTime();
    void *buf;
    size_t bufSize = readFile(CONFIG_PATH, &buf);
    if(buf == NULL)
        return false;

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
        changed = true; // trigger a save on app exit
        intSetMenuLanguage(menuLang);
        return true;
    }

    json_t *configEntry = json_object_get(json, "File Version");
    if(configEntry != NULL && json_is_integer(configEntry))
    {
        int v = json_integer_value(configEntry);
        if(v < 2)
        {
            addToScreenLog("Old vonfig file updating...");
            configEntry = json_object_get(json, "Language");
            if(configEntry != NULL && json_is_string(configEntry))
            {
                if(strcmp(json_string_value(configEntry), LANG_JAP) == 0)
                    lang = Swkbd_LanguageType__Japanese;
                else if(strcmp(json_string_value(configEntry), LANG_ENG) == 0)
                    lang = Swkbd_LanguageType__English;
                else if(strcmp(json_string_value(configEntry), LANG_GER) == 0)
                    lang = Swkbd_LanguageType__French;
                else if(strcmp(json_string_value(configEntry), LANG_ITA) == 0)
                    lang = Swkbd_LanguageType__Italian;
                else if(strcmp(json_string_value(configEntry), LANG_SPA) == 0)
                    lang = Swkbd_LanguageType__Spanish;
                else if(strcmp(json_string_value(configEntry), LANG_CHI) == 0)
                    lang = Swkbd_LanguageType__Chinese1;
                else if(strcmp(json_string_value(configEntry), LANG_KOR) == 0)
                    lang = Swkbd_LanguageType__Korean;
                else if(strcmp(json_string_value(configEntry), LANG_DUT) == 0)
                    lang = Swkbd_LanguageType__Dutch;
                else if(strcmp(json_string_value(configEntry), LANG_POR) == 0)
                    lang = Swkbd_LanguageType__Portuguese;
                else if(strcmp(json_string_value(configEntry), LANG_RUS) == 0)
                    lang = Swkbd_LanguageType__Russian;
                else if(strcmp(json_string_value(configEntry), LANG_TCH) == 0)
                    lang = Swkbd_LanguageType__Chinese2;
                else
                    lang = Swkbd_LanguageType__Invalid;
            }

            menuLang = sysLangToMenuLang(sysLang);
            changed = true;
        }
        else
        {
            configEntry = json_object_get(json, "Keyboard language");
            if(configEntry != NULL && json_is_string(configEntry))
            {
                if(strcmp(json_string_value(configEntry), LANG_JAP) == 0)
                    lang = Swkbd_LanguageType__Japanese;
                else if(strcmp(json_string_value(configEntry), LANG_ENG) == 0)
                    lang = Swkbd_LanguageType__English;
                else if(strcmp(json_string_value(configEntry), LANG_GER) == 0)
                    lang = Swkbd_LanguageType__French;
                else if(strcmp(json_string_value(configEntry), LANG_ITA) == 0)
                    lang = Swkbd_LanguageType__Italian;
                else if(strcmp(json_string_value(configEntry), LANG_SPA) == 0)
                    lang = Swkbd_LanguageType__Spanish;
                else if(strcmp(json_string_value(configEntry), LANG_CHI) == 0)
                    lang = Swkbd_LanguageType__Chinese1;
                else if(strcmp(json_string_value(configEntry), LANG_KOR) == 0)
                    lang = Swkbd_LanguageType__Korean;
                else if(strcmp(json_string_value(configEntry), LANG_DUT) == 0)
                    lang = Swkbd_LanguageType__Dutch;
                else if(strcmp(json_string_value(configEntry), LANG_POR) == 0)
                    lang = Swkbd_LanguageType__Portuguese;
                else if(strcmp(json_string_value(configEntry), LANG_RUS) == 0)
                    lang = Swkbd_LanguageType__Russian;
                else if(strcmp(json_string_value(configEntry), LANG_TCH) == 0)
                    lang = Swkbd_LanguageType__Chinese2;
                else
                    lang = Swkbd_LanguageType__Invalid;
            }
            else
                changed = true;

            configEntry = json_object_get(json, "Menu language");
            if(configEntry != NULL && json_is_string(configEntry))
                menuLang = stringToMenuLang(json_string_value(configEntry));
            else
                changed = true;
        }
    }
    else
    {
        addToScreenLog("Config file version not found!");
        menuLang = sysLangToMenuLang(sysLang);
        changed = true;
    }

    intSetMenuLanguage(menuLang);

    configEntry = json_object_get(json, "Auto resume failed downloads");
    if(configEntry != NULL && json_is_boolean(configEntry))
        autoResume = json_is_true(configEntry);
    else
    {
        addToScreenLog("Auto resume setting not found!");
        changed = true;
    }

#ifndef NUSSPLI_LITE
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
        addToScreenLog("Region setting not foind!");
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
#endif

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
    return true;
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
        case Swkbd_LanguageType__Russian:
            return LANG_RUS;
        case Swkbd_LanguageType__Chinese2:
            return LANG_TCH;
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

bool saveConfig(bool force)
{
    debugPrintf("saveConfig()");
    if(saveFailed)
        return false;

    if(!changed && !force)
        return true;

    json_t *config = json_object();
    bool ret = false;
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
                    value = json_string(menuLangToString(menuLang));
                    if(setValue(config, "Menu language", value))
                    {
                        value = json_string(getLanguageString(lang));
                        if(setValue(config, "Keyboard language", value))
                        {
#ifndef NUSSPLI_LITE
                            value = json_string(getFormattedRegion(getRegion()));
                            if(setValue(config, "Region", value))
                            {
                                value = dlToUSB ? json_true() : json_false();
                                if(setValue(config, "Download to USB", value))
                                {
#endif
                                    value = json_string(getNotificationString(getNotificationMethod()));
                                    if(setValue(config, "Notification method", value))
                                    {
                                        uint32_t entropy;
                                        osslBytes((unsigned char *)&entropy, 4);
                                        value = json_integer(entropy);
                                        if(setValue(config, "Seed", value))
                                        {
                                            char *json = json_dumps(config, JSON_INDENT(4));
                                            if(json != NULL)
                                            {
                                                entropy = strlen(json);
                                                flushIOQueue();
                                                FSFileHandle f = openFile(CONFIG_PATH, "w", 0);
                                                if(f != 0)
                                                {
                                                    addToIOQueue(json, 1, entropy, f);
                                                    addToIOQueue(NULL, 0, 0, f);
                                                    changed = false;
                                                    ret = true;
                                                }
                                                else
                                                    showErrorFrame(gettext("Couldn't save config file!\nYour SD card might be write locked."));

                                                MEMFreeToDefaultHeap(json);
                                            }
                                        }
                                    }
#ifndef NUSSPLI_LITE
                                }
                            }
#endif
                        }
                    }
                }
            }
        }

        json_decref(config);
    }
    else
        debugPrintf("config == NULL");

    return ret;
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

#ifndef NUSSPLI_LITE
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
#endif

MENU_LANGUAGE getMenuLanguage()
{
    return menuLang;
}

void setMenuLanguage(MENU_LANGUAGE lang)
{
    if(menuLang == lang)
        return;

    intSetMenuLanguage(lang);
    menuLang = lang;
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
