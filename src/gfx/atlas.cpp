/*
  Copyright 2010, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include <new>
#include <stdio.h>
#include <malloc.h>
#include "include/opl.h"
#include "include/atlas.h"
#include "include/renderman.h"

namespace
{

size_t pixelSize(u8 psm)
{
    switch (psm) {
        case GS_PSM_CT32:
            return 4;
        case GS_PSM_CT24:
            return 4;
        case GS_PSM_CT16:
            return 2;
        case GS_PSM_CT16S:
            return 2;
        case GS_PSM_T8:
            return 1;
        default:
            return 0;
    }
}

} // namespace

Atlas::Atlas()
{
    memset(&surface_, 0, sizeof(surface_));
}

Atlas::~Atlas()
{
    rmUnloadTexture(&surface_);
    free(surface_.Mem);
    surface_.Mem = NULL;
}

GSTEXTURE *Atlas::texture()
{
    return &surface_;
}

raii::UniquePtr<Atlas::Node> Atlas::newNode(int x, int y, size_t width, size_t height)
{
    raii::UniquePtr<Node> node = raii::makeUnique<Node>();
    if (node)
        node->area = {x, y, (int)width, (int)height};
    return node;
}

void Atlas::destroy(Atlas *atlas)
{
    raii::DestroyFree()(atlas);
}

Atlas *Atlas::create(size_t width, size_t height, u8 psm)
{
    void *mem = malloc(sizeof(Atlas)); // not `new`: see raii::makeUnique
    if (!mem)
        return NULL;
    raii::UniquePtr<Atlas> atlas(new (mem) Atlas());
    if (!atlas)
        return NULL;

    atlas->root_ = newNode(0, 0, width, height);

    GSTEXTURE &s = atlas->surface_;
    s.Width = width;
    s.Height = height;
    s.Filter = GS_FILTER_NEAREST;

    size_t txtsize = gsKit_texture_size(width, height, psm);
    s.PSM = psm;
    s.Mem = (u32 *)memalign(128, txtsize);
    s.Vram = 0;

    // defaults to no clut
    s.ClutPSM = 0;
    s.Clut = NULL;
    s.VramClut = 0;
    s.ClutStorageMode = GS_CLUT_STORAGE_CSM1;

    // Bail out on OOM rather than dereferencing a NULL allocation tree or memset-ing through a NULL
    // surface buffer. (~Atlas tolerates both being NULL.)
    if (!atlas->root_ || !s.Mem)
        return NULL;

    // zero out the atlas surface
    memset(s.Mem, 0x0, txtsize);

    return atlas.release();
}

Atlas::Node *Atlas::placeIn(Node *node, size_t width, size_t height)
{
    // do we fit?
    if (!(node->area.w >= (int)width && node->area.h >= (int)height))
        return NULL;

    if (!node->leaf1 && !node->leaf2) {
        // extra space
        size_t dx = node->area.w - width;
        size_t dy = node->area.h - height;

        // make the wider piece also longer - less wasted space
        if (dx < dy) {
            node->leaf1 = newNode(node->area.x + width, node->area.y, dx, height);
            node->leaf2 = newNode(node->area.x, node->area.y + height, node->area.w, dy);
        } else {
            node->leaf1 = newNode(node->area.x, node->area.y + height, width, dy);
            node->leaf2 = newNode(node->area.x + width, node->area.y, dx, node->area.h);
        }

        // An allocation failure leaves a leaf missing: the split is then simply unusable, which wastes space
        // but never crashes (placeIn tolerates absent children).
        return node;
    }

    // already occupied. Try children
    if (node->leaf1)
        if (Node *p = placeIn(node->leaf1.get(), width, height))
            return p;
    if (node->leaf2)
        if (Node *p = placeIn(node->leaf2.get(), width, height))
            return p;

    return NULL;
}

// copies the data into the atlas
void Atlas::copyData(const Allocation &al, size_t width, size_t height, const void *pixels)
{
    size_t ps = pixelSize(surface_.PSM);
    if (!ps)
        return;

    const char *src = static_cast<const char *>(pixels);
    char *data = (char *)surface_.Mem;
    const size_t atlasWidth = root_->area.w;

    // advance the pointer to the atlas position start (first pixel)
    data += ps * (al.y * atlasWidth + al.x);

    size_t rowsize = width * ps;

    for (size_t y = 0; y < height; ++y) {
        memcpy(data, src, rowsize);
        data += ps * atlasWidth;
        src += ps * width;
    }
}

const Atlas::Allocation *Atlas::place(size_t width, size_t height, const void *pixels)
{
    if (!pixels)
        return NULL;

    Node *node = placeIn(root_.get(), width + 1, height + 1);
    if (!node)
        return NULL;

    copyData(node->area, width, height, pixels);

    rmInvalidateTexture(&surface_);

    return &node->area;
}
