#!/usr/bin/env python3
"""A slow SMB1 server costs one late reply, not the session.

The in-game SMB1 reader (modules/iopcore/cdvdman/smb.c) gives each reply 10 seconds. A reply that
misses that deadline used to close the session, so every later read and VMC write failed until the
next launch, and a lost packet during negotiation put the launch on the red screen. Now a missed
deadline keeps the session: the next request first takes the rest of the late reply off the socket
(smb_Resync), and a request whose reply timed out is sent again, up to three times. Negotiation opens
a fresh socket for each of up to four attempts.

This compiles the whole of smb.c on the host against a scripted server that answers NEGOTIATE,
READ_ANDX and WRITE_ANDX over a fake SMSTCPIP socket (its recvfrom() header/payload split included),
with a virtual clock, and checks:
  - a read and a VMC page write whose first reply comes after the deadline still succeed, on the same
    session, and the late reply is not taken for the next one;
  - a late reply that arrives in pieces, after a keep-alive, and times out again part-way through, is
    still taken off whole;
  - a server that never answers fails each request within a bounded time (a read after its three
    10 s deadlines) and the session is closed after three drains in a row that get nothing;
  - negotiation that gets no reply on its first attempt succeeds on a fresh socket, and gives up
    after four attempts.
No console network or PS2 toolchain is needed; hardware validation is a separate step.
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SMB_C = ROOT / 'modules/iopcore/cdvdman/smb.c'
CC = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')

HEADERS = {
    'tamtypes.h': r'''
#pragma once
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
''',
    'usbhdfsd-common.h': '#pragma once\ntypedef struct { unsigned long long sector; unsigned int count; } bd_fragment_t;\n',
    'sysclib.h': '#pragma once\n#include <string.h>\n#include <stdio.h>\n',
    'intrman.h': '#pragma once\n',
    'sifman.h': '#pragma once\n',
    'smsutils.h': '#pragma once\n',
    'smstcpip.h': r'''
#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
''',
    'thbase.h': r'''
#pragma once
#include <tamtypes.h>
typedef struct { u32 lo, hi; } iop_sys_clock_t;
void GetSystemTime(iop_sys_clock_t *clock);
int DelayThread(int usec);
''',
    'thsemap.h': r'''
#pragma once
typedef struct { int attr, option, initial, max; } iop_sema_t;
int CreateSema(iop_sema_t *sema);
int DeleteSema(int sema);
int WaitSema(int sema);
int SignalSema(int sema);
''',
}

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tamtypes.h>
#include <thbase.h>
#include <thsemap.h>
#include <smstcpip.h>
#include "oplsmb.h"
#include "smb.h"
#include "cdvd_config.h"

struct cdvdman_settings_smb cdvdman_settings;

int smb_Disconnect(void);
int OpenTCPSession(struct in_addr dst_IP, u16 dst_port);
extern int smb_io_sema;

/* Virtual clock: DelayThread() is the only thing that moves it. */
static unsigned long long now_us;
void GetSystemTime(iop_sys_clock_t *clock)
{
    unsigned long long ticks = now_us * 36864ULL / 1000ULL;
    clock->lo = (u32)ticks;
    clock->hi = (u32)(ticks >> 32);
}
int DelayThread(int usec)
{
    now_us += (unsigned long long)usec;
    return 0;
}
static int sema_held;
int CreateSema(iop_sema_t *sema) { (void)sema; return 1; }
int DeleteSema(int sema) { (void)sema; return 0; }
int WaitSema(int sema) { (void)sema; assert(!sema_held); sema_held = 1; return 0; }
int SignalSema(int sema) { (void)sema; assert(sema_held); sema_held = 0; return 0; }

/* The server. Each request it answers is queued as a reply that becomes readable at a set time,
   cut into TCP segments of seg_size bytes, optionally after an NBSS keep-alive. */
#define NEVER (-1LL)
static long long delay_us[16]; /* by request number: when its reply comes, or NEVER */
static int requests, seg_size = 1460, keepalive_first, sockets, closes, live_socket = -1;
static int read_requests, write_requests, negotiate_requests;
static u8 file[1 << 20];
static u8 wire[1 << 18];
static int wire_len, wire_pos;
static struct { int end; unsigned long long at; } segs[256];
static int nsegs, seg_pos;
static u8 request[1 << 17];
static int request_len;

static void queue(const u8 *bytes, int len, unsigned long long at)
{
    int off = 0;
    while (off < len) {
        int n = len - off < seg_size ? len - off : seg_size;
        memcpy(&wire[wire_len], bytes + off, n);
        wire_len += n;
        off += n;
        segs[nsegs].end = wire_len;
        segs[nsegs].at = at;
        nsegs++;
    }
}

static void put_nbss(u8 *out, u8 type, int len)
{
    out[0] = type;
    out[1] = (u8)(len >> 16);
    out[2] = (u8)(len >> 8);
    out[3] = (u8)len;
}

static void answer(void)
{
    static u8 reply[1 << 17];
    SMBHeader_t *rh = (SMBHeader_t *)&request[4];
    long long delay = delay_us[requests < 16 ? requests : 15];
    int len = 0;
    requests++;
    memset(reply, 0, sizeof(reply));
    if (rh->Cmd == SMB_COM_NEGOTIATE) {
        NegotiateProtocolResponse_t *r = (NegotiateProtocolResponse_t *)&reply[4];
        negotiate_requests++;
        r->smbH.Magic = SMB_MAGIC;
        r->smbH.Cmd = SMB_COM_NEGOTIATE;
        r->smbWordcount = 17;
        r->SecurityMode = 0; /* share level, no password */
        r->MaxMpxCount = 2;
        r->KeyLength = 8;
        r->ByteCount = 8 + 3;
        memcpy(&r->ByteField[8], "WG", 3);
        len = sizeof(NegotiateProtocolResponse_t) + 8 + 3;
    } else if (rh->Cmd == SMB_COM_READ_ANDX) {
        ReadAndXRequest_t *q = (ReadAndXRequest_t *)&request[4];
        ReadAndXResponse_t *r = (ReadAndXResponse_t *)&reply[4];
        int n = q->MaxCountLow | (q->MaxCountHigh << 16);
        read_requests++;
        r->smbH.Magic = SMB_MAGIC;
        r->smbH.Cmd = SMB_COM_READ_ANDX;
        r->smbWordcount = 12;
        r->DataOffset = sizeof(ReadAndXResponse_t);
        r->DataLengthLow = (u16)n;
        r->DataLengthHigh = (u16)(n >> 16);
        memcpy(&reply[4 + sizeof(ReadAndXResponse_t)], &file[q->OffsetLow], n);
        len = sizeof(ReadAndXResponse_t) + n;
    } else if (rh->Cmd == SMB_COM_WRITE_ANDX) {
        WriteAndXRequest_t *q = (WriteAndXRequest_t *)&request[4];
        WriteAndXResponse_t *r = (WriteAndXResponse_t *)&reply[4];
        int n = q->DataLengthLow | (q->DataLengthHigh << 16);
        write_requests++;
        memcpy(&file[q->OffsetLow], &request[4 + q->DataOffset], n);
        r->smbH.Magic = SMB_MAGIC;
        r->smbH.Cmd = SMB_COM_WRITE_ANDX;
        r->smbWordcount = 6;
        r->Count = (u16)n;
        len = sizeof(WriteAndXResponse_t);
    } else {
        printf("FAIL: unexpected command 0x%02x\n", rh->Cmd);
        exit(1);
    }
    if (delay == NEVER)
        return;
    if (keepalive_first) {
        u8 ka[4];
        put_nbss(ka, 0x85, 0);
        queue(ka, 4, now_us + (unsigned long long)delay);
    }
    put_nbss(reply, 0, len);
    queue(reply, len + 4, now_us + (unsigned long long)delay);
}

static int fake_send(int s, void *data, int size, unsigned int flags)
{
    (void)flags;
    assert(s == live_socket);
    memcpy(&request[request_len], data, size);
    request_len += size;
    if (request_len >= 4) {
        int want = 4 + ((request[1] << 16) | (request[2] << 8) | request[3]);
        assert(request_len <= want);
        if (request_len == want) {
            answer();
            request_len = 0;
        }
    }
    return size;
}

/* SMSTCPIP lwip_recvfrom() (modules/network/SMSTCPIP/sockets.c) over the queued segments. */
static int fake_recvfrom(int s, void *header, int index, void *payload, int plen, unsigned int flags, struct sockaddr *from, socklen_t *fromlen)
{
    int avail, copylen = 0;
    (void)from; (void)fromlen;
    assert(flags == 0x40 && s == live_socket);
    if (seg_pos >= nsegs || segs[seg_pos].at > now_us)
        return -1; /* EWOULDBLOCK */
    avail = segs[seg_pos].end - wire_pos;
    if (index) {
        if (avail >= 63) {
            memcpy(header, &wire[wire_pos], 63);
            index = ((u8 *)header)[index] + 4;
        }
        if (avail < 63 || index > avail) {
            memcpy(header, &wire[wire_pos], avail);
            copylen = avail;
            avail = 0;
        } else {
            copylen = index;
            avail -= index;
        }
    }
    if (avail) {
        if (plen > avail)
            plen = avail;
        memcpy(payload, &wire[wire_pos + index], plen);
        copylen += plen;
    }
    wire_pos += copylen;
    if (wire_pos == segs[seg_pos].end)
        seg_pos++;
    return copylen;
}

static void new_connection(void)
{
    wire_len = wire_pos = nsegs = seg_pos = request_len = 0;
}
static int fake_socket(int d, int t, int p) { (void)d; (void)t; (void)p; new_connection(); live_socket = 10 + ++sockets; return live_socket; }
static int fake_setsockopt(int s, int l, int o, const void *v, socklen_t n) { (void)s; (void)l; (void)o; (void)v; (void)n; return 0; }
static int fake_connect(int s, struct sockaddr *a, socklen_t n) { (void)a; (void)n; assert(s == live_socket); return 0; }
static int fake_close(int s) { assert(s == live_socket); live_socket = -1; closes++; return 0; }
static u32 fake_inet_addr(const char *cp) { (void)cp; return 0x0164a8c0; }

int (*plwip_close)(int s) = fake_close;
int (*plwip_connect)(int s, struct sockaddr *name, socklen_t namelen) = fake_connect;
int (*plwip_recv)(int s, void *mem, int len, unsigned int flags);
int (*plwip_recvfrom)(int s, void *mem, int hlen, void *payload, int plen, unsigned int flags, struct sockaddr *from, socklen_t *fromlen) = fake_recvfrom;
int (*plwip_send)(int s, void *dataptr, int size, unsigned int flags) = fake_send;
int (*plwip_socket)(int domain, int type, int protocol) = fake_socket;
int (*plwip_setsockopt)(int s, int level, int optname, const void *optval, socklen_t optlen) = fake_setsockopt;
u32 (*pinet_addr)(const char *cp) = fake_inet_addr;

static void hash(server_specs_t *ss) { (void)ss; }
static u32 caps;

static void reset(void)
{
    int i;
    smb_Disconnect();
    now_us = 0;
    requests = read_requests = write_requests = negotiate_requests = sockets = closes = 0;
    keepalive_first = 0;
    seg_size = 1460;
    for (i = 0; i < 16; i++)
        delay_us[i] = 0;
    for (i = 0; i < (int)sizeof(file); i++)
        file[i] = (u8)(i * 7 + (i >> 11));
}

static int connected(void)
{
    reset();
    if (smb_NegotiateProtocol("192.168.1.100", 445, "", "", &caps, hash) != 1)
        return 0;
    requests = 0;
    closes = 0;
    return 1;
}

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)

int main(void)
{
    static u8 buf[8192], page[512];
    unsigned long long t0;
    int r, i;

    /* A read whose reply comes 12 s late: sent again on the same session, and the late reply
       is not taken for the next reply. */
    CHECK(connected(), "negotiate");
    delay_us[0] = 12000000;
    r = smb_ReadFile(0x4000, 4096, 0, buf, 2048);
    CHECK(r == 2048, "late read reply: the read succeeds");
    CHECK(memcmp(buf, &file[4096], 2048) == 0, "late read reply: the data is the server's");
    CHECK(read_requests == 2 && closes == 0, "late read reply: one retry, session kept");
    r = smb_ReadFile(0x4000, 0, 0, buf, 2048);
    CHECK(r == 2048 && memcmp(buf, &file[0], 2048) == 0, "the next read gets its own reply");
    CHECK(read_requests == 3 && closes == 0, "the next read: one request");

    /* A VMC page write whose reply comes late: written again, the page is on the server, and the
       next page write succeeds at once. */
    CHECK(connected(), "negotiate");
    delay_us[0] = 15000000;
    memset(page, 0x5A, sizeof(page));
    r = smb_WriteFile(0x4001, 8192, 0, page, sizeof(page));
    CHECK(r == (int)sizeof(page), "late write reply: the write succeeds");
    CHECK(memcmp(&file[8192], page, sizeof(page)) == 0, "late write reply: the page is stored");
    CHECK(write_requests == 2 && closes == 0, "late write reply: one retry, session kept");
    t0 = now_us;
    memset(page, 0x3C, sizeof(page));
    r = smb_WriteFile(0x4001, 8704, 0, page, sizeof(page));
    CHECK(r == (int)sizeof(page) && memcmp(&file[8704], page, sizeof(page)) == 0, "the next write");
    CHECK(now_us - t0 < 1000000, "the next write does not wait");

    /* A late reply after a keep-alive, in 100-byte segments, whose drain times out once part-way. */
    CHECK(connected(), "negotiate");
    keepalive_first = 1;
    seg_size = 100;
    delay_us[0] = 25000000; /* past the first reply deadline and the first drain deadline */
    delay_us[1] = 0;
    r = smb_ReadFile(0x4000, 0, 0, buf, 4096);
    CHECK(r == 4096 && memcmp(buf, &file[0], 4096) == 0, "pieced late reply: the read succeeds");
    CHECK(closes == 0, "pieced late reply: session kept");
    keepalive_first = 0;
    seg_size = 1460;
    r = smb_ReadFile(0x4000, 65536, 0, buf, 2048);
    CHECK(r == 2048 && memcmp(buf, &file[65536], 2048) == 0, "after the pieced reply: in step");

    /* A server that never answers: each request fails in bounded time, and after three silent
       drains the session is closed, so later requests fail at once. */
    CHECK(connected(), "negotiate");
    for (i = 0; i < 16; i++)
        delay_us[i] = NEVER;
    t0 = now_us;
    r = smb_ReadFile(0x4000, 0, 0, buf, 2048);
    CHECK(r < 0, "silent server: the read fails");
    CHECK(now_us - t0 == 30000000ULL, "silent server: after its three 10 s deadlines");
    CHECK(closes == 0, "silent server: the session is kept while the reply may still come");
    t0 = now_us;
    r = smb_WriteFile(0x4001, 0, 0, page, sizeof(page));
    CHECK(r <= 0 && now_us - t0 == 10000000ULL, "silent server: the next request waits one more drain");
    CHECK(closes == 1, "silent server: the session is closed after three silent drains");
    CHECK(write_requests == 0, "silent server: nothing is sent while out of step");
    t0 = now_us;
    r = smb_WriteFile(0x4001, 0, 0, page, sizeof(page));
    CHECK(r <= 0 && now_us - t0 == 0, "after it, a write fails at once");

    /* Negotiation: no reply on the first socket, then a reply on a fresh one. */
    reset();
    delay_us[0] = NEVER;
    r = smb_NegotiateProtocol("192.168.1.100", 445, "", "", &caps, hash);
    CHECK(r == 1, "negotiate: succeeds on the second attempt");
    CHECK(sockets == 2 && negotiate_requests == 2, "negotiate: a fresh socket for the retry");
    CHECK(now_us >= 10000000ULL && now_us < 13000000ULL, "negotiate: retried after one deadline");

    /* Negotiation that never gets a reply gives up after four attempts (about 43 s). */
    reset();
    for (i = 0; i < 16; i++)
        delay_us[i] = NEVER;
    r = smb_NegotiateProtocol("192.168.1.100", 445, "", "", &caps, hash);
    CHECK(r < 0, "negotiate: fails when the server never answers");
    CHECK(sockets == 4 && negotiate_requests == 4, "negotiate: four attempts");
    CHECK(now_us >= 40000000ULL && now_us <= 45000000ULL, "negotiate: about 43 s in all");
    CHECK(live_socket < 0, "negotiate: no socket left open");
    CHECK(!sema_held, "the I/O semaphore is released");

    if (failures)
        return 1;
    puts("SMB1 reply resync: PASS");
    return 0;
}
'''


def main():
    if CC is None:
        sys.exit('no C compiler')
    with tempfile.TemporaryDirectory(prefix='riptopl-smb1-resync-') as tmp:
        tmp = Path(tmp)
        inc = tmp / 'inc'
        inc.mkdir()
        for name, text in HEADERS.items():
            (inc / name).write_text(text, encoding='utf-8')
        harness = tmp / 'harness.c'
        harness.write_text(HARNESS, encoding='utf-8')
        exe = tmp / 'test'
        cmd = [CC, '-std=gnu99', '-O1', '-g', '-Wall', '-Wno-unused-function', '-Wno-unused-variable',
               '-Wno-pointer-sign', '-Wno-address-of-packed-member',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I', str(inc), '-I', str(ROOT / 'modules/iopcore/cdvdman'), '-I', str(ROOT / 'modules/iopcore/common'),
               str(harness), str(SMB_C), '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=True, text=True)
        if build.returncode:
            sys.exit('SMB1 reply resync: host build failed\n' + build.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
    sys.stdout.write(result.stdout)
    if result.returncode:
        sys.stdout.write(result.stderr)
        sys.exit('SMB1 reply resync: FAIL')


if __name__ == '__main__':
    main()
