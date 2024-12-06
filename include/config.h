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

#pragma once

#include <wut-fixups.h>

#include <stdbool.h>

#include <file.h>
#include <localisation.h>
#include <swkbd_wrapper.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#pragma GCC diagnostic pop

#define CONFIG_PATH            NUSDIR_SD "NUSspli.txt"
#define TITLE_KEY_URL_MAX_SIZE 1024

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        NOTIF_METHOD_NONE = 0x00,
        NOTIF_METHOD_RUMBLE = 0x01,
        NOTIF_METHOD_LED = 0x02,
    } NOTIF_METHOD;

    void initConfig();
    void saveConfig(bool force);
    bool updateCheckEnabled();
    void setUpdateCheck(bool enabled);
    bool autoResumeEnabled();
    void setAutoResume(bool enabled);
    const char *getFormattedRegion(MCPRegion region);
    Swkbd_LanguageType getKeyboardLanguage();
    Swkbd_LanguageType getUnfilteredLanguage();
    MCPRegion getRegion();
    void setRegion(MCPRegion region);
    void setKeyboardLanguage(Swkbd_LanguageType language);
    const char *getLanguageString(Swkbd_LanguageType language);
    const char *getNotificationString(NOTIF_METHOD method);
    bool dlToUSBenabled();
    void setDlToUSB(bool toUSB);
    NOTIF_METHOD getNotificationMethod();
    void setNotificationMethod(NOTIF_METHOD method);
    Swkbd_LanguageType getMenuLanguage();
    void setMenuLanguage(Swkbd_LanguageType language);

#ifdef __cplusplus
}
#endif
