/* RA: the "supported by RetroAchievements" badge in the game list.
   See src/sources/rabadge.cpp */
#ifndef __RABADGE_H__
#define __RABADGE_H__

#include "include/iosupport.h"

struct ra_badge_slot
{
    item_list_t *support;
    int count;
    char *text; /* count strings of RA_BADGE_TEXT each, or NULL */
};

// The "supported by RetroAchievements" badge cache (src/sources/rabadge.cpp):
// one slot per device list, rebuilt on the I/O thread when the game list is
// refreshed. One instance; the free functions below are thin forwarders.
class RaBadge
{
public:
    /* Rebuilds the badges for one device's list. Call from the I/O thread
       when the game list is refreshed; file operations are safe there. */
    void refresh(item_list_t *support, int count);

    /* The game name with the badge when the game is checked and supported,
       otherwise NULL, meaning show the plain name. The pointer stays valid
       until the next refresh of the same device. */
    const char *text(item_list_t *support, int idx);

private:
    struct ra_badge_slot *slotFor(item_list_t *support);
    static int watchListExists(const char *prefix, const char *serial);

    /* Keyed on MODE_COUNT: the fork has more device lists than upstream's
       four, and a four-slot table mis-attributes lists across devices. */
    struct ra_badge_slot slots_[MODE_COUNT] = {};
};

extern RaBadge gRaBadge;

/* The free API other translation units call; both forward to gRaBadge. */
void raBadgeRefresh(item_list_t *support, int count);
const char *raBadgeText(item_list_t *support, int idx);

#endif
