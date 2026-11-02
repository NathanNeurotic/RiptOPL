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
// Card-switch wait. On a CROSS-DEVICE launch (a USB/HDD/SMB game whose per-game card lives on the MMCE)
// the 0x8 push physically switches the SD card, and the launch MUST wait for that to finish -- otherwise
// the game boots while the card is still mounting and freezes at its MC check (issue #50).
// sd2psXtd starts the switch on a LATER pass of its main loop, not while it answers the 0x8 send, so a
// poll straight after the send reads "not busy" before the switch has begun and the game boots while
// the card is still loading (or creating) its per-game card from the SD the game streams from -- the
// reported Burnout 3 hang. So busy must be SEEN first, within MMCE_GAMEID_START_MS (never seen: the
// slot already holds this game's card, or Game ID is off on the device), and is then waited out on
// screen: loading an 8 MB card takes ~9 s, creating a new one longer. START launches at once.
// Ported from RIPPS2 build 115 (akilluminati47/RIPPS2, mmcesupport.c).
constexpr int MMCE_GAMEID_START_MS = 2000;    // how long a sent Game ID may take to start the switch
constexpr int MMCE_GAMEID_START_POLL_MS = 50; // poll interval while waiting for the switch to start
constexpr int MMCE_GAMEID_QUERY_FAILURES = 3; // failed busy queries in a row that end the wait (unconfirmed)
constexpr int MMCE_GAMEID_QUERY_SILENT = 2;   // after an answer: failed queries in a row that mean the switch began
constexpr int MMCE_CARD_READY_MAX_S = 180;    // the longest the launch waits for the game's card
constexpr int MMCE_CARD_READY_POLL_US = (250 * 1000);
constexpr int MMCE_CARD_READY_SILENT_MS = 7500; // the longest the card may not answer at all (mmceigr's bound)
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

int MmceSupport::sendGameID(const char *startup, const char *protectMcPath, int vmcSlotMask)
{
    char mmceDevice[sizeof(prefix)];

    gameIdTarget[0] = '\0'; // no stale target: only a send made by THIS call may be settled against
    gameIdSwitchSeen = 0;

    if (!gMMCEEnableGameID || startup == NULL || startup[0] == '\0')
        return 0;

    // Δ4 (NHDDL parity): do NOT self-arm the transport here. Loading an IRX inside the launch
    // sequence puts a module load/start at the launch's most fragile moment; the arming now happens
    // at menu/settings time (mmceArmGameIDTransport, called from initAllSupport whenever the GameID
    // feature is on), where a failure is a harmless LOG. Preserves the #51 intent -- GameID works
    // without the MMCE page ever being enabled -- with the risk moved off the launch path. If the
    // module is not resident by launch time (arm failed / raced), skip gracefully like no-card.
    if (!modLoaded) {
        LOG("MMCE GameID: transport not armed -- skipping (menu-time arm failed or pending)\n");
        return 0;
    }

    // Candidate order: the configured/resolved slot first, then both slots as fallback -- a card in
    // EITHER slot still gets the game-id on a cross-device launch (#261). Same 0x1 presence devctl
    // as detectSlot. Δ3 (NHDDL parity): a slot whose -mc<slot> Neutrino arg is set gets its MC
    // from the VMC FILE, not the card's per-game folder -- switching the physical card for it is
    // moot and only adds the busy/re-mount window, so covered slots are skipped; a card in the
    // OTHER, uncovered slot still gets the push. vmcSlotMask bit N = "-mcN arg present" (0 = the
    // OPL-core paths, which use mcemu and keep today's behavior).
    getDeviceRoot(mmceDevice, sizeof(mmceDevice));
    {
        const char *cands[3] = {mmceDevice[0] != '\0' ? mmceDevice : NULL, "mmce0:/", "mmce1:/"};
        int tried[2] = {0, 0};
        int found = 0;
        for (int c = 0; c < 3 && !found; c++) {
            if (cands[c] == NULL || strlen(cands[c]) < 5)
                continue;
            int slot = cands[c][4] - '0';
            if (slot < 0 || slot > 1 || tried[slot])
                continue;
            tried[slot] = 1;
            if ((vmcSlotMask >> slot) & 1) {
                LOG("MMCE GameID: slot %d covered by a -mc%d VMC arg -- not switching that card\n", slot, slot);
                continue;
            }
            if (fileXioDevctl(cands[c], 0x1, NULL, 0, NULL, 0) != -1) {
                if (cands[c] != mmceDevice)
                    snprintf(mmceDevice, sizeof(mmceDevice), "%s", cands[c]);
                found = 1;
            }
        }
        if (!found)
            return 0; // no eligible MMCE card present -> graceful no-op
    }

    // NHDDL-parity guard (#51): never switch the per-game card on a slot whose EMULATED memory card
    // (mcN:) holds the neutrino.elf we are about to load -- the 0x8 switch moves the mcN: surface and
    // would yank the loader out from under sysLaunchNeutrino. A neutrino.elf on the MMCE's SD (mmceN:)
    // is NOT affected by the card switch, so only the mcN: case is guarded.
    if (protectMcPath != NULL && protectMcPath[0] != '\0') {
        const char *mc = NULL;
        if (!strncmp(mmceDevice, "mmce0", 5))
            mc = "mc0:";
        else if (!strncmp(mmceDevice, "mmce1", 5))
            mc = "mc1:";
        if (mc != NULL && !strncmp(protectMcPath, mc, strlen(mc))) {
            // neutrino.elf is on this slot's emulated card -- switching would pull the loader out from
            // under sysLaunchNeutrino. Leave the card as-is so the launch still works, but SOFT-FAIL with
            // a transient notice (was a silent no-op) so the user understands their per-game card folder
            // wasn't applied this launch -- the only situation GameID + MMCE + neutrino-on-mc can collide.
            host::ui().warning(_l(_STR_MMCE_GAMEID_NEUTRINO_SKIP), 6);
            return 0;
        }
    }

    if (fileXioDevctl(mmceDevice, 0x8, (void *)startup, (strlen(startup) + 1), NULL, 0) < 0)
        return 0;

    // Remember the slot this send actually targeted -- mmceGameIdSettle() (the MX4SIO pre-launch
    // gate, batch S7) must poll the SAME device, not a guess.
    snprintf(gameIdTarget, sizeof(gameIdTarget), "%s", mmceDevice);

    int queryFailures = 0;
    int answered = 0; // the card answered a busy query since the send, so its firmware has one
    for (int waited = 0; waited < MMCE_GAMEID_START_MS; waited += MMCE_GAMEID_START_POLL_MS) {
        int status = fileXioDevctl(mmceDevice, 0x2, NULL, 0, NULL, 0);
        if (status < 0) {
            // One failed query is retried, not trusted. Before any answer, a run of failures means the
            // firmware has no busy query: end the wait unconfirmed so the launch is not held. After an
            // answer, a run of failures is the switch itself: an SD2PSX stops answering while it swaps the
            // cards, then reports busy while the new one loads (sd2psXtd 8a20bd9; belomaxorka/YAOPL
            // 19578cc6 found the same silence).
            ++queryFailures;
            if (!answered && queryFailures >= MMCE_GAMEID_QUERY_FAILURES) {
                // The card answered the presence probe and the 0x8 itself a moment ago, so a run of failed
                // busy queries straight after the send is one of two things, and only the presence devctl
                // tells them apart. A card that still answers 0x1 has a firmware without the busy query:
                // end the wait unconfirmed, as before. A card that answers NOTHING is already in its swap:
                // sd2psXtd processes SET_GAMEID and starts the switch in the SAME main-loop pass that
                // follows the 0x8, and from "card exit" until the new card is open nothing on it answers
                // SIO2 -- for 500 ms of sleep plus the dirty-page flush of whatever was just written (the
                // last-played save above). Reading that silence as "no busy query" launched the game while
                // the card it streams from was still being swapped: an MMCE-hosted launch then never gets
                // its ISO (black screen, or the PS2 logo and nothing after it), while the same game from
                // MX4SIO survives because its disc is elsewhere and the MX4SIO leg waits (mmceGameIdSettle).
                if (fileXioDevctl(mmceDevice, 0x1, NULL, 0, NULL, 0) != -1) {
                    LOG("MMCE GameID switch: busy query failed %d times while the card answers; launching unconfirmed\n", queryFailures);
                    diagLog("gameid %s: busy query failed %d times; launching unconfirmed", startup, queryFailures);
                    return -1;
                }
                LOG("MMCE GameID %s: the card went silent after %d ms (swapping)\n", startup, waited);
                diagLog("gameid %s: the card silent after %d ms; waiting for the swap", startup, waited);
                gameIdSwitchSeen = 1; // the card is switching for our send
                int ready = waitCardReady(mmceDevice);
                if (ready < 0)
                    return -1; // never came back within the silent bound: unconfirmed
                if (ready == 0)
                    break; // still busy at the cap: launched anyway, not settled
                LOG("Set MMCE GameID to: %s\n", startup);
                return 1;
            }
            if (!answered || queryFailures < MMCE_GAMEID_QUERY_SILENT) {
                DelayThread(MMCE_GAMEID_START_POLL_MS * 1000);
                continue;
            }
        } else {
            answered = 1;
            queryFailures = 0;
        }
        if (status < 0 || (status & 1)) {
            LOG("MMCE GameID %s: the card started switching after %d ms (%s)\n", startup, waited,
                status < 0 ? "not answering" : "busy");
            diagLog("gameid %s: the card switching after %d ms", startup, waited);
            gameIdSwitchSeen = 1; // the card is switching for our send
            int ready = waitCardReady(mmceDevice);
            if (ready < 0)
                return -1; // the query failed mid-wait: unconfirmed
            if (ready == 0)
                break; // still busy at the cap: launched anyway, not settled
            LOG("Set MMCE GameID to: %s\n", startup);
            return 1; // card finished switching (settle CONFIRMED, or the user pressed START)
        }
        DelayThread(MMCE_GAMEID_START_POLL_MS * 1000);
    }
    int idle = fileXioDevctl(mmceDevice, 0x2, NULL, 0, NULL, 0);
    if (idle >= 0 && (idle & 1) == 0) {
        LOG("MMCE GameID %s: no switch within %d ms (this game's card already, or Game ID off on the card)\n", startup,
            MMCE_GAMEID_START_MS);
        diagLog("gameid %s: no switch within %d ms", startup, MMCE_GAMEID_START_MS);
        return 1;
    }

    // Tri-state (batch S7): -1 = the 0x8 switch WAS sent but the settle was never confirmed (busy
    // query unsupported, or the budget expired). Truthy on purpose -- the mmce-launch callers test
    // truthiness and must still run their own settle; only the MX4SIO cross-device gate
    // (mmceGameIdSettle) distinguishes -1 from 1, because there the un-settled switch shares SIO2
    // with the SD enumeration the launch is about to depend on.
    LOG("MMCE GameID switch not confirmed within budget; launching anyway\n");
    return -1;
}

// Waits, on screen, while the card reports busy after a Game ID switch. Returns 1 when it is ready or the
// user pressed START, 0 when it was still busy at MMCE_CARD_READY_MAX_S, -1 when the card stopped
// answering for MMCE_CARD_READY_SILENT_MS before it reported ready (unconfirmed). The launch goes ahead in
// every case. An SD2PSX does not answer at all while it swaps cards (sd2psXtd ps2_mmceman_task(): card exit,
// close, 500 ms, open), so a failed query here is part of the switch, not its end.
int MmceSupport::waitCardReady(const char *device)
{
    char line[256];
    int status, polls, silent = 0;

    for (polls = 0;; polls++) {
        status = fileXioDevctl(device, 0x2, NULL, 0, NULL, 0);
        if (status >= 0 && (status & 1) == 0)
            break; // the game's card is ready
        if (status >= 0)
            silent = 0;
        else if (++silent * (MMCE_CARD_READY_POLL_US / 1000) >= MMCE_CARD_READY_SILENT_MS) {
            LOG("MMCE: the card did not answer for %d ms during the wait; not confirmed\n",
                silent * (MMCE_CARD_READY_POLL_US / 1000));
            diagLog("mmce: the card's busy query failed during the wait");
            return -1;
        }
        int seconds = polls * MMCE_CARD_READY_POLL_US / 1000000;
        if (seconds >= MMCE_CARD_READY_MAX_S) {
            LOG("MMCE: the game's memory card was still busy after %d s; launching\n", seconds);
            diagLog("mmce: the game's card still busy after %d s; launching", seconds);
            return 0;
        }
        readPads();
        if (getKeyOn(KEY_START)) {
            LOG("MMCE: START pressed after %d s of the card's wait; launching\n", seconds);
            diagLog("mmce: START after %d s of the card's wait; launching", seconds);
            return 1;
        }
        snprintf(line, sizeof(line), _l(_STR_MMCE_GAMEID_CARD_WAIT), seconds);
        host::ui().renderTextScreen(line);
        DelayThread(MMCE_CARD_READY_POLL_US);
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
// when it could not confirm the settle (the card fell silent past its bound, the firmware has no busy
// query, or the budget expired), and the handoff itself can land on a switch the firmware deferred (its
// own buttons held, a flush in progress). Both are bounded waits here, on the slot the send targeted:
// presence first (silence is the swap's first phase), then the busy bit, waited out on screen as in the
// send. A firmware without the busy query settles the moment presence answers, so it pays ~0 ms. The
// MX4SIO leg has had this since batch S7 (mmceGameIdSettle); the MMCE-hosted legs did not, and they are
// the ones that cannot survive the swap: the ISO the game streams is on the card being swapped.
void MmceSupport::waitSwapDoneOn(const char *device, const char *why)
{
    int silent;
    for (silent = 0; silent * 200 < MMCE_CARD_READY_SILENT_MS; silent++) {
        if (fileXioDevctl(device, 0x1, NULL, 0, NULL, 0) != -1) {
            int busy = fileXioDevctl(device, 0x2, NULL, 0, NULL, 0);
            if (busy >= 0 && (busy & 1)) {
                LOG("MMCE %s: the card is still swapping; waiting\n", why);
                diagLog("mmce %s: card still busy; waiting", why);
                waitCardReady(device);
            } else if (silent)
                LOG("MMCE %s: the card answers again after ~%d ms\n", why, silent * 200);
            return;
        }
        DelayThread(200 * 1000);
    }
    LOG("MMCE %s: the card did not answer for %d ms; launching anyway\n", why, silent * 200);
    diagLog("mmce %s: card silent for %d ms; launching anyway", why, silent * 200);
}

void MmceSupport::waitSwapDone(const char *why)
{
    if (gameIdTarget[0] == '\0')
        return; // no send this launch: nothing to wait for
    waitSwapDoneOn(gameIdTarget, why);
}

// The last look before the IOP handoff. With a Game ID send this launch it is the slot that send targeted;
// without one (Game ID off, the reported case) it is the game's own slot, so a history-triggered switch that
// began after waitHistorySwitch's window is still caught. ~0 ms on an idle card.
void MmceSupport::waitBeforeHandoff(void)
{
    char device[8];
    getDeviceRoot(device, sizeof(device));
    waitSwapDoneOn(gameIdTarget[0] != '\0' ? gameIdTarget : device, "before handoff");
}

// After the OSD history write on an MMCE-hosted launch: watch the game's slot for the card's own folder
// switch (see the launch) and wait it out on screen. Nothing to watch when our Game ID send already
// switched the card (it is on that folder), or when the firmware has no busy query (presence answers
// while the busy query fails): then the window is not paid. Bounded: the window, then waitCardReady.
int MmceSupport::waitHistorySwitch(const char *device, const char *why)
{
    if (gameIdSwitchSeen) {
        LOG("MMCE %s: the card already switched for the Game ID send; not waiting again\n", why);
        return 0;
    }
    char line[256];
    int polls, silent = 0;
    for (polls = 0; polls * MMCE_HISTORY_SWITCH_POLL_MS < MMCE_HISTORY_SWITCH_WINDOW_MS; polls++) {
        int status = fileXioDevctl(device, 0x2, NULL, 0, NULL, 0);
        if (status >= 0 && (status & 1) == 0) {
            silent = 0; // answering and idle: the switch has not begun (or will not)
        } else if (status < 0 && ++silent < MMCE_GAMEID_QUERY_FAILURES) {
            // one failed query is retried, not trusted
        } else if (status < 0 && fileXioDevctl(device, 0x1, NULL, 0, NULL, 0) != -1) {
            LOG("MMCE %s: the card has no busy query; cannot watch for its switch\n", why);
            return 0;
        } else {
            LOG("MMCE %s: the card is switching on its own after %d ms (%s)\n", why, polls * MMCE_HISTORY_SWITCH_POLL_MS,
                status < 0 ? "not answering" : "busy");
            diagLog("mmce %s: the card switching on its own after %d ms", why, polls * MMCE_HISTORY_SWITCH_POLL_MS);
            waitCardReady(device);
            return 1;
        }
        // The whole window is visible (it looked like a freeze before the launch), and START skips it.
        readPads();
        if (getKeyOn(KEY_START)) {
            LOG("MMCE %s: START pressed after %d ms; not watching for the card's switch\n", why, polls * MMCE_HISTORY_SWITCH_POLL_MS);
            return 0;
        }
        snprintf(line, sizeof(line), _l(_STR_MMCE_GAMEID_CARD_WAIT), polls * MMCE_HISTORY_SWITCH_POLL_MS / 1000);
        host::ui().renderTextScreen(line);
        DelayThread(MMCE_HISTORY_SWITCH_POLL_MS * 1000);
    }
    LOG("MMCE %s: no card switch within %d ms\n", why, MMCE_HISTORY_SWITCH_WINDOW_MS);
    return 0;
}

// Bounded post-GameID settle for cross-device launches that share SIO2 with the MMCE (MX4SIO, batch
// S7: the lime-green hang is cdvdman waiting forever for the SD card to enumerate; an mmce switch
// still in flight during the IOP reboot can starve that enumeration). Polls the exact device the
// last send targeted: settled when presence answers AND the busy bit is clear OR unavailable --
// requiring a readable busy bit would turn every busy-devctl-less firmware into a guaranteed full
// stall. Returns 0 settled, -1 expired. NEVER blocks the launch -- the caller toasts and proceeds.
int MmceSupport::gameIdSettle(int timeoutMs)
{
    if (gameIdTarget[0] == '\0')
        return 0;
    for (int waited = 0; waited <= timeoutMs; waited += 200) {
        if (fileXioDevctl(gameIdTarget, 0x1, NULL, 0, NULL, 0) != -1) {
            int busy = fileXioDevctl(gameIdTarget, 0x2, NULL, 0, NULL, 0);
            if (busy < 0 || (busy & 1) == 0)
                return 0;
        }
        DelayThread(200 * 1000);
    }
    LOG("MMCE GameID settle NOT confirmed after %d ms\n", timeoutMs);
    return -1;
}

int mmceGameIdSettle(int timeoutMs)
{
    return gMmceSupport.gameIdSettle(timeoutMs);
}

void MmceSupport::getDeviceRoot(char *root, size_t size)
{
    const char *separator = strstr(prefix, ":/");
    size_t length;

    if (root == NULL || size == 0)
        return;

    if (separator != NULL) {
        length = (size_t)(separator - prefix) + 2;
        if (length >= size)
            length = size - 1;

        memcpy(root, prefix, length);
        root[length] = '\0';
        return;
    }

    if (gMMCESlot == 0)
        snprintf(root, size, "mmce0:/");
    else if (gMMCESlot == 1)
        snprintf(root, size, "mmce1:/");
    else
        root[0] = '\0';
}

int MmceSupport::resetCards(void)
{
    char mmceDevice[sizeof(prefix)];
    int resetCount = 0;

    gameIdTarget[0] = '\0';

    if (!modLoaded)
        loadModules();

    if (!modLoaded)
        return 0;

    getDeviceRoot(mmceDevice, sizeof(mmceDevice));
    {
        const char *cands[3] = {mmceDevice[0] != '\0' ? mmceDevice : NULL, "mmce0:/", "mmce1:/"};
        int tried[2] = {0, 0};

        for (int c = 0; c < 3; c++) {
            if (cands[c] == NULL || strlen(cands[c]) < 5)
                continue;
            int slot = cands[c][4] - '0';
            if (slot < 0 || slot > 1 || tried[slot])
                continue;
            tried[slot] = 1;

            if (fileXioDevctl(cands[c], 0x1, NULL, 0, NULL, 0) != -1) {
                LOG("MMCE: sending reset (0x9) to %s\n", cands[c]);
                fileXioDevctl(cands[c], 0x9, NULL, 0, NULL, 0);

                for (int i = 0; i < MMCE_GAMEID_WAIT_TICKS; i++) {
                    int status = fileXioDevctl(cands[c], 0x2, NULL, 0, NULL, 0);
                    if (status < 0 || (status & 1) == 0)
                        break;

                    DelayThread(MMCE_GAMEID_POLL_US);
                }
                resetCount++;
            }
        }
    }

    return resetCount;
}

int mmceReset(void)
{
    return gMmceSupport.resetCards();
}

// Fs-settle after a GameID card switch. The 0x8 devctl physically re-mounts the card, and on Gen2
// the busy bit (sendGameID's own wait) can clear before the FILESYSTEM surface is back. Probe
// the switched slot until a directory open answers: poll-first so a fast card costs ~0 ms; bounded
// (~5 s) so a dead card can't hang; LOG each outcome so a debug ELF can localise a black screen to
// OPL vs the loaded core. The helper may have fallen back to the OTHER slot when the game's slot
// has no card (or is -mc-covered), so stay consistent: if the game's slot isn't present, settle the
// other slot instead -- otherwise we'd probe an empty slot for the full ~5 s (PR #89 review). Same
// 0x1 presence devctl mmceSendGameID itself uses.
void MmceSupport::settleAfterSwitch(void)
{
    char mmceRoot[sizeof(prefix)];
    getDeviceRoot(mmceRoot, sizeof(mmceRoot));
    if (mmceRoot[0] != '\0' && strlen(mmceRoot) >= 5 &&
        fileXioDevctl(mmceRoot, 0x1, NULL, 0, NULL, 0) == -1)
        mmceRoot[4] = (mmceRoot[4] == '0') ? '1' : '0'; // mmce0:/ <-> mmce1:/
    if (mmceRoot[0] == '\0')
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

void MmceSupport::refreshArtRoots(void)
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

int MmceSupport::detectSlot(void)
{
    int ret = -1;
    if (fileXioDevctl("mmce0:/", 0x1, NULL, 0, NULL, 0) != -1) {
        snprintf(prefix, sizeof(prefix), "mmce0:/%s", gMMCEPrefix);
        ret = 2;
    } else if (fileXioDevctl("mmce1:/", 0x1, NULL, 0, NULL, 0) != -1) {
        snprintf(prefix, sizeof(prefix), "mmce1:/%s", gMMCEPrefix);
        ret = 3;
    }
    return ret;
}

void MmceSupport::setPrefix(void)
{
    if (gMMCESlot == 0)
        snprintf(prefix, sizeof(prefix), "mmce0:/%s", gMMCEPrefix);
    else if (gMMCESlot == 1)
        snprintf(prefix, sizeof(prefix), "mmce1:/%s", gMMCEPrefix);
    else if (gMMCESlot == 2) {
        // Auto: reuse the previously-detected slot instead of probing BOTH slots every refresh.
        // On a cache hit, a presence devctl on the resolved slot confirms the card is still there
        // (prefix from the prior detect is still correct); only MMCE_PRESENCE_PROBE_MAX
        // CONSECUTIVE misses mean the card was pulled (#154 debounce, below), so invalidate and fall
        // through to a full re-detect. Net: 1 SIO2 probe/cycle instead of 2 on the happy path, and
        // card removal is still noticed (detectSlot alone leaves a stale prefix on a lost card).
        if (resolvedDevice > 0) {
            const char *root = (resolvedDevice == 2) ? "mmce0:/" : "mmce1:/";
            // Debounced presence check (#154): invalidate only after MMCE_PRESENCE_PROBE_MAX
            // consecutive failed devctls -- see the define block above. One success = present.
            int probe;
            for (probe = 0; probe < MMCE_PRESENCE_PROBE_MAX; probe++) {
                if (fileXioDevctl(root, 0x1, NULL, 0, NULL, 0) != -1)
                    break;
                if (probe + 1 < MMCE_PRESENCE_PROBE_MAX)
                    DelayThread(MMCE_PRESENCE_RETRY_US); // let a contended bus quiet, then re-probe
            }
            if (probe >= MMCE_PRESENCE_PROBE_MAX) {
                resolvedDevice = -1;
                prefix[0] = '\0';
            } else {
                // Still present. Rebuild prefix from the CURRENT gMMCEPrefix (a Device-Settings
                // prefix change must apply immediately -- initSupport does not re-init an already-
                // enabled MMCE tab) using the cached slot; we skip only the second SIO2 slot probe.
                snprintf(prefix, sizeof(prefix), "mmce%d:/%s", (resolvedDevice == 2) ? 0 : 1, gMMCEPrefix);
            }
        }
        if (resolvedDevice <= 0) {
            /* A USB hotplug is the worst moment to ask: re-enumeration floods the shared io worker
               while the pad keeps polling SIO2, and that is exactly when Kamo's MMCE tab emptied.
               Give the re-detect the same inline debounce as the presence probe above -- same gap,
               same reasoning -- so a bus-quiet window between attempts can absorb the transient. */
            int attempt, attempts = everResolved ? MMCE_PRESENCE_PROBE_MAX : 1;
            for (attempt = 0; attempt < attempts; attempt++) {
                resolvedDevice = detectSlot();
                if (resolvedDevice > 0)
                    break;
                if (attempt + 1 < attempts)
                    DelayThread(MMCE_PRESENCE_RETRY_US);
            }
        }
        if (resolvedDevice > 0)
            everResolved = 1;
    }

    refreshArtRoots();
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
    MmceSource()
        : GameSource(MMCE_MODE, 2, 0, MENU_MIN_INACTIVE_FRAMES, MMCE_MODE_UPDATE_DELAY, GameSource::CAP_LAUNCH_VCD | GameSource::CAP_LAUNCH_CUE | GameSource::CAP_ROW_VIEW | GameSource::CAP_SOURCE_ID | GameSource::CAP_SAVE_CUE_SETTINGS)
    {
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

static MmceSource *mmceInstance()
{
    static MmceSource *instance = NULL;
    alignas(MmceSource) static unsigned char storage[sizeof(MmceSource)];

    if (instance == NULL)
        instance = new (storage) MmceSource();
    return instance;
}


void MmceSource::init(item_list_t *itemList)
{
    LOG("MMCESUPPORT Init\n");
    gMmceSupport.prefix[0] = '\0';
    gMmceSupport.artPrimary[0] = '\0';
    gMmceSupport.ulSizePrev = -2;
    gMmceSupport.modifiedCDPrev = 0;
    gMmceSupport.modifiedDVDPrev = 0;
    gMmceSupport.gameCount = 0;
    gMmceSupport.games = NULL;
    gMmceSupport.ps1GameCount = 0;
    gMmceSupport.ps1Games = NULL;
    gMmceSupport.resolvedDevice = -1; // re-detect the Auto slot on a fresh init (tab re-enable / settings apply)
    gMmceSupport.foldersCreatedFor[0] = '\0';
    gMmceSupport.folderRetries = 0;

    (*mmceInstance()).delay = gArtDelay;
    (*mmceInstance()).updateDelay = MMCE_MODE_UPDATE_DELAY;

    gMmceSupport.loadModules();
    gMmceSupport.setPrefix();

    (*mmceInstance()).enabled = 1;
}

item_list_t *mmceGetObject(int initOnly)
{
    if (initOnly && !(*mmceInstance()).enabled)
        return NULL;
    return &(*mmceInstance());
}

int MmceSource::needsUpdate(item_list_t *itemList)
{
    static unsigned char ThemesLoaded = 0;
    static unsigned char LanguagesLoaded = 0;

    char path[256];
    int result = 0;
    struct stat st;

    // Hacky: check if slot was changed, update prefix if needed
    gMmceSupport.setPrefix();

    if (gMmceSupport.prefix[0] == '\0') {
        (*mmceInstance()).updateDelay = MMCE_MODE_UPDATE_DELAY;
        gMmceSupport.foldersCreatedFor[0] = '\0'; // card gone: recreate folders on the next (possibly different) card
        gMmceSupport.folderRetries = 0;
        // Card gone with an EMPTY failed VCD list never reaches update()'s resets (this
        // early return fires first), so a reinserted card would inherit an exhausted retry budget
        // and a dead VCD page (CodeRabbit review of #248, vetted). Fresh card = fresh budget.
        gMmceSupport.ps1Scanned = 0;
        gMmceSupport.ps1ScanFailed = 0;
        gMmceSupport.ps1ScanRetries = 0;
        // Card gone: re-arm THM/LNG registration so a swapped-in card's assets get discovered
        // (Gemini review of #153). The old card's already-registered entries stay in the pickers --
        // eviction infrastructure doesn't exist -- but picking a stale one fails gracefully
        // (thmLoad abandons and keeps the current theme), and thmAddElements caps at THM_MAX_FILES.
        ThemesLoaded = 0;
        LanguagesLoaded = 0;
        return (gMmceSupport.gameCount > 0 || gMmceSupport.ps1GameCount > 0);
    }

    (*mmceInstance()).updateDelay = MENU_UPD_DELAY_NOUPDATE;

    // Register the card's THM/LNG dirs BEFORE the VCD-view early returns (#152, AndrewBento). These
    // used to sit below them, giving one realistic shot at boot: once the first list scan latches
    // NOUPDATE above, the only future passes are L3-toggle / VCD-view ones, which returned before
    // reaching the registration -- so if the boot-time attempt lost a race against the contended
    // MMCE SIO2 bus (config + list + art traffic), themes on the card stayed invisible for the whole
    // session while USB's fast first try succeeded. Here every pass retries until each succeeds; the
    // cost is one dir-open per pass until then (identical to the old ISO-view retry behavior).
    if (!ThemesLoaded) {
        snprintf(path, sizeof(path), "%sTHM", gMmceSupport.prefix);
        if (thmAddElements(path, "/", 1) > 0)
            ThemesLoaded = 1;
    }
    if (!LanguagesLoaded) {
        snprintf(path, sizeof(path), "%sLNG", gMmceSupport.prefix);
        if (lngAddLanguages(path, "/", (*mmceInstance()).mode) > 0)
            LanguagesLoaded = 1;
    }

    // VCD view: force a rescan once on toggle, then skip the disc heuristics while showing VCDs.
    if (host::library().consumeDirty(itemList->mode)) {
        gMmceSupport.ps1ScanRetries = 0; // fresh user toggle re-arms the failed-scan retry budget
        return 1;
    }
    // Folder browsing: descend/ascend forces one rescan (consumed before the NOUPDATE latch below).
    if (folderConsumeDirty(itemList->mode))
        return 1;
    if (host::library().viewActive(itemList->mode) == LIB_VIEW_PS1 || host::library().viewActive(itemList->mode) == LIB_VIEW_MIXED) {
        if (!gMmceSupport.ps1Scanned) {
            (*mmceInstance()).updateDelay = MMCE_MODE_UPDATE_DELAY;
            result = 1;
        }
        // A contended scan left the VCD page empty (S6): keep the ~2s refresh alive until a scan
        // succeeds or the bounded budget runs out (no endless bus churn -- #246 doctrine). Also
        // revives the manual-refresh button in the failed state.
        if (gMmceSupport.ps1ScanFailed && gMmceSupport.ps1ScanRetries < MMCE_VCD_SCAN_RETRY_MAX) {
            gMmceSupport.ps1ScanRetries++;
            (*mmceInstance()).updateDelay = MMCE_MODE_UPDATE_DELAY;
            result = 1;
        }
        if (host::library().viewActive(itemList->mode) == LIB_VIEW_PS1)
            return result;
    }

    if (gMmceSupport.ulSizePrev == -2) {
        // First scan not yet successful. If it wedges on a contended SIO2 bus, sbReadList leaves
        // ulSizePrev at its -2 sentinel and the tab stays empty; the NOUPDATE latch above would
        // then strand it with no auto-retry (the reported "all MMCE lists vanished"). Keep the ~2s
        // background retry alive until a scan populates the list -- a genuinely-empty readable card
        // sets ulSizePrev via *fsize, so this still quiesces once the bus is readable.
        (*mmceInstance()).updateDelay = MMCE_MODE_UPDATE_DELAY;
        result = 1;
    }

    snprintf(path, sizeof(path), "%sCD", gMmceSupport.prefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;

    if (gMmceSupport.modifiedCDPrev != st.st_mtime) {
        gMmceSupport.modifiedCDPrev = st.st_mtime;
        result = 1;
    }

    snprintf(path, sizeof(path), "%sDVD", gMmceSupport.prefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;

    if (gMmceSupport.modifiedDVDPrev != st.st_mtime) {
        gMmceSupport.modifiedDVDPrev = st.st_mtime;
        result = 1;
    }

    if (!sbIsSameSize(gMmceSupport.prefix, gMmceSupport.ulSizePrev))
        result = 1;

    // Themes/Languages registration moved ABOVE the VCD-view early returns (#152) -- see the block
    // after the NOUPDATE latch near the top of this function.

    // Create the library folders once per card/slot, not on every refresh (each is an SIO2 mkdir).
    // Only latch the "done" memo once CFG actually EXISTS on the card: sbCreateFolders' mkdir burst
    // ignores its return, so a single mkdir dropped on a busy card would otherwise mark the tree
    // "created" forever while CFG never got made -- and every per-game save then fails, because the
    // config write targets <prefix>CFG/<id>.cfg (#245, AndrewBento). Leaving the memo unset here lets
    // the ~2s refresh keep retrying until CFG is confirmed present, so a missing folder always heals
    // itself instead of stranding saves.
    if (strcmp(gMmceSupport.foldersCreatedFor, gMmceSupport.prefix) != 0) {
        sbCreateFolders(gMmceSupport.prefix, 1);

        char cfgPath[sizeof(gMmceSupport.prefix) + 4];
        snprintf(cfgPath, sizeof(cfgPath), "%sCFG", gMmceSupport.prefix);
        DIR *cfgDir = opendir(cfgPath);
        if (cfgDir != NULL) {
            closedir(cfgDir);
            snprintf(gMmceSupport.foldersCreatedFor, sizeof(gMmceSupport.foldersCreatedFor), "%s", gMmceSupport.prefix);
            gMmceSupport.folderRetries = 0;
        } else if (++gMmceSupport.folderRetries >= MMCE_FOLDER_RETRY_MAX) {
            // CFG is OBSTRUCTED, not merely missing: mkdir + opendir have now both failed repeatedly
            // (a non-directory entry named CFG, or on-card FS damage to that dir entry -- e.g. a
            // PC-side deletion that left a broken record). Retrying forever would churn the shared
            // SIO2 bus with a 10-mkdir burst PLUS a forced full list rescan every ~2s for the whole
            // session (adversarial review of #246), so latch and stop. The user is NOT left stranded:
            // the write-time parent-create in checkFile still attempts CFG on every save, and the
            // save's failure toast names the exact path + errno -- the card needs a PC-side look.
            LOG("MMCE: %s still absent after %d create attempts -- obstructed; giving up for this session\n",
                cfgPath, MMCE_FOLDER_RETRY_MAX);
            snprintf(gMmceSupport.foldersCreatedFor, sizeof(gMmceSupport.foldersCreatedFor), "%s", gMmceSupport.prefix);
            gMmceSupport.folderRetries = 0;
        } else {
            // Keep the ~2s background refresh alive so the create genuinely retries -- the NOUPDATE
            // latch at the top of this function would otherwise settle the callback to 0
            // and the "retry next refresh" never happens. Mirrors the first-scan retry pattern above.
            LOG("MMCE: %s not present after sbCreateFolders -- re-arming retry (%d/%d)\n",
                cfgPath, gMmceSupport.folderRetries, MMCE_FOLDER_RETRY_MAX);
            (*mmceInstance()).updateDelay = MMCE_MODE_UPDATE_DELAY;
            result = 1;
        }
    }

    return result;
}

int MmceSource::update(item_list_t *itemList)
{
    int view = host::library().listViewActive(itemList);
    int result = 0;
    if (gMmceSupport.prefix[0] == '\0') {
        // Card absent / slot unresolved (Auto mode after removal). Actually CLEAR the list rather
        // than returning the stale count: needsUpdate keeps reporting "update needed" while
        // gameCount > 0, so returning the stale count here leaves the removed card's games on
        // screen and spins a menu-rebuild + apps-rescan + favourites-reload loop every ~2s. Freeing
        // lets updateMenuFromGameList empty the menu and drives needsUpdate's (count > 0) test to 0.
        if (gMmceSupport.games != NULL) {
            free(gMmceSupport.games);
            gMmceSupport.games = NULL;
        }
        if (gMmceSupport.ps1Games != NULL) {
            free(gMmceSupport.ps1Games);
            gMmceSupport.ps1Games = NULL;
        }
        gMmceSupport.gameCount = 0;
        gMmceSupport.ps1GameCount = 0;
        gMmceSupport.ulSizePrev = -2; // force a fresh scan when a card returns
        gMmceSupport.ps1Scanned = 0;
        gMmceSupport.ps1ScanFailed = 0;
        gMmceSupport.ps1ScanRetries = 0;
        return 0;
    }

    // Each view scans into its OWN array (#120): a failed rescan preserves only that view's last-good and
    // can never resurrect the other view's list (see the ps1Games member comment in include/mmceint.h).
    if (view == LIB_VIEW_PS1 || view == LIB_VIEW_MIXED) {
        // ONE list, BOTH cores -- POPS/*.VCD unioned with EMBER/games/*, both at the card root.
        char ps1Root[sizeof(gMmceSupport.prefix)];
        gMmceSupport.getDeviceRoot(ps1Root, sizeof(ps1Root));
        int r = ps1FillGameList(ps1Root, &gMmceSupport.ps1Games);
        if (r >= 0) { // r < 0: transient scan failure (contended bus) -> keep the last-good VCD list
            gMmceSupport.ps1Scanned = 1;
            gMmceSupport.ps1GameCount = r;
            gMmceSupport.ps1ScanFailed = 0;
            gMmceSupport.ps1ScanRetries = 0;
        } else {
            gMmceSupport.ps1Scanned = 0;
            gMmceSupport.ps1ScanFailed = 1; // arm needsUpdate's bounded retry (S6)
        }
        result += gMmceSupport.ps1GameCount;
    }
    if (view == LIB_VIEW_ISO || view == LIB_VIEW_MIXED) {
        sbReadList(&gMmceSupport.games, gMmceSupport.prefix, folderGetSub(itemList->mode), &gMmceSupport.ulSizePrev, &gMmceSupport.gameCount);
        result += gMmceSupport.gameCount;
    }
    return result;
}

int MmceSource::count(item_list_t *itemList)
{
    int view = host::library().listViewActive(itemList);
    return view == LIB_VIEW_MIXED ? gMmceSupport.gameCount + gMmceSupport.ps1GameCount :
           view == LIB_VIEW_PS1   ? gMmceSupport.ps1GameCount :
                                    gMmceSupport.gameCount;
}

int MmceSource::rowView(item_list_t *itemList, int id)
{
    int view = host::library().listViewActive(itemList);
    if (view == LIB_VIEW_MIXED)
        return id >= 0 && id < gMmceSupport.gameCount ? LIB_VIEW_ISO : LIB_VIEW_PS1;
    return view;
}

int MmceSource::sourceId(item_list_t *itemList, int id)
{
    return host::library().listViewActive(itemList) == LIB_VIEW_MIXED && id >= gMmceSupport.gameCount ? id - gMmceSupport.gameCount : id;
}

base_game_info_t *MmceSupport::activeGame(item_list_t *itemList, int id)
{
    int vcd = (mmceInstance()->rowView(itemList, id) == LIB_VIEW_PS1);
    id = mmceInstance()->sourceId(itemList, id);
    base_game_info_t *arr = vcd ? ps1Games : games;
    int count = vcd ? ps1GameCount : gameCount;
    if (arr == NULL || id < 0 || id >= count)
        return &emptyGame;
    return &arr[id];
}

void *MmceSource::game(item_list_t *itemList, int id)
{
    return (void *)gMmceSupport.activeGame(itemList, id);
}

char *MmceSource::name(item_list_t *itemList, int id)
{
    return gMmceSupport.activeGame(itemList, id)->name;
}

int MmceSource::nameLength(item_list_t *itemList, int id)
{
    base_game_info_t *g = gMmceSupport.activeGame(itemList, id);
    return ((g->format != GAME_FORMAT_USBLD) ? ISO_GAME_NAME_MAX + 1 : UL_GAME_NAME_MAX + 1);
}

char *MmceSource::startup(item_list_t *itemList, int id)
{
    // VCD view keys per-game data (CFG/art) off the VCD filename, not a disc ID (see sbPopulateConfig).
    base_game_info_t *g = gMmceSupport.activeGame(itemList, id);
    if (rowView(itemList, id) == LIB_VIEW_PS1)
        return g->name;
    return g->startup;
}

void MmceSource::deleteGame(item_list_t *itemList, int id)
{
    if (rowView(itemList, id) == LIB_VIEW_PS1)
        return; // #120: a VCD is not an ISO game -- no delete in VCD view
    if (gMmceSupport.activeGame(itemList, id) == &gMmceSupport.emptyGame)
        return;                                   // stale/invalid id: sbDelete does NOT bounds-check, so avoid an OOB/NULL deref + wrong unlink
    sbSetBrowseSub(folderGetSub(itemList->mode)); // delete inside the current subfolder, not the root
    id = sourceId(itemList, id);
    sbDelete(&gMmceSupport.games, gMmceSupport.prefix, "/", gMmceSupport.gameCount, id);
    gMmceSupport.ulSizePrev = -2;
}

void MmceSource::rename(item_list_t *itemList, int id, char *newName)
{
    if (rowView(itemList, id) == LIB_VIEW_PS1) {
        base_game_info_t *game = gMmceSupport.activeGame(itemList, id);

        if (game == &gMmceSupport.emptyGame)
            return; // stale/invalid id
        // The ROW picks the target, exactly as the launch does. Dispatching on the view would send
        // an Ember row to vcdRenameFile, which searches POPS/ for "<name>.VCD" -- absent for an
        // Ember title, so the rename would quietly do nothing; and where a .VCD of the same name
        // exists (a game held for BOTH cores, the case the merged list makes ordinary) it would
        // rename the POPSTARTER file instead. Renaming the wrong file is the worse half of that.
        char ps1Root[sizeof(gMmceSupport.prefix)];
        gMmceSupport.getDeviceRoot(ps1Root, sizeof(ps1Root));
        int renamed = cueIsCueEntry(game) ? cueRenameGame(ps1Root, game->name, newName) : vcdRenameFile(ps1Root, game->name, newName);
        if (renamed == 0) {
            // MMCE normally latches a successful VCD scan under NOUPDATE. Re-arm its normal scan
            // path so the deferred menu update publishes the new filename immediately.
            gMmceSupport.ps1Scanned = 0;
            gMmceSupport.ps1ScanFailed = 0;
            gMmceSupport.ps1ScanRetries = 0;
            (*mmceInstance()).updateDelay = MMCE_MODE_UPDATE_DELAY;
        }
        return;
    }
    if (gMmceSupport.activeGame(itemList, id) == &gMmceSupport.emptyGame)
        return; // stale/invalid id (see deleteGame) -> avoid sbRename OOB
    id = sourceId(itemList, id);
    sbSetBrowseSub(folderGetSub(itemList->mode)); // rename inside the current subfolder, not the root
    sbRename(&gMmceSupport.games, gMmceSupport.prefix, "/", gMmceSupport.gameCount, id, newName);
    gMmceSupport.ulSizePrev = -2;
}

// Launch an Ember (.cue) PS1 title BY NAME -- peer of launchVcd below. The card
// root, where EMBER/ sits next to POPS/. None of the POPSTARTER memory-card preparation applies:
// Ember performs no IOP reset and inherits our live driver stack, so it needs no MC-side driver.
void MmceSource::launchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet)
{
    char emberElf[256], biosPath[288];
    char launchName[CUE_NAME_MAX];
    char ps1Root[sizeof(gMmceSupport.prefix)];

    if (cueName == NULL || cueName[0] == '\0')
        return;
    if (!cueNameLaunchable(cueName)) {
        host::ui().msgBox(_l(_STR_EMBER_BAD_NAME), 0, NULL);
        return;
    }

    host::ui().renderTextScreen(_l(_STR_PLEASE_WAIT));

    // MMCE artwork and this launch share the SIO2 bus -- quiesce before touching the card, exactly
    // as the POPSTARTER leg does.
    if (!cacheAbortMmceImageLoadsTimed(MMCE_ART_ABORT_WAIT_TICKS)) {
        host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
        return;
    }

    gMmceSupport.getDeviceRoot(ps1Root, sizeof(ps1Root));

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
    // The scan lists folders without reading inside them -- that would be a directory read per row
    // on every refresh. Pay for it once, HERE, while a dialog can still be drawn: an empty or
    // mis-filled folder otherwise drops the user into the PS1 BIOS shell with no explanation.
    // Leave Ember's display marker before the handoff, like the BDMA equip does for POPSTARTER.
    // Best-effort: never a launch gate.
    cueApplySettings(ps1Root, cueName, configSet);

    if (!cueGameHasImage(ps1Root, cueName)) {
        host::ui().msgBox(_l(_STR_EMBER_NO_DISC), 0, NULL);
        return;
    }

    // Launch metadata, not Ember identity: resolve the GameID from the real image while the device
    // and the GUI are still live. A miss never blocks the launch.
    cuePrepareRetroGemBarcode(ps1Root, cueName);

    // UNMOUNT_EXCEPTION is load-bearing: Ember cannot remount the card it reads the game from.
    host::app().deinit(UNMOUNT_EXCEPTION, itemList->mode);
    sysLaunchEmber(emberElf, launchName);
}

// Launch a PS1/.VCD entry BY NAME via POPSTARTER (view-independent entry point: the in-view menu
// launch below and the Favourites tab both use it). gMmceSupport.prefix persists; UNMOUNT_EXCEPTION keeps the
// MMCE device mounted across the IOP reset.
void MmceSource::launchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet)
{
    char vcdElf[256], vcdSelector[320];
    char ps1Root[sizeof(gMmceSupport.prefix)];

    if (vcdName == NULL || vcdName[0] == '\0' || !strcasecmp(vcdName, "POPSTARTER")) // reserved-name belt: the scanner no longer lists it (#154); strcasecmp -- FAT is case-insensitive
        return;

    // Present preparation indicator before potentially expensive memory-card file IO
    host::ui().renderTextScreen(_l(_STR_PLEASE_WAIT));

    // Quiesce in-flight MMCE art reads before touching the card. MMCE artwork and VCD prep share the SIO2 bus.
    if (!cacheAbortMmceImageLoadsTimed(MMCE_ART_ABORT_WAIT_TICKS)) {
        host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
        return;
    }

    gMmceSupport.getDeviceRoot(ps1Root, sizeof(ps1Root));

    if (!vcdResolvePopstarter(ps1Root, vcdElf, sizeof(vcdElf))) {
        vcdDescribePopstarterLookup(ps1Root, vcdElf, sizeof(vcdElf));
        host::ui().missingFile(_l(_STR_POPSTARTER_NOT_FOUND), vcdElf);
        return;
    }
    if (!vcdConfirmPopsFiles(ps1Root)) // before the card is prepared: a missing emulator is a black screen
        return;
    vcdBuildSelector(ps1Root, VCD_PREFIX_MASS, vcdName, vcdSelector, sizeof(vcdSelector));

    // Source MC-side externals from this MMCE card's direct POPS/ folder; never overwrite card files.
    (void)vcdInstallPopstarterMc(ps1Root);

    // Best-effort card prep: BDMA prep is card preparation, never a POPSTARTER launch gate.
    vcdEnsureBdmaForLaunch(VCD_BDMA_SRC_MMCE, VCD_BDMA_MMCE);

    char vcdFullPath[256];
    snprintf(vcdFullPath, sizeof(vcdFullPath), "%sPOPS/%s.VCD", ps1Root, vcdName);
    if (!vcdConfirmCleanLaunch(ps1Root)) // a module left by a tracked USB launch reports stale telemetry
        return;
    vcdPrepareRetroGemBarcode(vcdFullPath);
    // Keep the MMCE game device plus any distinct custom POPSTARTER.ELF backend through handoff.
    host::app().deinitEx(sbLoaderDeinitException(vcdElf), itemList->mode, host::app().pathToMode(vcdElf));
    sysLaunchPopstarter(vcdElf, vcdSelector);
}

static int mmceMcemuSlots[2] = {-2, -2};

void MmceSource::launch(item_list_t *itemList, int id, config_set_t *configSet)
{
    int index, compatmask = 0;
    int EnablePS2Logo = 0;
    int result;

    char partname[256], filename[32];
    base_game_info_t *game;
    struct cdvdman_settings_mmce *settings;
    u32 layer1_start, layer1_offset;
    unsigned short int layer1_part;

    // No Autolaunch yet
    if (gAutoLaunchBDMGame == NULL) {
        game = gMmceSupport.activeGame(itemList, id);
        if (game == &gMmceSupport.emptyGame)
            return; // stale/invalid id (see MmceSupport::activeGame) -> nothing to launch
    } else
        game = gAutoLaunchBDMGame;

    // Folder browsing: a folder row is never launched (the dispatch descends first); guard defensively
    // and pin the path composers to the current subfolder so a nested game resolves.
    if (game != NULL && game->format == GAME_FORMAT_FOLDER)
        return;
    sbSetBrowseSub(folderGetSub(itemList->mode));

    // Quiesce every in-flight MMCE art read BEFORE either launch path touches the card. The VCD
    // handoff below resolves POPSTARTER and may equip BDMA modules -- real reads/writes on the SAME
    // shared mmceman SIO2 channel -- and its early return used to run BEFORE this guard, so a VCD
    // launch could collide with the art worker mid-read (FifthFox: "bombed one launch of a VCD on
    // the MMCE"; the disc path below has always quiesced first). The by-name handoff (ccd1d7a4)
    // landed AFTER the quiesce existed and slotted in above it -- ordering bug since, probabilistic
    // by nature, which is why it "worked until it didn't". Idea source: PR #236's quiesce-reorder,
    // vetted against this tree and re-landed with the full #120 rationale kept.
    if (!cacheAbortMmceImageLoadsTimed(MMCE_ART_ABORT_WAIT_TICKS)) {
        // The card is still busy. Leave the menu and worker intact so the user
        // can retry after the current read returns.
        host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
        diagLog("launch: %s from MMCE refused: an art read would not stop", game != NULL ? game->name : "?");
        return;
    }
    if (game != NULL) {
        diagLog("launch: %s (%s) from MMCE, Game ID %s", game->name, game->startup, gMMCEEnableGameID ? "on" : "off");
        diagSaveBeforeLaunch(); // Debug on only: a launch that hangs still leaves its steps in DIAG.TXT
    }

    // VCD view: hand off to POPSTARTER (by name) instead of the disc path below. Menu-launch only.
    if (gAutoLaunchBDMGame == NULL && game != NULL && (rowView(itemList, id) == LIB_VIEW_PS1)) {
        // The ROW picks the core, never the page -- both kinds share this one PS1 list.
        if (cueIsCueEntry(game))
            launchCue(itemList, game->name, configSet);
        else
            launchVcd(itemList, game->name, configSet);
        return;
    }

    // (The MMCE art quiesce runs ABOVE the VCD handoff now -- both launch paths are covered by the
    // single guard before any card IO.)
    void *irx = &mmce_cdvdman_irx;
    int irx_size = size_mmce_cdvdman_irx;
    compatmask = sbPrepare(game, configSet, irx_size, static_cast<void **>(irx), &index);
    if (compatmask < 0) // sbPrepare failed (patch zone not found): `index` is unset -- bail before using
        return;         // it. (The old `if (settings == NULL)` guard was dead: irx + index is never NULL.)
    settings = (struct cdvdman_settings_mmce *)((u8 *)irx + index);

    // Persist last-played BEFORE any card switch below: on an FMCB-on-MMCE setup this write goes to
    // the card's mcN: surface, and after a GameID switch it would land inside the per-game virtual
    // card instead of the boot card (adversarial review of the native-send re-land).
    if (gRememberLastPlayed) {
        configSetStr(configGetByType(CONFIG_LAST), "last_played", game->startup);
        host::app().saveConfig(CONFIG_LAST, 0);
    }

    // Native-core GameID (AndrewBento, Gen2: with PS2 Logo off nothing ever names the game to the
    // card, so its per-game save folder never engages). Send the 0x8 push HERE -- before ANY card-side
    // fd exists (the VMC fds and the ISO fd below all traverse the switched card) -- matching the
    // Neutrino leg's proven close-fds -> send -> settle ordering and NHDDL's zero-mmce-traffic-after-
    // switch rule. The historical #50 freeze came from the OPPOSITE shape: a send placed LAST (after
    // every fd was captured against the pre-switch surface) in a build whose card busy-wait had been
    // gutted to zero (see the corrected note below, near the launch tail). Per-slot Δ3 parity: a slot
    // whose per-game VMC will be covered by mcemu keeps its card (the folder is moot for it; saves go
    // to the VMC file), enforced via the mask. The Neutrino leg keeps its own send; a Neutrino config
    // that falls back to native (bad ZSO / neutrino.elf missing) keeps today's no-send behavior.
    {
        int coreLoaderEarly = gDefaultCoreLoader;
        configGetInt(configSet, CONFIG_ITEM_CORE_LOADER, &coreLoaderEarly);
        if (!coreLoaderEarly) {
            sbEnsureIgrUsbDrivers(compatmask); // the OPL core's IGR path; must precede the switch below
            char vmcNameEarly[32];
            int vmcMask = 0, vs;
            for (vs = 0; vs < 2; vs++) {
                configGetVMC(configSet, vmcNameEarly, sizeof(vmcNameEarly), vs);
                if (vmcNameEarly[0])
                    vmcMask |= (1 << vs);
            }
            // protectMcPath=NULL: the native path loads no ELF from mcN: mid-launch (ee_core is
            // embedded; sysLaunchLoaderElf reads rom0: only).
            int gameIdSwitched = mmceSendGameID(game->startup, NULL, vmcMask);
            if (gameIdSwitched < 0)
                gMmceSupport.waitSwapDone("unconfirmed switch"); // the game is ON this card: wait the swap out
            if (gameIdSwitched)
                gMmceSupport.settleAfterSwitch();
            // The OSD history record. sysLaunchLoaderElf used to write it right before the IOP reset, for every
            // device. An MMCE with its own Game ID detection on (the feature is meant to work this way) watches
            // that write and switches to the game's folder about 2 s later: during ee_core's start, with the
            // card answering nothing while it swaps and the ISO the game streams from ON that card. The OPL core
            // then never got its disc: a black screen, or the PS2 logo and nothing after it. Keep the write and
            // the switch, but make them happen HERE, before any card-side fd exists, and wait the switch out on
            // screen exactly like a Game ID one (the card's busy query, or its silence). A card that already
            // switched for our Game ID send is on that folder and will not switch again: no wait.
            char historyName[32];
            if (configGetStrCopy(configSet, CONFIG_ITEM_ALTSTARTUP, historyName, sizeof(historyName)) == 0)
                strcpy(historyName, game->startup);
            if (sysAddHistoryRecord(historyName)) { // no record (debug builds): nothing for the card to react to
                char historyDevice[8];
                gMmceSupport.getDeviceRoot(historyDevice, sizeof(historyDevice));
                if (gMmceSupport.waitHistorySwitch(historyDevice, "history write"))
                    gMmceSupport.settleAfterSwitch(); // the fs surface came back with the new card: let it answer first
            }
            diagSaveBeforeLaunch(); // again, now with the Game ID switch, the history write and the card's waits in it
        }
    }

    char vmc_name[32];
    char vmc_path[256];
    int vmc_size_mb;
    int vmc_id, size_mcemu_irx = 0;
    int vmc_fd;
    int vmc_fds[2] = {-1, -1}; // track VMC fds to close on the Neutrino handoff path (B3)
    mmce_vmc_infos_t mmce_vmc_infos;
    vmc_superblock_t vmc_superblock;

    for (vmc_id = 0; vmc_id < 2; vmc_id++) {
        memset(&mmce_vmc_infos, 0, sizeof(mmce_vmc_infos));
        configGetVMC(configSet, vmc_name, sizeof(vmc_name), vmc_id);
        if (vmc_name[0]) {
            vmc_size_mb = sysCheckVMC(gMmceSupport.prefix, "/", vmc_name, 0, &vmc_superblock);
            if (vmc_size_mb > 0) {
                mmce_vmc_infos.flags = vmc_superblock.mc_flag & 0xFF;
                mmce_vmc_infos.flags |= 0x100;
                mmce_vmc_infos.specs.page_size = vmc_superblock.page_size;
                mmce_vmc_infos.specs.block_size = vmc_superblock.pages_per_block;
                mmce_vmc_infos.specs.card_size = vmc_superblock.pages_per_cluster * vmc_superblock.clusters_per_card;

                snprintf(vmc_path, sizeof(vmc_path), "%sVMC/%s.bin", gMmceSupport.prefix, vmc_name);

                vmc_fd = fileXioOpen(vmc_path, 0x3, 0666);
                if (vmc_fd >= 0) {
                    vmc_fds[vmc_id] = vmc_fd;
                    mmce_vmc_infos.fd = fileXioIoctl2(vmc_fd, 0x80, NULL, 0, NULL, 0);
                    mmce_vmc_infos.active = 1;
                }
            }
        }

        // This write covers the slot's marker, so its position is found once and reused with a full-payload bound.
        int slotWord = nativeVmcSlotWord(&mmce_mcemu_irx, size_mmce_mcemu_irx, vmc_id, mmceMcemuSlots, sizeof(mmce_vmc_infos_t));
        if (slotWord >= 0) {
            if (mmce_vmc_infos.active)
                size_mcemu_irx = size_mmce_mcemu_irx;
            memcpy(&((u32 *)&mmce_mcemu_irx)[slotWord], &mmce_vmc_infos, sizeof(mmce_vmc_infos_t));
        }
    }

    // Sum the size of all parts, for CDVDMAN's out-of-bounds read emulation (upstream OPL #1763).
    // One part that cannot be opened or sized makes the whole size unknown: a partial total would be
    // too small a bound and refuse reads in the missing part, while an unknown size keeps the driver
    // on the PVD, as before (sbGetMediaLsnCount).
    u64 isoTotalBytes = 0;
    int isoSizeKnown = 1;
    for (int part = 0; part < game->parts; part++) {
        sbCreatePath(game, partname, gMmceSupport.prefix, "/", part);
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
    sbCreatePath(game, partname, gMmceSupport.prefix, "/", 0);

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
            sbCreatePath(game, partname, gMmceSupport.prefix, "/", layer1_part);
            break;
        default: // Raw ISO9660 disc image; one part.
            layer1_part = 0;
            layer1_offset = layer1_start;
    }

    if (sbProbeISO9660(partname, game, layer1_offset) != 0) {
        layer1_start = 0;
        LOG("DVD detected.\n");
    } else {
        layer1_start -= 16;
        LOG("DVD-DL layer 1 @ part %u sector 0x%lx.\n", layer1_part, layer1_offset);
    }
    settings->common.layer1_start = layer1_start;

#ifdef RETROACHIEVEMENTS
    // RA: this game's watch list, settled before sysLaunchLoaderElf reads
    // GetWatchCount() to decide whether the network modules travel with the
    // launch. Absent is normal -- the game is simply not tracked.
    sbLoadWatchList(gMmceSupport.prefix, game->startup);
#endif
    if ((result = sbLoadCheats(gMmceSupport.prefix, game->startup, configSet)) < 0) {
        // #265: let the user back out instead of sitting through the whole load. The helper does
        // the sbUnprepare itself -- see include/supportbase.h; skipping it breaks the NEXT launch.
        if (!sbCheatsMissingContinue(&settings->common, result)) {
            if (vmc_fds[0] >= 0)
                fileXioClose(vmc_fds[0]);
            if (vmc_fds[1] >= 0)
                fileXioClose(vmc_fds[1]);
            return;
        }
    }
    sbLoadImage(gMmceSupport.prefix, game->startup);

    // (last-played persistence hoisted above the GameID switch -- see the native-core send block)

    if (configGetStrCopy(configSet, CONFIG_ITEM_ALTSTARTUP, filename, sizeof(filename)) == 0)
        strcpy(filename, game->startup);


    // MMCEDRV settings
    if (gMMCESlot == 0)
        settings->port = 2;
    else if (gMMCESlot == 1)
        settings->port = 3;
    else if (gMMCESlot == 2) {
        int detectedPort = gMmceSupport.detectSlot();
        if (detectedPort < 0) {
            // Neither slot responded; abort rather than forward port -1 to the IOP.
            LOG("MMCE slot lost, aborting launch\n");
            // Make the bail VISIBLE (HW batch S5: "does gameID and then nothing" with no message).
            // deinit has not run yet, the launch is on the GUI thread, and guiWarning self-guards
            // for autolaunch -- same pattern as the quiesce bail above.
            host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
            // Close the VMC fds opened above so a failed launch does not leak them
            // back to the menu across repeated attempts (Codex audit, Medium 2).
            if (vmc_fds[0] >= 0)
                fileXioClose(vmc_fds[0]);
            if (vmc_fds[1] >= 0)
                fileXioClose(vmc_fds[1]);
            sbUnprepare(&settings->common);
            return;
        }
        settings->port = detectedPort;
        // Re-apply trailing-slash normalization: detectSlot() rewrites
        // prefix via sprintf with no slash append, de-normalizing the
        // value refreshArtRoots() previously set. sbBuildVmcNeutrinoArgs
        // (called below) builds "-mcN=<prefix>VMC/<name>.bin" and requires
        // prefix to end in '/' -- without this call a non-empty gMMCEPrefix
        // (e.g. "GAMES") produces the broken path "mmce0:/GAMESVMC/<name>.bin".
        gMmceSupport.refreshArtRoots();
    }

    // Per-game Neutrino core: gate BEFORE opening iso_file so no fd is leaked on
    // the Neutrino path (game is still valid here for the format check).
    int coreLoader = gDefaultCoreLoader; // no per-game $CoreLoader key -> follow the global default core
    configGetInt(configSet, CONFIG_ITEM_CORE_LOADER, &coreLoader);
    const char *neutrinoPath = NULL;
    char neutrinoExtraArgs[256] = "";              // per-game Neutrino flags; copied before deinit teardown
    int neutrinoVideo = gNeutrinoVideoDefault;     // per-game -gsm video mode; absent key = follow the global; copied before deinit
    int neutrinoGsmComp = gNeutrinoGsmCompDefault; // per-game -gsm ":c" field-flip half; absent key = follow the global; copied before deinit
    neutrino_vmc_args_t neutrinoVmc = {0};         // per-game VMC -mc args; resolved before deinit, lives on this stack frame across the launch (#47)
    if (coreLoader) {
        configGetStrCopy(configSet, CONFIG_ITEM_NEUTRINO_ARGS, neutrinoExtraArgs, sizeof(neutrinoExtraArgs));
        configGetInt(configSet, CONFIG_ITEM_NEUTRINO_VIDEO, &neutrinoVideo);
        configGetInt(configSet, CONFIG_ITEM_NEUTRINO_GSMCOMP, &neutrinoGsmComp);
        neutrinoPath = sbResolveNeutrinoPath(gMmceSupport.prefix); // #300: AUTO also probes this MMCE card for a co-located neutrino.elf
        if (game->format == GAME_FORMAT_USBLD || !strcasecmp(game->extension, ".zso")) {
            // isValidIsoName() admits .zso case-insensitively and game->extension is stored
            // verbatim, so an upper/mixed-case ".ZSO" must reject here too (Neutrino can't run it).
            host::ui().warning(_l(_STR_NEUTRINO_BAD_FORMAT), 6);
            coreLoader = 0;
        } else if (neutrinoPath == NULL) {
            host::ui().warning(_l(_STR_NEUTRINO_NOT_FOUND), 6);
            coreLoader = 0;
        }
        // Falling back to the OPL core: the native branch above skipped the IGR driver check, and no
        // card switch has happened yet on this path, so mc0: is still the boot card.
        if (!coreLoader)
            sbEnsureIgrUsbDrivers(compatmask);

        // VMC -> neutrino (#47): resolve any per-game VMC into discrete -mc0/-mc1 argv entries
        // (prefix ends in '/'); not the whitespace-tokenized extra-args buffer (spaced names).
        if (coreLoader)
            sbBuildVmcNeutrinoArgs(configSet, gMmceSupport.prefix, &neutrinoVmc);
    }
    if (coreLoader) {
        char mmcePartname[256];
        snprintf(mmcePartname, sizeof(mmcePartname), "%s", partname); // defensive copy across the deinit teardown (partname is a stack buffer, not freed by deinit)
        // game (== &gMmceSupport.games[id]) is freed by deinitEx() below (moduleCleanup -> cleanUp frees
        // gMmceSupport.games), but sysLaunchNeutrino still reads game->startup afterwards to build its -elf
        // argument -- a use-after-free read. Copy startup onto this stack frame like mmcePartname.
        char mmceStartup[GAME_STARTUP_MAX + 1];
        snprintf(mmceStartup, sizeof(mmceStartup), "%s", game->startup);
        // sbPrepare above resolved the OPL core's modes; a title default can be limited to one core.
        compatmask = sbGetCompatModes(configSet, 1);
        // Neutrino bypasses OPL's mcemu, so the VMC fds opened above go unused on this path --
        // close them instead of leaking until the IOP reset (B3). Closed BEFORE the GameID push
        // below (Beta-2947 hardware report): the 0x8 switch physically re-mounts the card, and
        // closing a handle opened against the PRE-switch filesystem afterwards wedged mmceman --
        // the GUI froze the instant the GameID appeared on the card. NHDDL's ordering has ZERO
        // mmce filesystem traffic after its mmceMountVMC; match it as closely as we can.
        if (vmc_fds[0] >= 0)
            fileXioClose(vmc_fds[0]);
        if (vmc_fds[1] >= 0)
            fileXioClose(vmc_fds[1]);
        // Every way back to the menu from here undoes the native preparation this leg shares
        // (sbPrepare above): a zone left patched fails the next MMCE launch's sbPrepare.
        if (sysNeutrinoPreflight("mmce", neutrinoPath, 0, NULL, -1) < 0) { // D6 pre-teardown validation
            sbUnprepare(&settings->common);
            return;
        }
        // Cheats: loaded above with the native preparation; hand them to Neutrino.
        if (sbNeutrinoHandCheats(mmceStartup, neutrinoPath, neutrinoExtraArgs) < 0) {
            sbUnprepare(&settings->common);
            return;
        }
#ifdef RETROACHIEVEMENTS
        sbNeutrinoPrepareRA(gMmceSupport.prefix, mmceStartup, "mmce", neutrinoPath, neutrinoExtraArgs);
#endif
        if (sysNeutrinoArgsPreflight("mmce", mmcePartname, mmceStartup, compatmask, EnablePS2Logo, neutrinoPath, neutrinoExtraArgs, neutrinoVideo, neutrinoGsmComp, 0, -1, &neutrinoVmc) < 0) {
            sbUnprepare(&settings->common);
            return;
        }
        sysNeutrinoWarnPadEmu(configSet);
        // GameID for the NEUTRINO core (issue #68): the native OPL-core launch deliberately does
        // NOT push a launcher GameID (see the issue-#50 note below -- in OPL core the in-game
        // card is OPL's mcemu, and a mid-launch re-switch froze early-MC-probing games). That
        // reasoning does NOT extend to Neutrino: it has no mcemu -- the game talks to the card's
        // REAL emulated-MC surface -- and nothing else ever tells the card which game is
        // starting, so its per-game folder never engaged (USB-hosted games via Neutrino DID work:
        // the cross-device paths all send it). NHDDL does exactly this before launching neutrino
        // (mmceMountVMC). mmceSendGameID waits out the card's busy bit (bounded ~3 s); the
        // MC-hosted-neutrino protect guard is inside the helper (skips + warns when neutrinoPath
        // sits on this slot's mcN:).
        int gameIdSwitched = mmceSendGameID(game->startup, neutrinoPath,
                                            (neutrinoVmc.arg[0][0] ? 1 : 0) | (neutrinoVmc.arg[1][0] ? 2 : 0)); // Δ3: -mc-covered slots keep their card
        // Fs-settle after a card switch (shared helper; see MmceSupport::settleAfterSwitch). The game on this
        // leg is ALWAYS mmce-hosted, so after any actual switch both our own reads below (neutrino.elf
        // load, ISO open for the keep-IOP handoff) AND Neutrino's own post-reset mmceman read hit the
        // just-switched card -- wait for the switched slot's fs to answer first (issue #56/#68).
        if (gameIdSwitched < 0)
            gMmceSupport.waitSwapDone("unconfirmed switch"); // the game is ON this card: wait the swap out
        if (gameIdSwitched)
            gMmceSupport.settleAfterSwitch();
        diagSaveBeforeLaunch(); // again, now with the Game ID switch and the card's wait in it
        // Neutrino keep-IOP handoff (sysLoadELFKeepIOP): Neutrino opens the mmce-hosted game through
        // OUR mmceman mount and its config/modules from the neutrino.elf device (-cwd) before its own
        // IOP reset -- keep BOTH mounted. An MC-hosted neutrino needs no exception (-1 second slot).
        int neutrinoDevMode = host::app().pathToMode(neutrinoPath);
        // Last look before the handoff, as on the native leg: a switch the firmware deferred must not be in
        // flight when Neutrino's mmceman takes the card over. ~0 ms on an idle card.
        gMmceSupport.waitBeforeHandoff();
        host::app().deinitEx(sbNeutrinoDeinitException(neutrinoPath), itemList->mode, neutrinoDevMode);
        sysLaunchNeutrino("mmce", mmcePartname, mmceStartup, compatmask, EnablePS2Logo, neutrinoPath, neutrinoExtraArgs, neutrinoVideo, neutrinoGsmComp, 0 /* #11: mmce is fileid, no fs layer */, -1, &neutrinoVmc);
        return;
    }

    // Poll-first: try once; on failure retry briefly. Covers a card re-mount that completes just past
    // the settle window (the settle's Dopen probes can burn their whole budget on a recovering channel).
    // A healthy card pays ~0 ms (first try succeeds); a dead one pays a bounded ~2 s before a VISIBLE
    // bail -- the old path returned to the menu with only a LOG, which read on HW as "does gameID and
    // then nothing" (batch S5).
    int iso_file = fileXioOpen(partname, 0x1, 0666);
    for (int isoRetry = 0; iso_file < 0 && isoRetry < 10; isoRetry++) {
        DelayThread(200 * 1000);
        iso_file = fileXioOpen(partname, 0x1, 0666);
    }
    if (iso_file < 0) {
        LOG("Failed to open iso %s (ret %d), aborting\n", partname, iso_file);
        // Same VMC-fd leak guard as the slot-lost path above (Codex audit, Medium 2).
        if (vmc_fds[0] >= 0)
            fileXioClose(vmc_fds[0]);
        if (vmc_fds[1] >= 0)
            fileXioClose(vmc_fds[1]);
        host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8); // batch S5: never bail silently
        sbUnprepare(&settings->common);
        return;
    }

    settings->ack_wait_cycles = gMMCEAckWaitCycles;
    settings->use_alarms = gMMCEUseAlarms;

    // TEMP: The fd given by sd2psx is not the same one we see here on the EE
    // and ps2sdk_get_iop_fd does not seem to return the right value either
    settings->iso_fd = fileXioIoctl2(iso_file, 0x80, NULL, 0, NULL, 0);

    LOG("name: %s\n", game->name);
    LOG("start: %s\n", game->startup);

    // Issue #50, CORRECTED HISTORY (2026-07-13 archaeology): the native GameID send was NOT the bug.
    // Native MMCE launches sent SET_GAMEID from the first MMCE support (incl. the tester's known-good
    // beta 2257, which waited up to 15 s for the card) -- the freeze regression (2257 -> 2813,
    // ramonesfm, SCPH-30001 + Gen2) came from the busy-wait being gutted to nopdelay() (68bf1a73) AND
    // the send sitting LAST, after every card-side fd was captured against the pre-switch surface.
    // The wait has since been restored (aa780f24/847cc620, poll-first ~3 s) and the send re-landed
    // ABOVE (before any fd exists, with the fs-settle) -- the mcemu rationale only ever held when a
    // per-game VMC was configured (no VMC -> mcemu never loads and the game talks to the REAL card,
    // where the per-game folder genuinely matters: AndrewBento's logo-off report). The cross-device
    // paths (bdm/hdd/eth) send it too, with the same wait.

    // Last look before the handoff: a switch the firmware deferred (its buttons held, a flush) or that a
    // card-side history refresh queued must not be in flight when mmcedrv takes the ISO handle over. On
    // the game's own slot when no Game ID was sent (see waitBeforeHandoff).
    gMmceSupport.waitBeforeHandoff();

    if (gAutoLaunchBDMGame == NULL) {
        host::app().deinit(NO_EXCEPTION, MMCE_MODE); // CAREFUL: deinit will call cleanUp, so gMmceSupport.games/game will be freed
    }

    /* No autolaunch yet
    else {
        miniDeinit(configSet);

        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;
    }*/

    settings->common.zso_cache = 0;

    sysLaunchLoaderElf(filename, "MMCE_MODE", irx_size, static_cast<void **>(irx), size_mcemu_irx, mmce_mcemu_irx, EnablePS2Logo, compatmask);
}

config_set_t *MmceSource::config(item_list_t *itemList, int id)
{
    // Config (CFG + #Format/#System/#DiscType badges) comes from the ACTIVE view's array;
    // gMmceSupport.activeGame picks it (VCD keys off the basename) and returns a safe empty entry
    // for any stale/invalid id, so this can never index the wrong/NULL array out of range.
    base_game_info_t *game = gMmceSupport.activeGame(itemList, id);
    config_set_t *config = sbPopulateConfig(game, gMmceSupport.prefix, "/");
    ps1Size(itemList, game, config);
    return config;
}

void MmceSource::ps1Size(item_list_t *, base_game_info_t *game, config_set_t *config)
{
    char root[sizeof(gMmceSupport.prefix)];
    gMmceSupport.getDeviceRoot(root, sizeof(root)); // POPS/ and EMBER/ live at the card root
    sbResolvePs1Size(game, root, config);
}

int MmceSource::image(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    int r = mmceTryLoadImage(gMmceSupport.artPrimary, folder, isRelative, value, suffix, resultTex);
    // ART LIVES IN THE ART FOLDER, and nowhere else. A PS1 cover is
    // ART/<name>_COV.png -- the same rule as a PS2 title -- which the texDiscoverLoad
    // above already tried. A miss is simply "no cover": there is no second directory to
    // search, which also means a missing cover costs one failed open instead of several.
    return r;
}

int MmceSource::textId(item_list_t *itemList)
{
    int mode = _STR_MMCE_GAMES;

    return mode;
}

int MmceSource::iconId(item_list_t *itemList)
{
    int mode = MMCE_ICON;

    return mode;
}

// This may be called, even if init() was not.
void MmceSource::cleanUp(item_list_t *itemList, int exception)
{
    if ((*mmceInstance()).enabled) {
        LOG("MMCESUPPORT CleanUp\n");

        free(gMmceSupport.games);
        gMmceSupport.games = NULL;
        free(gMmceSupport.ps1Games); // #120: free the separate VCD array too; NULL both (CleanUp + Shutdown both run)
        gMmceSupport.ps1Games = NULL;

        //      if ((exception & UNMOUNT_EXCEPTION) == 0)
        //          ...
    }
}

// This may be called, even if init() was not.
void MmceSource::shutdown(item_list_t *itemList)
{
    if ((*mmceInstance()).enabled) {
        LOG("MMCESUPPORT Shutdown\n");

        free(gMmceSupport.games);
        gMmceSupport.games = NULL;
        free(gMmceSupport.ps1Games);
        gMmceSupport.ps1Games = NULL;
    }

    // As required by some (typically 2.5") HDDs, issue the SCSI STOP UNIT command to avoid causing an emergency park.
    // fileXioDevctl("mass:", USBMASS_DEVCTL_STOP_ALL, NULL, 0, NULL, 0);
}

int MmceSource::checkVmc(item_list_t *itemList, char *name, int createSize)
{
    return sysCheckVMC(gMmceSupport.prefix, "/", name, createSize, NULL);
}

char *MmceSource::prefix(item_list_t *itemList)
{
    return gMmceSupport.prefix;
}


int MmceSource::ps1SaveDir(item_list_t *itemList, int id, const char *name, int *ember, char *directory, int directorySize,
                           char *partition, int partitionSize)
{
    char root[64];
    (void)partitionSize;
    if (!ps1RowKind(itemList, id, ember))
        return 0;
    partition[0] = '\0';
    gMmceSupport.getDeviceRoot(root, sizeof(root));
    return saveIconPs1Directory(root, name, *ember, directory, directorySize);
}

int MmceSource::saveCueSettings(item_list_t *itemList, int id, const char *name, config_set_t *configSet)
{
    char ps1Root[64];
    (void)itemList;
    (void)id;

    if (!cacheAbortMmceImageLoadsTimed(MMCE_ART_ABORT_WAIT_TICKS))
        return 0;
    gMmceSupport.getDeviceRoot(ps1Root, sizeof(ps1Root));
    return cueSaveGameSettings(ps1Root, name, configSet);
}
