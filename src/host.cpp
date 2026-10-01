// The one place that binds the host interfaces (include/host.h) to the application. Everything here forwards to
// functions that already existed in opl.cpp, gui.cpp and libview.cpp; it is the application layer, so it is
// allowed to depend on all of them.
#include "include/host.h"
#include "include/opl.h"
#include "include/gui.h"
#include "include/libview.h"

namespace
{

class OplUi final : public UiServices
{
public:
    int msgBox(const char *text, int addAccept, struct UIItem *ui) override { return guiMsgBox(text, addAccept, ui); }
    void warning(const char *text, int count) override { guiWarning(text, count); }
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

// Stateless, trivially constructed: constant-initialised, so safe before any other static init.
OplUi gUi;
OplApp gApp;
OplLibrary gLibrary;

} // namespace

namespace host
{
UiServices &ui() { return gUi; }
AppServices &app() { return gApp; }
LibraryServices &library() { return gLibrary; }
} // namespace host
