"""Execute the adapted Favorites #903/#904 C++ paths with storage/UI/clock fault injection."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


def run(name, source):
    directory = ROOT / "build/test-tmp"
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=directory) as tmp:
        src, exe = Path(tmp) / (name + ".cpp"), Path(tmp) / (name + ".exe")
        src.write_text("#include <initializer_list>\n" + source, encoding="utf-8")
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run([*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=15)


fav = read("src/sources/favsupport.cpp")
run("fav_remove", r'''
#include <cassert>
#include <cstddef>
struct fav_rec_t { int mode, id, kind; char *text; };
struct submenu_list_t { struct { int favourited; } item; };
struct opl_io_module_t {};
static submenu_list_t source = {{1}};
static opl_io_module_t module;
static int result, writes, finds;
static bool present = true;
static opl_io_module_t *oplGetModule(int mode) { assert(mode == 3); return present ? &module : nullptr; }
static int favKindView(int kind) { assert(kind == 2); return 9; }
static submenu_list_t *favFindVisibleSource(opl_io_module_t*, int id, const char *text, int view) {
    assert(writes == 1 && result && id == 17 && text && view == 9);
    ++finds; return &source;
}
class FavSupport {
public:
    int removeByIndex(int favIndex);
    fav_rec_t *array_ = nullptr; int count_ = 0;
    bool validIndex(int id) const { return array_ != nullptr && id >= 0 && id < count_; }
    int removeByIdAndText(int mode, int id, const char *text, int kind) {
        assert(source.item.favourited && mode == 3 && id == 17 && text && kind == 2);
        ++writes; return result;
    }
};
''' + function(fav, "int FavSupport::removeByIndex(") + r'''
int main() {
    FavSupport store;
    assert(!store.removeByIndex(0) && writes == 0);
    char name[] = "favorite";
    fav_rec_t record{3, 17, 2, name}; store.array_ = &record; store.count_ = 1;
    assert(!store.removeByIndex(-1) && !store.removeByIndex(1) && writes == 0);
    assert(!store.removeByIndex(0) && writes == 1 && finds == 0 && source.item.favourited);
    result = 1; writes = 0;
    assert(store.removeByIndex(0) && finds == 1 && !source.item.favourited);
    // Removing a stored record still succeeds when its device is absent.
    present = false; source.item.favourited = 1; writes = 0;
    assert(store.removeByIndex(0) && writes == 1 && finds == 1);
}
''')
assert "return gFavSupport.removeByIndex(favIndex);" in fav
assert "int favRemoveByIndex(int favIndex);" in read("include/favsupport.h")

menu = read("src/ui/menuitems.cpp")
run("fav_action", r'''
#include <cassert>
#include <vector>
enum { FAV_MODE = 5, FAV_KIND_VCD = 1, FAV_KIND_CUE = 2, SFX_CONFIRM = 10, IO_CUSTOM_SIMPLEACTION = 11 };
enum { _STR_FAV_REMOVE_FAILED, _STR_FAV_REMOVED, _STR_FAV_SAVING, _STR_FAV_SAVE_FAILED, _STR_FAV_SAVED };
struct item_list_t { int mode; };
struct submenu_item_t { int id, icon_id, text_id; const char *text; bool isFolder, favourited; };
struct row { submenu_item_t item; };
struct menu_item { row *current; void *userdata; };
static bool starState;
static bool gFavStarred() { return starState; }
static bool gFAVStartMode = true, gAllGames, writeOK, launchVcd = true, launchCue = true;
static int depth, kind, sourceId = 17, writes, sounds, reloads;
static std::vector<int> notices;
struct GameSource {
    bool hasLaunchVcd() { return launchVcd; }
    bool hasLaunchCue() { return launchCue; }
};
static GameSource gs;
static GameSource *gsOf(item_list_t*) { return &gs; }
static int folderDepth(int) { return depth; }
static int itemFavKind(item_list_t*, int, const char*) { return kind; }
static int libListSourceId(item_list_t*, int) { return sourceId; }
static int _l(int label) { return label; }
static void guiShowToast(int label) { notices.push_back(label); }
static int persist() { ++writes; return writeOK; }
static int favRemoveByIndex(int) { return persist(); }
static bool allGames;
static int favListsAllGames() { return allGames; }
static int favToggleByIndex(int) { return persist() ? !gFavStarred() : -1; }
static int removeFavouriteByIdAndText(int, int id, const char*, int) { assert(id == 17); return persist(); }
static int addFavouriteItem(int, int id, int, int, const char*, int) {
    assert(id == 17 && notices.back() == _STR_FAV_SAVING); return persist();
}
static void sfxPlay(int) { ++sounds; }
static void loadFavourites() {}
static void ioPutRequest(int, void*) { ++reloads; }
enum { LIB_VIEW_ALL = 3, LIB_VIEW_RECENT = 6 };
static int favView = LIB_VIEW_ALL;
static int libViewActive(int) { return favView; }
struct fav_rec_t { int mode, id, icon_id, text_id, kind; const char *text; };
struct FavStore {
    fav_rec_t rec{2, 17, 0, 0, 0, "title"};
    const fav_rec_t *entry(int id) { return id == 0 ? &rec : nullptr; }
};
static FavStore gFavSupport;
''' + function(menu, "void itemExecFav(") + r'''
static void reset() { writes = sounds = reloads = 0; notices.clear(); }
int main() {
    item_list_t support{1}; row selected{{0, 0, 0, "title", false, false}};
    menu_item menu{&selected, &support};
    // Failure displays feedback without a false success sound, reload or star.
    itemExecFav(&menu);
    assert(writes == 1 && sounds == 0 && reloads == 0 && !selected.item.favourited);
    assert((notices == std::vector<int>{_STR_FAV_SAVING, _STR_FAV_SAVE_FAILED}));
    reset(); writeOK = true; itemExecFav(&menu);
    assert(selected.item.favourited && sounds == 1 && reloads == 1 && notices.back() == _STR_FAV_SAVED);
    reset(); writeOK = false; itemExecFav(&menu);
    assert(selected.item.favourited && sounds == 0 && reloads == 0 && notices.back() == _STR_FAV_REMOVE_FAILED);
    reset(); writeOK = true; itemExecFav(&menu);
    assert(!selected.item.favourited && sounds == 1 && reloads == 1 && notices.back() == _STR_FAV_REMOVED);
    support.mode = FAV_MODE; reset(); writeOK = false; itemExecFav(&menu);
    assert(writes == 1 && sounds == 0 && reloads == 0 && notices.back() == _STR_FAV_REMOVE_FAILED);
    reset(); writeOK = true; itemExecFav(&menu);
    assert(writes == 1 && sounds == 1 && reloads == 1 && notices.back() == _STR_FAV_REMOVED);
    // On the Recent stop R3 stars the game (nothing is removed); a failed write says so.
    favView = LIB_VIEW_RECENT; reset(); writeOK = true; itemExecFav(&menu);
    assert(writes == 1 && sounds == 1 && reloads == 1 && notices.back() == _STR_FAV_SAVED);
    reset(); writeOK = false; itemExecFav(&menu);
    assert(writes == 1 && sounds == 0 && reloads == 0 && notices.back() == _STR_FAV_SAVE_FAILED);
    selected.item.id = 3; reset(); itemExecFav(&menu); selected.item.id = 0; // stale row: no-op
    assert(writes == 0 && sounds == 0 && notices.empty());
    favView = LIB_VIEW_ALL;
    // All Games: R3 toggles the row's star in place, with no reload; a failed write keeps the star.
    allGames = true; starState = false; reset(); writeOK = true; selected.item.favourited = false; itemExecFav(&menu);
    assert(writes == 1 && sounds == 1 && reloads == 0 && selected.item.favourited && notices.back() == _STR_FAV_SAVED);
    starState = true; reset(); writeOK = false; itemExecFav(&menu);
    assert(writes == 1 && sounds == 0 && selected.item.favourited && notices.back() == _STR_FAV_REMOVE_FAILED);
    reset(); writeOK = true; itemExecFav(&menu);
    assert(sounds == 1 && reloads == 0 && !selected.item.favourited && notices.back() == _STR_FAV_REMOVED);
    allGames = false; starState = false;
    // Every existing no-op guard must still suppress both storage and feedback.
    support.mode = 1; reset(); gFAVStartMode = false; itemExecFav(&menu); gFAVStartMode = true;
    menu.current = nullptr; itemExecFav(&menu); menu.current = &selected;
    menu.userdata = nullptr; itemExecFav(&menu); menu.userdata = &support;
    selected.item.isFolder = true; itemExecFav(&menu); selected.item.isFolder = false;
    depth = 1; itemExecFav(&menu); depth = 0;
    kind = -1; itemExecFav(&menu); kind = 0;
    sourceId = -1; itemExecFav(&menu); sourceId = 17;
    kind = FAV_KIND_VCD; launchVcd = false; itemExecFav(&menu);
    kind = FAV_KIND_CUE; launchCue = false; itemExecFav(&menu);
    assert(writes == 0 && sounds == 0 && reloads == 0 && notices.empty());
}
''')

libview = read("src/ui/libview.cpp")
view_enum = re.search(r"enum LIB_VIEW \{.*?\};", read("include/libview.h"), re.S).group()
mode_enum = re.search(r"enum IO_MODES \{.*?\};", read("include/iosupport.h"), re.S).group()
initial = libview[libview.index("#define LIB_VIEW_ENTRIES"):libview.index("// Mixed has")]
run("fav_default", r'''
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#define LIB_VIEW_STATE_NONE '-'
''' + mode_enum + r'''
#define BDM_MODE_LAST BDM_MODE7
#define BDM_MODE_COUNT (BDM_MODE_LAST - BDM_MODE + 1)
''' + view_enum + "\n" + initial + function(libview, "static int libViewIsDeviceMode(") +
    function(libview, "int libViewSupported(") + function(libview, "static void libViewDecodeRing(") +
    function(libview, "static void libViewEncodeRing(") + r'''
int main() {
    assert(retainedView[FAV_MODE] == LIB_VIEW_ALL);
    for (int i = 0; i < LIB_VIEW_ENTRIES; ++i) if (i != FAV_MODE) assert(retainedView[i] == LIB_VIEW_ISO);
    libViewDecodeRing("", retainedView.data(), nullptr);
    libViewDecodeRing("000", retainedView.data(), nullptr);
    assert(retainedView[FAV_MODE] == LIB_VIEW_ALL);
    char saved[MODE_COUNT + 1];
    for (int view = LIB_VIEW_ISO; view <= LIB_VIEW_ALL; ++view) {
        memset(saved, '0', MODE_COUNT); saved[MODE_COUNT] = 0; saved[FAV_MODE] = '0' + view;
        libViewDecodeRing(saved, retainedView.data(), nullptr);
        assert(retainedView[FAV_MODE] == view);
        char roundtrip[MODE_COUNT + 1]; libViewEncodeRing(roundtrip, retainedView.data(), nullptr);
        assert(!strcmp(roundtrip, saved));
    }
    saved[FAV_MODE] = '9'; libViewDecodeRing(saved, retainedView.data(), nullptr);
    assert(retainedView[FAV_MODE] == LIB_VIEW_ALL);
}
''')

notify = read("src/ui/guinotify.cpp")
toast_bytes = int(re.search(r"char toast_\[(\d+)\]", read("include/guinotify.h")).group(1))
run("fav_toast", "#define TOAST_SIZE %d\n" % toast_bytes + r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
static uint32_t tick;
#define clock() tick
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 1000000
class Notifications {
public:
    void setToast(const char*); void drawToast();
    char toast_[TOAST_SIZE] = {0}; uint32_t toastStart_ = 0;
    // The toast is drawn alone at the top: nothing stacks below it, so no y advance.
    static int render(const char *text, int y, int yadd) { assert(text[0] && y == 10 && yadd == 0); ++draws; return y + yadd; }
    static int draws;
};
int Notifications::draws;
''' + function(notify, "void Notifications::setToast(") + function(notify, "void Notifications::drawToast(") + r'''
int main() {
    Notifications n; n.drawToast(); assert(!n.draws);
    for (uint32_t start : {uint32_t(0), uint32_t(0x7fffffff), uint32_t(0xffffffff - 1000000)}) {
        tick = start; n.setToast("Saving..."); n.drawToast();
        int before = n.draws;
        tick = start + 2999999; n.drawToast(); assert(n.draws == before + 1);
        tick = start + 3000000; n.drawToast(); assert(n.draws == before + 1 && !n.toast_[0]);
    }
    char longText[TOAST_SIZE + 100]; memset(longText, 'x', sizeof(longText) - 1); longText[sizeof(longText) - 1] = 0;
    n.setToast(longText); assert(strlen(n.toast_) == TOAST_SIZE - 1);
    n.setToast(nullptr); n.drawToast(); assert(!n.toast_[0]);
}
''')
gui = read("src/ui/gui.cpp")
main_loop = function(gui, "void Gui::mainLoop()")
assert "#endif\n        gNotifications.drawToast();" in main_loop
assert function(gui, "void Gui::showToast(").index("startFrame();") < function(gui, "void Gui::showToast(").index("gNotifications.drawToast();")

theme = read("src/gfx/themeelems.cpp")
draw = function(theme, "void drawItemsList(")
geometry = draw[draw.index("        const int favRoom"):draw.index("        while (ps &&")]
star = draw[draw.index("            if (favRoom && ps->item.favourited)"):draw.index("            posY += MENU_ITEM_HEIGHT;")]
run("fav_title", r'''
#include <cassert>
#include <cstring>
enum { ALIGN_VCENTER = 2, ALIGN_RIGHT = 4, ALIGN_HCENTER = 8, MENU_ITEM_HEIGHT = 20, DECORATOR_SIZE = 20, FAV_MARK };
struct GSTEXTURE { void *Mem; };
struct Element { int posX, textRoom, width, font, scaled; short aligned; };
struct Image { void *cache; };
struct Items { Image *decoratorImage; };
struct Row { struct { bool favourited; } item; };
static bool gFAVStartMode, gEnableDiscArt;
static int gDefaultCol, starDraws, starPosition;
static GSTEXTURE texture{&texture};
static GSTEXTURE *thmGetTexture(int) { return &texture; }
static bool isHiddenArtworkCache(void*) { return false; }
static int rmUnScaleX(int value) { return value; }
static int fntCalcDimensions(int, const char *text) { return strlen(text) * 10; }
static void rmDrawPixmap(GSTEXTURE*, int x, int, int align, int, int, int, int, int) {
    assert(!(align & (ALIGN_HCENTER | ALIGN_RIGHT))); ++starDraws; starPosition = x;
}
static void check(short align, bool favorites, bool starred, bool decorator, int room, const char *dispText) {
    Element element{320, room, 200, 0, 0, align}; Element *elem = &element;
    Image image{}; Items items{decorator ? &image : nullptr}; Items *itemsList = &items;
    Row row{{starred}}; Row *ps = &row; int posY = 10;
    gFAVStartMode = favorites; gEnableDiscArt = true; starDraws = 0;
''' + geometry + star + r'''
    int anchor = 320 + (decorator ? DECORATOR_SIZE : 0);
    int available = room > 0 ? room : 200;
    if (favorites && (align & ALIGN_HCENTER)) {
        assert(textX == anchor && textAlign == align);
        assert(textRoom == (available > 48 ? available - 48 : available));
        if (starred) {
            int visible = strlen(dispText) * 10; if (visible > textRoom) visible = textRoom;
            assert(starPosition == anchor - visible / 2 - 24);
        }
    } else if (favorites) {
        assert(textX == anchor - ((align & ALIGN_RIGHT) ? available : 0) + 24);
        assert(textRoom == (available > 24 ? available - 24 : 1));
    } else {
        assert(textX == anchor && textRoom == available && textAlign == align);
    }
    assert(starDraws == (favorites && starred));
}
int main() {
    for (short align : {short(0), short(ALIGN_RIGHT), short(ALIGN_HCENTER | ALIGN_VCENTER)})
        for (bool favorites : {false, true}) for (bool starred : {false, true})
            for (bool decorator : {false, true}) for (int room : {0, 20, 48, 100, 400})
                for (const char *text : {"short", "A very long title that scrolls beyond its available room"})
                    check(align, favorites, starred, decorator, room, text);
}
''')
print("Favorites: persistence failure, R3 feedback/guards, saved views, clock wrap and title/star geometry passed")
