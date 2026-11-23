#ifndef CFGTITLE_H
#define CFGTITLE_H

#include <tamtypes.h>
#include "include/iosupport.h"

constexpr int CFG_TITLE_MAX = 128;

// Per-source presentation cache. Source/launch/CFG/art/Favorites identity stays unchanged.
// Refresh/forget belong to the IO worker; GUI readers borrow titles within the frame lock.
class CfgTitles
{
public:
    ~CfgTitles();
    static int clean(const char *in, char *out, int size);
    // Fast, before the rows are listed: titles this source showed in an earlier pass (any view), from memory.
    // Returns how many rows it had no answer for (those need the slow pass before they are shown).
    int prefill(item_list_t *support, int count);
    void refresh(item_list_t *support, int count);
    void forget(item_list_t *support);
    const char *forRow(item_list_t *support, int id);
    const char *displayName(item_list_t *support, int id, const char *text);
    // What a game row sorts by: displayName without the cosmetic RA mark.
    const char *sortName(item_list_t *support, int id, const char *text);

private:
    struct Memo
    {
        unsigned int hash;
        char *key;   // the row's view and name
        char *title; // as keepTitle made it
    };
    struct Slot
    {
        item_list_t *support;
        int count;
        char **title;
        Memo *memo; // every title a pass found, kept across rebuilds and view switches; IO thread only
        int memoCount, memoSize;
    };
    Slot slots_[MODE_COUNT] = {};
    Slot *findSlot(item_list_t *support);
    Slot *claimSlot(item_list_t *support);
    static void swapTable(Slot *slot, char **fresh, int count);
    static Memo *memoFind(Slot *slot, unsigned int hash, const char *key);
    static void memoKeep(Slot *slot, char *key, unsigned int hash, const char *title);
};

extern CfgTitles gCfgTitles;
int cfgTitleClean(const char *in, char *out, int size);
int cfgTitlePrefill(item_list_t *support, int count);
void cfgTitleRefresh(item_list_t *support, int count);
void cfgTitleForget(item_list_t *support);
const char *cfgTitleForRow(item_list_t *support, int id);
const char *cfgTitleDisplayName(item_list_t *support, int id, const char *text);
const char *cfgTitleSortName(item_list_t *support, int id, const char *text);

#endif
