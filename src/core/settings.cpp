// Table-driven persistence for the plain RiptOPL settings: one row per CONFIG_OPL_* key that is
// loaded, saved and defaulted without any special handling. Before this table each key appeared three times
// in opl.cpp (setDefaults, _loadConfig, _saveConfig); a key added to one and forgotten in another silently
// failed to persist. Anything with migration, clamping or cross-key logic (video-mode recovery, the network
// protocol derivation, theme/language, ...) stays in opl.cpp; those clamps run after settingsLoad().
//
// Row order is the old save order. Config-file key names and syntax are unchanged.
#include "include/opl.h"
#include "include/iosupport.h"
#include "include/themes.h"
#include "include/vcdsupport.h"
#include "include/favsupport.h"
#include "include/settings.h"

namespace
{

struct Setting
{
    enum Kind : unsigned char { Int,
                                Str,
                                Color } kind;
    const char *key;
    void *var;           // int*, char[size] or unsigned char[3]
    unsigned short size; // Str buffer size
    bool hasDefault;
    bool loadPlain; // false: the loader reads this key itself (settingsLoad skips it)
    int intDefault;
    const char *strDefault;
    unsigned char colorDefault[3];
};

constexpr Setting intSetting(const char *key, int *var, int def, bool loadPlain = true)
{
    return {Setting::Int, key, var, 0, true, loadPlain, def, nullptr, {0, 0, 0}};
}
// No default: the variable's own initialiser (or another default routine) owns its starting value.
constexpr Setting intSetting(const char *key, int *var)
{
    return {Setting::Int, key, var, 0, false, true, 0, nullptr, {0, 0, 0}};
}
template <unsigned N>
constexpr Setting strSetting(const char *key, char (&buf)[N], const char *def = "")
{
    return {Setting::Str, key, buf, N, true, true, 0, def, {0, 0, 0}};
}
constexpr Setting colorSetting(const char *key, unsigned char (&c)[3])
{
    return {Setting::Color, key, c, 3, false, true, 0, nullptr, {0, 0, 0}};
}
constexpr Setting colorSetting(const char *key, unsigned char (&c)[3], const unsigned char (&def)[3])
{
    return {Setting::Color, key, c, 3, true, true, 0, nullptr, {def[0], def[1], def[2]}};
}

constexpr Setting kSettings[] = {
    intSetting(CONFIG_OPL_SCROLLING, &gScrollSpeed, 1),
    colorSetting(CONFIG_OPL_BGCOLOR, gDefaultBgColor),
    colorSetting(CONFIG_OPL_TEXTCOLOR, gDefaultTextColor),
    colorSetting(CONFIG_OPL_UI_TEXTCOLOR, gDefaultUITextColor),
    colorSetting(CONFIG_OPL_TITLECOLOR, gDefaultTitleColor),
    colorSetting(CONFIG_OPL_SEL_TEXTCOLOR, gDefaultSelTextColor),
    intSetting(CONFIG_OPL_ENABLE_NOTIFICATIONS, &gEnableNotifications, 1),
#ifdef RETROACHIEVEMENTS
    intSetting(CONFIG_OPL_RA_TELEMETRY, &gRATelemetry, 0), // opt in: telemetry puts SMAP on the NIC in every launch that has a .wl
#endif
#ifdef RETROACHIEVEMENTS
    intSetting(CONFIG_OPL_RA_BADGES, &gRABadges, 1),                        // free once telemetry is on -- raBadgeRefresh runs on the I/O thread
    intSetting(CONFIG_OPL_RA_BADGE_SUFFIX, &gRABadgeSuffix, 0),             // the shipped prefix
    intSetting(CONFIG_OPL_RA_MODE, &gRAMode, RA_MODE_XERABORA),             // old configurations keep Xerabora
    intSetting(CONFIG_OPL_RA_EXPERIMENTAL_CARDS, &gRAExperimentalCards, 0), // explicit opt-in only
#endif
    intSetting(CONFIG_OPL_ENABLE_COVERART, &gEnableArt, 1),
    intSetting(CONFIG_OPL_ENABLE_DISCART, &gEnableDiscArt, 1), // preserve existing ItemIcon/ICO behavior unless the user disables it
    intSetting(CONFIG_OPL_ENABLE_LOGOART, &gEnableLogoArt, 1), // preserve existing LGO artwork unless the user disables it
    intSetting(CONFIG_OPL_WIDESCREEN, &gWideScreen, 0), // fresh configurations use 4:3; a saved choice overrides
    intSetting(CONFIG_OPL_FONT_SIZE, &gFontSize, FONT_SIZE_DEFAULT),
    intSetting(CONFIG_OPL_GLYPH_SET, &gGlyphSet, 0), // GLYPH_SET_THEME: the theme's own (RIPPS2 build 90)
    // Off: the theme's default= pictures (KrahJohlito's alt= idea, themeviews.h).
    intSetting(CONFIG_OPL_THEME_ALT_BG, &gThemeAltBackground, 0),
    intSetting(CONFIG_OPL_VMODE, &gVMode, 0, /*loadPlain=*/false),
    intSetting(CONFIG_OPL_XOFF, &gXOff, 0),
    intSetting(CONFIG_OPL_YOFF, &gYOff, 0),
    intSetting(CONFIG_OPL_OVERSCAN, &gOverscan, 0),
    intSetting(CONFIG_OPL_DISABLE_DEBUG, &gEnableDebug, 0),
    intSetting(CONFIG_OPL_PS2LOGO, &gPS2Logo, 1), // opinionated defaults: the fork ships ready-to-use
    intSetting(CONFIG_OPL_HDD_GAME_LIST_CACHE, &gHDDGameListCache, 0),
    strSetting(CONFIG_OPL_EXIT_PATH, gExitPath),
    strSetting(CONFIG_OPL_CUSTOM_SETTINGS_PATH, gCustomSettingsPath),
    intSetting(CONFIG_OPL_AUTO_SORT, &gAutosort, 1),
    intSetting(CONFIG_OPL_AUTO_REFRESH, &gAutoRefresh, 0),
    intSetting(CONFIG_OPL_DEFAULT_DEVICE, &gDefaultDevice, APP_MODE),
    intSetting(CONFIG_OPL_AUTO_CREATE_VMC, &gAutoCreateVmc, 0),
    intSetting(CONFIG_OPL_IGR_MENU, &gIgrMenu, 0),
    intSetting(CONFIG_OPL_ENABLE_WRITE, &gEnableWrite, 1),
    intSetting(CONFIG_OPL_HDD_SPINDOWN, &gHDDSpindown, 20),
    strSetting(CONFIG_OPL_BDM_PREFIX, gBDMPrefix),
    strSetting(CONFIG_OPL_ETH_PREFIX, gETHPrefix),
    intSetting(CONFIG_OPL_REMEMBER_LAST, &gRememberLastPlayed, 0),
    intSetting(CONFIG_OPL_AUTOSTART_LAST, &gAutoStartLastPlayed, 9),
    intSetting(CONFIG_OPL_BDM_MODE, &gBDMStartMode, START_MODE_DISABLED),
    intSetting(CONFIG_OPL_HDD_MODE, &gHDDStartMode, START_MODE_DISABLED),
    intSetting(CONFIG_OPL_ETH_MODE, &gETHStartMode, START_MODE_DISABLED),
    intSetting(CONFIG_OPL_APP_MODE, &gAPPStartMode, START_MODE_DISABLED),
    strSetting(CONFIG_OPL_MMCE_PREFIX, gMMCEPrefix),
    intSetting(CONFIG_OPL_MMCE_MODE, &gMMCEStartMode, START_MODE_DISABLED),
    intSetting(CONFIG_OPL_MMCE_SLOT, &gMMCESlot, 2), // Default to first Auto slot
    intSetting(CONFIG_OPL_MMCEIGR_SLOT, &gMMCEIGRSlot, 3),
    intSetting(CONFIG_OPL_MMCE_GAMEID, &gMMCEEnableGameID, 0),
    intSetting(CONFIG_OPL_MMCE_WAIT_CYCLES, &gMMCEAckWaitCycles, 5),
    intSetting(CONFIG_OPL_MMCE_USE_ALARMS, &gMMCEUseAlarms, 1),
    intSetting(CONFIG_OPL_BDM_CACHE, &bdmCacheSize, 16),
    intSetting(CONFIG_OPL_HDD_CACHE, &hddCacheSize, 8),
    intSetting(CONFIG_OPL_SMB_CACHE, &smbCacheSize, 16),
    intSetting(CONFIG_OPL_ENABLE_USB, &gEnableUSB, 0), // USB block device is opt-in, like the other BDM transports
    intSetting(CONFIG_OPL_FAV_MODE, &gFAVStartMode, START_MODE_DISABLED),
    intSetting(CONFIG_OPL_ALL_GAMES, &gAllGames, 0),     // All Games list (RIPPS2), off by default
    intSetting(CONFIG_OPL_SHOW_HIDDEN, &gShowHidden, 0), // Show Hidden Games (RIPPS2 Hide Game), off by default
    // Visual GameID barcode ships OFF, matching fork commit cc2cdfed ("GameID defaults OFF"): the
    // HDMI auto-profile latch is only verifiable on real GameID hardware, so it stays opt-in.
    intSetting(CONFIG_OPL_APPLY_GAMEID, &gApplyGameID, 0),
    intSetting(CONFIG_OPL_DEFAULT_GAME_VIEW, &gDefaultGameView, GAME_VIEW_BOTH),
    intSetting(CONFIG_OPL_APPS_DISPLAY, &gAppsDisplay, APPS_DISPLAY_MIXED),
    intSetting(CONFIG_OPL_CFG_TITLES, &gCfgTitlesInLists, 0),
    intSetting(CONFIG_OPL_GAME_TYPE_LABELS, &gGameTypeLabels, 0),
    strSetting(CONFIG_OPL_POPSTARTER_PATH, gPopstarterPath),
    intSetting(CONFIG_OPL_BDMA_SOURCE, &gBdmaSource, VCD_BDMA_SRC_USB),
    intSetting(CONFIG_OPL_BDMA_APPLY, &gBdmaApplyOnLaunch, 1),               // auto-equip on launch by default
    intSetting(CONFIG_OPL_VCD_USB_BDMA, &gVcdUsbBdmaMode, VCD_USB_BDMA_ASK), // preserve the per-launch prompt as the shipped behaviour
    intSetting(CONFIG_OPL_VCD_HIDE_GAMEID, &gVcdHideGameId, 1),              // hide the PS1 game-ID prefix by default (display-only)
    intSetting(CONFIG_OPL_VCD_FIRST_DISC_ONLY, &gVcdFirstDiscOnly, 1),       // hide discs 2+ of multi-disc PS1 sets by default (POPSLoader parity)
    intSetting(CONFIG_OPL_VCD_SHOW_PP_POPS, &gVcdShowPpPops, 1),             // list strict PP.<ID>.POPS.<name> one-game HDD partitions by default
#ifdef BUILTIN_KORIUM
    intSetting(CONFIG_OPL_ENABLE_BGART, &gEnableBGArt, 0), // as the original's Korium: its per-game _BG competes with cover loads
#else
    intSetting(CONFIG_OPL_ENABLE_BGART, &gEnableBGArt, 1), // fork parity; gEnableArt is 1 above, so this is live
#endif
    intSetting(CONFIG_OPL_ENABLE_SAVE_ICONS, &gEnableSaveIcons, 0),
    intSetting(CONFIG_OPL_ENABLE_ART_TAR, &gEnableArtTar, 0),
    // NO SETTLE BY DEFAULT. This is the number of INACTIVE frames the menu must see before art is
    // even asked for, and it shipped at 8 -- the slowest of the four values the UI offers {0,2,5,8}
    // -- as a placeholder pending a decision that never happened; the comment here previously said
    // as much. The effect is that art will not begin loading until navigation has fully stopped and
    // stayed stopped, which reads as covers refusing to appear while browsing.
    //
    // The reason to settle at all was to keep art reads off the bus while the user is moving, but
    // that concern is device-specific and is already handled where it belongs: texcache gates SIO2
    // devices (MMCE, MX4SIO) on real idleness because their reads contend with pad polling. Making
    // every device wait for the worst device's constraint is not a trade worth a default.
    intSetting(CONFIG_OPL_ART_DELAY, &gArtDelay, 0),
    intSetting(CONFIG_OPL_FOLDER_NAV, &gEnableFolderNav, 0),
    colorSetting(CONFIG_OPL_PLAS_BLEND_COLOR, gDefaultPlasBlendColor, {RIPTOPL_PLASMA_BLEND_R, RIPTOPL_PLASMA_BLEND_G, RIPTOPL_PLASMA_BLEND_B}),
    intSetting(CONFIG_OPL_COVERFLOW_COUNT, &gCoverflowCount),
    intSetting(CONFIG_OPL_COVERFLOW_SCALE, &gCoverflowCenterScale),
    intSetting(CONFIG_OPL_COVERFLOW_ANIM, &gCoverflowAnimSpeed),
    intSetting(CONFIG_OPL_COVERFLOW_DIM, &gCoverflowDimCovers),
    intSetting(CONFIG_OPL_GRID_LAYOUT, &gGridLayout),
    intSetting(CONFIG_OPL_ENABLE_ILINK, &gEnableILK, 0),
    intSetting(CONFIG_OPL_ENABLE_MX4SIO, &gEnableMX4SIO, 0),
    intSetting(CONFIG_OPL_ENABLE_PCCF, &gEnablePCCF, 0),
    intSetting(CONFIG_OPL_ENABLE_DVR, &gEnableDVR, 0),
    intSetting(CONFIG_OPL_ENABLE_BDMHDD, &gEnableBdmHDD, 0),
    intSetting(CONFIG_OPL_NET_BOOT_PROTOCOL, &gNetBootProtocol, NET_BOOT_UDPBD), // default transport when network boot is enabled (back-compat)
    // Audio and menu rumble start off, like the original (tester feedback 10-06: "disable the music and
    // squeak"; rumble #1070). Saved configs keep their own choice; the Audio and Controller pages turn each on.
    intSetting(CONFIG_OPL_SFX, &gEnableSFX, 0),
    intSetting(CONFIG_OPL_RUMBLE, &gEnableRumble, 0),
    intSetting(CONFIG_OPL_BOOT_SND, &gEnableBootSND, 0),
    intSetting(CONFIG_OPL_BGM, &gEnableBGM, 0), // inert without a bgm.ogg on the card
    intSetting(CONFIG_OPL_SFX_VOLUME, &gSFXVolume, 90),
    intSetting(CONFIG_OPL_BOOT_SND_VOLUME, &gBootSndVolume, 90),
    intSetting(CONFIG_OPL_BGM_VOLUME, &gBGMVolume, 90),
    strSetting(CONFIG_OPL_DEFAULT_BGM_PATH, gDefaultBGMPath),
    intSetting(CONFIG_OPL_XSENSITIVITY, &gXSensitivity, 1),
    intSetting(CONFIG_OPL_YSENSITIVITY, &gYSensitivity, 1),
};

} // namespace

void settingsApplyDefaults(void)
{
    for (const Setting &s : kSettings) {
        if (!s.hasDefault)
            continue;
        switch (s.kind) {
            case Setting::Int:
                *static_cast<int *>(s.var) = s.intDefault;
                break;
            case Setting::Str:
                snprintf(static_cast<char *>(s.var), s.size, "%s", s.strDefault);
                break;
            case Setting::Color:
                for (int i = 0; i < 3; i++)
                    static_cast<unsigned char *>(s.var)[i] = s.colorDefault[i];
                break;
        }
    }
}

void settingsLoad(config_set_t *cfg)
{
    for (const Setting &s : kSettings) {
        if (!s.loadPlain)
            continue;
        switch (s.kind) {
            case Setting::Int:
                configGetInt(cfg, s.key, static_cast<int *>(s.var));
                break;
            case Setting::Str:
                configGetStrCopy(cfg, s.key, static_cast<char *>(s.var), s.size);
                break;
            case Setting::Color:
                configGetColor(cfg, s.key, static_cast<unsigned char *>(s.var));
                break;
        }
    }
}

void settingsSave(config_set_t *cfg)
{
    for (const Setting &s : kSettings) {
        switch (s.kind) {
            case Setting::Int:
                configSetInt(cfg, s.key, *static_cast<int *>(s.var));
                break;
            case Setting::Str:
                configSetStr(cfg, s.key, static_cast<char *>(s.var));
                break;
            case Setting::Color:
                configSetColor(cfg, s.key, static_cast<unsigned char *>(s.var));
                break;
        }
    }
}

const char *settingsKeyAt(unsigned index)
{
    return index < sizeof(kSettings) / sizeof(kSettings[0]) ? kSettings[index].key : nullptr;
}

// The screen position for each video mode, so 1080i and 480i each keep their own centring (RIPPS2 build 124,
// akilluminati47). A mode never set takes the shared xoff/yoff/overscan, which are still written as the mode in
// use's, so an older build reads what it always did.
#define SCREEN_POS_MODES 17
static int gPosX[SCREEN_POS_MODES], gPosY[SCREEN_POS_MODES], gPosOver[SCREEN_POS_MODES];
static unsigned char gPosSet[SCREEN_POS_MODES];

void screenPosStore(int vmode)
{
    if (vmode < 0 || vmode >= SCREEN_POS_MODES)
        return;
    gPosX[vmode] = gXOff;
    gPosY[vmode] = gYOff;
    gPosOver[vmode] = gOverscan;
    gPosSet[vmode] = 1;
}

void screenPosRecall(int vmode)
{
    if (vmode < 0 || vmode >= SCREEN_POS_MODES || !gPosSet[vmode])
        return; // never set for this mode: the current values carry over, as before
    gXOff = gPosX[vmode];
    gYOff = gPosY[vmode];
    gOverscan = gPosOver[vmode];
}

// x, y and overscan are the position when the dialog opened. The mode left keeps it; the new mode brings back its
// own unless the position was moved in the same dialog, which then goes with the new mode.
void screenPosModeChanged(int oldMode, int x, int y, int overscan)
{
    int moved = gXOff != x || gYOff != y || gOverscan != overscan;

    if (oldMode == gVMode)
        return;
    if (oldMode >= 0 && oldMode < SCREEN_POS_MODES) {
        gPosX[oldMode] = x;
        gPosY[oldMode] = y;
        gPosOver[oldMode] = overscan;
        gPosSet[oldMode] = 1;
    }
    if (!moved)
        screenPosRecall(gVMode);
}

void screenPosLoad(config_set_t *configOPL)
{
    for (int m = 0; m < SCREEN_POS_MODES; m++) {
        char kx[16], ky[16], ko[20];
        snprintf(kx, sizeof(kx), "xoff_%d", m);
        snprintf(ky, sizeof(ky), "yoff_%d", m);
        snprintf(ko, sizeof(ko), "overscan_%d", m);
        gPosSet[m] = configGetInt(configOPL, kx, &gPosX[m]) && configGetInt(configOPL, ky, &gPosY[m]) &&
                     configGetInt(configOPL, ko, &gPosOver[m]);
    }
    screenPosRecall(gVMode);
}

void screenPosSave(config_set_t *configOPL)
{
    screenPosStore(gVMode); // the mode in use keeps what it shows now
    for (int m = 0; m < SCREEN_POS_MODES; m++) {
        char k[20];
        if (!gPosSet[m])
            continue;
        snprintf(k, sizeof(k), "xoff_%d", m);
        configSetInt(configOPL, k, gPosX[m]);
        snprintf(k, sizeof(k), "yoff_%d", m);
        configSetInt(configOPL, k, gPosY[m]);
        snprintf(k, sizeof(k), "overscan_%d", m);
        configSetInt(configOPL, k, gPosOver[m]);
    }
}

void screenPosReset(void)
{
    memset(gPosSet, 0, sizeof(gPosSet));
}
