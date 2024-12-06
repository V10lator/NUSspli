/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
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

#include <stdbool.h>
#include <stdint.h>

#include <titles.h>
#include <tmd.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
    ￼ * The header is meaned to be human (hex editor) and machine readable.
     * It's hidden inside the tmds signature field. The format is:
    ￼ *  - 0x00010203040506070809 As a magic value / metadata detection
    ￼ *  - 2 byte spacing
    ￼ *  - 64 byte "NUSspli\0" string
    ￼ *  - 64 byte version string ending with 0x00
    ￼ *  - 64 byte file type string ending with 0x00
    ￼ *  - Padding till 0x000000EF / area reserved for future use
    ￼ *  - uint8_t storing the meta version number (currently 1, so 00000001 or 0x01)
    ￼ *  - 128 + 32 random bytes marking the end of the header usable area
    ￼ */
    typedef struct WUT_PACKED
    {
        uint32_t sig_type;

        // Our header
        uint8_t magic_header[0x0A];
        WUT_UNKNOWN_BYTES(2);
        char app[0x10];
        char app_version[0x10];
        char file_type[0x10];
        WUT_UNKNOWN_BYTES(0xAF);
        uint8_t meta_version;
        uint8_t rand_area[0x50];
    } NUS_HEADER;
    WUT_CHECK_OFFSET(NUS_HEADER, 0x0004, magic_header);
    WUT_CHECK_OFFSET(NUS_HEADER, 0x0010, app);
    WUT_CHECK_OFFSET(NUS_HEADER, 0x0020, app_version);
    WUT_CHECK_OFFSET(NUS_HEADER, 0x0030, file_type);
    WUT_CHECK_OFFSET(NUS_HEADER, 0x00EF, meta_version);
    WUT_CHECK_OFFSET(NUS_HEADER, 0x00F0, rand_area);
    WUT_CHECK_SIZE(NUS_HEADER, 0x0140);

    typedef struct WUT_PACKED
    {
        NUS_HEADER header;

        // uint8_t sig[0x100];
        // WUT_UNKNOWN_BYTES(0x3C);
        char issuer[0x40];
        uint8_t ecdsa_pubkey[0x3c];
        uint8_t version;
        uint8_t ca_clr_version;
        uint8_t signer_crl_version;
        uint8_t key[0x10];
        WUT_UNKNOWN_BYTES(0x01);
        uint64_t ticket_id;
        uint32_t device_id;
        uint64_t tid;
        uint16_t sys_access;
        uint16_t title_version;
        WUT_UNKNOWN_BYTES(0x08);
        uint8_t license_type;
        uint8_t ckey_index;
        uint16_t property_mask;
        WUT_UNKNOWN_BYTES(0x28);
        uint32_t account_id;
        WUT_UNKNOWN_BYTES(0x01);
        uint8_t audit;
        WUT_UNKNOWN_BYTES(0x42);
        uint8_t limit_entries[0x40];
        uint16_t header_version; // we support version 1 only!
        uint16_t header_size;
        uint32_t total_hdr_size;
        uint32_t sect_hdr_offset;
        uint16_t num_sect_headers;
        uint16_t num_sect_header_entry_size;
        uint32_t header_flags;
    } TICKET;
    // WUT_CHECK_OFFSET(TICKET, 0x0004, sig);
    WUT_CHECK_OFFSET(TICKET, 0x0140, issuer);
    WUT_CHECK_OFFSET(TICKET, 0x0180, ecdsa_pubkey);
    WUT_CHECK_OFFSET(TICKET, 0x01BC, version);
    WUT_CHECK_OFFSET(TICKET, 0x01BD, ca_clr_version);
    WUT_CHECK_OFFSET(TICKET, 0x01BE, signer_crl_version);
    WUT_CHECK_OFFSET(TICKET, 0x01BF, key);
    WUT_CHECK_OFFSET(TICKET, 0x01D0, ticket_id);
    WUT_CHECK_OFFSET(TICKET, 0x01D8, device_id);
    WUT_CHECK_OFFSET(TICKET, 0x01DC, tid);
    WUT_CHECK_OFFSET(TICKET, 0x01E4, sys_access);
    WUT_CHECK_OFFSET(TICKET, 0x01E6, title_version);
    WUT_CHECK_OFFSET(TICKET, 0x01F0, license_type);
    WUT_CHECK_OFFSET(TICKET, 0x01F1, ckey_index);
    WUT_CHECK_OFFSET(TICKET, 0x01F2, property_mask);
    WUT_CHECK_OFFSET(TICKET, 0x021C, account_id);
    WUT_CHECK_OFFSET(TICKET, 0x0221, audit);
    WUT_CHECK_OFFSET(TICKET, 0x0264, limit_entries);
    WUT_CHECK_OFFSET(TICKET, 0x02A4, header_version);
    WUT_CHECK_OFFSET(TICKET, 0x02A6, header_size);
    WUT_CHECK_OFFSET(TICKET, 0x02A8, total_hdr_size);
    WUT_CHECK_OFFSET(TICKET, 0x02AC, sect_hdr_offset);
    WUT_CHECK_OFFSET(TICKET, 0x02B0, num_sect_headers);
    WUT_CHECK_OFFSET(TICKET, 0x02B2, num_sect_header_entry_size);
    WUT_CHECK_OFFSET(TICKET, 0x02B4, header_flags);
    WUT_CHECK_SIZE(TICKET, 0x02B8);

    typedef struct WUT_PACKED
    {
        uint32_t sig_type;
        uint8_t sig[0x200];
        WUT_UNKNOWN_BYTES(0x3C);
        char issuer[0x40];
        uint32_t version;
        char type[0x40];
        uint8_t cert[0x104];
        uint32_t unknown_01;
        WUT_UNKNOWN_BYTES(0x34);
    } CA3_PPKI_CERT;
    WUT_CHECK_OFFSET(CA3_PPKI_CERT, 0x0004, sig);
    WUT_CHECK_OFFSET(CA3_PPKI_CERT, 0x0240, issuer);
    WUT_CHECK_OFFSET(CA3_PPKI_CERT, 0x0280, version);
    WUT_CHECK_OFFSET(CA3_PPKI_CERT, 0x0284, type);
    WUT_CHECK_OFFSET(CA3_PPKI_CERT, 0x02C4, cert);
    WUT_CHECK_OFFSET(CA3_PPKI_CERT, 0x03C8, unknown_01);
    WUT_CHECK_SIZE(CA3_PPKI_CERT, 0x400);

    typedef struct WUT_PACKED
    {
        uint32_t sig_type;
        uint8_t sig[0x100];
        WUT_UNKNOWN_BYTES(0x3C);
        char issuer[0x40];
        uint32_t version;
        char type[0x40];
        uint8_t cert[0x104];
        uint32_t unknown_01;
        WUT_UNKNOWN_BYTES(0x34);
    } OTHER_PPKI_CERT;
    WUT_CHECK_OFFSET(OTHER_PPKI_CERT, 0x0004, sig);
    WUT_CHECK_OFFSET(OTHER_PPKI_CERT, 0x0140, issuer);
    WUT_CHECK_OFFSET(OTHER_PPKI_CERT, 0x0180, version);
    WUT_CHECK_OFFSET(OTHER_PPKI_CERT, 0x0184, type);
    WUT_CHECK_OFFSET(OTHER_PPKI_CERT, 0x01C4, cert);
    WUT_CHECK_OFFSET(OTHER_PPKI_CERT, 0x02C8, unknown_01);
    WUT_CHECK_SIZE(OTHER_PPKI_CERT, 0x300);

    typedef struct WUT_PACKED
    {
        CA3_PPKI_CERT cert1;
        OTHER_PPKI_CERT cert2;
        OTHER_PPKI_CERT cert3;
    } CETK;
    WUT_CHECK_OFFSET(CETK, sizeof(CA3_PPKI_CERT), cert2);
    WUT_CHECK_OFFSET(CETK, sizeof(CA3_PPKI_CERT) + sizeof(OTHER_PPKI_CERT), cert3);
    WUT_CHECK_SIZE(CETK, sizeof(CA3_PPKI_CERT) + (sizeof(OTHER_PPKI_CERT) * 2));

    bool generateTik(const char *path, const TMD *tmd);
    bool generateCert(const TMD *tmd, const TICKET *ticket, size_t ticketSize, const char *path);
    void generateFakeTicket();
    void deleteTicket(uint64_t tid);
    bool hasMagicHeader(const TICKET *ticket);

#ifdef __cplusplus
}
#endif
