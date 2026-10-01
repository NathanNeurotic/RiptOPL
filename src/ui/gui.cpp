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
#include "include/guiscreens.h"
#include "include/guiframe.h"
#include "include/guioverlay.h"
#include "include/guisplash.h"
#include "include/guinotify.h"
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


// Global data
int guiInactiveFrames;
int guiFrameId;

static PlasmaBackground gPlasma;

void guiShow()
{
    gScreens.show(gPlasma);
}

int guiInitComplete(void)
{
    return gInitComplete;
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

void guiEndFrame(void)
{
    rmEndFrame();
    gFrames.endFrame();

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
            gScreens.setCurrent(GUI_SCREEN_MAIN);
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
    gOverlays.setProbeFrame(true);
    guiShow();
    gOverlays.setProbeFrame(false);
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

void guiDrawBGPlasma()
{
    gPlasma.draw(gTheme->bgColor, gTheme->plasBlendColor, gGuiScreenWidth, gGuiScreenHeight);
}

static void guiReadPads()
{
    // A transition polls without dispatching input. Freeze against the triggering sample so that
    // button cannot replay on the destination, while a different button held through the fade can
    // still register there (see padFreezeEdgeBaseline in pad.c -- deliberately NOT a poll pause).
    padFreezeEdgeBaseline(gScreens.transitioning());

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

void guiMainLoop(void)
{
    gNotifications.reset();
    gNotifications.check(true, true);

    if (gOPLPart[0] != '\0')
        gNotifications.showPartition();

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
            gNotifications.show();

#ifdef RETROACHIEVEMENTS
        // RA action feedback: always drawn while a notice is set (it is the answer to
        // something the user just asked for), independent of gEnableNotifications.
        gNotifications.drawRA();
#endif

        // handle deferred operations
        gOps.drain(guiHandleOp);

        guiEndFrame();

        // if not transiting, handle input
        // done here so we can use renderman if needed
        gScreens.dispatchInput();

        if (gFrameHook)
            gFrameHook();
    }
}

void guiSetFrameHook(gui_callback_t cback)
{
    gFrameHook = cback;
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
