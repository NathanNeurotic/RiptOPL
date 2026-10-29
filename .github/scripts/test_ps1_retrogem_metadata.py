"""Execute the PS1 RetroGEM id memo, the id-keyed cover-art probe and check the Title-only CFG merge.

PS1 rows (POPStarter .VCD and Ember .CUE) look up ART/<RetroGEM id>_*.png first and fall back to the
row name, and an installer-written CFG/<id>.cfg may supply Title (original #1049). The id comes from
one session memo, keyed by (row name, core): a disc-verified id beats the filename parse, an Ember
row resolves disc 1 the way the launch barcode does (discs.txt first), and nothing does device IO
on the render or CFG paths.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
vcd = (ROOT / 'src/sources/vcdsupport.cpp').read_text()
vcd_h = (ROOT / 'include/vcdsupport.h').read_text()
elems = (ROOT / 'src/gfx/themeelems.cpp').read_text()
artload = (ROOT / 'src/gfx/artload.cpp').read_text()
texcache = (ROOT / 'src/gfx/texcache.cpp').read_text()
gamesource = (ROOT / 'src/sources/gamesource.cpp').read_text()
supportbase = (ROOT / 'src/sources/supportbase.cpp').read_text()
cue = (ROOT / 'src/sources/cuesupport.cpp').read_text()


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


klass = re.search(r'class VcdSupport\n\{.*?\n\};', vcd_h, re.S).group()
row_enum = re.search(r'enum\s*\{\s*PS1_ROW_VCD.*?\};', vcd_h, re.S).group()
memo = vcd[vcd.index('constexpr int VCD_ID_PROBE_BUDGET'):vcd.index('int ps1RowIdCached(')]
memo += function(vcd, 'int ps1RowIdCached(')
extract = function(vcd, 'int vcdExtractGameId(')
skip = function(vcd, 'static int vcdSkipPopstarterPrefix(') if 'static int vcdSkipPopstarterPrefix(' in vcd else None
assert skip is not None, 'vcdExtractGameId prefix helper moved'

MEMO_HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
constexpr int VCD_ID_MAX = 16;
constexpr int RETROGEM_GAMEID_MAX = 12;
constexpr int IO_CUSTOM_SIMPLEACTION = 1, IO_OK = 0;
char gPopstarterPath[256];
unsigned int guiFrameId;
static int artBusy, bgmBusy, ioFails, ioCalls, discReads, cueReads;
static void (*queued)(void);
static void DIntr(void) {}
static void EIntr(void) {}
static int cacheHasPendingArt(void) { return artBusy; }
static int bgmDiscretionaryIoAllowed(void) { return !bgmBusy; }
static int ioPutRequest(int type, void *fn) { assert(type == IO_CUSTOM_SIMPLEACTION); ioCalls++; if (ioFails) return 1; queued = (void (*)(void))fn; return IO_OK; }
/* The disc: a VCD image's SYSTEM.CNF id, or "" when the read fails. */
static char vcdDisc[32] = "SLUS-01234";
static char cueDisc[32] = "SCES_000.02";
static char lastPath[640];
static int retrogemGetVcdGameID(const char *path, char *out, int size) { discReads++; snprintf(lastPath, sizeof(lastPath), "%s", path); snprintf(out, size, "%s", vcdDisc); return vcdDisc[0] != '\0'; }
static int retrogemGetPs1ImageGameID(const char *path, char *out, int size) { discReads++; snprintf(lastPath, sizeof(lastPath), "%s", path); snprintf(out, size, "%s", cueDisc); return cueDisc[0] != '\0'; }
/* Disc 1 per discs.txt; the real resolver is executed by test_retrogem_ember.py. */
static int cueResolveFirstDiscImage(const char *dev, const char *name, char *out, int size) { cueReads++; snprintf(out, size, "%sEMBER/games/%s/Disc 1.cue", dev, name); return 1; }
@ROWS@
@SKIP@
@EXTRACT@
@CLASS@
VcdSupport gVcdSupport;
@MEMO@
static void run(void) { void (*fn)(void) = queued; queued = NULL; assert(fn != NULL); fn(); }
static const char *cached(const char *name, int kind) { static char id[VCD_ID_MAX]; return ps1RowIdCached(name, kind, id, sizeof(id)) ? id : NULL; }
int main(void)
{
    char id[VCD_ID_MAX];
    /* Free filename parse answers before any disc read: provisional, zero IO. */
    gVcdSupport.noteScanDir("SLUS_999.99.Crash", "mass0:/POPS");
    assert(!strcmp(cached("SLUS_999.99.Crash", PS1_ROW_VCD), "SLUS_999.99") && discReads == 0);
    assert(cached("Crash", PS1_ROW_VCD) == NULL);
    /* The caption keeps its zero-IO contract for well-named rows; metadata still verifies them. */
    vcdRequestDisplayId("SLUS_999.99.Crash");
    assert(ioCalls == 0);
    ps1RowIdRequest("SLUS_999.99.Crash", PS1_ROW_VCD);
    assert(ioCalls == 1);
    ps1RowIdRequest("SLUS_999.99.Crash", PS1_ROW_VCD); /* queued: deduped */
    assert(ioCalls == 1);
    run();
    assert(!strcmp(lastPath, "mass0:/POPS/SLUS_999.99.Crash.VCD"));
    /* The disc beats the name, canonicalized to the caption's one shape. */
    assert(!strcmp(cached("SLUS_999.99.Crash", PS1_ROW_VCD), "SLUS_012.34"));
    assert(vcdDisplayIdCached("SLUS_999.99.Crash", id, sizeof(id)) && !strcmp(id, "SLUS_012.34"));
    ps1RowIdRequest("SLUS_999.99.Crash", PS1_ROW_VCD); /* resolved: never read again */
    assert(ioCalls == 1);

    /* Kinds never cross: an Ember folder named like a VCD has its own entry, resolved from disc 1. */
    gVcdSupport.noteScanDir("SLUS_999.99.Crash", "mass0:/", PS1_ROW_CUE);
    assert(!strcmp(cached("SLUS_999.99.Crash", PS1_ROW_CUE), "SLUS_999.99"));
    ps1RowIdRequest("SLUS_999.99.Crash", PS1_ROW_CUE);
    run();
    assert(cueReads == 1 && !strcmp(lastPath, "mass0:/EMBER/games/SLUS_999.99.Crash/Disc 1.cue"));
    assert(!strcmp(cached("SLUS_999.99.Crash", PS1_ROW_CUE), "SCES_000.02"));
    assert(!strcmp(cached("SLUS_999.99.Crash", PS1_ROW_VCD), "SLUS_012.34"));
    assert(!vcdDisplayIdCached("Never", id, sizeof(id)));

    /* Busy art/BGM IO defers (no read), throttled by frame, then resolves. */
    gVcdSupport.noteScanDir("Spyro", "pfs0:/", PS1_ROW_VCD);
    artBusy = 1;
    int reads = discReads;
    ps1RowIdRequest("Spyro", PS1_ROW_VCD);
    run();
    assert(discReads == reads);
    ps1RowIdRequest("Spyro", PS1_ROW_VCD);
    assert(queued == NULL); /* inside the backoff window */
    artBusy = 0;
    guiFrameId += 91;
    ps1RowIdRequest("Spyro", PS1_ROW_VCD);
    run();
    assert(!strcmp(lastPath, "pfs0:/Spyro.VCD")); /* dir with a trailing slash: no double separator */

    /* A disc with no id retries a bounded number of times, then is absent for the session. */
    vcdDisc[0] = '\0';
    gVcdSupport.noteScanDir("Junk", "mass0:/POPS");
    for (int i = 0; i < 10; i++) {
        guiFrameId += 200;
        ps1RowIdRequest("Junk", PS1_ROW_VCD);
        if (queued)
            run();
    }
    assert(cached("Junk", PS1_ROW_VCD) == NULL);
    reads = discReads;
    guiFrameId += 200;
    ps1RowIdRequest("Junk", PS1_ROW_VCD);
    assert(queued == NULL && discReads == reads);

    /* The same row seen on another device is re-pointed and re-asked. */
    strcpy(vcdDisc, "SCUS_944.26");
    gVcdSupport.noteScanDir("Junk", "mmce0:/POPS");
    ps1RowIdRequest("Junk", PS1_ROW_VCD);
    run();
    assert(!strcmp(lastPath, "mmce0:/POPS/Junk.VCD") && !strcmp(cached("Junk", PS1_ROW_VCD), "SCUS_944.26"));

    /* A failed enqueue throttles instead of spinning. */
    ioFails = 1;
    gVcdSupport.noteScanDir("Retry", "mass0:/POPS");
    int calls = ioCalls;
    ps1RowIdRequest("Retry", PS1_ROW_VCD);
    ps1RowIdRequest("Retry", PS1_ROW_VCD);
    assert(ioCalls == calls + 1);
    ioFails = 0;

    /* Unscanned rows never touch a device; invalidation forgets everything. */
    ps1RowIdRequest("Unscanned", PS1_ROW_CUE);
    assert(queued == NULL);
    vcdInvalidateGameIds();
    assert(cached("Junk", PS1_ROW_VCD) == NULL);
    puts("PASS: PS1 id memo -- kinds, disc-first, discs.txt disc 1, dedup, deferral, absence, re-point");
    return 0;
}
'''

ART_HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { int unused; } GSTEXTURE;
typedef struct item_list item_list_t;
struct item_list { int unused; };
enum { FREE, LOADING, READY, ABSENT };
constexpr int SLOTS = 4, EPOCH = 5;
constexpr int CACHE_ID_PS1_BOTH_ABSENT = -3;
struct entry { char key[32]; int state, uid, nameFallback; };
static entry slots[SLOTS];
static GSTEXTURE tex[SLOTS];
static int nextUid = 100, claims;
static int haveId, haveName;
static const char *ID = "SLUS_012.34", *NAME = "Crash";
struct image_cache_t {
    int slotKeyedOther(int i, const char *value) const { return i >= 0 && i < SLOTS && strcmp(slots[i].key, value) != 0; }
    int holdsFallback(int i, int uid, const char *value) const
    {
        return i >= 0 && i < SLOTS && slots[i].uid == uid && slots[i].nameFallback && !strcmp(slots[i].key, value);
    }
    void markFallback(int i, int uid)
    {
        if (i >= 0 && i < SLOTS && slots[i].uid == uid)
            slots[i].nameFallback = 1;
    }
    GSTEXTURE *residentByValue(const char *value)
    {
        for (int i = 0; i < SLOTS; i++)
            if (slots[i].state == READY && !strcmp(slots[i].key, value))
                return &tex[i];
        return NULL;
    }
    int adoptFallback(const char *name, int *cacheId, int *uid, GSTEXTURE **t)
    {
        *t = NULL;
        if (*cacheId >= 0 && *cacheId < SLOTS && slots[*cacheId].uid == *uid)
            return 0;
        for (int i = 0; i < SLOTS; i++) {
            if (!slots[i].nameFallback || strcmp(slots[i].key, name) != 0)
                continue;
            if (slots[i].state == ABSENT) {
                *cacheId = CACHE_ID_PS1_BOTH_ABSENT, *uid = EPOCH;
                return 1;
            }
            if (slots[i].state == FREE)
                return 0;
            *cacheId = i, *uid = slots[i].uid;
            if (slots[i].state == READY)
                *t = &tex[i];
            return 1;
        }
        return 0;
    }
};
static int cacheAbsenceIsCurrent(int uid) { return uid == EPOCH; }
/* texcache.cpp's contract: fast path by (id, UID), a parked -2 honoured for this fail epoch,
   reunion by value (loading adopts, absent parks, ready returns), else claim a slot and queue. */
static GSTEXTURE *cacheGetTextureEx(image_cache_t *, item_list_t *, int *cacheId, int *uid, char *value, int)
{
    if (*cacheId == -2) {
        if (*uid == EPOCH)
            return NULL;
        *cacheId = -1;
    } else if (*cacheId != -1) {
        entry *e = &slots[*cacheId];
        if (e->uid == *uid) {
            if (e->state == LOADING)
                return NULL;
            if (e->state == ABSENT) {
                *cacheId = -2, *uid = EPOCH;
                return NULL;
            }
            return &tex[*cacheId];
        }
        *cacheId = -1;
    }
    for (int i = 0; i < SLOTS; i++) {
        if (strcmp(slots[i].key, value) != 0)
            continue;
        if (slots[i].state == ABSENT) {
            *cacheId = -2, *uid = EPOCH;
            return NULL;
        }
        *cacheId = i, *uid = slots[i].uid;
        return slots[i].state == READY ? &tex[i] : NULL;
    }
    for (int i = 0; i < SLOTS; i++) {
        if (slots[i].state != FREE)
            continue;
        snprintf(slots[i].key, sizeof(slots[i].key), "%s", value);
        slots[i].state = LOADING, slots[i].uid = nextUid++;
        *cacheId = i, *uid = slots[i].uid;
        claims++;
        return NULL;
    }
    assert(!"cache exhausted: the probe is thrashing");
    return NULL;
}
static void work(void)
{
    for (int i = 0; i < SLOTS; i++)
        if (slots[i].state == LOADING)
            slots[i].state = (!strcmp(slots[i].key, ID) ? haveId : haveName) ? READY : ABSENT;
}
@ART@
static GSTEXTURE *frames(int *cid, int *uid, int n)
{
    image_cache_t cache;
    item_list_t list;
    char id[16], name[16];
    GSTEXTURE *t = NULL;
    strcpy(id, ID);
    strcpy(name, NAME);
    for (int i = 0; i < n; i++) {
        t = cacheGetTextureIdOrName(&cache, &list, cid, uid, id, name, 0);
        work();
    }
    return t;
}
static void reset(int id, int name) { memset(slots, 0, sizeof(slots)); haveId = id, haveName = name, claims = 0; }
int main(void)
{
    int cid = -1, uid = 0;
    reset(1, 1); /* id art exists: it wins, one read */
    assert(frames(&cid, &uid, 5) == &tex[cid] && !strcmp(slots[cid].key, ID) && claims == 1);
    reset(0, 1), cid = -1; /* no id art: the name fallback draws, and stays drawn without re-reads */
    assert(frames(&cid, &uid, 8) == &tex[cid] && !strcmp(slots[cid].key, NAME) && claims == 2);
    /* ...even once the id's absence memo is evicted (parked memos go first on a full cache): the row
       stays on its name slot instead of re-reading the id and flashing the placeholder meanwhile */
    for (int i = 0; i < SLOTS; i++)
        if (slots[i].state == ABSENT)
            memset(&slots[i], 0, sizeof(slots[i]));
    assert(frames(&cid, &uid, 8) == &tex[cid] && !strcmp(slots[cid].key, NAME) && claims == 2);
    /* a list rebuild re-seats the row with no slot: it takes its settled name fallback back, no id re-read */
    for (int i = 0; i < SLOTS; i++)
        if (slots[i].state == ABSENT)
            memset(&slots[i], 0, sizeof(slots[i]));
    cid = -1, uid = -1;
    assert(frames(&cid, &uid, 8) == &tex[cid] && !strcmp(slots[cid].key, NAME) && claims == 2);
    reset(0, 0), cid = -1; /* neither: both parked, nothing re-queued */
    assert(frames(&cid, &uid, 8) == NULL && claims == 2 && cid == CACHE_ID_PS1_BOTH_ABSENT);
    for (int i = 0; i < SLOTS; i++) /* both memos evicted: the row's own park still holds */
        memset(&slots[i], 0, sizeof(slots[i]));
    assert(frames(&cid, &uid, 8) == NULL && claims == 2);
    reset(1, 1), cid = -1; /* name art already resident (an old row): the id still takes over */
    strcpy(slots[0].key, NAME), slots[0].state = READY, slots[0].uid = 1, cid = 0, uid = 1;
    assert(frames(&cid, &uid, 5) == &tex[cid] && !strcmp(slots[cid].key, ID));
    puts("PASS: id-keyed cover art with a stable name fallback");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    for label, source in (
        ('memo', MEMO_HARNESS.replace('@ROWS@', row_enum).replace('@SKIP@', skip).replace('@EXTRACT@', extract)
                 .replace('@CLASS@', klass).replace('@MEMO@', memo)),
        ('art', ART_HARNESS.replace('@ART@', function(texcache, 'GSTEXTURE *cacheGetTextureIdOrName('))),
    ):
        cfile = folder / (label + '.cpp')
        cfile.write_text(source)
        subprocess.run(['g++', '-std=gnu++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-Wno-unused-parameter', '-Wno-unused-variable', str(cfile), '-o', str(folder / label)], check=True)
        subprocess.run([str(folder / label)], check=True, timeout=15)

# Slot ownership is judged by the stored key (artload), not the UID.
assert 'strcmp(content_[i].key, value) != 0' in function(artload, 'int ImageCache::slotKeyedOther(')
# Only PS1 rows on device pages take the id path; favourites/apps match art against fav identities.
art_id = function(gamesource, 'int GameSource::ps1ArtId(')
assert 'FAV_MODE' in art_id and 'APP_MODE' in art_id and 'cueIsCueEntry(row)' in art_id and '".VCD"' in art_id
assert art_id.index('ps1RowIdRequest(') < art_id.index('ps1RowIdCached(')
consumer = function(elems, 'GSTEXTURE *getGameImageTextureEx(')
assert consumer.index('ps1ArtId(') < consumer.index('cacheGetTextureIdOrName(') < consumer.rindex('cacheGetTextureEx(')
# Ember rows are noted with their device root; the scan stays string-only.
fill = function(cue, 'cueFillGameListUnsorted(') if 'cueFillGameListUnsorted(' in cue else cue
assert 'gVcdSupport.noteScanDir(entries[i].name, devPrefix, PS1_ROW_CUE);' in fill
assert 'cueReadDiscList(' in function(cue, 'int cueResolveFirstDiscImage(')
# CFG metadata: Title only, read-only, never dirties the name cfg, zero disc IO (cached id only).
meta = supportbase[supportbase.index('PS1 metadata fallback'):supportbase.index('Get game size if not already set')]
assert 'ps1RowIdCached(' in meta and 'ps1RowIdRequest(' not in meta and 'retrogem' not in meta
assert re.findall(r'configSetStr\(config, "(\w+)"', meta) == ['Title']
assert 'configWrite' not in meta and 'clearModified()' in meta and 'configFree(meta)' in meta
print('PASS: art/CFG consumers -- PS1-only id path, key-owned slots, Title-only read-only CFG merge')
