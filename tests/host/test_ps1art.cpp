// HOST-TEST: sdk-headers
// ps1art: a PS1 row probes its disc-id art first and falls back to its name art through ONE (cacheId, UID)
// pair. Runs the REAL cache (texcache.cpp) and the REAL art worker's load/publish (artload.cpp) frame by frame,
// with a fake device that has name art and no id art -- the common POPS library -- and checks that a carousel
// which touches as many rows as the cache has slots settles and stays settled instead of flickering.
#include "ps1art_env.h"
#include "../../src/gfx/artqueue.cpp"
#include "../../src/gfx/artworker.cpp"
#include "../../src/gfx/texcache.cpp"
#include "../../src/gfx/artload.cpp"
#include "check.h"

// ---- link stand-ins for everything outside the cache the two real sources reach for ----------------------

int guiFrameId, guiInactiveFrames, gEnableArtTar;
void *_gp;
GameSource::GameSource(short int mode_, char appsPriority_, unsigned char flags_, int delay_, int updateDelay_, unsigned caps)
{
    mode = mode_;
    appsPriority = appsPriority_;
    flags = flags_;
    delay = delay_;
    updateDelay = updateDelay_;
    source = this;
    caps_ = caps;
}
char *GameSource::prefix(item_list_t *) { return NULL; }
void GameSource::deleteGame(item_list_t *, int) {}
void *GameSource::game(item_list_t *, int) { return NULL; }
int GameSource::checkVmc(item_list_t *, char *, int) { return -1; }
int GameSource::createVmc(item_list_t *, char *, int, int) { return -1; }
void GameSource::launchVcd(item_list_t *, const char *, config_set_t *) {}
int GameSource::artArchivePath(item_list_t *, char *, int) { return 0; }
void GameSource::launchCue(item_list_t *, const char *, config_set_t *) {}
int GameSource::rowView(item_list_t *, int) { return 0; }
int GameSource::sourceId(item_list_t *, int id) { return id; }
int GameSource::saveCueSettings(item_list_t *, int, const char *, config_set_t *) { return 0; }
char *GameSource::rowPrefix(item_list_t *, int) { return NULL; }
int GameSource::ps1SaveDir(item_list_t *, int, const char *, int *, char *, int, char *, int) { return 0; }
void GameSource::ps1Size(item_list_t *, base_game_info_t *, config_set_t *) {}
int appGetArtMode(const char *) { return -1; }
int favGetArtMode(const char *) { return -1; }
int hddGetArtArchivePath(item_list_t *, char *, int) { return 0; }
int bdmModeIsSIO2(int) { return 0; }
int bgmDiscretionaryIoAllowed(void) { return 1; }
int getKeyPressed(int) { return 0; }
void artIndexSetOwnerThread(int) {}
void rmUnloadTexture(GSTEXTURE *) {}
int tarLoadFile(TarKind, const char *) { return -1; }
TarEntryBase *tarFindPrefix(TarKind, const char *) { return NULL; }
TarEntryBase *tarFindPrefixExact(TarKind, const char *) { return NULL; }
u32 tarRead(TarKind, const TarEntryBase *, void *, u32) { return 0; }
int texLoadArtFromMemory(GSTEXTURE *, const void *, u32, int) { return -1; }
int texArtAlias(const char *, char *, int) { return 0; }
void texSetLoadAbortFlag(volatile int *) {}
void texFree(GSTEXTURE *texture)
{
    free(texture->Mem);
    texture->Mem = NULL;
}

// ---- the fake device -------------------------------------------------------------------------------------

static int gDeviceReads;

class FakeSource: public GameSource
{
public:
    FakeSource()
        : GameSource(BDM_MODE, 0, 0, 0, 0, 0)
    {
    }
    int textId(item_list_t *) override { return 0; }
    void init(item_list_t *) override {}
    int needsUpdate(item_list_t *) override { return 0; }
    int update(item_list_t *) override { return 0; }
    int count(item_list_t *) override { return 0; }
    char *name(item_list_t *, int) override { return NULL; }
    int nameLength(item_list_t *, int) override { return 0; }
    char *startup(item_list_t *, int) override { return NULL; }
    void rename(item_list_t *, int, char *) override {}
    void launch(item_list_t *, int, config_set_t *) override {}
    config_set_t *config(item_list_t *, int) override { return NULL; }
    void cleanUp(item_list_t *, int) override {}
    void shutdown(item_list_t *) override {}
    int iconId(item_list_t *) override { return 0; }
    // Name art exists ("NAME*"), disc-id art ("SLUS*") does not, unless gIdArt says so.
    int image(item_list_t *, char *, int, char *value, char *, GSTEXTURE *resultTex, short) override
    {
        gDeviceReads++;
        if (strncmp(value, "NAME", 4) == 0 || (gIdArt && strncmp(value, "SLUS", 4) == 0)) {
            resultTex->Mem = (u32 *)malloc(16);
            resultTex->Width = 4;
            resultTex->Height = 1;
            return 0;
        }
        return ERR_BAD_FILE;
    }
    static int gIdArt;
};
int FakeSource::gIdArt = 0;

// ---- the frame loop --------------------------------------------------------------------------------------

#define ROWS 8
struct Row
{
    char id[24], name[24];
    int cacheId, uid;
};
static Row rows[ROWS];
static FakeSource source;

// The art worker, run to empty between frames (it is a lower-priority thread that drains in the vsync gap).
static void drainWorker()
{
    load_image_request_t *req;
    while ((req = gArtQ.pop()) != NULL)
        cacheLoadImage(req);
}

// One frame of a carousel that draws `visible` rows starting at `first`. Returns how many drew the placeholder.
static int frame(image_cache_t *cache, int first, int visible)
{
    int placeholders = 0, r;
    guiFrameId++;
    guiInactiveFrames++;
    for (r = 0; r < visible; r++) {
        Row *row = &rows[(first + r) % ROWS];
        GSTEXTURE *tex = cacheGetTextureIdOrName(cache, &source, &row->cacheId, &row->uid, row->id, row->name, r == visible / 2);
        if (tex == NULL || tex->Mem == NULL)
            placeholders++;
    }
    drainWorker();
    return placeholders;
}

static void resetRows()
{
    for (int r = 0; r < ROWS; r++) {
        snprintf(rows[r].id, sizeof(rows[r].id), "SLUS_%03d.00", r);
        snprintf(rows[r].name, sizeof(rows[r].name), "NAME%d", r);
        rows[r].cacheId = -1;
        rows[r].uid = 0;
    }
}

int main()
{
    gArtW.running = 1;

    // A carousel showing exactly as many rows as the cache has slots: the coverflow warming budget.
    {
        resetRows();
        image_cache_t *cache = cacheInitCache(0, "ART", 1, "COV", 5);
        int f, flicker = 0;
        for (f = 0; f < 10; f++)
            frame(cache, 0, 5); // settle: id probe, id absent, name load
        int readsSettled = gDeviceReads;
        for (f = 0; f < 200; f++)
            flicker += frame(cache, 0, 5);
        CHECK(flicker == 0);                 // every visible cover keeps its art, every frame
        CHECK(gDeviceReads == readsSettled); // and nothing is read again once settled
        cacheDestroyCache(cache);
    }

    // A list rebuild (new fail generation) re-opens absences, not resident covers: the settled name art stays on
    // screen and nothing is read again.
    {
        resetRows();
        image_cache_t *cache = cacheInitCache(0, "ART", 1, "COV", 5);
        int f;
        for (f = 0; f < 10; f++)
            frame(cache, 0, 5);
        cacheInvalidateFailMemo();
        int before = gDeviceReads, flicker = 0;
        for (f = 0; f < 50; f++)
            flicker += frame(cache, 0, 5);
        CHECK(flicker == 0);
        CHECK(gDeviceReads == before);
        cacheDestroyCache(cache);
    }

    // A list rebuild WITHOUT a new fail generation (a background rescan, an L3 ISO/VCD toggle): every row is reborn
    // with cache_id -1 and no slot. It takes back the name fallback its old self settled on instead of re-reading its
    // id -- whose absence memo the name claim already evicted -- and claiming a slot for it, which on a full cache
    // evicted a visible cover. Before the adoption this cost 10 reads and 14 placeholders per rebuild.
    {
        resetRows();
        image_cache_t *cache = cacheInitCache(0, "ART", 1, "COV", 5);
        int f;
        for (f = 0; f < 10; f++)
            frame(cache, 0, 5);
        for (int r = 0; r < ROWS; r++) {
            rows[r].cacheId = -1;
            rows[r].uid = -1; // menusys seeds both arrays with -1
        }
        int before = gDeviceReads, flicker = 0;
        for (f = 0; f < 50; f++)
            flicker += frame(cache, 0, 5);
        CHECK(flicker == 0);
        CHECK(gDeviceReads == before);
        cacheDestroyCache(cache);
    }

    // The same rebuild for rows with neither art: they re-find their parked name absence and stay parked.
    {
        resetRows();
        for (int r = 0; r < ROWS; r++)
            snprintf(rows[r].name, sizeof(rows[r].name), "NONE%d", r);
        image_cache_t *cache = cacheInitCache(0, "ART", 1, "COV", 8);
        int f;
        for (f = 0; f < 10; f++)
            frame(cache, 0, 5);
        for (int r = 0; r < ROWS; r++) {
            rows[r].cacheId = -1;
            rows[r].uid = -1;
        }
        int before = gDeviceReads;
        for (f = 0; f < 50; f++)
            frame(cache, 0, 5);
        CHECK(gDeviceReads == before);
        CHECK(rows[0].cacheId == CACHE_ID_PS1_BOTH_ABSENT);
        cacheDestroyCache(cache);
    }

    // Scrolling a 5-wide window one row at a time through 8 rows: each row that enters is read at most twice (its
    // id probe and its name), and a window that stops moving settles at once.
    {
        resetRows();
        image_cache_t *cache = cacheInitCache(0, "ART", 1, "COV", 5);
        int f, step, before = gDeviceReads;
        for (step = 0; step < 16; step++)
            for (f = 0; f < 4; f++)
                frame(cache, step, 5);
        CHECK(gDeviceReads - before <= 2 * (5 + 16));
        for (f = 0; f < 10; f++)
            frame(cache, 16, 5);
        int settled = gDeviceReads, flicker = 0;
        for (f = 0; f < 100; f++)
            flicker += frame(cache, 16, 5);
        CHECK(flicker == 0);
        CHECK(gDeviceReads == settled);
        cacheDestroyCache(cache);
    }

    // Id art added later is picked up once the row's name slot has been evicted and the row asks again.
    {
        resetRows();
        image_cache_t *cache = cacheInitCache(0, "ART", 1, "COV", 5);
        int f;
        for (f = 0; f < 10; f++)
            frame(cache, 0, 5);
        FakeSource::gIdArt = 1;
        cacheInvalidateFailMemo();
        for (f = 0; f < 10; f++)
            frame(cache, 3, 5); // rows 3..7: rows 5-7 claim slots, evicting rows 0-2
        for (f = 0; f < 10; f++)
            frame(cache, 0, 5);
        CHECK(!cache->slotKeyedOther(rows[0].cacheId, rows[0].id));
        FakeSource::gIdArt = 0;
        cacheDestroyCache(cache);
    }

    // Disc-id art that DOES exist wins, and also settles.
    {
        resetRows();
        FakeSource::gIdArt = 1;
        image_cache_t *cache = cacheInitCache(0, "ART", 1, "COV", 5);
        int f, flicker = 0;
        for (f = 0; f < 10; f++)
            frame(cache, 0, 5);
        int readsSettled = gDeviceReads;
        for (f = 0; f < 100; f++)
            flicker += frame(cache, 0, 5);
        CHECK(flicker == 0);
        CHECK(gDeviceReads == readsSettled);
        CHECK(!cache->slotKeyedOther(rows[0].cacheId, rows[0].id)); // the id art holds the slot, not the name art
        FakeSource::gIdArt = 0;
        cacheDestroyCache(cache);
    }

    // Neither exists: the row parks both absences in itself and stops asking.
    {
        resetRows();
        for (int r = 0; r < ROWS; r++)
            snprintf(rows[r].name, sizeof(rows[r].name), "NONE%d", r);
        image_cache_t *cache = cacheInitCache(0, "ART", 1, "COV", 5);
        int f;
        for (f = 0; f < 10; f++)
            frame(cache, 0, 5);
        int readsSettled = gDeviceReads;
        for (f = 0; f < 100; f++)
            frame(cache, 0, 5);
        CHECK(gDeviceReads == readsSettled);
        CHECK(rows[0].cacheId == CACHE_ID_PS1_BOTH_ABSENT);
        cacheDestroyCache(cache);
    }

    TEST_DONE("ps1art");
}
