"""Hide Game and Delete everything, ported from RIPPS2 (akilluminati47/RIPPS2, build 114), group D.

1. hidden.txt: one list per device in its prefix, "0|1<tab>name"; bare names are PS2 games and '#'
   lines are comments. Hiding and showing again round-trip through the file, an empty list removes
   it, and a list that cannot be written changes nothing in memory.
2. Rows (menusys.cpp): a device row, a Favourites/All Games row (by its source), a folder row and an
   app.
3. Delete everything: the CFG, art (any letter case, PNG/JPG/JPEG, OPL Manager names) and cheats of
   the game go; another game's files and VMC saves stay; a second copy on the page keeps them all.
4. Wiring: hidden rows leave the page build and the All Games gather; the delete asks twice; a
   changed list rebuilds its page and Favourites.
"""
import re
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
header = (root / 'include/gamemanage.h').read_text()
header = header.replace('#include "include/iosupport.h"\n', '')
source = (root / 'src/sources/gamemanage.cpp').read_text()
body = source[source.index('int gShowHidden;'):]
menu = (root / 'src/ui/menusys.cpp').read_text()
rows = menu[menu.index('static int hiddenRowSource('):menu.index('// Rebuilds the pages a hidden game could be on')]

program = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
enum { HDD_MODE = 0, ETH_MODE, APP_MODE, FAV_MODE, HTTP_MODE, MMCE_MODE, MODE_COUNT };
enum { LIB_VIEW_ISO = 0, LIB_VIEW_PS1, LIB_VIEW_ELF, LIB_VIEW_PS1_ELF };
enum { FAV_KIND_ISO = 0, FAV_KIND_VCD, FAV_KIND_CUE, FAV_KIND_ELF };
enum { GAME_FORMAT_ISO = 1, GAME_FORMAT_FOLDER = 3 };
struct ee_sema_t { int init_count, max_count, option, attr; };
static int CreateSema(ee_sema_t *s) { (void)s; return 1; }
static int semaDepth;
namespace raii {
struct SemaLock {
    explicit SemaLock(int id) { assert(id >= 0 && semaDepth++ == 0); }
    ~SemaLock() { assert(--semaDepth == 0); }
};
}
struct base_game_info_t { char name[64]; char startup[16]; int format, parts; };
struct GameSource;
struct item_list_t { short mode; int enabled; GameSource *source; };
static char prefixBuf[64] = "dev/";
static base_game_info_t games[3];
static const char *names[3] = {"Alpha", "Beta", "Gamma"};
static int pageView, rowViews = 1, deletable = 1;
struct GameSource {
    bool hasPrefix() const { return true; }
    bool hasDelete() const { return deletable != 0; }
    bool hasGame() const { return true; }
    bool hasRowView() const { return rowViews != 0; }
    const char *prefix(item_list_t *l) { return l->mode == APP_MODE ? "apps/" : prefixBuf; }
    void *game(item_list_t *, int id) { return &games[id]; }
    int count(item_list_t *) { return 3; }
    const char *startup(item_list_t *, int id) { return games[id].startup; }
    const char *name(item_list_t *, int id) { return names[id]; }
    int rowView(item_list_t *, int) { return pageView; }
};
static GameSource gs;
static GameSource *gsOf(item_list_t *l) { return l->source; }
struct fav_rec_t { item_list_t *owner; int kind; char *text; };
static fav_rec_t favRecs[3];
struct FavStore { const fav_rec_t *entry(int id) const { return id >= 0 && id < 3 ? &favRecs[id] : NULL; } } gFavSupport;
static int libViewActive(int mode) { (void)mode; return pageView; }
static int folderModeSupported(int mode) { return mode == MMCE_MODE; }
''' + header + body + rows + r'''
static void put(const char *path) { FILE *f = fopen(path, "w"); assert(f); fputs("x", f); fclose(f); }
static int exists(const char *path) { return access(path, F_OK) == 0; }
static char *slurp(const char *path) { static char buf[512]; FILE *f = fopen(path, "r"); size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0; if (f) fclose(f); buf[n] = 0; return buf; }
static int planned(const gm_delete_plan_t *p, const char *path) { for (int i = 0; i < p->count; i++) if (!strcmp(p->files[i].path, path)) return 1; return 0; }

int main(void)
{
    item_list_t dev = {MMCE_MODE, 1, &gs};
    item_list_t net = {HTTP_MODE, 1, &gs};
    item_list_t fav = {FAV_MODE, 1, &gs};
    item_list_t app = {APP_MODE, 1, &gs};
    hiddenInit();
    mkdir("dev", 0777);

    /* 1. the file */
    FILE *f = fopen("dev/hidden.txt", "w");
    fputs("# kept by hand\n0\tAlpha\r\n1\tBeta\nGamma\n", f);
    fclose(f);
    hiddenReload(&dev);
    assert(hiddenHas(&dev, 0, "Alpha") && hiddenHas(&dev, 1, "Beta") && hiddenHas(&dev, 0, "Gamma"));
    assert(!hiddenHas(&dev, 1, "Alpha") && !hiddenHas(&dev, 0, "Beta")); /* PS2 and PS1 are separate */
    assert(hiddenHas(&net, 0, "Alpha") == 0 && hiddenSet(&net, 0, "Alpha", 1) == -1); /* no list there */
    assert(hiddenSet(&fav, 0, "Alpha", 1) == -1 && hiddenSet(&app, 0, "Alpha", 1) == -1);
    assert(hiddenSet(&dev, 0, "Gamma", 0) == 1 && !hiddenHas(&dev, 0, "Gamma"));
    assert(!strcmp(slurp("dev/hidden.txt"), "0\tAlpha\n1\tBeta\n"));
    assert(hiddenSet(&dev, 0, "Delta", 1) == 1 && !strcmp(slurp("dev/hidden.txt"), "0\tAlpha\n1\tBeta\n0\tDelta\n"));
    assert(hiddenSet(&dev, 0, "Delta", 1) == 1 && !strcmp(slurp("dev/hidden.txt"), "0\tAlpha\n1\tBeta\n0\tDelta\n"));
    assert(hiddenSet(&dev, 0, "Alpha", 0) == 1 && hiddenSet(&dev, 1, "Beta", 0) == 1 && hiddenSet(&dev, 0, "Delta", 0) == 1);
    assert(!exists("dev/hidden.txt")); /* an empty list leaves no file behind */
    hiddenReload(&dev);
    assert(!hiddenHas(&dev, 0, "Alpha"));
    snprintf(prefixBuf, sizeof(prefixBuf), "missing/");              /* a list that cannot be written */
    assert(hiddenSet(&dev, 0, "Alpha", 1) == 0 && !hiddenHas(&dev, 0, "Alpha"));
    snprintf(prefixBuf, sizeof(prefixBuf), "dev/");
    assert(hiddenSet(&dev, 0, "Alpha", 1) == 1);

    /* 2. rows */
    item_list_t *src;
    int ps1;
    const char *name;
    games[0].format = games[1].format = games[2].format = GAME_FORMAT_ISO;
    pageView = LIB_VIEW_ISO;
    assert(hiddenRow(&dev, 0) && !hiddenRow(&dev, 1));
    pageView = LIB_VIEW_PS1;
    assert(!hiddenRow(&dev, 0)); /* the same name in the PS1 view is another game */
    rowViews = 0;                /* no per-row view: the page's view decides */
    assert(!hiddenRow(&dev, 0));
    pageView = LIB_VIEW_ISO;
    assert(hiddenRow(&dev, 0));
    rowViews = 1;
    pageView = LIB_VIEW_PS1_ELF;
    assert(!hiddenRowSource(&dev, 0, &src, &ps1, &name)); /* an app row */
    pageView = LIB_VIEW_ISO;
    games[0].format = GAME_FORMAT_FOLDER;
    assert(!hiddenRowSource(&dev, 0, &src, &ps1, &name)); /* a folder row is never hidden */
    games[0].format = GAME_FORMAT_ISO;
    favRecs[0] = {&dev, FAV_KIND_ISO, (char *)"Alpha"};
    favRecs[1] = {&dev, FAV_KIND_CUE, (char *)"Alpha"};
    favRecs[2] = {&app, FAV_KIND_ELF, (char *)"uLE"};
    assert(hiddenRowSource(&fav, 0, &src, &ps1, &name) && src == &dev && ps1 == 0 && hiddenRow(&fav, 0));
    assert(hiddenRowSource(&fav, 1, &src, &ps1, &name) && ps1 == 1 && !hiddenRow(&fav, 1));
    assert(!hiddenRowSource(&fav, 2, &src, &ps1, &name) && !hiddenRowSource(&app, 0, &src, &ps1, &name));
    assert(!hiddenRowSource(&net, 0, &src, &ps1, &name) && !hiddenRowSource(&fav, 3, &src, &ps1, &name));

    /* 3. delete everything */
    strcpy(games[0].startup, "SLUS_123.45"), games[0].parts = 2;
    strcpy(games[1].startup, "SLES_555.55"), games[1].parts = 1;
    strcpy(games[2].startup, "SCUS_999.99"), games[2].parts = 1;
    mkdir("dev/CFG", 0777), mkdir("dev/ART", 0777), mkdir("dev/CHT", 0777), mkdir("dev/VMC", 0777);
    put("dev/CFG/SLUS_123.45.cfg"), put("dev/CFG/SLES_555.55.cfg");
    put("dev/ART/SLUS_123.45_COV.png"), put("dev/ART/slus_123.45_bg_00.JPG"), put("dev/ART/SLUS_123.45_SCR.jpeg");
    put("dev/ART/SLUS_123.45_COV.txt"), put("dev/ART/SLUS_123.456_COV.png"), put("dev/ART/SLES_555.55_COV.png");
    put("dev/CHT/SLUS_123.45.CHT"), put("dev/VMC/SLUS_123.45_0.bin");
    gm_delete_plan_t plan;
    assert(gmPlanDelete(&dev, 0, &plan) && plan.parts == 2 && !plan.shared);
    assert(plan.cfg == 1 && plan.art == 3 && plan.cht == 1 && plan.count == 5);
    assert(planned(&plan, "dev/CFG/SLUS_123.45.cfg") && planned(&plan, "dev/ART/slus_123.45_bg_00.JPG") && planned(&plan, "dev/CHT/SLUS_123.45.CHT"));
    assert(!planned(&plan, "dev/ART/SLUS_123.45_COV.txt") && !planned(&plan, "dev/ART/SLUS_123.456_COV.png"));
    assert(gmDeleteExtras(&plan) == 0 && gmDeleteExtras(&plan) == 5); /* already gone: each one counts */
    assert(!exists("dev/CFG/SLUS_123.45.cfg") && !exists("dev/ART/SLUS_123.45_COV.png") && !exists("dev/CHT/SLUS_123.45.CHT"));
    assert(exists("dev/VMC/SLUS_123.45_0.bin") && exists("dev/CFG/SLES_555.55.cfg") && exists("dev/ART/SLES_555.55_COV.png"));
    strcpy(games[2].startup, "SLES_555.55");                       /* a second copy of Beta */
    assert(gmPlanDelete(&dev, 1, &plan) && plan.shared && plan.count == 0);
    games[1].format = GAME_FORMAT_FOLDER;
    assert(!gmPlanDelete(&dev, 1, &plan));
    assert(!gmPlanDelete(&fav, 2, &plan));                            /* a Favourites row: its device deletes */
    deletable = 0;
    assert(!gmPlanDelete(&dev, 2, &plan));                            /* the source has no delete */
    deletable = 1;
    dev.mode = HDD_MODE;
    assert(!gmPlanDelete(&dev, 2, &plan));                            /* no partition delete */
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ripps2-manage-') as temp:
    src = Path(temp) / 'manage.cpp'
    src.write_text(program)
    exe = Path(temp) / 'manage.exe'
    subprocess.run(['g++', '-std=gnu++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=temp, check=True)
print('ok   hidden.txt: per device, PS2/PS1 apart, round trip, empty list removed, failed write changes nothing')
print('ok   rows: device, Favourites/All Games by source, folders and apps never hidden')
print('ok   delete everything: CFG, art (any case) and cheats go; saves, other games and shared copies stay')

opl = (root / 'src/app/opl.cpp').read_text()
build = opl[opl.index('static void updateMenuFromGameList('):]
build = build[:build.index('\n}\n')]
assert build.index('->update(mdl->support);') < build.index('hiddenReload(mdl->support);')
assert 'if (!isFolderRow && !gShowHidden && hiddenRow(mdl->support, i))\n                    continue;' in build
assert build.index('hiddenRow(mdl->support, i)') < build.index('gup = guiOpCreate(GUI_OP_APPEND_MENU);')
assert opl.count('ioInit();\n    hiddenInit();') == 2, 'both boots create the hidden-list lock'
fav = (root / 'src/sources/favsupport.cpp').read_text()
gather = fav[fav.index('static int favGatherAdd('):]
gather = gather[:gather.index('\n}\n')]
assert gather.index('hiddenHas(owner,') < gather.index('if (out == NULL)'), 'a hidden game must not be counted either'
everything = menu[menu.index('static int menuDeleteEverything('):]
everything = everything[:everything.index('\n}\n')]
assert everything.count('guiMsgBox(msg, 1, NULL) != 1') == 2, 'the delete asks twice'
assert everything.index('_STR_DELETE_GAME_LAST_CHECK') < everything.index('gs->deleteGame(support, id);') < everything.index('gmDeleteExtras(plan)')
assert 'for (part = 0; part < game.parts && !partLeft; part++)' in everything and everything.index('if (partLeft) {') < everything.index('gmDeleteExtras(plan)'), 'every part is gone before the extras go'
assert 'gEnableWrite' in menu[menu.index('static int menuHiddenState('):menu.index('static void menuHideGame(')]
hide = menu[menu.index('static void menuHideGame('):menu.index('static int menuDeleteEverything(')]
assert hide.index('hiddenSet(source, ps1, name, !hidden)') < hide.index('hiddenRefresh(source);'), 'a saved change rebuilds the pages'
refresh = menu[menu.index('static void hiddenRefresh('):menu.index('static int menuHiddenState(')]
assert 'libViewMarkDirty(source->mode);' in refresh and 'loadFavourites();' in refresh
deleteGame = menu[menu.index('static void menuDeleteGame('):]
deleteGame = deleteGame[:deleteGame.index('\n}\n')]
assert deleteGame.index('menuDeleteEverything(submenu, &row)') < deleteGame.index('_STR_DELETE_WARNING')
def last_gate(body, call):
    # the last `if (...)` whose condition makes the call: the write must stay behind it
    gates = [m.start() for m in re.finditer(r'\bif\s*\([^\n]*' + re.escape(call), body)]
    assert gates, call + ' must gate the write, not just run before it'
    return gates[-1]

# The list can be rebuilt while a dialog is up: each write checks the row is still the same game first.
assert deleteGame.index('_STR_DELETE_WARNING') < last_gate(deleteGame, 'menuRowBeginEdit(&row)') < deleteGame.index('deleteGame(support, row.id)')
assert everything.index('_STR_DELETE_GAME_LAST_CHECK') < last_gate(everything, 'menuRowBeginEdit(row)') < everything.index('gs->deleteGame(support, id);')
rename = menu[menu.index('static void menuRenameGame('):]
rename = rename[:rename.index('\n}\n')]
assert rename.index('guiShowKeyboard(') < last_gate(rename, 'menuRowBeginEdit(&row)') < rename.index('->rename(support, row.id, newName)')
assert 'selected_item' not in rename + deleteGame + everything + hide, 'the actions work from their copy of the row'
print('ok   wiring: hidden rows leave the page and the All Games gather; delete asks twice, parts first')
