"""Library features ported from RIPPS2 (akilluminati47/RIPPS2, build 115), group C1.

1. Recents: recentRecord keeps recents.bin newest first, at most RECENT_MAX_ITEMS, one row per game
   (BDM slots count as one device, apps match by title), and never replaces a file it cannot read.
2. Favourites L3 ring: PS2 -> PS1 -> ELF -> All -> Recent -> PS2.
3. Random Game: lands only on games (never a folder row), reaches every game, and reports an empty page.
4. A launch records the game only when something reads recents.bin.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


fav = (root / 'src/sources/favsupport.cpp').read_text()
recents = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#define FAV_TEXT_MAX 256
#define RECENT_MAX_ITEMS 10
enum { APP_MODE = 20, BDM_MODE = 0, BDM_MODE_LAST = 7, HDD = 10 };
struct fav_raw_t { int mode, id, icon_id, text_id, kind; char text[FAV_TEXT_MAX]; };
static const char kRecentFile[] = "recents.bin";
static std::vector<fav_raw_t> disk;
static int editableFile = 1, writes, invalidations;
static fav_raw_t *favReadFile(int *count, int *editable, const char *name)
{
    assert(!strcmp(name, kRecentFile));
    *count = (int)disk.size();
    if (editable) *editable = editableFile;
    if (disk.empty()) return NULL;
    fav_raw_t *p = (fav_raw_t *)calloc(disk.size(), sizeof(fav_raw_t));
    memcpy(p, disk.data(), disk.size() * sizeof(fav_raw_t));
    return p;
}
static int replaceable;
static bool recentReplaceable(void) { return replaceable; }
static int recentWriteFile(fav_raw_t *recs, int count)
{
    assert(count <= RECENT_MAX_ITEMS);
    writes++;
    disk.assign(recs, recs + count);
    return 1;
}
static void recentInvalidate(void) { invalidations++; }
''' + function(fav, 'static int favModesMatch(') + \
    function(fav, 'int recentRecord(') + r'''
static std::string order(void) { std::string s; for (auto &r : disk) { s += r.text; s += ' '; } return s; }
int main(void)
{
    assert(recentRecord(HDD, 3, 0, 0, "A", 0) && recentRecord(BDM_MODE + 1, 4, 0, 0, "B", 0) && recentRecord(HDD, 5, 0, 0, "C", 0));
    assert(order() == "C B A " && invalidations == 3);
    assert(recentRecord(HDD, 8, 0, 0, "A", 0) && order() == "A C B ");        /* replayed (id moved): to the front */
    assert(recentRecord(HDD, 3, 0, 0, "A", 1) && order() == "A A C B ");      /* PS1 kind is another game */
    assert(recentRecord(BDM_MODE + 2, 9, 0, 0, "B", 0) && order() == "B A A C "); /* BDM slots: one device */
    assert(recentRecord(HDD + 1, 9, 0, 0, "B", 0) && order() == "B B A A C "); /* another device: its own row */
    disk.erase(disk.begin());
    assert(disk[0].mode == BDM_MODE + 2 && disk[0].id == 9);                 /* the newest launch is kept */
    assert(recentRecord(APP_MODE, 1, 0, 0, "App", 3) && recentRecord(APP_MODE, 7, 0, 0, "App", 3));
    assert(order() == "App B A A C ");                                         /* apps match by title */
    char name[8];
    for (int i = 0; i < 12; i++) { snprintf(name, sizeof(name), "G%d", i); assert(recentRecord(HDD, i, 0, 0, name, 0)); }
    assert(disk.size() == RECENT_MAX_ITEMS && !strcmp(disk[0].text, "G11") && !strcmp(disk[9].text, "G2"));
    int before = writes;
    assert(!recentRecord(1, 1, 0, 0, "", 0) && writes == before);               /* nothing to record */
    editableFile = 0;
    assert(!recentRecord(1, 1, 0, 0, "X", 0) && writes == before);              /* unreadable file is kept */
    replaceable = 1;                                                           /* a write cut short: replaced */
    assert(recentRecord(1, 1, 0, 0, "X", 0) && writes == before + 1 && !strcmp(disk[0].text, "X"));
    return 0;
}
'''

menusys = (root / 'src/ui/menusys.cpp').read_text()
random_game = r'''
#include <assert.h>
#include <stdlib.h>
#include <time.h>
struct submenu_item { int isFolder; int id; };
struct submenu_list_t { submenu_item item; submenu_list_t *prev, *next; };
struct menu_item_t { int visible; submenu_list_t *submenu, *current, *pagestart; };
struct menu_list_t { menu_item_t *item; };
static menu_list_t *selected_item;
''' + function(menusys, 'static int menuRandomGame(void)') + r'''
int main(void)
{
    submenu_list_t rows[8] = {};
    for (int i = 0; i < 8; i++) {
        rows[i].item.id = i;
        rows[i].item.isFolder = i < 2; /* two folder rows first, as folder browsing emits them */
        rows[i].prev = i ? &rows[i - 1] : NULL;
        rows[i].next = i < 7 ? &rows[i + 1] : NULL;
    }
    menu_item_t page = {1, &rows[0], &rows[0], &rows[0]};
    menu_list_t list = {&page};
    assert(!menuRandomGame());             /* no page yet */
    selected_item = &list;
    int seen[8] = {0};
    for (int i = 0; i < 600; i++) {
        assert(menuRandomGame() && page.current && page.current == page.pagestart && !page.current->item.isFolder);
        seen[page.current->item.id]++;
    }
    for (int i = 2; i < 8; i++)
        assert(seen[i] > 0);               /* every game can come up */
    for (int i = 2; i < 8; i++)
        rows[i].item.isFolder = 1;
    page.current = &rows[0];
    assert(!menuRandomGame() && page.current == &rows[0]); /* only folders: nothing picked */
    page.submenu = NULL;
    assert(!menuRandomGame());
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='ripps2-library-') as temp:
    for name, program in (('recents', recents), ('random', random_game)):
        src = Path(temp) / (name + '.cpp')
        src.write_text(program)
        exe = Path(temp) / (name + '.exe')
        subprocess.run(['g++', '-std=gnu++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('ok   recents.bin: newest first, last 10, one row per game, unreadable file kept')
assert 'guiLock(); // the IO worker replaces a device\'s list under this lock; the pick walks it twice\n            int picked = menuRandomGame();\n            guiUnlock();' in menusys, 'Random Game picks under guiLock'
print('ok   Random Game: games only, every game reachable, empty page reported')

libview = (root / 'src/ui/libview.cpp').read_text()
ring = libview[libview.index('if (mode == FAV_MODE) {\n        // Explicit order'):]
ring = ring[:ring.index('} else if (mode == APP_MODE)')]
stops = re.findall(r'\(cur == (LIB_VIEW_\w+)\) \? (LIB_VIEW_\w+)', ring)
assert stops == [('LIB_VIEW_ISO', 'LIB_VIEW_PS1'), ('LIB_VIEW_PS1', 'LIB_VIEW_ELF'), ('LIB_VIEW_ELF', 'LIB_VIEW_ALL'),
                 ('LIB_VIEW_ALL', 'LIB_VIEW_RECENT')], stops
assert re.search(r'if \(mode == FAV_MODE\)\n\s*return gAllGames \? 6 : 5;', libview), 'Favourites ring must have five stops'
print('ok   Favourites L3: PS2 -> PS1 -> ELF -> All -> Recent')

items = (root / 'src/ui/menuitems.cpp').read_text()
record = function(items, 'static void itemRecordRecent(')
assert 'if (!gFAVStartMode && !gAllGames && gAutosort != AUTOSORT_LAST_PLAYED)\n        return;' in record
select = function(items, 'static void itemExecSelect(')
assert select.index('itemRecordRecent(') == select.index('gsOf(support)->launch(') - len('itemRecordRecent(support, &curMenu->current->item);\n                ')
opl = (root / 'src/app/opl.cpp').read_text()
assert 'libViewActive(FAV_MODE) == LIB_VIEW_RECENT' in opl, 'the Recent stop must keep play order'
print('ok   launches record recents only when Favourites or Sort Games: Last played use them')
