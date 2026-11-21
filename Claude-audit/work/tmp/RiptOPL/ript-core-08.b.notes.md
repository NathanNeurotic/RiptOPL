# ript-core-08.b notes (lens defects)

Done: src/app/opl.cpp lines 1-2134 (read fully, attempt 1; re-read fully in attempt 2: windows 1-700, 700-1399, 1400-2134).

Phase: candidates checked (attempt 2); writing findings file.

Kept:
- opl.cpp:1668,1686,1699-1708 | startPads called twice at boot, first result discarded; pad.cpp start() re-opens ports and re-runs initialize (waitReady 25x1ms, up to 6 per pad) | KEEP rank 2
- opl.cpp:1269-1277,1328,1422,1962 + ioman.cpp:380-386 + util.cpp:941-950 + lwnbdctl.cpp:78,150 | drain polls once per delay(1) NOP spin (~0.25 s); lwnbd bound 1000 ticks ~4 min | KEEP rank 2

Dropped (checked):
- gDefaultDevice clamped settingsio.cpp:2494-2501 (0..FAV_MODE < MODE_COUNT)
- item_list_t.mode is short int (iosupport.h:120): cast fine
- folderCrumb stale: text_id wins (grid.cpp:482, psbbnflow.cpp:190) and menusys.cpp:1899-1912 resets on leave
- gVMode clamped in Renderer::setMode (renderman.cpp:235)
- autolaunch fallback: IoQueue::start waits old worker and resets blocked_ (ioman.cpp:187-202); bdmInitSemaphore leaks 1 sema only (no consequence)
- hiddenRow/recentRankForRow per row: linear in small lists (RECENT_MAX_ITEMS 10)
- loadFavourites: only flags + queues, coalesces via one-shot
- guiCheckNotifications: sets flags only
- current read unlocked on IO thread: rows freed only on IO thread
- udpfsShowPendingNotice: flag check
- drain-timeout races: Gui::shutdown makes late lock calls no-ops; speculative
