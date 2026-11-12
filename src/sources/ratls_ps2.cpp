/*
  ratls_ps2.cpp -- the console half of the RetroAchievements HTTPS lookup and of
  Check for Updates (raHttpsDownload).

  Supplies what src/platform/ratls.c needs on a PS2: DNS and a TCP connection
  through the menu's network stack (ps2ips), a BIO that reads with lwIP's
  per-call non-blocking flag under an overall deadline (fcntl O_NONBLOCK has no
  effect on this stack, see ranet.cpp), the console clock as UTC, an entropy
  source, mbedtls_ms_time(), and the embedded roots (third_party/ca-roots,
  turned into asm/ratls_root_<name>.c by tools/assets.py embed): RA's five for
  the lookup, GitHub's four for Check for Updates.

  Runs on the I/O worker. Refuses while a UDP transport owns the NIC.
*/

#include "include/ratls.h"
#ifdef RETROACHIEVEMENTS
#include "include/ranet.h" /* raNetNicReserved, raNetTcpRecv */
#else
#include "include/opl.h"          /* gNetworkProtocol */
#include "include/bdmsupport.h"   /* bdmIsUDPBDLoaded */
#include "include/udpfssupport.h" /* udpfsGetModulesLoaded */
#endif

#include <ps2ips.h>
#include <netinet/in.h> /* sockaddr_in */
#include <lwip/def.h>   /* htons */
#include <netdb.h>
#include <kernel.h>
#include <delaythread.h>
#include <libcdvd.h>
#include <string.h>
#include <time.h>

#include "mbedtls/net_sockets.h" /* MBEDTLS_ERR_NET_SEND_FAILED */
#include "mbedtls/platform_time.h"
#include "mbedtls/ssl.h"

/* <netdb.h> only declares struct hostent, and <tcpip.h>, which defines it, clashes with
   <netinet/in.h>. This is its layout there (lwIP's LWIP_DNS_API_DECLARE_STRUCTS). */
struct hostent
{
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

constexpr int RATLS_DEADLINE_MS = 8000; /* the same budget as the xeRAbora HTTP fetch (RA_HTTP_MS) */
constexpr int RATLS_POLL_MS = 25;
constexpr int RATLS_MIN_YEAR = 2025; /* an earlier RTC is unset: fail closed */

constexpr int RATLS_IDLE_MS = 15000; /* raHttpsDownload: no data for this long ends it */

extern "C" {
#ifdef RETROACHIEVEMENTS
extern unsigned char ratls_root_gtsr1[], ratls_root_gtsr3[], ratls_root_gtsr4[];
extern unsigned int size_ratls_root_gtsr1, size_ratls_root_gtsr3, size_ratls_root_gtsr4;
#endif
extern unsigned char ratls_root_isrgrootx1[], ratls_root_isrgrootx2[];
extern unsigned int size_ratls_root_isrgrootx1, size_ratls_root_isrgrootx2;
#ifdef RIPTOPL_UPDATER
extern unsigned char ratls_root_sectigoe46[], ratls_root_sectigor46[];
extern unsigned int size_ratls_root_sectigoe46, size_ratls_root_sectigor46;
#endif
mbedtls_ms_time_t mbedtls_ms_time(void);
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen);
}

static int bcd(unsigned char v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

static long long ratlsNow(void)
{
    sceCdCLOCK clk;

    if (sceCdReadClock(&clk) == 0)
        return -1;
    int year = 2000 + bcd(clk.year);
    if (year < RATLS_MIN_YEAR)
        return -1;
    return raTlsRtcToUtc(year, bcd(clk.month & 0x1F), bcd(clk.day), bcd(clk.hour), bcd(clk.minute), bcd(clk.second));
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return static_cast<mbedtls_ms_time_t>(clock()) * 1000 / CLOCKS_PER_SEC;
}

static inline unsigned int cop0Count(void)
{
    unsigned int count;
    __asm__ volatile("mfc0 %0, $9"
                     : "=r"(count));
    return count;
}

/* The PS2 has no hardware RNG. Each output byte folds 8 samples of the
   294.912 MHz COP0 count's low bits across a thread switch (interrupt and DMA
   timing make them jitter), credited at 1 bit per sample; the CDVD clock and
   clock() are mixed in once per call. mbedTLS's entropy accumulator gathers
   until the CTR_DRBG seed reaches its threshold (spec: >= 512 samples, about
   256 bits by this estimate). */
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen)
{
    sceCdCLOCK clk;

    (void)data;
    for (size_t i = 0; i < len; i++) {
        unsigned char byte = 0;
        for (int s = 0; s < 8; s++) {
            unsigned int before = cop0Count();
            DelayThread(1);
            byte = static_cast<unsigned char>((byte << 1) | (byte >> 7)) ^ static_cast<unsigned char>(cop0Count() - before);
        }
        out[i] = byte ^ static_cast<unsigned char>(clock());
    }
    if (len >= sizeof(clk) && sceCdReadClock(&clk) != 0) {
        const unsigned char *c = reinterpret_cast<const unsigned char *>(&clk);
        for (size_t i = 0; i < sizeof(clk); i++)
            out[i] ^= c[i];
    }
    *olen = len;
    return 0;
}

struct RatlsBio
{
    int sock;
    clock_t start; /* the budget runs from before DNS */
    int idle;      /* raHttpsDownload: the budget restarts at every received piece */
};

#ifdef RETROACHIEVEMENTS
static int ratlsNicReserved(void)
{
    return raNetNicReserved();
}

static int ratlsTcpRecv(int sock, void *aligned, int max)
{
    return raNetTcpRecv(sock, aligned, max);
}
#else
/* Ranet::nicReserved (ranet.cpp, RA only): a UDP transport owns the NIC, or is the protocol chosen. */
static int ratlsNicReserved(void)
{
    return bdmIsUDPBDLoaded() || udpfsGetModulesLoaded() || gNetworkProtocol == NET_PROTO_UDPFS ||
           gNetworkProtocol == NET_PROTO_UDPFSBD || gNetworkProtocol == NET_PROTO_UDPBD;
}

/* Ranet::tcpRecv (ranet.cpp, RA only): peek, then take a length ps2ips copies whole. */
static int ratlsTcpRecv(int sock, void *aligned, int max)
{
    int got = recv(sock, aligned, max, 0x08 | 0x01); /* MSG_DONTWAIT | MSG_PEEK, lwIP's values */
    if (got <= 0)
        return got;
    if (got > 64 && got % 64 > 48)
        got -= got % 64 - 48;
    return recv(sock, aligned, got, 0x08);
}
#endif

/* clock() is a 32-bit microsecond counter here: compare elapsed time, which survives the wrap,
   and keep the product small (milliseconds times a full CLOCKS_PER_SEC would overflow). */
static int ratlsExpired(clock_t start, int ms = RATLS_DEADLINE_MS)
{
    return (clock_t)(clock() - start) >= (clock_t)ms * (CLOCKS_PER_SEC / 1000);
}

static int bioSend(void *ctx, const unsigned char *buf, size_t len)
{
    RatlsBio *bio = static_cast<RatlsBio *>(ctx);
    int n = send(bio->sock, const_cast<unsigned char *>(buf), len, 0);
    return n < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : n;
}

/* Reads through a 64-byte-aligned bounce buffer with ratlsTcpRecv (sized so ps2ips never drops an
   unaligned tail), polling until data, close or the deadline. */
static int bioRecv(void *ctx, unsigned char *buf, size_t len)
{
    static unsigned char chunk[960] __attribute__((aligned(64)));
    RatlsBio *bio = static_cast<RatlsBio *>(ctx);

    for (;;) {
        if (bio->idle ? ratlsExpired(bio->start, RATLS_IDLE_MS) : ratlsExpired(bio->start))
            return MBEDTLS_ERR_SSL_TIMEOUT;
        int got = ratlsTcpRecv(bio->sock, chunk, len < sizeof(chunk) ? static_cast<int>(len) : static_cast<int>(sizeof(chunk)));
        if (got == 0)
            return 0; /* closed */
        if (got > 0) {
            memcpy(buf, chunk, got);
            if (bio->idle)
                bio->start = clock();
            return got;
        }
        DelayThread(RATLS_POLL_MS * 1000);
    }
}

#ifdef RETROACHIEVEMENTS
int raHttpsGet(const char *host, const char *path, char *buf, int max, int *length)
{
    const ratls_anchor_t anchors[] = {
        {ratls_root_gtsr1, size_ratls_root_gtsr1},
        {ratls_root_gtsr3, size_ratls_root_gtsr3},
        {ratls_root_gtsr4, size_ratls_root_gtsr4},
        {ratls_root_isrgrootx1, size_ratls_root_isrgrootx1},
        {ratls_root_isrgrootx2, size_ratls_root_isrgrootx2},
    };
    struct sockaddr_in to;
    clock_t start = clock();

    if (length != NULL)
        *length = 0;
    if (!raTlsClockOk(ratlsNow)) /* before any DNS lookup or connection */
        return RATLS_ERR_CLOCK;
    if (raNetNicReserved())
        return RATLS_ERR_NET;

    struct hostent *he = gethostbyname(host);
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
        return RATLS_ERR_NET;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(443);
    memcpy(&to.sin_addr, he->h_addr_list[0], sizeof(to.sin_addr));

    RatlsBio bio = {socket(AF_INET, SOCK_STREAM, 0), start, 0};
    if (bio.sock < 0)
        return RATLS_ERR_NET;
    if (connect(bio.sock, reinterpret_cast<struct sockaddr *>(&to), sizeof(to)) < 0) {
        disconnect(bio.sock);
        return RATLS_ERR_NET;
    }
    if (ratlsExpired(start)) { /* DNS and connect already used the budget */
        disconnect(bio.sock);
        return RATLS_ERR_TIMEOUT;
    }
    const ratls_io_t io = {bioSend, bioRecv, &bio, ratlsNow};

    int rc = raTlsRequest(&io, host, path, "RiptOPL/" OPL_VERSION, anchors, sizeof(anchors) / sizeof(anchors[0]), buf, max, length);
    disconnect(bio.sock);
    return rc;
}
#endif

#ifdef RIPTOPL_UPDATER
extern "C" int ratlsNicBusy(void)
{
    return ratlsNicReserved();
}

int raHttpsDownload(const char *host, const char *path, char *buf, int max, int max_body, ratls_sink_t sink,
                    void *sink_ctx, char *location, int location_size)
{
    const ratls_anchor_t anchors[] = {
        {ratls_root_sectigoe46, size_ratls_root_sectigoe46}, /* github.com */
        {ratls_root_sectigor46, size_ratls_root_sectigor46},
        {ratls_root_isrgrootx1, size_ratls_root_isrgrootx1}, /* the release download host */
        {ratls_root_isrgrootx2, size_ratls_root_isrgrootx2},
    };
    struct sockaddr_in to;
    clock_t start = clock();

    if (location != NULL && location_size > 0)
        location[0] = '\0';
    if (!raTlsClockOk(ratlsNow)) /* before any DNS lookup or connection */
        return RATLS_ERR_CLOCK;
    if (ratlsNicReserved())
        return RATLS_ERR_NET;

    struct hostent *he = gethostbyname(host);
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
        return RATLS_ERR_NET;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(443);
    memcpy(&to.sin_addr, he->h_addr_list[0], sizeof(to.sin_addr));

    RatlsBio bio = {socket(AF_INET, SOCK_STREAM, 0), start, 0};
    if (bio.sock < 0)
        return RATLS_ERR_NET;
    if (connect(bio.sock, reinterpret_cast<struct sockaddr *>(&to), sizeof(to)) < 0) {
        disconnect(bio.sock);
        return RATLS_ERR_NET;
    }
    if (ratlsExpired(start)) { /* DNS and connect already used the 8 s budget */
        disconnect(bio.sock);
        return RATLS_ERR_TIMEOUT;
    }
    bio.idle = 1; /* from here, only a stall ends it */
    bio.start = clock();
    const ratls_io_t io = {bioSend, bioRecv, &bio, ratlsNow};

    int rc = raTlsDownload(&io, host, path, "RiptOPL/" OPL_VERSION, anchors, sizeof(anchors) / sizeof(anchors[0]), buf, max,
                           max_body, sink, sink_ctx, location, location_size);
    disconnect(bio.sock);
    return rc;
}
#endif
