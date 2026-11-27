# ript-core-18 notes (lens structure)

## Done
- include/themes.h 1-379: read fully (twice)
- src/gfx/themeelems.cpp 1-2347: read fully (twice)
- verification pass done; writing findings JSON

## Verified candidates -> findings
1 initImageInternalTexture redundant (762-784, 893-895); texLoadInternal(-1) returns ERR_BAD_FILE (textures.cpp:1007)
2 overlay draw x3 (931-938,1307-1314,1546-1553)
3 8 corner reads 729-752 (getInt leaves value untouched, config.cpp:420)
4 malloc+zeroing -> calloc (211-217, 810-829, 1608-1622, 704, 768, 2134-2141)
5 12-family list: 871-882 + themes.cpp 196-198, 334-336, 1122-1124, 1855-1858
6 constant/unused params (initMutableText, findDuplicate, initMutableImage, initGameImage, initStaticImage, initBackground, initItemsList)
7 ELEM_TYPE_COVERFLOW never assigned (367, 1380); themes.cpp:644 is a name compare, fine
8 drawItemsList dup text draw 2060-2086
9 redundant checks 1234-1239,1247-1249,1257-1259,860-869
10 forwarding wrappers getGameImageTexture, thmFindElemBySuffix, thmGetItemSource
11 attribute image dual built-in test 1535 vs 1557-1571 (INFERRED)
12 includes/decl 4-6,15,26, 1104-1106
13 stale comments (guiAlignMenuHints gone; .c names; themes.h:320; 2143)
## Dropped
- header prototypes/fields: all have users (grep)
- thmParseDeviceList quiet: used with 0 and 1
