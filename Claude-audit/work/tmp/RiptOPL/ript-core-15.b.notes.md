# ript-core-15.b notes (lens defects)

Done (all read full): src/gfx/fntsys.cpp 1-1324, include/grid.h 1-114, src/gfx/grid.cpp 1-714, include/plasma.h 1-36, src/gfx/plasma.cpp 1-260, include/psbbnflow.h 1-138, src/gfx/psbbnflow.cpp 1-313, include/renderman.h 1-169

Final candidates (checked):
1. fntsys.cpp:393-412,489-511,648-677 loadDefault new face never FT_Set_Char_Size (newFont local). Triggers: opl.cpp:1944-1952 OPLDIAG autolaunch draws right after fntInit; boot splash guisplash.cpp:102-107 before _loadConfig/applyConfig (opl.cpp:1711-1719, settingsio.cpp:2521-2523). FreeType 2.14.1 (SDK header) no-size => NO_SCALE, RENDER dropped (INFERRED). rank 2.
2. kerning never applied, previous always 0: fntsys.cpp 753-755,775-783,837-844,887-889,907-916,1227-1229,1244-1252; fontsystem.h 97-99. Built-in font has no kern table. rank 2, ~45 lines.
3. GLYPH_CACHE_PAGE_SIZE 256 x 36 B = 9 KB pages; CJK lang files touch 74-81 pages = 666-729 KB per font; at 32: 269-382 KB. fontsystem.h:22, fntsys.cpp:187-221. rank 3.
4. atlas full: glyphAtlasPlace 545-578 -> cacheGlyph 608-625 returns NULL with entry invalid -> FreeType reload+render every draw/measure; ATLAS_MAX 4 at 720p ~100 CJK/atlas (renderman.cpp:74 720p, hs 1.5). rank 2.
5. cacheGlyph 586-591 two cmap lookups per non-ASCII glyph per draw before cache check when fallback loaded (lang.cpp:424-441). rank 1.
Dropped: grid/psbbnflow/plasma buffers bounded; clock unsigned diffs fine; ease index ok (guiscreens.cpp:65-77); UTF-8 sticky reject not shown reachable; spacing overflow theoretical.
