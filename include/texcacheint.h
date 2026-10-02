#ifndef __TEXCACHEINT_H
#define __TEXCACHEINT_H

// Internals shared between texcache.cpp (cache entries and the per-frame lookup) and artload.cpp (the art worker and
// loader). Not public API.

#include "texcache.h"
#include "artqueue.h"
#include "artworker.h"

void cacheWakeArtWorker(void);
extern ArtQueue gArtQ;
extern ArtWorker gArtW;

// Shared tuning (used by the per-frame lookup in texcache.cpp and the loader in artload.cpp).
/* Minimum frames of no input before any art may be requested on an SIO2 device, regardless of the
   Art Delay setting. See the gate in cacheGetTexture for why this floor exists.

   THE NUMBER IS 8 BECAUSE 8 IS WHAT SHIPPED. While the Art Delay default was 8 this floor was
   dead code on every device -- max(8, 3) is 8 -- so the only settle MMCE and MX4SIO were ever
   observed to work with is 8 frames, and the 3 here had never once been the operative value.
   Dropping the default to 0 promoted this constant from decoration to the live setting and cut
   the quiet window on the card bus from ~133 ms to ~50 ms without anyone having tested that.

   MMCE pays for that immediately. Its VCD rescan has to land an opendir on the card's POPS folder
   against the same bus the covers are on, and mmceman collapses EVERY dopen failure into a bare
   -1 -- so a merely contended open is indistinguishable from a real error and counts against
   MMCE_VCD_SCAN_RETRY_MAX. Burn all 15 and the VCD page quiesces empty for the rest of the
   session, with only an L3 toggle re-arming it. Sitting still never recovers.

   USB, ATA and the network transports keep the user's setting, 0 included: they share nothing with
   the pad, which is the entire reason this floor is scoped to SIO2 rather than applied globally. */
constexpr int ART_MIN_SETTLE_FRAMES = 8;

// Frames a queued art request may go undrawn before the worker abandons it. Generous enough that a
// brief hitch does not throw away a wanted load, short enough that a scroll's worth of superseded
// covers never reaches the device.
constexpr int ART_CANCEL_STALE_FRAMES = 20;

constexpr int ART_FOREGROUND_DRAIN_MS = 500;

// texcache.cpp: release a cache slot (freeTxt != 0 also frees the texture).
void cacheClearItem(cache_entry_t *item, int freeTxt);

#endif
