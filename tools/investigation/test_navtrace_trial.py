"""Exercise actual pad integration, automatic export and the trial guard on the host."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from probe_navigation_input_gaps import ROOT, HARNESS, function


HOST = r'''
#include <time.h>
#include <unistd.h>
#include <stddef.h>
extern unsigned long long navTestClock;
static inline clock_t navtest_clock() {return (clock_t)(navTestClock * CLOCKS_PER_SEC / 1000000);}
#define clock navtest_clock
#define OPL_VERSION "host-navtrace-test"
extern char gBootDir[256];
extern int gEnableArt,gEnableBGArt,gEnableSaveIcons,gEnableRumble,gEnableSFX,gEnableBGM,gEnableMX4SIO;
const char *configGetHomePath();
int DIntr();
void EIntr();
int diagCopy(char *,int,int *);
int navTestWrite(int,const void *,unsigned int);
#define write navTestWrite
'''

MAIN = r'''
#undef write
#include <cerrno>
#include "include/navgapguard.h"
#include <fstream>
#include <iterator>
#include <string>
unsigned long long navTestClock;
char gBootDir[256];
int gEnableArt,gEnableBGArt,gEnableSaveIcons,gEnableRumble,gEnableSFX,gEnableBGM,gEnableMX4SIO;
static int writeMode, interruptOnce;
int navTestWrite(int fd,const void *data,unsigned int size) {
    if (interruptOnce) {interruptOnce=0; errno=EINTR; return -1;}
    if (writeMode==2) {errno=ENOSPC; return -1;}
    if (writeMode==1 && size>7) size=7;
    return (int)::write(fd,data,size);
}
const char *configGetHomePath() {return gBootDir;}
static int intrEnabled=1;
int DIntr() {int old=intrEnabled; intrEnabled=0; return old;}
void EIntr() {intrEnabled=1;}
int diagCopy(char *p,int,int *w) {*w=0; memcpy(p,"host diagnostic\r\n",17); return 17;}
static std::string slurp() {
    std::ifstream f(std::string(gBootDir)+"/NAVTRACE.TXT");
    return std::string(std::istreambuf_iterator<char>(f),{});
}
static int occurrences(const std::string &s,const std::string &term) {
    int count=0;
    for (size_t i=0; (i=s.find(term,i))!=std::string::npos; i+=term.size()) ++count;
    return count;
}
static void idle(PadManager &p) {
    for (int i=0; i<125; ++i) {frame(p,true,0); navTraceAutoSave();}
}
int main(int argc,char **argv) {
    assert(argc==2);
    snprintf(gBootDir,sizeof(gBootDir),"%s",argv[1]);
    // Independent state controls: bounded unknown, immediate release/disconnect,
    // wrap-safe age, other-controller evidence and no retention of Cross.
    NavigationGapGuard g;
    g.begin(true,0xfffffff0U); assert(g.sample(0,true,true,PAD_DOWN|PAD_CROSS)==(PAD_DOWN|PAD_CROSS));
    g.begin(true,20); assert(g.sample(0,true,false,0)==PAD_DOWN && g.unknown()==PAD_DOWN);
    g.begin(true,30); assert(g.sample(0,true,false,0)==PAD_DOWN);
    assert(g.sample(1,true,true,PAD_DOWN)==PAD_DOWN && g.unknown()==0);
    g.begin(true,40); assert(g.sample(0,true,true,0)==0 && g.unknown()==0);
    g.begin(true,50); g.sample(0,true,true,PAD_DOWN);
    g.begin(true,100051); assert(g.sample(0,true,false,0)==0);
    g.begin(true,100052); g.sample(0,true,true,PAD_DOWN);
    g.begin(true,100053); assert(g.sample(0,false,false,0)==0);
    g.begin(true,100054); assert(g.sample(0,true,false,0)==0);
    printf("guard controls: release, disconnect, timeout, tick wrap, independent ports, navigation-only OK\n");

    auto p=makePad(0x41);
    frame(p,true,0); frame(p,true,0);
    assert(frame(p,true,PAD_DOWN).event);
    frame(p,true,PAD_DOWN);
    assert(!frame(p,false,PAD_DOWN).event);
    assert(frame(p,true,PAD_DOWN).event); // A: original duplicate is visible.
    assert(slurp().empty()); // no file work during active recreation.
    idle(p);
    auto first=slurp();
    assert(occurrences(first,"BEGIN NAVTRACE")==1);
    assert(first.find("mode=A-normal")!=std::string::npos);
    assert(first.find("forced=0")!=std::string::npos);
    assert(occurrences(first,"END NAVTRACE complete=1")==1);
    printf("capture A: original gap behavior preserved; automatic file export after rest OK\n");

    p=makePad(0x41); frame(p,true,0); frame(p,true,0);
    assert(frame(p,true,PAD_DOWN).event);
    frame(p,true,PAD_DOWN);
    int before=p.delaycnt_[1];
    auto missing=frame(p,false,PAD_DOWN);
    assert(missing.held && !missing.event && !missing.edge && p.delaycnt_[1]==before);
    auto recovery=frame(p,true,PAD_DOWN);
    assert(!recovery.edge && !recovery.event);
    assert(!frame(p,true,0).held);
    assert(frame(p,true,PAD_DOWN).edge); // a verified release still rearms a genuine press.
    assert(slurp()==first);
    writeMode=1; interruptOnce=1; // short writes and one interrupted write must complete.
    idle(p);
    writeMode=0;
    auto second=slurp();
    assert(second.find(first)==0 && occurrences(second,"BEGIN NAVTRACE")==2);
    assert(second.find("mode=B-gap-guard")!=std::string::npos);
    assert(occurrences(second,"END NAVTRACE complete=1")==2);
    printf("capture B: no synthetic press or timer reset; live pad functions and append export OK\n");

    // Automatic return to baseline after the next completed idle capture.
    p=makePad(0x41); frame(p,true,0); frame(p,true,0);
    frame(p,true,PAD_DOWN); frame(p,false,PAD_DOWN);
    assert(frame(p,true,PAD_DOWN).event);
    idle(p);
    assert(occurrences(slurp(),"mode=A-normal")==2);
    assert(occurrences(slurp(),"END NAVTRACE complete=1")==3);
    assert(intrEnabled);
    printf("three recreations retained in one TXT; A/B/A modes alternate without controls OK\n");

    // A full output device must retain the RAM capture and current mode, then
    // retry automatically; it must not report a successful save or clear it.
    p=makePad(0x41); frame(p,true,0); frame(p,true,0);
    frame(p,true,PAD_DOWN);
    writeMode=2;
    idle(p);
    assert(occurrences(slurp(),"END NAVTRACE complete=1")==3);
    writeMode=0;
    for (int i=0; i<180; ++i) {frame(p,true,0); navTraceAutoSave();}
    assert(occurrences(slurp(),"END NAVTRACE complete=1")==4);
    assert(occurrences(slurp(),"mode=B-gap-guard")==2);
    printf("short writes, EINTR, ENOSPC retention and automatic retry OK\n");
}
'''


def main():
    pad = (ROOT / "src/core/pad.cpp").read_text()
    header = (ROOT / "include/padmanager.h").read_text()
    header = re.sub(r"^#include.*$", "", header, flags=re.M).replace("private:", "public:")
    header = re.sub(r"^\s*PadManager\(const PadManager.*$", "", header, flags=re.M)
    header = re.sub(r"^\s*PadManager &operator=.*$", "", header, flags=re.M)
    constants = "\n".join(re.findall(
        r"^#define (?:CLOCKS_PER_MILISEC|DEFAULT_PAD_DELAY)\s+\d+|"
        r"^constexpr \w+ (?:PAD_SELF_HEAL_IDLE_MS|PAD_IDLE_MAX_CLEAN_GAP_MS|PAD_REPEAT_MAX_CREDIT_MS) = \d+;",
        pad, re.M))
    functions = "\n".join(function(pad, s) for s in (
        "static int isPadReadyState(", "void PadManager::updateState(",
        "u32 PadManager::readLeftJoy(", "int PadManager::readOne(",
        "int PadManager::keyDelay(", "u32 PadManager::pollElapsedMs(",
        "void PadManager::advanceRepeatTimers(", "int PadManager::read(",
        "int PadManager::key(", "int PadManager::keyOn(", "int PadManager::keyPressed("))
    table = re.search(r"^static const int keyToPad\[17\] = \{.*?\};", pad, re.M | re.S).group()
    program = HARNESS.split("int main() {")[0]
    for key, value in {"HEADER": header, "CONSTANTS": constants, "TABLE": table, "FUNCTIONS": functions}.items():
        program = program.replace(f"@{key}@", value)
    program = program.replace("    p.read();", "    navTestClock += (unsigned int)delta * 1000U;\n    p.read();")
    program = '#include "include/navtrace.h"\n' + program + MAIN
    with tempfile.TemporaryDirectory(prefix="navtrace-trial-") as temp:
        folder = Path(temp)
        (folder / "host.h").write_text(HOST)
        (folder / "test.cpp").write_text(program)
        exe = folder / "test.exe"
        subprocess.run([shutil.which("g++"), "-std=c++17", "-O2", "-Wall", "-Wextra",
                        "-Wno-unused-function", "-Wno-unused-variable", "-include", "initializer_list",
                        "-include", str(folder / "host.h"), "-I", str(ROOT),
                        "-DOPL_NAV_INVESTIGATION", "-DNAVTRACE_HOST", str(folder / "test.cpp"),
                        str(ROOT / "src/core/navtrace.cpp"), "-o", str(exe)], check=True)
        subprocess.run([str(exe), str(folder)], cwd=folder, check=True, timeout=20)


if __name__ == "__main__":
    main()
