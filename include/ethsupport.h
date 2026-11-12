#ifndef __ETH_SUPPORT_H
#define __ETH_SUPPORT_H

#include <time.h>
#include "include/iosupport.h"
#include "include/supportbase.h" // base_game_info_t
#include "include/riptlite.h"    // LITE_HAS_NFS

constexpr int ETH_MODE_UPDATE_DELAY = 300;
constexpr int ETH_PS1_SCAN_RETRY_MAX = 15;

#include "include/mcemu.h"
typedef struct
{
    int active;       /* Activation flag */
    char fname[80];   /* File name (memorycard?.bin): worst-case = 31(prefix)+5(\\VMC\\)+31(name)+4(.bin)+1(NUL) = 72 bytes; rounded up to 80 */
    u16 fid;          /* SMB File ID */
    int flags;        /* Card flag */
    vmc_spec_t specs; /* Card specifications */
} smb_vmc_infos_t;

void ethDeinitModules(void);   // Module-only deinitialization, without the GUI's knowledge (for specific reasons, otherwise unused).
int ethLoadInitModules(void);  // Initializes Ethernet and applies configuration.
int ethGetModulesLoaded(void); // 1 if the SMB NIC stack is loaded (UDPBD must not load on top).
// The SMB dialect (enum SMB_DIALECT) of the browse driver now resident, or -1 when none is. The
// Network page compares it with gSMBDialect: smbman and smb2man cannot be swapped live.
int ethGetLoadedSmbDialect(void);
// The dialect that load asked for (gSMBDialect at the time), or -1 when no driver is resident. It
// differs from the loaded one only after an SMB2 -> SMBv1 fallback. The restart offer compares
// gSMBDialect with THIS, so a fallback is not mistaken for a pending change (which would offer the
// same failing restart forever and block the Network page's live reconnect).
int ethGetRequestedSmbDialect(void);
// True only after OPENSHARE succeeded and the active smb0: prefix is usable. This is deliberately
// stronger than "Network Protocol == SMB": callers must not turn a read-only import into a long
// connection attempt or consume stale saved values.
int ethIsSMBShareConnected(void);
// Explicit-loader helper: synchronously bring up SMB and open the configured share when no
// incompatible NIC stack is resident. Used only for a user-entered custom ELF path.
int ethEnsureSMBShareConnected(void);
// Queue a full live SMB reconnect: close the current session, apply the current Network Settings
// (link mode + IP/DHCP), log on/open the share again, and rebuild the list. Safe to call repeatedly;
// one request is coalesced while the IO worker is reconnecting.
void ethRequestReconnect(void);
// Explicit Refresh probes the session on the IO worker; healthy ART/BGM handles stay open.
void ethRequestCheckedRefresh(void);
int ethGetNetConfig(u8 *ip_address, u8 *netmask, u8 *gateway);
int ethApplyConfig(void);
int ethGetDHCPStatus(void);
item_list_t *ethGetObject(int initOnly);
// Resolve a stored ETH ISO favourite against the ISO backing view even while the live ETH page
// is showing VCDs. Read-only; never changes the visible ETH list or L3 view.
int ethResolveIsoFavourite(int id, const char *name, int *outId);
const char *ethGetSMBPrefix(void);
// 1 while the ETH page reads an NFS export (Network Protocol NFS) instead of an SMB share. NFS games
// run on either core, as SMB ones do (the OPL core's in-game reader is nfs_cdvdman). PS1 is Ember only: it
// keeps the live nfs: mount, while POPSTARTER resets the IOP and reaches only SMB.
int ethIsNfs(void);
// 1 while the ETH page is MODE_FLAG_READ_ONLY: NFS with NFS Writes Off, which keeps nothing (no VMC).
int ethIsReadOnly(void);
// 1 when the ETH page lists games: an NFS export, or a chosen SMB share. 0 while it is a share picker.
int ethShareChosen(void);

// The SMB-over-Ethernet session (src/sources/ethsupport.cpp): the smb0: base device and game prefix,
// the SMB filesystem-driver latch, the reconnect-in-flight flag and the ISO/PS1 backing lists the ETH
// page shows. One instance (gEthSupport); EthSource's virtuals and the ethXxx free functions reach
// the session state through it. The IO worker enters the session through the file-local trampolines
// (ioPutRequest takes a plain function pointer).
class EthSupport
{
public:
    constexpr EthSupport() = default;
    EthSupport(const EthSupport &) = delete;
    EthSupport &operator=(const EthSupport &) = delete;

    // Reference accessors: the EthSource page methods own the list-scan protocol and reset/publish
    // this state in place.
    char *prefix() { return prefix_; }
    char *base() const { return base_; }
    void setBase(const char *base) { base_ = const_cast<char *>(base); }
    int &ulSizePrev() { return ulSizePrev_; }
    time_t &modifiedCDPrev() { return modifiedCDPrev_; }
    time_t &modifiedDVDPrev() { return modifiedDVDPrev_; }
    int &gameCount() { return gameCount_; }
    base_game_info_t *&games() { return games_; }
    int &ps1GameCount() { return ps1GameCount_; }
    base_game_info_t *&ps1Games() { return ps1Games_; }
    int &ps1ScanFailed() { return ps1ScanFailed_; }
    int &ps1ScanRetries() { return ps1ScanRetries_; }

    // Session predicates and the public cross-module entry points (bodies; the ethXxx free
    // functions in ethsupport.cpp forward here).
    bool smbShareConnected() const;
    // The browse driver is nfsman (NFS) rather than smbman/smb2man. Latched with the driver.
    bool nfs() const { return LITE_HAS_NFS && nfs_; }
    bool shareChosen() const;
    // The SMB dialect the open share negotiated (0x0202 .. 0x0311), or 0 when unknown: no share
    // open, or the smbman (SMBv1) driver, which does not report one.
    int smbNegotiatedDialect() const { return smbNegotiatedDialect_; }
    int ensureShareConnected();
    void requestReconnect();
    void requestCheckedRefresh();
    int resolveIsoFavourite(int id, const char *name, int *outId);

    // The row lookup shared by the EthSource accessors.
    base_game_info_t *gameForView(item_list_t *itemList, int id);

    // Session lifecycle (the IO trampolines forward here).
    void selectDriver();
    bool protocolLive() const;
    void smbConnect();
    void nfsConnect();
    int smbDisconnect();
    void initSmb();
    void loadModules();
    void protocolTeardown();
    void displayErrorStatus();
    void reconnectSmb();
    void checkedRefreshSmb();

private:
    char prefix_[48] = {}; // Contains the full path to the folder where all the games are ("nfs:/NFS/" + 31 + "\\").
    char *base_ = NULL;
    int ulSizePrev_ = -2;
    time_t modifiedCDPrev_ = 0;
    time_t modifiedDVDPrev_ = 0;
    int gameCount_ = 0;
    base_game_info_t *games_ = NULL;
    int ps1GameCount_ = 0;
    base_game_info_t *ps1Games_ = NULL;
    int ps1ScanFailed_ = 0;
    int ps1ScanRetries_ = 0;
    unsigned char smbModuleLoaded_ = 0;
    unsigned char nfs_ = 0; // selectDriver(): which driver smbModuleLoaded_ is about
    int smbNegotiatedDialect_ = 0;
    unsigned char reconnectQueued_ = 0;
    unsigned char reconnectRequested_ = 0; // settings reconnect upgrades a pending checked Refresh
};

extern EthSupport gEthSupport;

#endif
