/* POPStarter telemetry adapted from hacan359/Open-PS2-Loader 531aad45, by way of the original
   RiptOPL. Driver selection and ELF handoff remain owned by the VCD launcher (bdmlaunch.cpp).

   POPStarter loads POPS/MODULE_9.IRX after its own IOP reset, without arguments. The copy written
   here is rapops with its configuration block filled in: the network setup, the watch list, and
   the modules rapops starts (modules/network/common/rapops_cfg.h). */
#ifdef RETROACHIEVEMENTS
#include "include/opl.h"
#include "include/ethsupport.h"
#include "include/extern_irx.h"
#include "include/supportbase.h"
#include "include/rahash.h"
#include "include/rawatch.h"
#include "include/ranet.h"
#include "include/rapopslaunch.h"
#include "modules/network/common/rapops_cfg.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The modules rapops starts, in RAPOPS_* order; the loader already embeds each of them. */
static int raPopsFillBlobs(struct rapops_cfg *cfg)
{
    const void *irx[RAPOPS_BLOBS] = {ps2dev9_irx, smsutils_irx, ingame_smstcpip_irx, smap_ingame_irx, raudp_irx, rapull_irx};
    const int size[RAPOPS_BLOBS] = {size_ps2dev9_irx, size_smsutils_irx, size_ingame_smstcpip_irx, size_smap_ingame_irx,
                                    size_raudp_irx, size_rapull_irx};
    unsigned int off = 0;

    for (int i = 0; i < RAPOPS_BLOBS; i++) {
        if (size[i] <= 0 || (unsigned int)size[i] > RAPOPS_BLOB_BYTES - off)
            return -1;
        memcpy(&cfg->blob[off], irx[i], size[i]);
        cfg->blob_off[i] = off;
        cfg->blob_size[i] = size[i];
        off = (off + size[i] + 15) & ~15u;
        if (off > RAPOPS_BLOB_BYTES)
            off = RAPOPS_BLOB_BYTES;
    }
    return 0;
}

int raPopsPrepare(const char *root, const char *watchRoot, const char *vcdPath, int slotFree)
{
    char boot[16], watchKey[16], contentHash[33], path[256];
    unsigned char ip[4], mask[4], gw[4];
    unsigned char *irx;
    struct rapops_cfg *cfg = NULL;
    unsigned int i, peer;
    int n, fd, off = 0, result = 0;

    if (!gRATelemetry)
        return 0;
    if (raVcdWatchKey(vcdPath, watchKey, sizeof(watchKey)) != 0 || sbReadWatchList(watchRoot, watchKey) <= 0)
        return 0; /* No support check/list: ordinary POPStarter launch. */
    /* A VCD replaced at the same path keeps its key, but MUST NOT inherit the other image's list.
       Rehash the executable rather than trusting a matching path or timestamp. */
    if (raHashVcd(vcdPath, boot, sizeof(boot), contentHash) != 0 || !raVcdWatchGuardMatches(watchRoot, watchKey, contentHash))
        return 0; /* Missing/old guard: the game boots normally, untracked. */
    /* A stale list must never make a playable PS1 game unlaunchable. PS2's link, DHCP and
       Caduceus session preflight apply here too; it clears the list when the network cannot come up. */
    raLaunchNetworkUp();
    if (GetWatchCount() <= 0 || ethGetNetConfig(ip, mask, gw) < 0 || !(ip[0] | ip[1] | ip[2] | ip[3]))
        return 0;
    if (!slotFree)
        return -1; /* FAT does not enforce O_EXCL: refuse the user-owned slot explicitly. */
    if (snprintf(path, sizeof(path), "%sPOPS/MODULE_9.IRX", root) >= (int)sizeof(path))
        return -1;
    irx = static_cast<unsigned char *>(malloc(size_rapops_irx));
    if (!irx)
        return 0; /* Optional telemetry: low memory must not block the game. */
    memcpy(irx, rapops_irx, size_rapops_irx);
    for (i = 0; i + sizeof(*cfg) <= (unsigned int)size_rapops_irx; i += 4) {
        if (memcmp(irx + i, RAPOPS_MAGIC, 8) == 0) {
            cfg = reinterpret_cast<struct rapops_cfg *>(irx + i);
            break;
        }
    }
    /* Nothing is written when the module, the set or the modules it carries do not fit. */
    if (!cfg || raPopsFillBlobs(cfg) != 0)
        goto done;
    cfg->count = GetWatchCount();
    cfg->bytes = GetWatchBytes();
    cfg->node_count = GetNodeCount();
    if (!cfg->count || cfg->count > RA_WATCH_MAX || cfg->node_count > RA_NODE_MAX_PS1 ||
        cfg->bytes + cfg->node_count * RA_NODE_PAIR_BYTES > RA_SNAP_MAX_BYTES)
        goto done;
    memcpy(cfg->list, GetWatchList(), cfg->count * sizeof(unsigned int));
    if (cfg->node_count)
        memcpy(cfg->nodes, GetNodeList(), cfg->node_count * sizeof(struct ra_node));
    n = snprintf(cfg->ipcfg, sizeof(cfg->ipcfg), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]) + 1;
    n += snprintf(cfg->ipcfg + n, sizeof(cfg->ipcfg) - n, "%u.%u.%u.%u", mask[0], mask[1], mask[2], mask[3]) + 1;
    n += snprintf(cfg->ipcfg + n, sizeof(cfg->ipcfg) - n, "%u.%u.%u.%u", gw[0], gw[1], gw[2], gw[3]) + 1;
    cfg->ipcfg_len = n;
    snprintf(cfg->game_id, sizeof(cfg->game_id), "%s", watchKey);
    cfg->client_mode = gRAMode == RA_MODE_CADUCEUS;
    cfg->host_pinned = (gRAHostIp[0] | gRAHostIp[1] | gRAHostIp[2] | gRAHostIp[3]) != 0;
    peer = raNetPeerIP();
    if (peer) {
        const unsigned char *host = reinterpret_cast<const unsigned char *>(&peer);
        snprintf(cfg->host, sizeof(cfg->host), "%u.%u.%u.%u", host[0], host[1], host[2], host[3]);
    } else if (cfg->host_pinned) {
        snprintf(cfg->host, sizeof(cfg->host), "%u.%u.%u.%u", gRAHostIp[0], gRAHostIp[1], gRAHostIp[2], gRAHostIp[3]);
    }
    /* xeRAbora 2.1.0: the list's image hash rides behind the host, for raudp's "RAI1 <serial> <hash>". */
    if (GetWatchHash()[0] != '\0') {
        const size_t used = strlen(cfg->host);
        snprintf(cfg->host + used, sizeof(cfg->host) - used, ",%s", GetWatchHash());
    }
    /* Ownership/absence was checked before preparation. O_EXCL is only a supplementary guard on
       backends that implement it; PS2 FAT currently ignores that flag. */
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0)
        goto done; /* Read-only/absent USB path: launch without telemetry. */
    while (off < size_rapops_irx) {
        n = write(fd, irx + off, size_rapops_irx - off);
        if (n <= 0)
            break;
        off += n;
    }
    result = off == size_rapops_irx ? 0 : -1;
    if (close(fd) < 0)
        result = -1;
    /* A failed install that was fully removed is safe to skip. If removal fails, block rather
       than boot a corrupt module. */
    if (result < 0 && unlink(path) == 0)
        result = 0;
done:
    free(irx);
    return result;
}
#endif
