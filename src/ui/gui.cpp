/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

#include "include/opl.h"
#include "include/plasma.h"
#include "include/gui.h"
#include "include/guiint.h"
#include "include/guiqueue.h"
#include "include/guiboot.h"
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
#include <math.h>
#include <stdlib.h>

// Last Played Auto Start
#include <time.h>

static int gTerminate;
static int gInitComplete;

static gui_callback_t gFrameHook;

static DeferredOpQueue gOps;
static s32 gGUILockSemaId = -1; // -1 = not created yet (guiLock/guiUnlock no-op; see guiLock)
static ee_sema_t gQueueSema;

int gGuiScreenWidth;
int gGuiScreenHeight;

static int showPartPopup = 0;
static int showThmPopup;
static int showLngPopup;

// Notification popup: START tick + how long to hold, NOT an absolute deadline. clock() is a
// 32-bit microsecond counter, so it wraps every ~71.6 minutes; `clock() >= start + duration`
// silently becomes false-for-71-minutes whenever the sum crosses the wrap. The (clock() - start)
// ELAPSED form is correct across one wrap by ordinary unsigned arithmetic.
static clock_t popupStart;
static int popupArmed;

// How far ABOVE the vertical centre the boot logo (static splash and animated frames alike) sits.
// The version line follows it, so the whole boot block moves together; the status line stays
// anchored near the bottom edge.
constexpr int BOOT_LOGO_RISE = 100;

static BootStatus gBoot;

#ifdef __DEBUG

// debug version displays an FPS meter
static clock_t prevtime = 0;
static clock_t curtime = 0;
static float fps = 0.0f;

extern GSGLOBAL *gsGlobal;
#endif

// Global data
int guiInactiveFrames;
int guiFrameId;

typedef struct
{
    void (*handleInput)(void);
    void (*renderScreen)(void);
    short inMenu;
} gui_screen_handler_t;

static gui_screen_handler_t screenHandlers[] = {{&menuHandleInputMain, &menuRenderMain, 0},
                                                {&menuHandleInputMenu, &menuRenderMenu, 1},
                                                {&menuHandleInputInfo, &menuRenderInfo, 1},
                                                {&menuHandleInputGameMenu, &menuRenderGameMenu, 1},
                                                {&menuHandleInputAppMenu, &menuRenderAppMenu, 1}};

// default screen handler (menu screen)
static gui_screen_handler_t *screenHandler = &screenHandlers[GUI_SCREEN_MENU];

// screen transition handling
static gui_screen_handler_t *screenHandlerTarget = NULL;
static int transIndex;

static PlasmaBackground gPlasma;

// Whether the active screen handler is one of the in-menu screens (dialogs use it to pick their backdrop).
int guiScreenInMenu(void)
{
    return screenHandler->inMenu;
}

void guiReloadScreenExtents()
{
    rmGetScreenExtents(&gGuiScreenWidth, &gGuiScreenHeight);
}

void guiInit(void)
{
    guiFrameId = 0;
    guiInactiveFrames = 0;

    gFrameHook = NULL;
    gTerminate = 0;
    gInitComplete = 0;
    gQueueSema.init_count = 1;
    gQueueSema.max_count = 1;
    gQueueSema.option = 0;

    gOps.init();
    gGUILockSemaId = CreateSema(&gQueueSema);

    guiReloadScreenExtents();

    gPlasma.init();
}

void guiEnd()
{
    gPlasma.shutdown();

    gOps.shutdown();
    DeleteSema(gGUILockSemaId);
    gGUILockSemaId = -1; // post-shutdown callers no-op instead of waiting on a deleted id
}

void guiLock(void)
{
    // Not-ready guard: lngInit/thmInit rebuild the GUI name lists BEFORE guiInit creates this
    // semaphore (opl.c init order), and everything is single-threaded until the GUI/IO threads
    // exist -- a no-op lock is correct there, and WaitSema on id 0/garbage is not.
    if (gGUILockSemaId < 0)
        return;
    WaitSema(gGUILockSemaId);
}

void guiUnlock(void)
{
    if (gGUILockSemaId < 0)
        return;
    SignalSema(gGUILockSemaId);
}

void guiStartFrame(void)
{
    guiLock();
    rmStartFrame();
    guiFrameId++;
}

// Frame-time watchdog for the debug HUD. It answers the one question nobody asked about the periodic
// swallowed navigation step: DOES THE EE STALL AT ALL when it happens?
//
// Every theory this session assumed the menu thread stops for a moment. If it does, this catches the
// overrun. If frames stay on vsync straight THROUGH a swallowed step, the EE polled the pad on time
// and freepad had nothing to give it -- the input died on the IOP over SIO2, and no EE-side work
// (thread priority, art queues, device probing) could ever have fixed it. Which would explain why
// none of it has.
static u32 gFrameLastUs = 0;
static u32 gFrameWorstUs = 0;
static u32 gFrameOverruns = 0; // frames longer than ~1.5 vsyncs

static u32 guiGetFrameWatchdog(u32 *worstUs, u32 *overruns)
{
    if (worstUs)
        *worstUs = gFrameWorstUs;
    if (overruns)
        *overruns = gFrameOverruns;
    return gFrameLastUs;
}

void guiEndFrame(void)
{
    rmEndFrame();
    static clock_t frameMark = 0;

    // clock() is microseconds on the EE. Sampled after rmEndFrame (i.e. after the vsync wait) so
    // this measures whole frames back to back.
    clock_t nowUs = clock();
    if (frameMark != 0) {
        u32 elapsed = (u32)(nowUs - frameMark);
        gFrameLastUs = elapsed;
        if (elapsed > gFrameWorstUs)
            gFrameWorstUs = elapsed;
        if (elapsed > 25000)
            gFrameOverruns++;
    }
    frameMark = nowUs;

#ifdef __DEBUG
    // Measure time directly after vsync
    prevtime = curtime;
    curtime = clock();
#endif
    guiUnlock();

    // Rotate priority 31 ready queue so peer threads (such as BGM I/O decode) get timeslices
    // even during continuous frames or animations.
    RotateThreadReadyQueue(31);
}

void guiShowAbout()
{
    char OPLVersion[40];
    char OPLBuildDetails[40];

    snprintf(OPLVersion, sizeof(OPLVersion), "Open PS2 Loader %s", OPL_VERSION);
    diaSetLabel(diaAbout, ABOUT_TITLE, OPLVersion);

    snprintf(OPLBuildDetails, sizeof(OPLBuildDetails), "GSM %s"
                                                       " - UDMA+"
#ifdef __RTL
                                                       " - RTL"
#endif
#ifdef IGS
                                                       " - IGS %s"
#endif
#ifdef PADEMU
                                                       " - PADEMU"
#endif
             // Version numbers
             ,
             GSM_VERSION
#ifdef IGS
             ,
             IGS_VERSION
#endif
    );
    diaSetLabel(diaAbout, ABOUT_BUILD_DETAILS, OPLBuildDetails);

    diaExecuteDialog(diaAbout, -1, 1, NULL);
}

void guiCheckNotifications(int checkTheme, int checkLang)
{
    if (gEnableNotifications) {
        if (checkTheme) {
            // Only DISK themes have a path to announce; the built-ins (<OPL>, <Coverflow>) return NULL
            // from thmGetFilePath. The old `!= 0` test was correct only while theme IDs ran 0..nThemes;
            // this tree's themes.c adds a built-in at nThemes+1, so a saved <Coverflow> passed the test
            // and fed NULL to the "%s" in _STR_THM_NOTIFICATION on EVERY boot. Coverflow is our default.
            if (thmGetFilePath(thmGetGuiValue()) != NULL)
                showThmPopup = 1;
        }

        if (checkLang) {
            if (lngGetGuiValue() != 0)
                showLngPopup = 1;
        }
    }
}

void guiResetNotifications(void)
{
    showThmPopup = 0;
    showLngPopup = 0;
    popupArmed = 0;
}

static void guiRenderNotifications(char *string, int y)
{
    int x;

    x = gGuiScreenWidth - rmUnScaleX(fntCalcDimensions(gTheme->fonts[0], string)) - 10;

    rmDrawRect(x - 10, y, gGuiScreenWidth - x, MENU_ITEM_HEIGHT + 10, gColDarker);
    fntRenderString(gTheme->fonts[0], x - 5, y + 5, ALIGN_NONE, 0, 0, string, gTheme->textColor);
}

static void guiShowNotifications(void)
{
    char notification[128];
    char *col_pos;
    int y = 10;
    int yadd = 35;

    if (showPartPopup || showThmPopup || showLngPopup || showCfgPopup || showNetDhcpPopup) {
        if (!popupArmed) {
            popupStart = clock();
            popupArmed = 1;
            sfxPlay(SFX_MESSAGE);
        }

        if (showPartPopup) {
            col_pos = strchr(gOPLPart, ':');
            col_pos++;

            snprintf(notification, sizeof(notification), _l(_STR_PARTITION_NOTIFICATION), col_pos);
            guiRenderNotifications(notification, y);
            y += yadd;
        }

        if (showCfgPopup) {
            // Official OPL's conf_opl.cfg is only a read-only seed (#545): say so, or "Config loaded"
            // reads as the user's RiptOPL settings on a device that has none (CosmicScale, APA-Jail).
            snprintf(notification, sizeof(notification), _l(configOplIsOfficialSeed() ? _STR_OFFICIAL_CFG_NOTIFICATION : _STR_CFG_NOTIFICATION), configGetLoadDir());
            if ((col_pos = strchr(notification, ':')) != NULL)
                *(col_pos + 1) = '\0';

            guiRenderNotifications(notification, y);
            y += yadd;
        }

        if (showThmPopup) {
            snprintf(notification, sizeof(notification), _l(_STR_THM_NOTIFICATION), thmGetFilePath(thmGetGuiValue()));
            if ((col_pos = strchr(notification, ':')) != NULL)
                *(col_pos + 1) = '\0';

            guiRenderNotifications(notification, y);
            y += yadd;
        }

        if (showLngPopup) {
            snprintf(notification, sizeof(notification), _l(_STR_LNG_NOTIFICATION), lngGetFilePath(lngGetGuiValue()));
            if ((col_pos = strchr(notification, ':')) != NULL)
                *(col_pos + 1) = '\0';

            guiRenderNotifications(notification, y);
            y += yadd;
        }

        // One-time network notice set at config load: UDP transports always use the saved static PS2
        // IP fields. If DHCP is the preserved SMB/HTTP preference, remind the user which address UDP uses.
        if (showNetDhcpPopup) {
            guiRenderNotifications(_l(_STR_UDPBD_NEEDS_STATIC_IP), y);
            y += yadd;
        }

        if ((clock() - popupStart) >= (clock_t)5000 * (CLOCKS_PER_SEC / 1000)) {
            guiResetNotifications();
            showPartPopup = 0;
            showCfgPopup = 0;
            showNetDhcpPopup = 0;
        }
    }
}

#ifdef RETROACHIEVEMENTS
/* RA notices: two-line feedback from the check/test actions, which run on the
   I/O thread and so cannot open a dialog. They set the text; the render loop
   draws it. Deliberately NOT gated on gEnableNotifications -- they are the
   answer to an action the user just took, not a courtesy popup. */
static char raNotice[2][96];
static int raNoticeLines = 0;
static clock_t raNoticeTimer = 0;

void guiShowRANotice(const char *line1, const char *line2)
{
    raNoticeLines = 0; /* renderer stops drawing before the new text lands */
    snprintf(raNotice[0], sizeof(raNotice[0]), "%s", line1 ? line1 : "");
    snprintf(raNotice[1], sizeof(raNotice[1]), "%s", line2 ? line2 : "");
    raNoticeTimer = 0;
    raNoticeLines = (line2 != NULL && line2[0] != '\0') ? 2 : 1;
}

static int guiRenderRANotices(int y, int yadd)
{
    int i;

    if (raNoticeLines <= 0)
        return y;

    if (!raNoticeTimer) {
        raNoticeTimer = clock() + 8000 * (CLOCKS_PER_SEC / 1000);
        sfxPlay(SFX_MESSAGE);
    }

    for (i = 0; i < raNoticeLines; i++) {
        guiRenderNotifications(raNotice[i], y);
        y += yadd;
    }

    if (clock() >= raNoticeTimer) {
        raNoticeLines = 0;
        raNoticeTimer = 0;
    }

    return y;
}

static void guiShowRANotices(void)
{
    guiRenderRANotices(10, 35);
}
#endif

int guiShowKeyboard(char *value, int maxLength)
{
    char tmp[maxLength];
    strncpy(tmp, value, maxLength);

    int result = diaShowKeyb(tmp, maxLength, 0, NULL);
    if (result) {
        strncpy(value, tmp, maxLength);
        value[maxLength - 1] = '\0';
    }

    return result;
}

int guiDeferUpdate(struct gui_update_t *op)
{
    return gOps.enqueue(op);
}

static void guiHandleOp(struct gui_update_t *item)
{
    submenu_list_t *result = NULL;

    switch (item->type) {
        case GUI_INIT_DONE:
            gInitComplete = 1;
            break;

        case GUI_OP_ADD_MENU:
            menuAppendItem(item->menu.menu);
            break;

        case GUI_OP_APPEND_MENU:
            result = submenuAppendItem(item->menu.subMenu, item->submenu.icon_id, item->submenu.text, item->submenu.id, item->submenu.text_id);
            if (result != NULL) {
                result->item.isFolder = item->submenu.isFolder; // folder-browse row marker
#ifdef RETROACHIEVEMENTS
                result->item.raBadged = item->submenu.raBadged; // RA cover mark, resolved on the I/O thread
#endif
            }
            // coverflow wrap tail: submenuAppendItem always returns the new tail
            item->menu.menu->last = result;
            if (!item->menu.menu->submenu) { // first subitem in list
                item->menu.menu->submenu = result;
                if (!item->submenu.selected) {
                    item->menu.menu->current = result;
                    item->menu.menu->pagestart = result;
                }
            }
            if (item->submenu.selected) { // restore cursor or select the remembered last-played game
                item->menu.menu->current = result;
                item->menu.menu->pagestart = result;
                item->menu.menu->remindLast = 1;

                // Last Played Auto Start
                if (item->submenu.autoStart && gRememberLastPlayed && gAutoStartLastPlayed && !(KeyPressedOnce))
                    DisableCron = 0; // Release Auto Start Last Played counter
            }

            break;

        case GUI_OP_SELECT_MENU:
            menuSetSelectedItem(item->menu.menu);
            screenHandler = &screenHandlers[GUI_SCREEN_MAIN];
            break;

        case GUI_OP_CLEAR_SUBMENU:
            submenuDestroy(item->menu.subMenu);
            item->menu.menu->submenu = NULL;
            item->menu.menu->current = NULL;
            item->menu.menu->pagestart = NULL;
            item->menu.menu->last = NULL; // coverflow wrap tail
            break;

        case GUI_OP_SORT: {
            int mode = -1;
            item_list_t *list = (item_list_t *)item->menu.menu->userdata;
            if (list != NULL)
                mode = list->mode;
            submenuSort(item->menu.subMenu, mode);
        }
            item->menu.menu->submenu = *item->menu.subMenu;

            { // recompute the coverflow wrap tail after the sort reorders the list
                submenu_list_t *tail = item->menu.menu->submenu;
                while (tail && tail->next)
                    tail = tail->next;
                item->menu.menu->last = tail;
            }

            if (!item->menu.menu->remindLast)
                item->menu.menu->current = item->menu.menu->submenu;

            item->menu.menu->pagestart = item->menu.menu->current;
            break;

        case GUI_OP_ADD_HINT:
            // append the hint list in the menu item
            menuHintsLock();
            menuAddHint(item->menu.menu, item->hint.text_id, item->hint.icon_id);
            menuHintsUnlock();
            break;

        default:
            LOG("GUI: ??? (%d)\n", item->type);
    }
}

void guiExecDeferredOps(void)
{
    // Clears deferred operations list by executing them.
    gOps.drain(guiHandleOp);
}

void guiDrawBusy(int alpha)
{
    if (gTheme->loadingIcon) {
        GSTEXTURE *texture = thmGetTexture(LOAD0_ICON + (guiFrameId >> 1) % gTheme->loadingIconCount);
        if (texture && texture->Mem) {
            u64 mycolor = GS_SETREG_RGBA(0x80, 0x80, 0x80, alpha);
            rmDrawPixmap(texture, gTheme->loadingIcon->posX, gTheme->loadingIcon->posY, gTheme->loadingIcon->aligned, gTheme->loadingIcon->width, gTheme->loadingIcon->height, gTheme->loadingIcon->scaled, mycolor, 0);
        }
    }
}

/* Set only for the guiShow() call inside guiRenderProbeFrame: while a probe wait owns the
   screen, guiDrawSubMenuHints skips the menu's hint row so the probe's Cancel hint is the only
   thing drawn on the bottom-right strip (issue #465). */
static int gProbeFrameActive = 0;

/* Render ONE frame of the current screen plus the busy animation, for a synchronous main-thread
   wait on hardware that cannot be moved off that thread.

   guiMainLoop dispatches handleInput AFTER guiEndFrame precisely so an input handler may drive
   renderman, so this is safe from there -- but it takes guiLock, so it must never be called from
   inside an existing guiStartFrame/guiEndFrame bracket or from any other thread.

   The vsync wait inside guiEndFrame also paces the caller's poll loop, which is why callers can
   drop their own DelayThread and measure the budget with clock().

   Returns 1 when the user pressed cancel, which the caller is expected to treat as "abandon the
   wait". This is the ONLY poll running during such a wait -- guiMainLoop is not turning -- so the
   pad read has to happen here, outside the guiLock bracket above, exactly as the other synchronous
   wait loops in this file do (issue #465: a drive that answers slowly must still be escapable). */
int guiRenderProbeFrame(void)
{
    int hints[1] = {_STR_CANCEL};
    int icons[1] = {gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON};
    int x;

    guiStartFrame();
    // The screen renderer's own hint row shares this bottom-right strip (same y, same right
    // edge), so drawing the Cancel hint over it overdraws glyphs on glyphs -- the garbled
    // "combined" string reported in #465. The menu hints are dead input while the wait runs
    // anyway, so suppress them for this frame and let Cancel own the strip.
    gProbeFrameActive = 1;
    guiShow();
    gProbeFrameActive = 0;
    guiDrawBusy(0x80);
    // Without this the escape hatch is invisible: the probe frame is the unchanged menu plus a
    // spinner, so nothing tells the user the wait can be abandoned at all.
    x = guiAlignSubMenuHints(1, hints, icons, gTheme->fonts[0], 12, 2);
    guiDrawIconAndText(icons[0], hints[0], gTheme->fonts[0], x, gTheme->usedHeight - 32, gTheme->textColor);
    guiEndFrame();

    readPads();
    if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
        sfxPlay(SFX_CANCEL);
        return 1;
    }

    return 0;
}

// Boot-splash status line setter. Pass NULL to clear. Main-thread only.
void guiSetBootStatus(const char *status)
{
    gBoot.set(status);
}

// Boot-step localizer setter, called from the deferred IO-thread boot steps. `label` MUST be a
// static string (an _l() lang entry or literal): only a POINTER to it is stored. If a step wedges,
// its label stays on the splash.
void guiSetBootStatusSticky(const char *label)
{
    gBoot.setSticky(label);
}

void guiSetBootStatusStickyCopy(const char *label)
{
    gBoot.setStickyCopy(label);
}

// Boot logo glow: a soft halo breathes around the logo. The halo is the logo redrawn at low alpha around its
// own position -- eight taps 2 px out and four 5 px out -- so it follows the letter shapes with no extra asset.
// The halo is tinted with the theme's selected-text colour, so it follows the user's colour settings (the white
// logo texture takes the tint; the logo itself stays white). One cycle is BOOT_LOGO_GLOW_PERIOD frames (~2 s at 60 Hz); the pulse is a raised cosine, so it eases in and
// out instead of stepping. `alpha` is the greeting's fade alpha (0..0x80): the glow fades with the splash.
constexpr int BOOT_LOGO_GLOW_PERIOD = 120;
constexpr float BOOT_LOGO_GLOW_MIN = 0.15f; // halo strength at the dim end of the pulse
constexpr float BOOT_LOGO_GLOW_TAP = 0.16f; // per-tap alpha at full pulse, as a fraction of `alpha`

static void guiDrawBootLogo(GSTEXTURE *logo, int cx, int cy, int alpha)
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

static void guiRenderGreeting(int alpha)
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

        guiDrawBootLogo(logo, gGuiScreenWidth >> 1, drawY, alpha);
    }

    // Boot info: the RiptOPL version + a live status line, faded with the splash.
    // Reuses gTheme->fonts[0] (always-loaded built-in) and the same OPL_VERSION About shows.
    u64 infoColor = GS_SETREG_RGBA(0x80, 0x80, 0x80, alpha);
    char verLine[48];
    snprintf(verLine, sizeof(verLine), "RiptOPL %s", OPL_VERSION);
    fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, logoCenterY + 80, ALIGN_CENTER, 0, 0, verLine, infoColor);
    // Prefer an IO-thread boot-step label (the localizer) over the main-thread scan/Ready line,
    // so a wedged step names itself. gBootStickyLabel is a single atomic pointer to a static string.
    const char *bootLine = gBoot.line();
    if (bootLine != NULL)
        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, gTheme->usedHeight - 40, ALIGN_CENTER, 0, 0, bootLine, infoColor);
}

// Draw one standalone boot-splash frame: the same greeting guiIntroLoop() shows,
// at full opacity. Used as the boot "loading" screen instead of
// guiRenderTextScreen(), whose guiShow() call would render the not-yet-ready main
// menu (empty lists, no device selected) as a garbled landing page before the
// intro splash. This keeps the OPL logo on screen across the config load so boot
// shows the splash, never a half-drawn menu.
void guiRenderGreetingScreen(void)
{
    guiStartFrame();
    guiRenderGreeting(0x80);
    guiEndFrame();
}

void guiDrawBGPlasma()
{
    gPlasma.draw(gTheme->bgColor, gTheme->plasBlendColor, gGuiScreenWidth, gGuiScreenHeight);
}

void guiDrawSubMenuHints(void)
{
    // During a probe wait these hints promise input the menu is not reading; the only live
    // action is the Cancel hint guiRenderProbeFrame draws on this same strip (#465).
    if (gProbeFrameActive)
        return;

    int subMenuHints[2] = {_STR_SELECT, _STR_GAMES_LIST};
    int subMenuIcons[2] = {CIRCLE_ICON, CROSS_ICON};

    int x = guiAlignSubMenuHints(2, subMenuHints, subMenuIcons, gTheme->fonts[0], 12, 2);
    int y = gTheme->usedHeight - 32;

    x = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? subMenuIcons[0] : subMenuIcons[1], subMenuHints[0], gTheme->fonts[0], x, y, gTheme->textColor);
    x += 12;
    guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? subMenuIcons[1] : subMenuIcons[0], subMenuHints[1], gTheme->fonts[0], x, y, gTheme->textColor);
}

static int endIntro = 0;           // Break intro loop and start 'Last Played Auto Start' countdown
static int gIntroSplashActive = 0; // The intro is a splash, never a generic I/O-spinner screen.
void guiDrawOverlays(void)
{
    // are there any pending operations?
    int pending = ioHasPendingRequests();
    static int busyAlpha = 0x00; // Fully transparant

    if (!pending) {
        // Fade out
        if (busyAlpha > 0x00)
            busyAlpha -= 0x02;
    } else {
        // Fade in
        if (busyAlpha < 0x80)
            busyAlpha += 0x02;
    }

    if (!oplIsBootInProgress() && !gIntroSplashActive && busyAlpha > 0x00)
        guiDrawBusy(busyAlpha);

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

    if (prevtime != 0) {
        clock_t diff = curtime - prevtime;
        if (diff == 0)
            diff = 1;

        // Raw FPS value with 2 decimal places
        float rawfps = ((100 * CLOCKS_PER_SEC) / diff) / 100.0f;

        if (fps == 0.0f)
            fps = rawfps;
        else
            fps = fps * 0.9f + rawfps / 10.0f; // Smooth FPS value

        snprintf(text, sizeof(text), "%.1f FPS", fps);
        fntRenderString(gTheme->fonts[0], x, y, ALIGN_LEFT, 0, 0, text, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
        y += yadd;
    }
#endif

    // Last Played Auto Start
    if (!pending && gRememberLastPlayed && gAutoStartLastPlayed && DisableCron == 0 && endIntro) {
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

    // Art pipeline HUD (Settings -> Advanced -> Debug). Q = requests queued, A = being read/decoded
    // right now, R = turned away by the depth cap, D = completed. What it is for: "art stops after a
    // few covers" has two very different causes that look identical on screen. If D keeps climbing
    // the pipeline is working and simply slow. If D stops while R climbs, requests are being made
    // and refused -- and if Q stays pinned at the cap with A at 0 and nothing completing, the
    // in-flight counter has drifted above the real number in flight and is refusing everything
    // forever. Cheap enough to leave in: four integer reads and one string, only when debug is on.
    if (gEnableDebug) {
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
        u32 fLast = guiGetFrameWatchdog(&fWorst, &fOver);
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
}

static void guiReadPads()
{
    // A transition polls without dispatching input. Freeze against the triggering sample so that
    // button cannot replay on the destination, while a different button held through the fade can
    // still register there (see padFreezeEdgeBaseline in pad.c -- deliberately NOT a poll pause).
    padFreezeEdgeBaseline(screenHandlerTarget != NULL);

    if (readPads())
        guiInactiveFrames = 0;
    else
        guiInactiveFrames++;

    // Art thread's per-frame tick, and it must sit HERE, immediately after the poll: it samples the
    // currently HELD direction so the worker can refuse to start an SIO2 cover mid-navigation, and
    // that sample is only meaningful against a pad state read this frame. It also re-wakes a worker
    // that has queued work but nothing running, which closes the lost-wakeup class outright.
    // O(1), never blocks, and every call inside it is GUI-thread-only by design.
    cacheTickArt();
}

// renders the screen and handles inputs. Also handles screen transitions between numerous
// screen handlers. Fade transition code written by Maximus32
void guiShow()
{
    // is there a transmission effect going on or are
    // we in a normal rendering state?
    if (screenHandlerTarget) {
        u8 alpha;
        const u8 transition_frames = 26;
        if (transIndex < (transition_frames / 2)) {
            // Fade-out old screen
            // index: 0..7
            // alpha: 1..8 * transition_step
            screenHandler->renderScreen();
            alpha = gPlasma.ease((float)(transIndex + 1) / (transition_frames / 2)) * 0x80;
        } else {
            // Fade-in new screen
            // index: 8..15
            // alpha: 8..1 * transition_step
            screenHandlerTarget->renderScreen();
            alpha = gPlasma.ease((float)(transition_frames - transIndex) / (transition_frames / 2)) * 0x80;
        }

        // Overlay the actual "fade"
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, GS_SETREG_RGBA(0x00, 0x00, 0x00, alpha));

        // Advance the effect
        transIndex++;
        if (transIndex >= transition_frames) {
            screenHandler = screenHandlerTarget;
            screenHandlerTarget = NULL;
        }
    } else
        // render with the set screen handler
        screenHandler->renderScreen();
}

void guiIntroLoop(void)
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

    // oplIsBootInProgress() can become false while background startup work is still queued. That
    // is correct for runtime overlays, but the animated greeting owns the entire intro lifecycle.
    gIntroSplashActive = 1;
    while (!endIntro) {
        guiStartFrame();

        if (greetingAlpha < 0x80)
            guiShow();

        if (greetingAlpha > 0)
            guiRenderGreeting(greetingAlpha);

        // Initialize boot sound
        if (gInitComplete && !tFadeArmed && gEnableBootSND) {
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

        if (gInitComplete && (clock() - tFadeStart) >= tFadeDelay)
            greetingAlpha -= 2;

        if (greetingAlpha <= 0)
            endIntro = 1;

        guiDrawOverlays();

        gOps.drain(guiHandleOp);

        guiEndFrame();

        if (!screenHandlerTarget && screenHandler)
            screenHandler->handleInput();
    }
    gIntroSplashActive = 0;
}

void guiMainLoop(void)
{
    guiResetNotifications();
    guiCheckNotifications(1, 1);

    if (gOPLPart[0] != '\0')
        showPartPopup = 1;

    if (gEnableBGM)
        bgmStart();

    while (!gTerminate) {
        guiStartFrame();

        // Read the pad states to prepare for input processing in the screen handler
        guiReadPads();

        // handle inputs and render screen
        guiShow();

        // Render overlaying gui thingies :)
        guiDrawOverlays();

        // The DHCP notice is a misconfiguration warning, not a courtesy popup -- show it even
        // when the user has notifications off (it explains an otherwise-silent empty games page).
        if (gEnableNotifications || showNetDhcpPopup)
            guiShowNotifications();

#ifdef RETROACHIEVEMENTS
        // RA action feedback: always drawn while a notice is set (it is the answer to
        // something the user just asked for), independent of gEnableNotifications.
        guiShowRANotices();
#endif

        // handle deferred operations
        gOps.drain(guiHandleOp);

        guiEndFrame();

        // if not transiting, handle input
        // done here so we can use renderman if needed
        if (!screenHandlerTarget && screenHandler)
            screenHandler->handleInput();

        if (gFrameHook)
            gFrameHook();
    }
}

void guiSetFrameHook(gui_callback_t cback)
{
    gFrameHook = cback;
}

void guiSwitchScreen(int target)
{
    // Only initiate the transition once or else we could get stuck in an infinite loop.
    if (screenHandlerTarget != NULL) {
        return;
    }
    sfxPlay(SFX_TRANSITION);
    transIndex = 0;
    screenHandlerTarget = &screenHandlers[target];
}

struct gui_update_t *guiOpCreate(gui_op_type_t type)
{
    struct gui_update_t *op = (struct gui_update_t *)malloc(sizeof(struct gui_update_t));

    // The memset used to run unconditionally, so an allocation failure faulted HERE rather than
    // returning NULL -- which made every "if (!op)" guard at the call sites unreachable code. The
    // callers were written expecting to handle OOM; let them.
    if (op == NULL)
        return NULL;

    memset(op, 0, sizeof(struct gui_update_t));
    op->type = type;
    return op;
}

void guiUpdateScrollSpeed(void)
{
    // sanitize the settings
    if ((gScrollSpeed < 0) || (gScrollSpeed > 2))
        gScrollSpeed = 1;

    // update the pad delays for KEY_UP and KEY_DOWN
    // default delay is 7
    // fast - 100 ms
    // medium - 300 ms
    // slow - 500 ms
    setButtonDelay(KEY_UP, 500 - gScrollSpeed * 200); // 0,1,2 -> 500, 300, 100
    setButtonDelay(KEY_DOWN, 500 - gScrollSpeed * 200);
}

void guiUpdateScreenScale(void)
{
    fntUpdateAspectRatio();
}
