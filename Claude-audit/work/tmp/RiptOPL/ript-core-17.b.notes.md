# ript-core-17.b notes (lens defects)

Done: src/gfx/textshape.cpp 1-452, include/textures.h 1-238, src/gfx/textures.cpp 1-1438 (all read in full).

Candidates:
- textures.cpp:1023-1031,1108-1111,1041-1042,1101-1105 + texDiscoverArt 1397-1409 | permanent decode failures (ERR_SET_JMP/ERR_BAD_DIMENSION/ERR_BAD_DEPTH) reach artload.cpp:746 which treats all != ERR_BAD_FILE as transient -> cacheClearItem -> texcache re-claims next frame (no gate for non-SIO2, texcache.cpp:372-378) -> endless reload | checked artload 714-758, texcache 294-470, bdmsupport 2082-2098; streaming read error -> png_error -> ERR_SET_JMP (texReadFileFunction 481-482) must become ERR_FILE_IO in fix
- textures.cpp:1309-1335,1269-1288 | .png name that fails as PNG: second open + full staged read for JPG attempt | checked texLoad/texLoadAll staged arm
- textures.cpp:1172-1212 | no dimension limit before jpeg_start_decompress; progressive JPEG whole-image coef buffer | INFERRED memory figures
- textures.cpp:639-659,681-698 | known size > 8 MB or malloc(size) fail falls into doubling loop (reads up to 8 MB, 12 MB realloc peak) before refusing
- include/utf8.h:21-36 table static in header, two copies in ELF (nm: 2 x 0x190 _ZL5utf8d), textshape.cpp:12 + fntsys.cpp:12 | MEASURED raw/riptopl-nm-size-sorted.full.txt:1757-1758
- dropped: txtShape static buffers (all callers under font lock), -Wclobbered fd (not modified after setjmp), tarThemeRead free(buf) (buf set NULL), clut swap logic OK, internalDefault id field (test-enforced order check), per-frame txtShape cost (small, caller outside unit)
