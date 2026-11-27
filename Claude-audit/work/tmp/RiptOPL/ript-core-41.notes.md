Unit ript-core-41 notes
Done: src/ui/menusys.cpp 1-3413 read; include/menuwatch.h 1-71 read.

Candidates:
- menusys.cpp:51-71,2395-2396 | MENU_RETIRED_NETWORK_UPDATE, MENU_SAVE_CHANGES, MENU_MMCE never appended in menuInitMainMenu -> handler dead | grep IDs repo-wide, persisted?
- menusys.cpp:1292-1295 | menuReinitMainMenu forwards to static menuInitMainMenu | callers
- menusys.cpp:284-295,530-541,322-323,559-560 | duplicate FAV refusal blocks in rename/delete | net reduction
- menusys.cpp:2557-2589,2864-2887 | menuRenderMain/Info duplicate; branches re-call getter | thmResolveItemsList semantics
- menusys.cpp:2600-2605,2640-2645,2676-2681,2616-2622,2656-2662 | grid count/index loop x3, walk tail x2
- menusys.cpp:2175-2216,2922-2984,3291-3348 + input 2325-2340,3154-3169,3363-3378 | three submenu screens duplicate count/up-down loops, bg comment x3
- menusys.cpp:2896-2918 | menuRequestInfoSize repeated 6x
- menusys.cpp:808-810 | stale NOTE: MENU_APP_CONFIG_IDLE_FRAMES now exists at 2426
- menusys.cpp:563-565 | stale comment: gameMenuLoadConfig no longer uses _menuLoadConfig | check waiter path reach
- menuwatch.h | enum values usage
- exported fns: menuHasRegisteredItems, menuResetPageStarts, menuSetSelectedItem, menuGameRequiresParentalLock, menuIsParentalLockPasswordSet, menuInvalidateConfig, hiddenRow, menuSetParentalLockCheckState return | callers
