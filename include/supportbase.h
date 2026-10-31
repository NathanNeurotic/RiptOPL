#ifndef __SUPPORT_BASE_H
#define __SUPPORT_BASE_H

#include "include/system.h" // neutrino_vmc_args_t

#define UL_GAME_NAME_MAX       32
#define ISO_GAME_NAME_MAX      160
#define ISO_GAME_EXTENSION_MAX 4
#define GAME_STARTUP_MAX       12

#define ISO_GAME_FNAME_MAX (ISO_GAME_NAME_MAX + ISO_GAME_EXTENSION_MAX)

enum GAME_FORMAT {
    GAME_FORMAT_USBLD = 0,
    GAME_FORMAT_OLD_ISO,
    GAME_FORMAT_ISO,
    // A browsable subdirectory row (folder navigation, opt-in). Never launched, never written to the
    // games.bin cache and never stored as a favourite; the dispatch intercepts it to descend instead.
    GAME_FORMAT_FOLDER,
};

typedef struct
{
    char name[ISO_GAME_NAME_MAX + 1]; // MUST be the higher value from UL / ISO
    char startup[GAME_STARTUP_MAX + 1];
    char extension[ISO_GAME_EXTENSION_MAX + 1];
    u8 parts;
    u8 media;
    u8 format;
    u32 sizeMB;
} base_game_info_t;

typedef struct
{
    char name[UL_GAME_NAME_MAX];    // it is not a string but character array, terminating NULL is not necessary
    char magic[3];                  // magic string "ul."
    char startup[GAME_STARTUP_MAX]; // it is not a string but character array, terminating NULL is not necessary
    u8 parts;                       // slice count
    u8 media;                       // Disc type
    u8 unknown[4];                  // Always zero
    u8 Byte08;                      // Always 0x08
    u8 unknown2[10];                // Always zero
} USBExtreme_game_entry_t;

int isValidIsoName(char *name, int *pNameLen);
int sbIsSameSize(const char *prefix, int prevSize);
int sbCreateSemaphore(void);
// sub = current browse subpath below CD/DVD ("" at root); NULL opts the caller OUT of folder rows
// (ETH/SMB). On a TOTAL device-read failure the caller's list is left untouched (last-good list);
// GAME_FORMAT_FOLDER rows, and the ul.cfg (USBLD) leg -- a device-root-only concept -- is skipped
// inside subfolders.
// Pass fsize=NULL for ISO-only scanning, without probing or updating UL-size state.
int sbReadList(base_game_info_t **list, const char *prefix, const char *sub, int *fsize, int *gamecount);
extern int sbListCacheReadOnly; // set around sbReadList() for a read-only device: its games.bin is not written
// errno of the last CD/DVD folder the latest sbReadList could not open (0 = both opened).
int sbGetReadListError(void);
// Folder browsing: set the active subpath the path composers inject (see sbBrowseSub in supportbase.c).
void sbSetBrowseSub(const char *sub);
int sbCheatsMissingContinue(void *pCommon, int cheatResult);
int sbLoadImage(const char *path, const char *file);
void sbSetDiscAttributes(config_set_t *config, int isPS1, int isCD); // #System/#Media/#DiscType identity stamp

// Compatibility modes for a game: its saved $Compatibility key, else a known title default. Every reader of
// the modes (launch paths and the per-game screen) must use sbGetCompatModes so they agree. A title default
// can be limited to one core, so pass neutrino=1 for a Neutrino launch and 0 for the OPL core.
int sbTitleCompatDefault(config_set_t *configSet, int neutrino);
int sbGetCompatModes(config_set_t *configSet, int neutrino);

void sbEnsureIgrUsbDrivers(int compatmask);
int sbPrepare(base_game_info_t *game, config_set_t *configSet, int size_cdvdman, void **cdvdman_irx, int *patchindex);
void sbUnprepare(void *pCommon);
void sbCreatePath(const base_game_info_t *game, char *path, const char *prefix, const char *sep, int part);
void sbDelete(base_game_info_t **list, const char *prefix, const char *sep, int gamecount, int id);
void sbRename(base_game_info_t **list, const char *prefix, const char *sep, int gamecount, int id, char *newname);
config_set_t *sbPopulateConfig(base_game_info_t *game, const char *prefix, const char *sep);
// As sbPopulateConfig, but the game's CFG is kept in cfgDir (ending in a separator) when cfgDir is set: the
// game's device is read-only. With nothing saved there yet, the CFG on the device is read and saved to cfgDir.
config_set_t *sbPopulateConfigIn(base_game_info_t *game, const char *prefix, const char *sep, const char *cfgDir);
// Gate for sbPopulateConfig's per-game size stat. OFF while scrolling the game list -- over SMB a
// fresh stat() of an ISO can cost seconds, and the main page only needs the metadata-derived badges
// (#DiscType/#Media/#Format), never #Size. The info screen flips it on via menuRequestInfoSize() so
// #Size still resolves on demand. (1 = stat, 0 = skip.)
void sbSetConfigStatSize(int enable);
int sbConfigStatSizeEnabled(void);
// Info-screen #Size of a PS1 row (VCD or Ember folder) under the device's PS1 root.
void sbResolvePs1Size(base_game_info_t *game, const char *ps1Root, config_set_t *config);
void sbMakeDirTree(const char *path);
void sbCreateFolders(const char *path, int createDiscImgFolders);

// ISO9660 filesystem management functions.
u32 sbGetISO9660MaxLBA(const char *path);
// Real sector count of an image (upstream OPL #1763): the ZSO header's block count, else totalBytes / 2048.
// 0 = unknown (unopenable, an unreadable ZSO header, or totalBytes 0), which keeps CDVDMAN on the PVD.
u32 sbGetMediaLsnCount(const char *path, u64 totalBytes);
int sbProbeISO9660(const char *path, base_game_info_t *game, u32 layer1_offset);

int sbLoadCheats(const char *path, const char *file, config_set_t *configSet);
// The cheats of a Neutrino launch whose leg skips the native preparation (BDM, APA): this game's cheat
// settings, its .cht from <prefix>CHT/ (Select mode: remembered picks, then the picker), then
// sbNeutrinoHandCheats. 0 = launch; <0 = the user chose to stay in the menu.
// The enabled codes of the cheats loaded for `startup`, handed to Neutrino (sysNeutrinoHandCheats). With
// cheats off or a NULL startup it only clears the last hand-over. 0 = launch; <0 = stay in the menu.
int sbNeutrinoHandCheats(const char *startup, const char *neutrinoPath, const char *extraArgs);
int sbNeutrinoLoadCheats(const char *prefix, const char *startup, config_set_t *configSet, const char *neutrinoPath, const char *extraArgs);
#ifdef RETROACHIEVEMENTS
// Hand the game's watch list to the RA kit's ORBIT Neutrino (sysNeutrinoHandRA), or say the launch goes
// untracked. Before sysNeutrinoArgsPreflight and the teardown; never refuses the launch.
void sbNeutrinoPrepareRA(const char *prefix, const char *startup, const char *driver, const char *neutrinoPath, const char *extraArgs);
#endif



// Resolve one user-entered full ELF path into the filesystem path RiptOPL can actually open.
// This activates the explicitly named storage stack when safe (USB/MX4SIO/iLink/exFAT BDM,
// MMCE, SMB, UDPFS, UDPBD/UDPFS-BD, APA/PFS) and normalizes aliases to live namespaces such as
// massN:/ or smb0:. Returns 1 only when the resolved file can be opened.
int sbResolveCustomLoaderPath(const char *requested, char *out, int outSize);

// First usable Neutrino core ELF, or NULL. Runtime order is fixed:
// custom gNeutrinoPath -> active game's device (activePrefix) -> mc0/mc1.
// A Memory Card / HDD (APA) choice migrated from the retired picker also gets that picker's
// case-tolerant probe until the migrated path is edited (see configReadNeutrinoGlobals).
const char *sbResolveNeutrinoPath(const char *activePrefix);

// Deinit exception mask for an external ELF that is opened AFTER OPL's teardown. Every loader path
// keeps its mount; PFS additionally keeps the IOP-side descriptors because hddCleanUp's
// PDIOC_CLOSEALL would otherwise invalidate the mount before the ELF loader opens it.
int sbLoaderDeinitException(const char *loaderPath);
// Backward-compatible Neutrino-named wrapper used by existing launch legs.
int sbNeutrinoDeinitException(const char *neutrinoPath);

// Structured view of the USER-settable Neutrino launch flags (the catch-all "Launch Args" box).
typedef struct
{
    int qb;           // -qb (quick-boot)
    int dbc;          // -dbc (debug colors)
    int logo;         // -logo (PS2 logo)
    char cwd[256];    // -cwd=
    char cfg[256];    // -cfg=
    char elf[256];    // -elf=
    char ata0[256];   // -ata0=
    char ata0id[256]; // -ata0id=
    char ata1[256];   // -ata1=
    char extra[256];  // unrecognised/free tokens, space-joined; "--b ..." preserved at the tail
} neutrino_args_t;
// Parse an args string into the struct; assemble it back in a Neutrino-accepted order (--b last).
void neutrinoArgsParse(const char *in, neutrino_args_t *na);
int neutrinoArgsAssemble(const neutrino_args_t *na, char *out, int outSize); // 0 if the result would be truncated

// Fully-formed Neutrino -mcN VMC args for both slots, resolved from the per-game config
// BEFORE deinit frees it. vmcPrefix = the device prefix VMC/ lives under.
void sbBuildVmcNeutrinoArgs(config_set_t *configSet, const char *vmcPrefix, neutrino_vmc_args_t *vmcArgs);

#ifdef RETROACHIEVEMENTS
// RA: load this game's watch list from <path>RA/<file>.wl. Mirrors sbLoadCheats'
// shape (256-byte path, both extension cases). A missing list is not an error.
int sbLoadWatchList(const char *path, const char *file);
/* The same, without raising the network (an external runtime brings its own). */
int sbReadWatchList(const char *path, const char *file);
// What the last of those read from <path>RA/<file>.ach, for the in-game menu: the locked achievements packed in its
// lines (rahunt.h), *count of them in *bytes, of *total with *unlocked unlocked; NULL when there was no list, or it
// was for another file than this one.
const unsigned char *sbGetRaHunt(const char *file, int *count, int *bytes, int *total, int *unlocked);

// RA: the hash log, <path>RA/hashes.txt. Open before hashing, close after:
// raHashStep and ranet.c drop their crumbs into whatever log is open.
void raHashLogOpen(const char *path);
void raHashStep(const char *what);
void raHashLogAdd(const char *name, const char *startup, const char *hash);
void raHashLogClose(void);

int sbHashGameBusy(void);
int sbHashGameDeferred(const char *path, const char *name, const char *ext, const char *startup, int format);
int sbHashHdlDeferred(const char *path, const char *name, const char *startup, unsigned int start_sector);

// RA: ask the PC client to answer, from the I/O thread; the result is shown
// as a notice.
void sbTestPCLinkDeferred(void);
#endif

// Neutrino has no pad emulation: tell the user when this game has it on (no-op without PADEMU).
void sysNeutrinoWarnPadEmu(config_set_t *configSet);

#endif
