/*
  Copyright 2009, Ifcaro & volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/sema_lock.h"
#include "include/diaglog.h"     // the hardware log, DIAG.TXT (RIPPS2 build 115)
#include "include/drivestatus.h" // Drive Status (RIPPS2 build 114)
#include "include/guifilemgr.h"  // File Manager (after RIPPS2's Memory Files)
#include "include/opl.h"
#include "include/cfgtitle.h"
#include "include/diag.h"
#include "include/menusys.h"
#include "include/supportbase.h" // sbSetConfigStatSize -- defer the #Size stat off the scroll path
#include "include/iosupport.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/pad.h"
#include "include/gui.h"
#include "include/guigame.h"
#include "include/system.h"
#ifdef RETROACHIEVEMENTS
#include "include/discsupport.h"
#include "include/achievements.h"
#include "include/ranet.h"
#include "include/ethsupport.h"
#include "include/udpfssupport.h"
#include "include/texcache.h"
#endif
#include "include/ioman.h"
#include "include/sound.h"
#include "include/gamesource.h"
#include "include/favsupport.h"   // gFAVStartMode -- the Favourites tab counts as "something to show"
#include "include/gamemanage.h"   // Hide Game and Delete everything (RIPPS2)
#include "include/grid.h"         // gridRowStep, gridPageStep, gridAtRowStart -- the Grid view's keys
#include "include/folderbrowse.h" // menuFolderResetLeaving -- leave a device page at its folder root
#include "include/vcdsupport.h"   // VCD launches never use Neutrino
#include "include/libview.h"      // libViewActive / libListViewActive -- which list this page shows
#include "include/bdmsupport.h"   // bdmSupportIsUDPBD() / bdmNetBlockCoreLoader() -- UDPBD games default to Neutrino
#include "include/hddsupport.h"   // hdl_game_info_t -- the RA check hashes an HDLoader install
#include <assert.h>
#include <delaythread.h>
#include <time.h> // clock() -- Random Game seed

enum MENU_IDs {
    MENU_SETTINGS = 0,
    // Retired Network Update slot. Keep subsequent menu IDs stable even though the item itself is gone.
    MENU_RETIRED_NETWORK_UPDATE,
    MENU_NBD,
    MENU_DRIVE_STATUS, // RIPPS2 build 114 (akilluminati47): drives, their space, DIAG.TXT
    MENU_ABOUT,
    MENU_SAVE_CHANGES,
    MENU_EXIT,
    MENU_POWER_OFF,
    MENU_LAUNCH_PS2_DISC,
    MENU_MMCE,
    MENU_RANDOM_GAME,
#ifdef RETROACHIEVEMENTS
    MENU_RA_DISC_CHECK,
    MENU_RA_ACHIEVEMENTS,
    MENU_RA_DISC_LAUNCH,
#endif
    MENU_FILE_MANAGER, // after RIPPS2's Memory Files browser (akilluminati47)
};

enum GAME_MENU_IDs {
    GAME_COMPAT_SETTINGS = 0,
    GAME_CHEAT_SETTINGS,
    GAME_GSM_SETTINGS,
    GAME_VMC_SETTINGS,
#ifdef PADEMU
    GAME_PADEMU_SETTINGS,
    GAME_PADMACRO_SETTINGS,
#endif
    GAME_OSD_LANGUAGE_SETTINGS,
    GAME_SAVE_CHANGES,
    GAME_TEST_CHANGES,
    GAME_REMOVE_CHANGES,
    GAME_RENAME_GAME,
    GAME_DELETE_GAME,
    GAME_HIDE_GAME,
#ifdef RETROACHIEVEMENTS
    // Appended last, and only in the RA flavour: menuRenderGameMenu's extra-spacing arithmetic keys
    // on these enum positions, so RA entries must not shift the existing ones.
    GAME_RA_CHECK,
    GAME_RA_TEST,
    GAME_RA_ACHIEVEMENTS,
#endif
};

enum APP_MENU_IDs {
    APP_RENAME = 0,
    APP_DELETE,
    APP_GLOBAL_PS_SETTINGS,
    APP_EMBER_GAME_SETTINGS,
    APP_HIDE_GAME,
};

// global menu variables
static menu_list_t *menu;
static menu_list_t *selected_item;

static int actionStatus;
static int menuSaveResult; // last per-game configWrite() result, published by _menuSaveConfig
static int itemConfigId;
static config_set_t *itemConfig;
// Owner of itemConfigId, including while an async read is in flight. IDs are only indices within a
// list, so (id == id) across two tabs is not the same game/config. This also lets X/Triangle safely
// reuse the browse-prefetched config instead of forcing a second device read.
static item_list_t *itemConfigList;
// Protected by menuSemaId; generations distinguish invalidation followed by the same list/id.
static unsigned char itemConfigWorkerPending;
static unsigned int itemConfigGeneration;
// One Info/#Size worker at a time, with a latest-wins target. Repeated enter/back/enter on the
// SAME row does not queue duplicate CFG+stat reads; moving to another row while one is active replaces
// the requested target and the worker loops to that newest row before releasing its latch.
static unsigned char infoSizeWorkerPending;
static item_list_t *infoSizeRequestList;
static int infoSizeRequestId = -1;
static unsigned int infoSizeRequestGeneration;

static u8 parentalLockCheckEnabled = 1;

// "main menu submenu"
static submenu_list_t *mainMenu;
// active item in the main menu
static submenu_list_t *mainMenuCurrent;

// "game settings submenu"
static submenu_list_t *gameMenu;
// active item in game settings
static submenu_list_t *gameMenuCurrent;

static submenu_list_t *appMenu;
static submenu_list_t *appMenuCurrent;

static s32 menuSemaId = -1;

/* HINT-LIST LOCK. Deliberately its OWN semaphore, and deliberately not guiLock.
 *
 * menu_item_t::hints is a linked list that the IO WORKER rebuilds -- moduleUpdateMenuInternal()
 * calls menuRemoveHints() to free every node and then menuAddHint() several times to malloc and
 * relink -- while the GUI thread walks that same list every frame in themes.c drawHintText(), twice
 * (guiAlignMenuHints() to align, then again to draw). Unserialized, a draw landing mid-rebuild
 * follows a freed ->next into memory already handed back out.
 *
 * guiLock() is the WRONG lock for this even though it does exclude the renderer: guiStartFrame()
 * takes it and guiEndFrame() releases it after rmEndFrame()'s vsync wait, so it is held for
 * essentially a whole frame. Taking that on a per-device-update path stalls the io worker ~16 ms
 * every time a device appears or vanishes, which measurably disturbed unrelated timing.
 *
 * This one is held only for the list work itself -- a few frees and mallocs, or one traversal --
 * so the worst a waiter ever sees is microseconds.
 *
 * CALLER-LOCKED. menuAddHint()/menuRemoveHints() do NOT lock internally: a rebuild is many calls and
 * must be atomic as a whole, and this semaphore is not recursive. Every caller brackets its own
 * sequence with menuHintsLock()/menuHintsUnlock().
 */
static s32 menuHintSemaId = -1;
static ee_sema_t menuHintSema;

void menuHintsLock(void)
{
    // Not-ready guard, same shape as guiLock(): hints are built during init before menuInit() has
    // created this, and everything is single-threaded until the GUI and IO threads exist.
    if (menuHintSemaId < 0)
        return;
    WaitSema(menuHintSemaId);
}

void menuHintsUnlock(void)
{
    if (menuHintSemaId < 0)
        return;
    SignalSema(menuHintSemaId);
}
static s32 menuListSemaId = -1;
static ee_sema_t menuSema;

static void menuRenameGame(submenu_list_t **submenu)
{
    if (!selected_item->item->current) {
        return;
    }

    if (!gEnableWrite)
        return;

    item_list_t *support = static_cast<item_list_t *>(selected_item->item->userdata);

    if (support && support->mode == FAV_MODE) {
        char text[128];
        snprintf(text, sizeof(text), _l(_STR_FAV_MSG), _l(_STR_RENAME));
        guiMsgBox(text, 0, NULL);
        return;
    }
    if (selected_item->item->current->item.favourited) {
        char text[128];
        snprintf(text, sizeof(text), _l(_STR_FAV_PERSISTENCE_MSG), _l(_STR_RENAME));
        guiMsgBox(text, 0, NULL);
        return;
    }

    if (support) {
        if (menuCheckParentalLock() == 0) {
            sfxPlay(SFX_MESSAGE);
            int nameLength = gsOf(support)->nameLength(support, selected_item->item->current->item.id);
            char newName[nameLength];
            strncpy(newName, selected_item->item->current->item.text, nameLength);
            if (guiShowKeyboard(newName, nameLength)) {
                guiSwitchScreen(GUI_SCREEN_MAIN);
                submenuDestroy(submenu);

                // Only rename the file if the name changed; trying to rename a file with a file name that hasn't changed can cause the file
                // to be deleted on certain file systems.
                if (strcmp(newName, selected_item->item->current->item.text) != 0) {
                    gsOf(support)->rename(support, selected_item->item->current->item.id, newName);
                    ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
                }
            }
        }
    } else
        guiMsgBox("NULL Support object. Please report", 0, NULL);
}

// ---- Hide Game and Delete everything (RIPPS2) ----
// gamemanage.cpp keeps the lists; the row walk and page rebuilds live here, beside the menus.

// The real source a row's game is on, whether it is a PS1 game, and its name. 0 when the row has no
// source that keeps a hidden list (apps, the network catalogue, a folder row). A Favourites/All Games
// row resolves to its device.
static int hiddenRowSource(item_list_t *support, int id, item_list_t **source, int *ps1, const char **name)
{
    item_list_t *src;
    int p1;
    const char *n;

    if (support == NULL || support->source == NULL || id < 0)
        return 0;
    if (support->mode == FAV_MODE) {
        const fav_rec_t *rec = gFavSupport.entry(id);
        if (rec == NULL || rec->owner == NULL || rec->kind == FAV_KIND_ELF)
            return 0;
        src = rec->owner;
        p1 = rec->kind == FAV_KIND_VCD || rec->kind == FAV_KIND_CUE;
        n = rec->text;
    } else {
        if (support->mode == APP_MODE || support->mode == HTTP_MODE)
            return 0;
        GameSource *gs = gsOf(support);
        if (folderModeSupported(support->mode) && gs->hasGame()) {
            base_game_info_t *game = static_cast<base_game_info_t *>(gs->game(support, id));
            if (game == NULL || game->format == GAME_FORMAT_FOLDER)
                return 0;
        }
        int view = gs->hasRowView() ? gs->rowView(support, id) : libViewActive(support->mode);
        if (view == LIB_VIEW_ELF || view == LIB_VIEW_PS1_ELF)
            return 0;
        src = support;
        p1 = view == LIB_VIEW_PS1;
        n = gs->name(support, id);
    }
    if (src->mode < 0 || src->mode >= MODE_COUNT || src->mode == HTTP_MODE || src->mode == APP_MODE || src->mode == FAV_MODE ||
        n == NULL || n[0] == '\0')
        return 0;
    *source = src;
    *ps1 = p1;
    *name = n;
    return 1;
}

int hiddenRow(item_list_t *support, int id)
{
    item_list_t *source;
    int ps1;
    const char *name;
    return hiddenRowSource(support, id, &source, &ps1, &name) && hiddenHas(source, ps1, name);
}

// Rebuilds the pages a hidden game could be on: its device's page, then Favourites/All Games.
static void hiddenRefresh(item_list_t *source)
{
    opl_io_module_t *mod = oplGetModule(source->mode);
    if (mod != NULL && mod->support != NULL && mod->support->enabled) {
        libViewMarkDirty(source->mode);
        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &mod->support->mode);
    }
    if (gFAVStartMode || gAllGames)
        loadFavourites();
}

// The selected row's game, its source and whether it is hidden now. 0 when the row has no source
// that keeps a hidden list.
static int menuHiddenState(item_list_t **source, int *ps1, const char **name, int *hidden)
{
    item_list_t *support = selected_item != NULL ? static_cast<item_list_t *>(selected_item->item->userdata) : NULL;
    if (!gEnableWrite || support == NULL || selected_item->item->current == NULL || selected_item->item->current->item.isFolder ||
        !hiddenRowSource(support, selected_item->item->current->item.id, source, ps1, name))
        return 0;
    *hidden = hiddenHas(*source, *ps1, *name);
    return 1;
}

static void menuHideGame(submenu_list_t **submenu)
{
    char name[HIDDEN_NAME_MAX], msg[512];
    item_list_t *source;
    const char *rowName;
    int ps1, hidden;

    if (!menuHiddenState(&source, &ps1, &rowName, &hidden))
        return;
    snprintf(name, sizeof(name), "%s", rowName); // the row goes when its page is rebuilt
    if (!hidden) {
        snprintf(msg, sizeof(msg), _l(_STR_HIDE_GAME_CONFIRM), name);
        if (guiMsgBox(msg, 1, NULL) != 1)
            return;
    }
    guiSwitchScreen(GUI_SCREEN_MAIN);
    submenuDestroy(submenu);
    int result = hiddenSet(source, ps1, name, !hidden);
    if (result < 0) {
        guiMsgBox(_l(_STR_HIDDEN_NO_LIST), 0, NULL);
    } else if (result == 0) {
        guiMsgBox(_l(_STR_HIDDEN_SAVE_FAILED), 0, NULL);
    } else {
        hiddenRefresh(source);
        snprintf(msg, sizeof(msg), _l(hidden ? _STR_GAME_UNHIDDEN : _STR_GAME_HIDDEN), name);
        guiShowToast(msg);
    }
}

// Delete everything: two questions, then the game's parts and its CFG, art and cheats unless another
// copy uses them. VMC saves stay. Returns 0 when the row is not a game this can delete, so the caller
// keeps the plain delete.
static int menuDeleteEverything(submenu_list_t **submenu, item_list_t *support)
{
    char name[HIDDEN_NAME_MAX], prefix[256], partPath[256], sepStr[2], msg[700];
    int id = selected_item->item->current->item.id;
    gm_delete_plan_t *plan = static_cast<gm_delete_plan_t *>(malloc(sizeof(*plan)));

    if (plan == NULL)
        return 0;
    if (!gmPlanDelete(support, id, plan)) {
        free(plan);
        return 0;
    }
    GameSource *gs = gsOf(support);
    const char *rowName = gs->name(support, id);
    snprintf(name, sizeof(name), "%s", rowName != NULL ? rowName : "");
    sepStr[0] = support->mode == ETH_MODE ? '\\' : '/';
    sepStr[1] = '\0';
    // a copy of the row and its prefix: the delete rebuilds the list they live in
    base_game_info_t game = *static_cast<base_game_info_t *>(gs->game(support, id));
    snprintf(prefix, sizeof(prefix), "%s", gs->prefix(support));

    snprintf(msg, sizeof(msg), _l(_STR_DELETE_GAME_PLAN), name, plan->parts, plan->cfg, plan->art, plan->cht);
    if (plan->shared)
        snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), " %s", _l(_STR_DELETE_GAME_SHARED));
    if (guiMsgBox(msg, 1, NULL) != 1) {
        free(plan);
        return 1;
    }
    snprintf(msg, sizeof(msg), _l(_STR_DELETE_GAME_LAST_CHECK), name);
    if (guiMsgBox(msg, 1, NULL) != 1) {
        free(plan);
        return 1;
    }

    guiSwitchScreen(GUI_SCREEN_MAIN);
    submenuDestroy(submenu);
    gs->deleteGame(support, id); // every part, and a UL game's ul.cfg made again
    // every part has to be gone before the CFG, art and cheats go: a part left behind keeps them
    int partLeft = 0, part, fd;
    for (part = 0; part < game.parts && !partLeft; part++) {
        sbCreatePath(&game, partPath, prefix, sepStr, part);
        if ((fd = open(partPath, O_RDONLY)) >= 0) {
            close(fd);
            partLeft = 1;
        }
    }
    if (partLeft) {
        snprintf(msg, sizeof(msg), _l(_STR_GAME_DELETE_FAILED), name);
        guiMsgBox(msg, 0, NULL);
    } else {
        int failed = gmDeleteExtras(plan);
        if (hiddenHas(support, 0, name) && hiddenSet(support, 0, name, 0) > 0) // its place on the hidden list goes with it
            hiddenRefresh(support);
        snprintf(msg, sizeof(msg), _l(failed ? _STR_GAME_DELETED_PARTLY : _STR_GAME_DELETED), name);
        guiShowToast(msg);
    }
    free(plan);
    ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
    return 1;
}

static void menuDeleteGame(submenu_list_t **submenu)
{
    if (!selected_item->item->current)
        return;

    if (!gEnableWrite)
        return;

    item_list_t *support = static_cast<item_list_t *>(selected_item->item->userdata);

    if (support && support->mode == FAV_MODE) {
        char text[128];
        snprintf(text, sizeof(text), _l(_STR_FAV_MSG), _l(_STR_DELETE));
        guiMsgBox(text, 0, NULL);
        return;
    }
    if (selected_item->item->current->item.favourited) {
        char text[128];
        snprintf(text, sizeof(text), _l(_STR_FAV_PERSISTENCE_MSG), _l(_STR_DELETE));
        guiMsgBox(text, 0, NULL);
        return;
    }

    if (support) {
        if (gsOf(support)->hasDelete()) {
            if (menuCheckParentalLock() == 0) {
                if (support->mode != APP_MODE && menuDeleteEverything(submenu, support))
                    return;
                if (guiMsgBox(_l(_STR_DELETE_WARNING), 1, NULL)) {
                    guiSwitchScreen(GUI_SCREEN_MAIN);
                    submenuDestroy(submenu);
                    gsOf(support)->deleteGame(support, selected_item->item->current->item.id);
                    ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
                }
            }
        }
    } else
        guiMsgBox("NULL Support object. Please report", 0, NULL);
}

// Waited-on load for the game-settings dialog path (gameMenuLoadConfig). Launch-time
// menuLoadConfig deliberately bypasses ioman below so foreground launch configuration can never
// sit behind queued device/list work. This queued variant stays for settings UI transitions.
static void _menuLoadConfig()
{
    WaitSema(menuSemaId);
    if (!itemConfig) {
        item_list_t *list = static_cast<item_list_t *>(selected_item->item->userdata);
        itemConfig = gsOf(list)->config(list, itemConfigId);
        if (itemConfig != NULL)
            itemConfigList = list;
    }
    actionStatus = 0;
    SignalSema(menuSemaId);
}

// Browse-time load (the SETTLE path: menuRenderElements -> _menuRequestConfig each frame). NOBODY
// waits on this one -- actionStatus is already 0 on that path -- so it can afford to do the read
// OUTSIDE menuSemaId, which is the whole point: menuRenderElements takes that sema every frame,
// and itemGetConfig is a per-game CFG open/read on the game's device, so holding it across the read
// froze rendering and input for the entire read every time the cursor settled on a row (visible on
// a slow USB stick with art off; worse with art on, when the same device is also serving covers).
// Snapshot under the sema, read outside it, re-check and publish under it again -- the same shape
// as _menuResolveInfoSize below.
static void _menuLoadConfigAsync()
{
    for (;;) {
        item_list_t *list = NULL;
        config_set_t *loadedConfig = NULL;
        int configId = -1;
        unsigned int generation;
        {
            raii::SemaLock lock(menuSemaId);
            if (itemConfig == NULL && itemConfigId >= 0 && selected_item != NULL &&
                selected_item->item != NULL && selected_item->item->current != NULL &&
                selected_item->item->userdata == itemConfigList &&
                selected_item->item->current->item.id == itemConfigId) {
                list = itemConfigList;
                configId = itemConfigId;
            }
            generation = itemConfigGeneration;
        }
        if (list != NULL) {
            loadedConfig = gsOf(list)->config(list, configId);
            {
                raii::SemaLock lock(menuSemaId);
                if (loadedConfig != NULL && itemConfig == NULL && itemConfigGeneration == generation &&
                    itemConfigId == configId && itemConfigList == list && selected_item != NULL &&
                    selected_item->item != NULL && selected_item->item->current != NULL &&
                    selected_item->item->userdata == list && selected_item->item->current->item.id == configId) {
                    itemConfig = loadedConfig;
                    loadedConfig = NULL;
                }
            }
            if (loadedConfig != NULL)
                configFree(loadedConfig);
        }
        raii::SemaLock lock(menuSemaId);
        if (itemConfigGeneration == generation) {
            itemConfigWorkerPending = 0;
            return;
        }
        // The target changed during IO. Keep the latch and process the newest target
        // directly instead of queuing a second worker behind unrelated device work.
    }
}

// True while this request is still the newest target AND its row is still the selected one.
// Caller holds menuSemaId.
static bool menuInfoSizeStillWanted(item_list_t *list, int configId, unsigned int generation)
{
    return infoSizeRequestGeneration == generation && infoSizeRequestList == list &&
           infoSizeRequestId == configId && selected_item != NULL &&
           selected_item->item != NULL && selected_item->item->current != NULL &&
           selected_item->item->userdata == list &&
           selected_item->item->current->item.id == configId;
}

// Resolve #Size for one request and publish it if the request is still wanted. Returns early (leaving
// nothing to clean up) when the target was superseded or abandoned.
static void menuResolveInfoSizeRequest(item_list_t *list, int configId, unsigned int generation)
{
    if (list == NULL || configId < 0)
        return;

    // #Size is cosmetic/discretionary IO. Wait for BGM reserve, but stop waiting on a target that
    // has already been superseded or a selection the user left; the next loop takes the newest.
    while (!bgmDiscretionaryIoAllowed()) {
        DelayThread(10 * 1000);
        raii::SemaLock lock(menuSemaId);
        if (!menuInfoSizeStillWanted(list, configId, generation))
            return;
    }

    // itemGetConfig runs outside menuSemaId: it is the slow CFG+stat operation.
    sbSetConfigStatSize(1);
    config_set_t *loadedConfig = gsOf(list)->config(list, configId);
    sbSetConfigStatSize(0);

    // Publish only if this request is STILL the newest target and the same row is selected.
    {
        raii::SemaLock lock(menuSemaId);
        if (loadedConfig != NULL && menuInfoSizeStillWanted(list, configId, generation)) {
            if (itemConfig != NULL)
                configFree(itemConfig);
            itemConfig = loadedConfig;
            itemConfigId = configId;
            itemConfigList = list;
            itemConfigGeneration++;
            loadedConfig = NULL;
        }
    }

    if (loadedConfig != NULL)
        configFree(loadedConfig);
}

// Opening the info screen needs #Size, which the scroll-time config load deliberately skips.
// The worker is single-instance but the TARGET is replaceable: if the user opens Info for B while A
// is still resolving, A is discarded when stale and the same worker immediately processes B.
static void _menuResolveInfoSize()
{
    for (;;) {
        item_list_t *list = NULL;
        int configId = -1;
        unsigned int generation = 0;

        {
            raii::SemaLock lock(menuSemaId);
            list = infoSizeRequestList;
            configId = infoSizeRequestId;
            generation = infoSizeRequestGeneration;
        }

        menuResolveInfoSizeRequest(list, configId, generation);

        raii::SemaLock lock(menuSemaId);
        if (infoSizeRequestGeneration == generation) {
            // Nobody replaced this target while it ran: the queue is drained.
            infoSizeRequestList = NULL;
            infoSizeRequestId = -1;
            infoSizeWorkerPending = 0;
            return;
        }
        // A newer target arrived while this one ran. Leave the worker latch set and loop directly;
        // no second IO request is needed and the newest request cannot be lost behind a queue failure.
    }
}

// Queued when the info screen opens: resolve #Size without blocking the UI. Same-row repeats dedupe;
// a different row replaces the target even when the worker is already active.
void menuRequestInfoSize(void)
{
    int queueWorker = 0;

    WaitSema(menuSemaId);
    if (selected_item == NULL || selected_item->item == NULL || selected_item->item->current == NULL) {
        SignalSema(menuSemaId);
        return;
    }

    item_list_t *list = static_cast<item_list_t *>(selected_item->item->userdata);
    int configId = selected_item->item->current->item.id;

    // Folder rows have no size; HDL rows already carry it in APA metadata (total_size_in_kb), so the
    // CFG+stat pass would be a redundant read. HDD PS1 rows go through: a one-game PP. partition is
    // only measured on demand.
    if (list == NULL || selected_item->item->current->item.isFolder ||
        (list->mode == HDD_MODE && libListRowView(list, configId) != LIB_VIEW_PS1)) {
        SignalSema(menuSemaId);
        return;
    }

    if (infoSizeWorkerPending && infoSizeRequestList == list && infoSizeRequestId == configId) {
        SignalSema(menuSemaId);
        return; // exact same work is already active/queued
    }

    infoSizeRequestList = list;
    infoSizeRequestId = configId;
    infoSizeRequestGeneration++;
    if (!infoSizeWorkerPending) {
        infoSizeWorkerPending = 1;
        queueWorker = 1;
    }
    SignalSema(menuSemaId);

    if (queueWorker && ioPutRequest(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&_menuResolveInfoSize)) != IO_OK) {
        WaitSema(menuSemaId);
        infoSizeWorkerPending = 0;
        infoSizeRequestList = NULL;
        infoSizeRequestId = -1;
        SignalSema(menuSemaId);
    }
}

static void _menuSaveConfig()
{
    int result;
    bdm_config_diag_snapshot_t diag;

    // bdmLogConfigWrite* report device identity by OPENING every BDM root -- up to nine blocking
    // device probes, most of them failed opens, which cost seconds apiece on a slow stick. Holding
    // menuSemaId across device I/O is the exact trap documented on _menuLoadConfigAsync above:
    // menuRenderElements takes that sema every frame, so the whole UI freezes for the duration.
    //
    // Nor may they keep borrowing itemConfigList/itemConfig once the sema is released -- either can
    // be freed the moment it is. So capture every value the report needs into a plain-value stack
    // snapshot while the sema IS held, then report from the copy with no lock and no borrowed
    // pointers. In a release build all three calls are no-ops; a diagnostic build is the one we hand
    // to a tester, and it has to measure the save rather than itself.
    WaitSema(menuSemaId);
    if (itemConfig == NULL) {
        actionStatus = 0;
        menuSaveResult = 0;
        SignalSema(menuSemaId);
        return;
    }
    bdmCaptureConfigWriteDiag(itemConfigList, itemConfig, &diag);
    SignalSema(menuSemaId);

    bdmLogConfigWriteEntry(&diag);

    WaitSema(menuSemaId);
    result = configWrite(itemConfig);
    itemConfigId = -1; // to invalidate cache and force reload
    itemConfigGeneration++;
    menuSaveResult = result; // publish BEFORE actionStatus=0 -- that flag is the waiter's release signal
    actionStatus = 0;
    SignalSema(menuSemaId);

    bdmLogConfigWriteResult(&diag, result);

    if (!result) {
        // gLastSaveErrno was latched inside configWrite at the real failure site.
        setErrorMessagePathCode(_STR_ERROR_SAVING_SETTINGS_TO, itemConfig ? itemConfig->filename() : "", gLastSaveErrno);
        diagLog("game settings: not saved to %s, error %d", itemConfig ? itemConfig->filename() : "", gLastSaveErrno);
    }
}

// Every path out of here MUST either queue a load that will clear actionStatus, or clear it itself.
// The old version had two paths that did neither: the selection changed but the idle-frame gate was
// not met, or the row was current with itemConfig still NULL. Both left actionStatus=1 with nothing
// in flight, so guiHandleDeferedIO span on a load that was never requested -- an outright hang
// before rebuild-38, and a 30s freeze plus a bogus error toast after it.
// NOTE(rebuild): the fork also swaps list->delay for MENU_APP_CONFIG_IDLE_FRAMES on APP_MODE. That
// constant does not exist here and is a separate tuning change, so the idle gate is left exactly as
// it was; only the completion contract is repaired.
static void _menuRequestConfig()
{
    int shouldQueueLoad = 0;
    int waiterPending;

    WaitSema(menuSemaId);
    // Which load variant to queue. A waiter (dialog path) needs the publish-unconditionally
    // in-sema load; the browse/settle path has none and gets the async one that keeps the sema
    // free across the device read. Read under the sema with everything else it decides on.
    waiterPending = (actionStatus != 0);
    if (selected_item == NULL || selected_item->item == NULL || selected_item->item->current == NULL) {
        // The list can be rebuilt out from under us between the GUI's check and this callback.
        actionStatus = 0;
    } else if (itemConfigId != selected_item->item->current->item.id ||
               itemConfigList != selected_item->item->userdata) {
        if (itemConfig) {
            configFree(itemConfig);
            itemConfig = NULL;
        }
        item_list_t *list = static_cast<item_list_t *>(selected_item->item->userdata);
        if (waiterPending || itemConfigId == -1 || guiInactiveFrames >= list->delay) {
            itemConfigId = selected_item->item->current->item.id;
            itemConfigList = list;
            itemConfigGeneration++;
            if (waiterPending || !itemConfigWorkerPending) {
                if (!waiterPending)
                    itemConfigWorkerPending = 1;
                shouldQueueLoad = 1;
            }
        } else
            actionStatus = 0; // still settling: nothing queued, so release the waiter now
    } else if (itemConfig == NULL && actionStatus != 0) {
        itemConfigGeneration++;
        shouldQueueLoad = 1; // right row, but its config never landed -- ask again
    } else
        actionStatus = 0;

    SignalSema(menuSemaId);

    // Queue OUTSIDE the sema: both load variants take it too.
    if (shouldQueueLoad && ioPutRequest(IO_CUSTOM_SIMPLEACTION, waiterPending ? (void *)&_menuLoadConfig : (void *)&_menuLoadConfigAsync) != IO_OK) {
        WaitSema(menuSemaId);
        if (!waiterPending)
            itemConfigWorkerPending = 0;
        if (itemConfig == NULL) {
            itemConfigId = -1; // let the next pass retry rather than caching a row that never loaded
            itemConfigGeneration++;
            itemConfigList = NULL;
        }
        actionStatus = 0;
        SignalSema(menuSemaId);
    }
}

// Favorites retain their own row IDs but use the owning device for bus policy.
static int menuConfigSourceMode(item_list_t *list)
{
    if (list != NULL && list->mode == FAV_MODE && selected_item != NULL &&
        selected_item->item != NULL && selected_item->item->userdata == list &&
        selected_item->item->current != NULL)
        return favGetItemSourceMode(selected_item->item->current->item.id);
    return list != NULL ? list->mode : -1;
}

config_set_t *menuLoadConfig()
{
    item_list_t *list = NULL;
    config_set_t *loadedConfig = NULL;
    config_set_t *result = NULL;
    int configId = -1;

    // Launch configuration is foreground work. Sending it through the single ioman FIFO makes a
    // game launch wait behind unrelated device/list rescans and browse-time metadata; on MX4SIO
    // that can pin the UI at "Loading config..." before the game-side IOP reset is even reached.
    WaitSema(menuSemaId);
    if (selected_item != NULL && selected_item->item != NULL && selected_item->item->current != NULL) {
        list = static_cast<item_list_t *>(selected_item->item->userdata);
        configId = selected_item->item->current->item.id;

        if (itemConfig != NULL && itemConfigId == configId && itemConfigList == list) {
            result = itemConfig; // browse-time prefetch already paid the device read
        } else {
            if (itemConfig != NULL) {
                configFree(itemConfig);
                itemConfig = NULL;
            }
            itemConfigId = configId;
            itemConfigList = list;
            itemConfigGeneration++;
        }
    } else {
        if (itemConfig != NULL) {
            configFree(itemConfig);
            itemConfig = NULL;
        }
        itemConfigId = -1;
        itemConfigGeneration++;
        itemConfigList = NULL;
    }
    actionStatus = 0;
    SignalSema(menuSemaId);

    if (list == NULL || configId < 0)
        return result;

    // Launch owns the storage path now. Drop speculative art before any launch I/O or cached return.
    // A zero wait requests cancellation but never kills or waits on an in-flight fileXio/SIO2 RPC;
    // that transaction is allowed to finish cleanly while no new queued art can get in front.
    if (menuConfigSourceMode(list) == MMCE_MODE)
        (void)cacheAbortMmceImageLoadsTimed(0);
    else
        (void)cacheCancelPendingImageLoadsTimed(0);

    if (result != NULL)
        return result;

    loadedConfig = gsOf(list)->config(list, configId);

    // A browse-time load may have completed while the direct read was running. Publish only if this
    // selection still owns the cache; otherwise free the duplicate/stale result.
    WaitSema(menuSemaId);
    if (itemConfig == NULL && itemConfigId == configId && itemConfigList == list) {
        itemConfig = loadedConfig;
        loadedConfig = NULL;
    }
    if (itemConfigId == configId && itemConfigList == list)
        result = itemConfig;
    SignalSema(menuSemaId);

    if (loadedConfig != NULL)
        configFree(loadedConfig);

    return result;
}

config_set_t *menuTakeLaunchConfig(config_set_t *expected, item_list_t *list, int id)
{
    config_set_t *owned = NULL;
    WaitSema(menuSemaId);
    if (itemConfig == expected && itemConfigList == list && itemConfigId == id) {
        owned = itemConfig;
        itemConfig = NULL;
        itemConfigId = -1;
        itemConfigGeneration++;
        itemConfigList = NULL;
    }
    SignalSema(menuSemaId);
    return owned;
}

void menuInvalidateConfig()
{
    WaitSema(menuSemaId);
    itemConfigId = -1; // a modal launch may have persisted after an old prefetch completed
    itemConfigGeneration++;
    SignalSema(menuSemaId);
}

// we don't want a pop up when transitioning to or refreshing Game Menu gui.
config_set_t *gameMenuLoadConfig(struct UIItem *ui)
{
    (void)ui;
    // Settings are foreground work too: the browse settle gate can return without
    // loading, and the shared worker may be occupied by unrelated device scans.
    padRumbleFlush();
    return menuLoadConfig();
}

int menuSaveConfig()
{
    actionStatus = 1;
    menuSaveResult = 0;
    guiHandleDeferedIO(&actionStatus, _l(_STR_SAVING_SETTINGS), IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&_menuSaveConfig), OPL_DEFERRED_IO_TIMEOUT_MS);
    return menuSaveResult;
}

int menuSaveEmberGameSettings(config_set_t *configSet)
{
    item_list_t *support;
    int id;
    char *name;

    if (selected_item == NULL || selected_item->item == NULL ||
        selected_item->item->current == NULL || configSet == NULL)
        return 0;
    support = static_cast<item_list_t *>(selected_item->item->userdata);
    id = selected_item->item->current->item.id;
    if (support == NULL || !gsOf(support)->hasSaveCueSettings())
        return 0;
    name = gsOf(support)->name(support, id);
    if (name == NULL || name[0] == '\0')
        return 0;

    guiRenderTextScreen(_l(_STR_SAVING_SETTINGS));
    return gsOf(support)->saveCueSettings(support, id, name, configSet);
}

static int menuSaveAllSettings(void)
{
    int result;

    guiGameSaveOSDLanguageGlobalConfig(configGetByType(CONFIG_GAME));
#ifdef PADEMU
    guiGameSavePadEmuGlobalConfig(configGetByType(CONFIG_GAME));
    guiGameSavePadMacroGlobalConfig(configGetByType(CONFIG_GAME));
#endif
    // CONFIG_LAST is in the list for the remembered L3 positions (_saveConfig folds them in there).
    // It is otherwise only written when a game launches, and that write is gated on Remember Last
    // Played -- so without this a user with that setting off could never persist a view. It costs
    // nothing when nothing changed: configWrite skips an unmodified set without touching the disk.
    result = saveConfig(CONFIG_OPL | CONFIG_NETWORK | CONFIG_GAME | CONFIG_LAST, 1);
    menuSetParentalLockCheckState(1); // Re-enable parental lock check.

    return result;
}

int menuSaveSettings(void)
{
    int result;

    if (menuCheckParentalLock() != 0)
        return 0;

    result = menuSaveAllSettings();

    // SMB, UDPFS and UDPBD share the one SMAP NIC and cannot coexist -- each loader refuses to
    // start while either of the others is resident, and each loads its IOP chain once per boot.
    // Once a stack is up, a changed protocol applies after a restart.
    if (result > 0 && guiNetProtocolNeedsRestart() && guiMsgBox(_l(_STR_NETBOOT_RESTART), 1, NULL))
        sysExecExit();

    return result;
}

// The network page's "restart now?" Yes: the user already agreed, so save and restart without asking
// again. A failed save stays in OPL -- restarting would throw the protocol change away.
void menuSaveSettingsAndRestart(void)
{
    if (menuCheckParentalLock() != 0)
        return;

    if (menuSaveAllSettings() > 0)
        sysExecExit();
}

#ifdef RETROACHIEVEMENTS
/* The RA achievement browser (adapted from Rian6/caduceus-opl 2a98a9d, AFL-3.0, via the original):
   the account library ('G') and one game's set ('A'), three rows a page, from a game card or the
   main menu. The I/O thread fetches; this screen draws the latest snapshot. */
static int achFromCard, achKind, achPage, achLibraryPage, achFilter, achSelected, achNeedsPage;
static int achImageVcd, achReturnScreen; // the card's image is a PS1 VCD; the menu it was opened from
static char achTarget[33], achImagePath[256], achImageStartup[16], achImagePrefix[256];
static image_cache_t *achIcons;
static int achIconIds[ACH_PAGE_SIZE] = {-1, -1, -1}, achIconUids[ACH_PAGE_SIZE];

static void menuCardText(int x, int y, int w, int h, const char *text, u64 color)
{
    fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, w, h, text, color);
}

// hash: NULL for the account library, "image" for the set of the image already requested.
static void menuOpenAchievements(const char *hash)
{
    achFromCard = hash != NULL;
    achReturnScreen = GUI_SCREEN_GAME_MENU;
    achImageVcd = 0;
    achKind = hash ? 'A' : 'G';
    snprintf(achTarget, sizeof(achTarget), "%s", hash ? hash : "0");
    achPage = achLibraryPage = achFilter = achSelected = 0;
    achNeedsPage = hash == NULL;
    guiSwitchScreen(GUI_SCREEN_ACHIEVEMENTS);
}

static void achievementReload(void)
{
    achNeedsPage = 1;
    achSelected = 0;
    for (int i = 0; i < ACH_PAGE_SIZE; i++)
        achIconIds[i] = -1;
}

// Badge images live under ART/ on the same share as ART/CADUCEUS.KEY.
static GSTEXTURE *achievementIcon(int row, const char *key)
{
    if (!key[0])
        return NULL;
    item_list_t *provider = gNetworkProtocol == NET_PROTO_UDPFS ? udpfsGetObject(0) : ethGetObject(0);
    if (!provider)
        return NULL;
    if (!achIcons)
        achIcons = cacheInitCache(-1, "ART", 1, "RA", ACH_PAGE_SIZE);
    if (!achIcons)
        return NULL;
    return cacheGetTexture(achIcons, provider, &achIconIds[row], &achIconUids[row], (char *)key);
}

void menuRenderAchievements()
{
    const u64 text = gTheme->textColor, accent = gTheme->selTextColor, panel = gColDarker;
    const char *filters[] = {_l(_STR_CAD_FILTER_ALL), _l(_STR_CAD_EARNED), _l(_STR_CAD_LOCKED), _l(_STR_CAD_HARDCORE)};
    achievement_page_t page;
    char line[160];

    guiDrawBGPlasma();
    if (achNeedsPage && !discCheckBusy() && achievementsRequest(achKind, achPage, achFilter, achTarget)) {
        achNeedsPage = 0;
        for (int i = 0; i < ACH_PAGE_SIZE; i++)
            achIconIds[i] = -1;
    }
    achievementsSnapshot(&page);
    if (achNeedsPage)
        page.state = ACH_LOADING;
    menuCardText(36, 28, 380, 24, _l(_STR_CAD_HEADING), accent);
    menuCardText(436, 28, 170, 24, page.user, text);
    rmDrawLine(36, 59, 604, 59, panel);
    menuCardText(36, 74, 560, 42, achKind == 'G' ? _l(_STR_CAD_LIBRARY) : page.title, text);
    if (page.state == ACH_READY) {
        if (achFromCard && page.gameId > 0)
            snprintf(achTarget, sizeof(achTarget), "%d", page.gameId);
        int pages = (page.total + ACH_PAGE_SIZE - 1) / ACH_PAGE_SIZE;
        snprintf(line, sizeof(line), "%d / %d", page.total ? page.page + 1 : 0, pages);
        menuCardText(518, 132, 90, 20, line, text);
        if (achKind == 'A') {
            snprintf(line, sizeof(line), _l(_STR_CAD_PROGRESS), page.earned, page.maximum, filters[achFilter]);
            menuCardText(36, 132, 470, 22, line, accent);
            rmDrawRect(36, 120, 568, 3, panel);
            if (page.maximum)
                rmDrawRect(36, 120, (int)(568LL * page.earned / page.maximum), 3, accent);
        } else {
            snprintf(line, sizeof(line), _l(page.total == 1 ? _STR_CAD_ONE_GAME : _STR_CAD_GAMES), page.total);
            menuCardText(36, 132, 460, 22, line, text);
        }
        if (achSelected >= page.count)
            achSelected = 0;
        for (int i = 0; i < page.count; i++) {
            achievement_entry_t *entry = &page.entries[i];
            int y = 164 + i * 65;
            rmDrawRect(36, y, 568, 59, panel);
            if (i == achSelected)
                rmDrawRect(36, y, 3, 59, accent);
            GSTEXTURE *icon = achievementIcon(i, entry->icon);
            if (icon && icon->Mem)
                rmDrawPixmap(icon, 45, y + 5, ALIGN_NONE, 48, 48, SCALING_RATIO, gDefaultCol, 0);
            else { // a padlock
                rmDrawRect(58, y + 13, 20, 17, text);
                rmDrawRect(65, y + 30, 6, 10, text);
                rmDrawRect(58, y + 40, 20, 3, text);
            }
            menuCardText(108, y + 7, 480, 20, entry->title, text);
            if (achKind == 'G')
                snprintf(line, sizeof(line), _l(_STR_CAD_GAME_PROGRESS), entry->earned, entry->total, entry->hardcore);
            else
                snprintf(line, sizeof(line), _l(_STR_CAD_POINTS), entry->points,
                         _l(entry->hardcore ? _STR_CAD_HARDCORE : entry->earned ? _STR_CAD_EARNED :
                                                                                  _STR_CAD_LOCKED));
            menuCardText(108, y + 32, 480, 20, line, entry->earned ? accent : text);
        }
        if (page.count) {
            achievement_entry_t *entry = &page.entries[achSelected];
            menuCardText(36, 367, 560, 42, entry->description, text);
            if (achKind == 'A' && entry->date[0])
                menuCardText(36, 408, 560, 20, entry->date, text);
        } else
            menuCardText(36, 195, 555, 60, _l(achKind == 'G' ? _STR_CAD_EMPTY_LIBRARY : _STR_CAD_EMPTY_FILTER), text);
    } else {
        const int xera = gRAMode == RA_MODE_XERABORA;
        int message = xera ? _STR_XERA_CONNECTION_ERROR : _STR_CAD_CONNECTION_ERROR;
        if (page.state == ACH_LOADING)
            message = _STR_CAD_LOADING;
        else if (page.state == ACH_UNSUPPORTED)
            message = xera ? _STR_XERA_UNKNOWN_GAME : _STR_CAD_UNKNOWN_GAME;
        else if (page.state == ACH_NOT_PLAYED)
            message = achievementsClockUnset() ? _STR_XERA_SET_CLOCK : _STR_XERA_NOT_PLAYED;
        else if (page.state == ACH_OFFLINE) {
            if (xera) // the xeRAbora list travels over TCP: no UDP transport may own the NIC
                message = raNetNicReserved() ? _STR_RA_NET_BUSY : _STR_XERA_OPEN_NETWORK;
            else if (raNetUdpfsNotLoaded())
                message = _STR_RA_UDPFS_LOAD_FIRST;
            else
                message = gNetworkProtocol == NET_PROTO_UDPFS   ? _STR_CAD_PAIR_UDPFS :
                          gNetworkProtocol == NET_PROTO_UDPFSBD ? _STR_CAD_UDPFS_IMG :
                                                                  _STR_CAD_PAIR;
        }
        menuCardText(48, 188, 535, 110, _l(message), text);
        menuCardText(48, 315, 535, 40, _l(_STR_CAD_BACK_REFRESH), text);
    }
    rmDrawLine(36, 433, 604, 433, panel);
    menuCardText(36, 442, 465, 22, _l(achKind == 'G' ? _STR_CAD_LIBRARY_CONTROLS : _STR_CAD_GAME_CONTROLS), text);
    GSTEXTURE *back = thmGetTexture(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON);
    if (back && back->Mem)
        rmDrawPixmap(back, 519, 442, ALIGN_NONE, 16, 16, SCALING_RATIO, gDefaultCol, 0);
    menuCardText(542, 442, 65, 22, _l(_STR_CAD_BACK), text);
}

void menuHandleInputAchievements()
{
    achievement_page_t page;
    achievementsSnapshot(&page);
    if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE) || (getKeyOn(KEY_START) && !achFromCard && achKind == 'G')) {
        if (achFromCard)
            guiSwitchScreen(achReturnScreen);
        else if (achKind == 'A') { // back from a game to the library page it was opened from
            achKind = 'G';
            achPage = achLibraryPage;
            achFilter = 0;
            strcpy(achTarget, "0");
            achievementReload();
        } else
            guiSwitchScreen(GUI_SCREEN_MENU);
        return;
    }
    if (discCheckBusy() || achNeedsPage)
        return;
    if (getKeyOn(KEY_SELECT)) {
        if (achFromCard && !strcmp(achTarget, "image")) {
            if (achImageVcd)
                achievementsRequestVcd(achImagePath);
            else
                achievementsRequestImage(achImagePath, achImageStartup, achImagePrefix);
        } else {
            achievementsForget();
            achievementReload();
        }
        return;
    }
    if (page.state != ACH_READY)
        return;
    if (getKeyOn(KEY_SQUARE) && achKind == 'A') {
        achFilter = (achFilter + 1) % 4;
        achPage = 0;
        achievementReload();
    } else if (getKeyOn(KEY_L1) && achPage > 0) {
        achPage--;
        achievementReload();
    } else if (getKeyOn(KEY_R1) && (achPage + 1) * ACH_PAGE_SIZE < page.total) {
        achPage++;
        achievementReload();
    } else if (getKey(KEY_UP) && page.count) {
        achSelected = (achSelected + page.count - 1) % page.count;
        sfxPlay(SFX_CURSOR);
    } else if (getKey(KEY_DOWN) && page.count) {
        achSelected = (achSelected + 1) % page.count;
        sfxPlay(SFX_CURSOR);
    } else if (getKeyOn(gSelectButton) && achKind == 'G' && page.count) {
        achLibraryPage = achPage;
        snprintf(achTarget, sizeof(achTarget), "%d", page.entries[achSelected].id);
        achKind = 'A';
        achPage = achFilter = 0;
        achievementReload();
    }
}
#endif

static void menuInitMainMenu(void)
{
    if (mainMenu)
        submenuDestroy(&mainMenu);

    // initialize the menu
    submenuAppendItem(&mainMenu, -1, NULL, MENU_LAUNCH_PS2_DISC, _STR_LAUNCH_PS2_DISC);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_RANDOM_GAME, _STR_RANDOM_GAME);
#ifdef RETROACHIEVEMENTS
    // The label follows RA Mode: a mode change rebuilds this menu (guisettings).
    submenuAppendItem(&mainMenu, -1, NULL, MENU_RA_ACHIEVEMENTS, gRAMode == RA_MODE_CADUCEUS ? _STR_CAD_ACHIEVEMENTS : _STR_XERA_ACHIEVEMENTS);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_RA_DISC_CHECK, _STR_RA_DISC_CHECK);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_RA_DISC_LAUNCH, _STR_RA_DISC_LAUNCH);
#endif
    submenuAppendItem(&mainMenu, -1, NULL, MENU_SETTINGS, _STR_SETTINGS);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_NBD, _STR_STARTNBD);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_DRIVE_STATUS, _STR_DRIVE_STATUS);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_FILE_MANAGER, _STR_FILE_MANAGER);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_ABOUT, _STR_ABOUT);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_EXIT, _STR_EXIT);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_POWER_OFF, _STR_POWEROFF);

    mainMenuCurrent = mainMenu;
}

void menuReinitMainMenu(void)
{
    menuInitMainMenu();
}

void menuInitGameMenu(item_list_t *support)
{
    if (gameMenu)
        submenuDestroy(&gameMenu);

    // initialize the menu
    submenuAppendItem(&gameMenu, -1, NULL, GAME_COMPAT_SETTINGS, _STR_COMPAT_SETTINGS);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_CHEAT_SETTINGS, _STR_CHEAT_SETTINGS);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_GSM_SETTINGS, _STR_GSCONFIG);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_VMC_SETTINGS, _STR_VMC_SCREEN);
#ifdef PADEMU
    submenuAppendItem(&gameMenu, -1, NULL, GAME_PADEMU_SETTINGS, _STR_PADEMUCONFIG);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_PADMACRO_SETTINGS, _STR_PADMACROCONFIG);
#endif
    submenuAppendItem(&gameMenu, -1, NULL, GAME_OSD_LANGUAGE_SETTINGS, _STR_OSD_SETTINGS);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_SAVE_CHANGES, _STR_SAVE_CHANGES);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_TEST_CHANGES, _STR_TEST);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_REMOVE_CHANGES, _STR_REMOVE_ALL_SETTINGS);
    if (gEnableWrite) {
        // Offer only what the page can do. Favorites keeps both so it can explain why not. The APA
        // HDD page neither renames nor deletes PS2 games: both would edit the partition table.
        // A read-only server would only seem to rename or delete (YAOPL 9f556b55).
        int fav = support != NULL && support->mode == FAV_MODE;
        int readOnly = support != NULL && !fav && (support->flags & MODE_FLAG_READ_ONLY);
        if (fav || (support != NULL && support->mode != HDD_MODE && !readOnly))
            submenuAppendItem(&gameMenu, -1, NULL, GAME_RENAME_GAME, _STR_RENAME);
        if (fav || (support != NULL && gsOf(support)->hasDelete() && !readOnly))
            submenuAppendItem(&gameMenu, -1, NULL, GAME_DELETE_GAME, _STR_DELETE);
        item_list_t *hideSource;
        const char *hideName;
        int hidePs1, hidden;
        if (menuHiddenState(&hideSource, &hidePs1, &hideName, &hidden))
            submenuAppendItem(&gameMenu, -1, NULL, GAME_HIDE_GAME, hidden ? _STR_UNHIDE_GAME : _STR_HIDE_GAME);
    }
#ifdef RETROACHIEVEMENTS
    submenuAppendItem(&gameMenu, -1, NULL, GAME_RA_CHECK, _STR_RA_CHECK_GAME);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_RA_TEST, _STR_RA_TEST_LINK);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_RA_ACHIEVEMENTS, _STR_CAD_VIEW_ACHIEVEMENTS);
#endif

    gameMenuCurrent = gameMenu;
}

void menuInitAppMenu(void)
{
    if (appMenu)
        submenuDestroy(&appMenu);

    // initialize the menu
    submenuAppendItem(&appMenu, -1, NULL, APP_RENAME, _STR_RENAME);
    submenuAppendItem(&appMenu, -1, NULL, APP_DELETE, _STR_DELETE);

    appMenuCurrent = appMenu;
}

void menuInitVcdMenu(int isEmber, int readOnly)
{
    if (appMenu)
        submenuDestroy(&appMenu);

    // PS1 titles are handed off to POPSTARTER or Ember, so this menu must not imply that RiptOPL
    // owns per-game emulator settings. Link to the existing GLOBAL PS emulation page instead, and
    // keep this item-operation path free of PS2 per-game CFG reads/creation.
    submenuAppendItem(&appMenu, -1, NULL, APP_GLOBAL_PS_SETTINGS, _STR_GLOBAL_PS_EMULATION_SETTINGS);
    // Ember reads a per-game settings.txt; POPSTARTER has nothing equivalent, so only Ember rows get it.
    // Both write to the game's device, which a read-only server would only seem to keep.
    if (isEmber && !readOnly)
        submenuAppendItem(&appMenu, -1, NULL, APP_EMBER_GAME_SETTINGS, _STR_EMBER_GAME_SETTINGS);
    if (!readOnly)
        submenuAppendItem(&appMenu, -1, NULL, APP_RENAME, _STR_RENAME);
    item_list_t *hideSource;
    const char *hideName;
    int hidePs1, hidden;
    if (menuHiddenState(&hideSource, &hidePs1, &hideName, &hidden))
        submenuAppendItem(&appMenu, -1, NULL, APP_HIDE_GAME, hidden ? _STR_UNHIDE_GAME : _STR_HIDE_GAME);
#ifdef RETROACHIEVEMENTS
    // POPStarter VCDs can be tracked (USB only, said on use); Ember launches carry no telemetry.
    if (!isEmber) {
        submenuAppendItem(&appMenu, -1, NULL, GAME_RA_CHECK, _STR_RA_CHECK_GAME);
        submenuAppendItem(&appMenu, -1, NULL, GAME_RA_TEST, _STR_RA_TEST_LINK);
        submenuAppendItem(&appMenu, -1, NULL, GAME_RA_ACHIEVEMENTS, _STR_CAD_VIEW_ACHIEVEMENTS);
    }
#endif

    appMenuCurrent = appMenu;
}

// -------------------------------------------------------------------------------------------
// ---------------------------------------- Menu manipulation --------------------------------
// -------------------------------------------------------------------------------------------
void menuInit()
{
    menu = NULL;
    selected_item = NULL;
    itemConfigId = -1;
    itemConfigGeneration++;
    itemConfig = NULL;
    itemConfigList = NULL;
    itemConfigWorkerPending = 0;
    infoSizeWorkerPending = 0;
    infoSizeRequestList = NULL;
    infoSizeRequestId = -1;
    infoSizeRequestGeneration = 0;
    mainMenu = NULL;
    mainMenuCurrent = NULL;
    gameMenu = NULL;
    gameMenuCurrent = NULL;
    appMenu = NULL;
    appMenuCurrent = NULL;
    menuInitMainMenu();

    // Create once; recreating on a second menuInit would leak the prior semaphore.
    if (menuSemaId < 0) {
        menuSema.init_count = 1;
        menuSema.max_count = 1;
        menuSema.option = 0;
        menuSemaId = CreateSema(&menuSema);
    }
    if (menuHintSemaId < 0) {
        menuHintSema.init_count = 1;
        menuHintSema.max_count = 1;
        menuHintSema.option = 0;
        menuHintSemaId = CreateSema(&menuHintSema);
    }
    if (menuListSemaId < 0) {
        menuListSemaId = sbCreateSemaphore();
    }
}

void menuEnd()
{
    // destroy menu
    menu_list_t *cur = menu;

    while (cur) {
        menu_list_t *td = cur;
        cur = cur->next;

        if (td->item)
            submenuDestroy(&(td->item->submenu));

        menuHintsLock();
        menuRemoveHints(td->item);
        menuHintsUnlock();

        free(td);
    }

    submenuDestroy(&mainMenu);
    submenuDestroy(&gameMenu);
    submenuDestroy(&appMenu);

    if (itemConfig) {
        configFree(itemConfig);
        itemConfig = NULL;
    }

    DeleteSema(menuSemaId);
    menuSemaId = -1;

    // PUBLISH "GONE" BEFORE DELETING, not after. ioEnd() only sets gIOTerminate and wakes the io
    // worker -- it does not join it -- so a queued moduleUpdateMenuInternal() can still be running
    // when we get here. Clearing the id first means such a caller either reads -1 and no-ops
    // (menuHintsLock's not-ready guard) or has already sampled a live id; deleting first would
    // additionally let it sample an id we are in the middle of destroying.
    //
    // This narrows the window rather than closing it, and the same race already exists for
    // menuSemaId above -- which ioman handlers (_menuSaveConfig, _menuLoadConfig,
    // _menuLoadConfigAsync) take directly with no not-ready guard at all. Closing it properly means
    // joining the io worker before support cleanup, which is a shutdown-ordering change and does
    // not belong in a hint-list lock.
    {
        s32 hintSema = menuHintSemaId;
        menuHintSemaId = -1;
        if (hintSema >= 0)
            DeleteSema(hintSema);
    }
    DeleteSema(menuListSemaId);
    menuListSemaId = -1;
}

static menu_list_t *AllocMenuItem(menu_item_t *item)
{
    menu_list_t *it;

    it = static_cast<menu_list_t *>(malloc(sizeof(menu_list_t)));
    if (it == NULL)
        return NULL;

    it->prev = NULL;
    it->next = NULL;
    it->item = item;

    return it;
}

static int menuDevicePageRank(const menu_item_t *item)
{
    item_list_t *support = item != NULL ? static_cast<item_list_t *>(item->userdata) : NULL;
    if (support == NULL)
        return 6;

    if (support->mode >= BDM_MODE && support->mode <= BDM_MODE_LAST)
        return 0;
    if (support->mode == MMCE_MODE)
        return 1;
    if (support->mode == HDD_MODE)
        return 2;
    if (support->mode == ETH_MODE || support->mode == UDPFS_MODE || support->mode == HTTP_MODE)
        return 3;
    if (support->mode == APP_MODE)
        return 4;
    if (support->mode == FAV_MODE)
        return 5;

    return 6;
}

void menuAppendItem(menu_item_t *item)
{
    assert(item);

    WaitSema(menuListSemaId);

    menu_list_t *newitem = AllocMenuItem(item);
    if (newitem == NULL) {
        SignalSema(menuListSemaId);
        return;
    }
    if (menu == NULL) {
        menu = newitem;
        selected_item = menu;
    } else {
        // The GUI page order is independent of support initialization order. List and Coverflow
        // share this same ring, so keep both views in the Korium order:
        // BDM, MMCE, APA HDD, NET, APPS, FAV.
        const int rank = menuDevicePageRank(item);
        if (rank < menuDevicePageRank(menu->item)) {
            newitem->next = menu;
            menu->prev = newitem;
            menu = newitem;
        } else {
            menu_list_t *cur = menu;
            while (cur->next != NULL && menuDevicePageRank(cur->next->item) <= rank)
                cur = cur->next;

            newitem->next = cur->next;
            newitem->prev = cur;
            if (cur->next != NULL)
                cur->next->prev = newitem;
            cur->next = newitem;
        }
    }

    SignalSema(menuListSemaId);
}

static int menuIsFavPage(const menu_list_t *page)
{
    const item_list_t *support = page != NULL && page->item != NULL ? static_cast<const item_list_t *>(page->item->userdata) : NULL;
    return support != NULL && support->mode == FAV_MODE;
}

static void refreshMenuPosition(void)
{
    // Returning from Start/menu/settings must retain the page the user paused on. The old helper
    // always selected the first visible tab here, which is commonly USB, even when the current page
    // remained present. Fall back only when that exact menu vanished or became invisible.
    if (menu == NULL)
        return;

    menu_list_t *cur = menu;
    while (cur != NULL) {
        if (cur == selected_item && cur->item != NULL && menuItemShown(cur->item))
            return;
        cur = cur->next;
    }

    // The retained page is no longer usable. With All Games on that is usually a device tab that
    // just stepped aside, so go to the All Games page. With it off it is usually the Favourites page
    // All Games left behind, so go to the startup page, as a boot would; otherwise the first visible.
    int target = gAllGames ? FAV_MODE : (menuIsFavPage(selected_item) ? gDefaultDevice : -1);
    for (cur = menu; cur != NULL; cur = cur->next) {
        item_list_t *support = cur->item != NULL ? static_cast<item_list_t *>(cur->item->userdata) : NULL;
        if (support != NULL && support->mode == target && menuItemShown(cur->item)) {
            selected_item = cur;
            return;
        }
    }
    cur = menu;
    while (!menuItemShown(cur->item) && cur->next)
        cur = cur->next;

    if (!menuItemShown(cur->item)) {
        // No visible menu was found, just set the current menu to the first one in the list.
        selected_item = menu;
    } else
        selected_item = cur;
}

void menuResetPageStarts(void)
{
    for (menu_list_t *cur = menu; cur != NULL; cur = cur->next) {
        if (cur->item != NULL)
            cur->item->pagestart = cur->item->current;
    }
}

void submenuRebuildCache(submenu_list_t *submenu)
{
    while (submenu) {
        int *newId = NULL;
        int *newUid = NULL;
        int count = gTheme != NULL ? gTheme->gameCacheCount : 0;

        if (count > 0 && (size_t)count <= (size_t)-1 / sizeof(int)) {
            size_t bytes = (size_t)count * sizeof(int);
            newId = static_cast<int *>(malloc(bytes));
            if (newId != NULL)
                newUid = static_cast<int *>(malloc(bytes));
            if (newId != NULL && newUid != NULL) {
                memset(newId, -1, bytes);
                memset(newUid, -1, bytes);
            } else {
                // A half-allocated pair cannot be indexed safely by the renderer.
                free(newId);
                free(newUid);
                newId = NULL;
                newUid = NULL;
            }
        }

        // Drop arrays from the previous theme size even after allocation failure:
        // retaining them could let the new theme index beyond their old capacity.
        free(submenu->item.cache_id);
        free(submenu->item.cache_uid);
        submenu->item.cache_id = newId;
        submenu->item.cache_uid = newUid;
        submenu->item.cache_count = newId != NULL ? count : 0;
        submenu->item.cache_gen = newId != NULL ? gTheme->artGeneration : 0;
        submenu = submenu->next;
    }
}

// True once the global menu list contains a registered device/mode tab (returns menu != NULL).
// initSupport uses the FALSE case to spot "user enabled the FIRST tab from the start menu",
// where nothing would otherwise take them to it (#254).
int menuHasRegisteredItems(void)
{
    return menu != NULL;
}

static submenu_list_t *submenuAllocItem(int icon_id, char *text, int id, int text_id)
{
    submenu_list_t *it = (submenu_list_t *)malloc(sizeof(submenu_list_t));
    if (it == NULL)
        return NULL;

    it->prev = NULL;
    it->next = NULL;
    it->item.icon_id = icon_id;
    it->item.text = text;
    it->item.text_id = text_id;
    it->item.id = id;
    it->item.cache_id = NULL;
    it->item.cache_uid = NULL;
    it->item.cache_count = 0;
    it->item.cache_gen = 0;
    it->item.favourited = 0;
    it->item.isFolder = 0;
    it->item.lastPlayed = 0;
#ifdef RETROACHIEVEMENTS
    it->item.raBadged = 0;
    it->item.raText = NULL;
#endif
    submenuRebuildCache(it);

    return it;
}

submenu_list_t *submenuAppendItem(submenu_list_t **submenu, int icon_id, char *text, int id, int text_id)
{
    if (*submenu == NULL) {
        *submenu = submenuAllocItem(icon_id, text, id, text_id);
        return *submenu;
    }

    submenu_list_t *cur = *submenu;

    // traverse till the end
    while (cur->next)
        cur = cur->next;

    // create new item
    submenu_list_t *newitem = submenuAllocItem(icon_id, text, id, text_id);
    if (newitem == NULL)
        return NULL;

    // link
    cur->next = newitem;
    newitem->prev = cur;

    return newitem;
}

static void submenuDestroyItem(submenu_list_t *submenu)
{
    gridForgetItem(submenu);
#ifdef RETROACHIEVEMENTS
    free(submenu->item.raText);
#endif
    free(submenu->item.cache_id);
    free(submenu->item.cache_uid);

    free(submenu);
}

void submenuDestroy(submenu_list_t **submenu)
{
    // destroy sub menu
    submenu_list_t *cur = *submenu;

    while (cur) {
        submenu_list_t *td = cur;
        cur = cur->next;

        submenuDestroyItem(td);
    }

    *submenu = NULL;
}

void menuAddHint(menu_item_t *menu, int text_id, int icon_id)
{
    // allocate a new hint item
    menu_hint_item_t *hint = static_cast<menu_hint_item_t *>(malloc(sizeof(menu_hint_item_t)));
    if (hint == NULL)
        return;

    hint->text_id = text_id;
    hint->icon_id = icon_id;
    hint->next = NULL;

    if (menu->hints) {
        menu_hint_item_t *top = menu->hints;

        // rewind to end
        for (; top->next; top = top->next)
            ;

        top->next = hint;
    } else {
        menu->hints = hint;
    }
}

void menuRemoveHints(menu_item_t *menu)
{
    while (menu->hints) {
        menu_hint_item_t *hint = menu->hints;
        menu->hints = hint->next;
        free(hint);
    }
}

char *menuItemGetText(menu_item_t *it)
{
    if (it->text_id >= 0)
        return _l(it->text_id);
    else
        return it->text;
}

char *submenuItemGetText(submenu_item_t *it)
{
#ifdef RETROACHIEVEMENTS
    if (it->raText != NULL)
        return it->raText;
#endif
    if (it->text_id >= 0)
        return _l(it->text_id);
    else
        return it->text;
}

// Folder browsing: folders group ahead of games; within each group sort by the on-screen title.
static int submenuSortCompare(submenu_list_t *a, submenu_list_t *b, item_list_t *support)
{
    const char *txt1, *txt2;

    if (a->item.isFolder != b->item.isFolder)
        return a->item.isFolder ? -1 : 1;

    // Sort Games: Last played (RIPPS2 build 115): recent games first, newest first, then the rest A to Z.
    if (gAutosort == AUTOSORT_LAST_PLAYED && !a->item.isFolder && a->item.lastPlayed != b->item.lastPlayed) {
        if (a->item.lastPlayed == 0 || b->item.lastPlayed == 0)
            return a->item.lastPlayed == 0 ? 1 : -1;
        return a->item.lastPlayed - b->item.lastPlayed;
    }

    // The source text, never the RA mark: badged and plain rows sort by the same clean title.
    txt1 = a->item.text_id >= 0 ? _l(a->item.text_id) : a->item.text;
    txt2 = b->item.text_id >= 0 ? _l(b->item.text_id) : b->item.text;
    if (!a->item.isFolder) {
        txt1 = cfgTitleSortName(support, a->item.id, txt1);
        txt2 = cfgTitleSortName(support, b->item.id, txt2);
    }
    return strcasecmp(txt1, txt2);
}

/* Sorts the given submenu by comparing the on-screen titles.

   A bottom-up merge sort over the linked list: O(n log n) comparisons and no extra memory. This was
   a bubble sort, O(n^2), and it runs on every list rebuild while Sort Games Alphabetically is on
   (the default) -- a rescan, an L3 view change, a device attach. The scan hands rows over in REVERSE
   directory order (scanForISO prepends), which on a library copied in alphabetical order is the
   bubble sort's worst case: its passes ran the whole list every time, so 1000 games cost ~1,000,000
   comparisons, each a strcasecmp behind two display-name lookups, drained by the GUI thread between
   frames. The merge sort needs about 5,000 for the same list, and a list already in order still
   costs one pass, as it did before.

   Both sorts are stable -- a tie keeps its current order -- and use the same comparison, so the
   resulting order is identical, row for row. */
void submenuSort(submenu_list_t **submenu, int mode)
{
    submenu_list_t *list, *p, *q, *e, *tail;
    int insize, nmerges, psize, qsize, i;

    if ((submenu == NULL) || (*submenu == NULL) || ((*submenu)->next == NULL))
        return;

    item_list_t *support = NULL;
    if (mode >= 0 && mode < MODE_COUNT) {
        opl_io_module_t *mod = oplGetModule(mode);
        if (mod != NULL)
            support = mod->support;
    }

    // Already in order (a rebuild of an unchanged, sorted source): one pass and done.
    for (p = *submenu; p->next != NULL; p = p->next)
        if (submenuSortCompare(p, p->next, support) > 0)
            break;
    if (p->next == NULL)
        return;

    // Merge runs of insize, doubling each pass, until one pass makes a single merge. Taking from the
    // left run on a tie is what keeps the sort stable. prev links are rebuilt as each row is placed.
    list = *submenu;
    for (insize = 1;; insize *= 2) {
        p = list;
        list = NULL;
        tail = NULL;
        nmerges = 0;

        while (p != NULL) {
            nmerges++;
            q = p;
            psize = 0;
            for (i = 0; i < insize && q != NULL; i++) {
                psize++;
                q = q->next;
            }
            qsize = insize;

            while (psize > 0 || (qsize > 0 && q != NULL)) {
                if (psize == 0) {
                    e = q;
                    q = q->next;
                    qsize--;
                } else if (qsize == 0 || q == NULL || submenuSortCompare(p, q, support) <= 0) {
                    e = p;
                    p = p->next;
                    psize--;
                } else {
                    e = q;
                    q = q->next;
                    qsize--;
                }

                if (tail != NULL)
                    tail->next = e;
                else
                    list = e;
                e->prev = tail;
                tail = e;
            }

            p = q;
        }

        tail->next = NULL;
        if (nmerges <= 1)
            break;
    }

    *submenu = list;
}

// Folder browsing: return the device page we are leaving to its folder root, so a device is never
// parked inside a subfolder while off-screen. This keeps folder navigation a per-visit affair and
// guarantees the Favourites tab / last-played always resolve against a device's root list. It also
// frees the single shared breadcrumb buffer for the next device by restoring the device-name title.
static void menuFolderResetLeaving(struct menu_list *leaving)
{
    if (leaving == NULL || leaving->item == NULL || leaving->item->userdata == NULL)
        return;
    item_list_t *support = (item_list_t *)leaving->item->userdata;
    if (!folderModeSupported(support->mode) || folderDepth(support->mode) == 0)
        return;
    folderReset(support->mode);
    leaving->item->text = NULL;
    leaving->item->text_id = gsOf(support)->textId(support); // restore the device name (was the breadcrumb)
    // Queue the rebuild now (folderReset marked the mode dirty) so the device is back at its root list
    // promptly -- the Favourites tab / last-played resolve against that root, not the subfolder we left.
    ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
}

static void menuNextH()
{
    int moved = 0;
    {
        // Match the async config worker's snapshot/publish lock, including traversal.
        raii::SemaLock lock(menuSemaId);
        struct menu_list *next = selected_item != NULL ? selected_item->next : NULL;
        while (next != NULL && !menuItemShown(next->item))
            next = next->next;

        // If we found a valid menu transition to it.
        if (next != NULL) {
            menuFolderResetLeaving(selected_item);
            selected_item = next;
            itemConfigId = -1;
            itemConfigGeneration++;
            moved = 1;
        }
    }
    if (moved)
        sfxPlay(SFX_CURSOR); // audio RPCs must not hold up config publication
}

static void menuPrevH()
{
    int moved = 0;
    {
        // Match the async config worker's snapshot/publish lock, including traversal.
        raii::SemaLock lock(menuSemaId);
        struct menu_list *prev = selected_item != NULL ? selected_item->prev : NULL;
        while (prev != NULL && !menuItemShown(prev->item))
            prev = prev->prev;

        if (prev != NULL) {
            menuFolderResetLeaving(selected_item);
            selected_item = prev;
            itemConfigId = -1;
            itemConfigGeneration++;
            moved = 1;
        }
    }
    if (moved)
        sfxPlay(SFX_CURSOR); // audio RPCs must not hold up config publication
}

// Random Game (RIPPS2 build 115, akilluminati47): put the cursor on a random game of the page the
// user paused on. It never launches; X does that, after the user has seen what came up.
static int menuRandomGame(void)
{
    static unsigned int seed;
    submenu_list_t *cur;
    int count = 0;

    if (selected_item == NULL || selected_item->item == NULL || !selected_item->item->visible)
        return 0;
    for (cur = selected_item->item->submenu; cur != NULL; cur = cur->next)
        if (!cur->item.isFolder)
            count++;
    if (count == 0)
        return 0;

    // xorshift, stirred with the clock on every pick so each boot gives a different run.
    seed ^= (unsigned int)clock() * 2654435761u + 1;
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    int pick = (int)(seed % (unsigned int)count);
    for (cur = selected_item->item->submenu; cur != NULL; cur = cur->next)
        if (!cur->item.isFolder && pick-- == 0)
            break;
    selected_item->item->current = cur;
    selected_item->item->pagestart = cur;
    return 1;
}

static void menuFirstPage()
{
    submenu_list_t *cur = selected_item->item->current;
    if (cur) {
        if (cur->prev) {
            sfxPlay(SFX_CURSOR);
        }

        selected_item->item->current = selected_item->item->submenu;
        selected_item->item->pagestart = selected_item->item->current;
    }
}

static void menuLastPage()
{
    submenu_list_t *cur = selected_item->item->current;
    if (cur) {
        if (cur->next) {
            sfxPlay(SFX_CURSOR);
        }
        while (cur->next)
            cur = cur->next; // go to end

        selected_item->item->current = cur;

        int itms = ((items_list_t *)gTheme->itemsList->extended)->displayedItems;
        while (--itms && cur->prev) // and move back to have a full page
            cur = cur->prev;

        selected_item->item->pagestart = cur;
    }
}

static void menuNextV()
{
    submenu_list_t *cur = selected_item->item->current;

    if (cur && cur->next) {
        selected_item->item->current = cur->next;
        sfxPlay(SFX_CURSOR);
        // coverflow slide animation; the wrap branch below stays instant
        if (gTheme->coverflow)
            thmTriggerCoverflowAnim(1);

        // if the current item is beyond the page start, move the page start one page down
        cur = selected_item->item->pagestart;
        int itms = ((items_list_t *)gTheme->itemsList->extended)->displayedItems + 1;
        while (--itms && cur)
            if (selected_item->item->current == cur)
                return;
            else
                cur = cur->next;

        selected_item->item->pagestart = selected_item->item->current;
    } else { // wrap to start
        menuFirstPage();
        /*
          Animate the wrap too, but ONLY in coverflow (#271).

          The carousel's visible window already wraps -- drawCoverFlow fans covers[] out through
          menu->item->last / ->submenu -- so last->first is a single VISUAL step there, exactly like
          every other move. Leaving it instant (10c19f1b's documented intent) therefore singled out
          the two entries at the seam: stepping onto the first game, and the step immediately after
          it, got no slide and no scale transfer while every other step did. That is what the
          reporter sees as the selection "clipping/skipping on the first game and the one following
          it", and it is also the most likely source of "the 3D effect drops to a flat 2D scroll" --
          because at the seam it literally does.

          The flat list views keep the instant jump: there the wrap really is an N-item page jump,
          not one step, so a one-step slide would misrepresent it.
        */
        if (gTheme->coverflow)
            thmTriggerCoverflowAnim(1);
    }
}

static void menuPrevV()
{
    submenu_list_t *cur = selected_item->item->current;

    if (cur && cur->prev) {
        selected_item->item->current = cur->prev;
        sfxPlay(SFX_CURSOR);

        // if the current item is on the page start, move the page start one page up
        if (selected_item->item->pagestart == cur) {
            int itms = ((items_list_t *)gTheme->itemsList->extended)->displayedItems + 1; // +1 because the selection will move as well
            while (--itms && selected_item->item->pagestart->prev)
                selected_item->item->pagestart = selected_item->item->pagestart->prev;
        }

        // coverflow slide animation; the wrap branch below stays instant
        if (gTheme->coverflow)
            thmTriggerCoverflowAnim(-1);
    } else { // wrap to end
        menuLastPage();
        // Mirror of the wrap in menuNextV -- see the rationale there (#271).
        if (gTheme->coverflow)
            thmTriggerCoverflowAnim(-1);
    }
}

static void menuNextPage()
{
    submenu_list_t *previous = selected_item->item->current;
    submenu_list_t *cur = selected_item->item->pagestart;
    int displayed = ((items_list_t *)gTheme->itemsList->extended)->displayedItems;

    // Probe to the item one row past the bottom of the current page. If the end comes first, the
    // rest of the list is already on screen, so never page onto it: that over-scroll collapsed a
    // sub-page list to just its last item (#48).
    submenu_list_t *probe = cur;
    int n = displayed;
    while (n-- && probe)
        probe = probe->next;

    if (cur && probe) {
        int itms = displayed + 1;
        sfxPlay(SFX_CURSOR);

        while (--itms && cur->next)
            cur = cur->next;

        selected_item->item->current = cur;
        selected_item->item->pagestart = selected_item->item->current;
    } else { // wrap to start
        // Mirror of L1, which wraps from the first page to the last (menuPrevPage). Without this R1
        // stopped dead at the end of the list while L1 kept cycling. Coverflow shows that at its
        // worst: its one-row items list makes a page a single title, so holding R1 walked to the
        // last game and stayed there (#729).
        menuFirstPage();
    }
    // Page keys use the carousel animation only when the selection changes.
    if (gTheme->coverflow && previous != selected_item->item->current)
        thmTriggerCoverflowAnim(1);
}

static void menuPrevPage()
{
    submenu_list_t *previous = selected_item->item->current;
    submenu_list_t *cur = selected_item->item->pagestart;

    if (cur && cur->prev) {
        int itms = ((items_list_t *)gTheme->itemsList->extended)->displayedItems + 1;
        sfxPlay(SFX_CURSOR);

        while (--itms && cur->prev)
            cur = cur->prev;

        selected_item->item->current = cur;
        selected_item->item->pagestart = selected_item->item->current;
    } else { // wrap to end
        menuLastPage();
    }
    // Page keys use the carousel animation only when the selection changes.
    if (gTheme->coverflow && previous != selected_item->item->current)
        thmTriggerCoverflowAnim(-1);
}

void menuSetSelectedItem(menu_item_t *item)
{
    menu_list_t *itm = menu;

    while (itm) {
        if (itm->item == item) {
            selected_item = itm;
            return;
        }

        itm = itm->next;
    }
}

void menuRenderMenu()
{
    // Optional themed settings background (use_settings_bg): guiDrawBGSettings draws it and
    // returns non-zero; 0 means the theme has none, so fall back to the plasma. dia.c already
    // did this for dialogs -- these three menu screens did not, so a theme that shipped a
    // settings background got it on its dialogs and the plasma on its menus.
    if (guiDrawBGSettings() == 0)
        guiDrawBGPlasma();

    if (!mainMenu)
        return;

    // draw the animated menu
    if (!mainMenuCurrent)
        mainMenuCurrent = mainMenu;

    submenu_list_t *it = mainMenu;

    // calculate the number of items
    int count = 0;
    int sitem = 0;
    for (; it; count++, it = it->next) {
        if (it == mainMenuCurrent)
            sitem = count;
    }

    int spacing = 25;
    int y = (gTheme->usedHeight >> 1) - (spacing * (count >> 1));
    int cp = 0; // current position
    for (it = mainMenu; it; it = it->next, cp++) {
        // render, advance
        fntRenderString(gTheme->fonts[0], 320, y, ALIGN_CENTER, 0, 0, submenuItemGetText(&it->item), (cp == sitem) ? gTheme->selTextColor : gTheme->textColor);
        y += spacing;
        // Group the live service/settings actions above the informational/exit actions.
        // Key this to the actual row ID, not enum arithmetic, so retired/reserved IDs cannot move the gap.
        if (it->item.id == MENU_NBD)
            y += spacing / 2;
    }

    // hints
    guiDrawSubMenuHints();
}

int menuSetParentalLockCheckState(int enabled)
{
    int wasEnabled;

    wasEnabled = parentalLockCheckEnabled;
    parentalLockCheckEnabled = enabled ? 1 : 0;

    return wasEnabled;
}

// Returns the configured parental lock password, or NULL if the parental lock is not set up.
static const char *menuGetParentalLockPassword(void)
{
    const char *parentalLockPassword;
    config_set_t *configOPL = configGetByType(CONFIG_OPL);

    if (configGetStr(configOPL, CONFIG_OPL_PARENTAL_LOCK_PWD, &parentalLockPassword) && (parentalLockPassword[0] != '\0'))
        return parentalLockPassword;

    return NULL;
}

int menuIsParentalLockPasswordSet(void)
{
    return menuGetParentalLockPassword() != NULL;
}

// Prompts for the parental lock password. Returns 0 if access was granted, EACCES otherwise.
static int menuPromptParentalLockPassword(const char *parentalLockPassword)
{
    char password[CONFIG_KEY_VALUE_LEN];
    int result;

    password[0] = '\0';
    if (diaShowKeyb(password, CONFIG_KEY_VALUE_LEN, 1, _l(_STR_PARENLOCK_ENTER_PASSWORD_TITLE))) {
        if (strncmp(parentalLockPassword, password, CONFIG_KEY_VALUE_LEN) == 0) {
            result = 0;
        } else if (strncmp(OPL_PARENTAL_LOCK_MASTER_PASS, password, CONFIG_KEY_VALUE_LEN) == 0) {
            guiMsgBox(_l(_STR_PARENLOCK_DISABLE_WARNING), 0, NULL);

            configRemoveKey(configGetByType(CONFIG_OPL), CONFIG_OPL_PARENTAL_LOCK_PWD);
            saveConfig(CONFIG_OPL, 1);

            result = 0;
        } else {
            guiMsgBox(_l(_STR_PARENLOCK_PASSWORD_INCORRECT), 0, NULL);
            result = EACCES;
        }
    } else // User aborted.
        result = EACCES;

    return result;
}

int menuCheckParentalLock(void)
{
    const char *parentalLockPassword;
    int result;

    result = 0; // Default to unlocked.
    if (parentalLockCheckEnabled) {
        // Prompt for password, only if one was set.
        parentalLockPassword = menuGetParentalLockPassword();
        if (parentalLockPassword != NULL) {
            result = menuPromptParentalLockPassword(parentalLockPassword);
            if (result == 0)
                parentalLockCheckEnabled = 0; // Stop asking for the password.
        }
    }

    return result;
}

int menuGameRequiresParentalLock(config_set_t *configSet)
{
    int locked = 0;
    if (configSet != NULL)
        configGetInt(configSet, CONFIG_ITEM_PARENTAL_LOCK, &locked);
    return locked != 0 && menuIsParentalLockPasswordSet();
}

int menuCheckGameParentalLock(config_set_t *configSet)
{
    const char *parentalLockPassword;
    int locked = 0;

    if (configSet != NULL)
        configGetInt(configSet, CONFIG_ITEM_PARENTAL_LOCK, &locked);
    if (!locked)
        return 0;

    parentalLockPassword = menuGetParentalLockPassword();
    if (parentalLockPassword == NULL)
        return 0; // No password configured, so there is nothing to enforce.

    // A locked game always asks for the password, even if the settings menus were unlocked earlier in this session.
    return menuPromptParentalLockPassword(parentalLockPassword);
}

void menuHandleInputMenu()
{
    if (!mainMenu)
        return;

    if (!mainMenuCurrent)
        mainMenuCurrent = mainMenu;

    if (getKey(KEY_UP)) {
        sfxPlay(SFX_CURSOR);
        if (mainMenuCurrent->prev)
            mainMenuCurrent = mainMenuCurrent->prev;
        else // rewind to the last item
            while (mainMenuCurrent->next)
                mainMenuCurrent = mainMenuCurrent->next;
    }

    if (getKey(KEY_DOWN)) {
        sfxPlay(SFX_CURSOR);
        if (mainMenuCurrent->next)
            mainMenuCurrent = mainMenuCurrent->next;
        else
            mainMenuCurrent = mainMenu;
    }

    if (getKeyOn(gSelectButton)) {
        // execute the item via looking at the id of it
        int id = mainMenuCurrent->item.id;

        sfxPlay(SFX_CONFIRM);

#ifdef RETROACHIEVEMENTS
        if (id == MENU_RA_ACHIEVEMENTS) {
            menuOpenAchievements(NULL);
        } else if (id == MENU_RA_DISC_CHECK) {
            if (discCheckSupportDeferred())
                guiShowRANotice(_l(_STR_RA_DISC_CHECKING), NULL);
            else
                guiShowRANotice(_l(_STR_RA_CHECK_RUNNING), NULL);
        } else if (id == MENU_RA_DISC_LAUNCH) {
            discLaunch(&guiRenderProbeFrame);
        } else
#endif
            if (id == MENU_LAUNCH_PS2_DISC) {
            // Pump a frame per poll: the drive can take seconds to answer with an empty tray, and
            // this runs on the GUI thread, so without it the menu simply stops dead (issue #465).
            // That same callback carries the cancel press back, and a wait the user ended on
            // purpose is not an error -- returning quietly to the menu IS the acknowledgement.
            int discResult = sysLaunchDisc(&guiRenderProbeFrame); // success never returns; <0 -> stay in OPL
            if (discResult < 0 && discResult != SYS_DISC_CANCELLED)
                guiMsgBox(_l(_STR_DISC_LAUNCH_ERR), 0, NULL);
        } else if (id == MENU_RANDOM_GAME) {
            refreshMenuPosition();
            guiLock(); // the IO worker replaces a device's list under this lock; the pick walks it twice
            int picked = menuRandomGame();
            guiUnlock();
            if (picked)
                guiSwitchScreen(GUI_SCREEN_MAIN);
            else
                guiMsgBox(_l(_STR_RANDOM_GAME_NONE), 0, NULL);
        } else if (id == MENU_SETTINGS) {
            if (menuCheckParentalLock() == 0)
                guiShowSettings();
        } else if (id == MENU_NBD) {
            if (menuCheckParentalLock() == 0)
                handleLwnbdSrv();
        } else if (id == MENU_DRIVE_STATUS) {
            guiShowDriveStatus();
        } else if (id == MENU_FILE_MANAGER) {
            if (menuCheckParentalLock() == 0) // it can delete and run anything
                guiShowFileManager();
        } else if (id == MENU_ABOUT) {
            guiShowAbout();
        } else if (id == MENU_SAVE_CHANGES) {
            menuSaveSettings();
        } else if (id == MENU_EXIT) {
            if (guiMsgBox(_l(_STR_CONFIRMATION_EXIT), 1, NULL))
                sysExecExit();
        } else if (id == MENU_POWER_OFF) {
            if (guiMsgBox(_l(_STR_CONFIRMATION_POFF), 1, NULL))
                sysPowerOff();
        }

        // so the exit press wont propagate twice
        readPads();
    }

    if (getKeyOn(KEY_START) || getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
        // Check if there is anything to show the user, at all. This MUST list every tab that can
        // hold rows, or the user gets trapped in the settings menu with no way back to the list:
        // gFAVStartMode was missing, so a Favourites-only setup could not escape at all.
        // bdmEffectiveStartMode() rather than raw gBDMStartMode: UDPBD floors the BDM start mode, so
        // a UDPBD-only rig has rows while gBDMStartMode itself is still disabled.
        int netActive = (gNetworkProtocol == NET_PROTO_SMB) ? gETHStartMode : (gNetworkProtocol == NET_PROTO_UDPFS ? gNetStartMode : 0);
        if (gAPPStartMode || gETHStartMode || netActive || bdmEffectiveStartMode() || gHDDStartMode || gFAVStartMode || gAllGames || gMMCEStartMode) {
            guiSwitchScreen(GUI_SCREEN_MAIN);
            refreshMenuPosition();
        }
    }
}

// May this list be asked for per-game config right now? Keeps automatic metadata off the active
// scroll for the two modes where fetching it is expensive: APPS reads a config per app, and MMCE
// talks to the memory-card bus that also carries the pad. master's predicate.
constexpr int MENU_APP_CONFIG_IDLE_FRAMES = 1;
constexpr int MENU_MMCE_CONFIG_IDLE_FRAMES = 3;

static int menuCanRequestItemConfig(item_list_t *list)
{
    int mode = menuConfigSourceMode(list);
    if (mode == APP_MODE)
        return guiInactiveFrames >= MENU_APP_CONFIG_IDLE_FRAMES;

    if (mode == MMCE_MODE)
        return guiInactiveFrames >= MENU_MMCE_CONFIG_IDLE_FRAMES;

    return 1;
}

static int menuSelectedRowView(item_list_t *list)
{
    if (list != NULL && selected_item != NULL && selected_item->item != NULL &&
        selected_item->item->current != NULL)
        return libListRowView(list, selected_item->item->current->item.id);
    return list != NULL ? libListViewActive(list) : LIB_VIEW_ISO;
}

static void menuRenderElements(theme_elems_t *elems)
{
    // selected_item can't be NULL here as we only allow to switch to "Main" rendering when there is at least one device activated
    theme_element_t *elem = elems->first;
    item_list_t *list = (selected_item != NULL && selected_item->item != NULL) ? static_cast<item_list_t *>(selected_item->item->userdata) : NULL;

    // ONLY IF SOMETHING ON SCREEN ACTUALLY READS IT. This was unconditional: every settled row
    // enqueued an IO_CUSTOM_SIMPLEACTION that opens CFG/<game>.cfg on the game device, whether or not
    // the theme has a single element capable of displaying it. On the stock theme that is every row's
    // worth of storage traffic bought for nothing -- and a per-game cfg usually does NOT exist, so it
    // is the expensive kind of file operation: a lookup that must walk the whole directory before it
    // can answer "no". Measured on hardware at ~3 accepted actions/sec while merely browsing, on the
    // ioman worker (priority 32), which outranks the art thread (64) and shares the one process-wide
    // fileXio RPC channel with it. That is the queue the art was waiting behind.
    //
    // elems->needsItemConfig has been set by the theme parser all along and READ BY NOTHING -- the
    // third field in this tree to ship with its data half and no code half, after cache_entry_t.key
    // and gArtShutdownAbandoned. It is exact rather than approximate: the parser sets it for
    // AttributeText, GameCountText and AttributeImage (themes.c), and those are precisely the only
    // three draw functions that consume the config argument. It is per-FAMILY, so the info screen
    // still loads what it needs the moment it is shown.
    // SaveIcon reads the row's VMC names from that config too -- but only while 3D Save Icons is on.
    if ((elems->needsItemConfig || (elems->needsSaveIconConfig && gEnableSaveIcons)) && menuCanRequestItemConfig(list))
        _menuRequestConfig();

    // Deep VCD ID inspection is cosmetic and must be explicit-theme-demand only. ItemText is
    // the sole render element that consumes vcdDisplayIdCached(), so a family without ItemText does
    // zero .VCD opens/seeks here. The resolver itself is opportunistic and yields to art/BGM.
    if (elems->needsVcdDisplayId && list != NULL && menuSelectedRowView(list) == LIB_VIEW_PS1 &&
        selected_item->item->current != NULL) {
        char *vcdName = gsOf(list)->startup(list, selected_item->item->current->item.id);
        if (vcdName != NULL)
            vcdRequestDisplayId(vcdName);
    }

    WaitSema(menuSemaId);

    while (elem) {
        if (elem->drawElem)
            elem->drawElem(selected_item, selected_item->item->current, itemConfig, elem);

        elem = elem->next;
    }
    SignalSema(menuSemaId);
}

// True when the highlighted Favourites row is an app favourite -- the same test the theme's per-row
// cover redirect uses (thmGetElemForItem), so the info page and its cover always agree on the kind.
static int menuFavSelectedIsApp(item_list_t *list, int rowView)
{
    if (rowView == LIB_VIEW_ELF || rowView == LIB_VIEW_PS1_ELF)
        return 1;
    if (list != NULL && list->mode == FAV_MODE && selected_item != NULL && selected_item->item != NULL &&
        selected_item->item->current != NULL)
        return favGetItemSourceMode(selected_item->item->current->item.id) == APP_MODE;
    return 0;
}

// Per-page element families fall back to the GAMES family when the theme defines none. Neither
// built-in theme (<OPL>, <Coverflow>) declares a single appsInfo*/favsInfo*/vcdInfo* element, and
// validateBackgroundElems only auto-adds a background to a NON-empty info group -- so dispatching
// straight to .first handed menuRenderElements a NULL list and the info screen (Square) drew
// NOTHING for apps, favourites and the PS1/VCD view. The main families are unaffected in practice
// (validateBackgroundElems seeds them a background, so .first is never NULL) but take the same
// guard so the two paths cannot drift again.
static theme_elems_t *menuGetInfoElems(item_list_t *list)
{
    // Favourites per-kind info families (opt-in, see theme_t): a PS1 or app favourite's info page uses
    // favsVcdInfo* / favsAppsInfo* when the theme declares them. Empty families fall through unchanged.
    if (list != NULL && list->mode == FAV_MODE) {
        int rowView = menuSelectedRowView(list);
        if (rowView == LIB_VIEW_PS1 && gTheme->favsVcdInfoElems.first)
            return &gTheme->favsVcdInfoElems;
        if (rowView != LIB_VIEW_PS1 && gTheme->favsAppsInfoElems.first && menuFavSelectedIsApp(list, rowView))
            return &gTheme->favsAppsInfoElems;
    }
    if (list != NULL && menuSelectedRowView(list) == LIB_VIEW_PS1)
        return gTheme->vcdInfoElems.first ? &gTheme->vcdInfoElems : &gTheme->infoElems;
    if (list != NULL && list->mode == FAV_MODE)
        return gTheme->favsInfoElems.first ? &gTheme->favsInfoElems : &gTheme->infoElems;
    if (list != NULL && list->mode == APP_MODE)
        return gTheme->appsInfoElems.first ? &gTheme->appsInfoElems : &gTheme->infoElems;
    return &gTheme->infoElems;
}

static theme_elems_t *menuGetMainElems(item_list_t *list)
{
    // Favourites per-kind main families (opt-in): the Favourites PS1 view draws favsVcdMain* and the ELF
    // view favsAppsMain* when the theme declares them. The All and PS2 views keep favsMain*.
    if (list != NULL && list->mode == FAV_MODE) {
        int view = libViewActive(FAV_MODE);
        if (view == LIB_VIEW_PS1 && gTheme->favsVcdMainElems.first)
            return &gTheme->favsVcdMainElems;
        if (view == LIB_VIEW_ELF && gTheme->favsAppsMainElems.first)
            return &gTheme->favsAppsMainElems;
    }
    if (list != NULL && (libViewActive(list->mode) == LIB_VIEW_PS1))
        return gTheme->vcdMainElems.first ? &gTheme->vcdMainElems : &gTheme->mainElems;
    if (list != NULL && list->mode == FAV_MODE)
        return gTheme->favsMainElems.first ? &gTheme->favsMainElems : &gTheme->mainElems;
    if (list != NULL && list->mode == APP_MODE)
        return gTheme->appsMainElems.first ? &gTheme->appsMainElems : &gTheme->mainElems;
    return &gTheme->mainElems;
}

void menuRenderMain(void)
{
    item_list_t *list = static_cast<item_list_t *>(selected_item->item->userdata);
    theme_elems_t *kindElems = menuGetMainElems(list);

    if (kindElems == &gTheme->favsVcdMainElems || kindElems == &gTheme->favsAppsMainElems) {
        // A Favourites per-kind family page. Its lists own no global slot, so navigation takes the list
        // the theme declared in the family (the one drawn), falling back to the slot this view used
        // before the family existed: the VCD list for the PS1 view, the favourites list for the ELF view.
        theme_element_t *slot = (kindElems == &gTheme->favsVcdMainElems) ? (gTheme->vcdItemsList ? gTheme->vcdItemsList : gTheme->gamesItemsList) : (gTheme->favsItemsList ? gTheme->favsItemsList : gTheme->gamesItemsList);
        menuRenderElements(kindElems);
        gTheme->itemsList = thmFamilyItemsList(kindElems, slot, selected_item->item->icon_id);
    } else if (libViewActive(list->mode) == LIB_VIEW_PS1) {
        // VCD/PS1 listings render with the vcd family (vcdMain*; each slot falls back at parse time to
        // appsMain* then main*). The VCD list uses its OWN items-list slot (vcdItemsList) so it keeps a
        // SEPARATE cover cache from the device's ISO list -- the view reuses the device's game list
        // (same item ids), so a shared cache thrashes on every L3 toggle. This also covers the
        // Favourites tab's own VCD view: VCD favourites render with the PS1 family, not the favs one.
        menuRenderElements(menuGetMainElems(list));
        gTheme->itemsList = thmResolveItemsList(&gTheme->vcdMainElems, gTheme->vcdItemsList ? gTheme->vcdItemsList : gTheme->gamesItemsList, selected_item->item->icon_id);
    } else if (list->mode == FAV_MODE) {
        menuRenderElements(menuGetMainElems(list));
        gTheme->itemsList = thmResolveItemsList(&gTheme->favsMainElems, gTheme->favsItemsList ? gTheme->favsItemsList : gTheme->gamesItemsList, selected_item->item->icon_id);
    } else if (list->mode == APP_MODE) {
        menuRenderElements(menuGetMainElems(list));
        gTheme->itemsList = thmResolveItemsList(&gTheme->appsMainElems, gTheme->appsItemsList ? gTheme->appsItemsList : gTheme->gamesItemsList, selected_item->item->icon_id);
    } else {
        menuRenderElements(menuGetMainElems(list));
        // Always falls back to gamesItemsList, never NULL: the scroll/paging code (menuNextV/PrevV/
        // Page) derefs gTheme->itemsList->extended with no NULL check.
        gTheme->itemsList = thmResolveItemsList(&gTheme->mainElems, gTheme->gamesItemsList, selected_item->item->icon_id);
    }
}

// Coverflow rotates the nav axis on the MAIN screen only: Left/Right step through the
// carousel (a vertical list move) while Up/Down switch device menus. Non-coverflow themes
// behave exactly as before. The info screen is intentionally NOT rotated (menuHandleInputInfo).
// The Grid view: Left/Right step one cover, Up/Down one row of covers. With the device sidebar (the
// default) the cursor stops at the top and bottom rows and Left from the first column opens the sidebar;
// a theme without it (<block>_devices=0) leaves for the previous/next device menu from the top/bottom row.
static void menuGridRow(int dir)
{
    submenu_list_t *cur = selected_item->item->current, *walk;
    int count = 0, index = 0;
    for (walk = selected_item->item->submenu; walk != NULL; walk = walk->next) {
        if (walk == cur)
            index = count;
        count++;
    }
    int target = cur != NULL ? gridRowStep(count, index, gTheme->gridColumns, dir) : -1;
    if (target < 0) {
        if (gTheme->gridDevices)
            return; // the top and bottom rows are the list's ends; devices change in the sidebar (RIPPS2 build 115)
        if (dir < 0)
            menuPrevH();
        else
            menuNextH();
        return;
    }
    for (walk = selected_item->item->submenu; target > 0 && walk != NULL; target--)
        walk = walk->next;
    if (walk == NULL)
        return;
    selected_item->item->current = walk;
    selected_item->item->pagestart = walk; // the grid's items list shows the selected title
    sfxPlay(SFX_CURSOR);
}

// Whether the page on screen draws a Grid. A theme can mix views per family (a Coverflow games page and
// a Grid apps page), so this follows the family the page draws, not the theme-wide gTheme->grid.
static int menuPageIsGrid(void)
{
    if (gTheme->grid == NULL || selected_item == NULL || selected_item->item == NULL)
        return 0;
    return menuGetMainElems((item_list_t *)selected_item->item->userdata)->hasGrid;
}

// L1/R1 on a Grid page turn a whole page of covers, the cursor keeping its place on the page (RIPPS2 build
// 98, akilluminati47). Through the list's own paging they moved one cover: the grid's items list is one row
// high. Past either end they wrap, as L1/R1 do in the list; on a single page they do nothing.
static void menuGridPage(int dir)
{
    submenu_list_t *cur = selected_item->item->current, *walk;
    int count = 0, index = 0;
    for (walk = selected_item->item->submenu; walk != NULL; walk = walk->next) {
        if (walk == cur)
            index = count;
        count++;
    }
    int target = cur != NULL ? gridPageStep(count, index, gTheme->gridRows, gTheme->gridColumns, dir) : -1;
    if (target < 0) {
        if (count > gTheme->gridRows * gTheme->gridColumns) { // a single page has nowhere to wrap to
            if (dir < 0)
                menuLastPage();
            else
                menuFirstPage();
        }
        return;
    }
    for (walk = selected_item->item->submenu; target > 0 && walk != NULL; target--)
        walk = walk->next;
    if (walk == NULL)
        return;
    selected_item->item->current = walk;
    selected_item->item->pagestart = walk; // the grid's items list shows the selected title
    sfxPlay(SFX_CURSOR);
}

// The Grid's device sidebar (RIPPS2 build 113's category sidebar, akilluminati47): while it has the pad,
// Up/Down switch device page and the grid shows that device at once; Right, Cross or Circle go back to the
// covers. Drawn by grid.cpp.
int menuGridSidebarFocused(void)
{
    return gGridSidebarOpen && gTheme != NULL && gTheme->gridDevices && menuPageIsGrid();
}

static int menuGridAtRowStart(void)
{
    submenu_list_t *cur = selected_item->item->current, *walk;
    int count = 0, index = 0;
    for (walk = selected_item->item->submenu; walk != NULL; walk = walk->next) {
        if (walk == cur)
            index = count;
        count++;
    }
    return gridAtRowStart(count, index, gTheme->gridColumns);
}

static void menuGridSidebarInput(void)
{
    if (getKey(KEY_UP)) {
        menuPrevH();
    } else if (getKey(KEY_DOWN)) {
        menuNextH();
    } else if (getKeyOn(KEY_RIGHT) || getKeyOn(KEY_CROSS) || getKeyOn(KEY_CIRCLE)) {
        gGridSidebarOpen = 0;
        sfxPlay(SFX_CURSOR);
    } else if (getKeyOn(KEY_START)) {
        gGridSidebarOpen = 0;
        menuInitMainMenu();
        guiSwitchScreen(GUI_SCREEN_MENU);
    }
    if (!menuPageIsGrid())
        gGridSidebarOpen = 0; // a device page another view draws has no sidebar
}

static void menuNavigateLeft()
{
    if (menuPageIsGrid() && gTheme->gridDevices && menuGridAtRowStart()) {
        if (getKeyOn(KEY_LEFT)) { // a fresh press, so a held Left stops at the first column
            gGridSidebarOpen = 1;
            sfxPlay(SFX_CURSOR);
        }
        return;
    }
    if (gTheme->coverflow || menuPageIsGrid())
        menuPrevV();
    else
        menuPrevH();
}

static void menuNavigateRight()
{
    if (gTheme->coverflow || menuPageIsGrid())
        menuNextV();
    else
        menuNextH();
}

static void menuNavigateUp()
{
    if (menuPageIsGrid())
        menuGridRow(-1);
    else if (gTheme->coverflow)
        menuPrevH();
    else
        menuPrevV();
}

static void menuNavigateDown()
{
    if (menuPageIsGrid())
        menuGridRow(1);
    else if (gTheme->coverflow)
        menuNextH();
    else
        menuNextV();
}

// L3 on the main page: a tap advances the page's library-view ring, as before. When the theme offers
// more than one view (multi-view themes, docs/MULTI-VIEW-THEMES-PLAN.md), holding L3 switches to the
// next view instead, so the tap acts on release.
#define MENU_VIEW_HOLD_MS 500
static clock_t menuL3Down;
static int menuL3State; // 0 idle, 1 held (tap so far), 2 hold already handled

static int menuL3HoldReached(void)
{
    return (uint32_t)clock() - (uint32_t)menuL3Down >= (uint32_t)MENU_VIEW_HOLD_MS * (CLOCKS_PER_SEC / 1000);
}

static void menuL3Tap(int viewPending)
{
    if (!viewPending && selected_item->item->toggleView)
        selected_item->item->toggleView(selected_item->item);
}

static void menuHandleViewHold(int viewPending)
{
    if (menuL3State == 0)
        return;
    if (!getKeyPressed(KEY_L3)) {
        // A release after the hold time (say, on another screen) is not a tap.
        if (menuL3State == 1 && !menuL3HoldReached())
            menuL3Tap(viewPending);
        menuL3State = 0;
    } else if (menuL3State == 1 && menuL3HoldReached()) {
        menuL3State = 2;
        if (!viewPending)
            thmCycleView();
    }
}

void menuHandleInputMain()
{
    // A device tab steps aside once All Games holds its games; never leave the user on it. Turning
    // All Games off can hide the page the user is on too: the Favourites page, when its own start
    // mode is Off. A hidden Favourites page never comes back by itself (only Settings shows it), so
    // leave it as well. Device tabs are left alone with All Games off: a BDM page hides for a poll
    // or two during a settings apply or a stall and comes back, and moving the user then is #806.
    if (selected_item != NULL && !menuItemShown(selected_item->item) && (gAllGames || menuIsFavPage(selected_item)))
        refreshMenuPosition();
    item_list_t *support = selected_item != NULL && selected_item->item != NULL ? static_cast<item_list_t *>(selected_item->item->userdata) : NULL;
    int viewPending = support != NULL && libViewPending(support->mode);

    if (menuGridSidebarFocused()) {
        // R3 still marks the selected game (read before the sidebar can move to another device this frame)
        if (getKeyOn(KEY_R3) && !viewPending && selected_item->item->fav)
            selected_item->item->fav(selected_item->item);
        menuGridSidebarInput();
        return;
    }
    gGridSidebarOpen = 0;

    if (getKey(KEY_LEFT)) {
        menuNavigateLeft();
    } else if (getKey(KEY_RIGHT)) {
        menuNavigateRight();
    } else if (getKey(KEY_UP)) {
        menuNavigateUp();
    } else if (getKey(KEY_DOWN)) {
        menuNavigateDown();
    } else if (getKeyOn(KEY_CROSS)) {
        if (!viewPending)
            selected_item->item->execCross(selected_item->item);
    } else if (getKeyOn(KEY_TRIANGLE)) {
        if (!viewPending)
            selected_item->item->execTriangle(selected_item->item);
    } else if (getKeyOn(KEY_CIRCLE)) {
        if (!viewPending)
            selected_item->item->execCircle(selected_item->item);
    } else if (getKeyOn(KEY_SQUARE)) {
        if (!viewPending)
            selected_item->item->execSquare(selected_item->item);
    } else if (getKeyOn(KEY_START)) {
        // reinit main menu - show/hide items valid in the active context
        menuInitMainMenu();
        guiSwitchScreen(GUI_SCREEN_MENU);
    } else if (getKeyOn(KEY_SELECT)) {
        if (!viewPending)
            selected_item->item->refresh(selected_item->item);
    } else if (getKey(KEY_L1)) {
        if (menuPageIsGrid())
            menuGridPage(-1);
        else
            menuPrevPage();
    } else if (getKey(KEY_R1)) {
        if (menuPageIsGrid())
            menuGridPage(1);
        else
            menuNextPage();
    } else if (getKeyOn(KEY_L2)) { // home
        menuFirstPage();
    } else if (getKeyOn(KEY_R2)) { // end
        menuLastPage();
    } else if (getKeyOn(KEY_R3)) { // toggle favourite
        if (!viewPending && selected_item->item->fav)
            selected_item->item->fav(selected_item->item);
    } else if (getKeyOn(KEY_L3)) { // advance the current page's library-view ring; hold = next theme view
        if (gTheme->viewMask & (gTheme->viewMask - 1)) {
            menuL3Down = clock();
            menuL3State = 1;
        } else
            menuL3Tap(viewPending);
    }
    menuHandleViewHold(viewPending);

    // Last Played Auto Start
    if (gRememberLastPlayed && gAutoStartLastPlayed > 0 && RemainSecs < 0 && DisableCron == 0 && !viewPending) {
        DisableCron = 1; // Disable Counter
        if (gSelectButton == KEY_CIRCLE)
            selected_item->item->execCircle(selected_item->item);
        else
            selected_item->item->execCross(selected_item->item);
    }
}

void menuRenderInfo(void)
{
    item_list_t *list = static_cast<item_list_t *>(selected_item->item->userdata);
    theme_elems_t *kindElems = menuGetInfoElems(list);

    if (kindElems == &gTheme->favsVcdInfoElems || kindElems == &gTheme->favsAppsInfoElems) {
        // Favourites per-kind info page: same slot-free list resolution as menuRenderMain.
        theme_element_t *slot = (kindElems == &gTheme->favsVcdInfoElems) ? (gTheme->vcdItemsList ? gTheme->vcdItemsList : gTheme->gamesItemsList) : (gTheme->favsItemsList ? gTheme->favsItemsList : gTheme->gamesItemsList);
        menuRenderElements(kindElems);
        gTheme->itemsList = thmFamilyItemsList(kindElems, slot, selected_item->item->icon_id);
    } else if (menuSelectedRowView(list) == LIB_VIEW_PS1) {
        menuRenderElements(menuGetInfoElems(list));
        gTheme->itemsList = thmResolveItemsList(&gTheme->vcdInfoElems, gTheme->vcdItemsList ? gTheme->vcdItemsList : gTheme->gamesItemsList, selected_item->item->icon_id);
    } else if (list->mode == FAV_MODE) {
        menuRenderElements(menuGetInfoElems(list));
        gTheme->itemsList = thmResolveItemsList(&gTheme->favsInfoElems, gTheme->favsItemsList ? gTheme->favsItemsList : gTheme->gamesItemsList, selected_item->item->icon_id);
    } else if (list->mode == APP_MODE) {
        menuRenderElements(menuGetInfoElems(list));
        gTheme->itemsList = thmResolveItemsList(&gTheme->appsInfoElems, gTheme->appsItemsList ? gTheme->appsItemsList : gTheme->gamesItemsList, selected_item->item->icon_id);
    } else {
        menuRenderElements(menuGetInfoElems(list));
        gTheme->itemsList = thmResolveItemsList(&gTheme->infoElems, gTheme->gamesItemsList, selected_item->item->icon_id);
    }
}

void menuHandleInputInfo()
{
    if (getKeyOn(KEY_CROSS)) {
        if (gSelectButton == KEY_CIRCLE)
            guiSwitchScreen(GUI_SCREEN_MAIN);
        else
            selected_item->item->execCross(selected_item->item);
    } else if (getKey(KEY_UP)) {
        menuPrevV();
        menuRequestInfoSize(); // each row reached from the info page needs its own #Size
    } else if (getKey(KEY_DOWN)) {
        menuNextV();
        menuRequestInfoSize(); // each row reached from the info page needs its own #Size
    } else if (getKeyOn(KEY_CIRCLE)) {
        if (gSelectButton == KEY_CROSS)
            guiSwitchScreen(GUI_SCREEN_MAIN);
        else
            selected_item->item->execCircle(selected_item->item);
    } else if (getKey(KEY_L1)) {
        menuPrevPage();
        menuRequestInfoSize(); // each row reached from the info page needs its own #Size
    } else if (getKey(KEY_R1)) {
        menuNextPage();
        menuRequestInfoSize(); // each row reached from the info page needs its own #Size
    } else if (getKeyOn(KEY_L2)) {
        menuFirstPage();
        menuRequestInfoSize(); // each row reached from the info page needs its own #Size
    } else if (getKeyOn(KEY_R2)) {
        menuLastPage();
        menuRequestInfoSize(); // each row reached from the info page needs its own #Size
    }
}

void menuRenderGameMenu()
{
    // Optional themed settings background (use_settings_bg): guiDrawBGSettings draws it and
    // returns non-zero; 0 means the theme has none, so fall back to the plasma. dia.c already
    // did this for dialogs -- these three menu screens did not, so a theme that shipped a
    // settings background got it on its dialogs and the plasma on its menus.
    if (guiDrawBGSettings() == 0)
        guiDrawBGPlasma();

    if (!gameMenu)
        return;

    // If the device menu that has the selected game suddenly goes invisible (device was removed), switch
    // back to the game list menu.
    if (!menuItemShown(selected_item->item)) {
        guiSwitchScreen(GUI_SCREEN_MAIN);
        return;
    }

    // If we enter the game settings menu and there's no selected item bail out. I'm not entirely sure how we get into
    // this state but it seems to happen on some consoles when transitioning from the game settings menu back to the game
    // list menu.
    if (selected_item->item->current == NULL)
        return;

    // draw the animated menu
    if (!gameMenuCurrent)
        gameMenuCurrent = gameMenu;

    submenu_list_t *it = gameMenu;

    // calculate the number of items
    int count = 0;
    int sitem = 0;
    for (; it; count++, it = it->next) {
        if (it == gameMenuCurrent)
            sitem = count;
    }

    int spacing = 25;
    int y = (gTheme->usedHeight >> 1) - (spacing * (count >> 1));
    int cp = 0; // current position

    // game title
    fntRenderString(gTheme->fonts[0], 320, 20, ALIGN_CENTER, 0, 0, cfgTitleDisplayName((item_list_t *)selected_item->item->userdata, selected_item->item->current->item.id, submenuItemGetText(&selected_item->item->current->item)), gTheme->selTextColor);

    // config source
    char *cfgSource = gameConfigSource();
    fntRenderString(gTheme->fonts[0], 320, 40, ALIGN_CENTER, 0, 0, cfgSource, gTheme->textColor);

    // settings list
    for (it = gameMenu; it; it = it->next, cp++) {
        // render, advance
        fntRenderString(gTheme->fonts[0], 320, y, ALIGN_CENTER, 0, 0, submenuItemGetText(&it->item), (cp == sitem) ? gTheme->selTextColor : gTheme->textColor);
        y += spacing;
        if (cp == (GAME_SAVE_CHANGES - 1) || cp == (GAME_REMOVE_CHANGES - 1))
            y += spacing / 2;
    }

    // hints
    guiDrawSubMenuHints();
}

// Is the effective launch core for the selected game Neutrino? Per-game Cheats/GSM/PADEMU/OSD are
// OPL-core features, so under Neutrino they must say so rather than opening a panel that cannot
// take effect (checklist item 22's guidance half).
static int gameMenuCoreIsNeutrino(void)
{
    int coreLoader = gDefaultCoreLoader; // no per-game $CoreLoader key -> follow the global default core
    if (itemConfig != NULL)
        configGetInt(itemConfig, CONFIG_ITEM_CORE_LOADER, &coreLoader);
    // VCD (PS1) games launch ONLY via POPSTARTER -- never Neutrino -- so a keyless VCD must not
    // inherit a Neutrino global default here, or its Cheats/GSM/OSD menu entries get blocked with
    // the wrong "not available under Neutrino" message. SMB takes Neutrino only from the game's own
    // key (ethTryNeutrino): "Default" is the <OPL> core there, so a Neutrino global default must not
    // block its panels, while a game set to Neutrino gets the Neutrino answers.
    // HTTP and CF are native-only too; UDPFS (Files and IMG) and UDPBD are Neutrino unless the game's own
    // key says <OPL>.
    // Favorites follow the device that owns the row.
    if (selected_item != NULL && selected_item->item != NULL) {
        item_list_t *support = (item_list_t *)selected_item->item->userdata;
        if (support != NULL) {
            int sourceMode = menuConfigSourceMode(support);
            if (menuSelectedRowView(support) == LIB_VIEW_PS1 || sourceMode == HTTP_MODE || bdmModeIsPCCF(sourceMode))
                return 0;
            if (sourceMode == ETH_MODE) {
                int key = 2;
                if (itemConfig != NULL)
                    configGetInt(itemConfig, CONFIG_ITEM_CORE_LOADER, &key);
                return key == 1;
            }
            if (sourceMode == UDPFS_MODE || bdmModeIsUDPBD(sourceMode))
                return bdmNetBlockCoreLoader(itemConfig);
        }
    }
    return coreLoader;
}

#ifdef RETROACHIEVEMENTS
static void menuCheckSelectedGameSupport(void)
{
    /* RA: hash the selected image, on demand. Hashing every image during the scan would
       hold the console on the splash screen: the scan runs before the menu appears and
       each image has to be opened and read.
       Every refusal SAYS why -- a silent no-op reads as broken. OPL-core launches carry
       ee_core's telemetry, Neutrino and UDPFS launches the RA kit's ORBIT agent, and USB VCDs
       their own POPStarter module; Ember and the PC CARD CF device carry none. */
    item_list_t *support = static_cast<item_list_t *>(selected_item->item->userdata);
    int gid = selected_item->item->current->item.id;
    int rowView = menuSelectedRowView(support);
    item_list_t sourceView;

    // A PS2 favourite is checked on its source device, by its current id there.
    if (support->mode == FAV_MODE && rowView != LIB_VIEW_PS1) {
        support = gFavSupport.sourceItem(gid, &sourceView, &gid);
        if (support == NULL) {
            guiShowRANotice(_l(gRAMode == RA_MODE_CADUCEUS ? _STR_CAD_USE_LIBRARY : _STR_XERA_USE_LIBRARY), NULL);
            return;
        }
    }
    if (rowView == LIB_VIEW_PS1) {
        /* PS1 rows never reach ee_core. A USB VCD carries its own telemetry module into
           POPStarter (rapopslaunch.cpp); Ember rows and other devices are untracked. */
        item_list_t ownerView;
        item_list_t *owner = support->mode == FAV_MODE ? gFavSupport.ownerView(gid, &ownerView) : support;
        base_game_info_t *g = NULL;
        const char *prefix = owner != NULL && gsOf(owner)->hasPrefix() ? gsOf(owner)->prefix(owner) : NULL;
        base_game_info_t favorite;

        if (support->mode == FAV_MODE) {
            if (favGetItemKind(gid) == FAV_KIND_VCD) {
                memset(&favorite, 0, sizeof(favorite));
                snprintf(favorite.name, sizeof(favorite.name), "%s", gsOf(support)->name(support, gid));
                strcpy(favorite.extension, ".VCD");
                favorite.format = GAME_FORMAT_ISO;
                g = &favorite;
            }
        } else if (gsOf(support)->hasGame())
            g = (base_game_info_t *)gsOf(support)->game(support, gid);
        if (owner != NULL && bdmModeIsUSB(owner->mode) && g && g->name[0] && prefix && !strcasecmp(g->extension, ".VCD")) {
            if (sbHashGameDeferred(prefix, g->name, g->extension, g->startup, g->format))
                guiShowRANotice(_l(_STR_RA_CHECKING_IMAGE), NULL);
            else
                guiShowRANotice(_l(_STR_RA_CHECK_RUNNING), NULL);
        } else
            guiShowRANotice(_l(_STR_PS1_RA_USB_REQUIRED), NULL);
    } else if (bdmModeIsPCCF(support->mode)) {
        // CF owns DEV9 in game, so the RA network modules never load beside it.
        guiShowRANotice(_l(_STR_PCCF_UNSUPPORTED_GAME), NULL);
    } else if (support->mode == HDD_MODE) {
        /* An HDLoader install has no image file: hash through its partition map, read-only,
           on the I/O worker. */
        hdl_game_info_t *g = gsOf(support)->hasGame() ? (hdl_game_info_t *)gsOf(support)->game(support, gid) : NULL;
        const char *prefix = gsOf(support)->hasPrefix() ? gsOf(support)->prefix(support) : NULL;

        if (g && g->start_sector && g->startup[0] && prefix && prefix[0]) {
            if (sbHashHdlDeferred(prefix, g->name, g->startup, g->start_sector))
                guiShowRANotice(_l(_STR_RA_CHECKING_IMAGE), NULL);
            else
                guiShowRANotice(_l(_STR_RA_CHECK_RUNNING), NULL);
        } else
            guiShowRANotice(_l(_STR_RA_NA_HDD), _l(_STR_RA_NA_HDD2));
    } else if (gsOf(support)->hasGame() && gsOf(support)->hasPrefix()) {
        base_game_info_t *g = (base_game_info_t *)gsOf(support)->game(support, gid);
        const char *prefix = gsOf(support)->prefix(support);

        if (g != NULL && prefix != NULL && g->format != GAME_FORMAT_FOLDER) {
            if (sbHashGameDeferred(prefix, g->name, g->extension, g->startup, g->format))
                guiShowRANotice(_l(_STR_RA_CHECKING_IMAGE), NULL);
            else
                guiShowRANotice(_l(_STR_RA_CHECK_RUNNING), NULL);
        }
    }
}

// A USB VCD's set, from the PS1 menu (or a VCD favourite). Other PS1 rows cannot be tracked.
static void menuOpenSelectedVcdAchievements(void)
{
    item_list_t *support = static_cast<item_list_t *>(selected_item->item->userdata);
    int gid = selected_item->item->current->item.id;
    item_list_t ownerView;
    item_list_t *owner = support->mode == FAV_MODE ? gFavSupport.ownerView(gid, &ownerView) : support;
    const char *prefix = owner != NULL && gsOf(owner)->hasPrefix() ? gsOf(owner)->prefix(owner) : NULL;
    const char *name = gsOf(support)->name(support, gid);
    const char *colon = prefix ? strchr(prefix, ':') : NULL;
    char path[256];

    if (owner == NULL || !bdmModeIsUSB(owner->mode) || colon == NULL || name == NULL || !name[0] ||
        menuSelectedRowView(support) != LIB_VIEW_PS1 || (support->mode == FAV_MODE && favGetItemKind(gid) != FAV_KIND_VCD) ||
        snprintf(path, sizeof(path), "%.*s/POPS/%s.VCD", (int)(colon - prefix) + 1, prefix, name) >= (int)sizeof(path)) {
        guiShowRANotice(_l(_STR_CAD_HASH_UNSUPPORTED), NULL);
        return;
    }
    if (achievementsRequestVcd(path)) {
        snprintf(achImagePath, sizeof(achImagePath), "%s", path);
        achImageStartup[0] = '\0';
        menuOpenAchievements("image");
        achImageVcd = 1;
        achReturnScreen = GUI_SCREEN_APP_MENU;
    } else
        guiShowRANotice(_l(_STR_RA_CHECK_RUNNING), NULL);
}
#endif

void menuHandleInputGameMenu()
{
    if (!gameMenu)
        return;

    if (!gameMenuCurrent)
        gameMenuCurrent = gameMenu;

    if (getKey(KEY_UP)) {
        sfxPlay(SFX_CURSOR);
        if (gameMenuCurrent->prev)
            gameMenuCurrent = gameMenuCurrent->prev;
        else // rewind to the last item
            while (gameMenuCurrent->next)
                gameMenuCurrent = gameMenuCurrent->next;
    }

    if (getKey(KEY_DOWN)) {
        sfxPlay(SFX_CURSOR);
        if (gameMenuCurrent->next)
            gameMenuCurrent = gameMenuCurrent->next;
        else
            gameMenuCurrent = gameMenu;
    }

    if (getKeyOn(gSelectButton)) {
        // execute the item via looking at the id of it
        int menuID = gameMenuCurrent->item.id;

        sfxPlay(SFX_CONFIRM);

#ifdef RETROACHIEVEMENTS
        if (menuID == GAME_RA_CHECK) {
            menuCheckSelectedGameSupport();
        } else if (menuID == GAME_RA_ACHIEVEMENTS) {
            // A whole ISO hashes; HDLoader entries and PS1 rows have no image to read here.
            item_list_t *support = static_cast<item_list_t *>(selected_item->item->userdata);
            base_game_info_t *g = NULL;
            const char *prefix = NULL;
            if (menuSelectedRowView(support) != LIB_VIEW_PS1 && support->mode != HDD_MODE && gsOf(support)->hasGame() &&
                gsOf(support)->hasPrefix()) {
                g = (base_game_info_t *)gsOf(support)->game(support, selected_item->item->current->item.id);
                prefix = gsOf(support)->prefix(support);
            }
            if (g == NULL || prefix == NULL)
                guiShowRANotice(_l(gRAMode == RA_MODE_CADUCEUS ? _STR_CAD_USE_LIBRARY : _STR_XERA_USE_LIBRARY), NULL);
            else if (g->format != GAME_FORMAT_ISO && g->format != GAME_FORMAT_OLD_ISO)
                guiShowRANotice(_l(_STR_CAD_HASH_UNSUPPORTED), NULL);
            else {
                char path[256];
                const char *save = (support->flags & MODE_FLAG_READ_ONLY) ? "" : prefix; // no .ach write on a read-only device
                sbCreatePath(g, path, prefix, "/", 0);
                if (achievementsRequestImage(path, g->startup, save)) {
                    snprintf(achImagePath, sizeof(achImagePath), "%s", path);
                    snprintf(achImageStartup, sizeof(achImageStartup), "%s", g->startup);
                    snprintf(achImagePrefix, sizeof(achImagePrefix), "%s", save);
                    menuOpenAchievements("image");
                } else
                    guiShowRANotice(_l(_STR_RA_CHECK_RUNNING), NULL);
            }
        } else if (menuID == GAME_RA_TEST) {
            sbTestPCLinkDeferred();
            guiShowRANotice(_l(_STR_RA_LOOKING_FOR_PC), NULL);
        } else
#endif
            if (menuID == GAME_COMPAT_SETTINGS) {
            guiGameShowCompatConfig(selected_item->item->current->item.id, static_cast<item_list_t *>(selected_item->item->userdata), itemConfig);
        } else if (menuID == GAME_CHEAT_SETTINGS) {
            // Both cores run cheats now; only the PS2RD .img row is OPL-core-only.
            guiGameShowCheatConfig(gameMenuCoreIsNeutrino());
        } else if (menuID == GAME_GSM_SETTINGS) {
            if (gameMenuCoreIsNeutrino())
                guiMsgBox(_l(_STR_NEUTRINO_SETTING_NA), 0, NULL);
            else
                guiGameShowGSConfig(0);
        } else if (menuID == GAME_VMC_SETTINGS) {
            guiGameShowVMCMenu(selected_item->item->current->item.id, static_cast<item_list_t *>(selected_item->item->userdata), itemConfig);
#ifdef PADEMU
        } else if (menuID == GAME_PADEMU_SETTINGS) {
            if (gameMenuCoreIsNeutrino())
                guiMsgBox(_l(_STR_NEUTRINO_SETTING_NA), 0, NULL);
            else
                guiGameShowPadEmuConfig(0);
        } else if (menuID == GAME_PADMACRO_SETTINGS) {
            if (gameMenuCoreIsNeutrino())
                guiMsgBox(_l(_STR_NEUTRINO_SETTING_NA), 0, NULL);
            else
                guiGameShowPadMacroConfig(0);
#endif
        } else if (menuID == GAME_OSD_LANGUAGE_SETTINGS) {
            if (gameMenuCoreIsNeutrino())
                guiMsgBox(_l(_STR_NEUTRINO_SETTING_NA), 0, NULL);
            else
                guiGameShowOSDLanguageConfig(0);
        } else if (menuID == GAME_SAVE_CHANGES) {
            if (itemConfig == NULL) {
                guiMsgBox(_l(_STR_ERROR_SAVING_SETTINGS), 0, NULL);
                return;
            }
            int changed = guiGameSaveConfig(itemConfig, static_cast<item_list_t *>(selected_item->item->userdata));
            if (changed < 0)
                return; // Denied lock removal must not write either configuration or report success.
            if (changed)
                configSetInt(itemConfig, CONFIG_ITEM_CONFIGSOURCE, CONFIG_SOURCE_USER);
            int okItem = menuSaveConfig();           // per-game CFG/<id>.cfg (itemConfig)
            int okGame = saveConfig(CONFIG_GAME, 0); // the global game config set
            // #245: claim "saved" only when BOTH writes actually persisted. This modal was
            // UNCONDITIONAL, so a failed write showed "Game settings saved" immediately followed
            // by the async "error writing settings!" toast -- the exact contradiction Andrew
            // reported. On failure the error toast already names the path and errno; don't also
            // lie that it saved.
            menuSetParentalLockCheckState(1);
            if (okItem && okGame)
                guiMsgBox(_l(_STR_GAME_SETTINGS_SAVED), 0, NULL);
            guiGameLoadConfig(selected_item->item->current->item.id, static_cast<item_list_t *>(selected_item->item->userdata), gameMenuLoadConfig(NULL));
        } else if (menuID == GAME_TEST_CHANGES) {
            guiGameTestSettings(selected_item->item->current->item.id, static_cast<item_list_t *>(selected_item->item->userdata), itemConfig);
        } else if (menuID == GAME_REMOVE_CHANGES) {
            if (guiGameShowRemoveSettings(itemConfig, configGetByType(CONFIG_GAME))) {
                menuSetParentalLockCheckState(1);
                guiGameLoadConfig(selected_item->item->current->item.id, static_cast<item_list_t *>(selected_item->item->userdata), gameMenuLoadConfig(NULL));
            }
        } else if (menuID == GAME_RENAME_GAME) {
            menuRenameGame(&gameMenu);
        } else if (menuID == GAME_DELETE_GAME) {
            menuDeleteGame(&gameMenu);
        } else if (menuID == GAME_HIDE_GAME) {
            menuHideGame(&gameMenu);
        }
        // so the exit press wont propagate twice
        readPads();
    }

    if (getKeyOn(KEY_START) || getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
        guiSwitchScreen(GUI_SCREEN_MAIN);
    }
}

void menuRenderAppMenu()
{
    // Optional themed settings background (use_settings_bg): guiDrawBGSettings draws it and
    // returns non-zero; 0 means the theme has none, so fall back to the plasma. dia.c already
    // did this for dialogs -- these three menu screens did not, so a theme that shipped a
    // settings background got it on its dialogs and the plasma on its menus.
    if (guiDrawBGSettings() == 0)
        guiDrawBGPlasma();

    if (!appMenu)
        return;

    // draw the animated menu
    if (!appMenuCurrent)
        appMenuCurrent = appMenu;

    submenu_list_t *it = appMenu;

    // calculate the number of items
    int count = 0;
    int sitem = 0;
    for (; it; count++, it = it->next) {
        if (it == appMenuCurrent)
            sitem = count;
    }

    int spacing = 25;
    int y = (gTheme->usedHeight >> 1) - (spacing * (count >> 1));
    int cp = 0; // current position

    // app title
    fntRenderString(gTheme->fonts[0], 320, 20, ALIGN_CENTER, 0, 0, cfgTitleDisplayName((item_list_t *)selected_item->item->userdata, selected_item->item->current->item.id, submenuItemGetText(&selected_item->item->current->item)), gTheme->selTextColor);

    // A PS1 row opens this smaller menu, not the PS2 VMC editor. Say why the missing PS2 control
    // is intentional without claiming that Ember and POPSTARTER store their cards identically.
    if (appMenu->item.id == APP_GLOBAL_PS_SETTINGS)
        fntRenderStringFit(gTheme->fonts[0], 320, 43, ALIGN_CENTER, 600,
                           _l(_STR_PS1_SAVE_GUIDANCE), gTheme->textColor, 0);

    for (it = appMenu; it; it = it->next, cp++) {
        // render, advance
        fntRenderString(gTheme->fonts[0], 320, y, ALIGN_CENTER, 0, 0, submenuItemGetText(&it->item), (cp == sitem) ? gTheme->selTextColor : gTheme->textColor);
        y += spacing;
    }

    // hints
    guiDrawSubMenuHints();
}

void menuHandleInputAppMenu()
{
    if (!appMenu)
        return;

    if (!appMenuCurrent)
        appMenuCurrent = appMenu;

    if (getKey(KEY_UP)) {
        sfxPlay(SFX_CURSOR);
        if (appMenuCurrent->prev)
            appMenuCurrent = appMenuCurrent->prev;
        else // rewind to the last item
            while (appMenuCurrent->next)
                appMenuCurrent = appMenuCurrent->next;
    }

    if (getKey(KEY_DOWN)) {
        sfxPlay(SFX_CURSOR);
        if (appMenuCurrent->next)
            appMenuCurrent = appMenuCurrent->next;
        else
            appMenuCurrent = appMenu;
    }

    if (getKeyOn(gSelectButton)) {
        // execute the item via looking at the id of it
        int menuID = appMenuCurrent->item.id;

        sfxPlay(SFX_CONFIRM);

        if (menuID == APP_RENAME) {
            menuRenameGame(&appMenu);
        } else if (menuID == APP_DELETE) {
            menuDeleteGame(&appMenu);
        } else if (menuID == APP_GLOBAL_PS_SETTINGS) {
            guiShowPsEmulationSettings();
        } else if (menuID == APP_EMBER_GAME_SETTINGS) {
            guiShowEmberGameSettings();
        } else if (menuID == APP_HIDE_GAME) {
            menuHideGame(&appMenu);
#ifdef RETROACHIEVEMENTS
        } else if (menuID == GAME_RA_CHECK) {
            menuCheckSelectedGameSupport();
        } else if (menuID == GAME_RA_TEST) {
            sbTestPCLinkDeferred();
            guiShowRANotice(_l(_STR_RA_LOOKING_FOR_PC), NULL);
        } else if (menuID == GAME_RA_ACHIEVEMENTS) {
            menuOpenSelectedVcdAchievements();
#endif
        }
        // so the exit press wont propagate twice
        readPads();
    }

    if (getKeyOn(KEY_START) || getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
        guiSwitchScreen(GUI_SCREEN_MAIN);
    }
}
