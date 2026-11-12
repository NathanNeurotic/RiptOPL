#!/usr/bin/env python3
"""A VMC page past 2 TiB on the internal drive reaches the right sector.

Compiles the in-game path of a VMC write on the internal drive: mcemu's
DeviceWritePage/DeviceWritePages/DeviceReadPage (modules/mcemu/device-bdm.c),
cdvdman's bdm_writeSector/bdm_readSector (modules/iopcore/cdvdman/device-bdm.c)
and the core's ATA driver (ata_bd_write/ata_bd_read, ata_device_sector_io_internal
and sceAtaExecCmd in modules/iopcore/cdvdman/atad.c). Register stores go to a
mock drive that latches them as an ATA device does: a second write to an
address register keeps the first byte as bits 24-47. The drive decodes the
sector it was sent.

48-bit commands: pages at 2^32 - 1, 2^32, every address byte different, the end
of an 8 TB drive and the last 48-bit sector each reach exactly that sector.
28-bit commands (48-bit support not confirmed): 2^28 - 1 is reached and 2^28
wraps to sector 0, the limit bdmNativeVmcLimit (src/sources/bdmlaunch.cpp)
applies at launch. Host only: no DMA, interrupts or real drive.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


atad = (ROOT / "modules/iopcore/cdvdman/atad.c").read_text(encoding="utf-8")
cdvdman = (ROOT / "modules/iopcore/cdvdman/device-bdm.c").read_text(encoding="utf-8")
mcemu = (ROOT / "modules/mcemu/device-bdm.c").read_text(encoding="utf-8")
mcemu_utils = (ROOT / "modules/mcemu/mcemu_utils.h").read_text(encoding="utf-8")
launch = (ROOT / "src/sources/bdmlaunch.cpp").read_text(encoding="utf-8")

# mcemu reaches cdvdman through an import: both sides must declare the same 64-bit sector argument.
for name in ("bdm_readSector", "bdm_writeSector"):
    imported = re.search(r"\nint %s\(([^)]*)\);" % name, mcemu_utils).group(1)
    exported = re.search(r"\nint %s\(([^)]*)\)\n\{" % name, cdvdman).group(1)
    assert imported == exported and imported.startswith("u64 lba,"), (name, imported, exported)

# The launch refuses a VMC from the first sector 28-bit commands cannot reach (checked below).
limit = function(launch, "static u64 bdmNativeVmcLimit(")
assert "1ULL << 28" in limit, limit

state_end = "static ata_cmd_state_t atad_cmd_state;"
ata = "\n".join([
    atad[atad.index("/* ATA command info.  */"):atad.index(state_end) + len(state_end)],
    function(atad, "int sceAtaExecCmd(void *buf,"),
    function(atad, "int ata_device_sector_io_internal(int device, void *buf, u64 lba, u32 nsectors, int dir)\n{"),
    function(atad, "static int ata_bd_io_common(struct block_device *bd, u64 lba, void *buf, u16 nsectors, int dir)\n{"),
    function(atad, "static int ata_bd_read(struct block_device *bd, u64 sector, void *buffer, u16 count)\n{"),
    function(atad, "static int ata_bd_write(struct block_device *bd, u64 sector, const void *buffer, u16 count)\n{"),
])
# Every task-file store goes to the mock drive, in order: control, five previous bytes, five current
# bytes, device/select and command.
ata, stores = re.subn(r"ata_hwport->(r_\w+) = ([^;]+);", r"drive_store(DRIVE_\1, \2);", ata)
assert stores == 13, stores

glue = "\n".join([
    function(cdvdman, "int bdm_readSector(u64 lba,"),
    function(cdvdman, "int bdm_writeSector(u64 lba,"),
    function(mcemu, "int DeviceWritePage("),
    function(mcemu, "int DeviceWritePages("),
    function(mcemu, "int DeviceReadPage("),
])

HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
#define DPRINTF(...) ((void)0)
#define M_PRINTF(...) ((void)0)

/* ATA command codes and the LBA select bit, as in ps2sdk's atahw.h. */
#define ATA_C_DEVICE_RESET 0x08
#define ATA_C_READ_DMA_EXT 0x25
#define ATA_C_WRITE_DMA_EXT 0x35
#define ATA_C_EXECUTE_DEVICE_DIAGNOSTIC 0x90
#define ATA_C_INITIALIZE_DEVICE_PARAMETERS 0x91
#define ATA_C_PACKET 0xa0
#define ATA_C_IDENTIFY_PACKET_DEVICE 0xa1
#define ATA_C_SMART 0xb0
#define ATA_C_READ_DMA 0xc8
#define ATA_C_WRITE_DMA 0xca
#define ATA_C_STANDBY_IMMEDIATE 0xe0
#define ATA_C_IDLE 0xe3
#define ATA_C_FLUSH_CACHE 0xe7
#define ATA_C_FLUSH_CACHE_EXT 0xea
#define ATA_C_IDENTIFY_DEVICE 0xec
#define ATA_C_SET_FEATURES 0xef
#define ATA_S_SMART_ENABLE_OPERATIONS 0xd8
#define ATA_SEL_LBA 0x40
#define ATA_RES_ERR_NOTREADY -501
#define ATA_RES_ERR_CMD -503
#define ATA_RES_ERR_ICRC -510
#define SPD_R_IF_CTRL 0x64
#define SPD_IF_DMA_ENABLE 0x04
#define SPD_INTR_ATA0 0x01

/* The mock drive. Writing a register moves its byte to "previous", as an ATA device keeps it. */
enum { DRIVE_r_feature, DRIVE_r_nsector, DRIVE_r_sector, DRIVE_r_lcyl, DRIVE_r_hcyl, DRIVE_r_select, DRIVE_r_command, DRIVE_r_control, DRIVE_REGS };
static struct { u8 current[DRIVE_REGS], previous[DRIVE_REGS]; } drive;
static struct { unsigned int commands; unsigned int command; u64 lba; u32 count; int lba_mode; } issued;

static void drive_store(int reg, unsigned int value)
{
    drive.previous[reg] = drive.current[reg];
    drive.current[reg] = (u8)value;
    if (reg != DRIVE_r_command)
        return;
    issued.commands++;
    issued.command = value & 0xff;
    issued.lba_mode = (drive.current[DRIVE_r_select] & ATA_SEL_LBA) != 0;
    if (issued.command == ATA_C_READ_DMA_EXT || issued.command == ATA_C_WRITE_DMA_EXT) {
        issued.lba = (u64)drive.previous[DRIVE_r_hcyl] << 40 | (u64)drive.previous[DRIVE_r_lcyl] << 32 |
                     (u64)drive.previous[DRIVE_r_sector] << 24 | (u64)drive.current[DRIVE_r_hcyl] << 16 |
                     (u64)drive.current[DRIVE_r_lcyl] << 8 | drive.current[DRIVE_r_sector];
        issued.count = (u32)drive.previous[DRIVE_r_nsector] << 8 | drive.current[DRIVE_r_nsector];
        if (issued.count == 0)
            issued.count = 65536;
    } else {
        /* 28-bit: bits 24-27 are the low nibble of the device register. */
        issued.lba = (u64)(drive.current[DRIVE_r_select] & 0x0f) << 24 | (u64)drive.current[DRIVE_r_hcyl] << 16 |
                     (u64)drive.current[DRIVE_r_lcyl] << 8 | drive.current[DRIVE_r_sector];
        issued.count = drive.current[DRIVE_r_nsector] ? drive.current[DRIVE_r_nsector] : 256;
    }
}

struct drive_status { u8 r_control; };
static struct drive_status drive_status = {0x40}; /* DRDY: ready for commands */
#define USE_ATA_REGS struct drive_status *ata_hwport = &drive_status
#define USE_SPD_REGS ((void)0)
static u16 spd_regs[0x100];
#define SPD_REG16(offset) spd_regs[(offset) & 0xff]

char lba_48bit;
static u8 ata_gamestar_workaround;
static int ata_evflg = 1;
int ata_io_sema = 2;
static int bdm_io_sema = 3;
static int held[4];
static void WaitSema(int sema) { assert(!held[sema]); held[sema] = 1; }
static void SignalSema(int sema) { assert(held[sema]); held[sema] = 0; }
#define WAITIOSEMA(x) WaitSema(x)
#define SIGNALIOSEMA(x) SignalSema(x)
static void ClearEventFlag(int flag, u32 bits) { (void)flag; (void)bits; }
static int ata_device_select(int device) { return device == 0 ? 0 : -1; }
typedef struct { u32 lo, hi; } iop_sys_clock_t;
static unsigned int ata_alarm_cb(void *unused) { (void)unused; return 0; }
static int SetAlarm(iop_sys_clock_t *clock, unsigned int (*cb)(void *), void *arg) { (void)clock; (void)cb; (void)arg; return 0; }
static void SpdIntrEnable(int mask) { (void)mask; }
static void SpdSetLED(int on) { (void)on; }
static int sceAtaWaitResult(void) { return 0; }
static void ata_set_dir(int dir) { (void)dir; }
static void cdvdman_vmc_io_begin(int write) { (void)write; }
static void cdvdman_vmc_io_end(void) {}

struct block_device
{
    int devNr;
    int (*read)(struct block_device *bd, u64 sector, void *buffer, u16 count);
    int (*write)(struct block_device *bd, u64 sector, const void *buffer, u16 count);
};
static struct block_device *g_bd;
static struct { int active; u64 stsec; } vmcSpec[2];
'''

MAIN = r'''
static unsigned int seen;

static void expect(const char *what, u64 lba, u32 count, unsigned int command)
{
    if (issued.commands != seen + 1 || issued.lba != lba || issued.count != count || issued.command != command || !issued.lba_mode) {
        fprintf(stderr, "%s: drive got %u command(s), last 0x%02x for 0x%llx x%u (LBA bit %d); expected 0x%02x for 0x%llx x%u\n",
                what, issued.commands - seen, issued.command, (unsigned long long)issued.lba, issued.count, issued.lba_mode,
                command, (unsigned long long)lba, count);
        exit(1);
    }
    seen = issued.commands;
}

int main(void)
{
    static struct block_device ata = {0, ata_bd_read, ata_bd_write};
    static u8 page[512], block[16 * 512];

    g_bd = &ata;
    lba_48bit = 1;

    /* A card whose first sector is the last one below 2 TiB: its second page is sector 2^32. */
    vmcSpec[0].stsec = 0xFFFFFFFFULL;
    assert(DeviceWritePage(0, page, 0) == 1);
    expect("write below 2 TiB", 0xFFFFFFFFULL, 1, ATA_C_WRITE_DMA_EXT);
    assert(DeviceWritePage(0, page, 1) == 1);
    expect("write at 2 TiB", 0x100000000ULL, 1, ATA_C_WRITE_DMA_EXT);
    assert(DeviceReadPage(0, page, 1) == 1);
    expect("read at 2 TiB", 0x100000000ULL, 1, ATA_C_READ_DMA_EXT);

    /* Slot 2, every address byte different, a 16-page erase block in one command. */
    vmcSpec[1].stsec = 0xABCDEF123456ULL;
    assert(DeviceWritePages(1, block, 7, 16) == 1);
    expect("block write, all bytes", 0xABCDEF12345DULL, 16, ATA_C_WRITE_DMA_EXT);

    /* An 8 TB drive (15,628,053,168 sectors) with an 8 MB card ending on its last sector. */
    vmcSpec[0].stsec = 15628053168ULL - 16384;
    assert(DeviceWritePage(0, page, 16383) == 1);
    expect("last page of an 8 TB drive", 15628053167ULL, 1, ATA_C_WRITE_DMA_EXT);

    /* The largest 48-bit sector. */
    vmcSpec[0].stsec = (1ULL << 48) - 1;
    assert(DeviceReadPage(0, page, 0) == 1);
    expect("last 48-bit sector", (1ULL << 48) - 1, 1, ATA_C_READ_DMA_EXT);

    /* 48-bit support not confirmed: 28-bit commands reach 2^28 - 1, and 2^28 lands on sector 0. */
    lba_48bit = 0;
    vmcSpec[0].stsec = (1ULL << 28) - 1;
    assert(DeviceWritePage(0, page, 0) == 1);
    expect("28-bit, last reachable sector", (1ULL << 28) - 1, 1, ATA_C_WRITE_DMA);
    assert(DeviceWritePage(0, page, 1) == 1);
    expect("28-bit wraps at 2^28", 0, 1, ATA_C_WRITE_DMA);

    assert(!held[ata_io_sema] && !held[bdm_io_sema]);
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / "vmc_past_2tb_ata.c"
    exe = Path(tmp) / "vmc_past_2tb_ata"
    source.write_text(HARNESS + ata + "\n" + glue + "\n" + MAIN, encoding="utf-8")
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(source)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)

print("VMC past 2 TiB, internal drive: 48-bit sectors exact to 2^48 - 1; 28-bit wraps at 2^28, where the launch stops")
