#ifndef __BDMSUPPORTCORE_H
#define __BDMSUPPORTCORE_H

#include <tamtypes.h> // u32

// The BDM glue state owned by an object (src/sources/bdmsupport.cpp): which network block
// transport (UDPBD/UDPFS) is resident, and the unsupported-sector drive accounting written by the
// bdmevent SIF callback and reported on the ordinary device-update path. The transport loaded
// latches (iUSBModLoaded, udpbdModLoaded, ...) stay plain globals shared through include/bdmint.h,
// and the one-shot module-pass latches stay function-local statics inside the pinned
// bdmLoadBlockDeviceModules body. One instance.
class BdmSupport
{
public:
    // The network block transport actually resident (NET_BOOT_UDPBD / NET_BOOT_UDPFS). Falls back
    // to the live gNetBootProtocol before anything has loaded, so first-boot labelling is
    // unchanged. Consumers describe the live device through this, never the picker setting.
    int loadedNetProtocol() const;
    // Written when the UDPBD/UDPFS chain comes up (the module-pass loader and the explicit-loader
    // path below agree on the resident protocol through this).
    void setLoadedNetProtocol(int protocol) { udpbdLoadedProtocol_ = protocol; }

    // Custom ELF paths may deliberately live on a network block device whose game page is disabled.
    // Load exactly the requested wire protocol, never whichever protocol happens to be selected in
    // the menu. The two protocols bind different sockets but share the single SMAP NIC, so an
    // already resident SMB/UDPFS-filesystem stack -- or the OTHER block protocol -- makes this
    // request impossible until reboot and must fail closed.
    int ensureNetworkSourceModules(int protocol, u32 timeoutMs);

    // Body of the BDMEVENT_EE_EVENT_CMD SIF callback. Runs in the SIF command callback context;
    // never perform diagnostic I/O here. bdmEventHandler in bdmsupport.cpp is the registered thunk.
    void handleEvent(void *packet, void *opt);

    // Drives BDM refused because their sectors are not 512 bytes (4K-sector drives, refused by
    // design) never become massN:, so without a message they just look dead. handleEvent snapshots
    // the refused count; this reports once per rise in that number (and withdraws on unplug) on
    // the ordinary device-update path.
    void reportUnsupportedDrives();

private:
    int udpbdLoadedProtocol_ = -1;
    volatile unsigned int unsupportedSectorDrives_ = 0;
    unsigned int unsupportedSectorReported_ = 0;
};

extern BdmSupport gBdmSupport;

#endif
