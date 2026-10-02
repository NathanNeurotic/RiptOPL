#include "include/host.h"
#include "include/opl.h"
#include "include/themes.h"
#include "include/themeint.h"
#include "include/themecatalog.h"
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

// Coverflow: the carousel element (draw + init), its slide animation and the render-mode settings. Split out of themes.cpp.

constexpr int COVERFLOW_PAD = 1;  // extra covers built off each edge, purely to fill the slide (see drawCoverFlow)
constexpr int COVERFLOW_MAX = (5 + 2 * COVERFLOW_PAD);
int gCoverflowCount = 3;        // 3 or 5 only (clamped on load AND at draw)
int gCoverflowCenterScale = 30; // px added to the center cover (UI 0/15/30/45)
int gCoverflowAnimSpeed = 200;  // ms (UI 0/100/200/400; 0 = instant, no anim)
int gCoverflowDimCovers = 0;    // bool

// Coverflow slide animation (cubic ease-out); armed by thmTriggerCoverflowAnim().
static int cfIsAnimating = 0;
static int cfAnimDirection = 0;
static clock_t cfAnimStartTime = 0;


static int cfAnimFrameCount = 0;

// Arms a slide animation in direction dir (+1 next / -1 prev). No-op (instant move) when
// animation speed is 0. Called from menusys.c via the themes.h extern.
void thmTriggerCoverflowAnim(int dir)
{
    if (gCoverflowAnimSpeed <= 0)
        return;
    cfIsAnimating = 1;
    cfAnimDirection = dir;
    cfAnimStartTime = clock();
    cfAnimFrameCount = 0;
}

static void drawCoverFlow(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (!item)
        return; // empty list -> nothing to draw

    if (elem->extended == NULL)
        return; // coverflow element disabled (no cover cache)
    item_list_t *sourceList = (item_list_t *)thmGetItemSource(menu, item);

    // Defensive clamp: never trust the global at the draw site (conf.cfg may be hand-edited
    // to e.g. 7 -> covers[] OOB). coverCount is the VISIBLE count and drives layout only;
    // coverTotal (= coverCount + 2*COVERFLOW_PAD) is the index bound for covers[].
    int coverCount = (gCoverflowCount == 5) ? 5 : 3;
    int centerIndex = coverCount / 2;

    // Layout in virtual 640x480 space (div-guarded). SCALING_RATIO + the panel apply
    // the widescreen/PAR correction at draw time (rmSetupQuad: w * iAspectWidth >> 2),
    // exactly like the stock ItemCover path. We must NOT pre-apply rmWideScale here or
    // the cover gets the aspect factor twice and warps when widescreen is toggled.
    int coverWidth = elem->width;
    int maxCoverWidth = (gThmScreenWidth - (coverCount - 1) * 10) / coverCount;
    // Widescreen draws each cover at 3/4 width (SCALING_RATIO), leaving spare screen, so
    // allow ~10% larger covers in 16:9. This scales SIZE only -- the cover's authored W:H
    // is untouched, so aspect immunity holds; 4:3 keeps the tight 3-up maximum.
    if (gWideScreen)
        maxCoverWidth = maxCoverWidth * 11 / 10;
    if (coverWidth > maxCoverWidth)
        coverWidth = maxCoverWidth;
    if (coverWidth <= 0)
        return;

    int coverSpacing = (gThmScreenWidth - coverCount * coverWidth) / (coverCount + 1);
    if (coverSpacing < 0)
        coverSpacing = 0;
    int coverDistance = coverWidth + coverSpacing;
    int basePosX = (gThmScreenWidth - (coverCount * coverWidth + (coverCount - 1) * coverSpacing)) / 2 + coverWidth / 2 + coverWidth * gTheme->coverflowCoverOffset / 256;

    // Carousel cover SIZE/aspect comes from the coverflow element (uniform across covers). The
    // per-cover frame-inset recenter is computed inside the loop from each cover's OWN element +
    // overlay -- the Favourites tab can mix game and app cases (different frame insets), so a
    // single shared recenter would shift the odd one out.

    /*
      Build the window: covers[centerIdx] is the selection; fan out both sides, wrapping
      last<->first. The left wrap reads menu->item->last (full lifecycle wired in Commit E) --
      single-game/exhausted lists break out before dereferencing a stale ptr.

      COVERFLOW_PAD extra covers are built on EACH side, outside the visible slots. The slide
      translates the WHOLE strip by up to +/-coverDistance (animOffset, below) rather than moving
      covers between slots, so without padding the leading slot has nothing to slide in from: for
      the duration of every animation one edge slot was empty, while the cover at the far end was
      pushed entirely off-screen after being fetched and decoded.

      Indices are shifted by COVERFLOW_PAD so that i == COVERFLOW_PAD lands on the SAME screen
      position slot 0 used before (see posX in the draw loop, which subtracts it back out). Layout
      maths -- maxCoverWidth, coverSpacing, basePosX -- still keys off coverCount, the VISIBLE
      count, so nothing about the resting layout changes.
    */
    int coverTotal = coverCount + 2 * COVERFLOW_PAD; // entries built and drawn
    int centerIdx = COVERFLOW_PAD + centerIndex;     // the selection, within the padded array

    struct submenu_list *covers[COVERFLOW_MAX];
    int i;
    for (i = 0; i < COVERFLOW_MAX; i++)
        covers[i] = NULL;
    covers[centerIdx] = item;

    struct submenu_list *walk = item;
    for (i = centerIdx - 1; i >= 0; i--) {
        struct submenu_list *prev = walk->prev ? walk->prev : menu->item->last;
        if (!prev || prev == walk || prev == item)
            break;
        covers[i] = prev;
        walk = prev;
    }

    walk = item;
    for (i = centerIdx + 1; i < coverTotal; i++) {
        struct submenu_list *next = walk->next ? walk->next : menu->item->submenu;
        if (!next || next == walk || next == item)
            break;
        covers[i] = next;
        walk = next;
    }

    // Slide animation (cubic ease-out). clock() wrap is clamped to snap-complete.
    // Sign convention (FifthFox HW report: coverflow "advances in the opposite direction you'd expect"):
    // advancing (Next / Right, dir=+1) must bring the incoming cover in from the RIGHT -> the strip starts
    // shifted +right and eases to 0 (i.e. slides LEFT), and the OUTGOING old-center (now at centerIdx-dir)
    // is the cover that shrinks. The prior (eased-1.0f) / centerIdx+dir convention slid the opposite way.
    float eased = 1.0f;
    int animOffset = 0;
    if (gCoverflowAnimSpeed <= 0) {
        cfIsAnimating = 0;
    } else if (cfIsAnimating) {
        clock_t durTicks = (clock_t)(gCoverflowAnimSpeed * (CLOCKS_PER_SEC / 1000.0f));
        if (durTicks <= 0)
            durTicks = 1;
        clock_t elapsed = clock() - cfAnimStartTime;
        if (elapsed < 0)
            elapsed = durTicks; // clock wrap -> finish now
        float t = (float)elapsed / (float)durTicks;
        if (t >= 1.0f) {
            if (cfAnimFrameCount == 0) {
                // Guarantee at least one intermediate 3D slide frame if a frame hitch occurred on start
                t = 0.5f;
            } else {
                t = 1.0f;
                cfIsAnimating = 0;
            }
        }
        cfAnimFrameCount++;
        eased = 1.0f - powf(1.0f - t, 3.0f);
        animOffset = (int)(cfAnimDirection * coverDistance * (1.0f - eased));
    }
    int leavingIndex = centerIdx - cfAnimDirection; // the OUTGOING old-center shrinks (opposite the slide-in side)

    rmSetReflectionYOffset(elem->reflectionOffset); // theme reflection_offset; reset after the loop

    /*
      At REST the two padding covers sit fully off-screen, so drawing them would cost two extra
      texture fetches per frame for nothing -- and this carousel is already the subject of a
      lag report (#271). Fetch and draw them ONLY while a slide is actually in flight, which is
      the only time they are on-screen. Rest-state cost is therefore byte-identical to before.
    */
    int drawFirst = cfIsAnimating ? 0 : COVERFLOW_PAD;
    int drawLast = cfIsAnimating ? coverTotal : coverTotal - COVERFLOW_PAD;

    // Submit the selected cover before the left-to-right draw loop so the FIFO
    // cannot place neighbouring covers ahead of it on a cold cache.
    if (covers[centerIdx]) {
        mutable_image_t *centerImg = (mutable_image_t *)thmGetElemForItem(menu, covers[centerIdx], elem)->extended;
        if (centerImg)
            // PRIORITY, for the same reason the list's cover panel is: submitting first only wins
            // the race on a frame where the queue has room. The depth cap refuses by ARRIVAL, so
            // with neighbours already in flight from earlier frames the centre cover -- the one the
            // user is looking at -- was turned away and re-asked next frame, behind the same
            // backlog. Coverflow was left out when the list draw path got this treatment.
            getGameImageTextureEx(centerImg->cache, sourceList, &covers[centerIdx]->item, 1);
    }

    /*
      Lookahead warming (#296). The draw loop below only ever requests the covers on screen
      (plus the slide padding), so scrolling faster than a decode leaves every incoming cover
      cold -- "CF covers load only on focus". Walk the ring outward from the selection,
      wrap-aware like the window builder above, and request neighbours beyond the window while
      the cache budget lasts. Depth is (cache->count() - 1)/2 per side, so one frame touches at
      most cache->count() distinct keys (selected + both walks) and warming can never evict the
      selected cover; the per-cache request budget mirrors drawItemsList (a Favourites tab can
      resolve covers into TWO caches via thmGetElemForItem). Folders carry no cover. Themes
      tune the depth with the existing main0_count knob -- more cache slots, deeper warming.
    */
    mutable_image_t *elemImg = (mutable_image_t *)elem->extended;
    int warmRadius = (elemImg != NULL && elemImg->cache != NULL) ? (elemImg->cache->count() - 1) / 2 : 0;
    struct
    {
        image_cache_t *cache;
        int left;
    } cfWarm[2] = {{NULL, 0}, {NULL, 0}};
    struct submenu_list *walkSide[2] = {item, item}; // [0] = forward, [1] = backward
    int step;
    for (step = 0; step < warmRadius; step++) {
        int side;
        for (side = 0; side < 2; side++) {
            struct submenu_list *walk = walkSide[side];
            struct submenu_list *next = side ? (walk->prev ? walk->prev : menu->item->last) : (walk->next ? walk->next : menu->item->submenu);
            if (!next || next == walk || next == item)
                continue;
            walkSide[side] = next;
            if (next->item.isFolder)
                continue;

            mutable_image_t *wimg = (mutable_image_t *)thmGetElemForItem(menu, next, elem)->extended;
            if (wimg == NULL || wimg->cache == NULL || wimg->cache->count() < 2)
                continue;
            if (step >= (wimg->cache->count() - 1) / 2)
                continue; // deeper than THIS resolved cache can hold

            int b;
            for (b = 0; b < 2 && cfWarm[b].cache != NULL && cfWarm[b].cache != wimg->cache; b++)
                ;
            if (b == 2)
                continue;
            if (cfWarm[b].cache == NULL) {
                cfWarm[b].cache = wimg->cache;
                cfWarm[b].left = wimg->cache->count() - 1; // selected's slot stays reserved
            }
            if (cfWarm[b].left > 0) {
                cfWarm[b].left--;
                getGameImageTexture(wimg->cache, sourceList, &next->item);
            }
        }
    }

    /*
      Draw order (#343): painter's algorithm renders back-to-front (outermost covers first,
      center cover last) so that inner/center covers cleanly overlap outer ones on BOTH
      left and right sides without geometry clipping.
    */
    int drawOrder[COVERFLOW_MAX];
    int drawCount = 0;
    int maxDist = centerIdx - drawFirst;
    if (drawLast - 1 - centerIdx > maxDist)
        maxDist = drawLast - 1 - centerIdx;

    int d;
    for (d = maxDist; d > 0; d--) {
        int leftIdx = centerIdx - d;
        if (leftIdx >= drawFirst && leftIdx < drawLast)
            drawOrder[drawCount++] = leftIdx;
        int rightIdx = centerIdx + d;
        if (rightIdx >= drawFirst && rightIdx < drawLast)
            drawOrder[drawCount++] = rightIdx;
    }
    if (centerIdx >= drawFirst && centerIdx < drawLast)
        drawOrder[drawCount++] = centerIdx;

    int idx;
    for (idx = 0; idx < drawCount; idx++) {
        i = drawOrder[idx];
        if (!covers[i])
            continue;

        // Per-cover element redirect: on the FAV tab an APP favourite uses the apps element (art
        // folder + case overlay); games and other tabs pass through to the coverflow element. The
        // texture lookup stays on sourceList (favList) so favGetImage proxies by the FAV index --
        // only the cache + overlay are redirected; frame-inset math uses THIS cover's own dims.
        theme_element_t *coverElem = thmGetElemForItem(menu, covers[i], elem);
        mutable_image_t *cimg = (mutable_image_t *)coverElem->extended;
        if (!cimg)
            continue;
        int csw = (coverElem->width > 0) ? coverElem->width : 1;   // div-guard
        int csh = (coverElem->height > 0) ? coverElem->height : 1; // div-guard

        GSTEXTURE *texture = getGameImageTexture(cimg->cache, sourceList, &covers[i]->item);
        int hasArt = (texture && texture->Mem);
        if (!hasArt)
            texture = cimg->defaultTexture ? &cimg->defaultTexture->source : thmGetTexture(COVER_DEFAULT);
        if (!texture || !texture->Mem)
            continue;

        // Center grows; the leaving neighbour shrinks. leavingIndex is bounds-checked into
        // [0,coverTotal) BEFORE it indexes covers[] (audit fix).
        int scaleAdd = 0;
        if (i == centerIdx)
            scaleAdd = (int)(gCoverflowCenterScale * eased);
        else if (cfIsAnimating && leavingIndex >= 0 && leavingIndex < coverTotal && i == leavingIndex)
            scaleAdd = (int)(gCoverflowCenterScale * (1.0f - eased));

        int drawW = coverWidth + scaleAdd;
        // Height tracks THIS cover's OWN element aspect (csh/csw), so a mixed tab -- Favourites,
        // which interleaves portrait game covers and square app covers -- honors each cover's shape
        // instead of stamping the base element's aspect on every cover. Width stays uniform
        // (coverWidth) for even carousel spacing. On single-type tabs coverElem == elem, so this is
        // identical to the old coverHeight*drawW/coverWidth. csw is div-guarded >= 1 above.
        int drawH = csh * drawW / csw;

        // Auto-center the VISIBLE cover (case frame box) from THIS cover's overlay corners: the
        // frame can sit off-center (e.g. apps_case = top-left 186 of 256). No overlay => no shift.
        int recenterX = 0, recenterY = 0;
        if (cimg->overlayTexture) {
            image_texture_t *ovc = cimg->overlayTexture;
            recenterX = csw / 2 - (ovc->upperLeft_x + ovc->upperRight_x) / 2;
            recenterY = csh / 2 - (ovc->upperLeft_y + ovc->lowerLeft_y) / 2;
        }

        int posX = basePosX + (i - COVERFLOW_PAD) * coverDistance + animOffset + recenterX * drawW / csw;
        // Covers stand on a common BASELINE: each cover's bottom edge sits where a cover of the PAGE
        // element's own shape (elem, not this row's coverElem) would end at this width. On a mixed shelf
        // (Favourites "All", Mixed device pages) portrait and square covers therefore rest on one floor
        // line and their reflections start together, where centering left the square ones floating
        // (#643). On a single-kind page coverElem == elem, the shift is 0 and elem->posY stays the
        // cover's center, so nothing moves there.
        int pageW = (elem->width > 0) ? elem->width : csw;
        int pageDrawH = ((elem->height > 0) ? elem->height : csh) * drawW / pageW;
        int posY = elem->posY + (pageDrawH - drawH) / 2 + recenterY * drawH / csh;

        u64 coverColor = (gCoverflowDimCovers && i != centerIdx) ? GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x40) : gDefaultCol;

        if (cimg->overlayTexture) {
            // Scale the inlay (cover) corner offsets to the drawn overlay size so the case
            // window tracks the cover at every scale. HW-verify alignment with real case art.
            image_texture_t *ov = cimg->overlayTexture;
            rmDrawOverlayPixmap(&ov->source, posX, posY, ALIGN_CENTER, drawW, drawH, SCALING_RATIO, coverColor,
                                texture, ov->upperLeft_x * drawW / csw, ov->upperLeft_y * drawH / csh, ov->upperRight_x * drawW / csw, ov->upperRight_y * drawH / csh,
                                ov->lowerLeft_x * drawW / csw, ov->lowerLeft_y * drawH / csh, ov->lowerRight_x * drawW / csw, ov->lowerRight_y * drawH / csh, elem->reflection);
            if (cimg->overlayTexture2)
                rmDrawPixmap(&cimg->overlayTexture2->source, posX, posY, ALIGN_CENTER, drawW, drawH, SCALING_RATIO, coverColor, elem->reflection);
        } else {
            rmDrawPixmap(texture, posX, posY, ALIGN_CENTER, drawW, drawH, SCALING_RATIO, coverColor, elem->reflection);
        }
    }

    rmSetReflectionYOffset(0); // don't leak the offset to any other reflection draw
}

void initCoverflow(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, "COV", COVER_CACHE_SLOTS, NULL, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage && mutableImage->cache) {
        elem->drawElem = &drawCoverFlow;
        if (!theme->coverflow)
            theme->coverflow = elem; // first coverflow element = the "coverflow active" flag
    } else
        LOG("THEMES Coverflow %s: NO cache, elem disabled !!\n", name);
}

