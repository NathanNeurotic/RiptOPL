/*
  Copyright 2009, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/settings.h"
#include "include/oplint.h"
#include "include/opl.h"
#include "include/ioman.h"
#include "include/launchdiag.h" // UDPBD hang-triage stage markers (gated on gLaunchDiag)
#include "include/gui.h"
#include "include/guigame.h"
#include "include/renderman.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/textures.h"
#include "include/artindex.h"
#include "include/pad.h"
#include "include/texcache.h"
#include "include/dia.h"
#include "include/dialogs.h"
#include "include/menusys.h"
#include "include/system.h"
#include "include/debug.h"
#ifdef __OPLDIAG
#include "include/fntsys.h"
#include <delaythread.h>
#endif
#include "include/config.h"
#include "include/util.h"
#include "include/extern_irx.h"

#include "include/supportbase.h"
#include "include/bdmsupport.h"
#include "include/ethsupport.h"
#include "include/udpfssupport.h"
#include "include/httpsupport.h"
#include "include/hddsupport.h"
#include "include/appsupport.h"
#include "include/favsupport.h"
#include "include/folderbrowse.h" // folderDepth -- favourites suppression inside subfolders
#include "include/mmcesupport.h"  // mmceLoadModules() -- MMCE boot branch of resolveBootDirToMass
#include "include/tar.h"          // tarInvalidate -- re-arm the .tar probe on a settings apply
#include "include/vcdsupport.h"
#include "include/cuesupport.h" // cueIsCueEntry -- a PS1 row names its own core   // VCD display naming + POPSTARTER launch helpers
#include "include/libview.h"    // libViewActive / libListViewActive -- which list this page shows
#ifdef RETROACHIEVEMENTS
#include "include/rabadge.h" // RA: "tracked game" badge in the list
#endif

#include "include/cheatman.h"
#include "include/sound.h"
#include "include/xparam.h"

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioDopen/fileXioIoctl2/fileXioDclose used by BDM autolaunch
#include <unistd.h>
#include <sys/stat.h> // mkdir() -- create mc?:/OPL before mirroring the settings-path redirect.
                      // Explicit rather than transitive on purpose: this tree builds WITHOUT -Wall,
                      // so an implicitly declared mkdir would compile silently and only go wrong at
                      // runtime. bdmsupport.c includes it for the same reason.
#ifdef PADEMU
#include <libds34bt.h>
#include <libds34usb.h>
#endif


// Auto-launch of the last played game from the boot arguments. Split out of opl.cpp.

void autoLaunchHDDGame(char *argv[])
{
    char path[256];
    char launchPart[sizeof(gOPLPart)];
    config_set_t *configSet;

    miniInit(HDD_MODE);

    gAutoLaunchGame = static_cast<hdl_game_info_t*>(malloc(sizeof(hdl_game_info_t)));
    if (gAutoLaunchGame == NULL) {
        miniDeinit(NULL);
        return;
    }
    memset(gAutoLaunchGame, 0, sizeof(hdl_game_info_t));

    // "%s", argv[1] -- NOT argv[1] as the format string. These come from another program's argv
    // (a launcher/shortcut), so a name containing a % sequence made snprintf walk a nonexistent
    // varargs list. Same below for argv[2] in the BDM twin.
    snprintf(gAutoLaunchGame->startup, sizeof(gAutoLaunchGame->startup), "%s", argv[1]);
    gAutoLaunchGame->start_sector = strtoul(argv[2], NULL, 0);
    snprintf(launchPart, sizeof(launchPart), "hdd0:%s", argv[3]);
    // argv[3] is a compatibility hint from the launching OPL instance. Re-read the authoritative
    // __common/OPL/conf_hdd.cfg policy in miniInit() rather than letting a stale launcher hint
    // overwrite the mounted data home used for CFG/VMC sidecars in this process.
    if (gOPLPart[0] != '\0' && strcmp(gOPLPart, launchPart) != 0)
        LOG("HDD mini launch: ignoring stale partition hint %s; policy selected %s\n", launchPart, gOPLPart);

    if (gHDDPrefix != NULL && gHDDPrefix[0] != '\0') {
        snprintf(path, sizeof(path), "%sCFG/%s.cfg", gHDDPrefix, gAutoLaunchGame->startup);
        configSet = configAlloc(0, NULL, path);
        configRead(configSet);
    } else {
        // HDL autolaunch does not require a persistent config/art PFS home. Keep the launch alive
        // with defaults only; never format a NULL prefix into a bogus path.
        configSet = configAlloc(0, NULL, NULL);
    }
    if (configSet == NULL) {
        free(gAutoLaunchGame);
        gAutoLaunchGame = NULL;
        miniDeinit(NULL);
        return;
    }
    // A menu launch gets #Startup from hddGetConfig; this config came straight from the CFG file, which
    // never stores '#' keys. sbGetCompatModes needs it to find a title's default compatibility modes.
    configSetStr(configSet, CONFIG_ITEM_STARTUP, gAutoLaunchGame->startup);

    hddLaunchGame(NULL, -1, configSet);

    // Only reached when the launch was refused. hddLaunchGame tears the mini session down only on the
    // way to a handoff, so finish it here: main() then boots the menu, which must not inherit a live
    // gAutoLaunchGame (every later menu launch would be treated as this autolaunch).
    if (gAutoLaunchGame != NULL) {
        LOG("AUTOLAUNCH HDD launch refused; falling back to the menu\n");
        free(gAutoLaunchGame);
        gAutoLaunchGame = NULL;
        miniDeinit(configSet);
    }
}

void autoLaunchBDMGame(char *argv[])
{
    char path[256];
    config_set_t *configSet;

    miniInit(BDM_MODE);

    gAutoLaunchBDMGame = static_cast<base_game_info_t*>(malloc(sizeof(base_game_info_t)));
    if (gAutoLaunchBDMGame == NULL) {
        miniDeinit(NULL);
        return;
    }
    memset(gAutoLaunchBDMGame, 0, sizeof(base_game_info_t));

    // nameLen MUST be initialised and the return MUST be checked. isValidIsoName (supportbase.c)
    // returns 0 WITHOUT writing *pNameLen for anything that is not *.iso/*.zso -- so an unchecked
    // call left nameLen holding stack garbage, which then became the length argument of the two
    // strncpy calls below and the index of the NUL store after them. Any launcher handing OPL a
    // name it does not recognise wrote an arbitrary distance past a 0x100-byte heap allocation.
    int nameLen = 0;
    int format = isValidIsoName(argv[1], &nameLen);
    // Reject unsupported / over-long filenames. Clamping nameLen instead would desync the
    // extension offset from the real suffix position, so bail rather than repair.
    if (format <= 0 || nameLen < 0 || nameLen > ISO_GAME_NAME_MAX) {
        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;
        miniDeinit(NULL);
        return;
    }
    if (format == GAME_FORMAT_OLD_ISO) {
        strncpy(gAutoLaunchBDMGame->name, &argv[1][GAME_STARTUP_MAX], nameLen);
        gAutoLaunchBDMGame->name[nameLen] = '\0';
        strncpy(gAutoLaunchBDMGame->extension, &argv[1][GAME_STARTUP_MAX + nameLen], sizeof(gAutoLaunchBDMGame->extension));
        gAutoLaunchBDMGame->extension[sizeof(gAutoLaunchBDMGame->extension) - 1] = '\0';
    } else {
        strncpy(gAutoLaunchBDMGame->name, argv[1], nameLen);
        gAutoLaunchBDMGame->name[nameLen] = '\0';
        strncpy(gAutoLaunchBDMGame->extension, &argv[1][nameLen], sizeof(gAutoLaunchBDMGame->extension));
        gAutoLaunchBDMGame->extension[sizeof(gAutoLaunchBDMGame->extension) - 1] = '\0';
    }

    snprintf(gAutoLaunchBDMGame->startup, sizeof(gAutoLaunchBDMGame->startup), "%s", argv[2]);

    if (strcasecmp("DVD", argv[3]) == 0)
        gAutoLaunchBDMGame->media = SCECdPS2DVD;
    else if (strcasecmp("CD", argv[3]) == 0)
        gAutoLaunchBDMGame->media = SCECdPS2CD;

    gAutoLaunchBDMGame->format = format;
    gAutoLaunchBDMGame->parts = 1; // ul not supported.

    gAutoLaunchDeviceData = static_cast<bdm_device_data_t*>(malloc(sizeof(bdm_device_data_t)));
    if (gAutoLaunchDeviceData == NULL) {
        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;
        miniDeinit(NULL);
        return;
    }
    memset(gAutoLaunchDeviceData, 0, sizeof(bdm_device_data_t));
    // memset leaves these at 0, and 0 is a VALID BDM_TYPE / a valid UDMA mode -- so an autolaunch
    // that never identifies the device would claim a concrete type instead of "unknown".
    gAutoLaunchDeviceData->bdmDeviceType = BDM_TYPE_UNKNOWN;
    gAutoLaunchDeviceData->bdmHddIsLBA48 = -1;
    gAutoLaunchDeviceData->ataHighestUDMAMode = -1;

    char apaDevicePrefix[BDM_DEVICE_ROOT_MAX] = {0};
    // The driver name and device index MUST be recorded together with the slot they came from.
    // Reading them straight into gAutoLaunchDeviceData meant every probed slot overwrote them, so
    // after the loop they described the LAST slot opened while apaDevicePrefix still named the
    // first -- the config path and the device identity disagreed whenever more than one mass
    // device was present.
    int selectedMassSlot = -1;
    // Upstream retries mass0: forever, so a device that never publishes a filesystem hung the
    // autolaunch on a black screen. Bound it (delay() ticks are ~0.25 s, so about 12 s) and let the
    // menu boot instead, where the device and its error are visible.
    int mass0Retries = 0;
    delay(8);
    // Loop through mass0: to mass4:
    for (int i = 0; i <= 4; i++) {
        snprintf(path, sizeof(path), "mass%d:/", i);
        int dir = fileXioDopen(path);

        if (dir >= 0) {
            char detectedDriver[sizeof(gAutoLaunchDeviceData->bdmDriver)] = {0};
            int detectedDeviceIndex = -1;

            // Every slot in this sweep gets asked, including ones whose backing device has gone --
            // and asking for the name WITH a return buffer there faults the IOP outright. Boot is
            // the worst place to do that, so go through the guarded reader and only chase the
            // device number once it has confirmed a mounted block device. See bdmReadDriverName().
            if (bdmReadDriverName(dir, detectedDriver, sizeof(detectedDriver)) >= 0)
                fileXioIoctl2(dir, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, &detectedDeviceIndex, sizeof(detectedDeviceIndex));
            fileXioDclose(dir);

            if (selectedMassSlot < 0) {
                selectedMassSlot = i;
                snprintf(gAutoLaunchDeviceData->bdmDriver, sizeof(gAutoLaunchDeviceData->bdmDriver), "%s", detectedDriver);
                gAutoLaunchDeviceData->massDeviceIndex = detectedDeviceIndex;
                snprintf(apaDevicePrefix, sizeof(apaDevicePrefix), "mass%d:", i);
            }

            if (!strcmp(detectedDriver, "ata") && strlen(detectedDriver) == 3) {
                selectedMassSlot = i;
                snprintf(gAutoLaunchDeviceData->bdmDriver, sizeof(gAutoLaunchDeviceData->bdmDriver), "%s", detectedDriver);
                gAutoLaunchDeviceData->massDeviceIndex = detectedDeviceIndex;
                bdmResolveLBA_UDMA(gAutoLaunchDeviceData); // fills bdmHddIsLBA48 / ataHighestUDMAMode
                snprintf(apaDevicePrefix, sizeof(apaDevicePrefix), "mass%d:", i);
                break; // Exit the loop if "ata" device is found
            }
        } else {
            // Retry for mass0: only, and only for a bounded time.
            if (i == 0 && mass0Retries++ < AUTOLAUNCH_MASS0_RETRIES) {
                delay(6);
                i--;
            } else {
                break;
            }
        }
        delay(6);
    }

    // No mass device answered at all. The old mass0: guess could only build a path to a file that is
    // not there, and the launch error behind it has no screen to show on.
    if (selectedMassSlot < 0) {
        LOG("AUTOLAUNCH no mass device answered; falling back to the menu\n");
        free(gAutoLaunchDeviceData);
        gAutoLaunchDeviceData = NULL;
        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;
        miniDeinit(NULL);
        return;
    }

    // bdmDeviceRoot was never written on this path, so the launch legs that resolve paths through
    // it saw an empty string.
    snprintf(gAutoLaunchDeviceData->bdmDeviceRoot, sizeof(gAutoLaunchDeviceData->bdmDeviceRoot), "%s", apaDevicePrefix);

    if (gBDMPrefix[0] != '\0') {
        snprintf(path, sizeof(path), "%s%s/CFG/%s.cfg", apaDevicePrefix, gBDMPrefix, gAutoLaunchBDMGame->startup);
        snprintf(gAutoLaunchDeviceData->bdmPrefix, sizeof(gAutoLaunchDeviceData->bdmPrefix), "%s%s/", apaDevicePrefix, gBDMPrefix);
    } else {
        snprintf(path, sizeof(path), "%sCFG/%s.cfg", apaDevicePrefix, gAutoLaunchBDMGame->startup);
        snprintf(gAutoLaunchDeviceData->bdmPrefix, sizeof(gAutoLaunchDeviceData->bdmPrefix), "%s", apaDevicePrefix);
    }


    configSet = configAlloc(0, NULL, path);
    configRead(configSet);
    // A menu launch gets #Startup from sbPopulateConfig; this config came straight from the CFG file, which
    // never stores '#' keys. sbGetCompatModes needs it to find a title's default compatibility modes.
    if (configSet != NULL)
        configSetStr(configSet, CONFIG_ITEM_STARTUP, gAutoLaunchBDMGame->startup);

    bdmLaunchGame(NULL, -1, configSet);

    // Only reached when the launch was refused (missing ISO, fragmented image, unusable device).
    // bdmLaunchGame tears the mini session down only on the way to a handoff; a Neutrino abort that
    // already did so has cleared gAutoLaunchBDMGame. Finish it here, or main() would boot the menu with
    // live autolaunch state and treat every later menu launch as this autolaunch.
    if (gAutoLaunchBDMGame != NULL) {
        LOG("AUTOLAUNCH BDM launch refused; falling back to the menu\n");
        free(gAutoLaunchDeviceData);
        gAutoLaunchDeviceData = NULL;
        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;
        miniDeinit(configSet);
    }
}

