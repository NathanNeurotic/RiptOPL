#ifndef __RENDERER_H
#define __RENDERER_H

// The GS renderer behind the rm* API in renderman.h: video mode, display geometry, aspect ratio and the
// draw primitives. One instance lives in src/renderman.cpp; the rm* functions forward to it.

#include "renderman.h"

class Renderer
{
public:
    constexpr Renderer() = default;
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    void invalidateTexture(GSTEXTURE *txt);
    void unloadTexture(GSTEXTURE *txt);
    void startFrame(void);
    void endFrame(void);
    void init();
    int setMode(int force);
    void getScreenExtentsNative(int *w, int *h);
    void getScreenExtents(int *w, int *h);
    void end(void);
    void drawQuad(rm_quad_t *q);
    void setReflectionYOffset(int yoff);
    void drawPixmap(GSTEXTURE *txt, int x, int y, short aligned, int w, int h, short scaled, u64 color, int reflection);
    void drawOverlayPixmap(GSTEXTURE *overlay, int x, int y, short aligned, int w, int h, short scaled, u64 color, GSTEXTURE *inlay, int ulx, int uly, int urx, int ury, int blx, int bly, int brx, int bry, int reflection);
#ifdef RETROACHIEVEMENTS
    void drawInlayPixmap(GSTEXTURE *txt, int x, int y, short aligned, int w, int h, short scaled, u64 color, int ulx, int uly, int urx, int ury, int blx, int bly, int brx, int bry);
#endif
    void drawRect(int x, int y, int w, int h, u64 color);
    void drawFramedRect(int x, int y, int w, int h, int border, u64 frameColor, u64 fillColor);
    void drawLine(int x1, int y1, int x2, int y2, u64 color);
    void setDisplayOffset(int x, int y);
    void setAspectRatio(enum rm_aratio dar);
    int wideScale(int x);
    float getPAR();
    int scaleXi(int x);
    int scaleYi(int y);
    int unScaleX(int x);
    void setOverscan(int overscan);
    unsigned char getHsync(void);

    // Vsync interrupt: wake the GUI thread once per frame (called from the handler in renderman.cpp).
    void onVSync();

private:
    void setupQuad(GSTEXTURE *txt, int x, int y, short aligned, int w, int h, short scaled, u64 color, rm_quad_t *q);
    void drawReflection(GSTEXTURE *txt, rm_quad_t *q, float bottomY, float botV);
    int interlacedFrameMode();

    // 640x480 virtual -> display pixels (after overscan).
    template <typename T>
    auto scaleX(T x) const
    {
        return (x * displayWidth_) / 640;
    }
    template <typename T>
    auto scaleY(T y) const
    {
        return (y * displayHeight_) / 480;
    }

    int order_ = 0;
    short int vmode_ = -1;
    u8 hires_ = 0;
    u8 wakeCount_ = 0;
    int vsyncId_ = -1;

    // Display Aspect Ratio
    int aspectWidth_ = 4;
    enum rm_aratio dar_ = RM_ARATIO_4_3;

    // Display dimensions after overscan compensation
    int displayWidth_ = 0;
    int displayHeight_ = 0;
    int displayXOff_ = 0;
    int displayYOff_ = 0;

    // Transposition values including overscan compensation
    float renderXOff_ = 0.0f;
    float renderYOff_ = 0.0f;

    // Vertical offset (px) applied to the coverflow reflection; set per-element by drawCoverFlow
    // from the theme's reflection_offset. 0 = flush under the cover, negative = up, positive = down.
    int reflectionYOff_ = 0;
};

#endif
