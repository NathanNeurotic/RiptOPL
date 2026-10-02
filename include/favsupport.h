#ifndef __FAVSUPPORT_H
#define __FAVSUPPORT_H

#include "include/iosupport.h"

constexpr int FAV_MODE_UPDATE_DELAY = 240;
constexpr int FAV_TEXT_MAX = 256;     // cap on a stored favourite's text length (incl. NUL)
constexpr int FAV_MAX_ITEMS = 512;    // cap on records read from favourites.bin
constexpr int FAV_MAGIC = 0x4641464F; // 'OFAV' (little-endian on the EE)
#define FAV_VERSION 3                 // v3 widens v2's isVcd byte to a KIND (see FAV_KIND_*)

// Defined in favsupport.c; consumed by opl.c/gui.c (config load/save/default).
extern int gFAVStartMode;

// Which shelf a favourite belongs on, and which core launches it. Stored in the byte OFAV v2 used
// for isVcd, so the on-disk record layout is unchanged and only its meaning widens.
//   ISO -- a PS2 disc game, launched by id through the source's itemLaunch
//   VCD -- a PS1 title launched by NAME through POPSTARTER (itemLaunchVcd)
//   CUE -- a PS1 title launched by NAME through Ember     (itemLaunchCue)
//   ELF -- a homebrew app, launched by id from the APPS tab
// VCD and CUE share the Favourites PS1 shelf; the kind picks the core, exactly as a row's extension
// does on a device page.
enum FAV_KIND {
    FAV_KIND_ISO = 0,
    FAV_KIND_VCD,
    FAV_KIND_CUE,
    FAV_KIND_ELF
};

// In-memory, validated favourites. Rebuilt by the store rebuild (FavSupport::ingest, driven from
// FavSource::update); each entry's owner/id are confirmed present in the source submenu, so
// proxying never goes OOB.
typedef struct
{
    item_list_t *owner; // resolved source list
    int mode;           // resolved source mode (BDM re-matched across slots)
    int id;             // source item id (validated present in owner's submenu; unused for VCD)
    int icon_id;
    int text_id;
    int kind;   // FAV_KIND_*: which shelf it sits on and which core launches it
    char *text; // heap copy; owned by FavSupport (for a VCD favourite this is the .VCD basename / launch name)
} fav_rec_t;

// Raw on-disk record (pre-validation).
typedef struct
{
    int mode;
    int id;
    int icon_id;
    int text_id;
    int kind; // FAV_KIND_*; v1 files and foreign imports default to FAV_KIND_ISO
    char text[FAV_TEXT_MAX];
} fav_raw_t;

// The Favourites store (src/sources/favsupport.cpp). One instance (gFavSupport). Owns the
// validated in-memory favourite array (rebuilt from favourites.bin), the deferred-rebuild
// one-shot and the APPS auto-init one-shot; the favXxx free functions below forward to it, and
// FavSource (the GameSource) reaches the store through it as well.
class FavSupport
{
public:
    // The deferred-rebuild one-shot: raised by loadFavourites (initial boot, every source-list
    // change, and each R3 toggle) and consumed exactly once by FavSource::needsUpdate, so the
    // deferred-update driver actually runs the rebuild. Starting at 1 makes the FIRST deferred
    // pass populate the tab; a self-clearing one-shot -- never a constant 1 -- avoids a
    // per-frame rebuild storm.
    void requestRebuild() { forceUpdate_ = 1; }
    int consumeRebuildRequest()
    {
        int pending = forceUpdate_;
        forceUpdate_ = 0;
        return pending;
    }

    // In-memory array lifecycle. clear() NULLs array_/count_ before the rebuild's first blocking
    // point, so a GUI-thread scan started around a rebuild hits the live()==false guard.
    void clear();
    // Single append pass over freshly-read raw records: resolve each against its source, filter
    // by the FAV page's L3 shelf, and append the survivors. Returns the new count. Never clears
    // or appends the submenu -- the caller's deferred driver does that from the count.
    int ingest(const fav_raw_t *recs, int rawCount);
    // Arm the APPS scan once per boot when a stored app favourite's APPS tab never loaded.
    void armAppsScan();

    // Store-backed queries. All index by FAV-list id and return -1 / NULL when the store is not
    // live or the id is out of range.
    bool live() const { return array_ != NULL; }
    int itemCount() const { return count_; }
    const fav_rec_t *entry(int id) const { return validIndex(id) ? &array_[id] : NULL; }
    int itemSourceMode(int id) const;
    int itemKind(int id) const;
    int itemView(int id) const;
    int artMode(const char *value);
    // Dynamic mode flags of the currently-selected FAV item (its source's live flags).
    unsigned char selectedItemFlags();

    // Build a stack-local view of a favourite's LIVE source support. Only viewOverride changes;
    // priv, callbacks, flags and owner all come from the current source object. The returned
    // pointer is valid only as long as `view` remains in scope.
    item_list_t *ownerView(int id, item_list_t *view);

    // R3 toggle API.
    int addItem(int mode, int id, int icon_id, int text_id, const char *text, int kind);
    int removeByIdAndText(int mode, int id, const char *text, int kind);
    void removeByIndex(int favIndex);

private:
    bool validIndex(int id) const { return array_ != NULL && id >= 0 && id < count_; }

    fav_rec_t *array_ = NULL;
    int count_ = 0;
    int forceUpdate_ = 1;
    int appsArmed_ = 0;
};

extern FavSupport gFavSupport;


item_list_t *favGetObject(int initOnly);

// Dynamic mode flags for the currently-selected FAV item (its source's flags). Used by
// itemExecTriangle so a FAV item exposes the same Options menu as its source would.
unsigned char favGetFlags(item_list_t *itemList);

// Source device mode of the FAV item at FAV-list index id, or -1 if out of range. Used by the
// theme engine to draw APP favourites with the apps element (proper art box + overlay).
int favGetItemSourceMode(int id);


// FAV_KIND_* of one displayed favourite, or -1 when id is invalid (which core a PS1 favourite uses).
int favGetItemKind(int id);

// Source device mode of the favourite whose art-cache value (source startup / VCD name) matches,
// or -1 if none does. Used by texcache's cacheGetEffectiveMode so MMCE idle
// deferral, abort, and worker-priority rules follow the device a FAV-tab read
// actually lands on. GUI thread only -- the store array is rebuilt
// unlocked on the IO worker; see FavSupport::artMode's safety note in favsupport.cpp.
int favGetArtMode(const char *value);

// R3-toggle helpers (called from opl.c). add/remove rewrite favourites.bin and return 1 on a
// successful write, 0 on failure (so the caller won't set a lying star). add returns 1 if the
// item is already present. removeFavouriteByIdAndText matches mode (BDM-lenient) + id + text + isVcd.
// isVcd = 1 when the favourited item was a PS1/.VCD entry (device was in its L3 VCD view); such
// favourites resolve + launch as POPSTARTER rather than as a disc image. See favsupport.c.
int addFavouriteItem(int mode, int id, int icon_id, int text_id, const char *text, int kind);
int removeFavouriteByIdAndText(int mode, int id, const char *text, int kind);

// Remove the favourite at FAV-list index favIndex (R3 pressed on the Favourites tab).
void favRemoveByIndex(int favIndex);

// Cheap/idempotent: clears the FAV list + schedules its single deferred rebuild.
void loadFavourites(void);

#endif
