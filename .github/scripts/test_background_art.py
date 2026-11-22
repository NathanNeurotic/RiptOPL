"""HIRES per-game background preparation: smooth bounded resize and CT16S, ordinary modes unchanged."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/gfx/textures.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
if "void texPrepareArtForDisplay" not in source:
    sys.exit("FAIL: no worker-side HIRES background preparation exists")
start = source.index("void texPrepareArtForDisplay")
brace = source.index("{", start)
depth = 0
for i in range(brace, len(source)):
    if source[i] == "{":
        depth += 1
    elif source[i] == "}":
        depth -= 1
        if depth == 0:
            helper = source[start:i + 1]
            break
else:
    raise SystemExit("unterminated texPrepareArtForDisplay")

program = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <math.h>
typedef unsigned char u8;
typedef unsigned int u32;
struct GSTEXTURE { int Width, Height; u8 PSM; u32 *Mem, *Clut, Vram, VramClut; };
enum { GS_PSM_CT24 = 1, GS_PSM_CT16S = 2 };
static int hires;
static int rmUse16BitArt() { return hires; }
static u32 gsKit_texture_size_ee(int w, int h, int psm) { return (u32)w * h * (psm == GS_PSM_CT24 ? 4 : 2); }
static void gsKit_texture_to_psm16(GSTEXTURE *texture) { texture->PSM = GS_PSM_CT16S; }
constexpr int TEX_BG_MAX_W = 720, TEX_BG_MAX_H = 512;
@HELPER@
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static GSTEXTURE make(int w, int h, const u8 *pixels) {
    GSTEXTURE t{};
    t.Width = w; t.Height = h; t.PSM = GS_PSM_CT24;
    t.Mem = (u32 *)malloc((size_t)w * h * 4);
    memcpy(t.Mem, pixels, (size_t)w * h * 3);
    return t;
}
int main() {
    const u8 pixels[12] = {10,10,10, 20,20,20, 30,30,30, 40,40,40};
    hires = 1;
    GSTEXTURE t = make(2, 2, pixels);
    texPrepareArtForDisplay(&t, 1);
    CHECK(t.PSM == GS_PSM_CT16S && t.Width == 2 && t.Height == 2);
    u8 *out = (u8 *)t.Mem;
    CHECK(out[0] == 10 && out[1] == 10 && out[2] == 10);
    free(t.Mem);

    const u8 bigPixels[1024 * 768 * 3] = {};
    GSTEXTURE big = make(1024, 768, bigPixels);
    texPrepareArtForDisplay(&big, 1);
    CHECK(big.PSM == GS_PSM_CT16S && big.Width == 682 && big.Height == 512);
    free(big.Mem);

    GSTEXTURE ordinary = make(2, 2, pixels);
    u32 *original = ordinary.Mem;
    hires = 0;
    texPrepareArtForDisplay(&ordinary, 1);
    CHECK(ordinary.PSM == GS_PSM_CT24 && ordinary.Width == 2 && ordinary.Height == 2 && ordinary.Mem == original);
    free(ordinary.Mem);

    GSTEXTURE notBackground = make(2, 2, pixels);
    original = notBackground.Mem;
    hires = 1;
    texPrepareArtForDisplay(&notBackground, 0);
    CHECK(notBackground.PSM == GS_PSM_CT24 && notBackground.Mem == original);
    free(notBackground.Mem);
    return failures ? 1 : 0;
}
'''.replace("@HELPER@", helper)

def compile_and_run(folder, name, text):
    cpp = folder / (name + ".cpp")
    exe = folder / name
    cpp.write_text(text, encoding="utf-8")
    built = subprocess.run(["g++", "-std=gnu++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", str(cpp), "-o", str(exe)], capture_output=True, text=True)
    if built.returncode:
        print(built.stderr)
        return 1
    ran = subprocess.run([str(exe)], capture_output=True, text=True)
    print(ran.stdout, end="")
    return ran.returncode

small_cap = program.replace("constexpr int TEX_BG_MAX_W = 720, TEX_BG_MAX_H = 512;", "constexpr int TEX_BG_MAX_W = 1, TEX_BG_MAX_H = 1;")
small_cap = small_cap.replace("t.Width == 2 && t.Height == 2", "t.Width == 1 && t.Height == 1")
small_cap = small_cap.replace("out[0] == 10 && out[1] == 10 && out[2] == 10", "out[0] == 25 && out[1] == 25 && out[2] == 25")
small_cap = small_cap.replace("big.Width == 682 && big.Height == 512", "big.Width == 1 && big.Height == 1")

with tempfile.TemporaryDirectory(prefix="background-art-") as tmp:
    folder = Path(tmp)
    results = [compile_and_run(folder, "normal-budget", program), compile_and_run(folder, "tiny-budget", small_cap)]
if any(results):
    sys.exit("FAIL: background resampling, bounds, or ordinary-mode preservation")
print("PASS: smooth bounded backgrounds, CT16S in HIRES, ordinary art unchanged")
