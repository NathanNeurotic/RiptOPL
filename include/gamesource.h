#ifndef __GAMESOURCE_H
#define __GAMESOURCE_H

// A game source (BDM, HDD, SMB, HTTP, ...) as a class. GameSource IS an item_list_t (the per-page data: mode, flags,
// priv, owner, ...), and callers reach the behaviour through gsOf(list)->method(list, ...). gsOf finds the object
// through item_list_t::source rather than by casting the list, because Favourites and the BDM device slots make
// shallow COPIES of a list -- the copy keeps `source` but is not a GameSource itself. Every virtual receives the
// list the caller used, so a proxy's own fields (viewOverride, priv, ...) are what the method sees.

#include "iosupport.h"

class GameSource: public item_list_t
{
public:
    // Optional methods: declared through these flags, and callers ask the has*() queries before using them.
    enum Caps : unsigned {
        CAP_LAUNCH_VCD = 1u << 0,
        CAP_ART_ARCHIVE_PATH = 1u << 1,
        CAP_LAUNCH_CUE = 1u << 2,
        CAP_ROW_VIEW = 1u << 3,
        CAP_SOURCE_ID = 1u << 4,
        // Entries that exist for most sources but are NULL in a few (App and Favourites have no prefix/raw game pointer,
        // App has no VMC check): pass the OMIT_ flag and leave the virtual at its default.
        OMIT_PREFIX = 1u << 5,
        OMIT_GAME = 1u << 6,
        OMIT_CHECK_VMC = 1u << 7,
        OMIT_DELETE = 1u << 8, // HDD cannot delete games from the menu
        CAP_SAVE_CUE_SETTINGS = 1u << 9,
    };

    GameSource(short int mode, char appsPriority, unsigned char flags, int delay, int updateDelay, unsigned caps);

    // item_list_t callbacks as virtuals (same parameters, `itemList` first).
    virtual int textId(item_list_t *itemList) = 0;
    virtual char *prefix(item_list_t *itemList);
    virtual void init(item_list_t *itemList) = 0;
    virtual int needsUpdate(item_list_t *itemList) = 0;
    virtual int update(item_list_t *itemList) = 0;
    virtual int count(item_list_t *itemList) = 0;
    virtual void *game(item_list_t *itemList, int id);
    virtual char *name(item_list_t *itemList, int id) = 0;
    virtual int nameLength(item_list_t *itemList, int id) = 0;
    virtual char *startup(item_list_t *itemList, int id) = 0;
    virtual void deleteGame(item_list_t *itemList, int id);
    virtual void rename(item_list_t *itemList, int id, char *newName) = 0;
    virtual void launch(item_list_t *itemList, int id, config_set_t *configSet) = 0;
    virtual config_set_t *config(item_list_t *itemList, int id) = 0;
    virtual int image(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm) = 0;
    virtual void cleanUp(item_list_t *itemList, int exception) = 0;
    virtual void shutdown(item_list_t *itemList) = 0;
    virtual int checkVmc(item_list_t *itemList, char *name, int createSize);
    virtual int iconId(item_list_t *itemList) = 0;

    // Optional capabilities (declared through Caps).
    virtual void launchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet);
    virtual int artArchivePath(item_list_t *itemList, char *out, int outSize);
    virtual void launchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet);
    virtual int rowView(item_list_t *itemList, int id);
    virtual int sourceId(item_list_t *itemList, int id);
    virtual int saveCueSettings(item_list_t *itemList, int id, const char *name, config_set_t *configSet);

    bool hasPrefix() const { return !(caps_ & OMIT_PREFIX); }
    bool hasGame() const { return !(caps_ & OMIT_GAME); }
    bool hasDelete() const { return !(caps_ & OMIT_DELETE); }
    bool hasCheckVmc() const { return !(caps_ & OMIT_CHECK_VMC); }
    bool hasLaunchVcd() const { return (caps_ & CAP_LAUNCH_VCD) != 0; }
    bool hasArtArchivePath() const { return (caps_ & CAP_ART_ARCHIVE_PATH) != 0; }
    bool hasLaunchCue() const { return (caps_ & CAP_LAUNCH_CUE) != 0; }
    bool hasRowView() const { return (caps_ & CAP_ROW_VIEW) != 0; }
    bool hasSaveCueSettings() const { return (caps_ & CAP_SAVE_CUE_SETTINGS) != 0; }
    bool hasSourceId() const { return (caps_ & CAP_SOURCE_ID) != 0; }

protected:
    ~GameSource() = default; // sources are static objects; never deleted through the base

private:
    unsigned caps_;
};

/// The source behind a list (the list itself, a Favourites proxy copy, or a BDM device slot).
inline GameSource *gsOf(item_list_t *list)
{
    return static_cast<GameSource *>(list->source);
}

inline const GameSource *gsOf(const item_list_t *list)
{
    return static_cast<const GameSource *>(list->source);
}

#endif
