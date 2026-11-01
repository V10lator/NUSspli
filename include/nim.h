/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2026 V10lator <v10lator@myway.de>                         *
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

#include <tmd.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <nn/result.h>
#pragma GCC diagnostic pop

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * The Wii U Download Management: the system lists and runs the download
     * tasks of nn_nim, the title installer the eShop hands its downloads to.
     * The calls below register such a task for a title, so the system
     * downloads and installs it in the background and shows it in its
     * Download Management, NUSspli does not download anything itself.
     *
     * nn_nim is not documented. The exports come from WUT, the layout of
     * the task config from Cemu, and what the system expects in it was
     * found out on a 5.5.5E console: see nimMakeConfig() in nim.c.
     *
     * The system installs with the ticket it has, so a title needs one on
     * the console already (bought, or left behind by an earlier install).
     * nn::nim::DownloadPatchTitleTicket added none for an update, which is
     * why NUSspli downloads an update without one itself. The system
     * also fetches data of its own from the servers of Nintendo (the icon
     * of the title, for example), so a title they no longer have all of
     * fails even with a ticket, while NUSspli itself can still install it.
     */

    // The type of a title as the task config wants it, a value of
    // nn::Cafe::TitleType. It is not the category byte of the title ID:
    // 0x0C and 0x0E leave the Download Management blank, and 0x02 installs a
    // demo as additional content.
    typedef enum
    {
        NIM_TITLE_TYPE_APPLICATION = 0, // A game or a demo
        NIM_TITLE_TYPE_UPDATE = 1,
        NIM_TITLE_TYPE_ADDITIONAL_CONTENT = 2, // DLC
    } NIM_TITLE_TYPE;

    // Where the system installs the title.
    typedef enum
    {
        NIM_DEVICE_INTERNAL = 1,
        NIM_DEVICE_USB = 2,
    } NIM_DEVICE;

    // What the system does once the download is complete.
    typedef enum
    {
        NIM_POST_DOWNLOAD_INSTALL = 1, // Install right away, as the eShop does
        NIM_POST_DOWNLOAD_INSTALL_ON_START = 2, // Install at the next start of the title
    } NIM_POST_DOWNLOAD;

    // nn::nim::TitlePackageTaskConfig, as Cemu fills it for the eShop.
    typedef struct WUT_PACKED
    {
        uint64_t titleId;
        uint32_t titleVersion; // Cemu calls it region or language, see nimMakeConfig()
        uint8_t titleType; // NIM_TITLE_TYPE
        uint8_t applicationBoxDevice1; // NIM_DEVICE
        WUT_UNKNOWN_BYTES(1);
        uint8_t applicationBoxDevice2; // NIM_DEVICE, the same as the first
        WUT_UNKNOWN_BYTES(5);
        uint8_t unknown15; // Cemu sets it to 1, its meaning is unknown
        uint8_t postDownloadAction; // NIM_POST_DOWNLOAD
        WUT_UNKNOWN_BYTES(1);
    } NimTitlePackageTaskConfig;
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x00, titleId);
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x08, titleVersion);
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x0C, titleType);
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x0F, applicationBoxDevice2);
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x15, unknown15);
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x16, postDownloadAction);
    WUT_CHECK_SIZE(NimTitlePackageTaskConfig, 0x18);

    // The steps of the handoff, in this order. Every one that returns an
    // NNResult stops the handoff on a failure, and nimFinalize() closes
    // what nimInitialize() opened.
    NNResult nimInitialize();
    // toUSB installs to the USB drive instead of the internal memory.
    void nimMakeConfig(NimTitlePackageTaskConfig *config, uint64_t tid, uint16_t version, bool toUSB);
    // What the Download Management holds: up to max title IDs, each with
    // the info block of its package (0x50 bytes in Cemu), and how many.
#define NIM_PACKAGE_INFO_SIZE 0x50
    uint32_t nimListPackages(uint64_t *tids, uint8_t (*infos)[NIM_PACKAGE_INFO_SIZE], uint32_t max);
    // The ID of the package the Download Management holds for a title, see
    // nimUnregister() in nim.c, and its info block when info is not NULL.
    // False when there is none.
#define NIM_MAX_PACKAGES 16
    bool nimFindPackage(uint64_t tid, uint64_t *packageId, uint8_t info[NIM_PACKAGE_INFO_SIZE]);
    // The error code the info block of a package carries, see nim.c.
    int32_t nimPackageErrorCode(const uint8_t info[NIM_PACKAGE_INFO_SIZE]);
    // Removes a package from the Download Management, with what it has
    // downloaded so far. Takes the ID of the package, see nimFindPackage().
    NNResult nimUnregister(uint64_t packageId);
    // The system picks the contents of the title itself.
    NNResult nimRegister(const NimTitlePackageTaskConfig *config, uint64_t tid);

    // The error code the system shows its users for a result (105-0583 is
    // 1050583), between nimInitialize() and nimFinalize().
    int32_t nimErrorCode(NNResult res);
    /*
     * What a code of the Download Management means, in the English the
     * caller hands to localise(), or NULL for a code with nothing written
     * down about it. The names behind the codes are the ones WiiUBrew
     * lists for them.
     */
    const char *nimErrorReason(int32_t code);
    void nimFinalize();

#ifdef __cplusplus
}
#endif
