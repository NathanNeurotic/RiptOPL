#ifndef __UDPFS_SUPPORT_H
#define __UDPFS_SUPPORT_H

#include <time.h>
#include "include/iosupport.h"
#include "include/supportbase.h" // base_game_info_t

#include "include/mcemu.h"
// A VMC for mmce_mcemu on the OPL core (modules/mcemu/mcemu.h McImageSpec with MMCE_DRIVER): fd is
// the server's handle for the .bin, opened read-write by the menu and kept open across the IOP reset.
typedef struct
{
    int active;       /* Activation flag */
    int fd;           /* Server handle of the VMC file */
    int flags;        /* Card flag */
    vmc_spec_t specs; /* Card specifications */
} udpfs_vmc_infos_t;

// GUI thread: show a one-shot notice queued by the background UDPFS scan.
void udpfsShowPendingNotice(void);

// Reuse ETH's refresh cadence: this is a network filesystem, so the same slow-poll delay applies.
#define UDPFS_MODE_UPDATE_DELAY 300

class UdpfsSource; // the GameSource backend, defined in src/sources/udpfssupport.cpp

// The UDPFS game-list cache (src/sources/udpfssupport.cpp): the PS2 CD/DVD list and the PS1/Ember list,
// the mtime/UL-size scan latches, the bounds-checked empty-row sentinel and the folder-creation latch.
// One instance; the UdpfsSource override bodies drive the scans directly (friend) and resolve rows
// through activeGame()/activeGameCount(). The theme/server-wait cluster (udpfsPrefix, udpfsBase,
// udpfsIomanModLoaded, udpfsWaitingForServer, udpfsThemesScanned, udpfsPs1Scanned and the functions on
// them) deliberately stays file-scope in the .cpp: test_udpfs_themes.py extracts those functions by
// exact signature and compiles them in a stub harness that provides the statics itself.
class UdpfsSupport
{
public:
    // Row count over the cached lists, split per the page's active libview.
    int activeGameCount(const item_list_t *itemList) const;
    // Resolve a row id through one bounds-checked sentinel; out-of-range/stale ids get emptyGame_.
    base_game_info_t *activeGame(item_list_t *itemList, int id);

    // Scan-latch bookkeeping for needsUpdate: init resets everything, a delete/rename only forces
    // the ISO scan latch (zero requests a rescan).
    void resetScanState()
    {
        isoScanned_ = 0;
        modifiedCDPrev_ = 0;
        modifiedDVDPrev_ = 0;
    }
    void invalidateIsoScan() { isoScanned_ = 0; }

    // Free both cached lists and zero their counts (cleanUp/shutdown keep the NIC stack resident).
    void clearGameLists();

private:
    friend class UdpfsSource; // the backend's override bodies scan/rebind the lists directly

    base_game_info_t *games_ = NULL;
    int gameCount_ = 0;
    base_game_info_t *ps1Games_ = NULL;
    int ps1GameCount_ = 0;
    base_game_info_t emptyGame_ = {}; // returned for out-of-range/stale ids; never launched
    int isoScanned_ = 0;              // only a successful server-backed ISO scan is latched
    time_t modifiedCDPrev_ = 0;
    time_t modifiedDVDPrev_ = 0;
    int foldersCreated_ = 0; // sbCreateFolders latched once the server first answers
};

extern UdpfsSupport gUdpfsSupport;

void udpfsIopWasReset(void);     // forget the module latches after an in-menu IOP reset
int udpfsGetModulesLoaded(void); // 1 if the UDPFS ioman NIC stack is loaded (SMB/UDPBD must not load on top).
// Explicit-loader helper: synchronously load the UDPFS filesystem stack and wait briefly for the
// server-backed root to answer. Fails closed if SMB/UDPBD already owns the NIC.
int udpfsEnsureReady(u32 timeoutMs);
item_list_t *udpfsGetObject(int initOnly);

#endif
