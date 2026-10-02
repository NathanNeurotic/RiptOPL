#ifndef __HDDINT_H
#define __HDDINT_H

#include "include/hddsupport.h" // hdl_games_list_t / hdl_game_info_t / APA_IDMAX / base_game_info_t (via iosupport.h)

// The HDD support runtime: one instance (gHddSupport, defined in src/sources/hddsupport.cpp) owns the
// ATA/PFS module lifecycle and its deferred-failure diagnostics, the staged normal-APA OPL-home
// selection and the two game lists (APA-table HDL games + the PS1/VCD rows). The HddSource GameSource
// and the hdd* free entry points of hddsupport.cpp forward here. Internal to that file: include/
// hddsupport.h stays C-compatible (zso.c includes it) and keeps declaring the public hdd* API.
//
// Data members are deliberately public (same rule as ArtWorker): single-word latches read and written
// from the HddSource virtuals and the test-pinned free helpers (hddMountSupportHome,
// hddReleaseSupportHome) in that file; the malloc'd game lists are published under the host::ui() lock
// by publishVcdGameList/publishHdlGameList. Behaviour spanning fields lives in methods.

constexpr int HDD_PFS_DIAG_NOT_RUN = (-9999);

// Latched reason of the deferred PFS failure report (Code 222/402), set by armPfsDiagFailure.
typedef enum {
    HDD_PFS_DIAG_REASON_NONE = 0,
    HDD_PFS_DIAG_REASON_HDD_CHECK_STATUS_1,
    HDD_PFS_DIAG_REASON_PS2FS_LOAD_FAILURE,
} hdd_pfs_diag_reason_t;

class HddSupport
{
public:
    constexpr HddSupport() = default;
    HddSupport(const HddSupport &) = delete;
    HddSupport &operator=(const HddSupport &) = delete;

    // ioPutRequest(IO_CUSTOM_SIMPLEACTION) entry (the kernel wants a plain function pointer):
    // brings the ATA + PFS support stack up and creates the data-home folders.
    static void initModulesEntry();

    // ---- ATA module stack + APA/PFS support stack ----------------------------------------
    int loadModules();            // was hddLoadModules
    int diagLoadModulesReady();   // was hddDiagLoadModulesReady: the visible DEV9/ATAD/XHDD boot bracket
    int loadCoreSupportModules(); // was hddLoadCoreSupportModules: ps2hdd + PS2FS for a read-only pfs1: probe
    void clearRecoveredErrors();  // was hddClearRecoveredErrors

    // The deferred-retry block of the list update (was the middle of HddSource::update): retry the
    // support stack when the base ATA stack is resident, then toast Code 222/402 once a failure
    // persists across enough settled attempts.
    void settleSupportRetry();

    // ---- OPL data-home discovery + the staged normal-APA OPL-home selection ---------------
    void findOplPartition();        // was hddFindOPLPartition: existing-partitions-only discovery
    int getLiveOplHomeSelection();  // was hddGetLiveOplHomeSelection
    int getOplHomeSelection();      // was hddGetOplHomeSelection (pending -> committed -> live)
    int resolveLiveOplDataPath(const char *relativePath, char *out, int outSize); // was hddResolveLiveOplDataPath
    int oplHomeSelectionNeedsTargetSave();                                        // was hddOplHomeSelectionNeedsTargetSave
    int mountSelectedOplHome(char *prefix, int prefixLen);                        // was hddMountSelectedOplHome
    int commitOplHomeSelection();                                                 // was hddCommitOplHomeSelection

    // ---- game lists ------------------------------------------------------------------------
    // Drop the once-per-session HDD VCD list cache (was hddVcdInvalidateCache): the next VCD-view
    // update re-walks the partitions. Needed only when a SCAN-TIME filter changes
    // (gVcdFirstDiscOnly); view flips reuse the built list.
    void invalidateVcdCache();
    // Build the HDD VCD game list from both supported APA/PFS shapes (was hddBuildVcdGameList):
    // pooled __.POPS[0-9]? containers plus one-game PP.<id>.POPS.<name> installs, then the EMBER
    // phase; publishes transactionally and returns the row count.
    int buildVcdGameList();
    // Swap in a new live HDL list under the UI lock, freeing the replaced one (was hddPublishHdlGameList).
    void publishHdlGameList(hdl_games_list_t *replacement);
    // Free both VCD backing arrays (was hddFreeVcdGameList); also the cleanUp/shutdown teardown path.
    void freeVcdGameList();
    // Split-store resolvers (Codex/Fable audit, was hddActiveVcd/hddActiveHdl): an out-of-range id
    // returns the static empty sentinel so a stale read is safe empty data; launch/delete/rename
    // early-return on `== &emptyVcd` / `== &emptyHdl`.
    base_game_info_t *activeVcd(int id);
    hdl_game_info_t *activeHdl(int id);
    // Index of the VCD whose basename matches vcdName in the current vcdGames list, or -1
    // (was hddFindVcdByName).
    int findVcdByName(const char *vcdName);
    // Rename a PS1 row within its partition (was hddRenameVcd); PP.* one-game labels are APA
    // metadata and only get the "unsupported" message.
    void renameVcd(int id, const char *newName);

    // ---- launch teardown pieces owned by the support stack (HddSource::cleanUp/shutdown) ---
    // The hddSupportModulesLoaded block of cleanUp (was part of HddSource::cleanUp): PDIOC_CLOSEALL
    // unless a keep exception covers it, flush the PFS write cache, drop the support latch and the
    // pfs0: data-home marker.
    void cleanUpSupportModules(int exception);
    // The hddSupportModulesLoaded block of shutdown: unconditional CLOSEALL, terminal-only flush.
    void shutdownSupportModules();
    // The hddModulesLoadCount block of shutdown: idle the drive on the last reference, power DEV9
    // down only on a terminal teardown.
    void shutdownBaseModules();

    // ---- ATA / PFS module latches -----------------------------------------------------------
    unsigned char forceUpdate = 0; // 0 at initial startup: use a valid games.bin; set after the first scan
    unsigned char hdProKitDetected = 0;
    unsigned char modulesLoadCount = 0;
    unsigned char modulesLoaded = 0;
    // Settled PS2FS attempts that must ALL fail before Code 222 is shown. A drive still spinning up
    // answers the ATA stack well before it will answer PS2FS, so a single refusal after the settle says
    // nothing -- hardware shows PFS declining a few of these and then mounting normally. Reset by
    // clearPfsDiagFailure() the moment the support stack latches.
    int pfsSettledFailures = 0;
    hdd_pfs_diag_reason_t pfsDiagReason = HDD_PFS_DIAG_REASON_NONE;
    int pfsDiagHddCheckResult = HDD_PFS_DIAG_NOT_RUN;
    int pfsDiagPs2fsResult = HDD_PFS_DIAG_NOT_RUN;
    // Scopes the diagnostic boot-stage narration to the bracket opened by diagLoadModulesReady().
    unsigned char diagBootStageActive = 0;

    // ---- APA/PFS support stack latches -------------------------------------------------------
    unsigned char supportModulesLoaded = 0;
    // One toast per failure streak: the list update RETRIES the support-module load every refresh
    // while it keeps failing, and re-toasting the same error box each pass would bury the UI. Reset on
    // success so a later, different failure toasts again.
    unsigned char supportErrToasted = 0;
    unsigned char pfsDeferredFailed = 0;
    int retryQueued = 0;

    // ---- staged normal-APA OPL-home selection ------------------------------------------------
    // A Settings selection is deliberately staged until Save Settings. It never changes the active
    // pfs0: mount: a live OPL data home can have artwork/config readers using it.
    int oplHomePending = -1;
    // After the selector is committed, retain its next-boot value for later saves in this process.
    // The live pfs0: data home remains unchanged until the requested restart.
    int oplHomeCommitted = -1;
    // +OPL can be the existing automatic home only when __common was unavailable during discovery.
    // Keep that distinct from an explicit conf_hdd.cfg redirect, which may fall back to __common.
    unsigned char oplHomeAutoPlus = 0;

    // ---- game lists ---------------------------------------------------------------------------
    // Live HDL list (APA-table games); swapped by publishHdlGameList, freed with hddFreeHDLGamelist.
    hdl_games_list_t hdlGames = {};
    // HDD VCD view: PS1 games gathered from pooled __.POPS[0-9]? partitions and one-game PP. / __.
    // partitions. vcdParts is index-parallel to vcdGames -- it records the full owning partition
    // label for the launch handoff.
    base_game_info_t *vcdGames = NULL;
    int vcdGameCount = 0;
    char (*vcdParts)[APA_IDMAX + 1] = NULL;
    // Once-per-session latch (POPSLoader's HAS_CHECKED parity: "HDD is checked only once since it
    // cannot be removed/replaced without damaging the console"). While set, the VCD branch of the
    // list update reuses the built arrays so an L3 toggle rebuilds only the submenu, never re-walks
    // the partitions. A latch (not vcdGames != NULL) so a drive whose candidates scanned to ZERO VCDs
    // is also remembered. Each POPS/Ember partition enumerator distinguishes a successful
    // zero-candidate walk from a failed APA walk; only a complete two-phase scan may replace/latch the
    // current combined PS1 list. Cleared by freeVcdGameList (rebuilds + cleanUp/shutdown teardown)
    // and by invalidateVcdCache (the first-disc-only setting filters at scan time, so its change must
    // rescan). The generation counter keeps an invalidation from being SWALLOWED by a build already in
    // flight on the IO worker (GUI-thread dialog save during a cold scan): the build re-latches only
    // if no invalidation arrived since it started.
    unsigned char vcdListBuilt = 0;
    volatile unsigned int vcdCacheGen = 0;
    // The split-store sentinels returned by activeVcd/activeHdl.
    base_game_info_t emptyVcd = {.extension = ".VCD"};
    hdl_game_info_t emptyHdl = {0};

private:
    void initModules(); // worker-side bring-up, entered through initModulesEntry
    void publishVcdGameList(base_game_info_t *games, char (*parts)[APA_IDMAX + 1], int count, int built);
    // Deferred Code 222/402 diagnostics (was the hddPfsDiag* helper family).
    void armPfsDiagFailure(hdd_pfs_diag_reason_t reason, int hddCheckResult, int ps2fsResult);
    void clearPfsDiagFailure(const char *why);
    const char *pfsDiagReasonName(hdd_pfs_diag_reason_t reason) const;
    void logPfsDiagState(const char *action, const char *why) const;
    void diagBootStageBegin(const char *stage);
    void diagBootStageEnd(const char *stage, int result);
    void diagBootStageEndVoid(const char *stage);
};

extern HddSupport gHddSupport;

#endif
