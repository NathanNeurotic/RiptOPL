# ript-core-18.b notes (lens defects)

## Done
- src/gfx/themeelems.cpp 1-2347 read fully (attempt 1 and re-read in attempt 2)
- include/themes.h 1-379 read fully (attempt 1 and re-read in attempt 2)
- Verification pass done (attempt 2)

## Verified -> findings
- F1 themeelems.cpp:633-649,837-855,871-891 | findDuplicate shares first COV cache ignoring requested count; synthesized presenter after GameImage pattern=COV gets 2 slots | verified themeviews.cpp:485-528, themes.cpp:700-702,1480-1491, coverflow.cpp:356, grid.cpp:663-671, texcache.cpp:396-407
- F2 unchecked malloc/calloc at theme-element init (211,226,275,703,723,767,774,810,1608,2123); coverflow.cpp:360 / grid.cpp:674 NULL guards unreachable | rank 1
- F3 per-frame texLookupInternalTexId (1506-1515,1534-1540,1917-1925) | 7 AttributeImage on built-in info page (conf_theme_OPL.cfg:238-286) | rank 1

## Dropped
- hintEntryWidth div by Height: thmGetTexture returns NULL when Mem NULL (themes.cpp:938-951)
- drawBDMIndex / getGameCount userdata NULL: only menuAppendItem caller is initMenuForListSupport (opl.cpp:430) with support set
- 1 << thmHintToken: always 0..6 (themes.cpp:1280-1298)
- currentValue into config: setStr overwrites in place; removeKey only on $-compat keys; uid unique counter (config.cpp:696)
- language format string _STR_FILE_COUNT: all lng use %i
- ceil(): libm ceil pulled by libvorbis anyway (archive-members:464); no size change
- drawSaveIcon: ps1SaveDir is string building only; stack 768 B fine
- decorator lifetime: nulled in validateItemsList before configFree (themes.cpp:148,1814)
- wallpaper read() negative handled (tests/host/test_wallpaper.cpp:62)
- MENU_ITEM_HEIGHT >= 2 (opl.h:540)
- bgPicture NULL: drawStaticImage only installed with defaultTexture or wallpaper
