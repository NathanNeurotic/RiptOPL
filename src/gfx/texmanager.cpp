// gsKit texture manager, adapted for HIRES queue ownership.
// Copyright 2017 - Rick "Maximus32" Gaiser <rgaiser@gmail.com>
// Copyright 2004 - Chris "Neovanglist" Gilbert <Neovanglist@LainOS.org>
// Licenced under Academic Free License version 2.0

#include <gsKit.h>
#include "gsTexManager.h"
#include "include/texmanager.h"

#include <kernel.h>
#include <malloc.h>
#include <string.h>

struct SVramBlock
{
    unsigned int iStart;
    unsigned int iSize;
    unsigned int iUseCount;
    unsigned int iUseCountPrev;
    unsigned int lastUsed;
    unsigned int lastUpload;
    unsigned int allocated;
    unsigned int protectUntil;
    GSTEXTURE *tex;
    SVramBlock *pNext;
    SVramBlock *pPrev;
};

static SVramBlock *__head;
static enum ETransferMode __trmode = ETM_INLINE;
static unsigned int __building = 1;
static unsigned int __committed;
static int __latency;
static int __starved;
static int __degraded;
static int __degradedProgress;
static int __pressureSeen;

static int frameAfter(unsigned int a, unsigned int b)
{
    return (int)(a - b) > 0;
}

static int frameAtLeast(unsigned int a, unsigned int b)
{
    return (int)(a - b) >= 0;
}

static SVramBlock *_blockCreate(unsigned int start, unsigned int size)
{
    SVramBlock *block = static_cast<SVramBlock *>(malloc(sizeof(SVramBlock)));
    if (block != NULL)
        memset(block, 0, sizeof(*block));
    if (block != NULL) {
        block->iStart = start;
        block->iSize = size;
    }
    return block;
}

static void _blockInsertAfter(SVramBlock *block, SVramBlock *next)
{
    next->pNext = block->pNext;
    next->pPrev = block;
    if (block->pNext != NULL)
        block->pNext->pPrev = next;
    block->pNext = next;
}

static SVramBlock *_blockRemove(SVramBlock *block)
{
    SVramBlock *next = block->pNext;
    if (block->pPrev != NULL)
        block->pPrev->pNext = block->pNext;
    if (block->pNext != NULL)
        block->pNext->pPrev = block->pPrev;
    free(block);
    return next;
}

static void _blockSplitFree(SVramBlock *block, unsigned int size)
{
    SVramBlock *newBlock = _blockCreate(block->iStart + size, block->iSize - size);
    if (newBlock == NULL)
        return;
    block->iSize = size;
    _blockInsertAfter(block, newBlock);
}

static int _blockProtected(const SVramBlock *block)
{
    if (!__latency || (block->tex == NULL && block->protectUntil == 0))
        return 0;
    if (block->protectUntil && frameAfter(block->protectUntil, __committed))
        return 1;
    return frameAtLeast(block->lastUsed, __committed);
}

static int _blockReclaimable(const SVramBlock *block)
{
    return !_blockProtected(block) && block->allocated != __building;
}

static SVramBlock *_blockMergeFree(SVramBlock *block)
{
    while (block->pPrev != NULL && block->pPrev->tex == NULL && _blockReclaimable(block->pPrev))
        block = block->pPrev;
    while (block->pNext != NULL && block->pNext->tex == NULL && _blockReclaimable(block->pNext)) {
        block->iSize += block->pNext->iSize;
        _blockRemove(block->pNext);
    }
    block->allocated = 0;
    block->protectUntil = 0;
    return block;
}

static unsigned int _blockGetWeight(const SVramBlock *block)
{
    unsigned int weight = 0;
    if (block != NULL && block->tex != NULL) {
        if (block->iUseCount == block->iUseCountPrev) {
            weight += block->iUseCountPrev;
        } else if (block->iUseCount < block->iUseCountPrev) {
            weight += block->iUseCountPrev - block->iUseCount;
            weight += block->iUseCountPrev;
        } else {
            weight += 1;
            weight += block->iUseCount;
        }
    }
    return weight;
}

static SVramBlock *_blockAlloc(unsigned int size)
{
    SVramBlock *block;
    for (block = __head; block != NULL; block = block->pNext) {
        if (block->tex == NULL && _blockReclaimable(block) && block->iSize >= size)
            break;
    }

    unsigned int weight = 0;
    while (block == NULL) {
        int evictionPossible = 0;
        for (SVramBlock *it = __head; it != NULL; it = it->pNext) {
            if (it->tex != NULL && !_blockProtected(it)) {
                evictionPossible = 1;
                break;
            }
        }
        if (!evictionPossible)
            return NULL;

        SVramBlock *candidate = NULL;
        for (SVramBlock *it = __head; it != NULL; it = it->pNext) {
            if (it->tex != NULL && !_blockProtected(it) && _blockGetWeight(it) <= weight) {
                candidate = it;
                break;
            }
        }
        if (candidate == NULL) {
            if (++weight == 0)
                return NULL;
            continue;
        }
        candidate->tex->Vram = 0;
        candidate->tex->VramClut = 0;
        candidate->tex = NULL;
        candidate->allocated = 0;
        candidate->protectUntil = 0;
        block = _blockMergeFree(candidate);
        if (block->iSize < size)
            block = NULL;
        else
            break;
        weight++;
    }

    if (block != NULL && block->iSize > size)
        _blockSplitFree(block, size);
    return block;
}

void gsKit_TexManager_init(GSGLOBAL *gsGlobal)
{
    SVramBlock *block = __head;
    while (block != NULL)
        block = _blockRemove(block);
    __head = _blockCreate(gsGlobal->CurrentPointer, (4 * 1024 * 1024) - gsGlobal->CurrentPointer);
    __building = 1;
    __committed = 0;
    __starved = 0;
    __degraded = 0;
    __degradedProgress = 0;
    __pressureSeen = 0;
}

void gsKit_TexManager_setmode(GSGLOBAL *gsGlobal, enum ETransferMode mode)
{
    (void)gsGlobal;
    __trmode = mode;
}

void gsKit_TexManager_setQueueLatency(int frames)
{
    __latency = frames > 0 ? 1 : 0;
    __trmode = __latency ? ETM_DIRECT : ETM_INLINE;
    __building = 1;
    __committed = 0;
    __starved = 0;
    __degraded = 0;
    __degradedProgress = 0;
    __pressureSeen = 0;
}

unsigned int gsKit_TexManager_bind(GSGLOBAL *gsGlobal, GSTEXTURE *tex)
{
    SVramBlock *block;
    for (block = __head; block != NULL; block = block->pNext) {
        if (block->tex == tex)
            break;
    }

    unsigned int tsize = gsKit_texture_size(tex->Width, tex->Height, tex->PSM);
    unsigned int csize = 0;
    unsigned int cwidth = 16;
    unsigned int cheight = 16;
    if (tex->Clut != NULL) {
        cwidth = tex->PSM == GS_PSM_T8 ? 16 : 8;
        cheight = tex->PSM == GS_PSM_T8 ? 16 : 2;
        csize = gsKit_texture_size(cwidth, cheight, tex->ClutPSM);
    }

    // Invalidation also needs a new VRAM generation when the old queue still uses this block.
    if (block != NULL && tex->Vram == 0 && _blockProtected(block)) {
        block->tex = NULL;
        block->protectUntil = __building + 1;
        block = NULL;
    }

    if (block == NULL) {
        block = _blockAlloc(tsize + csize);
        if (block == NULL) {
            if (!__pressureSeen) {
                __pressureSeen = 1;
                __starved = 1;
            } else {
                __degraded = 1;
            }
            tex->Vram = 0;
            tex->VramClut = 0;
            return GSKIT_TEXMGR_BIND_DEFERRED;
        }
        block->tex = tex;
        block->iUseCount = 0;
        block->iUseCountPrev = 1;
        block->allocated = __building;
        tex->Vram = 0;
        tex->VramClut = 0;
    }

    unsigned int ttransfer = tex->Vram == 0;
    unsigned int ctransfer = tex->Clut != NULL && tex->VramClut == 0;
    if (ttransfer) {
        tex->Vram = block->iStart;
        gsKit_setup_tbw(tex);
        SyncDCache(tex->Mem, (u8 *)tex->Mem + tsize);
        if (__trmode == ETM_INLINE)
            gsKit_texture_send_inline(gsGlobal, tex->Mem, tex->Width, tex->Height, tex->Vram, tex->PSM, tex->TBW, tex->Clut ? GS_CLUT_TEXTURE : GS_CLUT_NONE);
        else
            gsKit_texture_send(tex->Mem, tex->Width, tex->Height, tex->Vram, tex->PSM, tex->TBW, tex->Clut ? GS_CLUT_TEXTURE : GS_CLUT_NONE);
    }
    if (ctransfer) {
        tex->VramClut = block->iStart + tsize;
        SyncDCache(tex->Clut, (u8 *)tex->Clut + csize);
        if (__trmode == ETM_INLINE)
            gsKit_texture_send_inline(gsGlobal, tex->Clut, cwidth, cheight, tex->VramClut, tex->ClutPSM, 1, GS_CLUT_PALLETE);
        else
            gsKit_texture_send(tex->Clut, cwidth, cheight, tex->VramClut, tex->ClutPSM, 1, GS_CLUT_PALLETE);
    }
    if (ttransfer || ctransfer) {
        block->lastUpload = __building;
        if (__degraded)
            __degradedProgress = 1;
    }
    block->lastUsed = __building;
    block->iUseCount++;
    return ttransfer | ctransfer;
}

void gsKit_TexManager_invalidate(GSGLOBAL *gsGlobal, GSTEXTURE *tex)
{
    (void)gsGlobal;
    tex->Vram = 0;
    tex->VramClut = 0;
}

void gsKit_TexManager_free(GSGLOBAL *gsGlobal, GSTEXTURE *tex)
{
    (void)gsGlobal;
    for (SVramBlock *block = __head; block != NULL; block = block->pNext) {
        if (block->tex == tex) {
            block->tex = NULL;
            if (__latency)
                block->protectUntil = __building + 1;
            else
                _blockMergeFree(block);
            break;
        }
    }
    tex->Vram = 0;
    tex->VramClut = 0;
}

void gsKit_TexManager_release(GSGLOBAL *gsGlobal, GSTEXTURE *tex)
{
    gsKit_TexManager_free(gsGlobal, tex);
    // HIRES uses direct uploads, so no inline DMA reference survives this call.
    free(tex->Mem);
    free(tex->Clut);
    tex->Mem = NULL;
    tex->Clut = NULL;
}

static void _usageNextFrame(void)
{
    for (SVramBlock *block = __head; block != NULL; block = block->pNext) {
        if (block->tex != NULL) {
            block->iUseCountPrev = block->iUseCount;
            block->iUseCount = 0;
        }
    }
}

void gsKit_TexManager_nextFrame(GSGLOBAL *gsGlobal)
{
    (void)gsGlobal;
    _usageNextFrame();
}

static void _rollbackAllocations(void)
{
    for (SVramBlock *block = __head; block != NULL; block = block->pNext) {
        if (block->allocated != __building && block->lastUpload != __building)
            continue;
        if (block->tex != NULL) {
            block->tex->Vram = 0;
            block->tex->VramClut = 0;
        }
        block->lastUpload = 0;
        if (block->allocated == __building) {
            block->tex = NULL;
            block->allocated = 0;
            block->protectUntil = 0;
            block = _blockMergeFree(block);
        }
    }
}

void gsKit_TexManager_commitFrame(int completed)
{
    if (!completed) {
        _rollbackAllocations();
        __degraded = 1;
        __degradedProgress = 0;
        __starved = 0;
    } else {
        __committed = __building;
        if (__degradedProgress)
            __degraded = 0;
        __degradedProgress = 0;
        __starved = 0;
    }
    __building++;
    _usageNextFrame();
}

int gsKit_TexManager_starved(void)
{
    return __starved;
}

void gsKit_TexManager_flush(void)
{
    __starved = 0;
    __degraded = 0;
    __degradedProgress = 0;
}
