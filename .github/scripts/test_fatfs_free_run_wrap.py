"""f_expand never takes free space at the end of a volume and free space at its start for one run.

Runs the shipped FatFs on FAT32 and exFAT volumes held in memory. This is the allocator, not the
PS2 driver glue, a block device or a console."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
fatfs = root / 'modules/bdm/bdmfs_fatfs/fatfs'
assert 'test_fatfs_free_run_wrap.py' in (root / 'modules/bdm/bdmfs_fatfs/ORIGIN.txt').read_text(), 'ORIGIN.txt must list the local change'

PROGRAM = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.c" /* one unit: the test marks clusters free through the static FAT and bitmap helpers */

#define SECTOR 512
#define MB (1024u * 1024u)
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static BYTE *disk;
static LBA_t disk_sectors;
DSTATUS disk_status(BYTE n) { (void)n; return 0; }
DSTATUS disk_initialize(BYTE n) { (void)n; return 0; }
DRESULT disk_read(BYTE n, BYTE *out, LBA_t sector, UINT count) {
    (void)n; if (sector + count > disk_sectors) return RES_PARERR;
    memcpy(out, disk + sector * SECTOR, (size_t)count * SECTOR); return RES_OK;
}
DRESULT disk_write(BYTE n, const BYTE *in, LBA_t sector, UINT count) {
    (void)n; if (sector + count > disk_sectors) return RES_PARERR;
    memcpy(disk + sector * SECTOR, in, (size_t)count * SECTOR); return RES_OK;
}
DRESULT disk_ioctl(BYTE n, BYTE cmd, void *out) {
    (void)n;
    switch (cmd) {
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t *)out = disk_sectors; return RES_OK;
        case GET_SECTOR_SIZE: *(WORD *)out = SECTOR; return RES_OK;
        case GET_BLOCK_SIZE: *(DWORD *)out = 1; return RES_OK;
    }
    return RES_PARERR;
}
DWORD get_fattime(void) { return ((DWORD)(2026 - 1980) << 25) | (1 << 21) | (1 << 16); }
int ff_mutex_create(int n) { (void)n; return 1; }
void ff_mutex_delete(int n) { (void)n; }
int ff_mutex_take(int n) { (void)n; return 1; }
void ff_mutex_give(int n) { (void)n; }

static DWORD free_clusters(void) {
    DWORD n; FATFS *fs;
    CHECK(f_getfree("0:", &n, &fs) == FR_OK);
    return n;
}
/* A new file of exactly this size in one run; returns its first cluster. */
static DWORD reserve(const char *path, unsigned long long bytes) {
    FIL f; DWORD start;
    CHECK(f_open(&f, path, FA_CREATE_NEW | FA_WRITE) == FR_OK);
    CHECK(f_expand(&f, (FSIZE_t)bytes, 1) == FR_OK);
    start = f.obj.sclust;
    CHECK(f_close(&f) == FR_OK);
    return start;
}
static void run(const char *label, BYTE format, unsigned megabytes, unsigned cluster) {
    static FATFS fs;
    static BYTE work[65536];
    MKFS_PARM opt = {format | FM_SFD, 1, 0, 0, cluster};
    FIL f;
    DWORD before, head, lead, saved[64], c;

    disk_sectors = (LBA_t)megabytes * MB / SECTOR;
    free(disk);
    disk = calloc((size_t)disk_sectors, SECTOR);
    CHECK(disk);
    CHECK(f_mkfs("0:", &opt, work, sizeof(work)) == FR_OK);
    CHECK(f_mount(&fs, "0:", 1) == FR_OK);
    /* 8 MiB free at the start of the data area, 6 MiB free at its very end, the rest in use. */
    head = reserve("0:/HEAD.BIN", 8 * MB);
    reserve("0:/MID.BIN", (unsigned long long)free_clusters() * cluster - 6 * MB);
    CHECK(f_unlink("0:/HEAD.BIN") == FR_OK);
    before = free_clusters();
    CHECK((unsigned long long)before * cluster == 14 * MB);

    CHECK(f_open(&f, "0:/CARD.BIN", FA_CREATE_NEW | FA_WRITE) == FR_OK);
    CHECK(f_expand(&f, 12 * MB, 1) == FR_DENIED);
    CHECK(f_size(&f) == 0 && f.obj.sclust == 0);
    /* The same with every cluster in front of the 8 MiB hole free too, so the free space starts
       at the volume's first cluster. Formatters put the root folder or the exFAT bitmap there, so
       those are marked free by hand and put back. The longest real run is then those clusters
       plus 8 MiB; one cluster more can only come from joining the end of the volume to its start. */
    lead = head - 2;
    CHECK(lead >= 1 && lead <= 64);
    for (c = 0; c < lead; c++) {
        if (format == FM_EXFAT) CHECK(change_bitmap(&fs, 2 + c, 1, 0) == FR_OK);
        else CHECK((saved[c] = get_fat(&f.obj, 2 + c)) >= 2 && put_fat(&fs, 2 + c, 0) == FR_OK);
    }
    CHECK(f_expand(&f, 8 * MB + (FSIZE_t)(lead + 1) * cluster, 1) == FR_DENIED);
    CHECK(f_size(&f) == 0 && f.obj.sclust == 0);
    for (c = 0; c < lead; c++) {
        if (format == FM_EXFAT) CHECK(change_bitmap(&fs, 2 + c, 1, 1) == FR_OK);
        else CHECK(put_fat(&fs, 2 + c, saved[c]) == FR_OK);
    }
    CHECK(f_close(&f) == FR_OK && f_unlink("0:/CARD.BIN") == FR_OK);
    CHECK(free_clusters() == before);
    CHECK(f_mount(NULL, "0:", 0) == FR_OK && f_mount(&fs, "0:", 1) == FR_OK);
    CHECK(free_clusters() == before);

    /* Nothing was taken from either end: each still takes a file of exactly its size in one run,
       and the run at the end reaches the last cluster of the volume. */
    CHECK(reserve("0:/HEAD2.BIN", 8 * MB) == head);
    CHECK(reserve("0:/TAIL.BIN", 6 * MB) + 6 * MB / cluster == fs.n_fatent);
    CHECK(free_clusters() == 0);
    CHECK(f_mount(NULL, "0:", 0) == FR_OK);
    printf("%s: the end of the volume and its start are not joined; a run up to the last cluster is still found\n", label);
}
int main(void) {
    run("FAT32", FM_FAT32, 112, 1024);
    run("exFAT", FM_EXFAT, 112, 4096);
    free(disk);
    return 0;
}
'''

with tempfile.TemporaryDirectory() as temp:
    build = Path(temp)
    for source_file in [fatfs / 'ff.c', fatfs / 'ffunicode.c', *sorted((fatfs / 'include').glob('*.h'))]:
        shutil.copyfile(source_file, build / source_file.name)
    (build / 'module_debug.h').write_text('#include <stdint.h>\ntypedef uint64_t u64;\n#define M_DEBUG(...) ((void)0)\n#define M_PRINTF(...) ((void)0)\n#define DEBUG_U64_2XU32(x) ((void)0)\n')
    config = build / 'ffconf.h'
    text = config.read_text()
    assert re.search(r'#define FF_USE_EXPAND\s+1\b', text), 'the shipped FatFs must enable f_expand'
    text, changed = re.subn(r'(#define FF_USE_MKFS\s+)[01]\b', r'\g<1>1', text)  # test images only
    assert changed == 1
    config.write_text(text)
    header = build / 'ff.h'
    header.write_text(header.read_text().replace('#if defined(_WIN32)', '#if defined(_MSC_VER)'))
    c, exe = build / 'wrap.c', build / 'wrap.exe'
    c.write_text(PROGRAM)
    subprocess.run(['gcc', '-std=gnu99', '-O2', '-Wall', '-Wno-unused-function', '-I', str(build),
                    str(c), str(build / 'ffunicode.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=90)
