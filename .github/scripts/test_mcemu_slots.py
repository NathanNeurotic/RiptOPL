"""mcemu VMC slots survive a launch that returned to the menu.

Every native launch writes each VMC slot's settings over that slot's marker (0xC0DEFAC0 + slot) in the
embedded mcemu image, and nothing writes the marker back. A leg that searches for the marker on every
launch finds no slot after any launch that came back to the menu -- a declined prompt, a failed
preflight, Back on a preview -- leaves mcemu out, and that game runs without its VMC, silently. The SMB
leg also counted u32 words up to the image's BYTE size, reading up to four times past it.

Compiles the real nativeVmcSlotWord (include/nativevmc.h) against a fake image and checks: each slot is
found once while the image is pristine; the position survives the marker being written over; the
search stays inside the image; an unknown slot is refused. Then checks that all four legs use it with
their own image, size, cache and payload, that none still searches for the marker itself, and that the
HDD leg writes a slot with no card as inactive. Ported from the original's test of the same name.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
failures = []


def text(rel):
    return (root / rel).read_text(encoding='utf-8').replace('\r\n', '\n')


def function(source, signature):
    match = re.search(r'^' + re.escape(signature) + r'[^;{]*\)\s*\{', source, re.M)
    if match is None:
        sys.exit('missing ' + signature)
    return source[match.start():source.index('\n}', match.start()) + 2]


helper = function(text('include/nativevmc.h'), 'static inline int nativeVmcSlotWord(')

HARNESS = r'''
#include <stdio.h>
#include <string.h>
typedef unsigned int u32;

@HELPER@

int main(void)
{
    /* A 16-word image with the slot markers at words 5 and 9, plus room past its end. */
    u32 image[24];
    int cache[2] = {-2, -2}, a, b, c, d;
    memset(image, 0, sizeof(image));
    image[5] = 0xC0DEFAC0;
    image[9] = 0xC0DEFAC1;
    a = nativeVmcSlotWord(image, 16 * 4, 0, cache, 4);
    b = nativeVmcSlotWord(image, 16 * 4, 1, cache, 4);
    printf("first %d %d\n", a, b);

    /* A launch writes its VMC settings over both markers, then returns to the menu. */
    image[5] = 0x12345678;
    image[9] = 0;
    a = nativeVmcSlotWord(image, 16 * 4, 0, cache, 4);
    b = nativeVmcSlotWord(image, 16 * 4, 1, cache, 4);
    printf("after %d %d\n", a, b);

    /* A marker just past the image's end is not the image's: the search stops at size / 4 words. */
    int bounded[2] = {-2, -2};
    memset(image, 0, sizeof(image));
    image[16] = 0xC0DEFAC0;
    image[17] = 0xC0DEFAC1;
    c = nativeVmcSlotWord(image, 16 * 4, 0, bounded, 4);
    d = nativeVmcSlotWord(image, 16 * 4, 1, bounded, 4);
    printf("bounded %d %d\n", c, d);

    /* Only slots 0 and 1 exist. */
    int other[2] = {-2, -2};
    printf("slot2 %d slot-1 %d\n", nativeVmcSlotWord(image, 16 * 4, 2, other, 4), nativeVmcSlotWord(image, 16 * 4, -1, other, 4));
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / 'slots.cpp', Path(tmp) / 'slots'
    src.write_text(HARNESS.replace('@HELPER@', helper), encoding='utf-8')
    build = subprocess.run(['g++', '-Wall', '-Werror', '-o', str(exe), str(src)],
                           capture_output=True, text=True, check=False)
    if build.returncode != 0:
        sys.exit('harness did not compile:\n' + build.stderr)
    out = subprocess.run([str(exe)], capture_output=True, text=True, check=False).stdout.splitlines()

want = ['first 5 9', 'after 5 9', 'bounded -1 -1', 'slot2 -1 slot-1 -1']
if out != want:
    failures.append('nativeVmcSlotWord:\n  got  %r\n  want %r' % (out, want))

for leg, image, cache, info, fn in (
        ('src/sources/bdmlaunch.cpp', 'bdm_mcemu_irx', 'bdmMcemuSlots', 'bdm_vmc_infos', 'void BdmLauncher::launch('),
        ('src/sources/ethsupport.cpp', 'smb_mcemu_irx', 'ethMcemuSlots', 'smb_vmc_infos', 'void EthSource::launch('),
        ('src/sources/hddsupport.cpp', 'hdd_mcemu_irx', 'hddMcemuSlots', 'hdd_vmc_infos', 'void HddSource::launch('),
        ('src/sources/mmcesupport.cpp', 'mmce_mcemu_irx', 'mmceMcemuSlots', 'mmce_vmc_infos', 'void MmceSource::launch(')):
    body = text(leg)
    launch = function(body, fn)
    call = 'nativeVmcSlotWord(&%s, size_%s, vmc_id, %s, sizeof(%s_t))' % (image, image, cache, info)
    if launch.count(call) != 1:
        failures.append('%s: the VMC slot must be found through %s' % (leg, call))
    if ('static int %s[2] = {-2, -2};' if cache != 'mmceMcemuSlots' else 'int %s[2] = {-2, -2};') % cache not in body:
        failures.append('%s: %s must start unsearched (-2, -2)' % (leg, cache))
    if '0xC0DEFAC0' in launch:
        failures.append('%s: the launch must not search for the marker itself -- it is gone after the first launch' % leg)
    if not re.search(r'if \(slotWord >= 0\) \{\s*if \(%s\.active\)\s*size_mcemu_irx = size_%s;\s*'
                     r'memcpy\(&\(\(u32 \*\)&%s\)\[slotWord\]' % (info, image, image), launch):
        failures.append('%s: write the slot only where it was found, loading mcemu only for an active card' % leg)

udpfs_body = text('src/sources/udpfssupport.cpp')
udpfs = function(udpfs_body, 'static void udpfsLaunchOplCore(')
# UDPFS runs mmce_mcemu (udpfs_cdvdman exports the same oplutils ordinals), so it shares MMCE's image and
# slot cache: with its own cache, a launch on one device that came back to the menu would leave the other
# searching for a marker that is already gone.
if udpfs.count('nativeVmcSlotWord(&mmce_mcemu_irx, size_mmce_mcemu_irx, vmc_id, mmceMcemuSlots, sizeof(udpfs_vmc_infos_t))') != 1 \
        or 'McemuSlots[2] = {' in udpfs_body or '0xC0DEFAC0' in udpfs \
        or 'extern int mmceMcemuSlots[2];' not in text('include/mmcesupport.h') \
        or 'static_assert(sizeof(udpfs_vmc_infos_t) == sizeof(mmce_vmc_infos_t)' not in udpfs_body \
        or not re.search(r'if \(slotWord >= 0\) \{\s*if \(vmc_infos\[vmc_id\]\.active\)\s*size_mcemu_irx = size_mmce_mcemu_irx;\s*'
                         r'memcpy\(&\(\(u32 \*\)&mmce_mcemu_irx\)\[slotWord\]', udpfs):
    failures.append('udpfssupport.cpp: the UDPFS OPL core leg must use mmce_mcemu with the shared, cached, bounded slot')

hdd = function(text('src/sources/hddsupport.cpp'), 'void HddSource::launch(')
loop = hdd[hdd.index('for (vmc_id = 0; vmc_id < 2; vmc_id++) {'):]
if not (0 <= loop.find('} else\n                hdd_vmc_infos.active = 0;') < loop.find('int slotWord = nativeVmcSlotWord(&hdd_mcemu_irx')):
    failures.append('hddsupport.cpp: a slot with no VMC must be written inactive, after the per-card block')

if failures:
    print('\n'.join(failures))
    sys.exit(1)
print('mcemu VMC slots: found once, kept after the marker is overwritten, bounded; all five legs use them')
