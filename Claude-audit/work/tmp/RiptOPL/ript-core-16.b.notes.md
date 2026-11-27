# ript-core-16.b notes (lens defects)

## Done (all assigned lines read)
- src/gfx/renderman.cpp 1-965, include/saveicon.h 1-100, src/gfx/saveicon.cpp 1-1315,
  include/texcache.h 1-280, src/gfx/texcache.cpp 1-634, include/textshape.h 1-28

## Verified -> findings
- F1 texcache.cpp:396-407,467,24-31 | LRU eviction may free Mem of slot drawn last frame; HIRES replays last frame (repo's own comments fntsys.cpp:120-123, themes.cpp:1836-1839, renderman.cpp:188-189) | rank 2 INFERRED
- F2 saveicon.cpp:631-636,1031-1036,1238-1239 + renderman.cpp:445-451 | PS1 icon colour-0 texels meant transparent, drawQuad disables blend+atest for CT16 -> black | rank 2 INFERRED
- F3 texcache.h:103-107 + artload.cpp:370,386 vs texcache.cpp:520 | non-atomic decrement on art thread (prio 64) preemptible by GUI increment -> lost update on UAF guard | rank 1
- F4 renderman.cpp:784-787,789-803 + settings.cpp:88,301-302 | gOverscan unclamped from cfg (UI 0..100 dialogs.cpp:1087) -> displayWidth_ 0 -> div by zero | rank 1
- F5 saveicon.cpp:812,1165,1282-1283,1309 | siCol redundant per-vertex copy (up to 48 KB) | rank 1

## Dropped
- setMode on IO worker: only _loadConfig caller is init() on main thread (opl.cpp:1719); comment stale, no race
- setupQuad NULL txt: no NULL caller (only rmDrawQuad caller fntsys.cpp:704)
- pfs1: unmount in siLoadAction: same scratch-slot convention as hddsupport, IO worker serialised
- clock units: repo states clock() is microseconds, clock_t unsigned long (guisplash.cpp:179)
- siSelStartup[16]: startup ids are 11 chars
