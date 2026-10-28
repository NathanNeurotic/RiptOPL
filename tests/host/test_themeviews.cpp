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

    TEST_DONE("themeviews");
}
