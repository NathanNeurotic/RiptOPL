// GENERATED from the setDefaults() that preceded src/core/settings.cpp: one check per table row. Audio, menu
// rumble and widescreen were since changed to the original's fresh defaults (off / 4:3).
CHECK(gScrollSpeed == (1));
// gDefaultBgColor: no default in setDefaults (initialiser / another routine owns it)
// gDefaultTextColor: no default in setDefaults (initialiser / another routine owns it)
// gDefaultUITextColor: no default in setDefaults (initialiser / another routine owns it)
// gDefaultTitleColor: no default in setDefaults (initialiser / another routine owns it)
// gDefaultSelTextColor: no default in setDefaults (initialiser / another routine owns it)
CHECK(gEnableNotifications == (1));
#ifdef RETROACHIEVEMENTS
CHECK(gRATelemetry == (0));
#endif
#ifdef RETROACHIEVEMENTS
CHECK(gRABadges == (1));
CHECK(gRABadgeSuffix == (0));
CHECK(gRAMode == (RA_MODE_XERABORA));
CHECK(gRAExperimentalCards == (0));
#endif
CHECK(gEnableArt == (1));
CHECK(gEnableDiscArt == (1));
CHECK(gEnableLogoArt == (1));
CHECK(gWideScreen == (0));
CHECK(gFontSize == (FONT_SIZE_DEFAULT));
CHECK(gGlyphSet == (0)); // GLYPH_SET_THEME
CHECK(gVMode == (0));
CHECK(gXOff == (0));
CHECK(gYOff == (0));
CHECK(gOverscan == (0));
CHECK(gEnableDebug == (0));
CHECK(gPS2Logo == (1));
CHECK(gHDDGameListCache == (0));
CHECK(gExitPath[0] == '\0');
CHECK(gCustomSettingsPath[0] == '\0');
CHECK(gAutosort == (1));
CHECK(gAutoRefresh == (0));
CHECK(gDefaultDevice == (APP_MODE));
CHECK(gAutoCreateVmc == (0));
CHECK(gIgrMenu == (0));
CHECK(gEnableWrite == (1));
CHECK(gHDDSpindown == (20));
CHECK(gBDMPrefix[0] == '\0');
CHECK(gETHPrefix[0] == '\0');
CHECK(gRememberLastPlayed == (0));
CHECK(gAutoStartLastPlayed == (9));
CHECK(gBDMStartMode == (START_MODE_DISABLED));
CHECK(gHDDStartMode == (START_MODE_DISABLED));
CHECK(gETHStartMode == (START_MODE_DISABLED));
CHECK(gAPPStartMode == (START_MODE_DISABLED));
CHECK(gMMCEPrefix[0] == '\0');
CHECK(gMMCEStartMode == (START_MODE_DISABLED));
CHECK(gMMCESlot == (2));
CHECK(gMMCEIGRSlot == (3));
CHECK(gMMCEEnableGameID == (0));
CHECK(gMMCEAckWaitCycles == (5));
CHECK(gMMCEUseAlarms == (1));
CHECK(bdmCacheSize == (16));
CHECK(hddCacheSize == (8));
CHECK(smbCacheSize == (16));
CHECK(gEnableUSB == (0));
CHECK(gFAVStartMode == (START_MODE_DISABLED));
CHECK(gAllGames == (0));
CHECK(gShowHidden == (0));
CHECK(gApplyGameID == (0));
CHECK(gDefaultGameView == (GAME_VIEW_BOTH));
CHECK(gAppsDisplay == (APPS_DISPLAY_MIXED));
CHECK(gCfgTitlesInLists == (0));
CHECK(gPopstarterPath[0] == '\0');
CHECK(gBdmaSource == (VCD_BDMA_SRC_USB));
CHECK(gBdmaApplyOnLaunch == (1));
CHECK(gVcdUsbBdmaMode == (VCD_USB_BDMA_ASK));
CHECK(gVcdHideGameId == (1));
CHECK(gVcdFirstDiscOnly == (1));
CHECK(gVcdShowPpPops == (1));
CHECK(gEnableBGArt == (1));
CHECK(gEnableSaveIcons == (0));
CHECK(gEnableArtTar == (0));
CHECK(gArtDelay == (0));
CHECK(gEnableFolderNav == (0));
CHECK(gDefaultPlasBlendColor[0] == RIPTOPL_PLASMA_BLEND_R && gDefaultPlasBlendColor[1] == RIPTOPL_PLASMA_BLEND_G && gDefaultPlasBlendColor[2] == RIPTOPL_PLASMA_BLEND_B);
// gCoverflowCount: no default in setDefaults (initialiser / another routine owns it)
// gCoverflowCenterScale: no default in setDefaults (initialiser / another routine owns it)
// gCoverflowAnimSpeed: no default in setDefaults (initialiser / another routine owns it)
// gCoverflowDimCovers: no default in setDefaults (initialiser / another routine owns it)
// gGridLayout: no default in setDefaults (initialiser owns it: 0, the theme's layout)
CHECK(gEnableILK == (0));
CHECK(gEnableMX4SIO == (0));
CHECK(gEnablePCCF == (0));
CHECK(gEnableDVR == (0));
CHECK(gEnableBdmHDD == (0));
CHECK(gNetBootProtocol == (NET_BOOT_UDPBD));
CHECK(gEnableSFX == (0));
CHECK(gEnableRumble == (0));
CHECK(gEnableBootSND == (0));
CHECK(gEnableBGM == (0));
CHECK(gSFXVolume == (90));
CHECK(gBootSndVolume == (90));
CHECK(gBGMVolume == (90));
CHECK(gDefaultBGMPath[0] == '\0');
CHECK(gXSensitivity == (1));
CHECK(gYSensitivity == (1));
