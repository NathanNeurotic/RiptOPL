# ript-core-28.b notes (lens defects)

## Done
- src/sources/hddsupport.cpp 1-3383: read fully (second attempt re-read all of it)
- include/httpsupport.h 1-91: read fully (no candidates)

## Candidates checked (second attempt)
- 110-119 sysLoadModuleBuffer != 0: DROPPED (returns 0 or <0, system.cpp:139-187)
- 1328-1331 commit redundant mount: DROPPED for this lens; oplHomePending is never set >= 0 anywhere (whole selector path dead, other lens)
- 2079-2107 pfs1 without ioBlockOps from GUI launch path: DROPPED (narrow race, no proven overlap)
- 2364/2794 hddReadSectors unchecked: DROPPED (no realistic consequence)
- 2389/2720 last_played: KEEP as rank 1 (saved before abort points; second save is no-op since setStr only marks modified on change)
- 2644-2716 VMC bounds: DROPPED (number_data < 11 -> <=9 copied into blocks[10]; nparts<=5)
- 2786-2787 strcpy from games.bin data: KEEP rank 1 (untrusted file record, no termination/size check)
- 2841-2897 hddVcdResolveSize: DROPPED (per-thread gate sbConfigStatSizeEnabled)
- 3146-3198 cache compare by partition name only: KEEP rank 2 (stale start_sector at next boot; cache opt-in default 0)
- 3316-3354 ps1SaveDir no lock: DROPPED (draw runs under the frame-long GUI lock, gui.cpp:114-120)
- 3244 hddIopWasReset: DROPPED (deinitAllSupport cleanUp clears gHDDPrefix before reset)
- 592-704 settle ordering: DROPPED (no concurrent caller shown; all HDD init on IO worker)
- 236/268 DVR 45 KB array published regardless of count: KEEP rank 1
- 2319-2326 / 3367-3372 cold buildVcdGameList on GUI thread: KEEP rank 2
