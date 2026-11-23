/*
  RA: loading the watch list for the game being launched.

  Every game watches its own set of addresses. Baking them into the code would
  mean rebuilding the loader per game, so the list arrives as a file, the same
  way OPL already ships cheats from CHT/.

  The PC client builds the file from the game's achievement set on
  RetroAchievements. The format is modules/network/common/ra_watch.h.

  The list lives in the RaWatch object (gRaWatch), whose memory the loader owns;
  ee_core copies it during its initialisation, while loader memory is still
  intact. This is the same trick the cheat list uses (cheatman.c + cheat_api.c).

  Upstream design and implementation: hacan359.
*/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef __OPLDIAG
#include <sys/stat.h> /* mkdir, for the launch log */
#endif

#include "include/opl.h"
#include "include/ioman.h"
#include "include/rawatch.h"
#include "modules/network/common/ra_watch.h"

RaWatch gRaWatch;

int LoadWatchListFile(const char *file, const char *startup)
{
    return gRaWatch.loadWatchList(file, startup);
}

int SetWatchList(const void *data, int len, const char *startup)
{
    return gRaWatch.setWatchList(data, len, startup);
}

unsigned int *GetWatchList(void)
{
    return gRaWatch.watchList();
}

int GetWatchCount(void)
{
    return gRaWatch.watchCount();
}

int GetWatchBytes(void)
{
    return gRaWatch.watchBytes();
}

struct ra_node *GetNodeList(void)
{
    return gRaWatch.nodeList();
}

int GetNodeCount(void)
{
    return gRaWatch.nodeCount();
}

const char *GetWatchHash(void)
{
    return gRaWatch.watchHash();
}

void ClearWatchList(void)
{
    gRaWatch.clearWatchList();
}

/* Takes the chain list if it is sound, drops all of it otherwise.

   ee_core walks these in an interrupt handler with the game running, so
   nothing is checked there: a parent must already be resolved when its
   child is read, and a size must be one the reader knows. A list that
   breaks either rule is thrown away whole rather than half-used. */
bool RaWatch::takeNodes(const void *nodes, unsigned int count, unsigned int magic)
{
    nodeCount_ = 0;
    if (nodes == NULL || count == 0 || count > RA_NODE_MAX ||
        (magic != RA_NODE_MAGIC && magic != RA_NODE_LEGACY_MAGIC) ||
        watchBytes_ + (int)count * RA_NODE_PAIR_BYTES > RA_SNAP_MAX_BYTES)
        return false;

    // Normalize backwards: file reads can alias nodeList_, and RANL has a shorter stride.
    const unsigned char *data = (const unsigned char *)nodes;
    const unsigned int stride = magic == RA_NODE_MAGIC ? 12 : 8;
    for (int i = (int)count - 1; i >= 0; i--) {
        struct ra_node node;
        node.scale = 1;
        memcpy(&node, data + i * stride, stride);
        const unsigned int parent = RA_NODE_PARENT(node.w), size = RA_NODE_SIZE(node.w);
        const unsigned int allowed = magic == RA_NODE_MAGIC ? 0x07FFFFFFu : 0x0000FFFFu;
        if ((node.w & ~allowed) != 0 || RA_NODE_WIDTH(node.w) > 32 ||
            (size != 1 && size != 2 && size != 4) ||
            parent >= (RA_NODE_FROM_NODE(node.w) ? (unsigned int)i : (unsigned int)watchCount_))
            return false;
        if (magic == RA_NODE_LEGACY_MAGIC)
            node.w |= RA_NODE_LEGACY;
        nodeList_[i] = node;
    }
    nodeCount_ = (int)count;
    return true;
}

void RaWatch::clearWatchList()
{
    watchCount_ = 0;
    watchBytes_ = 0;
    nodeCount_ = 0;
    watchStartup_[0] = '\0';
    watchHash_[0] = '\0';
}

/* Appends one line per event while a game launches.

   Behind __OPLDIAG rather than upstream's own RA_DEBUG switch: CI builds
   `make release`, so a flag that only exists in a debug configuration never
   reaches a tester, which is exactly who needs this. The share comes first --
   a write to a USB stick can sit in the driver's cache and be lost when the
   console powers off, which is when the note matters -- but with no share
   mounted it falls back to local storage so the note still lands. The HDD
   fallback is the mounted pfs0: data home: hdd0: is the raw APA partition
   namespace, where mkdir/fopen("a") mean creating a partition. */
void raLaunchNote(const char *what, int a, int b)
{
#ifdef __OPLDIAG
    static const char *dirs[] = {"smb0:RA", "mass0:RA", "pfs0:RA"};
    FILE *f = NULL;
    int i;

    for (i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])) && f == NULL; i++) {
        char file[64];

        mkdir(dirs[i], 0777);
        snprintf(file, sizeof(file), "%s/launch.txt", dirs[i]);
        f = fopen(file, "a");
    }

    if (f == NULL)
        return;

    fprintf(f, "%-28s %6d %6d\n", what ? what : "?", a, b);
    fclose(f);
#else
    (void)what;
    (void)a;
    (void)b;
#endif
}

/* The xeRAbora v2.1 RAH1 tail is a magic word and exactly 32 hex digits.
   Do not accept random extra bytes as a valid achievement watch list.
   A valid tail's digits go to out (RA_HASH_LEN + 1 bytes), which is left alone otherwise. */
static bool raWatchHashTailValid(const unsigned char *data, int len, char *out)
{
    struct ra_hash_file hash;
    if (data == NULL || len != (int)sizeof(hash))
        return false;
    memcpy(&hash, data, sizeof(hash));
    if (hash.magic != RA_HASH_MAGIC)
        return false;
    for (int i = 0; i < RA_HASH_LEN; i++) {
        char c = hash.hash[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    memcpy(out, hash.hash, RA_HASH_LEN);
    out[RA_HASH_LEN] = '\0';
    return true;
}

int RaWatch::setWatchList(const void *data, int len, const char *startup)
{
    const struct ra_watch_file *hdr = (const struct ra_watch_file *)data;
    unsigned int need;

    clearWatchList();

    if (data == NULL || startup == NULL || len < (int)sizeof(*hdr))
        return -1;

    if (hdr->magic != RA_WATCH_MAGIC)
        return -3;

    if (hdr->version != RA_WATCH_VERSION)
        return -4;

    if (hdr->count == 0 || hdr->count > RA_WATCH_MAX)
        return -5;

    if (hdr->bytes == 0 || hdr->bytes > RA_SNAP_MAX_BYTES)
        return -6;

    need = hdr->count * sizeof(unsigned int);
    if (len < (int)(sizeof(*hdr) + need))
        return -7;

    memcpy(watchList_, (const unsigned char *)data + sizeof(*hdr), need);
    if (!raWatchEntriesValid(watchList_, (int)hdr->count, (int)hdr->bytes))
        return -8;

    watchCount_ = (int)hdr->count;
    watchBytes_ = (int)hdr->bytes;
    snprintf(watchStartup_, sizeof(watchStartup_), "%s", startup);

    // Network and saved lists accept direct entries, RANL/RAN2 nodes, and an
    // optional trailing RAH1 image hash, but never unrecognized extra bytes.
    const unsigned char *tail = (const unsigned char *)data + sizeof(*hdr) + need;
    const int left = len - (int)(sizeof(*hdr) + need);
    char hash[RA_HASH_LEN + 1] = "";
    if (left != 0 && !raWatchHashTailValid(tail, left, hash)) {
        struct ra_node_file nf;
        if (left < (int)sizeof(nf)) {
            clearWatchList();
            return -9;
        }
        memcpy(&nf, tail, sizeof(nf));
        if ((nf.magic != RA_NODE_MAGIC && nf.magic != RA_NODE_LEGACY_MAGIC) ||
            nf.count == 0 || nf.count > RA_NODE_MAX) {
            clearWatchList();
            return -9;
        }
        const unsigned int stride = nf.magic == RA_NODE_MAGIC ? sizeof(struct ra_node) : 8;
        const int nodesLen = (int)sizeof(nf) + (int)nf.count * (int)stride;
        if (nodesLen > left ||
            (nodesLen < left && !raWatchHashTailValid(tail + nodesLen, left - nodesLen, hash)) ||
            !takeNodes(tail + sizeof(nf), nf.count, nf.magic)) {
            clearWatchList();
            return -9;
        }
    }
    memcpy(watchHash_, hash, sizeof(watchHash_)); // only now: the whole list was accepted

    LOG("RA: list received over the network: %d entries, %d chains, snapshot %d bytes\n",
        watchCount_, nodeCount_, watchBytes_);

    return watchCount_;
}

/* Reads one specific file. Path construction belongs to the caller
   (sbLoadWatchList), which owns the CHT-style extension retries -- keeping it
   there means there is exactly one place that decides where a list may live.

   Returns the number of entries, or negative on failure. A missing file is not
   an error: this game has no set. */
int RaWatch::loadWatchList(const char *file, const char *startup)
{
    struct ra_watch_file hdr;
    int fd, got;

    if (file == NULL || startup == NULL)
        return -1;

    /* This game's list is already in memory: the network brought it in the
       menu, and it is fresher than any file. Leave the file alone; on USB it
       may not have reached the medium yet. */
    if (watchCount_ > 0 &&
        strncmp(watchStartup_, startup, sizeof(watchStartup_) - 1) == 0) {
        LOG("RA: list for %s already in memory, %d entries\n", startup, watchCount_);
        return watchCount_;
    }

    clearWatchList();

    LOG("RA: looking for watch list %s\n", file);

    fd = open(file, O_RDONLY);
    if (fd < 0) {
        LOG("RA: no list, telemetry will carry no snapshot\n");
        return -1;
    }

    got = read(fd, &hdr, sizeof(hdr));
    if (got != (int)sizeof(hdr)) {
        LOG("RA: short read on the header\n");
        close(fd);
        return -2;
    }

    if (hdr.magic != RA_WATCH_MAGIC) {
        LOG("RA: wrong file, magic %08X\n", hdr.magic);
        close(fd);
        return -3;
    }

    if (hdr.version != RA_WATCH_VERSION) {
        LOG("RA: format version %u, expected %d\n", hdr.version, RA_WATCH_VERSION);
        close(fd);
        return -4;
    }

    if (hdr.count == 0 || hdr.count > RA_WATCH_MAX) {
        LOG("RA: %u entries, limit %d\n", hdr.count, RA_WATCH_MAX);
        close(fd);
        return -5;
    }

    if (hdr.bytes == 0 || hdr.bytes > RA_SNAP_MAX_BYTES) {
        LOG("RA: snapshot %u bytes, limit %d\n", hdr.bytes, RA_SNAP_MAX_BYTES);
        close(fd);
        return -6;
    }

    got = read(fd, watchList_, (int)(hdr.count * sizeof(unsigned int)));

    if (got != (int)(hdr.count * sizeof(unsigned int))) {
        LOG("RA: short read on the list: %d of %u\n", got, (unsigned)(hdr.count * sizeof(unsigned int)));
        close(fd);
        return -7;
    }
    if (!raWatchEntriesValid(watchList_, (int)hdr.count, (int)hdr.bytes)) {
        close(fd);
        return -8;
    }

    watchCount_ = (int)hdr.count;
    watchBytes_ = (int)hdr.bytes;
    snprintf(watchStartup_, sizeof(watchStartup_), "%s", startup);

    struct ra_node_file nf = {};
    const int tailBytes = read(fd, &nf, sizeof(nf));
    char digits[RA_HASH_LEN + 1] = "";
    if (tailBytes != 0) {
        unsigned char hash[sizeof(struct ra_hash_file)];
        unsigned char extra;
        bool valid = tailBytes == (int)sizeof(nf);
        if (valid && nf.magic == RA_HASH_MAGIC) {
            // The first eight bytes contain the magic and four hash digits.
            memcpy(hash, &nf, sizeof(nf));
            valid = read(fd, hash + sizeof(nf), sizeof(hash) - sizeof(nf)) ==
                        (int)(sizeof(hash) - sizeof(nf)) &&
                    raWatchHashTailValid(hash, sizeof(hash), digits) && read(fd, &extra, 1) == 0;
        } else if (valid) {
            if ((nf.magic != RA_NODE_MAGIC && nf.magic != RA_NODE_LEGACY_MAGIC) ||
                nf.count == 0 || nf.count > RA_NODE_MAX) {
                valid = false;
            } else {
                const unsigned int stride = nf.magic == RA_NODE_MAGIC ? sizeof(struct ra_node) : 8;
                const int want = (int)(nf.count * stride);
                valid = read(fd, nodeList_, want) == want;
                if (valid) {
                    const int hashBytes = read(fd, hash, sizeof(hash));
                    valid = hashBytes == 0 ||
                            (hashBytes == (int)sizeof(hash) &&
                             raWatchHashTailValid(hash, hashBytes, digits) && read(fd, &extra, 1) == 0);
                }
                if (valid)
                    valid = takeNodes(nodeList_, nf.count, nf.magic);
            }
        }
        if (!valid) {
            clearWatchList();
            close(fd);
            return -9;
        }
    }

    close(fd);
    memcpy(watchHash_, digits, sizeof(watchHash_)); // only now: the whole file was accepted

    LOG("RA: list loaded: %d entries, %d chains, snapshot %d bytes\n",
        watchCount_, nodeCount_, watchBytes_);

    return watchCount_;
}
