/*
  Copyright 2024, Open-PS2-Loader contributors
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.

  Favourites (FAV_MODE): a virtual tab that aggregates favourited items from every loaded
  device list. Each favourite proxies launch / config / art / flags to its source list.
  Persistence uses a versioned, bounds-checked binary store (favourites.bin) written with
  explicit scalar fields -- never a raw-struct fwrite -- so a corrupt file can never index
  or allocate out of bounds.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <new>
#include "include/host.h"
#include "include/opl.h"
#include "include/iosupport.h"
#include "include/menusys.h"
#include "include/ioman.h"
#include "include/lang.h"
#include "include/textures.h"
#include "include/config.h"
#include "include/supportbase.h" // sbPopulateConfig + base_game_info_t (VCD favourite config)
#include "include/vcdsupport.h"
#include "include/cuesupport.h" // CUE_ROW_EXTENSION -- a PS1 favourite names its core  // VCD favourites: name-addressed POPSTARTER launch helpers
#include "include/libview.h"    // libViewActive / libListViewActive -- which list this page shows
#include "include/ethsupport.h" // ETH ISO favourite resolution while the live source is in VCD view
#include "include/bdmsupport.h" // bdmSupportIsUDPBD -- POPSTARTER favourites cannot bind to UDPBD
#include "include/favsupport.h"
#include "include/gamemanage.h" // Hide Game: hidden games are not gathered (RIPPS2)
#include "include/gamesource.h"
#include "include/folderbrowse.h" // folderModeSupported -- All Games skips folder rows

int gFAVStartMode;
int gAllGames; // All Games list: the Favourites page lists every game of every started source

// The one Favourites store; the public favXxx functions below and the FavSource callbacks
// forward to it. Constant-initialized only (no dynamic init before main).
FavSupport gFavSupport;

static char *favStrdup(const char *s)
{
    int n = (int)strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p != NULL)
        memcpy(p, s, n);
    return p;
}

// ---- on-disk path -------------------------------------------------------------

// The two stores that share this record format: the starred games, and the last games launched
// (RECENT_MAX_ITEMS, newest first; the Favourites Recent stop and Sort Games: Last played read it).
#define FAV_FILE_NAME "favourites.bin"
static const char kRecentFile[] = "recents.bin";

// favourites.bin lives next to the master config in the SHARED OPL config dir (not renamed to
// riptopl) so favourites carry across OPL / uOPL / wOPL -- only our settings file is private.
// recents.bin sits beside it.
static void favGetFilePath(char *out, int outSize, const char *name = FAV_FILE_NAME)
{
    config_set_t *cfg = configGetByType(CONFIG_OPL);
    const char *fn = (cfg != NULL && cfg->filename() != NULL) ? cfg->filename() : "mc0:OPL/" CONFIG_OPL_FILENAME;
    const char *base = CONFIG_OPL_FILENAME;
    int len = (int)strlen(fn);
    int blen = (int)strlen(base);
    if (len >= blen && strcmp(fn + len - blen, base) == 0) {
        int dirLen = len - blen;
        if (dirLen + (int)strlen(name) >= outSize) {
            out[0] = '\0';
            return;
        }
        memcpy(out, fn, dirLen);
        out[dirLen] = '\0';
        strncat(out, name, outSize - strlen(out) - 1);
    } else {
        snprintf(out, outSize, "mc0:OPL/%s", name);
    }
}

// ---- explicit little-endian scalar IO (never raw-struct) ----------------------

struct FavStream
{
    const u8 *data;
    int size;
    int position;
};

static int rdFull(FavStream *stream, void *buf, int size)
{
    if (size < 0 || stream->position > stream->size)
        return -1;
    int available = stream->size - stream->position;
    int count = size < available ? size : available;
    if (count > 0)
        memcpy(buf, stream->data + stream->position, count);
    stream->position += count;
    return count;
}

static int rdBytes(FavStream *stream, void *buf, int n) { return rdFull(stream, buf, n) == n; }

static int rdU16(FavStream *stream, u16 *v)
{
    u8 b[2];
    if (!rdBytes(stream, b, 2))
        return 0;
    *v = (u16)(b[0] | (b[1] << 8));
    return 1;
}
static int rdU32(FavStream *stream, u32 *v)
{
    u8 b[4];
    if (!rdBytes(stream, b, 4))
        return 0;
    *v = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
    return 1;
}

// Little-endian signed 32-bit from a byte buffer (for the foreign-format import below).
static int rdS32le(const u8 *b)
{
    return (int)((u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24));
}

// Translate a uOPL/wOPL IO_MODES value to ours. Their enum differs: they have only 5 BDM slots
// (0..4) then ETH=5, HDD=6, APP=7, FAV=8, MMCE=9; we have 8 BDM slots then ETH=8, HDD=9,
// APP=10, MMCE=11, FAV=12. Without this, a foreign ETH/HDD/APP favourite (5/6/7) would be read
// as one of OUR BDM slots and silently fail to resolve. BDM slots pass through unchanged --
// favResolve re-matches a BDM favourite across all of our slots by id+text anyway.
static int favMapWoplMode(int m)
{
    switch (m) {
        case 5:
            return ETH_MODE;
        case 6:
            return HDD_MODE;
        case 7:
            return APP_MODE;
        case 8:
            return FAV_MODE; // favourite of the FAV tab itself; favResolve will reject it
        case 9:
            return MMCE_MODE;
        default:
            return m; // 0..4 are BDM slots in both schemes; anything else passes through
    }
}

// Import a uOPL/wOPL favourites.bin (read-only). Their format is a header-less stream of
// records, each = a raw 32-byte submenu_item_t (we trust only icon_id@0, text_id@8, id@12; the
// on-disk text/cache/owner POINTERS are garbage and ignored), then int text_len, then the text
// bytes, then a short owner-mode. uOPL and wOPL share this exact layout. We never WRITE it --
// our own writes use the hardened OFAV format -- so this is a one-way carry-over that lets
// favourites set in those builds appear in RiptOPL. Returns NULL on empty/corrupt input.
constexpr int WOPL_FAV_STRUCT_SIZE = 32; // sizeof(submenu_item_t) on the EE: 8 x 4-byte fields
static fav_raw_t *favReadWoplFile(FavStream *stream, int *outCount, int *complete, int *recognized)
{
    *outCount = 0;
    *complete = 0;
    fav_raw_t *recs = (fav_raw_t *)calloc(FAV_MAX_ITEMS, sizeof(fav_raw_t));
    if (recs == NULL) {
        *recognized = 0;
        return NULL;
    }

    int got = 0, malformed = 0;
    while (got < FAV_MAX_ITEMS) {
        u8 st[WOPL_FAV_STRUCT_SIZE];
        u32 tlen = 0;
        u16 mode = 0;
        int bytes = rdFull(stream, st, WOPL_FAV_STRUCT_SIZE);
        if (bytes == 0) {
            *complete = got > 0; // an empty/torn file is not a validated foreign list
            break;
        }
        if (bytes != WOPL_FAV_STRUCT_SIZE)
            break; // keep a salvageable prefix for display, but refuse edits
        if (!rdU32(stream, &tlen))
            break;
        if (tlen == 0 || tlen > FAV_TEXT_MAX) {
            LOG("FAV import: bad text_len=%u (not uOPL/wOPL format?), aborting\n", (unsigned)tlen);
            break; // desync / foreign struct size / corruption -> stop
        }
        if (!rdBytes(stream, recs[got].text, tlen))
            break;
        int terminated = recs[got].text[tlen - 1] == '\0';
        recs[got].text[tlen - 1] = '\0'; // force-terminate even a salvageable malformed record
        if (!rdU16(stream, &mode))
            break;
        recs[got].icon_id = rdS32le(st + 0);
        recs[got].text_id = rdS32le(st + 8);
        recs[got].id = rdS32le(st + 12);
        recs[got].mode = favMapWoplMode((int)(short)mode); // owner mode (signed short on disk) -> our IO_MODES
        got++;
        if (!terminated) {
            malformed = 1;
            break;
        }
    }
    if (got == FAV_MAX_ITEMS) {
        u8 tail;
        *complete = !malformed && rdFull(stream, &tail, 1) == 0; // never silently discard excess records on save
        if (!*complete)
            *recognized = 0;
    }

    if (got == 0) {
        free(recs);
        return NULL;
    }
    LOG("FAV imported %d favourite(s) from uOPL/wOPL format\n", got);
    *outCount = got;
    return recs;
}

// Read bounds-checked records, retaining a salvageable prefix for display. A mutation must
// also require editable: only ENOENT or a fully read/supported list with clean EOF/close may
// replace the file. NULL alone cannot distinguish a valid empty store from an IO/allocation error.
static fav_raw_t *favParseImage(const u8 *data, int size, int *outCount, int *editable, int allowImport, int *recognized)
{
    FavStream bytes = {data, size, 0};
    FavStream *stream = &bytes;
    *outCount = 0;
    *editable = 0;
    *recognized = 1;
    if (size == 0)
        return NULL;

    u32 magic = 0;
    u16 ver = 0, cnt = 0;
    int hdrOk = rdU32(stream, &magic) && rdU16(stream, &ver) && rdU16(stream, &cnt);
    if (!hdrOk || magic != FAV_MAGIC) {
        // No OFAV header -> this may be a uOPL/wOPL favourites.bin in the shared OPL dir.
        // Rewind and import it read-only so favourites carry over from those builds.
        LOG("FAV: no OFAV header (magic=%08x) -- attempting uOPL/wOPL import\n", (unsigned)magic);
        if (!allowImport) {
            *recognized = 0;
            return NULL;
        }
        stream->position = 0;
        int complete = 0;
        fav_raw_t *imported = favReadWoplFile(stream, outCount, &complete, recognized);
        if (editable != NULL)
            *editable = complete;
        return imported;
    }
    // Range-checked against FAV_VERSION rather than listing versions. The enumerated form let the
    // writer's version outrun the reader's list: FAV_VERSION went to 3, favWriteFile stamped 3, and
    // this gate still admitted only 1 and 2 -- so the first save after upgrading produced a file the
    // NEXT boot refused, presenting an empty favourites tab that would then be written back over the
    // user's real list. Bumping the version must never again require remembering to edit this line.
    if (ver < 1 || ver > FAV_VERSION) {
        LOG("FAV reject: unsupported OFAV version %d\n", ver);
        *recognized = 0;
        return NULL;
    }
    if (cnt == 0) {
        u8 tail;
        int complete = rdFull(stream, &tail, 1) == 0;
        if (editable != NULL)
            *editable = complete;
        return NULL;
    }
    int complete = cnt <= FAV_MAX_ITEMS;
    if (cnt > FAV_MAX_ITEMS) {
        *recognized = 0;
        cnt = FAV_MAX_ITEMS;
    } // never trust the stored count beyond the hard cap

    fav_raw_t *recs = (fav_raw_t *)calloc(cnt, sizeof(fav_raw_t));
    if (recs == NULL) {
        *recognized = 0;
        return NULL;
    }

    int got = 0;
    for (int i = 0; i < (int)cnt; i++) {
        u16 mode = 0, tlen = 0;
        u32 id = 0, icon = 0, tid = 0;
        u8 kindByte = 0;
        if (!rdU16(stream, &mode) || !rdU32(stream, &id) || !rdU32(stream, &icon) || !rdU32(stream, &tid))
            break; // short read -> stop, keep what we have
        if (ver >= 2 && !rdBytes(stream, &kindByte, 1))
            break; // v2/v3 records carry a per-record byte between text_id and text_len
        if (!rdU16(stream, &tlen))
            break;
        if (tlen == 0 || tlen > FAV_TEXT_MAX) {
            LOG("FAV bad text_len=%d, aborting parse\n", tlen);
            break; // cannot resync past an unknown-length field -> stop
        }
        if (!rdBytes(stream, recs[got].text, tlen))
            break; // short read on text -> stop
        if (recs[got].text[tlen - 1] != '\0')
            complete = 0;
        recs[got].text[tlen - 1] = '\0'; // tlen includes the NUL; force-terminate
        recs[got].mode = (int)mode;
        recs[got].id = (int)id;
        recs[got].icon_id = (int)icon;
        recs[got].text_id = (int)tid;
        // v1: no byte at all -> ISO. v2: the byte was a BOOLEAN isVcd, so 1 means VCD and 0
        // means "not VCD" -- which lumped app favourites in with disc games on one shelf. Now that
        // APPS has a shelf of its own, a v2 record whose source is the APPS tab is an ELF
        // favourite; that is the only record whose shelf changes on upgrade. v3 stores the kind
        // directly. Values we do not recognise fall back to ISO rather than being dropped.
        if (ver >= 3)
            recs[got].kind = (kindByte <= FAV_KIND_ELF) ? (int)kindByte : FAV_KIND_ISO;
        else if (kindByte)
            recs[got].kind = FAV_KIND_VCD;
        else
            recs[got].kind = ((int)mode == APP_MODE) ? FAV_KIND_ELF : FAV_KIND_ISO;
        got++;
    }
    u8 tail;
    complete = complete && got == (int)cnt && rdFull(stream, &tail, 1) == 0;
    if (editable != NULL)
        *editable = complete;

    if (got == 0) {
        free(recs);
        return NULL;
    }
    *outCount = got;
    return recs;
}

// Bound both native and foreign layouts. Read an ordinary list in one transfer;
// positive short reads accumulate, and oversized/error inputs remain read-only.
constexpr int FAV_FILE_MAX = 8 + FAV_MAX_ITEMS * (WOPL_FAV_STRUCT_SIZE + 4 + FAV_TEXT_MAX + 2);
struct FavImage
{
    u8 *data = NULL;
    int size = 0;
    bool missing = false;
    bool readable = false;
};

static FavImage favLoadImage(const char *path)
{
    FavImage image;
    errno = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        image.missing = errno == ENOENT;
        return image;
    }
    int size = lseek(fd, 0, SEEK_END);
    bool exact = size >= 0 && size <= FAV_FILE_MAX;
    int wanted = size > FAV_FILE_MAX ? FAV_FILE_MAX : size;
    if (wanted >= 0 && lseek(fd, 0, SEEK_SET) == 0) {
        if (wanted > 0)
            image.data = (u8 *)malloc(wanted);
        if (wanted == 0 || image.data != NULL) {
            while (image.size < wanted) {
                int count = read(fd, image.data + image.size, wanted - image.size);
                if (count <= 0 || count > wanted - image.size)
                    break;
                image.size += count;
            }
            u8 tail;
            image.readable = exact && image.size == wanted && read(fd, &tail, 1) == 0;
        }
    }
    if (close(fd) < 0)
        image.readable = false;
    return image;
}

static bool favStoragePaths(char *path, int pathSize, char *staged, int stagedSize, const char *name = FAV_FILE_NAME)
{
    favGetFilePath(path, pathSize, name);
    int size = strlen(path), suffix = strlen(name);
    return size >= suffix && strcmp(path + size - suffix, name) == 0 &&
           (unsigned int)snprintf(staged, stagedSize, "%s.new", path) < (unsigned int)stagedSize;
}

static fav_raw_t *favReadPath(const char *path, int *count, int *complete, int *recoverable, int *missing, int allowImport)
{
    FavImage image = favLoadImage(path);
    int recognized = 0, valid = 0;
    fav_raw_t *records = favParseImage(image.data, image.size, count, &valid, allowImport, &recognized);
    *complete = image.readable && valid;
    *recoverable = image.readable && recognized;
    *missing = image.missing;
    free(image.data);
    return records;
}

static fav_raw_t *favReadFile(int *outCount, int *editable = NULL, const char *name = FAV_FILE_NAME)
{
    char path[256], staged[264];
    *outCount = 0;
    if (editable != NULL)
        *editable = 0;
    if (!favStoragePaths(path, sizeof(path), staged, sizeof(staged), name))
        return NULL;
    int complete, recoverable, missing;
    fav_raw_t *primary = favReadPath(path, outCount, &complete, &recoverable, &missing, 1);
    if (complete) {
        if (editable != NULL)
            *editable = 1;
        return primary; // A complete primary, including a PC edit or empty list, always wins.
    }
    int stagedCount, stagedComplete, stagedRecoverable, stagedMissing;
    fav_raw_t *recovery = favReadPath(staged, &stagedCount, &stagedComplete, &stagedRecoverable, &stagedMissing, 0);
    if (stagedComplete) {
        free(primary);
        *outCount = stagedCount;
        if (editable != NULL)
            *editable = missing || recoverable; // IO/unknown-version failures never authorize replacement.
        return recovery;
    }
    free(recovery);
    if (editable != NULL)
        *editable = missing && stagedMissing;
    return primary; // Salvage for display only when neither file is a validated editable list.
}

// Little-endian scalar appenders for the in-memory image favWriteFile builds.
static void bufU16(u8 *b, int *o, u16 v)
{
    b[(*o)++] = (u8)(v & 0xff);
    b[(*o)++] = (u8)((v >> 8) & 0xff);
}

static void bufU32(u8 *b, int *o, u32 v)
{
    b[(*o)++] = (u8)(v & 0xff);
    b[(*o)++] = (u8)((v >> 8) & 0xff);
    b[(*o)++] = (u8)((v >> 16) & 0xff);
    b[(*o)++] = (u8)((v >> 24) & 0xff);
}

/* Write a raw-record array back out (explicit scalar fields; pointers never persisted).

   SERIALISE FIRST, THEN WRITE THE IMAGE. This used to emit the file field by field: three writes for
   the header and SEVEN per record. Each one is an EE->IOP RPC round trip, so a library of 40
   favourites cost 283 of them -- and this runs on the GUI thread, straight off the R3 press in
   itemExecFav, which is why adding a favourite visibly stalled the menu. The byte count was never
   the problem; the syscall count was.

   The image is sized from the actual text lengths rather than FAV_MAX_ITEMS * FAV_TEXT_MAX, so a
   normal library allocates a few KB instead of 140. */
static u8 *favBuildImage(fav_raw_t *recs, int count, int *length)
{
    int i, off = 0, size = 8; // header: magic(4) + version(2) + count(2)
    u8 *img;

    if (count < 0 || count > FAV_MAX_ITEMS || (count > 0 && recs == NULL))
        return NULL;

    for (i = 0; i < count; i++) {
        int tlen = (int)strnlen(recs[i].text, FAV_TEXT_MAX) + 1;
        if (tlen > FAV_TEXT_MAX)
            tlen = FAV_TEXT_MAX;
        size += 17 + tlen; // mode(2) id(4) icon(4) text_id(4) kind(1) text_len(2) + text
    }

    img = (u8 *)malloc(size);
    if (img == NULL) {
        LOG("FAV write: cannot allocate %d byte image\n", size);
        return NULL;
    }

    bufU32(img, &off, FAV_MAGIC);
    bufU16(img, &off, FAV_VERSION);
    bufU16(img, &off, (u16)count);
    for (i = 0; i < count; i++) {
        int tlen = (int)strnlen(recs[i].text, FAV_TEXT_MAX) + 1;
        if (tlen > FAV_TEXT_MAX)
            tlen = FAV_TEXT_MAX;
        bufU16(img, &off, (u16)recs[i].mode);
        bufU32(img, &off, (u32)recs[i].id);
        bufU32(img, &off, (u32)recs[i].icon_id);
        bufU32(img, &off, (u32)recs[i].text_id);
        img[off++] = (u8)recs[i].kind; // same slot v2 used for isVcd, now a FAV_KIND_* (OFAV v3)
        bufU16(img, &off, (u16)tlen);
        memcpy(img + off, recs[i].text, tlen - 1);
        img[off + tlen - 1] = 0; // tlen counts the NUL, and a name capped at FAV_TEXT_MAX has none
        off += tlen;
    }

    *length = off;
    return img;
}

static int favWriteWhole(const char *path, const u8 *image, int size)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return 0;
    int written = 0;
    while (written < size) {
        int count = write(fd, image + written, size - written);
        if (count <= 0 || count > size - written)
            break;
        written += count;
    }
    int closed = close(fd);
    return written == size && closed >= 0;
}

static bool favImageMatches(const char *path, const u8 *data, int size)
{
    FavImage image = favLoadImage(path);
    bool matches = image.readable && image.size == size && memcmp(image.data, data, size) == 0;
    free(image.data);
    return matches;
}

static bool favImageValid(const FavImage &image, int allowImport, bool *recoverable)
{
    int count, complete, recognized;
    fav_raw_t *records = favParseImage(image.data, image.size, &count, &complete, allowImport, &recognized);
    free(records);
    *recoverable = image.readable && recognized;
    return image.readable && complete;
}

// Before reusing .new, restore it when it is the only complete copy. Re-read each
// path rather than caching authority across config-path changes or PC edits.
static bool favPrepareSave(const char *path, const char *staged)
{
    FavImage primary = favLoadImage(path);
    bool recoverable;
    bool complete = favImageValid(primary, 1, &recoverable);
    bool missing = primary.missing;
    free(primary.data);
    if (complete)
        return true;
    FavImage recovery = favLoadImage(staged);
    bool unused;
    bool stagedComplete = favImageValid(recovery, 0, &unused);
    bool ok = missing && recovery.missing;
    if (stagedComplete && (missing || recoverable))
        ok = favWriteWhole(path, recovery.data, recovery.size) && favImageMatches(path, recovery.data, recovery.size);
    free(recovery.data);
    return ok;
}

static int favWriteFile(fav_raw_t *recs, int count, const char *name = FAV_FILE_NAME)
{
    int size;
    u8 *image = favBuildImage(recs, count, &size);
    if (image == NULL)
        return 0;
    char path[256], staged[264];
    if (!favStoragePaths(path, sizeof(path), staged, sizeof(staged), name) || !favPrepareSave(path, staged)) {
        free(image);
        return 0;
    }
    // No ENOSPC in-place fallback: never truncate the last complete copy when
    // staging failed. Both successful writes must close and read back exactly.
    if (!favWriteWhole(staged, image, size) || !favImageMatches(staged, image, size)) {
        remove(staged); // The primary has not been touched; cleanup is best effort.
        free(image);
        return 0;
    }
    bool ok = favWriteWhole(path, image, size) && favImageMatches(path, image, size);
    free(image);
    if (ok)
        remove(staged); // A stale recovery file is harmless: the complete primary wins.
    return ok;
}

// ---- resolution helpers (stateless; operate on a source list, not the store) ---

// Resolve a stored record to a live source list + mark the source item favourited. BDM-range
// modes are re-matched across all 8 slots (hotplug / different bus). Returns the resolved
// owner (or NULL if the source isn't loaded / the item is absent).
// Best-effort star for a resolved VCD favourite: if the source device is currently in its VCD view,
// its submenu IS the VCD list, so light the star on the matching VCD item (by id+text). Misses
// harmlessly (no star, no change) when the device is in ISO view or the VCD list index has since
// shifted -- purely cosmetic on the source page; the FAV record + launch are unaffected either way.
static submenu_list_t *favFindVisibleSource(opl_io_module_t *mod, int sourceId, const char *text, int wantedView)
{
    submenu_list_t *cur;

    if (mod == NULL || mod->support == NULL || text == NULL)
        return NULL;
    for (cur = mod->subMenu; cur != NULL; cur = cur->next) {
        int rowView = host::library().listRowView(mod->support, cur->item.id);
        int viewMatches = wantedView == LIB_VIEW_ELF ? (rowView == LIB_VIEW_ELF || rowView == LIB_VIEW_PS1_ELF) : rowView == wantedView;
        if (!viewMatches || cur->item.text == NULL || strcmp(cur->item.text, text) != 0)
            continue;
        // Apps have title identity and PS1 records are name-addressed. ISO games retain the exact
        // source-local id, even when the visible Mixed id is offset by the PS2-list length.
        if (mod->support->mode == APP_MODE || wantedView == LIB_VIEW_PS1 ||
            host::library().listSourceId(mod->support, cur->item.id) == sourceId)
            return cur;
    }
    return NULL;
}

static void favVcdMarkStar(opl_io_module_t *mod, int id, const char *text)
{
    submenu_list_t *src = favFindVisibleSource(mod, id, text, LIB_VIEW_PS1);
    if (src != NULL)
        src->item.favourited = 1;
}

// An APP favourite's stored id is the row index into the single AGGREGATED appsList (legacy
// conf_apps entries first, then the device scan in mount order) -- add a stick, edit conf_apps, or
// change APPS folders and every later index shifts, so a strict id+text match dies FOREVER for those
// records. Games never have this problem (a game's id is stable within its device list, and BDM even
// gets a lenient cross-slot re-match). For apps the stable identity is the TITLE (the conf_apps key /
// title.cfg value), so apps match by mode+text and ignore the stored id everywhere: resolve (below),
// the add-time duplicate check, and removal. Nathan's "Apps didn't seem to have functioning
// R3/Favorites" (HW, 2026-07-16).
static int favIdsMatchForMode(int mode, int recId, int liveId)
{
    return (mode == APP_MODE) || (recId == liveId);
}

static int favKindView(int kind)
{
    switch (kind) {
        case FAV_KIND_VCD:
        case FAV_KIND_CUE:
            return LIB_VIEW_PS1; // both PS1 cores share one shelf, as they do on a device page
        case FAV_KIND_ELF:
            return LIB_VIEW_ELF;
        default:
            return LIB_VIEW_ISO;
    }
}

// A PS1 favourite is name-addressed (art, config and launch all key off the stored text) and so is
// view-independent; an ISO or ELF one is id-addressed against its source's list.
static int favKindIsNameAddressed(int kind)
{
    return kind == FAV_KIND_VCD || kind == FAV_KIND_CUE;
}

static int favResolveStoredId(item_list_t *source, int id, const char *text, int kind, int *outId)
{
    if (source == NULL || text == NULL || outId == NULL)
        return 0;

    // ETH keeps permanent independent PS2/PS1 backing arrays. Use its direct resolver for the PS2
    // identity so Favorites never depends on the source page's current view or visible mixed index.
    if (source->mode == ETH_MODE && kind == FAV_KIND_ISO)
        return ethResolveIsoFavourite(id, text, outId);

    item_list_t view = *source;
    view.viewOverride = favKindIsNameAddressed(kind) ? ITEM_VIEW_FORCE_PS1 : ITEM_VIEW_FORCE_ISO;
    int count = gsOf(&view)->count(&view);

    // APP ids are aggregate-list positions and can move; their stable identity is the title.
    if (source->mode == APP_MODE) {
        for (int i = 0; i < count; i++) {
            char *name = gsOf(&view)->name(&view, i);
            if (name != NULL && strcmp(name, text) == 0) {
                *outId = i;
                return 1;
            }
        }
        return 0;
    }

    if (id < 0 || id >= count)
        return 0;
    char *name = gsOf(&view)->name(&view, id);
    if (name == NULL || strcmp(name, text) != 0)
        return 0;
    *outId = id;
    return 1;
}

// Can this support launch a PS1 favourite of the wanted core by NAME?
static int favSupportLaunches(const item_list_t *support, int needCue)
{
    if (support == NULL)
        return 0;
    // Generic BDM supports carry one shared callback table, so UDPBD still has the POPSTARTER guard
    // function even though that transport cannot launch it. Do not bind a saved VCD favorite to that
    // slot merely because the pointer is non-NULL; Ember favorites remain valid there.
    if (!needCue && bdmSupportIsUDPBD(support))
        return 0;
    return needCue ? (gsOf(support)->hasLaunchCue()) : (gsOf(support)->hasLaunchVcd());
}

// markStars = 0 resolves without touching any source row's star: a Recent entry is not a favourite.
static item_list_t *favResolve(int mode, int id, const char *text, int kind, int *outMode, int *outId, int markStars = 1)
{
    *outMode = mode;
    *outId = id;

    // PS1 favourites are name-addressed and already view-independent: art/config/launch all key off
    // the stored name. Bind one to a loaded source that can launch ITS core -- a POPSTARTER
    // favourite needs itemLaunchVcd, an Ember one needs itemLaunchCue -- without disturbing that
    // source page's current view. Binding a CUE favourite to a device with no Ember support would
    // produce a row whose X button cannot work.
    if (favKindIsNameAddressed(kind)) {
        const int needCue = (kind == FAV_KIND_CUE);
        if (mode >= BDM_MODE && mode <= BDM_MODE_LAST) {
            opl_io_module_t *mod = oplGetModule(mode);
            if (mod != NULL && mod->support != NULL && favSupportLaunches(mod->support, needCue)) {
                if (markStars)
                    favVcdMarkStar(mod, id, text);
                return mod->support;
            }
            for (int m = BDM_MODE; m <= BDM_MODE_LAST; m++) {
                opl_io_module_t *bm = oplGetModule(m);
                if (bm != NULL && bm->support != NULL && favSupportLaunches(bm->support, needCue)) {
                    *outMode = m;
                    if (markStars)
                        favVcdMarkStar(bm, id, text);
                    return bm->support;
                }
            }
            return NULL;
        }
        if (mode < 0 || mode >= MODE_COUNT)
            return NULL;
        opl_io_module_t *mod = oplGetModule(mode);
        if (mod == NULL || mod->support == NULL || !favSupportLaunches(mod->support, needCue))
            return NULL;
        if (markStars)
            favVcdMarkStar(mod, id, text);
        return mod->support;
    }

    // ISO/DVD/CD favourites must be just as independent. Validate against a forced-ISO shallow
    // support view, which reads the source's retained ISO backing array even when its visible page
    // is currently VCD. Only mark a live submenu star when that submenu itself is in ISO view.
    if (mode >= BDM_MODE && mode <= BDM_MODE_LAST) {
        for (int m = BDM_MODE; m <= BDM_MODE_LAST; m++) {
            opl_io_module_t *mod = oplGetModule(m);
            if (mod == NULL || mod->support == NULL)
                continue;
            int liveId = id;
            if (favResolveStoredId(mod->support, id, text, 0, &liveId)) {
                submenu_list_t *src = markStars ? favFindVisibleSource(mod, liveId, text, LIB_VIEW_ISO) : NULL;
                if (src != NULL)
                    src->item.favourited = 1;
                *outMode = m;
                *outId = liveId;
                return mod->support;
            }
        }
        return NULL;
    }

    if (mode < 0 || mode >= MODE_COUNT)
        return NULL;
    opl_io_module_t *mod = oplGetModule(mode);
    if (mod == NULL || mod->support == NULL)
        return NULL;

    int liveId = id;
    if (!favResolveStoredId(mod->support, id, text, 0, &liveId))
        return NULL;

    submenu_list_t *src = markStars ? favFindVisibleSource(mod, liveId, text, mode == APP_MODE ? LIB_VIEW_ELF : LIB_VIEW_ISO) : NULL;
    if (src != NULL)
        src->item.favourited = 1;
    *outId = liveId;
    return mod->support;
}

// Guard the stored source id against the owner's CURRENT count -- the source list may have
// shrunk / re-scanned since the favourite was validated, so re-check before every proxy call
// to avoid indexing the owner's game array out of bounds.
static int favOwnerHasId(item_list_t *o, int id)
{
    return (o != NULL && id >= 0 && id < gsOf(o)->count(o));
}

// Two modes "match" for favourite identity if equal, OR both in the BDM range
// (USB/iLink/MX4SIO/ATA slots are interchangeable -- a BDM favourite can move slots).
static int favModesMatch(int a, int b)
{
    int aBdm = (a >= BDM_MODE && a <= BDM_MODE_LAST);
    int bBdm = (b >= BDM_MODE && b <= BDM_MODE_LAST);
    if (aBdm && bBdm)
        return 1;
    return a == b;
}

// ---- the store ----------------------------------------------------------------

void FavSupport::clear()
{
    if (array_ != NULL) {
        for (int i = 0; i < count_; i++) {
            free(array_[i].text);
            free(array_[i].display);
        }
        free(array_);
        array_ = NULL;
    }
    count_ = 0;
}

void FavSupport::markStars(const fav_raw_t *recs, int rawCount)
{
    for (int i = 0; i < rawCount; i++) {
        int resolvedMode, resolvedId;
        favResolve(recs[i].mode, recs[i].id, recs[i].text, recs[i].kind, &resolvedMode, &resolvedId);
    }
}

int FavSupport::ingest(const fav_raw_t *recs, int rawCount, bool recent)
{
    array_ = (fav_rec_t *)calloc(rawCount, sizeof(fav_rec_t));
    if (array_ == NULL)
        return 0;

    // The Recent stop shows every kind, newest first, and lights no stars. The All Games ring's
    // Favourites stop shows every kind too.
    int view = host::library().viewActive(FAV_MODE);
    int favShelf = (recent || view == LIB_VIEW_FAVS) ? LIB_VIEW_ALL : view;

    // Resolve EVERY record before filtering the Favorites tab's independent view. Resolution
    // also restores the star on the source submenu. Filtering first meant a saved VCD record was never
    // resolved while FAV itself was in ISO view, so rebuilding the HDD list on an L3 toggle erased the
    // visible star and loadFavourites() could not put it back (#495).
    for (int i = 0; i < rawCount; i++) {
        int resolvedMode = recs[i].mode;
        int resolvedId = recs[i].id;
        item_list_t *owner = favResolve(recs[i].mode, recs[i].id, recs[i].text, recs[i].kind, &resolvedMode, &resolvedId, !recent);

        // The FAV page keeps its own L3 ring (All / PS2 / PS1 / ELF): resolution above is global
        // star/state reconciliation. All accepts every kind; the other stops filter one shelf.
        if (favShelf != LIB_VIEW_ALL && favKindView(recs[i].kind) != favShelf)
            continue;

        if (owner == NULL) {
            if (recs[i].mode == APP_MODE)
                armAppsScan();
            continue; // device not loaded / item absent -> hidden (kept in the file)
        }

        char *txt = favStrdup(recs[i].text);
        if (txt == NULL)
            continue; // OOM -> skip this record rather than store a NULL name

        array_[count_].owner = owner;
        array_[count_].mode = resolvedMode;
        array_[count_].id = resolvedId;
        array_[count_].icon_id = recs[i].icon_id;
        array_[count_].text_id = recs[i].text_id;
        array_[count_].kind = recs[i].kind;
        array_[count_].text = txt;
        count_++;
    }

    return count_;
}

void FavSupport::armAppsScan()
{
    // APPS never populate on their own: gAPPStartMode defaults to MANUAL and nothing scans a
    // MANUAL tab until the user opens it, so every stored app favourite sat hidden ("device
    // not loaded") until the APPS tab happened to be visited that session -- while game
    // favourites resolved because the user starts their game tab every time. If the user has
    // app favourites, they've opted in: arm the apps scan ONCE per boot ourselves. appInit is
    // plain state init (safe on this IO thread, where config IO already runs), and the
    // deferred update it queues re-calls loadFavourites when the list lands (opl.c
    // menuDeferredUpdate), so the stars light through the normal resync -- no special path.
    opl_io_module_t *appMod = oplGetModule(APP_MODE);
    if (!appsArmed_ && appMod != NULL && appMod->support != NULL && !appMod->support->enabled) {
        appsArmed_ = 1;
        gsOf(appMod->support)->init(appMod->support);
        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &appMod->support->mode);
    }
}

int FavSupport::itemSourceMode(int id) const
{
    return validIndex(id) ? array_[id].mode : -1;
}

int FavSupport::itemKind(int id) const
{
    return validIndex(id) ? array_[id].kind : -1;
}

int FavSupport::itemView(int id) const
{
    return validIndex(id) ? favKindView(array_[id].kind) : -1;
}

const char *FavSupport::itemDisplay(int id) const
{
    if (!validIndex(id))
        return "";
    return array_[id].display != NULL ? array_[id].display : array_[id].text;
}

int FavSupport::itemStarred(int id) const
{
    return validIndex(id) ? array_[id].starred : 0;
}

// Resolve an art-cache `value` (a source item's startup, or a VCD favourite's .VCD name /
// strict PS1 id) to the favourite's SOURCE device mode, using the same matching FavSource::image
// uses to route the actual read. Lets texcache apply MMCE idle deferral, abort,
// and load priority to the device the read really lands on instead of the FAV
// wrapper mode. Returns -1 when no favourite matches (e.g. a theme attribute-image
// value, which takes the passthrough path in FavSource::image). Pure in-memory scan, no IO.
//
// Thread safety: callers are GUI-thread render paths (prio 31), but the store array is rebuilt
// UNLOCKED here (from FavSource::update) on the prio-30 IO worker (menuDeferredUpdate). Safe today
// because the IO thread outranks the GUI thread (a rebuild in progress keeps us off-CPU),
// clear() NULLs the array/count before the rebuild's first blocking point (favReadFile)
// so a scan started around a rebuild hits the live()==false guard, entries become visible only after
// full population (count_++ last), and the FAV submenu rows that drive these scans are torn
// down (clearMenuGameList) before the rebuild starts. Do NOT raise the GUI thread's priority
// to/above the IO worker's or add a yield inside clear() without adding real
// synchronization here.
int FavSupport::artMode(const char *value)
{
    if (!live() || value == NULL)
        return -1;
    // All Games can hold thousands of rows and this runs for every art request, so search outward
    // from the last match: the rows asked about next are almost always its neighbours.
    if (artHint_ >= count_)
        artHint_ = 0;
    for (int step = 0; step < 2 * count_; step++) {
        int i = artHint_ + ((step & 1) ? -(step + 1) / 2 : step / 2);
        if (i < 0 || i >= count_)
            continue;
        item_list_t ownerView;
        item_list_t *o = this->ownerView(i, &ownerView);
        if (o == NULL)
            continue;
        if (favKindIsNameAddressed(array_[i].kind)) {
            if (strcmp(array_[i].text, value) != 0) {
                char fallbackKey[VCD_ID_MAX];
                if (!vcdExtractGameId(array_[i].text, fallbackKey, sizeof(fallbackKey)) ||
                    strcmp(fallbackKey, value) != 0)
                    continue;
            }
            artHint_ = i;
            return array_[i].mode;
        }
        if (!favOwnerHasId(o, array_[i].id))
            continue;
        char *s = gsOf(o)->startup(o, array_[i].id);
        if (s != NULL && strcmp(s, value) == 0) {
            artHint_ = i;
            return array_[i].mode;
        }
    }
    return -1;
}

unsigned char FavSupport::selectedItemFlags()
{
    opl_io_module_t *mod = oplGetModule(FAV_MODE);
    if (mod == NULL || mod->menuItem.current == NULL)
        return 0;
    int id = mod->menuItem.current->item.id;
    if (!validIndex(id))
        return 0;
    // Prefer the SOURCE list's live flags so dynamic capabilities are forwarded -- e.g. a BDM
    // device backed by ATA only sets MODE_FLAG_COMPAT_DMA on itemList->flags after its scan, so
    // re-deriving from mode alone would hide the DMA compat option for ATA-backed favourites.
    item_list_t ownerView;
    item_list_t *o = this->ownerView(id, &ownerView);
    if (o != NULL)
        return o->flags;
    // Fallback by resolved mode if the owner pointer is somehow absent.
    int m = array_[id].mode;
    if (m == APP_MODE)
        return MODE_FLAG_NO_COMPAT | MODE_FLAG_NO_UPDATE;
    if (m == HDD_MODE)
        return MODE_FLAG_COMPAT_DMA;
    return 0;
}

// Build a stack-local view of a favourite's LIVE source support. Only viewOverride changes; priv,
// callbacks, flags and owner all come from the current source object. This lets Favourites keep its
// ISO/VCD split independent from the source page's own L3 state without copying or mutating device
// state. The returned pointer is valid only as long as `view` remains in scope.
item_list_t *FavSupport::ownerView(int id, item_list_t *view)
{
    if (!validIndex(id) || view == NULL || array_[id].owner == NULL)
        return NULL;

    *view = *array_[id].owner;
    // A PS1 favourite reads its source's retained PS1 array (which holds both cores' rows) even
    // while that source page shows PS2; anything else reads the source's own base list. An ELF
    // favourite's source is the APPS tab, which has one list and ignores the override entirely.
    view->viewOverride = favKindIsNameAddressed(array_[id].kind) ? ITEM_VIEW_FORCE_PS1 : ITEM_VIEW_FORCE_ISO;
    return view;
}

item_list_t *FavSupport::sourceItem(int id, item_list_t *view, int *sourceId)
{
    item_list_t *owner = ownerView(id, view);

    if (owner == NULL || sourceId == NULL || !favResolveStoredId(owner, array_[id].id, array_[id].text, array_[id].kind, sourceId))
        return NULL;
    return owner;
}

int FavSupport::addItem(int mode, int id, int icon_id, int text_id, const char *text, int kind)
{
    if (text == NULL || text[0] == '\0')
        return 0;

    int count = 0, editable = 0;
    fav_raw_t *recs = favReadFile(&count, &editable); // NULL is not necessarily an empty/new file
    if (!editable) {
        free(recs);
        return 0;
    }

    // Already present (same mode + id + text + kind) -> treat as success (the star stays set).
    // Use favModesMatch so a BDM favourite that moved slots (e.g. BDM_MODE -> BDM_MODE1) is
    // recognised as already-present, matching the BDM-lenient logic in removeByIdAndText.
    // kind is part of the identity so a disc favourite and a PS1 favourite never collide.
    for (int i = 0; i < count; i++) {
        // favIdsMatchForMode: apps identify by title (their stored id shifts with the device set) --
        // without it, a shifted app would collect a DUPLICATE record on every re-favourite.
        if (favModesMatch(recs[i].mode, mode) && favIdsMatchForMode(mode, recs[i].id, id) && recs[i].kind == kind && strcmp(recs[i].text, text) == 0) {
            free(recs);
            return 1;
        }
    }

    int newCount = count + 1;
    if (newCount > FAV_MAX_ITEMS) {
        free(recs);
        return 0;
    }
    fav_raw_t *out = (fav_raw_t *)calloc(newCount, sizeof(fav_raw_t));
    if (out == NULL) {
        free(recs);
        return 0;
    }
    for (int i = 0; i < count; i++)
        out[i] = recs[i];
    out[count].mode = mode;
    out[count].id = id;
    out[count].icon_id = icon_id;
    out[count].text_id = text_id;
    out[count].kind = kind;
    snprintf(out[count].text, FAV_TEXT_MAX, "%s", text);

    int ok = favWriteFile(out, newCount);
    free(out);
    free(recs);
    return ok;
}

int FavSupport::removeByIdAndText(int mode, int id, const char *text, int kind)
{
    int count = 0, editable = 0;
    fav_raw_t *recs = favReadFile(&count, &editable);
    if (!editable || recs == NULL) {
        free(recs);
        return 0;
    }

    int survivors = 0;
    for (int i = 0; i < count; i++) {
        // Match on id + text + mode (BDM-lenient) + kind so a same-titled favourite in a different
        // device mode -- or a disc vs PS1 favourite of the same title -- is NOT collaterally deleted.
        // Apps match by title alone (favIdsMatchForMode): their stored id shifts with the device set,
        // and without the leniency R3 on an already-starred app could not un-favourite it.
        if (!(favIdsMatchForMode(mode, recs[i].id, id) && text != NULL && strcmp(recs[i].text, text) == 0 && favModesMatch(recs[i].mode, mode) && recs[i].kind == kind))
            recs[survivors++] = recs[i]; // compact in place (single buffer)
    }
    int ok = favWriteFile(recs, survivors); // survivors may be 0 -> writes a header-only (empty) file
    free(recs);
    return ok;
}

int FavSupport::removeByIndex(int favIndex)
{
    if (!validIndex(favIndex))
        return 0;
    int srcMode = array_[favIndex].mode;
    int srcId = array_[favIndex].id;
    int srcKind = array_[favIndex].kind;
    char *txt = array_[favIndex].text;

    // Commit the removal before changing the visible source star. A failed write must not look
    // successful; an absent source list is harmless once the stored record has been removed.
    if (!removeByIdAndText(srcMode, srcId, txt, srcKind))
        return 0;

    opl_io_module_t *mod = oplGetModule(srcMode);
    if (mod != NULL) {
        submenu_list_t *src = favFindVisibleSource(mod, srcId, txt, favKindView(srcKind));
        if (src != NULL)
            src->item.favourited = 0;
    }
    return 1;
}

// ---- item_list_t callbacks ----------------------------------------------------

class FavSource: public GameSource
{
public:
    FavSource()
        : GameSource(FAV_MODE, -1, 0, MENU_MIN_INACTIVE_FRAMES, FAV_MODE_UPDATE_DELAY, GameSource::OMIT_PREFIX | GameSource::OMIT_GAME | GameSource::CAP_ROW_VIEW | GameSource::CAP_SAVE_CUE_SETTINGS)
    {
    }

    int textId(item_list_t *itemList) override;
    int iconId(item_list_t *itemList) override;
    int needsUpdate(item_list_t *itemList) override;
    void init(item_list_t *itemList) override;
    int update(item_list_t *itemList) override;
    int count(item_list_t *itemList) override;
    int rowView(item_list_t *itemList, int id) override;
    int saveCueSettings(item_list_t *itemList, int id, const char *name, config_set_t *configSet) override;
    char *rowPrefix(item_list_t *itemList, int id) override;
    int ps1SaveDir(item_list_t *itemList, int id, const char *name, int *ember, char *directory, int directorySize,
                   char *partition, int partitionSize) override;
    char *name(item_list_t *itemList, int id) override;
    int nameLength(item_list_t *itemList, int id) override;
    char *startup(item_list_t *itemList, int id) override;
    config_set_t *config(item_list_t *itemList, int id) override;
    void launch(item_list_t *itemList, int id, config_set_t *configSet) override;
    int image(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm) override;
    void deleteGame(item_list_t *itemList, int id) override;
    void rename(item_list_t *itemList, int id, char *newName) override;
    void cleanUp(item_list_t *itemList, int exception) override;
    void shutdown(item_list_t *itemList) override;
    int checkVmc(item_list_t *itemList, char *name, int createSize) override;
    int createVmc(item_list_t *itemList, char *name, int createSize, int requireContiguous) override;
};

// All Games: these Favourites-page stops list every source's games instead of favourites.bin.
static bool favViewListsAllGames(int view)
{
    return gAllGames && (view == LIB_VIEW_ALL || view == LIB_VIEW_ISO || view == LIB_VIEW_PS1 || view == LIB_VIEW_ELF);
}

int favListsAllGames(void)
{
    return favViewListsAllGames(host::library().viewActive(FAV_MODE));
}

static FavSource *favInstance()
{
    static FavSource *instance = NULL;
    alignas(FavSource) static unsigned char storage[sizeof(FavSource)];

    if (instance == NULL)
        instance = new (storage) FavSource();
    return instance;
}

int FavSource::textId(item_list_t *itemList) { return gAllGames ? _STR_ALL_GAMES : _STR_FAV; }
int FavSource::iconId(item_list_t *itemList) { return FAV_ICON; }

int FavSource::needsUpdate(item_list_t *itemList)
{
    // Consume the FAV L3 ISO<->VCD dirty flag UNCONDITIONALLY (and first) so a concurrent
    // rebuild request also clears it -- otherwise a default-view change that raises both would
    // trigger one redundant byte-identical re-read on the following pass.
    int viewToggled = host::library().consumeDirty(FAV_MODE);
    if (gFavSupport.consumeRebuildRequest()) {
        return 1;
    }
    return viewToggled; // L3 toggled the FAV view -> rebuild so the list re-filters
}

void FavSource::init(item_list_t *itemList)
{
    itemList->enabled = 1;
}

// MUST drive a single append pass: rebuild the validated in-memory array and return its
// count. updateMenuFromGameList does the clear + append from this count -- we never clear or
// append the submenu here (that would double-drive the list).
int FavSource::update(item_list_t *itemList)
{
    gFavSupport.clear();

    int rawCount = 0;
    fav_raw_t *recs = favReadFile(&rawCount);
    int view = host::library().viewActive(FAV_MODE);
    bool recent = view == LIB_VIEW_RECENT;
    bool allGames = favViewListsAllGames(view);
    // The Recent stop lists recents.bin and the All Games stops list every source, but favourites
    // still own the stars on the source pages (#495): resolve them for that first.
    if ((recent || allGames) && recs != NULL)
        gFavSupport.markStars(recs, rawCount);
    if (allGames) {
        int n = gFavSupport.gatherAllGames(view, recs, recs != NULL ? rawCount : 0);
        free(recs);
        return n;
    }
    if (recent) {
        free(recs);
        recs = favReadFile(&rawCount, NULL, kRecentFile);
    }
    if (recs == NULL)
        return 0;

    int n = gFavSupport.ingest(recs, rawCount, recent);
    free(recs);
    return n;
}

int FavSource::count(item_list_t *itemList) { return gFavSupport.itemCount(); }

// Source device mode (APP_MODE / HDD_MODE / BDM range / ...) of the FAV item at FAV-list index
// id, or -1 if id is out of range. Lets the theme engine redirect e.g. an APP favourite to the
// apps element (correct art folder + case overlay) instead of the game cover element.
int favGetItemSourceMode(int id)
{
    return gFavSupport.itemSourceMode(id);
}

int FavSource::rowView(item_list_t *itemList, int id)
{
    (void)itemList;
    return gFavSupport.itemView(id);
}

char *FavSource::name(item_list_t *itemList, int id)
{
    const fav_rec_t *fav = gFavSupport.entry(id);
    return fav != NULL ? fav->text : const_cast<char *>("");
}

int FavSource::nameLength(item_list_t *itemList, int id)
{
    const fav_rec_t *fav = gFavSupport.entry(id);
    return fav != NULL ? ((int)strlen(fav->text) + 1) : 0;
}

char *FavSource::startup(item_list_t *itemList, int id)
{
    const fav_rec_t *fav = gFavSupport.entry(id);
    if (fav == NULL)
        return "";
    if (favKindIsNameAddressed(fav->kind))
        return fav->text; // PS1 favourites key art/launch off the stored name, not a submenu id
    item_list_t ownerView;
    item_list_t *o = gFavSupport.ownerView(id, &ownerView);
    if (o == NULL || !favOwnerHasId(o, fav->id))
        return "";
    return gsOf(o)->startup(o, fav->id);
}

config_set_t *FavSource::config(item_list_t *itemList, int id)
{
    const fav_rec_t *fav = gFavSupport.entry(id);
    if (fav == NULL)
        return NULL;
    item_list_t ownerView;
    item_list_t *o = gFavSupport.ownerView(id, &ownerView);
    if (o == NULL)
        return NULL;
    // VCD favourite: the owner's id-based config is the disc list (wrong list / wrong id while the
    // device is in ISO view). Build the PS1 config straight from the .VCD name + the device prefix,
    // exactly as sbPopulateConfig keys VCD per-game data by filename -> gives Title + #DiscType badge.
    if (favKindIsNameAddressed(fav->kind)) {
        char *prefix = (gsOf(o)->hasPrefix()) ? gsOf(o)->prefix(o) : NULL;
        if (prefix == NULL)
            return NULL;
        int pl = (int)strlen(prefix);
        char sep[2] = {(pl > 0 && prefix[pl - 1] == '\\') ? '\\' : '/', '\0'};
        base_game_info_t game;
        memset(&game, 0, sizeof(game));
        snprintf(game.name, sizeof(game.name), "%s", fav->text);
        // The extension is the ROW-KIND discriminator, exactly as on a device page: stamp the
        // core this favourite actually belongs to, or an Ember favourite would describe itself as a
        // POPSTARTER one to every consumer that inspects the row.
        snprintf(game.extension, sizeof(game.extension), "%s",
                 (fav->kind == FAV_KIND_CUE) ? CUE_ROW_EXTENSION : ".VCD");
        game.parts = 1;
        game.format = GAME_FORMAT_ISO; // matches the device scans; the extension drives the PS1 badge
        config_set_t *config = sbPopulateConfig(&game, prefix, sep);
        gsOf(o)->ps1Size(o, &game, config); // the owner's PS1 root; a stack copy, so per Info open
        return config;
    }
    if (!favOwnerHasId(o, fav->id))
        return NULL;
    return gsOf(o)->config(o, fav->id);
}

void FavSource::launch(item_list_t *itemList, int id, config_set_t *configSet)
{
    const fav_rec_t *fav = gFavSupport.entry(id);
    if (fav == NULL)
        return;
    item_list_t ownerView;
    item_list_t *o = gFavSupport.ownerView(id, &ownerView);
    if (o == NULL)
        return;
    // PS1 favourite: hand the stored name to the owner's launcher for THIS row's core. Being
    // name-addressed, it works while the source page shows PS2 -- unlike the id-based itemLaunch,
    // whose PS1 branch is gated on the device being live in its PS1 view. favResolve only binds a
    // PS1 favourite to a device that provides the matching hook, so neither NULL check below should
    // fire for a resolved favourite.
    if (favKindIsNameAddressed(fav->kind)) {
        if (fav->kind == FAV_KIND_CUE) {
            if (gsOf(o)->hasLaunchCue())
                gsOf(o)->launchCue(o, fav->text, configSet);
        } else {
            if (gsOf(o)->hasLaunchVcd())
                gsOf(o)->launchVcd(o, fav->text, configSet);
        }
        return;
    }
    if (!favOwnerHasId(o, fav->id))
        return;
    gsOf(o)->launch(o, fav->id, configSet);
}

// Art proxy: the cache passes the source item's startup as `value`. Find the favourite whose
// source startup matches and forward to its owner's image lookup.
int FavSource::image(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    if (!gFavSupport.live() || value == NULL)
        return -1;
    for (int i = 0; i < gFavSupport.itemCount(); i++) {
        const fav_rec_t *fav = gFavSupport.entry(i);
        if (fav == NULL)
            continue; // store cleared mid-scan by the higher-prio IO worker (see artMode's note)
        item_list_t ownerView;
        item_list_t *o = gFavSupport.ownerView(i, &ownerView);
        if (o == NULL)
            continue;
        // PS1 favourite: route both the primary name and the cache's strict-ID fallback through the
        // OWNER device's normal ART path, which already resolves the per-core in-library fallback by
        // row kind. The disc-ID fallback simply never matches an Ember folder name, which is correct.
        if (favKindIsNameAddressed(fav->kind)) {
            if (strcmp(fav->text, value) != 0) {
                char fallbackKey[VCD_ID_MAX];
                if (!vcdExtractGameId(fav->text, fallbackKey, sizeof(fallbackKey)) ||
                    strcmp(fallbackKey, value) != 0)
                    continue;
            }
            return gsOf(o)->image(o, folder, isRelative, value, suffix, resultTex, psm);
        }
        if (!favOwnerHasId(o, fav->id))
            continue;
        char *s = gsOf(o)->startup(o, fav->id);
        if (s != NULL && strcmp(s, value) == 0) {
            int r = gsOf(o)->image(o, folder, isRelative, value, suffix, resultTex, psm);
            // Relative (ART) requests return as-is -- the matched owner IS the item's owner. For an
            // absolute (theme-folder) request a bare -1 may only mean this owner can't key the path
            // (e.g. appsupport's startup-keyed lookup with an unresolved artMode); fall through to
            // the passthrough loop below, same as an unmatched request (CodeRabbit review of #255).
            if (isRelative || r != -1)
                return r;
        }
    }
    // Attribute-image passthrough (#213): theme attribute caches (Rating/Vmode/Scan/Players...) are
    // created with an ABSOLUTE prefix (the theme path, isPrefixRelative=0, themes.c) and key their
    // request on the attribute VALUE ("4", "ntsc", ...) -- not on an item startup, so the loop
    // above can never match them. Returning -1 here made texcache brand the theme's glyph
    // "genuinely absent" (ERR_BAD_FILE) and memoize the failure for the whole session, which is why
    // the Favourites info page showed only the placeholder while the normal info page -- same value,
    // same theme file, drawn through the source list's itemGetImage -- rendered fine. `folder` is
    // the absolute theme path, so ANY resolved owner's itemGetImage loads the identical file; only
    // the interface needs an owner. ART loads (isRelative=1, startup-keyed) are untouched.
    //
    // Keep scanning past a -1 (CodeRabbit review of #255): some owners' itemGetImage is itself
    // startup-keyed (appsupport's resolves `value` via appLookupByStartup) and returns -1 for an
    // attribute VALUE even though a later, device-backed owner would load the same absolute file
    // fine -- with an APP favourite sorted first, the Favourites info page still failed. A result
    // other than plain -1 is a real signal (success, or a transient error worth surfacing over a
    // bogus "absent"); a bare -1 may just mean "this owner can't key that path".
    if (!isRelative) {
        for (int i = 0; i < gFavSupport.itemCount(); i++) {
            const fav_rec_t *fav = gFavSupport.entry(i);
            if (fav == NULL)
                continue; // store cleared mid-scan by the higher-prio IO worker (see artMode's note)
            item_list_t ownerView;
            item_list_t *o = gFavSupport.ownerView(i, &ownerView);
            if (o == NULL ||
                (!favKindIsNameAddressed(fav->kind) && !favOwnerHasId(o, fav->id)))
                continue;
            int r = gsOf(o)->image(o, folder, isRelative, value, suffix, resultTex, psm);
            if (r != -1)
                return r;
        }
    }
    return -1;
}

// Rename/Delete are blocked for favourites (also guarded at the menu level).
void FavSource::deleteGame(item_list_t *itemList, int id) {}
void FavSource::rename(item_list_t *itemList, int id, char *newName) {}

void FavSource::cleanUp(item_list_t *itemList, int exception)
{
    // Intentionally does NOT free the store array: the FAV submenu's item.text aliases the
    // array's text, and the array is freed/rebuilt only by the store rebuild (ingest(), driven
    // from update() after the submenu has been cleared) or shutdown(). Freeing here could dangle
    // a live submenu's text.
}

void FavSource::shutdown(item_list_t *itemList)
{
    gFavSupport.clear();
}

unsigned char favGetFlags(item_list_t *itemList)
{
    return gFavSupport.selectedItemFlags();
}

// VMC check proxy: itemCheckVMC is device-context (no id), so forward to the current FAV
// item's SOURCE device. Without this, the game-Options VMC menu would call a NULL callback.
static int favVmcForward(char *name, int createSize, int requireContiguous)
{
    opl_io_module_t *mod = oplGetModule(FAV_MODE);
    if (mod == NULL || mod->menuItem.current == NULL)
        return -1;
    int id = mod->menuItem.current->item.id;
    const fav_rec_t *fav = gFavSupport.entry(id);
    if (fav == NULL)
        return -1;
    item_list_t ownerView;
    item_list_t *o = gFavSupport.ownerView(id, &ownerView);
    if (o == NULL || !gsOf(o)->hasCheckVmc())
        return -1;
    return requireContiguous < 0 ? gsOf(o)->checkVmc(o, name, createSize) : gsOf(o)->createVmc(o, name, createSize, requireContiguous);
}

int FavSource::checkVmc(item_list_t *itemList, char *name, int createSize)
{
    return favVmcForward(name, createSize, -1);
}

int FavSource::createVmc(item_list_t *itemList, char *name, int createSize, int requireContiguous)
{
    return favVmcForward(name, createSize, requireContiguous);
}

// ---- public toggle / refresh API (forwarders to the store) --------------------

// FAV_KIND_* of one displayed favourite, or -1 when id is invalid (which core a PS1 favourite uses).
int favGetItemKind(int id)
{
    return gFavSupport.itemKind(id);
}

// Art-device mode for a favourite's startup/name value (see FavSupport::artMode above); texcache
// calls this on the GUI render path.
int favGetArtMode(const char *value)
{
    return gFavSupport.artMode(value);
}

int favRemoveByIndex(int favIndex)
{
    return gFavSupport.removeByIndex(favIndex);
}

void loadFavourites(void)
{
    // Mark the FAV list stale and schedule its single canonical rebuild. The clear + re-append
    // happen together inside the deferred updateMenuFromGameList (FavSource::needsUpdate consumes
    // the one-shot), so we must NOT clear here: an eager clear on every source refresh would blank
    // the tab (and reset its cursor) even when the favourites set did not change.
    gFavSupport.requestRebuild();
    ioPutRequest(IO_MENU_UPDATE_DEFFERED, &(*favInstance()).mode);
}

int addFavouriteItem(int mode, int id, int icon_id, int text_id, const char *text, int kind)
{
    return gFavSupport.addItem(mode, id, icon_id, text_id, text, kind);
}

int removeFavouriteByIdAndText(int mode, int id, const char *text, int kind)
{
    return gFavSupport.removeByIdAndText(mode, id, text, kind);
}

// ---- recently played (recents.bin) -----------------------------------------------------------
// Ported from RIPPS2 (akilluminati47) build 115's recents list. Same record format as the
// favourites, newest first, capped at RECENT_MAX_ITEMS. Each launch moves its game to the front.

int recentRecord(int mode, int id, int icon_id, int text_id, const char *text, int kind)
{
    if (text == NULL || text[0] == '\0')
        return 0;

    int count = 0, editable = 0;
    fav_raw_t *recs = favReadFile(&count, &editable, kRecentFile);
    if (!editable) { // an unreadable or foreign-version file is never replaced (as favourites.bin)
        free(recs);
        return 0;
    }
    fav_raw_t *out = (fav_raw_t *)calloc(RECENT_MAX_ITEMS, sizeof(fav_raw_t));
    if (out == NULL) {
        free(recs);
        return 0;
    }
    out[0].mode = mode;
    out[0].id = id;
    out[0].icon_id = icon_id;
    out[0].text_id = text_id;
    out[0].kind = kind;
    snprintf(out[0].text, FAV_TEXT_MAX, "%s", text);
    int n = 1;
    for (int i = 0; i < count && n < RECENT_MAX_ITEMS; i++) {
        // One row per game: same device (BDM slots are one), same shelf and core, same name. Not
        // the id: it moves when games are added, and a replay must not leave a stale duplicate.
        if (favModesMatch(recs[i].mode, mode) && recs[i].kind == kind && strcmp(recs[i].text, text) == 0)
            continue;
        out[n++] = recs[i];
    }
    int ok = favWriteFile(out, n, kRecentFile);
    free(out);
    free(recs);
    recentInvalidate();
    return ok;
}

// The ranks Sort Games: Last played reads while a list is rebuilt on the IO thread. Loaded once and
// kept until a launch is recorded or the settings folder moves. The count is published last, so a
// reader that runs mid-load sees either the old count of whole records or -1.
static fav_raw_t recentCache[RECENT_MAX_ITEMS];
static volatile int recentCacheCount = -1;
static char recentCachePath[256];

void recentInvalidate(void)
{
    recentCacheCount = -1;
}

static void recentCacheEnsure(void)
{
    char path[256];
    favGetFilePath(path, sizeof(path), kRecentFile);
    if (recentCacheCount >= 0 && strcmp(path, recentCachePath) == 0)
        return;
    recentCacheCount = -1;
    int count = 0;
    fav_raw_t *recs = favReadFile(&count, NULL, kRecentFile);
    if (count > RECENT_MAX_ITEMS)
        count = RECENT_MAX_ITEMS;
    if (recs != NULL && count > 0)
        memcpy(recentCache, recs, count * sizeof(fav_raw_t));
    free(recs);
    snprintf(recentCachePath, sizeof(recentCachePath), "%s", path);
    recentCacheCount = recs != NULL ? count : 0;
}

static int recentRank(int mode, int view, const char *text)
{
    for (int i = 0; i < recentCacheCount; i++) {
        if (favModesMatch(recentCache[i].mode, mode) && favKindView(recentCache[i].kind) == view &&
            strcmp(recentCache[i].text, text) == 0)
            return i + 1;
    }
    return 0;
}

int recentRankForRow(item_list_t *support, int id, const char *text)
{
    if (support == NULL || text == NULL)
        return 0;
    recentCacheEnsure();
    if (support->mode == FAV_MODE) {
        const fav_rec_t *fav = gFavSupport.entry(id);
        return fav != NULL ? recentRank(fav->mode, favKindView(fav->kind), fav->text) : 0;
    }
    // Rows are matched by device, shelf and name, not by id: ids move when games are added.
    int view = support->mode == APP_MODE ? LIB_VIEW_ELF : (host::library().listRowView(support, id) == LIB_VIEW_PS1 ? LIB_VIEW_PS1 : LIB_VIEW_ISO);
    return recentRank(support->mode, view, text);
}

// ---- All Games (RIPPS2 builds 114-116, akilluminati47) ---------------------------------------
// One list of every game on every started source, PS2, PS1 and apps together, built on the IO
// worker like the favourites. Each row proxies launch, config and art to the list it came from
// through the same forced PS2/PS1 view a favourite uses, so nothing below the page changes. Unlike
// RIPPS2, a title found on two sources is listed once per source, each tagged with its source.

// Source order: the order rows are gathered in, which is also the order equal titles keep.
static const int kAllGamesModes[] = {HDD_MODE, BDM_MODE, BDM_MODE1, BDM_MODE2, BDM_MODE3, BDM_MODE4, BDM_MODE5,
                                     BDM_MODE6, BDM_MODE7, MMCE_MODE, ETH_MODE, UDPFS_MODE, HTTP_MODE, APP_MODE};

// Appends one row; out == NULL only counts it.
static int favGatherAdd(fav_rec_t *out, int n, int cap, item_list_t *owner, int id, int kind, const char *name)
{
    // A hidden game is not gathered, so it cannot tag its other copies with a device name either.
    if (kind != FAV_KIND_ELF && !gShowHidden && hiddenHas(owner, favKindView(kind) == LIB_VIEW_PS1, name))
        return n;
    if (out == NULL)
        return n + 1;
    if (n >= cap)
        return n;
    char *txt = favStrdup(name);
    if (txt == NULL)
        return n; // OOM: skip this row rather than store a NULL name
    memset(&out[n], 0, sizeof(out[n]));
    out[n].owner = owner;
    out[n].mode = owner->mode;
    out[n].id = id;
    out[n].icon_id = -1;
    out[n].text_id = -1;
    out[n].kind = kind;
    out[n].text = txt;
    return n + 1;
}

// A folder-browse row lists a folder, not a game.
static bool favIsFolderRow(item_list_t *view, int id)
{
    if (!folderModeSupported(view->mode) || !gsOf(view)->hasGame())
        return false;
    base_game_info_t *game = (base_game_info_t *)gsOf(view)->game(view, id);
    return game == NULL || game->format == GAME_FORMAT_FOLDER;
}

// One pass over every started source's rows on the given shelf.
int FavSupport::gatherPass(fav_rec_t *out, int cap, int shelf)
{
    int n = 0;

    for (int mode : kAllGamesModes) {
        opl_io_module_t *mod = oplGetModule(mode);
        if (mod == NULL || mod->support == NULL)
            continue;
        item_list_t *src = mod->support;
        if (src->mode == APP_MODE && !src->enabled && gAPPStartMode != START_MODE_DISABLED &&
            (shelf == LIB_VIEW_ALL || shelf == LIB_VIEW_ELF))
            armAppsScan(); // the apps join the list once their scan lands
        if (!src->enabled)
            continue;
        // A started source keeps its rows after its device goes away (a pulled USB stick, a page
        // whose start mode was switched Off): only the page is hidden. Its page being shown is what
        // says the games are still there, so a removed device leaves the list with its tab. The
        // re-show path rebuilds the list, which gathers it again.
        if (!mod->menuItem.visible)
            continue;
        // A network share picker lists shares, not games.
        if (src->mode == ETH_MODE && !ethShareChosen())
            continue;

        if (src->mode == APP_MODE) {
            if (shelf != LIB_VIEW_ALL && shelf != LIB_VIEW_ELF)
                continue;
            // The same forced view an app favourite resolves through (favResolveStoredId).
            item_list_t view = *src;
            view.viewOverride = ITEM_VIEW_FORCE_ISO;
            int count = gsOf(&view)->count(&view);
            for (int i = 0; i < count; i++) {
                char *name = gsOf(&view)->name(&view, i);
                if (name != NULL && name[0] != '\0')
                    n = favGatherAdd(out, n, cap, src, i, FAV_KIND_ELF, name);
            }
            continue;
        }

        if (shelf == LIB_VIEW_ALL || shelf == LIB_VIEW_ISO) {
            item_list_t view = *src;
            view.viewOverride = ITEM_VIEW_FORCE_ISO;
            int count = gsOf(&view)->count(&view);
            for (int i = 0; i < count; i++) {
                if (favIsFolderRow(&view, i))
                    continue;
                char *name = gsOf(&view)->name(&view, i);
                int liveId = i;
                if (name != NULL && name[0] != '\0' && favResolveStoredId(src, i, name, FAV_KIND_ISO, &liveId))
                    n = favGatherAdd(out, n, cap, src, liveId, FAV_KIND_ISO, name);
            }
        }

        if ((shelf == LIB_VIEW_ALL || shelf == LIB_VIEW_PS1) && (favSupportLaunches(src, 0) || favSupportLaunches(src, 1))) {
            item_list_t view = *src;
            view.viewOverride = ITEM_VIEW_FORCE_PS1;
            int count = gsOf(&view)->count(&view);
            for (int i = 0; i < count; i++) {
                char *name = gsOf(&view)->name(&view, i);
                int kind = FAV_KIND_VCD;
                if (name == NULL || name[0] == '\0')
                    continue;
                if (gsOf(&view)->hasGame()) {
                    base_game_info_t *game = (base_game_info_t *)gsOf(&view)->game(&view, i);
                    if (game == NULL || game->format == GAME_FORMAT_FOLDER)
                        continue;
                    kind = cueIsCueEntry(game) ? FAV_KIND_CUE : FAV_KIND_VCD;
                }
                if (favSupportLaunches(src, kind == FAV_KIND_CUE))
                    n = favGatherAdd(out, n, cap, src, i, kind, name);
            }
        }
    }
    return n;
}

static const fav_rec_t *favDupBase; // qsort has no context argument; IO worker only
static int favDupCompare(const void *a, const void *b)
{
    const fav_rec_t *x = &favDupBase[*(const int *)a], *y = &favDupBase[*(const int *)b];
    int vx = favKindView(x->kind), vy = favKindView(y->kind);
    int c = vx != vy ? vx - vy : strcmp(x->text, y->text);
    return c != 0 ? c : *(const int *)a - *(const int *)b;
}

// A title on two sources is listed twice; name each copy's source so the two can be told apart.
static void favTagDuplicates(fav_rec_t *recs, int count)
{
    int *order = count > 1 ? (int *)malloc(count * sizeof(int)) : NULL;
    if (order == NULL)
        return; // OOM: the rows stay untagged, which only costs the hint
    for (int i = 0; i < count; i++)
        order[i] = i;
    favDupBase = recs;
    qsort(order, count, sizeof(int), favDupCompare);
    for (int i = 0; i < count;) {
        int j = i + 1;
        while (j < count && favKindView(recs[order[j]].kind) == favKindView(recs[order[i]].kind) &&
               strcmp(recs[order[j]].text, recs[order[i]].text) == 0)
            j++;
        for (int k = i; j - i > 1 && k < j; k++) {
            fav_rec_t *r = &recs[order[k]];
            const char *where = _l(gsOf(r->owner)->textId(r->owner));
            int len = (int)strlen(r->text) + (int)strlen(where) + 4;
            r->display = (char *)malloc(len);
            if (r->display != NULL)
                snprintf(r->display, len, "%s (%s)", r->text, where);
        }
        i = j;
    }
    free(order);
}

// Is this All Games row also a favourite? The same identity favourites use: mode (BDM-lenient),
// kind and name, plus the source id for a PS2 disc game.
static int favRecIsStarred(const fav_rec_t *r, const fav_raw_t *favs, int count)
{
    for (int i = 0; i < count; i++) {
        if (favs[i].kind == r->kind && favModesMatch(favs[i].mode, r->mode) &&
            (favKindIsNameAddressed(r->kind) || favIdsMatchForMode(r->mode, favs[i].id, r->id)) &&
            strcmp(favs[i].text, r->text) == 0)
            return 1;
    }
    return 0;
}

int FavSupport::gatherAllGames(int shelf, const fav_raw_t *favs, int favCount)
{
    int cap = gatherPass(NULL, 0, shelf);
    if (cap <= 0)
        return 0;
    fav_rec_t *rows = (fav_rec_t *)calloc(cap, sizeof(fav_rec_t));
    if (rows == NULL)
        return 0;
    int n = gatherPass(rows, cap, shelf);
    for (int i = 0; i < n; i++)
        rows[i].starred = favRecIsStarred(&rows[i], favs, favCount);
    favTagDuplicates(rows, n);
    // Publish only once every row is complete (see artMode's thread-safety note).
    array_ = rows;
    count_ = n;
    return count_;
}

int FavSupport::toggleByIndex(int favIndex)
{
    if (!validIndex(favIndex))
        return -1;
    fav_rec_t *r = &array_[favIndex];
    int ok = r->starred ? removeByIdAndText(r->mode, r->id, r->text, r->kind) :
                          addItem(r->mode, r->id, -1, -1, r->text, r->kind);
    if (!ok)
        return -1;
    r->starred = !r->starred;
    return r->starred;
}

const char *favGetItemDisplay(int id)
{
    return gFavSupport.itemDisplay(id);
}

int favGetItemStarred(int id)
{
    return gFavSupport.itemStarred(id);
}

int favToggleByIndex(int favIndex)
{
    return gFavSupport.toggleByIndex(favIndex);
}

item_list_t *favGetObject(int initOnly)
{
    if (initOnly && !(*favInstance()).enabled)
        return NULL;
    return &(*favInstance());
}

int FavSource::ps1SaveDir(item_list_t *itemList, int id, const char *name, int *ember, char *directory, int directorySize,
                          char *partition, int partitionSize)
{
    item_list_t ownerView;
    (void)itemList;
    const fav_rec_t *entry = gFavSupport.entry(id);
    if (entry == NULL || (entry->kind != FAV_KIND_VCD && entry->kind != FAV_KIND_CUE))
        return 0;
    item_list_t *owner = gFavSupport.ownerView(id, &ownerView);
    return owner != NULL &&
           gsOf(owner)->ps1SaveDir(owner, entry->id, name, ember, directory, directorySize, partition, partitionSize) &&
           *ember == (entry->kind == FAV_KIND_CUE);
}

char *FavSource::rowPrefix(item_list_t *itemList, int id)
{
    item_list_t ownerView;
    (void)itemList;
    const fav_rec_t *entry = gFavSupport.entry(id);
    item_list_t *owner = gFavSupport.ownerView(id, &ownerView);
    return entry != NULL && owner != NULL ? gsOf(owner)->rowPrefix(owner, entry->id) : NULL;
}

int FavSource::saveCueSettings(item_list_t *itemList, int id, const char *name, config_set_t *configSet)
{
    item_list_t ownerView;
    (void)itemList;
    const fav_rec_t *entry = gFavSupport.entry(id);
    if (entry == NULL || name == NULL || entry->kind != FAV_KIND_CUE || strcmp(entry->text, name) != 0)
        return 0;
    item_list_t *owner = gFavSupport.ownerView(id, &ownerView);
    return owner != NULL && gsOf(owner)->hasSaveCueSettings() &&
           gsOf(owner)->saveCueSettings(owner, entry->id, name, configSet);
}
