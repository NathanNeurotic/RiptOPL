// Cover-art worker state: wake and selection-focus tracking. Split out of texcache.cpp.
#include "include/opl.h"
#include "include/artworker.h"
#include "include/gamesource.h"
#include "include/sound.h"

#include <stdio.h>
#include <string.h>

void ArtWorker::wake()
{
    if (running && threadId >= 0)
        WakeupThread(threadId);
}

static unsigned int focusHashValue(const char *value)
{
    unsigned int h = 2166136261u; // FNV-1a
    const unsigned char *p = (const unsigned char *)value;
    while (*p != '\0') {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

void ArtWorker::updateFocus(item_list_t *list, const char *value)
{
    size_t len = strlen(value);
    unsigned int hash = focusHashValue(value);

    if (focusList == list && focusLen == len && focusHash == hash)
        return;

    focusList = list;
    focusLen = len;
    focusHash = hash;
    focusEpoch++;
    if (focusEpoch == 0)
        focusEpoch = 1;

    // Per-game background music lives beside the cover: <device>ART/<value>_BGM.ogg, the same
    // <prefix>ART/<value>_<suffix> path the device's image() builds for _COV. Lists without a
    // filesystem prefix (favourites proxies, HTTP) get the theme music. Same guards as the rest of
    // RiptOPL (gamemanage.cpp, favsupport.cpp): not every list has a source, and not every source a
    // prefix.
    char *prefix = NULL;
    if (list != NULL && list->source != NULL && list->mode != FAV_MODE && list->mode != HTTP_MODE &&
        list->mode != APP_MODE && gsOf(list)->hasPrefix())
        prefix = gsOf(list)->prefix(list);
    if (prefix != NULL && prefix[0] != '\0') {
        char bgmPath[256];
        snprintf(bgmPath, sizeof(bgmPath), "%sART/%s_BGM.ogg", prefix, value);
        bgmFocusGame(bgmPath);
    } else
        bgmFocusGame(NULL);

    // Do not forcibly kill the running request here. It may be a warmed neighbor that just became the
    // selected cover; its priority lookup below will adopt it into this new epoch. If it is truly
    // obsolete, cacheLoadImage drops it before IO when possible; a whole-file RPC already in flight
    // remains intentionally unkillable for device/RPC safety.
}

