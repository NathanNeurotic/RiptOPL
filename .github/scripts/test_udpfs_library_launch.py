"""Execute C++ ISO-only scans, partial PS1 scans and POPS launch refusal before teardown."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def run(name, code):
    directory = ROOT / "build/test-tmp"
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=directory) as tmp:
        src, exe = Path(tmp) / (name + ".cpp"), Path(tmp) / name
        src.write_text(code)
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run([*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


vcd = read("src/sources/vcdsupport.cpp")
run("safe_file_io", r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <algorithm>
enum { O_RDONLY, O_WRONLY, O_CREAT = 4, O_TRUNC = 8, SEEK_END, SEEK_SET, VCD_COPY_CHUNK = 4 };
static bool full, closeFails, readFails, writeFails, zeroWrite, removed;
static int sourceOffset, written, opens;
static int vcdMcHasSpace(const char*, int) { return !full; }
static int open(const char*, int flags, ...) { ++opens; return flags == O_RDONLY ? 1 : 2; }
static int lseek(int, int, int mode) { return mode == SEEK_END ? 6 : 0; }
static int read(int fd, void *out, int length) {
    assert(fd == 1); if (readFails) return -1;
    int size = std::min(length, 6 - sourceOffset);
    memcpy(out, "abcdef" + sourceOffset, size); sourceOffset += size; return size;
}
static int write(int fd, const void *buf, int length) {
    assert(fd == 2); if (writeFails) return -1; if (zeroWrite) return 0;
    int size = std::min(length, 2); // Successful partial writes must be retried.
    assert(!memcmp(buf, "abcdef" + written, size)); written += size; return size;
}
static int close(int fd) { return fd == 2 && closeFails ? -1 : 0; }
static int unlink(const char*) { removed = true; return 0; }
static void reset() {
    full = closeFails = readFails = writeFails = zeroWrite = removed = false;
    sourceOffset = written = opens = 0;
}
''' + function(vcd, "static int vcdSafeCopyFile(") + "\n" + function(vcd, "int vcdSafeWriteFile(") + r'''
int main() {
    reset(); assert(vcdSafeWriteFile("dst", "abcdef", 6) == 0 && written == 6 && !removed);
    reset(); assert(vcdSafeCopyFile("src", "dst") == 0 && written == 6 && !removed);
    for (int failure = 0; failure < 3; ++failure) {
        reset(); closeFails = failure == 0; writeFails = failure == 1; zeroWrite = failure == 2;
        assert(vcdSafeWriteFile("dst", "abcdef", 6) == -3 && removed);
        reset(); closeFails = failure == 0; writeFails = failure == 1; zeroWrite = failure == 2;
        assert(vcdSafeCopyFile("src", "dst") == -3 && removed);
    }
    reset(); closeFails = true;
    assert(vcdSafeWriteFile("broadcast", nullptr, 0) == -3 && removed);
    reset(); readFails = true; assert(vcdSafeCopyFile("src", "dst") == -3 && removed);
    reset(); full = true; assert(vcdSafeWriteFile("dst", "abcdef", 6) == -2 && opens == 0 && !removed);
    reset(); full = true; assert(vcdSafeCopyFile("src", "dst") == -2 && opens == 1 && !removed);
}
''')

support = read("src/sources/supportbase.cpp")
run("iso_only", r'''
#include <cassert>
#include <climits>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
enum { SCECdPS2CD, SCECdPS2DVD, O_RDONLY, GAME_FORMAT_USBLD, UL_GAME_NAME_MAX = 31, GAME_STARTUP_MAX = 12 };
struct base_game_info_t { char name[64], startup[32], extension[8]; int parts, media, format, sizeMB; };
struct USBExtreme_game_entry_t { char name[32], startup[13]; int parts, media; };
struct game_list_t { base_game_info_t gameinfo; game_list_t *next; };
static int sbReadListErrno, cdResult, dvdResult, probes, ulPresent;
static bool gEnableFolderNav;
static void sbSetBrowseSub(const char*) {}
static int scanForISO(const char*, int type, game_list_t **head, game_list_t**) {
    int count = type == SCECdPS2CD ? cdResult : dvdResult;
    for (int i = 0; i < count; ++i) {
        auto *node = static_cast<game_list_t*>(calloc(1, sizeof(game_list_t)));
        node->next = *head; *head = node;
    }
    return count;
}
static int sbDedupFolderList(game_list_t**) { return 0; }
static int openFile(const char *path, int) { assert(strstr(path, "ul.cfg")); ++probes; return ulPresent ? 1 : -1; }
static int getFileSize(int) { return sizeof(USBExtreme_game_entry_t); }
static int read(int, void *out, size_t size) { memset(out, 0, size); return size; }
static int close(int) { return 0; }
static bool failAllocation;
static void *allocate(size_t size) { return failAllocation ? nullptr : malloc(size); }
#define malloc allocate
''' + function(support, "int sbReadList(") + r'''
#undef malloc
int main() {
    auto *old = static_cast<base_game_info_t*>(calloc(1, sizeof(base_game_info_t)));
    base_game_info_t *list = old; int count = 1, ulSize = 123;
    cdResult = dvdResult = -1; ulPresent = 1;
    assert(sbReadList(&list, "udpfs:/", "", nullptr, &count) == -1);
    assert(list == old && count == 1 && probes == 0);
    cdResult = 1; dvdResult = 0;
    failAllocation = true;
    assert(sbReadList(&list, "udpfs:/", "", nullptr, &count) == -1);
    assert(list == old && count == 1 && probes == 0);
    assert(sbReadList(&list, "mass0:/", "", &ulSize, &count) == 1);
    assert(list == old && count == 1 && ulSize == 123);
    failAllocation = false; probes = 0;
    assert(sbReadList(&list, "udpfs:/", "", nullptr, &count) == 1 && count == 1 && probes == 0);
    cdResult = dvdResult = 0;
    assert(sbReadList(&list, "udpfs:/", "", nullptr, &count) == 0 && !list && count == 0 && probes == 0);
    // UL-capable callers retain enumeration, size tracking and last-good semantics.
    assert(sbReadList(&list, "mass0:/", "", &ulSize, &count) == 1 && probes == 1);
    assert(list[0].format == GAME_FORMAT_USBLD && ulSize == sizeof(USBExtreme_game_entry_t));
    old = list; cdResult = dvdResult = -1; ulPresent = 0;
    assert(sbReadList(&list, "mass0:/", "", &ulSize, &count) == 1 && list == old && count == 1);
    free(list);
}
''')

cue = read("src/sources/cuesupport.cpp")
run("partial_ps1", r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#define LOG(...) ((void)0)
struct base_game_info_t { int id; };
static int popsResult, emberResult, ps1ScanIncomplete;
static bool gAutosort;
static int scan(int count, base_game_info_t **out) {
    *out = count > 0 ? static_cast<base_game_info_t*>(calloc(count, sizeof(base_game_info_t))) : nullptr;
    return count;
}
static int vcdFillGameList(const char*, base_game_info_t **out) { return scan(popsResult, out); }
static int cueFillGameListUnsorted(const char*, base_game_info_t **out) { return scan(emberResult, out); }
static void *memalign(size_t, size_t size) { return malloc(size); }
static int ps1RowCmp(const void*, const void*) { return 0; }
static int gPs1Duplicates; // PS1 Duplicates off (Both): its filter is tested in test_ps1_duplicates.py
static int ps1DropTwins(base_game_info_t*, void*, size_t, int vcdCount, int cueCount, int) { return vcdCount + cueCount; }
''' + function(cue, "int ps1GameListNeedsRetry(") + "\n" + function(cue, "int ps1FillGameList(") + r'''
int main() {
    auto *old = static_cast<base_game_info_t*>(malloc(sizeof(base_game_info_t)));
    base_game_info_t *list = old;
    popsResult = -1; emberResult = 0;
    assert(ps1FillGameList("udpfs:/", &list) == -1 && list == old && ps1GameListNeedsRetry());
    popsResult = 0; emberResult = -1;
    assert(ps1FillGameList("udpfs:/", &list) == -1 && list == old && ps1GameListNeedsRetry());
    popsResult = 1; emberResult = -1;
    assert(ps1FillGameList("udpfs:/", &list) == 1 && list && ps1GameListNeedsRetry());
    emberResult = 1;
    assert(ps1FillGameList("udpfs:/", &list) == 2 && list && !ps1GameListNeedsRetry());
    popsResult = emberResult = 0;
    assert(ps1FillGameList("udpfs:/", &list) == 0 && !list && !ps1GameListNeedsRetry());
}
''')

udpfs = read("src/sources/udpfssupport.cpp")
run("pops_launch", r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <strings.h>
#include <string>
#include <vector>
#define POPS_FOLDER "POPS"
enum { _STR_ERR_FILE_INVALID, _STR_POPSTARTER_NOT_FOUND, _STR_PLEASE_WAIT, _STR_BDMA_ERR_SPACE, _STR_BDMA_ERR_IO,
       _STR_BDMA_ERR_OLD, _STR_BDMA_ERR_SRC, _STR_POPS_UDPFS_DRIVER_NOT_READY, O_RDONLY, VCD_PREFIX_MASS };
constexpr int VCD_BDMA_UDPFS = 6;
struct item_list_t { int mode; }; struct config_set_t {};
static const char *udpfsPrefix = "udpfs:/";
static int ready, gBdmaApplyOnLaunch, equips, configs, missing, error, clean, equipError, configError;
static std::vector<int> events;
static const char *_l(int id) { return reinterpret_cast<const char*>(long(id + 1)); }
namespace host {
struct Ui {
    void msgBox(const char *text, int, void*) { error = int(reinterpret_cast<long>(text)) - 1; }
    void missingFile(const char *text, const char*) { msgBox(text, 0, nullptr); }
    void renderTextScreen(const char*) { events.push_back(1); }
};
struct App {
    int pathToMode(const char*) { return 3; }
    void deinitEx(int, int mode, int) { assert(mode == 11); events.push_back(6); }
};
static Ui &ui() { static Ui value; return value; }
static App &app() { static App value; return value; }
}
// missing: 1 = no image at all, 2 = no POPSTARTER, 3 = only POPS/title.CUE (the TEST driver builds the VCD).
static std::vector<std::string> opened;
static int open(const char *path, int) {
    opened.push_back(path);
    if (!strcmp(path, "udpfs:/POPS/title.VCD")) return missing == 1 || missing == 3 ? -1 : 4;
    assert(!strcmp(path, "udpfs:/POPS/title.cue") || !strcmp(path, "udpfs:/POPS/title.CUE"));
    return missing == 3 && !strcmp(path, "udpfs:/POPS/title.CUE") ? 5 : -1;
}
static int close(int) { return 0; }
static int vcdResolvePopstarter(const char *root, char *elf, int size) {
    assert(!strcmp(root, udpfsPrefix)); if (missing == 2) return 0;
    snprintf(elf, size, "udpfs:/POPS/POPSTARTER.ELF"); return 1;
}
static void vcdDescribePopstarterLookup(const char*, char *elf, int size) { snprintf(elf, size, "missing"); }
static int vcdInstallPopstarterMc(const char *root) { assert(!strcmp(root, udpfsPrefix)); events.push_back(2); return 0; }
static int vcdInstallPopsUdpfsNet(const char *pops) { assert(!strcmp(pops, "udpfs:/POPS/")); ++configs; return configError; }
static int vcdEquipBdmaFromPops(const char *pops, int mode, char*, int) {
    assert(!strcmp(pops, "udpfs:/POPS/")); ++equips; if (!equipError) ready = mode; return equipError;
}
static int vcdBdmaLaunchReady(int mode) { assert(mode == VCD_BDMA_UDPFS); return ready == mode; }
static int vcdConfirmCleanLaunch(const char *root) { assert(!strcmp(root, udpfsPrefix)); events.push_back(4); return clean; }
static void vcdBuildSelector(const char *source, int type, const char*, char *out, int size) {
    assert(!strcmp(source, "udpfs:/") && type == VCD_PREFIX_MASS); snprintf(out, size, "XX.title");
}
static void vcdPrepareRetroGemBarcode(const char*) { events.push_back(5); }
static int sbLoaderDeinitException(const char*) { return 0; }
static void sysLaunchPopstarter(const char *elf, const char *selector) {
    assert(!strcmp(elf, "udpfs:/POPS/POPSTARTER.ELF") && !strcmp(selector, "XX.title")); events.push_back(7);
}
class UdpfsSource { public: void launchVcd(item_list_t*, const char*, config_set_t*); };
''' + function(udpfs, "void UdpfsSource::launchVcd(") + r'''
static UdpfsSource source; static item_list_t item{11};
static bool launch(int readyAt = 0, int apply = 1) {
    events.clear(); ready = readyAt; gBdmaApplyOnLaunch = apply; equips = configs = 0; error = -1; clean = 1;
    source.launchVcd(&item, "title", nullptr);
    bool launched = !events.empty() && events.back() == 7;
    if (!launched) for (int event : events) assert(event != 6);
    return launched;
}
int main() {
    source.launchVcd(&item, nullptr, nullptr); source.launchVcd(&item, "", nullptr);
    source.launchVcd(&item, "POPSTARTER", nullptr); assert(events.empty());
    // Network only: the config pair, POPSTARTER and the driver pair all come from the server.
    assert(launch() && equips == 1 && configs == 1 && (events == std::vector<int>{1, 2, 4, 5, 6, 7}));
    assert(launch(VCD_BDMA_UDPFS) && equips == 0 && configs == 1);    // an equipped pair is not recopied
    assert(launch(VCD_BDMA_UDPFS, 0) && equips == 0);                  // manual pair, auto-equip off
    assert(!launch(0, 0) && error == _STR_POPS_UDPFS_DRIVER_NOT_READY);
    equipError = -5; assert(!launch() && error == _STR_BDMA_ERR_OLD); equipError = -4;
    assert(!launch() && error == _STR_BDMA_ERR_SRC); equipError = 0;
    configError = -2; assert(!launch(VCD_BDMA_UDPFS) && error == _STR_BDMA_ERR_SPACE && !equips);
    configError = -3; assert(!launch(VCD_BDMA_UDPFS) && error == _STR_BDMA_ERR_IO); configError = 0;
    events.clear(); ready = VCD_BDMA_UDPFS; clean = 0; source.launchVcd(&item, "title", nullptr);
    assert(!events.empty() && events.back() == 4);                     // RA module refused: no teardown
    for (missing = 1; missing <= 2; ++missing) assert(!launch(VCD_BDMA_UDPFS));
    opened.clear(); missing = 1; assert(!launch(VCD_BDMA_UDPFS) && error == _STR_ERR_FILE_INVALID);
    assert((opened == std::vector<std::string>{"udpfs:/POPS/title.VCD", "udpfs:/POPS/title.cue", "udpfs:/POPS/title.CUE"}));
    missing = 3; assert(launch(VCD_BDMA_UDPFS));                        // cue-only game launches as XX.title
    missing = 0; opened.clear(); assert(launch(VCD_BDMA_UDPFS) && opened.size() == 1); // a .VCD is not second-guessed
}
''')

assert "GameSource::CAP_LAUNCH_VCD" in udpfs and "!ps1GameListNeedsRetry()" in udpfs
assert "sbIsSameSize(udpfsPrefix" not in udpfs and "ulSizePrev_" not in udpfs
assert "gUdpfsSupport.isoScanned_ = answeredBefore;" in udpfs
assert "gUdpfsSupport.invalidateIsoScan();" in function(udpfs, "void UdpfsSource::deleteGame(")
assert "gUdpfsSupport.invalidateIsoScan();" in function(udpfs, "void UdpfsSource::rename(")

provenance = json.loads(read("ship/POPS/UDPFS-NOTICES/PROVENANCE.json"))
assert provenance["archive_sha256"] == "225ea524f1eed59bffcc193e2fdf92868e6a2061a526a8c1d21675ac71c24d2f"
for filename, expected in provenance["files"].items():
    assert hashlib.sha256((ROOT / "ship/POPS" / filename).read_bytes()).hexdigest() == expected["sha256"], filename
print("UDPFS: ISO-only/cache preservation, partial PS1 recovery, launch gates and pinned payloads passed")
