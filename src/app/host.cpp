// The one place that binds the host interfaces (include/host.h) to the application. Everything here forwards to
// functions that already existed in opl.cpp, gui.cpp and libview.cpp; it is the application layer, so it is
// allowed to depend on all of them.
#include "include/host.h"
#include "include/opl.h"
#include "include/gui.h"
#include "include/libview.h"
#include "include/lang.h"
#include "include/cheatman.h"
#include "include/pggsm.h"
#include "../ee_core/include/coreconfig.h"
#ifdef RETROACHIEVEMENTS
#include "include/rawatch.h"
#include "include/discsupport.h"
#include "modules/network/common/ra_snap.h"
#endif

namespace
{

class OplUi final : public UiServices
{
public:
    int msgBox(const char *text, int addAccept, struct UIItem *ui) override { return guiMsgBox(text, addAccept, ui); }
    void warning(const char *text, int count) override { guiWarning(text, count); }
    void warningId(int strId, int count) override { guiWarning(_l(strId), count); }
    void raNotice(const char *line1, const char *line2) override
    {
#ifdef RETROACHIEVEMENTS
        guiShowRANotice(line1, line2); // only the RetroAchievements flavour has these notices
#else
        (void)line1;
        (void)line2;
#endif
    }
    void bootStatusSticky(const char *label) override { guiSetBootStatusSticky(label); }
    void bootStatusStickyCopy(const char *label) override { guiSetBootStatusStickyCopy(label); }
    void renderTextScreen(const char *message) override { guiRenderTextScreen(message); }
    void manageCheats() override { guiManageCheats(); }
    int showVcdUsbMode() override { return guiShowVcdUsbMode(); }
    int promptRebootIop() override { return guiPromptRebootIop(); }
    void drawBackgroundPlasma() override { guiDrawBGPlasma(); }
    void lock() override { guiLock(); }
    void unlock() override { guiUnlock(); }
};

class OplApp final : public AppServices
{
public:
    void deinit(int exception, int modeSelected) override { ::deinit(exception, modeSelected); }
    void deinitEx(int exception, int modeSelected, int modeSelected2) override { ::deinitEx(exception, modeSelected, modeSelected2); }
    void miniDeinit(config_set_t *configSet) override { ::miniDeinit(configSet); }
    int saveConfig(int types, int showUI) override { return ::saveConfig(types, showUI); }
    void applyConfig(int themeID, int langID, int skipDeviceRefresh) override { ::applyConfig(themeID, langID, skipDeviceRefresh); }
    void initSupport(item_list_t *itemList, int mode, int forceReinit) override { ::initSupport(itemList, mode, forceReinit); }
    void moduleUpdateMenu(int mode, int themeChanged, int langChanged) override { ::moduleUpdateMenu(mode, themeChanged, langChanged); }
    int pathToMode(const char *path) override { return oplPath2Mode(path); }

    void setError(int strId, int error) override { setErrorMessageWithCode(strId, error); }
    void setErrorDetail(int strId, int error, const char *detail) override { setErrorMessageWithCodeAndDetail(strId, error, detail); }
    void setErrorPath(int strId, const char *path, int error) override { setErrorMessagePathCode(strId, path, error); }
    void clearErrorIf(int strId) override { clearErrorMessageIf(strId); }

    config_set_t *legacyAppsConfig() override { return oplGetLegacyAppsConfig(); }
    config_set_t *legacyAppsInfo(char *name) override { return oplGetLegacyAppsInfo(name); }
    int scanApps(int (*callback)(const char *path, config_set_t *appConfig, void *arg), void *arg) override { return oplScanApps(callback, arg); }
    int shouldAppsUpdate() override { return oplShouldAppsUpdate(); }
};

class OplLibrary final : public LibraryServices
{
public:
    int listViewActive(const item_list_t *itemList) override { return libListViewActive(itemList); }
    int viewActive(int mode) override { return libViewActive(mode); }
    int consumeDirty(int mode) override { return libViewConsumeDirty(mode); }
    void markDirty(int mode) override { libViewMarkDirty(mode); }
    int listRowView(item_list_t *itemList, int id) override { return libListRowView(itemList, id); }
    int listSourceId(item_list_t *itemList, int id) override { return libListSourceId(itemList, id); }
    void bdmAttach(int mode, int bdmType) override { libViewBdmAttach(mode, bdmType); }
};

class OplLaunch final : public LaunchServices
{
public:
    void fillCoreConfig(struct EECoreConfig_t *config, bool discMode, void *raWorkArea) override
    {
        if (!discMode && GetCheatsEnabled()) {
            set_cheats_list();
            config->gCheatList = GetCheatsList();
        } else
            config->gCheatList = NULL;

        // PS2RD .img cheats (item 26). Without this the EE loads the .img into RAM and then never hands
        // the pointer to the core, so LinkImage() had nothing to link and the whole feature was dead.
        config->gImage = !discMode && GetImageEnabled() ? (u32 *)GetImage() : NULL;

#ifdef RETROACHIEVEMENTS
        // RetroAchievements: the watch list for this game, already loaded by the launch
        // leg. It may legitimately be absent -- the game simply is not tracked -- in
        // which case ee_core sees NULL and never starts the telemetry path.
        config->raWatchList = GetWatchList();
        config->raWatchCount = GetWatchCount();
        config->raSnapBytes = GetWatchBytes();
        config->raNodeList = GetNodeList();
        config->raNodeCount = GetNodeCount();
        config->raWorkArea = raWorkArea;

        // The last point where the list is still ours: from here it goes into ee_core
        // with no feedback. A zero shows up in the launch log directly, rather than as
        // empty packets on the PC.
        raLaunchNote("to-ee-core", config->raWatchCount, config->raSnapBytes);
#else
        (void)raWorkArea;
#endif

        struct GsmConfig_t gsm_config;
        // GSM now.
        config->EnableGSMOp = !discMode && GetGSMEnabled();
        if (config->EnableGSMOp) {
            PrepareGSM(NULL, &gsm_config);
            config->GsmConfig.interlace = gsm_config.interlace;
            config->GsmConfig.mode = gsm_config.mode;
            config->GsmConfig.ffmd = gsm_config.ffmd;
            config->GsmConfig.display = gsm_config.display;
            config->GsmConfig.syncv = gsm_config.syncv;
            config->GsmConfig.smode2 = gsm_config.smode2;
            config->GsmConfig.dx_offset = gsm_config.dx_offset;
            config->GsmConfig.dy_offset = gsm_config.dy_offset;
            config->GsmConfig.k576P_fix = gsm_config.k576P_fix;
            config->GsmConfig.kGsDxDyOffsetSupported = gsm_config.kGsDxDyOffsetSupported;
            config->GsmConfig.FIELD_fix = gsm_config.FIELD_fix;
        }
    }

    int raWatchCount() override
    {
#ifdef RETROACHIEVEMENTS
        return GetWatchCount();
#else
        return 0;
#endif
    }

    bool discCheckBusy() override
    {
#ifdef RETROACHIEVEMENTS
        return ::discCheckBusy() != 0;
#else
        return false;
#endif
    }
};

// Stateless, trivially constructed: constant-initialised, so safe before any other static init.
OplUi gUi;
OplApp gApp;
OplLibrary gLibrary;
OplLaunch gLaunch;

} // namespace

namespace host
{
UiServices &ui() { return gUi; }
AppServices &app() { return gApp; }
LibraryServices &library() { return gLibrary; }
LaunchServices &launch() { return gLaunch; }
} // namespace host
