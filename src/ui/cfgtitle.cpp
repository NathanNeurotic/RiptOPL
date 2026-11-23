// Adapted from original PR #932, production dea980ef.
/*
  cfgtitle.c -- optional CFG Titles in the game lists (#918).

  "Show CFG Titles in Game Lists" (off by default) lets a row show the Title
  written in the game's per-game CFG -- typically a proper Japanese title
  from OPL Manager -- instead of its file name, and sort by it.

  Display only. The row's identity text (item.text, which rename, Favorites,
  art, configs and launching key on) is never changed; drawing and sorting
  ask cfgTitleDisplayName() instead, the same way the PS1 game-ID hiding works
  (vcdDisplayNameForRow).

  Cost, which is why it is opt-in: each list rebuild reads every game's CFG
  once, on the I/O thread, through the same loader the info page uses
  (itemGetConfig). Reading them all is what lets the list sort by what it
  shows. Each source remembers every result its passes found (the PS1 and
  PS2 views both), so a rebuild first fills the table from memory: when that
  answers every row, the rows are listed at once with their titles and the
  pass runs after them to pick up changes; when it does not (the first visit
  after boot), the pass runs before the rows. Either way a row never shows its
  file name before its title. The table of kept titles is swapped in under
  the GUI frame lock, so a frame never sees it half built or freed.
*/

#include "include/opl.h" // first: the PS2 types every other header needs
#include "include/cfgtitle.h"
#include "include/config.h"
#include "include/gui.h"
#include "include/vcdsupport.h"
#include "include/libview.h" /* libListRowView: only a PS1 row has a base_game_info_t PS1 name */
#include "include/folderbrowse.h"
#include "include/gamesource.h"
#include "include/favsupport.h" /* favGetItemSourceMode: Favorites from the Apps tab are apps */
#ifdef RETROACHIEVEMENTS
#include "include/rabadge.h"
#endif

#include <stdlib.h>
#include <string.h>

CfgTitles gCfgTitles;

CfgTitles::Slot *CfgTitles::findSlot(item_list_t *support)
{
    int i;
    if (support == NULL)
        return NULL; /* an unclaimed slot also has support == NULL */
    for (i = 0; i < MODE_COUNT; i++)
        if (slots_[i].support == support)
            return &slots_[i];
    return NULL;
}

CfgTitles::Slot *CfgTitles::claimSlot(item_list_t *support)
{
    Slot *slot = findSlot(support);
    int i;

    if (slot != NULL)
        return slot;
    for (i = 0; i < MODE_COUNT; i++)
        if (slots_[i].support == NULL) {
            slots_[i].support = support;
            return &slots_[i];
        }
    return NULL;
}

/* Length of the UTF-8 character at p (within n bytes), or 0 if it is not valid strict UTF-8:
   no overlong forms, surrogates, code points past U+10FFFF or C1 control characters. */
static int utf8Length(const unsigned char *p, int n)
{
    int len, i;

    if (p[0] < 0x80)
        len = 1;
    else if ((p[0] & 0xE0) == 0xC0 && p[0] >= 0xC2)
        len = 2;
    else if ((p[0] & 0xF0) == 0xE0)
        len = 3;
    else if ((p[0] & 0xF8) == 0xF0 && p[0] <= 0xF4)
        len = 4;
    else
        return 0;
    if (len > n)
        return 0;
    for (i = 1; i < len; i++)
        if ((p[i] & 0xC0) != 0x80)
            return 0;
    if ((p[0] == 0xC2 && p[1] < 0xA0) || /* U+0080..U+009F: C1 controls */
        (p[0] == 0xE0 && p[1] < 0xA0) || /* overlong */
        (p[0] == 0xED && p[1] > 0x9F) || /* surrogates */
        (p[0] == 0xF0 && p[1] < 0x90) || /* overlong */
        (p[0] == 0xF4 && p[1] > 0x8F))   /* past U+10FFFF */
        return 0;
    return len;
}

int CfgTitles::clean(const char *in, char *out, int size)
{
    const unsigned char *p, *end;
    int at = 0, n;

    if (out == NULL || size < 2)
        return 0;
    out[0] = '\0';
    if (in == NULL)
        return 0;
    p = (const unsigned char *)in;
    end = p + strlen(in);
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    if (p == end)
        return 0;

    while (p < end) {
        n = utf8Length(p, (int)(end - p));
        if (n == 0 || (n == 1 && (*p < 0x20 || *p == 0x7F)))
            return 0; /* not text: show the file name */
        if (at + n > size - 1)
            break; /* cut on a character boundary */
        memcpy(out + at, p, n);
        at += n;
        p += n;
    }
    out[at] = '\0';
    return at > 0;
}

static int isFolderRow(item_list_t *support, int id)
{
    base_game_info_t *game;

    if (!folderModeSupported(support->mode) || !gsOf(support)->hasGame())
        return 0;
    game = (base_game_info_t *)gsOf(support)->game(support, id);
    return game != NULL && game->format == GAME_FORMAT_FOLDER;
}

/* A row the title pass skips: a folder, or a Favorite from the Apps tab. */
static int untitledRow(item_list_t *support, int id)
{
    /* a Favorite from the Apps tab is an app: the Apps list shows no titles */
    return isFolderRow(support, id) || (support->mode == FAV_MODE && favGetItemSourceMode(id) == APP_MODE);
}

/* The title as the row should show it: in RA builds a tracked game keeps its mark where the badge
   placed it. The whole known affix must match, so a file literally named "[RA]" is not mistaken for
   it. RA builds store the clean title after the shown one, for badge-independent sorting. */
static char *keepTitle(item_list_t *support, int id, const char *clean)
{
    const char *prefix = "", *suffix = "";
    size_t prefixLen = 0, suffixLen = 0, len = strlen(clean), sortLen = 0;
    char *kept;

#ifdef RETROACHIEVEMENTS
    {
        const char *badge = raBadgeText(support, id);
        const char *name = gsOf(support)->name(support, id);
        if (badge != NULL && name != NULL) {
            name = vcdDisplayNameForRow(support, id, name);
            size_t badgeLen = strlen(badge), nameLen = strlen(name);
            if (badgeLen == nameLen + 3 && !strncmp(badge, "RA ", 3) && !strcmp(badge + 3, name))
                prefix = badge, prefixLen = 3;
            else if (badgeLen == nameLen + 5 && !strncmp(badge, name, nameLen) && !strcmp(badge + nameLen, " [RA]"))
                suffix = badge + nameLen, suffixLen = 5;
        }
    }
    sortLen = len + 1;
#else
    (void)support;
    (void)id;
#endif
    kept = (char *)malloc(prefixLen + len + suffixLen + 1 + sortLen);
    if (kept == NULL)
        return NULL;
    memcpy(kept, prefix, prefixLen);
    memcpy(kept + prefixLen, clean, len);
    memcpy(kept + prefixLen + len, suffix, suffixLen + 1);
    memcpy(kept + prefixLen + len + suffixLen + 1, clean, sortLen);
    return kept;
}

/* Swaps a new table in under the GUI frame lock and frees the old one after it. */
void CfgTitles::swapTable(Slot *slot, char **fresh, int freshCount)
{
    char **old;
    int i, oldCount;

    guiLock();
    old = slot->title;
    oldCount = slot->count;
    slot->title = fresh;
    slot->count = freshCount;
    guiUnlock();

    if (old != NULL) {
        for (i = 0; i < oldCount; i++)
            free(old[i]);
        free(old);
    }
}

/* The size keepTitle gave a kept title: the shown title, then (RA builds) the clean copy after it. */
static size_t keptSize(const char *kept)
{
    size_t size = strlen(kept) + 1;
#ifdef RETROACHIEVEMENTS
    size += strlen(kept + size) + 1;
#endif
    return size;
}

static char *keptCopy(const char *kept)
{
    size_t size;
    char *copy;

    if (kept == NULL)
        return NULL;
    size = keptSize(kept);
    copy = (char *)malloc(size);
    if (copy != NULL)
        memcpy(copy, kept, size);
    return copy;
}

/* What a memo is keyed by: the row's view (a PS1 and a PS2 game can share a name) and its name. */
static char *memoKey(item_list_t *support, int id, unsigned int *hash)
{
    const char *name = gsOf(support)->name(support, id);
    size_t len;
    char *key;
    unsigned int h = 2166136261u;

    if (name == NULL)
        return NULL;
    len = strlen(name);
    key = (char *)malloc(len + 2);
    if (key == NULL)
        return NULL;
    key[0] = libListRowView(support, id) == LIB_VIEW_PS1 ? '1' : '2';
    memcpy(key + 1, name, len + 1);
    for (const char *p = key; *p; p++)
        h = (h ^ (unsigned char)*p) * 16777619u;
    *hash = h;
    return key;
}

CfgTitles::Memo *CfgTitles::memoFind(Slot *slot, unsigned int hash, const char *key)
{
    int i;
    for (i = 0; i < slot->memoCount; i++)
        if (slot->memo[i].hash == hash && !strcmp(slot->memo[i].key, key))
            return &slot->memo[i];
    return NULL;
}

/* Remember what the pass found for a row, a missing title included, so the next rebuild knows it. Takes key. */
void CfgTitles::memoKeep(Slot *slot, char *key, unsigned int hash, const char *title)
{
    Memo *memo = memoFind(slot, hash, key);
    char *copy = keptCopy(title);

    if (title != NULL && copy == NULL) {
        free(key); /* out of memory: forget nothing, learn nothing */
        return;
    }
    if (memo != NULL) {
        free(key);
        free(memo->title);
        memo->title = copy;
        return;
    }
    if (slot->memoCount == slot->memoSize) {
        int size = slot->memoSize ? slot->memoSize * 2 : 64;
        Memo *grown = (Memo *)realloc(slot->memo, (size_t)size * sizeof(Memo));
        if (grown == NULL) {
            free(key);
            free(copy);
            return;
        }
        slot->memo = grown;
        slot->memoSize = size;
    }
    slot->memo[slot->memoCount].hash = hash;
    slot->memo[slot->memoCount].key = key;
    slot->memo[slot->memoCount].title = copy;
    slot->memoCount++;
}

CfgTitles::~CfgTitles()
{
    int i, j;
    for (i = 0; i < MODE_COUNT; i++) {
        Slot *slot = &slots_[i];
        for (j = 0; j < slot->count; j++)
            free(slot->title[j]);
        free(slot->title);
        for (j = 0; j < slot->memoCount; j++) {
            free(slot->memo[j].key);
            free(slot->memo[j].title);
        }
        free(slot->memo);
    }
}

int CfgTitles::prefill(item_list_t *support, int count)
{
    Slot *slot;
    char **fresh;
    int i, misses = 0;

    if (!gCfgTitlesInLists || support == NULL || support->mode == APP_MODE || count <= 0 ||
        (slot = claimSlot(support)) == NULL)
        return 0;
    fresh = (char **)malloc((size_t)count * sizeof(char *));
    if (fresh == NULL)
        return count;
    memset(fresh, 0, (size_t)count * sizeof(char *));
    for (i = 0; i < count; i++) {
        unsigned int hash;
        char *key;
        Memo *memo;

        if (untitledRow(support, i))
            continue;
        key = memoKey(support, i, &hash);
        memo = key != NULL ? memoFind(slot, hash, key) : NULL;
        free(key);
        if (memo == NULL)
            misses++;
        else if (memo->title != NULL && (fresh[i] = keptCopy(memo->title)) == NULL)
            misses++;
    }
    swapTable(slot, fresh, count);
    return misses;
}

void CfgTitles::forget(item_list_t *support)
{
    Slot *slot = findSlot(support);

    if (slot != NULL && slot->title != NULL)
        swapTable(slot, NULL, 0);
}

void CfgTitles::refresh(item_list_t *support, int count)
{
    Slot *slot;
    char **fresh = NULL;
    int i, freshCount = 0;

    if (support == NULL || (slot = claimSlot(support)) == NULL)
        return;

    if (gCfgTitlesInLists && support->mode != APP_MODE && count > 0) {
        fresh = (char **)malloc((size_t)count * sizeof(char *));
        if (fresh != NULL) {
            char clean[CFG_TITLE_MAX];

            /* which rows' CFGs were read in full: only those results are remembered (calloc: no failure path) */
            unsigned char *read = (unsigned char *)calloc((size_t)count, 1);

            memset(fresh, 0, (size_t)count * sizeof(char *));
            freshCount = count;
            for (i = 0; i < count; i++) {
                config_set_t *config;
                const char *title = NULL;

                if (untitledRow(support, i))
                    continue;
                {
                    /* A PS1 row named without its id finds CFG/<id>.cfg only once the disc id is
                       known. The art and caption paths learn it later, so ask the disc here, on the
                       I/O thread, or the first build shows file names until a refresh (CosmicScale). */
                    int ember = 0;
                    if (libListRowView(support, i) == LIB_VIEW_PS1 && gsOf(support)->ps1RowKind(support, i, &ember))
                        ps1RowIdResolve(((base_game_info_t *)gsOf(support)->game(support, i))->name,
                                        ember ? PS1_ROW_CUE : PS1_ROW_VCD, gsOf(support)->rowPrefix(support, i));
                }
                config = gsOf(support)->config(support, i);
                if (config == NULL)
                    continue; /* not a result: out of memory or a stale row (a game with no CFG still gets a config) */
                if (configGetStr(config, "Title", &title) && cfgTitleClean(title, clean, sizeof(clean)))
                    fresh[i] = keepTitle(support, i, clean);
                else
                    title = NULL;
                if (read != NULL)
                    read[i] = title == NULL || fresh[i] != NULL; /* an out-of-memory title is not a result */
                configFree(config);
            }
            /* Remember every row's result, so the next rebuild of this list (or a view switch back to
               it) can show the titles before it reads them again. A row whose CFG could not be read
               is not remembered, so it is read before its rows the next time too. */
            for (i = 0; i < count; i++) {
                unsigned int hash;
                char *key;

                if (read == NULL || !read[i] || untitledRow(support, i))
                    continue;
                key = memoKey(support, i, &hash);
                if (key != NULL)
                    memoKeep(slot, key, hash, fresh[i]);
            }
            free(read);
        }
    }

    swapTable(slot, fresh, freshCount);
}

const char *CfgTitles::forRow(item_list_t *support, int id)
{
    Slot *slot;

    if (!gCfgTitlesInLists)
        return NULL; /* switched off: file names at once, before the lists rebuild */
    slot = findSlot(support);
    if (slot == NULL || slot->title == NULL || id < 0 || id >= slot->count)
        return NULL;
    return slot->title[id];
}

const char *CfgTitles::displayName(item_list_t *support, int id, const char *text)
{
    const char *title = forRow(support, id);
    return title != NULL ? title : vcdDisplayNameForRow(support, id, text);
}

const char *CfgTitles::sortName(item_list_t *support, int id, const char *text)
{
    const char *title = forRow(support, id);
#ifdef RETROACHIEVEMENTS
    if (title != NULL)
        title += strlen(title) + 1; // the clean copy keepTitle stores after the shown title
#endif
    return title != NULL ? title : vcdDisplayNameForRow(support, id, text);
}

// Forwarding API follows the existing RaBadge cache boundary.
int cfgTitleClean(const char *in, char *out, int size) { return CfgTitles::clean(in, out, size); }
int cfgTitlePrefill(item_list_t *support, int count) { return gCfgTitles.prefill(support, count); }
void cfgTitleRefresh(item_list_t *support, int count) { gCfgTitles.refresh(support, count); }
void cfgTitleForget(item_list_t *support) { gCfgTitles.forget(support); }
const char *cfgTitleForRow(item_list_t *support, int id) { return gCfgTitles.forRow(support, id); }
const char *cfgTitleDisplayName(item_list_t *support, int id, const char *text) { return gCfgTitles.displayName(support, id, text); }
const char *cfgTitleSortName(item_list_t *support, int id, const char *text) { return gCfgTitles.sortName(support, id, text); }
