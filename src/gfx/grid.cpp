#include "include/opl.h"
#include "include/themes.h"
#include "include/themeint.h"
#include "include/gui.h"
#include "include/renderman.h"
#include "include/textures.h"
#include "include/fntsys.h"
#include "include/texcache.h"
#include "include/ioman.h"
#include "include/menusys.h"
#include "include/iosupport.h"
#include "include/gamesource.h"
#include "include/saveicon.h"
#include "include/pad.h"
#include "include/grid.h"
#include "include/lang.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Grid: the page's covers in rows and columns with the selection framed (multi-view themes, idea by
 * KrahJohlito; docs/MULTI-VIEW-THEMES-PLAN.md step 6). Left/Right step one cover, Up/Down one row
 * (menusys.cpp), L1/R1 one page. The element's x/y place the grid's centre, width/height one cover; rows,
 * columns and spacing come from the theme's first Grid block. Covers come from the COV cache like Coverflow.
 * The look follows RIPPS2's grid (akilluminati47, builds 78-117): whole pages that slide as they turn, the
 * selected cover drawn larger and last inside a soft glow, the others a step back under a vignette, a case
 * the right stick turns over to its back cover, and the devices in a sidebar on the left. */

int gGridLayout = 0;      // Settings > Interface > Coverflow Settings > Grid Layout (grid.h, gridLayoutPreset)
int gGridSidebarOpen = 0; // menusys gives the device sidebar the pad; only set while a Grid page with a sidebar is on screen

#define GRID_SLIDE_SCALE 8 // the page slide lasts Coverflow's Animation Speed x 8/5 (Normal: 320 ms, RIPPS2's)
#define GRID_FLIP_HOLD_S 0.42f
#define GRID_FLIP_TURN_S 0.55f
#define GRID_FLIP_STICK  72 // how far the right stick leans before it turns a case
#define GRID_FLIPS       32
#define GRID_BACK_SLOTS  8  // back covers in memory: the selected case's and the ones left turned
#define GRID_BACK_ELEMS  16 // Grid elements with a back cache (a theme's families, and the next theme while it loads)

// The size of the cell a cover sits in: Grid Layout's preset, or the element's own size.
static void gridCellSize(theme_element_t *elem, int *w, int *h)
{
    *w = gTheme->gridCellW > 0 ? gTheme->gridCellW : elem->width;
    *h = gTheme->gridCellH > 0 ? gTheme->gridCellH : elem->height;
}

// The size a cover takes in a cell: the cell, or, for a cover of another shape (a preset cell, or a square
// app or PS1 cover another family draws on a mixed page), that cover's shape fitted into the cell.
static void gridCoverSize(theme_element_t *elem, theme_element_t *coverElem, int *w, int *h)
{
    gridCellSize(elem, w, h);
    if ((coverElem == elem && gTheme->gridCellW <= 0) || coverElem->width <= 0 || coverElem->height <= 0)
        return;
    if (coverElem->width * *h > *w * coverElem->height)
        *h = coverElem->height * *w / coverElem->width; // wider than the cell
    else
        *w = coverElem->width * *h / coverElem->height;
}

// ---- back covers (COV2), a small cache of their own for each Grid element ------------------------------
static struct
{
    const theme_element_t *elem;
    image_cache_t *cache;
} gridBacks[GRID_BACK_ELEMS];

static image_cache_t *gridBackCache(const theme_element_t *elem)
{
    for (int i = 0; i < GRID_BACK_ELEMS; i++)
        if (gridBacks[i].elem == elem)
            return gridBacks[i].cache;
    return NULL;
}

// A game's back cover when it is in memory (asking for it when not), else NULL.
static GSTEXTURE *gridBackTexture(struct menu_list *menu, struct submenu_list *item, theme_element_t *elem, int priority)
{
    image_cache_t *cache = gridBackCache(elem);
    if (cache == NULL || item->item.isFolder)
        return NULL;
    GSTEXTURE *t = getGameImageTextureEx(cache, thmGetItemSource(menu, item), &item->item, priority);
    return t != NULL && t->Mem != NULL ? t : NULL;
}

// ---- the case flip (RIPPS2 builds 82/97/100): hold the right stick to one side and the selected case turns
// over to its back cover, the way the stick leans. It stays turned when the cursor moves on, until the stick
// turns it home. Only once its back cover is in memory.
static struct
{
    const void *item;
    float pos;  // 0 front .. 1 back
    int target; // where it is turning to
    int side;   // the way it turns, +1 / -1
} gridFlips[GRID_FLIPS];
static int gridFlipNext;
static const void *gridFlipHoldItem;
static int gridFlipHoldSide, gridFlipHoldDone;
static clock_t gridFlipHoldSince, gridFlipLast;

static int gridFlipFind(const void *item, int make)
{
    int i;
    for (i = 0; i < GRID_FLIPS; i++)
        if (gridFlips[i].item == item)
            return i;
    if (!make)
        return -1;
    for (i = 0; i < GRID_FLIPS; i++) // a free slot, else one at rest at the front
        if (gridFlips[i].item == NULL || (gridFlips[i].target == 0 && gridFlips[i].pos <= 0.0f))
            break;
    if (i == GRID_FLIPS)
        i = gridFlipNext++ % GRID_FLIPS;
    gridFlips[i].item = item;
    gridFlips[i].pos = 0.0f;
    gridFlips[i].target = 0;
    gridFlips[i].side = 1;
    return i;
}

// A list item is freed (menusys rebuilds a device's list on rename, hide, delete and refresh): a new item can
// reuse its address, and must not inherit its turn or a half-done hold.
void gridForgetItem(const void *item)
{
    for (int i = 0; i < GRID_FLIPS; i++)
        if (gridFlips[i].item == item)
            memset(&gridFlips[i], 0, sizeof(gridFlips[i]));
    if (gridFlipHoldItem == item)
        gridFlipHoldItem = NULL;
}

// The turn (radians) a case is drawn at: 0 its front, +-pi its back.
static float gridFlipTurn(const void *item)
{
    int k = gridFlipFind(item, 0);
    if (k < 0 || gridFlips[k].pos <= 0.0f)
        return 0.0f;
    float p = gridFlips[k].pos, e = p * p * (3.0f - 2.0f * p);
    return gridFlips[k].side * 3.14159265f * e;
}

// Once a frame: every case eases toward the side it is turning to; the selected one turns over when the
// stick has leaned one way for GRID_FLIP_HOLD_S and its back cover is there.
static void gridFlipUpdate(const void *selected, int backReady)
{
    clock_t now = clock();
    float dt = gridFlipLast != 0 ? (float)(now - gridFlipLast) / (float)CLOCKS_PER_SEC : 0.0f;
    gridFlipLast = now;
    if (dt < 0.0f || dt > 0.25f)
        dt = 0.016f;
    for (int i = 0; i < GRID_FLIPS; i++) {
        if (gridFlips[i].item == NULL)
            continue;
        float goal = (float)gridFlips[i].target, step = dt / GRID_FLIP_TURN_S;
        gridFlips[i].pos += gridFlips[i].pos < goal ? fminf(step, goal - gridFlips[i].pos) : -fminf(step, gridFlips[i].pos - goal);
    }

    int sx = 0, sy = 0;
    padGetRightStick(&sx, &sy);
    int side = sx >= GRID_FLIP_STICK ? 1 : sx <= -GRID_FLIP_STICK ? -1 :
                                                                    0;
    if (side == 0 || selected == NULL || !backReady || gGridSidebarOpen) {
        gridFlipHoldItem = NULL;
        return;
    }
    if (gridFlipHoldItem != selected || gridFlipHoldSide != side) {
        gridFlipHoldItem = selected;
        gridFlipHoldSide = side;
        gridFlipHoldSince = now;
        gridFlipHoldDone = 0;
        return;
    }
    if (!gridFlipHoldDone && (float)(now - gridFlipHoldSince) / (float)CLOCKS_PER_SEC >= GRID_FLIP_HOLD_S) {
        int k = gridFlipFind(selected, 1);
        gridFlips[k].target = !gridFlips[k].target;
        // Both turns go the way the stick pushes: going home pos runs back to 0, so it takes the other sign.
        gridFlips[k].side = gridFlips[k].target ? side : -side;
        gridFlipHoldDone = 1; // let go and lean again to turn it back
    }
}

// ---- the page slide (RIPPS2 build 82): turning to another page slides the old one away and the new one in
static const void *gridSlideMenu; // the device page the state below belongs to
static int gridShownPage = -1;    // the first cover of the page on screen
static int gridSlideFrom = -1;    // the first cover of the page sliding away, -1 when still
static int gridSlideDir, gridSlideFrames;
static clock_t gridSlideStart;

// 0 the old page in place .. 1 the new one; follows Coverflow's Animation Speed (Off: no slide).
static float gridSlideUpdate(const void *menu, int page)
{
    if (gridSlideMenu != menu) {
        gridSlideMenu = menu;
        gridShownPage = page;
        gridSlideFrom = -1;
    }
    if (page != gridShownPage) {
        if (gCoverflowAnimSpeed > 0) {
            gridSlideFrom = gridShownPage;
            gridSlideDir = page > gridShownPage ? 1 : -1;
            gridSlideStart = clock();
            gridSlideFrames = 0;
        }
        gridShownPage = page;
    }
    if (gridSlideFrom < 0)
        return 1.0f;
    float dur = (float)gCoverflowAnimSpeed * GRID_SLIDE_SCALE / 5 * (CLOCKS_PER_SEC / 1000.0f);
    clock_t el = clock() - gridSlideStart;
    float t = el < 0 || dur <= 0.0f ? 1.0f : (float)el / dur;
    if (t >= 1.0f && gridSlideFrames == 0)
        t = 0.5f; // at least one frame of the slide after a hitch at its start
    gridSlideFrames++;
    if (t >= 1.0f) {
        gridSlideFrom = -1;
        return 1.0f;
    }
    return gridSlideEase(t);
}

// ---- drawing ------------------------------------------------------------------------------------------

// The vignette on the covers not selected (RIPPS2 build 91): their edges darken inwards over a third of the
// cover, the corners twice, over the whole a shade down, so the selected cover, drawn without it, seems lit.
// w is the width as drawn; strength 1 weak, 2 full.
static void drawVignette(int cx, int cy, int w, int h, int strength)
{
    float f = strength >= 2 ? 1.0f : 0.5f;
    float x0 = cx - w / 2.0f - 1.0f, y0 = cy - h / 2.0f - 1.0f, x1 = x0 + w + 2.0f, y1 = y0 + h + 2.0f;
    float fx = w * 0.34f, fy = h * 0.30f;
    u64 dark = GS_SETREG_RGBA(0x00, 0x00, 0x00, (int)(0x58 * f)), clear = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x00);
    rmDrawRect((int)x0, (int)y0, w + 2, h + 2, GS_SETREG_RGBA(0x00, 0x00, 0x00, (int)(0x14 * f)));

    // four bands, each two triangles: (ax0, ay0)-(ax1, ay1) with the dark side given by dx/dy
    const float bands[4][4] = {{x0, y0, x1, y0 + fy}, {x0, y1 - fy, x1, y1}, {x0, y0, x0 + fx, y1}, {x1 - fx, y0, x1, y1}};
    float xy[4 * 12];
    u64 col[4 * 6];
    for (int b = 0; b < 4; b++) {
        float ax = bands[b][0], ay = bands[b][1], bx = bands[b][2], by = bands[b][3];
        // corner colours: top-left, top-right, bottom-left, bottom-right
        u64 tl = clear, tr = clear, bl = clear, br = clear;
        if (b == 0)
            tl = tr = dark;
        else if (b == 1)
            bl = br = dark;
        else if (b == 2)
            tl = bl = dark;
        else
            tr = br = dark;
        float *p = &xy[b * 12];
        u64 *c = &col[b * 6];
        p[0] = ax, p[1] = ay, p[2] = bx, p[3] = ay, p[4] = ax, p[5] = by;
        c[0] = tl, c[1] = tr, c[2] = bl;
        p[6] = bx, p[7] = ay, p[8] = bx, p[9] = by, p[10] = ax, p[11] = by;
        c[3] = tr, c[4] = br, c[5] = bl;
    }
    rmDrawBlendedTriangles(NULL, 8, xy, NULL, col);
}

// A cover with no art, when the theme asks for it (<block>_coverless=name or serial; RIPPS2 build 79/92): a
// clear glass tile with the game's name, or the serial its case prints (the name when it has none).
static void drawCoverlessTile(struct menu_list *menu, struct submenu_list *item, theme_element_t *elem, int x, int y, int w, int h, int selected)
{
    int tw = rmWideScale(w);
    rmDrawRect(x - tw / 2, y - h / 2, tw, h, GS_SETREG_RGBA(0x04, 0x0C, 0x2E, selected ? 0x60 : 0x48));
    u64 edge = GS_SETREG_RGBA(0x80, 0xA0, 0xFF, selected ? 0x30 : 0x18);
    rmDrawRect(x - tw / 2, y - h / 2, tw, 1, edge);
    rmDrawRect(x - tw / 2, y + h / 2 - 1, tw, 1, edge);
    rmDrawRect(x - tw / 2, y - h / 2, 1, h, edge);
    rmDrawRect(x + tw / 2 - 1, y - h / 2, 1, h, edge);

    char serial[16];
    const char *text = item->item.text;
    item_list_t *src = (item_list_t *)thmGetItemSource(menu, item);
    if (gTheme->gridCoverless == 2 && src != NULL && !item->item.isFolder) {
        const char *startup = gsOf(src)->startup(src, item->item.id);
        if (saveIconSerialForStartup(startup, serial, sizeof(serial)))
            text = serial;
    }
    if (text != NULL && tw > 16)
        fntRenderStringFit(elem->font, x, y, ALIGN_CENTER, tw - 10, text,
                           selected ? gTheme->selTextColor : gTheme->textColor, selected);
}

// One cover. turn: the case flip (0 its front; past pi/2 its back, drawn plain); w/h the cover's size before
// the turn narrows it.
static void drawCell(struct menu_list *menu, struct submenu_list *item, theme_element_t *elem, int x, int y, u64 color, int scale,
                     float turn, GSTEXTURE *back, int vignette)
{
    theme_element_t *coverElem = thmGetElemForItem(menu, item, elem);
    mutable_image_t *image = (mutable_image_t *)coverElem->extended;
    if (image == NULL || isHiddenArtworkCache(image->cache))
        return;
    int w, h;
    gridCoverSize(elem, coverElem, &w, &h);
    w = w * scale / 100;
    h = h * scale / 100;
    float squash = fabsf(cosf(turn));
    int tw = (int)(w * squash + 0.5f);
    if (tw < 1)
        tw = 1;

    if (back != NULL && fabsf(turn) > 1.5708f) { // turned over: its back cover, the art alone
        rmDrawPixmap(back, x, y, ALIGN_CENTER, tw, h, SCALING_RATIO, color, 0);
        return;
    }
    GSTEXTURE *texture = getGameImageTexture(image->cache, thmGetItemSource(menu, item), &item->item);
    if (texture == NULL || texture->Mem == NULL) {
        if (gTheme->gridCoverless) {
            drawCoverlessTile(menu, item, elem, x, y, tw, h, color == gDefaultCol);
            return;
        }
        texture = image->defaultTexture ? &image->defaultTexture->source : thmGetTexture(COVER_DEFAULT);
    }
    if (texture == NULL || texture->Mem == NULL)
        return;

    // Frame insets scale from the element that owns the overlay to the size drawn here.
    if (image->overlayTexture) {
        image_texture_t *ov = image->overlayTexture;
        int cw = coverElem->width > 0 ? coverElem->width : 1;
        int ch = coverElem->height > 0 ? coverElem->height : 1;
        rmDrawOverlayPixmap(&ov->source, x, y, ALIGN_CENTER, tw, h, SCALING_RATIO, color,
                            texture, ov->upperLeft_x * tw / cw, ov->upperLeft_y * h / ch,
                            ov->upperRight_x * tw / cw, ov->upperRight_y * h / ch,
                            ov->lowerLeft_x * tw / cw, ov->lowerLeft_y * h / ch,
                            ov->lowerRight_x * tw / cw, ov->lowerRight_y * h / ch, 0);
        if (image->overlayTexture2)
            rmDrawPixmap(&image->overlayTexture2->source, x, y, ALIGN_CENTER, tw, h, SCALING_RATIO, color, 0);
    } else {
        rmDrawPixmap(texture, x, y, ALIGN_CENTER, tw, h, SCALING_RATIO, color, 0);
    }
    if (vignette)
        drawVignette(x, y, rmWideScale(tw), h, vignette);
}

// The selection's frame, after RIPPS2's (build 79): three soft rings in the selected-text colour, wider and
// fainter outwards, breathing over 2.4 s, round a crisp 2-pixel frame. w/h are the cover as drawn.
static void drawFrame(int x, int y, int w, int h, u64 color)
{
    float t = (float)(clock() % (CLOCKS_PER_SEC * 12)) / (float)CLOCKS_PER_SEC;
    float breath = 0.5f + 0.5f * sinf(t * 2.0f * 3.14159265f / 2.4f);
    int r = color & 0xFF, g = (color >> 8) & 0xFF, b = (color >> 16) & 0xFF;
    int left = x - w / 2, top = y - h / 2, right = left + w, bottom = top + h;

    for (int i = 3; i >= 1; i--) {
        int grow = 2 + i * 3 + (int)(breath * 3.0f), t2 = i + 1;
        u64 ring = GS_SETREG_RGBA(r, g, b, (int)((0x10 + 0x10 * breath) / i));
        int x0 = left - grow, y0 = top - grow, x1 = right + grow, y1 = bottom + grow;
        rmDrawRect(x0, y0, x1 - x0, t2, ring);
        rmDrawRect(x0, y1 - t2, x1 - x0, t2, ring);
        rmDrawRect(x0, y0 + t2, t2, y1 - y0 - 2 * t2, ring);
        rmDrawRect(x1 - t2, y0 + t2, t2, y1 - y0 - 2 * t2, ring);
    }
    u64 frame = GS_SETREG_RGBA(r, g, b, 0x60 + (int)(0x20 * breath));
    rmDrawRect(left - 2, top - 2, w + 4, 2, frame);
    rmDrawRect(left - 2, bottom, w + 4, 2, frame);
    rmDrawRect(left - 2, top, 2, h, frame);
    rmDrawRect(right, top, 2, h, frame);
}

// ---- the device sidebar (RIPPS2 build 113's category sidebar, by akilluminati47): the device pages as glyphs
// in a slim bar on the grid's left, the one on screen lit. Left from the grid's first column opens it into a
// drawer with the devices' names; Up/Down there switch device, Right, Cross or Circle go back to the covers.
// The glyphs are RIPPS2's atlas (its elf/theme/make_sidebar_icons.py): 32 px cells, row 0 the lines, row 1
// their glow.
extern unsigned char grid_devices_png[];
extern unsigned int size_grid_devices_png;
#define GRID_GLYPH_CELL  32.0f
#define GRID_GLYPH_ALL   3
#define GRID_GLYPH_USB   4
#define GRID_GLYPH_MMCE  5
#define GRID_GLYPH_HDD   6
#define GRID_GLYPH_NET   7
#define GRID_GLYPH_APPS  8
#define GRID_GLYPH_STAR  9
#define GRID_BAR_W       30  // the bar, in theme pixels (narrower on 16:9, like everything drawn there)
#define GRID_DRAWER_W    190 // the bar opened into a drawer
#define GRID_GLYPH_SIZE  20
#define GRID_GLYPH_PITCH 34

static GSTEXTURE gridGlyphs;
static int gridGlyphsTried;

static GSTEXTURE *gridGlyphTexture(void)
{
    if (!gridGlyphsTried) {
        gridGlyphsTried = 1;
        memset(&gridGlyphs, 0, sizeof(gridGlyphs));
        if (texLoadFromMemory(&gridGlyphs, grid_devices_png, size_grid_devices_png) < 0)
            memset(&gridGlyphs, 0, sizeof(gridGlyphs));
    }
    return gridGlyphs.Mem != NULL ? &gridGlyphs : NULL;
}

static int gridGlyphFor(struct menu_list *m)
{
    item_list_t *support = (item_list_t *)m->item->userdata;
    if (support == NULL)
        return GRID_GLYPH_USB;
    switch (support->mode) {
        case FAV_MODE:
            return gAllGames ? GRID_GLYPH_ALL : GRID_GLYPH_STAR;
        case APP_MODE:
            return GRID_GLYPH_APPS;
        case HDD_MODE:
            return GRID_GLYPH_HDD;
        case MMCE_MODE:
            return GRID_GLYPH_MMCE;
        case ETH_MODE:
        case UDPFS_MODE:
        case HTTP_MODE:
            return GRID_GLYPH_NET;
        default:
            break;
    }
    switch (m->item->icon_id) { // a BDM page by the driver it mounted
        case HDD_BD_ICON:
            return GRID_GLYPH_HDD;
        case MX4SIO_ICON:
        case MMCE_ICON:
            return GRID_GLYPH_MMCE;
        case UDP_ICON:
        case UDPFS_ICON:
            return GRID_GLYPH_NET;
        default:
            return GRID_GLYPH_USB;
    }
}

static void drawGlyph(GSTEXTURE *tex, int cell, int row, int cx, int cy, int size, u64 color)
{
    float w = (float)rmWideScale(size), x0 = cx - w / 2.0f, y0 = cy - size / 2.0f, x1 = x0 + w, y1 = y0 + size;
    float u0 = cell * GRID_GLYPH_CELL + 0.5f, u1 = (cell + 1) * GRID_GLYPH_CELL - 0.5f;
    float v0 = row * GRID_GLYPH_CELL + 0.5f, v1 = (row + 1) * GRID_GLYPH_CELL - 0.5f;
    float xy[12] = {x0, y0, x1, y0, x0, y1, x1, y0, x1, y1, x0, y1};
    float uv[12] = {u0, v0, u1, v0, u0, v1, u1, v0, u1, v1, u0, v1};
    u64 col[6] = {color, color, color, color, color, color};
    rmDrawBlendedTriangles(tex, 2, xy, uv, col);
}

static void drawDeviceSidebar(struct menu_list *menu, int centerY)
{
    struct menu_list *head = menu, *m;
    while (head->prev != NULL)
        head = head->prev;
    int count = 0, current = 0;
    for (m = head; m != NULL; m = m->next)
        if (menuItemShown(m->item)) {
            if (m == menu)
                current = count;
            count++;
        }
    if (count == 0)
        return;

    int open = gGridSidebarOpen;
    u64 sel = gTheme->selTextColor;
    int sr = sel & 0xFF, sg = (sel >> 8) & 0xFF, sb = (sel >> 16) & 0xFF;
    int barW = rmWideScale(open ? GRID_DRAWER_W : GRID_BAR_W), screenH = gTheme->usedHeight > 0 ? gTheme->usedHeight : 480;
    rmDrawRect(0, 0, barW, screenH, GS_SETREG_RGBA(0x01, 0x04, 0x0F, open ? 0x70 : 0x50));
    rmDrawRect(barW, 0, open ? 2 : 1, screenH, GS_SETREG_RGBA(sr, sg, sb, open ? 0x40 : 0x20));

    GSTEXTURE *tex = gridGlyphTexture();
    int glyphX = rmWideScale(GRID_BAR_W) / 2, top = centerY - (count - 1) * GRID_GLYPH_PITCH / 2, i = 0;
    for (m = head; m != NULL; m = m->next) {
        if (!menuItemShown(m->item))
            continue;
        int y = top + i * GRID_GLYPH_PITCH, lit = (i == current);
        if (open && lit) // the drawer's cursor, across the row
            rmDrawRect(rmWideScale(3), y - GRID_GLYPH_PITCH / 2 + 3, barW - rmWideScale(6), GRID_GLYPH_PITCH - 6, GS_SETREG_RGBA(sr, sg, sb, 0x50));
        else if (lit) // the page on screen: an accent at the bar's edge
            rmDrawRect(0, y - GRID_GLYPH_SIZE / 2, 3, GRID_GLYPH_SIZE, GS_SETREG_RGBA(sr, sg, sb, 0x70));
        int glyph = gridGlyphFor(m);
        if (tex != NULL) {
            if (lit)
                drawGlyph(tex, glyph, 1, glyphX, y, GRID_GLYPH_SIZE * 13 / 10, GS_SETREG_RGBA(0x24, 0x58, 0x80, 0x40));
            drawGlyph(tex, glyph, 0, glyphX, y, GRID_GLYPH_SIZE, lit ? GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80) : GS_SETREG_RGBA(0x55, 0x5E, 0x76, 0x60));
        }
        if (open) {
            const char *name = m->item->text_id >= 0 ? _l(m->item->text_id) : m->item->text;
            int x = rmWideScale(GRID_BAR_W + 4);
            if (name != NULL)
                fntRenderStringFit(gTheme->fonts[0], x, y, ALIGN_VCENTER, barW - x - rmWideScale(6), name,
                                   lit ? gTheme->selTextColor : gTheme->textColor, 0);
        }
        i++;
    }
}

static void drawGrid(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    (void)config;
    if (elem->extended == NULL || menu == NULL || menu->item == NULL || elem->width <= 0 || elem->height <= 0)
        return;
    if (item == NULL) { // an empty device page: no covers, but the sidebar may hold the pad and must show
        if (gTheme->gridDevices && gTheme->grid != NULL)
            drawDeviceSidebar(menu, elem->posY);
        return;
    }

    int count = 0, index = 0;
    for (struct submenu_list *row = menu->item->submenu; row != NULL; row = row->next) {
        if (row == item)
            index = count;
        count++;
    }
    int rows = gTheme->gridRows, columns = gTheme->gridColumns, spacing = gTheme->gridSpacing, perPage = rows * columns;
    int first = gridTopRow(count, index, rows, columns) * columns;
    int sidebar = gTheme->gridDevices && gTheme->grid != NULL;
    int focused = !(sidebar && gGridSidebarOpen); // the covers have the pad
    int vignette = gCoverflowDimCovers ? 2 : gTheme->gridVignette;

    // The selected cover first, at priority, so its neighbours cannot crowd it out of the queue.
    mutable_image_t *selected = (mutable_image_t *)thmGetElemForItem(menu, item, elem)->extended;
    GSTEXTURE *selFront = selected ? getGameImageTextureEx(selected->cache, thmGetItemSource(menu, item), &item->item, 1) : NULL;
    // Its back cover once its front is there; the flip turns it over on a hold of the right stick.
    GSTEXTURE *selBack = NULL;
    if (gTheme->gridFlip && selFront != NULL && selFront->Mem != NULL)
        selBack = gridBackTexture(menu, item, elem, 1);
    gridFlipUpdate(item->item.isFolder ? NULL : item, selBack != NULL);

    // Cell pitch on screen: covers draw at SCALING_RATIO, so in widescreen they are narrower.
    int cellW, cellH;
    gridCellSize(elem, &cellW, &cellH);
    int drawnW = rmWideScale(cellW), stepX = drawnW + spacing, stepY = cellH + spacing;
    int x0 = elem->posX - (columns * stepX - spacing) / 2 + drawnW / 2;
    int y0 = elem->posY - (rows * stepY - spacing) / 2 + cellH / 2;

    // Whole pages; turning to another one slides the old page away and the new one in.
    float slide = gridSlideUpdate(menu, first);
    int slideW = columns * stepX + 2 * spacing; // a page and a gap: the old page clears as the new arrives
    int shift = gridSlideFrom >= 0 ? (int)(gridSlideDir * slideW * (1.0f - slide)) : 0;

    u64 dim = GS_SETREG_RGBA(vignette ? 0x78 : 0x70, vignette ? 0x78 : 0x70, vignette ? 0x78 : 0x70, 0x80); // the others a step back
    if (gridSlideFrom >= 0) {                                                                               // the old page on its way out
        struct submenu_list *old = menu->item->submenu;
        for (int i = 0; i < gridSlideFrom && old != NULL; i++)
            old = old->next;
        int oshift = shift - gridSlideDir * slideW;
        for (int slot = 0; slot < perPage && old != NULL; slot++, old = old->next) {
            float turn = gridFlipTurn(old);
            drawCell(menu, old, elem, x0 + (slot % columns) * stepX + oshift, y0 + (slot / columns) * stepY, dim, 100, turn,
                     turn != 0.0f ? gridBackTexture(menu, old, elem, 0) : NULL, vignette);
        }
    }

    struct submenu_list *cell = menu->item->submenu;
    for (int i = 0; i < first && cell != NULL; i++)
        cell = cell->next;
    int selX = 0, selY = 0, shown = 0;
    for (int slot = 0; slot < perPage && cell != NULL; slot++, cell = cell->next) {
        int x = x0 + (slot % columns) * stepX + shift, y = y0 + (slot / columns) * stepY;
        if (cell == item) {
            selX = x;
            selY = y;
            shown = 1;
            continue; // drawn last, larger, over its neighbours
        }
        float turn = gridFlipTurn(cell); // a case left turned keeps its back
        drawCell(menu, cell, elem, x, y, dim, 100, turn, turn != 0.0f ? gridBackTexture(menu, cell, elem, 0) : NULL, vignette);
    }
    if (shown) {
        float turn = gridFlipTurn(item);
        if (selBack == NULL && fabsf(turn) > 1.5708f)
            turn = 0.0f; // turned, but its back is not in memory yet: its front until it is
        int coverW, coverH;
        gridCoverSize(elem, thmGetElemForItem(menu, item, elem), &coverW, &coverH);
        coverW = coverW * GRID_SELECTED_SCALE / 100;
        coverH = coverH * GRID_SELECTED_SCALE / 100;
        int frameW = (int)(coverW * fabsf(cosf(turn)) + 0.5f);
        if (focused)
            drawFrame(selX, selY, rmWideScale(frameW > 1 ? frameW : 1), coverH, gTheme->selTextColor);
        drawCell(menu, item, elem, selX, selY, gDefaultCol, GRID_SELECTED_SCALE, turn, selBack, 0);
    }

    // Under the grid, clear of the selected cover's growth on the bottom row.
    int gridRight = x0 + (columns - 1) * stepX + drawnW / 2;
    int below = y0 + (rows - 1) * stepY + cellH / 2 + cellH * (GRID_SELECTED_SCALE - 100) / 200 + 6;

    // Position in the list, under the grid's lower right corner (<block>_counter=0 hides it).
    int counterWidth = 0;
    if (gTheme->gridCounter) {
        char counter[32];
        snprintf(counter, sizeof(counter), "%d/%d", index + 1, count);
        counterWidth = rmUnScaleX(fntCalcDimensions(elem->font, counter));
        fntRenderString(elem->font, gridRight - counterWidth, below, ALIGN_LEFT, counterWidth, 0, counter, gTheme->textColor);
    }

    // The selected title on a line of its own under the grid (<block>_title=1), as RIPPS2's RIPGRID shows it;
    // centred, and kept clear of the counter.
    if (gTheme->gridTitle && item->item.text != NULL) {
        int room = 2 * (gridRight - elem->posX - (counterWidth > 0 ? counterWidth + 8 : 0));
        int y = gTheme->gridTitleY >= 0 ? gTheme->gridTitleY : below + 12;
        if (room > 0)
            fntRenderStringFit(gTheme->gridTitleFont, elem->posX, y, ALIGN_CENTER, room, item->item.text, gTheme->textColor, 1);
    }

    if (sidebar)
        drawDeviceSidebar(menu, elem->posY);
}

static void endGrid(struct theme_element *elem)
{
    for (int i = 0; i < GRID_BACK_ELEMS; i++)
        if (gridBacks[i].elem == elem) {
            if (gridBacks[i].cache != NULL)
                cacheDestroyCache(gridBacks[i].cache);
            gridBacks[i].elem = NULL;
            gridBacks[i].cache = NULL;
        }
    gridSlideMenu = NULL;
    gridSlideFrom = -1;
    memset(gridFlips, 0, sizeof(gridFlips));
    gridFlipHoldItem = NULL;
    // The theme loads again after a video-mode change, on a new GS context: drop the sidebar glyphs (and their
    // VRAM binding) so the next draw uploads them afresh instead of reading the old mode's VRAM.
    if (gridGlyphs.Mem != NULL) {
        rmUnloadTexture(&gridGlyphs);
        texFree(&gridGlyphs);
    }
    gridGlyphsTried = 0;
    endMutableImage(elem);
}

static int gridKeyInt(config_set_t *themeConfig, const char *name, const char *suffix, int value)
{
    char key[64];
    snprintf(key, sizeof(key), "%s_%s", name, suffix);
    configGetInt(themeConfig, key, &value);
    return value;
}

void initGrid(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    // The first Grid block sets the shape for the whole theme: navigation needs one column count.
    char key[64];
    int rows = GRID_DEFAULT_ROWS, columns = GRID_DEFAULT_COLUMNS, spacing = GRID_DEFAULT_SPACING;
    if (theme->grid != NULL) {
        rows = theme->gridRows;
        columns = theme->gridColumns;
    } else {
        rows = gridKeyInt(themeConfig, name, "rows", rows);
        columns = gridKeyInt(themeConfig, name, "columns", columns);
        spacing = gridKeyInt(themeConfig, name, "spacing", spacing);
        // Grid Layout: RIPPS2's pages of covers in place of the theme's, a wider twin on a 16:9 picture.
        grid_layout_t preset;
        if (gridLayoutPreset(gGridLayout, theme->layoutWide, &preset)) {
            rows = preset.rows;
            columns = preset.columns;
            spacing = preset.spacing;
            theme->gridCellW = preset.width;
            theme->gridCellH = preset.height;
            theme->layoutDependsOnWide = 1; // the preset changes with the picture
        } else {
            theme->gridCellW = 0;
            theme->gridCellH = 0;
        }
        gridClamp(&rows, &columns);
    }

    // Every visible cell needs its own cover slot, or the covers on screen evict each other on every frame;
    // two pages' worth while one slides out as the next slides in. A theme's <name>_count may ask for more
    // slots, never for fewer than two pages.
    int cells = rows * columns, count = 2 * cells > COVER_CACHE_SLOTS ? 2 * cells : COVER_CACHE_SLOTS;
    snprintf(key, sizeof(key), "%s_count", name);
    if (configGetInt(themeConfig, key, &count) && count < 2 * cells)
        configSetInt(themeConfig, key, 2 * cells);

    mutable_image_t *image = initMutableImage(themePath, themeConfig, theme, name, elem->type, "COV", count, NULL, NULL);
    elem->extended = image;
    elem->endElem = &endGrid;
    if (image == NULL || image->cache == NULL) {
        LOG("THEMES Grid %s: NO cache, elem disabled !!\n", name);
        return;
    }
    elem->drawElem = &drawGrid;

    // The back covers (COV2) for the case flip, a few slots of their own.
    if (gridKeyInt(themeConfig, name, "flip", 1)) {
        for (int i = 0; i < GRID_BACK_ELEMS; i++)
            if (gridBacks[i].elem == NULL) {
                gridBacks[i].elem = elem;
                gridBacks[i].cache = cacheInitCache(theme->gameCacheCount++, "ART", 1, "COV2", GRID_BACK_SLOTS);
                break;
            } else if (i == GRID_BACK_ELEMS - 1)
                LOG("THEMES Grid %s: no back-cover slot left, case flip off for it\n", name);
    }

    if (theme->grid != NULL)
        return;
    theme->grid = elem;
    theme->gridRows = rows;
    theme->gridColumns = columns;
    theme->gridSpacing = spacing < 0 ? 0 : spacing;
    theme->gridCounter = gridKeyInt(themeConfig, name, "counter", 1) != 0;
    theme->gridTitle = gridKeyInt(themeConfig, name, "title", 0) != 0;
    int value = gridKeyInt(themeConfig, name, "title_y", -1);
    theme->gridTitleY = value >= 0 && value <= 480 ? value : -1; // -1: under the grid
    value = gridKeyInt(themeConfig, name, "title_font", 0);
    theme->gridTitleFont = theme->fonts[value >= 0 && value < THM_MAX_FONTS ? value : 0];
    theme->gridDevices = gridKeyInt(themeConfig, name, "devices", 1) != 0;
    value = gridKeyInt(themeConfig, name, "vignette", 1);
    theme->gridVignette = value < 0 ? 0 : value > 2 ? 2 :
                                                      value;
    theme->gridFlip = gridKeyInt(themeConfig, name, "flip", 1) != 0;
    const char *coverless = NULL;
    snprintf(key, sizeof(key), "%s_coverless", name);
    theme->gridCoverless = 0;
    if (configGetStr(themeConfig, key, &coverless) && coverless != NULL)
        theme->gridCoverless = !strcmp(coverless, "serial") ? 2 : !strcmp(coverless, "name") ? 1 :
                                                                                               0;
}
