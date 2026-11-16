/*
  Copyright 2009, Ifcaro
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#ifdef __DECI2_DEBUG
#include <iopcontrol_special.h>
#endif

#include <delaythread.h> // DelayThread for the bounded disc re-detect polls in sysLaunchDisc
#include <time.h>        // clock()/CLOCKS_PER_SEC -- the disc waits are budgeted by wall clock

// Disc-probe budgets (issue #465). All are TOTAL wall-clock caps, not per-poll ones.
constexpr int SYS_DISC_DETECT_MS = 1000;   // first "still identifying" settle
constexpr int SYS_DISC_REDETECT_MS = 4000; // spin-up + re-detect after a NODISC report
// Empty-tray shortcut inside the re-detect budget above. A drive that is actually working on a
// disc reports SCECdDETCT ("identifying") while it spins up; a drive with nothing in it only ever
// answers NODISC. So NODISC that has never once been punctuated by DETCT means an empty tray, and
// waiting out the rest of the re-detect budget only makes the user stare at a frozen-looking menu.
// Generous on purpose: this is a floor for a cold drive to reach DETCT, not a tuned timeout. Once
// DETCT is seen the FULL budget applies again, so a slow-but-working drive is unaffected.
constexpr int SYS_DISC_NODISC_MS = 2000;
#include "include/host.h"
#include "include/opl.h"
#include "include/gui.h"
#include "include/lang.h"       // _l(_STR_...) -- Neutrino preflight abort toasts
#include "include/launchdiag.h" // UDPBD hang-triage stage markers (gated on gLaunchDiag)
#include "include/ethsupport.h"
#include "include/hddsupport.h"
#include "include/util.h"
#include "include/pad.h"
#include "include/system.h"
#ifdef RETROACHIEVEMENTS
#include "include/discsupport.h"
#endif
#include "include/ioman.h"
#include "include/ioprp.h"
#include "include/bdmsupport.h"
#include "include/mmcesupport.h"
#include "include/OSDHistory.h"
#include "include/renderman.h"
#include "include/extern_irx.h"
#include "../ee_core/include/modules.h"
#include "../ee_core/include/coreconfig.h"
#include <osd_config.h>
#include "include/pggsm.h"
#include "include/sema_lock.h"
#include "include/cheatman.h"
#include "include/xparam.h"
#ifdef RETROACHIEVEMENTS
#include "include/rawatch.h"
#include "modules/network/common/ra_snap.h"
#endif

#ifdef PADEMU
#include <libds34bt.h>
#include <libds34usb.h>
#endif

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>              // fileXioInit, fileXioExit, fileXioDevctl
#include <usbhdfsd-common.h>          // USBMASS_IOCTL_GET_LBA / _GET_FRAGLIST -- VMC contiguity probe
#include "include/ps2sdkapi_compat.h" // ps2sdk_get_iop_fd -- the ioctls above take the IOP-side fd
#include "modules/vmc/genvmc/genvmc.h"     // createVMCparam_t, VMC_CREATE_PFS_RESERVE

// VMC and memory-card checks for a launch (contiguity probe, creation via genvmc). Split out of system.cpp.


int sysCheckMC(void)
{
    DIR *mc0_root_dir = opendir("mc0:/");
    if (mc0_root_dir != NULL) {
        closedir(mc0_root_dir);
        return 0;
    }

    DIR *mc1_root_dir = opendir("mc1:/");
    if (mc1_root_dir != NULL) {
        closedir(mc1_root_dir);
        return 1;
    }

    return -11;
}

// Full path of the VMC whose creation was last STARTED, for sysVMCContiguity() to re-open once
// genvmc reports the job finished. Recorded on the create path only: the VMC dialog re-probes with
// createSize == 0 on every keystroke and deletes with -1, and either would otherwise clobber the
// path the completion check needs. Creation is modal and one-at-a-time -- the same assumption
// genvmc's own single-slot status devctl already makes -- so one slot is enough.
static char gVMCCreatePath[256] = {0};

// Did the VMC just created land as ONE contiguous chain? mcemu needs that, and genvmc does not
// defragment, so on a used volume a fresh VMC can land in pieces -- which the user otherwise only
// learns mid-launch, as an error about a card they thought they had just made.
//
// 1 contiguous, 0 FRAGMENTED, -1 unknown. bdmfs_fatfs CHECK_CHAIN collapses every non-single-fragment
// result to 0, so query the fragment count instead. GET_LBA is probed first because MMCE also returns
// success for unsupported ioctls WITHOUT writing the output: require GET_LBA to replace its sentinel
// before trusting GET_FRAGLIST, or an unsupported query looks exactly like a fragmented file.
const char *sysVMCCreatePath(void)
{
    return gVMCCreatePath;
}

int sysVMCContiguity(void)
{
    u64 startingLBA = (u64)-1;
    int fd, iop_fd, fragments;

    if (gVMCCreatePath[0] == '\0')
        return -1;

    fd = open(gVMCCreatePath, O_RDONLY);
    if (fd < 0)
        return -1;

    iop_fd = ps2sdk_get_iop_fd(fd);
    if (fileXioIoctl2(iop_fd, USBMASS_IOCTL_GET_LBA, NULL, 0, &startingLBA, sizeof(startingLBA)) != 0 ||
        startingLBA == (u64)-1) {
        close(fd);
        return -1; // not a fatfs-backed device: the fragment query below would be meaningless
    }

    fragments = fileXioIoctl2(iop_fd, USBMASS_IOCTL_GET_FRAGLIST, NULL, 0, NULL, 0);
    close(fd);

    LOG("SYSTEM VMC contiguity check on %s: fragments=%d\n", gVMCCreatePath, fragments);
    if (fragments == 1)
        return 1;
    if (fragments > 1)
        return 0;
    return -1; // anything else is the ioctl itself failing, not a verdict about the file
}

// 1 the file is listed in its folder, 0 the whole folder was read and it is not there, -1 cannot tell
// (a store fileXio cannot list, or a read error). Absence is proven by reading the folder, never by a
// failed open, which a transient error also gives.
static int sysVMCCardState(const char *path)
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

// -1: explicit delete, 0: size probe, SYS_VMC_PROBE_FORMAT: validated auto probe, >0: create.
// A VMC rebuild on FatFs removes the card, then renames its verified "<name>.new" copy. Power lost
// between the two leaves only the copy: give it the card's name. Called only when the card did not
// open, and only when reading the folder PROVES the card is not there: a rename onto an existing card
// replaces it on PFS and on the network stores, so a transient open failure must never reach it.
// Where the folder cannot be listed this does nothing. 1 = the copy is now the card.
static int sysVMCAdoptRebuilt(const char *path)
{
    char copy[256];

    if (!strncmp(path, "pfs", 3) || strlen(path) < 5 || sysVMCCardState(path) != 0)
        return 0;
    snprintf(copy, sizeof(copy), "%.*s.new", (int)strlen(path) - 4, path);
    return rename(copy, path) == 0;
}

int sysCheckVMCEx(const char *prefix, const char *sep, char *name, int createSize, vmc_superblock_t *vmc_superblock, int createFlags)
{
    int size = -1;
    char path[256];

    // Invalidate the completion probe up front on any create request. The creation below is
    // CONDITIONAL -- a file already at the requested size is left alone -- while the GUI arms its
    // check before knowing that. Without this, a skipped creation would leave the path of some
    // EARLIER VMC in place and the probe would answer about the wrong file. Cleared here and set
    // only where genvmc is actually invoked, so "no creation" reads as unknown and stays silent.
    if (createSize > 0)
        gVMCCreatePath[0] = '\0';

    if ((unsigned int)snprintf(path, sizeof(path), "%sVMC%s%s.bin", prefix, sep, name) >= sizeof(path))
        return SYS_VMC_CREATE_FAILED;

    if (createSize == -1) {
        char copy[256];

        unlink(path);
        // An unfinished rebuild's "<name>.new" must go with the card: the next probe would adopt it.
        if (strlen(path) >= 5) {
            snprintf(copy, sizeof(copy), "%.*s.new", (int)strlen(path) - 4, path);
            unlink(copy);
        }
    } else {
        int fd = open(path, O_RDONLY, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
        if (fd < 0 && sysVMCAdoptRebuilt(path))
            fd = open(path, O_RDONLY, 0);
        if (fd >= 0) {
            size = lseek(fd, 0, SEEK_END);
            const bool formatProbe = createSize == SYS_VMC_PROBE_FORMAT;
            vmc_superblock_t autoSuperblock;
            if (formatProbe && vmc_superblock == NULL)
                vmc_superblock = &autoSuperblock;

            // Size alone also matches an erased image left by failed formatting/cleanup.
            // Validate whole-MB cards even for size-only callers (automatic assignment).
            // Odd-size Neutrino cards retain their existing probe result and launch policy.
            if (vmc_superblock == NULL && size > 0 && size % 1048576 == 0) {
                vmc_superblock_t header;
                bool headerValid = lseek(fd, 0, SEEK_SET) == 0;
                unsigned received = 0;
                while (headerValid && received < sizeof(header)) {
                    int got = read(fd, (char *)&header + received, sizeof(header) - received);
                    if (got <= 0 || (unsigned)got > sizeof(header) - received)
                        headerValid = false;
                    else
                        received += got;
                }
                if (!headerValid || memcmp(header.magic, "Sony PS2 Memory Card Format", 27) != 0 ||
                    header.mc_type != 2 || header.page_size != 512 || header.pages_per_cluster == 0 ||
                    (u64)header.pages_per_cluster * header.clusters_per_card * header.page_size != (u64)size) {
                    close(fd);
                    return createSize > 0 ? SYS_VMC_REPLACE_REQUIRES_DELETE : SYS_VMC_CREATE_FAILED;
                }
            }

            bool valid = size > 0;
            if (vmc_superblock) {
                memset(vmc_superblock, 0, sizeof(vmc_superblock_t));
                valid = valid && lseek(fd, 0, SEEK_SET) == 0;
                unsigned received = 0;
                while (valid && received < sizeof(vmc_superblock_t)) {
                    int got = read(fd, (char *)vmc_superblock + received, sizeof(vmc_superblock_t) - received);
                    if (got <= 0 || (unsigned)got > sizeof(vmc_superblock_t) - received)
                        valid = false;
                    else
                        received += got;
                }

                LOG("SYSTEM File size  : 0x%X\n", size);
                LOG("SYSTEM Magic      : %.40s\n", vmc_superblock->magic);
                LOG("SYSTEM Card type  : %d\n", vmc_superblock->mc_type);
                LOG("SYSTEM Flags      : 0x%X\n", (vmc_superblock->mc_flag & 0xFF) | 0x100);
                LOG("SYSTEM Page_size  : 0x%X\n", vmc_superblock->page_size);
                LOG("SYSTEM Block_size : 0x%X\n", vmc_superblock->pages_per_block);
                LOG("SYSTEM Card_size  : 0x%X\n", vmc_superblock->pages_per_cluster * vmc_superblock->clusters_per_card);

                // Widen before multiplying untrusted geometry. The formatter
                // writes this header last, after IFC/FAT/root metadata.
                valid = valid && !memcmp(vmc_superblock->magic, "Sony PS2 Memory Card Format", 27) &&
                        vmc_superblock->mc_type == 0x2 && vmc_superblock->page_size != 0 &&
                        vmc_superblock->pages_per_cluster != 0 && vmc_superblock->clusters_per_card != 0 &&
                        (u64)size == (u64)vmc_superblock->pages_per_cluster * vmc_superblock->clusters_per_card * vmc_superblock->page_size;
                if (formatProbe)
                    valid = valid && vmc_superblock->page_size == 512 && vmc_superblock->pages_per_cluster == 2 &&
                            vmc_superblock->pages_per_block >= 2 && vmc_superblock->pages_per_block % 2 == 0;
                if (valid) {
                    LOG("SYSTEM VMC file structure valid: %s\n", path);
                }
            }
            if (close(fd) < 0)
                valid = false;
            if (formatProbe && !valid)
                return SYS_VMC_CREATE_FAILED;
            if (!valid)
                size = 0;
            if (size % 1048576) // invalid size, should be a an integer (8, 16, 32, 64, ...)
                size = 0;
            else
                size /= 1048576;

            if (createSize > 0 && createSize != size) {
                /* Never destroy a VMC containing saves to resize/recreate it.
                 * genvmc opens O_TRUNC asynchronously and can fail later;
                 * explicit Delete (which has UI confirmation) is required. */
                LOG("SYSTEM refusing implicit VMC replacement: %s (%d -> %d MB)\n", path, size, createSize);
                return SYS_VMC_REPLACE_REQUIRES_DELETE;
            }
        } else if (createSize > 0) {
            // BDM FatFs errors are not POSIX errno values. A completed parent
            // directory scan, rather than an open error number, proves absence.
            char folder[256], filename[64];
            iox_dirent_t entry;
            int result;
            if ((unsigned int)snprintf(folder, sizeof(folder), "%sVMC", prefix) >= sizeof(folder) ||
                (unsigned int)snprintf(filename, sizeof(filename), "%s.bin", name) >= sizeof(filename))
                return SYS_VMC_CREATE_FAILED;
            int dir = fileXioDopen(folder);
            if (dir < 0)
                return SYS_VMC_CREATE_FAILED;
            while ((result = fileXioDread(dir, &entry)) > 0) {
                if (!strcasecmp(entry.name, filename)) {
                    fileXioDclose(dir);
                    return SYS_VMC_CREATE_FAILED;
                }
            }
            fileXioDclose(dir);
            if (result < 0)
                return SYS_VMC_CREATE_FAILED;
        }

        if (createSize > 0 && createSize != size) {
            createVMCparam_t createParam = {};
            // Remember what is being built so the GUI can ask about its layout when genvmc finishes.
            snprintf(gVMCCreatePath, sizeof(gVMCCreatePath), "%s", path);
            strcpy(createParam.VMC_filename, path);
            createParam.VMC_size_mb = createSize;
            createParam.VMC_blocksize = 16;
            createParam.VMC_thread_priority = 0x0f;
            createParam.VMC_card_slot = -1;
            createParam.VMC_flags = createFlags;
            if (fileXioDevctl("genvmc:", 0xC0DE0001, (void *)&createParam, sizeof(createParam), NULL, 0) < 0) {
                gVMCCreatePath[0] = '\0';
                return SYS_VMC_CREATE_FAILED;
            }
        }
    }
    return size;
}

int sysCheckVMC(const char *prefix, const char *sep, char *name, int createSize, vmc_superblock_t *vmc_superblock)
{
    return sysCheckVMCEx(prefix, sep, name, createSize, vmc_superblock, 0);
}

