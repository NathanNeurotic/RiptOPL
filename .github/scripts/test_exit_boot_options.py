"""Fixed-path BOOT ELF choices in the real Exit To dispatcher."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
system = (root / "src/platform/system.cpp").read_text(encoding="utf-8")
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
static char osdArg[32], loadPath[2][32], loadArgv0[32];
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
static int sysLoadELFApp(const char *path, const char *partition, int argc, char *argv[], int reset, int cleanup) {
    int slot = loadCalls++;
    assert(slot < 2 && partition != NULL && !strcmp(partition, ""));
    snprintf(loadPath[slot], sizeof(loadPath[slot]), "%s", path);
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
    osdArg[0] = 0;
}
static void assertBoot(int choice, const char *name) {
    gExitMenu = choice; reset(); available = 3;
    sysExecExit(1);
    assert(deinitCalls == 1 && loadCalls == 1 && !osdCalls && !exitCalls);
    assert(!strcmp(loadPath[0], (std::string("mc0:/BOOT/") + name).c_str()));
    assert(!strcmp(loadArgv0, loadPath[0]) && loadArgc[0] == 1 && loadReset[0] == 1 && loadCleanup[0] == 0);
}
int main(void) {
    assertBoot(EXIT_MENU_BOOT2, "BOOT2.ELF");
    assertBoot(EXIT_MENU_BOOT, "BOOT.ELF");

    gExitMenu = EXIT_MENU_BOOT; reset(); available = 2; sysExecExit(1);
    assert(loadCalls == 1 && !strcmp(loadPath[0], "mc1:/BOOT/BOOT.ELF"));

    gExitMenu = EXIT_MENU_BOOT2; reset(); available = 3; loadFailures = 1; sysExecExit(1);
    assert(deinitCalls == 1 && loadCalls == 2);
    assert(!strcmp(loadPath[0], "mc0:/BOOT/BOOT2.ELF") && !strcmp(loadPath[1], "mc1:/BOOT/BOOT2.ELF"));

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
print("PASS: BOOT2/BOOT exit choices, mc0/mc1 fallback, legacy exits and saved-value validation")
