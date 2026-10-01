#ifndef __IOSUPPORT_H
#define __IOSUPPORT_H

#include "include/config.h"

#define IO_MODE_SELECTED_NONE      -1
#define IO_MODE_SELECTED_ALL       MODE_COUNT
#define IO_MODE_SELECTED_ALL_SPARE (MODE_COUNT + 1)

enum IO_MODES {
    BDM_MODE = 0,
    BDM_MODE1,
    BDM_MODE2,
    BDM_MODE3,
    BDM_MODE4,
    BDM_MODE5,
    BDM_MODE6,
    BDM_MODE7,
    ETH_MODE,
    HDD_MODE,
    APP_MODE,
    MMCE_MODE,  // reserved: MMCE support returns with checklist item 1
    FAV_MODE,   // reserved: Favourites returns with checklist item 33
    UDPFS_MODE, // reserved (item 6); appended last so existing mode values don't shift
    HTTP_MODE,  // games.csv catalog + byte-range ISO reads; appended for the same reason

    MODE_COUNT
};

#define BDM_MODE_LAST  BDM_MODE7
#define BDM_MODE_COUNT (BDM_MODE_LAST - BDM_MODE + 1)

enum START_MODE {
    START_MODE_DISABLED = 0,
    START_MODE_MANUAL,
    START_MODE_AUTO
};

enum ERROR_CODE {
    // Generic error codes
    ERROR_ETH_NOT_STARTED = 100,

    // Ethernet (module startup) error codes
    ERROR_ETH_MODULE_NETIF_FAILURE = 200,
    ERROR_ETH_MODULE_SMBMAN_FAILURE,
    // HDD (module startup) error codes
    ERROR_HDD_MODULE_ATAD_FAILURE = 220,
    ERROR_HDD_MODULE_HDD_FAILURE,
    ERROR_HDD_MODULE_PFS_FAILURE,

    // Ethernet (software) error codes
    ERROR_ETH_SMB_CONN = 300,
    ERROR_ETH_SMB_LOGON,
    ERROR_ETH_SMB_ECHO,
    ERROR_ETH_SMB_OPENSHARE,
    ERROR_ETH_SMB_LISTSHARES,
    ERROR_ETH_SMB_LISTGAMES,
    // Ethernet (hardware) error codes
    ERROR_ETH_LINK_FAIL = 310,
    ERROR_ETH_DHCP_FAIL,

    // HDD error codes
    ERROR_HDD_IF_NOT_DETECTED = 400,
    ERROR_HDD_NOT_DETECTED,
    ERROR_HDD_APA_TABLE_UNREADABLE, // 402: the drive answered; its APA table did not validate

    // BDM error codes
    ERROR_BDM_UNSUPPORTED_SECTOR_SIZE = 500, // a drive's sectors are not 512 bytes; BDM refused it
};

#define NO_EXCEPTION           0x00
#define UNMOUNT_EXCEPTION      0x01
// The child ELF keeps the IOP: it inherits the live modules, mounts and file descriptors instead of
// resetting them away. Set by the Ember launch paths, which hand off through sysLoadELFKeepIOP.
//
// UNMOUNT_EXCEPTION alone is not enough on APA. It stops the pfs0: UNMOUNT, but hddCleanUp also
// issues PDIOC_CLOSEALL, which drops every pfs descriptor in the IOP -- fine when the next thing to
// run resets the IOP anyway, fatal when it does not. A driver only OPL knows about is still the
// driver Ember has to read its game through.
#define KEEPIOP_EXCEPTION      0x02
// pfs1: is HDD support's transient scan/settings mount and must keep its historical always-unmount
// behaviour. Only an explicitly resolved child ELF on pfs1: may preserve it across the handoff.
#define KEEP_PFS1_EXCEPTION    0x04
// External loaders opened from PFS need the mount descriptors to survive until sysLoadELFKeepIOP
// reads the ELF, but unlike Ember/wLaunchELF they do NOT need OPL's IOP pad RPC left active.
// Keep this narrower than KEEPIOP_EXCEPTION so PFS-hosted Neutrino/POPSTARTER retain the same pad
// teardown as those loaders on every other backend.
#define KEEP_PFS_FDS_EXCEPTION 0x08

#define MODE_FLAG_NO_COMPAT  0x01 // no compat support
#define MODE_FLAG_COMPAT_DMA 0x02 // Supports DMA compat flags
#define MODE_FLAG_NO_UPDATE  0x04 // Retired updater capability bit; keep the flag value stable for existing support definitions.

#define COMPAT_MODE_1 0x01 // Accurate Reads
#define COMPAT_MODE_2 0x02 // Alternative data read method (Synchronous)
#define COMPAT_MODE_3 0x04 // Unhook Syscalls
#define COMPAT_MODE_4 0x08 // Skip Videos: Apply 0 (zero) file size to PSS videos and also skip Bink (.BIK) ones
#define COMPAT_MODE_5 0x10 // Emulate DVD-DL
#define COMPAT_MODE_6 0x20 // Disable IGR
#define COMPAT_MODE_7 0x40 // Unused
#define COMPAT_MODE_8 0x80 // Unused

#define COMPAT_MODE_COUNT 6 // only count modes in use

#define OPL_MOD_STORAGE 0x00097000 //(default) Address of the module storage region

// minimal inactive frames for cover display, can be pretty low since it means no button is pressed :)
#define MENU_MIN_INACTIVE_FRAMES 8

#define MENU_UPD_DELAY_NOUPDATE   -1 // Auto refresh is disabled for the item. The refresh button may be used to manually refresh the item.
#define MENU_UPD_DELAY_GENREFRESH 0  // The item will be refreshed every MENU_GENERAL_UPDATE_DELAY frames, regardless of whether automatic refresh is enabled or not.

typedef struct _item_list_t item_list_t;

typedef struct _item_list_t
{
    short int mode;

    /// Device priority when it comes to locating art assets for apps. Higher value = lower priority. (< 0) means no support for art assets.
    char appsPriority;

    char enabled;

    unsigned char flags;

    /// max inactive frame delay
    int delay;

    /// Amount of frame to wait, before refreshing this menu's list. Setting an invalid value (<0) means no automatic refresh.
    /// 0 = General refresh, which means that it will be refreshed every MENU_GENERAL_UPDATE_DELAY frames, regardless of whether automatic refresh is enabled or not.
    int updateDelay;

    // Per-device data
    void *priv;

    // opl_io_module_t instance that owns this item list.
    void *owner;

    /// Optional view override for a shallow proxy of this support. Zero keeps the support's native
    /// per-mode L3 state; Favourites uses the forced values so an ISO/VCD favourite can proxy its
    // source without mutating the real source page's independent view.
    unsigned char viewOverride;

    /// The GameSource object behind this list Copies of a list, such as Favourites' proxies
    /// and the BDM device slots, keep it.
    void *source;
} item_list_t;

#define ITEM_VIEW_NATIVE    0
#define ITEM_VIEW_FORCE_ISO 1
#define ITEM_VIEW_FORCE_PS1 2 // the PS1 list -- holds POPSTARTER *.VCD and Ember rows alike

#endif
