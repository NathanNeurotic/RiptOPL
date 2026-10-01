// Deferred GUI operation queue. Split out of gui.cpp.

#include "include/opl.h"
#include "include/guiqueue.h"
#include "include/sema_lock.h"

#include <stdlib.h>

void DeferredOpQueue::init()
{
    head_ = NULL;
    tail_ = NULL;
    scheduled_ = 0;
    completed_ = 0;

    ee_sema_t sema;
    sema.init_count = 1;
    sema.max_count = 1;
    sema.option = 0;
    sema_ = CreateSema(&sema);
}

void DeferredOpQueue::shutdown()
{
    DeleteSema(sema_);
    sema_ = -1;
}

int DeferredOpQueue::enqueue(struct gui_update_t *op)
{
    raii::SemaLock lock(sema_);

    Node *up = static_cast<Node *>(malloc(sizeof(Node)));
    if (!up) {
        /* OOM: the lock releases on return so future callers are not permanently locked out. The op
           is ours to release too -- ownership transfers on enqueue, so a caller that got -1 back has
           no pointer left to free and would otherwise leak the very object we could not queue. */
        free(op);
        return -1;
    }
    up->item = op;
    up->next = NULL;

    if (!head_) {
        head_ = up;
        tail_ = up;
    } else {
        tail_->next = up;
        tail_ = up;
    }

    return scheduled_++;
}

void DeferredOpQueue::drain(void (*apply)(struct gui_update_t *))
{
    raii::SemaLock lock(sema_);

    while (head_) {
        apply(head_->item);

        Node *td = head_;
        head_ = head_->next;

        // FREE THE OPERATION, NOT JUST ITS QUEUE NODE. Every deferred op is TWO allocations --
        // guiOpCreate() mallocs the gui_update_t, enqueue() mallocs the list node that carries it --
        // and this loop only ever released the node. The op leaked, always.
        //
        // The rate is what makes it fatal rather than untidy: updateMenuFromGameList() calls
        // guiOpCreate(GUI_OP_APPEND_MENU) once PER GAME ROW, so a single list rebuild leaks one
        // object per row and every page change rebuilds. Enough navigation and the heap is gone.
        //
        // apply() reads the op but never retains the pointer, and submenu.text points into the
        // support's own storage rather than the op, so releasing it here is safe.
        free(td->item);
        free(td);

        completed_++;
    }
    // Clear the now-dangling tail pointer INSIDE the lock. Doing this after releasing it let an IO-thread
    // enqueue slip in (rebuild the list, set tail_) only to have it clobbered here -> the next enqueue's
    // tail_->next was a NULL deref. Not a rare interleaving: the IO worker is prio 30 and the GUI thread
    // prio 31, and lower wins on EE, so the thread woken by the release preempts us immediately.
    tail_ = NULL;
}
