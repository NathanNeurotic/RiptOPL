#ifndef __MMCEINT_H
#define __MMCEINT_H

#include <time.h>
#include "include/iosupport.h"   // item_list_t, MMCE_MODE_COUNT
#include "include/supportbase.h" // base_game_info_t

// Per-slot MMCE page state (0 = mmce0:/ / MMCE_MODE, 1 = mmce1:/ / MMCE_MODE1).
// Constant-initialized (EE rule: no dynamic initialization before main).
struct MmceSlotState
{
    int slotIndex = 0; // 0 = mmce0:, 1 = mmce1:
    char prefix[40] = {0};
    char artPrimary[40] = {0};
    int ulSizePrev = -2; // -2 = first ISO scan not yet successful (contended-bus retry sentinel)
    // VCD-scan self-heal (HW batch S6): bounded retry (~15 passes at the ~2s cadence), re-armed by
    // a fresh L3 toggle, self-quiescing on success/expiry.
    unsigned char ps1Scanned = 0;
    unsigned char ps1ScanFailed = 0;
    unsigned char ps1ScanRetries = 0;
    time_t modifiedCDPrev = 0;
    time_t modifiedDVDPrev = 0;
    int gameCount = 0;
    base_game_info_t *games = NULL;
    // #120: separate ISO and PS1 backing arrays so a failed rescan of one view preserves only that view.
    int ps1GameCount = 0;
    base_game_info_t *ps1Games = NULL;
    // sbCreateFolders() issues ~10 mkdir devctls; remember the prefix we last created folders for so
    // a refresh on an unchanged card skips the redundant burst. Reset by init and on card removal.
    char foldersCreatedFor[40] = {0};
    unsigned char folderRetries = 0;
    // 1 once this slot has ever seen a card: the debounced presence probe only pays its retry
    // budget for a card we have already seen -- an empty slot must not pay ~1.0s of timeouts per refresh.
    unsigned char everResolved = 0;
    unsigned char present = 0;
    // Per-slot THM/LNG registration latches (reset on card removal so a swapped card's assets register).
    unsigned char themesLoaded = 0;
    unsigned char languagesLoaded = 0;
    base_game_info_t emptyGame = {}; // sentinel returned for stale/invalid ids (never launched)

    void getDeviceRoot(char *root, size_t size) const;
    void refreshArtRoots();
    void probePresence();
    void resetState(int slot);
    base_game_info_t *activeGame(item_list_t *itemList, int id);
};

// MMCE internals (src/sources/mmcesupport.cpp): owns both per-slot states and the shared mmceman /
// GameID transport. One instance; members are constant-initialized (no dynamic init before main).
class MmceSupport
{
public:
    // Module/transport lifecycle. The public mmceLoadModules/mmceArmGameIDTransport/mmceSendGameID/
    // mmceGameIdSettle/mmceReset entry points (include/mmcesupport.h) are thin forwarders to these.
    void loadModules();
    void armGameIDTransport();
    // Push the selected game's disc id to eligible present card(s) for per-game folder switching.
    // 1 = settled, 0 = not sent, -1 = sent to at least one slot but unconfirmed.
    int sendGameID(const char *startup, const char *protectMcPath, int vmcSlotMask);
    // Bounded post-GameID settle for cross-device launches sharing SIO2 with the MMCE (MX4SIO, S7):
    // 0 = settled, -1 = expired. Never blocks the launch.
    int gameIdSettle(int timeoutMs);
    // MMCE-hosted launches only: wait, bounded and on screen, until every slot the last send
    // targeted answers and is not swapping. Nothing to do when no send was made this launch.
    void waitSwapDone(const char *why);
    // The same wait on a given slot; 1 when a swap was waited out. *start carries a START press
    // across the looks of one wait (waitBeforeHandoff's slots); NULL for a single look.
    int waitSwapDoneOn(const char *device, const char *why, int *start);
    // Last look before the IOP handoff, always ending on the game's own slot; 1 when a swap was waited out.
    int waitBeforeHandoff(const char *gameDevice);
    // After the OSD history write: wait out the card's own folder switch on the game's slot (bounded).
    int waitHistorySwitch(const char *device, const char *why);
    // Reset present MMCE cards to the default channel and clear GameID.
    int resetCards();

    // Fs-settle after a GameID card switch (the 0x8 devctl re-mounts the card; on Gen2 the busy bit
    // can clear before the filesystem surface is back).
    void settleDeviceFs(const char *mmceRoot);
    void settleAfterSwitch(const char *gameDevice);

    // *start: see waitSwapDoneOn; NULL for a single look.
    int waitCardReady(const char *device, int *start);

    MmceSlotState slots[MMCE_MODE_COUNT] = {};
    int modLoaded = 0;            // latched by loadModules; read by sendGameID's arm check
    int gameIdTargetMask = 0;     // bit N = GameID 0x8 was sent to mmceN:/ on this launch
    int gameIdSwitchSeenMask = 0; // bit N = mmceN:/ was seen switching for our GameID send
};

extern MmceSupport gMmceSupport;

#endif
