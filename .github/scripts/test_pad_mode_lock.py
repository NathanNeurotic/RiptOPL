"""The menu keeps a DualShock locked in analog mode, and puts the lock back when it is lost.

zackcage6 on #133 (2026-10-31, v1.2.0-Beta-4010, DualShock 2, rumble and cursor sounds on): turning the
pad's ANALOG button OFF then ON hangs the scrolling, and pressing it over and over pauses scrolling for
over 5 seconds. The D-pad alone is fine.

The button can only do that while the pad is unlocked. Each ANALOG press changes the pad's mode, and
freepad answers a mode change by re-reading the whole pad: 240-260 ms with no samples (EXECCMD in his
RIPTOPL-NAVTRACE capture of 2026-10-29). Pressed again before that ends, the re-read starts over.
Upstream OPL sends padSetMainMode(DUALSHOCK, LOCK) and the button then does nothing. RiptOPL sent it
once and never checked it: freepad clears the lock on every reconnect, the reconnect re-init is skipped
when the pad is already back in analog mode, and a refused or failed request was not retried.

This compiles the real read path of src/core/pad.cpp on the host (readOne, read, the lock functions,
the repeat timers, key/keyOn/keyPressed, rumble) against a simulated freepad where an unlocked pad's
ANALOG button changes the mode and costs 13 polls of EXECCMD, and padSetMainMode(LOCK) runs for 12
polls, with reqState reaching the EE one poll late. It checks:

- an unlocked pad is locked in the next quiet second, and ANALOG presses after that do not stop a held
  direction from scrolling;
- a pad that comes back from a reconnect already in analog mode (the re-init is skipped) is locked;
- the lock is not confirmed from a stale reqState, a failed request is retried, and a pad that keeps
  refusing gets at most three requests;
- a pressure-mode change (0x73 -> 0x79) is not mistaken for an ANALOG press;
- a pad with no DualShock modes gets no lock request.

Pass --ref <git revision> to run the same scenarios against an older pad.cpp; the parent of this change
fails them, which shows the harness sees the defect.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
failures = []

ref = None
if len(sys.argv) == 3 and sys.argv[1] == '--ref':
    ref = sys.argv[2]


def read_source(path):
    if ref is None:
        return (root / path).read_text(encoding='utf-8').replace('\r\n', '\n')
    return subprocess.run(['git', '-C', str(root), 'show', '%s:%s' % (ref, path)], check=True,
                          capture_output=True, text=True).stdout.replace('\r\n', '\n')


pad = read_source('src/core/pad.cpp')
header = read_source('include/padmanager.h')


def function_text(source, signature, optional=False):
    match = re.search(r'^' + re.escape(signature) + r'[^;{]*\)\s*\{', source, re.M)
    if match is None:
        if not optional:
            failures.append('src/core/pad.cpp: %s...) not found' % signature)
        return ''
    end = source.index('\n}', match.start())
    return source[match.start():end] + '\n}\n'


key_table = re.search(r'^static const int keyToPad\[17\] = \{.*?\};', pad, re.M | re.S)
if key_table is None:
    failures.append('src/core/pad.cpp: keyToPad table not found')
constants = '\n'.join(re.findall(r'^(?:constexpr [^\n]*;[^\n]*|#define (?:CLOCKS_PER_MILISEC|DEFAULT_PAD_DELAY)\s[^\n]*)$', pad, re.M))

functions = ''.join(function_text(pad, sig) for sig in (
    'static int isPadReadyState(',
    'void PadManager::updateState(',
    'u32 PadManager::readLeftJoy(',
    'int PadManager::readOne(',
    'int PadManager::keyDelay(',
    'void PadManager::rumbleRealign(',
    'void PadManager::actSet(',
    'void PadManager::rumbleFlush(',
    'void PadManager::rumble(',
    'u32 PadManager::pollElapsedMs(',
    'void PadManager::advanceRepeatTimers(',
    'int PadManager::read(',
    'int PadManager::key(',
    'int PadManager::keyOn(',
    'int PadManager::keyPressed(',
)) + ''.join(function_text(pad, sig, optional=True) for sig in (
    'void PadManager::lockRequested(',
    'void PadManager::lockCheck(',
    'int PadManager::lockRelock(',
))

LIBPAD = r'''
#pragma once
#include <stdint.h>
struct padButtonStatus {
    uint8_t ok, mode;
    uint16_t btns;
    uint8_t rjoy_h, rjoy_v, ljoy_h, ljoy_v;
    uint8_t reserved[24];
};
#define PAD_STATE_DISCONN 0
#define PAD_STATE_FINDPAD 1
#define PAD_STATE_FINDCTP1 2
#define PAD_STATE_EXECCMD 5
#define PAD_STATE_STABLE 6
#define PAD_STATE_ERROR 7
#define PAD_RSTAT_COMPLETE 0
#define PAD_RSTAT_FAILED 1
#define PAD_RSTAT_BUSY 2
#define PAD_MODECURID 1
#define PAD_MODECUREXID 2
#define PAD_MODETABLE 4
#define PAD_TYPE_DIGITAL 4
#define PAD_TYPE_DUALSHOCK 7
#define PAD_MMODE_DIGITAL 0
#define PAD_MMODE_DUALSHOCK 1
#define PAD_MMODE_UNLOCK 2
#define PAD_MMODE_LOCK 3
#define PAD_LEFT 0x0080
#define PAD_DOWN 0x0040
#define PAD_RIGHT 0x0020
#define PAD_UP 0x0010
#define PAD_START 0x0008
#define PAD_R3 0x0004
#define PAD_L3 0x0002
#define PAD_SELECT 0x0001
#define PAD_SQUARE 0x8000
#define PAD_CROSS 0x4000
#define PAD_CIRCLE 0x2000
#define PAD_TRIANGLE 0x1000
#define PAD_R1 0x0800
#define PAD_L1 0x0400
#define PAD_R2 0x0200
#define PAD_L2 0x0100
int padGetState(int port, int slot);
int padGetReqState(int port, int slot);
int padRead(int port, int slot, struct padButtonStatus *data);
int padInfoMode(int port, int slot, int infoMode, int index);
int padInfoAct(int port, int slot, int actuator, int cmd);
int padSetActAlign(int port, int slot, const char *actAlign);
int padSetActDirect(int port, int slot, const char *actAlign);
int padSetMainMode(int port, int slot, int mode, int lock);
'''

HARNESS = r'''
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef uint64_t u64;
#define private public
#include "padmanager.h"
#undef private

#define LOG(...) ((void)0)
static void diagLog(const char *, ...) {}
int gEnableRumble = 1, gXSensitivity = 1, gYSensitivity = 1;
static int KeyPressedOnce, DisableCron;

static u32 fakeTicks;
static u32 cpu_ticks(void) { return fakeTicks; }

/* Simulated freepad + DualShock 2 on port 0, nothing on port 1. */
static int simState, simExec, simModeReq, simReq, simReqShown, simRefuse, simFailNext, simExId, simLocked;
static u16 simButtons; /* active-high */
static u8 simMode;
static int lockRequests;

int padGetState(int port, int slot)
{
    (void)slot;
    if (port != 0)
        return PAD_STATE_DISCONN;
    return simExec > 0 ? PAD_STATE_EXECCMD : simState;
}
/* What the EE sees is last frame's DMA copy: a new request shows up one poll late. */
int padGetReqState(int, int) { return simReqShown; }
int padRead(int port, int, struct padButtonStatus *d)
{
    if (port != 0 || simExec > 0 || simState != PAD_STATE_STABLE)
        return 0;
    d->btns = (u16)(0xffff ^ simButtons);
    d->mode = simMode;
    d->ljoy_h = d->ljoy_v = d->rjoy_h = d->rjoy_v = 127;
    return 32;
}
int padInfoMode(int port, int, int infoMode, int)
{
    if (port != 0 || simExec > 0)
        return 0;
    if (infoMode == PAD_MODECUREXID)
        return simExId;
    if (infoMode == PAD_MODECURID)
        return (simMode >> 4) == 7 ? PAD_TYPE_DUALSHOCK : PAD_TYPE_DIGITAL;
    return 0;
}
int padInfoAct(int, int, int, int) { return 2; }
int padSetActAlign(int, int, const char *) { return 1; }
int padSetActDirect(int, int, const char *) { return 1; }
int padSetMainMode(int port, int, int mode, int lock)
{
    if (port != 0 || simExec > 0 || simReq == PAD_RSTAT_BUSY || simRefuse)
        return 0;
    lockRequests++;
    simModeReq = (mode == PAD_MMODE_DUALSHOCK ? 1 : 0) | (lock == PAD_MMODE_LOCK ? 2 : 0);
    simReq = PAD_RSTAT_BUSY;
    simExec = 12;
    return 1;
}

static int initCalls;
int PadManager::initialize(struct pad_data_t *)
{
    initCalls++;
    return 1;
}

@TABLE@
@CONSTANTS@
@FUNCTIONS@

static PadManager pm;
static const u32 TICKS_PER_MS = 294912;
static int failures;
static int moves, frameNo, lastMove, longestGap;

static void reset(u8 mode, int locked)
{
    pm.~PadManager();
    new (&pm) PadManager();
    pm.padCount_ = 2;
    pm.padData_[0].port = 0;
    pm.padData_[0].state = PAD_STATE_STABLE;
    pm.padData_[0].actuators = 2;
    pm.padData_[0].actAligned = 1;
    pm.padData_[1].port = 1;
    pm.padData_[1].state = PAD_STATE_DISCONN;
    for (int i = 0; i < 16; i++)
        pm.delaycnt_[i] = pm.paddelay_[i] = DEFAULT_PAD_DELAY;
    simState = PAD_STATE_STABLE;
    simExec = simModeReq = simRefuse = simFailNext = 0;
    simReq = simReqShown = PAD_RSTAT_COMPLETE;
    simExId = 1;
    simButtons = 0;
    simMode = mode;
    simLocked = locked;
    lockRequests = initCalls = 0;
    moves = frameNo = lastMove = longestGap = 0;
}

/* The DualShock's ANALOG button: nothing when locked, else a mode change and freepad's re-read. */
static void pressAnalog(void)
{
    if (simLocked)
        return;
    simMode = (simMode >> 4) == 7 ? 0x41 : 0x73;
    simExec = 13;
}

/* One 20 ms frame: read the pads, then the menu asks getKey() and plays the cursor sound. */
static void frame(int keyId)
{
    fakeTicks += 20 * TICKS_PER_MS;
    frameNo++;
    pm.read();
    simReqShown = simReq;
    if (simExec > 0 && --simExec == 0 && simReq == PAD_RSTAT_BUSY) {
        if (simFailNext) {
            simFailNext = 0;
            simReq = PAD_RSTAT_FAILED;
        } else {
            if (simModeReq & 1)
                simMode = 0x73;
            simLocked = (simModeReq & 2) != 0;
            simReq = PAD_RSTAT_COMPLETE;
        }
    }
    if (pm.key(keyId)) {
        moves++;
        if (frameNo - lastMove > longestGap)
            longestGap = frameNo - lastMove;
        lastMove = frameNo;
        pm.rumble(100, 128);
    }
}

static void frames(int n, int keyId)
{
    while (n-- > 0)
        frame(keyId);
}

/* Hold a direction for n frames, pressing ANALOG every 15 frames (300 ms). Returns the moves. */
static int holdWithAnalog(int n, int keyId, u16 bit)
{
    int start = moves;
    simButtons = bit;
    lastMove = frameNo;
    longestGap = 0;
    for (int i = 0; i < n; i++) {
        if (i % 15 == 7)
            pressAnalog();
        frame(keyId);
    }
    simButtons = 0;
    return moves - start;
}

static void expect(const char *what, int got, int want)
{
    printf("%-74s %d (want %d)\n", what, got, want);
    if (got != want)
        failures++;
}

int main(void)
{
    const int DOWN = 2;

    /* Baseline: a 3 s hold on a locked pad with ANALOG pressed throughout. */
    reset(0x73, 1);
    frames(5, DOWN);
    int baseline = holdWithAnalog(150, DOWN, PAD_DOWN);
    int baselineGap = longestGap;
    printf("baseline: locked pad, 3 s hold with ANALOG pressed every 300 ms: %d moves, longest gap %d frames\n",
           baseline, baselineGap);

    /* 1. Unlocked analog pad (the state a skipped reconnect re-init leaves). Hold DOWN and play with
          ANALOG for 3 s, let go for 1.5 s, then do it again. */
    reset(0x73, 0);
    frames(5, DOWN);
    int first = holdWithAnalog(150, DOWN, PAD_DOWN);
    int firstGap = longestGap;
    frames(75, DOWN);
    expect("unlocked pad: lock requested in the quiet second after", lockRequests, 1);
    expect("unlocked pad: the pad is locked", simLocked, 1);
    int second = holdWithAnalog(150, DOWN, PAD_DOWN);
    printf("unlocked pad: first 3 s %d moves (longest gap %d frames), second 3 s %d moves (longest gap %d frames)\n",
           first, firstGap, second, longestGap);
    expect("after the lock: ANALOG presses no longer stop the scrolling", second == baseline, 1);
    expect("after the lock: longest gap between moves no longer than the locked pad's", longestGap <= baselineGap, 1);

    /* 2. Reconnect: freepad clears the lock; the pad is back in analog mode, so the re-init is skipped. */
    reset(0x73, 1);
    frames(5, DOWN);
    simState = PAD_STATE_DISCONN;
    simLocked = 0;
    frames(5, DOWN);
    simState = PAD_STATE_STABLE;
    frames(100, DOWN);
    expect("reconnect back in analog mode: re-init run", initCalls, 0);
    expect("reconnect back in analog mode: lock requested", lockRequests, 1);
    expect("reconnect back in analog mode: the pad is locked", simLocked, 1);

    /* 3. The lock is not confirmed from the stale reqState of the poll before the request. */
    reset(0x73, 0);
    int earlyLocked = 0;
    for (int i = 0; i < 100; i++) {
        frame(DOWN);
        if (simExec > 0 && pm.padData_[0].locked)
            earlyLocked = 1;
    }
    expect("lock confirmed while freepad was still running it", earlyLocked, 0);
    expect("lock confirmed after it finished", pm.padData_[0].locked, 1);

    /* 4. A request that fails inside freepad is sent again. */
    reset(0x73, 0);
    simFailNext = 1;
    frames(200, DOWN);
    expect("failed request: requests sent", lockRequests, 2);
    expect("failed request: the pad is locked", simLocked, 1);

    /* 5. A pad that refuses every request gets at most three tries. */
    reset(0x73, 0);
    simRefuse = 1;
    frames(500, DOWN);
    expect("refused requests: tries recorded", pm.padData_[0].lockTries, 3);
    expect("refused requests: the pad is not reported locked", pm.padData_[0].locked, 0);

    /* 6. Pressure mode (0x73 -> 0x79) is not an ANALOG press. */
    reset(0x73, 1);
    frames(100, DOWN); /* locked by the quiet gate (the PadManager starts unaware) */
    int before = lockRequests;
    simMode = 0x79;
    frames(100, DOWN);
    expect("0x73 -> 0x79 on a locked pad: extra lock requests", lockRequests - before, 0);

    /* 6b. A confirmed lock that is lost (by whatever path) is noticed at the next ANALOG press and sent again. */
    reset(0x73, 1);
    frames(100, DOWN); /* locked and confirmed by the quiet gate */
    expect("lost lock: confirmed before", pm.padData_[0].locked, 1);
    before = lockRequests;
    int cleared = 0;
    simLocked = 0;
    simButtons = PAD_DOWN; /* held across the mode change: no request while input is live */
    pressAnalog();
    for (int i = 0; i < 150; i++) {
        frame(DOWN);
        if (!pm.padData_[0].locked)
            cleared = 1;
    }
    expect("lost lock: the software lock flag cleared after the mode change", cleared, 1);
    expect("lost lock: no lock request while DOWN is held", lockRequests - before, 0);
    simButtons = 0;
    frames(100, DOWN);
    expect("lost lock: lock requests sent again", lockRequests - before, 1);
    expect("lost lock: the pad is locked again", simLocked, 1);

    /* 7. No DualShock modes: no lock request. */
    reset(0x41, 0);
    simExId = 0;
    frames(200, DOWN);
    expect("digital-only pad: lock requests", lockRequests, 0);

    return failures ? 1 : 0;
}
'''

if not failures:
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'libpad.h').write_text(LIBPAD, encoding='utf-8')
        (tmp / 'padmanager.h').write_text(header, encoding='utf-8')
        harness = HARNESS
        if 'int locked;' not in header:
            # An older pad.cpp has no lock bookkeeping: read the scenario results from the simulation alone.
            harness = harness.replace('pm.padData_[0].lockTries', '-1').replace('pm.padData_[0].locked', '0')
        source = (harness.replace('@TABLE@', key_table.group(0))
                  .replace('@CONSTANTS@', constants)
                  .replace('@FUNCTIONS@', functions))
        (tmp / 'pad_mode_lock.cpp').write_text('#include <new>\n' + source, encoding='utf-8')
        exe = tmp / 'pad_mode_lock'
        build = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wno-unused-function', '-I', str(tmp),
                                str(tmp / 'pad_mode_lock.cpp'), '-o', str(exe)],
                               capture_output=True, text=True)
        if build.returncode != 0:
            failures.append('harness did not compile:\n' + build.stderr)
        else:
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            print(run.stdout, end='')
            if run.returncode != 0:
                failures.append('scenarios failed (see above)')

if failures:
    for failure in failures:
        print('FAIL: ' + failure)
    sys.exit(1)
print('pad mode lock: OK')
