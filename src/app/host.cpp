// The one place that binds the host interfaces (include/host.h) to the application. Everything here forwards to
// functions that already existed in opl.cpp, gui.cpp and libview.cpp; it is the application layer, so it is
// allowed to depend on all of them.
#include "include/host.h"
#include "include/opl.h"
#include "include/cfgtitle.h"
#include "include/gui.h"
#include "include/libview.h"
#include "include/lang.h"
#include "include/cheatman.h"
#include "include/pad.h"
#include "include/pggsm.h"
#include "../ee_core/include/coreconfig.h"
#ifdef RETROACHIEVEMENTS
#include "include/rawatch.h"
#include "include/discsupport.h"
#include "include/ranet.h"
#include "modules/network/common/ra_snap.h"
#include "include/ra_features.h"
#endif

namespace
{

class OplUi final: public UiServices
{
public:
    int msgBox(const char *text, int addAccept, struct UIItem *ui) override { return guiMsgBox(text, addAccept, ui); }
    void missingFile(const char *message, const char *path) override
    {
        char text[384];
        if (path == NULL || path[0] == '\0') {
            guiMsgBox(message, 0, NULL);
            return;
        }
        snprintf(text, sizeof(text), "%s\n%s", message, path);
        guiMsgBox(text, 0, NULL);
    }
    void warning(const char *text, int count) override { guiWarning(text, count); }
    void missingFileId(int strId, const char *path) override { missingFile(_l(strId), path); }
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
    void manageCheats(config_set_t *configSet) override { guiManageCheats(configSet); }
    int showVcdUsbMode() override { return guiShowVcdUsbMode(); }
    int pickEmberDisc(const char *folder, const struct cue_disc_list *list) override { return guiShowEmberDiscList(folder, list); }
    int promptRebootIop() override { return guiPromptRebootIop(); }
    int active() override { return guiIsActive(); }
    int confirmId(int strId, const char *detail) override
    {
        char text[480];
        if (detail == NULL)
            return guiMsgBox(_l(strId), 1, NULL);
        snprintf(text, sizeof(text), "%s\n\n%s", _l(strId), detail);
        return guiMsgBox(text, 1, NULL);
    }
    void drawBackgroundPlasma() override { guiDrawBGPlasma(); }
    void lock() override { guiLock(); }
    void unlock() override { guiUnlock(); }
};

class OplApp final: public AppServices
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

class OplLibrary final: public LibraryServices
{
public:
    int listViewActive(const item_list_t *itemList) override { return libListViewActive(itemList); }
    int viewActive(int mode) override { return libViewActive(mode); }
    int consumeDirty(int mode) override { return libViewConsumeDirty(mode); }
    void markDirty(int mode) override { libViewMarkDirty(mode); }
    int listRowView(item_list_t *itemList, int id) override { return libListRowView(itemList, id); }
    int listSourceId(item_list_t *itemList, int id) override { return libListSourceId(itemList, id); }
    const char *listDisplayName(item_list_t *itemList, int id, const char *text) override { return cfgTitleDisplayName(itemList, id, text); }
    void bdmAttach(int mode, int bdmType) override { libViewBdmAttach(mode, bdmType); }
};

#ifdef IGR_MENU
static void *igrCheats; // OplLaunch::igrCheatsPlace's block, for fillCoreConfig
#endif

class OplLaunch final: public LaunchServices
{
public:
#ifdef RETROACHIEVEMENTS
    // Snapshot, watch words and chain scratch; 0 when there is nothing valid to watch.
    static unsigned int raListBytes()
    {
        const int count = GetWatchCount(), nodes = GetNodeCount();
        if (!gRATelemetry || !raWatchEntriesValid(GetWatchList(), count, GetWatchBytes()) || nodes < 0 || nodes > RA_NODE_MAX)
            return 0;
        return RA_EE_WORK_BYTES(count, nodes);
    }

    // Where the card packet starts, cache-line aligned behind the list.
    static unsigned int raCardOffset(unsigned int listBytes) { return (listBytes + 63) & ~63u; }
#endif

    unsigned int raWorkBytes(const void *at) override
    {
#ifdef RETROACHIEVEMENTS
        const unsigned int list = raListBytes();
        if (list == 0 || !RA_EXPERIMENTAL_CARD_DMA || gRAMode != RA_MODE_CADUCEUS || gRAExperimentalCards != 1)
            return list;
        // The kernel keeps module storage from the game. Below 1 MB it must stay below 1 MB, where
        // games load; without room the cards fall back to the pulse.
        const unsigned int start = (unsigned int)(uintptr_t)at, limit = start < 0x00100000 ? 0x00100000 : 0x02000000;
        const unsigned int total = raCardOffset(list) + RA_CARD_PACKET_BYTES;
        if (start > limit || total > limit - start) {
            raLaunchNote("overlay-no-room", RA_CARD_PACKET_BYTES, (int)start);
            return list;
        }
        return total;
#else
        (void)at;
        return 0;
#endif
    }

    void fillCoreConfig(struct EECoreConfig_t *config, bool discMode, void *raWorkArea, unsigned int raWorkBytes) override
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
        config->raWorkBytes = raWorkBytes;
        // Only a reservation that grew past the list holds a card packet.
        const unsigned int list = raListBytes();
        config->raOverlayBuf = raWorkBytes >= raCardOffset(list) + RA_CARD_PACKET_BYTES ? (u8 *)raWorkArea + raCardOffset(list) : NULL;

        // raudp's companion: the peer the menu verified, else the configured PC, else the
        // share the game streams from (where a passive listener's notices come from).
        config->raClientMode = gRAMode;
        config->raHostPinned = (gRAHostIp[0] | gRAHostIp[1] | gRAHostIp[2] | gRAHostIp[3]) != 0;
        if (unsigned int peer = raNetPeerIP()) {
            const u8 *ip = (const u8 *)&peer;
            snprintf(config->raHost, sizeof(config->raHost), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        } else {
            const int *ip = config->raHostPinned ? gRAHostIp : !strcmp(config->GameModeDesc, "HTTP_MODE") ? gHttpServerIp :
                                                                                                            pc_ip;
            snprintf(config->raHost, sizeof(config->raHost), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        }

        // The last point where the list is still ours: from here it goes into ee_core
        // with no feedback. A zero shows up in the launch log directly, rather than as
        // empty packets on the PC.
        raLaunchNote("to-ee-core", config->raWatchCount, config->raSnapBytes);
#else
        (void)raWorkArea;
        (void)raWorkBytes;
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

#ifdef IGR_MENU
        // The in-game menu (RIPPS2's, by akilluminati47; ee_core igrmenu.c). RetroAchievements editions and the IGS
        // and freeze-report configurations build ee_core without it (no room), and leave these zero.
        config->IgrMenu = gIgrMenu;
        config->IgrConfirmCircle = gSelectButton == KEY_CIRCLE;
        if (igrCheats != NULL && config->gCheatList != NULL) {
            set_cheats_list_live(igrCheats); // every hook, and the ticked cheats as the menu builds them
            config->gCheatList = GetCheatsList();
            config->IgrCheats = igrCheats;
        }
#endif
    }

    unsigned int igrCheatsPlace(void *at, unsigned int room) override
    {
#ifdef IGR_MENU
        igrCheats = NULL;
        int bytes = gIgrMenu && GetCheatsEnabled() ? igrCheatsPack(at, room) : 0;
        if (bytes > 0)
            igrCheats = at;
        return bytes > 0 ? (unsigned int)bytes : 0;
#else
        (void)at;
        (void)room;
        return 0;
#endif
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
