"""Compile the real cover cache hand-over and check a destroyed cache's loaded pictures move to the next theme load's caches."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/gfx/texcache.cpp").read_text(encoding="utf-8")
header = (root / "include/texcache.h").read_text(encoding="utf-8")


def function(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 0, brace
    while end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
        if depth == 0:
            break
    assert depth == 0
    return source[start:end]


entry = header[header.index("/// A single cache entry..."):header.index("} cache_entry_t;") + len("} cache_entry_t;")]
cache_class = header[header.index("class ImageCache;"):header.index("/** Initializes the cache subsystem.")]
cache_class = cache_class.replace("private:", "public:")
functions = "\n".join(function(s) for s in (
    "void cacheClearItem(cache_entry_t *item, int freeTxt)",
    "image_cache_t *cacheInitCache(",
    "void cacheNewGeneration(void)",
    "static int cacheSameArt(",
    "void cacheRehomeLoaded(image_cache_t *from)",
))

# The destroy path calls the hand-over once no art request references the cache, and before it frees the slots.
destroy = function("void cacheDestroyCache(image_cache_t *cache)")
assert destroy.index("cache->activeRequests_ > 0") < destroy.index("cacheRehomeLoaded(cache);") < destroy.index("cacheClearItem(&cache->content_[i], 1);")
assert "*link = cache->nextLive_;" in destroy
themes = (root / "src/gfx/themes.cpp").read_text(encoding="utf-8")
load = themes[themes.index("newT->gameCacheCount = 0;"):themes.index("host::ui().lock();", themes.index("newT->gameCacheCount = 0;"))]
assert "cacheNewGeneration();" in load, "a theme load must start a cache generation before it makes its caches"

program = r'''
#include <assert.h>
#include <new>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define GS_CLUT_STORAGE_CSM1 0
typedef struct { void *Mem, *Clut; int Vram, VramClut, ClutStorageMode; } GSTEXTURE;
typedef struct {} item_list_t;
static int unloaded, released;
static void rmUnloadTexture(GSTEXTURE *t) { unloaded++; t->Vram = 0; }
static void rmReleaseTexture(GSTEXTURE *t) { released++; free(t->Mem); t->Mem = NULL; }
@ENTRY@
@CLASS@
@FUNCTIONS@
static void put(image_cache_t *c, int slot, const char *key, int stamp)
{
    cache_entry_t *e = &c->content_[slot];
    e->texture.Mem = malloc(16);
    e->texture.Vram = 1234;
    strcpy(e->key, key);
    e->lastUsed = stamp;
    e->UID = c->nextUID_++;
}
static cache_entry_t *find(image_cache_t *c, const char *key)
{
    for (int i = 0; i < c->count_; i++)
        if (!strcmp(c->content_[i].key, key))
            return &c->content_[i];
    return NULL;
}
static int loaded(image_cache_t *c)
{
    int n = 0;
    for (int i = 0; i < c->count_; i++)
        n += c->content_[i].texture.Mem != NULL;
    return n;
}
int main(void)
{
    // The old layout: a cover cache with four covers, one still loading and one known absent, and a disc cache.
    cacheNewGeneration();
    image_cache_t *oldCov = cacheInitCache(0, "ART", 1, "COV", 6);
    image_cache_t *oldIco = cacheInitCache(1, "ART", 1, "ICO", 4);
    image_cache_t *attr = cacheInitCache(-1, "mass0:/THM/x", 0, "COV", 4); // a theme's own images: never handed over
    put(oldCov, 0, "SLUS_000.01", 10);
    put(oldCov, 1, "SLUS_000.02", 40);
    put(oldCov, 2, "SLUS_000.03", 30);
    put(oldCov, 3, "SLUS_000.04", 20);
    strcpy(oldCov->content_[4].key, "SLUS_000.05"); // loading: the art worker owns it
    oldCov->content_[4].qr = &oldCov;
    oldCov->content_[4].lastUsed = 50;
    strcpy(oldCov->content_[5].key, "SLUS_000.06"); // known absent
    oldCov->content_[5].lastUsed = 0;
    put(oldIco, 0, "SLUS_000.01", 10);
    put(attr, 0, "SLUS_000.01", 10);
    void *pixels2 = oldCov->content_[1].texture.Mem;

    // Destroying a cache of the load on screen (shutdown) hands nothing over.
    cacheRehomeLoaded(oldCov);
    assert(loaded(oldCov) == 4 && unloaded == 0);

    // The new layout: a smaller cover cache (three slots), a second cover cache, another device's, and no disc cache.
    cacheNewGeneration();
    image_cache_t *newOther = cacheInitCache(0, "mass0:/ART", 0, "COV", 6);
    image_cache_t *newCov2 = cacheInitCache(1, "ART", 1, "COV", 1);
    image_cache_t *newCov = cacheInitCache(2, "ART", 1, "COV", 3);
    put(newCov2, 0, "SLUS_000.02", 60); // already holds the newest cover

    cacheRehomeLoaded(oldCov);
    // Newest first: .02 is in newCov2 already so it goes to newCov, then .03 and .04; .01 finds no room and stays.
    assert(loaded(newCov) == 3 && find(newCov, "SLUS_000.02") && find(newCov, "SLUS_000.03") && find(newCov, "SLUS_000.04"));
    assert(find(newCov, "SLUS_000.02")->texture.Mem == pixels2 && find(newCov, "SLUS_000.02")->texture.Vram == 0);
    assert(find(newCov, "SLUS_000.02")->lastUsed == 40 && find(newCov, "SLUS_000.02")->UID > 0);
    assert(loaded(newCov2) == 1 && loaded(newOther) == 0 && unloaded == 3);
    assert(loaded(oldCov) == 1 && find(oldCov, "SLUS_000.01") && !find(oldCov, "SLUS_000.02"));
    assert(oldCov->content_[4].qr != NULL && !strcmp(oldCov->content_[4].key, "SLUS_000.05")); // loading slot untouched
    assert(oldCov->content_[1].lastUsed == -1 && oldCov->content_[1].texture.Mem == NULL);     // moved slots cleared
    // No two slots of the new cache share pixels, and every UID is its own.
    for (int i = 0; i < 3; i++)
        for (int j = i + 1; j < 3; j++)
            assert(newCov->content_[i].texture.Mem != newCov->content_[j].texture.Mem && newCov->content_[i].UID != newCov->content_[j].UID);

    // No cache of the same art in the new load, and a cache that is not game art: nothing moves.
    cacheRehomeLoaded(oldIco);
    cacheRehomeLoaded(attr);
    assert(loaded(oldIco) == 1 && loaded(attr) == 1 && unloaded == 3 && released == 0);
    puts("PASS: a destroyed cover cache hands its loaded pictures to the next theme load");
    return 0;
}
'''.replace("@FUNCTIONS@", functions).replace("@CLASS@", cache_class).replace("@ENTRY@", entry)
with tempfile.TemporaryDirectory() as d:
    src = Path(d) / "test.cpp"
    out = Path(d) / "test"
    src.write_text(program, encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-Wno-write-strings", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", str(src), "-o", str(out)], check=True)
    subprocess.run([str(out)], check=True, timeout=10, env={"ASAN_OPTIONS": "detect_leaks=0"})
