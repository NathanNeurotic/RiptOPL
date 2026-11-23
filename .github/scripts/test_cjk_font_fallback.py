#!/usr/bin/env python3
"""Host regression for optional glyph fallback without a PS2 toolchain.

Compile the actual face-selection block from src/gfx/fntsys.cpp against small stubs.
This covers the decision made by BOTH text measurement and rendering because
they share FontSystem::cacheGlyph(). It does not simulate PS2 glyph atlas/VRAM behavior.
"""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
font = (root / "src/gfx/fntsys.cpp").read_text(encoding="utf-8")
fontsystem = (root / "include/fontsystem.h").read_text(encoding="utf-8")
language = (root / "src/core/lang.cpp").read_text(encoding="utf-8")
header = (root / "include/fntsys.h").read_text(encoding="utf-8")
start = "    // Select a second face only for characters missing from the active font."
end = "    // calc page id and in-page index from glyph id"
assert font.count(start) == 1 and font.count(end) == 1
selection = font.split(start, 1)[1].split(end, 1)[0]
assert "FT_Get_Char_Index(font->face, gid) == 0" in selection
assert "FT_Get_Char_Index(fonts_[fallbackFontId_].face, gid) != 0" in selection
assert "int fntLoadFallback(char *path)" in font
assert "int fntLoadFallback(char *path);" in header
assert "int loadFallback(char *path);" in fontsystem
lang_header = (root / "include/lang.h").read_text(encoding="utf-8")
opl = (root / "src/app/opl.cpp").read_text(encoding="utf-8")
# The fallback belongs to the FINAL settings home: loaded after _loadConfig, never from lngInit.
assert "void lngInitFallbackFont(void);" in lang_header
assert "    lngInitFallbackFont();" not in language
assert opl.index("lngInitFallbackFont();") > opl.index("_loadConfig();")
assert "configGetHomePath()" in language
assert "fntLoadDefault(NULL);" in language  # Existing UI-language reset retained.
assert '"font_CJK.ttf"' in language and '"font_Japanese.ttf"' in language

harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#define FNT_DEFAULT 0
#define FNT_MAX_COUNT 16
typedef struct { int isValid; void *face; } font_t;
static font_t fonts_[FNT_MAX_COUNT];
static int fallbackFontId_ = -1;
/* Fake font: 1 = Latin + Japanese; 2 = Japanese + Greek; 3 = Latin. */
static int FT_Get_Char_Index(void *face, uint32_t cp)
{
    uintptr_t id = (uintptr_t)face;
    if (id == 1) return cp == 0x3042 || cp == 'A';
    if (id == 2) return cp == 0x3042 || cp == 0x03A9;
    if (id == 3) return cp == 'A';
    return 0;
}
static font_t *resolve(font_t *font, uint32_t gid)
{
""" + start + selection + """
    return font;
}
int main(void)
{
    fonts_[0] = (font_t){1, (void*)3}; /* Default Latin UI font. */
    fonts_[1] = (font_t){1, (void*)2}; /* Optional fallback. */
    fallbackFontId_ = 1;
    assert(resolve(&fonts_[0], 0x3042) == &fonts_[1]); /* Japanese. */
    assert(resolve(&fonts_[0], 0x03A9) == &fonts_[1]); /* Greek. */
    assert(resolve(&fonts_[0], 'A') == &fonts_[0]); /* Never steal ASCII. */
    assert(resolve(&fonts_[1], 0x3042) == &fonts_[1]); /* No recursion. */
    fonts_[2] = (font_t){1, (void*)1};
    assert(resolve(&fonts_[2], 0x3042) == &fonts_[2]); /* Keep active face. */
    fonts_[1].isValid = 0;
    assert(resolve(&fonts_[0], 0x3042) == &fonts_[0]); /* Safe if unavailable. */
    fallbackFontId_ = -1;
    assert(resolve(&fonts_[0], 0x3042) == &fonts_[0]); /* Safe if absent. */
    /* A translation's font is the menu font: a theme font without Japanese borrows from it. */
    fonts_[0] = (font_t){1, (void*)1}; /* Japanese UI font. */
    fonts_[3] = (font_t){1, (void*)3}; /* Theme Latin font. */
    assert(resolve(&fonts_[3], 0x3042) == &fonts_[0]);
    assert(resolve(&fonts_[3], 'A') == &fonts_[3]); /* Never steal ASCII. */
    assert(resolve(&fonts_[3], 0x03A9) == &fonts_[3]); /* Neither face has it: keep the theme font. */
    fonts_[1] = (font_t){1, (void*)2};
    fallbackFontId_ = 1;
    assert(resolve(&fonts_[3], 0x03A9) == &fonts_[1]); /* The optional fallback still comes first. */
    fonts_[0].isValid = 0;
    fallbackFontId_ = -1;
    assert(resolve(&fonts_[3], 0x3042) == &fonts_[3]); /* Safe if the menu font is unavailable. */
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="cjk-font-test-") as td:
    temp_path = pathlib.Path(td)
    source = temp_path / "fallback.c"
    binary = temp_path / ("fallback.exe" if os.name == "nt" else "fallback")
    source.write_text(harness, encoding="utf-8")
    cc = os.environ.get("CC", "gcc" if os.name == "nt" else "cc")
    subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-o", str(binary), str(source)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
print("PASS: CJK fallback is opt-in, font-preserving and selected only for missing Unicode glyphs")
