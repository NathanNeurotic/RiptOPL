#include "include/opl.h"
#include "include/ioman.h"
#include "include/ioqueue.h"
#include "include/util.h" // delay() -- bounded drain in ioBlockOpsTimed
#include <kernel.h>
#include <stdarg.h>
#include <delaythread.h> // DelayThread -- ioInit waits for a previous worker to leave
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include <unistd.h>
#ifdef __EESIO_DEBUG
#include <sio.h>
#endif

/*
  There is deliberately NO cap on the number of queued requests.

  A MAX_IO_REQUESTS 64 constant used to sit here, enforced nowhere -- the dead half of the pair whose
  live half (MAX_IO_HANDLERS, below) IS enforced in ioRegisterHandler. Removed rather than wired up,
  because enforcing it would be wrong in both directions:

  Nothing to protect. A request node is a type plus two pointers. Sixty-four of them is about a
  kilobyte on a 32 MB console, and the worker drains the queue to empty before it sleeps
  (ioWorkerThread), so depth is set by the event rate, not by any cap.

  Backpressure already exists, at the PRODUCERS, which is the only place it can be lossless. The
  background rescans refuse to enqueue at all while anything is pending (menuUpdateHook's
  `if (ioHasPendingRequests()) return;`), and art has its own gArtQueuedCount gate -- lock-free
  precisely because the obvious queue-side counter, ioGetPendingRequestCount(), takes gProcSemaId,
  which the worker holds across the WHOLE drain, so calling it from the GUI thread froze the menu for
  as long as any art was in flight (see texcache.c). Those gates also close the one case a cap might
  seem to answer: a wedged handler stops the drain, but it stops the enqueues with it.

  And rejecting here would LOSE work rather than defer it. Most call sites ignore the return value, so
  a refused IO_MENU_UPDATE_DEFFERED is a list that never refreshes and a refused art load is a cover
  that parks forever -- the exact failure classes this fork keeps having to fix. IO_ERR_TOO_MANY_REQUESTS
  stays: ioPutRequest still returns it when the node allocation itself fails.
*/
extern void *_gp;

constexpr int THREAD_STACK_SIZE = (96 * 1024);

static u8 thread_stack[THREAD_STACK_SIZE] ALIGNED(16);

static ee_thread_t gIOThread;

static IoQueue gQueue;

int IoQueue::pending(int type) const
{
    return (type > 0 && type < IO_REQ_TYPE_COUNT) ? pendingByType_[type] : 0;
}

unsigned int IoQueue::total(int type) const
{
    return (type > 0 && type < IO_REQ_TYPE_COUNT) ? totalByType_[type] : 0;
}

int IoQueue::registerHandler(int type, io_request_handler_t handler)
{
    WaitSema(procSema_);

    // Every early return below must release procSema_, otherwise a registration
    // failure leaves the semaphore held and deadlocks the entire I/O subsystem.
    if (handler == NULL) {
        SignalSema(procSema_);
        return IO_ERR_INVALID_HANDLER;
    }

    if (handlerCount_ >= kMaxHandlers) {
        SignalSema(procSema_);
        return IO_ERR_TOO_MANY_HANDLERS;
    }

    for (int i = 0; i < handlerCount_; ++i) {
        if (handlers_[i].type == type) {
            SignalSema(procSema_);
            return IO_ERR_DUPLICIT_HANDLER;
        }
    }

    handlers_[handlerCount_].type = type;
    handlers_[handlerCount_].handler = handler;
    handlerCount_++;

    SignalSema(procSema_);

    return IO_OK;
}

io_request_handler_t IoQueue::findHandler(int type) const
{
    for (int i = 0; i < handlerCount_; ++i) {
        const Handler *h = &handlers_[i];

        if (h->type == type)
            return h->handler;
    }

    return NULL;
}

void IoQueue::process(Request *req)
{
    if (!req)
        return;

    io_request_handler_t hlr = findHandler(req->type);

    // invalidate the request
    void *data = req->data;

    if (hlr)
        hlr(data);
}

void IoQueue::workerEntry(void *arg)
{
    (void)arg;
    gQueue.workerLoop();
}

void IoQueue::workerLoop()
{
    while (!terminate_) {
        SleepThread();

        // if term requested exit immediately from the loop
        if (terminate_)
            break;

        // do we have a request in the queue?
        WaitSema(procSema_);
        while (head_) {
            // if term requested exit immediately from the loop
            if (terminate_)
                break;

            Request *req = head_;
            current_ = req;
            process(req);

            // lock the queue tip as well now
            WaitSema(endSema_);
            current_ = NULL;

            if (req->type > 0 && req->type < IO_REQ_TYPE_COUNT && pendingByType_[req->type] > 0)
                pendingByType_[req->type]--;

            // can't be sure if the request was
            head_ = req->next;
            free(req);

            if (!head_)
                tail_ = NULL;

            SignalSema(endSema_);
        }
        SignalSema(procSema_);
    }

    // delete the pending requests
    while (head_) {
        Request *req = head_;
        head_ = head_->next;
        free(req);
    }

    // delete the semaphores
    DeleteSema(procSema_);
    DeleteSema(endSema_);

    workerRunning_ = 0;

    ExitDeleteThread();
}

static void ioSimpleActionHandler(void *data)
{
    io_simpleaction_t action = (io_simpleaction_t)data;

    if (action)
        action();
}

void IoQueue::start()
{
    // A second start happens only when a refused autolaunch falls back to the menu. miniDeinit's drain
    // is bounded, so when it asks the first worker to stop (stop) that worker may still be inside the
    // one request it was running -- and it runs below this thread. Wait until it has actually left,
    // with NO cap: it lives on the static thread_stack reused below, and resetting the queue, the
    // semaphores and terminate_ while it is alive would put two threads on one stack. A cap would
    // not save the menu anyway: the worker only blocks in bounded waits or IOP RPCs, and an RPC that
    // never returns leaves that IOP service stuck for this thread's own calls too.
    for (int waited = 0; *(volatile int *)&workerRunning_; waited++) {
        if (waited > 0 && waited % 100 == 0)
            LOG("IOMAN ioInit: previous worker still finishing its request (%d s)\n", waited / 100);
        DelayThread(10 * 1000);
    }
    // The new worker starts with an open queue, whatever the previous session left behind.
    blocked_ = 0;

    terminate_ = 0;
    handlerCount_ = 0;
    head_ = NULL;
    tail_ = NULL;
    current_ = NULL;

    threadId_ = 0;

    ee_sema_t sema;
    sema.init_count = 1;
    sema.max_count = 1;
    sema.option = 0;

    procSema_ = CreateSema(&sema);
    endSema_ = CreateSema(&sema);
    printSema_ = CreateSema(&sema);

    // default custom simple action handler
    registerHandler(IO_CUSTOM_SIMPLEACTION, &ioSimpleActionHandler);

    gIOThread.attr = 0;
    gIOThread.stack_size = THREAD_STACK_SIZE;
    gIOThread.gp_reg = &_gp;
    gIOThread.func = reinterpret_cast<void *>(&IoQueue::workerEntry);
    gIOThread.stack = thread_stack;
    // BELOW the GUI/pad thread (31), not above it.
    //
    // At 30 this worker OUTRANKED the thread that renders and reads the controller, so any work it
    // picked up -- a cover decode, a device probe, a list rebuild, even churning through a queue of
    // superseded requests -- took the CPU away from input for as long as it ran. On hardware that is
    // the navigation fault the reporter has chased all session: hold, a swallowed step, resume, and
    // more recently "jumping instead of stalling" while the HUD showed seventeen art requests queued
    // with none executing. Everything queued here is BACKGROUND work by definition -- it exists
    // precisely so the menu does not have to wait for it -- so it has no business preempting the
    // menu.
    //
    // Safe because the GUI never busy-waits on this thread: guiHandleDeferedIO pumps real frames and
    // blocks on vsync each pass (guiShow), which yields the CPU, so the worker still runs during a
    // wait. Blocking device I/O is unaffected -- a thread blocked in the IOP RPC consumes nothing at
    // either priority. What changes is only who wins when both are runnable, and that should always
    // be the one holding the user's input.
    gIOThread.initial_priority = 32;

    workerRunning_ = 1;
    threadId_ = CreateThread(&gIOThread);
    StartThread(threadId_, NULL);
}

// Link a request at the queue tail. Caller holds endSema_ (and has checked blocked_ and the
// handler); the caller also wakes the worker once the semaphore is released.
int IoQueue::appendLocked(int type, void *data)
{
    // We don't have to lock the tip of the queue...
    // If it exists, it won't be touched, if it does not exist, it is not being processed
    // Allocate FIRST and bail cleanly on OOM (fork parity): the old shape malloc'd straight into
    // head_/tail_->next and then dereferenced the result, so an allocation failure was a NULL
    // write plus a corrupted queue tail -- with the semaphore held.
    Request *req = static_cast<Request *>(malloc(sizeof(Request)));
    if (!req)
        return IO_ERR_TOO_MANY_REQUESTS;

    if (!tail_)
        head_ = req;
    else
        tail_->next = req;
    tail_ = req;

    req->next = NULL;
    req->type = type;
    req->data = data;

    if (type > 0 && type < IO_REQ_TYPE_COUNT) {
        pendingByType_[type]++;
        totalByType_[type]++;
    }
    return IO_OK;
}

int IoQueue::put(int type, void *data)
{
    int result;

    if (blocked_)
        return IO_ERR_IO_BLOCKED;

    // check the type before queueing
    if (!findHandler(type))
        return IO_ERR_INVALID_HANDLER;

    WaitSema(endSema_);
    result = appendLocked(type, data);
    SignalSema(endSema_);

    // Worker thread cannot wake itself up (WakeupThread will return an error), but it will find the new request before sleeping.
    if (result == IO_OK)
        WakeupThread(threadId_);
    return result;
}

int IoQueue::putUnlessWaiting(int type, void *data)
{
    int result = IO_OK, found = 0;

    if (blocked_)
        return IO_ERR_IO_BLOCKED;
    if (!findHandler(type))
        return IO_ERR_INVALID_HANDLER;

    // endSema_ only, as the plain enqueue takes it: it is held for a link or an unlink and never across
    // a handler, so this is safe from the GUI thread. (procSema_ is held across the whole drain and
    // must never be taken here -- see pendingByType_.) The scan and the append are ONE hold, so the GUI
    // thread and the worker asking at once cannot both miss each other and queue two (CodeRabbit).
    WaitSema(endSema_);
    for (Request *r = head_; r != NULL && !found; r = r->next)
        found = (r != current_ && r->type == type && r->data == data); // not started: sees the change
    if (!found)
        result = appendLocked(type, data);
    SignalSema(endSema_);

    if (!found && result == IO_OK)
        WakeupThread(threadId_);
    return result;
}

void IoQueue::stop()
{
    // termination requested flag
    terminate_ = 1;

    // wake up and wait for end
    WakeupThread(threadId_);
}

int IoQueue::pendingCount()
{
    int count = 0;

    // The worker can free the head while we wait. Sample only under the lock.
    WaitSema(procSema_);
    Request *req = head_;

    while (req) {
        count++;
        req = req->next;
    }

    SignalSema(procSema_);

    return count;
}

int IoQueue::blockOps(int block, int timeoutTicks)
{
    ee_thread_status_t status;
    int ThreadID;

    if (block && !blocked_) {
        blocked_ = 1;

        ThreadID = GetThreadId();
        // EE ReferThreadStatus() returns the thread status (>= 0) or a negative error, NOT 0 on
        // success (that is the IOP thbase variant). An `== 0` test is always false, so haveStatus
        // would always be 0 and the saved priority below would never be restored -- leaving this
        // (usually the main GUI) thread pinned at priority 90 for the rest of the session.
        int haveStatus = (ReferThreadStatus(ThreadID, &status) >= 0);
        ChangeThreadPriority(ThreadID, 90);

        // Wait for the in-flight IO handler(s) to finish. timeoutTicks < 0 waits unbounded
        // (the historical behaviour); a non-negative value caps the wait. The NBD preflight
        // passes a bound so a request stuck on a removed/slow device fails the start instead
        // of freezing after audio and pads have already been dismantled.
        while (hasPending()) {
            if (timeoutTicks == 0)
                break;
            delay(1);
            if (timeoutTicks > 0)
                timeoutTicks--;
        }

        // Only restore the saved priority if ReferThreadStatus actually filled it.
        if (haveStatus)
            ChangeThreadPriority(ThreadID, status.current_priority);

        // now all io should be blocked
    } else if (!block && blocked_) {
        blocked_ = 0;
    }

    return IO_OK;
}

// ---- C-callable API (ioman.h) -------------------------------------------------------------------

int ioGetPending(int type)
{
    return gQueue.pending(type);
}

int ioRegisterHandler(int type, io_request_handler_t handler)
{
    return gQueue.registerHandler(type, handler);
}

void ioInit(void)
{
    gQueue.start();
}

int ioPutRequest(int type, void *data)
{
    return gQueue.put(type, data);
}

int ioPutRequestUnlessWaiting(int type, void *data)
{
    return gQueue.putUnlessWaiting(type, data);
}

void ioEnd(void)
{
    gQueue.stop();
}

int ioGetPendingRequestCount(void)
{
    return gQueue.pendingCount();
}

int ioHasPendingRequests(void)
{
    return gQueue.hasPending() ? 1 : 0;
}

#ifdef __EESIO_DEBUG
static char tbuf[2048];
#endif

int ioPrintf(const char *format, ...)
{
    if (gQueue.running())
        gQueue.lockPrint();

    va_list args;
    va_start(args, format);
#ifdef __EESIO_DEBUG
    int ret = vsnprintf((char *)tbuf, sizeof(tbuf), format, args);
    sio_putsn(tbuf);
#else
    int ret = vprintf(format, args);
#endif
    va_end(args);

    if (gQueue.running())
        gQueue.unlockPrint();

    return ret;
}

int ioBlockOpsTimed(int block, int timeoutTicks)
{
    return gQueue.blockOps(block, timeoutTicks);
}

int ioBlockOps(int block)
{
    // Unbounded wait (historical behavior) for all non-teardown callers.
    return ioBlockOpsTimed(block, -1);
}
