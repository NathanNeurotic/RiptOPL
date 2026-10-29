#include <new>
#include "include/host.h"
#include "include/opl.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/supportbase.h"
#include "include/udpfssupport.h"
#include "include/cuesupport.h" // combined POPSTARTER/Ember PS1 view
#include "include/saveicon.h"   // saveIconPs1Directory
#include "include/vcdsupport.h"
#include "include/libview.h" // libViewActive / libListViewActive -- which list this page shows
#include "include/folderbrowse.h"
#include "include/util.h"
#include "include/renderman.h"
#include "include/themes.h"
#include "include/lang.h"
#include "include/textures.h"
#include "include/texcache.h"
#include "include/artindex.h"
#include "include/ioman.h"
#include "include/system.h"
#include "include/extern_irx.h"
#include "include/cheatman.h"
#include "include/gamesource.h"
#include "include/ethsupport.h"  // ethGetModulesLoaded() for the UDPFS<->SMB NIC interlock
#include "include/bdmsupport.h"  // bdmIsUDPBDLoaded() for the UDPFS<->UDPBD NIC interlock
#include "include/mmcesupport.h" // mmceSendGameID() cross-device game-id (#261)
#include "modules/iopcore/common/cdvd_config.h"

#include "include/ps2sdkapi_compat.h"
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <delaythread.h>
#include <errno.h>

// udpfs is a network FILESYSTEM device: the iomanX device "udpfs:" is served by the udpfs_ioman IRX
// chain (smap -> ministack(ip=) -> udpfs_ioman). Games are read straight off "udpfs:/" and boot through
// the external Neutrino core (this device has no embedded OPL cdvdman backend), exactly like the "udp"
// BDM transport in bdmsupport.c -- but here the mount is a filesystem, not a block device.

// Theme/server-wait session state. This cluster stays file-scope deliberately: test_udpfs_themes.py
// extracts udpfsDiscoverThemes()/udpfsSetWaitingForServer() by exact signature and compiles them in a
// stub harness that declares these statics itself, and it string-scans UdpfsSource::init/needsUpdate/
// update for the bare identifiers. The game-list cache (counts, arrays, scan latches, the empty-row
// sentinel, the folder latch) lives in UdpfsSupport (include/udpfssupport.h).
static char udpfsPrefix[40]; // full path to the folder that holds all the games ("udpfs:/")
static char *udpfsBase;      // device root ("udpfs:")
static int udpfsIomanModLoaded = 0;
static int udpfsNicClaimed = 0;       // SMAP resident, even if later modules failed; only an IOP reset frees it
static int udpfsWaitingForServer = 0; // the last scan got no answer from the server; see udpfsSetWaitingForServer
static int udpfsThemesScanned = 0;    // THM is registered once per active UDPFS session, after THM opens
static int udpfsLanguagesScanned = 0; // LNG uses the shared registry once its directory becomes readable
static int udpfsPs1Scanned = 0;       // a PS1-view scan has reached the server this session; see udpfsNeedsUpdate
// The share's spelling of ART: a case-sensitive server keeps "art" and "ART" apart. Resolved once per
// session by udpfsDiscoverArtFolder, never per image.
static const char *const udpfsArtCaseNames[8] = {
    "ART", "ARt", "ArT", "Art", "aRT", "aRt", "arT", "art"};
static volatile unsigned int udpfsArtCaseIndex = 0;
static int udpfsArtCaseChecked = 0;

// Another device answering for this PS2's IP (udpfs_ioman devctl, value shared with udpfs_ioman.c).
// The scan runs on the IO thread, but guiWarning draws, so the scan only queues the text and the GUI
// thread shows it (udpfsShowPendingNotice, from the menu frame hook). Once per boot.
#define UDPFS_DEVCTL_IP_CONFLICT 0x55460001
static int udpfsIpConflictWarned = 0;
static volatile int udpfsIpConflictNotice = 0;
static char udpfsIpConflictText[320];

UdpfsSupport gUdpfsSupport;

// Load the UDPFS ioman IRX chain ONCE per boot. Mirrors bdmsupport's udpfs_bd chain load
// (bdmLoadBlockDeviceModules), but loads udpfs_ioman (the FILESYSTEM variant) instead of udpfs_bd (the
// BLOCK-device variant): smap (exports to ministack + ioman) -> ministack (gets the "ip=" arg) ->
// udpfs_ioman (registers the "udpfs:" iomanX device).
//
// SOCKET-ONCE: the ministack has no udp_unbind, so this is load-once -- never DelDrv+re-AddDrv and never
// re-load (a re-bind of the UDPRDMA socket bricks UDPFS after a few cycles). Once udpfsIomanModLoaded is
// set it stays set for the life of the boot.
static void udpfsLoadModules(void)
{
    char ipArg[24];

    LOG("UDPFSSUPPORT LoadModules\n");

    if (udpfsIomanModLoaded)
        return;
    if (udpfsNicClaimed) {
        LOG("UDPFSSUPPORT: partial NIC chain resident, requires IOP reset\n");
        return;
    }

    // NIC-exclusivity: the SMAP adapter registers a single "SMAP_driver". The SMB/ETH stack and the
    // UDPBD/udpfs_bd block chain both own it, so refuse to bring up the UDPFS ioman stack on top of
    // either. The Device-hub UI already interlocks these; this is the runtime backstop.
    if (ethGetModulesLoaded()) {
        LOG("UDPFSSUPPORT: SMB NIC active -- not loading the UDPFS ioman stack\n");
        return;
    }
    if (bdmIsUDPBDLoaded()) {
        LOG("UDPFSSUPPORT: UDPBD/udpfs_bd active -- not loading the UDPFS ioman stack\n");
        return;
    }

    // dev9 is refcounted (shared with ATA-HDD). The ministack has no DHCP client, so it needs the PS2's
    // static IP as an "ip=A.B.C.D" arg -- built exactly as bdmsupport does for the udpfs_bd chain.
    if (!sysInitDev9()) {
        LOG("UDPFSSUPPORT: DEV9 failed -- not loading the UDPFS ioman stack\n");
        sysShutdownDev9();
        return;
    }
    snprintf(ipArg, sizeof(ipArg), "ip=%d.%d.%d.%d", ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3]);

    LOG("[UDPFS_SMAP]:\n");
    if (sysLoadModuleBuffer(&udpfs_smap_irx, size_udpfs_smap_irx, 0, NULL) >= 0) {
        udpfsNicClaimed = 1;
        LOG("[UDPFS_MINISTACK]:\n");
        if (sysLoadModuleBuffer(&udpfs_ministack_irx, size_udpfs_ministack_irx, (int)strlen(ipArg) + 1, ipArg) >= 0) {
            LOG("[UDPFS_IOMAN]:\n");
            if (sysLoadModuleBuffer(&udpfs_ioman_irx, size_udpfs_ioman_irx, 0, NULL) >= 0) {
                udpfsIomanModLoaded = 1;
                LOG("UDPFSSUPPORT Modules loaded\n");
                return;
            }
        }
    }

    // Release the dev9 reference taken above if nothing was loaded -- otherwise a failing/retrying UDPFS
    // inflates the refcounted dev9InitCount and a later HDD/ETH teardown can never power dev9 down. A
    // resident SMAP still needs DEV9 powered, and cannot be replaced without an IOP reset.
    LOG("UDPFSSUPPORT: module chain failed to load\n");
    if (!udpfsNicClaimed)
        sysShutdownDev9();
}

// An in-menu IOP reset unloads every module; forget the latches so the next refresh loads them again.
// Ported from RIPPS2 build 115 (akilluminati47/RIPPS2).
void udpfsIopWasReset(void)
{
    udpfsIomanModLoaded = udpfsNicClaimed = 0;
}

int udpfsGetModulesLoaded(void)
{
    return udpfsNicClaimed; // the NIC interlock must also catch a partial load
}

int udpfsEnsureReady(u32 timeoutMs)
{
    u64 start;

    if (ethGetModulesLoaded() || bdmIsUDPBDLoaded())
        return 0;

    if (!udpfsIomanModLoaded)
        udpfsLoadModules();
    if (!udpfsIomanModLoaded)
        return 0;

    // A healthy, already-connected server returns on the first probe. A just-loaded ioman session
    // can need a short discovery window, so only the explicit custom-path request pays this wait.
    start = GetTimerSystemTime();
    do {
        struct stat st;
        if (stat("udpfs:/", &st) == 0)
            return 1;
        if ((GetTimerSystemTime() - start) / (kBUSCLK / 1000) >= timeoutMs)
            break;
        DelayThread(100 * 1000);
    } while (1);

    return 0;
}

// True when the server answers. While the udpfs_ioman session is down, every call fails inside the IOP
// before anything reaches the network, so this is cheap enough to poll.
static int udpfsServerAnswers(void)
{
    struct stat st;

    return stat(udpfsPrefix, &st) == 0;
}
// The server is silent. If another device has answered ARP for this PS2's IP, the server's replies
// go to that device: say so once, with the address and the other device's MAC, instead of leaving an
// empty list. The query is answered inside the IOP without network traffic.
static void udpfsCheckIpConflict(void)
{
    static unsigned char mac[64] __attribute__((aligned(64)));

    if (udpfsIpConflictWarned || !udpfsIomanModLoaded)
        return;
    if (fileXioDevctl("udpfs:", UDPFS_DEVCTL_IP_CONFLICT, NULL, 0, mac, 6) != 1)
        return;
    udpfsIpConflictWarned = 1;
    snprintf(udpfsIpConflictText, sizeof(udpfsIpConflictText), "%s\n%d.%d.%d.%d  /  %02X:%02X:%02X:%02X:%02X:%02X",
             _l(_STR_UDPFS_IP_CONFLICT), ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3],
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    LOG("UDPFSSUPPORT: IP conflict -- %s\n", udpfsIpConflictText);
    udpfsIpConflictNotice = 1;
}

void udpfsShowPendingNotice(void)
{
    if (!udpfsIpConflictNotice)
        return;
    udpfsIpConflictNotice = 0;
    host::ui().warning(udpfsIpConflictText, 10);
}

class UdpfsSource: public GameSource
{
public:
    UdpfsSource()
        : GameSource(UDPFS_MODE, 1, 0, MENU_MIN_INACTIVE_FRAMES, UDPFS_MODE_UPDATE_DELAY, GameSource::CAP_LAUNCH_VCD | GameSource::CAP_LAUNCH_CUE | GameSource::CAP_ROW_VIEW | GameSource::CAP_SOURCE_ID | GameSource::CAP_SAVE_CUE_SETTINGS)
    {
    }

    void init(item_list_t *itemList) override;
    int rowView(item_list_t *itemList, int id) override;
    int sourceId(item_list_t *itemList, int id) override;
    int saveCueSettings(item_list_t *itemList, int id, const char *name, config_set_t *configSet) override;
    int ps1SaveDir(item_list_t *itemList, int id, const char *name, int *ember, char *directory, int directorySize,
                   char *partition, int partitionSize) override;
    int needsUpdate(item_list_t *itemList) override;
    int update(item_list_t *itemList) override;
    int count(item_list_t *itemList) override;
    void *game(item_list_t *itemList, int id) override;
    char *name(item_list_t *itemList, int id) override;
    int nameLength(item_list_t *itemList, int id) override;
    char *startup(item_list_t *itemList, int id) override;
    void deleteGame(item_list_t *itemList, int id) override;
    void rename(item_list_t *itemList, int id, char *newName) override;
    void launchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet) override;
    void launchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet) override;
    void launch(item_list_t *itemList, int id, config_set_t *configSet) override;
    config_set_t *config(item_list_t *itemList, int id) override;
    int image(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm) override;
    int textId(item_list_t *itemList) override;
    int iconId(item_list_t *itemList) override;
    void cleanUp(item_list_t *itemList, int exception) override;
    void shutdown(item_list_t *itemList) override;
    int checkVmc(item_list_t *itemList, char *name, int createSize) override;
    char *prefix(item_list_t *itemList) override;
};

static UdpfsSource *udpfsInstance()
{
    static UdpfsSource *instance = NULL;
    alignas(UdpfsSource) static unsigned char storage[sizeof(UdpfsSource)];

    if (instance == NULL)
        instance = new (storage) UdpfsSource();
    return instance;
}

int UdpfsSupport::activeGameCount(const item_list_t *itemList) const
{
    int view = host::library().listViewActive(itemList);
    return view == LIB_VIEW_MIXED ? gameCount_ + ps1GameCount_ :
           view == LIB_VIEW_PS1   ? ps1GameCount_ :
                                    gameCount_;
}

// Resolve every row through one bounds-checked sentinel, matching the BDM support's split-list
// guard. L3 now stages its destination until the old submenu is cleared, but an unscanned, failed,
// partial, or teardown refresh can still leave either array NULL/shorter than a stale id.
base_game_info_t *UdpfsSupport::activeGame(item_list_t *itemList, int id)
{
    int ps1 = udpfsInstance()->rowView(itemList, id) == LIB_VIEW_PS1;
    id = udpfsInstance()->sourceId(itemList, id);
    base_game_info_t *games = ps1 ? ps1Games_ : games_;
    int count = ps1 ? ps1GameCount_ : gameCount_;

    if (games == NULL || id < 0 || id >= count)
        return &emptyGame_;
    return &games[id];
}

void UdpfsSupport::clearGameLists()
{
    free(games_);
    games_ = NULL;
    gameCount_ = 0;
    free(ps1Games_);
    ps1Games_ = NULL;
    ps1GameCount_ = 0;
}


// Without Automatic Refresh (off by default) this page is scanned when it first comes up and on SELECT,
// nothing else. udpfs_ioman connects in the background, so a first scan that ran before the server
// answered -- server started after the console, slow link negotiation, server restarted -- left the page
// empty for good, and the server log showed DISCOVERY and nothing after it. While waiting, poll through
// the every-frame background path (2 s throttle, idle only, yields to cover art) and rescan the moment the
// server answers; once a scan reaches it, go back to the normal cadence.
static void udpfsSetWaitingForServer(int waiting)
{
    udpfsWaitingForServer = waiting;
    // Keep the lightweight background poll alive until THM, LNG and ART resolution complete. The server
    // root/game folders can become usable slightly before those folders; without this, a failed first
    // attempt drops back to the normal cadence and, with Automatic Refresh off, is never retried for
    // the rest of the session (original #1036/#1037).
    (*udpfsInstance()).updateDelay = (waiting || !udpfsThemesScanned || !udpfsLanguagesScanned || !udpfsArtCaseChecked) ? MENU_UPD_DELAY_GENREFRESH : UDPFS_MODE_UPDATE_DELAY;
}

void UdpfsSource::init(item_list_t *itemList)
{
    LOG("UDPFSSUPPORT Init\n");

    // A settings re-apply can re-enter this initializer while UDPFS themes are already registered.
    // Remove only the old udpfs:-backed entries before scanning the refreshed server again, matching
    // SMB's reconnect behavior and avoiding duplicate theme names in the picker.
    if (udpfsBase != NULL)
        thmReinit(udpfsBase);

    udpfsBase = "udpfs:";
    udpfsThemesScanned = 0;
    udpfsLanguagesScanned = 0;
    udpfsPs1Scanned = 0;
    udpfsArtCaseIndex = 0;
    udpfsArtCaseChecked = 0;
    // The games live directly at the device root; no share/prefix to prepend (unlike SMB).
    snprintf(udpfsPrefix, sizeof(udpfsPrefix), "udpfs:/");
    gUdpfsSupport.resetScanState();
    gUdpfsSupport.clearGameLists();
    (*udpfsInstance()).delay = gArtDelay;
    udpfsSetWaitingForServer(0);
    ioPutRequest(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&udpfsLoadModules));
    (*udpfsInstance()).enabled = 1;
}

item_list_t *udpfsGetObject(int initOnly)
{
    if (initOnly && !(*udpfsInstance()).enabled)
        return NULL;
    return &(*udpfsInstance());
}

// Keep the PS2 and Ember lists physically separate. Besides making forced-view/Favorites proxies
// unambiguous, this prevents a transient first Ember scan failure from exposing stale PS2 rows on
// the PS1 page. A network PS1 page is therefore Ember-only even in its failure state.
int UdpfsSource::rowView(item_list_t *itemList, int id)
{
    int view = host::library().listViewActive(itemList);
    if (view == LIB_VIEW_MIXED)
        return id >= 0 && id < gUdpfsSupport.gameCount_ ? LIB_VIEW_ISO : LIB_VIEW_PS1;
    return view;
}

int UdpfsSource::sourceId(item_list_t *itemList, int id)
{
    return host::library().listViewActive(itemList) == LIB_VIEW_MIXED && id >= gUdpfsSupport.gameCount_ ? id - gUdpfsSupport.gameCount_ : id;
}

// Theme discovery used to be missing entirely from the UDPFS filesystem backend: BDM, MMCE, SMB and
// APA all register their THM directories when the device becomes usable, while UDPFS only scanned
// games/art. Once the server-backed root actually answers, register udpfs:/THM exactly once for this
// session. thmAddElements(..., forceRefresh=1) also reapplies a saved UDPFS theme as soon as it
// appears.
//
// Do NOT latch merely because the root answered. listDir()/thmAddElements() returns 0 both for an
// existing-but-empty THM directory and for a directory that could not be opened at all. On Auto boot
// the root/game paths can win that race before THM is ready; latching that failed attempt made themes
// disappear for the whole session. Probe THM itself first. A successful open (even if empty) is the
// proof that completes discovery; a failed open keeps the lightweight background retry armed.
//
// ANY CASE. The share is a PC folder, and on a case-sensitive server (Linux) "thm" and "THM" are two
// different folders. Worse, sbCreateFolders makes an empty "THM" beside a user's "thm", and a fixed
// "THM" open then found that empty one and latched with no themes. So list the root and scan every
// folder whose name is THM in any case -- an empty auto-created one costs nothing.
static void udpfsDiscoverThemes(void)
{
    char themePath[128];
    DIR *root, *dir;
    struct dirent *entry;
    int found = 0;

    if (udpfsThemesScanned || !udpfsIomanModLoaded || !udpfsServerAnswers())
        return;

    root = opendir(udpfsPrefix);
    if (root == NULL)
        return;
    while ((entry = readdir(root)) != NULL) {
        if (strcasecmp(entry->d_name, "THM") != 0)
            continue;
        snprintf(themePath, sizeof(themePath), "%s%s", udpfsPrefix, entry->d_name);
        dir = opendir(themePath); // a directory, whatever d_type the driver reports
        if (dir == NULL)
            continue;
        closedir(dir);
        thmAddElements(themePath, "/", 1);
        found = 1;
    }
    closedir(root);

    if (!found)
        return;
    udpfsThemesScanned = 1;
    // If server-waiting is already clear, retire the temporary general-refresh cadence now that
    // theme discovery really completed. If the server is still considered down, preserve that wait.
    udpfsSetWaitingForServer(udpfsWaitingForServer);
}

// UDPFS registered no languages at all (original #1035). Match the shared LNG registration the other
// devices use, scanning every case variant so an empty auto-created LNG cannot hide a populated lng.
// A failed root listing or an unopenable LNG keeps retrying.
static void udpfsDiscoverLanguages(void)
{
    char path[128];
    DIR *root, *dir;
    struct dirent *entry;
    int found = 0, failed = 0, error;

    if (udpfsLanguagesScanned || !udpfsIomanModLoaded || !udpfsServerAnswers())
        return;
    root = opendir(udpfsPrefix);
    if (root == NULL)
        return;
    for (;;) {
        errno = 0;
        entry = readdir(root);
        if (entry == NULL) {
            error = errno;
            break;
        }
        if (strcasecmp(entry->d_name, "LNG") != 0)
            continue;
        snprintf(path, sizeof(path), "%s%s", udpfsPrefix, entry->d_name);
        dir = opendir(path);
        if (dir == NULL) {
            failed = 1;
            continue;
        }
        closedir(dir);
        lngAddLanguages(path, "/", (*udpfsInstance()).mode);
        found = 1;
    }
    closedir(root);
    if (!found || failed || error)
        return;
    udpfsLanguagesScanned = 1;
    udpfsSetWaitingForServer(udpfsWaitingForServer);
}

// 1 if udpfs:/<variant> lists at least one entry, 0 if empty, -1 if it cannot be opened (a file named
// "art", say), -2 on a read error (transient: discovery must retry, not latch).
static int udpfsArtFolderHasFiles(unsigned int variant)
{
    char path[64];
    DIR *dir;
    struct dirent *entry;
    int found = 0;

    snprintf(path, sizeof(path), "%s%s", udpfsPrefix, udpfsArtCaseNames[variant]);
    dir = opendir(path);
    if (dir == NULL)
        return -1;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0)
                found = -2;
            break;
        }
        if (entry->d_name[0] != '.') {
            found = 1;
            break;
        }
    }
    closedir(dir);
    return found;
}

// Resolve the share's ART spelling once its root can be fully listed. The ordinary path stays
// udpfs:/ART, including on new shares; if a case-sensitive server has both an empty auto-created ART
// and a populated art, the populated one wins. A single canonical ART costs no extra directory open.
// Discovery always ends by re-arming covers parked as missing and the art index: first-connect probes
// can memoize both before the server's ART is known (original #1037).
static void udpfsDiscoverArtFolder(void)
{
    DIR *root;
    struct dirent *entry;
    unsigned int candidates = 0;
    unsigned int chosen = 0;
    int readError;

    if (udpfsArtCaseChecked || !udpfsIomanModLoaded || !udpfsServerAnswers())
        return;

    root = opendir(udpfsPrefix);
    if (root == NULL)
        return;
    for (;;) {
        errno = 0;
        if ((entry = readdir(root)) == NULL)
            break;
        if (strlen(entry->d_name) == 3 && strcasecmp(entry->d_name, "ART") == 0) {
            for (unsigned int i = 0; i < 8; i++) {
                if (strcmp(entry->d_name, udpfsArtCaseNames[i]) == 0) {
                    candidates |= 1u << i;
                    break;
                }
            }
        }
    }
    readError = errno;
    closedir(root);
    if (readError != 0)
        return; // an incomplete root listing must not become a permanent answer

    if (candidates != 0 && !(candidates & 1u)) {
        for (unsigned int i = 1; i < 8; i++) {
            if (candidates & (1u << i)) {
                chosen = i;
                break;
            }
        }
    }
    if (candidates & (candidates - 1u)) {
        int chosenState = udpfsArtFolderHasFiles(chosen);
        if (chosenState == -2)
            return;
        if (chosenState <= 0) {
            unsigned int fallback = chosenState == 0 ? chosen : 0;
            int haveFallback = chosenState == 0;
            int foundPopulated = 0;
            for (unsigned int i = 0; i < 8; i++) {
                if (i == chosen || !(candidates & (1u << i)))
                    continue;
                int candidateState = udpfsArtFolderHasFiles(i);
                if (candidateState == -2)
                    return;
                if (candidateState > 0) {
                    chosen = i;
                    foundPopulated = 1;
                    break;
                }
                if (candidateState == 0 && !haveFallback) {
                    fallback = i;
                    haveFallback = 1;
                }
            }
            if (!foundPopulated)
                chosen = haveFallback ? fallback : 0;
        }
    } else if (chosen != 0) {
        // A single noncanonical entry must really be a directory; d_type is unreliable here.
        int chosenState = udpfsArtFolderHasFiles(chosen);
        if (chosenState == -2)
            return;
        if (chosenState < 0)
            chosen = 0;
    }

    udpfsArtCaseChecked = 1;
    udpfsArtCaseIndex = chosen;
    cacheInvalidateFailMemo();
    artIndexInvalidate();
    udpfsSetWaitingForServer(udpfsWaitingForServer);
}

int UdpfsSource::needsUpdate(item_list_t *itemList)
{
    int result = 0;

    // Keep this before every view-specific early return so a remembered PS1/Ember page cannot
    // suppress theme, language or ART discovery.
    udpfsDiscoverThemes();
    udpfsDiscoverLanguages();
    udpfsDiscoverArtFolder();

    // PS1/Ember view: force a rescan once on toggle, then refresh on toggle only (skip disc heuristics).
    if (host::library().consumeDirty(itemList->mode))
        return 1;
    // Folder browsing: descend/ascend forces one rescan.
    if (folderConsumeDirty(itemList->mode))
        return 1;
    // Waiting for the server: rescan only once it answers, so an unreachable server costs one failed stat
    // per poll instead of an empty rebuild every two seconds. Ahead of the PS1 early-out so Ember heals too.
    if (udpfsWaitingForServer)
        return udpfsServerAnswers();
    // The PS1 view refreshes on L3 / SELECT only -- but it must still get its FIRST scan. A page
    // restored onto a remembered PS1 view (Remember Last Played on an Ember title) otherwise returned
    // here forever and stayed blank until the user toggled L3 (TwistedZeon, 09-29). MMCE and BDM
    // build that first list the same way (mmcePs1Scanned; bdmNeedsUpdate's connect pass).
    if (host::library().listViewActive(itemList) == LIB_VIEW_PS1)
        return !udpfsPs1Scanned;

    if (!gUdpfsSupport.isoScanned_)
        result = 1;

    // Only stat the mount once the ioman device is actually up -- statting "udpfs:/..." before then
    // just fails and would keep result at 0 anyway, but guard it to avoid pointless RPC churn.
    if (udpfsIomanModLoaded) {
        struct stat st;
        char path[256];

        snprintf(path, sizeof(path), "%sCD", udpfsPrefix);
        if (stat(path, &st) != 0)
            st.st_mtime = 0;
        if (gUdpfsSupport.modifiedCDPrev_ != st.st_mtime) {
            gUdpfsSupport.modifiedCDPrev_ = st.st_mtime;
            result = 1;
        }

        snprintf(path, sizeof(path), "%sDVD", udpfsPrefix);
        if (stat(path, &st) != 0)
            st.st_mtime = 0;
        if (gUdpfsSupport.modifiedDVDPrev_ != st.st_mtime) {
            gUdpfsSupport.modifiedDVDPrev_ = st.st_mtime;
            result = 1;
        }
    }

    return result;
}

int UdpfsSource::update(item_list_t *itemList)
{
    int view = host::library().listViewActive(itemList);
    int reached = 0;
    int answeredBefore;
    if (udpfsIomanModLoaded == 0)
        return gUdpfsSupport.activeGameCount(itemList);

    // Asked BEFORE scanning: a scan that started while the server was not answering cannot be trusted
    // even when it "succeeds" -- the Ember scan reports an unreachable server as "no titles" -- so
    // such a pass always keeps polling, and the next poll rescans once the server answers.
    answeredBefore = udpfsServerAnswers();

    // Resolve an existing lowercase ART before sbCreateFolders creates an empty ART beside it.
    if (answeredBefore)
        udpfsDiscoverArtFolder();

    // Latch only once the server can hear us. A scan that ran before it answered used to latch here, so
    // the CD/DVD folders were never created for the rest of the boot.
    if (!gUdpfsSupport.foldersCreated_ && answeredBefore) {
        sbCreateFolders(udpfsPrefix, 1);
        gUdpfsSupport.foldersCreated_ = 1;
    }

    // Folder creation includes THM. Discover only AFTER that pass so a fresh share gets the same
    // chance as one that already contained THM; udpfsDiscoverThemes itself refuses to latch a failed
    // directory open and leaves the background retry cadence armed.
    if (answeredBefore)
        udpfsDiscoverThemes();
    if (answeredBefore)
        udpfsDiscoverLanguages();

    if (view == LIB_VIEW_PS1 || view == LIB_VIEW_MIXED) {
        // Ember inherits this mount; POPSTARTER restores networking through the UDPFS BDMA pair.
        int r = ps1FillGameList(udpfsPrefix, &gUdpfsSupport.ps1Games_);
        if (answeredBefore && r >= 0 && !ps1GameListNeedsRetry()) // only a complete server-backed scan counts
            udpfsPs1Scanned = 1;
        if (r >= 0) // r < 0: transient scan failure -> preserve the last-good list
            gUdpfsSupport.ps1GameCount_ = r;
    }
    if (view == LIB_VIEW_ISO || view == LIB_VIEW_MIXED) {
        // Neutrino cannot launch USBExtreme parts. A failed scan keeps the last-good library.
        if (sbReadList(&gUdpfsSupport.games_, udpfsPrefix, folderGetSub(itemList->mode), NULL, &gUdpfsSupport.gameCount_) < 0) {
            gUdpfsSupport.isoScanned_ = 0;
        } else {
            reached = 1;
            gUdpfsSupport.isoScanned_ = answeredBefore;
        }
    }
    // Only a readable CD/DVD scan proves the server answered. A share without those folders, or the PS1
    // view (whose empty result can't tell "no titles" from "no answer"), asks the server itself before
    // deciding to wait -- otherwise a working server would be polled and rescanned every two seconds.
    udpfsSetWaitingForServer(!answeredBefore || (!reached && !udpfsServerAnswers()));
    if (udpfsWaitingForServer)
        udpfsCheckIpConflict();
    return gUdpfsSupport.activeGameCount(itemList);
}

int UdpfsSource::count(item_list_t *itemList)
{
    return gUdpfsSupport.activeGameCount(itemList);
}

void *UdpfsSource::game(item_list_t *itemList, int id)
{
    return (void *)gUdpfsSupport.activeGame(itemList, id);
}

char *UdpfsSource::name(item_list_t *itemList, int id)
{
    return gUdpfsSupport.activeGame(itemList, id)->name;
}

int UdpfsSource::nameLength(item_list_t *itemList, int id)
{
    if (gUdpfsSupport.activeGame(itemList, id)->format != GAME_FORMAT_USBLD)
        return ISO_GAME_NAME_MAX + 1;
    else
        return UL_GAME_NAME_MAX + 1;
}

char *UdpfsSource::startup(item_list_t *itemList, int id)
{
    base_game_info_t *game = gUdpfsSupport.activeGame(itemList, id);

    // VCD view keys per-game data (CFG/art) off the VCD filename, not a disc ID (see sbPopulateConfig).
    if (rowView(itemList, id) == LIB_VIEW_PS1)
        return game->name;
    return game->startup;
}

void UdpfsSource::deleteGame(item_list_t *itemList, int id)
{
    if (rowView(itemList, id) == LIB_VIEW_PS1)
        return; // PS1 rows are Ember folders, not CD/DVD images for sbDelete
    if (gUdpfsSupport.activeGame(itemList, id) == &gUdpfsSupport.emptyGame_)
        return;
    sbSetBrowseSub(folderGetSub(itemList->mode)); // delete inside the current subfolder, not the root
    id = sourceId(itemList, id);
    sbDelete(&gUdpfsSupport.games_, udpfsPrefix, "/", gUdpfsSupport.gameCount_, id);
    gUdpfsSupport.invalidateIsoScan();
}

void UdpfsSource::rename(item_list_t *itemList, int id, char *newName)
{
    if (rowView(itemList, id) == LIB_VIEW_PS1) {
        base_game_info_t *game = gUdpfsSupport.activeGame(itemList, id);
        if (game != &gUdpfsSupport.emptyGame_ && cueIsCueEntry(game) && cueRenameGame(udpfsPrefix, game->name, newName) == 0)
            host::library().markDirty(itemList->mode); // the already-queued menu update must re-scan EMBER/games
        return;
    }
    if (gUdpfsSupport.activeGame(itemList, id) == &gUdpfsSupport.emptyGame_)
        return;
    id = sourceId(itemList, id);
    sbSetBrowseSub(folderGetSub(itemList->mode)); // rename inside the current subfolder, not the root
    sbRename(&gUdpfsSupport.games_, udpfsPrefix, "/", gUdpfsSupport.gameCount_, id, newName);
    gUdpfsSupport.invalidateIsoScan();
}

// Shared UDPFS VCD handoff for this source page and Favorites; prepare before teardown. UDPFS
// Assault serves every mass: file from the server, so POPSTARTER, the driver pair and the optional
// DAT files all come from the server's POPS/ folder.
void UdpfsSource::launchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet)
{
    char elf[256], selector[320], vcdPath[320], popsDir[96], diag[160];
    const int mode = VCD_BDMA_UDPFS;
    (void)configSet;
    if (vcdName == NULL || vcdName[0] == '\0' || !strcasecmp(vcdName, "POPSTARTER"))
        return;
    // Linux servers are case-sensitive and this IRX only accepts uppercase .VCD. Without one, UDPFS
    // Assault builds the VCD from POPS/<game>.cue (or .CUE) and its .bin files on the fly.
    snprintf(vcdPath, sizeof(vcdPath), "%sPOPS/%s.VCD", udpfsPrefix, vcdName);
    int fd = open(vcdPath, O_RDONLY);
    if (fd < 0) {
        char cuePath[320];
        static const char *const cueExt[] = {".cue", ".CUE"};
        for (int k = 0; k < 2 && fd < 0; k++) {
            snprintf(cuePath, sizeof(cuePath), "%sPOPS/%s%s", udpfsPrefix, vcdName, cueExt[k]);
            fd = open(cuePath, O_RDONLY);
        }
        if (fd < 0) {
            host::ui().missingFile(_l(_STR_ERR_FILE_INVALID), vcdPath);
            return;
        }
    }
    close(fd);
    if (!vcdResolvePopstarter(udpfsPrefix, elf, sizeof(elf))) {
        vcdDescribePopstarterLookup(udpfsPrefix, elf, sizeof(elf));
        host::ui().missingFile(_l(_STR_POPSTARTER_NOT_FOUND), elf);
        return;
    }
    host::ui().renderTextScreen(_l(_STR_PLEASE_WAIT));
    (void)vcdInstallPopstarterMc(udpfsPrefix);
    snprintf(popsDir, sizeof(popsDir), "%s" POPS_FOLDER "/", udpfsPrefix);
    int er = vcdInstallPopsUdpfsNet(popsDir);
    if (er != 0) {
        host::ui().msgBox(_l(er == -2 ? _STR_BDMA_ERR_SPACE : _STR_BDMA_ERR_IO), 0, NULL);
        return;
    }
    if (gBdmaApplyOnLaunch && !vcdBdmaLaunchReady(mode)) {
        er = vcdEquipBdmaFromPops(popsDir, mode, diag, sizeof(diag));
        if (er != 0) {
            host::ui().msgBox(_l(er == -5 ? _STR_BDMA_ERR_OLD : er == -4 ? _STR_BDMA_ERR_SRC :
                                                            er == -2     ? _STR_BDMA_ERR_SPACE :
                                                                           _STR_BDMA_ERR_IO),
                              0, NULL);
            return;
        }
    }
    if (!vcdBdmaLaunchReady(mode)) {
        // Unlike local POPSTARTER, network VCDs require this exact driver family.
        host::ui().msgBox(_l(_STR_POPS_UDPFS_DRIVER_NOT_READY), 0, NULL);
        return;
    }
    // POPS UDPFS has no RA integration yet: a module a tracked launch left in the server's POPS/
    // would report telemetry for a game it does not see.
    if (!vcdConfirmCleanLaunch(udpfsPrefix))
        return;
    vcdBuildSelector(udpfsPrefix, VCD_PREFIX_MASS, vcdName, selector, sizeof(selector));
    vcdPrepareRetroGemBarcode(vcdPath);
    host::app().deinitEx(sbLoaderDeinitException(elf), itemList->mode, host::app().pathToMode(elf));
    sysLaunchPopstarter(elf, selector);
}

// Ember over UDPFS. Ember performs no IOP reset, so it inherits the live udpfs: filesystem and can
// read both its own ELF and EMBER/games/<name>/ from the connection already mounted by RiptOPL.
void UdpfsSource::launchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet)
{
    char emberElf[256], biosPath[288];
    char launchName[CUE_NAME_MAX];

    if (cueName == NULL || cueName[0] == '\0')
        return;
    if (!cueNameLaunchable(cueName)) {
        host::ui().msgBox(_l(_STR_EMBER_BAD_NAME), 0, NULL);
        return;
    }
    if (!cueResolveEmber(udpfsPrefix, emberElf, sizeof(emberElf))) {
        host::ui().missingFile(_l(_STR_EMBER_NOT_FOUND), emberElf);
        return;
    }
    if (!cueResolveEmberBios(udpfsPrefix, biosPath, sizeof(biosPath))) {
        host::ui().missingFile(_l(_STR_EMBER_BIOS_MISSING), biosPath);
        return;
    }
    // Copies the argument onto our stack: cleanup frees the cached PS1 list during deinit.
    if (!cuePrepareLaunch(udpfsPrefix, cueName, emberElf, launchName, sizeof(launchName)))
        return;

    cueApplySettings(udpfsPrefix, cueName, configSet); // best-effort marker, never a launch gate
    if (!cueGameHasImage(udpfsPrefix, cueName)) {
        host::ui().msgBox(_l(_STR_EMBER_NO_DISC), 0, NULL);
        return;
    }

    // Launch metadata, not Ember identity: resolve the GameID from the real image while the device
    // and the GUI are still live. A miss never blocks the launch.
    cuePrepareRetroGemBarcode(udpfsPrefix, cueName);

    // UNMOUNT_EXCEPTION is load-bearing: Ember inherits this live mount. udpfsCleanUp keeps the
    // socket/module chain resident for the whole boot, so no network reinitialization is attempted.
    host::app().deinit(UNMOUNT_EXCEPTION, itemList->mode);
    sysLaunchEmber(emberElf, launchName);
}

void UdpfsSource::launch(item_list_t *itemList, int id, config_set_t *configSet)
{
    int compatmask = 0, index;
    int EnablePS2Logo = 0;
    int result;
    char filename[32], partname[256];
    base_game_info_t *game = gUdpfsSupport.activeGame(itemList, id);

    if (game == &gUdpfsSupport.emptyGame_)
        return; // stale/invalid id

    // Folder browsing: a folder row is never launched (the dispatch descends first); guard defensively
    // and pin the path composers to the current subfolder so a nested game resolves.
    if (game != NULL && game->format == GAME_FORMAT_FOLDER)
        return;
    sbSetBrowseSub(folderGetSub(itemList->mode));

    // Dispatch each PS1 row through its owning core.
    if (game != NULL && (rowView(itemList, id) == LIB_VIEW_PS1)) {
        if (cueIsCueEntry(game))
            launchCue(itemList, game->name, configSet);
        else
            launchVcd(itemList, game->name, configSet);
        return;
    }

    // The udpfs device has NO embedded OPL cdvdman backend -- games boot ONLY via the external Neutrino
    // core. Resolve the neutrino.elf below (while `game`/`configSet` are still valid -- deinit frees them),
    // and reject formats Neutrino cannot run (UL-split / .zso), mirroring bdmsupport's udp Neutrino path.
    // NB: VMC emulation is emitted as discrete -mcN Neutrino args (sbBuildVmcNeutrinoArgs below), not via
    // an OPL mcemu IRX, so no smb/bdm-style mcemu patch loop is needed here.

    if (gRememberLastPlayed) {
        configSetStr(configGetByType(CONFIG_LAST), "last_played", game->startup);
        host::app().saveConfig(CONFIG_LAST, 0);
    }

    // sbPrepare resolves the per-game compat mask (and per-game pad-emu etc.). The udpfs launch does not
    // patch an OPL cdvdman IRX, but sbPrepare still needs a valid buffer to locate the patch zone; reuse
    // the smb cdvdman image as a scratch target (its patched fields are never launched here -- only the
    // returned compatmask is used). A follow-up stage finalizes the exact backend wiring.
    compatmask = sbPrepare(game, configSet, size_smb_cdvdman_irx, smb_cdvdman_irx, &index);
    if (compatmask < 0)
        return;
    compatmask = sbGetCompatModes(configSet, 1); // UDPFS only launches Neutrino, and a title default can differ per core

    if ((result = sbLoadCheats(udpfsPrefix, game->startup, configSet)) < 0) {
        // #265: let the user back out instead of sitting through the whole load. The helper does
        // the sbUnprepare itself -- see include/supportbase.h; skipping it breaks the NEXT launch.
        // udpfs never assigns `settings` (Neutrino reads the game, not an OPL cdvdman), but
        // sbPrepare still patched smb_cdvdman_irx, so the unwind is required all the same.
        if (!sbCheatsMissingContinue((u8 *)(&smb_cdvdman_irx) + index, result))
            return;
    }
    // The scratch patch has done its job (the compat mask, and the cheats step's way back out above).
    // Restore the zone NOW, so every return below -- format, no neutrino.elf, the preflights, the
    // cheats hand-off -- leaves it pristine: smb_cdvdman_irx is the SMB leg's IRX too, and a zone left
    // patched fails the next SMB or UDPFS launch's sbPrepare ("unable to locate patch zone").
    sbUnprepare((u8 *)(&smb_cdvdman_irx) + index);
    sbLoadImage(udpfsPrefix, game->startup);

    if (gPS2Logo) {
        int fd;
        sbCreatePath(game, partname, udpfsPrefix, "/", 0);
        fd = open(partname, O_RDONLY, 0666);
        if (fd >= 0) {
            EnablePS2Logo = CheckPS2Logo(fd, 0);
            close(fd);
        }
    }

    if (configGetStrCopy(configSet, CONFIG_ITEM_ALTSTARTUP, filename, sizeof(filename)) == 0)
        strcpy(filename, game->startup);

    // Per-game Neutrino ELF + flags, resolved BEFORE deinit frees configSet's owner.
    const char *neutrinoPath = NULL;
    char neutrinoExtraArgs[256] = "";
    int neutrinoVideo = gNeutrinoVideoDefault, neutrinoGsmComp = gNeutrinoGsmCompDefault; // absent per-game keys = follow the globals
    neutrino_vmc_args_t neutrinoVmc = {0};

    configGetStrCopy(configSet, CONFIG_ITEM_NEUTRINO_ARGS, neutrinoExtraArgs, sizeof(neutrinoExtraArgs));
    configGetInt(configSet, CONFIG_ITEM_NEUTRINO_VIDEO, &neutrinoVideo);
    configGetInt(configSet, CONFIG_ITEM_NEUTRINO_GSMCOMP, &neutrinoGsmComp);
    neutrinoPath = sbResolveNeutrinoPath(udpfsPrefix);

    // Format / neutrino availability check: on failure show the network-specific message (the OPL-core
    // fallback the generic _STR_NEUTRINO_* warnings promise does not exist for a network filesystem) and
    // abort cleanly while the GUI is still up (no deinit into a no-op launch = black screen).
    if (game->format == GAME_FORMAT_USBLD || !strcasecmp(game->extension, ".zso")) {
        host::ui().warning(_l(_STR_NET_NEEDS_NEUTRINO), 6);
        return;
    }
    if (neutrinoPath == NULL) {
        host::ui().warning(_l(_STR_NET_NEEDS_NEUTRINO), 6);
        return;
    }

    // VMC -> neutrino (#47): resolve any per-game VMC into discrete -mc0/-mc1 argv entries before deinit.
    sbBuildVmcNeutrinoArgs(configSet, udpfsPrefix, &neutrinoVmc);

    // Build the filesystem device path Neutrino reads the game from. This is a plausible, correct-shaped
    // -dvd target (the udpfs: filesystem game file); a follow-up stage finalizes the exact -dvd/-bsd argv.
    sbCreatePath(game, partname, udpfsPrefix, "/", 0);

    // Neutrino keep-IOP handoff (sysLoadELFKeepIOP): Neutrino reads the game through OUR udpfs
    // filesystem and its config/modules from the neutrino.elf device (-cwd) before its own IOP
    // reset -- keep BOTH mounted across the teardown. Mirrors bdmsupport's Neutrino deinit contract.
    // D6: pre-teardown validation -- for UDPFS this is where the bsd toml ip= sync now happens
    // (was post-deinit inside sysLaunchNeutrino); a failure toasts and stays in the menu.
    //
    // ORDER (matches bdmsupport/hddsupport): the preflight runs BEFORE the GameID push, never after.
    // The 0x8 devctl below physically re-mounts the MMCE card, and on Gen2 the busy bit it waits on
    // can clear before the FILESYSTEM surface is back (see mmceSettleAfterSwitch). On a
    // "Neutrino Device = MMCE" install the preflight's config/bsd-udpfs.toml open lands on mmceN:,
    // so running it after the push aimed that open straight into the re-mount window and failed the
    // launch with _STR_NEUTRINO_TOML_SYNC_FAILED on a toml that reads fine a moment earlier or
    // later. Preflighting first also means an abort leaves the card UNSWITCHED instead of dropping
    // the user back into the menu sitting on the per-game card.
    if (sysNeutrinoPreflight("udpfs", neutrinoPath, 0, NULL, -1) < 0)
        return;
    // Cheats: loaded above (sbLoadCheats) and handed to Neutrino here. Backing out stays in the menu.
    if (sbNeutrinoHandCheats(game->startup, neutrinoPath, neutrinoExtraArgs) < 0)
        return;
#ifdef RETROACHIEVEMENTS
    sbNeutrinoPrepareRA(udpfsPrefix, game->startup, "udpfs", neutrinoPath, neutrinoExtraArgs);
#endif
    if (sysNeutrinoArgsPreflight("udpfs", partname, game->startup, compatmask, EnablePS2Logo, neutrinoPath, neutrinoExtraArgs, neutrinoVideo, neutrinoGsmComp, 0, -1, &neutrinoVmc) < 0)
        return;

    sysNeutrinoWarnPadEmu(configSet);
    // MMCE cross-device game-id (#261): push the disc id to a present MMCE card before teardown frees
    // `game`. Neutrino path forwarded so a Neutrino launch protects the MMCE hand-off timing.
    mmceSendGameID(game->startup, neutrinoPath,
                   (neutrinoVmc.arg[0][0] ? 1 : 0) | (neutrinoVmc.arg[1][0] ? 2 : 0)); // Δ3: -mc-covered slots keep their card

    int neutrinoDevMode = host::app().pathToMode(neutrinoPath);
    char gameStartup[GAME_STARTUP_MAX + 1];
    snprintf(gameStartup, sizeof(gameStartup), "%s", game->startup);
    host::app().deinitEx(sbNeutrinoDeinitException(neutrinoPath), itemList->mode, neutrinoDevMode); // itemCleanUp frees the cached game lists

    // Hand off to Neutrino with the udpfs driver token. `partname` and `gameStartup` survive the deinit;
    // `game` does not and is not dereferenced past this point.
    sysLaunchNeutrino("udpfs", partname, gameStartup, compatmask, EnablePS2Logo, neutrinoPath, neutrinoExtraArgs, neutrinoVideo, neutrinoGsmComp, 0 /* udpfs is fileid, no fs layer */, -1, &neutrinoVmc);
}

config_set_t *UdpfsSource::config(item_list_t *itemList, int id)
{
    base_game_info_t *game = gUdpfsSupport.activeGame(itemList, id);

    if (game == &gUdpfsSupport.emptyGame_)
        return NULL;
    config_set_t *config = sbPopulateConfig(game, udpfsPrefix, "/");
    ps1Size(itemList, game, config);
    return config;
}

int UdpfsSource::image(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char path[256];

    if (isRelative) {
        const char *imageFolder = strcmp(folder, "ART") == 0 ? udpfsArtCaseNames[udpfsArtCaseIndex] : folder;
        snprintf(path, sizeof(path), "%s%s/%s_%s", udpfsPrefix, imageFolder, value, suffix);
    } else
        snprintf(path, sizeof(path), "%s%s_%s", folder, value, suffix);
    int r = texDiscoverArt(resultTex, path);
    // ART LIVES IN THE ART FOLDER, and nowhere else. A PS1 cover is
    // ART/<name>_COV.png -- the same rule as a PS2 title -- which the texDiscoverLoad
    // above already tried. A miss is simply "no cover": there is no second directory to
    // search, which also means a missing cover costs one failed open instead of several.
    return r;
}

int UdpfsSource::textId(item_list_t *itemList)
{
    return _STR_UDPFS_GAMES;
}

int UdpfsSource::iconId(item_list_t *itemList)
{
    return UDPFS_ICON;
}

// This may be called, even if udpfsInit() was not.
void UdpfsSource::cleanUp(item_list_t *itemList, int exception)
{
    if ((*udpfsInstance()).enabled) {
        LOG("UDPFSSUPPORT CleanUp\n");
        gUdpfsSupport.clearGameLists();
    }
    // SOCKET-ONCE: the ministack has no udp_unbind, so the module chain is intentionally NOT torn down
    // here (never DelDrv+re-AddDrv the UDPRDMA socket). udpfsIomanModLoaded stays set for the boot.
    (void)exception;
}

// This may be called, even if udpfsInit() was not.
void UdpfsSource::shutdown(item_list_t *itemList)
{
    if ((*udpfsInstance()).enabled) {
        LOG("UDPFSSUPPORT Shutdown\n");
        gUdpfsSupport.clearGameLists();
    }
    // SOCKET-ONCE: keep the ioman stack loaded (no unbind path). dev9 was init'd once in udpfsLoadModules
    // and is left to the shared dev9 refcount / final power-off.
}

int UdpfsSource::checkVmc(item_list_t *itemList, char *name, int createSize)
{
    return sysCheckVMC(udpfsPrefix, "/", name, createSize, NULL);
}

char *UdpfsSource::prefix(item_list_t *itemList)
{
    return udpfsPrefix;
}


int UdpfsSource::ps1SaveDir(item_list_t *itemList, int id, const char *name, int *ember, char *directory, int directorySize,
                            char *partition, int partitionSize)
{
    (void)partitionSize;
    if (!ps1RowKind(itemList, id, ember))
        return 0;
    partition[0] = '\0';
    return *ember && saveIconPs1Directory(udpfsPrefix, name, 1, directory, directorySize);
}

int UdpfsSource::saveCueSettings(item_list_t *itemList, int id, const char *name, config_set_t *configSet)
{
    (void)itemList;
    (void)id;
    return cueSaveGameSettings(udpfsPrefix, name, configSet);
}
