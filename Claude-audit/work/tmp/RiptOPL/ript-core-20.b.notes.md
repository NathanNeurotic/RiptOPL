Unit ript-core-20.b (RiptOPL, defects lens)

Done: all 25 files read in full (debug.h/.cpp, diaglog.h/.cpp, elfldr_noreset.cpp, hdd.h/.cpp, httpcatalog.h/.cpp, httpclient.cpp, ioman.h/.cpp, ioprp.h/.cpp, launchdiag.h/.cpp, lz4.c, md5.h/.c, nbns.h/.cpp, new.cpp, ps2cnf.h/.cpp, ratls.h)

Candidates:
- src/platform/ioman.cpp:380-386 | blockOps polls with delay(1) (~0.25 s NOP spin, util.cpp:941-949) at prio 90; drain noticed only at end of a spin; ticks count idle CPU not wall time; lwnbdctl.cpp:78 LWNBD_IO_DRAIN_TICKS=1000 ~ 4 min | CHECKED (opl.cpp:1269-1277 confirms units; lwnbdctl:150 caller)
- src/platform/ps2cnf.cpp:54-65 | CNFGetKey no terminator after 64 chars; caller buffer supportbase.cpp:116 is exactly 64 -> over-read in GetStartupExecName 127-144 | CHECKED (caller supportbase.cpp:450 iso:/SYSTEM.CNF;1 on ISO scan)
- src/platform/httpclient.cpp:9-12,95-155 | static RPC buffers + single IOP stream shared by IO-worker catalog fetch (httpsupport.cpp:596) and GUI-thread launch probe (httpsupport.cpp:794) | CHECKED reachability (menuitems.cpp:176-192 launch without IO block; IO_MENU_UPDATE_DEFFERED on worker opl.cpp:1691); consequence INFERRED
Dropped: hdd.cpp number_data overflow (max=11 only caller, hddsupport.h:67); IOPRP buffer sizing ok (system.cpp:833); httpcatalog bounds ok; diaglog bounds ok; nbns ok; ioman tail_ dangling after exit (put blocked by deinit before ioEnd).
