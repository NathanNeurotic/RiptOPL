"""Execute the production ORBIT Neutrino RA handoff with deterministic filesystem/network failures.

Ported from the original's test_neutrino_ra.py. Additions: the runtime marker must match in full (the
original compared 26 of its 28 bytes, so "v1" also accepted "v12"), the flat layout (no config/ folder)
writes next to neutrino.elf, and the RA files land beside the cheats file.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hostmap  # noqa: F401,E402  (host::ui().warningId -> guiWarning(_l(...)))

root = Path(__file__).resolve().parents[2]
platform = (root / "src/platform/sysneutrino.cpp").read_text(encoding="utf-8")
sources = (root / "src/sources/supportbase.cpp").read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


functions = "\n".join([function(platform, s) for s in (
    "static const char *sysLastPathSeparator(",
    "static int neutrinoArgActiveValue(",
    "static int sysNeutrinoCheatPath(",
    "static int sysNeutrinoCfgNameSafe(",
    "static int sysNeutrinoRADir(",
    "int sysNeutrinoRARuntime(",
    "static int sysNeutrinoRAWrite(",
    "int sysNeutrinoHandRA(",
)] + [function(sources, "void sbNeutrinoPrepareRA(")])
limit = re.search(r"^#define RA_ORBIT_SNAPSHOT_MAX .*$", sources, re.M).group(0)

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "modules/network/common/ra_watch.h"
#define NEUTRINO_CHEAT_CFG "riptopl-cheats"
#define RA_MODE_XERABORA 0
#define RA_MODE_CADUCEUS 1
#define _STR_RA_NEUTRINO_UNTRACKED 1
#define LOG(...) ((void)0)
@LIMIT@
static char gNeutrinoArgs[256], sysNeutrinoCheatArg[24], sysNeutrinoCheatStartup[32];
static char sysNeutrinoRAArg[336], sysNeutrinoRAStartup[32];
static int gRATelemetry=1, gRAMode, gRAHostIp[4]={192,168,1,2}, ps2_ip[4]={192,168,1,10}, ps2_ip_use_dhcp;
static int count=2, bytes=5, warning, networkCalls, writes, missingMarker, failWrite, failClose, flat;
static int nicBusy, sessionFail; /* a resident UDP transport; a Caduceus bridge reporting OFFLINE */
static const char *marker="RiptOPL ORBIT RA handoff v1\nORBIT credits\n";
static unsigned int entries[2]={RA_WATCH_PACK(0x100000,4),RA_WATCH_PACK(0x100004,1)};
static char cfg[2048], watch[128], tomlPath[512], wlPath[512];
static int watchLen;
static int fake_open(const char *path,int flags,...) {
    if (flags & O_WRONLY) {
        writes++;
        snprintf(strstr(path,".wl") ? wlPath : tomlPath, 512, "%s", path);
        return strstr(path,".wl") ? 3 : 4;
    }
    if (strstr(path,"ORBIT-RA.txt")) return missingMarker ? -1 : 1;
    if (flat && strstr(path,"system.toml")) return -1;
    return 2;
}
static int fake_read(int fd,void *out,int size) {
    (void)fd; int n=strlen(marker); if(n>size)n=size; memcpy(out,marker,n);return n;
}
static int fake_write(int fd,const void *in,int size) {
    if(failWrite)return size-1;
    if(fd==3) { assert(size<128); memcpy(watch,in,size);watchLen=size; }
    else { assert(size<2048);memcpy(cfg,in,size);cfg[size]=0; }
    return size;
}
static int fake_close(int fd) { return failClose && fd>=3 ? -1 : 0; }
static int fake_unlink(const char *path) { (void)path;return 0; }
#define open fake_open
#define read fake_read
#define write fake_write
#define close fake_close
#define unlink fake_unlink
static int sbReadWatchList(const char *prefix,const char *startup) { assert(prefix); return strcmp(startup,"SLUS_123.45") ? 0 : count; }
static int GetWatchCount(void){return count;}
static int GetWatchBytes(void){return bytes;}
static unsigned int *GetWatchList(void){return entries;}
static int raNetNicBusy(void){return nicBusy;}
/* The production raLaunchNetworkUp clears the list and shows its own notice on a failed session. */
static void raLaunchNetworkUp(void){networkCalls++; if(sessionFail) count=0;}
static int ethGetNetConfig(unsigned char *ip,unsigned char *mask,unsigned char *gateway) {
    (void)mask;(void)gateway;for(int i=0;i<4;i++)ip[i]=ps2_ip[i];return 0;
}
static unsigned int raNetPeerIP(void){return 0;}
static const char *_l(int x){(void)x;return "untracked";}
static void guiWarning(const char *s,int seconds){(void)s;(void)seconds;warning++;}
@FUNCTIONS@
static void prepare(const char *driver,const char *args) {
    writes=networkCalls=warning=0;watchLen=0;
    sbNeutrinoPrepareRA("mmce1:/Games/","SLUS_123.45",driver,"mmce1:/neutrino/neutrino.elf",args);
}
int main(void) {
    prepare("mmce", "-cfg=mine");
    assert(sysNeutrinoRAArg[0]);assert(networkCalls==1 && writes==2 && !warning);
    assert(!strcmp(sysNeutrinoRAArg,"-ra=mmce1:/neutrino/config/ript-ra.wl"));
    assert(!strcmp(wlPath,"mmce1:/neutrino/config/ript-ra.wl") && !strcmp(tomlPath,"mmce1:/neutrino/config/ript-ra.toml"));
    assert(strstr(cfg,"depends = [\"i_dev9_hidden\", \"mine\"]"));assert(strstr(cfg,"file = \"smap.irx\""));
    assert(strstr(cfg,"args = [\"ip=192.168.1.10\"]"));
    struct ra_watch_file h;memcpy(&h,watch,sizeof(h));
    assert(h.magic==RA_WATCH_MAGIC && h.version==RA_WATCH_VERSION && h.count==2 && h.bytes==5);
    assert(watchLen==(int)sizeof(h)+8 && !memcmp(watch+sizeof(h),entries,8));
    prepare("udpfs", "");
    assert(sysNeutrinoRAArg[0] && networkCalls==0 && strstr(cfg,"depends = []"));
    assert(!strstr(cfg,"smap.irx") && !strstr(cfg,"ministack.irx") && strstr(cfg,"load_order = 30"));
    assert(strstr(cfg,"srv=192.168.1.2") && strstr(cfg,"me=192.168.1.10"));
    prepare("udpfs", "-cfg=mine");assert(strstr(cfg,"depends = [\"mine\"]"));
    prepare("usb", "-cwd=mass1:/custom");
    assert(!strcmp(sysNeutrinoRAArg,"-ra=mass1:/custom/config/ript-ra.wl"));
    strcpy(sysNeutrinoCheatArg,"-cfg=riptopl-cheats");strcpy(sysNeutrinoCheatStartup,"SLUS_123.45");
    prepare("usb", "-cfg=mine");assert(strstr(cfg,"\"riptopl-cheats\""));
    /* Cheats written for another game are not this launch's: depend on the user's config instead. */
    strcpy(sysNeutrinoCheatStartup,"SLUS_999.99");
    prepare("usb", "-cfg=mine");assert(strstr(cfg,"\"mine\"") && !strstr(cfg,"riptopl-cheats"));
    sysNeutrinoCheatArg[0]=0;
    /* Flat layout: no config/system.toml, so Neutrino reads its configs beside neutrino.elf. */
    flat=1;writes=networkCalls=warning=0;
    sbNeutrinoPrepareRA("mmce1:/Games/","SLUS_123.45","usb","mass0:/neutrino.elf","");
    assert(!strcmp(sysNeutrinoRAArg,"-ra=mass0:/ript-ra.wl") && !strcmp(tomlPath,"mass0:/ript-ra.toml") && !warning);flat=0;
    bytes=RA_ORBIT_SNAPSHOT_MAX+1;prepare("usb", "");assert(!sysNeutrinoRAArg[0] && !writes && warning);
    bytes=RA_ORBIT_SNAPSHOT_MAX;prepare("usb", "");assert(sysNeutrinoRAArg[0] && !warning);bytes=5;
    /* A resident UDP transport owns the NIC: untracked with the notice, without raising the network. */
    nicBusy=1;prepare("usb", "");assert(!sysNeutrinoRAArg[0] && !writes && warning && !networkCalls);nicBusy=0;
    missingMarker=1;prepare("usb", "");assert(!sysNeutrinoRAArg[0] && !writes && warning);missingMarker=0;
    /* A game with no watch list launches quietly, even where the RA runtime is absent. */
    missingMarker=1;warning=0;sbNeutrinoPrepareRA("mmce1:/Games/","SLUS_999.99","usb","mmce1:/neutrino/neutrino.elf","");
    assert(!sysNeutrinoRAArg[0] && !warning);missingMarker=0;
    marker="not a supported runtime";prepare("usb", "");assert(!sysNeutrinoRAArg[0] && !writes && warning);
    marker="RiptOPL ORBIT RA handoff v12\n";prepare("usb", "");assert(!sysNeutrinoRAArg[0] && !writes && warning);
    marker="RiptOPL ORBIT RA handoff v1\n";prepare("usb", "");assert(sysNeutrinoRAArg[0]);
    ps2_ip_use_dhcp=1;prepare("udpfs", "");assert(!sysNeutrinoRAArg[0] && !writes && !networkCalls && warning);ps2_ip_use_dhcp=0;
    failWrite=1;prepare("usb", "");assert(!sysNeutrinoRAArg[0] && warning);failWrite=0;
    failClose=1;prepare("usb", "");assert(!sysNeutrinoRAArg[0] && warning);failClose=0;
    prepare("usb", "-cwd=hdd0:");assert(!sysNeutrinoRAArg[0] && !writes && warning);
    prepare("usb", "-cwd=mass0:\\neutrino");assert(!sysNeutrinoRAArg[0] && !writes && warning);
    prepare("usb", "-cfg=bad\"name");assert(!sysNeutrinoRAArg[0] && warning);
    prepare("usb", "-cfg=ript-ra");assert(!sysNeutrinoRAArg[0] && warning);
    gRAHostIp[0]=0;gRAHostIp[1]=0;gRAHostIp[2]=0;gRAHostIp[3]=0;
    prepare("udpfs", "");assert(!sysNeutrinoRAArg[0] && warning);
    gRAHostIp[0]=127;prepare("udpfs", "");assert(!sysNeutrinoRAArg[0] && warning);
    gRAHostIp[0]=224;prepare("udpfs", "");assert(!sysNeutrinoRAArg[0] && warning);
    gRAHostIp[0]=192;gRAHostIp[1]=168;gRAHostIp[2]=1;gRAHostIp[3]=2;
    ps2_ip[0]=0;ps2_ip[1]=0;ps2_ip[2]=0;ps2_ip[3]=0;
    prepare("udpfs", "");assert(!sysNeutrinoRAArg[0] && warning);
    ps2_ip[0]=192;ps2_ip[1]=168;ps2_ip[2]=1;ps2_ip[3]=10;
    gRATelemetry=0;prepare("usb", "");assert(!sysNeutrinoRAArg[0] && !writes && !warning && !networkCalls);gRATelemetry=1;
    gRAMode=RA_MODE_CADUCEUS;prepare("usb", "");
    assert(sysNeutrinoRAArg[0] && networkCalls==1 && writes==2 && !warning);
    assert(strstr(cfg,"srv=192.168.1.2") && strstr(cfg,"raagent.irx"));
    prepare("udpfs", "");assert(sysNeutrinoRAArg[0] && networkCalls==0);
    sessionFail=1;prepare("usb", "");
    assert(!sysNeutrinoRAArg[0] && !writes && !warning); /* raLaunchNetworkUp already gave the notice */
    sessionFail=0;count=2;
    gRAMode=2;prepare("usb", "");assert(!sysNeutrinoRAArg[0] && !writes && !warning); /* unknown mode */
    puts("ORBIT handoff: device paths, network ownership, config/cheats, watch limits and failures passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "handoff.cpp"
    exe = Path(tmp) / "handoff.exe"
    c.write_text(harness.replace("@FUNCTIONS@", functions).replace("@LIMIT@", limit))
    result = subprocess.run(["g++", "-x", "c++", "-Wall", "-Werror", "-Wno-unused-function", "-I", str(root), str(c), "-o", str(exe)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    subprocess.run([str(exe)], check=True)

for name in ("bdmlaunch.cpp", "mmcesupport.cpp", "hddsupport.cpp", "udpfssupport.cpp"):
    leg = (root / "src/sources" / name).read_text(encoding="utf-8")
    call = leg.index("sbNeutrinoPrepareRA(")
    assert leg.rfind("#ifdef RETROACHIEVEMENTS", 0, call) > leg.rfind("#endif", 0, call), name
    assert call < leg.index("sysNeutrinoArgsPreflight(") < leg.index("sysLaunchNeutrino("), name
print("PASS: all four launch legs prepare RA before argument-budget validation and teardown")
