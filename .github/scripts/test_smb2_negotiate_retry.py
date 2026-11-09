"""Verify in-game SMB2 negotiate bounds, 10s attempt deadline, fast-fail, and SMB 3.0/3.0.2 fallback."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
cdvdman = root / 'modules/iopcore/cdvdman'

TAMTYPES_H = """#ifndef _TAMTYPES_H_
#define _TAMTYPES_H_
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
#endif
"""

STUB_H = """/* stub header */
#ifndef _STUB_IOP_H_
#define _STUB_IOP_H_
#include <tamtypes.h>
#include <string.h>
#define DECLARE_IMPORT_TABLE(m, a, b)
#define END_IMPORT_TABLE
#define DECLARE_IMPORT(n, f)
typedef int socklen_t;
struct in_addr { u32 s_addr; };
struct sockaddr { u16 sa_family; char sa_data[14]; };
struct sockaddr_in { u16 sin_family; u16 sin_port; struct in_addr sin_addr; char sin_zero[8]; };
#define AF_INET 2
#define PF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
#define TCP_NODELAY 1
static inline u16 htons(u16 v) { return (u16)((v << 8) | (v >> 8)); }
typedef struct { u32 lo, hi; } iop_sys_clock_t;
typedef struct { u32 attr, option; int initial, max; } iop_sema_t;
typedef struct { u64 sector; u32 count; } bd_fragment_t;
static inline void mips_memcpy(void *d, const void *s, int n) { memcpy(d, s, (size_t)n); }
static inline void mips_memset(void *d, int c, int n) { memset(d, c, (size_t)n); }
#endif
"""

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <tamtypes.h>
#include "smstcpip.h"

static u64 fake_ticks;
static int delays, connects, closes, sends, sema_alive;
static int mode;

static int DelayThread(int us) { (void)us; delays++; return 0; }
static void GetSystemTime(iop_sys_clock_t *c) { c->lo = (u32)fake_ticks; c->hi = (u32)(fake_ticks >> 32); }
static int CreateSema(iop_sema_t *s) { (void)s; sema_alive++; return 42; }
static int WaitSema(int s) { (void)s; return 0; }
static int SignalSema(int s) { (void)s; return 0; }
static int DeleteSema(int s) { if (s == 42) sema_alive--; return 0; }

#include "oplsmb.h"
#include "smb.h"
#include "cdvd_config.h"

struct cdvdman_settings_smb cdvdman_settings;
static u8 rx_buf[512];
static int rx_len, rx_pos;
static u16 last_d0, last_d1;

static int h_socket(int d, int t, int p) { (void)d; (void)t; (void)p; return 7; }
static int h_setsockopt(int s, int l, int o, const void *v, socklen_t n) { (void)s; (void)l; (void)o; (void)v; (void)n; return 0; }
static u32 h_inet(const char *cp) { (void)cp; return 0x0100007fU; }
static int h_close(int s) { if (s == 7) closes++; return 0; }

static void build_reply(const u8 *req, u32 status, u16 dialect, u16 sec_mode)
{
    int body_len = (status == 0) ? 65 : 9;
    int total = 64 + body_len;
    memset(rx_buf, 0, sizeof(rx_buf));
    rx_buf[1] = (u8)(total >> 16);
    rx_buf[2] = (u8)(total >> 8);
    rx_buf[3] = (u8)total;
    memcpy(rx_buf + 4, "\xfeSMB", 4);
    rx_buf[4 + 4] = 64;
    rx_buf[4 + 8] = (u8)status;
    rx_buf[4 + 9] = (u8)(status >> 8);
    rx_buf[4 + 10] = (u8)(status >> 16);
    rx_buf[4 + 11] = (u8)(status >> 24);
    rx_buf[4 + 16] = 1;
    memcpy(rx_buf + 4 + 24, req + 4 + 24, 8);
    if (status == 0) {
        rx_buf[4 + 64] = 65;
        rx_buf[4 + 66] = (u8)sec_mode;
        rx_buf[4 + 68] = (u8)dialect;
        rx_buf[4 + 69] = (u8)(dialect >> 8);
    } else {
        rx_buf[4 + 64] = 9;
    }
    rx_len = 4 + total;
    rx_pos = 0;
}

static int h_connect(int s, struct sockaddr *a, socklen_t l)
{
    (void)s; (void)a; (void)l;
    connects++;
    return (mode == 0) ? -1 : 0;
}

static int h_send(int s, void *buf, int len, unsigned int f)
{
    const u8 *p = (const u8 *)buf;
    (void)s; (void)f;
    sends++;
    last_d0 = (u16)(p[4 + 64 + 36] | (p[4 + 64 + 37] << 8));
    last_d1 = (u16)(p[4 + 64 + 38] | (p[4 + 64 + 39] << 8));
    if (mode == 1) {
        rx_buf[0] = 0x85; rx_buf[1] = 0; rx_buf[2] = 0; rx_buf[3] = 0;
        rx_len = 4; rx_pos = 0;
    } else if (mode == 2) {
        build_reply(p, 0xC0000022U, 0, 0);
    } else if (mode == 3) {
        if (last_d0 == 0x0202)
            build_reply(p, 0xC00000BBU, 0, 0);
        else
            build_reply(p, 0, 0x0302, 0x0001);
    } else if (mode == 4) {
        if (last_d0 == 0x0202)
            build_reply(p, 0xC00000BBU, 0, 0);
        else
            build_reply(p, 0, 0x0302, 0x0003);
    }
    return len;
}

static int h_recv(int s, void *buf, int len, unsigned int f)
{
    int n;
    (void)s; (void)f;
    if (mode == 1) {
        fake_ticks += 150000000ULL;
        rx_pos = 0;
    }
    if (rx_pos >= rx_len)
        return -1;
    n = (rx_len - rx_pos < len) ? (rx_len - rx_pos) : len;
    memcpy(buf, rx_buf + rx_pos, n);
    rx_pos += n;
    return n;
}

int (*plwip_close)(int) = h_close;
int (*plwip_connect)(int, struct sockaddr *, socklen_t) = h_connect;
int (*plwip_recv)(int, void *, int, unsigned int) = h_recv;
int (*plwip_send)(int, void *, int, unsigned int) = h_send;
int (*plwip_socket)(int, int, int) = h_socket;
int (*plwip_setsockopt)(int, int, int, const void *, socklen_t) = h_setsockopt;
u32 (*pinet_addr)(const char *) = h_inet;

#include "smb2.c"

static void reset_state(int m, u64 start_ticks)
{
    mode = m;
    fake_ticks = start_ticks;
    delays = connects = closes = sends = sema_alive = 0;
    rx_len = rx_pos = 0;
}

int main(void)
{
    u32 caps = 0;
    int r;

    /* 0: transient connect failure retries exactly 3 times and frees sema */
    reset_state(0, 100);
    r = smb_NegotiateProtocol("127.0.0.1", 445, "u", "p", &caps, NULL);
    if (r != -1 || connects != 3 || delays != 2 || sema_alive != 0 || smb_io_sema != -1)
        return 10;

    /* 1: endless keepalives hit the 10-second per-attempt deadline across u32 clock wrap */
    reset_state(1, 0xFFFFFF00ULL);
    r = smb_NegotiateProtocol("127.0.0.1", 445, "u", "p", &caps, NULL);
    if (r != -1 || connects != 3 || closes != 3 || sema_alive != 0)
        return 11;

    /* 2: unusable negotiate status fails fast (-1000) on the first attempt */
    reset_state(2, 100);
    r = smb_NegotiateProtocol("127.0.0.1", 445, "u", "p", &caps, NULL);
    if (r != -1000 || connects != 1 || sends != 1 || sema_alive != 0)
        return 12;

    /* 3: STATUS_NOT_SUPPORTED on 0x0202/0x0210 reconnects and succeeds with unsigned 0x0300/0x0302 */
    reset_state(3, 100);
    r = smb_NegotiateProtocol("127.0.0.1", 445, "u", "p", &caps, NULL);
    if (r != 1 || connects != 2 || sends != 2 || last_d0 != 0x0300 || last_d1 != 0x0302 || dialect != 0x0302 || sema_alive != 1)
        return 13;
    smb_Disconnect();
    DeleteSema(smb_io_sema);
    smb_io_sema = -1;

    /* 4: SMB 3.x with mandatory signing fails fast (-1000) without retrying */
    reset_state(4, 100);
    r = smb_NegotiateProtocol("127.0.0.1", 445, "u", "p", &caps, NULL);
    if (r != -1000 || connects != 2 || sends != 2 || sema_alive != 0)
        return 14;

    return 0;
}
"""

cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
if not cc:
    sys.exit('No host C compiler found')

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'tamtypes.h').write_text(TAMTYPES_H, encoding='utf-8')
    for h in ('sysclib.h', 'smstcpip.h', 'thbase.h', 'thsemap.h', 'smsutils.h', 'irx.h', 'usbhdfsd-common.h'):
        (tmp / h).write_text(STUB_H, encoding='utf-8')
    (tmp / 'harness.c').write_text(HARNESS, encoding='utf-8')
    exe = tmp / ('harness.exe' if sys.platform == 'win32' else 'harness')
    cmd = [cc, '-O1', '-Wall', '-Wno-unused-function', '-I', str(tmp),
           '-I', str(cdvdman), '-I', str(root / 'modules/iopcore/common'), str(tmp / 'harness.c'),
           str(cdvdman / 'smb2auth.c'), '-o', str(exe)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout + p.stderr)
        sys.exit(1)
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    if r.returncode != 0:
        print(f'Harness failed with exit code {r.returncode}')
        sys.exit(1)

print('in-game SMB2 negotiate: 3-attempt bound, 10s deadline, -1000 fast-fail, and SMB 3.0/3.0.2 fallback verified')
