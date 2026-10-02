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
constexpr int SYS_DISC_DETECT_MS = 1000;  // first "still identifying" settle
constexpr int SYS_DISC_REDETECT_MS = 4000;  // spin-up + re-detect after a NODISC report
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
#include <fileXio_rpc.h>     // fileXioInit, fileXioExit, fileXioDevctl
#include <usbhdfsd-common.h> // USBMASS_IOCTL_GET_LBA / _CHECK_CHAIN -- VMC contiguity probe
#include <ps2sdkapi.h>       // ps2sdk_get_iop_fd -- the ioctls above take the IOP-side fd

// VMC and memory-card checks for a launch (contiguity probe, creation via genvmc). Split out of system.cpp.

typedef struct
{
    char VMC_filename[1024];
    int VMC_size_mb;
    int VMC_blocksize;
    int VMC_thread_priority;
    int VMC_card_slot;
} createVMCparam_t;

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
// 1 contiguous, 0 FRAGMENTED, -1 unknown. GET_LBA is probed first because bdmfs_fatfs answers
// CHECK_CHAIN with a bare 1/0 and never an error, so a store that cannot do LBA lookups at all
// (mmce, pfs, SMB, udpfs) would otherwise return a 0 meaning only "not my ioctl".
int sysVMCContiguity(void)
{
    u64 startingLBA;
    int fd, iop_fd, chain;

    if (gVMCCreatePath[0] == '\0')
        return -1;

    fd = open(gVMCCreatePath, O_RDONLY);
    if (fd < 0)
        return -1;

    iop_fd = ps2sdk_get_iop_fd(fd);
    if (fileXioIoctl2(iop_fd, USBMASS_IOCTL_GET_LBA, NULL, 0, &startingLBA, sizeof(startingLBA)) != 0) {
        close(fd);
        return -1; // not a fatfs-backed device: CHECK_CHAIN below would be meaningless
    }

    chain = fileXioIoctl(iop_fd, USBMASS_IOCTL_CHECK_CHAIN, const_cast<char *>(""));
    close(fd);

    LOG("SYSTEM VMC contiguity check on %s: chain=%d\n", gVMCCreatePath, chain);
    if (chain == 1)
        return 1;
    if (chain == 0)
        return 0;
    return -1; // anything else is the ioctl itself failing, not a verdict about the file
}

// createSize == -1 : delete, createSize == 0 : probing, createSize > 0 : creation
int sysCheckVMC(const char *prefix, const char *sep, char *name, int createSize, vmc_superblock_t *vmc_superblock)
{
    int size = -1;
    char path[256];
    snprintf(path, sizeof(path), "%sVMC%s%s.bin", prefix, sep, name);

    // Invalidate the completion probe up front on any create request. The creation below is
    // CONDITIONAL -- a file already at the requested size is left alone -- while the GUI arms its
    // check before knowing that. Without this, a skipped creation would leave the path of some
    // EARLIER VMC in place and the probe would answer about the wrong file. Cleared here and set
    // only where genvmc is actually invoked, so "no creation" reads as unknown and stays silent.
    if (createSize > 0)
        gVMCCreatePath[0] = '\0';

    if (createSize == -1)
        unlink(path);
    else {
        int fd = open(path, O_RDONLY, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
        if (fd >= 0) {
            size = lseek(fd, 0, SEEK_END);

            if (vmc_superblock) {
                memset(vmc_superblock, 0, sizeof(vmc_superblock_t));
                lseek(fd, 0, SEEK_SET);
                read(fd, (void *)vmc_superblock, sizeof(vmc_superblock_t));

                LOG("SYSTEM File size  : 0x%X\n", size);
                LOG("SYSTEM Magic      : %s\n", vmc_superblock->magic);
                LOG("SYSTEM Card type  : %d\n", vmc_superblock->mc_type);
                LOG("SYSTEM Flags      : 0x%X\n", (vmc_superblock->mc_flag & 0xFF) | 0x100);
                LOG("SYSTEM Page_size  : 0x%X\n", vmc_superblock->page_size);
                LOG("SYSTEM Block_size : 0x%X\n", vmc_superblock->pages_per_block);
                LOG("SYSTEM Card_size  : 0x%X\n", vmc_superblock->pages_per_cluster * vmc_superblock->clusters_per_card);

                if (!strncmp(vmc_superblock->magic, "Sony PS2 Memory Card Format", 27) && vmc_superblock->mc_type == 0x2 && size == vmc_superblock->pages_per_cluster * vmc_superblock->clusters_per_card * vmc_superblock->page_size) {
                    LOG("SYSTEM VMC file structure valid: %s\n", path);
                } else
                    size = 0;
            }

            if (size % 1048576) // invalid size, should be a an integer (8, 16, 32, 64, ...)
                size = 0;
            else
                size /= 1048576;

            close(fd);

            if (createSize && (createSize != size))
                unlink(path);
        }


        if (createSize && (createSize != size)) {
            createVMCparam_t createParam;
            // Remember what is being built so the GUI can ask about its layout when genvmc finishes.
            snprintf(gVMCCreatePath, sizeof(gVMCCreatePath), "%s", path);
            strcpy(createParam.VMC_filename, path);
            createParam.VMC_size_mb = createSize;
            createParam.VMC_blocksize = 16;
            createParam.VMC_thread_priority = 0x0f;
            createParam.VMC_card_slot = -1;
            fileXioDevctl("genvmc:", 0xC0DE0001, (void *)&createParam, sizeof(createParam), NULL, 0);
        }
    }
    return size;
}
