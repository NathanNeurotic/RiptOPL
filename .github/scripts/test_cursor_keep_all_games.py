"""A list rebuild puts the cursor back on the row it was on (#565).

All Games lists one game once per device, all copies with the same startup, and a Favourites or
All Games row reaches its startup through its source's list by the id it was gathered with. The
rebuild runs right after that source rescanned, so the old id can name another game. Keyed on the
startup, the cursor jumped to another copy or another game on every rebuild.

The cursor is now kept by the row's source device, kind and plain name on those pages (not the
shown text, whose "(device)" tag comes and goes as devices appear), by the startup elsewhere, and
the first matching row wins instead of the last. The startup still picks between two games of one name
on one device.

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
for needle in ('keepMode = favGetItemSourceMode(id)', 'keepKind = favGetItemKind(id)', 'gsOf(mdl->support)->name(mdl->support, id)', 'gsOf(mdl->support)->startup(mdl->support, id)'):
    if needle not in keep:
        failures.append('the remembered Favourites/All Games row must be keyed on %s' % needle)
if 'keepCursor && i == keepRow' not in opl:
    failures.append('only the picked row may be selected (keepCursor && i == keepRow)')

harness = r'''
#include <stdio.h>
#include <string.h>
#define FAV_MODE 12
struct item_list_t { int mode; };
struct Src { const char *startup(item_list_t *, int i); const char *name(item_list_t *, int i); };
static const char *gStartups[8];
static const char *gTexts[8];
static int gModes[8];
static int gHidden[8];
static int gKinds[8];
const char *Src::startup(item_list_t *, int i) { return gStartups[i]; }
const char *Src::name(item_list_t *, int i) { return gTexts[i]; }
static Src gSrc;
static Src *gsOf(item_list_t *) { return &gSrc; }
static int gShowHidden = 0;
static int hiddenRow(item_list_t *, int i) { return gHidden[i]; }
static int favGetItemSourceMode(int i) { return gModes[i]; }
static int favGetItemKind(int i) { return gKinds[i]; }
struct Mdl { item_list_t *support; };

static int pick(int favMode, int count, const char *keepStartup, const char *keepText, int keepMode, int keepKind = 0)
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
    const char *tx[] = {"Game", "Game", "Game", "Game"};
    int md[] = {2, 11, 15, 15};
    int kd[] = {0, 0, 0, 1}; // row 3: a PS1 game of the same name on slot 2
    for (int k = 0; k < 4; k++) { gStartups[k] = st[k < 3 ? k : 0]; gTexts[k] = tx[k]; gModes[k] = md[k]; gKinds[k] = kd[k]; gHidden[k] = 0; }
    int r = pick(1, 4, "", "Game", 11);
    if (r != 1) { printf("FAIL: All Games: cursor on row %d, want the slot 1 copy (1)\n", r); bad = 1; }
    r = pick(1, 4, "", "Game", 15);
    if (r != 2) { printf("FAIL: All Games: cursor on row %d, want the slot 2 copy (2)\n", r); bad = 1; }
    r = pick(1, 4, "", "Game", 2);
    if (r != 0) { printf("FAIL: All Games: cursor on row %d, want the HDD copy (0)\n", r); bad = 1; }
    r = pick(1, 4, "", "Game", 15, 1);
    if (r != 3) { printf("FAIL: All Games: cursor on row %d, want the PS1 game of that name (3)\n", r); bad = 1; }
    // The kept game is gone: no row is picked (the list opens at its top, as before).
    r = pick(1, 4, "", "Other", 2);
    if (r != -1) { printf("FAIL: All Games: a missing game picked row %d\n", r); bad = 1; }
    // Two games of one name on one device (old-style SLUS_210.41.Game.iso and SLUS_210.44.Game.iso):
    // the kept startup picks between them; a stale startup matches neither, so the first wins.
    gStartups[4] = "SLUS_210.41"; gTexts[4] = "Twin"; gModes[4] = 2; gKinds[4] = 0; gHidden[4] = 0;
    gStartups[5] = "SLUS_210.44"; gTexts[5] = "Twin"; gModes[5] = 2; gKinds[5] = 0; gHidden[5] = 0;
    r = pick(1, 6, "SLUS_210.44", "Twin", 2);
    if (r != 5) { printf("FAIL: All Games: cursor on row %d, want the second game of that name (5)\n", r); bad = 1; }
    r = pick(1, 6, "SLUS_210.41", "Twin", 2);
    if (r != 4) { printf("FAIL: All Games: cursor on row %d, want the first game of that name (4)\n", r); bad = 1; }
    r = pick(1, 6, "SLUS_999.99", "Twin", 2);
    if (r != 4) { printf("FAIL: All Games: stale startup put the cursor on row %d, want the first copy (4)\n", r); bad = 1; }
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
print('PASS: a rebuild keeps the cursor on its own row: All Games by device, kind and name (startup between same-named games), the first match wins')
