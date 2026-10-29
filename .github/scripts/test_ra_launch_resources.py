"""Compile real EE-core RA resource paths against controlled IOP-heap failures.

The optional achievements module must never trap a game in MODLOAD's -400 loop.
The snapshot buffer must be proportional to the snapshot ra.c accepted, the
load argument must follow the RA_ARG_* layout raudp parses, and every module
failure, including a module that declines residency, must give the buffer back.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
iop = (root / "ee_core/src/iopmgr.c").read_text()
mod = (root / "ee_core/src/modmgr.c").read_text()


def function(source, signature):
    begin = source.index(signature)
    return source[begin:source.index("\n}", begin) + 2]


wanted = function(iop, "static int RA_TelemetryWanted(struct EECoreConfig_t *config)")
hex32 = function(iop, "static void RA_Hex32(unsigned int v, char *out)")
special_reset = function(iop, "static int ResetIopSpecial(")
load_begin = special_reset.index("    ra_snap_iop = 0;\n    if (RA_TelemetryWanted(config)")
load_end = special_reset.rindex("\n#endif")
load_body = special_reset[load_begin:load_end]
module_loader = function(mod, "int LoadOPLModule(int id, int mode, int arg_len, const char *args)")
transport = (function(mod, "static int IsRequiredTransport(int id)") + "\n" +
             function(mod, "static void RequiredTransportFailed(void)"))

harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "modules/network/common/ra_snap.h"
#define RETROACHIEVEMENTS 1
#define RA_PROBE 0
#define IPCONFIG_MAX_LEN 128
#define ETH_MODE 3
#define HTTP_MODE 4
#define OPL_MODULE_ID_RAUDP 111
#define OPL_MODULE_ID_PCCF 60
#define OPL_MODULE_ID_SMAP_UDPBD 61
#define OPL_MODULE_ID_UDPFS_SMAP 62
#define OPL_MODULE_ID_UDPFS_MINISTACK 63
#define OPL_MODULE_ID_UDPFS_BD 64
#define MODMGR 0
#define BGCOLND(...) ((void)0)
#define DBGCOL(...) ((void)0)
#define DBGCOL_BLNK(...) ((void)0)
typedef struct EECoreConfig_t {
    char GameID[16], raHost[16];
    int GameMode, raClientMode, raHostPinned;
} EECoreConfig_t;
static int moduleStartResult;
static int EnableDebug, module_result, module_start, module_calls, allocations, frees, fail_alloc, snap_bytes;
static unsigned last_size, ra_snap_iop;
static char last_args[256], g_ipconfig[IPCONFIG_MAX_LEN];
static int g_ipconfig_len, last_argc;
static char buffer[RA_SNAP_TOTAL];
static char module_data[8];
static void delay(int n) { (void)n; }
static int RA_SnapBytes(void) { return snap_bytes; }
static void *SifAllocIopHeap(unsigned n) {
    allocations++;
    last_size = n;
    assert(n <= sizeof(buffer));
    return fail_alloc ? NULL : buffer;
}
static void SifFreeIopHeap(void *p) { assert(p == buffer); frees++; }
static int GetOPLModInfo(int id, void **p, unsigned *size) {
    assert(id == OPL_MODULE_ID_RAUDP);
    *p = module_data;
    *size = sizeof(module_data);
    return 0;
}
static int LoadMemModule(int mode, void *p, unsigned size, int argc, const char *args) {
    assert(mode == 0 && p == module_data && size == sizeof(module_data));
    assert(argc < (int)sizeof(last_args));
    memcpy(last_args, args, argc);
    last_argc = argc;
    module_calls++;
    moduleStartResult = module_start;
    return module_result;
}
static void *RA_OverlayEventBuffer(void) { return (void *)0x00123440; }
"""
harness += wanted + "\n" + hex32 + "\n" + transport + "\n" + module_loader + "\n"
harness += "static void load_ra(EECoreConfig_t *config) {\n" + load_body + "\n}\n"
harness += r"""
static void reset(void) {
    EnableDebug=0; module_result=1; module_start=0; module_calls=allocations=frees=fail_alloc=0;
    last_size=0; ra_snap_iop=0; memset(last_args, 0, sizeof(last_args)); last_argc=0;
}
int main(void) {
    EECoreConfig_t c;
    memset(&c,0,sizeof(c));
    c.GameMode=3; c.raClientMode=1; c.raHostPinned=1;
    strcpy(c.GameID,"SLUS_212.69");
    strcpy(c.raHost,"192.168.1.5");
    memcpy(g_ipconfig, "192.168.1.9\0" "255.255.255.0\0" "192.168.1.1", 38);
    g_ipconfig_len=38;
    snap_bytes=697;
    reset(); load_ra(&c);
    assert(allocations==1 && module_calls==1 && !frees);
    assert(last_size==RA_SNAP_TOTAL_FOR(697) && last_size<RA_SNAP_TOTAL);
    assert(ra_snap_iop==(unsigned)(uintptr_t)buffer);
    assert(last_args[RA_ARG_EVENT-1]==',' && !memcmp(&last_args[RA_ARG_EVENT],"00123440",8));
    assert(last_args[RA_ARG_RX-1]==',' && last_args[RA_ARG_RX]=='0'+2+4);
    assert(last_args[RA_ARG_ID-1]==',' && !memcmp(&last_args[RA_ARG_ID],"SLUS_212.69    ",RA_ARG_ID_MAX));
    assert(last_args[RA_ARG_HOST-1]==',' && !strcmp(&last_args[RA_ARG_HOST],"192.168.1.5"));
    assert(last_argc==RA_ARG_HOST+12+38 && !memcmp(&last_args[RA_ARG_HOST+12],g_ipconfig,38));
    puts("PASS: a small Caduceus list takes only its rounded snapshot; the argument follows RA_ARG_*");

    reset(); strcpy(c.raHost,"255.255.255.255"); strcpy(c.GameID,"SLUS_212.69XXXX"); load_ra(&c);
    assert(!memcmp(&last_args[RA_ARG_ID],"SLUS_212.69XXXX",RA_ARG_ID_MAX) && !strcmp(&last_args[RA_ARG_HOST],"255.255.255.255"));
    assert(last_argc<=RA_ARG_MAX+1+38);
    puts("PASS: the longest serial and host stay inside RA_ARG_MAX");
    strcpy(c.raHost,"192.168.1.5"); strcpy(c.GameID,"SLUS_212.69");

    reset(); snap_bytes=0; load_ra(&c);
    assert(!allocations && !module_calls && !ra_snap_iop);
    snap_bytes=697;
    reset(); fail_alloc=1; load_ra(&c);
    assert(allocations==1 && module_calls==0 && ra_snap_iop==0);
    puts("PASS: rejected/untracked lists and out-of-memory launches stay untracked");

    reset(); module_result=-400; load_ra(&c);
    assert(allocations==1 && module_calls==1 && frees==1 && ra_snap_iop==0);
    reset(); module_result=-200; load_ra(&c);
    assert(frees==1 && ra_snap_iop==0);
    reset(); module_start=1; load_ra(&c);
    assert(frees==1 && ra_snap_iop==0);
    puts("PASS: -400, a missing dependency or a declined residency never hang and release the IOP heap");

    reset(); c.GameMode=0; c.raClientMode=0; c.raHostPinned=0; load_ra(&c);
    assert(last_args[RA_ARG_RX]=='1');
    reset(); c.GameMode=HTTP_MODE; load_ra(&c);
    assert(last_args[RA_ARG_RX]=='0');
    puts("PASS: Xerabora flags; network reads off while the game streams from SMB or HTTP");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="ra-launch-resources-") as temp:
    src = Path(temp) / "ra-launch-resources.c"
    exe = Path(temp) / "ra-launch-resources"
    src.write_text(harness)
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Werror", "-Wno-pointer-to-int-cast", "-Wno-unused-function",
                    "-I", str(root), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
