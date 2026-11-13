"""Compile production SMB1 connection and reply functions against timed socket stubs.

Protects against repeated full TCP connect attempts and a read that waits forever
on a silent, slowly-dribbling, or keep-alive-only server. No console network or
PS2 toolchain is required; hardware validation remains a separate requirement.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "modules/iopcore/cdvdman/smb.c").read_text(encoding="utf-8")


def extract(signature):
    start = SOURCE.index(signature)
    brace = SOURCE.index("{", start)
    depth = 0
    for pos in range(brace, len(SOURCE)):
        if SOURCE[pos] == "{":
            depth += 1
        elif SOURCE[pos] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[start:pos + 1]
    raise ValueError("Unterminated function: " + signature)


assert "#define MSG_DONTWAIT 0x40" in SOURCE
assert re.search(r"^#define SMB_REPLY_TIMEOUT_TICKS\s+368640000U", SOURCE, re.M)

# The receive deadline, the reply-stream tracking and smb_Resync, as one block of smb.c.
STREAM = SOURCE[SOURCE.index("static iop_sys_clock_t smb_reply_start;"):SOURCE.index("static int RecvData(int sock,")]
functions = extract("int OpenTCPSession(struct in_addr") + "\n\n" + STREAM + "\n\n" + "\n\n".join(
    extract(signature) for signature in (
        "static int RecvData(int sock,",
        "static int GetSMBServerReply(int shdrlen,",
    ))

header = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef struct { u32 lo, hi; } iop_sys_clock_t;
typedef struct { u8 data[32]; } SMBHeader_t;
#define SMB_REPLY_TIMEOUT_TICKS 368640000U
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif

static uint64_t virtual_us;
static int socket_calls, connect_calls, close_calls, recv_calls, body_calls, send_calls;
static int socket_fails, connect_fails, delay_fails, receive_mode, short_delays, long_delays;
enum { SILENT, CHUNKS, DRIP, CLOSED, KEEPALIVES, GOOD_REPLY, OVERSIZE_REPLY, SHORT_REPLY };
static struct {
    u32 sessionHeader;
    union { u8 bytes[1024]; } smb;
} SMB_buf;
static int main_socket = 7;

static void GetSystemTime(iop_sys_clock_t *clock)
{
    const uint64_t ticks = virtual_us * 36864ULL / 1000ULL;
    clock->lo = (u32)ticks;
    clock->hi = (u32)(ticks >> 32);
}
static int DelayThread(unsigned int micros)
{
    assert(micros == 100 || micros == 1000); // short polls first, then 1 ms (#399)
    if (delay_fails)
        return -1;
    if (micros == 100) {
        assert(long_delays == 0); // short polls come first, never after a 1 ms one
        short_delays++;
    } else
        long_delays++;
    virtual_us += micros;
    return 0;
}
static int fake_socket(int domain, int type, int protocol)
{
    assert(domain == PF_INET && type == SOCK_STREAM && protocol == IPPROTO_TCP);
    socket_calls++;
    return socket_fails ? -1 : 7;
}
static int fake_setsockopt(int fd, int level, int option, const void *value, socklen_t size)
{
    assert(fd == 7 && level == IPPROTO_TCP && option == TCP_NODELAY);
    assert(size == sizeof(int) && *(const int *)value == 1);
    return 0;
}
static int fake_connect(int fd, struct sockaddr *addr, socklen_t size)
{
    assert(fd == 7 && addr && size == sizeof(struct sockaddr_in));
    connect_calls++;
    return connect_fails ? -1 : 0;
}
static int fake_close(int fd)
{
    assert(fd == 7);
    close_calls++;
    return 0;
}
static void smb_ResetStream(void);
static int smb_Disconnect(void)
{
    smb_ResetStream();
    if (main_socket >= 0) {
        fake_close(main_socket);
        main_socket = -1;
    }
    return 1;
}
static int fake_recvfrom(int fd, void *h, int hlen, void *payload,
                         int plen, unsigned int flags, struct sockaddr *from,
                         socklen_t *fromlen)
{
    int n;
    assert(fd == 7 && flags == MSG_DONTWAIT && from == NULL && fromlen == NULL);
    assert(plen > 0);
    recv_calls++;
    if (hlen != 0)
        assert(h != NULL);
    if (receive_mode == SILENT)
        return -1;
    if (receive_mode == CLOSED)
        return 0;
    if (receive_mode == CHUNKS || receive_mode == DRIP) {
        n = plen > 3 ? 3 : plen;
        if (receive_mode == DRIP) {
            n = 1;
            virtual_us += 1200000;
        }
        memset(payload, 0xA5, (size_t)n);
        return n;
    }
    if (plen == 4) {
        /* An NBSS header: type, then the 24-bit length nb_GetSessionMessageLength() reports. */
        u8 *nbss = payload;
        int length = receive_mode == KEEPALIVES ? 0 : receive_mode == OVERSIZE_REPLY ? 2048 : receive_mode == SHORT_REPLY ? 16 : 32;
        nbss[0] = receive_mode == KEEPALIVES ? 0x85 : 0;
        nbss[1] = (u8)(length >> 16);
        nbss[2] = (u8)(length >> 8);
        nbss[3] = (u8)length;
        if (receive_mode == KEEPALIVES)
            virtual_us += 2000000;
        return 4;
    }
    body_calls++;
    memset(payload, 0, (size_t)plen);
    return plen;
}
static int nb_GetSessionMessageLength(void)
{
    return receive_mode == OVERSIZE_REPLY ? 2048 : receive_mode == SHORT_REPLY ? 16 : 32;
}
static u8 nb_GetPacketType(void) { return (u8)(SMB_buf.sessionHeader & 0xff); }
static int SendData(int fd, char *data, int bytes)
{
    assert(fd == 7 && data && bytes > 0);
    send_calls++;
    return bytes;
}
#define plwip_socket fake_socket
#define plwip_setsockopt fake_setsockopt
#define plwip_connect fake_connect
#define plwip_close fake_close
#define plwip_recvfrom fake_recvfrom
'''

tests = r'''
static void reset(int mode)
{
    virtual_us = 0;
    main_socket = 7;
    socket_calls = connect_calls = close_calls = recv_calls = body_calls = send_calls = 0;
    socket_fails = connect_fails = delay_fails = 0;
    short_delays = long_delays = 0;
    receive_mode = mode;
    memset(&SMB_buf, 0, sizeof(SMB_buf));
    smb_ResetStream();
}
int main(void)
{
    struct in_addr server = {0};
    char buf[40];
    reset(SILENT);
    connect_fails = 1;
    assert(OpenTCPSession(server, 445) == -1);
    assert(socket_calls == 1 && connect_calls == 1 && close_calls == 1);

    reset(SILENT);
    assert(OpenTCPSession(server, 445) == 7);
    assert(socket_calls == 1 && connect_calls == 1 && close_calls == 0);

    reset(SILENT);
    socket_fails = 1;
    assert(OpenTCPSession(server, 445) < 0);
    assert(connect_calls == 0 && close_calls == 0);

    reset(SILENT);
    smb_BeginReply();
    assert(RecvData(7, buf, 10) == -2);
    assert(virtual_us == 10000000ULL && recv_calls > 1);
    /* A reply's segments arrive a fraction of a millisecond apart: the first 2 ms are polled every 100 us (#399). */
    assert(short_delays == 20 && long_delays == 9998);
    /* A missed deadline keeps the session: the late reply is taken off before the next request. */
    assert(main_socket == 7 && close_calls == 0 && smb_rx.timed_out && smb_rx.open);

    reset(SILENT);
    delay_fails = 1;
    smb_BeginReply();
    assert(RecvData(7, buf, 10) == -2);
    assert(main_socket == -1 && close_calls == 1);
    assert(virtual_us == 0 && recv_calls == 1);

    reset(DRIP);
    smb_BeginReply();
    assert(RecvData(7, buf, 30) == -2);
    assert(virtual_us >= 10000000ULL && virtual_us < 12000000ULL);
    assert(recv_calls <= 9); /* Progress does not reset the reply deadline. */
    assert(main_socket == 7 && close_calls == 0 && smb_rx.timed_out);

    reset(CHUNKS);
    smb_BeginReply();
    assert(RecvData(7, buf, 10) == 10);
    assert(recv_calls == 4 && virtual_us == 0);

    reset(CLOSED);
    smb_BeginReply();
    assert(RecvData(7, buf, 10) == 0 && recv_calls == 1);
    assert(main_socket == -1 && close_calls == 1);

    reset(CHUNKS);
    smb_BeginReply();
    assert(smb_RecvTimed(7, buf, 49, buf, 10) == 3);

    reset(KEEPALIVES);
    assert(GetSMBServerReply(0, NULL, 0) == -2);
    assert(virtual_us >= 10000000ULL && virtual_us < 12000000ULL);
    assert(body_calls == 0); /* Keep-alives cannot make negotiation infinite. */

    reset(GOOD_REPLY);
    assert(GetSMBServerReply(0, NULL, 0) == 32);
    assert(body_calls == 1 && send_calls == 1);

    reset(OVERSIZE_REPLY);
    assert(GetSMBServerReply(0, NULL, 0) == -2);
    assert(body_calls == 0 && main_socket == -1 && close_calls == 1);
    /* Reject an overlong NBSS body and discard the unsynchronized stream. */

    reset(SHORT_REPLY);
    assert(GetSMBServerReply(0, NULL, 0) == -2);
    assert(body_calls == 0 && main_socket == -1 && close_calls == 1);

    puts("SMB1 TCP/reply bounds: PASS");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="riptopl-smb1-transport-") as tmp:
    source = Path(tmp) / "test.c"
    executable = Path(tmp) / "test"
    source.write_text(header + functions + tests, encoding="utf-8")
    compiler = os.environ.get("CC", "cc")
    build = subprocess.run(
        [compiler, "-std=c99", "-O1", "-g", "-Wall", "-Wextra",
         "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(source), "-o", str(executable)],
        capture_output=True, text=True,
    )
    if build.returncode:
        raise RuntimeError("SMB1 host compile failed:\n" + build.stderr)
    subprocess.run([str(executable)], check=True, timeout=20)
