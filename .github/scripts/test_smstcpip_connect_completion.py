"""Compile the production SMSTCPIP do_connect() against SYN-enqueue failures.

The old do_connect() ignored tcp_connect() errors. When tcp_enqueue() could not
allocate a SYN, no asynchronous callback would acknowledge the waiting netconn
API message, leaving lwip_connect blocked. The test checks one ACK, no leaked PCB,
and the original successful asynchronous-completion path.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "modules/network/SMSTCPIP/api_msg.c").read_text(encoding="utf-8")
tcp = (ROOT / "modules/network/SMSTCPIP/tcp.c").read_text(encoding="utf-8")
tcp_out = (ROOT / "modules/network/SMSTCPIP/tcp_out.c").read_text(encoding="utf-8")

# Keep the test tied to the actual lwIP ownership contract.
assert tcp.index("TCP_REG(&tcp_active_pcbs, pcb);") < tcp.index(
    "ret = tcp_enqueue(pcb, NULL, 0, TCP_SYN"
)
assert "case SYN_SENT:" in tcp and "tcp_pcb_remove(&tcp_active_pcbs, pcb);" in tcp
assert "tcp_segs_free(queue);" in tcp_out

def c_function(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    raise ValueError("Unterminated do_connect")

target = c_function("static void\ndo_connect(struct api_msg_msg *msg)")
harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define LWIP_RAW 0
#define LWIP_UDP 0
#define LWIP_TCP 1
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_VAL -2
#define NETCONN_TCP 1
#define SYN_SENT 4
#define CLOSED 0

typedef int err_t;
struct ip_addr { int dummy; };
struct tcp_pcb { int state; };
struct netconn {
    int type;
    union { struct tcp_pcb *tcp; } pcb;
    err_t err;
    int mbox;
};
struct api_msg_msg {
    struct netconn *conn;
    struct { struct { struct ip_addr *ipaddr; unsigned short port; } bc; } msg;
};
static struct tcp_pcb pcb;
static int scenario, allocation_failure, new_calls, connect_calls, close_calls, ack_calls, setup_calls;
static void ack(int mailbox) { assert(mailbox == 7); ack_calls++; }
#define TCPIP_APIMSG_ACK(mailbox) ack(mailbox)

static struct tcp_pcb *tcp_new(void)
{
    new_calls++;
    if (allocation_failure)
        return NULL;
    pcb.state = CLOSED;
    return &pcb;
}
static void setup_tcp(struct netconn *conn)
{
    assert(conn && conn->pcb.tcp);
    setup_calls++;
}
static err_t do_connected(void *arg, struct tcp_pcb *pc, err_t err)
{
    (void)arg;
    assert(pc == &pcb);
    assert(err == ERR_OK);
    return ERR_OK;
}
static err_t tcp_connect(struct tcp_pcb *pc, struct ip_addr *ip, unsigned short port,
                         err_t (*callback)(void *, struct tcp_pcb *, err_t))
{
    assert(pc == &pcb && ip && port == 445 && callback == do_connected);
    connect_calls++;
    if (scenario == ERR_MEM)
        pc->state = SYN_SENT;
    return scenario;
}
static err_t tcp_close(struct tcp_pcb *pc)
{
    assert(pc == &pcb && pc->state == SYN_SENT);
    close_calls++;
    pc->state = CLOSED;
    return ERR_OK;
}
'''
tests = r'''
static void reset(int result, int fail_alloc)
{
    scenario = result;
    allocation_failure = fail_alloc;
    new_calls = connect_calls = close_calls = ack_calls = setup_calls = 0;
    pcb.state = CLOSED;
}
int main(void)
{
    struct netconn conn;
    struct ip_addr ip = {0};
    struct api_msg_msg message;

    memset(&conn, 0, sizeof(conn));
    conn.type = NETCONN_TCP;
    conn.mbox = 7;
    message.conn = &conn;
    message.msg.bc.ipaddr = &ip;
    message.msg.bc.port = 445;

    reset(ERR_OK, 0);
    do_connect(&message);
    assert(new_calls == 1 && connect_calls == 1 && setup_calls == 1);
    assert(ack_calls == 0 && close_calls == 0 && conn.pcb.tcp == &pcb);
    /* An ordinary connect remains pending until the real TCP callback. */

    conn.pcb.tcp = NULL;
    conn.err = ERR_OK;
    reset(ERR_MEM, 0);
    do_connect(&message);
    assert(new_calls == 1 && connect_calls == 1 && setup_calls == 1);
    assert(ack_calls == 1 && close_calls == 1);
    assert(conn.pcb.tcp == NULL && conn.err == ERR_MEM);
    /* A SYN allocation failure closes the registered PCB and releases the waiter. */

    conn.pcb.tcp = NULL;
    conn.err = ERR_OK;
    reset(ERR_VAL, 0);
    do_connect(&message);
    assert(ack_calls == 1 && close_calls == 0);
    assert(conn.pcb.tcp == &pcb && conn.err == ERR_VAL);
    /* A pre-SYN error ACKs but does not free a PCB not registered in the active list. */

    conn.pcb.tcp = NULL;
    conn.err = ERR_OK;
    reset(ERR_OK, 1);
    do_connect(&message);
    assert(new_calls == 1 && connect_calls == 0 && close_calls == 0);
    assert(ack_calls == 1 && conn.pcb.tcp == NULL && conn.err == ERR_MEM);
    /* Failure to allocate the initial PCB also releases the waiter. */

    puts("SMSTCPIP immediate TCP connect failure completion: PASS");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="riptopl-smstcpip-connect-") as temp:
    cfile = Path(temp) / "test.c"
    binary = Path(temp) / "test"
    cfile.write_text(harness + target + tests, encoding="utf-8")
    compiler = os.environ.get("CC", "cc")
    result = subprocess.run(
        [compiler, "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(cfile), "-o", str(binary)],
        capture_output=True, text=True,
    )
    if result.returncode:
        raise RuntimeError("Transport host compile failed:\n" + result.stderr)
    subprocess.run([str(binary)], check=True, timeout=15)
