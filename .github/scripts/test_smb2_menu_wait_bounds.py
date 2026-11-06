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
copy_callback = function(sync_source, "static void sync_copy_ioctl_cb(", "int smb2_request_resume_key(")
copy_wrappers = function(sync_source, "int smb2_request_resume_key(", "static void sync_echo_cb(")

sync_harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct smb2_context;
struct smb2_pdu {
    struct smb2_pdu *next, *next_compound;
    void (*cb)(struct smb2_context *, int, void *, void *);
    void *cb_data;
};
struct smb2_context {
    int fd;
    int timeout;
    struct smb2_pdu *pdu, *next_pdu, *outqueue, *waitqueue;
};
#define SMB2_STATUS_SHUTDOWN (-999)
#define SMB2_STATUS_CANCELLED (-998)
struct sync_cb_data { int is_finished, status; void *ptr; };
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

copy_stubs = r'''
struct smb2fh { int unused; };
struct smb2_srv_copychunk_resume_key { unsigned char bytes[24]; };
struct smb2_srv_copychunk { int unused; };
struct smb2_srv_copychunk_reply { int unused; };
typedef void (*copy_cb_t)(struct smb2_context *, int, void *, void *);
static copy_cb_t pending_copy_callback;
static void *pending_copy_state;
static int copy_state_frees;

static void tracked_copy_free(void *pointer)
{
    assert(pointer == pending_copy_state);
    copy_state_frees++;
    pending_copy_state = NULL;
    free(pointer);
}
static int smb2_request_resume_key_async(struct smb2_context *ctx,
        struct smb2fh *fh, copy_cb_t cb, void *user)
{
    (void)ctx; (void)fh;
    pending_copy_callback = cb;
    pending_copy_state = user;
    return 0;
}
static int smb2_copychunk_async(struct smb2_context *ctx, uint32_t ctl,
        const struct smb2_srv_copychunk_resume_key *key, struct smb2fh *fh,
        const struct smb2_srv_copychunk *chunks, uint32_t n, copy_cb_t cb, void *user)
{
    (void)ctx; (void)ctl; (void)key; (void)fh; (void)chunks; (void)n;
    pending_copy_callback = cb;
    pending_copy_state = user;
    return 0;
}
static int smb2_server_side_copy_async(struct smb2_context *ctx, uint32_t ctl,
        struct smb2fh *src, struct smb2fh *dst,
        const struct smb2_srv_copychunk *chunks, uint32_t n, copy_cb_t cb, void *user)
{
    (void)ctx; (void)ctl; (void)src; (void)dst; (void)chunks; (void)n;
    pending_copy_callback = cb;
    pending_copy_state = user;
    return 0;
}
static void smb2_free_data(struct smb2_context *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}
'''

copy_tests = r'''
static void test_copy_callback_lifetime(void)
{
    struct smb2fh fh = {0};
    struct smb2_srv_copychunk_resume_key key = {0};
    struct smb2_srv_copychunk_reply reply = {0};
    struct smb2_srv_copychunk chunk = {0};
    int which, rc;

    for (which = 0; which < 3; ++which) {
        struct smb2_context ctx = { .fd = 3, .timeout = 2 };
        pending_copy_callback = NULL;
        pending_copy_state = NULL;
        copy_state_frees = closes = tick = 0;
        error_message[0] = 0;
        mode = 0;

        if (which == 0)
            rc = smb2_request_resume_key(&ctx, &fh, &key);
        else if (which == 1)
            rc = smb2_copychunk(&ctx, 0, &key, &fh, &chunk, 1, &reply);
        else
            rc = smb2_server_side_copy(&ctx, 0, &fh, &fh, &chunk, 1, &reply);

        assert(rc == -1 && closes == 1 && tick == 2);
        assert(pending_copy_callback && pending_copy_state);
        assert(copy_state_frees == 0); /* no free while callback is pending */
        pending_copy_callback(&ctx, SMB2_STATUS_SHUTDOWN, NULL, pending_copy_state);
        assert(copy_state_frees == 1 && pending_copy_state == NULL);
    }
}
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

/* Model the production queue traversal in smb2_destroy_context. Closing a
 * socket must not drain the queued requests. Shutdown callbacks may queue
 * additional work, so the outer loop must revisit each queue. */
static void destroy_queued_pdus(struct smb2_context *ctx)
{
    while (ctx->outqueue || ctx->pdu || ctx->next_pdu || ctx->waitqueue) {
        while (ctx->outqueue) {
            struct smb2_pdu *pdu = ctx->outqueue, *part;
            ctx->outqueue = pdu->next;
            for (part = pdu; part; part = part->next_compound)
                if (part->cb)
                    part->cb(ctx, SMB2_STATUS_SHUTDOWN, NULL, part->cb_data);
            smb2_free_pdu(ctx, pdu);
        }
        if (ctx->pdu) {
            struct smb2_pdu *pdu = ctx->pdu;
            ctx->pdu = NULL;
            if (pdu->cb)
                pdu->cb(ctx, SMB2_STATUS_SHUTDOWN, NULL, pdu->cb_data);
            smb2_free_pdu(ctx, pdu);
        }
        if (ctx->next_pdu) {
            struct smb2_pdu *pdu = ctx->next_pdu;
            ctx->next_pdu = NULL;
            if (pdu->cb)
                pdu->cb(ctx, SMB2_STATUS_SHUTDOWN, NULL, pdu->cb_data);
            smb2_free_pdu(ctx, pdu);
        }
        while (ctx->waitqueue) {
            struct smb2_pdu *pdu = ctx->waitqueue;
            ctx->waitqueue = pdu->next;
            if (pdu->cb)
                pdu->cb(ctx, SMB2_STATUS_SHUTDOWN, NULL, pdu->cb_data);
            if (pdu == ctx->pdu)
                ctx->pdu = NULL;
            smb2_free_pdu(ctx, pdu);
        }
    }
}

static struct smb2_pdu *enqueue_on_shutdown;
static void pending_cb_requeue(struct smb2_context *ctx, int status,
                               void *data, void *private_data)
{
    pending_cb(ctx, status, data, private_data);
    assert(enqueue_on_shutdown && ctx->outqueue == NULL);
    ctx->outqueue = enqueue_on_shutdown;
    enqueue_on_shutdown = NULL;
}

/* Verify that active partial reception, next_pdu and waitqueue are drained,
 * including a new request queued by a shutdown callback after outqueue ran. */
static void test_destroy_next_and_requeued(void)
{
    struct smb2_context ctx = {0};
    struct smb2_pdu active = {0}, next = {0}, waiting = {0}, late = {0};
    active.cb = next.cb = late.cb = pending_cb;
    waiting.cb = pending_cb_requeue;
    ctx.pdu = &active;
    ctx.next_pdu = &next;
    ctx.waitqueue = &waiting;
    enqueue_on_shutdown = &late;
    pending_callback_count = pending_freed_count = 0;
    destroy_queued_pdus(&ctx);
    assert(ctx.pdu == NULL && ctx.next_pdu == NULL);
    assert(ctx.waitqueue == NULL && ctx.outqueue == NULL);
    assert(enqueue_on_shutdown == NULL);
    assert(pending_callback_count == 4 && pending_freed_count == 4);
}

/* Unsent compound request: all callbacks run, but the entire compound is
 * freed once by smb2_free_pdu at the compound head. */
static void test_destroy_compound(void)
{
    struct smb2_context ctx = {0};
    struct smb2_pdu head = {0}, part = {0};
    head.cb = part.cb = pending_cb;
    head.next_compound = &part;
    ctx.outqueue = &head;
    pending_callback_count = pending_freed_count = 0;
    destroy_queued_pdus(&ctx);
    assert(ctx.outqueue == NULL);
    assert(pending_callback_count == 2 && pending_freed_count == 1);
}

static void test_timeout_then_destroy(void)
{
    struct smb2_context ctx = { .fd = 3, .timeout = 5 };
    struct sync_cb_data callback = { .is_finished = 0 };
    struct smb2_pdu timed_out = {0}, another = {0};

    timed_out.cb = pending_cb;
    another.cb = pending_cb;
    ctx.outqueue = &timed_out;
    timed_out.next = &another;

    tick = closes = close_saw_timeout_error = 0;
    mode = 0;
    active_callback = &callback;
    error_message[0] = 0;
    pending_callback_count = pending_freed_count = 0;

    assert(wait_for_reply(&ctx, &callback) == -1);
    assert(tick == 5 && closes == 1 && ctx.fd == -1);
    /* Close alone must not drain or free either queued request. */
    assert(pending_callback_count == 0 && pending_freed_count == 0);
    assert(ctx.outqueue == &timed_out);

    /* The synchronous caller owns only its own request. */
    sync_free_pending_pdu(&ctx, &timed_out);
    assert(ctx.outqueue == &another);
    assert(pending_callback_count == 1 && pending_freed_count == 1);

    /* Later destruction must visit just the other, still-queued request. */
    destroy_queued_pdus(&ctx);
    assert(ctx.outqueue == NULL);
    assert(pending_callback_count == 2 && pending_freed_count == 2);
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

    /* A partially decoded second PDU must also be detached. */
    ctx.next_pdu = &target;
    sync_free_pending_pdu(&ctx, &target);
    assert(ctx.next_pdu == NULL);
    assert(pending_callback_count == 4 && pending_freed_count == 4);
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
    test_timeout_then_destroy();
    test_destroy_next_and_requeued();
    test_destroy_compound();
    test_copy_callback_lifetime();
    scenario(0, -1, 1, 5);  /* Valid socket, no events: active PDU absent from timeout queues */
    scenario(1, 0, 0, 1);   /* Reply completes normally */
    scenario(2, 0, 0, 3);   /* Existing PDU timeout callback completes the request */
    scenario(3, -1, 1, 5);  /* Readable socket, but active reply never completes */
    puts("smb2 sync deadline: PASS");
    return 0;
}
'''

# Protect the three vendor sync copy wrappers: a timeout leaves a queued
# callback owning its cb_data until reply completion or context teardown.
# Both the cancellation path and callback's final free are required.
vendor_source = sync_source.read_text(encoding="utf-8")
for operation, following in (
    ("smb2_request_resume_key", "smb2_copychunk"),
    ("smb2_copychunk", "smb2_server_side_copy"),
    ("smb2_server_side_copy", "sync_echo_cb"),
):
    segment_start = vendor_source.index("int " + operation + "(")
    segment_end = vendor_source.index(following, segment_start + len(operation))
    operation_body = vendor_source[segment_start:segment_end]
    failure_branch = operation_body.split("rc = wait_for_reply(smb2, cb_data);", 1)[1].split("rc = cb_data->status;", 1)[0]
    assert "cb_data->status = SMB2_STATUS_CANCELLED;" in failure_branch
    assert "return rc;" in failure_branch
    assert "goto out;" not in failure_branch

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
        subprocess.run(
            [compiler, "-O0", "-Wall", "-Wextra", "-Wno-unused-parameter",
             str(src), "-o", str(exe)], check=True,
        )
        subprocess.run([str(exe)], check=True, timeout=15)


run(os.environ.get("CC", "cc"), "c",
    sync_harness + pending_helpers + sync + copy_stubs +
    "#define free tracked_copy_free\n" + copy_callback + copy_wrappers +
    "#undef free\n" + copy_tests + sync_main)
run(os.environ.get("CXX", "c++"), "cpp", net_harness + net_wait + net + net_main)
print("smb2 menu wait bounds: all passed")
