#include "include/host.h"
#include "include/opl.h"
#include "include/themes.h"
#include "include/themecatalog.h"
#include "include/psbbnflow.h"
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
#include "include/themeviews.h"
#ifdef RETROACHIEVEMENTS
#include "include/rabadge.h" // RA: mark over the cover
#endif

#include <time.h>
#include <math.h>

extern const char conf_theme_OPL_cfg;
extern u16 size_conf_theme_OPL_cfg;
extern const char theme_coverflow_cfg;
extern u16 size_theme_coverflow_cfg;
extern const char theme_psbbn_flow_cfg;
extern u16 size_theme_psbbn_flow_cfg;
extern const char theme_grid_cfg;
extern u16 size_theme_grid_cfg;

theme_t *gTheme;

// The view the user asked for (THM_VIEW_*), -1 = the theme's default. thmLoad falls back to the theme's
// default when the theme does not offer it.
int gThemeViewRequest = -1;

static unsigned int thmLoadCount; // theme_t::artGeneration of the last load

// Parse-time only: the "<view>" block prefix of the view thmLoad is building, "" for the base blocks.
static const char *thmParseViewPrefix = "";
static int thmParseViewInherit = 1; // 0 for a <view>.inherit=0 view: no fallback to the base blocks
// Parse-time only: hd_autofit=1 and the theme is being built for 16:9 (thmAutoFitElem).
static int thmParseAutoFit = 0;

static ThemeCatalog gCatalog;

int gThmScreenWidth;
int gThmScreenHeight;

const char *const elementsType[ELEM_TYPE_COUNT] = {
    "AttributeText",
    "StaticText",
    "AttributeImage",
    "GameImage",
    "StaticImage",
    "Background",
    "MenuIcon",
    "MenuText",
    "ItemsList",
    "ItemIcon",
    "ItemCover",
    "ItemText",
    "HintText",
    "InfoHintText",
    "LoadingIcon",
    "BdmIndex",
    "GameCountText",
    "Coverflow",
    "PSBBNFlow",
    "SaveIcon",
    "Grid"};

static void validateBackgroundElems(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *mainElems, theme_elems_t *infoElems)
{
    if (!mainElems->first || (mainElems->first->type != ELEM_TYPE_BACKGROUND)) {
        LOG("THEMES No valid background found for main, add default BG_ART\n");
        theme_element_t *backgroundElem = initBasic(themePath, themeConfig, theme, "bg", ELEM_TYPE_BACKGROUND, 0, 0, ALIGN_NONE, gThmScreenWidth, gThmScreenHeight, SCALING_NONE, gDefaultCol, theme->fonts[0]);
        initBackground(themePath, themeConfig, theme, backgroundElem, "bg", "BG", 1, NULL);
        backgroundElem->family = mainElems;
        backgroundElem->next = mainElems->first;
        mainElems->first = backgroundElem;
    }

    if (infoElems->first) {
        if (infoElems->first->type != ELEM_TYPE_BACKGROUND) {
            LOG("THEMES No valid background found for info, add default BG_ART\n");
            theme_element_t *backgroundElem = initBasic(themePath, themeConfig, theme, "bg", ELEM_TYPE_BACKGROUND, 0, 0, ALIGN_NONE, gThmScreenWidth, gThmScreenHeight, SCALING_NONE, gDefaultCol, theme->fonts[0]);
            initBackground(themePath, themeConfig, theme, backgroundElem, "bg", "BG", 1, NULL);
            backgroundElem->family = infoElems;
            backgroundElem->next = infoElems->first;
            infoElems->first = backgroundElem;
        }
    }
}

// Returns the validated list -- the caller MUST store it back into the slot: when no list exists a
// default one is created here, and before the write-back the slot stayed NULL while the default was
// only chained for drawing, leaving gTheme->itemsList NULL (menusys derefs it without a check). That
// was unreachable while every theme carried an unfiltered ItemsList; devices=-filtered ItemsLists
// (which deliberately claim no slot) make it a one-key theme edit away.
static theme_element_t *validateItemsList(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *list, theme_elems_t *mainElems)
{
    if (list) {
        items_list_t *itemsList = (items_list_t *)list->extended;
        if (itemsList->decorator) {
            // Second pass to find the decorator
            theme_element_t *decoratorElem = mainElems->first;
            while (decoratorElem) {
                if (decoratorElem->type == ELEM_TYPE_GAME_IMAGE) {
                    mutable_image_t *gameImage = (mutable_image_t *)decoratorElem->extended;
                    // A GAME_IMAGE element can have cache == NULL (no _pattern); guard before
                    // dereferencing ->suffix, matching drawGameImage and the other cache users.
                    if (gameImage->cache && !strcmp(itemsList->decorator, gameImage->cache->suffix())) {
                        // if user want to cache less than displayed items, then disable itemslist icons, if not would load constantly
                        if (gameImage->cache->count() >= itemsList->displayedItems)
                            itemsList->decoratorImage = gameImage;
                        break;
                    }
                }

                decoratorElem = decoratorElem->next;
            }
            itemsList->decorator = NULL;
        }
    } else {
        LOG("THEMES No itemsList found, adding a default one\n");
        list = initBasic(themePath, themeConfig, theme, "il", ELEM_TYPE_ITEMS_LIST, 42, 42, ALIGN_NONE, 373, 316, SCALING_RATIO, theme->titleColor, theme->fonts[0]);
        initItemsList(themePath, themeConfig, theme, list, "il", NULL);
        list->family = mainElems;
        list->next = mainElems->first->next; // Position the itemsList as second element (right after the Background)
        mainElems->first->next = list;
    }

    return list;
}

// Run validateItemsList's decorator linking for devices=-FILTERED ItemsList elements: they claim no
// slot, so the slot-based pass above never sees them -- without this their `decorator` string would
// keep pointing into themeConfig (freed at the end of thmLoad) and the decorator image would never
// link. Filtered lists are always non-NULL here, so this can never take the create-default branch.
static void validateFilteredItemsLists(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *mainElems)
{
    theme_element_t *elem;

    for (elem = mainElems->first; elem != NULL; elem = elem->next) {
        if (elem->type == ELEM_TYPE_ITEMS_LIST && elem->deviceFilter)
            validateItemsList(themePath, themeConfig, theme, elem, mainElems);
    }
}

// devices=-filtered ItemsLists own no slot, so the four slot checks in isDecoratorCoverImage never
// see their decorators. Recognizing them here is the CASCADE KILLER for the filtered-decorator
// cache split below: without it, each split's replaceSharedCoverCache treats another filtered
// list's decorator as a plain selected cover and re-points it, so repeated splits keep cloning.
// Walks all 8 families; keys off elem->deviceFilter (0 for every element of a devices=-free theme,
// so such themes take zero new branches).
// The Favourites per-kind families (favsVcd* / favsApps*) own no ItemsList slot, so EVERY ItemsList in
// them -- filtered or not -- is a slot-free list that the slot-based passes never reach. Callers treat
// such lists exactly like devices=-filtered ones. False for every other family, so themes without
// these opt-in families take none of the new branches.
static int thmIsFavKindFamily(theme_t *theme, theme_elems_t *elems)
{
    return elems == &theme->favsVcdMainElems || elems == &theme->favsVcdInfoElems ||
           elems == &theme->favsAppsMainElems || elems == &theme->favsAppsInfoElems;
}

static int loadedFontSize = FONT_SIZE_DEFAULT; // the size gTheme's list geometry was computed for

static int isFilteredDecoratorCoverImage(theme_t *theme, mutable_image_t *gameImage)
{
    theme_elems_t *groups[12] = {&theme->mainElems, &theme->infoElems, &theme->appsMainElems, &theme->appsInfoElems,
                                 &theme->favsMainElems, &theme->favsInfoElems, &theme->vcdMainElems, &theme->vcdInfoElems,
                                 &theme->favsVcdMainElems, &theme->favsVcdInfoElems, &theme->favsAppsMainElems, &theme->favsAppsInfoElems};
    int g;

    for (g = 0; g < 12; g++) {
        int slotFree = thmIsFavKindFamily(theme, groups[g]);
        theme_element_t *e = groups[g]->first;
        while (e != NULL) {
            if (e->type == ELEM_TYPE_ITEMS_LIST && (e->deviceFilter || slotFree) && e->extended != NULL &&
                ((items_list_t *)e->extended)->decoratorImage == gameImage)
                return 1;
            e = e->next;
        }
    }
    return 0;
}

static int isDecoratorCoverImage(theme_t *theme, mutable_image_t *gameImage)
{
    items_list_t *itemsList;

    if (theme == NULL || gameImage == NULL)
        return 0;

    if (theme->gamesItemsList != NULL && theme->gamesItemsList->extended != NULL) {
        itemsList = (items_list_t *)theme->gamesItemsList->extended;
        if (itemsList->decoratorImage == gameImage)
            return 1;
    }

    if (theme->appsItemsList != NULL && theme->appsItemsList->extended != NULL) {
        itemsList = (items_list_t *)theme->appsItemsList->extended;
        if (itemsList->decoratorImage == gameImage)
            return 1;
    }

    if (theme->favsItemsList != NULL && theme->favsItemsList->extended != NULL) {
        itemsList = (items_list_t *)theme->favsItemsList->extended;
        if (itemsList->decoratorImage == gameImage)
            return 1;
    }

    if (theme->vcdItemsList != NULL && theme->vcdItemsList->extended != NULL) {
        itemsList = (items_list_t *)theme->vcdItemsList->extended;
        if (itemsList->decoratorImage == gameImage)
            return 1;
    }

    return isFilteredDecoratorCoverImage(theme, gameImage);
}

static image_cache_t *cloneImageCache(theme_t *theme, image_cache_t *source)
{
    image_cache_t *cache;

    if (theme == NULL || source == NULL)
        return NULL;

    cache = cacheInitCache(theme->gameCacheCount++, source->prefix(), source->isPrefixRelative(), source->suffix(), source->count());

    return cache;
}

static void replaceSharedCoverCache(theme_t *theme, theme_elems_t *elems, image_cache_t *sourceCache, image_cache_t *replacementCache, int *replacementAssigned)
{
    theme_element_t *elem;

    if (theme == NULL || elems == NULL || sourceCache == NULL || replacementCache == NULL || replacementAssigned == NULL)
        return;

    elem = elems->first;
    while (elem != NULL) {
        if (elem->type == ELEM_TYPE_GAME_IMAGE) {
            mutable_image_t *gameImage = (mutable_image_t *)elem->extended;

            if (gameImage != NULL && !isDecoratorCoverImage(theme, gameImage) && gameImage->cache == sourceCache) {
                gameImage->cache = replacementCache;
                gameImage->cacheLinked = *replacementAssigned ? 1 : 0;
                *replacementAssigned = 1;
            }
        }

        elem = elem->next;
    }
}

static void splitDecoratorCoverCache(theme_t *theme, theme_element_t *list)
{
    items_list_t *itemsList;
    image_cache_t *sourceCache;
    image_cache_t *replacementCache;
    int replacementAssigned;

    if (theme == NULL || list == NULL || list->extended == NULL)
        return;

    itemsList = (items_list_t *)list->extended;
    if (itemsList->decoratorImage == NULL || itemsList->decoratorImage->cache == NULL)
        return;

    sourceCache = itemsList->decoratorImage->cache;
    if (sourceCache->suffix() == NULL || strcmp(sourceCache->suffix(), "COV") != 0)
        return;

    replacementCache = cloneImageCache(theme, sourceCache);
    if (replacementCache == NULL)
        return;

    replacementAssigned = 0;
    replaceSharedCoverCache(theme, &theme->mainElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->infoElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->appsMainElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->appsInfoElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->favsMainElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->favsInfoElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->vcdMainElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->vcdInfoElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->favsVcdMainElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->favsVcdInfoElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->favsAppsMainElems, sourceCache, replacementCache, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->favsAppsInfoElems, sourceCache, replacementCache, &replacementAssigned);

    if (!replacementAssigned)
        cacheDestroyCache(replacementCache);
}

// Run the decorator/selected-cover split for every devices=-filtered ItemsList (they own no slot,
// so the four slot calls in validateGUIElems never reach them). splitDecoratorCoverCache's own
// guards (NULL extended/decoratorImage, non-COV suffix, destroy-unassigned-clone) make repeat
// calls per shared source cache a no-op -- see the non-cascade note at the call site.
static void splitFilteredDecoratorCoverCaches(theme_t *theme)
{
    int g;

    if (theme == NULL)
        return;

    theme_elems_t *groups[12] = {&theme->mainElems, &theme->infoElems, &theme->appsMainElems, &theme->appsInfoElems,
                                 &theme->favsMainElems, &theme->favsInfoElems, &theme->vcdMainElems, &theme->vcdInfoElems,
                                 &theme->favsVcdMainElems, &theme->favsVcdInfoElems, &theme->favsAppsMainElems, &theme->favsAppsInfoElems};
    for (g = 0; g < 12; g++) {
        int slotFree = thmIsFavKindFamily(theme, groups[g]);
        theme_element_t *e = groups[g]->first;
        while (e != NULL) {
            if (e->type == ELEM_TYPE_ITEMS_LIST && (e->deviceFilter || slotFree))
                splitDecoratorCoverCache(theme, e);
            e = e->next;
        }
    }
}

// True if `cache` is referenced by a COV GAME_IMAGE OUTSIDE the vcd family (games/apps/favs main+info).
// separateVcdCoverCache only acts when the vcd covers' cache is genuinely SHARED with a non-vcd family;
// if the cache is vcd-private (e.g. a theme with vcd COV covers but no games COV cover), cloning and
// re-pointing the vcd covers would leave the original cache unreferenced -> a one-time leak. Skip then.
static int vcdCoverCacheSharedOutsideVcd(theme_t *theme, image_cache_t *cache)
{
    // favsVcd* is on the VCD side (it is re-pointed with vcd* below); favsApps* is outside it.
    theme_elems_t *groups[8] = {&theme->mainElems, &theme->infoElems, &theme->appsMainElems,
                                &theme->appsInfoElems, &theme->favsMainElems, &theme->favsInfoElems,
                                &theme->favsAppsMainElems, &theme->favsAppsInfoElems};
    for (int g = 0; g < 8; g++) {
        theme_element_t *e = groups[g]->first;
        while (e != NULL) {
            if (e->type == ELEM_TYPE_GAME_IMAGE) {
                mutable_image_t *gi = (mutable_image_t *)e->extended;
                if (gi != NULL && gi->cache == cache)
                    return 1;
            }
            e = e->next;
        }
    }
    return 0;
}

// Give the L3 VCD/PS1 view its OWN COV cover cache so it never shares cache slots with the games view.
// The VCD list reuses the device's own game list (identical submenu item ids), and findDuplicate unifies
// every "COV" GAME_IMAGE cache by suffix -- so without this the VCD covers and the ISO covers collide in
// one (cache userId, item id)-keyed cache and thrash on every L3 toggle (eliminator1403's no-covers /
// flash-then-wrong-cover hardware report). splitDecoratorCoverCache only separates DECORATOR covers and
// no-ops on themes whose items list has no decorator (e.g. the default Coverflow theme), so the selected/
// carousel cover (vcdMain2) would still share the games cache; this handles that case unconditionally.
// Clones the COV cache used by the vcd family and re-points its non-decorator COV game-images onto it.
static void separateVcdCoverCache(theme_t *theme)
{
    if (theme == NULL)
        return;

    image_cache_t *source = NULL;
    theme_element_t *elem = theme->vcdMainElems.first;
    while (elem != NULL) {
        if (elem->type == ELEM_TYPE_GAME_IMAGE) {
            mutable_image_t *gameImage = (mutable_image_t *)elem->extended;
            if (gameImage != NULL && gameImage->cache != NULL && gameImage->cache->suffix() != NULL &&
                strcmp(gameImage->cache->suffix(), "COV") == 0 && !isDecoratorCoverImage(theme, gameImage)) {
                source = gameImage->cache; // the COV cache shared with the games view (via findDuplicate)
                break;
            }
        }
        elem = elem->next;
    }
    // Only separate when the cache is genuinely shared with a non-vcd family. If it is vcd-private there
    // is nothing to separate, and cloning + re-pointing would orphan (leak) the original cache.
    if (source == NULL || !vcdCoverCacheSharedOutsideVcd(theme, source))
        return;

    image_cache_t *replacement = cloneImageCache(theme, source);
    if (replacement == NULL)
        return;

    int replacementAssigned = 0;
    replaceSharedCoverCache(theme, &theme->vcdMainElems, source, replacement, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->vcdInfoElems, source, replacement, &replacementAssigned);
    // PS1 favourites draw from favsVcd* when the theme declares it (the Favourites PS1 view and the PS1
    // covers on the All shelf). Keep them on the same VCD cache the vcd family moves to, so a PS1 row
    // never loads its cover into the games cache on one view and the VCD cache on another.
    replaceSharedCoverCache(theme, &theme->favsVcdMainElems, source, replacement, &replacementAssigned);
    replaceSharedCoverCache(theme, &theme->favsVcdInfoElems, source, replacement, &replacementAssigned);

    if (!replacementAssigned)
        cacheDestroyCache(replacement);
}

// Link every non-decorator ItemsList of a family to that family's existing COV element (the
// selected-cover game-image / coverflow carousel). Runs AFTER the cache splits above, so the
// linked element's cache pointer is already final; the link itself is to the element, which
// survives the splits untouched. Families without a COV element simply get no warming.
static void linkItemsListCoverElems(theme_elems_t *elems)
{
    theme_element_t *e;

    for (e = elems->first; e != NULL; e = e->next) {
        items_list_t *itemsList;

        if (e->type != ELEM_TYPE_ITEMS_LIST || e->extended == NULL)
            continue;

        itemsList = (items_list_t *)e->extended;
        if (itemsList->decoratorImage == NULL)
            itemsList->coverElem = thmFindElemBySuffix(elems, "COV", NULL);
    }
}

// Theme elements are painted in linked-list order. ItemText carries the selected GAME ID/startup
// (and ELF caption in app views), so it is semantic foreground: an ItemIcon/ICO declared later by a
// theme must never paint over it. Stable-promote every ItemText node to the family tail while keeping
// all other theme z-order intact.
static void promoteItemTextElements(theme_elems_t *elems)
{
    theme_element_t *elem = elems->first;
    theme_element_t *prev = NULL;
    theme_element_t *textFirst = NULL;
    theme_element_t *textLast = NULL;

    while (elem != NULL) {
        theme_element_t *next = elem->next;

        if (elem->type == ELEM_TYPE_ITEM_TEXT) {
            if (prev != NULL)
                prev->next = next;
            else
                elems->first = next;

            elem->next = NULL;
            if (textLast != NULL)
                textLast->next = elem;
            else
                textFirst = elem;
            textLast = elem;
        } else {
            prev = elem;
        }

        elem = next;
    }

    if (textFirst != NULL) {
        if (elems->first != NULL)
            prev->next = textFirst;
        else
            elems->first = textFirst;
        elems->last = textLast;
    }
}

// Background validation for a Favourites per-kind pair. validateBackgroundElems adds a background to the
// MAIN group unconditionally, which would turn an undeclared (empty) family into a non-empty one and make
// menusys select it. So only a family that already has elements is touched; an info-only declaration is
// validated as its own main (its first element becomes the background, and the info branch then no-ops).
static void validateFavKindBackgrounds(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *mainElems, theme_elems_t *infoElems)
{
    if (mainElems->first)
        validateBackgroundElems(themePath, themeConfig, theme, mainElems, infoElems);
    else if (infoElems->first)
        validateBackgroundElems(themePath, themeConfig, theme, infoElems, infoElems);
}

// ItemsList validation for a Favourites per-kind family. Its lists own no slot, so link every list's
// decorator here (validateItemsList with a non-NULL list only does that). A declared MAIN family with no
// unfiltered list of its own gets a default list exactly when the slot family that view used before got
// one (addDefault: that family's slot was still empty at validation). Otherwise the page draws no list,
// as a theme that replaced the list slot intends, and thmFamilyItemsList navigates with the old slot.
static void validateFavKindItemsLists(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *elems, int addDefault)
{
    theme_element_t *elem;
    int hasUnfiltered = 0;

    if (elems->first == NULL)
        return;
    for (elem = elems->first; elem != NULL; elem = elem->next) {
        if (elem->type != ELEM_TYPE_ITEMS_LIST)
            continue;
        if (!elem->deviceFilter)
            hasUnfiltered = 1;
        validateItemsList(themePath, themeConfig, theme, elem, elems);
    }
    if (addDefault && !hasUnfiltered)
        validateItemsList(themePath, themeConfig, theme, NULL, elems);
}

static void validateGUIElems(const char *themePath, config_set_t *themeConfig, theme_t *theme)
{
    // 1. check we have a valid Background elements
    validateBackgroundElems(themePath, themeConfig, theme, &theme->mainElems, &theme->infoElems);
    validateBackgroundElems(themePath, themeConfig, theme, &theme->appsMainElems, &theme->appsInfoElems);
    validateBackgroundElems(themePath, themeConfig, theme, &theme->favsMainElems, &theme->favsInfoElems);
    validateBackgroundElems(themePath, themeConfig, theme, &theme->vcdMainElems, &theme->vcdInfoElems);
    validateFavKindBackgrounds(themePath, themeConfig, theme, &theme->favsVcdMainElems, &theme->favsVcdInfoElems);
    validateFavKindBackgrounds(themePath, themeConfig, theme, &theme->favsAppsMainElems, &theme->favsAppsInfoElems);

    // 2. check we have a valid ItemsList element, and link its decorator to the target element.
    // Store the result back: validateItemsList may CREATE the default list, and a NULL slot is a
    // menusys NULL deref (see the function comment).
    // Whether the Favourites PS1 / ELF views' slot families get a default list, for their per-kind
    // counterparts below (validateFavKindItemsLists).
    int vcdGetsDefaultList = theme->vcdItemsList == NULL;
    int favsGetsDefaultList = theme->favsItemsList == NULL;
    theme->gamesItemsList = validateItemsList(themePath, themeConfig, theme, theme->gamesItemsList, &theme->mainElems);
    theme->appsItemsList = validateItemsList(themePath, themeConfig, theme, theme->appsItemsList, &theme->appsMainElems);
    theme->favsItemsList = validateItemsList(themePath, themeConfig, theme, theme->favsItemsList, &theme->favsMainElems);
    theme->vcdItemsList = validateItemsList(themePath, themeConfig, theme, theme->vcdItemsList, &theme->vcdMainElems);

    // devices=-filtered ItemsList overrides: link their decorators (they own no slot, so the pass
    // above never reaches them). Info families too -- a filtered ItemsList can be declared there
    // and its decorator string must not be left pointing into themeConfig (freed at end of load).
    // Their decorators are also cache-split below; isDecoratorCoverImage recognizes
    // them, which keeps repeated splits from cascading.
    validateFilteredItemsLists(themePath, themeConfig, theme, &theme->mainElems);
    validateFilteredItemsLists(themePath, themeConfig, theme, &theme->infoElems);
    validateFilteredItemsLists(themePath, themeConfig, theme, &theme->appsMainElems);
    validateFilteredItemsLists(themePath, themeConfig, theme, &theme->appsInfoElems);
    validateFilteredItemsLists(themePath, themeConfig, theme, &theme->favsMainElems);
    validateFilteredItemsLists(themePath, themeConfig, theme, &theme->favsInfoElems);
    validateFilteredItemsLists(themePath, themeConfig, theme, &theme->vcdMainElems);
    validateFilteredItemsLists(themePath, themeConfig, theme, &theme->vcdInfoElems);
    // Favourites per-kind families: every list is slot-free (no-op for an undeclared, empty family).
    validateFavKindItemsLists(themePath, themeConfig, theme, &theme->favsVcdMainElems, vcdGetsDefaultList);
    validateFavKindItemsLists(themePath, themeConfig, theme, &theme->favsVcdInfoElems, 0);
    validateFavKindItemsLists(themePath, themeConfig, theme, &theme->favsAppsMainElems, favsGetsDefaultList);
    validateFavKindItemsLists(themePath, themeConfig, theme, &theme->favsAppsInfoElems, 0);

    // ...then precompute the unfiltered elements' coverage so an unfiltered MenuIcon/ItemsList/
    // HintText yields to filtered same-type siblings on the devices those cover. Must run AFTER the
    // slot pass: the auto-created default list needs its coverage too.
    thmComputeDeviceCoverage(&theme->mainElems);
    thmComputeDeviceCoverage(&theme->infoElems);
    thmComputeDeviceCoverage(&theme->appsMainElems);
    thmComputeDeviceCoverage(&theme->appsInfoElems);
    thmComputeDeviceCoverage(&theme->favsMainElems);
    thmComputeDeviceCoverage(&theme->favsInfoElems);
    thmComputeDeviceCoverage(&theme->vcdMainElems);
    thmComputeDeviceCoverage(&theme->vcdInfoElems);
    thmComputeDeviceCoverage(&theme->favsVcdMainElems);
    thmComputeDeviceCoverage(&theme->favsVcdInfoElems);
    thmComputeDeviceCoverage(&theme->favsAppsMainElems);
    thmComputeDeviceCoverage(&theme->favsAppsInfoElems);

    // Where each text element's text must stop so it never draws over a neighbour. After the list
    // pass above: the decorator decides where a list's rows start.
    thmComputeTextRooms(&theme->mainElems);
    thmComputeTextRooms(&theme->infoElems);
    thmComputeTextRooms(&theme->appsMainElems);
    thmComputeTextRooms(&theme->appsInfoElems);
    thmComputeTextRooms(&theme->favsMainElems);
    thmComputeTextRooms(&theme->favsInfoElems);
    thmComputeTextRooms(&theme->vcdMainElems);
    thmComputeTextRooms(&theme->vcdInfoElems);
    thmComputeTextRooms(&theme->favsVcdMainElems);
    thmComputeTextRooms(&theme->favsVcdInfoElems);
    thmComputeTextRooms(&theme->favsAppsMainElems);
    thmComputeTextRooms(&theme->favsAppsInfoElems);

    // Keep list-row decorator covers from evicting the selected cover.
    splitDecoratorCoverCache(theme, theme->gamesItemsList);
    splitDecoratorCoverCache(theme, theme->appsItemsList);
    splitDecoratorCoverCache(theme, theme->favsItemsList);
    splitDecoratorCoverCache(theme, theme->vcdItemsList);

    // devices=-filtered lists' decorators get the same split. Safe to repeat per list: with
    // isDecoratorCoverImage recognizing every decorator (filtered included), a split's
    // replaceSharedCoverCache can never re-point another list's decorator, so after the first
    // effective clone per source cache the later calls find no sharers and destroy their clone.
    splitFilteredDecoratorCoverCaches(theme);

    // The L3 VCD view reuses the device's own game list (same item ids), so its selected/carousel covers
    // must not share a COV cache with the ISO list -- otherwise toggling thrashes the same cache slots.
    separateVcdCoverCache(theme);

    // Default (decorator-less) Lists never demanded covers beyond the selection, so viewport
    // artwork loaded one highlight at a time (#296). Give each such List a non-owning link to
    // its family's COV element; drawItemsList uses it to warm the visible page through the
    // normal per-frame cache path. After the splits, so linked caches are final. Info
    // families too: a devices=-filtered ItemsList can live there (validateFilteredItemsLists).
    linkItemsListCoverElems(&theme->mainElems);
    linkItemsListCoverElems(&theme->infoElems);
    linkItemsListCoverElems(&theme->appsMainElems);
    linkItemsListCoverElems(&theme->appsInfoElems);
    linkItemsListCoverElems(&theme->favsMainElems);
    linkItemsListCoverElems(&theme->favsInfoElems);
    linkItemsListCoverElems(&theme->vcdMainElems);
    linkItemsListCoverElems(&theme->vcdInfoElems);
    linkItemsListCoverElems(&theme->favsVcdMainElems);
    linkItemsListCoverElems(&theme->favsVcdInfoElems);
    linkItemsListCoverElems(&theme->favsAppsMainElems);
    linkItemsListCoverElems(&theme->favsAppsInfoElems);

    // Painter-order safety: GAME ID / startup / ELF captions always remain readable over ICO discs.
    promoteItemTextElements(&theme->mainElems);
    promoteItemTextElements(&theme->infoElems);
    promoteItemTextElements(&theme->appsMainElems);
    promoteItemTextElements(&theme->appsInfoElems);
    promoteItemTextElements(&theme->favsMainElems);
    promoteItemTextElements(&theme->favsInfoElems);
    promoteItemTextElements(&theme->vcdMainElems);
    promoteItemTextElements(&theme->vcdInfoElems);
    promoteItemTextElements(&theme->favsVcdMainElems);
    promoteItemTextElements(&theme->favsVcdInfoElems);
    promoteItemTextElements(&theme->favsAppsMainElems);
    promoteItemTextElements(&theme->favsAppsInfoElems);
}

// hd_autofit=1 in 16:9: move a 4:3-placed element so the 4:3 composition keeps its shape (thmLayoutAutoFitX).
// Left alone: elements the theme placed for 16:9 itself (<name>_x_hd / <name>_wsX), the background and
// anything spanning the screen, the views that lay themselves out (Coverflow, PSBBN Flow, Grid), and
// unscaled art, which 16:9 still draws at full width so no position would keep its shape.
static void thmAutoFitElem(config_set_t *themeConfig, theme_element_t *elem, const char *name, const char *type)
{
    static const int selfPlaced[] = {ELEM_TYPE_BACKGROUND, ELEM_TYPE_COVERFLOW, ELEM_TYPE_PSBBN_FLOW, ELEM_TYPE_GRID};
    // The renderer narrows these elements' glyphs, not their boxes: narrow the box to match.
    static const int textBoxes[] = {ELEM_TYPE_ATTRIBUTE_TEXT, ELEM_TYPE_STATIC_TEXT, ELEM_TYPE_GAME_COUNT_TEXT, ELEM_TYPE_MENU_TEXT, ELEM_TYPE_ITEMS_LIST, ELEM_TYPE_ITEM_TEXT};
    char key[64];
    const char *value;

    if (!thmParseAutoFit || elem == NULL || type == NULL)
        return;
    for (int t : selfPlaced) {
        if (!strcmp(elementsType[t], type))
            return;
    }
    snprintf(key, sizeof(key), "%s_x" THM_LAYOUT_SUFFIX_HD, name);
    if (configGetStr(themeConfig, key, &value))
        return;
    snprintf(key, sizeof(key), "%s" THM_LAYOUT_KEY_WSX, name);
    if (configGetStr(themeConfig, key, &value))
        return;
    if (elem->scaled == SCALING_NONE || elem->width >= gThmScreenWidth)
        return;

    elem->posX = thmLayoutAutoFitX(elem->posX, gThmScreenWidth);
    for (int t : textBoxes) {
        if (!strcmp(elementsType[t], type) && elem->width > 0)
            elem->width = elem->width * 3 / 4;
    }
}

static int addGUIElem(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *elems, const char *type, const char *name)
{
    int enabled = 1;
    char elemProp[64];
    theme_element_t *elem = NULL;

    snprintf(elemProp, sizeof(elemProp), "%s_enabled", name);
    configGetInt(themeConfig, elemProp, &enabled);

    if (enabled) {
        snprintf(elemProp, sizeof(elemProp), "%s_type", name);
        configGetStr(themeConfig, elemProp, &type);
        if (type) {
            if (!strcmp(elementsType[ELEM_TYPE_ATTRIBUTE_TEXT], type)) {
                elems->needsItemConfig = 1;
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initAttributeText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_STATIC_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initStaticText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_GAME_COUNT_TEXT], type)) {
                elems->needsItemConfig = 1;
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initGameCountText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_ATTRIBUTE_IMAGE], type)) {
                elems->needsItemConfig = 1;
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initAttributeImage(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_GAME_IMAGE], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGameImage(themePath, themeConfig, theme, elem, name, NULL, 1, NULL, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_STATIC_IMAGE], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initStaticImage(themePath, themeConfig, theme, elem, name, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_BACKGROUND], type)) {
                if (!elems->first) { // Background elem can only be the first one
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_BACKGROUND, 0, 0, ALIGN_NONE, gThmScreenWidth, gThmScreenHeight, SCALING_NONE, gDefaultCol, theme->fonts[0]);
                    initBackground(themePath, themeConfig, theme, elem, name, NULL, 1, NULL);
                }
            } else if (!strcmp(elementsType[ELEM_TYPE_MENU_ICON], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_MENU_ICON, gThmScreenWidth >> 1, 400, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                elem->drawElem = &drawMenuIcon;
            } else if (!strcmp(elementsType[ELEM_TYPE_MENU_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_MENU_TEXT, gThmScreenWidth >> 1, 20, ALIGN_CENTER, 200, 20, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                elem->drawElem = &drawMenuText;
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEMS_LIST], type)) {
                // A devices=-filtered ItemsList is a per-device OVERRIDE: it joins its family's
                // draw list but must NOT claim one of the four global nav slots below (games/apps/
                // favs/vcd are claimed in strict parse order; a filtered element in that chain
                // would corrupt the family-to-slot mapping and could silently drop later lists).
                // Navigation resolves it per page via thmResolveItemsList instead.
                char devProp[64];
                const char *devValue;
                snprintf(devProp, sizeof(devProp), "%s_devices", name);
                // A Favourites per-kind family (favsVcd* / favsApps*) owns no slot either: its lists take
                // this same slot-free path, filtered or not, so they can never displace the four
                // families' slot mapping. menusys resolves them per page via thmFamilyItemsList.
                if (theme->parsingFavKindFamily ||
                    (configGetStr(themeConfig, devProp, &devValue) && thmParseDeviceList(devValue, 1 /* initBasic re-parses and logs */) != 0)) {
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEMS_LIST, 42, 42, ALIGN_NONE, 400, 360, SCALING_RATIO, theme->titleColor, theme->fonts[0]);
                    initItemsList(themePath, themeConfig, theme, elem, name, NULL);
                    // Pre-existing quirk kept as-is: an UNFILTERED ItemsList parsed for an INFO
                    // family still claims the next nav slot below (thmLoad's info loops run after
                    // the main ones). Refusing info-family claims here would change behavior for
                    // themes that rely on it.
                } else if (!theme->gamesItemsList) {
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEMS_LIST, 0, 0, ALIGN_NONE, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->titleColor, theme->fonts[0]);
                    initItemsList(themePath, themeConfig, theme, elem, name, NULL);
                    theme->gamesItemsList = elem;
                } else if (!theme->appsItemsList) {
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEMS_LIST, 42, 42, ALIGN_NONE, 400, 360, SCALING_RATIO, theme->titleColor, theme->fonts[0]);
                    initItemsList(themePath, themeConfig, theme, elem, name, NULL);
                    theme->appsItemsList = elem;
                } else if (!theme->favsItemsList) {
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEMS_LIST, 42, 42, ALIGN_NONE, 400, 360, SCALING_RATIO, theme->titleColor, theme->fonts[0]);
                    initItemsList(themePath, themeConfig, theme, elem, name, NULL);
                    theme->favsItemsList = elem;
                } else if (!theme->vcdItemsList) {
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEMS_LIST, 42, 42, ALIGN_NONE, 400, 360, SCALING_RATIO, theme->titleColor, theme->fonts[0]);
                    initItemsList(themePath, themeConfig, theme, elem, name, NULL);
                    theme->vcdItemsList = elem; // 4th slot: the L3 VCD view's own items list (own cover cache)
                }
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEM_ICON], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, 64, 64, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGameImage(themePath, themeConfig, theme, elem, name, "ICO", 20, NULL, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEM_COVER], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGameImage(themePath, themeConfig, theme, elem, name, "COV", COVER_CACHE_SLOTS, NULL, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEM_TEXT], type)) {
                elems->needsVcdDisplayId = 1;
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEM_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->titleColor, theme->fonts[0]);
                elem->drawElem = &drawItemText;
            } else if (!strcmp(elementsType[ELEM_TYPE_HINT_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_HINT_TEXT, 16, -HINT_HEIGHT, ALIGN_NONE, 12, 20, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                elem->drawElem = &drawHintText;
            } else if (!strcmp(elementsType[ELEM_TYPE_INFO_HINT_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_INFO_HINT_TEXT, 16, -HINT_HEIGHT, ALIGN_NONE, 12, 20, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                int showRun = 1;
                snprintf(elemProp, sizeof(elemProp), "%s_show_run", name);
                configGetInt(themeConfig, elemProp, &showRun);
                elem->showRun = showRun != 0;
                elem->drawElem = &drawInfoHintText;
            } else if (!strcmp(elementsType[ELEM_TYPE_LOADING_ICON], type)) {
                if (!theme->loadingIcon)
                    theme->loadingIcon = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_LOADING_ICON, -40, -60, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
            } else if (!strcmp(elementsType[ELEM_TYPE_BDM_INDEX], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_BDM_INDEX, gThmScreenWidth >> 1, 355, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                elem->drawElem = &drawBDMIndex;
            } else if (!strcmp(elementsType[ELEM_TYPE_COVERFLOW], type)) {
                // GAME_IMAGE-backed (COV cache) so initMutableImage/findDuplicate work unchanged.
                // Allow one per list (e.g. a games "main" coverflow + an "appsMain" coverflow,
                // like wOPL); initCoverflow points gTheme->coverflow at the FIRST as the active flag.
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, gThmScreenWidth >> 1, gThmScreenHeight >> 1, ALIGN_CENTER, 150, 210, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initCoverflow(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_PSBBN_FLOW], type)) {
                // x/y place the focal cover's centre: right of centre, mid-height, as in PSBBN.
                int size = psbbnFlowSize(gThmScreenWidth, gThmScreenHeight);
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, gThmScreenWidth - 186, gThmScreenHeight >> 1, ALIGN_CENTER, size, size, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initPsbbnFlow(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_GRID], type)) {
                // x/y place the grid's centre, width/height one cover (GAME_IMAGE-backed like Coverflow).
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, gThmScreenWidth >> 1, gThmScreenHeight >> 1, ALIGN_CENTER, 80, 112, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGrid(themePath, themeConfig, theme, elem, name);
                if (elem->drawElem)
                    elems->hasGrid = 1;
            } else if (!strcmp(elementsType[ELEM_TYPE_SAVE_ICON], type)) {
                elems->needsSaveIconConfig = 1;
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_SAVE_ICON, gThmScreenWidth >> 1, gThmScreenHeight >> 1, ALIGN_CENTER, 64, 64, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                elem->drawElem = &drawSaveIcon;
            }

            if (elem) {
                thmAutoFitElem(themeConfig, elem, name, type);
                elem->family = elems;

                if (!elems->first)
                    elems->first = elem;

                if (!elems->last)
                    elems->last = elem;
                else {
                    elems->last->next = elem;
                    elems->last = elem;
                }
            }
        } else
            return 0; // ends the reading of elements
    }

    return 1;
}

// How addGUIElem treats block <name>, without building it: 0 when absent (a fallback chain moves on),
// 1 when present -- declared with a _type, or switched off with <name>_enabled=0 (present, builds
// nothing). *claimsSlot is set when the block is an unfiltered ItemsList, i.e. one that takes a global
// nav slot in a slot family (or is dropped once all four are taken).
static int thmBlockPresent(config_set_t *themeConfig, const char *name, int *claimsSlot)
{
    char key[64];
    const char *type = NULL;
    const char *devValue;
    int enabled = 1;

    *claimsSlot = 0;
    snprintf(key, sizeof(key), "%s_enabled", name);
    configGetInt(themeConfig, key, &enabled);
    if (!enabled)
        return 1;
    snprintf(key, sizeof(key), "%s_type", name);
    if (!configGetStr(themeConfig, key, &type) || type == NULL)
        return 0;
    if (!strcmp(elementsType[ELEM_TYPE_ITEMS_LIST], type)) {
        snprintf(key, sizeof(key), "%s_devices", name);
        *claimsSlot = !(configGetStr(themeConfig, key, &devValue) && thmParseDeviceList(devValue, 1) != 0);
    }
    return 1;
}

// True when the theme declares at least one <prefix><j> block (j < count) -- the opt-in test for the
// Favourites per-kind families. Same presence rule as addGUIElem, so a slot the theme only switches off
// (<prefix><j>_enabled=0, to hide it on that Favourites view) opts in too.
static int thmFavKindDeclared(config_set_t *themeConfig, const char *prefix, int count)
{
    char name[64];
    int j, claimsSlot;

    for (j = 0; j < count; j++) {
        if (thmViewResolveSlot(themeConfig, thmParseViewPrefix, thmParseViewInherit, &prefix, 1, j, name, sizeof(name)) && thmBlockPresent(themeConfig, name, &claimsSlot))
            return 1;
    }
    return 0;
}

// Did the slot family `today` (the family this Favourites view renders from before the per-kind family
// existed, parsed with the same chain) keep its unfiltered ItemsList at slot j? A slot family builds such
// a list only while a nav slot is free, and slots never free up during a family's parse, so the lists it
// kept are the FIRST ones in slot order: slot j's list survived iff fewer earlier slots resolve to one
// than the family holds.
static int thmSlotListKept(config_set_t *themeConfig, theme_elems_t *today, const char *const *chain, int chainLen, int j)
{
    char path[64];
    theme_element_t *e;
    int k, claimsSlot, earlier = 0, kept = 0;

    for (e = today->first; e != NULL; e = e->next) {
        if (e->type == ELEM_TYPE_ITEMS_LIST && !e->deviceFilter)
            kept++;
    }
    for (k = 0; k < j; k++) {
        if (thmViewResolveSlot(themeConfig, thmParseViewPrefix, thmParseViewInherit, chain, chainLen, k, path, sizeof(path)) && thmBlockPresent(themeConfig, path, &claimsSlot))
            earlier += claimsSlot;
    }
    return earlier < kept;
}

// Parse one slot of a Favourites per-kind family: the declared <prefix><j> block if present, otherwise
// the first present block of the fallback chain, whose copy is marked inherited. The chain is exactly
// the one `today` (the family that view renders from today) was parsed with, so an undeclared slot
// reproduces today's element -- including an unfiltered ItemsList today's family had to DROP for lack of
// a free nav slot: that copy is skipped too, rather than appearing only on the Favourites view.
static void addFavKindSlot(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *elems, theme_elems_t *today, const char *prefix, int j, const char *const *chain, int chainLen)
{
    char path[64];
    int claimsSlot;

    if (thmViewResolveSlot(themeConfig, thmParseViewPrefix, thmParseViewInherit, &prefix, 1, j, path, sizeof(path))) {
        addGUIElem(themePath, themeConfig, theme, elems, NULL, path);
        return;
    }
    if (!thmViewResolveSlot(themeConfig, thmParseViewPrefix, thmParseViewInherit, chain, chainLen, j, path, sizeof(path)))
        return;
    thmBlockPresent(themeConfig, path, &claimsSlot);
    if (claimsSlot && !thmSlotListKept(themeConfig, today, chain, chainLen, j))
        return;
    theme_element_t *before = elems->last;
    addGUIElem(themePath, themeConfig, theme, elems, NULL, path);
    theme_element_t *e = before ? before->next : elems->first;
    for (; e != NULL; e = e->next)
        e->inherited = 1;
}

// Parse block j of a slot family through its fallback chain in the view being built (see
// thmViewResolveSlot for the order). 0 when no block of the chain is present.
static int addGUIElemSlot(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *elems, const char *const *chain, int chainLen, int j)
{
    char path[THM_VIEW_BLOCK_NAME_LEN];

    if (!thmViewResolveSlot(themeConfig, thmParseViewPrefix, thmParseViewInherit, chain, chainLen, j, path, sizeof(path)))
        return 0;
    addGUIElem(themePath, themeConfig, theme, elems, NULL, path);
    return 1;
}

static void freeGUIElems(theme_elems_t *elems)
{
    theme_element_t *elem = elems->first;
    while (elem) {
        elems->first = elem->next;
        elem->endElem(elem);
        elem = elems->first;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

GSTEXTURE *thmGetTexture(unsigned int id)
{
    if (id >= TEXTURES_COUNT)
        return NULL;
    else {
        // see if the texture is valid
        GSTEXTURE *txt = &gTheme->textures[id];

        if (txt->Mem)
            return txt;
        else
            return NULL;
    }
}

static void thmFree(theme_t *theme)
{
    if (theme) {
        // free elements
        freeGUIElems(&theme->mainElems);
        freeGUIElems(&theme->infoElems);
        freeGUIElems(&theme->appsMainElems);
        freeGUIElems(&theme->appsInfoElems);
        freeGUIElems(&theme->favsMainElems);
        freeGUIElems(&theme->favsInfoElems);
        freeGUIElems(&theme->vcdMainElems);
        freeGUIElems(&theme->vcdInfoElems);
        freeGUIElems(&theme->favsVcdMainElems);
        freeGUIElems(&theme->favsVcdInfoElems);
        freeGUIElems(&theme->favsAppsMainElems);
        freeGUIElems(&theme->favsAppsInfoElems);

        // free textures
        GSTEXTURE *texture;
        int id = 0;
        for (; id < TEXTURES_COUNT; id++) {
            texture = &theme->textures[id];
            if (texture->Mem != NULL) {
                rmUnloadTexture(texture);
                texFree(texture);
            }
        }

        // free fonts
        for (id = 0; id < THM_MAX_FONTS; ++id)
            fntRelease(theme->fonts[id]);

        // The loading icon is stored outside the four managed element lists, so
        // free it explicitly (endBasic) to avoid leaking it on every theme reload.
        if (theme->loadingIcon)
            theme->loadingIcon->endElem(theme->loadingIcon);

        free(theme);
    }
}

int ThemeCatalog::readEntry(int index, const char *path, const char *separator, const char *name, unsigned char d_type)
{
    // #152: don't trust d_type alone. It is derived from iomanX FIO_S_IFDIR mode bits, but MMCE
    // card FIRMWARE fills stat.mode verbatim over the wire (mmceman copies it through), and clone
    // firmwares (e.g. Bitfunx PSxMemCard) may not speak the FIO_S_* dialect -- every entry then
    // degrades to DT_UNKNOWN. That is why languages still listed (lngReadEntry accepts != DT_DIR)
    // while themes vanished (this required == DT_DIR exactly). For a thm_-prefixed candidate that
    // is not positively DT_DIR, confirm with an opendir() probe: it succeeds only for directories
    // on every driver regardless of mode-bit dialect, and runs only for THM-folder candidates.
    //
    // The prefix matches in ANY case: a PC share on a case-sensitive server keeps "THM_Name" exactly as
    // typed, and the name after it is used as-is.
    int isDir = (d_type == DT_DIR);
    if (!isDir && strncasecmp(name, "thm_", 4) == 0) {
        char probe[256];
        snprintf(probe, sizeof(probe), "%s%s%s", path, separator, name);
        DIR *pd = opendir(probe);
        if (pd != NULL) {
            closedir(pd);
            isDir = 1;
        }
    }

    // strncmp, not upstream's strstr: the name parse below assumes the prefix is at the START
    // (name + 4), so a mid-string "thm_" match always produced a garbage theme name anyway.
    if (isDir && strncasecmp(name, "thm_", 4) == 0) {
        theme_file_t *currTheme = &themes_[count_ + index];

        int length = strlen(name) - 4 + 1;
        currTheme->name = (char *)malloc(length * sizeof(char));
        memcpy(currTheme->name, name + 4, length);
        currTheme->name[length - 1] = '\0';

        length = strlen(path) + 1 + strlen(name) + 1 + 1;
        currTheme->filePath = (char *)malloc(length * sizeof(char));
        sprintf(currTheme->filePath, "%s%s%s%s", path, separator, name, separator);

        LOG("THEMES Theme found: %s\n", currTheme->filePath);

        index++;
    }
    return index;
}

/* themePath must contains the leading separator (as it is dependent of the device, we can't know here) */
static int thmLoadResource(GSTEXTURE *texture, int texId, const char *themePath, short psm, int useDefault)
{
    int success = -1;

    if (themePath != NULL)
        success = texDiscoverLoad(texture, themePath, texId); // only set success here

    if ((success < 0) && useDefault)
        texLoadInternal(texture, texId); // we don't mind the result of "default"

    return success;
}

// The other screens' elements (info, apps, favourites, PS1 and their info pages) are re-coloured only where they
// still carry the colour the settings gave them, so an element whose colour the theme cfg sets keeps it. Images
// keep theirs too. Without this a Colours change reached the main list alone and the other screens kept the old
// colour until the theme was loaded again (YAOPL d21b3bc5, Roman Kelesidis).
static void thmRecolorFollowers(theme_elems_t *elems, u64 oldText, u64 oldTitle, const theme_t *theme)
{
    for (theme_element_t *elem = elems->first; elem != NULL; elem = elem->next) {
        if (elem->type == ELEM_TYPE_ATTRIBUTE_IMAGE || elem->type == ELEM_TYPE_GAME_IMAGE || elem->type == ELEM_TYPE_STATIC_IMAGE ||
            elem->type == ELEM_TYPE_BACKGROUND || elem->type == ELEM_TYPE_MENU_ICON || elem->type == ELEM_TYPE_BDM_INDEX)
            continue;
        if (elem->color == oldTitle && (elem->type == ELEM_TYPE_ITEMS_LIST || elem->type == ELEM_TYPE_ITEM_TEXT))
            elem->color = theme->titleColor;
        else if (elem->color == oldText)
            elem->color = theme->textColor;
    }
}

static void thmSetColors(theme_t *theme)
{
    const u64 oldText = theme->textColor, oldTitle = theme->titleColor;

    memcpy(theme->bgColor, gDefaultBgColor, 3);
    memcpy(theme->plasBlendColor, gDefaultPlasBlendColor, 3);
    theme->textColor = GS_SETREG_RGBA(gDefaultTextColor[0], gDefaultTextColor[1], gDefaultTextColor[2], 0x80);
    theme->uiTextColor = GS_SETREG_RGBA(gDefaultUITextColor[0], gDefaultUITextColor[1], gDefaultUITextColor[2], 0x80);
    theme->titleColor = GS_SETREG_RGBA(gDefaultTitleColor[0], gDefaultTitleColor[1], gDefaultTitleColor[2], 0x80);
    theme->selTextColor = GS_SETREG_RGBA(gDefaultSelTextColor[0], gDefaultSelTextColor[1], gDefaultSelTextColor[2], 0x80);

    /* Re-colour the LIVE elements. This runs against a theme whose elements already exist, which
       is what lets the Colours page apply without a reload -- and it is why the title picker has
       to be honoured HERE as well as at element construction.

       Game titles take titleColor, everything else textColor (#464). initBasic() already hands the
       right colour to each element when a theme loads, but this loop forced textColor onto every
       element, so the moment the user touched ANY colour the title choice was overwritten. The row
       saved its value and the list went straight back to the general text colour -- which reads
       exactly as "the option does nothing".

       ITEMS_LIST is the game list in List mode; ITEM_TEXT is the title under the cover in
       Coverflow. Both ARE the game title, so both follow the title picker. */
    theme_element_t *elem = theme->mainElems.first;
    while (elem) {
        elem->color = (elem->type == ELEM_TYPE_ITEMS_LIST || elem->type == ELEM_TYPE_ITEM_TEXT) ? theme->titleColor : theme->textColor;
        elem = elem->next;
    }

    theme_elems_t *others[11] = {&theme->infoElems, &theme->appsMainElems, &theme->appsInfoElems, &theme->favsMainElems,
                                 &theme->favsInfoElems, &theme->vcdMainElems, &theme->vcdInfoElems, &theme->favsVcdMainElems,
                                 &theme->favsVcdInfoElems, &theme->favsAppsMainElems, &theme->favsAppsInfoElems};
    for (int g = 0; g < 11; g++)
        thmRecolorFollowers(others[g], oldText, oldTitle, theme);
}

static void thmLoadFonts(config_set_t *themeConfig, const char *themePath, theme_t *theme)
{
    int fntID; // theme side font id, not the fntSys handle
    for (fntID = 0; fntID < THM_MAX_FONTS; ++fntID) {
        // does the font by the key exist?
        char fntKey[16];

        if (fntID == 0) {
            snprintf(fntKey, sizeof(fntKey), "default_font");
            theme->fonts[0] = FNT_DEFAULT;
        } else {
            snprintf(fntKey, sizeof(fntKey), "font%d", fntID);
            theme->fonts[fntID] = theme->fonts[0];
        }

        char fullPath[128];
        const char *fntFile;
        if (configGetStr(themeConfig, fntKey, &fntFile)) {
            snprintf(fullPath, sizeof(fullPath), "%s%s", themePath, fntFile);

            int fontSize;
            char sizeKey[64];
            if (fntID == 0)
                snprintf(sizeKey, sizeof(sizeKey), "default_font_size");
            else
                snprintf(sizeKey, sizeof(sizeKey), "font%d_size", fntID);

            if (!configGetInt(themeConfig, sizeKey, &fontSize) || fontSize <= 0)
                fontSize = FNTSYS_DEFAULT_SIZE;

            int fntHandle = fntLoadFile(fullPath, fontSize);
            // Do we have a valid font? Assign the font handle to the theme font slot
            if (fntHandle != FNT_ERROR) {
                theme->fonts[fntID] = fntHandle;
                // <font>_weight=100..900, the weight it is drawn at (RIPPS2 build 114, akilluminati47)
                int weight = 0;
                if (fntID == 0)
                    snprintf(sizeKey, sizeof(sizeKey), "default_font_weight");
                else
                    snprintf(sizeKey, sizeof(sizeKey), "font%d_weight", fntID);
                if (configGetInt(themeConfig, sizeKey, &weight) && weight >= 100 && weight <= 900)
                    fntSetWeight(fntHandle, weight);
            }
        }
    }
}

// Bound the MMCE-art drain before a theme swap. On timeout the current theme
// remains active and a later trigger can retry.
constexpr int THM_MMCE_ART_ABORT_WAIT_TICKS = 500;

// Returns 0 on success, -1 when the load was ABANDONED because the art worker would not drain (a
// wedged MMCE read holding the shared fileXio channel). On -1 the current theme stays untouched;
// callers must not commit the new theme id, so a later trigger (another device's theme scan, a
// settings re-apply) naturally retries once the card recovers.
/*
  Button glyph sets (RIPPS2 build 90, akilluminati47). A set fills every glyph slot the hints draw: the theme's own
  PNGs (OPL's where it has none and use_default allows), OPL's built-in ones, or Standard / White, built in from
  RIPPS2's gfx/glyph_<set>_<name>.png (its make_glyph_sets.py draws Standard from the Adapt theme and White from the
  Grunge theme, both RIPPS2's own).
*/
#define GLYPH_PNG(set, n)                           \
    extern unsigned char glyph_##set##_##n##_png[]; \
    extern unsigned int size_glyph_##set##_##n##_png
#define GLYPH_SET_PNGS(set)   \
    GLYPH_PNG(set, cross);    \
    GLYPH_PNG(set, circle);   \
    GLYPH_PNG(set, square);   \
    GLYPH_PNG(set, triangle); \
    GLYPH_PNG(set, start);    \
    GLYPH_PNG(set, select);   \
    GLYPH_PNG(set, L1);       \
    GLYPH_PNG(set, R1);       \
    GLYPH_PNG(set, L3);       \
    GLYPH_PNG(set, R3);       \
    GLYPH_PNG(set, left);     \
    GLYPH_PNG(set, right);
GLYPH_SET_PNGS(standard)
GLYPH_SET_PNGS(white)

static const int glyphSlots[] = {CROSS_ICON, CIRCLE_ICON, SQUARE_ICON, TRIANGLE_ICON, START_ICON, SELECT_ICON,
                                 L1_ICON, R1_ICON, L3_ICON, R3_ICON, LEFT_ICON, RIGHT_ICON};
#define GLYPH_SLOTS ((int)(sizeof(glyphSlots) / sizeof(glyphSlots[0])))
#define GLYPH_ENTRY(set, n)                                    \
    {                                                          \
        glyph_##set##_##n##_png, &size_glyph_##set##_##n##_png \
    }
#define GLYPH_ROW(set)                                                                                           \
    {                                                                                                            \
        GLYPH_ENTRY(set, cross), GLYPH_ENTRY(set, circle), GLYPH_ENTRY(set, square), GLYPH_ENTRY(set, triangle), \
            GLYPH_ENTRY(set, start), GLYPH_ENTRY(set, select), GLYPH_ENTRY(set, L1), GLYPH_ENTRY(set, R1),       \
            GLYPH_ENTRY(set, L3), GLYPH_ENTRY(set, R3), GLYPH_ENTRY(set, left), GLYPH_ENTRY(set, right)          \
    }
static const struct
{
    const unsigned char *png;
    const unsigned int *size;
} glyphSetPng[2][GLYPH_SLOTS] = {GLYPH_ROW(standard), GLYPH_ROW(white)}; // [set - GLYPH_SET_STANDARD][slot]

static const char *const glyphSetKeys[GLYPH_SET_COUNT] = {"theme", "builtin", "standard", "white"};

int thmGlyphSetParse(const char *name)
{
    int i;
    for (i = 0; name != NULL && i < GLYPH_SET_COUNT; i++)
        if (!strcasecmp(name, glyphSetKeys[i]))
            return i;
    return GLYPH_SET_THEME;
}

int thmGlyphSetFor(int themeSet)
{
    return gGlyphSet > GLYPH_SET_THEME && gGlyphSet < GLYPH_SET_COUNT ? gGlyphSet : themeSet;
}

// Fill a theme's glyph slots from a set. onGui: the textures may have been drawn (VRAM copies to drop), which is
// only ever done on the GUI thread; a theme still being built has never been drawn.
static void thmGlyphSetLoad(theme_t *theme, int set, int onGui)
{
    int i;
    for (i = 0; i < GLYPH_SLOTS; i++) {
        GSTEXTURE *tex = &theme->textures[glyphSlots[i]];
        if (tex->Mem != NULL) {
            if (onGui)
                rmUnloadTexture(tex);
            texFree(tex);
        }
        if (set == GLYPH_SET_STANDARD || set == GLYPH_SET_WHITE)
            texLoadFromMemory(tex, glyphSetPng[set - GLYPH_SET_STANDARD][i].png, *glyphSetPng[set - GLYPH_SET_STANDARD][i].size);
        else if (set == GLYPH_SET_BUILTIN)
            texLoadInternal(tex, glyphSlots[i]);
        else
            thmLoadResource(tex, glyphSlots[i], theme->glyphPath[0] ? theme->glyphPath : NULL, GS_PSM_CT32, theme->useDefault);
    }
    theme->glyphSetLoaded = set;
}

void thmGlyphSetRefresh(void)
{
    if (gTheme != NULL && thmGlyphSetFor(gTheme->glyphSetDef) != gTheme->glyphSetLoaded)
        thmGlyphSetLoad(gTheme, thmGlyphSetFor(gTheme->glyphSetDef), 1);
}

// The hint bar's theme keys (RIPPS2 build 90, akilluminati47). A hint is named by what it does, or by its button.
static const char *const hintKeys[THM_HINT_COUNT] = {"menu", "run", "info", "options", "refresh", "star", "view"};
static const struct
{
    const char *name;
    int token;
} hintAliases[] = {{"start", THM_HINT_MENU}, {"select", THM_HINT_REFRESH}, {"r3", THM_HINT_STAR}, {"l3", THM_HINT_VIEW}, {"cross", THM_HINT_RUN}, {"circle", THM_HINT_RUN}, {"square", THM_HINT_INFO}, {"triangle", THM_HINT_OPTIONS}};

int thmHintToken(int iconId)
{
    switch (iconId) {
        case START_ICON:
            return THM_HINT_MENU;
        case SQUARE_ICON:
            return THM_HINT_INFO;
        case TRIANGLE_ICON:
            return THM_HINT_OPTIONS;
        case SELECT_ICON:
            return THM_HINT_REFRESH;
        case R3_ICON:
            return THM_HINT_STAR;
        case L3_ICON:
            return THM_HINT_VIEW;
        default:
            return THM_HINT_RUN; // the confirm button: Run, or Start Device
    }
}

static int thmHintByName(const char *s, int len)
{
    int i;
    for (i = 0; i < THM_HINT_COUNT; i++)
        if ((int)strlen(hintKeys[i]) == len && !strncasecmp(s, hintKeys[i], len))
            return i;
    for (i = 0; i < (int)(sizeof(hintAliases) / sizeof(hintAliases[0])); i++)
        if ((int)strlen(hintAliases[i].name) == len && !strncasecmp(s, hintAliases[i].name, len))
            return hintAliases[i].token;
    return -1;
}

int thmHintParse(const char *list, int *out, int max)
{
    int n = 0, seen = 0;
    while (list != NULL && *list && n < max) {
        while (*list == ',' || *list == ' ')
            list++;
        const char *end = list;
        while (*end && *end != ',' && *end != ' ')
            end++;
        int t = thmHintByName(list, (int)(end - list));
        if (t >= 0 && !(seen & (1 << t))) { // a name twice keeps its first place
            seen |= 1 << t;
            out[n++] = t;
        }
        list = end;
    }
    return n;
}

int thmHintRank(const theme_t *theme, int token)
{
    int i;
    for (i = 0; theme != NULL && i < theme->hintsOrderCount; i++)
        if (theme->hintsOrder[i] == token)
            return i;
    return THM_HINT_COUNT;
}

// The built-in <OPL> theme's cfg for a view: the template a fallback view takes its presenter from.
static void thmReadViewTemplate(config_set_t *cfg, int view)
{
    if (view == THM_VIEW_COVERFLOW)
        configReadBuffer(cfg, &theme_coverflow_cfg, size_theme_coverflow_cfg);
    else if (view == THM_VIEW_PSBBN)
        configReadBuffer(cfg, &theme_psbbn_flow_cfg, size_theme_psbbn_flow_cfg);
    else if (view == THM_VIEW_GRID)
        configReadBuffer(cfg, &theme_grid_cfg, size_theme_grid_cfg);
    else
        configReadBuffer(cfg, &conf_theme_OPL_cfg, size_conf_theme_OPL_cfg);
}

static int thmLoad(const char *themePath)
{
    LOG("THEMES Load theme path=%s\n", themePath);
    char path[256];
    theme_t *curT = gTheme;
    theme_t *newT = (theme_t *)malloc(sizeof(theme_t));
    if (newT == NULL) {
        // Same contract as an abandoned load: the current theme stays, a later trigger retries.
        LOG("THEMES Load: out of memory for the theme struct -- keeping the current theme\n");
        return -1;
    }
    memset(newT, 0, sizeof(theme_t));

    newT->useDefault = 1;
    newT->usedHeight = 480;
    thmSetColors(newT);
    newT->mainElems.first = NULL;
    newT->mainElems.last = NULL;
    newT->infoElems.first = NULL;
    newT->infoElems.last = NULL;
    newT->appsMainElems.first = NULL;
    newT->appsMainElems.last = NULL;
    newT->appsInfoElems.first = NULL;
    newT->appsInfoElems.last = NULL;
    newT->favsMainElems.first = NULL;
    newT->favsMainElems.last = NULL;
    newT->favsInfoElems.first = NULL;
    newT->favsInfoElems.last = NULL;
    newT->vcdMainElems.first = NULL;
    newT->vcdMainElems.last = NULL;
    newT->vcdInfoElems.first = NULL;
    newT->vcdInfoElems.last = NULL;
    newT->favsVcdMainElems.first = NULL;
    newT->favsVcdMainElems.last = NULL;
    newT->favsVcdInfoElems.first = NULL;
    newT->favsVcdInfoElems.last = NULL;
    newT->favsAppsMainElems.first = NULL;
    newT->favsAppsMainElems.last = NULL;
    newT->favsAppsInfoElems.first = NULL;
    newT->favsAppsInfoElems.last = NULL;
    newT->parsingFavKindFamily = 0;
    newT->gameCacheCount = 0;
    newT->artGeneration = ++thmLoadCount;
    newT->itemsList = NULL;
    newT->gamesItemsList = NULL;
    newT->appsItemsList = NULL;
    newT->favsItemsList = NULL;
    newT->vcdItemsList = NULL;
    newT->coverflow = NULL;
    newT->psbbnFlow = NULL;
    newT->grid = NULL;
    newT->coverflowCoverOffset = 0;
    newT->loadingIcon = NULL;
    newT->loadingIconCount = LOAD7_ICON - LOAD0_ICON + 1;

    // Do not begin blocking theme reads while an MMCE art read still owns the
    // shared fileXio channel. Keep the current theme if it will not drain.
    if (!cacheAbortMmceImageLoadsTimed(THM_MMCE_ART_ABORT_WAIT_TICKS) && curT != NULL) {
        LOG("THEMES Load: MMCE art worker wedged -- keeping the current theme (retry later)\n");
        free(newT);
        return -1;
    }

    config_set_t *themeConfig = NULL;
    // The built-in <OPL> theme offers its List, Coverflow, PSBBN Flow and Grid looks as views (Coverflow and
    // PSBBN Flow were the separate <Coverflow> and <PSBBN Flow> themes). Each view is its own embedded cfg, because an edition
    // such as Korium replaces some of them, so the view picks the buffer.
    int builtinView = -1;
    if (!themePath) {
        // No theme specified. Prepare and load the default theme.
        themeConfig = configAlloc(0, NULL, NULL);
        builtinView = (gThemeViewRequest >= 0 && gThemeViewRequest < THM_VIEW_COUNT) ? gThemeViewRequest : THM_VIEW_LIST;
        if (builtinView == THM_VIEW_COVERFLOW)
            configReadBuffer(themeConfig, &theme_coverflow_cfg, size_theme_coverflow_cfg);
        else if (builtinView == THM_VIEW_PSBBN)
            configReadBuffer(themeConfig, &theme_psbbn_flow_cfg, size_theme_psbbn_flow_cfg);
        else if (builtinView == THM_VIEW_GRID)
            configReadBuffer(themeConfig, &theme_grid_cfg, size_theme_grid_cfg);
        else
            configReadBuffer(themeConfig, &conf_theme_OPL_cfg, size_conf_theme_OPL_cfg);
    } else {
        snprintf(path, sizeof(path), "%sconf_theme.cfg", themePath);
        themeConfig = configAlloc(0, NULL, path);
        configRead(themeConfig); // try to load the theme config file. If it does not exist, defaults will be used.
    }

    // SD/HD layouts: copy the _sd or _hd variants (and uOPL/wOPL _wsX) over their plain keys before anything
    // reads the cfg, so the rest of the load sees one layout. Themes without them are untouched.
    newT->layoutWide = rmIsWideDisplay();
    newT->layoutDependsOnWide = thmApplyDisplayLayout(themeConfig, newT->layoutWide);

    // Pick the view to build (docs/MULTI-VIEW-THEMES-PLAN.md). Only that view is parsed; switching views
    // reloads the theme. A theme that declares no "<view>." keys parses exactly as before.
    thm_view_info_t viewInfo;
    thmViewScan(themeConfig, &viewInfo);
    newT->view = thmViewChoose(&viewInfo, gThemeViewRequest);
    newT->viewMask = viewInfo.mask;
    newT->defaultView = viewInfo.defaultView;
    // Fallback views: a disk theme without views= offers every view. One it does not declare is built from
    // its own layout with the built-in template's presenter (thmViewSynthesize). views= keeps a theme to the
    // views it names.
    if (themePath != NULL && !viewInfo.listed) {
        unsigned int offered = (1u << THM_VIEW_COUNT) - 1;
        int requested = gThemeViewRequest;
        if (requested >= 0 && requested < THM_VIEW_COUNT && !(viewInfo.mask & (1u << requested))) {
            int built = 0;
            config_set_t *tmpl = configAlloc(0, NULL, NULL);
            if (tmpl != NULL) {
                thmReadViewTemplate(tmpl, requested);
                // The template's own SD/HD keys (the built-in Grid's columns_hd) resolve for this picture too.
                if (thmApplyDisplayLayout(tmpl, newT->layoutWide))
                    newT->layoutDependsOnWide = 1;
                built = thmViewSynthesize(themeConfig, tmpl, requested);
                configFree(tmpl);
            }
            if (built) {
                viewInfo.blocksMask |= 1u << requested; // its blocks now exist under "<view>."
                newT->view = requested;
                LOG("THEMES View %s: not declared by the theme, built from its layout\n", thmViewName(requested));
            } else {
                offered &= ~(1u << requested); // stays on the default view; L3 must not keep asking for it
            }
        }
        newT->viewMask = offered;
    }
    if (builtinView >= 0) {
        newT->view = builtinView;
        newT->viewMask = (1u << THM_VIEW_LIST) | (1u << THM_VIEW_COVERFLOW) | (1u << THM_VIEW_PSBBN) | (1u << THM_VIEW_GRID);
        newT->defaultView = THM_VIEW_LIST;
    }
    const char *viewPrefix = thmViewPrefix(&viewInfo, newT->view);
    thmParseViewPrefix = viewPrefix;
    thmParseViewInherit = thmViewInheritsBase(&viewInfo, newT->view);
    LOG("THEMES View %s (offered mask 0x%x, native %s, prefix '%s')\n", thmViewName(newT->view), newT->viewMask, thmViewName(viewInfo.native), viewPrefix);

    int autoFit = 0;
    thmViewGetInt(themeConfig, viewPrefix, THM_LAYOUT_KEY_AUTOFIT, &autoFit);
    if (autoFit)
        newT->layoutDependsOnWide = 1;
    thmParseAutoFit = autoFit && newT->layoutWide;
    LOG("THEMES Layout %s (mode variants %d, hd_autofit %d)\n", newT->layoutWide ? "16:9" : "4:3", newT->layoutDependsOnWide, autoFit);

    int intValue;
    if (thmViewGetInt(themeConfig, viewPrefix, "use_default", &intValue))
        newT->useDefault = intValue;

    if (thmViewGetInt(themeConfig, viewPrefix, "use_real_height", &intValue)) {
        if (intValue)
            newT->usedHeight = gThmScreenHeight;
    }

    // THE TWO BUILT-IN THEMES MUST NOT CARRY THESE KEYS. guiShowColorsConfig offers <OPL> (id 0)
    // and <Coverflow> as EDITABLE and stores their palette in gDefault*Color, so for those two the
    // settings picker is the source of truth. thmSetColors() above has already applied it, and
    // every override below silently beats it on each load -- which turns the picker into a control
    // that appears to do nothing. That was issue #574: <Coverflow>'s embedded cfg set all four, so
    // its colours reverted on every mode switch, and only re-opening the Colours page (which
    // re-runs thmSetColors on the live theme, without a reload) put them back. <OPL>'s cfg has
    // never carried them, which is exactly why List mode was unaffected.
    //
    // DISK themes are the opposite case and keep these overrides: their colours belong to the theme
    // file, which is why the picker is greyed out read-only for them (issue #537).
    thmViewGetColor(themeConfig, viewPrefix, "bg_color", newT->bgColor);
    // absent key leaves the thmSetColors default (settings picker value; black out of the box)
    thmViewGetColor(themeConfig, viewPrefix, CONFIG_OPL_PLAS_BLEND_COLOR, newT->plasBlendColor);

    unsigned char color[3];
    if (thmViewGetColor(themeConfig, viewPrefix, "text_color", color))
        newT->textColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    if (thmViewGetColor(themeConfig, viewPrefix, "ui_text_color", color))
        newT->uiTextColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    if (thmViewGetColor(themeConfig, viewPrefix, "title_color", color))
        newT->titleColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    if (thmViewGetColor(themeConfig, viewPrefix, "sel_text_color", color))
        newT->selTextColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    if (thmViewGetInt(themeConfig, viewPrefix, "coverflow_cover_offset", &intValue)) {
        // clamp -- this feeds coverWidth * offset / 256 in drawCoverFlow; an
        // unbounded value from an untrusted theme .cfg would signed-overflow that
        // multiply (Codex audit, Low 1). The range is far wider than any real theme.
        if (intValue < -1024)
            intValue = -1024;
        else if (intValue > 1024)
            intValue = 1024;
        newT->coverflowCoverOffset = intValue;
    }

    // before loading the element definitions, we have to have the fonts prepared
    // for that, we load the fonts and a translation table
    if (themePath)
        thmLoadFonts(themeConfig, themePath, newT);

    // Every family resolves block j through its fallback chain, the active view's "<view>." blocks first
    // (thmViewResolveSlot). The chains are the ones these families always used.
    static const char *const mainChain[] = {"main"};
    static const char *const appsMainChain[] = {"appsMain", "main"};
    static const char *const favsMainChain[] = {"favsMain", "main"};
    static const char *const vcdMainChain[] = {"vcdMain", "appsMain", "main"};
    static const char *const infoChain[] = {"info"};
    static const char *const appsInfoChain[] = {"appsInfo", "info"};
    static const char *const favsInfoChain[] = {"favsInfo", "info"};
    static const char *const vcdInfoChain[] = {"vcdInfo", "info"};

    // The base list runs until the first absent block. i ends one past the count, as it always has, so
    // the kind loops below also try a kind block at the slot right after the last main block.
    int i, j;
    for (i = 0; addGUIElemSlot(themePath, themeConfig, newT, &newT->mainElems, mainChain, 1, i); i++)
        ;
    i++;

    for (j = 0; j < i; j++)
        addGUIElemSlot(themePath, themeConfig, newT, &newT->appsMainElems, appsMainChain, 2, j);

    // Favourites family: favsMain<j> override, else fall back to main<j> (identical to appsMain).
    // Runs after appsMain so a favsMain ItemsList claims the 3rd slot (favsItemsList), and before
    // the info passes so an info ItemsList never steals it.
    for (j = 0; j < i; j++)
        addGUIElemSlot(themePath, themeConfig, newT, &newT->favsMainElems, favsMainChain, 2, j);

    // VCD/PS1 view main family: vcdMain<j> override, else fall back to appsMain<j> (the square box),
    // else main<j>. The apps fallback means a theme that defines no vcdMain still gets the square VCD
    // look it had before this family existed. An ItemsList reached via this fallback claims the 4th
    // slot (vcdItemsList) in addGUIElem, giving the VCD list its OWN cover cache (separateVcdCoverCache).
    for (j = 0; j < i; j++)
        addGUIElemSlot(themePath, themeConfig, newT, &newT->vcdMainElems, vcdMainChain, 3, j);

    // Favourites per-kind MAIN families -- opt-in, built only when the theme declares a block of them.
    // favsVcdMain<j> falls back along vcdMain's own chain (the Favourites PS1 view renders vcdMain today);
    // favsAppsMain<j> falls back along favsMain's (the Favourites ELF view renders favsMain today). Parsed
    // after the four slot families with slot claiming off, so they can never take an ItemsList slot.
    newT->parsingFavKindFamily = 1;
    if (thmFavKindDeclared(themeConfig, "favsVcdMain", i)) {
        for (j = 0; j < i; j++)
            addFavKindSlot(themePath, themeConfig, newT, &newT->favsVcdMainElems, &newT->vcdMainElems, "favsVcdMain", j, vcdMainChain, 3);
    }
    if (thmFavKindDeclared(themeConfig, "favsAppsMain", i)) {
        for (j = 0; j < i; j++)
            addFavKindSlot(themePath, themeConfig, newT, &newT->favsAppsMainElems, &newT->favsMainElems, "favsAppsMain", j, favsMainChain, 2);
    }
    newT->parsingFavKindFamily = 0;

    for (i = 0; addGUIElemSlot(themePath, themeConfig, newT, &newT->infoElems, infoChain, 1, i); i++)
        ;
    i++;

    for (j = 0; j < i; j++)
        addGUIElemSlot(themePath, themeConfig, newT, &newT->appsInfoElems, appsInfoChain, 2, j);

    // Favourites info family: favsInfo<j> override, else info<j> (identical to appsInfo).
    for (j = 0; j < i; j++)
        addGUIElemSlot(themePath, themeConfig, newT, &newT->favsInfoElems, favsInfoChain, 2, j);

    // VCD/PS1 view info family: vcdInfo<j> override, else fall back to info<j>.
    for (j = 0; j < i; j++)
        addGUIElemSlot(themePath, themeConfig, newT, &newT->vcdInfoElems, vcdInfoChain, 2, j);

    // Favourites per-kind INFO families, same opt-in rule. favsVcdInfo<j> falls back like vcdInfo (a PS1
    // row's info screen uses vcdInfo today); favsAppsInfo<j> like favsInfo (an app favourite's info
    // screen uses favsInfo today).
    newT->parsingFavKindFamily = 1;
    if (thmFavKindDeclared(themeConfig, "favsVcdInfo", i)) {
        for (j = 0; j < i; j++)
            addFavKindSlot(themePath, themeConfig, newT, &newT->favsVcdInfoElems, &newT->vcdInfoElems, "favsVcdInfo", j, vcdInfoChain, 2);
    }
    if (thmFavKindDeclared(themeConfig, "favsAppsInfo", i)) {
        for (j = 0; j < i; j++)
            addFavKindSlot(themePath, themeConfig, newT, &newT->favsAppsInfoElems, &newT->favsInfoElems, "favsAppsInfo", j, favsInfoChain, 2);
    }
    newT->parsingFavKindFamily = 0;
    thmParseViewPrefix = "";
    thmParseViewInherit = 1;
    thmParseAutoFit = 0;

    validateGUIElems(themePath, themeConfig, newT);

    newT->itemsList = newT->gamesItemsList;

    // NOTE: themeConfig is freed AFTER the texture-load section below -- the use_settings_bg lookup near
    // the end still reads it. Freeing here was a use-after-free (configGetInt on freed memory).
    LOG("THEMES Number of cache: %d\n", newT->gameCacheCount);
    LOG("THEMES Used height: %d\n", newT->usedHeight);

    // default all to not loaded...
    for (i = 0; i < TEXTURES_COUNT; i++)
        newT->textures[i].Mem = NULL;

    // LOGO, loaded here to avoid flickering during startup with device in AUTO + theme set
    texLoadInternal(&newT->textures[LOGO_PICTURE], LOGO_PICTURE);
    texLoadInternal(&newT->textures[TECHWRITESCODE_LOGO], TECHWRITESCODE_LOGO);

    // Optional animated boot-logo frames (embedded build assets logo0..logo6).
    // Count the contiguous frames that actually load; guiRenderGreeting() cycles
    // them on the boot splash. Zero when none are embedded -> single LOGO_PICTURE.
    newT->logoFrameCount = 0;
    for (i = LOGO0_PICTURE; i <= LOGO6_PICTURE; i++) {
        texLoadInternal(&newT->textures[i], i);
        if (newT->textures[i].Mem != NULL)
            newT->logoFrameCount++;
        else
            break;
    }

    // First start with busy icon
    const char *themePath_temp = themePath;
    int customBusy = 0;
    for (i = LOAD0_ICON; i <= LOAD7_ICON; i++) {
        if (thmLoadResource(&newT->textures[i], i, themePath_temp, GS_PSM_CT32, newT->useDefault) >= 0)
            customBusy = 1;
        else {
            if (customBusy)
                break;
            else
                themePath_temp = NULL;
        }
    }
    newT->loadingIconCount = i;

    // Customizable icons
    int customIndex = 0, customIndexCount = 0;
    for (i = BDM_ICON; i <= START_ICON; i++) {
        if (thmLoadResource(&newT->textures[i], i, themePath, GS_PSM_CT32, newT->useDefault) >= 0 && i == INDEX_0)
            customIndex = 1;
    }
    for (i = INDEX2_0; i <= INDEX4_3; i++) {
        if (thmLoadResource(&newT->textures[i], i, themePath, GS_PSM_CT32, newT->useDefault) >= 0)
            customIndexCount = 1;
    }
    // A theme with its own five-dot BdmIndex pictures but none for fewer devices keeps its five dots by
    // slot, so its style is not mixed with the built-in dots (belomaxorka / YAOPL f84b227d).
    newT->bdmIndexFixed = customIndex && !customIndexCount;

    // UDPFS_ICON is appended at the very END of the enum (after CASE_OVERLAY2) so that saved
    // favourite icon_ids stay ABI-stable -- which puts it OUTSIDE the BDM_ICON..START_ICON device
    // range above. Load it explicitly with the same disk-override + embedded-default semantics, or
    // the UDPFS filesystem tab and the UDPFSBD block tab (bdmGetIconId returns UDPFS_ICON when
    // gNetBootProtocol == NET_BOOT_UDPFS) draw no icon (thmGetTexture(UDPFS_ICON) returns NULL).
    thmLoadResource(&newT->textures[UDPFS_ICON], UDPFS_ICON, themePath, GS_PSM_CT32, newT->useDefault);

    // Control-hint glyphs + Favourites tab icon/star (contiguous L3_ICON..FAV_MARK: the VCD L3 hint,
    // the Favourites R3 hint, the FAV tab icon FAV_ICON/"fav", the favourited-item star
    // FAV_MARK/"fav_mark"). Theme-overridable with embedded fallback -- the SAME disk-override +
    // baked-default semantics as the BDM_ICON..START_ICON loop above: a disk theme's fav.png /
    // fav_mark.png / L3.png / R3.png win; the baked glyphs fill only what the theme does not ship
    // (and nothing when use_default=0). This loop previously passed themePath=NULL + useDefault=1,
    // so a disk theme's fav.png was never even probed and the baked icon always drew (#213).
    // thmLoadResource is synchronous here, so the baked glyph can never flash ahead of the theme's
    // own file.
    for (i = L3_ICON; i <= FAV_MARK; i++)
        thmLoadResource(&newT->textures[i], i, themePath, GS_PSM_CT32, newT->useDefault);

    // L1/R1 are used by the Settings peer-page footer. They are kept outside the L3..FAV_MARK
    // range so the existing texture IDs and theme loading range remain stable.
    thmLoadResource(&newT->textures[L1_ICON], L1_ICON, themePath, GS_PSM_CT32, newT->useDefault);
    thmLoadResource(&newT->textures[R1_ICON], R1_ICON, themePath, GS_PSM_CT32, newT->useDefault);

    // glyph_set=theme|builtin|standard|white (RIPPS2 build 90): the glyphs this theme draws. Settings > Interface >
    // Button Glyphs overrides it.
    {
        const char *glyphSet = NULL;
        newT->glyphSetDef = configGetStr(themeConfig, "glyph_set", &glyphSet) ? thmGlyphSetParse(glyphSet) : GLYPH_SET_THEME;
        newT->glyphSetLoaded = GLYPH_SET_THEME;
        snprintf(newT->glyphPath, sizeof(newT->glyphPath), "%s", themePath != NULL ? themePath : "");
        if (thmGlyphSetFor(newT->glyphSetDef) != GLYPH_SET_THEME)
            thmGlyphSetLoad(newT, thmGlyphSetFor(newT->glyphSetDef), 0); // never drawn yet: no VRAM to drop
    }

    // The hint bar (RIPPS2 build 90): hints_hide=star,view hints_order=run,info,menu hints_labels=0|1
    // hints_layout=row|column hints_spacing=28 (12 to 96)
    {
        const char *v = NULL;
        int hidden[THM_HINT_COUNT], n, k;
        newT->hintsHide = 0;
        newT->hintsLabels = 1;
        newT->hintsColumn = 0;
        newT->hintsSpacing = 28;
        if (configGetStr(themeConfig, "hints_hide", &v) && v != NULL) {
            n = thmHintParse(v, hidden, THM_HINT_COUNT);
            for (k = 0; k < n; k++)
                newT->hintsHide |= 1 << hidden[k];
        }
        newT->hintsOrderCount = configGetStr(themeConfig, "hints_order", &v) && v != NULL ? thmHintParse(v, newT->hintsOrder, THM_HINT_COUNT) : 0;
        if (configGetStr(themeConfig, "hints_labels", &v) && v != NULL)
            newT->hintsLabels = strcmp(v, "0") != 0;
        if (configGetStr(themeConfig, "hints_layout", &v) && v != NULL)
            newT->hintsColumn = !strcasecmp(v, "column");
        if (configGetInt(themeConfig, "hints_spacing", &k) && k >= 12 && k <= 96)
            newT->hintsSpacing = k;
    }

    // Embedded attribute glyphs (#Format/#Media/Aspect/Rating/Scan/Vmode values): BUILT-IN theme
    // ONLY. a1ae5e5e decoded these for every theme so badges could fall back to them on disk themes
    // -- but the fallback fires on every frame BEFORE the theme's own glyph PNG has loaded off the
    // single art worker, so custom themes FLASHED our baked glyph and then swapped to their own
    // asset (brenotomaz, #213: "the original assets load first"). Doctrine (NathanNeurotic): a
    // custom theme uses its OWN assets entirely -- never the baked set; no glyph shipped means the
    // element's own _default (or nothing), not our art. Gating the decode also reclaims the ~153 KB
    // of EE RAM on disk themes. The draw-site fallback is additionally gated on the built-in
    // (NULL-prefix) cache in drawAttributeImage, so this can never regress by re-widening one side.
    if (!themePath) {
        for (i = ELF_FORMAT; i <= VMODE_PAL; i++)
            thmLoadResource(&newT->textures[i], i, NULL, GS_PSM_CT32, 1);
#ifdef BUILTIN_KORIUM
        // Korium's cfgs draw attribute=Players; its badges are outside the ELF_FORMAT..VMODE_PAL range.
        for (i = PLAYERS_1; i <= PLAYERS_4; i++)
            thmLoadResource(&newT->textures[i], i, NULL, GS_PSM_CT32, 1);
#endif
    }

    // Optional settings/menu background (guiDrawBGSettings draws it instead of the plasma).
    // Theme-supplied only for now: a disk theme opts in with use_settings_bg=1 and ships its
    // own settings_bg.png. No embedded default yet (internalDefault[SETTINGS_BG] is NULL), so
    // the built-in <OPL>/<Coverflow> themes leave the slot empty and keep the plasma.
    if (themePath) {
        if (configGetInt(themeConfig, "use_settings_bg", &intValue) && intValue)
            thmLoadResource(&newT->textures[SETTINGS_BG], SETTINGS_BG, themePath, GS_PSM_CT32, 0);
    }

    configFree(themeConfig); // all themeConfig reads are done now (last was use_settings_bg above)

    // The old theme cannot be freed while its caches are still referenced by the
    // worker. Keep it if the bounded drain does not complete.
    if (curT != NULL &&
        (!cacheAbortMmceImageLoadsTimed(THM_MMCE_ART_ABORT_WAIT_TICKS) ||
         !cacheCancelPendingImageLoadsTimed(THM_MMCE_ART_ABORT_WAIT_TICKS))) {
        LOG("THEMES Load: art drain timed out -- keeping the current theme (retry later)\n");
        thmFree(newT);
        return -1;
    }
    if (curT == NULL)
        cacheCancelPendingImageLoads(); // first load: pre-cacheInit, nothing queued -- plain no-op drain
    gTheme = newT;
    loadedFontSize = gFontSize;
    psbbnFlowReset();
    thmFree(curT);
    return 0;
}

int thmRefreshFontSize(void)
{
    if (gTheme == NULL || loadedFontSize == gFontSize)
        return 0;

    // Font changes only alter geometry. Keep artwork and disk-theme resources in place.
    theme_elems_t *families[] = {
        &gTheme->mainElems, &gTheme->infoElems, &gTheme->appsMainElems, &gTheme->appsInfoElems,
        &gTheme->favsMainElems, &gTheme->favsInfoElems, &gTheme->vcdMainElems, &gTheme->vcdInfoElems,
        &gTheme->favsVcdMainElems, &gTheme->favsVcdInfoElems, &gTheme->favsAppsMainElems, &gTheme->favsAppsInfoElems};
    for (theme_elems_t *family : families) {
        for (theme_element_t *elem = family->first; elem != NULL; elem = elem->next) {
            if (elem->type == ELEM_TYPE_ITEMS_LIST && elem->extended != NULL)
                itemsListFit((items_list_t *)elem->extended, elem->height);
        }
        thmComputeTextRooms(family);
    }
    loadedFontSize = gFontSize;
    return 1;
}

int ThemeCatalog::readEntryCallback(int index, const char *path, const char *separator, const char *name, unsigned char d_type)
{
    return gCatalog.readEntry(index, path, separator, name, d_type);
}

// Callers MUST hold guiLock (except pre-GUI single-threaded init, where the lock is a not-ready
// no-op): this free()s the list the UI-Settings dialog may be handed via thmGetGuiList, and it
// runs on the IO worker for device-triggered rebuilds. guiSettingsShowInterface snapshots the list under
// the same lock. NOT self-locking: thmReinit must hold the lock across its name-string frees AND
// this rebuild as ONE critical section (between the two, the published list points at freed
// strings), and the sema is not recursive.
void ThemeCatalog::rebuildNames()
{
    if (names_)
        free(names_);

    // Default internal, disk themes, then the NULL terminator. The old <Coverflow> and <PSBBN Flow>
    // entries are views of <OPL> now (settingsio maps a saved name to <OPL> plus that view).
    names_ = (const char **)malloc((count_ + 2) * sizeof(char **));

    // add default internal
    names_[0] = "<OPL>";

    int i = 0;
    for (; i < count_; i++) {
        names_[i + 1] = themes_[i].name;
    }
    names_[count_ + 1] = NULL;
}

int ThemeCatalog::addElements(char *path, const char *separator, int forceRefresh)
{
    int result, i;

    result = listDir(path, separator, THM_MAX_FILES - count_, &readEntryCallback);
    count_ += result;
    // Appends above only fill NEW themes_[] slots (the published name list references old slots
    // untouched); the rebuild's free+swap is what must be serialized against the GUI's readers.
    host::ui().lock();
    rebuildNames();
    host::ui().unlock();

    const char *temp;
    if (configGetStr(configGetByType(CONFIG_OPL), "theme", &temp)) {
        LOG("THEMES Trying to set again theme: %s\n", temp);
        if (setGuiValue(findGuiID(temp), 0) && forceRefresh) {
            for (i = 0; i < MODE_COUNT; i++)
                host::app().moduleUpdateMenu(i, 1, 0);
        }
    }

    return result;
}

void thmInit(void)
{
    LOG("THEMES Init\n");
    gTheme = NULL;

    thmReloadScreenExtents();

    // initialize default internal
    thmLoad(NULL);

    gCatalog.addElements(gBaseMCDir, "/", 0);
}

void ThemeCatalog::reinit(const char *path)
{
    // Nad #5 (settings change reverts the look): this used to UNCONDITIONALLY thmLoad(NULL) +
    // themeId_=0 -- reverting to the default <OPL> look even when the ACTIVE theme does not live
    // on the device being removed. Worse, an SMB/ETH re-init calls in here on EVERY settings-OK
    // while the network is in any error state (server asleep, cable out, or plain share-browse
    // mode, which parks in an "error" state even when healthy), and a subsequent Save Changes then
    // wrote the transient "<OPL>" over the user's saved theme name -- reverting it PERMANENTLY.
    // Only fall back to the default when the active theme actually lives on the removed device;
    // otherwise keep it and just re-index (the removal loop reshuffles ids, so re-find by name).
    char activeName[64] = "";
    int activeOnDevice = 0;
    if (themeId_ >= 1 && themeId_ <= count_) {
        snprintf(activeName, sizeof(activeName), "%s", themes_[themeId_ - 1].name);
        activeOnDevice = strncmp(themes_[themeId_ - 1].filePath, path, strlen(path)) == 0;
    }

    // One guiLock section across the removal loop AND the rebuild: the frees below invalidate
    // strings the CURRENTLY PUBLISHED names_ points at, so the GUI-side readers
    // (frame-locked renders, guiSettingsShowInterface's snapshot) must not run between free and swap.
    host::ui().lock();
    int i = 0;
    while (i < count_) {
        if (strncmp(themes_[i].filePath, path, strlen(path)) == 0) {
            LOG("THEMES Remove theme: %s\n", themes_[i].filePath);
            count_--;
            free(themes_[i].name);
            themes_[i].name = themes_[count_].name;
            themes_[count_].name = NULL;
            free(themes_[i].filePath);
            themes_[i].filePath = themes_[count_].filePath;
            themes_[count_].filePath = NULL;
        } else
            i++;
    }

    rebuildNames();
    host::ui().unlock();

    if (activeOnDevice) {
        // The displayed theme's files just went away with its device: drop to the built-in default.
        // Honor thmLoad's abandon-and-retry (#120): commit the default id only if the swap happened,
        // else keep the old id so the still-displayed theme and the saved config stay consistent.
        if (thmLoad(NULL) == 0)
            themeId_ = 0;
    } else if (themeId_ != 0) {
        // Active theme untouched (a theme on another device): keep it. gTheme
        // needs no reload -- its loaded resources are unaffected by the removed entries.
        themeId_ = findGuiID(activeName);
    }
}

void thmReloadScreenExtents(void)
{
    rmGetScreenExtents(&gThmScreenWidth, &gThmScreenHeight);
}

const char *ThemeCatalog::currentName() const
{
    return names_[themeId_];
}

int ThemeCatalog::setGuiValue(int themeID, int reload)
{
    if (themeID >= 0 && themeID <= count_) {
        if (themeId_ != themeID || reload) {
            int loadResult = thmLoad(themeID != 0 ? themes_[themeID - 1].filePath : NULL);

            // #120: an abandoned load (wedged MMCE art worker) keeps the CURRENT theme -- do not
            // commit the new id, so the next thmAddElements/settings-apply trigger retries it.
            if (loadResult != 0)
                return 0;

            themeId_ = themeID;
            return 1;
        } else if (themeId_ == 0) {
            /* Re-apply the edited gDefault*Color values to the LIVE theme when the theme itself is
               not being reloaded -- which is the whole of what a colour edit does.

               This used to fire for theme 0 alone, so the built-in Coverflow theme (id count_ + 1)
               silently dropped every colour change: guiShowColorsConfig offers Coverflow as
               editable, wrote the new values into gDefault*Color, then called applyConfig with the
               SAME theme id and reload == 0, and landed here to be skipped. The colours were saved
               correctly and never applied, which reads exactly like "the option does nothing".

               Disk themes stay excluded on purpose -- their colours come from the theme file, and
               overwriting them with the settings picker's values is what the read-only grey-out in
               guiShowColorsConfig exists to prevent. (issue #537) */
            thmSetColors(gTheme);
        }
    }
    return 0;
}

int ThemeCatalog::guiValue() const
{
    return themeId_;
}

int ThemeCatalog::findGuiID(const char *theme) const
{
    if (theme) {
        // A saved "<Coverflow>" / "<PSBBN Flow>" matches nothing here and so loads <OPL> (id 0).
        for (int i = 0; i < count_; i++) {
            if (strcasecmp(themes_[i].name, theme) == 0)
                return i + 1;
        }
    }
    return 0;
}

const char **ThemeCatalog::guiList() const
{
    return names_;
}

char *ThemeCatalog::filePath(int themeID)
{
    // Disk themes occupy IDs 1..count_. The built-ins (<OPL> = 0, the flows above count_)
    // and any out-of-range ID have no on-disk path -> return NULL so callers fall back to
    // the default/internal assets instead of indexing themes_[] out of bounds.
    if (themeID <= 0 || themeID > count_)
        return NULL;

    theme_file_t *currTheme = &themes_[themeID - 1];
    char *path = currTheme->filePath;

    return path;
}

void ThemeCatalog::end()
{
    for (int i = 0; i < count_; i++) {
        free(themes_[i].name);
        free(themes_[i].filePath);
    }

    free(names_);
}

// ---- themes.h API --------------------------------------------------------------------------------

int thmAddElements(char *path, const char *separator, int forceRefresh)
{
    return gCatalog.addElements(path, separator, forceRefresh);
}

void thmReinit(const char *path)
{
    gCatalog.reinit(path);
}

const char *thmGetValue(void)
{
    return gCatalog.currentName();
}

int thmSetGuiValue(int themeID, int reload)
{
    return gCatalog.setGuiValue(themeID, reload);
}

int thmSetView(int view)
{
    if (view < 0 || view >= THM_VIEW_COUNT)
        view = -1;
    int previous = gThemeViewRequest;
    gThemeViewRequest = view;
    if (gTheme == NULL)
        return 0;

    // The view the active theme would load for this request; nothing to do when it is already showing.
    int target = (view >= 0 && (gTheme->viewMask & (1u << view))) ? view : gTheme->defaultView;
    if (target == gTheme->view)
        return 0;
    if (!gCatalog.setGuiValue(gCatalog.guiValue(), 1)) {
        gThemeViewRequest = previous; // abandoned load (#120): the old view is still on screen
        return 0;
    }
    return 1;
}

int thmRefreshDisplayLayout(void)
{
    if (gTheme == NULL || !gTheme->layoutDependsOnWide || gTheme->layoutWide == rmIsWideDisplay())
        return 0;
    return gCatalog.setGuiValue(gCatalog.guiValue(), 1);
}

int thmCycleView(void)
{
    if (gTheme == NULL)
        return 0;
    // Coverflow (3) and Coverflow (5) are two stops of one theme view: the count is read at draw time, so
    // moving between them needs no reload.
    int covers = gCoverflowCount;
    int v = thmViewCycleNext(gTheme->viewMask, gTheme->view, &covers);
    if (v == gTheme->view) {
        if (covers == gCoverflowCount)
            return 0; // the theme offers a single view
        gCoverflowCount = covers;
        return 1;
    }
    int previousCovers = gCoverflowCount;
    gCoverflowCount = covers;
    if (!thmSetView(v) && gTheme->view != v) {
        gCoverflowCount = previousCovers; // abandoned load: the old view is still on screen
        return 0;
    }
    return 1;
}

int thmGetGuiValue(void)
{
    return gCatalog.guiValue();
}

int thmFindGuiID(const char *theme)
{
    return gCatalog.findGuiID(theme);
}

// The saved theme is a disk theme (not one of the built-in names) that is not on screen yet: its drive is still coming
// up, its theme is still loading, or the drive is gone. The boot greeting waits a bounded time for it (RIPPS2 build
// 125, AK). Only the built-in names are skipped, so a disk theme whose folder name starts with '<' still waits.
int thmSavedThemePending(void)
{
    const char *temp = NULL;
    if (!configGetStr(configGetByType(CONFIG_OPL), CONFIG_OPL_THEME, &temp) || temp == NULL || temp[0] == '\0' ||
        strcasecmp(temp, "<OPL>") == 0 || strcasecmp(temp, "<Coverflow>") == 0 || strcasecmp(temp, "<PSBBN Flow>") == 0)
        return 0;
    const int id = thmFindGuiID(temp);
    return id == 0 || thmGetGuiValue() != id;
}

const char **thmGetGuiList(void)
{
    return gCatalog.guiList();
}

char *thmGetFilePath(int themeID)
{
    return gCatalog.filePath(themeID);
}

void thmEnd(void)
{
    thmFree(gTheme);
    gCatalog.end();
}
