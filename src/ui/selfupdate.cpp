/*
  selfupdate.cpp -- Check for Updates, from the main menu.

  Asks this build's own release channel (RIPTOPL_UPDATE_TAG, tools/build_edition.py update_channel)
  for its SHA256SUMS.txt over verified HTTPS (raHttpsDownload), and offers the release's ELF only when
  its build number is newer. Nothing is fetched until the user picks the menu entry, and nothing is
  downloaded until they accept the offer.

  The download is held in RAM. It must match the release's SHA-256 and look like a MIPS ELF before
  anything on the device is touched. Then the booted ELF is overwritten in place under its own name:
  no second file, no rename, no backup, so a memory card needs no spare room. If that write fails
  part-way the ELF may be damaged; the message gives the step and the error. Network and card work
  run on the I/O worker.
*/

#include "include/opl.h"
#include "include/oplint.h" /* gBootElfName */
#include "include/gui.h"
#include "include/lang.h"
#include "include/ioman.h"
#include "include/netsupport.h"
#include "include/ethsupport.h"
#include "include/ratls.h"
#include "include/guiwait.h"
#include "include/selfupdate.h"

#include "mbedtls/sha256.h"

#include <errno.h>
#include <libmc.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <delaythread.h>

/* This build's channel, from tools/build_edition.py update_defines. */
#if defined(RIPTOPL_UPDATE_TAG_KORIUM)
#define RIPTOPL_UPDATE_TAG "rolling-korium"
#elif defined(RIPTOPL_UPDATE_TAG_INCEBTION)
#define RIPTOPL_UPDATE_TAG "rolling-incebtion"
#elif defined(RIPTOPL_UPDATE_TAG_RA)
#define RIPTOPL_UPDATE_TAG "rolling-ra"
#elif defined(RIPTOPL_UPDATE_TAG_LLVM)
#define RIPTOPL_UPDATE_TAG "rolling-llvm"
#else
#error "Check for Updates needs a RIPTOPL_UPDATE_TAG_* define (tools/build_edition.py)"
#endif
#if defined(RIPTOPL_UPDATE_ELF_PLAIN)
#define RIPTOPL_UPDATE_ELF "RIPTOPL.ELF"
#elif defined(RIPTOPL_UPDATE_ELF_RA)
#define RIPTOPL_UPDATE_ELF "RIPTOPL-RA.ELF"
#elif defined(RIPTOPL_UPDATE_ELF_RA_INCEBTION)
#define RIPTOPL_UPDATE_ELF "RIPTOPL-RA-IncebtIon.ELF"
#else
#error "Check for Updates needs a RIPTOPL_UPDATE_ELF_* define (tools/build_edition.py)"
#endif


enum {
    SU_OK = 0,
    SU_NEWEST,
    SU_ERR_NETBUSY,
    SU_ERR_NETWORK,
    SU_ERR_CLOCK,
    SU_ERR_GITHUB,
    SU_ERR_WRITE,
    SU_ERR_VERIFY,
    SU_ERR_SWAP,
    SU_ERR_SPACE, /* the card cannot hold the new ELF: nothing was changed */
    SU_ERR_MEMORY,
};

static const int SU_BUF = 16 * 1024;           /* request, headers and each piece of the body */
static const int SU_LOCATION = 2048;           /* GitHub's signed download redirects run to about 900 bytes */
static const int SU_SUMS_MAX = 8 * 1024;       /* SHA256SUMS.txt is about 1.6 KB */
static const int SU_ELF_MAX = 8 * 1024 * 1024; /* RIPTOPL.ELF is about 2.3 MB */
static const int SU_MC_NAME_MAX = 31;          /* a memory card entry name */

static int suBusy;
static int suResult;
static int suError;        /* the RATLS_ERR_* behind SU_ERR_GITHUB */
static int suRemoteRev;    /* the release's build number */
static int suInstalledRev; /* a build installed this session: the running ELF is older than the file */
static unsigned char suSha[32];
static char suMessage[96]; /* the wait screen's text, rewritten by the I/O worker as the download runs */

struct SuBuffers
{
    char buf[SU_BUF];
    char location[2][SU_LOCATION];
};

struct SuSums
{
    char *text;
    int len;
};

struct SuFile
{
    unsigned char *data; /* the download, sized from Content-Length */
    int cap;
    int written;
    int lastKb;
    int headLen; /* the first bytes, held until the ELF header can be checked */
    unsigned char head[20];
    mbedtls_sha256_context sha;
};

static int suIsMc(const char *path)
{
    return !strncmp(path, "mc", 2) && (path[2] == '0' || path[2] == '1') && path[3] == ':';
}

/* The boot ELF can be replaced in place only on a memory card or a BDM (USB/exFAT) drive. */
static int suPlaceOk(void)
{
    if (gBootDir[0] == '\0' || gBootElfName[0] == '\0')
        return 0;
    if (suIsMc(gBootDir))
        return strlen(gBootElfName) + 4 <= (size_t)SU_MC_NAME_MAX;
    return !strncmp(gBootDir, "mass", 4) && gBootDir[4] >= '0' && gBootDir[4] <= '9';
}

static int suFromTls(int rc)
{
    suError = rc;
    if (rc == RATLS_ERR_CLOCK)
        return SU_ERR_CLOCK;
    if (rc == RATLS_ERR_MEM)
        return SU_ERR_MEMORY;
    if (rc == RATLS_ERR_SINK)
        return SU_ERR_WRITE;
    return SU_ERR_GITHUB;
}

/* Follows up to three redirects, to GitHub's hosts only. The body length, or RATLS_ERR_*. */
static int suFetch(SuBuffers *b, const char *path, int max_body, ratls_sink_t sink, void *ctx)
{
    char host[SU_HOST_MAX] = "github.com";
    int hop, rc = RATLS_ERR_HTTP;

    for (hop = 0; hop < 4; hop++) {
        char *location = b->location[hop & 1]; /* path may point into the other one */
        rc = raHttpsDownload(host, path, b->buf, SU_BUF, max_body, sink, ctx, location, SU_LOCATION);
        if (rc != RATLS_ERR_REDIRECT)
            return rc;
        if ((path = suSplitUrl(location, host, sizeof(host))) == NULL)
            return RATLS_ERR_HTTP; /* a redirect anywhere but GitHub is never followed */
    }
    return RATLS_ERR_HTTP;
}

static int suSumsSink(void *ctx, const char *data, int len)
{
    SuSums *s = static_cast<SuSums *>(ctx);
    if (s->len + len > SU_SUMS_MAX)
        return -1;
    memcpy(s->text + s->len, data, len);
    s->len += len;
    return 0;
}

static int suFileSink(void *ctx, const char *data, int len)
{
    SuFile *f = static_cast<SuFile *>(ctx);
    if (f->written == 0) { /* hold the header until it is whole: the first piece can be short */
        int take = len < (int)sizeof(f->head) - f->headLen ? len : (int)sizeof(f->head) - f->headLen;
        memcpy(f->head + f->headLen, data, take);
        f->headLen += take;
        data += take;
        len -= take;
        if (f->headLen < (int)sizeof(f->head))
            return 0;
        if (!suLooksLikeElf(f->head, f->headLen))
            return -1; /* not an ELF: stop before writing anything */
        if (raTlsBodyLength <= 0 || raTlsBodyLength > SU_ELF_MAX || (f->data = static_cast<unsigned char *>(malloc(raTlsBodyLength))) == NULL)
            return -1;
        f->cap = raTlsBodyLength;
        memcpy(f->data, f->head, f->headLen);
        mbedtls_sha256_update(&f->sha, f->head, f->headLen);
        f->written = f->headLen;
        if (len == 0)
            return 0;
    }
    if (f->written + len > f->cap)
        return -1;
    memcpy(f->data + f->written, data, len);
    mbedtls_sha256_update(&f->sha, reinterpret_cast<const unsigned char *>(data), len);
    f->written += len;
    if (f->written / 1024 - f->lastKb >= 64) {
        f->lastKb = f->written / 1024;
        snprintf(suMessage, sizeof(suMessage), _l(_STR_UPDATE_DOWNLOADING), f->lastKb);
    }
    guiWaitBarTotal = raTlsBodyLength;
    guiWaitBarDone = f->written;
    return 0;
}

/* The network, raised the way the RA lookup raises it: the Network device's stack (SMB or NFS) with the
   saved settings applied, a link and an address. Loading the modules alone is not enough: ps2ip starts
   with a fixed 192.168.0.80 and no DNS server until the settings (DHCP or static, with DNS) are applied. */
static int suNetworkUp(void)
{
    u8 ip[4], mask[4], gw[4];
    int i;

    if (ratlsNicBusy())
        return SU_ERR_NETBUSY;
    if (ethLoadInitModules() != 0)
        return SU_ERR_NETWORK;
    for (i = 0; i < 40; i++) { /* a DHCP lease can take a few seconds */
        if (ethGetNetConfig(ip, mask, gw) >= 0 && (ip[0] | ip[1] | ip[2] | ip[3]) != 0)
            return SU_OK;
        DelayThread(250 * 1000);
    }
    return SU_ERR_NETWORK;
}

static void suCheckWorker(void)
{
    SuBuffers *b = static_cast<SuBuffers *>(malloc(sizeof(SuBuffers)));
    SuSums sums = {static_cast<char *>(malloc(SU_SUMS_MAX)), 0};

    suResult = SU_ERR_MEMORY;
    if (b != NULL && sums.text != NULL && (suResult = suNetworkUp()) == SU_OK) {
        int rc = suFetch(b, "/NathanNeurotic/RiptOPL/releases/download/" RIPTOPL_UPDATE_TAG "/SHA256SUMS.txt", SU_SUMS_MAX,
                         suSumsSink, &sums);
        if (rc < 0)
            suResult = suFromTls(rc);
        else if (suParseSums(sums.text, sums.len, RIPTOPL_UPDATE_ELF, suSha, &suRemoteRev) != 0)
            suResult = suFromTls(RATLS_ERR_HTTP);
        else
            suResult = suRemoteRev > suRevision(OPL_VERSION) && suRemoteRev > suInstalledRev ? SU_OK : SU_NEWEST;
    }
    free(sums.text);
    free(b);
    suBusy = 0;
}

static int suStep, suErrno; /* which write step failed and why, shown after "Could not replace the ELF" */

/* A card allocates in 1 KB clusters. The new ELF has to fit in the free clusters plus the old ELF's own,
   with one to spare; a card that cannot answer is left to the write to report. */
static bool suCardFits(const char *path, int newSize)
{
    int type = 0, freeClusters = -1, format = 0, r = -1, old = 0;
    int fd = open(path, O_RDONLY);

    if (fd >= 0) {
        old = lseek(fd, 0, SEEK_END);
        close(fd);
        if (old < 0)
            old = 0;
    }
    if (mcGetInfo(path[2] - '0', 0, &type, &freeClusters, &format) < 0)
        return true;
    mcSync(0, NULL, &r); // mcGetInfo is async; the values are good after the sync
    if (freeClusters < 0)
        return true;
    return freeClusters + (old + 1023) / 1024 >= (newSize + 1023) / 1024 + 1;
}

static void suInstallWorker(void)
{
    SuBuffers *b = static_cast<SuBuffers *>(malloc(sizeof(SuBuffers)));
    char cur[256];
    unsigned char sha[32];
    SuFile f;
    int fd, off;

    suResult = SU_ERR_MEMORY;
    if (b == NULL)
        goto done;
    suStep = 0;
    suErrno = 0;
    suResult = SU_ERR_SWAP;
    if (suJoin(cur, sizeof(cur), gBootDir, gBootElfName, ""))
        goto done;
    if (suIsMc(cur) && cur[4] == '/') /* mcman's own form: "mc0:BOOT/FILE", no slash after the colon */
        memmove(cur + 4, cur + 5, strlen(cur + 5) + 1);

    f.data = NULL;
    f.cap = 0;
    f.written = 0;
    f.lastKb = 0;
    guiWaitBarDone = 0;
    f.headLen = 0;
    mbedtls_sha256_init(&f.sha);
    mbedtls_sha256_starts(&f.sha, 0);
    {
        int rc = suFetch(b, "/NathanNeurotic/RiptOPL/releases/download/" RIPTOPL_UPDATE_TAG "/" RIPTOPL_UPDATE_ELF, SU_ELF_MAX,
                         suFileSink, &f);
        mbedtls_sha256_finish(&f.sha, sha);
        mbedtls_sha256_free(&f.sha);
        if (rc >= 0 && f.written == 0)
            rc = RATLS_ERR_SINK; /* shorter than an ELF header */
        if (rc < 0) {
            free(f.data);
            suResult = suFromTls(rc);
            goto done;
        }
    }
    if (f.written != f.cap || memcmp(sha, suSha, sizeof(sha))) {
        free(f.data);
        suResult = SU_ERR_VERIFY;
        goto done;
    }

    /* A card truncated and then filled up part-way would lose the ELF: refuse first if it cannot hold the new one. */
    if (suIsMc(cur) && !suCardFits(cur, f.written)) {
        free(f.data);
        suResult = SU_ERR_SPACE;
        goto done;
    }

    /* Overwrite the booted ELF with the verified bytes. Step 1 changes nothing if it fails. */
    suResult = SU_ERR_SWAP;
    suStep = 1;
    /* O_CREAT is there for mcman, which truncates an existing file only in its O_CREAT branch (as sysneutrino.cpp
       notes). The first access after a card is detected answers "changed card": asking again is the cure. */
    for (off = 0; off < 4 && (fd = open(cur, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0 && suIsMc(cur); off++)
        DelayThread(200 * 1000);
    if (fd < 0) {
        suErrno = errno;
        free(f.data);
        goto done;
    }
    suStep = 2;
    for (off = 0; off < f.written;) {
        int n = f.written - off < 16 * 1024 ? f.written - off : 16 * 1024;
        if (write(fd, f.data + off, n) != n) {
            suErrno = errno ? errno : ENOSPC;
            close(fd);
            free(f.data);
            goto done;
        }
        off += n;
    }
    suStep = 3;
    if (close(fd) < 0) {
        suErrno = errno;
        free(f.data);
        goto done;
    }
    free(f.data);
    suInstalledRev = suRemoteRev;
    suResult = SU_OK;
done:
    free(b);
    suBusy = 0;
}

static void suWait(void (*worker)(void), const char *message)
{
    snprintf(suMessage, sizeof(suMessage), "%s", message);
    suBusy = 1;
    guiWaitBarTotal = 0; /* the bar appears once a download reports its size */
    guiHandleDeferedIO(&suBusy, suMessage, IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(worker), 0);
    guiWaitBarTotal = 0;
}

static void suReport(void)
{
    char text[256];

    switch (suResult) {
        case SU_ERR_NETBUSY:
            snprintf(text, sizeof(text), "%s", _l(_STR_UPDATE_ERR_NETBUSY));
            break;
        case SU_ERR_NETWORK:
            snprintf(text, sizeof(text), "%s", _l(_STR_UPDATE_ERR_NETWORK));
            break;
        case SU_ERR_CLOCK:
            snprintf(text, sizeof(text), "%s", _l(_STR_UPDATE_ERR_CLOCK));
            break;
        case SU_ERR_WRITE:
        case SU_ERR_MEMORY:
            snprintf(text, sizeof(text), "%s", _l(_STR_UPDATE_ERR_WRITE));
            break;
        case SU_ERR_VERIFY:
            snprintf(text, sizeof(text), "%s", _l(_STR_UPDATE_ERR_VERIFY));
            break;
        case SU_ERR_SPACE:
            snprintf(text, sizeof(text), "%s", _l(_STR_UPDATE_ERR_SPACE));
            break;
        case SU_ERR_SWAP:
            snprintf(text, sizeof(text), "%s (%d/%d)", _l(_STR_UPDATE_ERR_SWAP), suStep, suErrno);
            break;
        default:
            snprintf(text, sizeof(text), _l(_STR_UPDATE_ERR_GITHUB), suError);
    }
    guiMsgBox(text, 0, NULL);
}

void selfUpdateMenu(void)
{
    char text[256];
    int local = suRevision(OPL_VERSION);

    if (!suPlaceOk()) {
        guiMsgBox(_l(_STR_UPDATE_ERR_PLACE), 0, NULL);
        return;
    }
    if (local < 0) {
        guiMsgBox(_l(_STR_UPDATE_ERR_UNVERSIONED), 0, NULL);
        return;
    }
    if (suInstalledRev > local)
        local = suInstalledRev; /* the file on the device is already that build */
    suWait(&suCheckWorker, _l(_STR_UPDATE_CHECKING));
    if (suResult == SU_NEWEST) { /* the checksum is read: offer the same swap again */
        snprintf(text, sizeof(text), _l(_STR_UPDATE_NEWEST), local);
        if (suRemoteRev < local) { /* the channel is older than this build: no reinstall offer */
            guiMsgBox(text, 0, NULL);
            return;
        }
        size_t used = strlen(text);
        snprintf(text + used, sizeof(text) - used, " %s", _l(_STR_UPDATE_AGAIN));
    } else if (suResult != SU_OK) {
        suReport();
        return;
    } else {
        snprintf(text, sizeof(text), _l(_STR_UPDATE_AVAILABLE), suRemoteRev, local);
    }
    if (!guiMsgBox(text, 1, NULL))
        return;
    snprintf(text, sizeof(text), _l(_STR_UPDATE_DOWNLOADING), 0);
    suWait(&suInstallWorker, text);
    if (suResult != SU_OK) {
        suReport();
        return;
    }
    snprintf(text, sizeof(text), _l(_STR_UPDATE_DONE), suRemoteRev);
    guiMsgBox(text, 0, NULL);
}
