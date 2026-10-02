#ifndef __ARTINDEXINT_H
#define __ARTINDEXINT_H

// ArtIndex (src/sources/artindex.cpp): the in-RAM listing of an art directory, so "this cover does
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
    // Drop every cached listing; safe from any thread (marks the slots unusable, frees nothing).
    void invalidate();
    // Debug HUD: directories currently held, probes answered "absent", sweeps that failed.
    void debug(int *dirsHeld, unsigned int *absentAnswered, unsigned int *sweepsFailed);

private:
    // Directories held at once. Three covers the realistic worst case in one view -- a device's
    // ART/ folder, a second device's, and the POPS folder the VCD fallback probes -- without
    // turning this into a cache that needs an eviction policy of its own.
    static constexpr int kSlots = 3;
    // Refuse to hold a directory larger than this. Not a memory limit so much as a sanity limit:
    // a sweep that big is itself slow enough to be the wrong trade, and a caller is better served
    // by the ordinary probe path. 4096 entries is far past any real ART folder (a 400-game
    // library with five art types per game is 2000).
    static constexpr int kMaxEntries = 4096;
    static constexpr int kDirMax = 192;

    struct Dir
    {
        char dir[kDirMax];
        unsigned int *hashes; // sorted, `count` of them
        int count;
        int valid; // 0 = this slot answers "don't know" for everything
    };

    // FNV-1a over the UPPERCASED name (case folding is not cosmetic -- see the .cpp).
    static unsigned int hashName(const char *s);
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
