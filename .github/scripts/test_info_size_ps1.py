"""Info-screen #Size for PS1 rows and for rows reached by moving on the info page (original #1057).

Compiles sbResolvePs1Size and cueGameSizeMB from the production sources against a temporary PS1
root, then checks the wiring: every info-page move re-requests the size, HDD scans carry the sizes
they can only read while a partition is mounted, and the stat permission belongs to one thread.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def read(path):
    return (root / path).read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    depth = 0
    for index in range(source.index("{", start), len(source)):
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        if depth == 0:
            return source[start:index + 1]
    raise AssertionError(signature)


cue = read("src/sources/cuesupport.cpp")
base = read("src/sources/supportbase.cpp")
program = r'''
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#define lseek64 lseek
typedef long long s64;
typedef unsigned int u32;
typedef unsigned long long u64;
#define POPS_FOLDER "POPS"
#define EMBER_FOLDER_DEFAULT "EMBER"
#define EMBER_GAMES_FOLDER "games"
#define CUE_ROW_EXTENSION ".CUE"
#define CONFIG_ITEM_SIZE "#Size"
typedef struct { char name[161], startup[13], extension[8]; u32 sizeMB; } base_game_info_t;
typedef struct { int size; } config_set_t;
static int gate = 1;
static struct { int configStatSizeEnabled() { return gate; } } gSupportBase;
static void configSetInt(config_set_t *c, const char *, int v) { c->size = v; }
static int cueIsCueEntry(const base_game_info_t *g) { return g && !strcasecmp(g->extension, CUE_ROW_EXTENSION); }
''' + "\n".join(function(cue, s) for s in ("static char cueSep(", "static const char *cueEmberFolder(",
                                            "static void cueBuildGamesDir(", "unsigned int cueGameSizeMB(")) \
    + "\n" + function(base, "static int sbFileSizeMB(") + "\n" + function(base, "void sbResolvePs1Size(") + r'''
int main(int argc, char **argv) {
    char rootDir[512];
    snprintf(rootDir, sizeof(rootDir), "%s/", argv[1]);
    config_set_t c = {-1};
    base_game_info_t vcd = {"Crash", "Crash", ".VCD", 0}, ember = {"Spyro", "Spyro", ".CUE", 0}, iso = {"Gt", "Gt", ".iso", 0};
    gate = 0; sbResolvePs1Size(&vcd, rootDir, &c); assert(c.size == -1 && vcd.sizeMB == 0); /* scroll path: no IO */
    gate = 1; sbResolvePs1Size(&vcd, rootDir, &c); assert(c.size == 3 && vcd.sizeMB == 3);
    /* Ember: every top-level .bin track counts; the .cue, other files and sub-folders do not. */
    c.size = -1; sbResolvePs1Size(&ember, rootDir, &c); assert(c.size == 5 && ember.sizeMB == 5);
    c.size = -1; sbResolvePs1Size(&iso, rootDir, &c); assert(c.size == -1);   /* not a PS1 row */
    c.size = -1; sbResolvePs1Size(&vcd, rootDir, &c); assert(c.size == -1);   /* cached: no restat */
    base_game_info_t missing = {"Gone", "Gone", ".CUE", 0};
    c.size = -1; sbResolvePs1Size(&missing, rootDir, &c); assert(c.size == 0);
    assert(argc == 2 && argv[1][0]);
    puts("PASS: PS1 #Size resolves the VCD and the Ember tracks under the PS1 root, only on the info path");
}
'''
with tempfile.TemporaryDirectory(prefix="info-size-") as tmp:
    tmp = Path(tmp)
    lib = tmp / "lib"
    (lib / "POPS").mkdir(parents=True)
    (lib / "POPS" / "Crash.VCD").write_bytes(b"\0" * (3 << 20))
    game = lib / "EMBER" / "games" / "Spyro"
    (game / "extra").mkdir(parents=True)
    (game / "Spyro (Track 1).bin").write_bytes(b"\0" * (4 << 20))
    (game / "Spyro (Track 2).BIN").write_bytes(b"\0" * (1 << 20))
    (game / "Spyro.cue").write_bytes(b"\0" * (2 << 20))
    (game / "extra" / "x.bin").write_bytes(b"\0" * (2 << 20))
    src, exe = tmp / "t.cpp", tmp / "t"
    src.write_text(program, encoding="utf-8")
    subprocess.run(["c++", "-std=gnu++17", "-Wall", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(lib)], check=True)

info = function(read("src/ui/menusys.cpp"), "void menuHandleInputInfo()")
for move in ("menuPrevV", "menuNextV", "menuPrevPage", "menuNextPage", "menuFirstPage", "menuLastPage"):
    assert re.search(move + r"\(\);\s*menuRequestInfoSize\(\);", info), move
request = function(read("src/ui/menusys.cpp"), "void menuRequestInfoSize(")
assert "isFolder" in request and "LIB_VIEW_PS1" in request, "row eligibility lives in menuRequestInfoSize"
assert "LIB_VIEW_PS1" not in function(read("src/ui/menuitems.cpp"), "void itemExecSquare("), "PS1 rows are sized now"

for path in ("src/sources/bdmsupport.cpp", "src/sources/mmcesupport.cpp", "src/sources/ethsupport.cpp",
             "src/sources/udpfssupport.cpp"):
    assert re.search(r"Source::config\([^)]*\)\s*\{.*?ps1Size\(itemList, game, config\);", read(path), re.S), path
assert "gsOf(o)->ps1Size(o, &game, config)" in read("src/sources/favsupport.cpp")

hdd = read("src/sources/hddsupport.cpp")
assert "g->sizeMB = vcds[i].sizeMB;" in hdd and "g->sizeMB = cueSizes != NULL ? cueSizes[i] : 0;" in hdd
scan = hdd[hdd.index('int n = cueScanDir("pfs1:/", &cues);'):]
assert scan.index("cueGameSizeMB") < scan.index('fileXioUmount("pfs1:")'), "Ember sizes are read while mounted"
assert "vcdScanOpenDir(dirPath, outList, 1)" in function(read("src/sources/vcdsupport.cpp"), "int vcdScanDirRoot(")
resolve = function(hdd, "static u32 hddVcdResolveSize(")
assert "sbConfigStatSizeEnabled()" in resolve and "hddIsPopsPartitionGame(part)" in resolve
assert resolve.count("host::ui().lock()") == 2, "snapshot and write back under the UI lock"

gate = function(base, "int SupportBase::configStatSizeEnabled(")
assert "GetThreadId()" in gate and "GetThreadId()" in function(base, "void SupportBase::setConfigStatSize(")
print("PASS: info-page moves re-request #Size; HDD scans keep mounted-only sizes; the gate is per thread")
