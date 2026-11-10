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
    // createSize also accepts SYS_VMC_PROBE_FORMAT for read-only auto-assignment validation.
    virtual int checkVmc(item_list_t *itemList, char *name, int createSize);
    // Creation that knows the launch core: native BDM cards need one reserved extent (original #994).
    virtual int createVmc(item_list_t *itemList, char *name, int createSize, int requireContiguous);
    virtual int iconId(item_list_t *itemList) = 0;

    // Optional capabilities (declared through Caps).
    virtual void launchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet);
    virtual int artArchivePath(item_list_t *itemList, char *out, int outSize);
    virtual void launchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet);
    virtual int rowView(item_list_t *itemList, int id);
    virtual int sourceId(item_list_t *itemList, int id);
    virtual int saveCueSettings(item_list_t *itemList, int id, const char *name, config_set_t *configSet);
    // A PS1 row's own folder, which holds its dedicated VMCs, for the 3D save icon: 1 with directory
    // set, *ember = 1 for an Ember row, and partition = the APA partition to mount on pfs1: first
    // ("" when the folder is already reachable). 0 when the source has none. String work only.
    // The device prefix a row's files live under (its VMC folder), or NULL. Favourites answer for
    // the source the row belongs to.
    virtual char *rowPrefix(item_list_t *itemList, int id);
    // Info-screen #Size of a PS1 row, measured under the source's PS1 root (supportbase.h
    // sbResolvePs1Size). Defaults to the source prefix; BDM/MMCE use the device root, HDD its rows.
    virtual void ps1Size(item_list_t *itemList, base_game_info_t *game, config_set_t *config);
    virtual int ps1SaveDir(item_list_t *itemList, int id, const char *name, int *ember, char *directory, int directorySize,
                           char *partition, int partitionSize);

    // RetroGEM art key for a PS1 row on a device page (original #1049): 1 and the row's disc id --
    // disc-verified, or the free filename parse until the queued background verify lands. 0 for
    // PS2 rows, Favourites and Apps. Never device IO on the calling (render) thread.
    int ps1ArtId(item_list_t *itemList, int id, const char *startup, char *out, int outSize);
    // 1 and *ember for the PS1 row behind id (an Ember .CUE row or a POPSTARTER .VCD row), else 0.
    int ps1RowKind(item_list_t *itemList, int id, int *ember);

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
