"""Execute commit-pinned pad functions with controlled native sample gaps.

This is an investigation probe, not a hardware test or a proposed input fix.
It records what the current implementation does, including unwanted events.
Run from any directory; a host C++ compiler and local Git history are required.
"""

from pathlib import Path
import argparse
import json
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REFS = ("6edea174e5522b241139eeb4c683c9f2c76f776f",
                "bc439979fde0861aa56e8a63e5408b0a63a52785",
                "b4c23bdac8f983a4b4e3456d4036b8645cf95842")


def source(ref, path):
    if ref == "WORKTREE":
        return (ROOT / path).read_text(encoding="utf-8")
    return subprocess.check_output(
        ["git", "-c", "core.fsmonitor=false", "show", f"{ref}:{path}"],
        cwd=ROOT, text=True, encoding="utf-8")


def function(text, signature):
    match = re.search(r"^" + re.escape(signature) + r"[^;{]*\)\s*\{", text, re.M)
    if match is None:
        raise ValueError(f"Missing function: {signature}")
    end = text.index("\n}", match.start()) + 2
    return text[match.start():end] + "\n"


HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using u8 = uint8_t;
using u32 = uint32_t;
enum {
    PAD_SELECT=1, PAD_L3=2, PAD_R3=4, PAD_START=8,
    PAD_UP=16, PAD_RIGHT=32, PAD_DOWN=64, PAD_LEFT=128,
    PAD_L2=256, PAD_R2=512, PAD_L1=1024, PAD_R1=2048,
    PAD_TRIANGLE=4096, PAD_CIRCLE=8192, PAD_CROSS=16384, PAD_SQUARE=32768,
    PAD_STATE_DISCONN=0, PAD_STATE_FINDCTP1=2, PAD_STATE_STABLE=6,
    PAD_MODECURID=1, PAD_TYPE_DUALSHOCK=7
};
struct padButtonStatus {
    u8 ok=0, mode=0x41;
    uint16_t btns=0xffff;
    u8 rjoy_h=128, rjoy_v=128, ljoy_h=127, ljoy_v=127;
};
static int gXSensitivity, gYSensitivity, KeyPressedOnce, DisableCron;
static int sampleOk=1, sampleState=PAD_STATE_STABLE, packetMode=0x41;
static u32 physicalButtons, ticks;
static int initCalls, actuatorCalls;
static u32 cpu_ticks() { return ticks; }
static int padGetState(int, int) { return sampleState; }
static int padRead(int, int, padButtonStatus *buttons) {
    if (!sampleOk) return 0;
    *buttons = padButtonStatus{};
    buttons->mode = packetMode;
    buttons->btns = (uint16_t)(0xffff ^ physicalButtons);
    return 1;
}
static int padInfoMode(int, int, int, int) { return packetMode >> 4; }
#define LOG(...) ((void)0)
#define diagLog(...) ((void)0)
@CONSTANTS@
@HEADER@
@TABLE@
@FUNCTIONS@
int PadManager::initialize(pad_data_t *) { ++initCalls; return 1; }
void PadManager::actSet(pad_data_t *, int, int) { ++actuatorCalls; }
void PadManager::rumbleFlush() { rumbleActive_=0; rumbleOffRepeats_=0; }

static PadManager makePad(int mode) {
    PadManager p;
    p.padCount_=1;
    p.padData_[0].state=PAD_STATE_STABLE;
    for (int i=0; i<16; ++i) p.paddelay_[i]=200;
    sampleOk=1; sampleState=PAD_STATE_STABLE; packetMode=mode;
    physicalButtons=0; initCalls=actuatorCalls=0;
    return p;
}
struct Frame { int edge, event, held, timer; };
static Frame frame(PadManager &p, bool ok, u32 buttons, int delta=33) {
    sampleOk=ok; physicalButtons=buttons; ticks+=(u32)delta * CLOCKS_PER_MILISEC;
    p.read();
    const int edge=!!p.keyOn(2), held=!!p.keyPressed(2);
    const int event=!!p.key(2); // KEY_DOWN, exactly once, as in menuHandleInputMain.
    return {edge,event,held,p.delaycnt_[1]};
}
int main() {
    for (int mode : {0x41,0x73,0x79}) {
        auto p=makePad(mode);
        frame(p,true,0); frame(p,true,0);
        auto press=frame(p,true,PAD_DOWN);
        for (int i=0; i<3; ++i) frame(p,true,PAD_DOWN);
        auto gap=frame(p,false,PAD_DOWN);
        auto recovery=frame(p,true,PAD_DOWN);
        assert(press.edge && press.event && !gap.held && !gap.event);
        assert(recovery.edge && recovery.event && recovery.timer==600);
        assert(!initCalls && !actuatorCalls);
        printf("{\"case\":\"one_missed_sample\",\"mode\":%d,\"gap_held\":%d,\"recovery_edge\":%d,\"recovery_event\":%d,\"reset_timer\":%d}\n",
               mode,gap.held,recovery.edge,recovery.event,recovery.timer);

        p=makePad(mode); frame(p,true,0); frame(p,true,0);
        int events=0;
        for (int i=0; i<6; ++i) events+=frame(p,false,PAD_DOWN).event;
        events+=frame(p,true,0).event;
        assert(events==0);
        printf("{\"case\":\"tap_entirely_in_gap\",\"mode\":%d,\"events\":%d}\n",mode,events);

        p=makePad(mode); frame(p,true,0); frame(p,true,0);
        events=0;
        for (int i=0; i<8; ++i) events+=frame(p,true,PAD_DOWN).event;
        assert(events==1);
        printf("{\"case\":\"clean_short_hold\",\"mode\":%d,\"events\":%d}\n",mode,events);

        p=makePad(mode); frame(p,true,0); frame(p,true,0);
        frame(p,true,PAD_DOWN);
        auto stall=frame(p,true,PAD_DOWN,800);
        printf("{\"case\":\"long_frame_after_press\",\"mode\":%d,\"edge\":%d,\"repeat_event\":%d}\n",mode,stall.edge,stall.event);

        // A running hold already past the initial delay: the recovery event then
        // resets its 200-unit repeat period to the 600-unit initial delay.
        p=makePad(mode); frame(p,true,0); frame(p,true,0);
        frame(p,true,PAD_DOWN);
        while (!frame(p,true,PAD_DOWN).event) {}
        frame(p,false,PAD_DOWN);
        recovery=frame(p,true,PAD_DOWN);
        int next=0;
        do { ++next; } while (!frame(p,true,PAD_DOWN).event && next<100);
        assert(recovery.edge && recovery.event && next==19);
        printf("{\"case\":\"gap_during_repeating_hold\",\"mode\":%d,\"recovery_edge\":%d,\"next_repeat_frames\":%d}\n",mode,recovery.edge,next);

        // A mode re-query / busy-state interval is distinct from a failed native read.
        // updateState retains the last stable tracked state for PAD_STATE_EXECCMD.
        p=makePad(mode); frame(p,true,0); frame(p,true,0);
        frame(p,true,PAD_DOWN);
        sampleState=5; // PAD_STATE_EXECCMD
        gap=frame(p,false,PAD_DOWN);
        sampleState=PAD_STATE_STABLE;
        recovery=frame(p,true,PAD_DOWN);
        assert(!gap.held && recovery.edge && recovery.event);
        printf("{\"case\":\"busy_state_read_gap\",\"mode\":%d,\"recovery_event\":%d}\n",mode,recovery.event);

        for (int delta : {33,100,200,800}) {
            p=makePad(mode); frame(p,true,0); frame(p,true,0);
            frame(p,true,PAD_DOWN);
            int frames=0;
            do { ++frames; } while (!frame(p,true,PAD_DOWN,delta).event && frames<100);
            printf("{\"case\":\"sustained_slow_frames\",\"mode\":%d,\"delta_units\":%d,\"first_repeat_frames\":%d}\n",mode,delta,frames);
        }
    }
}
'''


def run(ref, compiler, folder):
    pad = source(ref, "src/core/pad.cpp")
    header = source(ref, "include/padmanager.h")
    header = re.sub(r"^#include.*$", "", header, flags=re.M)
    header = header.replace("private:", "public:")
    # Probe instances are reset independently; copyability has no role in input handling.
    header = re.sub(r"^\s*PadManager\(const PadManager.*$", "", header, flags=re.M)
    header = re.sub(r"^\s*PadManager &operator=.*$", "", header, flags=re.M)
    constants = "\n".join(re.findall(
        r"^#define (?:CLOCKS_PER_MILISEC|DEFAULT_PAD_DELAY)\s+\d+|"
        r"^constexpr \w+ (?:PAD_SELF_HEAL_IDLE_MS|PAD_IDLE_MAX_CLEAN_GAP_MS|PAD_REPEAT_MAX_CREDIT_MS) = \d+;",
        pad, re.M))
    functions = "\n".join(function(pad, signature) for signature in (
        "static int isPadReadyState(", "void PadManager::updateState(",
        "u32 PadManager::readLeftJoy(", "int PadManager::readOne(",
        "int PadManager::keyDelay(", "u32 PadManager::pollElapsedMs(",
        "void PadManager::advanceRepeatTimers(", "int PadManager::read(",
        "int PadManager::key(", "int PadManager::keyOn(", "int PadManager::keyPressed("))
    table = re.search(r"^static const int keyToPad\[17\] = \{.*?\};", pad, re.M | re.S).group()
    program = HARNESS
    for key, value in {"HEADER": header, "CONSTANTS": constants,
                       "TABLE": table, "FUNCTIONS": functions}.items():
        program = program.replace(f"@{key}@", value)
    cpp = folder / "input.cpp"
    exe = folder / ("input.exe" if __import__("os").name == "nt" else "input")
    cpp.write_text(program, encoding="utf-8")
    subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra",
                    "-Wno-unused-function", "-Wno-unused-variable", "-include", "initializer_list",
                    str(cpp), "-o", str(exe)], check=True)
    output = subprocess.check_output([str(exe)], text=True, timeout=10)
    return [{"ref": ref, **json.loads(line)} for line in output.splitlines()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refs", nargs="+", default=DEFAULT_REFS)
    parser.add_argument("--compiler", default=shutil.which("g++") or shutil.which("clang++"))
    args = parser.parse_args()
    if not args.compiler:
        parser.error("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="navigation-input-") as temp:
        for ref in args.refs:
            for result in run(ref, args.compiler, Path(temp)):
                print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
