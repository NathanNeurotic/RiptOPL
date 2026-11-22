"""Pin failure-only ATA/partition read diagnostics without changing read results."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def function(source, signature):
    match = re.search(r'^' + re.escape(signature) + r'[^;{]*\)\s*\{', source, re.M)
    if match is None:
        raise ValueError(f'definition not found: {signature}')
    start = match.start()
    return source[start:source.index('\n}', start) + 2]


atad = (root / 'modules/hdd/atad/src/ps2atad.c').read_text(encoding='utf-8')
part = (root / 'modules/bdm/src/part_driver.c').read_text(encoding='utf-8')
program = r'''
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define EIO 5
#define ATA_DIR_READ 0

struct block_device {
    void *priv;
    char *name;
    unsigned int devNr;
    unsigned int parNr;
    unsigned char parId;
    unsigned int sectorSize;
    u64 sectorOffset;
    u64 sectorCount;
    int (*read)(struct block_device *bd, u64 sector, void *buffer, u16 count);
};
struct partition { struct block_device *bd; };

static char logText[2048];
static int logLength;
static int __attribute__((unused)) test_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int written = vsnprintf(logText + logLength, sizeof(logText) - logLength, format, args);
    va_end(args);
    if (written > 0)
        logLength += written;
    return written;
}
#define M_PRINTF(format, ...) test_printf(format, ##__VA_ARGS__)
#define M_DEBUG(...) ((void)0)

static int ataResult;
static int ata_device_sector_io64(int device, void *buf, u64 lba, u32 nsectors, int dir)
{
    (void)device; (void)buf; (void)lba; (void)nsectors; (void)dir;
    return ataResult;
}

@ATA_READ@

static int rawResult;
static int raw_read(struct block_device *bd, u64 sector, void *buffer, u16 count)
{
    (void)bd; (void)sector; (void)buffer; (void)count;
    return rawResult;
}

@PART_READ@

static int failures;
#define CHECK(condition, message) do { if (!(condition)) { printf("FAIL: %s\n", message); failures = 1; } } while (0)

int main(void)
{
    unsigned char buffer[16 * 512];
    struct block_device ata = {0};
    ata.devNr = 0;
    ataResult = -17;
    CHECK(ata_bd_read(&ata, 0x123456789ULL, buffer, 8) == -EIO, "ATA error result changed");
    CHECK(strstr(logText, "-17") != NULL, "ATA driver result missing");
    CHECK(strstr(logText, "12345678") != NULL || strstr(logText, "00000001") != NULL,
          "ATA failing LBA missing");
    CHECK(strstr(logText, "8") != NULL, "ATA sector count missing");

    logLength = 0;
    logText[0] = '\0';
    ataResult = 0;
    CHECK(ata_bd_read(&ata, 0x123456789ULL, buffer, 8) == 8, "ATA success result changed");
    CHECK(logLength == 0, "successful ATA read logged an error");

    logLength = 0;
    logText[0] = '\0';
    struct block_device raw = {0};
    raw.read = raw_read;
    struct partition owner = {&raw};
    struct block_device partition = {0};
    partition.priv = &owner;
    partition.sectorCount = 40;
    CHECK(part_read(&partition, 39, buffer, 2) == -1, "partition bounds result changed");
    CHECK(strstr(logText, "00000027") != NULL, "partition-relative LBA missing");
    CHECK(strstr(logText, "00000028") != NULL, "partition sector count missing");
    CHECK(strstr(logText, "2") != NULL, "partition read length missing");

    logLength = 0;
    logText[0] = '\0';
    rawResult = 1;
    CHECK(part_read(&partition, 39, buffer, 1) == 1, "partition success result changed");
    CHECK(logLength == 0, "successful partition read logged an error");

    if (!failures)
        puts("ATA read diagnostics: failures identify layer/LBA/count and successes stay quiet");
    return failures;
}
'''
program = program.replace('@ATA_READ@', function(atad, 'static int ata_bd_read('))
program = program.replace('@PART_READ@', function(part, 'static int part_read('))
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'ata_read_diagnostics.c'
    exe = Path(tmp) / 'ata_read_diagnostics'
    source.write_text(program, encoding='utf-8')
    build = subprocess.run(['cc', '-std=gnu99', '-Wall', '-Werror', str(source), '-o', str(exe)],
                           capture_output=True, text=True)
    if build.returncode:
        raise SystemExit(build.stderr)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end='')
    print(run.stderr, end='')
    raise SystemExit(run.returncode)
