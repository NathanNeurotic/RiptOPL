ript-core-36 notes
DONE (all read): dialogs.cpp 1-2414, drivestatus.h, drivestatus.cpp, gui.h, gui.cpp, guiboot.h, guiboot.cpp, guifilemgr.h
Candidates:
- dialogs.cpp:14-42 | 29 diaStr_* all marked unused but all used in DIA_STRING rows | cosmetic, probably skip
- dialogs.cpp:654-667,835-843,1232-1234 | legacy buttons VCD_BDMA/LIST/NET, MMCE_COMM/PATH, LAUNCH_NEUTRINO_DEFAULTS "settings shell skips" | check standalone callers
- dialogs.cpp:290-332 vs 375-417 | POPS rows duplicated | check composition mechanism
- dialogs.cpp:2263-2268 | stale NOTE comment | clutter
- dialogs.cpp:283 UDPFS dialog has POPS_SMB_IP rows hint -1 | check
- gui.cpp:19-53 | many includes with stale comments for code no longer in gui.cpp (httpTestServer, bdmIsUDPBDLoaded, tarInvalidate, appGetObject, libView, gFAVStartMode) | grep usage in gui.cpp; same block in guisettings/guigameid/guimodal/guiwait
- gui.h | prototypes to check for definitions/callers: guiShowColorsConfig, guiDrawBGSettings, guiShowPsEmulationSettings, guiSetFrameHook, guiPromptRebootIop, guiWarning, guiRenderGreetingScreen, guiFadeCurrentScreenToBlack, guiGameShowRemoveSettings, guiManageCheats, guiIsActive, guiReloadScreenExtents, guiInactiveFrames, guiFrameId, GUI_SCREEN_*, gui_callback_t; comment refs guiShowRANotices
- gui.cpp:489 guiSetFrameHook / frameHook_ | check callers
- guiboot BootStatus active_ redundant with text_[0] | trivial
- drivestatus.h:12 DSTAT_LINE_LAST, guifilemgr.h:16 FM_ROW_LAST | check use
- drivestatus.cpp dsSizeText | check for other size formatters
