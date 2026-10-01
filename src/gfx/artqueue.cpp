// The art request FIFO. Split out of texcache.cpp.
#include "include/opl.h"
#include "include/artqueue.h"

#include <kernel.h>

void ArtQueue::push(load_image_request_t *req)
{
    DIntr();
    req->next = NULL;
    req->prev = tail_;
    req->queueEpoch = epoch_;
    if (tail_)
        tail_->next = req;
    else
        head_ = req;
    tail_ = req;
    queued_++;
    EIntr();
}

void ArtQueue::pushFront(load_image_request_t *req)
{
    DIntr();
    req->prev = NULL;
    req->next = head_;
    req->queueEpoch = epoch_;
    if (head_)
        head_->prev = req;
    else
        tail_ = req;
    head_ = req;
    queued_++;
    EIntr();
}

void ArtQueue::promote(load_image_request_t *req)
{
    if (req == NULL)
        return;

    DIntr();
    // The selected cover can be a warmed neighbour buried in a deep queue. This used to scan the
    // singly-linked FIFO under DIntr(), making one selection's priority handoff O(queue depth) and
    // extending the interrupts-off window as browsing filled cache slots. The intrusive back link
    // keeps this splice O(1); no allocation, free, or device work occurs in the bracket.
    // Guard against promoting the request the worker is already executing, or one already at head.
    if (current_ != req && req->queueEpoch == epoch_ && req->prev != NULL) {
        load_image_request_t *prev = req->prev;
        load_image_request_t *next = req->next;

        prev->next = next;
        if (next)
            next->prev = prev;
        else
            tail_ = prev;

        req->prev = NULL;
        req->next = head_;
        head_->prev = req;
        head_ = req;
    }
    EIntr();
}

// Called ONLY by the worker.
load_image_request_t *ArtQueue::pop()
{
    load_image_request_t *req;

    DIntr();
    req = head_;
    if (req) {
        // SIO2 DEFER. The head of the queue is a cover on the controller's own bus and the user is
        // holding a direction RIGHT NOW. Issuing that read is issue #340's exact mechanism: the SD
        // transfer and the pad poll share SIO2, and on hardware that is a swallowed step. The
        // enqueue-side idle gate cannot help here -- it only stops requests being MADE during
        // navigation, and this one was made during a settle the user has since ended.
        //
        // Leave it queued. It costs nothing, and the next idle frame runs it. This is the ONLY
        // lever that works for MX4SIO: the abort flag cannot shorten a cover on that device,
        // because a "mass*" path takes the whole-file staging fast path in textures.c -- one
        // lseek and one read() of the entire PNG, a single un-interruptible IOP RPC. Not issuing
        // the read is the whole defence.
        //
        // Head-of-line blocking is accepted deliberately: a page is almost always one device, and
        // a skip-scan would make this pop O(n) inside the interrupts-off bracket.
        if (req->sio2 && navActive_) {
            EIntr();
            return NULL;
        }

        head_ = req->next;
        if (head_)
            head_->prev = NULL;
        else
            tail_ = NULL;
        req->next = NULL;
        req->prev = NULL;
        req->queueEpoch = 0;

        if (queued_ > 0)
            queued_--;
        // ACTIVE now means "owned by the worker", not "reading the device" -- a deliberate
        // widening. cacheHasPendingArt() gates background rescans and the per-game background
        // request, and it must stay true across a cancellation too, or a rescan slips in exactly
        // while the art lane is churning.
        active_++;
        current_ = req;
    }
    EIntr();

    return req;
}

// Take the WHOLE queue in one O(1) swap. The caller then walks and frees it with interrupts
// ENABLED -- which is why no canceller ever holds a bracket across a free().
load_image_request_t *ArtQueue::detachAll()
{
    load_image_request_t *list;

    DIntr();
    list = head_;
    head_ = NULL;
    tail_ = NULL;
    queued_ = 0;
    if (++epoch_ == 0)
        epoch_ = 1;
    EIntr();

    return list;
}

void ArtQueue::release(load_image_request_t *req)
{
    DIntr();
    if (current_ == req)
        current_ = NULL;
    if (active_ > 0)
        active_--;
    EIntr();
}

void ArtQueue::reset()
{
    head_ = NULL;
    tail_ = NULL;
    current_ = NULL;
    if (++epoch_ == 0)
        epoch_ = 1;
    queued_ = 0;
    active_ = 0;
    navActive_ = 0;
}
