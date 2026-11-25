#ifndef __ARTINDEXINT_H
#define __ARTINDEXINT_H

// ArtIndex (src/gfx/artindex.cpp): the in-RAM listing of an art directory, so "this cover does
// not exist" can be answered without asking the device. See artindex.h for the design rule (an
// uncertain answer is always "go and look") and why the whole module exists. One instance,
// gArtIndex; the artIndex* functions in artindex.h are thin forwarders to it.

class ArtIndex
{
public:
    // Register the ONE thread allowed to touch the index (the art worker). Every other caller of
    // mayExist gets 1 = "go and look", which is the pre-index behaviour.
    void setOwnerThread(int threadId) { ownerThread_ = threadId; }
    // 1 = this file may exist, probe it normally; 0 = definitely NOT there, skip the open.
    int mayExist(const char *fullPath);
    // A picture by its path WITHOUT extension, in any letter case, as .png, .jpg or .jpeg (a .png first).
    // 1 = found, its real path (as the device lists it) in out; 0 = definitely none; -1 = don't know.
    int findPicture(const char *stemPath, char *out, int outSize);
    // Drop every cached listing; safe from any thread (marks the slots unusable, frees nothing).
    void invalidate();
    // Debug HUD: directories currently held, probes answered "absent", sweeps that failed.
    void debug(int *dirsHeld, unsigned int *absentAnswered, unsigned int *sweepsFailed);

private:
    // Directories held at once, replaced round-robin. Three was too few: Favourites, All Games and
    // APPS show rows from every device, and each device brings its ART/ folder plus the POPS folder
    // the VCD fallback probes. With four devices that is eight folders, so three slots evicted on
    // nearly every cover and re-listed a whole ART folder each time -- on the one I/O channel the
    // music and the settings save also use. Slots are trimmed to their folder's size (build()).
    static constexpr int kSlots = 8;
    // Refuse to hold a directory larger than this. Not a memory limit so much as a sanity limit:
    // a sweep that big is itself slow enough to be the wrong trade, and a caller is better served
    // by the ordinary probe path. 4096 entries is far past any real ART folder (a 400-game
    // library with five art types per game is 2000).
    static constexpr int kMaxEntries = 4096;
    static constexpr int kDirMax = 192;
    // Bytes of picture names kept per directory (RIPPS2 build 115). Past it the slot keeps no names and
    // findPicture answers "don't know"; the hash index is unaffected.
    static constexpr int kNamesMax = 128 * 1024;

    struct Pic
    {
        unsigned int hash; // hashName of the name WITHOUT its extension
        int name;          // offset of the real name in Dir::names
    };

    struct Dir
    {
        char dir[kDirMax];
        unsigned int *hashes; // sorted, `count` of them
        int count;
        Pic *pics; // the .png/.jpg/.jpeg entries, sorted by hash, `picCount` of them; NULL = names not kept
        int picCount;
        char *names; // the pictures' names as the device lists them (PFS minds letter case)
        int valid;   // 0 = this slot answers "don't know" for everything
    };

    // FNV-1a over the UPPERCASED name (case folding is not cosmetic -- see the .cpp).
    static unsigned int hashName(const char *s, size_t n = (size_t)-1);
    static int pictureExt(const char *name);
    static int picCmp(const void *a, const void *b);
    static void release(Dir *slot);
    static int hashCmp(const void *a, const void *b);
    // Split a full path into its directory and its basename.
    static int splitPath(const char *fullPath, char *dirOut, int dirMax, const char **baseOut);
    // Sweep one directory into a fresh slot; the slot may come back invalid (still worth keeping,
    // so a directory that cannot be indexed is not re-swept on every probe).
    Dir *build(const char *dir);
    Dir *slotFor(const char *dir);

    Dir slots_[kSlots] = {};
    int next_ = 0; // round-robin replacement; see slotFor()
    unsigned int absentAnswered_ = 0;
    // Sweeps that could not list their directory. THE FIELD THAT WOULD HAVE CAUGHT THIS MODULE
    // BEING INERT: on its first hardware run the HUD read IX0/0 -- no directories held, no
    // absences answered -- which is indistinguishable from "no art has been requested yet". A
    // failure count separates "not used" from "tried and could not".
    unsigned int sweepFailed_ = 0;
    // Probes rejected because they did not come from the owner thread. Folded into the HUD next to
    // the failed-sweep count: "inert" and "not yet used" must never read the same.
    unsigned int wrongThread_ = 0;

    // SINGLE-OWNER-THREAD CONFINEMENT, and it is not decoration. texDiscoverLoad has callers on
    // TWO threads (the art worker and the theme loader); without this, two threads could take the
    // same round-robin slot and both free and reassign its buffer -- a double free. Any caller but
    // the owner gets 1 = "go and look", exactly the behaviour that existed before this file.
    int ownerThread_ = -1;

    // Bumped by invalidate(), callable from ANY thread. A reader samples it before and after its
    // search and gives up if it moved -- an invalidation mid-search can only downgrade the answer
    // to "go and look", never leave a stale "absent" behind (the direction that would hide art
    // that exists).
    volatile unsigned int gen_ = 0;
};

extern ArtIndex gArtIndex;

#endif
