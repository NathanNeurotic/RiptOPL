"""A list rebuild puts the cursor back on the row it was on (#565).

All Games lists one game once per device, all copies with the same startup, and a Favourites or
All Games row reaches its startup through its source's list by the id it was gathered with. The
rebuild runs right after that source rescanned, so the old id can name another game. Keyed on the
startup, the cursor jumped to another copy or another game on every rebuild.

The cursor is now kept by the row's source device and shown text on those pages (a title on two
devices shows its device name, so that pair is unique), by the startup elsewhere, and the first
matching row wins instead of the last.

This compiles the row pick from updateMenuFromGameList (src/app/opl.cpp) on the host against stubs.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
opl = (root / 'src/app/opl.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
failures = []

start = opl.find('        // The one row the cursor goes back to')
end = opl.find('        const int keepCursor', start)
if start < 0 or end < 0:
    print('FAIL: the cursor row pick is missing from updateMenuFromGameList')
    sys.exit(1)
pick = opl[start:end]

keep = opl[opl.find('    char keepStartup[128] = {0};'):opl.find('    cfgTitleForget(mdl->support);')]
for needle in ('favGetItemSourceMode(mdl->menuItem.current->item.id)', 'mdl->menuItem.current->item.text'):
    if needle not in keep:
        failures.append('the remembered Favourites/All Games row must be keyed on %s' % needle)
if 'keepCursor && i == keepRow' not in opl:
    failures.append('only the picked row may be selected (keepCursor && i == keepRow)')

harness = r'''
#include <stdio.h>
#include <string.h>
#define FAV_MODE 12
struct item_list_t { int mode; };
struct Src { const char *startup(item_list_t *, int i); };
static const char *gStartups[8];
static const char *gTexts[8];
static int gModes[8];
static int gHidden[8];
const char *Src::startup(item_list_t *, int i) { return gStartups[i]; }
static Src gSrc;
static Src *gsOf(item_list_t *) { return &gSrc; }
static int gShowHidden = 0;
static int hiddenRow(item_list_t *, int i) { return gHidden[i]; }
static int favGetItemSourceMode(int i) { return gModes[i]; }
static const char *favGetItemDisplay(int i) { return gTexts[i]; }
struct Mdl { item_list_t *support; };

static int pick(int favMode, int count, const char *keepStartup, const char *keepText, int keepMode)
{
    item_list_t list = {favMode ? FAV_MODE : 3};
    Mdl m = {&list};
    Mdl *mdl = &m;
    const int favPage = favMode;
    int i;
    (void)favPage;
@PICK@
    return keepRow;
}

int main(void)
{
    int bad = 0;
    // All Games: one game on the HDD (mode 2), MMCE slot 1 (11) and slot 2 (15); the user is on the
    // slot 1 copy. Every copy has the same startup.
    const char *st[] = {"SLUS_123.45", "SLUS_123.45", "SLUS_123.45"};
    const char *tx[] = {"Game (HDD Games)", "Game (MMCE Games)", "Game (MMCE Games)"};
    int md[] = {2, 11, 15};
    for (int k = 0; k < 3; k++) { gStartups[k] = st[k]; gTexts[k] = tx[k]; gModes[k] = md[k]; gHidden[k] = 0; }
    int r = pick(1, 3, "", "Game (MMCE Games)", 11);
    if (r != 1) { printf("FAIL: All Games: cursor on row %d, want the slot 1 copy (1)\n", r); bad = 1; }
    r = pick(1, 3, "", "Game (MMCE Games)", 15);
    if (r != 2) { printf("FAIL: All Games: cursor on row %d, want the slot 2 copy (2)\n", r); bad = 1; }
    r = pick(1, 3, "", "Game (HDD Games)", 2);
    if (r != 0) { printf("FAIL: All Games: cursor on row %d, want the HDD copy (0)\n", r); bad = 1; }
    // The kept game is gone: no row is picked (the list opens at its top, as before).
    r = pick(1, 3, "", "Other", 2);
    if (r != -1) { printf("FAIL: All Games: a missing game picked row %d\n", r); bad = 1; }
    // A device page: by startup, first visible match.
    gHidden[0] = 1;
    r = pick(0, 3, "SLUS_123.45", "", -1);
    if (r != 1) { printf("FAIL: device page: cursor on row %d, want the first shown match (1)\n", r); bad = 1; }
    gHidden[0] = 0;
    r = pick(0, 3, "SLUS_123.45", "", -1);
    if (r != 0) { printf("FAIL: device page: cursor on row %d, want the first match (0)\n", r); bad = 1; }
    return bad;
}
'''.replace('@PICK@', pick)

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'pick.cpp'
    exe = Path(tmp) / 'pick'
    src.write_text(harness)
    build = subprocess.run(['g++', '-std=c++17', '-Wall', '-Werror', '-o', str(exe), str(src)], capture_output=True, text=True)
    if build.returncode != 0:
        failures.append('harness did not build:\n' + build.stdout + build.stderr)
    else:
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        if run.returncode != 0:
            failures.append(run.stdout + run.stderr)

if failures:
    for f in failures:
        print('FAIL:', f)
    sys.exit(1)
print('PASS: a rebuild keeps the cursor on its own row: All Games by device and shown text, the first match wins')
