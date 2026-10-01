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
#include "include/ethsupport.h"   // ethGetModulesLoaded() for the UDPBD<->SMB NIC interlock
#include "include/udpfssupport.h" // udpfsGetModulesLoaded() -- symmetric backstop vs the udpfs_ioman filesystem stack
#include "include/mmcesupport.h"  // mmceSendGameID() cross-device game-id (#261)
#include "include/extern_irx.h"
#include "include/cheatman.h"
#include "include/sound.h"
#include "include/bdmevent.h"
#include "modules/iopcore/common/cdvd_config.h"

#include <usbhdfsd-common.h>

#include <ps2sdkapi.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioIoctl, fileXioDevctl
#include <delaythread.h>
#include <sys/stat.h> // mkdir() -- create the config folder before a BDM save probe

int iUSBModLoaded = 0;
int iLinkModLoaded = 0;
int iLinkManModLoaded = 0;
int ieee1394ModLoaded = 0;
int mx4sioModLoaded = 0;
int hddModLoaded = 0;
static int udpbdModLoaded = 0;
/*
  WHICH network block transport is actually resident, as NET_BOOT_UDPBD / NET_BOOT_UDPFS, or -1
  when none is loaded.

  udpbdModLoaded alone is protocol-agnostic: both branches of the loader set it to 1, so once
  either chain is up the module gate short-circuits. But the tab label, the tab icon and the
  Neutrino -bsd backend all read the LIVE gNetBootProtocol. Change the picker without rebooting and
  those three start describing a transport that is NOT the one loaded -- e.g. a tab labelled UDPFSBD
  and a "-bsd=udpfsbd" launch arg while the smap_udpbd monolith is what is actually resident.
  (A restart notice is shown on protocol change, but nothing enforces it.)

  Consumers below use this instead, so they always describe what is really loaded.
*/
static int udpbdLoadedProtocol = -1;
s32 bdmLoadModuleLock;
int bdmDeviceModeStarted;

item_list_t bdmDeviceList[MAX_BDM_DEVICES];
int bdmDeviceListInitialized = 0;

unsigned int BdmGeneration = 0;
// Drives BDM refused because their sectors are not 512 bytes (4K-sector drives, refused by design).
// They never become massN:, so without a message they just look dead. Written by bdmEventHandler,
// reported by bdmReportUnsupportedDrives on the ordinary update path.
static volatile unsigned int BdmUnsupportedSectorDrives;
static unsigned int BdmUnsupportedSectorReported;
#ifdef __DEBUG
// bdmEventHandler runs in the SIF command callback context; never perform diagnostic I/O there.
// Snapshot the change and emit it from the ordinary deferred device-update worker instead.
volatile unsigned int BdmDiagEventPreviousGeneration;
volatile unsigned int BdmDiagEventGeneration;
volatile unsigned int BdmDiagEventReceivedCount;
volatile int BdmDiagEventCause;
volatile unsigned int BdmDiagIopCallbackSequence;
volatile unsigned int BdmDiagIopMountCallbackCount;
volatile unsigned int BdmDiagIopUnmountCallbackCount;
volatile unsigned int BdmDiagIopBlockDeviceCount;
volatile unsigned int BdmDiagIopUsbRootCount;
volatile unsigned int BdmDiagIopUsbRootMask;
volatile int BdmDiagEventPending;
volatile int BdmDiagIopSnapshotPending;
unsigned int BdmDiagSnapshotRequestCount;
unsigned int BdmDiagSlotProbeCount[MAX_BDM_DEVICES];
#endif

int bdmDriverIsUSB(const char *driverName)
{
    return driverName != NULL && strcmp(driverName, "usb") == 0;
}

int bdmDriverIsIlink(const char *driverName)
{
    return driverName != NULL && (strcmp(driverName, "sd") == 0 || strcmp(driverName, "ilink") == 0);
}

int bdmDriverIsMx4sio(const char *driverName)
{
    return driverName != NULL && (strcmp(driverName, "sdc") == 0 || strcmp(driverName, "mx4sio") == 0);
}

int bdmDriverIsATA(const char *driverName)
{
    return driverName != NULL && strcmp(driverName, "ata") == 0;
}

int bdmDriverIsUDPBD(const char *driverName)
{
    return driverName != NULL && strcmp(driverName, "udp") == 0;
}

int bdmDetermineDeviceType(const char *driverName)
{
    if (bdmDriverIsUSB(driverName))
        return BDM_TYPE_USB;
    if (bdmDriverIsIlink(driverName))
        return BDM_TYPE_ILINK;
    if (bdmDriverIsMx4sio(driverName))
        return BDM_TYPE_SDC;
    if (bdmDriverIsATA(driverName))
        return BDM_TYPE_ATA;
    if (bdmDriverIsUDPBD(driverName))
        return BDM_TYPE_UDPBD;

    return BDM_TYPE_UNKNOWN;
}

#ifdef __DEBUG
static const char *bdmDeviceTypeName(int type)
{
    switch (type) {
        case BDM_TYPE_USB:
            return "USB";
        case BDM_TYPE_ILINK:
            return "iLink";
        case BDM_TYPE_SDC:
            return "MX4SIO";
        case BDM_TYPE_ATA:
            return "ATA";
        case BDM_TYPE_UDPBD:
            return "UDPBD";
        default:
            return "unknown";
    }
}
#endif

// Parse the leading "<device><unit>:" token of a boot path into its digit-stripped stem ("ata0" ->
// "ata", "mass1" -> "mass") and unit number (-1 when the token carries no digits, e.g. uLE's "mass:").
// Anchored to the FIRST ':' so it can never match a folder name deeper in the path. Returns 1 on a
// well-formed device token, 0 otherwise. Used by bdmResolveBootDir to classify boot directories.
int bdmParseBootStem(const char *bootPath, char *stem, size_t stemSize, int *unit)
{
    if (bootPath == NULL)
        return 0;

    const char *colon = strchr(bootPath, ':');
    if (colon == NULL || colon == bootPath)
        return 0; // no "<device>:" segment

    size_t n = (size_t)(colon - bootPath);
    if (n >= stemSize)
        return 0;
    memcpy(stem, bootPath, n);
    stem[n] = '\0';

    *unit = -1;
    size_t digits = 0;
    while (n > 0 && stem[n - 1] >= '0' && stem[n - 1] <= '9') { // "ata0" -> "ata", "mx4sio0" -> "mx4sio"
        stem[--n] = '\0';
        digits++;
    }
    // Bound the unit parse: no real device token carries more than 2 digits, and an unbounded
    // accumulate over launcher-controlled argv[0] bytes would run signed arithmetic into UB on a
    // mangled path ("mass4294967296:"). Longer runs keep the stem valid but leave unit = -1
    // ("unspecified"), which the resolver already treats as slot 0 with verification.
    if (digits > 0 && digits <= 2) {
        *unit = 0;
        for (const char *d = bootPath + n; *d != ':'; d++)
            *unit = *unit * 10 + (*d - '0');
    }

    return n > 0; // require a non-empty alpha stem ("0:" is not a device token)
}

void bdmSetLaunchDeviceBinding(struct cdvdman_settings_bdm *settings, const char *driverName, int deviceIndex)
{
    settings->bdDeviceId = deviceIndex;
    // Match the IOP-side block device by its exported driver token, not the EE typed mount alias.
    if (driverName != NULL && driverName[0] != '\0')
        snprintf(settings->bdDeviceDriver, sizeof(settings->bdDeviceDriver), "%s", driverName);
    else
        settings->bdDeviceDriver[0] = '\0';
}

void bdmBuildGamePrefix(char *target, int targetLength, const char *deviceRoot)
{
    if (gBDMPrefix[0] != '\0')
        snprintf(target, targetLength, "%s%s/", deviceRoot, gBDMPrefix);
    else
        snprintf(target, targetLength, "%s", deviceRoot);
}

// The VCD (PS1/POPSTARTER) view is anchored to the DEVICE ROOT ("mass<N>:/"), never to the gBDMPrefix
// library folder: POPSTARTER's own BDMA driver always reads <root>/POPS/<name>.VCD, so a prefixed scan
// ("mass0:<prefix>/POPS") would list VCDs that POPSTARTER can never boot -- they'd all drop to OSDSYS.
// gBDMPrefix stays a PS2-game-library concept only.
void bdmBuildPs1Prefix(char *target, int targetLength, int massSlot)
{
    snprintf(target, targetLength, "mass%d:/", massSlot);
}

/* ⚠ NEVER ASK FOR THE DRIVER NAME WITH A RETURN BUFFER UNTIL YOU KNOW THE VOLUME IS MOUNTED.
   ps2sdk's handler NULL-DEREFERENCES otherwise. iop/fs/bdmfs_fatfs/src/fs_driver.c, upstream master,
   verified 2026-08-26:

       struct block_device *mounted_bd = fatfs_..._get_mounted_bd_from_index(file->obj.fs->pdrv);
       ret = (mounted_bd == NULL) ? -ENXIO : *(int *)(mounted_bd->name);
       if (rdata != NULL)
           strncpy(rdata, mounted_bd->name, rdatalen);   // <-- NOT guarded on mounted_bd

   The NULL check on the line above does not cover the copy. Supplying rdata for a volume with no
   mounted block device therefore dereferences NULL ON THE IOP, and an IOP fault takes down every
   thread waiting on it -- observed on hardware as the console freezing after browsing BDM pages,
   and as mass slots that never resolve an identity.

   It is reachable because "mass" resolves its volume straight from the unit number:
   fs_driver_resolve_volume() returns `unit` for "mass" WITHOUT checking that anything is mounted
   there, so Dopen can succeed on a slot whose backing device has gone away.

   We embed the SDK's PREBUILT bdmfs_fatfs.irx (Makefile), so this cannot be fixed on our side --
   only avoided. The no-buffer form returns -ENXIO safely (it carries the name's first four bytes in
   its return value instead). That is precisely what Grimdoomer's fork does, and why it reads mass
   devices where we fault. Gate on it, then ask for the full name.

   Returns >= 0 on success; driverName is always left NUL-terminated. */
int bdmReadDriverName(int dir, char *driverName, int driverNameLength)
{
    if (driverName == NULL || driverNameLength <= 0)
        return -1;

    driverName[0] = '\0';

    /* NO PRECONDITION PROBE. This is a retraction, and the reasoning matters more than the diff.
     *
     * rebuild-136 put a probe here -- fileXioIoctl(dir, USBMASS_IOCTL_GET_DRIVERNAME, "") -- and
     * abandoned the read when it failed, on the theory that a slot with no mounted block device
     * would fault the IOP once asked WITH a return buffer.
     *
     * That probe is sent to the driver's `ioctl` handler. USBMASS_IOCTL_GET_DRIVERNAME is an
     * `ioctl2` command, and iomanX_iop_device_ops_t carries the two as SEPARATE function pointers
     * -- so the probe never reached the handler the theory was about. Whether any given filesystem
     * driver's plain ioctl handler even recognises 0x0003 was never checked. If it does not, the
     * probe fails on EVERY device, always, and this function returns before reading anything.
     *
     * That failure is not quiet. bdmReadDeviceIdentity gates GET_DEVICE_NUMBER behind this result,
     * so a device that is present and perfectly mountable ends up with an empty driver token AND a
     * -1 device index -- the precise pair that dead-ends art and launch, and a strong candidate for
     * a tab that lists nothing at all.
     *
     * So the guard goes. The direct call below is what OPL shipped at all three former call sites
     * for years, on hardware, and a hardening built on an unverified premise does not get to
     * outrank that. Reinstating protection here needs the mounted-device question answered through
     * an entry point known to work -- GET_DEVICE_NUMBER via ioctl2 into a bounded int -- and needs
     * the ps2sdk handler read rather than assumed. Not guessed at twice. */
    return fileXioIoctl2(dir, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, driverName, driverNameLength - 1);
}

int bdmReadDeviceIdentity(const char *path, char *driverName, int driverNameLength, int *deviceIndex)
{
    int dir, result;

    if (driverNameLength > 0)
        memset(driverName, 0, driverNameLength);
    *deviceIndex = -1;

    dir = fileXioDopen(path);
    if (dir < 0)
        return dir;

    result = bdmReadDriverName(dir, driverName, driverNameLength);
    if (result >= 0)
        result = fileXioIoctl2(dir, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, deviceIndex, sizeof(*deviceIndex));

    fileXioDclose(dir);
    return result;
}

#ifdef __DEBUG
typedef struct
{
    int openResult;
    int identityResult;
    char driver[32];
    int deviceIndex;
} bdm_diag_identity_t;

static void bdmDiagProbeIdentity(const char *path, bdm_diag_identity_t *identity)
{
    int dir;

    memset(identity, 0, sizeof(*identity));
    identity->openResult = -1;
    identity->identityResult = -1;
    identity->deviceIndex = -1;

    dir = fileXioDopen(path);
    identity->openResult = dir;
    if (dir < 0) {
        identity->identityResult = dir;
        return;
    }

    identity->identityResult = bdmReadDriverName(dir, identity->driver, sizeof(identity->driver));
    if (identity->identityResult >= 0)
        identity->identityResult = fileXioIoctl2(dir, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, &identity->deviceIndex, sizeof(identity->deviceIndex));

    fileXioDclose(dir);
}

static const char *bdmTypedRootStem(int type)
{
    switch (type) {
        case BDM_TYPE_USB:
            return "usb";
        case BDM_TYPE_ILINK:
            return "ilink";
        case BDM_TYPE_SDC:
            return "mx4sio";
        case BDM_TYPE_ATA:
            return "ata";
        default:
            return NULL;
    }
}

// Probe the SDK's typed filesystem aliases without making them runtime inputs. A typed unit is a
// same-family ordinal, not the BDM driver's raw device number, so enumerate aliases and validate
// the candidate by the driver-reported raw number instead of assuming the digits are interchangeable.
static int bdmDiagResolveTypedIdentity(int type, int expectedDeviceIndex, char *root, int rootLength, bdm_diag_identity_t *identity, int *matchingAliases)
{
    const char *stem = bdmTypedRootStem(type);
    int haveReadableAlias = 0;
    int haveTypedCandidate = 0;

    root[0] = '\0';
    memset(identity, 0, sizeof(*identity));
    identity->openResult = -1;
    identity->identityResult = -1;
    identity->deviceIndex = -1;
    *matchingAliases = 0;

    if (stem == NULL)
        return 0;

    for (int unit = 0; unit < MAX_BDM_DEVICES; unit++) {
        char candidateRoot[16];
        bdm_diag_identity_t candidate;

        snprintf(candidateRoot, sizeof(candidateRoot), "%s%d:/", stem, unit);
        bdmDiagProbeIdentity(candidateRoot, &candidate);
        if (candidate.openResult < 0)
            continue;

        // Preserve the first readable alias even if its identity ioctl is unavailable. That still
        // proves the installed SDK exported the typed filesystem, while revalidation remains false.
        if (!haveReadableAlias) {
            snprintf(root, rootLength, "%s", candidateRoot);
            *identity = candidate;
            haveReadableAlias = 1;
        }

        if (bdmDetermineDeviceType(candidate.driver) != type)
            continue;

        (*matchingAliases)++;
        if (!haveTypedCandidate) {
            snprintf(root, rootLength, "%s", candidateRoot);
            *identity = candidate;
            haveTypedCandidate = 1;
        }
        if (expectedDeviceIndex >= 0 && candidate.deviceIndex == expectedDeviceIndex) {
            snprintf(root, rootLength, "%s", candidateRoot);
            *identity = candidate;
            return 1;
        }

        // With no cached raw number there is no way to prove ownership. Continue enumerating so the
        // report can distinguish a single inferred same-family alias from an ambiguous multi-device
        // result without pretending either one is revalidated.
    }

    return 0;
}

static int bdmDiagListIndex(const item_list_t *itemList)
{
    for (int i = 0; i < MAX_BDM_DEVICES; i++) {
        if (itemList == &bdmDeviceList[i])
            return i;
    }

    return -1;
}

static void bdmLogIdentityDiagnostic(const char *event, const bdm_config_diag_snapshot_t *snap)
{
    int slot;
    char legacyRoot[16], typedRoot[16];
    bdm_diag_identity_t legacyIdentity, typedIdentity;
    int typedRevalidated;
    int matchingTypedAliases;
    int legacyDriverMatch;
    int legacyDeviceMatch;
    int legacyRevalidated;

    if (snap == NULL || !snap->valid)
        return;

    slot = snap->slot;

    // Everything below reads the CAPTURED COPY. The probes that follow are blocking device I/O, so
    // this must never reach back into the live item_list_t / config_set_t -- by the time we get here
    // the menu lock is released and either of them may already be gone.
    snprintf(legacyRoot, sizeof(legacyRoot), "mass%d:/", slot);
    bdmDiagProbeIdentity(legacyRoot, &legacyIdentity);
    typedRevalidated = bdmDiagResolveTypedIdentity(snap->bdmDeviceType, snap->massDeviceIndex, typedRoot, sizeof(typedRoot), &typedIdentity, &matchingTypedAliases);

    legacyDriverMatch = snap->driver[0] != '\0' && legacyIdentity.driver[0] != '\0' && strcmp(snap->driver, legacyIdentity.driver) == 0;
    legacyDeviceMatch = snap->massDeviceIndex >= 0 && legacyIdentity.deviceIndex >= 0 && snap->massDeviceIndex == legacyIdentity.deviceIndex;
    legacyRevalidated = legacyDriverMatch && legacyDeviceMatch;

    LOG("[BDM DIAG] event=%s generation=%u itemList=%p mode=%d visible=%d config=%p uid=%u modified=%d format=%d filename=\"%s\"\n",
        event, snap->generation, snap->itemListAddr, snap->mode, snap->visible, snap->configAddr, snap->configUid,
        snap->configModified, snap->configFormat, snap->configFilename);
    LOG("[BDM DIAG] event=%s cached slot=%d root=\"%s\" prefix=\"%s\" driver=\"%s\" devnr=%d type=%s(%d) folders=%u games=%p/%d ps1Games=%p/%d\n",
        event, slot, snap->deviceRoot, snap->devicePrefix, snap->driver, snap->massDeviceIndex,
        bdmDeviceTypeName(snap->bdmDeviceType), snap->bdmDeviceType, snap->foldersCreated,
        snap->games, snap->gameCount, snap->ps1Games, snap->ps1GameCount);
    LOG("[BDM DIAG] event=%s legacy=\"%s\" readable=%d open=%d identity=%d driver=\"%s\" devnr=%d driverMatch=%d devnrMatch=%d revalidated=%d\n",
        event, legacyRoot, legacyIdentity.openResult >= 0, legacyIdentity.openResult, legacyIdentity.identityResult,
        legacyIdentity.driver, legacyIdentity.deviceIndex, legacyDriverMatch, legacyDeviceMatch, legacyRevalidated);
    LOG("[BDM DIAG] event=%s typed=\"%s\" exported=%d open=%d identity=%d driver=\"%s\" devnr=%d candidates=%d ambiguous=%d revalidated=%d writeOwnershipRevalidated=%d\n",
        event, typedRoot[0] != '\0' ? typedRoot : "<none>", typedIdentity.openResult >= 0,
        typedIdentity.openResult, typedIdentity.identityResult, typedIdentity.driver, typedIdentity.deviceIndex,
        matchingTypedAliases, matchingTypedAliases > 1, typedRevalidated, legacyRevalidated);
}
#else
static void bdmLogIdentityDiagnostic(const char *event, const bdm_config_diag_snapshot_t *snap)
{
    (void)event;
    (void)snap;
}
#endif

// Find the first mounted BDM device whose driver matches bdmType (BDM_TYPE_*) and write its massN:
// FILESYSTEM root WITH a trailing slash (e.g. "mass0:/") to root. Returns 1 if such a device is mounted,
// 0 otherwise. Used by bdmEnsureSourceModules as a mount-readiness check for a transport type.
// The root is ALWAYS the legacy mass<slot>: mount, NEVER a typed root (ata0:/usb0:/mx4sio0:/ilink0:):
// on the pinned SDK those are launch-binding identities with no filesystem behind them, and on newer
// SDKs (ps2sdk 2026-02-26 "typed devices": bdmfs_fatfs registers them as real iomanX filesystems) a
// typed root here would leak ata0:-rooted paths into consumers that must stay on massN: -- POPSTARTER,
// the BDMA equip, the boot-dir resolver and the Neutrino pickers all read files through massN: only.
// Launch binding keeps using the raw driver token via bdmSetLaunchDeviceBinding, which is unaffected.
// The BDMA equip itself uses bdmGetDeviceSlotsByType to search EVERY same-type slot, not just the first.
int bdmGetDeviceRootByType(int bdmType, char *root, int rootLen)
{
    if (root == NULL || rootLen <= 0)
        return 0;

    for (int i = 0; i < MAX_BDM_DEVICES; i++) {
        char path[16], driver[32];
        int devIndex = -1;

        snprintf(path, sizeof(path), "mass%d:/", i);
        // Identify the device by its DRIVER NAME only. Do NOT gate on the full bdmReadDeviceIdentity
        // return: GET_DEVICE_NUMBER can fail on a perfectly usable device (seen on some USB drives), and
        // the device pages already tolerate that (they keep the device by driver name, bdmsupport.c
        // ~1219). Requiring it here made the equip report "no device matching" for a connected source.
        bdmReadDeviceIdentity(path, driver, sizeof(driver), &devIndex);
        if (driver[0] == '\0')
            continue;
        if (bdmDetermineDeviceType(driver) != bdmType)
            continue;

        snprintf(root, rootLen, "mass%d:/", i);
        return 1;
    }

    return 0;
}

// Fill `slots` with the massN: slot index of EVERY mounted device whose driver matches bdmType, up to
// maxSlots; returns the count. The filesystem root for slot i is "mass<i>:/" (OPL never mounts a typed
// ata0:/usb0: filesystem -- block-device identities only). The BDMA equip uses this to search a source
// family that has more than one device of the same type: the variant files may sit on the second one,
// which bdmGetDeviceRootByType (first-match) would miss.
int bdmGetDeviceSlotsByType(int bdmType, int *slots, int maxSlots)
{
    int n = 0;

    if (slots == NULL || maxSlots <= 0)
        return 0;

    for (int i = 0; i < MAX_BDM_DEVICES && n < maxSlots; i++) {
        char path[16], driver[32];
        int devIndex = -1;

        snprintf(path, sizeof(path), "mass%d:/", i);
        bdmReadDeviceIdentity(path, driver, sizeof(driver), &devIndex);
        if (driver[0] != '\0' && bdmDetermineDeviceType(driver) == bdmType)
            slots[n++] = i;
    }

    return n;
}

int bdmLoadOptionalModule(const char *name, void *module, int moduleSize)
{
    int result;

    LOG("[%s]:\n", name);
    result = sysLoadModuleBuffer(module, moduleSize, 0, NULL);
    if (result < 0)
        LOG("%s failed to load: %d\n", name, result);

    return result;
}

// Like bdmLoadOptionalModule, but passes IOP module args (arglen = byte length of the packed,
// NUL-terminated args buffer). smap_udpbd requires an "ip=A.B.C.D" arg; the base loader passes none.
static int bdmLoadOptionalModuleArgs(const char *name, void *module, int moduleSize, int arglen, char *args)
{
    int result;

    LOG("[%s]:\n", name);
    result = sysLoadModuleBuffer(module, moduleSize, arglen, args);
    if (result < 0)
        LOG("%s failed to load: %d\n", name, result);

    return result;
}

/* OPEN EVERY massN: ROOT ONCE, AND SAY WHAT WAS THERE.
 *
 * The probe is FUNCTIONAL and always compiled; only the on-screen report is diagnostic-only.
 *
 * Why the probe matters: fs_dopen() calls f_opendir(), and FatFs mounts a volume LAZILY, on first
 * access. Nothing else in the boot path opens a slot we do not already believe holds a device, so a
 * volume can sit unmounted purely because nobody asked -- and our publication path then finds
 * nothing and concludes the slot is empty. Touching each root once breaks that chicken-and-egg.
 * This is why the second USB stick appeared in diagnostic builds (which did this sweep) and not in
 * release builds (which did not): the measurement WAS the fix.
 *
 * Note fs_driver_resolve_volume() maps "mass" straight from the unit number without checking that
 * anything is mounted there, so Dopen on an empty slot is a cheap, safe no-op.
 *
 * The report half stays OPLDIAG-only -- it is raw developer text on a line every other caller feeds
 * through _l(). Compact by necessity: one token per slot, '-' absent, '?' open but no identity, or
 * the driver's first letter (u/a/s/i) followed by its device number.
 */
// Returns the number of slots that did NOT answer, so a caller can stop waiting early.
static int bdmProbeMassSlots(const char *when)
{
    char summary[96];
    int len;
    int i;
    int absent = 0;

    len = snprintf(summary, sizeof(summary), "MASS %s:", when);

    for (i = 0; i < MAX_BDM_DEVICES && len > 0 && len < (int)sizeof(summary); i++) {
        char root[16];
        char driver[32] = {0};
        int dir;
        int n;

        snprintf(root, sizeof(root), "mass%d:/", i);
        dir = fileXioDopen(root);
        if (dir < 0) {
            absent++;
            n = snprintf(summary + len, sizeof(summary) - len, " %d-", i);
        } else {
            int devnr = -1;
            // Guarded reader: a buffered ask on an unmounted volume faults the IOP outright.
            if (bdmReadDriverName(dir, driver, sizeof(driver)) >= 0 && driver[0] != '\0') {
                fileXioIoctl2(dir, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, &devnr, sizeof(devnr));
                n = snprintf(summary + len, sizeof(summary) - len, " %d%c%d", i, driver[0], devnr);
            } else {
                n = snprintf(summary + len, sizeof(summary) - len, " %d?", i);
            }
            fileXioDclose(dir);
        }

        if (n <= 0)
            break;
        len += n;
    }

    LOG("[BDM DIAG] %s\n", summary);
#ifdef __OPLDIAG
    host::ui().bootStatusStickyCopy(summary);
#endif

    return absent;
}

// BOOT-STAGE LABELS ARE DIAGNOSTIC-BUILD ONLY (OPLDIAG=1). They publish raw, untranslated developer
// text -- "BEGIN USBMASS_BD", "END DEV9/ATAD/XHDD result=1" -- straight to the boot status line that
// every other caller feeds through _l(). A plain release must show only localized boot steps, so
// these compile to nothing there; the diagnostic flavour is what a tester runs to localize a wedge.
#ifdef __OPLDIAG
static void bdmDiagBootStageBegin(const char *stage)
{
    char status[64];

    snprintf(status, sizeof(status), "BEGIN %s", stage);
    LOG("[BDM DIAG] %s\n", status);
    host::ui().bootStatusStickyCopy(status);
}

static void bdmDiagBootStageEnd(const char *stage, int result)
{
    char status[64];

    snprintf(status, sizeof(status), "END %s result=%d", stage, result);
    LOG("[BDM DIAG] %s\n", status);
    host::ui().bootStatusStickyCopy(status);
}
#else
static void bdmDiagBootStageBegin(const char *stage)
{
    (void)stage;
}

static void bdmDiagBootStageEnd(const char *stage, int result)
{
    (void)stage;
    (void)result;
}
#endif

static int bdmDiagLoadOptionalModule(const char *stage, const char *name, void *module, int moduleSize)
{
    int result;

    bdmDiagBootStageBegin(stage);
    result = bdmLoadOptionalModule(name, module, moduleSize);
    bdmDiagBootStageEnd(stage, result);
    return result;
}

static int bdmDiagLoadOptionalModuleArgs(const char *stage, const char *name, void *module, int moduleSize, int arglen, char *args)
{
    int result;

    bdmDiagBootStageBegin(stage);
    result = bdmLoadOptionalModuleArgs(name, module, moduleSize, arglen, args);
    bdmDiagBootStageEnd(stage, result);
    return result;
}

// Exposed for ethsupport's NIC mutual-exclusion: UDPBD and the SMB stack both own the SMAP adapter.
int bdmIsUDPBDLoaded(void)
{
    return udpbdModLoaded;
}

/*
  The network block transport actually resident (NET_BOOT_UDPBD / NET_BOOT_UDPFS). Falls back to the
  live gNetBootProtocol before anything has loaded, so first-boot labelling is unchanged.
*/
int bdmGetLoadedNetProtocol(void)
{
    return (udpbdLoadedProtocol >= 0) ? udpbdLoadedProtocol : gNetBootProtocol;
}

// Custom ELF paths may deliberately live on a network block device whose game page is disabled.
// Load exactly the requested wire protocol, never whichever protocol happens to be selected in the
// menu. The two protocols bind different sockets but share the single SMAP NIC, so an already
// resident SMB/UDPFS-filesystem stack -- or the OTHER block protocol -- makes this request
// impossible until reboot and must fail closed.
int bdmEnsureNetworkSourceModules(int protocol, u32 timeoutMs)
{
    char ipArg[24];
    char root[BDM_DEVICE_ROOT_MAX];
    int result = -1;
    u64 start;

    if (protocol != NET_BOOT_UDPBD && protocol != NET_BOOT_UDPFS)
        return 0;
    if (ethGetModulesLoaded() || udpfsGetModulesLoaded())
        return 0;

    // The transport IRX only publishes a BDM block device. A custom path needs the common BDM +
    // FatFs filesystem infrastructure too so that device can become an openable massN: mount.
    // Use the synchronous core-only path here: bdmLoadModules() would also queue every enabled
    // optional transport on the IO worker, which is unrelated to resolving this one explicit path.
    bdmLoadCoreModules(0);

    WaitSema(bdmLoadModuleLock);

    if (udpbdModLoaded) {
        int same = udpbdLoadedProtocol == protocol;
        SignalSema(bdmLoadModuleLock);
        if (!same)
            return 0;
    } else {
        sysInitDev9();
        snprintf(ipArg, sizeof(ipArg), "ip=%d.%d.%d.%d", ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3]);

        if (protocol == NET_BOOT_UDPFS) {
            result = bdmLoadOptionalModule("UDPFS_SMAP", &udpfs_smap_irx, size_udpfs_smap_irx);
            if (result >= 0)
                result = bdmLoadOptionalModuleArgs("UDPFS_MINISTACK", &udpfs_ministack_irx, size_udpfs_ministack_irx, (int)strlen(ipArg) + 1, ipArg);
            if (result >= 0)
                result = bdmLoadOptionalModule("UDPFS_BD", &udpfs_bd_irx, size_udpfs_bd_irx);
        } else {
            result = bdmLoadOptionalModuleArgs("SMAP_UDPBD", &smap_udpbd_irx, size_smap_udpbd_irx, (int)strlen(ipArg) + 1, ipArg);
        }

        if (result >= 0) {
            udpbdModLoaded = 1;
            udpbdLoadedProtocol = protocol;
        } else {
            sysShutdownDev9();
        }
        SignalSema(bdmLoadModuleLock);

        if (result < 0)
            return 0;
    }

    start = GetTimerSystemTime();
    while (!bdmGetDeviceRootByType(BDM_TYPE_UDPBD, root, sizeof(root))) {
        if ((GetTimerSystemTime() - start) / (kBUSCLK / 1000) >= timeoutMs)
            return 0;
        DelayThread(100 * 1000);
    }
    return 1;
}

// A typed custom ELF path (usb:/ata:/mx4sio:/ilink:) may name a family whose games page is off, so
// its transport may not be resident. Load the core and exactly that transport, never the boot-dir
// resolver's escalation ladder: a custom path that misses must not drag MX4SIO onto the pad bus or
// wake the ATA stack on a console that never asked for them. bdmEnsureSourceModules returns at once
// when the transport was already up, whether or not a device of the family is present.
int bdmEnsureTypedSource(int bdmType, u32 timeoutMs)
{
    bdmLoadCoreModules(0);
    return bdmEnsureSourceModules(bdmType, timeoutMs);
}

// True when this support's device is the UDPBD block device (its games are Neutrino-only).
// Returns 0 for non-BDM supports (incl. FAV-wrapped items, which have no source lookup here).
int bdmSupportIsUDPBD(const item_list_t *support)
{
    if (support == NULL || support->priv == NULL)
        return 0;
    if (support->mode < BDM_MODE || support->mode > BDM_MODE_LAST)
        return 0;
    return ((bdm_device_data_t *)support->priv)->bdmDeviceType == BDM_TYPE_UDPBD;
}

// True when this BDM mode slot is the UDPBD block device.
int bdmModeIsUDPBD(int mode)
{
    if (mode < BDM_MODE || mode > BDM_MODE_LAST)
        return 0;
    if (bdmDeviceList[mode - BDM_MODE].priv == NULL)
        return 0;
    return ((bdm_device_data_t *)bdmDeviceList[mode - BDM_MODE].priv)->bdmDeviceType == BDM_TYPE_UDPBD;
}

static void bdmEventHandler(void *packet, void *opt)
{
    const bdm_event_packet_t *event = (const bdm_event_packet_t *)packet;

    (void)opt;

    if (event != NULL)
        BdmUnsupportedSectorDrives = event->unsupportedSectorCount;

#ifdef __DEBUG
    if (event != NULL) {
        BdmDiagIopCallbackSequence = event->callbackSequence;
        BdmDiagIopMountCallbackCount = event->mountCallbackCount;
        BdmDiagIopUnmountCallbackCount = event->unmountCallbackCount;
        BdmDiagIopBlockDeviceCount = event->blockDeviceCount;
        BdmDiagIopUsbRootCount = event->usbRootCount;
        BdmDiagIopUsbRootMask = event->usbRootMask;
    }
#endif

    // A diagnostic snapshot reports IOP-side state without representing a storage change. Never
    // let the query itself bump BdmGeneration or alter the page-refresh decision.
    if (event != NULL && event->kind == BDMEVENT_PACKET_DIAG_SNAPSHOT) {
#ifdef __DEBUG
        BdmDiagIopSnapshotPending = 1;
#endif
        return;
    }

#ifdef __DEBUG
    BdmDiagEventPreviousGeneration = BdmGeneration;
    BdmDiagEventCause = event != NULL ? event->cause : -1;
    BdmDiagEventReceivedCount++;
#endif
    BdmGeneration++;
#ifdef __DEBUG
    BdmDiagEventGeneration = BdmGeneration;
    BdmDiagEventPending = 1;
#endif
}

#ifdef __DEBUG
void bdmDiagRequestIopSnapshot(const char *reason, int mode, int openResult)
{
    static SifCmdHeader_t query;
    int result;

    BdmDiagSnapshotRequestCount++;
    query.opt = BdmDiagSnapshotRequestCount;
    result = SifSendCmd(BDMEVENT_IOP_DIAG_QUERY_CMD, &query, sizeof(query), NULL, NULL, 0);
    LOG("[BDM COLD DIAG] event=iop-snapshot-request request=%u reason=%s generation=%u mode=%d open=%d send=%d\n",
        BdmDiagSnapshotRequestCount, reason, BdmGeneration, mode, openResult, result);
}

void bdmDiagFlushPendingIopState(item_list_t *itemList)
{
    if (BdmDiagEventPending) {
        unsigned int previousGeneration = BdmDiagEventPreviousGeneration;
        unsigned int generation = BdmDiagEventGeneration;
        int cause = BdmDiagEventCause;

        BdmDiagEventPending = 0;
        LOG("[BDM COLD DIAG] event=generation-change source=bdm-event generation=%u->%u "
            "eeReceived=%u cause=%d iopCallbacks=%u mountCallbacks=%u unmountCallbacks=%u "
            "blockDevices=%u usbRoots=%u usbRootMask=0x%08x itemList=%p mode=%d\n",
            previousGeneration, generation, BdmDiagEventReceivedCount, cause, BdmDiagIopCallbackSequence,
            BdmDiagIopMountCallbackCount, BdmDiagIopUnmountCallbackCount, BdmDiagIopBlockDeviceCount,
            BdmDiagIopUsbRootCount, BdmDiagIopUsbRootMask, itemList, itemList->mode);
    }

    if (BdmDiagIopSnapshotPending) {
        BdmDiagIopSnapshotPending = 0;
        LOG("[BDM COLD DIAG] event=iop-snapshot generation=%u eeReceived=%u iopCallbacks=%u "
            "mountCallbacks=%u unmountCallbacks=%u blockDevices=%u usbRoots=%u usbRootMask=0x%08x "
            "itemList=%p mode=%d\n",
            BdmGeneration, BdmDiagEventReceivedCount, BdmDiagIopCallbackSequence,
            BdmDiagIopMountCallbackCount, BdmDiagIopUnmountCallbackCount, BdmDiagIopBlockDeviceCount,
            BdmDiagIopUsbRootCount, BdmDiagIopUsbRootMask, itemList, itemList->mode);
    }
}
#endif

// Bump the device generation the per-device refresh latch keys off, exactly as a real hotplug event
// (bdmEventHandler) does. This invalidates every device's bdmDeviceTick so the next bdmNeedsUpdate
// pass re-evaluates presence + page visibility instead of short-circuiting on the latch. Called when
// the user applies Game Sources, so a BDM page that was force-hidden while its enable flag read 0
// (e.g. an exFAT ATA tab, or a UDPBD tab) re-shows as soon as the device is re-enabled and present --
// without needing a physical replug.
void bdmForceDeviceRefresh(void)
{
#ifdef __DEBUG
    unsigned int previousGeneration = BdmGeneration;
#endif
    BdmGeneration++;
#ifdef __DEBUG
    LOG("[BDM DIAG] event=generation-change source=forced-refresh generation=%u->%u itemList=%p mode=-1\n",
        previousGeneration, BdmGeneration, NULL);
#endif
}

// Read-only accessor so the menu hook can detect a real BDM device change (hotplug via bdmEventHandler
// or a Device-Settings apply) and bypass its background-rescan throttle for immediate detection.
// Plain single-word read, matching the existing non-atomic BdmGeneration read at bdmNeedsUpdate.
unsigned int bdmGetGeneration(void)
{
    return BdmGeneration;
}

// Runs UNDER the caller's menu lock: pure value copies, no I/O, no allocation. Everything the
// reporting calls below need must be taken here, because once the lock is released the item_list_t
// and config_set_t may be freed. Cheap enough to run unconditionally; the reporting is what costs.
void bdmCaptureConfigWriteDiag(item_list_t *itemList, const config_set_t *configSet, bdm_config_diag_snapshot_t *out)
{
#ifdef __DEBUG
    bdm_device_data_t *device;
    int slot;

    if (out == NULL)
        return;

    memset(out, 0, sizeof(*out));
    out->massDeviceIndex = -1;
    out->slot = -1;

    if (itemList == NULL)
        return;

    slot = bdmDiagListIndex(itemList);
    if (slot < 0 || itemList->priv == NULL)
        return; // not a BDM list: valid stays 0 and the reporting calls do nothing

    device = (bdm_device_data_t *)itemList->priv;

    out->valid = 1;
    out->slot = slot;
    out->mode = itemList->mode;
    out->visible = itemList->owner != NULL ? ((opl_io_module_t *)itemList->owner)->menuItem.visible : 0;
    out->generation = BdmGeneration;
    out->itemListAddr = itemList;
    out->configAddr = configSet;

    if (configSet != NULL) {
        out->configUid = configSet->uid();
        out->configModified = configSet->modified();
        out->configFormat = configSet->format();
        snprintf(out->configFilename, sizeof(out->configFilename), "%s",
                 configSet->filename() != NULL ? configSet->filename() : "<none>");
    } else {
        out->configModified = -1;
        out->configFormat = -1;
        snprintf(out->configFilename, sizeof(out->configFilename), "%s", "<none>");
    }

    snprintf(out->deviceRoot, sizeof(out->deviceRoot), "%s", device->bdmDeviceRoot);
    snprintf(out->devicePrefix, sizeof(out->devicePrefix), "%s", device->bdmPrefix);
    snprintf(out->driver, sizeof(out->driver), "%s", device->bdmDriver);
    out->massDeviceIndex = device->massDeviceIndex;
    out->bdmDeviceType = device->bdmDeviceType;
    out->foldersCreated = device->FoldersCreated;
    out->games = device->bdmGames;
    out->gameCount = device->bdmGameCount;
    out->ps1Games = device->bdmPs1Games;
    out->ps1GameCount = device->bdmPs1GameCount;
#else
    (void)itemList;
    (void)configSet;
    if (out != NULL)
        out->valid = 0;
#endif
}

// Capture-and-report in one step, for the in-module callers that run on the IO worker holding NO
// menu lock and owning the pointers they pass. Only the settings-save path needs the two-phase form,
// because only it must release a lock between the capture and the device probes.
void bdmLogIdentityDiagnosticLive(const char *event, item_list_t *itemList, const config_set_t *configSet)
{
#ifdef __DEBUG
    bdm_config_diag_snapshot_t snap;

    bdmCaptureConfigWriteDiag(itemList, configSet, &snap);
    bdmLogIdentityDiagnostic(event, &snap);
#else
    (void)event;
    (void)itemList;
    (void)configSet;
#endif
}

void bdmLogConfigWriteEntry(const bdm_config_diag_snapshot_t *snap)
{
    bdmLogIdentityDiagnostic("config-write-entry", snap);
}

void bdmLogConfigWriteResult(const bdm_config_diag_snapshot_t *snap, int result)
{
#ifdef __DEBUG
    if (snap == NULL || !snap->valid)
        return;

    LOG("[BDM DIAG] event=config-write-result generation=%u itemList=%p mode=%d config=%p uid=%u filename=\"%s\" result=%d cachedRoot=\"%s\" cachedDriver=\"%s\" cachedDevnr=%d type=%s(%d)\n",
        snap->generation, snap->itemListAddr, snap->mode, snap->configAddr, snap->configUid,
        snap->configFilename, result, snap->deviceRoot, snap->driver, snap->massDeviceIndex,
        bdmDeviceTypeName(snap->bdmDeviceType), snap->bdmDeviceType);
#else
    (void)snap;
    (void)result;
#endif
}

// Has this BDM slot EVER had a device on it? bdmUpdateDeviceData's connect pass is the only thing
// that fills bdmPrefix, so an empty prefix means no device has ever been seen in this slot this
// session. The menu hook uses it to stop paying a full fileXioDopen round-trip per empty slot on
// every background rescan -- on a one-stick rig that is 7 of the 8 probes, forever.
int bdmSlotEverConnected(int mode)
{
    if (mode < BDM_MODE || mode > BDM_MODE_LAST)
        return 0;

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)bdmDeviceList[mode - BDM_MODE].priv;
    if (pDeviceData == NULL)
        return 0;

    // bdmSeenMounted, not just bdmPrefix. The prefix is written only once the driver identity ioctl
    // has answered, and bdmUpdateDeviceData deliberately defers publication when a mount answers
    // Dopen before its transport can answer that ioctl -- routine when a second device enumerates
    // behind the first. Testing the prefix alone therefore reported that slot as never-connected and
    // dropped it to the empty-slot rotation (~8 s, and only while the user holds still), so a device
    // that WAS mounted and merely needed one more probe could sit unpublished until it was replugged
    // -- a hotplug bumps the generation and bypasses every gate, which is why replugging "fixed" it.
    return pDeviceData->bdmSeenMounted != 0 || pDeviceData->bdmPrefix[0] != '\0';
}

// Is this slot the MX4SIO card -- i.e. does its IO ride SIO2, the CONTROLLER'S OWN BUS?
//
// This is the one device where reading cover art actively costs the user input. mx4sio_bd shares
// SIO2 with freepad; under that traffic the pad drops to a reconnect state and initializePad() then
// runs six bounded waitPadReady calls ON THE GUI THREAD (src/pad.c:108-125). Hardware-confirmed
// 2026-08-12: with MX4SIO enabled the debug HUD showed a 4966 ms frame and a 43-poll run where the
// pad could not be read at all; with it disabled, clean. That is inherent to putting an SD driver on
// the pad's bus and is NOT a defect we can fix -- so the art path treats it as a device to be
// gentle with, rather than one to go faster on.
//
// Deliberately identity-based (bdmDeviceType), not slot-index based: which massN: slot MX4SIO lands
// on depends on attach order and changes between boots.
int bdmModeIsSIO2(int mode)
{
    if (mode < BDM_MODE || mode > BDM_MODE_LAST)
        return 0;

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)bdmDeviceList[mode - BDM_MODE].priv;
    return pDeviceData != NULL && pDeviceData->bdmDeviceType == BDM_TYPE_SDC;
}

static int bdmLoadUsbMassBd(void)
{
    int result;

    if (iUSBModLoaded)
        return 0;

    // Localized boot step, restored with the move into bdmLoadCoreModules. USBMASS_BD is the prime
    // suspect for a real exFAT/USB boot wedge, so it is the last step a stalled console should be
    // naming -- losing this left a wedge here reading as a stall in the PREVIOUS step (BDMFS_FATFS).
    host::ui().bootStatusSticky(_l(_STR_BOOT_LOADING_USB));
    bdmDiagBootStageBegin("USBMASS_BD");
    result = bdmLoadOptionalModule("USBMASS_BD", &usbmass_bd_irx, size_usbmass_bd_irx);
    bdmDiagBootStageEnd("USBMASS_BD", result);
    if (result >= 0) {
        iUSBModLoaded = 1;
        return 0;
    }
    return -1;
}

static int bdmShouldQueueModuleLoad(void)
{
    // Mirror each load gate in bdmLoadBlockDeviceModules. With USB switched off the pass never loads
    // it, so an ungated test here queued a pass that did nothing on every device event.
    if (gEnableUSB && !iUSBModLoaded)
        return 1;
    if (gEnableILK && !iLinkModLoaded)
        return 1;
    if (gEnableMX4SIO && !mx4sioModLoaded)
        return 1;
    if (gEnableBdmHDD && !hddModLoaded)
        return 1;
    if (gEnableUDPBD && !udpbdModLoaded && !ethGetModulesLoaded() && !udpfsGetModulesLoaded())
        return 1; // mirror the load gate -- if the SMB or udpfs-filesystem NIC is up, the block chain can't load

    return 0;
}

void bdmLoadBlockDeviceModules(void)
{
    // Boot-step localizer: this runs on the IO thread and can wedge on a real drive with no timeout,
    // freezing the splash. Publish the step so a hang here names it instead of the main thread's "Ready.".
    // (brenotomaz exFAT-USB boot hang investigation.)
    host::ui().bootStatusSticky(_l(_STR_BOOT_LOADING_DRIVERS));

    WaitSema(bdmLoadModuleLock);

    // Snapshot, compared after the loads below. See the refresh at the end of this function for why
    // a transport enabled from Settings needed a "double tap" before its tab appeared.
    int modsWere = iUSBModLoaded + iLinkModLoaded + mx4sioModLoaded + hddModLoaded + udpbdModLoaded;

    // Retry a synchronous bdmLoadCoreModules failure via the generation/worker path. Gated on the
    // setting for the same reason as the core load above: an off source must not become resident.
    if (gEnableUSB && !iUSBModLoaded) {
        host::ui().bootStatusSticky(_l(_STR_BOOT_LOADING_USB));
        bdmLoadUsbMassBd();
    }

    if (gEnableILK && !iLinkModLoaded) {
        // Load iLink Block Device drivers
        if (!iLinkManModLoaded && bdmDiagLoadOptionalModule("iLinkman", "ILINKMAN", &iLinkman_irx, size_iLinkman_irx) >= 0)
            iLinkManModLoaded = 1;
        if (iLinkManModLoaded && !ieee1394ModLoaded && bdmDiagLoadOptionalModule("IEEE1394_bd", "IEEE1394_BD", &IEEE1394_bd_irx, size_IEEE1394_bd_irx) >= 0)
            ieee1394ModLoaded = 1;

        iLinkModLoaded = iLinkManModLoaded && ieee1394ModLoaded;
    }

    if (gEnableMX4SIO && !mx4sioModLoaded) {
        // Load MX4SIO Block Device drivers
        if (bdmDiagLoadOptionalModule("MX4SIO", "MX4SIO_BD", &mx4sio_bd_irx, size_mx4sio_bd_irx) >= 0)
            mx4sioModLoaded = 1;
    }

    if (gEnableBdmHDD && !hddModLoaded) {
        // Load dev9 and atad device drivers.
        LOG("bdmLoadBlockDeviceModules loading hdd drivers...\n");
        // Only mark loaded on success -- a failure (no HDD/interface) must not
        // suppress future retries via bdmShouldQueueModuleLoad() (matches the
        // conditional pattern used for USB/MX4SIO and opl.c's hddLoadModules gate).
        bdmDiagBootStageBegin("DEV9/ATAD/XHDD");
        int hddResult = hddDiagLoadModulesReady();
        bdmDiagBootStageEnd("DEV9/ATAD/XHDD", hddResult);
        if (hddResult)
            hddModLoaded = 1;
    }

    // Network block device (UDPBD or UDPFS, picked by gNetBootProtocol). NIC-exclusive with the SMB/ETH
    // stack (smap registers "SMAP_driver"), so only load when SMB isn't up. dev9 is refcounted (shared
    // with ATA-HDD). Both need the PS2's static IP as an "ip=" arg -- the ministack has no DHCP client.
    if (gEnableUDPBD && !udpbdModLoaded && !ethGetModulesLoaded() && !udpfsGetModulesLoaded()) {
        char ipArg[24];
        int networkResult = -1;
        const char *networkStage = gNetBootProtocol == NET_BOOT_UDPFS ? "UDPFS-BD chain" : "UDPBD";

        bdmDiagBootStageBegin(networkStage);
        sysInitDev9();
        snprintf(ipArg, sizeof(ipArg), "ip=%d.%d.%d.%d", ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3]);
        if (gNetBootProtocol == NET_BOOT_UDPFS) {
            // UDPFS: a 3-IRX chain loaded in dependency order -- smap (exports to ministack + bd), then
            // ministack (gets the ip= arg, exports to bd), then udpfs_bd (registers the "udp" BDM device).
            networkResult = bdmDiagLoadOptionalModule("UDPFS_SMAP", "UDPFS_SMAP", &udpfs_smap_irx, size_udpfs_smap_irx);
            if (networkResult >= 0)
                networkResult = bdmDiagLoadOptionalModuleArgs("UDPFS_MINISTACK", "UDPFS_MINISTACK", &udpfs_ministack_irx, size_udpfs_ministack_irx, (int)strlen(ipArg) + 1, ipArg);
            if (networkResult >= 0)
                networkResult = bdmDiagLoadOptionalModule("UDPFS_BD", "UDPFS_BD", &udpfs_bd_irx, size_udpfs_bd_irx);
            if (networkResult >= 0) {
                udpbdModLoaded = 1;
                udpbdLoadedProtocol = NET_BOOT_UDPFS;
            }
        } else {
            // UDPBD: the self-contained smap_udpbd monolith (smap + ministack + udpbd in one irx).
            networkResult = bdmDiagLoadOptionalModuleArgs("SMAP_UDPBD", "SMAP_UDPBD", &smap_udpbd_irx, size_smap_udpbd_irx, (int)strlen(ipArg) + 1, ipArg);
            if (networkResult >= 0) {
                udpbdModLoaded = 1;
                udpbdLoadedProtocol = NET_BOOT_UDPBD;
            }
        }
        bdmDiagBootStageEnd(networkStage, networkResult);
        // Release the dev9 reference taken above if the load failed -- otherwise a failing/retrying
        // UDPBD/UDPFS (both gates re-enter on every device refresh while !udpbdModLoaded) inflates the
        // refcounted dev9InitCount and a later HDD/ETH teardown can never power dev9 down. On success
        // the reference is intentionally kept (the device stays mounted). Mirrors ETH/HDD pairing.
        if (!udpbdModLoaded)
            sysShutdownDev9();
    }

    int modsNow = iUSBModLoaded + iLinkModLoaded + mx4sioModLoaded + hddModLoaded + udpbdModLoaded;

    // BOOT PASS BUMPS NOTHING. On the first call every enabled transport loads for the first time, so
    // the comparison below is guaranteed true -- and bdmInitDevicesData is about to publish all of
    // them anyway. Bumping there bought nothing and cost a full immediate rescan of all eight slots
    // plus, since rebuild-155, a wipe of the missing-art memo. Reported from hardware as OPL "doing
    // the auto-hotplug thing" unprompted, on a console with MX4SIO switched off entirely -- i.e. the
    // fix for the MX4SIO double-tap was firing for transports that had nothing to do with it.
    //
    // The case that actually needed the bump is a LATE load: the user enabling a transport in Game
    // Sources, where the generation is bumped before the driver exists. Late passes are exactly the
    // non-first ones.
    static int moduleLoadFirstPassDone = 0;
    int wasFirstPass = !moduleLoadFirstPassDone;
    moduleLoadFirstPassDone = 1;

    SignalSema(bdmLoadModuleLock);

    // A DRIVER THAT ARRIVED LATE NEEDS ONE MORE LOOK, and without this it does not get one. Enabling
    // a transport from Game Sources runs, in this order:
    //
    //   guiShowDeviceSettings -> bdmForceDeviceRefresh()   generation bumps HERE
    //                         -> applyConfig -> bdmEnumerateDevices
    //                                        -> ioPutRequest(bdmLoadBlockDeviceModules)  queued
    //   ...IO worker, some time later...     -> THIS FUNCTION, driver finally loads
    //
    // The generation bump -- the thing that clears the "tab is hidden" latch and makes menuUpdateHook
    // look again -- happens BEFORE the driver exists. By the time the card mounts, everything that
    // would have noticed has already run and concluded there is nothing there, and the next bump
    // needs a real hotplug. Toggling the setting a second time supplies one, which is exactly the
    // "MX4SIO needs a double tap" behaviour Nathan hit: the driver was loaded the first time and
    // nothing ever asked again.
    //
    // Bumping on a load that actually SUCCEEDED closes the window for every transport, not just SDC.
    // Safe from this thread: bdmForceDeviceRefresh only increments the generation counter, and it is
    // called after SignalSema so it never runs under bdmLoadModuleLock.
    if (modsNow != modsWere && !wasFirstPass)
        bdmForceDeviceRefresh();

    // MX4SIO initial discovery ("double tap"): the SIO2 SD card may still be completing its
    // internal power-up / SPI handshake after mx4sio_bd_irx links. If MX4SIO is enabled and loaded,
    // check if it was immediately found. If not, perform one bounded settle (~600ms) on this IO worker
    // thread and issue exactly one second refresh. Strictly one-shot so an absent card never delays later calls.
    static int mx4sioSecondProbeDone = 0;
    if (!gEnableMX4SIO)
        mx4sioSecondProbeDone = 0;
    else if (mx4sioModLoaded && !mx4sioSecondProbeDone) {
        char sdcRoot[16];
        if (!bdmGetDeviceRootByType(BDM_TYPE_SDC, sdcRoot, sizeof(sdcRoot))) {
            mx4sioSecondProbeDone = 1;
            DelayThread(600 * 1000); // bounded settle for SIO2 SPI init
            bdmForceDeviceRefresh(); // exactly one second probe
        } else {
            mx4sioSecondProbeDone = 1; // found immediately -> done
        }
    }

    // USB initial discovery -- the SAME double tap, for the same reason, and USB needs it MORE than
    // MX4SIO does since fa1a18d4.
    //
    // USBMASS_BD is now loaded in bdmLoadCoreModules(), i.e. BEFORE this function runs. So
    // iUSBModLoaded is already set on entry, USB is counted in both modsWere and modsNow, and the
    // "a driver that arrived late needs one more look" refresh above can NEVER fire for it. USB
    // silently lost the mechanism every other transport still has.
    //
    // A stick has real enumeration latency of its own -- bus reset, SET_ADDRESS, descriptors, SCSI
    // INQUIRY/READ CAPACITY, then the FAT mount. The failure this produces is counter-intuitive and
    // matches the hardware report exactly: a SLOW stick finishes late, its mount event lands while
    // the device list and menu loop are up, and it publishes normally. A FAST stick finishes during
    // early init -- its event is spent before anything can act on it -- and then nothing ever asks
    // again, so it stays invisible until the user physically replugs it to manufacture a new event.
    //
    // One bounded settle and exactly one refresh, one-shot, so an absent or already-found stick
    // never costs a later call anything.
    // UNCONDITIONAL, unlike the MX4SIO probe above, and the difference is deliberate. MX4SIO asks
    // "did any card show up?" because there is only ever one. USB can have SEVERAL sticks, and the
    // reported failure is one of two going missing while the other is present -- so a
    // "did any USB root answer?" guard would find the working stick, conclude all is well, and skip
    // the very refresh the missing one needs. Ask again regardless of what is already mounted.
    /* ONE settle, one probe, one refresh -- deliberately NOT a retry loop.
     *
     * A four-pass version (4 x 700 ms) was tried and did not help: the stick that needs a replug
     * still needed one. That result matters more than the theory behind it, which was that a modern
     * high-speed stick negotiating down to the PS2's USB 1.1 controller simply needed longer. If
     * extra time were the missing ingredient, four passes would have found it.
     *
     * So do not reintroduce a loop or raise a pass count here. Up to 2.8 s of io-worker sleep at
     * every boot, on every console, is a real cost paid by everyone -- and it bought nothing. The
     * remaining fault is something other than "not enough time", and adding delay only hides how
     * little is understood about it. */
    static int usbSecondProbeDone = 0;
    if (iUSBModLoaded && !usbSecondProbeDone) {
        usbSecondProbeDone = 1;
        DelayThread(600 * 1000);               // bounded settle for USB enumeration + FAT mount
        bdmProbeMassSlots("after-usb-settle"); // forces FatFs's lazy mount on every slot
        bdmForceDeviceRefresh();               // exactly one second probe, covering every slot
    }
}

// Bring up only the common BDM infrastructure. Literal massN: boot resolution uses this path so an
// explicit filesystem slot never incidentally queues every transport enabled by the last settings
// load; it determines its backing driver from that slot's own devctl/ioctl identity instead.
// forceUsb: load the USB block driver even when the USB games source is off. Only the literal
// massN: boot resolver passes 1 -- it must be able to identify whatever slot the user actually
// booted from, and that is independent of which sources they browse.
void bdmLoadCoreModules(int forceUsb)
{
    LOG("BDMSUPPORT LoadModules\n");

    // Load Block Device Manager (BDM)
    LOG("[BDM]:\n");
    sysLoadModuleBuffer(&bdm_irx, size_bdm_irx, 0, NULL);

    // Load FATFS (mass:) driver
    LOG("[BDMFS_FATFS]:\n");
    sysLoadModuleBuffer(&bdmfs_fatfs_irx, size_bdmfs_fatfs_irx, 0, NULL);

    // ARM THE ATTACH HANDLER BEFORE ANY BLOCK DEVICE DRIVER CAN ENUMERATE. bdmevent registers a
    // bdm_RegisterCallback the moment it loads, and bdm only calls back for mounts that happen AFTER
    // that -- a device already mounted when the handler arrives generates no event, ever.
    //
    // Every other transport (iLink, MX4SIO, ATA, UDPBD) loads later, from bdmLoadBlockDeviceModules
    // on the io worker, so this ordering only ever mattered for USB -- and USB is precisely the one
    // that got moved up here. Loading USBMASS_BD first left a window where a stick that enumerated
    // quickly mounted before the handler existed: no attach event, so its page was left to the
    // empty-slot probe rotation (opl.c, gated on longIdle AND a frame ratio) or to the user
    // replugging it -- which is the "second USB device only shows up after a hotplug" report.
    // The hotplug works because it is the first event the handler is actually alive for.
    LOG("[BDMEVENT]:\n");
    sysLoadModuleBuffer(&bdmevent_irx, size_bdmevent_irx, 0, NULL);
    SifAddCmdHandler(BDMEVENT_EE_EVENT_CMD, &bdmEventHandler, NULL);

    /* USB goes AFTER the handler, per the ordering note above -- but it still HONOURS THE SETTING.
     *
     * rebuild-136 made this residency unconditional on the reasoning that upstream and Grimdoomer
     * load it synchronously rather than as an optional transport. That dropped the gEnableUSB gate
     * that had been here, so a console with the USB source switched OFF still paid for usbd plus
     * USBMASS_BD -- resident IOP memory taken from a budget that iLink, MX4SIO, ATAD, the network
     * stack and the cdvdman replacement all draw on. On a tight configuration the cost of that does
     * not land on USB; it lands on whichever module allocates next and fails, which presents as a
     * transport with no games rather than as anything pointing back here.
     *
     * Respecting the setting is also just correct: a user who turned USB off did not ask for it. */
    if (forceUsb || gEnableUSB)
        bdmLoadUsbMassBd();

#ifdef __DEBUG
    bdmDiagRequestIopSnapshot("handler-ready", -1, -1);
#endif

    LOG("BDMSUPPORT Modules loaded\n");
}

void bdmLoadModules(void)
{
    bdmLoadCoreModules(0); // normal enumeration: USB residency follows gEnableUSB

    // Normal enumeration retains the established asynchronous optional-transport load -- ONE pass.
    // Every BDM slot's bdmInit comes through here (eight at boot, then bdmEnumerateDevices asks a
    // ninth time), and each extra pass retried any transport that had failed: BDM HDD with no drive
    // was a full DEV9 + ATA load per pass before a single list was read. The pass reads the enable
    // flags when it runs, so a waiting one already covers these; its one-shot MX4SIO/USB double tap
    // is untouched.
    ioPutRequestUnlessWaiting(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&bdmLoadBlockDeviceModules));
}

static void bdmInit(item_list_t *itemList)
{
    LOG("BDMSUPPORT Init\n");

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    pDeviceData->bdmULSizePrev = -2;
    pDeviceData->bdmModifiedCDPrev = 0;
    pDeviceData->bdmModifiedDVDPrev = 0;
    pDeviceData->bdmGameCount = 0;
    pDeviceData->bdmGames = NULL;
    pDeviceData->bdmPs1GameCount = 0; // #120: separate VCD store (see bdm_device_data_t)
    pDeviceData->bdmPs1Games = NULL;
    pDeviceData->FoldersCreated = 0;
    pDeviceData->bdmDeviceType = BDM_TYPE_UNKNOWN;
    pDeviceData->bdmHddIsLBA48 = -1;
    pDeviceData->ataHighestUDMAMode = -1;
    itemList->delay = gArtDelay;
    bdmLoadModules();
    itemList->enabled = 1;
}

// Effective BDM device start mode. The UDPBD tab is not a mode-level tab like SMB -- it is a
// hotplug-published BDM mass page, which needs the BDM pages initialized, polled, and the bdm core
// modules loaded before smap_udpbd can even LINK. The unified network-protocol picker couples SMB to
// gETHStartMode and UDPFS to its own derived start mode, but selecting UDPBD/UDPFSBD wrote NOTHING to
// gBDMStartMode -- with the shipped Manual default (or Off), the UDPBD tab could NEVER appear unless
// the user happened to enter the generic BDM placeholder tab first ("UDPBD tab never shows", 2026-07-13).
// Floor the EFFECTIVE mode at AUTO while a BDM network transport is the selected protocol, mirroring
// the UDPFS_MODE derivation; the user's saved gBDMStartMode is never modified or persisted.
int bdmEffectiveStartMode(void)
{
    if (gEnableUDPBD && gBDMStartMode != START_MODE_AUTO)
        return START_MODE_AUTO;
    return gBDMStartMode;
}

// Per-transport enable flag for a classified BDM device type.
// Returns 1 = enabled, 0 = disabled, -1 = unclassifiable (generic/unknown driver: never force-hidden).
int bdmTransportEnabled(int bdmDeviceType)
{
    switch (bdmDeviceType) {
        case BDM_TYPE_USB:
            return gEnableUSB ? 1 : 0;
        case BDM_TYPE_ILINK:
            return gEnableILK ? 1 : 0;
        case BDM_TYPE_SDC:
            return gEnableMX4SIO ? 1 : 0;
        case BDM_TYPE_ATA:
            return gEnableBdmHDD ? 1 : 0;
        case BDM_TYPE_UDPBD:
            return gEnableUDPBD ? 1 : 0;
        default:
            return -1;
    }
}

// Once per rise in the number of refused drives: plugging one in says why it will not appear, and
// unplugging it re-arms the message (and withdraws it if it has not been shown yet).
static void bdmReportUnsupportedDrives(void)
{
    unsigned int count = BdmUnsupportedSectorDrives;

    if (count > BdmUnsupportedSectorReported)
        host::app().setError(_STR_BDM_UNSUPPORTED_SECTOR_SIZE_ERROR, ERROR_BDM_UNSUPPORTED_SECTOR_SIZE);
    else if (count == 0 && BdmUnsupportedSectorReported != 0)
        host::app().clearErrorIf(_STR_BDM_UNSUPPORTED_SECTOR_SIZE_ERROR);
    BdmUnsupportedSectorReported = count;
}

static int bdmNeedsUpdate(item_list_t *itemList)
{
    char path[256];
    int result = 0;
    struct stat st;

    // If we made it here then BDM device mode has been started.
    bdmDeviceModeStarted = 1;

    // If bdm mode is disabled bail out as we don't want to update the visibility state of the device pages.
    if (bdmEffectiveStartMode() == START_MODE_DISABLED)
        return 0;

    bdmReportUnsupportedDrives();

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    // Check for forced refresh from deleting or renaming a game.
    if (pDeviceData->ForceRefresh != 0) {
        pDeviceData->ForceRefresh = 0;
        return 1;
    }

    // If the device menu is visible double check the device type and if support for this device type is enabled. If the user switches device support
    // to off for a bdm device we want to hide the menu even though the drivers are still loaded and the device is being detected by bdm.
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    if (pOwner != NULL && pOwner->menuItem.visible == 1) {
        // If the device page is visible but the device support is not enabled, hide the device page.
        if (bdmTransportEnabled(pDeviceData->bdmDeviceType) == 0) {
            pOwner->menuItem.visible = 0;
            host::app().moduleUpdateMenu(itemList->mode, 0, 0);
        }
    }

    // VCD view: an L3 toggle marks the mode dirty but does NOT bump BdmGeneration or reset
    // bdmULSizePrev, so consume the dirty flag BEFORE the device-tick cache gate below (mirrors how
    // mmceNeedsUpdate checks it first). Otherwise a device already scanned this BdmGeneration -- which
    // every USB stick is, the moment its ISO list loads -- short-circuits at the gate and never
    // rescans, leaving the VCD (PS1/POPSTARTER) list permanently empty on USB/BDM while MMCE worked.
    if (host::library().consumeDirty(itemList->mode))
        return 1;

    // Folder browsing: a descend/ascend marks the mode dirty but bumps nothing the tick gate below
    // watches, so consume it here (same discipline as libViewConsumeDirty) or the deeper/shallower list
    // never rebuilds on a device already scanned this BdmGeneration.
    if (folderConsumeDirty(itemList->mode))
        return 1;

    // A NEVER-SCANNED slot (-2) skips this per-generation cache, INCLUDING while its page is still
    // invisible, so bdmUpdateDeviceData keeps polling it until the page is published: a mount can
    // answer Dopen before its identity ioctl is ready, and that deferral needs another look. Upstream
    // has the same bypass.
    //
    // It does NOT buy a rescan. A published page whose first scan failed (sbReadList keeps -2 when
    // neither CD nor DVD opens) returns on "no change" below, as upstream does, and bdmUpdateGameList
    // says which folder it could not open. The #186 rescue that rescanned such a page instead did it on
    // EVERY idle poll, forever, and each pass also rebuilt APPS and Favourites through
    // menuDeferredUpdate -- so every L3, on every page, queued behind it (FifthFox: USB and MX4SIO
    // both). SELECT, L3, a replug or a Game Sources apply is the retry.
    if (pDeviceData->bdmDeviceTick == BdmGeneration && pDeviceData->bdmULSizePrev != -2) {
        if (pOwner != NULL && pOwner->menuItem.visible == 0)
            return 0;
        // While a network page is inside its disconnect grace window (bdmMissCount > 0, set by the
        // presence-poll debounce in bdmUpdateDeviceData), keep re-polling each refresh so the debounce
        // advances to the hide threshold -- otherwise it would stall at the single disconnect event and
        // a truly-gone network page would never hide. Local devices always have bdmMissCount == 0 here.
        if (pDeviceData->bdmMissCount == 0)
            return 0;
    }
    pDeviceData->bdmDeviceTick = BdmGeneration;

    // ONE module-load attempt per device GENERATION, not one per rescan pass.
    //
    // bdmShouldQueueModuleLoad() is true while any ENABLED transport is not loaded, and
    // bdmLoadBlockDeviceModules sets its loaded latch only on SUCCESS. So a transport that is enabled
    // but cannot load -- BDM HDD with no drive, MX4SIO with no card, a stick that failed to attach --
    // queued a fresh attempt on EVERY pass, forever. The background rescan driving those passes is
    // gated on longIdle, i.e. it begins the moment the user stops moving, so the symptom is: settle
    // on a game and the loading spinner never clears, because the ioman queue is never observed
    // empty. Anything the user asks for then waits behind it -- which is why leaving the game-settings
    // screen could take seconds.
    //
    // ATA made it expensive as well as endless: its failure arm deliberately clears
    // hddModulesLoadCount so a later attempt is not poisoned (Vapor's report), so every pass re-ran
    // sysInitDev9() plus the whole atad/xhdd load.
    //
    // Keying on BdmGeneration keeps that retryability exactly where it was wanted and removes it where
    // it was not. The generation moves on a REAL device event -- bdmEventHandler's hotplug, or
    // bdmForceDeviceRefresh() when Game Sources is applied -- so plugging the drive in, or
    // toggling the transport, still gets an immediate retry. Sitting idle no longer buys one.
    static unsigned int lastModuleLoadGen = (unsigned int)-1;
    if (bdmShouldQueueModuleLoad() && lastModuleLoadGen != BdmGeneration) {
        // Stamp only on an ACCEPTED request, so one rejected during a teardown block retries on the
        // next pass instead of being silently skipped until the next hotplug.
        if (ioPutRequestUnlessWaiting(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&bdmLoadBlockDeviceModules)) == IO_OK)
            lastModuleLoadGen = BdmGeneration;
    }

    // Check if the device has been connected or removed. "No change" means nothing to do, including
    // for a page whose first scan failed (see the -2 note above).
    result = bdmUpdateDeviceData(itemList);
    if (result == 0)
        return 0;

    // If a device was added or removed play the appropriate UI sound.
    if (result == -1) {
        sfxPlay(SFX_BD_DISCONNECT);
        return result;
    } else if (result == 1)
        sfxPlay(SFX_BD_CONNECT);

    // Belt-and-suspenders: never create the library tree at an empty prefix (= the CWD/boot folder).
    if (!pDeviceData->FoldersCreated && pDeviceData->bdmPrefix[0] != '\0') {
        sbCreateFolders(pDeviceData->bdmPrefix, 1);
        pDeviceData->FoldersCreated = 1;
    }

    // VCD view: while this device shows its VCD list, skip the disc-folder heuristics (it refreshes on
    // L3 toggle / manual refresh only). The toggle's forced rescan is consumed ABOVE the device-tick
    // gate (see libViewConsumeDirty near the top) so the per-generation cache can't swallow it.
    // Return `result`, not 0: on the connect pass it is 1, and that pass is what builds a newly
    // attached device's first list. Returning 0 left a page whose remembered view was PS1 blank until
    // L3 or a view change forced a rescan (found while investigating FifthFox's empty BDM pages).
    if (host::library().listViewActive(itemList) == LIB_VIEW_PS1)
        return result;

    snprintf(path, sizeof(path), "%sCD", pDeviceData->bdmPrefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;
    if (pDeviceData->bdmModifiedCDPrev != st.st_mtime) {
        pDeviceData->bdmModifiedCDPrev = st.st_mtime;
        result = 1;
    }

    snprintf(path, sizeof(path), "%sDVD", pDeviceData->bdmPrefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;
    if (pDeviceData->bdmModifiedDVDPrev != st.st_mtime) {
        pDeviceData->bdmModifiedDVDPrev = st.st_mtime;
        result = 1;
    }

    if (!sbIsSameSize(pDeviceData->bdmPrefix, pDeviceData->bdmULSizePrev))
        result = 1;

    // update Themes
    if (!pDeviceData->ThemesLoaded) {
        snprintf(path, sizeof(path), "%sTHM", pDeviceData->bdmPrefix);
        if (thmAddElements(path, "/", 1) > 0)
            pDeviceData->ThemesLoaded = 1;
    }

    // update Languages
    if (!pDeviceData->LanguagesLoaded) {
        snprintf(path, sizeof(path), "%sLNG", pDeviceData->bdmPrefix);
        if (lngAddLanguages(path, "/", itemList->mode) > 0)
            pDeviceData->LanguagesLoaded = 1;
    }

    return result;
}

static int bdmUpdateGameList(item_list_t *itemList)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    int view = host::library().listViewActive(itemList);
    int result = 0;

    // #120: each view scans into its OWN array (see bdm_device_data_t). Previously both shared
    // bdmGames, so a VCD scan reporting failure (r < 0) left the ISO list published under the VCD
    // view -- the L3 toggle then "never changed the list" (Nathan, HW, ATA/HDD_BD), and a device with
    // no POPS folder hit that on every single toggle.
    if (view == LIB_VIEW_PS1 || view == LIB_VIEW_MIXED) {
        char ps1Prefix[BDM_DEVICE_ROOT_MAX + 2];
        bdmBuildPs1Prefix(ps1Prefix, sizeof(ps1Prefix), itemList->mode); // device root, NOT gBDMPrefix
        // Ordinary BDM devices merge both cores. UDPBD deliberately publishes Ember ONLY:
        // POPSTARTER resets the IOP and cannot restore that network block device, while Ember keeps
        // the live mount. A listed row must never be a title this transport cannot launch.
        int r = (pDeviceData->bdmDeviceType == BDM_TYPE_UDPBD) ?
                    cueFillGameList(ps1Prefix, &pDeviceData->bdmPs1Games) :
                    ps1FillGameList(ps1Prefix, &pDeviceData->bdmPs1Games);
        if (r >= 0) // r < 0: transient scan failure -> keep THIS view's last-good (empty if never scanned)
            pDeviceData->bdmPs1GameCount = r;
        result += pDeviceData->bdmPs1GameCount;
    }
    if (view == LIB_VIEW_ISO || view == LIB_VIEW_MIXED) {
        const char *sub = folderGetSub(itemList->mode);
        sbReadList(&pDeviceData->bdmGames, pDeviceData->bdmPrefix, sub, &pDeviceData->bdmULSizePrev, &pDeviceData->bdmGameCount);
        // Still -2 after a root scan = neither CD nor DVD would open and there is no ul.cfg. Say where
        // it looked rather than leave a silently empty page. Only a connect, SELECT, L3 or
        // delete/rename pass scans, so this cannot repeat on its own.
        if (pDeviceData->bdmULSizePrev == -2 && sub[0] == '\0' && pDeviceData->bdmPrefix[0] != '\0')
            host::app().setErrorPath(_STR_BDM_PS2_FOLDERS_UNREADABLE, pDeviceData->bdmPrefix, sbGetReadListError());
        result += pDeviceData->bdmGameCount;
    }
    return result;
}

static int bdmGetGameCount(item_list_t *itemList)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    int view = host::library().listViewActive(itemList);
    return view == LIB_VIEW_MIXED ? pDeviceData->bdmGameCount + pDeviceData->bdmPs1GameCount :
           view == LIB_VIEW_PS1   ? pDeviceData->bdmPs1GameCount :
                                    pDeviceData->bdmGameCount;
}

int bdmGetItemView(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    int view = host::library().listViewActive(itemList);

    if (view == LIB_VIEW_MIXED)
        return id >= 0 && id < pDeviceData->bdmGameCount ? LIB_VIEW_ISO : LIB_VIEW_PS1;
    return view;
}

static int bdmGetSourceId(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    return host::library().listViewActive(itemList) == LIB_VIEW_MIXED && id >= pDeviceData->bdmGameCount ? id - pDeviceData->bdmGameCount : id;
}

// Split-store guard (mirrors mmceActiveGame). L3 now stages the destination until the old submenu is
// cleared, so its ordinary handoff cannot cross-wire row ids. Arrays can still be NULL (never scanned),
// shorter after a failed/partial refresh, or absent during teardown. Resolve EVERY id through here:
// an out-of-range id yields a STATIC EMPTY entry, so a stale read is safe empty data and
// launch/delete/rename treat &bdmEmptyGame as "nothing there". (The shared store this replaced could
// not OOB: one array + one count stayed self-consistent -- splitting them makes this guard mandatory.)
base_game_info_t bdmEmptyGame = {0};
base_game_info_t *bdmActiveGame(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    int vcd = (bdmGetItemView(itemList, id) == LIB_VIEW_PS1);
    id = bdmGetSourceId(itemList, id);
    base_game_info_t *arr = vcd ? pDeviceData->bdmPs1Games : pDeviceData->bdmGames;
    int count = vcd ? pDeviceData->bdmPs1GameCount : pDeviceData->bdmGameCount;

    if (arr == NULL || id < 0 || id >= count)
        return &bdmEmptyGame;
    return &arr[id];
}

static void *bdmGetGame(item_list_t *itemList, int id)
{
    return (void *)bdmActiveGame(itemList, id);
}

static char *bdmGetGameName(item_list_t *itemList, int id)
{
    return bdmActiveGame(itemList, id)->name;
}

static int bdmGetGameNameLength(item_list_t *itemList, int id)
{
    base_game_info_t *g = bdmActiveGame(itemList, id);

    return ((g->format != GAME_FORMAT_USBLD) ? ISO_GAME_NAME_MAX + 1 : UL_GAME_NAME_MAX + 1);
}

static char *bdmGetGameStartup(item_list_t *itemList, int id)
{
    base_game_info_t *g = bdmActiveGame(itemList, id);

    // VCD view: identity is the filename, not a disc ID -> per-game data (CFG/art) keys off the
    // VCD name (matches sbPopulateConfig).
    if (bdmGetItemView(itemList, id) == LIB_VIEW_PS1)
        return g->name;
    return g->startup;
}

static void bdmDeleteGame(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    if (bdmGetItemView(itemList, id) == LIB_VIEW_PS1)
        return; // #120: a VCD is not an ISO game -- no delete in VCD view (mirrors mmceDeleteGame)
    if (bdmActiveGame(itemList, id) == &bdmEmptyGame)
        return;                                   // stale/invalid id: sbDelete does NOT bounds-check -> avoid an OOB/wrong unlink
    sbSetBrowseSub(folderGetSub(itemList->mode)); // delete inside the current subfolder, not the root
    id = bdmGetSourceId(itemList, id);
    sbDelete(&pDeviceData->bdmGames, pDeviceData->bdmPrefix, "/", pDeviceData->bdmGameCount, id);
    pDeviceData->bdmULSizePrev = -2;
    pDeviceData->ForceRefresh = 1;
}

static void bdmRenameGame(item_list_t *itemList, int id, char *newName)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    if (bdmGetItemView(itemList, id) == LIB_VIEW_PS1) {
        base_game_info_t *game = bdmActiveGame(itemList, id);
        char vcdPrefix[BDM_DEVICE_ROOT_MAX + 2];

        if (game == &bdmEmptyGame)
            return; // stale/invalid id
        bdmBuildPs1Prefix(vcdPrefix, sizeof(vcdPrefix), itemList->mode);
        // The ROW picks the target, as the launch does. Dispatching on the view would hand an Ember
        // row to vcdRenameFile, which looks in POPS/ for "<name>.VCD" -- absent for an Ember title,
        // so the rename would quietly do nothing; and where a .VCD of the same name exists (a game
        // held for BOTH cores, which the merged list makes an ordinary case) it would rename the
        // POPSTARTER file instead. Renaming the wrong file is the worse half of that.
        if ((cueIsCueEntry(game) ? cueRenameGame(vcdPrefix, game->name, newName) : vcdRenameFile(vcdPrefix, game->name, newName)) == 0)
            pDeviceData->ForceRefresh = 1; // the queued menu update re-scans both libraries
        return;
    }
    if (bdmActiveGame(itemList, id) == &bdmEmptyGame)
        return; // stale/invalid id (see bdmDeleteGame) -> avoid sbRename OOB
    id = bdmGetSourceId(itemList, id);
    sbSetBrowseSub(folderGetSub(itemList->mode)); // rename inside the current subfolder, not the root
    sbRename(&pDeviceData->bdmGames, pDeviceData->bdmPrefix, "/", pDeviceData->bdmGameCount, id, newName);
    pDeviceData->bdmULSizePrev = -2;
    pDeviceData->ForceRefresh = 1;
}


static config_set_t *bdmGetConfig(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    config_set_t *config;

    // Active view's array (#120 split) -- a VCD's per-game CFG keys off its own entry, and a stale
    // id resolves to the empty entry instead of indexing outside either backing array.
    config = sbPopulateConfig(bdmActiveGame(itemList, id), pDeviceData->bdmPrefix, "/");
    bdmLogIdentityDiagnosticLive("config-create", itemList, config);
    return config;
}

static int bdmGetImage(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char path[256];

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    if (isRelative)
        snprintf(path, sizeof(path), "%s%s/%s_%s", pDeviceData->bdmPrefix, folder, value, suffix);
    else
        snprintf(path, sizeof(path), "%s%s_%s", folder, value, suffix);
    int r = texDiscoverLoad(resultTex, path, -1);
    // ART LIVES IN THE ART FOLDER, and nowhere else. A PS1 cover is
    // ART/<name>_COV.png -- the same rule as a PS2 title -- which the texDiscoverLoad
    // above already tried. A miss is simply "no cover": there is no second directory to
    // search, which also means a missing cover costs one failed open instead of several.
    return r;
}

static int bdmGetTextId(item_list_t *itemList)
{
    int mode = _STR_BDM_GAMES;

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    if (bdmDriverIsUSB(pDeviceData->bdmDriver))
        mode = _STR_USB_GAMES;
    else if (bdmDriverIsIlink(pDeviceData->bdmDriver))
        mode = _STR_ILINK_GAMES;
    else if (bdmDriverIsMx4sio(pDeviceData->bdmDriver))
        mode = _STR_MX4SIO_GAMES;
    else if (bdmDriverIsATA(pDeviceData->bdmDriver))
        mode = _STR_HDD_GAMES;
    else if (bdmDriverIsUDPBD(pDeviceData->bdmDriver))
        mode = (bdmGetLoadedNetProtocol() == NET_BOOT_UDPFS) ? _STR_UDPFSBD_GAMES : _STR_UDPBD_GAMES; // BLOCK: UDPFSBD vs UDPBD (the udpfs FILESYSTEM tab is _STR_UDPFS_GAMES, a separate device); mirror bdmGetIconId

    return mode;
}

static int bdmGetIconId(item_list_t *itemList)
{
    int mode = BDM_ICON;

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    if (bdmDriverIsUSB(pDeviceData->bdmDriver))
        mode = USB_ICON;
    else if (bdmDriverIsIlink(pDeviceData->bdmDriver))
        mode = ILINK_ICON;
    else if (bdmDriverIsMx4sio(pDeviceData->bdmDriver))
        mode = MX4SIO_ICON;
    else if (bdmDriverIsATA(pDeviceData->bdmDriver))
        mode = HDD_BD_ICON;
    else if (bdmDriverIsUDPBD(pDeviceData->bdmDriver))
        mode = (bdmGetLoadedNetProtocol() == NET_BOOT_UDPFS) ? UDPFS_ICON : UDP_ICON;

    return mode;
}

// This may be called, even if bdmInit() was not.
static void bdmCleanUp(item_list_t *itemList, int exception)
{
    if (itemList->enabled) {
        LOG("BDMSUPPORT CleanUp\n");

        bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
        free(pDeviceData->bdmGames);
        free(pDeviceData->bdmPs1Games); // #120: free the separate VCD store too
        free(pDeviceData);
        itemList->priv = NULL;

        //      if ((exception & UNMOUNT_EXCEPTION) == 0)
        //          ...
    }
}

// This may be called, even if bdmInit() was not.
static void bdmShutdown(item_list_t *itemList)
{
    char path[BDM_DEVICE_ROOT_MAX];
    LOG("BDMSUPPORT Shutdown\n");

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    snprintf(path, sizeof(path), "mass%d:", itemList->mode);

    // As required by some (typically 2.5") HDDs, issue the SCSI STOP UNIT command to avoid causing an emergency park.
    // pDeviceData may be NULL here (shutdown without init, or a second deinit pass after priv was freed below),
    // so guard the dereference and fall back to the constructed mass%d: path.
    //
    // TERMINAL TEARDOWN ONLY. Parking the heads is a poweroff courtesy, and moduleCleanup sends
    // every NON-selected page down this path -- so on a launch it used to spin down the other BDM
    // devices while the handoff that follows still had to read from them. hddShutdown carries the
    // same gate for the same reason (DEV9 off here once killed the ATA bus before a post-deinit
    // POPSTARTER read). Worst on an external enclosure, which may not spin back up.
    if (gDeinitTerminal)
        fileXioDevctl((pDeviceData != NULL && pDeviceData->bdmDeviceRoot[0] != '\0') ? pDeviceData->bdmDeviceRoot : path, USBMASS_DEVCTL_STOP_ALL, NULL, 0, NULL, 0);
    else
        LOG("BDMSUPPORT Shutdown: launch teardown -- NOT stopping %s\n",
            (pDeviceData != NULL && pDeviceData->bdmDeviceRoot[0] != '\0') ? pDeviceData->bdmDeviceRoot : path);

    if (itemList->enabled && pDeviceData != NULL) {
        LOG("BDMSUPPORT Shutdown free data\n");

        // Free device data.
        free(pDeviceData->bdmGames);
        free(pDeviceData->bdmPs1Games); // both stores, same as bdmCleanUp -- the asymmetry leaked
        free(pDeviceData);
        itemList->priv = NULL;
    }
}

static int bdmCheckVMC(item_list_t *itemList, char *name, int createSize)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    return sysCheckVMC(pDeviceData->bdmPrefix, "/", name, createSize, NULL);
}

static char *bdmGetPrefix(item_list_t *itemList)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    return pDeviceData->bdmPrefix;
}

item_list_t bdmGameList = {
    BDM_MODE, 2, 0, 0, MENU_MIN_INACTIVE_FRAMES, BDM_MODE_UPDATE_DELAY, NULL, NULL, &bdmGetTextId, &bdmGetPrefix, &bdmInit, &bdmNeedsUpdate,
    &bdmUpdateGameList, &bdmGetGameCount, &bdmGetGame, &bdmGetGameName, &bdmGetGameNameLength, &bdmGetGameStartup, &bdmDeleteGame, &bdmRenameGame,
    &bdmLaunchGame, &bdmGetConfig, &bdmGetImage, &bdmCleanUp, &bdmShutdown, &bdmCheckVMC, &bdmGetIconId, &bdmLaunchVcd,
    /* viewOverride */ 0, /* itemGetArtArchivePath */ NULL, &bdmLaunchCue, &bdmGetItemView, &bdmGetSourceId};



static int bdmDeviceIsATA(int deviceId)
{
    char path[16];
    bdm_device_data_t data;

    snprintf(path, sizeof(path), "mass%d:/", deviceId);
    int dir = fileXioDopen(path);
    if (dir < 0)
        return 0;

    /* Zero the buffer before the ioctl: if the call fails, strcmp would read
     * uninitialised stack bytes. Pattern mirrors bdmReadDeviceIdentity(). */
    memset(&data.bdmDriver, 0, sizeof(data.bdmDriver));
    if (bdmReadDriverName(dir, data.bdmDriver, sizeof(data.bdmDriver)) < 0) {
        fileXioDclose(dir);
        return 0;
    }
    fileXioDclose(dir);

    return (!strcmp(data.bdmDriver, "ata") && strlen(data.bdmDriver) == 3);
}

static int bdmGetATADeviceId()
{
    for (int i = 0; i < MAX_BDM_DEVICES; i++) {
        if (bdmDeviceIsATA(i)) {
            return i;
        }
    }
    return -1;
}

int bdmHDDIsPresent(u32 timeoutMs)
{
    int hdd_id = -1;
    int timedout = 0;

    if (!hddIsPresent())
        return 0;

    // 1. scan via normal methods first...
    hdd_id = bdmGetATADeviceId();
    if (hdd_id >= 0)
        return 1;

    // 2. try to scan as fast as possible if the previous scan fails...
    hdd_id = -1;

    for (int i = 0; i < MAX_BDM_DEVICES; i++) {
        // find the first inaccessible device - this one should be the HDD once it's mounted (we don't have access to device data at this point yet!)
        if (!bdmDeviceIsPresent(i)) {
            hdd_id = i;
            break;
        }
    }

    if (hdd_id < 0) {
        // All slots are already accessible; no inaccessible slot to poll.
        // Skip the targeted wait and rescan immediately.
        timedout = 1;
    } else if (bdmWaitForDevice(hdd_id, timeoutMs)) {
        // double-check to see if this indeed is the HDD, and if it is, we can exit early without stalling any further
        if (bdmDeviceIsATA(hdd_id)) {
            return 1;
        } else
            LOG("bdmHDDIsPresent: device at id %d is not an ATA HDD...\n", hdd_id);
    } else {
        timedout = 1;
        LOG("bdmHDDIsPresent: waiting for hdd at id %d timed out...\n", hdd_id);
    }

    // 3. last resort - time out (if needed) and scan again...
    if (!timedout) {
        // if we haven't timed out already, then we need to wait for the devices to wake up... wait for the timeout...
        LOG("bdmHDDIsPresent: waiting for timeout before scanning again (id %d)...\n", hdd_id);
        DelayThread(timeoutMs * 1000);
    }

    return bdmGetATADeviceId() >= 0;
}


int bdmFindPartition(char *target, const char *name, int write)
{
    int i, fd;
    char path[256];

    /* Probe massN:/<prefix>/<name> on each BDM device for the config file, and
     * return the matching device directory prefix in target so configInit()/
     * configSetMove() can compose "<target>/<filename>".  The format strings
     * MUST carry %d/%s specifiers: the previous version used literal "mass0:/"
     * strings with leftover varargs, which the compiler silently discards, so
     * the loop never iterated devices and the prefix/filename were never
     * appended (BDM per-device config load/save was non-functional and only
     * ever touched mass0's root).  gBDMPrefix is a bounded folder name
     * (char[32]) and target buffers are 64 bytes, so the device-prefix writes
     * cannot overflow. */
    for (i = 0; i < MAX_BDM_DEVICES; i++) {
        if (gBDMPrefix[0] != '\0')
            snprintf(path, sizeof(path), "mass%d:/%s/%s", i, gBDMPrefix, name);
        else
            snprintf(path, sizeof(path), "mass%d:/%s", i, name);
        if (write) {
            /* O_CREAT makes the config FILE but never the gBDMPrefix FOLDER.  On a freshly
             * formatted drive (e.g. an exFAT HDD whose OPL folder doesn't exist yet) the very
             * first save would therefore fail with the folder absent, and only start working
             * once sbCreateFolders() happened to create it on a later device refresh -- exactly
             * the "save errored, then saved fine after a try or two" symptom.  Create the prefix
             * directory up front (mkdir is a harmless no-op when it already exists) so the first
             * save succeeds.  Probe with O_CREAT but WITHOUT O_TRUNC: this open only tests for a
             * writable device, so it must never truncate an existing config -- configWrite()
             * truncates when it actually commits the data. */
            if (gBDMPrefix[0] != '\0') {
                char dir[256];
                snprintf(dir, sizeof(dir), "mass%d:/%s", i, gBDMPrefix);
                sbMakeDirTree(dir);
            }
            fd = open(path, O_WRONLY | O_CREAT, 0666);
        } else
            fd = open(path, O_RDONLY);

        if (fd >= 0) {
            if (gBDMPrefix[0] != '\0')
                sprintf(target, "mass%d:/%s", i, gBDMPrefix);
            else
                sprintf(target, "mass%d:", i);
            close(fd);
            return 1;
        }
    }

    if (gBDMPrefix[0] != '\0')
        sprintf(target, "mass0:/%s", gBDMPrefix);
    else
        sprintf(target, "mass0:");
    return 0;
}
