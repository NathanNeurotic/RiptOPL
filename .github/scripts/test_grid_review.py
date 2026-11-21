"""Grid shared-resource ownership and disc-cache capacity regressions."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
themes = (root / "src/gfx/themes.cpp").read_text(encoding="utf-8")
grid = (root / "src/gfx/grid.cpp").read_text(encoding="utf-8")
elems = (root / "src/gfx/themeelems.cpp").read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    end = source.index("\n}\n", start)
    return source[start:end + 3]


failures = []
for label, source, signature in (
    ("candidate theme teardown", themes, "static void thmFree("),
    ("Grid element teardown", grid, "static void endGrid("),
):
    if "gridReleaseGlyphs();" in function(source, signature):
        failures.append(f"{label} frees the shared glyph texture")

load = themes[themes.index("static int thmLoad("):themes.index("int thmRefreshFontSize(")]
publish = load[load.rindex("    host::ui().lock();"):load.rindex("    return 0;")]
if "gridReleaseGlyphs();" not in publish or publish.index("gridReleaseGlyphs();") < publish.index("rmDiscardFrame();"):
    failures.append("a committed theme load does not retire the frame before releasing Grid glyphs")

program = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "grid.h"
struct config_set_t {};
struct theme_element_t { int drawDisc; };
struct theme_t { void *discCache; int gameCacheCount; };
static int cacheSlots, cacheCalls;
static void *cache;
static int configGetInt(config_set_t *, const char *prop, int *value)
{
    assert(!strcmp(prop, "covers_disc") || !strcmp(prop, "grid_disc"));
    *value = 1;
    return 1;
}
static void *cacheInitCache(int, const char *, int, const char *, int slots)
{
    cacheSlots = slots;
    cacheCalls++;
    return &cache;
}
''' + function(elems, "void thmInitElemDiscCache(") + r'''
int main()
{
    config_set_t config;
    theme_t theme = {};
    theme_element_t elem = {};
    thmInitElemDiscCache(&config, &theme, &elem, "covers", 20);
    assert(elem.drawDisc == 1);
    assert(cacheCalls == 1 && cacheSlots >= GRID_MAX_CELLS);
    assert(theme.gameCacheCount == 1 && theme.discCache == &cache);
    thmInitElemDiscCache(&config, &theme, &elem, "grid", 48);
    assert(cacheCalls == 1 && cacheSlots >= GRID_MAX_CELLS);
}
'''
with tempfile.TemporaryDirectory(prefix="grid-disc-cache-") as tmp:
    src, exe = Path(tmp) / "cache.cpp", Path(tmp) / "cache"
    src.write_text(program, encoding="utf-8")
    result = subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", f"-I{root / 'include'}", str(src), "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        failures.append("disc-cache harness did not compile: " + result.stderr)
    else:
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        if result.returncode:
            failures.append("a Coverflow-first disc cache is smaller than the largest Grid: " + result.stderr)

if failures:
    raise SystemExit("\n".join(failures))
print("PASS: Grid glyphs outlive abandoned loads and the shared disc cache covers the largest Grid")
