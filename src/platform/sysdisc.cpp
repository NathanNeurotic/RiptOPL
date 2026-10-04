/*
  Copyright 2009, Ifcaro
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#ifdef __DECI2_DEBUG
#include <iopcontrol_special.h>
#endif

#include <delaythread.h> // DelayThread for the bounded disc re-detect polls in sysLaunchDisc
#include <time.h>        // clock()/CLOCKS_PER_SEC -- the disc waits are budgeted by wall clock

// Disc-probe budgets (issue #465). All are TOTAL wall-clock caps, not per-poll ones.
constexpr int SYS_DISC_DETECT_MS = 1000;  // first "still identifying" settle
constexpr int SYS_DISC_REDETECT_MS = 4000;  // spin-up + re-detect after a NODISC report
// Empty-tray shortcut inside the re-detect budget above. A drive that is actually working on a
// disc reports SCECdDETCT ("identifying") while it spins up; a drive with nothing in it only ever
// answers NODISC. So NODISC that has never once been punctuated by DETCT means an empty tray, and
// waiting out the rest of the re-detect budget only makes the user stare at a frozen-looking menu.
// Generous on purpose: this is a floor for a cold drive to reach DETCT, not a tuned timeout. Once
// DETCT is seen the FULL budget applies again, so a slow-but-working drive is unaffected.
constexpr int SYS_DISC_NODISC_MS = 2000;
#include "include/host.h"
#include "include/opl.h"
#include "include/gui.h"
#include "include/lang.h"       // _l(_STR_...) -- Neutrino preflight abort toasts
#include "include/launchdiag.h" // UDPBD hang-triage stage markers (gated on gLaunchDiag)
#include "include/ethsupport.h"
#include "include/hddsupport.h"
#include "include/util.h"
#include "include/pad.h"
#include "include/system.h"
#ifdef RETROACHIEVEMENTS
#include "include/discsupport.h"
#endif
#include "include/ioman.h"
#include "include/ioprp.h"
#include "include/bdmsupport.h"
#include "include/mmcesupport.h"
#include "include/OSDHistory.h"
#include "include/renderman.h"
#include "include/extern_irx.h"
#include "../ee_core/include/modules.h"
#include "../ee_core/include/coreconfig.h"
#include <osd_config.h>
#include "include/pggsm.h"
#include "include/sema_lock.h"
#include "include/cheatman.h"
#include "include/xparam.h"
#ifdef RETROACHIEVEMENTS
#include "include/rawatch.h"
#include "modules/network/common/ra_snap.h"
#endif

#ifdef PADEMU
#include <libds34bt.h>
#include <libds34usb.h>
#endif

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>     // fileXioInit, fileXioExit, fileXioDevctl
#include <usbhdfsd-common.h> // USBMASS_IOCTL_GET_LBA / _CHECK_CHAIN -- VMC contiguity probe
#include "include/ps2sdkapi_compat.h"       // ps2sdk_get_iop_fd -- the ioctls above take the IOP-side fd

// Launching the disc in the drive: boot-path discovery from SYSTEM.CNF and the settle/redetect budget. Split out of system.cpp.

// Parse SYSTEM.CNF text for "BOOT2 = <path>" and copy the path into out. Returns 0 on success.
// Retail discs are tiny uppercase "KEY = value" files, but stay defensive: match the key
// case-insensitively, require the '=' on the SAME line right after the key (a naive strchr
// could walk across a newline on a malformed file and grab the wrong line's value), and keep
// scanning past substring hits so a "BOOT2" inside another value can't shadow the real key.
static int sysParseBoot2(const char *cnf, char *out, int outSize)
{
    const char *p = cnf;

    if (outSize <= 0)
        return -1;

    while (*p != '\0') {
        if (strncasecmp(p, "BOOT2", 5) == 0) {
            const char *eq = p + 5;
            while (*eq == ' ' || *eq == '\t')
                eq++;
            if (*eq == '=') {
                int i = 0;
                eq++;
                while (*eq == ' ' || *eq == '\t')
                    eq++;
                while (*eq != '\0' && *eq != '\r' && *eq != '\n' && *eq != ' ' && *eq != '\t' && i < outSize - 1)
                    out[i++] = *eq++;
                out[i] = '\0';
                if (*eq != '\0' && *eq != '\r' && *eq != '\n' && *eq != ' ' && *eq != '\t') {
                    out[0] = '\0';
                    return -1;
                }
                return (i > 0) ? 0 : -1;
            }
            p += 5;
        } else {
            p++;
        }
    }
    return -1;
}

// Boot the physical PS2 disc in the drive, always through rom0:PS2LOGO (which performs the disc
// region/auth check). PS2 discs only. Returns a negative code (and stays in OPL) on failure; on
// success it tears OPL down and never returns.
//
// The boot path comes from the disc's OWN SYSTEM.CNF "BOOT2" line -- full PS2BBL/OSDSYS parity.
// A sceCdReadKey(0x004B)-derived name that returns plausible-but-wrong bytes produces a
// nonexistent cdrom0: path, so PS2LOGO shows the logo (the DISC authenticates fine) and falls
// through to OSDSYS. The derivation is kept as the FALLBACK when SYSTEM.CNF is unreadable, and
// as a logged cross-check otherwise.
// One poll interval of a disc wait. With a progress callback the vsync inside it does the waiting
// (and keeps the menu animating); without one we fall back to a plain sleep.
// Returns non-zero when the callback reports the user asked to abort the wait.
static int sysDiscProbeWait(int (*progress)(void))
{
    if (progress != NULL)
        return progress();

    DelayThread(20 * 1000);
    return 0;
}

// Wait out a DETCT ("still identifying") report, bounded by WALL CLOCK rather than by a poll count.
// `cancelled` is set when the user abandoned the wait; `type` is then whatever the drive last said.
static int sysDiscSettle(int (*progress)(void), int budgetMs, int *cancelled)
{
    clock_t deadline = clock() + (clock_t)budgetMs * (CLOCKS_PER_SEC / 1000);
    int type = sceCdGetDiskType();

    while (type == SCECdDETCT && clock() < deadline) {
        if (sysDiscProbeWait(progress)) {
            *cancelled = 1;
            break;
        }
        type = sceCdGetDiskType();
    }
    return type;
}

int sysGetDiscBootPath(char *path, int pathSize, int (*progress)(void))
{
    u8 key[16];
    char boot[16], cnf[1024];
    u32 k32;
    int type, fd, len;
    int cancelled = 0;

    if (path == NULL || pathSize < 64)
        return -3;

    if (sceCdStatus() == SCECdErOPENS) // tray open
        return -1;

    type = sysDiscSettle(progress, SYS_DISC_DETECT_MS, &cancelled);
    if (cancelled)
        return SYS_DISC_CANCELLED;
    // An idle drive spins the disc DOWN and then reports NODISC even with a game disc loaded.
    // The software equivalent of a tray open/close is a TRAY-CLOSE REQUEST on the already-closed
    // tray, which re-runs the detect cycle. A genuinely empty drive still ends at the -2 bail
    // below, just ~4 s later.
    if (type == SCECdNODISC) {
        int spin = 0;
        clock_t deadline;
        u32 traychk = 0;
        int tr = sceCdTrayReq(SCECdTrayClose, &traychk);
        (void)tr; // only read by LOG (a no-op in release builds)
        LOG("[DISC] drive reports NODISC -- tray-close re-detect: req=%d traychk=%lu\n", tr, (unsigned long)traychk);
        sceCdSync(0);

        /* ONE budget for the whole re-detect, not one per poll.

           This used to nest a 1 s DETCT wait inside a 20-iteration loop whose comment claimed a
           "~4 s budget" -- true only if DETCT never appeared. A drive that keeps answering DETCT
           (dirty laser, marginal disc, and routinely an EMPTY tray) turned that into 20 x 1.2 s,
           so selecting Launch Disc with no disc in the tray sat there for up to 24 seconds with a
           frozen UI. That is issue #465's "extended read/poll loop", and the nesting was the whole
           of it: each layer was individually bounded, and the PRODUCT was not. */
        deadline = clock() + (clock_t)SYS_DISC_REDETECT_MS * (CLOCKS_PER_SEC / 1000);
        clock_t nodiscDeadline = clock() + (clock_t)SYS_DISC_NODISC_MS * (CLOCKS_PER_SEC / 1000);
        int sawDetct = 0;
        while (clock() < deadline) {
            spin++;
            type = sceCdGetDiskType();
            if (type != SCECdNODISC && type != SCECdDETCT)
                break;
            if (type == SCECdDETCT)
                sawDetct = 1; // the drive is working on something -- it has earned the full budget
            // Nothing to identify: unbroken NODISC past the shorter cap is an empty tray, and the
            // remaining budget buys only dead time. See SYS_DISC_NODISC_MS.
            if (!sawDetct && clock() >= nodiscDeadline)
                break;
            if (sysDiscProbeWait(progress)) {
                cancelled = 1;
                break;
            }
        }
        LOG("[DISC] after tray-close re-detect: type=%d (%d re-polls, sawDetct=%d, cancelled=%d)\n", type, spin, sawDetct, cancelled);
        if (cancelled)
            return SYS_DISC_CANCELLED;
    }

    if (type != SCECdPS2DVD && type != SCECdPS2CD) // no disc / not a PS2 game disc
        return -2;

    sceCdDiskReady(0);

    // Primary: the disc's SYSTEM.CNF BOOT2 line (what OSDSYS itself boots).
    path[0] = '\0';
    fd = open("cdrom0:\\SYSTEM.CNF;1", O_RDONLY);
    if (fd >= 0) {
        len = read(fd, cnf, sizeof(cnf) - 1);
        close(fd);
        if (len > 0) {
            cnf[len] = '\0';
            if (sysParseBoot2(cnf, path, pathSize) != 0)
                path[0] = '\0';
        }
    }

    // Fallback + cross-check: derive the boot name from the disc key (PS2BBL PS2GetBootFile,
    // non-China path). Best-effort: if BOOT2 was parsed, a key failure no longer matters.
    boot[0] = '\0';
    if (sceCdReadKey(0, 0, 0x004B, key) != 0 && sceCdGetError() == 0) {
        boot[11] = '\0';
        k32 = (key[4] >> 3) | (key[14] >> 3 << 5) | ((key[0] & 0x7F) << 10);
        boot[10] = '0' + (k32 % 10);
        boot[9] = '0' + (k32 / 10 % 10);
        boot[8] = '.';
        boot[7] = '0' + (k32 / 10 / 10 % 10);
        boot[6] = '0' + (k32 / 10 / 10 / 10 % 10);
        boot[5] = '0' + (k32 / 10 / 10 / 10 / 10 % 10);
        boot[4] = '_';
        boot[3] = (key[0] >> 7) | ((key[1] & 0x3F) << 1);
        boot[2] = (key[1] >> 6) | ((key[2] & 0x1F) << 2);
        boot[1] = (key[2] >> 5) | ((key[3] & 0xF) << 3);
        boot[0] = ((key[4] & 0x7) << 4) | (key[3] >> 4);
        if (boot[0] < 'A' || boot[0] > 'Z') // sanity: a real boot name starts with a letter
            boot[0] = '\0';
    }

    if (path[0] == '\0') {
        if (boot[0] == '\0') // neither source produced a boot path
            return -3;
        snprintf(path, pathSize, "cdrom0:\\%s;1", boot);
        LOG("[DISC] SYSTEM.CNF unavailable; using key-derived name\n");
    } else if (boot[0] != '\0' && strstr(path, boot) == NULL) {
        // BOOT2 wins (it is what the browser boots); the mismatch is diagnostic.
        LOG("[DISC] key-derived name %s does not match BOOT2 %s -- booting BOOT2\n", boot, path);
    }

    return 0;
}

int sysLaunchDisc(int (*progress)(void))
{
    char path[64];
    char *args[1];
    int result;
#ifdef RETROACHIEVEMENTS
    if (host::launch().discCheckBusy())
        return -1;
#endif
    result = sysGetDiscBootPath(path, sizeof(path), progress);
    if (result < 0)
        return result;

    LOG("[DISC] booting %s\n", path);

    // Tell the MMCE which game is about to boot (#183). EVERY other launch path already does this --
    // only Launch Disc was missing it. Without this, physical disc launches on a console with a
    // PSX MemCard Gen2 / MMCE never switched to the game's folder and the user simply could not save
    // or load from their card.
    // Disc paths are "cdrom0:\SLUS_123.45;1" (see path[] above) -- strip the prefix and semicolon so
    // we pass the pure id. A naive strchr(';') would also match the ";1" if there is NO slash
    // (a leading slash the MMCE would never match) instead of a clean id. Take whichever separator
    // comes last.
    {
        const char *sep = strrchr(path, '\\');
        const char *fwd = strrchr(path, '/');
        const char *colon = strrchr(path, ':');
        if (fwd > sep)
            sep = fwd;
        if (colon > sep)
            sep = colon;
        const char *idStart = (sep != NULL) ? sep + 1 : path;
        char gameid[32];
        int i = 0;
        while (idStart[i] != '\0' && idStart[i] != ';' && i < (int)sizeof(gameid) - 1) {
            gameid[i] = idStart[i];
            i++;
        }
        gameid[i] = '\0';
        if (gameid[0] != '\0') {
            LOG("[DISC] sending game id '%s' to MMCE\n", gameid);
            mmceSendGameID(gameid, NULL, 0);
        }
    }

    host::app().deinit(NO_EXCEPTION, IO_MODE_SELECTED_ALL); // tear OPL down (mirrors sysExecExit)

    args[0] = path;
    LoadExecPS2("rom0:PS2LOGO", 1, args); // logo performs the disc region/auth check
    return 0;                             // unreachable on success
}
