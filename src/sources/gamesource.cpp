// GameSource: the base class of the game sources; defaults for the optional methods. See gamesource.h.

#include "include/opl.h"
#include "include/gamesource.h"
#include "include/cuesupport.h"  // cueIsCueEntry
#include "include/supportbase.h" // sbResolvePs1Size
#include "include/vcdsupport.h"  // PS1_ROW_* + the PS1-row id memo

GameSource::GameSource(short int mode_, char appsPriority_, unsigned char flags_, int delay_, int updateDelay_, unsigned caps)
{
    mode = mode_;
    appsPriority = appsPriority_;
    enabled = 0;
    flags = flags_;
    delay = delay_;
    updateDelay = updateDelay_;
    priv = NULL;
    owner = NULL;
    viewOverride = 0;
    source = this;
    caps_ = caps;
}

char *GameSource::prefix(item_list_t *) { return NULL; }
void GameSource::deleteGame(item_list_t *, int) {}
void *GameSource::game(item_list_t *, int) { return NULL; }
int GameSource::checkVmc(item_list_t *, char *, int) { return -1; }
int GameSource::createVmc(item_list_t *itemList, char *name, int createSize, int) { return checkVmc(itemList, name, createSize); }
void GameSource::launchVcd(item_list_t *, const char *, config_set_t *) {}
int GameSource::artArchivePath(item_list_t *, char *, int) { return 0; }
void GameSource::launchCue(item_list_t *, const char *, config_set_t *) {}
int GameSource::rowView(item_list_t *, int) { return 0; }
int GameSource::sourceId(item_list_t *, int id) { return id; }
int GameSource::saveCueSettings(item_list_t *, int, const char *, config_set_t *) { return 0; }
char *GameSource::rowPrefix(item_list_t *itemList, int) { return hasPrefix() ? prefix(itemList) : NULL; }
int GameSource::ps1SaveDir(item_list_t *, int, const char *, int *, char *, int, char *, int) { return 0; }
void GameSource::ps1Size(item_list_t *itemList, base_game_info_t *game, config_set_t *config)
{
    if (hasPrefix())
        sbResolvePs1Size(game, prefix(itemList), config);
}

int GameSource::ps1RowKind(item_list_t *itemList, int id, int *ember)
{
    const base_game_info_t *row = hasGame() ? (const base_game_info_t *)game(itemList, id) : NULL;
    if (row == NULL)
        return 0;
    *ember = cueIsCueEntry(row);
    return *ember || strcasecmp(row->extension, ".VCD") == 0;
}

int GameSource::ps1ArtId(item_list_t *itemList, int id, const char *startup, char *out, int outSize)
{
    // Favourites match the art value against fav identities (favGetArtMode); keep them name-keyed.
    if (itemList->mode == FAV_MODE || itemList->mode == APP_MODE || !hasGame())
        return 0;
    const base_game_info_t *row = (const base_game_info_t *)game(itemList, id);
    if (row == NULL)
        return 0;
    int kind = PS1_ROW_VCD;
    if (cueIsCueEntry(row))
        kind = PS1_ROW_CUE;
    else if (strcasecmp(row->extension, ".VCD") != 0)
        return 0;
    const char *root = gsOf(itemList)->prefix(itemList);
    ps1RowIdRequest(startup, kind, root); // verify provisional filename ids too
    return ps1RowIdCached(startup, kind, out, outSize, root);
}
