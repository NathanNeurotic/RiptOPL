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
    (void)out;
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
}
'''

name_lookup = r'''
#include <assert.h>
#include <string.h>
enum { FAV_KIND_ISO, FAV_KIND_VCD, FAV_KIND_CUE, FAV_KIND_ELF };
enum { APP_MODE = 1, ETH_MODE = 2 };
#define ITEM_VIEW_FORCE_PS1 2
#define ITEM_VIEW_FORCE_ISO 1
struct item_list_t { int mode, viewOverride; };
struct Source {
    int count(item_list_t *) { return 2; }
    char *name(item_list_t *, int id) { return names[id]; }
    char names[2][16] = {"Other", "Game"};
};
static Source source;
static Source *gsOf(item_list_t *) { return &source; }
static int ethResolveIsoFavourite(int, const char *, int *) { return 0; }
''' + function(fav, "static int favKindIsNameAddressed(") + function(fav, "static int favResolveStoredId(") + r'''
int main()
{
    item_list_t list = {};
    int out = -1;
    assert(favResolveStoredId(&list, 0, "Game", FAV_KIND_VCD, &out) == 1 && out == 1);
    out = -1;
    assert(favResolveStoredId(&list, 1, "Game", FAV_KIND_CUE, &out) == 1 && out == 1);
    assert(favResolveStoredId(&list, 0, "Game", FAV_KIND_ISO, &out) == 0);
    assert(favResolveStoredId(&list, 0, "Missing", FAV_KIND_VCD, &out) == 0);
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
probe_ok = "gMmceSupport.slots[1].probePresence();" in needs

with tempfile.TemporaryDirectory(prefix="mmce-dual-slot-") as tmp:
    folder = Path(tmp)
    failures = []
    for label, source in (("favorites", favorites), ("name_lookup", name_lookup), ("vmc", vmc)):
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
        failures.append("Manual slot-0 fallback does not probe hidden slot 1")
    if failures:
        raise SystemExit("\n".join(failures))
print("PASS: legacy MMCE favorites, hidden-slot probing and failed-VMC retry behavior")
