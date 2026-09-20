/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2023 V10lator <v10lator@myway.de>                    *
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
#include <errno.h>
#include <netinet/tcp.h>

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
#include <queue.h>
#include <renderer.h>
#include <romfs.h>
#include <state.h>
#include <thread.h>
#include <ticket.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/bsp.h>
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>
#include <curl/curl.h>
#include <nn/ac/ac_c.h>
#include <nn/nets2/somemopt.h>
#include <nn/result.h>
#include <nsysnet/_socket.h>
#include <nsysnet/misc.h>
#include <nsysnet/netconfig.h>
#pragma GCC diagnostic pop

#define USERAGENT        "NUSspli/" NUSSPLI_VERSION
#define SMOOTHING_FACTOR 0.2f

// A TCP stream cannot exceed its receive window divided by the round trip time,
// and CafeOS backs socket buffers from a small built-in pool that cannot grant a
// window this size - hence the donation below. 0x40000 is what RetroArch asks
// for on this console; at the ~10 ms round trip of a local CDN edge it is already
// far more than the hardware can fill, and it leaves room for DL_STREAMS of them
// inside the donation.
#define SOCKET_BUFSIZE 0x40000

// CafeOS hands every socket its buffers out of one global pool that
// socket_lib_init() leaves at a small default size, which is what quietly clamps
// the window above. somemopt() lets a title donate its own memory to that pool.
#define SOCKET_POOL_SIZE 0x300000 // 3 MB - the maximum somemopt() accepts

// Even with the window fixed, one stream to a CDN that far away spends most of
// its life waiting on acknowledgements. Splitting a content file across a couple
// of streams sidesteps that: each one is slow, together they fill the link. Two
// is enough - the console tops out long before the streams do, and every extra
// stream costs another slice of a socket pool that ftpiiu also feeds from.
#define DL_STREAMS   2
#define DL_CHUNKSIZE (1024 * 1024)
// Twice as many buffers as streams. A finished chunk has to wait for the chunks
// before it to be written out, and with one buffer per stream that wait idles
// the stream too - which showed up as a sawtooth in the reported speed. Spare
// buffers let a stream start its next range while an earlier chunk is still
// queued for the disk.
#define DL_SLOTS (DL_STREAMS * 2)

// The benchmark varies the stream count at run time, so the arrays are sized for
// the largest it asks for. In a release build these collapse back to DL_*.
#ifdef NUSSPLI_DEBUG
#define DL_MAX_STREAMS 3
#else
#define DL_MAX_STREAMS DL_STREAMS
#endif
#define DL_MAX_SLOTS (DL_MAX_STREAMS * 2)
// Below this the extra connections cost more than they win, and the .h3, ticket
// and TMD files are tiny to begin with.
#define DL_MIN_PARALLEL (2 * DL_CHUNKSIZE)

static bool initialised = false;
static CURL *curl;
static char curlError[CURL_ERROR_SIZE];
static bool curlReuseConnection = true;
static void *socketPool = NULL;
static OSThread *socketPoolThread = NULL;
static bool socketPoolDonated = false;
#ifdef NUSSPLI_DEBUG
// Set by the speed test so one run can compare configurations back to back,
// under the same network conditions, instead of across rebuilds.
static bool speedTestOverride = false;
static bool speedTestRusrbuf = false;
static int speedTestRcvbuf = IO_BUFSIZE;
#endif

typedef struct
{
    CURL *handle; // the stream carrying this chunk, NULL when it isn't in flight
    uint8_t *buf;
    curl_off_t start; // this chunk's offset in the file
    size_t size; // how many bytes this chunk covers
    size_t filled; // how many have arrived
    bool full; // finished, waiting its turn to be written out
} dlChunk;

typedef struct
{
    const char *url;
    FSAFileHandle fp;
    curl_off_t start; // first byte we still need
    curl_off_t end; // one past the last byte of the file
    volatile void *cdata;
} dlJob;

static dlChunk dlSlots[DL_MAX_SLOTS];
static int dlStreams = DL_STREAMS;
static int dlSlotCount = DL_SLOTS;
// One handle per buffer, so a free slot is always ready to be issued. How many
// of them libCURL actually connects at once is its own business - see the
// CURLMOPT_MAX_TOTAL_CONNECTIONS below.
static CURL *dlHandles[DL_MAX_SLOTS];
static bool parallelReady = false;

static size_t chunkWrite(const void *ptr, size_t size, size_t n, void *userdata);
static void initParallel(void);
static void deinitParallel(void);

static void *cancelOverlay = NULL;
static CURLM *multi = NULL;

typedef struct
{
    bool running;
    CURLcode error;
    spinlock lock;
    OSTick ts;
    curl_off_t dltotal;
    curl_off_t dlnow;
} curlProgressData;

#define closeCancelOverlay()               \
    {                                      \
        removeErrorOverlay(cancelOverlay); \
        cancelOverlay = NULL;              \
    }

// Both download paths report through this. The byte count and the timestamp have
// to be published together: the screen samples them on its own clock and divides
// one by the other, so a fresh count beside a stale tick reads as a speed that
// never happened.
static void publishProgress(volatile curlProgressData *data, curl_off_t dltotal, curl_off_t dlnow)
{
    OSTick t = OSGetTick();
    if(spinTryLock(data->lock))
    {
        data->ts = t;
        data->dltotal = dltotal;
        data->dlnow = dlnow;
        spinReleaseLock(data->lock);
    }

    addEntropy(&dlnow, sizeof(curl_off_t));
    addEntropy(&t, sizeof(OSTick));
}

static int progressCallback(void *rawData, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;

    curlProgressData *data = (curlProgressData *)rawData;
    if(!AppRunning(false))
        data->error = CURLE_ABORTED_BY_CALLBACK;

    if(data->error != CURLE_OK)
        return 1;

    publishProgress((volatile curlProgressData *)data, dltotal, dlnow);
    return 0;
}

// somemopt(SOMEMOPT_REQUEST_INIT) hands our memory to the network stack and only
// returns once nsysnet shuts down, so it needs a thread to sit in for the rest of
// the app's life. That also means we never free the pool while it is live - the
// stack is still holding pointers into it.
static int socketPoolThreadMain(int argc, const char **argv)
{
    (void)argc;
    (void)argv;

    // BIG_BUFFERS splits the donation 50-50 between small and big buffers instead
    // of 80-20. Receive buffers are what we are here for, so the big half is the
    // half that matters.
    int ret = somemopt(SOMEMOPT_REQUEST_INIT, socketPool, SOCKET_POOL_SIZE, SOMEMOPT_FLAGS_BIG_BUFFERS);

    // A request that never took never signals either, and initDownloader() is
    // sitting in WAIT_FOR_INIT on the thread that draws the screen.
    if(ret < 0)
        somemopt(SOMEMOPT_REQUEST_CANCEL_WAIT, NULL, 0, SOMEMOPT_FLAGS_NONE);

    return ret;
}

// A second INIT fails while the first donation is still live, so a plain
// reconnect must not try again. A network reset is different: it runs
// socket_lib_finish(), which ends the blocking request and hands the pool back,
// and then the donation does have to be made anew - a terminated thread is how
// we tell the two apart.
static void initSocketPool()
{
    // Ours is the only donation that must not be repeated, and the only one we
    // may ever end, so that is what the flag tracks. The pool's own accounting
    // would count everybody's - nsysnet's defaults, another title's donation -
    // and hand us a buffer we do not own.
    if(socketPoolDonated)
        return;

    if(socketPool == NULL)
    {
        socketPool = MEMAllocFromDefaultHeapEx(SOCKET_POOL_SIZE, 0x40);
        if(socketPool == NULL)
        {
            debugPrintf("initSocketPool: Out of memory");
            return;
        }
    }

    // Detached on purpose. somemopt() does not return until the socket library
    // shuts down, and NUSspli only does that on a network reset - so on the way
    // out this thread is still sitting in the call, and anything waiting to join
    // it would hang the exit. A detached thread is never joined, so the stack
    // that prepareThread() allocated is handed back by a deallocator instead.
    socketPoolThread = startThread("NUSspli socket pool", THREAD_PRIORITY_LOW, STACKSIZE_SMALL, socketPoolThreadMain, 0, NULL, OS_THREAD_ATTRIB_AFFINITY_CPU2);
    if(socketPoolThread == NULL)
    {
        debugPrintf("initSocketPool: Couldn't start thread");
        return;
    }

    // From here the thread is parked inside somemopt() until the socket library
    // ends, so the pool counts as ours whatever the donation turned out to be.
    socketPoolDonated = true;

    // Donating is asynchronous, and a socket created before it lands would get a
    // default-sized buffer anyway, so wait it out. Returns the bytes now in use.
    int used = somemopt(SOMEMOPT_REQUEST_WAIT_FOR_INIT, NULL, 0, SOMEMOPT_FLAGS_NONE);
    debugPrintf("initSocketPool: %d bytes donated", used);
}

// All the socket options we set below are pure performance tweaks, so a failure
// is never fatal. CafeOS answers with ENOPROTOOPT (92, "Non-supported option")
// for options it doesn't know about and returning CURL_SOCKOPT_ERROR on that
// would kill the whole transfer instead of just losing the tweak.
static inline bool trySockopt(curl_socket_t socket, int level, int option, int value, const char *name)
{
    int ret = setsockopt(socket, level, option, &value, sizeof(value));
    if(ret != 0 && errno != 92)
    {
        debugPrintf("initSocket: Error setting %s: %d", name, errno);
        return false;
    }

    return true;
}

static int initSocket(void *ptr, curl_socket_t socket, curlsocktype type)
{
    (void)ptr;
    (void)type;

    bool ret = trySockopt(socket, SOL_SOCKET, SO_WINSCALE, 1, "WinScale");
    if(ret)
    {
        ret = trySockopt(socket, SOL_SOCKET, SO_TCPSACK, 1, "TCP SAck");
        if(ret)
        {
            ret = trySockopt(socket, IPPROTO_TCP, TCP_NODELAY, 1, "TCP nodelay"); // libCURL default
            if(ret)
            {
                ret = trySockopt(socket, SOL_SOCKET, 0x4000, 1, "Noslowstart"); // Disable slowstart
                if(ret)
                {
                    ret = trySockopt(socket, SOL_SOCKET, SO_KEEPALIVE, 0, "TCP keepalive"); // libCURL default
                    if(ret)
                    {
                        ret = trySockopt(socket, SOL_SOCKET, SO_SNDBUF, IO_BUFSIZE, "send buffersize");
                        if(ret)
                        {
                            // A socket draws from the donated pool only once it asks to,
                            // and only for buffers sized after the fact.
                            bool rusrbuf = socketPoolDonated;
                            int rcvbuf = SOCKET_BUFSIZE;
#ifdef NUSSPLI_DEBUG
                            if(speedTestOverride)
                            {
                                rusrbuf = speedTestRusrbuf && socketPoolDonated;
                                rcvbuf = speedTestRcvbuf;
                            }
#endif
                            // Not part of the chain: a stack that turns this one
                            // down still gives a working socket, just a smaller
                            // receive buffer.
                            if(rusrbuf && !trySockopt(socket, SOL_SOCKET, SO_RUSRBUF, 1, "user receive buffers"))
                                rcvbuf = IO_BUFSIZE;

                            // Anything past IO_BUFSIZE only exists inside the
                            // donated pool - the default one answers EINVAL - and
                            // this option does gate the connection.
                            ret = trySockopt(socket, SOL_SOCKET, SO_RCVBUF, rcvbuf, "receive buffersize");
                            if(!ret && rcvbuf != IO_BUFSIZE)
                                ret = trySockopt(socket, SOL_SOCKET, SO_RCVBUF, IO_BUFSIZE, "receive buffersize");
                        }
                    }
                }
            }
        }
    }

#ifdef NUSSPLI_DEBUG
    // What was asked for and what the stack settled on are two different things.
    int got = 0;
    socklen_t gotLen = sizeof(got);
    if(getsockopt(socket, SOL_SOCKET, SO_RCVBUF, &got, &gotLen) == 0)
        debugPrintf("initSocket: SO_RCVBUF requested %d, got %d", speedTestOverride ? speedTestRcvbuf : SOCKET_BUFSIZE, got);
#endif

    return ret ? CURL_SOCKOPT_OK : CURL_SOCKOPT_ERROR;
}

static CURLcode ssl_ctx_init(CURL *cu, void *sslctx, void *parm)
{
    (void)cu;
    (void)parm;

    mbedtls_ssl_conf_rng((mbedtls_ssl_config *)sslctx, NUSrng, NULL);
    return CURLE_OK;
}

#define initNetwork() (curlReuseConnection = false)

static bool showNetworkError(const char *err)
{
    char toScreen[512];
    if(toScreen != err)
        strcpy(toScreen, err);

    int os = 0;
    int frames = 0;
    char *p = NULL;
    if(autoResumeEnabled())
    {
        os = 9 * 60; // 9 seconds with 60 FPS
        frames = os;
        strcat(toScreen, "\n\n");
        p = toScreen + strlen(toScreen);
        const char *pt = localise("Next try in _ seconds.");
        strcpy(p, pt);
        const char *n = strchr(pt, '_');
        p += n - pt;
    }
    else
        drawErrorFrame(toScreen, B_RETURN | Y_RETRY);

    int s;
    bool ret = false;
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
            ret = true;
            break;
        }
    }

    return ret;
}

// We're not using WUTs NNResult_IsSuccess() / NNResult_IsFailure() here as it's wrong
static void resetNetwork()
{
    BOOL con;
    NNResult nnres = ACIsApplicationConnected(&con);
    if(nnres.value != 0 || con)
        return;

    void *ovl = addErrorOverlay(localise("Preparing. This might take some time. Please be patient."));

    // Disconnect from network. deinitDownloader() ends the socket library on its
    // way out, so there is nothing left to finish here.
    restartUdpLog1();
    deinitDownloader();
    NNResult cr;

closeAgain:
    nnres = ACClose();
    do
    {
        cr = ACGetCloseStatus();
        if(cr.value == -1) // FAILED
        {
            if(ovl)
                removeErrorOverlay(ovl);

            if(showNetworkError(localise("Error closing network!")))
            {
                ovl = addErrorOverlay(localise("Preparing. This might take some time. Please be patient."));
                goto closeAgain;
            }

            goto exitApp;
        }
    } while(cr.value != 0); // SUCCESS. A value of 1 means processing, so we're not handling it.

    // Connect to network
reconnect:
    nnres = ACConnect();
    if(nnres.value == 0)
    {
        socket_lib_init();
        set_multicast_state(true);

        restartUdpLog2();
        initDownloader();

        if(ovl)
            removeErrorOverlay(ovl);

        return;
    }

    if(ovl)
        removeErrorOverlay(ovl);

    if(showNetworkError(localise("Error connecting to network!")))
    {
        ovl = addErrorOverlay(localise("Preparing. This might take some time. Please be patient."));
        goto reconnect;
    }

exitApp:
    if(AppRunning(true))
        homeButtonCallback((void *)true);
}

bool initDownloader()
{
    initNetwork();

    struct curl_blob blob = { .data = NULL, .flags = CURL_BLOB_COPY };
    blob.len = readFile(ROMFS_PATH "ca-certs.pem", &blob.data);
    if(blob.data == NULL)
        return false;

    char pUrl[sizeof("http://") + 0x80 /* host */ + 0x40 /* user and pass */ + 5 /* port */ + 3 /* rest */] = "http://"; // TODO;
    char *pUrl2 = NULL;

    if(netconf_init() == 0)
    {
        NetConfProxyConfig proxy;
        if(netconf_get_proxy_config(&proxy) == 0)
        {
            if(proxy.use_proxy == NET_CONF_PROXY_ENABLED)
            {
                pUrl2 = pUrl + sizeof("http://") - 1;
                size_t ss;

                if(proxy.auth_type == NET_CONF_PROXY_AUTH_TYPE_BASIC_AUTHENTICATION)
                {
                    ss = strlen(proxy.username);
                    OSBlockMove(pUrl2, proxy.username, ss, false);
                    pUrl2 += ss;

                    *pUrl2 = ':';

                    ss = strlen(proxy.password);
                    OSBlockMove(++pUrl2, proxy.password, ss, false);
                    pUrl2 += ss;

                    *pUrl2 = '@';
                    ++pUrl2;
                }

                ss = strlen(proxy.host);
                OSBlockMove(pUrl2, proxy.host, ss, false);
                pUrl2 += ss;

                *pUrl2 = ':';
                itoa(proxy.port, ++pUrl2, 10);

                pUrl2 = pUrl;
                debugPrintf("Proxy: %s", pUrl2);
            }
        }
        else
            debugPrintf("Proxy error!");

        netconf_close();
    }
    else
        debugPrintf("Netconf error!");

    CURLcode ret = curl_global_init(CURL_GLOBAL_DEFAULT & ~(CURL_GLOBAL_SSL));
    if(ret != CURLE_OK)
    {
        MEMFreeToDefaultHeap(blob.data);
        return false;
    }

    curl = curl_easy_init();
    if(curl == NULL)
    {
        debugPrintf("curl_easy_init() failed!");
        curl_global_cleanup();
        MEMFreeToDefaultHeap(blob.data);
        return false;
    }

#define setOpt(opt, v)                                                               \
    ret = curl_easy_setopt(curl, opt, (v));                                          \
    if(ret != CURLE_OK)                                                              \
    {                                                                                \
        debugPrintf("curl_easy_setopt() failed: %s (%u / %d)", curlError, opt, ret); \
        curl_easy_cleanup(curl);                                                     \
        curl = NULL;                                                                 \
        curl_global_cleanup();                                                       \
        if(blob.data != NULL)                                                        \
            MEMFreeToDefaultHeap(blob.data);                                         \
        return false;                                                                \
    }

#ifdef NUSSPLI_DEBUG
    curlError[0] = '\0';
    setOpt(CURLOPT_ERRORBUFFER, curlError);
#endif
    setOpt(CURLOPT_SOCKOPTFUNCTION, initSocket);
    setOpt(CURLOPT_USERAGENT, USERAGENT);
    setOpt(CURLOPT_XFERINFOFUNCTION, progressCallback);
    setOpt(CURLOPT_NOPROGRESS, 0L);
    setOpt(CURLOPT_FOLLOWLOCATION, 1L);
    setOpt(CURLOPT_MAXREDIRS, 8L);
    setOpt(CURLOPT_SSL_CTX_FUNCTION, ssl_ctx_init);
    setOpt(CURLOPT_CAINFO_BLOB, &blob);

    // libCURL copied the certificates (CURL_BLOB_COPY), so we're done with our copy.
    MEMFreeToDefaultHeap(blob.data);
    blob.data = NULL;

    setOpt(CURLOPT_LOW_SPEED_LIMIT, 1L);
    setOpt(CURLOPT_LOW_SPEED_TIME, 60L);
    setOpt(CURLOPT_ACCEPT_ENCODING, "");
    setOpt(CURLOPT_PROXY, pUrl2);
#undef setOpt

    initSocketPool();
    initParallel();

    initialised = true;
    return true;
}

// somemopt(SOMEMOPT_REQUEST_INIT) does not return until the socket library is
// shut down, so the donating thread stays parked - and the title cannot unload -
// until socket_lib_finish() runs. The UDP log holds a socket of its own on the
// same library, so it goes first, and the thread is joined once it is unparked.
static void releaseSocketPool(void)
{
    if(socketPoolThread == NULL)
        return;

    shutdownDebug();
    socket_lib_finish();
    stopThread(socketPoolThread, NULL);
    socketPoolThread = NULL;
    socketPoolDonated = false;
}

void deinitDownloader()
{
    if(!initialised)
        return;

    deinitParallel();
    debugPrintf("Parallel streams closed");

    if(curl != NULL)
    {
        curl_easy_cleanup(curl);
        curl = NULL;
    }
    curl_global_cleanup();
    debugPrintf("curl closed");

    // Last, as it takes the socket library with it: every handle above still had
    // a keep-alive connection to close.
    releaseSocketPool();
    initialised = false;
}

static int dlThreadMain(int argc, const char **argv)
{
    debugPrintf("Download thread spawned!");
    argc = curl_easy_perform(curl);
    ((curlProgressData *)argv[0])->running = false;
    return argc;
}

static void deinitParallel(void)
{
    parallelReady = false;
    if(multi != NULL)
    {
        curl_multi_cleanup(multi);
        multi = NULL;
    }

    for(int i = 0; i < DL_MAX_SLOTS; ++i)
        if(dlHandles[i] != NULL)
        {
            curl_easy_cleanup(dlHandles[i]);
            dlHandles[i] = NULL;
        }

    for(int i = 0; i < DL_MAX_SLOTS; ++i)
        if(dlSlots[i].buf != NULL)
        {
            MEMFreeToDefaultHeap(dlSlots[i].buf);
            dlSlots[i].buf = NULL;
        }
}

static size_t chunkWrite(const void *ptr, size_t size, size_t n, void *userdata)
{
    dlChunk *chunk = (dlChunk *)userdata;
    size *= n;

    // A server that ignored our Range header and started sending the whole file
    // would silently corrupt the chunk after this one, so refuse the overflow
    // instead and let libCURL fail the transfer.
    if(chunk->filled + size > chunk->size)
        return 0;

    OSBlockMove(chunk->buf + chunk->filled, ptr, size, false);
    chunk->filled += size;
    return size;
}

// Duplicating the configured handle keeps every stream on the same certificates,
// user agent, socket options and timeouts without repeating the setup.
static void initParallel(void)
{
    if(parallelReady)
        return;

    // One multi handle for the whole session: the connection cache lives in it,
    // so building a fresh one per file would hand back a handshake per stream on
    // every single content.
    multi = curl_multi_init();
    if(multi == NULL)
    {
        debugPrintf("initParallel: Out of memory, falling back to a single connection");
        return;
    }

    // MAXCONNECTS only sizes that cache, so it is sized once for the largest the
    // session can ask for. How many of them may run at a time is per job.
    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, (long)DL_MAX_STREAMS);

    for(int i = 0; i < DL_MAX_SLOTS; ++i)
    {
        dlSlots[i].buf = MEMAllocFromDefaultHeapEx(DL_CHUNKSIZE, 0x40);
        if(dlSlots[i].buf == NULL)
        {
            debugPrintf("initParallel: Out of memory, falling back to a single connection");
            deinitParallel();
            return;
        }
    }

    for(int i = 0; i < DL_MAX_SLOTS; ++i)
    {
        dlHandles[i] = curl_easy_duphandle(curl);
        if(dlHandles[i] == NULL)
        {
            debugPrintf("initParallel: Setup of stream %d failed, falling back to a single connection", i);
            deinitParallel();
            return;
        }

#pragma GCC diagnostic ignored "-Wcast-function-type"
        CURLcode wf = curl_easy_setopt(dlHandles[i], CURLOPT_WRITEFUNCTION, (size_t(*)(const void *, size_t, size_t, FILE *))chunkWrite);
#pragma GCC diagnostic pop

        if(wf != CURLE_OK
            // Each stream keeps its own connection alive across chunks - a fresh
            // handshake per chunk would hand the round trip time right back.
            || curl_easy_setopt(dlHandles[i], CURLOPT_FRESH_CONNECT, 0L) != CURLE_OK
            || curl_easy_setopt(dlHandles[i], CURLOPT_NOPROGRESS, 1L) != CURLE_OK
            // A compressed range response would not match the byte count we sized
            // the chunk for, and content is already compressed anyway.
            || curl_easy_setopt(dlHandles[i], CURLOPT_ACCEPT_ENCODING, NULL) != CURLE_OK)
        {
            debugPrintf("initParallel: Setup of stream %d failed, falling back to a single connection", i);
            deinitParallel();
            return;
        }
    }

    parallelReady = true;
}

// A 206 carrying a different range than we asked for would be exactly the right
// length and land at the wrong offset - silent corruption that only surfaces as a
// failed hash hours later, at install time, with no clue which file is bad.
static bool rangeMatches(CURL *handle, const dlChunk *chunk)
{
    struct curl_header *h;
    if(curl_easy_header(handle, "Content-Range", 0, CURLH_HEADER, -1, &h) != CURLHE_OK)
        return false;

    long long from, to;
    if(sscanf(h->value, "bytes %lld-%lld", &from, &to) != 2)
        return false;

    return from == (long long)chunk->start && to == (long long)(chunk->start + (curl_off_t)chunk->size - 1);
}

// Committed bytes plus whatever is still in flight.
static curl_off_t chunkedProgress(curl_off_t written, curl_off_t start)
{
    curl_off_t p = written - start;
    for(int i = 0; i < dlSlotCount; ++i)
        p += dlSlots[i].filled;

    return p;
}

// Slots are handed out and drained in the same cyclic order, so the chunk that
// has to be written next is always the one at nextWrite - no search, no
// reordering buffer, and the file on disk is always a valid prefix of itself,
// which is exactly what resume needs after a cancel or a failure.
static int mdlThreadMain(int argc, const char **argv)
{
    (void)argc;
    debugPrintf("Parallel download thread spawned!");

    dlJob *job = (dlJob *)argv[0];
    volatile curlProgressData *cdata = (volatile curlProgressData *)job->cdata;

    // MAX_TOTAL_CONNECTIONS is what caps concurrency, queueing the rest
    // internally. Between it and the cache libCURL does the bookkeeping that
    // would otherwise be a busy array and a search.
    curl_multi_setopt(multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long)dlStreams);

    CURLcode ret = CURLE_OK;
    curl_off_t issue = job->start; // next byte to hand to a stream
    curl_off_t written = job->start; // next byte to hand to the I/O queue
    int nextIssue = 0;
    int nextWrite = 0;
    char range[48];

    for(int i = 0; i < dlSlotCount; ++i)
    {
        dlSlots[i].size = dlSlots[i].filled = 0;
        dlSlots[i].handle = NULL;
        dlSlots[i].full = false;
    }

    // Published on either side of the blocking write below: while a commit waits
    // on the disk nothing else refreshes the figure, and the screen would pair
    // its next byte count with a stale tick.
#define updateProgress() publishProgress(cdata, job->end - job->start, chunkedProgress(written, job->start))

    while(true)
    {
        while(issue < job->end && dlSlots[nextIssue].handle == NULL && !dlSlots[nextIssue].full)
        {
            dlChunk *chunk = dlSlots + nextIssue;
            curl_off_t len = job->end - issue;
            if(len > DL_CHUNKSIZE)
                len = DL_CHUNKSIZE;

            sprintf(range, "%lld-%lld", (long long)issue, (long long)(issue + len - 1));
            chunk->start = issue;
            chunk->size = (size_t)len;
            chunk->filled = 0;

            if(curl_easy_setopt(dlHandles[nextIssue], CURLOPT_URL, job->url) != CURLE_OK || curl_easy_setopt(dlHandles[nextIssue], CURLOPT_RANGE, range) != CURLE_OK || curl_easy_setopt(dlHandles[nextIssue], CURLOPT_WRITEDATA, chunk) != CURLE_OK || curl_multi_add_handle(multi, dlHandles[nextIssue]) != CURLM_OK)
            {
                ret = CURLE_FAILED_INIT;
                break;
            }

            chunk->handle = dlHandles[nextIssue];
            issue += len;
            if(++nextIssue == dlSlotCount)
                nextIssue = 0;
        }

        if(ret != CURLE_OK)
            break;

        int running = 0;
        if(curl_multi_perform(multi, &running) != CURLM_OK)
        {
            ret = CURLE_RECV_ERROR;
            break;
        }

        CURLMsg *msg;
        int left;
        while((msg = curl_multi_info_read(multi, &left)) != NULL)
        {
            if(msg->msg != CURLMSG_DONE)
                continue;

            for(int i = 0; i < dlSlotCount; ++i)
            {
                if(dlSlots[i].handle != msg->easy_handle)
                    continue;

                // The status decides first, and deliberately so. A server that
                // ignored Range answers 200 with the whole file, which overflows
                // the chunk buffer and ends the transfer as CURLE_WRITE_ERROR.
                // Reading the result first would hide "no Range support" behind a
                // generic write failure, and the fallback to a single stream -
                // the entire point of noticing - would never fire.
                long code = 0;
                if(curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK && code != 0 && code != 206)
                {
                    debugPrintf("Range request answered with %ld, expected 206", code);
                    ret = CURLE_RANGE_ERROR;
                }
                else if(msg->data.result != CURLE_OK)
                    ret = msg->data.result;
                else if(dlSlots[i].filled != dlSlots[i].size)
                    ret = CURLE_PARTIAL_FILE; // Short range - treat it like a truncated transfer.
                else if(!rangeMatches(msg->easy_handle, dlSlots + i))
                {
                    debugPrintf("Range response did not cover %lld-%lld", (long long)dlSlots[i].start, (long long)(dlSlots[i].start + dlSlots[i].size - 1));
                    ret = CURLE_RANGE_ERROR;
                }

                curl_multi_remove_handle(multi, msg->easy_handle);
                dlSlots[i].handle = NULL;
                dlSlots[i].full = true;
                break;
            }
        }

        if(ret != CURLE_OK)
            break;

        updateProgress();

        while(dlSlots[nextWrite].full)
        {
            dlChunk *chunk = dlSlots + nextWrite;
#ifdef NUSSPLI_DEBUG
            // The benchmark drives this same path with nowhere to write to, so
            // what it measures is the network and not the disk behind it.
            const bool stored = job->fp == 0 || addToIOQueue(chunk->buf, 1, chunk->size, job->fp) == chunk->size;
#else
            const bool stored = addToIOQueue(chunk->buf, 1, chunk->size, job->fp) == chunk->size;
#endif
            if(!stored)
            {
                ret = CURLE_WRITE_ERROR;
                break;
            }

            written += chunk->size;
            chunk->size = chunk->filled = 0;
            chunk->full = false;
            if(++nextWrite == dlSlotCount)
                nextWrite = 0;
        }

        if(ret != CURLE_OK || written >= job->end)
            break;

        updateProgress();

        if(!AppRunning(false) || cdata->error != CURLE_OK)
        {
            ret = CURLE_ABORTED_BY_CALLBACK;
            break;
        }

        if(running)
            curl_multi_poll(multi, NULL, 0, 50, NULL);
    }

#undef updateProgress

    for(int i = 0; i < dlSlotCount; ++i)
        if(dlSlots[i].handle != NULL)
        {
            curl_multi_remove_handle(multi, dlSlots[i].handle);
            dlSlots[i].handle = NULL;
        }

    cdata->running = false;
    return ret;
}

#ifdef NUSSPLI_DEBUG
static void drawStatLine(int line, curl_off_t totalSize, curl_off_t currentSize, float bps, uint32_t *eta);
static const char *translateCurlError(CURLcode err, const char *error);

// The network stack runs on the Starbucks, the ARM coprocessor, and the argument
// against large socket buffers is that they saturate it. Reading it here puts that number next to the
// throughput it is supposed to explain, rather than leaving it to an overlay and
// the naked eye. Reported by the OS as tenths of a percent.
static float starbucksLoad(void) // CPU utilisation, percent
{
    uint32_t val = 0;
    return bspRead("Sys", 0, "cpuUtil", sizeof(val), &val) == BSP_ERROR_OK ? val / 10.0f : -1.0f;
}

// Two hosts at deliberately different distances, because the question is whether
// the receive window or the console itself is the limit: one sits about as far
// away as the CDN, the other is next door. Both are plain HTTP and both are far
// faster than the console, so neither can be the bottleneck. What arrives is
// thrown away - no disk, no decryption, nothing but recv().
//
// Each run is capped by bytes and by time so a full sweep stays minutes rather
// than hours, and the sweep is repeated in passes so every configuration is
// spread across the measurement window instead of owning one end of it.
#define SPEEDTEST_BYTES  (32 * 1024 * 1024)
#define SPEEDTEST_MAX_MS 15000
#define SPEEDTEST_PASSES 3
#define SPEEDTEST_PAUSE  3
// Below this the two hosts are not telling us anything different.
#define SPEEDTEST_MIN_SPREAD 30
// Past this the far run stops measuring the console and starts measuring the
// mirror: a link that long carries somebody else's congestion too, and a single
// retransmit costs more than the whole difference we came to look for.
#define SPEEDTEST_FAR_RTT 150
// And the near host only says what the console does when latency is not the
// limit if it is genuinely close. The lowest one found wins, so anything better
// than this is taken automatically.
#define SPEEDTEST_NEAR_RTT 20
// Spaces kept between two columns of the results table.
#define SPEEDTEST_GAP 3

// Hosts spread across continents, so that wherever this runs there is a wide
// spread of round trip times to choose from. Which two get used is decided by
// measurement, not by assumption: a host that is far from one line is near to
// another, and the whole point is to see how throughput moves with latency.
static const char *speedTestHosts[] = {
    "http://ipv4.download.thinkbroadband.com/200MB.zip",
    "http://mirror.yandex.ru/debian-cd/current/amd64/iso-cd/debian-13.7.0-amd64-netinst.iso",
    "http://mirrors.edge.kernel.org/ubuntu-releases/24.04/ubuntu-24.04.3-live-server-amd64.iso",
    "http://mirror.math.princeton.edu/pub/ubuntu-iso/24.04/ubuntu-24.04.3-live-server-amd64.iso",
    "http://ftp.riken.jp/Linux/ubuntu-releases/24.04/ubuntu-24.04.3-live-server-amd64.iso",
    "http://mirror.aarnet.edu.au/pub/ubuntu/releases/24.04/ubuntu-24.04.3-live-server-amd64.iso",
    "http://ftp.fau.de/ubuntu-releases/24.04/ubuntu-24.04.3-live-server-amd64.iso",
};

#define SPEEDTEST_HOSTS (sizeof(speedTestHosts) / sizeof(speedTestHosts[0]))

// Filled in by the probe below: the closest and the most distant host that
// answered, named after the latency actually measured to them.
static struct
{
    const char *name; // "far" and "near" relative to each other, not absolutes
    unsigned int rtt;
    char host[64];
    const char *url;
} speedTestTargets[2];

// Just the host part, so the screen can say which mirrors were picked.
static void speedTestHostName(const char *url, char *out, size_t len)
{
    const char *a = strstr(url, "://");
    a = a ? a + 3 : url;
    const char *b = strchr(a, '/');
    size_t n = (b ? (size_t)(b - a) : strlen(a));
    if(n >= len)
        n = len - 1;

    memcpy(out, a, n);
    out[n] = '\0';
}

static const struct
{
    const char *name;
    bool rusrbuf;
    int rcvbuf;
    int streams; // 1 drives the ordinary single-connection path
    bool disk; // land the bytes on the SD card instead of dropping them
} speedTestConfigs[] = {
    { "stock", false, IO_BUFSIZE, 1, false },
    { "user256", true, 0x40000, 1, false },
    { "user512", true, 0x80000, 1, false },
    { "parallel 2", true, 0x40000, 2, false },
    { "parallel 3", true, 0x40000, 3, false },
    { "stock+disk", false, IO_BUFSIZE, 1, true },
    { "user256+disk", true, 0x40000, 1, true },
    { "parallel 3+disk", true, 0x40000, 3, true },
};

#define SPEEDTEST_TARGETS (sizeof(speedTestTargets) / sizeof(speedTestTargets[0]))
#define SPEEDTEST_CONFIGS (sizeof(speedTestConfigs) / sizeof(speedTestConfigs[0]))

// Somewhere to put the bytes of a run that measures the card as well as the
// link. The directory goes away with the results.
#define SPEEDTEST_DIR  NUSDIR_SD "speedtest/"
#define SPEEDTEST_FILE SPEEDTEST_DIR "data.tmp"

static FSAFileHandle speedTestFile;

// One column of the log, appended to line and padded to width spaces - a width
// of 0 just appends. Counting characters would not line anything up: the font is
// proportional, so "far" and "near" padded to the same length still end at
// different places, which is why the padding is measured. FC_Draw() takes its
// text as a printf format, so a literal percent sign is doubled on the way in.

static uint32_t colRun;
static uint32_t colTarget;
static uint32_t colConfig;
static uint32_t colRate;
static uint32_t colLoad;

// Column widths are measured from the widest value each one can ever hold,
// because a guess in spaces does not survive a proportional font.
static void measureColumns(void)
{
    // Digits are not all the same width either. A run of ten tells them apart
    // even though getTextWidth() answers in whole spaces.
    char run[11];
    char digit = '0';
    uint32_t widest = 0;
    run[sizeof(run) - 1] = '\0';
    for(int d = 0; d < 10; ++d)
    {
        memset(run, '0' + d, sizeof(run) - 1);
        uint32_t w = getTextWidth(run);
        if(w > widest)
        {
            widest = w;
            digit = '0' + d;
        }
    }

    char sample[16];
    sprintf(sample, "[%c/%c]", digit, digit);
    colRun = getTextWidth(sample) + SPEEDTEST_GAP;
    sprintf(sample, "%c%c%c.%c", digit, digit, digit, digit);
    colRate = getTextWidth(sample) + SPEEDTEST_GAP;
    sprintf(sample, "%c%c%c%%", digit, digit, digit);
    colLoad = getTextWidth(sample) + SPEEDTEST_GAP;

    colTarget = colConfig = 0;
    for(size_t i = 0; i < SPEEDTEST_TARGETS; ++i)
    {
        uint32_t w = getTextWidth(speedTestTargets[i].name);
        if(w > colTarget)
            colTarget = w;
    }

    for(size_t i = 0; i < SPEEDTEST_CONFIGS; ++i)
    {
        uint32_t w = getTextWidth(speedTestConfigs[i].name);
        if(w > colConfig)
            colConfig = w;
    }

    colTarget += SPEEDTEST_GAP;
    colConfig += SPEEDTEST_GAP;
}

static char *addColumn(char *line, const char *field, uint32_t width, bool right)
{
    // Never less than one space, so a column that outgrows its width pushes the
    // next one along instead of running into it.
    uint32_t w = getTextWidth(field);
    uint32_t pad = w < width ? width - w : 1;

    if(right)
    {
        memset(line, ' ', pad);
        line += pad;
        pad = 0;
    }

    while(*field != '\0')
    {
        if(*field == '%')
            *line++ = '%';

        *line++ = *field++;
    }

    memset(line, ' ', pad);
    line += pad;
    *line = '\0';
    return line;
}

static volatile curl_off_t speedTestReceived;
static bool speedTestAborted;

static size_t discardWrite(const void *ptr, size_t size, size_t n, void *userdata)
{
    (void)userdata;
    size *= n;

    // Refusing the write is what ends a run: both targets are far bigger files
    // than a single sample needs.
    if(speedTestReceived >= SPEEDTEST_BYTES)
        return 0;

    speedTestReceived += (curl_off_t)size;

    // No file open means this run is only after the network.
    return speedTestFile == 0 ? size : addToIOQueue(ptr, 1, size, speedTestFile);
}

static int speedTestThreadMain(int argc, const char **argv)
{
    (void)argc;
    argc = curl_easy_perform(curl);
    ((curlProgressData *)argv[0])->running = false;
    return argc;
}

static void speedTestRun(size_t pass, size_t target, size_t config, size_t runIndex)
{
    const char *tn = speedTestTargets[target].name;
    const char *cn = speedTestConfigs[config].name;

    const int streams = speedTestConfigs[config].streams;
    speedTestFile = 0;
    if(speedTestConfigs[config].disk)
    {
        speedTestFile = openFile(SPEEDTEST_FILE, "w", SPEEDTEST_BYTES);
        if(speedTestFile == 0)
        {
            debugPrintf("Speedtest[%u/%s/%s]: couldn't open " SPEEDTEST_FILE, (unsigned int)pass, tn, cn);
            addErrorToScreenLog("couldn't open " SPEEDTEST_FILE);
            return;
        }
    }

    speedTestRusrbuf = speedTestConfigs[config].rusrbuf;
    speedTestRcvbuf = speedTestConfigs[config].rcvbuf;
    speedTestReceived = 0;
    dlStreams = streams;
    dlSlotCount = streams * 2;

    volatile curlProgressData cdata = {
        .running = true,
        .error = CURLE_OK,
        .dlnow = 0,
        .dltotal = 0,
    };
    spinCreateLock((cdata.lock), SPINLOCK_FREE);

    curlError[0] = '\0';
#pragma GCC diagnostic ignored "-Wcast-function-type"
    CURLcode co = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (size_t(*)(const void *, size_t, size_t, FILE *))discardWrite);
#pragma GCC diagnostic pop
    if(co != CURLE_OK || curl_easy_setopt(curl, CURLOPT_URL, speedTestTargets[target].url) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_RANGE, NULL) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)0) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cdata) != CURLE_OK)
    {
        debugPrintf("Speedtest[%u/%s/%s]: setup failed", (unsigned int)pass, tn, cn);
        return;
    }

    debugPrintf("Speedtest[%u/%s/%s]: START rusrbuf=%s rcvbuf=%d streams=%d donated=%s", (unsigned int)pass, tn, cn, speedTestConfigs[config].rusrbuf ? "yes" : "no", speedTestConfigs[config].rcvbuf, streams, socketPoolDonated ? "yes" : "no");

    curlReuseConnection = false;
    OSTime start = OSGetSystemTime();

    // The parallel path is exercised as it actually ships - same chunking, same
    // ordering, same validation - writing through the same I/O queue when the
    // run is meant to include the card.
    dlJob job = {
        .url = speedTestTargets[target].url,
        .fp = speedTestFile,
        .start = 0,
        .end = SPEEDTEST_BYTES,
        .cdata = &cdata,
    };

    char *argv[1] = { streams > 1 ? (char *)&job : (char *)&cdata };
    OSThread *thread = startThread("NUSspli speedtest", THREAD_PRIORITY_HIGH, STACKSIZE_BIG, streams > 1 ? mdlThreadMain : speedTestThreadMain, 1, (char *)argv, OS_THREAD_ATTRIB_AFFINITY_CPU0);
    if(thread == NULL)
    {
        debugPrintf("Speedtest[%u/%s/%s]: no thread", (unsigned int)pass, tn, cn);
        return;
    }

    char toScreen[192];
    curl_off_t last = 0;
    OSTime lastTick = start;
    float armSum = 0.0f;
    int armSamples = 0;
    int frames = 1;
    while(cdata.running && AppRunning(true))
    {
        if(--frames == 0)
        {
            frames = 30;
            OSTime now = OSGetSystemTime();
            curl_off_t got = streams > 1 ? cdata.dlnow : speedTestReceived;
            uint32_t elapsed = (uint32_t)OSTicksToMilliseconds(now - start);
            uint32_t ms = (uint32_t)OSTicksToMilliseconds(now - lastTick);
            float bps = ms ? ((float)(got - last) * 1000.0f) / (float)ms : 0.0f;
            last = got;
            lastTick = now;

            // A time series, not just an average: a link held back by its window
            // and one that is simply saturated end up alike but do not get there
            // the same way.
            float arm = starbucksLoad();
            if(arm >= 0.0f)
            {
                armSum += arm;
                ++armSamples;
            }

            debugPrintf("Speedtest[%u/%s/%s]: t=%ums bytes=%lld inst=%.0fB/s starbucks=%.1f%%", (unsigned int)pass, tn, cn, elapsed, (long long)got, bps, arm);

            startNewFrame();
            sprintf(toScreen, "Speed test [%u/%u] - run %u/%u - %s", (unsigned int)pass, SPEEDTEST_PASSES, (unsigned int)runIndex, (unsigned int)(SPEEDTEST_PASSES * SPEEDTEST_TARGETS * SPEEDTEST_CONFIGS), tn);
            textToFrame(0, ALIGNED_CENTER, toScreen);
            lineToFrame(1, SCREEN_COLOR_WHITE);

            sprintf(toScreen, "%-28s %4u ms", speedTestTargets[target].host, speedTestTargets[target].rtt);
            textToFrame(2, 0, toScreen);
            textToFrame(3, 0, cn);

            uint32_t eta = UINT32_MAX;
            drawStatLine(4, SPEEDTEST_BYTES, got, bps, &eta);

            strcpy(toScreen, "Now: ");
            getSpeedString(bps, toScreen + strlen(toScreen));
            textToFrame(6, 0, toScreen);

            strcpy(toScreen, "Average: ");
            getSpeedString(elapsed ? ((float)got * 1000.0f) / (float)elapsed : 0.0f, toScreen + strlen(toScreen));
            textToFrame(7, 0, toScreen);

            // Left column with the rest of the stats: the right edge is measured
            // in space widths and a proportional font clips whatever overruns it.
            if(arm >= 0.0f)
                sprintf(toScreen, "Starbucks (net stack CPU): %.1f%%%%", arm);
            else
                strcpy(toScreen, "Starbucks (net stack CPU): n/a");

            textToFrame(8, 0, toScreen);

            writeScreenLogCut(9, MAX_LINES - 3);
            lineToFrame(MAX_LINES - 2, SCREEN_COLOR_WHITE);
            textToFrame(MAX_LINES - 1, ALIGNED_CENTER, localise("Press " BUTTON_B " to abort"));

            drawFrame();

            if(elapsed >= SPEEDTEST_MAX_MS)
                cdata.error = CURLE_ABORTED_BY_CALLBACK;
        }

        showFrame();
        if(vpad.trigger & VPAD_BUTTON_B)
        {
            // Ends the sweep, not just this run - the outer loops read this.
            speedTestAborted = true;
            cdata.error = CURLE_ABORTED_BY_CALLBACK;
            break;
        }
    }

    int ret;
    stopThread(thread, &ret);

    if(speedTestFile != 0)
    {
        // The queue is asynchronous, so the clock cannot stop before the last
        // block is actually on the card - that wait is the point of the run.
        addToIOQueue(NULL, 0, 0, speedTestFile);
        flushIOQueue();
        speedTestFile = 0;
    }

    uint32_t ms = (uint32_t)OSTicksToMilliseconds(OSGetSystemTime() - start);
    const curl_off_t total = streams > 1 ? cdata.dlnow : speedTestReceived;
    float avg = ms ? ((float)total * 1000.0f) / (float)ms : 0.0f;

    // Straight from libCURL, so nothing rests on a measurement taken on the far
    // side of the network. Connect time is the round trip that decides whether
    // the receive window can be the limit at all.
    // Only the single-stream path drives the shared handle; a parallel run leaves
    // it untouched, and a stream handle would answer for its last chunk on a
    // connection it kept, not for the run. The latency is the probe's figure.
    curl_off_t curlSpeed = 0;
    long code = 0;
    if(streams == 1)
    {
        curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD_T, &curlSpeed);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }

    const float armAvg = armSamples ? armSum / (float)armSamples : -1.0f;
    debugPrintf("Speedtest[%u/%s/%s]: RESULT bytes=%lld ms=%u avg=%.0fB/s curl=%lldB/s rtt=%ums starbucks=%.1f%% http=%ld rc=%d", (unsigned int)pass, tn, cn, (long long)total, ms, avg, (long long)curlSpeed, speedTestTargets[target].rtt, armAvg, code, ret);

    // Fixed columns so the runs line up under each other and can be compared by
    // eye. getSpeedString() picks its own unit, which would make the numbers
    // ragged, so the rate is printed here in Mbit/s throughout.
    char logLine[MAX_CHARS];
    char field[32];

    sprintf(field, "[%u/%u]", (unsigned int)pass, SPEEDTEST_PASSES);
    char *l = addColumn(logLine, field, colRun, false);
    l = addColumn(l, tn, colTarget, false);
    l = addColumn(l, cn, colConfig, false);

    sprintf(field, "%.1f", avg * 8.0f / 1000000.0f);
    l = addColumn(l, field, colRate, true);
    l = addColumn(l, "Mbit/s   Starbucks", 0, false);

    if(armAvg >= 0.0f)
        sprintf(field, "%.0f%%", armAvg);
    else
        strcpy(field, "n/a");

    addColumn(l, field, colLoad, true);
    // Running out of budget or hitting the byte limit is how a run ends normally,
    // so neither counts as a failure.
    if(ret == CURLE_OK || ret == CURLE_WRITE_ERROR || ret == CURLE_ABORTED_BY_CALLBACK)
        addToScreenLog("%s", logLine);
    else
    {
        debugPrintf("Speedtest[%u/%s/%s]: curl error: %s (%d) %s", (unsigned int)pass, tn, cn, translateCurlError(ret, curlError), ret, curlError);
        addErrorToScreenLog("%s", logLine);

        // The message is somebody else's text, as long as CURL_ERROR_SIZE and
        // able to double in length when addColumn() escapes its percent signs,
        // so it gets a buffer of its own rather than the line's.
        char errLine[(CURL_ERROR_SIZE * 2) + 16];
        addColumn(addColumn(errLine, "", colRun, false), translateCurlError(ret, curlError), 0, false);
        addErrorToScreenLog("%s", errLine);
    }

    curlReuseConnection = false;
}

// One byte from each candidate, purely to time the connect. Whoever runs this
// gets their own spread of latencies rather than mine.
static bool speedTestPickTargets(void)
{
    unsigned int rtt[SPEEDTEST_HOSTS];
    size_t lo = SPEEDTEST_HOSTS;
    size_t hi = SPEEDTEST_HOSTS;

    for(size_t i = 0; i < SPEEDTEST_HOSTS && AppRunning(true); ++i)
    {
        char probeHost[64];
        speedTestHostName(speedTestHosts[i], probeHost, sizeof(probeHost));

        char probeLine[128];
        sprintf(probeLine, "Speed test - measuring latency %u/%u - %s", (unsigned int)(i + 1), (unsigned int)SPEEDTEST_HOSTS, probeHost);

        startNewFrame();
        textToFrame(0, ALIGNED_CENTER, probeLine);
        writeScreenLogCut(1, MAX_LINES - 3);
        drawFrame();
        showFrame();

        rtt[i] = 0;
        speedTestReceived = 0;
#pragma GCC diagnostic ignored "-Wcast-function-type"
        CURLcode co = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (size_t(*)(const void *, size_t, size_t, FILE *))discardWrite);
#pragma GCC diagnostic pop
        // Redirects are not followed on purpose. A mirror that bounces us to
        // HTTPS would put a TLS handshake and a stream cipher into a measurement
        // that is supposed to contain neither, so such a host is dropped here
        // instead of quietly skewing the numbers.
        // The progress callback is registered on this handle for real downloads
        // and dereferences whatever XFERINFODATA points at. There is no transfer
        // state here for it to read, so it has to be switched off rather than
        // left aimed at a stack frame that no longer exists.
        // Without this the option chain in initSocket() lands inside the
        // measurement: it runs between socket() and connect(), which is exactly
        // the window CONNECT_TIME covers, and every one of its calls crosses to
        // the core the network stack runs on.
        curlError[0] = '\0';
        if(co != CURLE_OK || curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, NULL) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_URL, speedTestHosts[i]) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L) != CURLE_OK || curl_easy_setopt(curl, CURLOPT_RANGE, "0-0") != CURLE_OK || curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL) != CURLE_OK)
            continue;

        CURLcode pr = curl_easy_perform(curl);
        if(pr != CURLE_OK)
        {
            debugPrintf("Speedtest: probe %s FAILED: %s (%d) %s", speedTestHosts[i], translateCurlError(pr, curlError), pr, curlError);
            addToScreenLog("    -- ms   %s   unreachable", probeHost);
            continue;
        }

        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if(code != 206)
        {
            debugPrintf("Speedtest: probe %s answered %ld, not a plain HTTP range - skipped", speedTestHosts[i], code);
            addToScreenLog("    -- ms   %s   HTTP %ld, skipped", probeHost, code);
            continue;
        }

        // Both timers count from the start of the request, so the lookup has to
        // come off to leave the TCP handshake alone - one round trip, which is
        // what speedtest tools report as latency.
        curl_off_t connectUs = 0;
        curl_off_t lookupUs = 0;
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME_T, &connectUs);
        curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME_T, &lookupUs);
        rtt[i] = (unsigned int)((connectUs - lookupUs) / 1000);
        debugPrintf("Speedtest: probe %s -> %u ms", speedTestHosts[i], rtt[i]);
        char probeLog[MAX_CHARS];
        char ms[16];
        sprintf(ms, "%u", rtt[i]);
        addColumn(addColumn(addColumn(probeLog, ms, 6, true), "ms  ", 0, false), probeHost, 0, false);
        addToScreenLog("%s", probeLog);

        if(rtt[i] == 0)
            continue;

        if(lo == SPEEDTEST_HOSTS || rtt[i] < rtt[lo])
            lo = i;
        if(rtt[i] > SPEEDTEST_FAR_RTT)
            continue;

        if(hi == SPEEDTEST_HOSTS || rtt[i] > rtt[hi])
            hi = i;
    }

    curl_easy_setopt(curl, CURLOPT_RANGE, NULL);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, initSocket);

    if(lo == SPEEDTEST_HOSTS || hi == SPEEDTEST_HOSTS || lo == hi)
    {
        debugPrintf("Speedtest: fewer than two usable hosts");
        addToScreenLog("no pair under %u ms to measure with", SPEEDTEST_FAR_RTT);
        return false;
    }

    // Two hosts a few milliseconds apart would say nothing about how throughput
    // moves with latency, which is the only reason there are two of them.
    if(rtt[hi] < rtt[lo] + SPEEDTEST_MIN_SPREAD)
    {
        debugPrintf("Speedtest: closest %u ms and furthest %u ms are too alike", rtt[lo], rtt[hi]);
        addToScreenLog("spread too small: %u ms vs %u ms", rtt[lo], rtt[hi]);
        return false;
    }

    speedTestTargets[0].name = "far";
    speedTestTargets[0].rtt = rtt[hi];
    speedTestTargets[0].url = speedTestHosts[hi];
    speedTestHostName(speedTestHosts[hi], speedTestTargets[0].host, sizeof(speedTestTargets[0].host));
    speedTestTargets[1].name = "near";
    speedTestTargets[1].rtt = rtt[lo];
    speedTestTargets[1].url = speedTestHosts[lo];
    speedTestHostName(speedTestHosts[lo], speedTestTargets[1].host, sizeof(speedTestTargets[1].host));
    debugPrintf("Speedtest: far=%s (%u ms), near=%s (%u ms)", speedTestHosts[hi], rtt[hi], speedTestHosts[lo], rtt[lo]);
    return true;
}

void speedTest(void)
{
    // writeScreenLog() shows the tail of a full list, so priming it keeps the
    // first few results from sitting alone at the top of the screen.
    clearScreenLog();
    for(int i = 0; i < MAX_LINES; ++i)
        addToScreenLog(" ");
    if(!speedTestPickTargets())
    {
        showErrorFrame("Speed test\n\nCouldn't reach two hosts to measure against.");
        return;
    }

    debugPrintf("Speedtest: %u passes over %u targets x %u configs, %d bytes or %d ms per run", SPEEDTEST_PASSES, (unsigned int)SPEEDTEST_TARGETS, (unsigned int)SPEEDTEST_CONFIGS, SPEEDTEST_BYTES, SPEEDTEST_MAX_MS);
    // The targets are known now, so their names can be measured along with the
    // rest of the columns.
    measureColumns();
    createDirRecursive(SPEEDTEST_DIR);

    // The latencies have done their job picking the targets, and both chosen
    // hosts are on the run screen anyway, so the results start from a clean log.
    clearScreenLog();
    for(int i = 0; i < MAX_LINES; ++i)
        addToScreenLog(" ");

    // Advisory, not a rejection: this is the console's own TCP handshake, which
    // reads some 10 ms above what a PC on the same cable pings, so a mirror that
    // looks too distant here may still be the closest one anybody has.
    if(speedTestTargets[1].rtt > SPEEDTEST_NEAR_RTT)
        addToScreenLog("near host is %u ms, over the %u ms it should be", speedTestTargets[1].rtt, SPEEDTEST_NEAR_RTT);

    speedTestAborted = false;
    speedTestOverride = true;
    size_t done = 0;
    for(size_t pass = 1; pass <= SPEEDTEST_PASSES && !speedTestAborted && AppRunning(true); ++pass)
        for(size_t t = 0; t < SPEEDTEST_TARGETS && !speedTestAborted && AppRunning(true); ++t)
            for(size_t c = 0; c < SPEEDTEST_CONFIGS && !speedTestAborted && AppRunning(true); ++c)
            {
                speedTestRun(pass, t, c, ++done);

                // Let the stack settle so one run does not colour the next.
                for(int i = 0; i < SPEEDTEST_PAUSE * 60 && !speedTestAborted && AppRunning(true); ++i)
                    showFrame();
            }

    speedTestOverride = false;
    dlStreams = DL_STREAMS;
    dlSlotCount = DL_SLOTS;
    removeDirectory(SPEEDTEST_DIR);
    debugPrintf("Speedtest: %s", speedTestAborted ? "aborted" : "done");
    if(speedTestAborted)
        return;

    // The results are the point of the screen, so they stay up until dismissed.
    colorStartNewFrame(SCREEN_COLOR_D_GREEN);
    textToFrame(0, ALIGNED_CENTER, "Speed test finished");
    writeScreenLogCut(1, MAX_LINES - 3);
    lineToFrame(MAX_LINES - 2, SCREEN_COLOR_WHITE);
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, localise("Press " BUTTON_A " to return"));
    drawFrame();

    while(AppRunning(true))
    {
        showFrame();
        if(vpad.trigger & (VPAD_BUTTON_A | VPAD_BUTTON_B))
            break;
    }
}
#endif

static const char *translateCurlError(CURLcode err, const char *error)
{
    switch(err)
    {
        case CURLE_COULDNT_RESOLVE_HOST:
            return "Couldn't resolve hostname";
        case CURLE_COULDNT_CONNECT:
            return "Couldn't connect to server";
        case CURLE_OPERATION_TIMEDOUT:
            return "Operation timed out";
        case CURLE_GOT_NOTHING:
            return "The server didn't return any data";
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_PARTIAL_FILE:
            return "I/O error";
        case CURLE_PEER_FAILED_VERIFICATION:
            return "Verification failed";
        case CURLE_SSL_CONNECT_ERROR:
            return "Handshake failed";
        case CURLE_FAILED_INIT:
        case CURLE_READ_ERROR:
        case CURLE_OUT_OF_MEMORY:
            return "Internal error";
        // libCURL is right here: CafeOS answered ENOPROTOOPT (92) to a WUT socket
        // call, so libCURL got an invalid argument. See issue #302.
        case CURLE_BAD_FUNCTION_ARGUMENT:
            return "Internal WUT error"; // TODO: How to handle correctly?
        default:
            return error[0] == '\0' ? curl_easy_strerror(err) : error;
    }
}

static void drawStatLine(int line, curl_off_t totalSize, curl_off_t currentSize, float bps, uint32_t *eta)
{
    if(currentSize)
    {
        float tmp = currentSize;
        tmp /= totalSize;
        barToFrame(line, 0, 29, tmp);
        // A speed at or near zero makes the quotient infinite or larger than
        // *eta can hold, and converting such a float to uint32_t is undefined.
        if(totalSize && bps > 0.0f)
        {
            float secs = (totalSize - currentSize) / bps;
            *eta = secs >= (float)UINT32_MAX ? UINT32_MAX : (uint32_t)secs;
        }
    }
    else
        barToFrame(line, 0, 29, 0.0D);

    char toScreen[256];
    humanize(currentSize, toScreen);
    char *ptr = toScreen + strlen(toScreen);
    strcpy(ptr, " / ");
    ptr += 3;
    humanize(totalSize, ptr);
    textToFrame(line, 30, toScreen);

    // UINT32_MAX means there is no usable estimate: either none has been taken
    // yet, or the transfer is too slow to put a bound on.
    if(*eta != UINT32_MAX)
    {
        secsToTime(*eta, toScreen);
        textToFrame(line, ALIGNED_RIGHT, toScreen);
    }
}

int downloadFile(const char *url, char *file, downloadData *data, FileType type, bool resume, QUEUE_DATA *queueData, RAMBUF *rambuf)
{
    // Results: 0 = OK | 1 = Error | 2 = No ticket aviable | 3 = Exit
    // Types: 0 = .app | 1 = .h3 | 2 = title.tmd | 3 = tilte.tik

    // Every retry below used to be a `return downloadFile(...)` tail call. On a long,
    // flaky download that can recurse dozens or hundreds of times (e.g. the WUT bug
    // from issue #302 repeating every few minutes), and since this function runs on
    // the caller's thread rather than a dedicated one, each "retry" left a stack frame
    // behind permanently, slowly eating that thread's stack until it overflowed -
    // silently corrupting whatever memory sat past it instead of failing cleanly.
    // Jumping back here instead reuses the same frame, so retrying never grows the stack.
    // A server that ignores Range gets one more chance as a single stream. This
    // has to live outside the retry label: `resume` used to double as "no Range
    // header", but the parallel path issues ranges of its own, so reusing it
    // would reissue the exact same requests and loop forever, truncating the
    // file on every pass.
    bool allowParallel = true;

retry:
    debugPrintf("Download URL: %s", url);
    debugPrintf("Download PATH: %s", rambuf ? "<RAM>" : file);

    char *name;
    if(rambuf)
        name = file;
    else
    {
        size_t haystack;
        for(haystack = strlen(file); file[haystack] != '/'; haystack--)
            ;
        name = file + haystack + 1;
    }

    char toScreen[FS_MAX_PATH + 64];
    void *fp;
    size_t fileSize;
    if(rambuf)
    {
        fp = (void *)open_memstream(&rambuf->buf, &rambuf->size);
        fileSize = 0;
    }
    else
    {
        if(resume && fileExists(file))
        {
            fileSize = getFilesize(file);
            if(fileSize != 0)
            {
                if(data != NULL && data->cs)
                {
                    if(fileSize == data->cs)
                    {
                        sprintf(toScreen, "Download %s skipped!", name);
                        addToScreenLog(toScreen);
                        data->dlnow += fileSize;
                        if(queueData != NULL)
                            queueData->downloaded += fileSize;

                        return 0;
                    }
                    if(fileSize > data->cs)
                    {
                        resume = false;
                        goto retry;
                    }
                }

                fp = (void *)openFile(file, "a", 0);
            }
            else
                fp = (void *)openFile(file, "w", data == NULL ? 0 : data->cs);
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
    volatile curlProgressData cdata = {
        .running = true,
        .error = CURLE_OK,
        .dlnow = 0.0D,
        .dltotal = 0.0D,
    };
    spinCreateLock((cdata.lock), SPINLOCK_FREE);

    // Only content files are worth splitting: they are the big ones, their size is
    // known up front from the TMD, and they land straight on disk rather than in
    // a RAM buffer.
    const bool parallel = allowParallel && parallelReady && !rambuf && data != NULL && data->cs != 0 && (curl_off_t)(data->cs - fileSize) >= DL_MIN_PARALLEL;

    CURLoption opt = CURLOPT_URL;
    CURLcode ret = CURLE_OK;
    if(!parallel)
        ret = curl_easy_setopt(curl, opt, url);
    if(!parallel && ret == CURLE_OK)
    {
        opt = CURLOPT_FRESH_CONNECT;
        if(curlReuseConnection)
            ret = curl_easy_setopt(curl, opt, 0L);
        else
        {
            ret = curl_easy_setopt(curl, opt, 1L);
            curlReuseConnection = true;
        }
        if(ret == CURLE_OK)
        {
            opt = CURLOPT_RESUME_FROM_LARGE;
            ret = curl_easy_setopt(curl, opt, (curl_off_t)fileSize);
            if(ret == CURLE_OK)
            {
                opt = CURLOPT_WRITEFUNCTION;
#pragma GCC diagnostic ignored "-Wcast-function-type"
                ret = curl_easy_setopt(curl, opt, rambuf ? fwrite : (size_t(*)(const void *, size_t, size_t, FILE *))addToIOQueue);
#pragma GCC diagnostic pop
                if(ret == CURLE_OK)
                {
                    opt = CURLOPT_WRITEDATA;
                    ret = curl_easy_setopt(curl, opt, (FILE *)fp);
                    if(ret == CURLE_OK)
                    {
                        opt = CURLOPT_XFERINFODATA;
                        ret = curl_easy_setopt(curl, opt, &cdata);
                    }
                }
            }
        }
    }

    if(ret != CURLE_OK)
    {
        if(rambuf)
            fclose((FILE *)fp);
        else
            addToIOQueue(NULL, 0, 0, (FSAFileHandle)fp);

        debugPrintf("curl_easy_setopt error: %s (%d / %u / %ud)", curlError, ret, opt, fileSize);
        return 1;
    }

    OSTime t = OSGetSystemTime();

    dlJob job;
    char *argv[1];
    OSThread *dlThread;
    if(parallel)
    {
        debugPrintf("Downloading %u bytes over %d streams", (unsigned int)(data->cs - fileSize), dlStreams);
        job.url = url;
        job.fp = (FSAFileHandle)fp;
        job.start = fileSize;
        job.end = data->cs;
        job.cdata = &cdata;
        argv[0] = (char *)&job;
        dlThread = startThread("NUSspli downloader", THREAD_PRIORITY_HIGH, STACKSIZE_BIG, mdlThreadMain, 1, (char *)argv, OS_THREAD_ATTRIB_AFFINITY_CPU0);
    }
    else
    {
        debugPrintf("Calling curl_easy_perform()");
        argv[0] = (char *)&cdata;
        dlThread = startThread("NUSspli downloader", THREAD_PRIORITY_HIGH, STACKSIZE_BIG, dlThreadMain, 1, (char *)argv, OS_THREAD_ATTRIB_AFFINITY_CPU0);
    }

    if(dlThread == NULL)
        return 1;

    OSTick ts;
    OSTick lastTransfair = OSGetTick();
    size_t dltotal; // We use size_t instead of curl_off_t as filesizes are limitted to 4 GB anyway,
    size_t dlnow;
    size_t downloaded = 0;
    size_t tmp;
    uint32_t fileEta = UINT32_MAX;
    float bps;
    float oldBps = 0.0D;
    int frames = 1;
    int line;
    while(cdata.running && AppRunning(true))
    {
        if(--frames == 0)
        {
            if(!spinTryLock(cdata.lock))
            {
                frames = 2;
                continue;
            }

            ts = cdata.ts;
            dltotal = cdata.dltotal;
            dlnow = cdata.dlnow;
            spinReleaseLock(cdata.lock);

            bps = dlnow - downloaded;
            downloaded = dlnow;
            dlnow += fileSize;

            // Calculate download speed
            if(bps != 0.0f)
            {
                if(dltotal)
                {
                    tmp = OSTicksToMilliseconds(ts - lastTransfair); // sample duration in milliseconds
                    if(tmp)
                    {
                        bps *= 1000.0f; // secs to ms.
                        bps /= tmp; // byte/s

                        // Smoothing
                        bps *= 1.0f - SMOOTHING_FACTOR;
                        oldBps *= SMOOTHING_FACTOR;
                        bps += oldBps;
                        oldBps = bps;
                    }
                    else
                        bps = 0.0f;
                }
                else
                    bps = 0.0f;
            }

            lastTransfair = ts;
            startNewFrame();

            if(data != NULL)
            {
                if(queueData != NULL)
                {
                    sprintf(toScreen, "%s (%d/%d)", data->name, queueData->current, queueData->packages);
                    line = textToFrameMultiline(0, ALIGNED_CENTER, toScreen, MAX_CHARS);
                }
                else
                    line = textToFrameMultiline(0, ALIGNED_CENTER, data->name, MAX_CHARS);

                drawStatLine(line++, data->dltotal, data->dlnow + dlnow, bps, &data->eta);

                if(queueData != NULL)
                    drawStatLine(line++, queueData->dlSize, queueData->downloaded + dlnow, bps, &queueData->eta);

                lineToFrame(line++, SCREEN_COLOR_WHITE);

                sprintf(toScreen, "(%d/%d)", data->dcontent + 1, data->contents);
                textToFrame(line, ALIGNED_CENTER, toScreen);
            }
            else
                line = 0;

            if(dltotal)
            {
                if(!rambuf)
                    checkForQueueErrors();

                frames = 60;
                dltotal += fileSize;

                strcpy(toScreen, localise("Downloading"));
                strcat(toScreen, " ");
                strcat(toScreen, name);
                textToFrame(line, 0, toScreen);

                getSpeedString(bps, toScreen);
                textToFrame(line, ALIGNED_RIGHT, toScreen);

                drawStatLine(++line, dltotal, dlnow, bps, &fileEta);
            }
            else
            {
                frames = 1;
                strcpy(toScreen, localise("Preparing"));
                strcat(toScreen, " ");
                strcat(toScreen, name);
                textToFrame(line++, 0, toScreen);
            }

            writeScreenLog(++line);
            drawFrame();
        }

        showFrame();

        if(cancelOverlay == NULL)
        {
            if(vpad.trigger & VPAD_BUTTON_B)
            {
                strcpy(toScreen, localise("Do you really want to cancel?"));
                strcat(toScreen, "\n\n" BUTTON_A " ");
                strcat(toScreen, localise("Yes"));
                strcat(toScreen, " || " BUTTON_B " ");
                strcat(toScreen, localise("No"));
                cancelOverlay = addErrorOverlay(toScreen);
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
    if(data == NULL && cancelOverlay != NULL)
        closeCancelOverlay();

    debugPrintf("curl_easy_perform() returned: %d", ret);

    if(rambuf)
        fclose((FILE *)fp);
    else
        addToIOQueue(NULL, 0, 0, (FSAFileHandle)fp);

    if(!AppRunning(true))
        return 1;

    if(ret != CURLE_OK)
    {
        debugPrintf("curl_easy_perform returned an error: %s (%d/%d)\nFile: %s", curlError, ret, cdata.error, rambuf ? "<RAM>" : file);

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

        // Whatever went wrong, the connection libCURL has cached is not trustworthy
        // anymore. This matters most for CURLE_BAD_FUNCTION_ARGUMENT: CafeOS kills
        // the socket behind libCURLs back ("Received request to kill all sockets"),
        // select() then fails with ENOPROTOOPT and libCURL reports an unrecoverable
        // poll. Retrying on that very same socket just reproduces the error, so make
        // sure the next attempt does a fresh connect.
        curlReuseConnection = false;

        const char *te = translateCurlError(ret, curlError);
        switch(ret)
        {
            case CURLE_RANGE_ERROR:
                if(rambuf && rambuf->buf)
                {
                    MEMFreeToDefaultHeap(rambuf->buf);
                    rambuf->buf = NULL;
                    rambuf->size = 0;
                }

                if(parallel)
                    // The ranges were ours, not a resume offset, so the server's
                    // answer says nothing about whether it can resume. Chunks are
                    // written in order, so what is on disk is a valid prefix -
                    // throwing gigabytes away over one chunk's transient 503 would
                    // be far worse than retrying on a single stream.
                    allowParallel = false;
                else
                    resume = false; // Sequential path failed too: no Range support.

                // The close command is queued, not done: retrying before it lands
                // would size the file short and resume from the wrong offset.
                flushIOQueue();
                goto retry;
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_CONNECT:
            case CURLE_OPERATION_TIMEDOUT:
            case CURLE_GOT_NOTHING:
            case CURLE_SEND_ERROR:
            case CURLE_RECV_ERROR:
            case CURLE_PARTIAL_FILE:
                sprintf(toScreen, "%s:\n\t%s\n\n%s", "Network error", te, "check the network settings and try again");
                break;
            case CURLE_BAD_FUNCTION_ARGUMENT: // Killed socket, see above - TODO: Why did it kill the socket? "see above" is not really an answer. Also how to handle correctly?
                sprintf(toScreen, "%s:\n\t%s\n\n%s", localise("Internal WUT error"), te, "See https://github.com/V10lator/NUSspli/issues/302#issuecomment-2108134284");
                break;
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_CONNECT_ERROR:
                sprintf(toScreen, "%s:\n\t%s!\n\n%s", "SSL error", te, "check your Wii Us date and time settings");
                break;
            default:
                sprintf(toScreen, "%s:\n\t%d %s", te, ret, curlError);
                break;
        }

        if(data != NULL && cancelOverlay != NULL)
            closeCancelOverlay();

        if(showNetworkError(toScreen))
        {
            resetNetwork();
            flushIOQueue(); // We flush here so the last file is completely on disc and closed before we retry.
            goto retry;
        }

        resetNetwork();
        return 1;
    }
    debugPrintf("curl_easy_perform executed successfully");

    // The parallel path never touches the shared handle, so asking it for a
    // response code returns whatever the last sequential transfer left behind -
    // or 0 on a freshly created handle, which would read as failure and delete a
    // perfectly good file. Its chunks are already validated as 206 individually.
    long resp = 200;
    if(!parallel)
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp);
        if(resp == 206) // Resumed download OK
            resp = 200;
    }

    debugPrintf("The download returned: %u", resp);
    if(resp != 200)
    {
        if(!rambuf)
        {
            flushIOQueue();
            FSARemove(getFSAClient(), file);
        }

        if(resp == 404 && (type & FILE_TYPE_TMD) == FILE_TYPE_TMD) // Title.tmd not found
        {
            strcpy(toScreen, localise("The download of title.tmd failed with error: 404"));
            strcat(toScreen, "\n\n");
            strcat(toScreen, localise("The title cannot be found on the NUS, maybe the provided title ID doesn't exists or\nthe TMD was deleted"));
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
                {
                    if(rambuf && rambuf->buf)
                    {
                        MEMFreeToDefaultHeap(rambuf->buf);
                        rambuf->buf = NULL;
                        rambuf->size = 0;
                    }
                    goto retry;
                }
            }
            return 1;
        }
        else if(resp == 404 && (type & FILE_TYPE_TIK) == FILE_TYPE_TIK)
        { // Fake ticket needed
            return 2;
        }
        else
        {
            sprintf(toScreen, "%s: %ld\n%s: %s\n\n", localise("The download returned a result different to 200 (OK)"), resp, localise("File"), rambuf ? file : prettyDir(file));
            if(resp == 400)
            {
                strcat(toScreen, localise("Request failed. Try again"));
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
                    goto retry;
            }
            return 1;
        }
    }

    if(data != NULL)
    {
        curl_off_t dld;
        if(parallel)
            // Same reason as the response code above: the shared handle knows
            // nothing about this transfer. We asked for exactly this many bytes
            // and checked every chunk arrived, so this is what landed.
            dld = (curl_off_t)(data->cs - fileSize);
        else
        {
            ret = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &dld);
            if(ret != CURLE_OK)
                dld = 0;
        }

        if(fileSize)
            dld += fileSize;

        data->dlnow += dld;
        if(queueData != NULL)
            queueData->downloaded += dld;
    }

    sprintf(toScreen, "Download %s finished!", name);
    addToScreenLog(toScreen);
    return 0;
}

bool downloadTitle(const TMD *tmd, size_t tmdSize, const TitleEntry *titleEntry, const char *titleVer, char *folderName, bool inst, NUSDEV dlDev, bool toUSB, bool keepFiles, QUEUE_DATA *queueData)
{
    char tid[17];
    hex(tmd->tid, 16, tid);
    debugPrintf("Downloading title... tID: %s, tVer: %s, name: %s, folder: %s", tid, titleVer, titleEntry->name, folderName);

    char downloadUrl[256];
    strcpy(downloadUrl, DOWNLOAD_URL);
    strcat(downloadUrl, tid);
    strcat(downloadUrl, "/");

    if(folderName[0] == '\0')
        for(size_t i = 0; i < strlen(titleEntry->name); ++i)
            folderName[i] = isAllowedInFilename(titleEntry->name[i]) ? titleEntry->name[i] : '_';

    strcpy(folderName + strlen(titleEntry->name), " [");
    strcat(folderName, tid);
    strcat(folderName, "]");

    if(strlen(titleVer) > 0)
    {
        strcat(folderName, " v");
        strcat(folderName, titleVer);
    }

    char installDir[FS_MAX_PATH];
    strcpy(installDir, dlDev == NUSDEV_USB01 ? INSTALL_DIR_USB1 : (dlDev == NUSDEV_USB02 ? INSTALL_DIR_USB2 : (dlDev == NUSDEV_SD ? INSTALL_DIR_SD : INSTALL_DIR_MLC)));
    if(!dirExists(installDir))
    {
        debugPrintf("Creating directory \"%s\"", installDir);
        FSError err = createDirectory(installDir);
        if(err == FS_ERROR_OK)
            addToScreenLog("Install directory successfully created");
        else
        {
            showErrorFrame(translateFSErr(err));
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
        FSError err = createDirectory(installDir);
        if(err == FS_ERROR_OK)
            addToScreenLog("Download directory successfully created");
        else
        {
            showErrorFrame(translateFSErr(err));
            return false;
        }
    }
    else
        addToScreenLog("WARNING: The download directory already exists");

    char *idp = installDir + strlen(installDir);
    strcpy(idp, "title.tmd");

    FSAFileHandle fp = openFile(installDir, "w", tmdSize);
    if(fp == 0)
    {
        showErrorFrame("Can't save title.tmd file!");
        return false;
    }

    addToIOQueue(tmd, 1, tmdSize, fp);
    addToIOQueue(NULL, 0, 0, fp);
    addToScreenLog("title.tmd saved");

    char toScreen[128];
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

    downloadData data = {
        .name = titleEntry->name,
        .contents = tmd->num_contents + 1,
        .dcontent = 0,
        .dlnow = 0,
        .dltotal = 0,
        .eta = -1,
    };

    if(!fileExists(installDir))
    {
        RAMBUF *tikBuf = allocRamBuf();
        if(tikBuf == NULL)
            return false;

        data.cs = 0;
        int tikRes = downloadFile(downloadUrl, installDir, &data, FILE_TYPE_TIK | FILE_TYPE_TORAM, false, queueData, tikBuf);
        switch(tikRes)
        {
            case 2:
                if(!generateTik(installDir, tmd))
                    return false;

                addToScreenLog("Fake ticket created successfully");
                tikBuf->size = 0;
                break;
            case 0:
                fp = openFile(installDir, "w", tikBuf->size);
                if(fp == 0)
                {
                    freeRamBuf(tikBuf);
                    showErrorFrame("Can't save title.tik file!");
                    return false;
                }

                addToIOQueue(tikBuf->buf, 1, tikBuf->size, fp);
                addToIOQueue(NULL, 0, 0, fp);
                break;
            default:
                freeRamBuf(tikBuf);
                return false;
        }

        ++data.dcontent;
        strcpy(idp, "title.cert");
        if(!fileExists(installDir))
        {
            if(generateCert(tmd, (TICKET *)tikBuf->buf, tikBuf->size, installDir))
                addToScreenLog("Cert created!");
            else
            {
                freeRamBuf(tikBuf);
                return false;
            }
        }
        else
            addToScreenLog("Cert skipped!");

        freeRamBuf(tikBuf);
    }
    else
        addToScreenLog("title.tik skipped!");

    if(!AppRunning(true))
        return false;

    // Get .app and .h3 files
    curl_off_t as;
    for(int i = 0; i < tmd->num_contents; ++i)
    {
        as = tmd->contents[i].size;
        data.dltotal += as;
        if(tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED)
        {
            ++data.contents;
            data.dltotal += getH3size(as);
        }
    }

    char *dupp = dup + 8;
    char *idpp = idp + 8;
    for(int i = 0; i < tmd->num_contents && AppRunning(true); ++i)
    {
        hex(tmd->contents[i].cid, 8, dup);
        OSBlockMove(idp, dup, 8, false);
        strcpy(idpp, ".app");

        data.cs = tmd->contents[i].size;
        if(downloadFile(downloadUrl, installDir, &data, FILE_TYPE_APP, true, queueData, NULL) == 1)
            return false;

        ++data.dcontent;

        if(tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED)
        {
            strcpy(dupp, ".h3");
            strcpy(idpp, ".h3");
            data.cs = getH3size(tmd->contents[i].size);

            if(downloadFile(downloadUrl, installDir, &data, FILE_TYPE_H3, true, queueData, NULL) == 1)
                return false;

            ++data.dcontent;
        }
    }

    if(cancelOverlay != NULL)
        closeCancelOverlay();

    if(!AppRunning(true))
        return false;

    bool ret;
    if(inst)
    {
        *idp = '\0';
        ret = install(titleEntry->name, hasDependencies, dlDev, installDir, toUSB, keepFiles, tmd);
    }
    else
        ret = true;

    return ret;
}

RAMBUF *allocRamBuf()
{
    RAMBUF *ret = MEMAllocFromDefaultHeap(sizeof(RAMBUF));
    if(ret == NULL)
        return NULL;

    ret->buf = NULL;
    ret->size = 0;
    return ret;
}

void freeRamBuf(RAMBUF *rambuf)
{
    if(rambuf->buf != NULL)
        MEMFreeToDefaultHeap(rambuf->buf);

    MEMFreeToDefaultHeap(rambuf);
}
