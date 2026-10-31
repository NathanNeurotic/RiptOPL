#ifndef __SYSTEM_H
#define __SYSTEM_H

#include "include/mcemu.h"

#define SYS_LOAD_MC_MODULES   0x01
#define SYS_LOAD_USB_MODULES  0x02
#define SYS_LOAD_ISOFS_MODULE 0x04

unsigned int USBA_crc32(const char *string);
int sysGetDiscID(char *discID);
// Takes a DEV9 reference even on failure; pair with sysShutdownDev9. Returns 1 when usable.
int sysInitDev9(void);
// PC CARD CF owns DEV9 exclusively: claim only while DEV9 holds no reference.
int sysTryClaimPccf(void);
int sysIsPccfOwned(void);
void sysReleasePccf(void);
void sysShutdownDev9(void);
void sysReset(int modload_mask);
void sysExecExit(void);
// Boot the physical PS2 disc in the drive; <0 (stays in OPL) on failure. `progress` is called once
// per poll while waiting on the drive -- pass guiRenderProbeFrame from the GUI thread so the menu
// keeps drawing, or NULL to sleep instead. Bounded by SYS_DISC_DETECT_MS + SYS_DISC_REDETECT_MS.
// A progress callback that returns non-zero ABANDONS the wait and yields SYS_DISC_CANCELLED, so a
// slow or unresponsive drive can always be escaped by the user rather than only by the budget.
#define SYS_DISC_CANCELLED (-4)
int sysLaunchDisc(int (*progress)(void));
int sysGetDiscBootPath(char *path, int pathSize, int (*progress)(void));

#define NEUTRINO_PATH     "mc0:NEUTRINO/neutrino.elf"
#define NEUTRINO_ALT_PATH "mc1:NEUTRINO/neutrino.elf"

#define POPS_FOLDER "POPS" // per-device PS1/POPSTARTER folder: <dev>/POPS/POPSTARTER.ELF + *.VCD

#define NEUTRINO_VMC_SLOTS 2

// Fully-formed Neutrino "-mcN=<prefix>VMC/<name>.bin" args, one per VMC slot, resolved by the
// device support layer BEFORE deinit frees the per-game config. Carried into sysLaunchNeutrino
// and emitted as DISCRETE argv[] entries -- never whitespace-tokenized -- so a VMC whose name
// contains a space survives intact.
typedef struct neutrino_vmc_args
{
    // "-mcN=<prefix>VMC/<name>.bin"; "" => slot unconfigured / skipped.
    char arg[NEUTRINO_VMC_SLOTS][160];
} neutrino_vmc_args_t;

// neutrinoBsdfs: per-game -bsdfs override, 0=Auto (per-device default), 1=exfat, 2=hdl, 3=bd.
void sysLaunchNeutrino(const char *driver, const char *path, const char *startup, int compatmask, int EnablePS2Logo, const char *neutrinoPath, const char *extraArgs, int neutrinoVideo, int neutrinoGsmComp, int neutrinoBsdfs, int bdDevNr, const neutrino_vmc_args_t *vmcArgs);

// Pre-deinit launch pre-flight: validates the driver token (and a -bsdfs=bd device number) while
// every mount is up and the GUI can still toast. Call BEFORE deinit in every Neutrino leg, with the
// same neutrinoBsdfs/extraArgs/bdDevNr it will pass to sysLaunchNeutrino. Returns 0 = proceed;
// <0 = abort the launch (a toast has already been shown).
int sysNeutrinoPreflight(const char *driver, const char *neutrinoPath, int neutrinoBsdfs, const char *extraArgs, int bdDevNr);

// Neutrino over SMB (-bsd=smb, ps2max32/neutrino PR #236 by Ronnie Sahlberg): smb_ioman.irx and
// smb_fhi.irx take the network and the login as module arguments in <neutrino>/config/bsd-smb.toml,
// so the launch writes this game's values there first. The limits are the driver's: user and pass
// are cut at 31 characters, share at 63, srv is a dotted address (no name lookup) and the port is 445.
#define NEUTRINO_SMB_USER_MAX  31
#define NEUTRINO_SMB_PASS_MAX  31
#define NEUTRINO_SMB_SHARE_MAX 63
#define NEUTRINO_SMB_PATH_MAX  127 // FHI_SMB_PATH_MAX (128) with its NUL: the path on the share
#define NEUTRINO_SMB_PORT      445
typedef struct neutrino_smb_args
{
    unsigned char ip[4], mask[4], gw[4], srv[4];
    char user[NEUTRINO_SMB_USER_MAX + 1];
    char pass[NEUTRINO_SMB_PASS_MAX + 1];
    char share[NEUTRINO_SMB_SHARE_MAX + 1];
} neutrino_smb_args_t;
// Before teardown. 0 = bsd-smb.toml holds these values; -1 = this Neutrino has no SMB support
// (no config/bsd-smb.toml, or no modules/smb_ioman.irx / smb_fhi.irx); -2 = bsd-smb.toml is not
// in the stock shape, or could not be written. No toast: the SMB leg falls back to the OPL core.
int sysNeutrinoSmbPreflight(const char *neutrinoPath, const neutrino_smb_args_t *args);

// Compose the exact launch argv while the GUI is still live. Refuses a launch that would exceed
// the 14-entry or 256-byte ExecPS2 budget, with a visible warning instead of dropping arguments.
// Neutrino cheats: write `pairs` address/value code pairs where Neutrino resolves -cfg=riptopl-cheats and
// add that flag to its argv. Every call clears the last hand-over first; NULL startup or no pairs hands
// over nothing. 0 = launch; <0 = the user chose to stay in the menu. Callers go through
// sbNeutrinoHandCheats, which reads the codes from the cheat engine.
int sysNeutrinoHandCheats(const char *startup, const char *neutrinoPath, const char *extraArgs, const u32 *list, int pairs);
#ifdef RETROACHIEVEMENTS
/* RA through the RA kit's ORBIT Neutrino (sbNeutrinoPrepareRA decides). 1 when the effective -cwd holds
   that runtime. HandRA writes the watch file and ript-ra.toml beside the cheats file and arms
   -cfg=ript-ra -ra=... for startup's launch: 0 = armed, -1 = could not; NULL startup only disarms. */
int sysNeutrinoRARuntime(const char *neutrinoPath, const char *extraArgs);
int sysNeutrinoHandRA(const char *startup, const char *neutrinoPath, const char *extraArgs, int networkGame,
                      const unsigned char *ip, const unsigned char *peer, const void *watch, int size);
#endif
int sysNeutrinoArgsPreflight(const char *driver, const char *path, const char *startup, int compatmask, int EnablePS2Logo, const char *neutrinoPath, const char *extraArgs, int neutrinoVideo, int neutrinoGsmComp, int neutrinoBsdfs, int bdDevNr, const neutrino_vmc_args_t *vmcArgs);

// Launch an external POPSTARTER.ELF for a PS1 VCD. selector = the target's argv[0]
// "<POPS>/<prefix><name>.ELF" token. Caller deinit()s with UNMOUNT_EXCEPTION first.
void sysLaunchPopstarter(const char *popstarterElf, const char *selector);

// Launch an external ember.elf for a PS1 CUE title (the second PS1 core; see include/cuesupport.h).
//   emberElf   full path of ember.elf. It is ALSO the target's argv[0], and that is load-bearing:
//              ps2sdk's __init_cwd derives Ember's working directory from argv[0], so everything
//              Ember opens -- bios.bin, settings.txt, games/ -- resolves relative to it.
//   gameFolder the BARE game-folder name under <emberdir>/games/. Ember refuses '/', ':' and '\\'
//              anywhere in it, so validate with cueNameLaunchable() first. NULL or "" passes no
//              game argument at all, which is Ember's own default: it then looks for games/default
//              and otherwise runs the PS1 BIOS shell.
// Caller deinit()s with UNMOUNT_EXCEPTION first and MUST leave the game's device mounted -- Ember
// performs no IOP reset of its own and cannot bring a device back up.
void sysLaunchEmber(const char *emberElf, const char *gameFolder);

// Ordinary APPS variant. cleanupHdd requests the wLaunchELF_R3Z post-load PFS/HDD/DEV9 teardown;
// dedicated Ember/Neutrino/POPSTARTER paths keep using their existing handoff functions.
int sysLoadELFApp(const char *filename, const char *partition, int argc, char *argv[], int resetIop, int cleanupHdd);
int sysLoadELFKeepIOP(const char *filename, const char *partition, int argc, char *argv[]);
void sysPowerOff(void);
#ifdef __DECI2_DEBUG
int sysInitDECI2(void);
#endif

void sysLaunchLoaderElf(const char *filename, const char *mode_str, int size_cdvdman_irx, void **cdvdman_irx, int size_mcemu_irx, void **mcemu_irx, int EnablePS2Logo, unsigned int compatflags);

int sysLoadModuleBuffer(void *buffer, int size, int argc, char *argv);
int sysCheckMC(void);
/* Existing images require explicit, confirmed Delete before replacement. */
#define SYS_VMC_REPLACE_REQUIRES_DELETE (-2)
#define SYS_VMC_CREATE_FAILED           (-3)
/* Read-only automatic-assignment probe: require a complete format header and
 * matching geometry. Valid non-integral MB sizes return 0 (Neutrino only);
 * invalid opened images return SYS_VMC_CREATE_FAILED. An open failure still
 * requires the creation path's directory proof of absence before any job. */
#define SYS_VMC_PROBE_FORMAT            (-4)
int sysCheckVMC(const char *prefix, const char *sep, char *name, int createSize, vmc_superblock_t *vmc_superblock);
// sysCheckVMC with genvmc creation flags (VMC_CREATE_*).
int sysCheckVMCEx(const char *prefix, const char *sep, char *name, int createSize, vmc_superblock_t *vmc_superblock, int createFlags);
// Layout of the VMC whose creation was last STARTED: 1 contiguous, 0 fragmented, -1 unknown.
// Only meaningful once genvmc reports the job finished. -1 means "this backing store cannot say"
// (mmce, APA/pfs, SMB, udpfs) and must be treated as silence -- never as a pass or a failure.
int sysVMCContiguity(void);
// Path of the VMC whose creation was last started, or "" when none.
const char *sysVMCCreatePath(void);

#endif
