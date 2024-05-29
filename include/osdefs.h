/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2021 V10lator <v10lator@myway.de>                    *
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

#include <stdint.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#include <nn/acp/title.h>
#pragma GCC diagnostic pop

#ifdef __cplusplus
extern "C"
{
#endif

    // ACP
    extern ACPResult ACPGetTitleMetaXmlByTitleListType(MCPTitleListType *titleListType, ACPMetaXml *out);

    // ACT
    uint32_t GetPersistentId() asm("GetPersistentId__Q2_2nn3actFv");

    // MCP
    extern MCPError MCP_DeleteTitleAsync(int handle, char *path, MCPInstallTitleInfo *out);

    // GX2
    extern void GX2SetDRCGamma(float gamma);
    extern void GX2SetTVGamma(float gamma);

#ifdef __cplusplus
}
#endif
