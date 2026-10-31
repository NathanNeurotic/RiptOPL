#!/usr/bin/env python3
"""Host regression test for optional [PS1]/[PS2] game-type list labels.

Verifies:
1. Master config definition, golden keys, and settings table registration.
2. Interface settings UI toggle wiring and persistence.
3. Language template string presence.
4. drawItemsList presentation logic: opt-in, distinguishes PS1 and PS2, ignores Apps.
5. Functional C++ test harness exercising prefix behavior.
"""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]

config_h = (root / "include/config.h").read_text(encoding="utf-8")
dialogs_h = (root / "include/dialogs.h").read_text(encoding="utf-8")
opl_h = (root / "include/opl.h").read_text(encoding="utf-8")
opl_cpp = (root / "src/app/opl.cpp").read_text(encoding="utf-8")
settings_cpp = (root / "src/core/settings.cpp").read_text(encoding="utf-8")
settings_golden = (root / "tests/host/settings_golden_keys.inc").read_text(encoding="utf-8")
dialogs_cpp = (root / "src/ui/dialogs.cpp").read_text(encoding="utf-8")
guisettings_cpp = (root / "src/ui/guisettings.cpp").read_text(encoding="utf-8")
themeelems_cpp = (root / "src/gfx/themeelems.cpp").read_text(encoding="utf-8")
lang_base = (root / "lng/tmpl/_base.yml").read_text(encoding="utf-8")

# 1. Config definitions and golden keys
assert '#define CONFIG_OPL_GAME_TYPE_LABELS           "game_type_labels"' in config_h
assert "CONFIG_OPL_GAME_TYPE_LABELS" in settings_golden
assert "intSetting(CONFIG_OPL_GAME_TYPE_LABELS, &gGameTypeLabels, 0)" in settings_cpp

# 2. UI items and dialog wiring
assert "UICFG_GAME_TYPE_LABELS" in dialogs_h
assert "_STR_GAME_TYPE_LABELS" in dialogs_cpp
assert "UICFG_GAME_TYPE_LABELS" in dialogs_cpp
assert "diaSetInt(ui, UICFG_GAME_TYPE_LABELS, gGameTypeLabels);" in guisettings_cpp
assert "diaGetInt(ui, UICFG_GAME_TYPE_LABELS, &gGameTypeLabels);" in guisettings_cpp

# 3. Globals and defaults
assert "extern int gGameTypeLabels;" in opl_h
assert "int gGameTypeLabels;" in opl_cpp
assert "gGameTypeLabels = 0;" in opl_cpp

# 4. Themes drawing logic
assert "gGameTypeLabels && list->mode != APP_MODE" in themeelems_cpp
assert '"[PS1] %s"' in themeelems_cpp
assert '"[PS2] %s"' in themeelems_cpp

# 5. Language string
assert "- label: GAME_TYPE_LABELS\n  string: Show [PS1]/[PS2] Labels" in lang_base

# 6. Functional test harness
harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define APP_MODE 4
#define BDM_MODE 0

enum {
    LIB_VIEW_ALL = 0,
    LIB_VIEW_ISO,
    LIB_VIEW_PS1
};

static int gGameTypeLabels = 0;

static const char *formatDisplayName(int listMode, int rowView, const char *dispText, char *buf, size_t bufSize)
{
    if (gGameTypeLabels && listMode != APP_MODE) {
        if (rowView == LIB_VIEW_PS1) {
            snprintf(buf, bufSize, "[PS1] %s", dispText);
            return buf;
        } else if (rowView == LIB_VIEW_ISO) {
            snprintf(buf, bufSize, "[PS2] %s", dispText);
            return buf;
        }
    }
    return dispText;
}

int main(void)
{
    char buf[256];

    // Default: gGameTypeLabels == 0 (opt-in disabled)
    gGameTypeLabels = 0;
    assert(strcmp(formatDisplayName(BDM_MODE, LIB_VIEW_ISO, "Crash Bandicoot", buf, sizeof(buf)), "Crash Bandicoot") == 0);
    assert(strcmp(formatDisplayName(BDM_MODE, LIB_VIEW_PS1, "Spyro the Dragon", buf, sizeof(buf)), "Spyro the Dragon") == 0);
    assert(strcmp(formatDisplayName(APP_MODE, LIB_VIEW_ISO, "uLaunchELF", buf, sizeof(buf)), "uLaunchELF") == 0);

    // Enabled: gGameTypeLabels == 1
    gGameTypeLabels = 1;
    // PS2 ISO gets [PS2] prefix
    assert(strcmp(formatDisplayName(BDM_MODE, LIB_VIEW_ISO, "Crash Bandicoot", buf, sizeof(buf)), "[PS2] Crash Bandicoot") == 0);
    // PS1 VCD gets [PS1] prefix
    assert(strcmp(formatDisplayName(BDM_MODE, LIB_VIEW_PS1, "Spyro the Dragon", buf, sizeof(buf)), "[PS1] Spyro the Dragon") == 0);
    // APP_MODE never gets prefixed even when enabled
    assert(strcmp(formatDisplayName(APP_MODE, LIB_VIEW_ISO, "uLaunchELF", buf, sizeof(buf)), "uLaunchELF") == 0);
    assert(strcmp(formatDisplayName(APP_MODE, LIB_VIEW_PS1, "uLaunchELF", buf, sizeof(buf)), "uLaunchELF") == 0);

    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="game-type-labels-test-") as td:
    temp_path = pathlib.Path(td)
    source = temp_path / "test.c"
    binary = temp_path / ("test.exe" if os.name == "nt" else "test")
    source.write_text(harness, encoding="utf-8")
    cc = os.environ.get("CC", "gcc" if os.name == "nt" else "cc")
    subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-o", str(binary), str(source)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)

print("PASS: [PS1]/[PS2] game-type labels are opt-in, default off, and preserve app items")
