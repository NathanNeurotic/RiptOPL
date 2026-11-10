#ifndef __TAR_H
#define __TAR_H

// Lazy .tar archive loader (ART/CFG/CHT), ported from wOPL (author: mystyq).
// RiptOPL wires ONLY the ART kind, behind the user toggle gEnableArtTar (default OFF);
// the CFG/CHT kinds are kept intact so they can be wired later with no engine change.
// On-disk format is plain uncompressed ustar (512-byte blocks) so existing wOPL/sOPL
// art packs (ART/art.tar of <GAMEID>_<suffix>.png files) are compatible as-is.

#include <tamtypes.h>

#define TAR_BLOCK_SIZE 512
#define MAX_FILE_SIZE  (4 * 1024 * 1024) // 4 MiB
#define ARC_MAGIC      "ARC\0"
// 4: the v3 indexer STOPPED at the first end-of-archive marker, so a concatenated art.tar was
// indexed only up to its first member and the sidecar cached that TRUNCATED result. The sidecar
// is validated against the tar's SIZE alone (tar.c tarReadCache), which does not change, and
// tarInvalidate only drops the RAM copy -- so without a version bump every affected user would
// keep their short index forever. Bumping forces a one-time re-scan. Cost: sidecars are no longer
// interchangeable with wOPL/sOPL's, which is acceptable -- the file is a regenerable cache.
#define ARC_VERSION    4

typedef struct
{
    char magic[4];
    u32 version;
    u64 tarSize;
    u32 entryCount;
} TarCacheHeader;

typedef struct
{
    u64 offset;     // offset of file data in archive
    u32 rawSize;    // actual file size from header (octal), capped by MAX_FILE_SIZE
    u32 paddedSize; // file size rounded up to 512-byte TAR blocks
} TarEntryBase;

typedef struct
{
    TarEntryBase base;
    // [48], not [41]: the parser derives its copy length from the entry STRIDE, not from this array --
    // nameMax = entrySize - sizeof(TarEntryBase) - 1 = 64 - 16 - 1 = 47 -- so it memcpy'd 47 bytes and
    // NUL-terminated at [47], i.e. 7 bytes past the declared size. It landed in this struct's own tail
    // padding (the stride is exactly 64), so it was benign in practice, but it is out of bounds on the
    // declared type and would corrupt the next entry the moment anyone resizes this array or grows
    // TarEntryBase. [48] keeps sizeof(TarEntryArt) == 64, so entrySize, nameMax, copyLen, the on-disk
    // art_cache.bin layout and wOPL cache compatibility are all unchanged (do NOT widen further without
    // bumping ARC_VERSION -- 47 is also wOPL-base's cap, and real keys are far shorter,
    // e.g. "SLUS_203.70_COV.png" = 19).
    char filename[48]; // ART: filename up to 47 chars + null
} TarEntryArt;

typedef struct
{
    TarEntryBase base;
    char filename[20]; // CFG: "SLUS_203.70.cfg"
} TarEntryCfg;

typedef struct
{
    TarEntryBase base;
    char filename[20]; // CHT: "SLUS_203.70.cht"
} TarEntryCht;

typedef enum {
    TAR_KIND_ART = 0,
    TAR_KIND_CFG = 1,
    TAR_KIND_CHT = 2,
    TAR_KIND_THM = 3, // the active theme's archive (tarTheme* below); entries are TarEntryArt-sized
    TAR_KIND_MAX
} TarKind;

int tarLoadFromAnyDevice(TarKind kind);
int tarLoadFile(TarKind kind, const char *path);
int tarClose(TarKind kind);

TarEntryBase *tarFind(TarKind kind, const char *filename);
TarEntryBase *tarFindPrefix(TarKind kind, const char *prefix);
TarEntryBase *tarFindExact(TarKind kind, const char *filename);
TarEntryBase *tarFindPrefixExact(TarKind kind, const char *prefix);
u32 tarRead(TarKind kind, const TarEntryBase *entry, void *dst, u32 dstSize);

void tarInvalidate(TarKind kind);

const char *tarGetDevicePrefix(TarKind kind);

// A theme packed as one archive, THM/thm_<Name>.tar: the files of its folder at the top (an enclosing thm_<Name>/
// folder is accepted too). Its files are named by paths that run through the archive, e.g.
// "mass0:/THM/thm_Name.tar/sound/bgm.ogg", so the loaders build paths as for a folder and ask here first. The index
// is read once per archive (no sidecar cache: one cache name would be shared by every theme of a THM folder) and is
// not capped at MAX_FILE_SIZE, so a long bgm.ogg can stream from its place in the archive. Names are up to 47 chars.
void tarThemeInit(void); // once, before any thread loads a theme file (the index is shared by them)
// 1 when path runs through a theme archive, else 0.
int tarThemePath(const char *path);
// 0: path does not run through a theme archive (open it as usual). 1: *buf holds the member (memalign 64, a NUL
// after its last byte, the caller frees it) and *size its length. -1: the archive lacks it or cannot be read.
int tarThemeRead(const char *path, void **buf, int *size);
// The same answers; on 1 the member's place: the archive's own path, the offset of its first byte, its length.
int tarThemeSpan(const char *path, char *tarPath, int tarPathSize, u64 *offset, u32 *size);

#endif
