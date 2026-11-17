# ript-core-10.b notes (lens defects)

## Done (all 8 files read in full; earlier files re-read for verification)
- include/OSDHistory.h 1-101, src/core/OSDHistory.cpp 1-298, src/core/bgm.cpp 1-928
- include/cheatconfig.h 1-22, src/core/cheatconfig.cpp 1-111, include/cheatman.h 1-95, src/core/cheatman.cpp 1-680, include/config.h 1-375

## Verified -> findings
- bgm.cpp:72 768 KB bss ring (MEASURED BUILD-EVIDENCE.md:95,249), BGM default off settings.cpp:176
- bgm.cpp:800-806 prebuffer up to 500 ms + 860-870 200 ms slices; GUI thread callers gui.cpp:332-333, guisettings.cpp:1957-1958, 2741-2742, 2766-2767; main prio 31 opl.cpp:2064
- bgm.cpp:820-842 quiesce no live_ guard; called unconditionally opl.cpp:1325,1419; IDs stale after teardown (load fail path 759-764) or 0 (bgmplayer.h:35-36)
- cheatman.h:41,67 codes[MAX_CHEATLIST] pairs vs 255-pair list; 250*4216 -> 250*2176, saves 510,000 B heap
- cheatman.cpp:261-271 trim_str leaves all-space line -> indented comment becomes cheat name; 334-335/381 long line advance drops byte 255 and reparses tail
- cheatconfig.cpp:100-110 removeKey all + re-add marks set modified even if unchanged -> guimodal.cpp:439-441 configWrite rewrites CFG each Select launch (config.cpp:447-458, 371-383, 1519)

## Dropped
- OSDHistory: history.old no O_CREAT (upstream, unknown impact), empty name (callers pass startup), mcGetInfo unchecked, strncpy name[15] (theoretical)
- bgm thread stacks 4 KB (upstream, works); gImage 4 KB bss (too small to add code)
