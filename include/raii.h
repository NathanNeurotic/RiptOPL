// Small RAII helpers for the EE code (C++ only; do not include from C or the IOP modules).
//
// Exceptions are off in this build, so none of these throw; they exist to make every exit path of a function
// release what it acquired, replacing `goto fail` ladders and hand-ordered free()/close() chains.
#ifndef RAII_H
#define RAII_H

#include <memory>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <utility>

namespace raii
{

// A file descriptor closed on scope exit. Valid when >= 0, like the raw int it replaces.
class UniqueFd
{
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    UniqueFd(UniqueFd &&o) noexcept : fd_(o.release()) {}
    UniqueFd &operator=(UniqueFd &&o) noexcept
    {
        if (this != &o)
            reset(o.release());
        return *this;
    }
    UniqueFd(const UniqueFd &) = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;
    ~UniqueFd() { reset(); }

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    explicit operator bool() const { return valid(); }
    // Give up ownership without closing (e.g. the descriptor is handed to something that closes it).
    int release()
    {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }
    void reset(int fd = -1)
    {
        if (fd_ >= 0)
            close(fd_);
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

struct FileCloser
{
    void operator()(FILE *f) const
    {
        if (f)
            fclose(f);
    }
};
using UniqueFile = std::unique_ptr<FILE, FileCloser>;

struct FreeDeleter
{
    void operator()(void *p) const { free(p); }
};
// Memory from malloc/calloc/realloc/memalign, released with free().
template <typename T = void>
using UniqueMalloc = std::unique_ptr<T, FreeDeleter>;

// Objects created with makeUnique() live in malloc memory. This deliberately avoids `new (std::nothrow)`: naming the
// std::nothrow object makes the linker pull in libstdc++'s new_handler.o and with it the whole exception/unwinder/
// demangler machinery (~120 KB) that this exception-free build otherwise never links.
struct DestroyFree
{
    template <typename T>
    void operator()(T *p) const
    {
        if (p) {
            p->~T();
            free(p);
        }
    }
};
template <typename T>
using UniquePtr = std::unique_ptr<T, DestroyFree>;

// Constructs a T in malloc memory; an empty pointer when malloc fails (T's constructor must not fail).
template <typename T, typename... Args>
UniquePtr<T> makeUnique(Args &&...args)
{
    void *mem = malloc(sizeof(T));
    if (!mem)
        return UniquePtr<T>();
    return UniquePtr<T>(new (mem) T(std::forward<Args>(args)...));
}

// Runs a callable when the scope ends, on every path. dismiss() cancels it (e.g. after a successful commit).
template <typename F>
class ScopeExit
{
public:
    explicit ScopeExit(F f) : f_(std::move(f)) {}
    ScopeExit(ScopeExit &&o) noexcept : f_(std::move(o.f_)), active_(o.active_) { o.active_ = false; }
    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
    ~ScopeExit()
    {
        if (active_)
            f_();
    }
    void dismiss() { active_ = false; }

private:
    F f_;
    bool active_ = true;
};
template <typename F>
ScopeExit<F> scopeExit(F f)
{
    return ScopeExit<F>(std::move(f));
}

} // namespace raii

#endif // RAII_H
