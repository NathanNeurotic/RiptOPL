/*
  Copyright 2009, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/settings.h"
#include "include/oplint.h"
#include "include/opl.h"
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
#ifdef __OPLDIAG
#include "include/fntsys.h"
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


// Per-source menu button handlers (select, refresh, view toggle, favourites, ...). Split out of opl.cpp.

static void itemInitSupport(item_list_t *support)
{
    support->itemInit(support);
    moduleUpdateMenuInternal((opl_io_module_t *)support->owner, 0, 0);

    // A support that was just started has no populated submenu yet, so always build it once now.
    // Automatic Refresh controls later periodic rescans; it must never suppress this first list
    // population and leave a Manual source enabled-but-empty until a background cadence happens.
    ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
}

static void itemExecSelect(struct menu_item *curMenu)
{
    item_list_t *support = static_cast<item_list_t*>(curMenu->userdata);
    sfxPlay(SFX_CONFIRM);
    // That confirm just armed a rumble pulse, and everything below blocks the GUI thread without
    // polling readPads() -- config load, GameID hold, the whole itemLaunch chain. The IOP LATCHES
    // the actuator, so without this the motor buzzes through the entire loading screen. GUI-thread
    // call site, which is the only kind pad.c allows.
    padRumbleFlush();

    if (support) {
        if (support->enabled) {
            if (curMenu->current) {
                // Folder browsing: a folder row DESCENDS (rescan one level deeper) instead of
                // launching. folderDescend marks the mode dirty; the deferred update rebuilds the list.
                if (curMenu->current->item.isFolder) {
                    if (folderDescend(support->mode, curMenu->current->item.text))
                        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
                    return;
                }
                config_set_t *configSet = menuLoadConfig();
                if (curMenu->current == NULL)
                    return; // a deferred source refresh replaced the row while config IO was pending
                int launchId = curMenu->current->item.id;
                char gameIdStartup[128] = {0};
                char *startup = support->itemGetStartup(support, launchId);
                if (startup != NULL)
                    snprintf(gameIdStartup, sizeof(gameIdStartup), "%s", startup);
                // Flash the GameID barcode (Pixel FX/RetroGEM HDMI auto-profile) before handoff. Use
                // the stack copy: the hold renders/unlocks for many frames while source lists may refresh.
                guiShowGameID(gameIdStartup);
                support->itemLaunch(support, launchId, configSet);
            }
        } else {
            // If we're trying to enable BDM support we need to enable it for all BDM menu slots.
            if (support->mode == BDM_MODE) {
                // Initialize support for all bdm modules.
                for (int i = BDM_MODE; i <= BDM_MODE_LAST; i++) {
                    opl_io_module_t *mod = &(*oplGetModule(i));
                    itemInitSupport(mod->support);
                }
            } else {
                // Normal initialization.
                itemInitSupport(support);
            }
        }
    } else
        guiMsgBox("NULL Support object. Please report", 0, NULL);
}

void itemExecRefresh(struct menu_item *curMenu)
{
    item_list_t *support = static_cast<item_list_t*>(curMenu->userdata);

    if (support && support->enabled) {
        // FAV has no device to rescan: loadFavourites() re-reads favourites.bin and schedules its
        // own rebuild. HDD's VCD view is intentionally cached across ordinary L3 flips, so an
        // explicit user Refresh must invalidate that cache before posting the deferred update.
        if (support->mode == FAV_MODE) {
            loadFavourites();
        } else if (support->mode == ETH_MODE && gNetworkStartup != 0) {
            // A failed SMB page uses the same Select/Refresh gesture as every other list, but its
            // failure is a connection state rather than stale directory metadata. Retry the full
            // live apply/logon/open-share path so a slow router/server can recover in place.
            ethRequestReconnect();
        } else {
            if (support->mode == HDD_MODE &&
                (libListViewActive(support) == LIB_VIEW_PS1 || libListViewActive(support) == LIB_VIEW_MIXED))
                hddVcdInvalidateCache();
            // Explicit Refresh must bypass per-device scan caches. This also ensures a Mixed list
            // refreshes both of its backing stores instead of merely rebuilding the same rows.
            libViewMarkDirty(support->mode);
            ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
        }
        sfxPlay(SFX_CONFIRM);
    }
}

// L3: stage the page's next list and let the deferred worker commit it only after clearing the old
// submenu. Until that clear, callbacks and rendering must continue resolving the visible row ids
// through the old view; otherwise a valid row 0 from one list can silently become row 0 of another.
void itemExecToggleView(struct menu_item *curMenu)
{
    item_list_t *support = static_cast<item_list_t*>(curMenu->userdata);
    if (!support || !libViewRingUsable(support->mode) || libViewPending(support->mode))
        return;

    // Queue before changing any view state. The IO worker is deliberately lower priority than this
    // GUI thread, so it cannot inspect the request until this handler has staged its target. If the
    // queue rejects the request, the visible view and its rows remain completely untouched.
    if (ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode) != IO_OK)
        return;

    // Folder browsing: the VCD/POPS list has no folder tree, so drop any ISO-view subfolder position
    // back to root on a view toggle (the deferred rebuild below restores the plain device title).
    folderReset(support->mode);
    if (!libViewStageAdvance(support->mode))
        return;

    // Every cover already queued belongs to the view being discarded, and none of them will be
    // cancelled on their own: the loader's per-row cancellation keys on a row having scrolled away,
    // and here nothing scrolls -- the old rows keep being drawn until the rebuild below lands, so
    // their stamps stay fresh. They are FIFO ahead of that rebuild and art shares the single ioman
    // worker with it, so without this the device has to serve every cover of the OLD list before the
    // NEW one can even be built. That is why the page took so long to become correct.
    cacheDropQueuedArt();

    sfxPlay(SFX_CONFIRM);
    // Favorites has four stops (PS2 -> PS1 -> ELF -> All), so the toast names the
    // one you landed on rather than reading as an on/off flag.
    {
        int view = libViewPendingTarget(support->mode);
        int msg;
        if (support->mode == FAV_MODE)
            msg = (view == LIB_VIEW_PS1) ? _STR_VIEW_PS1 : (view == LIB_VIEW_ELF) ? _STR_VIEW_ELF :
                                                       (view == LIB_VIEW_ALL)     ? _STR_VIEW_ALL :
                                                                                    _STR_VIEW_PS2;
        else if (support->mode == APP_MODE)
            msg = view == LIB_VIEW_PS1_ELF ? _STR_VIEW_PS1_ELF : _STR_VIEW_APPS;
        else if (view == LIB_VIEW_MIXED)
            msg = _STR_VIEW_MIXED;
        else
            msg = (view == LIB_VIEW_PS1) ? _STR_VCD_ON : _STR_VCD_OFF;
        guiWarning(_l(msg), 2);
    }
}

// Effective kind of the selected row. Every normal device page is homogeneous, so its active view
// is enough. Favorites' All-in-One stop is intentionally mixed; there the stored row kind decides
// whether Triangle opens PS2 options, the PS1 rename-only menu, or the ELF/app menu.
static int itemSelectedView(struct menu_item *curMenu, item_list_t *support)
{
    if (support == NULL)
        return LIB_VIEW_ISO;
    if (curMenu != NULL && curMenu->current != NULL)
        return libListRowView(support, curMenu->current->item.id);
    return libListViewActive(support);
}

// Folder browsing: the cancel button (whichever of Cross/Circle is not Select) ascends a level.
static void itemFolderAscend(struct menu_item *curMenu)
{
    item_list_t *support = curMenu ? static_cast<item_list_t*>(curMenu->userdata) : NULL;
    if (support == NULL || folderDepth(support->mode) == 0)
        return;
    if (folderAscend(support->mode)) {
        sfxPlay(SFX_CANCEL);
        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
    }
}

void itemExecCross(struct menu_item *curMenu)
{
    if (gSelectButton == KEY_CROSS)
        itemExecSelect(curMenu);
    else
        itemFolderAscend(curMenu); // Cross is the cancel button here -> ascend a folder level
}

void itemExecCircle(struct menu_item *curMenu)
{
    if (gSelectButton == KEY_CIRCLE)
        itemExecSelect(curMenu);
    else
        itemFolderAscend(curMenu); // Circle is the cancel button here -> ascend a folder level
}

void itemExecSquare(struct menu_item *curMenu)
{
    // Folder browsing: a folder row has no info screen (#Size/#DiscType would stat a directory).
    if (curMenu->current && curMenu->current->item.isFolder)
        return;
    if (curMenu->current && gTheme->infoElems.first) {
        // #Size is skipped while scrolling so the badges paint instantly; resolve it now (async) --
        // but NEVER for a VCD (PS1) list. A VCD carries no meaningful #Size: vcdFillGameList tags the
        // entry .VCD/GAME_FORMAT_ISO (vcdsupport.c), so sbPopulateConfig stats the CD/DVD folder while
        // .VCDs actually live in POPS/ -- the stat always misses, game->sizeMB stays 0 and never
        // caches, so it re-runs on EVERY entry. That makes the resolve a pure redundant CFG re-read +
        // failing stat on the shared MMCE/fileXio channel whose only visible effect is raising the
        // busy spinner (Andrew, #120). Skipping it for VCD strictly REDUCES channel traffic and drops
        // no displayed data (the info page shows no size for a VCD anyway).
        item_list_t *support = static_cast<item_list_t*>(curMenu->userdata);
        // Direct HDD/HDL rows already carry their size in APA metadata (total_size_in_kb), so the
        // generic CFG+stat pass is redundant there. VCD rows likewise have no meaningful #Size.
        // Avoid putting either no-op read onto the shared IO worker merely for opening Info.
        if (support == NULL || (itemSelectedView(curMenu, support) != LIB_VIEW_PS1 && support->mode != HDD_MODE))
            menuRequestInfoSize();
        guiSwitchScreen(GUI_SCREEN_INFO);
    }
}

// Which favourite kind is the row at `id` on this page? The SHELF comes from the page (APPS rows
// are ELF favourites, a PS1 page's rows are PS1 favourites), but for PS1 the CORE comes from the
// ROW -- both cores share that one list, so asking the page which core to record would file every
// Ember title as a POPSTARTER one and it would then launch through the wrong core.
// `text` is the row as DRAWN. Comparing the looked-up row's name against it is a final identity
// check across asynchronous source refreshes: they agree only when the row on screen is the row we
// resolved. Returns -1 when they disagree, and the caller declines the action rather than writing a
// record that names one game and launches another.
static int itemFavKind(item_list_t *support, int id, const char *text)
{
    const base_game_info_t *game;
    int rowView;

    if (support == NULL)
        return FAV_KIND_ISO;
    if (support->mode == APP_MODE)
        return FAV_KIND_ELF;
    rowView = libListRowView(support, id);
    if (rowView != LIB_VIEW_PS1)
        return FAV_KIND_ISO;

    // A PS1 page holds both cores, so the CORE comes from the row. Without itemGet there is nothing
    // to read it from; POPSTARTER is the right assumption, being what every pre-Ember library is.
    if (support->itemGet == NULL || text == NULL)
        return FAV_KIND_VCD;

    game = (const base_game_info_t *)support->itemGet(support, id);
    if (game == NULL)
        return -1;
    // Split-store guards hand back a static EMPTY entry for an id the list cannot honour; its name is
    // "", which no drawn row has, so the mismatch below catches that case too.
    if (strcmp(game->name, text) != 0)
        return -1;

    return cueIsCueEntry(game) ? FAV_KIND_CUE : FAV_KIND_VCD;
}

// R3: toggle the current row's favourite star (checklist item 33). On the Favourites tab
// itself R3 removes; on any source list it adds/removes by (mode, id, text).
void itemExecFav(struct menu_item *curMenu)
{
    if (!gFAVStartMode) // Favourites disabled -> R3 is a no-op (no hidden writes)
        return;

    if (!curMenu->current)
        return;

    item_list_t *support = static_cast<item_list_t*>(curMenu->userdata);
    if (!support)
        return;

    submenu_item_t *it = &curMenu->current->item;

    // Folder browsing: on a source list a folder row is not favouritable, and a game favourited
    // from INSIDE a subfolder would resolve by index against the wrong (root) view later --
    // suppress both. Root favourites are unchanged; the FAV tab's own removals are unaffected.
    if (support->mode != FAV_MODE && (it->isFolder || folderDepth(support->mode) > 0))
        return;

    if (support->mode == FAV_MODE) {
        favRemoveByIndex(it->id);
    } else {
        int kind = itemFavKind(support, it->id, it->text);
        int sourceId = libListSourceId(support, it->id);
        // Mid-rebuild: the row on screen is not the row the backing list would give us. Storing it
        // now would file the favourite under the wrong core, or under a name that is about to move.
        // Do nothing and let the user press R3 again once the list has settled -- the star is not
        // touched either, so nothing on screen lies about what was saved.
        if (kind < 0 || sourceId < 0)
            return;
        // Only store a favourite this device can actually resolve and launch later. A PS1 record
        // whose source has no hook for that core would be a permanently-hidden, unlaunchable entry.
        if (kind == FAV_KIND_VCD && support->itemLaunchVcd == NULL)
            return;
        if (kind == FAV_KIND_CUE && support->itemLaunchCue == NULL)
            return;
        if (it->favourited) {
            if (removeFavouriteByIdAndText(support->mode, sourceId, it->text, kind))
                it->favourited = 0; // only clear the star once the store write succeeded
        } else {
            if (addFavouriteItem(support->mode, sourceId, it->icon_id, it->text_id, it->text, kind))
                it->favourited = 1; // only show the star once the store write succeeded
        }
    }

    sfxPlay(SFX_CONFIRM);
    ioPutRequest(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&loadFavourites));
}

void itemExecTriangle(struct menu_item *curMenu)
{
    if (!curMenu->current)
        return;

    // Folder browsing: a folder row has no per-game settings menu.
    if (curMenu->current->item.isFolder)
        return;

    item_list_t *support = static_cast<item_list_t*>(curMenu->userdata);

    if (support) {
        // A VCD is a POPSTARTER title, not a PS2-loader title. Route before the generic per-game
        // settings branch: that branch loads/creates CFG state and exposes controls that can never
        // affect a POPSTARTER handoff. libListViewActive also covers a forced-VCD Favourites proxy.
        if (itemSelectedView(curMenu, support) == LIB_VIEW_PS1) {
            if (menuCheckParentalLock() == 0) {
                // The row names its core: a favourite by its stored kind, a device row by its extension.
                int kind = (support->mode == FAV_MODE) ? favGetItemKind(curMenu->current->item.id) :
                                                         itemFavKind(support, curMenu->current->item.id, curMenu->current->item.text);
                menuInitVcdMenu(kind == FAV_KIND_CUE);
                guiSwitchScreen(GUI_SCREEN_APP_MENU);
            }
            return;
        }

        unsigned char flags = (support->mode == FAV_MODE) ? favGetFlags(support) : support->flags;

        if (!(flags & MODE_FLAG_NO_COMPAT)) {
            if (menuCheckParentalLock() == 0) {
                menuInitGameMenu(support);
                guiSwitchScreen(GUI_SCREEN_GAME_MENU);
                guiGameLoadConfig(support, gameMenuLoadConfig(NULL));
            }
        } else {
            if (menuCheckParentalLock() == 0 && gEnableWrite) {
                menuInitAppMenu();
                guiSwitchScreen(GUI_SCREEN_APP_MENU);
            }
        }
    } else
        guiMsgBox("NULL Support object. Please report", 0, NULL);
}
