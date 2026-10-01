#ifndef __ATLAS_H
#define __ATLAS_H

#include <gsKit.h>
#include <stddef.h>
#include <tamtypes.h>
#include "include/raii.h"

/// A texture atlas: one GS texture that small bitmaps (font glyphs) are packed into with a guillotine allocator.
/// Pixels are copied in at place() time; the surface is invalidated in VRAM so the next draw re-uploads it.
class Atlas
{
public:
    /// Where a placed bitmap ended up. Owned by the atlas; valid until the atlas is destroyed.
    struct Allocation
    {
        int x, y;
        int w, h;
    };

    /// Allocates a new atlas (all-zero pixels, no CLUT, nearest filtering). NULL when out of memory.
    /// Further settings can be applied to texture().
    static Atlas *create(size_t width, size_t height, u8 psm);

    /// Destroys an atlas made by create() (NULL is fine): unloads the texture from VRAM and frees the surface and the
    /// allocation tree. Atlases live in malloc memory, so do not `delete` one.
    static void destroy(Atlas *atlas);
    ~Atlas();
    Atlas(const Atlas &) = delete;
    Atlas &operator=(const Atlas &) = delete;

    /// Reserves room for a width x height bitmap (one pixel of padding is added on each axis) and copies
    /// `pixels` in. Pixels are in the atlas' PSM; 32-bit-wide formats are copied as 4 bytes per pixel.
    /// Returns NULL when `pixels` is NULL or nothing fits.
    const Allocation *place(size_t width, size_t height, const void *pixels);

    GSTEXTURE *texture();

private:
    Atlas();

    struct Node
    {
        Allocation area;
        raii::UniquePtr<Node> leaf1, leaf2;
    };

    static raii::UniquePtr<Node> newNode(int x, int y, size_t width, size_t height);
    static Node *placeIn(Node *node, size_t width, size_t height);
    void copyData(const Allocation &al, size_t width, size_t height, const void *pixels);

    raii::UniquePtr<Node> root_;
    GSTEXTURE surface_;
};

#endif
