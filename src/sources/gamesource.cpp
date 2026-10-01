// GameSource: the base class of the game sources; defaults for the optional methods. See gamesource.h.

#include "include/opl.h"
#include "include/gamesource.h"

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
void GameSource::launchVcd(item_list_t *, const char *, config_set_t *) {}
int GameSource::artArchivePath(item_list_t *, char *, int) { return 0; }
void GameSource::launchCue(item_list_t *, const char *, config_set_t *) {}
int GameSource::rowView(item_list_t *, int) { return 0; }
int GameSource::sourceId(item_list_t *, int id) { return id; }
