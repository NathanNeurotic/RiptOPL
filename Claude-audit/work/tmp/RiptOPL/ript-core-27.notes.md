# ript-core-27 notes (lens structure)

## Done
- src/sources/favsupport.cpp 1-1833 (read fully, re-read fully in attempt 2)
- include/folderbrowse.h 1-46, src/sources/folderbrowse.cpp 1-130
- include/gamemanage.h 1-49, src/sources/gamemanage.cpp 1-347
- include/gamesource.h 1-112, src/sources/gamesource.cpp 1-67
- include/hddsupport.h 1-175
ALL FILES READ. Next: verification greps, then findings JSON.

## Candidates
- favsupport.cpp:250-251,267-268,322-323 | editable != NULL checks dead (231 derefs unconditionally; both callers 387,520 pass non-NULL) | verified by read
- favsupport.cpp:971-977 | selectedItemFlags fallback unreachable (owner non-NULL: ingest 835 skips NULL, gatherAdd owner=src) | verified by read
- favsupport.cpp:997-1004 | FavSupport::sourceItem | grep callers
- favsupport.cpp:528-545 | favPrepareSave repeats favReadFile 395-423 decision; callers addItem/removeByIdAndText/recentRecord just ran favReadFile | extra primary read per save
- favsupport.cpp:919-954 vs 1321-1357 | artMode / image same match logic | count lines
- favsupport.cpp forwarders 1216,1409,1445,1452,1457,1472,1477,1778,1783,1788 | check favsupport.h, callers
- favsupport.cpp:1393 | FavSource::deleteGame same as base default gamesource.cpp:25; FAV lacks OMIT_DELETE (gamemanage.cpp:313 special-cases FAV_MODE)
- favsupport.cpp:40 | gFAVStartMode | grep
- comments: favsupport.cpp:574-576 misplaced, :31 merged, :866 opl.c; folderbrowse.cpp:9 opl.c; folderbrowse.h:16 vcdsupport.c; gamesource.h:64-66 misplaced
- folderbrowse.cpp:23-51 forwarders; guard repeated 55,62,69,95 (range check redundant w/ folderModeSupported) | check folderbrowseint.h, callers
- gamemanage.h:36-41 fields parts/cfg/art/cht/shared/what readers | grep menusys
- gamemanage.cpp:160-166 hiddenSave open-before-unlink | minor
- hddsupport.h | APA_IOCTL2_GETHEADER, all prototypes/enum values usage | grep
- hddsupport.h:82-86,128-131 history comments | clutter
