"""Execute Ranet's actual watch-list transfer with padded binary UDP replies."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]).read_text(encoding="utf-8") if len(sys.argv) > 1 else (root / "src/sources/ranet.cpp").read_text(encoding="utf-8")


def function(signature):
    first = source.index(signature)
    return source[first:source.index("\n}", first) + 2]


helper = "".join(function(name) for name in ("static int raParseChunkHeader(", "static int raExpectedChunkLength(") if name in source)
program = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <climits>
#include "include/ranet.h"
struct sockaddr_in {int unused;};
constexpr int RA_CHUNK=896, RA_WAIT_ROUNDS=20, RA_WAIT_PAUSE_MS=500, RA_LINK_BUSY=-8, RA_LINK_MSTACK=0x10000;
enum {RA_MODE_XERABORA, RA_MODE_CADUCEUS};
static int gRAMode = RA_MODE_XERABORA;
enum {_STR_RA_SUPPORTED,_STR_RA_ACHIEVEMENTS_FULL,_STR_RA_ACHIEVEMENTS,_STR_RA_WL_NOT_SAVED};
static void DelayThread(int) {}
#define LOG(...) ((void)0)
static const char *_l(int id) {return id==_STR_RA_ACHIEVEMENTS_FULL ? "%d %d %d" : "%d achievements";}
static void raHashStep(const char*) {}
static std::vector<std::string> replies;
static std::vector<unsigned char> published;
static int cursor, parsed, files, writes, closes, disconnected, parserResult=1, guards, shortWrite;
static bool reserved;
int Ranet::nicReserved() {return reserved;}
int Ranet::openPcLink(char *address,int,unsigned char ip[4]) {address[0]=0;memset(ip,0,4);return reserved ? RA_LINK_BUSY : 7;}
int Ranet::pcTarget(int,sockaddr_in*,const char*,char*,int) {return 0;}
void Ranet::forgetPeer() {}
void Ranet::rememberPeer(unsigned int) {}
int Ranet::ask(int, sockaddr_in*, const char *req, char *out, int max, int attempts) {
    assert(attempts == (!strncmp(req, "RAG1 ", 5) && gRAMode == RA_MODE_XERABORA ? 60 : 12));
    if(cursor==(int)replies.size())return -1;
    const auto &reply=replies[cursor++];assert(reply.size()<(unsigned)max);
    memcpy(out,reply.data(),reply.size());out[reply.size()]=0;return reply.size();
}
void Ranet::closePcLink(int) {disconnected++;}
static int fake_unlink(const char*) {return 0;}
static int SetWatchList(const void *data,int len,const char*) {
    parsed++;published.assign((const unsigned char*)data,(const unsigned char*)data+len);return parserResult;
}
static int mkdir(const char*,int) {return 0;}
static int raVcdWatchGuardStore(const char*,const char*,const char*) {guards++;return 0;}
static FILE *fake_open(const char*,const char*) {files++;return reinterpret_cast<FILE*>(1);}
static size_t fake_write(const void*,size_t size,size_t count,FILE*) {writes++;return shortWrite ? 0 : size*count;}
static int fake_close(FILE*) {closes++;return 0;}
#define fopen fake_open
#define fwrite fake_write
#define fclose fake_close
#define unlink fake_unlink
@HELPER@
@FUNCTION@
#undef fopen
#undef fwrite
#undef fclose
#undef unlink
static Ranet client;
static std::string padded(std::string packet) {
    packet.resize(std::max<size_t>(128,(packet.size()+63)/64*64),' ');return packet;
}
static void announce(int bytes,int chunks) {
    replies.push_back(padded("RAA1 OK "+std::to_string(bytes)+" "+std::to_string(chunks)+" 1 Example"));
}
static void chunk(int index,const std::string &payload,int advertised=-1) {
    if(advertised<0)advertised=payload.size();
    replies.push_back(padded("RAC1 "+std::to_string(index)+" "+std::to_string(advertised)+" "+payload));
}
static void reset() {
    replies.clear();published.clear();cursor=parsed=files=writes=closes=disconnected=guards=shortWrite=0;parserResult=1;reserved=false;
}
static int transfer() {return client.askPc("0123456789abcdef0123456789abcdef","SLUS_123.45","smb0:",nullptr,0,nullptr,0);}
static void refused() {assert(transfer()<0);assert(!parsed && !files && !writes);assert(disconnected==1);}
int main() {
    // Fixed stride: these sum to 1,000 but leave a gap after byte 104.
    reset();announce(1000,2);chunk(0,std::string(104,'a'));chunk(1,std::string(896,'b'));refused();
    for(int count : {0,1,3,INT_MAX}) {reset();announce(1000,count);assert(transfer()==-3);assert(cursor==1 && !parsed && !files);}
    for(int total : {0,-1,RA_MAX_BYTES+1,INT_MAX}) {reset();announce(total,1);refused();}
    for(int len : {0,100,895,897}) {reset();announce(1000,2);chunk(0,std::string(len,'a'));refused();}
    reset();announce(1000,2);chunk(0,std::string(896,'a'));chunk(1,std::string(103,'b'));refused();
    reset();announce(896,1);chunk(0,std::string(1,'a'),896);refused(); // packet too short even after padding
    reset();announce(1,1);replies.push_back(padded("RAC1 0 1x"));refused(); // missing protocol separator
    reset();announce(1,1);for(int i=0;i<3;i++)chunk(1,"a");refused(); // stale indices exhaust bounded retry
    reset();announce(1,1);chunk(7,"x");chunk(0,"a");assert(transfer()==0 && parsed==1 && published[0]=='a');
    // Every whitespace byte is binary data after the one protocol separator.
    for(char first : {' ','\t','\n','\r','\v','\f','\0'}) {
        reset();announce(896,1);std::string body(896,'x');body[0]=first;chunk(0,body);
        assert(transfer()==0 && parsed==1 && files==1 && writes==1 && closes==1);
        assert(published==std::vector<unsigned char>(body.begin(),body.end()));
    }
    for(int total : {1,896,897,1793,RA_MAX_BYTES}) {
        reset();int count=(total+895)/896;announce(total,count);std::string expected;
        for(int i=0;i<count;i++){std::string body(std::min(896,total-i*896),'A'+i);expected+=body;chunk(i,body);}
        assert(transfer()==0 && parsed==1 && files==1);
        assert(published==std::vector<unsigned char>(expected.begin(),expected.end()));
    }
    reset();gRAMode=RA_MODE_CADUCEUS;announce(1,1);chunk(0,"a");assert(transfer()==0);
    gRAMode=RA_MODE_XERABORA;
    reset();announce(1,1);chunk(0,"x");parserResult=-3;
    assert(transfer()==-6 && parsed==1 && !files && !writes && disconnected==1);
    reset();reserved=true;assert(transfer()==-8 && !cursor && !parsed && !files);
    // A VCD list (15-character P key) is bound to its content hash, only once the list is on disk.
    reset();announce(1,1);chunk(0,"a");assert(transfer()==0 && !guards);
    const char *hash="0123456789abcdef0123456789abcdef";
    reset();announce(1,1);chunk(0,"a");assert(client.askPc(hash,"P0123456789abcd","smb0:",nullptr,0,nullptr,0)==0 && guards==1);
    reset();announce(1,1);chunk(0,"a");shortWrite=1;assert(client.askPc(hash,"P0123456789abcd","smb0:",nullptr,0,nullptr,0)==0 && !guards);
    puts("PASS: actual Ranet transfer refuses gaps/counts/truncation/invalid lists and preserves binary whitespace");
}
'''.replace("@HELPER@", helper).replace("@FUNCTION@", function("int Ranet::askPc(") + function("int Ranet::raqAsk("))

with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / "transfer.cpp", Path(directory) / "transfer"
    cpp.write_text(program, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=undefined", "-I", str(root), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
