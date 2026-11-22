"""Behavioral controls for Grid/Coverflow device navigation and presentation.

The production function bodies are extracted and compiled against linked pages and recording draw stubs.
The checks fail if browsing a Grid device activates it prematurely, if Cross falls through to a game,
if Coverflow needs its old focus mode, or if Coverflow does not use the vertical device renderer.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]


def function_text(source, signature, required=True):
    try:
        start = source.index(signature)
    except ValueError:
        if required:
            raise SystemExit(f"missing production function: {signature}")
        return ""
    brace = source.index("{", start)
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise SystemExit(f"unterminated production function: {signature}")


menusys = (root / "src/ui/menusys.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
activate = function_text(menusys, "static void menuActivateH(struct menu_list *target)", required=False)
input_program = r'''
#include <cstdio>
#include <cstring>
typedef unsigned long long u64;
enum { KEY_LEFT=1, KEY_RIGHT=2, KEY_UP=4, KEY_DOWN=8, KEY_CROSS=16, KEY_CIRCLE=32, KEY_START=64 };
enum { SFX_CURSOR=1, GUI_SCREEN_MENU=1 };
struct submenu_list { int unused; };
struct menu_item;
struct menu_list { menu_item *item; menu_list *next, *prev; };
struct menu_item {
    int visible = 1;
    submenu_list *current = nullptr;
    void *userdata = nullptr;
    void (*execCross)(menu_item *) = nullptr;
};
static int launches;
static void gameCross(menu_item *) { launches++; }
static menu_list pages[3];
static menu_list *selected_item;
menu_list *gGridSidebarCursor;
int gGridSidebarOpen, gCoverflowDeviceBarFocused;
static int menuPageIsGridResult = 1, coverflowBarAvailable = 1;
static int itemConfigId = -1, itemConfigGeneration = 0, menuSemaId = 0;
static int previousCoverMoves, nextCoverMoves;
namespace raii { struct SemaLock { explicit SemaLock(int) {} }; }
static int menuItemShown(menu_item *item) { return item != nullptr && item->visible; }
static void menuFolderResetLeaving(menu_list *) {}
static void sfxPlay(int) {}
static int keys;
static int getKey(int key) { return keys & key; }
static int getKeyOn(int key) { return keys & key; }
static void menuInitMainMenu() {}
static void guiSwitchScreen(int) {}
static int menuPageIsGrid() { return menuPageIsGridResult; }
static int menuCoverflowDeviceBarAvailable() { return coverflowBarAvailable; }
static int menuGridAtRowStart() { return 1; }
static void menuGridRow(int) {}
static void menuPrevV() { previousCoverMoves++; }
static void menuNextV() { nextCoverMoves++; }
struct theme { int gridDevices; void *coverflow; void *psbbnFlow; } themeStore;
static theme *gTheme = &themeStore;
@ACTIVATE_FALLBACK@
@NEXT@
@PREV@
@SIDEBAR@
@LEFT@
@UP@
@DOWN@
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static void resetPages() {
    for (int i = 0; i < 3; i++) {
        pages[i].item = new menu_item;
        pages[i].item->execCross = gameCross;
        pages[i].prev = i ? &pages[i-1] : nullptr;
        pages[i].next = i < 2 ? &pages[i+1] : nullptr;
    }
}
static void resetInput() {
    keys = 0; launches = 0; itemConfigGeneration = 0; itemConfigId = -1;
    gGridSidebarOpen = 0; gCoverflowDeviceBarFocused = 0;
    previousCoverMoves = nextCoverMoves = 0;
    themeStore.gridDevices = 1; themeStore.coverflow = &themeStore; themeStore.psbbnFlow = nullptr;
    menuPageIsGridResult = 1; coverflowBarAvailable = 1;
}
int main() {
    resetPages(); resetInput();
    selected_item = &pages[1]; gGridSidebarCursor = &pages[1]; gGridSidebarOpen = 1;
    keys = KEY_DOWN; menuGridSidebarInput();
    CHECK(selected_item == &pages[1]);
    CHECK(gGridSidebarCursor == &pages[2]);
    CHECK(itemConfigGeneration == 0 && launches == 0 && gGridSidebarOpen == 1);

    resetInput(); selected_item = &pages[0]; gGridSidebarCursor = &pages[1]; gGridSidebarOpen = 1;
    keys = KEY_CROSS; menuGridSidebarInput();
    CHECK(selected_item == &pages[1]);
    CHECK(itemConfigGeneration == 1 && launches == 0 && gGridSidebarOpen == 0);

    resetInput(); selected_item = &pages[1]; gGridSidebarCursor = &pages[1]; gGridSidebarOpen = 1;
    keys = KEY_CROSS; menuGridSidebarInput();
    CHECK(selected_item == &pages[1]);
    CHECK(itemConfigGeneration == 0 && launches == 0 && gGridSidebarOpen == 0);

    resetInput(); selected_item = &pages[1]; gGridSidebarCursor = &pages[1]; gGridSidebarOpen = 1;
    keys = KEY_RIGHT; menuGridSidebarInput();
    CHECK(selected_item == &pages[1]);
    CHECK(itemConfigGeneration == 0 && launches == 0 && gGridSidebarOpen == 0);

    resetInput(); selected_item = &pages[1]; gGridSidebarCursor = &pages[0]; gGridSidebarOpen = 1;
    keys = KEY_CIRCLE; menuGridSidebarInput();
    CHECK(selected_item == &pages[1]);
    CHECK(itemConfigGeneration == 0 && launches == 0 && gGridSidebarOpen == 0);
    keys = KEY_LEFT; menuNavigateLeft();
    CHECK(gGridSidebarOpen == 1 && gGridSidebarCursor == &pages[1]);
    CHECK(itemConfigGeneration == 0 && launches == 0);

    resetInput(); selected_item = &pages[1]; gGridSidebarCursor = nullptr;
    keys = KEY_LEFT; menuNavigateLeft();
    CHECK(gGridSidebarOpen == 1 && gGridSidebarCursor == &pages[1]);
    CHECK(previousCoverMoves == 0 && launches == 0);

    resetInput(); menuPageIsGridResult = 0; selected_item = &pages[1];
    keys = KEY_UP; menuNavigateUp();
    CHECK(selected_item == &pages[0]);
    CHECK(itemConfigGeneration == 1 && gCoverflowDeviceBarFocused == 0 && launches == 0);

    resetInput(); menuPageIsGridResult = 0; selected_item = &pages[1];
    keys = KEY_DOWN; menuNavigateDown();
    CHECK(selected_item == &pages[2]);
    CHECK(itemConfigGeneration == 1 && gCoverflowDeviceBarFocused == 0 && launches == 0);
    return failures ? 1 : 0;
}
'''
input_program = (input_program.replace("@ACTIVATE_FALLBACK@", "static void menuActivateH(struct menu_list *) {}" if not activate else "")
                 .replace("@NEXT@", function_text(menusys, "static void menuNextH()"))
                 .replace("@PREV@", function_text(menusys, "static void menuPrevH()"))
                 .replace("@SIDEBAR@", function_text(menusys, "static void menuGridSidebarInput(void)"))
                 .replace("@LEFT@", function_text(menusys, "static void menuNavigateLeft()"))
                 .replace("@UP@", function_text(menusys, "static void menuNavigateUp()"))
                 .replace("@DOWN@", function_text(menusys, "static void menuNavigateDown()"))
                 .replace("@ACTIVATE@", activate))

if activate:
    input_program = input_program.replace("static void menuNextH()", activate + "\nstatic void menuNextH()", 1)

themeelems = (root / "src/gfx/themeelems.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
bar = function_text(themeelems, "if (elem->style == THM_MENU_STYLE_PSBBN_BAR)")
render_program = r'''
#include <cstdio>
typedef unsigned long long u64;
#define THM_MENU_STYLE_PSBBN_BAR 7
#define ALIGN_VCENTER 2
#define GS_SETREG_RGBA(r,g,b,a) ((u64)0)
struct menu_item { int visible = 1; const char *text = ""; };
struct menu_list { menu_item *item; menu_list *next = nullptr, *prev = nullptr; };
struct theme_elems { unsigned char hasGrid; };
struct theme_element {
    unsigned char style = THM_MENU_STYLE_PSBBN_BAR;
    int font = 0, width = 540, height = 20, posX = 50, posY = 400, aligned = 0;
    u64 color = 0;
    theme_elems *family = nullptr;
};
struct theme { void *coverflow; void *psbbnFlow; u64 selTextColor = 0, textColor = 0; int usedHeight = 480; } themeStore;
static theme *gTheme = &themeStore;
static int verticalDraws, horizontalDraws, verticalOpen;
static menu_list *verticalActive, *verticalHighlight;
static int menuItemShown(menu_item *item) { return item->visible; }
static const char *menuItemGetText(menu_item *item) { return item->text; }
static int rmWideScale(int x) { return x; }
static int rmUnScaleX(int x) { return x; }
static int fntCalcDimensions(int, const char *text) { return (int)std::strlen(text) * 6; }
static int thmMenuStyleStartX(int x, int, int) { return x; }
static void rmDrawRect(int, int, int, int, u64) {}
static void fntRenderString(int, int, int, int, int, int, const char *, u64) {}
static void fntRenderStringFit(int, int, int, int, int, const char *, u64, int) {}
static void gridDrawPageGlyph(menu_list *, int, int, int, u64) { horizontalDraws++; }
static void gridDrawDeviceSidebar(menu_list *active, int, int open, menu_list *highlight) {
    verticalDraws++; verticalOpen = open; verticalActive = active; verticalHighlight = highlight;
}
static void drawBar(menu_list *menu, theme_element *elem) {
@BRANCH@
}
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
int main() {
    menu_item a, b; a.text = "A"; b.text = "B";
    menu_list first{&a}, second{&b}; first.next = &second; second.prev = &first;
    theme_element elem;
    verticalDraws = horizontalDraws = 0; themeStore.coverflow = &themeStore; themeStore.psbbnFlow = nullptr;
    drawBar(&second, &elem);
    CHECK(verticalDraws == 1 && horizontalDraws == 0);
    CHECK(verticalOpen == 0 && verticalActive == &second && verticalHighlight == &second);

    verticalDraws = horizontalDraws = 0; themeStore.coverflow = &themeStore; themeStore.psbbnFlow = &themeStore;
    drawBar(&second, &elem);
    CHECK(verticalDraws == 0 && horizontalDraws == 2);
    return failures ? 1 : 0;
}
'''
render_program = render_program.replace("@BRANCH@", bar)
render_program = render_program.replace("#include <cstdio>", "#include <cstdio>\n#include <cstring>", 1)


def compile_and_run(folder, name, source):
    cpp = folder / f"{name}.cpp"
    exe = folder / name
    cpp.write_text(source, encoding="utf-8")
    built = subprocess.run(["g++", "-std=gnu++17", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-parameter", str(cpp), "-o", str(exe)], capture_output=True, text=True)
    if built.returncode:
        print(built.stderr)
        return 1
    ran = subprocess.run([str(exe)], capture_output=True, text=True)
    print(ran.stdout, end="")
    if ran.stderr:
        print(ran.stderr, end="", file=sys.stderr)
    return ran.returncode


with tempfile.TemporaryDirectory(prefix="device-navigation-") as tmp:
    folder = Path(tmp)
    results = [compile_and_run(folder, "input", input_program), compile_and_run(folder, "render", render_program)]
if any(results):
    sys.exit("FAIL: device navigation/presentation behavior differs from the approved contract")
print("PASS: Grid highlight/X behavior, direct vertical Coverflow devices, and PSBBN Flow control")
