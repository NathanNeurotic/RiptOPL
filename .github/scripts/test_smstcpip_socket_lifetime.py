"""Compile production SMSTCPIP socket option / close handlers against PCB failures.

The TCP PCB is allocated during socket creation, before connect. Failed
connect can clear it while the descriptor remains live. Closing a never-
connected socket must also free its unregistered CLOSED PCB rather than call
tcp_close(CLOSED), which intentionally does not release it.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOCKETS = (ROOT / "modules/network/SMSTCPIP/sockets.c").read_text(encoding="utf-8")
API = (ROOT / "modules/network/SMSTCPIP/api_msg.c").read_text(encoding="utf-8")
TCP = (ROOT / "modules/network/SMSTCPIP/tcp.c").read_text(encoding="utf-8")

assert "case SYN_SENT:" in TCP
assert "case LISTEN:" in TCP
assert "/* Has already been closed, do nothing. */" in TCP
assert "memp_free(MEMP_TCP_PCB, pcb);" in TCP
assert "case NETCONN_TCP:" in API


def extract(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    raise ValueError("Unterminated function: " + marker)


functions = "\n\n".join((
    extract(SOCKETS, "int lwip_setsockopt(int s,"),
    extract(SOCKETS, "int lwip_close(int s)"),
    extract(API, "static void\ndo_delconn(struct api_msg_msg *msg)"),
))

header = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8_t;
typedef uint32_t u32_t;
typedef uint16_t u16_t;
typedef int socklen_t;
typedef int err_t;
typedef int sys_sem_t;
#define ERR_OK 0
#define ERR_MEM -1
#define err_to_errno(e) ((e) == ERR_MEM ? ENOMEM : EIO)
#define LWIP_TCP 1
#define LWIP_RAW 0
#define LWIP_UDP 0
#define NETCONN_TCP 1
#define CLOSED 0
#define LISTEN 2
#define SYN_SENT 4
#define MEMP_TCP_PCB 3
#define SYS_MBOX_NULL 0
#define SOL_SOCKET 0xfff
#define SO_BROADCAST 0x20
#define SO_KEEPALIVE 0x08
#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IP_TTL 2
#define IP_TOS 1
#define TCP_NODELAY 0x01
#define TCP_KEEPALIVE 0x02
#define TCP_ACKNODELAY 0x04
#define TCP_EVENSEG 0x08
#define TF_NODELAY 0x40
#define TF_ACKNODELAY 0x80
#define TF_EVENSEG 0x100
#define SOCKETS_DEBUG 0
#define LWIP_DEBUGF(...) ((void)0)

struct tcp_pcb {
    int state;
    unsigned so_options, flags, keepalive;
    u8_t ttl, tos;
};
struct netconn {
    int type;
    union { struct tcp_pcb *tcp; } pcb;
    int mbox;
    void (*callback)(struct netconn *, int, u16_t);
};
struct api_msg_msg { struct netconn *conn; };
struct netbuf { int marker; };
struct lwip_socket { struct netconn *conn; struct netbuf *lastdata; u16_t lastoffset; int err; };

static struct tcp_pcb pcb;
static struct netconn conn;
static struct lwip_socket sock;
static int live, freed, close_calls, abort_calls, acks, callbacks;
static int delete_fail, deleted, netbuf_deletes, sem_waits, sem_signals;
static sys_sem_t socksem;
static void do_delconn(struct api_msg_msg *msg);
static sys_sem_t sys_sem_new(int count) { assert(count == 1); return 1; }
static void sys_sem_wait(sys_sem_t sem) { assert(sem == 1); sem_waits++; }
static void sys_sem_signal(sys_sem_t sem) { assert(sem == 1); sem_signals++; }
static err_t netconn_delete(struct netconn *c)
{
    assert(c == &conn);
    deleted++;
    if (delete_fail)
        return ERR_MEM;
    struct api_msg_msg message = {c};
    do_delconn(&message);
    return ERR_OK;
}
static void netbuf_delete(struct netbuf *b)
{
    assert(b);
    netbuf_deletes++;
}
static int last_errno;
static struct lwip_socket *get_socket(int s)
{
    return s == 7 && sock.conn ? &sock : NULL;
}
#define set_errno(e) (last_errno = (e))
#define sock_set_errno(sk, e) do { (sk)->err = (e); last_errno = (e); } while (0)
static void memp_free(int type, void *pointer)
{
    assert(type == MEMP_TCP_PCB && pointer == &pcb && live);
    assert(pcb.state == CLOSED);
    live = 0;
    freed++;
}
static err_t tcp_close(struct tcp_pcb *p)
{
    assert(p == &pcb && live);
    close_calls++;
    if (p->state == SYN_SENT || p->state == LISTEN) {
        live = 0;
        return ERR_OK;
    }
    /* Reproduce the production tcp_close(CLOSED) NO-OP. */
    return ERR_OK;
}
static void tcp_abort(struct tcp_pcb *p)
{
    assert(p == &pcb);
    abort_calls++;
}
#define tcp_arg(p, cb) ((void)0)
#define tcp_accept(p, cb) ((void)0)
#define tcp_sent(p, cb) ((void)0)
#define tcp_recv(p, cb) ((void)0)
#define tcp_poll(p, cb, n) ((void)0)
#define tcp_err(p, cb) ((void)0)
static void notify(struct netconn *c, int event, u16_t length)
{
    assert(c == &conn && length == 0);
    assert(event == 1 || event == 2);
    callbacks++;
}
#define NETCONN_EVT_RCVPLUS 1
#define NETCONN_EVT_SENDPLUS 2
#define TCPIP_APIMSG_ACK(mailbox) do { assert((mailbox) == 5); acks++; } while (0)
'''

tests = r'''
static void begin(int state)
{
    assert(!live);
    memset(&pcb, 0, sizeof(pcb));
    pcb.state = state;
    live = 1;
    conn.pcb.tcp = &pcb;
    conn.type = NETCONN_TCP;
    conn.mbox = 5;
    conn.callback = notify;
    sock.conn = &conn;
    sock.err = 0;
    sock.lastdata = NULL;
    sock.lastoffset = 0;
}
int main(void)
{
    struct api_msg_msg msg = {&conn};
    int on = 1;
    int val = 100;
    int i;

    /* Live socket options retain their old behavior. */
    begin(CLOSED);
    assert(lwip_setsockopt(7, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == 0);
    assert((pcb.flags & TF_NODELAY) != 0);
    assert(lwip_setsockopt(7, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) == 0);
    assert((pcb.so_options & SO_KEEPALIVE) != 0);
    assert(lwip_setsockopt(7, IPPROTO_IP, IP_TTL, &val, sizeof(val)) == 0);
    assert(pcb.ttl == 100);
    assert(lwip_setsockopt(7, IPPROTO_TCP, TCP_NODELAY, &on, 1) == -1);
    assert(sock.err == EINVAL);
    assert(lwip_setsockopt(7, IPPROTO_TCP, TCP_NODELAY, NULL, sizeof(on)) == -1);
    assert(sock.err == EFAULT);
    assert(lwip_setsockopt(99, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == -1);
    assert(last_errno == EBADF);

    /* Simulate the new immediate-error close path: descriptor outlives its PCB. */
    memp_free(MEMP_TCP_PCB, conn.pcb.tcp);
    conn.pcb.tcp = NULL;
    assert(lwip_setsockopt(7, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == -1);
    assert(sock.err == ENOTCONN);
    assert(lwip_setsockopt(7, IPPROTO_IP, IP_TTL, &val, sizeof(val)) == -1);
    assert(sock.err == ENOTCONN);
    assert(lwip_setsockopt(7, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) == -1);
    assert(sock.err == ENOTCONN);
    do_delconn(&msg); /* Null PCB is already released; no second free. */
    assert(!live && close_calls == 0 && acks == 1);

    /* A newly created socket can be closed without ever calling connect.
       Repeat beyond the IOP's small PCB pool to expose leaked CLOSED PCBs. */
    freed = close_calls = acks = callbacks = 0;
    for (i = 0; i < 100; i++) {
        begin(CLOSED);
        do_delconn(&msg);
        assert(!live && conn.pcb.tcp == NULL);
    }
    assert(freed == 100 && close_calls == 0 && acks == 100);
    assert(callbacks == 200 && abort_calls == 0);

    /* The established SYN_SENT close path still uses the existing tcp_close. */
    begin(SYN_SENT);
    do_delconn(&msg);
    assert(!live && close_calls == 1 && abort_calls == 0);
    assert(acks == 101);

    /* If netconn_delete cannot allocate its API message, closing must fail
       without losing the socket/PCB or its unread data; retry can reclaim it. */
    struct netbuf packet = {123};
    begin(CLOSED);
    sock.lastdata = &packet;
    sock.lastoffset = 17;
    delete_fail = 1;
    assert(lwip_close(7) == -1);
    assert(sock.err == ENOMEM && live && sock.conn == &conn);
    assert(sock.lastdata == &packet && sock.lastoffset == 17);
    assert(netbuf_deletes == 0 && sem_waits == sem_signals);
    delete_fail = 0;
    assert(lwip_close(7) == 0);
    assert(!live && sock.conn == NULL && sock.lastdata == NULL);
    assert(sock.lastoffset == 0 && netbuf_deletes == 1);
    assert(deleted == 2 && sem_waits == sem_signals);

    puts("SMSTCPIP null-PCB options and socket failure lifecycle: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="riptopl-socket-lifetime-") as td:
    src = Path(td) / "case.c"
    exe = Path(td) / "case"
    src.write_text(header + functions + tests, encoding="utf-8")
    compiler = os.environ.get("CC", "cc")
    cmd = [compiler, "-std=c99", "-O1", "-Wall", "-Wextra",
           "-Wno-unused-parameter", "-Werror",
           "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
           str(src), "-o", str(exe)]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError("Socket lifecycle host compilation failed:\n" + result.stderr)
    subprocess.run([str(exe)], check=True, timeout=15)
