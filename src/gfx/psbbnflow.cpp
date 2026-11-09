#include "include/opl.h"
#include "include/themes.h"
#include "include/themeint.h"
#include "include/gui.h"
#include "include/renderman.h"
#include "include/textures.h"
#include "include/fntsys.h"
#include "include/lang.h"
#include "include/texcache.h"
#include "include/psbbnflow.h"

#include <stdio.h>

/* <PSBBN Flow>: PSBBN Definitive Project's Game Collection. A rounded square focal cover with a soft
 * white rim, a tail of covers turned away into the distance on its left, and an outgoing cover that
 * swells toward the viewer as it fades. Glide timing adapted from dnunezx's PR #733 (original #977,
 * reworked to the Game Collection in #1023). Rendering uses the native COV cache and current menu
 * rows, including folders and favourites. Split out of themes.cpp like coverflow.cpp. */

constexpr int PSBBN_FLOW_STRIPS = 4; // vertical strips per turned card, so its art foreshortens smoothly
constexpr int PSBBN_FLOW_MAX_TRIS = 2 * PSBBN_FLOW_ROUND_POINTS;

static float psbbnXY[PSBBN_FLOW_MAX_TRIS * 6], psbbnUV[PSBBN_FLOW_MAX_TRIS * 6];
static u64 psbbnCol[PSBBN_FLOW_MAX_TRIS * 3];

// Glide state: the offset (1000 per row) the covers started from, the frame it started on, and the
// last frame that drew covers in motion (the title hides on such frames).
static int startOffset = 0;
static unsigned int startFrame = 0;
static unsigned int movingFrame = ~0u;

void psbbnFlowTrigger(int dir)
{
    int offset = psbbnFlowOffset(startOffset, (unsigned int)guiFrameId - startFrame) + dir * 1000;
    startOffset = offset > 3000 ? 3000 : offset < -3000 ? -3000 :
                                                          offset;
    startFrame = (unsigned int)guiFrameId;
}

void psbbnFlowReset(void)
{
    startOffset = 0;
    movingFrame = ~0u;
}

int psbbnFlowIsMoving(void)
{
    return movingFrame == (unsigned int)guiFrameId;
}

static int focalSize(const theme_element_t *elem)
{
    return elem->width > 0 && elem->width <= gThmScreenHeight * 6 / 10 ? elem->width : psbbnFlowSize(gThmScreenWidth, gThmScreenHeight);
}

static void vertex(int n, float x, float y, float u, float v, u64 color)
{
    psbbnXY[n * 2] = x;
    psbbnXY[n * 2 + 1] = y;
    psbbnUV[n * 2] = u;
    psbbnUV[n * 2 + 1] = v;
    psbbnCol[n] = color;
}

// Soft band between two rounded outlines, fading from the inner colour to the outer one.
static void drawRing(const float *inner, const float *outer, u64 innerColor, u64 outerColor)
{
    int n = 0;
    for (int i = 0; i < PSBBN_FLOW_ROUND_POINTS; i++) {
        int j = (i + 1) % PSBBN_FLOW_ROUND_POINTS;
        vertex(n++, inner[i * 2], inner[i * 2 + 1], 0.0f, 0.0f, innerColor);
        vertex(n++, inner[j * 2], inner[j * 2 + 1], 0.0f, 0.0f, innerColor);
        vertex(n++, outer[i * 2], outer[i * 2 + 1], 0.0f, 0.0f, outerColor);
        vertex(n++, outer[i * 2], outer[i * 2 + 1], 0.0f, 0.0f, outerColor);
        vertex(n++, inner[j * 2], inner[j * 2 + 1], 0.0f, 0.0f, innerColor);
        vertex(n++, outer[j * 2], outer[j * 2 + 1], 0.0f, 0.0f, outerColor);
    }
    rmDrawBlendedTriangles(NULL, n / 3, psbbnXY, NULL, psbbnCol);
}

// A flat cover: a rounded square fan. The settled selection also gets PSBBN's soft white rim.
static void drawTile(GSTEXTURE *texture, const psbbn_flow_card_t *card, const float *uv, u64 color, int rim)
{
    float x0 = card->xl, x1 = card->xr, y0 = card->yc - card->hr / 2.0f, y1 = y0 + card->hr;
    float aspect = (x1 - x0) / card->hr; // keeps the corners round in widescreen
    float ry = card->hr * 0.055f, rx = ry * aspect;
    float edge[PSBBN_FLOW_ROUND_POINTS * 2], band[PSBBN_FLOW_ROUND_POINTS * 2];
    psbbnFlowRoundRect(x0, y0, x1, y1, rx, ry, edge);
    if (rim) {
        psbbnFlowRoundRect(x0 - 7.0f * aspect, y0 - 7.0f, x1 + 7.0f * aspect, y1 + 7.0f, rx + 7.0f * aspect, ry + 7.0f, band);
        drawRing(edge, band, GS_SETREG_RGBA(0xd8, 0xe8, 0xff, 0x48), GS_SETREG_RGBA(0x80, 0xb0, 0xff, 0x00));
    }

    float du = (uv[2] - uv[0]) / (x1 - x0), dv = (uv[3] - uv[1]) / (y1 - y0);
    float cx = (x0 + x1) / 2.0f, cy = (y0 + y1) / 2.0f;
    int n = 0;
    for (int i = 0; i < PSBBN_FLOW_ROUND_POINTS; i++) {
        int j = (i + 1) % PSBBN_FLOW_ROUND_POINTS;
        vertex(n++, cx, cy, uv[0] + (cx - x0) * du, uv[1] + (cy - y0) * dv, color);
        vertex(n++, edge[i * 2], edge[i * 2 + 1], uv[0] + (edge[i * 2] - x0) * du, uv[1] + (edge[i * 2 + 1] - y0) * dv, color);
        vertex(n++, edge[j * 2], edge[j * 2 + 1], uv[0] + (edge[j * 2] - x0) * du, uv[1] + (edge[j * 2 + 1] - y0) * dv, color);
    }
    rmDrawBlendedTriangles(texture, n / 3, psbbnXY, psbbnUV, psbbnCol);

    if (rim) {
        float ix = 5.0f * aspect, iy = 5.0f;
        psbbnFlowRoundRect(x0 + ix, y0 + iy, x1 - ix, y1 - iy, rx > ix ? rx - ix : 0.5f, ry > iy ? ry - iy : 0.5f, band);
        drawRing(edge, band, GS_SETREG_RGBA(0xff, 0xff, 0xff, 0x50), GS_SETREG_RGBA(0xff, 0xff, 0xff, 0x00));
    }
}

// A tail cover turned away from the viewer: strips with perspective-correct art, far edge darker.
static void drawCard(GSTEXTURE *texture, const psbbn_flow_card_t *card, const float *uv)
{
    float px = 0.0f, pTop = 0.0f, pBottom = 0.0f, pu = 0.0f;
    u64 pColor = 0;
    int n = 0;
    for (int i = 0; i <= PSBBN_FLOW_STRIPS; i++) {
        float s = (float)i / PSBBN_FLOW_STRIPS;
        float x = card->xl + (card->xr - card->xl) * s;
        float h = card->hl + (card->hr - card->hl) * s;
        float t = s * card->hr / ((1.0f - s) * card->hl + s * card->hr);
        float u = uv[0] + (uv[2] - uv[0]) * t;
        int shade = card->shade * (82 + 18 * i / PSBBN_FLOW_STRIPS) / 100;
        u64 color = GS_SETREG_RGBA(shade, shade, shade, card->alpha);
        float top = card->yc - h / 2.0f, bottom = card->yc + h / 2.0f;
        if (i > 0) {
            vertex(n++, px, pTop, pu, uv[1], pColor);
            vertex(n++, x, top, u, uv[1], color);
            vertex(n++, px, pBottom, pu, uv[3], pColor);
            vertex(n++, px, pBottom, pu, uv[3], pColor);
            vertex(n++, x, top, u, uv[1], color);
            vertex(n++, x, bottom, u, uv[3], color);
        }
        px = x;
        pTop = top;
        pBottom = bottom;
        pu = u;
        pColor = color;
    }
    rmDrawBlendedTriangles(texture, n / 3, psbbnXY, psbbnUV, psbbnCol);
}

static void drawCover(struct menu_list *menu, struct submenu_list *item, theme_element_t *elem, int position, int rim)
{
    int sizeY = focalSize(elem), sizeX = rmWideScale(sizeY);
    psbbn_flow_card_t card = psbbnFlowCard(position, sizeX, sizeY, elem->posX, elem->posY);
    if (card.alpha <= 0 || card.hl <= 0 || card.hr <= 0 || card.xr <= card.xl || card.xr < 0 || card.xl > gThmScreenWidth)
        return;

    theme_element_t *coverElem = thmGetElemForItem(menu, item, elem);
    mutable_image_t *image = (mutable_image_t *)coverElem->extended;
    if (image == NULL || isHiddenArtworkCache(image->cache))
        return;
    GSTEXTURE *texture = getGameImageTexture(image->cache, thmGetItemSource(menu, item), &item->item);
    if (texture == NULL || texture->Mem == NULL)
        texture = image->defaultTexture ? &image->defaultTexture->source : thmGetTexture(COVER_DEFAULT);
    if (texture == NULL || texture->Mem == NULL || texture->Width <= 0 || texture->Height <= 0)
        return;

    u64 color = GS_SETREG_RGBA(card.shade, card.shade, card.shade, card.alpha);
    if (image->overlayTexture) {
        // Framed covers keep their frame: drawn flat at the card's size, without the perspective.
        image_texture_t *ov = image->overlayTexture;
        int w = (card.xr - card.xl) * 1000 / rmWideScale(1000), h = card.hl > card.hr ? card.hl : card.hr;
        int x = (card.xl + card.xr) / 2, y = card.yc;
        int cw = coverElem->width > 0 ? coverElem->width : 1;
        int ch = coverElem->height > 0 ? coverElem->height : 1;
        rmDrawOverlayPixmap(&ov->source, x, y, ALIGN_CENTER, w, h, SCALING_RATIO, color,
                            texture, ov->upperLeft_x * w / cw, ov->upperLeft_y * h / ch,
                            ov->upperRight_x * w / cw, ov->upperRight_y * h / ch,
                            ov->lowerLeft_x * w / cw, ov->lowerLeft_y * h / ch,
                            ov->lowerRight_x * w / cw, ov->lowerRight_y * h / ch, 0);
        if (image->overlayTexture2)
            rmDrawPixmap(&image->overlayTexture2->source, x, y, ALIGN_CENTER, w, h, SCALING_RATIO, color, 0);
        return;
    }

    float uv[4];
    psbbnFlowCrop(texture->Width, texture->Height, uv); // PSBBN shows square tiles
    if (position <= 0)
        drawTile(texture, &card, uv, color, rim);
    else
        drawCard(texture, &card, uv);
}

// A page's name, as menuItemGetText gives it (read here so the theme layer does not call up into menusys).
static const char *pageName(const menu_item_t *page)
{
    return page->text_id >= 0 ? _l(page->text_id) : page->text;
}

static struct menu_list *visiblePage(struct menu_list *page, int forward)
{
    do
        page = forward ? page->next : page->prev;
    while (page != NULL && !menuItemShown(page->item));
    return page;
}

// The PSBBN top-menu frame around the collection: black header and hint bands with their rules,
// a still star field over the nebula, the stacked Collection panel outline, and the page list
// where PSBBN lists Collection and Disc (this page marked, Up/Down neighbours dimmed).
static void drawFrame(struct menu_list *menu, theme_element_t *elem)
{
    int footer = gThmScreenHeight - (480 - PSBBN_FLOW_FOOTER_Y);
    u64 black = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80), rule = GS_SETREG_RGBA(0x70, 0x78, 0x88, 0x50);
    rmDrawRect(0, 0, gThmScreenWidth, PSBBN_FLOW_HEADER_Y, black);
    rmDrawRect(0, footer, gThmScreenWidth, gThmScreenHeight - footer, black);
    rmDrawLine(PSBBN_FLOW_RULE_X0, PSBBN_FLOW_HEADER_Y, PSBBN_FLOW_RULE_X1, PSBBN_FLOW_HEADER_Y, rule);
    rmDrawLine(PSBBN_FLOW_RULE_X0, footer, PSBBN_FLOW_RULE_X1, footer, rule);

    unsigned int seed = 0x2545f491u;
    for (int i = 0; i < 36; i++) {
        seed = seed * 1103515245u + 12345u;
        int x = PSBBN_FLOW_RULE_X0 + (seed >> 8) % (PSBBN_FLOW_RULE_X1 - PSBBN_FLOW_RULE_X0);
        seed = seed * 1103515245u + 12345u;
        int y = PSBBN_FLOW_HEADER_Y + 4 + (seed >> 8) % (footer - PSBBN_FLOW_HEADER_Y - 8);
        rmDrawRect(x, y, 1, 1, GS_SETREG_RGBA(0xc8, 0xd8, 0xff, 0x20 + (seed >> 4) % 0x40));
    }

    u64 outline = GS_SETREG_RGBA(0x60, 0x88, 0xd0, 0x1c);
    for (int j = 3; j >= 0; j--) {
        int x0 = 100 + j * 8, x1 = 282 + j * 8, y0 = 172 + j * 9, y1 = footer - 6;
        rmDrawLine(x0, y0, x1, y0, outline);
        rmDrawLine(x1, y0, x1, y1, outline);
    }
    rmDrawLine(100, 172, 100, footer - 6, outline);

    if (menu->item == NULL)
        return;
    u64 bright = gTheme->textColor, dim = (gTheme->textColor & ~(0xffULL << 24)) | (0x40ULL << 24);
    struct menu_list *above = visiblePage(menu, 0);
    if (above != NULL)
        fntRenderString(elem->font, 116, 182, ALIGN_VCENTER, 0, 0, pageName(above->item), dim);
    rmDrawRect(103, 218, 4, 4, GS_SETREG_RGBA(0x58, 0x98, 0xff, 0x80));
    fntRenderString(elem->font, 116, 220, ALIGN_VCENTER, 0, 0, pageName(menu->item), bright);
    struct menu_list *below = menu;
    for (int row = 1; row <= 2 && (below = visiblePage(below, 1)) != NULL; row++)
        fntRenderString(elem->font, 116, 220 + row * 38, ALIGN_VCENTER, 0, 0, pageName(below->item), dim);
}

static void drawPsbbnFlow(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    (void)config;
    int flow = psbbnFlowOffset(startOffset, (unsigned int)guiFrameId - startFrame);
    if (elem->extended == NULL || menu == NULL || menu->item == NULL)
        return;

    drawFrame(menu, elem);
    int countRows = 0, selectedRow = 0;
    for (struct submenu_list *row = menu->item->submenu; row != NULL; row = row->next) {
        countRows++;
        if (row == item)
            selectedRow = countRows;
    }
    if (countRows < 2) {
        flow = 0;
        startOffset = 0;
    }
    if (flow != 0)
        movingFrame = (unsigned int)guiFrameId;
    if (item == NULL)
        return;

    // Admit the focal cover first. The cache owns async I/O and texture lifetimes.
    mutable_image_t *selected = (mutable_image_t *)thmGetElemForItem(menu, item, elem)->extended;
    if (selected)
        getGameImageTextureEx(selected->cache, thmGetItemSource(menu, item), &item->item, 1);

    // One card past the visible tail fades in or out as the row moves.
    struct submenu_list *tail[PSBBN_FLOW_TAIL + 2] = {item};
    struct submenu_list *previous = item->prev ? item->prev : menu->item->last;
    int count = 1, previousInTail = previous == item;
    for (int i = 1; i <= PSBBN_FLOW_TAIL + 1; i++) {
        struct submenu_list *next = tail[i - 1]->next ? tail[i - 1]->next : menu->item->submenu;
        if (next == NULL || next == item || next == tail[i - 1])
            break;
        tail[count++] = next;
        if (next == previous)
            previousInTail = 1;
    }
    // Far tail first; the focal cover and outgoing foreground must stay on top.
    for (int i = count - 1; i >= 0; i--) {
        if (flow > 0 && i > 0 && tail[i] == previous)
            continue;
        drawCover(menu, tail[i], elem, i * 1000 + flow, i == 0 && flow == 0);
    }
    if (previous != NULL && previous != item && (!previousInTail || flow > 0))
        drawCover(menu, previous, elem, -1000 + flow, 0);

    if (flow == 0) {
        // The count sits under the focal cover's lower right corner, as in PSBBN. Drawn unclipped (width 0):
        // a clip box exactly as wide as the text drops the last glyph whenever the width loses a pixel through
        // rmUnScaleX/rmScaleX (overscan) or a weighted glyph is wider than its advance: the "1/" of #287.
        int size = focalSize(elem), half = rmWideScale(size) / 2;
        char counter[32];
        snprintf(counter, sizeof(counter), "%d/%d", selectedRow, countRows);
        int counterWidth = rmUnScaleX(fntCalcDimensions(elem->font, counter));
        fntRenderString(elem->font, elem->posX + half - 4 - counterWidth, elem->posY + size / 2 + 6,
                        ALIGN_LEFT, 0, 0, counter, gTheme->textColor);
    }
}

void initPsbbnFlow(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    initCoverflow(themePath, themeConfig, theme, elem, name);
    if (elem->drawElem) {
        elem->drawElem = &drawPsbbnFlow;
        if (theme->psbbnFlow == NULL)
            theme->psbbnFlow = elem;
    }
}
