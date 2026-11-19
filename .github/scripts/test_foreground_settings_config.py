"""Exercise actual foreground settings/cache methods without an available IO worker."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/ui/menusys.cpp").read_text(encoding="utf-8")


def function(signature):
    """Extract the production method, retaining its control flow and ownership."""
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for i in range(brace, len(source)):
        depth += (source[i] == "{") - (source[i] == "}")
        if depth == 0:
            return source[start:i + 1]
    raise AssertionError(signature)


methods = "\n\n".join(function(s) for s in (
    "static int menuConfigSourceMode(", "config_set_t *menuLoadConfig()",
    "config_set_t *gameMenuLoadConfig(", "static int menuCanRequestItemConfig(",
))
program = r'''
#include <cassert>
#include <cstdio>
enum { FAV_MODE, MMCE_MODE, MMCE_MODE1, APP_MODE, HDD_MODE };
static inline int isMmceMode(int mode) { return mode == MMCE_MODE || mode == MMCE_MODE1; }
constexpr int MENU_APP_CONFIG_IDLE_FRAMES = 1, MENU_MMCE_CONFIG_IDLE_FRAMES = 3;
struct config_set_t { int serial; };
struct item_list_t { int mode; };
struct UIItem {};
struct Row { struct { int id; } item; } row{{8}};
struct Item { void *userdata; Row *current; } item;
struct Menu { Item *item; } menu;
static Menu *selected_item = &menu;
static config_set_t *itemConfig;
static item_list_t *itemConfigList;
static unsigned int itemConfigGeneration;
static int itemConfigId = -1, actionStatus, menuSemaId, depth, reads, frees, flushes;
static int guiInactiveFrames, ownerMode, mmceCancels, otherCancels;
static bool failRead, duplicate, switchList;
static item_list_t favorite{FAV_MODE}, mmce{MMCE_MODE}, mmce1{MMCE_MODE1}, apps{APP_MODE}, hdd{HDD_MODE};
static config_set_t *makeConfig() { return new config_set_t{++reads}; }
static void configFree(config_set_t *p) { assert(p); ++frees; delete p; }
static void WaitSema(int) { assert(depth++ == 0); }
static void SignalSema(int) { assert(--depth == 0); }
static void padRumbleFlush() { assert(depth == 0); ++flushes; }
static int favGetItemSourceMode(int id) { assert(id == row.item.id); return ownerMode; }
static int cacheAbortMmceImageLoadsTimed(int wait) { assert(wait == 0 && depth == 0); ++mmceCancels; return 0; }
static int cacheCancelPendingImageLoadsTimed(int wait) { assert(wait == 0 && depth == 0); ++otherCancels; return 0; }
struct Source {
    config_set_t *config(item_list_t *list, int id) {
        assert(depth == 0 && list == item.userdata && id == row.item.id);
        if (switchList) { itemConfigList = &hdd; itemConfigId = 99; item.userdata = &hdd; }
        if (duplicate) itemConfig = makeConfig();
        return failRead ? nullptr : makeConfig();
    }
} source;
static Source *gsOf(item_list_t *) { return &source; }
@METHODS@
static void reset(item_list_t *list) {
    if (itemConfig) configFree(itemConfig);
    itemConfig = nullptr; itemConfigId = 7; itemConfigList = list;
    menu.item = &item; item.current = &row; item.userdata = list;
    reads = frees = flushes = mmceCancels = otherCancels = 0;
    guiInactiveFrames = actionStatus = 0;
    failRead = duplicate = switchList = false;
}
int main() {
    const int mmceOwners[] = {MMCE_MODE, MMCE_MODE1};
    for (int mmceOwner : mmceOwners) {
        reset(&favorite); ownerMode = mmceOwner;
        // A new row below the settle threshold still loads directly; there is no worker stub.
        auto *loaded = gameMenuLoadConfig(nullptr);
        assert(loaded && loaded == itemConfig && itemConfigId == 8 && itemConfigList == &favorite);
        assert(reads == 1 && flushes == 1 && mmceCancels == 1 && otherCancels == 0 && actionStatus == 0);
        assert(gameMenuLoadConfig(nullptr) == loaded && reads == 1 && mmceCancels == 2);
        assert(!menuCanRequestItemConfig(&favorite));
        guiInactiveFrames = 2; assert(!menuCanRequestItemConfig(&favorite));
        guiInactiveFrames = 3; assert(menuCanRequestItemConfig(&favorite));
    }
    ownerMode = APP_MODE; guiInactiveFrames = 0; assert(!menuCanRequestItemConfig(&favorite));
    guiInactiveFrames = 1; assert(menuCanRequestItemConfig(&favorite));
    ownerMode = HDD_MODE; guiInactiveFrames = 0; assert(menuCanRequestItemConfig(&favorite));
    assert(menuConfigSourceMode(&apps) == APP_MODE); // unrelated Favorites selection cannot override a device
    reset(&mmce); assert(gameMenuLoadConfig(nullptr) && mmceCancels == 1 && otherCancels == 0);
    reset(&mmce1); assert(gameMenuLoadConfig(nullptr) && mmceCancels == 1 && otherCancels == 0);
    reset(&hdd); assert(gameMenuLoadConfig(nullptr) && otherCancels == 1 && mmceCancels == 0);
    for (int mmceOwner : mmceOwners) {
        reset(&favorite); ownerMode = mmceOwner; failRead = true;
        assert(!gameMenuLoadConfig(nullptr) && !itemConfig && !actionStatus);
        failRead = false; assert(gameMenuLoadConfig(nullptr)); // failed foreground reads remain retryable
    }
    reset(&favorite); duplicate = true;
    auto *loaded = gameMenuLoadConfig(nullptr);
    assert(loaded && loaded->serial == 1 && reads == 2 && frees == 1); // keep the published winner
    reset(&favorite); switchList = true;
    assert(!gameMenuLoadConfig(nullptr) && !itemConfig && frees == 1); // discard superseded read
    reset(&favorite); item.current = nullptr;
    assert(!gameMenuLoadConfig(nullptr) && itemConfigId == -1 && !itemConfigList && !reads);
    assert(menuConfigSourceMode(nullptr) == -1 && depth == 0);
    puts("PASS: foreground settings bypass settle/worker, honor Favorites bus policy and preserve cache ownership");
}
'''.replace("@METHODS@", methods)
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "test.cpp"
    exe = Path(tmp) / "test"
    cpp.write_text(program, encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
