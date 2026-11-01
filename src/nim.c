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

#include <wut-fixups.h>

#include <stdint.h>

#include <nim.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

// The C++ exports of nn_nim.rpl. WUT links them, but ships no header: the
// names carry the mangling, and a config returned by value travels through
// a hidden pointer in r3, which is what a struct return does in C as well.
extern NNResult NIMInitialize(void) __asm__("Initialize__Q2_2nn3nimFv");
extern NNResult NIMFinalize(void) __asm__("Finalize__Q2_2nn3nimFv");
extern NNResult NIMRegisterTitlePackageTask(const NimTitlePackageTaskConfig *config, const uint16_t *contents, uint32_t numContents) __asm__("RegisterTitlePackageTask__Q2_2nn3nimFRCQ3_2nn3nim22TitlePackageTaskConfigPCUsUi");
// nn::nim::ResultError turns a result into the error code the system shows
// its users. The compiler of the system passes the nn::Result by pointer,
// which is how Cemu receives it, and the object holds two words there.
extern void NIMResultErrorConstruct(void *resultError, const NNResult *result, int32_t unknown) __asm__("Construct__Q3_2nn3nim11ResultErrorFQ2_2nn6Resulti");
extern int32_t NIMResultErrorGetErrorCode(const void *resultError) __asm__("GetErrorCode__Q3_2nn3nim11ResultErrorCFv");
// The packages the Download Management holds, as Cemu emulates them: the
// number, their title IDs and a 0x50 byte info block per title ID.
extern uint32_t NIMGetNumTitlePackages(void) __asm__("GetNumTitlePackages__Q2_2nn3nimFv");
extern NNResult NIMListTitlePackagesStatically(uint64_t *titleIds, uint32_t max) __asm__("ListTitlePackagesStatically__Q2_2nn3nimFPULUi");
extern NNResult NIMGetTitlePackageInfos(void *infos, const uint64_t *titleIds, uint32_t count) __asm__("GetTitlePackageInfos__Q2_2nn3nimFPQ3_2nn3nim16TitlePackageInfoPCULUi");
extern NNResult NIMUnregisterTitlePackageTask(uint64_t titleId) __asm__("UnregisterTitlePackageTask__Q2_2nn3nimFUL");

// Every call is logged in front of itself as well: the last line that
// reaches the UDP log names a call that crashes.
NNResult nimInitialize()
{
    debugPrintf("NIM: Initialize()");
    NNResult res = NIMInitialize();
    debugPrintf("NIM: Initialize() returned %#010x", res.value);
    return res;
}

// The eShop fills the config with
// nn::nim::util::MakeTitlePackageTaskConfigAutoUsingBgInstallPolicy(), but
// that call took a 5.5.5E console down in the step by step test, with a
// title type of 0 and a region of 0. Cemu only stubs it, so whatever it
// checks or reads on a console is unknown: the config is filled here the
// way Cemu fills it instead, which is what the eShop runs with in Cemu.
void nimMakeConfig(NimTitlePackageTaskConfig *config, uint64_t tid, uint16_t version, bool toUSB)
{
    OSBlockSet(config, 0, sizeof(NimTitlePackageTaskConfig));
    config->titleId = tid;
    // Cemu names this word region or language, but it is the version of the
    // title: with 0 the system fetches tmd.0, which most titles do not have,
    // and that 404 is 105-0955.
    config->titleVersion = version;
    if(isUpdate(tid))
        config->titleType = NIM_TITLE_TYPE_UPDATE;
    else if(isDLC(tid))
        config->titleType = NIM_TITLE_TYPE_ADDITIONAL_CONTENT;
    else
        config->titleType = NIM_TITLE_TYPE_APPLICATION;
    config->applicationBoxDevice1 = toUSB ? NIM_DEVICE_USB : NIM_DEVICE_INTERNAL;
    config->applicationBoxDevice2 = config->applicationBoxDevice1;
    config->unknown15 = 1;
    // Installing at the next start of the title would leave a title that is
    // not installed yet waiting forever: it has no icon to start it with.
    config->postDownloadAction = NIM_POST_DOWNLOAD_INSTALL;

    debugPrintf("NIM config for %016llX v%u: type %u, devices %u/%u, action %u", tid, version, config->titleType, config->applicationBoxDevice1, config->applicationBoxDevice2, config->postDownloadAction);
}

// The contents are left to the system: the content list of the TMD was
// refused as a usage error (0xC0512E80) on a 5.5.5E console, with and
// without the right title type, while no list got past that check.
NNResult nimRegister(const NimTitlePackageTaskConfig *config, uint64_t tid)
{
    (void)tid; // Only read by the debug log
    debugPrintf("NIM: RegisterTitlePackageTask(%016llX, no contents)", tid);
    NNResult res = NIMRegisterTitlePackageTask(config, NULL, 0);
    debugPrintf("NIM: RegisterTitlePackageTask() returned %#010x", res.value);
    return res;
}

// Takes a task out of the Download Management again, with what it has
// downloaded so far: the call the system has for it, next to the one that
// would keep the download (UnregisterTitlePackageTaskWithoutCleanup). It
// takes the ID of the package, not the title ID: a 5.5.5E console refused
// the title ID with 105-0259 (TITLE_PACKAGE_NOT_FOUND) and listed the
// package of the very same title as 0x1F.
NNResult nimUnregister(uint64_t packageId)
{
    debugPrintf("NIM: UnregisterTitlePackageTask(%016llX)", packageId);
    NNResult res = NIMUnregisterTitlePackageTask(packageId);
    debugPrintf("NIM: UnregisterTitlePackageTask() returned %#010x", res.value);
    return res;
}

bool nimFindPackage(uint64_t tid, uint64_t *packageId, uint8_t info[NIM_PACKAGE_INFO_SIZE])
{
    static uint64_t ids[NIM_MAX_PACKAGES] __attribute__((__aligned__(0x40)));
    static uint8_t infos[NIM_MAX_PACKAGES][NIM_PACKAGE_INFO_SIZE] __attribute__((__aligned__(0x40)));
    uint32_t num = nimListPackages(ids, infos, NIM_MAX_PACKAGES);

    // The info block starts with the config the package was registered
    // with, so with its title ID: the bytes after it on that console were
    // exactly the config NUSspli had handed over.
    for(uint32_t i = 0; i < num; ++i)
    {
        uint64_t packageTid;
        OSBlockMove(&packageTid, infos[i], sizeof(packageTid), false);
        if(packageTid == tid)
        {
            *packageId = ids[i];
            if(info != NULL)
                OSBlockMove(info, infos[i], NIM_PACKAGE_INFO_SIZE, false);
            debugPrintf("NIM: package of %016llX is %016llX", tid, ids[i]);
            return true;
        }
    }

    return false;
}

uint32_t nimListPackages(uint64_t *tids, uint8_t (*infos)[NIM_PACKAGE_INFO_SIZE], uint32_t max)
{
    debugPrintf("NIM: GetNumTitlePackages()");
    uint32_t num = NIMGetNumTitlePackages();
    debugPrintf("NIM: %u packages", num);
    if(num > max)
        num = max;

    if(num == 0)
        return 0;

    OSBlockSet(tids, 0, sizeof(uint64_t) * max);
    OSBlockSet(infos, 0, NIM_PACKAGE_INFO_SIZE * max);
    NNResult res = NIMListTitlePackagesStatically(tids, num);
    debugPrintf("NIM: ListTitlePackagesStatically() returned %#010x", res.value);
    res = NIMGetTitlePackageInfos(infos, tids, num);
    debugPrintf("NIM: GetTitlePackageInfos() returned %#010x", res.value);
    (void)res; // Only read by the debug log
    return num;
}

// Cemu leaves the end of the info block unnamed: two words at 0x40 and
// 0x44, the layout of nn::nim::ResultError as it emulates that object,
// and a state at 0x48 that takes the values 0 to 7. The error code is read
// from the two words as the error object they look like.
int32_t nimPackageErrorCode(const uint8_t info[NIM_PACKAGE_INFO_SIZE])
{
    uint32_t resultError[16] __attribute__((__aligned__(0x40)));
    OSBlockSet(resultError, 0, sizeof(resultError));
    OSBlockMove(resultError, info + 0x40, 8, false);
    int32_t code = NIMResultErrorGetErrorCode(resultError);
    debugPrintf("NIM: package error code %d", code);
    return code;
}

int32_t nimErrorCode(NNResult res)
{
    // Room for more than the two words Cemu knows of, in case the object
    // of a console is larger.
    uint32_t resultError[16] __attribute__((__aligned__(0x40)));
    OSBlockSet(resultError, 0, sizeof(resultError));
    debugPrintf("NIM: ResultError(%#010x)", res.value);
    NIMResultErrorConstruct(resultError, &res, 0);
    int32_t code = NIMResultErrorGetErrorCode(resultError);
    debugPrintf("NIM: error code %d", code);
    return code;
}

const char *nimErrorReason(int32_t code)
{
    if(code / 10000 != 105)
        return NULL;

    switch(code % 10000)
    {
        case 583: // TITLE_PACKAGE_ALREADY_REGISTERED
            return "It is already in the Download Management.\nClear it there first.";
        case 601: // NEW_TITLE_ALREADY_EXISTS
            return "It is already installed.";
        case 955: // BOSS_HTTP_ERROR_404_NOT_FOUND
            return "A file of this title is no longer on the servers of Nintendo.";
        case 1017: // IDBE_FAILED_TO_DOWNLOAD
            // The icon is fetched before the title is taken, and Nintendo
            // still answers for it. A console that does not get one is
            // usually asking somewhere else: Inkay patches the icon server
            // out of nim-boss, and the one it writes there has no icons, so
            // every handoff fails while that plugin is loaded. A Pretendo
            // account needs Inkay though: without it the Wii U Menu of a
            // 5.5.5E console came up without icons, while a user with a
            // Nintendo Network ID was fine, so the advice names that user.
            return "The icon of this title could not be downloaded.\nA Pretendo install (Inkay) redirects the icon server of Nintendo,\nso turn that plugin off and try again, with a user that has a\nNintendo Network ID: without Inkay a Pretendo account left the Wii U\nMenu without icons.";
    }

    return NULL;
}

void nimFinalize()
{
    debugPrintf("NIM: Finalize()");
    NIMFinalize();
}
