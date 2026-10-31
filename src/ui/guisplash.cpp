/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

#include "include/opl.h"
#include "include/gui.h"
#include "include/guiint.h"
#include "include/guiruntime.h"
#include "include/guisplash.h"
#include "include/guiscreens.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/themes.h"
#include "include/sound.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

BootSplash gSplash;

// How far ABOVE the vertical centre the boot logo (static splash and animated frames alike) sits.
// The version line follows it, so the whole boot block moves together; the status line stays
// anchored near the bottom edge.
constexpr int BOOT_LOGO_RISE = 100;

// Boot logo glow: a soft halo breathes around the logo. The halo is the logo redrawn at low alpha around its
// own position -- eight taps 2 px out and four 5 px out -- so it follows the letter shapes with no extra asset.
// The halo is tinted with the theme's selected-text colour, so it follows the user's colour settings (the white
// logo texture takes the tint; the logo itself stays white). One cycle is BOOT_LOGO_GLOW_PERIOD frames (~2 s at 60 Hz); the pulse is a raised cosine, so it eases in and
// out instead of stepping. `alpha` is the greeting's fade alpha (0..0x80): the glow fades with the splash.
constexpr int BOOT_LOGO_GLOW_PERIOD = 120;
constexpr float BOOT_LOGO_GLOW_MIN = 0.15f; // halo strength at the dim end of the pulse
constexpr float BOOT_LOGO_GLOW_TAP = 0.16f; // per-tap alpha at full pulse, as a fraction of `alpha`

void BootSplash::drawLogo(GSTEXTURE *logo, int cx, int cy, int alpha)
{
    static const signed char inner[8][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}, {2, 2}, {-2, 2}, {2, -2}, {-2, -2}};
    static const signed char outer[4][2] = {{5, 0}, {-5, 0}, {0, 5}, {0, -5}};

    const float phase = (float)(guiFrameId % BOOT_LOGO_GLOW_PERIOD) * (6.2831853f / BOOT_LOGO_GLOW_PERIOD);
    const float pulse = 0.5f - 0.5f * cosf(phase); // 0..1..0, zero slope at both ends
    const float strength = BOOT_LOGO_GLOW_MIN + (1.0f - BOOT_LOGO_GLOW_MIN) * pulse;

    // selTextColor is packed RGBA with 0..0xFF components; texture modulation is 0x80 = 1.0, so halve them.
    const u64 tint = gTheme->selTextColor;
    const int tr = (int)(tint & 0xFF) >> 1, tg = (int)((tint >> 8) & 0xFF) >> 1, tb = (int)((tint >> 16) & 0xFF) >> 1;

    int tapAlpha = (int)((float)alpha * strength * BOOT_LOGO_GLOW_TAP);
    if (tapAlpha > 0) {
        u64 color = GS_SETREG_RGBA(tr, tg, tb, tapAlpha);
        for (int i = 0; i < 8; i++)
            rmDrawPixmap(logo, cx + inner[i][0], cy + inner[i][1], ALIGN_CENTER, logo->Width, logo->Height, SCALING_RATIO, color, 0);
        color = GS_SETREG_RGBA(tr, tg, tb, tapAlpha >> 1);
        for (int i = 0; i < 4; i++)
            rmDrawPixmap(logo, cx + outer[i][0], cy + outer[i][1], ALIGN_CENTER, logo->Width, logo->Height, SCALING_RATIO, color, 0);
    }

    rmDrawPixmap(logo, cx, cy, ALIGN_CENTER, logo->Width, logo->Height, SCALING_RATIO, GS_SETREG_RGBA(0x80, 0x80, 0x80, alpha), 0);
}

void BootSplash::render(int alpha)
{
    u64 mycolor = GS_SETREG_RGBA(0x1C, 0x1C, 0x1C, alpha);
    rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, mycolor);

    // The logo holds still and glows. It used to flip through the embedded logo0..logoN frames (a vertical
    // blur that read as a jump); those are a squash/stretch of one crisp image, so the first frame is the
    // logo and the "animation" is now a halo that breathes around it (guiDrawBootLogo).
    int logoTex = (gTheme->logoFrameCount >= 1) ? LOGO0_PICTURE : LOGO_PICTURE;

    // Sit the logo above centre rather than dead centre: the version + status lines hang below it,
    // so a centred logo pushes that block low and reads bottom-heavy. Clamped so a tall logo (or a
    // short theme height) can never ride off the top of the screen.
    int logoCenterY = (gTheme->usedHeight >> 1) - BOOT_LOGO_RISE;

    GSTEXTURE *logo = thmGetTexture(logoTex);
    if (logo) {
        int drawY = logoCenterY;
        if (drawY < (int)(logo->Height >> 1))
            drawY = logo->Height >> 1;

        drawLogo(logo, gGuiScreenWidth >> 1, drawY, alpha);
    }

    // Credit mark, centred between the version line and the status line, faded with the splash.
    GSTEXTURE *credit = thmGetTexture(TECHWRITESCODE_LOGO);
    if (credit) {
        const int cw = credit->Width, ch = credit->Height; // drawn at native size
        const int creditY = ((logoCenterY + 80) + (gTheme->usedHeight - 40)) >> 1;
        rmDrawPixmap(credit, gGuiScreenWidth >> 1, creditY, ALIGN_CENTER, cw, ch, SCALING_RATIO, GS_SETREG_RGBA(0x80, 0x80, 0x80, alpha), 0);
    }

    // Boot info: the RiptOPL version + a live status line, faded with the splash.
    // Reuses gTheme->fonts[0] (always-loaded built-in) and the same OPL_VERSION About shows.
    u64 infoColor = GS_SETREG_RGBA(0x80, 0x80, 0x80, alpha);
    char verLine[48];
    snprintf(verLine, sizeof(verLine), "RiptOPL %s", OPL_VERSION);
    fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, logoCenterY + 80, ALIGN_CENTER, 0, 0, verLine, infoColor);
    // Prefer an IO-thread boot-step label (the localizer) over the main-thread scan/Ready line,
    // so a wedged step names itself. gBootStickyLabel is a single atomic pointer to a static string.
    const char *bootLine = status.line();
    if (bootLine != NULL)
        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, gTheme->usedHeight - 40, ALIGN_CENTER, 0, 0, bootLine, infoColor);
}

// Draw one standalone boot-splash frame: the same greeting guiIntroLoop() shows,
// at full opacity. Used as the boot "loading" screen instead of
// guiRenderTextScreen(), whose guiShow() call would render the not-yet-ready main
// menu (empty lists, no device selected) as a garbled landing page before the
// intro splash. This keeps the OPL logo on screen across the config load so boot
// shows the splash, never a half-drawn menu.
void BootSplash::renderScreen()
{
    guiStartFrame();
    render(0x80);
    guiEndFrame();
}

// The longest the greeting waits for the drive that holds the saved theme (RIPPS2 build 125 uses 6 s)
#define THEME_HOLD_MS 6000

void BootSplash::run()
{
    int greetingAlpha = 0x80;
    const int fadeFrameCount = 0x80 / 2;
    const int fadeDuration = (fadeFrameCount * 1000) / 55; // Average between 50 and 60 fps
    // Elapsed, not an absolute deadline: this comparison is the ONLY exit from the loop below,
    // so a clock() wrap between arming and testing it parks OPL on the boot splash for up to
    // ~71 minutes with no timeout and no escape. clock() is not reset when OPL is re-launched
    // from a game, so a console that has been on for a while rolls this dice on every boot.
    clock_t tFadeStart = 0;
    clock_t tFadeDelay = 0;
    int tFadeArmed = 0;
    // RIPPS2 build 125 (AK): a saved disk theme whose drive is not up yet keeps the greeting on screen, so the built-in
    // theme does not show for a moment before the saved one replaces it. Bounded: at most THEME_HOLD_MS after init.
    clock_t tInitDone = 0;
    int tInitSeen = 0;

    // oplIsBootInProgress() can become false while background startup work is still queued. That
    // is correct for runtime overlays, but the animated greeting owns the entire intro lifecycle.
    active_ = true;
    while (!ended_) {
        guiStartFrame();

        if (greetingAlpha < 0x80)
            guiShow();

        if (greetingAlpha > 0)
            render(greetingAlpha);

        // Initialize boot sound
        if (guiInitComplete() && !tFadeArmed && gEnableBootSND) {
            // Start playing sound
            sfxPlay(SFX_BOOT);
            // Calculate transition delay
            tFadeStart = clock();
            // CLAMP AT ZERO. sfxGetSoundDuration returns 0 when audsrv failed to initialise, and a
            // custom theme's boot sound can legitimately be shorter than the fade -- either way this
            // subtraction goes negative, and clock_t is UNSIGNED LONG, so the cast turns ~-1163 into
            // ~4.29 billion and the wait below becomes ~71 minutes on the boot splash with no exit.
            // The absolute-deadline form this replaced was accidentally immune (a deadline in the
            // past reads as already elapsed); converting it to the elapsed form is what exposed it.
            int fadeDelayMs = sfxGetSoundDuration(SFX_BOOT) - fadeDuration;
            if (fadeDelayMs < 0)
                fadeDelayMs = 0;
            tFadeDelay = (clock_t)fadeDelayMs * (CLOCKS_PER_SEC / 1000);
            tFadeArmed = 1;
        }

        if (guiInitComplete() && !tInitSeen) {
            tInitDone = clock();
            tInitSeen = 1;
        }
        const bool themeHold = greetingAlpha == 0x80 && tInitSeen && thmSavedThemePending() &&
                               (clock() - tInitDone) < (clock_t)THEME_HOLD_MS * (CLOCKS_PER_SEC / 1000);

        if (guiInitComplete() && (clock() - tFadeStart) >= tFadeDelay && !themeHold)
            greetingAlpha -= 2;

        if (greetingAlpha <= 0)
            ended_ = true;

        guiDrawOverlays();

        guiExecDeferredOps();

        guiEndFrame();

        gScreens.dispatchInput();

        // While the greeting waits for the saved theme, run the menu update hook as the main loop would: a drive that
        // comes up late is only picked up there (BdmGeneration), so without it the hold could never see the theme.
        if (themeHold)
            gGui.runFrameHook();
    }
    active_ = false;
}

// The C-style entry points other files use.
// Boot-splash status line setter. Pass NULL to clear. Main-thread only.
void guiSetBootStatus(const char *status)
{
    gSplash.status.set(status);
}

// Boot-step localizer setter, called from the deferred IO-thread boot steps. `label` MUST be a
// static string (an _l() lang entry or literal): only a POINTER to it is stored. If a step wedges,
// its label stays on the splash.
void guiSetBootStatusSticky(const char *label)
{
    gSplash.status.setSticky(label);
}

void guiSetBootStatusStickyCopy(const char *label)
{
    gSplash.status.setStickyCopy(label);
}

void guiRenderGreetingScreen(void)
{
    gSplash.renderScreen();
}

void guiIntroLoop(void)
{
    gSplash.run();
}
