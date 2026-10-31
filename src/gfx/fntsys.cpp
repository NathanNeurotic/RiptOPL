/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

#include "include/opl.h"
#include "include/fntsys.h"
#include "include/renderman.h"
#include "include/ioman.h"
#include "include/utf8.h"
#include "include/util.h"
#include "include/atlas.h"
#include "include/fontsystem.h"
#include "include/textshape.h"

#include <sys/types.h>
#include <time.h> // clock(): fntRenderStringFit's scroll timing
#include <ft2build.h>

#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_MULTIPLE_MASTERS_H // a variable face's weight
#include FT_TRUETYPE_TABLES_H  // a face's own weight (OS/2)
#include FT_OUTLINE_H          // a static face drawn heavier

// FT_Init_FreeType and FT_Done_FreeType sit in ftinit.c beside the table of every module the library was
// built with, so calling either links all of them (Type 1, CID, PCF, BDF, PFR, Windows FNT, SDF, SVG...).
// Fonts here are TrueType and OpenType (.ttf/.otf, docs/INTERFACE.md), so register only what those need.
// Ported from RIPPS2 build 114 (akilluminati47/RIPPS2, fntsys.c fntNewLibrary).
extern "C" {
extern const FT_Module_Class autofit_module_class;
extern const FT_Module_Class tt_driver_class;
extern const FT_Module_Class cff_driver_class;
extern const FT_Module_Class psaux_module_class;    // CFF needs psaux, psnames and pshinter
extern const FT_Module_Class psnames_module_class;
extern const FT_Module_Class pshinter_module_class;
extern const FT_Module_Class sfnt_module_class;
extern const FT_Module_Class ft_smooth_renderer_class;
FT_Memory FT_New_Memory(void);         // ftsystem.c, as FT_Init_FreeType uses it
void FT_Done_Memory(FT_Memory memory); // and as FT_Done_FreeType frees it
}

static FT_Memory fntMemory; // FT_LibraryRec is opaque outside FreeType, so remember it for fntDoneLibrary

static FT_Error fntNewLibrary(FT_Library *library)
{
    static const FT_Module_Class *const modules[] = {
        &autofit_module_class, &tt_driver_class, &cff_driver_class, &psaux_module_class,
        &psnames_module_class, &pshinter_module_class, &sfnt_module_class, &ft_smooth_renderer_class};
    FT_Memory memory = FT_New_Memory();
    FT_Error error;

    if (memory == NULL)
        return FT_Err_Out_Of_Memory;
    if ((error = FT_New_Library(memory, library)) != 0) {
        FT_Done_Memory(memory);
        return error;
    }
    for (const FT_Module_Class *module : modules) {
        if ((error = FT_Add_Module(*library, module)) != 0) {
            FT_Done_Library(*library);
            *library = NULL; // fntEnd must not free it again
            FT_Done_Memory(memory);
            return error;
        }
    }
    // No FT_Set_Default_Properties: it is in ftinit.c too, and only reads a FREETYPE_PROPERTIES
    // environment variable a PS2 never has.
    fntMemory = memory;
    return 0;
}

static void fntDoneLibrary(FT_Library library)
{
    FT_Done_Library(library);
    if (fntMemory != NULL)
        FT_Done_Memory(fntMemory);
    fntMemory = NULL;
}

typedef FontSystem::font_t font_t;
typedef FontSystem::fnt_glyph_cache_entry_t fnt_glyph_cache_entry_t;

extern void *poeveticanew_raw;
extern int size_poeveticanew_raw;


static const float fDPI = 72.0f;


/* ONE LOCK FOR ALL FONT STATE, HELD FOR THE WHOLE OPERATION.

   The slots, their FreeType faces, the glyph caches and atlases, and the render scratch globals
   below are shared by the GUI thread, which draws text every frame, and the IO worker, which loads
   theme and language fonts (thmAddElements -> thmLoad -> thmLoadFonts runs from a device's
   NeedsUpdate). Every load ends in fntUpdateAspectRatio, which FREES every font's glyph cache and
   atlases and resizes every face.

   Readers used to take this semaphore only to fetch &fonts_[id] and then render unlocked, so the
   worker could flush the caches under a glyph the GUI thread was drawing: a use-after-free that
   froze the GUI thread mid-frame. At boot that is the configured theme being found on a storage
   device that comes up late (BDM HDD, after the ATA settle) while the splash is drawing its status
   line -- a random hang with "Loading storage drivers..." frozen on screen.

   Public entry points take the lock; the static helpers assume it is held. File reads stay outside
   it, so the GUI keeps drawing while a font is read from a slow device. */
void FontSystem::lock(void)
{
    WaitSema(semaId_);
}

void FontSystem::unlock(void)
{
    SignalSema(semaId_);
}


static inline bool glyphPageOk(const font_t *font, int page)
{
    return (page <= font->cacheMaxPageID) && (font->glyphCache[page]);
}

static void fntCacheFlushPage(fnt_glyph_cache_entry_t *page)
{
    int i;

    for (i = 0; i < GLYPH_CACHE_PAGE_SIZE; ++i, ++page) {
        page->isValid = 0;
        // we're not doing any atlasFree or such - atlas has to be rebuild
        page->allocation = NULL;
        page->atlas = NULL;
    }
}

void FontSystem::cacheFlush(font_t *font)
{
    // Release all the glyphs from the cache
    int i;
    for (i = 0; i <= font->cacheMaxPageID; ++i) {
        if (font->glyphCache[i]) {
            fntCacheFlushPage(font->glyphCache[i]);
            free(font->glyphCache[i]);
            font->glyphCache[i] = NULL;
        }
    }

    free(font->glyphCache);
    font->glyphCache = NULL;
    font->cacheMaxPageID = -1;

    // free all atlasses too, they're invalid now anyway
    int aid;
    for (aid = 0; aid < ATLAS_MAX; ++aid) {
        Atlas::destroy(font->atlases[aid]);
        font->atlases[aid] = NULL;
    }
}

int FontSystem::prepareGlyphCachePage(font_t *font, int pageid)
{
    if (pageid > font->cacheMaxPageID) {
        fnt_glyph_cache_entry_t **np = static_cast<fnt_glyph_cache_entry_t **>(realloc(font->glyphCache, (pageid + 1) * sizeof(fnt_glyph_cache_entry_t *)));

        if (!np)
            return 0;

        font->glyphCache = np;

        unsigned int page;
        for (page = font->cacheMaxPageID + 1; page <= pageid; ++page)
            font->glyphCache[page] = NULL;

        font->cacheMaxPageID = pageid;
    }

    // if it already was allocated, skip this
    if (font->glyphCache[pageid])
        return 1;

    // allocate the page
    font->glyphCache[pageid] = static_cast<fnt_glyph_cache_entry_t *>(malloc(sizeof(fnt_glyph_cache_entry_t) * GLYPH_CACHE_PAGE_SIZE));
    if (!font->glyphCache[pageid])
        return 0; // out of memory: the caller treats this as "page not available" (it used to dereference NULL)

    int i;
    for (i = 0; i < GLYPH_CACHE_PAGE_SIZE; ++i) {
        font->glyphCache[pageid][i].isValid = 0;
        font->glyphCache[pageid][i].atlas = NULL;
        font->glyphCache[pageid][i].allocation = NULL;
    }

    return 1;
}

void FontSystem::prepareCLUT()
{
    clut_.PSM = GS_PSM_T8;
    clut_.ClutPSM = GS_PSM_CT32;
    clut_.Clut = static_cast<u32 *>(memalign(128, 256 * 4));
    clut_.VramClut = 0;

    // generate the clut table
    size_t i;
    u32 *clut = clut_.Clut;
    for (i = 0; i < 256; ++i) {
        u8 alpha = (i * 128) / 255;

        *clut = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, alpha);
        clut++;
    }
}

void FontSystem::destroyCLUT()
{
    free(clut_.Clut);
    clut_.Clut = NULL;
}

// A face drawn at a weight (RIPPS2 build 114, akilluminati47). A variable face (a wght axis) takes it as designed,
// within its range; a face asked to be heavier than it can be (a static face, or past a variable one's range) has its
// outlines grown in code, about 1 px at 18 px for 300 weight units.
static int fntFaceOwnWeight(FT_Face face)
{
    TT_OS2 *os2 = face != NULL ? (TT_OS2 *)FT_Get_Sfnt_Table(face, FT_SFNT_OS2) : NULL;
    return os2 != NULL && os2->usWeightClass >= 100 && os2->usWeightClass <= 1000 ? os2->usWeightClass : 400;
}

static int fntFaceSetWeight(FT_Library library, FT_Face face, int weight) // the weight the face now draws at
{
    FT_MM_Var *mm = NULL;
    FT_Fixed coords[8];
    unsigned i, n;
    int drawn = fntFaceOwnWeight(face);

    if (face == NULL || !FT_HAS_MULTIPLE_MASTERS(face) || FT_Get_MM_Var(face, &mm) != 0)
        return drawn;
    n = mm->num_axis < 8 ? mm->num_axis : 8;
    for (i = 0; i < n; i++) {
        coords[i] = mm->axis[i].def;
        if (mm->axis[i].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
            FT_Fixed w = weight > 0 ? (FT_Fixed)weight << 16 : mm->axis[i].def;
            if (w < mm->axis[i].minimum)
                w = mm->axis[i].minimum;
            if (w > mm->axis[i].maximum)
                w = mm->axis[i].maximum;
            coords[i] = w;
            drawn = (int)(w >> 16);
        }
    }
    if (FT_Set_Var_Design_Coordinates(face, n, coords) != 0)
        drawn = fntFaceOwnWeight(face);
    FT_Done_MM_Var(library, mm);
    return drawn;
}

// How far a glyph's outline grows, in 26.6 units at 18 px, to draw `weight` on a face that draws `drawn`
static int fntEmboldenFor(int weight, int drawn)
{
    int e = weight > drawn ? (weight - drawn) * 64 / 300 : 0;
    return e > 96 ? 96 : e;
}

// Load and render one glyph, grown by `embolden` (fntEmboldenFor) when it is above 0
static int fntLoadGlyph(FT_Face face, uint32_t gid, int embolden)
{
    int error;

    if (embolden <= 0)
        return FT_Load_Char(face, gid, FT_LOAD_RENDER);
    error = FT_Load_Char(face, gid, FT_LOAD_DEFAULT);
    if (!error && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_Pos s = (FT_Pos)embolden * (face->size != NULL ? face->size->metrics.y_ppem : 18) / 18;
        FT_Outline_EmboldenXY(&face->glyph->outline, s, s / 2);
        face->glyph->advance.x += s;
    }
    if (!error)
        error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
    return error;
}

void FontSystem::initSlot(font_t *font)
{
    font->face = NULL;
    font->glyphCache = NULL;
    font->cacheMaxPageID = -1;
    font->dataPtr = NULL;
    font->isValid = 0;
    font->fontSize = 0;
    font->weight = font->embolden = 0;

    int aid = 0;
    for (; aid < ATLAS_MAX; ++aid)
        font->atlases[aid] = NULL;
}

void FontSystem::deleteSlot(font_t *font)
{
    // free the glyph cache, atlases, unload the font
    cacheFlush(font);

    FT_Done_Face(font->face);
    font->face = NULL;

    if (font->dataPtr) {
        free(font->dataPtr);
        font->dataPtr = NULL;
    }

    if (fallbackFontId_ != FNT_ERROR && font == &fonts_[fallbackFontId_])
        fallbackFontId_ = FNT_ERROR;

    font->isValid = 0;
    font->fontSize = 0;
}

void FontSystem::release(int id)
{
    if (id > FNT_DEFAULT && id < FNT_MAX_COUNT) {
        lock();
        deleteSlot(&fonts_[id]);
        unlock();
    }
}

// Draw a theme's font at a weight, 100 to 900 (0: the face's own). Its cached glyphs go, so the next frame redraws them.
void FontSystem::setWeight(int id, int weight)
{
    if (id <= FNT_DEFAULT || id >= FNT_MAX_COUNT)
        return;
    lock();
    font_t *font = &fonts_[id];
    if (font->isValid && font->face != NULL && font->weight != weight) {
        font->weight = weight;
        font->embolden = fntEmboldenFor(weight, fntFaceSetWeight(library_, font->face, weight));
        cacheFlush(font);
    }
    unlock();
}

// The slow half of a load, done WITHOUT the lock: read the font file (or point at the built-in
// one). *owned is set when the buffer was allocated here and must end up in font->dataPtr.
static void *fntReadData(char *path, int *bufferSize, int *owned)
{
    void *buffer;

    *owned = 0;
    *bufferSize = -1; // readFile() takes a positive incoming size as the exact expected file size
    if (path == NULL) {
        *bufferSize = size_poeveticanew_raw;
        return &poeveticanew_raw;
    }

    buffer = readFile(path, -1, bufferSize);
    if (!buffer) {
        LOG("FNTSYS Font file loading failed: %s\n", path);
        return NULL;
    }
    *owned = 1;
    return buffer;
}

// Lock held. Takes ownership of an owned buffer (freed with the slot, including on failure).
int FontSystem::loadSlotLocked(font_t *font, void *buffer, int bufferSize, int owned, int fontSize)
{
    initSlot(font);
    if (owned)
        font->dataPtr = buffer;

    // load the font via memory handle
    int error = FT_New_Memory_Face(library_, (FT_Byte *)buffer, bufferSize, 0, &font->face);
    if (error) {
        LOG("FNTSYS Freetype font loading failed with %x!\n", error);
        deleteSlot(font);
        return FNT_ERROR;
    }

    font->isValid = 1;
    font->fontSize = fontSize;
    updateAspectRatioLocked();

    return 0;
}

void FontSystem::init()
{
    LOG("FNTSYS Init\n");
    int error = fntNewLibrary(&library_);
    if (error) {
        // just report over the ps2link
        LOG("FNTSYS Freetype init failed with %x!\n", error);
        return;
    }

    prepareCLUT();

    ee_sema_t sema;
    sema.init_count = 1;
    sema.max_count = 1;
    sema.option = 0;
    semaId_ = CreateSema(&sema);

    int i = 0;
    for (; i < FNT_MAX_COUNT; ++i)
        initSlot(&fonts_[i]);
    fallbackFontId_ = FNT_ERROR;

    loadDefault(NULL);
}

// Keep this font independent of language and theme switching. Missing optional
// font files are harmless: loadFile leaves the current default untouched.
int FontSystem::loadFallback(char *path)
{
    if (path == NULL)
        return FNT_ERROR;

    // Called during language initialization, before theme files are admitted.
    if (fallbackFontId_ != FNT_ERROR)
        return 0;

    int id = loadFile(path, FNTSYS_DEFAULT_SIZE);
    if (id == FNT_ERROR)
        return FNT_ERROR;

    lock();
    fallbackFontId_ = id;
    unlock();
    return 0;
}

int FontSystem::loadFile(char *path, int fontSize)
{
    int bufferSize, owned, i, result = FNT_ERROR;
    void *buffer = fntReadData(path, &bufferSize, &owned);

    if (buffer == NULL)
        return FNT_ERROR;

    lock();
    for (i = 1; i < FNT_MAX_COUNT; i++) {
        if (!fonts_[i].isValid) {
            if (loadSlotLocked(&fonts_[i], buffer, bufferSize, owned, fontSize) != FNT_ERROR)
                result = i;
            owned = 0; // the slot owns (or already freed) it either way
            break;
        }
    }
    unlock();

    if (owned) // no free slot
        free(buffer);

    return result;
}

int FontSystem::loadDefault(char *path)
{
    font_t newFont, oldFont;
    int bufferSize, owned, result = -1;
    void *buffer = fntReadData(path, &bufferSize, &owned);

    if (buffer == NULL)
        return -1;

    // The swap and the delete of the old font happen under the same lock the renderers hold, so no
    // draw can be using the old face, cache or atlases when they go.
    lock();
    if (loadSlotLocked(&newFont, buffer, bufferSize, owned, FNTSYS_DEFAULT_SIZE) != FNT_ERROR) {
        memcpy(&oldFont, &fonts_[FNT_DEFAULT], sizeof(font_t));
        memcpy(&fonts_[FNT_DEFAULT], &newFont, sizeof(font_t));
        deleteSlot(&oldFont);
        result = 0;
    }
    unlock();

    return result;
}

void FontSystem::end()
{
    LOG("FNTSYS End\n");
    // release all the fonts
    int id;
    lock();
    for (id = 0; id < FNT_MAX_COUNT; ++id)
        deleteSlot(&fonts_[id]);
    fallbackFontId_ = FNT_ERROR;
    unlock();

    // deinit freetype system
    fntDoneLibrary(library_);

    DeleteSema(semaId_);

    destroyCLUT();
}

Atlas *FontSystem::newAtlas()
{
    Atlas *atl = Atlas::create(ATLAS_WIDTH, ATLAS_HEIGHT, GS_PSM_T8);
    if (!atl)
        return NULL; // out of memory (this used to be dereferenced unchecked)

    GSTEXTURE *surface = atl->texture();
    surface->ClutPSM = GS_PSM_CT32;
    surface->Clut = clut_.Clut;

    return atl;
}

int FontSystem::glyphAtlasPlace(font_t *font, fnt_glyph_cache_entry_t *glyph)
{
    FT_GlyphSlot slot = font->face->glyph;

    // LOG("FNTSYS GlyphAtlasPlace: Placing the glyph... %d x %d\n", slot->bitmap.width, slot->bitmap.rows);

    if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) {
        // no bitmap glyph, just skip
        return 1;
    }

    int aid = 0;
    for (; aid < ATLAS_MAX; aid++) {
        // LOG("FNTSYS Placing aid %d...\n", aid);
        Atlas **atl = &font->atlases[aid];
        if (!*atl) { // atlas slot not yet used
            // LOG("FNTSYS aid %d is new...\n", aid);
            *atl = newAtlas();
            if (!*atl)
                return 0;
        }

        glyph->allocation = (*atl)->place(slot->bitmap.width, slot->bitmap.rows, slot->bitmap.buffer);
        if (glyph->allocation) {
            // LOG("FNTSYS Found placement\n", aid);
            glyph->atlas = *atl;

            return 1;
        }
    }

    LOG("FNTSYS No atlas free\n", aid);
    return 0;
}

/** Internal method. Makes sure the bitmap data for particular character are pre-rendered to the glyph cache */
fnt_glyph_cache_entry_t *FontSystem::cacheGlyph(font_t *font, uint32_t gid)
{
    // Select a second face only for characters missing from the active font.
    // The font mutex is held by every caller, including measurement. No I/O
    // and no font switching occurs on the frame-rendering path.
    if (gid >= 0x80 && fallbackFontId_ > FNT_DEFAULT &&
        fallbackFontId_ < FNT_MAX_COUNT && font != &fonts_[fallbackFontId_] &&
        fonts_[fallbackFontId_].isValid && fonts_[fallbackFontId_].face != NULL &&
        font->face != NULL && FT_Get_Char_Index(font->face, gid) == 0 &&
        FT_Get_Char_Index(fonts_[fallbackFontId_].face, gid) != 0)
        font = &fonts_[fallbackFontId_];

    // calc page id and in-page index from glyph id
    int pageid = gid / GLYPH_CACHE_PAGE_SIZE;
    int idx = gid % GLYPH_CACHE_PAGE_SIZE;

    // do not call on every char of every font rendering call
    if (!glyphPageOk(font, pageid))
        if (!prepareGlyphCachePage(font, pageid)) // failed to prepare the page...
            return NULL;

    fnt_glyph_cache_entry_t *page = font->glyphCache[pageid];
    /* Should never happen.
    if (!page) // safeguard
        return NULL;
    */

    fnt_glyph_cache_entry_t *glyph = &page[idx];
    if (glyph->isValid)
        return glyph;

    // not cached but valid. Cache
    if (!font->face) {
        LOG("FNTSYS Face is NULL!\n");
    }

    int error = fntLoadGlyph(font->face, gid, font->embolden);
    if (error) {
        LOG("FNTSYS Error loading glyph - %d\n", error);
        return NULL;
    }

    // find atlas placement for the glyph
    if (!glyphAtlasPlace(font, glyph))
        return NULL;

    FT_GlyphSlot slot = font->face->glyph;
    glyph->width = slot->bitmap.width;
    glyph->height = slot->bitmap.rows;
    glyph->shx = slot->advance.x;
    glyph->shy = slot->advance.y;
    glyph->ox = slot->bitmap_left;
    glyph->oy = -slot->bitmap_top;

    glyph->isValid = 1;

    return glyph;
}

void FontSystem::updateAspectRatio()
{
    lock();
    updateAspectRatioLocked();
    unlock();
}

void FontSystem::updateAspectRatioLocked(void)
{
    int i;
    int w, h, wn, hn;
    float ws, hs;

    rmGetScreenExtents(&w, &h);
    rmGetScreenExtentsNative(&wn, &hn);
    // Scale height from virtual resolution (640x480) to the native display resolution
    hs = (float)hn / (float)h;
    // Scale width according to the PAR (Pixel Aspect Ratio)
    ws = hs * rmGetPAR();

    // NOTE: interlaced FRAME modes (1080i, the 480i/576i flicker-free rows) used to supersample
    // the glyph bitmaps 2x vertically here (hs *= 2) and squash the quad back down at draw time.
    // Removed: the atlas samples with GS_FILTER_NEAREST (atlas.c), so that 2:1 minification just
    // drops every other glyph row -- at a 224-line framebuffer the 17px main font lost whole
    // strokes and rendered blank/half-readable (issue #615). At native height the path is
    // numerically identical to the known-good 640x224p/640x256p rows, and a FRAME display can
    // only show the halved line count anyway, so the extra texels were never displayable.
    // flush cache - it will be invalid after the setting
    for (i = 0; i < FNT_MAX_COUNT; i++) {
        if (fonts_[i].isValid) {
            cacheFlush(&fonts_[i]);
            // TODO: this seems correct, but the rest of the OPL UI (i.e. spacers) doesn't seem to be correctly scaled.
            int size = fntScaleFontSize(fonts_[i].fontSize);
            FT_Set_Char_Size(fonts_[i].face, size * 64, size * 64, fDPI * ws, fDPI * hs);
        }
    }
}

void FontSystem::renderGlyph(GlyphRun &run, fnt_glyph_cache_entry_t *glyph, int pen_x, int pen_y)
{
    // only if glyph has atlas placement
    if (glyph->allocation) {
        /* TODO: Ineffective on many parts:
         * 1. Usage of floats for UV - fixed point should suffice (and is used internally by GS for UV)
         *
         * 2. GS_SETREG_TEX0 for every run.quad - why? gsKit should only set texture if demanded
         *    We should prepare a special fnt render method that would step over most of the
         *    performance problems under - beginning with rmSetupQuad and continuing into gsKit
         *    - this method would handle the preparation of the quads and GS upload itself,
         *    without the use of prim_quad_texture and rmSetupQuad...
         */
        run.quad.ul.x = pen_x + glyph->ox;
        run.quad.ul.y = pen_y + glyph->oy;
        run.quad.ul.u = glyph->allocation->x;
        run.quad.ul.v = glyph->allocation->y;

        run.quad.br.x = run.quad.ul.x + glyph->width;
        run.quad.br.y = run.quad.ul.y + glyph->height;
        run.quad.br.u = run.quad.ul.u + glyph->width;
        run.quad.br.v = run.quad.ul.v + glyph->height;

        run.quad.txt = glyph->atlas->texture();

        rmDrawQuad(&run.quad);
    }
}


#ifndef __RTL
int FontSystem::renderString(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, u64 colour)
{
    if (string == NULL)
        return 0;
    lock();
    font_t *font = &fonts_[id];
    GlyphRun run;
    fnt_glyph_cache_entry_t *glyph;

    // Convert to native display resolution
    x = rmScaleX(x);
    y = rmScaleY(y);
    width = rmScaleX(width);
    height = rmScaleY(height);

    if (aligned & ALIGN_HCENTER) {
        if (width) {
            x -= min(calcDimensionsLocked(font, string), width) >> 1;
        } else {
            x -= calcDimensionsLocked(font, string) >> 1;
        }
    }

    int fontSize = fntScaleFontSize(fonts_[id].fontSize);
    if (aligned & ALIGN_VCENTER) {
        y += rmScaleY(fontSize - 4) >> 1;
    } else {
        y += rmScaleY(fontSize - 2);
    }

    run.quad.color = colour;

    // Arabic and Persian (RIPPS2 build 104): their letters joined and each line laid out right to left, then drawn
    // from the left like any other text. Measured above from the same joined letters, so a centred line stays centred.
    static char shaped[2048]; // under the font lock
    const char *visual = txtShape(string, shaped, sizeof(shaped), 1);
    if (visual != NULL)
        string = visual;

    int pen_x = x;
    int xmax = x + width;
    int ymax = y + height;

    run.use_kerning = FT_HAS_KERNING(font->face);
    run.state = UTF8_ACCEPT;
    run.previous = 0;

    // Note: We need to change this so that we'll accumulate whole word before doing a layout with it
    // for now this method breaks on any character - which is a bit ugly

    // I don't want to do anything complicated though so I'd say
    // we should instead have a dynamic layout routine that'll replace spaces with newlines as appropriate
    // because that'll make the code run only once per N frames, not every frame

    // cache glyphs and render as we go
    for (; *string; ++string) {
        if (utf8Decode(&run.state, &run.codepoint, *string)) // accumulate the run.codepoint value
            continue;
        if (txtZeroWidth(run.codepoint)) // the zero-width marks some translations carry draw nothing (RIPPS2 build 104)
            continue;

        glyph = cacheGlyph(font, run.codepoint);
        if (!glyph)
            continue;

        // kerning
        if (run.use_kerning && run.previous) {
            run.glyph_index = FT_Get_Char_Index(font->face, run.codepoint);
            if (run.glyph_index) {
                FT_Get_Kerning(font->face, run.previous, run.glyph_index, FT_KERNING_DEFAULT, &run.delta);
                pen_x += run.delta.x >> 6;
            }
            run.previous = run.glyph_index;
        }

        if (width) {
            if (run.codepoint == '\n') {
                pen_x = x;
                y += rmScaleY(MENU_ITEM_HEIGHT); // hmax is too tight and unordered, generally
                continue;
            }

            if (y > ymax) // stepped over the max
                break;

            if (pen_x + glyph->width > xmax) {
                pen_x = xmax + 1; // to be sure no other cahr will be written (even not a smaller one just following)
                continue;
            }
        }

        renderGlyph(run, glyph, pen_x, y);
        pen_x += glyph->shx >> 6;
    }

    unlock();
    return rmUnScaleX(pen_x);
}

#else
static int isRTL(u32 character)
{
    return (((character >= 0x00000590 && character <= 0x000008FF) || (character >= 0x0000FB50 && character <= 0x0000FDFF) || (character >= 0x0000FE70 && character <= 0x0000FEFF) || (character >= 0x00010800 && character <= 0x00010FFF) || (character >= 0x0001E800 && character <= 0x0001EFFF)) ? 1 : 0);
}

static int isWeak(u32 character)
{
    return (((character >= 0x0000 && character <= 0x0040) || (character >= 0x005B && character <= 0x0060) || (character >= 0x007B && character <= 0x00BF) || (character >= 0x00D7 && character <= 0x00F7) || (character >= 0x02B9 && character <= 0x02FF) || (character >= 0x2000 && character <= 0x2BFF)) ? 1 : 0);
}

void FontSystem::renderSubRTL(GlyphRun &run, font_t *font, const char *startRTL, const char *string, fnt_glyph_cache_entry_t *glyph, int x, int y)
{
    if (glyph) {
        x -= glyph->shx >> 6;
        renderGlyph(run, glyph, x, y);
    }

    for (; startRTL != string; ++startRTL) {
        if (utf8Decode(&run.state, &run.codepoint, *startRTL))
            continue;
        if (txtZeroWidth(run.codepoint))
            continue;

        glyph = cacheGlyph(font, run.codepoint);
        if (!glyph)
            continue;

        if (run.use_kerning && run.previous) {
            run.glyph_index = FT_Get_Char_Index(font->face, run.codepoint);
            if (run.glyph_index) {
                FT_Get_Kerning(font->face, run.previous, run.glyph_index, FT_KERNING_DEFAULT, &run.delta);
                x -= run.delta.x >> 6;
            }
            run.previous = run.glyph_index;
        }

        x -= glyph->shx >> 6;
        renderGlyph(run, glyph, x, y);
    }
}

int FontSystem::renderString(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, u64 colour)
{
    if (string == NULL)
        return 0;
    lock();
    font_t *font = &fonts_[id];
    GlyphRun run;
    fnt_glyph_cache_entry_t *glyph;

    // Convert to native display resolution
    x = rmScaleX(x);
    y = rmScaleY(y);
    width = rmScaleX(width);
    height = rmScaleY(height);

    if (aligned & ALIGN_HCENTER) {
        if (width) {
            x -= min(calcDimensionsLocked(font, string), width) >> 1;
        } else {
            x -= calcDimensionsLocked(font, string) >> 1;
        }
    }

    int fontSize = fntScaleFontSize(fonts_[id].fontSize);
    if (aligned & ALIGN_VCENTER) {
        y += rmScaleY(fontSize - 4) >> 1;
    } else {
        y += rmScaleY(fontSize - 2);
    }

    run.quad.color = colour;

    int pen_x = x;
    int xmax = x + width;
    int ymax = y + height;

    run.use_kerning = FT_HAS_KERNING(font->face);
    run.state = UTF8_ACCEPT;
    run.previous = 0;

    short inRTL = 0;
    int delta_x, pen_xRTL = 0;
    fnt_glyph_cache_entry_t *glyphRTL = NULL;
    const char *startRTL = NULL;

    // cache glyphs and render as we go
    for (; *string; ++string) {
        if (utf8Decode(&run.state, &run.codepoint, *string)) // accumulate the run.codepoint value
            continue;
        if (txtZeroWidth(run.codepoint)) // the zero-width marks some translations carry draw nothing (RIPPS2 build 104)
            continue;

        glyph = cacheGlyph(font, run.codepoint);
        if (!glyph)
            continue;

        // kerning
        delta_x = 0;
        if (run.use_kerning && run.previous) {
            run.glyph_index = FT_Get_Char_Index(font->face, run.codepoint);
            if (run.glyph_index) {
                FT_Get_Kerning(font->face, run.previous, run.glyph_index, FT_KERNING_DEFAULT, &run.delta);
                delta_x = run.delta.x >> 6;
            }
            run.previous = run.glyph_index;
        }


        if (width) {
            if (run.codepoint == '\n') {
                pen_x = x;
                y += rmScaleY(MENU_ITEM_HEIGHT); // hmax is too tight and unordered, generally
                continue;
            }

            if ((pen_x + glyph->width > xmax) || (y > ymax)) // stepped over the max
                break;
        }

        if (isRTL(run.codepoint)) {
            if (!inRTL && !isWeak(run.codepoint)) {
                inRTL = 1;
                pen_xRTL = pen_x;
                glyphRTL = glyph;
                startRTL = string + 1;
            }
        } else {
            if (inRTL && !isWeak(run.codepoint)) { // A LTR character is encountered. Render RTL characters before continuing.
                inRTL = 0;
                pen_x = pen_xRTL;
                renderSubRTL(run, font, startRTL, string, glyphRTL, pen_xRTL, y);
            }
        }

        if (inRTL) {
            pen_xRTL += delta_x + (glyph->shx >> 6);
        } else {
            pen_x += delta_x;
            renderGlyph(run, glyph, pen_x, y);
            pen_x += glyph->shx >> 6;
        }
    }

    if (inRTL) {
        pen_x = pen_xRTL;
        renderSubRTL(run, font, startRTL, string, glyphRTL, pen_xRTL, y);
    }

    unlock();
    return rmUnScaleX(pen_x);
}
#endif

// Draw `string` in a window `room` units wide (the same unscaled units as x), starting at x -- or centred
// on x when `aligned` has ALIGN_HCENTER, exactly where fntRenderString would centre it. Text that fits is
// drawn exactly as fntRenderString draws it. Text that does not: with `scroll` it scrolls through the whole
// string, one character per FNT_MARQUEE_STEP_MS, holding FNT_MARQUEE_PAUSE steps at each end; without it,
// it is cut to end in "...". Returns the end x, like fntRenderString.
//
// Shared by the settings dialogs (dia.c, #732: values ran off the edge on 4:3) and theme text (themes.c),
// which stops long text at the next theme element instead of drawing over it.
#define FNT_MARQUEE_STEP_MS 200 // one character per step
#define FNT_MARQUEE_PAUSE   5   // steps held at each end

static int fntTextWidth(int id, const char *text)
{
    return rmUnScaleX(fntCalcDimensions(id, text));
}

int fntRenderStringFit(int id, int x, int y, short aligned, int room, const char *string, u64 colour, int scroll)
{
    short at[256]; // byte offset where each character starts, then the end
    char buf[256];
    int n = 0, i, lo, hi, mid;

    if (string == NULL || room <= 0) // no room at all: draw nothing rather than run past the limit
        return x;
    if (fntTextWidth(id, string) <= room)
        return fntRenderString(id, x, y, aligned, 0, 0, string, colour);

    // Too long: the window itself is what gets placed. fntRenderString centres by the drawn width, so a
    // centred element's window is centred on x; the text then runs left to right inside it.
    if (aligned & ALIGN_HCENTER)
        x -= room / 2;
    aligned &= ~(ALIGN_HCENTER | ALIGN_RIGHT);

    for (i = 0; string[i] != '\0'; i++) {
        if ((string[i] & 0xC0) == 0x80) // UTF-8 continuation byte
            continue;
        if (n == 255)
            break;
        at[n++] = i;
    }
    at[n] = i;

    // Binary searches: this runs every frame, so measure O(log n) times, not once per character.
    if (scroll) {
        for (lo = 0, hi = n; lo < hi;) { // first character from which the rest fits = the last stop
            mid = (lo + hi) / 2;
            if (fntTextWidth(id, &string[at[mid]]) <= room)
                hi = mid;
            else
                lo = mid + 1;
        }
        int step = (int)((clock() / (CLOCKS_PER_SEC / 1000) / FNT_MARQUEE_STEP_MS) % (lo + 2 * FNT_MARQUEE_PAUSE)) - FNT_MARQUEE_PAUSE;
        step = step < 0 ? 0 : (step > lo ? lo : step);
        fntRenderString(id, x, y, aligned, room, 0, &string[at[step]], colour);
        return x + room;
    }

    room -= fntTextWidth(id, "...");
    if (room < 0) // not even "..." fits
        return x;
    // buf must hold the complete UTF-8 prefix plus "...". Cap by byte offset, not character count.
    while (n > 0 && at[n] > (int)sizeof(buf) - 4)
        n--;
    for (lo = 0, hi = n; lo < hi;) { // most characters that fit ahead of the "..."
        mid = (lo + hi + 1) / 2;
        snprintf(buf, sizeof(buf), "%.*s", at[mid], string);
        if (fntTextWidth(id, buf) <= room)
            lo = mid;
        else
            hi = mid - 1;
    }
    snprintf(buf, sizeof(buf), "%.*s...", at[lo], string);
    return fntRenderString(id, x, y, aligned, 0, 0, buf, colour);
}

// fntWrapText's output: n bytes of src at out[*wlen], whole or not at all, so a full buffer never ends inside a character
static int fntWrapPut(char *out, int outSize, int *wlen, const char *src, int n)
{
    if (*wlen + n > outSize - 1)
        return 0;
    memcpy(&out[*wlen], src, n);
    *wlen += n;
    return 1;
}

int fntWrapText(int id, const char *text, int innerW, char *out, int outSize)
{
    int wlen = 0, lineW = 0, lines = 1;
    int spaceW;
    const char *p = text;

    if (out == NULL || outSize <= 0)
        return 0;
    out[0] = '\0';
    if (text == NULL)
        return 0;
    spaceW = rmUnScaleX(fntCalcDimensions(id, " "));

    while (*p) {
        if (*p == '\n') { // an explicit newline in the source string is a hard break
            fntWrapPut(out, outSize, &wlen, "\n", 1);
            lines++;
            lineW = 0;
            p++;
            continue;
        }
        if (*p == ' ') { // runs of spaces collapse; one separator goes back between words
            p++;
            continue;
        }

        const char *word = p;
        while (*p && *p != ' ' && *p != '\n')
            p++;
        int len = (int)(p - word);
        int noSpaces = txtNoSpaceScript(word, len);

        if (!noSpaces && len < 256) {
            char buf[256];
            memcpy(buf, word, len);
            buf[len] = '\0';
            int wordW = rmUnScaleX(fntCalcDimensions(id, buf)); // the whole word, so kerning stays exact
            if (wordW <= innerW) {
                if (lineW > 0 && lineW + spaceW + wordW > innerW) {
                    fntWrapPut(out, outSize, &wlen, "\n", 1);
                    lines++;
                    lineW = 0;
                } else if (lineW > 0) {
                    fntWrapPut(out, outSize, &wlen, " ", 1);
                    lineW += spaceW;
                }
                fntWrapPut(out, outSize, &wlen, word, len);
                lineW += wordW;
                continue;
            }
        }

        // A word wider than the line (a URL, a path) breaks between any two characters. CJK, Thai and Lao text has
        // no spaces to break at: it goes on from where the line is, and breaks only where those scripts allow
        // (RIPPS2 build 104), never before a closing mark or a vowel that belongs to the letter before.
        if (lineW > 0 && noSpaces) {
            fntWrapPut(out, outSize, &wlen, " ", 1);
            lineW += spaceW;
        } else if (lineW > 0) {
            fntWrapPut(out, outSize, &wlen, "\n", 1);
            lines++;
            lineW = 0;
        }
        for (const char *q = word; q < p;) {
            uint32_t cp;
            char glyph[5];
            int n = txtNext(q, &cp);
            if (n <= 0 || q + n > p)
                n = 1;
            memcpy(glyph, q, n);
            glyph[n] = '\0';
            int glyphW = rmUnScaleX(fntCalcDimensions(id, glyph));
            if (lineW > 0 && lineW + glyphW > innerW && (!noSpaces || q == word || txtBreakAt(word, q))) {
                fntWrapPut(out, outSize, &wlen, "\n", 1);
                lines++;
                lineW = 0;
            }
            fntWrapPut(out, outSize, &wlen, q, n);
            lineW += glyphW;
            q += n;
        }
    }
    out[wlen] = '\0';
    return lines;
}

void fntFitString(int id, char *string, size_t width)
{
    size_t cw = 0;
    char *str = string;
    size_t spacewidth = fntCalcDimensions(id, " ");
    char *psp = NULL;
    width = rmScaleX(width);

    while (*str) {
        // scan forward to the next whitespace
        char *sp = str;
        for (; *sp && *sp != ' ' && *sp != '\n'; ++sp)
            ;

        // store what was there before
        char osp = *sp;

        // newline resets the situation
        if (osp == '\n') {
            cw = 0;
            str = ++sp;
            psp = NULL;
            continue;
        }

        // terminate after the word
        *sp = '\0';

        // Calc the font's width...
        // NOTE: The word was terminated, so we're seeing a single word
        // on that position
        size_t ww = fntCalcDimensions(id, str);

        if (cw + ww > width) {
            if (psp) {
                // we have a prev space to utilise (wrap on it)
                *psp = '\n';
                *sp = osp;
                cw = ww;
                psp = sp;
            } else if (osp != '\0') {
                // no prev. space to hijack, must break after the word
                // this will mean overflowed text...
                *sp = '\n';
                cw = 0;
            }
        } else {
            cw += ww;
            *sp = osp;
            psp = sp;
        }

        // belomaxorka/YAOPL a4c0830f: keep the final NUL and never
        // scan beyond an exact-size theme/config string allocation.
        if (osp == '\0')
            break;

        cw += spacewidth;
        str = ++sp;
    }
}

int FontSystem::calcDimensions(int id, const char *str)
{
    int w;

    if (str == NULL)
        return 0;

    lock();
    w = calcDimensionsLocked(&fonts_[id], str);
    unlock();

    return w;
}

int FontSystem::calcDimensionsLocked(font_t *font, const char *str)
{
    int w = 0;

    if (str == NULL)
        return 0;
#ifndef __RTL
    // Arabic and Persian are measured as drawn, their letters joined (a joined letter is narrower)
    static char joined[2048]; // under the font lock, like every caller of this
    const char *shaped = txtShape(str, joined, sizeof(joined), 0);
    if (shaped != NULL)
        str = shaped;
#endif

    uint32_t codepoint;
    uint32_t state = UTF8_ACCEPT;
    FT_Bool use_kerning = FT_HAS_KERNING(font->face);
    FT_UInt glyph_index, previous = 0;
    FT_Vector delta;

    // cache glyphs and render as we go
    for (; *str; ++str) {
        if (utf8Decode(&state, &codepoint, *str)) // accumulate the codepoint value
            continue;
        if (txtZeroWidth(codepoint)) // drawn as nothing, so measured as nothing
            continue;

        // Could just as well only get the glyph dimensions
        // but it is probable the glyphs will be needed anyway
        fnt_glyph_cache_entry_t *glyph = cacheGlyph(font, codepoint);
        if (!glyph)
            continue;

        // kerning
        if (use_kerning && previous) {
            glyph_index = FT_Get_Char_Index(font->face, codepoint);
            if (glyph_index) {
                FT_Get_Kerning(font->face, previous, glyph_index, FT_KERNING_DEFAULT, &delta);
                w += delta.x >> 6;
            }
            previous = glyph_index;
        }

        w += glyph->shx >> 6;
    }

    return w;
}

// ---- fntsys.h API ---------------------------------------------------------------------------------

static FontSystem gFonts;

void fntInit()
{
    gFonts.init();
}

void fntEnd()
{
    gFonts.end();
}

int fntScaleFontSize(int baseSize)
{
    // Themes can deliberately use larger titles or smaller labels. Preserve those sizes.
    if (baseSize == FNTSYS_DEFAULT_SIZE) {
        if (gFontSize == FONT_SIZE_SMALL)
            return 15;
        if (gFontSize == FONT_SIZE_LARGE)
            return 19;
    }
    return baseSize;
}

int fntLoadFile(char *path, int fontSize)
{
    return gFonts.loadFile(path, fontSize);
}

int fntLoadFallback(char *path)
{
    return gFonts.loadFallback(path);
}

int fntLoadDefault(char *path)
{
    return gFonts.loadDefault(path);
}

void fntRelease(int id)
{
    gFonts.release(id);
}

void fntSetWeight(int id, int weight)
{
    gFonts.setWeight(id, weight);
}

void fntUpdateAspectRatio()
{
    gFonts.updateAspectRatio();
}

#ifdef __OPLDIAG
int FontSystem::diagDescribe(char *out, int size)
{
    int n = 0;

    out[0] = '\0';
    lock();
    for (int i = 0; i < FNT_MAX_COUNT && n < size - 1; i++) {
        font_t *f = &fonts_[i];
        if (!f->isValid)
            continue;
        FT_Size fs = f->face ? f->face->size : NULL;
        int r = snprintf(out + n, size - n, " f%d:size %d->%d ppem %dx%d embolden %d", i, f->fontSize, fntScaleFontSize(f->fontSize),
                         fs ? fs->metrics.x_ppem : -1, fs ? fs->metrics.y_ppem : -1, f->embolden);
        if (r < 0)
            break;
        n += (r < size - n) ? r : size - 1 - n;
    }
    unlock();
    return n;
}

int fntDiagDescribe(char *out, int size)
{
    return gFonts.diagDescribe(out, size);
}
#endif

int fntRenderString(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, u64 colour)
{
    return gFonts.renderString(id, x, y, aligned, width, height, string, colour);
}

int fntCalcDimensions(int id, const char *str)
{
    return gFonts.calcDimensions(id, str);
}
