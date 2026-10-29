"""Exercise production UDPFS Assault TEST DAT population/editor code with host IO and MC faults.

Ported from the original (#1057). The launch order itself is driven in test_udpfs_library_launch.py.
"""
from pathlib import Path
import json
import hashlib
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/sources/vcdsupport.cpp').read_text(encoding='utf-8')
header = (ROOT / 'include/vcdsupport.h').read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]

typedef = re.search(r'typedef struct\s*\{[^}]*\}\s*vcd_pops_udpfs_t;', header).group(0)
pieces = '\n'.join(function(s) for s in (
    'static int vcdReadNetFile(', 'static int vcdQuadOk(',
    'static unsigned int vcdUdpfsAddress(', 'static int vcdUdpfsHost(',
    'int vcdPopsUdpfsNetValid(', 'static const char *vcdUdpfsConfigLine(',
    'static int vcdUdpfsParseQuad(', 'static int vcdPopsUdpfsConfigPath(',
    'static int vcdWritePopsUdpfsFile(', 'int vcdInstallPopsUdpfsNet(',
    'int vcdReadPopsUdpfsNet(', 'int vcdWritePopsUdpfsNet(',
))
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#define MAX_BDM_DEVICES 10
#define BDM_TYPE_USB 7
#define LOG(...) ((void)0)
static int writes, failWrite, failRestore, noSpace, corruptWrite;
static const char *deny;
static const char *mapped(const char *p) {
 static char b[8][512];static unsigned next;
 const char *prefix=NULL;
 if(!strncmp(p,"mc0:/",5))prefix="MC0/";
 if(!strncmp(p,"mc1:/",5))prefix="MC1/";
 if(!strncmp(p,"udpfs:/",7))prefix="GAME/";
 if(!prefix)return p;
 char *out=b[(next++)%8];snprintf(out,512,"%s%s",prefix,p+(!strncmp(p,"udpfs:",6)?7:5));return out;
}
static int hostOpen(const char *p,int f) {
 if(deny && !strcmp(p,deny)){errno=EACCES;return -1;}
 return open(mapped(p),f|O_BINARY,0644);
}
static int hostMkdir(const char *p,int mode) {
#ifdef _WIN32
 (void)mode; return mkdir(mapped(p));
#else
 return mkdir(mapped(p),mode);
#endif
}
static int hostUnlink(const char *p) {return unlink(mapped(p));}
static DIR *hostOpendir(const char *p) {return opendir(mapped(p));}
static int vcdFindPopstarterMc(char *out,int n) {
 for(int i=0;i<2;i++){snprintf(out,n,"mc%d:/POPSTARTER",i);DIR*d=hostOpendir(out);if(d){closedir(d);return 1;}}
 return 0;
}
static int vcdSafeWriteFile(const char *p,const void *buf,int len) {
 if(noSpace)return -2;
 ++writes;
 FILE*f=fopen(mapped(p),"wb");if(!f)return -3;
 if(writes==failWrite || writes==failRestore){fputs("partial",f);fclose(f);hostUnlink(p);return -3;}
 int ok=fwrite(buf,1,len,f)==(size_t)len;
 if(corruptWrite){fputc('!',f);corruptWrite=0;}
 if(fclose(f))ok=0;
 return ok?0:-3;
}
#define open hostOpen
#define mkdir hostMkdir
#define unlink hostUnlink
#define opendir hostOpendir
@TYPE@
@CODE@
static int checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void put(const char *p,const char *text){FILE*f=fopen(mapped(p),"wb");if(!f)abort();fputs(text,f);fclose(f);}
static int equals(const char *p,const char *text){char b[512]={0};FILE*f=fopen(mapped(p),"rb");if(!f)return 0;fread(b,1,sizeof(b)-1,f);fclose(f);return !strcmp(b,text);}
static int exists(const char *p){struct stat s;return stat(mapped(p),&s)==0;}
int main(void) {
 vcd_pops_udpfs_t c,changed;
 const char *ip="192.168.1.20 255.255.255.0 0.0.0.0\n",*srv="192.168.1.21\n";
 const char *ip0="mc0:/SYS-CONF/IPCONFIG.DAT",*srv0="mc0:/SYS-CONF/UDPFSCONFIG.DAT";
 const char *ip1="mc1:/SYS-CONF/IPCONFIG.DAT",*srv1="mc1:/SYS-CONF/UDPFSCONFIG.DAT";
 mkdir("MC0",0777);mkdir("MC1",0777);mkdir("GAME",0777);mkdir("GAME/POPS",0777);
 CHECK(vcdReadPopsUdpfsNet(&c)==0 && !c.ipExists && !c.serverExists && !c.invalid);
 CHECK(c.ps2Ip[3]==10 && c.server[0]==0 && writes==0);
 CHECK(!exists("MC0/SYS-CONF"));
 CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")==0 && writes==0);
 CHECK(!exists("MC0/SYS-CONF"));
 put("udpfs:/POPS/IPCONFIG.DAT",ip);put("udpfs:/POPS/UDPFSCONFIG.DAT",srv);
 CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")==0 && writes==2);
 CHECK(equals(ip0,ip)&&equals(srv0,srv));
 CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")==0 && writes==2);
 put("udpfs:/POPS/IPCONFIG.DAT","different game config");
 CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")==0 && equals(ip0,ip));
 unlink(srv0);CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")==0 && writes==3);
 CHECK(equals(ip0,ip)&&equals(srv0,srv));
 // A file on MC1 is already on MC: do not create a shadow on MC0.
 mkdir("MC1/SYS-CONF",0777);unlink(srv0);put(srv1,srv);
 CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")==0 && writes==3 && !exists(srv0));
 CHECK(vcdReadPopsUdpfsNet(&c)==0 && !c.invalid && c.ps2Ip[3]==20 && c.server[3]==21);
 CHECK(strstr(c.home,"mc1:")!=NULL);
 changed=c;changed.server[3]=22;
 CHECK(vcdWritePopsUdpfsNet(&changed)==0 && equals(srv1,"192.168.1.22\n") && !exists(srv0));
 // Editor updates the effective MC0 file when both cards hold one.
 put(srv0,srv);put(srv1,"192.168.1.99\n");
 CHECK(vcdReadPopsUdpfsNet(&c)==0 && c.server[3]==21);
 changed=c;changed.server[3]=22;
 CHECK(vcdWritePopsUdpfsNet(&changed)==0 && equals(srv0,"192.168.1.22\n") && equals(srv1,"192.168.1.99\n"));
 for(int failure=1;failure<=4;failure++) {
  put(ip0,ip);put(srv0,srv);writes=0;failWrite=failure;
  CHECK(vcdWritePopsUdpfsNet(&changed)!=0);failWrite=0;
  CHECK(equals(ip0,ip)&&equals(srv0,srv));
  CHECK(!exists("mc0:/SYS-CONF/IPCONFIG.DAT.bak")&&!exists("mc0:/SYS-CONF/UDPFSCONFIG.DAT.bak"));
 }
 // A failed rollback retains the original backup and blocks another save.
 put(ip0,ip);put(srv0,srv);writes=0;failWrite=4;failRestore=5;
 CHECK(vcdWritePopsUdpfsNet(&changed)!=0);failWrite=failRestore=0;
 CHECK(equals("mc0:/SYS-CONF/IPCONFIG.DAT.bak",ip));
 CHECK(vcdWritePopsUdpfsNet(&changed)!=0);
 unlink("mc0:/SYS-CONF/IPCONFIG.DAT.bak");put(ip0,ip);
 noSpace=1;CHECK(vcdWritePopsUdpfsNet(&changed)!=0);noSpace=0;
 CHECK(equals(ip0,ip)&&equals(srv0,srv));
 corruptWrite=1;CHECK(vcdWritePopsUdpfsNet(&changed)!=0);
 CHECK(equals(ip0,ip)&&equals(srv0,srv));
 // Population failures leave no partial destination, and retry succeeds.
 unlink(srv0);unlink(srv1);writes=0;failWrite=1;
 CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")!=0 && !exists(srv0));failWrite=0;
 CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")==0 && equals(srv0,srv));
 deny=ip0;CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")!=0);deny=NULL;
 put(ip0,"bad IP");CHECK(vcdReadPopsUdpfsNet(&c)==0 && c.invalid);
 put(ip0,"\xef\xbb\xbf#comment\r\n192.168.1.20 255.255.255.0\r\n");
 CHECK(vcdReadPopsUdpfsNet(&c)==0&&!c.invalid&&c.gateway[0]==0);
 changed=c;changed.server[2]=2;CHECK(!vcdPopsUdpfsNetValid(&changed));
 changed=c;changed.ps2Ip[0]=127;CHECK(!vcdPopsUdpfsNetValid(&changed));
 changed=c;memset(changed.server,0,sizeof(changed.server));
 CHECK(vcdWritePopsUdpfsNet(&changed)==0 && equals(srv0,""));
 CHECK(vcdReadPopsUdpfsNet(&c)==0&&!c.invalid&&c.server[0]==0);
 // Existing MC1 installation chooses MC1 for missing DATs.
 unlink(ip0);unlink(srv0);mkdir("MC1/POPSTARTER",0777);put("udpfs:/POPS/IPCONFIG.DAT",ip);
 CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")==0 && equals(ip1,ip)&&equals(srv1,srv));
 CHECK(!exists(ip0)&&!exists(srv0));
 unlink(srv1);char large[122];memset(large,'x',121);large[121]=0;put("udpfs:/POPS/UDPFSCONFIG.DAT",large);
 CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")!=0 && !exists(srv1));
    // An interrupted settings save must be recovered before automatic population.
    put("udpfs:/POPS/UDPFSCONFIG.DAT",srv);
    put("mc1:/SYS-CONF/UDPFSCONFIG.DAT.bak","original server config");
    CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")!=0 && !exists(srv1));
    CHECK(equals("mc1:/SYS-CONF/UDPFSCONFIG.DAT.bak","original server config"));
    unlink("mc1:/SYS-CONF/UDPFSCONFIG.DAT.bak");
    noSpace=1;CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")==-2 && !exists(srv1));noSpace=0;
    corruptWrite=1;CHECK(vcdInstallPopsUdpfsNet("udpfs:/POPS/")!=0 && !exists(srv1));
 printf("POPS UDPFS: %d population, precedence, config and rollback checks passed\n",checks);return 0;
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temp:
    path = Path(temp)
    (path / 'test.cpp').write_text(HARNESS.replace('@TYPE@', typedef).replace('@CODE@', pieces), encoding='utf-8')
    exe = path / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(path/'test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=path, check=True)

udpfs = (ROOT/'src/sources/udpfssupport.cpp').read_text(encoding='utf-8')
assert 'ps1FillGameList(udpfsPrefix, &gUdpfsSupport.ps1Games_)' in udpfs
assert 'GameSource::CAP_LAUNCH_VCD' in udpfs
start = udpfs.index('void UdpfsSource::launchVcd(')
launch = udpfs[start:udpfs.index('\n}', start) + 2]
assert launch.index('vcdInstallPopsUdpfsNet(') < launch.index('vcdEquipBdmaFromPops(') < launch.index('sysLaunchPopstarter(')
assert 'vcdInstallPopstarterMc(udpfsPrefix)' in launch and 'vcdConfirmCleanLaunch(udpfsPrefix)' in launch
assert 'VCD_BDMA_UDPFS' in launch and 'VCD_PREFIX_MASS' in launch
assert 'vcdEnsureBdmaForLaunch(' not in launch and 'usbRoot' not in launch
assert 'host::ui().msgBox(_l(_STR_POPS_UDPFS_DRIVER_NOT_READY)' in launch
for gone in ('vcdPreparePopsUdpfsLaunch', 'vcdPopsUdpfsUsbHomes', 'vcdPopsUdpfsCleanModules'):
    assert gone not in source and gone not in header, gone

# An explicit POPS source is authoritative, and a UDPFS equip populates the DAT files after the marker.
equip = function('int VcdSupport::equipBdma(int source, int mode, char *diag, int diagSize, const char *popsDir, int shippedOnly)')
assert 'An explicit game-device POPS source is authoritative.' in equip
assert 'if (found || popsDir != NULL) {' in equip
assert equip.index('vcdWriteBdmaMarker(mcDir, mode);') < equip.index('return vcdInstallPopsUdpfsNet(sourcePops);')
assert '{51029, 34508}  // UDPFS Assault TEST 2026-10-28' in source
assert 'if (strcmp(buf, "udpfsnet") == 0)' in function('int vcdReadBdmaMode(')

provenance = json.loads((ROOT/'ship/POPS/UDPFS-NOTICES/PROVENANCE.json').read_text(encoding='utf-8'))
assert provenance['release'].endswith('/tag/UDPFS_TEST') and provenance['prerelease'] is True
assert provenance['archive_sha256'] == '225ea524f1eed59bffcc193e2fdf92868e6a2061a526a8c1d21675ac71c24d2f'
for name, expected in provenance['files'].items():
    assert hashlib.sha256((ROOT/'ship/POPS'/name).read_bytes()).hexdigest() == expected['sha256'], name
assert (ROOT/'ship/POPS/usbd.irx.udpfs').stat().st_size == 51029

# The TEST driver presents POPS/<game>.cue (or .CUE) as a VCD: the UDPFS scan lists it once and the
# launch accepts it when no .VCD exists. Other devices still list only .VCD.
scan = function('static int vcdScanOpenDir(')
assert 'const int udpfsDir = !strncmp(dirPath, "udpfs:", 6);' in scan
assert '(!strcmp(ext, ".cue") || !strcmp(ext, ".CUE"))' in scan and 'udpfsDir &&' in scan
assert 'the cue row already stands for this game' in scan
assert 'static const char *const cueExt[] = {".cue", ".CUE"};' in launch
assert launch.index('%sPOPS/%s.VCD') < launch.index('cueExt[k]') < launch.index('vcdResolvePopstarter(')
assert (ROOT/'ship/POPS/usbhdfsd.irx.udpfs').stat().st_size == 34508

# Behaviour of the scan itself, on a fake directory: UDPFS lists *.VCD (uppercase only) and *.cue/.CUE
# once per game; any other device keeps its case-insensitive .VCD-only listing.
SCAN = r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
typedef unsigned int u32;
#define VCD_NAME_MAX 256
#define VCD_MAX_ITEMS 2048
#define ISO_GAME_NAME_MAX 160
#define LOG(...) ((void)0)
typedef struct { char name[VCD_NAME_MAX]; u32 sizeMB; } vcd_entry_t;
struct dirent { char d_name[256]; };
typedef struct { const char **names; int at; struct dirent de; } DIR;
static const char **gNames;
static DIR gDir;
static DIR *opendir(const char *) { gDir.names = gNames; gDir.at = 0; return &gDir; }
static struct dirent *readdir(DIR *d) {
    if (d->names[d->at] == NULL) return NULL;
    snprintf(d->de.d_name, sizeof(d->de.d_name), "%s", d->names[d->at++]); return &d->de;
}
static int closedir(DIR *) { return 0; }
static int gAutosort = 1;
static int vcdEntryCmp(const void *a, const void *b) { return strcmp(((const vcd_entry_t *)a)->name, ((const vcd_entry_t *)b)->name); }
static int vcdJoinDirFile(char *, int, const char *, const char *) { return -1; }
struct { void noteScanDir(const char *, const char *) {} } gVcdSupport;
""" + function('static int vcdScanOpenDir(') + r"""
static int scan(const char *dir, const char **names, vcd_entry_t **list) { gNames = names; return vcdScanOpenDir(dir, list, 0); }
int main() {
    vcd_entry_t *l = NULL;
    const char *udp[] = {"A.cue", "A.VCD", "B.VCD", "B.CUE", "C.CUE", "C.cue", "D.Cue", "E.vcd", "F.bin", "G.VCD", NULL};
    assert(scan("udpfs:/POPS", udp, &l) == 4);
    assert(!strcmp(l[0].name, "A") && !strcmp(l[1].name, "B") && !strcmp(l[2].name, "C") && !strcmp(l[3].name, "G"));
    free(l); l = NULL;
    const char *usb[] = {"A.cue", "B.VCD", "C.vcd", NULL};
    assert(scan("mass0:/POPS", usb, &l) == 2 && !strcmp(l[0].name, "B") && !strcmp(l[1].name, "C"));
    free(l);
    return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'scan.cpp'
    exe = Path(tmp) / 'scan'
    src.write_text(SCAN, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Werror', '-Wno-unused-function', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('POPS UDPFS: game-device equip, launch wiring, cue rows and pinned TEST payload passed')
