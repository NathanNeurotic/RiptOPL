#!/usr/bin/env python3
"""Check 15-title jumps, multi-row paging, wraparound, home/end, animation and the page after a layout switch."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
source = (Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'src/ui/menusys.cpp').read_text(encoding='utf-8')
functions = []
start = source.index('static constexpr int carouselPageStep')
functions.append(source[start:source.index('\n}', start) + 2])
for name in ('menuFirstPage', 'menuLastPage', 'menuNextV', 'menuPrevV', 'menuCarouselPage', 'menuListPage', 'menuFillPage'):
    start = source.index('static void ' + name + '(')
    functions.append(source[start:source.index('\n}', start) + 2])
harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
typedef struct node {struct node *next, *prev;} submenu_list_t;
typedef struct {int displayedItems; int hidden;} items_list_t;
static items_list_t list = {1, 0};
static struct {void *extended;} element = {&list};
static struct {int coverflow; __typeof__(element) *itemsList;} theme = {1, &element};
static __typeof__(theme) *gTheme = &theme;
typedef struct menu_item {submenu_list_t *current, *pagestart, *submenu;} menu_item_t;
static menu_item item;
static struct menu_list_t {menu_item *item;} selection = {&item};
static menu_list_t *selected_item = &selection;
#define SFX_CURSOR 0
#define KEY_R1 13
#define KEY_L1 14
static int fresh = 1; /* getKeyOn: 1 a new press, 0 a held key's repeat */
static int getKeyOn(int id) {(void)id;return fresh;}
static int sounds;
static void sfxPlay(int id) {(void)id;sounds++;}
static int menuSemaId, semaDepth;
static void WaitSema(int id) {(void)id;semaDepth++;}
static void SignalSema(int id) {(void)id;semaDepth--;}
static int animations, direction;
[[maybe_unused]] static void thmTriggerCoverflowAnim(int dir) {animations++;direction=dir;}
@FUNCTIONS@
#define menuNextPage() menuListPage(1)
#define menuPrevPage() menuListPage(-1)
static submenu_list_t nodes[64];
static void setup(int count, int selected, int first=0) {
    for (int i=0;i<count;i++) {
        nodes[i].prev=i ? &nodes[i-1] : NULL;
        nodes[i].next=i+1<count ? &nodes[i+1] : NULL;
    }
    item.submenu=count ? &nodes[0] : NULL;
    item.current=count ? &nodes[selected] : NULL;
    item.pagestart=count ? &nodes[first] : NULL;
    animations=direction=sounds=0;
}
static void checkPage(int count, int selected, int first, int dir, int target, int top) {
    setup(count,selected,first);
    if (dir>0) menuNextPage(); else menuPrevPage();
    assert(item.current==&nodes[target] && item.pagestart==&nodes[top]);
    assert(animations==(theme.coverflow && target!=selected));
    if (animations) assert(direction==dir);
}
int main(void) {
    /* Coverflow (3/5), PSBBN Flow, compact custom layouts and one-row info pages all
       share a one-row ItemsList. Test every position, including short lists and both ends. */
    list.displayedItems=1;
    for (int carousel=0;carousel<=1;carousel++) {
        theme.coverflow=carousel;
        for (int n=1;n<=64;n++) for (int s=0;s<n;s++) for (int dir=-1;dir<=1;dir+=2) {
            int k=n<=15 ? 1 : 15; /* a library no bigger than a jump steps one title, like the D-pad */
            int target=dir>0 ? (s==n-1 ? 0 : (s+k<n ? s+k : n-1))
                             : (s==0 ? n-1 : (s>k ? s-k : 0));
            // Entering Info from a multi-row main list can retain an earlier page start.
            for (int first=0;first<=s;first++) {
                checkPage(n,s,first,dir,target,target);
                assert(sounds==(target!=s));
                assert(list.displayedItems==1); // rendering still has one title row
            }
        }
    }
    /* Short final steps land at the end, and the next press wraps. */
    theme.coverflow=1;setup(40,0);
    int forward[]={15,30,39,0}, backward[]={39,24,9,0};
    for (int target:forward) {menuNextPage();assert(item.current==&nodes[target]);}
    for (int target:backward) {menuPrevPage();assert(item.current==&nodes[target]);}
    /* Carousel paging follows the selected cover, not a multi-row ItemsList or a stale page start. */
    list.displayedItems=18;
    for (int n=1;n<=64;n++) for (int s=0;s<n;s++) for (int dir=-1;dir<=1;dir+=2) {
        setup(n,s);
        int k=n<=15 ? 1 : 15;
        int target=dir>0 ? (s==n-1 ? 0 : (s+k<n ? s+k : n-1))
                         : (s==0 ? n-1 : (s>k ? s-k : 0));
        menuCarouselPage(dir);
        assert(item.current==&nodes[target] && item.pagestart==&nodes[target]);
        assert(sounds==(target!=s) && animations==(target!=s));
    }
    setup(0,0); menuCarouselPage(1); menuCarouselPage(-1);
    assert(!item.current && !sounds && !animations);
    list.displayedItems=1;
    /* Ordinary D-pad moves and L2/R2 still move one title or go straight to an end. */
    setup(40,0);menuNextPage();menuNextV();
    assert(item.current==&nodes[16] && item.pagestart==&nodes[16]);
    menuPrevV();assert(item.current==&nodes[15] && item.pagestart==&nodes[15]);
    menuLastPage();assert(item.current==&nodes[39] && item.pagestart==&nodes[39]);
    menuNextV();assert(item.current==&nodes[0] && item.pagestart==&nodes[0]);
    menuPrevV();assert(item.current==&nodes[39] && item.pagestart==&nodes[39]);
    menuFirstPage();assert(item.current==&nodes[0] && item.pagestart==&nodes[0]);
    /* Multi-row lists move a whole page: the cursor moves `rows` titles and keeps its row on screen.
       A jump past either end stops on the first / last title, the next press wraps, and the page never
       moves past the last title, so a short list keeps all its rows (#48). Vary page size, start and row. */
    for (int carousel=0;carousel<=1;carousel++) {
        theme.coverflow=carousel;
        for (int rows=3;rows<=16;rows++) {
            list.displayedItems=rows;
            for (int n=1;n<=40;n++) for (int first=0;first<n;first++)
                for (int s=first;s<n && s<first+rows;s++) {
                    int bottom=first+rows-1<n-1 ? first+rows-1 : n-1, lastTop=n>rows ? n-rows : 0;
                    int r=n<=rows ? 1 : rows; /* all titles on screen: one title, like the D-pad */
                    int next=s==n-1 ? 0 : (s+r<n ? s+r : n-1);
                    int k=next-s, room=n-1-bottom;
                    int top=s==n-1 ? 0 : first+(k<room ? k : room);
                    checkPage(n,s,first,1,next,top);
                    assert(next>=top && next<top+rows);
                    int prev=s==0 ? n-1 : (s>r ? s-r : 0);
                    k=s-prev;
                    top=s==0 ? lastTop : first-(k<first ? k : first);
                    checkPage(n,s,first,-1,prev,top);
                    assert(prev>=top && prev<top+rows);
                }
            /* A page start that does not hold the cursor restarts the page at the cursor. */
            {
                int bottom=25+rows-1<39 ? 25+rows-1 : 39, next=25+rows<40 ? 25+rows : 39, k=next-25, room=39-bottom;
                checkPage(40,25,0,1,next,25+(k<room ? k : room));
            }
        }
    }
    /* A hidden list (PSBBN Flow, a theme that shows the titles another way) is no page whatever rows a small font
       fits in it, and a visible list of two rows is too short to page by: both take 15 titles. */
    theme.coverflow=0;
    for (int rows=1;rows<=17;rows++) {
        list.displayedItems=rows;list.hidden=1;setup(40,0);
        menuNextPage();assert(item.current==&nodes[15] && item.pagestart==&nodes[15]);
        menuPrevPage();assert(item.current==&nodes[0] && item.pagestart==&nodes[0]);
    }
    list.hidden=0;list.displayedItems=2;setup(40,0);
    menuNextPage();assert(item.current==&nodes[15] && item.pagestart==&nodes[15]);
    menuNextPage();assert(item.current==&nodes[30] && item.pagestart==&nodes[30]);
    menuNextPage();assert(item.current==&nodes[39] && item.pagestart==&nodes[38]);
    menuPrevPage();assert(item.current==&nodes[24] && item.pagestart==&nodes[23]);
    /* After a layout switch (Grid and the flow views leave the page start on the cursor) the list shows a full
       page: the cursor stays on top unless the list ends first; a hidden list is left alone. */
    list.hidden=0;
    for (int rows=1;rows<=17;rows++) {
        list.displayedItems=rows;
        for (int n=1;n<=40;n++) for (int s=0;s<n;s++) {
            setup(n,s,s);menuFillPage();
            int top=s+rows<=n ? s : (n>rows ? n-rows : 0);
            assert(item.current==&nodes[s] && item.pagestart==&nodes[top] && semaDepth==0);
        }
    }
    list.hidden=1;list.displayedItems=17;setup(40,39,39);menuFillPage();assert(item.pagestart==&nodes[39]);
    list.hidden=0;
    /* Repeated R1 then L1 on a 40-title, 10-row list: whole pages, a stop at each end, then a wrap. */
    theme.coverflow=0;list.displayedItems=10;setup(40,3);
    int pages[]={13,23,33,39,0}, back[]={39,29,19,9,0,39};
    for (int target:pages) {menuNextPage();assert(item.current==&nodes[target]);}
    for (int target:back) {menuPrevPage();assert(item.current==&nodes[target]);}
    /* A held key (a repeat, not a new press) stops on the end and does not wrap; the next fresh press wraps.
       Every view: one-title-row carousel, a multi-row list and a hidden list. */
    for (int rows=1;rows<=10;rows++) for (int hidden=0;hidden<=1;hidden++) {
        list.displayedItems=rows;list.hidden=hidden;theme.coverflow=rows==1;
        setup(40,39,rows>1 ? 40-rows : 39);fresh=0;
        menuNextPage();assert(item.current==&nodes[39] && !sounds && !animations);
        setup(40,0);
        menuPrevPage();assert(item.current==&nodes[0] && !sounds && !animations);
        setup(40,30);menuNextPage();assert(item.current==&nodes[rows<3||hidden ? 39 : (30+rows<40 ? 30+rows : 39)]); /* a repeat still moves */
        fresh=1;setup(40,39,rows>1 ? 40-rows : 39);
        menuNextPage();assert(item.current==&nodes[0]);
        setup(40,0);menuPrevPage();assert(item.current==&nodes[39]);
    }
    list.hidden=0;fresh=1;
    /* Empty and one-title libraries stay silent and do not animate. */
    for (int rows=1;rows<=4;rows++) {
        list.displayedItems=rows;setup(0,0);
        menuNextPage();menuPrevPage();
        assert(!item.current && !item.pagestart && !animations && !sounds);
        setup(1,0);menuNextPage();menuPrevPage();
        assert(item.current==&nodes[0] && !animations && !sounds);
    }
    puts("PASS: 15-title jumps, whole-page list jumps, one title when the library fits in a jump, stops then wraps at both ends, D-pad, home/end, animation, empty/single libraries, full page after a layout switch");
}
'''.replace('@FUNCTIONS@', '\n'.join(functions))
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    c, exe = path / 'pages.cpp', path / 'pages.exe'
    c.write_text(harness, encoding='utf-8')
    subprocess.run(['g++', '-std=gnu++17', '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
