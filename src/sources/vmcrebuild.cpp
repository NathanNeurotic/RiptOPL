/*
  Licenced under Academic Free License version 3.0
  Review Open PS2 Loader README & LICENSE files for further details.
*/

// Rebuild a native VMC that is too fragmented to map (original #986/#1015). The card stays as it is until a copy of
// it exists, reserved the way a new card is, matches it byte for byte (genvmc reads both back) and
// passes the same layout check the launch makes. Only then does the copy take the card's name: on
// PFS one rename replaces the card; FatFs cannot rename onto a file, so the card is removed and the
// copy renamed right after. No ".bak" is ever made. If power is lost between those two steps the
// saves are whole in "<name>.new", and sysCheckVMCEx gives it the card's name the next time the card
// is looked for. A ".new" found beside the card at the start is an interrupted rebuild's unfinished
// copy and is removed. A ".bak" left by an older build is neither touched nor in the way.

#include "include/opl.h"
#include "include/ioman.h"
#include "include/system.h"
#include "include/bdmsupport.h"
#include "include/hddsupport.h" // hddVMCMapFits
#include "include/nativevmc.h"
#include "include/vmcrebuild.h"
#include "modules/vmc/genvmc/genvmc.h"
#include "include/ps2sdkapi_compat.h" // ps2sdk_get_iop_fd
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

static struct
{
    int active, sizeMb;
    char card[256], copy[256];
} gVMCRebuild;

int vmcRebuildLayout(const char *path, int sizeMb)
{
    u64 startingLBA;
    int fd, fragments, verdict;

    if (!strncmp(path, "pfs", 3))
        return hddVMCMapFits(path);

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    verdict = bdmCheckNativeVmcLayout(ps2sdk_get_iop_fd(fd), sizeMb, (u64)-1, &startingLBA, &fragments); // reach: checked at launch
    close(fd);
    LOG("VMC rebuild layout of %s: reason=%d fragments=%d\n", path, verdict, fragments);
    if (verdict == NATIVE_VMC_OK)
        return 1;
    return verdict == NATIVE_VMC_FRAGMENTED ? 0 : -1;
}

// "<name>.bin" -> "<name><ext>" in out (256 bytes).
static int vmcRebuildSibling(char *out, const char *card, const char *ext)
{
    size_t len = strlen(card);

    if (len < 5 || len >= 256 || strcmp(&card[len - 4], ".bin"))
        return -1;
    memcpy(out, card, len - 4);
    strcpy(&out[len - 4], ext);
    return 0;
}

// 1 present, 0 absent, -1 cannot tell. Absence is proven by reading the whole folder, never by an
// open error (BDM FatFs errors are not errno values) -- and PFS rename REPLACES an existing target.
static int vmcRebuildExists(const char *path)
{
    char folder[256];
    const char *name = strrchr(path, '/');
    iox_dirent_t entry;
    int dir, result;

    if (name == NULL || (size_t)(name - path) >= sizeof(folder))
        return -1;
    memcpy(folder, path, name - path);
    folder[name - path] = '\0';
    dir = fileXioDopen(folder);
    if (dir < 0)
        return -1;
    while ((result = fileXioDread(dir, &entry)) > 0) {
        if (!strcasecmp(entry.name, name + 1)) {
            fileXioDclose(dir);
            return 1;
        }
    }
    fileXioDclose(dir);
    return result < 0 ? -1 : 0;
}

// pfs0: is write-back: a rename is only in PFS's RAM cache until it is synced. 0 = committed (or not
// PFS), < 0 = PFS could not commit it.
static int vmcRebuildSync(void)
{
    char device[16];
    const char *colon = strchr(gVMCRebuild.card, ':');
    int result;

    if (strncmp(gVMCRebuild.card, "pfs", 3) || colon == NULL || (size_t)(colon - gVMCRebuild.card) + 1 >= sizeof(device))
        return 0;
    memcpy(device, gVMCRebuild.card, colon - gVMCRebuild.card + 1);
    device[colon - gVMCRebuild.card + 1] = '\0';
    result = fileXioSync(device, 0);
    if (result < 0)
        LOG("VMC rebuild: sync of %s failed: %d\n", device, result);
    return result < 0 ? result : 0;
}

// The copy is this rebuild's own file (genvmc made it exclusively); remove it only while the card is
// in place, so a step that reported failure but happened can never cost the last good copy.
static int vmcRebuildDropCopy(int result)
{
    if (vmcRebuildExists(gVMCRebuild.card) != 1) {
        LOG("VMC rebuild: %s not found, keeping %s\n", gVMCRebuild.card, gVMCRebuild.copy);
        return VMC_REBUILD_STUCK;
    }
    if (unlink(gVMCRebuild.copy) != 0)
        LOG("VMC rebuild: could not remove %s\n", gVMCRebuild.copy);
    return result;
}

int vmcRebuildStart(const char *path, int sizeMb)
{
    createVMCparam_t param;
    int present;

    if (gVMCRebuild.active || (sizeMb != 8 && sizeMb != 16 && sizeMb != 32 && sizeMb != 64) || strlen(path) >= sizeof(gVMCRebuild.card) ||
        vmcRebuildSibling(gVMCRebuild.copy, path, ".new") != 0)
        return VMC_REBUILD_FAILED;
    strcpy(gVMCRebuild.card, path);

    // With the card in place, a ".new" beside it is an unfinished copy of it.
    if (vmcRebuildExists(gVMCRebuild.card) != 1)
        return VMC_REBUILD_FAILED;
    present = vmcRebuildExists(gVMCRebuild.copy);
    if (present < 0 || (present == 1 && unlink(gVMCRebuild.copy) != 0))
        return VMC_REBUILD_FAILED;

    memset(&param, 0, sizeof(param));
    strcpy(param.VMC_filename, gVMCRebuild.copy);
    param.VMC_size_mb = sizeMb;
    param.VMC_blocksize = 16;
    param.VMC_thread_priority = 0x0f;
    param.VMC_card_slot = -1;
    param.VMC_flags = VMC_CREATE_REBUILD | (strncmp(path, "pfs", 3) ? VMC_CREATE_CONTIGUOUS : VMC_CREATE_PFS_RESERVE);
    if (fileXioDevctl("genvmc:", GENVMC_DEVCTL_CREATE_VMC, &param, sizeof(param), NULL, 0) < 0)
        return VMC_REBUILD_FAILED;

    gVMCRebuild.sizeMb = sizeMb;
    gVMCRebuild.active = 1;
    LOG("VMC rebuild started: %s -> %s\n", gVMCRebuild.card, gVMCRebuild.copy);
    return 0;
}

int vmcRebuildFinish(int jobError)
{
    int layout, swapped;

    if (!gVMCRebuild.active)
        return VMC_REBUILD_FAILED;
    gVMCRebuild.active = 0;
    LOG("VMC rebuild of %s: job result %d\n", gVMCRebuild.card, jobError);

    // genvmc has already removed a copy it made on any failure.
    if (jobError == -1000)
        return VMC_REBUILD_ABORTED;
    if (jobError == -112)
        return VMC_REBUILD_NO_ROOM;
    if (jobError != 0)
        return VMC_REBUILD_FAILED;

    // The copy is complete and identical. It replaces the card only if the launch would map it.
    layout = vmcRebuildLayout(gVMCRebuild.copy, gVMCRebuild.sizeMb);
    if (layout != 1)
        return vmcRebuildDropCopy(layout == 0 ? VMC_REBUILD_NO_ROOM : VMC_REBUILD_FAILED);

    if (!strncmp(gVMCRebuild.card, "pfs", 3))
        swapped = rename(gVMCRebuild.copy, gVMCRebuild.card) == 0; // PFS replaces the card in one step
    else if (unlink(gVMCRebuild.card) != 0)
        return vmcRebuildDropCopy(VMC_REBUILD_FAILED); // STUCK, copy kept, if the card went after all
    else
        swapped = rename(gVMCRebuild.copy, gVMCRebuild.card) == 0;
    if (!swapped) {
        // A rename that reported failure but happened leaves the card present and no copy.
        if (vmcRebuildExists(gVMCRebuild.copy) != 0 || vmcRebuildExists(gVMCRebuild.card) != 1) {
            LOG("VMC rebuild: %s could not take the card's name\n", gVMCRebuild.copy);
            vmcRebuildSync();
            return vmcRebuildDropCopy(VMC_REBUILD_FAILED);
        }
    }
    // "Rebuilt" only once the swap is on the disk; otherwise say the change was not confirmed.
    if (vmcRebuildSync() < 0)
        return VMC_REBUILD_UNSYNCED;
    LOG("VMC rebuild done: %s\n", gVMCRebuild.card);
    return VMC_REBUILD_DONE;
}
