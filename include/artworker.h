#ifndef __ARTWORKER_H
#define __ARTWORKER_H

// State of the cover-art worker thread (see src/texcache.cpp): its thread and lifecycle flags, the epochs that
// supersede queued work, the selection-focus tracker and the diagnostics the debug HUD draws.
//
// The data members are deliberately public: the worker (a different thread from the GUI) and the GUI-side
// enqueue path read and write individual words lock-free, and every reader/writer pair has been reasoned
// about one field at a time (see the comment on each). Behaviour that spans fields lives in methods.

#include <kernel.h>
#include <stddef.h>
#include "iosupport.h" // item_list_t (needs the opl.h types included first)

// Matches ioman's worker stack. The identical libpng decode runs on that stack today, so this is a
// measured size rather than a guess. .bss only; it adds nothing to the ELF.
constexpr int ART_THREAD_STACK_SIZE = (96 * 1024);

class ArtWorker
{
public:
    constexpr ArtWorker() = default;
    ArtWorker(const ArtWorker &) = delete;
    ArtWorker &operator=(const ArtWorker &) = delete;

    // Wake the worker if it is running (no-op otherwise).
    void wake();
    // Record the selected cover; a change starts a new focus epoch so deep lookahead for the old
    // neighbourhood is dropped. Returns nothing: callers read focusEpoch.
    void updateFocus(item_list_t *list, const char *value);

    int threadId = -1;
    ee_thread_t thread = {};
    u8 stack[ART_THREAD_STACK_SIZE] __attribute__((aligned(16))) = {};
    volatile int running = 0;
    volatile int terminate = 0;
    volatile int shutdownAbandoned = 0;
    // NO HASH-TABLE FAIL MEMO, and no retry budget: a load that fails parks its SLOT (lastUsed = 0 ->
    // cacheId -2 in cacheGetTexture) and is not asked again, which is the same protection one layer down
    // and without a side table to get wrong. The retry budget that used to sit on top was how the IO
    // worker got pinned (rebuild-103/105), and absence is detected correctly now (rebuild-108).
    //
    // What rebuild-153 added is only the GENERATION. The park used to survive exactly as long as the
    // row's cache_id did, so every list rebuild re-probed every missing cover -- and on hardware a MISS
    // is the expensive case, not the hit: Nathan's capture shows successful covers at 55-81 ms against
    // 1091, 2586, 2677 and 7120 ms for the ones that resolve to nothing. Re-probing those on every
    // rebuild is most of where the art throughput went (~0.7 loads/sec observed, against the >12/sec the
    // 55-81 ms hits imply).
    unsigned int failEpoch = 1;
    // Diagnostics only (drawn by the debug HUD in gui.c). gArtRefused counts requests the backpressure
    // gate turned away, gArtDone counts loads that finished -- together they say whether the pipeline is
    // moving or wedged, which is not something a still image of the menu can tell you.
    volatile int refused = 0;
    // Last art load's wall-clock cost in ms, and the last SUCCESSFUL one with its decoded dimensions.
    // Written by the IO worker, read by the GUI thread for the debug HUD -- plain ints, a torn read
    // mis-prints one frame and nothing else depends on them.
    volatile int lastMs = 0;
    volatile int lastOkMs = 0;
    volatile int lastWidth = 0;
    volatile int lastHeight = 0;
    volatile int done = 0;
    // Requests released WITHOUT loading -- cancelled, superseded, or dropped at teardown. The
    // complement of gArtDone: together they must account for every request artPop ever handed out, so a
    // climbing total with both of these flat is the signature of a request leak.
    volatile int dropped = 0;
    // Rows whose value was too long to keep as a reunion key, i.e. rows getting NONE of the cache
    // reunion or the absence memo. Must be 0 in practice; anything else names a page that is still
    // paying the device for art it has already looked at. See cache_entry_t.key.
    volatile unsigned int keyTooLong = 0;
    // Art loads that failed TRANSIENTLY (not ERR_BAD_FILE) and were re-queued rather than parked as
    // absent. Should be ~0 on a healthy device; a climbing count on SMB is the bus/share misbehaving,
    // and the covers self-heal instead of vanishing for the session. See the branch in cacheLoadImage.
    volatile unsigned int transientFail = 0;
    // Set once, at teardown, by cacheShutdownArtLoads(). See texcache.h for the full reasoning -- and in
    // particular why this is NOT wired to cacheCancelPendingImageLoads(), which themes.c calls on the
    // first theme load of every boot.
    int loadsCancelled = 0;
    // Art epoch. Bumped when the ENTIRE view a queue of covers belongs to is discarded, which the
    // per-row cancellation below cannot detect on its own.
    //
    // That cancellation drops a request whose row has scrolled away, keyed on entry->lastUsed going
    // stale. On an L3 ISO<->VCD toggle nothing scrolls: the old rows keep being drawn (which is why the
    // previous view's titles sit there looking stale) so every stamp keeps refreshing, and not one of
    // those now-pointless covers is cancelled. They are FIFO ahead of the IO_MENU_UPDATE_DEFFERED that
    // rebuilds the list, and art shares the single ioman worker with that rebuild -- so the page cannot
    // become correct until every cover for the view being thrown away has been read off the device
    // first. That is the whole "slow to be correct" symptom, and it is why the fork feels faster here:
    // its art runs on a dedicated thread and never sits in front of the rebuild.
    unsigned int epoch = 0;
    // Selection-focus epoch. The selected cover is the only priority art request, so it is also the
    // authoritative signal that the useful prefetch neighborhood changed. Keep only a hash+length here:
    // VCD basenames may exceed the cache reunion key, and a collision can at worst preserve a little
    // obsolete prefetch -- it can never select the wrong texture or affect identity. GUI thread writes;
    // the art worker only compares the epoch captured in each request.
    volatile unsigned int focusEpoch = 1;
    item_list_t *focusList = NULL;
    unsigned int focusHash = 0;
    size_t focusLen = 0;
};

#endif
