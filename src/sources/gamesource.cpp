// GameSource: the item_list_t function table filled with thunks that call the virtual methods. See gamesource.h.

#include "include/opl.h"
#include "include/gamesource.h"

static inline GameSource *src(item_list_t *l)
{
    return static_cast<GameSource *>(l->source);
}

static int tTextId(item_list_t *l) { return src(l)->textId(l); }
static char *tPrefix(item_list_t *l) { return src(l)->prefix(l); }
static void tInit(item_list_t *l) { src(l)->init(l); }
static int tNeedsUpdate(item_list_t *l) { return src(l)->needsUpdate(l); }
static int tUpdate(item_list_t *l) { return src(l)->update(l); }
static int tCount(item_list_t *l) { return src(l)->count(l); }
static void *tGame(item_list_t *l, int id) { return src(l)->game(l, id); }
static char *tName(item_list_t *l, int id) { return src(l)->name(l, id); }
static int tNameLength(item_list_t *l, int id) { return src(l)->nameLength(l, id); }
static char *tStartup(item_list_t *l, int id) { return src(l)->startup(l, id); }
static void tDelete(item_list_t *l, int id) { src(l)->deleteGame(l, id); }
static void tRename(item_list_t *l, int id, char *newName) { src(l)->rename(l, id, newName); }
static void tLaunch(item_list_t *l, int id, config_set_t *cfg) { src(l)->launch(l, id, cfg); }
static config_set_t *tConfig(item_list_t *l, int id) { return src(l)->config(l, id); }
static int tImage(item_list_t *l, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *tex, short psm)
{
    return src(l)->image(l, folder, isRelative, value, suffix, tex, psm);
}
static void tCleanUp(item_list_t *l, int exception) { src(l)->cleanUp(l, exception); }
static void tShutdown(item_list_t *l) { src(l)->shutdown(l); }
static int tCheckVmc(item_list_t *l, char *name, int createSize) { return src(l)->checkVmc(l, name, createSize); }
static int tIconId(item_list_t *l) { return src(l)->iconId(l); }
static void tLaunchVcd(item_list_t *l, const char *n, config_set_t *cfg) { src(l)->launchVcd(l, n, cfg); }
static int tArtArchive(item_list_t *l, char *out, int outSize) { return src(l)->artArchivePath(l, out, outSize); }
static void tLaunchCue(item_list_t *l, const char *n, config_set_t *cfg) { src(l)->launchCue(l, n, cfg); }
static int tRowView(item_list_t *l, int id) { return src(l)->rowView(l, id); }
static int tSourceId(item_list_t *l, int id) { return src(l)->sourceId(l, id); }


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
    install(caps);
}

void GameSource::install(unsigned caps)
{
    itemTextId = &tTextId;
    itemGetPrefix = &tPrefix;
    itemInit = &tInit;
    itemNeedsUpdate = &tNeedsUpdate;
    itemUpdate = &tUpdate;
    itemGetCount = &tCount;
    itemGet = &tGame;
    itemGetName = &tName;
    itemGetNameLength = &tNameLength;
    itemGetStartup = &tStartup;
    itemDelete = &tDelete;
    itemRename = &tRename;
    itemLaunch = &tLaunch;
    itemGetConfig = &tConfig;
    itemGetImage = &tImage;
    itemCleanUp = &tCleanUp;
    itemShutdown = &tShutdown;
    itemCheckVMC = &tCheckVmc;
    itemIconId = &tIconId;
    itemLaunchVcd = (caps & CAP_LAUNCH_VCD) ? &tLaunchVcd : NULL;
    itemGetArtArchivePath = (caps & CAP_ART_ARCHIVE_PATH) ? &tArtArchive : NULL;
    itemLaunchCue = (caps & CAP_LAUNCH_CUE) ? &tLaunchCue : NULL;
    itemGetView = (caps & CAP_ROW_VIEW) ? &tRowView : NULL;
    itemGetSourceId = (caps & CAP_SOURCE_ID) ? &tSourceId : NULL;
}

void GameSource::launchVcd(item_list_t *, const char *, config_set_t *) {}
int GameSource::artArchivePath(item_list_t *, char *, int) { return 0; }
void GameSource::launchCue(item_list_t *, const char *, config_set_t *) {}
int GameSource::rowView(item_list_t *, int) { return 0; }
int GameSource::sourceId(item_list_t *, int id) { return id; }
