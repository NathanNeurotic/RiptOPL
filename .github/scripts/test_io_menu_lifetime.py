"""Run the production C++ queue snapshot and GUI append paths under failure."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    end = text.index("\n}", start) + 2
    return text[start:end]


io = (ROOT / "src/platform/ioman.cpp").read_text(encoding="utf-8")
queue = function(io, "int IoQueue::pendingCount()")
gui = (ROOT / "src/ui/gui.cpp").read_text(encoding="utf-8")
start = gui.index("case GUI_OP_APPEND_MENU:")
append = gui[start:gui.index("case GUI_OP_SELECT_MENU:", start)]

program = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
class IoQueue {
public:
    struct Request { Request *next; };
    Request *head_ = nullptr;
    int procSema_ = 5;
    int pendingCount();
};
static IoQueue q;
static IoQueue::Request oldHead{}, head{}, tail{};
static bool locked, mutate;
static int WaitSema(int id) {
    assert(id == q.procSema_ && !locked);
    if (mutate) { head.next = &tail; q.head_ = &head; }
    locked = true;
    return 0;
}
static int SignalSema(int id) {
    assert(id == q.procSema_ && locked);
    locked = false;
    return 0;
}
@QUEUE@
enum { GUI_OP_APPEND_MENU = 1 };
struct Row { int isFolder, lastPlayed, raBadged, favourited; char *raText; };
struct Entry { Row item; };
struct Menu { Entry *last, *submenu, *current, *pagestart; int remindLast; };
struct Update {
    struct { Menu *menu; Entry **subMenu; } menu;
    struct { int icon_id; char *text; int id, text_id, isFolder, lastPlayed, raBadged, favourited, selected, autoStart; char *raText; } submenu;
};
static Entry oldEntry{}, newEntry{};
static bool fail;
static int DisableCron = 1, gRememberLastPlayed = 1, gAutoStartLastPlayed = 1, KeyPressedOnce;
static Entry **appendFrom;
static Entry *submenuAppendItem(Entry **from, int, char *, int, int) {
    appendFrom = from;
    return fail ? nullptr : &newEntry;
}
static int frees;
[[maybe_unused]] static void freeTracked(void *p) { frees += p != nullptr; std::free(p); }
#define free freeTracked
static void appendRow(Update *item) {
    Entry *result;
    switch (GUI_OP_APPEND_MENU) {
@APPEND@
    }
}
#undef free
int main() {
    q.head_ = &oldHead; mutate = true;
    // A worker consumes/frees the old head before WaitSema returns.
    assert(q.pendingCount() == 2 && !locked);
    mutate = false; assert(q.pendingCount() == 2 && !locked);
    q.head_ = nullptr; assert(q.pendingCount() == 0 && !locked);
    Menu menu{&oldEntry, &oldEntry, &oldEntry, &oldEntry, 0};
    Entry *rows = &oldEntry;
    Update item{{&menu, &rows}, {0, nullptr, 7, 0, 1, 0, 1, 0, 1, 1, nullptr}};
    item.submenu.raText = strdup("RA Game");
    fail = true; appendRow(&item); // OOM: the badge snapshot is released, not leaked
    assert(appendFrom == &menu.last); // a list with rows is appended at its tracked tail, not walked from the head
#ifdef RETROACHIEVEMENTS
    assert(frees == 1);
#else
    std::free(item.submenu.raText);
#endif
    item.submenu.raText = nullptr;
    assert(menu.last == &oldEntry && menu.submenu == &oldEntry);
    assert(menu.current == &oldEntry && menu.pagestart == &oldEntry);
    assert(!menu.remindLast && DisableCron == 1);
    menu = {nullptr, nullptr, nullptr, nullptr, 0}; rows = nullptr;
    appendRow(&item);
    assert(appendFrom == &rows); // an empty list is started through its head
    assert(!menu.last && !menu.submenu && !menu.current && !menu.pagestart);
    assert(!menu.remindLast && DisableCron == 1);
    char *snapshot = item.submenu.raText = strdup("RA Game");
    menu.last = &oldEntry; // a stale tail on an empty list is never appended to
    fail = false; appendRow(&item);
    assert(appendFrom == &rows);
    assert(menu.last == &newEntry && menu.submenu == &newEntry);
    assert(menu.current == &newEntry && menu.pagestart == &newEntry);
    assert(menu.remindLast && DisableCron == 0 && newEntry.item.isFolder);
#ifdef RETROACHIEVEMENTS
    assert(newEntry.item.raBadged && newEntry.item.raText == snapshot && frees == 1); // ownership moves to the row
    std::free(snapshot);
#else
    std::free(snapshot);
#endif
    puts("PASS: C++ queue snapshot holds its lock; append OOM preserves GUI navigation");
}
'''.replace("@QUEUE@", queue).replace("@APPEND@", append)

with tempfile.TemporaryDirectory() as directory:
    src = Path(directory) / "lifetime.cpp"
    exe = Path(directory) / "lifetime"
    src.write_text(program, encoding="utf-8")
    for defines in ([], ["-DRETROACHIEVEMENTS"]):
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *defines, str(src), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)

# A queue that cannot take the op frees it, and with it the row's badge snapshot.
queue_cpp = (ROOT / "src/ui/guiqueue.cpp").read_text(encoding="utf-8")
oom = function(queue_cpp, "int DeferredOpQueue::enqueue(")
assert oom.index("free(op->submenu.raText);") < oom.index("free(op);")
