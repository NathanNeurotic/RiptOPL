#!/usr/bin/env python3
"""cdvdman_searchfile_init() never leaves the in-game read bound at 0 when it fails.

For a DVD9 image the function clears mediaLsnCount (0 = no end-of-media check) so it can read the
layer 1 volume descriptor. If that read fails, the function returns 0, but direct sceCdRead calls
still run afterwards, so the bound must be put back: the loader's count when it gave one, otherwise
the layer 0 PVD count. A failed layer 0 read also takes the loader's count when there is one. Compiles the real function from modules/iopcore/cdvdman/searchfile.c against
a fake sceCdRead and checks every outcome: layer 0 failure, layer 1 refused, layer 1 read error,
success, and the loader-provided count.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SEARCHFILE = ROOT / 'modules/iopcore/cdvdman/searchfile.c'
CC = shutil.which('cc') or shutil.which('gcc')


def function(text, name):
    match = re.search(r'\n[^\n;{}]*\b' + re.escape(name) + r'\([^;{]*\)[^;{\n]*\n\{', text)
    if not match:
        sys.exit('%s: %s not found' % (SEARCHFILE, name))
    depth, i = 0, match.end() - 1
    while True:
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        i += 1
        if depth == 0:
            return text[match.start():i]


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define DPRINTF(...)
#define SCECdErNO 0
#define SCECdErREAD 0x30
#define IOPCORE_COMPAT_EMU_DVDDL 1
struct dirTocEntry { u8 pad[2]; u32 fileLBA; u32 pad2; u32 fileSize; } __attribute__((packed));
typedef struct { u32 rootDirtocLBA; u32 rootDirtocLength; } layer_info_t;
static struct { struct { int flags; u32 mediaLsnCount; } common; } cdvdman_settings;
static layer_info_t layer_info[2];
static u8 cdvdman_buf[2048];
u32 mediaLsnCount;
static int fail_lsn = -1, refuse_lsn = -1, err;
static int sceCdRead(u32 lsn, u32 n, void *buf, void *mode)
{
    (void)n; (void)mode;
    if ((int)lsn == refuse_lsn)
        return 0;
    err = ((int)lsn == fail_lsn) ? SCECdErREAD : SCECdErNO;
    memset(buf, 0, 2048);
    *(u32 *)((u8 *)buf + 0x50) = (lsn == 16) ? 1000 : 900; /* PVD volume space size */
    return 1;
}
static int sceCdSync(int mode) { (void)mode; return 0; }
static int sceCdGetError(void) { return err; }
static int sceCdReadDvdDualInfo(int *on_dual, unsigned int *layer1_start) { *on_dual = 1; *layer1_start = 2000; return 1; }

FUNCTION

static int check(const char *name, int fail, int refuse, u32 loader, int want_ret, u32 want_count)
{
    fail_lsn = fail; refuse_lsn = refuse; err = 0;
    cdvdman_settings.common.flags = 0;
    cdvdman_settings.common.mediaLsnCount = loader;
    mediaLsnCount = 12345;
    int ret = cdvdman_searchfile_init();
    if (ret != want_ret || mediaLsnCount != want_count) {
        printf("FAIL %s: returned %d, mediaLsnCount %u (want %d, %u)\n", name, ret, mediaLsnCount, want_ret, want_count);
        return 1;
    }
    return 0;
}

int main(void)
{
    int bad = 0;
    bad += check("layer 0 read error keeps the old bound", 16, -1, 0, 0, 12345);
    bad += check("layer 0 read refused keeps the old bound", -1, 16, 0, 0, 12345);
    bad += check("layer 0 failure takes the loader count", 16, -1, 4000, 0, 4000);
    bad += check("layer 1 read refused restores the layer 0 bound", -1, 2016, 0, 0, 1000);
    bad += check("layer 1 read error restores the layer 0 bound", 2016, -1, 0, 0, 1000);
    bad += check("layer 1 failure prefers the loader count", 2016, -1, 4000, 0, 4000);
    bad += check("DVD9 success adds both layers", -1, -1, 0, 1, 1000 + 900 - 16);
    bad += check("success prefers the loader count", -1, -1, 4000, 1, 4000);
    return bad != 0;
}
'''


def main():
    if CC is None:
        sys.exit('no C compiler')
    text = SEARCHFILE.read_text(encoding='utf-8').replace('\r\n', '\n')
    source = HARNESS.replace('FUNCTION', function(text, 'cdvdman_searchfile_init'))
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 't.c'
        exe = Path(tmp) / 't'
        c.write_text(source)
        subprocess.run([CC, '-Wall', '-Werror', '-Wno-unused-function', '-O1', str(c), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.exit('searchfile init bounds: FAIL')
    print('searchfile init bounds: PASS')


if __name__ == '__main__':
    main()
