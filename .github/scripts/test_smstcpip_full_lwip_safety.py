"""Run FULL_LWIP socket-information functions against a failed TCP connection.

SMSTCPIP is normally built with FULL_LWIP=0, but FULL_LWIP=1 exposes
getsockname/getpeername/getsockopt. The descriptor can outlive a failed TCP
connect whose PCB has been freed. Keep SO_ERROR/SO_TYPE usable in that state.
Compile the actual functions rather than testing a rewritten implementation.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "modules/network/SMSTCPIP/sockets.c").read_text(encoding="utf-8")


def extract(signature):
    start = SOURCE.index(signature)
    opening = SOURCE.index("{", start)
    depth = 0
    for index in range(opening, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[start:index + 1]
    raise ValueError("Unterminated function: " + signature)


functions = "\n\n".join(extract(s) for s in (
    "int lwip_getpeername(int s,",
    "int lwip_getsockname(int s,",
    "int lwip_getsockopt(int s,",
))

harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int socklen_t;
typedef unsigned char u8_t;
typedef unsigned short u16_t;
typedef int err_t;
#define ERR_OK 0
#define ERR_CONN -6
#define AF_INET 2
#define SOL_SOCKET 0xfff
#define SO_ACCEPTCONN 0x01
#define SO_BROADCAST 0x20
#define SO_ERROR 0x1007
#define SO_KEEPALIVE 0x08
#define SO_TYPE 0x1008
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
#define NETCONN_RAW 1
#define NETCONN_TCP 2
#define NETCONN_UDP 3
#define NETCONN_UDPLITE 4
#define NETCONN_UDPNOCHKSUM 5
#define SOCK_RAW 3
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCKETS_DEBUG 0
#define LWIP_DEBUGF(...) ((void)0)
#define ip_addr_debug_print(...) ((void)0)
#define mips_memset memset
#define mips_memcpy memcpy
#define htons(x) (x)

struct ip_addr { uint32_t addr; };
struct sockaddr { char contents[16]; };
struct sockaddr_in {
    u8_t sin_len, sin_family;
    u16_t sin_port;
    struct { uint32_t s_addr; } sin_addr;
};
struct tcp_pcb {
    unsigned so_options, flags, keepalive;
    u8_t ttl, tos;
};
struct netconn {
    int type;
    union { struct tcp_pcb *tcp; } pcb;
};
struct lwip_socket { struct netconn *conn; int err; };
static struct tcp_pcb pcb;
static struct netconn conn;
static struct lwip_socket sock;
static int last_errno, addr_calls, peer_calls;
static struct lwip_socket *get_socket(int fd) {
    return fd == 7 && sock.conn ? &sock : NULL;
}
#define set_errno(value) (last_errno = (value))
#define sock_set_errno(sk, value) do { (sk)->err = (value); last_errno = (value); } while (0)

static err_t netconn_addr(struct netconn *c, struct ip_addr **addr, u16_t *port) {
    static struct ip_addr local = {0x01020304};
    addr_calls++;
    assert(c == &conn && c->pcb.tcp != NULL);
    *addr = &local;
    *port = 445;
    return ERR_OK;
}
static err_t netconn_peer(struct netconn *c, struct ip_addr *addr, u16_t *port) {
    peer_calls++;
    assert(c == &conn);
    if (!c->pcb.tcp)
        return ERR_CONN;
    addr->addr = 0x05060708;
    *port = 445;
    return ERR_OK;
}
'''
tests = r'''
int main(void)
{
    struct sockaddr_in address = {0};
    socklen_t len = sizeof(address);
    int value = 0;
    socklen_t optlen = sizeof(value);

    memset(&conn, 0, sizeof(conn));
    conn.type = NETCONN_TCP;
    sock.conn = &conn;
    sock.err = ECONNRESET;

    /* Failed connect left the descriptor valid but released its PCB. */
    assert(lwip_getsockname(7, (struct sockaddr *)&address, &len) == -1);
    assert(sock.err == ENOTCONN && addr_calls == 0);
    assert(lwip_getpeername(7, (struct sockaddr *)&address, &len) == -1);
    assert(sock.err == ENOTCONN && peer_calls == 1);

    sock.err = ECONNRESET;
    assert(lwip_getsockopt(7, SOL_SOCKET, SO_ERROR, &value, &optlen) == 0);
    assert(value == ECONNRESET && sock.err == 0);
    assert(lwip_getsockopt(7, SOL_SOCKET, SO_TYPE, &value, &optlen) == 0);
    assert(value == SOCK_STREAM);
    assert(lwip_getsockopt(7, IPPROTO_IP, IP_TTL, &value, &optlen) == -1);
    assert(sock.err == ENOTCONN);
    assert(lwip_getsockopt(7, IPPROTO_TCP, TCP_NODELAY, &value, &optlen) == -1);
    assert(sock.err == ENOTCONN);
    assert(lwip_getsockopt(7, SOL_SOCKET, SO_KEEPALIVE, &value, &optlen) == -1);
    assert(sock.err == ENOTCONN);
    assert(lwip_getsockopt(7, IPPROTO_TCP, TCP_NODELAY, NULL, &optlen) == -1);
    assert(sock.err == EFAULT);

    /* A still-allocated TCP PCB retains all preexisting options. */
    memset(&pcb, 0, sizeof(pcb));
    conn.pcb.tcp = &pcb;
    pcb.flags = TF_NODELAY;
    pcb.ttl = 64;
    pcb.so_options = SO_KEEPALIVE;
    len = sizeof(address);
    assert(lwip_getsockname(7, (struct sockaddr *)&address, &len) == 0);
    assert(address.sin_addr.s_addr == 0x01020304 && addr_calls == 1);
    len = sizeof(address);
    assert(lwip_getpeername(7, (struct sockaddr *)&address, &len) == 0);
    assert(address.sin_addr.s_addr == 0x05060708 && peer_calls == 2);
    assert(lwip_getsockopt(7, IPPROTO_TCP, TCP_NODELAY, &value, &optlen) == 0);
    assert(value == TF_NODELAY);
    assert(lwip_getsockopt(7, IPPROTO_IP, IP_TTL, &value, &optlen) == 0);
    assert(value == 64);
    assert(lwip_getsockopt(7, SOL_SOCKET, SO_KEEPALIVE, &value, &optlen) == 0);
    assert(value != 0);
    assert(lwip_getsockopt(7, SOL_SOCKET, SO_TYPE, &value, &optlen) == 0);
    assert(value == SOCK_STREAM);

    puts("SMSTCPIP FULL_LWIP socket info with null PCB: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="riptopl-full-lwip-") as td:
    source = Path(td) / "test.c"
    executable = Path(td) / "test"
    source.write_text(harness + functions + tests, encoding="utf-8")
    compiler = os.environ.get("CC", "cc")
    compilation = subprocess.run(
        [compiler, "-std=c99", "-O1", "-Wall", "-Wextra",
         "-Wno-unused-parameter", "-Wno-sign-compare", "-Werror",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(source), "-o", str(executable)],
        text=True, capture_output=True, check=False,
    )
    if compilation.returncode:
        raise RuntimeError("FULL_LWIP socket test compile failed:\n" + compilation.stderr)
    subprocess.run([str(executable)], check=True, timeout=15)
