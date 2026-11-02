"""Validate real shipped BDMA pair sizes through actual environment/manual checks."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
source=(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'src/sources/vcdsupport.cpp').read_text(encoding='utf-8')
def function(signature):
    start=source.index(signature)
    return source[start:source.index('\n}',start)+2]
table=re.search(r'static const bdma_pair_sig_t vcdBdmaPairSig\[[^;]+;',source,re.S).group()
suffixes=['usbexfat','mx4sio','mmce','ata','ilink','udpfs']
sizes=[[ (ROOT/'ship/POPS'/f'{name}.{suffix}').stat().st_size for name in ['usbd.irx','usbhdfsd.irx']] for suffix in suffixes]
program=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
constexpr int VCD_BDMA_MODE_COUNT=7, VCD_BDMA_FAT32=0, VCD_BDMA_ILINK=5, O_RDONLY=0;
#define VCD_BDMA_MARKER "bdma.mode"
struct bdma_pair_sig_t {int usbdSize,hdfsdSize;};
static const char *vcdBdmaModule[]={"usbd.irx","usbhdfsd.irx"};
static int pairSize[2], markerMode, opens, closes;
static bool markerExists, cardExists=true;
static int vcdFindPopstarterMc(char *out,int n) {snprintf(out,n,"mc0:/POPSTARTER");return cardExists;}
static int vcdReadBdmaMode() {return markerMode;}
static int vcdGetFileSize(const char *p) {return pairSize[strstr(p,"usbhdfsd")?1:0];}
static int open(const char *,int flags) {assert(flags==O_RDONLY);if(markerExists){++opens;return 7;}return -1;}
static int close(int fd) {assert(fd==7);++closes;return 0;}
@TABLE@
@FUNCTIONS@
static const int shipped[][2]=@SIZES@;
int main() {
    for(int mode=1;mode<VCD_BDMA_MODE_COUNT;++mode) {
        pairSize[0]=shipped[mode-1][0];pairSize[1]=shipped[mode-1][1];
        markerExists=true;markerMode=mode;
        assert(vcdBdmaEnvironmentValid(mode));
        assert(!vcdBdmaManualPairMatches(mode));assert(opens==closes);
        markerExists=false;assert(vcdBdmaManualPairMatches(mode));
        for(int other=1;other<VCD_BDMA_MODE_COUNT;++other)
            if(other!=mode)assert(!vcdBdmaManualPairMatches(other));
        markerMode=0;assert(!vcdBdmaEnvironmentValid(mode));
        ++pairSize[1];assert(!vcdBdmaManualPairMatches(mode));
        pairSize[1]-=2;assert(!vcdBdmaManualPairMatches(mode));
        pairSize[1]=-1;assert(!vcdBdmaManualPairMatches(mode));
        markerMode=mode;assert(!vcdBdmaEnvironmentValid(mode));
    }
    /* Every older iLink driver loses its login after the IOP reset: a card holding one re-equips. */
    const int staleIlink[]={21609,23452,21977,22025,22185,27081,27145,27129,27641,27705};
    for(int stale:staleIlink) {
        pairSize[0]=48500;pairSize[1]=stale;markerMode=VCD_BDMA_ILINK;markerExists=true;
        assert(!vcdBdmaEnvironmentValid(VCD_BDMA_ILINK));
        markerExists=false;assert(!vcdBdmaManualPairMatches(VCD_BDMA_ILINK));
    }
    assert(!vcdBdmaPairMatches(-1,48500,27769)&&!vcdBdmaPairMatches(7,48500,27769));
    pairSize[0]=pairSize[1]=-1;assert(vcdBdmaEnvironmentValid(0));
    pairSize[0]=48500;assert(!vcdBdmaEnvironmentValid(0));
    /* No POPSTARTER folder: no external pair, which is exactly the FAT32 environment (and reading never makes one). */
    cardExists=false;assert(!vcdBdmaEnvironmentValid(1)&&!vcdBdmaManualPairMatches(1)&&vcdBdmaEnvironmentValid(0));
    assert(!vcdBdmaEnvironmentValid(-1)&&!vcdBdmaEnvironmentValid(7));
    assert(!vcdBdmaManualPairMatches(0)&&!vcdBdmaManualPairMatches(7));
    puts("PASS: all six shipped BDMA pairs accepted by actual validators; stale iLink, family/marker/missing/truncated pairs refused");
}
'''.replace('@TABLE@',table).replace('@FUNCTIONS@','\n'.join(function(s) for s in ['static int vcdBdmaPairMatches(', 'static int vcdBdmaEnvironmentValid(', 'static int vcdBdmaManualPairMatches('])).replace('@SIZES@','{'+','.join('{'+','.join(map(str,p))+'}' for p in sizes)+'}')
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'pairs.cpp';exe=Path(tmp)/('pairs.exe' if os.name=='nt' else 'pairs')
    cpp.write_text(program,encoding='utf-8')
    flags=['-fsanitize=address,undefined','-fno-pie','-no-pie'] if os.name!='nt' else []
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*flags,str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
