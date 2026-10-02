/* RA: the watch list for the game being launched. See src/sources/rawatch.cpp */
#ifndef __RAWATCH_H__
#define __RAWATCH_H__

#include "modules/network/common/ra_watch.h"

// The watch list for the game being launched (src/sources/rawatch.cpp). Owns the
// entry array, the pointer-chain array and the record of which game's list is in
// memory. One instance; the free functions below are thin forwarders so callers
// (ranet, supportbase, the launch path) do not change.
class RaWatch
{
public:
    /* Reads one specific watch-list file. Callers go through sbLoadWatchList(),
       which owns where a list may live. Returns the entry count, or negative on
       failure -- a missing file is not an error, the game simply has no set. */
    int loadWatchList(const char *file, const char *startup);

    /* Takes the list from memory instead of a file: the network brought it and the
       file may not have reached the medium yet. Records which game it belongs to. */
    int setWatchList(const void *data, int len, const char *startup);

    unsigned int *watchList() { return watchCount_ > 0 ? watchList_ : NULL; }
    int watchCount() const { return watchCount_; }
    int watchBytes() const { return watchBytes_; }
    /* The pointer chains that came with the list, already checked: every
       parent points backwards, so ee_core can resolve them in one pass. */
    struct ra_node *nodeList() { return nodeCount_ > 0 ? nodeList_ : NULL; }
    int nodeCount() const { return nodeCount_; }
    void clearWatchList();

private:
    /* Takes the chain list if it is sound, drops all of it otherwise. */
    void takeNodes(const struct ra_node *nodes, unsigned int count);

    unsigned int watchList_[RA_WATCH_MAX] = {};
    int watchCount_ = 0;
    int watchBytes_ = 0;
    /* Pointer chains, optional: an older client sends a list without them
       and everything works as it did, minus the chains. */
    struct ra_node nodeList_[RA_NODE_MAX] = {};
    int nodeCount_ = 0;
    /* Which game's list is in memory. The list can arrive over the network
       (ranet.cpp) while still in the menu; at launch there is then no reason to read
       the file, which on USB may not even have left the driver's cache yet. */
    char watchStartup_[16] = {};
};

extern RaWatch gRaWatch;

/* One line in the launch log: what happened plus two numbers. Compiled away
   unless this is an __OPLDIAG build. */
void raLaunchNote(const char *what, int a, int b);

/* The free API other translation units call; all forward to gRaWatch. */
int LoadWatchListFile(const char *file, const char *startup);
int SetWatchList(const void *data, int len, const char *startup);
unsigned int *GetWatchList(void);
int GetWatchCount(void);
int GetWatchBytes(void);
struct ra_node *GetNodeList(void);
int GetNodeCount(void);
void ClearWatchList(void);

#endif
