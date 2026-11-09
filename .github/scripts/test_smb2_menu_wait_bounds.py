"""Exercise the production SMB2 menu socket waits and network link-mode init against stalled peers.

The host harness extracts the real code: smb2man.c's socket calls (every read and write usmb2 makes
goes through them) and NetSupport's link-mode init. The tests fail if a refactor removes the reply
deadline or restores unlimited link retries. A console/server test is still needed for the IOP's
own socket behaviour.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source: Path, start: str, next_decl: str) -> str:
    code = source.read_text(encoding="utf-8")
    first = code.index(start)
    last = code.index(next_decl, first + len(start))
    return code[first:last]


smb2man_source = ROOT / "modules/network/smb2man/smb2man.c"
socket_calls = function(smb2man_source, "static int smb2man_wait_ms", "#undef read")
assert "#include \"usmb2/usmb2.c\"" in smb2man_source.read_text(encoding="utf-8")
assert "#define read(a, b, c)  smb2man_sock_read(a, b, c)" in smb2man_source.read_text(encoding="utf-8")

sock_harness = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define SMB2MAN_REPLY_MS 15000
#define SMB2MAN_CLOSE(fd) close(fd)
'''

sock_main = r'''
static long elapsed_ms(struct timespec *t0)
{
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0->tv_sec) * 1000 + (t1.tv_nsec - t0->tv_nsec) / 1000000;
}

int main(void)
{
    int sv[2];
    char buf[4096];
    struct timespec t0;
    long ms;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    smb2man_wait_ms = 300;

    /* A peer that never answers: the read gives up at the deadline instead of blocking. */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    assert(smb2man_sock_read(sv[0], buf, sizeof(buf)) == -1);
    ms = elapsed_ms(&t0);
    assert(ms >= 250 && ms < 3000);

    /* Data that is there is read at once. */
    assert(write(sv[1], "abc", 3) == 3);
    assert(smb2man_sock_read(sv[0], buf, sizeof(buf)) == 3 && memcmp(buf, "abc", 3) == 0);

    /* A peer that never reads: once its buffer is full the write gives up at the deadline. */
    fcntl(sv[0], F_SETFL, O_NONBLOCK);
    memset(buf, 0x5a, sizeof(buf));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        int n = smb2man_sock_write(sv[0], buf, sizeof(buf));
        if (n == -1 && elapsed_ms(&t0) >= 250)
            break;
        assert(elapsed_ms(&t0) < 10000);
    }
    assert(elapsed_ms(&t0) < 10000);

    /* A closed or never-opened socket fails without waiting. */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    assert(smb2man_sock_read(-1, buf, 1) == -1 && smb2man_sock_write(-1, buf, 1) == -1);
    assert(elapsed_ms(&t0) < 100);
    smb2man_sock_close(sv[0]);
    close(sv[1]);
    puts("smb2man socket waits: PASS");
    return 0;
}
'''

net_wait = function(
    ROOT / "src/sources/netsupport.cpp",
    "int NetSupport::waitValidState(",
    "int NetSupport::waitValidLinkState()",
)
net = function(
    ROOT / "src/sources/netsupport.cpp",
    "int NetSupport::initApplyConfig()",
    "int NetSupport::applyNetConfig()",
)

net_harness = r'''
#include <cassert>
#include <cstdio>
#define LOG(...) ((void)0)
#define ERROR_ETH_LINK_FAIL 501
#define ERROR_ETH_DHCP_FAIL 502

static int gNetworkStartup, ps2_ip_use_dhcp;
static int attempts, failures_left;
typedef int s32;
typedef unsigned short u16;
struct ee_sema_t { int option, attr, init_count, max_count; };
static int alarm_fail, alarm_registrations, wait_calls, deleted_semas;
static int CreateSema(ee_sema_t *) { return 4; }
static int DeleteSema(int) { deleted_semas++; return 0; }
static int rmGetHsync() { return 15; }
static void NetStatusCheckCb(s32, u16, void *) {}
static int SetAlarm(unsigned int, void (*)(s32, u16, void *), void *)
{
    alarm_registrations++;
    return alarm_fail ? -1 : 7;
}
static int WaitSema(int) { wait_calls++; return 0; }
static int link_always_down() { return 0; }

struct NetSupport {
    int waitValidState(int (*checkingFunction)(void));
    int initApplyConfig();
    int waitValidLinkState() { return 0; }
    int applyNetifConfig() {
        attempts++;
        if (failures_left-- > 0) return -7;
        return 0;
    }
    int applyIpConfig() { return 0; }
    int waitValidDhcpState() { return 0; }
};
'''

net_main = r'''
int main()
{
    NetSupport support;
    /* If SetAlarm fails, the real waitValidState must not enter WaitSema. */
    alarm_fail = 1;
    alarm_registrations = wait_calls = deleted_semas = 0;
    assert(support.waitValidState(&link_always_down) == -1);
    assert(alarm_registrations == 1 && wait_calls == 0 && deleted_semas == 1);
    gNetworkStartup = attempts = 0;
    failures_left = 100;
    assert(support.initApplyConfig() == ERROR_ETH_LINK_FAIL);
    assert(gNetworkStartup == ERROR_ETH_LINK_FAIL && attempts == 3);

    gNetworkStartup = attempts = 0;
    failures_left = 2;
    assert(support.initApplyConfig() == 0);
    assert(attempts == 3);

    gNetworkStartup = attempts = 0;
    failures_left = 0;
    assert(support.initApplyConfig() == 0);
    assert(attempts == 1);
    std::puts("network link-mode retries: PASS");
    return 0;
}
'''


def run(compiler: str, suffix: str, source: str) -> None:
    with tempfile.TemporaryDirectory(prefix="riptopl-smb2-waits-") as temp:
        src = Path(temp) / ("case." + suffix)
        exe = Path(temp) / "case"
        src.write_text(source, encoding="utf-8")
        compile_result = subprocess.run(
            [compiler, "-O0", "-Wall", "-Wextra", "-Wno-unused-parameter",
             str(src), "-o", str(exe)], capture_output=True, text=True,
        )
        if compile_result.returncode:
            # The aggregated host runner retains only a short tail. Put all
            # diagnostics on one line so the actual failure is not concealed.
            raise RuntimeError("Host test compile failed: " +
                               compile_result.stderr.replace("\n", " | ")[:6000])
        subprocess.run([str(exe)], check=True, timeout=15)


run(os.environ.get("CC", "cc"), "c", sock_harness + socket_calls + sock_main)
run(os.environ.get("CXX", "c++"), "cpp", net_harness + net_wait + net + net_main)
print("smb2 menu wait bounds: all passed")
