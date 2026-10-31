"""Optional CFG Titles in game lists (#918): display only, off by default, read on the I/O thread.

With "Show CFG Titles in Game Lists" on, every list rebuild reads each game's per-game CFG once
(itemGetConfig, the same loader the info page uses) and keeps its UTF-8 Title. Rows then show and
sort by that title; identity (item.text, startup, file name, Favorites, art, rename, launch) is never
touched. Off (the default) reads nothing. src/cfgtitle.c is compiled against stubs.
"""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/ui/cfgtitle.cpp").read_text(encoding="utf-8")
source = re.sub(r"^#include.*\n", "", source, flags=re.M)

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#define MODE_COUNT 8
enum { BDM_MODE, ETH_MODE, HDD_MODE, APP_MODE, FAV_MODE };
#define GAME_FORMAT_FOLDER 9

typedef struct { const char *title; int present; } config_set_t;
typedef struct { int format; } base_game_info_t;
typedef struct item_list item_list_t;
struct item_list {
    int mode;
    char *(*itemGetName)(item_list_t *, int);
    void *(*itemGet)(item_list_t *, int);
    config_set_t *(*itemGetConfig)(item_list_t *, int);
};
static int gCfgTitlesInLists, configReads, configFrees, locks, unlocks, mallocFailAt = -1, mallocs;
static const char *titles[8];
static base_game_info_t games[8];
static const char *names[8] = {"Dragon Quest VIII", "SLPM_655.55", "Game C", "Folder", "Game E", "Game F", "Game G", "Game H"};
static config_set_t configs[8];
static char *getName(item_list_t *l, int i) { (void)l; return const_cast<char *>(names[i]); }
static void *getGame(item_list_t *l, int i) { (void)l; return &games[i]; }
static config_set_t *getConfig(item_list_t *l, int i) { (void)l; configReads++; configs[i].title = titles[i]; return &configs[i]; }
static int configGetStr(config_set_t *c, const char *key, const char **out) { assert(!strcmp(key, "Title")); *out = c->title; return c->title != NULL; }
static void configFree(config_set_t *c) { (void)c; configFrees++; }
static void guiLock(void) { locks++; }
static void guiUnlock(void) { unlocks++; }
static int folderModeSupported(int mode) { return mode == BDM_MODE; }
static int hideVcdPrefix;
static const char *vcdDisplayNameForRow(item_list_t *l, int id, const char *text)
{
    (void)l, (void)id;
    return hideVcdPrefix && !strncmp(text, "SLUS_012.15.", 12) ? text + 12 : text;
}
static int favSource[8];
static int favGetItemSourceMode(int id) { return favSource[id]; }
#ifdef RETROACHIEVEMENTS
static const char *badges[8];
static const char *raBadgeText(item_list_t *l, int id) { (void)l; return badges[id]; }
#endif
static void *testMalloc(size_t n) { if (mallocs++ == mallocFailAt) return NULL; return malloc(n); }
#define malloc testMalloc
'''

header = re.sub(r'^#include.*\n', '', (ROOT / 'include/cfgtitle.h').read_text(encoding='utf-8'), flags=re.M)
harness += r'''
struct FakeSource {
    bool hasGame() const { return true; }
    void *game(item_list_t *l, int i) { return l->itemGet(l, i); }
    char *name(item_list_t *l, int i) { return l->itemGetName(l, i); }
    config_set_t *config(item_list_t *l, int i) { return l->itemGetConfig(l, i); }
};
static FakeSource sourceObject;
static FakeSource *gsOf(item_list_t *) { return &sourceObject; }
'''
harness += header
menusys = (ROOT / "src/ui/menusys.cpp").read_text(encoding="utf-8")
compare_at = menusys.index("static int submenuSortCompare(")
compare = menusys[compare_at:menusys.index("\n}", compare_at) + 2]
source += r'''
struct submenu_list_t { struct {int isFolder, id; char *text; int text_id, lastPlayed;} item; };
enum { AUTOSORT_OFF, AUTOSORT_AZ, AUTOSORT_LAST_PLAYED };
static int gAutosort = AUTOSORT_AZ;
static char *_l(int id) { (void)id; return NULL; }
''' + compare
tests = r'''
int main(void)
{
    item_list_t bdm = {BDM_MODE, getName, getGame, getConfig};
    item_list_t app = {APP_MODE, getName, getGame, getConfig};
    char clean[CFG_TITLE_MAX];

    /* off_reads_nothing: the default costs no CFG reads and shows names unchanged. */
    titles[0] = "ãƒ‰ãƒ©ã‚´ãƒ³ã‚¯ã‚¨ã‚¹ãƒˆVIII";
    cfgTitleRefresh(&bdm, 3);
    assert(configReads == 0 && cfgTitleForRow(&bdm, 0) == NULL);
    assert(!strcmp(cfgTitleDisplayName(&bdm, 0, names[0]), "Dragon Quest VIII"));

    /* on_shows_title: one read per game, freed, title shown; identity untouched. */
    gCfgTitlesInLists = 1;
    titles[1] = "  Kingdom Hearts  ";
    titles[2] = "";
    games[3].format = GAME_FORMAT_FOLDER;
    titles[3] = "Never read";
    cfgTitleRefresh(&bdm, 4);
    assert(configReads == 3 && configFrees == 3); /* the folder row is skipped */
    assert(!strcmp(cfgTitleDisplayName(&bdm, 0, names[0]), "ãƒ‰ãƒ©ã‚´ãƒ³ã‚¯ã‚¨ã‚¹ãƒˆVIII"));
    assert(!strcmp(cfgTitleDisplayName(&bdm, 1, names[1]), "Kingdom Hearts")); /* trimmed */
    assert(!strcmp(cfgTitleDisplayName(&bdm, 2, names[2]), "Game C"));        /* empty: name */
    assert(!strcmp(cfgTitleDisplayName(&bdm, 3, names[3]), "Folder"));
    assert(!strcmp(names[0], "Dragon Quest VIII"));
    assert(locks == unlocks && locks >= 1); /* swapped under the GUI lock */
    assert(cfgTitleForRow(&bdm, 99) == NULL && cfgTitleForRow(&bdm, -1) == NULL);

    /* apps_skipped: Apps are executables, not library games. */
    configReads = 0;
    cfgTitleRefresh(&app, 3);
    assert(configReads == 0 && cfgTitleForRow(&app, 0) == NULL);

    /* clean: invalid UTF-8 and control characters fall back; long titles cut on a character. */
    assert(!cfgTitleClean("\xff\xfe broken", clean, sizeof(clean)));
    assert(!cfgTitleClean("bad\x01ctrl", clean, sizeof(clean)));
    assert(!cfgTitleClean("\xe3\x83", clean, sizeof(clean))); /* truncated sequence */
    assert(!cfgTitleClean("   ", clean, sizeof(clean)));
    assert(cfgTitleClean("ãƒ•ã‚¡ã‚¤ãƒŠãƒ«ãƒ•ã‚¡ãƒ³ã‚¿ã‚¸ãƒ¼ãƒ•ã‚¡ã‚¤ãƒŠãƒ«ãƒ•ã‚¡ãƒ³ã‚¿ã‚¸ãƒ¼ãƒ•ã‚¡ã‚¤ãƒŠãƒ«ãƒ•ã‚¡ãƒ³ã‚¿ã‚¸ãƒ¼", clean, sizeof(clean)));
    assert(strlen(clean) < CFG_TITLE_MAX && (strlen(clean) % 3) == 0); /* whole 3-byte characters */
    assert(cfgTitleClean("Ok", clean, sizeof(clean)) && !strcmp(clean, "Ok"));
    /* strict UTF-8: overlong, surrogate, beyond U+10FFFF and C1 controls fall back. */
    assert(!cfgTitleClean("\xe0\x80\x80", clean, sizeof(clean)));
    assert(!cfgTitleClean("\xed\xa0\x80", clean, sizeof(clean)));
    assert(!cfgTitleClean("\xf4\x90\x80\x80", clean, sizeof(clean)));
    assert(!cfgTitleClean("\xf0\x80\x80\x80", clean, sizeof(clean)));
    assert(!cfgTitleClean("a\xc2\x85" "b", clean, sizeof(clean)));
    assert(cfgTitleClean("Caf\xc3\xa9", clean, sizeof(clean)));

    /* off_is_immediate: switching off shows file names at once, before any rebuild. */
    gCfgTitlesInLists = 0;
    assert(cfgTitleForRow(&bdm, 0) == NULL);
    assert(!strcmp(cfgTitleDisplayName(&bdm, 0, names[0]), "Dragon Quest VIII"));
    gCfgTitlesInLists = 1;
    assert(cfgTitleForRow(&bdm, 0) != NULL);

    /* forget: a rebuild drops the old table before new rows appear (no stale-index titles). */
    cfgTitleForget(&bdm);
    assert(cfgTitleForRow(&bdm, 0) == NULL);
    assert(cfgTitleForRow(NULL, 0) == NULL);

    /* turned_off_clears: switching off drops the titles on the next rebuild. */
    gCfgTitlesInLists = 0;
    cfgTitleRefresh(&bdm, 4);
    assert(cfgTitleForRow(&bdm, 0) == NULL);

    /* fav_apps_skipped: a Favorite whose source is the Apps tab is an app, never titled. */
    {
        item_list_t fav = {FAV_MODE, getName, getGame, getConfig};
        gCfgTitlesInLists = 1;
        favSource[0] = APP_MODE;
        favSource[1] = BDM_MODE;
        configReads = 0;
        cfgTitleRefresh(&fav, 2);
        assert(configReads == 1 && cfgTitleForRow(&fav, 0) == NULL && cfgTitleForRow(&fav, 1) != NULL);
        gCfgTitlesInLists = 0;
        cfgTitleRefresh(&fav, 2);
    }

#ifdef RETROACHIEVEMENTS
    /* ra_badge_prefix: a tracked game keeps its badge in front of the title. */
    gCfgTitlesInLists = 1;
    badges[0] = "RA Dragon Quest VIII";
    badges[1] = "RA something else";
    titles[1] = "Kingdom Hearts";
    cfgTitleRefresh(&bdm, 2);
    assert(!strcmp(cfgTitleForRow(&bdm, 0), "RA ãƒ‰ãƒ©ã‚´ãƒ³ã‚¯ã‚¨ã‚¹ãƒˆVIII"));
    assert(!strcmp(cfgTitleForRow(&bdm, 1), "Kingdom Hearts")); /* badge does not end with the name */
    assert(!strcmp(cfgTitleSortName(&bdm, 0, names[0]), titles[0])); /* sorts without the mark */
    /* ra_badge_suffix: the mark stays after the title, and sorting stays clean. */
    badges[0] = "Dragon Quest VIII [RA]";
    badges[1] = "SLPM_655.55 [RA]";
    cfgTitleRefresh(&bdm, 2);
    assert(!strcmp(cfgTitleForRow(&bdm, 0), (std::string(titles[0]) + " [RA]").c_str()));
    assert(!strcmp(cfgTitleForRow(&bdm, 1), "Kingdom Hearts [RA]"));
    assert(!strcmp(cfgTitleSortName(&bdm, 1, names[1]), "Kingdom Hearts"));
    /* A file literally named "[RA]" is not mistaken for the mark. */
    const char *savedName = names[0];
    names[0] = "[RA]";
    badges[0] = "[RA] [RA]";
    cfgTitleRefresh(&bdm, 1);
    assert(!strcmp(cfgTitleForRow(&bdm, 0), (std::string(titles[0]) + " [RA]").c_str()));
    /* PS1 rows match the badge against the display name (game id hidden). */
    names[0] = "SLUS_012.15.Dragon Quest VIII";
    hideVcdPrefix = 1;
    badges[0] = "RA Dragon Quest VIII";
    cfgTitleRefresh(&bdm, 1);
    assert(!strncmp(cfgTitleForRow(&bdm, 0), "RA ", 3));
    badges[0] = "Dragon Quest VIII [RA]";
    cfgTitleRefresh(&bdm, 1);
    assert(strstr(cfgTitleForRow(&bdm, 0), " [RA]") != NULL);
    hideVcdPrefix = 0;
    names[0] = savedName;
    badges[0] = badges[1] = NULL;
    gCfgTitlesInLists = 0;
    cfgTitleRefresh(&bdm, 2);
#endif

    /* alloc_failure: no table means names, never a crash. */
    gCfgTitlesInLists = 1;
    mallocs = 0;
    mallocFailAt = 0;
    cfgTitleRefresh(&bdm, 4);
    assert(cfgTitleForRow(&bdm, 0) == NULL);
    assert(!strcmp(cfgTitleDisplayName(&bdm, 0, names[0]), "Dragon Quest VIII"));
    mallocFailAt = -1;

    // Each C++ cache owns its own table; displayName must not reach the global cache.
    gCfgTitlesInLists = 1;
    CfgTitles isolated;
    isolated.refresh(&bdm, 1);
    assert(isolated.forRow(&bdm, 0) != NULL && cfgTitleForRow(&bdm, 0) == NULL);
    assert(!strcmp(isolated.displayName(&bdm, 0, names[0]), isolated.forRow(&bdm, 0)));
    isolated.forget(&bdm);
    assert(isolated.forRow(&bdm, 0) == NULL);
    mallocs = 0; mallocFailAt = 1;
    cfgTitleRefresh(&bdm, 2);
    assert(cfgTitleForRow(&bdm, 0) == NULL && cfgTitleForRow(&bdm, 1) != NULL);
    mallocFailAt = -1;
    cfgTitleForget(&bdm);
    // Execute the actual menu comparator against the real cache, not a copied sort key.
    char rawA[] = "A filename", rawZ[] = "Z filename";
    submenu_list_t a{{0,0,rawA,-1}}, b{{0,1,rawZ,-1}};
    titles[0] = "Zulu Override"; titles[1] = "Alpha Override";
    cfgTitleRefresh(&bdm, 2);
    assert(submenuSortCompare(&a, &b, &bdm) > 0);
    assert(!strcmp(rawA,"A filename") && !strcmp(rawZ,"Z filename"));
    a.item.isFolder = 1;
    assert(submenuSortCompare(&a, &b, &bdm) < 0);
    a.item.isFolder = 0; gCfgTitlesInLists = 0;
    assert(submenuSortCompare(&a, &b, &bdm) < 0);
    cfgTitleForget(&bdm);
    assert(locks == unlocks);
    puts("ok");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "titles.cpp"
    src.write_text(harness + source + tests, encoding="utf-8")
    for flags in ([], ["-DRETROACHIEVEMENTS"]):  # standard and RA builds (badge prefix)
        exe = Path(tmp) / ("titles%s.exe" % ("_ra" if flags else ""))
        subprocess.run(["c++", "-std=c++17", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie", "-Wall", "-Werror", "-Wno-unused-function", *flags, str(src), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

# Wiring: refresh after each scan; rows and sorting draw through the display name; off by default.
opl = (ROOT / "src/app/opl.cpp").read_text(encoding="utf-8")
update = opl[opl.index("static void updateMenuFromGameList("):]
update = update[:update.index("\n}\n")]
# Rows appear with file names at once; the CFG pass runs after them and the sort follows it.
assert update.index("cfgTitleForget(mdl->support);") < update.index("gsOf(mdl->support)->update(mdl->support)") < update.index("guiDeferUpdate(gup);")
assert update.index("guiDeferUpdate(gup);") < update.index("cfgTitleRefresh(mdl->support, count);") < update.index("GUI_OP_SORT")
assert update.index("libViewFinishPending(") < update.index("cfgTitleRefresh(mdl->support, count);")
assert "gup->submenu.text = mdl->support->mode == FAV_MODE ? const_cast<char *>(favGetItemDisplay(i)) : gsOf(mdl->support)->name(mdl->support, i);" in update  # identity unchanged
menusys = (ROOT / "src/ui/menusys.cpp").read_text(encoding="utf-8")
sort = menusys[menusys.index("static int submenuSortCompare("):]
sort = sort[:sort.index("\n}\n")]
assert "cfgTitleSortName(support, a->item.id, txt1)" in sort and "vcdDisplayNameForRow" not in sort
themes = (ROOT / "src/gfx/themeelems.cpp").read_text(encoding="utf-8")
assert "host::library().listDisplayName(list, ps->item.id, submenuItemGetText(&ps->item))" in themes
assert 'include/cfgtitle.h' not in themes
host = (ROOT / "src/app/host.cpp").read_text(encoding="utf-8")
assert "return cfgTitleDisplayName(itemList, id, text);" in host
assert menusys.count("cfgTitleDisplayName(") >= 2  # both game-menu headers
settings = (ROOT / "src/core/settings.cpp").read_text(encoding="utf-8")
assert "intSetting(CONFIG_OPL_CFG_TITLES, &gCfgTitlesInLists, 0)" in settings
dialogs = (ROOT / "src/ui/dialogs.cpp").read_text(encoding="utf-8")
assert "UICFG_CFG_TITLES, 1, 1, _STR_HINT_CFG_TITLES" in dialogs
gui = (ROOT / "src/ui/guisettings.cpp").read_text(encoding="utf-8")
assert gui.count("diaGetInt(ui, UICFG_CFG_TITLES") == 1
# A flip must rebuild every game list: mark the pages dirty (each NeedsUpdate consumes it) and
# queue HTTP too, which oplQueueLibraryDeviceUpdates leaves out.
refresh = gui[gui.index("static void guiRefreshCfgTitles(void)\n{"):]
refresh = refresh[:refresh.index("\n}\n")]
assert "libViewMarkAllDirty();" in refresh and "oplQueueLibraryDeviceUpdates();" in refresh and "httpGetObject(0)" in refresh
assert gui.count("guiRefreshCfgTitles();") == 5  # the CFG-title flip, the RA badge (#1039) flip, the All Games flip, 2 Show Hidden Games flips
base = (ROOT / "lng/tmpl/_base.yml").read_text(encoding="utf-8")
assert base.index("- label: CFG_TITLES") > base.index("- label: AUTO_VMC_TIMEOUT")
print("PASS: CFG titles in game lists are opt-in, display-only and read once per rebuild")

# Execute the settings refresh helper: dirty flags alone do not enqueue HDD or
# HTTP, and Favorites must resolve after every enabled source has been queued.
helper = gui[gui.index("static void guiRefreshCfgTitles(void)\n{"):]
helper = helper[:helper.index("\n}") + 2]
with tempfile.TemporaryDirectory() as tmp:
    cpp, exe = Path(tmp) / "refresh.cpp", Path(tmp) / "refresh"
    cpp.write_text(r'''
#include <cassert>
#include <vector>
#include <cstddef>
struct item_list_t {int enabled,mode;};
static item_list_t http{1,14};
static bool present=true;
static std::vector<int> events;
enum {IO_MENU_UPDATE_DEFFERED=1};
static void libViewMarkAllDirty(){events.push_back(100);}
static void oplQueueLibraryDeviceUpdates(){events.push_back(101);}
static item_list_t *httpGetObject(int){return present?&http:nullptr;}
static void ioPutRequest(int type,int *mode){assert(type==1);events.push_back(*mode);}
static void loadFavourites(){events.push_back(102);}
''' + helper + r'''
int main(){
 guiRefreshCfgTitles(); assert((events==std::vector<int>{100,101,14,102}));
 events.clear();http.enabled=0;
 guiRefreshCfgTitles(); assert((events==std::vector<int>{100,101,102}));
 events.clear();present=false;
 guiRefreshCfgTitles(); assert((events==std::vector<int>{100,101,102}));
}
''', encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# Source rebuilds precede Favorites resolution even for HTTP, and toggling does
# not depend on automatic refresh or the PS1-capable-only device queue.
assert refresh.index('libViewMarkAllDirty();') < refresh.index('oplQueueLibraryDeviceUpdates();')
assert refresh.index('ioPutRequest(IO_MENU_UPDATE_DEFFERED, &http->mode);') < refresh.index('loadFavourites();')
assert gui.count('diaSetInt(ui, UICFG_CFG_TITLES') == 1
