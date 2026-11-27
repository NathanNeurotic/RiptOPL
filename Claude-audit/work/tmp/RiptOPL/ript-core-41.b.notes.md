# ript-core-41.b notes (lens defects)

Done: src/ui/menusys.cpp 1-3413 read fully. include/menuwatch.h pending.

Candidates:
- menusys.cpp:774-799 | _menuSaveConfig: 2nd WaitSema does not recheck itemConfig != NULL; configWrite(itemConfig) device I/O under menuSemaId (UI freeze?); line 798 itemConfig after release | check guiHandleDeferedIO timeout behaviour (guiwait.cpp), configWrite NULL handling, who frees itemConfig on GUI side
- menusys.cpp:659-673 | #Size worker frees itemConfig and replaces it while game menu / launch holds pointer from menuLoadConfig | check gameMenuLoadConfig callers, guiGameLoadConfig storing ptr, menuTakeLaunchConfig callers
- menusys.cpp:1794 | lastPlayed sort: a - b (oldest first?) overflow? | check lastPlayed type/meaning
- menusys.cpp:396-401,1328,1370 | menuHiddenState reads device list (gs->game/name) on GUI thread without menuRowBeginEdit | check whether guiLock held when menuInitGameMenu called
- menusys.cpp:1145-1151 | page.count vs ACH_PAGE_SIZE index achIconIds[i] | check achievement_page_t
- menusys.cpp:3056 | strcpy extension ".VCD" size | check base_game_info_t
- menusys.cpp:2755 | CLOCKS_PER_SEC/1000 | check ps2 value
- menusys.cpp:1454-1475 | menuEnd deletes semas while io worker may run (documented) | probably skip
- menusys.cpp:451 | malloc plan each delete fine
