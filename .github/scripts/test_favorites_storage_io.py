"""Execute Favorites reader/edit/writer bodies with IO and allocation faults."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "src/sources/favsupport.cpp").read_text(encoding="utf-8")
# Permit reproducing the same edit regression against the original C bodies.
source = source.replace("int addFavouriteItem(", "int FavSupport::addItem(")
source = source.replace("int removeFavouriteByIdAndText(", "int FavSupport::removeByIdAndText(")


def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


def run(name, body):
    parent = ROOT / "build/test-tmp"
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=parent) as tmp:
        cpp, exe = Path(tmp) / (name + ".cpp"), Path(tmp) / name
        cpp.write_text(body, encoding="utf-8")
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run([*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-no-pie", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=15)


prefix = r'''
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <fcntl.h>
using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
constexpr int FAV_MAX_ITEMS=512, FAV_TEXT_MAX=256, FAV_VERSION=3;
constexpr u32 FAV_MAGIC=0x4641464f;
enum {ETH_MODE=8,HDD_MODE=9,APP_MODE=10,FAV_MODE=12,MMCE_MODE=11,MMCE_MODE1=13,
      FAV_KIND_ISO=0,FAV_KIND_VCD=1,FAV_KIND_ELF=3};
struct fav_raw_t {int mode,id,icon_id,text_id,kind; char text[FAV_TEXT_MAX];};
#define LOG(...) ((void)0)
#define FAV_FILE_NAME "favourites.bin"
'''

# These use the actual editing methods, while a failed/partial read is injected at
# their storage boundary. Old methods overwrite the store before this assertion.
run("favorites_failed_read_edit", prefix + r'''
static int complete, rawCount, writes;
static fav_raw_t *favReadFile(int *count, int *editable=nullptr) {
    *count=rawCount; if(editable) *editable=complete;
    if(!rawCount) return nullptr;
    auto *p=static_cast<fav_raw_t*>(calloc(rawCount,sizeof(fav_raw_t)));
    assert(p); p[0].mode=3; p[0].id=7; strcpy(p[0].text,"previous"); return p;
}
static int favWriteFile(fav_raw_t*,int) {++writes;return 1;}
static int favRecordMatchesResolved(int a,int,const char*,int,int b){return a==b;}
static int favIdsMatchForMode(int,int,int a,int b){return a==b;}
class FavSupport { public:
 int addItem(int,int,int,int,const char*,int);
 int removeByIdAndText(int,int,const char*,int);
};
''' + function("int FavSupport::addItem(") + function("int FavSupport::removeByIdAndText(") + r'''
int main() {
 FavSupport store;
 // Missing and valid-empty are editable; IO/allocation/corruption failures are not.
 complete=0; rawCount=0;
 assert(!store.addItem(3,8,0,0,"new",0) && writes==0);
 rawCount=1;
 assert(!store.addItem(3,8,0,0,"new",0) && writes==0);
 assert(!store.removeByIdAndText(3,7,"previous",0) && writes==0);
 complete=1; rawCount=0;
 assert(store.addItem(3,8,0,0,"new",0) && writes==1);
 rawCount=1;
 assert(store.addItem(3,7,0,0,"previous",0) && writes==1);
 assert(store.removeByIdAndText(3,7,"previous",0) && writes==2);
}
''')

io = r'''
static std::vector<u8> disk, staged;
static bool primaryExists=true, stagedExists;
struct Descriptor {std::vector<u8>* bytes=nullptr;int pos=0;bool writing=false;};
struct Snapshot {std::vector<u8> main, recovery;bool mainExists,recoveryExists;};
static std::vector<Snapshot> checkpoints;
static bool capture;
static void checkpoint() {if(capture)checkpoints.push_back({disk,staged,primaryExists,stagedExists});}

static Descriptor fds[8];
static int readLimit=1000000, writeLimit=1000000, failReadAt=-1;
static int openError, closeError, writeErrorAt=-1, writeZeroAt=-1, opens, opened, closes, writes;
static bool failSeek, failAllocation;
static int writeFaultFile,readFaultFile,closeWriteFile,closeReadFile,openWriteFile,writeErrno=EIO,corruptWriteFile;
static bool removeError;
static int reads,allocations,failAllocationAt=-1;
static int fileNumber(const std::vector<u8>* bytes) {return bytes==&disk?1:2;}

static int fakeOpen(const char *path,int flags,...) {
 ++opens; if(openError) {errno=openError;return -1;}
 bool recovery=strstr(path,".new")!=nullptr;
 if((flags&O_WRONLY) && openWriteFile==(recovery?2:1)) {errno=EACCES;return -1;}
 auto &exists=recovery?stagedExists:primaryExists;
 if(!exists && !(flags&O_CREAT)) {errno=ENOENT;return -1;}
 exists=true;auto &bytes=recovery?staged:disk;
 if(flags&O_TRUNC){bytes.clear();checkpoint();}
 for(int i=0;i<8;i++)if(!fds[i].bytes){fds[i]={&bytes,0,(flags&O_WRONLY)!=0};++opened;return i+17;}
 assert(false);return -1;
}
static int fakeRead(int fd,void *out,int size) {
 auto &f=fds[fd-17];assert(f.bytes);++reads;
 if((!readFaultFile || readFaultFile==fileNumber(f.bytes)) && failReadAt>=0 && f.pos>=failReadAt) {errno=EIO; return -1;}
 int n=std::min({size,readLimit,static_cast<int>(f.bytes->size())-f.pos});
 if((!readFaultFile || readFaultFile==fileNumber(f.bytes)) && failReadAt>=0) n=std::min(n,failReadAt-f.pos);
 if(n) memcpy(out,f.bytes->data()+f.pos,n);
 f.pos+=n; return n;
}
static int fakeWrite(int fd,const void *in,int size) {
 auto &f=fds[fd-17];assert(f.bytes);++writes;
 if((!writeFaultFile || writeFaultFile==fileNumber(f.bytes)) && writeErrorAt>=0 && f.pos>=writeErrorAt) {errno=writeErrno;return -1;}
 if(writeZeroAt>=0 && f.pos>=writeZeroAt) return 0;
 int n=std::min(size,writeLimit);
 if((!writeFaultFile || writeFaultFile==fileNumber(f.bytes)) && writeErrorAt>=0) n=std::min(n,writeErrorAt-f.pos);
 if(writeZeroAt>=0) n=std::min(n,writeZeroAt-f.pos);
 if(static_cast<int>(f.bytes->size())<f.pos+n)f.bytes->resize(f.pos+n);
 memcpy(f.bytes->data()+f.pos,in,n);
 if(corruptWriteFile==fileNumber(f.bytes) && f.pos==0 && n>0)(*f.bytes)[0]^=1;
 f.pos+=n;checkpoint();return n;
}
static int fakeClose(int fd) {
 auto f=fds[fd-17];assert(f.bytes);fds[fd-17]={};++closes;
 if(f.writing)checkpoint();
 return closeError || (f.writing?closeWriteFile:closeReadFile)==fileNumber(f.bytes)?-1:0;
}
static int fakeSeek(int fd,int pos,int origin) {
 auto &f=fds[fd-17];assert(f.bytes);if(failSeek)return -1;
 f.pos=origin==SEEK_END?static_cast<int>(f.bytes->size())+pos:pos;return f.pos;
}
[[maybe_unused]] static int fakeRemove(const char *path) {
 if(removeError)return -1;
 if(strstr(path,".new")){staged.clear();stagedExists=false;checkpoint();return 0;}
 disk.clear();primaryExists=false;return 0;
}
static void *fakeMalloc(size_t n) {return (failAllocation || allocations++==failAllocationAt)?nullptr:malloc(n);}
static void *fakeCalloc(size_t n,size_t s) {return (failAllocation || allocations++==failAllocationAt)?nullptr:calloc(n,s);}
static void favGetFilePath(char *p,int,const char *name=FAV_FILE_NAME) {strcpy(p,"mc0:OPL/");strcat(p,name);}
static void resetFaults() {
 readLimit=writeLimit=1000000;failReadAt=writeErrorAt=writeZeroAt=-1;
 openError=closeError=opens=opened=closes=writes=0;failSeek=failAllocation=false;
 writeFaultFile=readFaultFile=closeWriteFile=closeReadFile=openWriteFile=corruptWriteFile=reads=0;
 writeErrno=EIO;removeError=capture=false;allocations=0;failAllocationAt=-1;
 for(auto &f:fds)f={};
}
static void reset() {resetFaults();staged.clear();stagedExists=false;}
#define open fakeOpen
#define read fakeRead
#define write fakeWrite
#define close fakeClose
#define lseek fakeSeek
#define remove fakeRemove
#define malloc fakeMalloc
#define calloc fakeCalloc
'''
reader_start = "struct FavStream" if "struct FavStream" in source else "static int rdFull(" if "static int rdFull(" in source else "static int rdBytes("
reader = source[source.index(reader_start):source.index("// Little-endian scalar appenders")]
writer = source[source.index("static void bufU16("):source.index("// ---- resolution helpers")]

# The old reader is wrapped with its previous implicit editable assumption so the
# regression fails at runtime rather than simply failing to compile its old API.
load = "return favReadFile(n,editable);" if "int *editable" in function("static fav_raw_t *favReadFile(") else "*editable=1;return favReadFile(n);"
run("favorites_storage_io", prefix + io + reader + writer + r'''
static fav_raw_t *load(int *n,int *editable) {''' + load + r'''}
static void expect(int count,int editable) {
 int n=-1,e=-1; auto *r=load(&n,&e); assert(n==count && e==editable);
 if(count) assert(r);
 free(r); assert(closes==opened);
}
int main() {
 fav_raw_t row{};row.mode=APP_MODE;row.id=3;strcpy(row.text,"test");
 reset();primaryExists=false; assert(favWriteFile(&row,1)); const auto valid=disk;
 reset(); assert(!favWriteFile(nullptr,1) && !favWriteFile(&row,-1) &&
                 !favWriteFile(&row,FAV_MAX_ITEMS+1) && opens==0);
 reset(); expect(1,1);
 // Scalar/text reads and writes accumulate valid positive short transfers.
 reset();readLimit=1;expect(1,1);
 reset();writeLimit=2;assert(favWriteFile(&row,1) && disk==valid && writes>1);
 reset();closeError=1;assert(!favWriteFile(&row,1) && closes==1);
 reset();writeErrorAt=4;assert(!favWriteFile(&row,1));assert(disk==valid);
 reset();writeZeroAt=4;assert(!favWriteFile(&row,1));assert(disk==valid);
 reset();failAllocation=true;assert(!favWriteFile(&row,1) && opens==0);
 reset();openError=EACCES;assert(!favWriteFile(&row,1) && closes==0);
 disk=valid;reset();failReadAt=4;expect(0,0);
 disk=valid;reset();failAllocation=true;expect(0,0);
 disk=valid;reset();closeError=1;expect(1,0);
 disk=valid;reset();disk[4]=FAV_VERSION+1;expect(0,0);
 disk=valid;reset();disk[4]=0;expect(0,0);
 disk=valid;disk[4]=2;reset();expect(1,1);
 disk=valid;disk[4]=1;disk.erase(disk.begin()+22);reset();expect(1,1);
 // A salvaged prefix may still display, but must never become a replacement list.
 disk=valid;disk[6]=2;reset();expect(1,0);
 disk=valid;disk.back()='x';reset();expect(1,0);
 disk=valid;disk.push_back(1);reset();expect(1,0);
 // A genuinely empty OFAV store is editable, unlike a zero-byte/torn header.
 disk=valid;reset();assert(favWriteFile(nullptr,0));reset();expect(0,1);
 disk.clear();reset();expect(0,0);
 disk={1,2,3};reset();expect(0,0);
 // ENOENT alone permits a new store; unknown, permission and device errors refuse.
 for(int e:{EIO,EACCES,EPERM,EMFILE}) {
   reset();openError=e;int n,eout;auto*r=load(&n,&eout);assert(!r && !n && !eout && !closes);
 }
 reset();openError=ENOENT;int n,eout;auto*r=load(&n,&eout);assert(!r && !n && eout && !closes);
 // Legacy import is editable only after complete records and clean EOF/close.
 std::vector<u8> foreign(32,0);foreign[0]=5;foreign[12]=7;
 foreign.insert(foreign.end(),{5,0,0,0,'t','e','s','t',0,5,0});
 disk=foreign;reset();readLimit=2;expect(1,1);
 disk=foreign;disk.push_back(9);reset();expect(1,0);
 disk=foreign;reset();failSeek=true;expect(0,0);
 disk=foreign;reset();closeError=1;expect(1,0);
 disk=foreign;disk[disk.size()-3]='x';reset();expect(1,0);
 disk.clear();for(int i=0;i<FAV_MAX_ITEMS;i++)disk.insert(disk.end(),foreign.begin(),foreign.end());
 reset();expect(FAV_MAX_ITEMS,1);
 disk.insert(disk.end(),foreign.begin(),foreign.end());reset();expect(FAV_MAX_ITEMS,0);

 // Ordinary IO is bounded by image size, not by record count.
 disk=valid;reset();expect(1,1);assert(reads==2);
 // Save a distinct complete second image to use as a recovery fixture.
 disk=valid;reset();strcpy(row.text,"requested");assert(favWriteFile(&row,1));const auto requested=disk;
 // Rewriting the primary can fail after truncation: .new remains complete and editable.
 disk=valid;reset();writeFaultFile=1;writeErrorAt=4;
 assert(!favWriteFile(&row,1) && disk!=valid && staged==requested && stagedExists);
 resetFaults();expect(1,1);assert(disk!=requested); // Reads never mutate either file.
 // Restore the sole recovery copy before staging another edit; stage failure must
 // leave the restored list intact, including after a fresh process/authority decision.
 strcpy(row.text,"next");resetFaults();writeFaultFile=2;writeErrorAt=0;
 assert(!favWriteFile(&row,1) && disk==requested);
 resetFaults();expect(1,1);
 // Failed restoration, or failed verification of the restored primary, never touches .new.
 disk={1,2,3};staged=requested;stagedExists=true;resetFaults();writeFaultFile=1;writeErrorAt=0;
 assert(!favWriteFile(&row,1) && staged==requested);
 disk={1,2,3};resetFaults();closeWriteFile=1;
 assert(!favWriteFile(&row,1) && staged==requested);
 disk={1,2,3};resetFaults();corruptWriteFile=1;
 assert(!favWriteFile(&row,1) && staged==requested);
 // Stage write/close/readback failure and ENOSPC preserve the complete primary.
 for(int fault=0;fault<4;fault++) {
   disk=valid;reset();
   if(fault==0){writeFaultFile=2;writeErrorAt=0;writeErrno=ENOSPC;}
   if(fault==1)closeWriteFile=2;
   if(fault==2)corruptWriteFile=2;
   if(fault==3){readFaultFile=2;failReadAt=0;}
   assert(!favWriteFile(&row,1) && disk==valid);
 }
 // A write-open failure leaves the old complete primary authoritative, even with
 // a newer complete stage; a PC-edited complete main wins too, including empty.
 disk=valid;reset();openWriteFile=1;
 assert(!favWriteFile(&row,1) && disk==valid && stagedExists);
 resetFaults();expect(1,1);
 staged=requested;stagedExists=true;disk=valid;resetFaults();expect(1,1);
 disk={0x4f,0x46,0x41,0x46,3,0,0,0};resetFaults();expect(0,1);
 // Cleanup failure is not a failed commit: readback has confirmed the primary.
 disk=valid;reset();removeError=true;assert(favWriteFile(&row,1) && stagedExists);
 disk=valid;resetFaults();writeFaultFile=2;writeErrorAt=0;
 assert(!favWriteFile(&row,1) && disk==valid);
 // Recovery may be shown read-only when authority cannot be established. Do not
 // overwrite unreadable, unknown-version or oversized primary data with a stale stage.
 for(int fault=0;fault<3;fault++) {
   disk=valid;staged=requested;stagedExists=true;resetFaults();
   if(fault==0){readFaultFile=1;failReadAt=0;}
   if(fault==1)disk[4]=FAV_VERSION+1;
   if(fault==2)disk.resize(160000,0);
   expect(1,0);const auto primary=disk;
   assert(!favWriteFile(&row,1) && disk==primary && staged==requested);
 }
 // An orphan recovery file must be native OFAV, never a foreign import or partial list.
 primaryExists=false;staged=foreign;stagedExists=true;resetFaults();expect(0,0);
 assert(!favWriteFile(&row,1) && staged==foreign);
 staged=requested;resetFaults();expect(1,1);
 // Capture filesystem states at every truncate, short write, close and cleanup.
 // Reboot-style reads of each snapshot retain one complete old/new list, both
 // for an ordinary save and for a save starting with the sole copy in .new.
 for(int recoveryOnly=0;recoveryOnly<2;recoveryOnly++) {
   reset();primaryExists=true;disk=recoveryOnly?std::vector<u8>{1,2,3}:valid;
   if(recoveryOnly){staged=requested;stagedExists=true;}
   checkpoints.clear();capture=true;writeLimit=3;
   assert(favWriteFile(&row,1));capture=false;auto snapshots=checkpoints;
   for(const auto &snapshot:snapshots) {
     resetFaults();disk=snapshot.main;staged=snapshot.recovery;
     primaryExists=snapshot.mainExists;stagedExists=snapshot.recoveryExists;
     int n,e;auto *r=load(&n,&e);
     assert(n==1 && e && r);
     assert(!strcmp(r[0].text,"test") || !strcmp(r[0].text,"requested") || !strcmp(r[0].text,"next"));
     free(r);
   }
 }
 // Fail every allocation reached by normal saves and recovery-only saves.
 for(int recoveryOnly=0;recoveryOnly<2;recoveryOnly++)for(int allocation=0;allocation<16;allocation++) {
   reset();primaryExists=true;disk=recoveryOnly?std::vector<u8>{1,2,3}:valid;
   if(recoveryOnly){staged=requested;stagedExists=true;}
   failAllocationAt=allocation;(void)favWriteFile(&row,1);
   resetFaults();int n,e;auto *r=load(&n,&e);
   assert(n==1 && e && r);
   assert(!strcmp(r[0].text,"test") || !strcmp(r[0].text,"requested") || !strcmp(r[0].text,"next"));
   free(r);
 }
 disk=valid;primaryExists=true;reset();

 // Reject editing oversized lists rather than silently saving just the first 512.
 disk=valid;reset();std::vector<fav_raw_t> many(FAV_MAX_ITEMS,row);assert(favWriteFile(many.data(),many.size()));
 reset();disk[6]=1;disk[7]=2;expect(FAV_MAX_ITEMS,0);
}
''')
# The recents list is written on every launch, so it is one write in place: no .new copy, no read-back.
run("recents_single_write", prefix + io + reader + writer + r"""
static const char kRecentFile[] = "recents.bin";
""" + function("static int recentWriteFile(") + function("static bool recentReplaceable(") + r"""
int main() {
 fav_raw_t row{};row.mode=3;row.id=7;strcpy(row.text,"game");
 // The favourites way, for the count it costs: five opens (three of them reads) and a deletion.
 reset();primaryExists=false;assert(favWriteFile(&row,1));const auto valid=disk;
 reset();assert(favWriteFile(&row,1) && opens==5 && reads>=3 && !stagedExists);
 // The recents way: one open for writing, nothing read, nothing created beside the file.
 disk.clear();reset();assert(recentWriteFile(&row,1));
 assert(opens==1 && closes==1 && reads==0 && !stagedExists && disk==valid);
 disk=valid;reset();writeLimit=2;assert(recentWriteFile(&row,1) && opens==1 && disk==valid);
 // A failed write or close is reported, and nothing else is tried.
 reset();writeErrorAt=4;assert(!recentWriteFile(&row,1) && opens==1 && !stagedExists);
 reset();closeError=1;assert(!recentWriteFile(&row,1));
 reset();openError=EACCES;assert(!recentWriteFile(&row,1) && closes==0);
 reset();failAllocation=true;assert(!recentWriteFile(&row,1) && opens==0);
 // A file cut short reads but does not parse: it may be replaced. One the device cannot read may not.
 disk={0x4f,0x46,0x41};primaryExists=true;reset();
 int n,e;free(favReadFile(&n,&e,kRecentFile));assert(!e);
 resetFaults();assert(recentReplaceable() && closes==opened);
 disk.clear();resetFaults();assert(recentReplaceable());
 primaryExists=false;resetFaults();assert(recentReplaceable());
 primaryExists=true;disk=valid;resetFaults();failReadAt=0;assert(!recentReplaceable());
 resetFaults();openError=EIO;assert(!recentReplaceable());
 resetFaults();closeError=1;assert(!recentReplaceable());
}
""")
run("favorites_path_bounds", prefix + r'''
#define CONFIG_OPL 1
#define CONFIG_OPL_FILENAME "settings_riptopl.cfg"
static const char *filename;
struct config_set_t {const char *filename() {return ::filename;}};
static config_set_t config;
static config_set_t *configGetByType(int) {return &config;}
''' + function("static void favGetFilePath(") + r'''
int main() {
 char path[256];filename="mc0:OPL/settings_riptopl.cfg";
 favGetFilePath(path,sizeof(path));assert(!strcmp(path,"mc0:OPL/favourites.bin"));
 char longName[300];memset(longName,'x',sizeof(longName));
 strcpy(longName+240,CONFIG_OPL_FILENAME);filename=longName;
 favGetFilePath(path,sizeof(path));assert(strlen(path)==254);
 strcpy(longName+242,CONFIG_OPL_FILENAME);
 favGetFilePath(path,sizeof(path));assert(!path[0]);
}
''')

print("Favorites failed-read editing and real reader/writer IO fault regressions passed")
