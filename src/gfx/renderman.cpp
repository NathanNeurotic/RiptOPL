/*
  Copyright 2010, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include <stdio.h>
#include <kernel.h>

#include "include/opl.h"
#include "include/renderman.h"
#include "include/renderer.h"
#include "include/ioman.h"

// Allocateable space in vram, as indicated in GsKit's code
#define __VRAM_SIZE 4194304

GSGLOBAL *gsGlobal;
s32 guiThreadID;

static Renderer gRenderer;


constexpr int NUM_RM_VMODES = 20;
constexpr int RM_VMODE_AUTO = 0;

// RM Vmode -> GS Vmode conversion table
struct rm_mode
{
    char mode;
    char hsync; // In KHz
    short int width;
    short int height;
    short int passes;
    short int VCK;
    short int interlace;
    short int field;
    short int aratio;
    short int PAR1; // Pixel Aspect Ratio 1 (For video modes with non-square pixels, like PAL/NTSC)
    short int PAR2; // Pixel Aspect Ratio 2 (For video modes with non-square pixels, like PAL/NTSC)
    // 4:3 safe area: the part of the framebuffer the 640x480 design grid is drawn into, centred,
    // before the user's Overscan setting is applied. 0 = the whole framebuffer. Rows that leave
    // these out get 0 from aggregate initialisation.
    short int safeW;
    short int safeH;
    // 1 = flicker filter for an interlaced FIELD HIRES row: each displayed line is blended with the
    // framebuffer line below it in the GS merge circuit (see Renderer::applyLineBlend). 0 elsewhere.
    short int lineBlend;
};

// clang-format off
static struct rm_mode rm_mode_table[NUM_RM_VMODES] = {
    // 24 bit color mode with black borders
    {-1,                 16,  640,   -1,  1, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, -1, 15}, // AUTO
    {GS_MODE_PAL,        16,  640,  512,  1, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 16, 15}, // PAL@50Hz
    {GS_MODE_NTSC,       16,  640,  448,  1, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 14, 15}, // NTSC@60Hz
    {GS_MODE_DTV_480P,   31,  640,  448,  1, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3, 14, 15}, // DTV480P@60Hz
    {GS_MODE_DTV_576P,   31,  640,  512,  1, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3, 16, 15}, // DTV576P@50Hz
    {GS_MODE_VGA_640_60, 31,  640,  480,  1, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3,  1,  1}, // VGA640x480@60Hz
    // 24 bit color mode full screen, multi-pass (2 passes, HIRES)
    // The 480i/576i HIRES rows fill all 704 samples x every active line, which is the WHOLE 4:3
    // raster -- on a CRT a good part of it sits under the bezel (overscan), so the menu was cut off
    // at the edges. Draw the design grid into the same footprint as the standard 640-wide
    // rows instead: 640 wide (1:1 horizontally, exactly the width the 640x448i/640x512i rows show
    // with MagH 4) and, vertically, 480 lines for NTSC (1:1, the full design grid) and 512 lines
    // for PAL (the 640x512i row's height). PAR is then the grid's fill ratio, as for every other
    // row: (640/640)/(480/480) = 1/1 and (640/640)/(512/480) = 15/16. The 480p/576p HIRES rows
    // are left full-raster: they are for digital/component displays that do not overscan.
    {GS_MODE_PAL,        16,  704,  576,  2, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 16, 15, 640, 512}, // PAL@50Hz
    {GS_MODE_NTSC,       16,  704,  480,  2, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3,  1,  1, 640, 480}, // NTSC@60Hz
    {GS_MODE_DTV_480P,   31,  704,  480,  2, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3, 10, 11}, // DTV480P@60Hz
    {GS_MODE_DTV_576P,   31,  704,  576,  2, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3, 12, 11}, // DTV576P@50Hz
    // 16 bit color mode full screen, multi-pass (3 passes, HIRES)
    {GS_MODE_DTV_720P,   31, 1280,  720,  3, 1, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_16_9, 1,  1}, // HDTV720P@60Hz
    {GS_MODE_DTV_1080I,  31, 1920, 1080,  3, 1, GS_INTERLACED,    GS_FRAME, RM_ARATIO_16_9, 1,  1}, // HDTV1080I@60Hz
    // 24 bit color mode for older systems (non-interlaced, low resolution)
    {GS_MODE_PAL,        16,  640,  256,  1, 4, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3,  8, 15}, // PAL@50Hz
    {GS_MODE_NTSC,       16,  640,  224,  1, 4, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3,  7, 15}, // NTSC@60Hz
    // Flicker-filtered interlaced modes (issue #615). APPENDED AT THE TAIL, never inserted beside
    // their FIELD twins above: $VMode is a stored raw INDEX, so a mid-list insert would silently
    // repoint every saved config one row down.
    //
    // Identical raster to the 640x512i/640x448i rows -- same GS mode, same 50/60Hz interlaced
    // signal a CRT expects -- but FFMD=1 (GS_FRAME), so the CRTC reads the SAME lines for both
    // fields instead of alternating. Nothing alternates at 25/30Hz any more, which is what removes
    // the interlace strobe on thin lines and font edges; the cost is that the framebuffer is half
    // as tall (rmSetMode halves Height for INTERLACED+FRAME) so vertical detail is genuinely
    // halved. The GS has no vertical-blend filter for the CRTC -- this is the hardware's flicker
    // control -- and the whole path is already proven here by the 1080i FRAME row above: Height
    // halving and the doubled PAR in rmGetPAR key off the same INTERLACED+FRAME test, and with the
    // halved height the fntsys scaling math lands exactly on the long-standing 224p/256p rows
    // (there is deliberately NO font supersample for FRAME mode -- under the atlas's NEAREST
    // sampling the 2:1 squash just dropped every other glyph row, issue #615). PAR1/PAR2 stay as
    // the FIELD rows' because rmGetPAR applies the x2 for the twice-as-tall pixels itself.
    {GS_MODE_PAL,        16,  640,  512,  1, 4, GS_INTERLACED,    GS_FRAME, RM_ARATIO_4_3, 16, 15}, // PAL@50Hz flicker-filtered
    {GS_MODE_NTSC,       16,  640,  448,  1, 4, GS_INTERLACED,    GS_FRAME, RM_ARATIO_4_3, 14, 15}, // NTSC@60Hz flicker-filtered
    // NTSC 704x480i HIRES with the menu in 640x448, the exact footprint of the 640x448i row, for a CRT
    // whose top and bottom edges hide part of the full-height 480-line area of the row above. It
    // trades the 1:1 vertical mapping for that fit (the 480-line grid is squeezed to 448 lines).
    // Appended at the tail for the same stored-index reason as the flicker-free rows.
    {GS_MODE_NTSC,       16,  704,  480,  2, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 14, 15, 640, 448}, // NTSC@60Hz HIRES, 448-line area
    // Flicker-filtered twins of the three 480i/576i HIRES rows (issue #132), appended for the same
    // stored-index reason. Unlike the FLICKER-FREE rows above they keep FIELD mode and the full
    // 576/480-line framebuffer, so vertical detail is not thrown away: instead the GS merge circuit
    // shows every line half-and-half with the line below it (Renderer::applyLineBlend). A one-line
    // detail then appears in both fields at half strength instead of strobing at 25/30Hz, which is
    // the classic interlace flicker filter. Everything else (safe area, PAR, gsKit hires passes) is
    // exactly the twin's.
    {GS_MODE_PAL,        16,  704,  576,  2, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 16, 15, 640, 512, 1}, // PAL@50Hz HIRES flicker-filtered
    {GS_MODE_NTSC,       16,  704,  480,  2, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3,  1,  1, 640, 480, 1}, // NTSC@60Hz HIRES flicker-filtered
    {GS_MODE_NTSC,       16,  704,  480,  2, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 14, 15, 640, 448, 1}, // NTSC@60Hz HIRES, 448-line area, flicker-filtered
};
// clang-format on

// Display Aspect Ratio

// Display dimensions after overscan compensation

// Transposition values including overscan compensation

const u64 gColWhite = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);  // Alpha 0x80 -> solid white
const u64 gColBlack = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);  // Alpha 0x80 -> solid black
const u64 gColDarker = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x60); // Alpha 0x60 -> transparent overlay color
const u64 gColFocus = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x50);  // Alpha 0x50 -> transparent overlay color

const u64 gDefaultCol = GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80); // Special color for texture multiplication
const u64 gDefaultAlpha = GS_SETREG_ALPHA(0, 1, 0, 1, 0);

void Renderer::invalidateTexture(GSTEXTURE *txt)
{
    gsKit_TexManager_invalidate(gsGlobal, txt);
}

void Renderer::unloadTexture(GSTEXTURE *txt)
{
    gsKit_TexManager_free(gsGlobal, txt);
}

void Renderer::startFrame(void)
{
    if (hires_ == 0)
        gsKit_clear(gsGlobal, gColBlack);

    order_ = 0;
}

void Renderer::endFrame(void)
{
    if (hires_) {
        gsKit_hires_sync(gsGlobal);
        gsKit_hires_flip(gsGlobal);
    } else {
        gsKit_set_finish(gsGlobal);
        gsKit_queue_exec(gsGlobal);

        // Wait for draw ops to finish
        gsKit_finish();

        if (!gsGlobal->FirstFrame) {
            // A VSync that fired while this frame was still being drawn has already queued a wakeup,
            // and sleeping on it would return at once and flip in the middle of the scanout. Drop it
            // (with interrupts off, so a VSync can't land between the two steps and be lost) and wait
            // for the next one. A slow frame then shows one field late instead of torn.
            DIntr();
            wakeCount_ = 0;
            CancelWakeupThread(guiThreadID);
            EIntr();
            SleepThread();
            wakeCount_ = 0;

            if (gsGlobal->DoubleBuffering == GS_SETTING_ON) {
                GS_SET_DISPFB2(gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer & 1] / 8192,
                               gsGlobal->Width / 64, gsGlobal->PSM, 0, 0);

                gsGlobal->ActiveBuffer ^= 1;
            }
        }

        gsKit_setactive(gsGlobal);
    }

    gsKit_TexManager_nextFrame(gsGlobal);
}

void Renderer::onVSync()
{
    if (wakeCount_ == 0) {
        wakeCount_ = 1;
        iWakeupThread(guiThreadID);
    }
}

static int rmOnVSync(int cause)
{
    gRenderer.onVSync();

    ExitHandler();
    return 0;
}

void Renderer::init()
{
    short int mode = gsKit_check_rom();

    rm_mode_table[RM_VMODE_AUTO].mode = mode;
    rm_mode_table[RM_VMODE_AUTO].height = (mode == GS_MODE_PAL) ? 512 : 448;
    rm_mode_table[RM_VMODE_AUTO].PAR1 = (mode == GS_MODE_PAL) ? 16 : 14;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

    // Initialize the DMAC
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    setMode(1);

    order_ = 0;

    wakeCount_ = 0;
    guiThreadID = GetThreadId();
}

int Renderer::setMode(int force)
{
    if (gVMode < RM_VMODE_AUTO || gVMode >= NUM_RM_VMODES)
        gVMode = RM_VMODE_AUTO;

    // we don't want to set the vmode_ without a reason...
    int changed = (vmode_ != gVMode || force);
    if (changed) {
        // Cleanup previous gsKit instance
        if (vmode_ >= 0)
            end();

        vmode_ = gVMode;
        hires_ = (rm_mode_table[vmode_].passes > 1) ? 1 : 0;

        if (hires_) {
            gsGlobal = gsKit_hires_init_global();
        } else {
            gsGlobal = gsKit_init_global();
            vsyncId_ = gsKit_add_vsync_handler(&rmOnVSync);
        }
        gsGlobal->Mode = rm_mode_table[vmode_].mode;
        gsGlobal->Width = rm_mode_table[vmode_].width;
        gsGlobal->Height = rm_mode_table[vmode_].height;
        gsGlobal->Interlace = rm_mode_table[vmode_].interlace;
        gsGlobal->Field = rm_mode_table[vmode_].field;
        gsGlobal->PSM = GS_PSM_CT24;
        // Higher resolution use too much VRAM
        // so automatically switch back to 16bit color depth
        if ((gsGlobal->Width * gsGlobal->Height) > (704 * 576))
            gsGlobal->PSM = GS_PSM_CT16S;
        gsGlobal->PSMZ = GS_PSMZ_16S;
        gsGlobal->ZBuffering = GS_SETTING_OFF;
        gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
        gsGlobal->DoubleBuffering = GS_SETTING_ON;
        gsGlobal->Dithering = GS_SETTING_ON;

        // Do not draw pixels if they are fully transparent
        // gsGlobal->Test->ATE  = GS_SETTING_ON;
        gsGlobal->Test->ATST = 7; // NOTEQUAL to AREF passes
        gsGlobal->Test->AREF = 0x00;
        gsGlobal->Test->AFAIL = 0; // KEEP

        if ((gsGlobal->Interlace == GS_INTERLACED) && (gsGlobal->Field == GS_FRAME))
            gsGlobal->Height /= 2;

        // Coordinate space ranges from 0 to 4096 pixels
        // Center the buffer in the coordinate space
        gsGlobal->OffsetX = ((4096 - gsGlobal->Width) / 2) * 16;
        gsGlobal->OffsetY = ((4096 - gsGlobal->Height) / 2) * 16;

        if (hires_) {
            // PrimAlphaEnable MUST be OFF across this call, and the bracket below is a real fix, not
            // hygiene (FifthFox's 1080i "scrolling leaves artifacts that persist", HW).
            //
            // gsKit_hires_init_screen -> _gsKit_create_passes bakes a gsKit_clear into each pass's
            // PERSISTENT queue -- that baked clear is the ONLY thing that ever erases a hires_ frame
            // (rmStartFrame deliberately skips clearing when hires_; see there). But gsKit_clear's
            // sprite samples the PRIM ABE bit from gsGlobal->PrimAlphaEnable AT CREATION TIME
            // (gsPrimitive.c gsKit_prim_list_sprite_flat), and the colour gsKit bakes is
            // GS_SETREG_RGBAQ(0,0,0,0x00,0) -- ALPHA ZERO. With PrimAlphaEnable ON (set above) and our
            // source-over blend (gDefaultAlpha: (Cs-Cd)*As+Cd), As=0 reduces to Cd: the "clear" wrote
            // the old destination pixel back. A no-op, baked in, replayed every pass, forever.
            //
            // That is why a band whose pass misses its 1/180s deadline shows STALE pixels instead of
            // black, and why they persist across screens: nothing under the UI ever repaints. The
            // non-hires_ path never had this bug for exactly one reason: rmStartFrame's per-frame
            // clear passes gColBlack, whose alpha is 0x80 (opaque) -- same function, different alpha.
            //
            // With ABE=0 baked in, the clear writes opaque black regardless of the runtime blend
            // state. Restoring PrimAlphaEnable right after is safe: it is only ever sampled when a
            // primitive is CREATED, and no OPL primitive is created inside init_screen. The only
            // other _gsKit_create_passes caller is gsKit_hires_set_bg, which OPL never calls (and
            // must not casually: a bg texture replaces the baked clear entirely).
            gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
            // gsKit_hires_init_screen raises its CALLER to priority 2 (a HACK in gsHires.c, so the
            // caller sits right under its priority-1 DMA thread) and gsKit_hires_deinit_global never
            // lowers it again. Remember who that is and what it ran at, so end() can put it back:
            // otherwise one visit to a 704-wide, 720p or 1080i mode left the GUI thread (31 since
            // main) at 2 until reboot, above the background-music threads (30/31) it normally yields
            // to, in every mode chosen after it.
            // setMode can also run on the IO worker (applyConfig from _loadConfig), so the thread is
            // recorded, not assumed to be guiThreadID.
            ee_thread_status_t status;
            hiresThread_ = GetThreadId();
            hiresPriority_ = (ReferThreadStatus(hiresThread_, &status) >= 0) ? status.current_priority : -1;
            gsKit_hires_init_screen(gsGlobal, rm_mode_table[vmode_].passes);
            gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
        } else {
            gsKit_init_screen(gsGlobal);
            gsKit_mode_switch(gsGlobal, GS_ONESHOT);
        }

        gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
        gsKit_set_primalpha(gsGlobal, gDefaultAlpha, 0);

        // reset the contents of the screen to avoid garbage being displayed
        if (hires_) {
            gsKit_hires_sync(gsGlobal);
            gsKit_hires_flip(gsGlobal);
        } else {
            // gsKit_init_screen() clears only the active DRAW buffer; with double
            // buffering the DISPLAYED buffer keeps whatever leftover VRAM was there
            // (the boot / mode-change "garbage"), and it is never cleared here:
            // gsKit_sync_flip() skips its buffer toggle while FirstFrame is set
            // (see gsKit gsCore.c), so a plain clear+flip just clears the same
            // buffer twice. Clear BOTH double buffers explicitly -- clear the
            // active one, switch the draw target to the other the way
            // gsKit_setactive() derives it from ActiveBuffer, clear that one too,
            // then restore the original active buffer so the following flip and all
            // later rendering behave exactly as before. We are in GS_ONESHOT mode
            // here, so each gsKit_clear() is drawn immediately.
            gsKit_clear(gsGlobal, gColBlack);
            if (gsGlobal->DoubleBuffering == GS_SETTING_ON) {
                gsGlobal->ActiveBuffer ^= 1;
                gsKit_setactive(gsGlobal);
                gsKit_clear(gsGlobal, gColBlack);
                gsGlobal->ActiveBuffer ^= 1;
                gsKit_setactive(gsGlobal);
            }
            gsKit_sync_flip(gsGlobal);
        }

        LOG("RENDERMAN New vmode_: %d, %d x %d\n", vmode_, gsGlobal->Width, gsGlobal->Height);
    }

    setDisplayOffset(gXOff, gYOff);
    setOverscan(gOverscan);
    setAspectRatio(wideDisplay() ? RM_ARATIO_16_9 : RM_ARATIO_4_3);

    return changed;
}

void Renderer::getScreenExtentsNative(int *w, int *h)
{
    *w = displayWidth_;
    *h = displayHeight_;
}

void Renderer::getScreenExtents(int *w, int *h)
{
    // Emulate 640x480 (square pixel VGA)
    *w = 640;
    *h = 480;
}

void Renderer::end(void)
{
    if (hires_) {
        gsKit_hires_deinit_global(gsGlobal);
        // Undo gsKit_hires_init_screen's priority bump (see setMode). ReferThreadStatus returns the
        // status (>= 0) on the EE, so a failed lookup is the only case left at -1 and skipped.
        if (hiresThread_ >= 0 && hiresPriority_ >= 0)
            ChangeThreadPriority(hiresThread_, hiresPriority_);
        hiresThread_ = -1;
        hiresPriority_ = -1;
    } else {
        gsKit_deinit_global(gsGlobal);
        gsKit_remove_vsync_handler(vsyncId_);
    }

    vmode_ = -1;
}

// Scale from the 640x480 design grid to the live display. Generic on purpose: callers pass ints and floats.
/** If txt is null, don't use DIM_UNDEF size */
void Renderer::setupQuad(GSTEXTURE *txt, int x, int y, short aligned, int w, int h, short scaled, u64 color, rm_quad_t *q)
{
    if (w == DIM_UNDEF)
        w = txt->Width;
    if (h == DIM_UNDEF)
        h = txt->Height;

    // Legacy scaling
    x = scaleX(x);
    y = scaleY(y);
    if (scaled & SCALING_RATIO)
        w = scaleX(w * aspectWidth_) >> 2;
    else
        w = scaleX(w);
    h = scaleY(h);

    // Align LEFT/HCENTER/RIGHT
    if (aligned & ALIGN_HCENTER)
        q->ul.x = x - (w >> 1);
    else if (aligned & ALIGN_RIGHT)
        q->ul.x = x - w;
    else
        q->ul.x = x;
    q->br.x = q->ul.x + w;

    // Align TOP/VCENTER/BOTTOM
    if (aligned & ALIGN_VCENTER)
        q->ul.y = y - (h >> 1);
    else if (aligned & ALIGN_BOTTOM)
        q->ul.y = y - h;
    else
        q->ul.y = y;
    q->br.y = q->ul.y + h;

    q->color = color;
    if (txt) {
        q->txt = txt;
        q->ul.u = 0;
        q->ul.v = 0;
        q->br.u = txt->Width;
        q->br.v = txt->Height;
    }
}

void Renderer::drawQuad(rm_quad_t *q)
{
    if ((q->txt->PSM == GS_PSM_CT32) || (q->txt->Clut && q->txt->ClutPSM == GS_PSM_CT32)) {
        gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
        gsKit_set_test(gsGlobal, GS_ATEST_ON);
    } else {
        gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
        gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    }

    gsKit_TexManager_bind(gsGlobal, q->txt);
    gsKit_prim_sprite_texture(gsGlobal, q->txt,
                              q->ul.x + renderXOff_, q->ul.y + renderYOff_,
                              q->ul.u, q->ul.v,
                              q->br.x + renderXOff_, q->br.y + renderYOff_,
                              q->br.u, q->br.v, order_, q->color);
    order_++;
}

// Vertical offset (px) applied to the coverflow reflection; set per-element by drawCoverFlow
// from the theme's reflection_offset. 0 = flush under the cover, negative = up, positive = down.

void Renderer::setReflectionYOffset(int yoff)
{
    reflectionYOff_ = yoff;
}

// Coverflow mirror: a vertically-flipped, alpha-faded reflection below the cover.
// bottomY = the cover's VISIBLE bottom in screen space; botV = the texture-v of that
// visible bottom. For an overlay (case art) these track the FRAME, not the padded
// element, so the case's transparent bottom padding is never mirrored into a gap and
// the mirror sits flush under the cover regardless of widescreen/PAR. reflection_offset
// (reflectionYOff_) is left as an optional fine-tune knob on top. Dormant unless a
// caller passes reflection != 0 (only drawCoverFlow does).
//
// gsKit sprites are single-color, so the alpha gradient is built from N stacked strips
// whose alpha falls off linearly to 0 with distance from the cover -- a real reflection
// fade instead of a flat, "pasted-on second box".
void Renderer::drawReflection(GSTEXTURE *txt, rm_quad_t *q, float bottomY, float botV)
{
    int reflH = (bottomY - q->ul.y) / 4;
    if (reflH <= 0)
        return;
    float ry = bottomY + reflectionYOff_;

    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_TexManager_bind(gsGlobal, txt);

    const int strips = 12;
    const int topAlpha = 0x23; // gsKit modulate alpha right under the cover (reduced again, ~27% below original); fades to 0
    int s;
    for (s = 0; s < strips; s++) {
        int a = topAlpha * (strips - 1 - s) / (strips - 1);
        if (a <= 0)
            break;
        // Strip s spans [s, s+1]/strips of the reflection height; v runs botV (waterline)
        // -> ul.v (cover top) so the cover is mirrored vertically as it fades out.
        float y0 = ry + (float)reflH * s / strips;
        float y1 = ry + (float)reflH * (s + 1) / strips;
        float v0 = botV + (q->ul.v - botV) * s / strips;
        float v1 = botV + (q->ul.v - botV) * (s + 1) / strips;
        gsKit_prim_sprite_texture(gsGlobal, txt,
                                  q->ul.x + renderXOff_, y0 + renderYOff_,
                                  q->ul.u, v0,
                                  q->br.x + renderXOff_, y1 + renderYOff_,
                                  q->br.u, v1, order_, GS_SETREG_RGBA(0x80, 0x80, 0x80, a));
        order_++;
    }
}

void Renderer::drawPixmap(GSTEXTURE *txt, int x, int y, short aligned, int w, int h, short scaled, u64 color, int reflection)
{
    rm_quad_t quad;
    setupQuad(txt, x, y, aligned, w, h, scaled, color, &quad);
    drawQuad(&quad);
    if (reflection)
        drawReflection(txt, &quad, quad.br.y, quad.br.v); // plain pixmap: the whole texture is the cover
}

void Renderer::drawOverlayPixmap(GSTEXTURE *overlay, int x, int y, short aligned, int w, int h, short scaled, u64 color,
                                 GSTEXTURE *inlay, int ulx, int uly, int urx, int ury, int blx, int bly, int brx, int bry, int reflection)
{
    rm_quad_t quad;
    setupQuad(overlay, x, y, aligned, w, h, scaled, color, &quad);
    int origBly = bly, origBry = bry; // unscaled inlay lower corners (element/texture space) for the reflection
    ulx = scaleX(ulx * aspectWidth_) >> 2;
    urx = scaleX(urx * aspectWidth_) >> 2;
    blx = scaleX(blx * aspectWidth_) >> 2;
    brx = scaleX(brx * aspectWidth_) >> 2;
    uly = scaleY(uly);
    ury = scaleY(ury);
    bly = scaleY(bly);
    bry = scaleY(bry);

    if ((inlay->PSM == GS_PSM_CT32) || (inlay->Clut && inlay->ClutPSM == GS_PSM_CT32))
        gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    else
        gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;

    gsKit_TexManager_bind(gsGlobal, inlay);
    gsKit_prim_quad_texture(gsGlobal, inlay,
                            quad.ul.x + ulx + renderXOff_, quad.ul.y + uly + renderYOff_,
                            0.0f, 0.0f,
                            quad.ul.x + urx + renderXOff_, quad.ul.y + ury + renderYOff_,
                            inlay->Width, 0.0f,
                            quad.ul.x + blx + renderXOff_, quad.ul.y + bly + renderYOff_,
                            0.0f, inlay->Height,
                            quad.ul.x + brx + renderXOff_, quad.ul.y + bry + renderYOff_,
                            inlay->Width, inlay->Height, order_, color);
    order_++;

    drawQuad(&quad);
    if (reflection) {
        // Anchor the mirror to the cover's VISIBLE bottom (the inlay's lower corner), not
        // the padded element bottom, and sample only the frame's texture rows -- so the
        // case art's transparent bottom padding can't open a gap above the reflection.
        int coverBottomOff = (bly > bry) ? bly : bry;              // scaled, screen space
        int frameBottom = (origBly > origBry) ? origBly : origBry; // unscaled, element space
        float botV = (h > 0) ? (float)frameBottom * overlay->Height / h : overlay->Height;
        float waterline = quad.ul.y + coverBottomOff;
        // Mirror the INLAY (the actual game cover art) too -- previously only the case FRAME was
        // reflected, so the cover box mirrored but the artwork inside it had no reflection at all
        // (the disc/frame appeared to mirror, the cover did not). Reuse the same rmDrawReflection the
        // plain-cover path uses; the inlay occupies the cover WINDOW inside the frame, so span its x
        // from the inlay's own (scaled) left/right corners and its u/v over the whole inlay texture.
        // Share the frame's waterline + height so the art and frame reflections track together, and
        // draw the inlay mirror FIRST so the frame mirror lands on top (matching the upright
        // inlay-then-frame draw order_ above). Non-coverflow callers pass reflection=0, so unaffected.
        rm_quad_t iquad = quad;
        iquad.ul.x = quad.ul.x + ((ulx < blx) ? ulx : blx);
        iquad.br.x = quad.ul.x + ((urx > brx) ? urx : brx);
        // Base the mirror height on the inlay WINDOW's top (uly/ury, already Y_SCALEd into the same
        // screen space as quad.ul.y), not the frame's top -- otherwise reflH spans the padded frame
        // height and the mirrored art renders vertically stretched, overflowing the frame's mirror.
        iquad.ul.y = quad.ul.y + ((uly < ury) ? uly : ury);
        iquad.ul.u = 0.0f;
        iquad.br.u = inlay->Width;
        iquad.ul.v = 0.0f;
        drawReflection(inlay, &iquad, waterline, inlay->Height);
        drawReflection(overlay, &quad, waterline, botV);
    }
}

void Renderer::drawRect(int x, int y, int w, int h, u64 color)
{
    // Scale both edges instead of the size, so neighbouring rects line up, and keep anything with a
    // size at least one display pixel: on a 224-448 line framebuffer a 1 or 2 high rect (frame
    // borders, separators, PSBBN Flow's stars) otherwise scaled to 0 and was not drawn at all.
    int x0 = scaleX(x), x1 = scaleX(x + w);
    int y0 = scaleY(y), y1 = scaleY(y + h);
    if (w > 0 && x1 <= x0)
        x1 = x0 + 1;
    if (h > 0 && y1 <= y0)
        y1 = y0 + 1;

    float fx = x0 + renderXOff_;
    float fy = y0 + renderYOff_;
    float fw = x1 - x0;
    float fh = y1 - y0;

    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_prim_sprite(gsGlobal, fx, fy, fx + fw, fy + fh, order_, color);
    order_++;
}

/* A frame and its fill, placed from the frame's EDGES in display pixels. Drawing them as two
   rmDrawRect calls scaled each height on its own, and Y_SCALE truncates: on a 224-line framebuffer
   (the flicker-free 448i/512i rows, 224p/256p) a 17-high swatch became 7 lines and its 13-high fill
   6, so whichever of the 2px top or bottom borders rounded to a line left the other with none. A
   dark colour swatch then read as an open box (zackcage6). Each side keeps at least one line. */
void Renderer::drawFramedRect(int x, int y, int w, int h, int border, u64 frameColor, u64 fillColor)
{
    int x0 = scaleX(x), y0 = scaleY(y), x1 = scaleX(x + w), y1 = scaleY(y + h);
    int bx = scaleX(border), by = scaleY(border);

    if (bx < 1)
        bx = 1;
    if (by < 1)
        by = 1;

    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_prim_sprite(gsGlobal, x0 + renderXOff_, y0 + renderYOff_, x1 + renderXOff_, y1 + renderYOff_, order_, frameColor);
    order_++;
    if (x1 - x0 > 2 * bx && y1 - y0 > 2 * by) {
        gsKit_prim_sprite(gsGlobal, x0 + bx + renderXOff_, y0 + by + renderYOff_, x1 - bx + renderXOff_, y1 - by + renderYOff_, order_, fillColor);
        order_++;
    }
}

void Renderer::drawLine(int x1, int y1, int x2, int y2, u64 color)
{
    float fx1 = scaleX(x1) + renderXOff_;
    float fy1 = scaleY(y1) + renderYOff_;
    float fx2 = scaleX(x2) + renderXOff_;
    float fy2 = scaleY(y2) + renderYOff_;

    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_prim_line(gsGlobal, fx1, fy1, fx2, fy2, order_, color);
    order_++;
}

void Renderer::drawTriangles(GSTEXTURE *txt, int count, const float *xy, const float *uv, const u64 *colors, int blend)
{
    // A texture without its own alpha channel (CT24) has none to blend with, so a fade uses the first
    // vertex's alpha as the GS's fixed blend factor instead; the default blend comes back afterwards.
    int fixed = blend && txt != NULL && count > 0 && txt->PSM != GS_PSM_CT32 && !(txt->Clut && txt->ClutPSM == GS_PSM_CT32);
    if (fixed)
        gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 2, 1, (colors[0] >> 24) & 0xff), 0);
    gsGlobal->PrimAlphaEnable = blend ? GS_SETTING_ON : GS_SETTING_OFF;
    gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    // One bind for the batch: nothing else binds between these triangles, so it stays resident.
    if (txt != NULL)
        gsKit_TexManager_bind(gsGlobal, txt);
    for (int i = 0; i < count; i++, xy += 6, uv += (txt != NULL) ? 6 : 0, colors += 3) {
        float x0 = scaleX(xy[0]) + renderXOff_, y0 = scaleY(xy[1]) + renderYOff_;
        float x1 = scaleX(xy[2]) + renderXOff_, y1 = scaleY(xy[3]) + renderYOff_;
        float x2 = scaleX(xy[4]) + renderXOff_, y2 = scaleY(xy[5]) + renderYOff_;
        if (txt != NULL)
            gsKit_prim_triangle_goraud_texture_3d(gsGlobal, txt, x0, y0, order_, uv[0], uv[1], x1, y1, order_, uv[2], uv[3],
                                                  x2, y2, order_, uv[4], uv[5], colors[0], colors[1], colors[2]);
        else
            gsKit_prim_triangle_gouraud_3d(gsGlobal, x0, y0, order_, x1, y1, order_, x2, y2, order_, colors[0], colors[1], colors[2]);
    }
    order_++;
    if (fixed)
        gsKit_set_primalpha(gsGlobal, gDefaultAlpha, 0);
}

void Renderer::setDisplayOffset(int x, int y)
{
    gsKit_set_display_offset(gsGlobal, x * rm_mode_table[vmode_].VCK, y);
    // gsKit_set_display_offset rewrites DISPLAY1 at full height, so the flicker filter's read
    // circuit 1 setup has to follow every position change (setMode always ends up here too).
    if (rm_mode_table[vmode_].lineBlend)
        applyLineBlend();
}

// Interlace flicker filter for the lineBlend HIRES rows (issue #132).
//
// gsKit only ever displays through read circuit 2 (gsKit_init_screen: PMODE EN1=0 EN2=1), and the
// hires renderer keeps a single front buffer at ScreenBuffer[0] that DISPFB2 points at for the
// whole life of the mode (no double buffering, nothing in gsHires.c touches PMODE or the DISPFB
// registers). That leaves read circuit 1 free. Point it at the same buffer one line further down
// (DISPFB1 DBY=1) with the same DISPLAY position, and let the merge circuit mix the two at a fixed
// ~50% (PMODE MMOD=1, ALP=0x80, SLBG=0: out = RC1*ALP + RC2*(1-ALP)). In FIELD mode the CRTC reads
// line 2k+field, so every scanline of either field shows lines n and n+1 averaged: a one-line
// detail lands in both fields at half strength instead of alternating at 25/30Hz.
//
// DISPLAY1 is two raster lines (one line per field) shorter than DISPLAY2, so circuit 1 never reads
// the line past the end of the framebuffer (that is texture VRAM). The last line of the picture is
// then circuit 2 alone at the blend weight, at the very bottom of the raster, under the bezel.
//
// Leaving the mode needs no undo: every mode switch goes through gsKit_init_screen, which resets
// the GS and writes PMODE, DISPFB1 and DISPLAY1 back to gsKit's single-circuit setup.
void Renderer::applyLineBlend(void)
{
    DIntr();
    GS_SET_DISPFB1(gsGlobal->ScreenBuffer[0] / 8192, // same front buffer as DISPFB2
                   gsGlobal->Width / 64,
                   gsGlobal->PSM,
                   0,
                   1); // one framebuffer line down
    GS_SET_DISPLAY1(gsGlobal->StartX + gsGlobal->StartXOffset,
                    gsGlobal->StartY + gsGlobal->StartYOffset,
                    gsGlobal->MagH,
                    gsGlobal->MagV,
                    gsGlobal->DW - 1,
                    gsGlobal->DH - 1 - 2);
    GS_SET_PMODE(1,     // read circuit 1 on (the line below)
                 1,     // read circuit 2 on (gsKit's own output)
                 1,     // blend with the fixed ALP value, not pixel alpha
                 0,     // (alpha output select, unused for display)
                 0,     // blend circuit 1 with circuit 2, not with the background colour
                 0x80); // ~50%
    EIntr();
}

void Renderer::setAspectRatio(enum rm_aratio dar)
{
    dar_ = dar;

    switch (dar_) {
        case RM_ARATIO_4_3:
            aspectWidth_ = 4; // width = width * 4 / 4
            break;
        case RM_ARATIO_16_9:
            aspectWidth_ = 3; // width = width * 3 / 4
            break;
    };
}

// 720p and 1080i are 16:9 with square pixels, so the 640x480 design grid is stretched across a 16:9
// screen whatever the Widescreen setting says; treat them as wide so art keeps its shape there too.
int Renderer::wideDisplay() const
{
    int mode = (vmode_ >= 0) ? vmode_ : gVMode;
    if (mode < RM_VMODE_AUTO || mode >= NUM_RM_VMODES)
        mode = RM_VMODE_AUTO;
    return gWideScreen != 0 || rm_mode_table[mode].aratio == RM_ARATIO_16_9;
}

int Renderer::wideScale(int x)
{
    return (x * aspectWidth_) >> 2;
}

// Get the pixel aspect ratio (how wide or narrow are the pixels?)
float Renderer::getPAR()
{
    float fPAR = (float)rm_mode_table[vmode_].PAR2 / (float)rm_mode_table[vmode_].PAR1;

    // In anamorphic mode the pixels are stretched to 16:9
    if ((dar_ == RM_ARATIO_16_9) && (rm_mode_table[vmode_].aratio == RM_ARATIO_4_3))
        fPAR *= 0.75f;

    // In interlaced frame mode, the pixel are (virtually) twice as high
    if ((gsGlobal->Interlace == GS_INTERLACED) && (gsGlobal->Field == GS_FRAME))
        fPAR *= 2.0f;

    return fPAR;
}

// Get interfaced frame mode
int Renderer::interlacedFrameMode()
{
    if ((gsGlobal->Interlace == GS_INTERLACED) && (gsGlobal->Field == GS_FRAME))
        return 1;

    return 0;
}

int Renderer::scaleXi(int x)
{
    return scaleX(x);
}

int Renderer::scaleYi(int y)
{
    return scaleY(y);
}

int Renderer::unScaleX(int x)
{
    return (x * 640) / displayWidth_;
}

void Renderer::setOverscan(int overscan)
{
    // Start from the mode's 4:3 safe area (centred), then shrink it by the user's Overscan setting.
    // The safe area is in full-height lines; an interlaced FRAME framebuffer holds half of them.
    int safeW = rm_mode_table[vmode_].safeW;
    int safeH = rm_mode_table[vmode_].safeH;
    if (interlacedFrameMode() == 1)
        safeH /= 2;
    int baseW = (safeW > 0 && safeW <= gsGlobal->Width) ? safeW : gsGlobal->Width;
    int baseH = (safeH > 0 && safeH <= gsGlobal->Height) ? safeH : gsGlobal->Height;

    displayXOff_ = (gsGlobal->Width - baseW) / 2 + (baseW * overscan) / (2 * 1000);
    displayYOff_ = (gsGlobal->Height - baseH) / 2 + (baseH * overscan) / (2 * 1000);
    displayWidth_ = gsGlobal->Width - (2 * displayXOff_);
    displayHeight_ = gsGlobal->Height - (2 * displayYOff_);

    renderXOff_ = (float)displayXOff_ - 0.5f;
    renderYOff_ = (float)displayYOff_ - 0.5f;

    if (interlacedFrameMode() == 1)
        renderYOff_ += 0.25f;
}

unsigned char Renderer::getHsync(void)
{
    return rm_mode_table[vmode_].hsync;
}

// ---- rm* API (renderman.h) -------------------------------------------------------------------

void rmInvalidateTexture(GSTEXTURE *txt)
{
    gRenderer.invalidateTexture(txt);
}

void rmUnloadTexture(GSTEXTURE *txt)
{
    gRenderer.unloadTexture(txt);
}

void rmStartFrame(void)
{
    gRenderer.startFrame();
}

volatile unsigned int rmFramesDrawn;

void rmEndFrame(void)
{
    gRenderer.endFrame();
    rmFramesDrawn++;
}

void rmInit()
{
    gRenderer.init();
}

int rmSetMode(int force)
{
    return gRenderer.setMode(force);
}

void rmGetScreenExtentsNative(int *w, int *h)
{
    gRenderer.getScreenExtentsNative(w, h);
}

void rmGetScreenExtents(int *w, int *h)
{
    gRenderer.getScreenExtents(w, h);
}

void rmEnd(void)
{
    gRenderer.end();
}

void rmDrawQuad(rm_quad_t *q)
{
    gRenderer.drawQuad(q);
}

void rmSetReflectionYOffset(int yoff)
{
    gRenderer.setReflectionYOffset(yoff);
}

void rmDrawPixmap(GSTEXTURE *txt, int x, int y, short aligned, int w, int h, short scaled, u64 color, int reflection)
{
    gRenderer.drawPixmap(txt, x, y, aligned, w, h, scaled, color, reflection);
}

void rmDrawOverlayPixmap(GSTEXTURE *overlay, int x, int y, short aligned, int w, int h, short scaled, u64 color, GSTEXTURE *inlay, int ulx, int uly, int urx, int ury, int blx, int bly, int brx, int bry, int reflection)
{
    gRenderer.drawOverlayPixmap(overlay, x, y, aligned, w, h, scaled, color, inlay, ulx, uly, urx, ury, blx, bly, brx, bry, reflection);
}

void rmDrawRect(int x, int y, int w, int h, u64 color)
{
    gRenderer.drawRect(x, y, w, h, color);
}

void rmDrawFramedRect(int x, int y, int w, int h, int border, u64 frameColor, u64 fillColor)
{
    gRenderer.drawFramedRect(x, y, w, h, border, frameColor, fillColor);
}

void rmDrawLine(int x1, int y1, int x2, int y2, u64 color)
{
    gRenderer.drawLine(x1, y1, x2, y2, color);
}

void rmDrawTriangles(GSTEXTURE *txt, int count, const float *xy, const float *uv, const u64 *colors)
{
    gRenderer.drawTriangles(txt, count, xy, uv, colors, 0);
}

void rmDrawBlendedTriangles(GSTEXTURE *txt, int count, const float *xy, const float *uv, const u64 *colors)
{
    gRenderer.drawTriangles(txt, count, xy, uv, colors, 1);
}

void rmSetDisplayOffset(int x, int y)
{
    gRenderer.setDisplayOffset(x, y);
}

void rmSetAspectRatio(enum rm_aratio dar)
{
    gRenderer.setAspectRatio(dar);
}

int rmIsWideDisplay(void)
{
    return gRenderer.wideDisplay();
}

int rmWideScale(int x)
{
    return gRenderer.wideScale(x);
}

float rmGetPAR()
{
    return gRenderer.getPAR();
}

int rmScaleX(int x)
{
    return gRenderer.scaleXi(x);
}

int rmScaleY(int y)
{
    return gRenderer.scaleYi(y);
}

int rmUnScaleX(int x)
{
    return gRenderer.unScaleX(x);
}

void rmSetOverscan(int overscan)
{
    gRenderer.setOverscan(overscan);
}

unsigned char rmGetHsync(void)
{
    return gRenderer.getHsync();
}
