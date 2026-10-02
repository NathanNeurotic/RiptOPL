#ifndef __SETTINGSHELL_H
#define __SETTINGSHELL_H

#include "include/opl.h" // gMMCEPrefix (sizes SourceState::mmcePrefix)
#include "include/dia.h" // struct UIItem, DIA_RESULT_*

// The Settings shell (src/ui/guisettings.cpp). Owns the composed-page scratch buffers, the
// shell/page/save-pending flags and the transactional source-selection snapshot taken when
// Settings opens, so "Exit without saving" can restore the device/network state that was active
// at entry (#806, zackcage6) instead of leaving an applied-but-unsaved source empty. Pages stay
// free functions in guisettings.cpp and reach the shell through gSettingsShell. One instance.
constexpr int SETTINGS_DIALOG_CAPACITY = 256;

struct SourceState
{
    int defaultDevice;
    int bdmStartMode, hddStartMode, appStartMode, mmceStartMode, favStartMode;
    int enableUSB, enableILK, enableMX4SIO, enableBdmHDD;
    int networkProtocol, netStartMode, netProtocolPick;
    int ethStartMode, enableUDPBD, netBootProtocol;
    int mmceSlot, mmceIgrSlot, mmceEnableGameID, mmceAckWaitCycles, mmceUseAlarms;
    char mmcePrefix[sizeof(gMMCEPrefix)];
};

class SettingsShell
{
public:
    // Enter the shell on `page` (the Settings Index first when showIndexFirst); the only exits.
    void showFromPage(int page, int showIndexFirst);
    // The Settings Index hub. Returns 1 with *page set when a page is chosen, 0 to leave Settings.
    int showIndex(int *page);
    // Save / Exit without saving / Continue-editing prompt (SETTINGS_PROMPT_*).
    int promptSave();

    // Back/L1/R1 off a page whose rows the user did not change: such an exit applies nothing,
    // so an untouched Back cannot re-apply every source (#806).
    int leftUntouched(struct UIItem *ui, int result, const char *pathEdit, const char *pathValue);
    // Every page returns through here: L1/R1/Index pass back, anything else lands at the Index
    // and arms the save prompt (Cancel is the only exception).
    int pageResult(int result);
    int isShellResult(int result);
    int isPeerResult(int result);

    // Compose a peer page into the shared scratch buffer (or mmceDialog() for the MMCE child).
    struct UIItem *compose(const struct UIItem *const *parts, int partCount, const int *skipIDs,
                           int skipCount, int skipPart, int suppressSecondaryHeaders);
    struct UIItem *composeInto(struct UIItem *dialog, const struct UIItem *const *parts, int partCount,
                               const int *skipIDs, int skipCount, int skipPart, int suppressSecondaryHeaders);
    struct UIItem *mmceDialog() { return mmceDialog_; }

    // Page indicator + Settings dialog context. No-ops when the shell is not active, so legacy
    // standalone callers of a page keep their old behaviour.
    void beginDialog(struct UIItem *ui);
    void endDialog();

    // State the pages (free functions) legitimately share with the shell.
    struct UIItem *activeDialog() const { return activeDialog_; }
    void setActiveDialog(struct UIItem *ui) { activeDialog_ = ui; }
    bool shellActive() const { return shellActive_; }
    void markSavePending() { savePending_ = 1; }

    // The entry snapshot and its restore (bdm refresh + config re-apply only when it changed).
    void captureSourceState();
    void restoreSourceState();

private:
    void readSourceState(SourceState *state);
    bool sourceStateChanged();

    struct UIItem dialog_[SETTINGS_DIALOG_CAPACITY];
    // MMCE Settings can open from the composed Game Sources page; its editor stays separate so
    // returning from that child cannot overwrite the parent's dialog contents.
    struct UIItem mmceDialog_[SETTINGS_DIALOG_CAPACITY];
    struct UIItem *activeDialog_ = NULL;
    bool shellActive_ = false;
    int currentPage_ = 0;
    int savePending_ = 0;
    char pageIndicator_[32] = {};
    SourceState sourceState_ = {};
};

extern SettingsShell gSettingsShell;

#endif
