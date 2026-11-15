"""Production PS1 hashing (VCD and HDL), module ownership, the rapops handoff and rapull against host fixtures. Ported from the original's
test_ra_ps1.py; the hashing it covers cannot validate PS2 drive or IOP behavior."""
from pathlib import Path
import hashlib
import os
import re
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='ra-ps1-') as directory:
    tmp = Path(directory)

    def run(name, source, extra=()):
        cxx = 'constexpr' in source
        path = tmp / (name + ('.cpp' if cxx else '.c'))
        exe = tmp / (name + '.exe')
        source = '#ifdef _WIN32\n#include <direct.h>\n#define mkdir(path, mode) _mkdir(path)\n#endif\n' + source
        path.write_text(source, encoding='utf-8')
        objs = []
        for i, e in enumerate(map(str, extra)):
            o = str(tmp / ('%s_x%d.o' % (name, i)))
            subprocess.run(['gcc', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-I', str(root), '-c', e, '-o', o], check=True)
            objs.append(o)
        compiler = ['g++', '-std=gnu++17'] if cxx else ['gcc', '-std=gnu99', '-Wno-pointer-to-int-cast', '-Wno-int-to-pointer-cast']  # IOP addresses are 32-bit
        if name == 'snapshot' and os.name != 'nt':
            compiler += ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined']
        subprocess.run(compiler + ['-Wall', '-Wextra', '-Werror', '-I', str(root), str(path), *objs, '-o', str(exe)], check=True)
        subprocess.run([str(exe)], cwd=tmp, check=True)

    payload = bytearray((i % 251 for i in range(2048 * 3 + 17)))
    payload[:8] = b'PS-X EXE'
    struct.pack_into('<I', payload, 28, len(payload) - 2048)

    def record(name, lba, size):
        result = bytearray(34 + len(name))
        result[0] = len(result)
        struct.pack_into('<I', result, 2, lba)
        struct.pack_into('<I', result, 10, size)
        result[32] = len(name)
        result[33:33 + len(name)] = name.encode()
        return result

    def fixture(name, boot='SLUS_012.15', cnf=True, fallback=False, overflow=False, listed=None):
        iso = bytearray(40 * 2048)
        iso[16*2048+1:16*2048+6] = b'CD001'
        struct.pack_into('<II', iso, 16*2048+158, 20, 0)
        struct.pack_into('<I', iso, 16*2048+166, 2048)
        entries = b''
        if cnf:
            text = ('BOOT = cdrom:\\' + boot + ';1\r\n').encode()
            iso[22*2048:22*2048+len(text)] = text
            entries += record('SYSTEM.CNF;1', 22, len(text))
        if '\\' in boot:
            entries += record('D;1', 21, 2048)
            nested = record('G.EXE;1', 24, len(payload))
            iso[21*2048:21*2048+len(nested)] = nested
        else:
            entries += record(('PSX.EXE' if fallback else listed or boot) + ';1', 24, len(payload))
        iso[20*2048:20*2048+len(entries)] = entries
        data = bytearray(payload)
        if overflow:
            struct.pack_into('<I', data, 28, 0xfffff800)
        iso[24*2048:24*2048+len(data)] = data
        raw = bytearray(0x100000 + 40 * 2352)
        for lba in range(40):
            at = 0x100000 + lba * 2352 + 24
            raw[at:at+2048] = iso[lba*2048:(lba+1)*2048]
        (tmp / name).write_bytes(raw)

    fixture('retail.vcd')
    fixture('nested.vcd', 'D\\G.EXE')
    fixture('fallback.vcd', cnf=False, fallback=True)
    fixture('overflow.vcd', overflow=True)
    # rcheevos compares names without case: SYSTEM.CNF may name the executable in lower case.
    fixture('lower.vcd', 'slus_012.15', listed='SLUS_012.15')
    # Split a synthetic HDL game's logical ISO across two separated ATA
    # extents. Its real 1024-byte map lives at the APA game start sector.
    # No partition table or real HDD is opened or modified by this test.
    raw = (tmp / 'retail.vcd').read_bytes()
    hdisk = bytearray(1150 * 512)
    for logical in range(40):
        physical = (200 + 4 * logical) if logical < 16 else (1024 + 4 * (logical - 16))
        at = 0x100000 + logical * 2352 + 24
        hdisk[physical * 512:physical * 512 + 2048] = raw[at:at + 2048]
    hdisk[100 * 512 + 172:100 * 512 + 172 + len(b'SLUS_012.15')] = b'SLUS_012.15'
    struct.pack_into('<i', hdisk, 100 * 512 + 240, 2)
    struct.pack_into('<III', hdisk, 100 * 512 + 244, 0, 200, 16 * 2048)
    struct.pack_into('<III', hdisk, 100 * 512 + 256, 16, 1024, 24 * 2048)
    (tmp / 'hdd.raw').write_bytes(hdisk)
    (tmp / 'truncated.vcd').write_bytes((tmp / 'retail.vcd').read_bytes()[:0x100000+26*2352])
    source = re.sub(r'^#include.*$', '', (root/'src/sources/rahash.cpp').read_text(), flags=re.M)
    prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <stdlib.h>
#endif
#include "include/md5.h"
#include "include/hdl_layout.h"
typedef void (*ra_step_fn)(const char *);
static unsigned max_hdd_sectors;
static int hddReadSectors(unsigned int lba,unsigned int n,void *out) {
    FILE *f=fopen("hdd.raw","rb");int ok=0;
    if(n>max_hdd_sectors)max_hdd_sectors=n;
    if(!f)return -1;
    if(!fseek(f,(long)lba*512,SEEK_SET) && fread(out,512,n,f)==n)ok=1;
    fclose(f);
    return ok?0:-1;
}
typedef struct { unsigned char trycount, spindlctrl, datapattern, pad; } sceCdRMode;
enum { SCECdErNO=0, SCECdSecS2048=0, SCECdSpinNom=1, SCECdSpinStm=2 };
#define LOG(...) ((void)0)
#define lseek64 lseek
static int sceCdDiskReady(int m) {(void)m;return 0;}
static int sceCdRead(unsigned a,unsigned b,void *c,sceCdRMode *d) {(void)a;(void)b;(void)c;(void)d;return 0;}
static int sceCdSync(int m) {(void)m;return 0;}
static int sceCdGetError(void) {return 0;}
'''
    checks = '''
int main(void) {
#ifdef _WIN32
    _set_fmode(_O_BINARY);
#endif
    char boot[16], hash[33], key[16], key2[16];
'''
    for file, boot in [('retail.vcd', 'SLUS_012.15'), ('nested.vcd', 'D\\G.EXE'), ('fallback.vcd', 'PSX.EXE'), ('lower.vcd', 'slus_012.15')]:
        expected = hashlib.md5(boot.encode() + payload).hexdigest()
        escaped = boot.replace('\\', '\\\\')
        checks += f'assert(raHashVcd("{file}",boot,sizeof(boot),hash)==0);\n'
        checks += f'assert(!strcmp(boot,"{escaped}") && !strcmp(hash,"{expected}"));\n'
        checks += f'assert(raVcdBootName("{file}",boot,sizeof(boot))==0 && !strcmp(boot,"{escaped}"));\n'
    checks += r'''
    assert(raHashVcd("truncated.vcd",boot,sizeof(boot),hash)<0 && hash[0]==0);
    assert(raHashVcd("overflow.vcd",boot,sizeof(boot),hash)<0 && hash[0]==0);
    assert(raHashVcd("missing.vcd",boot,sizeof(boot),hash)<0 && hash[0]==0);
    assert(raVcdWatchKey("mass0:/POPS/First.VCD",key,sizeof(key))==0);
    assert(strlen(key)==15 && key[0]=='P');
    assert(raVcdWatchKey("mass0:/POPS/Second.VCD",key2,sizeof(key2))==0);
    assert(strcmp(key,key2)!=0); /* Both images can contain the same BOOT. */
    assert(raVcdWatchKey("MASS0:\\POPS\\FIRST.vcd",key2,sizeof(key2))==0);
    assert(!strcmp(key,key2)); /* Case and separator folding for FAT roots. */
    assert(raVcdWatchKey("mass1:/POPS/First.VCD",key2,sizeof(key2))==0);
    assert(strcmp(key,key2)!=0); /* A second simultaneously mounted USB. */
    assert(raVcdWatchKey("mass0:/POPS/First.VCD",key2,15)<0);
    assert(raVcdWatchKey("",key2,sizeof(key2))<0);
    /* A watch list's path key is not proof that its contents are unchanged. */
    assert(raVcdWatchGuardMatches("",key,hash)==0); /* Old pre-guard list. */
    assert(raVcdWatchGuardStore("",key,"bad")<0);
    assert(raHashVcd("retail.vcd",boot,sizeof(boot),hash)==0);
    assert(raVcdWatchGuardStore("",key,hash)<0); /* RA directory absent. */
    assert(mkdir("RA",0777)==0);
    assert(raVcdWatchGuardStore("",key,hash)==0);
    assert(raVcdWatchGuardMatches("",key,hash)==1);
    hash[0] = hash[0]=='a' ? 'b':'a';
    assert(raVcdWatchGuardMatches("",key,hash)==0); /* Different content. */
    assert(raHashVcd("retail.vcd",boot,sizeof(boot),hash)==0);
    FILE *guard=fopen("RA/P", "rb");
    assert(!guard); /* Only keyed sidecar is created. */
    assert(raVcdWatchGuardMatches("",key,hash)==1);
    assert(raVcdWatchGuardStore("","Pbad",hash)<0); /* bad key input */
    /* HDL read-only hashing crosses separated APA extents and shares the
       ISO content hash; corrupt partition counts/overlaps must fail closed. */
    assert(raHashHdl(100,"SLUS_012.15",hash)==0);
    assert(max_hdd_sectors>4 && max_hdd_sectors<=16); /* Bounded, batched raw reads. */
    assert(raHashHdl(100,"SLES_999.99",hash)<0 && !hash[0]); /* Stale list. */
    assert(raHashHdl(100,"SLUS_012.15",hash)==0);
    assert(!strcmp(hash,"''' + hashlib.md5(b"SLUS_012.15" + payload).hexdigest() + r'''"));
    FILE *disk=fopen("hdd.raw","r+b"); assert(disk);
    int bad_count=66;
    assert(!fseek(disk,100*512+240,SEEK_SET));
    assert(fwrite(&bad_count,4,1,disk)==1);
    fclose(disk);
    assert(raHashHdl(100,"SLUS_012.15",hash)<0 && !hash[0]);
    disk=fopen("hdd.raw","r+b");assert(disk);
    bad_count=2;
    assert(!fseek(disk,100*512+240,SEEK_SET));
    assert(fwrite(&bad_count,4,1,disk)==1);
    int overlap=10;
    assert(!fseek(disk,100*512+256,SEEK_SET));
    assert(fwrite(&overlap,4,1,disk)==1);
    fclose(disk);
    assert(raHashHdl(100,"SLUS_012.15",hash)<0 && !hash[0]);
    puts("PASS: HDL two-extent read-only hash, bounded partition count and overlap rejection");
    puts("PASS: PS1 hashes and stale-VCD guard, missing, mismatched and malformed sidecars");
}
'''
    run('hash_ps1', prefix + source + checks, [root/'src/platform/md5.c'])


    # The slot rapops is written into belongs to the user unless it carries rapops' magic.
    (tmp/'POPS').mkdir()
    source = (root/'src/sources/vcdsupport.cpp').read_text()
    source = source[source.index('static int vcdRaModuleStatus('):]
    run('ownership', '''
/* constexpr: compiled as C++, like the source */
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <strings.h>
#define _l(x) "test"
#define _STR_PS1_RA_CLEAN_DELETE 0
#define _STR_PS1_RA_CLEAN_FAILED 0
#define _STR_PS1_RA_CLEAN_CONTINUE 0
static char vcdSep(const char *p) {(void)p;return '/';}
static int forced_open_errno, forced_dopen_errno, forced_dread_errno;
static int module_open(const char *p, int flags) {
    if (forced_open_errno && strstr(p, "MODULE_9.IRX")) {errno=forced_open_errno;return -1;}
    return open(p, flags);
}
static DIR *module_opendir(const char *p) {
    if (forced_dopen_errno) {errno=forced_dopen_errno;return NULL;}
    return opendir(p);
}
static struct dirent *module_readdir(DIR *dir) {
    if (forced_dread_errno) {errno=forced_dread_errno;return NULL;}
    return readdir(dir);
}
static int forced_read_errno;
static ssize_t module_read(int fd, void *b, size_t n) {
    if (forced_read_errno) {errno=forced_read_errno;return -1;}
    return read(fd, b, n);
}
#define open module_open
#define opendir module_opendir
#define readdir module_readdir
#define read module_read
static int replies[3], replypos, fail_remove;
static int module_unlink(const char *p) {if(fail_remove) {errno=EACCES;return -1;}return unlink(p);}
#define unlink module_unlink
namespace host {
struct Ui { int msgBox(const char *s,int confirm,void *unused) {(void)s;(void)unused;return confirm?replies[replypos++]:0;} };
static Ui theUi;
static Ui &ui() {return theUi;}
}
''' + source + '''
int main(void) {
    assert(vcdRemoveRaModule("")==0);
    FILE *f=fopen("POPS/MODULE_9.IRX","wb");fputs("user module",f);fclose(f);
    assert(vcdRemoveRaModule("")==-1);
    assert(access("POPS/MODULE_9.IRX",0)==0);
    f=fopen("POPS/MODULE_9.IRX","wb");
    for(int i=0;i<4093;i++) fputc(0,f);
    fputs("RIPTRA02",f);fclose(f);
    replies[0]=0;replies[1]=0;replypos=0;
    assert(vcdConfirmCleanLaunch("")==0 && access("POPS/MODULE_9.IRX",0)==0);
    replies[0]=0;replies[1]=1;replypos=0;
    assert(vcdConfirmCleanLaunch("")==1 && access("POPS/MODULE_9.IRX",0)==0);
    replies[0]=1;replypos=0;
    assert(vcdConfirmCleanLaunch("")==1);
    assert(access("POPS/MODULE_9.IRX",0)<0);
    f=fopen("POPS/MODULE_9.IRX","wb");fputs("RIPTRA02",f);fclose(f);
    fail_remove=1;replies[0]=1;replies[1]=0;replypos=0;
    assert(vcdConfirmCleanLaunch("")==0 && access("POPS/MODULE_9.IRX",0)==0);
    replies[0]=1;replies[1]=1;replypos=0;
    assert(vcdConfirmCleanLaunch("")==1 && access("POPS/MODULE_9.IRX",0)==0);
    fail_remove=0;assert(vcdRemoveRaModule("")==0);
    /* FatFs's FR_NO_FILE reaches the EE as EINTR, but the directory is readable:
       no confirmation may be shown for absence. */
    forced_open_errno=EINTR;
    replies[0]=0;replypos=0;
    assert(vcdRemoveRaModule("")==0);
    assert(vcdConfirmCleanLaunch("")==1 && replypos==0);
    /* Real I/O errors keep the warning even if the file is absent. */
    forced_open_errno=EIO;replies[0]=0;replypos=0;
    assert(vcdRemoveRaModule("")==-2);
    assert(vcdConfirmCleanLaunch("")==0 && replypos==1);
    /* Inaccessible directory / failed enumeration: never infer absence. */
    forced_open_errno=EINTR;
    forced_dopen_errno=EACCES;
    assert(vcdRemoveRaModule("")==-2);
    forced_dopen_errno=0;forced_dread_errno=EIO;
    assert(vcdRemoveRaModule("")==-2);
    forced_dread_errno=0;
    /* User-owned and RiptOPL-owned files both stay when unreadable. */
    f=fopen("POPS/MODULE_9.IRX","wb");fputs("user module",f);fclose(f);
    assert(vcdRemoveRaModule("")==-2 && access("POPS/MODULE_9.IRX",0)==0);
    replies[0]=0;replypos=0;
    assert(vcdConfirmCleanLaunch("")==0 && replypos==1);
    forced_open_errno=0;
    assert(vcdRemoveRaModule("")==-1);
    /* A module that cannot be read through is not proven to be the user's. */
    forced_read_errno=EIO;
    assert(vcdRemoveRaModule("")==-2 && access("POPS/MODULE_9.IRX",0)==0);
    forced_read_errno=0;
    f=fopen("POPS/MODULE_9.IRX","wb");fputs("RIPTRA02",f);fclose(f);
    forced_open_errno=EINTR;
    assert(vcdRemoveRaModule("")==-2 && access("POPS/MODULE_9.IRX",0)==0);
    forced_open_errno=0;
    assert(vcdRemoveRaModule("")==0);
    puts("PASS: absent FatFs module needs no warning; failures remain guarded; user and RA modules preserved");
    puts("PASS: user module preserved; owned module across read boundary; delete, continue anyway and cancel choices");
}
''')

    source = re.sub(r'^#include.*$', '', (root/'src/sources/rapopslaunch.cpp').read_text(), flags=re.M)
    run('prepare', r'''
/* constexpr: compiled as C++, like the source */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "modules/network/common/rapops_cfg.h"
#define RETROACHIEVEMENTS
#define RA_MODE_CADUCEUS 1
static int gRATelemetry=1, gRAMode=1, count=1, network=1;
static int link_up=1, dhcp_ready=1, session_ready=1;
static void raLaunchNetworkUp(void) { if (!link_up || !dhcp_ready || (gRAMode==1 && !session_ready)) count=0; }
static int gRAHostIp[4];
static unsigned char rapops_irx[sizeof(struct rapops_cfg)];
static int size_rapops_irx=sizeof(rapops_irx);
/* The six modules rapops starts; sizes are set per case. */
static unsigned char ps2dev9_irx[64*1024], smsutils_irx[16], ingame_smstcpip_irx[16], smap_ingame_irx[16], raudp_irx[16], rapull_irx[16];
static int size_ps2dev9_irx=11, size_smsutils_irx=13, size_ingame_smstcpip_irx=16, size_smap_ingame_irx=1, size_raudp_irx=16, size_rapull_irx=5;
static int raVcdWatchKey(const char *p,char *s,int n) {(void)p;snprintf(s,n,"P123456789abcde");return 0;}
static int good_guard=1;
static int raHashVcd(const char *p,char *boot,int n,char *hash)
    {(void)p;snprintf(boot,n,"SLUS_012.15");snprintf(hash,33,"123456789abcdef0123456789abcdef0");return 0;}
static int raVcdWatchGuardMatches(const char *r,const char *k,const char *h)
    {(void)r;(void)k;(void)h;return good_guard;}
static char loaded_key[16];
static int sbReadWatchList(const char *p,const char *s) {(void)p;snprintf(loaded_key,sizeof(loaded_key),"%s",s);return count;}
static int GetWatchCount(void) {return count;}
static int GetWatchBytes(void) {return 4;}
static int GetNodeCount(void) {return 0;}
static unsigned int *GetWatchList(void) {static unsigned int w=RA_WATCH_PACK(15,4);return &w;}
static struct ra_node *GetNodeList(void) {return NULL;}
static unsigned int raNetPeerIP(void) {return 0;}
static int ethGetNetConfig(unsigned char *ip,unsigned char *m,unsigned char *g) {
    ip[0]=192;ip[1]=168;ip[2]=1;ip[3]=2;memset(m,255,4);memset(g,0,4);return network ? 0 : -1;
}
/* Match PS2 FAT's actual behavior: exclusive creation is ignored. */
static int fail_create, fail_write;
static int fat_open(const char *p,int flags,int mode) {
    if (fail_create) { errno=EACCES; return -1; }
    return open(p,flags & ~O_EXCL,mode);
}
static ssize_t fat_write(int fd,const void *data,size_t count) {
    if (fail_write) { errno=ENOSPC; return -1; }
    return write(fd,data,count);
}
#define open fat_open
#define write fat_write
''' + source + r'''
static struct rapops_cfg cfg;
static void read_back(void) {
    FILE *f=fopen("POPS/MODULE_9.IRX","rb");
    assert(f && fread(&cfg,1,sizeof(cfg),f)==sizeof(cfg));fclose(f);
}
int main(void) {
    memcpy(rapops_irx,RAPOPS_MAGIC,8);
    memset(ps2dev9_irx,0xd9,sizeof(ps2dev9_irx));memset(rapull_irx,0x9b,sizeof(rapull_irx));
    FILE *f=fopen("POPS/MODULE_9.IRX","wb");fputs("user module",f);fclose(f);
    assert(raPopsPrepare("","","game.vcd",0)<0);
    char buf[32]={0};f=fopen("POPS/MODULE_9.IRX","rb");assert(fread(buf,1,sizeof(buf),f)>0);fclose(f);
    assert(!strcmp(buf,"user module"));
    count=0;assert(raPopsPrepare("","","game.vcd",0)==0);
    count=1;gRATelemetry=0;assert(raPopsPrepare("","","game.vcd",0)==0);
    gRATelemetry=1;good_guard=0;
    assert(raPopsPrepare("","","game.vcd",0)==0); /* A stale list never blocks a normal launch. */
    good_guard=1;
    unlink("POPS/MODULE_9.IRX");
    network=0;assert(raPopsPrepare("","","game.vcd",1)==0);
    assert(access("POPS/MODULE_9.IRX",0)!=0); /* Offline plays without tracking. */
    network=1;
    link_up=0;assert(raPopsPrepare("","","game.vcd",0)==0);assert(access("POPS/MODULE_9.IRX",0)<0);
    link_up=1;count=1;dhcp_ready=0;assert(raPopsPrepare("","","game.vcd",1)==0);assert(access("POPS/MODULE_9.IRX",0)<0);
    dhcp_ready=1;count=1;session_ready=0;assert(raPopsPrepare("","","game.vcd",1)==0);assert(access("POPS/MODULE_9.IRX",0)<0);
    session_ready=1;count=1;gRAHostIp[0]=10;
    assert(raPopsPrepare("","","game.vcd",1)==0);
    read_back();
    assert(cfg.host_pinned==1 && cfg.client_mode==1 && cfg.count==1 && cfg.bytes==4 && !strcmp(cfg.game_id,"P123456789abcde"));
    assert(!strcmp(loaded_key,cfg.game_id) && !strcmp(cfg.host,"10.0.0.0"));
    assert(!memcmp(cfg.ipcfg,"192.168.1.2\0" "255.255.255.255\0" "0.0.0.0",cfg.ipcfg_len));
    /* Every module present, in load order, 16-byte aligned and intact. */
    assert(cfg.blob_off[RAPOPS_DEV9]==0 && cfg.blob_size[RAPOPS_DEV9]==11);
    for (int i=1;i<RAPOPS_BLOBS;i++)
        assert(cfg.blob_off[i]%16==0 && cfg.blob_off[i]>=cfg.blob_off[i-1]+cfg.blob_size[i-1] && cfg.blob_size[i]);
    assert(cfg.blob_off[RAPOPS_RAPULL]==80 && cfg.blob_size[RAPOPS_RAPULL]==5);
    assert(cfg.blob[0]==0xd9 && cfg.blob[10]==0xd9 && cfg.blob[80]==0x9b && cfg.blob[84]==0x9b);
    unlink("POPS/MODULE_9.IRX");
    /* Modules that do not fit, or one that is missing, launch untracked and write nothing. */
    size_ps2dev9_irx=RAPOPS_BLOB_BYTES-48;
    assert(raPopsPrepare("","","game.vcd",1)==0 && access("POPS/MODULE_9.IRX",0)<0);
    size_ps2dev9_irx=RAPOPS_BLOB_BYTES-80;
    assert(raPopsPrepare("","","game.vcd",1)==0);
    read_back();
    assert(cfg.blob_off[RAPOPS_RAPULL]+cfg.blob_size[RAPOPS_RAPULL]<=RAPOPS_BLOB_BYTES);
    unlink("POPS/MODULE_9.IRX");
    size_ps2dev9_irx=11;size_rapull_irx=0;
    assert(raPopsPrepare("","","game.vcd",1)==0 && access("POPS/MODULE_9.IRX",0)<0);
    size_rapull_irx=5;
    fail_create=1;
    assert(raPopsPrepare("","","game.vcd",1)==0);
    assert(access("POPS/MODULE_9.IRX",0)<0);
    fail_create=0;
    fail_write=1;
    assert(raPopsPrepare("","","game.vcd",1)==0);
    assert(access("POPS/MODULE_9.IRX",0)<0); /* Partial module removed. */
    fail_write=0;
    memset(rapops_irx,0,sizeof(rapops_irx));
    assert(raPopsPrepare("","","game.vcd",1)==0); /* Absent embedded magic. */
    assert(access("POPS/MODULE_9.IRX",0)<0);
    puts("PASS: occupied user slot preserved even when O_EXCL is ignored; modules packed aligned, oversize or missing launches untracked; create/write/config errors fail open");
}
''')

    source = re.sub(r'^#include.*$', '', (root/'modules/network/rapull/rapull.c').read_text(), flags=re.M)
    source = source[:source.index('static void rp_thread(')]
    run('snapshot', r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "modules/network/common/ra_snap.h"
#include "modules/network/common/ra_watch.h"
typedef uint32_t u32;
typedef uint8_t u8;
typedef int SifRpcReceiveData_t;
#define IRX_ID(a,b,c)
#define ALLOC_FIRST 0
static unsigned char ram[0x200000];
static int fail;
static void *AllocSysMemory(int a,int b,void *c) {(void)a;(void)c;return calloc(1,b);}
static void FreeSysMemory(void *p) {free(p);}
static int sceSifGetOtherData(void *a,void *b,void *c,int d,int e) {
    (void)a;(void)e;
    if(fail)return -1;
    memcpy(c,ram+((uintptr_t)b-0x1000000),d);return 0;
}
''' + source + r'''
int main(void) {
    u32 entries[]={RA_WATCH_PACK(15,4), RA_WATCH_PACK(0x1fffff,1)};
    unsigned char snap[RA_SNAP_TOTAL]={0};
    rp_snap=(void *)snap;rp_list=entries;rp_count=2;rp_bytes=5;
    assert(rp_hex_at("0000000f",0,8)==15);
    assert(rp_setup());
    ram[15]=1;ram[16]=2;ram[17]=3;ram[18]=4;ram[0x1fffff]=7;
    rp_frames=1;rp_frame_values(1);
    assert(rp_snap->magic==RA_SNAP_MAGIC && rp_snap->bytes==5);
    assert(!memcmp(snap+RA_SNAP_HDR,"\1\2\3\4\7",5));
    u32 seq=rp_snap->seq;
    fail=1;rp_frame_values(1);
    assert(rp_snap->seq==seq && rp_fail==1);
    fail=0;ram[15]=8;rp_frame_values(1);
    assert(rp_snap->seq==seq+1 && snap[RA_SNAP_HDR]==8);
    assert(*(u32 *)(snap+RA_SNAP_TRAILER_OFF(5))==rp_snap->seq);
    // Production IOP sampler resolves indexed chains and a dependent node.
    struct ra_node nodes[]={{RA_NODE_PACK(0,0,1,4,4),0x1000,4},
                            {RA_NODE_PACK(0,1,1,0,0),0x2000,1}};
    rp_node_count=2;rp_nodes=nodes;
    FreeSysMemory(rp_win);FreeSysMemory(rp_entry_off);FreeSysMemory(rp_node_val);FreeSysMemory(rp_stage);
    assert(rp_setup());
    ram[15]=0x20;ram[16]=ram[17]=ram[18]=0;
    ram[0x1008]=3;ram[0x2003]=0x55;
    rp_frame_values(1);
    u32 pairs[4];memcpy(pairs,snap+RA_SNAP_HDR+5,sizeof(pairs));
    assert(pairs[0]==0x1008 && pairs[1]==3 && pairs[2]==0x2003 && pairs[3]==0x55);
    ram[15]=0;ram[0x1000]=2;rp_frame_values(1);
    memcpy(pairs,snap+RA_SNAP_HDR+5,8);assert(pairs[0]==0x1000 && pairs[1]==2);
    nodes[0].offset=0x300000;nodes[1].offset=0x2000;ram[0x2000]=0xAA;
    rp_frame_values(1);memcpy(pairs,snap+RA_SNAP_HDR+5,sizeof(pairs));
    assert(pairs[0]==0 && pairs[1]==0 && pairs[2]==0 && pairs[3]==0);
    nodes[0].offset=0x1000;ram[0x1000]=0;
    rp_frame_values(1);memcpy(pairs,snap+RA_SNAP_HDR+5,sizeof(pairs));
    assert(pairs[0]==0x1000 && pairs[1]==0 && pairs[2]==0x2000 && pairs[3]==0xAA);
    nodes[0].w|=RA_NODE_LEGACY;nodes[0].offset=0x2000;
    rp_frame_values(1);memcpy(pairs,snap+RA_SNAP_HDR+5,8);
    assert(pairs[0]==0 && pairs[1]==0);
    FreeSysMemory(rp_win);FreeSysMemory(rp_entry_off);FreeSysMemory(rp_node_val);FreeSysMemory(rp_stage);
    puts("PASS: PS1 read windows, unaligned and last-byte reads, failed read suppresses frame, recovery and trailer");
}
''')

    # Compile the production badge cache against real directories. PS1 artwork and settings still
    # use the VCD filename; only the RA watch files use the P key.
    header = re.sub(r'^#include.*$', '', (root/'include/rabadge.h').read_text(), flags=re.M)
    badge_source = re.sub(r'^#include.*$', '', (root/'src/sources/rabadge.cpp').read_text(), flags=re.M)
    run('badge_ps1', r'''
/* constexpr: compiled as C++, like the source */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#define RETROACHIEVEMENTS 1
#define MODE_COUNT 15
#define FAV_MODE 13
#define LIB_VIEW_PS1 1
#define ISO_GAME_NAME_MAX 160
#define FAV_KIND_VCD 1
struct item_list { int mode; };
typedef struct item_list item_list_t;
typedef struct { char extension[8]; } base_game_info_t;
static int gRATelemetry=1, gRABadges=1, gRABadgeSuffix=0, view=1, favkind=1, favmode=0, gVcdHideGameId;
static char name[32]="First", startup[32]="First", full[256], usbPrefix[]="mass0:/games/";
static base_game_info_t game={".VCD"};
static item_list_t usb={0}, favorite={FAV_MODE};
struct GameSource {
    bool hasPrefix() const {return this==&usbSource;}
    bool hasGame() const {return this==&usbSource;}
    char *prefix(item_list_t *x) {(void)x;return usbPrefix;}
    void *game(item_list_t *x,int i) {(void)x;(void)i;return &::game;}
    char *name(item_list_t *x,int i) {(void)x;(void)i;return ::name;}
    char *startup(item_list_t *x,int i) {(void)x;(void)i;return ::startup;}
    static GameSource usbSource, favSource;
};
GameSource GameSource::usbSource, GameSource::favSource;
static GameSource *gsOf(item_list_t *x) {return x->mode==FAV_MODE ? &GameSource::favSource : &GameSource::usbSource;}
struct FavSupport {
    item_list_t *ownerView(int i,item_list_t *v) {(void)i;v->mode=favmode;return v;}
};
static FavSupport gFavSupport;
namespace host {
struct Library { int listRowView(item_list_t *x,int i) {(void)x;(void)i;return view;} };
static Library theLibrary;
static Library &library() {return theLibrary;}
}
static const char *vcdDisplayNameForRow(item_list_t *x,int i,const char *text) {
    (void)x;(void)i;
    return gVcdHideGameId && view==LIB_VIEW_PS1 && !strncmp(text,"SLUS_012.15.",12) ? text+12 : text;
}
static int favGetItemKind(int i){(void)i;return favkind;}
static int favGetItemSourceMode(int i){(void)i;return favmode;}
static int bdmModeIsUSB(int m){return m==0;}
static int raVcdWatchKey(const char *p,char *out,int sz) {
    snprintf(full,sizeof(full),"%s",p);
    return snprintf(out,sz,"P123456789abcde")==15?0:-1;
}
''' + header + badge_source + r'''
int main(void) {
    mkdir("mass0:",0700);
    mkdir("mass0:/games",0700);
    mkdir("mass0:/games/RA",0700);
    FILE *f=fopen("mass0:/games/RA/P123456789abcde.wl","wb");
    assert(f); fputs("watch",f);fclose(f);

    raBadgeRefresh(&usb,1);
    assert(raBadgeText(&usb,0) && !strcmp(raBadgeText(&usb,0),"RA First"));
    assert(!strcmp(full,"mass0:/POPS/First.VCD"));

    raBadgeRefresh(&favorite,1);
    assert(raBadgeText(&favorite,0) && !strcmp(raBadgeText(&favorite,0),"RA First"));

    gRABadgeSuffix=1;
    raBadgeRefresh(&usb,1);
    assert(raBadgeText(&usb,0) && !strcmp(raBadgeText(&usb,0),"First [RA]"));
    raBadgeRefresh(&favorite,1);
    assert(raBadgeText(&favorite,0) && !strcmp(raBadgeText(&favorite,0),"First [RA]"));
    gRABadgeSuffix=0;

    strcpy(name,"SLUS_012.15.First");
    gVcdHideGameId=1;
    raBadgeRefresh(&usb,1);
    assert(!strcmp(raBadgeText(&usb,0),"RA First"));
    assert(!strcmp(full,"mass0:/POPS/SLUS_012.15.First.VCD"));
    raBadgeRefresh(&favorite,1);
    assert(!strcmp(raBadgeText(&favorite,0),"RA First"));
    gVcdHideGameId=0;
    raBadgeRefresh(&usb,1);
    assert(!strcmp(raBadgeText(&usb,0),"RA SLUS_012.15.First"));
    strcpy(name,"First");

    favkind=2; /* Ember CUE cannot be tracked by the POPStarter bridge. */
    raBadgeRefresh(&favorite,1);
    assert(raBadgeText(&favorite,0)==NULL);
    favkind=FAV_KIND_VCD;favmode=5; /* A VCD favourite from a non-USB device. */
    raBadgeRefresh(&favorite,1);
    assert(raBadgeText(&favorite,0)==NULL);
    favmode=0;

    /* An SMB-only PS1 watch list cannot be loaded by USB POPStarter and must not
       trigger remote probes or a false local badge. */
    assert(unlink("mass0:/games/RA/P123456789abcde.wl")==0);
    assert(mkdir("smb0:RA",0700)==0);
    f=fopen("smb0:RA/P123456789abcde.wl","wb");
    assert(f);fputs("remote",f);fclose(f);
    raBadgeRefresh(&usb,1);
    assert(raBadgeText(&usb,0)==NULL);
    raBadgeRefresh(&favorite,1);
    assert(raBadgeText(&favorite,0)==NULL);

    /* PS2 rows: their serial, the share fallback, and favourites through their source device. */
    view=0; strcpy(startup,"SLUS_210.65");
    f=fopen("mass0:/games/RA/SLUS_210.65.wl","wb");
    assert(f);fputs("watch",f);fclose(f);
    raBadgeRefresh(&usb,1);
    assert(raBadgeText(&usb,0) && !strcmp(raBadgeText(&usb,0),"RA First"));
    raBadgeRefresh(&favorite,1);
    assert(raBadgeText(&favorite,0) && !strcmp(raBadgeText(&favorite,0),"RA First"));
    assert(unlink("mass0:/games/RA/SLUS_210.65.wl")==0);
    f=fopen("smb0:RA/SLUS_210.65.wl","wb");
    assert(f);fputs("remote",f);fclose(f);
    raBadgeRefresh(&usb,1);
    assert(raBadgeText(&usb,0) && !strcmp(raBadgeText(&usb,0),"RA First"));

    gRABadges=0;raBadgeRefresh(&usb,1);
    assert(raBadgeText(&usb,0)==NULL);
    puts("PASS: USB PS1/Favorites per-VCD badges, excluded Ember and non-USB VCDs, PS2 and PS2 favourite badges");
}
''')
