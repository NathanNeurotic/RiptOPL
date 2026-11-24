/* Verified HTTPS: the RetroAchievements edition's game-ID lookup and Check for Updates. See src/platform/ratls.c
   (portable TLS + HTTP over callbacks, C so the host test builds it) and src/sources/ratls_ps2.cpp
   (DNS, TCP, clock, entropy, roots). */
#ifndef __RATLS_H__
#define __RATLS_H__

#include <stddef.h>

/* The configuration the mbedtls library target is built with (tools/build_edition.py). */
#ifndef MBEDTLS_CONFIG_FILE
#if defined(RIPTOPL_UPDATER) && !defined(RETROACHIEVEMENTS)
#define MBEDTLS_CONFIG_FILE "riptopl_mbedtls_slim_config.h" /* Check for Updates only: TLS 1.2 */
#else
#define MBEDTLS_CONFIG_FILE "riptopl_mbedtls_config.h"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RATLS_ERR_NET = -1,      /* no network, DNS failed, or connect refused */
    RATLS_ERR_CLOCK = -2,    /* no usable time (clock unset or unreadable, none given): nothing was sent */
    RATLS_ERR_TLS = -3,      /* handshake or record failure */
    RATLS_ERR_CERT = -4,     /* certificate invalid, expired, untrusted or for another host */
    RATLS_ERR_HTTP = -5,     /* not 200, malformed, or larger than the buffer */
    RATLS_ERR_TIMEOUT = -6,  /* the deadline passed */
    RATLS_ERR_MEM = -7,      /* out of memory, or the crypto library failed to start */
    RATLS_ERR_ARG = -8,      /* bad arguments */
    RATLS_ERR_REDIRECT = -9, /* raTlsDownload: the server redirected; the target is in location */
    RATLS_ERR_SINK = -10     /* raTlsDownload: the sink refused the data (a write failed) */
};

/* Receives each piece of a download's body in order. 0 to go on, anything else stops the download. */
typedef int (*ratls_sink_t)(void *ctx, const char *data, int len);

typedef struct
{
    int (*send)(void *ctx, const unsigned char *buf, size_t len); /* mbedTLS BIO contract */
    int (*recv)(void *ctx, unsigned char *buf, size_t len);       /* MBEDTLS_ERR_SSL_TIMEOUT at the deadline, 0 = closed */
    void *ctx;
    long long (*now_utc)(void); /* seconds since 1970 UTC, -1 = clock unset */
} ratls_io_t;

typedef struct
{
    const unsigned char *der;
    size_t len;
} ratls_anchor_t;

/* 1 when now_utc() gives a usable time. Callers check it before any DNS or connection. */
int raTlsClockOk(long long (*now_utc)(void));

/* One HTTPS GET over an already-connected io. Returns the body offset in buf (>= 0), with the
   total bytes in buf (headers + body, NUL-terminated) in *length, or RATLS_ERR_*. */
int raTlsRequest(const ratls_io_t *io, const char *host, const char *path, const char *user_agent,
                 const ratls_anchor_t *anchors, int anchor_count, char *buf, int max, int *length);

/* One streamed HTTPS GET over an already-connected io, for Check for Updates. buf (max >= 512) holds
   the request and then the headers, so it must hold both; the body goes to sink as it arrives. Only a
   200 with a Content-Length of 1..max_body and no transfer or content encoding is taken. Returns the
   body length, RATLS_ERR_REDIRECT with the Location target in location (301/302/303/307/308), or
   RATLS_ERR_*. Certificates are verified exactly as for raTlsRequest. */
/* The Content-Length of the body raTlsDownload last accepted, set before the first sink call (a progress bar total). */
extern int raTlsBodyLength;
int raTlsDownload(const ratls_io_t *io, const char *host, const char *path, const char *user_agent,
                  const ratls_anchor_t *anchors, int anchor_count, char *buf, int max, int max_body,
                  ratls_sink_t sink, void *sink_ctx, char *location, int location_size);

/* The console RTC (already decoded from BCD, full year) to seconds since 1970 UTC. */
long long raTlsRtcToUtc(int year, int month, int day, int hour, int minute, int second);

/* The 8 bytes of a clock reply (sceCdCLOCK: stat, second, minute, hour, pad, day, month, year; BCD,
   Japan time) to seconds since 1970 UTC, or -1 when they are not a date from min_year on. */
long long raTlsClockDecode(const unsigned char raw[8], int min_year);

/* The console clock, read so that a busy drive is not taken for an unset clock.
   read: one clock read into raw (zeroed by the caller); its result, 0 = the command was not taken.
   rest: a short wait between tries. ticks: a free-running counter that wraps at 2^32. */
typedef struct
{
    int (*read)(unsigned char raw[8]);
    void (*rest)(void);
    unsigned int (*ticks)(void);
    unsigned int per_second; /* ticks per second */
    int min_year;            /* an earlier date is an unset clock */
} ratls_clock_ops_t;

typedef struct
{
    int known;            /* base holds a time */
    int resting;          /* the clock gave nothing usable at rest_at */
    long long base;       /* seconds since 1970 UTC at base_at */
    unsigned int base_at; /* ticks */
    unsigned int rest_at; /* ticks */
} ratls_clock_t;

enum {
    RATLS_CLOCK_TRIES = 6,   /* reads before the clock counts as unusable */
    RATLS_CLOCK_REST_S = 30, /* with a time already known, an unusable clock is not asked again for this long */
};

/* Seconds since 1970 UTC, or -1. A read the drive did not take is tried again; a time learnt earlier
   (a good read, or raTlsClockSet) carries on from the counter when the clock gives nothing usable. */
long long raTlsClockNow(ratls_clock_t *c, const ratls_clock_ops_t *ops);

/* A time from elsewhere (a web server's Date header), used while the clock gives nothing usable. */
void raTlsClockSet(ratls_clock_t *c, const ratls_clock_ops_t *ops, long long utc);

/* mbedTLS's reasons (MBEDTLS_X509_BADCERT_*) for the last RATLS_ERR_CERT. */
extern unsigned int raTlsVerifyFlags;

/* PS2: clock check, DNS, TCP and raTlsRequest with the five embedded roots, 8 s deadline.
   Refuses while a UDP transport owns the NIC. Same return convention as raTlsRequest. */
int raHttpsGet(const char *host, const char *path, char *buf, int max, int *length);

/* PS2: raTlsDownload to host:443 with GitHub's roots (Sectigo E46/R46, ISRG X1/X2). The deadline is
   idle time: 15 s without data, so a long download over a slow card is not cut off. Refuses while a
   UDP transport owns the NIC. Same return convention as raTlsDownload. */
int raHttpsDownload(const char *host, const char *path, char *buf, int max, int max_body, ratls_sink_t sink,
                    void *sink_ctx, char *location, int location_size);

/* PS2: 1 while a UDP transport owns (or is chosen for) the NIC, so no HTTPS request may start. */
int ratlsNicBusy(void);

/* PS2: the time to check certificates against while the console clock gives nothing usable
   (seconds since 1970 UTC). A usable console clock always comes first. */
void raHttpsSetTime(long long utc);

#ifdef __cplusplus
}
#endif

#endif
