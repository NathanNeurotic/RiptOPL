"""The exFAT free-cluster scan steps over fully used bitmap bytes and still finds what the plain scan finds."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
ff = (root / 'modules/bdm/bdmfs_fatfs/fatfs/ff.c').read_text()
start = ff.index('static DWORD find_bitmap (')
function = ff[start:ff.index('\n}\n', start) + 3]
assert 'fs->win[i] == 0xFF' in function, 'the scan must step over a fully used byte'
assert 'find_bitmap' in (root / 'modules/bdm/bdmfs_fatfs/ORIGIN.txt').read_text(), 'ORIGIN.txt must list the local change'

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t BYTE;
typedef unsigned int UINT;
typedef uint32_t DWORD;
typedef uint32_t LBA_t;
typedef int FRESULT;
#define FR_OK 0
#define SS(fs) 512U
typedef struct { DWORD n_fatent; LBA_t bitbase; BYTE win[512]; } FATFS;
#define MAXCL 12000
static BYTE bitmap[MAXCL / 8 + 1024];
static unsigned long windows;
static FRESULT move_window(FATFS *fs, LBA_t sect) {
    windows++;
    memcpy(fs->win, bitmap + (size_t)(sect - fs->bitbase) * 512, 512);
    return FR_OK;
}
FIND_BITMAP
/* The scan one bit at a time, with the same restart at the wrap. */
static DWORD plain(FATFS *fs, DWORD clst, DWORD ncl) {
    BYTE bm, bv; UINT i; DWORD val, scl, ctr;
    clst -= 2;
    if (clst >= fs->n_fatent - 2) clst = 0;
    scl = val = clst; ctr = 0;
    for (;;) {
        if (move_window(fs, fs->bitbase + val / 8 / SS(fs)) != FR_OK) return 0xFFFFFFFF;
        i = val / 8 % SS(fs); bm = 1 << (val % 8);
        do {
            do {
                bv = fs->win[i] & bm; bm <<= 1;
                if (++val >= fs->n_fatent - 2) { val = 0; bm = 0; i = SS(fs); }
                if (bv == 0) { if (++ctr == ncl) return scl + 2; } else { scl = val; ctr = 0; }
                if (val == 0) { scl = 0; ctr = 0; } /* the end of the volume and its start are not one run */
                if (val == clst) return 0;
            } while (bm != 0);
            bm = 1;
        } while (++i < SS(fs));
    }
}
static unsigned rnd_state = 12345;
static unsigned rnd(void) { rnd_state = rnd_state * 1103515245u + 12345u; return rnd_state >> 8; }
int main(void) {
    static FATFS fs;
    int t;
    for (t = 0; t < 60000; t++) {
        DWORD n = 1 + rnd() % (t % 7 ? 300 : MAXCL - 1); /* clusters on the volume */
        unsigned free_per_1024 = (t % 5 == 0) ? 0 : (t % 5 == 1) ? 2 : (t % 5 == 2) ? 40 : (t % 5 == 3) ? 500 : 1000;
        DWORD c, clst, ncl, a, b;
        memset(bitmap, 0xFF, sizeof(bitmap));
        for (c = 0; c < n; c++)
            if (rnd() % 1024 < free_per_1024)
                bitmap[c / 8] &= (BYTE)~(1u << (c % 8));
        if (t % 3 == 0 && n > 40) { /* a free block at a random place, as on a volume filled in order */
            DWORD at = rnd() % (n - 40), len = 1 + rnd() % 40;
            for (c = at; c < at + len; c++) bitmap[c / 8] &= (BYTE)~(1u << (c % 8));
        }
        fs.n_fatent = n + 2;
        fs.bitbase = 7;
        clst = (t % 11 == 0) ? 0xFFFFFFFF : (t % 11 == 1) ? 1 : 2 + rnd() % (n + 3);
        ncl = (t % 4) ? 1 : 1 + rnd() % 64;
        a = find_bitmap(&fs, clst, ncl);
        b = plain(&fs, clst, ncl);
        if (a != b) {
            printf("mismatch: n=%u clst=%u ncl=%u got=%u want=%u\n", n, clst, ncl, a, b);
            return 1;
        }
        /* Independent of either scan: a found run lies inside the volume and is free throughout,
           and "not found" means no such run exists. One limit of the scan as FatFs ships it is kept:
           it starts at clst and stops when it is back there, so it does not join the clusters
           before clst to those from clst on. That can only refuse; it never returns a bad run. */
        if (a) {
            if (a < 2 || a - 2 + ncl > n) { printf("run past the end: n=%u clst=%u ncl=%u got=%u\n", n, clst, ncl, a); return 1; }
            for (c = a - 2; c < a - 2 + ncl; c++)
                if (bitmap[c / 8] & (1u << (c % 8))) { printf("run not free: n=%u clst=%u ncl=%u got=%u\n", n, clst, ncl, a); return 1; }
        } else {
            DWORD run = 0, from = (clst - 2 >= n) ? 0 : clst - 2;
            for (c = 0; c < n; c++) {
                if (c == from) run = 0;
                run = (bitmap[c / 8] & (1u << (c % 8))) ? 0 : run + 1;
                if (run == ncl) { printf("missed a run: n=%u clst=%u ncl=%u at=%u\n", n, clst, ncl, c); return 1; }
            }
        }
    }
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'scan.c'
    src.write_text(HARNESS.replace('FIND_BITMAP', function))
    exe = Path(tmp) / 'scan'
    subprocess.run(['cc', '-O1', '-Wall', '-Werror', '-o', str(exe), str(src)], check=True)
    subprocess.run([str(exe)], check=True, timeout=120)
print('exFAT bitmap scan: fast path matches the plain scan; every run found is real and inside the volume')
