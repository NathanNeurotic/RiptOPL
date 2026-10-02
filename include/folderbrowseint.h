#ifndef __FOLDERBROWSEINT_H
#define __FOLDERBROWSEINT_H

// FolderBrowse (src/sources/folderbrowse.cpp): the lazy per-device folder-navigation state for the
// game list (opt-in via gEnableFolderNav; see folderbrowse.h for the user-visible model). One
// instance, gFolderBrowse; the folder* functions in folderbrowse.h are thin forwarders to it.

#include "include/iosupport.h"    // MODE_COUNT
#include "include/folderbrowse.h" // FOLDER_SUB_MAX

class FolderBrowse
{
public:
    // The current subpath BELOW the CD/DVD root ("" at the device root). Never returns NULL;
    // "" when folder-nav is off or the mode is not folder-capable.
    const char *getSub(int mode) const;
    // Nesting depth (0 == at the device root).
    int depth(int mode) const;
    // Push a folder level. Returns 1 on success, 0 if refused (disabled, at depth cap, or the
    // joined path would overflow). Marks the mode dirty so its next NeedsUpdate forces a rescan.
    int descend(int mode, const char *name);
    // Pop a folder level. Returns 1 if it actually ascended, 0 if already at the root.
    int ascend(int mode);
    // Force the mode back to the device root (marks dirty only if it was not already at root).
    void resetToRoot(int mode);
    // One-shot: returns 1 once after a descend/ascend/reset, so NeedsUpdate can force the rescan
    // even when the device's own change-detection would otherwise short-circuit.
    int consumeDirty(int mode);

private:
    char sub_[MODE_COUNT][FOLDER_SUB_MAX] = {}; // "/"-joined subpath below CD/DVD; "" == root
    unsigned char dirtyFlag_[MODE_COUNT] = {};  // 1 = descended/ascended/reset -> force one rescan
    unsigned char levels_[MODE_COUNT] = {};     // pushed segment count (depth)
};

extern FolderBrowse gFolderBrowse;

#endif
