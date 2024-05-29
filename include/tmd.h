/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2021-2022 V10lator <v10lator@myway.de>                    *
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
#include <wut_structsize.h>
#pragma GCC diagnostic pop

#ifdef __cplusplus
extern "C"
{
#endif

    // From: https://wiiubrew.org/wiki/Title_metadata
    // And: https://github.com/Maschell/nuspacker

#define TMD_CONTENT_TYPE_ENCRYPTED 0x0001
#define TMD_CONTENT_TYPE_HASHED    0x0002 // Never seen alone, alsways combined with TMD_CONTENT_TYPE_ENCRYPTED
#define TMD_CONTENT_TYPE_CONTENT   0x2000
#define TMD_CONTENT_TYPE_UNKNOWN   0x4000 // Never seen alone, alsways combined with TMD_CONTENT_TYPE_CONTENT

    typedef struct WUT_PACKED
    {
        uint32_t cid;
        uint16_t index;
        uint16_t type;
        uint64_t size;
        uint32_t hash[8];
    } TMD_CONTENT;
    WUT_CHECK_OFFSET(TMD_CONTENT, 0x00, cid);
    WUT_CHECK_OFFSET(TMD_CONTENT, 0x04, index);
    WUT_CHECK_OFFSET(TMD_CONTENT, 0x06, type);
    WUT_CHECK_OFFSET(TMD_CONTENT, 0x08, size);
    WUT_CHECK_OFFSET(TMD_CONTENT, 0x10, hash);
    WUT_CHECK_SIZE(TMD_CONTENT, 0x30);

    typedef struct WUT_PACKED
    {
        uint16_t index;
        uint16_t count;
        uint32_t hash[8];
    } TMD_CONTENT_INFO;
    WUT_CHECK_OFFSET(TMD_CONTENT_INFO, 0x00, index);
    WUT_CHECK_OFFSET(TMD_CONTENT_INFO, 0x02, count);
    WUT_CHECK_OFFSET(TMD_CONTENT_INFO, 0x04, hash);
    WUT_CHECK_SIZE(TMD_CONTENT_INFO, 0x24);

    typedef struct WUT_PACKED
    {
        uint32_t sig_type;
        uint8_t sig[256];
        WUT_UNKNOWN_BYTES(60);
        uint8_t issuer[64];
        uint8_t version;
        uint8_t ca_crl_version;
        uint8_t signer_crl_version;
        WUT_UNKNOWN_BYTES(1);
        uint64_t sys_version;
        uint64_t tid;
        uint32_t type;
        uint16_t group;
        WUT_UNKNOWN_BYTES(62);
        uint32_t access_rights;
        uint16_t title_version;
        uint16_t num_contents;
        uint16_t boot_index;
        WUT_UNKNOWN_BYTES(2);
        uint32_t hash[8];
        TMD_CONTENT_INFO content_infos[64];
        TMD_CONTENT contents[0];
    } TMD;
    WUT_CHECK_OFFSET(TMD, 0x0000, sig_type);
    WUT_CHECK_OFFSET(TMD, 0x0004, sig);
    WUT_CHECK_OFFSET(TMD, 0x0140, issuer);
    WUT_CHECK_OFFSET(TMD, 0x0180, version);
    WUT_CHECK_OFFSET(TMD, 0x0181, ca_crl_version);
    WUT_CHECK_OFFSET(TMD, 0x0182, signer_crl_version);
    WUT_CHECK_OFFSET(TMD, 0x0184, sys_version);
    WUT_CHECK_OFFSET(TMD, 0x018C, tid);
    WUT_CHECK_OFFSET(TMD, 0x0194, type);
    WUT_CHECK_OFFSET(TMD, 0x0198, group);
    WUT_CHECK_OFFSET(TMD, 0x01D8, access_rights);
    WUT_CHECK_OFFSET(TMD, 0x01DC, title_version);
    WUT_CHECK_OFFSET(TMD, 0x01DE, num_contents);
    WUT_CHECK_OFFSET(TMD, 0x01E0, boot_index);
    WUT_CHECK_OFFSET(TMD, 0x01E4, hash);
    WUT_CHECK_OFFSET(TMD, 0x0204, content_infos);
    WUT_CHECK_OFFSET(TMD, 0x0B04, contents);
    WUT_CHECK_SIZE(TMD, 0x0B04);

#ifdef __cplusplus
}
#endif
