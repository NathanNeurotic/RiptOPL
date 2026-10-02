#ifndef __MMCEINT_H
#define __MMCEINT_H

#include <time.h>
#include "include/iosupport.h"   // item_list_t
#include "include/supportbase.h" // base_game_info_t

// MMCE internals (src/sources/mmcesupport.cpp): the state behind the MMCE source and the GameID
// transport. MmceSupport owns what used to be file-scope statics -- the games/art prefixes, the ISO
// and PS1 (VCD) game lists and their scan-healing flags, the Auto-slot resolution cache and the
// mmceman/GameID transport state. The MmceSource page callbacks and the public mmce* entry points
// reach it through gMmceSupport. One instance; members are constant-initialized (no dynamic init
// before main).
class MmceSupport
{
public:
    // Module/transport lifecycle. The public mmceLoadModules/mmceArmGameIDTransport/mmceSendGameID/
    // mmceGameIdSettle/mmceReset entry points (include/mmcesupport.h) are thin forwarders to these,
    // so callers in other translation units are unchanged.
    void loadModules();
    void armGameIDTransport();
    // Push the selected game's disc id to a present card for per-game folder switching. 1 = settled,
    // 0 = not sent, -1 = sent but unconfirmed (see mmcesupport.h for the full contract).
    int sendGameID(const char *startup, const char *protectMcPath, int vmcSlotMask);
    // Bounded post-GameID settle for cross-device launches sharing SIO2 with the MMCE (MX4SIO, S7):
    // 0 = settled, -1 = expired. Never blocks the launch.
    int gameIdSettle(int timeoutMs);
    // Reset present MMCE cards to the default channel and clear GameID. NOT named reset(): that
    // collides with the free opl.cpp reset() the coupling analyzer matches by name.
    int resetCards();

    // Prefix management. prefix carries the user's configurable games folder (gMMCEPrefix), right
    // for OPL's own data (CD/ DVD/ ART/ CFG/ VMC/); getDeviceRoot() derives the card root
    // ("mmceN:/") where the PS1 library folders live (POPS/ and EMBER/). Read the root into a LOCAL
    // buffer: these paths are built on the IO worker, the art thread and the GUI thread, and the
    // device re-detect can blank prefix underneath any of them.
    void getDeviceRoot(char *root, size_t size);
    void refreshArtRoots();
    void setPrefix();
    int detectSlot();
    // Fs-settle after a GameID card switch (the 0x8 devctl re-mounts the card; on Gen2 the busy bit
    // can clear before the filesystem surface is back).
    void settleAfterSwitch();

    // The active row across the two views (#120): picks the ISO vs PS1 array and returns a safe
    // empty entry for any stale/invalid id.
    base_game_info_t *activeGame(item_list_t *itemList, int id);

    // State the MmceSource page callbacks operate on directly (public like ArtWorker's counters:
    // the scan callbacks run on the IO worker at ~2s cadence and the launch path on the GUI thread,
    // so keep the straightforward member access the file statics had).
    char prefix[40];
    char artPrimary[40];
    int ulSizePrev = -2; // -2 = first ISO scan not yet successful (contended-bus retry sentinel)
    // VCD-scan self-heal (HW batch S6): a failed vcdFillGameList on a CONTENDED bus (-1) used to
    // latch an empty VCD page under NOUPDATE with no rescan for the session. Bounded retry (~15
    // passes at the ~2s cadence), re-armed by a fresh L3 toggle, self-quiescing on success/expiry.
    unsigned char ps1Scanned = 0;
    unsigned char ps1ScanFailed = 0;
    unsigned char ps1ScanRetries = 0;
    time_t modifiedCDPrev = 0;
    time_t modifiedDVDPrev = 0;
    int gameCount = 0;
    base_game_info_t *games = NULL;
    // #120: the PS2 (ISO) and PS1 (VCD) views must NOT share one backing store -- a failed rescan of
    // one view preserves only THAT view's last-good and can never resurrect the other view's list.
    // Separate arrays, mirroring hddGames vs hddVcdGames.
    int ps1GameCount = 0;
    base_game_info_t *ps1Games = NULL;
    // Auto-slot (gMMCESlot==2) resolution cache: detectSlot()'s last result (2=mmce0, 3=mmce1,
    // -1=unresolved). Avoids re-probing BOTH slots over SIO2 every menu refresh; reset by init on a
    // fresh enable/settings-apply.
    int resolvedDevice = -1;
    // sbCreateFolders() issues ~10 mkdir devctls; remember the prefix we last created folders for so
    // a refresh on an unchanged card skips the redundant burst. Reset by init and on card removal
    // (empty prefix) so a freshly inserted card still gets its folders.
    char foldersCreatedFor[40] = {0};
    unsigned char folderRetries = 0;
    // 1 once an Auto probe has ever resolved a slot: the full re-detect retries the debounced probe
    // budget only for a card we have already seen -- a console with genuinely no MMCE must not pay
    // ~1.6s of devctl timeouts on every 2s refresh.
    unsigned char everResolved = 0;
    int modLoaded = 0;          // latched by loadModules; read by sendGameID's arm check
    char gameIdTarget[8] = {0}; // last device a GameID 0x8 switch was SENT to (gameIdSettle polls it)
    base_game_info_t emptyGame = {}; // sentinel returned for stale/invalid ids (never launched)
};

extern MmceSupport gMmceSupport;

#endif
