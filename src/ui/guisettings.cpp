/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

// Settings screens (the guiShow*Config family and the Settings shell). Split out of gui.cpp.

#include "include/opl.h"
#include "include/cfgtitle.h"
#include "include/plasma.h"
#include "include/gui.h"
#include "include/cuesupport.h"
#include "include/guiint.h"
#include "include/settingshell.h"
#ifdef RETROACHIEVEMENTS
#include "include/ranet.h" // raNetForgetPeer
#include "include/achievements.h"
#endif
#include "include/diag.h" // gLastDeferredTimedOut -- a bounded wait that expired means the handler never ran
#include "include/renderman.h"
#include "include/saveicon.h" // saveIconReset -- 3D Save Icons switched off
#include "include/menusys.h"
#include "include/fntsys.h"
#include "include/ioman.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/themeviews.h"
#include "include/favsupport.h"
#include "include/host.h" // host::app().initSupport -- the app layer starts the All Games page
#include "include/gamesource.h" // gsOf -- the Favourites tab names itself (All Games) // gFAVStartMode -- Favourites Start Mode row
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
#include "include/grid.h"       // GRID_LAYOUTS -- Grid Layout's presets
#include <limits.h>
#include <stdlib.h>
// Last Played Auto Start
#include <time.h>

constexpr int VMODE_KEEP_HOLD_MS = 2000;     // how long Accept must be held to keep a new video mode
constexpr int VMODE_KEEP_BAR_FULL_MS = 1750; // the hold bar reads full this far in -- a beat BEFORE the keep

// forward declaration (was declared in the header before it became file-local)
static int guiConfirmVideoMode(void);

// forward declaration (was declared in the header before it became file-local)
static void guiShowVcdListConfig(void);

// forward declaration (was declared in the header before it became file-local)
static void guiShowCoverflowConfig(void);

// forward declaration (was declared in the header before it became file-local)
static void guiShowArtworkConfig(void);

// forward declaration (was declared in the header before it became file-local)
static int guiShowMmceConfig(void);

// The eight Settings peer screens reuse the established dialog definitions. Composite screens are
// assembled into scratch buffers at entry so the existing field ids, visibility rules and temporary
// editor buttons remain owned by their original feature code. Values still live in the existing
// globals/config sets; these buffers are only views, never a second configuration store.
SettingsShell gSettingsShell;

static int guiDeviceTypeToIoMode(int deviceType)
{
    // Translates an index into deviceNames into an IO mode index used internally.
    if (deviceType == 0)
        return BDM_MODE;
    else if (deviceType == 1)
        return MMCE_MODE;
    else if (deviceType == 2)
        return HDD_MODE;
    else if (deviceType == 3)
        return ETH_MODE;
    else if (deviceType == 4)
        return APP_MODE;
    else if (deviceType == 5)
        return FAV_MODE;
    else
        return APP_MODE; // safe fallback for unexpected indices: Apps is always present, FAV may be disabled
}

static int guiIoModeToDeviceType(int ioMode)
{
    if (ioMode >= BDM_MODE && ioMode < ETH_MODE)
        return 0;
    if (ioMode == MMCE_MODE)
        return 1;
    if (ioMode == HDD_MODE)
        return 2;
    if (ioMode == ETH_MODE)
        return 3;
    if (ioMode == APP_MODE)
        return 4;
    if (ioMode == FAV_MODE)
        return 5;

    return 0;
}

// IGR Path gets the full-size backing buffer the core path fields use (below). gExitPath and the
// core's ExitPath both hold 255 characters, but a UI_STRING row holds 31: the keyboard stopped at
// 31, so an install path such as mass:/APPS/APP_RIPTOPL/RIPTOPL.ELF could not be typed, and the
// page read the row back into gExitPath on every close, cutting a longer path from the settings
// file to 31 characters. Either way the IGR then failed and fell back to the browser (#731).
static char exitPathEdit[sizeof(gExitPath)];

static void guiExitPathBegin(struct UIItem *ui)
{
    snprintf(exitPathEdit, sizeof(exitPathEdit), "%s", gExitPath);
    diaSetString(ui, CFG_EXITTO, exitPathEdit);
    // Blank still means the built-in fallback, so keep the dim "Default" placeholder.
    diaSetShowDefaultWhenEmpty(ui, CFG_EXITTO, 1);
}

int guiExitPathHandler(char *text, int maxLen)
{
    if (!guiShowKeyboard(exitPathEdit, sizeof(exitPathEdit)))
        return 0;
    snprintf(text, maxLen, "%s", exitPathEdit);
    return 1;
}

// Game Sources page: device start modes, the default device, and the block-device enables
// (inlined; the separate Block Devices sub-dialog is gone).
// What the NIC is ACTUALLY running, as opposed to what the config asks for. The three transports are
// mutually exclusive on the single SMAP NIC -- each of the three loaders refuses to start while either
// of the others is resident (bdmsupport.c's UDPBD gate, ethsupport.c's SMB gate, udpfssupport.c's
// UDPFS gate) -- so at most one of these can answer, and the order below is only for definiteness.
static int guiGetResidentNetProtocol(void)
{
    if (ethGetModulesLoaded())
        return netGetResidentProtocol();
    if (udpfsGetModulesLoaded())
        return NET_PROTO_UDPFS;
    if (bdmIsUDPBDLoaded())
        return (bdmGetLoadedNetProtocol() == NET_BOOT_UDPFS) ? NET_PROTO_UDPFSBD : NET_PROTO_UDPBD;

    return NET_PROTO_OFF;
}

// Does the saved choice disagree with what is live? Nothing is resident -> the next start is free to
// be anything, so there is nothing to restart FOR. This is deliberately a comparison against the
// RESIDENT protocol rather than against the protocol the picker happened to open with: a user who
// switches away and back within one session ends up agreeing with the NIC again, and must not then be
// nagged to restart for a change that is no longer a change.
int guiNetProtocolNeedsRestart(void)
{
    int resident = guiGetResidentNetProtocol();

    if (resident == NET_PROTO_OFF)
        return 0;

    return gNetworkProtocol != resident;
}

// The Protocol row -- SMB(0) / UDPFS(1) / UDPBD(2) / HTTP(3) -- appears on the Network page AND,
// indented under Network Connectivity, on Game Sources. It is one setting shown twice: both rows read
// and write gNetworkProtocol through these helpers, so neither page can disagree with the other.
static const char *guiNetProtocolNames[] = {"SMB", "UDPFS", "UDPBD", "HTTP", NULL}; // protocol names, not translated

// What the Protocol rows show: the live protocol, or while Connectivity is Off (live = OFF) the one the
// user last chose, so switching Off and back on never loses it.
static int guiNetProtocolShown(void)
{
    return (gNetworkProtocol != NET_PROTO_OFF) ? gNetworkProtocol : gNetProtocolPick;
}

static int guiNetProtocolToPicker(int protocol)
{
    if (protocol == NET_PROTO_HTTP)
        return 3;
    if (protocol == NET_PROTO_UDPBD)
        return 2;
    if (protocol == NET_PROTO_UDPFS || protocol == NET_PROTO_UDPFSBD)
        return 1;
    return 0;
}

// Display-only Access for the currently shown protocol. UDPBD's IMG-locked value
// must never become the saved UDPFS Access preference (gUdpfsAccessMode).
static int guiNetProtocolAccess(int protocol)
{
    return (protocol == NET_PROTO_UDPFSBD || protocol == NET_PROTO_UDPBD) ? 1 : 0;
}

// The protocol the rows name. Access (Files 0 / IMG 1) is consulted only for UDPFS.
static int guiNetProtocolFromPicker(int picker, int access)
{
    if (picker == 3)
        return NET_PROTO_HTTP;
    if (picker == 2)
        return NET_PROTO_UDPBD;
    if (picker == 1)
        return access ? NET_PROTO_UDPFSBD : NET_PROTO_UDPFS;
    return NET_PROTO_SMB;
}

// Fold the rows back: the picked protocol is always remembered, and becomes live unless Connectivity is
// Off -- then gNetworkProtocol is OFF (the stack does not run at all) and the pick waits for it.
static void guiNetProtocolStore(int picker, int access)
{
    if (picker == 1)
        gUdpfsAccessMode = access ? 1 : 0;
    gNetProtocolPick = guiNetProtocolFromPicker(picker, access);
    gNetworkProtocol = (gNetStartMode == START_MODE_DISABLED) ? NET_PROTO_OFF : gNetProtocolPick;
}

// The protocol the user picked differs from the stack already running. Yes saves every setting and
// restarts; No keeps the choice pending, and Save Settings offers the restart again.
static void guiNetProtocolOfferRestart(void)
{
    if (guiMsgBox(_l(_STR_NETBOOT_RESTART_NOW), 1, NULL))
        menuSaveSettingsAndRestart();
}

// After either page set gNetworkProtocol: re-derive the legacy shadows downstream consumers read, then
// give the same notices in the same order from both pages -- the UDP static-IP note, then EITHER the
// restart offer (another stack is already resident) OR what to expect from the new page.
static void guiNetProtocolApplied(int netProtocolWas)
{
    gEnableUDPBD = (gNetworkProtocol == NET_PROTO_UDPBD || gNetworkProtocol == NET_PROTO_UDPFSBD);
    gNetBootProtocol = (gNetworkProtocol == NET_PROTO_UDPFSBD) ? NET_BOOT_UDPFS : NET_BOOT_UDPBD;
    // SMB's start mode IS the network start row (Auto = boot connect, Manual = on-entry); every
    // non-SMB protocol forces the SMB/ETH stack off so only one transport claims the NIC.
    gETHStartMode = (gNetworkProtocol == NET_PROTO_SMB) ? gNetStartMode : START_MODE_DISABLED;

    if (gNetworkProtocol == netProtocolWas)
        return;

    // UDP transports always use the saved static PS2 IP fields. If the user's preserved SMB/HTTP
    // preference is DHCP, explain that the UDP transport is using Static by design.
    int nowUdp = (gNetworkProtocol == NET_PROTO_UDPFS || gNetworkProtocol == NET_PROTO_UDPFSBD || gNetworkProtocol == NET_PROTO_UDPBD);
    int wasUdp = (netProtocolWas == NET_PROTO_UDPFS || netProtocolWas == NET_PROTO_UDPFSBD || netProtocolWas == NET_PROTO_UDPBD);
    if (nowUdp && !wasUdp && ps2_ip_use_dhcp)
        guiMsgBox(_l(_STR_UDPBD_NEEDS_STATIC_IP), 0, NULL);

    // Each network transport loads its IOP module chain once per boot (the load latch is not cleared
    // live). If a stack is already up and the protocol changed away from the one actually running,
    // the new protocol's page cannot connect until a restart -- so the "open its page" hints below
    // would send the user to a page that does nothing. Offer the restart instead, saving first so the
    // choice survives it.
    if (guiNetProtocolNeedsRestart()) {
        guiNetProtocolOfferRestart();
        return;
    }

    // "Nothing happens" guard: enabling a network protocol gives NO feedback -- the UDPFS tab joins the
    // ring silently (Manual start waits for a Confirm-press inside it), and the block transports show a
    // tab only once the PC server answers. Tell the user what to expect + which PC server to run.
    if (gNetworkProtocol == NET_PROTO_HTTP)
        guiMsgBox(_l(_STR_HTTP_TAB_HINT), 0, NULL);
    else if (gNetworkProtocol == NET_PROTO_UDPFS)
        guiMsgBox(_l(_STR_NET_UDPFS_TAB_HINT), 0, NULL);
    else if (gNetworkProtocol == NET_PROTO_UDPFSBD || gNetworkProtocol == NET_PROTO_UDPBD)
        guiMsgBox(_l(_STR_NET_UDPBD_TAB_HINT), 0, NULL);
}

// Game Sources: the indented Protocol row greys while Connectivity is Off.
static void guiSourcesNetRowsUpdate(struct UIItem *ui)
{
    int startMode = START_MODE_DISABLED;

    diaGetInt(ui, CFG_NETSTART, &startMode);
    diaSetEnabled(ui, CFG_NETPROTOCOL, startMode != START_MODE_DISABLED);
}

static void guiSourcesNetRowsBegin(struct UIItem *ui, const char **deviceModes)
{
    diaSetEnum(ui, CFG_NETSTART, deviceModes);
    diaSetInt(ui, CFG_NETSTART, gNetStartMode);
    diaSetEnum(ui, CFG_NETPROTOCOL, guiNetProtocolNames);
    diaSetInt(ui, CFG_NETPROTOCOL, guiNetProtocolToPicker(guiNetProtocolShown()));
    guiSourcesNetRowsUpdate(ui); // the first frame renders before the updater runs
}

// Game Sources has no Access row. Restore UDPFS's own preference, not UDPBD's IMG.
static void guiSourcesNetRowsRead(struct UIItem *ui)
{
    int netProtocolWas = gNetworkProtocol;
    int access = gUdpfsAccessMode;
    int picker = guiNetProtocolToPicker(guiNetProtocolShown());

    diaGetInt(ui, CFG_NETSTART, &gNetStartMode);
    diaGetInt(ui, CFG_NETPROTOCOL, &picker);
    guiNetProtocolStore(picker, access);
    guiNetProtocolApplied(netProtocolWas);
}

static void guiRefreshCfgTitles(void);

// All Games switched on or off (Game Sources). The device pages now read both lists or go back to
// their own view, the Favourites tab changes name and rows, and with it on the user lands there.
static void guiApplyAllGamesChange(void)
{
    // All Games lives on the Favourites page, so start, show or hide that page for the new setting.
    // The Game Sources apply did this already through applyConfig(..., 0); the Interface (Library)
    // apply runs applyConfig(..., 1), which skips every device, so without this All Games had no page
    // to open when Favourites was Off, and turning it off left a started Favourites page on screen.
    host::app().initSupport(favGetObject(0), FAV_MODE, 0);

    item_list_t *favList = favGetObject(0);
    opl_io_module_t *fav = favList != NULL ? static_cast<opl_io_module_t *>(favList->owner) : NULL;

    if (fav != NULL && fav->support != NULL) {
        fav->menuItem.text_id = gsOf(fav->support)->textId(fav->support);
        if (gAllGames) {
            struct gui_update_t *sel = guiOpCreate(GUI_OP_SELECT_MENU);
            if (sel != NULL) {
                sel->menu.menu = &fav->menuItem;
                guiDeferUpdate(sel);
            }
        }
    }
    guiRefreshCfgTitles(); // rebuilds every game list, Favourites last
}

// MMCE page (settings-layout restructure, was MMCE Settings): SD2PSX / MemCard PRO2 basics plus
// communication tuning and the library path. The legacy child definitions remain available, but
// this entry composes their fields inline so the user does not need another navigation layer.
static int guiShowMmceConfig(void)
{
    const struct UIItem *parts[] = {diaMmceConfig, diaMmceCommConfig, diaMmcePathConfig};
    const int skipIDs[] = {MMCE_COMM_BUTTON, MMCE_PATH_BUTTON};
    static const char *deviceSlots[] = {"0", "1", NULL, NULL};
    static const char *deviceIGRSlots[] = {NULL, "0", "1", NULL, NULL};
    static const char *deviceAckWaitCycles[] = {"0", "1", "2", "3", "4", "5", NULL};
    static const char *deviceOnOff[] = {NULL, NULL, NULL};
    struct UIItem *previousActiveDialog = gSettingsShell.activeDialog();
    struct UIItem *ui;

    // diaSetEnum stores the array pointer rather than copying it. Keep the MMCE options alive for
    // the lifetime of the dedicated child dialog, including any parent re-entry after Circle.
    deviceSlots[2] = _l(_STR_AUTO);
    // The IGR slot and ON/OFF choices were fixed English text no translation could change
    // (belomaxorka / YAOPL 999bb6d3). The stored values (mmceigr_slot 0-3, 0/1) are unchanged.
    deviceIGRSlots[0] = _l(_STR_OFF);
    deviceIGRSlots[3] = _l(_STR_BOTH);
    deviceOnOff[0] = _l(_STR_OFF);
    deviceOnOff[1] = _l(_STR_ON);
    ui = gSettingsShell.composeInto(gSettingsShell.mmceDialog(), parts, 3, skipIDs, 2, -1, 1);

    if (ui == NULL)
        return -1;

    diaSetEnum(ui, CFG_MMCESLOT, deviceSlots);
    diaSetInt(ui, CFG_MMCESLOT, gMMCESlot);
    diaSetEnum(ui, CFG_MMCEIGRSLOT, deviceIGRSlots);
    diaSetInt(ui, CFG_MMCEIGRSLOT, gMMCEIGRSlot);
    diaSetInt(ui, CFG_MMCEGAMEID, gMMCEEnableGameID);
    diaSetEnum(ui, CFG_MMCE_WAIT_CYCLES, deviceAckWaitCycles);
    diaSetInt(ui, CFG_MMCE_WAIT_CYCLES, gMMCEAckWaitCycles);
    diaSetEnum(ui, CFG_MMCE_USE_ALARMS, deviceOnOff);
    diaSetInt(ui, CFG_MMCE_USE_ALARMS, gMMCEUseAlarms);
    diaSetString(ui, CFG_MMCEPREFIX, gMMCEPrefix);

    int ret = diaExecuteDialog(ui, -1, 1, NULL);
    if (ret == UIID_BTN_OK) {
        diaGetInt(ui, CFG_MMCESLOT, &gMMCESlot);
        diaGetInt(ui, CFG_MMCEIGRSLOT, &gMMCEIGRSlot);
        diaGetInt(ui, CFG_MMCEGAMEID, &gMMCEEnableGameID);
        diaGetInt(ui, CFG_MMCE_WAIT_CYCLES, &gMMCEAckWaitCycles);
        diaGetInt(ui, CFG_MMCE_USE_ALARMS, &gMMCEUseAlarms);
        diaGetString(ui, CFG_MMCEPREFIX, gMMCEPrefix, sizeof(gMMCEPrefix));
        // The Settings parent commits the complete Game Sources page after this child returns.
        // Legacy callers still own their own apply path.
        if (!gSettingsShell.shellActive()) {
            applyConfig(-1, -1, 0);
            menuReinitMainMenu();
        }
    }

    gSettingsShell.setActiveDialog(previousActiveDialog);
    return ret;
}

// Deep-copy a NULL-terminated name list for handing to diaSetEnum. The CALLER must hold guiLock
// across BOTH the getter fetch (thmGetGuiList/lngGetGuiList) and this copy: the source lists live
// on the heap and are freed+rebuilt under guiLock on the IO worker (thmRebuildGuiNames/
// lngRebuildLangNames; thmReinit also frees the theme NAME strings on device removal), and taking
// the lock only inside this function left a preemption window between fetching the pointer and
// locking (Gemini review of #165). Returns NULL on OOM (caller falls back to the live list).
const char **guiCopyNameList(const char **src)
{
    int n = 0, i;
    const char **copy;

    if (src == NULL)
        return NULL;

    while (src[n] != NULL)
        n++;
    copy = (const char **)malloc((n + 1) * sizeof(char *));
    if (copy != NULL) {
        for (i = 0; i < n; i++) {
            char *dup = (char *)malloc(strlen(src[i]) + 1);
            if (dup == NULL) {
                while (--i >= 0)
                    free((void *)copy[i]);
                free((void *)copy);
                copy = NULL;
                break;
            }
            strcpy(dup, src[i]);
            copy[i] = dup;
        }
        if (copy != NULL)
            copy[n] = NULL;
    }
    return copy;
}

void guiFreeNameList(const char **list)
{
    int i;

    if (list == NULL)
        return;
    for (i = 0; list[i] != NULL; i++)
        free((void *)list[i]);
    free((void *)list);
}

// One persisted four-mode PS2/PS1 picker is presented on both Interface and PS Emulation. Keeping its
// enum setup/readback here prevents two visually identical rows from acquiring different semantics.
// Saved values keep their historical ABI (Both=0, PS2=1, PS1=2, Mixed=3), while the picker uses
// the user-facing order requested by the display model.
static const char *guiGameViewNames[] = {"Both (L3)", "Mixed", "PS2", "PS1", NULL};
static const char *guiAppsViewNames[] = {"Mixed", "Apps / PS1ELF (L3)", NULL};

static int guiGameViewToPicker(int view)
{
    return view == GAME_VIEW_MIXED ? 1 : view == GAME_VIEW_ISO ? 2 :
                                     view == GAME_VIEW_VCD     ? 3 :
                                                                 0;
}

static int guiPickerToGameView(int picker)
{
    return picker == 1 ? GAME_VIEW_MIXED : picker == 2 ? GAME_VIEW_ISO :
                                       picker == 3     ? GAME_VIEW_VCD :
                                                         GAME_VIEW_BOTH;
}

static void guiSetGameViewPicker(struct UIItem *ui)
{
    diaSetEnum(ui, UICFG_GAMEVIEW, guiGameViewNames);
    diaSetInt(ui, UICFG_GAMEVIEW, guiGameViewToPicker(gDefaultGameView));
}

static int guiReadGameViewPicker(struct UIItem *ui)
{
    int previousGameView = gDefaultGameView;

    int picker;
    diaGetInt(ui, UICFG_GAMEVIEW, &picker);
    gDefaultGameView = guiPickerToGameView(picker);
    if (gDefaultGameView == previousGameView)
        return 0;

    libViewMarkAllDirty();
    return 1;
}

static void guiSetAppsViewPicker(struct UIItem *ui)
{
    diaSetEnum(ui, UICFG_APPSVIEW, guiAppsViewNames);
    diaSetInt(ui, UICFG_APPSVIEW, gAppsDisplay);
}

static int guiReadAppsViewPicker(struct UIItem *ui)
{
    int previous = gAppsDisplay;

    diaGetInt(ui, UICFG_APPSVIEW, &gAppsDisplay);
    if (gAppsDisplay < APPS_DISPLAY_MIXED || gAppsDisplay > APPS_DISPLAY_SPLIT)
        gAppsDisplay = APPS_DISPLAY_MIXED;
    if (gAppsDisplay == previous)
        return 0;

    libViewMarkDirty(APP_MODE);
    return 1;
}

// Theme View picker: THM_VIEW_PICK_* (include/themeviews.h): the theme's default view, then List,
// Coverflow (3), Coverflow (5), PSBBN Flow and Grid. The two Coverflow entries are one theme view with 3 or 5
// covers, so they replace the old Cover Count row. A theme that does not offer the picked view shows its
// default one; the choice is kept for the themes that do.
static int sThemeViewPickerShown; // the value guiSetThemeViewPicker put on the picker

static void guiSetThemeViewPicker(struct UIItem *ui)
{
    static const char *names[THM_VIEW_PICK_COUNT + 1];
    names[THM_VIEW_PICK_DEFAULT] = _l(_STR_DEFAULT);
    names[THM_VIEW_PICK_LIST] = "List";
    names[THM_VIEW_PICK_COVERFLOW3] = "Coverflow (3)";
    names[THM_VIEW_PICK_COVERFLOW5] = "Coverflow (5)";
    names[THM_VIEW_PICK_PSBBN] = "PSBBN Flow";
    names[THM_VIEW_PICK_GRID] = "Grid";
    names[THM_VIEW_PICK_COUNT] = NULL;
    diaSetEnum(ui, UICFG_THEME_VIEW, names);
    sThemeViewPickerShown = thmViewPickFor(gThemeViewRequest, gCoverflowCount);
    diaSetInt(ui, UICFG_THEME_VIEW, sThemeViewPickerShown);
}

// The view request the picker asks for. The caller hands it to thmSetView after applyConfig without
// storing it first, so an abandoned reload keeps the request of the view still on screen. An untouched
// picker keeps the current request. A Coverflow pick sets the cover count here: it is read at draw time.
static int guiReadThemeViewPicker(struct UIItem *ui)
{
    int picker = THM_VIEW_PICK_DEFAULT;

    diaGetInt(ui, UICFG_THEME_VIEW, &picker);
    if (picker == sThemeViewPickerShown)
        return gThemeViewRequest;
    return thmViewPickRequest(picker, &gCoverflowCount);
}

// Library: Pages (a tab per device) or All Games (one list). Sits under Theme View because it changes how
// the main page is laid out; it is the same all_games setting as the Game Sources row.
static void guiSetLibraryPicker(struct UIItem *ui)
{
    static const char *names[3];
    names[0] = _l(_STR_LIBRARY_PAGES);
    names[1] = _l(_STR_ALL_GAMES_LIST);
    names[2] = NULL;
    diaSetEnum(ui, UICFG_LIBRARY, names);
    diaSetInt(ui, UICFG_LIBRARY, gAllGames ? 1 : 0);
}

// 1 when the picker changed All Games; the caller applies it after applyConfig.
static int guiReadLibraryPicker(struct UIItem *ui)
{
    int picker = gAllGames ? 1 : 0;

    diaGetInt(ui, UICFG_LIBRARY, &picker);
    picker = picker ? 1 : 0;
    if (picker == (gAllGames ? 1 : 0))
        return 0;
    gAllGames = picker;
    return 1;
}

static void guiRefreshGameViews(void)
{
    // libViewMarkAllDirty is intentionally side-effect-free; queue the actual source rebuilds and
    // rebuild Favorites after them so its resolver sees the newly selected source views.
    oplQueueLibraryDeviceUpdates();
    loadFavourites();
}

// Read all title caches only after source lists are marked dirty. HTTP must precede Favorites too.
static void guiRefreshCfgTitles(void)
{
    libViewMarkAllDirty();
    oplQueueLibraryDeviceUpdates();
    item_list_t *http = httpGetObject(0);
    if (http != NULL && http->enabled)
        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &http->mode);
    loadFavourites();
}

static void guiRefreshAppsView(void)
{
    item_list_t *apps = appGetObject(0);
    if (apps != NULL && apps->enabled)
        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &apps->mode);
}

static int httpTestBusy;
static int httpTestIp[4], httpTestPort;
static char httpTestBase[HTTP_BASE_PATH_MAX], httpTestMessage[128];

// UDPFS/UDPBD use a ministack with no DHCP client, but SMB/HTTP can still use DHCP.
// Keep the user's full-stack preference separate from the transport-effective UI value so
// selecting a UDP protocol can force Static without permanently clobbering that preference.
static int netConfigDhcpPreference;
static int netConfigLastProtocol = -1;
static int netConfigUdpfsAccess; // pending UDPFS Files/IMG preference, not UDPBD's forced IMG

static void httpTestWorker(void)
{
    httpTestServer(httpTestIp, httpTestPort, httpTestBase, httpTestMessage, sizeof(httpTestMessage));
    httpTestBusy = 0;
}

static int netConfigUpdater(int modified)
{
    int showAdvancedOptions, isNetBIOS, isDHCPEnabled, netProto, isSMB, isHTTP, isUdp, i;

    if (modified) {
        diaGetInt(diaNetConfig, NETCFG_SHOW_ADVANCED_OPTS, &showAdvancedOptions);

        diaGetInt(diaNetConfig, NETCFG_PS2_IP_ADDR_TYPE, &isDHCPEnabled);
        diaGetInt(diaNetConfig, NETCFG_SHARE_ADDR_TYPE, &isNetBIOS);
        diaGetInt(diaNetConfig, CFG_NETPROTOCOL, &netProto);
        isSMB = netProto == 0;
        isHTTP = netProto == 3;
        isUdp = netProto == 1 || netProto == 2;

        // Preserve UDPFS Access across in-dialog switches. A forced UDPBD IMG
        // never overwrites a user-selected UDPFS Files (or explicit IMG).
        if (netConfigLastProtocol == 1 && netProto != 1)
            diaGetInt(diaNetConfig, CFG_UDPFSMODE, &netConfigUdpfsAccess);
        if (netProto == 1 && netConfigLastProtocol != 1)
            diaSetInt(diaNetConfig, CFG_UDPFSMODE, netConfigUdpfsAccess);

        // UDPFS/UDPBD always bind the configured static PS2 IP; their ministack has no DHCP client.
        // Force the effective row to Static while either protocol is selected, but remember the
        // user's DHCP choice so switching back to SMB/HTTP restores it instead of silently changing
        // their normal network preference.
        if (isUdp) {
            if (netConfigLastProtocol != 1 && netConfigLastProtocol != 2)
                netConfigDhcpPreference = isDHCPEnabled;
            diaSetInt(diaNetConfig, NETCFG_PS2_IP_ADDR_TYPE, 0);
            diaSetEnabled(diaNetConfig, NETCFG_PS2_IP_ADDR_TYPE, 0);
            isDHCPEnabled = 0;
        } else {
            if (netConfigLastProtocol == 1 || netConfigLastProtocol == 2) {
                diaSetInt(diaNetConfig, NETCFG_PS2_IP_ADDR_TYPE, netConfigDhcpPreference);
                isDHCPEnabled = netConfigDhcpPreference;
            } else {
                netConfigDhcpPreference = isDHCPEnabled;
            }
            diaSetEnabled(diaNetConfig, NETCFG_PS2_IP_ADDR_TYPE, 1);
        }
        netConfigLastProtocol = netProto;
#ifdef RETROACHIEVEMENTS
        // Cards are a Caduceus feature and need telemetry; the saved choice survives a switch away.
        int raMode, raTelemetry;
        diaGetInt(diaNetConfig, NETCFG_RA_MODE, &raMode);
        diaGetInt(diaNetConfig, NETCFG_RA_TELEMETRY, &raTelemetry);
        diaSetEnabled(diaNetConfig, NETCFG_RA_EXPERIMENTAL_CARDS, raMode == RA_MODE_CADUCEUS && raTelemetry);
#endif

        diaSetVisible(diaNetConfig, NETCFG_SHARE_NB_ADDR, isNetBIOS);

        // SMB server fields belong to OPL's SMB consumer. UDPFS/UDPBD are the Neutrino-facing
        // transports, so do not present SMB-only options as if they applied to those protocols.
        diaSetVisible(diaNetConfig, NETCFG_LBL_SMB_SERVER, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_LBL_SHARE_ADDR_TYPE, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_LBL_SHARE_ADDRESS, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_LBL_SHARE_PORT, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_LBL_SHARE_NAME, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_LBL_SHARE_USER, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_LBL_SHARE_PASSWORD, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_LBL_SMBDIALECT, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_SHARE_ADDR_TYPE, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_SHARE_NB_ADDR, isSMB && isNetBIOS);

        for (i = 0; i < 4; i++) {
            diaSetVisible(diaNetConfig, NETCFG_SHARE_IP_ADDR_0 + i, isSMB && !isNetBIOS);

            diaSetEnabled(diaNetConfig, NETCFG_PS2_IP_ADDR_0 + i, !isDHCPEnabled);
            diaSetEnabled(diaNetConfig, NETCFG_PS2_NETMASK_0 + i, !isDHCPEnabled);
            diaSetEnabled(diaNetConfig, NETCFG_PS2_GATEWAY_0 + i, !isDHCPEnabled);
            diaSetEnabled(diaNetConfig, NETCFG_PS2_DNS_0 + i, !isDHCPEnabled);
        }

        for (i = 0; i < 3; i++)
            diaSetVisible(diaNetConfig, NETCFG_SHARE_IP_ADDR_DOT_0 + i, isSMB && !isNetBIOS);

        diaSetEnabled(diaNetConfig, NETCFG_SHARE_PORT, isSMB && showAdvancedOptions);
        diaSetEnabled(diaNetConfig, NETCFG_ETHOPMODE, showAdvancedOptions);
        diaSetVisible(diaNetConfig, NETCFG_SHARE_PORT, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_SHARE_NAME, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_SHARE_USERNAME, isSMB);
        diaSetVisible(diaNetConfig, NETCFG_SHARE_PASSWORD, isSMB);

        // HTTP endpoint, shown only for HTTP. Its fields are separate from the SMB ones, so a
        // protocol switch hides them rather than repurposing another protocol's settings.
        diaSetVisible(diaNetConfig, NETCFG_LBL_HTTP_SERVER, isHTTP);
        diaSetVisible(diaNetConfig, NETCFG_LBL_HTTP_PORT, isHTTP);
        diaSetVisible(diaNetConfig, NETCFG_LBL_HTTP_BASE, isHTTP);
        diaSetVisible(diaNetConfig, NETCFG_HTTP_PORT, isHTTP);
        diaSetVisible(diaNetConfig, NETCFG_HTTP_BASE, isHTTP);
        diaSetVisible(diaNetConfig, NETCFG_HTTP_TEST, isHTTP);
        diaSetVisible(diaNetConfig, NETCFG_LBL_HTTP_TEST, isHTTP);
        for (i = 0; i < 3; i++)
            diaSetVisible(diaNetConfig, NETCFG_HTTP_DOT_0 + i, isHTTP);
        for (i = 0; i < 4; i++)
            diaSetVisible(diaNetConfig, NETCFG_HTTP_IP_0 + i, isHTTP);

        // Protocol: lock Access to Files for SMB and to IMG for UDPBD (only UDPFS offers the free
        // toggle) -- snap the value so a stale IMG left over from UDPFS can never mis-derive to
        // UDPFSBD under SMB, AND grey the control so the lock is visible.
        // NOTE(rebuild): the SMB Version row stays greyed at SMBv1 until item 4 lands (the fork
        // enables it while SMB is the selected protocol).
        diaSetEnabled(diaNetConfig, CFG_SMBDIALECT, 0);
        diaSetVisible(diaNetConfig, CFG_SMBDIALECT, isSMB);
        if (netProto == 0 || netProto == 3) { // SMB and HTTP -> Files, locked
            diaSetInt(diaNetConfig, CFG_UDPFSMODE, 0);
            diaSetEnabled(diaNetConfig, CFG_UDPFSMODE, 0);
        } else if (netProto == 2) { // UDPBD -> IMG, locked
            diaSetInt(diaNetConfig, CFG_UDPFSMODE, 1);
            diaSetEnabled(diaNetConfig, CFG_UDPFSMODE, 0);
        } else { // UDPFS -> Files/IMG free
            diaSetEnabled(diaNetConfig, CFG_UDPFSMODE, 1);
        }
    }

    return 0;
}

// Everything a live SMB session depends on. The Network page snapshots it before the dialog so that
// an exit which changed none of it -- Back, or L1/R1 past the page -- does not drop and redo the
// session; only OK/Reconnect or a real change does. Strings are copied as strings into a zeroed
// struct, so bytes past a terminator can never read as a change.
typedef struct
{
    int dhcp, ps2Ip[4], netmask[4], gateway[4], dns[4], pcIp[4], pcPort, pcNetBIOS, opMode, protocol;
    char pcNBAddress[17], shareName[32], userName[32], password[32];
} gui_smb_link_t;

static void guiSmbLinkCapture(gui_smb_link_t *link)
{
    memset(link, 0, sizeof(*link));
    link->dhcp = ps2_ip_use_dhcp;
    memcpy(link->ps2Ip, ps2_ip, sizeof(link->ps2Ip));
    memcpy(link->netmask, ps2_netmask, sizeof(link->netmask));
    memcpy(link->gateway, ps2_gateway, sizeof(link->gateway));
    memcpy(link->dns, ps2_dns, sizeof(link->dns));
    memcpy(link->pcIp, pc_ip, sizeof(link->pcIp));
    link->pcPort = gPCPort;
    link->pcNetBIOS = gPCShareAddressIsNetBIOS;
    link->opMode = gETHOpMode;
    link->protocol = gNetworkProtocol;
    snprintf(link->pcNBAddress, sizeof(link->pcNBAddress), "%s", gPCShareNBAddress);
    snprintf(link->shareName, sizeof(link->shareName), "%s", gPCShareName);
    snprintf(link->userName, sizeof(link->userName), "%s", gPCUserName);
    snprintf(link->password, sizeof(link->password), "%s", gPCPassword);
}

static int guiSmbLinkChanged(const gui_smb_link_t *before)
{
    gui_smb_link_t now;

    guiSmbLinkCapture(&now);
    return memcmp(before, &now, sizeof(now)) != 0;
}

int guiShowNetConfig(void)
{
#ifdef RETROACHIEVEMENTS
    if (achievementsBusy()) { // a browser page still owns the network and the share
        guiShowRANotice(_l(_STR_RA_CHECK_RUNNING), NULL);
        return gSettingsShell.pageResult(UIID_BTN_CANCEL);
    }
#endif
    size_t i;
    // What the live SMB session was built from, before this page changes anything.
    gui_smb_link_t smbBefore;
    guiSmbLinkCapture(&smbBefore);
    const char *ethOpModes[] = {_l(_STR_AUTO), _l(_STR_ETH_100MFDX), _l(_STR_ETH_100MHDX), _l(_STR_ETH_10MFDX), _l(_STR_ETH_10MHDX), NULL};
    const char *addrConfModes[] = {_l(_STR_ADDR_TYPE_IP), _l(_STR_ADDR_TYPE_NETBIOS), NULL};
    const char *ipAddrConfModes[] = {_l(_STR_IP_ADDRESS_TYPE_STATIC), _l(_STR_IP_ADDRESS_TYPE_DHCP), NULL};
    const char *udpfsModes[] = {"Files", "IMG", NULL}; // Access: Files=udpfs_ioman filesystem, IMG=udpfs_bd block
    // NOTE(rebuild): SMBv1 only until item 4 re-adds the SMB2 dialect; the row shows the active
    // dialect and stays greyed (netConfigUpdater keeps it disabled).
    const char *smbDialects[] = {"SMBv1", NULL};
    diaSetEnum(diaNetConfig, NETCFG_PS2_IP_ADDR_TYPE, ipAddrConfModes);
    diaSetEnum(diaNetConfig, NETCFG_SHARE_ADDR_TYPE, addrConfModes);
    diaSetEnum(diaNetConfig, NETCFG_ETHOPMODE, ethOpModes);
    diaSetEnum(diaNetConfig, CFG_NETPROTOCOL, guiNetProtocolNames); // UDPBD = SUDPBDv2 server
    diaSetEnum(diaNetConfig, CFG_UDPFSMODE, udpfsModes);
    diaSetEnum(diaNetConfig, CFG_SMBDIALECT, smbDialects);

    // upload current values
    // Open the Network Config with advanced options ON so SMB Port + ETH op-mode are immediately
    // editable. Forcing 0 here contradicted the row's own def=1 in dialogs.c AND overwrote it:
    // diaSetInt writes both `def` and `current`, so even diaResetValue restored the 0. It matters
    // because this fork defaults the SMB port to 1111 -- anyone pointing at a normal 445 server hit
    // a greyed field first.
    diaSetInt(diaNetConfig, NETCFG_SHOW_ADVANCED_OPTS, 1);
    diaSetEnabled(diaNetConfig, NETCFG_ETHOPMODE, 1);
    diaSetEnabled(diaNetConfig, NETCFG_SHARE_PORT, 1);

    diaSetInt(diaNetConfig, NETCFG_PS2_IP_ADDR_TYPE, ps2_ip_use_dhcp);
    diaSetInt(diaNetConfig, NETCFG_SHARE_ADDR_TYPE, gPCShareAddressIsNetBIOS);
    diaSetVisible(diaNetConfig, NETCFG_SHARE_NB_ADDR, gPCShareAddressIsNetBIOS);
    diaSetInt(diaNetConfig, NETCFG_SHARE_NB_ADDR, gPCShareAddressIsNetBIOS);
    diaSetString(diaNetConfig, NETCFG_SHARE_NB_ADDR, gPCShareNBAddress);

    for (i = 0; i < 4; ++i) {
        diaSetEnabled(diaNetConfig, NETCFG_PS2_IP_ADDR_0 + i, !ps2_ip_use_dhcp);
        diaSetEnabled(diaNetConfig, NETCFG_PS2_NETMASK_0 + i, !ps2_ip_use_dhcp);
        diaSetEnabled(diaNetConfig, NETCFG_PS2_GATEWAY_0 + i, !ps2_ip_use_dhcp);
        diaSetEnabled(diaNetConfig, NETCFG_PS2_DNS_0 + i, !ps2_ip_use_dhcp);

        diaSetVisible(diaNetConfig, NETCFG_SHARE_IP_ADDR_0 + i, !gPCShareAddressIsNetBIOS);
        diaSetInt(diaNetConfig, NETCFG_PS2_IP_ADDR_0 + i, ps2_ip[i]);
        diaSetInt(diaNetConfig, NETCFG_PS2_NETMASK_0 + i, ps2_netmask[i]);
        diaSetInt(diaNetConfig, NETCFG_PS2_GATEWAY_0 + i, ps2_gateway[i]);
        diaSetInt(diaNetConfig, NETCFG_PS2_DNS_0 + i, ps2_dns[i]);
        diaSetInt(diaNetConfig, NETCFG_SHARE_IP_ADDR_0 + i, pc_ip[i]);
    }

    for (i = 0; i < 3; ++i)
        diaSetVisible(diaNetConfig, NETCFG_SHARE_IP_ADDR_DOT_0 + i, !gPCShareAddressIsNetBIOS);

    diaSetInt(diaNetConfig, NETCFG_SHARE_PORT, gPCPort);
    diaSetString(diaNetConfig, NETCFG_SHARE_NAME, gPCShareName);
    diaSetString(diaNetConfig, NETCFG_SHARE_USERNAME, gPCUserName);
    diaSetString(diaNetConfig, NETCFG_SHARE_PASSWORD, gPCPassword);
    diaSetInt(diaNetConfig, NETCFG_ETHOPMODE, gETHOpMode);
#ifdef RETROACHIEVEMENTS
    static const char *raModes[] = {"Xerabora", "Caduceus", NULL};
    diaSetEnum(diaNetConfig, NETCFG_RA_MODE, raModes);
    diaSetInt(diaNetConfig, NETCFG_RA_MODE, gRAMode);
    for (i = 0; i < 4; ++i)
        diaSetInt(diaNetConfig, NETCFG_RA_HOST_IP_0 + i, gRAHostIp[i]);
    diaSetInt(diaNetConfig, NETCFG_RA_TELEMETRY, gRATelemetry);
    diaSetInt(diaNetConfig, NETCFG_RA_BADGES, gRABadges);
    const char *raBadgePos[] = {_l(_STR_RA_BADGE_PREFIX), _l(_STR_RA_BADGE_SUFFIX), NULL};
    diaSetEnum(diaNetConfig, NETCFG_RA_BADGE_SUFFIX, raBadgePos);
    diaSetInt(diaNetConfig, NETCFG_RA_BADGE_SUFFIX, gRABadgeSuffix);
    diaSetInt(diaNetConfig, NETCFG_RA_EXPERIMENTAL_CARDS, gRAExperimentalCards);
#endif

    // Protocol rows, seeded from the authoritative gNetworkProtocol.
    //   Protocol: SMB(0)/UDPFS(1)/UDPBD(2)  -- OFF and UDPFSBD both collapse to their protocol
    //   Access:   Files(0)/IMG(1); IMG only distinct for UDPFS (-> UDPFSBD backend)
    // A NET_PROTO_OFF backend has no protocol memory, so seed the protocol row to SMB -- the common
    // default a user reaches when they switch the Game Sources Start row from Off to Manual/Auto.
    int netProtoVal = guiNetProtocolToPicker(guiNetProtocolShown()); // while Off: the remembered pick
    // IMG for the udpfs block backend AND UDPBD (IMG-locked), so the seed already matches the lock.
    int netAccessVal = (netProtoVal == 1) ? gUdpfsAccessMode : guiNetProtocolAccess(guiNetProtocolShown());
    for (i = 0; i < 4; ++i)
        diaSetInt(diaNetConfig, NETCFG_HTTP_IP_0 + i, gHttpServerIp[i]);
    diaSetInt(diaNetConfig, NETCFG_HTTP_PORT, gHttpPort);
    diaSetString(diaNetConfig, NETCFG_HTTP_BASE, gHttpBasePath);
    diaSetInt(diaNetConfig, CFG_NETPROTOCOL, netProtoVal);
    diaSetInt(diaNetConfig, CFG_UDPFSMODE, netAccessVal);
    diaSetInt(diaNetConfig, CFG_SMBDIALECT, 0); // SMBv1 -- the only dialect until item 4
    // Seed the initial grey/lock: diaExecuteDialog renders the FIRST frame before it calls
    // netConfigUpdater, so without this the first frame flashes every row enabled.
    diaSetEnabled(diaNetConfig, CFG_UDPFSMODE, netProtoVal == 1);
    diaSetEnabled(diaNetConfig, CFG_SMBDIALECT, 0); // NOTE(rebuild): greyed until item 4
    netConfigDhcpPreference = ps2_ip_use_dhcp;
    netConfigUdpfsAccess = gUdpfsAccessMode;
    netConfigLastProtocol = -1;
    netConfigUpdater(1);

    // Update the spacer item between the OK and reconnect buttons (See dialogs.c).
    if (gNetworkStartup == 0) {
        diaSetLabel(diaNetConfig, NETCFG_OK, _l(_STR_OK));
        diaSetVisible(diaNetConfig, NETCFG_RECONNECT, 1);
    } else if (gNetworkStartup >= ERROR_ETH_SMB_CONN) {
        diaSetLabel(diaNetConfig, NETCFG_OK, _l(_STR_RECONNECT));
        diaSetVisible(diaNetConfig, NETCFG_RECONNECT, 0);
    } else {
        diaSetLabel(diaNetConfig, NETCFG_OK, _l(_STR_OK));
        diaSetVisible(diaNetConfig, NETCFG_RECONNECT, 0);
    }

    gSettingsShell.beginDialog(diaNetConfig);
    int result;
reshow_network:
    result = diaExecuteDialog(diaNetConfig, -1, 1, &netConfigUpdater);
    if (result == NETCFG_HTTP_TEST) {
        for (i = 0; i < 4; ++i)
            diaGetInt(diaNetConfig, NETCFG_HTTP_IP_0 + i, &httpTestIp[i]);
        diaGetInt(diaNetConfig, NETCFG_HTTP_PORT, &httpTestPort);
        diaGetString(diaNetConfig, NETCFG_HTTP_BASE, httpTestBase, sizeof(httpTestBase));
        httpNormalizeBasePath(httpTestBase, sizeof(httpTestBase));
        snprintf(httpTestMessage, sizeof(httpTestMessage), "%s", _l(_STR_HTTP_ERR_CONNECT));
        httpTestBusy = 1;
        guiHandleDeferedIO(&httpTestBusy, _l(_STR_HTTP_TEST), IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&httpTestWorker), 0);
        guiMsgBox(httpTestMessage, 0, NULL);
        goto reshow_network;
    }
    if (result == NETCFG_POPSTARTER_BUTTON) {
        // This is the shared POPSTARTER network editor. It owns IPCONFIG.DAT / SMBCONFIG.DAT;
        // the Network page only provides a second entry point and never mirrors that state.
        guiShowPopsNetConfig();
        goto reshow_network;
    }
    if (result && !gSettingsShell.leftUntouched(diaNetConfig, result, NULL, NULL)) {
        int netProtoVal2, netAccessVal2;
        diaGetInt(diaNetConfig, CFG_NETPROTOCOL, &netProtoVal2);
        diaGetInt(diaNetConfig, CFG_UDPFSMODE, &netAccessVal2);

        // Under UDPFS/UDPBD the visible row is transport-effectively Static, but ps2_ip_use_dhcp
        // remains the user's SMB/HTTP preference. The UDP stacks never consult it; they always use
        // ps2_ip[] directly. This lets a DHCP user test UDP and return to SMB without losing DHCP.
        if (netProtoVal2 == 1 || netProtoVal2 == 2)
            ps2_ip_use_dhcp = netConfigDhcpPreference;
        else
            diaGetInt(diaNetConfig, NETCFG_PS2_IP_ADDR_TYPE, &ps2_ip_use_dhcp);

        // Store values
        diaGetInt(diaNetConfig, NETCFG_SHARE_ADDR_TYPE, &gPCShareAddressIsNetBIOS);
        diaGetString(diaNetConfig, NETCFG_SHARE_NB_ADDR, gPCShareNBAddress, sizeof(gPCShareNBAddress));

        for (i = 0; i < 4; ++i) {
            diaGetInt(diaNetConfig, NETCFG_PS2_IP_ADDR_0 + i, &ps2_ip[i]);
            diaGetInt(diaNetConfig, NETCFG_PS2_NETMASK_0 + i, &ps2_netmask[i]);
            diaGetInt(diaNetConfig, NETCFG_PS2_GATEWAY_0 + i, &ps2_gateway[i]);
            diaGetInt(diaNetConfig, NETCFG_PS2_DNS_0 + i, &ps2_dns[i]);
            diaGetInt(diaNetConfig, NETCFG_SHARE_IP_ADDR_0 + i, &pc_ip[i]);
        }
        diaGetInt(diaNetConfig, NETCFG_ETHOPMODE, &gETHOpMode);
#ifdef RETROACHIEVEMENTS
        int raBadgeState = gRATelemetry << 2 | gRABadges << 1 | gRABadgeSuffix;
        int raMode = gRAMode;
        diaGetInt(diaNetConfig, NETCFG_RA_MODE, &gRAMode);
        if (gRAMode != raMode)
            menuReinitMainMenu(); // the browser's entry is named after the mode
        for (i = 0; i < 4; ++i)
            diaGetInt(diaNetConfig, NETCFG_RA_HOST_IP_0 + i, &gRAHostIp[i]);
        diaGetInt(diaNetConfig, NETCFG_RA_TELEMETRY, &gRATelemetry);
        diaGetInt(diaNetConfig, NETCFG_RA_BADGES, &gRABadges);
        diaGetInt(diaNetConfig, NETCFG_RA_BADGE_SUFFIX, &gRABadgeSuffix);
        diaGetInt(diaNetConfig, NETCFG_RA_EXPERIMENTAL_CARDS, &gRAExperimentalCards);
        gRABadgeSuffix = (gRABadgeSuffix == 1);
        // Badges are resolved on the I/O thread when rows are built: rebuild every list (HTTP and
        // Favorites too) when visibility or placement changes, not on the next unrelated rescan.
        if ((gRATelemetry << 2 | gRABadges << 1 | gRABadgeSuffix) != raBadgeState)
            guiRefreshCfgTitles();
#endif

        diaGetInt(diaNetConfig, NETCFG_SHARE_PORT, &gPCPort);
        for (i = 0; i < 4; ++i)
            diaGetInt(diaNetConfig, NETCFG_HTTP_IP_0 + i, &gHttpServerIp[i]);
        diaGetInt(diaNetConfig, NETCFG_HTTP_PORT, &gHttpPort);
        diaGetString(diaNetConfig, NETCFG_HTTP_BASE, gHttpBasePath, sizeof(gHttpBasePath));
        httpNormalizeBasePath(gHttpBasePath, sizeof(gHttpBasePath));
        diaGetString(diaNetConfig, NETCFG_SHARE_NAME, gPCShareName, sizeof(gPCShareName));
        diaGetString(diaNetConfig, NETCFG_SHARE_USERNAME, gPCUserName, sizeof(gPCUserName));
        diaGetString(diaNetConfig, NETCFG_SHARE_PASSWORD, gPCPassword, sizeof(gPCPassword));

        // Fold the Protocol/Access rows back into the authoritative gNetworkProtocol. PROTOCOL-FIRST:
        // the Access value is consulted ONLY for UDPFS, so a stale IMG under SMB/UDPBD can never
        // mis-derive. The Game Sources "Network Start Mode" row decides whether the stack runs at
        // all: Start=Off -> OFF regardless of the protocol picked here. Then re-derive the legacy
        // shadows (gEnableUDPBD / gNetBootProtocol / gETHStartMode) downstream consumers read.
        // NOTE(rebuild): the fork also reads the SMB dialect row back here (item 4).
        int netProtocolWas = gNetworkProtocol;
        guiNetProtocolStore(netProtoVal2, netAccessVal2);
        if (netProtoVal2 != 1)
            gUdpfsAccessMode = netConfigUdpfsAccess; // retain pending preference even if switching away
        guiNetProtocolApplied(netProtocolWas);
        // OK is this page's apply button. A protocol change made on Game Sources (or declined here
        // earlier) is still waiting on a restart, so OK offers it again rather than doing nothing.
        if (gNetworkProtocol == netProtocolWas && (result == NETCFG_OK || result == NETCFG_RECONNECT) &&
            guiNetProtocolNeedsRestart())
            guiNetProtocolOfferRestart();

#ifdef RETROACHIEVEMENTS
        // Applied network/RA settings invalidate the peer a previous menu query verified.
        raNetForgetPeer();
#endif
        applyConfig(-1, -1, 0);

        // OK is an Apply action for the live SMB stack, not merely a RAM/config edit -- and after a
        // failed connect it is labelled Reconnect, so it reconnects even with nothing changed. Any
        // other way off the page (Back, L1/R1) keeps the edits too, but reconnects only when the SMB
        // link actually changed: backing out of a page untouched must not drop the session. A
        // failed startup may already have queued this through initAllSupport();
        // ethRequestReconnect coalesces the duplicate. Protocol changes that disagree with the
        // resident NIC remain restart-only and are deliberately excluded here.
        if (gNetworkProtocol == NET_PROTO_SMB && gETHStartMode != START_MODE_DISABLED &&
            !guiNetProtocolNeedsRestart() &&
            (result == NETCFG_OK || result == NETCFG_RECONNECT || guiSmbLinkChanged(&smbBefore)))
            ethRequestReconnect();
    }

    gSettingsShell.endDialog();
    return gSettingsShell.pageResult(result);
}

// Core path fields use a full-size backing buffer. UI_STRING's inline storage is intentionally
// short, so using diaGetString() directly would truncate perfectly valid cross-device paths.
// The row is only a preview; selecting it opens the normal full keyboard editor.
static char neutrinoPathEdit[sizeof(gNeutrinoPath)];
static char popstarterPathEdit[sizeof(gPopstarterPath)];

static void guiCorePathBegin(struct UIItem *ui, int id, char *edit, size_t editSize, const char *value)
{
    snprintf(edit, editSize, "%s", value != NULL ? value : "");
    diaSetString(ui, id, edit);
    // Empty is intentionally rendered by UI_STRING as the localized "<not set>" placeholder.
    // Do not opt into showDefaultWhenEmpty: that would display "Default", which is not the contract.
    diaSetShowDefaultWhenEmpty(ui, id, 0);
}

int guiNeutrinoPathHandler(char *text, int maxLen)
{
    if (!guiShowKeyboard(neutrinoPathEdit, sizeof(neutrinoPathEdit)))
        return 0;
    snprintf(text, maxLen, "%s", neutrinoPathEdit);
    return 1;
}

int guiPopstarterPathHandler(char *text, int maxLen)
{
    if (!guiShowKeyboard(popstarterPathEdit, sizeof(popstarterPathEdit)))
        return 0;
    snprintf(text, maxLen, "%s", popstarterPathEdit);
    return 1;
}

// BDMA Settings live-updater: hide the manual BDMA Source/Mode pickers while "VCD BDMA Apply on
// Launch" is ON (it auto-equips); re-reveal them live when toggled off.
static int guiBdmaUpdater(int modified)
{
    struct UIItem *ui = gSettingsShell.activeDialog() != NULL ? gSettingsShell.activeDialog() : diaBdmaConfig;
    int bdmaApply;

    if (modified) {
        diaGetInt(ui, CFG_BDMA_APPLY, &bdmaApply);
        diaSetVisible(ui, CFG_LBL_BDMASOURCE, !bdmaApply);
        diaSetVisible(ui, CFG_BDMASOURCE, !bdmaApply);
        diaSetVisible(ui, CFG_LBL_BDMAMODE, !bdmaApply);
        diaSetVisible(ui, CFG_BDMAMODE, !bdmaApply);
    }
    return 0;
}

// USB .VCD launches only: force the fat32/exFAT POPSTARTER driver pick EVERY launch -- the PS2
// cannot detect the filesystem a USB stick is actually formatted with, so the user chooses per
// launch. Returns the pressed button's id (VCDUSB_BTN_FAT32 / VCDUSB_BTN_EXFAT) or UIID_BTN_CANCEL.
int guiShowVcdUsbMode(void)
{
    return diaExecuteDialog(diaVcdUsbMode, -1, 1, NULL);
}

static void guiDiscListItem(struct UIItem *item, UIItemType type, int id, const char *text, int stringId)
{
    memset(item, 0, sizeof(*item));
    item->type = type;
    item->id = id;
    item->enabled = item->visible = 1;
    item->hintId = -1;
    item->label.text = text;
    item->label.stringId = stringId;
}

int guiShowEmberDiscList(const char *folder, const cue_disc_list_t *list)
{
    struct UIItem *dialog = (struct UIItem *)calloc(list->count * 2 + 5, sizeof(*dialog));
    char(*labels)[CUE_NAME_MAX + 32] = (char(*)[CUE_NAME_MAX + 32]) calloc(list->count, sizeof(*labels));
    int index = 0, result;

    if (dialog == NULL || labels == NULL) {
        free(dialog);
        free(labels);
        return -1;
    }
    guiDiscListItem(&dialog[index++], UI_HEADER, 0, NULL, _STR_EMBER_SELECT_DISC);
    guiDiscListItem(&dialog[index++], UI_SPLITTER, 0, NULL, -1);
    guiDiscListItem(&dialog[index++], UI_LABEL, 0, folder, -1);
    guiDiscListItem(&dialog[index++], UI_BREAK, 0, NULL, -1);
    for (int i = 0; i < list->count; i++) {
        snprintf(labels[i], sizeof(*labels), _l(_STR_EMBER_DISC_ENTRY), i + 1, list->names[i]);
        guiDiscListItem(&dialog[index++], UI_BUTTON, i + 1, labels[i], -1);
        guiDiscListItem(&dialog[index++], UI_BREAK, 0, NULL, -1);
    }
    guiDiscListItem(&dialog[index], UI_TERMINATOR, 0, NULL, -1);
    result = diaExecuteDialog(dialog, 1, 1, NULL);
    free(labels);
    free(dialog);
    return (result >= 1 && result <= list->count) ? result - 1 : -1;
}

static void guiSetBdmaSettings(struct UIItem *ui)
{
    static const char *bdmaSourceStrs[] = {NULL, NULL, NULL, NULL, NULL, NULL};
    static const char *bdmaModeStrs[VCD_BDMA_MODE_COUNT + 1];
    // Order matches enum VCD_USB_BDMA_MODE: Ask / exFAT / fat32. The two driver labels are the ones the
    // per-launch prompt already uses, so the row and the dialog it replaces read identically.
    static const char *vcdUsbBdmaStrs[] = {NULL, NULL, NULL, NULL};

    // diaSetEnum stores the array pointer rather than copying it. These arrays must therefore outlive
    // this initializer, especially when the BDMA rows are composed into the POPSTARTER peer page.
    bdmaSourceStrs[0] = _l(_STR_BDMA_SRC_USB);
    bdmaSourceStrs[1] = _l(_STR_BDMA_SRC_MX4SIO);
    bdmaSourceStrs[2] = _l(_STR_BDMA_SRC_MMCE);
    bdmaSourceStrs[3] = _l(_STR_BDMA_SRC_HDD);
    bdmaSourceStrs[4] = _l(_STR_BDMA_SRC_ILINK);
    bdmaModeStrs[0] = _l(_STR_BDMA_MODE_FAT32);
    bdmaModeStrs[1] = _l(_STR_BDMA_MODE_USBEXFAT);
    bdmaModeStrs[2] = _l(_STR_BDMA_MODE_MX4SIO);
    bdmaModeStrs[3] = _l(_STR_BDMA_MODE_MMCE);
    bdmaModeStrs[4] = _l(_STR_BDMA_MODE_ATA);
    bdmaModeStrs[5] = _l(_STR_BDMA_MODE_ILINK);
    bdmaModeStrs[VCD_BDMA_UDPFS] = "UDPFS";
    vcdUsbBdmaStrs[0] = _l(_STR_VCD_USB_BDMA_ASK);
    vcdUsbBdmaStrs[1] = _l(_STR_VCD_USB_MODE_EXFAT);
    vcdUsbBdmaStrs[2] = _l(_STR_VCD_USB_MODE_FAT32);

    gBdmaMode = vcdReadBdmaMode();
    diaSetEnum(ui, CFG_BDMASOURCE, bdmaSourceStrs);
    diaSetEnum(ui, CFG_BDMAMODE, bdmaModeStrs);
    diaSetEnum(ui, CFG_VCD_USB_BDMA, vcdUsbBdmaStrs);
    diaSetInt(ui, CFG_BDMASOURCE, gBdmaSource);
    diaSetInt(ui, CFG_BDMAMODE, gBdmaMode);
    diaSetInt(ui, CFG_VCD_USB_BDMA, gVcdUsbBdmaMode);
    diaSetInt(ui, CFG_BDMA_APPLY, gBdmaApplyOnLaunch);
    // "VCD BDMA Apply on Launch" ON auto-equips, so hide the manual SOURCE/MODE pickers
    // (guiBdmaUpdater re-reveals them live when toggled off).
    diaSetVisible(ui, CFG_LBL_BDMASOURCE, !gBdmaApplyOnLaunch);
    diaSetVisible(ui, CFG_BDMASOURCE, !gBdmaApplyOnLaunch);
    diaSetVisible(ui, CFG_LBL_BDMAMODE, !gBdmaApplyOnLaunch);
    diaSetVisible(ui, CFG_BDMAMODE, !gBdmaApplyOnLaunch);
}

// Save the BDMA preference and equip changed modules. The helper deliberately does not call
// applyConfig so a composed Settings page can commit all of its fields together.
static void guiSaveBdmaSettings(struct UIItem *ui)
{
    diaGetInt(ui, CFG_BDMA_APPLY, &gBdmaApplyOnLaunch);
    // Read BEFORE the equip block below: that block can re-enter vcdEquipBdma and toast, and this
    // row must be taken from the dialog regardless of how the equip turns out (it governs the
    // per-launch USB prompt, not the card's equipped state).
    diaGetInt(ui, CFG_VCD_USB_BDMA, &gVcdUsbBdmaMode);
    {
        // Equip BDMA modules only when SOURCE or MODE actually changed (the equip copies files to
        // the memory card). vcdEquipBdma is free-space-gated + truncation-safe, so a failure never
        // corrupts the card; report it and resync MODE to what's really equipped.
        int oldSrc = gBdmaSource, oldMode = gBdmaMode; // baselines (MODE = card's actual state)
        int newSrc = oldSrc, newMode = oldMode;
        diaGetInt(ui, CFG_BDMASOURCE, &newSrc);
        diaGetInt(ui, CFG_BDMAMODE, &newMode);
        // Re-equip on any MODE change, and on a SOURCE change only when MODE installs modules. FAT32
        // ignores the source, so a SOURCE-only move while already FAT32 must NOT re-run the pointless work.
        if (newMode != oldMode || (newMode != VCD_BDMA_FAT32 && newSrc != oldSrc)) {
            char bdmaDiag[160];
            int er = vcdEquipBdma(newSrc, newMode, bdmaDiag, sizeof(bdmaDiag));
            if (er == -4) {
                char msg[256];
                snprintf(msg, sizeof(msg), "%s\n%s", _l(_STR_BDMA_ERR_SRC), bdmaDiag);
                guiMsgBox(msg, 0, NULL);
            } else if (er == -2)
                guiMsgBox(_l(_STR_BDMA_ERR_SPACE), 0, NULL);
            else if (er == -3)
                guiMsgBox(_l(_STR_BDMA_ERR_IO), 0, NULL);
            gBdmaMode = vcdReadBdmaMode(); // MODE = what is now actually equipped
        }
        gBdmaSource = newSrc; // remember the source preference regardless of equip outcome
    }
}

// POPStarter -> Game List Settings (VCD list display options).
static void guiShowVcdListConfig(void)
{
    diaSetInt(diaVcdListConfig, CFG_VCD_HIDE_GAMEID, gVcdHideGameId);
    diaSetInt(diaVcdListConfig, CFG_VCD_FIRST_DISC_ONLY, gVcdFirstDiscOnly);
    diaSetInt(diaVcdListConfig, CFG_VCD_SHOW_PP_POPS, gVcdShowPpPops);

    int rebuildVcdLists = 0;
    int ret = diaExecuteDialog(diaVcdListConfig, -1, 1, NULL);
    if (ret) {
        // This editor is reachable from both Settings parents. Its values still belong to the
        // existing globals/config set, but an OK inside the shell must participate in the shell's
        // shared Save Changes prompt regardless of which parent opened it.
        if (gSettingsShell.shellActive())
            gSettingsShell.markSavePending();
        {
            // #195: hide-gameid is NO LONGER purely cosmetic -- it is now a SORT KEY. The menu sort
            // (submenuSort) orders by the DISPLAYED title, i.e. past the hidden prefix, so a change must
            // re-sort every VCD-capable page. libViewMarkAllDirty() + rebuildVcdLists forces that menu rebuild
            // and its submenuSort re-runs against the new vcdDisplayName key -- WITHOUT re-reading any
            // device. Do NOT invalidate the HDD VCD cache here (unlike first-disc-only below): this toggle
            // changes only the DISPLAY ORDER, not the list CONTENTS, and submenuSort reorders the cached
            // rows in place. hddVcdInvalidateCache() would force an unnecessary full re-walk of every
            // __.POPS partition (pfs1: remount) plus a transient empty HDD VCD page on every flip (audit
            // 2026-07-17 of the CodeRabbit #200 fix -- my "sorted with the old key" note was true but
            // irrelevant, the menu sort fixes the order regardless of the backing array).
            int previousHideGameId = gVcdHideGameId;
            diaGetInt(diaVcdListConfig, CFG_VCD_HIDE_GAMEID, &gVcdHideGameId);
            if (gVcdHideGameId != previousHideGameId) {
                libViewMarkAllDirty();
                rebuildVcdLists = 1;
            }
        }
        {
            // #118: first-disc-only changes the VCD list CONTENTS (discs hidden/shown), so unlike the
            // cosmetic hide-gameid it must rebuild every VCD-capable device page when toggled. Device
            // lists only -- Favourites are intentionally left unfiltered (an explicit user pick).
            int previousFirstDiscOnly = gVcdFirstDiscOnly;
            diaGetInt(diaVcdListConfig, CFG_VCD_FIRST_DISC_ONLY, &gVcdFirstDiscOnly);
            if (gVcdFirstDiscOnly != previousFirstDiscOnly) {
                libViewMarkAllDirty();
                hddVcdInvalidateCache(); // scan-time filter changed -> the cached HDD VCD list is stale
                rebuildVcdLists = 1;
            }
        }
        {
            // PP-POPS display changes the HDD VCD list CONTENTS (one-game partitions included or not).
            // Same treatment as first-disc-only: it is a scan-time filter, so the cached HDD VCD list
            // is stale the moment it flips. Enumeration-only -- launch semantics never change.
            int previousShowPpPops = gVcdShowPpPops;
            diaGetInt(diaVcdListConfig, CFG_VCD_SHOW_PP_POPS, &gVcdShowPpPops);
            if (gVcdShowPpPops != previousShowPpPops) {
                libViewMarkAllDirty();
                hddVcdInvalidateCache();
                rebuildVcdLists = 1;
            }
        }
        applyConfig(-1, -1, 0);
        menuReinitMainMenu();
        // Queue after applyConfig/menu reinit so the IO worker cannot rebuild a submenu concurrently
        // with their support/menu bookkeeping on the GUI thread.
        if (rebuildVcdLists)
            oplQueueLibraryDeviceUpdates();
    }
}

// POPSTARTER's local memory-card resolver evaluates both mc0 and mc1, including directory/file
// RPCs. On a slow or absent card that work can take seconds. Keep it on the I/O worker and pump a
// real runtime frame here; guiHandleDeferedIO is intentionally not used because its text screen
// does not poll input or animate the Settings plasma.
constexpr int POPSNET_READ_TIMEOUT_MS = 15000;
// Distinct abort reasons. The editor used to return a single ABORTED code and the caller simply
// returned, so a read that timed out after fifteen seconds and a read the user cancelled were
// indistinguishable on screen -- both silently dropped you back on the network page with no
// explanation. Only CANCELLED is the user's own doing and stays quiet.
constexpr int GUI_POPSNET_READ_ABORTED = (-100); // user pressed Circle
constexpr int GUI_POPSNET_READ_TIMEOUT = (-101); // the worker never finished inside POPSNET_READ_TIMEOUT_MS
constexpr int GUI_POPSNET_READ_UNAVAIL = (-102); // the request could not be queued at all (I/O blocked/OOM)
enum gui_popsnet_read_source {
    GUI_POPSNET_READ_LOCAL = 0,
    GUI_POPSNET_READ_SMB,
};
static int guiPopsNetReadPending;
static int guiPopsNetReadInFlight;
static int guiPopsNetReadAbandoned;
static int guiPopsNetReadSource;
static int guiPopsNetReadResult;
static vcd_popsnet_t guiPopsNetReadData;

static void guiPopsNetReadWorker(void)
{
    vcd_popsnet_t data;
    int result;

    if (guiPopsNetReadSource == GUI_POPSNET_READ_LOCAL)
        result = vcdReadPopstarterNet(&data);
    else
        result = vcdReadPopstarterNetFromSmb(&data);

    if (!guiPopsNetReadAbandoned) {
        guiPopsNetReadData = data;
        guiPopsNetReadResult = result;
    }
    guiPopsNetReadInFlight = 0;
    guiPopsNetReadPending = 0;
}

static int guiReadPopsNet(int source, vcd_popsnet_t *out)
{
    clock_t startTick;
    clock_t timeoutTicks = (clock_t)POPSNET_READ_TIMEOUT_MS * (CLOCKS_PER_SEC / 1000);

    if (guiPopsNetReadInFlight) {
        // A cancelled request still owns the static payload until its I/O RPC returns. Reopening the
        // same editor may resume its wait, but a second source may not overwrite that payload.
        if (guiPopsNetReadSource != source)
            return GUI_POPSNET_READ_UNAVAIL;
        guiPopsNetReadAbandoned = 0;
    } else {
        memset(&guiPopsNetReadData, 0, sizeof(guiPopsNetReadData));
        guiPopsNetReadSource = source;
        guiPopsNetReadResult = GUI_POPSNET_READ_ABORTED;
        guiPopsNetReadAbandoned = 0;
        guiPopsNetReadPending = 1;
        guiPopsNetReadInFlight = 1;
        if (ioPutRequest(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&guiPopsNetReadWorker)) != IO_OK) {
            guiPopsNetReadInFlight = 0;
            guiPopsNetReadPending = 0;
            return GUI_POPSNET_READ_UNAVAIL;
        }
    }

    startTick = clock();
    while (guiPopsNetReadInFlight) {
        guiStartFrame();
        if (guiDrawBGSettings() == 0)
            guiDrawBGPlasma();
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColDarker);
        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, gTheme->usedHeight >> 1,
                        ALIGN_CENTER, 0, 0, _l(_STR_POPS_LOADING_SETTINGS), gTheme->textColor);
        guiDrawOverlays();
        guiEndFrame();

        readPads();
        if (getKeyOn(guiCancelKey())) {
            guiPopsNetReadAbandoned = 1;
            return GUI_POPSNET_READ_ABORTED;
        }
        if ((clock() - startTick) >= timeoutTicks) {
            guiPopsNetReadAbandoned = 1;
            return GUI_POPSNET_READ_TIMEOUT;
        }
    }

    if (out != NULL)
        *out = guiPopsNetReadData;
    return guiPopsNetReadResult;
}

static void guiSetPopsNetDialogFields(const vcd_popsnet_t *cfg)
{
    size_t i;

    diaSetInt(diaPopsNetConfig, NETCFG_POPS_IPTYPE, cfg->ipDhcp);
    for (i = 0; i < 4; ++i) {
        diaSetInt(diaPopsNetConfig, NETCFG_POPS_IP_0 + i, cfg->ps2Ip[i]);
        diaSetInt(diaPopsNetConfig, NETCFG_POPS_MASK_0 + i, cfg->ps2Mask[i]);
        diaSetInt(diaPopsNetConfig, NETCFG_POPS_GW_0 + i, cfg->ps2Gw[i]);
        diaSetInt(diaPopsNetConfig, NETCFG_POPS_SMB_IP_0 + i, cfg->smbIp[i]);
        diaSetEnabled(diaPopsNetConfig, NETCFG_POPS_IP_0 + i, !cfg->ipDhcp);
        diaSetEnabled(diaPopsNetConfig, NETCFG_POPS_MASK_0 + i, !cfg->ipDhcp);
        diaSetEnabled(diaPopsNetConfig, NETCFG_POPS_GW_0 + i, !cfg->ipDhcp);
    }
    diaSetInt(diaPopsNetConfig, NETCFG_POPS_SMB_PORT, cfg->smbPort);
    diaSetString(diaPopsNetConfig, NETCFG_POPS_SMB_SHARE, cfg->smbShare);
    diaSetString(diaPopsNetConfig, NETCFG_POPS_SMB_USER, cfg->smbUser);
    diaSetString(diaPopsNetConfig, NETCFG_POPS_SMB_PASS, cfg->smbPass);
}

// POPStarter -> Network Settings live-updater: DHCP = no static IP/mask/gateway triple.
static int guiPopsNetUpdater(int modified)
{
    int isPopsDhcp, i;

    if (modified) {
        diaGetInt(diaPopsNetConfig, NETCFG_POPS_IPTYPE, &isPopsDhcp);
        for (i = 0; i < 4; i++) {
            diaSetEnabled(diaPopsNetConfig, NETCFG_POPS_IP_0 + i, !isPopsDhcp);
            diaSetEnabled(diaPopsNetConfig, NETCFG_POPS_MASK_0 + i, !isPopsDhcp);
            diaSetEnabled(diaPopsNetConfig, NETCFG_POPS_GW_0 + i, !isPopsDhcp);
        }
    }

    return 0;
}

// POPStarter -> Network Settings (VCD over SMB). The local memory-card snapshot remains the sole
// write owner. The explicit import path only copies valid POPS/*.DAT values into the editor, so the
// normal change-detection/Replace flow still decides whether anything is committed to an MC.
static void guiShowPopsUdpfsConfig(void)
{
    vcd_pops_udpfs_t original, cfg;
    static char notice[128];
    if (vcdReadPopsUdpfsNet(&original) != 0) {
        guiMsgBox(_l(_STR_POPS_UDPFS_NOT_READY), 0, NULL);
        return;
    }
    const char *suffix = "";
    if (original.invalid)
        suffix = _l(_STR_POPS_UDPFS_INVALID_SUFFIX);
    else if (!original.ipExists || !original.serverExists)
        suffix = _l(_STR_POPS_UDPFS_INCOMPLETE_SUFFIX);
    snprintf(notice, sizeof(notice), "%s%s", original.home, suffix);
    diaSetLabel(diaPopsUdpfsConfig, NETCFG_POPS_NOTICE, notice);
    for (int i = 0; i < 4; i++) {
        diaSetInt(diaPopsUdpfsConfig, NETCFG_POPS_IP_0 + i, original.ps2Ip[i]);
        diaSetInt(diaPopsUdpfsConfig, NETCFG_POPS_MASK_0 + i, original.mask[i]);
        diaSetInt(diaPopsUdpfsConfig, NETCFG_POPS_GW_0 + i, original.gateway[i]);
        diaSetInt(diaPopsUdpfsConfig, NETCFG_POPS_SMB_IP_0 + i, original.server[i]);
    }
    for (;;) {
        if (diaExecuteDialog(diaPopsUdpfsConfig, -1, 1, NULL) != UIID_BTN_OK)
            return;
        cfg = original;
        for (int i = 0; i < 4; i++) {
            diaGetInt(diaPopsUdpfsConfig, NETCFG_POPS_IP_0 + i, &cfg.ps2Ip[i]);
            diaGetInt(diaPopsUdpfsConfig, NETCFG_POPS_MASK_0 + i, &cfg.mask[i]);
            diaGetInt(diaPopsUdpfsConfig, NETCFG_POPS_GW_0 + i, &cfg.gateway[i]);
            diaGetInt(diaPopsUdpfsConfig, NETCFG_POPS_SMB_IP_0 + i, &cfg.server[i]);
        }
        if (!vcdPopsUdpfsNetValid(&cfg)) {
            guiMsgBox(_l(_STR_POPSTARTER_NET_INVALID), 0, NULL);
            continue;
        }
        if (original.ipExists && original.serverExists && !original.invalid &&
            !memcmp(cfg.ps2Ip, original.ps2Ip, sizeof(cfg.ps2Ip)) &&
            !memcmp(cfg.mask, original.mask, sizeof(cfg.mask)) &&
            !memcmp(cfg.gateway, original.gateway, sizeof(cfg.gateway)) &&
            !memcmp(cfg.server, original.server, sizeof(cfg.server)))
            return;
        if ((original.ipExists || original.serverExists) &&
            diaExecuteDialog(diaPopsOverwrite, -1, 1, NULL) != POPS_OVERWRITE_REPLACE)
            continue;
        if (vcdWritePopsUdpfsNet(&cfg) != 0) {
            guiMsgBox(_l(_STR_POPSTARTER_NET_ERR), 0, NULL);
            continue;
        }
        return;
    }
}

void guiShowPopsNetConfig(void)
{
    size_t i;
    static vcd_popsnet_t popsOriginal;
    static char popsNotice[128];
    static const char *ipAddrConfModes[3];

    ipAddrConfModes[0] = _l(_STR_IP_ADDRESS_TYPE_STATIC);
    ipAddrConfModes[1] = _l(_STR_IP_ADDRESS_TYPE_DHCP);
    ipAddrConfModes[2] = NULL;
    diaSetEnum(diaPopsNetConfig, NETCFG_POPS_IPTYPE, ipAddrConfModes);

    int readResult = guiReadPopsNet(GUI_POPSNET_READ_LOCAL, &popsOriginal);
    if (readResult == GUI_POPSNET_READ_ABORTED)
        return; // the user cancelled; they know why they are back on the previous page
    if (readResult == GUI_POPSNET_READ_TIMEOUT || readResult == GUI_POPSNET_READ_UNAVAIL) {
        // Say so rather than dropping the user back on the network page with no explanation. The
        // page is unreachable here for a transient reason -- the single I/O worker is busy, or the
        // card scan (both slots, directory and file RPCs) outran the fifteen-second bound.
        guiMsgBox(_l(_STR_POPS_SETTINGS_UNREADABLE), 0, NULL);
        return;
    }
    guiSetPopsNetDialogFields(&popsOriginal);

    if (popsOriginal.smbInvalid || popsOriginal.ipInvalid) {
        diaSetLabel(diaPopsNetConfig, NETCFG_POPS_NOTICE, _l(_STR_POPSTARTER_NET_INVALID));
    } else if (popsOriginal.smbExists || popsOriginal.ipExists) {
        snprintf(popsNotice, sizeof(popsNotice), _l(_STR_POPS_LOADED_FROM), popsOriginal.home);
        diaSetLabel(diaPopsNetConfig, NETCFG_POPS_NOTICE, popsNotice);
    } else {
        diaSetLabel(diaPopsNetConfig, NETCFG_POPS_NOTICE, _l(_STR_POPS_NONE_DETECTED));
    }

    for (;;) {
        int result = diaExecuteDialog(diaPopsNetConfig, -1, 1, &guiPopsNetUpdater);
        if (result == NETCFG_POPS_UDPFS) {
            guiShowPopsUdpfsConfig();
            continue;
        }
        if (result == NETCFG_POPS_IMPORT) {
            vcd_popsnet_t imported;
            int importResult;

            // Do not start or reconnect SMB merely to import. Its mounted state, not the saved
            // protocol picker, is the authority for whether these files are readable.
            if (!ethIsSMBShareConnected()) {
                guiMsgBox(_l(_STR_POPS_SMB_NOT_CONNECTED), 0, NULL);
                continue;
            }
            importResult = guiReadPopsNet(GUI_POPSNET_READ_SMB, &imported);
            if (importResult == GUI_POPSNET_READ_ABORTED)
                return;
            if (importResult == GUI_POPSNET_READ_TIMEOUT || importResult == GUI_POPSNET_READ_UNAVAIL) {
                // Stay on the editor for the import path -- the user has unsaved fields in front of
                // them and dropping out would discard them for a transient share/worker problem.
                guiMsgBox(_l(_STR_POPS_SETTINGS_UNREADABLE), 0, NULL);
                continue;
            }
            if (importResult == VCD_POPSNET_SMB_IMPORT_NOT_CONNECTED) {
                guiMsgBox(_l(_STR_POPS_SMB_NOT_CONNECTED), 0, NULL);
                continue;
            }
            if (importResult != VCD_POPSNET_SMB_IMPORT_OK) {
                guiMsgBox(_l(_STR_POPS_SMB_SETTINGS_NOT_FOUND), 0, NULL);
                continue;
            }
            guiSetPopsNetDialogFields(&imported);
            continue;
        }

        if (result != UIID_BTN_OK)
            return;

        // POPStarter save matrix (POPSLoader parity): compare the dialog's POPStarter fields against
        // the read-time snapshot and write ONLY actual changes. The resolved home from the snapshot
        // remains attached to popsCur, so a valid MC1 setup never causes a new MC0 shadow file.
        vcd_popsnet_t popsCur = popsOriginal;
        diaGetInt(diaPopsNetConfig, NETCFG_POPS_IPTYPE, &popsCur.ipDhcp);
        for (i = 0; i < 4; ++i) {
            diaGetInt(diaPopsNetConfig, NETCFG_POPS_IP_0 + i, &popsCur.ps2Ip[i]);
            diaGetInt(diaPopsNetConfig, NETCFG_POPS_MASK_0 + i, &popsCur.ps2Mask[i]);
            diaGetInt(diaPopsNetConfig, NETCFG_POPS_GW_0 + i, &popsCur.ps2Gw[i]);
            diaGetInt(diaPopsNetConfig, NETCFG_POPS_SMB_IP_0 + i, &popsCur.smbIp[i]);
        }
        diaGetInt(diaPopsNetConfig, NETCFG_POPS_SMB_PORT, &popsCur.smbPort);
        diaGetString(diaPopsNetConfig, NETCFG_POPS_SMB_SHARE, popsCur.smbShare, sizeof(popsCur.smbShare));
        diaGetString(diaPopsNetConfig, NETCFG_POPS_SMB_USER, popsCur.smbUser, sizeof(popsCur.smbUser));
        diaGetString(diaPopsNetConfig, NETCFG_POPS_SMB_PASS, popsCur.smbPass, sizeof(popsCur.smbPass));

        int popsMask = vcdPopsNetChanged(&popsOriginal, &popsCur);
        int popsSmbComplete = vcdPopsNetValuesValid(&popsCur, 1, 0);
        int popsIpComplete = !popsCur.ipDhcp && vcdPopsNetValuesValid(&popsCur, 0, 1);
        int writeSmb = (popsMask & 1) && (popsOriginal.smbExists || popsSmbComplete);
        int writeIp = ((popsMask & 2) || popsOriginal.ipInvalid) &&
                      (popsOriginal.ipExists || popsCur.ipDhcp || popsIpComplete);

        if (writeSmb && !popsOriginal.smbExists && !popsOriginal.ipExists) {
            writeIp = 1;
            if (!popsCur.ipDhcp && !popsIpComplete)
                popsCur.ipDhcp = 1;
        }

        if (!writeSmb && !writeIp)
            return;
        if (!vcdPopsNetValuesValid(&popsCur, writeSmb, writeIp)) {
            guiMsgBox(_l(_STR_POPSTARTER_NET_INVALID), 0, NULL);
            continue;
        }

        if ((writeSmb && popsOriginal.smbExists) || (writeIp && popsOriginal.ipExists)) {
            int ov = diaExecuteDialog(diaPopsOverwrite, -1, 1, NULL);
            if (ov != POPS_OVERWRITE_REPLACE)
                continue;
        }

        if (vcdWritePopstarterNetFiles(&popsCur, writeSmb, writeIp) != 0) {
            guiMsgBox(_l(_STR_POPSTARTER_NET_ERR), 0, NULL);
            continue;
        }
        return;
    }
}

static void guiSetParentalLockValue(struct UIItem *ui)
{
    char password[CONFIG_KEY_VALUE_LEN];
    config_set_t *configOPL = configGetByType(CONFIG_OPL);

    configGetStrCopy(configOPL, CONFIG_OPL_PARENTAL_LOCK_PWD, password, sizeof(password));
    diaSetString(ui, CFG_PARENLOCK_PASSWORD, password);
}

static void guiSaveParentalLockValue(struct UIItem *ui)
{
    char oldPassword[CONFIG_KEY_VALUE_LEN];
    char password[CONFIG_KEY_VALUE_LEN];
    config_set_t *configOPL = configGetByType(CONFIG_OPL);

    configGetStrCopy(configOPL, CONFIG_OPL_PARENTAL_LOCK_PWD, oldPassword, sizeof(oldPassword));
    diaGetString(ui, CFG_PARENLOCK_PASSWORD, password, sizeof(password));
    if (strcmp(oldPassword, password) == 0)
        return;

    if (strlen(password) > 0) {
        if (strncmp(OPL_PARENTAL_LOCK_MASTER_PASS, password, sizeof(password)) != 0)
            configSetStr(configOPL, CONFIG_OPL_PARENTAL_LOCK_PWD, password);
        else {
            diaSetString(ui, CFG_PARENLOCK_PASSWORD, oldPassword);
            guiMsgBox(_l(_STR_PARENLOCK_INVALID_PASSWORD), 0, NULL);
            return;
        }
    } else {
        configRemoveKey(configOPL, CONFIG_OPL_PARENTAL_LOCK_PWD);
        guiMsgBox(_l(_STR_PARENLOCK_DISABLE_WARNING), 0, ui);
    }

    menuSetParentalLockCheckState(1);
}

static void guiSetAdvancedSettings(struct UIItem *ui)
{
    diaSetString(ui, CFG_BDMPREFIX, gBDMPrefix);
    diaSetString(ui, CFG_ETHPREFIX, gETHPrefix);
    diaSetInt(ui, CFG_HDDSPINDOWN, gHDDSpindown);
    diaSetInt(ui, CFG_HDDGAMELISTCACHE, gHDDGameListCache);
    diaSetInt(ui, CFG_BDMCACHE, bdmCacheSize);
    diaSetInt(ui, CFG_HDDCACHE, hddCacheSize);
    diaSetInt(ui, CFG_SMBCACHE, smbCacheSize);
}

static void guiSaveAdvancedSettings(struct UIItem *ui)
{
    diaGetString(ui, CFG_BDMPREFIX, gBDMPrefix, sizeof(gBDMPrefix));
    sanitizePrefix(gBDMPrefix);
    diaGetString(ui, CFG_ETHPREFIX, gETHPrefix, sizeof(gETHPrefix));
    sanitizePrefix(gETHPrefix);
    diaGetInt(ui, CFG_HDDSPINDOWN, &gHDDSpindown);
    diaGetInt(ui, CFG_HDDGAMELISTCACHE, &gHDDGameListCache);
    diaGetInt(ui, CFG_BDMCACHE, &bdmCacheSize);
    diaGetInt(ui, CFG_HDDCACHE, &hddCacheSize);
    diaGetInt(ui, CFG_SMBCACHE, &smbCacheSize);
}

// Neutrino Launch Args sub-screen: edit the user-settable Neutrino flags as structured fields and
// reassemble them in a Neutrino-accepted order (--b last). Used for both the global args (here) and
// the per-game args. argsBuf in/out is the stored "Launch Args" string.
// UI_STRING holds only a 31-character preview. The callbacks edit the complete backing fields, so
// a path or argument can be changed on the console without hand-editing a config file.
static neutrino_args_t neutrinoArgsEdit;

#define NEUTRINO_ARG_HANDLER(name, field)                                             \
    int guiNeutrinoArgs##name##Handler(char *text, int maxLen)                        \
    {                                                                                 \
        if (!guiShowKeyboard(neutrinoArgsEdit.field, sizeof(neutrinoArgsEdit.field))) \
            return 0;                                                                 \
        snprintf(text, maxLen, "%s", neutrinoArgsEdit.field);                         \
        return 1;                                                                     \
    }

NEUTRINO_ARG_HANDLER(Cwd, cwd)
NEUTRINO_ARG_HANDLER(Cfg, cfg)
NEUTRINO_ARG_HANDLER(Elf, elf)
NEUTRINO_ARG_HANDLER(Ata0, ata0)
NEUTRINO_ARG_HANDLER(Ata0id, ata0id)
NEUTRINO_ARG_HANDLER(Ata1, ata1)
NEUTRINO_ARG_HANDLER(Extra, extra)
#undef NEUTRINO_ARG_HANDLER

int guiShowNeutrinoArgsConfig(char *argsBuf, int bufSize)
{
    if (argsBuf == NULL || bufSize <= 0)
        return 0;
    neutrino_args_t na;
    neutrinoArgsParse(argsBuf, &na);
    neutrinoArgsEdit = na;

    diaSetInt(diaNeutrinoArgs, NARGS_QB, na.qb ? 1 : 0);
    diaSetInt(diaNeutrinoArgs, NARGS_DBC, na.dbc ? 1 : 0);
    diaSetInt(diaNeutrinoArgs, NARGS_LOGO, na.logo ? 1 : 0);
    diaSetString(diaNeutrinoArgs, NARGS_CWD, na.cwd);
    diaSetString(diaNeutrinoArgs, NARGS_CFG, na.cfg);
    diaSetString(diaNeutrinoArgs, NARGS_ELF, na.elf);
    diaSetString(diaNeutrinoArgs, NARGS_ATA0, na.ata0);
    diaSetString(diaNeutrinoArgs, NARGS_ATA0ID, na.ata0id);
    diaSetString(diaNeutrinoArgs, NARGS_ATA1, na.ata1);
    diaSetString(diaNeutrinoArgs, NARGS_EXTRA, na.extra);

    while (diaExecuteDialog(diaNeutrinoArgs, -1, 1, NULL)) {
        char after[256];
        na = neutrinoArgsEdit;
        diaGetInt(diaNeutrinoArgs, NARGS_QB, &na.qb);
        diaGetInt(diaNeutrinoArgs, NARGS_DBC, &na.dbc);
        diaGetInt(diaNeutrinoArgs, NARGS_LOGO, &na.logo);
        int afterSize = bufSize < (int)sizeof(after) ? bufSize : (int)sizeof(after);
        if (!neutrinoArgsAssemble(&na, after, afterSize)) {
            guiMsgBox(_l(_STR_NEUTRINO_ARGS_TOO_LONG), 0, NULL);
            continue;
        }
        int changed = strcmp(argsBuf, after) != 0;
        snprintf(argsBuf, bufSize, "%s", after);
        return changed;
    }
    return 0;
}


static const char *artDelayNames[] = {"0 (Instant)", "2 (Fast)", "5 (Medium)", "8 (Standard)", NULL};
static const int artDelayValues[] = {0, 2, 5, 8};

static int artDelayToEnum(int delay)
{
    if (delay <= 0)
        return 0;
    if (delay <= 3)
        return 1;
    if (delay <= 6)
        return 2;
    return 3;
}

// Interface -> Artwork Settings (cover art display + ART.TAR loading).
static void guiShowArtworkConfig(void)
{
    diaSetInt(diaArtworkConfig, UICFG_COVERART, gEnableArt);
    diaSetInt(diaArtworkConfig, UICFG_ENABLE_DISCART, gEnableDiscArt);
    diaSetInt(diaArtworkConfig, UICFG_ENABLE_LOGOART, gEnableLogoArt);
    diaSetInt(diaArtworkConfig, UICFG_ENABLE_BGART, gEnableBGArt);
    diaSetInt(diaArtworkConfig, UICFG_ENABLE_SAVE_ICONS, gEnableSaveIcons);
    diaSetInt(diaArtworkConfig, UICFG_ENABLE_ART_TAR, gEnableArtTar);
    diaSetEnum(diaArtworkConfig, UICFG_ART_DELAY, artDelayNames);
    diaSetInt(diaArtworkConfig, UICFG_ART_DELAY, artDelayToEnum(gArtDelay));

    int ret = diaExecuteDialog(diaArtworkConfig, -1, 1, NULL);
    if (ret) {
        diaGetInt(diaArtworkConfig, UICFG_COVERART, &gEnableArt);
        diaGetInt(diaArtworkConfig, UICFG_ENABLE_DISCART, &gEnableDiscArt);
        diaGetInt(diaArtworkConfig, UICFG_ENABLE_LOGOART, &gEnableLogoArt);
        diaGetInt(diaArtworkConfig, UICFG_ENABLE_BGART, &gEnableBGArt);
        diaGetInt(diaArtworkConfig, UICFG_ENABLE_SAVE_ICONS, &gEnableSaveIcons);
        if (!gEnableSaveIcons)
            saveIconReset(); // hand back the model, its texture and the work buffers
        {
            // Re-arm the .tar probe when the toggle actually flips. tarFind's "no archive anywhere"
            // latch is write-once and process-wide, so a user who turns the loader on after boot would
            // otherwise keep getting nothing until a reboot -- which looks exactly like "the toggle
            // does nothing".
            int previousArtTar = gEnableArtTar;
            diaGetInt(diaArtworkConfig, UICFG_ENABLE_ART_TAR, &gEnableArtTar);
            if (gEnableArtTar != previousArtTar)
                tarInvalidate(TAR_KIND_ART);
        }
        int artDelayIdx = 0;
        diaGetInt(diaArtworkConfig, UICFG_ART_DELAY, &artDelayIdx);
        if (artDelayIdx >= 0 && artDelayIdx < 4) {
            gArtDelay = artDelayValues[artDelayIdx];
            item_list_t *lists[] = {appGetObject(1), hddGetObject(1), ethGetObject(1), mmceGetObject(1), udpfsGetObject(1), favGetObject(1)};
            for (int i = 0; i < 6; i++) {
                if (lists[i] != NULL)
                    lists[i]->delay = gArtDelay;
            }
        }

        applyConfig(-1, -1, 1);
    }
}

void guiShowColorsConfig(void)
{
    int themeID = thmGetGuiValue();
    int editable = thmGetFilePath(themeID) == NULL; // <OPL> and the built-in flows take the picker's palette

    if (editable) {
        // Display the default theme's colours.
        diaSetColor(diaColorsConfig, UICFG_BGCOL, gDefaultBgColor);
        diaSetColor(diaColorsConfig, UICFG_UICOL, gDefaultUITextColor);
        diaSetColor(diaColorsConfig, UICFG_TITLECOL, gDefaultTitleColor);
        diaSetColor(diaColorsConfig, UICFG_TXTCOL, gDefaultTextColor);
        diaSetColor(diaColorsConfig, UICFG_SELCOL, gDefaultSelTextColor);
        diaSetColor(diaColorsConfig, UICFG_PLASCOL, gDefaultPlasBlendColor);
    } else {
        // Display the current theme's colours (read-only).
        diaSetColor(diaColorsConfig, UICFG_BGCOL, gTheme->bgColor);
        diaSetColor(diaColorsConfig, UICFG_PLASCOL, gTheme->plasBlendColor); // raw uchar[3] like bgColor -- NOT the U64 form
        diaSetU64Color(diaColorsConfig, UICFG_UICOL, gTheme->uiTextColor);
        diaSetU64Color(diaColorsConfig, UICFG_TITLECOL, gTheme->titleColor);
        diaSetU64Color(diaColorsConfig, UICFG_TXTCOL, gTheme->textColor);
        diaSetU64Color(diaColorsConfig, UICFG_SELCOL, gTheme->selTextColor);
    }
    diaSetEnabled(diaColorsConfig, UICFG_BGCOL, editable);
    diaSetEnabled(diaColorsConfig, UICFG_UICOL, editable);
    diaSetEnabled(diaColorsConfig, UICFG_TITLECOL, editable);
    diaSetEnabled(diaColorsConfig, UICFG_TXTCOL, editable);
    diaSetEnabled(diaColorsConfig, UICFG_SELCOL, editable);
    diaSetEnabled(diaColorsConfig, UICFG_PLASCOL, editable);
    diaSetEnabled(diaColorsConfig, UICFG_RESETCOL, editable);

    int ret = diaExecuteDialog(diaColorsConfig, -1, 1, NULL);
    if (ret) {
        if (editable) {
            diaGetColor(diaColorsConfig, UICFG_BGCOL, gDefaultBgColor);
            diaGetColor(diaColorsConfig, UICFG_UICOL, gDefaultUITextColor);
            diaGetColor(diaColorsConfig, UICFG_TITLECOL, gDefaultTitleColor);
            diaGetColor(diaColorsConfig, UICFG_TXTCOL, gDefaultTextColor);
            diaGetColor(diaColorsConfig, UICFG_SELCOL, gDefaultSelTextColor);
            diaGetColor(diaColorsConfig, UICFG_PLASCOL, gDefaultPlasBlendColor);
        }
        if (ret == UICFG_RESETCOL)
            setDefaultColors();

        applyConfig(themeID, -1, 1);
    }
}

static int guiGameIdModeFromGlobals(void)
{
    if (gApplyGameID)
        return gPopstarterRetroGemGameID ? 0 : 2; // All / PS2 only
    return gPopstarterRetroGemGameID ? 1 : 3;     // POPSTARTER only / Off
}

static void guiApplyGameIdMode(int mode)
{
    gApplyGameID = mode == 0 || mode == 2;
    gPopstarterRetroGemGameID = mode == 0 || mode == 1;
}

static void guiShowCoverflowConfig(void)
{
    int value;
    int i;

    // Map index<->stored value: scale 0/15/30/45 is linear (idx*15); anim 0/100/200/400 is a lookup.
    static const int animValues[] = {0, 100, 200, 400};

    const char *centerScales[] = {_l(_STR_NONE), _l(_STR_SMALL), _l(_STR_MEDIUM), _l(_STR_LARGE), NULL};
    const char *animSpeeds[] = {_l(_STR_OFF), _l(_STR_FAST), _l(_STR_NORMAL), _l(_STR_SLOW), NULL};

    diaSetEnum(diaCoverflowConfig, COVERFLOW_CFG_SCALE, centerScales);
    diaSetEnum(diaCoverflowConfig, COVERFLOW_CFG_ANIM, animSpeeds);

    int scaleIdx = gCoverflowCenterScale / 15;
    if (scaleIdx < 0)
        scaleIdx = 0;
    else if (scaleIdx > 3)
        scaleIdx = 3;
    diaSetInt(diaCoverflowConfig, COVERFLOW_CFG_SCALE, scaleIdx);

    // default to Normal (200ms) if the stored value isn't one of the table entries
    int animIdx = 2;
    for (i = 0; i < 4; i++)
        if (animValues[i] == gCoverflowAnimSpeed)
            animIdx = i;
    diaSetInt(diaCoverflowConfig, COVERFLOW_CFG_ANIM, animIdx);

    diaSetInt(diaCoverflowConfig, COVERFLOW_CFG_DIM, gCoverflowDimCovers ? 1 : 0);

    // Grid Layout (RIPPS2 build 91, akilluminati47): the theme's, or one of RIPPS2's pages of covers.
    char large[48], compact[48];
    snprintf(large, sizeof(large), "3 x 2 %s", _l(_STR_LARGE));
    snprintf(compact, sizeof(compact), "6 x 3 %s", _l(_STR_GRID_COMPACT));
    const char *gridLayouts[] = {_l(_STR_THEME), "4 x 2", "5 x 2", large, compact, NULL};
    diaSetEnum(diaCoverflowConfig, COVERFLOW_CFG_GRID_LAYOUT, gridLayouts);
    diaSetInt(diaCoverflowConfig, COVERFLOW_CFG_GRID_LAYOUT, gGridLayout);
    int keepLayout = gGridLayout;

    int result = diaExecuteDialog(diaCoverflowConfig, -1, 1, NULL);
    if (result) {
        if (diaGetInt(diaCoverflowConfig, COVERFLOW_CFG_SCALE, &value))
            gCoverflowCenterScale = ((value >= 0 && value <= 3) ? value : 0) * 15;
        if (diaGetInt(diaCoverflowConfig, COVERFLOW_CFG_ANIM, &value))
            gCoverflowAnimSpeed = animValues[(value >= 0 && value <= 3) ? value : 2];
        if (diaGetInt(diaCoverflowConfig, COVERFLOW_CFG_DIM, &value))
            gCoverflowDimCovers = value ? 1 : 0;
        if (diaGetInt(diaCoverflowConfig, COVERFLOW_CFG_GRID_LAYOUT, &value))
            gGridLayout = (value >= 0 && value <= GRID_LAYOUTS) ? value : 0;
        // A new layout changes how many covers a page holds, and so the cover cache: the theme loads again.
        if (gGridLayout != keepLayout && gTheme != NULL && gTheme->grid != NULL && !thmSetGuiValue(thmGetGuiValue(), 1))
            gGridLayout = keepLayout; // abandoned load: the old grid is still on screen, keep the setting with it
    }
}

int guiDrawBGSettings(void)
{
    GSTEXTURE *bg = thmGetTexture(SETTINGS_BG);
    if (bg) {
        rmDrawPixmap(bg, 0, 0, ALIGN_NONE, gGuiScreenWidth, gGuiScreenHeight, SCALING_NONE, gDefaultCol, 0);
        return 1;
    }
    return 0;
}

static void guiSetAudioSettingsState(void)
{
    diaGetInt(diaAudioConfig, CFG_SFX, &gEnableSFX);
    diaGetInt(diaAudioConfig, CFG_BOOT_SND, &gEnableBootSND);
    diaGetInt(diaAudioConfig, CFG_BGM, &gEnableBGM);
    diaGetInt(diaAudioConfig, CFG_SFX_VOLUME, &gSFXVolume);
    diaGetInt(diaAudioConfig, CFG_BOOT_SND_VOLUME, &gBootSndVolume);
    diaGetInt(diaAudioConfig, CFG_BGM_VOLUME, &gBGMVolume);
    diaGetString(diaAudioConfig, CFG_DEFAULT_BGM_PATH, gDefaultBGMPath, sizeof(gDefaultBGMPath));
    audioSetVolume();

    if (gEnableBGM && !isBgmPlaying())
        bgmStart();
}

static int guiAudioUpdater(int modified)
{
    if (modified) {
        gSettingsShell.markSavePending();
        guiSetAudioSettingsState();
    }

    return 0;
}

static int guiShowAudioConfig(void)
{
    diaSetInt(diaAudioConfig, CFG_SFX, gEnableSFX);
    diaSetInt(diaAudioConfig, CFG_BOOT_SND, gEnableBootSND);
    diaSetInt(diaAudioConfig, CFG_BGM, gEnableBGM);
    diaSetInt(diaAudioConfig, CFG_SFX_VOLUME, gSFXVolume);
    diaSetInt(diaAudioConfig, CFG_BOOT_SND_VOLUME, gBootSndVolume);
    diaSetInt(diaAudioConfig, CFG_BGM_VOLUME, gBGMVolume);
    diaSetString(diaAudioConfig, CFG_DEFAULT_BGM_PATH, gDefaultBGMPath);
    diaSetShowDefaultWhenEmpty(diaAudioConfig, CFG_DEFAULT_BGM_PATH, 1); // blank -> the theme's own bgm

    gSettingsShell.beginDialog(diaAudioConfig);
    int result = diaExecuteDialog(diaAudioConfig, -1, 1, guiAudioUpdater);
    gSettingsShell.endDialog();
    return gSettingsShell.pageResult(result);
}

static int guiShowControllerConfig(void)
{
    int value;

    // configure the enumerations
    const char *scrollSpeeds[] = {_l(_STR_SLOW), _l(_STR_MEDIUM), _l(_STR_FAST), NULL};
    const char *selectButtons[] = {_l(_STR_CIRCLE), _l(_STR_CROSS), NULL};
    const char *sensitivity[] = {_l(_STR_OFF), _l(_STR_LOW), _l(_STR_MEDIUM), _l(_STR_HIGH), NULL};

    diaSetEnum(diaControllerConfig, UICFG_SCROLL, scrollSpeeds);
    diaSetEnum(diaControllerConfig, CFG_SELECTBUTTON, selectButtons);
    diaSetEnum(diaControllerConfig, CFG_XSENSITIVITY, sensitivity);
    diaSetEnum(diaControllerConfig, CFG_YSENSITIVITY, sensitivity);

    diaSetInt(diaControllerConfig, UICFG_SCROLL, gScrollSpeed);
    diaSetInt(diaControllerConfig, CFG_SELECTBUTTON, gSelectButton == KEY_CIRCLE ? 0 : 1);
    diaSetInt(diaControllerConfig, CFG_XSENSITIVITY, gXSensitivity);
    diaSetInt(diaControllerConfig, CFG_YSENSITIVITY, gYSensitivity);
    diaSetInt(diaControllerConfig, CFG_RUMBLE, gEnableRumble);

    gSettingsShell.beginDialog(diaControllerConfig);
    int result;
reshow_controller:
    result = diaExecuteDialog(diaControllerConfig, -1, 1, NULL);
    if (result) {
        diaGetInt(diaControllerConfig, UICFG_SCROLL, &gScrollSpeed);
        diaGetInt(diaControllerConfig, CFG_XSENSITIVITY, &gXSensitivity);
        diaGetInt(diaControllerConfig, CFG_YSENSITIVITY, &gYSensitivity);
        diaGetInt(diaControllerConfig, CFG_RUMBLE, &gEnableRumble);
        if (!gEnableRumble)
            padRumbleFlush(); // turned off mid-pulse -> do not leave a motor running

        if (diaGetInt(diaControllerConfig, CFG_SELECTBUTTON, &value))
            gSelectButton = value == 0 ? KEY_CIRCLE : KEY_CROSS;
        else
            gSelectButton = KEY_CIRCLE;
#ifdef PADEMU
        if (result == PADEMU_GLOBAL_BUTTON) {
            guiGameShowPadEmuConfig(1);
            goto reshow_controller;
        } else if (result == PADMACRO_GLOBAL_BUTTON) {
            guiGameShowPadMacroConfig(1);
            goto reshow_controller;
        }
#endif
        applyConfig(-1, -1, 1);
    }

    gSettingsShell.endDialog();
    return gSettingsShell.pageResult(result);
}

static int guiSettingsSkipID(int id, const int *skipIDs, int skipCount)
{
    int i;

    for (i = 0; i < skipCount; i++) {
        if (id == skipIDs[i])
            return 1;
    }
    return 0;
}

struct UIItem *SettingsShell::compose(const struct UIItem *const *parts, int partCount,
                                      const int *skipIDs, int skipCount, int skipPart,
                                      int suppressSecondaryHeaders)
{
    return composeInto(dialog_, parts, partCount, skipIDs, skipCount, skipPart,
                       suppressSecondaryHeaders);
}

struct UIItem *SettingsShell::composeInto(struct UIItem *dialog, const struct UIItem *const *parts,
                                          int partCount, const int *skipIDs, int skipCount, int skipPart,
                                          int suppressSecondaryHeaders)
{
    int part, i, skipTrailingBreak, skipSecondarySplitter;
    int count = 0;

    for (part = 0; part < partCount; part++) {
        skipTrailingBreak = 0;
        skipSecondarySplitter = 0;
        for (i = 0; parts[part][i].type != UI_TERMINATOR; i++) {
            const struct UIItem *item = &parts[part][i];

            // Each source definition owns its own OK row. A composite screen has one shared
            // commit point, while feature buttons and section headings remain intact.
            if (item->type == UI_OK) {
                // The source dialog also has a trailing UI_BREAK after its OK. That break is useful
                // when the source is shown alone, but becomes an empty row in a composite page.
                skipTrailingBreak = 1;
                continue;
            }
            if (skipTrailingBreak) {
                skipTrailingBreak = 0;
                if (item->type == UI_BREAK)
                    continue;
            }
            if ((skipPart < 0 || part == skipPart) && guiSettingsSkipID(item->id, skipIDs, skipCount)) {
                // Some legacy rows are represented as LABEL + SPACER + CONTROL. When the control
                // is omitted from a composed peer page, remove that now-orphaned label as well;
                // otherwise the page shows an empty heading before the replacement inline block.
                if (item->type != UI_LABEL && item->type != UI_SPACER && count >= 2 &&
                    dialog[count - 1].type == UI_SPACER &&
                    dialog[count - 2].type == UI_LABEL)
                    count -= 2;
                // A skipped control owns the following break in the source dialog. Once it is
                // omitted from a composite page, that break would become an empty
                // row with no visual or navigation purpose.
                if (item->type != UI_LABEL && item->type != UI_SPACER)
                    skipTrailingBreak = 1;
                continue;
            }
            if (suppressSecondaryHeaders && part > 0 && item->type == UI_HEADER) {
                // The peer page title already supplies the context. Keep the fields and actions,
                // but do not repeat each chained dialog's title as a second hierarchy level.
                skipSecondarySplitter = 1;
                continue;
            }
            if (skipSecondarySplitter && item->type == UI_SPLITTER) {
                skipSecondarySplitter = 0;
                if (count == 0 || dialog[count - 1].type == UI_BREAK)
                    continue;
                dialog[count++] = (struct UIItem) {UI_BREAK};
                continue;
            }
            // Secondary settings sections already have their own colored heading. Keep the heading
            // on its own line, but omit the source dialog's redundant separator line when it is
            // composed into the peer page.
            if (part > 0 && item->type == UI_SPLITTER && count > 0 && dialog[count - 1].type == UI_HEADER) {
                if (count >= SETTINGS_DIALOG_CAPACITY - 3) {
                    LOG("GUI Settings: composed page exceeds SETTINGS_DIALOG_CAPACITY (%d)\n",
                        SETTINGS_DIALOG_CAPACITY);
                    return NULL;
                }
                dialog[count++] = (struct UIItem) {UI_BREAK};
                continue;
            }
            if (count >= SETTINGS_DIALOG_CAPACITY - 3) {
                LOG("GUI Settings: composed page exceeds SETTINGS_DIALOG_CAPACITY (%d)\n",
                    SETTINGS_DIALOG_CAPACITY);
                return NULL;
            }
            dialog[count++] = *item;
        }
    }

    dialog[count++] = (struct UIItem) {UI_OK, 0, 1, 1, -1, 0, 0, {.label = {NULL, _STR_OK}}};
    dialog[count] = (struct UIItem) {UI_TERMINATOR};
    activeDialog_ = dialog;
    return dialog;
}

int SettingsShell::isPeerResult(int result)
{
    return result == DIA_RESULT_PREV || result == DIA_RESULT_NEXT;
}

int SettingsShell::isShellResult(int result)
{
    return isPeerResult(result) || result == DIA_RESULT_INDEX;
}

// Back / L1 / R1 off a page whose rows the user did not change. Such an exit applies nothing, as
// Back did before #793 made it keep edits. Since then every Back ran the page's apply block, and on
// Game Sources, General, Launch, PS1 and Network that block is a full device re-apply
// (applyConfig(..., 0), plus bdmForceDeviceRefresh on Game Sources) -- so merely opening Settings,
// entering a page and backing out re-applied every source and left the USB page blank (#806,
// zackcage6, Beta-3338). With nothing changed there is nothing to apply; an explicit OK or an edited
// row still applies exactly as before.
//
// A long-path row (IGR/Exit path on General, Neutrino path on Launch, POPSTARTER path on PS1) shows
// only a 31-character preview; the value lives in a full-size edit buffer that THAT page seeds from
// its global when it opens, so an edit past the preview is caught by comparing the page's own buffer.
// Only its own: the other pages' buffers stay empty until their page first opens, and comparing them
// would make every other page look edited (pathEdit/pathValue are NULL on pages without such a row).
int SettingsShell::leftUntouched(struct UIItem *ui, int result, const char *pathEdit, const char *pathValue)
{
    if (!isShellResult(result) || diaHasChanges(ui))
        return 0;

    return pathEdit == NULL || strcmp(pathEdit, pathValue) == 0;
}

int SettingsShell::pageResult(int result)
{
    // L1/R1 peer paging and an explicit Index request pass straight back to the shell loop.
    if (isShellResult(result)) {
        savePending_ = 1;
        return result;
    }

    /* EVERY other way out of a page finishes at the Settings Index -- never at the main menu.
       Only showIndex leaves Settings, and only after offering to save.

       The old test recognised the two generic button ids and nothing else, but pages carry their
       OWN button ids: the Network page's confirm button is NETCFG_OK, not UIID_BTN_OK. That fell
       through to 0, which guiShowSettings reads as "leave Settings entirely" -- so pressing OK on
       the Network page dumped the user at the main start menu, skipping the save prompt, with
       every edit still pending. Reconnect (NETCFG_RECONNECT) did the same.

       Fixing the one id would have left the trap armed for the next page that adds a button, so
       this fails safe instead: unless the page asked for a specific shell move, we stay inside.
       Cancel is the only result that does not arm the save prompt. */
    if (result != UIID_BTN_CANCEL)
        savePending_ = 1;

    return DIA_RESULT_INDEX;
}

enum gui_settings_page {
    SETTINGS_SOURCES = 0,
    SETTINGS_GENERAL,
    SETTINGS_LANGUAGE,
    SETTINGS_NETWORK,
    SETTINGS_INTERFACE,
    SETTINGS_LAUNCH,
    SETTINGS_POPSTARTER,
    SETTINGS_CONTROLLERS,
    SETTINGS_AUDIO,
    SETTINGS_PAGE_COUNT
};

void SettingsShell::beginDialog(struct UIItem *ui)
{
    if (!shellActive_)
        return;

    snprintf(pageIndicator_, sizeof(pageIndicator_), "%d/%d", currentPage_ + 1, (int)SETTINGS_PAGE_COUNT);
    diaSetSettingsShell(ui, pageIndicator_);
    diaSetSettingsContext(1);
}

void SettingsShell::endDialog()
{
    diaSetSettingsShell(NULL, NULL);
    diaSetSettingsContext(0);
}

static int guiSettingsGeneralUpdater(int modified)
{
    int showAutoStartLast;
    struct UIItem *activeDialog = gSettingsShell.activeDialog();

    if (modified) {
        diaGetInt(activeDialog, CFG_LASTPLAYED, &showAutoStartLast);
        diaSetVisible(activeDialog, CFG_LBL_AUTOSTARTLAST, showAutoStartLast);
        diaSetVisible(activeDialog, CFG_AUTOSTARTLAST, showAutoStartLast);
    }
    return 0;
}

static int guiSettingsShowGeneral(void)
{
    const struct UIItem *parts[] = {diaConfig, diaSecurityConfig, diaAdvancedConfig};
    struct UIItem *ui = gSettingsShell.compose(parts, 3, NULL, 0, -1, 1);
    int result;

    if (ui == NULL)
        return 0;

    guiExitPathBegin(ui);
    diaSetShowDefaultWhenEmpty(ui, CFG_CUSTOMCFGPATH, 1);
    diaSetString(ui, CFG_CUSTOMCFGPATH, gCustomSettingsPath);
    diaSetInt(ui, CFG_LASTPLAYED, gRememberLastPlayed);
    diaSetInt(ui, CFG_FOLDERNAV, gEnableFolderNav);
    diaSetInt(ui, CFG_AUTOSTARTLAST, gAutoStartLastPlayed);
    diaSetVisible(ui, CFG_AUTOSTARTLAST, gRememberLastPlayed);
    diaSetVisible(ui, CFG_LBL_AUTOSTARTLAST, gRememberLastPlayed);
    diaSetInt(ui, CFG_ENWRITEOP, gEnableWrite);
    diaSetInt(ui, CFG_DEBUG, gEnableDebug);
    guiSetParentalLockValue(ui);
    guiSetAdvancedSettings(ui);
    gSettingsShell.beginDialog(ui);

reshow_general:
    result = diaExecuteDialog(ui, -1, 1, &guiSettingsGeneralUpdater);
    if (result == GENERAL_GSM_DEFAULTS_BUTTON) {
        if (guiGameShowGSConfig(1))
            gSettingsShell.markSavePending();
        goto reshow_general;
    }

    if (result != UIID_BTN_CANCEL && result != -1 && !gSettingsShell.leftUntouched(ui, result, exitPathEdit, gExitPath)) {
        snprintf(gExitPath, sizeof(gExitPath), "%s", exitPathEdit);
        diaGetString(ui, CFG_CUSTOMCFGPATH, gCustomSettingsPath, sizeof(gCustomSettingsPath));
        diaGetInt(ui, CFG_LASTPLAYED, &gRememberLastPlayed);
        diaGetInt(ui, CFG_FOLDERNAV, &gEnableFolderNav);
        diaGetInt(ui, CFG_AUTOSTARTLAST, &gAutoStartLastPlayed);
        diaGetInt(ui, CFG_ENWRITEOP, &gEnableWrite);
        diaGetInt(ui, CFG_DEBUG, &gEnableDebug);
        guiSaveParentalLockValue(ui);
        guiSaveAdvancedSettings(ui);

        DisableCron = 1;
        applyConfig(-1, -1, 0);
        menuReinitMainMenu();
    }

    gSettingsShell.endDialog();
    gSettingsShell.setActiveDialog(NULL);
    return gSettingsShell.pageResult(result);
}

// The one menu-language picker, plus the game-facing OSD defaults (original #1043).
static int guiSettingsShowLanguage(void)
{
    const struct UIItem *parts[] = {diaLanguageConfig};
    struct UIItem *ui = gSettingsShell.compose(parts, 1, NULL, 0, -1, 1);
    const char **langNamesSnap;
    int result, langID;

    if (ui == NULL)
        return DIA_RESULT_INDEX;

    // Discovery can rebuild the live list while a dialog is open. Own the names until exit.
    guiLock();
    langNamesSnap = guiCopyNameList((const char **)lngGetGuiList());
    guiUnlock();
    if (langNamesSnap == NULL) {
        gSettingsShell.setActiveDialog(NULL);
        return DIA_RESULT_INDEX;
    }
    diaSetEnum(ui, UICFG_LANG, langNamesSnap);
    diaSetInt(ui, UICFG_LANG, lngGetGuiValue());
    gSettingsShell.beginDialog(ui);

reshow_language:
    result = diaExecuteDialog(ui, -1, 1, NULL);
    if (result == LAUNCH_OSD_DEFAULTS_BUTTON) {
        guiGameShowOSDLanguageConfig(1);
        gSettingsShell.markSavePending();
        goto reshow_language;
    }

    if (result != UIID_BTN_CANCEL && result != -1) {
        diaGetInt(ui, UICFG_LANG, &langID);
        if (langID != lngGetGuiValue()) {
            applyConfig(-1, langID, 1);
            menuReinitMainMenu();
        }
    }

    guiFreeNameList(langNamesSnap);
    gSettingsShell.endDialog();
    gSettingsShell.setActiveDialog(NULL);
    return gSettingsShell.pageResult(result);
}


static int guiSettingsSourcesUpdater(int modified)
{
    struct UIItem *activeDialog = gSettingsShell.activeDialog();

    if (modified && activeDialog != NULL)
        guiSourcesNetRowsUpdate(activeDialog);
    return 0;
}

static int guiSettingsShowSources(void)
{
    const struct UIItem *parts[] = {diaDeviceConfig};
    const char *deviceNames[] = {_l(_STR_BDM_GAMES), _l(_STR_MMCE), _l(_STR_HDD_GAMES), _l(_STR_NET_GAMES), _l(_STR_APPS), _l(_STR_FAV), NULL};
    const char *deviceModes[] = {_l(_STR_OFF), _l(_STR_MANUAL), _l(_STR_AUTO), NULL};
    struct UIItem *ui = gSettingsShell.compose(parts, 1, NULL, 0, -1, 0);
    int deviceModeIndex;
    int result;

    if (ui == NULL)
        return 0;

    diaSetEnum(ui, CFG_DEFDEVICE, deviceNames);
    diaSetEnum(ui, CFG_BDMMODE, deviceModes);
    diaSetEnum(ui, CFG_HDDMODE, deviceModes);
    diaSetEnum(ui, CFG_APPMODE, deviceModes);
    diaSetEnum(ui, CFG_FAVMODE, deviceModes);
    deviceModeIndex = guiIoModeToDeviceType(gDefaultDevice);
    diaSetInt(ui, CFG_DEFDEVICE, deviceModeIndex);
    diaSetInt(ui, CFG_BDMMODE, gBDMStartMode);
    diaSetInt(ui, CFG_HDDMODE, gHDDStartMode);
    diaSetInt(ui, CFG_APPMODE, gAPPStartMode);
    diaSetInt(ui, CFG_FAVMODE, gFAVStartMode);
    diaSetInt(ui, CFG_ALLGAMES, gAllGames);
    diaSetInt(ui, CFG_SHOWHIDDEN, gShowHidden);
    diaSetInt(ui, CFG_ENABLEUSB, gEnableUSB);
    diaSetInt(ui, CFG_ENABLEILK, gEnableILK);
    diaSetInt(ui, CFG_ENABLEMX4SIO, gEnableMX4SIO);
    diaSetInt(ui, CFG_ENABLEPCCF, gEnablePCCF);
    diaSetInt(ui, CFG_ENABLEDVR, gEnableDVR);
    diaSetInt(ui, CFG_ENABLEBDMHDD, gEnableBdmHDD);
    diaSetEnabled(ui, CFG_ENABLEBDMHDD, 1);
    diaSetEnabled(ui, CFG_HDDMODE, 1);
    guiSourcesNetRowsBegin(ui, deviceModes);
    diaSetEnum(ui, CFG_MMCEMODE, deviceModes);
    diaSetInt(ui, CFG_MMCEMODE, gMMCEStartMode);
    diaSetEnabled(ui, CFG_MMCEMODE, 1);
    gSettingsShell.beginDialog(ui);

reshow_sources:
    result = diaExecuteDialog(ui, -1, 1, &guiSettingsSourcesUpdater);
    if (result == MMCE_SETTINGS_BUTTON) {
        // MMCE uses a separate composition buffer. Confirming the child editor finishes the
        // Settings page and returns to the hub; Circle resumes Game Sources as the parent page.
        int mmceResult = guiShowMmceConfig();
        if (mmceResult != UIID_BTN_OK)
            goto reshow_sources;

        // Treat MMCE confirmation as confirmation of its parent page too. This preserves any
        // Game Sources edits made before opening MMCE, then the normal page-result mapping returns
        // to the Settings Index and marks the session as needing persistence.
        result = UIID_BTN_OK;
    }

    if (result != UIID_BTN_CANCEL && result != -1 && !gSettingsShell.leftUntouched(ui, result, NULL, NULL)) {
        diaGetInt(ui, CFG_DEFDEVICE, &deviceModeIndex);
        gDefaultDevice = guiDeviceTypeToIoMode(deviceModeIndex);
        diaGetInt(ui, CFG_BDMMODE, &gBDMStartMode);
        diaGetInt(ui, CFG_HDDMODE, &gHDDStartMode);
        diaGetInt(ui, CFG_APPMODE, &gAPPStartMode);
        diaGetInt(ui, CFG_MMCEMODE, &gMMCEStartMode);
        diaGetInt(ui, CFG_FAVMODE, &gFAVStartMode);
        int previousAllGames = gAllGames, previousShowHidden = gShowHidden;
        diaGetInt(ui, CFG_ALLGAMES, &gAllGames);
        diaGetInt(ui, CFG_SHOWHIDDEN, &gShowHidden);
        diaGetInt(ui, CFG_ENABLEUSB, &gEnableUSB);
        diaGetInt(ui, CFG_ENABLEILK, &gEnableILK);
        diaGetInt(ui, CFG_ENABLEMX4SIO, &gEnableMX4SIO);
        int oldEnablePCCF = gEnablePCCF;
        diaGetInt(ui, CFG_ENABLEPCCF, &gEnablePCCF);
        if (oldEnablePCCF != gEnablePCCF)
            bdmRequestPccfProbe();
        diaGetInt(ui, CFG_ENABLEDVR, &gEnableDVR);
        diaGetInt(ui, CFG_ENABLEBDMHDD, &gEnableBdmHDD);
        guiSourcesNetRowsRead(ui);

        bdmForceDeviceRefresh();
        applyConfig(-1, -1, 0);
        menuReinitMainMenu();
        if (previousAllGames != gAllGames)
            guiApplyAllGamesChange();
        else if (previousShowHidden != gShowHidden)
            guiRefreshCfgTitles(); // rebuilds every game list, Favourites last
        else if (gAllGames)
            loadFavourites(); // a source switched Off or on: All Games drops or gathers its rows
    }

    gSettingsShell.endDialog();
    gSettingsShell.setActiveDialog(NULL);
    return gSettingsShell.pageResult(result);
}

static int guiSettingsDisplayUpdater(int modified)
{
    int temp, x, y;
    struct UIItem *activeDialog = gSettingsShell.activeDialog();

    if (!modified)
        return 0;

    gSettingsShell.markSavePending();

    diaGetInt(activeDialog, UICFG_XOFF, &x);
    diaGetInt(activeDialog, UICFG_YOFF, &y);
    if (x != gXOff || y != gYOff) {
        gXOff = x;
        gYOff = y;
        rmSetDisplayOffset(x, y);
    }
    diaGetInt(activeDialog, UICFG_OVERSCAN, &temp);
    if (temp != gOverscan) {
        gOverscan = temp;
        rmSetOverscan(gOverscan);
        guiUpdateScreenScale();
    }
    diaGetInt(activeDialog, UICFG_WIDESCREEN, &temp);
    if (temp != gWideScreen) {
        gWideScreen = temp;
        rmSetAspectRatio(rmIsWideDisplay() ? RM_ARATIO_16_9 : RM_ARATIO_4_3);
        guiUpdateScreenScale();
    }
    diaGetInt(activeDialog, UICFG_FONT_SIZE, &temp);
    if (temp != gFontSize) {
        gFontSize = temp;
        guiUpdateScreenScale();
    }
    if (diaGetInt(activeDialog, UICFG_GLYPH_SET, &temp))
        gGlyphSet = temp; // the next frame swaps the glyphs (thmGlyphSetRefresh)
    return 0;
}

static int guiSettingsShowInterface(void)
{
    const struct UIItem *parts[] = {diaUIConfig, diaDisplayConfig};
    const int skipIDs[] = {UICFG_VMODE};
    const char *vmodeNames[] = {_l(_STR_AUTO), "PAL 640x512i @50Hz 24bit", "NTSC 640x448i @60Hz 24bit",
                                "EDTV 640x448p @60Hz 24bit", "EDTV 640x512p @50Hz 24bit", "VGA 640x480p @60Hz 24bit",
                                "PAL 704x576i @50Hz 24bit (HIRES)", "NTSC 704x480i @60Hz 24bit (HIRES)",
                                "EDTV 704x480p @60Hz 24bit (HIRES)", "EDTV 704x576p @50Hz 24bit (HIRES)",
                                "HDTV 1280x720p @60Hz 16bit (HIRES)", "HDTV 1920x1080i @60Hz 16bit (HIRES)",
                                "PAL 640x256p @50Hz 24bit", "NTSC 640x224p @60Hz 24bit",
                                "PAL 640x512i @50Hz 24bit (FLICKER-FREE)", "NTSC 640x448i @60Hz 24bit (FLICKER-FREE)", NULL};
    // Video Mode stays in diaDisplayConfig for the legacy standalone Display editor, but the
    // composed Interface page supplies its one copy at the top of diaUIConfig.
    struct UIItem *ui = gSettingsShell.compose(parts, 2, skipIDs, 1, 1, 1);
    const char **themeNamesSnap = NULL;
    int themeID, previousTheme, previousVMode, result;
    int gameViewChanged = 0;
    int appsViewChanged = 0;
    int themeViewRequest = gThemeViewRequest;
    int allGamesChanged = 0;

    if (ui == NULL)
        return 0;

    showCfgPopup = 0;
    guiResetNotifications();
    previousTheme = thmGetGuiValue();
    previousVMode = gVMode;
    int openX = gXOff, openY = gYOff, openOver = gOverscan; // the position kept for each video mode

    guiLock();
    themeNamesSnap = guiCopyNameList((const char **)thmGetGuiList());
    guiUnlock();
    diaSetEnum(ui, UICFG_THEME, themeNamesSnap != NULL ? themeNamesSnap : (const char **)thmGetGuiList());
    guiSetGameViewPicker(ui);
    guiSetAppsViewPicker(ui);
    guiSetThemeViewPicker(ui);
    guiSetLibraryPicker(ui);
    diaSetEnum(ui, UICFG_VMODE, vmodeNames);
    diaSetInt(ui, UICFG_THEME, thmGetGuiValue());
    const char *sortNames[] = {_l(_STR_OFF), _l(_STR_SORT_AZ), _l(_STR_SORT_LAST_PLAYED), NULL};
    diaSetEnum(ui, UICFG_AUTOSORT, sortNames);
    diaSetInt(ui, UICFG_AUTOSORT, gAutosort);
    diaSetInt(ui, UICFG_AUTOREFRESH, gAutoRefresh);
    diaSetInt(ui, UICFG_NOTIFICATIONS, gEnableNotifications);
    diaSetInt(ui, UICFG_CFG_TITLES, gCfgTitlesInLists);
    diaSetInt(ui, UICFG_GAME_TYPE_LABELS, gGameTypeLabels);
    // Keep the editor reachable even when the current theme has no active Coverflow view; users
    // need to be able to configure it before enabling or switching to a Coverflow-capable theme.
    diaSetVisible(ui, UICFG_COVERFLOW_BUTTON, 1);
    diaSetInt(ui, UICFG_VMODE, gVMode);
    diaSetInt(ui, UICFG_WIDESCREEN, gWideScreen);
    const char *fontSizeNames[] = {_l(_STR_SMALL), _l(_STR_DEFAULT), _l(_STR_LARGE), NULL};
    diaSetEnum(ui, UICFG_FONT_SIZE, fontSizeNames);
    diaSetInt(ui, UICFG_FONT_SIZE, gFontSize);
    const char *glyphSetNames[] = {_l(_STR_THEME), _l(_STR_GLYPHS_BUILTIN), _l(_STR_GLYPHS_STANDARD), _l(_STR_GLYPHS_WHITE), NULL};
    diaSetEnum(ui, UICFG_GLYPH_SET, glyphSetNames);
    diaSetInt(ui, UICFG_GLYPH_SET, gGlyphSet);
    diaSetInt(ui, UICFG_XOFF, gXOff);
    diaSetInt(ui, UICFG_YOFF, gYOff);
    diaSetInt(ui, UICFG_OVERSCAN, gOverscan);
    const char *gameIDModes[] = {_l(_STR_GAMEID_MODE_ALL), _l(_STR_GAMEID_MODE_POPSTARTER),
                                 _l(_STR_GAMEID_MODE_PS2), _l(_STR_GAMEID_MODE_OFF), NULL};
    diaSetEnum(ui, CFG_APPLYGAMEID, gameIDModes);
    diaSetInt(ui, CFG_APPLYGAMEID, guiGameIdModeFromGlobals());
    gSettingsShell.beginDialog(ui);
reshow_interface:
    result = diaExecuteDialog(ui, -1, 1, &guiSettingsDisplayUpdater);
    if (result == UICFG_ARTWORK_BUTTON) {
        guiShowArtworkConfig();
        goto reshow_interface;
    }
    if (result == UICFG_COVERFLOW_BUTTON) {
        guiShowCoverflowConfig();
        goto reshow_interface;
    }
    if (result == UICFG_COLORS_BUTTON) {
        guiShowColorsConfig();
        goto reshow_interface;
    }
    if (result == UICFG_GAME_LIST_BUTTON) {
        guiShowVcdListConfig();
        goto reshow_interface;
    }
    if (result == DISPLAY_GSM_DEFAULTS_BUTTON) {
        if (guiGameShowGSConfig(1))
            gSettingsShell.markSavePending();
        goto reshow_interface;
    }

    padRumbleFlush();
    if (result != UIID_BTN_CANCEL && result != -1) {
        diaGetInt(ui, UICFG_THEME, &themeID);
        int previousSort = gAutosort;
        diaGetInt(ui, UICFG_AUTOSORT, &gAutosort);
        diaGetInt(ui, UICFG_AUTOREFRESH, &gAutoRefresh);
        diaGetInt(ui, UICFG_NOTIFICATIONS, &gEnableNotifications);
        int previousCfgTitles = gCfgTitlesInLists;
        diaGetInt(ui, UICFG_CFG_TITLES, &gCfgTitlesInLists);
        // A new sort needs the rows rebuilt: Last played ranks are resolved as each row is built.
        int cfgTitlesChanged = previousCfgTitles != gCfgTitlesInLists || previousSort != gAutosort;
        diaGetInt(ui, UICFG_GAME_TYPE_LABELS, &gGameTypeLabels);
        gameViewChanged = guiReadGameViewPicker(ui);
        appsViewChanged = guiReadAppsViewPicker(ui);
        themeViewRequest = guiReadThemeViewPicker(ui);
        allGamesChanged = guiReadLibraryPicker(ui);
        diaGetInt(ui, UICFG_VMODE, &gVMode);
        diaGetInt(ui, UICFG_WIDESCREEN, &gWideScreen);
        diaGetInt(ui, UICFG_FONT_SIZE, &gFontSize);
        diaGetInt(ui, UICFG_GLYPH_SET, &gGlyphSet);
        diaGetInt(ui, UICFG_XOFF, &gXOff);
        diaGetInt(ui, UICFG_YOFF, &gYOff);
        diaGetInt(ui, UICFG_OVERSCAN, &gOverscan);
        screenPosModeChanged(previousVMode, openX, openY, openOver); // before applyConfig shows the mode
        {
            int gameIDMode;
            diaGetInt(ui, CFG_APPLYGAMEID, &gameIDMode);
            guiApplyGameIdMode(gameIDMode);
        }

        if (previousTheme != themeID && isBgmPlaying())
            bgmStop();
        if (previousVMode != gVMode)
            guiFadeCurrentScreenToBlack();
        applyConfig(themeID, -1, 1);
        if (themeViewRequest != gThemeViewRequest)
            thmSetView(themeViewRequest); // reloads only when the theme now shows a different view
        if (allGamesChanged) {
            guiApplyAllGamesChange(); // rebuilds every game list too
        } else if (cfgTitlesChanged) {
            guiRefreshCfgTitles();
        } else if (gameViewChanged) {
            guiRefreshGameViews();
        }
        if (appsViewChanged || (cfgTitlesChanged && previousSort != gAutosort))
            guiRefreshAppsView();
        sfxInit(0);
        if (gEnableBGM && !isBgmPlaying())
            bgmStart();

        if (previousVMode != gVMode && guiConfirmVideoMode() == 0) {
            gVMode = previousVMode;
            gXOff = openX; // and its position with it
            gYOff = openY;
            gOverscan = openOver;
            applyConfig(-1, -1, 1);
        }
    }

    guiFreeNameList(themeNamesSnap);
    gSettingsShell.endDialog();
    gSettingsShell.setActiveDialog(NULL);
    return gSettingsShell.pageResult(result);
}

static int guiSettingsLaunchUpdater(int modified)
{
    int neutrinoVideoDef;
    struct UIItem *activeDialog = gSettingsShell.activeDialog();

    if (modified) {
        diaGetInt(activeDialog, CFG_NEUTRINO_VIDEO, &neutrinoVideoDef);
        diaSetEnabled(activeDialog, CFG_NEUTRINO_GSMCOMP, neutrinoVideoDef != 0);
    }
    return 0;
}

static int guiSettingsShowLaunch(void)
{
    const struct UIItem *parts[] = {diaLaunchConfig, diaNeutrinoDefaults};
    const int skipIDs[] = {LAUNCH_NEUTRINO_DEFAULTS_BUTTON};
    const char *defaultCoreStrs[] = {"<OPL>", "Neutrino", NULL};
    static const char *neutrinoVideoDefStrs[] = {"Off", "240p", "480p", "1080i x1", "1080i x2", "1080i x3", NULL};
    static const char *neutrinoGsmCompDefStrs[] = {"Off", "Type 1 (GSM/OPL)", "Type 2", "Type 3", NULL};
    struct UIItem *ui = gSettingsShell.compose(parts, 2, skipIDs, 1, -1, 1);
    int result;

    if (ui == NULL)
        return 0;

    diaSetEnum(ui, CFG_DEFAULT_CORE, defaultCoreStrs);
    diaSetInt(ui, CFG_DEFAULT_CORE, gDefaultCoreLoader);
    diaSetInt(ui, CFG_AUTO_CREATE_VMC, gAutoCreateVmc);
#ifdef IGR_MENU
    diaSetInt(ui, CFG_IGR_MENU, gIgrMenu);
#endif
    diaSetInt(ui, CFG_PS2LOGO, gPS2Logo);
    guiCorePathBegin(ui, CFG_NEUTRINO_PATH, neutrinoPathEdit, sizeof(neutrinoPathEdit), gNeutrinoPath);
    diaSetEnum(ui, CFG_NEUTRINO_VIDEO, neutrinoVideoDefStrs);
    diaSetInt(ui, CFG_NEUTRINO_VIDEO, gNeutrinoVideoDefault);
    diaSetEnum(ui, CFG_NEUTRINO_GSMCOMP, neutrinoGsmCompDefStrs);
    diaSetInt(ui, CFG_NEUTRINO_GSMCOMP, gNeutrinoGsmCompDefault);
    diaSetEnabled(ui, CFG_NEUTRINO_GSMCOMP, gNeutrinoVideoDefault != 0);
    diaSetInt(ui, CFG_NEUTRINO_ELF_ARG, gNeutrinoElfArg);
    diaSetInt(ui, CFG_NEUTRINO_SHOW_ARGS, gNeutrinoShowArgs);
    gSettingsShell.beginDialog(ui);

reshow_launch:
    result = diaExecuteDialog(ui, -1, 1, &guiSettingsLaunchUpdater);
    if (result == LAUNCH_GSM_DEFAULTS_BUTTON) {
        if (guiGameShowGSConfig(1))
            gSettingsShell.markSavePending();
        goto reshow_launch;
    }
    if (result == CFG_NEUTRINO_ARGS) {
        if (guiShowNeutrinoArgsConfig(gNeutrinoArgs, sizeof(gNeutrinoArgs)))
            gSettingsShell.markSavePending();
        goto reshow_launch;
    }

    if (result != UIID_BTN_CANCEL && result != -1 && !gSettingsShell.leftUntouched(ui, result, neutrinoPathEdit, gNeutrinoPath)) {
        diaGetInt(ui, CFG_DEFAULT_CORE, &gDefaultCoreLoader);
        diaGetInt(ui, CFG_AUTO_CREATE_VMC, &gAutoCreateVmc);
#ifdef IGR_MENU
        diaGetInt(ui, CFG_IGR_MENU, &gIgrMenu);
#endif
        diaGetInt(ui, CFG_PS2LOGO, &gPS2Logo);
        if (strcmp(gNeutrinoPath, neutrinoPathEdit) != 0)
            gNeutrinoDevice = NEUTRINO_DEV_AUTO;
        snprintf(gNeutrinoPath, sizeof(gNeutrinoPath), "%s", neutrinoPathEdit);
        diaGetInt(ui, CFG_NEUTRINO_VIDEO, &gNeutrinoVideoDefault);
        diaGetInt(ui, CFG_NEUTRINO_GSMCOMP, &gNeutrinoGsmCompDefault);
        diaGetInt(ui, CFG_NEUTRINO_ELF_ARG, &gNeutrinoElfArg);
        diaGetInt(ui, CFG_NEUTRINO_SHOW_ARGS, &gNeutrinoShowArgs);
        applyConfig(-1, -1, 0);
        menuReinitMainMenu();
    }

    gSettingsShell.endDialog();
    gSettingsShell.setActiveDialog(NULL);
    return gSettingsShell.pageResult(result);
}

static int guiSettingsPopstarterUpdater(int modified)
{
    guiBdmaUpdater(modified);
    return 0;
}

// An Ember settings row after OK: the row's value if the user changed it, otherwise the setting as it
// was. That is what keeps a setting never changed in RiptOPL UNSET -- showing Default and pressing OK
// must not start managing a key someone may have written into settings.txt by hand.
static int emberSettingFromRow(struct UIItem *ui, int id, int shown, int current)
{
    int picked = shown;

    diaGetInt(ui, id, &picked);
    return (picked != shown) ? picked : current;
}

// What an Ember row shows for a stored setting: UNSET (never changed in RiptOPL) shows as Default.
static int emberSettingShown(int current)
{
    return (current == EMBER_SETTING_UNSET) ? 0 : current;
}

// PS Emulation Settings: PS1-via-POPSTARTER launch config (optional POPSTARTER.ELF full path).
//
// This page COMPOSES a copy of diaVcdConfig + diaBdmaConfig and drives that copy, so a new row
// belongs HERE, against the composed `ui` -- never against either template. A row wired into a
// template renders on the composed page with no enum list and never saves; that happened once
// with CFG_EMBER_DISPLAY.
//
// The BDMA equip, VCD list display options and POPStarter network settings are chained
// sub-dialogs (guiShowBdmaConfig / guiShowVcdListConfig / guiShowPopsNetConfig). CFG ids are
// shared with the pre-shell rows, so saved config values map through unchanged.
static int guiSettingsShowPopstarter(void)
{
    const struct UIItem *parts[] = {diaVcdConfig, diaBdmaConfig};
    const int skipIDs[] = {VCD_BDMA_BUTTON};
    // "480" is 480i to Ember -- it always was; this row used to call it 480p.
    const char *emberDisplayStrs[] = {_l(_STR_DEFAULT), "240p", "480i", "480p", NULL};
    const char *emberTimingStrs[] = {_l(_STR_DEFAULT), _l(_STR_AUTO), "NTSC", "PAL", NULL};
    const char *emberDitherStrs[] = {_l(_STR_DEFAULT), _l(_STR_ON), _l(_STR_OFF), NULL};
    const char *emberShadingStrs[] = {_l(_STR_DEFAULT), "15-bit", _l(_STR_EMBER_SHADING_24), NULL};
    const char *emberControllerStrs[] = {_l(_STR_DEFAULT), _l(_STR_AUTO), "Analog", "D2A", NULL};
    struct UIItem *ui = gSettingsShell.compose(parts, 2, skipIDs, 1, -1, 1);
    // A setting never changed in RiptOPL is UNSET and shows as Default; it only becomes stored when
    // the user actually picks something else (see emberSettingFromRow).
    int emberDisplayShown = emberSettingShown(gEmberDisplay);
    int emberTimingShown = emberSettingShown(gEmberTiming);
    int emberDitherShown = emberSettingShown(gEmberDither);
    int emberShadingShown = emberSettingShown(gEmberShading);
    int emberControllerShown = emberSettingShown(gEmberController);
    int result;

    if (ui == NULL)
        return 0;

    // Ember's display mode. This MUST be set on `ui` -- the composed COPY -- not on the diaVcdConfig
    // template it was built from: the copy is what renders and what diaGetInt reads back. Setting
    // the template instead leaves the row on screen with no enum list and silently discards every
    // change, which is exactly the bug this line replaced.
    diaSetEnum(ui, CFG_EMBER_DISPLAY, emberDisplayStrs);
    diaSetInt(ui, CFG_EMBER_DISPLAY, emberDisplayShown);
    diaSetEnum(ui, CFG_EMBER_TIMING, emberTimingStrs);
    diaSetInt(ui, CFG_EMBER_TIMING, emberTimingShown);
    diaSetEnum(ui, CFG_EMBER_DITHER, emberDitherStrs);
    diaSetInt(ui, CFG_EMBER_DITHER, emberDitherShown);
    diaSetEnum(ui, CFG_EMBER_SHADING, emberShadingStrs);
    diaSetInt(ui, CFG_EMBER_SHADING, emberShadingShown);
    diaSetEnum(ui, CFG_EMBER_CONTROLLER, emberControllerStrs);
    diaSetInt(ui, CFG_EMBER_CONTROLLER, emberControllerShown);
    guiSetGameViewPicker(ui);

    guiCorePathBegin(ui, CFG_POPSTARTER_PATH, popstarterPathEdit, sizeof(popstarterPathEdit), gPopstarterPath);
    diaSetInt(ui, CFG_POPSTARTER_RETROGEM_GAMEID, gPopstarterRetroGemGameID);
    guiSetBdmaSettings(ui);
    gSettingsShell.beginDialog(ui);

reshow_popstarter:
    result = diaExecuteDialog(ui, -1, 1, &guiSettingsPopstarterUpdater);
    if (result == VCD_NET_BUTTON) {
        guiShowPopsNetConfig();
        goto reshow_popstarter;
    }
    if (result == VCD_LIST_BUTTON) {
        // Reuse the exact same static editor as Interface. It owns the VCD-list globals and
        // returns to the caller, so Circle naturally resumes the parent that launched it.
        guiShowVcdListConfig();
        goto reshow_popstarter;
    }

    if (result != UIID_BTN_CANCEL && result != -1 && !gSettingsShell.leftUntouched(ui, result, popstarterPathEdit, gPopstarterPath)) {
        int gameViewChanged = guiReadGameViewPicker(ui);

        diaGetInt(ui, CFG_POPSTARTER_RETROGEM_GAMEID, &gPopstarterRetroGemGameID);
        int emberDisplay = emberSettingFromRow(ui, CFG_EMBER_DISPLAY, emberDisplayShown, gEmberDisplay);
        // 480p is the one choice that can blank the screen (composite shows nothing): ask first, and
        // keep the previous mode if the answer is no.
        if (emberDisplay == EMBER_DISPLAY_480P && gEmberDisplay != EMBER_DISPLAY_480P && !guiMsgBox(_l(_STR_EMBER_480P_CONFIRM), 1, NULL))
            emberDisplay = gEmberDisplay;
        gEmberDisplay = emberDisplay;
        int emberTiming = emberSettingFromRow(ui, CFG_EMBER_TIMING, emberTimingShown, gEmberTiming);
        int emberDither = emberSettingFromRow(ui, CFG_EMBER_DITHER, emberDitherShown, gEmberDither);
        int emberShading = emberSettingFromRow(ui, CFG_EMBER_SHADING, emberShadingShown, gEmberShading);
        int emberController = emberSettingFromRow(ui, CFG_EMBER_CONTROLLER, emberControllerShown, gEmberController);
        // Global Timing / Dithering / Shading / Controller go into EMBER/settings.txt and reach EVERY PS1
        // game; some cannot take them (Arcade's Greatest Hits 1, a 2D MAME package, crashed -- FifthFox).
        // Gageformer: only Display belongs there. Warn when one of these is newly set (not back to
        // Default), pointing at the per-game menu; the choice itself is kept.
        if ((emberTiming != gEmberTiming && emberTiming > 0) || (emberDither != gEmberDither && emberDither > 0) ||
            (emberShading != gEmberShading && emberShading > 0) || (emberController != gEmberController && emberController > 0))
            guiMsgBox(_l(_STR_EMBER_GLOBAL_WARNING), 0, NULL);
        gEmberTiming = emberTiming;
        gEmberDither = emberDither;
        gEmberShading = emberShading;
        gEmberController = emberController;
        snprintf(gPopstarterPath, sizeof(gPopstarterPath), "%s", popstarterPathEdit);
        // Runtime ignores the retired picker, but keeping CUSTOM in the persisted compatibility
        // key means a downgrade still honours the full path the user entered here.
        gPopstarterDevice = (gPopstarterPath[0] != '\0') ? POPS_DEV_CUSTOM : POPS_DEV_DEFAULT;
        guiSaveBdmaSettings(ui);
        applyConfig(-1, -1, 0);
        if (gameViewChanged)
            guiRefreshGameViews();
        menuReinitMainMenu();
    }

    gSettingsShell.endDialog();
    gSettingsShell.setActiveDialog(NULL);
    return gSettingsShell.pageResult(result);
}

enum gui_settings_prompt_result {
    SETTINGS_PROMPT_SAVE = 1,
    SETTINGS_PROMPT_EXIT,
    SETTINGS_PROMPT_CONTINUE
};

void SettingsShell::readSourceState(SourceState *state)
{
    memset(state, 0, sizeof(*state));
    state->defaultDevice = gDefaultDevice;
    state->bdmStartMode = gBDMStartMode;
    state->hddStartMode = gHDDStartMode;
    state->appStartMode = gAPPStartMode;
    state->mmceStartMode = gMMCEStartMode;
    state->favStartMode = gFAVStartMode;
    state->allGames = gAllGames;
    state->showHidden = gShowHidden;
    state->enableUSB = gEnableUSB;
    state->enableILK = gEnableILK;
    state->enableMX4SIO = gEnableMX4SIO;
    state->enableBdmHDD = gEnableBdmHDD;
    state->networkProtocol = gNetworkProtocol;
    state->netStartMode = gNetStartMode;
    state->netProtocolPick = gNetProtocolPick;
    state->ethStartMode = gETHStartMode;
    state->enableUDPBD = gEnableUDPBD;
    state->netBootProtocol = gNetBootProtocol;
    state->mmceSlot = gMMCESlot;
    state->mmceIgrSlot = gMMCEIGRSlot;
    state->mmceEnableGameID = gMMCEEnableGameID;
    state->mmceAckWaitCycles = gMMCEAckWaitCycles;
    state->mmceUseAlarms = gMMCEUseAlarms;
    snprintf(state->mmcePrefix, sizeof(state->mmcePrefix), "%s", gMMCEPrefix);
}

void SettingsShell::captureSourceState()
{
    readSourceState(&sourceState_);
}

bool SettingsShell::sourceStateChanged()
{
    SourceState current;

    readSourceState(&current);
    return memcmp(&current, &sourceState_, sizeof(current)) != 0;
}

void SettingsShell::restoreSourceState()
{
    // Most Settings pages are not source pages. Avoid a full device refresh on a discard that only
    // changed visual/audio/launch options; doing so would manufacture exactly the kind of storage
    // churn this path exists to prevent.
    if (!sourceStateChanged())
        return;

    gDefaultDevice = sourceState_.defaultDevice;
    gBDMStartMode = sourceState_.bdmStartMode;
    gHDDStartMode = sourceState_.hddStartMode;
    gAPPStartMode = sourceState_.appStartMode;
    gMMCEStartMode = sourceState_.mmceStartMode;
    gFAVStartMode = sourceState_.favStartMode;
    int previousAllGames = gAllGames, previousShowHidden = gShowHidden;
    gAllGames = sourceState_.allGames;
    gShowHidden = sourceState_.showHidden;
    gEnableUSB = sourceState_.enableUSB;
    gEnableILK = sourceState_.enableILK;
    gEnableMX4SIO = sourceState_.enableMX4SIO;
    gEnableBdmHDD = sourceState_.enableBdmHDD;
    gNetworkProtocol = sourceState_.networkProtocol;
    gNetStartMode = sourceState_.netStartMode;
    gNetProtocolPick = sourceState_.netProtocolPick;
    gETHStartMode = sourceState_.ethStartMode;
    gEnableUDPBD = sourceState_.enableUDPBD;
    gNetBootProtocol = sourceState_.netBootProtocol;
    gMMCESlot = sourceState_.mmceSlot;
    gMMCEIGRSlot = sourceState_.mmceIgrSlot;
    gMMCEEnableGameID = sourceState_.mmceEnableGameID;
    gMMCEAckWaitCycles = sourceState_.mmceAckWaitCycles;
    gMMCEUseAlarms = sourceState_.mmceUseAlarms;
    snprintf(gMMCEPrefix, sizeof(gMMCEPrefix), "%s", sourceState_.mmcePrefix);

    // Game Sources can have already forced a BDM re-enumeration and every Settings page that calls
    // applyConfig(..., 0) can have rebuilt source visibility. Re-apply the captured routing now so
    // the list/menu state matches the values we just restored. This is the programmatic equivalent
    // of the X press zackcage6 needed to recover the blank page, but it restores the original source
    // instead of merely starting whichever page happens to be selected.
    bdmForceDeviceRefresh();
    applyConfig(-1, -1, 0);
    menuReinitMainMenu();
    if (previousAllGames != gAllGames)
        guiApplyAllGamesChange();
    else if (previousShowHidden != gShowHidden)
        guiRefreshCfgTitles();
    else if (gAllGames)
        loadFavourites(); // a source switched Off or on: All Games drops or gathers its rows
}

int SettingsShell::promptSave(void)
{
    int promptHints[3] = {_STR_SETTINGS_SAVE, _STR_SETTINGS_EXIT_WITHOUT_SAVING, _STR_SETTINGS_CONTINUE_EDITING};
    int promptIcons[3] = {guiConfirmIcon(), guiCancelIcon(), TRIANGLE_ICON};

    sfxPlay(SFX_MESSAGE);
    while (1) {
        int x, y;

        guiStartFrame();
        if (guiDrawBGSettings() == 0)
            guiDrawBGPlasma();

        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColDarker);
        rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
        rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);
        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, 150, ALIGN_CENTER, 0, 0,
                        _l(_STR_SETTINGS_SAVE_PROMPT), gTheme->selTextColor);
        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, 205, ALIGN_CENTER, 0, 0,
                        _l(_STR_SETTINGS_SAVE_PROMPT_LINE1), gTheme->textColor);
        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, 230, ALIGN_CENTER, 0, 0,
                        _l(_STR_SETTINGS_SAVE_PROMPT_LINE2), gTheme->textColor);

        x = guiAlignSubMenuHints(3, promptHints, promptIcons, gTheme->fonts[0], 12, 1);
        y = gTheme->usedHeight - 32;
        x = guiDrawIconAndText(promptIcons[0], promptHints[0], gTheme->fonts[0], x, y, gTheme->textColor);
        x += 12;
        x = guiDrawIconAndText(promptIcons[1], promptHints[1], gTheme->fonts[0], x, y, gTheme->textColor);
        x += 12;
        guiDrawIconAndText(promptIcons[2], promptHints[2], gTheme->fonts[0], x, y, gTheme->textColor);
        guiEndFrame();

        readPads();
        if (getKeyOn(guiConfirmKey())) {
            sfxPlay(SFX_CONFIRM);
            return SETTINGS_PROMPT_SAVE;
        }
        if (getKeyOn(guiCancelKey())) {
            sfxPlay(SFX_CANCEL);
            return SETTINGS_PROMPT_EXIT;
        }
        if (getKeyOn(KEY_TRIANGLE)) {
            sfxPlay(SFX_CURSOR);
            return SETTINGS_PROMPT_CONTINUE;
        }
    }
}

static void guiDrawSettingsIndexHints(void)
{
    int hints[2] = {_STR_SELECT, _STR_BACK};
    int icons[2] = {guiConfirmIcon(), guiCancelIcon()};
    int x = guiAlignSubMenuHints(2, hints, icons, gTheme->fonts[0], 12, 2);
    int y = gTheme->usedHeight - 32;

    x = guiDrawIconAndText(icons[0], hints[0], gTheme->fonts[0], x, y, gTheme->textColor);
    x += 12;
    guiDrawIconAndText(icons[1], hints[1], gTheme->fonts[0], x, y, gTheme->textColor);
}

int SettingsShell::showIndex(int *page)
{
    const char *labels[SETTINGS_PAGE_COUNT + 1] = {
        _l(_STR_GAME_SOURCES),
        _l(_STR_GENERAL_SYSTEM),
        _l(_STR_LANGUAGE),
        _l(_STR_MENU_NETWORK),
        _l(_STR_INTERFACE_SETTINGS),
        _l(_STR_GAME_LAUNCHING),
        _l(_STR_POPSTARTER),
        _l(_STR_CONTROLLER_SETTINGS),
        _l(_STR_AUDIO_SETTINGS),
        _l(_STR_SAVE_CHANGES),
    };
    int selected = *page;
    const int itemCount = SETTINGS_PAGE_COUNT + 1;
    int spacing = 25;
    int y;

    if (selected < SETTINGS_SOURCES || selected >= SETTINGS_PAGE_COUNT)
        selected = SETTINGS_SOURCES;

    // This is intentionally rendered with the same geometry and highlight treatment as the main
    // menu. The Index is the Settings hub, not another dialog with a focus box around each row.
    while (1) {
        guiStartFrame();
        if (guiDrawBGSettings() == 0)
            guiDrawBGPlasma();

        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, 50, ALIGN_CENTER, 0, 0, _l(_STR_SETTINGS), gTheme->textColor);

        // Keep the exact running build visible on the Settings hub so bug reports do not require
        // opening About/Credits just to discover the version. OPL_VERSION already includes the
        // release/revision/hash/toolchain suffix used everywhere else in the build.
        char versionLine[80];
        snprintf(versionLine, sizeof(versionLine), "RiptOPL %s", OPL_VERSION);
        fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, 72, ALIGN_CENTER, 0, 0, versionLine, gTheme->textColor);

        y = (gTheme->usedHeight >> 1) - (spacing * (itemCount >> 1));
        for (int i = 0; i < itemCount; i++) {
            fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, y, ALIGN_CENTER, 0, 0, labels[i], (i == selected) ? gTheme->selTextColor : gTheme->textColor);
            y += spacing;
            if (i == SETTINGS_PAGE_COUNT - 1)
                y += spacing / 2;
        }

        guiDrawSettingsIndexHints();
        guiEndFrame();

        readPads();
        if (getKey(KEY_UP)) {
            sfxPlay(SFX_CURSOR);
            selected = (selected + itemCount - 1) % itemCount;
        } else if (getKey(KEY_DOWN)) {
            sfxPlay(SFX_CURSOR);
            selected = (selected + 1) % itemCount;
        } else if (getKeyOn(guiConfirmKey())) {
            sfxPlay(SFX_CONFIRM);
            if (selected == SETTINGS_PAGE_COUNT) {
                if (menuSaveSettings() > 0) {
                    // A successful in-shell save becomes the new discard baseline. Otherwise,
                    // saving once and later choosing "Exit without saving" would wrongly restore
                    // the source state from before Settings was opened, undoing a state that is
                    // already persisted on disk.
                    captureSourceState();
                    savePending_ = 0;
                }
            } else {
                *page = selected;
                return 1;
            }
        } else if (getKeyOn(KEY_START) || getKeyOn(guiCancelKey())) {
            sfxPlay(SFX_CANCEL);
            if (!savePending_)
                return 0;

            int promptResult = promptSave();
            if (promptResult == SETTINGS_PROMPT_SAVE) {
                if (menuSaveSettings() > 0) {
                    savePending_ = 0;
                    return 0;
                }
            } else if (promptResult == SETTINGS_PROMPT_EXIT) {
                hddDiscardOplHomeSelection();
                restoreSourceState();
                savePending_ = 0;
                return 0;
            }
        }
    }
}

void SettingsShell::showFromPage(int page, int showIndexFirst)
{
    int result;

    if (page < SETTINGS_SOURCES || page >= SETTINGS_PAGE_COUNT)
        page = SETTINGS_SOURCES;

    shellActive_ = true;
    savePending_ = 0;
    captureSourceState();
    if (showIndexFirst && !showIndex(&page)) {
        hddDiscardOplHomeSelection();
        shellActive_ = false;
        savePending_ = 0;
        return;
    }

    while (1) {
        currentPage_ = page;
        switch (page) {
            case SETTINGS_GENERAL:
                result = guiSettingsShowGeneral();
                break;
            case SETTINGS_LANGUAGE:
                result = guiSettingsShowLanguage();
                break;
            case SETTINGS_SOURCES:
                result = guiSettingsShowSources();
                break;
            case SETTINGS_NETWORK:
                result = guiShowNetConfig();
                break;
            case SETTINGS_INTERFACE:
                result = guiSettingsShowInterface();
                break;
            case SETTINGS_LAUNCH:
                result = guiSettingsShowLaunch();
                break;
            case SETTINGS_POPSTARTER:
                result = guiSettingsShowPopstarter();
                break;
            case SETTINGS_CONTROLLERS:
                result = guiShowControllerConfig();
                break;
            case SETTINGS_AUDIO:
                result = guiShowAudioConfig();
                break;
            default:
                hddDiscardOplHomeSelection();
                shellActive_ = false;
                savePending_ = 0;
                return;
        }

        if (result == DIA_RESULT_INDEX) {
            if (!showIndex(&page)) {
                hddDiscardOplHomeSelection();
                shellActive_ = false;
                savePending_ = 0;
                return;
            }
        } else if (result == DIA_RESULT_NEXT) {
            page = (page + 1) % SETTINGS_PAGE_COUNT;
        } else if (result == DIA_RESULT_PREV) {
            page = (page + SETTINGS_PAGE_COUNT - 1) % SETTINGS_PAGE_COUNT;
        } else {
            hddDiscardOplHomeSelection();
            shellActive_ = false;
            savePending_ = 0;
            return;
        }
    }
}

void guiShowSettings(void)
{
    gSettingsShell.showFromPage(SETTINGS_SOURCES, 1);
}

void guiShowPsEmulationSettings(void)
{
    // PS1 Triangle shortcut: enter the normal Settings shell directly on the global PS emulation
    // page. The shell still owns Save Changes and navigation, and returning leaves the caller's
    // selected PS1 title/menu intact.
    gSettingsShell.showFromPage(SETTINGS_POPSTARTER, 0);
}

// One per-game Ember setting from the row's CFG, or UNSET when absent / out of range.
static int emberGameSettingGet(config_set_t *configSet, const char *key, int count)
{
    int value;

    if (!configGetInt(configSet, key, &value) || value < 0 || value >= count)
        return EMBER_SETTING_UNSET;
    return value;
}

// PS1 Triangle -> Ember Game Settings. Ember accepts timing, dither, shading and controller in
// a game's settings.txt; display is deliberately global-only. Untouched Default preserves existing
// game values; changing a managed value back to Default removes that override before launch.
void guiShowEmberGameSettings(void)
{
    const char *timingStrs[] = {_l(_STR_DEFAULT), _l(_STR_AUTO), "NTSC", "PAL", NULL};
    const char *ditherStrs[] = {_l(_STR_DEFAULT), _l(_STR_ON), _l(_STR_OFF), NULL};
    const char *shadingStrs[] = {_l(_STR_DEFAULT), "15-bit", _l(_STR_EMBER_SHADING_24), NULL};
    const char *controllerStrs[] = {_l(_STR_DEFAULT), _l(_STR_AUTO), "Analog", "D2A", NULL};
    config_set_t *configSet = gameMenuLoadConfig(NULL);
    int timing, dither, shading, controller;
    int newTiming, newDither, newShading, newController;

    if (configSet == NULL)
        return;

    timing = emberGameSettingGet(configSet, CONFIG_ITEM_EMBER_TIMING, EMBER_TIMING_COUNT);
    dither = emberGameSettingGet(configSet, CONFIG_ITEM_EMBER_DITHER, EMBER_DITHER_COUNT);
    shading = emberGameSettingGet(configSet, CONFIG_ITEM_EMBER_SHADING, EMBER_SHADING_COUNT);
    controller = emberGameSettingGet(configSet, CONFIG_ITEM_EMBER_CONTROLLER, EMBER_CONTROLLER_COUNT);

    diaSetEnum(diaEmberGameConfig, CFG_EMBER_TIMING, timingStrs);
    diaSetInt(diaEmberGameConfig, CFG_EMBER_TIMING, emberSettingShown(timing));
    diaSetEnum(diaEmberGameConfig, CFG_EMBER_DITHER, ditherStrs);
    diaSetInt(diaEmberGameConfig, CFG_EMBER_DITHER, emberSettingShown(dither));
    diaSetEnum(diaEmberGameConfig, CFG_EMBER_SHADING, shadingStrs);
    diaSetInt(diaEmberGameConfig, CFG_EMBER_SHADING, emberSettingShown(shading));
    diaSetEnum(diaEmberGameConfig, CFG_EMBER_CONTROLLER, controllerStrs);
    diaSetInt(diaEmberGameConfig, CFG_EMBER_CONTROLLER, emberSettingShown(controller));

    if (diaExecuteDialog(diaEmberGameConfig, -1, 1, NULL) != UIID_BTN_OK)
        return;

    newTiming = 0;
    newDither = 0;
    newShading = 0;
    newController = 0;
    diaGetInt(diaEmberGameConfig, CFG_EMBER_TIMING, &newTiming);
    diaGetInt(diaEmberGameConfig, CFG_EMBER_DITHER, &newDither);
    diaGetInt(diaEmberGameConfig, CFG_EMBER_SHADING, &newShading);
    diaGetInt(diaEmberGameConfig, CFG_EMBER_CONTROLLER, &newController);

    // An absent CFG key is displayed as Default and stays unmanaged until a row changes. Once a row
    // changes, store all four choices so a later return to Default can remove its per-game override.
    if (newTiming != emberSettingShown(timing) || newDither != emberSettingShown(dither) ||
        newShading != emberSettingShown(shading) || newController != emberSettingShown(controller)) {
        configSetInt(configSet, CONFIG_ITEM_EMBER_TIMING, newTiming);
        configSetInt(configSet, CONFIG_ITEM_EMBER_DITHER, newDither);
        configSetInt(configSet, CONFIG_ITEM_EMBER_SHADING, newShading);
        configSetInt(configSet, CONFIG_ITEM_EMBER_CONTROLLER, newController);
        if (!menuSaveConfig()) // the normal CFG error message includes its path and errno
            return;
    }

    if (menuSaveEmberGameSettings(configSet))
        guiMsgBox(_l(_STR_GAME_SETTINGS_SAVED), 0, NULL);
    else
        guiMsgBox(_l(_STR_ERROR_SAVING_SETTINGS), 0, NULL);
}

static int guiConfirmVideoMode(void)
{
    // Elapsed form. This auto-revert is the ONLY thing that rescues a user whose new video mode
    // does not sync -- there is nothing on screen to read and nothing to aim at. A deadline that
    // straddles the clock() wrap would leave them there.
    clock_t timeStart, holdStart = 0;
    int terminate = 0, holding = 0, holdPermille = 0, barPermille = 0;

    sfxPlay(SFX_MESSAGE);

    timeStart = clock();
    while (!terminate) {
        guiStartFrame();

        readPads();

        // KEEPING the new mode takes a HOLD of Accept, not a tap. A mode the TV cannot show leaves the
        // user blind, and a blind user taps buttons: one tap of Accept used to keep the dead mode, the
        // next Save wrote it to the config, and every boot came up black until the .cfg was deleted
        // (zackcage6, 09-25, interlaced-only TV). The hold has to START on this screen -- a button
        // still down from the Settings dialog has no key-on edge here -- and letting go restarts it.
        if (getKeyOn(gSelectButton)) {
            holding = 1;
            holdStart = clock();
        } else if (!getKeyPressed(gSelectButton))
            holding = 0;

        holdPermille = 0;
        barPermille = 0;
        if (holding) {
            // To ms FIRST: scaling raw ticks by 1000 could overflow clock_t at a high tick rate.
            int heldMs = (int)((clock() - holdStart) / (CLOCKS_PER_SEC / 1000));
            holdPermille = (heldMs * 1000) / VMODE_KEEP_HOLD_MS;
            barPermille = (heldMs * 1000) / VMODE_KEEP_BAR_FULL_MS;
        }

        if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE))
            terminate = 1;
        else if (holdPermille >= 1000)
            terminate = 2;
        // No answer within the timeout = cancel. Not while a hold is running: it settles within
        // VMODE_KEEP_HOLD_MS either way, and a hold started at the last second should get to finish.
        else if (!holding && (clock() - timeStart) >= (clock_t)OPL_VMODE_CHANGE_CONFIRMATION_TIMEOUT_MS * (CLOCKS_PER_SEC / 1000))
            terminate = 1;

        // Opaque, and NOTHING is drawn under it. The prompt itself is what proves the new mode syncs;
        // what showed through the old ~75% wash was the game list page, which testers saw empty
        // behind the prompt. Once the backdrop went opaque, rendering that page first (guiShow) was
        // pure frame time -- in the heavy high-res modes enough that the hold bar advanced in big
        // steps and the keep landed while it still looked 4/5 full (zackcage6, 09-26).
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);

        rmDrawLine(50, 75, gGuiScreenWidth - 50, 75, gColWhite);
        rmDrawLine(50, 410, gGuiScreenWidth - 50, 410, gColWhite);

        int textBottom = guiDrawMsgText(_l(_STR_CFM_VMODE_CHG));
        // How long until the automatic revert (zackcage6's idea). Hidden during a hold. A hold does NOT
        // pause the deadline -- it only defers the revert check so a started hold can finish; let go
        // early after the deadline and the next pass reverts. Whole seconds rounded up, so it reads
        // 10 .. 1 and never 0 while still up.
        if (!holding) {
            int leftMs = OPL_VMODE_CHANGE_CONFIRMATION_TIMEOUT_MS - (int)((clock() - timeStart) / (CLOCKS_PER_SEC / 1000));
            char countdown[64];
            snprintf(countdown, sizeof(countdown), _l(_STR_CFM_VMODE_REVERT_IN), leftMs > 0 ? (leftMs + 999) / 1000 : 1);
            fntRenderString(gTheme->fonts[0], gGuiScreenWidth >> 1, textBottom + 32, ALIGN_CENTER, 0, 0, countdown, gTheme->textColor);
        }
        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_BACK, gTheme->fonts[0], 500, 417, gTheme->textColor);
        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON, _STR_CFM_VMODE_HOLD_KEEP, gTheme->fonts[0], 70, 417, gTheme->textColor);
        // Hold progress, just above the footer line and exactly as long as it, so FULL is unmistakable:
        // the bar has reached the line's right-hand end. It used to be a fixed 200 px from x 70, which
        // ended ~40% across the screen and read as stuck halfway (zackcage6, #774). It also reads full
        // VMODE_KEEP_HOLD_MS - VMODE_KEEP_BAR_FULL_MS before the keep, so the user SEES it complete --
        // before, it only reached full on the very frame the prompt closed.
        if (holdPermille > 0)
            rmDrawRect(50, 400, ((gGuiScreenWidth - 100) * (barPermille > 1000 ? 1000 : barPermille)) / 1000, 4, gTheme->selTextColor);

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
