/*
  smb2probe: a small hardware test for the RiptOPL SMB2 menu freeze (#312). It runs the menu's own
  IOP modules, embedded from the same build, without the menu around them, and reports which of
  three mechanisms stalls the share handshake (P0_SMB2_FREEZE_RCA.md §3):

    M1  OPENSHARE never returns, but the IOP is alive (helper RPC still answers)
    M2  the IOP stops answering altogether (helper RPC silent)
    --  OPENSHARE completes: the fault needs something only the full menu has (M3 or a menu state)

  Arguments (ps2client execee host:program.elf ARG...), key=value:
    ip=  mask=  gw=        static address for the console (or "dhcp")
    srv=  port=  share=    the SMB2 server (PS2-Servers); port defaults to 445
    user=  pass=           empty user = guest
    squeeze=BYTES          before LOGON, hold IOP RAM until only BYTES are free (0 = off)
    back=PATH              the ELF to return to afterwards, e.g. mc0:/BOOT/PS2LINK.ELF
    dump                   print mc0:/SMB2PROBE.TXT over stdout and stop (no IOP reset)

  Output: the screen, and mc0:/SMB2PROBE.TXT (written through mcserv's own RPC channel, so it still
  works while fileXio is held by smb2man). The IOP is reset first, so ps2link's own stdout is gone
  for the run; "back=" reloads ps2link at the end, and a watchdog does it after 60 s whatever happens.
*/

#define NEWLIB_PORT_AWARE // fileXio is used only for its devctl and no-wait mode
#include <kernel.h>
#include <delaythread.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopheap.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <fileXio_rpc.h>
#include <libmc.h>
#include <netman.h>
#include <ps2ips.h>
#include <lwip/ip_addr.h> // struct ip4_addr, IP4_ADDR: ps2ip.h no longer pulls them in
#include <ps2ip.h>
#include <ps2smb.h>
#include <debug.h>
#include <elf-loader.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>

#include "probe_rpc.h"
#include "modules/network/smb2man/smb2man_devctl.h"

#define IRX(name)                      \
    extern unsigned char name##_irx[]; \
    extern unsigned int size_##name##_irx
IRX(iomanx);
IRX(filexio);
IRX(sio2man);
IRX(mcman);
IRX(mcserv);
IRX(ps2dev9);
IRX(netman);
IRX(smsutils);
IRX(smap);
IRX(ps2ip);
IRX(ps2ips);
IRX(httpclient);
IRX(smb2man);
IRX(smb2probe_iop);

#define LOG_PATH     "/SMB2PROBE.TXT"
#define OPENSHARE_MS 30000
#define WATCHDOG_SEC 60
#define SAMPLE_MS    500

static char logbuf[24 * 1024];
static int loglen;
static int mc_ready;
static char back_path[256];

static SifRpcClientData_t probe_cd __attribute__((aligned(64)));
static u32 probe_buf[16] __attribute__((aligned(64)));
static int probe_bound;

static unsigned int ms_now(void)
{
    return (unsigned int)((unsigned long long)clock() * 1000 / CLOCKS_PER_SEC);
}

// Wait for the pending libmc call, at most ms. libmc's own blocking wait would hang with the IOP.
static int mc_wait(int ms, int *result)
{
    unsigned int start = ms_now();
    int cmd;

    for (;;) {
        int st = mcSync(MC_NOWAIT, &cmd, result);
        if (st == 1)
            return 0;
        if (st < 0) // nothing pending: the call was refused
            return -1;
        if ((int)(ms_now() - start) >= ms)
            return -1;
        DelayThread(5 * 1000);
    }
}

// Rewrite the whole log file. Each write is bounded, so a stuck IOP only loses the file, not the run.
static void log_flush(void)
{
    int fd, r;

    if (!mc_ready)
        return;
    mcDelete(0, 0, LOG_PATH);
    if (mc_wait(2000, &r) < 0)
        goto stuck;
    mcOpen(0, 0, LOG_PATH, O_WRONLY | O_CREAT);
    if (mc_wait(2000, &fd) < 0)
        goto stuck;
    if (fd < 0)
        return;
    mcWrite(fd, logbuf, loglen);
    if (mc_wait(5000, &r) < 0)
        goto stuck;
    mcClose(fd);
    mc_wait(2000, &r);
    return;
stuck:
    mc_ready = 0; // the memory card channel is gone too: stop trying
    scr_printf("(memory card write timed out; log not saved)\n");
}

static void plog(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= (int)sizeof(line))
        n = sizeof(line) - 1;
    scr_printf("%s", line);
    if (loglen + n < (int)sizeof(logbuf)) {
        memcpy(logbuf + loglen, line, n);
        loglen += n;
    }
}

// One helper call. 0 on a reply within ms, -1 when the IOP did not answer.
static int probe_call(int fn, u32 arg, int ms)
{
    unsigned int start = ms_now();

    if (!probe_bound)
        return -1;
    probe_buf[0] = arg;
    if (SifCallRpc(&probe_cd, fn, SIF_RPC_M_NOWAIT, probe_buf, 16, probe_buf, 32, NULL, NULL) < 0)
        return -1;
    while (SifCheckStatRpc(&probe_cd)) {
        if ((int)(ms_now() - start) >= ms)
            return -1;
        DelayThread(1000);
    }
    return 0;
}

static void probe_sample(const char *what)
{
    if (probe_call(PROBE_ALIVE, 0, 1000) == 0)
        plog("  [%6u ms] %-26s rpc %u, heartbeat %u, free %u B, largest %u B, SetAlarm %d, held %u B\n",
             ms_now(), what, probe_buf[0], probe_buf[1], probe_buf[2], probe_buf[3], (int)probe_buf[4], probe_buf[5]);
    else
        plog("  [%6u ms] %-26s IOP HELPER DID NOT ANSWER within 1 s\n", ms_now(), what);
}

static int load(const char *name, void *irx, unsigned int size)
{
    int ret = 0, id = SifExecModuleBuffer(irx, size, 0, NULL, &ret);

    plog("load %-10s id %d ret %d\n", name, id, ret);
    if (probe_bound)
        probe_sample(name);
    return (id >= 0 && ret != 1) ? 0 : -1; // ret 1 = MODULE_NO_RESIDENT_END
}

static void go_back(void)
{
    char *argv[1];
    unsigned int start;

    log_flush();
    if (back_path[0] == '\0') {
        plog("no back= path: power-cycle the console\n");
        SleepThread();
    }
    plog("returning to %s\n", back_path);
    // fileXio may still be held by a stuck smb2man call, so reset the IOP before reading the ELF,
    // and bound the wait: an IOP that never comes back means a manual reset.
    fileXioExit();
    SifExitRpc();
    SifInitRpc(0);
    start = ms_now();
    while (!SifIopReset("", 0)) {
        if ((int)(ms_now() - start) > 5000)
            goto dead;
    }
    while (!SifIopSync()) {
        if ((int)(ms_now() - start) > 10000)
            goto dead;
    }
    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();
    sbv_patch_enable_lmb();
    SifExecModuleBuffer(iomanx_irx, size_iomanx_irx, 0, NULL, NULL);
    SifExecModuleBuffer(filexio_irx, size_filexio_irx, 0, NULL, NULL);
    fileXioInit();
    SifExecModuleBuffer(sio2man_irx, size_sio2man_irx, 0, NULL, NULL);
    SifExecModuleBuffer(mcman_irx, size_mcman_irx, 0, NULL, NULL);
    SifExecModuleBuffer(mcserv_irx, size_mcserv_irx, 0, NULL, NULL);
    argv[0] = back_path;
    LoadELFFromFile(back_path, 1, argv); // resets the IOP again and starts the ELF; returns only on failure
    scr_printf("could not start %s: power-cycle the console\n", back_path);
    SleepThread();
dead:
    scr_printf("the IOP did not come back from reset: power-cycle the console\n");
    SleepThread();
}

// EE watchdog: an alarm-driven thread wake-up, independent of everything the IOP does.
extern void *_gp;
static int main_tid, dog_tid;
static volatile int dog_fired;
static u8 dog_stack[16 * 1024] __attribute__((aligned(16)));

static void watchdog_thread(void *arg)
{
    (void)arg;
    DelayThread(WATCHDOG_SEC * 1000 * 1000);
    dog_fired = 1;
    SuspendThread(main_tid);
    plog("WATCHDOG: %d s elapsed, giving up\n", WATCHDOG_SEC);
    go_back();
}

static void start_watchdog(void)
{
    ee_thread_t t;

    main_tid = GetThreadId();
    memset(&t, 0, sizeof(t));
    t.func = watchdog_thread;
    t.stack = dog_stack;
    t.stack_size = sizeof(dog_stack);
    t.gp_reg = &_gp;
    t.initial_priority = 1; // above main
    dog_tid = CreateThread(&t);
    if (dog_tid >= 0)
        StartThread(dog_tid, NULL);
}

static const char *arg_val(int argc, char *argv[], const char *key, const char *def)
{
    size_t k = strlen(key);
    int i;

    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], key, k) && argv[i][k] == '=')
            return argv[i] + k + 1;
    return def;
}

static int has_arg(int argc, char *argv[], const char *flag)
{
    int i;

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], flag))
            return 1;
    return 0;
}

// "dump": runs under ps2link without touching the IOP, and prints the last run's log.
static void dump(void)
{
    static char buf[sizeof(logbuf) + 1];
    int fd, n = 0, r;

    SifInitRpc(0);
    SifLoadModule("rom0:XSIO2MAN", 0, NULL);
    SifLoadModule("rom0:XMCMAN", 0, NULL);
    SifLoadModule("rom0:XMCSERV", 0, NULL);
    if (mcInit(MC_TYPE_XMC) < 0) {
        printf("smb2probe dump: mcInit failed\n");
        SleepThread();
    }
    mcOpen(0, 0, LOG_PATH, O_RDONLY);
    if (mc_wait(3000, &fd) == 0 && fd >= 0) {
        mcRead(fd, buf, sizeof(buf) - 1);
        if (mc_wait(5000, &n) < 0 || n < 0)
            n = 0;
        mcClose(fd);
        mc_wait(2000, &r);
    }
    buf[n] = '\0';
    printf("----- mc0:%s (%d bytes) -----\n%s\n----- end -----\n", LOG_PATH, n, buf);
    SleepThread();
}

static int link_up(void)
{
    return NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP;
}

static void parse_ip(const char *txt, struct ip4_addr *out)
{
    unsigned int a = 0, b = 0, c = 0, d = 0;

    sscanf(txt, "%u.%u.%u.%u", &a, &b, &c, &d);
    IP4_ADDR(out, a, b, c, d);
}

static int setup_ip(int argc, char *argv[])
{
    t_ip_info info;
    struct ip4_addr a;
    int dhcp = has_arg(argc, argv, "dhcp");
    unsigned int start;

    if (ps2ip_getconfig("sm0", &info) < 0)
        return -1;
    if (dhcp) {
        memset(&info.ipaddr, 0, sizeof(info.ipaddr));
        memset(&info.netmask, 0, sizeof(info.netmask));
        memset(&info.gw, 0, sizeof(info.gw));
        info.dhcp_enabled = 1;
    } else {
        parse_ip(arg_val(argc, argv, "ip", "192.168.1.10"), &a);
        memcpy(&info.ipaddr, &a, sizeof(a));
        parse_ip(arg_val(argc, argv, "mask", "255.255.255.0"), &a);
        memcpy(&info.netmask, &a, sizeof(a));
        parse_ip(arg_val(argc, argv, "gw", "192.168.1.1"), &a);
        memcpy(&info.gw, &a, sizeof(a));
        info.dhcp_enabled = 0;
    }
    if (ps2ip_setconfig(&info) < 0)
        return -1;

    start = ms_now();
    while (!link_up()) {
        if ((int)(ms_now() - start) > 15000) {
            plog("link did not come up in 15 s\n");
            return -1;
        }
        DelayThread(100 * 1000);
    }
    plog("link up after %u ms\n", ms_now() - start);
    if (dhcp) {
        start = ms_now();
        for (;;) {
            if (ps2ip_getconfig("sm0", &info) >= 0 && info.dhcp_status == DHCP_STATE_BOUND)
                break;
            if ((int)(ms_now() - start) > 15000) {
                plog("DHCP did not bind in 15 s\n");
                return -1;
            }
            DelayThread(100 * 1000);
        }
        plog("DHCP bound after %u ms\n", ms_now() - start);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    static smbLogOn_in_t logon;
    static smbOpenShare_in_t share;
    u32 mem[2];
    int ret, done, rv;
    unsigned int start, next;
    unsigned long squeeze;

    if (has_arg(argc, argv, "dump"))
        dump();

    strncpy(back_path, arg_val(argc, argv, "back", ""), sizeof(back_path) - 1);

    init_scr();
    scr_printf("smb2probe\n");
    start_watchdog();

    // The menu's IOP reset (src/platform/system.cpp sysReset).
    SifInitRpc(0);
    while (!SifIopReset("", 0))
        ;
    while (!SifIopSync())
        ;
    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    load("iomanx", iomanx_irx, size_iomanx_irx);
    load("filexio", filexio_irx, size_filexio_irx);
    fileXioInit();
    load("sio2man", sio2man_irx, size_sio2man_irx);
    load("mcman", mcman_irx, size_mcman_irx);
    load("mcserv", mcserv_irx, size_mcserv_irx);
    mc_ready = mcInit(MC_TYPE_XMC) >= 0;
    if (load("smb2probe", smb2probe_iop_irx, size_smb2probe_iop_irx) == 0) {
        int tries;
        for (tries = 0; tries < 100 && !probe_bound; tries++) {
            if (SifBindRpc(&probe_cd, PROBE_RPC_ID, 0) >= 0 && probe_cd.server != NULL)
                probe_bound = 1;
            else
                DelayThread(10 * 1000);
        }
    }
    plog("helper bound: %s\n", probe_bound ? "yes" : "NO (M1/M2 cannot be told apart)");
    probe_sample("baseline");
    log_flush();

    // The menu's network order (src/sources/netsupport.cpp loadModules).
    if (load("ps2dev9", ps2dev9_irx, size_ps2dev9_irx) < 0 || load("netman", netman_irx, size_netman_irx) < 0)
        goto out;
    NetManInit();
    load("smsutils", smsutils_irx, size_smsutils_irx);
    if (load("smap", smap_irx, size_smap_irx) < 0 || load("ps2ip", ps2ip_irx, size_ps2ip_irx) < 0)
        goto out;
    load("ps2ips", ps2ips_irx, size_ps2ips_irx);
    load("httpclient", httpclient_irx, size_httpclient_irx);
    ps2ip_init();
    if (setup_ip(argc, argv) < 0)
        goto out;
    probe_sample("network up");
    if (load("smb2man", smb2man_irx, size_smb2man_irx) < 0) {
        plog("smb2man did not load: the menu would fall back to SMB1 here\n");
        goto out;
    }
    log_flush();

    squeeze = strtoul(arg_val(argc, argv, "squeeze", "0"), NULL, 0);
    if (squeeze > 0) {
        probe_call(PROBE_SQUEEZE, (u32)squeeze, 3000);
        probe_sample("after squeeze");
    }

    memset(&logon, 0, sizeof(logon));
    strncpy(logon.serverIP, arg_val(argc, argv, "srv", "192.168.1.100"), sizeof(logon.serverIP) - 1);
    logon.serverPort = atoi(arg_val(argc, argv, "port", "445"));
    strncpy(logon.User, arg_val(argc, argv, "user", ""), sizeof(logon.User) - 1);
    strncpy(logon.Password, arg_val(argc, argv, "pass", ""), sizeof(logon.Password) - 1);
    logon.PasswordType = logon.Password[0] ? PLAINTEXT_PASSWORD : NO_PASSWORD;
    memset(&share, 0, sizeof(share));
    strncpy(share.ShareName, arg_val(argc, argv, "share", "games"), sizeof(share.ShareName) - 1);
    strncpy(share.Password, logon.Password, sizeof(share.Password) - 1);
    share.PasswordType = logon.PasswordType;

    rv = fileXioDevctl("smb0:", SMB2_DEVCTL_GETIOPFREE, NULL, 0, mem, sizeof(mem));
    plog("smb2man GETIOPFREE %d: free %u B, largest %u B\n", rv, rv == 0 ? mem[0] : 0, rv == 0 ? mem[1] : 0);
    rv = fileXioDevctl("smb0:", SMB_DEVCTL_LOGON, &logon, sizeof(logon), NULL, 0);
    plog("LOGON %s:%d user '%s' -> %d\n", logon.serverIP, logon.serverPort, logon.User, rv);
    log_flush();
    if (rv < 0)
        goto out;

    // OPENSHARE without blocking the EE: fileXio's no-wait mode, polled, the helper sampled meanwhile.
    plog("OPENSHARE '%s' (limit %d ms)\n", share.ShareName, OPENSHARE_MS);
    log_flush();
    fileXioSetBlockMode(FXIO_NOWAIT);
    fileXioDevctl("smb0:", SMB_DEVCTL_OPENSHARE, &share, sizeof(share), NULL, 0);
    start = ms_now();
    next = start;
    done = 0;
    ret = 0;
    while (!done && (int)(ms_now() - start) < OPENSHARE_MS) {
        if (fileXioWaitAsync(FXIO_NOWAIT, &ret) == FXIO_COMPLETE) {
            done = 1;
            break;
        }
        if ((int)(ms_now() - next) >= 0) {
            probe_sample("OPENSHARE pending");
            next += SAMPLE_MS;
        }
        DelayThread(10 * 1000);
    }

    if (done) {
        fileXioSetBlockMode(FXIO_WAIT);
        plog("OPENSHARE returned %d after %u ms\n", ret, ms_now() - start);
        rv = fileXioDevctl("smb0:", SMB2_DEVCTL_GETDIALECT, NULL, 0, NULL, 0);
        plog("dialect 0x%04x\n", rv > 0 ? rv : 0);
        probe_sample("after OPENSHARE");
        plog("VERDICT: OPENSHARE completes here (%s). The freeze needs something only the menu has.\n",
             ret >= 0 ? "share open" : "clean error");
    } else {
        probe_sample("OPENSHARE limit");
        if (probe_call(PROBE_ALIVE, 0, 1000) == 0)
            plog("VERDICT: M1. OPENSHARE did not return in %d ms and the IOP is alive (see SetAlarm and heartbeat above).\n", OPENSHARE_MS);
        else
            plog("VERDICT: M2. OPENSHARE did not return and the IOP stopped answering.\n");
    }

out:
    plog("end at %u ms\n", ms_now());
    if (!dog_fired) {
        TerminateThread(dog_tid);
        go_back();
    }
    SleepThread();
    return 0;
}
