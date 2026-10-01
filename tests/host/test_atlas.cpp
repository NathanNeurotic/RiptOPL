// HOST-TEST: sdk-headers
// atlas: guillotine packing of bitmaps into one texture, pixel copy, padding, exhaustion and teardown.
#include <string.h>
#include <vector>
#include "include/atlas.h"
#include "../../src/gfx/atlas.cpp"
#include "check.h"

// renderman hooks the atlas calls: count them
static int g_invalidated = 0, g_unloaded = 0;
void rmInvalidateTexture(GSTEXTURE *) { g_invalidated++; }
void rmUnloadTexture(GSTEXTURE *) { g_unloaded++; }

static unsigned rngState = 12345;
static unsigned rnd(unsigned n) { rngState = rngState * 1103515245u + 12345u; return (rngState >> 16) % n; }

int main()
{
    // creation: zeroed 8-bit surface, nearest filtering, no CLUT
    Atlas *a = Atlas::create(64, 32, GS_PSM_T8);
    CHECK(a != NULL);
    GSTEXTURE *t = a->texture();
    CHECK(t->Width == 64 && t->Height == 32 && t->PSM == GS_PSM_T8 && t->Filter == GS_FILTER_NEAREST && t->Clut == NULL);
    const unsigned char *mem = static_cast<const unsigned char *>(t->Mem);
    bool zero = true;
    for (int i = 0; i < 64 * 32; i++) zero &= mem[i] == 0;
    CHECK(zero);

    // a bitmap lands where it says, with its pixels intact and the texture invalidated
    unsigned char glyph[3 * 2] = {1, 2, 3, 4, 5, 6};
    const Atlas::Allocation *g = a->place(3, 2, glyph);
    CHECK(g != NULL && g->x == 0 && g->y == 0);
    CHECK(g_invalidated == 1);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++) CHECK(mem[(g->y + y) * 64 + g->x + x] == glyph[y * 3 + x]);
    CHECK(a->place(1, 1, NULL) == NULL);   // no pixels: refused, nothing invalidated
    CHECK(g_invalidated == 1);

    // many random bitmaps: every placement is inside the atlas, none overlap, pixels land exactly
    struct Placed { const Atlas::Allocation *al; int w, h; unsigned char fill; };
    std::vector<Placed> placed;
    int refused = 0;
    placed.push_back({g, 3, 2, 0});
    for (int i = 0; i < 200; i++) {
        int w = 1 + rnd(9), h = 1 + rnd(9);
        std::vector<unsigned char> px(w * h, (unsigned char)(10 + i));
        const Atlas::Allocation *al = a->place(w, h, px.data());
        if (al) placed.push_back({al, w, h, (unsigned char)(10 + i)}); else refused++;
    }
    CHECK(refused > 0 && placed.size() > 5);    // the atlas does fill up, and then says so
    for (size_t i = 0; i < placed.size(); i++) {
        const Atlas::Allocation &p = *placed[i].al;
        CHECK(p.x >= 0 && p.y >= 0 && p.x + placed[i].w <= 64 && p.y + placed[i].h <= 32);
        for (size_t j = i + 1; j < placed.size(); j++) {
            const Atlas::Allocation &q = *placed[j].al;
            bool disjoint = p.x + placed[i].w <= q.x || q.x + placed[j].w <= p.x || p.y + placed[i].h <= q.y || q.y + placed[j].h <= p.y;
            CHECK(disjoint);
        }
        if (placed[i].fill)
            for (int y = 0; y < placed[i].h; y++)
                for (int x = 0; x < placed[i].w; x++) CHECK(mem[(p.y + y) * 64 + p.x + x] == placed[i].fill);
    }
    CHECK(a->place(100, 100, glyph) == NULL);   // bigger than the atlas

    // 32-bit surfaces copy 4 bytes per pixel
    Atlas *b = Atlas::create(8, 8, GS_PSM_CT32);
    unsigned px32[2 * 2] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    const Atlas::Allocation *g32 = b->place(2, 2, px32);
    CHECK(g32 != NULL);
    const unsigned *m32 = static_cast<const unsigned *>(b->texture()->Mem);
    CHECK(m32[g32->y * 8 + g32->x] == 0x11111111 && m32[(g32->y + 1) * 8 + g32->x + 1] == 0x44444444);

    // destroy unloads the texture; NULL is fine
    int before = g_unloaded;
    Atlas::destroy(a);
    Atlas::destroy(b);
    Atlas::destroy(NULL);
    CHECK(g_unloaded == before + 2);

    TEST_DONE("atlas");
}
