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
# Where Neutrino loads its modules from: the real argument reader and folder resolver, compiled below.
neutrino_src = (ROOT / 'src/platform/sysneutrino.cpp').read_text(encoding='utf-8')
resolver = ''.join(re.search(pattern, neutrino_src, re.S).group(0) for pattern in (
    r'static const char \*sysLastPathSeparator\(.*?\n}\n',
    r'static int neutrinoArgActiveValue\(.*?\n}\n',
    r'int sysNeutrinoWorkDir\(.*?\n}\n'))
launch = src[src.index('int NeutrinoLauncher::tryLaunch('):]
assert launch.index('bdmDriverIsIlink(pDeviceData->bdmDriver)') < launch.index('sysLaunchNeutrino(')
# The per-game arguments are read before the check, so a per-game -cwd= is seen by it.
assert launch.index('CONFIG_ITEM_NEUTRINO_ARGS') < launch.index('sysNeutrinoWorkDir(')
assert 'neutrinoIlinkModuleIsOurs(workDir, "iLinkman.irx"' in launch
assert '"iLinkman.irx", (const unsigned char *)iLinkman_irx, size_iLinkman_irx' in launch
assert '"IEEE1394_bd_mini.irx", (const unsigned char *)IEEE1394_bd_irx, size_IEEE1394_bd_irx' in launch

program = r'''
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
static char gNeutrinoArgs[128] = "";
''' + resolver + helper + r'''
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
    int ok = neutrinoIlinkModuleIsOurs("n/", "same.irx", ours, 1500) == 1
          && neutrinoIlinkModuleIsOurs("n/", "short.irx", ours, 1500) == 0
          && neutrinoIlinkModuleIsOurs("n/", "long.irx", ours, 1500) == 0
          && neutrinoIlinkModuleIsOurs("n/", "missing.irx", ours, 1500) == 0;
    ours[700] ^= 1;
    ok = ok && neutrinoIlinkModuleIsOurs("n/", "same.irx", ours, 1500) == 0;
    ours[700] ^= 1;

    // Folder A holds the matching file, folder B an altered one: the answer follows the -cwd Neutrino gets.
    put("A/modules/pair.irx", ours, 1500, 0);
    static unsigned char other[1500];
    memcpy(other, ours, 1500);
    other[3] ^= 1;
    put("B/modules/pair.irx", other, 1500, 0);
    char dir[64];
    ok = ok && sysNeutrinoWorkDir("A/neutrino.elf", "", dir, sizeof(dir)) == 0
            && neutrinoIlinkModuleIsOurs(dir, "pair.irx", ours, 1500) == 1;
    ok = ok && sysNeutrinoWorkDir("A/neutrino.elf", "-cwd=B/", dir, sizeof(dir)) == 0
            && neutrinoIlinkModuleIsOurs(dir, "pair.irx", ours, 1500) == 0;
    strcpy(gNeutrinoArgs, "-cwd=B");        // the global override applies, with no trailing separator
    ok = ok && sysNeutrinoWorkDir("A/neutrino.elf", "", dir, sizeof(dir)) == 0
            && neutrinoIlinkModuleIsOurs(dir, "pair.irx", ours, 1500) == 0;
    ok = ok && sysNeutrinoWorkDir("A/neutrino.elf", "-cwd=A/", dir, sizeof(dir)) == 0   // per-game is read last
            && neutrinoIlinkModuleIsOurs(dir, "pair.irx", ours, 1500) == 1;
    ok = ok && sysNeutrinoWorkDir("A/neutrino.elf", "$-cwd=B/ --b -cwd=B/", dir, sizeof(dir)) == 0
            && neutrinoIlinkModuleIsOurs(dir, "pair.irx", ours, 1500) == 0;      // the global B still holds
    strcpy(gNeutrinoArgs, "");
    ok = ok && sysNeutrinoWorkDir("A/neutrino.elf", "$-cwd=B/", dir, sizeof(dir)) == 0
            && neutrinoIlinkModuleIsOurs(dir, "pair.irx", ours, 1500) == 1;      // a $-prefixed flag is switched off
    return ok ? 0 : 1;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    t = Path(tmp)
    (t / 'n/modules').mkdir(parents=True)
    (t / 'A/modules').mkdir(parents=True)
    (t / 'B/modules').mkdir(parents=True)
    (t / 'h.cpp').write_text(program, encoding='utf-8')
    subprocess.run(['g++', '-Wall', '-Werror', '-o', str(t / 'h'), str(t / 'h.cpp')], check=True)
    subprocess.run([str(t / 'h')], cwd=tmp, check=True)

print('PASS: i.LINK Neutrino launches compare the install\'s i.LINK pair with the embedded fixed pair')
