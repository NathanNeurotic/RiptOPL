#ifndef __BDMLAUNCHER_H
#define __BDMLAUNCHER_H

#include "include/iosupport.h"   // item_list_t
#include "include/config.h"      // config_set_t
#include "include/supportbase.h" // base_game_info_t
#include "include/bdmsupport.h"  // bdm_device_data_t

// The BDM launch paths (src/sources/bdmlaunch.cpp): the ISO/ZSO/UL native-core launch (launch) and
// the PS1 launches by name -- an Ember .cue game folder (launchCue) and a .VCD via POPSTARTER
// (launchVcd). One instance (gBdmLauncher). The module's public entry points stay free functions
// that forward to it, because BdmSource, the autolaunch flow and the source-scraping tests call
// them by name.
class BdmLauncher
{
public:
    // Everything the old bdmLaunchGame did past its entry: the PS1-row dispatch to launchCue /
    // launchVcd, the Neutrino lean path first (gNeutrinoLauncher), the native-launch guards, then
    // the VMC/mcemu prep, fragment table, layer-1 probe, cheats, ATA DMA and the driver-token
    // sysLaunchLoaderElf dispatch. game/pDeviceData arrive already resolved (the active view's
    // array, or the autolaunch globals) from the free bdmLaunchGame entry, which also keeps the
    // browse-sub pinning that .github/scripts/test_autolaunch_settings.py pins by name.
    void launch(item_list_t *itemList, int id, config_set_t *configSet, base_game_info_t *game, bdm_device_data_t *pDeviceData);

    // Launch an Ember (.cue) PS1 title BY NAME -- the view-independent entry point, peer of
    // launchVcd. `cueName` is the game FOLDER under EMBER/games/, which is also Ember's launch
    // argument and the row's identity for art and per-game config.
    void launchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet);

    // Launch a PS1/.VCD entry BY NAME via POPSTARTER -- the view-independent entry point used by
    // both the in-view menu launch and the Favourites tab.
    void launchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet);
};

extern BdmLauncher gBdmLauncher;

#endif
