/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

#include "include/opl.h"
#include "include/gui.h"
#include "include/guiint.h"
#include "include/guioverlay.h"
#include "include/guiframe.h"
#include "include/guisplash.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/ioman.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/pad.h"
#include "include/sound.h" // sfxGetPlayDiag/sfxGetDropDiag -- SFX RPC cost + silent-drop counters for the debug HUD
#include "include/texcache.h"
#include "include/artindex.h"
#include "include/util.h"

#include <stdio.h>
#include <time.h>

#ifdef __DEBUG
extern GSGLOBAL *gsGlobal;
#endif

Overlays gOverlays;

void Overlays::draw()
{
    // are there any pending operations?
    int pending = ioHasPendingRequests();

    if (!pending) {
        // Fade out
        if (busyAlpha_ > 0x00)
            busyAlpha_ -= 0x02;
    } else {
        // Fade in
        if (busyAlpha_ < 0x80)
            busyAlpha_ += 0x02;
    }

    if (!oplIsBootInProgress() && !gSplash.active() && busyAlpha_ > 0x00)
        drawBusy(busyAlpha_);

    drawDebugHud();
    drawAutoStart(pending != 0);
    if (gEnableDebug)
        drawArtHud();
}

void Overlays::drawBusy(int alpha)
{
    if (gTheme->loadingIcon) {
        GSTEXTURE *texture = thmGetTexture(LOAD0_ICON + (guiFrameId >> 1) % gTheme->loadingIconCount);
        if (texture && texture->Mem) {
            u64 mycolor = GS_SETREG_RGBA(0x80, 0x80, 0x80, alpha);
            rmDrawPixmap(texture, gTheme->loadingIcon->posX, gTheme->loadingIcon->posY, gTheme->loadingIcon->aligned, gTheme->loadingIcon->width, gTheme->loadingIcon->height, gTheme->loadingIcon->scaled, mycolor, 0);
        }
    }
}

void Overlays::drawSubMenuHints()
{
    // During a probe wait these hints promise input the menu is not reading; the only live
    // action is the Cancel hint guiRenderProbeFrame draws on this same strip (#465).
    if (probeFrame_)
        return;

    int subMenuHints[2] = {_STR_SELECT, _STR_GAMES_LIST};
    int subMenuIcons[2] = {CIRCLE_ICON, CROSS_ICON};

    int x = guiAlignSubMenuHints(2, subMenuHints, subMenuIcons, gTheme->fonts[0], 12, 2);
    int y = gTheme->usedHeight - 32;

    x = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? subMenuIcons[0] : subMenuIcons[1], subMenuHints[0], gTheme->fonts[0], x, y, gTheme->textColor);
    x += 12;
    guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? subMenuIcons[1] : subMenuIcons[0], subMenuHints[1], gTheme->fonts[0], x, y, gTheme->textColor);
}

void Overlays::drawDebugHud()
{
#ifdef __DEBUG
    char text[20];
    int x = gGuiScreenWidth - 120;
    int y = 15;
    int yadd = 15;

    snprintf(text, sizeof(text), "VRAM:");
    fntRenderString(gTheme->fonts[0], x, y, ALIGN_LEFT, 0, 0, text, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
    y += yadd;

    snprintf(text, sizeof(text), "%dKiB FIXED", gsGlobal->CurrentPointer / 1024);
    fntRenderString(gTheme->fonts[0], x, y, ALIGN_LEFT, 0, 0, text, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
    y += yadd;

    snprintf(text, sizeof(text), "%dKiB TEXMAN", ((4 * 1024 * 1024) - gsGlobal->CurrentPointer) / 1024);
    fntRenderString(gTheme->fonts[0], x, y, ALIGN_LEFT, 0, 0, text, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
    y += yadd;
    y += yadd; // Empty line

    float fps = gFrames.fps();
    if (fps != 0.0f) {
        snprintf(text, sizeof(text), "%.1f FPS", fps);
        fntRenderString(gTheme->fonts[0], x, y, ALIGN_LEFT, 0, 0, text, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
        y += yadd;
    }
#endif
}

void Overlays::drawAutoStart(bool pending)
{
    // Last Played Auto Start
    if (!pending && gRememberLastPlayed && gAutoStartLastPlayed && DisableCron == 0 && gSplash.ended()) {
        if (CronStart == 0) {
            CronStart = clock() / CLOCKS_PER_SEC;
        } else {
            char strAutoStartInNSecs[21];
            clock_t CronCurrent;

            CronCurrent = clock() / CLOCKS_PER_SEC;
            RemainSecs = gAutoStartLastPlayed - (CronCurrent - CronStart);
            snprintf(strAutoStartInNSecs, sizeof(strAutoStartInNSecs), _l(_STR_AUTO_START_IN_N_SECS), RemainSecs);
            fntRenderString(gTheme->fonts[0], gGuiScreenWidth / 2, gGuiScreenHeight / 2, ALIGN_CENTER, 0, 0, strAutoStartInNSecs, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
        }
    }
}

void Overlays::drawArtHud()
{
    // Art pipeline HUD (Settings -> Advanced -> Debug). Q = requests queued, A = being read/decoded
    // right now, R = turned away by the depth cap, D = completed. What it is for: "art stops after a
    // few covers" has two very different causes that look identical on screen. If D keeps climbing
    // the pipeline is working and simply slow. If D stops while R climbs, requests are being made
    // and refused -- and if Q stays pinned at the cap with A at 0 and nothing completing, the
    // in-flight counter has drifted above the real number in flight and is refusing everything
    // forever. Cheap enough to leave in: four integer reads and one string, only when debug is on.
    int q = 0, a = 0, r = 0, d = 0;
    int lastMs = 0, okMs = 0, w = 0, h = 0;
    char artdbg[224]; // 96 truncated the IO counters off the end -- snprintf is safe, but silently
                      // dropping the field you added to diagnose something is its own trap.
    cacheDebugCounters(&q, &a, &r, &d);
    cacheDebugLastLoad(&lastMs, &okMs, &w, &h);
    // ms = the last load, ok = the last SUCCESSFUL one with its decoded size. One cover at a few
    // hundred ms means the pipeline is fine and the wait is how many images we ask for; one at
    // seconds means the cost is inside a single read on this device.
    u32 fWorst = 0, fOver = 0;
    u32 fLast = gFrames.watchdog(&fWorst, &fOver);
    // F<last frame ms>/<worst ms> OV<frames over ~1.5 vsyncs>. Read it AS a step is
    // swallowed: OV climbing at that instant means the EE stalled (cause is EE-side); OV
    // standing still while the step is still lost means the EE polled on time and the
    // input died on the IOP.
    u32 padNR = 0, padEmpty = 0;
    padGetFaultCounters(&padNR, &padEmpty);
    // NR = longest run of polls freepad could not be read at all; MT = longest run of polls that
    // read fine but were EMPTY. A press spans 4-6 frames, so a RUN >= 4 in either column is a
    // swallowed press -- and which column it lands in decides where the fault lives.
    // IO = the shared ioman queue, which is what raises the busy overlay. Two pending counts
    // (Simple / Menu-rescan) then their running totals. ART IS NO LONGER IN IT -- covers have
    // their own thread and their own queue now, so the old third column would have reported a
    // permanent 0 and quietly stopped being a diagnostic.
    //
    // X is the art thread's DROPPED count: requests released without loading (cancelled,
    // superseded, torn down). It is the complement of D -- every request the worker takes ends
    // in exactly one of the two -- so D and X both flat while covers keep being asked for is the
    // signature of a leak, and X racing while D crawls is the signature of over-cancelling.
    // That pair is how the born-stale bug was caught, and it is worth keeping legible.
    // ...and the last SUCCESSFUL load's cost WITH ITS DECODED PIXEL SIZE, which is the field that
    // settles the open question about the VCD page. Covers there measured 4309, 6847, 8764 and
    // 8813 ms against 55-81 ms for a PS2 cover on the same USB stick, and the two explanations --
    // "PS1 box scans are simply enormous PNGs" and "something about that path is slow" -- are
    // indistinguishable from a millisecond alone. WxH tells them apart in one glance: a
    // 1000x1400 scan decoding for eight seconds is arithmetic, a 184x256 one is a bug.
    // OE = staged-art opens that failed with errno != ENOENT, i.e. covers branded permanently
    // absent on evidence that was not the filesystem saying "no such file". Must stay 0; if it
    // climbs, transient bus errors are eating real art and texStagedOpenIsAbsence needs master's
    // ENOENT-only rule.
    // O:<hit>/<miss> = cost of the last staged art open() and of the last one that FAILED. THIS IS
    // THE FIELD TO READ NOW. If a missing cover's open costs seconds while a successful one costs
    // milliseconds, the cost is the filesystem walking the whole directory to prove a file is not
    // there -- and an in-RAM index of the art folder becomes worth building. If both are small,
    // the seconds are in the transfer or in contention and the index would fix nothing.
    // IX<dirs>/<absent> = art directories held in RAM, and probes answered "not there" without
    // touching the device. The second number is the whole point of the index: every one of those
    // is a directory walk that did not happen.
    int ixDirs = 0;
    unsigned int ixAbsent = 0, ixFailed = 0;
    int wOpen = 0, wPend = 0, wMiss = 0, wMenu = 0, wBgm = 0;
    texDebugWorstOpen(&wOpen, &wPend, &wMiss, &wMenu, &wBgm);
    artIndexDebug(&ixDirs, &ixAbsent, &ixFailed);
    // SX<stale>/<full> = SFX silently discarded: stale = cursor ticks aged out (harmless), full =
    // ANY sound dropped by the saturated 8-deep ring -- the one that eats deliberate presses.
    // SP<last>/<max> = per-press audsrv RPC wall ms, measured on the dispatch thread. The diag
    // existed since #340 but NOTHING READ IT -- the classic half-landed instrument. Pair them
    // for #364 ("effects cut without a ~5 s cooldown"): SP huge with SX flat = the IOP is slow;
    // SP small with SX/full climbing = the dispatcher is starved of CPU.
    unsigned int sxStale = 0, sxFull = 0, spLastMs = 0, spMaxMs = 0;
    sfxGetDropDiag(&sxStale, &sxFull);
    sfxGetPlayDiag(&spLastMs, &spMaxMs);
    int padAct0 = 0, padAln0 = 0, padAct1 = 0, padAln1 = 0;
    padGetActuatorDiag(&padAct0, &padAln0, &padAct1, &padAln1);
    char actBuf[16];
    snprintf(actBuf, sizeof(actBuf), "ACT%d%s/%d%s",
             padAct0, (padAct0 > 0 ? (padAln0 ? "a" : "u") : ""),
             padAct1, (padAct1 > 0 ? (padAln1 ? "a" : "u") : ""));
    snprintf(artdbg, sizeof(artdbg), "Q%d A%d D%d X%d %dms(ok %dms %dx%d) O:%d/%d W%d@%d/%d/%d%c OE%u IX%d/%u/%u KL%u TF%u SX%u/%u SP%u/%u F%u/%u %s NR%u MT%u IO%d/%d",
             q, a, d, cacheDebugDropped(), lastMs, okMs, w, h,
             gTexLastOpenMs, gTexLastMissOpenMs, wOpen, wPend, wMenu, wBgm, wMiss ? 'm' : 'h', gTexStagedOpenNonEnoent, ixDirs, ixAbsent, ixFailed, cacheDebugKeyTooLong(), cacheDebugTransientFail(), sxStale, sxFull, spLastMs, spMaxMs,
             (unsigned)(fLast / 1000), (unsigned)(fWorst / 1000),
             actBuf, (unsigned)padNR, (unsigned)padEmpty,
             ioGetPending(IO_CUSTOM_SIMPLEACTION), ioGetPending(IO_MENU_UPDATE_DEFFERED));
    fntRenderString(gTheme->fonts[0], 8, gGuiScreenHeight - 24, ALIGN_NONE, 0, 0, artdbg, GS_SETREG_RGBA(255, 255, 0, 128));
}

// The C-style entry points other files use.
void guiDrawOverlays(void)
{
    gOverlays.draw();
}

void guiDrawBusy(int alpha)
{
    gOverlays.drawBusy(alpha);
}

void guiDrawSubMenuHints(void)
{
    gOverlays.drawSubMenuHints();
}
