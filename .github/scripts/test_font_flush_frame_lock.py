"""Font loads that flush the glyph atlases must do so as GUI-frame work (#297).

The glyph atlases are GS textures. gsKit's texture manager keeps every texture in one unlocked
global list that the GUI thread walks on each draw, and a frame's inline uploads reference the
atlas pixels until that frame (and, under HIRES, its replay) is retired. The theme loader honours
that in thmLoad: GUI lock, rmDiscardFrame(), then free. The font loaders did not: fntLoadFile,
fntLoadDefault, fntSetWeight and fntUpdateAspectRatio flushed EVERY font's atlases
(updateAspectRatioLocked -> cacheFlush -> Atlas::destroy -> rmUnloadTexture + free) under the
font lock alone. A saved disk theme found by the IO worker while the boot splash is drawing is
exactly that path, so the splash's own text atlases were torn down under the GUI thread.

Compile the production FontSystem loaders against a stub GUI/renderer that models the contract:
 - a texture may leave the texture manager only under the GUI lock, after the in-flight frame was
   retired with rmDiscardFrame();
 - the slow file read stays OUTSIDE the GUI lock (the splash keeps drawing while a font is read);
 - release() takes no GUI lock of its own: thmFree calls it already holding the lock.
The "before" control strips only the fix from the same production code and must fail.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gfx/fntsys.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')

members = ['lock', 'unlock', 'cacheFlush', 'deleteSlot', 'release', 'setWeight', 'loadSlotLocked',
           'loadFile', 'loadDefault', 'updateAspectRatio', 'updateAspectRatioLocked']
bodies = []
for name in members:
    match = re.search(r'^[A-Za-z_][\w \*&]*?FontSystem::' + name + r'\([^)]*\)\n\{.*?^\}\n', source, re.M | re.S)
    if match is None:
        print(f'FAIL: src/gfx/fntsys.cpp: FontSystem::{name}() not found')
        sys.exit(1)
    bodies.append(match.group(0))
helper = re.search(r'^struct FontFrameScope\b.*?^\};\n', source, re.M | re.S)
helper = helper.group(0) if helper else ''
production = helper + '\n' + '\n'.join(bodies)

fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#define LOG(...) ((void)0)
#define FNT_ERROR (-1)
#define FNT_DEFAULT 0
#define FNT_MAX_COUNT 4
#define FNTSYS_DEFAULT_SIZE 17
#define ATLAS_MAX 2

struct GSTEXTURE { void *Mem; int Vram; };
typedef struct FT_FaceRec_ { int size; } *FT_Face;
typedef struct FT_LibraryRec_ { int dummy; } *FT_Library;
typedef unsigned char FT_Byte;
static const float fDPI = 72.0f;

// --- the GUI thread, as the stubs see it -------------------------------------------------
static bool guiReady = true;    // false before guiInit: single-threaded, the GUI lock is a no-op
static bool guiLocked = false;  // the frame lock (non-recursive)
static bool frameRetired = false; // rmDiscardFrame() ran since the lock was taken
static bool fontLocked = false;
static int discards = 0, unloads = 0;
struct UI {
    void lock() { if (!guiReady) return; if (guiLocked) throw std::runtime_error("GUI lock taken twice (deadlock)"); guiLocked = true; }
    void unlock() { if (!guiReady) return; assert(guiLocked); guiLocked = false; frameRetired = false; }
};
namespace host { static UI instance; UI &ui() { return instance; } }
static void rmDiscardFrame(void) { if (guiReady) assert(guiLocked); frameRetired = true; ++discards; }
static void rmUnloadTexture(GSTEXTURE *) {
    ++unloads;
    if (!guiReady) return;
    if (!guiLocked) throw std::runtime_error("glyph atlas left the texture manager outside the GUI lock");
    if (!frameRetired) throw std::runtime_error("glyph atlas freed while the last frame's uploads can still reference it");
}
static void rmGetScreenExtents(int *w, int *h) { *w = 640; *h = 480; }
static void rmGetScreenExtentsNative(int *w, int *h) { *w = 640; *h = 448; }
static float rmGetPAR(void) { return 1.0f; }
static int fntScaleFontSize(int size) { return size; }
static int fntFaceSetWeight(FT_Library, FT_Face, int weight) { return weight; }
static int fntEmboldenFor(int, int) { return 0; }
static void WaitSema(int) { assert(!fontLocked); fontLocked = true; }
static void SignalSema(int) { assert(fontLocked); fontLocked = false; }
static int FT_New_Memory_Face(FT_Library, const FT_Byte *, int, int, FT_Face *face) { *face = new FT_FaceRec_{0}; return 0; }
static void FT_Done_Face(FT_Face face) { delete face; }
static void FT_Set_Char_Size(FT_Face face, int, int, float, float) { face->size++; }

struct Atlas {
    GSTEXTURE surface_;
    static Atlas *create() { Atlas *a = new Atlas; a->surface_.Mem = malloc(64); a->surface_.Vram = 1; return a; }
    static void destroy(Atlas *a) { if (a == NULL) return; rmUnloadTexture(&a->surface_); free(a->surface_.Mem); delete a; }
};

class FontSystem
{
public:
    typedef struct { int isValid; void *allocation; void *atlas; } fnt_glyph_cache_entry_t;
    typedef struct {
        fnt_glyph_cache_entry_t **glyphCache;
        int cacheMaxPageID;
        FT_Face face;
        int isValid;
        int fontSize;
        int weight, embolden;
        Atlas *atlases[ATLAS_MAX];
        void *dataPtr;
    } font_t;
    int loadFile(char *path, int fontSize);
    int loadDefault(char *path);
    void release(int id);
    void setWeight(int id, int weight);
    void updateAspectRatio();
    void lock();
    void unlock();
    void cacheFlush(font_t *font);
    void initSlot(font_t *font) { memset(font, 0, sizeof(*font)); font->cacheMaxPageID = -1; }
    void deleteSlot(font_t *font);
    int loadSlotLocked(font_t *font, void *buffer, int bufferSize, int owned, int fontSize);
    void updateAspectRatioLocked();
    FT_Library library_ = NULL;
    font_t fonts_[FNT_MAX_COUNT] = {};
    int fallbackFontId_ = FNT_ERROR;
    int semaId_ = 0;
};
typedef FontSystem::font_t font_t;
typedef FontSystem::fnt_glyph_cache_entry_t fnt_glyph_cache_entry_t;
static void fntCacheFlushPage(fnt_glyph_cache_entry_t *) {}
static unsigned char builtin[8];
// Stands in for fntReadData: the slow read must happen while the GUI thread can still draw.
static void *fntReadData(char *path, int *bufferSize, int *owned) {
    if (guiReady && guiLocked) throw std::runtime_error("font file read under the GUI lock");
    *owned = 0;
    if (path == NULL) { *bufferSize = sizeof(builtin); return builtin; }
    *owned = 1; *bufferSize = 32; return malloc(32);
}

@PRODUCTION@

// The splash (GUI thread) has drawn text: the default font owns live atlases and a glyph cache page.
static void cacheDrawn(FontSystem &fs, int id) {
    font_t *f = &fs.fonts_[id];
    f->atlases[0] = Atlas::create();
    f->glyphCache = (fnt_glyph_cache_entry_t **)calloc(1, sizeof(*f->glyphCache));
    f->glyphCache[0] = (fnt_glyph_cache_entry_t *)calloc(1, sizeof(fnt_glyph_cache_entry_t));
    f->cacheMaxPageID = 0;
}
static void idle() { assert(!guiLocked && !fontLocked); }

int main() {
    FontSystem fs;
    for (font_t &slot : fs.fonts_) // FontSystem::init
        fs.initSlot(&slot);
    char themeFont[] = "mass0:/THM/thm_test/font.ttf";
    try {
        // Pre-GUI: lngInit/thmInit load the default font before guiInit; nothing may wait or assert.
        guiReady = false;
        assert(fs.loadDefault(NULL) == 0);
        guiReady = true;

        // Boot: the splash is drawing with font 0 while the IO worker finds the saved theme's fonts.
        cacheDrawn(fs, FNT_DEFAULT);
        int id = fs.loadFile(themeFont, 17);
        assert(id != FNT_ERROR); idle();
        assert(fs.fonts_[FNT_DEFAULT].atlases[0] == NULL); // the flush really ran
        cacheDrawn(fs, FNT_DEFAULT); cacheDrawn(fs, id);
        fs.setWeight(id, 700); idle();
        cacheDrawn(fs, FNT_DEFAULT);
        fs.updateAspectRatio(); idle();
        cacheDrawn(fs, FNT_DEFAULT);
        assert(fs.loadDefault(themeFont) == 0); idle();

        // thmFree: release() runs under the caller's GUI lock, after its own rmDiscardFrame.
        cacheDrawn(fs, id);
        host::ui().lock(); rmDiscardFrame();
        fs.release(id);
        host::ui().unlock(); idle();
        assert(!fs.fonts_[id].isValid);
        assert(unloads > 0 && discards > 0);
    } catch (const std::runtime_error &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 3;
    }
    std::puts("ok");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='font-flush-frame-lock-') as folder:
    folder = Path(folder)
    # The control strips only the fix (the scope object) from the production loaders. Run it first:
    # a fixture that cannot catch the unguarded flush proves nothing about the fixed one.
    before = re.sub(r'^\s*FontFrameScope \w+;\n', '', production, flags=re.M)
    for name, code, expected in [('before', before, 3), ('fixed', production, 0)]:
        cpp = folder / (name + '.cpp')
        exe = folder / name
        cpp.write_text(fixture.replace('@PRODUCTION@', code), encoding='utf-8')
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', str(cpp), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        if name == 'before' and 'outside the GUI lock' not in result.stderr:
            print(f'FAIL: the control did not trip on the unguarded flush (exit {result.returncode}):\n{result.stderr}')
            sys.exit(1)
        if result.returncode != expected:
            print(f'FAIL: {name}: exit {result.returncode}, expected {expected}\n{result.stderr}')
            sys.exit(1)
        if name == 'before' and before == production:
            print('FAIL: the font loaders take no FontFrameScope (GUI lock + rmDiscardFrame) around their atlas flush')
            sys.exit(1)
print('PASS: font loads flush glyph atlases under the GUI lock after retiring the frame; the file read stays outside it; release() does not re-lock; the before-control is caught')
