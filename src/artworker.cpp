// Cover-art worker state: wake and selection-focus tracking. Split out of texcache.cpp.
#include "include/opl.h"
#include "include/artworker.h"

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

    // Do not forcibly kill the running request here. It may be a warmed neighbor that just became the
    // selected cover; its priority lookup below will adopt it into this new epoch. If it is truly
    // obsolete, cacheLoadImage drops it before IO when possible; a whole-file RPC already in flight
    // remains intentionally unkillable for device/RPC safety.
}

