"""APA table safety: exercise the production write fence and pin the driver's write policy.

Part 1 compiles modules/hdd/apa/src/table_fence.h -- the exact header the IOP driver uses -- on the
host and drives it with real __mbr and partition headers.

Part 2 statically checks the vendored driver still refuses every partition-table edit the loader
never performs, so a careless re-vendor from ps2sdk cannot silently drop the protection. These are
text checks on purpose: the policy lives in a handful of well-known functions.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
apa = root / 'modules/hdd/apa'
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
#include "table_fence.h"

static unsigned char hdr[1024];

static void put32(unsigned int ofs, u32 v)
{
    hdr[ofs] = v & 0xff;
    hdr[ofs + 1] = (v >> 8) & 0xff;
    hdr[ofs + 2] = (v >> 16) & 0xff;
    hdr[ofs + 3] = (v >> 24) & 0xff;
}

static void seal(void)
{
    u32 sum = 0;
    unsigned int i;
    for (i = 1; i < 256; i++)
        sum += apaFenceWord(hdr, i * 4);
    put32(0, sum);
}

static void make_mbr(u32 next, u32 prev)
{
    memset(hdr, 0, sizeof(hdr));
    put32(0x04, 0x00415041);
    put32(0x08, next);
    put32(0x0C, prev);
    memcpy(hdr + 0x10, "__mbr", 5);
    put32(0x40, 0);
    put32(0x44, 0x40000);
    put32(0x48, 0x0001);
    memcpy(hdr + 0x100, "Sony Computer Entertainment Inc.", 32);
    put32(0x120, 2);
    seal();
}

static void make_partition(const char *id, u32 start)
{
    memset(hdr, 0, sizeof(hdr));
    put32(0x04, 0x00415041);
    memcpy(hdr + 0x10, id, strlen(id));
    put32(0x40, start);
    put32(0x44, 0x40000);
    put32(0x48, 0x0100);
    seal();
}

#define TOTAL 0x08000000u

int main(void)
{
    /* A legitimate table edit: a whole, checksummed __mbr header at LBA 0. */
    make_mbr(0x40000, 0x7FC0000);
    assert(apaFenceIsValidMbrHeader(hdr, TOTAL));
    assert(apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    assert(apaFenceWriteAllowed(0, 2, hdr, 0)); /* capacity unknown: links are not range-checked */

    /* ...but not a partial one, an oversized one, or none at all. */
    assert(!apaFenceWriteAllowed(0, 1, hdr, TOTAL));
    assert(!apaFenceWriteAllowed(0, 3, hdr, TOTAL));
    assert(!apaFenceWriteAllowed(0, 8, hdr, TOTAL));
    assert(!apaFenceWriteAllowed(0, 2, 0, TOTAL));

    /* A valid header shifted into the table sector is still a misdirected write. */
    assert(!apaFenceWriteAllowed(1, 2, hdr, TOTAL));
    assert(!apaFenceWriteAllowed(6, 2, hdr, TOTAL));

    /* Every field the driver itself validates, and every formatter invariant, is enforced. */
    make_mbr(0x40000, 0x7FC0000);
    hdr[0x200] = 1; /* payload changed after sealing: checksum no longer matches */
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(0x40000, 0x7FC0000);
    put32(0x04, 0x00415042);
    seal();
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(0x40000, 0x7FC0000);
    hdr[0x100] = 'X';
    seal();
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(0x40000, 0x7FC0000);
    memcpy(hdr + 0x10, "__mbrX", 6);
    seal();
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(0x40000, 0x7FC0000);
    memcpy(hdr + 0x16, "junk", 4); /* padding after the terminator is not part of the id */
    seal();
    assert(apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(0x40000, 0x7FC0000);
    memcpy(hdr + 0x10, "__mb", 4);
    hdr[0x14] = 0;
    seal();
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(0x40000, 0x7FC0000);
    put32(0x40, 0x40000);
    seal();
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(0x40000, 0x7FC0000);
    put32(0x48, 0x0100);
    seal();
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(0x40000, 0x7FC0000);
    put32(0x4C, 1);
    seal();
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(TOTAL, 0x7FC0000);
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));
    make_mbr(0x40000, TOTAL + 5);
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));

    /* A real partition's header can never land at LBA 0. */
    make_partition("+OPL", 0x40000);
    assert(!apaFenceWriteAllowed(0, 2, hdr, TOTAL));

    /* The SDK error records (LBA 6 and 7) and anything else in the physical sector are refused. */
    memset(hdr, 0, sizeof(hdr));
    put32(0, 0x12345678);
    assert(!apaFenceWriteAllowed(6, 1, hdr, TOTAL));
    assert(!apaFenceWriteAllowed(7, 1, hdr, TOTAL));
    assert(!apaFenceWriteAllowed(2, 4, hdr, TOTAL));
    assert(!apaFenceWriteAllowed(7, 2, hdr, TOTAL)); /* straddles the boundary */

    /* Outside the physical sector: the journal, partition headers and data all pass untouched. */
    assert(apaFenceWriteAllowed(8, 1, hdr, TOTAL));
    assert(apaFenceWriteAllowed(10, 2, hdr, TOTAL));
    assert(apaFenceWriteAllowed(0x40000, 2, hdr, TOTAL));
    assert(apaFenceWriteAllowed(3, 0, hdr, TOTAL)); /* zero-length: no I/O happens */

    puts("fence: all cases passed");
    return 0;
}
'''


def run_fence_harness():
    with tempfile.TemporaryDirectory() as tmp:
        c_file = Path(tmp) / 'fence_test.c'
        exe = Path(tmp) / 'fence_test'
        c_file.write_text(HARNESS)
        compile_cmd = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(apa / 'src'),
                       str(c_file), '-o', str(exe)]
        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            failures.append('fence harness failed to compile:\n' + result.stderr)
            return
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        if result.returncode != 0:
            failures.append('fence harness failed:\n' + result.stdout + result.stderr)


def text(path):
    # Windows checkouts carry CRLF; the checks below match on LF.
    return path.read_text().replace('\r\n', '\n')


def function_body(source, signature):
    # Definitions only (a prototype ends in ';'); parameter lists may wrap onto several lines.
    match = re.search(r'^' + re.escape(signature) + r'[^;{]*\)\s*\{', source, re.M)
    if match is None:
        return None
    end = source.index('\n}', match.start())
    return source[match.start():end]


def case_body(source, function_signature, case_label):
    body = function_body(source, function_signature)
    if body is None:
        return None
    # The live dispatch starts under fioSema; skip the APA_SUPPORT_BHDD pre-filter above it, which
    # also lists these labels.
    live = body.find('WaitSema(fioSema);')
    if live < 0:
        return None
    match = re.search(r'case ' + case_label + r':(.*?)break;', body[live:], re.S)
    return match.group(1) if match else None


def check_driver_policy():
    fio = text(apa / 'src/hdd_fio.c')
    apa_c = text(apa / 'src/apa.c')
    blkio = text(apa / 'src/hdd_blkio.h')
    fence_c = text(apa / 'src/table_fence.c')
    makefile = text(apa / 'Makefile')
    top_makefile = text(root / 'Makefile')

    check('table_fence.o' in makefile, 'modules/hdd/apa/Makefile no longer builds table_fence.o')
    check(re.search(r'static inline int blkIoDmaTransfer\([^)]*\)\s*\{\s*return apaFencedDmaTransfer\(', blkio),
          'hdd_blkio.h: blkIoDmaTransfer no longer routes through the fence')
    check('apaFenceWriteAllowed' in fence_c and 'sceAtaDmaTransfer' in fence_c,
          'table_fence.c: fenced transfer is missing its check or its transfer')
    check(re.search(r'modules/hdd/apa/ps2hdd-osd\.irx', top_makefile) and
          not re.search(r'ps2hdd\.c:\s*\$\(PS2SDK\)', top_makefile),
          'top-level Makefile embeds the SDK prebuilt ps2hdd-osd.irx instead of the fenced build')

    save_error = function_body(apa_c, 'void apaSaveError(')
    check(save_error is not None and 'blkIoDmaTransfer' not in save_error and 'blkIoFlushCache' not in save_error,
          'apa.c: apaSaveError writes error records again')
    set_part = function_body(apa_c, 'void apaSetPartErrorSector(')
    check(set_part is not None and 'apaCacheAlloc' not in set_part,
          'apa.c: apaSetPartErrorSector allocates again (NULL dereference on an exhausted cache)')

    fmt = function_body(fio, 'int hddFormat(')
    check(fmt is not None and re.search(r'return -EACCES;.*clear all errors', fmt, re.S),
          'hdd_fio.c: hddFormat no longer refuses before its first write')
    open_body = function_body(fio, 'static int apaOpen(')
    check(open_body is not None and 'hddAddPartitionHere(' not in open_body and 'rv = -EACCES' in open_body,
          'hdd_fio.c: apaOpen can create partitions again')
    for label in ('HIOCADDSUB', 'HIOCDELSUB'):
        body = case_body(fio, 'int hddIoctl2(', label)
        check(body is not None and '-EACCES' in body and 'ioctl2' not in body,
              'hdd_fio.c: %s is reachable again' % label)
    for label in ('HDIOC_SWAPTMP', 'HDIOC_SETOSDMBR', 'HDIOC_WRITESECTOR'):
        body = case_body(fio, 'int hddDevctl(', label)
        check(body is not None and '-EACCES' in body and 'devctl' not in body and 'blkIoDmaTransfer' not in body,
              'hdd_fio.c: %s is reachable again' % label)
    # A game loader is not a partition manager: delete and rename refuse before touching the table.
    for signature, call in (('int hddRemove(', 'apaRemove('), ('int hddReName(', 'apaRename(')):
        body = function_body(fio, signature)
        check(body is not None and 0 <= body.find('return -EACCES;') < body.find(call),
              'hdd_fio.c: %s can reach %s again' % (signature.rstrip('('), call.rstrip('(')))
    transfer = function_body(fio, 'static int ioctl2Transfer(')
    check(transfer is not None and 'arg->sector + arg->size' not in transfer,
          'hdd_fio.c: ioctl2Transfer bounds check can wrap again')

    # A corrupt on-disk sub-partition count must never index past the 64-entry subs array.
    open_body = function_body(fio, 'static int apaOpen(')
    check(open_body is not None and re.search(r'nsub > APA_MAXSUB.*?return -EIO;.*?fileSlot->parts\[0\]', open_body, re.S),
          'hdd_fio.c: apaOpen no longer rejects an out-of-range sub-partition count before using it')
    stat_body = function_body(fio, 'static void fioGetStatFiller(')
    check(stat_body is not None and 'i < APA_MAXSUB' in stat_body,
          'hdd_fio.c: fioGetStatFiller walks subs[] without the APA_MAXSUB bound')
    walk = function_body(apa_c, 'apa_cache_t *apaGetNextHeader(')
    check(walk is not None and 'APA_CACHE_FLAG_DIRTY' not in walk and 'apaCacheFlushAllDirty' not in walk,
          'apa.c: walking the partition chain writes headers again (start != prev repair)')

    # ...and the loader itself never asks for a partition edit or a raw write.
    hdd_c = text(root / 'src/platform/hdd.cpp')
    support = text(root / 'src/sources/hddsupport.cpp')
    check('HDIOC_WRITESECTOR' not in hdd_c and 'unlink(' not in hdd_c,
          'src/platform/hdd.c: the loader issues raw sector writes or partition deletes again')
    check('fileXioRename(' not in support and 'hddDeleteGame' not in support and 'hddSetHDLGameInfo' not in support,
          'src/sources/hddsupport.c: the HDD page renames a partition, deletes a game or rewrites an HDL header again')

    # The probe classifies a GPT/APA hybrid as GPT before it ever looks for an APA header.
    probe = function_body(text(root / 'src/sources/hddsupport.cpp'), 'int hddDetectNonSonyFileSystem(')
    check(probe is not None and 0 <= probe.find('"EFI PART"') < probe.find('"APA", 3'),
          'hddsupport.c: the probe no longer classifies GPT/APA hybrids before the APA header check')


BD_HARNESS_PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define ATA_DIR_READ 0
static unsigned char disk_table[1024]; /* LBA 0-1 */
static unsigned char disk_bs[512];
static u64 disk_bs_lba;
static int reads, read_fails, bs_read_fails;
static int ata_device_sector_io64(int device, void *buf, u64 lba, u32 nsectors, int dir)
{
    (void)device;
    assert(dir == ATA_DIR_READ);
    reads++;
    if (lba == 0) {
        assert(nsectors == 2);
        if (read_fails)
            return -5;
        memcpy(buf, disk_table, 1024);
        return 0;
    }
    assert(nsectors == 1);
    if (bs_read_fails)
        return -5;
    if (lba == disk_bs_lba)
        memcpy(buf, disk_bs, 512);
    else
        memset(buf, 0, 512);
    return 0;
}
'''

BD_HARNESS_MAIN = r'''
static void put32(unsigned char *p, u32 v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = v >> 24;
}

static u32 header_sum(void)
{
    u32 sum = 0;
    int i;
    for (i = 1; i < 256; i++)
        sum += (u32)disk_table[i * 4] | ((u32)disk_table[i * 4 + 1] << 8) | ((u32)disk_table[i * 4 + 2] << 16) | ((u32)disk_table[i * 4 + 3] << 24);
    return sum;
}

static void set_entry(int i, u8 status, u8 type, u32 start, u32 size)
{
    unsigned char *e = disk_table + 446 + i * 16;
    memset(e, 0, 16);
    e[0] = status;
    e[4] = type;
    put32(e + 8, start);
    put32(e + 12, size);
}

/* An APA header with a correct checksum (a real APA disk, or APA-Jail when an MBR is added). */
static void apa_header(int with_mbr)
{
    memset(disk_table, 0, sizeof(disk_table));
    put32(disk_table + 4, 0x00415041);
    memcpy(disk_table + 0x10, "__mbr", 6);
    memcpy(disk_table + 0x100, "Sony Computer Entertainment Inc.", 32);
    if (with_mbr) {
        disk_table[510] = 0x55;
        disk_table[511] = 0xAA;
    }
}

static void seal(int valid)
{
    put32(disk_table, header_sum() + (valid ? 0 : 1));
}

static void exfat_bs(u64 lba, u64 sectors, u8 shift)
{
    memset(disk_bs, 0, sizeof(disk_bs));
    disk_bs[0] = 0xEB;
    disk_bs[1] = 0x76;
    disk_bs[2] = 0x90;
    memcpy(disk_bs + 3, "EXFAT   ", 8);
    put32(disk_bs + 72, (u32)sectors);
    put32(disk_bs + 76, (u32)(sectors >> 32));
    disk_bs[108] = shift;
    disk_bs[510] = 0x55;
    disk_bs[511] = 0xAA;
    disk_bs_lba = lba;
}

/* lilkuz2005's kind of disk: once APA, then an MBR and exFAT from a PC tool that kept the APA bytes,
   so the old header no longer checksums. */
static void stale_disk(u32 start, u32 size, u64 volume)
{
    apa_header(1);
    set_entry(0, 0x00, 0x07, start, size);
    seal(0);
    exfat_bs(start, volume, 9);
}

#define REFUSED(s, c) assert(ata_bd_write_hits_apa_reserved(0, (s), (c)))
#define ALLOWED(s, c) assert(!ata_bd_write_hits_apa_reserved(0, (s), (c)))

int main(void)
{
    /* A real APA disk (valid header, no MBR): nothing below the first user partition. */
    apa_header(0);
    seal(1);
    REFUSED(0, 1);
    REFUSED(7, 1);
    REFUSED(0x3FFFF, 1);
    REFUSED(2048, 64);
    ALLOWED(0x40000, 1);
    ALLOWED(0x12345678, 8);
    /* The bare magic with garbage after it is refused too: no MBR signature. */
    memset(disk_table + 8, 0xA5, 1016);
    disk_table[510] = 0;
    REFUSED(2048, 64);

    /* A real APA disk keeps its protection even when every other condition holds: a valid header
       with an MBR, an exFAT entry and a matching boot sector (APA-Jail's sector 0, or a crafted one). */
    apa_header(1);
    set_entry(0, 0x00, 0x07, 2048, 1000000);
    seal(1);
    exfat_bs(2048, 1000000, 9);
    REFUSED(2048, 8);
    REFUSED(4096, 64);

    /* APA-Jail as PSBBN builds it: valid header, exFAT entry past the reserved area. Its exFAT writes
       are above the fence and never read LBA 0; nothing below it is writable. */
    apa_header(1);
    set_entry(0, 0x00, 0x07, 0x200000, 0x1000000);
    seal(1);
    exfat_bs(0x200000, 0x1000000, 9);
    reads = 0;
    ALLOWED(0x200000, 8);
    assert(reads == 0);
    REFUSED(2048, 8);
    REFUSED(0x3FFFF, 1);

    /* The stale-APA disk: writes inside the exFAT volume go through. */
    stale_disk(2048, 1000000, 1000000);
    ALLOWED(2048, 1);
    ALLOWED(2048 + 100, 8);
    ALLOWED(0x3FFFF, 1);
    ALLOWED(0x3FFF0, 0x20);
    /* ...and nothing outside it: the table sectors, the gap before the partition. */
    REFUSED(0, 1);
    REFUSED(1, 1);
    REFUSED(6, 2);
    REFUSED(63, 1);
    REFUSED(2047, 1);
    REFUSED(2047, 2);

    /* Bounded by the entry and by the volume, whichever ends first. */
    stale_disk(2048, 4096, 4000);
    ALLOWED(2048 + 3999, 1);
    REFUSED(2048 + 3999, 2);
    REFUSED(2048 + 4000, 1);
    REFUSED(2048 + 4095, 2);
    stale_disk(2048, 4096, 4096);
    ALLOWED(2048 + 4095, 1);
    REFUSED(2048 + 4095, 2);
    stale_disk(2048, 4096, 4097); /* volume larger than its entry: refused outright */
    REFUSED(2048, 1);

    /* No MBR signature. */
    stale_disk(2048, 1000000, 1000000);
    disk_table[511] = 0;
    REFUSED(4096, 8);

    /* Not a FAT/exFAT entry, or an invalid status byte. */
    stale_disk(2048, 1000000, 1000000);
    set_entry(0, 0x00, 0x17, 2048, 1000000);
    REFUSED(4096, 8);
    set_entry(0, 0x00, 0x00, 2048, 1000000);
    REFUSED(4096, 8);
    set_entry(0, 0x05, 0x07, 2048, 1000000);
    REFUSED(4096, 8);
    set_entry(0, 0x80, 0x07, 2048, 1000000);
    ALLOWED(4096, 8);

    /* A partition starting inside LBA 0-7 (the physical sector of the table) is never trusted. */
    stale_disk(1, 0x50000, 0x50000);
    REFUSED(1, 1);
    REFUSED(8, 1);
    stale_disk(8, 0x50000, 0x50000);
    ALLOWED(8, 1);

    /* Overlapping entries: the table cannot be trusted at all. */
    stale_disk(2048, 1000000, 1000000);
    set_entry(1, 0x00, 0x0C, 500000, 100000);
    REFUSED(4096, 8);
    set_entry(1, 0x00, 0x0C, 2048 + 1000000, 100000); /* touching, not overlapping */
    ALLOWED(4096, 8);
    set_entry(1, 0x00, 0x05, 100, 2000); /* any entry type counts */
    REFUSED(4096, 8);

    /* The boot sector must be FAT/exFAT of 512-byte sectors. */
    stale_disk(2048, 1000000, 1000000);
    exfat_bs(2048, 1000000, 12);
    REFUSED(4096, 8);
    exfat_bs(2048, 1000000, 9);
    memcpy(disk_bs + 3, "NTFS    ", 8);
    REFUSED(4096, 8);
    exfat_bs(2048, 1000000, 9);
    disk_bs[511] = 0;
    REFUSED(4096, 8);
    exfat_bs(4096, 1000000, 9); /* boot sector not where the entry starts */
    REFUSED(4096, 8);
    exfat_bs(2048, 0, 9);
    REFUSED(4096, 8);

    /* FAT32 inside a 0x0C entry. */
    stale_disk(2048, 1000000, 1000000);
    set_entry(0, 0x00, 0x0C, 2048, 1000000);
    memset(disk_bs, 0, sizeof(disk_bs));
    disk_bs[0] = 0xEB;
    disk_bs[12] = 0x02;
    put32(disk_bs + 32, 1000000);
    memcpy(disk_bs + 82, "FAT32   ", 8);
    disk_bs[510] = 0x55;
    disk_bs[511] = 0xAA;
    ALLOWED(4096, 8);
    disk_bs[12] = 0x10; /* 4096-byte sectors */
    REFUSED(4096, 8);
    disk_bs[12] = 0x02;
    put32(disk_bs + 32, 1000001);
    REFUSED(4096, 8);

    /* Two entries both holding the write cannot happen without overlap; one entry, two disjoint
       volumes: only the one that holds the write counts. */
    stale_disk(2048, 100000, 100000);
    set_entry(1, 0x00, 0x07, 200000, 100000);
    REFUSED(150000, 1);
    ALLOWED(2048, 1);

    /* Read failures refuse: LBA 0-1, and the boot sector. */
    stale_disk(2048, 1000000, 1000000);
    bs_read_fails = 1;
    REFUSED(4096, 8);
    bs_read_fails = 0;
    read_fails = 1;
    REFUSED(4096, 8);
    REFUSED(6, 1);
    read_fails = 0;

    /* A plain MBR/exFAT disk (or a superfloppy) keeps full write access. */
    memset(disk_table, 0, sizeof(disk_table));
    memcpy(disk_table + 3, "EXFAT   ", 8);
    disk_table[510] = 0x55;
    disk_table[511] = 0xAA;
    ALLOWED(1, 1);
    ALLOWED(2048, 8);

    /* Writes above the reserved area never pay for the probe read, and zero-length writes do nothing. */
    reads = 0;
    ALLOWED(0x40000, 1);
    ALLOWED(0, 0);
    assert(reads == 0);

    puts("bdm fence: all cases passed");
    return 0;
}
'''

RANGE_HARNESS_PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { u32 exists, has_packet, total_sectors, security_status, lba48, total_sectors_lba48; } ata_devinfo_t;
'''

RANGE_HARNESS_MAIN = r'''
int main(void)
{
    ata_devinfo_t small = {1, 0, 0x0FFFFFFF, 0, 0, 0x0FFFFFFF};   /* 137 GB, no LBA48 */
    ata_devinfo_t tiny = {1, 0, 1000000, 0, 0, 1000000};           /* no LBA48, exact size known */
    ata_devinfo_t big = {1, 0, 0x0FFFFFFF, 0, 1, 0x74706DB0};      /* 1 TB, LBA48 */
    ata_devinfo_t huge = {1, 0, 0x0FFFFFFF, 0, 1, 0xFFFFFFFF};     /* > 2 TiB: the u32 saturates */

    /* Without LBA48, a request that would wrap past 2^28 (or past the end) is refused. */
    assert(ata_request_in_range(&small, 0, 8));
    assert(ata_request_in_range(&small, 0x0FFFFFFE, 1));
    assert(!ata_request_in_range(&small, 0x0FFFFFFF, 1));
    assert(!ata_request_in_range(&small, 0x10000000, 1));
    assert(!ata_request_in_range(&small, 0x0FFFFF00, 0x200));
    assert(ata_request_in_range(&tiny, 999999, 1));
    assert(!ata_request_in_range(&tiny, 999999, 2));

    /* With LBA48 and a real capacity, the capacity is the limit. */
    assert(ata_request_in_range(&big, 0x74706DAF, 1));
    assert(!ata_request_in_range(&big, 0x74706DB0, 1));
    assert(!ata_request_in_range(&big, 0xFFFFFFFFFFFFull, 1));

    /* Past 2 TiB the reported capacity is saturated: stay usable beyond it, but never past 2^48. */
    assert(ata_request_in_range(&huge, 0x1FFFFFFFFull, 8));
    assert(ata_request_in_range(&huge, (1ull << 48) - 8, 8));
    assert(!ata_request_in_range(&huge, (1ull << 48) - 8, 9));
    assert(!ata_request_in_range(&huge, 0xFFFFFFFFFFFFFFFFull, 1));

    /* Zero-length requests do no I/O and are never refused. */
    assert(ata_request_in_range(&small, 0x20000000, 0));

    puts("ata range: all cases passed");
    return 0;
}
'''


def run_range_harness():
    atad = text(root / 'modules/hdd/atad/src/ps2atad.c')
    match = re.search(r'static int ata_request_in_range\(.*?\n\}\n', atad, re.S)
    if match is None:
        failures.append('ps2atad.c: the ATA addressable-range check is missing')
        return
    io_body = function_body(atad, 'int ata_device_sector_io64(')
    check(io_body is not None and 0 <= io_body.find('ata_request_in_range(') < io_body.find('while (res == 0'),
          'ps2atad.c: ata_device_sector_io64 no longer checks the addressable range before its first command')
    with tempfile.TemporaryDirectory() as tmp:
        c_file = Path(tmp) / 'range_test.c'
        exe = Path(tmp) / 'range_test'
        c_file.write_text(RANGE_HARNESS_PREFIX + match.group(0) + RANGE_HARNESS_MAIN)
        result = subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(c_file), '-o', str(exe)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            failures.append('ata range harness failed to compile:\n' + result.stderr)
            return
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        if result.returncode != 0:
            failures.append('ata range harness failed:\n' + result.stdout + result.stderr)


SECTOR_SIZE_HARNESS_PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint16_t u16;
typedef uint32_t u32;
'''

SECTOR_SIZE_HARNESS_MAIN = r'''
int main(void)
{
    u16 id[256];

    /* Old drive: word 106 not valid. */
    memset(id, 0, sizeof(id));
    assert(ata_identify_logical_sector_size(id) == 512);

    /* 512e: 4K physical (bit 13, exponent 3), 512 logical -- accepted. */
    id[106] = 0x4000 | 0x2000 | 3;
    assert(ata_identify_logical_sector_size(id) == 512);

    /* 4Kn: bit 12, 2048 words = 4096 bytes -- refused by the caller. */
    id[106] = 0x4000 | 0x1000;
    id[117] = 2048;
    id[118] = 0;
    assert(ata_identify_logical_sector_size(id) == 4096);

    /* Bit 12 without a size is not 512 either. */
    id[117] = 0;
    assert(ata_identify_logical_sector_size(id) != 512);

    /* A garbage word count that would wrap to 512 when doubled is still refused. */
    id[117] = 0x0100;
    id[118] = 0x8000;
    assert(ata_identify_logical_sector_size(id) != 512);

    /* Word 106 with bit 15 set is invalid and ignored. */
    id[106] = 0x8000 | 0x4000 | 0x1000;
    id[117] = 2048;
    assert(ata_identify_logical_sector_size(id) == 512);

    puts("ata logical sector size: all cases passed");
    return 0;
}
'''


def run_sector_size_harness():
    atad = text(root / 'modules/hdd/atad/src/ps2atad.c')
    match = re.search(r'static u32 ata_identify_logical_sector_size\(.*?\n\}\n', atad, re.S)
    if match is None:
        failures.append('ps2atad.c: the IDENTIFY logical sector size check is missing')
        return
    io_body = function_body(atad, 'int ata_device_sector_io64(')
    check(io_body is not None and 0 <= io_body.find('ata_logical_sector_size[device] != 512') < io_body.find('while (res == 0'),
          'ps2atad.c: ata_device_sector_io64 no longer refuses drives with non-512-byte logical sectors')
    check(0 <= atad.find('ata_logical_sector_size[i] = ata_identify_logical_sector_size(ata_param);') <
          atad.find('sceAtaGetSceId(i, ata_param)'),
          'ps2atad.c: the logical sector size is no longer read before the Sony identify overwrites ata_param')
    check('g_ata_bd[i].sectorSize = ata_logical_sector_size[i];' in atad,
          'ps2atad.c: the ATA block device no longer reports its logical sector size to BDM')
    with tempfile.TemporaryDirectory() as tmp:
        c_file = Path(tmp) / 'sector_size_test.c'
        exe = Path(tmp) / 'sector_size_test'
        c_file.write_text(SECTOR_SIZE_HARNESS_PREFIX + match.group(0) + SECTOR_SIZE_HARNESS_MAIN)
        result = subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(c_file), '-o', str(exe)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            failures.append('ata sector size harness failed to compile:\n' + result.stderr)
            return
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        if result.returncode != 0:
            failures.append('ata sector size harness failed:\n' + result.stdout + result.stderr)


def run_bd_harness():
    atad = text(root / 'modules/hdd/atad/src/ps2atad.c')
    check('lba += chunk;' in atad and 'nsectors -= chunk;' in atad,
          'ps2atad.c: the 65536-sector zero-progress loop fix (upstream f30f05eba) is gone')
    match = re.search(r'#define ATA_BD_APA_RESERVED_SECTORS.*?\nstatic int ata_bd_write_hits_apa_reserved\(.*?\n\}\n',
                      atad, re.S)
    if match is None:
        failures.append('ps2atad.c: the BDM APA reserved-area guard is missing')
        return
    write_body = function_body(atad, 'static int ata_bd_write(')
    check(write_body is not None and
          write_body.find('ata_bd_write_hits_apa_reserved(') != -1 and
          write_body.find('ata_bd_write_hits_apa_reserved(') < write_body.find('ATA_DIR_WRITE'),
          'ps2atad.c: ata_bd_write no longer checks the APA reserved area before writing')
    with tempfile.TemporaryDirectory() as tmp:
        c_file = Path(tmp) / 'bd_test.c'
        exe = Path(tmp) / 'bd_test'
        c_file.write_text(BD_HARNESS_PREFIX + match.group(0) + BD_HARNESS_MAIN)
        result = subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(c_file), '-o', str(exe)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            failures.append('bdm fence harness failed to compile:\n' + result.stderr)
            return
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        if result.returncode != 0:
            failures.append('bdm fence harness failed:\n' + result.stdout + result.stderr)


# hdd0:/hdd1: is the raw APA partition namespace, not a filesystem: there, mkdir/open(O_CREAT) means
# "create a partition" and remove means "delete one". Only the three files that own APA handling may
# spell it. Everything else reaches the HDD through the mounted pfs0: data home. (The driver refuses
# creation regardless; this keeps new code from even trying, as the RA launch log and the tar device
# table once did.)
RAW_APA_LITERAL = re.compile(r'"hdd(?:[0-9]|%[a-z])?:')
RAW_APA_ALLOWED = {'src/platform/hdd.cpp', 'src/sources/hddsupport.cpp', 'src/app/opl.cpp'}


def check_raw_apa_namespace():
    for directory in ('src', 'ee_core/src', 'include'):
        for path in sorted((root / directory).rglob('*.[ch]')):
            rel = path.relative_to(root).as_posix()
            source = text(path)
            if rel not in RAW_APA_ALLOWED:
                for number, line in enumerate(source.split('\n'), 1):
                    if RAW_APA_LITERAL.search(line):
                        failures.append('%s:%d spells the raw APA namespace (use the pfs0: data home): %s'
                                        % (rel, number, line.strip()))
            if re.search(r'\bfileXioFormat\s*\(', source):
                failures.append('%s calls fileXioFormat; the loader never formats a drive' % rel)


run_fence_harness()
check_driver_policy()
run_bd_harness()
run_range_harness()
run_sector_size_harness()
check_raw_apa_namespace()

if failures:
    print('\nAPA table safety checks FAILED:')
    for failure in failures:
        print(' - ' + failure)
    sys.exit(1)
print('APA table safety: fence and driver policy OK')
