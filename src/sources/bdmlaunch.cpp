#include "include/host.h"
#include "include/sema_lock.h"
#include "include/opl.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/supportbase.h"
#include "include/bdmsupport.h"
#include "include/bdmint.h"
#include "include/vcdsupport.h"
#include "include/cuesupport.h" // PS1 rows can belong to either core; the ROW decides
#include "include/libview.h"    // libViewActive / libListViewActive -- which list this page shows
#include "include/folderbrowse.h"
#include "include/util.h"
#include "include/themes.h"
#include "include/textures.h"
#include "include/texcache.h"
#include "include/ioman.h"
#include "include/launchdiag.h" // UDPBD hang-triage stage markers (inert unless a udp launch arms them)
#include "include/system.h"
#include "include/nativevmc.h"
#include "include/ethsupport.h"   // ethGetModulesLoaded() for the UDPBD<->SMB NIC interlock
#include "include/udpfssupport.h" // udpfsGetModulesLoaded() -- symmetric backstop vs the udpfs_ioman filesystem stack
#include "include/mmcesupport.h"  // mmceSendGameID() cross-device game-id (#261)
#include "include/extern_irx.h"
#include "include/cheatman.h"
#include "include/sound.h"
#include "include/bdmevent.h"
#include "modules/iopcore/common/cdvd_config.h"
#include "include/bdmlauncher.h"
#include "include/neutrinolauncher.h"
#include "include/rapopslaunch.h"

#include <usbhdfsd-common.h>

#include "include/ps2sdkapi_compat.h"
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioIoctl, fileXioDevctl
#include <delaythread.h>
#include <sys/stat.h> // mkdir() -- create the config folder before a BDM save probe

// BDM launch paths: the ISO/ZSO/UL native-core launch plus the PS1 VCD/CUE launches (BdmLauncher),
// and the Δ8 lean Neutrino path (NeutrinoLauncher). Split out of bdmsupport.cpp. The public entry
// points stay free functions -- BdmSource and the autolaunch flow call them directly, and
// .github/scripts/test_autolaunch_settings.py extracts bdmLaunchGame's body by name -- forwarding
// to these single instances.
BdmLauncher gBdmLauncher;
NeutrinoLauncher gNeutrinoLauncher;

// Launch an Ember (.cue) PS1 title BY NAME -- the view-independent entry point, peer of
// launchVcd below. `cueName` is the game FOLDER under EMBER/games/, which is also Ember's launch
// argument and the row's identity for art and per-game config.
//
// Notice how much SHORTER this is than the POPSTARTER path below, and why: every one of that
// function's memory-card steps -- installing MC externals, equipping a BDMAssault exFAT driver
// pair, the fat32/exFAT prompt -- exists because POPSTARTER resets the IOP and then reloads its
// block driver from mc?:/POPSTARTER/. Ember performs no reset and inherits our live driver stack,
// so none of that applies to it. Do not port any of it here.
void BdmLauncher::launchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    char ps1Prefix[64], emberElf[256], biosPath[288];
    char launchName[CUE_NAME_MAX];

    if (pDeviceData == NULL || cueName == NULL || cueName[0] == '\0')
        return;

    // Refuse what Ember itself would refuse, while a dialog can still be drawn. The scanner already
    // filters these out, so reaching this is either a favourite stored before a rename or a
    // hand-edited entry -- both worth a message rather than a drop to the PS1 BIOS shell.
    if (!cueNameLaunchable(cueName)) {
        host::ui().msgBox(_l(_STR_EMBER_BAD_NAME), 0, NULL);
        return;
    }

    bdmBuildPs1Prefix(ps1Prefix, sizeof(ps1Prefix), itemList->mode); // device ROOT: EMBER/ lives there

    if (!cueResolveEmber(ps1Prefix, emberElf, sizeof(emberElf))) {
        host::ui().missingFile(_l(_STR_EMBER_NOT_FOUND), emberElf);
        return;
    }
    // Ember needs a user-supplied bios.bin beside it and says nothing useful when it is missing --
    // it just runs the PS1 BIOS shell. Check while we can still explain.
    if (!cueResolveEmberBios(ps1Prefix, biosPath, sizeof(biosPath))) {
        host::ui().missingFile(_l(_STR_EMBER_BIOS_MISSING), biosPath);
        return;
    }
    // Copies the argument (folder, or the disc picked from discs.txt): cleanup frees the PS1 array.
    if (!cuePrepareLaunch(ps1Prefix, cueName, emberElf, launchName, sizeof(launchName)))
        return;
    // The scan lists folders without reading inside them -- that would be a directory read per row
    // on every refresh. Pay for it once, HERE, while a dialog can still be drawn: an empty or
    // mis-filled folder otherwise drops the user into the PS1 BIOS shell with no explanation.
    // Leave Ember's display marker before the handoff, like the BDMA equip does for POPSTARTER.
    // Best-effort: never a launch gate.
    cueApplySettings(ps1Prefix, cueName, configSet);

    if (!cueGameHasImage(ps1Prefix, cueName)) {
        host::ui().msgBox(_l(_STR_EMBER_NO_DISC), 0, NULL);
        return;
    }

    // MX4SIO shares the SIO2 bus with MMCE artwork; quiesce it before the handoff reads the device.
    if (pDeviceData->bdmDeviceType == BDM_TYPE_SDC) {
        if (!cacheAbortMmceImageLoadsTimed(500)) {
            host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
            return;
        }
    }

    // Launch metadata, not Ember identity: resolve the GameID from the real image while the device
    // and the GUI are still live. A miss never blocks the launch.
    cuePrepareRetroGemBarcode(ps1Prefix, cueName);

    // UNMOUNT_EXCEPTION is load-bearing here, not defensive: Ember never resets the IOP, so the
    // device it reads its game from must still be mounted when it starts.
    host::app().deinit(UNMOUNT_EXCEPTION, itemList->mode);
    sysLaunchEmber(emberElf, launchName);
}

void bdmLaunchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet)
{
    gBdmLauncher.launchCue(itemList, cueName, configSet);
}

// Launch a PS1/.VCD entry BY NAME via POPSTARTER (the view-independent entry point used by both the
// in-view menu launch below and the Favourites tab). Every BDM device is a massN: mount at runtime,
// so the ELF + selector both use the live prefix; UNMOUNT_EXCEPTION keeps it mounted across the reset.
void BdmLauncher::launchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    char vcdPrefix[64], vcdElf[256], vcdSelector[320];

    if (pDeviceData == NULL || vcdName == NULL || vcdName[0] == '\0' || !strcasecmp(vcdName, "POPSTARTER")) // reserved-name belt: the scanner no longer lists it (#154); strcasecmp -- FAT is case-insensitive
        return;
    // POPSTARTER does its own IOP reset and reloads its block drivers from the MC -- it can't bring up
    // the network (udp) block device (no BDMA variant for it), so a PS1 VCD on the udp device would just
    // drop to OSDSYS. Abort cleanly with a message instead of a dead launch.
    if (pDeviceData->bdmDeviceType == BDM_TYPE_UDPBD) {
        host::ui().msgBox(_l(_STR_VCD_NOT_ON_NET), 0, NULL);
        return;
    }
    bdmBuildPs1Prefix(vcdPrefix, sizeof(vcdPrefix), itemList->mode); // device root, NOT gBDMPrefix -- POPSTARTER reads <root>/POPS only
    if (!vcdResolvePopstarter(vcdPrefix, vcdElf, sizeof(vcdElf))) {
        vcdDescribePopstarterLookup(vcdPrefix, vcdElf, sizeof(vcdElf));
        host::ui().missingFile(_l(_STR_POPSTARTER_NOT_FOUND), vcdElf);
        return;
    }
    if (!vcdConfirmPopsFiles(vcdPrefix)) // before the card is prepared: a missing emulator is a black screen
        return;
    // vcdBuildSelector emits the BARE POPSTARTER label ("mass:/POPS/XX.<name>.ELF"), NOT this live
    // unit-numbered mount -- POPSTARTER remounts under "mass:" after its own IOP reset (see the function).
    vcdBuildSelector(vcdPrefix, VCD_PREFIX_MASS, vcdName, vcdSelector, sizeof(vcdSelector));

    // ASK FIRST. Nothing above this point has written to the card or put anything on screen, and
    // nothing below it asks the user a question -- which is the ordering Nathan asked for: the
    // fat32/exFAT decision comes before every visible and every expensive step, and the RetroGEM
    // barcode is the very last thing that happens before the handoff.
    //
    // It used to sit after vcdInstallPopstarterMc, which populates mc?:/POPSTARTER and is the
    // "Preparing PS1 driver on memory card..." step -- many seconds of card writes on hardware. So
    // pressing Back at the dialog meant OPL had already spent all of that on a launch the user then
    // cancelled, and the question arrived after the wait rather than before it.
    //
    // USB is special: the PS2 cannot detect whether the stick is fat32 or exFAT formatted, so the
    // POPSTARTER USB driver has to be CHOSEN rather than detected. fat32 = POPSTARTER's built-in USB
    // stack (recommended for non-exFAT users); exFAT = the BDMAssault usbexfat pair.
    //
    // Asking on EVERY USB VCD launch was the original directive (2026-08-01) and remains the shipped
    // default, because a wrong answer here is a failed boot rather than a wrong setting. It is now the
    // ASK value of a three-way preference (BDMA Settings -> USB BDMA Driver) rather than the only
    // behaviour: a user whose sticks are all one format was being asked a question with the same
    // answer every single time.
    //
    // Back still cancels the LAUNCH, and only in ASK mode -- a pinned choice has no dialog to cancel.
    // USB VCDs only; the MX4SIO, iLink and ATA cases equip by device type and never ask.
    int usbBdmaVariant = -1;
    if (pDeviceData->bdmDeviceType == BDM_TYPE_USB) {
        if (gVcdUsbBdmaMode == VCD_USB_BDMA_EXFAT) {
            usbBdmaVariant = VCD_BDMA_USBEXFAT;
        } else if (gVcdUsbBdmaMode == VCD_USB_BDMA_FAT32) {
            usbBdmaVariant = VCD_BDMA_FAT32;
        } else {
            int usbMode = host::ui().showVcdUsbMode();
            if (usbMode == VCDUSB_BTN_FAT32)
                usbBdmaVariant = VCD_BDMA_FAT32;
            else if (usbMode == VCDUSB_BTN_EXFAT)
                usbBdmaVariant = VCD_BDMA_USBEXFAT;
            else
                return; // dialog cancelled -- nothing written, nothing prepared, nothing displayed
        }
    }

    // Present preparation indicator before potentially expensive memory-card file IO
    host::ui().renderTextScreen(_l(_STR_PLEASE_WAIT));

    // MMCE artwork must be quiesced before VCD card I/O because MMCE and MX4SIO share the SIO2/fileXio path.
    if (pDeviceData->bdmDeviceType == BDM_TYPE_SDC) {
        if (!cacheAbortMmceImageLoadsTimed(500)) {
            host::ui().warning(_l(_STR_ERR_FILE_INVALID), 8);
            return;
        }
    }

    // Populate mc?:/POPSTARTER from this game's own direct POPS/ folder. Best effort: local VCD
    // launches keep their existing handoff behavior when optional files are absent or the card is full.
    (void)vcdInstallPopstarterMc(vcdPrefix);

    // Best-effort card prep: POPSTARTER reloads its block-device driver pair from the MC after its OWN
    // IOP reset, so equip the device-matching BDMAssault variant. NEVER a launch gate -- the handoff
    // below always proceeds (POPSTARTER owns everything past the exec); a failed equip just toasts its
    // diagnostic in passing. UDPBD/unknown have no BDMA variant and are left as-is.
    switch (pDeviceData->bdmDeviceType) {
        case BDM_TYPE_USB:
            vcdApplyUsbModeForLaunch(usbBdmaVariant); // decided above, before anything was written
            break;
        case BDM_TYPE_SDC:
            vcdEnsureBdmaForLaunch(VCD_BDMA_SRC_MX4SIO, VCD_BDMA_MX4SIO);
            break;
        case BDM_TYPE_ILINK:
            vcdEnsureBdmaForLaunch(VCD_BDMA_SRC_ILINK, VCD_BDMA_ILINK);
            break;
        case BDM_TYPE_ATA:
            vcdEnsureBdmaForLaunch(VCD_BDMA_SRC_HDD, VCD_BDMA_ATA);
            break;
        default:
            break;
    }

    char vcdFullPath[256];
    snprintf(vcdFullPath, sizeof(vcdFullPath), "%sPOPS/%s.VCD", vcdPrefix, vcdName);
#ifdef RETROACHIEVEMENTS
    // A module left by an earlier tracked launch must not report for this one; a user's own
    // MODULE_9.IRX is never touched. Only USB VCDs are tracked (POPStarter's USB stack).
    int raModule = vcdRemoveRaModule(vcdPrefix);
    if (raModule == -2) {
        host::ui().msgBox(_l(_STR_PS1_RA_REMOVE_PREVIOUS), 0, NULL);
        return;
    }
    if (pDeviceData->bdmDeviceType == BDM_TYPE_USB && raPopsPrepare(vcdPrefix, pDeviceData->bdmPrefix, vcdFullPath, raModule == 0) < 0) {
        host::ui().msgBox(_l(_STR_PS1_RA_PREPARE_FAILED), 0, NULL);
        return;
    }
#else
    if (!vcdConfirmCleanLaunch(vcdPrefix))
        return;
#endif
    vcdPrepareRetroGemBarcode(vcdFullPath);

    // POPSTARTER.ELF may be on a different backend than the VCD. Keep both alive until the
    // argv-preserving loader has opened the ELF; POPSTARTER performs its own IOP reset afterwards.
    host::app().deinitEx(sbLoaderDeinitException(vcdElf), itemList->mode, host::app().pathToMode(vcdElf));
    sysLaunchPopstarter(vcdElf, vcdSelector);
}

void bdmLaunchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet)
{
    gBdmLauncher.launchVcd(itemList, vcdName, configSet);
}

// Δ9 (NHDDL parity): Neutrino's bd backend packs the ISO and BOTH -mc VMCs into ONE shared
// 64-entry fragment table (BDM_MAX_FRAGS in neutrino's fhi_bd_config.h; fhi_config.c's
// backend_bd_add_file_fd errors past it). That packing runs AFTER neutrino's own IOP reset,
// where its only error output is a debug printf -- from the couch, a black screen. So pre-count
// the same budget here, while the menu can still explain the abort, using the count-only form
// of the same ioctl (NULL buffer: bdmfs_fatfs's get_frag_list gates only the WRITE on capacity,
// the count is unconditional -- it returns the file's TOTAL fragment count and writes nothing).
// Every device this helper can see is bdmfs_fatfs-backed (mmce/udpfs launches live in their own
// support files), including both udp block transports (udpbd + udpfsbd depend on i_bdm), so the
// ioctl is always valid here. The budget itself is NeutrinoLauncher::kMaxFrags
// (include/neutrinolauncher.h): NOT OPL's BDM_MAX_FRAGS -- same value today, different owner.
int NeutrinoLauncher::fragBudgetOk(const char *isoPath, neutrino_vmc_args_t *vmcArgs, int isUdp)
{
    int isoFrags = 0, total = 0, i;
    int vmcFrags[NEUTRINO_VMC_SLOTS] = {0};
    const char *vmcPaths[NEUTRINO_VMC_SLOTS] = {NULL};

    int fd = open(isoPath, O_RDONLY);
    if (fd >= 0) {
        isoFrags = fileXioIoctl2(ps2sdk_get_iop_fd(fd), USBMASS_IOCTL_GET_FRAGLIST, NULL, 0, NULL, 0);
        close(fd);
        if (isoFrags < 0) {
            LOG("[NEUTRINO] ISO fragment mapping failed: %d\n", isoFrags);
            return 0;
        }
    }
    total = isoFrags;

    for (i = 0; i < NEUTRINO_VMC_SLOTS; i++) {
        const char *eq = (vmcArgs != NULL && vmcArgs->arg[i][0]) ? strchr(vmcArgs->arg[i], '=') : NULL;
        if (eq != NULL && eq[1] != '\0') {
            vmcPaths[i] = eq + 1; // "-mcN=<path>"; Δ2 already verified the file exists
            fd = open(vmcPaths[i], O_RDONLY);
            if (fd >= 0) {
                int frags = fileXioIoctl2(ps2sdk_get_iop_fd(fd), USBMASS_IOCTL_GET_FRAGLIST, NULL, 0, NULL, 0);
                close(fd);
                if (frags < 0) {
                    LOG("[NEUTRINO] VMC slot %d fragment mapping failed: %d\n", i, frags);
                    return 0;
                }
                if (frags > 0)
                    vmcFrags[i] = frags;
            }
            total += vmcFrags[i];
        }
    }

    if (total > kMaxFrags) { // == is fine: neutrino packs exactly-full tables
        LOG("[NEUTRINO] frag budget exceeded: %d/%d (ISO=%d, VMC0=%d, VMC1=%d)\n",
            total, kMaxFrags, isoFrags, vmcFrags[0], vmcFrags[1]);

        // If the ISO itself fits within the budget, but VMCs pushed it over:
        if (isoFrags <= kMaxFrags && (vmcFrags[0] > 0 || vmcFrags[1] > 0)) {
            int only = -1;
            for (i = 0; i < NEUTRINO_VMC_SLOTS; i++)
                if (vmcFrags[i] > 0 && total - vmcFrags[i] <= kMaxFrags &&
                    (only < 0 || vmcFrags[i] < vmcFrags[only]))
                    only = i;
            for (i = 0; i < NEUTRINO_VMC_SLOTS && total > kMaxFrags; i++) {
                if (vmcFrags[i] > 0 && (only < 0 || i == only)) {
                    char vmcName[32];
                    const char *slash = strrchr(vmcPaths[i], '/');
                    const char *dot = strrchr(vmcPaths[i], '.');
                    const char *start = slash ? slash + 1 : vmcPaths[i];
                    int nlen = dot && dot > start ? (int)(dot - start) : (int)strlen(start);
                    snprintf(vmcName, sizeof(vmcName), "%.*s", nlen, start);

                    if (gAutoLaunchBDMGame == NULL) {
                        char error[256];
                        snprintf(error, sizeof(error), _l(_STR_NEUTRINO_VMC_FRAG_BUDGET_CONTINUE), total, kMaxFrags, vmcName, (i + 1));
                        if (!host::ui().msgBox(error, 1, NULL))
                            return -1; // Abort launch to menu
                    }
                    total -= vmcFrags[i];
                    vmcArgs->arg[i][0] = '\0';
                    LOG("[NEUTRINO] VMC slot %d (%s) dropped due to fragment budget: new total=%d\n", i, vmcName, total);
                }
            }
            if (total <= kMaxFrags)
                return 1;
        }

        char msg[256]; // headroom for the lng/fork overlay's longer translations
        LOG("[NEUTRINO] frag budget exceeded: %d/%d across ISO + VMCs\n", total, kMaxFrags);
        // Match the Δ8 abort-message convention: the udp outcome is a consumed launch, the
        // local outcome is a native-core fallback (which usually boots: OPL's own 64-frag
        // table holds the ISO only -- VMCs go through mcemu's contiguity check instead).
        snprintf(msg, sizeof(msg), _l(isUdp ? _STR_NEUTRINO_TOO_FRAGMENTED_NET : _STR_NEUTRINO_TOO_FRAGMENTED), total, kMaxFrags);
        host::ui().warning(msg, 6);
        return 0;
    }
    return 1;
}

// Abort path of tryLaunch: back to a live menu so a later launch from another device does not
// inherit this one's markers. Returns failResult (1 = launch consumed, 0 = fall back to native).
int NeutrinoLauncher::abortLaunch(int failResult, config_set_t *configSet)
{
    launchDiagDisarm();
    if (failResult && gAutoLaunchBDMGame != NULL) {
        host::app().miniDeinit(configSet); // mirror the normal autolaunch teardown (ioEnd/configEnd + frees configSet)
        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;
        free(gAutoLaunchDeviceData);
        gAutoLaunchDeviceData = NULL;
    }
    return failResult;
}

// Δ8 (NHDDL parity): the LEAN Neutrino launch path. Everything bdmLaunchGame's native flow
// prepares -- VMC superblock prompts + mcemu patching, sbPrepare's cdvdman patch, per-part
// fragment lists (with a hard abort past 64 frags), layer-1 probing, cheats (with dialogs),
// PS2RD images, ATA DMA setup -- exists for the EMBEDDED cdvdman core; Neutrino re-derives all
// of it from -bsd/-dvd after its own IOP reset. Paying for that work meant a Neutrino launch
// could DIE on native-only failures (the fragmented-ISO abort, an interactive cheats dialog
// mid-launch, a VMC-superblock cancel whose result the launch then ignored). NHDDL's whole
// pre-handoff is ~3 file operations; this is ours.
// Returns 1 = handled (handed off, or aborted with the user informed -- caller returns);
// 0 = proceed with the native launch (core is OPL, or Neutrino unavailable).
int NeutrinoLauncher::tryLaunch(item_list_t *itemList, base_game_info_t *game, bdm_device_data_t *pDeviceData, config_set_t *configSet)
{
    // Native CF driver only; external Neutrino cannot restore this transport.
    if (pDeviceData->bdmDeviceType == BDM_TYPE_PCCF)
        return 0;
    int coreLoader = gDefaultCoreLoader; // no per-game $CoreLoader key -> follow the global default core
    configGetInt(configSet, CONFIG_ITEM_CORE_LOADER, &coreLoader);
    // The "udp" BDM device (UDPBD or UDPFS block) defaults to Neutrino, not to the global core; the
    // OPL core runs it only when the game's Loader Core says <OPL> (bdmNetBlockCoreLoader).
    int isUdp = bdmDriverIsUDPBD(pDeviceData->bdmDriver);
    if (isUdp)
        coreLoader = bdmNetBlockCoreLoader(configSet);
    if (!coreLoader)
        return 0;
    if (isUdp)
        launchDiagMark(1); // arms gLaunchDiag; every later marker gates on it

    // Abort helper contract: a failure here falls back to the native core, which since BDM_UDP_MODE /
    // BDM_UFS_MODE can also boot the udp device. Declined prompts, argv overflows and (on udp) a
    // failed preflight still abort.
    int failResult = 0;

    if (game->format == GAME_FORMAT_USBLD || !strcasecmp(game->extension, ".zso")) {
        // isValidIsoName() admits .zso case-insensitively and game->extension is stored verbatim,
        // so an upper/mixed-case ".ZSO" must reject here too (Neutrino can't run it).
        host::ui().warning(_l(_STR_NEUTRINO_BAD_FORMAT), 6);
        return abortLaunch(failResult, configSet);
    }

    const char *neutrinoPath = sbResolveNeutrinoPath(pDeviceData->bdmPrefix); // #300: AUTO probes this device for a co-located install
    if (neutrinoPath == NULL) {
        host::ui().warning(_l(_STR_NEUTRINO_NOT_FOUND), 6);
        return abortLaunch(failResult, configSet);
    }

    // Everything Neutrino actually needs from the config/device, copied to THIS stack frame
    // (deinit below frees configSet's owner + pDeviceData).
    // Absent per-game $NeutrinoVideo/$NeutrinoGsmComp keys = follow the global defaults (the
    // per-game picker's "Default" row removes the keys; explicit values -- including Off=0 --
    // persist and override).
    int compatmask = 0, neutrinoVideo = gNeutrinoVideoDefault, neutrinoGsmComp = gNeutrinoGsmCompDefault, neutrinoBsdfs = 0;
    char neutrinoExtraArgs[256] = "";
    neutrino_vmc_args_t neutrinoVmc = {0};
    char partname[256], bdmCurrentDriver[32];
    compatmask = sbGetCompatModes(configSet, 1); // same source sbPrepare reads; no IRX patch needed
    configGetStrCopy(configSet, CONFIG_ITEM_NEUTRINO_ARGS, neutrinoExtraArgs, sizeof(neutrinoExtraArgs));
    configGetInt(configSet, CONFIG_ITEM_NEUTRINO_VIDEO, &neutrinoVideo);
    configGetInt(configSet, CONFIG_ITEM_NEUTRINO_GSMCOMP, &neutrinoGsmComp);
    configGetInt(configSet, CONFIG_ITEM_NEUTRINO_BSDFS, &neutrinoBsdfs);     // #11: bdm legs (incl. udpbd/udpfsbd) are the block-backed consumers
    sbBuildVmcNeutrinoArgs(configSet, pDeviceData->bdmPrefix, &neutrinoVmc); // Δ2-validated -mc args
    sbCreatePath(game, partname, pDeviceData->bdmPrefix, "/", 0);            // -dvd target (multi-part was rejected above)
    snprintf(bdmCurrentDriver, sizeof(bdmCurrentDriver), "%s", pDeviceData->bdmDriver);
    // The bdm device number behind this mount (USBMASS_IOCTL_GET_DEVICE_NUMBER, the same value the
    // native leg binds) -- the <devNr> of a -bsdfs=bd "<driver><devNr>p0" tuple. Copied now: the
    // teardown below frees pDeviceData.
    int bdmDevNr = pDeviceData->massDeviceIndex;

    // Δ9: neutrino only discovers a blown fragment budget after its own IOP reset (debug
    // printf + exit = black screen). Count it now: a blown budget falls back to the native core,
    // which since BDM_UDP_MODE / BDM_UFS_MODE boots the udp device too, so udp takes the local message.
    int fragCheck = fragBudgetOk(partname, &neutrinoVmc, 0);
    if (fragCheck <= 0) {
        if (fragCheck < 0)
            failResult = 1; // User declined prompt: abort launch, do not fall back to native core
        return abortLaunch(failResult, configSet);
    }
    if (gLaunchDiag)
        launchDiagMark(2);

    if (gRememberLastPlayed) {
        configSetStr(configGetByType(CONFIG_LAST), "last_played", game->startup);
        host::app().saveConfig(CONFIG_LAST, 0);
    }

    // Δ6 preflight (driver token + network toml sync) -- abort stays in a live menu. On the udp device
    // its failures (bsd toml missing or unwritable, bd with no device number) are settings the user
    // must fix, and the toast says the launch was aborted, so it aborts rather than falling back.
    if (sysNeutrinoPreflight(bdmCurrentDriver, neutrinoPath, neutrinoBsdfs, neutrinoExtraArgs, bdmDevNr) < 0)
        return abortLaunch(isUdp ? 1 : failResult, configSet);
    // Cheats: the same per-game settings, .cht and Select-mode picker as the native leg, handed to
    // Neutrino as a config file. After the preflight, so a launch that falls back to the native
    // core asks nothing twice; backing out returns to the menu, never to the other core.
    if (sbNeutrinoLoadCheats(pDeviceData->bdmPrefix, game->startup, configSet, neutrinoPath, neutrinoExtraArgs) < 0)
        return abortLaunch(1, configSet);
#ifdef RETROACHIEVEMENTS
    sbNeutrinoPrepareRA(pDeviceData->bdmPrefix, game->startup, bdmCurrentDriver, neutrinoPath, neutrinoExtraArgs);
#endif
    if (sysNeutrinoArgsPreflight(bdmCurrentDriver, partname, game->startup, compatmask, gPS2Logo, neutrinoPath, neutrinoExtraArgs, neutrinoVideo, neutrinoGsmComp, neutrinoBsdfs, bdmDevNr, &neutrinoVmc) < 0)
        return abortLaunch(1, configSet); // an overfull Neutrino argv must not silently fall back to the native core
    sysNeutrinoWarnPadEmu(configSet);
    if (gLaunchDiag)
        launchDiagMark(3);

    // MMCE cross-device game-id (#261) with the Δ3 -mc-covered-slot guard. Before deinit (uses game).
    // Same MX4SIO settle gate as the native leg below (CodeRabbit review of #248, vetted): a switch
    // still in flight through the IOP reboot can starve the SD enumeration on the shared SIO2.
    if (mmceSendGameID(game->startup, neutrinoPath,
                       (neutrinoVmc.arg[0][0] ? 1 : 0) | (neutrinoVmc.arg[1][0] ? 2 : 0)) < 0 &&
        bdmDriverIsMx4sio(bdmCurrentDriver)) {
        if (mmceGameIdSettle(5000) < 0)
            host::ui().warning(_l(_STR_MMCE_GAMEID_UNSETTLED), 6);
    }
    if (gLaunchDiag)
        launchDiagMark(4);

    // game->startup lives inside bdmGames / gAutoLaunchBDMGame, both freed below (deinitEx's
    // itemCleanUp, or the explicit free in the autolaunch branch). Copy it before the teardown so
    // sysLaunchNeutrino's -elf build reads valid stack memory instead of freed heap (UAF).
    char bdmStartup[GAME_STARTUP_MAX + 1];
    snprintf(bdmStartup, sizeof(bdmStartup), "%s", game->startup);

    if (gAutoLaunchBDMGame == NULL) {
        // Keep-IOP handoff: keep BOTH the game device and the neutrino.elf device mounted
        // (Neutrino reads its -cwd config/modules and the ISO through our mounts pre-reset).
        int neutrinoDevMode = host::app().pathToMode(neutrinoPath);
        host::app().deinitEx(sbNeutrinoDeinitException(neutrinoPath), itemList->mode, neutrinoDevMode); // CAREFUL: itemCleanUp frees bdmGames/game
    } else {
        host::app().miniDeinit(configSet);
        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;
        free(gAutoLaunchDeviceData);
        gAutoLaunchDeviceData = NULL;
    }

    // gPS2Logo passes the user's preference straight through: Neutrino performs its own logo
    // read/validation for -logo, so the native path's CheckPS2Logo disc pass is not needed here.
    sysLaunchNeutrino(bdmCurrentDriver, partname, bdmStartup, compatmask, gPS2Logo, neutrinoPath, neutrinoExtraArgs, neutrinoVideo, neutrinoGsmComp, neutrinoBsdfs, bdmDevNr, &neutrinoVmc);
    return 1;
}

void bdmLaunchGame(item_list_t *itemList, int id, config_set_t *configSet)
{
    base_game_info_t *game;
    bdm_device_data_t *pDeviceData = NULL;

    if (gAutoLaunchBDMGame == NULL) {
        pDeviceData = (bdm_device_data_t *)itemList->priv;
        // Resolve through the ACTIVE view's array (#120 split): in the VCD view this yields the VCD
        // entry whose name the POPSTARTER handoff uses, and any stale/invalid id yields the
        // empty entry rather than indexing a possibly NULL/shorter array.
        game = bdmActiveGame(itemList, id);
        if (game == &bdmEmptyGame)
            return; // nothing there (stale id / not scanned) -- never launch a blank entry
    } else {
        pDeviceData = gAutoLaunchDeviceData;
        game = gAutoLaunchBDMGame;
    }

    if (pDeviceData->bdmDeviceType == BDM_TYPE_PCCF) {
#ifdef __INGAME_DEBUG
        host::ui().msgBox(_l(_STR_PCCF_UNSUPPORTED_GAME), 0, NULL);
        return; // Network/DECI2 game-debug builds cannot own the CF bus.
#endif
        char vmc[32];
        for (int slot = 0; slot < 2; slot++) {
            configGetVMC(configSet, vmc, sizeof(vmc), slot);
            if (vmc[0]) {
                host::ui().msgBox(_l(_STR_PCCF_UNSUPPORTED_GAME), 0, NULL);
                return;
            }
        }
    }

    // Folder browsing: a folder row is never launchable (the dispatch descends instead of reaching
    // here); guard defensively. Otherwise pin the path composers to the current subfolder so a game
    // inside it resolves to <prefix>CD|DVD/<sub>/<name>.
    if (game != NULL && game->format == GAME_FORMAT_FOLDER)
        return;
    // Autolaunch passes itemList == NULL (autoLaunchBDMGame), and an argv launch always names a file
    // at the CD/DVD root. Reading itemList->mode there loads from virtual address 0, which the EE's
    // default TLB leaves unmapped below 0x80000: a TLB-miss exception on real hardware, i.e. every BDM
    // autolaunch hung on a black screen from the day folder browsing landed (#545). The line is
    // pinned by name in .github/scripts/test_autolaunch_settings.py, so it stays here in the free
    // entry rather than in BdmLauncher::launch.
    sbSetBrowseSub(itemList != NULL ? folderGetSub(itemList->mode) : "");

    gBdmLauncher.launch(itemList, id, configSet, game, pDeviceData);
}

static int bdmMcemuSlots[2] = {-2, -2};

int bdmCheckNativeVmcLayout(int iop_fd, int size_mb, u64 *start_lba, int *fragments)
{
    *start_lba = (u64)-1;
    *fragments = -1;
    if (size_mb <= 0)
        return NATIVE_VMC_INVALID;

    // Some unsupported ioctl implementations return success without writing output.
    if (fileXioIoctl2(iop_fd, USBMASS_IOCTL_GET_LBA, NULL, 0, start_lba, sizeof(*start_lba)) != 0 ||
        *start_lba == (u64)-1)
        return NATIVE_VMC_LAYOUT_UNKNOWN;

    // Require the returned single extent to cover the whole card. A FAT read error
    // in an older driver can otherwise look like a prematurely terminated chain.
    bd_fragment_t extent = {.sector = (u64)-1, .count = 0};
    *fragments = fileXioIoctl2(iop_fd, USBMASS_IOCTL_GET_FRAGLIST, NULL, 0, &extent, sizeof(extent));
    if (*fragments <= 0)
        return NATIVE_VMC_LAYOUT_UNKNOWN;

    // mcemu's sector address is 32 bits. Check the last sector without wrapping.
    u64 sectors = (u64)size_mb * ((1024 * 1024) / 512);
    if (*start_lba > 0xffffffffULL || sectors - 1 > 0xffffffffULL - *start_lba)
        return NATIVE_VMC_LBA_RANGE;

    if (*fragments == 1 && (extent.sector != *start_lba || extent.count < sectors))
        return NATIVE_VMC_LAYOUT_UNKNOWN;

    return *fragments == 1 ? NATIVE_VMC_OK : NATIVE_VMC_FRAGMENTED;
}

void BdmLauncher::launch(item_list_t *itemList, int id, config_set_t *configSet, base_game_info_t *game, bdm_device_data_t *pDeviceData)
{
    int i, fd, iop_fd, index, compatmask = 0;
    int EnablePS2Logo = 0;
    int result;
    char partname[256], filename[32];
    struct cdvdman_settings_bdm *settings;
    u32 layer1_start, layer1_offset;
    unsigned short int layer1_part;

    // VCD view: this device is showing PS1 VCDs -> hand off to POPSTARTER (by name) instead of the
    // disc / Neutrino path below (which is entirely disc-specific). The BDM_TYPE_ATA internal exFAT HDD
    // still launches off the live massN: prefix, NOT ata0:/ (OPL never mounts an ata0: filesystem -- a
    // re-point there made LoadELFFromFile fail into an already-deinit'd OPL = black-screen freeze).
    if (gAutoLaunchBDMGame == NULL && game != NULL && (bdmGetItemView(itemList, id) == LIB_VIEW_PS1)) {
        // ONE PS1 list, TWO cores. Which one runs this title is a property of the ROW -- a .VCD row
        // goes to POPSTARTER, an Ember game folder to ember.elf -- and NEVER of the page, because
        // both kinds sit side by side in the same list. Asking the view here instead of the row is
        // exactly the mistake that would send every Ember title to POPSTARTER.
        if (cueIsCueEntry(game))
            launchCue(itemList, game->name, configSet);
        else
            launchVcd(itemList, game->name, configSet);
        return;
    }

    // Δ8: Neutrino core gets its own lean path FIRST -- everything below is native-core prep it
    // neither needs nor should be able to die on (see NeutrinoLauncher::tryLaunch).
    if (gNeutrinoLauncher.tryLaunch(itemList, game, pDeviceData, configSet))
        return;

    // NATIVE PATH ONLY, and it must be refused HERE if it cannot possibly finish.
    //
    // Everything below ends in the driver-token dispatch at the bottom of this function -- usb /
    // sd+ilink / sdc+mx4sio / ata -- which has NO default branch, and which runs AFTER deinit() has
    // torn the UI down. A device whose identity was never readable matches none of them: the launch
    // then simply RETURNS into a deinit'd OPL, and the user is left staring at a black screen with a
    // stuck loading icon until they reset the console. That is exactly what MX4SIO did after
    // rebuild-135 deferred its transport load past the first device poll, and bdmUpdateDeviceData
    // publishes such a slot quite happily: it fills bdmPrefix before the identity ioctl and treats an
    // unclassifiable transport as "never force-hide", so the games list and browse fine.
    //
    // Neutrino already refuses the same case while the menu is still alive (sysNeutrinoPreflight's
    // "unsupported device" stop); the native leg had no equivalent. The test is written as the exact
    // negation of the dispatch chain rather than as a device-type check, so the two cannot drift apart.
    //
    // bdmEnumerateDevices now waits for an enabled MX4SIO to answer that ioctl before any page polls,
    // so this should be unreachable. It is here to make "should" cost a message box instead of a dead
    // console. The udp device is natively launchable since BDM_UDP_MODE / BDM_UFS_MODE.
    if (!bdmDriverIsUSB(pDeviceData->bdmDriver) && !bdmDriverIsIlink(pDeviceData->bdmDriver) &&
        !bdmDriverIsMx4sio(pDeviceData->bdmDriver) && !bdmDriverIsATA(pDeviceData->bdmDriver) &&
        !bdmDriverIsUDPBD(pDeviceData->bdmDriver) && pDeviceData->bdmDeviceType != BDM_TYPE_PCCF) {
        LOG("BDMSUPPORT launch aborted: device %d has no natively launchable driver token ('%s')\n",
            itemList != NULL ? itemList->mode : -1, pDeviceData->bdmDriver);
        if (gAutoLaunchBDMGame == NULL)
            host::ui().msgBox(_l(_STR_ERR_FILE_INVALID), 0, NULL);
        return;
    }

    // Same dead end, reached by the OTHER half of the identity pass. USBMASS_IOCTL_GET_DEVICE_NUMBER
    // is allowed to fail on its own (bdmUpdateDeviceData only LOGs it and publishes anyway -- some USB
    // drives genuinely do not answer it), which leaves massDeviceIndex at -1. Nothing ever retries it:
    // the identity re-entry gate tests the root, the driver token and the device TYPE, but never the
    // index, so a slot that missed only this ioctl is latched at -1 until the console is rebooted.
    //
    // A -1 index is not a cosmetic gap. bdmSetLaunchDeviceBinding assigns it to settings->bdDeviceId,
    // which is a u32 (modules/iopcore/common/cdvd_config.h:82), so it reaches the IOP as 0xFFFFFFFF.
    // The in-game matcher requires bd->devNr == bdDeviceId (modules/iopcore/cdvdman/device-bdm.c:57-59),
    // no real device can ever equal that, so g_bd is never attached, bdm_io_sema is never signalled and
    // the game blocks on it FOREVER: the same black screen with a stuck loading icon, except this one
    // cannot self-heal at all.
    //
    // ATA is deliberately exempt: its cdvdman exposes one synthetic "ata" block device and binds by
    // driver token alone (the USE_BDM_ATA branch in the same matcher), so an ATA launch is unharmed by
    // a missing device number and must not be blocked here.
    if (pDeviceData->massDeviceIndex < 0 && !bdmDriverIsATA(pDeviceData->bdmDriver)) {
        LOG("BDMSUPPORT launch aborted: device %d ('%s') never reported a device number\n",
            itemList != NULL ? itemList->mode : -1, pDeviceData->bdmDriver);
        if (gAutoLaunchBDMGame == NULL)
            host::ui().msgBox(_l(_STR_ERR_FILE_INVALID), 0, NULL);
        return;
    }

    char vmc_name[32], vmc_path[256];
    int vmc_id, have_error, vmc_fragments, size_mcemu_irx = 0;
    bdm_vmc_infos_t bdm_vmc_infos;
    vmc_superblock_t vmc_superblock;

    for (vmc_id = 0; vmc_id < 2 && pDeviceData->bdmDeviceType != BDM_TYPE_PCCF; vmc_id++) {
        have_error = NATIVE_VMC_OK;
        vmc_fragments = 0;
        memset(&bdm_vmc_infos, 0, sizeof(bdm_vmc_infos_t));
        configGetVMC(configSet, vmc_name, sizeof(vmc_name), vmc_id);
        if (vmc_name[0]) {
            have_error = NATIVE_VMC_INVALID;
            int vmcSizeInMb = sysCheckVMC(pDeviceData->bdmPrefix, "/", vmc_name, 0, &vmc_superblock);
            if (vmcSizeInMb > 0) {
                bdm_vmc_infos.flags = vmc_superblock.mc_flag & 0xFF;
                bdm_vmc_infos.flags |= 0x100;
                bdm_vmc_infos.specs.page_size = vmc_superblock.page_size;
                bdm_vmc_infos.specs.block_size = vmc_superblock.pages_per_block;
                bdm_vmc_infos.specs.card_size = vmc_superblock.pages_per_cluster * vmc_superblock.clusters_per_card;

                snprintf(vmc_path, sizeof(vmc_path), "%sVMC/%s.bin", pDeviceData->bdmPrefix, vmc_name);

                fd = open(vmc_path, O_RDONLY);
                if (fd >= 0) {
                    iop_fd = ps2sdk_get_iop_fd(fd);
                    u64 startingLBA;
                    have_error = bdmCheckNativeVmcLayout(iop_fd, vmcSizeInMb, &startingLBA, &vmc_fragments);
                    LOG("BDMSUPPORT VMC slot %d layout: reason=%d fragments=%d start=0x%08x%08x\n",
                        vmc_id, have_error, vmc_fragments, (u32)(startingLBA >> 32), (u32)startingLBA);
                    if (have_error == NATIVE_VMC_OK) {
                        bdm_vmc_infos.active = 1;
                        bdm_vmc_infos.start_sector = startingLBA;
                    }

                    close(fd);
                }
            }
        }

        if (gAutoLaunchBDMGame == NULL) {
            if (have_error) {
                char error[256];
                // Diagnostic-only port: belomaxorka/YAOPL c19eed08. Reuse the
                // verified layout count; fragmented native VMCs stay inactive.
                if (have_error == NATIVE_VMC_FRAGMENTED && vmc_fragments > 1)
                    snprintf(error, sizeof(error), _l(_STR_ERR_VMC_FRAGMENTS_CONTINUE), vmc_name, vmc_fragments, (vmc_id + 1));
                else if (have_error == NATIVE_VMC_FRAGMENTED)
                    snprintf(error, sizeof(error), _l(_STR_ERR_VMC_FRAGMENTED_CONTINUE), vmc_name, (vmc_id + 1));
                else if (have_error == NATIVE_VMC_LBA_RANGE)
                    snprintf(error, sizeof(error), _l(_STR_ERR_VMC_LBA_RANGE_CONTINUE), vmc_name, (vmc_id + 1));
                else if (have_error == NATIVE_VMC_LAYOUT_UNKNOWN)
                    snprintf(error, sizeof(error), _l(_STR_ERR_VMC_LAYOUT_CONTINUE), vmc_name, (vmc_id + 1));
                else
                    snprintf(error, sizeof(error), _l(_STR_ERR_VMC_CONTINUE), vmc_name, (vmc_id + 1));
                if (!host::ui().msgBox(error, 1, NULL)) {
                    if (have_error == NATIVE_VMC_FRAGMENTED && vmc_fragments > 1)
                        host::ui().msgBox(_l(_STR_VMC_DEFRAGMENT_HINT), 0, NULL);
                    return;
                }
            }
        } else
            LOG("VMC error\n");

        // This write covers the slot's marker, so its position is found once and reused with a full-payload bound.
        int slotWord = nativeVmcSlotWord(&bdm_mcemu_irx, size_bdm_mcemu_irx, vmc_id, bdmMcemuSlots, sizeof(bdm_vmc_infos_t));
        if (slotWord >= 0) {
            if (bdm_vmc_infos.active)
                size_mcemu_irx = size_bdm_mcemu_irx;
            memcpy(&((u32 *)&bdm_mcemu_irx)[slotWord], &bdm_vmc_infos, sizeof(bdm_vmc_infos_t));
        }
    }

    void *irx = NULL;
    int irx_size = 0;
    if (pDeviceData->bdmDeviceType == BDM_TYPE_PCCF) {
        irx = &pccf_cdvdman_irx;
        irx_size = size_pccf_cdvdman_irx;
    } else if (bdmDriverIsATA(pDeviceData->bdmDriver)) {
        irx = &bdm_ata_cdvdman_irx;
        irx_size = size_bdm_ata_cdvdman_irx;
    } else if (bdmDriverIsUDPBD(pDeviceData->bdmDriver)) {
        irx = &bdm_udp_cdvdman_irx;
        irx_size = size_bdm_udp_cdvdman_irx;
    } else {
        irx = &bdm_cdvdman_irx;
        irx_size = size_bdm_cdvdman_irx;
    }

    compatmask = sbPrepare(game, configSet, irx_size, static_cast<void **>(irx), &index);
    if (compatmask < 0) // sbPrepare failed (patch zone not found): `index` is unset -- bail before using
        return;         // it. (The old `if (settings == NULL)` guard was dead: irx + index is never NULL.)
    sbEnsureIgrUsbDrivers(compatmask);

#ifdef PADEMU
    // MX4SIO reads the SD card over the SIO2 / memory-card bus -- the SAME bus pad emulation hooks
    // (modules/pademu installs a sio2man hook while pad emulation is enabled). With both active they
    // contend on SIO2 and the game frequently fails to boot (black screen, issue #53). It is an
    // inherent shared-bus conflict, not fixable here, so warn instead of leaving a mystery black
    // screen. sbPrepare() above resolved gEnablePadEmu for this game (global or per-game).
    if (pDeviceData->bdmDeviceType == BDM_TYPE_SDC && gEnablePadEmu)
        host::ui().warning(_l(_STR_MX4SIO_PADEMU_WARN), 6);
#endif

    settings = (struct cdvdman_settings_bdm *)((u8 *)irx + index);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
    memset(&settings->frags[0], 0, sizeof(bd_fragment_t) * BDM_MAX_FRAGS);
#pragma GCC diagnostic pop
    u8 iTotalFragCount = 0;

    //
    // Add ISO as fragfile[0] to fragment list
    //
    struct cdvdman_fragfile *iso_frag = &settings->fragfile[0];
    iso_frag->frag_start = 0;
    iso_frag->frag_count = 0;
    u64 isoTotalBytes = 0;
    int isoSizeKnown = 1;
    for (i = 0; i < game->parts; i++) {
        // Open file
        sbCreatePath(game, partname, pDeviceData->bdmPrefix, "/", i);
        fd = open(partname, O_RDONLY);
        if (fd < 0) {
            sbUnprepare(&settings->common);
            host::ui().msgBox(_l(_STR_ERR_FILE_INVALID), 0, NULL);
            return;
        }
        iop_fd = ps2sdk_get_iop_fd(fd); // only valid after confirming fd >= 0

        // Get fragment list
        int iFragCount = fileXioIoctl2(iop_fd, USBMASS_IOCTL_GET_FRAGLIST, NULL, 0, (void *)&settings->frags[iTotalFragCount], sizeof(bd_fragment_t) * (BDM_MAX_FRAGS - iTotalFragCount));
        if (iFragCount < 0 || iFragCount > BDM_MAX_FRAGS - iTotalFragCount) {
            // Negative (ioctl error) or more fragments than the remaining frags[] slots.
            // A negative value would underflow the u8 iTotalFragCount and corrupt the next
            // iteration's destination offset and size.
            close(fd);
            sbUnprepare(&settings->common);
            host::ui().msgBox(_l(_STR_ERR_FRAGMENTED), 0, NULL);
            return;
        }
        iso_frag->frag_count += iFragCount;
        iTotalFragCount += iFragCount;

        // A part whose size cannot be read makes the whole size unknown: a partial or wrapped total
        // would be a wrong bound, and an unknown one keeps CDVDMAN on the PVD (sbGetMediaLsnCount).
        s64 partBytes = lseek64(fd, 0, SEEK_END);
        if (partBytes > 0)
            isoTotalBytes += (u64)partBytes;
        else
            isoSizeKnown = 0;

        if ((gPS2Logo) && (i == 0))
            EnablePS2Logo = CheckPS2Logo(fd, 0);

        close(fd);
    }

    // Initialize layer 1 information.
    sbCreatePath(game, partname, pDeviceData->bdmPrefix, "/", 0);
    layer1_start = sbGetISO9660MaxLBA(partname);

    // Real media size, for CDVDMAN's out-of-bounds read emulation (upstream OPL #1763). The ISO9660
    // PVD cannot be trusted for this: badly mastered discs understate it and read data past the end
    // of the volume by raw LBA.
    settings->common.mediaLsnCount = sbGetMediaLsnCount(partname, isoSizeKnown ? isoTotalBytes : 0);

    switch (game->format) {
        case GAME_FORMAT_USBLD:
            layer1_part = layer1_start / 0x80000;
            layer1_offset = layer1_start % 0x80000;
            sbCreatePath(game, partname, pDeviceData->bdmPrefix, "/", layer1_part);
            break;
        default: // Raw ISO9660 disc image; one part.
            layer1_part = 0;
            layer1_offset = layer1_start;
    }

    if (sbProbeISO9660(partname, game, layer1_offset) != 0) {
        layer1_start = 0;
        LOG("DVD detected.\n");
    } else {
        layer1_start -= 16;
        LOG("DVD-DL layer 1 @ part %u sector 0x%lx.\n", layer1_part, layer1_offset);
    }
    settings->common.layer1_start = layer1_start;

    // adjust ZSO cache
    settings->common.zso_cache = bdmCacheSize;

#ifdef RETROACHIEVEMENTS
    // RA: this game's watch list, settled before sysLaunchLoaderElf reads
    // GetWatchCount() to decide whether the network modules travel with the
    // launch. Absent is normal -- the game is simply not tracked.
    sbLoadWatchList(pDeviceData->bdmPrefix, game->startup);
#endif
    if ((result = sbLoadCheats(pDeviceData->bdmPrefix, game->startup, configSet)) < 0) {
        // #265: let the user back out instead of sitting through the whole load. The helper does
        // the sbUnprepare itself -- see include/supportbase.h; skipping it breaks the NEXT launch.
        if (!sbCheatsMissingContinue(&settings->common, result))
            return;
    }
    sbLoadImage(pDeviceData->bdmPrefix, game->startup);

    if (gRememberLastPlayed) {
        configSetStr(configGetByType(CONFIG_LAST), "last_played", game->startup);
        host::app().saveConfig(CONFIG_LAST, 0);
    }

    if (configGetStrCopy(configSet, CONFIG_ITEM_ALTSTARTUP, filename, sizeof(filename)) == 0)
        strcpy(filename, game->startup);

    // deinit will free per device data.. copy driver name before free to compare for launch
    char bdmCurrentDriver[32];
    snprintf(bdmCurrentDriver, sizeof(bdmCurrentDriver), "%s", pDeviceData->bdmDriver);
    bdmSetLaunchDeviceBinding(settings, bdmCurrentDriver, pDeviceData->massDeviceIndex);

    if (bdmDriverIsATA(bdmCurrentDriver)) {
        // Get DMA settings for ATA mode.
        int dmaType = 0, dmaMode = 7;

        if (pDeviceData->bdmHddIsLBA48 < 0 || pDeviceData->ataHighestUDMAMode < 0)
            bdmResolveLBA_UDMA(pDeviceData);

        configGetInt(configSet, CONFIG_ITEM_DMA, &dmaMode);

        // Set DMA mode and spindown time.
        if (dmaMode < 3)
            dmaType = 0x20;
        else {
            dmaType = 0x40;
            dmaMode -= 3;
            // Use the user's configured UDMA mode, only CLAMPED to the drive's max -- don't FORCE the
            // highest, which is unstable on some drives/SATA adapters (wOPL 0f7443e, KrahJohlito). NB:
            // RiptOPL already avoids the worse scan-time UDMA push -- bdmResolveLBA_UDMA only queries
            // (never sets) the mode and caps it to UDMA 4 -- so the reported exFAT-HDD corruption from a
            // max-UDMA scan does not apply here; this just respects a user's lower-DMA stability choice.
            if (pDeviceData->ataHighestUDMAMode > 0 && dmaMode > pDeviceData->ataHighestUDMAMode)
                dmaMode = pDeviceData->ataHighestUDMAMode;
        }

        hddSetTransferMode(dmaType, dmaMode);
        // gHDDSpindown [0..20] -> spindown [0..240] -> seconds [0..1200]
        hddSetIdleTimeout(gHDDSpindown * 12);
        settings->hddIsLBA48 = pDeviceData->bdmHddIsLBA48;
    }

    // Δ8: Neutrino never reaches this point (gNeutrinoLauncher.tryLaunch handled it at the top), so
    // everything from here on is the NATIVE (embedded cdvdman) launch only. The udp device gets
    // here when its game's Loader Core is <OPL>, or when its Neutrino leg fell back.
    // Read before the teardown: which network block chain is resident decides the in-game modules.
    int netBlockProtocol = bdmGetLoadedNetProtocol();

    // MMCE cross-device game-id (#261): push the disc id to a present SD2PSX/MemCard PRO2 (either slot)
    // so it switches its per-game folder, even though this game is not on the MMCE. Self-probes +
    // no-ops if no card answers / feature off. Must run BEFORE deinit frees `game`.
    //
    // MX4SIO settle gate (HW batch S7): MX4SIO shares SIO2 with the MMCE, and an mmce card switch
    // still in flight during the IOP reboot can starve the SD enumeration cdvdman then waits on
    // FOREVER (the lime-green hang -- ee_core's marker right before LoadElf from cdrom0:). When the
    // send reports "switched but settle NOT confirmed" (-1), re-probe the exact slot it targeted for
    // up to 5 s; on expiry, warn and LAUNCH ANYWAY (the gate mitigates, never blocks -- and other
    // BDM transports don't share SIO2, so only the SDC leg pays it).
    if (mmceSendGameID(game->startup, NULL, 0) < 0 && bdmDriverIsMx4sio(bdmCurrentDriver)) {
        if (mmceGameIdSettle(5000) < 0)
            host::ui().warning(_l(_STR_MMCE_GAMEID_UNSETTLED), 6);
    }

    if (gAutoLaunchBDMGame == NULL) {
        host::app().deinit(NO_EXCEPTION, itemList->mode); // CAREFUL: deinit will call bdmCleanUp, so bdmGames/game will be freed
    } else {
        host::app().miniDeinit(configSet);

        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;

        free(gAutoLaunchDeviceData);
        gAutoLaunchDeviceData = NULL;
    }

    LOG("bdm pre sysLaunchLoaderElf\n");
    if (!strcmp(bdmCurrentDriver, "cf")) {
        // The CF driver owns DEV9: fake the game's own DEV9/ATAD so they never touch the slot.
        settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_DEV9 | FAKE_MODULE_FLAG_ATAD;
        sysLaunchLoaderElf(filename, "BDM_CF_MODE", irx_size, static_cast<void **>(irx), 0, NULL, EnablePS2Logo, compatmask);
    } else if (bdmDriverIsUSB(bdmCurrentDriver)) {
        settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_USBD;
        sysLaunchLoaderElf(filename, "BDM_USB_MODE", irx_size, static_cast<void **>(irx), size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
    } else if (bdmDriverIsIlink(bdmCurrentDriver)) {
        settings->common.fakemodule_flags |= 0 /* TODO! fake ilinkman ? */;
        sysLaunchLoaderElf(filename, "BDM_ILK_MODE", irx_size, static_cast<void **>(irx), size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
    } else if (bdmDriverIsMx4sio(bdmCurrentDriver)) {
        settings->common.fakemodule_flags |= 0;
        sysLaunchLoaderElf(filename, "BDM_M4S_MODE", irx_size, static_cast<void **>(irx), size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
    } else if (bdmDriverIsATA(bdmCurrentDriver)) {
        settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_DEV9;
        settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_ATAD;
        sysLaunchLoaderElf(filename, "BDM_ATA_MODE", irx_size, static_cast<void **>(irx), size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
    } else if (bdmDriverIsUDPBD(bdmCurrentDriver)) {
        // The block driver owns the network adapter in game: the game's own DEV9 and SMAP are faked,
        // as SMB does, so the game cannot take the adapter from it.
        settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_DEV9;
        settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_SMAP;
        sysLaunchLoaderElf(filename, netBlockProtocol == NET_BOOT_UDPFS ? "BDM_UFS_MODE" : "BDM_UDP_MODE", irx_size, static_cast<void **>(irx), size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
    }
}
