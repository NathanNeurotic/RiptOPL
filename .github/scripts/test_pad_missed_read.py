"""A missed pad read is unknown, not a release; rumble alignment never lands in the middle of input.

zackcage6's RIPTOPL-NAVTRACE capture (2026-10-29, SCPH-30001R, DualShock 2, rumble and cursor
sounds on) recorded every native pad read. Two shapes made one press move the cursor twice:

1. The first rumble pulse ran padSetActAlign from inside read(). That pulse is armed by the press's
   own cursor sound, so the alignment command went out while the direction was still held. freepad
   then answered no reads for about 80 ms (EXECCMD), read() zeroed the held bits, and when samples
   came back with the direction still down keyOn() saw a second press. It happened on the first
   press after boot and on the first press after each reconnect re-init.
2. Pressing ANALOG while holding a direction: freepad re-reads the pad for 240-260 ms (EXECCMD,
   no samples), and the same release + press followed.

This compiles the real read path of src/core/pad.cpp on the host (readOne, read, the repeat
timers, key/keyOn/keyPressed, rumble, rumbleRealign, actSet, rumbleFlush) against a simulated
libpad whose padSetActAlign puts the pad into EXECCMD for four polls, as the trace shows, and
checks:

- the first press with rumble on moves once, and no padSetActAlign is sent while a button is held;
  alignment happens in the next quiet second instead, and rumble works after it;
- a hold across an ANALOG press (13 polls of EXECCMD with no reads, mode 0x41 -> 0x73) is one press,
  with no repeat fired from the unknown polls;
- a blank reply taken while freepad is mid-command is not a release;
- unplugging (FINDPAD) releases at once, and a pad that stays silent past 300 ms is released;
- a release that arrives with the first sample after a miss is seen at once.

Pass --ref <git revision> to run the same scenarios against an older pad.cpp; the parent of this
change fails them, which shows the harness sees the defect.
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


def function_text(source, signature):
    match = re.search(r'^' + re.escape(signature) + r'[^;{]*\)\s*\{', source, re.M)
    if match is None:
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
#define PAD_TYPE_DUALSHOCK 7
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

/* Simulated freepad: one DualShock on port 0, nothing on port 1. */
static int simState = PAD_STATE_STABLE, simLen = 32, simExec = 0, simBlankExec = 0;
static u16 simButtons = 0; /* active-high, what the user is physically pressing */
static u8 simMode = 0x41;
static int alignCalls, alignWhileHeld, directCalls;

int padGetState(int port, int slot)
{
    if (port != 0)
        return PAD_STATE_DISCONN;
    return simExec > 0 ? PAD_STATE_EXECCMD : simState;
}
int padGetReqState(int, int) { return simExec > 0 ? PAD_RSTAT_BUSY : PAD_RSTAT_COMPLETE; }
int padRead(int port, int, struct padButtonStatus *d)
{
    if (port != 0)
        return 0;
    if (simExec > 0) {
        if (!simBlankExec)
            return 0;
        d->btns = 0xffff; /* a reply taken mid-command: everything released */
        d->mode = simMode;
        return 32;
    }
    if (simState != PAD_STATE_STABLE || !simLen)
        return 0;
    d->btns = (u16)(0xffff ^ simButtons);
    d->mode = simMode;
    d->ljoy_h = d->ljoy_v = d->rjoy_h = d->rjoy_v = 127;
    return simLen;
}
int padInfoMode(int, int, int, int) { return PAD_TYPE_DUALSHOCK; }
int padInfoAct(int, int, int, int) { return 2; }
int padSetActAlign(int, int, const char *)
{
    alignCalls++;
    if (simButtons)
        alignWhileHeld++;
    simExec = 4; /* trace: ~80 ms of EXECCMD with no samples */
    return 1;
}
int padSetActDirect(int, int, const char *)
{
    directCalls++;
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
static int moves, edges;

static void reset(int aligned)
{
    pm.~PadManager();
    new (&pm) PadManager();
    pm.padCount_ = 2;
    pm.padData_[0].port = 0;
    pm.padData_[0].state = PAD_STATE_STABLE;
    pm.padData_[0].actuators = 2;
    pm.padData_[0].actAligned = aligned;
    pm.padData_[1].port = 1;
    pm.padData_[1].state = PAD_STATE_DISCONN;
    for (int i = 0; i < 16; i++)
        pm.delaycnt_[i] = pm.paddelay_[i] = DEFAULT_PAD_DELAY;
    simState = PAD_STATE_STABLE;
    simLen = 32;
    simExec = simBlankExec = 0;
    simButtons = 0;
    simMode = 0x41;
    alignCalls = alignWhileHeld = directCalls = initCalls = 0;
    moves = edges = 0;
}

/* One 20 ms frame: what the GUI does -- read the pads, then the menu asks getKey() and plays the
   cursor sound, which arms a rumble pulse, on a move. */
static void frame(int keyId)
{
    fakeTicks += 20 * TICKS_PER_MS;
    pm.read();
    if (simExec > 0)
        simExec--;
    if (pm.keyOn(keyId))
        edges++;
    if (pm.key(keyId)) {
        moves++;
        pm.rumble(100, 128);
    }
}

static void frames(int n, int keyId)
{
    while (n-- > 0)
        frame(keyId);
}

static void expect(const char *what, int got, int want)
{
    printf("%-70s %d (want %d)\n", what, got, want);
    if (got != want)
        failures++;
}

int main(void)
{
    const int RIGHT = 3, LEFT = 1;

    /* 1. First press after boot, rumble on, alignment not yet confirmed. 200 ms press. */
    reset(0);
    frames(10, RIGHT);
    simButtons = PAD_RIGHT;
    frames(10, RIGHT);
    simButtons = 0;
    frames(3, RIGHT);
    expect("first press, rumble on: moves", moves, 1);
    expect("first press, rumble on: padSetActAlign sent while a button was held", alignWhileHeld, 0);
    frames(50, RIGHT); /* a quiet second */
    expect("quiet second after it: padSetActAlign sent", alignCalls, 1);
    expect("quiet second after it: alignment confirmed", pm.padData_[0].actAligned, 1);
    directCalls = 0;
    simButtons = PAD_RIGHT;
    frames(10, RIGHT);
    simButtons = 0;
    frames(10, RIGHT);
    expect("next press: moves", moves, 2);
    expect("next press: rumble commands reached the pad", directCalls > 0, 1);

    /* 2. Hold LEFT and press ANALOG 300 ms in: 13 polls of EXECCMD with no reads, then analog mode. */
    reset(1);
    frames(5, LEFT);
    simButtons = PAD_LEFT;
    frames(15, LEFT);
    int before = moves;
    simExec = 13;
    int repeatsDuringMiss = 0;
    for (int i = 0; i < 13; i++) {
        int m = moves;
        frame(LEFT);
        repeatsDuringMiss += moves - m;
    }
    simMode = 0x73;
    frames(20, LEFT);
    simButtons = 0;
    frames(3, LEFT);
    expect("hold across an ANALOG press: key-on edges", edges, 1);
    expect("hold across an ANALOG press: moves while no sample arrived", repeatsDuringMiss, 0);
    expect("hold across an ANALOG press: still repeating after it", moves > before, 1);

    /* 3. A blank reply while freepad is mid-command, during a hold. */
    reset(1);
    simButtons = PAD_RIGHT;
    frames(5, RIGHT);
    simExec = 1;
    simBlankExec = 1;
    frames(1, RIGHT);
    simBlankExec = 0;
    frames(5, RIGHT);
    simButtons = 0;
    frames(2, RIGHT);
    expect("blank reply mid-command during a hold: key-on edges", edges, 1);

    /* 4. Unplug mid-hold: FINDPAD releases at once. */
    reset(1);
    simButtons = PAD_RIGHT;
    frames(5, RIGHT);
    simState = PAD_STATE_FINDPAD;
    frames(1, RIGHT);
    expect("unplugged (FINDPAD): direction still pressed", pm.keyPressed(RIGHT) != 0, 0);
    simState = PAD_STATE_STABLE;
    simButtons = 0;
    frames(2, RIGHT);

    /* 5. A pad that stays silent past 300 ms is released. */
    reset(1);
    simButtons = PAD_RIGHT;
    frames(5, RIGHT);
    simLen = 0;
    frames(10, RIGHT); /* 200 ms */
    expect("silent 200 ms: direction still pressed", pm.keyPressed(RIGHT) != 0, 1);
    frames(10, RIGHT); /* 400 ms */
    expect("silent 400 ms: direction still pressed", pm.keyPressed(RIGHT) != 0, 0);
    simLen = 32;
    simButtons = 0;
    frames(2, RIGHT);

    /* 6. Released during the miss: the first sample after it releases at once. */
    reset(1);
    simButtons = PAD_RIGHT;
    frames(5, RIGHT);
    simLen = 0;
    frames(3, RIGHT);
    simButtons = 0;
    simLen = 32;
    frames(1, RIGHT);
    expect("released during a miss: pressed on the first sample after it", pm.keyPressed(RIGHT) != 0, 0);
    expect("released during a miss: moves", moves, 1);

    /* 7. Rumble off: no alignment traffic at all. */
    reset(0);
    gEnableRumble = 0;
    frames(100, RIGHT);
    expect("rumble off: padSetActAlign sent", alignCalls, 0);
    gEnableRumble = 1;

    return failures ? 1 : 0;
}
'''

if not failures:
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'libpad.h').write_text(LIBPAD, encoding='utf-8')
        (tmp / 'padmanager.h').write_text(header, encoding='utf-8')
        source = (HARNESS.replace('@TABLE@', key_table.group(0))
                  .replace('@CONSTANTS@', constants)
                  .replace('@FUNCTIONS@', functions))
        (tmp / 'pad_missed_read.cpp').write_text('#include <new>\n' + source, encoding='utf-8')
        exe = tmp / 'pad_missed_read'
        build = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wno-unused-function', '-I', str(tmp),
                                str(tmp / 'pad_missed_read.cpp'), '-o', str(exe)],
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
print('pad missed reads: OK')
