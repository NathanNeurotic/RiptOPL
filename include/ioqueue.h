#ifndef __IOQUEUE_H
#define __IOQUEUE_H

// The IO worker: one thread draining a FIFO of typed requests through registered handlers.
// C++ only; C and legacy callers use the ioPutRequest()/ioBlockOps()/... functions in ioman.h, which
// forward to the single instance in src/ioman.cpp.

#include <kernel.h>
#include "ioman.h"
#include "opl.h" // IO_REQ_TYPE_COUNT

class IoQueue
{
public:
    static constexpr int kMaxHandlers = 64;

    constexpr IoQueue() = default;
    IoQueue(const IoQueue &) = delete;
    IoQueue &operator=(const IoQueue &) = delete;

    // Start (or restart after a refused autolaunch) the worker thread with an empty, open queue.
    void start();
    // Ask the worker to stop; it deletes its semaphores and pending requests on the way out.
    void stop();

    int registerHandler(int type, io_request_handler_t handler);
    int put(int type, void *data);
    // As put(), but does nothing (and returns IO_OK) when an identical type+data request is waiting.
    int putUnlessWaiting(int type, void *data);
    int pendingCount();
    bool hasPending() const { return head_ != NULL; }
    int pending(int type) const;
    unsigned int total(int type) const;
    // Drain and block (or unblock) new requests; timeoutTicks < 0 waits unbounded.
    int blockOps(int block, int timeoutTicks);

    // ioPrintf serialisation, only while the worker runs.
    bool running() const { return workerRunning_ != 0; }
    void lockPrint() { WaitSema(printSema_); }
    void unlockPrint() { SignalSema(printSema_); }

    // Thread entry (the kernel wants a plain function pointer).
    static void workerEntry(void *arg);

private:
    struct Request
    {
        int type;
        void *data;
        Request *next;
    };

    struct Handler
    {
        int type;
        io_request_handler_t handler;
    };

    io_request_handler_t findHandler(int type) const;
    void process(Request *req);
    void workerLoop();
    // Link a request at the tail. Caller holds endSema_ and has checked blocked_ and the handler.
    int appendLocked(int type, void *data);

    // Circular request queue.
    Request *head_ = NULL;
    Request *tail_ = NULL;
    // The request the worker is executing (still linked at the head until it returns), or NULL.
    Request *volatile current_ = NULL;

    Handler handlers_[kMaxHandlers] = {};
    int handlerCount_ = 0;

    s32 threadId_ = 0;
    s32 procSema_ = 0; // lock for tip processing
    s32 endSema_ = 0;  // lock for queue end
    s32 printSema_ = 0;

    int terminate_ = 0;
    int blocked_ = 0;
    int workerRunning_ = 0;

    // DIAGNOSTIC ONLY: per-type counters for the debug HUD; read unsynchronised from the GUI thread.
    int pendingByType_[IO_REQ_TYPE_COUNT] = {};
    unsigned int totalByType_[IO_REQ_TYPE_COUNT] = {};
};

#endif
