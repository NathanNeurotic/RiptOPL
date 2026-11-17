unit ript-core-39 notes
done: src/ui/guisettings.cpp 1-2399

candidates:
- guisettings.cpp:65-78 | 5 forward decls each with same "was declared in header" comment | trivial clutter
- guisettings.cpp:33,49 | sound.h included twice | trivial
- guisettings.cpp:86-121 | guiDeviceTypeToIoMode / guiIoModeToDeviceType | grep callers
- guisettings.cpp:138-144,807-813,1083-1097,1705-1721 | 4 identical keyboard-edit handlers + macro for neutrino args | could all use the macro / one helper
- guisettings.cpp:1322,1344,1363,1367 | guiPopsNetReadPending written never read? | grep
- guisettings.cpp:2117-2129 | duplicated capacity check + LOG in composeInto | small
- guisettings.cpp:2051-2057 | compose forwards to composeInto | check callers
- guisettings.cpp:1103 | guiBdmaUpdater fallback diaBdmaConfig | check whether activeDialog can be null
- guisettings.cpp:216 guiNetProtocolAccess single caller
- guisettings.cpp:580 guiRefreshGameViews vs 589 guiRefreshCfgTitles | check callers
- guisettings.cpp:1868-1879 guiGameIdModeFromGlobals usage
- guisettings.cpp:1936 guiDrawBGSettings exported? callers
