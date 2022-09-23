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

#include <dirent.h>
#include <math.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <config.h>
#include <crypto.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <ioQueue.h>
#include <localisation.h>
#include <menu/utils.h>
#include <osdefs.h>
#include <renderer.h>
#include <romfs.h>
#include <state.h>
#include <staticMem.h>
#include <thread.h>
#include <ticket.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <curl/curl.h>
#include <nn/ac/ac_c.h>
#include <nn/result.h>
#include <nsysnet/_socket.h>

#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#define USERAGENT        "NUSspli/" NUSSPLI_VERSION // TODO: Spoof eShop here?
#define DLBGT_STACK_SIZE 0x1000
#define DLT_STACK_SIZE   0x4000
#define SOCKLIB_BUFSIZE  (IO_BUFSIZE * 2) // double buffering

static volatile char *ramBuf = NULL;
static volatile size_t ramBufSize = 0;

static CURL *curl;
static char curlError[CURL_ERROR_SIZE];
static bool curlReuseConnection = true;
static OSThread *dlbgThread = NULL;

static int cancelOverlayId = -1;

typedef struct
{
    bool running;
    CURLcode error;
    spinlock lock;
    OSTick ts;
    double dltotal;
    double dlnow;
} curlProgressData;

#define closeCancelOverlay()                 \
    {                                        \
        removeErrorOverlay(cancelOverlayId); \
        cancelOverlayId = -1;                \
    }

static int progressCallback(void *rawData, double dltotal, double dlnow, double ultotal, double ulnow)
{
    curlProgressData *data = (curlProgressData *)rawData;
    if(!AppRunning(false))
        data->error = CURLE_ABORTED_BY_CALLBACK;

    if(data->error != CURLE_OK)
        return 1;

    if(dltotal > 0.1D && !isinf(dltotal) && !isnan(dltotal))
    {
        if(!spinTryLock(data->lock))
            return 0;

        data->ts = OSGetTick();
        data->dltotal = dltotal;
        data->dlnow = dlnow;
        spinReleaseLock(data->lock);
    }

    return 0;
}

static int dlbgThreadMain(int argc, const char **argv)
{
    debugPrintf("Socket optimizer running!");

    void *buf = MEMAllocFromDefaultHeapEx(SOCKLIB_BUFSIZE, 64);
    if(buf == NULL)
    {
        debugPrintf("Socket optimizer: OUT OF MEMORY!");
        return 1;
    }

    int ret = somemopt(0x01, buf, SOCKLIB_BUFSIZE, 0) == -1 ? RPLWRAP(socketlasterr)() : 50; // We need the rplwrapped socketlasterr() here as WUTs simply retuns errno but errno hasn't been setted.
    __init_wut_socket();
    debugInit();
    if(ret == 50)
        ret = 0;
    else
        debugPrintf("somemopt failed!");

    MEMFreeToDefaultHeap(buf);
    debugPrintf("Socket optimizer finished!");
    return ret;
}

static int initSocket(void *ptr, curl_socket_t socket, curlsocktype type)
{
    int o = 1;

    // Activate WinScale
    int r = setsockopt(socket, SOL_SOCKET, SO_WINSCALE, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings WinScale: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    // Activate TCP SAck
    r = setsockopt(socket, SOL_SOCKET, SO_TCPSACK, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings TCP SAck: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    // Activate TCP nodelay - libCURL default
    r = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings TCP nodelay: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    // Disable slowstart. Should be more important fo a server but doesn't hurt a client, too
    r = setsockopt(socket, SOL_SOCKET, 0x4000, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings Noslowstart: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    // Activate userspace buffer (fom our socket optimizer)
    r = setsockopt(socket, SOL_SOCKET, 0x10000, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings UB: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    o = 0;
    // Disable TCP keepalive - libCURL default
    r = setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings TCP nodelay: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    o = IO_BUFSIZE;
    // Set send buffersize
    /*	r = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &o, sizeof(o));
            if(r != 0)
            {
                    debugPrintf("initSocket: Error settings SBS: %d", r);
                    return CURL_SOCKOPT_ERROR;
            }
    */
    // Set receive buffersize
    r = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings RBS: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    return CURL_SOCKOPT_OK;
}

static CURLcode ssl_ctx_init(CURL *curl, void *sslctx, void *parm)
{
    mbedtls_ssl_conf_rng((mbedtls_ssl_config *)sslctx, NUSrng, NULL);
    return CURLE_OK;
}

#define killDlbgThread()                  \
    {                                     \
        if(dlbgThread != NULL)            \
        {                                 \
            shutdownDebug();              \
            __fini_wut_socket();          \
            stopThread(dlbgThread, NULL); \
            dlbgThread = NULL;            \
        }                                 \
    }

#define initNetwork()                                                                                                                                         \
    {                                                                                                                                                         \
        curlReuseConnection = false;                                                                                                                          \
        dlbgThread = startThread("NUSspli socket optimizer", THREAD_PRIORITY_LOW, DLBGT_STACK_SIZE, dlbgThreadMain, 0, NULL, OS_THREAD_ATTRIB_AFFINITY_CPU0); \
    }

// We call AC functions here without calling ACInitialize() / ACFinalize() as WUT should call these for us.
#define resetNetwork()                                      \
    {                                                       \
        killDlbgThread();                                   \
                                                            \
        ACConfigId networkCfg;                              \
        if(NNResult_IsSuccess(ACGetStartupId(&networkCfg))) \
            ACConnectWithConfigId(networkCfg);              \
                                                            \
        initNetwork();                                      \
    }

bool initDownloader()
{
    initNetwork();
    if(dlbgThread == NULL)
        return false;

    char *fn = getStaticPathBuffer(2);
    strcpy(fn, ROMFS_PATH "ca-certificates/");
    struct curl_blob blob = { .data = NULL, .len = 0, .flags = CURL_BLOB_COPY };

#ifndef NUSSPLI_HBL
    FSDirectoryHandle dir;
    if(FSOpenDir(__wut_devoptab_fs_client, getCmdBlk(), fn, &dir, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
#else
    DIR *dir = opendir(fn);
    if(dir != NULL)
#endif
    {
        char *ptr = fn + strlen(fn);
        void *buf;
        size_t bufsize;
        size_t oldcertsize = 0;
        void *tmp;
#ifndef NUSSPLI_HBL
        FSDirectoryEntry entry;
        while(FSReadDir(__wut_devoptab_fs_client, getCmdBlk(), dir, &entry, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
        {
            if(entry.name[0] == '.')
                continue;

            strcpy(ptr, entry.name);
#else
        for(struct dirent *entry = readdir(dir); entry != NULL; entry = readdir(dir))
        {
            if(entry->d_name[0] == '.')
                continue;

            strcpy(ptr, entry->d_name);
#endif
            bufsize = readFile(fn, &buf);
            if(buf == NULL)
                continue;

            oldcertsize = blob.len;
            blob.len += bufsize;
            if(blob.data == NULL)
            {
                blob.data = MEMAllocFromDefaultHeap(blob.len);
                if(blob.data == NULL)
                {
                    blob.len = 0;
                    continue;
                }
            }
            else
            {
                tmp = blob.data;
                blob.data = MEMAllocFromDefaultHeap(blob.len);
                if(blob.data == NULL)
                {
                    blob.data = tmp;
                    blob.len -= bufsize;
                    continue;
                }

                OSBlockMove(blob.data, tmp, oldcertsize, false);
                MEMFreeToDefaultHeap(tmp);
            }

            OSBlockMove(blob.data + oldcertsize, buf, bufsize, false);
        }

#ifndef NUSSPLI_HBL
        FSCloseDir(__wut_devoptab_fs_client, getCmdBlk(), dir, FS_ERROR_FLAG_ALL);
#else
        closedir(dir);
#endif
    }
    else
        debugPrintf("Error opening %s!", fn);

    CURLcode ret = curl_global_init(CURL_GLOBAL_DEFAULT & ~(CURL_GLOBAL_SSL));
    if(ret == CURLE_OK)
    {
        curl = curl_easy_init();
        if(curl != NULL)
        {
#ifdef NUSSPLI_DEBUG
            curlError[0] = '\0';
            ret = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
            if(ret == CURLE_OK)
            {
#endif
                ret = curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, initSocket);
                if(ret == CURLE_OK)
                {
                    ret = curl_easy_setopt(curl, CURLOPT_USERAGENT, USERAGENT);
                    if(ret == CURLE_OK)
                    {
                        ret = curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressCallback);
                        if(ret == CURLE_OK)
                        {
                            ret = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                            if(ret == CURLE_OK)
                            {
                                ret = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                                if(ret == CURLE_OK)
                                {
                                    ret = curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, ssl_ctx_init);
                                    if(ret == CURLE_OK)
                                    {
                                        ret = curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, blob);
                                        if(ret == CURLE_OK)
                                        {
                                            ret = curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
                                            if(ret == CURLE_OK)
                                            {
                                                ret = curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
                                                if(ret == CURLE_OK)
                                                {
                                                    if(blob.data != NULL)
                                                        MEMFreeToDefaultHeap(blob.data);

                                                    return true;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
#ifdef NUSSPLI_DEBUG
            }
            debugPrintf("curl_easy_setopt() failed: %s (%d)", curlError, ret);
#endif
            curl_easy_cleanup(curl);
            curl = NULL;
        }
#ifdef NUSSPLI_DEBUG
        else
            debugPrintf("curl_easy_init() failed!");
#endif
        curl_global_cleanup();
    }

    killDlbgThread();
    if(blob.data != NULL)
        MEMFreeToDefaultHeap(blob.data);

    return false;
}

void deinitDownloader()
{
    if(curl != NULL)
    {
        curl_easy_cleanup(curl);
        curl = NULL;
    }
    curl_global_cleanup();
    killDlbgThread();
}

static int dlThreadMain(int argc, const char **argv)
{
    debugPrintf("Download thread spawned!");
    int ret = curl_easy_perform(curl);
    ((curlProgressData *)argv[0])->running = false;
    return ret;
}

#define setDefaultDataValues(x)                  \
    {                                            \
        x.running = true;                        \
        x.error = CURLE_OK;                      \
        spinCreateLock((x.lock), SPINLOCK_FREE); \
        x.dlnow = x.dltotal = 0.0D;              \
    }

static const char *translateCurlError(CURLcode err, const char *curlError)
{
    const char *ret;
    switch(err)
    {
        case CURLE_COULDNT_RESOLVE_HOST:
            ret = "Couldn't resolve hostname";
            break;
        case CURLE_COULDNT_CONNECT:
            ret = "Couldn't connect to server";
            break;
        case CURLE_OPERATION_TIMEDOUT:
            ret = "Operation timed out";
            break;
        case CURLE_GOT_NOTHING:
            ret = "The server didn't return any data";
            break;
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_PARTIAL_FILE:
            ret = "I/O error";
            break;
        case CURLE_PEER_FAILED_VERIFICATION:
            ret = "Verification failed";
            break;
        case CURLE_SSL_CONNECT_ERROR:
            ret = "Handshake failed";
            break;

        case CURLE_FAILED_INIT:
        case CURLE_READ_ERROR:
        case CURLE_OUT_OF_MEMORY:
            return "Internal error";
        default:
            return curlError[0] == '\0' ? "Unknown libcurl error" : curlError;
    }

    return ret;
}

int downloadFile(const char *url, char *file, downloadData *data, FileType type, bool resume)
{
    // Results: 0 = OK | 1 = Error | 2 = No ticket aviable | 3 = Exit
    // Types: 0 = .app | 1 = .h3 | 2 = title.tmd | 3 = tilte.tik
    bool toRam = (type & FILE_TYPE_TORAM) == FILE_TYPE_TORAM;

    debugPrintf("Download URL: %s", url);
    debugPrintf("Download PATH: %s", toRam ? "<RAM>" : file);

    char *name;
    if(toRam)
        name = file;
    else
    {
        size_t haystack;
        for(haystack = strlen(file); file[haystack] != '/'; haystack--)
            ;
        name = file + haystack + 1;
    }

    char *toScreen = getToFrameBuffer();
    void *fp;
    size_t fileSize;
    if(toRam)
    {
        fp = (void *)open_memstream((char **)&ramBuf, (size_t *)&ramBufSize);
        fileSize = 0;
    }
    else
    {
        if(resume && fileExists(file))
        {
            FSFileHandle *nf;
            fileSize = getFilesize(file);
            if(fileSize != 0)
            {
                if((type & FILE_TYPE_H3) || fileSize == data->cs)
                {
                    sprintf(toScreen, "Download %s skipped!", name);
                    addToScreenLog(toScreen);
                    data->dltmp += (double)fileSize;
                    return 0;
                }
                if(fileSize > data->cs)
                    return downloadFile(url, file, data, type, false);

                nf = openFile(file, "a", 0);
            }
            else
                nf = openFile(file, "w", data == NULL ? 0 : data->cs);

            fp = (void *)nf;
        }
        else
        {
            fp = (void *)openFile(file, "w", data == NULL ? 0 : data->cs);
            fileSize = 0;
        }
    }

    if(fp == NULL)
        return 1;

    curlError[0] = '\0';
    volatile curlProgressData cdata;

    CURLcode ret = curl_easy_setopt(curl, CURLOPT_URL, url);
    if(ret == CURLE_OK)
    {
        if(curlReuseConnection)
            ret = curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0L);
        else
        {
            ret = curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
            curlReuseConnection = true;
        }
        if(ret == CURLE_OK)
        {
            ret = curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)fileSize);
            if(ret == CURLE_OK)
            {
                ret = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, toRam ? fwrite : (size_t(*)(const void *, size_t, size_t, FILE *))addToIOQueue);
                if(ret == CURLE_OK)
                {
                    ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (FILE *)fp);
                    if(ret == CURLE_OK)
                        ret = curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &cdata);
                }
            }
        }
    }

    if(ret != CURLE_OK)
    {
        if(toRam)
            fclose((FILE *)fp);
        else
            addToIOQueue(NULL, 0, 0, (FSFileHandle *)fp);

        debugPrintf("curl_easy_setopt error: %s (%d / %ud)", curlError, ret, fileSize);
        return 1;
    }

    setDefaultDataValues(cdata);

    debugPrintf("Calling curl_easy_perform()");
    OSTime t = OSGetSystemTime();

    char *argv[1] = { (char *)&cdata };
    OSThread *dlThread = startThread("NUSspli downloader", THREAD_PRIORITY_HIGH, DLT_STACK_SIZE, dlThreadMain, 1, (char *)argv, OS_THREAD_ATTRIB_AFFINITY_CPU2);
    if(dlThread == NULL)
        return 1;

    double multiplier;
    char *multiplierName;
    double downloaded = 0.0D;
    double dlnow;
    double dltotal;
    OSTick now;
    OSTick lastTransfair = OSGetTick();
    OSTick ent;
    int frames = 1;
    uint32_t fileeta;
    uint32_t totaleta;
    while(cdata.running && AppRunning(true))
    {
        if(--frames == 0)
        {
            if(cdata.dltotal > 0.1D)
            {
                if(!spinTryLock(cdata.lock))
                {
                    frames = 2;
                    continue;
                }

                if(!toRam)
                    checkForQueueErrors();

                dlnow = cdata.dlnow;
                now = cdata.ts;
                spinReleaseLock(cdata.lock);

                dltotal = cdata.dltotal + fileSize;
                dlnow += fileSize;

                frames = 60;
                startNewFrame();
                strcpy(toScreen, gettext("Downloading"));
                strcat(toScreen, " ");
                strcat(toScreen, name);
                textToFrame(0, 0, toScreen);
                barToFrame(1, 0, 29, dlnow / dltotal * 100.0D);

                if(dltotal < 1024.0D)
                {
                    multiplier = 1.0D;
                    multiplierName = "B";
                }
                else if(dltotal < 1024.0D * 1024.0D)
                {
                    multiplier = 1 << 10;
                    multiplierName = "KB";
                }
                else if(dltotal < 1024.0f * 1024.0D * 1024.0D)
                {
                    multiplier = 1 << 20;
                    multiplierName = "MB";
                }
                else
                {
                    multiplier = 1 << 30;
                    multiplierName = "GB";
                }

                sprintf(toScreen, "%.2f / %.2f %s", dlnow / multiplier, dltotal / multiplier, multiplierName);
                textToFrame(1, 30, toScreen);

                fileeta = (dltotal - dlnow);
                dltotal = (dlnow - downloaded); // sample length in bytes
                fileeta /= dltotal;

                secsToTime(fileeta, toScreen);
                textToFrame(1, ALIGNED_RIGHT, toScreen);

                ent = (OSTick)dltotal;
                addEntropy(&ent, sizeof(OSTick));
                downloaded = dlnow;
                if(dltotal > 0.0D)
                {
                    ent = now - lastTransfair;
                    addEntropy(&ent, sizeof(OSTick));
                    dlnow = OSTicksToMilliseconds(ent); // sample duration in milliseconds
                    if(dlnow != 0.0D)
                    {
                        dlnow /= 1000.0D; // sample duration in seconds
                        if(dlnow != 0.0D)
                            dltotal /= dlnow; // mbyte/s
                    }
                }

                lastTransfair = now;
                getSpeedString(dltotal, toScreen);
                textToFrame(0, ALIGNED_RIGHT, toScreen);
            }
            else
            {
                frames = 1;
                startNewFrame();
                strcpy(toScreen, gettext("Preparing"));
                strcat(toScreen, " ");
                strcat(toScreen, name);
                textToFrame(0, 0, toScreen);
            }

            if(data != NULL)
            {
                sprintf(toScreen, "(%d/%d)", data->dcontent + 1, data->contents);
                textToFrame(0, ALIGNED_CENTER, toScreen);

                if(data->dltotal < 1024.0D)
                {
                    multiplier = 1.0D;
                    multiplierName = "B";
                }
                else if(data->dltotal < 1024.0D * 1024.0f)
                {
                    multiplier = 1 << 10;
                    multiplierName = "KB";
                }
                else if(data->dltotal < 1024.0D * 1024.0D * 1024.0D)
                {
                    multiplier = 1 << 20;
                    multiplierName = "MB";
                }
                else
                {
                    multiplier = 1 << 30;
                    multiplierName = "GB";
                }
                data->dlnow = data->dltmp + downloaded;
                barToFrame(2, 0, 29, data->dlnow / data->dltotal * 100.0f);
                sprintf(toScreen, "%.2f / %.2f %s", data->dlnow / multiplier, data->dltotal / multiplier, multiplierName);
                textToFrame(2, 30, toScreen);

                if(cdata.dltotal > 0.1D)
                {
                    totaleta = (data->dltotal - data->dlnow) / dltotal;
                    if(totaleta)
                        data->eta = totaleta;
                    else
                        totaleta = data->eta;
                }
                else
                    totaleta = data->eta;

                secsToTime(totaleta, toScreen);
                textToFrame(2, ALIGNED_RIGHT, toScreen);

                writeScreenLog(3);
            }
            else
                writeScreenLog(2);

            drawFrame();
        }

        showFrame();

        if(cancelOverlayId < 0)
        {
            if(vpad.trigger & VPAD_BUTTON_B)
            {
                strcpy(toScreen, gettext("Do you really want to cancel?"));
                strcat(toScreen, "\n\n" BUTTON_A " ");
                strcat(toScreen, gettext("Yes"));
                strcat(toScreen, " || " BUTTON_B " ");
                strcat(toScreen, gettext("No"));
                cancelOverlayId = addErrorOverlay(toScreen);
            }
        }
        else
        {
            if(vpad.trigger & VPAD_BUTTON_A)
            {
                cdata.error = CURLE_ABORTED_BY_CALLBACK;
                closeCancelOverlay();
                break;
            }
            if(vpad.trigger & VPAD_BUTTON_B)
                closeCancelOverlay();
        }
    }

    stopThread(dlThread, (int *)&ret);

    t = OSGetSystemTime() - t;
    addEntropy(&t, sizeof(OSTime));
    if(data == NULL && cancelOverlayId >= 0)
        closeCancelOverlay();

    debugPrintf("curl_easy_perform() returned: %d", ret);

    if(toRam)
        fclose((FILE *)fp);
    else
        addToIOQueue(NULL, 0, 0, (FSFileHandle *)fp);

    if(!AppRunning(true))
        return 1;

    if(ret != CURLE_OK)
    {
        debugPrintf("curl_easy_perform returned an error: %s (%d/%d)\nFile: %s", curlError, ret, cdata.error, toRam ? "<RAM>" : file);

        if(ret == CURLE_ABORTED_BY_CALLBACK)
        {
            switch(cdata.error)
            {
                case CURLE_ABORTED_BY_CALLBACK:
                    return 1;
                case CURLE_OK:
                    break;
                default:
                    ret = cdata.error;
            }
        }

        const char *te = translateCurlError(ret, curlError);
        switch(ret)
        {
            case CURLE_RANGE_ERROR:
                int r = downloadFile(url, file, data, type, false);
                curlReuseConnection = false;
                return r;
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_CONNECT:
            case CURLE_OPERATION_TIMEDOUT:
            case CURLE_GOT_NOTHING:
            case CURLE_SEND_ERROR:
            case CURLE_RECV_ERROR:
            case CURLE_PARTIAL_FILE:
                sprintf(toScreen, "%s:\n\t%s\n\n%s", "Network error", te, "check the network settings and try again");
                break;
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_CONNECT_ERROR:
                sprintf(toScreen, "%s:\n\t%s!\n\n%s", "SSL error", te, "check your Wii Us date and time settings");
                break;
            default:
                sprintf(toScreen, "%s:\n\t%d %s", te, ret, curlError);
                break;
        }

        if(data != NULL && cancelOverlayId >= 0)
            closeCancelOverlay();

        int os;

        char *p;
        if(autoResumeEnabled())
        {
            os = 9 * 60; // 9 seconds with 60 FPS
            frames = os;
            strcat(toScreen, "\n\n");
            p = toScreen + strlen(toScreen);
            const char *pt = gettext("Next try in _ seconds.");
            strcpy(p, pt);
            const char *n = strchr(pt, '_');
            p += n - pt;
            strcat(toScreen, pt);
        }
        else
            drawErrorFrame(toScreen, B_RETURN | Y_RETRY);

        int s;
        while(AppRunning(true))
        {
            if(app == APP_STATE_BACKGROUND)
                continue;
            else if(app == APP_STATE_RETURNING)
                drawErrorFrame(toScreen, B_RETURN | Y_RETRY);

            if(autoResumeEnabled())
            {
                s = frames / 60;
                if(s != os)
                {
                    *p = '1' + s;
                    os = s;
                    drawErrorFrame(toScreen, B_RETURN | Y_RETRY);
                }
            }

            showFrame();

            if(vpad.trigger & VPAD_BUTTON_B)
                break;
            if(vpad.trigger & VPAD_BUTTON_Y || (autoResumeEnabled() && --frames == 0))
            {
                flushIOQueue(); // We flush here so the last file is completely on disc and closed before we retry.
                resetNetwork(); // Recover from network errors.
                return downloadFile(url, file, data, type, resume);
            }
        }
        resetNetwork();
        return 1;
    }
    debugPrintf("curl_easy_perform executed successfully");

    double dld;
    ret = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &dld);
    if(ret != CURLE_OK)
        dld = 0.0D;

    long resp;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp);
    if(resp == 206) // Resumed download OK
        resp = 200;

    debugPrintf("The download returned: %u", resp);
    if(resp != 200)
    {
        if(!toRam)
        {
            flushIOQueue();
            char *newFile = getStaticPathBuffer(2);
            strcpy(newFile, file);
            FSRemove(__wut_devoptab_fs_client, getCmdBlk(), newFile, FS_ERROR_FLAG_ALL);
        }

        if(resp == 404 && (type & FILE_TYPE_TMD) == FILE_TYPE_TMD) // Title.tmd not found
        {
            strcpy(toScreen, gettext("The download of title.tmd failed with error: 404"));
            strcat(toScreen, "\n\n");
            strcat(toScreen, gettext("The title cannot be found on the NUS, maybe the provided title ID doesn't exists or\nthe TMD was deleted"));
            drawErrorFrame(toScreen, B_RETURN | Y_RETRY);

            while(AppRunning(true))
            {
                if(app == APP_STATE_BACKGROUND)
                    continue;
                if(app == APP_STATE_RETURNING)
                    drawErrorFrame(toScreen, B_RETURN | Y_RETRY);

                showFrame();

                if(vpad.trigger & VPAD_BUTTON_B)
                    break;
                if(vpad.trigger & VPAD_BUTTON_Y)
                    return downloadFile(url, file, data, type, resume);
            }
            return 1;
        }
        else if(resp == 404 && (type & FILE_TYPE_TIK) == FILE_TYPE_TIK)
        { // Fake ticket needed
            return 2;
        }
        else
        {
            sprintf(toScreen, "%s: %ld\n%s: %s\n\n", gettext("The download returned a result different to 200 (OK)"), resp, gettext("File"), toRam ? file : prettyDir(file));
            if(resp == 400)
            {
                strcat(toScreen, gettext("Request failed. Try again"));
                strcat(toScreen, "\n\n");
            }

            drawErrorFrame(toScreen, B_RETURN | Y_RETRY);

            while(AppRunning(true))
            {
                if(app == APP_STATE_BACKGROUND)
                    continue;
                if(app == APP_STATE_RETURNING)
                    drawErrorFrame(toScreen, B_RETURN | Y_RETRY);

                showFrame();

                if(vpad.trigger & VPAD_BUTTON_B)
                    break;
                if(vpad.trigger & VPAD_BUTTON_Y)
                    return downloadFile(url, file, data, type, resume);
            }
            return 1;
        }
    }

    if(data != NULL)
    {
        if(fileSize)
            dld += (double)fileSize;

        data->dltmp += dld;
    }

    sprintf(toScreen, "Download %s finished!", name);
    addToScreenLog(toScreen);
    return 0;
}

bool downloadTitle(const TMD *tmd, size_t tmdSize, const TitleEntry *titleEntry, const char *titleVer, char *folderName, bool inst, NUSDEV dlDev, bool toUSB, bool keepFiles)
{
    char tid[17];
    hex(tmd->tid, 16, tid);
    debugPrintf("Downloading title... tID: %s, tVer: %s, name: %s, folder: %s", tid, titleVer, titleEntry->name, folderName);

    char downloadUrl[256];
    strcpy(downloadUrl, DOWNLOAD_URL);
    strcat(downloadUrl, tid);
    strcat(downloadUrl, "/");

    if(folderName[0] == '\0')
    {
        for(int i = 0; i < strlen(titleEntry->name); ++i)
            folderName[i] = isAllowedInFilename(titleEntry->name[i]) ? titleEntry->name[i] : '_';
    }
    strcpy(folderName + strlen(titleEntry->name), " [");
    strcat(folderName, tid);
    strcat(folderName, "]");

    if(strlen(titleVer) > 0)
    {
        strcat(folderName, " v");
        strcat(folderName, titleVer);
    }

    char *installDir = getStaticPathBuffer(3);
    strcpy(installDir, dlDev == NUSDEV_USB01 ? INSTALL_DIR_USB1 : (dlDev == NUSDEV_USB02 ? INSTALL_DIR_USB2 : (dlDev == NUSDEV_SD ? INSTALL_DIR_SD : INSTALL_DIR_MLC)));
    if(!dirExists(installDir))
    {
        debugPrintf("Creating directory \"%s\"", installDir);
        FSStatus err = createDirectory(installDir);
        if(err == FS_STATUS_OK)
            addToScreenLog("Install directory successfully created");
        else
        {
            char *toScreen = getToFrameBuffer();
            strcpy(toScreen, translateFSErr(err));
            drawErrorFrame(toScreen, ANY_RETURN);

            while(AppRunning(true))
            {
                if(app == APP_STATE_BACKGROUND)
                    continue;
                if(app == APP_STATE_RETURNING)
                    drawErrorFrame(toScreen, ANY_RETURN);

                showFrame();

                if(vpad.trigger)
                    break;
            }
            return false;
        }
    }

    strcat(installDir, folderName);
    strcat(installDir, "/");

    addToScreenLog("Started the download of \"%s\"", titleEntry->name);
    addToScreenLog("The content will be saved on \"%s\"", prettyDir(installDir));

    if(!dirExists(installDir))
    {
        debugPrintf("Creating directory \"%s\"", installDir);
        FSStatus err = createDirectory(installDir);
        if(err == FS_STATUS_OK)
            addToScreenLog("Download directory successfully created");
        else
        {
            char *toScreen = getToFrameBuffer();
            strcpy(toScreen, translateFSErr(err));
            drawErrorFrame(toScreen, ANY_RETURN);

            while(AppRunning(true))
            {
                if(app == APP_STATE_BACKGROUND)
                    continue;
                if(app == APP_STATE_RETURNING)
                    drawErrorFrame(toScreen, ANY_RETURN);

                showFrame();

                if(vpad.trigger)
                    break;
            }
            return false;
        }
    }
    else
        addToScreenLog("WARNING: The download directory already exists");

    char *idp = installDir + strlen(installDir);
    strcpy(idp, "title.tmd");

    FSFileHandle *fp = openFile(installDir, "w", tmdSize);
    if(fp == NULL)
    {
        drawErrorFrame("Can't save title.tmd file!", ANY_RETURN);

        while(AppRunning(true))
        {
            if(app == APP_STATE_BACKGROUND)
                continue;
            if(app == APP_STATE_RETURNING)
                drawErrorFrame("Can't save title.tmd file!", ANY_RETURN);

            showFrame();

            if(vpad.trigger)
                break;
        }
        return false;
    }

    addToIOQueue(tmd, 1, tmdSize, fp);
    addToIOQueue(NULL, 0, 0, fp);
    addToScreenLog("title.tmd saved");

    char *toScreen = getToFrameBuffer();
    strcpy(toScreen, "=>Title type: ");
    bool hasDependencies;
    switch(getTidHighFromTid(tmd->tid)) // Title type
    {
        case TID_HIGH_GAME:
            strcat(toScreen, "eShop or Packed");
            hasDependencies = false;
            break;
        case TID_HIGH_DEMO:
            strcat(toScreen, "eShop/Kiosk demo");
            hasDependencies = false;
            break;
        case TID_HIGH_DLC:
            strcat(toScreen, "eShop DLC");
            hasDependencies = true;
            break;
        case TID_HIGH_UPDATE:
            strcat(toScreen, "eShop Update");
            hasDependencies = true;
            break;
        case TID_HIGH_SYSTEM_APP:
            strcat(toScreen, "System Application");
            hasDependencies = false;
            break;
        case TID_HIGH_SYSTEM_DATA:
            strcat(toScreen, "System Data Archive");
            hasDependencies = false;
            break;
        case TID_HIGH_SYSTEM_APPLET:
            strcat(toScreen, "Applet");
            hasDependencies = false;
            break;
        // vWii //
        case TID_HIGH_VWII_IOS:
            strcat(toScreen, "Wii IOS");
            hasDependencies = false;
            break;
        case TID_HIGH_VWII_SYSTEM_APP:
            strcat(toScreen, "vWii System Application");
            hasDependencies = false;
            break;
        case TID_HIGH_VWII_SYSTEM:
            strcat(toScreen, "vWii System Channel");
            hasDependencies = false;
            break;
        default:
            sprintf(toScreen + strlen(toScreen), "Unknown (0x%08X)", getTidHighFromTid(tmd->tid));
            hasDependencies = false;
            break;
    }
    addToScreenLog(toScreen);

    char *dup = downloadUrl + strlen(downloadUrl);
    strcpy(dup, "cetk");
    strcpy(idp, "title.tik");
    if(!fileExists(installDir))
    {
        int tikRes = downloadFile(downloadUrl, installDir, NULL, FILE_TYPE_TIK, false);
        switch(tikRes)
        {
            case 2:
                addToScreenLog("title.tik not found on the NUS. Generating...");
                startNewFrame();
                textToFrame(0, 0, gettext("Creating fake title.tik"));
                writeScreenLog(3);
                drawFrame();

                if(generateTik(installDir, titleEntry))
                    addToScreenLog("Fake ticket created successfully");
                else
                    return false;

                break;
            case 1:
                return false;
            default:
                break;
        }
    }
    else
        addToScreenLog("title.tik skipped!");

    if(!AppRunning(true))
        return false;

    strcpy(idp, "title.cert");
    if(!fileExists(installDir))
    {
        debugPrintf("Creating CERT...");
        startNewFrame();
        textToFrame(0, 0, gettext("Creating CERT"));
        writeScreenLog(3);
        drawFrame();

        if(generateCert(installDir))
            addToScreenLog("Cert created!");
        else
            return false;

        if(!AppRunning(true))
            return false;
    }
    else
        addToScreenLog("Cert skipped!");

    downloadData data;
    data.contents = tmd->num_contents;
    data.dcontent = 0;
    data.dlnow = data.dltmp = data.dltotal = 0.0D;
    data.eta = 0;

    // Get .app and .h3 files
    for(int i = 0; i < tmd->num_contents; ++i)
    {
        data.dltotal += (double)(tmd->contents[i].size);
        if(tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED)
        {
            ++data.contents;
            data.dltotal += 20; // TODO
        }
    }

    disableApd();
    char *dupp = dup + 8;
    char *idpp = idp + 8;
    char cid[9];
    for(int i = 0; i < tmd->num_contents && AppRunning(true); ++i)
    {
        hex(tmd->contents[i].cid, 8, cid);
        strcpy(dup, cid);
        strcpy(idp, cid);
        strcpy(idpp, ".app");

        data.cs = tmd->contents[i].size;
        if(downloadFile(downloadUrl, installDir, &data, FILE_TYPE_APP, true) == 1)
        {
            enableApd();
            return false;
        }
        ++data.dcontent;

        if(tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED)
        {
            strcpy(dupp, ".h3");
            strcpy(idpp, ".h3");

            if(downloadFile(downloadUrl, installDir, &data, FILE_TYPE_H3, true) == 1)
            {
                enableApd();
                return false;
            }
            ++data.dcontent;
        }
    }

    if(cancelOverlayId >= 0)
        closeCancelOverlay();

    if(!AppRunning(true))
    {
        enableApd();
        return false;
    }

    bool ret;
    if(inst)
    {
        *idp = '\0';
        ret = install(titleEntry->name, hasDependencies, dlDev, installDir, toUSB, keepFiles, tmd);
    }
    else
        ret = true;

    enableApd();
    return ret;
}

char *getRamBuf()
{
    return (char *)ramBuf;
}

size_t getRamBufSize()
{
    return ramBufSize;
}

void clearRamBuf()
{
    if(ramBuf != NULL)
    {
        ramBufSize = 0;
        MEMFreeToDefaultHeap((void *)ramBuf);
        ramBuf = NULL;
    }
}
