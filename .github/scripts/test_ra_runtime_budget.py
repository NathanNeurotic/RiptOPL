"""Actual EE-core setup/sampling/pacing and C++ watch parser at expanded bounds."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
core = (root / 'ee_core/src/ra.c').read_text(encoding='utf-8')
core_functions = Path(sys.argv[1]).read_text(encoding='utf-8') if len(sys.argv)>1 else core
watch = (root / 'src/sources/rawatch.cpp').read_text(encoding='utf-8')


def function(source, signature):
    first = source.index(signature)
    return source[first:source.index('\n}', first) + 2]


def run(name, program):
    with tempfile.TemporaryDirectory() as temp:
        cpp, exe = Path(temp)/(name+'.cpp'), Path(temp)/name
        (Path(temp)/'osd_config.h').write_text('typedef struct {unsigned int unused;} ConfigParam;\n')
        cpp.write_text(program, encoding='utf-8')
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-int-to-pointer-cast',
                        '-fsanitize=address,undefined','-fno-sanitize-recover=undefined',
                        '-I',temp,'-I',str(root),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=30)


prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef uint64_t u64;
#define RETROACHIEVEMENTS
#include "modules/network/common/ra_snap.h"
'''
state = core[core.index('static u8 *ra_snap_buf'):core.index('#define RA_RAM_HIGH')]
state += '#define RA_RAM_HIGH 0x02000000\n'
# The scratchpad has no host address either: the reader's 0x70000000 becomes a 16 KB array.
state += '#undef RA_SPR_BASE\nstatic u8 scratchpad[0x4000 + 16];\n#define RA_SPR_BASE ((uintptr_t)scratchpad)\n'
# COP0 Count has no host equivalent: each read advances a fake clock by 7.
state = state.replace(function(state, 'static inline u32 ra_ticks'), 'static u32 ticks;\nstatic u32 ra_ticks(void) { return ticks += 7; }')
run('ee_ra_budget', prefix + r'''
#include "ee_core/include/coreconfig.h"
struct EECoreConfig_t g_ee_core_config;
static u8 gameRam[0x02000000];
static void *uncached(uintptr_t address) {return address<sizeof(gameRam) ? gameRam+address : (void*)address;}
#define UNCACHED_SEG(address) uncached((uintptr_t)(address))
#define RA_START_DELAY 600
#define RA_PROBE 0
unsigned int ra_snap_iop=0x1000;
typedef struct {void *src,*dest;int size,attr;} SifDmaTransfer_t;
static int transfers, overlays, dmaBusy;
static int isceSifDmaStat(int) {return dmaBusy ? 0 : -1;}
static int isceSifSetDma(SifDmaTransfer_t *dma,int count) {
    assert(count==1 && dma->size>0 && dma->size<=RA_SNAP_TOTAL && dma->size%16==0);
    const ra_snap *snap=(const ra_snap*)dma->src;
    assert(snap->bytes<=RA_SNAP_MAX_BYTES);
    assert(*(u32*)((u8*)dma->src+RA_SNAP_TRAILER_OFF(snap->bytes))==snap->seq);
    transfers++;return transfers;
}
static void RA_OverlayOnVblank(unsigned) {overlays++;}
''' + state + '\n' + '\n'.join(function(core_functions,s) for s in
    ('int RA_SnapBytes(', 'void RA_SetupWatchList(', 'static void ra_snap_send(', 'void RA_OnVblank(')) + r'''
static u8 work[RA_EE_WORK_BYTES(RA_WATCH_MAX,RA_NODE_MAX)+128] __attribute__((aligned(64)));
static u32 entries[RA_WATCH_MAX];
static ra_node nodes[RA_NODE_MAX];
static void setup(int count,int bytes,int chains=0) {
    memset(work,0xCC,sizeof(work));g_ee_core_config={};
    auto &cfg=g_ee_core_config;
    cfg.raWatchList=entries;cfg.raWatchCount=count;cfg.raSnapBytes=bytes;
    cfg.raNodeList=chains ? nodes : nullptr;cfg.raNodeCount=chains;
    cfg.raWorkArea=work;cfg.raWorkBytes=RA_EE_WORK_BYTES(count,chains);
    strcpy(cfg.GameID,"SLUS_123.45");
    transfers=overlays=dmaBusy=0;ra_frames=ra_snap_dma_id=ra_snap_seq=0;
    RA_SetupWatchList();
}
static void tailUnchanged(unsigned start) {for(unsigned i=start;i<sizeof(work);i++)assert(work[i]==0xCC);}
int main() {
    assert(RA_WATCH_MAX==4096 && RA_SNAP_PARTS==9);
    for(int parts=1;parts<=9;parts++) {
        int bytes=((parts-1)*RA_SNAP_CHUNK_BYTES/4+1)*4;
        int count=bytes/4;
        for(int i=0;i<count;i++)entries[i]=RA_WATCH_PACK(0x80000+4*i,4);
        setup(count,bytes);assert(ra_watch_count==count && ra_snap_bytes==bytes && RA_SnapBytes()==bytes);
        assert(ra_snap_every==(unsigned)(parts+2)/3);
        for(int frame=0;frame<1200;frame++)RA_OnVblank();
        assert(overlays==1200 && transfers==600/(int)ra_snap_every);
        tailUnchanged(g_ee_core_config.raWorkBytes);
    }
    // Full entry/node budgets, with source + scratch words beyond 4096.
    int bytes=0, extra=RA_SNAP_MAX_BYTES-RA_NODE_MAX*RA_NODE_PAIR_BYTES-RA_WATCH_MAX;
    for(int i=0;i<RA_WATCH_MAX;i++) {
        int size=extra>=3 ? 4 : extra>0 ? 2 : 1;
        extra-=size-1;
        entries[i]=RA_WATCH_PACK(0x80000+4*i,size);bytes+=size;
    }
    assert(extra==0 && RA_NODE_MAX==512);
    for(int i=0;i<RA_NODE_MAX;i++)nodes[i]={RA_NODE_PACK(0,0,4,0,0),(u32)i*4,1};
    u32 base=0x100000;memcpy(gameRam+0x80000,&base,4);
    for(int i=0;i<RA_NODE_MAX;i++){u32 v=0x12340000+i;memcpy(gameRam+base+i*4,&v,4);}
    // The final node follows a pointer from node 510, beyond the old index ceiling.
    const u32 secondBase=0x110000, lastValue=0xABCD1234;
    memcpy(gameRam+base+(RA_NODE_MAX-2)*4,&secondBase,4);
    memcpy(gameRam+secondBase,&lastValue,4);
    nodes[RA_NODE_MAX-1]={RA_NODE_PACK(RA_NODE_MAX-2,1,4,0,0),0,1};
    setup(RA_WATCH_MAX,bytes,RA_NODE_MAX);
    assert(ra_watch_count==4096 && ra_node_count==RA_NODE_MAX && ra_snap_bytes==RA_SNAP_MAX_BYTES && ra_snap_every==3);
    ra_snap_send();assert(transfers==1 && RA_SnapBytes()==RA_SNAP_MAX_BYTES);
    // The read cost is two clock reads apart; the frame is the gap between two VBLANKs.
    RA_OnVblank();RA_OnVblank();ra_snap_dma_id=0;ra_snap_send();
    assert(((const ra_snap*)work)->read_cycles==7 && ((const ra_snap*)work)->frame_cycles==7);
    const u8 *vals=work+RA_SNAP_HDR;
    for(int i=0;i<RA_NODE_MAX;i++) {
        u32 address,value;memcpy(&address,vals+bytes+i*8,4);memcpy(&value,vals+bytes+i*8+4,4);
        assert(address==(i==RA_NODE_MAX-1 ? secondBase : base+(u32)i*4));
        assert(value==(i==RA_NODE_MAX-1 ? lastValue : i==RA_NODE_MAX-2 ? secondBase : 0x12340000+(u32)i));
    }
    tailUnchanged(g_ee_core_config.raWorkBytes);
    // xeRAbora 2.1.0: direct entries in 0x02000000-0x02003FFF are read from the scratchpad, at every
    // size and alignment. One that would run past its end, and anything above it, is sent as zero.
    memset(scratchpad,0xEE,sizeof(scratchpad));
    for(int i=0;i<16;i++){scratchpad[i]=0x10+i;scratchpad[0x3FF0+i]=0xA0+i;}
    entries[0]=RA_WATCH_PACK(0x02000000,4);entries[1]=RA_WATCH_PACK(0x02000005,4);
    entries[2]=RA_WATCH_PACK(0x02000008,2);entries[3]=RA_WATCH_PACK(0x0200000B,2);
    entries[4]=RA_WATCH_PACK(0x0200000D,1);entries[5]=RA_WATCH_PACK(0x02003FFC,4);
    entries[6]=RA_WATCH_PACK(0x02003FFF,1);entries[7]=RA_WATCH_PACK(0x02003FFE,4);
    entries[8]=RA_WATCH_PACK(0x02003FFF,2);entries[9]=RA_WATCH_PACK(0x02004000,1);
    entries[10]=RA_WATCH_PACK(0x01FFFFFF,2);entries[11]=RA_WATCH_PACK(0x01FFFFFE,2);
    gameRam[0x01FFFFFE]=0x61;gameRam[0x01FFFFFF]=0x62;
    setup(12,4+4+2+2+1+4+1+4+2+1+2+2);ra_snap_send();
    {
        const u8 want[]={0x10,0x11,0x12,0x13, 0x15,0x16,0x17,0x18, 0x18,0x19, 0x1B,0x1C, 0x1D,
                         0xAC,0xAD,0xAE,0xAF, 0xAF, 0,0,0,0, 0,0, 0, 0,0, 0x61,0x62};
        assert(!memcmp(work+RA_SNAP_HDR,want,sizeof(want)));
    }
    // A chain whose parent sits in the scratchpad stays unresolved, as upstream leaves it.
    nodes[0]={RA_NODE_PACK(0,0,1,0,0),0x100000,1};
    u32 pair[4];
    setup(1,4,1);ra_snap_send();memcpy(pair,work+RA_SNAP_HDR+4,8);
    assert(!memcmp(work+RA_SNAP_HDR,scratchpad,4) && pair[0]==0 && pair[1]==0);
    // RAN2 nibble extraction, index zero, negative scale and dependent nodes.
    entries[0]=RA_WATCH_PACK(0x80000,1);
    gameRam[0x80000]=0x30; gameRam[0x10000c]=0x5A;
    nodes[0]={RA_NODE_PACK(0,0,1,4,4),0x100000,4};
    nodes[1]={RA_NODE_PACK(0,1,1,0,0),0x11005a,0xFFFFFFFFu};
    gameRam[0x110000]=0x77;
    setup(1,1,2);ra_snap_send();
    memcpy(pair,work+RA_SNAP_HDR+1,sizeof(pair));
    assert(pair[0]==0x10000c && pair[1]==0x5A && pair[2]==0x110000 && pair[3]==0x77);
    gameRam[0x80000]=0;gameRam[0x100000]=0x33;
    setup(1,1,1);ra_snap_send();memcpy(pair,work+RA_SNAP_HDR+1,8);
    assert(pair[0]==0x100000 && pair[1]==0x33);
    nodes[0]={RA_NODE_PACK(0,0,1,0,32),0x100000,1};
    setup(1,1,1);ra_snap_send();memcpy(pair,work+RA_SNAP_HDR+1,8);
    assert(pair[0]==0x100000 && pair[1]==0x33);
    tailUnchanged(g_ee_core_config.raWorkBytes);
    // An unresolved ancestor is not a successful read of index zero.
    nodes[0]={RA_NODE_PACK(0,0,1,0,0),4,1};
    nodes[1]={RA_NODE_PACK(0,1,1,0,0),0x110000,1};
    setup(1,1,2);ra_snap_send();memcpy(pair,work+RA_SNAP_HDR+1,sizeof(pair));
    assert(pair[0]==0 && pair[1]==0 && pair[2]==0 && pair[3]==0);
    nodes[0].offset=0x100000;gameRam[0x100000]=0;
    setup(1,1,2);ra_snap_send();memcpy(pair,work+RA_SNAP_HDR+1,sizeof(pair));
    assert(pair[0]==0x100000 && pair[1]==0 && pair[2]==0x110000 && pair[3]==0x77);
    entries[0]=RA_WATCH_PACK(0x7ffff,1);
    setup(1,1,2);ra_snap_send();memcpy(pair,work+RA_SNAP_HDR+1,sizeof(pair));
    assert(pair[0]==0 && pair[1]==0 && pair[2]==0 && pair[3]==0);
    // Undersized or unaligned reservations refuse without writing into them.
    setup(1,4);g_ee_core_config.raWorkBytes--;memset(work,0xCC,sizeof(work));RA_SetupWatchList();
    assert(!ra_watch_count && !RA_SnapBytes());tailUnchanged(0);
    setup(1,4);g_ee_core_config.raWorkArea=work+1;memset(work,0xCC,sizeof(work));RA_SetupWatchList();
    assert(!ra_watch_count);tailUnchanged(0);
    setup(1,4);g_ee_core_config.raSnapBytes=3;memset(work,0xCC,sizeof(work));RA_SetupWatchList();
    assert(!ra_watch_count);tailUnchanged(0);
    // Wider direct reads at odd addresses must use byte loads.
    entries[0]=RA_WATCH_PACK(0x80001,4);entries[1]=RA_WATCH_PACK(0x80009,2);
    for(int i=0;i<16;i++)gameRam[0x80000+i]=(u8)i;
    setup(2,6);ra_snap_send();assert(!memcmp(work+RA_SNAP_HDR,gameRam+0x80001,4));
    assert(!memcmp(work+RA_SNAP_HDR+4,gameRam+0x80009,2));
    // Unreadable addresses produce zeros without disturbing snapshot shape.
    entries[0]=RA_WATCH_PACK(0x7ffff,4);entries[1]=RA_WATCH_PACK(0x1ffffff,2);
    setup(2,6);ra_snap_send();for(int i=0;i<6;i++)assert(work[RA_SNAP_HDR+i]==0);
    // A chain whose base pointer is null sends address 0 and value 0, never reading through it
    // (ported from the original's test_ra_watch), including legacy RANL with an in-range offset.
    entries[0]=RA_WATCH_PACK(0x80000,4);memset(gameRam+0x80000,0,4);gameRam[0x80004]=0xAA;gameRam[0x100000]=0x55;
    nodes[0]={RA_NODE_PACK(0,0,4,0,0),4,1};
    setup(1,4,1);ra_snap_send();
    {const u8 zero[8]={0};assert(!memcmp(work+RA_SNAP_HDR+4,zero,8));}
    nodes[0]={RA_NODE_PACK(0,0,1,0,0)|RA_NODE_LEGACY,0x100000,1};
    setup(1,4,1);ra_snap_send();
    {const u8 zero[8]={0};assert(!memcmp(work+RA_SNAP_HDR+4,zero,8));}
    puts("PASS: EE module-storage bounds, full entries/nodes, IRQ reads, null chains and one/two/three-frame pacing");
}
''')

run('loader_ra_budget', prefix + r'''
#include "include/rawatch.h"
#include "include/ranet.h"
#include "modules/network/common/rapops_cfg.h"
static_assert(RA_NODE_MAX_PS1 == 256 && sizeof(rapops_cfg::nodes) / sizeof(ra_node) == RA_NODE_MAX_PS1);
#define LOG(...) ((void)0)
static std::vector<u32> disk;
static unsigned readPosition;
static int fake_open(const char*,int) {readPosition=0;return 3;}
static int fake_read(int,void *out,int len) {
    len=std::min<unsigned>(len,disk.size()*4-readPosition);
    memcpy(out,(u8*)disk.data()+readPosition,len);readPosition+=len;return len;
}
static int fake_close(int) {return 0;}
#define open fake_open
#define read fake_read
#define close fake_close
#define O_RDONLY 0
''' + '\n'.join(function(watch,s) for s in
    ('void RaWatch::clearWatchList(', 'bool RaWatch::takeNodes(', 'static bool raWatchHashTailValid(', 'int RaWatch::setWatchList(', 'int RaWatch::loadWatchList(')) + r'''
int main() {
    RaWatch watch;
    std::vector<u32> blob(4+RA_WATCH_MAX+2+RA_NODE_MAX*3);
    auto *header=(ra_watch_file*)blob.data();
    *header={RA_WATCH_MAGIC,RA_WATCH_VERSION,RA_WATCH_MAX,RA_WATCH_MAX};
    for(int i=0;i<RA_WATCH_MAX;i++)blob[4+i]=RA_WATCH_PACK(0x80000+i,1);
    auto *nf=(ra_node_file*)&blob[4+RA_WATCH_MAX];*nf={RA_NODE_MAGIC,RA_NODE_MAX};
    auto *node=(ra_node*)(nf+1);for(int i=0;i<RA_NODE_MAX;i++)node[i]={RA_NODE_PACK(0,0,1,0,0),0,1};
    int len=blob.size()*4;
    assert(watch.setWatchList(blob.data(),len,"SLUS_123.45")==4096 && watch.nodeCount()==RA_NODE_MAX);
    assert(len>20*1024 && len<RA_MAX_BYTES);
    disk=blob;watch.clearWatchList();
    assert(watch.loadWatchList("save.wl","SLUS_123.45")==4096 && watch.nodeCount()==RA_NODE_MAX);
    for(int bad : {0,3,5,15}) {
        blob[4]=RA_WATCH_PACK(0x80000,bad);
        assert(watch.setWatchList(blob.data(),len,"SLUS_123.45")<0 && !watch.watchCount());
        disk=blob;assert(watch.loadWatchList("save.wl","SLUS_123.45")<0 && !watch.watchCount());
    }
    blob[4]=RA_WATCH_PACK(0x80000,1);header->bytes++;
    assert(watch.setWatchList(blob.data(),len,"SLUS_123.45")<0 && !watch.watchCount());
    disk=blob;assert(watch.loadWatchList("save.wl","SLUS_123.45")<0 && !watch.watchCount());
    header->bytes--;nf->count=0xffffffff;
    assert(watch.setWatchList(blob.data(),len,"SLUS_123.45")<0 && !watch.watchCount());
    assert(watch.setWatchList(blob.data(),16+4096*4-1,"SLUS_123.45")<0);
    // Legacy nodes normalize in memory and from disk, including alias expansion.
    disk={RA_WATCH_MAGIC,1,1,4,RA_WATCH_PACK(0x80000,4),RA_NODE_LEGACY_MAGIC,2,
          RA_NODE_PACK(0,0,4,0,0),4,RA_NODE_PACK(0,1,1,0,0),8};
    watch.clearWatchList();
    assert(watch.loadWatchList("legacy.wl","SLUS_123.45")==1 && watch.nodeCount()==2);
    assert(watch.nodeList()[0].scale==1 && watch.nodeList()[1].scale==1 && watch.nodeList()[1].offset==8);
    assert(watch.setWatchList(disk.data(),disk.size()*4,"SLUS_123.45")==1 && watch.nodeCount()==2);
    disk[5]=0xDEADBEEF;
    assert(watch.setWatchList(disk.data(),disk.size()*4,"SLUS_123.45")<0 && !watch.watchCount());
    watch.clearWatchList();assert(watch.loadWatchList("unknown.wl","SLUS_123.45")<0 && !watch.watchCount());
    disk[5]=RA_NODE_MAGIC;
    assert(watch.setWatchList(disk.data(),disk.size()*4,"SLUS_123.45")<0);
    // xeRAbora hash suffix: direct-only, legacy chains and all 512 PS2 RAN2 nodes.
    auto withHash = [](std::vector<u32> original) {
        const char *md5 = "0123456789abcdef0123456789abcdef";
        original.push_back(RA_HASH_MAGIC);
        for(int i=0;i<RA_HASH_LEN;i+=4) {u32 word;memcpy(&word,md5+i,4);original.push_back(word);}
        return original;
    };
    nf->count=RA_NODE_MAX;
    const std::vector<u32> bare={RA_WATCH_MAGIC,1,1,4,RA_WATCH_PACK(0x80000,4)};
    const std::vector<u32> legacy={RA_WATCH_MAGIC,1,1,4,RA_WATCH_PACK(0x80000,4),
                                   RA_NODE_LEGACY_MAGIC,1,RA_NODE_PACK(0,0,1,0,0),4};
    for (const auto &original : {bare,legacy,blob}) {
        auto current=withHash(original);
        int expected=original.size()==bare.size()?0:original.size()==legacy.size()?1:RA_NODE_MAX;
        assert(watch.setWatchList(current.data(),current.size()*4,"SLUS_123.45")>0 && watch.nodeCount()==expected);
        assert(!strcmp(watch.watchHash(),"0123456789abcdef0123456789abcdef"));
        disk=current;watch.clearWatchList();
        assert(!watch.watchHash()[0]);
        assert(watch.loadWatchList("hash.wl","SLUS_123.45")>0 && watch.nodeCount()==expected);
        assert(!strcmp(watch.watchHash(),"0123456789abcdef0123456789abcdef"));
        // The same list from a client before 2.1.0 carries no hash and leaves none behind.
        assert(watch.setWatchList(original.data(),original.size()*4,"SLUS_123.45")>0 && !watch.watchHash()[0]);
        assert(watch.setWatchList(current.data(),current.size()*4,"SLUS_123.45")>0 && watch.watchHash()[0]);
        current.back()=0x7A7A7A7A; // not hexadecimal
        assert(watch.setWatchList(current.data(),current.size()*4,"SLUS_123.45")<0 && !watch.watchCount());
        assert(!watch.watchHash()[0]);
        disk=current;assert(watch.loadWatchList("invalid.wl","SLUS_123.45")<0 && !watch.watchCount());
        assert(!watch.watchHash()[0]);
        current=withHash(original);current.pop_back();
        assert(watch.setWatchList(current.data(),current.size()*4,"SLUS_123.45")<0 && !watch.watchCount());
        disk=current;assert(watch.loadWatchList("short.wl","SLUS_123.45")<0 && !watch.watchCount());
        current=withHash(original);current.push_back(0);
        assert(watch.setWatchList(current.data(),current.size()*4,"SLUS_123.45")<0 && !watch.watchCount());
        disk=current;assert(watch.loadWatchList("extra.wl","SLUS_123.45")<0 && !watch.watchCount());
    }
    assert(16+4*RA_WATCH_MAX+8+12*RA_NODE_MAX+sizeof(ra_hash_file)<=RA_MAX_BYTES);
    puts("PASS: real C++ parser accepts RAH1/RANL/RAN2 and rejects invalid suffixes");
}
''')

app=(root/'src/app/host.cpp').read_text(encoding='utf-8')
first=app.index('#ifdef RETROACHIEVEMENTS\n    // Snapshot, watch words')
last=app.index('unsigned int raWorkBytes(const void *at) override')
method=app[first:app.index('\n    }',last)+6]
overlay=app.index('        const unsigned int list = raListBytes();',app.index('void fillCoreConfig'))
overlay=app[overlay:app.index(': NULL;',overlay)+7]
run('launch_ra_capacity', prefix + r'''
static unsigned int entries[RA_WATCH_MAX];
static int count,bytes,nodes,gRATelemetry=1,gRAMode,gRAExperimentalCards,notes;
#define RA_MODE_CADUCEUS 1
#include "include/ra_features.h"
static void raLaunchNote(const char *,int,int) {notes++;}
unsigned int *GetWatchList() {return entries;}
int GetWatchCount() {return count;}
int GetWatchBytes() {return bytes;}
int GetNodeCount() {return nodes;}
struct LaunchServices {virtual unsigned int raWorkBytes(const void *at)=0;};
struct Config {void *raOverlayBuf;};
class OplLaunch final:public LaunchServices {public:
''' + method + r'''
    void *overlayFor(void *raWorkArea, unsigned int raWorkBytes) {
        Config c, *config=&c;
''' + overlay + r'''
        return c.raOverlayBuf;
    }
};
int main() {
    for(int i=0;i<RA_WATCH_MAX;i++)entries[i]=RA_WATCH_PACK(0x80000+i,1);
    OplLaunch launch;
    for(int n : {0,1,1024,4096,4097}) {
        count=bytes=n;nodes=0;
        assert(launch.raWorkBytes(nullptr)==(n>0 && n<=4096 ? (unsigned)RA_EE_WORK_BYTES(n,0) : 0));
    }
    count=bytes=4096;nodes=RA_NODE_MAX;
    assert(launch.raWorkBytes(nullptr)==RA_EE_WORK_BYTES(4096,RA_NODE_MAX));
    const unsigned full=RA_EE_WORK_BYTES(4096,RA_NODE_MAX);
    const unsigned aligned=(full+63)&~63u;
    for (uintptr_t end : {uintptr_t(0x00100000),uintptr_t(0x02000000)}) {
        assert(launch.raWorkBytes((const void *)(end-aligned))==full);
        assert(!launch.raWorkBytes((const void *)(end-aligned+64)));
    }
    nodes=-1;assert(!launch.raWorkBytes(nullptr));nodes=RA_NODE_MAX+1;assert(!launch.raWorkBytes(nullptr));
    nodes=0;bytes--;assert(!launch.raWorkBytes(nullptr));bytes++;
    entries[0]=RA_WATCH_PACK(0x80000,3);assert(!launch.raWorkBytes(nullptr));
    entries[0]=RA_WATCH_PACK(0x80000,1);gRATelemetry=0;assert(!launch.raWorkBytes(nullptr));
    puts("PASS: actual C++ launch service reserves only validated set-sized module storage");
    // Cards: Caduceus and an explicit opt-in add a cache-aligned packet behind the list,
    // never past 1 MB when the block starts below it; the ee_core pointer follows the size.
    static u8 area[1];
    const void *low=(const void *)0x000C0000, *high=(const void *)0x00200000;
    entries[0]=RA_WATCH_PACK(0x80000,1);gRATelemetry=1;count=bytes=3;nodes=0;
    const unsigned list=RA_EE_WORK_BYTES(3,0), cards=((list+63)&~63u)+RA_CARD_PACKET_BYTES;
    assert(list%64 && launch.raWorkBytes(low)==list && !launch.overlayFor(area,list));
    gRAMode=1;assert(launch.raWorkBytes(low)==list);
    gRAExperimentalCards=1;assert(launch.raWorkBytes(low)==cards);
    assert(launch.overlayFor(area,cards)==area+((list+63)&~63u) && !launch.overlayFor(area,cards-1));
    gRAMode=0;assert(launch.raWorkBytes(low)==list);gRAMode=1;
    assert(!notes && launch.raWorkBytes((const void *)(0x00100000-cards))==cards);
    assert(launch.raWorkBytes((const void *)(0x00100000-cards+64))==list && notes==1);
    assert(launch.raWorkBytes(high)==cards && launch.raWorkBytes((const void *)(0x02000000-cards+64))==list);
    assert(!launch.raWorkBytes((const void *)0x02000040) && notes==2);
    count=0;assert(!launch.raWorkBytes(low) && !launch.overlayFor(area,0));
    puts("PASS: card packet only for Caduceus opt-in, cache-aligned, below the 1 MB/32 MB limit, else the pulse");
}
''')
