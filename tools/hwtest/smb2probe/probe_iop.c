/*
  smb2probe IOP helper. A SIF RPC server on its own channel and its own high-priority thread, so it
  answers while fileXio's single RPC thread is stuck inside smb2man. Each ALIVE call reports:

    [0] calls served        -- the IOP is running and SIF RPC is alive
    [1] heartbeat           -- ticks of a low-priority thread that sleeps 100 ms (DelayThread uses
                               an alarm): if it stops while [0] still counts, either a higher
                               priority thread is spinning or kernel alarms no longer fire
    [2] QueryTotalFreeMemSize()
    [3] QueryMaxFreeMemSize()
    [4] result of a SetAlarm/CancelAlarm round trip made right now (0 = alarms can be set)
    [5] bytes currently held by SQUEEZE

  SQUEEZE(target) allocates 4 KB blocks until total free IOP RAM is at most target bytes, to put
  smb2man under a known memory pressure. RELEASE frees them.
*/

#include <irx.h>
#include <loadcore.h>
#include <sifrpc.h>
#include <sysmem.h>
#include <thbase.h>
#include <intrman.h>

#include "probe_rpc.h"

IRX_ID("smb2probe", 1, 0);

static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static u32 rpc_buf[16] __attribute__((aligned(64)));

static volatile u32 served, heartbeat;
static void *held; // singly linked through the first word of each block
static u32 held_bytes;

static unsigned int probe_dummy_alarm(void *arg)
{
    (void)arg;
    return 0;
}

static void *probe_alloc(int size)
{
    int state;
    void *p;

    CpuSuspendIntr(&state);
    p = AllocSysMemory(ALLOC_FIRST, size, NULL);
    CpuResumeIntr(state);
    return p;
}

static void probe_free(void *p)
{
    int state;

    CpuSuspendIntr(&state);
    FreeSysMemory(p);
    CpuResumeIntr(state);
}

static void *probe_rpc(int fn, void *data, int size)
{
    u32 *b = (u32 *)data;
    iop_sys_clock_t clk;

    (void)size;
    served++;

    if (fn == PROBE_SQUEEZE) {
        while ((u32)QueryTotalFreeMemSize() > b[0]) {
            void *p = probe_alloc(PROBE_SQUEEZE_BLOCK);
            if (p == NULL)
                break;
            *(void **)p = held;
            held = p;
            held_bytes += PROBE_SQUEEZE_BLOCK;
        }
    } else if (fn == PROBE_RELEASE) {
        while (held != NULL) {
            void *next = *(void **)held;
            probe_free(held);
            held = next;
        }
        held_bytes = 0;
    }

    b[0] = served;
    b[1] = heartbeat;
    b[2] = QueryTotalFreeMemSize();
    b[3] = QueryMaxFreeMemSize();
    USec2SysClock(10 * 1000 * 1000, &clk);
    b[4] = (u32)SetAlarm(&clk, probe_dummy_alarm, NULL);
    if (b[4] == 0)
        CancelAlarm(probe_dummy_alarm, NULL);
    b[5] = held_bytes;
    return data;
}

static void probe_server_thread(void *arg)
{
    (void)arg;
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&queue, GetThreadId());
    sceSifRegisterRpc(&server, PROBE_RPC_ID, probe_rpc, rpc_buf, NULL, NULL, &queue);
    sceSifRpcLoop(&queue);
}

static void probe_heartbeat_thread(void *arg)
{
    (void)arg;
    for (;;) {
        DelayThread(100 * 1000);
        heartbeat++;
    }
}

static int probe_start_thread(void (*fn)(void *), int priority)
{
    iop_thread_t t;
    int tid;

    t.attr = TH_C;
    t.option = 0;
    t.thread = fn;
    t.stacksize = 0x800;
    t.priority = priority;
    tid = CreateThread(&t);
    return (tid > 0 && StartThread(tid, NULL) == 0) ? 0 : -1;
}

int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    // Above fileXio's RPC thread (40), lwIP's tcpip thread (0x18) and smb2man's keepalive (0x64),
    // so a busy network stack cannot starve the reply. The heartbeat sits below all of them.
    if (probe_start_thread(probe_server_thread, 0x10) < 0 || probe_start_thread(probe_heartbeat_thread, 0x78) < 0)
        return MODULE_NO_RESIDENT_END;
    return MODULE_RESIDENT_END;
}
