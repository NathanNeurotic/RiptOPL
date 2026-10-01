/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

// Modal dialogs and one-shot screens (message box, reboot prompt, deferred-IO waits, Game ID, warnings,
// cheat manager). Split out of gui.cpp.

#include "include/opl.h"
#include "include/plasma.h"
#include "include/gui.h"
#include "include/guiint.h"
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

// Greedily word-wrap `text` to innerW, writing '\n'-separated lines into out. Returns the line count.
//
// fntRenderString does NOT wrap: it lays a string on ONE line and CLIPS at the window edge (only an
// explicit '\n' starts a new line), so a long message ran off BOTH screen edges and was unreadable
// -- reported on the 1080p warning, which is simply the longest string we ship.
// NOTE: src/dia.c carries the same algorithm inline for its hint box. Deliberately not merged here:
// that copy is entangled with the hint box's own geometry (it needs the line count to size the box
// before rendering), and this is a message-box fix, not a refactor of a working path.
static int guiWrapText(const char *text, int innerW, char *out, int outSize)
{
    int wlen = 0, lineW = 0, lines = 1;
    int spaceW = rmUnScaleX(fntCalcDimensions(gTheme->fonts[0], " "));
    const char *p = text;

    if (out == NULL || outSize <= 0)
        return 0;

    while (*p) {
        while (*p == ' ') // collapse runs of spaces; we re-insert our own single separators
            p++;
        if (!*p)
            break;

        // An explicit newline in the source string is honoured as a hard break.
        if (*p == '\n') {
            if (wlen < outSize - 1)
                out[wlen++] = '\n';
            lines++;
            lineW = 0;
            p++;
            continue;
        }

        char word[96];
        int k = 0;
        while (*p && *p != ' ' && *p != '\n' && k < (int)sizeof(word) - 1)
            word[k++] = *p++;
        word[k] = '\0';
        int wordW = rmUnScaleX(fntCalcDimensions(gTheme->fonts[0], word));

        if (lineW > 0 && (lineW + spaceW + wordW) > innerW) { // does not fit -> break the line
            if (wlen < outSize - 1)
                out[wlen++] = '\n';
            lines++;
            lineW = 0;
        } else if (lineW > 0) { // same line -> re-insert the separating space
            if (wlen < outSize - 1)
                out[wlen++] = ' ';
            lineW += spaceW;
        }
        for (int i = 0; i < k && wlen < outSize - 1; i++)
            out[wlen++] = word[i];
        lineW += wordW;
    }
    out[wlen] = '\0';
    return lines;
}

int guiMsgBox(const char *text, int addAccept, struct UIItem *ui)
{
    int terminate = 0;
    // Wrapped once up front, not per frame: the result depends only on `text` and the screen width.
    char wrapped[512];

    // Same pre-GUI guard as guiWarning: autolaunch (miniInit) never runs rmInit/thmInit/guiInit or
    // starts the pads, so a launch-path error box would draw through a NULL gsGlobal/gTheme and then
    // wait forever for a button -- a black screen instead of a refused launch. Answer "back" (0, see
    // the return below) so the caller unwinds and autolaunch falls back to the menu.
    if (gTheme == NULL) {
        LOG("guiMsgBox (pre-GUI): %s\n", text);
        return 0;
    }

    int lines = guiWrapText(text, gGuiScreenWidth - 120, wrapped, sizeof(wrapped));

    sfxPlay(SFX_MESSAGE);

    while (!terminate) {
        guiStartFrame();

        readPads();

        if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE))
            terminate = 1;
        else if (getKeyOn(gSelectButton))
            terminate = 2;

        if (ui)
            diaRenderUI(ui, guiScreenInMenu(), NULL, 0);
        else
            guiShow();

        // With a `ui` the CALLER chose what sits behind this box, so keep that context visible.
        // With ui == NULL the backdrop is just whichever menu happened to be on screen -- it carries
        // no information and, at gColDarker's ~75%, left the menu legible straight through the
        // message. Errors get their own background in that case.
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, ui ? gColDarker : gColBlack);

        rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
        rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

        // Render the wrapped lines ONE AT A TIME: ALIGN_CENTER centres on the measured width of the
        // WHOLE string, so handing it a multi-line buffer would centre it as a single long run and
        // leave every line off-centre. Block is vertically centred on the old single-line position.
        {
            int lineH = MENU_ITEM_HEIGHT;
            int ly = (gTheme->usedHeight >> 1) - ((lines - 1) * lineH) / 2;
            const char *ls = wrapped;
            while (ls != NULL) {
                const char *eol = strchr(ls, '\n');
                char line[192];
                int n = eol ? (int)(eol - ls) : (int)strlen(ls);
                if (n > (int)sizeof(line) - 1)
                    n = (int)sizeof(line) - 1;
                memcpy(line, ls, n);
                line[n] = '\0';
                fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, ly, ALIGN_CENTER, 0, 0, line, gTheme->textColor);
                ly += lineH;
                ls = eol ? eol + 1 : NULL;
            }
        }
        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_BACK, gTheme->fonts[0], 500, 417, gTheme->textColor);
        if (addAccept)
            guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON, _STR_ACCEPT, gTheme->fonts[0], 70, 417, gTheme->textColor);

        guiEndFrame();
    }

    if (terminate == 1) {
        sfxPlay(SFX_CANCEL);
    }
    if (terminate == 2) {
        sfxPlay(SFX_CONFIRM);
    }

    return terminate - 1;
}

// Settings, its save prompt and the Reboot IOP prompt used to hard-wire Cross = confirm / Circle = back
// (Settings redesign, #527), so with Circle chosen as the confirm button those screens ran the other way
// round from the rest of RiptOPL (volventura, PSX-Place). Everything asks here instead.
int guiConfirmKey(void)
{
    return gSelectButton;
}

int guiCancelKey(void)
{
    return gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE;
}

int guiConfirmIcon(void)
{
    return gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON;
}

int guiCancelIcon(void)
{
    return gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON;
}

int guiPromptRebootIop(void)
{
    int terminate = 0;

    sfxPlay(SFX_MESSAGE);

    while (!terminate) {
        guiStartFrame();

        readPads();

        if (getKeyOn(guiConfirmKey()))
            terminate = 1;
        else if (getKeyOn(guiCancelKey()))
            terminate = 2;
        else if (getKeyOn(KEY_TRIANGLE))
            terminate = 3;

        guiShow();

        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);

        rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
        rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, gTheme->usedHeight >> 1, ALIGN_CENTER, 0, 0, "REBOOT IOP?", gTheme->textColor);

        guiDrawIconAndLiteralText(guiConfirmIcon(), "YES", gTheme->fonts[0], 70, 417, gTheme->textColor);
        guiDrawIconAndLiteralText(TRIANGLE_ICON, _l(_STR_CANCEL), gTheme->fonts[0], 285, 417, gTheme->textColor);
        guiDrawIconAndLiteralText(guiCancelIcon(), "NO", gTheme->fonts[0], 500, 417, gTheme->textColor);

        guiEndFrame();
    }

    if (terminate == 1) {
        sfxPlay(SFX_CONFIRM);
        return 1;
    }
    if (terminate == 2) {
        sfxPlay(SFX_CONFIRM);
        return 0;
    }

    sfxPlay(SFX_CANCEL);
    return -1;
}

void guiHandleDeferedIO(int *ptr, const char *message, int type, void *data, int timeoutMs)
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
        LOG("guiHandleDeferedIO: art drain timed out; deferred IO may stall on stuck storage\n");
    }

    // A rejected request never runs, so nothing will ever clear *ptr -- without this the loop below
    // would spin forever on a queue-full/failed put. Clear it here so the caller falls into its
    // normal failure path instead of hanging.
    if (ioPutRequest(type, data) != IO_OK) {
        *ptr = 0;
        return;
    }

    // Bound the wait when the caller asks (timeoutMs > 0). A deferred IO that never completes -- a
    // failing HDD/card that wedges the single IOP fileXio channel mid-write -- would otherwise spin
    // here forever and freeze the GUI on "Saving..." with no escape. After a timeout far beyond any
    // real config save/load we stop waiting and clear the status, so the caller falls into its normal
    // failure path (saveConfig -> "Error saving settings"), exactly like the put-failure branch above.
    // This does NOT recover the stuck IOP -- the IO worker runs requests one at a time and stays
    // blocked on the wedged one until reboot -- but a readable error beats an apparent hard lock.
    // Memory-safe: every caller's *ptr is a file-static int and `data` a file-static handler, so a
    // late write by the still-blocked IO thread is harmless. timeoutMs <= 0 keeps the original
    // unbounded wait (the compat-list update, which is a network fetch with no meaningful bound).
    // clock() is the same idiom used elsewhere in this file; the (clock() - startTick) ELAPSED form
    // is single-wrap-safe, unlike an absolute clock()+limit deadline.
    clock_t startTick = clock();
    clock_t limitTicks = (clock_t)timeoutMs * (CLOCKS_PER_SEC / 1000);
    gLastDeferredTimedOut = 0;
    while (*ptr) {
        if (timeoutMs > 0 && (clock() - startTick) >= limitTicks) {
            LOG("guiHandleDeferedIO: deferred IO unfinished after %d ms; storage stuck, abandoning wait\n", timeoutMs);
            *ptr = 0;
            // The handler never ran. Callers must not report a device error against their target
            // path on the strength of this -- nothing touched it.
            gLastDeferredTimedOut = 1;
            break;
        }
        guiRenderTextScreen(message);
    }
}

void guiGameHandleDeferedIO(int *ptr, struct UIItem *ui, int type, void *data)
{
    // Neither of these loops calls readPads(), so the rumble decay is frozen for the whole wait.
    // The IOP LATCHES the actuator state, so a pulse armed by the SFX_CONFIRM that started this
    // save keeps the motor spinning for the entire blocking operation -- seconds, at full level.
    // GUI thread only: every libpad call in pad.c is, and applyConfig() is NOT a safe home for
    // this because _loadConfig() reaches it from the IO worker.
    padRumbleFlush();

    // A rejected request never runs, so nothing will ever clear *ptr and the loop below spins
    // forever. The sibling guiHandleDeferedIO already checks this; this one did not.
    if (ioPutRequest(type, data) != IO_OK) {
        *ptr = 0;
        return;
    }

    while (*ptr) {
        guiStartFrame();
        if (ui)
            diaRenderUI(ui, guiScreenInMenu(), NULL, 0);
        else
            guiShow();
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

    fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, gTheme->usedHeight >> 1, ALIGN_CENTER, 0, 0, message, gTheme->textColor);

    guiDrawOverlays();

    guiEndFrame();
}

// ---- Visual GameID barcode (Pixel FX / RetroGEM / PS2Digital HDMI auto-profile) ----
// Renders the CosmicScale "GameID" barcode just before a game is handed to its core, so an HDMI
// scaler can auto-load that game's per-title display profile. Encoding is the canonical CosmicScale
// scheme (start word 0xA5 / end word 0xD5 / length byte / additive 0x100-sum checksum), drawn with
// rmDrawRect exactly as CosmicScale's own OPL fork does. Gated behind gApplyGameID, which ships OFF
// (fork commit cc2cdfed, "GameID defaults OFF") -- the HDMI latch is only verifiable on real GameID
// hardware, so this stays opt-in until a tester confirms it. The fork's own header comment here still
// claimed "default ON since 120045d0"; setDefaults() has said 0 since cc2cdfed, so that claim is stale
// and is not carried over.
//
// NOTE (#269): "imperceptible on other displays" was only true once guiShowGameID stopped LEAVING the
// barcode on screen. It is the last thing OPL draws before the ELF handoff, and on composite the
// 1-pixel-pitch strip averages to a solid white bar that the GS scans out for the whole game load.
// The trailing clean-frame loop at the end of guiShowGameID is what makes that premise hold.

constexpr int GAMEID_HOLD_FRAMES = 45;  // ~0.75s @ 60fps -- enough stable frames for a scaler to sample

// Normalise a startup id into the serial the GameID device expects: drop a POPS "XX."/"SB." prefix and
// a trailing ".elf"/".ELF", cap at 11 chars (e.g. "SLUS_200.02"). Copied VERBATIM (no case fold) to
// stay byte-identical to CosmicScale's HW-validated guiSetGameId; retail serials are already uppercase.
static void gameIDCleanSerial(const char *startup, char *out, int outSize)
{
    int i = 0, len;
    const char *src = startup;

    out[0] = '\0';
    if (src == NULL)
        return;

    if (!strncmp(src, "XX.", 3) || !strncmp(src, "SB.", 3))
        src += 3;

    while (src[i] != '\0' && i < outSize - 1) {
        out[i] = src[i];
        i++;
    }
    out[i] = '\0';

    len = (int)strlen(out);
    if (len >= 4 && !strcasecmp(&out[len - 4], ".elf"))
        out[len - 4] = '\0';
    if (strlen(out) > 11)
        out[11] = '\0';
}

// Build the GameID packet from a cleaned serial; returns its length in bytes.
static int gameIDBuildPacket(u8 *data, const char *serial)
{
    int n = 0, i, sum = 0, crcpos;
    int gidlen = (int)strlen(serial);
    if (gidlen > 11)
        gidlen = 11;

    data[n++] = 0xA5;       // start / detect word
    data[n++] = 0x00;       // address offset
    crcpos = n++;           // checksum placeholder (data[2])
    data[n++] = (u8)gidlen; // payload length
    for (i = 0; i < gidlen; i++)
        data[n++] = (u8)serial[i];
    data[n++] = 0x00;
    data[n++] = 0xD5; // end word
    data[n++] = 0x00; // padding

    for (i = 3; i < n; i++) // additive 8-bit checksum over {length byte .. end}
        sum += data[i];
    data[crcpos] = (u8)(0x100 - (sum & 0xFF));
    return n;
}

// Draw the barcode once: per data bit (MSB-first) a magenta clock column + a cyan(1)/yellow(0) column.
static void gameIDDrawBars(const char *startup)
{
    u8 data[32];
    char serial[16];
    int data_len, i, ii, xstart, ystart;

    gameIDCleanSerial(startup, serial, sizeof(serial));
    if (serial[0] == '\0')
        return;

    data_len = gameIDBuildPacket(data, serial);
    xstart = (gGuiScreenWidth / 2) - (data_len * 8); // centered horizontally
    ystart = gGuiScreenHeight - ((gGuiScreenHeight / 8) * 2 + 20);

    for (i = 0; i < data_len; i++) {
        for (ii = 7; ii >= 0; ii--) {
            int x = xstart + (i * 16 + (7 - ii) * 2);
            rmDrawRect(x, ystart, 1, 2, GS_SETREG_RGBA(0xFF, 0x00, 0xFF, 0x80)); // magenta clock col
            rmDrawRect(x + 1, ystart, 1, 2,
                       ((data[i] >> ii) & 1) ? GS_SETREG_RGBA(0x00, 0xFF, 0xFF, 0x80) // cyan = 1
                                               :
                                               GS_SETREG_RGBA(0xFF, 0xFF, 0x00, 0x80)); // yellow = 0
        }
    }
}

void guiShowGameID(const char *startup)
{
    int frame;

    if (!gApplyGameID || startup == NULL || startup[0] == '\0')
        return;

    // Hold the barcode on a clean black field for a few frames so an HDMI scaler can latch it.
    // The loader rides these already-pumped frames (#299): this hold is the one place on the
    // synchronous launch path that still renders, so drawing the busy animation here makes it
    // genuinely cycle during launch prep. Bars drawn LAST so the barcode strip stays on top.
    for (frame = 0; frame < GAMEID_HOLD_FRAMES; frame++) {
        guiStartFrame();
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80));
        guiDrawBusy(0x80);
        gameIDDrawBars(startup);
        guiEndFrame();
    }

    /*
      Leave a CLEAN black frame behind (#269) -- clean of the BARCODE, that is; the loader is drawn
      on these frames on purpose (#299, below).

      This is the last thing the GameID path renders: itemExecSelect() calls us and then goes
      straight into itemLaunch() -> deinit() -> ExecPS2(), and on a default config nothing in
      between draws anything (gRememberLastPlayed is off, so there is no save toast; no cheats, no
      VMC, no MX4SIO warning). Neither deinit() nor sysLaunchLoaderElf() reprograms a GS display
      register, so whatever buffer rmEndFrame last pointed DISPFB2 at keeps being scanned out until
      the GAME programs the GS -- which on USB is minutes away.

      Without this, that buffer is "black field + barcode strip", and the strip is drawn at
      1-virtual-pixel pitch (magenta column immediately followed by cyan/yellow). That is below the
      chroma bandwidth of composite output, so the columns average to near-white and the user sees
      a solid ~288x2 white bar, ~71% down an otherwise black screen, for the entire load. Exactly
      the report: every game, unrelated to Debug Colors, lasting as long as the media takes.

      Two frames because rendering is double-buffered -- one alone leaves the barcode in the OTHER
      buffer, which is the one a scaler may resync to across the video-mode change.

      #299: the busy animation is drawn onto both clean frames, so nothing but "black field +
      loader" is left in EITHER buffer -- the loading indicator issue #299 asks for. The themed
      load*.png pixmaps are ordinary composite-safe art, nothing like the 1-pixel-pitch barcode
      strip above, so the #269 premise is untouched.

      NOTE(rebuild): the fork's comment here also credited itemExecSelect with hand-pumping a
      guiRenderBusyFrame() around this call, which is what used to cover the GameID-OFF config.
      That helper was removed again by fork commit 79865cde ("Restore loading icon piping to
      official OPL baseline") and does not exist on fork master or here -- so with the barcode
      off, launch prep still renders nothing. Re-adding the hand-pumped launch-prep loader is
      checklist item 48's business, not this step's.
    */
    for (frame = 0; frame < 2; frame++) {
        guiStartFrame();
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80));
        guiDrawBusy(0x80);
        guiEndFrame();
    }
}

void guiWarning(const char *text, int count)
{
    // Pre-GUI callers exist: autolaunch (miniInit) never runs rmInit/thmInit/guiInit, so rendering
    // here would draw through a NULL gsGlobal/gTheme. Launch-path helpers shared between the menu
    // and autolaunch (the Neutrino/VMC toasts in bdmsupport/hddsupport/supportbase) rely on this
    // guard instead of each call site checking the autolaunch globals.
    if (gTheme == NULL) {
        LOG("guiWarning (pre-GUI): %s\n", text);
        return;
    }

    guiStartFrame();

    guiShow();

    // Same reasoning as guiRenderTextScreen: a warning is a non-interactive surface shown for a
    // fixed number of frames, so it gets an opaque backdrop rather than a ~75% wash.
    rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);

    rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
    rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

    fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, gTheme->usedHeight >> 1, ALIGN_CENTER, gGuiScreenWidth, gGuiScreenHeight, text, gTheme->textColor);

    guiEndFrame();

    delay(count);
}

int guiGameShowRemoveSettings(config_set_t *configSet, config_set_t *configGame)
{
    int terminate = 0;
    char message[256];

    sfxPlay(SFX_MESSAGE);

    while (!terminate) {
        guiStartFrame();

        readPads();

        if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE))
            terminate = 1;
        else if (getKeyOn(gSelectButton))
            terminate = 2;
        else if (getKeyOn(KEY_SQUARE))
            terminate = 3;
        else if (getKeyOn(KEY_TRIANGLE))
            terminate = 4;

        guiShow();

        // Opaque for the same reason as guiConfirmVideoMode: the backdrop is just guiShow().
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);

        rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
        rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, gTheme->usedHeight >> 1, ALIGN_CENTER, 0, 0, _l(_STR_GAME_SETTINGS_PROMPT), gTheme->textColor);

        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_BACK, gTheme->fonts[0], 500, 417, gTheme->textColor);
        guiDrawIconAndText(SQUARE_ICON, _STR_GLOBAL_SETTINGS, gTheme->fonts[0], 213, 417, gTheme->textColor);
        guiDrawIconAndText(TRIANGLE_ICON, _STR_ALL_SETTINGS, gTheme->fonts[0], 356, 417, gTheme->textColor);
        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON, _STR_PERGAME_SETTINGS, gTheme->fonts[0], 70, 417, gTheme->textColor);

        guiEndFrame();
    }

    if (terminate == 1) {
        sfxPlay(SFX_CANCEL);
        return 0;
    } else if (terminate == 2) {
        guiGameRemoveSettings(configSet);
        snprintf(message, sizeof(message), _l(_STR_GAME_SETTINGS_REMOVED), _l(_STR_PERGAME_SETTINGS));
    } else if (terminate == 3) {
        guiGameRemoveGlobalSettings(configGame);
        snprintf(message, sizeof(message), _l(_STR_GAME_SETTINGS_REMOVED), _l(_STR_GLOBAL_SETTINGS));
    } else if (terminate == 4) {
        guiGameRemoveSettings(configSet);
        guiGameRemoveGlobalSettings(configGame);
        snprintf(message, sizeof(message), _l(_STR_GAME_SETTINGS_REMOVED), _l(_STR_ALL_SETTINGS));
    }
    sfxPlay(SFX_CONFIRM);
    guiMsgBox(message, 0, NULL);

    return 1;
}

void guiManageCheats(void)
{
    int offset = 0;
    int terminate = 0;
    int cheatCount = 0;
    int selectedCheat = 0;
    int visibleCheats = 10; // Maximum number of cheats visible on screen

    if (gCheats == NULL) // defensive: the menu is only reachable after load_cheats, but never deref NULL
        return;

    while (cheatCount < MAX_CODES && gCheats[cheatCount].name[0] != '\0')
        cheatCount++;

    sfxPlay(SFX_MESSAGE);

    while (!terminate) {
        guiStartFrame();
        readPads();

        if (getKeyOn(KEY_UP) && selectedCheat > 0) {
            selectedCheat -= 1;
            if (selectedCheat < offset)
                offset = selectedCheat;
        }

        if (getKeyOn(KEY_DOWN) && selectedCheat < cheatCount - 1) {
            selectedCheat += 1;
            if (selectedCheat >= offset + visibleCheats)
                offset = selectedCheat - visibleCheats + 1;
        }

        if (getKeyOn(gSelectButton)) {
            if (!(strncasecmp(gCheats[selectedCheat].name, "mastercode", 10) == 0 || strncasecmp(gCheats[selectedCheat].name, "master code", 11) == 0))
                gCheats[selectedCheat].enabled = !gCheats[selectedCheat].enabled;
        }

        if (getKeyOn(KEY_SQUARE)) {
            // Disable All: clear every cheat's enabled flag in one press. Skip the mastercode (the
            // per-cheat toggle can't touch it either -- it is the engine enabler, not a cheat).
            for (int i = 0; i < cheatCount; i++) {
                if (!(strncasecmp(gCheats[i].name, "mastercode", 10) == 0 || strncasecmp(gCheats[i].name, "master code", 11) == 0))
                    gCheats[i].enabled = 0;
            }
            sfxPlay(SFX_CURSOR);
        }

        if (getKeyOn(KEY_START))
            terminate = 1;

        guiShow();

        // Opaque for the same reason as guiConfirmVideoMode: the backdrop is just guiShow(), and
        // the cheat names are far easier to read on black than over the menu.
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);
        rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
        rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, 60, ALIGN_CENTER, 0, 0, _l(_STR_CHEAT_SELECTION), gTheme->textColor);

        int renderedCheats = 0;
        for (int i = offset; renderedCheats < visibleCheats && i < cheatCount; i++) {
            if (gCheats[i].name[0] == '\0')
                continue;

            int enabled = gCheats[i].enabled;

            int boxX = 50;
            int boxY = 100 + (renderedCheats * 30);
            int boxWidth = rmWideScale(25);
            int boxHeight = 17;

            if (enabled)
                rmDrawFramedRect(boxX, boxY + 3, boxWidth, boxHeight, 2, gTheme->textColor, gTheme->selTextColor);

            u32 textColour = (i == selectedCheat) ? gTheme->selTextColor : gTheme->textColor;
            fntRenderString(gTheme->fonts[0], boxX + 35, boxY + 3, ALIGN_LEFT, 0, 0, gCheats[i].name, textColour);

            renderedCheats++;
        }

        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON, _STR_SELECT, gTheme->fonts[0], 70, 417, gTheme->textColor);
        guiDrawIconAndText(SQUARE_ICON, _STR_DISABLE_ALL, gTheme->fonts[0], 270, 417, gTheme->textColor);
        guiDrawIconAndText(START_ICON, _STR_RUN, gTheme->fonts[0], 500, 417, gTheme->textColor);

        guiEndFrame();
    }

    sfxPlay(SFX_CONFIRM);
}

