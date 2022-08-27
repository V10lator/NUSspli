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

#include <wut-fixups.h>

#include <ticket.h>

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

/* This generates a ticket and writes it to a file.
 * The ticket algorithm is ported from FunkiiU combined
 * with the randomness of NUSPackager to create unique
 * NUSspli tickets.
 */
bool generateTik(const char *path, const TitleEntry *titleEntry)
{
    FSFileHandle *tik = openFile(path, "w", 0);
    if(tik == NULL)
    {
        char *err = getStaticScreenBuffer();
        sprintf(err, "%s\n%s", gettext("Could not open path"), prettyDir(path));
        drawErrorFrame(err, ANY_RETURN);

        while(AppRunning())
        {
            showFrame();

            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
                drawErrorFrame(err, ANY_RETURN);

            if(vpad.trigger)
                break;
        }
        return false;
    }

    char encKey[33];
    if(!generateKey(titleEntry, encKey))
        return false;

    char tid[17];
    hex(titleEntry->tid, 16, tid);

    debugPrintf("Generating fake ticket at %s", path);

    // NUSspli adds its own header.
    writeHeader(tik, FILE_TYPE_TIK);

    // SSH certificate
    writeCustomBytes(tik, "0x526F6F742D434130303030303030332D58533030303030303063"); // "Root-CA00000003-XS0000000c"writeVoidBytes(tik, 0x26);
    writeVoidBytes(tik, 0x26);
    writeRandomBytes(tik, 0x3B);

    // The title data
    writeCustomBytes(tik, "0x00010000");
    writeCustomBytes(tik, encKey);
    writeVoidBytes(tik, 0xD); // 0x00000000, so one of the magic things again
    writeCustomBytes(tik, tid);

    writeVoidBytes(tik, 0x3D);
    writeCustomBytes(tik, "01"); // Not sure what shis is for
    writeVoidBytes(tik, 0x82);

    // And some footer. Also not sure what it means
    // Let's write it out in parts so we might be able to find patterns
    writeCustomBytes(tik, "0x00010014"); // Magic thing A
    writeCustomBytes(tik, "0x000000ac");
    writeCustomBytes(tik, "0x00000014"); // Might be connected to magic thing A
    writeCustomBytes(tik, "0x00010014"); // Magic thing A
    writeCustomBytes(tik, "0x0000000000000028");
    writeCustomBytes(tik, "0x00000001"); // Magic thing B ?
    writeCustomBytes(tik, "0x0000008400000084"); // Pattern
    writeCustomBytes(tik, "0x0003000000000000");
    writeCustomBytes(tik, "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    writeVoidBytes(tik, 0x60);

    addToIOQueue(NULL, 0, 0, tik);
    return true;
}

bool generateCert(const char *path)
{
    FSFileHandle *cert = openFile(path, "w", 0);
    if(cert == NULL)
    {
        char *err = getStaticScreenBuffer();
        sprintf(err, "%s\n%s", gettext("Could not open path"), prettyDir(path));
        drawErrorFrame(err, ANY_RETURN);

        while(AppRunning())
        {
            showFrame();

            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
                drawErrorFrame(err, ANY_RETURN);

            if(vpad.trigger)
                break;
        }
        return false;
    }

    // NUSspli adds its own header.
    writeHeader(cert, FILE_TYPE_CERT);

    // Some SSH certificate
    writeCustomBytes(cert, "0x526F6F742D43413030303030303033"); // "Root-CA00000003"
    writeVoidBytes(cert, 0x34);
    writeCustomBytes(cert, "0x0143503030303030303062"); // "?CP0000000b"
    writeVoidBytes(cert, 0x36);
    writeRandomBytes(cert, 0x104);
    writeCustomBytes(cert, "0x00010001");
    writeVoidBytes(cert, 0x34);
    writeCustomBytes(cert, "0x00010003");
    writeRandomBytes(cert, 0x200);
    writeVoidBytes(cert, 0x3C);

    // Next certificate
    writeCustomBytes(cert, "0x526F6F74"); // "Root"
    writeVoidBytes(cert, 0x3F);
    writeCustomBytes(cert, "0x0143413030303030303033"); // "?CA00000003"
    writeVoidBytes(cert, 0x36);
    writeRandomBytes(cert, 0x104);
    writeCustomBytes(cert, "0x00010001");
    writeVoidBytes(cert, 0x34);
    writeCustomBytes(cert, "0x00010004");
    writeRandomBytes(cert, 0x100);
    writeVoidBytes(cert, 0x3C);

    // Last certificate
    writeCustomBytes(cert, "0x526F6F742D43413030303030303033"); // "Root-CA00000003"
    writeVoidBytes(cert, 0x34);
    writeCustomBytes(cert, "0x0158533030303030303063"); // "?XS0000000c"
    writeVoidBytes(cert, 0x36);
    writeRandomBytes(cert, 0x104);
    writeCustomBytes(cert, "0x00010001");
    writeVoidBytes(cert, 0x34);

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

void generateFakeTicket()
{
    char *dir = fileBrowserMenu();
    if(dir == NULL)
        return;

    TMD *tmd = getTmd(dir);
    if(tmd == NULL)
    {
        drawErrorFrame(gettext("Invalid title.tmd file!"), ANY_RETURN);

        while(AppRunning())
        {
            showFrame();

            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
                drawErrorFrame(gettext("Invalid title.tmd file!"), ANY_RETURN);

            if(vpad.trigger)
                break;
        }
        return;
    }

    drawTicketFrame(tmd->tid);

    while(AppRunning())
    {
        showFrame();

        if(app == APP_STATE_BACKGROUND)
            continue;
        if(app == APP_STATE_RETURNING)
            drawTicketFrame(tmd->tid);

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
            if(!generateTik(dir, entry))
                break;

            colorStartNewFrame(SCREEN_COLOR_D_GREEN);
            textToFrame(0, 0, gettext("Fake ticket generated on:"));
            textToFrame(1, 0, prettyDir(dir));

            textToFrame(3, 0, gettext("Press any key to return"));
            drawFrame();

            while(AppRunning())
            {
                showFrame();

                if(app == APP_STATE_BACKGROUND)
                    continue;
                // TODO: APP_STATE_RETURNING

                if(vpad.trigger)
                    break;
            }
            break;
        }
        if(vpad.trigger & VPAD_BUTTON_B)
            break;
    }

    MEMFreeToDefaultHeap(tmd);
}
