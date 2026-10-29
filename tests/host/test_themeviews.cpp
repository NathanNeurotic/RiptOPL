// themeviews: multi-view theme resolution (docs/MULTI-VIEW-THEMES-PLAN.md). Which views a theme offers,
// which one loads, and which block each family slot resolves to in that view.
#include <string>
#include <string.h>
#include "../../src/platform/util.cpp"
#include "../../src/core/config.cpp"
#include "../../src/gfx/themeviews.cpp"
#include "host_env.h"
#include "check.h"

static config_set_t *parse(const char *text)
{
    config_set_t *c = configAlloc(0, NULL, NULL);
    configReadBuffer(c, text, (int)strlen(text));
    return c;
}

static std::string slot(config_set_t *c, const char *prefix, const char *const *chain, int chainLen, int j)
{
    char name[THM_VIEW_BLOCK_NAME_LEN];
    return thmViewResolveSlot(c, prefix, 1, chain, chainLen, j, name, sizeof(name)) ? std::string(name) : std::string("<none>");
}

int main()
{
    static const char *const mainChain[] = {"main"};
    static const char *const appsChain[] = {"appsMain", "main"};
    static const char *const vcdChain[] = {"vcdMain", "appsMain", "main"};
    thm_view_info_t info;

    CHECK(thmViewFromName("Coverflow") == THM_VIEW_COVERFLOW);
    CHECK(thmViewFromName("psbbn") == THM_VIEW_PSBBN);
    CHECK(thmViewFromName("bogus") == -1);
    CHECK(thmViewFromName(NULL) == -1);
    CHECK(thmViewFromLegacyTheme("<Coverflow>") == THM_VIEW_COVERFLOW);
    CHECK(thmViewFromLegacyTheme("<psbbn flow>") == THM_VIEW_PSBBN);
    CHECK(thmViewFromLegacyTheme("<OPL>") == -1);
    CHECK(thmViewFromLegacyTheme(NULL) == -1);

    // ---- a theme without views: List, no prefix, base resolution exactly as before -------------------
    config_set_t *plain = parse("main0:\n\ttype=Background\nmain1:\n\ttype=ItemsList\nappsMain1:\n\ttype=ItemCover\n"
                                "main2:\n\ttype=StaticImage\nmain3_enabled=0\n");
    thmViewScan(plain, &info);
    CHECK(info.native == THM_VIEW_LIST);
    CHECK(info.mask == (1u << THM_VIEW_LIST));
    CHECK(info.blocksMask == 0);
    CHECK(thmViewChoose(&info, THM_VIEW_COVERFLOW) == THM_VIEW_LIST); // not offered -> default
    CHECK(strcmp(thmViewPrefix(&info, THM_VIEW_LIST), "") == 0);
    CHECK(slot(plain, "", mainChain, 1, 1) == "main1");
    CHECK(slot(plain, "", appsChain, 2, 1) == "appsMain1");
    CHECK(slot(plain, "", appsChain, 2, 2) == "main2");
    CHECK(slot(plain, "", mainChain, 1, 3) == "main3"); // switched off is still present
    CHECK(slot(plain, "", mainChain, 1, 4) == "<none>");
    configFree(plain);

    // ---- a legacy single-view flow theme: native view from its element type ---------------------------
    config_set_t *flow = parse("main0:\n\ttype=Background\nmain1:\n\ttype=PSBBNFlow\n");
    thmViewScan(flow, &info);
    CHECK(info.native == THM_VIEW_PSBBN);
    CHECK(info.mask == (1u << THM_VIEW_PSBBN));
    CHECK(info.defaultView == THM_VIEW_PSBBN);
    configFree(flow);
    config_set_t *grid = parse("main0:\n\ttype=Background\nmain1:\n\ttype=Grid\n");
    thmViewScan(grid, &info);
    CHECK(info.native == THM_VIEW_GRID && info.mask == (1u << THM_VIEW_GRID));
    configFree(grid);

    // ---- a multi-view theme ---------------------------------------------------------------------------
    config_set_t *mv = parse("views=list, coverflow,grid\n"
                             "default_view=coverflow\n"
                             "text_color=#101010\n"
                             "coverflow.text_color=#202020\n"
                             "use_default=1\n"
                             "main0:\n\ttype=Background\n"
                             "main1:\n\ttype=ItemsList\n"
                             "main2:\n\ttype=ItemCover\n"
                             "appsMain2:\n\ttype=ItemCover\n"
                             "coverflow.main2:\n\ttype=Coverflow\n"
                             "coverflow.main1_enabled=0\n"
                             "coverflow.main3:\n\ttype=StaticText\n"
                             "psbbn.main2:\n\ttype=PSBBNFlow\n");
    thmViewScan(mv, &info);
    CHECK(info.native == THM_VIEW_LIST);
    CHECK(info.mask == ((1u << THM_VIEW_LIST) | (1u << THM_VIEW_COVERFLOW) | (1u << THM_VIEW_GRID))); // views= wins over psbbn. keys
    CHECK(info.blocksMask == ((1u << THM_VIEW_COVERFLOW) | (1u << THM_VIEW_PSBBN)));
    CHECK(info.defaultView == THM_VIEW_COVERFLOW);
    CHECK(thmViewChoose(&info, -1) == THM_VIEW_COVERFLOW);
    CHECK(thmViewChoose(&info, THM_VIEW_LIST) == THM_VIEW_LIST);
    CHECK(thmViewChoose(&info, THM_VIEW_PSBBN) == THM_VIEW_COVERFLOW); // not in views=
    CHECK(strcmp(thmViewPrefix(&info, THM_VIEW_COVERFLOW), "coverflow") == 0);
    CHECK(strcmp(thmViewPrefix(&info, THM_VIEW_GRID), "") == 0); // offered, but nothing declared for it

    // View blocks replace base blocks; the view prefix applies to the whole chain before the base.
    CHECK(slot(mv, "coverflow", mainChain, 1, 2) == "coverflow.main2");
    CHECK(slot(mv, "coverflow", appsChain, 2, 2) == "coverflow.main2"); // view main beats base appsMain
    CHECK(slot(mv, "", appsChain, 2, 2) == "appsMain2");
    CHECK(slot(mv, "coverflow", vcdChain, 3, 2) == "coverflow.main2");
    CHECK(slot(mv, "coverflow", mainChain, 1, 1) == "coverflow.main1"); // switched off in this view
    CHECK(slot(mv, "coverflow", mainChain, 1, 0) == "main0");           // inherited
    CHECK(slot(mv, "coverflow", mainChain, 1, 3) == "coverflow.main3"); // appended past the base
    CHECK(slot(mv, "", mainChain, 1, 3) == "<none>");
    CHECK(slot(mv, "coverflow", mainChain, 1, 4) == "<none>");

    // View-scoped theme-wide keys.
    unsigned char col[3] = {0, 0, 0};
    CHECK(thmViewGetColor(mv, "coverflow", "text_color", col) && col[0] == 0x20);
    CHECK(thmViewGetColor(mv, "", "text_color", col) && col[0] == 0x10);
    int v = 0;
    CHECK(thmViewGetInt(mv, "coverflow", "use_default", &v) && v == 1);
    configFree(mv);

    // ---- <view>.inherit=0: the view's blocks never fall back to the base ------------------------------
    config_set_t *own = parse("main0:\n\ttype=Background\nmain1:\n\ttype=ItemsList\nmain2:\n\ttype=ItemCover\n"
                              "psbbn.inherit=0\npsbbn.main0:\n\ttype=Background\npsbbn.main1:\n\ttype=PSBBNFlow\n");
    thmViewScan(own, &info);
    CHECK(info.ownMask == (1u << THM_VIEW_PSBBN));
    CHECK(thmViewInheritsBase(&info, THM_VIEW_LIST) == 1);
    CHECK(thmViewInheritsBase(&info, THM_VIEW_PSBBN) == 0);
    char name[THM_VIEW_BLOCK_NAME_LEN];
    CHECK(thmViewResolveSlot(own, "psbbn", 0, mainChain, 1, 1, name, sizeof(name)) && strcmp(name, "psbbn.main1") == 0);
    CHECK(thmViewResolveSlot(own, "psbbn", 0, mainChain, 1, 2, name, sizeof(name)) == 0); // base main2 not inherited
    CHECK(thmViewResolveSlot(own, "psbbn", 1, mainChain, 1, 2, name, sizeof(name)) && strcmp(name, "main2") == 0);
    CHECK(thmViewResolveSlot(own, "", 0, mainChain, 1, 2, name, sizeof(name)) && strcmp(name, "main2") == 0); // no prefix = base
    configFree(own);

    // ---- default_view naming a view the theme does not offer falls back to the native view -------------
    config_set_t *bad = parse("views=coverflow,list\ndefault_view=grid\nmain0:\n\ttype=Coverflow\n");
    thmViewScan(bad, &info);
    CHECK(info.native == THM_VIEW_COVERFLOW);
    CHECK(info.defaultView == THM_VIEW_COVERFLOW);
    configFree(bad);

    // ---- views= with no known name is ignored ------------------------------------------------------
    config_set_t *junk = parse("views=carousel\nmain0:\n\ttype=Background\n");
    thmViewScan(junk, &info);
    CHECK(info.mask == (1u << THM_VIEW_LIST));
    configFree(junk);

    // Key prefixes are matched exactly: blocks are looked up as "<lowercase view>.<key>", so a mixed-case
    // prefix declares no view and its inherit=0 does not strip the base.
    config_set_t *mixed = parse("main0:\n\ttype=Background\nCoverflow.inherit=0\nCoverflow.main1:\n\ttype=Coverflow\n");
    thmViewScan(mixed, &info);
    CHECK(info.blocksMask == 0 && info.ownMask == 0 && info.mask == (1u << THM_VIEW_LIST));
    configFree(mixed);

    // ---- SD/HD layouts: _sd/_hd variants and the uOPL/wOPL _wsX key -----------------------------------
    {
        static const char *layout = "bg_color=#000000\n"
                                    "bg_color_hd=#101010\n"
                                    "main1:\n\ttype=StaticImage\n\tx=100\n\ty=50\n\tx_hd=140\n\ty_hd=60\n"
                                    "main2:\n\ttype=StaticImage\n\tx=200\n\twsX=-150\n"
                                    "main3:\n\ttype=StaticImage\n\tx=300\n\twsX=310\n\tx_hd=320\n"
                                    "main4:\n\ttype=StaticImage\n\tx=400\n\tx_sd=410\n"
                                    "coverflow.main5:\n\ttype=Coverflow\n\ty=240\n\ty_hd=250\n";
        const char *v;
        int i;

        config_set_t *sd = parse(layout);
        CHECK(thmApplyDisplayLayout(sd, 0) == 1);
        CHECK(configGetInt(sd, "main1_x", &i) && i == 100);
        CHECK(configGetInt(sd, "main1_y", &i) && i == 50);
        CHECK(configGetInt(sd, "main2_x", &i) && i == 200); // _wsX is for 16:9 only
        CHECK(configGetInt(sd, "main4_x", &i) && i == 410); // _sd wins in 4:3
        CHECK(configGetStr(sd, "bg_color", &v) && strcmp(v, "#000000") == 0);
        CHECK(configGetInt(sd, "coverflow.main5_y", &i) && i == 240);
        configFree(sd);

        config_set_t *hd = parse(layout);
        CHECK(thmApplyDisplayLayout(hd, 1) == 1);
        CHECK(configGetInt(hd, "main1_x", &i) && i == 140);
        CHECK(configGetInt(hd, "main1_y", &i) && i == 60);
        CHECK(configGetInt(hd, "main2_x", &i) && i == -150); // negative = from the right edge, as for x
        CHECK(configGetInt(hd, "main3_x", &i) && i == 320);  // x_hd beats wsX
        CHECK(configGetInt(hd, "main4_x", &i) && i == 400);  // no _hd: the plain key
        CHECK(configGetStr(hd, "bg_color", &v) && strcmp(v, "#101010") == 0);
        CHECK(configGetInt(hd, "coverflow.main5_y", &i) && i == 250); // works under a view prefix
        CHECK(configGetInt(hd, "main1_x_hd", &i) && i == 140);        // the variant keys stay readable
        configFree(hd);

        // A theme without variants reports none and is left exactly as read.
        config_set_t *none = parse("main0:\n\ttype=Background\nmain1:\n\ttype=StaticImage\n\tx=12\n\tshadow=3\n");
        int before = 0, after = 0;
        for (const config_value_t *it = none->entries(); it; it = it->next)
            before++;
        CHECK(thmApplyDisplayLayout(none, 1) == 0);
        for (const config_value_t *it = none->entries(); it; it = it->next)
            after++;
        CHECK(before == after);
        CHECK(configGetInt(none, "main1_x", &i) && i == 12);
        configFree(none);
        CHECK(thmApplyDisplayLayout(NULL, 1) == 0);

        // A variant whose plain key is missing creates it: an element can exist in one mode only.
        config_set_t *only = parse("main6_type_hd=StaticText\n");
        CHECK(thmApplyDisplayLayout(only, 0) == 1 && !configGetStr(only, "main6_type", &v));
        configFree(only);
        only = parse("main6_type_hd=StaticText\n");
        CHECK(thmApplyDisplayLayout(only, 1) == 1 && configGetStr(only, "main6_type", &v) && strcmp(v, "StaticText") == 0);
        configFree(only);

        // hd_autofit: the 4:3 layout scaled by 3/4 about the centre; centre, edges and gaps keep their shape.
        CHECK(thmLayoutAutoFitX(320, 640) == 320);
        CHECK(thmLayoutAutoFitX(0, 640) == 80);
        CHECK(thmLayoutAutoFitX(640, 640) == 560);
        CHECK(thmLayoutAutoFitX(100, 640) == 155);
    }

    // ---- fallback views: a theme without views= gets every view, built from its own layout ---------------
    {
        config_set_t *lv = parse("views=list,coverflow\nmain0:\n\ttype=Background\n");
        thmViewScan(lv, &info);
        CHECK(info.listed == 1);
        configFree(lv);

        static const char *listTheme = "main0:\n\ttype=Background\n\tdefault=bg\n"
                                       "main1:\n\ttype=MenuIcon\n\tx=40\n"
                                       "main2:\n\ttype=ItemsList\n\tx=20\n\ty=60\n\twidth=380\n\theight=330\n\tfont=1\n"
                                       "main3:\n\ttype=ItemCover\n\tdefault=cover\n\tx=-120\n"
                                       "main4:\n\ttype=StaticText\n\tvalue=Hi\n"
                                       "main5_enabled=0\n"
                                       "appsMain3:\n\ttype=ItemCover\n\tdefault=coverapp\n"
                                       "info0:\n\ttype=Background\n";
        static const char *cfTemplate = "main0:\n\ttype=Background\n"
                                        "main2:\n\ttype=Coverflow\n\tdefault=cover\n\ty=233\n\twidth=184\n"
                                        "appsMain2:\n\ttype=Coverflow\n\tdefault=coverapp\n\ty=239\n"
                                        "vcdMain2:\n\ttype=Coverflow\n\tdefault=cover\n"
                                        "main15:\n\ttype=ItemsList\n\tx=POS_MID\n\ty=45\n\twidth=DIM_INF\n\theight=24\n\taligned=1\n";
        config_set_t *t = parse(listTheme);
        config_set_t *tmpl = parse(cfTemplate);
        thmViewScan(t, &info);
        CHECK(info.listed == 0 && info.mask == (1u << THM_VIEW_LIST));
        CHECK(thmViewSynthesize(t, tmpl, THM_VIEW_COVERFLOW) == 1);
        const char *v;
        int i;
        CHECK(slot(t, "coverflow", mainChain, 1, 0) == "coverflow.main0");
        CHECK(configGetStr(t, "coverflow.main0_default", &v) && strcmp(v, "bg") == 0);     // the theme's own background
        CHECK(configGetStr(t, "coverflow.main3_type", &v) && strcmp(v, "Coverflow") == 0); // cover -> carousel, same slot
        CHECK(configGetInt(t, "coverflow.main3_y", &i) && i == 233);
        CHECK(!configGetStr(t, "coverflow.main3_x", &v));                        // no stale key from the theme's ItemCover
        CHECK(configGetStr(t, "coverflow.main2_y", &v) && strcmp(v, "45") == 0); // list moved to the title strip
        CHECK(configGetStr(t, "coverflow.main2_width", &v) && strcmp(v, "DIM_INF") == 0);
        CHECK(configGetInt(t, "coverflow.main2_font", &i) && i == 1); // keeps its own font
        CHECK(configGetStr(t, "coverflow.main4_value", &v) && strcmp(v, "Hi") == 0);
        CHECK(configGetInt(t, "coverflow.main5_enabled", &i) && i == 0);
        CHECK(configGetStr(t, "coverflow.appsMain3_default", &v) && strcmp(v, "coverapp") == 0); // apps template
        CHECK(configGetInt(t, "coverflow.appsMain3_y", &i) && i == 239);
        CHECK(configGetStr(t, "coverflow.vcdMain3_type", &v) && strcmp(v, "Coverflow") == 0); // PS1 gets its own
        CHECK(!configGetStr(t, "coverflow.info0_type", &v));                                  // info pages untouched
        CHECK(configGetStr(t, "main3_type", &v) && strcmp(v, "ItemCover") == 0);              // base left as is
        configFree(t);

        // A list-only theme without a cover: the presenter goes after its last main block.
        t = parse("main0:\n\ttype=Background\nmain1:\n\ttype=ItemsList\n");
        CHECK(thmViewSynthesize(t, tmpl, THM_VIEW_COVERFLOW) == 1);
        CHECK(configGetStr(t, "coverflow.main2_type", &v) && strcmp(v, "Coverflow") == 0);
        CHECK(slot(t, "coverflow", mainChain, 1, 2) == "coverflow.main2");
        configFree(t);

        // A template without the presenter builds nothing.
        config_set_t *empty = parse("main0:\n\ttype=Background\n");
        t = parse(listTheme);
        CHECK(thmViewSynthesize(t, empty, THM_VIEW_GRID) == 0);
        CHECK(!configGetStr(t, "grid.main0_type", &v));
        configFree(t);
        configFree(empty);
        configFree(tmpl);
    }

    TEST_DONE("themeviews");
}
