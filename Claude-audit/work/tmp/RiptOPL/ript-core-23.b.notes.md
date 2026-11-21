# ript-core-23.b notes (defects lens)

Done: util.cpp 1-991, zso.c 1-95, achievements.h 1-51, achievements.cpp 1-797, appscan.cpp 1-248, appsupport.h 1-39 (ALL READ)

Verified candidates (to write):
- util.cpp:615,631-634 | readFileBuffer empty line w/ allocResult returns ptr into buffer; lang.cpp:163-188 stores it, closeFileBuffer frees buffer, lngFreeFromFile (lang.cpp:42-53, lngEnd in deinitEx opl.cpp:1363) frees it -> invalid free. Also malloc unchecked. rank 3
- util.cpp:941-950 | delay() NOP spin ~0.25s/tick (opl.cpp:1269-1275, autolaunch.cpp:204, settingsio.cpp:1419); main thr prio31 starves IO(32)/BGM IO(31)/art(72); lwnbdctl.cpp:197-201,228-237 assumes 100x delay(50)=~5s -> ~20min. Fix DelayThread. rank 3
- achievements.cpp:144-146,356,383-385 | xeraList 1024*232=237,568 B held for session; realloc to count. rank 3
- util.cpp:857-936 | sysDeleteFolder leaks list when subfolder fails (909); malloc unchecked 873-874. caller appsupport.cpp:930. rank 1
- util.cpp:799-806 + zso.c:14-23 | HDD reads 1x512B per fileXio devctl; rahash.cpp:340-344 uses 16 sectors/call. rank 1 INFERRED limit
- achievements.cpp:496-500,656-660 + menusys.cpp:1224-1229 | Select on image page (non-READY) never calls achievementsForget -> cached library + latched https unreachable. rank 1

Dropped: CheckPS2Logo 24KB stack (main thread ok); raNetHttpGet length semantics OK (body len, NUL-terminated ranet.cpp:1125-1133); raHttpsGet total len OK; hddReadSectors IOBuffer arg clobber not reachable (only +80 read); appscan truncation harmless; checkMC/checkMCSaveIcons reads minor; InitConsoleRegionData uninit romver theoretical.
