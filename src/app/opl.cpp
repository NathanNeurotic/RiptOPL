/*
  Copyright 2009, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/settings.h"
#include "include/diaglog.h" // the hardware log, DIAG.TXT (RIPPS2 build 115)
#include "include/menuwatch.h"
#include "include/oplint.h"
#include "include/opl.h"
#include "include/cfgtitle.h"
#include "include/sourceregistry.h" // SourceRegistry gSourceRegistry
#include "include/ioman.h"
#include "include/launchdiag.h" // UDPBD hang-triage stage markers (gated on gLaunchDiag)
#include "include/gui.h"
#include "include/guigame.h"
#include "include/renderman.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/textures.h"
#include "include/artindex.h"
#include "include/pad.h"
#include "include/texcache.h"
#include "include/dia.h"
#include "include/dialogs.h"
#include "include/menusys.h"
#include "include/system.h"
#include "include/debug.h"
#include "include/fntsys.h"
#ifdef __OPLDIAG
#include <delaythread.h>
#endif
#include "include/config.h"
#include "include/util.h"
#include "include/extern_irx.h"

#include "include/supportbase.h"
#include "include/bdmsupport.h"
#include "include/ethsupport.h"
#include "include/udpfssupport.h"
#include "include/httpsupport.h"
#include "include/hddsupport.h"
#include "include/appsupport.h"
#include "include/favsupport.h"
#include "include/gamemanage.h"   // Hide Game and Delete everything (RIPPS2)
#include "include/folderbrowse.h" // folderDepth -- favourites suppression inside subfolders
#include "include/mmcesupport.h"  // mmceLoadModules() -- MMCE boot branch of resolveBootDirToMass
#include "include/tar.h"          // tarInvalidate -- re-arm the .tar probe on a settings apply
#include "include/vcdsupport.h"
#include "include/cuesupport.h" // cueIsCueEntry -- a PS1 row names its own core   // VCD display naming + POPSTARTER launch helpers
#include "include/libview.h"    // libViewActive / libListViewActive -- which list this page shows
#ifdef RETROACHIEVEMENTS
#include "include/rabadge.h" // RA: "tracked game" badge in the list
#endif

#include "include/cheatman.h"
#include "include/sound.h"
#include "include/xparam.h"

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioDopen/fileXioIoctl2/fileXioDclose used by BDM autolaunch
#include <unistd.h>
#include <sys/stat.h> // mkdir() -- create mc?:/OPL before mirroring the settings-path redirect.
                      // Explicit rather than transitive on purpose: this tree builds WITHOUT -Wall,
                      // so an implicitly declared mkdir would compile silently and only go wrong at
                      // runtime. bdmsupport.c includes it for the same reason.
#ifdef PADEMU
#include <libds34bt.h>
#include <libds34usb.h>
#endif

#include "include/oplog.h"
#include "include/gamesource.h"

// App support stuff.

static void clearIOModuleT(opl_io_module_t *mod)
{
    mod->subMenu = NULL;
    mod->support = NULL;
    mod->menuItem.execCross = NULL;
    mod->menuItem.execCircle = NULL;
    mod->menuItem.execSquare = NULL;
    mod->menuItem.execTriangle = NULL;
    mod->menuItem.fav = NULL;
    mod->menuItem.toggleView = NULL;
    mod->menuItem.hints = NULL;
    mod->menuItem.icon_id = -1;
    mod->menuItem.current = NULL;
    mod->menuItem.submenu = NULL;
    mod->menuItem.pagestart = NULL;
    mod->menuItem.last = NULL; // coverflow wrap tail (device re-init must not leave it dangling)
    mod->menuItem.remindLast = 0;
    mod->menuItem.refresh = NULL;
    mod->menuItem.text = NULL;
    mod->menuItem.text_id = -1;
    mod->menuItem.userdata = NULL;
}

// forward decl
static void clearMenuGameList(opl_io_module_t *mdl);
static void moduleCleanup(opl_io_module_t *mod, int exception, int modeSelected);
static void deferredAudioInit(void);

// frame counter
static unsigned int frameCounter;
// Per-mode background-rescan throttle (Fix B): the every-frame (updateDelay==0) device rescans
// enumerate the SIO2/mass bus; space them by a minimum wall-clock interval so they don't run
// unthrottled. A real device change bypasses the throttle via bdmGetGeneration().
// A BDM slot that has never held a device is probed this many times less often than one that has.
// Its probe is only a missed-event net; a present device keeps the full cadence so removal is still
// noticed promptly.
constexpr int BDM_EMPTY_SLOT_PROBE_RATIO = 8;

static clock_t lastBgRescan[MODE_COUNT];
static unsigned int lastSeenBdmGeneration;

static char errorMessage[256];
static int errorMessageStringId = -1;

// The page table itself lives in gSourceRegistry (include/sourceregistry.h). list_support stays as
// a NAME in this file, a constant-initialized pointer at the table's first slot: the body of
// menuDeferredUpdate below is extracted by test_io_queue_trims.py and compiled against a harness
// that declares its own file-scope list_support[], so that body must keep saying list_support[i].
static opl_io_module_t *const list_support = gSourceRegistry.table();

SourceRegistry gSourceRegistry;

// Favourites accessor (see opl.h): the table lives in gSourceRegistry; this thin wrapper is the
// cross-module reach (favsupport and the rest address pages by mode, never by the table).
opl_io_module_t *oplGetModule(int mode)
{
    return gSourceRegistry.module(mode);
}

// Body of the oplGetAppImageByMode entry point (declared in opl.h, kept as a free forwarder in
// appscan.cpp): art lookup addressed by IO MODE rather than by a device string.
int SourceRegistry::appImageByMode(int mode, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    item_list_t *listSupport;

    if (mode < 0 || mode >= MODE_COUNT)
        return -1;

    listSupport = module(mode)->support;
    if ((listSupport != NULL) && (listSupport->enabled))
        return gsOf(listSupport)->image(listSupport, folder, isRelative, value, suffix, resultTex, psm);

    return -1;
}

// Global data
char *gBaseMCDir;
int ps2_ip_use_dhcp;
int ps2_ip[4];
int ps2_netmask[4];
int ps2_gateway[4];
int ps2_dns[4];
int gETHOpMode; // See ETH_OP_MODES.
int gPCShareAddressIsNetBIOS;
int pc_ip[4];
int gPCPort;
int gHttpServerIp[4];
int gHttpPort;
char gHttpBasePath[HTTP_BASE_PATH_MAX];
char gPCShareNBAddress[17];
char gPCShareName[32];
char gPCUserName[32];
char gPCPassword[32];
int gNetworkStartup;
int gHDDSpindown;
int gBDMStartMode;
int gHDDStartMode;
int gETHStartMode;
int gAPPStartMode;
int gMMCEStartMode;
int gMMCEIGRSlot;
int gMMCESlot;
int gMMCEAckWaitCycles;
int gMMCEUseAlarms;
int gMMCEEnableGameID;
int bdmCacheSize;
int hddCacheSize;
int smbCacheSize;
int gApplyGameID;
int gEnableUSB;
char gNeutrinoArgs[256];     // extra command-line flags appended to every Neutrino launch
char gNeutrinoPath[256];     // optional full path; runtime order: custom -> game device -> mc0/mc1
int gNeutrinoDevice;         // retired picker value; only MC/APA survive, as a migration marker (configReadNeutrinoGlobals)
int gDefaultCoreLoader;      // global default Loader Core (0=<OPL>, 1=Neutrino); per-game $CoreLoader overrides, absent key = follow this
int gAutoCreateVmc;          // opt in: create and assign a first-launch PS2 card in slot 1
int gIgrMenu;                // opt in: the IGR combo pauses the game under the in-game menu (beta)
int gNeutrinoVideoDefault;   // global default Neutrino -gsm video mode (0=Off..5=1080i x3); per-game $NeutrinoVideo overrides
int gNeutrinoGsmCompDefault; // global default -gsm ":c" field-flip half (0=off, 1-3=type)
int gNeutrinoShowArgs;       // default-off: show the composed Neutrino argv and ask before each launch
int gNeutrinoElfArg;         // default-on (settings key only, no UI): auto-emit -elf=cdrom0: on Neutrino launches
int gDefaultGameView;
int gAppsDisplay;
int gCfgTitlesInLists;
int gPs1Duplicates; // PS1_DUP_*: a game both as a POPSTARTER VCD and an Ember image shows once or twice
int gGameTypeLabels;
int gEmberDisplay;
int gEmberTiming;
int gEmberDither;
int gEmberShading;
int gEmberController;
char gPopstarterPath[256];         // optional full path; runtime order: custom -> game device -> mc0/mc1
int gPopstarterDevice;             // retired picker value retained in config for compatibility; runtime ignores it
int gPopstarterRetroGemGameID = 1; // RetroGEM Game ID optical barcode for VCD launches (1=on, default)
int gBdmaSource;                   // BDMA SOURCE device family (VCD_BDMA_SRC_*)
int gBdmaMode;                     // BDMA MODE mirrored from the mc?:/POPSTARTER/ marker
int gBdmaApplyOnLaunch;            // auto-equip the launched VCD's matching exFAT driver before boot
int gVcdUsbBdmaMode;               // VCD_USB_BDMA_*: POPSTARTER USB driver for USB VCD launches
int gVcdHideGameId;                // display-only: hide a leading PS1 game-ID prefix from VCD lists
int gVcdShowPpPops;                // enumeration-only: list strict PP.<ID>.POPS.<name> one-game HDD partitions
int gVcdFirstDiscOnly;             // hide discs 2+ of multi-disc PS1 sets
char gBootDir[256];                // boot directory (cwd) OPL launched from; "" if undeterminable
int gEnableILK;
int gEnableBGArt;
int gEnableSaveIcons;                    // 3D save icon theme element (SaveIcon)
int gEnableArtTar;                       // .tar art packs (item 45); no UI until gate D
int gArtDelay;                           // inactivity frames before art loads; safe default until gate D tunes it
int gEnableFolderNav;                    // folder browsing in game lists (item 34)
unsigned char gDefaultPlasBlendColor[3]; // plasma gradient low end; black = historical look
volatile int gLastSaveErrno = 0;
// Set by guiHandleDeferedIO when it abandons a bounded wait: the request never ran, so any
// error the caller would otherwise report against the target path is meaningless.
volatile int gLastDeferredTimedOut = 0;
int gEnableMX4SIO;
int gEnablePCCF;
int gEnableDVR;
int gEnableBdmHDD;
int gEnableUDPBD;
int gNetBootProtocol; // NET_BOOT_UDPBD | NET_BOOT_UDPFS (legacy shadow, derived from gNetworkProtocol)
int gNetworkProtocol; // enum NETWORK_PROTOCOL -- authoritative backend selector (Off/SMB/UDPBD/UDPFSBD/UDPFS)
int gNetProtocolPick; // last protocol chosen, never OFF (see opl.h)
int gUdpfsAccessMode; // UDPFS Files/IMG preference, independent of UDPBD
int gNetStartMode;    // START_MODE_* -- the Off/Manual/Auto network start row (see the 3-row Network setting)
int gAutosort;
int gAutoRefresh;
int gEnableNotifications;
#ifdef RETROACHIEVEMENTS
int gRATelemetry;
int gRABadges;
int gRABadgeSuffix;
int gRAMode;
int gRAHostIp[4];
int gRAExperimentalCards;
#endif
int gEnableArt;
int gEnableDiscArt;
int gEnableLogoArt;
int gWideScreen;
int gFontSize = FONT_SIZE_DEFAULT;
int gGlyphSet = 0; // GLYPH_SET_THEME
// Alternate Background: the theme's alt= pictures instead of its default= ones (themeviews.h).
int gThemeAltBackground;
// Boot Theme Wait: seconds the intro holds for a saved disk theme whose drive is not up yet (guisplash.cpp).
int gBootThemeWait;
int gVMode; // 0 - Auto, 1 - PAL, 2 - NTSC
int gXOff;
int gYOff;
int gOverscan;
int gSelectButton;
int gHDDGameListCache;
int gEnableSFX;
int gEnableRumble;
int gEnableBootSND;
int gEnableBGM;
int gSFXVolume;
int gBootSndVolume;
int gBGMVolume;
char gDefaultBGMPath[128];
int gCheatSource;
int gGSMSource;
int gPadEmuSource;
int gFadeDelay;
int toggleSfx;
int showCfgPopup;
int showNetDhcpPopup; // a UDP transport is selected but IP Type is DHCP -- ministack needs a static IP
#ifdef PADEMU
int gEnablePadEmu;
int gPadEmuSettings;
int gPadMacroSource;
int gPadMacroSettings;
#endif
int gScrollSpeed;
char gExitPath[256];
char gCustomSettingsPath[64];
int gEnableDebug;
int gPS2Logo;
int gDefaultDevice;
int gEnableWrite;
char gBDMPrefix[32];
char gETHPrefix[32];
char gMMCEPrefix[32];
int gRememberLastPlayed;
int KeyPressedOnce;
int gAutoStartLastPlayed;
int RemainSecs, DisableCron;
clock_t CronStart;
unsigned char gDefaultBgColor[3];
unsigned char gDefaultTextColor[3];
unsigned char gDefaultSelTextColor[3];
unsigned char gDefaultUITextColor[3];
unsigned char gDefaultTitleColor[3];
hdl_game_info_t *gAutoLaunchGame;
base_game_info_t *gAutoLaunchBDMGame;
bdm_device_data_t *gAutoLaunchDeviceData;
char gOPLPart[128];
char *gHDDPrefix;
char gExportName[32];

int gXSensitivity;
int gYSensitivity;

int gOSDLanguageValue;
int gOSDTVAspectRatio;
int gOSDVideOutput;
int gOSDLanguageEnable;
int gOSDLanguageSource;

void moduleUpdateMenuInternal(opl_io_module_t *mod, int themeChanged, int langChanged);

void moduleUpdateMenu(int mode, int themeChanged, int langChanged)
{
    if (mode == -1)
        return;

    opl_io_module_t *mod = &list_support[mode];
    moduleUpdateMenuInternal(mod, themeChanged, langChanged);
}

void moduleUpdateMenuInternal(opl_io_module_t *mod, int themeChanged, int langChanged)
{
    if (!mod->support)
        return;

    if (langChanged) {
        guiUpdateScreenScale();
        guiCheckNotifications(0, langChanged);
    }

    /* One lock across the WHOLE rebuild, not per call. This runs on the io worker while the GUI
       thread walks the same list twice a frame; a per-call lock would still let a draw land between
       the free and the relink and render a half-built row. Cheap: a few frees and mallocs, no I/O,
       nothing that can block -- so the renderer never waits more than microseconds for it. */
    menuHintsLock();

    // refresh Hints
    menuRemoveHints(&mod->menuItem);

    menuAddHint(&mod->menuItem, _STR_MENU, START_ICON);
    if (!mod->support->enabled)
        menuAddHint(&mod->menuItem, _STR_START_DEVICE, gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON);
    else {
        menuAddHint(&mod->menuItem, _STR_RUN, gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON);

        if (gTheme->infoElems.first)
            menuAddHint(&mod->menuItem, _STR_INFO, SQUARE_ICON);

        unsigned char flags = (mod->support->mode == FAV_MODE) ? favGetFlags(mod->support) : mod->support->flags;
        if (!(flags & MODE_FLAG_NO_COMPAT) || gEnableWrite)
            menuAddHint(&mod->menuItem, _STR_OPTIONS, TRIANGLE_ICON);

        menuAddHint(&mod->menuItem, _STR_REFRESH, SELECT_ICON);

        if (gFAVStartMode || gAllGames)
            menuAddHint(&mod->menuItem, _STR_FAV_HINT, R3_ICON);

        // L3 moves the page around its ring of lists. Both and Mixed expose rings; PS2/PS1 pin an
        // ordinary device page, while Favorites and APPS retain their independent display rules.
        //
        // The question is "does this page have more than one list", NOT "does it have a VCD list".
        // Those were the same thing while every ring was ISO<->VCD, and stop being the same as soon
        // as a page offers a third; libViewRingSize keeps the hint honest either way.
        if (libViewRingUsable(mod->support->mode)) {
            int hint = _STR_VCD;
            if (mod->support->mode == FAV_MODE || mod->support->mode == APP_MODE ||
                gDefaultGameView == GAME_VIEW_MIXED)
                hint = _STR_VIEW;
            menuAddHint(&mod->menuItem, hint, L3_ICON);
        }
    }

    menuHintsUnlock();

    // refresh Cache
    if (themeChanged) {
        if (mod->subMenu) {
            // Serialize the per-item cache_id/cache_uid array reallocation with rendering: this runs
            // on the IO thread during a mid-session theme reload while the GUI thread draws the
            // carousel THROUGH those arrays (read + write on enqueue). Unserialized, a draw racing
            // the free()+malloc() wrote a fresh (index, uid) pair through a freed pointer into a
            // NEIGHBOUR's reallocated array -- the permanent wrong-cover mapping.
            guiLock();
            submenuRebuildCache(mod->subMenu);
            guiUnlock();
        }
        guiCheckNotifications(themeChanged, 0);
    }
}


static void initMenuForListSupport(opl_io_module_t *mod)
{
    mod->menuItem.icon_id = gsOf(mod->support)->iconId(mod->support);
    mod->menuItem.text = NULL;
    mod->menuItem.text_id = gsOf(mod->support)->textId(mod->support);
    mod->menuItem.visible = 1;

    mod->menuItem.userdata = mod->support;

    mod->subMenu = NULL;

    mod->menuItem.submenu = NULL;
    mod->menuItem.current = NULL;
    mod->menuItem.pagestart = NULL;
    mod->menuItem.last = NULL; // coverflow wrap tail
    mod->menuItem.remindLast = 0;

    mod->menuItem.refresh = &itemExecRefresh;
    mod->menuItem.execCross = &itemExecCross;
    mod->menuItem.execTriangle = &itemExecTriangle;
    mod->menuItem.execSquare = &itemExecSquare;
    mod->menuItem.execCircle = &itemExecCircle;
    mod->menuItem.fav = &itemExecFav;
    mod->menuItem.toggleView = &itemExecToggleView;

    mod->menuItem.hints = NULL;

    moduleUpdateMenuInternal(mod, 0, 0);

    struct gui_update_t *mc = guiOpCreate(GUI_OP_ADD_MENU);
    if (mc) { // guiOpCreate returns NULL on OOM -- skip the deferred op rather than deref NULL
        mc->menu.menu = &mod->menuItem;
        mc->menu.subMenu = &mod->subMenu;
        guiDeferUpdate(mc);
    }
}

static void clearMenuGameList(opl_io_module_t *mdl)
{
    if (mdl->subMenu != NULL) {
        // lock - gui has to be unused here
        guiLock();

        submenuDestroy(&mdl->subMenu);
        mdl->menuItem.submenu = NULL;
        mdl->menuItem.current = NULL;
        mdl->menuItem.pagestart = NULL;
        mdl->menuItem.last = NULL; // coverflow wrap tail (list clear/refresh must reset it)
        mdl->menuItem.remindLast = 0;

        // unlock
        guiUnlock();
    }
}

void initSupport(item_list_t *itemList, int mode, int force_reinit)
{
    opl_io_module_t *mod = &list_support[mode];

    // Set the start mode flag based on device type.
    int startMode = 0;
    if (mode >= BDM_MODE && mode < ETH_MODE)
        // Effective, not raw: a selected UDPBD/UDPFSBD network protocol floors BDM to Auto so its
        // hotplug tab can exist (see bdmEffectiveStartMode) -- the saved setting is untouched.
        startMode = bdmEffectiveStartMode();
    else if (mode == ETH_MODE)
        startMode = gETHStartMode;
    else if (mode == HDD_MODE)
        startMode = gHDDStartMode;
    else if (mode == APP_MODE)
        startMode = gAPPStartMode;
    else if (mode == FAV_MODE)
        startMode = gAllGames ? START_MODE_AUTO : gFAVStartMode; // All Games lives on this page
    else if (mode == UDPFS_MODE)
        // UDPFS filesystem tab honours the network start row: Auto loads the udpfs IRX chain + mount at
        // boot, Manual defers it to tab-entry. Live only while its protocol is the selected one, so
        // exactly one network tab is ever enabled.
        startMode = (gNetworkProtocol == NET_PROTO_UDPFS) ? gNetStartMode : START_MODE_DISABLED;
    else if (mode == HTTP_MODE)
        startMode = (gNetworkProtocol == NET_PROTO_HTTP) ? gNetStartMode : START_MODE_DISABLED;
    else if (mode == MMCE_MODE)
        startMode = gMMCEStartMode;

    if (startMode) {
        if (!mod->support) {
            mod->support = itemList;
            mod->support->owner = mod;
            initMenuForListSupport(mod);

            // First-ever tab while the GUI sits on the start menu (fresh/default config, every
            // mode OFF): take the user TO the new tab, or they stay on the "default interface"
            // with no visible way to it (#254 follow-up: "Apps section does not appear in the
            // default interface even if enabled, until a theme is applied" -- the theme apply was
            // just the thing that returned them to the tab screen). Boot is unaffected:
            // deferredInit's default-device select queues AFTER this one and wins; multi-mode
            // enables land on the last registered tab.
            if (!menuHasRegisteredItems()) {
                struct gui_update_t *sel = guiOpCreate(GUI_OP_SELECT_MENU);
                if (sel) {
                    sel->menu.menu = &mod->menuItem;
                    guiDeferUpdate(sel);
                }
            }
        } else {
            // Re-enable after a prior disable: the support + its menu item already exist (registered on
            // an earlier enable), so the !mod->support branch above -- the ONLY place that sets
            // menuItem.visible = 1 -- is skipped. Disabling a mode (else branch below) set visible = 0
            // but LEFT mod->support non-NULL, so without restoring it here a tab that was ever toggled
            // off stays hidden for the rest of the session even when re-selected. This is why picking a
            // network protocol after having switched away from it showed no tab. BDM device tabs escape
            // this via bdmNeedsUpdate's per-refresh visibility (and bdmInitDevicesData overrides it on
            // its own path); ETH/UDPFS/APP/FAV/HDD have no such hook, so restore it here.
            mod->menuItem.visible = 1;
        }

        if (((force_reinit) && (mod->support->enabled)) || (startMode == START_MODE_AUTO && !mod->support->enabled)) {
            gsOf(mod->support)->init(mod->support);
            moduleUpdateMenuInternal(mod, 0, 0);

            ioPutRequest(IO_MENU_UPDATE_DEFFERED, &list_support[mode].support->mode); // can't use mode as the variable will die at end of execution
        }
    } else {
        // If the module has a valid menu instance try to refresh the visibility state.
        mod->menuItem.visible = 0;
    }
}

// Boot-splash status (#297): true only across init()'s synchronous boot device-load, so the
// in-initAllSupport greeting redraws fire ONLY during boot -- not on a post-boot settings refresh
// (which runs on the IO thread with the menu showing; an ungated redraw would flash the boot logo).
static int gBootInProgress = 0;

int oplIsBootInProgress(void)
{
    return gBootInProgress;
}

void initAllSupport(int force_reinit)
{
    // Keep support probing in its established order. GUI page order is handled independently by
    // menuAppendItem(), so visual tab placement does not change device initialization timing.
    guiSetBootStatus(_l(_STR_BOOT_SCANNING_BDM));
    if (gBootInProgress)
        guiRenderGreetingScreen();
    bdmEnumerateDevices();
    if (gBootInProgress)
        diagLog("boot: BDM start stage done (enabled drivers may still be loading in the background)");

    guiSetBootStatus(_l(_STR_BOOT_SCANNING_NET));
    if (gBootInProgress)
        guiRenderGreetingScreen();
    initSupport(ethGetObject(0), ETH_MODE, force_reinit || (gNetworkStartup >= ERROR_ETH_SMB_CONN));
    // UDPFS filesystem shares the single NIC with SMB/UDPBD; its start-mode gate (initSupport) is live
    // only when gNetworkProtocol == NET_PROTO_UDPFS, so exactly one network tab is ever enabled.
    initSupport(udpfsGetObject(0), UDPFS_MODE, force_reinit);
    // HTTP shares that same NIC and the same one-tab-at-a-time rule: its start-mode gate is live
    // only when gNetworkProtocol == NET_PROTO_HTTP.
    initSupport(httpGetObject(0), HTTP_MODE, force_reinit);
    if (gBootInProgress)
        diagLog("boot: network start stage done (enabled drivers may still be loading in the background)");

    guiSetBootStatus(_l(_STR_BOOT_SCANNING_HDD));
    if (gBootInProgress)
        guiRenderGreetingScreen();
    initSupport(hddGetObject(0), HDD_MODE, force_reinit);
    initSupport(appGetObject(0), APP_MODE, force_reinit);
    initSupport(favGetObject(0), FAV_MODE, force_reinit);
    if (gBootInProgress)
        diagLog("boot: HDD, apps and favourites start stage done (enabled drivers may still be loading in the background)");

    guiSetBootStatus(_l(_STR_BOOT_SCANNING_MC));
    if (gBootInProgress)
        guiRenderGreetingScreen();
    LOG("BOOT scan: MMCE initSupport begin\n");
    initSupport(mmceGetObject(0), MMCE_MODE, force_reinit);
    LOG("BOOT scan: MMCE initSupport done\n");
    if (gBootInProgress)
        diagLog("boot: MMCE start stage done (enabled drivers may still be loading in the background)");

    // Arm the MMCE GameID transport at boot and on every settings apply -- instead
    // of deferring it to itemLaunchMMCE.
    if (gMMCEEnableGameID && !gBootInProgress)
        mmceArmGameIDTransport();
}

void deinitAllSupport(int exception, int modeSelected)
{
    for (int i = 0; i < MODE_COUNT; i++) {
        if (list_support[i].support != NULL)
            moduleCleanup(&list_support[i], exception, modeSelected);
    }
}

void oplQueueLibraryDeviceUpdates(void)
{
    // libViewMarkAllDirty() is intentionally side-effect-free because it also runs during early config
    // loading, before the IO worker and device modules are ready. Runtime callers must explicitly
    // enqueue the enabled pages. This matters most for HDD, whose updateDelay=-1 means a dirty view
    // otherwise keeps displaying the old submenu indefinitely while rendering uses the new view.
    for (int i = 0; i < MODE_COUNT; i++) {
        item_list_t *support = list_support[i].support;
        if (support != NULL && support->enabled && support->mode != FAV_MODE && support->mode != APP_MODE &&
            libViewSupported(support->mode, LIB_VIEW_PS1))
            ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
    }
}

// For resolving the mode, given an app's path
int oplPath2Mode(const char *path)
{
    char appsPath[64];
    const char *blkdevnameend;
    int i, blkdevnamelen;
    item_list_t *listSupport;

    if (path == NULL || path[0] == '\0')
        return -1;

    // Custom core paths are normalized to these live filesystem namespaces before teardown.
    // Classify them directly instead of depending on a visible/enabled menu page: a user may keep
    // Neutrino/POPSTARTER on a device whose games tab is disabled.
    if (!strncasecmp(path, "mmce", 4))
        return MMCE_MODE;
    if (!strncasecmp(path, "smb", 3))
        return ETH_MODE;
    if (!strncasecmp(path, "udpfs:", 6))
        return UDPFS_MODE;
    if (!strncasecmp(path, "pfs", 3) || !strncasecmp(path, "hdd", 3))
        return HDD_MODE;
    if (!strncasecmp(path, "http://", 7) || !strncasecmp(path, "https://", 8))
        return HTTP_MODE;
    if (!strncasecmp(path, "mass", 4)) {
        const char *p = path + 4;
        int slot = 0, haveDigit = 0;
        while (*p >= '0' && *p <= '9') {
            haveDigit = 1;
            slot = slot * 10 + (*p++ - '0');
        }
        if (haveDigit && *p == ':' && slot >= BDM_MODE && slot <= BDM_MODE_LAST)
            return slot;
    }

    for (i = 0; i < MODE_COUNT; i++) {
        listSupport = list_support[i].support;
        if ((listSupport != NULL) && (gsOf(listSupport)->hasPrefix())) {
            char *prefix = gsOf(listSupport)->prefix(listSupport);
            if (prefix == NULL || prefix[0] == '\0')
                continue;
            snprintf(appsPath, sizeof(appsPath), "%sAPPS", prefix);

            blkdevnameend = strchr(appsPath, ':');
            if (blkdevnameend != NULL) {
                blkdevnamelen = (int)(blkdevnameend - appsPath);

                if (strncmp(path, appsPath, blkdevnamelen) == 0)
                    return listSupport->mode;
            }
        }
    }

    return -1;
}


// ----------------------------------------------------------
// ----------------------- Updaters -------------------------
// ----------------------------------------------------------
static void updateMenuFromGameList(opl_io_module_t *mdl)
{
    int viewTransition;

    guiExecDeferredOps();

    // Remember where the cursor was, so a rebuild does not dump the user back at the top of
    // the list (#589). Keyed on the game's startup id, not its row index: a rescan is exactly
    // the thing that renumbers rows. Folder rows have no startup and are skipped.
    char keepStartup[128] = {0};
    if (mdl->menuItem.current != NULL && !mdl->menuItem.current->item.isFolder) {
        const char *s = gsOf(mdl->support)->startup(mdl->support, mdl->menuItem.current->item.id);
        if (s != NULL)
            snprintf(keepStartup, sizeof(keepStartup), "%s", s);
    }

    cfgTitleForget(mdl->support); // publish no titles before replacing the source rows
    clearMenuGameList(mdl);
    // An L3 transition is staged while the old submenu is still live. Commit only after that submenu
    // is gone, then every array read below and every newly queued row belongs to the same new view.
    viewTransition = libViewPending(mdl->support->mode);
    libViewCommitPending(mdl->support->mode);

    // NO cacheInvalidateFailMemo() HERE, and this is deliberate -- it used to sit on this line.
    //
    // The reasoning was "a rebuilt list is the natural retry point for art that failed earlier". The
    // flaw is what rebuilds a list: not just the user changing view, but every background device
    // rescan that lands while they browse. So the memo was thrown away every few seconds and a game
    // with no cover was re-probed forever, which is the expensive case -- a lookup for a file that
    // does not exist must walk the whole directory before it can answer.
    //
    // The retry now hangs off the event that can actually make absent art exist: a device generation
    // change (hotplug, or a Game Sources apply) in menuUpdateHook, plus the deliberate
    // applyConfig invalidation. master keeps only the latter; ours keeps the hotplug case too, so
    // plugging in a stick with new art still picks it up without a reboot.

    const char *temp = NULL;
    if (gRememberLastPlayed)
        configGetStr(configGetByType(CONFIG_LAST), "last_played", &temp);

    // refresh device icon and text (for bdm)
    mdl->menuItem.icon_id = gsOf(mdl->support)->iconId(mdl->support);
    mdl->menuItem.text_id = gsOf(mdl->support)->textId(mdl->support);

    // read the new game list
    struct gui_update_t *gup = NULL;
    int count = gsOf(mdl->support)->update(mdl->support);
    hiddenReload(mdl->support); // the device's hidden list, read with its games

#ifdef RETROACHIEVEMENTS
    // RA badges: recompute on every list rebuild. This runs on the I/O thread (itemUpdate
    // above is the device scan), where the <device>RA/ stat() calls are safe.
    raBadgeRefresh(mdl->support, count);
#endif

    // Folder browsing: while inside a subfolder, show the breadcrumb ("Device: RPGs/SNES") as the page
    // title. folderGetSub() points at persistent static state, so the char* stays valid. At the device
    // root the device name set above stands. Only one device is ever inside a folder at a time (the
    // browse state resets to root on device switch), so a single static crumb buffer is safe.
    const int folderMode = folderModeSupported(mdl->support->mode);
    if (folderMode && folderDepth(mdl->support->mode) > 0) {
        static char folderCrumb[256]; // generous headroom for a localized device name + subpath
        snprintf(folderCrumb, sizeof(folderCrumb), "%s: %s", _l(gsOf(mdl->support)->textId(mdl->support)), folderGetSub(mdl->support->mode));
        mdl->menuItem.text = folderCrumb;
        mdl->menuItem.text_id = -1;
    }

    if (count > 0) {
        // Folder browsing: emit in TWO passes -- folders first, then the games -- so folders always sit
        // at the TOP of the list regardless of the Auto Sort setting. Each row keeps id=i (its array
        // index), so favourites still resolve by id+text no matter the display order. Devices that never
        // produce folder rows use a single pass, byte-for-byte the original behaviour. When Auto Sort is
        // on, submenuSort's folder-first comparator keeps this grouping while sorting each group.
        int passes = (gEnableFolderNav && folderMode) ? 2 : 1;
        int pass, i;

        for (pass = 0; pass < passes; ++pass) {
            for (i = 0; i < count; ++i) {
                // Flag folder rows so the dispatch descends and the renderer marks them. Only the
                // loose-file tree devices return base_game_info_t from itemGet, so gate on the mode.
                int isFolderRow = 0;
                if (folderMode && gsOf(mdl->support)->hasGame()) {
                    base_game_info_t *ginfo = (base_game_info_t *)gsOf(mdl->support)->game(mdl->support, i);
                    isFolderRow = (ginfo != NULL && ginfo->format == GAME_FORMAT_FOLDER);
                }
                // Pass 0 emits folders, pass 1 emits games (single-pass mode emits every row).
                if (passes == 2 && ((pass == 0) != (isFolderRow != 0)))
                    continue;
                // Hide Game: a hidden game leaves every list until Show Hidden Games lists it again.
                if (!isFolderRow && !gShowHidden && hiddenRow(mdl->support, i))
                    continue;

                gup = guiOpCreate(GUI_OP_APPEND_MENU);
                if (!gup) // OOM: skip this entry rather than deref NULL
                    continue;

                gup->menu.menu = &mdl->menuItem;
                gup->menu.subMenu = &mdl->subMenu;

                gup->submenu.icon_id = -1;
                gup->submenu.id = i;
#ifdef RETROACHIEVEMENTS
                {
                    // The source name stays the row's identity; the badge is copied now, before the next
                    // raBadgeRefresh frees the cache it lives in. NULL means plain name.
                    const char *badge = raBadgeText(mdl->support, i);
                    gup->submenu.text = mdl->support->mode == FAV_MODE ? const_cast<char *>(favGetItemDisplay(i)) : gsOf(mdl->support)->name(mdl->support, i);
                    gup->submenu.raText = badge != NULL ? strdup(badge) : NULL;
                    /* Resolve the cover mark HERE, on the I/O thread that owns the badge cache,
                       and carry the answer to the renderer as a plain int. */
                    gup->submenu.raBadged = (badge != NULL);
                }
#else
                // Favourites/All Games rows draw their display text: a title on two sources names its source.
                gup->submenu.text = mdl->support->mode == FAV_MODE ? const_cast<char *>(favGetItemDisplay(i)) : gsOf(mdl->support)->name(mdl->support, i);
#endif
                gup->submenu.favourited = mdl->support->mode == FAV_MODE ? favGetItemStarred(i) : 0;
                gup->submenu.text_id = -1;
                gup->submenu.selected = 0;
                gup->submenu.autoStart = 0;
                gup->submenu.isFolder = isFolderRow;
                gup->submenu.lastPlayed = (gAutosort == AUTOSORT_LAST_PLAYED && !isFolderRow) ? recentRankForRow(mdl->support, i, gup->submenu.text) : 0;

                // Neither auto-select targets a folder row (no startup). The remembered cursor
                // wins over last-played: it is where the user actually was. Only the persisted
                // Last Played match may release the Auto Start countdown; cursor restoration must
                // never turn a menu rebuild into an automatic launch.
                if (!isFolderRow) {
                    const char *st = gsOf(mdl->support)->startup(mdl->support, i);
                    if (st != NULL) {
                        if (keepStartup[0] && strcmp(keepStartup, st) == 0) {
                            gup->submenu.selected = 1;
                        } else if (!keepStartup[0] && gRememberLastPlayed && temp && strcmp(temp, st) == 0) {
                            gup->submenu.selected = 1;
                            gup->submenu.autoStart = 1;
                        }
                    }
                }

                guiDeferUpdate(gup);
            }
        }
    }

    // Complete the view transition before the optional slow per-game CFG pass.
    if (viewTransition)
        libViewFinishPending(mdl->support->mode);
    cfgTitleRefresh(mdl->support, count);

    // The Favourites Recent stop is already in play order; sorting it would undo the point.
    if (gAutosort && !(mdl->support->mode == FAV_MODE && libViewActive(FAV_MODE) == LIB_VIEW_RECENT)) {
        gup = guiOpCreate(GUI_OP_SORT);
        if (gup) { // OOM: an unsorted list beats dereferencing NULL, same as the append loop above
            gup->menu.menu = &mdl->menuItem;
            gup->menu.subMenu = &mdl->subMenu;
            guiDeferUpdate(gup);
        }
    }
}

void menuDeferredUpdate(void *data)
{
    short int *mode = static_cast<short int *>(data);

    opl_io_module_t *mod = &list_support[*mode];
    if (!mod->support)
        return;

    // An L3 flip changes which list this page shows, not which devices exist, so it cannot change
    // APPS -- a walk of every device's APPS folder, MMCE over SIO2 included. Read it before the
    // rebuild below commits the view. (Only L3 stages a view; libViewBdmAttach never does, so a
    // device that attaches still rebuilds APPS.)
    int viewFlip = libViewPending(mod->support->mode);

    // see if we have to update
    if (gsOf(mod->support)->needsUpdate(mod->support)) {
        updateMenuFromGameList(mod);

        // If other modes have been updated, then the apps list should be updated too.
        // Exclude FAV: a FAV rebuild marking apps dirty would re-trigger the FAV resync below
        // (an apps refresh calls loadFavourites), looping forever once favNeedsUpdate fires.
        if (mod->support->mode != APP_MODE && mod->support->mode != FAV_MODE && !viewFlip) {
            gShouldAppsUpdate = 1;

            // ...and SCHEDULE the pass that consumes that flag. Raising it is not enough: APPS has
            // updateDelay > 0, so the only thing that ever queued an APP_MODE deferred update in the
            // background was menuUpdateHook's `gAutoRefresh && longIdle` branch -- and automatic
            // refresh is OFF by default. With it off the flag simply sat set until the user pressed
            // SELECT.
            //
            // That is the reported bug (eliminator1403): USB and APPS both on Auto, and the APPS tab
            // comes up EMPTY every boot. bdmEnumerateDevices() is asynchronous -- it only queues
            // bdmLoadBlockDeviceModules -- so the per-slot bdmPrefix is still empty when the boot's
            // one APP_MODE update runs, and oplScanApps skips every device-less prefix (#253). The
            // stick then attaches, BDM publishes its list, this line raises gShouldAppsUpdate... and
            // nothing ever asked APPS to look at it. Pressing SELECT was the only consumer.
            //
            // Queue it here instead, exactly the way loadFavourites() below schedules FAV's rebuild.
            // Bounded, not a storm: itemNeedsUpdate returns 1 only on a REAL source change (a
            // never-connected BDM slot returns 0 early), and an APPS pass that is still waiting
            // reads gShouldAppsUpdate when it runs, so a second one is not queued behind it (two
            // sticks attaching together used to queue two). Enqueueing from this IO worker is the
            // established pattern here (bdmNeedsUpdate and loadFavourites both do it): it takes
            // only gEndSemaId, which the worker does not hold while a handler runs.
            opl_io_module_t *appMod = &list_support[APP_MODE];
            if (appMod->support != NULL && appMod->support->enabled)
                ioPutRequestUnlessWaiting(IO_MENU_UPDATE_DEFFERED, &appMod->support->mode);
        }

        // A source-list refresh may expose newly-loaded items to validate favourites
        // against. Re-sync the FAV tab (cheap/idempotent; skipped when FAV is disabled).
        // An L3 flip still counts: favourites resolve against a device's PS1 or PS2 list
        // (favResolveStoredId), and a flip can fill one that was never scanned.
        if ((gFAVStartMode || gAllGames) && mod->support->mode != FAV_MODE)
            loadFavourites();
    } else if (libViewPending(mod->support->mode)) {
        // A device can reject a scan before it consumes the view-dirty flag (for example, MMCE with
        // no card inserted). Still retire the old rows and commit the retained destination so input
        // does not remain locked and the next successful device scan uses the user's chosen view.
        guiExecDeferredOps();
        clearMenuGameList(mod);
        libViewCommitPending(mod->support->mode);
        libViewFinishPending(mod->support->mode);
    }
}

constexpr int MENU_GENERAL_UPDATE_DELAY = 60;
// Minimum wall-clock gap between background rescans of the SAME updateDelay==0 device (Fix B). At
// ~2 s this drops the steady SIO2/mass enumeration rate well below the old every-60-frames cadence,
// cutting MX4SIO bus contention, while a real device change still refreshes immediately
// (BdmGeneration bypass below). clock() = microseconds.
#define MENU_BG_RESCAN_MIN_INTERVAL_TICKS (2 * CLOCKS_PER_SEC)
/*
  Idle time required before STEADY-STATE background probes may run at all, in frames (~1 s NTSC).

  The short gate (MENU_MIN_INACTIVE_FRAMES = 8, ~133 ms) is tuned for "the user paused between
  inputs" -- right for waking cover-art loads, and WRONG for device probes. Frame analysis of a
  tester capture (#271) showed why: tapping through the list at a steady ~4.3 taps/s leaves
  ~130 ms release gaps, which sit exactly AT the short gate, so throttled probes leaked into
  active navigation and a probe firing into a tap gap contends with the pads on SIO2 -- the tap
  landing inside the resulting read-miss burst is eaten.

  60 frames of REAL idleness is far above any tap gap, so probes never fire mid-navigation, and a
  parked console still probes within a second of the user stopping. Hotplug stays immediate-ish:
  the BdmGeneration bypass below keeps the SHORT gate, so a plugged device is picked up in the
  next tap gap rather than after a full second.
*/
constexpr int MENU_BG_RESCAN_MIN_INACTIVE_FRAMES = 60;

void menuUpdateHook()
{
    int i;

    // if timer exceeds some threshold, schedule updates of the available input sources
    frameCounter++;

    // IO scans queue warnings; rendering belongs to the GUI thread.
    udpfsShowPendingNotice();

    // Keep background refresh work out of the shared IO queue while the user is actively navigating.
    if (guiInactiveFrames < MENU_MIN_INACTIVE_FRAMES)
        return;

    // Let the current queue drain before adding background refresh work.
    if (ioHasPendingRequests())
        return;

    // Yield to cover art still on its way. The rescans below enqueue one device probe per BDM page
    // onto the SAME single IO worker the art loads use, so without this gate a settle puts a batch
    // of probes AHEAD of the covers the user is looking at -- and since the gate opens again on the
    // next idle window, every scroll-and-stop cycle adds another batch and the art falls further
    // behind (hardware: ~15 s to fill after a few cycles). Device detection loses nothing: this
    // only defers a periodic rescan by the length of the art queue, and a real hotplug still comes
    // through BdmGeneration below once art is idle. Matches the fork.
    if (cacheHasPendingArt())
        return;

    // Steady-state probes additionally require REAL idleness (see the define): a tap gap passes the
    // short gate above, and a probe fired into it contends with the pads on SIO2 and eats the next
    // tap (#271). Event-driven work (the genChanged hotplug bypass below) keeps the short gate.
    int longIdle = (guiInactiveFrames >= MENU_BG_RESCAN_MIN_INACTIVE_FRAMES);

    // schedule updates of all the list handlers
    // Periodic background rescans. Both steady-state rescans enqueue standard requests and are
    // rendered with the unified busy overlay.
    if (gAutoRefresh && longIdle) {
        for (i = 0; i < MODE_COUNT; i++) {
            if ((list_support[i].support && list_support[i].support->enabled) && ((list_support[i].support->updateDelay > 0) && (frameCounter % list_support[i].support->updateDelay == 0)))
                ioPutRequest(IO_MENU_UPDATE_DEFFERED, &list_support[i].support->mode);
        }
    }

    // Schedule updates of the every-frame (updateDelay==0) list handlers -- all BDM/MX4SIO. These
    // enumerate the SIO2/mass bus, so throttle each to a minimum wall-clock interval instead of
    // firing every MENU_GENERAL_UPDATE_DELAY frames. A genuine BDM device change (hotplug/removal
    // via BdmGeneration, or a Device-Settings apply) bypasses the throttle so detection stays
    // immediate.
    if (frameCounter % MENU_GENERAL_UPDATE_DELAY == 0) {
        unsigned int gen = bdmGetGeneration();
        int genChanged = (gen != lastSeenBdmGeneration);
        clock_t now = clock();

        // "Storage changed underneath us" is the ONLY routine event that can make previously-absent
        // art exist, so it is the only routine event that should re-open the question. The memo used
        // to be cleared by updateMenuFromGameList instead -- i.e. on every list rebuild, including the
        // background rescans that fire while the user simply browses -- which meant a missing cover
        // was re-probed over and over forever. That is the expensive direction: a lookup for a file
        // that is not there has to walk the whole directory before it can say so, and this build
        // measured art loads spanning 82 ms to 2922 ms with the decoded size held constant.
        if (genChanged) {
            cacheInvalidateFailMemo();
            artIndexInvalidate();   // the listing is only as current as the device it was read from
            vcdInvalidateGameIds(); // ...and neither is a PS1 disc id read off the old media
        }
        // Consume the generation bump only if every hotplug-driven enqueue is ACCEPTED:
        // ioPutRequest can fail (queue blocked during teardown, allocation failure), and
        // committing lastSeenBdmGeneration up front would silently eat the one immediate-rescan
        // event a hotplug gets. Leaving the generation unconsumed makes the next 1 s tick retry
        // the whole event instead.
        int genConsumed = 1;
        for (i = 0; i < MODE_COUNT; i++) {
            if ((list_support[i].support && list_support[i].support->enabled) && (list_support[i].support->updateDelay == 0)) {
                int mode = list_support[i].support->mode;
                // elapsed form is single-wrap-safe; zero-initialized timestamp allows one immediate rescan.
                // genChanged (hotplug / Device-Settings apply) deliberately requires NEITHER longIdle:
                // it fires precisely BECAUSE a device changed, the scan populates a page the
                // user is waiting on, and a plugged device should be noticed in the
                // next tap gap rather than after a second of stillness (#271).
                // Slots that HAVE a device are probed every tick -- that poll is what notices a
                // removal, and thinning it (rebuild-96) is how a disconnect went unseen and the BDM
                // stack later vanished. Slots that have NEVER held a device are probed far less
                // often, because that probe is only a net for a missed attach event (attach is
                // event-driven via bdmEventHandler, and a generation bump bypasses this gate
                // entirely).
                //
                // Why it matters, measured: the hardware HUD showed "Q12 A0" -- twelve cover loads
                // queued and NONE executing, i.e. the single IO worker was inside something else
                // while the art waited. That something is this batch: eight fileXioDopen round
                // trips per tick on a rig with one stick, seven of them for slots that have been
                // empty since boot. It is also the most plausible source of the spurious
                // "device replugged" the user sees while simply navigating, since those probes poke
                // the USB stack for devices that are not there.
                if (!genChanged && mode >= BDM_MODE && mode <= BDM_MODE_LAST && !bdmSlotEverConnected(mode) &&
                    (frameCounter % (MENU_GENERAL_UPDATE_DELAY * BDM_EMPTY_SLOT_PROBE_RATIO)) != 0)
                    continue;

                if (genChanged || (longIdle && (now - lastBgRescan[mode]) >= MENU_BG_RESCAN_MIN_INTERVAL_TICKS)) {
                    // Stamp the throttle only on an accepted request, so a rejected one retries on
                    // the next tick instead of being silently skipped for a full interval.
                    int accepted = (ioPutRequest(IO_MENU_UPDATE_DEFFERED, &list_support[i].support->mode) == IO_OK);
                    if (accepted)
                        lastBgRescan[mode] = now;
                    else if (genChanged)
                        genConsumed = 0;
                }
            }
        }
        if (genConsumed)
            lastSeenBdmGeneration = gen;
    }
}

void clearErrorMessage(void)
{
    // reset the original frame hook
    frameCounter = 0;
    errorMessageStringId = -1;
    guiSetFrameHook(&menuUpdateHook);
}

static void errorMessageHook()
{
    guiMsgBox(errorMessage, 0, NULL);
    clearErrorMessage();
}

// Language strings can choose the order of the supported substitutions, but are never handed to
// printf directly. Unknown conversion sequences remain literal text instead of consuming an
// argument that this caller did not provide.
static void formatErrorMessage(const char *format, const char *path, int error, int allowPath, int allowError)
{
    char errorText[16];
    size_t used = 0;

    if (format == NULL) {
        errorMessage[0] = '\0';
        return;
    }

    snprintf(errorText, sizeof(errorText), "%d", error);
    while (*format != '\0' && used < sizeof(errorMessage) - 1) {
        const char *replacement = NULL;

        if (*format != '%') {
            errorMessage[used++] = *format++;
            continue;
        }

        format++;
        if (*format == '\0') {
            errorMessage[used++] = '%';
            break;
        }
        if (*format == '%') {
            errorMessage[used++] = *format++;
            continue;
        }
        if (*format == 's' && allowPath)
            replacement = path ? path : "";
        else if ((*format == 'd' || *format == 'i') && allowError)
            replacement = errorText;

        if (replacement != NULL) {
            format++;
            while (*replacement != '\0' && used < sizeof(errorMessage) - 1)
                errorMessage[used++] = *replacement++;
        } else {
            errorMessage[used++] = '%';
            if (used < sizeof(errorMessage) - 1)
                errorMessage[used++] = *format;
            format++;
        }
    }
    errorMessage[used] = '\0';
}

void setErrorMessageWithCode(int strId, int error)
{
    formatErrorMessage(_l(strId), NULL, error, 0, 1);
    errorMessageStringId = strId;
    guiSetFrameHook(&errorMessageHook);
}

// The detail suffix is an internal enum name ("PS2FS_LOAD_FAILURE"), untranslated by construction,
// so a plain release gets the localized message alone and behaves exactly like setErrorMessageWithCode.
// OPLDIAG=1 appends the reason so a tester's screenshot names WHICH probe failed, not just the code.
void setErrorMessageWithCodeAndDetail(int strId, int error, const char *detail)
{
#ifdef __OPLDIAG
    size_t used;
#else
    (void)detail;
#endif

    formatErrorMessage(_l(strId), NULL, error, 0, 1);
#ifdef __OPLDIAG
    used = strlen(errorMessage);
    if (detail != NULL && detail[0] != '\0' && used < sizeof(errorMessage) - 1)
        snprintf(&errorMessage[used], sizeof(errorMessage) - used, "\nDiagnostic reason: %s", detail);
#endif
    errorMessageStringId = strId;
    guiSetFrameHook(&errorMessageHook);
}

void setErrorMessagePathCode(int strId, const char *path, int error)
{
    formatErrorMessage(_l(strId), path, error, 1, 1);
    errorMessageStringId = strId;
    guiSetFrameHook(&errorMessageHook);
}

void clearErrorMessageIf(int strId)
{
    if (errorMessageStringId == strId)
        clearErrorMessage();
}

void reset(void)
{
    sysReset(SYS_LOAD_MC_MODULES | SYS_LOAD_USB_MODULES | SYS_LOAD_ISOFS_MODULE);
    // The IOP lost every module; the sources' own "loaded" latches must forget them too, or the
    // next initAllSupport skips loading them (NBD server stop left iLink/MX4SIO/HDD/UDPBD dead).
    bdmIopWasReset();
    hddIopWasReset();
    mmceIopWasReset();
    udpfsIopWasReset();

    bootTrace("memory card library");
    mcInit(MC_TYPE_XMC);
}

static void moduleCleanup(opl_io_module_t *mod, int exception, int modeSelected)
{
    if (!mod->support)
        return;

    // Shutdown if not required anymore.
    if ((mod->support->mode != modeSelected) && (modeSelected != IO_MODE_SELECTED_ALL) && (modeSelected != IO_MODE_SELECTED_ALL_SPARE)) {
        gsOf(mod->support)->shutdown(mod->support);
    } else {
        gsOf(mod->support)->cleanUp(mod->support, exception);
    }

    clearMenuGameList(mod);
}

// 1 while deinit() tears down for exit/poweroff, 0 for a game/app launch. Consumed by device shutdowns
// that must behave differently on the launch path (hddShutdown keeps DEV9 powered so the post-deinit
// POPSTARTER.ELF read from the ATA-backed massN: mount still works; ee_core/POPSTARTER reset the IOP
// themselves, so skipping the power-off on launches leaks nothing).
int gDeinitTerminal = 0;

// Set by deinit/deinitEx when cacheEnd() gave up waiting for the art worker, i.e. a thread is still
// inside a device read while the teardown proceeds. hddCleanUp reads it and skips the two operations
// that would pull the ground out from under that thread -- unmounting pfs and PDIOC_CLOSEALL, which
// closes EVERY pfs descriptor including the one the BGM decoder is streaming from. Leaving them is
// free: every caller of deinit hands off to an ELF that resets the IOP anyway.
int gArtAbandoned = 0;

// deinitEx: deinit for the Neutrino keep-IOP handoff -- spares TWO modes' mounts (the game
// device AND the device holding neutrino.elf), since Neutrino reads its -cwd config/modules
// and the ISO through OPL's live mounts before performing its own IOP reset.
// Bound for the teardown drains below. ioBlockOps() waits UNBOUNDED, which is correct only if the
// queue is guaranteed to finish -- and it is not: cover art is queued one request per visible row on
// the same single-threaded FIFO, so on a slow device (a network share especially) "Exit to Browser"
// or a game launch sat waiting for hundreds of art reads with nothing on screen to explain it. The
// NBD preflight already passes a bound for exactly this reason; the exit and launch paths never got
// one. Abandoning a pending art read here is safe: every one of these callers is on its way to an
// IOP reset, and the art request only ever writes into its own cache entry.
// Two budgets, keyed off gDeinitTerminal (fork parity). EXIT can afford to abandon quickly -- the
// console is going away. A LAUNCH gets ~10 s: a healthy slow device drains well under that, but a
// wedged art read must not freeze the loading screen forever, and a straggler request cannot
// survive the handoff target's own IOP reset anyway (ee_core / Neutrino / POPSTARTER all reset it).
//
// ⚠ THE UNITS ARE NOT MILLISECONDS. ioBlockOpsTimed spends one delay(1) per tick, and delay() is a
// NOP spin of 0x01000000 iterations x 4 nops (util.c) -- about a QUARTER SECOND on a 294 MHz EE, not
// a millisecond. The old values were written as if they were ms, so the "~10 s" launch budget above
// was really on the order of forty MINUTES and the "1 s" exit budget several minutes. Neither was a
// bound in any sense that mattered; a launch pressed with covers queued simply waited for all of
// them. Values below are the ORIGINAL INTENT expressed in real ticks. If delay() is ever given a
// real time base, convert these with it -- do not just scale the numbers again.
constexpr int EXIT_IO_DRAIN_TICKS = 4;    // ~1 s
constexpr int LAUNCH_IO_DRAIN_TICKS = 40; // ~10 s, as the paragraph above always meant

void deinitEx(int exception, int modeSelected, int modeSelected2)
{
    gDeinitTerminal = (modeSelected == IO_MODE_SELECTED_ALL || modeSelected == IO_MODE_SELECTED_NONE);

    // Say something BEFORE the blocking teardown below, because it can hold this thread for the
    // whole of LAUNCH_IO_DRAIN_TICKS (~10 s) and nothing repaints while it does. Measured on
    // hardware: roughly seven seconds of black screen between pressing X and the game's first
    // output, with a loading spinner visible for only the first instant. A user cannot tell that
    // from a freeze, and reasonably concludes the console has hung.
    //
    // This is the one place worth doing it: every launch and the exit path all funnel through here,
    // so a per-launch-leg banner would have to be repeated in each and would still miss some. The
    // frame flips and then persists on its own -- nothing draws again until the handoff -- so one
    // call covers the entire wait.
    guiRenderTextScreen(_l(_STR_PLEASE_WAIT));
    if (gLaunchDiag)
        launchDiagMark(5); // "Please Wait" is up -- the io/art drain below runs next

    // Give up on the covers still QUEUED before draining. The drain waits on the ioman LIST, and the
    // worker keeps servicing it regardless of isIOBlocked, so without this the handoff pays for every
    // queued cover to be read off the game device first -- for a menu guiEnd() is about to destroy.
    cacheShutdownArtLoads();

    // JOIN the art worker before anything unmounts a device under it. cacheShutdownArtLoads
    // above only drops the QUEUE and flags the in-flight request; the thread itself may still
    // be parked inside a device read, and deinitAllSupport below is about to unmount that
    // device. Bounded, and a thread that will not come back is ABANDONED rather than
    // terminated -- killing one inside fileXio leaves the shared IOP RPC channel half-used for
    // every later caller, from any thread.
    // ORDER: block+drain the io worker FIRST, then join art. Art left the ioman queue in rebuild-152,
    // so nothing here needs art gone before the drain -- and doing it this way stops the two workers
    // competing for the device during the join, which is the whole point of the join.
    //
    // Stop the music BEFORE anything tears down the device it is streaming from -- bgm.ogg normally
    // lives on the same stick, HDD or share as the games. Issue #382 is that race on the network.
    // Signal only: the thread JOIN stays in audioEnd() where it has always been. rebuild-163 moved
    // the whole blocking bgmStop() here and froze exit from a UDPFS boot; the order was right, the
    // wait was not.
    bgmQuiesce();

    // block all io ops, wait for the ones still running to finish (BOUNDED -- see the note above)
    ioBlockOpsTimed(1, gDeinitTerminal ? EXIT_IO_DRAIN_TICKS : LAUNCH_IO_DRAIN_TICKS);

    // Drain is finished; now fade the status screen to black before device/GS teardown.
    guiFadeOutForHandoff(_l(_STR_PLEASE_WAIT));

    // JOIN the art worker before anything unmounts a device under it. Its RESULT IS LOAD-BEARING and
    // used to be thrown away: a 0 means a thread is still inside a device read, and the teardown
    // below is about to unmount that device and close its descriptors.
    gArtAbandoned = !cacheEnd(gDeinitTerminal);
    guiExecDeferredOps();
    if (gLaunchDiag)
        launchDiagMark(6); // io + art drain done -- the per-module teardown below runs next

#ifdef PADEMU
    ds34usb_reset();
    ds34bt_reset();
#endif
    if (exception & KEEPIOP_EXCEPTION)
        unloadPadsEx(1);
    else
        unloadPads();

    for (int i = 0; i < MODE_COUNT; i++) {
        if (list_support[i].support != NULL) {
            int spared = (modeSelected2 >= 0 && list_support[i].support->mode == modeSelected2);
            moduleCleanup(&list_support[i], exception, spared ? modeSelected2 : modeSelected);
        }
    }

    if (gLaunchDiag)
        launchDiagMark(7); // per-module teardown done -- audioEnd/ioEnd run next
    audioEnd();
    ioEnd();
    guiEnd();
    menuEnd();
    lngEnd();
    fntEnd();
    thmEnd();
    rmEnd();
    menuWatchStop(); // diagnostic builds: no VBlank handler or exception hooks survive the handoff
    configEnd();
    if (gLaunchDiag)
        launchDiagMark(8); // deinitEx finished -- sysLaunchNeutrino runs next
}

void deinit(int exception, int modeSelected)
{
    gDeinitTerminal = (modeSelected == IO_MODE_SELECTED_ALL || modeSelected == IO_MODE_SELECTED_NONE);

    // Say something BEFORE the blocking teardown below, because it can hold this thread for the
    // whole of LAUNCH_IO_DRAIN_TICKS (~10 s) and nothing repaints while it does. Measured on
    // hardware: roughly seven seconds of black screen between pressing X and the game's first
    // output, with a loading spinner visible for only the first instant. A user cannot tell that
    // from a freeze, and reasonably concludes the console has hung.
    //
    // This is the one place worth doing it: every launch and the exit path all funnel through here,
    // so a per-launch-leg banner would have to be repeated in each and would still miss some. The
    // frame flips and then persists on its own -- nothing draws again until the handoff -- so one
    // call covers the entire wait.
    guiRenderTextScreen(_l(_STR_PLEASE_WAIT));
    if (gLaunchDiag)
        launchDiagMark(5); // "Please Wait" is up -- the io/art drain below runs next

    // Give up on the covers still QUEUED before draining. The drain waits on the ioman LIST, and the
    // worker keeps servicing it regardless of isIOBlocked, so without this the handoff pays for every
    // queued cover to be read off the game device first -- for a menu guiEnd() is about to destroy.
    cacheShutdownArtLoads();

    // JOIN the art worker before anything unmounts a device under it. cacheShutdownArtLoads
    // above only drops the QUEUE and flags the in-flight request; the thread itself may still
    // be parked inside a device read, and deinitAllSupport below is about to unmount that
    // device. Bounded, and a thread that will not come back is ABANDONED rather than
    // terminated -- killing one inside fileXio leaves the shared IOP RPC channel half-used for
    // every later caller, from any thread.
    // ORDER: block+drain the io worker FIRST, then join art. Art left the ioman queue in rebuild-152,
    // so nothing here needs art gone before the drain -- and doing it this way stops the two workers
    // competing for the device during the join, which is the whole point of the join.
    //
    // Stop the music BEFORE anything tears down the device it is streaming from -- bgm.ogg normally
    // lives on the same stick, HDD or share as the games. Issue #382 is that race on the network.
    // Signal only: the thread JOIN stays in audioEnd() where it has always been. rebuild-163 moved
    // the whole blocking bgmStop() here and froze exit from a UDPFS boot; the order was right, the
    // wait was not.
    bgmQuiesce();

    // block all io ops, wait for the ones still running to finish (BOUNDED -- see the note above)
    ioBlockOpsTimed(1, gDeinitTerminal ? EXIT_IO_DRAIN_TICKS : LAUNCH_IO_DRAIN_TICKS);

    // Drain is finished; now fade the status screen to black before device/GS teardown.
    guiFadeOutForHandoff(_l(_STR_PLEASE_WAIT));

    // JOIN the art worker before anything unmounts a device under it. Its RESULT IS LOAD-BEARING and
    // used to be thrown away: a 0 means a thread is still inside a device read, and the teardown
    // below is about to unmount that device and close its descriptors.
    gArtAbandoned = !cacheEnd(gDeinitTerminal);
    guiExecDeferredOps();
    if (gLaunchDiag)
        launchDiagMark(6); // io + art drain done -- the per-module teardown below runs next

#ifdef PADEMU
    ds34usb_reset();
    ds34bt_reset();
#endif
    if (exception & KEEPIOP_EXCEPTION)
        unloadPadsEx(1);
    else
        unloadPads();

    deinitAllSupport(exception, modeSelected);

    audioEnd();
    ioEnd();
    guiEnd();
    menuEnd();
    lngEnd();
    fntEnd();
    thmEnd();
    rmEnd();
    menuWatchStop(); // diagnostic builds: no VBlank handler or exception hooks survive the handoff
    configEnd();
}

void setDefaultColors(void)
{
    gDefaultBgColor[0] = RIPTOPL_PLASMA_BG_R;
    gDefaultBgColor[1] = RIPTOPL_PLASMA_BG_G;
    gDefaultBgColor[2] = RIPTOPL_PLASMA_BG_B;

    gDefaultTextColor[0] = 0xFF;
    gDefaultTextColor[1] = 0xFF;
    gDefaultTextColor[2] = 0xFF;

    gDefaultSelTextColor[0] = 0x00;
    gDefaultSelTextColor[1] = 0xAE;
    gDefaultSelTextColor[2] = 0xFF;

    gDefaultUITextColor[0] = 0x58;
    gDefaultUITextColor[1] = 0x68;
    gDefaultUITextColor[2] = 0xB4;

    // Seeded from the general text colour: out of the box titles look exactly as before, and
    // only diverge once the user actually picks something in the new row.
    gDefaultTitleColor[0] = gDefaultTextColor[0];
    gDefaultTitleColor[1] = gDefaultTextColor[1];
    gDefaultTitleColor[2] = gDefaultTextColor[2];
}

static void setDefaults(void)
{
    settingsApplyDefaults(); // plain settings: key, variable and default live in src/settings.cpp
    screenPosReset();
#ifdef RETROACHIEVEMENTS
    memset(gRAHostIp, 0, sizeof(gRAHostIp));
#endif

    for (int i = 0; i < MODE_COUNT; i++)
        clearIOModuleT(&list_support[i]);

    gAutoLaunchGame = NULL;
    gAutoLaunchBDMGame = NULL;
    gAutoLaunchDeviceData = NULL;
    gOPLPart[0] = '\0';
    // NULL is the only truthful pre-mount state. hddLoadSupportModules() uses non-NULL as the
    // proof that the persistent pfs0: data home is already mounted; seeding this with "pfs0:"
    // skipped discovery/mount entirely on a fresh APA boot.
    gHDDPrefix = NULL;
    gBaseMCDir = "mc?:OPL";


    // Network defaults are ONE coherent set, not four independent knobs -- restoring the fork's
    // opinionated fresh-install profile (this had reverted to the upstream 192.168.0.x / DHCP one).
    // Static addressing on 192.168.1.x, the far more common home subnet, with the PS2 and the PC on
    // the same wire: the UDP transports (UDPBD / UDPFS / NBD) run on a ministack that needs a STATIC
    // ip, so defaulting to DHCP hands a fresh install a config those transports cannot use.
    // gPCShareAddressIsNetBIOS follows the same logic -- raw-IP SMB addressing is what matches the
    // static defaults directly below; NetBIOS name resolution is the opt-in.
    ps2_ip_use_dhcp = 0;
    gETHOpMode = ETH_OP_MODE_AUTO;
    gPCShareAddressIsNetBIOS = 0; // raw-IP SMB addressing by default (matches the static defaults below)
    gPCShareNBAddress[0] = '\0';
    ps2_ip[0] = 192;
    ps2_ip[1] = 168;
    ps2_ip[2] = 1;
    ps2_ip[3] = 10;
    ps2_netmask[0] = 255;
    ps2_netmask[1] = 255;
    ps2_netmask[2] = 255;
    ps2_netmask[3] = 0;
    ps2_gateway[0] = 192;
    ps2_gateway[1] = 168;
    ps2_gateway[2] = 1;
    ps2_gateway[3] = 1;
    pc_ip[0] = 192;
    pc_ip[1] = 168;
    pc_ip[2] = 1;
    pc_ip[3] = 100;
    ps2_dns[0] = 192;
    ps2_dns[1] = 168;
    ps2_dns[2] = 1;
    ps2_dns[3] = 1;
    gPCPort = 1111; // RiptOPL default SMB port (was 445): matches the bundled PC server tooling
    // 1100 is Docmine17's PC server default, and his unmodified server is the compatibility
    // baseline. Any other port works; this is only what an unconfigured install tries first.
    gHttpServerIp[0] = 192;
    gHttpServerIp[1] = 168;
    gHttpServerIp[2] = 0;
    gHttpServerIp[3] = 10;
    gHttpPort = 1100;
    strcpy(gHttpBasePath, "/");
    // Fork's opinionated first-boot defaults: the overwhelmingly common SMB setup is a share named
    // "games" with guest access, so a fresh install can connect after typing only the host IP.
    // A loaded config still overwrites both.
    strcpy(gPCShareName, "games");
    strcpy(gPCUserName, "guest");
    gPCPassword[0] = '\0';
    gNetworkStartup = ERROR_ETH_NOT_STARTED;
    gSelectButton = KEY_CIRCLE; // Default to Japan.



    gNeutrinoArgs[0] = '\0';
    gNeutrinoPath[0] = '\0';
    gNeutrinoDevice = NEUTRINO_DEV_AUTO;
    gDefaultCoreLoader = 0;    // <OPL> (native) -- preserves behaviour until the user opts into Neutrino globally
    gNeutrinoVideoDefault = 0; // no global -gsm until the user opts in
    gNeutrinoGsmCompDefault = 0;
    gNeutrinoElfArg = 1; // auto-emit the game ELF for Neutrino compatibility lookup by default
    gNeutrinoShowArgs = 0;
    gPopstarterDevice = POPS_DEV_DEFAULT;
    gPopstarterRetroGemGameID = 1;
    gGameTypeLabels = 0;
    // Nothing is set until the user changes it here, so no device's settings.txt is touched until asked.
    gEmberDisplay = EMBER_SETTING_UNSET;
    gEmberTiming = EMBER_SETTING_UNSET;
    gEmberDither = EMBER_SETTING_UNSET;
    gEmberShading = EMBER_SETTING_UNSET;
    gEmberController = EMBER_SETTING_UNSET;
    gBdmaMode = VCD_BDMA_FAT32;
    // gBootDir is deliberately NOT reset here. main() resolves it (setBootDir, from argv[0] with a
    // getcwd fallback) BEFORE calling init(), and init() calls setDefaults() -- so clearing it here
    // erased the boot identity a few lines before configInit() needs it, on EVERY boot. That made
    // resolveBootDirToMass() early-return at its `gBootDir[0] == '\0'` guard every time, and homed
    // every config set on the mc?:OPL default regardless of what OPL actually booted from.
    // setBootDir() already zeroes the buffer at its own entry, so nothing needs a reset here.
    gEnableUDPBD = 0; // the UDPBD BLOCK device stays opt-in
    // Unified network selector defaults to OFF. The reason is the NIC latch: every network stack
    // loads its IOP chain ONCE per boot and never unloads (re-binding the UDPRDMA socket bricks
    // UDPFS; smap registers a single SMAP_driver), so whichever protocol is active FIRST owns the
    // adapter until a restart -- the settings page even tells you so (NETBOOT_RESTART). Defaulting
    // to Off means nothing claims the NIC at boot, so the first protocol the user picks in Device
    // Settings comes up live -- the apply path re-derives the gEnableUDPBD/gNetBootProtocol shadows
    // and forces a device refresh already. A saved net protocol in the config overrides this.
    gNetworkProtocol = NET_PROTO_OFF;
    gNetStartMode = START_MODE_DISABLED; // Off in the 3-row Network setting; migration reconciles old configs
    gNetProtocolPick = NET_PROTO_SMB;
    gUdpfsAccessMode = 0; // UDPFS Files; never inherit UDPBD's forced IMG

    frameCounter = 0;


    setDefaultColors();

    // Last Played Auto Start
    KeyPressedOnce = 0;
    DisableCron = 1; // Auto Start Last Played counter disabled by default
    CronStart = 0;
    RemainSecs = 0;
}

// RIPPS2 build 120 (akilluminati47): start the screen in the saved video mode. rmInit() runs before
// the settings load, so the boot screen came up in Auto and _loadConfig()'s applyConfig() set the
// saved mode a moment later: a second resync on a 480p or 1080i set, or through an HDMI adapter.
// Video Mode and Widescreen are read straight from the settings file the boot folder will load, when
// it is on a memory card (the card driver is already up; USB, the HDD and the network are not yet),
// so rmInit() sets the user's mode once. Nothing is saved and nothing else is read: _loadConfig()
// reads every key again as before, and the recovery combos and the START skip still decide there.
// A missing or unreadable file leaves the defaults, which is the old behaviour.
// One settings line, "key=value" as configWrite() writes it or "key = value;" as a libconfig file
// (wOPL) has it: 1 and the value when the line is that key.
static int bootPeekKey(const char *line, const char *key, int *value)
{
    size_t len = strlen(key);

    while (*line == ' ' || *line == '\t')
        line++;
    if (strncmp(line, key, len) != 0)
        return 0;
    line += len;
    while (*line == ' ' || *line == '\t')
        line++;
    if (*line != '=')
        return 0;
    *value = atoi(line + 1); // atoi skips the spaces after '=' and stops at a ';'
    return 1;
}

static void bootDisplayPeek(void)
{
    config_set_t *configOPL = configGetByType(CONFIG_OPL);
    const char *path = (configOPL != NULL) ? configOPL->filename() : NULL;
    char line[256];
    FILE *fp;

    if (path == NULL || strncmp(path, "mc", 2) != 0 || path[2] == '?')
        return;
    fp = fopen(path, "r");
    if (fp == NULL)
        return;
    int atLineStart = 1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        // A line longer than the buffer comes back in pieces: only a piece that starts a line is a key.
        int lineStart = atLineStart, value;
        atLineStart = strchr(line, '\n') != NULL;
        if (!lineStart)
            continue;
        if (bootPeekKey(line, CONFIG_OPL_VMODE, &value))
            gVMode = value; // out of range is clamped to Auto by rmSetMode()
        else if (bootPeekKey(line, CONFIG_OPL_WIDESCREEN, &value))
            gWideScreen = value != 0;
    }
    fclose(fp);
    LOG("BOOT display peek: %s vmode=%d wide=%d\n", path, gVMode, gWideScreen);
}

static void init(void)
{
    // default variable values
    setDefaults();

    bootTrace("pad driver");
    padInit(0);
    int padStatus = 0;
    menuWatchStep(MENUWATCH_BOOT_CONFIG);
    configInit(gBootDir[0] ? gBootDir : NULL); // settings live in the boot dir (cwd), not a fixed MC default

    const int defaultVMode = gVMode, defaultWideScreen = gWideScreen;
    bootDisplayPeek(); // the saved mode before the first one is set (RIPPS2 build 120)
    menuWatchStep(MENUWATCH_BOOT_VIDEO);
    rmInit();
    lngInit();
    thmInit();
    guiInit();
    menuWatchStep(MENUWATCH_BOOT_DEVICES);
    ioInit();
    hiddenInit();
    bdmSetAutoLaunch(0);
    menuInit();

    menuWatchStep(MENUWATCH_BOOT_PADS);
    startPads();

    bdmInitSemaphore();

    // handler for deffered menu updates
    ioRegisterHandler(IO_MENU_UPDATE_DEFFERED, &menuDeferredUpdate);
    cacheInit();

    gSelectButton = (InitConsoleRegionData() == CONSOLE_REGION_JAPAN) ? KEY_CIRCLE : KEY_CROSS;

    // This waits for the pad ports to open, not for a controller: a port with nothing plugged in
    // still counts, so a boot with no controller carries on as before. The wait only lasts when no
    // port will open; then it draws the boot splash after 10 s and logs it, and keeps waiting.
    clock_t padWaitStart = clock(); // elapsed time, not a deadline: clock() can wrap (see guisplash.cpp)
    int padWaitShown = 0;
    while (!padStatus) {
        padStatus = startPads();
        if (!padStatus && !padWaitShown && clock() - padWaitStart > 10 * CLOCKS_PER_SEC) {
            diagLog("boot: no pad port opened for 10 s, still waiting");
            guiRenderGreetingScreen();
            padWaitShown = 1;
        }
    }
    readPads();
    menuWatchStep(MENUWATCH_BOOT_SETTINGS);
    gBootInProgress = 1; // gate the in-initAllSupport greeting redraws to the boot pass only (#297)
    const int safeBoot = getKeyPressed(KEY_START);
    if (!safeBoot) {
        // Show the boot splash (not guiRenderTextScreen(), which calls guiShow()
        // and would draw the not-yet-ready main menu as a garbled landing page
        // before the intro splash) while the config loads.
        guiSetBootStatus(_l(_STR_BOOT_LOADING_CONFIG));
        guiRenderGreetingScreen();
        _loadConfig(); // only try to restore config if emergency key is not being pressed
#ifdef __OPLDIAG
        // #545: which settings home this boot chose, and why, before the menu covers it. Untranslated
        // developer text, diagnostic builds only -- a tester's photo answers what a log cannot.
        {
            char homeDiag[224];
            snprintf(homeDiag, sizeof(homeDiag), "Settings home: %s\n%s\nBDM start mode: %d  HDD (exFAT): %d",
                     configGetHomePath(), gBootHomeWhy, gBDMStartMode, gEnableBdmHDD);
            guiWarning(homeDiag, 20);
        }
#endif
    } else {
        LOG("--- SKIPPING OPL CONFIG LOADING\n");
        // START skips the settings, so the mode read early by bootDisplayPeek() goes too.
        gVMode = defaultVMode;
        gWideScreen = defaultWideScreen;
        applyConfig(-1, -1, 0);
    }
    // _loadConfig may move the settings home away from the boot folder: resolve the optional title
    // font only now, from the final settings owner.
    // START is the safe boot: it reads nothing optional from the card before the first frame.
    if (!safeBoot)
        lngInitFallbackFont();
    guiSetBootStatus(_l(_STR_BOOT_READY));
    guiRenderGreetingScreen();
    gBootInProgress = 0;


    // queue deffered init of sound effects, which will take place after the preceding initialization steps within the queue are complete.
    ioPutRequest(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&deferredAudioInit));
}

static void deferredInit(void)
{
    guiSetBootStatusSticky(_l(_STR_BOOT_BUILDING_MENU)); // boot-step localizer (IO thread) -- reaching
                                                         // here means the device init chain cleared; see gui.c

    // inform GUI main init part is over
    struct gui_update_t *id = guiOpCreate(GUI_INIT_DONE);
    if (id)
        guiDeferUpdate(id);

    if (gMMCEEnableGameID)
        ioPutRequest(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&mmceArmGameIDTransport));

    // Boot onto the configured Default Menu -- but if that mode has no registered support (its
    // device never initialised, or the config named a mode this build defers), fall through to a
    // registered one, mirroring applyConfig's clamp. Only when NOTHING is registered is there no
    // main screen worth selecting and the start menu stays -- previously ANY unregistered pick
    // silently parked every boot on the start menu with no explanation.
    int bootMode = gDefaultDevice;
    // "ETH Games" is the Default Menu entry for the network, whichever protocol the Network page picked.
    // Only SMB registers ETH_MODE; the other protocols put their games on a different page, so an
    // unresolved ETH_MODE had no support and every UDPFS/HTTP boot fell through to MMCE/Apps below.
    if (bootMode == ETH_MODE) {
        if (gNetworkProtocol == NET_PROTO_UDPFS)
            bootMode = UDPFS_MODE;
        else if (gNetworkProtocol == NET_PROTO_HTTP)
            bootMode = HTTP_MODE;
        else if (gNetworkProtocol == NET_PROTO_UDPBD || gNetworkProtocol == NET_PROTO_UDPFSBD) {
            // Block transports appear as a massN: page, and only once the device has attached; if it
            // has not by now, the fallback below applies as before.
            for (int i = BDM_MODE; i <= BDM_MODE_LAST; i++) {
                if (list_support[i].support != NULL && bdmModeIsUDPBD(i)) {
                    bootMode = i;
                    break;
                }
            }
        }
    }
    // All Games: the device pages step aside for the one list, so boot onto it.
    if (gAllGames && list_support[FAV_MODE].support != NULL)
        bootMode = FAV_MODE;
    if (list_support[bootMode].support == NULL) {
        if (list_support[MMCE_MODE].support != NULL)
            bootMode = MMCE_MODE;
        else if (list_support[APP_MODE].support != NULL)
            bootMode = APP_MODE;
        else {
            bootMode = -1;
            for (int i = 0; i < MODE_COUNT; i++) {
                if (list_support[i].support != NULL) {
                    bootMode = i;
                    break;
                }
            }
        }
    }
    if (bootMode >= 0 && list_support[bootMode].support) {
        id = guiOpCreate(GUI_OP_SELECT_MENU);
        if (id) {
            id->menu.menu = &list_support[bootMode].menuItem;
            guiDeferUpdate(id);
        }
    }
}

static void deferredAudioInit(void)
{
    int ret;

    guiSetBootStatusSticky(_l(_STR_BOOT_LOADING_SOUNDS)); // boot-step localizer (IO thread) -- see gui.c
    audioInit();
    ret = sfxInit(1);
    if (ret < 0)
        LOG("sfxInit: failed to initialize - %d.\n", ret);
    else
        LOG("sfxInit: %d samples loaded.\n", ret);
}

// ----------------------------------------------------------
// --------------------- Auto Loading -----------------------
// ----------------------------------------------------------


void miniInit(int mode)
{
    int ret;
#ifdef __OPLDIAG
    int initialRet;
    char initialHome[256];
    clock_t configStart;
#endif

    setDefaults();
    configInit(gBootDir[0] ? gBootDir : NULL); // settings live in the boot dir (cwd)

    ioInit();
    hiddenInit();
    bdmSetAutoLaunch(1);
    LOG_ENABLE();

    // Settings discovery can need a BDM transport even when the game itself is on APA HDD.
    bdmInitSemaphore();

    if (mode == BDM_MODE) {
        // Force load all BDM modules.. we aren't using the gui so this is fine.
        // gEnableUSB belongs in this list and was the one missing from it. Unlike the others it is a
        // FORK INVENTION -- upstream loads USBMASS_BD unconditionally and has no such flag -- so the
        // opt-in default of 0 that setDefaults() applies a few lines earlier (and which the GUI
        // normally overrides from the saved config) had nothing to override it here: configReadMulti
        // runs AFTER bdmLoadModules(). bdmLoadBlockDeviceModules gates the USB load on the flag, so
        // an argv autolaunch from a USB stick loaded no USB block driver at all.
        gEnableUSB = 1;
        gEnableILK = 1; // iLink will break pcsx2 however.
        gEnableMX4SIO = 1;
        gEnableBdmHDD = 1;
        bdmLoadModules();
    } else if (mode == HDD_MODE) {
        hddLoadModules();
        hddLoadSupportModules();
    } else if (mode == MMCE_MODE) {
        mmceLoadModules();
    }

    // Resolve the settings home and its recovery policy for every launch mode, as _loadConfig does.
    // The launcher directory can differ from the game's device and from the saved settings home.
    resolveBootDirToMass();
    InitConsoleRegionData();

#ifdef __OPLDIAG
    configStart = clock();
    snprintf(initialHome, sizeof(initialHome), "%s", configGetHomePath());
#endif
    ret = configReadMulti(CONFIG_ALL);
#ifdef __OPLDIAG
    initialRet = ret;
#endif
    if (CONFIG_ALL & CONFIG_OPL) {
        // Exactly the menu's discovery: resolveBootDirToMass above picked the same home (an APA+exFAT
        // hybrid included), so the settings -- global game settings among them -- are the ones the menu
        // shows and saves. A missing CONFIG_GAME alone still never triggers discovery on any launch path.
        if (!(ret & CONFIG_OPL)) {
            ret = tryAlternateDevice(CONFIG_ALL);
        }

        if (ret & CONFIG_OPL) {
            config_set_t *configOPL = configGetByType(CONFIG_OPL);

            // Autolaunch error dialogs use the same menu font size as the interactive menu.
            configGetInt(configOPL, CONFIG_OPL_FONT_SIZE, &gFontSize);
            gFontSize = fntValidFontSize(gFontSize);
            configGetInt(configOPL, CONFIG_OPL_PS2LOGO, &gPS2Logo);
            configGetStrCopy(configOPL, CONFIG_OPL_EXIT_PATH, gExitPath, sizeof(gExitPath));
            configGetInt(configOPL, CONFIG_OPL_HDD_SPINDOWN, &gHDDSpindown);
            // Honor ALL the Neutrino-launch globals on the autolaunch/argv path exactly like the
            // interactive _loadConfig -- not just the default core. An autolaunched keyless "Default"
            // game must resolve the SAME neutrino.elf (device pick / custom path) with the SAME global
            // args as an interactive launch, or it silently boots a different/stale core without flags.
            configReadNeutrinoGlobals(configOPL);
            configReadRA(configOPL); // or an auto-launched game is never tracked
            if (mode == BDM_MODE) {
                configGetStrCopy(configOPL, CONFIG_OPL_BDM_PREFIX, gBDMPrefix, sizeof(gBDMPrefix));
                sanitizePrefix(gBDMPrefix);
                configGetInt(configOPL, CONFIG_OPL_BDM_CACHE, &bdmCacheSize);
            } else if (mode == HDD_MODE)
                configGetInt(configOPL, CONFIG_OPL_HDD_CACHE, &hddCacheSize);
        }
    }
#ifdef __OPLDIAG
    // Autolaunch never initializes the GUI or a release TTY. Display the captured config state
    // using only the renderer and built-in font, then release them before the game handoff.
    // Capture timing before the diagnostic display; its eight-second hold is DIAG-only.
    config_set_t *globalGame = configGetByType(CONFIG_GAME);
    char diagnostic[1536];
    snprintf(diagnostic, sizeof(diagnostic),
             "Auto Loading config diagnostic (#545)\n"
             "mode=%d  config=%ld ms\n"
             "boot: %s\ninitial home: %s\nfinal home: %s\n"
             "home chosen because: %s\n"
             "read=0x%02x  final=0x%02x  GAME bit=0x%02x\n"
             "CONFIG_GAME populated=%d\n"
             "Game continues after 8 seconds.",
             mode, (long)((clock() - configStart) / (CLOCKS_PER_SEC / 1000)),
             gBootDir, initialHome, configGetHomePath(), gBootHomeWhy, initialRet, ret, CONFIG_GAME,
             globalGame != NULL && globalGame->entries() != NULL);
    printf("%s\n", diagnostic);
    rmInit();
    fntInit();
    for (int frame = 0; frame < 2; frame++) {
        rmStartFrame();
        rmDrawRect(0, 0, 640, 480, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
        fntRenderString(FNT_DEFAULT, 24, 32, 0, 592, 416, diagnostic,
                        GS_SETREG_RGBAQ(0xff, 0xff, 0xff, 0x80, 0));
        rmEndFrame();
    }
    DelayThread(8000000);
    fntEnd();
    rmEnd();
#endif
}

void miniDeinit(config_set_t *configSet)
{
    // Autolaunch teardown is always a LAUNCH: the game is about to take over.
    ioBlockOpsTimed(1, LAUNCH_IO_DRAIN_TICKS);
#ifdef PADEMU
    ds34usb_reset();
    ds34bt_reset();
#endif
    configFree(configSet);

    ioEnd();
    configEnd();
}

// --------------------- Main --------------------
// "mc?:" names no card. A launcher that hands over its configured wildcard leaves the slot to us,
// and the file helpers would then use whichever card checkMC() prefers -- mc0 whenever both are
// inserted -- so an mc1 install read and saved its settings on the other card. The card holding the
// booted ELF (or, with no ELF name, the boot folder) is the boot card. Runs after reset() has loaded
// the card driver; when neither card answers, the wildcard stays and nothing changes.
static void resolveMcWildcardBootDir(void)
{
    char probe[sizeof(gBootDir) + sizeof(gBootElfName) + 8];
    const char *rest;
    DIR *dir;
    int slot, fd;

    if (strncmp(gBootDir, "mc?:", 4))
        return;
    rest = gBootDir + 4;
    while (*rest == '/')
        rest++;
    for (slot = 0; slot < 2; slot++) {
        if (gBootElfName[0] != '\0') {
            snprintf(probe, sizeof(probe), "mc%d:/%s%s%s", slot, rest, rest[0] != '\0' ? "/" : "", gBootElfName);
            fd = open(probe, O_RDONLY);
            if (fd < 0)
                continue;
            close(fd);
        } else {
            snprintf(probe, sizeof(probe), "mc%d:/%s", slot, rest);
            dir = opendir(probe);
            if (dir == NULL)
                continue;
            closedir(dir);
        }
        gBootDir[2] = (char)('0' + slot);
        return;
    }
}

// Memory-card roots require an explicit slash for file creation (mc1:/FILE, not mc1:FILE).
// Some launchers provide an equivalent compact path such as mc1:APPS/OPL.ELF or leave getcwd()
// at mc1:. Normalize the generic device representation once, without any launcher-specific branch.
static void normalizeMcBootDir(void)
{
    resolveMcWildcardBootDir();
    normalizeMcPath(gBootDir, sizeof(gBootDir));
}

static void setBootDir(const char *bootPath)
{
    gBootDir[0] = '\0';
    gBootElfName[0] = '\0';
    if (bootPath == NULL || bootPath[0] == '\0')
        return;

    // Launchers are not consistent about separators (wLaunchELF variants can hand backslash paths);
    // normalize to '/' before splitting so the folder/basename split can't misfire.
    char path[sizeof(gBootDir)];
    snprintf(path, sizeof(path), "%s", bootPath);
    for (char *p = path; *p != '\0'; p++) {
        if (*p == '\\')
            *p = '/';
    }

    const char *slash = strrchr(path, '/');
    if (slash != NULL) {
        size_t len = (size_t)(slash - path); // keep the folder, drop the trailing '/' + filename
        if (len > 0 && len < sizeof(gBootDir)) {
            memcpy(gBootDir, path, len);
            gBootDir[len] = '\0';
            snprintf(gBootElfName, sizeof(gBootElfName), "%s", slash + 1);
        }
    } else {
        const char *colon = strrchr(path, ':'); // "mass0:X.ELF" -> device root "mass0:"
        if (colon != NULL && (size_t)(colon - path) + 1 < sizeof(gBootDir)) {
            size_t len = (size_t)(colon - path) + 1;
            memcpy(gBootDir, path, len);
            gBootDir[len] = '\0';
            snprintf(gBootElfName, sizeof(gBootElfName), "%s", colon + 1);
        }
    }
}

int main(int argc, char *argv[])
{
#ifdef __DECI2_DEBUG
    sysInitDECI2();
#endif

    bootTrace("main() started");
    LOG_INIT();
    PREINIT_LOG("OPL GUI start!\n");

    ChangeThreadPriority(GetThreadId(), 31);

    // reset, load modules
    reset();
    bootTrace("Deckard settings");
    ResetDeckardXParams();
    menuWatchStart(); // diagnostic builds: report a menu that stops drawing (needs the IOP that reset() set up)
    // Diagnostic builds: from here each boot stage shows its own colour if the boot stops in it.
    menuWatchStep(MENUWATCH_BOOT_DIR);

    // Settings live in the boot directory (cwd). Resolve it once, before any config init -- the
    // autolaunch path below calls miniInit() -> configInit(). argv[0] is the launcher's boot path;
    // getcwd() backs it up. Empty only if both are unusable, in which case configInit falls back to
    // the memory-card default.
    setBootDir(argc >= 1 ? argv[0] : NULL);
    if (gBootDir[0] == '\0') {
        char cwd[256];
        if (getcwd(cwd, sizeof(cwd)) != NULL && cwd[0] != '\0') {
            int n = (int)strlen(cwd);
            while (n > 0 && cwd[n - 1] == '/') // configInit appends '/', so drop any trailing one
                cwd[--n] = '\0';
            snprintf(gBootDir, sizeof(gBootDir), "%s", cwd);
        }
    }
    normalizeMcBootDir();

    if (argc >= 5) {
        /* argv[0] boot path
           argv[1] game->startup
           argv[2] str to u32 game->start_sector
           argv[3] legacy OPL data-partition hint (revalidated from hdd0:__common/OPL/conf_hdd.cfg)
           argv[4] "mini" */
        if (!strcmp(argv[4], "mini"))
            autoLaunchHDDGame(argv);
        /* argv[0] boot path
           argv[1] file name (including extention)
           argv[2] game->startup
           argv[3] game->media ("CD" / "DVD")
           argv[4] "bdm" */
        if (!strcmp(argv[4], "bdm"))
            autoLaunchBDMGame(argv);
    }

    init();
    menuWatchStep(MENUWATCH_BOOT_INTRO);

    // until this point in the code is reached, only PREINIT_LOG macro should be used
    LOG_ENABLE();

    // queue deffered init which shuts down the intro screen later
    ioPutRequest(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&deferredInit));

    guiIntroLoop();
    diagLog("boot: the menu is up; settings from %s", configGetDir());

    // No writable config home? Say so NOW, not after the user has changed settings and lost them.
    // The '?' only survives configGetDir() when the home fell all the way back to the "mc?:OPL"
    // default AND checkMC() found no card to substitute into it -- i.e. no boot identity, no custom
    // path, no discovery hit, and no local card. Reported from the field as "settings don't save,
    // but they claim to load", which is exactly what silence looks like from the outside.
    // A toast, not a modal: a standing property of the setup, not an error the user just caused,
    // and it must not gate a boot that otherwise works fine.
    if (strchr(configGetDir(), '?') != NULL)
        guiWarning(_l(_STR_SETTINGS_NO_HOME), 6);

    guiMainLoop();

    return 0;
}
