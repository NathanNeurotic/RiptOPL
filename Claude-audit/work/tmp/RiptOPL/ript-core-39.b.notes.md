# ript-core-39.b notes (RiptOPL, lens defects)

Done: src/ui/guisettings.cpp 1-3499 (all read), include/guisplash.h 1-32 (all read).

Candidates:
- guisettings.cpp:775-801 | gui_smb_link_t fixed field sizes (17/32/32/32) vs globals | check sizes of gPCShareNBAddress, gPCShareName, gPCUserName, gPCPassword
- guisettings.cpp:1350,3454 | clock timeout = ms * (CLOCKS_PER_SEC/1000) may overflow 32-bit clock_t | check CLOCKS_PER_SEC / clock_t in repo, other usages
- guisettings.cpp:1439-1513 | PopsUdpfs page reads/writes MC on UI thread while sibling page uses IO worker | check vcdReadPopsUdpfsNet etc in vcdsupport.cpp
- guisettings.cpp:1206-1238 | vcdEquipBdma copies files to MC on UI thread | check vcdEquipBdma
- guisettings.cpp:2241-2245,2349 | days[month-1] with dialog month | check DATETIME_MONTH range in dialogs.cpp
- guisettings.cpp:2339-2341 | netTimeFetch values validated? | check nettime.cpp
- guisettings.cpp:1929 | gGridLayout <= GRID_LAYOUTS | check grid.h
- guisettings.cpp:946-955,2329-2337 | guiHandleDeferedIO bounded wait -> worker still writing shared statics | check guiwait.cpp
- guisettings.cpp:2059-2138 | composeInto capacity for mmceDialog | check settingshell.h
- guisettings.cpp:3029 | SourceState.mmcePrefix size | check settingshell.h
