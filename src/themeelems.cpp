#include "include/host.h"
#include "include/opl.h"
#include "include/themes.h"
#include "include/themeint.h"
#include "include/themecatalog.h"
#include "include/themeint.h"
#include "include/util.h"
#include "include/gui.h"
#include "include/renderman.h"
#include "include/textures.h"
#include "include/ioman.h"
#include "include/fntsys.h"
#include "include/lang.h"
#include "include/pad.h"
#include "include/sound.h"
#include "include/texcache.h"
#include "include/favsupport.h"
#include "include/vcdsupport.h" // vcdDisplayName -- display-only VCD game-ID prefix hide
#include "include/libview.h"    // libViewActive / libListViewActive -- which list this page shows
#ifdef RETROACHIEVEMENTS
#include "include/rabadge.h" // RA: mark over the cover
#endif

#include <time.h>
#include <math.h>

// Theme elements: per-device filters, text and image elements, the basic/internal elements (menu, items list, hints) and their
// draw functions. Split out of themes.cpp; the loader and the theme catalog stay there.

// Per-device element filter (theme key devices=usb,hdd,... on MenuIcon/ItemsList/HintText) /////////////////////////////////
//
// Themer-facing device vocabulary, keyed to the SAME icon identity the MenuIcon renders
// (menu->item->icon_id): BDM pages re-resolve it per detected driver at mount time (opl.c's
// updateMenuFromGameList re-reads itemIconId), so a filter can never disagree with the visible
// icon, and one 'usb' filter naturally covers every USB slot. 'bdm' names the generic
// pre-mount/manual-start BDM page -- whose legacy TEXTURE name is literally "usb" (textures.c),
// which is why this table must never be derived from texture names. 'vcd' is deliberately
// absent: the L3 VCD view is an element FAMILY (vcdMain*), not a device; device filters keep
// working inside the vcd family, where icon_id remains the device's.
static const struct
{
    const char *name;
    int iconId;
} thmDeviceVocab[] = {
    {"usb", USB_ICON},
    {"ilink", ILINK_ICON},
    {"mx4sio", MX4SIO_ICON},
    {"hdd_bd", HDD_BD_ICON}, // internal exFAT (GPT/MBR) HDD via BDM
    {"hdd", HDD_ICON},       // internal APA/PFS HDD
    {"eth", ETH_ICON},
    {"smb", ETH_ICON}, // alias: same page/icon as eth
    {"mmce", MMCE_ICON},
    {"udpbd", UDP_ICON},
    {"udpfs", UDPFS_ICON},
    {"app", APP_ICON},
    {"fav", FAV_ICON},
    {"bdm", BDM_ICON}, // generic/unidentified BDM page (pre-mount or manual-start)
};
#define THM_DEVICE_VOCAB_COUNT ((int)(sizeof(thmDeviceVocab) / sizeof(thmDeviceVocab[0])))

// Parse a comma-separated devices= value into a vocab-index bitmask. Unknown names LOG and are
// ignored (quiet suppresses that -- addGUIElem pre-checks the same value initBasic then parses
// for real, and the warning should print once); an empty/unparseable value yields 0 = unfiltered,
// so a typo degrades to the pre-existing shared-element behavior instead of hiding the element
// everywhere. strtok_r, NOT strtok: theme loads run on the IO worker too (device NeedsUpdate ->
// thmAddElements) and could interleave with a GUI-thread strtok (e.g. neutrinoArgsParse).
int thmParseDeviceList(const char *value, int quiet)
{
    int mask = 0;
    char *buf;
    char *tok;
    char *saveptr;

    if (value == NULL)
        return 0;

    buf = static_cast<char*>(malloc(strlen(value) + 1));
    if (buf == NULL)
        return 0;
    strcpy(buf, value);

    for (tok = strtok_r(buf, ", \t", &saveptr); tok != NULL; tok = strtok_r(NULL, ", \t", &saveptr)) {
        int i, hit = 0;
        for (i = 0; i < THM_DEVICE_VOCAB_COUNT; i++) {
            if (strcasecmp(tok, thmDeviceVocab[i].name) == 0) {
                mask |= (1 << i);
                hit = 1;
                break;
            }
        }
        if (!hit && !quiet)
            LOG("THEMES devices=: unknown device name '%s' ignored\n", tok);
    }
    free(buf);
    return mask;
}

// Does a vocab-index bitmask cover the given page icon? Matching is by icon id, so aliases
// (smb/eth) and any future same-icon entries are free.
static int thmDeviceMaskMatches(int mask, int iconId)
{
    int i;
    if (mask == 0)
        return 0;
    for (i = 0; i < THM_DEVICE_VOCAB_COUNT; i++) {
        if ((mask & (1 << i)) && thmDeviceVocab[i].iconId == iconId)
            return 1;
    }
    return 0;
}

// Shared draw gate for MenuIcon/ItemsList/HintText: a FILTERED element draws only on its devices;
// an UNFILTERED element skips the devices a filtered same-type sibling covers (deviceCoverage,
// precomputed at theme load). Both fields zero -- every theme without the devices= key -- makes
// this a no-op, preserving byte-identical behavior.
static int thmElemSkipsDevice(const theme_element_t *elem, int iconId)
{
    if (elem->deviceFilter)
        return !thmDeviceMaskMatches(elem->deviceFilter, iconId);
    if (elem->deviceCoverage)
        return thmDeviceMaskMatches(elem->deviceCoverage, iconId);
    return 0;
}

// Nav-side twin of drawItemsList's gate: pick the FILTERED ItemsList that covers this page, else
// the family's slot element (fallback). menusys assigns gTheme->itemsList through this so paging
// math (displayedItems) always reads the exact element whose rows are on screen.
theme_element_t *thmResolveItemsList(theme_elems_t *family, theme_element_t *fallback, int iconId)
{
    theme_element_t *elem;

    if (family != NULL) {
        for (elem = family->first; elem != NULL; elem = elem->next) {
            if (elem->type == ELEM_TYPE_ITEMS_LIST && elem->deviceFilter && thmDeviceMaskMatches(elem->deviceFilter, iconId))
                return elem;
        }
    }
    return fallback;
}

theme_element_t *thmFamilyItemsList(theme_elems_t *family, theme_element_t *fallback, int iconId)
{
    theme_element_t *elem;

    if (family == NULL)
        return fallback;
    elem = thmResolveItemsList(family, NULL, iconId);
    if (elem != NULL)
        return elem;
    // No filtered list covers this page: navigate with the unfiltered list the theme DECLARED in this
    // family (or the default one validation added). An inherited copy is today's list, and today's page
    // navigated with the slot, so it keeps doing so through the fallback.
    for (elem = family->first; elem != NULL; elem = elem->next) {
        if (elem->type == ELEM_TYPE_ITEMS_LIST && !elem->deviceFilter && !elem->inherited)
            return elem;
    }
    return fallback;
}

// Precompute deviceCoverage per family: an UNFILTERED MenuIcon/ItemsList/HintText yields to
// filtered same-type siblings on the devices they cover (the themer's per-device override wins
// there; the unfiltered element stays the everywhere-else default). Runs once at theme load so the
// per-frame gate never walks the family. Families with no devices= keys leave everything 0.
void thmComputeDeviceCoverage(theme_elems_t *elems)
{
    static const int filteredTypes[] = {ELEM_TYPE_MENU_ICON, ELEM_TYPE_ITEMS_LIST, ELEM_TYPE_HINT_TEXT};
    unsigned int t;

    for (t = 0; t < sizeof(filteredTypes) / sizeof(filteredTypes[0]); t++) {
        int covered = 0;
        theme_element_t *elem;

        for (elem = elems->first; elem != NULL; elem = elem->next) {
            if (elem->type == filteredTypes[t] && elem->deviceFilter)
                covered |= elem->deviceFilter;
        }
        if (!covered)
            continue;
        for (elem = elems->first; elem != NULL; elem = elem->next) {
            if (elem->type == filteredTypes[t] && !elem->deviceFilter)
                elem->deviceCoverage = covered;
        }
    }
}

// Common functions for Text ////////////////////////////////////////////////////////////////////////////////////////////////

static void endMutableText(theme_element_t *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    if (mutableText) {
        if (mutableText->value)
            free(mutableText->value);

        if (mutableText->alias)
            free(mutableText->alias);

        if (mutableText->wrappedValue)
            free(mutableText->wrappedValue);

        free(mutableText);
    }

    free(elem);
}

static mutable_text_t *initMutableText(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, struct theme_element *elem, const char *value, const char *alias, int displayMode, int sizingMode)
{
    mutable_text_t *mutableText = (mutable_text_t *)malloc(sizeof(mutable_text_t));
    mutableText->currentConfigId = 0;
    mutableText->currentValue = NULL;
    mutableText->wrappedValue = NULL;
    mutableText->alias = NULL;

    char elemProp[64];

    snprintf(elemProp, sizeof(elemProp), "%s_display", name);
    configGetInt(themeConfig, elemProp, &displayMode);
    mutableText->displayMode = displayMode;

    int length = strlen(value) + 1;
    mutableText->value = (char *)malloc(length * sizeof(char));
    memcpy(mutableText->value, value, length);

    snprintf(elemProp, sizeof(elemProp), "%s_wrap", name);
    if (configGetInt(themeConfig, elemProp, &sizingMode)) {
        if (sizingMode > 0)
            sizingMode = SIZING_WRAP;
    }

    if ((elem->width != DIM_UNDEF) || (elem->height != DIM_UNDEF)) {
        if (sizingMode == SIZING_NONE)
            sizingMode = SIZING_CLIP;

        if (elem->width == DIM_UNDEF)
            elem->width = gThmScreenWidth;

        if (elem->height == DIM_UNDEF)
            elem->height = gThmScreenHeight;
    } else
        sizingMode = SIZING_NONE;
    mutableText->sizingMode = sizingMode;

    if (type == ELEM_TYPE_ATTRIBUTE_TEXT) {
        snprintf(elemProp, sizeof(elemProp), "%s_title", name);
        configGetStr(themeConfig, elemProp, &alias);
        if (!alias) {
            if (value[0] == '#')
                alias = &value[1];
            else
                alias = value;
        }

        char *temp;
        if (!strncmp(alias, "Title", 5))
            temp = _l(_STR_INFO_TITLE);
        else if (!strncmp(alias, "Genre", 5))
            temp = _l(_STR_INFO_GENRE);
        else if (!strncmp(alias, "Release", 7))
            temp = _l(_STR_INFO_RELEASE);
        else if (!strncmp(alias, "Developer", 9))
            temp = _l(_STR_INFO_DEVELOPER);
        else if (!strncmp(alias, "Size", 4))
            temp = _l(_STR_SIZE);
        else if (!strncmp(alias, "Description", 11))
            temp = _l(_STR_INFO_DESCRIPTION);
        else
            temp = (char *)alias;

        length = strlen(temp) + 1 + 2;
        mutableText->alias = (char *)calloc(length, sizeof(char));
        if (mutableText->sizingMode == SIZING_WRAP)
            snprintf(mutableText->alias, length, "%s:\n", temp);
        else
            snprintf(mutableText->alias, length, "%s: ", temp);
    } else {
        if (mutableText->sizingMode == SIZING_WRAP)
            fntFitString(elem->font, mutableText->value, elem->width);
    }

    return mutableText;
}

// Collision-aware text ////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Long text used to draw straight over whatever sat next to it: a List title ran across the cover, an
// attribute value across the next label. At theme load each text element gets a room -- how far it may
// run on its row before it reaches another element of the SAME screen or the screen edge -- and text
// longer than that scrolls inside it (fntRenderStringFit), like a settings value does (#732).
//
// Backdrops are not obstacles: an element the text STARTS inside (a banner or panel the theme draws the
// text on) is ignored. Only what the text would run INTO counts.

#define THM_TEXT_GAP      4   // keep this much space before the neighbour
constexpr int THM_TEXT_MIN_ROOM = 40;  // a smaller room is more likely a misread layout than a real one: keep the old draw
constexpr int THM_COVER_W = 140;  // an image with no declared size and no default image: OPL's cover art size
#define THM_COVER_H       200

typedef struct
{
    int x0, y0, x1, y1;
} thm_box_t;

// Room for text starting at sx on the row band [ry0, ry1): rightward to the nearest box or the screen
// edge; centred text (it grows both ways) gets twice the nearer side. Boxes the text starts inside are
// backdrops and ignored.
static int thmTextRoom(int sx, int ry0, int ry1, int centred, const thm_box_t *boxes, int count, int screenW)
{
    int right = screenW - sx, left = sx, i;

    for (i = 0; i < count; i++) {
        const thm_box_t *b = &boxes[i];
        if (b->y1 <= ry0 || b->y0 >= ry1)
            continue; // not on this row
        if (b->x0 <= sx && sx < b->x1)
            continue; // the text starts inside it: a backdrop
        if (b->x0 > sx) {
            if (b->x0 - sx < right)
                right = b->x0 - sx;
        } else if (sx - b->x1 < left) {
            left = sx - b->x1;
        }
    }
    right -= THM_TEXT_GAP;
    left -= THM_TEXT_GAP;
    if (centred)
        return 2 * (left < right ? left : right);
    return right;
}

static int thmIsTextElem(int type)
{
    return type == ELEM_TYPE_ATTRIBUTE_TEXT || type == ELEM_TYPE_STATIC_TEXT || type == ELEM_TYPE_ITEM_TEXT ||
           type == ELEM_TYPE_GAME_COUNT_TEXT;
}

// The row a single line of text occupies (fntRenderString's vertical placement, one MENU_ITEM_HEIGHT line).
static void thmTextRow(const theme_element_t *elem, int *y0, int *y1)
{
    *y0 = (elem->aligned & ALIGN_VCENTER) ? elem->posY - MENU_ITEM_HEIGHT / 2 : elem->posY;
    *y1 = *y0 + MENU_ITEM_HEIGHT;
}

// An element's box as an obstacle. Text and lists are their start point on their row (their far edge
// depends on what they show). Returns 0 for elements with no knowable box.
static int thmElemBox(const theme_element_t *elem, thm_box_t *box)
{
    int w = elem->width, h = elem->height;

    if (thmIsTextElem(elem->type) || elem->type == ELEM_TYPE_MENU_TEXT || elem->type == ELEM_TYPE_HINT_TEXT ||
        elem->type == ELEM_TYPE_INFO_HINT_TEXT) {
        box->x0 = box->x1 = elem->posX;
        thmTextRow(elem, &box->y0, &box->y1);
        return 1;
    }
    if (elem->type == ELEM_TYPE_ITEMS_LIST) {
        box->x0 = box->x1 = elem->posX;
        box->y0 = elem->posY;
        box->y1 = elem->posY + elem->height;
        return 1;
    }
    if (elem->type != ELEM_TYPE_ATTRIBUTE_IMAGE && elem->type != ELEM_TYPE_GAME_IMAGE && elem->type != ELEM_TYPE_STATIC_IMAGE &&
        elem->type != ELEM_TYPE_MENU_ICON && elem->type != ELEM_TYPE_BDM_INDEX && elem->type != ELEM_TYPE_COVERFLOW)
        return 0; // background, loading icon: never obstacles

    if (w == DIM_UNDEF || h == DIM_UNDEF) {
        const mutable_image_t *image = NULL;
        if (elem->type == ELEM_TYPE_ATTRIBUTE_IMAGE || elem->type == ELEM_TYPE_GAME_IMAGE || elem->type == ELEM_TYPE_STATIC_IMAGE)
            image = (const mutable_image_t *)elem->extended;
        if (image != NULL && image->defaultTexture != NULL && image->defaultTexture->source.Width > 0) {
            if (w == DIM_UNDEF)
                w = image->defaultTexture->source.Width;
            if (h == DIM_UNDEF)
                h = image->defaultTexture->source.Height;
        } else if (elem->type == ELEM_TYPE_GAME_IMAGE) {
            if (w == DIM_UNDEF)
                w = THM_COVER_W;
            if (h == DIM_UNDEF)
                h = THM_COVER_H;
        } else {
            return 0;
        }
    }
    box->x0 = (elem->aligned & ALIGN_HCENTER) ? elem->posX - w / 2 : elem->posX;
    box->y0 = (elem->aligned & ALIGN_VCENTER) ? elem->posY - h / 2 : elem->posY;
    box->x1 = box->x0 + w;
    box->y1 = box->y0 + h;
    return 1;
}

void thmComputeTextRooms(theme_elems_t *elems)
{
    thm_box_t boxes[64];
    theme_element_t *elem, *other;

    for (elem = elems->first; elem != NULL; elem = elem->next) {
        int sx, ry0, ry1, count = 0, room;

        elem->textRoom = 0;
        if (elem->type == ELEM_TYPE_ITEMS_LIST) {
            const items_list_t *list = (const items_list_t *)elem->extended;
            sx = elem->posX + ((list != NULL && list->decoratorImage != NULL) ? DECORATOR_SIZE : 0);
            ry0 = elem->posY;
            ry1 = elem->posY + elem->height;
        } else if (thmIsTextElem(elem->type)) {
            // Word-wrapped text already fits its box by wrapping.
            if (elem->type != ELEM_TYPE_ITEM_TEXT && elem->extended != NULL &&
                ((const mutable_text_t *)elem->extended)->sizingMode == SIZING_WRAP)
                continue;
            sx = elem->posX;
            thmTextRow(elem, &ry0, &ry1);
        } else {
            continue;
        }

        for (other = elems->first; other != NULL && count < (int)(sizeof(boxes) / sizeof(boxes[0])); other = other->next) {
            if (other == elem)
                continue;
            // A devices=-filtered MenuIcon/ItemsList/HintText is drawn on ONE page only (the types
            // thmElemSkipsDevice gates; any other type with devices= is drawn everywhere, so it stays
            // an obstacle). Counting it on every page boxed the built-in Coverflow title into 196 units
            // because of the Favorites icon (zackcage6, Beta-3335).
            if (other->deviceFilter && (other->type == ELEM_TYPE_MENU_ICON || other->type == ELEM_TYPE_ITEMS_LIST ||
                                        other->type == ELEM_TYPE_HINT_TEXT))
                continue;
            if (thmElemBox(other, &boxes[count]))
                count++;
        }

        room = thmTextRoom(sx, ry0, ry1, (elem->aligned & ALIGN_HCENTER) != 0, boxes, count, gThmScreenWidth);
        // A width the theme declared still bounds the text; it now scrolls there instead of being cut.
        if (elem->width != DIM_UNDEF && elem->width > 0 && elem->width < room)
            room = elem->width;
        elem->textRoom = (room >= THM_TEXT_MIN_ROOM) ? room : 0;
    }
}

// Draw one line of an element's text: inside its room (scrolling when longer) once one is known,
// otherwise exactly as before. Returns the end x.
static int thmDrawElemText(theme_element_t *elem, int sizingMode, const char *text, int scroll)
{
    if (elem->textRoom > 0 && sizingMode != SIZING_WRAP)
        return fntRenderStringFit(elem->font, elem->posX, elem->posY, elem->aligned, elem->textRoom, text, elem->color, scroll);
    if (sizingMode == SIZING_NONE)
        return fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, text, elem->color);
    return fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, text, elem->color);
}

// StaticText ///////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawStaticText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    thmDrawElemText(elem, mutableText->sizingMode, mutableText->value, 1);
}

void initStaticText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    const char *value = NULL; // configGetStr leaves this untouched if the key is absent
    char elemProp[64];

    snprintf(elemProp, sizeof(elemProp), "%s_value", name);
    configGetStr(themeConfig, elemProp, &value);
    if (value) {
        elem->extended = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, elem, value, NULL, DISPLAY_ALWAYS, SIZING_NONE);
        elem->endElem = &endMutableText;
        elem->drawElem = &drawStaticText;
    } else
        LOG("THEMES StaticText %s: NO value, elem disabled !!\n", name);
}

// GameCountText ////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int getGameCount(void *support)
{
    item_list_t *list = (item_list_t *)support;
    return list->itemGetCount(list);
}

static void drawGameCountText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;

    if (config) {
        if (mutableText->currentConfigId != config->uid()) {
            // force refresh
            mutableText->currentConfigId = config->uid();

            int count = getGameCount(menu->item->userdata);
            snprintf(mutableText->value, sizeof(char) * 60, _l(_STR_FILE_COUNT), count);
        }
    }

    thmDrawElemText(elem, SIZING_NONE, mutableText->value, 1);
}

void initGameCountText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    /* drawGameCountText() snprintf()s up to 60 bytes into mutableText->value.
     * initMutableText() sizes value from strlen(seed)+1, so seeding with an
     * empty string previously produced a 1-byte buffer that the 60-byte
     * snprintf overran (and the separate 60-byte countStr was leaked).  Seed
     * with "" then replace value with a correctly sized 60-byte buffer. */
    mutable_text_t *mutableText = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_TEXT, elem, "", NULL, DISPLAY_ALWAYS, SIZING_NONE);
    elem->extended = mutableText;

    if (mutableText != NULL) {
        char *countStr = (char *)malloc(60);
        if (countStr != NULL) {
            countStr[0] = '\0';
            free(mutableText->value);
            mutableText->value = countStr;
        }
    }

    elem->endElem = &endMutableText;
    elem->drawElem = &drawGameCountText;
}

// AttributeText ////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawAttributeText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    // No current item (e.g. the last favourite was removed -> empty list): clear, don't redraw the
    // stale cached value. Mirrors drawGameImage's `if (item)` guard so the description clears (#48).
    if (item == NULL)
        return;
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    if (config) {
        if (mutableText->currentConfigId != config->uid()) {
            // force refresh
            mutableText->currentConfigId = config->uid();
            mutableText->currentValue = NULL;
            if (configGetStr(config, mutableText->value, (const char **)&mutableText->currentValue)) {
                if ((mutableText->sizingMode == SIZING_WRAP) && mutableText->currentValue) {
                    // Word-wrap a private copy, not the config's own stored string.
                    // configGetStr() hands back a pointer straight into the config
                    // buffer (it->val); fntFitString() inserts '\n's in place, so
                    // wrapping currentValue directly would bake the line breaks into
                    // the stored value. Those then get written back to the .cfg on the
                    // next settings save, corrupting the field (issue #44). Copying
                    // first keeps the on-screen wrapping while leaving the value intact.
                    free(mutableText->wrappedValue);
                    mutableText->wrappedValue = NULL;
                    size_t len = strlen(mutableText->currentValue) + 1;
                    char *wrapped = (char *)malloc(len);
                    if (wrapped) {
                        memcpy(wrapped, mutableText->currentValue, len);
                        fntFitString(elem->font, wrapped, elem->width);
                        mutableText->wrappedValue = wrapped;
                        mutableText->currentValue = wrapped;
                    }
                }
            }
        }
        if (mutableText->currentValue) {
            char result[300];
            if (mutableText->displayMode == DISPLAY_NEVER) {
                if (!strncmp(mutableText->alias, _l(_STR_SIZE), strlen(_l(_STR_SIZE)))) {
                    snprintf(result, sizeof(result), "%s MiB", mutableText->currentValue);
                    thmDrawElemText(elem, mutableText->sizingMode, result, 1);
                } else {
                    thmDrawElemText(elem, mutableText->sizingMode, mutableText->currentValue, 1);
                }
            } else {
                if (!strncmp(mutableText->alias, _l(_STR_SIZE), strlen(_l(_STR_SIZE))))
                    snprintf(result, sizeof(result), "%s%s MiB", mutableText->alias, mutableText->currentValue);
                else
                    snprintf(result, sizeof(result), "%s%s", mutableText->alias, mutableText->currentValue);
                thmDrawElemText(elem, mutableText->sizingMode, result, 1);
            }
            return;
        }
    }
    if (mutableText->displayMode == DISPLAY_ALWAYS)
        thmDrawElemText(elem, mutableText->sizingMode, mutableText->alias, 1);
}

void initAttributeText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    const char *attribute = NULL; // configGetStr leaves this untouched if the key is absent
    char elemProp[64];

    snprintf(elemProp, sizeof(elemProp), "%s_attribute", name);
    configGetStr(themeConfig, elemProp, &attribute);
    if (attribute) {
        elem->extended = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_TEXT, elem, attribute, NULL, DISPLAY_ALWAYS, SIZING_NONE);
        elem->endElem = &endMutableText;
        elem->drawElem = &drawAttributeText;
    } else
        LOG("THEMES AttributeText %s: NO attribute, elem disabled !!\n", name);
}

// Common functions for Image ///////////////////////////////////////////////////////////////////////////////////////////////

void findDuplicate(theme_element_t *first, const char *cachePattern, const char *defaultTexture, const char *overlayTexture, const char *overlayTexture2, mutable_image_t *target)
{
    theme_element_t *elem = first;
    while (elem) {
        if ((elem->type == ELEM_TYPE_STATIC_IMAGE) || (elem->type == ELEM_TYPE_ATTRIBUTE_IMAGE) || (elem->type == ELEM_TYPE_GAME_IMAGE) || (elem->type == ELEM_TYPE_BACKGROUND)) {
            mutable_image_t *source = (mutable_image_t *)elem->extended;

            if (cachePattern && source->cache && !strcmp(cachePattern, source->cache->suffix)) {
                target->cache = source->cache;
                target->cacheLinked = 1;
                LOG("THEMES Re-using a cache for pattern %s\n", cachePattern);
            }

            if (defaultTexture && source->defaultTexture && !strcmp(defaultTexture, source->defaultTexture->name)) {
                target->defaultTexture = source->defaultTexture;
                target->defaultTextureLinked = 1;
                LOG("THEMES Re-using the default texture for %s\n", defaultTexture);
            }

            // The overlay (cover-window) texture is intentionally NOT shared by name here. Its
            // per-element cover-window corners (overlay_ulx..lry) are stored ON the image_texture_t,
            // so reusing the object would make a later element inherit an earlier one's corners --
            // e.g. the square apps/VCD coverflow (own corners 0..184/0..256-square) inheriting the
            // games' 184x256 window, which makes the cover IMAGE draw past its frame at the other
            // page's rectangle size. Each element loads its own overlay (initImageTexture, called
            // below in initMutableImage) so its own corners are read. The case bitmap is tiny +
            // paletted; the expensive case_overlay glare (overlay2, no corners, plain pixmap) is
            // still safely shared just below.
            (void)overlayTexture;

            if (overlayTexture2 && source->overlayTexture2 && !strcmp(overlayTexture2, source->overlayTexture2->name)) {
                target->overlayTexture2 = source->overlayTexture2;
                target->overlayTexture2Linked = 1;
                LOG("THEMES Re-using the overlay2 texture for %s\n", overlayTexture2);
            }
        }

        elem = elem->next;
    }
}

static void freeImageTexture(image_texture_t *texture)
{
    if (texture) {
        if (texture->source.Mem) {
            rmUnloadTexture(&texture->source);
            free(texture->source.Mem);
            texture->source.Mem = NULL;
        }
        if (texture->source.Clut) {
            free(texture->source.Clut);
            texture->source.Clut = NULL;
        }
        if (texture->name) {
            free(texture->name);
            texture->name = NULL;
        }
        free(texture);
    }
}

static image_texture_t *initImageTexture(const char *themePath, config_set_t *themeConfig, const char *name, const char *imgName, int isOverlay)
{
    // calloc so omitted overlay coordinate keys default to 0 instead of
    // uninitialized heap garbage that would feed rmDrawOverlayPixmap.
    image_texture_t *texture = (image_texture_t *)calloc(1, sizeof(image_texture_t));
    texture->name = NULL;

    int texId = -1;
    int result = 0;

    // Propagate the actual load result so a missing/corrupt texture takes the
    // free-and-return-NULL path below instead of becoming a live element whose
    // source.Mem is NULL (the empty `if (...) ;` previously discarded it).
    if (themePath) {
        char path[256];
        snprintf(path, sizeof(path), "%s%s", themePath, imgName);
        result = (texDiscoverLoad(&texture->source, path, texId) >= 0);
    } else {
        texId = texLookupInternalTexId(imgName);
        result = (texLoadInternal(&texture->source, texId) >= 0);
    }

    if (result) {
        int length = strlen(imgName) + 1;
        texture->name = (char *)malloc(length * sizeof(char));
        memcpy(texture->name, imgName, length);

        if (isOverlay) {
            int intValue;
            char elemProp[64];
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_ulx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperLeft_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_uly", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperLeft_y = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_urx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperRight_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_ury", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperRight_y = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_llx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerLeft_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_lly", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerLeft_y = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_lrx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerRight_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_lry", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerRight_y = intValue;
        }
    } else {
        freeImageTexture(texture);
        texture = NULL;
    }

    return texture;
}

static image_texture_t *initImageInternalTexture(config_set_t *themeConfig, const char *name)
{
    // calloc so the embedded source GSTEXTURE is zeroed: if texLookupInternalTexId
    // fails below, freeImageTexture() reads texture->source.Mem/.Clut and must not
    // see uninitialized garbage (which it would rmUnloadTexture()/free()).
    image_texture_t *texture = (image_texture_t *)calloc(1, sizeof(image_texture_t));
    texture->name = NULL;
    int result;

    if ((result = texLookupInternalTexId(name)) >= 0) {
        result = texLoadInternal(&texture->source, result);
        int length = strlen(name) + 1;
        texture->name = (char *)malloc(length * sizeof(char));
        memcpy(texture->name, name, length);
    }

    if (result < 0) {
        freeImageTexture(texture);
        texture = NULL;
    }

    return texture;
}

void endMutableImage(struct theme_element *elem)
{
    mutable_image_t *mutableImage = (mutable_image_t *)elem->extended;
    if (mutableImage) {
        if (mutableImage->cache && !mutableImage->cacheLinked)
            cacheDestroyCache(mutableImage->cache);

        if (mutableImage->defaultTexture && !mutableImage->defaultTextureLinked)
            freeImageTexture(mutableImage->defaultTexture);

        if (mutableImage->overlayTexture && !mutableImage->overlayTextureLinked)
            freeImageTexture(mutableImage->overlayTexture);

        if (mutableImage->overlayTexture2 && !mutableImage->overlayTexture2Linked)
            freeImageTexture(mutableImage->overlayTexture2);

        free(mutableImage);
    }

    free(elem);
}

mutable_image_t *initMutableImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, const char *cachePattern, int cacheCount, const char *defaultTexture, const char *overlayTexture)
{
    mutable_image_t *mutableImage = (mutable_image_t *)malloc(sizeof(mutable_image_t));
    mutableImage->currentCacheId = -1; // -1 = the cache API's "no entry" sentinel (see themes.h)
    mutableImage->currentUid = -1;
    mutableImage->currentConfigId = 0;
    mutableImage->currentValue = NULL;
    mutableImage->cache = NULL;
    mutableImage->cacheLinked = 0;
    mutableImage->defaultTexture = NULL;
    mutableImage->defaultTextureLinked = 0;
    mutableImage->overlayTexture = NULL;
    mutableImage->overlayTextureLinked = 0;
    mutableImage->overlayTexture2 = NULL;
    mutableImage->overlayTexture2Linked = 0;
#ifdef RETROACHIEVEMENTS
    mutableImage->raIsCover = 0; // set in initGameImage, which knows the pattern
#endif

    char elemProp[64];

    if (type == ELEM_TYPE_ATTRIBUTE_IMAGE) {
        snprintf(elemProp, sizeof(elemProp), "%s_attribute", name);
        configGetStr(themeConfig, elemProp, &cachePattern);
        LOG("THEMES MutableImage %s: type: %s using cache pattern: %s\n", name, elementsType[type], cachePattern);
    } else if ((type == ELEM_TYPE_GAME_IMAGE) || (type == ELEM_TYPE_BACKGROUND)) {
        snprintf(elemProp, sizeof(elemProp), "%s_pattern", name);
        configGetStr(themeConfig, elemProp, &cachePattern);
        snprintf(elemProp, sizeof(elemProp), "%s_count", name);
        configGetInt(themeConfig, elemProp, &cacheCount);
        // Floor EVERY per-game art cache at 2 slots, not just COV.
        //
        // A one-slot cache has no redundancy at all: the single slot is claimed, and if that one load
        // fails for any transient reason the art type shows nothing until something else evicts it.
        // COV was floored at 2 long ago for exactly this reason -- but BG, SCR and SCR2 are created
        // with count 1 (the Background/GameImage defaults), which is why a tester with a .tar-only
        // setup saw covers appear while backgrounds and screenshots stayed blank: covers had ten
        // slots to absorb a hiccup and the others had one each.
        // Cheap: an image_cache_t slot is a small struct, and the texture itself is only allocated
        // when a load actually succeeds.
        if (cachePattern != NULL && cacheCount < 2)
            cacheCount = 2;
        LOG("THEMES MutableImage %s: type: %s using cache pattern: %s count: %d\n", name, elementsType[type], cachePattern, cacheCount);
    }

    snprintf(elemProp, sizeof(elemProp), "%s_default", name);
    configGetStr(themeConfig, elemProp, &defaultTexture);

    if (type != ELEM_TYPE_BACKGROUND) {
        snprintf(elemProp, sizeof(elemProp), "%s_overlay", name);
        configGetStr(themeConfig, elemProp, &overlayTexture);
    }

    const char *overlayTexture2 = NULL;
    if (type != ELEM_TYPE_BACKGROUND) {
        snprintf(elemProp, sizeof(elemProp), "%s_overlay2", name);
        configGetStr(themeConfig, elemProp, &overlayTexture2);
    }

    findDuplicate(theme->mainElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->infoElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->appsMainElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->appsInfoElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->favsMainElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->favsInfoElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->vcdMainElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->vcdInfoElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->favsVcdMainElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->favsVcdInfoElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->favsAppsMainElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);
    findDuplicate(theme->favsAppsInfoElems.first, cachePattern, defaultTexture, overlayTexture, overlayTexture2, mutableImage);

    if (cachePattern && !mutableImage->cache) {
        if (type == ELEM_TYPE_ATTRIBUTE_IMAGE)
            // Attribute images use a small fixed glyph set, so retain each glyph
            // instead of re-reading it whenever the selected value changes.
            mutableImage->cache = cacheInitCache(-1, themePath, 0, cachePattern, ATTR_IMAGE_CACHE_SLOTS);
        else
            mutableImage->cache = cacheInitCache(theme->gameCacheCount++, "ART", 1, cachePattern, cacheCount);
    }

    if (!themePath)
        if (defaultTexture && !mutableImage->defaultTexture)
            mutableImage->defaultTexture = initImageInternalTexture(themeConfig, defaultTexture);

    if (defaultTexture && !mutableImage->defaultTexture)
        mutableImage->defaultTexture = initImageTexture(themePath, themeConfig, name, defaultTexture, 0);

    if (overlayTexture && !mutableImage->overlayTexture)
        mutableImage->overlayTexture = initImageTexture(themePath, themeConfig, name, overlayTexture, 1);

    // overlay2 is drawn over the cover composite (plain rmDrawPixmap), so it needs no cover-window
    // corners -- load it as a plain texture (isOverlay=0).
    if (overlayTexture2 && !mutableImage->overlayTexture2)
        mutableImage->overlayTexture2 = initImageTexture(themePath, themeConfig, name, overlayTexture2, 0);

    return mutableImage;
}

// StaticImage //////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawStaticImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_image_t *staticImage = (mutable_image_t *)elem->extended;
    if (staticImage->overlayTexture) {
        rmDrawOverlayPixmap(&staticImage->overlayTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol,
                            &staticImage->defaultTexture->source, staticImage->overlayTexture->upperLeft_x, staticImage->overlayTexture->upperLeft_y, staticImage->overlayTexture->upperRight_x, staticImage->overlayTexture->upperRight_y,
                            staticImage->overlayTexture->lowerLeft_x, staticImage->overlayTexture->lowerLeft_y, staticImage->overlayTexture->lowerRight_x, staticImage->overlayTexture->lowerRight_y, 0);
        if (staticImage->overlayTexture2)
            rmDrawPixmap(&staticImage->overlayTexture2->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);
    } else
        rmDrawPixmap(&staticImage->defaultTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);
}

void initStaticImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *imageName)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, NULL, 0, imageName, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->defaultTexture)
        elem->drawElem = &drawStaticImage;
    else
        LOG("THEMES StaticImage %s: NO image name, elem disabled !!\n", name);
}

// GameImage ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// ItemIcon is backed by the ICO art suffix. Treat it as disc artwork independently from COV so
// users can keep covers/backgrounds while hiding the large disc image used by some themes.
static int isDiscArtCache(const image_cache_t *cache)
{
    return cache != NULL && cache->suffix != NULL && strcmp(cache->suffix, "ICO") == 0;
}

GSTEXTURE *getGameImageTextureEx(image_cache_t *cache, void *support, struct submenu_item *item, int isPriority)
{
    // Folder browsing: a folder row has no cover art (and no startup key). Never route it through the
    // cover cache -- an empty key would thrash the cache and paint the empty case frame. Applies to
    // every consumer: the list decorator, the big cover panel and the coverflow carousel.
    if (item == NULL || item->isFolder)
        return NULL;

    if (cache == NULL || cache->userId < 0 || item->cache_id == NULL || item->cache_uid == NULL)
        return NULL;

    if (!gEnableDiscArt && isDiscArtCache(cache))
        return NULL;

    if (gTheme == NULL || cache->userId >= gTheme->gameCacheCount)
        return NULL;

    if (gEnableArt) {
        if (cache->suffix != NULL && strcmp(cache->suffix, "BG") == 0 && !gEnableBGArt)
            return NULL;

        item_list_t *list = (item_list_t *)support;
        if (list == NULL || list->itemGetStartup == NULL)
            return NULL;

        char *startup = list->itemGetStartup(list, item->id);
        if (startup == NULL || startup[0] == '\0')
            return NULL;

        return cacheGetTextureEx(cache, list, &item->cache_id[cache->userId], &item->cache_uid[cache->userId], startup, isPriority);
    }

    return NULL;
}

GSTEXTURE *getGameImageTexture(image_cache_t *cache, void *support, struct submenu_item *item)
{
    return getGameImageTextureEx(cache, support, item, 0);
}

// Draw-what-we-have companion to the above: same eligibility checks, but it only ever returns art
// that is ALREADY loaded -- no slot claim, no queued read. Used by rows that are deliberately not
// requesting this frame so they keep showing their thumbnail instead of flicking to the placeholder.
static GSTEXTURE *getGameImageCached(image_cache_t *cache, struct submenu_item *item)
{
    if (item == NULL || item->isFolder || !gEnableArt)
        return NULL;

    if (cache == NULL || cache->userId < 0 || item->cache_id == NULL || item->cache_uid == NULL)
        return NULL;

    if (!gEnableDiscArt && isDiscArtCache(cache))
        return NULL;

    if (gTheme == NULL || cache->userId >= gTheme->gameCacheCount)
        return NULL;

    return cacheLookupTexture(cache, &item->cache_id[cache->userId], &item->cache_uid[cache->userId]);
}

// Favourites element redirection (defined in the Coverflow section below; used by both draw
// paths so an APP favourite renders with the apps element, not the game cover element).
theme_element_t *thmGetElemForItem(struct menu_list *menu, struct submenu_list *item, theme_element_t *elem);

#ifdef RETROACHIEVEMENTS
/* RA: the mark over the cover of a checked, tracked game. What
   we know about the game does not depend on the picture. The mark image
   carries a dark backing so it stays visible on light covers.

   The rectangle follows the same arithmetic as rmSetupQuad: the element
   may use any alignment, and we need the upper-right corner of what
   was drawn. Sizes come from the texture when the theme leaves them
   undefined, since covers vary in size. */
/* RA: positions along the cover's sides are fractions of RA_MARK_ONE;
   a power of two keeps the division cheap. */
constexpr int RA_MARK_ONE = 1024;

/* A point inside the cover quadrilateral: s is the fraction along the
   top edge, t along the left edge, both out of RA_MARK_ONE. Bilinear
   interpolation over the four corners, the same mapping the renderer
   uses to fit the cover itself (gsKit_prim_quad_texture in
   rmDrawOverlayPixmap). Corner values never exceed the texture size, so
   the products fit an int with room to spare. */
static void raQuadPoint(const image_texture_t *ovl, int s, int t, int *px, int *py)
{
    int topx = ovl->upperLeft_x + ((ovl->upperRight_x - ovl->upperLeft_x) * s) / RA_MARK_ONE;
    int topy = ovl->upperLeft_y + ((ovl->upperRight_y - ovl->upperLeft_y) * s) / RA_MARK_ONE;
    int botx = ovl->lowerLeft_x + ((ovl->lowerRight_x - ovl->lowerLeft_x) * s) / RA_MARK_ONE;
    int boty = ovl->lowerLeft_y + ((ovl->lowerRight_y - ovl->lowerLeft_y) * s) / RA_MARK_ONE;

    *px = topx + ((botx - topx) * t) / RA_MARK_ONE;
    *py = topy + ((boty - topy) * t) / RA_MARK_ONE;
}

/* Whether the overlay corners are sane enough to compute from. All four
   are checked because the lower ones take part in the interpolation. */
static int raQuadUsable(const image_texture_t *ovl, const GSTEXTURE *geom)
{
    return ovl->upperRight_x > 8 && ovl->upperRight_x <= geom->Width &&
           ovl->upperLeft_x >= 0 && ovl->upperLeft_x < ovl->upperRight_x &&
           ovl->upperLeft_y >= 0 && ovl->upperLeft_y < geom->Height &&
           ovl->upperRight_y >= 0 && ovl->upperRight_y < geom->Height &&
           ovl->lowerLeft_x >= 0 && ovl->lowerLeft_x <= geom->Width &&
           ovl->lowerRight_x >= 0 && ovl->lowerRight_x <= geom->Width &&
           ovl->lowerLeft_y > ovl->upperLeft_y && ovl->lowerLeft_y <= geom->Height &&
           ovl->lowerRight_y > ovl->upperRight_y && ovl->lowerRight_y <= geom->Height;
}

static void drawRAMark(struct submenu_list *item, struct theme_element *elem,
                       mutable_image_t *img, GSTEXTURE *cover)
{
    GSTEXTURE *mark, *geom;
    image_texture_t *ovl;
    int w, h, x0, y0, side, margin, right, topy, refw, x, y;

    if (item == NULL || cover == NULL)
        return;

    /* The row already knows. Asking the badge cache from here would mean the render thread
       dereferencing a pointer the I/O thread frees on every list rebuild (raBadgeRefresh). */
    if (!item->item.raBadged)
        return;

    mark = thmGetTexture(RA_MARK);
    if (mark == NULL || mark->Mem == NULL)
        return;

    /* The case overlay, when present, defines the quad size: the
       renderer fits the cover inside it. */
    ovl = img->overlayTexture;
    geom = (ovl != NULL) ? &ovl->source : cover;

    w = (elem->width == DIM_UNDEF) ? geom->Width : elem->width;
    h = (elem->height == DIM_UNDEF) ? geom->Height : elem->height;
    if (w <= 0 || h <= 0)
        return;

    if (elem->scaled & SCALING_RATIO)
        w = rmWideScale(w);

    if (elem->aligned & ALIGN_HCENTER)
        x0 = elem->posX - (w >> 1);
    else if (elem->aligned & ALIGN_RIGHT)
        x0 = elem->posX - w;
    else
        x0 = elem->posX;

    if (elem->aligned & ALIGN_VCENTER)
        y0 = elem->posY - (h >> 1);
    else if (elem->aligned & ALIGN_BOTTOM)
        y0 = elem->posY - h;
    else
        y0 = elem->posY;

    /* Skew. The renderer fits the cover into the quadrilateral given by
       the overlay corners, which is where its slant comes from. The
       mark lives in the cover's own coordinate system: its corners are
       bilinearly interpolated and handed to the renderer as a
       quadrilateral, so it shares the slant instead of looking like a
       sticker.

       Size and margin are FRACTIONS OF THE COVER'S SIDES rather than
       screen pixels: the sides are slanted and their length differs
       from the case width, so pixel arithmetic would be wrong. */
    if (ovl != NULL && raQuadUsable(ovl, geom)) {
        int coverW = ovl->upperRight_x - ovl->upperLeft_x;
        int coverH = ovl->lowerLeft_y - ovl->upperLeft_y;
        int s0, s1, t0, t1;
        int ax, ay, bx, by, cx, cy, dx, dy;

        side = coverW / 3;
        if (side < 22)
            side = 22;
        if (side > 48)
            side = 48;
        if (side > coverH / 2)
            side = coverH / 2;
        if (side < 4)
            return;

        margin = side / 8;
        if (margin < 2)
            margin = 2;

        s1 = RA_MARK_ONE - (margin * RA_MARK_ONE) / coverW;
        s0 = s1 - (side * RA_MARK_ONE) / coverW;
        t0 = (margin * RA_MARK_ONE) / coverH;
        t1 = t0 + (side * RA_MARK_ONE) / coverH;

        /* A broken theme with a tiny case: draw nothing rather than an
           inside-out quadrilateral. */
        if (s0 < 0 || t1 > RA_MARK_ONE)
            return;

        raQuadPoint(ovl, s0, t0, &ax, &ay);
        raQuadPoint(ovl, s1, t0, &bx, &by);
        raQuadPoint(ovl, s0, t1, &cx, &cy);
        raQuadPoint(ovl, s1, t1, &dx, &dy);

        /* The origin is computed HERE, as in the flat path below: x0/y0
           are already derived from the COVER's size. Passing
           elem->width/height would not do: rmSetupQuad resolves
           DIM_UNDEF from the texture it is given, and that texture is
           the mark, so the box would take the mark's size. With centre
           alignment the whole quadrilateral then lands off screen and
           the mark disappears. */
        rmDrawInlayPixmap(mark, x0, y0, ALIGN_NONE, w, h, SCALING_NONE,
                          gDefaultCol, ax, ay, bx, by, cx, cy, dx, dy);
        return;
    }

    /* No overlay: the cover is not slanted, so draw a flat square at its
       upper-right corner. */
    right = w;
    topy = 0;
    refw = w;

    /* A third of the cover width, within limits: on a small cover the
       mark would swallow it, on a large one it would get lost. */
    side = refw / 3;
    if (side < 22)
        side = 22;
    if (side > 48)
        side = 48;
    if (side > h / 2)
        side = h / 2;

    margin = side / 8;
    if (margin < 2)
        margin = 2;

    x = x0 + right - side - margin;
    y = y0 + topy + margin;

    /* Guard against a broken theme: stay on screen. */
    if (x + side > gThmScreenWidth)
        x = gThmScreenWidth - side;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;

    rmDrawPixmap(mark, x, y, ALIGN_NONE, side, side, SCALING_NONE, gDefaultCol, 0);
}
#endif /* RETROACHIEVEMENTS */

void drawGameImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (item) {
        // On the Favourites tab an APP favourite draws with the theme's apps element (its own
        // dimensions, case overlay and art folder); games and every other tab use elem as-is.
        // The texture lookup stays on menu->item->userdata (favList) so favGetImage proxies by
        // the FAV index -- only the element (geometry + cache + overlay) is redirected.
        struct theme_element *drawElem = thmGetElemForItem(menu, item, elem);
        mutable_image_t *gameImage = (mutable_image_t *)drawElem->extended;
        if (gameImage == NULL)
            return;

        // A disabled disc element must disappear completely, including its theme-supplied default
        // texture/overlay. Merely blocking the ICO file read would otherwise leave the placeholder.
        if (!gEnableDiscArt && isDiscArtCache(gameImage->cache))
            return;

        // A per-game BACKGROUND must never be REQUESTED before the cover. This function draws both,
        // and the theme's Background element is the FIRST in the list (painter's order), so on the
        // settle frame its request reached the empty queue first and became the executing head --
        // and the head cannot be jumped by any amount of priority, because the worker is already
        // inside it. A full-screen PNG is ~5x the pixels of a cover, so the cover the user is
        // actually waiting for sat behind the better part of a second of somebody else's scenery.
        // That is the whole gap to official OPL, whose built-in theme has no per-game background on
        // the main page at all (it uses a static image), and it explains why a device with no _BG
        // art in its set feels fine while one with a full art set does not.
        //
        // So hold the background back: request it only once the cover has had its turn -- an extra
        // idle margin beyond the art delay AND an idle art path. Until then draw whatever is already
        // cached, which keeps a background that IS loaded on screen instead of flickering to the
        // default. Nothing is lost but the order.
        int isBackground = (drawElem->type == ELEM_TYPE_BACKGROUND);
        GSTEXTURE *texture;
        int coverflowCoverSettled = 1;

        if (isBackground) {
            // Prioritize the highlighted game's cover before the background so the small cover
            // is read and displayed first before starting the larger background read.
            if (gTheme != NULL && !item->item.isFolder) {
                if (gTheme->itemsList != NULL && gTheme->itemsList->extended != NULL) {
                    items_list_t *itemsList = (items_list_t *)gTheme->itemsList->extended;
                    if (itemsList->coverElem != NULL) {
                        struct theme_element *covElem = thmGetElemForItem(menu, item, itemsList->coverElem);
                        if (covElem != NULL && covElem->extended != NULL) {
                            mutable_image_t *selImg = (mutable_image_t *)covElem->extended;
                            if (selImg != NULL && selImg->cache != NULL)
                                getGameImageTextureEx(selImg->cache, menu->item->userdata, &item->item, 1);
                        }
                    } else if (itemsList->decoratorImage != NULL && itemsList->decoratorImage->cache != NULL) {
                        getGameImageTextureEx(itemsList->decoratorImage->cache, menu->item->userdata, &item->item, 1);
                    }
                } else if (gTheme->coverflow != NULL) {
                    struct theme_element *cfElem = thmGetElemForItem(menu, item, gTheme->coverflow);
                    if (cfElem != NULL && cfElem->extended != NULL) {
                        mutable_image_t *cfImg = (mutable_image_t *)cfElem->extended;
                        if (cfImg != NULL && cfImg->cache != NULL) {
                            GSTEXTURE *coverTexture = getGameImageTextureEx(cfImg->cache, menu->item->userdata, &item->item, 1);

                            // Coverflow draws after the Background element. If its selected cover is still
                            // pending, starting a full-screen BG read here would occupy the art worker before
                            // the carousel gets to enqueue its visible neighbours. Hold only this background
                            // request for that short window. A loaded cover, disabled art, or a confirmed
                            // absent cover (-2) releases the gate immediately.
                            if (gEnableArt && coverTexture == NULL && cfImg->cache->userId >= 0 &&
                                cfImg->cache->userId < gTheme->gameCacheCount && item->item.cache_id != NULL &&
                                item->item.cache_id[cfImg->cache->userId] != -2)
                                coverflowCoverSettled = 0;
                        }
                    }
                }
            }

            // List mode keeps master's immediate background request. Coverflow is different: its
            // Background element is painted before the carousel, so on a cold selection the large BG read
            // could start before the side covers even reach the queue. Draw an already-cached background
            // while the selected cover settles; the same frame's Coverflow draw then gets first claim on
            // visible-neighbour reads. This is admission ordering, not an artificial frame delay.
            if (coverflowCoverSettled)
                texture = getGameImageTexture(gameImage->cache, menu->item->userdata, &item->item);
            else
                texture = getGameImageCached(gameImage->cache, &item->item);
        } else {
            // PRIORITY: the highlighted game's own cover, the one image the user is looking for.
            texture = getGameImageTextureEx(gameImage->cache, menu->item->userdata, &item->item, 1);
        }

        if (!texture || !texture->Mem) {
            // Keep the configured/default cover visible when art is absent. This is also the
            // placeholder path for Favorites, so all list views keep the same no-art affordance.
            if (gameImage->defaultTexture)
                texture = &gameImage->defaultTexture->source;
            else {
                if (elem->type == ELEM_TYPE_BACKGROUND)
                    host::ui().drawBackgroundPlasma();
                return;
            }
        }

        if (gameImage->overlayTexture) {
            rmDrawOverlayPixmap(&gameImage->overlayTexture->source, drawElem->posX, drawElem->posY, drawElem->aligned, drawElem->width, drawElem->height, drawElem->scaled, gDefaultCol,
                                texture, gameImage->overlayTexture->upperLeft_x, gameImage->overlayTexture->upperLeft_y, gameImage->overlayTexture->upperRight_x, gameImage->overlayTexture->upperRight_y,
                                gameImage->overlayTexture->lowerLeft_x, gameImage->overlayTexture->lowerLeft_y, gameImage->overlayTexture->lowerRight_x, gameImage->overlayTexture->lowerRight_y, 0);
            if (gameImage->overlayTexture2)
                rmDrawPixmap(&gameImage->overlayTexture2->source, drawElem->posX, drawElem->posY, drawElem->aligned, drawElem->width, drawElem->height, drawElem->scaled, gDefaultCol, 0);
        } else
            rmDrawPixmap(texture, drawElem->posX, drawElem->posY, drawElem->aligned, drawElem->width, drawElem->height, drawElem->scaled, gDefaultCol, 0);

#ifdef RETROACHIEVEMENTS
        /* The RA mark over the cover of a tracked game. Only elements whose pattern is
           COV (raIsCover); drawn after the cover so it lands on top. */
        if (gameImage->raIsCover)
            drawRAMark(item, drawElem, gameImage, texture);
#endif

    } else if (elem->type == ELEM_TYPE_BACKGROUND) {
        mutable_image_t *gameImage = (mutable_image_t *)elem->extended;
        if (gameImage->defaultTexture)
            rmDrawPixmap(&gameImage->defaultTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);
        else
            host::ui().drawBackgroundPlasma();
    }
}

void initGameImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *pattern, int count, const char *texture, const char *overlay)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, pattern, count, texture, overlay);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

#ifdef RETROACHIEVEMENTS
    mutableImage->raIsCover = (pattern != NULL && !strcmp(pattern, "COV"));
#endif

    if (mutableImage->cache)
        elem->drawElem = &drawGameImage;
    else
        LOG("THEMES GameImage %s: NO pattern, elem disabled !!\n", name);
}

// Coverflow ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Source list used for a submenu item's TEXTURE lookup. Always the active menu's userdata --
// including the Favourites tab: our FAV items carry a FAV-array index (not the source id), and
// favGetImage / favGetItemStartup proxy by that index, so the lookup must stay on favList.
void *thmGetItemSource(struct menu_list *menu, struct submenu_list *item)
{
    return menu->item->userdata;
}

// Find a game-image / coverflow element in a list by its cover-cache suffix (e.g. "COV"). A family
// may hold more than one element on the same suffix -- a carousel or big cover panel alongside a
// list decorator, which is the split splitDecoratorCoverCache exists for. When `like` is given,
// prefer the element DRAWN the same way (the carousel resolves to a carousel, a panel to a panel);
// a Coverflow element is typed GAME_IMAGE like the rest, so the draw function is what separates
// them. Falls back to the first suffix match, which is the historical behaviour.
// declaredOnly skips elements a Favourites per-kind family copied from a fallback block (see
// theme_element_t::inherited) AND drops the first-match fallback: a declared element is only returned
// when it is drawn the same way as `like`, so a declared Coverflow never stands in for an ItemCover panel
// (the caller then keeps today's redirect). 0 keeps the historical behaviour.
static theme_element_t *thmFindElemBySuffixEx(theme_elems_t *elems, const char *suffix, theme_element_t *like, int declaredOnly)
{
    theme_element_t *firstMatch = NULL;

    if (suffix == NULL)
        return NULL;
    for (theme_element_t *e = elems->first; e != NULL; e = e->next) {
        if (e->type != ELEM_TYPE_GAME_IMAGE && e->type != ELEM_TYPE_COVERFLOW)
            continue;
        if (declaredOnly && e->inherited)
            continue;
        mutable_image_t *eimg = (mutable_image_t *)e->extended;
        if (eimg == NULL || eimg->cache == NULL || eimg->cache->suffix == NULL || strcmp(eimg->cache->suffix, suffix) != 0)
            continue;
        if (like != NULL && e->drawElem == like->drawElem)
            return e;
        if (firstMatch == NULL && !declaredOnly)
            firstMatch = e;
    }
    return firstMatch;
}

theme_element_t *thmFindElemBySuffix(theme_elems_t *elems, const char *suffix, theme_element_t *like)
{
    return thmFindElemBySuffixEx(elems, suffix, like, 0);
}

// Family a redirected cover resolves into. Stays on the SAME screen half the source element came
// from: a browse-page cover redirects to another main-family element, an info-page cover to an info
// one. Redirecting across the two would hand the cover the other screen's coordinates.
static int thmElemIsInfo(theme_element_t *elem)
{
    return elem->family == &gTheme->infoElems || elem->family == &gTheme->appsInfoElems ||
           elem->family == &gTheme->favsInfoElems || elem->family == &gTheme->vcdInfoElems ||
           elem->family == &gTheme->favsVcdInfoElems || elem->family == &gTheme->favsAppsInfoElems;
}

static theme_elems_t *thmKindFamily(theme_element_t *elem, int isApp)
{
    int info = thmElemIsInfo(elem);

    if (isApp)
        return info ? &gTheme->appsInfoElems : &gTheme->appsMainElems;
    return info ? &gTheme->vcdInfoElems : &gTheme->vcdMainElems;
}

// The Favourites per-kind family for a PS1 or app row on the same screen half, or NULL when the theme
// does not declare it (the family is empty) -- in which case the caller keeps today's redirect.
static theme_elems_t *thmFavKindFamily(theme_element_t *elem, int isApp)
{
    int info = thmElemIsInfo(elem);
    theme_elems_t *family;

    if (isApp)
        family = info ? &gTheme->favsAppsInfoElems : &gTheme->favsAppsMainElems;
    else
        family = info ? &gTheme->favsVcdInfoElems : &gTheme->favsVcdMainElems;
    return family->first != NULL ? family : NULL;
}

// Element to DRAW a submenu item's COVER with. A page as a whole renders with one family
// (menuRenderMain / menuGetInfoElems switch the element set per mode), which is right for the
// background, the texts and the items list -- they belong to the page, not to any one row. The
// cover does not: a PS2 case is portrait (184x256 on our built-in theme), a PS1 case and an app box
// are square (184x184 / 256x256), and a MIXED page interleaves them -- the Favourites "All" shelf,
// and a device page under PS2/PS1 Game Display = Mixed. Stamping the page family's dimensions on
// every cover stretches whichever kinds are not the page's own (#623, Yuramash: PS1 covers stretched
// on the Favourites mixer).
//
// So resolve the cover element per ROW, by media kind: PS1 rows draw with the vcd family, app/ELF
// rows with the apps family, PS2 disc rows with the page's own element. On a homogeneous page the
// row kind IS the page kind and the lookup lands back on the element it was given, so those pages
// are unchanged. Matching is by cover-cache suffix, so a family that defines no counterpart element
// (or a theme using a different pattern) falls through to the caller's element as before.
//
// Single chokepoint for the selected-cover panel (drawGameImage), the coverflow carousel and the
// items-list cover warming. Only the ELEMENT is redirected -- geometry, case overlay, placeholder
// and cover cache; the texture lookup stays on the page's own list, so art resolution, the
// Favourites index proxying and per-row art gating are all untouched.
theme_element_t *thmGetElemForItem(struct menu_list *menu, struct submenu_list *item, theme_element_t *elem)
{
    if (item == NULL || elem == NULL || item->item.isFolder)
        return elem;
    item_list_t *menuList = (item_list_t *)menu->item->userdata;
    if (menuList == NULL)
        return elem;

    // Favourites also consults the record's SOURCE device: an app favourite stored before the ELF
    // shelf existed still resolves to the apps box, exactly as it did when this hook was apps-only.
    int rowView = libListRowView(menuList, item->item.id);
    int isApp = rowView == LIB_VIEW_ELF || rowView == LIB_VIEW_PS1_ELF ||
                (menuList->mode == FAV_MODE && favGetItemSourceMode(item->item.id) == APP_MODE);
    if (!isApp && rowView != LIB_VIEW_PS1)
        return elem;

    mutable_image_t *img = (mutable_image_t *)elem->extended;

    // Favourites with an opt-in per-kind family (favsVcd* for PS1 rows, favsApps* for app rows): take
    // the cover the theme DECLARED there -- on that family's own page (the Favourites PS1 / ELF view)
    // and on the All shelf alike. Inherited fallback copies are skipped, so an undeclared cover slot
    // lands on today's vcd/apps redirect below instead of e.g. favsMain's portrait case. An inherited
    // slot on the family's own page is NOT moved onto another declared slot of the same family (that
    // element is drawn at its own position already); it takes today's redirect like any other.
    if (menuList->mode == FAV_MODE) {
        theme_elems_t *favFamily = thmFavKindFamily(elem, isApp);
        if (favFamily != NULL) {
            if (elem->family == favFamily) {
                if (!elem->inherited)
                    return elem;
            } else if (img != NULL && img->cache != NULL) {
                theme_element_t *favElem = thmFindElemBySuffixEx(favFamily, img->cache->suffix, elem, 1);
                if (favElem != NULL)
                    return favElem;
            }
        }
    }

    theme_elems_t *family = thmKindFamily(elem, isApp);
    if (elem->family == family)
        return elem; // already this kind's own element (a homogeneous page): nothing to redirect

    if (img == NULL || img->cache == NULL)
        return elem;
    theme_element_t *kindElem = thmFindElemBySuffix(family, img->cache->suffix, elem);
    return (kindElem != NULL) ? kindElem : elem;
}

// AttributeImage ///////////////////////////////////////////////////////////////////////////////////////////////////////////

// Maps an AttributeImage's current value to the embedded internal glyph texId, including the
// "display/asset" suffix-value form ("NTSC/ntsc" with attribute "Vmode" -> "Vmode_ntsc").
// Returns -1 when no embedded glyph exists (e.g. Players, custom attributes); thmGetTexture(-1)
// safely yields NULL (unsigned wrap >= TEXTURES_COUNT).
static int thmAttributeTexId(mutable_image_t *attributeImage)
{
    char *seppos = strchr(attributeImage->currentValue, '/');
    if (!seppos)
        return texLookupInternalTexId(attributeImage->currentValue);

    char imgName[32];
    snprintf(imgName, sizeof(imgName), "%s_%s", attributeImage->cache->suffix, &seppos[1]);
    return texLookupInternalTexId(imgName);
}

void drawAttributeImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    // No current item (empty list): clear, don't redraw the stale cached image -- same guard as
    // drawAttributeText/drawGameImage (#48). Also correct for the per-game #Media icon.
    if (item == NULL)
        return;
    mutable_image_t *attributeImage = (mutable_image_t *)elem->extended;
    if (config) {
        if (attributeImage->currentConfigId != config->uid()) {
            // A -2 cache id is a genuine-absence memo for the old value. Clear
            // both identity fields before switching to the new attribute.
            attributeImage->currentCacheId = -1;
            attributeImage->currentUid = -1;
            attributeImage->currentConfigId = config->uid();
            attributeImage->currentValue = NULL;
            configGetStr(config, attributeImage->cache->suffix, (const char **)&attributeImage->currentValue);
        }
        if (attributeImage->currentValue) {
            if (thmGetGuiValue() == 0 || thmGetGuiValue() == thmDiskThemeCount() + 1) {
                GSTEXTURE *texture = thmGetTexture(thmAttributeTexId(attributeImage));
                if (texture && texture->Mem)
                    rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);

                return;
            } else {
                // Keep the cache identity persistent so a missing optional glyph
                // is memoized instead of being reopened every frame.
                GSTEXTURE *texture = cacheGetTexture(attributeImage->cache, static_cast<item_list_t*>(menu->item->userdata), &attributeImage->currentCacheId, &attributeImage->currentUid, attributeImage->currentValue);
                if (texture && texture->Mem) {
                    if (attributeImage->overlayTexture) {
                        rmDrawOverlayPixmap(&attributeImage->overlayTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol,
                                            texture, attributeImage->overlayTexture->upperLeft_x, attributeImage->overlayTexture->upperLeft_y, attributeImage->overlayTexture->upperRight_x, attributeImage->overlayTexture->upperRight_y,
                                            attributeImage->overlayTexture->lowerLeft_x, attributeImage->overlayTexture->lowerLeft_y, attributeImage->overlayTexture->lowerRight_x, attributeImage->overlayTexture->lowerRight_y, 0);
                        if (attributeImage->overlayTexture2)
                            rmDrawPixmap(&attributeImage->overlayTexture2->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);
                    } else
                        rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);

                    return;
                }
                // Embedded-glyph fallback: BUILT-IN theme only (#213). Attribute caches carry
                // prefix = themePath, so a NULL prefix is exactly the built-in path -- there the
                // baked glyph fills NULL frames while nothing else can. On a DISK theme this
                // fallback made our baked art FLASH before the theme's own glyph finished its async
                // load (and stand in permanently when the theme ships none) -- violating the rule
                // that a custom theme uses its OWN assets entirely. Disk themes now fall through to
                // the element's own _default (or draw nothing). thmLoad also no longer decodes the
                // baked set for disk themes, so this gate is double-covered.
                if (attributeImage->cache->prefix == NULL) {
                    texture = thmGetTexture(thmAttributeTexId(attributeImage));
                    if (texture && texture->Mem) {
                        rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);
                        return;
                    }
                }
            }
        }
    }
    if (attributeImage->defaultTexture)
        rmDrawPixmap(&attributeImage->defaultTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);
}

void initAttributeImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, NULL, 1, NULL, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->cache)
        elem->drawElem = &drawAttributeImage;
    else
        LOG("THEMES AttributeImage %s: NO attribute, elem disabled !!\n", name);
}

// BasicElement /////////////////////////////////////////////////////////////////////////////////////////////////////////////

void endBasic(theme_element_t *elem)
{
    if (elem->extended)
        free(elem->extended);

    free(elem);
}

theme_element_t *initBasic(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, int x, int y, short aligned, int w, int h, short scaled, u64 color, int font)
{
    int intValue;
    unsigned char charColor[3];
    const char *temp;
    char elemProp[64];

    theme_element_t *elem = (theme_element_t *)malloc(sizeof(theme_element_t));

    elem->type = type;
    elem->reflection = 0;
    elem->reflectionOffset = 0;
    elem->showRun = 1;
    elem->deviceFilter = 0;   // malloc'd without memset: MUST be zeroed explicitly (0 = unfiltered)
    elem->deviceCoverage = 0; // filled at validateGUIElems for unfiltered MenuIcon/ItemsList/HintText
    elem->inherited = 0;      // set by the Favourites per-kind family parse for fallback-block copies
    elem->family = NULL;      // set by whoever chains the element into a family (addGUIElem, below)
    elem->extended = NULL;
    elem->drawElem = NULL;
    elem->endElem = &endBasic;
    elem->next = NULL;
    elem->textRoom = 0;

    snprintf(elemProp, sizeof(elemProp), "%s_x", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "POS_MID", 7))
            x = gThmScreenWidth >> 1;
        else
            x = atoi(temp);
    }
    if (x < 0)
        elem->posX = gThmScreenWidth + x;
    else
        elem->posX = x;

    snprintf(elemProp, sizeof(elemProp), "%s_y", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "POS_MID", 7))
            y = gThmScreenHeight >> 1;
        else
            y = atoi(temp);
    }
    if (y < 0)
        elem->posY = ceil((gThmScreenHeight + y) * theme->usedHeight / gThmScreenHeight);
    else
        elem->posY = y;

    snprintf(elemProp, sizeof(elemProp), "%s_width", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "DIM_INF", 7))
            elem->width = gThmScreenWidth;
        else
            elem->width = atoi(temp);
    } else
        elem->width = w;

    snprintf(elemProp, sizeof(elemProp), "%s_height", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "DIM_INF", 7))
            elem->height = gThmScreenHeight;
        else
            elem->height = atoi(temp);
    } else
        elem->height = h;

    snprintf(elemProp, sizeof(elemProp), "%s_aligned", name);
    if (configGetInt(themeConfig, elemProp, &intValue)) {
        // 0 = none/left, 2 = right-justified (wOPL/uOPL theme key; used to silently center),
        // anything else = centered (the long-standing non-zero behavior).
        if (intValue == 0)
            elem->aligned = ALIGN_NONE;
        else if (intValue == 2)
            elem->aligned = (ALIGN_VCENTER | ALIGN_RIGHT);
        else
            elem->aligned = ALIGN_CENTER;
    } else
        elem->aligned = aligned;

    snprintf(elemProp, sizeof(elemProp), "%s_scaled", name);
    if (configGetInt(themeConfig, elemProp, &intValue))
        elem->scaled = (intValue == 0) ? SCALING_NONE : SCALING_RATIO;
    else
        elem->scaled = scaled;

    snprintf(elemProp, sizeof(elemProp), "%s_color", name);
    if (configGetColor(themeConfig, elemProp, charColor))
        elem->color = GS_SETREG_RGBA(charColor[0], charColor[1], charColor[2], 0x80);
    else
        elem->color = color;

    elem->font = font;
    snprintf(elemProp, sizeof(elemProp), "%s_font", name);
    if (configGetInt(themeConfig, elemProp, &intValue)) {
        if (intValue > 0 && intValue < THM_MAX_FONTS)
            elem->font = theme->fonts[intValue];
    }

    snprintf(elemProp, sizeof(elemProp), "%s_reflection", name);
    if (configGetInt(themeConfig, elemProp, &intValue))
        elem->reflection = intValue ? 1 : 0;

    snprintf(elemProp, sizeof(elemProp), "%s_reflection_offset", name);
    if (configGetInt(themeConfig, elemProp, &intValue)) {
        // Clamp to a sane band: an offset beyond a screen height only draws strips off-canvas
        // (wasted work), and a hand-edited extreme should not push the reflection into nowhere.
        if (intValue < -1024)
            intValue = -1024;
        else if (intValue > 1024)
            intValue = 1024;
        elem->reflectionOffset = intValue;
    }

    // Optional per-device filter (MenuIcon/ItemsList/HintText consume it; harmless elsewhere).
    snprintf(elemProp, sizeof(elemProp), "%s_devices", name);
    if (configGetStr(themeConfig, elemProp, &temp))
        elem->deviceFilter = thmParseDeviceList(temp, 0);

    return elem;
}

// Internal elements ////////////////////////////////////////////////////////////////////////////////////////////////////////
static void drawBackground(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    host::ui().drawBackgroundPlasma();
}

void initBackground(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *pattern, int count, const char *texture)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, pattern, count, texture, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->cache)
        elem->drawElem = &drawGameImage;
    else if (mutableImage->defaultTexture)
        elem->drawElem = &drawStaticImage;
    else
        elem->drawElem = &drawBackground;
}

void drawMenuIcon(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (thmElemSkipsDevice(elem, menu->item->icon_id))
        return; // devices= filter: not this page's element
    GSTEXTURE *menuIconTex = thmGetTexture(menu->item->icon_id);
    if (menuIconTex && menuIconTex->Mem)
        rmDrawPixmap(menuIconTex, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);
}

static int findMenuNext(struct menu_list *menu)
{
    struct menu_list *next = menu->next;
    while (next != NULL && next->item->visible == 0)
        next = next->next;

    return next == NULL ? 0 : next->item->visible;
}

static int findMenuPrev(struct menu_list *menu)
{
    struct menu_list *prev = menu->prev;
    while (prev != NULL && prev->item->visible == 0)
        prev = prev->prev;

    return prev == NULL ? 0 : prev->item->visible;
}

void drawMenuText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    GSTEXTURE *leftIconTex = NULL, *rightIconTex = NULL;
    if (findMenuPrev(menu) != 0)
        leftIconTex = thmGetTexture(LEFT_ICON);
    if (findMenuNext(menu) != 0)
        rightIconTex = thmGetTexture(RIGHT_ICON);

    if (elem->aligned) {
        int offset = elem->width >> 1;
        if (leftIconTex && leftIconTex->Mem)
            rmDrawPixmap(leftIconTex, elem->posX - offset, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol, 0);
        if (rightIconTex && rightIconTex->Mem)
            rmDrawPixmap(rightIconTex, elem->posX + offset, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol, 0);
    } else {
        if (leftIconTex && leftIconTex->Mem)
            rmDrawPixmap(leftIconTex, elem->posX - leftIconTex->Width, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol, 0);
        if (rightIconTex && rightIconTex->Mem)
            rmDrawPixmap(rightIconTex, elem->posX + elem->width, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol, 0);
    }
    fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, menuItemGetText(menu->item), elem->color);
}

void drawBDMIndex(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    item_list_t *itemList = static_cast<item_list_t*>(menu->item->userdata);
    // Only render for bdm modes and if current mode is visible
    if (itemList->mode >= ETH_MODE || menu->item->visible == 0)
        return;

    // Only render if multiple mass devices are connected
    if (itemList->mode == 0 && menu->next->item->visible == 0)
        return;

    char imgName[32];
    snprintf(imgName, sizeof(imgName), "Index_%d", itemList->mode);

    GSTEXTURE *indexTex = thmGetTexture(texLookupInternalTexId(&imgName[0]));
    if (indexTex && indexTex->Mem)
        rmDrawPixmap(indexTex, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol, 0);
}

void drawItemsList(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    // devices= filter: the nav pointer (gTheme->itemsList) is resolved with the SAME matching rules
    // (thmResolveItemsList), so the rows that draw are always the rows navigation counts.
    if (thmElemSkipsDevice(elem, menu->item->icon_id))
        return;

    if (item) {
        items_list_t *itemsList = (items_list_t *)elem->extended;
        item_list_t *list = static_cast<item_list_t*>(menu->item->userdata);

        int posX = elem->posX, posY = elem->posY;
        if (elem->aligned) {
            posX -= elem->width >> 1;
            posY -= elem->height >> 1;
        }

        // List admission mirrors Coverflow: selected first, then center-out warming sized by
        // the actual resolved cover cache. Merely being visible is not enough to start a read; the
        // warm radius is bounded by cache capacity, so the selected cover keeps a reserved slot and
        // loose PNG users regain the deep lookahead older fast builds provided. Games, APPS,
        // Favourites and VCD all pass through this one scheduler.
        image_cache_t *selectedCache = NULL;
        if (itemsList->decoratorImage != NULL && !item->item.isFolder &&
            (gEnableDiscArt || !isDiscArtCache(itemsList->decoratorImage->cache))) {
            selectedCache = itemsList->decoratorImage->cache;
            getGameImageTextureEx(selectedCache, menu->item->userdata, &item->item, 1);
        } else if (itemsList->coverElem != NULL && !item->item.isFolder) {
            mutable_image_t *selImg = (mutable_image_t *)thmGetElemForItem(menu, item, itemsList->coverElem)->extended;
            if (selImg != NULL && selImg->cache != NULL) {
                selectedCache = selImg->cache;
                getGameImageTextureEx(selectedCache, menu->item->userdata, &item->item, 1);
            }
        }

        int warmRadius = (selectedCache != NULL && selectedCache->count > 1) ? selectedCache->count - 1 : 0;
        struct
        {
            image_cache_t *cache;
            int left;
        } listWarm[2] = {{NULL, 0}, {NULL, 0}};
        submenu_list_t *walkSide[2] = {item, item}; // [0] next, [1] previous
        for (int step = 0; step < warmRadius; step++) {
            for (int side = 0; side < 2; side++) {
                submenu_list_t *walk = walkSide[side];
                submenu_list_t *next = side ? walk->prev : walk->next;
                if (next == NULL || next == walk)
                    continue;
                walkSide[side] = next;
                if (next->item.isFolder)
                    continue;

                image_cache_t *warmCache = NULL;
                if (itemsList->decoratorImage != NULL) {
                    warmCache = itemsList->decoratorImage->cache;
                } else if (itemsList->coverElem != NULL) {
                    mutable_image_t *warmImg = (mutable_image_t *)thmGetElemForItem(menu, next, itemsList->coverElem)->extended;
                    if (warmImg != NULL)
                        warmCache = warmImg->cache;
                }
                if (warmCache == NULL || warmCache->count < 2)
                    continue;

                int b;
                for (b = 0; b < 2 && listWarm[b].cache != NULL && listWarm[b].cache != warmCache; b++)
                    ;
                if (b == 2)
                    continue;
                if (listWarm[b].cache == NULL) {
                    listWarm[b].cache = warmCache;
                    listWarm[b].left = warmCache->count - 1; // selected's slot remains reserved
                }
                if (listWarm[b].left > 0) {
                    listWarm[b].left--;
                    getGameImageTexture(warmCache, list, &next->item);
                }
            }
        }

        submenu_list_t *ps = menu->item->pagestart;
        int others = 0;
        u64 color;
        int textEndX = 0;
        while (ps && (others++ < itemsList->displayedItems)) {
            if (ps == item)
                color = gTheme->selTextColor;
            else
                color = elem->color;

            const char *dispText = ps->item.isFolder ? submenuItemGetText(&ps->item) :
                                                       vcdDisplayNameForRow(list, ps->item.id, submenuItemGetText(&ps->item));
            char folderBuf[256];
            if (ps->item.isFolder) {
                snprintf(folderBuf, sizeof(folderBuf), "%s/", dispText);
                dispText = folderBuf;
            }

            if (itemsList->decoratorImage &&
                (gEnableDiscArt || !isDiscArtCache(itemsList->decoratorImage->cache))) {
                GSTEXTURE *itemIconTex = NULL;

                if (!ps->item.isFolder) {
                    // Admission happened center-out above. Farther visible rows draw only what is
                    // already resident, so a 10-20-row viewport cannot flood USB with loose PNG opens.
                    itemIconTex = getGameImageCached(itemsList->decoratorImage->cache, &ps->item);
                }

                if (itemIconTex && itemIconTex->Mem)
                    rmDrawPixmap(itemIconTex, posX, posY, elem->aligned, DECORATOR_SIZE, DECORATOR_SIZE, elem->scaled, gDefaultCol, 0);
                else if (itemsList->decoratorImage->defaultTexture)
                    rmDrawPixmap(&itemsList->decoratorImage->defaultTexture->source, posX, posY, elem->aligned, DECORATOR_SIZE, DECORATOR_SIZE, elem->scaled, gDefaultCol, 0);

                if (elem->textRoom > 0)
                    textEndX = fntRenderStringFit(elem->font, elem->posX + DECORATOR_SIZE, posY, elem->aligned, elem->textRoom, dispText, color, ps == item);
                else
                    textEndX = fntRenderString(elem->font, elem->posX + DECORATOR_SIZE, posY, elem->aligned, elem->width, elem->height, dispText, color);
            } else {
                // Decorator-less Lists use the same center-out warming above; their separate COV
                // element draws the selected cover. No additional per-visible-row reads start here.
                if (elem->textRoom > 0)
                    textEndX = fntRenderStringFit(elem->font, elem->posX, posY, elem->aligned, elem->textRoom, dispText, color, ps == item);
                else
                    textEndX = fntRenderString(elem->font, elem->posX, posY, elem->aligned, elem->width, elem->height, dispText, color);
            }

            if (ps->item.favourited) {
                GSTEXTURE *favTex = thmGetTexture(FAV_MARK);
                if (favTex != NULL && favTex->Mem != NULL)
                    rmDrawPixmap(favTex, textEndX + 4, posY, elem->aligned, MENU_ITEM_HEIGHT, MENU_ITEM_HEIGHT, elem->scaled, gDefaultCol, 0);
            }

            posY += MENU_ITEM_HEIGHT;
            ps = ps->next;
        }
    }
}

void initItemsList(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *decorator)
{
    char elemProp[64];

    items_list_t *itemsList = (items_list_t *)malloc(sizeof(items_list_t));

    if (elem->width == DIM_UNDEF)
        elem->width = gThmScreenWidth;

    if (elem->height == DIM_UNDEF)
        elem->height = theme->usedHeight - (MENU_POS_V + HINT_HEIGHT);

    itemsList->displayedItems = elem->height / MENU_ITEM_HEIGHT;
    LOG("THEMES ItemsList %s: displaying %d elems, item height: %d\n", name, itemsList->displayedItems, elem->height);

    itemsList->decorator = NULL;
    snprintf(elemProp, sizeof(elemProp), "%s_decorator", name);
    configGetStr(themeConfig, elemProp, &decorator);
    if (decorator)
        itemsList->decorator = decorator; // Will be used later (thmValidate)

    itemsList->decoratorImage = NULL;
    itemsList->coverElem = NULL;
    elem->extended = itemsList;
    // elem->endElem = &endBasic; does the job

    elem->drawElem = &drawItemsList;
}

void drawItemText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (menu != NULL && menu->item != NULL && item != NULL) {
        item_list_t *support = static_cast<item_list_t*>(menu->item->userdata);
        if (support != NULL && support->itemGetStartup != NULL) {
            char *startup = support->itemGetStartup(support, item->item.id);
            if (startup != NULL) {
                // VCD view (#380): the caption shows the resolved disc ID, not the title -- the same
                // thing the PS2 page shows from this element (g->startup), and 11 chars cannot
                // overflow into the cover art the way "SLUS_123.45.Final Fantasy VII" did. Memo-read
                // ONLY (vcdDisplayIdCached): the id is resolved off the render thread
                // (vcdRequestDisplayId, menusys.c), so a slow device never blocks here; until it
                // lands, the filename's own id prefix is shown, then the title as before.
                if (libListRowView(support, item->item.id) == LIB_VIEW_PS1) {
                    char vcdId[VCD_ID_MAX];
                    if (vcdDisplayIdCached(startup, vcdId, sizeof(vcdId)) ||
                        vcdExtractGameId(startup, vcdId, sizeof(vcdId))) {
                        thmDrawElemText(elem, SIZING_NONE, vcdId, 1);
                        return;
                    }
                }
                thmDrawElemText(elem, SIZING_NONE, vcdDisplayNameForRow(support, item->item.id, startup), 1);
            }
        }
    }
}

void drawHintText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (thmElemSkipsDevice(elem, menu->item->icon_id))
        return; // devices= filter: not this page's element

    /* HELD ACROSS BOTH WALKS, not just the draw loop. guiAlignMenuHints() traverses the list too, so
       reading the head and then measuring it under one lock and drawing it under another would still
       let the io worker free the chain in between. The rebuild that races this
       (moduleUpdateMenuInternal, on the io worker) frees every node before relinking, so a stale
       ->next points into memory already handed back out.
       Cheap enough to sit on the per-frame draw path: uncontended almost always, and the only
       holder on the other side does a handful of frees and mallocs with no I/O. */
    menuHintsLock();

    menu_hint_item_t *hint = menu->item->hints;
    if (hint) {
        int x = elem->posX;

        if (elem->aligned)
            x = guiAlignMenuHints(hint, elem->font, elem->width);

        for (; hint; hint = hint->next) {
            x = guiDrawIconAndText(hint->icon_id, hint->text_id, elem->font, x, elem->posY, elem->color);
            x += elem->width;
        }
    }

    menuHintsUnlock();
}

void drawInfoHintText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    int infoHints[2] = {_STR_RUN, _STR_BACK};
    int infoIcons[2] = {CIRCLE_ICON, CROSS_ICON};
    int x = elem->posX;
    // <name>_show_run=0 hides Run for a theme that draws its own launch prompt. Back always stays: it
    // is the only way off this screen.
    int first = elem->showRun ? 0 : 1;

    if (elem->aligned)
        x = guiAlignSubMenuHints(2 - first, infoHints + first, infoIcons + first, elem->font, elem->width, 1);

    if (elem->showRun) {
        x = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? infoIcons[0] : infoIcons[1], infoHints[0], elem->font, x, elem->posY, elem->color);
        x += elem->width;
    }
    x = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? infoIcons[1] : infoIcons[0], infoHints[1], elem->font, x, elem->posY, elem->color);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

