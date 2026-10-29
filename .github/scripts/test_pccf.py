"""Compile CF production drivers and ownership/eligibility checks on the host.

Ported from the original's test of the same name (PR #955). The EE pieces are C++ here, so the
ownership harness builds with g++; the IOP driver tests in tests/pccf stay C. NetSupport::loadModules
is a class member, so its DEV9 refusal is checked by source order instead of being compiled. The
original's Refresh assertion is not ported: the CF page only exists once the driver loaded, so a
reprobe from it can never run.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def read(path):
    return (root / path).read_text(encoding='utf-8')


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


system = read('src/platform/system.cpp')
bdm = read('src/sources/bdmsupport.cpp')
ownership = '\n'.join(function(system, name) for name in (
    'int sysTryClaimPccf(', 'int sysIsPccfOwned(', 'void sysReleasePccf(', 'int sysInitDev9(', 'void sysShutdownDev9('))
candidate = function(bdm, 'static int bdmPccfCandidate(')
shutdown = function(read('modules/iopcore/cdvdman/device-bdm.c'), 'void DeviceDeinit(')
# Reset must never infer hardware power-off from a software reboot.
assert 'sysPccfOwned = 0' not in function(system, 'void sysReset(')
nbd = function(read('src/app/lwnbdctl.cpp'), 'static int loadLwnbdSvr(')
assert nbd.index('ioHasPendingRequests()') < nbd.index('if (sysIsPccfOwned())') < nbd.index('*teardownStarted = 1;')
assert 'ioBlockOps(0);' in nbd[nbd.index('if (sysIsPccfOwned())'):nbd.index('*teardownStarted = 1;')]
network = function(read('src/sources/netsupport.cpp'), 'int NetSupport::loadModules(int protocol)')
refusal = network.index('if (!sysInitDev9()) {')
assert refusal < network.index('modulesLoaded_ = 1;') < network.index('netman_irx')
assert 'sysShutdownDev9();' in network[refusal:network.index('modulesLoaded_ = 1;')]
publication = function(read('src/sources/bdmdevices.cpp'), 'int bdmUpdateDeviceData(')
assert '!(pDeviceData->bdmDeviceType == BDM_TYPE_PCCF && gEnablePCCF)' in publication
assert '(gEnableUDPBD || gEnablePCCF)' in function(bdm, 'int bdmEffectiveStartMode(')
# A CF game launches natively with the game's own DEV9/ATAD faked, no VMC and no RA telemetry.
launch = read('src/sources/bdmlaunch.cpp')
assert 'FAKE_MODULE_FLAG_DEV9 | FAKE_MODULE_FLAG_ATAD;\n        sysLaunchLoaderElf(filename, "BDM_CF_MODE"' in launch
assert 'BDM_TYPE_PCCF)\n        return 0;' in function(launch, 'int NeutrinoLauncher::tryLaunch(')
assert '!discMode && !cfMode && size_mcemu_irx > 0' in system
# CF shares the no-telemetry gate with the network block modes (sysModeHasNoRATelemetry).
assert 'return !strcmp(mode_str, "BDM_CF_MODE") || sysModeIsNetBlock(mode_str);' in system
assert 'sysModeHasNoRATelemetry(mode_str) ? 0 : host::launch().raWorkBytes(' in system
assert '!sysModeHasNoRATelemetry(mode_str) && gRATelemetry' in system
probe = function(bdm, 'static void bdmLoadPccfModule(')
request = function(bdm, 'void bdmRequestPccfProbe(')
queue = function(bdm, 'static int bdmShouldQueueModuleLoad(')
harness = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "modules/pccf/pccf_mbr.h"
#define LOG(...) ((void)0)
#define O_RDONLY 0
#define DDIOC_OFF 1
static int sysDev9BusLock, locked, sysPccfOwned;
static unsigned char dev9Initialized, dev9Loaded, dev9InitCount;
static int ps2dev9_irx, size_ps2dev9_irx, loadCalls, loadFailure, poweredOff;
static void WaitSema(int) {assert(!locked);locked=1;}
static void SignalSema(int) {assert(locked);locked=0;}
static int sysLoadModuleBuffer(void *,int,int,char *) {assert(locked);loadCalls++;return loadFailure ? -2 : 0;}
static int fileXioDevctl(const char *,int,void *,int,void *,int) {assert(locked);poweredOff++;return 0;}
@OWNERSHIP@
static int gEnablePCCF = 1, pccfModLoaded, pccf_bd_irx, size_pccf_bd_irx, probeLoads, probeFailure = 1, errors;
static unsigned int pccfProbeGeneration = 1, pccfAttemptGeneration, BdmGeneration;
static int requestDuringProbe;
void bdmRequestPccfProbe(void);
#define _STR_PCCF_UNAVAILABLE 1
static int bdmLoadOptionalModule(const char *,void *,int) {
    probeLoads++;
    if (requestDuringProbe) {requestDuringProbe=0;bdmRequestPccfProbe();}
    return probeFailure ? -1 : 0;
}
struct App {void setError(int,int) {errors++;}};
namespace host {static App app() {return App();}}
static void bdmForceDeviceRefresh(void) {BdmGeneration++;}
@REQUEST@
static const char *rom;
static int psx;
static int open(const char *path,int) {return !strcmp(path,"rom0:ROMVER") ? 1 : psx ? 2 : -1;}
static int read(int,void *p,int n) {int len=(int)strlen(rom);if(len>n)len=n;memcpy(p,rom,len);return len;}
static void close(int) {}
@CANDIDATE@
@PROBE@
static int gEnableUSB, iUSBModLoaded, gEnableILK, iLinkModLoaded, gEnableMX4SIO, mx4sioModLoaded;
static int gEnableBdmHDD, hddModLoaded, gEnableUDPBD, udpbdNicClaimed;
static int hostBoot;
static int oplBootDirIsHostFs(void) { return hostBoot; }
static int ethGetModulesLoaded(void) {return 0;}
static int udpfsGetModulesLoaded(void) {return 0;}
@QUEUE@
#define USE_PCCF
#define DPRINTF(...) ((void)0)
static int bdm_io_sema, stops;
struct block_device {int (*stop)(struct block_device *);};
static struct block_device *g_bd;
static int stop_cf(struct block_device *) {assert(locked);stops++;return 0;}
@SHUTDOWN@
int main(int argc,char **argv) {
    assert(argc==4);rom=argv[1];psx=atoi(argv[2]);
    assert(bdmPccfCandidate()==atoi(argv[3]));
    assert(sysTryClaimPccf() && !locked && sysIsPccfOwned());
    assert(!sysTryClaimPccf() && !sysInitDev9() && !loadCalls && dev9InitCount==1 && !locked);
    sysShutdownDev9(); /* paired failed-init shutdown never touches the CF-owned slot */
    assert(dev9InitCount==0 && !poweredOff && !locked);
    sysReleasePccf();assert(sysInitDev9() && loadCalls==1 && dev9InitCount==1 && !locked);
    assert(!sysTryClaimPccf());
    sysShutdownDev9();assert(dev9InitCount==0 && poweredOff==1);
    assert(!sysTryClaimPccf()); /* resident DEV9 still owns the bus */
    dev9Loaded=dev9Initialized=0;loadFailure=1;
    assert(!sysInitDev9() && dev9InitCount==1 && !sysTryClaimPccf());
    sysShutdownDev9();assert(sysTryClaimPccf());sysReleasePccf();
    assert(!sysIsPccfOwned());
    assert(bdmShouldQueueModuleLoad());
    bdmLoadPccfModule();
    assert(!pccfModLoaded && errors==1 && !sysIsPccfOwned());
    int firstLoads=probeLoads;
    for (int i=0;i<30;i++) {BdmGeneration++;assert(!bdmShouldQueueModuleLoad());bdmLoadPccfModule();}
    assert(probeLoads==firstLoads && errors==1);
    bdmRequestPccfProbe();assert(bdmShouldQueueModuleLoad());bdmLoadPccfModule();
    assert(errors==2 && probeLoads==firstLoads*2);
    if (atoi(argv[3])) {
        requestDuringProbe=1;bdmRequestPccfProbe();bdmLoadPccfModule();
        assert(bdmShouldQueueModuleLoad()); /* a request during a failed attempt is not lost */
        bdmLoadPccfModule();assert(!bdmShouldQueueModuleLoad());
    }
    unsigned int beforeProbe=pccfProbeGeneration, beforeRefresh=BdmGeneration;
    gEnablePCCF=0;bdmRequestPccfProbe();
    assert(pccfProbeGeneration==beforeProbe && BdmGeneration==beforeRefresh);
    gEnablePCCF=1;
    int beforeSuccess=probeLoads;
    probeFailure=0;bdmRequestPccfProbe();bdmLoadPccfModule();
    if (atoi(argv[3])) {
        assert(pccfModLoaded && sysIsPccfOwned());
        beforeProbe=pccfProbeGeneration;beforeRefresh=BdmGeneration;
        bdmRequestPccfProbe();bdmLoadPccfModule();assert(probeLoads==beforeSuccess+1);
        assert(pccfProbeGeneration==beforeProbe && BdmGeneration==beforeRefresh);
    } else {assert(!pccfModLoaded && !probeLoads && errors==3);}
    struct block_device cf = {stop_cf};
    DeviceDeinit();assert(!stops && !locked);
    g_bd=&cf;DeviceDeinit();assert(stops==1 && g_bd==NULL && !locked);
    DeviceDeinit();assert(stops==1 && !locked);
    unsigned char mbr[512]={0};assert(!pccf_mbr_valid(mbr,10000));
    mbr[510]=0x55;mbr[511]=0xaa;mbr[450]=0x0c;mbr[454]=1;mbr[458]=100;
    assert(pccf_mbr_valid(mbr,101));assert(!pccf_mbr_valid(mbr,100));
    mbr[450]=0x07;assert(pccf_mbr_valid(mbr,10000));
    mbr[466]=0xee;assert(!pccf_mbr_valid(mbr,10000));mbr[466]=0;
    mbr[454]=0;assert(!pccf_mbr_valid(mbr,10000));
    mbr[454]=1;mbr[461]=0xff;assert(!pccf_mbr_valid(mbr,10000));
    puts("PASS: ROM eligibility, exclusive CF/DEV9 ownership and MBR bounds");
}
'''
for key, text in (('OWNERSHIP', ownership), ('CANDIDATE', candidate), ('SHUTDOWN', shutdown),
                  ('PROBE', probe), ('REQUEST', request), ('QUEUE', queue)):
    harness = harness.replace('@' + key + '@', text)
sanitize = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.name != 'nt' else []
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    for name in ('bd', 'profile', 'ata_rw', 'ownership'):
        source = root / 'tests/pccf' / ('test_' + name + '.c')
        compiler, std = os.environ.get('CC', 'gcc'), '-std=gnu99'
        if name == 'ownership':
            source = temp / 'ownership.cpp'
            source.write_text(harness, encoding='utf-8')
            compiler, std = os.environ.get('CXX', 'g++'), '-std=gnu++17'
        binary = temp / (name + '.exe')
        flags = [std, '-Wall', '-Wextra', '-Werror', '-I' + str(root), '-I' + str(root / 'tests/pccf/stubs'), *sanitize]
        subprocess.run([compiler, *flags, str(source), '-o', str(binary)], check=True)
        if name == 'ownership':
            for rom, psx, eligible in (('0100JC20000117', 0, 1), ('0101JC20000217', 0, 1),
                                       ('0120JC20001027', 0, 1), ('0160JC20011004', 0, 0),
                                       ('0100EC20000117', 0, 0), ('', 0, 0), ('0100JC20000117', 1, 0)):
                subprocess.run([str(binary), rom, str(psx), str(eligible)], check=True, timeout=10)
        else:
            subprocess.run([str(binary)], check=True, timeout=20)
print('PASS: CF low-level and integration host regressions; hardware unverified')

# Compile the production stubs and exercise their import ordinals as MIPS callers
# would: surplus arguments must remain ignored and no output/callback is touched.
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    source = temp / 'imports.c'
    source.write_text(r"""
#include <assert.h>
#include "modules/iopcore/cdvdman/dev9-pccf.c"
static int called;
static void callback(void) { called++; }
int main(void) {
    int (*dev9)() = pccfDev9Unavailable;
    int (*ata)() = pccfAtaUnavailable;
    unsigned short ethernet[3] = {1,2,3};
    assert(dev9(ethernet)==-1 && ethernet[0]==1 && ethernet[2]==3);
    assert(dev9(0,callback)==-1 && !called);
    assert(dev9(0,ethernet,4,0)==-1);
    assert(pccfAtaInit(0)==0);
    assert(ata(0,ethernet,0,1,0)==-505);
    return 0;
}
""", encoding='utf-8')
    binary = temp / 'imports.exe'
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=gnu99', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root), '-I' + str(root / 'tests/pccf/stubs'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
    preprocessed = subprocess.check_output([os.environ.get('CC', 'gcc'), '-E', '-P', '-x', 'c', '-DUSE_PCCF',
                                            str(root / 'modules/iopcore/cdvdman/exports.tab')], text=True)
    tables = dict(re.findall(r'DECLARE_EXPORT_TABLE\((\w+),[^)]*\)(.*?)END_EXPORT_TABLE', preprocessed, re.S))
    dev9 = re.findall(r'DECLARE_EXPORT\((\w+)\)', tables['dev9'])
    ata = re.findall(r'DECLARE_EXPORT\((\w+)\)', tables['atad'])
    assert len(dev9) == 14 and all(dev9[i] == 'pccfDev9Unavailable' for i in (5, 9, 11))
    assert len(ata) == 19 and ata[4] == 'pccfAtaInit' and set(ata[5:]) == {'pccfAtaUnavailable'}
    assert 'dev9d_init' not in preprocessed and 'sceAtaDmaTransfer' not in preprocessed
print('PASS: CF hardware-inert DEV9/ATAD exports and ordinal ABI')
