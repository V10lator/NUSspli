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

#ifndef NUSSPLI_LITE

#include <wut-fixups.h>

#include <ticket.h>

#include <crypto.h>
#include <file.h>
#include <input.h>
#include <ioQueue.h>
#include <keygen.h>
#include <localisation.h>
#include <menu/filebrowser.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>

static const uint8_t magic_header[10] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };

static void generateHeader(FileType type, NUS_HEADER *out)
{
    OSBlockMove(out->magic_header, magic_header, 10, false);
    OSBlockMove(out->app, "NUSspli", strlen("NUSspli"), false);
    OSBlockMove(out->app_version, NUSSPLI_VERSION, strlen(NUSSPLI_VERSION), false);

    if(type == FILE_TYPE_TIK)
        OSBlockMove(out->file_type, "Ticket", strlen("Ticket"), false);
    else
        OSBlockMove(out->file_type, "Certificate", strlen("Certificate"), false);

    out->sig_type = 0x00010004;
    out->meta_version = 0x01;
    osslBytes(out->rand_area, sizeof(out->rand_area));
}

/* This generates a ticket and writes it to a file.
 * The ticket algorithm is ported from FunkiiU combined
 * with the randomness of NUSPackager to create unique
 * NUSspli tickets.
 */
bool generateTik(const char *path, const TitleEntry *titleEntry, const TMD *tmd)
{
    TICKET ticket;
    OSBlockSet(&ticket, 0x00, sizeof(TICKET));

    if(!generateKey(titleEntry, ticket.key))
        return false;

    generateHeader(FILE_TYPE_TIK, &ticket.header);
    osslBytes(&ticket.ecdsa_pubkey, sizeof(ticket.ecdsa_pubkey));
    osslBytes(&ticket.ticket_id, sizeof(uint64_t));
    ticket.ticket_id &= 0x0000FFFFFFFFFFFF;
    ticket.ticket_id |= 0x0005000000000000;

    OSBlockMove(ticket.issuer, "Root-CA00000003-XS0000000c", strlen("Root-CA00000003-XS0000000c"), false);

    ticket.version = 0x01;
    ticket.ca_clr_version = 0x01;

    ticket.tid = titleEntry->tid;
    ticket.title_version = tmd->version;
    ticket.property_mask = 0x0006;
    ticket.audit = 0x01;

    // We support zero sections only
    ticket.header_version = 0x0001;
    ticket.total_hdr_size = 0x14;

    FSFileHandle tik = openFile(path, "w", 0);
    if(tik == 0)
    {
        char *err = getStaticScreenBuffer();
        sprintf(err, "%s\n%s", gettext("Could not open path"), prettyDir(path));
        showErrorFrame(err);
        return false;
    }

    addToIOQueue(&ticket, 1, sizeof(TICKET), tik);
    addToIOQueue(NULL, 0, 0, tik);
    return true;
}

bool generateCert(const char *path)
{
    CETK cetk;
    OSBlockSet(&cetk, 0x00, sizeof(CETK));

    generateHeader(FILE_TYPE_CERT, &cetk.header);

    OSBlockMove(cetk.cert1.issuer, "Root-CA00000003", strlen("Root-CA00000003"), false);
    OSBlockMove(cetk.cert1.type, "CP0000000b", strlen("CP0000000b"), false);

    OSBlockMove(cetk.cert2.issuer, "Root", strlen("Root"), false);
    OSBlockMove(cetk.cert2.type, "CA00000003", strlen("CA00000003"), false);

    OSBlockMove(cetk.cert3.issuer, "Root-CA00000003", strlen("Root-CA00000003"), false);
    OSBlockMove(cetk.cert3.type, "XS0000000c", strlen("XS0000000c"), false);

    osslBytes(&cetk.cert1.sig, sizeof(cetk.cert1.sig));
    osslBytes(&cetk.cert1.cert, sizeof(cetk.cert1.cert));
    osslBytes(&cetk.cert2.sig, sizeof(cetk.cert2.sig));
    osslBytes(&cetk.cert2.cert, sizeof(cetk.cert2.cert));
    osslBytes(&cetk.cert3.sig, sizeof(cetk.cert3.sig));

    cetk.cert1.version = 0x01;
    cetk.cert1.unknown_01 = 0x00010001;
    cetk.cert1.unknown_02 = 0x00010003;

    cetk.cert2.version = 0x01;
    cetk.cert2.unknown_01 = 0x00010001;
    cetk.cert2.unknown_02 = 0x00010004;

    cetk.cert3.version = 0x01;
    cetk.cert3.unknown_01 = 0x00010001;

    FSFileHandle cert = openFile(path, "w", 0);
    if(cert == 0)
    {
        char *err = getStaticScreenBuffer();
        sprintf(err, "%s\n%s", gettext("Could not open path"), prettyDir(path));
        showErrorFrame(err);
        return false;
    }

    addToIOQueue(&cetk, 1, sizeof(CETK), cert);
    addToIOQueue(NULL, 0, 0, cert);
    return true;
}

static void drawTicketFrame(uint64_t titleID)
{
    char tid[17];
    hex(titleID, 16, tid);

    startNewFrame();
    textToFrame(0, 0, gettext("Title ID:"));
    textToFrame(1, 3, tid);

    int line = MAX_LINES - 1;
    textToFrame(line--, 0, gettext("Press " BUTTON_B " to return"));
    textToFrame(line--, 0, gettext("Press " BUTTON_A " to continue"));
    lineToFrame(line, SCREEN_COLOR_WHITE);
    drawFrame();
}

static void drawTicketGenFrame(const char *dir)
{
    colorStartNewFrame(SCREEN_COLOR_D_GREEN);
    textToFrame(0, 0, gettext("Fake ticket generated on:"));
    textToFrame(1, 0, prettyDir(dir));

    textToFrame(3, 0, gettext("Press any key to return"));
    drawFrame();
}
void generateFakeTicket()
{
    char *dir;
    TMD *tmd;
gftEntry:
    dir = fileBrowserMenu(false);
    if(dir == NULL || !AppRunning(true))
        return;

    tmd = getTmd(dir);
    if(tmd == NULL)
    {
        showErrorFrame(gettext("Invalid title.tmd file!"));
        return;
    }

    drawTicketFrame(tmd->tid);

    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            drawTicketFrame(tmd->tid);

        showFrame();

        if(vpad.trigger & VPAD_BUTTON_A)
        {
            startNewFrame();
            textToFrame(0, 0, gettext("Generating fake ticket..."));
            drawFrame();
            showFrame();

            strcat(dir, "title.");
            char *ptr = dir + strlen(dir);
            strcpy(ptr, "cert");
            if(!generateCert(dir))
                break;

            const TitleEntry *entry = getTitleEntryByTid(tmd->tid);
            const TitleEntry te = { .name = "UNKNOWN", .tid = tmd->tid, .region = MCP_REGION_UNKNOWN, .key = 99 };
            if(entry == NULL)
                entry = &te;

            strcpy(ptr, "tik");
            if(!generateTik(dir, entry, tmd))
                break;

            drawTicketGenFrame(dir);

            while(AppRunning(true))
            {
                if(app == APP_STATE_BACKGROUND)
                    continue;
                if(app == APP_STATE_RETURNING)
                    drawTicketGenFrame(dir);

                showFrame();
                if(vpad.trigger)
                    break;
            }
            break;
        }
        if(vpad.trigger & VPAD_BUTTON_B)
        {
            MEMFreeToDefaultHeap(tmd);
            goto gftEntry;
        }
    }

    MEMFreeToDefaultHeap(tmd);
}

#endif
