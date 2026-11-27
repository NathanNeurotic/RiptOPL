# ript-core-27.b notes (lens defects)

Done (all read full): src/sources/favsupport.cpp 1-1833, include/folderbrowse.h, src/sources/folderbrowse.cpp, include/gamemanage.h, src/sources/gamemanage.cpp, include/gamesource.h, src/sources/gamesource.cpp, include/hddsupport.h.

Final findings (verified):
1. favsupport.cpp:616-619,1022-1029,1065-1076,1765-1776 | PS1 favs name-addressed for resolve/star but add/remove need exact id; PS1 lists qsorted -> false "Removed", dup records. VERIFIED.
2. favsupport.cpp:1765-1776,1079-1099,1491-1507 + menuitems.cpp:416-424,92-95 | R3 pointers held across card I/O, IO worker (prio30) rebuild frees them. gui.cpp:368-374 dispatch after unlock. INFERRED timing.
3. favsupport.cpp:528-570,1486-1521,1065-1076 | unchanged-content rewrites (recent replay, no-match remove). 
4. gamemanage.cpp:33-41 | hiddenLists 40,920 B .bss MEASURED (BUILD-EVIDENCE.md:113).
5. favsupport.cpp:226-234,410-422,537-541 | torn first save / zero-length primary => permanent save refusal.
Dropped: art scan perf (enqueue-time only), wOPL 138KB transient calloc, uOPL shared-file format (unverifiable), folderbrowse/gamesource/hddsupport.h nothing in lens.
