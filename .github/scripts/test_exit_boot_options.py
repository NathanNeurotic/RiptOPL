"""Fixed-path BOOT ELF choices in the real Exit To dispatcher."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
system = (root / "src/platform/system.cpp").read_text(encoding="utf-8")
loader = (root / "elfldr/loader.c").read_text(encoding="utf-8")
elf_parent = (root / "src/platform/elfldr_noreset.cpp").read_text(encoding="utf-8")
settingsio = (root / "src/app/settingsio.cpp").read_text(encoding="utf-8")
opl = (root / "include/opl.h").read_text(encoding="utf-8")
base_lang = (root / "lng/tmpl/_base.yml").read_text(encoding="utf-8")
lang_labels = re.findall(r"^\s*label:\s*(\S+)\s*$", base_lang, re.M)
# Keep the BOOT labels immediately after the last pre-BOOT label. Future
# labels can extend the table without shifting the positions of existing ones.
boot_labels = lang_labels.index("NFS_EMBER_NO_SAVES") + 1
assert lang_labels[boot_labels:boot_labels + 2] == ["EXIT_MENU_BOOT2", "EXIT_MENU_BOOT"], "BOOT labels must preserve language-pack positions"


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
    void deinitTerminal(int exception, int selected) {
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
static char execArg0[32], execArg1[32];
static int execArgc;
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
    execArgc = argc;
    snprintf(execArg0, sizeof(execArg0), "%s", argv[0]);
    if (argc > 1) snprintf(execArg1, sizeof(execArg1), "%s", argv[1]);
    return 0;
}
#define main loaderMain
''' + function(loader, "int main(int argc, char *argv[])") + r'''
#undef main
static int runChild(int fail0, int fail1, int withFallback, int withReset, int targetReset) {
    char *args[6];
    int argc = 0;
    args[argc++] = (char *)"mc0:/BOOT/BOOT.ELF";
    args[argc++] = (char *)"mc0:/BOOT/BOOT.ELF";
    if (targetReset) args[argc++] = (char *)"-reset-iop";
    if (withReset) args[argc++] = (char *)"-reset-iop";
    if (withFallback) args[argc++] = (char *)"-fallback=mc1:/BOOT/BOOT.ELF";
    args[argc++] = (char *)(withFallback && withReset ? "-rlopt=2" : (withFallback || withReset ? "-rlopt=1" : "-rlopt=0"));
    failPrimary = fail0; failFallback = fail1;
    resetCalls = execCalls = primaryLoads = fallbackLoads = 0;
    execArg0[0] = execArg1[0] = 0;
    execArgc = 0;
    return loaderMain(argc, args);
}
int main(void) {
    assert(runChild(0, 0, 1, 1, 0) == 0 && execCalls == 1 && fallbackLoads == 0 && execArgc == 1);
    assert(!strcmp(execArg0, "mc0:/BOOT/BOOT.ELF") && resetCalls == 1);
    assert(runChild(1, 0, 1, 1, 0) == 0 && execCalls == 1 && fallbackLoads == 1 && execArgc == 1);
    assert(!strcmp(execArg0, "mc1:/BOOT/BOOT.ELF") && resetCalls == 1);
    assert(runChild(1, 1, 1, 1, 0) == -ENOENT && execCalls == 0);
    assert(runChild(1, 0, 0, 1, 0) == -ENOENT && execCalls == 0);
    assert(runChild(1, 0, 1, 0, 0) == 0 && execCalls == 1 && resetCalls == 0);
    assert(runChild(1, 0, 1, 1, 1) == 0 && execCalls == 1 && execArgc == 2);
    assert(!strcmp(execArg0, "mc1:/BOOT/BOOT.ELF") && !strcmp(execArg1, "-reset-iop"));
    assert(runChild(0, 0, 0, 0, 1) == 0 && execCalls == 1 && execArgc == 2);
    assert(!strcmp(execArg1, "-reset-iop") && resetCalls == 0); // target flag is never a parent flag
    char *targetFallback[] = {(char *)"mc0:/BOOT/BOOT.ELF", (char *)"mc0:/BOOT/BOOT.ELF",
                              (char *)"-fallback=mc1:/NOT-A-BOOT.ELF", (char *)"-rlopt=0"};
    failPrimary = 1; failFallback = 0; fallbackLoads = 0;
    assert(loaderMain(4, targetFallback) == -ENOENT && fallbackLoads == 0);
}
'''
with tempfile.TemporaryDirectory(prefix="exit-boot-child-") as tmp:
    src = Path(tmp) / "loader.c"
    exe = Path(tmp) / "loader"
    src.write_text(child_harness, encoding="utf-8")
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
parent_harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define SYS_LOAD_ELF_RESET_ROM0_MC 2
#define LAUNCHDIAG_REFUSE_ARGS 1
#define LAUNCHDIAG_REFUSE_CHILD_BUDGET 2
#define LAUNCHDIAG_REFUSE_CHILD_MAGIC 3
#define LOG(...) ((void)0)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uintptr_t u32;
typedef struct { u8 ident[16]; u16 type, machine; u32 version, entry, phoff, shoff, flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } elfldr_header_t;
typedef struct { u32 type, offset; void *vaddr; u32 paddr, filesz, memsz, flags, align; } elfldr_pheader_t;
#define ELFLDR_ELF_MAGIC 0x464c457f
#define ELFLDR_PT_LOAD 1
static unsigned char elfldr_irx[sizeof(elfldr_header_t)] __attribute__((aligned(8)));
static int gLaunchDiag, probeFailures, execCalls, wipeCalls;
static int capturedArgc;
static char captured[6][256];
static u32 _lw(u32 address) { return *(u32 *)address; }
static int probeTarget(const char *path, int diag) {
    (void)diag;
    return (!strncmp(path, "mc0:", 4) && (probeFailures & 1)) ||
           (!strncmp(path, "mc1:", 4) && (probeFailures & 2)) ? -1 : 0;
}
static void wipeBramMem(void) { wipeCalls++; }
static void sceSifExitRpc(void) {}
static void FlushCache(int which) { (void)which; }
static void launchDiagMark(int stage) { (void)stage; }
static void launchDiagRefuse(int code) { (void)code; }
static int ExecPS2(void *entry, void *gp, int argc, char **argv) {
    (void)entry; (void)gp;
    int i;
    execCalls++;
    capturedArgc = argc;
    assert(argc <= 6);
    for (i = 0; i < argc; i++) snprintf(captured[i], sizeof(captured[i]), "%s", argv[i]);
    return 0;
}
''' + function(elf_parent, "static int sysLoadELFCommon(") + function(elf_parent, "int sysLoadELFAppFallback(") + r'''
static void resetParent(void) {
    elfldr_header_t *eh = (elfldr_header_t *)elfldr_irx;
    memset(elfldr_irx, 0, sizeof(elfldr_irx));
    eh->ident[0] = 0x7f; eh->ident[1] = 'E'; eh->ident[2] = 'L'; eh->ident[3] = 'F';
    probeFailures = execCalls = wipeCalls = capturedArgc = 0;
}
int main(void) {
    char *args[] = {(char *)"mc0:/BOOT/BOOT.ELF"};
    resetParent();
    assert(sysLoadELFAppFallback("mc0:/BOOT/BOOT.ELF", "mc1:/BOOT/BOOT.ELF", "", 1, args, 1, 0) == 0);
    assert(execCalls == 1 && wipeCalls == 1 && capturedArgc == 5);
    assert(!strcmp(captured[0], "mc0:/BOOT/BOOT.ELF") && !strcmp(captured[1], "mc0:/BOOT/BOOT.ELF"));
    assert(!strcmp(captured[2], "-reset-iop") && !strcmp(captured[3], "-fallback=mc1:/BOOT/BOOT.ELF"));
    assert(!strcmp(captured[4], "-rlopt=2"));

    char *promoted[] = {(char *)"mc0:/BOOT/BOOT.ELF"};
    resetParent(); probeFailures = 1;
    assert(sysLoadELFAppFallback("mc0:/BOOT/BOOT.ELF", "mc1:/BOOT/BOOT.ELF", "", 1, promoted, 1, 0) == 0);
    assert(execCalls == 1 && capturedArgc == 4);
    assert(!strcmp(captured[0], "mc1:/BOOT/BOOT.ELF") && !strcmp(captured[1], "mc1:/BOOT/BOOT.ELF"));
    assert(!strcmp(captured[2], "-reset-iop") && !strcmp(captured[3], "-rlopt=1"));

    char *plainArgs[] = {(char *)"mc0:/BOOT/BOOT.ELF", (char *)"-fallback=mc1:/AN-APP-ARG"};
    resetParent();
    assert(sysLoadELFCommon("mc0:/BOOT/BOOT.ELF", "", 2, plainArgs, 0, 0, NULL) == 0);
    assert(capturedArgc == 4 && !strcmp(captured[2], "-fallback=mc1:/AN-APP-ARG"));
    assert(!strcmp(captured[3], "-rlopt=0"));

    char longFallback[280];
    memset(longFallback, 'x', sizeof(longFallback));
    memcpy(longFallback, "mc1:/", 5);
    longFallback[sizeof(longFallback) - 1] = 0;
    char *refused[] = {(char *)"mc0:/BOOT/BOOT.ELF"};
    resetParent();
    assert(sysLoadELFAppFallback("mc0:/BOOT/BOOT.ELF", longFallback, "", 1, refused, 1, 0) == -1);
    assert(!execCalls && !wipeCalls);
}
'''
with tempfile.TemporaryDirectory(prefix="exit-boot-parent-") as tmp:
    src = Path(tmp) / "parent.cpp"
    exe = Path(tmp) / "parent"
    src.write_text('#include "include/elfldr_args.h"\n' + parent_harness, encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(root), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: BOOT2/BOOT exits, parent/child fallback packaging, terminal failures, legacy exits and saved values")
