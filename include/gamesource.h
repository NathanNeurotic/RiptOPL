#ifndef __GAMESOURCE_H
#define __GAMESOURCE_H

// A game source (BDM, HDD, SMB, HTTP, ...) as a class. GameSource IS an item_list_t: the constructor installs thunks
// into the function-pointer table, so every existing consumer (`list->itemUpdate(list)`) keeps working while a backend
// is migrated; the thunks call the virtuals below. The thunks find their object through item_list_t::source rather
// than by casting the list, because Favourites makes shallow COPIES of a list as a proxy -- the copy keeps `source`
// but is not a GameSource itself. Every virtual receives the list the caller used, so a proxy's own fields
// (viewOverride, ...) are what the method sees, exactly as with the C functions.

#include "iosupport.h"

class GameSource: public item_list_t
{
public:
    // Optional entries: the table pointer is installed only when the capability is declared, because callers test it
    // against NULL.
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
    };

    // The table is filled by install(), called from the subclass constructor: a base constructor must not see
    // the derived vtable's capabilities.
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
    virtual void deleteGame(item_list_t *itemList, int id) = 0;
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

protected:
    ~GameSource() = default; // sources are static objects; never deleted through the base

private:
    void install(unsigned caps);
};

#endif
