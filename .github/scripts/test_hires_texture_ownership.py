"""HIRES texture ownership and starvation recovery controls.

The first program compiles the real cacheClearItem and catches immediate pixel frees. The second compiles
the production texture manager against bounded VRAM and DMA-reference models. A degraded frame may skip new
uploads, but it must complete so expired VRAM becomes available instead of starving forever.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/gfx/texcache.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
start = source.index("void cacheClearItem(cache_entry_t *item, int freeTxt)")
brace = source.index("{", start)
depth = 0
for i in range(brace, len(source)):
    if source[i] == "{":
        depth += 1
    elif source[i] == "}":
        depth -= 1
        if depth == 0:
            clear_item = source[start:i + 1]
            break
else:
    raise SystemExit("unterminated cacheClearItem")

caller_program = r'''
#include <cstdio>
#include <cstring>
typedef unsigned int u32;
typedef unsigned long long u64;
struct GSTEXTURE { u32 *Mem, *Clut, Vram, VramClut, ClutStorageMode; };
struct cache_entry_t { GSTEXTURE texture; void *qr; int lastUsed, UID; };
#define GS_CLUT_STORAGE_CSM1 0
static int releases, unloads, frees;
static void rmReleaseTexture(GSTEXTURE *) { releases++; }
static void rmUnloadTexture(GSTEXTURE *) { unloads++; }
static void test_free(void *) { frees++; }
#define free test_free
@CLEAR_ITEM@
#undef free
int main() {
    cache_entry_t item{};
    item.texture.Mem = (u32 *)0x10;
    item.texture.Clut = (u32 *)0x20;
    cacheClearItem(&item, 1);
    if (releases != 1 || unloads != 0 || frees != 0) {
        std::printf("FAIL: cache released pixels early (release=%d unload=%d free=%d)\n", releases, unloads, frees);
        return 1;
    }
    std::printf("PASS: cache hands whole texture ownership to the renderer\n");
    return 0;
}
'''.replace("@CLEAR_ITEM@", clear_item)

plasma_source = (root / "src/gfx/plasma.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
start = plasma_source.index("void PlasmaBackground::shutdown()")
brace = plasma_source.index("{", start)
depth = 0
for i in range(brace, len(plasma_source)):
    if plasma_source[i] == "{":
        depth += 1
    elif plasma_source[i] == "}":
        depth -= 1
        if depth == 0:
            plasma_shutdown = plasma_source[start:i + 1]
            break
else:
    raise SystemExit("unterminated PlasmaBackground::shutdown")

plasma_program = r'''
#include <cstdio>
typedef unsigned int u32;
struct GSTEXTURE { u32 *Mem, *Clut, Vram, VramClut; };
struct PlasmaBackground { GSTEXTURE tex_; void shutdown(); };
static int releases, frees;
static void rmReleaseTexture(GSTEXTURE *texture) { releases++; texture->Mem = nullptr; texture->Clut = nullptr; }
static void test_free(void *) { frees++; }
#define free test_free
@PLASMA_SHUTDOWN@
#undef free
int main() {
    PlasmaBackground plasma{};
    plasma.tex_.Mem = (u32 *)0x10;
    plasma.shutdown();
    if (releases != 1 || frees != 0 || plasma.tex_.Mem != nullptr) {
        std::printf("FAIL: plasma shutdown bypassed deferred release (release=%d free=%d)\n", releases, frees);
        return 1;
    }
    std::printf("PASS: plasma shutdown uses renderer-owned texture release\n");
    return 0;
}
'''.replace("@PLASMA_SHUTDOWN@", plasma_shutdown)

render_source = (root / "src/gfx/renderman.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
start = render_source.index("void Renderer::endFrame(void)")
brace = render_source.index("{", start)
depth = 0
for i in range(brace, len(render_source)):
    if render_source[i] == "{":
        depth += 1
    elif render_source[i] == "}":
        depth -= 1
        if depth == 0:
            frame_end = render_source[start:i + 1]
            break
else:
    raise SystemExit("unterminated Renderer::endFrame")

frame_end_program = r'''
#include <cstdio>
struct GSQUEUE { int unused; };
struct GSGLOBAL {
    GSQUEUE *CurQueue;
    int DoubleBuffering = 0, ActiveBuffer = 0, FirstFrame = 0, PSM = 0, Width = 640;
    unsigned ScreenBuffer[2] = {0, 0};
};
static GSGLOBAL globalStore, *gsGlobal = &globalStore;
static int guiThreadID;
enum { GS_SETTING_ON = 1 };
class Renderer { public: int hires_ = 1, wakeCount_ = 0; void endFrame(); };
static int starved, syncs, flips, resets, commits, lastCompleted;
static void gsKit_hires_sync(GSGLOBAL *) { syncs++; }
static void gsKit_hires_flip(GSGLOBAL *) { flips++; }
static void gsKit_queue_reset(GSQUEUE *) { resets++; }
static int gsKit_TexManager_starved() { return starved; }
static void gsKit_TexManager_commitFrame(int completed) { commits++; lastCompleted = completed; }
static void gsKit_set_finish(GSGLOBAL *) {}
static void gsKit_queue_exec(GSGLOBAL *) {}
static void gsKit_finish() {}
static void DIntr() {}
static void EIntr() {}
static void CancelWakeupThread(int) {}
static void SleepThread() {}
#define GS_SET_DISPFB2(a,b,c,d,e) ((void)0)
static void gsKit_setactive(GSGLOBAL *) {}
@FRAME_END@
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
int main() {
    Renderer renderer;
    starved = 1;
    renderer.endFrame();
    CHECK(syncs == 1 && flips == 0 && resets == 1 && commits == 1 && lastCompleted == 0);
    starved = 0;
    renderer.endFrame();
    CHECK(syncs == 2 && flips == 1 && resets == 1 && commits == 2 && lastCompleted == 1);
    if (!failures) std::printf("PASS: starved frame resets without flipping, then completes normally\n");
    return failures ? 1 : 0;
}
'''.replace("@FRAME_END@", frame_end)

manager_program = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
struct GSTEXTURE {
    u32 *Mem = nullptr, *Clut = nullptr;
    u32 Vram = 0, VramClut = 0;
    int Width = 64, Height = 64;
    u8 PSM = 0, ClutPSM = 0, TBW = 0, ClutStorageMode = 0;
};
struct GSGLOBAL { u32 CurrentPointer; };
enum ETransferMode { ETM_INLINE, ETM_DIRECT };
enum { GS_PSM_CT16S = 2, GS_PSM_CT32 = 0, GS_PSM_T8 = 3, GS_CLUT_NONE = 0, GS_CLUT_TEXTURE = 1, GS_CLUT_PALLETE = 2 };
#define GSKIT_TEXMGR_BIND_DEFERRED 0x80000000u
static std::vector<void *> frees;
extern "C" void test_free(void *p) noexcept { frees.push_back(p); std::free(p); }
#define free test_free
static int uploadCount, inlineUploads, directUploads;
static u32 textureBytes(int w, int h, int psm) { return (u32)w * (u32)h * (psm == GS_PSM_CT16S ? 2 : 4); }
static u32 gsKit_texture_size_ee(int w, int h, int psm) { return textureBytes(w, h, psm); }
static u32 gsKit_texture_size(int w, int h, int psm) { return textureBytes(w, h, psm); }
static void gsKit_setup_tbw(GSTEXTURE *tex) { tex->TBW = (tex->Width + 63) / 64; }
static void SyncDCache(const void *, const void *) {}
static void gsKit_texture_send_inline(GSGLOBAL *, u32 *, int, int, u32, u32, u32, u8) { uploadCount++; inlineUploads++; }
static void gsKit_texture_send(u32 *, int, int, u32, u32, u32, u8) { uploadCount++; directUploads++; }
#include "src/gfx/texmanager.cpp"
#undef free
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static bool freed(void *p) { for (void *q : frees) if (p == q) return true; return false; }
static GSTEXTURE makeTexture(int w, int h, bool clut = false) {
    GSTEXTURE t;
    t.Width = w; t.Height = h; t.PSM = GS_PSM_CT16S; t.ClutPSM = GS_PSM_CT32;
    t.Mem = (u32 *)std::malloc(16);
    if (clut) t.Clut = (u32 *)std::malloc(16);
    return t;
}
static GSGLOBAL global{4096};
static void reset(int latency) {
    gsKit_TexManager_flush();
    gsKit_TexManager_init(&global);
    gsKit_TexManager_setQueueLatency(latency);
    frees.clear(); uploadCount = inlineUploads = directUploads = 0;
}
static void releaseNow(GSTEXTURE *t) {
    gsKit_TexManager_setQueueLatency(0);
    gsKit_TexManager_release(&global, t);
}
int main() {
    // Direct uploads free source pixels immediately while outgoing VRAM remains protected.
    reset(1);
    GSTEXTURE a = makeTexture(64, 64, true), b = makeTexture(64, 64), c = makeTexture(64, 64);
    gsKit_TexManager_bind(&global, &a);
    CHECK(directUploads > 0 && inlineUploads == 0);
    u32 oldVram = a.Vram;
    void *oldMem = a.Mem, *oldClut = a.Clut;
    gsKit_TexManager_commitFrame(1);
    gsKit_TexManager_release(&global, &a);
    CHECK(freed(oldMem) && freed(oldClut));
    CHECK(a.Mem == nullptr && a.Clut == nullptr);
    gsKit_TexManager_bind(&global, &b);
    CHECK(b.Vram != oldVram);
    gsKit_TexManager_commitFrame(1);
    CHECK(gsKit_TexManager_bind(&global, &c) != GSKIT_TEXMGR_BIND_DEFERRED);
    releaseNow(&b); releaseNow(&c);

    // Bounded VRAM: a new frame starves, then a degraded complete frame lets old blocks expire.
    reset(1);
    GSTEXTURE d = makeTexture(768, 1024), e = makeTexture(768, 1024), f = makeTexture(768, 1024);
    CHECK(gsKit_TexManager_bind(&global, &d) != GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(gsKit_TexManager_bind(&global, &e) != GSKIT_TEXMGR_BIND_DEFERRED);
    gsKit_TexManager_commitFrame(1);
    CHECK(gsKit_TexManager_bind(&global, &f) == GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(gsKit_TexManager_starved());
    gsKit_TexManager_commitFrame(0);
    CHECK(!gsKit_TexManager_starved());
    CHECK(gsKit_TexManager_bind(&global, &d) != GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(gsKit_TexManager_bind(&global, &f) == GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(!gsKit_TexManager_starved()); // degraded frame: skip new uploads, but complete the frame
    gsKit_TexManager_commitFrame(1);
    CHECK(gsKit_TexManager_bind(&global, &f) != GSKIT_TEXMGR_BIND_DEFERRED);
    releaseNow(&d); releaseNow(&e); releaseNow(&f);

    // A reused block reuploaded in a discarded queue must be invalidated by rollback too.
    reset(1);
    GSTEXTURE r = makeTexture(64, 64), huge = makeTexture(2048, 1024);
    gsKit_TexManager_bind(&global, &r);
    gsKit_TexManager_commitFrame(1);
    gsKit_TexManager_commitFrame(1);
    gsKit_TexManager_invalidate(&global, &r);
    CHECK(gsKit_TexManager_bind(&global, &r) != GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(gsKit_TexManager_bind(&global, &huge) == GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(gsKit_TexManager_starved());
    gsKit_TexManager_commitFrame(0);
    CHECK(r.Vram == 0);
    int uploadsBeforeRetry = uploadCount;
    CHECK(gsKit_TexManager_bind(&global, &r) != GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(r.Vram != 0 && uploadCount > uploadsBeforeRetry);
    gsKit_TexManager_commitFrame(1);
    CHECK(gsKit_TexManager_bind(&global, &r) != GSKIT_TEXMGR_BIND_DEFERRED);
    releaseNow(&r); releaseNow(&huge);

    // Persistent overflow stays deferred without another failed queue or a starvation loop.
    reset(1);
    GSTEXTURE s = makeTexture(64, 64), oversized = makeTexture(2048, 1024), small = makeTexture(64, 64);
    gsKit_TexManager_bind(&global, &s);
    gsKit_TexManager_commitFrame(1);
    CHECK(gsKit_TexManager_bind(&global, &oversized) == GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(gsKit_TexManager_starved());
    gsKit_TexManager_commitFrame(0);
    gsKit_TexManager_commitFrame(1);
    CHECK(gsKit_TexManager_bind(&global, &oversized) == GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(!gsKit_TexManager_starved());
    gsKit_TexManager_commitFrame(1);
    CHECK(gsKit_TexManager_bind(&global, &oversized) == GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(!gsKit_TexManager_starved());
    CHECK(gsKit_TexManager_bind(&global, &small) != GSKIT_TEXMGR_BIND_DEFERRED);
    gsKit_TexManager_commitFrame(1);
    CHECK(gsKit_TexManager_bind(&global, &oversized) == GSKIT_TEXMGR_BIND_DEFERRED);
    CHECK(!gsKit_TexManager_starved());
    releaseNow(&s); releaseNow(&oversized); releaseNow(&small);

    // Seventy protected blocks plus forty different requests recover through one degraded complete frame.
    reset(1);
    std::vector<GSTEXTURE> displayed(70), requested(40);
    for (GSTEXTURE &t : displayed) t = makeTexture(320, 64);
    for (GSTEXTURE &t : requested) t = makeTexture(320, 64);
    for (GSTEXTURE &t : displayed)
        CHECK(gsKit_TexManager_bind(&global, &t) != GSKIT_TEXMGR_BIND_DEFERRED);
    gsKit_TexManager_commitFrame(1);
    int deferred = 0;
    for (GSTEXTURE &t : requested) {
        if (gsKit_TexManager_bind(&global, &t) == GSKIT_TEXMGR_BIND_DEFERRED)
            deferred++;
    }
    CHECK(deferred > 0 && gsKit_TexManager_starved());
    gsKit_TexManager_commitFrame(0);
    for (GSTEXTURE &t : requested)
        (void)gsKit_TexManager_bind(&global, &t);
    CHECK(!gsKit_TexManager_starved());
    gsKit_TexManager_commitFrame(1);
    for (GSTEXTURE &t : requested)
        CHECK(gsKit_TexManager_bind(&global, &t) != GSKIT_TEXMGR_BIND_DEFERRED);
    for (GSTEXTURE &t : displayed) releaseNow(&t);
    for (GSTEXTURE &t : requested) releaseNow(&t);

    // gsKit's framebuffer allocator reinitializes the texture manager; HIRES protection must survive it.
    reset(1);
    GSTEXTURE q = makeTexture(64, 64, true);
    gsKit_TexManager_bind(&global, &q);
    void *qMem = q.Mem;
    gsKit_TexManager_init(&global);
    gsKit_TexManager_release(&global, &q);
    CHECK(freed(qMem));
    gsKit_TexManager_flush();
    CHECK(freed(qMem));

    // Ordinary video modes keep immediate release and immediate VRAM reuse.
    reset(0);
    GSTEXTURE g = makeTexture(64, 64, true), h = makeTexture(64, 64);
    gsKit_TexManager_bind(&global, &g);
    u32 ordinaryVram = g.Vram;
    void *ordinaryMem = g.Mem, *ordinaryClut = g.Clut;
    gsKit_TexManager_release(&global, &g);
    CHECK(freed(ordinaryMem) && freed(ordinaryClut));
    gsKit_TexManager_bind(&global, &h);
    CHECK(h.Vram == ordinaryVram);
    releaseNow(&h);

    // Expired free blocks coalesce before allocation: six 512 KiB releases must satisfy a 1 MiB request.
    reset(1);
    std::vector<GSTEXTURE> fragments(6);
    for (GSTEXTURE &t : fragments) t = makeTexture(512, 512);
    for (GSTEXTURE &t : fragments)
        CHECK(gsKit_TexManager_bind(&global, &t) != GSKIT_TEXMGR_BIND_DEFERRED);
    gsKit_TexManager_commitFrame(1);
    for (GSTEXTURE &t : fragments) gsKit_TexManager_release(&global, &t);
    gsKit_TexManager_commitFrame(1);
    gsKit_TexManager_commitFrame(1); // expire the released blocks before the larger request
    GSTEXTURE merged = makeTexture(1024, 512);
    CHECK(gsKit_TexManager_bind(&global, &merged) != GSKIT_TEXMGR_BIND_DEFERRED);
    releaseNow(&merged);

    // Shutdown also releases source pixels immediately; no deferred bookkeeping remains.
    reset(1);
    GSTEXTURE z = makeTexture(64, 64, true);
    void *zMem = z.Mem, *zClut = z.Clut;
    gsKit_TexManager_bind(&global, &z);
    gsKit_TexManager_release(&global, &z);
    CHECK(freed(zMem) && freed(zClut));
    gsKit_TexManager_flush();
    CHECK(freed(zMem) && freed(zClut));
    return failures ? 1 : 0;
}
'''


def compile_and_run(folder, name, source):
    cpp = folder / f"{name}.cpp"
    exe = folder / name
    cpp.write_text(source, encoding="utf-8")
    built = subprocess.run(["g++", "-std=gnu++17", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-I", str(folder), "-I.", str(cpp), "-o", str(exe)], cwd=root, capture_output=True, text=True)
    if built.returncode:
        print(built.stderr)
        return 1
    ran = subprocess.run([str(exe)], capture_output=True, text=True)
    print(ran.stdout, end="")
    if ran.stderr:
        print(ran.stderr, end="", file=sys.stderr)
    return ran.returncode


with tempfile.TemporaryDirectory(prefix="hires-texture-ownership-") as tmp:
    folder = Path(tmp)
    (folder / "gsKit.h").write_text("")
    (folder / "gsTexManager.h").write_text("")
    (folder / "kernel.h").write_text("")
    results = [compile_and_run(folder, "cache-release", caller_program), compile_and_run(folder, "plasma-shutdown", plasma_program), compile_and_run(folder, "frame-end", frame_end_program)]
    manager = root / "src/gfx/texmanager.cpp"
    if manager.exists():
        results.append(compile_and_run(folder, "texture-manager", manager_program))
        previous_scan = '''        if (block->tex == NULL && _blockReclaimable(block) && block->iSize >= size)
            break;'''
        corrected_scan = '''        if (block->tex == NULL && _blockReclaimable(block)) {
            block = _blockMergeFree(block);
            if (block->iSize >= size)
                break;
        }'''
        manager_source = manager.read_text(encoding="utf-8")
        if manager_source.count(corrected_scan) != 1:
            print("FAIL: production allocation scan is not uniquely identifiable")
            results.append(1)
        else:
            previous_source = manager_source.replace(corrected_scan, previous_scan, 1)
            previous_program = manager_program.replace('#include "src/gfx/texmanager.cpp"', previous_source, 1)
            if compile_and_run(folder, "texture-manager-previous", previous_program) == 0:
                print("FAIL: previous allocation scan also satisfies the fragmented release case")
                results.append(1)
            else:
                print("PASS: previous allocation scan fails the fragmented release case")
    else:
        print("FAIL: production HIRES texture manager is absent; outgoing queues have no VRAM protection")
        results.append(1)
if any(results):
    sys.exit("FAIL: HIRES texture ownership or starvation recovery is unsafe")
print("PASS: direct pixel release, VRAM protection, starvation recovery, ordinary modes, and shutdown")
