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

#pragma once

#include <wut-fixups.h>

#include <stdbool.h>
#include <stdint.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#pragma GCC diagnostic pop

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        APP_STATE_STOPPING,
        APP_STATE_HOME,
        APP_STATE_STOPPED,
        APP_STATE_RUNNING,
        APP_STATE_BACKGROUND,
        APP_STATE_RETURNING,

    } APP_STATE;

    extern volatile APP_STATE app;

    void initState() __attribute__((__cold__));
    void deinitState() __attribute__((__cold__));
    void enableApd();
    void disableApd();
    void enableShutdown();
    void disableShutdown();
#ifndef NUSSPLI_LITE
    bool isChannel();
#endif
    bool AppRunning(bool mainthread) __attribute__((__hot__));
    uint32_t homeButtonCallback(void *dummy);
    void launchTitle(MCPTitleListType *title) __attribute__((__cold__));
    void relaunch() __attribute__((__cold__));
    bool launchingTitle() __attribute__((__cold__));

#ifdef __cplusplus
}
#endif
