/*
  selfupdate.cpp -- Check for Updates, from the main menu.

  Asks this build's own release channel (RIPTOPL_UPDATE_TAG, tools/build_edition.py update_channel)
  for its SHA256SUMS.txt over verified HTTPS (raHttpsDownload), and offers the release's ELF only when
  its build number is newer. Nothing is fetched until the user picks the menu entry, and nothing is
  downloaded until they accept the offer.

  The download is held in RAM. It must match the release's SHA-256 and look like a MIPS ELF before
  anything on the device is touched. Every supported device then installs directly over the existing
  ELF, without a staged or backup copy. Network and file work run on the I/O worker.
*/

#include "include/opl.h"
#include "include/oplint.h" /* gBootElfName */
#include "include/gui.h"
#include "include/elfldr_args.h"
#include "include/lang.h"
#include "include/ioman.h"
#include "include/netsupport.h"
#include "include/ethsupport.h"
#include "include/ratls.h"
#include "include/nettime.h"
#include "include/guiwait.h"
#include "include/hdd.h"
#include "include/hddint.h"
#include "include/host.h"
#include "include/selfupdate.h"
#include "include/sound.h"
#include "include/supportbase.h"
#include "include/system.h"

#include "mbedtls/sha256.h"

#define SU_FIO_WRONLY 0x0002 /* iomanX flags, which fileXioOpen takes (newlib's differ) */
#define SU_FIO_CREAT  0x0200
#define SU_FIO_TRUNC  0x0400

#include <errno.h>
#define NEWLIB_PORT_AWARE // fileXioOpen/Write/Close: the call Launchstation overwrites its ELF with
#include <fileXio_rpc.h>
#include <io_common.h>
#include <kernel.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <delaythread.h>

#define SU_STRINGIFY_VALUE(value) #value
#define SU_STRINGIFY(value)       SU_STRINGIFY_VALUE(value)
#ifdef RIPTOPL_UPDATE_STABLE
/* A stable build reads only GitHub's Latest release, which publish_stable.py makes the newest stable one;
   GitHub never points Latest at a pre-release, and every rolling channel is one once a stable release
   exists. The release must also list SU_STABLE_MARKER, which only stable releases carry. */
#define SU_RELEASE_PATH  "/NathanNeurotic/RiptOPL/releases/latest/download/"
#define SU_STABLE_MARKER "RIPTOPL-STABLE.txt"
#else
#define SU_RELEASE_PATH "/NathanNeurotic/RiptOPL/releases/download/" SU_STRINGIFY(RIPTOPL_UPDATE_CHANNEL) "/"
#endif
static const char *suAsset;
static int suFormat;


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
    SU_ERR_MEMORY,
    SU_ERR_PLACE,
    SU_ERR_METADATA,
};

static const int SU_BUF = 16 * 1024;           /* request, headers and each piece of the body */
static const int SU_LOCATION = 2048;           /* GitHub's signed download redirects run to about 900 bytes */
static const int SU_SUMS_MAX = 32 * 1024;      /* bounds the release's complete checksum list (publish_stable.py) */
static const int SU_ELF_MAX = 8 * 1024 * 1024; /* RIPTOPL.ELF is about 2.3 MB */
static const int SU_MC_NAME_MAX = 31;          /* a memory card entry name */

static int suBusy;
static int suResult;
static int suError;        /* the RATLS_ERR_* behind SU_ERR_GITHUB */
static int suRemoteRev;    /* the release's build number */
static int suInstalledRev; /* a build installed this session: the running ELF is older than the file */
static int suVersionPending; /* the ELF is installed; only version.txt still needs a retry */
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

struct SuTarget
{
    char path[256];
    bool apa = false;
    bool mounted = false;

    void close()
    {
        if (mounted) {
            fileXioUmount("pfs1:");
            mounted = false;
        }
    }

    ~SuTarget() { close(); }
};

static bool suNumberedDevice(const char *path, const char *name)
{
    size_t length = strlen(name);
    if (strncmp(path, name, length))
        return false;
    const char *unit = path + length;
    const char *end = unit + strspn(unit, "0123456789");
    return end != unit && *end == ':';
}

static bool suResolveTarget(SuTarget &target, bool writing)
{
    if (gBootElfName[0] == '\0' || strpbrk(gBootElfName, "/\\:") != NULL ||
        strcasecmp(gBootElfName, "version.txt") == 0)
        return false;
    char partition[APA_IDMAX + 6], directory[256];
    if (hddParseBootDirectory(gBootLaunchDir, partition, sizeof(partition), directory, sizeof(directory)) == 0) {
        target.apa = true;
        // The updater runs on the I/O worker, which also owns the temporary PFS scan slot.
        const char *device = "pfs0:";
        if (gHDDPrefix == NULL || strcmp(gOPLPart, partition)) {
            if (!hddLoadModulesReady() || !gHddSupport.loadCoreSupportModules() ||
                fileXioMount("pfs1:", partition, writing ? FIO_MT_RDWR : FIO_MT_RDONLY) < 0)
                return false;
            target.mounted = true;
            device = "pfs1:";
        }
        char root[256];
        int len = snprintf(root, sizeof(root), "%s%s", device, directory);
        return len > 0 && len < (int)sizeof(root) &&
               suJoin(target.path, sizeof(target.path), root, gBootElfName, "") == 0;
    }
    if (!suNumberedDevice(gBootDir, "mass") && !suNumberedDevice(gBootDir, "mmce") &&
        !(suIsMc(gBootDir) && strlen(gBootElfName) <= (size_t)SU_MC_NAME_MAX))
        return false;
    return suJoin(target.path, sizeof(target.path), gBootDir, gBootElfName, "") == 0;
}

static int suReadBootFormat(SuTarget &target)
{
    int fd = fileXioOpen(target.path, 0x0001, 0); // iomanX read-only
    if (fd < 0 && !target.apa) {
        // Keep the spelling that actually opened the original for the subsequent write.
        char *sep = strchr(target.path, ':');
        if (sep[1] == '/')
            memmove(sep + 1, sep + 2, strlen(sep + 2) + 1);
        else if (strlen(target.path) + 1 < sizeof(target.path)) {
            memmove(sep + 2, sep + 1, strlen(sep + 1) + 1);
            sep[1] = '/';
        }
        fd = fileXioOpen(target.path, 0x0001, 0);
    }
    if (fd < 0)
        return SU_ELF_UNKNOWN;
    unsigned char head[SU_ELF_HEADER_MAX];
    int bytes = fileXioRead(fd, head, sizeof(head));
    fileXioClose(fd);
    return suElfFormat(head, bytes);
}

static bool suSelectAsset(void)
{
    SuTarget target;
    if (!suResolveTarget(target, false))
        return false;
    suFormat = suReadBootFormat(target);
    if (suFormat == SU_ELF_PACKED)
        suAsset = SU_STRINGIFY(RIPTOPL_UPDATE_ASSET);
#ifdef RIPTOPL_UPDATE_RAW_ASSET
    else if (suFormat == SU_ELF_UNPACKED)
        suAsset = SU_STRINGIFY(RIPTOPL_UPDATE_RAW_ASSET);
#endif
    else
        return false;
    return true;
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

/* The console clock gave no usable time (unset, or it cannot be read): take the time from a web
   server's Date header, as Date and Time > Set from Internet does, and check certificates against
   that. Kept in memory only: the console clock is not written. The download is still checked
   against the SHA-256 read over HTTPS. */
static bool suInternetTime(void)
{
    int t[6];

    if (netTimeFetch(t) != NETTIME_OK)
        return false;
    raHttpsSetTime(raTlsRtcToUtc(t[0], t[1], t[2], t[3], t[4], t[5]) + 9 * 60 * 60); /* raTlsRtcToUtc takes Japan time */
    return true;
}

/* Follows up to three redirects, to GitHub's hosts only. The body length, or RATLS_ERR_*. */
static int suFetch(SuBuffers *b, const char *path, int max_body, ratls_sink_t sink, void *ctx)
{
    char host[SU_HOST_MAX] = "github.com";
    int hop, rc = RATLS_ERR_HTTP;

    for (hop = 0; hop < 4; hop++) {
        char *location = b->location[hop & 1]; /* path may point into the other one */
        rc = raHttpsDownload(host, path, b->buf, SU_BUF, max_body, sink, ctx, location, SU_LOCATION);
        if (rc == RATLS_ERR_CLOCK && suInternetTime()) /* refused before anything was sent */
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

    if (ratlsNicBusy()) /* selfUpdateMenu could not take the adapter from UDPBD or UDPFS */
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

#ifdef SU_STABLE_MARKER
static bool suListsStableMarker(const char *text, int len)
{
    unsigned char sha[32];
    int rev;
    return suParseSums(text, len, SU_STABLE_MARKER, sha, &rev) == 0;
}
#endif

static void suCheckWorker(void)
{
    if (!suSelectAsset()) {
        suResult = SU_ERR_PLACE;
        suBusy = 0;
        return;
    }
    SuBuffers *b = static_cast<SuBuffers *>(malloc(sizeof(SuBuffers)));
    SuSums sums = {static_cast<char *>(malloc(SU_SUMS_MAX)), 0};

    suResult = SU_ERR_MEMORY;
    if (b != NULL && sums.text != NULL && (suResult = suNetworkUp()) == SU_OK) {
        int rc = suFetch(b, SU_RELEASE_PATH "SHA256SUMS.txt", SU_SUMS_MAX,
                         suSumsSink, &sums);
        if (rc < 0)
            suResult = suFromTls(rc);
        else if (suParseSums(sums.text, sums.len, suAsset, suSha, &suRemoteRev) != 0)
            suResult = suFromTls(RATLS_ERR_HTTP);
#ifdef SU_STABLE_MARKER
        else if (!suListsStableMarker(sums.text, sums.len))
            suResult = suFromTls(RATLS_ERR_HTTP); /* not a stable release: never offered to a stable build */
#endif
        else
            suResult = suRemoteRev > suRevision(OPL_VERSION) && suRemoteRev > suInstalledRev ? SU_OK : SU_NEWEST;
    }
    free(sums.text);
    free(b);
    suBusy = 0;
}

static int suStep, suErrno; /* which write step failed and why, shown after "Could not replace the ELF" */

static int suWriteElf(const char *path, int flags, const SuFile &file)
{
    suStep = 1;
    int fd = fileXioOpen(path, SU_FIO_WRONLY | flags, 0777);
    if (fd < 0)
        return fd;
    suStep = 2;
    for (int off = 0; off < file.written;) {
        int n = file.written - off < 16 * 1024 ? file.written - off : 16 * 1024;
        int written = fileXioWrite(fd, file.data + off, n);
        if (written <= 0 || written > n) {
            fileXioClose(fd);
            return written < 0 ? written : -ENOSPC;
        }
        off += written;
        guiWaitBarDone = off;
        snprintf(suMessage, sizeof(suMessage), "Installing... (%d / %d KB)", off / 1024, file.written / 1024);
    }
    suStep = 3;
    return fileXioClose(fd);
}

static int suWriteVersion(const char *elfPath)
{
    char dir[256], path[272], text[16];
    suStep = 6;
    if (strlen(elfPath) >= sizeof(dir))
        return -ENAMETOOLONG;
    strcpy(dir, elfPath);
    char *slash = strrchr(dir, '/');
    if (slash != NULL)
        *slash = '\0';
    else {
        char *colon = strrchr(dir, ':');
        if (colon == NULL)
            return -EINVAL;
        colon[1] = '\0';
    }
    if (suJoin(path, sizeof(path), dir, "version.txt", "") != 0)
        return -ENAMETOOLONG;
    int len = snprintf(text, sizeof(text), "%d\n", suRemoteRev);
    int fd = fileXioOpen(path, SU_FIO_WRONLY | SU_FIO_CREAT | SU_FIO_TRUNC, 0777);
    if (fd < 0)
        return fd;
    int rc = fileXioWrite(fd, text, len);
    int closeRc = fileXioClose(fd);
    if (rc == len && closeRc >= 0)
        return 0;
    return rc < 0 ? rc : closeRc < 0 ? closeRc : -EIO;
}

static void suInstallWorker(void)
{
    SuTarget target;
    int versionRc;
    if (suVersionPending) {
        if (!suResolveTarget(target, true)) {
            suResult = SU_ERR_PLACE;
        } else {
            versionRc = suWriteVersion(target.path);
            if (versionRc < 0) {
                suResult = SU_ERR_METADATA;
                suErrno = -versionRc;
            } else {
                suVersionPending = 0;
                suInstalledRev = suRemoteRev;
                suResult = SU_OK;
            }
        }
        target.close();
        suBusy = 0;
        return;
    }

    SuBuffers *b = static_cast<SuBuffers *>(malloc(sizeof(SuBuffers)));
    char url[sizeof(SU_RELEASE_PATH) + 64];
    unsigned char sha[32];
    SuFile f;

    suResult = SU_ERR_MEMORY;
    if (b == NULL)
        goto done;
    suStep = 0;
    suErrno = 0;

    f.data = NULL;
    f.cap = 0;
    f.written = 0;
    f.lastKb = 0;
    guiWaitBarDone = 0;
    f.headLen = 0;
    mbedtls_sha256_init(&f.sha);
    mbedtls_sha256_starts(&f.sha, 0);
    {
        snprintf(url, sizeof(url), "%s%s", SU_RELEASE_PATH, suAsset);
        int rc = suFetch(b, url, SU_ELF_MAX, suFileSink, &f);
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
    if (f.written != f.cap || memcmp(sha, suSha, sizeof(sha)) ||
        suElfFormat(f.data, f.written < SU_ELF_HEADER_MAX ? f.written : SU_ELF_HEADER_MAX) != suFormat) {
        free(f.data);
        suResult = SU_ERR_VERIFY;
        goto done;
    }

    guiWaitBarDone = 0;
    guiWaitBarTotal = f.written;
    snprintf(suMessage, sizeof(suMessage), "Installing... (%d / %d KB)", 0, f.written / 1024);

    if (!suResolveTarget(target, true) || suReadBootFormat(target) != suFormat) {
        suResult = SU_ERR_PLACE;
        free(f.data);
        goto done;
    }
    {
        int rc = suWriteElf(target.path, SU_FIO_CREAT | SU_FIO_TRUNC, f);
        if (rc >= 0 && target.apa) {
            char device[6];
            snprintf(device, sizeof(device), "%.5s", target.path);
            suStep = 4;
            rc = fileXioSync(device, 0);
        }
        if (rc < 0) {
            suResult = SU_ERR_SWAP;
            suErrno = -rc;
            free(f.data);
            goto done;
        }
    }
    versionRc = suWriteVersion(target.path);
    if (versionRc < 0) {
        suResult = SU_ERR_METADATA;
        suVersionPending = 1;
        suErrno = -versionRc;
        free(f.data);
        goto done;
    }
    free(f.data);
    suInstalledRev = suRemoteRev;
    suResult = SU_OK;
done:
    target.close();
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
        case SU_ERR_PLACE:
            snprintf(text, sizeof(text), "%s", _l(_STR_UPDATE_ERR_PLACE));
            break;
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
            snprintf(text, sizeof(text), "%s (%d/%d)", _l(_STR_UPDATE_ERR_SWAP), suStep, suErrno);
            break;
        case SU_ERR_METADATA:
            snprintf(text, sizeof(text), _l(_STR_UPDATE_ERR_METADATA), suErrno);
            break;
        default: {
            int label = _STR_UPDATE_ERR_GITHUB;
            switch (suError) {
                case RATLS_ERR_NET:
                    label = _STR_UPDATE_ERR_CONNECT;
                    break;
                case RATLS_ERR_CERT:
                    label = _STR_UPDATE_ERR_CERTIFICATE;
                    break;
                case RATLS_ERR_HTTP:
                    label = _STR_UPDATE_ERR_RESPONSE;
                    break;
                case RATLS_ERR_TIMEOUT:
                    label = _STR_UPDATE_ERR_TIMEOUT;
                    break;
            }
            /* a certificate error shows mbedTLS's reasons: 1 expired, 512 not yet valid, 8 not trusted, 4 wrong name */
            snprintf(text, sizeof(text), _l(label), suError == RATLS_ERR_CERT ? (int)raTlsVerifyFlags : suError);
            break;
        }
    }
    guiMsgBox(text, 0, NULL);
}

static void suRelaunch(void)
{
    // Reserve the scratch mount through the handoff and check the child's argument budget.
    ioBlockOpsTimed(1, 10);
    const bool drained = !ioHasPendingRequests();
    SuTarget target;
    char bootPath[256];
    char *argv[] = {bootPath};
    if (drained && suResolveTarget(target, false) && suReadBootFormat(target) == suFormat &&
        suJoin(bootPath, sizeof(bootPath), target.apa ? gBootLaunchDir : gBootDir, gBootElfName, "") == 0 &&
        elfldrArgsFit(target.path, 1, argv, "-la=RH", NULL)) {
        // Load from the live mount, but give the new instance the physical boot identity.
        host::app().deinitTerminal(sbLoaderDeinitException(target.path) | KEEPIOP_EXCEPTION, IO_MODE_SELECTED_ALL_SPARE);
        // The teardown above keeps the update's network stack, and any HDD, live with DEV9 powered.
        // An IOP reset against that can hang (#307), so from every boot device the loader closes
        // PFS, idles the HDD and powers DEV9 off once the new ELF is in memory, then resets.
        sysLoadELFApp(target.path, "", 1, argv, 1, 1);
        // The UI has already shut down if the loader unexpectedly returns.
        LoadExecPS2("rom0:OSDSYS", 0, NULL);
        exit(0);
    }
    target.close();
    ioBlockOps(0);
    guiMsgBox(_l(_STR_UPDATE_RELAUNCH_FAILED), 0, NULL);
}

static void suOffer(int local)
{
    char text[256];

    if (suInstalledRev > local)
        local = suInstalledRev; /* the file on the device is already that build */
    if (suVersionPending) {
        suWait(&suInstallWorker, _l(_STR_UPDATE_METADATA_RETRY));
    } else {
        suWait(&suCheckWorker, _l(_STR_UPDATE_CHECKING));
        if (suResult == SU_ERR_CLOCK) {
            if (!guiMsgBox(_l(_STR_UPDATE_OPEN_CLOCK), 1, NULL) || !guiShowDateTime())
                return;
            suWait(&suCheckWorker, _l(_STR_UPDATE_CHECKING));
        }
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
        if (isBgmPlaying())
            bgmStop();
        snprintf(text, sizeof(text), _l(_STR_UPDATE_DOWNLOADING), 0);
        suWait(&suInstallWorker, text);
        if (suResult != SU_OK) {
            suReport();
            return;
        }
    }
    if (suVersionPending && suResult != SU_OK) {
        suReport();
        return;
    }
    snprintf(text, sizeof(text), _l(_STR_UPDATE_DONE), suRemoteRev);
    size_t used = strlen(text);
    snprintf(text + used, sizeof(text) - used, " %s", _l(_STR_UPDATE_RELAUNCH));
    if (guiMsgBox(text, 1, NULL))
        suRelaunch();
}

int selfUpdateTakeNic(void)
{
    return oplSwapNicForUpdate(1) == 0;
}

void selfUpdateGiveNic(void)
{
    /* each try waits for the menu's work to drain; the saved protocol is already back, so a restart applies it */
    for (int tries = 0; oplSwapNicForUpdate(0) != 0;)
        if (++tries == 10) {
            guiMsgBox(_l(_STR_NETBOOT_RESTART), 0, NULL);
            break;
        }
}

void selfUpdateMenu(void)
{
    int local = suRevision(OPL_VERSION);

    if (local < 0) {
        guiMsgBox(_l(_STR_UPDATE_ERR_UNVERSIONED), 0, NULL);
        return;
    }
    /* UDPBD or UDPFS holds the network adapter: reset the IOP to take it for the update, and again after to
       give it back, as Launchstation does when it switches network stacks. */
    if (!ratlsNicBusy()) {
        suOffer(local);
        return;
    }
    if (!selfUpdateTakeNic()) {
        guiMsgBox(_l(_STR_UPDATE_ERR_NETBUSY), 0, NULL);
        return;
    }
    suOffer(local);
    selfUpdateGiveNic();
}
