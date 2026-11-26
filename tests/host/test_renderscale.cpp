// HOST-TEST: sdk-headers
// Host test for the renderer's design-unit -> display-pixel scaling (include/renderer.h displayX/displayY/displayW).
// Whole design units must land on exactly the pixel the old integer maths gave (nothing at rest moves), and a
// fractional one must reach the display pixels in between (a moving cover steps 1 display pixel, not 2 or 3).
#include <tamtypes.h>
#include <gsKit.h>
#define private public
#include "include/renderer.h"
#undef private
#include "check.h"

int main()
{
    Renderer r;
    // Display sizes after overscan seen in the mode table: 480i/p, 576i, 720p, 1080i, and some odd overscan widths.
    const int widths[] = {640, 704, 1280, 1920, 1824, 1216, 608, 668};
    const int heights[] = {224, 256, 448, 480, 512, 540, 576, 720, 1026};
    int mismatches = 0;
    for (int wi = 0; wi < (int)(sizeof(widths) / sizeof(widths[0])); wi++) {
        for (int hi = 0; hi < (int)(sizeof(heights) / sizeof(heights[0])); hi++) {
            r.displayWidth_ = widths[wi];
            r.displayHeight_ = heights[hi];
            for (int aspect = 3; aspect <= 4; aspect++) {
                r.aspectWidth_ = aspect;
                for (int v = -700; v <= 1400; v++) {
                    if (r.displayX((float)v) != (v * widths[wi]) / 640)
                        mismatches++;
                    if (r.displayY((float)v) != (v * heights[hi]) / 480)
                        mismatches++;
                    if (r.displayW((float)v, SCALING_RATIO) != ((v * aspect * widths[wi]) / 640) >> 2)
                        mismatches++;
                    if (r.displayW((float)v, 0) != (v * widths[wi]) / 640)
                        mismatches++;
                }
            }
        }
    }
    CHECK(mismatches == 0);

    // 1080i: one design unit is 3 display pixels across; a third of one now reaches the pixel in between.
    r.displayWidth_ = 1920;
    r.displayHeight_ = 1080;
    CHECK(r.displayX(100.0f) == 300);
    CHECK(r.displayX(100.34f) == 301);
    CHECK(r.displayX(100.67f) == 302);
    CHECK(r.displayX(101.0f) == 303);
    CHECK(r.displayY(10.5f) == 23); // 2.25 lines a unit: 23.625 -> 23, where 10 -> 22
    TEST_DONE("renderscale");
}
