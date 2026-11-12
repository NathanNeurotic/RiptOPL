#!/usr/bin/env python3
"""Exercise the production probe with early, delayed, and absent iLink roots."""
from pathlib import Path
import subprocess
import tempfile


root = Path(__file__).resolve().parents[2]
source = (root / 'src/sources/bdmsupport.cpp').read_text(encoding='utf-8')
start = source.index('    static int ilinkProbeDone = 0;')
probe = source[start:source.index('\n}', start)]
program = r'''
#include <assert.h>
#include <stdio.h>
static int gEnableILK, iLinkModLoaded, delays, refreshes, ready_after, bdmAutoLaunch;
#define BDM_TYPE_ILINK 1
#define LOG(...) ((void)0)
static int bdmGetDeviceRootByType(int type, char *root, unsigned int size) {
    assert(type == BDM_TYPE_ILINK && size >= 8);
    if (ready_after < 0 || delays < ready_after) return 0;
    snprintf(root, size, "mass0:/");
    return 1;
}
static void DelayThread(int usec) { assert(usec == 250000); delays++; }
static void bdmForceDeviceRefresh(void) { refreshes++; }
static void run_probe(void) {
''' + probe + r'''
}
int main(void) {
    gEnableILK = 0; run_probe();
    gEnableILK = iLinkModLoaded = bdmAutoLaunch = 1;
    ready_after = -1; run_probe();
    assert(delays == 0 && refreshes == 0); // an absent optional iLink cannot delay Auto Loading
    bdmAutoLaunch = 0; ready_after = 2; run_probe();
    assert(delays == 2 && refreshes == 1); // a menu fallback still gets its late-drive probe
    int readiness[] = {0, 2, 11, 12, 13, -1};
    for (unsigned int i = 0; i < sizeof(readiness)/sizeof(readiness[0]); i++) {
        gEnableILK = 0; run_probe();
        delays = refreshes = 0; ready_after = readiness[i];
        gEnableILK = 1; iLinkModLoaded = 1; run_probe();
        assert(refreshes == 1);
        assert(delays == (ready_after < 0 || ready_after > 12 ? 12 : ready_after));
        run_probe(); assert(refreshes == 1); // one probe per enabled session
    }
    gEnableILK = 0; run_probe();
    refreshes = delays = 0; iLinkModLoaded = 0; gEnableILK = 1;
    run_probe(); assert(refreshes == 0 && delays == 0);
    iLinkModLoaded = 1; ready_after = 0;
    run_probe(); assert(refreshes == 1 && delays == 0);
    puts("iLink readiness: autoload skip, menu fallback, ready, delayed, absent, one-shot and re-enable passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    cfile = Path(temp) / 'probe.c'
    exe = Path(temp) / 'probe.exe'
    cfile.write_text(program, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(cfile), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# Auto Loading sets the policy after the IO worker exists and before BDM starts; the menu clears it.
opl = (root / 'src/app/opl.cpp').read_text(encoding='utf-8')
for signature, enabled in [('\nstatic void init(void)', 0), ('\nvoid miniInit(int mode, int ataOnly)', 1)]:
    begin = opl.index(signature)
    body = opl[begin:opl.index('\n}', begin)]
    assert body.index('ioInit();') < body.index(f'bdmSetAutoLaunch({enabled});') < body.index('bdmInitSemaphore();'), signature
