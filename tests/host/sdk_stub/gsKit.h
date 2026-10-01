#pragma once
typedef struct { int Width, Height, PSM; void *Mem; int VramClut; void *Clut; int ClutPSM; int ClutStorageMode; int Filter; int Vram; } GSTEXTURE;
typedef struct { int Width, Height; } GSGLOBAL;
// Constants and helpers the atlas/fntsys code needs (values only matter to the host tests).
#define GS_PSM_CT32 0
#define GS_PSM_CT24 1
#define GS_PSM_CT16 2
#define GS_PSM_CT16S 10
#define GS_PSM_T8 19
#define GS_FILTER_NEAREST 0
#define GS_CLUT_STORAGE_CSM1 0
inline int gsKit_texture_size(int w, int h, int psm)
{
    int bpp = psm == GS_PSM_T8 ? 1 : (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S) ? 2 : 4;
    return w * h * bpp;
}
