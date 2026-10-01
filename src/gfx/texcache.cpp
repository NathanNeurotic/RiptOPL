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

void cacheClearItem(cache_entry_t *item, int freeTxt)
{
    if (freeTxt && item->texture.Mem) {
        rmUnloadTexture(&item->texture);
        free(item->texture.Mem);
        if (item->texture.Clut)
            free(item->texture.Clut);
    }

    memset(item, 0, sizeof(cache_entry_t));
    item->texture.Mem = NULL;
    item->texture.Vram = 0;
    item->texture.Clut = NULL;
    item->texture.VramClut = 0;
    item->texture.ClutStorageMode = GS_CLUT_STORAGE_CSM1; // Default
    item->qr = NULL;
    item->lastUsed = -1;
    item->UID = 0;
}

image_cache_t *cacheInitCache(int userId, const char *prefix, int isPrefixRelative, const char *suffix, int count)
{
    image_cache_t *cache = (image_cache_t *)malloc(sizeof(image_cache_t));
    cache->userId = userId;
    cache->count = count;
    cache->prefix = NULL;
    int length;
    if (prefix) {
        length = strlen(prefix) + 1;
        cache->prefix = (char *)malloc(length * sizeof(char));
        memcpy(cache->prefix, prefix, length);
    }
    cache->isPrefixRelative = isPrefixRelative;
    length = strlen(suffix) + 1;
    cache->suffix = (char *)malloc(length * sizeof(char));
    memcpy(cache->suffix, suffix, length);
    cache->nextUID = 1;
    cache->activeRequests = 0;
    cache->content = (cache_entry_t *)malloc(count * sizeof(cache_entry_t));

    int i;
    for (i = 0; i < count; ++i)
        cacheClearItem(&cache->content[i], 0);

    return cache;
}

void cacheDestroyCache(image_cache_t *cache)
{
    int i, ms = ART_FOREGROUND_DRAIN_MS;

    if (cache == NULL)
        return;

    // Belt and braces on top of the drain the CALLER should have done. The dangerous caller is
    // exactly one: endMutableImage via thmFree, i.e. a theme switch. The others destroy a fresh
    // clone no request can ever have referenced.
    //
    // What a stray request would do if we just freed: dereference req->cache->suffix and ->prefix,
    // and req->entry -- a pointer INTO the content[] array below. Worse, a cancellation early-out
    // would then call cacheClearItem on whatever bytes now occupy the freed slot, i.e. free() a
    // garbage pointer. Harmless while art shared the io worker; a live use-after-free with a worker
    // running concurrently.
    if (cache->activeRequests > 0) {
        if (gArtQ.current() && gArtQ.current()->cache == cache)
            gArtQ.current()->abortRequested = 1;
        cacheWakeArtWorker();

        while (cache->activeRequests > 0 && ms-- > 0)
            DelayThread(1000);
    }

    // LEAK RATHER THAN FREE. If the wait expired something still holds a pointer into this cache. A
    // few KB of EE RAM on a path that only runs on a theme switch is cheap; a use-after-free in the
    // art path is the entire class of bug this change exists to close.
    if (cache->activeRequests > 0) {
        LOG("cacheDestroyCache: %d art request(s) still reference this cache -- leaking it\n",
            cache->activeRequests);
        return;
    }

    for (i = 0; i < cache->count; ++i) {
        cacheClearItem(&cache->content[i], 1); // GUI thread: gsKit contact is correct here
    }

    free(cache->prefix);
    free(cache->suffix);
    free(cache->content);
    free(cache);
}

// Does this cover ride SIO2 -- the CONTROLLER'S OWN BUS? MX4SIO today; MMCE joins it the moment
// checklist item 1 lands, which is why MMCE_MODE is already tested here.
//
// Resolved on the GUI THREAD at enqueue, never on the worker: answering it reaches into the
// support's private device data, and a background rescan rewrites that underneath. One byte copied
// into the request is immune to that.
//
// FAV and APP rows are PROXIES -- their art lives on whatever device the underlying entry came from,
// not on the proxy tab -- so resolve through to the real mode first.
static int cacheRequestIsSIO2(item_list_t *list, const char *value)
{
    int mode;

    if (list == NULL)
        return 0;

    mode = list->mode;

    if (mode == FAV_MODE && value != NULL) {
        int src = favGetArtMode(value);
        if (src >= 0)
            mode = src;
    }
    if (mode == APP_MODE && value != NULL) {
        int src = appGetArtMode(value);
        if (src >= 0)
            mode = src;
    }

    if (mode == MMCE_MODE)
        return 1;

    return bdmModeIsSIO2(mode);
}

// WHICH .tar archive serves this row -- resolved here, on the GUI thread at enqueue, under the same
// rule and for the same reason as cacheRequestIsSIO2 directly above. FAV and APP rows are PROXIES,
// so answering it means reading favArray / the app list, and the HDD answer reads gHDDPrefix; all
// three are rewritten by the prio-30 IO worker during a list rebuild. The worker asking them itself
// is a race, which is what this function exists to prevent.
//
// Returns 1 and fills path when one specific archive must be used, -1 when the row's source has an
// archive home that is currently unavailable (probe NOTHING rather than falling through to the
// generic sweep, which would serve another device's art), and 0 for the ordinary sweep.
static int cacheResolveArtArchive(item_list_t *list, const char *value, char *path, int pathSize)
{
    int mode;

    if (list == NULL)
        return 0;

    // A support that owns its archive answers for itself (HDD does).
    if (list->itemGetArtArchivePath != NULL)
        return list->itemGetArtArchivePath(list, path, pathSize);

    mode = list->mode;

    if (mode == FAV_MODE && value != NULL) {
        int src = favGetArtMode(value);
        if (src >= 0)
            mode = src;
    }
    if (mode == APP_MODE && value != NULL) {
        int src = appGetArtMode(value);
        if (src >= 0)
            mode = src;
    }

    if (mode == HDD_MODE)
        return hddGetArtArchivePath(list, path, pathSize);

    return 0;
}

// Abort every background still queued or loading for a game other than `value`. Scans EVERY slot:
// initMutableImage floors each per-game art cache at 2, so the `cache->count == 1` guard this used to
// sit behind made it dead code, and an abandoned background kept its decode running in front of the
// next cover (#772). Marking it aborted rather than dropping it keeps one owner for the request: the
// worker releases it on its own terms, whether it is still queued or already inside the read.
static void cacheAbortOtherBackgrounds(image_cache_t *cache, const char *value)
{
    int i;

    for (i = 0; i < cache->count; i++) {
        cache_entry_t *entry = &cache->content[i];
        if (entry->qr != NULL && entry->key[0] != '\0' && strcmp(entry->key, value) != 0)
            ((load_image_request_t *)entry->qr)->abortRequested = 1;
    }
}

GSTEXTURE *cacheGetTextureEx(image_cache_t *cache, item_list_t *list, int *cacheId, int *UID, char *value, int isPriority)
{
    int i, rtime;
    if (cache == NULL || list == NULL || cacheId == NULL || UID == NULL || value == NULL || value[0] == '\0')
        return NULL;

    // A priority request is the highlighted cover. Move the global focus before ANY cache fast-path
    // return so even a cover that is already resident immediately invalidates queued lookahead from
    // the selection we just left.
    if (isPriority)
        gArtW.updateFocus(list, value);

    // NO memo lookup here. This function runs for EVERY art element on screen EVERY frame --
    // including covers that are already loaded and rendering -- and the memo check costs a
    // snprintf + hash + strcmp. Placed here it taxed every cached frame (dozens of format-string
    // calls per frame on a 294 MHz CPU); the fast path below must stay a couple of integer
    // compares, exactly what the hardware-validated builds did. The memo is consulted at the
    // ENQUEUE decision instead -- the only place it saves anything real (an IO attempt).

    if (*cacheId == -2) {
        // Parked as absent. Honour it only while it belongs to the CURRENT fail generation, so
        // cacheInvalidateFailMemo() can genuinely re-open the question (the epoch rides in *UID,
        // which is meaningless in the -2 state).
        if ((unsigned int)*UID == gArtW.failEpoch)
            return NULL;
        *cacheId = -1;
    } else if (*cacheId != -1) {
        // Bounds check, which this path never had: a row keeps its cache_id across a theme switch,
        // and the new theme's cache can be SMALLER. content[*cacheId] was then an out-of-bounds read
        // whose UID happened not to match often enough to be noticed.
        if (*cacheId >= 0 && *cacheId < cache->count) {
            cache_entry_t *entry = &cache->content[*cacheId];
            if (entry->UID == *UID) {
                if (entry->qr) {
                    // Still loading -- but mark it as still WANTED. This stamp is the only evidence
                    // the worker has that anyone is still looking at this row (see cacheLoadImage).
                    entry->lastUsed = guiFrameId;
                    load_image_request_t *req = (load_image_request_t *)entry->qr;
                    if (req != NULL) {
                        // Any pending cover touched by THIS frame is still useful to the new
                        // selection neighborhood. Adopt it into the current generation even when
                        // it is merely a neighbour; only the highlighted cover gets queue promotion.
                        // Without this, overlapping lookahead (+2 becomes +1 after one step) kept
                        // the OLD generation and the worker discarded work the new frame still wanted.
                        req->focusEpoch = gArtW.focusEpoch;
                        if (isPriority && !(req->sio2 && gArtQ.navActive()))
                            gArtQ.promote(req);
                    }
                    return NULL;
                } else if (entry->lastUsed == 0) {
                    *cacheId = -2;
                    *UID = (int)gArtW.failEpoch;
                    return NULL;
                } else {
                    entry->lastUsed = guiFrameId;
                    return &entry->texture;
                }
            }
        }

        *cacheId = -1;
    }

    // REUNION BY VALUE. The ids above are handed out per REQUEST, so anything that renumbers rows --
    // every list rebuild, every device rescan that lands while the user browses, an L3 ISO/VCD
    // toggle -- makes every row forget art this cache is still holding, decoded, one slot away. The
    // row then re-requests it and pays the device again.
    //
    // Cheap enough to run on every miss: it is a strcmp against at most `count` slots (typically 6
    // to 32) and ONLY on the path that was already about to claim a slot and malloc a request. The
    // fast path -- a row whose ids still match -- returns above without touching this.
    //
    // The in-flight case matters as much as the ready one: without it, a rebuild that lands while a
    // cover is loading orphans that request and immediately queues a SECOND read of the same file.
    {
        size_t keyLen = strlen(value);
        if (keyLen > 0 && keyLen < sizeof(cache->content[0].key)) {
            for (i = 0; i < cache->count; i++) {
                cache_entry_t *entry = &cache->content[i];

                if (entry->key[0] == '\0' || strcmp(entry->key, value) != 0)
                    continue;

                if (entry->qr) { // already being loaded FOR US -- adopt it, do not queue a duplicate
                    *cacheId = i;
                    *UID = entry->UID;
                    entry->lastUsed = guiFrameId;
                    load_image_request_t *req = (load_image_request_t *)entry->qr;
                    if (req != NULL) {
                        // Any pending cover touched by THIS frame is still useful to the new
                        // selection neighborhood. Adopt it into the current generation even when
                        // it is merely a neighbour; only the highlighted cover gets queue promotion.
                        // Without this, overlapping lookahead (+2 becomes +1 after one step) kept
                        // the OLD generation and the worker discarded work the new frame still wanted.
                        req->focusEpoch = gArtW.focusEpoch;
                        if (isPriority && !(req->sio2 && gArtQ.navActive()))
                            gArtQ.promote(req);
                    }
                    return NULL;
                }

                if (entry->lastUsed == 0) { // known absent, from this generation
                    if (entry->failEpoch == gArtW.failEpoch) {
                        *cacheId = -2;
                        *UID = (int)gArtW.failEpoch;
                        return NULL;
                    }
                    break; // stale memo: fall through and re-probe
                }

                if (entry->texture.Mem != NULL) {
                    *cacheId = i;
                    *UID = entry->UID;
                    entry->lastUsed = guiFrameId;
                    return &entry->texture;
                }

                break;
            }
        }
    }

    // Which bus this cover lives on decides whether it may be asked for mid-scroll. Resolved HERE,
    // on the GUI thread -- see cacheRequestIsSIO2 for why it cannot be done on the worker.
    int isSio2 = cacheRequestIsSIO2(list, value);

    // THE IDLE GATE IS FOR SIO2 ONLY (MMCE and MX4SIO). master gates exactly the same way, on
    // MMCE_MODE alone; the rebuild applied it to every device, and that is most of the "pop-in"
    // feel -- on USB, HDD and the network transports nothing was even ASKED FOR until the user had
    // been still for several frames, so art could not start arriving until after they stopped.
    //
    // The floor's original justification is gone, and it went with rebuild-152. It read: "the IO
    // worker is priority 30 and the GUI/pad thread is 31, so the worker PREEMPTS rendering and
    // input; the PNG decode is tens of ms of pure CPU and during it the menu stops sampling the
    // pad." True while art rode the IO worker. Art now has its own thread at priority 64 -- BELOW
    // the GUI -- so a decode cannot preempt rendering or input by construction. The lurch that
    // motivated the floor is not reachable from this path any more.
    //
    // What remains true is bus contention, which priority does not fix: an MX4SIO cover and the pad
    // share SIO2, and an MMCE cover shares the card bus. Those keep the gate, and artPop keeps
    // refusing to START one while a direction is held.
    if (isSio2) {
        int artDelay = list->delay;
        if (artDelay < ART_MIN_SETTLE_FRAMES)
            artDelay = ART_MIN_SETTLE_FRAMES;
        if (guiInactiveFrames < artDelay)
            return NULL;
    }

    // LATEST SELECTION WINS on the background cache. A background queued or loading for a selection
    // the user has already left holds its slot until it finishes, so the cover for where they ARE
    // waits behind somebody else's scenery -- seconds, on the evidence above. The stale-stamp
    // cancellation in cacheLoadImage cannot help, because the row it belongs to may still be on
    // screen and still stamping.
    if (cache->suffix != NULL && strcmp(cache->suffix, "BG") == 0)
        cacheAbortOtherBackgrounds(cache, value);

    cache_entry_t *currEntry, *oldestEntry = NULL;
    rtime = guiFrameId;

    // WHILE A DIRECTION IS HELD, CLAIM ONLY SLOTS THAT COST NOTHING TO TAKE -- free ones and ones
    // parked as absent. Never evict a decoded texture mid-scroll on SIO2/MMCE where bus contention
    // with pad polling exists. USB, HDD, and SMB stream-evict smoothly without holding.
    int navHoldsCache = isSio2 && gArtQ.navActive();

    for (i = 0; i < cache->count; i++) {
        currEntry = &cache->content[i];
        if (currEntry->qr)
            continue;
        if (navHoldsCache && currEntry->lastUsed > 0)
            continue; // holds a decoded image; leave it alone until the scroll stops
        if (currEntry->lastUsed < rtime) {
            oldestEntry = currEntry;
            rtime = currEntry->lastUsed;
            *cacheId = i;
        }
    }

    // BACKPRESSURE, lock-free (see gArtQ.queued() above). Art is the one request type that is
    // free to drop: the slot is never claimed, and the next frame simply asks again. Refusing to
    // deepen an already-deep queue keeps the config save, the deferred menu rebuild and device
    // init from starving behind hundreds of queued art reads on a slow device.
    // NO QUEUE-DEPTH CAP, and no in-flight gate. uOPL and rebuild-66 -- the two builds hardware
    // agrees are fast -- both enqueue every visible miss unconditionally, with no throttle of any
    // kind, and this fork's art was the only one that was slow. The cap was added to stop art
    // burying a config save; that is now handled where it belongs (the visible cover is a priority
    // request that runs next, and background device rescans yield to pending art), so the throttle
    // was pure latency. Refusing a request also cost more than it saved: a refusal resets the row's
    // cacheId and the whole claim is redone next frame.
    // gArtW.running is part of the gate: if the worker never started, nothing would ever clear qr and
    // the slot would be dead for the session. Better to draw no art than to poison the cache.
    if (oldestEntry && gArtW.running) {
        // Resolve the archive HERE, while support-private state is stable -- see cacheResolveArtArchive.
        char archivePath[256];
        int archiveMode = 0;
        size_t archiveLen = 0;
        size_t valueLen = strlen(value);

        if (gEnableArtTar) {
            archiveMode = cacheResolveArtArchive(list, value, archivePath, sizeof(archivePath));
            if (archiveMode == 1) {
                archiveLen = strlen(archivePath);
                // "Use this archive" with nothing to use means the home is unreachable, not that the
                // generic sweep is appropriate -- that fallback would serve another device's art.
                if (archiveLen == 0)
                    archiveMode = -1;
            }
        }

        load_image_request_t *req = static_cast<load_image_request_t*>(malloc(sizeof(load_image_request_t) + valueLen + 1 + (archiveLen ? archiveLen + 1 : 0)));
        if (!req) {
            *cacheId = -1; // nothing cleared yet at this point, so the slot is untouched
            return NULL;
        }
        req->cache = cache;
        req->entry = oldestEntry;
        req->list = list;
        req->value = (char *)req + sizeof(load_image_request_t);
        strcpy(req->value, value);
        req->artArchive = (signed char)archiveMode;
        if (archiveLen) {
            req->artArchivePath = req->value + valueLen + 1;
            strcpy(req->artArchivePath, archivePath);
        } else {
            req->artArchivePath = NULL;
        }
        req->cacheUID = cache->nextUID;
        req->epoch = gArtW.epoch;           // the view this cover belongs to; see cacheDropQueuedArt()
        req->failEpoch = gArtW.failEpoch;   // the generation an "absent" answer would belong to
        req->focusEpoch = gArtW.focusEpoch; // selected-game neighborhood this speculative read serves
        req->abortRequested = 0;
        req->next = NULL;
        req->prev = NULL;
        req->queueEpoch = 0;
        req->sio2 = (unsigned char)isSio2; // resolved above, on the GUI thread

        cacheClearItem(oldestEntry, 1);
        oldestEntry->qr = req;
        oldestEntry->UID = cache->nextUID;

        // The slot's identity, and the only thing that lets a renumbered row find this art again.
        // Written only when the value FITS, so a hit is always a whole-string match; an over-long
        // value simply gets no key and behaves exactly as it did before rebuild-153.
        {
            size_t keyLen = strlen(value);
            if (keyLen < sizeof(oldestEntry->key))
                memcpy(oldestEntry->key, value, keyLen + 1);
            else
                gArtW.keyTooLong++; // HUD: rows getting none of the reunion. See cache_entry_t.key.
        }

        // BORN WANTED. cacheClearItem one line above just wrote the module's "this slot is free"
        // sentinel -- lastUsed = -1 -- and the cancellation in cacheLoadImage reads that SAME field
        // as an AGE: (u32)(guiFrameId - lastUsed). For -1 that evaluates to guiFrameId + 1, so from
        // frame 20 of every session onward a request reached the worker already counting as "20+
        // frames since anyone looked at this row" and was thrown away BEFORE it touched the device.
        // One field carrying two meanings; the cancellation added the reader and there had never
        // been a writer here to match it.
        //
        // It only bites when the queue is EMPTY, which is why it read as an idle-only fault. The IO
        // worker is priority 32 and the GUI 31, so it cannot preempt -- it runs in the vsync gap
        // inside the frame that queued the request, before the requester gets another draw pass to
        // refresh the stamp at the entry->qr branch in cacheGetTexture. Anything the worker was
        // already busy with pushes the request past that frame boundary, the requester re-stamps it,
        // and it survives. That is the whole reason art appeared during boot and device rescans and
        // then stopped the moment the menu settled.
        //
        // HARDWARE, debug HUD, sitting IDLE on a coverflow list with covers visibly on screen: 25
        // video frames apart the art total went 22269 -> 22293 -- one request per frame, forever --
        // while D stayed 59 and A stayed 0. The drop sits above both the active count and
        // gArtW.done++, so nothing counted and no device read was ever issued: 22269 requests, zero
        // loads. Coverflow's covers survived only because the draw path touches each key more than
        // once per frame; the perpetual loser was the per-game Background, which asks ONLY when
        // !cacheHasPendingArt() and therefore asks only in the exact state that guarantees a
        // same-frame drain -- and could not even park itself as "absent", because parking requires a
        // load that actually ran.
        //
        // Stamping here makes the test measure what its own comment says it measures: frames since
        // the row was last DRAWN. A row that scrolls away still stops stamping and its request still
        // dies at 20 frames, so the cancellation keeps doing its job -- it just stops shooting every
        // request the instant it is made. guiFrameId is >= 1 inside any drawn frame (guiStartFrame
        // increments before anything draws), so this can never write the 0 that means "load failed".
        oldestEntry->lastUsed = guiFrameId;

        *UID = cache->nextUID++;

        // The cache now owns an in-flight reference. cacheDestroyCache waits on this rather than
        // freeing content[] out from under a request that still points into it -- which a theme
        // switch would otherwise do.
        cache->activeRequests++;

        // artPush takes its own DIntr bracket and does the the queued count inside it. It cannot
        // fail: no allocation, no queue cap. Priority requests push to the head of the FIFO unless
        // an SIO2 request is being made while a direction is held (which falls back to tail to protect #340).
        if (isPriority && !(req->sio2 && gArtQ.navActive()))
            gArtQ.pushFront(req);
        else
            gArtQ.push(req);
        cacheWakeArtWorker();
    }

    return NULL;
}

GSTEXTURE *cacheGetTexture(image_cache_t *cache, item_list_t *list, int *cacheId, int *UID, char *value)
{
    return cacheGetTextureEx(cache, list, cacheId, UID, value, 0);
}
