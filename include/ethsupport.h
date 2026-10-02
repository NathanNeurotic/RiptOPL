#ifndef __ETH_SUPPORT_H
#define __ETH_SUPPORT_H

#include <time.h>
#include "include/iosupport.h"
#include "include/supportbase.h" // base_game_info_t

constexpr int ETH_MODE_UPDATE_DELAY = 300;

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
int ethGetNetConfig(u8 *ip_address, u8 *netmask, u8 *gateway);
int ethApplyConfig(void);
int ethGetDHCPStatus(void);
item_list_t *ethGetObject(int initOnly);
// Resolve a stored ETH ISO favourite against the ISO backing view even while the live ETH page
// is showing VCDs. Read-only; never changes the visible ETH list or L3 view.
int ethResolveIsoFavourite(int id, const char *name, int *outId);
const char *ethGetSMBPrefix(void);

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

    // Session predicates and the public cross-module entry points (bodies; the ethXxx free
    // functions in ethsupport.cpp forward here).
    bool smbShareConnected() const;
    int ensureShareConnected();
    void requestReconnect();
    int resolveIsoFavourite(int id, const char *name, int *outId);

    // The row lookup shared by the EthSource accessors.
    base_game_info_t *gameForView(item_list_t *itemList, int id);

    // Session lifecycle (the IO trampolines forward here).
    void smbConnect();
    int smbDisconnect();
    void initSmb();
    void loadModules();
    void protocolTeardown();
    void displayErrorStatus();
    void reconnectSmb();

private:
    char prefix_[40] = {}; // Contains the full path to the folder where all the games are.
    char *base_ = NULL;
    int ulSizePrev_ = -2;
    time_t modifiedCDPrev_ = 0;
    time_t modifiedDVDPrev_ = 0;
    int gameCount_ = 0;
    base_game_info_t *games_ = NULL;
    int ps1GameCount_ = 0;
    base_game_info_t *ps1Games_ = NULL;
    unsigned char smbModuleLoaded_ = 0;
    unsigned char reconnectQueued_ = 0;
};

extern EthSupport gEthSupport;

#endif
