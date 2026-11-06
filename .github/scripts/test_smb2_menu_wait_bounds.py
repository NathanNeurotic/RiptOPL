"""Exercise the production SMB2 sync wait and network link-mode init against stalled stubs.

The host harness extracts the actual two functions; the tests fail if a future
refactor removes the hard reply deadline or restores unlimited link retries.
A real console/server test is still required to verify the PS2 socket behavior.
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


sync_source = ROOT / "modules/network/smb2man/libsmb2/lib/sync.c"
pending_helpers = function(
    sync_source,
    "static int unlink_pending_pdu(",
    "static int wait_for_reply(",
)
sync = function(
    sync_source,
    "static int wait_for_reply(",
    "static void sync_connect_cb(",
)

sync_harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct smb2_context;
struct smb2_pdu {
    struct smb2_pdu *next;
    void (*cb)(struct smb2_context *, int, void *, void *);
    void *cb_data;
};
struct smb2_context {
    int fd;
    int timeout;
    struct smb2_pdu *pdu, *next_pdu, *outqueue, *waitqueue;
};
#define SMB2_STATUS_SHUTDOWN (-999)
struct sync_cb_data { int is_finished; };
struct pollfd { int fd; short events, revents; };
#define SMB2_VALID_SOCKET(fd) ((fd) >= 0)

static int tick, closes, mode, close_saw_timeout_error;
static int pending_callback_count, pending_freed_count;
static void smb2_free_pdu(struct smb2_context *ctx, struct smb2_pdu *pdu)
{
    (void)ctx;
    (void)pdu;
    pending_freed_count++;
}
static char error_message[128];
static struct sync_cb_data *active_callback;

static time_t fake_time(time_t *unused)
{
    (void)unused;
    return (time_t)tick;
}
static int fake_poll(struct pollfd *fd, unsigned long nfds, int timeout_ms)
{
    assert(nfds == 1 && timeout_ms == 1000);
    tick++;
    fd->revents = (mode == 1 || mode == 3) ? 1 : 0;
    return 0;
}
static int smb2_get_fd(struct smb2_context *ctx) { return ctx->fd; }
static int smb2_which_events(struct smb2_context *ctx) { (void)ctx; return 1; }
static void smb2_timeout_pdus(struct smb2_context *ctx)
{
    (void)ctx;
    if (mode == 2 && tick >= 3)
        active_callback->is_finished = 1;
}
static int smb2_service(struct smb2_context *ctx, int events)
{
    (void)ctx;
    (void)events;
    if (mode == 1)
        active_callback->is_finished = 1;
    return 0;
}
static const char *smb2_get_error(struct smb2_context *ctx)
{
    (void)ctx;
    return error_message;
}
static void smb2_set_error(struct smb2_context *ctx, const char *message, ...)
{
    (void)ctx;
    snprintf(error_message, sizeof(error_message), "%s", message);
}
static void smb2_close_context(struct smb2_context *ctx)
{
    close_saw_timeout_error = strstr(error_message, "timed out") != NULL;
    closes++;
    ctx->fd = -1;
}
#define time fake_time
#define poll fake_poll
'''

sync_main = r'''
static void pending_cb(struct smb2_context *ctx, int status, void *data, void *private_data)
{
    (void)ctx;
    (void)data;
    (void)private_data;
    assert(status == SMB2_STATUS_SHUTDOWN);
    pending_callback_count++;
}

static void test_pending_pdu_unlink(void)
{
    struct smb2_context ctx = {0};
    struct smb2_pdu target = {0}, first = {0}, last = {0};
    target.cb = pending_cb;
    pending_callback_count = pending_freed_count = 0;

    /* Remove an outqueue head without losing its successor. */
    ctx.outqueue = &target;
    target.next = &last;
    sync_free_pending_pdu(&ctx, &target);
    assert(ctx.outqueue == &last && target.next == NULL);
    assert(pending_callback_count == 1 && pending_freed_count == 1);

    /* Remove a middle waitqueue node without leaving a freed link. */
    ctx.outqueue = NULL;
    ctx.waitqueue = &first;
    first.next = &target;
    target.next = &last;
    sync_free_pending_pdu(&ctx, &target);
    assert(ctx.waitqueue == &first && first.next == &last && target.next == NULL);
    assert(pending_callback_count == 2 && pending_freed_count == 2);

    /* A partially received active PDU is also detached before freeing. */
    ctx.waitqueue = NULL;
    ctx.pdu = &target;
    sync_free_pending_pdu(&ctx, &target);
    assert(ctx.pdu == NULL);
    assert(pending_callback_count == 3 && pending_freed_count == 3);
}

static void scenario(int kind, int expected_result, int expected_closes, int expected_tick)
{
    struct smb2_context context = { .fd = 3, .timeout = 5 };
    struct sync_cb_data callback = { .is_finished = 0 };
    tick = closes = close_saw_timeout_error = 0;
    mode = kind;
    active_callback = &callback;
    error_message[0] = 0;
    assert(wait_for_reply(&context, &callback) == expected_result);
    assert(closes == expected_closes);
    /* Exact boundary catches changing >= to > in the production timeout. */
    assert(tick == expected_tick);
    assert(close_saw_timeout_error == expected_closes);
    if (expected_closes)
        assert(strstr(error_message, "timed out") != NULL);
}
int main(void)
{
    test_pending_pdu_unlink();
    scenario(0, -1, 1, 5);  /* Valid socket, no events: active PDU absent from timeout queues */
    scenario(1, 0, 0, 1);   /* Reply completes normally */
    scenario(2, 0, 0, 3);   /* Existing PDU timeout callback completes the request */
    scenario(3, -1, 1, 5);  /* Readable socket, but active reply never completes */
    puts("smb2 sync deadline: PASS");
    return 0;
}
'''

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
struct NetSupport {
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
        subprocess.run(
            [compiler, "-O0", "-Wall", "-Wextra", "-Wno-unused-parameter",
             str(src), "-o", str(exe)], check=True,
        )
        subprocess.run([str(exe)], check=True, timeout=15)


run(os.environ.get("CC", "cc"), "c", sync_harness + pending_helpers + sync + sync_main)
run(os.environ.get("CXX", "c++"), "cpp", net_harness + net + net_main)
print("smb2 menu wait bounds: all passed")
