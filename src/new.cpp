// Global operator new/delete on top of malloc/free.
//
// The build has exceptions off, and the stock libstdc++ operator new drags in the throwing path
// (std::bad_alloc, new_handler, the unwinder: ~40 KB packed). With these definitions nothing in the program needs
// that machinery. The plain forms cannot throw, so on exhaustion they stop the program the way an uncaught
// bad_alloc would have; code that wants to survive out-of-memory uses `new (std::nothrow)`.
#include <new>
#include <stdlib.h>

void *operator new(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p)
        abort();
    return p;
}

void *operator new[](size_t size)
{
    return operator new(size);
}

void *operator new(size_t size, const std::nothrow_t &) noexcept
{
    return malloc(size ? size : 1);
}

void *operator new[](size_t size, const std::nothrow_t &) noexcept
{
    return malloc(size ? size : 1);
}

void operator delete(void *p) noexcept
{
    free(p);
}

void operator delete[](void *p) noexcept
{
    free(p);
}

void operator delete(void *p, size_t) noexcept
{
    free(p);
}

void operator delete[](void *p, size_t) noexcept
{
    free(p);
}

void operator delete(void *p, const std::nothrow_t &) noexcept
{
    free(p);
}

void operator delete[](void *p, const std::nothrow_t &) noexcept
{
    free(p);
}
