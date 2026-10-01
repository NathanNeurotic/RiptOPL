#include "include/opl.h"
#include "include/texcache.h"
#include "include/texcacheint.h"
#include "include/artqueue.h"
#include "include/artworker.h"
#include "include/artindex.h"
#include "include/textures.h"
#include "include/ioman.h"
#include "include/gui.h"
#include "include/util.h"
#include "include/renderman.h"
#include "include/tar.h"
#include "include/pad.h"        // getKeyPressed -- the SIO2 nav gate, GUI thread only
#include "include/sound.h"      // BGM low-water admission: don't start art while audio reserve is critical
#include "include/bdmsupport.h" // bdmModeIsSIO2 -- is this cover on the pad's bus?
#include "include/appsupport.h" // appGetArtMode -- APP rows are proxies for another device
#include "include/favsupport.h" // favGetArtMode -- so are FAV rows
#include "include/hddsupport.h" // hddGetArtArchivePath -- exact HDD tar home
#include <kernel.h>
#include <delaythread.h> // DelayThread -- must be included explicitly, it is not pulled in by kernel.h

// Cover-art loading: the art worker thread and its lifecycle, queue cancellation, the .tar art-pack lookup and the PNG load path.
// Split out of texcache.cpp, which keeps the cache entries and the per-frame lookup.

extern void *_gp;

// Art thread priority. LOWER NUMBER WINS on the EE. BGM playback/decode are 30/31 (sound.c), the
// GUI/pad thread is 31 (opl.c), and the shared io worker is 32 (ioman.c). 72 (0x48) puts art below
// every one of them: it can never take the CPU from input, config work, list rebuilds, or audio.
constexpr int ART_THREAD_PRIORITY = 72;

// ...and lower still while reading anything on SIO2. EE priority does not speed up or slow down a
// thread blocked in an IOP RPC, so this buys nothing on the device read itself -- what it protects
// is the PNG DECODE that follows, which is tens of ms of pure EE work on the pad's own bus.
constexpr int ART_SIO2_LOAD_PRIORITY = 90;

// Matches ioman's worker stack. The identical libpng decode runs on that stack today, so this is a
// measured size rather than a guess. .bss only; it adds nothing to the ELF.

// Teardown join budgets in MILLISECONDS -- the join sleeps via DelayThread(1000) = 1 ms, NOT via
// util.c's delay(), whose "tick" is a ~0.25 s NOP spin (the units trap that made the old
// LAUNCH_IO_DRAIN_TICKS "10 s" actually forty minutes).
// LAUNCH must outlast a MISSED cover open (~4.3 s measured, vs 53 ms for a hit -- a fixed
// driver-side timeout), or a worker parked in one is abandoned; on the keep-IOP paths that leaves
// the target reading through a half-used RPC channel. The join exits as soon as the worker stops,
// so the larger cap is only ever paid against a real miss.
constexpr int ART_JOIN_MS_LAUNCH = 6000;
constexpr int ART_JOIN_MS_TERMINAL = 1500;

ArtQueue gArtQ;
ArtWorker gArtW;
// The thread stack stays a plain file-static array: inside the object it would drag all 96 KiB into .data (the
// object has non-zero initialisers), instead of .bss.
static u8 gArtStack[ART_THREAD_STACK_SIZE] __attribute__((aligned(16)));


// .tar art packs (checklist item 45). Tries "<value>_<suffix>.png" inside ART/art.tar before the
// normal per-file lookup. Opt-in: gEnableArtTar ships 0, so with the toggle off this is a single
// predictable branch and the art path is byte-for-byte the behaviour hardware has already validated.
//
// #340 note -- what this actually costs, stated in full because item 45 is a flagged suspect:
//   WHERE IT RUNS: cacheLoadImage is an ioman handler (ioRegisterHandler/ioPutRequest below), so all
//   of this executes on the IO worker thread at priority 30 -- never on the GUI/pad thread. The
//   caller also gates the enqueue behind the art idle delay (guiInactiveFrames < list->delay in
//   cacheGetTexture), so nothing here is even queued while the user is holding a direction.
//   ONE-SHOT COSTS on the first request after the toggle goes on:
//     - tarFind() lazily probes up to 13 devices (mass0-7, mmce0/1, hdd0:, pfs0:, smb0:) with one
//       open() each; mmce* and smb0: are the slow ones. tar.c's s_inactive[] latch stops a failed
//       probe from repeating.
//     - a successful parse then WRITES a sidecar index (art_cache.bin, ~64 bytes per entry) next to
//       the archive. That is a genuine write on the art path, unlike official's read-only one. It is
//       non-fatal (tarParseFile ignores the result) and happens only when the archive changes, but it
//       is the thing to instrument first if a .tar-enabled build ever feels worse than a plain one.
//   DEFAULT PATH: gEnableArtTar ships 0, so none of the above is reachable unless the user opts in.
static int artTarLoadImageExact(const char *value, const char *suffix, const char *exactPath, GSTEXTURE *texture)
{
    char prefix[64];
    TarEntryBase *entry = NULL;
    void *buffer;
    int result;

    if (snprintf(prefix, sizeof(prefix), "%s_%s.", value, suffix) >= (int)sizeof(prefix))
        return -1;

    if (tarLoadFile(TAR_KIND_ART, exactPath) < 0)
        return -1;

    entry = tarFindPrefixExact(TAR_KIND_ART, prefix);

    if (entry == NULL) {
        LOG("ART TAR: '%s_%s' not in the archive index\n", value, suffix);
        return -1;
    }

    buffer = malloc(entry->rawSize);
    if (buffer == NULL) {
        LOG("ART TAR: out of memory for '%s' (%u bytes)\n", prefix, entry->rawSize);
        return -1;
    }

    if (tarRead(TAR_KIND_ART, entry, buffer, entry->rawSize) != entry->rawSize) {
        LOG("ART TAR: short read for '%s' (%u bytes expected)\n", prefix, entry->rawSize);
        free(buffer);
        return -1;
    }

    result = texLoadFromMemory(texture, buffer, entry->rawSize);
    if (result < 0)
        LOG("ART TAR: '%s' found (%u bytes) but PNG decode failed (%d)\n", prefix, entry->rawSize, result);
    free(buffer);
    return result;
}

static int artTarLoadImage(const char *value, const char *suffix, GSTEXTURE *texture)
{
    char prefix[64];
    TarEntryBase *entry = NULL;
    void *buffer;
    int result;

    if (snprintf(prefix, sizeof(prefix), "%s_%s.", value, suffix) >= (int)sizeof(prefix))
        return -1;

    entry = tarFindPrefix(TAR_KIND_ART, prefix);

    if (entry == NULL) {
        LOG("ART TAR: '%s_%s' not in the archive index\n", value, suffix);
        return -1;
    }

    buffer = malloc(entry->rawSize);
    if (buffer == NULL) {
        LOG("ART TAR: out of memory for '%s' (%u bytes)\n", prefix, entry->rawSize);
        return -1;
    }

    if (tarRead(TAR_KIND_ART, entry, buffer, entry->rawSize) != entry->rawSize) {
        LOG("ART TAR: short read for '%s' (%u bytes expected)\n", prefix, entry->rawSize);
        free(buffer);
        return -1;
    }

    result = texLoadFromMemory(texture, buffer, entry->rawSize);
    if (result < 0)
        LOG("ART TAR: '%s' found (%u bytes) but PNG decode failed (%d)\n", prefix, entry->rawSize, result);
    free(buffer);
    return result;
}

void cacheInvalidateFailMemo(void)
{
    // Now has teeth. The callers that mean "new art may have appeared" (applyConfig, every list
    // rebuild) get what they always asked for: parked rows re-probe, and nothing else is disturbed.
    gArtW.failEpoch++;
    if (gArtW.failEpoch == 0) // 0 is the memset value in a fresh slot; never let a stale park match it
        gArtW.failEpoch = 1;
}



// Art requests currently sitting in the ioman queue. LOCK-FREE on purpose, and that is the whole
// story of this counter: the first backpressure gate here called ioGetPendingRequestCount(), whose
// WaitSema(gProcSemaId) blocks until the worker finishes draining the ENTIRE queue -- the worker
// holds that sema across the whole BATCH, not per request. Called from cacheGetTexture on the GUI
// thread, that froze the menu for as long as any art was in flight: the hardware 'everything
// turns to chit' / black-screen-after-save reports. A plain volatile int the producer increments
// and the handler decrements needs no lock; a lost update skews the gate by one for a frame, and
// the gate self-heals by resetting when the queue is observed empty.

// The load the worker is executing RIGHT NOW, which gArtQ.queued() can no longer see: the counter
// above is decremented on entry to cacheLoadImage (it balances the enqueue, once per request), so
// between that decrement and the end of the decode "queued" reads zero while the device is very
// much busy. Single writer (the IO worker), read by the GUI thread in cacheHasPendingArt.

unsigned int cacheDebugTransientFail(void)
{
    return gArtW.transientFail;
}

unsigned int cacheDebugKeyTooLong(void)
{
    return gArtW.keyTooLong;
}

void cacheDebugCounters(int *queued, int *active, int *refused, int *done)
{
    if (queued)
        *queued = gArtQ.queued();
    if (active)
        *active = gArtQ.active();
    if (refused)
        *refused = gArtW.refused;
    if (done)
        *done = gArtW.done;
}

// Requests released WITHOUT loading. The complement of gArtW.done: every request artPop hands out ends
// in exactly one of the two, so a climbing enqueue total with BOTH of these flat means a leak.
int cacheDebugDropped(void)
{
    return gArtW.dropped;
}

void cacheDebugLastLoad(int *lastMs, int *lastOkMs, int *width, int *height)
{
    if (lastMs)
        *lastMs = gArtW.lastMs;
    if (lastOkMs)
        *lastOkMs = gArtW.lastOkMs;
    if (width)
        *width = gArtW.lastWidth;
    if (height)
        *height = gArtW.lastHeight;
}

// Is any cover art queued or being read/decoded? Used by menuUpdateHook to keep background device
// rescans OUT of the shared IO queue while art is still arriving -- without it, a settle enqueues a
// batch of per-device probes that the worker serializes AHEAD of the covers the user is waiting on,
// so the art keeps falling further behind on every scroll-and-stop cycle. Lock-free by the same
// argument as gArtQ.queued(): a torn read mis-answers one frame and self-corrects on the next.
int cacheHasPendingArt(void)
{
    return (gArtQ.queued() > 0) || (gArtQ.active() > 0);
}

// LOOKUP ONLY: hand back this item's texture if its slot is still live, and never claim a slot,
// never queue a load, never touch the fail memo. For a caller that wants to keep DRAWING art it
// already has while deliberately not asking for more -- without this, "don't request" also means
// "don't draw", and a row that already had its thumbnail would drop back to the placeholder.
GSTEXTURE *cacheLookupTexture(image_cache_t *cache, int *cacheId, int *UID)
{
    if (cache == NULL || cacheId == NULL || UID == NULL)
        return NULL;

    if (*cacheId < 0 || *cacheId >= cache->count())
        return NULL;

    cache_entry_t *entry = &cache->content_[*cacheId];
    if (entry->UID != *UID || entry->qr != NULL || entry->lastUsed == 0 || entry->texture.Mem == NULL)
        return NULL;

    entry->lastUsed = guiFrameId; // still on screen: keep it away from the eviction scan
    return &entry->texture;
}


void cacheDropQueuedArt(void)
{
    gArtW.epoch++;
    // Let the worker burn the now-dead requests immediately instead of at the next enqueue. Its job
    // narrowed once art got its own thread -- covers no longer sit FIFO in front of the list rebuild
    // at all, that half is structural now -- but it still stops the DEVICE READS for a view that no
    // longer exists.
    cacheWakeArtWorker();
}

// ---------------------------------------------------------------------------------------------
// ART QUEUE. A private intrusive FIFO, guarded by a DIntr()/EIntr() bracket rather than a
// semaphore.
//
// Why not a semaphore, given the fork uses one: the fork's GUI thread takes gArtSemaId on EVERY
// cacheGetTexture call -- including plain cache hits -- and once more per frame in its pump, while
// the worker can be holding that same semaphore having raised ITSELF to priority 90 for a slow
// SIO2 read. That is an unbounded priority inversion on the render thread: the thread that reads
// your controller blocks on a lock held by the lowest-priority thread in the system, for as long as
// a cover takes. This codebase has already paid for one freeze of exactly that shape (the
// GUI-thread-must-never-take-an-ioman-semaphore rule).
//
// The bracket has none of that. Every critical section below is a few pointer stores with no call
// out to anything, so interrupts are off for a handful of instructions and no thread can ever block
// another. The expensive parts -- the device read, the PNG decode, and every free() -- happen with
// interrupts ENABLED, outside any section.
//
// RULE FOR CALLERS: never call these with interrupts already disabled; EIntr() enables
// unconditionally and would re-enable early.
// ---------------------------------------------------------------------------------------------

void cacheWakeArtWorker(void)
{
    gArtW.wake();
}

// Free a chain the caller has already detached, so nothing else can reach it. Releases each
// request's cache SLOT so the row still on screen can claim it again next frame: a slot left with
// qr set is invisible to BOTH the read path and the eviction scan for the rest of the session,
// which is a failure this file has now recorded twice.
//
// freeTxt is 0 on purpose -- see the GS/VRAM note in cacheLoadImage.
static void artFreeChain(load_image_request_t *chain)
{
    while (chain) {
        load_image_request_t *next = chain->next;

        if (chain->entry && chain->entry->qr == chain)
            cacheClearItem(chain->entry, 0);
        if (chain->cache)
            chain->cache->requestFinished();

        gArtW.dropped++;
        free(chain);
        chain = next;
    }
}

// The worker's single exit path. artPop is the ONLY place that increments gArtQ.active() and
// publishes gArtQ.current(), so this is the ONLY place that undoes either: one increment, one
// decrement, and no path in between can skip it.
static void artReleaseRequest(load_image_request_t *req, int completed)
{
    gArtQ.release(req);

    if (req->cache)
        req->cache->requestFinished();

    if (completed)
        gArtW.done++;
    else
        gArtW.dropped++;

    free(req);
}

void cacheShutdownArtLoads(void)
{
    load_image_request_t *drop;

    gArtW.loadsCancelled = 1;

    // The one request the worker already owns cannot be freed here -- it may be inside a device
    // read. Flag it so textures.c can bail at its next chunk boundary where that is possible.
    if (gArtQ.current())
        gArtQ.current()->abortRequested = 1;

    drop = gArtQ.detachAll();
    artFreeChain(drop);

    cacheWakeArtWorker(); // so a sleeping worker leaves SleepThread and observes the latch
}

// Drop everything queued and wait, bounded, for the one in-flight request to let go.
//
// WAS A NO-OP STUB, which quietly disarmed every caller: the theme-switch guard (whose "keep the
// current theme and retry" branch was therefore unreachable -- a real use-after-free, since thmFree
// -> cacheDestroyCache frees content[] while queued requests still point into it), the apps list
// rebuild, the pfs0: remount, and the LWNBD preflight. Arming them is part of the point of giving
// art its own thread: with a worker running concurrently those races stop being theoretical.
//
// waitTicks is MILLISECONDS here -- the wait is DelayThread(1000), not util.c's delay(), whose
// "tick" is a ~0.25 s NOP spin. Every existing call-site constant is sane read that way.
int cacheCancelPendingImageLoadsTimed(int waitTicks)
{
    load_image_request_t *drop;

    if (!gArtW.running)
        return 1;

    if (gArtQ.current())
        gArtQ.current()->abortRequested = 1;

    drop = gArtQ.detachAll();
    artFreeChain(drop);
    cacheWakeArtWorker();

    while (waitTicks != 0) {
        if (gArtQ.queued() == 0 && gArtQ.active() == 0)
            return 1;
        // DelayThread, not a spin: it puts THIS thread (GUI at 31, or the io worker at 32) to sleep,
        // which is exactly what lets the priority-64 art thread run at all. No priority donation --
        // the fork demotes its caller to 91 for the wait, and a wedged read then leaves the menu
        // pinned there for the rest of the session.
        DelayThread(1000);
        if (waitTicks > 0)
            waitTicks--;
    }

    return (gArtQ.queued() == 0 && gArtQ.active() == 0);
}

// The same operation restricted to SIO2 requests -- the ones sharing the pad's bus. The NAME is kept
// because five call sites use it and because MMCE rejoins the predicate untouched when checklist
// item 1 lands; today the SIO2 set is MX4SIO alone.
//
// Detach the whole queue, free the SIO2 ones, re-push the rest. Re-pushing lands them at the tail,
// so a filtered drop can reorder art against anything enqueued concurrently. Accepted on purpose:
// FIFO order among covers is a soft property -- the stale-stamp cancellation exists precisely
// because order was never a guarantee -- and the alternative is an O(n) walk with interrupts off.
int cacheAbortMmceImageLoadsTimed(int waitTicks)
{
    load_image_request_t *chain, *keepHead = NULL, *keepTail = NULL;

    if (!gArtW.running)
        return 1;

    if (gArtQ.current() && gArtQ.current()->sio2)
        gArtQ.current()->abortRequested = 1;

    chain = gArtQ.detachAll();
    while (chain) {
        load_image_request_t *next = chain->next;

        chain->next = NULL;
        if (chain->sio2) {
            artFreeChain(chain);
        } else {
            if (keepTail)
                keepTail->next = chain;
            else
                keepHead = chain;
            keepTail = chain;
        }
        chain = next;
    }
    while (keepHead) {
        load_image_request_t *next = keepHead->next;
        gArtQ.push(keepHead);
        keepHead = next;
    }

    cacheWakeArtWorker();

    while (waitTicks != 0) {
        if (!(gArtQ.current() && gArtQ.current()->sio2))
            return 1;
        DelayThread(1000);
        if (waitTicks > 0)
            waitTicks--;
    }

    return !(gArtQ.current() && gArtQ.current()->sio2);
}

// Runs on the ART THREAD, never on the GUI thread and no longer on the ioman worker. The request
// arrives already unlinked and already counted ACTIVE by artPop, so every exit below goes through
// artReleaseRequest exactly once.
static void cacheLoadImage(load_image_request_t *req)
{
    // Safeguards...
    if (!req)
        return;

    // TEARDOWN. OPL is on its way out, and this cover is for a menu guiEnd() is about to destroy --
    // reading it would spend a device access and a PNG decode on a texture nobody will ever draw.
    // The queue no longer sits in ioman's list, so ioBlockOpsTimed cannot see it at all;
    // cacheShutdownArtLoads() empties it directly and this latch catches the one request the worker
    // had already taken. That is the whole difference between a launch that hands off immediately
    // and one that first pays for every cover the user scrolled past.
    // Placed after the !req guard so the pointer is known good, before every other check so nothing
    // can slip past it, and before the the active count below like all the other early-outs.
    //
    // Leaves entry->qr pointing at the freed request, exactly as the UID-mismatch early-out below
    // already does. Checked rather than assumed: qr is only ever ASSIGNED or NULL-TESTED (texcache.c
    // 174, 301, 335, 397, 441, 470) and never dereferenced, so a stale one cannot fault -- at worst
    // it makes a slot read as "still loading" to a GUI that is being torn down in the next breath.
    // Clearing it here is not possible anyway: req->entry is not yet known non-NULL at this point.
    if (gArtW.loadsCancelled) {
        artReleaseRequest(req, 0);
        return;
    }

    // Every early-out below must FREE the request. It is a single allocation (the struct with its
    // value string appended), owned solely by this handler once ioPutRequest accepted it, and the
    // entry's qr back-pointer is cleared by cacheClearItem -- so nothing else can reach it and
    // returning without free() simply loses the memory.
    if (!req->entry || !req->cache) {
        artReleaseRequest(req, 0);
        return;
    }

    item_list_t *handler = req->list;
    if (!handler) {
        artReleaseRequest(req, 0);
        return;
    }

    // The cache entry was already reused (or cleared: cacheClearItem zeroes UID and nulls qr, which
    // is what a list rebuild or theme switch does to every slot). This request is now orphaned --
    // and it is NOT a rare path: background rescans rebuild lists while the user browses, so every
    // rebuild used to leak one of these per art load still in flight.
    if (req->cacheUID != req->entry->UID) {
        artReleaseRequest(req, 0);
        return;
    }

    // SELECTION-GENERATION CANCELLATION. Cache-sized lookahead intentionally queues deeply so the
    // next covers can already be waiting, but a new selection makes the PREVIOUS neighborhood
    // speculative waste. Drop it immediately before device IO instead of waiting up to
    // ART_CANCEL_STALE_FRAMES for row draw stamps to age out. If this request itself became selected,
    // the priority reunion path updated focusEpoch before promotion, so it survives.
    if (req->focusEpoch != gArtW.focusEpoch) {
        cacheClearItem(req->entry, 0);
        artReleaseRequest(req, 0);
        return;
    }

    // CANCELLATION. The queue is FIFO and a scroll enqueues a cover per row it passes, so by the
    // time a request reaches the front the user has usually moved on -- and reading it anyway is
    // both wasted device time and a PNG decode that preempts the priority-31 GUI/pad thread. Every
    // frame a row is drawn while its load is pending, cacheGetTexture refreshes entry->lastUsed
    // (above); a row that has scrolled away stops being drawn and its stamp goes stale. So a stale
    // stamp means nobody is waiting for this image: drop it before it costs anything, and release
    // the slot so the row that IS on screen can claim it.
    //
    // This is what "drop the old and immediately load the new" actually requires. Ordering fixes --
    // running the visible cover next, promoting it in the queue -- only choose which stale read goes
    // first; they cannot stop the stale reads existing.
    if ((u32)(guiFrameId - req->entry->lastUsed) > ART_CANCEL_STALE_FRAMES) {
        // freeTxt 0, not 1: the entry holds NO texture during the in-flight window (the enqueue
        // cleared it on the GUI thread) and rmUnloadTexture must never run off the GUI thread --
        // see the GS/VRAM note at the staged decode below.
        cacheClearItem(req->entry, 0);
        artReleaseRequest(req, 0);
        return;
    }

    // WHOLE-VIEW cancellation, which the stamp test above cannot see. A view toggle discards every
    // row at once WITHOUT any of them scrolling away, so their stamps stay fresh and each pointless
    // cover would still be read. Same release as the stale case -- cacheClearItem drops qr and frees
    // the slot, which is load-bearing: a slot left with qr set is invisible to both the read path and
    // the eviction scan for the rest of the session, so freeing the request alone would quietly kill
    // every cache slot the discarded view was using.
    if (req->epoch != gArtW.epoch) {
        cacheClearItem(req->entry, 0); // freeTxt 0: see the GS/VRAM note below
        artReleaseRequest(req, 0);
        return;
    }

    // Superseded before we ever started (the one-slot background case in cacheGetTexture). Same
    // release as the two above -- and note it clears the slot rather than parking it, because a
    // request that was cancelled says NOTHING about whether the file exists.
    if (req->abortRequested) {
        cacheClearItem(req->entry, 0);
        artReleaseRequest(req, 0);
        return;
    }

    // ---- from here the request owns a device read + PNG decode ----
    //
    // STAGED TEXTURE. The decode lands in a GSTEXTURE this THREAD owns, and only a finished image is
    // copied into the cache entry. Two things fall out of that and both are load-bearing:
    //
    //  1. GS/VRAM IS NEVER TOUCHED OFF THE GUI THREAD. cacheClearItem(item, 1) calls rmUnloadTexture
    //     -> gsKit_TexManager_free, which mutates gsKit's VRAM allocator list while the GUI thread
    //     may be inside gsKit_TexManager_bind on that same list. Today that call is REACHABLE from
    //     the io worker and is saved only by an accident: the enqueue cleared the entry with
    //     freeTxt=1 on the GUI thread first, so texture.Mem is NULL and the branch is skipped.
    //     Staging makes it unreachable BY CONSTRUCTION -- the entry stays all-zeros for the whole
    //     in-flight window, so the worker passes freeTxt=0 everywhere and cannot reach gsKit even if
    //     a later edit gets it wrong.
    //  2. A cancelled load frees its own pixels with plain texFree() and leaves the entry alone.
    //
    // The entry is exclusively ours for this window: while qr is set, cacheGetTexture returns NULL,
    // cacheLookupTexture returns NULL, and the eviction scan skips the slot. Nothing on the GUI
    // thread can read entry->texture until qr clears.
    GSTEXTURE staged;
    memset(&staged, 0, sizeof(staged));
    staged.ClutStorageMode = GS_CLUT_STORAGE_CSM1;

    int result = -1;

    // Time the load itself -- open, read off the device, decode -- with nothing else in the window.
    // This is the number that separates the two remaining explanations for slow art, which no amount
    // of reading the source can settle: if ONE cover costs a few hundred ms then the pipeline is
    // fine and the wait is the number of images we ask for, and if it costs seconds then the cost is
    // inside a single read on that device and the queue was never the story.
    clock_t loadStart = clock();

    // SIO2 PRIORITY BRACKET, around the load ONLY and restored before the publish. A thread still at
    // 90 when it touches anything the GUI also touches is the inversion this design exists to avoid.
    // EE priority neither speeds nor slows a thread blocked in an IOP RPC, so this buys nothing on
    // the device read -- what it protects is the PNG DECODE, tens of ms of pure EE work happening
    // while the pad is being polled over the very same bus.
    int sio2Bracket = req->sio2;
    if (sio2Bracket)
        ChangeThreadPriority(gArtW.threadId, ART_SIO2_LOAD_PRIORITY);

    texSetLoadAbortFlag(&req->abortRequested);

    // The archive decision was made on the GUI thread at enqueue (cacheResolveArtArchive). Nothing
    // here may re-derive it: every input to that answer is support-private state a list rebuild
    // rewrites underneath this thread.
    if (gEnableArtTar) {
        if (req->artArchive < 0)
            result = -1; // source has a home we cannot reach -- never fall back to another device's archive
        else if (req->artArchive > 0 && req->artArchivePath != NULL)
            result = artTarLoadImageExact(req->value, req->cache->suffix(), req->artArchivePath, &staged);
        else
            result = artTarLoadImage(req->value, req->cache->suffix(), &staged);
    }

    // Fall through to the per-file lookup whenever the archive is off, absent, or lacks this key.
    if (result < 0)
        result = handler->itemGetImage(handler, req->cache->prefix(), req->cache->isPrefixRelative(), req->value, req->cache->suffix(), &staged, GS_PSM_CT24);

    texSetLoadAbortFlag(NULL);

    if (sio2Bracket)
        ChangeThreadPriority(gArtW.threadId, ART_THREAD_PRIORITY);

    // ABANDONED WHILE WE WERE IN THE READ. cacheEnd() gave up waiting for this thread and deinit has
    // carried on without us -- devices are being unmounted, supports torn down, the theme freed.
    //
    // gArtW.shutdownAbandoned was SET by cacheEnd and read by nobody: another flag with no consumer.
    // The cache this request points into is protected (cacheDestroyCache leaks any cache with
    // activeRequests outstanding, which is exactly this one), but nothing protects the rest, and
    // there is no reason whatsoever to publish a cover into a menu that no longer exists. Take the
    // only safe action available: drop everything on the floor and let the thread die.
    //
    // Deliberately NOT artReleaseRequest: that walks req->cache to decrement activeRequests, and
    // leaving that count non-zero is what keeps the leak-instead-of-free branch armed.
    if (gArtW.shutdownAbandoned) {
        texFree(&staged);
        gArtQ.release(req);
        gArtW.dropped++;
        free(req);
        gArtW.terminate = 1;
        return;
    }

    // clock() is microseconds here; the elapsed form is single-wrap safe.
    gArtW.lastMs = (int)((clock() - loadStart) / 1000);
    if (result >= 0) {
        gArtW.lastOkMs = gArtW.lastMs;
        gArtW.lastWidth = (int)staged.Width;
        gArtW.lastHeight = (int)staged.Height;
    }

    // PUBLISH. The order IS the handoff protocol: fill the entry first, clear qr LAST. qr going NULL
    // is what tells the GUI thread the slot is readable, so every store it must see has to precede
    // that one. Single-core EE, so a context switch is a full sync and ordering the stores is all
    // that is required -- there is no barrier to add.
    //
    // uOPL/rebuild-66 shape on failure: park the row (lastUsed = 0 -> cacheGetTexture moves it to
    // the -2 "asked and answered" state) and do not ask again until a list rebuild. No memo, no
    // retry budget -- both existed to soften a mis-detected absence, and absence is detected
    // correctly now (rebuild-108). Retrying was also how the IO worker got pinned (rebuild-103/105).
    if (result >= 0) {
        req->entry->texture = staged;
        req->entry->lastUsed = guiFrameId;
    } else {
        texFree(&staged); // our own pixels, never the entry's -- no gsKit contact on this thread

        // ABORTED IS NOT ABSENT. texLoad honours abortRequested mid-read and returns a plain
        // failure, so parking the row here would memo "this game has no background" purely because
        // the user moved on -- and with the fail generation that memo now OUTLIVES a list rebuild.
        // Clear the slot instead: no memo, no key, and the row re-requests when it is next drawn.
        if (req->abortRequested) {
            cacheClearItem(req->entry, 0);
            artReleaseRequest(req, 0);
            return;
        }

        // TRANSIENT vs ABSENT, and this distinction is the whole of issue #388's art half.
        //
        // Every failure used to park the row as "this file does not exist". textures.c already
        // separates the two -- ERR_BAD_FILE for a real ENOENT, ERR_FILE_IO for a contended bus or a
        // share mid-reconnect -- and this function threw that away by testing only `result < 0`.
        // master does not: it parks only on ERR_BAD_FILE and gives anything else a retry.
        //
        // L10N37 on SMB: art worked on connect, then "dropping artwork altogether for a bunch of
        // games", reconnecting did not fix it, two reboots ended with none at all. A network blip
        // marked those covers missing and rebuild-155 made that verdict outlive the list rebuild
        // that used to wipe it. Nathan: SMB art is instant on the fork -- correct, because the fork
        // retries.
        //
        // A transient failure clears the slot instead of parking it, so the row asks again on its
        // next draw. It cannot spin hot: the request still has to pass the settle gate and the
        // serial worker, and a device that is genuinely gone fails its list scan too.
        if (result != ERR_BAD_FILE) {
            gArtW.transientFail++;
            cacheClearItem(req->entry, 0);
            artReleaseRequest(req, 0);
            return;
        }

        req->entry->lastUsed = 0;
        req->entry->failEpoch = req->failEpoch;
    }
    req->entry->qr = NULL;

    artReleaseRequest(req, 1);
}

// The art worker. Wake, drain, sleep -- the same shape as the io worker and the fork's, and it
// leans on the same EE kernel guarantee both do: a WakeupThread landing while this thread is still
// running accumulates, so the next SleepThread returns immediately. cacheTickArt() is a
// belt-and-braces net on top of that.
static void cacheArtWorkerThread(void *arg)
{
    (void)arg;

    // CLAIM THE ART INDEX FROM INSIDE THE THREAD, using the id this thread actually reports.
    //
    // It was previously registered as cacheInit's gArtW.threadId -- the value CreateThread returned --
    // and the index then compared that against GetThreadId() on every probe. Hardware says those two
    // did not match: the HUD read IX0/0/0, dirs AND absences AND failed-sweeps all zero, meaning the
    // owner check rejected every call and the sweep was never even attempted. Registering the id the
    // thread reports about ITSELF cannot disagree with the id it reports later, so the whole question
    // of whether those two id spaces are the same stops mattering.
    artIndexSetOwnerThread(GetThreadId());

    while (!gArtW.terminate) {
        SleepThread();

        if (gArtW.terminate)
            break;

        for (;;) {
            load_image_request_t *req = gArtQ.pop();

            if (!req)
                break; // queue empty, or its head is an SIO2 cover deferred while a direction is held

            // Device-level backpressure, not another CPU-priority tweak. Finish any read already in
            // flight, but while BGM's decoded reserve is critical do not START the next PNG open.
            // The request remains owned by this worker and cacheLoadImage re-checks stale/abort state
            // after the wait, so scrolling can still make this work evaporate before it touches disk.
            while (!gArtW.terminate && !req->abortRequested && !bgmDiscretionaryIoAllowed())
                DelayThread(10 * 1000);

            cacheLoadImage(req);

            // Yield the ready queue so other threads (such as BGM I/O) can process between loads.
            RotateThreadReadyQueue(ART_THREAD_PRIORITY);

            if (gArtW.terminate)
                break;
        }
    }

    gArtW.running = 0;
    ExitDeleteThread();
}

void cacheInit()
{
    // No ioRegisterHandler any more. Art has left the shared queue entirely, and that IS the change:
    // the deferred menu rebuild (IO_MENU_UPDATE_DEFFERED) and the config save
    // (IO_CUSTOM_SIMPLEACTION) no longer queue BEHIND a page's worth of covers on one FIFO whose
    // worker holds gProcSemaId across the whole batch. Reported from hardware as everything feeling
    // "pop-in like": the list could not become correct until the device had served art for a view
    // the user had already left.
    if (gArtW.running)
        return;

    gArtW.terminate = 0;
    gArtW.shutdownAbandoned = 0;
    gArtQ.reset();
    gArtW.focusEpoch = 1;
    gArtW.focusList = NULL;
    gArtW.focusHash = 0;
    gArtW.focusLen = 0;

    gArtW.thread.attr = 0;
    gArtW.thread.option = 0;
    gArtW.thread.stack_size = ART_THREAD_STACK_SIZE;
    gArtW.thread.gp_reg = &_gp;
    gArtW.thread.func = reinterpret_cast<void *>(&cacheArtWorkerThread);
    gArtW.thread.stack = gArtStack;
    gArtW.thread.initial_priority = ART_THREAD_PRIORITY;

    gArtW.threadId = CreateThread(&gArtW.thread);
    if (gArtW.threadId < 0) {
        gArtW.threadId = -1;
        return; // gArtW.running stays 0 -> the enqueue refuses to claim slots, see cacheGetTexture
    }

    if (StartThread(gArtW.threadId, NULL) < 0) {
        DeleteThread(gArtW.threadId);
        gArtW.threadId = -1;
        return;
    }

    gArtW.running = 1;
}

// Once per video frame, from the GUI thread. Two jobs, both O(1), neither able to block.
void cacheTickArt(void)
{
    // (1) Sample held direction for the SIO2 defer in artPop. GUI thread only -- getKeyPressed is
    // libpad, and every libpad call in this codebase is GUI-thread-only. getKeyPressed and not
    // getKey: getKey would CONSUME the menu repeat before navigation ever sees it.
    gArtQ.setNavActive(getKeyPressed(KEY_LEFT) || getKeyPressed(KEY_RIGHT) ||
                       getKeyPressed(KEY_UP) || getKeyPressed(KEY_DOWN) ||
                       getKeyPressed(KEY_L1) || getKeyPressed(KEY_R1));

    // (2) Lost-wakeup net. Work queued and nothing running means the worker is asleep and a wakeup
    // went missing -- one WakeupThread per frame costs nothing and makes the whole class of "art
    // silently stopped forever" impossible. It also re-arms the queue the instant a held direction
    // is released, without waiting for the next enqueue.
    if (gArtW.running && gArtQ.queued() > 0 && gArtQ.active() == 0)
        cacheWakeArtWorker();
}

int cacheEnd(int forceStop)
{
    int ms = forceStop ? ART_JOIN_MS_TERMINAL : ART_JOIN_MS_LAUNCH;

    if (!gArtW.running)
        return 1;

    cacheShutdownArtLoads(); // idempotent: latch, abort the in-flight one, drop the queue, wake
    gArtW.terminate = 1;
    cacheWakeArtWorker();

    while (gArtW.running && ms-- > 0)
        DelayThread(1000);

    if (gArtW.running) {
        // ABANDON, never TerminateThread. Killing a thread parked inside fileXio leaves the shared
        // IOP RPC channel half-used, and every later fileXio call from ANY thread is then undefined.
        //
        // ⚠ NOT free on the keep-IOP paths: they do not reset the IOP, so the target inherits that
        // half-used channel on the device it is about to read. Getting here is an anomaly worth
        // reading in the log, not the routine case.
        gArtW.shutdownAbandoned = 1;
        LOG("cacheEnd: art worker still running after %d ms -- abandoned, not terminated\n",
            forceStop ? ART_JOIN_MS_TERMINAL : ART_JOIN_MS_LAUNCH);
        return 0;
    }

    gArtW.threadId = -1;
    return 1;
}
