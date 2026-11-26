"""Per-game RetroAchievements off ($RADisable) and the short first contact at launch.

1. Every launch leg that reads a watch list, prepares the Neutrino or POPStarter hand-over, or runs the
   Auto Loading support check asks sbGameRAOff(configSet) first.
2. The production sbGameRAOff runs against stubs: absent or 0 follows the global switch and touches
   nothing; 1 drops the loaded watch list, the hunt list and the Neutrino hand-over.
3. The per-game dialog loads, saves and removes the key, in RA builds only.
4. The short first contact covers only an ask with the default wait, and a reply ends it.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def read(rel):
    return (root / rel).read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


# 1. Call sites.
gated = {
    "src/sources/bdmlaunch.cpp": ["sbLoadWatchList(", "sbNeutrinoPrepareRA(", "raPopsPrepare("],
    "src/sources/hddsupport.cpp": ["sbLoadWatchList(", "sbNeutrinoPrepareRA("],
    "src/sources/ethsupport.cpp": ["sbLoadWatchList("],
    "src/sources/httpsupport.cpp": ["sbLoadWatchList("],
    "src/sources/udpfssupport.cpp": ["sbNeutrinoPrepareRA("],
    "src/sources/mmcesupport.cpp": ["sbLoadWatchList(", "sbNeutrinoPrepareRA("],
    "src/app/autolaunch.cpp": ["sbAutoLaunchCheckRA("],
}
for rel, calls in gated.items():
    text = read(rel)
    for call in calls:
        sites = [m.start() for m in re.finditer(re.escape(call), text)]
        assert sites, (rel, call)
        for at in sites:
            line_start = text.rfind("\n", 0, at)
            before = text[text.rfind("\n", 0, line_start - 1):at]
            assert "sbGameRAOff(configSet)" in before, (rel, call, text[line_start:at + 60])

# 2. sbGameRAOff itself.
supportbase = read("src/sources/supportbase.cpp")
body = function(supportbase, "int sbGameRAOff(")
key = re.search(r'#define CONFIG_ITEM_RA_DISABLE\s+"([^"]+)"', read("include/config.h")).group(1)
harness = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define CONFIG_ITEM_RA_DISABLE "@KEY@"
#define LOG(...) ((void)0)
typedef struct { const char *key; int value; } config_set_t;
static int configGetInt(config_set_t *s, const char *key, int *value)
{
    if (s == NULL || s->key == NULL || strcmp(s->key, key)) return 0;
    *value = s->value; return 1;
}
static int handCleared, listCleared;
static int sysNeutrinoHandRA(const char *startup, const char *a, const char *b, int c, int hdd, const unsigned char *d,
                             const unsigned char *e, const void *f, int g)
{ assert(!startup && !a && !b && !c && !hdd && !d && !e && !f && !g); handCleared++; return 0; }
static void ClearWatchList(void) { listCleared++; }
static int sbRaHuntCount, sbRaHuntBytes, sbRaHuntTotal, sbRaHuntUnlocked;
@BODY@
int main(void)
{
    config_set_t none = {NULL, 0}, zero = {"@KEY@", 0}, off = {"@KEY@", 1}, other = {"$DNAS", 1};
    sbRaHuntCount = sbRaHuntBytes = sbRaHuntTotal = sbRaHuntUnlocked = 7;
    assert(sbGameRAOff(NULL) == 0 && sbGameRAOff(&none) == 0 && sbGameRAOff(&zero) == 0 && sbGameRAOff(&other) == 0);
    assert(!handCleared && !listCleared && sbRaHuntTotal == 7); /* following the global switch changes nothing */
    assert(sbGameRAOff(&off) == 1);
    assert(handCleared == 1 && listCleared == 1);
    assert(!sbRaHuntCount && !sbRaHuntBytes && !sbRaHuntTotal && !sbRaHuntUnlocked);
    puts("sbGameRAOff: follows the global switch unless set, and drops everything RA when set");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "off.c"
    exe = Path(tmp) / "off"
    c.write_text(harness.replace("@KEY@", key).replace("@BODY@", body))
    result = subprocess.run(["gcc", "-Wall", "-Werror", str(c), "-o", str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    subprocess.run([str(exe)], check=True)

# 3. The dialog row and its key, RA builds only.
dialogs = read("src/ui/dialogs.cpp")
row = dialogs.index("COMPAT_RA_DISABLE")
assert dialogs.rfind("#ifdef RETROACHIEVEMENTS", 0, row) > dialogs.rfind("#endif", 0, row)
guigame = read("src/ui/guigame.cpp")
for needle in ("configGetInt(configSet, CONFIG_ITEM_RA_DISABLE",
               "configSetInt(configSet, CONFIG_ITEM_RA_DISABLE, 1)",
               "configRemoveKey(configSet, CONFIG_ITEM_RA_DISABLE)"):
    assert needle in guigame, needle
apply = function(guigame, "static int guiGameApplyConfig(")
assert "COMPAT_RA_DISABLE" in apply, "Save and Test share guiGameApplyConfig"
assert "configRemoveKey(configSet, CONFIG_ITEM_RA_DISABLE);" in function(guigame, "void guiGameRemoveSettings(")
base = read("lng/tmpl/_base.yml")
assert "label: RA_GAME_OFF" in base and "label: HINT_RA_GAME_OFF" in base

# 4. The short first contact.
ranet = read("src/sources/ranet.cpp")
ask = function(ranet, "int Ranet::ask(")
assert "if (quickTries_ > 0 && attempts == RA_TRY)" in ask, "chunk and long waits keep their own budget"
assert ask.count("quickTries_ = 0;") == 2, "a reply on either link ends the short wait"
header = read("include/ranet.h")
tries = int(re.search(r"RA_TRY = (\d+);", header).group(1))
quick = int(re.search(r"RA_TRY_LAUNCH = (\d+);", header).group(1))
assert 0 < quick < tries
check = function(supportbase, "void sbAutoLaunchCheckRA(")
assert check.index("raNetQuickFirstContact(1)") < check.index("sbHashGame(") < check.index("raNetQuickFirstContact(0)")
print("PASS: per-game RA off gates every launch leg; the launch-time first contact is shorter")
