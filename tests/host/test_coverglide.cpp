// Host test for src/gfx/coverglide.cpp: the hold L3 layout switch's NULD-style cover flight.
#include "src/gfx/coverglide.cpp"
#include "check.h"

static glide_box_t box(float x, float y, float w, float h)
{
    glide_box_t b = {x, y, w, h};
    return b;
}

int main()
{
    // Before any switch: boxes pass through untouched, at full alpha.
    coverGlideFrame();
    glide_box_t a = box(100, 200, 50, 70), b = box(160, 200, 50, 70);
    CHECK(coverGlide("Game A", 1, 0, &a) == 0x80);
    CHECK(coverGlide("Game B", 2, 1, &b) == 0x80);
    CHECK(a.x == 100 && b.x == 160);
    CHECK(!coverGlideActive());

    // Hold L3: the new layout puts A at 400 and B at 500, and C (not on screen before) at 600.
    coverGlideStart();
    CHECK(coverGlideActive());
    coverGlideFrame();
    a = box(400, 300, 100, 140);
    b = box(500, 300, 100, 140);
    glide_box_t c = box(600, 300, 100, 140);
    CHECK(coverGlide("Game A", 1, 0, &a) == 0x80);
    // A (the selected game) starts at once: 16 % of the way.
    CHECK(fabsf(a.x - (100 + 300 * 0.16f)) < 0.01f);
    CHECK(fabsf(a.w - (50 + 50 * 0.16f)) < 0.01f);
    // B, one step away, waits 2 frames where it was.
    CHECK(coverGlide("Game B", 2, 1, &b) == 0x80);
    CHECK(b.x == 160);
    // C fades in where it lands (after its own wave of 4 frames).
    CHECK(coverGlide("Game C", 3, 2, &c) == 0);
    CHECK(c.x == 600);

    // Drawn twice in one frame: moves once.
    glide_box_t again = box(400, 300, 100, 140);
    coverGlide("Game A", 1, 0, &again);
    CHECK(fabsf(again.x - a.x) < 0.001f);
    // A second copy of A elsewhere in the same frame (a short list shown twice) stays at its own place.
    glide_box_t copy = box(900, 300, 100, 140);
    coverGlide("Game A", 1, 0, &copy);
    CHECK(copy.x == 900);

    // Run it out: every cover lands exactly, C fully opaque, and the glide ends by itself.
    int frames = 1;
    while (coverGlideActive() && frames < 200) {
        coverGlideFrame();
        a = box(400, 300, 100, 140);
        b = box(500, 300, 100, 140);
        c = box(600, 300, 100, 140);
        int aa = coverGlide("Game A", 1, 0, &a), ab = coverGlide("Game B", 2, 1, &b), ac = coverGlide("Game C", 3, 2, &c);
        (void)aa, (void)ab;
        if (!coverGlideActive())
            break;
        frames++;
        if (frames > 60)
            CHECK(ac == 0x80);
    }
    CHECK(!coverGlideActive());
    CHECK(frames < 120);
    CHECK(a.x == 400 && a.y == 300 && a.w == 100 && a.h == 140);
    CHECK(b.x == 500 && c.x == 600);

    // Frames drawn while the theme loads (no cover yet) do not end it; a layout that never draws a cover ends it at
    // the limit.
    coverGlideStart();
    for (int i = 0; i < 6; i++)
        coverGlideFrame();
    CHECK(coverGlideActive());
    for (int i = 0; i < GLIDE_FRAMES; i++)
        coverGlideFrame();
    CHECK(!coverGlideActive());

    // Many covers never overflow the table.
    for (int i = 0; i < 200; i++) {
        coverGlideFrame();
        glide_box_t d = box(i, i, 10, 10);
        CHECK(coverGlide("Many", i, 0, &d) == 0x80);
    }
    TEST_DONE("coverglide");
}
