"""Exercise production SMSTCPIP netconn startup with resource failures.

An allocated mailbox owns an IOP semaphore/messagebox. Failure to allocate
the next API message, or failure to create the TCP PCB, must free it before
returning NULL. A one-mailbox model exposes resource leaks across retries.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "modules/network/SMSTCPIP/api_lib.c").read_text(encoding="utf-8")


def extract(marker):
    start = SOURCE.index(marker)
    brace = SOURCE.index("{", start)
    depth = 0
    for pos in range(brace, len(SOURCE)):
        if SOURCE[pos] == "{":
            depth += 1
        elif SOURCE[pos] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[start:pos + 1]
    raise ValueError("Unterminated function: " + marker)


production = extract("struct\n    netconn *\n    netconn_new_with_proto_and_callback(")
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint16_t u16_t;
typedef int err_t;
typedef int sys_mbox_t;
typedef int sys_sem_t;

#define ERR_OK 0
#define ERR_MEM -1
#define LWIP_TCPIP_CORE_LOCKING 0
#define NETCONN_NONE 0
#define NETCONN_TCP 1
#define SYS_MBOX_NULL 0
#define SYS_SEM_NULL 0
#define MEMP_NETCONN 1
#define MEMP_API_MSG 2
#define API_MSG_NEWCONN 3
enum netconn_type { NETCONN_TCP_ENUM = NETCONN_TCP };
enum netconn_evt { NETCONN_EVT_RCVPLUS = 1 };

struct netconn {
    err_t err;
    int type;
    union { void *tcp; } pcb;
    sys_mbox_t mbox, recvmbox, acceptmbox;
    sys_sem_t sem;
    int state, socket, recv_avail;
    void (*callback)(struct netconn *, enum netconn_evt, u16_t);
};
struct api_msg {
    int type;
    union {
        struct netconn *conn;
        struct { struct { u16_t port; } bc; } msg;
    } msg;
};
static struct netconn conn;
static struct api_msg message;
static int netconn_live, mailbox_live, netconn_freed, mailbox_freed, msg_freed;
static int fail_conn_alloc, fail_mbox_alloc, fail_msg_alloc, fail_driver;
static int posted;

static void *memp_malloc(int type)
{
    if (type == MEMP_NETCONN) {
        if (fail_conn_alloc || netconn_live)
            return NULL;
        netconn_live = 1;
        memset(&conn, 0, sizeof(conn));
        return &conn;
    }
    assert(type == MEMP_API_MSG);
    return fail_msg_alloc ? NULL : &message;
}
static void memp_free(int type, void *ptr)
{
    if (type == MEMP_NETCONN) {
        assert(ptr == &conn && netconn_live);
        netconn_live = 0;
        netconn_freed++;
    } else {
        assert(type == MEMP_API_MSG && ptr == &message);
        msg_freed++;
    }
}
static sys_mbox_t sys_mbox_new(void)
{
    if (fail_mbox_alloc || mailbox_live)
        return SYS_MBOX_NULL;
    mailbox_live = 1;
    return 5;
}
static void sys_mbox_free(sys_mbox_t mailbox)
{
    assert(mailbox == 5 && mailbox_live);
    mailbox_live = 0;
    mailbox_freed++;
}
static void api_msg_post(struct api_msg *msg)
{
    assert(msg == &message && msg->type == API_MSG_NEWCONN);
    assert(msg->msg.conn == &conn);
    posted++;
    conn.err = fail_driver ? ERR_MEM : ERR_OK;
}
'''
tests = r'''
static void configure(int fail_conn, int fail_mailbox, int fail_message, int fail_core)
{
    assert(!netconn_live && !mailbox_live);
    fail_conn_alloc = fail_conn;
    fail_mbox_alloc = fail_mailbox;
    fail_msg_alloc = fail_message;
    fail_driver = fail_core;
}
int main(void)
{
    struct netconn *c;
    int i;
    for (i = 0; i < 100; i++) {
        configure(1, 0, 0, 0);
        assert(netconn_new_with_proto_and_callback(NETCONN_TCP_ENUM, 0, NULL) == NULL);
        assert(!netconn_live && !mailbox_live);

        configure(0, 1, 0, 0);
        assert(netconn_new_with_proto_and_callback(NETCONN_TCP_ENUM, 0, NULL) == NULL);
        assert(!netconn_live && !mailbox_live);

        configure(0, 0, 1, 0);
        assert(netconn_new_with_proto_and_callback(NETCONN_TCP_ENUM, 0, NULL) == NULL);
        assert(!netconn_live && !mailbox_live);

        configure(0, 0, 0, 1);
        assert(netconn_new_with_proto_and_callback(NETCONN_TCP_ENUM, 0, NULL) == NULL);
        assert(!netconn_live && !mailbox_live);
    }
    assert(netconn_freed == 300 && mailbox_freed == 200);
    assert(msg_freed == 100 && posted == 100);

    configure(0, 0, 0, 0);
    c = netconn_new_with_proto_and_callback(NETCONN_TCP_ENUM, 445, NULL);
    assert(c == &conn && c->err == ERR_OK);
    assert(netconn_live && mailbox_live && c->mbox == 5);
    sys_mbox_free(c->mbox);  /* Model normal netconn_delete completion. */
    memp_free(MEMP_NETCONN, c);
    assert(!netconn_live && !mailbox_live);
    assert(netconn_freed == 301 && mailbox_freed == 201);
    puts("SMSTCPIP netconn startup resource failures: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="riptopl-netconn-start-") as td:
    source = Path(td) / "test.c"
    binary = Path(td) / "test"
    source.write_text(harness + production + tests, encoding="utf-8")
    compiler = os.environ.get("CC", "cc")
    build = subprocess.run(
        [compiler, "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(source), "-o", str(binary)],
        text=True, capture_output=True, check=False,
    )
    if build.returncode:
        raise RuntimeError("SMSTCPIP netconn test compile failed:\n" + build.stderr)
    subprocess.run([str(binary)], check=True, timeout=15)
