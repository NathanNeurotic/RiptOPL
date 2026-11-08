"""Exercise SMB2 sync waits, the IOP poll adapter, and network init against stalled stubs.

The host harness extracts production functions; tests fail if a refactor removes
the hard reply deadline, restores unlimited link retries, or reintroduces an
alarm-backed blocking select in the IOP socket poll path. A real console/server
test is still required to verify the complete PS2 networking stack.
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


def c_function(source: Path, signature: str) -> str:
    """Extract one C function by balancing its braces."""
    code = source.read_text(encoding="utf-8")
    first = code.index(signature)
    opening = code.index("{", first)
    depth = 0
    for position in range(opening, len(code)):
        if code[position] == "{":
            depth += 1
        elif code[position] == "}":
            depth -= 1
            if depth == 0:
                return code[first:position + 1]
    raise ValueError(f"Unterminated C function: {signature}")


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
open_callback = function(sync_source, "static void sync_open_cb(", "struct smb2fh *smb2_open(")
opendir_callback = function(sync_source, "static void sync_opendir_cb(", "struct smb2dir *smb2_opendir(")


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
#define SMB2_STATUS_SUCCESS 0
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
static int copy_state_frees, copy_result_frees;

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
    if (ptr != NULL)
        copy_result_frees++;
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
        copy_state_frees = copy_result_frees = closes = tick = 0;
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
        /* A late successful reply still owns allocator-backed output.
         * The cancelled callback must release both the output and itself. */
        void *late_data = malloc(32);
        assert(late_data != NULL);
        pending_copy_callback(&ctx, 0, late_data, pending_copy_state);
        assert(copy_state_frees == 1 && copy_result_frees == 1);
        assert(pending_copy_state == NULL);
    }
}
'''

chain_callback_stubs = r'''
static int callback_state_frees, late_dir_closed;
static void *pending_chain_state;
static void tracked_chain_free(void *ptr)
{
    assert(ptr == pending_chain_state);
    callback_state_frees++;
    pending_chain_state = NULL;
    free(ptr);
}
static void smb2_closedir(struct smb2_context *ctx, void *dir)
{
    (void)ctx;
    assert(dir != NULL);
    late_dir_closed++;
}
'''

chain_callback_tests = r'''
static void test_chained_callback_lifetime(void)
{
    struct smb2_context ctx = {0};
    struct sync_cb_data *state;
    int fake_dir = 7;

    callback_state_frees = late_dir_closed = 0;
    state = calloc(1, sizeof(*state));
    assert(state != NULL);
    pending_chain_state = state;
    state->status = SMB2_STATUS_CANCELLED;
    sync_open_cb(&ctx, SMB2_STATUS_SHUTDOWN, NULL, state);
    assert(callback_state_frees == 1 && pending_chain_state == NULL);

    state = calloc(1, sizeof(*state));
    assert(state != NULL);
    pending_chain_state = state;
    state->status = SMB2_STATUS_CANCELLED;
    sync_opendir_cb(&ctx, SMB2_STATUS_SHUTDOWN, NULL, state);
    assert(callback_state_frees == 2 && late_dir_closed == 0);

    state = calloc(1, sizeof(*state));
    assert(state != NULL);
    pending_chain_state = state;
    state->status = SMB2_STATUS_CANCELLED;
    sync_opendir_cb(&ctx, 0, &fake_dir, state);
    assert(callback_state_frees == 3 && late_dir_closed == 1);
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
    test_chained_callback_lifetime();
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

for function_name, next_name in (
    ("smb2_opendir", "static void sync_open_cb("),
    ("smb2_open", "static void sync_close_cb("),
):
    result_type = "struct smb2dir *" if function_name == "smb2_opendir" else "struct smb2fh *"
    body = vendor_source.split(result_type + function_name + "(", 1)[1].split(next_name, 1)[0]
    failed_wait = body.split("if (wait_for_reply(smb2, cb_data) < 0)", 1)[1].split("return NULL;", 1)[0]
    assert "cb_data->status = SMB2_STATUS_CANCELLED" in failed_wait
    assert "sync_free_pending_pdu(smb2, pdu)" in failed_wait
    assert "free(cb_data)" not in failed_wait

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


run(os.environ.get("CC", "cc"), "c",
    sync_harness + pending_helpers + sync + copy_stubs +
    "#define free tracked_copy_free\n" + copy_callback + copy_wrappers +
    "#undef free\n" + copy_tests +
    chain_callback_stubs + "#define free tracked_chain_free\n" +
    open_callback + opendir_callback + "#undef free\n" +
    chain_callback_tests + sync_main)
compat_source = ROOT / "modules/network/smb2man/libsmb2/lib/compat.c"
iop_poll = c_function(
    compat_source,
    "#ifdef SMB2_HAVE_OGC_POLL_H\nint poll(struct pollfd *fds, nfds_t nfds, int timo)",
)

poll_harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#define _IOP 1
#define FD_SETSIZE 32

typedef struct { unsigned int bits; } fd_set;
#define FD_ZERO(set) ((set)->bits = 0u)
static void test_fd_set(int fd, fd_set *set)
{
    assert(fd >= 0 && fd < FD_SETSIZE);
    set->bits |= (1u << fd);
}
static int test_fd_isset(int fd, const fd_set *set)
{
    assert(fd >= 0 && fd < FD_SETSIZE);
    return (set->bits & (1u << fd)) != 0u;
}
#define FD_SET(fd, set) test_fd_set((fd), (set))
#define FD_ISSET(fd, set) test_fd_isset((fd), (set))

struct timeval { long tv_sec; long tv_usec; };
struct pollfd { int fd; short events; short revents; };

#define POLLIN 0x0001
#define POLLPRI 0x0002
#define POLLOUT 0x0004
#define POLLHUP 0x0010
#define SMB2_VALID_SOCKET(fd) ((fd) >= 0)

typedef uint32_t u32;
typedef struct { u32 lo, hi; } iop_sys_clock_t;
static uint64_t virtual_us;
static unsigned int select_cost_us;
static void GetSystemTime(iop_sys_clock_t *now)
{
    uint64_t ticks = virtual_us * 36864ULL / 1000ULL;
    now->lo = (u32)ticks;
    now->hi = (u32)(ticks >> 32);
}
static void SysClock2USec(iop_sys_clock_t *clock, u32 *sec, u32 *usec)
{
    uint64_t ticks = ((uint64_t)clock->hi << 32) | clock->lo;
    *sec = (u32)(ticks / 36864000ULL);
    *usec = (u32)((ticks % 36864000ULL) * 1000000ULL / 36864000ULL);
}

static int select_calls, delay_calls, delayed_us;
static int ready_on_call, select_error;
static unsigned int ready_read_mask, ready_write_mask, ready_except_mask;

static int fake_select(int nfds, fd_set *readfds, fd_set *writefds,
                       fd_set *exceptfds, struct timeval *timeout)
{
    assert(nfds >= 0);
    // Every production probe must take the immediate non-allocating lwIP path.
    assert(timeout != NULL && timeout->tv_sec == 0 && timeout->tv_usec == 0);
    select_calls++;
    virtual_us += select_cost_us;
    if (select_error)
        return -1;
    if (select_calls != ready_on_call) {
        if (readfds) readfds->bits = 0;
        if (writefds) writefds->bits = 0;
        if (exceptfds) exceptfds->bits = 0;
        return 0;
    }
    if (readfds) readfds->bits &= ready_read_mask;
    if (writefds) writefds->bits &= ready_write_mask;
    if (exceptfds) exceptfds->bits &= ready_except_mask;
    return !!((readfds && readfds->bits) ||
              (writefds && writefds->bits) ||
              (exceptfds && exceptfds->bits));
}

static int DelayThread(unsigned int usec)
{
    assert(usec > 0);
    delay_calls++;
    delayed_us += (int)usec;
    virtual_us += usec;
    return 0;
}

#define select fake_select
'''

poll_main = r'''
static void reset_poll(void)
{
    select_calls = delay_calls = delayed_us = 0;
    virtual_us = select_cost_us = 0;
    ready_on_call = select_error = 0;
    ready_read_mask = ready_write_mask = ready_except_mask = 0;
}

int main(void)
{
    struct pollfd pfd;

    reset_poll();
    pfd = (struct pollfd){3, POLLIN, 0};
    assert(poll(&pfd, 1, 25) == 0);
    assert(select_calls == 6 && delay_calls == 5 && delayed_us == 25000);

    /* The low IOP clock word wraps during this wait: subtraction must borrow
       from the high word without changing the actual 25 ms deadline. */
    reset_poll();
    virtual_us = 116508000ULL;
    pfd = (struct pollfd){3, POLLIN, 0};
    assert(poll(&pfd, 1, 25) == 0);
    assert(select_calls == 6 && delay_calls == 5 && delayed_us == 25000);
    assert(virtual_us == 116533000ULL);

    /* Slow select probes count against the real timeout, not just DelayThread. */
    reset_poll();
    select_cost_us = 1000;
    pfd = (struct pollfd){3, POLLIN, 0};
    assert(poll(&pfd, 1, 25) == 0);
    assert(select_calls == 5 && delay_calls == 4 && virtual_us == 25000);

    /* A slower-than-expected probe still advances the deadline. */
    reset_poll();
    select_cost_us = 3000;
    pfd = (struct pollfd){3, POLLIN, 0};
    assert(poll(&pfd, 1, 25) == 0);
    assert(select_calls == 4 && delay_calls == 3 && virtual_us == 27000);

    reset_poll();
    select_cost_us = 1000;
    pfd = (struct pollfd){3, POLLIN, 0};
    assert(poll(&pfd, 1, 1) == 0);
    assert(select_calls == 1 && delay_calls == 0 && virtual_us == 1000);

    reset_poll();
    ready_on_call = 2;
    ready_read_mask = 1u << 3;
    pfd = (struct pollfd){3, POLLIN, 0};
    assert(poll(&pfd, 1, 1000) == 1);
    assert(pfd.revents == POLLIN && select_calls == 2 && delayed_us == 5000);

    reset_poll();
    ready_on_call = 1;
    ready_write_mask = 1u << 4;
    pfd = (struct pollfd){4, POLLOUT, 0};
    assert(poll(&pfd, 1, 1000) == 1);
    assert(pfd.revents == POLLOUT && select_calls == 1 && delay_calls == 0);

    reset_poll();
    pfd = (struct pollfd){3, POLLIN, 0};
    assert(poll(&pfd, 1, 0) == 0);
    assert(select_calls == 1 && delay_calls == 0);

    reset_poll();
    ready_on_call = 3;
    ready_read_mask = 1u << 5;
    pfd = (struct pollfd){5, POLLIN, 0};
    assert(poll(&pfd, 1, -1) == 1);
    assert(pfd.revents == POLLIN && select_calls == 3 && delayed_us == 10000);

    reset_poll();
    pfd = (struct pollfd){-1, POLLIN, 0};
    assert(poll(&pfd, 1, 0) == 0);
    assert(select_calls == 1);

    reset_poll();
    pfd = (struct pollfd){FD_SETSIZE, POLLIN, 0};
    assert(poll(&pfd, 1, 0) == -1);
    assert(select_calls == 0);

    reset_poll();
    select_error = 1;
    pfd = (struct pollfd){3, POLLIN, 0};
    assert(poll(&pfd, 1, 1000) == -1);
    assert(select_calls == 1 && delay_calls == 0);

    puts("SMB2 IOP poll bounds: PASS");
    return 0;
}
'''

run(os.environ.get("CC", "cc"), "c", poll_harness + iop_poll + poll_main)
print("smb2 menu wait bounds: all passed")
