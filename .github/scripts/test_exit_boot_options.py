"""Fixed-path BOOT ELF choices in the real Exit To dispatcher."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
system = (root / "src/platform/system.cpp").read_text(encoding="utf-8")
loader = (root / "elfldr/loader.c").read_text(encoding="utf-8")
settingsio = (root / "src/app/settingsio.cpp").read_text(encoding="utf-8")
opl = (root / "include/opl.h").read_text(encoding="utf-8")
base_lang = (root / "lng/tmpl/_base.yml").read_text(encoding="utf-8")
lang_labels = re.findall(r"^\s*label:\s*(\S+)\s*$", base_lang, re.M)
assert lang_labels[-2:] == ["EXIT_MENU_BOOT2", "EXIT_MENU_BOOT"], "new labels must preserve positional language-pack compatibility"


def function(source, signature):
    start = source.index(signature)
    end = source.index("\n}\n", start)
    return source[start:end + 3]


enum_start = opl.index("enum { EXIT_MENU_CONSOLE")
exit_enum = opl[enum_start:opl.index("};", enum_start) + 2]
validation_start = settingsio.index("if (gExitMenu == EXIT_MENU_LEGACY_BROWSER_SKIPMC)")
validation_end = settingsio.index("if (gListCfgText", validation_start)
validation = settingsio[validation_start:validation_end]

harness = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#define NO_EXCEPTION 0
#define IO_MODE_SELECTED_ALL 0
#define O_RDONLY 0
''' + exit_enum + r'''
static int gExitMenu;
static int available, loadFailures, deinitCalls, osdCalls, exitCalls, loadCalls;
static char osdArg[32], loadPath[2][32], loadArgv0[32], loadFallback[32];
static int loadReset[2], loadCleanup[2], loadArgc[2];
namespace host {
struct App {
    void deinit(int exception, int selected) {
        assert(exception == NO_EXCEPTION && selected == IO_MODE_SELECTED_ALL);
        deinitCalls++;
    }
};
static App instance;
App &app() { return instance; }
}
static void ExecOSD(int argc, char **argv) {
    osdCalls++;
    assert(argc == 1);
    snprintf(osdArg, sizeof(osdArg), "%s", argv[0]);
}
static int open(const char *path, int flags, ...) {
    assert(flags == O_RDONLY);
    if (!strncmp(path, "mc0:", 4)) return (available & 1) ? 10 : -1;
    if (!strncmp(path, "mc1:", 4)) return (available & 2) ? 11 : -1;
    return -1;
}
static int close(int fd) { assert(fd == 10 || fd == 11); return 0; }
static int sysLoadELFAppFallback(const char *path, const char *fallback, const char *partition, int argc, char *argv[], int reset, int cleanup) {
    int slot = loadCalls++;
    assert(slot < 2 && partition != NULL && !strcmp(partition, ""));
    snprintf(loadPath[slot], sizeof(loadPath[slot]), "%s", path);
    snprintf(loadFallback, sizeof(loadFallback), "%s", fallback != NULL ? fallback : "");
    snprintf(loadArgv0, sizeof(loadArgv0), "%s", argv[0]);
    loadArgc[slot] = argc; loadReset[slot] = reset; loadCleanup[slot] = cleanup;
    return (!strncmp(path, "mc0:", 4) && (loadFailures & 1)) ||
           (!strncmp(path, "mc1:", 4) && (loadFailures & 2)) ? -1 : 0;
}
static int exitCardUpdateIgnoresBrowser(void) { return 0; }
#define exit mockedExit
static void mockedExit(int status) { assert(status == 0); exitCalls++; }
''' + function(system, "void sysExecExit(") + r'''
static void reset(void) {
    deinitCalls = osdCalls = exitCalls = loadCalls = 0;
    available = loadFailures = 0;
    memset(loadPath, 0, sizeof(loadPath));
    memset(loadArgv0, 0, sizeof(loadArgv0));
    memset(loadFallback, 0, sizeof(loadFallback));
    osdArg[0] = 0;
}
static void assertBoot(int choice, const char *name) {
    gExitMenu = choice; reset(); available = 3;
    sysExecExit(1);
    if (deinitCalls != 1 || loadCalls != 1 || osdCalls || exitCalls != 1)
        fprintf(stderr, "BOOT dispatch counts: deinit=%d load=%d osd=%d exit=%d\n", deinitCalls, loadCalls, osdCalls, exitCalls);
    assert(deinitCalls == 1 && loadCalls == 1 && !osdCalls && exitCalls == 1);
    assert(!strcmp(loadPath[0], (std::string("mc0:/BOOT/") + name).c_str()));
    assert(!strcmp(loadArgv0, loadPath[0]) && loadArgc[0] == 1 && loadReset[0] == 1 && loadCleanup[0] == 0);
    assert(!strcmp(loadFallback, (std::string("mc1:/BOOT/") + name).c_str()));
}
int main(void) {
    assertBoot(EXIT_MENU_BOOT2, "BOOT2.ELF");
    assertBoot(EXIT_MENU_BOOT, "BOOT.ELF");

    gExitMenu = EXIT_MENU_BOOT; reset(); available = 2; sysExecExit(1);
    assert(loadCalls == 1 && !strcmp(loadPath[0], "mc1:/BOOT/BOOT.ELF") && !loadFallback[0]);

    gExitMenu = EXIT_MENU_BOOT2; reset(); available = 3; sysExecExit(1);
    assert(deinitCalls == 1 && loadCalls == 1);
    assert(!strcmp(loadPath[0], "mc0:/BOOT/BOOT2.ELF") && !strcmp(loadFallback, "mc1:/BOOT/BOOT2.ELF"));

    gExitMenu = EXIT_MENU_BOOT2; reset(); available = 3; loadFailures = 1; sysExecExit(1);
    assert(deinitCalls == 1 && loadCalls == 1 && exitCalls == 1 && !osdCalls);

    gExitMenu = EXIT_MENU_BOOT; reset(); available = 0; sysExecExit(1);
    assert(!deinitCalls && !loadCalls && !osdCalls && !exitCalls);

    gExitMenu = EXIT_MENU_CONSOLE; reset(); sysExecExit(1);
    assert(deinitCalls == 1 && exitCalls == 1 && !osdCalls && !loadCalls);
    gExitMenu = EXIT_MENU_BROWSER; reset(); sysExecExit(1);
    assert(osdCalls == 1 && !strcmp(osdArg, "BootBrowser") && exitCalls == 1 && !loadCalls);
    gExitMenu = EXIT_MENU_OSDSYS_SKIPMC; reset(); sysExecExit(1);
    assert(osdCalls == 1 && !strcmp(osdArg, "SkipMc") && exitCalls == 1 && !loadCalls);
    gExitMenu = EXIT_MENU_BOOT; reset(); sysExecExit(0);
    assert(deinitCalls == 1 && exitCalls == 1 && !osdCalls && !loadCalls);

    gExitMenu = EXIT_MENU_LEGACY_BROWSER_SKIPMC;
    ''' + validation + r'''
    assert(gExitMenu == EXIT_MENU_BROWSER);
    gExitMenu = EXIT_MENU_BOOT2;
    ''' + validation + r'''
    assert(gExitMenu == EXIT_MENU_BOOT2);
    gExitMenu = EXIT_MENU_BOOT;
    ''' + validation + r'''
    assert(gExitMenu == EXIT_MENU_BOOT);
    gExitMenu = EXIT_MENU_BOOT + 1;
    ''' + validation + r'''
    assert(gExitMenu == EXIT_MENU_CONSOLE);
    gExitMenu = -1;
    ''' + validation + r'''
    assert(gExitMenu == EXIT_MENU_CONSOLE);
}
'''

with tempfile.TemporaryDirectory(prefix="exit-boot-options-") as tmp:
    src = Path(tmp) / "exit.cpp"
    exe = Path(tmp) / "exit"
    src.write_text(harness, encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
child_harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define ENOENT 2
#define EINVAL 22
#define LAUNCHDIAG_CHILD_PAINT(color) ((void)0)
typedef uintptr_t u32;
typedef struct { u32 epc, gp; } t_ExecData;
static int failPrimary, failFallback, resetCalls, execCalls, primaryLoads, fallbackLoads;
static char execArg0[32];
static int fails(const char *path) {
    return !strncmp(path, "mc0:", 4) ? failPrimary : failFallback;
}
static void wipeUserMem(void) {}
static void FlushCache(int which) { (void)which; }
static void SifInitRpc(int which) { (void)which; }
static void SifExitRpc(void) {}
static void SifLoadFileInit(void) {}
static void SifLoadFileExit(void) {}
static void resetIOP(int rom0) { assert(rom0 == 0); resetCalls++; }
static void cleanupHddForLaunch(void) {}
static int SifLoadElf(const char *path, t_ExecData *data) {
    if (!strncmp(path, "mc0:", 4)) primaryLoads++; else fallbackLoads++;
    if (fails(path)) return -1;
    data->epc = 0x100000; data->gp = 0;
    return 0;
}
static int SifLoadElfEncrypted(const char *path, t_ExecData *data) { return SifLoadElf(path, data); }
static int loadElfViaFileXio(const char *path, u32 *entry, u32 *gp) {
    if (!strncmp(path, "mc0:", 4)) primaryLoads++; else fallbackLoads++;
    if (fails(path)) return -1;
    *entry = 0x100000; *gp = 0;
    return 0;
}
static int ExecPS2(void *entry, void *gp, int argc, char **argv) {
    (void)entry; (void)gp;
    execCalls++;
    assert(argc == 1);
    snprintf(execArg0, sizeof(execArg0), "%s", argv[0]);
    return 0;
}
#define main loaderMain
''' + function(loader, "int main(int argc, char *argv[])") + r'''
#undef main
static int runChild(int fail0, int fail1, int withFallback, int withReset) {
    char *args[4];
    int argc = 0;
    args[argc++] = (char *)"mc0:/BOOT/BOOT.ELF";
    args[argc++] = (char *)"mc0:/BOOT/BOOT.ELF";
    if (withReset) args[argc++] = (char *)"-reset-iop";
    if (withFallback) args[argc++] = (char *)"-fallback=mc1:/BOOT/BOOT.ELF";
    failPrimary = fail0; failFallback = fail1;
    resetCalls = execCalls = primaryLoads = fallbackLoads = 0;
    execArg0[0] = 0;
    return loaderMain(argc, args);
}
int main(void) {
    assert(runChild(0, 0, 1, 1) == 0 && execCalls == 1 && fallbackLoads == 0);
    assert(!strcmp(execArg0, "mc0:/BOOT/BOOT.ELF") && resetCalls == 1);
    assert(runChild(1, 0, 1, 1) == 0 && execCalls == 1 && fallbackLoads == 1);
    assert(!strcmp(execArg0, "mc1:/BOOT/BOOT.ELF") && resetCalls == 1);
    assert(runChild(1, 1, 1, 1) == -ENOENT && execCalls == 0);
    assert(runChild(1, 0, 0, 1) == -ENOENT && execCalls == 0);
    assert(runChild(1, 0, 1, 0) == 0 && execCalls == 1 && resetCalls == 0);
}
'''
with tempfile.TemporaryDirectory(prefix="exit-boot-child-") as tmp:
    src = Path(tmp) / "loader.c"
    exe = Path(tmp) / "loader"
    src.write_text(child_harness, encoding="utf-8")
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: BOOT2/BOOT exit choices, child fallback, terminal failures, legacy exits and saved values")
