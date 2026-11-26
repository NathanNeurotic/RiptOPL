"""The exFAT free-cluster scan steps over fully used bitmap bytes and sectors and still finds what the plain scan finds."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
ff = (root / 'modules/bdm/bdmfs_fatfs/fatfs/ff.c').read_text()
start = ff.index('static DWORD find_bitmap (')
function = ff[start:ff.index('\n}\n', start) + 3]
assert 'fs->win[i] == 0xFF' in function, 'the scan must step over a fully used byte'
assert 'disk_read(fs->pdrv, (BYTE*)bulk' in function, 'the scan must read ahead and step over fully used sectors'
assert 'sync_window(fs)' in function, 'a changed window must reach the drive before the scan reads around it'
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
typedef int DRESULT;
#define FR_OK 0
#define RES_OK 0
#define RES_ERROR 1
#define FF_MAX_SS 512
#define SS(fs) 512U
typedef struct { BYTE pdrv; BYTE wflag; DWORD n_fatent; LBA_t bitbase; LBA_t winsect; BYTE win[512]; } FATFS;
#define MAXCL 400000
#define SMALLCL 12000
/* What is on the drive. The window may hold one sector that is newer than the drive (wflag). */
static BYTE bitmap[MAXCL / 8 + 1024];
static unsigned long requests;
static int fail_bulk;
static FRESULT sync_window(FATFS *fs) {
    if (fs->wflag) {
        memcpy(bitmap + (size_t)(fs->winsect - fs->bitbase) * 512, fs->win, 512);
        fs->wflag = 0;
    }
    return FR_OK;
}
static DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sect, UINT count) {
    (void)pdrv;
    requests++;
    if (count > 1 && fail_bulk) return RES_ERROR;
    if ((size_t)(sect - 7) * 512 + (size_t)count * 512 > sizeof(bitmap)) { printf("read past the bitmap\n"); exit(1); }
    memcpy(buff, bitmap + (size_t)(sect - 7) * 512, (size_t)count * 512);
    return RES_OK;
}
static FRESULT move_window(FATFS *fs, LBA_t sect) {
    if (sect != fs->winsect) {
        sync_window(fs);
        if (disk_read(fs->pdrv, fs->win, sect, 1) != RES_OK) return 1;
        fs->winsect = sect;
    }
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
        DWORD n = 1 + rnd() % (t % 97 == 0 ? MAXCL - 1 : t % 7 ? 300 : SMALLCL - 1); /* clusters on the volume */
        unsigned long fast, slow;
        DWORD dirty_at;
        if (t % 97 == 0 && t % 2) n = 4096 * (1 + rnd() % 97); /* the volume ends exactly on a bitmap sector */
        if (t % 97 == 1) n = 4096 * (1 + rnd() % 8) + (rnd() % 3) - 1;
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
        if (t & 1) { /* what lies past the last cluster is not the scan's to read: make it look free */
            for (c = n; c < n + 8 - n % 8 && c % 8; c++) bitmap[c / 8] &= (BYTE)~(1u << (c % 8));
            memset(bitmap + (n + 7) / 8, 0, sizeof(bitmap) - (n + 7) / 8);
        }
        fs.n_fatent = n + 2;
        fs.bitbase = 7;
        clst = (t % 11 == 0) ? 0xFFFFFFFF : (t % 11 == 1) ? 1 : 2 + rnd() % (n + 3);
        ncl = (t % 4) ? 1 : 1 + rnd() % 64;
        if (t % 13 == 0 && n > 8) { /* a byte or sector boundary right where the scan starts and ends */
            clst = 2 + ((rnd() % n) & ~7u);
            if (t % 26 == 0) clst = 2 + ((rnd() % n) & ~4095u);
        }
        fail_bulk = (t % 17 == 0); /* a read-ahead that fails leaves the plain scan to do the work */
        /* A cluster freed a moment ago is in the window only, as after remove_chain: the drive still says "in use". */
        dirty_at = rnd() % n;
        fs.winsect = fs.bitbase + dirty_at / 8 / 512;
        memcpy(fs.win, bitmap + (size_t)(dirty_at / 8 / 512) * 512, 512);
        fs.wflag = 0;
        if (t % 4 == 1) {
            bitmap[dirty_at / 8] |= (BYTE)(1u << (dirty_at % 8));
            fs.win[dirty_at / 8 % 512] &= (BYTE)~(1u << (dirty_at % 8));
            fs.wflag = 1;
        }
        requests = 0;
        a = find_bitmap(&fs, clst, ncl);
        fast = requests;
        sync_window(&fs);
        requests = 0;
        b = plain(&fs, clst, ncl);
        slow = requests;
        if (fast > 2 * slow + 4) {
            printf("too many reads: t=%d n=%u clst=%u ncl=%u fast=%lu plain=%lu\n", t, n, clst, ncl, fast, slow);
            return 1;
        }
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
    /* The case the read-ahead is for: a full volume with its only free cluster at the end. */
    {
        DWORD n = MAXCL - 1, a;
        memset(bitmap, 0xFF, sizeof(bitmap));
        bitmap[(n - 1) / 8] &= (BYTE)~(1u << ((n - 1) % 8));
        fs.n_fatent = n + 2; fs.bitbase = 7; fs.winsect = 0xFFFFFFFF; fs.wflag = 0; fail_bulk = 0;
        requests = 0;
        a = find_bitmap(&fs, 0xFFFFFFFF, 1);
        if (a != n - 1 + 2) { printf("full volume: got %u\n", a); return 1; }
        printf("full volume of %u clusters: %lu read requests, one per sector is %u\n", n, requests, n / 4096 + 1);
        if (requests > n / 4096 / 8) { printf("the read-ahead did not cut the requests\n"); return 1; }
    }
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'scan.c'
    src.write_text(HARNESS.replace('FIND_BITMAP', function))
    exe = Path(tmp) / 'scan'
    subprocess.run(['cc', '-O1', '-Wall', '-Werror', '-o', str(exe), str(src)], check=True)
    subprocess.run([str(exe)], check=True, timeout=300)
print('exFAT bitmap scan: byte and sector steps match the plain scan; every run found is real and inside the volume')
