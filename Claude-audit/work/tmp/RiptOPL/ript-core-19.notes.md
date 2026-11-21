# ript-core-19 notes

## Done
- all 5 files read in full (themes.cpp re-read in this attempt to verify)

## Verified candidates -> findings
1 families x12 hand-listed (themes.cpp + themeelems.cpp:871-882)
2 thmLoad zero stores after memset; 1410 dead store
3 ThemeCatalog single instance + forwarders; count() unused; themecatalog.h include unused in coverflow/themeelems
4 uidraw guiDrawIconAndText duplicate
5 copy-paste: validateBackgroundElems, isDecoratorCoverImage, addGUIElem slot branches
6 1433-1440 duplicates thmReadViewTemplate
7 thmLoadResource psm unused
8 unused includes themes.cpp
9 impossible NULL guards
10 stale comments
11 thmBlockPresent vs thmViewBlockPresent
## Dropped
- themeviews exports: all used; LOAD0_ICON=0 so loadingIconCount OK
## Status: writing findings JSON
