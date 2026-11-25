"""A write the disk driver refuses must not stop the FAT/exFAT volume from being read.

atad refuses every BDM write below LBA 0x40000 on a disk whose LBA 0 carries the APA magic
(docs/APA-SAFETY.md). A disk that was once PS2 formatted and later given an MBR and an exFAT volume by a
tool that keeps the first bytes of sector 0 still carries that magic, and its exFAT volume sits inside
that range. FatFs kept the refused sector as "changed, not written yet" and tried to write it again
before every read of another sector, so after the first folder RiptOPL tried to create, CD, DVD and
ul.cfg all failed to open ("Could not open the CD or DVD folder ... (error 1)") until the next boot.

This builds the real fatfs/ff.c and fatfs/diskio.c on the host over a block device with the same fence
as atad and checks:

- with the fence active and one standard folder missing, mkdir fails, and CD, DVD and the root still open;
- nothing is written to the image, and the volume takes no further write until it is mounted again;
- a plain write error (-EIO, a real failing drive) keeps the window, as upstream FatFs does;
- without the APA magic the same calls create the folder.

Run with --baseline <dir> to point it at another ff.c and diskio.c and watch the first case fail.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
fatfs = root / 'modules/bdm/bdmfs_fatfs/fatfs'
ff_path = fatfs / 'ff.c'
diskio_path = fatfs / 'diskio.c'
if len(sys.argv) == 3 and sys.argv[1] == '--baseline':
    ff_path = Path(sys.argv[2]) / 'ff.c'
    diskio_path = Path(sys.argv[2]) / 'diskio.c'

diskio = (fatfs / 'diskio.c').read_text()
assert '-EROFS' in diskio and 'RES_WRPRT' in diskio, 'diskio.c must report a refused write as RES_WRPRT'
atad = (root / 'modules/hdd/atad/src/ps2atad.c').read_text()
assert '-EROFS' in atad, 'atad must mark a write refused by the APA fence with -EROFS'
assert 'sync_window' in (root / 'modules/bdm/bdmfs_fatfs/ORIGIN.txt').read_text(), 'ORIGIN.txt must list the local change'

DISK = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#define PART 2048
#define DISK_SECT (1024*1024) /* 512 MiB */
FILE *img; int guard_on; int refused; int writes;
static int apa(void){ unsigned char b[512]; fseek(img,0,SEEK_SET); fread(b,1,512,img); return b[4]=='A'&&b[5]=='P'&&b[6]=='A'&&b[7]==0; }
DSTATUS disk_status(BYTE p){return 0;} DSTATUS disk_initialize(BYTE p){return 0;}
DRESULT disk_read(BYTE p, BYTE*b, LBA_t s, UINT c){ fseek(img,(long)(s+PART)*512,SEEK_SET); return fread(b,512,c,img)==c?RES_OK:RES_ERROR; }
DRESULT disk_write(BYTE p,const BYTE*b,LBA_t s,UINT c){
  /* same test as ata_bd_write_hits_apa_reserved: absolute LBA below 0x40000 on a disk whose LBA 0 has the APA magic */
  if (guard_on && c && (s+PART) < 1024*256 && apa()) { refused++; return RES_ERROR; }
  writes++; fseek(img,(long)(s+PART)*512,SEEK_SET); return fwrite(b,512,c,img)==c?RES_OK:RES_ERROR; }
DRESULT disk_ioctl(BYTE p,BYTE cmd,void*buf){ switch(cmd){case CTRL_SYNC:fflush(img);break;case GET_SECTOR_COUNT:*(LBA_t*)buf=DISK_SECT-PART;break;case GET_SECTOR_SIZE:*(WORD*)buf=512;break;case GET_BLOCK_SIZE:*(DWORD*)buf=1;break;default:return RES_PARERR;} return RES_OK; }
DWORD get_fattime(void){return ((2026-1980)<<25)|(11<<21)|(25<<16);}
void* ff_memalloc(UINT n){return malloc(n);} void ff_memfree(void*p){free(p);}
int ff_mutex_create(int v){return 1;} void ff_mutex_delete(int v){} int ff_mutex_take(int v){return 1;} void ff_mutex_give(int v){}
'''

MKIMG = r'''
#include <stdio.h>
#include "ff.h"
extern FILE *img;
int main(int c, char **v)
{
    static BYTE work[65536];
    FATFS fs;
    MKFS_PARM o = {FM_EXFAT | FM_SFD, 0, 0, 0, 0};
    img = fopen(v[1], "w+b");
    fseek(img, (long)1024 * 1024 * 512 - 1, SEEK_SET);
    fputc(0, img);
    if (f_mkfs("0:", &o, work, sizeof work) != FR_OK || f_mount(&fs, "0:", 1) != FR_OK || fs.fs_type != FS_EXFAT)
        return 1;
    for (int i = 2; i < c; i++)
        if (f_mkdir(v[i]) != FR_OK)
            return 1;
    f_mount(0, "0:", 0);
    fclose(img);
    return 0;
}
'''

BD = r'''
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <bdm.h>
#include <cdvdman.h>
#include "ff.h"
/* The partition block device bdmfs_fatfs mounts: an exFAT volume at LBA 2048 of a disk with atad's fence. */
#define PART 2048
FILE *img;
int plain_error, refused, writes;
static int apa(void)
{
    unsigned char b[512];
    fseek(img, 0, SEEK_SET);
    fread(b, 1, 512, img);
    return b[4] == 'A' && b[5] == 'P' && b[6] == 'A' && b[7] == 0;
}
static int bd_read(struct block_device *bd, u64 s, void *b, u16 c)
{
    fseek(img, (long)(s + PART) * 512, SEEK_SET);
    return fread(b, 512, c, img) == c ? c : -EIO;
}
static int bd_write(struct block_device *bd, u64 s, const void *b, u16 c)
{
    if (c && (s + PART) < 1024 * 256 && apa()) { /* ata_bd_write_hits_apa_reserved */
        refused++;
        return plain_error ? -EIO : -EROFS;
    }
    writes++;
    fseek(img, (long)(s + PART) * 512, SEEK_SET);
    return fwrite(b, 512, c, img) == c ? c : -EIO;
}
static void bd_flush(struct block_device *bd) { fflush(img); }
static struct block_device the_bd = {0, "ata", 0, 0, 7, 512, PART, 1024 * 1024 - PART, bd_read, bd_write, bd_flush, 0};
struct block_device *fatfs_fs_driver_get_mounted_bd_from_index(int i) { return i == 0 ? &the_bd : 0; }
int sceCdReadClock(sceCdCLOCK *c) { return 0; }
void *ff_memalloc(UINT n) { return malloc(n); }
void ff_memfree(void *p) { free(p); }
int ff_mutex_create(int v) { return 1; }
void ff_mutex_delete(int v) {}
int ff_mutex_take(int v) { return 1; }
void ff_mutex_give(int v) {}
'''

TEST = r'''
#include <stdio.h>
#include <string.h>
#include "ff.h"
extern FILE *img;
extern int plain_error, refused, writes;
/* argv: image, apa (0/1), plain_error (0/1). Prints one line of results. */
int main(int c, char **v)
{
    FATFS fs;
    DIR d;
    int mk, mk2, cd, dvd, rootdir, wflag;
    img = fopen(v[1], "r+b");
    if (v[2][0] == '1') {
        fseek(img, 4, SEEK_SET);
        fwrite("APA\0", 1, 4, img);
        fflush(img);
    }
    plain_error = v[3][0] == '1';
    if (f_mount(&fs, "0:", 1) != FR_OK)
        return 1;
    mk = f_mkdir("0:/RA");
    cd = f_opendir(&d, "0:/CD");
    dvd = f_opendir(&d, "0:/DVD");
    rootdir = f_opendir(&d, "0:/");
    wflag = fs.wflag;
    mk2 = f_mkdir("0:/CFG");
    printf("mkdir=%d cd=%d dvd=%d root=%d wflag=%d mkdir2=%d refused=%d written=%d", mk, cd, dvd, rootdir, wflag, mk2, refused, writes);
    /* A fresh mount takes writes again (on this disk they are refused again at once). */
    f_mount(&fs, "0:", 1);
    refused = 0;
    f_mkdir("0:/CFG");
    printf(" remount_refused=%d\n", refused);
    return 0;
}
'''

failures = []
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'inc').mkdir()
    (tmp / 'mk').mkdir()
    (tmp / 'inc/module_debug.h').write_text('#define M_DEBUG(...)\n#define M_PRINTF(...)\n#define DEBUG_U64_2XU32(x)\n')
    conf = (fatfs / 'include/ffconf.h').read_text()
    conf, n = re.subn(r'#define FF_USE_MKFS\s+0', '#define FF_USE_MKFS 1', conf)
    assert n == 1
    (tmp / 'mk/ffconf.h').write_text(conf)
    for h in ('ff.h', 'diskio.h'):
        (tmp / 'mk' / h).write_text((fatfs / 'include' / h).read_text())
    (tmp / 'disk.c').write_text(DISK)
    (tmp / 'mkimg.c').write_text(MKIMG)
    (tmp / 'test.c').write_text(TEST)
    (tmp / 'bd.c').write_text(BD)
    (tmp / 'inc/bdm.h').write_text('#include <stddef.h>\ntypedef unsigned short u16;\nstruct block_device { void *priv; char *name; unsigned int devNr, parNr; unsigned char parId; unsigned int sectorSize; u64 sectorOffset, sectorCount; int (*read)(struct block_device *, u64, void *, u16); int (*write)(struct block_device *, u64, const void *, u16); void (*flush)(struct block_device *); int (*stop)(struct block_device *); };\n')
    (tmp / 'inc/cdvdman.h').write_text('typedef struct { unsigned char stat, second, minute, hour, pad, day, month, year; } sceCdCLOCK;\nint sceCdReadClock(sceCdCLOCK *clock);\n#define btoi(b) ((b) / 16 * 10 + (b) % 16)\n')
    (tmp / 'inc/fs_driver.h').write_text('struct block_device *fatfs_fs_driver_get_mounted_bd_from_index(int index);\n')
    flags = ['-w', '-O1', '-Du64=unsigned long long', '-Du32=unsigned', f'-I{tmp}/inc']
    tail = [str(tmp / 'disk.c'), str(fatfs / 'ffunicode.c'), '-lm']
    subprocess.run(['gcc', *flags, f'-I{tmp}/mk', '-o', str(tmp / 'mkimg'), str(tmp / 'mkimg.c'), str(fatfs / 'ff.c'), *tail], check=True)
    subprocess.run(['gcc', *flags, f'-I{fatfs}/include', '-o', str(tmp / 'test'), str(tmp / 'test.c'), str(tmp / 'bd.c'), str(diskio_path), str(ff_path), str(fatfs / 'ffunicode.c'), '-lm'], check=True)
    base = tmp / 'base.img'
    subprocess.run([str(tmp / 'mkimg'), str(base), 'CD', 'DVD'], check=True)

    def run(apa, plain):
        image = tmp / 't.img'
        image.write_bytes(base.read_bytes())
        line = subprocess.run([str(tmp / 'test'), str(image), str(apa), str(plain)], check=True, capture_output=True, text=True).stdout
        values = {k: int(x) for k, x in (pair.split('=') for pair in line.split())}
        before = base.read_bytes()
        changed = image.read_bytes() != before[:4] + (b'APA\0' if apa else before[4:8]) + before[8:]
        return values, changed

    r, changed = run(1, 0)
    if r['mkdir'] == 0:
        failures.append(f'APA disk: mkdir inside the fenced area reported success: {r}')
    if r['cd'] or r['dvd'] or r['root']:
        failures.append(f'APA disk: a refused write stopped the folders from opening: {r}')
    if r['wflag'] or r['written'] or changed:
        failures.append(f'APA disk: something was written or left waiting to be written: {r} changed={changed}')
    if r['mkdir2'] != 10 or r['refused'] != 1:
        failures.append(f'APA disk: after the first refusal the volume must be write protected (10) and ask the drive nothing more: {r}')
    if r['remount_refused'] != 1:
        failures.append(f'APA disk: a fresh mount must try the drive again: {r}')

    r, changed = run(1, 1)
    if r['mkdir'] == 0 or not r['wflag'] or r['cd'] != 1 or r['written'] or changed:
        failures.append(f'plain write error: the changed sector must stay waiting, as upstream does: {r} changed={changed}')

    r, changed = run(0, 0)
    if r['mkdir'] or r['mkdir2'] or r['cd'] or r['dvd'] or r['root'] or r['refused'] or not r['written']:
        failures.append(f'plain exFAT disk: the folders were not created: {r}')

if failures:
    print('refused write checks FAILED:')
    for f in failures:
        print(' -', f)
    sys.exit(1)
print('refused write: all cases passed')
