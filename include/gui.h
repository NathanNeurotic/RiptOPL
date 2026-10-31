#ifndef __GUI_H
#define __GUI_H

#include "include/iosupport.h"
#include "include/opl.h"
#include "include/texcache.h"
#include "include/dialogs.h"
#include "include/menusys.h"

typedef enum {
    // Informs gui that init is over and main gui can be rendered
    GUI_INIT_DONE = 1,
    GUI_OP_ADD_MENU,
    GUI_OP_APPEND_MENU,
    GUI_OP_SELECT_MENU,
    GUI_OP_CLEAR_SUBMENU,
    GUI_OP_SORT,
    GUI_OP_ADD_HINT
} gui_op_type_t;

/** a single GUI update in a package form */
struct gui_update_t
{
    gui_op_type_t type;

    struct
    {
        menu_item_t *menu;
        submenu_list_t **subMenu;
    } menu;

    union
    {
        struct
        {
            int icon_id;
            char *text;
            int id;
            int text_id;
            int selected;
            int autoStart;  // selected from the persisted Last Played setting, not cursor restoration
            int isFolder;   // folder-browse row marker (checklist item 34)
            int lastPlayed; // recents rank for Sort Games: Last played, resolved on the I/O thread
            int favourited; // All Games row that is also a favourite (its star)
#ifdef RETROACHIEVEMENTS
            int raBadged; // RA watch list present -- resolved on the I/O thread, see submenu_item_t
            char *raText; // owned badge snapshot until transferred to the row
#endif
        } submenu;

        struct
        { // hint for the given menu
            int icon_id;
            int text_id;
        } hint;
    };
};

typedef void (*gui_callback_t)(void);

extern int guiInactiveFrames;
extern int guiFrameId;

constexpr int GUI_SCREEN_MAIN = 0;
constexpr int GUI_SCREEN_MENU = 1;
constexpr int GUI_SCREEN_INFO = 2;
constexpr int GUI_SCREEN_GAME_MENU = 3;
constexpr int GUI_SCREEN_APP_MENU = 4;
#ifdef RETROACHIEVEMENTS
constexpr int GUI_SCREEN_ACHIEVEMENTS = 5;
#endif

void guiSwitchScreen(int target);

void guiReloadScreenExtents();

/** Initializes the GUI */
void guiInit();

/** Clean-up the GUI */
void guiEnd();

/** Locks gui for direct gui data updates */
void guiLock();

/** Unlocks gui after direct gui data updates */
void guiUnlock();

/** invokes the intro loop */
void guiIntroLoop();

/** invokes the main loop */
void guiMainLoop();

/** Hooks a single per-frame callback */
void guiSetFrameHook(gui_callback_t cback);

// Deffered update handling:
/* Note:
All the GUI operation requests can be deffered to the proper time
when rendering is not going on. This allows us, for example, to schedule
updates of the menu from another thread without stalls.
*/


/** Defers the given update to an appropriate time.
 * @param op The operation to defer
 * @return int The operation serial id
 */
int guiDeferUpdate(struct gui_update_t *op);

void guiExecDeferredOps(void);

/** Allocates a new deffered operation */
struct gui_update_t *guiOpCreate(gui_op_type_t type);


int guiShowKeyboard(char *value, int maxLength);
int guiMsgBox(const char *text, int addAccept, struct UIItem *ui);
int guiIsActive(void);
int guiPromptRebootIop(void);
// The user's Confirm / Cancel buttons (Select Button setting) and their icons. Every screen asks these
// instead of assuming Cross = confirm.
int guiConfirmKey(void);
int guiCancelKey(void);
int guiConfirmIcon(void);
int guiCancelIcon(void);

void guiUpdateScrollSpeed(void);
void guiUpdateScreenScale(void);

void guiDrawBGPlasma();
#include "include/uidraw.h" // the hint helpers moved out of gui.cpp
void guiDrawSubMenuHints(void);


void guiShowAbout();
void guiShowSettings(void);
void guiShowPsEmulationSettings(void); // PS1 Triangle shortcut to the global PS emulation page
void guiShowEmberGameSettings(void);   // PS1 Triangle, Ember rows: timing/dither/shading/controller
// Settings-layout category pages (rebuild step 06)
void guiShowColorsConfig(void);
int guiDrawBGSettings(void);
int guiNeutrinoPathHandler(char *text, int maxLen);
int guiPopstarterPathHandler(char *text, int maxLen);
int guiExitPathHandler(char *text, int maxLen);
int guiShowNeutrinoArgsConfig(char *argsBuf, int bufSize); // 1 when the saved args changed
int guiNeutrinoArgsCwdHandler(char *text, int maxLen);
int guiNeutrinoArgsCfgHandler(char *text, int maxLen);
int guiNeutrinoArgsElfHandler(char *text, int maxLen);
int guiNeutrinoArgsAta0Handler(char *text, int maxLen);
int guiNeutrinoArgsAta0idHandler(char *text, int maxLen);
int guiNeutrinoArgsAta1Handler(char *text, int maxLen);
int guiNeutrinoArgsExtraHandler(char *text, int maxLen);
int guiShowNetConfig(void);


// 1 when a network stack is resident and the saved protocol is NOT the one running -- i.e. the
// user's choice cannot take effect until OPL is restarted. Asked from the SAVE path, never from the
// picker: the settings dialogs only touch RAM and Save Changes is a separate menu action, so acting
// at picker time would tear OPL down before the change was ever written to disk.
int guiNetProtocolNeedsRestart(void);

// One-shot POPSTARTER USB driver pick (FAT32 vs exFAT), shown on EVERY USB VCD launch
// (maintainer directive 2026-08-01: the user picks per launch, never a sticky default).
int guiShowVcdUsbMode(void);
struct cue_disc_list;
// The discs.txt starting-disc picker: the chosen index, or -1 for Back.
int guiShowEmberDiscList(const char *folder, const struct cue_disc_list *list);
void guiShowPopsNetConfig(void); // POPSTARTER's OWN IPCONFIG.DAT/SMBCONFIG.DAT editor

void guiCheckNotifications(int checkTheme, int checkLang);

// GUI thread only, outside a frame bracket. Draws immediately before a synchronous action,
// then keeps the feedback visible for three seconds, independently of courtesy notifications.
void guiShowToast(const char *text);

/** Renders the given string on screen for the given function until it's io finishes
 * @note The ptr pointer is watched for it's value. The IO is considered finished when the value becomes zero.
 * @param ptr The finished state pointer (1 unfinished, 0 finished)
 * @param message The message to display while working
 * @param type the io operation type
 * @param data the data for the operation
 * @param timeoutMs abandon the wait after this many ms if the deferred IO never completes (a wedged
 *        IOP fileXio channel on failing storage); 0 = wait forever (network compat-list update).
 */
// Bound for guiHandleDeferedIO's wait on a deferred config save/load. Far beyond any real config
// write; only a genuinely stuck device (which would otherwise freeze the GUI forever on "Saving...")
// reaches it, after which the caller falls into its normal failure path ("Error saving settings").
constexpr int OPL_DEFERRED_IO_TIMEOUT_MS = 30000;
void guiHandleDeferedIO(int *ptr, const char *message, int type, void *data, int timeoutMs);

void guiGameHandleDeferedIO(int *ptr, struct UIItem *ui, int type, void *data);

/** Renders a single frame with a specified message on the screen
 */
void guiRenderTextScreen(const char *message);
/* VBLANK-paced status fade, called only after the launch I/O drain. */
void guiFadeOutForHandoff(const char *message);
/* Main-thread-only fade before an explicit display-mode change. */
void guiFadeCurrentScreenToBlack(void);

/** Display the visual GameID barcode (CosmicScale scheme) for Pixel FX / RetroGEM / PS2Digital
 *  HDMI auto-profiles. No-op unless gApplyGameID is set. Call just before launching a game.
 */
void guiShowGameID(const char *startup);
// One frame of the current screen + the busy spinner, for main-thread hardware waits; returns 1 if
// the user pressed cancel, so a caller polling hardware can abandon the wait. Must NOT be
// called inside a guiStartFrame/guiEndFrame bracket, nor from any thread but the GUI thread.
int guiRenderProbeFrame(void);

void guiRenderGreetingScreen(void);
// Boot-splash status line (main thread); NULL clears both the line and the sticky label.
void guiSetBootStatus(const char *status);
// Boot-step localizer for deferred IO-thread steps; label must be a static/_l() string.
void guiSetBootStatusSticky(const char *label);
// Diagnostic boot-step setter for dynamic text. Copies into an internal double buffer before
// publishing, so an IO-thread caller may safely pass a stack-formatted message.
void guiSetBootStatusStickyCopy(const char *label);

void guiWarning(const char *text, int count);
// Word-wraps `text` to the message box width and draws it centred between the message rules.
// Returns the y of the last line, for anything drawn below the text.
int guiDrawMsgText(const char *text);
// Draws text already wrapped by fntWrapText, one centred line at a time. Returns the last line's y.
int guiDrawWrappedLines(const char *wrapped, int lines);


int guiGameShowRemoveSettings(config_set_t *configSet, config_set_t *configGame);

void guiManageCheats(config_set_t *configSet);

#ifdef RETROACHIEVEMENTS
/* RA: two-line notice popup, set from the I/O thread (the check/test actions run
   there) and drawn by the render loop. guiShowRANotices() is called once per
   frame from the main loop. */
void guiShowRANotice(const char *line1, const char *line2);
#endif

#endif
