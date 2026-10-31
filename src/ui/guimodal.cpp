/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

// Modal dialogs: message box, reboot prompt, remove-settings prompt, cheat selector, warning. Split out of gui.cpp.

#include "include/cheatconfig.h"
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

#include "include/guimodal.h"

int ModalScreen::run()
{
    begin();

    bool done = false;
    while (!done) {
        guiStartFrame();

        readPads();

        done = onKeys();

        draw();

        guiEndFrame();
    }

    return finish();
}

void ModalScreen::begin()
{
    sfxPlay(SFX_MESSAGE);
}

// Greedily word-wrap `text` to innerW, writing '\n'-separated lines into out. Returns the line count.
//
// fntRenderString does NOT wrap: it lays a string on ONE line and CLIPS at the window edge (only an
// explicit '\n' starts a new line), so a long message ran off BOTH screen edges and was unreadable
// -- reported on the 1080p warning, which is simply the longest string we ship.
// fntWrapText does the work, the same one the settings hint box uses.
int MessageBoxScreen::wrapText(const char *text, int innerW, char *out, int outSize)
{
    return fntWrapText(gTheme->fonts[0], text, innerW, out, outSize);
}

// Render the wrapped lines ONE AT A TIME: ALIGN_CENTER centres on the measured width of the
// WHOLE string, so handing it a multi-line buffer would centre it as a single long run and leave
// every line off-centre. The block is vertically centred on the old single-line position, so a
// one-line message stays where it was.
int guiDrawWrappedLines(const char *wrapped, int lines)
{
    int lineH = MENU_ITEM_HEIGHT;
    int ly = (gTheme->usedHeight >> 1) - ((lines - 1) * lineH) / 2;
    int lastY = ly;
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
        lastY = ly;
        ly += lineH;
        ls = eol ? eol + 1 : NULL;
    }
    return lastY;
}

// The message box width for the screens that drew their text as one unbounded line (text screen,
// warning, video-mode confirmation, remove-settings prompt): a long translation ran past the rules
// and off the screen. Ported from YAOPL 6a091fc (Roman Kelesidis).
int guiDrawMsgText(const char *text)
{
    char wrapped[512];
    int lines = fntWrapText(gTheme->fonts[0], text, gGuiScreenWidth - 120, wrapped, sizeof(wrapped));
    return guiDrawWrappedLines(wrapped, lines);
}

MessageBoxScreen::MessageBoxScreen(const char *text, int addAccept, struct UIItem *ui)
    : addAccept_(addAccept), ui_(ui)
{
    lines_ = wrapText(text, gGuiScreenWidth - 120, wrapped_, sizeof(wrapped_));
}

bool MessageBoxScreen::onKeys()
{
    if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE))
        terminate_ = 1;
    else if (getKeyOn(gSelectButton))
        terminate_ = 2;

    return terminate_ != 0;
}

void MessageBoxScreen::draw()
{
    if (ui_)
        diaRenderUI(ui_, guiScreenInMenu(), NULL, 0);

    // With a `ui` the CALLER chose what sits behind this box, so keep that context visible.
    // A context-free error paints its own opaque background. Do not render the active menu behind
    // it: that can queue cover IO while a launch preflight has pfs0: remounted (original #1034).
    rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, ui_ ? gColDarker : gColBlack);

    rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
    rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

    guiDrawWrappedLines(wrapped_, lines_);
    guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_BACK, gTheme->fonts[0], 500, 417, gTheme->textColor);
    if (addAccept_)
        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON, _STR_ACCEPT, gTheme->fonts[0], 70, 417, gTheme->textColor);
}

int MessageBoxScreen::finish()
{
    if (terminate_ == 1) {
        sfxPlay(SFX_CANCEL);
    }
    if (terminate_ == 2) {
        sfxPlay(SFX_CONFIRM);
    }

    return terminate_ - 1;
}

int guiIsActive(void)
{
    return gTheme != NULL;
}

int guiMsgBox(const char *text, int addAccept, struct UIItem *ui)
{
    // Same pre-GUI guard as guiWarning: autolaunch (miniInit) never runs rmInit/thmInit/guiInit or
    // starts the pads, so a launch-path error box would draw through a NULL gsGlobal/gTheme and then
    // wait forever for a button -- a black screen instead of a refused launch. Answer "back" (0, see
    // MessageBoxScreen::finish) so the caller unwinds and autolaunch falls back to the menu.
    if (gTheme == NULL) {
        LOG("guiMsgBox (pre-GUI): %s\n", text);
        return 0;
    }

    MessageBoxScreen box(text, addAccept, ui);
    return box.run();
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

bool RebootIopPrompt::onKeys()
{
    if (getKeyOn(guiConfirmKey()))
        terminate_ = 1;
    else if (getKeyOn(guiCancelKey()))
        terminate_ = 2;
    else if (getKeyOn(KEY_TRIANGLE))
        terminate_ = 3;

    return terminate_ != 0;
}

void RebootIopPrompt::draw()
{
    guiShow();

    rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);

    rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
    rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

    fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, gTheme->usedHeight >> 1, ALIGN_CENTER, 0, 0, "REBOOT IOP?", gTheme->textColor);

    guiDrawIconAndLiteralText(guiConfirmIcon(), "YES", gTheme->fonts[0], 70, 417, gTheme->textColor);
    guiDrawIconAndLiteralText(TRIANGLE_ICON, _l(_STR_CANCEL), gTheme->fonts[0], 285, 417, gTheme->textColor);
    guiDrawIconAndLiteralText(guiCancelIcon(), "NO", gTheme->fonts[0], 500, 417, gTheme->textColor);
}

int RebootIopPrompt::finish()
{
    if (terminate_ == 1) {
        sfxPlay(SFX_CONFIRM);
        return 1;
    }
    if (terminate_ == 2) {
        sfxPlay(SFX_CONFIRM);
        return 0;
    }

    sfxPlay(SFX_CANCEL);
    return -1;
}

int guiPromptRebootIop(void)
{
    RebootIopPrompt prompt;
    return prompt.run();
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

    guiDrawMsgText(text);

    guiEndFrame();

    delay(count);
}

bool RemoveSettingsPrompt::onKeys()
{
    if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE))
        terminate_ = 1;
    else if (getKeyOn(gSelectButton))
        terminate_ = 2;
    else if (getKeyOn(KEY_SQUARE))
        terminate_ = 3;
    else if (getKeyOn(KEY_TRIANGLE))
        terminate_ = 4;

    return terminate_ != 0;
}

void RemoveSettingsPrompt::draw()
{
    guiShow();

    // Opaque for the same reason as guiConfirmVideoMode: the backdrop is just guiShow().
    rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);

    rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
    rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

    guiDrawMsgText(_l(_STR_GAME_SETTINGS_PROMPT));

    guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_BACK, gTheme->fonts[0], 500, 417, gTheme->textColor);
    guiDrawIconAndText(SQUARE_ICON, _STR_GLOBAL_SETTINGS, gTheme->fonts[0], 213, 417, gTheme->textColor);
    guiDrawIconAndText(TRIANGLE_ICON, _STR_ALL_SETTINGS, gTheme->fonts[0], 356, 417, gTheme->textColor);
    guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON, _STR_PERGAME_SETTINGS, gTheme->fonts[0], 70, 417, gTheme->textColor);
}

int RemoveSettingsPrompt::finish()
{
    char message[256];

    if (terminate_ == 1) {
        sfxPlay(SFX_CANCEL);
        return 0;
    } else if (terminate_ == 2) {
        guiGameRemoveSettings(configSet_);
        snprintf(message, sizeof(message), _l(_STR_GAME_SETTINGS_REMOVED), _l(_STR_PERGAME_SETTINGS));
    } else if (terminate_ == 3) {
        guiGameRemoveGlobalSettings(configGame_);
        snprintf(message, sizeof(message), _l(_STR_GAME_SETTINGS_REMOVED), _l(_STR_GLOBAL_SETTINGS));
    } else if (terminate_ == 4) {
        guiGameRemoveSettings(configSet_);
        guiGameRemoveGlobalSettings(configGame_);
        snprintf(message, sizeof(message), _l(_STR_GAME_SETTINGS_REMOVED), _l(_STR_ALL_SETTINGS));
    }
    sfxPlay(SFX_CONFIRM);
    guiMsgBox(message, 0, NULL);

    return 1;
}

int guiGameShowRemoveSettings(config_set_t *configSet, config_set_t *configGame)
{
    RemoveSettingsPrompt prompt(configSet, configGame);
    return prompt.run();
}

CheatScreen::CheatScreen(config_set_t *configSet)
    : configSet_(configSet)
{
    while (cheatCount_ < MAX_CODES && gCheats[cheatCount_].name[0] != '\0')
        cheatCount_++;
}

bool CheatScreen::onKeys()
{
    if (getKeyOn(KEY_UP) && selected_ > 0) {
        selected_ -= 1;
        if (selected_ < offset_)
            offset_ = selected_;
    }

    if (getKeyOn(KEY_DOWN) && selected_ < cheatCount_ - 1) {
        selected_ += 1;
        if (selected_ >= offset_ + VISIBLE)
            offset_ = selected_ - VISIBLE + 1;
    }

    if (getKeyOn(gSelectButton)) {
        if (!cheatConfigIsMasterCode(gCheats[selected_].name))
            gCheats[selected_].enabled = !gCheats[selected_].enabled;
    }

    if (getKeyOn(KEY_SQUARE)) {
        // Disable All: clear every cheat's enabled flag in one press. Skip the mastercode (the
        // per-cheat toggle can't touch it either -- it is the engine enabler, not a cheat).
        for (int i = 0; i < cheatCount_; i++) {
            if (!cheatConfigIsMasterCode(gCheats[i].name))
                gCheats[i].enabled = 0;
        }
        sfxPlay(SFX_CURSOR);
    }

    if (getKeyOn(KEY_START))
        return true;

    return false;
}

void CheatScreen::draw()
{
    guiShow();

    // Opaque for the same reason as guiConfirmVideoMode: the backdrop is just guiShow(), and
    // the cheat names are far easier to read on black than over the menu.
    rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);
    rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
    rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

    fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, 60, ALIGN_CENTER, 0, 0, _l(_STR_CHEAT_SELECTION), gTheme->textColor);

    int renderedCheats = 0;
    for (int i = offset_; renderedCheats < VISIBLE && i < cheatCount_; i++) {
        if (gCheats[i].name[0] == '\0')
            continue;

        int enabled = gCheats[i].enabled;

        int boxX = 50;
        int boxY = 100 + (renderedCheats * 30);
        int boxWidth = rmWideScale(25);
        int boxHeight = 17;

        if (enabled)
            rmDrawFramedRect(boxX, boxY + 3, boxWidth, boxHeight, 2, gTheme->textColor, gTheme->selTextColor);

        u32 textColour = (i == selected_) ? gTheme->selTextColor : gTheme->textColor;
        fntRenderString(gTheme->fonts[0], boxX + 35, boxY + 3, ALIGN_LEFT, 0, 0, gCheats[i].name, textColour);

        renderedCheats++;
    }

    guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON, _STR_SELECT, gTheme->fonts[0], 70, 417, gTheme->textColor);
    guiDrawIconAndText(SQUARE_ICON, _STR_DISABLE_ALL, gTheme->fonts[0], 270, 417, gTheme->textColor);
    guiDrawIconAndText(START_ICON, _STR_RUN, gTheme->fonts[0], 500, 417, gTheme->textColor);
}

int CheatScreen::finish()
{
    // Remember these picks for the game's next launch, on either core (upstream #1748). A save that
    // fails only costs the memory of them: this launch still runs with what was just chosen.
    cheatConfigSaveSelections(configSet_);
    if (configSet_ != NULL && !configWrite(configSet_))
        LOG("CHEATS: could not save this game's cheat picks\n");
    sfxPlay(SFX_CONFIRM);
    return 0;
}

void guiManageCheats(config_set_t *configSet)
{
    if (gCheats == NULL) // defensive: the menu is only reachable after load_cheats, but never deref NULL
        return;

    CheatScreen screen(configSet);
    screen.run();
}
