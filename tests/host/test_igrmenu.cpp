// igrmenu: the in-game menu's renderer (ee_core/src/igrmenu.c, IGR_MENU_HOST_TEST) and its cheat list
// (ee_core/include/igrcheats.h): what a page draws, which items it lists, and the code set CHEATS builds. And
// RA/<startup>.ach (src/sources/rahunt.cpp) as LEFT TO HUNT gets it.
// IGRMENU_PPM=<dir> also writes each page as a picture, to look at.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#define IGR_MENU_HOST_TEST
#include "../../ee_core/src/igrmenu.c"
#include "../../src/sources/rahunt.cpp"
#include "check.h"

u64 GSMSourceGSRegs[11]; // what the GS write tracker recorded, as each frame test sets it
u16 GSMSourceSetGsCrt[3];

static u64 dispfb(u32 fbp, u32 fbw, u32 psm) { return (u64)fbp | (u64)fbw << 9 | (u64)psm << 15; }
static u64 display(u32 dx, u32 dy, u32 magh, u32 magv, u32 dw, u32 dh)
{
    return (u64)dx | (u64)dy << 12 | (u64)magh << 23 | (u64)magv << 27 | (u64)dw << 32 | (u64)dh << 44;
}

static void put_ppm(const char *name, const u8 *rgb, int w, int h)
{
    const char *dir = getenv("IGRMENU_PPM");
    char path[512];
    if (dir == NULL)
        return;
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 1, w * h * 3, f);
    fclose(f);
}

// a cheat block like the launcher's: rows with `counts` pairs each, codes numbered so a test can tell them apart
static std::vector<unsigned int> make_block(const std::vector<int> &counts, const std::vector<int> &on)
{
    unsigned int pairs = 0;
    for (int c : counts)
        pairs += c;
    std::vector<unsigned int> mem((IGR_CHEATS_BYTES(counts.size(), pairs) + 3) / 4, 0);
    igr_cheats_t *h = (igr_cheats_t *)mem.data();
    h->magic = IGR_CHEATS_MAGIC;
    h->count = counts.size();
    h->pairs = pairs;
    igr_cheat_t *row = IGR_CHEATS_ROWS(h);
    unsigned int *codes = IGR_CHEATS_CODES(h), p = 0;
    for (size_t i = 0; i < counts.size(); i++) {
        row[i].first = p;
        row[i].count = counts[i];
        row[i].on = on[i];
        char name[64];
        snprintf(name, sizeof(name), "Cheat number %02d with a long name", (int)(i % 100));
        memcpy(row[i].name, name, sizeof(row[i].name) - 1); // cut to the row, as the launcher's snprintf does
        for (int k = 0; k < counts[i]; k++, p++) {
            codes[p * 2] = 0x20000000 | (i << 8) | k; // 32-bit write, row i, code k
            codes[p * 2 + 1] = i;
        }
    }
    return mem;
}

static int count_colour(const u8 *rgb, int n, int colour)
{
    int hits = 0;
    for (int i = 0; i < n; i++)
        if (!memcmp(rgb + i * 3, igrmRgb[colour], 3))
            hits++;
    return hits;
}

int main()
{
    static u8 rgb[PW * 2 * PH * 2 * 3];
    igrm_info_t in;
    memset(&in, 0, sizeof(in));
    in.gameId = "SLUS_209.46";
    in.exitPath = "mass:/APPS/RIPTOPL.ELF";
    in.canPowerOff = 1;

    // ---- items: CHEATS only with a cheat list
    igrmListItems(&in);
    CHECK(igrmItems == 3);
    CHECK(igrmIds[0] == IT_RESUME && igrmIds[1] == IT_EXIT && igrmIds[2] == IT_POWER);
    std::vector<unsigned int> blk = make_block({2, 1, 3, 250, 4, 1, 1, 1, 1, 1, 1, 1}, {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    in.cheats = (igr_cheats_t *)blk.data();
    igrmListItems(&in);
    CHECK(igrmItems == 4);
    CHECK(igrmIds[1] == IT_CHEATS);

    // ---- every page renders at both scales, with the panel's border on its edge and text on it
    for (int sel = 0; sel < 4; sel++)
        for (int armed = 0; armed < 2; armed++) {
            int h = igrmTestPage(&in, 0, sel, armed, 2, 2, rgb);
            CHECK(h == PH * 2);
            CHECK(!memcmp(rgb, igrmRgb[C_BORDER], 3));
            CHECK(count_colour(rgb, PW * 2 * h, C_SEL) > 50); // the selected item, in white
            char name[32];
            snprintf(name, sizeof(name), "menu-%d%s", sel, armed ? "-armed" : "");
            put_ppm(name, rgb, PW * 2, h);
        }
    igrmTestPage(&in, 0, 2, 1, 2, 2, rgb); // EXIT GAME armed: the warning colour
    CHECK(count_colour(rgb, PW * 2 * PH * 2, C_WARN) > 50);
    CHECK(igrmLines <= IGRM_LINES);

    // the CHEATS page: rows ON in gold, scrolled so the cursor stays in view, and the FULL hint
    igrmTestPage(&in, 1, 0, 0, 2, 2, rgb);
    CHECK(count_colour(rgb, PW * 2 * PH * 2, C_GOLD) > 20);
    put_ppm("cheats-top", rgb, PW * 2, PH * 2);
    igrmTestPage(&in, 1, 11, 3, 2, 2, rgb); // the last row, the list scrolled to it
    CHECK(igrmLines <= IGRM_LINES);
    put_ppm("cheats-bottom", rgb, PW * 2, PH * 2);
    igrmTestPage(&in, 1, 3, -1, 1, 1, rgb);
    CHECK(count_colour(rgb, PW * PH, C_WARN) > 10);
    put_ppm("cheats-full", rgb, PW, PH);
    // a 16-bit frame: two bytes a pixel, the alpha bit set
    igrmBpp = 2;
    igrmRenderLine(rgb, 0);
    CHECK((rgb[1] & 0x80) != 0);

    // ---- the code set CHEATS builds: ticked rows whole, in file order
    igr_cheats_t *b = (igr_cheats_t *)blk.data();
    static unsigned int set[IGR_CHEATS_SPARE_WORDS];
    CHECK(igrCheatsBuild(b, set) == 5); // rows 0 (2) and 2 (3)
    CHECK(set[0] == 5);
    CHECK(set[1] == 0x20000000 && set[2] == 0);
    CHECK(set[3] == 0x20000001);
    CHECK(set[5] == 0x20000200 && set[6] == 2);
    // the 250-code row does not fit beside the ticked ones; it fits alone
    CHECK(!igrCheatsFit(b, 3));
    IGR_CHEATS_ROWS(b)
    [0].on = 0;
    IGR_CHEATS_ROWS(b)
    [2].on = 0;
    CHECK(igrCheatsFit(b, 3));
    IGR_CHEATS_ROWS(b)
    [3].on = 1;
    CHECK(igrCheatsBuild(b, set) == 250);
    // a row turned on past the set's room is skipped, not cut
    IGR_CHEATS_ROWS(b)
    [4].on = 1;
    CHECK(igrCheatsBuild(b, set) == 250);
    CHECK(set[1 + 249 * 2] == (0x20000000 | (3 << 8) | 249));
    // the spare set holds a full set
    CHECK(IGR_CHEATS_SPARE_WORDS >= 1 + IGR_CHEAT_SET_CODES * 2);
    CHECK((void *)IGR_CHEATS_ROWS(b) == (void *)((u8 *)b + sizeof(igr_cheats_t) + IGR_CHEATS_SPARE_WORDS * 4));
    CHECK(sizeof(igr_cheat_t) == 32);

    // ---- ACHIEVEMENTS: listed in RA builds only, after CHEATS
    in.raBuilt = 1;
    igrmListItems(&in);
    CHECK(igrmItems == 5);
    CHECK(igrmIds[2] == IT_ACH && igrmIds[3] == IT_EXIT);
    igrmTestPage(&in, 0, 2, 0, 2, 2, rgb); // no list yet
    CHECK(igrmLines <= IGRM_LINES);
    put_ppm("ach-none", rgb, PW * 2, PH * 2);

    // ---- RA/<startup>.ach: written a line per achievement, tabs and breaks in the text as spaces
    static char ach[4096];
    int o = snprintf(ach, sizeof(ach), "RIPPS2 ACH 1\n");
    o += raHuntLine(ach + o, sizeof(ach) - o, 1, 5, "First Steps", "Finish the tutorial");
    o += raHuntLine(ach + o, sizeof(ach) - o, 0, 10, "Caf\xc3\xa9 \xe2\x80\x9cRegular\xe2\x80\x9d",
                    "Visit the caf\xc3\xa9 on every\tday of the week\nwithout missing one, in a single save file of the game");
    o += raHuntLine(ach + o, sizeof(ach) - o, 0, 1, "A title long enough to need two lines here and more", "");
    char tiny[8];
    CHECK(raHuntLine(tiny, sizeof(tiny), 0, 1, "too long", "for this") == 0);
    static ra_hunt_t list[8];
    CHECK(raHuntRead(ach, o, list, 8) == 3);
    CHECK(raHuntRead("NOT AN ACH", 10, list, 8) == -1);
    CHECK(raHuntRead("RIPPS2 ACH 10\n1\t5\tA\tB\n", 22, list, 8) == -1); // the header is the whole first line
    CHECK(raHuntRead("RIPPS2 ACH 1", 12, list, 8) == -1);
    CHECK(list[0].unlocked == 1 && list[1].unlocked == 0 && list[1].points == 10);
    CHECK(strchr(list[1].desc, '\t') == NULL);
    static unsigned char blob[1024];
    int count;
    int bytes = raHuntBlob(list, 3, blob, sizeof(blob), &count);
    CHECK(count == 2 && bytes > 0);
    CHECK(blob[0] == 10 && blob[1] == 1 && blob[2] >= 2); // the cafe one: one title line, its description wrapped
    CHECK(!strcmp((const char *)blob + 3, "CAFE 'REGULAR'"));
    for (const unsigned char *l = blob + 3; l < blob + bytes; l += strlen((const char *)l) + 1)
        CHECK(strlen((const char *)l) <= RA_HUNT_COLS); // header bytes of the next entry read as short lines
    int small;
    CHECK(raHuntBlob(list, 3, blob, 20, &small) <= 20 && small == 0); // no room for the first: none of it

    raHuntBlob(list, 3, blob, sizeof(blob), &count);
    in.raTotal = 3;
    in.raUnlocked = 1;
    in.hunt = blob;
    in.huntCount = count;
    igrmTestPage(&in, 0, 2, 0, 2, 2, rgb);
    CHECK(count_colour(rgb, PW * 2 * PH * 2, C_GOLD) > 20); // "2 LEFT TO HUNT"
    put_ppm("ach-info", rgb, PW * 2, PH * 2);
    static u8 body[PW * 2 * PH * 3]; // the lower half of the last entry's page: each entry shows its own text
    for (int at = 0; at < count; at++) {
        igrmTestPage(&in, 2, at, 0, 2, 2, rgb);
        CHECK(igrmLines <= IGRM_LINES);
        CHECK(count_colour(rgb, PW * 2 * PH * 2, C_GOLD) > 20); // the title
        CHECK(at == 0 || memcmp(body, rgb + sizeof(body), sizeof(body)) != 0);
        memcpy(body, rgb + sizeof(body), sizeof(body));
        char name[32];
        snprintf(name, sizeof(name), "hunt-%d", at);
        put_ppm(name, rgb, PW * 2, PH * 2);
    }
    in.raUnlocked = 3;
    in.huntCount = 0;
    igrmTestPage(&in, 0, 2, 0, 1, 1, rgb); // all of them
    CHECK(count_colour(rgb, PW * PH, C_GOLD) > 10);

    // ---- the frame: nothing recorded (an interrupt-handler flip) opens on a frame of the menu's own
    memset(GSMSourceGSRegs, 0, sizeof(GSMSourceGSRegs));
    memset(GSMSourceSetGsCrt, 0, sizeof(GSMSourceSetGsCrt));
    CHECK(igrmFindFrame() == 2);
    CHECK(igrmBp == 0 && igrmBw == 10 && igrmPsm == 0 && igrmBpp == 4);
    CHECK(igrmSx == 1 && igrmSy == 1); // shown width unknown: the panel fits a 320 wide picture
    CHECK(igrmX == (320 - PW) / 2 && igrmY == (224 - PH) / 2);
    // a DISPLAY the game wrote from its own code still sizes that frame: 512 x 448 shown, the panel at scale 2
    GSMSourceGSRegs[SRC_PMODE] = 1;
    GSMSourceGSRegs[SRC_DISPLAY1] = display(636, 50, 4, 0, 2559, 447);
    CHECK(igrmFindFrame() == 2);
    CHECK(igrmSx == 2 && igrmSy == 2);
    CHECK(igrmX == (512 - PW * 2) / 2 && igrmY == (448 - PH * 2) / 2);
    // a recorded DISPFB is the game's own: used as it is, no frame of our own
    GSMSourceGSRegs[SRC_DISPFB1] = dispfb(70, 8, 2); // 512 wide CT16 at page 70
    CHECK(igrmFindFrame() == 1);
    CHECK(igrmBp == 70 * 32 && igrmBw == 8 && igrmPsm == 2 && igrmBpp == 2);
    // a format the panel cannot draw in (PSMT8) still leaves at once
    GSMSourceGSRegs[SRC_DISPFB1] = dispfb(0, 10, 0x13);
    CHECK(igrmFindFrame() == 0);
    // circuit 1 on top with only circuit 2 recorded: its format is unknown, so it leaves at once
    GSMSourceGSRegs[SRC_DISPFB1] = 0;
    GSMSourceGSRegs[SRC_DISPFB2] = dispfb(0, 10, 0);
    CHECK(igrmFindFrame() == 0);

    TEST_DONE("igrmenu");
}
