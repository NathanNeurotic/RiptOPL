# ript-core-19.b notes (lens defects)

## Done (attempt 2 re-read everything)
- src/gfx/themes.cpp 1-2247
- include/themeviews.h 1-143
- src/gfx/themeviews.cpp 1-544
- include/uidraw.h 1-13
- src/gfx/uidraw.cpp 1-86

## Checked and dropped
- themes.cpp:1445-1456 free(cfgBuf): tarThemeRead sets *buf=NULL first (tar.cpp:684) -> fine
- themes.cpp:1026 themes_ bound: listDir stops at maxElem = THM_MAX_FILES - count_ (util.cpp:399) -> fine
- themes.cpp:1049-1051 separator: every caller passes "/" or "\\" (1 char) -> fine
- themes.cpp:1712 loadingIconCount: LOAD0_ICON = 0, never 0 -> fine
- uidraw.cpp div by Height: thmGetTexture returns NULL unless Mem set -> fine
- themeviews setStr while walking: append at tail, existing entries updated in place (config.cpp:363-386) -> fine
- 500-tick drains = 500 ms DelayThread(1000) each (artload.cpp:426-455); folded into GUI-thread reload finding

## Findings to write
1. themes.cpp:1683-1697 boot logo LOGO + LOGO1..6 decoded per theme, never drawn (guisplash.cpp:70-73); ~519 KB RAM; blobs 29,811 B MEASURED per-object.tsv
2. themes.cpp:2186-2208 + menusys.cpp:2773-2776 hold-L3 view switch: full thmLoad on GUI thread (fonts, ~47 png probes, drains) -> use guiHandleDeferedIO
3. themes.cpp:1972-2012 reinit: stale themeId_ after failed thmLoad(NULL); activeName[64] truncation
4. font slots: FNT_MAX_COUNT 16 (fontsystem.h:21) vs THM_MAX_FONTS 16, new fonts loaded before old freed (themes.cpp:1576 vs 1843), fallback slot (fntsys.cpp:448-456)
5. uidraw.cpp:70 NULL icon measured as 20+2 but drawn as 0 (uidraw.cpp:14-29)
