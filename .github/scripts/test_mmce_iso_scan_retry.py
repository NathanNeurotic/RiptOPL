"""An MMCE card whose ISO list never reads is rescanned a bounded number of times (#565).

ulSizePrev stays -2 until a scan reads CD or DVD. MmceSource::needsUpdate rescanned every -2 card on
every idle poll (~2 s) for as long as it stayed in, and each pass also rebuilt Apps, Favourites and
All Games: the All Games list flickered. sbIsSameSize(prefix, -2) never matches either, so it kept
the loop going on its own.

Now a -2 card gets MMCE_VCD_SCAN_RETRY_MAX rescans, re-armed by a successful scan (and by Refresh,
L3 and a card change, which reset the counter), and the ul.cfg check skips -2.

This compiles the retry block from needsUpdate (src/sources/mmcesupport.cpp) on the host against stubs.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
src = (root / 'src/sources/mmcesupport.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
hdr = (root / 'include/mmceint.h').read_text(encoding='utf-8')
failures = []

start = src.find('    // A card whose ISO list has not been read yet is rescanned')
end = src.find('    snprintf(path, sizeof(path), "%sCD", slot->prefix);', start)
if start < 0 or end < 0:
    print('FAIL: the bounded ISO rescan block is missing from MmceSource::needsUpdate')
    sys.exit(1)
block = src[start:end]

if 'slot->ulSizePrev != -2 && !sbIsSameSize(slot->prefix, slot->ulSizePrev)' not in src:
    failures.append('the ul.cfg size check must skip a never-read (-2) card')
if 'unsigned char isoScanRetries = 0;' not in hdr:
    failures.append('MmceSlotState must carry isoScanRetries')
for where, needle in (('resetState', '    ulSizePrev = -2;\n    isoScanRetries = 0;'),
                      ('card removal', '        slot->ps1ScanRetries = 0;\n        slot->isoScanRetries = 0;\n        slot->themesLoaded = 0;'),
                      ('Refresh/L3', '        slot->ps1ScanRetries = 0;\n        slot->isoScanRetries = 0;\n        slot->folderRetries = 0;')):
    if needle not in src:
        failures.append('isoScanRetries must be re-armed on %s' % where)

harness = r'''
#include <stdio.h>
constexpr int MMCE_VCD_SCAN_RETRY_MAX = 15;
#define MMCE_MODE_UPDATE_DELAY 0
struct Slot { int ulSizePrev; unsigned char isoScanRetries; };
struct List { int updateDelay; };
static int poll(Slot *slot, List *itemList)
{
    int result = 0;
@BLOCK@
    return result;
}
int main(void)
{
    int bad = 0;
    Slot s = {-2, 0};
    List l = {-1};
    int rescans = 0;
    for (int i = 0; i < 100; i++)
        rescans += poll(&s, &l);
    if (rescans != MMCE_VCD_SCAN_RETRY_MAX) { printf("FAIL: an unreadable card was rescanned %d times in 100 polls, want %d\n", rescans, MMCE_VCD_SCAN_RETRY_MAX); bad = 1; }
    // A successful scan re-arms the budget; a later delete/rename (-2 again) gets its rescan.
    s.ulSizePrev = -1;
    if (poll(&s, &l) != 0) { printf("FAIL: a read card was rescanned by the retry block\n"); bad = 1; }
    s.ulSizePrev = -2;
    if (poll(&s, &l) != 1) { printf("FAIL: a card marked for a fresh scan after a read was not rescanned\n"); bad = 1; }
    return bad;
}
'''.replace('@BLOCK@', block)

with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'retry.cpp'
    exe = Path(tmp) / 'retry'
    c.write_text(harness)
    b = subprocess.run(['g++', '-std=c++17', '-Wall', '-Werror', '-o', str(exe), str(c)], capture_output=True, text=True)
    if b.returncode != 0:
        failures.append('harness did not build:\n' + b.stdout + b.stderr)
    else:
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        if r.returncode != 0:
            failures.append(r.stdout + r.stderr)

if failures:
    for f in failures:
        print('FAIL:', f)
    sys.exit(1)
print('PASS: an MMCE card whose ISO list never reads is rescanned %d times, not forever' % 15)
