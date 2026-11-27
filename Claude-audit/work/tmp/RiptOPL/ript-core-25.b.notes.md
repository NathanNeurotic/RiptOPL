# ript-core-25.b notes (lens defects)

## Done (all assigned ranges read in full; re-read in attempt 2)
- include/bdmsupport.h 1-228
- src/sources/bdmsupport.cpp 1-2408
- include/cuesupport.h 1-237

## Verified -> findings
- bdmsupport.cpp:1411-1485 boot settles (iLink 3 s loop, MX4SIO/USB 600 ms) on IO worker ahead of page updates and deferredInit (GUI_INIT_DONE gates splash) | FIFO checked ioman.cpp 259-331; opl.cpp 538-542, 1757-1759, 2116; guisplash 191-206; doc ILINK-DISCOVERY-PARITY
- bdmsupport.cpp:1844-1861 redundant stat/ul.cfg I/O (result always 1; bdmUpdateDeviceData returns -1/0/1 only, bdmdevices.cpp 413/449/455)
- bdmsupport.cpp:2055-2066 mkdir repeated per config() when CFG exists (menusys.cpp 587-606 calls config on every settle)

## Dropped
- itemList->mode as slot: BDM_MODE = 0 (iosupport.h:11)
- STOP_ALL per slot: bdmfs devctl STOP_ALL stops a single volume (fs_driver.c 1024-1030)
- dev9 refcount: balanced (system.cpp 228-285)
- init leak: priv reallocated after cleanUp (bdmdevices.cpp 80-94), BDM always force_reinit 0
- bdmFindPartition: target 64 B, prefix 32 B -> fits
- bdmHDDIsPresent double wait: upstream logic, narrow trigger
- ps1SaveDir partition: callers pass arrays
- bdmReadDriverName terminator: all callers pre-zero
- cue CUE_MAX_ITEMS: cueScanDir grows by realloc
- bdmLoadCoreModules repeats: sysLoadModuleBuffer dedupes
- bdmIsReadOnlyPath digits: theoretical
- debug SifSendCmd alignment: cannot verify SDK, debug only
