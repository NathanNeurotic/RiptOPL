#ifndef __GUIQUEUE_H
#define __GUIQUEUE_H

#include <kernel.h>
#include "gui.h"

// FIFO of GUI updates posted by other threads (the IO worker) and applied on the GUI thread.
// Guarded by a binary semaphore; sema_ < 0 means "not created yet".
class DeferredOpQueue
{
public:
    void init();
    void shutdown();

    // Takes ownership of `op`: it is freed after being applied, or here when the queue cannot allocate
    // (-1). Returns the serial id of the operation otherwise.
    int enqueue(struct gui_update_t *op);
    // Apply every queued op with `apply`, freeing each (op and queue node). The lock is held for the
    // whole drain, including the tail reset, so an enqueue cannot interleave with it.
    void drain(void (*apply)(struct gui_update_t *));

private:
    struct Node
    {
        struct gui_update_t *item;
        Node *next;
    };

    Node *head_ = NULL;
    Node *tail_ = NULL;
    s32 sema_ = -1;
    int scheduled_ = 0;
    int completed_ = 0;
};

#endif
