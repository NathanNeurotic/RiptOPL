/*
  Copyright 2009, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/settings.h"
#include "include/oplint.h"
#include "include/oplog.h"
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


// NBD server (lwNBD) launch/teardown. Split out of opl.cpp.

// ----------------------------------------------------------
// -------------------- NBD SRV Support ---------------------
// ----------------------------------------------------------


constexpr int LWNBD_ART_DRAIN_TICKS = 500;
constexpr int LWNBD_IO_DRAIN_TICKS = 1000;

static void shutdownLwnbdNetwork(void)
{
    // The loaded-check mirrors ethShutdown: DEV9's refcount is shared with BDM/HDD, so only
    // release it when the eth path actually holds it.
    int ethWasLoaded = ethGetModulesLoaded();

    ethDeinitModules();
    if (ethWasLoaded)
        sysShutdownDev9();
}

static int loadLwnbdSvr(int *teardownStarted)
{
    int ret, padStatus, padTries;
    int ethWasLoadedBeforePreflight;
    struct lwnbd_config
    {
        char defaultexport[32];
        uint8_t readonly;
    };
    struct lwnbd_config config;

    *teardownStarted = 0;

    /* compat stuff for user not providing name export (useless when there was only one export) */
    ret = strlen(gExportName);
    if (ret == 0)
        strcpy(config.defaultexport, "hdd0");
    else
        strcpy(config.defaultexport, gExportName);

    config.readonly = !gEnableWrite;

    /*
     * Prove that the NIC can establish link and configuration while the live menu
     * is still intact. In particular, an unplugged cable must not send the failure
     * path through audioEnd(), unloadPads(), or the IOP reset used to restore a
     * successfully-started server -- that is the reported freeze at the
     * "NBD Server unloading..." screen (#307).
     */
    ethWasLoadedBeforePreflight = ethGetModulesLoaded();
    ret = ethLoadInitModules();
    if (ret != 0) {
        if (!ethWasLoadedBeforePreflight)
            shutdownLwnbdNetwork();
        return ret;
    }

    /*
     * If preflight brought up the stack solely for this check, return DEV9 to its
     * prior ownership state. The normal post-teardown load below then starts from
     * the same clean state as every other NBD launch.
     */
    if (!ethWasLoadedBeforePreflight)
        shutdownLwnbdNetwork();

    /*
     * Support cleanup invalidates device-owned lists used by art requests.
     * If the bounded drain cannot make that ownership safe, keep the live menu
     * intact and let the user retry.
     */
    if (!cacheAbortMmceImageLoadsTimed(LWNBD_ART_DRAIN_TICKS) ||
        !cacheCancelPendingImageLoadsTimed(LWNBD_ART_DRAIN_TICKS))
        return -1;

    /*
     * Block new foreground work and bound the drain while the rest of the menu
     * is still live. A stuck request therefore fails the start instead of
     * freezing after audio and pads have already been dismantled.
     */
    ioBlockOpsTimed(1, LWNBD_IO_DRAIN_TICKS);
    if (ioHasPendingRequests()) {
        ioBlockOps(0);
        return -1;
    }

    *teardownStarted = 1;

    // deinit audio lib while nbd server is running
    audioEnd();

    guiExecDeferredOps();

    // Deinitialize all support without shutting down the HDD unit.
    // NOTE(rebuild): the fork passes a 3rd "spare a second mode" argument here; this tree's
    // deinitAllSupport is still the 2-arg form, so the call is adapted rather than copied.
    deinitAllSupport(NO_EXCEPTION, IO_MODE_SELECTED_ALL);
    clearErrorMessage(); /* At this point, an error might have been displayed (since background tasks were completed).
                            Clear it, otherwise it will get displayed after the server is closed. */

    unloadPads();

    ret = ethLoadInitModules();
    if (ret == 0) {
        ret = sysLoadModuleBuffer(&ps2atad_irx, size_ps2atad_irx, 0, NULL); /* gHDDStartMode ? */
        if (ret >= 0) {
            ret = sysLoadModuleBuffer(&lwnbdsvr_irx, size_lwnbdsvr_irx, sizeof(config), (char *)&config);
            if (ret >= 0)
                ret = 0;
        }
    }

    if (ret != 0)
        shutdownLwnbdNetwork();

    padInit(0);

    // init all pads, but never wedge the status screen on a failed reconnect
    padStatus = 0;
    padTries = 0;
    while (!padStatus && padTries++ < 100) {
        padStatus = startPads();
        if (!padStatus)
            delay(50);
    }
    if (!padStatus)
        LOG("loadLwnbdSvr: pads did not start after teardown (%d tries)\n", padTries);

    // now ready to display some status

    return ret;
}


static void unloadLwnbdSvr(void)
{
    // Release the live server stack before resetting the IOP (#307): reset() -> sysReset() parks
    // the EE in unbounded SifIopReset/SifIopSync waits, which wedge against a live, DEV9-powered
    // stack. This function is now only reached when loadLwnbdSvr actually dismantled the menu
    // (teardownStarted), so a failed bring-up never gets here at all.
    shutdownLwnbdNetwork();
    unloadPads();

    reset();

    LOG_INIT();
    LOG_ENABLE();

    // reinit the input pads
    padInit(0);

    /* Bounded retry (#307): an unbounded startPads loop turns a half-completed IOP reset into a
     * hard freeze at exactly the reported "NBD Server unloading..." screen. ~5 s of retries covers
     * a slow IOP; if pads still won't start, degrade (log + continue, the menu's own pad handling
     * recovers on later polls) instead of wedging forever. */
    int ret = 0, tries = 0;
    while (!ret && tries++ < 100) {
        ret = startPads();
        if (!ret)
            delay(50);
    }
    if (!ret)
        LOG("unloadLwnbdSvr: pads did not start after IOP reset (%d tries)\n", tries);

    // now start io again
    ioBlockOps(0);

    // init all supports again
    initAllSupport(1);

    audioInit();
    sfxInit(0);
    if (gEnableBGM)
        bgmStart();
}

void handleLwnbdSrv()
{
    char temp[256];
    int teardownStarted;

    // prepare for lwnbd, display screen with info
    guiRenderTextScreen(_l(_STR_STARTINGNBD));
    if (loadLwnbdSvr(&teardownStarted) == 0) {
        snprintf(temp, sizeof(temp), "%s", _l(_STR_RUNNINGNBD));
        guiMsgBox(temp, 0, NULL);
    } else
        guiMsgBox(_l(_STR_STARTFAILNBD), 0, NULL);

    // Only a path that actually dismantled the menu needs the reset-and-restore cycle. A failed
    // preflight (no cable/link) leaves the menu live, so it must NOT reach the IOP reset -- that
    // reset is the reported freeze at the "NBD Server unloading..." screen (#307).
    if (teardownStarted) {
        guiRenderTextScreen(_l(_STR_UNLOADNBD));
        unloadLwnbdSvr();
    }
}

// ----------------------------------------------------------
// --------------------- Init/Deinit ------------------------
// ----------------------------------------------------------
