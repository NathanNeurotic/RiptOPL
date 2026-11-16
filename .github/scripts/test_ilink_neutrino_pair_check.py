"""An i.LINK Neutrino launch checks the install's own i.LINK pair against the fixed pair this ELF embeds.

Neutrino's in-game environment loads <install>/modules/iLinkman.irx and IEEE1394_bd_mini.irx after
the game's IOP reset; the stock pair gives up its LOGIN inside the drive's hold. The real helper is
compiled on the host and run against matching, short, long, altered and missing files.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
src = (ROOT / 'src/sources/bdmlaunch.cpp').read_text(encoding='utf-8')
yaml = (ROOT / 'ps2.yaml').read_text(encoding='utf-8')

# The embedded symbols the check compares with are the fork builds, the same ones the kit ships.
assert 'iLinkman_fork:iLinkman' in yaml and 'IEEE1394_bd_mini_fork:IEEE1394_bd' in yaml

helper = re.search(r'static int neutrinoIlinkModuleIsOurs\(.*?\n}\n', src, re.S).group(0)
launch = src[src.index('int NeutrinoLauncher::tryLaunch('):]
assert launch.index('bdmDriverIsIlink(pDeviceData->bdmDriver)') < launch.index('sysLaunchNeutrino(')
assert '"iLinkman.irx", (const unsigned char *)iLinkman_irx, size_iLinkman_irx' in launch
assert '"IEEE1394_bd_mini.irx", (const unsigned char *)IEEE1394_bd_irx, size_IEEE1394_bd_irx' in launch

program = r'''
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
''' + helper + r'''
static void put(const char *p, const unsigned char *d, int n, int extra)
{
    FILE *f = fopen(p, "wb");
    fwrite(d, 1, n, f);
    if (extra) fputc(0, f);
    fclose(f);
}
int main(void)
{
    static unsigned char ours[1500];
    for (int i = 0; i < 1500; i++) ours[i] = (unsigned char)(i * 7);
    put("n/modules/same.irx", ours, 1500, 0);
    put("n/modules/short.irx", ours, 1499, 0);
    put("n/modules/long.irx", ours, 1500, 1);
    int ok = neutrinoIlinkModuleIsOurs("n/neutrino.elf", "same.irx", ours, 1500) == 1
          && neutrinoIlinkModuleIsOurs("n/neutrino.elf", "short.irx", ours, 1500) == 0
          && neutrinoIlinkModuleIsOurs("n/neutrino.elf", "long.irx", ours, 1500) == 0
          && neutrinoIlinkModuleIsOurs("n/neutrino.elf", "missing.irx", ours, 1500) == 0;
    ours[700] ^= 1;
    ok = ok && neutrinoIlinkModuleIsOurs("n/neutrino.elf", "same.irx", ours, 1500) == 0;
    return ok ? 0 : 1;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    t = Path(tmp)
    (t / 'n/modules').mkdir(parents=True)
    (t / 'h.cpp').write_text(program, encoding='utf-8')
    subprocess.run(['g++', '-Wall', '-Werror', '-o', str(t / 'h'), str(t / 'h.cpp')], check=True)
    subprocess.run([str(t / 'h')], cwd=tmp, check=True)

print('PASS: i.LINK Neutrino launches compare the install\'s i.LINK pair with the embedded fixed pair')
