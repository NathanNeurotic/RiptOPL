// Crystal composition and geometry adapted from Launchstation's dash_bg.c (0BSD).
// Closed-form 24-second timing keeps every component continuous across the loop boundary.
#include "include/ambient.h"
#include <gsKit.h>
#include "include/renderman.h"

#include <algorithm>
#include <math.h>
#include <string.h>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 2.0f * kPi;
constexpr float kFan6X[7] = {1.0f, 0.5f, -0.5f, -1.0f, -0.5f, 0.5f, 1.0f};
constexpr float kFan6Y[7] = {0.0f, 0.8660254f, 0.8660254f, 0.0f, -0.8660254f, -0.8660254f, 0.0f};
constexpr float kFan8X[9] = {1.0f, 0.7071068f, 0.0f, -0.7071068f, -1.0f, -0.7071068f, 0.0f, 0.7071068f, 1.0f};
constexpr float kFan8Y[9] = {0.0f, 0.7071068f, 1.0f, 0.7071068f, 0.0f, -0.7071068f, -1.0f, -0.7071068f, 0.0f};

float hash01(int index, int salt)
{
    u32 value = (u32)(index + 1) * 0x9e3779b9u ^ (u32)salt * 0x85ebca6bu;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return (value & 0x00ffffffu) / 16777215.0f;
}

u64 rgba(int r, int g, int b, int a)
{
    return (u64)(u8)r | ((u64)(u8)g << 8) | ((u64)(u8)b << 16) | ((u64)(u8)a << 24);
}

u64 withAlpha(u64 rgb, int alpha)
{
    return rgb | ((u64)(u8)alpha << 24);
}

u64 hueRgb(int hue, int sat, int val)
{
    hue %= 360;
    if (hue < 0)
        hue += 360;
    int region = hue / 60;
    int rem = (hue - region * 60) * 255 / 60;
    int p = (val * (255 - sat)) / 255;
    int q = (val * (255 - (sat * rem) / 255)) / 255;
    int t = (val * (255 - (sat * (255 - rem)) / 255)) / 255;
    int r, g, b;
    switch (region) {
        case 0: r = val; g = t; b = p; break;
        case 1: r = q; g = val; b = p; break;
        case 2: r = p; g = val; b = t; break;
        case 3: r = p; g = q; b = val; break;
        case 4: r = t; g = p; b = val; break;
        default: r = val; g = p; b = q; break;
    }
    return rgba(r, g, b, 0);
}
} // namespace

void AmbientEffects::init()
{
    triangles_ = 0;
    phaseUs_ = 0;
    lastTick_ = 0;
    lastFrame_ = 0;
    launchStartTick_ = 0;
    started_ = false;
    launching_ = false;

    for (int i = 0; i < kGlintCount; i++) {
        Glint &g = glints_[i];
        g.x = 0.06f + 0.88f * hash01(i, 0);
        g.y = 0.10f + 0.76f * hash01(i, 1);
        g.radiusX = 0.012f + 0.028f * hash01(i, 2);
        g.radiusY = 0.012f + 0.032f * hash01(i, 3);
        g.phaseX = kTau * hash01(i, 4);
        g.phaseY = kTau * hash01(i, 5);
        g.radius = 2.0f + 3.0f * hash01(i, 6);
        g.alpha = 45.0f + 55.0f * hash01(i, 7);
        g.xCycles = 1 + i % 2;
        g.yCycles = 1 + (i / 2) % 2;
        g.twinkleCycles = 2 + i % 3;
    }

    static const float x[kTowerCount] = {372, 418, 450, 488, 522, 560, 604};
    static const float height[kTowerCount] = {150, 230, 175, 290, 205, 250, 160};
    static const float radius[kTowerCount] = {11, 15, 12, 18, 13, 16, 11};
    for (int i = 0; i < kTowerCount; i++) {
        towers_[i].x = x[i];
        towers_[i].height = height[i];
        towers_[i].radius = radius[i];
        towers_[i].rotation = kTau * hash01(i, 20);
        towers_[i].phase = kTau * hash01(i, 21);
        towers_[i].hue = 195 + (int)(60 * hash01(i, 22));
    }
}

void AmbientEffects::shutdown()
{
    started_ = false;
    launching_ = false;
}

void AmbientEffects::beginLaunch(clock_t now)
{
    launchStartTick_ = now;
    launching_ = true;
}

void AmbientEffects::endLaunch()
{
    launching_ = false;
}

void AmbientEffects::advance(clock_t now, unsigned int frame)
{
    if (started_ && frame == lastFrame_)
        return;

    if (started_) {
        u32 elapsed = (u32)now - (u32)lastTick_;
        phaseUs_ = (phaseUs_ + elapsed) % (u32)kLoopUs;
    }
    lastTick_ = now;
    lastFrame_ = frame;
    started_ = true;
}

void AmbientEffects::triangle(float x0, float y0, u64 c0, float x1, float y1, u64 c1, float x2, float y2, u64 c2)
{
    int vertex = triangles_ * 3;
    int coordinate = triangles_ * 6;
    xy_[coordinate] = x0;
    xy_[coordinate + 1] = y0;
    xy_[coordinate + 2] = x1;
    xy_[coordinate + 3] = y1;
    xy_[coordinate + 4] = x2;
    xy_[coordinate + 5] = y2;
    colors_[vertex] = c0;
    colors_[vertex + 1] = c1;
    colors_[vertex + 2] = c2;
    triangles_++;
}

void AmbientEffects::glow(float x, float y, float rx, float ry, u64 rgb, int alpha, int segments)
{
    u64 center = withAlpha(rgb, alpha);
    for (int k = 0; k < segments; k++) {
        float x0 = segments == 8 ? kFan8X[k] : kFan6X[k];
        float y0 = segments == 8 ? kFan8Y[k] : kFan6Y[k];
        float x1 = segments == 8 ? kFan8X[k + 1] : kFan6X[k + 1];
        float y1 = segments == 8 ? kFan8Y[k + 1] : kFan6Y[k + 1];
        triangle(x, y, center, x + rx * x0, y + ry * y0, rgb, x + rx * x1, y + ry * y1, rgb);
    }
}

void AmbientEffects::tower(const Tower &tower, float phase, float pull, float surge, int width, int height)
{
    const float sx = width / 640.0f;
    const float sy = height / 480.0f;
    const float strength = 0.35f * (1.0f + 1.8f * surge) * (1.0f - pull);
    const float baseY = 470.0f * sy;
    const float towerHeight = tower.height * sy * (1.0f - pull);
    const float bob = 6.0f * sy * sinf(phase * 2.0f + tower.phase) * (1.0f - pull);
    const float topY = baseY - towerHeight + bob;
    const float tipY = topY - tower.radius * sy * 2.4f;
    const float rotation = tower.rotation + phase;

    // Only the pointed cap is drawn: long side walls read as vertical slivers on this backdrop.
    for (int k = 0; k < 6; k++) {
        float a0 = rotation + k * (kTau / 6.0f);
        float a1 = a0 + kTau / 6.0f;
        float x0 = tower.x * sx + cosf(a0) * tower.radius * sx;
        float x1 = tower.x * sx + cosf(a1) * tower.radius * sx;
        float facing = (sinf(a0) + sinf(a1)) * 0.5f;
        if (facing < -0.05f || x1 <= x0)
            continue;
        float nx = cosf(a0 + kPi / 6.0f);
        float light = 0.35f + 0.65f * std::min(1.0f, std::max(0.0f, 0.6f * nx + 0.8f * facing));
        float alpha = strength * light;
        u64 top = withAlpha(hueRgb(tower.hue + (int)(20 * nx), 120, 255), (int)(90 * alpha));
        u64 tip = withAlpha(hueRgb(tower.hue - 10, 90, 255), (int)(120 * alpha));
        triangle(x0, topY, top, x1, topY, top, tower.x * sx, tipY, tip);
    }
    glow(tower.x * sx, tipY + tower.radius * sy, tower.radius * sx * 2.6f, tower.radius * sy * 2.6f,
         rgba(170, 220, 255, 0), (int)(60 * strength), 6);
}

void AmbientEffects::build(clock_t now, const unsigned char *background, u64 text, int width, int height)
{
    triangles_ = 0;
    const float phase = kTau * (float)phaseUs_ / (float)kLoopUs;
    float launch = 0.0f;
    float pull = 0.0f;
    float surge = 0.0f;
    if (launching_) {
        u32 elapsed = (u32)now - (u32)launchStartTick_;
        launch = std::min(elapsed / (float)kLaunchUs, 1.0f);
        pull = launch * launch * (3.0f - 2.0f * launch);
        surge = launch >= 1.0f ? 0.0f : sinf(kPi * launch);
    }
    const float launchAlpha = (1.0f + 1.8f * surge) * (1.0f - pull);
    const float scale = std::min(width / 640.0f, height / 480.0f);

    int accentR = text & 0xff;
    int accentG = (text >> 8) & 0xff;
    int accentB = (text >> 16) & 0xff;
    int bgMax = std::max(background[0], std::max(background[1], background[2]));
    float lift = bgMax > 0 ? 200.0f / bgMax : 0.0f;
    int baseR = bgMax > 0 ? std::min(224, (int)(background[0] * lift)) : accentR;
    int baseG = bgMax > 0 ? std::min(224, (int)(background[1] * lift)) : accentG;
    int baseB = bgMax > 0 ? std::min(224, (int)(background[2] * lift)) : accentB;
    u64 glintRgb = rgba(std::min(255, (baseR + accentR) / 2 + 64),
                        std::min(255, (baseG + accentG) / 2 + 64),
                        std::min(255, (baseB + accentB) / 2 + 64), 0);

    static const int hues[kOrbCount] = {232, 188, 268, 32};
    static const float radii[kOrbCount] = {250, 210, 190, 150};
    for (int i = 0; i < kOrbCount; i++) {
        float cx = width * 0.5f + sinf(phase * (1 + i % 2) + i * 1.7f) * (220.0f - i * 20.0f) * scale;
        float cy = 210.0f * scale + cosf(phase * (1 + (i + 1) % 2) + i * 2.3f) * (110.0f + i * 10.0f) * scale;
        cx += (width * 0.5f - cx) * pull;
        cy += (height * 0.5f - cy) * pull;
        int alpha = (int)((i == 3 ? 16 : 34) * launchAlpha);
        glow(cx, cy, radii[i] * scale, radii[i] * scale * 0.8f, hueRgb(hues[i], 170, 200), alpha, 8);
    }

    for (int i = 0; i < kTowerCount; i++)
        tower(towers_[i], phase, pull, surge, width, height);

    for (int i = 0; i < kGlintCount; i++) {
        const Glint &g = glints_[i];
        float x = width * (g.x + g.radiusX * cosf(phase * g.xCycles + g.phaseX));
        float y = height * (g.y + g.radiusY * sinf(phase * g.yCycles + g.phaseY));
        float radius = g.radius * scale * (0.90f + 0.10f * cosf(phase + g.phaseX)) * (1.0f + 0.8f * surge);
        int alpha = std::min(0x80, (int)(g.alpha * (0.70f + 0.30f * sinf(phase * g.twinkleCycles + g.phaseY)) * launchAlpha));
        float curve = sinf(kPi * pull) * width * 0.04f * (i & 1 ? 1.0f : -1.0f);
        x += (width * 0.5f - x) * pull + curve;
        y += (height * 0.5f - y) * pull - curve * 0.25f;
        glow(x, y, radius, radius, glintRgb, alpha, 6);
    }

}

void AmbientEffects::draw(bool enabled, clock_t now, unsigned int frame, const unsigned char *background, u64 text, int width, int height)
{
    advance(now, frame);
    if (!enabled || background == NULL || width <= 0 || height <= 0)
        return;

    build(now, background, text, width, height);
    rmDrawBlendedTriangles(NULL, triangles_, xy_, NULL, colors_);
}
