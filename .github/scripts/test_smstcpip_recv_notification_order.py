"""Prove production TCP event callbacks never advertise unqueued mailbox data.

SMSTCPIP's lwip_recvfrom(MSG_DONTWAIT) gates netconn_recv() using the socket's
readability event count, while netconn_recv() uses blocking sys_mbox_fetch().
Calling the readability callback before sys_mbox_post() can therefore block
a supposed nonblocking receive. Compile the real recv_tcp/err_tcp functions and
make the callback consume a just-notified mailbox item synchronously.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
API = (ROOT / "modules/network/SMSTCPIP/api_msg.c").read_text(encoding="utf-8")
SOCKETS = (ROOT / "modules/network/SMSTCPIP/sockets.c").read_text(encoding="utf-8")
ARCH = (ROOT / "modules/network/SMSTCPIP/ps2ip.c").read_text(encoding="utf-8")

assert "((flags & MSG_DONTWAIT) || (sock->flags & O_NONBLOCK))" in SOCKETS
assert "netconn_recv(sock->conn)" in SOCKETS
assert "sys_mbox_fetch(conn->recvmbox, (void **)&p);" in (
    ROOT / "modules/network/SMSTCPIP/api_lib.c"
).read_text(encoding="utf-8")
assert "while (IsMessageBoxEmpty(pMBox))" in ARCH


def function(signature):
    start = API.index(signature)
    opening = API.index("{", start)
    depth = 0
    for end in range(opening, len(API)):
        if API[end] == "{":
            depth += 1
        elif API[end] == "}":
            depth -= 1
            if depth == 0:
                return API[start:end + 1]
    raise ValueError("Unterminated C function: " + signature)


production = "\n\n".join((
    function("static err_t recv_tcp(void *arg,"),
    function("static void\nerr_tcp(void *arg, err_t err)"),
))

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned short u16_t;
typedef int err_t;
typedef int sys_mbox_t;
enum netconn_evt { NETCONN_EVT_RCVPLUS = 1 };
#define ERR_OK 0
#define ERR_VAL -6
#define SYS_MBOX_NULL 0
#define SYS_SEM_NULL 0
#define SYS_ARCH_INC(value, delta) ((value) += (delta))

struct tcp_pcb { int dummy; };
struct pbuf { u16_t tot_len; };
struct netconn {
    struct { struct tcp_pcb *tcp; } pcb;
    err_t err;
    sys_mbox_t recvmbox, acceptmbox, mbox, sem;
    int recv_avail;
    void (*callback)(struct netconn *, enum netconn_evt, u16_t);
};
static int queued[4], posts, notifications, frees, acks, signals;
static void *last_item;
static void sys_mbox_post(sys_mbox_t mb, void *p)
{
    assert(mb > 0 && mb < 4);
    queued[mb]++;
    posts++;
    last_item = p;
}
static void pbuf_free(struct pbuf *p) { assert(p); frees++; }
static void sys_sem_signal(int sem) { assert(sem == 3); signals++; }
static void ack(int mb) { assert(mb == 2); acks++; }
#define TCPIP_APIMSG_ACK(mb) ack(mb)
static void notification(struct netconn *conn, enum netconn_evt event, u16_t length)
{
    assert(conn && event == NETCONN_EVT_RCVPLUS);
    assert(length <= 2048);
    notifications++;
    // This is the client's first MSG_DONTWAIT receive after the socket
    // reports readiness. An empty mailbox would make netconn_recv block.
    assert(queued[conn->recvmbox] > 0);
    queued[conn->recvmbox]--;
}
'''

tests = r'''
int main(void)
{
    struct netconn conn;
    struct tcp_pcb pcb;
    struct pbuf packet = {512};

    memset(&conn, 0, sizeof(conn));
    conn.pcb.tcp = &pcb;
    conn.recvmbox = 1;
    conn.callback = notification;

    assert(recv_tcp(&conn, &pcb, &packet, ERR_OK) == ERR_OK);
    assert(posts == 1 && notifications == 1 && conn.recv_avail == 512);
    assert(queued[1] == 0 && last_item == &packet);

    assert(recv_tcp(&conn, &pcb, NULL, ERR_OK) == ERR_OK);
    assert(posts == 2 && notifications == 2 && queued[1] == 0);
    assert(last_item == NULL);
    // The EOF marker carries no data, so it must not change the advertised byte count.
    assert(conn.recv_avail == 512);

    // Error delivery must likewise queue EOF before advertising readiness.
    conn.mbox = 2;
    conn.sem = 3;
    err_tcp(&conn, -7);
    assert(conn.pcb.tcp == NULL && conn.err == -7);
    assert(posts == 3 && notifications == 3 && acks == 1 && signals == 1);
    assert(queued[1] == 0);

    assert(recv_tcp(NULL, &pcb, &packet, ERR_OK) == ERR_VAL);
    assert(frees == 1);

    puts("SMSTCPIP TCP mailbox/readability ordering: PASS");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="riptopl-tcp-recv-order-") as tmp:
    cfile = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    cfile.write_text(harness + production + tests, encoding="utf-8")
    compiler = os.environ.get("CC", "cc")
    result = subprocess.run(
        [compiler, "-std=c99", "-O1", "-Wall", "-Wextra",
         "-Wno-unused-parameter", "-Werror",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(cfile), "-o", str(binary)],
        text=True, capture_output=True,
    )
    if result.returncode:
        raise RuntimeError("SMSTCPIP host compilation failed:\n" + result.stderr)
    subprocess.run([str(binary)], check=True, timeout=15)
