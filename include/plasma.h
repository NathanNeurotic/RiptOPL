#ifndef __PLASMA_H
#define __PLASMA_H

#include <gsKit.h>

// Animated perlin-noise menu background (see src/plasma.cpp). One instance lives in gui.
class PlasmaBackground
{
public:
    static constexpr int kWidth = 32;
    static constexpr int kHeight = 32;
    static constexpr int kRowsPerFrame = 6;
    static constexpr int kFadeSize = 256;

    void init();
    void shutdown();
    // Advance the animation by one frame's worth of rows and draw it full screen.
    // tint = the gradient's high end (theme bgColor), blendColor = its low end (theme plasBlendColor).
    void draw(const unsigned char *tint, const unsigned char *blendColor, int screenWidth, int screenHeight);
    // Quintic ease curve 0..1 (table driven; valid after init()). Also used for screen transitions.
    float ease(float t) const { return smoothStep(t); }

private:
    float noise(float x, float y, float z) const;
    float smoothStep(float t) const;

    GSTEXTURE tex_ = {};
    int perm_[512] = {};
    float fadeTable_[kFadeSize + 1] = {};
    float dir_ = 0.02f;
    float z_ = -100.0f;
    int row_ = 0;
    unsigned char cur_[3] = {0, 0, 0};
};

#endif
