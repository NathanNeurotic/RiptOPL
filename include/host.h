// Services the application provides to the layers below it.
//
// Support backends (bdm, hdd, eth, app, mmce, udpfs, http, ...) and the modules under them used to call straight
// into opl.cpp (deinit, saveConfig, error messages), gui.cpp (message boxes, boot status) and libview.cpp. That made
// 43 of 55 modules one dependency cycle. They now talk to these three small interfaces instead; src/host.cpp is the
// only place that knows the concrete implementations (it forwards to the existing functions), and tests substitute
// their own.
//
//     host::ui().warning(_l(_STR_FOO), 6);
//     host::app().saveConfig(CONFIG_OPL, 0);
//     if (host::library().viewActive(mode)) ...
//
// C++ only. The accessors return references to objects with static storage and trivial construction, so they are
// usable at any point after load (including before main touches anything).
#ifndef HOST_H
#define HOST_H

#include <tamtypes.h>
#include "include/config.h"

typedef struct _item_list_t item_list_t;
struct UIItem;
struct cue_disc_list;
struct EECoreConfig_t;

// Dialogs, notices and boot status on the user-facing side.
class UiServices
{
public:
    virtual ~UiServices() = default;
    virtual int msgBox(const char *text, int addAccept, struct UIItem *ui) = 0;
    virtual void missingFile(const char *message, const char *path) = 0;
    virtual void missingFileId(int strId, const char *path) = 0;
    virtual void warning(const char *text, int count) = 0;
    // Same, for a translatable string id (the UI layer owns the language tables).
    virtual void warningId(int strId, int count) = 0;
    virtual void raNotice(const char *line1, const char *line2) = 0;
    virtual void bootStatusSticky(const char *label) = 0;
    virtual void bootStatusStickyCopy(const char *label) = 0;
    virtual void renderTextScreen(const char *message) = 0;
    virtual void manageCheats(config_set_t *configSet) = 0;
    virtual int showVcdUsbMode() = 0;
    // Ember starting disc from a discs.txt list: its index, or -1 for Back.
    virtual int pickEmberDisc(const char *folder, const struct cue_disc_list *list) = 0;
    virtual int promptRebootIop() = 0;
    // 1 while the menu can answer a question (0 during autolaunch, before the GUI exists).
    virtual int active() = 0;
    // Ask a yes/no question: the string, then (unless NULL) a blank line and detail. 1 = Accept.
    virtual int confirmId(int strId, const char *detail) = 0;
    // The GUI-wide lock (no-op before the GUI exists).
    // The animated plasma behind the menu (a theme can ask for it as its background).
    virtual void drawBackgroundPlasma() = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;
};

// Application lifecycle, persistence, error reporting and menu refresh.
class AppServices
{
public:
    virtual ~AppServices() = default;
    virtual void deinit(int exception, int modeSelected) = 0;
    virtual void deinitEx(int exception, int modeSelected, int modeSelected2) = 0;
    virtual void miniDeinit(config_set_t *configSet) = 0;
    virtual int saveConfig(int types, int showUI) = 0;
    virtual void applyConfig(int themeID, int langID, int skipDeviceRefresh) = 0;
    virtual void initSupport(item_list_t *itemList, int mode, int forceReinit) = 0;
    virtual void moduleUpdateMenu(int mode, int themeChanged, int langChanged) = 0;
    virtual int pathToMode(const char *path) = 0;

    virtual void setError(int strId, int error) = 0;
    virtual void setErrorDetail(int strId, int error, const char *detail) = 0;
    virtual void setErrorPath(int strId, const char *path, int error) = 0;
    virtual void clearErrorIf(int strId) = 0;

    // Legacy "apps" discovery owned by the application.
    virtual config_set_t *legacyAppsConfig() = 0;
    virtual config_set_t *legacyAppsInfo(char *name) = 0;
    virtual int scanApps(int (*callback)(const char *path, config_set_t *appConfig, void *arg), void *arg) = 0;
    virtual int shouldAppsUpdate() = 0;
};

// Which game-library page/view is showing, and the dirty flags that make a page rescan.
class LibraryServices
{
public:
    virtual ~LibraryServices() = default;
    virtual int listViewActive(const item_list_t *itemList) = 0;
    virtual int viewActive(int mode) = 0;
    virtual int consumeDirty(int mode) = 0;
    virtual void markDirty(int mode) = 0;
    virtual int listRowView(item_list_t *itemList, int id) = 0;
    virtual int listSourceId(item_list_t *itemList, int id) = 0;
    // Borrowed presentation text; caller holds the GUI frame lock. Never reads CFG files.
    virtual const char *listDisplayName(item_list_t *itemList, int id, const char *text) = 0;
    virtual void bdmAttach(int mode, int bdmType) = 0;
};

// What optional features (cheats, GSM, RetroAchievements) contribute to a game launch. system.cpp builds the ee_core
// configuration but must not know those feature modules.
class LaunchServices
{
public:
    virtual ~LaunchServices() = default;
    // Fill the feature-derived fields of the ee_core config: cheat list, PS2RD image, RetroAchievements watch
    // list/buffers, GSM. discMode launches carry none of cheats/image/GSM.
    virtual void fillCoreConfig(struct EECoreConfig_t *config, bool discMode, void *raWorkArea, unsigned int raWorkBytes) = 0;
    // ee_core's RA work area for the list loaded now, placed at 'at' in module storage.
    virtual unsigned int raWorkBytes(const void *at) = 0;
    // Entries in the loaded RetroAchievements watch list (0 outside that flavour or for an untracked game).
    virtual int raWatchCount() = 0;
    // The in-game menu's CHEATS page (ee_core igrcheats.h): with the menu and the game's cheats on, every cheat of
    // the file is packed at 'at' (room bytes); returns the bytes used (0: none). fillCoreConfig then hands it to
    // ee_core. Always 0 in builds without IGR_MENU.
    virtual unsigned int igrCheatsPlace(void *at, unsigned int room) = 0;
    virtual bool discCheckBusy() = 0;
};

namespace host
{
UiServices &ui();
AppServices &app();
LaunchServices &launch();
LibraryServices &library();
} // namespace host

#endif // HOST_H
