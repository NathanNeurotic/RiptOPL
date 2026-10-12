// Animated "plasma" menu background: a 32x32 perlin-noise texture refreshed a few rows per frame and
// stretched over the screen. Split out of gui.cpp; the screen/theme colours are passed in.

#include "include/opl.h"
#include "include/plasma.h"
#include "include/renderman.h"

#include <math.h>
#include <stdlib.h>
#include <malloc.h>
#include <libvux.h>

static VU_VECTOR pgrad3[12] = {{1, 1, 0, 1}, {-1, 1, 0, 1}, {1, -1, 0, 1}, {-1, -1, 0, 1}, {1, 0, 1, 1}, {-1, 0, 1, 1}, {1, 0, -1, 1}, {-1, 0, -1, 1}, {0, 1, 1, 1}, {0, -1, 1, 1}, {0, 1, -1, 1}, {0, -1, -1, 1}};

static float mix(float a, float b, float t)
{
    return a + (b - a) * t;
}

float PlasmaBackground::smoothStep(float t) const
{
    return fadeTable_[(int)(t * kFadeSize)];
}

// The same as mix, but with 8 (2*4) values mixed at once
static void VU0MixVec(VU_VECTOR *a, VU_VECTOR *b, float mix, VU_VECTOR *res)
{
    unsigned int mixBits;
    __builtin_memcpy(&mixBits, &mix, sizeof(mixBits));
    asm volatile(
#if __GNUC__ > 3
        "lqc2           $vf1, (%[a])\n"        // load the first vector
        "lqc2           $vf2, (%[b])\n"        // load the second vector
        "qmtc2          %[mix], $vf3\n"        // move the mix value from reg to VU
        "vaddw.x        $vf5, $vf0, $vf0\n"    // vf5.x = 1
        "vsub.x         $vf4x, $vf5x, $vf3x\n" // subtract 1 - vf3,x, store the result in vf4.x
        "vmulax.xyzw    $ACC, $vf1, $vf3x\n"   // multiply vf1 by vf3.x, store the result in ACC
        "vmaddx.xyzw    $vf1, $vf2, $vf4x\n"   // multiply vf2 by vf4.x add ACC, store the result in vf1
        "sqc2           $vf1, (%[res])\n"      // transfer the result in acc to the ee
#else
        "lqc2           vf1, (%[a])\n"      // load the first vector
        "lqc2           vf2, (%[b])\n"      // load the second vector
        "qmtc2          %[mix], vf3\n"      // move the mix value from reg to VU
        "vaddw.x        vf5, vf00, vf00\n"  // vf5.x = 1
        "vsub.x         vf4x, vf5x, vf3x\n" // subtract 1 - vf3,x, store the result in vf4.x
        "vmulax.xyzw    ACC, vf1, vf3x\n"   // multiply vf1 by vf3.x, store the result in ACC
        "vmaddx.xyzw    vf1, vf2, vf4x\n"   // multiply vf2 by vf4.x add ACC, store the result in vf1
        "sqc2           vf1, (%[res])\n"    // transfer the result in acc to the ee
#endif
        : [res] "+r"(res), "=m"(*res)
        : [a] "r"(a), [b] "r"(b), [mix] "r"(mixBits), "m"(*a), "m"(*b));
}

float PlasmaBackground::noise(float x, float y, float z) const
{
    // Taken from: http://people.opera.com/patrickl/experiments/canvas/plasma/perlin-noise-classical.js
    // By Sean McCullough

    // Find unit grid cell containing point
    int X = floorf(x);
    int Y = floorf(y);
    int Z = floorf(z);

    // Get relative xyz coordinates of point within that cell
    x = x - X;
    y = y - Y;
    z = z - Z;

    // Wrap the integer cells at 255 (smaller integer period can be introduced here)
    X = X & 255;
    Y = Y & 255;
    Z = Z & 255;

    // Calculate a set of eight hashed gradient indices
    int gi000 = perm_[X + perm_[Y + perm_[Z]]] % 12;
    int gi001 = perm_[X + perm_[Y + perm_[Z + 1]]] % 12;
    int gi010 = perm_[X + perm_[Y + 1 + perm_[Z]]] % 12;
    int gi011 = perm_[X + perm_[Y + 1 + perm_[Z + 1]]] % 12;
    int gi100 = perm_[X + 1 + perm_[Y + perm_[Z]]] % 12;
    int gi101 = perm_[X + 1 + perm_[Y + perm_[Z + 1]]] % 12;
    int gi110 = perm_[X + 1 + perm_[Y + 1 + perm_[Z]]] % 12;
    int gi111 = perm_[X + 1 + perm_[Y + 1 + perm_[Z + 1]]] % 12;

    // The gradients of each corner are now:
    // g000 = grad3[gi000];
    // g001 = grad3[gi001];
    // g010 = grad3[gi010];
    // g011 = grad3[gi011];
    // g100 = grad3[gi100];
    // g101 = grad3[gi101];
    // g110 = grad3[gi110];
    // g111 = grad3[gi111];
    // Calculate noise contributions from each of the eight corners
    VU_VECTOR vec;
    vec.x = x;
    vec.y = y;
    vec.z = z;
    vec.w = 1;

    VU_VECTOR a, b;

    // float n000
    a.x = Vu0DotProduct(&pgrad3[gi000], &vec);

    vec.y -= 1;

    // float n010
    a.z = Vu0DotProduct(&pgrad3[gi010], &vec);

    vec.x -= 1;

    // float n110
    b.z = Vu0DotProduct(&pgrad3[gi110], &vec);

    vec.y += 1;

    // float n100
    b.x = Vu0DotProduct(&pgrad3[gi100], &vec);

    vec.z -= 1;

    // float n101
    b.y = Vu0DotProduct(&pgrad3[gi101], &vec);

    vec.y -= 1;

    // float n111
    b.w = Vu0DotProduct(&pgrad3[gi111], &vec);

    vec.x += 1;

    // float n011
    a.w = Vu0DotProduct(&pgrad3[gi011], &vec);

    vec.y += 1;

    // float n001
    a.y = Vu0DotProduct(&pgrad3[gi001], &vec);

    // Compute the fade curve value for each of x, y, z
    float u = smoothStep(x);
    float v = smoothStep(y);
    float w = smoothStep(z);

    // TODO: Low priority... This could be done on VU0 (xyzw for the first 4 mixes)
    // The result in sw
    // Interpolate along x the contributions from each of the corners
    VU_VECTOR rv;
    VU0MixVec(&b, &a, u, &rv);

    // TODO: The VU0MixVec could as well mix the results (as follows) - might improve performance...
    // Interpolate the four results along y
    float nxy0 = mix(rv.x, rv.z, v);
    float nxy1 = mix(rv.y, rv.w, v);
    // Interpolate the two last results along z
    float nxyz = mix(nxy0, nxy1, w);

    return nxyz;
}

static int colorDirection(unsigned char a, unsigned char b)
{
    if (a == b)
        return 0;
    else if (a > b)
        return -1;
    else
        return 1;
}


void PlasmaBackground::init()
{
    // background texture - for perlin
    tex_.Width = kWidth;
    tex_.Height = kHeight;
    tex_.Mem = static_cast<u32 *>(memalign(128, kWidth * kHeight * 4));
    tex_.PSM = GS_PSM_CT32;
    tex_.Filter = GS_FILTER_LINEAR;
    tex_.Vram = 0;
    tex_.VramClut = 0;
    tex_.Clut = NULL;
    tex_.ClutStorageMode = GS_CLUT_STORAGE_CSM1;

    // Precalculate the values for the perlin noise plasma
    int i;
    for (i = 0; i < 256; ++i) {
        perm_[i] = rand() % 256;
        perm_[i + 256] = perm_[i];
    }

    for (i = 0; i <= kFadeSize; ++i) {
        float t = (float)(i) / kFadeSize;

        fadeTable_[i] = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }
}

void PlasmaBackground::shutdown()
{
    if (tex_.Mem) {
        free(tex_.Mem);
        tex_.Mem = NULL;
    }
}

void PlasmaBackground::draw(const unsigned char *tint, const unsigned char *blendColor, int screenWidth, int screenHeight)
{
    int x, y;

    // transition the colors
    cur_[0] += colorDirection(cur_[0], tint[0]);
    cur_[1] += colorDirection(cur_[1], tint[1]);
    cur_[2] += colorDirection(cur_[2], tint[2]);

    // it's kRowsPerFrame rows a frame to stop being a resource hog
    if (row_ >= kHeight) {
        row_ = 0;
        z_ += dir_;

        if (z_ > 100.0f || z_ < -100.0f)
            dir_ = -dir_;
    }

    u32 *buf = tex_.Mem + kWidth * row_;
    int ymax = row_ + kRowsPerFrame;

    if (ymax > kHeight)
        ymax = kHeight;

    // plasma_blend_color (parity-audit #15): the gradient's LOW end, historically hardcoded black
    // (the old expression fper*cur_>>8 lerps black->bgColor). Lerp blend->bgColor instead;
    // the default black collapses to the exact old bytes (all terms non-negative there, and /256 of
    // a non-negative int is exactly >>8). Signed intermediates: the delta goes negative when the
    // blend is brighter than the tint -- divide instead of shifting (right-shift of a negative int
    // is implementation-defined); the result stays in [0,255] without clamping (fper<=256,
    // |delta|<=255: endpoints land exactly on blend / cur_). Every OTHER half of this feature
    // -- the Colors-page picker, the theme key, the default, the struct field -- was already here;
    // this loop was the one consumer that never read it, so the picker stored a colour the plasma
    // never used.
    const unsigned char *blend = blendColor;

    for (y = row_; y < ymax; y++) {
        for (x = 0; x < kWidth; x++) {
            u32 fper = noise((float)(2 * x) / kWidth, (float)(2 * y) / kHeight, z_) * 0x80 + 0x80;

            *buf = GS_SETREG_RGBA(
                (u32)(blend[0] + (((int)fper * ((int)cur_[0] - (int)blend[0])) / 256)),
                (u32)(blend[1] + (((int)fper * ((int)cur_[1] - (int)blend[1])) / 256)),
                (u32)(blend[2] + (((int)fper * ((int)cur_[2] - (int)blend[2])) / 256)),
                0x80);

            ++buf;
        }
    }

    row_ = ymax;
    rmInvalidateTexture(&tex_);
    rmDrawPixmap(&tex_, 0, 0, ALIGN_NONE, screenWidth, screenHeight, SCALING_NONE, gDefaultCol, 0);
}
