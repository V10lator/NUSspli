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

// The parallel modes pick their stream count at run time: the Auto ramp measures
// its way up to the maximum and An starts there, so the arrays are sized for that
// largest ask in every build, not just for the benchmark.
#define DL_MAX_STREAMS 6
#define DL_MAX_SLOTS   (DL_MAX_STREAMS * 2)
// Below this the extra connections cost more than they win, and the .h3, ticket
// and TMD files are tiny to begin with.
#define DL_MIN_PARALLEL (2 * DL_CHUNKSIZE)

// One Auto-ramp phase runs long enough and moves enough bytes for its rate to be
// trusted against the gate below, and never past the cap - which is also what
// bounds a stalled single stream whose eight megabytes would never arrive.
#define RAMP_MIN_MS    5000
#define RAMP_MAX_MS    15000
#define RAMP_MIN_BYTES (8 * 1024 * 1024)
// The first count whose rate beats the single-stream yardstick by this percent
// is the one that wins the host.
#define RAMP_GATE 115

static bool initialised = false;
static CURL *curl;
static char curlError[CURL_ERROR_SIZE];
static bool curlReuseConnection = true;
static OSThread *socketPoolThread = NULL;
static bool socketPoolDonated = false;

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

// Point the engine at the stream count the next job should use: one pair of
// slots per stream, capped by the arrays sized above.
static void setStreamCount(int streams)
{
    if(streams < 1)
        streams = 1;
    if(streams > DL_MAX_STREAMS)
        streams = DL_MAX_STREAMS;

    dlStreams = streams;
    dlSlotCount = streams * 2;
}

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

    void *socketPool = MEMAllocFromDefaultHeapEx(SOCKET_POOL_SIZE, 0x40);
    if(socketPool == NULL)
    {
        debugPrintf("socketPoolThread: Out of memory");
        return -1;
    }

    // BIG_BUFFERS splits the donation 50-50 between small and big buffers instead
    // of 80-20. Receive buffers are what we are here for, so the big half is the
    // half that matters.
    int ret = somemopt(SOMEMOPT_REQUEST_INIT, socketPool, SOCKET_POOL_SIZE, SOMEMOPT_FLAGS_BIG_BUFFERS);

    // A request that never took never signals either, and initDownloader() is
    // sitting in WAIT_FOR_INIT on the thread that draws the screen.
    if(ret < 0)
        somemopt(SOMEMOPT_REQUEST_CANCEL_WAIT, NULL, 0, SOMEMOPT_FLAGS_NONE);

    MEMFreeToDefaultHeap(socketPool);

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

    socketPoolThread = startThread("NUSspli socket pool", THREAD_PRIORITY_LOW, STACKSIZE_SMALL, socketPoolThreadMain, 0, NULL, AFFINITY_CPU12);
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
#ifdef NUSSPLI_DEBUG
    int used = somemopt(SOMEMOPT_REQUEST_WAIT_FOR_INIT, NULL, 0, SOMEMOPT_FLAGS_NONE);
    debugPrintf("initSocketPool: %d bytes donated", used);
#endif
}

// All the socket options we set below are pure performance tweaks, so a failure
// is never fatal. CafeOS answers with ENOPROTOOPT (92, "Non-supported option")
// for options it doesn't know about and returning CURL_SOCKOPT_ERROR on that
// would kill the whole transfer instead of just losing the tweak.
static inline bool trySockopt(curl_socket_t socket, int level, int option, int value, const char *name)
{
    (void)name; // Only handed to debugPrintf, which compiles away in release builds
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
        debugPrintf("initSocket: SO_RCVBUF requested %d, got %d", SOCKET_BUFSIZE, got);
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
    void *ovl = addErrorOverlay(localise("Preparing. This might take some time. Please be patient."));

    // Disconnect from network. deinitDownloader() ends the socket library on its
    // way out, so there is nothing left to finish here.
    restartUdpLog1();
    deinitDownloader();
    NNResult nnres;
    BOOL con;

closeAgain:
    nnres = ACIsApplicationConnected(&con);
    if(nnres.value != 0)
        con = false;

    if(con)
    {
        ACClose();
        uint32_t timeout = 5 * 1000 / 10;
        do
        {
            nnres = ACGetCloseStatus();
            if(nnres.value == 0) // SUCCESS
                break;

            if(nnres.value == -1 || !--timeout) // FAILED
            {
                if(ovl)
                    removeErrorOverlay(ovl);

                if(showNetworkError(localise("Error closing network!")))
                {
                    if(!AppRunning(true))
                        return;

                    ovl = addErrorOverlay(localise("Preparing. This might take some time. Please be patient."));
                    goto closeAgain;
                }

                goto exitApp;
            }

            // A nnres.value of 1 means processing
            OSSleepTicks(OSMillisecondsToTicks(10));
        } while(AppRunning(true));
    }

    ACFinalize();

    // Connect to network
reconnect:
    nnres = ACInitialize();
    if(nnres.value == 0)
    {
        nnres = ACConnectAsync();
        if(nnres.value == 0)
        {
            for(uint32_t i = 10 * 1000 / 10; i && AppRunning(true); --i)
            {
                nnres = ACIsApplicationConnected(&con);
                if(nnres.value != 0)
                    con = false;

                if(con)
                {
                    socket_lib_init();
                    set_multicast_state(true);

                    restartUdpLog2();
                    initDownloader();

                    if(ovl)
                        removeErrorOverlay(ovl);

                    return;
                }

                OSSleepTicks(OSMillisecondsToTicks(10));
            }

            ACClose();
        }
    }

    if(ovl)
        removeErrorOverlay(ovl);

    if(showNetworkError(localise("Error connecting to network!")))
    {
        if(!AppRunning(true))
            return;

        ovl = addErrorOverlay(localise("Preparing. This might take some time. Please be patient."));
        ACFinalize();
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

    // Sized for the longest URL the code below can assemble:
    // "http://" + username + ':' + password + '@' + host + ':' + port + NUL, with
    // username and password clamped to the 0x20 usable bytes of their NetConf fields.
    char pUrl[sizeof("http://") + 0x80 /* host */ + 0x40 /* user and pass */ + 5 /* port */ + 3 /* ':', '@' and ':' */] = "http://";
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
                    ss = strnlen(proxy.username, 0x20); // Only 0x20 bytes usable
                    OSBlockMove(pUrl2, proxy.username, ss, false);
                    pUrl2 += ss;

                    *pUrl2 = ':';

                    ss = strnlen(proxy.password, 0x20); // Only 0x20 bytes usable
                    OSBlockMove(++pUrl2, proxy.password, ss, false);
                    pUrl2 += ss;

                    *pUrl2 = '@';
                    ++pUrl2;
                }

                ss = strnlen(proxy.host, sizeof(proxy.host));
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

            if(curl_easy_setopt(dlHandles[nextIssue], CURLOPT_URL, job->url) != CURLE_OK || curl_easy_setopt(dlHandles[nextIssue], CURLOPT_RANGE, range) != CURLE_OK || curl_easy_setopt(dlHandles[nextIssue], CURLOPT_WRITEDATA, chunk) != CURLE_OK || curl_easy_setopt(dlHandles[nextIssue], CURLOPT_XFERINFODATA, cdata) != CURLE_OK || curl_multi_add_handle(multi, dlHandles[nextIssue]) != CURLM_OK)
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
            if(addToIOQueue(chunk->buf, 1, chunk->size, job->fp) != chunk->size)
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
            // Why the socket died is never reported to the app: CafeOS destroys it
            // itself on a network hiccup (only visible as "Received request to kill
            // all sockets" in a serial log, see issue #302), so the errno above is
            // all we get. downloadFile() reconnects instead of reusing the dead
            // socket, the message there carries the issue link.
            return "Internal WUT error";
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

// Peel scheme and path off a URL to get at the host, for the Auto ramp's
// per-host pin: what six streams won on one mirror says nothing about the next.
static void urlHost(const char *url, char *out, size_t len)
{
    const char *start = strstr(url, "://");
    start = start != NULL ? start + 3 : url;

    const char *end = strchr(start, '/');
    size_t hostLen = end != NULL ? (size_t)(end - start) : strlen(start);
    if(hostLen >= len)
        hostLen = len - 1;

    memcpy(out, start, hostLen);
    out[hostLen] = '\0';
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
    const PARALLEL_MODE dlMode = getParallelMode();
    // Streams for the next attempt: 0 = resolve it fresh, then held across
    // retries and across the Auto ramp's phases.
    int want = 0;
    // -1 = no ramp in flight, 0..2 = a phase measuring, 3 = settled.
    int rampPhase = -1;
    curl_off_t rampR1 = 0;
    OSTick phaseTick = 0;
    bool phaseStop = false;
    bool userCancelled = false;
    // The Auto pin is measured once per host and kept for the session,
    // 0 = not measured yet.
    static int rampPinned = 0;
    static char rampHost[64];

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

transfer:;
    // Only content files are worth splitting: they are the big ones, their size is
    // known up front from the TMD, and they land straight on disk rather than in
    // a RAM buffer.
    const bool eligible = allowParallel && parallelReady && !rambuf && data != NULL && data->cs != 0 && (curl_off_t)(data->cs - fileSize) >= DL_MIN_PARALLEL;

    // Aus stays on one stream, An on the maximum. Auto measures which count
    // actually pays: single first as the yardstick, then the maximum, then the
    // middle - the first that beats the yardstick by RAMP_GATE keeps the host.
    if(!eligible)
        want = 1;
    else if(want == 0)
    {
        if(dlMode == PARALLEL_MODE_ON)
            want = DL_MAX_STREAMS;
        else if(dlMode == PARALLEL_MODE_AUTO)
        {
            char host[64];
            urlHost(url, host, sizeof(host));
            if(rampPinned != 0 && strcmp(host, rampHost) != 0)
            {
                debugPrintf("Ramp: host changed from %s to %s, measuring again", rampHost, host);
                rampPinned = 0;
            }

            if(rampPinned == 0)
            {
                strcpy(rampHost, host);
                rampPhase = 0;
                want = 1;
            }
            else
                want = rampPinned;
        }
        else
            want = 1;
    }

    const bool ramping = eligible && dlMode == PARALLEL_MODE_AUTO && rampPinned == 0 && rampPhase >= 0 && rampPhase <= 2;
    debugPrintf("Stream plan: mode=%s want=%d phase=%d pinned=%d", getParallelString(dlMode), want, rampPhase, rampPinned);

    const bool parallel = eligible && want > 1;

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
    phaseTick = OSGetTick();
    phaseStop = false;
    setStreamCount(want);
    if(ramping)
        debugPrintf("Ramp[phase=%d/streams=%d] begin at offset %u", rampPhase, want, (unsigned int)fileSize);

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
    {
        if(rambuf)
            fclose((FILE *)fp);
        else
            addToIOQueue(NULL, 0, 0, (FSAFileHandle)fp);

        debugPrintf("Error starting the download thread!");
        return 1;
    }

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

            if(ramping && !phaseStop)
            {
                uint32_t ms = (uint32_t)OSTicksToMilliseconds(OSGetTick() - phaseTick);
                if(ms >= RAMP_MAX_MS || (ms >= RAMP_MIN_MS && dlnow >= RAMP_MIN_BYTES))
                {
                    debugPrintf("Ramp[phase=%d] cap reached: bytes=%u ms=%u", rampPhase, (unsigned int)dlnow, ms);
                    phaseStop = true;
                    cdata.error = CURLE_ABORTED_BY_CALLBACK;
                }
            }

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
                userCancelled = true;
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

    // A ramp phase that stopped on its own cap - not an error, not the user -
    // scores the configuration just measured and hands over to the next one.
    // Whoever wins re-enters above for the rest of the file; the handle below
    // stays open across all of it.
    if(ramping && phaseStop && !userCancelled && ret == CURLE_ABORTED_BY_CALLBACK && cdata.error == CURLE_ABORTED_BY_CALLBACK && AppRunning(true))
    {
        uint32_t ms = (uint32_t)OSTicksToMilliseconds(OSGetTick() - phaseTick);
        if(ms == 0)
            ms = 1;

        curl_off_t got = cdata.dlnow;
        curl_off_t rate = got * 1000 / ms;
        debugPrintf("Ramp[phase=%d/streams=%d] result: bytes=%u ms=%u rate=%u", rampPhase, want, (unsigned int)got, ms, (unsigned int)rate);

        if(rampPhase == 0)
        {
            rampR1 = rate;
            rampPhase = 1;
            want = DL_MAX_STREAMS;
        }
        else if(rate * 100 >= rampR1 * RAMP_GATE)
        {
            rampPinned = want;
            debugPrintf("Ramp[pin=%d] beats single: %u vs %u B/s", rampPinned, (unsigned int)rate, (unsigned int)rampR1);
            rampPhase = 3;
            want = rampPinned;
        }
        else if(rampPhase == 1)
        {
            rampPhase = 2;
            want = 3;
        }
        else
        {
            rampPinned = 1;
            debugPrintf("Ramp[pin=1] single wins: %u vs %u B/s", (unsigned int)rate, (unsigned int)rampR1);
            rampPhase = 3;
            want = 1;
        }

        if((curl_off_t)fileSize + got >= (curl_off_t)data->cs)
        {
            // The phase carried the file over the line itself: nothing left to
            // re-enter for, so fall through and let the code below account for
            // it as the success it is.
            debugPrintf("Ramp: file finished inside the phase");
            ret = CURLE_OK;
            cdata.error = CURLE_OK;
        }
        else
        {
            fileSize += (size_t)got;
            cdata.running = true;
            cdata.error = CURLE_OK;
            cdata.dltotal = 0;
            cdata.dlnow = 0;
            goto transfer;
        }
    }

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

                // Ranges are what the ramp measures with: a host that refuses
                // them cannot be parallel at all, so stop re-measuring it.
                if(rampPhase >= 0 && rampPhase <= 2)
                {
                    rampPinned = 1;
                    rampPhase = 3;
                    debugPrintf("Ramp[pin=1] no Range support on this host");
                }

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
            case CURLE_BAD_FUNCTION_ARGUMENT: // The socket was killed by CafeOS behind libCURLs back (why is up to the OS, see the comment above and issue #302) - handled by reconnecting instead of reusing it
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
    {
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
                data.dltotal += tikBuf->size; // dlnow already includes the ticket bytes
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
    {
        addToScreenLog("title.tik skipped!");
        ++data.dcontent; // The ticket is already there, count it as done
    }

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
