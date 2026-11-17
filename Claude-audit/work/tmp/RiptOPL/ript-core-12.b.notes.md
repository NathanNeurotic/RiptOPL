# ript-core-12.b notes (lens defects)

## Done (all 9 files read in full)
- src/core/pad.cpp 1-1322, include/riptlite.h, src/core/riptlite.cpp, include/settings.h, src/core/settings.cpp, src/core/sfx.cpp, include/sound.h, src/core/sound.cpp, include/vmc_groups.h

## Verified -> findings
- F1 pad.cpp:466-482,673-676 | ds34 get_status sync SIF RPC per pad per frame; port 1 never enabled (system.cpp:431 mask 1; ds34usb.c:940-959, ds34bt.c:1681-1685) | cat 5 rank 2
- F2 pad.cpp:484-501 | fault-run counters shared across pads; empty port counted (initialize always returns 1) -> doubled runs, false diagLog | cat 6 rank 2
- F3 opl.cpp:1686 + 1699-1708 | start() runs twice at boot, reopens ports, initialize twice | cat 5 rank 1 INFERRED cost
- F4 sfx.cpp:175-184 + guisplash.cpp:183-206 | boot.adp header sample count trusted -> splash wait hours | cat 6 rank 2 INFERRED

## Dropped
- keyPressed bounds (callers pass constants), rumble cross-thread (benign), screenPos (20 == NUM_RM_VMODES), riptlite 16-byte values (ints), tarThemeRead free (memalign'd), audsrv locking (SDK not readable), disable_debug key (upstream naming)
