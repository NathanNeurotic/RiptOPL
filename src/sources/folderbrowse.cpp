#include <stdio.h>
#include <string.h>

#include "include/opl.h" // umbrella: SDK base types (u32/GSTEXTURE) that iosupport.h depends on
#include "include/iosupport.h"
#include "include/folderbrowse.h"
#include "include/folderbrowseint.h"

// Set by opl.c from the "folder_nav" config key. Folder browsing is entirely inert unless this is
// on: scanForISO never emits a folder row and every helper below returns the root ("") state.
extern int gEnableFolderNav;

FolderBrowse gFolderBrowse;

int folderModeSupported(int mode)
{
    // Loose-file tree scanners only (sbReadList/base_game_info_t, "/" separator): BDM (all 8 slots),
    // MMCE and UDPFS-Files. APA-HDD (HDL), the UDPFS block device and the VCD/POPS view do not scan a
    // CD/DVD directory tree, so they are excluded. SMB/ETH is deferred (uses "\\").
    return (mode >= BDM_MODE && mode <= BDM_MODE_LAST) || mode == MMCE_MODE || mode == UDPFS_MODE;
}

const char *folderGetSub(int mode)
{
    return gFolderBrowse.getSub(mode);
}

int folderDepth(int mode)
{
    return gFolderBrowse.depth(mode);
}

int folderDescend(int mode, const char *name)
{
    return gFolderBrowse.descend(mode, name);
}

int folderAscend(int mode)
{
    return gFolderBrowse.ascend(mode);
}

void folderReset(int mode)
{
    gFolderBrowse.resetToRoot(mode);
}

int folderConsumeDirty(int mode)
{
    return gFolderBrowse.consumeDirty(mode);
}

const char *FolderBrowse::getSub(int mode) const
{
    if (!gEnableFolderNav || mode < 0 || mode >= MODE_COUNT || !folderModeSupported(mode))
        return "";
    return sub_[mode];
}

int FolderBrowse::depth(int mode) const
{
    if (!gEnableFolderNav || mode < 0 || mode >= MODE_COUNT || !folderModeSupported(mode))
        return 0;
    return levels_[mode];
}

int FolderBrowse::descend(int mode, const char *name)
{
    if (!gEnableFolderNav || mode < 0 || mode >= MODE_COUNT || !folderModeSupported(mode))
        return 0;
    if (name == NULL || name[0] == '\0' || levels_[mode] >= FOLDER_DEPTH_MAX)
        return 0;

    char joined[FOLDER_SUB_MAX];
    int len;
    if (sub_[mode][0] == '\0')
        len = snprintf(joined, sizeof(joined), "%s", name);
    else
        len = snprintf(joined, sizeof(joined), "%s/%s", sub_[mode], name);

    // Refuse a join that would not fit: a truncated subpath would scan the WRONG directory, so the
    // user simply stays where they are (a folder with a name this long is not navigable). snprintf
    // returns the length it WOULD have written, so len >= buffer size means it did not fit.
    if (len < 0 || (size_t)len >= sizeof(sub_[mode]))
        return 0;

    strcpy(sub_[mode], joined);
    levels_[mode]++;
    dirtyFlag_[mode] = 1;
    return 1;
}

int FolderBrowse::ascend(int mode)
{
    if (!gEnableFolderNav || mode < 0 || mode >= MODE_COUNT || !folderModeSupported(mode))
        return 0;
    if (levels_[mode] == 0)
        return 0;

    char *slash = strrchr(sub_[mode], '/');
    if (slash != NULL)
        *slash = '\0'; // drop the last "/segment"
    else
        sub_[mode][0] = '\0'; // back to root

    levels_[mode]--;
    dirtyFlag_[mode] = 1;
    return 1;
}

void FolderBrowse::resetToRoot(int mode)
{
    if (mode < 0 || mode >= MODE_COUNT)
        return;
    // Reset the raw state even for modes that are not folder-capable / when the toggle is off, so a
    // stale subpath can never leak across a device switch. Mark dirty only when something changed.
    if (sub_[mode][0] != '\0' || levels_[mode] != 0) {
        sub_[mode][0] = '\0';
        levels_[mode] = 0;
        dirtyFlag_[mode] = 1;
    }
}

int FolderBrowse::consumeDirty(int mode)
{
    if (mode < 0 || mode >= MODE_COUNT || !dirtyFlag_[mode])
        return 0;
    dirtyFlag_[mode] = 0;
    return 1;
}
