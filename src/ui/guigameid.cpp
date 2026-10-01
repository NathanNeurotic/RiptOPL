/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

// Visual GameID barcode (Pixel FX / RetroGEM / PS2Digital HDMI auto-profile).
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

#include "include/guigameid.h"

constexpr int GAMEID_HOLD_FRAMES = 45;  // ~0.75s @ 60fps -- enough stable frames for a scaler to sample

// Normalise a startup id into the serial the GameID device expects: drop a POPS "XX."/"SB." prefix and
// a trailing ".elf"/".ELF", cap at 11 chars (e.g. "SLUS_200.02"). Copied VERBATIM (no case fold) to
// stay byte-identical to CosmicScale's HW-validated guiSetGameId; retail serials are already uppercase.
void GameIdBarcode::cleanSerial(const char *startup, char *out, int outSize)
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
int GameIdBarcode::buildPacket(u8 *data, const char *serial)
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

GameIdBarcode::GameIdBarcode(const char *startup)
{
    char serial[16];

    memset(data_, 0, sizeof(data_));
    cleanSerial(startup, serial, sizeof(serial));
    if (serial[0] != '\0')
        length_ = buildPacket(data_, serial);
}

void GameIdBarcode::draw() const
{
    int data_len = length_, i, ii, xstart, ystart;
    const u8 *data = data_;

    if (!valid())
        return;

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

void GameIdBarcode::show() const
{
    int frame;

    // Hold the barcode on a clean black field for a few frames so an HDMI scaler can latch it.
    // The loader rides these already-pumped frames (#299): this hold is the one place on the
    // synchronous launch path that still renders, so drawing the busy animation here makes it
    // genuinely cycle during launch prep. Bars drawn LAST so the barcode strip stays on top.
    for (frame = 0; frame < GAMEID_HOLD_FRAMES; frame++) {
        guiStartFrame();
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80));
        guiDrawBusy(0x80);
        draw();
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

void guiShowGameID(const char *startup)
{
    if (!gApplyGameID || startup == NULL || startup[0] == '\0')
        return;

    GameIdBarcode barcode(startup);
    barcode.show();
}
