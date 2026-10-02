#ifndef __FONTSYSTEM_H
#define __FONTSYSTEM_H

// The font subsystem behind fntsys.h: FreeType faces, per-font glyph caches and atlases, the shared
// 8-bit CLUT and the lock that serialises the GUI thread (drawing) against the IO worker (loading).
// One instance lives in src/fntsys.cpp; the fnt* functions forward to it.

#include <ft2build.h>
#include FT_FREETYPE_H

#include "renderman.h"
#include "atlas.h"
#include "fntsys.h"

/// Maximal count of atlases per font
constexpr int ATLAS_MAX = 4;
/// Atlas width in pixels
constexpr int ATLAS_WIDTH = 256;
/// Atlas height in pixels
constexpr int ATLAS_HEIGHT = 256;
constexpr int FNT_MAX_COUNT = (16);
constexpr int GLYPH_CACHE_PAGE_SIZE = 256;

class FontSystem
{
public:
    constexpr FontSystem() = default;
    FontSystem(const FontSystem &) = delete;
    FontSystem &operator=(const FontSystem &) = delete;

    void init();
    void end();
    int loadFile(char *path, int fontSize);
    int loadDefault(char *path);
    void release(int id);
    void updateAspectRatio();
    int renderString(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, u64 colour);
    int calcDimensions(int id, const char *str);

    /** Single entry in the glyph cache */
    typedef struct
    {
        int isValid;
        // size in pixels of the glyph
        int width, height;
        // offsetting of the glyph
        int ox, oy;
        // advancements in pixels after rendering this glyph
        int shx, shy;

        // atlas for which the allocation was done
        Atlas *atlas;

        // atlas allocation position
        const Atlas::Allocation *allocation;

    } fnt_glyph_cache_entry_t;

    /** A whole font definition */
    typedef struct
    {
        /** GLYPH CACHE. Every glyph in the ASCII range is cached when first used
         * this means no additional memory aside from the one needed to render the
         * character set is used.
         */
        fnt_glyph_cache_entry_t **glyphCache;

        /// Maximal font cache page index
        int cacheMaxPageID;

        /// Font face
        FT_Face face;

        /// Nonzero if font is used
        int isValid;

        int fontSize;

        /// Texture atlases (default to NULL)
        Atlas *atlases[ATLAS_MAX];

        /// Pointer to data, if allocation takeover was selected (will be freed)
        void *dataPtr;
    } font_t;

private:
    // Per-call glyph-run state shared by renderString and its helpers (RTL runs continue with it).
    struct GlyphRun
    {
        rm_quad_t quad;
        uint32_t codepoint, state;
        FT_Bool use_kerning;
        FT_UInt glyph_index, previous;
        FT_Vector delta;
    };

    void lock();
    void unlock();
    void cacheFlush(font_t *font);
    int prepareGlyphCachePage(font_t *font, int pageid);
    void prepareCLUT();
    void destroyCLUT();
    void initSlot(font_t *font);
    void deleteSlot(font_t *font);
    int loadSlotLocked(font_t *font, void *buffer, int bufferSize, int owned, int fontSize);
    Atlas *newAtlas();
    int glyphAtlasPlace(font_t *font, fnt_glyph_cache_entry_t *glyph);
    fnt_glyph_cache_entry_t *cacheGlyph(font_t *font, uint32_t gid);
    void updateAspectRatioLocked();
    void renderGlyph(GlyphRun &run, fnt_glyph_cache_entry_t *glyph, int pen_x, int pen_y);
#ifdef __RTL
    void renderSubRTL(GlyphRun &run, font_t *font, const char *startRTL, const char *string, fnt_glyph_cache_entry_t *glyph, int x, int y);
#endif
    int calcDimensionsLocked(font_t *font, const char *str);

    FT_Library library_ = NULL;
    GSCLUT clut_ = {};
    font_t fonts_[FNT_MAX_COUNT] = {};
    // ONE LOCK FOR ALL FONT STATE (see the long note on lock() in fntsys.cpp).
    s32 semaId_ = 0;
};

#endif
