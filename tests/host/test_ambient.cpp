// HOST-TEST: sdk-headers
// AmbientEffects: a closed 24-second loop, frame-rate-independent time, clock rollover and bounded geometry.
#include "check.h"
#include <algorithm>
#include <math.h>
#include <map>
#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include <gsKit.h>
#include <tamtypes.h>
#include "include/renderman.h"

static struct Capture
{
    int calls = 0;
    int count = 0;
    std::vector<float> xy;
    std::vector<u64> colors;
} captured;

void rmDrawBlendedTriangles(GSTEXTURE *txt, int count, const float *xy, const float *uv, const u64 *colors)
{
    CHECK(txt == NULL);
    CHECK(uv == NULL);
    captured.calls++;
    captured.count = count;
    captured.xy.assign(xy, xy + count * 6);
    captured.colors.assign(colors, colors + count * 3);
}

#include "../../src/gfx/ambient.cpp"

static Capture draw(AmbientEffects &fx, bool enabled, clock_t now, unsigned int frame, int width = 640, int height = 480)
{
    captured = Capture{};
    static const unsigned char background[] = {0x0C, 0x3A, 0xA8};
    fx.draw(enabled, now, frame, background, 0x0000000080C0E0FFULL, width, height);
    return captured;
}

static void checkSame(const Capture &a, const Capture &b)
{
    CHECK(a.count == b.count);
    CHECK(a.xy == b.xy);
    CHECK(a.colors == b.colors);
}

static float largestStep(const Capture &a, const Capture &b)
{
    if (a.xy.size() != b.xy.size())
        return 1000000.0f;
    float largest = 0.0f;
    for (size_t i = 0; i < a.xy.size(); i++)
        largest = fmaxf(largest, fabsf(a.xy[i] - b.xy[i]));
    return largest;
}

static int largestAlphaStep(const Capture &a, const Capture &b)
{
    if (a.colors.size() != b.colors.size())
        return 256;
    int largest = 0;
    for (size_t i = 0; i < a.colors.size(); i++) {
        int aa = (a.colors[i] >> 24) & 0xff;
        int ba = (b.colors[i] >> 24) & 0xff;
        largest = std::max(largest, abs(aa - ba));
    }
    return largest;
}

static void checkGeometry(const Capture &capture, int width, int height)
{
    CHECK(capture.calls == 1);
    CHECK(capture.count > 0 && capture.count <= AmbientEffects::kMaxTriangles);
    CHECK(capture.xy.size() == (size_t)capture.count * 6);
    CHECK(capture.colors.size() == (size_t)capture.count * 3);
    for (size_t i = 0; i < capture.xy.size(); i += 2) {
        CHECK(std::isfinite(capture.xy[i]) && std::isfinite(capture.xy[i + 1]));
        CHECK(capture.xy[i] >= -width / 4 && capture.xy[i] <= width * 5 / 4);
        CHECK(capture.xy[i + 1] >= -height / 4 && capture.xy[i + 1] <= height * 5 / 4);
    }
    for (u64 color : capture.colors) {
        CHECK((color >> 32) == 0);
        CHECK(((color >> 24) & 0xff) <= 0x80);
    }
}

static void checkCapGeometry(const Capture &capture)
{
    std::map<int, int> faces;
    std::map<int, float> widths;
    for (int triangle = 0; triangle < capture.count; triangle++) {
        u64 top = capture.colors[triangle * 3];
        if (top != capture.colors[triangle * 3 + 1] || top == capture.colors[triangle * 3 + 2])
            continue;
        const float *xy = capture.xy.data() + triangle * 6;
        CHECK(xy[1] == xy[3] && xy[5] < xy[1]);
        int center = (int)lroundf(xy[4]);
        faces[center]++;
        widths[center] = std::max(widths[center], fabsf(xy[2] - xy[0]));
    }
    CHECK(faces.size() == AmbientEffects::kTowerCount);
    for (const auto &cap : faces) {
        CHECK(cap.second >= 2);
        CHECK(widths[cap.first] >= 5.0f);
    }
}

int main()
{
    AmbientEffects fx;
    fx.init();

    // One or many complete periods return every vertex and colour to its exact starting value.
    Capture start = draw(fx, true, 0, 0);
    checkGeometry(start, 640, 480);
    CHECK(start.count > AmbientEffects::kOrbCount * AmbientEffects::kGlowSegments + AmbientEffects::kGlintCount * AmbientEffects::kGlintSegments);
    CHECK(start.count <= AmbientEffects::kOrbCount * AmbientEffects::kGlowSegments + AmbientEffects::kGlintCount * AmbientEffects::kGlintSegments + AmbientEffects::kTowerCount * 12);
    Capture quarterLoop = draw(fx, true, AmbientEffects::kLoopUs / 4, 1);
    CHECK(start.xy != quarterLoop.xy);
    Capture oneLoop = draw(fx, true, AmbientEffects::kLoopUs, 2);
    checkSame(start, oneLoop);
    Capture twoLoops = draw(fx, true, AmbientEffects::kLoopUs * 2, 2);
    checkSame(start, twoLoops);
    Capture tenLoops = draw(fx, true, AmbientEffects::kLoopUs * 10, 3);
    checkSame(start, tenLoops);

    // Crossing the loop boundary is an ordinary 2 ms step: no teleport or brightness jump.
    Capture before = draw(fx, true, AmbientEffects::kLoopUs * 10 + AmbientEffects::kLoopUs - 1000, 4);
    Capture after = draw(fx, true, AmbientEffects::kLoopUs * 11 + 1000, 5);
    CHECK(largestStep(before, after) < 1.0f);
    CHECK(largestAlphaStep(before, after) <= 2);

    // A repeated draw in one rendered frame reuses the phase, including during screen transitions.
    fx.init();
    Capture frameStart = draw(fx, true, 1000, 7);
    Capture sameFrame = draw(fx, true, 9000, 7);
    checkSame(frameStart, sameFrame);
    Capture nextFrame = draw(fx, true, 11000, 8);
    fx.init();
    draw(fx, true, 0, 0);
    Capture tenMs = draw(fx, true, 10000, 1);
    checkSame(nextFrame, tenMs);

    // Fractional frame durations still total exactly one second at 30, 50 and 60 Hz.
    Capture rateReference;
    for (int rate : {30, 50, 60}) {
        AmbientEffects paced;
        paced.init();
        for (int frame = 0; frame <= rate; frame++)
            rateReference = draw(paced, true, (clock_t)frame * CLOCKS_PER_SEC / rate, frame);
        if (rate == 30)
            start = rateReference;
        else
            checkSame(start, rateReference);
    }

    // Unsigned clock subtraction crosses a 32-bit EE clock wrap without a phase discontinuity.
    AmbientEffects wrapped, reference;
    wrapped.init();
    reference.init();
    clock_t beforeWrap = (clock_t)(u32)(UINT32_MAX - 9999);
    draw(wrapped, true, 0, 0);
    draw(wrapped, true, beforeWrap, 1);
    Capture acrossWrap = draw(wrapped, true, 5000, 2);
    clock_t expectedPhase = (clock_t)(((u64)(u32)beforeWrap + 15000) % (u64)AmbientEffects::kLoopUs);
    draw(reference, true, 0, 0);
    Capture fifteenMsPastWrap = draw(reference, true, expectedPhase, 1);
    checkSame(acrossWrap, fifteenMsPastWrap);

    // Sampling the whole period keeps every mode's geometry finite and inside the guarded margins.
    const int extents[][2] = {{640, 480}, {640, 448}, {640, 512}};
    for (const auto &extent : extents) {
        AmbientEffects sampled;
        sampled.init();
        for (clock_t t = 0; t < AmbientEffects::kLoopUs; t += 977777)
            checkGeometry(draw(sampled, true, t, (unsigned int)t), extent[0], extent[1]);
    }

    // Every cap keeps multiple meaningful front faces throughout the complete rotation loop.
    AmbientEffects caps;
    caps.init();
    for (clock_t t = 0; t < AmbientEffects::kLoopUs; t += 1000)
        checkCapGeometry(draw(caps, true, t, (unsigned int)t));

    // Launch is a one-shot 700 ms convergence: idle at its start, dark at its end, then cleanly reusable.
    CHECK(AmbientEffects::kLaunchUs == 700000);
    fx.init();
    Capture idleAtLaunch = draw(fx, true, 0, 0);
    fx.beginLaunch(0);
    checkSame(idleAtLaunch, draw(fx, true, 0, 1));
    Capture launchMiddle = draw(fx, true, AmbientEffects::kLaunchUs / 2, 2);
    checkGeometry(launchMiddle, 640, 480);
    Capture launchEnd = draw(fx, true, AmbientEffects::kLaunchUs, 3);
    checkGeometry(launchEnd, 640, 480);
    for (u64 color : launchEnd.colors)
        CHECK(((color >> 24) & 0xff) == 0);
    fx.endLaunch();
    Capture resumed = draw(fx, true, AmbientEffects::kLaunchUs, 4);
    AmbientEffects ordinary;
    ordinary.init();
    draw(ordinary, true, 0, 0);
    checkSame(resumed, draw(ordinary, true, AmbientEffects::kLaunchUs, 1));

    // The same one-shot timing remains correct when its clock crosses 32-bit zero.
    clock_t launchTick = (clock_t)(u32)(UINT32_MAX - 1000);
    fx.beginLaunch(launchTick);
    Capture wrappedLaunch = draw(fx, true, (clock_t)(u32)(launchTick + AmbientEffects::kLaunchUs), 5);
    for (u64 color : wrappedLaunch.colors)
        CHECK(((color >> 24) & 0xff) == 0);
    fx.endLaunch();

    // Off emits no GS work; enabling later starts from the continuously tracked shared phase.
    fx.init();
    Capture off = draw(fx, false, 0, 0);
    CHECK(off.calls == 0 && off.count == 0 && off.xy.empty() && off.colors.empty());
    off = draw(fx, false, 500000, 1);
    CHECK(off.calls == 0 && off.count == 0);
    CHECK(draw(fx, true, 1000000, 2).calls == 1);

    TEST_DONE("ambient");
}
