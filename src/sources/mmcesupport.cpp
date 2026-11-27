#include <new>
#include "include/host.h"
#include "include/diaglog.h" // the hardware log, DIAG.TXT (RIPPS2 build 115)
#include "include/opl.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/pad.h" // readPads/getKeyOn: START skips the Game ID card wait
#include "include/supportbase.h"
#include "include/nativevmc.h"
#include "include/mmcesupport.h"
#include "include/vcdsupport.h"
#include "include/cuesupport.h" // PS1 rows can belong to either core; the ROW decides
#include "include/saveicon.h"   // saveIconPs1Directory
#include "include/libview.h"    // libViewActive / libListViewActive -- which list this page shows
#include "include/folderbrowse.h"
#include "include/util.h"
#include "include/themes.h"
#include "include/textures.h"
#include "include/texcache.h"
#include "include/ioman.h"
#include "include/system.h"
#include "include/extern_irx.h"
#include "include/cheatman.h"
#include "include/favsupport.h" // loadFavourites -- a reappearing/disappearing slot updates All Games
#include "include/gamesource.h"
#include "include/mmceint.h" // gMmceSupport -- owns the module state (was file-scope statics)
#include "modules/iopcore/common/cdvd_config.h"
#include "../ee_core/include/coreconfig.h"
#include <usbhdfsd-common.h>

#include <kernel.h>
#include "include/ps2sdkapi_compat.h"
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioIoctl, fileXioDevctl
#include <delaythread.h> // DelayThread() -- real-sleep gap in the MMCE card-switch wait

// The one MMCE state owner (class + rationale in include/mmceint.h). Members are
// constant-initialized (EE rule: no dynamic initialization before main).
MmceSupport gMmceSupport;

// An in-menu IOP reset unloads every module; forget the latches so the next refresh loads them again.
// Ported from RIPPS2 build 115 (akilluminati47/RIPPS2).
void mmceIopWasReset(void)
{
    gMmceSupport.modLoaded = 0;
}

// VCD-scan retry budget (member ps1ScanRetries): ~15 passes at the ~2s cadence, re-armed by a fresh
// L3 toggle, self-quiescing on success/expiry. (The other half of the old "-1 for both" ambiguity
// is split at the source: vcdScanOpenDir returns 0 for a genuinely-ABSENT POPS folder -- #154
// residual -- so only contention ever arms this budget.)
constexpr int MMCE_VCD_SCAN_RETRY_MAX = 15;
// Bounded CFG-create retries per card before declaring it obstructed.
constexpr int MMCE_FOLDER_RETRY_MAX = 5;
// Auto-slot presence debounce (#154 audit residual): the cache-hit probe in setPrefix used to
// invalidate the slot on ONE failed presence devctl -- and update() then frees BOTH game lists --
// so a transiently contended SIO2 bus (list scan + art read + MX4SIO traffic all sharing it) read
// as "card pulled" and both lists vanished until the next ~2s refresh re-detected. Require
// MMCE_PRESENCE_PROBE_MAX CONSECUTIVE failures before invalidating, re-probing INLINE with a short
// gap (the same 200 ms DelayThread cadence the GameID settle loops use): worst case ~1.0 s on a
// genuine pull (3x the devctl's own 200 ms timeout + 2 gaps), still inside the ~2s refresh cadence,
// so a pull is still detected on THIS pass -- just two probes later -- while a bus-quiet window
// between probes absorbs the transient. No persistent counter: any successful probe breaks the loop
// and declares the card present.
constexpr int MMCE_PRESENCE_PROBE_MAX = 3;
constexpr int MMCE_PRESENCE_RETRY_US = (200 * 1000);
// Game ID send: the launch sends 0x8 and goes on at once, as NHDDL does. The card switches on its own, fast
// enough that the old wait for it to begin (up to 2 s, ported from RIPPS2 build 115) only cost every launch time.
// The launches that stream from the card itself still look at it before the handoff (waitSwapDone,
// waitBeforeHandoff, waitHistorySwitch), and the MX4SIO leg keeps its settle gate (gameIdSettle).
constexpr int MMCE_GAMEID_QUERY_FAILURES = 3; // failed busy queries in a row that end the wait (unconfirmed)
constexpr int MMCE_CARD_READY_MAX_S = 180;    // the longest the launch waits for the game's card
constexpr int MMCE_CARD_READY_POLL_US = (250 * 1000);
constexpr int MMCE_CARD_READY_SILENT_MS = 7500; // the longest the card may not answer at all (mmceigr's bound)
// START is read this often (about a frame) while a card wait sleeps between two looks at the card: a quick tap is
// shorter than a look's interval and would otherwise fall between two reads (getKeyOn is an edge between them).
constexpr int MMCE_START_SAMPLE_US = (20 * 1000);
// The card's OWN switch after the OSD history write (sd2psXtd ps2_history_tracker: 2 s of quiet after the last
// history page write, then the folder switch). The window is how long the launch watches for it to begin.
constexpr int MMCE_HISTORY_SWITCH_WINDOW_MS = 3500;
constexpr int MMCE_HISTORY_SWITCH_POLL_MS = 250;
// Card reset (0x9) wait: poll the busy bit every 200 ms for up to ~3 s.
constexpr int MMCE_GAMEID_WAIT_TICKS = 15;
constexpr int MMCE_GAMEID_POLL_US = (200 * 1000);
/* The MMCE worker checks its abort flag between 32 KB staged reads. A
 * 500 ms wait gives a slow card time to reach the next safe checkpoint. */
constexpr int MMCE_ART_ABORT_WAIT_TICKS = 500;

// The MMCE slot a device root names: 0 for "mmce0:...", 1 for "mmce1:...", -1 for anything else.
static int mmceSlotOf(const char *device)
{
    if (device == NULL || strncmp(device, "mmce", 4) != 0 || (device[4] != '0' && device[4] != '1'))
        return -1;
    return device[4] - '0';
}

// Whether a card whose busy query just failed has no busy query at all (a firmware without it). Only when it then
// answers its presence query AND fails a fresh busy query. The presence and busy queries are separate commands and
// the card can change state between them: one that answers neither is in its swap (silence is the swap's first
// phase), and one whose busy query answers now was silent a moment ago and is back, loading its new card. Every
// "this card has no busy query" decision is made here, never on a failed busy query alone.
static int mmceLacksBusyQuery(const char *device)
{
    return fileXioDevctl(device, 0x1, NULL, 0, NULL, 0) != -1 && fileXioDevctl(device, 0x2, NULL, 0, NULL, 0) < 0;
}

// START during a card's wait (getKeyOn is an edge between two reads: each press is seen once). *latch is the wait's
// own: it keeps the press for the rest of that wait, which can be more than one look (the handoff's look at the
// other slot, then at the game's): one press launches, so a later look still asks its card but waits nothing out.
// The press is answered on screen at once: what follows START can still take a moment (the card's file system
// coming back before the game's files are opened on it), and the wait's last frame left standing reads as a freeze.
static int mmceStartPressed(int *latch)
{
    if (*latch)
        return 1;
    readPads();
    if (!getKeyOn(KEY_START))
        return 0;
    *latch = 1;
    host::ui().renderTextScreen(_l(_STR_PLEASE_WAIT));
    return 1;
}

// A card wait's sleep between two looks at the card, reading START about once a frame (MMCE_START_SAMPLE_US): a
// tap is shorter than the look's interval and would otherwise fall between two reads. It ends early on START, so
// the wait's next look ends the wait at once. The card is still asked only once per interval.
static void mmceSleepReadingStart(int us, int *latch)
{
    while (us > 0) {
        int slice = us < MMCE_START_SAMPLE_US ? us : MMCE_START_SAMPLE_US;
        DelayThread(slice);
        us -= slice;
        if (mmceStartPressed(latch))
            return;
    }
}

int MmceSupport::sendGameID(const char *startup, const char *protectMcPath, int vmcSlotMask)
{
    gameIdTargetMask = 0; // no stale targets: only sends made by THIS call may be settled against
    gameIdSwitchSeenMask = 0;

    int allowedMask = 0;
    if (gMMCEEnableGameID == MMCE_GAMEID_SLOT0)
        allowedMask = (1 << 0);
    else if (gMMCEEnableGameID == MMCE_GAMEID_SLOT1)
        allowedMask = (1 << 1);
    else if (gMMCEEnableGameID == MMCE_GAMEID_BOTH)
        allowedMask = (1 << 0) | (1 << 1);

    if (allowedMask == 0 || startup == NULL || startup[0] == '\0')
        return 0;

    // Δ4 (NHDDL parity): do NOT self-arm the transport here. Arming happens at menu/settings time
    // (mmceArmGameIDTransport). If the module is not resident by launch time, skip gracefully.
    if (!modLoaded) {
        LOG("MMCE GameID: transport not armed -- skipping (menu-time arm failed or pending)\n");
        return 0;
    }

    for (int slot = 0; slot < MMCE_MODE_COUNT; slot++) {
        if (!((allowedMask >> slot) & 1))
            continue;
        if ((vmcSlotMask >> slot) & 1) {
            LOG("MMCE GameID: slot %d covered by a -mc%d VMC arg -- not switching that card\n", slot, slot);
            continue;
        }
        const char *mmceDevice = (slot == 0) ? "mmce0:/" : "mmce1:/";
        if (fileXioDevctl(mmceDevice, 0x1, NULL, 0, NULL, 0) == -1)
            continue; // no card in this slot

        // NHDDL-parity guard (#51): never switch the per-game card on a slot whose EMULATED memory
        // card (mcN:) holds the neutrino.elf we are about to load. Skip only this slot so the other
        // slot can still receive its GameID when Both is selected.
        if (protectMcPath != NULL && protectMcPath[0] != '\0') {
            const char *mc = (slot == 0) ? "mc0:" : "mc1:";
            if (!strncmp(protectMcPath, mc, strlen(mc))) {
                host::ui().warning(_l(_STR_MMCE_GAMEID_NEUTRINO_SKIP), 6);
                continue;
            }
        }

        if (fileXioDevctl(mmceDevice, 0x8, (void *)startup, (strlen(startup) + 1), NULL, 0) < 0)
            continue;

        gameIdTargetMask |= (1 << slot);
        diagLog("gameid %s (%s): sent", startup, mmceDevice);

        // Neutrino loads from this card's own file system (mmceN:) next: wait the switch out so neutrino.elf and
        // its modules are not read while the card swaps. Bounded (the switch window, then the card-ready wait).
        // START ends this wait only: the caller's own look (waitSwapDone) still waits if the card is silent or busy.
        if (protectMcPath != NULL && !strncmp(protectMcPath, mmceDevice, 5) &&
            waitHistorySwitch(mmceDevice, "Game ID, Neutrino on this card")) {
            gameIdSwitchSeenMask |= (1 << slot);
            settleDeviceFs(mmceDevice);
        }
    }

    if (gameIdTargetMask == 0)
        return 0; // no eligible MMCE card present -> graceful no-op
    return -1;    // sent, not waited for: the callers' own looks at the card (waitSwapDone, gameIdSettle) apply
}

// Waits, on screen, while the card reports busy after a Game ID switch. Returns 1 when it is ready or the
// user pressed START (now, or earlier in the same wait: *start, see mmceStartPressed; NULL: this wait alone),
// 0 when it was still busy at MMCE_CARD_READY_MAX_S, -1 when the card stopped answering for
// MMCE_CARD_READY_SILENT_MS before it reported ready (unconfirmed). START is read before either bound: a press
// made while the wait slept launches, even on the look that would have ended the wait. The launch goes ahead in
// every case. An SD2PSX does not answer at all while it swaps cards (sd2psXtd ps2_mmceman_task(): card exit,
// close, 500 ms, open), so a failed query here is part of the switch, not its end.
int MmceSupport::waitCardReady(const char *device, int *start)
{
    char line[256];
    int status, polls, silent = 0, own = 0;

    if (start == NULL)
        start = &own;
    for (polls = 0;; polls++) {
        status = fileXioDevctl(device, 0x2, NULL, 0, NULL, 0);
        if (status >= 0 && (status & 1) == 0)
            break; // the game's card is ready
        int seconds = polls * MMCE_CARD_READY_POLL_US / 1000000;
        if (mmceStartPressed(start)) {
            LOG("MMCE: START pressed after %d s of the card's wait; launching\n", seconds);
            diagLog("mmce: START after %d s of the card's wait; launching", seconds);
            return 1;
        }
        if (status >= 0)
            silent = 0;
        else if (++silent * (MMCE_CARD_READY_POLL_US / 1000) >= MMCE_CARD_READY_SILENT_MS) {
            LOG("MMCE: the card did not answer for %d ms during the wait; not confirmed\n",
                silent * (MMCE_CARD_READY_POLL_US / 1000));
            diagLog("mmce: the card's busy query failed during the wait");
            return -1;
        }
        if (seconds >= MMCE_CARD_READY_MAX_S) {
            LOG("MMCE: the game's memory card was still busy after %d s; launching\n", seconds);
            diagLog("mmce: the game's card still busy after %d s; launching", seconds);
            return 0;
        }
        snprintf(line, sizeof(line), _l(_STR_MMCE_GAMEID_CARD_WAIT), seconds);
        host::ui().renderTextScreen(line);
        mmceSleepReadingStart(MMCE_CARD_READY_POLL_US, start);
    }
    diagLog("mmce: the game's card ready after %d ms", polls * MMCE_CARD_READY_POLL_US / 1000);
    return 1;
}

int mmceSendGameID(const char *startup, const char *protectMcPath, int vmcSlotMask)
{
    return gMmceSupport.sendGameID(startup, protectMcPath, vmcSlotMask);
}

// For a launch whose GAME lives on the card that was just told to switch (the native MMCE path and the
// Neutrino MMCE leg): never hand the IOP over while the swap is still in flight. mmceSendGameID returns -1
// whenever it sent (it does not wait for the switch), and the handoff itself can land on a switch the firmware deferred (its
// own buttons held, a flush in progress). Both are bounded waits here, on the slot the send targeted:
// presence first (silence is the swap's first phase), then the busy bit, waited out on screen as in the
// send. A firmware without the busy query settles the moment presence answers, so it pays ~0 ms. The
// MX4SIO leg has had this since batch S7 (mmceGameIdSettle); the MMCE-hosted legs did not, and they are
// the ones that cannot survive the swap: the ISO the game streams is on the card being swapped.
// Returns 1 when it waited a swap out (the card was busy, or silent and then answering again), so the caller
// lets the card's file system answer again too (settleAfterSwitch); 0 when there was nothing to wait out, or
// when START cut the card's silence short. START is *start's (mmceStartPressed; NULL: this look alone): pressed in
// an earlier look of the same wait, it holds here, so this look still asks its card but waits nothing out (a card
// still loading returns 1 at once, as after START in its own wait; a silent one returns 0). A busy query that fails
// after the presence probe answered is not taken for a firmware without one on its own: the card can fall silent
// between the two queries, the start of its swap (mmceLacksBusyQuery).
int MmceSupport::waitSwapDoneOn(const char *device, const char *why, int *start)
{
    if (device == NULL || device[0] == '\0')
        return 0; // no slot resolved (Auto mode after the card was taken out): nothing to watch
    char line[256];
    int silent, own = 0;
    if (start == NULL)
        start = &own;
    for (silent = 0; silent * 200 < MMCE_CARD_READY_SILENT_MS; silent++) {
        if (fileXioDevctl(device, 0x1, NULL, 0, NULL, 0) != -1) {
            int busy = fileXioDevctl(device, 0x2, NULL, 0, NULL, 0);
            if (busy >= 0 && (busy & 1)) {
                LOG("MMCE %s: the card is still swapping; waiting\n", why);
                diagLog("mmce %s: card still busy; waiting", why);
                waitCardReady(device, start);
                return 1;
            }
            // Answering and idle, or answering with no busy query: nothing in flight now. A busy query that failed
            // otherwise (the card fell silent between the two queries) is counted as silence: looked at again below.
            if (busy >= 0 || mmceLacksBusyQuery(device)) {
                if (silent) {
                    LOG("MMCE %s: the card answers again after ~%d ms\n", why, silent * 200);
                    return 1;
                }
                return 0;
            }
        }
        // The card is silent, the first phase of its swap: on screen like the rest of the card's wait (a frame
        // left standing for up to MMCE_CARD_READY_SILENT_MS reads as a freeze), and START launches now. START
        // returns 0, so the handoff look does not settle a card that answers nothing (nor its file system).
        if (mmceStartPressed(start)) {
            LOG("MMCE %s: START pressed after ~%d ms of the card's silence; launching\n", why, silent * 200);
            diagLog("mmce %s: START after ~%d ms of the card's silence; launching", why, silent * 200);
            return 0;
        }
        snprintf(line, sizeof(line), _l(_STR_MMCE_GAMEID_CARD_WAIT), silent * 200 / 1000);
        host::ui().renderTextScreen(line);
        mmceSleepReadingStart(200 * 1000, start);
    }
    LOG("MMCE %s: the card did not answer for %d ms; launching anyway\n", why, silent * 200);
    diagLog("mmce %s: card silent for %d ms; launching anyway", why, silent * 200);
    return 0;
}

void MmceSupport::waitSwapDone(const char *why)
{
    if (gameIdTargetMask == 0)
        return; // no send this launch: nothing to wait for
    int start = 0;
    for (int s = 0; s < MMCE_MODE_COUNT; s++) {
        if ((gameIdTargetMask >> s) & 1)
            waitSwapDoneOn(s == 0 ? "mmce0:/" : "mmce1:/", why, &start);
    }
}

// The last look before the IOP handoff, always ending on the game's own slot: the ISO the game streams is on it,
// and a switch on it must be over, including a history-triggered one that began after waitHistorySwitch's window
// (Game ID off, the reported case) or one on this slot while a Game ID send went to the OTHER slot. Before that,
// when a send this launch went to the other slot, that slot too: a switch its firmware deferred must not be in
// flight either. ~0 ms on idle cards. On screen the looks are one wait, so one START press ends both.
int MmceSupport::waitBeforeHandoff(const char *gameDevice)
{
    int gameSlot = mmceSlotOf(gameDevice);
    int waited = 0, start = 0;
    for (int s = 0; s < MMCE_MODE_COUNT; s++) {
        if (((gameIdTargetMask >> s) & 1) && s != gameSlot) {
            if (waitSwapDoneOn(s == 0 ? "mmce0:/" : "mmce1:/", "before handoff, the Game ID slot", &start))
                waited = 1;
        }
    }
    if (gameDevice != NULL && gameDevice[0] != '\0' && waitSwapDoneOn(gameDevice, "before handoff", &start))
        waited = 1;
    return waited;
}

// After the OSD history write on an MMCE-hosted launch: watch the game's slot for the card's own folder
// switch (see the launch) and wait it out on screen. Nothing to watch when our Game ID send already
// switched this same card (it is on that folder; a send to the other slot does not cover this one), or when
// the firmware has no busy query (mmceLacksBusyQuery): then the window is not paid. Bounded: the window, then
// waitCardReady.
int MmceSupport::waitHistorySwitch(const char *device, const char *why)
{
    if (device == NULL || device[0] == '\0')
        return 0; // no slot resolved: nothing to watch
    int slot = mmceSlotOf(device);
    if (slot >= 0 && ((gameIdSwitchSeenMask >> slot) & 1)) {
        LOG("MMCE %s: the card already switched for the Game ID send; not waiting again\n", why);
        return 0;
    }
    char line[256];
    int polls, silent = 0, start = 0;
    for (polls = 0; polls * MMCE_HISTORY_SWITCH_POLL_MS < MMCE_HISTORY_SWITCH_WINDOW_MS; polls++) {
        int status = fileXioDevctl(device, 0x2, NULL, 0, NULL, 0);
        if (status >= 0 && (status & 1) == 0) {
            silent = 0; // answering and idle: the switch has not begun (or will not)
        } else if (status < 0 && ++silent < MMCE_GAMEID_QUERY_FAILURES) {
            // one failed query is retried, not trusted
        } else if (status < 0 && mmceLacksBusyQuery(device)) {
            LOG("MMCE %s: the card has no busy query; cannot watch for its switch\n", why);
            return 0;
        } else { // busy, silent, or its busy query answers again after failing: the switch began
            LOG("MMCE %s: the card is switching on its own after %d ms (%s)\n", why, polls * MMCE_HISTORY_SWITCH_POLL_MS,
                status < 0 ? "not answering" : "busy");
            diagLog("mmce %s: the card switching on its own after %d ms", why, polls * MMCE_HISTORY_SWITCH_POLL_MS);
            waitCardReady(device, &start);
            return 1;
        }
        if (mmceStartPressed(&start)) {
            LOG("MMCE %s: START pressed after %d ms; not watching for the card's switch\n", why, polls * MMCE_HISTORY_SWITCH_POLL_MS);
            return 0;
        }
        snprintf(line, sizeof(line), _l(_STR_MMCE_GAMEID_CARD_WAIT), polls * MMCE_HISTORY_SWITCH_POLL_MS / 1000);
        host::ui().renderTextScreen(line);
        mmceSleepReadingStart(MMCE_HISTORY_SWITCH_POLL_MS * 1000, &start);
    }
    LOG("MMCE %s: no card switch within %d ms\n", why, MMCE_HISTORY_SWITCH_WINDOW_MS);
    return 0;
}

// Bounded post-GameID settle for cross-device launches that share SIO2 with the MMCE (MX4SIO, batch S7).
// Polls every slot the last send targeted within the shared timeoutMs budget. Returns 0 settled, -1 expired.
// NEVER blocks the launch -- the caller toasts and proceeds.
int MmceSupport::gameIdSettle(int timeoutMs)
{
    if (gameIdTargetMask == 0)
        return 0;
    int busyMask = gameIdTargetMask;
    for (int waited = 0; waited <= timeoutMs && busyMask != 0; waited += 200) {
        busyMask = 0;
        for (int s = 0; s < MMCE_MODE_COUNT; s++) {
            if (!((gameIdTargetMask >> s) & 1))
                continue;
            const char *dev = (s == 0) ? "mmce0:/" : "mmce1:/";
            if (fileXioDevctl(dev, 0x1, NULL, 0, NULL, 0) == -1) {
                busyMask |= (1 << s);
                continue;
            }
            int busy = fileXioDevctl(dev, 0x2, NULL, 0, NULL, 0);
            if (!((busy >= 0 && (busy & 1) == 0) || (busy < 0 && mmceLacksBusyQuery(dev))))
                busyMask |= (1 << s);
        }
        if (busyMask == 0)
            return 0;
        DelayThread(200 * 1000);
    }
    LOG("MMCE GameID settle NOT confirmed after %d ms (pending mask 0x%x)\n", timeoutMs, busyMask);
    return -1;
}

int mmceGameIdSettle(int timeoutMs)
{
    return gMmceSupport.gameIdSettle(timeoutMs);
}

int MmceSupport::resetCards(void)
{
    int resetCount = 0;

    gameIdTargetMask = 0;
    gameIdSwitchSeenMask = 0;

    if (!modLoaded)
        loadModules();

    if (!modLoaded)
        return 0;

    for (int slot = 0; slot < MMCE_MODE_COUNT; slot++) {
        const char *dev = (slot == 0) ? "mmce0:/" : "mmce1:/";
        if (fileXioDevctl(dev, 0x1, NULL, 0, NULL, 0) != -1) {
            LOG("MMCE: sending reset (0x9) to %s\n", dev);
            fileXioDevctl(dev, 0x9, NULL, 0, NULL, 0);

            for (int i = 0; i < MMCE_GAMEID_WAIT_TICKS; i++) {
                int status = fileXioDevctl(dev, 0x2, NULL, 0, NULL, 0);
                if (status < 0 || (status & 1) == 0)
                    break;

                DelayThread(MMCE_GAMEID_POLL_US);
            }
            resetCount++;
        }
    }

    return resetCount;
}

int mmceReset(void)
{
    return gMmceSupport.resetCards();
}

// Fs-settle after a GameID card switch. The 0x8 devctl physically re-mounts the card, and on Gen2
// the busy bit can clear before the FILESYSTEM surface is back. Probe
// the switched slot until a directory open answers: poll-first so a fast card costs ~0 ms; bounded
// (~5 s) so a dead card can't hang.
void MmceSupport::settleDeviceFs(const char *mmceRoot)
{
    if (mmceRoot == NULL || mmceRoot[0] == '\0')
        return;
    if (fileXioDevctl(mmceRoot, 0x1, NULL, 0, NULL, 0) == -1)
        return;
    int settled = 0, settle;
    for (settle = 0; settle < 25; settle++) {
        int dfd = fileXioDopen(mmceRoot);
        if (dfd >= 0) {
            fileXioDclose(dfd);
            settled = 1;
            break;
        }
        DelayThread(200 * 1000);
    }
    if (settled)
        LOG("MMCE settle: %s fs surface up after ~%d ms\n", mmceRoot, settle * 200);
    else
        LOG("MMCE settle: %s fs surface not back within ~5000 ms; launching anyway\n", mmceRoot);
}

void MmceSupport::settleAfterSwitch(const char *gameDevice)
{
    int settledMask = 0;
    for (int s = 0; s < MMCE_MODE_COUNT; s++) {
        if ((gameIdTargetMask >> s) & 1) {
            settleDeviceFs(s == 0 ? "mmce0:/" : "mmce1:/");
            settledMask |= (1 << s);
        }
    }
    int gameSlot = mmceSlotOf(gameDevice);
    if (gameSlot >= 0 && !((settledMask >> gameSlot) & 1))
        settleDeviceFs(gameDevice);
}

void MmceSlotState::getDeviceRoot(char *root, size_t size) const
{
    if (root == NULL || size == 0)
        return;
    snprintf(root, size, "mmce%d:/", slotIndex);
}

void MmceSlotState::refreshArtRoots(void)
{
    int len;

    artPrimary[0] = '\0';

    if (prefix[0] == '\0')
        return;

    /* Ensure prefix always ends with '/' so path concatenation is correct
     * (e.g. "mmce0:/CD" -> "mmce0:/CD/" prevents "mmce0:/CDART" paths). */
    len = strlen(prefix);
    if (len < (int)sizeof(prefix) - 1 && prefix[len - 1] != '/') {
        prefix[len] = '/';
        prefix[len + 1] = '\0';
    }

    snprintf(artPrimary, sizeof(artPrimary), "%s", prefix);
}

void MmceSlotState::resetState(int slot)
{
    slotIndex = slot;
    prefix[0] = '\0';
    artPrimary[0] = '\0';
    ulSizePrev = -2;
    isoScanRetries = 0;
    modifiedCDPrev = 0;
    modifiedDVDPrev = 0;
    if (games != NULL) {
        free(games);
        games = NULL;
    }
    gameCount = 0;
    if (ps1Games != NULL) {
        free(ps1Games);
        ps1Games = NULL;
    }
    ps1GameCount = 0;
    ps1Scanned = 0;
    ps1ScanFailed = 0;
    ps1ScanRetries = 0;
    foldersCreatedFor[0] = '\0';
    folderRetries = 0;
    present = 0;
    themesLoaded = 0;
    languagesLoaded = 0;
}

void MmceSlotState::probePresence(void)
{
    char root[8];
    getDeviceRoot(root, sizeof(root));

    if (present) {
        // Debounced presence check (#154): invalidate only after MMCE_PRESENCE_PROBE_MAX
        // consecutive failed devctls. One success = present.
        int probe;
        for (probe = 0; probe < MMCE_PRESENCE_PROBE_MAX; probe++) {
            if (fileXioDevctl(root, 0x1, NULL, 0, NULL, 0) != -1)
                break;
            if (probe + 1 < MMCE_PRESENCE_PROBE_MAX)
                DelayThread(MMCE_PRESENCE_RETRY_US);
        }
        if (probe >= MMCE_PRESENCE_PROBE_MAX) {
            present = 0;
            prefix[0] = '\0';
        } else {
            // Still present. Rebuild prefix from CURRENT gMMCEPrefix so prefix changes apply immediately.
            snprintf(prefix, sizeof(prefix), "mmce%d:/%s", slotIndex, gMMCEPrefix);
        }
    } else {
        int attempt, attempts = everResolved ? MMCE_PRESENCE_PROBE_MAX : 1;
        for (attempt = 0; attempt < attempts; attempt++) {
            if (fileXioDevctl(root, 0x1, NULL, 0, NULL, 0) != -1) {
                present = 1;
                everResolved = 1;
                snprintf(prefix, sizeof(prefix), "mmce%d:/%s", slotIndex, gMMCEPrefix);
                break;
            }
            if (attempt + 1 < attempts)
                DelayThread(MMCE_PRESENCE_RETRY_US);
        }
        if (!present)
            prefix[0] = '\0';
    }

    refreshArtRoots();
}

static int mmceTryLoadImage(const char *prefix, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex)
{
    char path[256];

    if ((prefix == NULL || prefix[0] == '\0') && isRelative)
        return -1;

    if (isRelative)
        snprintf(path, sizeof(path), "%s%s/%s_%s", prefix, folder, value, suffix);
    else
        snprintf(path, sizeof(path), "%s%s_%s", folder, value, suffix);

    return texDiscoverArt(resultTex, path);
}

void MmceSupport::loadModules(void)
{
    // mmceman is a singleton, and this is called from several places -- MmceSource::init, the BDMA equip
    // (to wake an MMCE source when MMCE games are off or Manual-not-started), and the boot-dir
    // resolver -- so it has to be idempotent.
    //
    // It used to set the flag BEFORE the load, to stop a partial load creating a second instance.
    // That protection already exists one layer down: sysLoadModuleBuffer keeps a table of buffers
    // it has loaded, returns 0 for one it already holds, never records a failure, and consults that
    // table under sysLoadModuleLock. Guarding it a second time here bought nothing and cost the
    // retry -- a single failed load left mmceman marked resident for the whole session, so MMCE
    // games and the GameID transport stayed silently dead until a reboot.
    //
    // Latch on SUCCESS instead. A retry after a failure genuinely re-attempts; a retry after a
    // success is a cheap no-op; and a racing second caller is serialised by that same lock, so no
    // in-progress flag is needed either.
    if (modLoaded)
        return;
    LOG("MMCESUPPORT LoadModules\n");
    LOG("[MMCEMAN]:\n");
    if (sysLoadModuleBuffer(&mmceman_irx, size_mmceman_irx, 0, NULL) == 0)
        modLoaded = 1;
    else
        LOG("MMCESUPPORT mmceman load FAILED -- retry stays armed\n");
}

void mmceLoadModules(void)
{
    gMmceSupport.loadModules();
}

int mmceModulesLoaded(void)
{
    return gMmceSupport.modLoaded != 0;
}

// Δ4 (NHDDL parity): arm the GameID transport OUTSIDE the launch path. NHDDL loads mmceman once at
// boot; RiptOPL used to self-arm inside sendGameID -- an IRX load/start at the launch's most
// fragile moment (issue #51's fix, right intent, wrong timing). Called from initAllSupport (boot +
// every settings apply) via the IO worker, so a wedged load is a harmless LOG at menu time instead
// of a dead launch. Idempotent (derived from modLoaded); no-op when the GameID feature is off.
void MmceSupport::armGameIDTransport(void)
{
    if (!gMMCEEnableGameID || modLoaded)
        return;

    host::ui().bootStatusSticky(_l(_STR_BOOT_ARMING_MMCE)); // boot-step localizer (IO thread) -- see gui.c
    loadModules();
    // Post-load marker (#254): the boot arm runs right after GUI_INIT_DONE; a serial log that
    // shows the arm begin without this completion line localizes a wedge to the mmceman load.
    LOG("MMCESUPPORT GameID transport armed\n");
}

void mmceArmGameIDTransport(void)
{
    gMmceSupport.armGameIDTransport();
}
class MmceSource: public GameSource
{
public:
    explicit MmceSource(int slot)
        : GameSource(mmceSlotToMode(slot), 2, 0, MENU_MIN_INACTIVE_FRAMES, MMCE_MODE_UPDATE_DELAY, GameSource::CAP_LAUNCH_VCD | GameSource::CAP_LAUNCH_CUE | GameSource::CAP_ROW_VIEW | GameSource::CAP_SOURCE_ID | GameSource::CAP_SAVE_CUE_SETTINGS)
    {
        gMmceSupport.slots[slot].slotIndex = slot;
        this->priv = &gMmceSupport.slots[slot];
    }

    void init(item_list_t *itemList) override;
    int needsUpdate(item_list_t *itemList) override;
    int update(item_list_t *itemList) override;
    int count(item_list_t *itemList) override;
    int rowView(item_list_t *itemList, int id) override;
    int sourceId(item_list_t *itemList, int id) override;
    int saveCueSettings(item_list_t *itemList, int id, const char *name, config_set_t *configSet) override;
    int ps1SaveDir(item_list_t *itemList, int id, const char *name, int *ember, char *directory, int directorySize,
                   char *partition, int partitionSize) override;
    void *game(item_list_t *itemList, int id) override;
    char *name(item_list_t *itemList, int id) override;
    int nameLength(item_list_t *itemList, int id) override;
    char *startup(item_list_t *itemList, int id) override;
    void deleteGame(item_list_t *itemList, int id) override;
    void rename(item_list_t *itemList, int id, char *newName) override;
    void launchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet) override;
    void launchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet) override;
    void launch(item_list_t *itemList, int id, config_set_t *configSet) override;
    config_set_t *config(item_list_t *itemList, int id) override;
    void ps1Size(item_list_t *itemList, base_game_info_t *game, config_set_t *config) override;
    int image(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm) override;
    int textId(item_list_t *itemList) override;
    int iconId(item_list_t *itemList) override;
    void cleanUp(item_list_t *itemList, int exception) override;
    void shutdown(item_list_t *itemList) override;
    int checkVmc(item_list_t *itemList, char *name, int createSize) override;
    char *prefix(item_list_t *itemList) override;
};

static MmceSource *mmceInstance(int slot)
{
    static MmceSource *instances[MMCE_MODE_COUNT] = {NULL, NULL};
    alignas(MmceSource) static unsigned char storage[MMCE_MODE_COUNT][sizeof(MmceSource)];

    int idx = (slot == 1) ? 1 : 0;
    if (instances[idx] == NULL)
        instances[idx] = new (storage[idx]) MmceSource(idx);
    return instances[idx];
}

static MmceSlotState *mmceSlotStateOf(item_list_t *itemList)
{
    if (itemList != NULL && itemList->priv != NULL)
        return static_cast<MmceSlotState *>(itemList->priv);
    int idx = (itemList != NULL) ? mmceModeSlot(itemList->mode) : 0;
    return &gMmceSupport.slots[idx];
}

static void mmceUpdateVisibility(item_list_t *itemList, const MmceSlotState *slot, int notifyMenu)
{
    if (itemList == NULL || itemList->owner == NULL || slot == NULL)
        return;

    opl_io_module_t *pOwner = static_cast<opl_io_module_t *>(itemList->owner);
    int shouldBeVisible = 0;

    if (gMMCEStartMode == START_MODE_DISABLED) {
        shouldBeVisible = 0;
    } else if (gMMCEStartMode == START_MODE_MANUAL && !itemList->enabled) {
        shouldBeVisible = (slot->slotIndex == 0) ? 1 : 0;
    } else {
        shouldBeVisible = (slot->prefix[0] != '\0') ? 1 : 0;
        // In Manual mode after start, if neither slot has a card, keep slot 0 visible (empty)
        // so the user's Manual tab does not vanish completely.
        if (!shouldBeVisible && gMMCEStartMode == START_MODE_MANUAL && slot->slotIndex == 0 &&
            gMmceSupport.slots[1].prefix[0] == '\0')
            shouldBeVisible = 1;
    }

    if (pOwner->menuItem.visible != shouldBeVisible) {
        pOwner->menuItem.visible = shouldBeVisible;
        if (notifyMenu) {
            host::app().moduleUpdateMenu(itemList->mode, 0, 0);
            if (gAllGames)
                loadFavourites();
        }
    }
}

void MmceSource::init(item_list_t *itemList)
{
    int slotIdx = mmceModeSlot(itemList->mode);
    MmceSlotState *slot = mmceSlotStateOf(itemList);

    LOG("MMCESUPPORT Init (slot %d)\n", slotIdx);
    slot->resetState(slotIdx);

    itemList->delay = gArtDelay;
    itemList->updateDelay = MMCE_MODE_UPDATE_DELAY;

    gMmceSupport.loadModules();
    slot->probePresence();

    itemList->enabled = 1;

    // When Manual mode is started from the entry tab (slot 0), start slot 1 as well so both
    // MMCE cards appear on their own pages without requiring a second manual trigger.
    if (gMMCEStartMode == START_MODE_MANUAL && slotIdx == 0) {
        item_list_t *slot1 = mmceInstance(1);
        if (slot1->owner != NULL && !slot1->enabled) {
            gsOf(slot1)->init(slot1);
            host::app().moduleUpdateMenu(slot1->mode, 0, 0);
            ioPutRequest(IO_MENU_UPDATE_DEFFERED, &slot1->mode);
        }
    }

    mmceUpdateVisibility(itemList, slot, 0);
}

item_list_t *mmceGetSlotObject(int slot, int initOnly)
{
    MmceSource *inst = mmceInstance(slot);
    if (initOnly && !inst->enabled)
        return NULL;
    return inst;
}

item_list_t *mmceGetObject(int initOnly)
{
    return mmceGetSlotObject(0, initOnly);
}

void mmceEnumerateDevices(int forceReinit)
{
    for (int s = 0; s < MMCE_MODE_COUNT; s++)
        host::app().initSupport(mmceGetSlotObject(s, 0), mmceSlotToMode(s), forceReinit);

    // Refresh visibility after both slots have probed so Manual-mode fallback sees both slots.
    for (int s = 0; s < MMCE_MODE_COUNT; s++) {
        item_list_t *list = mmceGetSlotObject(s, 0);
        mmceUpdateVisibility(list, &gMmceSupport.slots[s], 0);
    }
}

int MmceSource::needsUpdate(item_list_t *itemList)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    char path[256];
    int result = 0;
    struct stat st;

    slot->probePresence();
    if (slot->slotIndex == 0) {
        MmceSlotState *peer = &gMmceSupport.slots[1];
        int wasPresent = peer->present;
        peer->probePresence();
        item_list_t *peerList = mmceInstance(1);
        if (peerList->enabled && wasPresent != peer->present)
            ioPutRequestUnlessWaiting(IO_MENU_UPDATE_DEFFERED, &peerList->mode);
    }
    // Recompute both slots so the Manual-mode slot-0 fallback updates whenever slot 1 appears or disappears.
    for (int s = 0; s < MMCE_MODE_COUNT; s++) {
        item_list_t *list = mmceInstance(s);
        mmceUpdateVisibility(list, &gMmceSupport.slots[s], 1);
    }

    if (slot->prefix[0] == '\0') {
        itemList->updateDelay = MMCE_MODE_UPDATE_DELAY;
        slot->foldersCreatedFor[0] = '\0'; // card gone: recreate folders on the next (possibly different) card
        slot->folderRetries = 0;
        slot->ps1Scanned = 0;
        slot->ps1ScanFailed = 0;
        slot->ps1ScanRetries = 0;
        slot->isoScanRetries = 0;
        slot->themesLoaded = 0;
        slot->languagesLoaded = 0;
        return (slot->gameCount > 0 || slot->ps1GameCount > 0);
    }

    itemList->updateDelay = MENU_UPD_DELAY_NOUPDATE;

    // Register the card's THM/LNG dirs BEFORE the VCD-view early returns (#152).
    if (!slot->themesLoaded) {
        snprintf(path, sizeof(path), "%sTHM", slot->prefix);
        if (thmAddElements(path, "/", 1) > 0)
            slot->themesLoaded = 1;
    }
    if (!slot->languagesLoaded) {
        snprintf(path, sizeof(path), "%sLNG", slot->prefix);
        if (lngAddLanguages(path, "/", itemList->mode) > 0)
            slot->languagesLoaded = 1;
    }

    // VCD view: force a rescan once on toggle, then skip the disc heuristics while showing VCDs.
    if (host::library().consumeDirty(itemList->mode)) {
        slot->ps1ScanRetries = 0;
        slot->isoScanRetries = 0;
        slot->folderRetries = 0; // Refresh also retries a still-missing CFG folder.
        result = 1;
    }
    // Folder browsing: descend/ascend forces one rescan (consumed before the NOUPDATE latch below).
    if (folderConsumeDirty(itemList->mode))
        return 1;
    if (host::library().viewActive(itemList->mode) == LIB_VIEW_PS1 || host::library().viewActive(itemList->mode) == LIB_VIEW_MIXED) {
        if (!slot->ps1Scanned && !slot->ps1ScanFailed) {
            itemList->updateDelay = MMCE_MODE_UPDATE_DELAY;
            result = 1;
        }
        if (slot->ps1ScanFailed && slot->ps1ScanRetries < MMCE_VCD_SCAN_RETRY_MAX) {
            slot->ps1ScanRetries++;
            itemList->updateDelay = MMCE_MODE_UPDATE_DELAY;
            result = 1;
        }
        if (host::library().viewActive(itemList->mode) == LIB_VIEW_PS1)
            return result;
    }
    /* A Favourites-triggered retry can be queued while this page shows ISO. */
    if (slot->ps1ScanFailed && slot->ps1ScanRetries < MMCE_VCD_SCAN_RETRY_MAX &&
        host::library().viewActive(itemList->mode) != LIB_VIEW_PS1 &&
        host::library().viewActive(itemList->mode) != LIB_VIEW_MIXED) {
        slot->ps1ScanRetries++;
        itemList->updateDelay = MMCE_MODE_UPDATE_DELAY;
        result = 1;
    }

    // A card whose ISO list has not been read yet is rescanned, but only so many times: with CD and
    // DVD both unreadable the list never gets read, and an unbounded retry rebuilt this page, Apps,
    // Favourites and All Games every ~2 s for as long as the card stayed in (#565). The card stays
    // polled after that, so the folders appearing (their dates below), a removal, Refresh, L3 or a
    // card change still retries it, as on BDM (#186); only the rebuilds stop.
    if (slot->ulSizePrev != -2)
        slot->isoScanRetries = 0;
    else {
        itemList->updateDelay = MMCE_MODE_UPDATE_DELAY;
        if (slot->isoScanRetries < MMCE_VCD_SCAN_RETRY_MAX) {
            slot->isoScanRetries++;
            result = 1;
        }
    }

    snprintf(path, sizeof(path), "%sCD", slot->prefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;

    if (slot->modifiedCDPrev != st.st_mtime) {
        slot->modifiedCDPrev = st.st_mtime;
        result = 1;
    }

    snprintf(path, sizeof(path), "%sDVD", slot->prefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;

    if (slot->modifiedDVDPrev != st.st_mtime) {
        slot->modifiedDVDPrev = st.st_mtime;
        result = 1;
    }

    if (slot->ulSizePrev != -2 && !sbIsSameSize(slot->prefix, slot->ulSizePrev))
        result = 1; // (-2 never matches a size: the budget above is its only retry)

    if (slot->folderRetries < MMCE_FOLDER_RETRY_MAX && strcmp(slot->foldersCreatedFor, slot->prefix) != 0) {
        sbCreateFolders(slot->prefix, 1);

        char cfgPath[sizeof(slot->prefix) + 4];
        snprintf(cfgPath, sizeof(cfgPath), "%sCFG", slot->prefix);
        DIR *cfgDir = opendir(cfgPath);
        if (cfgDir != NULL) {
            closedir(cfgDir);
            snprintf(slot->foldersCreatedFor, sizeof(slot->foldersCreatedFor), "%s", slot->prefix);
            slot->folderRetries = 0;
        } else if (++slot->folderRetries >= MMCE_FOLDER_RETRY_MAX) {
            LOG("MMCE: %s still absent after %d create attempts; waiting for a refresh\n",
                cfgPath, MMCE_FOLDER_RETRY_MAX);
        } else {
            LOG("MMCE: %s not present after sbCreateFolders -- re-arming retry (%d/%d)\n",
                cfgPath, slot->folderRetries, MMCE_FOLDER_RETRY_MAX);
            itemList->updateDelay = MMCE_MODE_UPDATE_DELAY;
            result = 1;
        }
    }

    return result;
}

int MmceSource::update(item_list_t *itemList)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    int view = host::library().listViewActive(itemList);
    int result = 0;
    if (slot->prefix[0] == '\0') {
        if (slot->games != NULL) {
            free(slot->games);
            slot->games = NULL;
        }
        if (slot->ps1Games != NULL) {
            free(slot->ps1Games);
            slot->ps1Games = NULL;
        }
        slot->gameCount = 0;
        slot->ps1GameCount = 0;
        slot->ulSizePrev = -2; // force a fresh scan when a card returns
        slot->ps1Scanned = 0;
        slot->ps1ScanFailed = 0;
        slot->ps1ScanRetries = 0;
        return 0;
    }

    if (view == LIB_VIEW_PS1 || view == LIB_VIEW_MIXED || slot->ps1ScanFailed) {
        char ps1Root[sizeof(slot->prefix)];
        slot->getDeviceRoot(ps1Root, sizeof(ps1Root));
        int r = ps1FillGameList(ps1Root, &slot->ps1Games, slot->ps1GameCount);
        if (r >= 0) {
            slot->ps1Scanned = 1;
            slot->ps1GameCount = r;
            slot->ps1ScanFailed = ps1GameListNeedsRetry();
        } else {
            slot->ps1Scanned = 0;
            slot->ps1ScanFailed = 1;
        }
        if (!slot->ps1ScanFailed)
            slot->ps1ScanRetries = 0;
        result += slot->ps1GameCount;
    }
    if (view == LIB_VIEW_ISO || view == LIB_VIEW_MIXED) {
        sbListCacheFirstRead(itemList->mode, GLC_MMCE);
        sbReadList(&slot->games, slot->prefix, folderGetSub(itemList->mode), &slot->ulSizePrev, &slot->gameCount);
        result += slot->gameCount;
    }
    return result;
}

// Favourites resolve PS1 titles even while this card's visible page is ISO.
// Populate the backing list directly instead of making resolution depend on a
// prior PS1-view scan, and keep the existing failed-scan retry budget.
int mmceEnsurePs1List(item_list_t *itemList)
{
    if (itemList == NULL)
        return 0;
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    if (slot->prefix[0] == '\0')
        return 0;
    if (slot->ps1Scanned && !slot->ps1ScanFailed)
        return 1;
    if (slot->ps1ScanFailed && slot->ps1ScanRetries >= MMCE_VCD_SCAN_RETRY_MAX)
        return 0;
    if (slot->ps1ScanFailed && slot->ps1Games != NULL) {
        /* A source menu may still hold pointers into this partial list. Let its
         * normal deferred rebuild retire those consumers before replacing it. */
        ioPutRequestUnlessWaiting(IO_MENU_UPDATE_DEFFERED, &itemList->mode);
        return 0;
    }

    char root[sizeof(slot->prefix)];
    slot->getDeviceRoot(root, sizeof(root));
    int result = ps1FillGameList(root, &slot->ps1Games, slot->ps1GameCount);
    if (result < 0) {
        itemList->updateDelay = MMCE_MODE_UPDATE_DELAY;
        ioPutRequestUnlessWaiting(IO_MENU_UPDATE_DEFFERED, &itemList->mode);
        slot->ps1Scanned = 0;
        slot->ps1ScanFailed = 1;
        slot->ps1ScanRetries++;
        return 0;
    }
    slot->ps1Scanned = 1;
    slot->ps1GameCount = result;
    slot->ps1ScanFailed = ps1GameListNeedsRetry();
    if (slot->ps1ScanFailed)
        slot->ps1ScanRetries++;
    else
        slot->ps1ScanRetries = 0;
    return 1;
}

int MmceSource::count(item_list_t *itemList)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    // Detached rows can still belong to the old submenu until the deferred rebuild.
    if (slot->prefix[0] == '\0')
        return 0;
    int view = host::library().listViewActive(itemList);
    return view == LIB_VIEW_MIXED ? slot->gameCount + slot->ps1GameCount :
           view == LIB_VIEW_PS1   ? slot->ps1GameCount :
                                    slot->gameCount;
}

int MmceSource::rowView(item_list_t *itemList, int id)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    int view = host::library().listViewActive(itemList);
    if (view == LIB_VIEW_MIXED)
        return id >= 0 && id < slot->gameCount ? LIB_VIEW_ISO : LIB_VIEW_PS1;
    return view;
}

int MmceSource::sourceId(item_list_t *itemList, int id)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    return host::library().listViewActive(itemList) == LIB_VIEW_MIXED && id >= slot->gameCount ? id - slot->gameCount : id;
}

base_game_info_t *MmceSlotState::activeGame(item_list_t *itemList, int id)
{
    MmceSource *src = mmceInstance(slotIndex);
    int vcd = (src->rowView(itemList, id) == LIB_VIEW_PS1);
    id = src->sourceId(itemList, id);
    base_game_info_t *arr = vcd ? ps1Games : games;
    int count = vcd ? ps1GameCount : gameCount;
    if (arr == NULL || id < 0 || id >= count)
        return &emptyGame;
    return &arr[id];
}

void *MmceSource::game(item_list_t *itemList, int id)
{
    return (void *)mmceSlotStateOf(itemList)->activeGame(itemList, id);
}

char *MmceSource::name(item_list_t *itemList, int id)
{
    return mmceSlotStateOf(itemList)->activeGame(itemList, id)->name;
}

int MmceSource::nameLength(item_list_t *itemList, int id)
{
    base_game_info_t *g = mmceSlotStateOf(itemList)->activeGame(itemList, id);
    return ((g->format != GAME_FORMAT_USBLD) ? ISO_GAME_NAME_MAX + 1 : UL_GAME_NAME_MAX + 1);
}

char *MmceSource::startup(item_list_t *itemList, int id)
{
    base_game_info_t *g = mmceSlotStateOf(itemList)->activeGame(itemList, id);
    if (rowView(itemList, id) == LIB_VIEW_PS1)
        return g->name;
    return g->startup;
}

void MmceSource::deleteGame(item_list_t *itemList, int id)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    if (rowView(itemList, id) == LIB_VIEW_PS1)
        return; // #120: a VCD is not an ISO game -- no delete in VCD view
    if (slot->activeGame(itemList, id) == &slot->emptyGame)
        return;
    sbSetBrowseSub(folderGetSub(itemList->mode));
    id = sourceId(itemList, id);
    sbDelete(&slot->games, slot->prefix, "/", slot->gameCount, id);
    slot->ulSizePrev = -2;
    slot->isoScanRetries = 0; // a fresh rescan, even if the last retry budget ran out
}

void MmceSource::rename(item_list_t *itemList, int id, char *newName)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    if (rowView(itemList, id) == LIB_VIEW_PS1) {
        base_game_info_t *game = slot->activeGame(itemList, id);

        if (game == &slot->emptyGame)
            return;
        char ps1Root[sizeof(slot->prefix)];
        slot->getDeviceRoot(ps1Root, sizeof(ps1Root));
        int renamed = cueIsCueEntry(game) ? cueRenameGame(ps1Root, game->name, newName) : vcdRenameFile(ps1Root, game->name, newName);
        if (renamed == 0) {
            slot->ps1Scanned = 0;
            slot->ps1ScanFailed = 0;
            slot->ps1ScanRetries = 0;
            itemList->updateDelay = MMCE_MODE_UPDATE_DELAY;
        }
        return;
    }
    if (slot->activeGame(itemList, id) == &slot->emptyGame)
        return;
    id = sourceId(itemList, id);
    sbSetBrowseSub(folderGetSub(itemList->mode));
    sbRename(&slot->games, slot->prefix, "/", slot->gameCount, id, newName);
    slot->ulSizePrev = -2;
    slot->isoScanRetries = 0; // a fresh rescan, even if the last retry budget ran out
}

void MmceSource::launchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    char emberElf[256], biosPath[288];
    char launchName[CUE_NAME_MAX];
    char ps1Root[sizeof(slot->prefix)];

    if (cueName == NULL || cueName[0] == '\0')
        return;
    if (!cueNameLaunchable(cueName)) {
        host::ui().msgBox(_l(_STR_EMBER_BAD_NAME), 0, NULL);
        return;
    }

    host::ui().renderTextScreen(_l(_STR_PLEASE_WAIT));

    if (!cacheAbortMmceImageLoadsTimed(MMCE_ART_ABORT_WAIT_TICKS)) {
        host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
        return;
    }

    slot->getDeviceRoot(ps1Root, sizeof(ps1Root));

    if (!cueResolveEmber(ps1Root, emberElf, sizeof(emberElf))) {
        host::ui().missingFile(_l(_STR_EMBER_NOT_FOUND), emberElf);
        return;
    }
    if (!cueResolveEmberBios(ps1Root, biosPath, sizeof(biosPath))) {
        host::ui().missingFile(_l(_STR_EMBER_BIOS_MISSING), biosPath);
        return;
    }
    if (!cuePrepareLaunch(ps1Root, cueName, emberElf, launchName, sizeof(launchName)))
        return;
    cueApplySettings(ps1Root, cueName, configSet);

    if (!cueGameHasImage(ps1Root, cueName)) {
        host::ui().msgBox(_l(_STR_EMBER_NO_DISC), 0, NULL);
        return;
    }

    cuePrepareRetroGemBarcode(ps1Root, cueName);

    // UNMOUNT_EXCEPTION is load-bearing: Ember cannot remount the card it reads the game from.
    host::app().deinit(UNMOUNT_EXCEPTION, itemList->mode);
    sysLaunchEmber(emberElf, launchName);
}

void MmceSource::launchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    char vcdElf[256], vcdSelector[320];
    char ps1Root[sizeof(slot->prefix)];

    if (vcdName == NULL || vcdName[0] == '\0' || !strcasecmp(vcdName, "POPSTARTER"))
        return;

    host::ui().renderTextScreen(_l(_STR_PLEASE_WAIT));

    if (!cacheAbortMmceImageLoadsTimed(MMCE_ART_ABORT_WAIT_TICKS)) {
        host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
        return;
    }

    // The BDMA driver reopens the bare mass: selector on the first present MMCE.
    if (slot->slotIndex == 1) {
        MmceSlotState &primary = gMmceSupport.slots[0];
        primary.probePresence();
        if (primary.present) {
            host::ui().msgBox(_l(_STR_POPSTARTER_MMCE_TWO_CARDS), 0, NULL);
            return;
        }
    }

    slot->getDeviceRoot(ps1Root, sizeof(ps1Root));

    if (!vcdResolvePopstarter(ps1Root, vcdElf, sizeof(vcdElf))) {
        vcdDescribePopstarterLookup(ps1Root, vcdElf, sizeof(vcdElf));
        host::ui().missingFile(_l(_STR_POPSTARTER_NOT_FOUND), vcdElf);
        return;
    }
    if (!vcdConfirmPopsFiles(ps1Root))
        return;
    vcdBuildSelector(ps1Root, VCD_PREFIX_MASS, vcdName, vcdSelector, sizeof(vcdSelector));

    (void)vcdInstallPopstarterMc(ps1Root);

    vcdEnsureBdmaForLaunch(VCD_BDMA_SRC_MMCE, VCD_BDMA_MMCE);

    char vcdFullPath[256];
    snprintf(vcdFullPath, sizeof(vcdFullPath), "%sPOPS/%s.VCD", ps1Root, vcdName);
    if (!vcdConfirmCleanLaunch(ps1Root))
        return;
    vcdPrepareRetroGemBarcode(vcdFullPath);
    host::app().deinitEx(sbLoaderDeinitException(vcdElf), itemList->mode, host::app().pathToMode(vcdElf));
    sysLaunchPopstarter(vcdElf, vcdSelector);
}

// Shared with udpfssupport.cpp: both write their VMC slots into the one mmce_mcemu image.
int mmceMcemuSlots[2] = {-2, -2};

// Open any configured VMC not yet opened and patch its descriptor into mmce_mcemu_irx.
// Idempotent per slot (already-opened vmc_fds[i] >= 0 are left intact), so the post-recovery
// path below can retry slots that failed while the card was mid-swap.
static void mmcePrepareVmcSlots(const char *prefix, config_set_t *configSet, int vmc_fds[2], int *size_mcemu_irx)
{
    char vmc_name[32];
    char vmc_path[256];
    mmce_vmc_infos_t mmce_vmc_infos;
    vmc_superblock_t vmc_superblock;

    for (int vmc_id = 0; vmc_id < 2; vmc_id++) {
        if (vmc_fds[vmc_id] >= 0)
            continue;

        memset(&mmce_vmc_infos, 0, sizeof(mmce_vmc_infos));
        configGetVMC(configSet, vmc_name, sizeof(vmc_name), vmc_id);
        if (vmc_name[0]) {
            int vmc_size_mb = sysCheckVMC(prefix, "/", vmc_name, 0, &vmc_superblock);
            if (vmc_size_mb > 0) {
                mmce_vmc_infos.flags = vmc_superblock.mc_flag & 0xFF;
                mmce_vmc_infos.flags |= 0x100;
                mmce_vmc_infos.specs.page_size = vmc_superblock.page_size;
                mmce_vmc_infos.specs.block_size = vmc_superblock.pages_per_block;
                mmce_vmc_infos.specs.card_size = vmc_superblock.pages_per_cluster * vmc_superblock.clusters_per_card;

                snprintf(vmc_path, sizeof(vmc_path), "%sVMC/%s.bin", prefix, vmc_name);

                int vmc_fd = fileXioOpen(vmc_path, 0x3, 0666);
                if (vmc_fd >= 0) {
                    int vmc_handle = fileXioIoctl2(vmc_fd, 0x80, NULL, 0, NULL, 0);
                    if (vmc_handle >= 0) {
                        vmc_fds[vmc_id] = vmc_fd;
                        mmce_vmc_infos.fd = vmc_handle;
                        mmce_vmc_infos.active = 1;
                    } else {
                        fileXioClose(vmc_fd);
                        host::ui().warningId(_STR_VMC_FILE_ERROR, 6);
                    }
                }
            }
        }

        int slotWord = nativeVmcSlotWord(&mmce_mcemu_irx, size_mmce_mcemu_irx, vmc_id, mmceMcemuSlots, sizeof(mmce_vmc_infos_t));
        if (slotWord >= 0) {
            if (mmce_vmc_infos.active)
                *size_mcemu_irx = size_mmce_mcemu_irx;
            memcpy(&((u32 *)&mmce_mcemu_irx)[slotWord], &mmce_vmc_infos, sizeof(mmce_vmc_infos_t));
        }
    }
}

void MmceSource::launch(item_list_t *itemList, int id, config_set_t *configSet)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    int index, compatmask = 0;
    int EnablePS2Logo = 0;
    int result;

    char partname[256], filename[32], slotRoot[8];
    base_game_info_t *game;
    struct cdvdman_settings_mmce *settings;
    u32 layer1_start, layer1_offset;
    unsigned short int layer1_part;

    slot->getDeviceRoot(slotRoot, sizeof(slotRoot));

    // No Autolaunch yet
    if (gAutoLaunchBDMGame == NULL) {
        game = slot->activeGame(itemList, id);
        if (game == &slot->emptyGame)
            return; // stale/invalid id (see MmceSlotState::activeGame) -> nothing to launch
    } else
        game = gAutoLaunchBDMGame;

    if (game != NULL && game->format == GAME_FORMAT_FOLDER)
        return;
    sbSetBrowseSub(folderGetSub(itemList->mode));

    if (!cacheAbortMmceImageLoadsTimed(MMCE_ART_ABORT_WAIT_TICKS)) {
        host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
        diagLog("launch: %s from MMCE slot %d refused: an art read would not stop", game != NULL ? game->name : "?", slot->slotIndex);
        return;
    }
    if (game != NULL) {
        diagLog("launch: %s (%s) from MMCE slot %d, Game ID %s", game->name, game->startup, slot->slotIndex, mmceGameIdModeLabel(gMMCEEnableGameID));
        diagSaveBeforeLaunch();
    }

    // VCD view: hand off to POPSTARTER/Ember (by name) instead of the disc path below. Menu-launch only.
    if (gAutoLaunchBDMGame == NULL && game != NULL && (rowView(itemList, id) == LIB_VIEW_PS1)) {
        if (cueIsCueEntry(game))
            launchCue(itemList, game->name, configSet);
        else
            launchVcd(itemList, game->name, configSet);
        return;
    }

    int coreLoader = gDefaultCoreLoader;
    configGetInt(configSet, CONFIG_ITEM_CORE_LOADER, &coreLoader);
    const char *neutrinoPath = NULL;
    if (coreLoader) {
        neutrinoPath = sbResolveNeutrinoPath(slot->prefix);
        if (game->format == GAME_FORMAT_USBLD || !strcasecmp(game->extension, ".zso")) {
            host::ui().warning(_l(_STR_NEUTRINO_BAD_FORMAT), 6);
            coreLoader = 0;
        } else if (neutrinoPath == NULL) {
            host::ui().warning(_l(_STR_NEUTRINO_NOT_FOUND), 6);
            coreLoader = 0;
        }
    }

    void *irx = &mmce_cdvdman_irx;
    int irx_size = size_mmce_cdvdman_irx;
    compatmask = sbPrepare(game, configSet, irx_size, static_cast<void **>(irx), &index, !coreLoader);
    if (compatmask < 0)
        return;
    settings = (struct cdvdman_settings_mmce *)((u8 *)irx + index);

    // Persist last-played BEFORE any card switch below.
    if (gRememberLastPlayed) {
        configSetStr(configGetByType(CONFIG_LAST), "last_played", game->startup);
        host::app().saveConfig(CONFIG_LAST, 0);
    }

    if (!coreLoader) {
        sbEnsureIgrUsbDrivers(compatmask);
        // Let the user's Game ID selection switch the physical cards before
        // opening the game and any native VMC files.
        int gameIdSwitched = mmceSendGameID(game->startup, NULL, 0);
        if (gameIdSwitched < 0)
            gMmceSupport.waitSwapDone("unconfirmed switch");
        if (gameIdSwitched)
            gMmceSupport.settleAfterSwitch(slotRoot);
        char historyName[32];
        if (configGetStrCopy(configSet, CONFIG_ITEM_ALTSTARTUP, historyName, sizeof(historyName)) == 0)
            snprintf(historyName, sizeof(historyName), "%s", game->startup);
        if (sysAddHistoryRecord(historyName)) {
            if (strcmp(historyName, game->startup) != 0)
                gMmceSupport.gameIdSwitchSeenMask = 0;
            if (gMmceSupport.waitHistorySwitch(slotRoot, "history write"))
                gMmceSupport.settleDeviceFs(slotRoot);
        }
        diagSaveBeforeLaunch();
    }

    int size_mcemu_irx = 0;
    int vmc_fds[2] = {-1, -1};
    mmcePrepareVmcSlots(slot->prefix, configSet, vmc_fds, &size_mcemu_irx);

#ifdef RETROACHIEVEMENTS
    if (!sbGameRAOff(configSet))
        sbLoadWatchList(slot->prefix, game->startup);
#endif
    if ((result = sbLoadCheats(slot->prefix, game->startup, configSet)) < 0) {
        if (!sbCheatsMissingContinue(&settings->common, result)) {
            if (vmc_fds[0] >= 0)
                fileXioClose(vmc_fds[0]);
            if (vmc_fds[1] >= 0)
                fileXioClose(vmc_fds[1]);
            return;
        }
    }
    sbLoadImage(slot->prefix, game->startup);

    if (configGetStrCopy(configSet, CONFIG_ITEM_ALTSTARTUP, filename, sizeof(filename)) == 0)
        strcpy(filename, game->startup);

    // MMCEDRV port: slot 0 -> port 2 (mmce0:), slot 1 -> port 3 (mmce1:).
    if (fileXioDevctl(slotRoot, 0x1, NULL, 0, NULL, 0) == -1) {
        if (gMmceSupport.waitSwapDoneOn(slotRoot, "recovering slot", NULL))
            gMmceSupport.settleDeviceFs(slotRoot);
        if (fileXioDevctl(slotRoot, 0x1, NULL, 0, NULL, 0) == -1) {
            LOG("MMCE slot %d lost, aborting launch\n", slot->slotIndex);
            host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
            if (vmc_fds[0] >= 0)
                fileXioClose(vmc_fds[0]);
            if (vmc_fds[1] >= 0)
                fileXioClose(vmc_fds[1]);
            sbUnprepare(&settings->common);
            return;
        }
        // Card recovered from a swap/transient drop: retry any configured VMC slot that was not yet opened.
        mmcePrepareVmcSlots(slot->prefix, configSet, vmc_fds, &size_mcemu_irx);
    }
    settings->port = (slot->slotIndex == 0) ? 2 : 3;

    u64 isoTotalBytes = 0;
    int isoSizeKnown = 1;
    for (int part = 0; part < game->parts; part++) {
        sbCreatePath(game, partname, slot->prefix, "/", part);
        int fd = open(partname, O_RDONLY, 0666);
        s64 partBytes = -1;
        if (fd >= 0) {
            partBytes = lseek64(fd, 0, SEEK_END);
            close(fd);
        }
        if (partBytes > 0)
            isoTotalBytes += (u64)partBytes;
        else
            isoSizeKnown = 0;
    }

    // Initialize layer 1 information.
    sbCreatePath(game, partname, slot->prefix, "/", 0);

    if (gPS2Logo) {
        int fd = open(partname, O_RDONLY, 0666);
        if (fd >= 0) {
            EnablePS2Logo = CheckPS2Logo(fd, 0);
            close(fd);
        }
    }

    layer1_start = sbGetISO9660MaxLBA(partname);
    settings->common.mediaLsnCount = sbGetMediaLsnCount(partname, isoSizeKnown ? isoTotalBytes : 0);

    switch (game->format) {
        case GAME_FORMAT_USBLD:
            layer1_part = layer1_start / 0x80000;
            layer1_offset = layer1_start % 0x80000;
            sbCreatePath(game, partname, slot->prefix, "/", layer1_part);
            break;
        default: // Raw ISO9660 disc image; one part.
            layer1_part = 0;
            layer1_offset = layer1_start;
    }

    if (sbProbeISO9660(partname, game, layer1_offset, 0) != 0) {
        layer1_start = 0;
        LOG("DVD detected.\n");
    } else {
        layer1_start -= 16;
        LOG("DVD-DL layer 1 @ part %u sector 0x%lx.\n", layer1_part, layer1_offset);
    }
    settings->common.layer1_start = layer1_start;

    // Per-game Neutrino core
    char neutrinoExtraArgs[256] = "";
    int neutrinoVideo = gNeutrinoVideoDefault;
    int neutrinoGsmComp = gNeutrinoGsmCompDefault;
    neutrino_vmc_args_t neutrinoVmc = {0};
    if (coreLoader) {
        configGetStrCopy(configSet, CONFIG_ITEM_NEUTRINO_ARGS, neutrinoExtraArgs, sizeof(neutrinoExtraArgs));
        configGetInt(configSet, CONFIG_ITEM_NEUTRINO_VIDEO, &neutrinoVideo);
        configGetInt(configSet, CONFIG_ITEM_NEUTRINO_GSMCOMP, &neutrinoGsmComp);

        sbBuildVmcNeutrinoArgs(configSet, slot->prefix, &neutrinoVmc);
    }
    if (coreLoader) {
        char mmcePartname[256];
        snprintf(mmcePartname, sizeof(mmcePartname), "%s", partname);
        char mmceStartup[GAME_STARTUP_MAX + 1];
        snprintf(mmceStartup, sizeof(mmceStartup), "%s", game->startup);
        compatmask = sbGetCompatModes(configSet, 1);
        if (vmc_fds[0] >= 0)
            fileXioClose(vmc_fds[0]);
        if (vmc_fds[1] >= 0)
            fileXioClose(vmc_fds[1]);
        if (sysNeutrinoPreflight("mmce", neutrinoPath, 0, NULL, -1) < 0) {
            sbUnprepare(&settings->common);
            return;
        }
        if (sbNeutrinoHandCheats(mmceStartup, neutrinoPath, neutrinoExtraArgs) < 0) {
            sbUnprepare(&settings->common);
            return;
        }
#ifdef RETROACHIEVEMENTS
        if (!sbGameRAOff(configSet))
            sbNeutrinoPrepareRA(slot->prefix, mmceStartup, "mmce", neutrinoPath, neutrinoExtraArgs);
#endif
        if (sysNeutrinoArgsPreflight("mmce", mmcePartname, mmceStartup, compatmask, EnablePS2Logo, neutrinoPath, neutrinoExtraArgs, neutrinoVideo, neutrinoGsmComp, 0, -1, &neutrinoVmc) < 0) {
            sbUnprepare(&settings->common);
            return;
        }
        sysNeutrinoWarnPadEmu(configSet);
        int gameIdSwitched = mmceSendGameID(game->startup, neutrinoPath,
                                            (neutrinoVmc.arg[0][0] ? 1 : 0) | (neutrinoVmc.arg[1][0] ? 2 : 0));
        if (gameIdSwitched < 0)
            gMmceSupport.waitSwapDone("unconfirmed switch");
        if (gameIdSwitched)
            gMmceSupport.settleAfterSwitch(slotRoot);
        diagSaveBeforeLaunch();
        int neutrinoDevMode = host::app().pathToMode(neutrinoPath);
        if (gMmceSupport.waitBeforeHandoff(slotRoot))
            gMmceSupport.settleAfterSwitch(slotRoot);
        host::app().deinitEx(sbNeutrinoDeinitException(neutrinoPath), itemList->mode, neutrinoDevMode);
        sysLaunchNeutrino("mmce", mmcePartname, mmceStartup, compatmask, EnablePS2Logo, neutrinoPath, neutrinoExtraArgs, neutrinoVideo, neutrinoGsmComp, 0, -1, &neutrinoVmc);
        return;
    }

    int iso_file = fileXioOpen(partname, 0x1, 0666);
    for (int isoRetry = 0; iso_file < 0 && isoRetry < 10; isoRetry++) {
        DelayThread(200 * 1000);
        iso_file = fileXioOpen(partname, 0x1, 0666);
    }
    if (iso_file < 0) {
        LOG("Failed to open iso %s (ret %d), aborting\n", partname, iso_file);
        if (vmc_fds[0] >= 0)
            fileXioClose(vmc_fds[0]);
        if (vmc_fds[1] >= 0)
            fileXioClose(vmc_fds[1]);
        host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
        sbUnprepare(&settings->common);
        return;
    }

    settings->ack_wait_cycles = gMMCEAckWaitCycles;
    settings->use_alarms = gMMCEUseAlarms;

    settings->iso_fd = fileXioIoctl2(iso_file, 0x80, NULL, 0, NULL, 0);

    LOG("name: %s\n", game->name);
    LOG("start: %s\n", game->startup);

    if (gMmceSupport.waitBeforeHandoff(slotRoot))
        gMmceSupport.settleAfterSwitch(slotRoot);

    if (gAutoLaunchBDMGame == NULL) {
        host::app().deinit(NO_EXCEPTION, itemList->mode); // CAREFUL: deinit calls cleanUp, freeing slot->games
    }

    settings->common.zso_cache = 0;

    sysLaunchLoaderElf(filename, "MMCE_MODE", irx_size, static_cast<void **>(irx), size_mcemu_irx, mmce_mcemu_irx, EnablePS2Logo, compatmask);
}

config_set_t *MmceSource::config(item_list_t *itemList, int id)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    base_game_info_t *game = slot->activeGame(itemList, id);
    config_set_t *config = sbPopulateConfig(game, slot->prefix, "/");
    ps1Size(itemList, game, config);
    return config;
}

void MmceSource::ps1Size(item_list_t *itemList, base_game_info_t *game, config_set_t *config)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    char root[sizeof(slot->prefix)];
    slot->getDeviceRoot(root, sizeof(root)); // POPS/ and EMBER/ live at the card root
    sbResolvePs1Size(game, root, config);
}

int MmceSource::image(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    return mmceTryLoadImage(slot->artPrimary, folder, isRelative, value, suffix, resultTex);
}

int MmceSource::textId(item_list_t *itemList)
{
    (void)itemList;
    return _STR_MMCE_GAMES;
}

int MmceSource::iconId(item_list_t *itemList)
{
    (void)itemList;
    return MMCE_ICON;
}

// This may be called, even if init() was not.
void MmceSource::cleanUp(item_list_t *itemList, int exception)
{
    (void)exception;
    if (itemList != NULL && itemList->enabled) {
        MmceSlotState *slot = mmceSlotStateOf(itemList);
        LOG("MMCESUPPORT CleanUp (slot %d)\n", slot->slotIndex);

        free(slot->games);
        slot->games = NULL;
        free(slot->ps1Games);
        slot->ps1Games = NULL;
    }
}

// This may be called, even if init() was not.
void MmceSource::shutdown(item_list_t *itemList)
{
    if (itemList != NULL && itemList->enabled) {
        MmceSlotState *slot = mmceSlotStateOf(itemList);
        LOG("MMCESUPPORT Shutdown (slot %d)\n", slot->slotIndex);

        free(slot->games);
        slot->games = NULL;
        free(slot->ps1Games);
        slot->ps1Games = NULL;
    }
}

int MmceSource::checkVmc(item_list_t *itemList, char *name, int createSize)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    return sysCheckVMC(slot->prefix, "/", name, createSize, NULL);
}

char *MmceSource::prefix(item_list_t *itemList)
{
    return mmceSlotStateOf(itemList)->prefix;
}

int MmceSource::ps1SaveDir(item_list_t *itemList, int id, const char *name, int *ember, char *directory, int directorySize,
                           char *partition, int partitionSize)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    char root[64];
    (void)partitionSize;
    if (!ps1RowKind(itemList, id, ember))
        return 0;
    partition[0] = '\0';
    slot->getDeviceRoot(root, sizeof(root));
    return saveIconPs1Directory(root, name, *ember, directory, directorySize);
}

int MmceSource::saveCueSettings(item_list_t *itemList, int id, const char *name, config_set_t *configSet)
{
    MmceSlotState *slot = mmceSlotStateOf(itemList);
    char ps1Root[64];
    (void)id;

    if (!cacheAbortMmceImageLoadsTimed(MMCE_ART_ABORT_WAIT_TICKS))
        return 0;
    slot->getDeviceRoot(ps1Root, sizeof(ps1Root));
    return cueSaveGameSettings(ps1Root, name, configSet);
}
