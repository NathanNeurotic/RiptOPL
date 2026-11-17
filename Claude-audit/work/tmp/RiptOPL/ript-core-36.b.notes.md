# ript-core-36.b notes (lens defects)

Done: src/ui/dialogs.cpp 1-2414 (read_full)

Candidates:
- src/ui/dialogs.cpp:2230 + :478 | CFG_DEFAULT_BGM_PATH / CFG_CUSTOMCFGPATH UI_STRING rows hold 31 chars; guisettings.cpp:1954 (audio updater on any change) and :2398 (General OK) read back into gDefaultBGMPath[128] / gCustomSettingsPath[64] -> silent truncation saved. Other long paths (exit, neutrino, popstarter, http base, nfs export) have preserve logic. | confirmed callers read.
- data tables only otherwise; BDMCACHE init def!=current irrelevant (diaSetInt at guisettings 1680).
