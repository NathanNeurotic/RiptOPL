// The menu-side TCP/IP stack, shared by every protocol that runs over it. See
// include/netsupport.h for what belongs here and what deliberately does not.
//
// Everything in this file was moved verbatim out of src/ethsupport.c, where it had grown up
// alongside the SMB session code and could only be reached through it. The move renames and
// relocates; it does not change what any of it does. The one structural change is that the stack
// half of the old ethDeinitModules() is now netDeinitModules(), taking the protocol's own teardown
// as a callback so it still runs at exactly the point in the sequence it always did.

#include <ps2ip.h>      // t_ip_info, DHCP_STATE_*, ps2ip_getconfig: no longer pulled in by ps2ips.h
#include <lwip/ip_addr.h> // struct ip4_addr, IP4_ADDR, ip_addr_set/cmp (same reason)
#include <delaythread.h> // DelayThread -- bounded lock acquire in netDeinitModules
#include "include/opl.h"
#include "include/netsupport.h"
#include "include/supportbase.h" // sbCreateSemaphore
#include "include/renderman.h"   // rmGetHsync
#include "include/ioman.h"       // LOG
#include "include/system.h"
#include "include/extern_irx.h"
#include "include/bdmsupport.h"   // bdmIsUDPBDLoaded() for the NIC interlock
#include "include/udpfssupport.h" // udpfsGetModulesLoaded() for the NIC interlock
#include "include/nbns.h"         // nbnsDeinit -- released with the other RPC client
#include "httpclient.h"

NetSupport gNetSupport;

// forward declaration
static void NetStatusCheckCb(s32 alarm_id, u16 time, void *common);

// Initializes locking semaphore for network support (not for just SMB support, but for the network subsystem).
int NetSupport::initSema()
{
    if (initSemaId_ < 0) {
        if ((initSemaId_ = sbCreateSemaphore()) < 0)
            return initSemaId_;
    }

    return 0;
}

int netInitSema(void)
{
    return gNetSupport.initSema();
}

void NetSupport::lockInit()
{
    if (initSemaId_ >= 0)
        WaitSema(initSemaId_);
}

void NetSupport::unlockInit()
{
    if (initSemaId_ >= 0)
        SignalSema(initSemaId_);
}

static void NetStatusCheckCb(s32 alarm_id, u16 time, void *common)
{
    iSignalSema(*(int *)common);
}

int NetSupport::netGetNetIFLinkStatus(void)
{
    return (NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP);
}

int NetSupport::waitValidState(int (*checkingFunction)(void))
{
    int SemaID, retry_cycles;
    ee_sema_t SemaData;

    // Wait for a valid network status;
    SemaData.option = SemaData.attr = 0;
    SemaData.init_count = 0;
    SemaData.max_count = 1;
    if ((SemaID = CreateSema(&SemaData)) < 0)
        return SemaID;

    for (retry_cycles = 0; checkingFunction() == 0; retry_cycles++) {
        SetAlarm(1000 * rmGetHsync(), &NetStatusCheckCb, &SemaID);
        WaitSema(SemaID);

        if (retry_cycles >= 30) // 30s = 30*1000ms
        {
            DeleteSema(SemaID);
            return -1;
        }
    }

    DeleteSema(SemaID);
    return 0;
}

int NetSupport::waitValidLinkState()
{
    return waitValidState(&NetSupport::netGetNetIFLinkStatus);
}

int NetSupport::waitValidDhcpState()
{
    return waitValidState(&netGetDHCPStatus);
}

// Caller must hold the init lock.
int NetSupport::initApplyConfig()
{
    LOG("NETSUPPORT ApplyConfig\n");

    do {
        if (waitValidLinkState() != 0) {
            gNetworkStartup = ERROR_ETH_LINK_FAIL;
            return ERROR_ETH_LINK_FAIL;
        }
    } while (applyNetifConfig() != 0);

    // Wait for the link to re-establish after applying the NIF link-mode setting.
    if (waitValidLinkState() != 0) {
        gNetworkStartup = ERROR_ETH_LINK_FAIL;
        return ERROR_ETH_LINK_FAIL;
    }

    applyIpConfig();

    // Wait for DHCP to initialize, if DHCP is enabled.
    if (ps2_ip_use_dhcp && (waitValidDhcpState() != 0)) {
        gNetworkStartup = ERROR_ETH_DHCP_FAIL;
        return ERROR_ETH_DHCP_FAIL;
    }

    return 0;
}

int NetSupport::applyNetConfig()
{
    int ret;

    lockInit();
    ret = initApplyConfig();
    unlockInit();

    return ret;
}

int netApplyConfig(void)
{
    return gNetSupport.applyNetConfig();
}

int netGetModulesLoaded(void)
{
    return gNetSupport.modulesLoaded();
}

// Caller must hold the init lock.
int NetSupport::loadModules(int protocol)
{
    LOG("NETSUPPORT LoadModules\n");

    // UDPBD owns the single SMAP NIC ("SMAP_driver" modname); refuse to bring up the menu TCP/IP
    // stack on top of it. The Device-hub UI already interlocks the two; this is the runtime backstop.
    if (bdmIsUDPBDLoaded()) {
        LOG("NETSUPPORT: UDPBD active -- not loading the NIC stack\n");
        return -1;
    }
    // Same NIC exclusivity vs the udpfs FILESYSTEM chain (udpfs_smap + ministack). The other two
    // directions already guard symmetrically (udpfssupport checks eth+bdm, bdmsupport checks
    // eth+udpfs); without this one an in-session UDPFS->SMB protocol switch could double-drive the
    // SMAP EMAC with two drivers -- at best SMB fails to start, at worst the IOP wedges.
    if (udpfsGetModulesLoaded()) {
        LOG("NETSUPPORT: UDPFS filesystem NIC active -- not loading the NIC stack\n");
        return -1;
    }

    if (!modulesLoaded_) {
        modulesLoaded_ = 1;
        residentProtocol_ = protocol;

        sysInitDev9();

        LOG("[NETMAN]:\n");
        if (sysLoadModuleBuffer(&netman_irx, size_netman_irx, 0, NULL) >= 0) {
            NetManInit();
            LOG("[SMSUTILS]:\n");
            sysLoadModuleBuffer(&smsutils_irx, size_smsutils_irx, 0, NULL);
            LOG("[SMAP]:\n");
            if (sysLoadModuleBuffer(&smap_irx, size_smap_irx, 0, NULL) >= 0) {
                // Before the network stack is loaded, attempt to set the link settings in order to avoid needing double-initialization of the IF.
                // But do not fail here because there is currently no way to re-start initialization.
                applyNetifConfig();
                LOG("[PS2IP]:\n");
                if (sysLoadModuleBuffer(&ps2ip_irx, size_ps2ip_irx, 0, NULL) >= 0) {
                    LOG("[PS2IPS]:\n");
                    sysLoadModuleBuffer(&ps2ips_irx, size_ps2ips_irx, 0, NULL);
                    LOG("[HTTPCLIENT]:\n");
                    if (sysLoadModuleBuffer(&httpclient_irx, size_httpclient_irx, 0, NULL) >= 0) {
                        if (HttpInit() < 0)
                            LOG("NETSUPPORT: httpclient RPC bind failed; compat update unavailable\n");
                    }
                    ps2ip_init();
                    LOG("NETSUPPORT Modules loaded\n");
                    if (protocol == NET_PROTO_HTTP && !HttpIsInitialized()) {
                        gNetworkStartup = ERROR_ETH_MODULE_NETIF_FAILURE;
                        return -1;
                    }
                    return 0;
                }
            }
        }

        gNetworkStartup = ERROR_ETH_MODULE_NETIF_FAILURE;
        return -1;
    }

    // Keep the residency latch: teardown does not unload IRXs, so pretending the NIC is free
    // after an HTTP bind failure would allow another driver to load on top of it. SMB can still
    // use the shared stack when only its optional compatibility HTTP client failed.
    if (protocol == NET_PROTO_HTTP && !HttpIsInitialized()) {
        gNetworkStartup = ERROR_ETH_MODULE_NETIF_FAILURE;
        return -1;
    }
    return 0;
}

int netGetResidentProtocol(void)
{
    return gNetSupport.residentProtocol();
}

// Slices netDeinitModules will wait for the init lock before giving up. One slice is 100 ms, so ~3 s
// -- far past any healthy hand-off, and the ONLY unbounded blocker that was left on the teardown
// path (found by the rebuild-154 exit-hang audit and parked then because it is not ATA-specific;
// issue #382 is the not-ATA case).
constexpr int NET_DEINIT_LOCK_SLICES = 30;

void NetSupport::deinitModules(void (*protocolTeardown)(void))
{
    if (modulesLoaded_) {
        // BOUNDED ACQUIRE, was an unbounded WaitSema. deinit runs this while a game launch is in
        // flight, and anything still holding this lock -- an in-progress SMB reconnect, a network
        // read that will not return because the link is already going away -- used to stop the exit
        // dead with the screen already torn down. That is the exact shape of rebuild-154's ATA hang,
        // one transport over.
        //
        // Giving up is safe and is the whole point: every caller is on its way to an IOP reset that
        // reclaims the network stack wholesale. A teardown that gives up beats one that hangs.
        int lockHeld = 0;
        if (initSemaId_ >= 0) {
            int slices = NET_DEINIT_LOCK_SLICES;
            while (slices-- > 0) {
                if (PollSema(initSemaId_) == initSemaId_) {
                    lockHeld = 1;
                    break;
                }
                DelayThread(100000); // 100 ms
            }
            if (!lockHeld)
                LOG("NET: deinit could not take the init lock -- tearing down anyway\n");
        }

        // Both RPC clients this stack registered, released together and in the order they always
        // were. nbns is NetBIOS name resolution, so it only matters to SMB, but releasing it is a
        // property of the stack rather than of the session -- and keeping it here is what lets the
        // protocol hook sit at the exact position the SMB flag writes used to occupy.
        HttpDeinit();
        nbnsDeinit();
        NetManDeinit();
        modulesLoaded_ = 0;
        residentProtocol_ = NET_PROTO_OFF;
        // The protocol's own state is cleared HERE, immediately after netModulesLoaded and before
        // the semaphore goes, because that is precisely where SMB cleared its two flags.
        if (protocolTeardown != NULL)
            protocolTeardown();

        if (initSemaId_ >= 0) {
            if (lockHeld)
                SignalSema(initSemaId_);
            DeleteSema(initSemaId_);
            initSemaId_ = -1;
        }

        // To allow the configuration to be read later on, read the latest version now.
        readNetConfig();
        ps2ip_deinit();
    }
}

void netDeinitModules(void (*protocolTeardown)(void))
{
    gNetSupport.deinitModules(protocolTeardown);
}

int NetSupport::loadInitModules(int protocol)
{
    int ret;

    if ((ret = initSema()) < 0)
        return ret;

    WaitSema(initSemaId_);

    if ((ret = loadModules(protocol)) == 0) {
        ret = initApplyConfig();
    }

    SignalSema(initSemaId_);

    return ret;
}

int netLoadInitModules(int protocol)
{
    return gNetSupport.loadInitModules(protocol);
}

int NetSupport::ensureModules()
{
    int ret;

    if ((ret = initSema()) < 0)
        return ret;

    WaitSema(initSemaId_);
    ret = loadModules(NET_PROTO_SMB);
    SignalSema(initSemaId_);

    return ret;
}

int netEnsureModules(void)
{
    return gNetSupport.ensureModules();
}

int NetSupport::readNetConfig(void)
{
    t_ip_info ip_info;
    int result;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
    if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0) {
        lastIP_ = *(struct ip4_addr *)&ip_info.ipaddr;
        lastNM_ = *(struct ip4_addr *)&ip_info.netmask;
        lastGW_ = *(struct ip4_addr *)&ip_info.gw;
    } else {
        ip4_addr_set_zero(&lastIP_);
        ip4_addr_set_zero(&lastNM_);
        ip4_addr_set_zero(&lastGW_);
    }
#pragma GCC diagnostic pop

    return result;
}

int NetSupport::getConfig(u8 *ip_address, u8 *netmask, u8 *gateway)
{
    int result;

    // Read a cached copy of the settings, if this is read after deinitialization.
    result = modulesLoaded_ ? readNetConfig() : -1;
    ip_address[0] = ip4_addr1(&lastIP_);
    ip_address[1] = ip4_addr2(&lastIP_);
    ip_address[2] = ip4_addr3(&lastIP_);
    ip_address[3] = ip4_addr4(&lastIP_);

    netmask[0] = ip4_addr1(&lastNM_);
    netmask[1] = ip4_addr2(&lastNM_);
    netmask[2] = ip4_addr3(&lastNM_);
    netmask[3] = ip4_addr4(&lastNM_);

    gateway[0] = ip4_addr1(&lastGW_);
    gateway[1] = ip4_addr2(&lastGW_);
    gateway[2] = ip4_addr3(&lastGW_);
    gateway[3] = ip4_addr4(&lastGW_);

    return result;
}

int netGetConfig(u8 *ip_address, u8 *netmask, u8 *gateway)
{
    return gNetSupport.getConfig(ip_address, netmask, gateway);
}

int NetSupport::applyNetifConfig(void)
{
    int mode, result;

    switch (gETHOpMode) {
        case ETH_OP_MODE_100M_FDX:
            mode = NETMAN_NETIF_ETH_LINK_MODE_100M_FDX;
            break;
        case ETH_OP_MODE_100M_HDX:
            mode = NETMAN_NETIF_ETH_LINK_MODE_100M_HDX;
            break;
        case ETH_OP_MODE_10M_FDX:
            mode = NETMAN_NETIF_ETH_LINK_MODE_10M_FDX;
            break;
        case ETH_OP_MODE_10M_HDX:
            mode = NETMAN_NETIF_ETH_LINK_MODE_10M_HDX;
            break;
        default:
            mode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;
    }

    if (netifCurrentMode_ != mode) {
        if ((result = NetManSetLinkMode(mode)) == 0)
            netifCurrentMode_ = mode;
    } else
        result = 0;

    return result;
}

int NetSupport::applyIpConfig(void)
{
    t_ip_info ip_info;
    struct ip4_addr ipaddr, netmask, gw, dns;
    const struct ip4_addr *dns_curr;
    int result;

    if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0) {
        IP4_ADDR(&ipaddr, ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3]);
        IP4_ADDR(&netmask, ps2_netmask[0], ps2_netmask[1], ps2_netmask[2], ps2_netmask[3]);
        IP4_ADDR(&gw, ps2_gateway[0], ps2_gateway[1], ps2_gateway[2], ps2_gateway[3]);
        IP4_ADDR(&dns, ps2_dns[0], ps2_dns[1], ps2_dns[2], ps2_dns[3]);
        dns_curr = dns_getserver(0);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
        // Check if it's the same. Otherwise, apply the new configuration.
        if ((ps2_ip_use_dhcp != ip_info.dhcp_enabled) || (!ps2_ip_use_dhcp &&
                                                          (!ip_addr_cmp(&ipaddr, (struct ip4_addr *)&ip_info.ipaddr) ||
                                                           !ip_addr_cmp(&netmask, (struct ip4_addr *)&ip_info.netmask) ||
                                                           !ip_addr_cmp(&gw, (struct ip4_addr *)&ip_info.gw) ||
                                                           !ip_addr_cmp(&dns, dns_curr)))) {
            if (ps2_ip_use_dhcp) {
                ip4_addr_set_zero((struct ip4_addr *)&ip_info.ipaddr);
                ip4_addr_set_zero((struct ip4_addr *)&ip_info.netmask);
                ip4_addr_set_zero((struct ip4_addr *)&ip_info.gw);
                ip4_addr_set_zero(&dns);

                ip_info.dhcp_enabled = 1;
            } else {
                ip_addr_set((struct ip4_addr *)&ip_info.ipaddr, &ipaddr);
                ip_addr_set((struct ip4_addr *)&ip_info.netmask, &netmask);
                ip_addr_set((struct ip4_addr *)&ip_info.gw, &gw);

                ip_info.dhcp_enabled = 0;
            }

            result = ps2ip_setconfig(&ip_info);
            if (!ps2_ip_use_dhcp)
                dns_setserver(0, &dns);
        } else
            result = 0;
#pragma GCC diagnostic pop
    }

    return result;
}

int NetSupport::dhcpStatus(void)
{
    t_ip_info ip_info;
    int result;

    if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0) {
        if (ip_info.dhcp_enabled) {
            result = (ip_info.dhcp_status == DHCP_STATE_BOUND || (ip_info.dhcp_status == DHCP_STATE_OFF));
        } else
            result = -1;
    }

    return result;
}

int netGetDHCPStatus(void)
{
    return gNetSupport.dhcpStatus();
}

