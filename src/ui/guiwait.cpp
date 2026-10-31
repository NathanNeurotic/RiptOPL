/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

// Blocking waits on the I/O thread and the plain text status screen. Split out of gui.cpp.

#include "include/opl.h"
#include "include/plasma.h"
#include "include/gui.h"
#include "include/guiint.h"
#include "include/diag.h" // gLastDeferredTimedOut -- a bounded wait that expired means the handler never ran
#include "include/renderman.h"
#include "include/menusys.h"
#include "include/fntsys.h"
#include "include/ioman.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/favsupport.h" // gFAVStartMode -- Favourites Start Mode row
#include "include/pad.h"
#include "include/sound.h" // sfxGetPlayDiag/sfxGetDropDiag -- SFX RPC cost + silent-drop counters for the debug HUD
#include "include/util.h"
#include "include/config.h"
#include "include/system.h"
#include "include/mmcesupport.h"
#include "include/ethsupport.h"
#include "include/netsupport.h"
#include "include/udpfssupport.h"
#include "include/httpsupport.h" // httpTestServer() -- the Network page Test button
#include "include/artindex.h"
#include "include/bdmsupport.h" // bdmIsUDPBDLoaded() + bdmForceDeviceRefresh()
#include "include/hddsupport.h" // staged normal APA OPL-home selector
#include "include/vcdsupport.h" // POPStarter pages: BDMA equip, list options, POPS net config
#include "include/libview.h"    // libViewActive / libListViewActive -- which list this page shows
#include "include/pggsm.h"
#include "include/cheatman.h"
#include "include/sound.h"
#include "include/guigame.h"
#include "include/texcache.h"
#include "include/appsupport.h" // appGetObject() -- push a live Art Delay change onto the APPS list
#include "include/tar.h"        // tarInvalidate -- re-arm the .tar probe when the toggle flips
#include <limits.h>
#include <stdlib.h>
#include <time.h>

#include "include/guiwait.h"

void DeferredIoWait::runWithMessage()
{
    // Neither of these loops calls readPads(), so the rumble decay is frozen for the whole wait.
    // The IOP LATCHES the actuator state, so a pulse armed by the SFX_CONFIRM that started this
    // save keeps the motor spinning for the entire blocking operation -- seconds, at full level.
    // GUI thread only: every libpad call in pad.c is, and applyConfig() is NOT a safe home for
    // this because _loadConfig() reaches it from the IO worker.
    padRumbleFlush();

    // Free the shared IOP/fileXio channel before running the deferred IO. The
    // cover-art worker's queued and in-flight reads otherwise tie up that single
    // channel, so a config write (e.g. the last-played save on game launch) queues
    // behind them and the screen freezes on "Saving config..." after browsing the
    // list (issue #45 -- confirmed on hardware: disabling cover art avoids it).
    // Cancel queued cover loads, abort a slow in-flight MMCE read, and drain. The
    // drain returns as soon as the art queue is empty (cacheWaitForAllRequestsTimed
    // early-exits when nothing is queued/active), so this adds no delay in the
    // common case; the timeout only bounds a genuinely stuck read.
    int abortOk = cacheAbortMmceImageLoadsTimed(500);
    int cancelOk = cacheCancelPendingImageLoadsTimed(500);
    if (!abortOk || !cancelOk) {
        // A cover-art read did not drain within the timeout -- most likely a slow
        // (not dead) storage device. We still issue the deferred IO below: bailing
        // here would silently drop a valid config save on a merely-slow card, and
        // could not unwedge a genuinely stuck IOP RPC channel anyway. Logged so a
        // true hardware hang is diagnosable rather than a silent freeze on the
        // unbounded wait below (Codex audit, Medium 1).
        LOG("DeferredIoWait: art drain timed out; deferred IO may stall on stuck storage\n");
    }

    // A rejected request never runs, so nothing will ever clear *flag_ -- without this the loop below
    // would spin forever on a queue-full/failed put. Clear it here so the caller falls into its
    // normal failure path instead of hanging.
    if (ioPutRequest(type_, data_) != IO_OK) {
        *flag_ = 0;
        return;
    }

    // Bound the wait when the caller asks (timeoutMs_ > 0). A deferred IO that never completes -- a
    // failing HDD/card that wedges the single IOP fileXio channel mid-write -- would otherwise spin
    // here forever and freeze the GUI on "Saving..." with no escape. After a timeout far beyond any
    // real config save/load we stop waiting and clear the status, so the caller falls into its normal
    // failure path (saveConfig -> "Error saving settings"), exactly like the put-failure branch above.
    // This does NOT recover the stuck IOP -- the IO worker runs requests one at a time and stays
    // blocked on the wedged one until reboot -- but a readable error beats an apparent hard lock.
    // Memory-safe: every caller's *flag_ is a file-static int and `data` a file-static handler, so a
    // late write by the still-blocked IO thread is harmless. timeoutMs_ <= 0 keeps the original
    // unbounded wait (the compat-list update, which is a network fetch with no meaningful bound).
    // clock() is the same idiom used elsewhere in this file; the (clock() - startTick) ELAPSED form
    // is single-wrap-safe, unlike an absolute clock()+limit deadline.
    clock_t startTick = clock();
    clock_t limitTicks = (clock_t)timeoutMs_ * (CLOCKS_PER_SEC / 1000);
    gLastDeferredTimedOut = 0;
    while (*flag_) {
        if (timeoutMs_ > 0 && (clock() - startTick) >= limitTicks) {
            LOG("DeferredIoWait: deferred IO unfinished after %d ms; storage stuck, abandoning wait\n", timeoutMs_);
            *flag_ = 0;
            // The handler never ran. Callers must not report a device error against their target
            // path on the strength of this -- nothing touched it.
            gLastDeferredTimedOut = 1;
            break;
        }
        guiRenderTextScreen(message_);
    }
}

void DeferredIoWait::runWithDialog()
{
    // Neither of these loops calls readPads(), so the rumble decay is frozen for the whole wait.
    // The IOP LATCHES the actuator state, so a pulse armed by the SFX_CONFIRM that started this
    // save keeps the motor spinning for the entire blocking operation -- seconds, at full level.
    // GUI thread only: every libpad call in pad.c is, and applyConfig() is NOT a safe home for
    // this because _loadConfig() reaches it from the IO worker.
    padRumbleFlush();

    // A rejected request never runs, so nothing will ever clear *flag_ and the loop below spins
    // forever. The sibling guiHandleDeferedIO already checks this; this one did not.
    if (ioPutRequest(type_, data_) != IO_OK) {
        *flag_ = 0;
        return;
    }

    while (*flag_) {
        guiStartFrame();
        if (ui_)
            diaRenderUI(ui_, guiScreenInMenu(), NULL, 0);
        else
            guiShow();
        guiEndFrame();
    }
}

void DeferredIoWait::run()
{
    if (message_ != nullptr)
        runWithMessage();
    else
        runWithDialog();
}

void guiHandleDeferedIO(int *ptr, const char *message, int type, void *data, int timeoutMs)
{
    DeferredIoWait wait(ptr, message, type, data, timeoutMs);
    wait.run();
}

void guiGameHandleDeferedIO(int *ptr, struct UIItem *ui, int type, void *data)
{
    DeferredIoWait wait(ptr, ui, type, data);
    wait.run();
}


/* Fade the final status frame out just before renderer teardown. This does not
 * touch the game core, device/IOP lifetime or launch arguments. The long IO
 * drain remains visible as "Please Wait"; only its completion starts the fade.
 * No guiShow(): menu/art state may be changing during shutdown. */

/* User-initiated TV-mode switch. Draw each outgoing frame against the old
 * GS mode, then let applyConfig reinitialize its framebuffers. This entry
 * point is intentionally NOT called from the renderer or the IO worker. */
void guiFadeCurrentScreenToBlack(void)
{
    int frame;
    for (frame = 1; frame <= 8; ++frame) {
        guiStartFrame();
        guiShow();
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight,
                   GS_SETREG_RGBA(0, 0, 0, (frame * 0x80) / 8));
        guiEndFrame();
    }
}

void guiFadeOutForHandoff(const char *message)
{
    int frame;
    for (frame = 1; frame <= 8; ++frame) {
        guiStartFrame();
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);
        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1,
                        gTheme->usedHeight >> 1, ALIGN_CENTER, 0, 0,
                        message, gTheme->textColor);
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight,
                   GS_SETREG_RGBA(0, 0, 0, (frame * 0x80) / 8));
        guiEndFrame();
    }
}

void guiRenderTextScreen(const char *message)
{
    guiStartFrame();

    guiShow();

    // Opaque backdrop, not gColDarker. This is a non-interactive STATUS screen ("Please wait",
    // "NBD Server unloading..."), and gColDarker is only alpha 0x60 of a 0x80 maximum -- i.e. ~75%
    // -- so the menu underneath stayed clearly legible through the message. On a CRT that reads as
    // a transparent error with the settings menu showing through it, which is exactly what the
    // tester reported (and photographed) three times. Whatever is behind a status screen carries no
    // information, so it is hidden. Interactive dialogs keep the translucent overlay only when the
    // caller chose what sits behind them (guiMsgBox with a ui, the prompts over the settings
    // background); the video-mode, remove-settings and cheat-selection screens went opaque too.
    rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);

    guiDrawMsgText(message);

    guiDrawOverlays();

    guiEndFrame();
}
