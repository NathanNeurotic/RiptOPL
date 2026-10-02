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

// BDM device table: slot discovery, identity/geometry refresh and the wait-for-device helpers. Split out of bdmsupport.cpp.

void bdmInitSemaphore()
{
    // Create a semaphore so only one thread can load IOP modules at a time.
    ee_sema_t semaphore;
    semaphore.init_count = 1;
    semaphore.max_count = 1;
    semaphore.option = 0;
    bdmLoadModuleLock = CreateSema(&semaphore);
}

// This slot's device is mounted and fully identified, with a published list. Such a page is kept
// through a settings re-apply (bdmInitDevicesData) and restored as-is when it reappears
// (bdmUpdateDeviceData) -- nothing about the device changed, so there is nothing to hide or re-read.
static int bdmSlotIdentified(const bdm_device_data_t *pDeviceData)
{
    return pDeviceData != NULL && pDeviceData->bdmDeviceRoot[0] != '\0' && pDeviceData->bdmDriver[0] != '\0' &&
           pDeviceData->bdmDeviceType != BDM_TYPE_UNKNOWN && pDeviceData->bdmULSizePrev != -2;
}

void bdmInitDevicesData()
{
    // If the device list hasn't been initialized do it now.
    if (bdmDeviceListInitialized == 0) {
        bdmDeviceListInitialized = 1;

        for (int i = 0; i < MAX_BDM_DEVICES; i++) {
            // Setup the device list item.
            item_list_t *pDeviceSupport = &bdmDeviceList[i];
            memcpy(pDeviceSupport, bdmTemplateList(), sizeof(item_list_t));
            pDeviceSupport->mode = i;

            // Setup the per-device data.
            bdm_device_data_t *pDeviceData = (bdm_device_data_t *)malloc(sizeof(bdm_device_data_t));
            if (pDeviceData != NULL) // guard the memset on OOM; bdmShutdown tolerates a NULL priv
                memset(pDeviceData, 0, sizeof(bdm_device_data_t));
            pDeviceSupport->priv = pDeviceData;
        }
    }

    // Refresh the visibility of the menu.
    for (int i = 0; i < MAX_BDM_DEVICES; i++) {
        // Register the device structure into the UI.
        host::app().initSupport(&bdmDeviceList[i], i, 0);

        // If bdm support is set to auto then make the page invisible and reset the bdm tick counter, when a bdm device is mounted it will dynamically be made visible.
        // If bdm support is set to manual then only make the first page visible.
        //
        // A page whose device is still mounted and identified is NOT hidden here, only re-checked (tick
        // reset). This runs on every settings apply (applyConfig -> initAllSupport), and hiding the page
        // the user is on sent them, on return from the Start menu, to the first visible tab
        // (refreshMenuPosition) -- APPS, Favourites -- until the next device poll brought the page back
        // (#806, zackcage6, Beta-3383). The poll still hides it if the device is gone (the debounced
        // removal path) or its transport was switched off (bdmNeedsUpdate).
        if (bdmDeviceList[i].owner != NULL) {
            opl_io_module_t *pOwner = (opl_io_module_t *)bdmDeviceList[i].owner;
            bdm_device_data_t *pDeviceData = (bdm_device_data_t *)bdmDeviceList[i].priv;

            int effectiveMode = bdmEffectiveStartMode();
            if (effectiveMode == START_MODE_DISABLED) {
                pOwner->menuItem.visible = 0;
            } else if (effectiveMode == START_MODE_MANUAL) {
                // If BDM has already been started then make the page invisible and reset the bdm tick counter so visibility status is refreshed
                // according to device state.
                if (bdmDeviceModeStarted == 1) {
                    if (!bdmSlotIdentified(pDeviceData))
                        pOwner->menuItem.visible = 0;
                    pDeviceData->bdmDeviceTick = -1;
                } else
                    pOwner->menuItem.visible = (i == 0 ? 1 : 0);
            } else if (effectiveMode == START_MODE_AUTO) {
                if (!bdmSlotIdentified(pDeviceData))
                    pOwner->menuItem.visible = 0;
                pDeviceData->bdmDeviceTick = -1;
            }

            LOG("bdmInitDevicesData: setting device %d %s\n", i, (pOwner->menuItem.visible != 0 ? "visible" : "invisible"));
        }
    }
}

void bdmEnumerateDevices()
{
    LOG("bdmEnumerateDevices\n");

    // Optional transports must remain asynchronous. A synchronous MX4SIO mount/identity wait here
    // makes Auto startup depend on an IOP RPC returning on the GUI thread, where a real device wedge
    // can freeze the splash indefinitely. bdmUpdateDeviceData keeps a mounted-but-unidentified slot
    // unpublished and retries it on the deferred IO worker instead.

    // Initialize the device list data if it hasn't been initialized yet.
    bdmInitDevicesData();

    // Because bdmLoadModules is called before the config file is loaded bdmLoadBlockDeviceModules will not have loaded any
    // optional bdm modules. Now that the config file has been loaded try loading any optional modules that weren't previously loaded.
    // A pass still waiting reads the loaded config when it runs, so it is not queued twice.
    ioPutRequestUnlessWaiting(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&bdmLoadBlockDeviceModules));

    LOG("bdmEnumerateDevices done\n");
}

void bdmResolveLBA_UDMA(bdm_device_data_t *pDeviceData)
{
    // If atad is loaded then xhdd is also loaded, query the hdd to see if it supports LBA48 or not.
    pDeviceData->bdmHddIsLBA48 = fileXioDevctl("xhdd0:", ATA_DEVCTL_IS_48BIT, NULL, 0, NULL, 0);
    if (pDeviceData->bdmHddIsLBA48 < 0) {
        // Failed to query the LBA limit of the device, fail safe to LBA28.
        LOG("Mass device %d is backed by ATA but failed to get LBA limit %d\n", pDeviceData->massDeviceIndex, pDeviceData->bdmHddIsLBA48);
        pDeviceData->bdmHddIsLBA48 = 0;
    }

    // Query the drive for the highest UDMA mode.
    pDeviceData->ataHighestUDMAMode = fileXioDevctl("xhdd0:", ATA_DEVCTL_GET_HIGHEST_UDMA_MODE, NULL, 0, NULL, 0);
    if (pDeviceData->ataHighestUDMAMode < 0) {
        // Failed to query highest UDMA mode supported.
        LOG("Mass device %d is backed by ATA but failed to get highest UDMA mode %d\n", pDeviceData->massDeviceIndex, pDeviceData->ataHighestUDMAMode);
        pDeviceData->ataHighestUDMAMode = 4;
    } else if (pDeviceData->ataHighestUDMAMode > 4) {
        // Limit max UDMA mode to 4 to avoid compatibility issues
        LOG("Mass device %d supports up to UDMA mode %d, limiting to UDMA 4\n", pDeviceData->massDeviceIndex, pDeviceData->ataHighestUDMAMode);
        pDeviceData->ataHighestUDMAMode = 4;
    }
}

int bdmUpdateDeviceData(item_list_t *itemList)
{
    char path[BDM_DEVICE_ROOT_MAX + 2] = {0};
    int driverResult, deviceResult;

#ifdef __DEBUG
    bdmDiagFlushPendingIopState(itemList);
#endif

    // If bdm mode is disabled bail out as we don't want to update the visibility state of the device pages.
    if (bdmEffectiveStartMode() == START_MODE_DISABLED)
        return 0;

    // LOG("bdmUpdateDeviceData: %d\n", itemList->mode);

    // Get the per-device data and check if the menu item is currently visible.
    bdm_device_data_t *pDeviceData = static_cast<bdm_device_data_t*>(itemList->priv);
    int visible = itemList->owner != NULL ? ((opl_io_module_t *)itemList->owner)->menuItem.visible : 0;

    // Format the device path and try to open the device.
    snprintf(path, sizeof(path), "mass%d:/", itemList->mode);
    int dir = fileXioDopen(path);
    // LOG("opendir %s -> %d\n", path, dir);

#ifdef __DEBUG
    {
        const int diagSlot = itemList->mode - BDM_MODE;

        if (diagSlot >= 0 && diagSlot < MAX_BDM_DEVICES) {
            const unsigned int probe = ++BdmDiagSlotProbeCount[diagSlot];

            if (probe <= 4 || (dir >= 0 && pDeviceData->bdmDeviceRoot[0] == '\0')) {
                LOG("[BDM COLD DIAG] event=slot-probe probe=%u generation=%u eeReceived=%u "
                    "iopCallbacks=%u mountCallbacks=%u unmountCallbacks=%u blockDevices=%u "
                    "usbRoots=%u usbRootMask=0x%08x mode=%d slot=%d legacy=\"%s\" open=%d "
                    "visible=%d tick=%u ulPrev=%d root=\"%s\" driver=\"%s\" type=%d\n",
                    probe, BdmGeneration, BdmDiagEventReceivedCount, BdmDiagIopCallbackSequence,
                    BdmDiagIopMountCallbackCount, BdmDiagIopUnmountCallbackCount,
                    BdmDiagIopBlockDeviceCount, BdmDiagIopUsbRootCount, BdmDiagIopUsbRootMask,
                    itemList->mode, diagSlot, path, dir, visible, pDeviceData->bdmDeviceTick,
                    pDeviceData->bdmULSizePrev, pDeviceData->bdmDeviceRoot,
                    pDeviceData->bdmDriver, pDeviceData->bdmDeviceType);
            }

            if (probe == 1 && (diagSlot == 0 || diagSlot == 1 || diagSlot == MAX_BDM_DEVICES - 1))
                bdmDiagRequestIopSnapshot("slot-first-probe", itemList->mode, dir);
            else if (probe == 2 && diagSlot == 1 && dir < 0)
                bdmDiagRequestIopSnapshot("slot1-second-miss", itemList->mode, dir);
        }
    }
#endif

    // Device reachable this poll -> clear the debounce miss counter (see the hide branch below).
    if (dir >= 0) {
        pDeviceData->bdmMissCount = 0;
        // Latch "something has mounted here" the moment the root answers, BEFORE identity is known.
        // The publish path below deliberately defers a mount whose driver ioctl is not ready yet,
        // and without this latch that slot still looks empty to bdmSlotEverConnected() -- so the one
        // slot guaranteed to need another look promptly is the one sent to the slow rotation.
        pDeviceData->bdmSeenMounted = 1;
    }

    // If we opened the device and the menu isn't visible (OR is visible but hasn't been initialized ex: manual device start) initialize device info.
    // A device that went away and came RIGHT BACK, identity intact, is a stall -- not a new device.
    // Tell them apart before doing anything drastic.
    //
    // Hardware (a second tester, whose art pipeline is provably IDLE at the moment of the fault --
    // "ART Q0 A0 R0 D24, 3ms"): the menu hangs and, in his words, "it is like it unplugged and
    // plugged in". It is exactly that: the presence Dopen fails for a poll or two, the page hides,
    // and the recovery below treats the return as a FIRST sight of the device -- wiping the
    // identity, re-running the driver ioctls, recreating folders, rescanning THM/LNG and forcing a
    // full sbReadList of the entire library. THAT rebuild is the hang, and it also wipes every
    // row's cache ids, which is why art dies alongside it. His empty art queue is what proves the
    // fault lives here and not in the art path.
    //
    // So: if this slot already knows what it is and has published a list, a reappearance restores
    // the page and nothing else. Nothing about the device changed, so there is nothing to re-read.
    if (dir >= 0 && visible == 0 && bdmSlotIdentified(pDeviceData)) {
        if (itemList->owner != NULL) {
            ((opl_io_module_t *)itemList->owner)->menuItem.visible = 1;
            host::app().moduleUpdateMenu(itemList->mode, 0, 0);
        }
        pDeviceData->bdmMissCount = 0;
        LOG("bdmUpdateDeviceData: device %d reappeared intact -- restoring page, no rescan\n", itemList->mode);
        bdmLogIdentityDiagnosticLive("update-fast-reappearance", itemList, NULL);
        fileXioDclose(dir);
        return 0; // "nothing changed": no connect sound, no folder pass, no sbReadList
    }

    if (dir >= 0 && (visible == 0 || pDeviceData->bdmDeviceRoot[0] == '\0' || pDeviceData->bdmDriver[0] == '\0' || pDeviceData->bdmDeviceType == BDM_TYPE_UNKNOWN)) {
        char driverName[sizeof(pDeviceData->bdmDriver)] = {0};
        int massDeviceIndex = -1;

        // A BDM mount can answer Dopen before its backing transport can answer the identity
        // ioctl. Do not publish an empty-driver page: it cannot reach the native launch dispatch.
        // This runs on the deferred IO worker, so a slow or wedged IOP RPC cannot freeze boot;
        // the never-scanned state keeps this slot eligible for the normal later retry.
        // Must go through bdmReadDriverName(): asking for the name WITH a return buffer on a volume
        // whose block device has gone faults the IOP outright. See the note on that function.
        driverResult = bdmReadDriverName(dir, driverName, sizeof(driverName));
        deviceResult = -1;
        if (driverResult >= 0 && driverName[0] != '\0')
            deviceResult = fileXioIoctl2(dir, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, &massDeviceIndex, sizeof(massDeviceIndex));

        if (driverResult < 0 || driverName[0] == '\0') {
            LOG("Mass device: %d mounted but driver identity is not ready (%d); deferring page publication\n", itemList->mode, driverResult);
            LOG("[BDM DIAG] event=update-identity-not-ready generation=%u itemList=%p mode=%d legacy=\"%s\" open=%d driverResult=%d deviceResult=%d driver=\"%s\"\n",
                BdmGeneration, itemList, itemList->mode, path, dir, driverResult, deviceResult, driverName);
            fileXioDclose(dir);
            return 0;
        }

        snprintf(pDeviceData->bdmDeviceRoot, sizeof(pDeviceData->bdmDeviceRoot), "mass%d:", itemList->mode);
        bdmBuildGamePrefix(pDeviceData->bdmPrefix, sizeof(pDeviceData->bdmPrefix), pDeviceData->bdmDeviceRoot);
        LOG("[BDM DIAG] event=folder-reset reason=connect generation=%u itemList=%p mode=%d old=%u new=0\n",
            BdmGeneration, itemList, itemList->mode, pDeviceData->FoldersCreated);
        pDeviceData->FoldersCreated = 0;
        snprintf(pDeviceData->bdmDriver, sizeof(pDeviceData->bdmDriver), "%s", driverName);
        pDeviceData->massDeviceIndex = massDeviceIndex;
        pDeviceData->bdmDeviceType = BDM_TYPE_UNKNOWN;
        pDeviceData->bdmHddIsLBA48 = -1;
        pDeviceData->ataHighestUDMAMode = -1;

        itemList->flags = 0;

        pDeviceData->bdmDeviceType = bdmDetermineDeviceType(pDeviceData->bdmDriver);

        if (deviceResult < 0) {
            LOG("Mass device: %d driver %s failed to report device number: %d, using %s\n", itemList->mode, pDeviceData->bdmDriver, deviceResult, pDeviceData->bdmDeviceRoot);
        }

        if (pDeviceData->bdmDeviceType == BDM_TYPE_ATA) {
            itemList->flags = MODE_FLAG_COMPAT_DMA;
        }

        if (pDeviceData->bdmDeviceType == BDM_TYPE_ATA)
            LOG("Mass device: %d (%d) ATA device %s (compat root %s)\n", itemList->mode, pDeviceData->massDeviceIndex, pDeviceData->bdmPrefix, pDeviceData->bdmDeviceRoot);
        else if (pDeviceData->bdmDriver[0] != '\0')
            LOG("Mass device: %d (%d) %s -> %s (compat root %s)\n", itemList->mode, pDeviceData->massDeviceIndex, pDeviceData->bdmPrefix, pDeviceData->bdmDriver, pDeviceData->bdmDeviceRoot);
        else
            LOG("Mass device: %d using generic BDM path %s\n", itemList->mode, pDeviceData->bdmPrefix);

        bdmLogIdentityDiagnosticLive("update-connect-identity", itemList, NULL);

        // Publish-time enable gate (#120 audit F-13): the visible-page filter in bdmNeedsUpdate only
        // hides a page that is ALREADY showing, i.e. one refresh pass too late. Without this gate a
        // mounted slot on a DISABLED transport (e.g. an ATA-backed mass slot left over from the
        // boot-resolver escalation or a BDMA-equip force-load, with BDM HDD OFF) flashes its tab,
        // plays the connect sound and folder-writes the device on every BdmGeneration bump. Identity
        // stays populated above: the BDMA equip and the device pickers read it through
        // bdmGetDeviceSlotsByType/bdmReadDeviceIdentity independent of page visibility.
        if (bdmTransportEnabled(pDeviceData->bdmDeviceType) == 0) {
            LOG("bdmUpdateDeviceData: device %d is %s-backed but that transport is disabled; not publishing\n",
                itemList->mode, pDeviceData->bdmDriver);
            fileXioDclose(dir);
            return 0;
        }

        // An EXPLICIT BDM = Off is only overridden by the UDPBD floor (bdmEffectiveStartMode) so
        // the selected UDPBD protocol can publish ITS tab -- every other transport's page stays
        // hidden exactly as the user asked (their sticks would otherwise resurface with the dialog
        // still showing "Off").
        if (gBDMStartMode == START_MODE_DISABLED && pDeviceData->bdmDeviceType != BDM_TYPE_UDPBD) {
            LOG("bdmUpdateDeviceData: device %d withheld -- BDM is explicitly Off (UDPBD floor active)\n",
                itemList->mode);
            fileXioDclose(dir);
            return 0;
        }

        // Attach order can hand this device a different massN slot than last boot; take the L3
        // position remembered for its type before the first scan reads it.
        host::library().bdmAttach(itemList->mode, pDeviceData->bdmDeviceType);

        // Make the menu item visible.
        if (itemList->owner != NULL) {
            LOG("bdmUpdateDeviceData: setting device %d visible\n", itemList->mode);
            ((opl_io_module_t *)itemList->owner)->menuItem.visible = 1;
            host::app().moduleUpdateMenu(itemList->mode, 0, 0);
        }

        // Close the device handle.
        fileXioDclose(dir);
        return 1;
    } else if (dir < 0 && visible == 1) {
        // Device open failed while the page is showing. ONE missed poll is not evidence of removal
        // on ANY transport, so every transport debounces now.
        //
        // Local devices used to hide on the FIRST miss, reasoning that a local Dopen only fails if
        // the device is gone. Hardware disagrees: the reporter saw "the USB acted like it was
        // replugged" twice in a session having touched nothing, and this branch is literally that --
        // one failed Dopen hides the tab and plays the disconnect sound, then the next successful
        // poll takes the visible == 0 path above, plays the connect sound, recreates folders,
        // rescans THM/LNG and runs a FULL sbReadList. That rebuild is what the debug HUD caught as
        // "Q19 A0": every row is reborn with cache_id -1, so the whole viewport's art is discarded
        // and re-read behind a multi-second scan.
        //
        // A real unplug still hides one poll later (about a second) -- a far better trade than
        // turning a single transient miss into a full device rescan.
        int hideThreshold = (pDeviceData->bdmDeviceType == BDM_TYPE_UDPBD) ? BDM_NET_HIDE_MISSES : 2;
        if (++pDeviceData->bdmMissCount < hideThreshold) {
            LOG("bdmUpdateDeviceData: device %d miss %d/%d (debounce, keeping page)\n", itemList->mode, pDeviceData->bdmMissCount, hideThreshold);
            return 0; // within the grace window -- keep the page, re-poll next cycle
        }

        // Device has been removed, make the menu item invisible. We can't really cleanup resources (like the game list) just yet
        // as we don't know if the data is being used asynchronously.
        if (itemList->owner != NULL) {
            LOG("bdmUpdateDeviceData: setting device %d invisible\n", itemList->mode);
            ((opl_io_module_t *)itemList->owner)->menuItem.visible = 0;
            host::app().moduleUpdateMenu(itemList->mode, 0, 0);
        }

        bdmLogIdentityDiagnosticLive("update-confirmed-disconnect", itemList, NULL);
        LOG("[BDM DIAG] event=folder-reset reason=confirmed-disconnect generation=%u itemList=%p mode=%d old=%u new=0\n",
            BdmGeneration, itemList, itemList->mode, pDeviceData->FoldersCreated);
        pDeviceData->FoldersCreated = 0;
        pDeviceData->bdmMissCount = 0;
        LOG("Mass device: %d (%d) disconnected\n", itemList->mode, pDeviceData->massDeviceIndex);
        return -1;
    }

    // No change to the device state detected.
    if (dir >= 0)
        fileXioDclose(dir);
    return 0;
}

int bdmWaitForDevice(int deviceId, u32 timeoutMs)
{
    const int RETRY_DELAY = 100;
    char path[16];

    u32 start = GetTimerSystemTime();
    snprintf(path, sizeof(path), "mass%d:/", deviceId);

    while (1) {
        int dir = fileXioDopen(path);

        if (dir >= 0) {
            fileXioDclose(dir);
            return 1; // ready
        }

        u32 now = GetTimerSystemTime();
        u32 elapsed_ms = (now - start) / (kBUSCLK / 1000);

        if (elapsed_ms > timeoutMs) {
            return 0; // timeout
        }

        DelayThread(RETRY_DELAY * 1000);
    }
}

int bdmDeviceIsPresent(int deviceId)
{
    char path[16];
    snprintf(path, sizeof(path), "mass%d:/", deviceId);
    int dir = fileXioDopen(path);

    if (dir >= 0) {
        fileXioDclose(dir);
        return 1; // ready
    }

    return 0;
}

// Load the block-device transport driver for one BDM type, IGNORING the gEnable* config gates (the
// callers exist precisely because the wanted device may not be enabled for games -- or, for the boot-dir
// resolver, because the config that holds the gates cannot be read until the transport is up). Caller
// must hold bdmLoadModuleLock. Idempotent. Returns 1 when the transport is (already) loaded, 0 on
// failure or an unsupported type. MMCE is a separate subsystem with its own namespace + loader.
int bdmEnsureTransportLoaded(int bdmType)
{
    switch (bdmType) {
        case BDM_TYPE_USB:
            if (!iUSBModLoaded && bdmLoadOptionalModule("USBMASS_BD", &usbmass_bd_irx, size_usbmass_bd_irx) >= 0)
                iUSBModLoaded = 1;
            return iUSBModLoaded;
        case BDM_TYPE_SDC:
            if (!mx4sioModLoaded && bdmLoadOptionalModule("MX4SIO_BD", &mx4sio_bd_irx, size_mx4sio_bd_irx) >= 0)
                mx4sioModLoaded = 1;
            return mx4sioModLoaded;
        case BDM_TYPE_ILINK:
            if (!iLinkModLoaded) {
                if (!iLinkManModLoaded && bdmLoadOptionalModule("ILINKMAN", &iLinkman_irx, size_iLinkman_irx) >= 0)
                    iLinkManModLoaded = 1;
                if (iLinkManModLoaded && !ieee1394ModLoaded && bdmLoadOptionalModule("IEEE1394_BD", &IEEE1394_bd_irx, size_IEEE1394_bd_irx) >= 0)
                    ieee1394ModLoaded = 1;
                iLinkModLoaded = iLinkManModLoaded && ieee1394ModLoaded;
            }
            return iLinkModLoaded;
        case BDM_TYPE_ATA:
            if (!hddModLoaded && hddLoadModulesReady())
                hddModLoaded = 1;
            return hddModLoaded;
        default:
            return 0;
    }
}

// Ensure the BDM transport for a BDMA source type is loaded AND a device of that type is mounted, so
// the equip can read modules from a source device even when that device family is NOT enabled for games
// (e.g. BDMA files kept on a USB stick you never browse). Force-loads the driver -- the games tab stays
// hidden because bdmNeedsUpdate gates page visibility on gEnable* independently -- then waits up to
// timeoutMs for the just-loaded device to mount. Idempotent + instant when the transport is already up.
// Returns 1 if a device of the type is present afterwards, 0 otherwise.
int bdmEnsureSourceModules(int bdmType, u32 timeoutMs)
{
    int wasLoaded;
    char root[BDM_DEVICE_ROOT_MAX + 2];
    // u64, NOT u32: GetTimerSystemTime() returns a u64 bus-clock count that passes 2^32 after ~29 s of
    // uptime. Truncating the start into a u32 while subtracting it from the full u64 makes the elapsed
    // math explode on the first check for any call after that -- the wait budget silently collapses.
    u64 start;

    {
        raii::SemaLock lock(bdmLoadModuleLock);
        switch (bdmType) {
            case BDM_TYPE_USB:
                wasLoaded = iUSBModLoaded;
                break;
            case BDM_TYPE_SDC:
                wasLoaded = mx4sioModLoaded;
                break;
            case BDM_TYPE_ILINK:
                wasLoaded = iLinkModLoaded;
                break;
            case BDM_TYPE_ATA:
                wasLoaded = hddModLoaded;
                break;
            default:
                return 0;
        }
        bdmEnsureTransportLoaded(bdmType);
    }

    // Already loaded -> any connected device is already mounted, so the first probe returns with no stall
    // (and if nothing is connected we bail immediately). Just loaded -> poll until the IOP detects and
    // mounts the device, or timeoutMs elapses.
    start = GetTimerSystemTime();
    while (!bdmGetDeviceRootByType(bdmType, root, sizeof(root))) {
        if (wasLoaded)
            return 0;
        if ((GetTimerSystemTime() - start) / (kBUSCLK / 1000) > timeoutMs)
            return 0;
        DelayThread(100 * 1000);
    }
    return 1;
}
