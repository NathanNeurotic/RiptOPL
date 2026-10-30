"""Exercise RA badge-row ownership across producer cleanup and allocation failures (original #1039).

A badged row keeps its source name as identity (Favorites, rename, artwork, sorting) and owns a
copy of the display text, so the I/O thread rebuilding the badge cache cannot leave a row
pointing into freed memory. Every allocation-failure path releases that copy.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
menu = (ROOT / 'src/ui/menusys.cpp').read_text()
gui = (ROOT / 'src/ui/gui.cpp').read_text()
queue = (ROOT / 'src/ui/guiqueue.cpp').read_text()
opl = (ROOT / 'src/app/opl.cpp').read_text()
settings = (ROOT / 'src/ui/guisettings.cpp').read_text()


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


append = gui[gui.index('        case GUI_OP_APPEND_MENU:'):gui.index('        case GUI_OP_SELECT_MENU:')]
program = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RETROACHIEVEMENTS 1
enum { GUI_OP_APPEND_MENU = 1 };
typedef int s32;
typedef struct submenu_item {
    int icon_id, text_id, id, isFolder, lastPlayed, raBadged, favourited;
    char *text, *raText;
    int *cache_id, *cache_uid;
} submenu_item_t;
typedef struct submenu_list {
    submenu_item_t item;
    struct submenu_list *prev, *next;
} submenu_list_t;
typedef struct menu_item {
    submenu_list_t *last, *submenu, *current, *pagestart;
    int remindLast;
} menu_item_t;
struct gui_update_t {
    int type;
    struct { menu_item_t *menu; submenu_list_t **subMenu; } menu;
    struct { int icon_id, id, text_id, isFolder, lastPlayed, raBadged, favourited, selected, autoStart; char *text, *raText; } submenu;
};
static int live, failNext, appendFail, depth;
static void gridForgetItem(const void *) {} // grid.cpp: a freed row drops its case-flip state
static int gRememberLastPlayed, gAutoStartLastPlayed, KeyPressedOnce, DisableCron;
static void *take(size_t n)
{
    if (failNext) {
        failNext = 0;
        return NULL;
    }
    void *p = malloc(n);
    assert(p);
    live++;
    return p;
}
static void drop(void *p)
{
    if (p) {
        assert(live > 0);
        live--;
        free(p);
    }
}
static char *copy(const char *s) { char *p = (char *)take(strlen(s) + 1); strcpy(p, s); return p; }
#define malloc take
#define free drop
namespace raii { struct SemaLock { explicit SemaLock(s32) { assert(depth++ == 0); } ~SemaLock() { --depth; } }; }
class DeferredOpQueue {
public:
    int enqueue(struct gui_update_t *op);
private:
    struct Node { struct gui_update_t *item; Node *next; };
    Node *head_ = NULL, *tail_ = NULL;
    s32 sema_ = 1;
    int scheduled_ = 0;
};
@ENQUEUE@
static char *_l(int) { return (char *)"localized"; }
static submenu_list_t *submenuAppendItem(submenu_list_t **head, int, char *text, int id, int textId)
{
    if (appendFail)
        return NULL;
    submenu_list_t *n = (submenu_list_t *)malloc(sizeof(*n));
    memset(n, 0, sizeof(*n));
    n->item.text = text, n->item.id = id, n->item.text_id = textId;
    *head = n;
    return n;
}
@DESTROY@
@GETTEXT@
static void appendOp(struct gui_update_t *item)
{
    submenu_list_t *result = NULL;
    switch (item->type) {
@APPEND@
    }
}
static struct gui_update_t *op(menu_item_t *m, submenu_list_t **head)
{
    struct gui_update_t *p = (struct gui_update_t *)malloc(sizeof(*p));
    memset(p, 0, sizeof(*p));
    p->type = GUI_OP_APPEND_MENU, p->menu.menu = m, p->menu.subMenu = head;
    p->submenu.text = (char *)"Crash", p->submenu.text_id = -1;
    p->submenu.raText = copy("Crash [RA]"), p->submenu.raBadged = 1;
    return p;
}
int main(void)
{
    menu_item_t m = {};
    submenu_list_t *head = NULL;
    DeferredOpQueue q;
    /* The queue cannot take the op: op and snapshot are released. */
    struct gui_update_t *p = op(&m, &head);
    failNext = 1;
    assert(q.enqueue(p) == -1 && depth == 0 && live == 0);
    /* The menu cannot take the row: the snapshot is released, navigation untouched. */
    p = op(&m, &head);
    appendFail = 1;
    appendOp(p);
    free(p);
    assert(head == NULL && m.last == NULL && live == 0);
    /* Success: identity is the source name, the display is the row's own copy, which outlives the
       producer's op (and the badge cache it was copied from). */
    appendFail = 0;
    p = op(&m, &head);
    appendOp(p);
    free(p);
    assert(head && !strcmp(head->item.text, "Crash") && !strcmp(submenuItemGetText(&head->item), "Crash [RA]"));
    assert(live == 2);
    submenuDestroyItem(head);
    assert(live == 0);
    /* No badge (or a failed copy): the plain name, unchanged. */
    p = op(&m, &head);
    free(p->submenu.raText);
    p->submenu.raText = NULL;
    appendOp(p);
    free(p);
    assert(!strcmp(submenuItemGetText(&head->item), "Crash"));
    submenuDestroyItem(head);
    assert(live == 0);
    puts("PASS: RA badge snapshots keep row identity, outlive the producer, and are freed on every failure path");
    return 0;
}
'''
program = program.replace('@ENQUEUE@', function(queue, 'int DeferredOpQueue::enqueue('))
program = program.replace('@DESTROY@', function(menu, 'static void submenuDestroyItem('))
program = program.replace('@GETTEXT@', function(menu, 'char *submenuItemGetText('))
program = program.replace('@APPEND@', append)

# The producer copies only the cosmetic label; the source name stays the row's identity.
update = function(opl, 'static void updateMenuFromGameList(')
assert 'gup->submenu.raText = badge != NULL ? strdup(badge) : NULL;' in update
assert 'gup->submenu.text = mdl->support->mode == FAV_MODE ? const_cast<char *>(favGetItemDisplay(i)) : gsOf(mdl->support)->name(mdl->support, i);' in update and '(badge != NULL ? badge' not in update
# Toggling telemetry, badges or placement rebuilds the lists (badges are resolved when rows are built).
net = function(settings, 'int guiShowNetConfig(')
snap = net.index('int raBadgeState')
assert snap < net.index('diaGetInt(diaNetConfig, NETCFG_RA_TELEMETRY') < net.index('guiRefreshCfgTitles();')
assert 'gRABadgeSuffix = (gRABadgeSuffix == 1);' in net

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'rows.cpp'
    exe = Path(tmp) / 'rows'
    src.write_text(program)
    subprocess.run(['g++', '-std=gnu++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', str(src), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
