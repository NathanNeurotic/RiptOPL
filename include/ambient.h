#ifndef __AMBIENT_H
#define __AMBIENT_H

#include <tamtypes.h>
#include <time.h>

class AmbientEffects
{
public:
    static constexpr clock_t kLoopUs = 24000000;
    static constexpr clock_t kLaunchUs = 700000;
    static constexpr int kOrbCount = 4;
    static constexpr int kGlintCount = 32;
    static constexpr int kGlintSegments = 6;
    static constexpr int kTowerCount = 7;
    static constexpr int kOrbTriangles = 40; // an 8-triangle core and two 16-triangle rings
    static constexpr int kMaxTriangles = kOrbCount * kOrbTriangles + kGlintCount * kGlintSegments + kTowerCount * 12;

    void init();
    void shutdown();
    void beginLaunch(clock_t now);
    void endLaunch();
    void draw(bool enabled, clock_t now, unsigned int frame, const unsigned char *background, u64 text, int width, int height);

private:
    struct Glint
    {
        float x, y, radiusX, radiusY;
        float phaseX, phaseY, radius, alpha;
        int xCycles, yCycles, twinkleCycles;
    };
    struct Tower
    {
        float x, height, radius, rotation, phase;
        int hue;
    };

    void advance(clock_t now, unsigned int frame);
    void build(clock_t now, const unsigned char *background, u64 text, int width, int height);
    void triangle(float x0, float y0, u64 c0, float x1, float y1, u64 c1, float x2, float y2, u64 c2);
    void glow(float x, float y, float rx, float ry, u64 rgb, int alpha);
    void orb(float x, float y, float rx, float ry, float cosR, float sinR, u64 rgb, int alpha);
    void tower(const Tower &tower, float phase, float pull, float surge, int width, int height);

    Glint glints_[kGlintCount];
    Tower towers_[kTowerCount];
    float xy_[kMaxTriangles * 6];
    u64 colors_[kMaxTriangles * 3];
    int triangles_ = 0;
    u32 phaseUs_ = 0;
    clock_t lastTick_ = 0;
    clock_t launchStartTick_ = 0;
    unsigned int lastFrame_ = 0;
    bool started_ = false;
    bool launching_ = false;
};

#endif
