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
#include <ioQueue.h>
#include <keygen.h>
#include <list.h>
#include <localisation.h>
#include <menu/engineFrames.h>
#include <menu/filebrowser.h>
#include <menu/utils.h>
#include <state.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#include <mbedtls/sha1.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define TICKET_BUCKET "/vol/slc/sys/rights/ticket/apps/"
// What a ticket of Nintendo carries, and where the file that is about to be
// changed is copied before it is touched.
#define TICKET_ISSUER     "Root-CA00000003-XS0000000c"
#define TICKET_SIG_RSA    0x00010004 // RSA-2048 with SHA-256
#define TICKET_BACKUP_DIR NUSDIR_SD "NUSspli-tickets/"
#define TICKET_TMP        NUSDIR_SD "NUSspli-plant.tik"

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

// cert3 (XS0000000c) of a downloaded cetk starts at 0x350 - that is sizeof(TICKET)
// plus sizeof(TICKET_HEADER_SECTION):
#define CETK_CERT3_OFFSET (sizeof(TICKET) + sizeof(TICKET_HEADER_SECTION))

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
        char err[FS_MAX_PATH + 256];
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
                if(rambuf->size >= CETK_CERT3_OFFSET + sizeof(OTHER_PPKI_CERT))
                {
                    OSBlockMove(default_cert, rambuf->buf + CETK_CERT3_OFFSET, sizeof(OTHER_PPKI_CERT), false);
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
        cetk.cert3.unknown_01 = 0x00010001;

        // Overrite header
        OSBlockSet(&cetk, 0x00, sizeof(NUS_HEADER));
        generateHeader(FILE_TYPE_CERT, (NUS_HEADER *)&cetk);
    }
    else
    {
        const uint8_t *ptr;
        if(ticketSize >= CETK_CERT3_OFFSET + sizeof(OTHER_PPKI_CERT))
        {
            ptr = (uint8_t *)ticket;
            ptr += CETK_CERT3_OFFSET;
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
        char err[FS_MAX_PATH + 256];
        sprintf(err, "%s\n%s", localise("Could not open path"), prettyDir(path));
        showErrorFrame(err);
        return false;
    }

    addToIOQueue(&cetk, 1, sizeof(CETK), cert);
    addToIOQueue(NULL, 0, 0, cert);
    return true;
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

    ErrorOptions choice = showTicketConfirmDialog(tmd->tid);
    if(choice == 0) // The app stopped while the dialog was up
    {
        MEMFreeToDefaultHeap(tmd);
        return;
    }
    if(choice & B_RETURN)
    {
        MEMFreeToDefaultHeap(tmd);
        goto gftEntry;
    }

    showStatusFrame(localise("Generating fake ticket..."), NULL, false, -1);

    // fileBrowserMenu() leaves room for exactly this behind the folder it
    // hands out, so this only guards against a shorter way in later on:
    // never write the two names past the buffer.
    if(strlen(dir) + sizeof("title.cert") > FS_MAX_PATH)
    {
        debugPrintf("Path too long: %s", dir);
        goto gftFree;
    }

    strcat(dir, "title.");
    char *ptr = dir + strlen(dir);
    strcpy(ptr, "cert");
    if(!generateCert(tmd, NULL, 0, dir))
        goto gftFree;

    strcpy(ptr, "tik");
    if(!generateTik(dir, tmd))
        goto gftFree;

    showTicketGeneratedDialog(dir);

gftFree:
    MEMFreeToDefaultHeap(tmd);
}

/*
 * The file a ticket belongs in: the low nibble of the third byte of the
 * SHA-1 of the title ID names the folder, the low five bits of the fourth
 * name the file. That is what the store of a 5.5.5E console showed, and the
 * walk of hasTicket() finds a ticket wherever it ends up anyway.
 */
static void ticketPath(uint64_t tid, char *path, size_t size)
{
    uint8_t hash[20];
    mbedtls_sha1((const unsigned char *)&tid, sizeof(tid), hash);
    snprintf(path, size, TICKET_BUCKET "%04x/%08x.tik", hash[2] & 0x0F, hash[3] & 0x1F);
}

// How much of a buffer is the ticket itself: the struct and the header
// sections behind it, the way deleteTicket() walks them. 0 when the buffer
// holds no ticket that makes sense. Not named ticketSize because
// generateCert() takes an argument of that name.
static size_t ticketRecordSize(const void *buf, size_t avail)
{
    if(buf == NULL || avail < sizeof(TICKET))
        return 0;

    const TICKET *ticket = (const TICKET *)buf;
    size_t size = sizeof(TICKET);
    if(ticket->total_hdr_size > 0x14)
        size += ticket->total_hdr_size - 0x14;

    return size <= avail && size <= 0x1000 ? size : 0;
}

// A cetk of the servers of Nintendo: the ticket anyone gets for free content,
// which is the one that belongs to every console instead of to one.
static bool isPublicTicket(const void *buf, size_t size, uint64_t tid)
{
    if(ticketRecordSize(buf, size) == 0)
        return false;

    uint32_t sigType;
    OSBlockMove(&sigType, buf, sizeof(sigType), false);
    const TICKET *ticket = (const TICKET *)buf;
    return sigType == TICKET_SIG_RSA && strncmp(ticket->issuer, TICKET_ISSUER, sizeof(ticket->issuer)) == 0 && ticket->tid == tid && ticket->device_id == 0 && ticket->account_id == 0;
}

// The file about to be changed, copied to the SD card first. Returns false
// when the copy could not be made, which stops the write: a change to the
// store of the console that cannot be taken back is not worth making.
static bool backupBucket(const char *path, const char *name)
{
    // Nothing there yet is nothing to lose: the file is made by the append
    // and taking it back is removing it again.
    if(!fileExists(path))
        return true;

    if(!dirExists(TICKET_BACKUP_DIR) && createDirectory(TICKET_BACKUP_DIR) != FS_ERROR_OK)
        return false;

    void *file;
    size_t size = readFile(path, &file);
    // A file that is there but will not be read is one that must not be
    // appended to either: there would be no way back.
    if(file == NULL)
        return false;

    char backup[FS_MAX_PATH];
    snprintf(backup, sizeof(backup), TICKET_BACKUP_DIR "%s", name);
    FSAFileHandle fh = openFile(backup, "w", size);
    if(fh == 0)
    {
        MEMFreeToDefaultHeap(file);
        return false;
    }

    addToIOQueue(file, 1, size, fh);
    addToIOQueue(NULL, 0, 0, fh);
    flushIOQueue();
    MEMFreeToDefaultHeap(file);
    return !checkForQueueErrors();
}

// Puts the file back the way the backup has it, for a write that did not
// end up with the ticket the console can find.
static void restoreBucket(const char *path, const char *name, bool existed)
{
    if(!existed)
    {
        FSARemove(getFSAClient(), path);
        return;
    }

    char backup[FS_MAX_PATH];
    snprintf(backup, sizeof(backup), TICKET_BACKUP_DIR "%s", name);

    void *file;
    size_t size = readFile(backup, &file);
    if(file == NULL)
    {
        debugPrintf("No backup to put back at %s", backup);
        return;
    }

    FSAFileHandle fh = openFile(path, "w", size);
    if(fh != 0)
    {
        addToIOQueue(file, 1, size, fh);
        addToIOQueue(NULL, 0, 0, fh);
        flushIOQueue();
    }

    MEMFreeToDefaultHeap(file);
}

TICKET_PLANT_RESULT plantTicket(const TMD *tmd)
{
    if(tmd == NULL)
        return TICKET_PLANT_INVALID;

    const uint64_t tid = tmd->tid;
    // A ticket that is there already is never touched: it may be the one the
    // console was sold with.
    if(hasTicket(tid))
        return TICKET_PLANT_EXISTS;

    RAMBUF *rambuf = allocRamBuf();
    if(rambuf == NULL)
        return TICKET_PLANT_FAILED;

    char path[FS_MAX_PATH];
    snprintf(path, sizeof(path), DOWNLOAD_URL "%016llx/cetk", tid);

    void *owned = NULL;
    const void *bytes = NULL;
    size_t size = 0;
    bool fake = false;

    // The public ticket of Nintendo when there is one, which is the real
    // thing and belongs to every console.
    if(downloadFile(path, "title.tik", NULL, FILE_TYPE_TIK | FILE_TYPE_TORAM, false, NULL, rambuf) == 0 && isPublicTicket(rambuf->buf, rambuf->size, tid))
    {
        bytes = rambuf->buf;
        size = ticketRecordSize(rambuf->buf, rambuf->size);
    }
    else
    {
        // Nothing on the servers, so the one NUSspli makes for its own
        // installs. It is written out and read back instead of built twice.
        fake = true;
        if(!generateTik(TICKET_TMP, tmd))
        {
            freeRamBuf(rambuf);
            return TICKET_PLANT_NO_TICKET;
        }

        flushIOQueue();
        size_t fileSize = readFile(TICKET_TMP, &owned);
        FSARemove(getFSAClient(), TICKET_TMP);
        size = ticketRecordSize(owned, fileSize);
        bytes = owned;
    }

    if(size == 0)
    {
        freeRamBuf(rambuf);
        if(owned != NULL)
            MEMFreeToDefaultHeap(owned);

        return TICKET_PLANT_NO_TICKET;
    }

    ticketPath(tid, path, sizeof(path));
    const char *name = path + sizeof(TICKET_BUCKET) - 1;
    char backupName[sizeof("0000/00000000.tik")];
    // "0001/00000002.tik" becomes "0001-00000002.tik", one folder of copies.
    snprintf(backupName, sizeof(backupName), "%s", name);
    char *slash = strchr(backupName, '/');
    if(slash != NULL)
        *slash = '-';

    debugPrintf("Putting a%s ticket for %016llX into %s", fake ? " fake" : " public", tid, path);

    const bool existed = fileExists(path);
    if(!backupBucket(path, backupName))
    {
        freeRamBuf(rambuf);
        if(owned != NULL)
            MEMFreeToDefaultHeap(owned);

        return TICKET_PLANT_FAILED;
    }

    // Only ever appended: the tickets already in the file keep their bytes,
    // and taking this one back is cutting the file at the length it had.
    FSAFileHandle fh = openFile(path, "a", 0);
    if(fh == 0)
    {
        freeRamBuf(rambuf);
        if(owned != NULL)
            MEMFreeToDefaultHeap(owned);

        return TICKET_PLANT_FAILED;
    }

    addToIOQueue(bytes, 1, size, fh);
    addToIOQueue(NULL, 0, 0, fh);
    flushIOQueue();
    freeRamBuf(rambuf);
    if(owned != NULL)
        MEMFreeToDefaultHeap(owned);

    // The console has to be able to find it, else the file goes back to what
    // it was.
    if(checkForQueueErrors() || !hasTicket(tid))
    {
        debugPrintf("The ticket did not take, putting the file back");
        restoreBucket(path, backupName, existed);
        return TICKET_PLANT_FAILED;
    }

    return fake ? TICKET_PLANT_OK_FAKE : TICKET_PLANT_OK;
}

void deleteTicket(uint64_t tid)
{
    LIST *ticketList = createList();
    if(ticketList == NULL)
        return;

    char path[FS_MAX_PATH];
    OSBlockMove(path, TICKET_BUCKET, sizeof(TICKET_BUCKET), false);

    char *inSentence = path + (sizeof(TICKET_BUCKET) - 1);
    FSADirectoryHandle dir;
    OSTime t = OSGetTime();
    FSError ret = FSAOpenDir(getFSAClient(), path, &dir);
    if(ret != FS_ERROR_OK)
    {
        debugPrintf("Error opening %s: %s", path, translateFSErr(ret));
        goto exit;
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
                // FSAReadDir never returns FS_STAT_FILE (0x01000000): the IOSU leaves
                // that bit clear for plain files (libiosuhax even has to OR it into its
                // own FSA results by hand), so the directory flag is tested negatively.
                if((entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 12)
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
                else if(fileSize == 0 && getFilesize(path) == 0) // readFile() also returns 0 on errors, only remove real zero byte files
                {
                    debugPrintf("Removing %s", path);
                    FSARemove(getFSAClient(), path);
                }
            }

            FSACloseDir(getFSAClient(), dir2);
        }
        else
            debugPrintf("Error opening %s: %s", path, translateFSErr(ret));
    }

    FSACloseDir(getFSAClient(), dir);
exit:
    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));
    destroyList(ticketList, true);
}

bool hasTicket(uint64_t tid)
{
    char path[FS_MAX_PATH];
    OSBlockMove(path, TICKET_BUCKET, sizeof(TICKET_BUCKET), false);
    char *inSentence = path + (sizeof(TICKET_BUCKET) - 1);

    FSADirectoryHandle dir;
    if(FSAOpenDir(getFSAClient(), path, &dir) != FS_ERROR_OK)
        return false;

    // The same walk as deleteTicket(): folders of four characters, files of
    // twelve, every file a row of tickets.
    bool found = false;
    FSADirectoryEntry entry;
    FSADirectoryHandle dir2;
    while(!found && FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
    {
        if(!(entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 4)
            continue;

        strcpy(inSentence, entry.name);
        if(FSAOpenDir(getFSAClient(), path, &dir2) != FS_ERROR_OK)
            continue;

        strcat(inSentence, "/");
        char *fileName = inSentence + strlen(inSentence);
        while(!found && FSAReadDir(getFSAClient(), dir2, &entry) == FS_ERROR_OK)
        {
            if((entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 12)
                continue;

            strcpy(fileName, entry.name);
            void *file;
            size_t fileSize = readFile(path, &file);
            if(file == NULL)
                continue;

            uint8_t *fileEnd = ((uint8_t *)file) + fileSize;
            TICKET *ticket = (TICKET *)file;
            while(((uint8_t *)ticket) + sizeof(TICKET) <= fileEnd)
            {
                if(ticket->tid == tid)
                {
                    // Which bucket file holds the ticket of a title is the
                    // system's own business: the names of the folders and
                    // files follow no title ID, and one file carries the
                    // tickets of several titles. A debug build says where
                    // the ticket of a title turned up, the way
                    // deleteTicket() does, so the choice of the system can
                    // be looked at instead of guessed at.
                    debugPrintf("Ticket of %016llX found at %s+0x%X", tid, path, (unsigned int)(((uint8_t *)ticket) - ((uint8_t *)file)));
                    found = true;
                    break;
                }

                uint8_t *ptr = ((uint8_t *)ticket) + sizeof(TICKET);
                if(ticket->total_hdr_size > 0x14)
                    ptr += ticket->total_hdr_size - 0x14;

                ticket = (TICKET *)ptr;
            }

            MEMFreeToDefaultHeap(file);
        }

        FSACloseDir(getFSAClient(), dir2);
    }

    FSACloseDir(getFSAClient(), dir);
    return found;
}

bool hasMagicHeader(const TICKET *ticket)
{
    for(size_t i = 0; i < sizeof(magic_header); ++i)
        if(ticket->header.magic_header[i] != magic_header[i])
            return false;

    return ticket->header.meta_version == 0x01;
}
