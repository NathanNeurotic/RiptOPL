#!/usr/bin/env python3
"""Settings has one menu-language picker, on its own Language page beside OSD Defaults (original #1043).

The picker used to appear on General, Interface and the legacy editors, and OSD Defaults sat on Game
Launching. The Language page owns a snapshot of the language names (discovery can rebuild the live list
while the dialog is open), frees it on every exit, and arms the save prompt after an OSD Defaults edit.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
gui = (ROOT / "src/ui/guisettings.cpp").read_text(encoding="utf-8")
dialogs = (ROOT / "src/ui/dialogs.cpp").read_text(encoding="utf-8")
failures = []


def function(source, signature):
    start = source.find(signature)
    if start < 0:
        failures.append(f"missing {signature}")
        return ""
    return source[start:source.index("\n}\n", start) + 3]


def array(name):
    match = re.search(r"^struct UIItem " + name + r"\[\] = \{.*?\};", dialogs, re.M | re.S)
    if not match:
        failures.append(f"missing dialog {name}")
        return ""
    return match.group()


language = array("diaLanguageConfig")
if len(re.findall(r"\{UI_ENUM, UICFG_LANG,", dialogs)) != 1:
    failures.append("the language picker must exist exactly once")
if "UICFG_LANG" not in language or "LAUNCH_OSD_DEFAULTS_BUTTON" not in language:
    failures.append("the Language page must hold the picker and the OSD Defaults action")
for old in ("diaConfig", "diaUIConfig", "diaLaunchConfig"):
    body = array(old)
    if "UICFG_LANG" in body or "LAUNCH_OSD_DEFAULTS_BUTTON" in body:
        failures.append(f"{old} must not carry the language picker or OSD Defaults")
if "UICFG_LANG" in gui.replace(function(gui, "static int guiSettingsShowLanguage("), ""):
    failures.append("only the Language page may read or write the language picker")
if "guiGameShowOSDLanguageConfig" in function(gui, "static int guiSettingsShowLaunch("):
    failures.append("Game Launching must not open OSD Defaults any more")

page = function(gui, "static int guiSettingsShowLanguage(")
for needle in ("guiCopyNameList((const char **)lngGetGuiList())", "if (langNamesSnap == NULL)",
               "guiGameShowOSDLanguageConfig(1);\n        gSettingsShell.markSavePending();",
               "if (langID != lngGetGuiValue())", "applyConfig(-1, langID, 1);",
               "guiFreeNameList(langNamesSnap);", "gSettingsShell.endDialog();",
               "return gSettingsShell.pageResult(result);"):
    if needle not in page:
        failures.append(f"guiSettingsShowLanguage missing: {needle}")
if page and page.index("guiCopyNameList(") > page.index("guiUnlock();"):
    failures.append("the name snapshot must be taken under the GUI lock")

enum = re.search(r"enum gui_settings_page \{(.*?)\};", gui, re.S)
order = [n.strip() for n in enum.group(1).split(",") if n.strip()] if enum else []
if order[:3] != ["SETTINGS_SOURCES = 0", "SETTINGS_GENERAL", "SETTINGS_LANGUAGE"] or order[-1] != "SETTINGS_PAGE_COUNT":
    failures.append("Language must be the third Settings page")
labels = function(gui, "int SettingsShell::showIndex(")
if labels.find("_STR_GENERAL_SYSTEM,\n        _STR_LANGUAGE,") < 0:  # string ids, looked up in each frame
    failures.append("the Settings index must list Language after General & System")
if not re.search(r"case SETTINGS_LANGUAGE:\s*result = guiSettingsShowLanguage\(\);", gui):
    failures.append("the Settings shell must dispatch the Language page")
if '"%d/8"' in gui:
    failures.append("the page indicator must follow SETTINGS_PAGE_COUNT")

if failures:
    print("\n".join(failures))
    sys.exit(1)
print("Language picker and OSD Defaults have one Settings entry point")
