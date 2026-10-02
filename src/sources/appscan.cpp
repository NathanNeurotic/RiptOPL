/*
  Copyright 2009, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/settings.h"
#include "include/oplint.h"
#include "include/opl.h"
#include "include/sourceregistry.h" // SourceRegistry gSourceRegistry
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
#include "include/gamesource.h"

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


// Apps list scanning and the legacy apps config lookups. Split out of opl.cpp.

// App support stuff: raised when an APPS rescan is due (see initSupport), consumed by oplShouldAppsUpdate().
unsigned char gShouldAppsUpdate;

// Art lookup addressed by IO MODE rather than by a device string. appsupport resolves each app's art
// source to a mode once at scan time (appSetArtSource), so the per-cover lookup does not have to
// re-derive it from a path on every request. Body lives in SourceRegistry::appImageByMode (opl.cpp).
int oplGetAppImageByMode(int mode, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    return gSourceRegistry.appImageByMode(mode, folder, isRelative, value, suffix, resultTex, psm);
}

static int scanApps(int (*callback)(const char *path, config_set_t *appConfig, void *arg), void *arg, char *appsPath, int exception)
{
    struct dirent *pdirent;
    DIR *pdir;
    int count, ret;
    config_set_t *appConfig;
    char dir[128];
    char path[128];

    count = 0;
    if ((pdir = opendir(appsPath)) != NULL) {
        while ((pdirent = readdir(pdir)) != NULL) {
            if (exception && strchr(pdirent->d_name, '_') == NULL)
                continue;

            if (strcmp(pdirent->d_name, ".") == 0 || strcmp(pdirent->d_name, "..") == 0)
                continue;

            snprintf(dir, sizeof(dir), "%s/%s", appsPath, pdirent->d_name);
            if (pdirent->d_type != DT_DIR)
                continue;

            snprintf(path, sizeof(path), "%s/%s", dir, APP_TITLE_CONFIG_FILE);
            appConfig = configAlloc(0, NULL, path);
            if (appConfig != NULL) {
                configRead(appConfig);

                ret = callback(dir, appConfig, arg);
                configFree(appConfig);

                if (ret == 0)
                    count++;
                else if (ret < 0) { // Stopped because of unrecoverable error.
                    break;
                }
            }
        }

        closedir(pdir);
    } else
        LOG("APPS failed to open dir %s\n", appsPath);

    return count;
}

int oplScanApps(int (*callback)(const char *path, config_set_t *appConfig, void *arg), void *arg)
{
    int i, count;
    item_list_t *listSupport;
    char appsPath[64];

    count = 0;
    for (i = 0; i < MODE_COUNT; i++) {
        listSupport = (*oplGetModule(i)).support;
        if ((listSupport != NULL) && (listSupport->enabled) && (gsOf(listSupport)->hasPrefix())) {
            char *prefix = gsOf(listSupport)->prefix(listSupport);
            /*
              #253: never scan a DEVICE-LESS prefix.

              An enabled-but-unmounted slot (a BDM index with nothing plugged in, ETH with no
              share) returns an EMPTY prefix. "%sAPPS" then yields the bare relative path "APPS",
              which the PS2 resolves against the CWD -- OPL's own boot folder. When OPL boots from
              a device root (PixeliGer's case: USB), that is the SAME directory the real mass0:
              prefix already scanned, so every app was discovered twice and appeared twice in the
              list.

              It also explains why dedup did not help: the two hits arrive with different path
              strings ("mass0:APPS/..." vs "APPS/..."), so they are legitimately distinct entries
              as far as the dedup key is concerned.
            */
            if (prefix == NULL || prefix[0] == '\0')
                continue;
            snprintf(appsPath, sizeof(appsPath), "%sAPPS", prefix);
            count += scanApps(callback, arg, appsPath, 0);
        }
    }

    for (i = 0; i < 2; i++) {
        snprintf(appsPath, sizeof(appsPath), "mc%d:", i);
        count += scanApps(callback, arg, appsPath, 1);
    }

    return count;
}

int oplShouldAppsUpdate(void)
{
    int result;

    result = (int)gShouldAppsUpdate;
    gShouldAppsUpdate = 0;

    return result;
}

config_set_t *oplGetLegacyAppsConfig(void)
{
    int i, fd;
    item_list_t *listSupport;
    config_set_t *appConfig;
    char appsPath[128];

    snprintf(appsPath, sizeof(appsPath), "mc?:OPL/conf_apps.cfg");
    fd = openFile(appsPath, O_RDONLY);
    if (fd >= 0) {
        appConfig = configAlloc(CONFIG_APPS, NULL, appsPath);
        close(fd);
        return appConfig;
    }

    for (i = MODE_COUNT - 1; i >= 0; i--) {
        listSupport = (*oplGetModule(i)).support;
        if ((listSupport != NULL) && (listSupport->enabled) && (gsOf(listSupport)->hasPrefix())) {
            char *prefix = gsOf(listSupport)->prefix(listSupport);
            if (prefix == NULL || prefix[0] == '\0')
                continue;
            snprintf(appsPath, sizeof(appsPath), "%sconf_apps.cfg", prefix);

            fd = openFile(appsPath, O_RDONLY);
            if (fd >= 0) {
                appConfig = configAlloc(CONFIG_APPS, NULL, appsPath);
                close(fd);
                return appConfig;
            }
        }
    }

    /* Apps config not found on any device, go with last tested device.
       Does not matter if the config file could be loaded or not */
    appConfig = configAlloc(CONFIG_APPS, NULL, appsPath);

    return appConfig;
}

config_set_t *oplGetLegacyAppsInfo(char *name)
{
    int i, fd;
    item_list_t *listSupport;
    config_set_t *appConfig;
    char appsPath[128] = {0};

    for (i = MODE_COUNT - 1; i >= 0; i--) {
        listSupport = (*oplGetModule(i)).support;
        if ((listSupport != NULL) && (listSupport->enabled) && (gsOf(listSupport)->hasPrefix())) {
            char *prefix = gsOf(listSupport)->prefix(listSupport);
            if (prefix == NULL || prefix[0] == '\0')
                continue;
            snprintf(appsPath, sizeof(appsPath), "%sCFG%s%s.cfg", prefix, i == ETH_MODE ? "\\" : "/", name);

            fd = openFile(appsPath, O_RDONLY);
            if (fd >= 0) {
                appConfig = configAlloc(0, NULL, appsPath);
                close(fd);
                return appConfig;
            }
        }
    }

    /* Apps config not found on any device, go with the last tested device when one had a
       real prefix. A valid list-only APA session can have no persistent PFS prefix at all; in that
       state return metadata-only config instead of inventing a path on another device. */
    appConfig = configAlloc(0, NULL, appsPath[0] != '\0' ? appsPath : NULL);

    return appConfig;
}
