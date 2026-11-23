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
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned int u32;
struct GSTEXTURE { int Width, Height; u8 PSM; u32 *Mem, *Clut, Vram, VramClut; };
enum { GS_PSM_CT32 = 0, GS_PSM_CT24 = 1, GS_PSM_CT16S = 2, GS_PSM_T8 = 3, GS_PSM_T4 = 4 };
static int hires;
static int rmUse16BitArt() { return hires; }
static u32 gsKit_texture_size_ee(int w, int h, int psm) { return (u32)w * h * (psm == GS_PSM_CT24 || psm == GS_PSM_CT32 ? 4 : psm == GS_PSM_CT16S ? 2 : 1); }
static int failAlignedAllocation;
static void *controlledMemalign(size_t alignment, size_t size) {
    if (failAlignedAllocation) return NULL;
    (void)alignment;
    return malloc(size);
}
#define memalign controlledMemalign
static void gsKit_texture_to_psm16(GSTEXTURE *texture) {
    const s8 matrix[16] = {-4,2,-3,3,0,-2,1,-1,-3,3,-4,2,1,-1,0,-2};
    u16 *out = (u16 *)memalign(128, (size_t)texture->Width * texture->Height * 2);
    const u8 *in = (const u8 *)texture->Mem;
    for (int y = 0; y < texture->Height; y++) for (int x = 0; x < texture->Width; x++) {
        int i = y * texture->Width + x;
        int d = matrix[(y & 3) * 4 + (x & 3)];
        u16 c[3];
        for (int channel = 0; channel < 3; channel++) {
            int v = in[i * 3 + channel] + d;
            c[channel] = v > 255 ? 31 : v < 0 ? 0 : (u16)(v >> 3);
        }
        out[i] = 0x8000 | (c[2] << 10) | (c[1] << 5) | c[0];
    }
    free(texture->Mem);
    texture->Mem = (u32 *)out;
    texture->PSM = GS_PSM_CT16S;
}
constexpr int TEX_BG_MAX_W = 720, TEX_BG_MAX_H = 512;
@HELPER@
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static GSTEXTURE make(int w, int h, const u8 *pixels, u8 psm = GS_PSM_CT24) {
    GSTEXTURE t{};
    t.Width = w; t.Height = h; t.PSM = psm;
    t.Mem = (u32 *)malloc((size_t)w * h * 4);
    memcpy(t.Mem, pixels, (size_t)w * h * (psm == GS_PSM_CT32 ? 4 : 3));
    return t;
}
int main() {
    const u8 pixels[12] = {10,10,10, 20,20,20, 30,30,30, 40,40,40};
    hires = 1;
    GSTEXTURE t = make(2, 2, pixels);
    texPrepareArtForDisplay(&t, 1);
    CHECK(t.PSM == GS_PSM_CT16S && t.Width == 2 && t.Height == 2);
    if (TEX_BG_MAX_W == 720) {
        const u16 expected[4] = {0x8000, 0x8842, 0x8C63, 0x9084};
        for (int i = 0; i < 4; i++) CHECK(((const u16 *)t.Mem)[i] == expected[i]);
    } else {
        CHECK(((const u16 *)t.Mem)[0] == 0x8842);
    }
    free(t.Mem);

    static const u8 bigPixels[1024 * 768 * 3] = {};
    GSTEXTURE big = make(1024, 768, bigPixels);
    texPrepareArtForDisplay(&big, 1);
    CHECK(big.PSM == GS_PSM_CT16S && big.Width == 682 && big.Height == 512);
    free(big.Mem);

    const u8 rgbaPixels[8] = {0,255,0,127, 255,0,248,0};
    GSTEXTURE rgba = make(2, 1, rgbaPixels, GS_PSM_CT32);
    texPrepareArtForDisplay(&rgba, 1);
    CHECK(rgba.PSM == GS_PSM_CT16S && rgba.Width == 2 && rgba.Height == 1);
    const u16 *rgbaOut = (const u16 *)rgba.Mem;
    CHECK(rgbaOut[0] == 0x83E0 && rgbaOut[1] == 0x7C1F);
    free(rgba.Mem);

    u32 *original;
    GSTEXTURE indexed8 = make(2, 2, pixels);
    indexed8.PSM = GS_PSM_T8;
    original = indexed8.Mem;
    texPrepareArtForDisplay(&indexed8, 1);
    CHECK(indexed8.PSM == GS_PSM_T8 && indexed8.Mem == original);
    free(indexed8.Mem);

    GSTEXTURE indexed4 = make(2, 2, pixels);
    indexed4.PSM = GS_PSM_T4;
    original = indexed4.Mem;
    texPrepareArtForDisplay(&indexed4, 1);
    CHECK(indexed4.PSM == GS_PSM_T4 && indexed4.Mem == original);
    free(indexed4.Mem);

    GSTEXTURE ordinary = make(2, 2, pixels);
    original = ordinary.Mem;
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

    GSTEXTURE oom = make(2, 2, pixels);
    original = oom.Mem;
    failAlignedAllocation = 1;
    texPrepareArtForDisplay(&oom, 1);
    failAlignedAllocation = 0;
    CHECK(oom.PSM == GS_PSM_CT24 && oom.Mem == original && oom.Width == 2 && oom.Height == 2);
    free(oom.Mem);
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
small_cap = small_cap.replace("big.Width == 682 && big.Height == 512", "big.Width == 1 && big.Height == 1")
small_cap = small_cap.replace("rgba.Width == 2 && rgba.Height == 1", "rgba.Width == 1 && rgba.Height == 1")
small_cap = small_cap.replace("rgbaOut[0] == 0x83E0 && rgbaOut[1] == 0x7C1F", "rgbaOut[0] == 0xBDEF")

with tempfile.TemporaryDirectory(prefix="background-art-") as tmp:
    folder = Path(tmp)
    results = [compile_and_run(folder, "normal-budget", program), compile_and_run(folder, "tiny-budget", small_cap)]
if any(results):
    sys.exit("FAIL: background resampling, bounds, or ordinary-mode preservation")
print("PASS: smooth bounded backgrounds, CT16S in HIRES, ordinary art unchanged")
