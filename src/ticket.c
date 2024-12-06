/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2024 V10lator <v10lator@myway.de>                    *
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
#include <stdio.h>
#include <string.h>

#include <ticket.h>

#include <crypto.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <ioQueue.h>
#include <keygen.h>
#include <list.h>
#include <localisation.h>
#include <menu/filebrowser.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define TICKET_BUCKET "/vol/slc/sys/rights/ticket/apps/"

typedef struct
{
    uint8_t *start;
    size_t size;
} TICKET_SECTION;

typedef struct WUT_PACKED
{
    uint32_t unk01;
    uint32_t unk02;
    uint32_t unk03;
    uint32_t unk04;
    uint16_t unk05;
    WUT_UNKNOWN_BYTES(0x06);
    uint32_t unk06[8];
    WUT_UNKNOWN_BYTES(0x60);
} TICKET_HEADER_SECTION;
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x04, unk02);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x08, unk03);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x0C, unk04);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x10, unk05);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x18, unk06);
WUT_CHECK_SIZE(TICKET_HEADER_SECTION, 0x98);

static const uint8_t magic_header[10] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
static uint8_t default_cert[sizeof(OTHER_PPKI_CERT)] = { 0xff };

static void generateHeader(FileType type, NUS_HEADER *out)
{
    OSBlockMove(out->magic_header, magic_header, 10, false);
    OSBlockMove(out->app, "NUSspli", sizeof("NUSspli") - 1, false);
    OSBlockMove(out->app_version, NUSSPLI_VERSION, sizeof(NUSSPLI_VERSION) - 1, false);

    if(type == FILE_TYPE_TIK)
        OSBlockMove(out->file_type, "Ticket", sizeof("Ticket") - 1, false);
    else
        OSBlockMove(out->file_type, "Certificate", sizeof("Certificate") - 1, false);

    out->sig_type = type == FILE_TYPE_TIK ? 0x00010004 : 0x00010003;
    out->meta_version = 0x01;
    osslBytes(out->rand_area, sizeof(out->rand_area));
}

bool generateTik(const char *path, const TMD *tmd)
{
    TICKET ticket;
    OSBlockSet(&ticket, 0x00, sizeof(TICKET));

    if(!generateKey(tmd->tid, ticket.key))
        return false;

    generateHeader(FILE_TYPE_TIK, &ticket.header);
    osslBytes(&ticket.ecdsa_pubkey, sizeof(ticket.ecdsa_pubkey));
    osslBytes(&ticket.ticket_id, sizeof(uint64_t));
    ticket.ticket_id &= 0x0000FFFFFFFFFFFF;
    ticket.ticket_id |= 0x0005000000000000;

    OSBlockMove(ticket.issuer, "Root-CA00000003-XS0000000c", sizeof("Root-CA00000003-XS0000000c") - 1, false);

    ticket.version = 0x01;
    ticket.tid = tmd->tid;
    ticket.title_version = tmd->title_version;
    ticket.property_mask = 0xFFFF;

    ticket.header_version = 0x0001;
    // Normal tickets don't need header sections but DLC needs one
    if(!isDLC(tmd->tid))
        ticket.total_hdr_size = 0x00000014;
    else
    {
        ticket.total_hdr_size = 0x000000AC;
        ticket.sect_hdr_offset = 0x00000014;
        ticket.num_sect_headers = 0x0001;
        ticket.num_sect_header_entry_size = 0x0014;
    }

    FSAFileHandle tik = openFile(path, "w", 0);
    if(tik == 0)
    {
        char *err = getStaticScreenBuffer();
        sprintf(err, "%s\n%s", localise("Could not open path"), prettyDir(path));
        showErrorFrame(err);
        return false;
    }

    addToIOQueue(&ticket, 1, sizeof(TICKET), tik);

    if(isDLC(tmd->tid))
    {
        TICKET_HEADER_SECTION section;
        OSBlockSet(&section, 0x00, sizeof(TICKET_HEADER_SECTION));

        section.unk01 = 0x00000028;
        section.unk02 = 0x00000001;
        section.unk03 = 0x00000084;
        section.unk04 = 0x00000084;
        section.unk05 = 0x0003;
        for(int i = 0; i < 8; i++)
            section.unk06[i] = 0xFFFFFFFF;

        addToIOQueue(&section, 1, sizeof(TICKET_HEADER_SECTION), tik);
    }

    addToIOQueue(NULL, 0, 0, tik);
    return true;
}

static uint8_t *getDefaultCert()
{
    uint8_t *ret = NULL;
    if(default_cert[0] == 0xff)
    {
        RAMBUF *rambuf = allocRamBuf();
        if(rambuf != NULL)
        {
            if(downloadFile(DOWNLOAD_URL "000500101000400a/cetk", "OSv10 title.tik", NULL, FILE_TYPE_TIK | FILE_TYPE_TORAM, false, NULL, rambuf) == 0)
            {
                if(rambuf->size >= 0x350 + sizeof(OTHER_PPKI_CERT)) // TODO
                {
                    OSBlockMove(default_cert, rambuf->buf + 0x350, sizeof(OTHER_PPKI_CERT), false);
                    ret = default_cert;
                }
            }

            freeRamBuf(rambuf);
        }
    }
    else
        return default_cert;

    return ret;
}

static void *getCert(int id, const TMD *tmd)
{
    const uint8_t *ptr = (const uint8_t *)tmd;
    ptr += sizeof(TMD) + (sizeof(TMD_CONTENT) * tmd->num_contents);
    if(id == 0)
        ptr += sizeof(OTHER_PPKI_CERT);

    return (void *)ptr;
}

bool generateCert(const TMD *tmd, const TICKET *ticket, size_t ticketSize, const char *path)
{
    CETK cetk;

    if(ticketSize == 0)
    {

        OSBlockSet(&cetk, 0x00, sizeof(CETK));

        OSBlockMove(cetk.cert1.issuer, "Root", sizeof("Root") - 1, false);
        OSBlockMove(cetk.cert1.type, "CA00000003", sizeof("CA00000003") - 1, false);

        OSBlockMove(cetk.cert2.issuer, "Root-CA00000003", sizeof("Root-CA00000003") - 1, false);
        OSBlockMove(cetk.cert2.type, "CP0000000b", sizeof("CP0000000b") - 1, false);

        OSBlockMove(cetk.cert3.issuer, "Root-CA00000003", sizeof("Root-CA00000003") - 1, false);
        OSBlockMove(cetk.cert3.type, "XS0000000c", sizeof("XS0000000c") - 1, false);

        osslBytes(&cetk.cert1.sig, sizeof(cetk.cert1.sig));
        osslBytes(&cetk.cert1.cert, sizeof(cetk.cert1.cert));
        osslBytes(&cetk.cert2.sig, sizeof(cetk.cert2.sig));
        osslBytes(&cetk.cert2.cert, sizeof(cetk.cert2.cert));
        osslBytes(&cetk.cert3.sig, sizeof(cetk.cert3.sig));

        cetk.cert1.sig_type = 0x00010003;
        cetk.cert1.version = 0x00000001;
        cetk.cert1.unknown_01 = 0x00010001;

        cetk.cert2.sig_type = 0x00010004;
        cetk.cert2.version = 0x00000001;
        cetk.cert2.unknown_01 = 0x00010001;

        cetk.cert3.sig_type = 0x00010004;
        cetk.cert3.version = 0x00000001;
        cetk.cert2.unknown_01 = 0x00010001;

        // Overrite header
        OSBlockSet(&cetk, 0x00, sizeof(NUS_HEADER));
        generateHeader(FILE_TYPE_CERT, (NUS_HEADER *)&cetk);
    }
    else
    {
        const uint8_t *ptr;
        if(ticketSize >= 0x350 + sizeof(OTHER_PPKI_CERT)) // TODO
        {
            ptr = (uint8_t *)ticket;
            ptr += 0x350;
        }
        else
        {
            ptr = getDefaultCert();
            if(ptr == NULL)
                return generateCert(tmd, NULL, 0, path);
        }

        OSBlockMove(&cetk.cert1, getCert(0, tmd), sizeof(CA3_PPKI_CERT), false);
        OSBlockMove(&cetk.cert2, getCert(1, tmd), sizeof(OTHER_PPKI_CERT), false);
        OSBlockMove(&cetk.cert3, ptr, sizeof(OTHER_PPKI_CERT), false);
    }

    FSAFileHandle cert = openFile(path, "w", 0);
    if(cert == 0)
    {
        char *err = getStaticScreenBuffer();
        sprintf(err, "%s\n%s", localise("Could not open path"), prettyDir(path));
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
    textToFrame(0, 0, localise("Title ID:"));
    textToFrame(1, 3, tid);

    int line = MAX_LINES - 1;
    textToFrame(line--, 0, localise("Press " BUTTON_B " to return"));
    textToFrame(line--, 0, localise("Press " BUTTON_A " to continue"));
    lineToFrame(line, SCREEN_COLOR_WHITE);
    drawFrame();
}

static void drawTicketGenFrame(const char *dir)
{
    colorStartNewFrame(SCREEN_COLOR_D_GREEN);
    textToFrame(0, 0, localise("Fake ticket generated on:"));
    textToFrame(1, 0, prettyDir(dir));

    textToFrame(3, 0, localise("Press any key to return"));
    drawFrame();
}

static void browseFiles(char *out)
{
    const char *dir = fileBrowserMenu(false, false);
    if(dir)
    {
        strcpy(out, dir);
        MEMFreeToDefaultHeap((void *)dir);
    }
    else
        *out = '\0';
}

void generateFakeTicket()
{
    char dir[FS_MAX_PATH];
    TMD *tmd;
gftEntry:
    browseFiles(dir);
    if(*dir == '\0')
        return;

    tmd = getTmd(dir, false);
    if(tmd == NULL)
    {
        showErrorFrame(localise("Invalid title.tmd file!"));
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
            textToFrame(0, 0, localise("Generating fake ticket..."));
            drawFrame();
            showFrame();

            strcat(dir, "title.");
            char *ptr = dir + strlen(dir);
            strcpy(ptr, "cert");
            if(!generateCert(tmd, NULL, 0, dir))
                break;

            strcpy(ptr, "tik");
            if(!generateTik(dir, tmd))
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

void deleteTicket(uint64_t tid)
{
    LIST *ticketList = createList();
    if(ticketList == NULL)
        return;

    char *path = getStaticPathBuffer(0);
    OSBlockMove(path, TICKET_BUCKET, sizeof(TICKET_BUCKET), false);

    char *inSentence = path + (sizeof(TICKET_BUCKET) - 1);
    FSADirectoryHandle dir;
    OSTime t = OSGetTime();
    FSError ret = FSAOpenDir(getFSAClient(), path, &dir);
    if(ret != FS_ERROR_OK)
    {
        debugPrintf("Error opening %s: %s", path, translateFSErr(ret));
        return;
    }

    FSADirectoryEntry entry;
    FSADirectoryHandle dir2;
    char *fileName;
    void *file;
    size_t fileSize;
    TICKET *ticket;
    TICKET_SECTION *sec;
    bool found;
    uint8_t *fileEnd;
    uint8_t *ptr;
    // Loop through all the folder inside of the ticket bucket
    while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
    {
        if(!(entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 4)
        {
            debugPrintf("Sanity check failed on %s", entry.name);
            continue;
        }

        strcpy(inSentence, entry.name);
        ret = FSAOpenDir(getFSAClient(), path, &dir2);
        if(ret == FS_ERROR_OK)
        {
            strcat(inSentence, "/");
            fileName = inSentence + strlen(inSentence);
            // Loop through all the subfolders
            while(FSAReadDir(getFSAClient(), dir2, &entry) == FS_ERROR_OK)
            {
                if((entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 12) // TODO: entry.info.flags & FS_STAT_FILE is false for some reason
                {
                    debugPrintf("Sanity check failed on %s", entry.name);
                    continue;
                }

                strcpy(fileName, entry.name);
                fileSize = readFile(path, &file);
                if(file != NULL)
                {
                    ticket = (TICKET *)file;
                    fileEnd = ((uint8_t *)file) + fileSize;
                    found = false;
                    // Loop through all the tickets inside of a file
                    while(true)
                    {
                        ptr = ((uint8_t *)ticket) + sizeof(TICKET);
                        if(ticket->total_hdr_size > 0x14)
                            ptr += ticket->total_hdr_size - 0x14;

                        // Ignore the ticket in case the title ID matches
                        if(ticket->tid == tid)
                        {
                            found = true;
                            debugPrintf("Ticket found at %s+0x%X", path, ((uint8_t *)ticket) - ((uint8_t *)file));
                        }
                        // Else remeber its offset and size
                        else
                        {
                            sec = MEMAllocFromDefaultHeap(sizeof(TICKET_SECTION));
                            if(sec)
                            {
                                sec->start = (uint8_t *)ticket;
                                sec->size = ptr - sec->start;

                                if(!addToListEnd(ticketList, sec))
                                {
                                    MEMFreeToDefaultHeap(sec);
                                    debugPrintf("Error allocating memory!");
                                    found = false;
                                    break;
                                }
                            }
                            else
                            {
                                debugPrintf("Error allocating memory!");
                                found = false;
                                break;
                            }
                        }

                        if(ptr == fileEnd)
                            break;
                        if(ptr > fileEnd)
                        {
                            debugPrintf("Filesize missmatch!");
                            found = false;
                            break;
                        }

                        ticket = (TICKET *)ptr;
                    }

                    // In case there was a matching ticket inside of the file either delete or recreate it with the remembered tickets only
                    if(found)
                    {
                        if(getListSize(ticketList) == 0)
                            FSARemove(getFSAClient(), path);
                        else
                        {
                            FSAFileHandle fh = openFile(path, "w", 0);
                            forEachListEntry(ticketList, sec)
                                addToIOQueue(sec->start, 1, sec->size, fh);

                            addToIOQueue(NULL, 0, 0, fh);
                        }
                    }

                    clearList(ticketList, true);
                    MEMFreeToDefaultHeap(file);
                }
            }

            FSACloseDir(getFSAClient(), dir2);
        }
        else
            debugPrintf("Error opening %s: %s", path, translateFSErr(ret));
    }

    FSACloseDir(getFSAClient(), dir);
    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));
    destroyList(ticketList, true);
}

bool hasMagicHeader(const TICKET *ticket)
{
    for(size_t i = 0; i < sizeof(magic_header); ++i)
        if(ticket->header.magic_header[i] != magic_header[i])
            return false;

    return ticket->header.meta_version == 0x01;
}
