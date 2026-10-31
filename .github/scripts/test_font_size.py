"""Compile the menu font, config and theme-layout paths with host stubs (original #1038).

Changing raster size must also rebuild list geometry, preserve explicitly sized theme
fonts, invalidate every font's glyph cache, and accept the same config on both boot paths.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
font = (ROOT / 'src/gfx/fntsys.cpp').read_text()
settingsio = (ROOT / 'src/app/settingsio.cpp').read_text()
opl = (ROOT / 'src/app/opl.cpp').read_text()
settings = (ROOT / 'src/core/settings.cpp').read_text()
theme = (ROOT / 'src/gfx/themes.cpp').read_text()
elems = (ROOT / 'src/gfx/themeelems.cpp').read_text()
menus = (ROOT / 'src/ui/menusys.cpp').read_text()
dia = (ROOT / 'src/ui/dia.cpp').read_text()
dia_header = (ROOT / 'include/dia.h').read_text()
gui = (ROOT / 'src/ui/gui.cpp').read_text()
guisettings = (ROOT / 'src/ui/guisettings.cpp').read_text()
font_header = (ROOT / 'include/fntsys.h').read_text()
opl_header = (ROOT / 'include/opl.h').read_text()


def function(source, signature):
    match = re.search(r'^' + re.escape(signature) + r'[^;{]*\)\s*\{', source, re.M)
    assert match is not None, signature
    return source[match.start():source.index('\n}', match.start()) + 2]


def run(name, program):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / (name + '.cpp')
        binary = Path(directory) / name
        source.write_text(program)
        subprocess.run(['g++', '-std=gnu++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                        '-Wno-missing-field-initializers', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)


font_enum = font_header[font_header.index('constexpr int FNTSYS_DEFAULT_SIZE'):font_header.index('/** Initializes')]
common = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
''' + font_enum + '\nstatic int gFontSize = FONT_SIZE_DEFAULT;\n' + function(font, 'int fntScaleFontSize(') + '\n'
common += re.search(r'^#define MENU_ITEM_HEIGHT[^\n]+', opl_header, re.M).group() + '\n'

run('font_size', common + r'''
#define FNT_MAX_COUNT 5
static const float fDPI = 72.0f;
typedef struct { int isValid, fontSize, face; } font_t;
static int flushes[FNT_MAX_COUNT], sizes[FNT_MAX_COUNT], nativeH = 480;
static void rmGetScreenExtents(int *w, int *h) { *w = 640; *h = 480; }
static void rmGetScreenExtentsNative(int *w, int *h) { *w = 640; *h = nativeH; }
static float rmGetPAR(void) { return 1.0f; }
static void FT_Set_Char_Size(int face, int x, int y, float dpiX, float dpiY)
{
    float delta = dpiX - 72.0f * nativeH / 480;
    assert(x == y && delta > -0.0001f && delta < 0.0001f && dpiX == dpiY);
    sizes[face] = x / 64;
}
struct FontSystem {
    font_t fonts_[FNT_MAX_COUNT];
    void cacheFlush(font_t *font) { flushes[font - fonts_]++; }
    void updateAspectRatioLocked(void);
};
''' + function(font, 'void FontSystem::updateAspectRatioLocked(') + r'''
int main(void)
{
    const int expected[] = {15, 17, 19};
    FontSystem fs;
    /* Built-in, translated and CJK fallback slots plus two explicitly sized theme faces. */
    for (int i = 0; i < FNT_MAX_COUNT; i++) fs.fonts_[i] = (font_t){1, 17, i};
    fs.fonts_[3].fontSize = 9;
    fs.fonts_[4].fontSize = 42;
    for (int mode = 0; mode < FONT_SIZE_COUNT; mode++) {
        gFontSize = fntValidFontSize(mode);
        assert(gFontSize == mode && MENU_ITEM_HEIGHT == expected[mode] + 2);
        for (nativeH = 224; nativeH <= 1080; nativeH += 214) {
            fs.updateAspectRatioLocked();
            for (int i = 0; i < 3; i++) assert(sizes[i] == expected[mode]);
            assert(sizes[3] == 9 && sizes[4] == 42);
            for (int i = 0; i < 3; i++) assert(fs.fonts_[i].fontSize == 17);
        }
    }
    for (int i = 0; i < FNT_MAX_COUNT; i++) assert(flushes[i] == 15);
    assert(fntValidFontSize(-1) == FONT_SIZE_DEFAULT && fntValidFontSize(FONT_SIZE_COUNT) == FONT_SIZE_DEFAULT);
    gFontSize = 7; /* an unnormalized value never shrinks or grows text */
    assert(fntScaleFontSize(FNTSYS_DEFAULT_SIZE) == FNTSYS_DEFAULT_SIZE);
    puts("PASS: menu raster sizes, custom point sizes, cache invalidation and config normalization");
}
''')

run('font_layout', common + r'''
typedef struct { int displayedItems; } items_list_t;
typedef struct theme_element { int height, type; void *extended; struct theme_element *next; } theme_element_t;
typedef struct { theme_element_t *first; } theme_elems_t;
typedef struct {
    theme_elems_t mainElems, infoElems, appsMainElems, appsInfoElems, favsMainElems, favsInfoElems;
    theme_elems_t vcdMainElems, vcdInfoElems, favsVcdMainElems, favsVcdInfoElems, favsAppsMainElems, favsAppsInfoElems;
} theme_t;
''' + function(elems, 'void itemsListFit(') + r'''
static int loadedFontSize = FONT_SIZE_DEFAULT;
static theme_t *gTheme;
enum { ELEM_TYPE_ITEMS_LIST = 1 };
static int rooms, resets;
static void thmComputeTextRooms(theme_elems_t *family) { assert(family != NULL); rooms++; }
typedef struct { void *pagestart, *current; } menu_item_t;
typedef struct menu_list { menu_item_t *item; struct menu_list *next; } menu_list_t;
static menu_list_t *menu;
''' + function(menus, 'void menuResetPageStarts(') + r'''
static void resetAndCheckPageStarts(void) {
    int first, second;
    menu_item_t a = {NULL, &first}, b = {NULL, &second};
    menu_list_t tail = {&b, NULL}, middle = {NULL, &tail}, head = {&a, &middle};
    menu = &head;
    menuResetPageStarts();
    assert(a.pagestart == a.current && b.pagestart == b.current);
    menu = NULL;
    resets++;
}
''' + function(theme, 'int thmRefreshFontSize(') + r'''
/* Gui::updateScreenScale's composition, under guiLock on the console. */
static void refresh(void) { if (thmRefreshFontSize()) resetAndCheckPageStarts(); }
int main(void)
{
    for (int mode = 0; mode < FONT_SIZE_COUNT; mode++) {
        gFontSize = mode;
        for (int height = 1; height <= 400; height++) {
            items_list_t items;
            itemsListFit(&items, height);
            assert(items.displayedItems >= 1);
            if (height >= MENU_ITEM_HEIGHT) assert(items.displayedItems * MENU_ITEM_HEIGHT <= height);
        }
    }
    /* All list families, including filtered siblings and one-row Coverflow, update in place. */
    static theme_t layout;
    theme_elems_t *families[] = {
        &layout.mainElems, &layout.infoElems, &layout.appsMainElems, &layout.appsInfoElems,
        &layout.favsMainElems, &layout.favsInfoElems, &layout.vcdMainElems, &layout.vcdInfoElems,
        &layout.favsVcdMainElems, &layout.favsVcdInfoElems, &layout.favsAppsMainElems, &layout.favsAppsInfoElems};
    theme_element_t lists[12][2];
    items_list_t counts[12][2];
    for (int i = 0; i < 12; i++) {
        lists[i][0] = (theme_element_t){398, ELEM_TYPE_ITEMS_LIST, &counts[i][0], &lists[i][1]};
        lists[i][1] = (theme_element_t){19, ELEM_TYPE_ITEMS_LIST, &counts[i][1], NULL};
        families[i]->first = &lists[i][0];
    }
    refresh(); /* Safe before theme/menu initialization. */
    assert(rooms == 0 && resets == 0);
    gTheme = &layout;
    loadedFontSize = -1;
    for (int mode = 0; mode < FONT_SIZE_COUNT; mode++) {
        gFontSize = mode;
        int before = rooms;
        refresh();
        assert(rooms == before + 12 && loadedFontSize == mode);
        for (int i = 0; i < 12; i++) {
            assert(counts[i][0].displayedItems == 398 / MENU_ITEM_HEIGHT);
            assert(counts[i][1].displayedItems == 1);
            assert(families[i]->first == &lists[i][0]);
        }
        refresh();
        assert(rooms == before + 12); /* No work for unchanged geometry. */
    }
    assert(resets == FONT_SIZE_COUNT);
    puts("PASS: list extents, all 12 theme families, in-place layout changes and visible selections");
}
''')

# Wiring: both config load paths normalize, the table saves/defaults, the preview applies live.
assert 'intSetting(CONFIG_OPL_FONT_SIZE, &gFontSize, FONT_SIZE_DEFAULT)' in settings
load = function(settingsio, 'void _loadConfig(')
assert load.index('settingsLoad(configOPL);') < load.index('gFontSize = fntValidFontSize(gFontSize);')
mini = function(opl, 'void miniInit(')
assert mini.index('CONFIG_OPL_FONT_SIZE, &gFontSize') < mini.index('gFontSize = fntValidFontSize(gFontSize);')
assert 'int gFontSize = FONT_SIZE_DEFAULT;' in opl, 'the autolaunch path never sees a zero (Small) default'
assert 'spacingH += fntScaleFontSize(FNTSYS_DEFAULT_SIZE) - FNTSYS_DEFAULT_SIZE;' in function(dia, 'void diaRenderUI(')
updater = function(guisettings, 'static int guiSettingsDisplayUpdater(')
assert 'UICFG_FONT_SIZE' in updater and updater.count('guiUpdateScreenScale();') >= 3
for page in ('static int guiSettingsShowInterface(',):
    body = function(guisettings, page)
    assert 'diaSetEnum(' in body and 'UICFG_FONT_SIZE, fontSizeNames' in body, page
    assert re.search(r'diaGetInt\(\w+, UICFG_FONT_SIZE, &gFontSize\)', body), page
scale = function(gui, 'void Gui::updateScreenScale(')
assert scale.index('lock();') < scale.index('if (thmRefreshFontSize())\n        menuResetPageStarts();') < scale.index('unlock();')
assert 'loadedFontSize = gFontSize;' in function(theme, 'static int thmLoad(')
assert 'itemsListFit(itemsList, elem->height);' in function(elems, 'void initItemsList(')
assert font.count('int fontSize = fntScaleFontSize(fonts_[id].fontSize);') == 2, 'LTR and RTL baselines must match raster size'
print('PASS: both config load paths, save/defaults, live preview and LTR/RTL wiring')

dialog_types = re.search(r'^typedef enum\s*\{.*?\} UIItemType;', dia_header, re.M | re.S).group()
dialog_types += re.search(r'^struct UIItem\s*\{.*?^\};', dia_header, re.M | re.S).group()
dialog_defines = '\n'.join(re.findall(r'^#define UI_SETTINGS_(?:SPACING_H|MIN_GAP)\s+\d+', dia, re.M))
dialog_functions = '\n'.join(function(dia, signature) for signature in (
    'static int diaShouldBreakLine(', 'static int diaShouldBreakLineAfter(',
    'static int diaItemHeight(', 'static void diaRowState(', 'static int diaRowCollapses(', 'static int diaLayoutBottom(', 'static int diaSettingsFitGap(',
))
run('font_dialog', common + dialog_types + '\n' + dialog_defines + r'''
static int diaScreenHeight = 480;
''' + dialog_functions + r'''
int main(void)
{
    static struct UIItem page[150];
    for (int mode = 0; mode < FONT_SIZE_COUNT; mode++) {
        gFontSize = mode;
        int size = fntScaleFontSize(FNTSYS_DEFAULT_SIZE);
        int spacing = UI_SETTINGS_SPACING_H + size - FNTSYS_DEFAULT_SIZE;
        for (int rows = 5; rows <= 30; rows += 5) {
            int n = 0;
            memset(page, 0, sizeof(page));
            page[n].type = UI_HEADER; page[n++].visible = 1;
            page[n].type = UI_SPLITTER; page[n++].visible = 1;
            for (int row = 0; row < rows; row++) {
                page[n].type = UI_LABEL; page[n++].visible = 1;
                page[n].type = UI_SPACER; page[n++].visible = 1;
                page[n].type = UI_BOOL; page[n++].visible = 1;
                page[n].type = UI_BREAK; page[n++].visible = 1;
            }
            page[n].type = UI_OK; page[n++].visible = 1;
            page[n].type = UI_TERMINATOR;
            int gap = diaSettingsFitGap(page, spacing, 20, 440);
            assert(spacing + gap >= size); /* Raster rows never overlap, even at minimum gap. */
            assert(gap >= UI_SETTINGS_MIN_GAP && gap <= spacing);
            if (gap > UI_SETTINGS_MIN_GAP) assert(diaLayoutBottom(page, spacing, gap, 20) <= 440);
            if (rows == 30) assert(gap == UI_SETTINGS_MIN_GAP); /* Tall pages scroll. */
        }
        assert(MENU_ITEM_HEIGHT >= size + 2);
    }
    puts("PASS: dialog rows fit or scroll without overlapping at every menu font size");
}
''')
