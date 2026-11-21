# ript-core-31.b notes (lens defects)

Done: src/sources/rawatch.cpp 1-318; include/retrogem.h 1-30; src/sources/retrogem.cpp 1-675; include/supportbase.h 1-185 (all read in full)

Checked and dropped:
- rawatch setWatchList unaligned: wl_ follows rx_[2048] aligned(64) in ranet.h:89-90 -> aligned. Dropped.
- rawatch stale list for another game: every OPL-core leg calls sbLoadWatchList; UDPFS/netblock/CF nulled at system.cpp:1565. Dropped.
- rawatch prefix compare 15 chars: keys are <=15 (raVcdWatchKey 15, startup 12). Dropped.
- rawatch takeNodes alias/backward copy: correct. Dropped.
- raLaunchNote: diag only, 2 calls per launch. Dropped.
- supportbase.h structs: games memset before fill; no defect. neutrino_args_t static/stack in guisettings, fine. Dropped.
- retrogem stack ~13 KB: IO worker 96 KB, main thread fine. Dropped.

Finding:
- src/sources/retrogem.cpp:558-571 | VCD: offset-0 raw+cooked fallbacks cannot match (1 MiB header), rerun per retry (vcdsupport.cpp:264,436-470) and at launch (3027-3036) | done
