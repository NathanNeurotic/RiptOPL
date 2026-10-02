#ifndef __ARTQUEUE_H
#define __ARTQUEUE_H

#include "texcache.h"

// Tagged, not anonymous, because the record now carries its OWN queue links: art no longer rides an
// ioman request node, it rides this one.
typedef struct load_image_request
{
    struct load_image_request *next;
    struct load_image_request *prev; // art FIFO links, owned by whoever owns the request
    unsigned int queueEpoch;         // FIFO generation while queued; cleared when the worker owns it
    image_cache_t *cache;
    cache_entry_t *entry;
    item_list_t *list;
    // only for comparison if the deferred action is still valid
    int cacheUID;
    // Art epoch this request was queued in; a mismatch means the whole VIEW it belonged to is gone.
    // See cacheDropQueuedArt().
    unsigned int epoch;
    // Fail generation CURRENT WHEN THIS PROBE WAS ISSUED. An "absent" park is stamped with this and
    // not with the live global, deliberately: if cacheInvalidateFailMemo() fires while the read is
    // in flight, someone is telling us new art may have appeared and our answer may predate it, so
    // the memo lands stale and the row probes again. Wrong in the cheap direction.
    unsigned int failEpoch;
    // Selection generation this request belongs to. Deep cover lookahead is intentionally aggressive,
    // but once the cursor moves its old neighborhood is no longer useful. A stale generation is dropped
    // before another device read; an already-running uninterruptible RPC is allowed to finish safely.
    unsigned int focusEpoch;
    // Chunk-level abort, read by textures.c via texSetLoadAbortFlag. Lets a cancel interrupt a load
    // that is ALREADY RUNNING, which neither the stale-stamp test nor the epoch test can do -- both
    // only ever caught a request still sitting in the queue.
    //
    // ⚠ It cannot shorten an MX4SIO cover. texShouldUseMemoryReader sends every "mass*" path down
    // the whole-file staging fast path: one lseek and one read() of the entire PNG, a single
    // un-interruptible IOP RPC. The flag is only checked in the CHUNKED staging loop and inside the
    // streaming PNG reader. For the device #340 is actually about, NOT ISSUING the read is the only
    // lever -- which is what the SIO2 defer in artPop is for.
    volatile int abortRequested;
    // Resolved on the GUI THREAD at enqueue: does this cover ride SIO2, the controller's own bus?
    // Not resolvable on the worker -- answering it reads the support's private device data, which a
    // background rescan rewrites underneath. One byte, copied in, is immune.
    unsigned char sio2;
    // Which .tar archive this cover must come from, resolved on the GUI THREAD at enqueue for
    // exactly the same reason sio2 is. Answering it reaches into support-private state -- the FAV
    // store, the app list, the HDD prefix -- that the prio-30 IO worker rewrites during a list
    // rebuild, so the
    // art worker must never ask the question itself.
    //   1 = read artArchivePath and nothing else   0 = ordinary multi-device sweep
    //  -1 = this row's source has no archive home; probe no archive at all
    signed char artArchive;
    // Trails value in the SAME allocation (NULL unless artArchive == 1), so a request costs nothing
    // extra on the common path. Art has no queue-depth cap, so a fixed 256-byte field here would be
    // paid hundreds of times over on a slow device.
    char *artArchivePath;
    char *value;
} load_image_request_t;

// The art request FIFO plus the counters the GUI thread reads without a lock. A private intrusive
// list guarded by DIntr()/EIntr() brackets (see src/artqueue.cpp for why not a semaphore). RULE FOR
// CALLERS: never call the mutators with interrupts already disabled.
class ArtQueue
{
public:
    constexpr ArtQueue() = default;
    ArtQueue(const ArtQueue &) = delete;
    ArtQueue &operator=(const ArtQueue &) = delete;

    void push(load_image_request_t *req);
    void pushFront(load_image_request_t *req);
    // Move a queued request to the head in O(1); no-op for the running request or one at the head.
    void promote(load_image_request_t *req);
    // Called ONLY by the worker. NULL when empty, or when the head is an SIO2 cover and the user is
    // holding a direction.
    load_image_request_t *pop();
    // Take the whole queue in one O(1) swap; the caller walks and frees it with interrupts enabled.
    load_image_request_t *detachAll();
    // The worker's exit path for a request it popped: undoes pop()'s active/current bookkeeping.
    void release(load_image_request_t *req);
    // Forget everything (worker restart). The queue must be quiescent.
    void reset();

    load_image_request_t *current() const { return current_; }
    int queued() const { return queued_; }
    int active() const { return active_; }
    bool navActive() const { return navActive_ != 0; }
    void setNavActive(bool held) { navActive_ = held ? 1 : 0; }

private:
    load_image_request_t *head_ = NULL;
    load_image_request_t *tail_ = NULL;
    load_image_request_t *volatile current_ = NULL;
    // detachAll() swaps the queue in O(1), then frees its old nodes with interrupts enabled. A
    // request can therefore retain old next/prev links briefly after the swap; generation membership
    // makes a later promotion reject that detached chain without turning the splice back into O(n).
    unsigned int epoch_ = 1;
    volatile int queued_ = 0;
    volatile int active_ = 0;
    volatile int navActive_ = 0;
};

#endif
