// raii.h / sema_lock.h: ownership, move, release/dismiss and early-exit behaviour.
#include <fcntl.h>
#include <string.h>
#include "../../include/raii.h"
#include "../../include/sema_lock.h"
#include "check.h"

static bool fdOpen(int fd) { return fcntl(fd, F_GETFD) != -1; }

static int earlyReturn(bool bail, int *cleaned)
{
    auto guard = raii::scopeExit([&] { ++*cleaned; });
    if (bail)
        return -1; // every exit path runs the guard
    return 0;
}

int main()
{
    int raw;
    {
        raii::UniqueFd fd(open("/dev/null", O_RDONLY));
        CHECK(fd.valid() && fdOpen(fd.get()));
        raw = fd.get();
        raii::UniqueFd moved(std::move(fd));
        CHECK(!fd.valid() && moved.get() == raw && fdOpen(raw)); // moved-from does not close
    }
    CHECK(!fdOpen(raw)); // closed at scope end

    raii::UniqueFd none;
    CHECK(!none && none.get() == -1);
    raii::UniqueFd bad(open("/nonexistent/x", O_RDONLY));
    CHECK(!bad.valid()); // a failed open() is "not valid", close() is never called on -1

    int kept;
    {
        raii::UniqueFd fd(open("/dev/null", O_RDONLY));
        kept = fd.release();
    }
    CHECK(fdOpen(kept)); // release() hands ownership away
    close(kept);

    {
        raii::UniqueFd a(open("/dev/null", O_RDONLY));
        int old = a.get();
        a.reset(open("/dev/null", O_RDONLY)); // reset closes the previous descriptor
        CHECK(!fdOpen(old) || a.get() == old);
    }

    // UniqueMalloc frees with free(); realloc'd blocks work too (ASan would flag a mismatch or a leak).
    {
        raii::UniqueMalloc<char> buf(static_cast<char *>(malloc(16)));
        strcpy(buf.get(), "hello");
        buf.reset(static_cast<char *>(realloc(buf.release(), 64)));
        CHECK(strcmp(buf.get(), "hello") == 0);
    }
    {
        raii::UniqueFile f(tmpfile());
        CHECK(f != nullptr);
    }
    raii::UniqueFile nullFile(nullptr); // destructor tolerates NULL

    int cleaned = 0;
    CHECK(earlyReturn(true, &cleaned) == -1 && cleaned == 1);
    CHECK(earlyReturn(false, &cleaned) == 0 && cleaned == 2);
    {
        auto g = raii::scopeExit([&] { ++cleaned; });
        g.dismiss();
    }
    CHECK(cleaned == 2); // dismissed guards do nothing

    {
        raii::SemaLock lock(3);
        CHECK(g_semaDepth[3] == 1);
        lock.unlock();
        CHECK(g_semaDepth[3] == 0);
    } // destructor after unlock() must not signal twice
    CHECK(g_semaDepth[3] == 0);
    {
        raii::SemaLock early(-1); // not-yet-created semaphore: no-op, like guiLock()
        CHECK(g_semaWaits == 1);
    }
    CHECK(g_semaDepth[3] == 0);
    TEST_DONE("raii");
}
