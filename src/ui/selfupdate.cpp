/*
  selfupdate.cpp -- Check for Updates, from the main menu.

  Asks this build's own release channel (RIPTOPL_UPDATE_TAG, tools/build_edition.py update_channel)
  for its SHA256SUMS.txt over verified HTTPS (raHttpsDownload), and offers the release's ELF only when
  its build number is newer. Nothing is fetched until the user picks the menu entry, and nothing is
  downloaded until they accept the offer.

  The only files written are beside the ELF that booted, under its own name: <name>.NEW while it
  downloads, then <name>.BAK. The download must match the release's SHA-256 and look like a MIPS ELF
  before anything is renamed. The swap is booted -> .BAK, .NEW -> booted; if the second rename fails
  the first is undone. On a memory card the .BAK is then deleted (it would hold a second 2 MB copy);
  on a USB/exFAT drive it stays. Any failure before the swap deletes .NEW and leaves the booted ELF
  as it was. Network and card work run on the I/O worker.
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

#include <fcntl.h>
#include <fileXio_rpc.h>
#include <libmc.h>
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
    SU_ERR_RESTORE,
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
    int fd;
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
        if (write(f->fd, f->head, f->headLen) != f->headLen)
            return -1;
        mbedtls_sha256_update(&f->sha, f->head, f->headLen);
        f->written = f->headLen;
        if (len == 0)
            return 0;
    }
    if (write(f->fd, data, len) != len)
        return -1;
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

static int suExists(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

/* A rename within the boot folder, checked by looking: from gone, to there. The memory card's ioman
   device has no rename (vcdsupport.cpp), so a card entry is renamed through mcserv, as Launchstation
   (genRenameNoReplace) and wLaunchELF do. */
static int suRename(const char *from, const char *to, const char *toName)
{
    if (suIsMc(from)) {
        sceMcTblGetDir info;
        int ret = -1, type = 0, port = from[2] - '0';
        /* Wake the card first, as Launchstation does: a rename on a card that has not been touched since boot can fail. */
        if (mcGetInfo(port, 0, &type, NULL, NULL) >= 0)
            mcSync(0, NULL, &ret);
        memset(&info, 0, sizeof(info));
        strncpy(reinterpret_cast<char *>(info.EntryName), toName, sizeof(info.EntryName) - 1);
        if (mcSetFileInfo(port, 0, strchr(from, ':') + 1, &info, 0x10 /* the name */) < 0 || mcSync(0, NULL, &ret) <= 0 || ret < 0)
            return -1;
    } else if (fileXioRename(from, to) < 0) /* the filesystem driver's own rename, as Launchstation does; libc rename() failed on a real console */
        return -1;
    return !suExists(from) && suExists(to) ? 0 : -1;
}

static void suInstallWorker(void)
{
    SuBuffers *b = static_cast<SuBuffers *>(malloc(sizeof(SuBuffers)));
    char cur[256], fresh[256], bak[256], freshName[40], bakName[40];
    unsigned char sha[32];
    SuFile f;

    suResult = SU_ERR_MEMORY;
    if (b == NULL)
        goto done;
    suResult = SU_ERR_SWAP;
    if (suJoin(cur, sizeof(cur), gBootDir, gBootElfName, "") || suJoin(fresh, sizeof(fresh), gBootDir, gBootElfName, ".NEW") ||
        suJoin(bak, sizeof(bak), gBootDir, gBootElfName, ".BAK") ||
        snprintf(freshName, sizeof(freshName), "%s.NEW", gBootElfName) >= (int)sizeof(freshName) ||
        snprintf(bakName, sizeof(bakName), "%s.BAK", gBootElfName) >= (int)sizeof(bakName) || !suExists(cur))
        goto done;

    unlink(fresh); /* a .NEW left by an interrupted update is ours */
    suResult = SU_ERR_WRITE;
    if ((f.fd = open(fresh, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
        goto done;
    f.written = 0;
    f.lastKb = 0;
    guiWaitBarDone = 0;
    f.headLen = 0;
    mbedtls_sha256_init(&f.sha);
    mbedtls_sha256_starts(&f.sha, 0);
    {
        int rc = suFetch(b, "/NathanNeurotic/RiptOPL/releases/download/" RIPTOPL_UPDATE_TAG "/" RIPTOPL_UPDATE_ELF, SU_ELF_MAX,
                         suFileSink, &f);
        int closed = close(f.fd);
        mbedtls_sha256_finish(&f.sha, sha);
        mbedtls_sha256_free(&f.sha);
        if (rc >= 0 && f.written == 0)
            rc = RATLS_ERR_SINK; /* shorter than an ELF header */
        if (rc < 0 || closed < 0) {
            unlink(fresh);
            suResult = rc < 0 ? suFromTls(rc) : SU_ERR_WRITE;
            goto done;
        }
    }
    if (memcmp(sha, suSha, sizeof(sha))) {
        unlink(fresh);
        suResult = SU_ERR_VERIFY;
        goto done;
    }

    suResult = SU_ERR_SWAP;
    unlink(bak); /* the previous update's backup */
    if (suRename(cur, bak, bakName) != 0) {
        if (!suExists(cur) && suRename(bak, cur, gBootElfName) != 0) {
            suResult = SU_ERR_RESTORE; /* the old ELF is <name>.BAK: keep every file */
            goto done;
        }
        unlink(fresh);
        goto done;
    }
    if (suRename(fresh, cur, gBootElfName) != 0) {
        if (suRename(bak, cur, gBootElfName) != 0) {
            suResult = SU_ERR_RESTORE; /* the old ELF is <name>.BAK: the message names it */
            goto done;
        }
        unlink(fresh);
        goto done;
    }
    if (suIsMc(cur))
        unlink(bak);
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
        case SU_ERR_SWAP:
            snprintf(text, sizeof(text), "%s", _l(_STR_UPDATE_ERR_SWAP));
            break;
        case SU_ERR_RESTORE:
            snprintf(text, sizeof(text), _l(_STR_UPDATE_ERR_RESTORE), gBootElfName);
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
    if (suResult == SU_NEWEST) {
        snprintf(text, sizeof(text), _l(_STR_UPDATE_NEWEST), local);
        guiMsgBox(text, 0, NULL);
        return;
    }
    if (suResult != SU_OK) {
        suReport();
        return;
    }
    snprintf(text, sizeof(text), _l(_STR_UPDATE_AVAILABLE), suRemoteRev, local);
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
