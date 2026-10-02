#ifndef __NEUTRINOLAUNCHER_H
#define __NEUTRINOLAUNCHER_H

#include "include/iosupport.h"   // item_list_t
#include "include/config.h"      // config_set_t
#include "include/supportbase.h" // base_game_info_t, neutrino_vmc_args_t (via system.h)
#include "include/bdmsupport.h"  // bdm_device_data_t

// The Δ8 (NHDDL parity) lean Neutrino launch path for BDM devices (src/sources/bdmlaunch.cpp).
// Everything the native flow prepares -- VMC superblock prompts + mcemu patching, sbPrepare's
// cdvdman patch, per-part fragment lists (with a hard abort past 64 frags), layer-1 probing,
// cheats (with dialogs), PS2RD images, ATA DMA setup -- exists for the EMBEDDED cdvdman core;
// Neutrino re-derives all of it from -bsd/-dvd after its own IOP reset. One instance
// (gNeutrinoLauncher).
class NeutrinoLauncher
{
public:
    // Returns 1 = handled (handed off, or aborted with the user informed -- caller returns);
    // 0 = proceed with the native launch (core is OPL, or Neutrino unavailable on a non-udp
    // device).
    int tryLaunch(item_list_t *itemList, base_game_info_t *game, bdm_device_data_t *pDeviceData, config_set_t *configSet);

private:
    // Δ9 (NHDDL parity): Neutrino's bd backend packs the ISO and BOTH -mc VMCs into ONE shared
    // 64-entry fragment table (BDM_MAX_FRAGS in neutrino's fhi_bd_config.h; fhi_config.c's
    // backend_bd_add_file_fd errors past it). NOT OPL's BDM_MAX_FRAGS: same value today,
    // different owner.
    static constexpr int kMaxFrags = 64;

    // Pre-count the same fragment budget Neutrino itself discovers only after its own IOP reset
    // (debug printf + exit = black screen), using the count-only form of the same ioctl, while the
    // menu can still explain the abort. Returns 1 when the budget holds.
    int fragBudgetOk(const char *isoPath, const neutrino_vmc_args_t *vmcArgs, int isUdp);

    // Abort path of tryLaunch: back to a live menu so a later launch from another device does not
    // inherit this one's markers. Returns failResult (1 = launch consumed on udp, 0 = fall back to
    // native).
    int abortLaunch(int failResult, config_set_t *configSet);
};

extern NeutrinoLauncher gNeutrinoLauncher;

#endif
