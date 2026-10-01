// Scoped semaphore hold for the EE kernel (WaitSema/SignalSema). Separate from raii.h because it needs <kernel.h>.
#ifndef SEMA_LOCK_H
#define SEMA_LOCK_H

#include <kernel.h>

namespace raii
{

// Holds `id` from construction to destruction. A negative id means "semaphore not created yet" (early boot,
// single-threaded) and is a no-op, matching the guiLock() convention.
class SemaLock
{
public:
    explicit SemaLock(int id) : id_(id)
    {
        if (id_ >= 0)
            WaitSema(id_);
    }
    SemaLock(const SemaLock &) = delete;
    SemaLock &operator=(const SemaLock &) = delete;
    ~SemaLock() { unlock(); }

    // Release early (before the end of the scope); the destructor then does nothing.
    void unlock()
    {
        if (id_ >= 0) {
            SignalSema(id_);
            id_ = -1;
        }
    }

private:
    int id_;
};

} // namespace raii

#endif // SEMA_LOCK_H
