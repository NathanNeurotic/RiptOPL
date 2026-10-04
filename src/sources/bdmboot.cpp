#include "include/host.h"
#include "include/sema_lock.h"
#include "include/opl.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/supportbase.h"
#include "include/bdmsupport.h"
#include "include/bdmint.h"
#include "include/bdmboot.h"
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
#include "include/ethsupport.h"   // ethGetModulesLoaded() for the UDPBD<->SMB NIC interlock
#include "include/udpfssupport.h" // udpfsGetModulesLoaded() -- symmetric backstop vs the udpfs_ioman filesystem stack
#include "include/mmcesupport.h"  // mmceSendGameID() cross-device game-id (#261)
#include "include/extern_irx.h"
#include "include/cheatman.h"
#include "include/sound.h"
#include "include/bdmevent.h"
#include "modules/iopcore/common/cdvd_config.h"

#include <usbhdfsd-common.h>

#include "include/ps2sdkapi_compat.h"
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioIoctl, fileXioDevctl
#include <delaythread.h>
#include <sys/stat.h> // mkdir() -- create the config folder before a BDM save probe

// Boot directory resolution on BDM: map a launcher path on a mass/ilink/mx4sio/udpbd device to the concrete mass slot and settings home.
// Split out of bdmsupport.cpp.

BdmBoot gBdmBoot;

// True when the booted ELF's own folder exists on mass<slot>: -- the weak boot-device evidence.
int BdmBoot::bootDirPresent(int slot, const char *tail)
{
    char path[288];
    int dir;

    if (tail[0] == '\0')
        snprintf(path, sizeof(path), "mass%d:/", slot);
    else if (tail[0] == '/')
        snprintf(path, sizeof(path), "mass%d:%s", slot, tail);
    else
        snprintf(path, sizeof(path), "mass%d:/%s", slot, tail);

    dir = fileXioDopen(path);
    if (dir < 0)
        return 0;
    fileXioDclose(dir);
    return 1;
}

// True when the booted ELF itself exists at <tail>/<elfName> on mass<slot>: -- the definitive
// boot-device evidence (OPL is running from that very file).
int BdmBoot::bootElfPresent(int slot, const char *tail, const char *elfName)
{
    char path[320];
    int fd;

    if (tail[0] == '\0' || tail[0] == '/')
        snprintf(path, sizeof(path), "mass%d:%s/%s", slot, tail, elfName);
    else
        snprintf(path, sizeof(path), "mass%d:/%s/%s", slot, tail, elfName);

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

// Rewrite ONLY the device prefix of the boot dir, preserving the boot folder ("ata0:/APPS" ->
// "mass1:/APPS"). tail points INTO bootDir, so compose into a local buffer before writing back.
void BdmBoot::rewriteBootDir(char *bootDir, int bootDirSize, int slot, const char *tail)
{
    char resolved[256];

    if (tail[0] == '\0' || tail[0] == '/')
        snprintf(resolved, sizeof(resolved), "mass%d:%s", slot, tail);
    else
        snprintf(resolved, sizeof(resolved), "mass%d:/%s", slot, tail);

    snprintf(bootDir, bootDirSize, "%s", resolved);
}

// Read identity from ONE literal massN: slot. The driver name is the authoritative classification;
// USBMASS_IOCTL_GET_DEVICE_NUMBER is still requested for diagnostics, but some usable devices do not
// implement it, so its failure must not discard a valid driver name.
int BdmBoot::readLiteralMassIdentity(int slot, int *outType)
{
    char path[16], driver[32];
    int deviceIndex = -1;

    snprintf(path, sizeof(path), "mass%d:/", slot);
    bdmReadDeviceIdentity(path, driver, sizeof(driver), &deviceIndex);
    if (driver[0] == '\0')
        return 0;

    *outType = bdmDetermineDeviceType(driver);
    LOG("BOOT literal mass%d: driver %s device %d\n", slot, driver, deviceIndex);
    return 1;
}

// Resolve an explicit massN: boot identity without treating it as an invitation to search every
// mounted BDM device. The launcher has already named the filesystem slot; after IOP reset the only
// work required is to let a possible transport register THAT slot, then query its own driver/device
// ioctls. A cold untyped slot needs bounded transport bring-up, but every Dopen/ioctl below remains
// massN: itself. Bare mass: and typed launch aliases retain the legacy broader resolver below.
int BdmBoot::resolveLiteralMassSlot(int slot, int *ioBdmType,
                                    u32 initialBudgetMs, u32 ataInitialBudgetMs,
                                    u32 mx4sioExtraBudgetMs, u32 expensiveExtraBudgetMs)
{
    int knownType = *ioBdmType;
    int detectedType;

    if (readLiteralMassIdentity(slot, &detectedType)) {
        *ioBdmType = detectedType;
        return detectedType == BDM_TYPE_UDPBD ? -1 : 1;
    }

    // A UDPBD identity cannot be mounted until its network configuration has been read. Do not
    // make that bootstrap cycle worse by loading unrelated local transports or scanning other slots.
    if (knownType == BDM_TYPE_UDPBD)
        return -1;

    bdmLoadCoreModules(1); // literal boot slot: identify it whatever the USB source setting says

    // A save-time retry may already know the exact backing family. Load only that family and wait
    // only for the literal slot; an identity change never redirects settings to another massN:.
    if (knownType != BDM_TYPE_UNKNOWN) {
        u32 budgetMs = knownType == BDM_TYPE_ATA ? ataInitialBudgetMs : initialBudgetMs;
        u64 start;

        WaitSema(bdmLoadModuleLock);
        bdmEnsureTransportLoaded(knownType);
        SignalSema(bdmLoadModuleLock);

        start = GetTimerSystemTime();
        for (;;) {
            if (readLiteralMassIdentity(slot, &detectedType)) {
                *ioBdmType = detectedType;
                return detectedType == BDM_TYPE_UDPBD ? -1 : 1;
            }
            if ((GetTimerSystemTime() - start) / (kBUSCLK / 1000) > budgetMs)
                break;
            DelayThread(100 * 1000);
        }

        *ioBdmType = knownType;
        return -1;
    }

    // With an untyped massN: token, try the possible local transports in increasing cost. The
    // final iLink/ATA tier shares a budget because both can be cold, but no tier ever probes a
    // different mass slot to infer where the launcher came from.
    for (int tier = 0; tier < 3; tier++) {
        u32 budgetMs;
        u64 start;

        WaitSema(bdmLoadModuleLock);
        if (tier == 0) {
            bdmEnsureTransportLoaded(BDM_TYPE_USB);
            budgetMs = initialBudgetMs;
        } else if (tier == 1) {
            bdmEnsureTransportLoaded(BDM_TYPE_SDC);
            budgetMs = mx4sioExtraBudgetMs;
        } else {
            bdmEnsureTransportLoaded(BDM_TYPE_ILINK);
            bdmEnsureTransportLoaded(BDM_TYPE_ATA);
            budgetMs = expensiveExtraBudgetMs;
        }
        SignalSema(bdmLoadModuleLock);

        start = GetTimerSystemTime();
        for (;;) {
            if (readLiteralMassIdentity(slot, &detectedType)) {
                *ioBdmType = detectedType;
                return detectedType == BDM_TYPE_UDPBD ? -1 : 1;
            }
            if ((GetTimerSystemTime() - start) / (kBUSCLK / 1000) > budgetMs)
                break;
            DelayThread(100 * 1000);
        }
    }

    return -1;
}

// Resolve a boot directory that names a BDM device into that device's mounted massN: filesystem root,
// force-loading whatever driver stack the boot device needs FIRST. Called from the deferred config
// load -- after ioInit()/bdmInitSemaphore(), before the first configReadMulti() -- because at boot time
// the IOP has NO BDM modules at all (sysReset loads none; the BDM page only loads them on tab entry),
// so a "mass0:/APPS" or "ata0:/APPS" boot dir is unreadable exactly when settings must load. Two input
// families are handled:
//   - launch-binding identities (usb0:/ilink0:/sd0:/mx4sio0:/sdc0:/ata0:) -- device names with no
//     filesystem OPL reads; remapped to the SAME device's massN: mount.
//   - massN: -- an explicit filesystem slot. It is identified through THAT slot's driver/device ioctls
//     after the required transport registers it; it is never renumbered by probing other devices.
//   - mass: -- a legacy unspecified filesystem token, which has no literal slot and retains the
//     broader boot-ELF proof below.
// elfName (argv[0]'s basename; may be empty for a getcwd()-derived boot dir) anchors the verification:
// typed launch aliases and bare mass: are resolved to the slot that actually holds <tail>/<elfName>.
// The gEnable* config flags are deliberately ignored when loading transports -- the config is precisely
// what cannot be read yet (the exFAT-HDD chicken-and-egg: mounting it needs gEnableBdmHDD, which lives
// in the unreadable config); the caller reconciles the flags after the config loads.
// *ioBdmType is in/out: pass the boot device's BDM_TYPE_* when already known (the save-path re-resolve)
// to pin the search to that transport, or BDM_TYPE_UNKNOWN to classify from the prefix; on success it
// holds the resolved device's type.
// Returns 1 with bootDir rewritten in place on success; 0 when bootDir is not a BDM path (left
// untouched -- mc/mmce/host/pfs/hdd/... prefixes are not ours); -1 when the boot device never mounted
// within the budget (bootDir left untouched so its caller can keep the explicit identity and use defaults).
int BdmBoot::resolveWithBudget(char *bootDir, int bootDirSize, const char *elfName, int *ioBdmType,
                               u32 initialBudgetMs, u32 ataInitialBudgetMs,
                               u32 mx4sioExtraBudgetMs, u32 expensiveExtraBudgetMs)
{
    char stem[16];
    int unit;

    // *ioBdmType must never ERASE the caller's knowledge of the boot device's type -- the save-path
    // retry keys on it. It is written on success, and also on the -1 (never-mounted) paths with the
    // PREFIX CLASSIFICATION: that is not an erase (for an untyped massN: boot filterType IS knownType,
    // so it round-trips unchanged; for a typed usb0:/ata0:/... boot the prefix is authoritative). This
    // matters now that a failed resolve KEEPS the boot dir instead of dropping it to legacy discovery:
    // the caller zeroes this to UNKNOWN before every call, so without writing it back a slow-to-mount
    // boot device left gBootDirBdmType == UNKNOWN and _saveConfig's re-resolve-and-retry -- gated on
    // exactly that -- could never fire, so the settings never reached the device once it did mount
    // (CodeRabbit review of #202).
    int knownType = *ioBdmType;

    if (!bdmParseBootStem(bootDir, stem, sizeof(stem), &unit))
        return 0;

    int isMass = (strcmp(stem, "mass") == 0);
    int filterType = isMass ? knownType : bdmDetermineDeviceType(stem);
    if (!isMass && filterType == BDM_TYPE_UNKNOWN)
        return 0; // mc/mmce/host/pfs/hdd/cdrom/... -- not a BDM boot path, leave it alone
    // Reachable ONLY for a TYPED "udp0:" boot token, and never for the massN: form -- worth stating
    // because the massN: form is the one a UDPBD launcher actually hands over, so this reads like the
    // guard for it and is not. When isMass, filterType IS knownType, and resolveBootDirToMass zeroes
    // gBootDirBdmType to BDM_TYPE_UNKNOWN on the line immediately before every boot-path call; so on
    // that path this comparison cannot hold. It still earns its place on the _saveConfig re-resolve,
    // which passes the already-classified type back in.
    //
    // The consequence is deliberate and not fixable here: a UDPBD/UDPFS-BD boot arriving as
    // "massN:/OPL" is byte-identical to a USB one until the "udp" driver has registered, and that
    // transport needs the static IP out of the config being sought. The literal-slot resolver therefore
    // tries only local transports and fails closed rather than adding a network rung to every boot.
    if (filterType == BDM_TYPE_UDPBD) {
        *ioBdmType = filterType; // keep the classification so a later save retry stays pinned to it
        return -1;               // network block device: no local mount to resolve at config-load time
    }

    // A numeric massN: names one filesystem slot. Do not use the boot ELF as a reason to scan
    // MAX_BDM_DEVICES and potentially select a different device that happens to contain it.
    if (isMass && unit >= 0)
        return resolveLiteralMassSlot(unit, ioBdmType, initialBudgetMs, ataInitialBudgetMs,
                                      mx4sioExtraBudgetMs, expensiveExtraBudgetMs);

    const char *tail = strchr(bootDir, ':') + 1; // "" | "/APPS" | "APPS" (bdmParseBootStem proved the ':')
    // A typed launch alias's unit digit is a scan-order hint. Bare mass: has no literal slot, so its
    // legacy boot-ELF proof begins at mass0: and may inspect the remaining slots.
    int literalSlot = (unit >= 0 && unit < MAX_BDM_DEVICES) ? unit : 0;
    int haveElf = (elfName != NULL && elfName[0] != '\0');

    // Bring-up: the BDM core (bdm.irx + bdmfs_fatfs + hotplug events; idempotent -- every bdmInit calls
    // it too) plus the transport(s) the boot prefix implies. Only a legacy bare mass: has no transport
    // or literal slot, so start with the cheap common ones (USB, MX4SIO) and only escalate to iLink +
    // the ATA stack (iLinkman is known to break PCSX2, where a virtual-USB mass: boot is possible; ATA
    // means dev9 power + drive spin-up) if nothing turns up holding the boot folder.
    bdmLoadModules();
    WaitSema(bdmLoadModuleLock);
    if (filterType != BDM_TYPE_UNKNOWN) {
        bdmEnsureTransportLoaded(filterType);
    } else {
        // USB ONLY at first pass; MX4SIO waits for the escalation below.
        //
        // mx4sio_bd is an SD-card driver on SIO2 -- THE PAD'S OWN BUS -- and once loaded it is
        // resident for the session: mx4sioModLoaded latches and no unload path exists. Turning
        // MX4SIO Off in settings afterwards cannot undo it, and this resolver ignores that flag by
        // design. The literal massN: resolver above delays it until its second bounded tier; this
        // remaining broad path is only for an old bare mass: token.
        //
        // rebuild-66 never reaches this resolver at all (its setDefaults clears gBootDir before
        // configInit), and uOPL has no resolver and loads MX4SIO only when the user enables it.
        // Both are hardware-clean; this fork drops navigation inputs in every menu with the EE
        // provably at 60 Hz. This is the only source-proven HEAD-only difference that touches SIO2.
        //
        // Deferring costs a USB booter nothing and an MX4SIO booter one extra scan round.
        bdmEnsureTransportLoaded(BDM_TYPE_USB);
    }
    SignalSema(bdmLoadModuleLock);

    u32 budgetMs = (filterType == BDM_TYPE_ATA) ? ataInitialBudgetMs : initialBudgetMs;
    // Escalation tier for a bare mass: search: 0 = only USB is up, 1 = MX4SIO added, 2 = iLink + ATA
    // added, i.e. everything. A TYPED search starts past the end -- its one transport was loaded up
    // front and no other family can satisfy it -- which keeps its timeout path exactly as it was.
    // The full resolver receives 3000 + 3000 + 8000 = 14 s for explicit/save-time recovery. The
    // bootstrap wrapper below uses a shorter bounded ladder: a missing first-run config must reach
    // defaults promptly instead of spending that full recovery budget before the boot splash moves.
    int escalationTier = (filterType != BDM_TYPE_UNKNOWN) ? 2 : 0;
    // u64: GetTimerSystemTime() passes 2^32 bus ticks ~29 s after boot; a u32 start would make every
    // POST-BOOT resolve (the _saveConfig hotplug retry, a settings reload) compute a bogus huge elapsed
    // and give up after a single scan instead of waiting the budget out.
    u64 start = GetTimerSystemTime();

    for (;;) {
        int matchSlot = -1, fallbackSlot = -1;
        int matchType = BDM_TYPE_UNKNOWN, fallbackType = BDM_TYPE_UNKNOWN;

        for (int scan = 0; scan < MAX_BDM_DEVICES; scan++) {
            // Probe the boot dir's LITERAL slot first, then the rest in order: with duplicate OPL
            // installs on two same-type devices the ELF probe matches both, and first-match-wins
            // must prefer the slot the launcher actually named over an arbitrary lower one.
            int i = (scan == 0) ? literalSlot : (scan <= literalSlot ? scan - 1 : scan);
            char path[16], driver[32];
            int devIndex = -1;

            snprintf(path, sizeof(path), "mass%d:/", i);
            bdmReadDeviceIdentity(path, driver, sizeof(driver), &devIndex);
            if (driver[0] == '\0')
                continue; // slot not mounted (yet)

            int slotType = bdmDetermineDeviceType(driver);
            if (filterType != BDM_TYPE_UNKNOWN && slotType != filterType)
                continue; // wrong device family for a typed/pinned search

            if (haveElf && bootElfPresent(i, tail, elfName)) {
                matchSlot = i; // definitive: the ELF we are running from is right here
                matchType = slotType;
                break;
            }
            int dirOk = bootDirPresent(i, tail);
            if (!haveElf && dirOk && (!isMass || i == literalSlot)) {
                matchSlot = i; // getcwd-derived boot dir: folder presence is the best evidence there is
                matchType = slotType;
                break;
            }
            if (fallbackSlot < 0 && dirOk && (!isMass || i == literalSlot)) {
                fallbackSlot = i; // boot folder exists but the ELF probe missed -- hold as weak fallback
                fallbackType = slotType;
            }
        }

        if (matchSlot >= 0) {
            *ioBdmType = matchType;
            rewriteBootDir(bootDir, bootDirSize, matchSlot, tail);
            return 1;
        }

        if ((GetTimerSystemTime() - start) / (kBUSCLK / 1000) > budgetMs) {
            if (escalationTier == 0) {
                // TIER 1 -- MX4SIO alone, and nothing else. rebuild-135 moved the SD driver out of the
                // first pass because it lives on SIO2, the pad's bus; it did NOT stop being a cheap,
                // silent, dev9-free load. Lumping it in with iLink and ATA meant every MX4SIO boot also
                // dragged in ILINKMAN + IEEE1394_BD + dev9 + ps2atad + xhdd, which pre-135 it never
                // touched -- and on the usual MX4SIO rig (no HDD) the ATA leg fails and posts
                // _STR_HDD_NOT_CONNECTED_ERROR, whose setErrorMessageWithCode REPLACES the frame hook
                // with a modal box. The hook it replaces is menuUpdateHook, i.e. the thing that
                // schedules every BDM rescan and holds them back while covers are in flight. Splitting
                // the tiers costs a plain USB boot nothing (it matches in the first pass and never gets
                // here) and takes an MX4SIO boot straight from the SD load to its definitive ELF match.
                escalationTier = 1;
                WaitSema(bdmLoadModuleLock);
                bdmEnsureTransportLoaded(BDM_TYPE_SDC);
                SignalSema(bdmLoadModuleLock);
                budgetMs += mx4sioExtraBudgetMs;
                continue;
            }
            if (escalationTier == 1) {
                // TIER 2 -- the expensive ones. The boot device may still be the internal exFAT HDD
                // handed in massN: form (HDD-OSD launchers do this) or an iLink drive; extra budget for
                // dev9 power-up and platter spin-up.
                escalationTier = 2;
                WaitSema(bdmLoadModuleLock);
                bdmEnsureTransportLoaded(BDM_TYPE_ILINK);
                bdmEnsureTransportLoaded(BDM_TYPE_ATA);
                SignalSema(bdmLoadModuleLock);
                budgetMs += expensiveExtraBudgetMs;
                continue;
            }
            // The weak fallback is now consumed LAST, after every transport has had its chance --
            // it used to be tested above the escalation, which was harmless only while the first pass
            // loaded MX4SIO too. Post-135 the first pass can see nothing but USB, so a USB stick that
            // merely CONTAINS a folder with the boot dir's name (an /OPL you keep on a second stick)
            // became the only candidate in sight, was latched here as fallbackSlot, and won -- the
            // resolver returned it as the boot device and the escalation that would have revealed the
            // real MX4SIO card never ran at all. That homed the config on the wrong device. Folder
            // presence is a guess; a mounted transport holding the booted ELF is evidence, so the guess
            // must not be allowed to pre-empt gathering the evidence.
            if (fallbackSlot >= 0) {
                *ioBdmType = fallbackType;
                rewriteBootDir(bootDir, bootDirSize, fallbackSlot, tail);
                return 1;
            }
            *ioBdmType = filterType; // never mounted in time, but keep the classification: the caller
                                     // now KEEPS the boot dir, and _saveConfig's retry is gated on this
                                     // being != UNKNOWN (see the header comment)
            return -1;
        }
        DelayThread(100 * 1000);
    }
}

int BdmBoot::resolve(char *bootDir, int bootDirSize, const char *elfName, int *ioBdmType)
{
    // Full-budget resolver for explicit Custom Settings Path and save-time hotplug recovery.
    return resolveWithBudget(bootDir, bootDirSize, elfName, ioBdmType,
                             3000, 10000, 3000, 8000);
}

int BdmBoot::resolveBootstrap(char *bootDir, int bootDirSize, const char *elfName, int *ioBdmType)
{
    // Bootstrap is allowed to defer a cold/ambiguous device to the first explicit save retry, but
    // it must not make defaults wait for every BDM transport. ATA retains a little extra spin-up
    // room; an untyped massN: ladder is at most 1500 + 1500 + 3500 ms, plus its one ATA settle.
    return resolveWithBudget(bootDir, bootDirSize, elfName, ioBdmType,
                             1500, 5000, 1500, 3500);
}

int bdmResolveBootDir(char *bootDir, int bootDirSize, const char *elfName, int *ioBdmType)
{
    return gBdmBoot.resolve(bootDir, bootDirSize, elfName, ioBdmType);
}

int bdmResolveBootDirBootstrap(char *bootDir, int bootDirSize, const char *elfName, int *ioBdmType)
{
    return gBdmBoot.resolveBootstrap(bootDir, bootDirSize, elfName, ioBdmType);
}
