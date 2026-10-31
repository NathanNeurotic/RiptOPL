"""Verify NULL config handling across config routines, autolaunch, and menu handlers (Issue #961).

Ported from the original's test of the same name. configAlloc() returns NULL when the heap is
exhausted; every path that allocates or loads a config must survive that without dereferencing it
or opening a game menu full of stale values.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]


def text(path):
    return (ROOT / path).read_text(encoding="utf-8")


def body(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


config_cpp = text("src/core/config.cpp")
config_h = text("include/config.h")
supportbase = text("src/sources/supportbase.cpp")
autolaunch = text("src/app/autolaunch.cpp")
menuitems = text("src/ui/menuitems.cpp")
guigame = text("src/ui/guigame.cpp")
menusys = text("src/ui/menusys.cpp")
appsupport = text("src/sources/appsupport.cpp")
failures = []


def need(cond, msg):
    if not cond:
        failures.append(msg)


# Config entry points.
for sig in ("int configRead(config_set_t *configSet)", "int configReadBuffer(", "int configWrite(config_set_t *configSet)"):
    need("if (configSet == NULL" in body(config_cpp, sig)[:200], "config.cpp: %s lacks a NULL guard" % sig)
need("inline void configClear(config_set_t *s)\n{\n    if (s != NULL)" in config_h, "config.h: configClear lacks a NULL guard")

# The per-game config loader stops before configRead on a failed allocation.
sb = supportbase
sb_alloc = sb.index("config_set_t *config = configAlloc(0, NULL, path);")
need("if (config == NULL)\n        return NULL;" in sb[sb_alloc:sb.index("configRead(config)", sb_alloc)],
     "supportbase.cpp: config allocation is not checked before configRead")

# HDL and BDM autolaunch check the allocation before reading.
for sig in ("void autoLaunchHDDGame(", "void autoLaunchBDMGame("):
    fn = body(autolaunch, sig)
    need(0 <= fn.find("if (configSet == NULL)") < fn.find("configRead(configSet)"),
         "autolaunch.cpp %s: configSet is not checked before configRead" % sig)

# Triangle loads the config BEFORE opening the game menu, and stays on the list without one.
tri = menuitems[menuitems.index("if (!(flags & MODE_FLAG_NO_COMPAT)) {"):]
tri = tri[:tri.index("} else {")]
need("config_set_t *configSet = gameMenuLoadConfig(NULL);" in tri and "if (configSet == NULL)\n                    return;" in tri,
     "menuitems.cpp: Triangle does not check gameMenuLoadConfig")
need(0 <= tri.find("gameMenuLoadConfig(NULL)") < tri.find("menuInitGameMenu(support);"),
     "menuitems.cpp: the game menu opens before its config is known to exist")

# Game settings screens.
need("int guiGameSaveConfig(config_set_t *configSet, item_list_t *support)\n{\n    if (configSet == NULL)\n        return -1;" in guigame,
     "guigame.cpp: guiGameSaveConfig lacks a NULL guard")
need("void guiGameRemoveSettings(config_set_t *configSet)\n{\n    if (configSet == NULL)\n        return;" in guigame,
     "guigame.cpp: guiGameRemoveSettings lacks a NULL guard")
load = body(guigame, "void guiGameLoadConfig(int id, item_list_t *support, config_set_t *configSet)")
need(0 <= load.find("if (configSet == NULL)\n        return;") < load.find("configSource[0]"),
     "guigame.cpp: guiGameLoadConfig touches the UI before checking configSet")

# Deferred save and the Save Changes action.
need("WaitSema(menuSemaId);\n    if (itemConfig == NULL) {\n        actionStatus = 0;\n        menuSaveResult = 0;\n        SignalSema(menuSemaId);\n        return;\n    }" in menusys,
     "menusys.cpp: _menuSaveConfig does not handle a NULL itemConfig")
save = menusys[menusys.index("} else if (menuID == GAME_SAVE_CHANGES)"):menusys.index("} else if (menuID == GAME_TEST_CHANGES)")]
need("if (itemConfig == NULL) {\n                guiMsgBox(_l(_STR_ERROR_SAVING_SETTINGS), 0, NULL);\n                return;\n            }" in save,
     "menusys.cpp: Save Changes does not handle a NULL itemConfig")

# App configs, legacy and per-folder.
app = body(appsupport, "config_set_t *AppSource::config(")
need("config = host::app().legacyAppsInfo(appGetELFName(cur->val));\n        if (config == NULL)\n            return NULL;" in app,
     "appsupport.cpp: AppSource::config does not check the legacy apps config")
need("config = configAlloc(0, NULL, path);\n        if (config == NULL)\n            return NULL;" in app,
     "appsupport.cpp: AppSource::config does not check its config allocation")

if failures:
    print("\n".join("FAIL: " + f for f in failures))
    sys.exit(1)
print("PASS: config NULL checks protect against unallocated configSet dereferences")
