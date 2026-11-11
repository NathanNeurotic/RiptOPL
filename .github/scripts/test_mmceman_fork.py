"""The embedded mmceman is ps2-mmce's commit plus RiptOPL's patches, and the patches do their job.

1. Provenance: reversing patches/ on the vendored mmce_fs.c gives the pinned commit's file, and the
   other vendored files are that commit's files unchanged.
2. Behaviour: the real open/close/dopen/dclose/dread are compiled over a scripted SIO2 wire. The
   card's own status replies must come back as ENOENT (open/dopen/getstat) and end of directory
   (dread), a listed entry must be 1 whatever the card's iterator fd, bus timeouts must stay -1,
   and failed closes must still free their handle slot.
"""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
tree = root / 'modules/mmce/mmceman'
PATCHES = ['mmceman-fs-close-fd-leak.patch', 'mmceman-fs-open-enoent.patch',
           'mmceman-fs-dopen-enoent.patch', 'mmceman-fs-dread-eof.patch',
           'mmceman-fs-dread-getstat-returns.patch']
# sha256 of ps2-mmce/mmceman @ de28936f (mmce_fs.c and mmce_cmds.h are unchanged since db3e93f0)
PINNED = {
    'src/mmce_fs.c': '0f6ffa10a03dcdd3a453ae65b99426e126d30570ebe8cffaaa489896384efdc9',
    'include/mmce_cmds.h': '1163e81f9f4557ef6ee54c35abc6cdc0df9bf8eca7f18fdb759f863a09f00b8a',
}


def sha(data):
    return hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest()


with tempfile.TemporaryDirectory(prefix='mmceman-') as temp:
    work = Path(temp)
    target = work / 'mmceman/src/mmce_fs.c'
    target.parent.mkdir(parents=True)
    target.write_bytes((tree / 'src/mmce_fs.c').read_bytes().replace(b'\r\n', b'\n'))
    for name in reversed(PATCHES):
        patch = work / name
        patch.write_bytes((tree / 'patches' / name).read_bytes().replace(b'\r\n', b'\n'))
        subprocess.run(['git', 'apply', '-R', str(patch)], cwd=work, check=True)
    assert sha(target.read_bytes()) == PINNED['src/mmce_fs.c'], 'mmce_fs.c is not the pin plus patches/'
    assert sha((tree / 'include/mmce_cmds.h').read_bytes()) == PINNED['include/mmce_cmds.h']
print('ok   mmce_fs.c is the pinned file plus the five patches')

driver = (tree / 'src/mmce_fs.c').read_text()


def function(signature):
    start = driver.index(signature)
    return driver[start:driver.index('\n}', start) + 2] + '\n'


cmds = '\n'.join(line for line in (tree / 'include/mmce_cmds.h').read_text().splitlines()
                 if not line.startswith('#include'))
functions = ''.join(function(s) for s in (
    'static int *mmce_fs_find_free_handle(', 'static int mmce_fs_update_unit(', 'int mmce_fs_open(',
    'int mmce_fs_close(', 'int mmce_fs_dopen(', 'int mmce_fs_dclose(', 'int mmce_fs_dread(',
    'int mmce_fs_getstat(',
))

program = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef struct {u32 lo, hi;} iop_sys_clock_t;
typedef struct {u32 mode, attr, size; u8 ctime[8], atime[8], mtime[8]; u32 hisize, priv[6];} iox_stat_t;
typedef struct {iox_stat_t stat; char name[256]; void *unknown;} iox_dirent_t;
typedef struct {int mode; int unit; void *device; void *privdata;} iomanX_iop_file_t;
#define DPRINTF(...) ((void)0)
#define MMCE_FS_MAX_FD 16
@CMDS@
static iop_sys_clock_t timeout_1s;
static int mmce_fs_fds[MMCE_FS_MAX_FD];
static int last_unit = -1, locked;

/* The wire: each transfer takes the next scripted reply. TIMEOUT is a missing /ACK. */
enum {TIMEOUT = -1, ACK = 0, STATUS = 1, FD = 2, STAT_RV = 3};
static struct {int kind, value;} script[16];
static int step, steps;
static void wire(int n, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    for (steps = 0, step = 0; steps < n; steps++) {
        script[steps].kind = __builtin_va_arg(ap, int);
        script[steps].value = __builtin_va_arg(ap, int);
    }
    __builtin_va_end(ap);
}
static void mmce_sio2_lock(void) {assert(!locked); locked = 1;}
static void mmce_sio2_unlock(void) {assert(locked); locked = 0;}
static void mmce_sio2_set_port(int port) {(void)port;}
static int mmce_sio2_tx_rx_pio(u8 tx, u8 rx, const void *out, u8 *in, iop_sys_clock_t *timeout) {
    (void)tx; (void)out; (void)timeout;
    assert(step < steps);
    int kind = script[step].kind, value = script[step].value;
    step++;
    if (kind == TIMEOUT) return -1;
    if (in != NULL) memset(in, 0, rx);
    if (kind == STATUS) {in[1] = MMCE_REPLY_CONST; in[4] = (u8)value;}
    if (kind == ACK && in != NULL) in[1] = MMCE_REPLY_CONST;
    if (kind == FD) in[1] = (u8)value;
    if (kind == STAT_RV) in[0x29] = (u8)value;
    return 0;
}
@FUNCTIONS@
static int freeSlots(void) {int n = 0; for (int i = 0; i < MMCE_FS_MAX_FD; i++) n += mmce_fs_fds[i] < 0; return n;}
static int failures;
static void check(int ok, const char *what) {printf("%s %s\n", ok ? "ok  " : "FAIL", what); failures += !ok;}

int main(void) {
    iomanX_iop_file_t f = {0};
    iox_dirent_t d;
    memset(mmce_fs_fds, -1, sizeof(mmce_fs_fds));

    wire(3, ACK, 0, ACK, 0, FD, 0xff);
    check(mmce_fs_open(&f, "/missing", 1, 0) == -ENOENT && freeSlots() == 16, "open: card says not found -> -ENOENT");
    wire(1, TIMEOUT, 0);
    check(mmce_fs_open(&f, "/x", 1, 0) == -1, "open: bus timeout stays -1");
    mmce_fs_fds[0] = -1;

    wire(3, ACK, 0, ACK, 0, FD, 0xff);
    check(mmce_fs_dopen(&f, "/POPS") == -ENOENT && freeSlots() == 16, "dopen: card says not found -> -ENOENT");

    wire(3, ACK, 0, ACK, 0, FD, 3);
    check(mmce_fs_dopen(&f, "/EMBER") == 0 && freeSlots() == 15, "dopen: folder opens");
    wire(4, STATUS, 0, ACK, 0, ACK, 0, FD, 0);
    check(mmce_fs_dread(&f, &d) == 1 && !locked, "dread: entry whose card iterator fd is 0 -> 1, not end of directory");
    wire(4, STATUS, 0, ACK, 0, ACK, 0, FD, 5);
    check(mmce_fs_dread(&f, &d) == 1 && !locked, "dread: entry -> 1 whatever the iterator fd");
    wire(1, STATUS, 1);
    check(mmce_fs_dread(&f, &d) == 0 && !locked, "dread: card status at end of listing -> 0 (end of directory)");
    wire(1, TIMEOUT, 0);
    check(mmce_fs_dread(&f, &d) == -1 && !locked, "dread: bus timeout stays -1");
    wire(1, TIMEOUT, 0);
    check(mmce_fs_dclose(&f) == -1 && freeSlots() == 16, "dclose: timeout still frees the slot");

    for (int i = 0; i < 20; i++) {
        wire(3, ACK, 0, ACK, 0, FD, 4);
        if (mmce_fs_open(&f, "/art.png", 1, 0) != 0) break;
        wire(1, TIMEOUT, 0);
        mmce_fs_close(&f);
    }
    check(freeSlots() == 16, "close: 20 timed-out closes leave all 16 slots free");

    iox_stat_t st;
    wire(3, ACK, 0, ACK, 0, STAT_RV, 1);
    check(mmce_fs_getstat(&f, "/missing", &st) == -ENOENT && !locked, "getstat: card could not open the path -> -ENOENT");
    wire(3, ACK, 0, ACK, 0, STAT_RV, 0);
    check(mmce_fs_getstat(&f, "/x", &st) == 0 && !locked, "getstat: found -> 0");
    wire(1, TIMEOUT, 0);
    check(mmce_fs_getstat(&f, "/x", &st) == -1 && !locked, "getstat: bus timeout stays -1");
    return failures != 0;
}
'''

with tempfile.TemporaryDirectory(prefix='mmceman-') as temp:
    c = Path(temp) / 'fork.c'
    c.write_text(program.replace('@CMDS@', cmds).replace('@FUNCTIONS@', functions))
    exe = Path(temp) / 'fork'
    cc = shutil.which('gcc') or shutil.which('cc')
    subprocess.run([cc, '-std=gnu99', '-O2', '-Wall', '-Werror', '-Wno-unused-function',
                    '-Wno-pointer-sign', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
print('mmceman fork: card status replies are ENOENT / end of directory, entries are 1, timeouts stay -1, slots are freed')
