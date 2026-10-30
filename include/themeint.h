#ifndef __THEMEINT_H
#define __THEMEINT_H

// Internals shared between themes.cpp (loader, catalog, element drawing), coverflow.cpp and psbbnflow.cpp. Not public API.

#include "themes.h"

constexpr int MENU_POS_V = 50;
constexpr int HINT_HEIGHT = 32;
constexpr int DECORATOR_SIZE = 20;
// Cover cache depth. Hardware (debug HUD, art delay 0, one USB device, BG off): 73 ms per cover
// load -- the load path is fine -- but D climbing past 100 COMPLETED loads in a single browse, far
// more covers than the library shows. That is the same files being read again and again: the
// viewport plus the warm loop demand more distinct covers than the cache holds, so every move
// evicts one that is about to be asked for again and the device never goes quiet. That re-reading
// is the "drag" that survived removing the throttles. Sized so the visible set and its warmed
// neighbours fit together; at the size themes actually ship (140x200 in that capture) a decoded
// cover is well under 100 KB, so this trades ~1 MB of EE RAM for not re-reading USB.
// Cover warming is derived from each resolved COV cache's actual capacity. The selected cover
// always claims the first slot; the remaining slots are walked center-out on both sides. This
// restores the pre-2587 "already waiting for you" behavior without a fixed +/-2 cap, while the
// BGM low-water gate in texcache.c remains the device-level safety valve under real contention.
// Extra idle frames a per-game BACKGROUND waits beyond the art delay before it may be requested, so
// the cover always reaches the IO queue first (see drawGameImage). Half a second at 60 Hz: long
// enough that a cover request is queued and usually finished, short enough that a background still
// arrives while the user is reading the row.
constexpr int BG_REQUEST_EXTRA_IDLE_FRAMES = 30;
// Cache slots per AttributeImage element. An AttributeImage is NOT a per-game image -- it is a small FIXED
// SET of glyphs keyed by the attribute's value, so the cache only ever needs to hold that attribute's whole
// value set to be thrash-free. Our built-in attributes are tiny (#Format = ISO/ZSO/VCD/UL/ELF/HDL = 6,
// #DiscType = 3, #Media/#System = 2), but a theme can bind AttributeImage to a CUSTOM per-game config key
// (Genre, Developer, Publisher, ...) whose value set is open-ended (Gemini review of #49), so size for that:
// 32 covers any realistic custom attribute with headroom. Genuinely cheap -- cacheInitCache allocates only
// `count` empty cache_entry_t structs (no texture memory until a glyph is actually loaded into a slot), so
// 32 slots is ~2-3 KB of EE RAM and a #DiscType cache still only ever HOLDS its 3.
constexpr int ATTR_IMAGE_CACHE_SLOTS = 32;


enum ELEM_ATTRIBUTE_TYPE {
    ELEM_TYPE_ATTRIBUTE_TEXT = 0,
    ELEM_TYPE_STATIC_TEXT,
    ELEM_TYPE_ATTRIBUTE_IMAGE,
    ELEM_TYPE_GAME_IMAGE,
    ELEM_TYPE_STATIC_IMAGE,
    ELEM_TYPE_BACKGROUND, // A static image can be specified as the background. Otherwise, the plasma background will be drawn.
    ELEM_TYPE_MENU_ICON,
    ELEM_TYPE_MENU_TEXT,
    ELEM_TYPE_ITEMS_LIST,
    ELEM_TYPE_ITEM_ICON,
    ELEM_TYPE_ITEM_COVER,
    ELEM_TYPE_ITEM_TEXT,
    ELEM_TYPE_HINT_TEXT,
    ELEM_TYPE_INFO_HINT_TEXT,
    ELEM_TYPE_LOADING_ICON,
    ELEM_TYPE_BDM_INDEX,
    ELEM_TYPE_GAME_COUNT_TEXT,
    ELEM_TYPE_COVERFLOW,
    ELEM_TYPE_PSBBN_FLOW,
    ELEM_TYPE_SAVE_ICON,
    ELEM_TYPE_GRID,
    ELEM_TYPE_COUNT
};

constexpr int DISPLAY_ALWAYS = 0;
constexpr int DISPLAY_DEFINED = 1;
constexpr int DISPLAY_NEVER = 2;

constexpr int SIZING_NONE = -1;
constexpr int SIZING_CLIP = 0;
constexpr int SIZING_WRAP = 1;

extern const char *const elementsType[ELEM_TYPE_COUNT];

// Cover cache size for COV elements. Sized so the visible set and its warmed neighbours fit together; a decoded
// cover is well under 100 KB at shipped theme sizes, so this trades ~1 MB of EE RAM for not re-reading USB.
constexpr int COVER_CACHE_SLOTS = 20;

// Virtual screen size as last read by thmReloadScreenExtents().
extern int gThmScreenWidth;
extern int gThmScreenHeight;

mutable_image_t *initMutableImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, const char *cachePattern, int cacheCount, const char *defaultTexture, const char *overlayTexture);
void endMutableImage(struct theme_element *elem);
GSTEXTURE *getGameImageTextureEx(image_cache_t *cache, void *support, struct submenu_item *item, int isPriority);
GSTEXTURE *getGameImageTexture(image_cache_t *cache, void *support, struct submenu_item *item);
int isHiddenArtworkCache(const image_cache_t *cache);
void *thmGetItemSource(struct menu_list *menu, struct submenu_list *item);
theme_element_t *thmGetElemForItem(struct menu_list *menu, struct submenu_list *item, theme_element_t *elem);

// themeelems.cpp (element init/draw functions the loader wires up)
int thmParseDeviceList(const char *value, int quiet);
void thmComputeDeviceCoverage(theme_elems_t *elems);
void thmComputeTextRooms(theme_elems_t *elems);
void initStaticText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name);
void initGameCountText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name);
void initAttributeText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name);
void findDuplicate(theme_element_t *first, int type, const char *cachePattern, const char *defaultTexture, const char *overlayTexture, const char *overlayTexture2, mutable_image_t *target);
void initStaticImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *imageName);
void drawGameImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
void initGameImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *pattern, int count, const char *texture, const char *overlay);
theme_element_t *thmFindElemBySuffix(theme_elems_t *elems, const char *suffix, theme_element_t *like);
void drawAttributeImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
void initAttributeImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name);
void endBasic(theme_element_t *elem);
theme_element_t *initBasic(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, int x, int y, short aligned, int w, int h, short scaled, u64 color, int font);
void initBackground(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *pattern, int count, const char *texture);
void drawMenuIcon(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
void drawMenuText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
void drawBDMIndex(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
void drawItemsList(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
void itemsListFit(items_list_t *itemsList, int height);
void initItemsList(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *decorator);
void drawItemText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
void drawSaveIcon(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
void drawHintText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
void drawInfoHintText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);

// coverflow.cpp
void initCoverflow(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name);

// grid.cpp
void initGrid(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name);

// psbbnflow.cpp
void initPsbbnFlow(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name);
void psbbnFlowTrigger(int dir);
void psbbnFlowReset(void);
// True on a frame where PSBBN Flow drew covers in motion: its title row stays hidden until they settle.
int psbbnFlowIsMoving(void);

#endif
