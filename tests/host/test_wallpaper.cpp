// HOST-TEST: sdk-headers
// Berion's theme toggles: the User Wallpaper header check (texPngHeaderIsSmallT8) and the old-theme icon fallback
// (texNewerThanStockOpl), both in include/textures.h.
#include <string.h>
#include "include/opl.h"
#include "include/textures.h"
#include "check.h"

// Signature + IHDR of a w x h PNG with the given bit depth and colour type (3 = palette).
static void pngHeader(unsigned char *h, unsigned int w, unsigned int ht, unsigned char depth, unsigned char colorType)
{
    static const unsigned char head[16] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    memcpy(h, head, 16);
    h[16] = w >> 24, h[17] = w >> 16, h[18] = w >> 8, h[19] = w;
    h[20] = ht >> 24, h[21] = ht >> 16, h[22] = ht >> 8, h[23] = ht;
    h[24] = depth, h[25] = colorType, h[26] = 0, h[27] = 0, h[28] = 0;
}

int main()
{
    unsigned char h[TEX_PNG_HEADER_LEN];

    // Allowed: 8-bit palette, up to 512 on each side.
    pngHeader(h, 512, 512, 8, 3);
    CHECK(texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 320, 240, 8, 3);
    CHECK(texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 1, 1, 8, 3);
    CHECK(texPngHeaderIsSmallT8(h, sizeof(h), 512));

    // Too large on either side.
    pngHeader(h, 513, 512, 8, 3);
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 512, 513, 8, 3);
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 640, 480, 8, 3);
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 0x80000000u, 1, 8, 3); // a width that is negative as an int
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 0, 100, 8, 3);
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));

    // Not an 8-bit palette: RGB, RGBA, grey, 4-bit palette.
    pngHeader(h, 256, 256, 8, 2);
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 256, 256, 8, 6);
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 256, 256, 8, 0);
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 256, 256, 4, 3);
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));

    // Not a PNG, a damaged signature, a first chunk that is not IHDR, a short read, no buffer.
    pngHeader(h, 256, 256, 8, 3);
    h[1] = 'J';
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 256, 256, 8, 3);
    h[12] = 'i';
    CHECK(!texPngHeaderIsSmallT8(h, sizeof(h), 512));
    pngHeader(h, 256, 256, 8, 3);
    CHECK(!texPngHeaderIsSmallT8(h, TEX_PNG_HEADER_LEN - 1, 512));
    CHECK(!texPngHeaderIsSmallT8(h, -1, 512)); // read() failed
    CHECK(!texPngHeaderIsSmallT8(NULL, sizeof(h), 512));

    // Old-theme fallback: only the pictures stock OPL never loaded.
    const int newer[] = {MMCE_ICON, UDP_ICON, UDPFS_ICON, FAV_ICON, FAV_MARK, L1_ICON, R1_ICON, L3_ICON, R3_ICON};
    for (int id : newer)
        CHECK(texNewerThanStockOpl(id));
    // Stock OPL's own device pages, glyphs and pictures keep use_default=0's meaning.
    const int stock[] = {BDM_ICON, USB_ICON, ILINK_ICON, MX4SIO_ICON, HDD_BD_ICON, HDD_ICON, ETH_ICON, APP_ICON,
                         INDEX_0, LEFT_ICON, RIGHT_ICON, CROSS_ICON, TRIANGLE_ICON, CIRCLE_ICON, SQUARE_ICON,
                         SELECT_ICON, START_ICON, LOAD0_ICON, LOAD7_ICON, SETTINGS_BG, COVER_DEFAULT, LOGO_PICTURE};
    for (int id : stock)
        CHECK(!texNewerThanStockOpl(id));

    TEST_DONE("wallpaper");
}
