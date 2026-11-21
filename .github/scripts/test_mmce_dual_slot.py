"""Dual-MMCE review regressions: favorites, hidden-slot polling and VMC setup."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
fav = (root / "src/sources/favsupport.cpp").read_text(encoding="utf-8")
mmce = (root / "src/sources/mmcesupport.cpp").read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    end = source.index("\n}\n", start)
    return source[start:end + 3]


favorites = r'''
#include <assert.h>
#include <stddef.h>
enum { BDM_MODE = 0, BDM_MODE7 = 7, ETH_MODE, HDD_MODE, APP_MODE, MMCE_MODE, FAV_MODE,
       UDPFS_MODE, HTTP_MODE, MMCE_MODE1, MODE_COUNT };
#define BDM_MODE_LAST BDM_MODE7
enum { FAV_KIND_ISO, FAV_KIND_VCD, FAV_KIND_CUE, FAV_KIND_ELF };
enum { LIB_VIEW_ISO, LIB_VIEW_PS1, LIB_VIEW_ELF };
#define ITEM_VIEW_FORCE_PS1 2
struct item_list_t { int mode, match, vcd, cue; };
struct opl_io_module_t { item_list_t *support; };
struct submenu_list_t { struct { int favourited; } item; };
static item_list_t slots[2], unrelated;
static opl_io_module_t modules[2], unrelatedModule;
static opl_io_module_t *oplGetModule(int mode)
{
    if (mode == MMCE_MODE) { modules[0].support = &slots[0]; return &modules[0]; }
    if (mode == MMCE_MODE1) { modules[1].support = &slots[1]; return &modules[1]; }
    if (mode == FAV_MODE) { unrelatedModule.support = &unrelated; return &unrelatedModule; }
    return NULL;
}
static int favResolveStoredId(item_list_t *source, int, const char *, int, int *out)
{
    if (source != NULL && source->match) *out = 7;
    return source != NULL && source->match;
}
static int favSupportLaunches(const item_list_t *source, int needCue)
{
    return needCue ? source->cue : source->vcd;
}
static submenu_list_t *favFindVisibleSource(opl_io_module_t *, int, const char *, int) { return NULL; }
static void favVcdMarkStar(opl_io_module_t *, int, const char *) {}
''' + function(fav, "static int favKindIsNameAddressed(") + function(fav, "static item_list_t *favResolve(") + r'''
static item_list_t *resolve(int mode, int *outMode)
{
    int outId = 0;
    return favResolve(mode, 0, "Game", FAV_KIND_ISO, outMode, &outId, 0);
}
static item_list_t *resolveNamed(int mode, int *outMode, int *outId)
{
    return favResolve(mode, 0, "Game", FAV_KIND_VCD, outMode, outId, 0);
}
int main()
{
    for (int i = 0; i < 2; i++) {
        slots[i].mode = i ? MMCE_MODE1 : MMCE_MODE;
        slots[i].vcd = slots[i].cue = 1;
    }
    int outMode = -1;
    slots[0].match = 0; slots[1].match = 1;
    assert(resolve(MMCE_MODE, &outMode) == &slots[1] && outMode == MMCE_MODE1);
    slots[0].match = slots[1].match = 1;
    assert(resolve(MMCE_MODE, &outMode) == &slots[0] && outMode == MMCE_MODE);
    slots[0].match = 1; slots[1].match = 0;
    assert(resolve(MMCE_MODE1, &outMode) == NULL);
    unrelated.match = 1;
    slots[0].match = 0; slots[1].match = 1;
    assert(resolve(MMCE_MODE, &outMode) == &slots[1] && outMode == MMCE_MODE1);
    slots[1].match = 0;
    int outId = -1;
    assert(resolveNamed(MMCE_MODE1, &outMode, &outId) == NULL);
    slots[1].match = 1;
    assert(resolveNamed(MMCE_MODE1, &outMode, &outId) == &slots[1] && outId == 7);
}
'''

name_lookup = r'''
#include <assert.h>
#include <string.h>
enum { FAV_KIND_ISO, FAV_KIND_VCD, FAV_KIND_CUE, FAV_KIND_ELF };
enum { APP_MODE = 1, ETH_MODE = 2, MMCE_MODE = 11, MMCE_MODE1 = 13 };
#define ITEM_VIEW_FORCE_PS1 2
#define ITEM_VIEW_FORCE_ISO 1
struct item_list_t { int mode, viewOverride; };
static int cueRow = 0;
struct Source {
    int count(item_list_t *) { return 2; }
    char *name(item_list_t *, int id) { return names[id]; }
    int ps1RowKind(item_list_t *, int id, int *ember) { *ember = (id == cueRow); return 1; }
    char names[2][16] = {"Other", "Game"};
};
static Source source;
static Source *gsOf(item_list_t *) { return &source; }
static int ethResolveIsoFavourite(int, const char *, int *) { return 0; }
static int ensureCalls;
static int mmceEnsurePs1List(item_list_t *) { ensureCalls++; return 1; }
''' + function(fav, "static int favKindIsNameAddressed(") + function(fav, "static int favMmcePs1KindMatches(") + function(fav, "static int favResolveStoredId(") + r'''
int main()
{
    item_list_t list = {};
    list.mode = MMCE_MODE1;
    int out = -1;
    cueRow = 0;
    assert(favResolveStoredId(&list, 0, "Game", FAV_KIND_VCD, &out) == 1 && out == 1);
    cueRow = 1;
    assert(favResolveStoredId(&list, 1, "Game", FAV_KIND_CUE, &out) == 1 && out == 1);
    assert(favResolveStoredId(&list, 1, "Game", FAV_KIND_VCD, &out) == 0);
    cueRow = 0;
    assert(favResolveStoredId(&list, 1, "Game", FAV_KIND_CUE, &out) == 0);
    assert(favResolveStoredId(&list, 0, "Game", FAV_KIND_ISO, &out) == 0);
    assert(favResolveStoredId(&list, 0, "Missing", FAV_KIND_VCD, &out) == 0);
    // Same title on both cores: saved index may point to the wrong one.
    strcpy(source.names[0], "Game");
    assert(favResolveStoredId(&list, 0, "Game", FAV_KIND_VCD, &out) == 1 && out == 1);
    assert(favResolveStoredId(&list, 1, "Game", FAV_KIND_CUE, &out) == 1 && out == 0);
    // A PS1 lookup must not mutate this source page's visible view.
    assert(list.viewOverride == 0);
    assert(ensureCalls > 0);
}
'''

moved_identity = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { BDM_MODE = 0, BDM_MODE_LAST = 7, APP_MODE = 8, MMCE_MODE = 11, MMCE_MODE1 = 13 };
enum { FAV_KIND_ISO, FAV_KIND_VCD, FAV_KIND_CUE, FAV_KIND_ELF };
#define FAV_TEXT_MAX 256
#define FAV_MAX_ITEMS 512
typedef struct { int mode, id, icon_id, text_id, kind; char text[FAV_TEXT_MAX]; } fav_raw_t;
struct item_list_t {};
typedef struct { item_list_t *owner; int mode, id, icon_id, text_id, kind; char *text; char *display; int starred; } fav_rec_t;
static fav_raw_t stored[2];
static int storedCount = 1;
static int writtenCount = -1;
static int legacyResolvedMode = MMCE_MODE1;
static item_list_t *favResolve(int mode, int id, const char *text, int kind, int *outMode, int *outId, int markStars)
{
    (void)mode; (void)id; (void)text; (void)kind;
    assert(markStars == 0);
    *outMode = legacyResolvedMode; *outId = 1;
    static item_list_t resolved;
    return legacyResolvedMode == MMCE_MODE1 ? &resolved : NULL;
}
static fav_raw_t *favReadFile(int *count, int *editable = NULL, const char *name = NULL)
{
    (void)name;
    fav_raw_t *copy = (fav_raw_t *)malloc(sizeof(fav_raw_t) * (storedCount ? storedCount : 1));
    if (copy != NULL && storedCount)
        memcpy(copy, stored, sizeof(fav_raw_t) * storedCount);
    *count = copy != NULL ? storedCount : 0;
    if (editable != NULL) *editable = 1;
    return copy;
}
static int favWriteFile(fav_raw_t *recs, int count)
{
    assert(count >= 0 && count <= 2);
    if (count)
        memcpy(stored, recs, sizeof(fav_raw_t) * count);
    storedCount = count;
    writtenCount = count;
    return 1;
}
class FavSupport
{
public:
    int addItem(int mode, int id, int icon_id, int text_id, const char *text, int kind);
    int removeByIdAndText(int mode, int id, const char *text, int kind);
};
''' + function(fav, "static int favIdsMatchForMode(") + function(fav, "static int favKindIsNameAddressed(") + function(fav, "static int favModesMatch(") + function(fav, "static int favRecordMatchesResolved(") + function(fav, "static int favRecIsStarred(") + function(fav, "int FavSupport::addItem(") + function(fav, "int FavSupport::removeByIdAndText(") + r'''
static void storedRecord(int kind)
{
    memset(stored, 0, sizeof(stored));
    storedCount = 1;
    stored[0].mode = MMCE_MODE;
    stored[0].id = 0;
    stored[0].kind = kind;
    strcpy(stored[0].text, "Game");
    writtenCount = -1;
}
int main()
{
    FavSupport store;
    storedRecord(FAV_KIND_VCD);
    assert(store.addItem(MMCE_MODE, 1, 0, 0, "Game", FAV_KIND_VCD) == 1 && writtenCount == -1);
    assert(store.removeByIdAndText(MMCE_MODE, 1, "Game", FAV_KIND_VCD) == 1 && writtenCount == 0);
    storedRecord(FAV_KIND_CUE);
    assert(store.addItem(MMCE_MODE, 1, 0, 0, "Game", FAV_KIND_CUE) == 1 && writtenCount == -1);
    assert(store.removeByIdAndText(MMCE_MODE, 1, "Game", FAV_KIND_CUE) == 1 && writtenCount == 0);
    storedRecord(FAV_KIND_ISO);
    assert(store.addItem(MMCE_MODE, 1, 0, 0, "Game", FAV_KIND_ISO) == 1 && writtenCount == 2);
    writtenCount = -1;
    assert(store.removeByIdAndText(MMCE_MODE, 1, "Game", FAV_KIND_ISO) == 1 && writtenCount == 1);
    storedRecord(FAV_KIND_VCD);
    assert(store.addItem(MMCE_MODE1, 1, 0, 0, "Game", FAV_KIND_VCD) == 1 && writtenCount == -1);
    assert(store.removeByIdAndText(MMCE_MODE1, 1, "Game", FAV_KIND_VCD) == 1 && writtenCount == 0);
    storedRecord(FAV_KIND_VCD);
    stored[0].mode = MMCE_MODE1;
    assert(store.removeByIdAndText(MMCE_MODE, 1, "Game", FAV_KIND_VCD) == 0 && writtenCount == -1);
    storedRecord(FAV_KIND_VCD);
    legacyResolvedMode = MMCE_MODE;
    assert(store.addItem(MMCE_MODE1, 1, 0, 0, "Game", FAV_KIND_VCD) == 1 && writtenCount == 2);
    writtenCount = -1;
    assert(store.removeByIdAndText(MMCE_MODE1, 1, "Game", FAV_KIND_VCD) == 1 && writtenCount == 1);
    fav_rec_t row = {};
    row.mode = MMCE_MODE1; row.id = 1; row.kind = FAV_KIND_VCD; row.text = (char *)"Game";
    legacyResolvedMode = MMCE_MODE1;
    assert(favRecIsStarred(&row, stored, 1) == 1);
    legacyResolvedMode = MMCE_MODE;
    assert(favRecIsStarred(&row, stored, 1) == 0);
}
'''

ps1_ensure = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MMCE_VCD_SCAN_RETRY_MAX 15
struct item_list_t { int mode, updateDelay; };
#define IO_MENU_UPDATE_DEFFERED 1
static int queued;
struct MmceSlotState {
    char prefix[16];
    void *ps1Games;
    int ps1GameCount, ps1Scanned, ps1ScanFailed, ps1ScanRetries;
    void getDeviceRoot(char *out, int size) { snprintf(out, size, "mmce1:/"); }
};
static MmceSlotState slot;
static item_list_t list;
static int ioPutRequestUnlessWaiting(int request, void *data) { assert(request == IO_MENU_UPDATE_DEFFERED && data == &list.mode); queued++; return 0; }
static MmceSlotState *mmceSlotStateOf(item_list_t *) { return &slot; }
static int fillResult, fillCalls, needsRetry;
static int ps1FillGameList(const char *root, void *, int &) {
    assert(!strcmp(root, "mmce1:/"));
    fillCalls++;
    return fillResult;
}
static int ps1GameListNeedsRetry(void) { return needsRetry; }
''' + function(mmce, "int mmceEnsurePs1List(") + r'''
int main() {
    strcpy(slot.prefix, "mmce1:/");
    fillResult = 1;
    assert(mmceEnsurePs1List(&list) == 1 && fillCalls == 1 && slot.ps1Scanned && slot.ps1GameCount == 1);
    assert(mmceEnsurePs1List(&list) == 1 && fillCalls == 1);
    slot.ps1Scanned = 0; slot.ps1ScanFailed = 1; slot.ps1ScanRetries = MMCE_VCD_SCAN_RETRY_MAX;
    fillResult = -1;
    assert(mmceEnsurePs1List(&list) == 0 && fillCalls == 1);
    slot.prefix[0] = 0;
    assert(mmceEnsurePs1List(&list) == 0 && fillCalls == 1);
    strcpy(slot.prefix, "mmce1:/");
    slot.ps1Games = NULL; slot.ps1Scanned = 0; slot.ps1ScanFailed = 0; slot.ps1ScanRetries = 0;
    needsRetry = 1; fillResult = 1; fillCalls = 0;
    assert(mmceEnsurePs1List(&list) == 1 && slot.ps1ScanRetries == 1);
    slot.ps1Games = (void *)1; queued = 0;
    assert(mmceEnsurePs1List(&list) == 0 && fillCalls == 1 && queued == 1);
}
'''

vmc = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef unsigned int u32;
#define _STR_VMC_FILE_ERROR 1
struct config_set_t {};
typedef struct { int mc_flag, page_size, pages_per_block, pages_per_cluster, clusters_per_card; } vmc_superblock_t;
typedef struct {
    int fd, active, flags;
    struct { int page_size, block_size, card_size; } specs;
} mmce_vmc_infos_t;
static unsigned char mmce_mcemu_irx[512];
static int size_mmce_mcemu_irx = sizeof(mmce_mcemu_irx);
static int mmceMcemuSlots[2] = {-2, -2};
static int nextFd, ioctlResult, closes, warnings;
static void configGetVMC(config_set_t *, char *name, int size, int id)
{
    snprintf(name, size, "%s", id == 0 ? "Card" : "");
}
static int sysCheckVMC(const char *, const char *, const char *, int, vmc_superblock_t *superblock)
{
    memset(superblock, 0, sizeof(*superblock));
    superblock->page_size = 512;
    superblock->pages_per_block = 16;
    superblock->pages_per_cluster = 2;
    superblock->clusters_per_card = 8192;
    return 8;
}
static int fileXioOpen(const char *, int, int) { return nextFd; }
static int fileXioIoctl2(int, int, void *, int, void *, int) { return ioctlResult; }
static int fileXioClose(int fd) { assert(fd == nextFd); closes++; return 0; }
static int nativeVmcSlotWord(void *, int, int id, int *, int) { return id * (int)(sizeof(mmce_vmc_infos_t) / sizeof(u32)); }
static mmce_vmc_infos_t descriptor(int id)
{
    mmce_vmc_infos_t out;
    memcpy(&out, ((u32 *)mmce_mcemu_irx) + id * (sizeof(out) / sizeof(u32)), sizeof(out));
    return out;
}
namespace host {
struct UI { void warningId(int, int) { warnings++; } };
static UI instance;
static UI &ui() { return instance; }
}
''' + function(mmce, "static void mmcePrepareVmcSlots(") + r'''
int main()
{
    config_set_t config;
    int fds[2] = {-1, -1};
    int size = 0;
    nextFd = 10; ioctlResult = 7;
    mmcePrepareVmcSlots("mmce0:/", &config, fds, &size);
    assert(fds[0] == 10 && closes == 0 && size == sizeof(mmce_mcemu_irx));
    assert(descriptor(0).active == 1 && descriptor(0).fd == 7 && descriptor(1).active == 0);
    nextFd = 11; ioctlResult = -1;
    fds[0] = fds[1] = -1; size = 0;
    mmcePrepareVmcSlots("mmce0:/", &config, fds, &size);
    assert(fds[0] == -1 && closes == 1 && size == 0 && warnings == 1);
    assert(descriptor(0).active == 0 && descriptor(1).active == 0);
    nextFd = 12; ioctlResult = 9;
    mmcePrepareVmcSlots("mmce0:/", &config, fds, &size);
    assert(fds[0] == 12 && size == sizeof(mmce_mcemu_irx));
    assert(descriptor(0).active == 1 && descriptor(0).fd == 9 && descriptor(1).active == 0);
}
'''

needs = function(mmce, "int MmceSource::needsUpdate(")
probe_ok = "peer->probePresence();" in needs and "wasPresent != peer->present" in needs and "ioPutRequestUnlessWaiting(IO_MENU_UPDATE_DEFFERED, &peerList->mode)" in needs
all_games = function(fav, "int FavSupport::gatherPass(")
all_games_ready = all_games.index("mmceEnsurePs1List(src);") < all_games.index("if (src->mode == APP_MODE)") and "shelf == LIB_VIEW_PS1" in all_games

with tempfile.TemporaryDirectory(prefix="mmce-dual-slot-") as tmp:
    folder = Path(tmp)
    failures = []
    for label, source in (("favorites", favorites), ("name_lookup", name_lookup), ("moved_identity", moved_identity), ("ps1_ensure", ps1_ensure), ("vmc", vmc)):
        src, exe = folder / (label + ".cpp"), folder / label
        src.write_text(source, encoding="utf-8")
        result = subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", str(src), "-o", str(exe)], capture_output=True, text=True)
        if result.returncode:
            failures.append(label + " harness compile failed: " + result.stderr)
        else:
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            if result.returncode:
                failures.append(label + " regression: " + result.stderr)
    bad_source = vmc.replace("mmce_vmc_infos.fd = vmc_handle;", "mmce_vmc_infos.fd = -1;")
    bad_src, bad_exe = folder / "vmc_bad.cpp", folder / "vmc_bad"
    bad_src.write_text(bad_source, encoding="utf-8")
    result = subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", str(bad_src), "-o", str(bad_exe)], capture_output=True, text=True)
    if result.returncode:
        failures.append("VMC before-control compile failed: " + result.stderr)
    else:
        result = subprocess.run([str(bad_exe)], capture_output=True, text=True)
        if result.returncode == 0:
            failures.append("VMC descriptor assertions accepted the wrong ioctl handle")
    if not probe_ok:
        failures.append("Manual slot-0 fallback does not refresh changed slot-1 game list")
    if not all_games_ready:
        failures.append("All Games did not prepare MMCE PS1 backing lists before gathering")
    if failures:
        raise SystemExit("\n".join(failures))
print("PASS: legacy MMCE favorites, hidden-slot probing and failed-VMC retry behavior")
