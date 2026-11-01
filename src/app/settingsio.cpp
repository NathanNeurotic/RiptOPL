/*
  Copyright 2009, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/settings.h"
#include "include/diaglog.h" // the hardware log, DIAG.TXT (RIPPS2 build 115)
#include "include/oplint.h"
#include "include/diag.h"
#include "include/opl.h"
#include "include/ioman.h"
#include "include/launchdiag.h" // UDPBD hang-triage stage markers (gated on gLaunchDiag)
#include "include/gui.h"
#include "include/guigame.h"
#include "include/renderman.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/themeviews.h"
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
#include "include/grid.h" // GRID_LAYOUTS -- clamp a hand-edited grid_layout

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


// Settings persistence: where the settings live (boot-home resolution, config-path redirect, recovery), loading and saving them,
// and applyConfig/saveConfig. Split out of opl.cpp.

// ----------------------------------------------------------
// ------------------ Configuration handling ----------------
// ----------------------------------------------------------

static int lscstatus = CONFIG_ALL;
static int lscret = 0;
static char gLastSaveTarget[sizeof(gCustomSettingsPath)];
static int gLastSaveWasStagedOplHome = 0;
static int gHddSettingsFallbackNotice = 0;
static int gBootHddCommonFallback = 0;

static int checkLoadConfigMMCE(int types)
{
    int value;
    DIR *dir = opendir("mmce0:");
    if (dir != NULL) {
        closedir(dir);
        configEnd();
        configInit("mmce0:");
        value = configReadMulti(types);
        if (value & CONFIG_OPL) {
            config_set_t *configOPL = configGetByType(CONFIG_OPL);
            configSetInt(configOPL, CONFIG_OPL_MMCE_MODE, START_MODE_AUTO);
            return value;
        }
    }

    dir = opendir("mmce1:");
    if (dir != NULL) {
        closedir(dir);
        configEnd();
        configInit("mmce1:");
        value = configReadMulti(types);
        if (value & CONFIG_OPL) {
            config_set_t *configOPL = configGetByType(CONFIG_OPL);
            configSetInt(configOPL, CONFIG_OPL_MMCE_MODE, START_MODE_AUTO);
            return value;
        }
    }

    return 0;
}

static int checkLoadConfigHDD(int types)
{
    int value;
    char path[64];

    hddLoadModules();
    hddLoadSupportModules();
    if (gHDDPrefix == NULL) {
        LOG("CONFIG HDD load skipped: no existing PFS data home is mounted\n");
        return 0;
    }

    snprintf(path, sizeof(path), "%s%s", gHDDPrefix, CONFIG_OPL_FILENAME);
    value = open(path, O_RDONLY);
    if (value < 0) {
        // Legacy fallback so an existing conf_riptopl.cfg install is still found (auto-migrates on save).
        snprintf(path, sizeof(path), "%s%s", gHDDPrefix, CONFIG_OPL_FILENAME_LEGACY);
        value = open(path, O_RDONLY);
    }
    if (value >= 0) {
        close(value);
        configEnd();
        configInit(gHDDPrefix);
        value = configReadMulti(types);
        config_set_t *configOPL = configGetByType(CONFIG_OPL);
        configSetInt(configOPL, CONFIG_OPL_HDD_MODE, START_MODE_AUTO);
        return value;
    }

    return 0;
}

// The boot device's filesystem cannot exist until AFTER the config has been read, because bringing
// its transport up needs values THAT LIVE IN THAT CONFIG -- the PS2's static IP for every network
// transport, plus server/share/user/password for SMB. That is a real bootstrap cycle, unlike a BDM
// boot where loading a driver needs no configuration at all.
//
// Set from the boot TOKEN by resolveBootDirToMass(). Deliberately NOT named "is a network boot":
// UDPBD and UDPFS-BD are BLOCK devices that mount as massN: exactly like USB, and their "udp" driver
// token is unreadable until the transport is up -- so those boots are indistinguishable from a local
// one here and correctly leave this at 0. See the classification branch for what that costs.
static int gBootHomeDeferred = 0;
// Set for an APA/PFS launch identity, even when the first persistent-home mount attempt fails.
// Used after config read because a stored HDD=Disabled value must not turn off the transport OPL
// itself was launched from, and by discovery to prohibit an unrelated MC write fallback.
static int gBootHomeApa = 0;
// 1 while the settings home is an APA+exFAT hybrid's exFAT volume (set by adoptHybridExfatHome).
static int gBootHomeHybridExfat = 0;

// Basename of the ELF OPL was booted as (argv[0]); pairs with gBootDir. The BDM resolver uses it
// to verify typed launch aliases and a legacy bare mass: token. An explicit massN: is already a
// concrete slot and is identified from that slot's own ioctls instead. Empty when argv[0] had no
// filename part or the boot dir came from getcwd().
char gBootElfName[64];
// BDM_TYPE_* of the resolved boot device, BDM_TYPE_UNKNOWN for an unclassified boot. The save-path
// retry pins a later re-resolve to this type; config recovery also uses it to keep a known BDM boot
// from probing unrelated storage classes.
static int gBootDirBdmType = BDM_TYPE_UNKNOWN;
// True for a typed BDM boot or a concrete massN: token, even when the latter has not finished
// registering and therefore has no driver classification yet. This keeps a missing first-run file
// on that explicit slot from triggering the legacy all-device recovery scan.
static int gBootHomeBdm = 0;

// The built-in themes have no theme directory. Their conventional BGM belongs to the normal OPL
// data home, never POPS: `pfs0:OPL/THM/bgm.ogg` for __common/OPL and `pfs0:THM/bgm.ogg` for +OPL.
// Every non-APA home is the exact RiptOPL working directory resolved at boot. Do not turn this into
// a directory scan: a missing file is simply silence.
int oplGetDefaultThemeBgmPath(char *path, int pathSize)
{
    int length;

    if (path == NULL || pathSize <= 1)
        return 0;

    path[0] = '\0';
    if (gBootHomeApa) {
        if (gHDDPrefix == NULL || gHDDPrefix[0] == '\0')
            return 0;

        length = snprintf(path, pathSize, "%sTHM/bgm.ogg", gHDDPrefix);
    } else {
        size_t baseLength;

        if (gBootDir[0] == '\0')
            return 0;

        baseLength = strlen(gBootDir);
        if (gBootDir[baseLength - 1] == ':' || gBootDir[baseLength - 1] == '/' ||
            gBootDir[baseLength - 1] == '\\')
            length = snprintf(path, pathSize, "%sbgm.ogg", gBootDir);
        else
            length = snprintf(path, pathSize, "%s/bgm.ogg", gBootDir);
    }

    if (length < 0 || length >= pathSize) {
        path[0] = '\0';
        return 0;
    }

    return 1;
}

// When this function is called, the current device for loading/saving config is the memory card.
// "Custom Settings Path" bootstrap.
//
// THE CHICKEN-AND-EGG: the user's chosen path is stored in CONFIG_OPL_CUSTOM_SETTINGS_PATH, which
// lives INSIDE the config file that path points at. At boot we cannot read the setting without
// already knowing it. So the path is ALSO mirrored to a tiny plain-text "config.path" file in the
// CWD -- readable before any device driver is up, because it is where OPL was launched from. Boot
// reads the redirect; the setting inside the config is what the GUI edits and what keeps the two in
// sync on save.
//
// ⚠ THAT PREMISE HAS ONE EXCEPTION, and it is the whole reason for the mc mirror below: on a NETWORK
// boot the CWD *is* the share, and mounting the share needs the static IP (and for SMB the server,
// share, user and password) out of the very config this redirect exists to locate. So there the CWD
// copy is unreadable at exactly the moment it is needed, and a custom settings path set by a network
// booter was silently lost on the next boot.
static const char *configPathRedirectFile = "config.path";

// Prefer the concrete slot OPL actually booted from. With both cards inserted, a generic
// first-present probe must not redirect an mc1 boot's bootstrap/config traffic onto mc0. For
// non-MC boots retain the historical first-present fallback.
static int preferredMcSlot(void)
{
    if (!strncmp(gBootDir, "mc0:", 4))
        return 0;
    if (!strncmp(gBootDir, "mc1:", 4))
        return 1;
    return sysCheckMC();
}

// Absolute mc-side location for the bootstrap mirror. A concrete MC boot owns its own slot;
// network/bare boots use the existing first-present policy. Returns 0 when there is none.
static int mcConfigPathRedirect(char *out, int outLen)
{
    int slot = preferredMcSlot();

    if (slot < 0)
        return 0;

    snprintf(out, outLen, "mc%d:/OPL/%s", slot, configPathRedirectFile);
    return 1;
}

static int configPathRedirectLocation(char *out, int outLen)
{
    const char *home = gBootDir;

    // APA launch identity and config ownership are separate. A delayed HDD mount can leave
    // gBootDir as raw hddN:, but once the EXISTING persistent PFS home is mounted, config.path
    // belongs there. This is the only APA override; every other boot class keeps gBootDir.
    if (gBootHomeApa && gHDDPrefix != NULL && gHDDPrefix[0] != '\0')
        home = gHDDPrefix;

    // Never compose a writable bootstrap file in raw APA space. hddN: is a partition
    // namespace, not a PFS filesystem; any file-like O_CREAT there is unsafe by definition.
    if (!strncmp(home, "hdd", 3)) {
        out[0] = '\0';
        return 0;
    }

    if (home[0] != '\0') {
        size_t len = strlen(home);
        if (len > 0 && (home[len - 1] == ':' || home[len - 1] == '/' || home[len - 1] == '\\'))
            snprintf(out, outLen, "%s%s", home, configPathRedirectFile);
        else
            snprintf(out, outLen, "%s/%s", home, configPathRedirectFile);
    } else {
        // Read-only legacy discovery may still inspect a relative redirect. Writers reject this
        // case below because an unknown CWD could itself be raw hddN: space.
        snprintf(out, outLen, "%s", configPathRedirectFile);
    }

    return 1;
}

static int readConfigPathRedirect(char *outPath, int outPathLen)
{
    int fd;
    int len;
    char redirectFile[sizeof(gBootDir) + 32];

    // APA launchers can leave the process CWD in raw hdd0: space. Once the data partition
    // is mounted, anchor the bootstrap pointer to the resolved PFS config home instead of
    // sending an ordinary file open through the raw APA namespace.
    if (!configPathRedirectLocation(redirectFile, sizeof(redirectFile)))
        return 0;
    fd = open(redirectFile, O_RDONLY);
    if (fd < 0) {
        char mcPath[64];

        if (!gBootHomeDeferred || !mcConfigPathRedirect(mcPath, sizeof(mcPath)))
            return 0;

        fd = open(mcPath, O_RDONLY);
        if (fd < 0)
            return 0;
    }

    len = read(fd, outPath, outPathLen - 1);
    close(fd);
    if (len <= 0)
        return 0;

    while (len > 0 && (outPath[len - 1] == '\r' || outPath[len - 1] == '\n' || outPath[len - 1] == ' ' || outPath[len - 1] == '\t'))
        len--;
    outPath[len] = '\0';

    return len > 0;
}

static int writeConfigPathRedirect(const char *path)
{
    char redirectFile[sizeof(gBootDir) + 32];
    int fd;
    int primaryOk = 0;

    // No boot identity means no provably safe place for an O_CREAT bootstrap file:
    // the launcher may have left CWD in raw APA space. The config payload may still save via
    // normal legacy discovery, but we do not manufacture a redirect in an unknown namespace.
    if (gBootDir[0] == '\0') {
        LOG("CONFIG refusing relative config.path write with unknown boot CWD\n");
        return 0;
    }
    if (!configPathRedirectLocation(redirectFile, sizeof(redirectFile))) {
        LOG("CONFIG refusing config.path write in raw APA boot namespace %s\n", gBootDir);
        return 0;
    }
    fd = open(redirectFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        int n1 = write(fd, path, strlen(path));
        int n2 = write(fd, "\n", 1);
        close(fd);
        primaryOk = (n1 == (int)strlen(path) && n2 == 1);
    }

    if (gBootHomeDeferred) {
        char mcPath[64];
        int mirrorOk = 0;

        if (mcConfigPathRedirect(mcPath, sizeof(mcPath))) {
            fd = open(mcPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd < 0) {
                char mcDir[64];
                snprintf(mcDir, sizeof(mcDir), "mc%d:/OPL", sysCheckMC());
                mkdir(mcDir, 0777);
                fd = open(mcPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            }
            if (fd >= 0) {
                int n1 = write(fd, path, strlen(path));
                int n2 = write(fd, "\n", 1);
                close(fd);
                mirrorOk = (n1 == (int)strlen(path) && n2 == 1);
                if (mirrorOk)
                    LOG("CONFIG mirrored settings path to %s\n", mcPath);
            }
            if (!mirrorOk)
                LOG("CONFIG could not mirror settings path to %s\n", mcPath);
        }

        // The card mirror is the only bootstrap copy a deferred network boot can read.
        return mirrorOk;
    }

    return primaryOk;
}

// Convert an explicit APA settings target to the already-owned pfs0: mount. OPL's HDD
// support owns pfs0: for gOPLPart; Step-208 deliberately refuses to remount another APA
// partition (or borrow transient pfs1:) merely to store settings.
static int prepareCustomApaSettingsPath(char *path, int pathLen)
{
    char targetPart[64] = {0};
    char subfolder[64] = {0};
    char canonical[64] = {0};
    const char *p = path;
    const char *unit = "hdd0";
    int isPfsAlias = 0;
    size_t i = 0;
    DIR *dir;
    int n;

    if (path == NULL || path[0] == '\0') {
        gLastSaveErrno = ENODEV;
        return -1;
    }

    // Accept both pfs: and pfs0: as user-facing aliases for OPL's already-mounted persistent
    // data home. pfs1: remains reserved for transient HDD scanning and is never a settings home.
    if (!strncmp(path, "pfs", 3)) {
        if (path[3] == ':') {
            p = path + 4;
            isPfsAlias = 1;
        } else if (path[3] == '0' && path[4] == ':') {
            p = path + 5;
            isPfsAlias = 1;
        } else {
            gLastSaveErrno = ENODEV;
            return -1;
        }
    } else if (path[0] == '+') {
        p = path;
    } else {
        if (strncmp(path, "hdd", 3) != 0 || (path[3] != '0' && path[3] != '1') || path[4] != ':') {
            LOG("CONFIG malformed APA settings path %s rejected\n", path);
            gLastSaveErrno = ENODEV;
            return -1;
        }
        if (path[3] == '1')
            unit = "hdd1";
        p = path + 5;
        while (*p == '/' || *p == ':' || *p == '\\')
            p++;
    }

    if (!hddLoadModulesReady()) {
        gLastSaveErrno = ENODEV;
        return -1;
    }
    hddLoadSupportModules();
    if (gOPLPart[0] == '\0' || gHDDPrefix == NULL || gHDDPrefix[0] == '\0') {
        gLastSaveErrno = ENODEV;
        return -1;
    }

    const char *activeLabel = strchr(gOPLPart, ':');
    activeLabel = activeLabel ? activeLabel + 1 : gOPLPart;
    // Only the canonical __common fallback maps the OPL data home below a physical OPL/
    // subdirectory. Every valid conf_hdd.cfg-selected partition, including a non-+ label,
    // owns its PFS root directly.
    int commonStyleHome = !strcmp(gOPLPart, "hdd0:__common");

    if (isPfsAlias) {
        // Compatibility spellings seen on hardware include pfs0:, pfs:/__common/OPL and
        // pfs0:/OPL. They all mean the ACTIVE already-mounted data home; never reinterpret a
        // pfs alias as permission to mount another APA partition.
        while (*p == '/' || *p == ':' || *p == '\\')
            p++;

        size_t labelLen = strlen(activeLabel);
        if (labelLen > 0 && !strncmp(p, activeLabel, labelLen) &&
            (p[labelLen] == '\0' || p[labelLen] == '/' || p[labelLen] == ':' || p[labelLen] == '\\')) {
            p += labelLen;
            while (*p == '/' || *p == ':' || *p == '\\')
                p++;
        } else if ((*p == '+' || !strncmp(p, "__", 2)) && *p != '\0') {
            LOG("CONFIG PFS settings alias %s names a non-active APA partition\n", path);
            gLastSaveErrno = ENODEV;
            return -1;
        }
    } else {
        i = 0;
        while (*p != '\0' && *p != '/' && *p != ':' && *p != '\\' && i < sizeof(subfolder) - 1)
            subfolder[i++] = *p++;
        subfolder[i] = '\0';
        if (subfolder[0] == '\0') {
            gLastSaveErrno = ENODEV;
            return -1;
        }
        snprintf(targetPart, sizeof(targetPart), "%s:%s", unit, subfolder);
        if (strcmp(targetPart, gOPLPart) != 0) {
            LOG("CONFIG APA settings partition %s rejected; active OPL data partition is %s\n", targetPart, gOPLPart);
            gLastSaveErrno = ENODEV;
            return -1;
        }

        while (*p != '\0') {
            while (*p == ':' || *p == '/' || *p == '\\')
                p++;
            if (!strncmp(p, "pfs:", 4)) {
                p += 4;
                continue;
            }
            break;
        }
    }

    // __common's HDD-notation data home is already pfs0:OPL/. Consume one redundant OPL
    // component from hdd0:/__common/OPL. Direct pfs0: syntax remains direct PFS syntax.
    if (!isPfsAlias && commonStyleHome && !strncmp(p, "OPL", 3) &&
        (p[3] == '\0' || p[3] == '/' || p[3] == ':' || p[3] == '\\')) {
        p += 3;
        while (*p == ':' || *p == '/' || *p == '\\')
            p++;
    }

    i = 0;
    while (*p != '\0' && i < sizeof(subfolder) - 1) {
        char c = *p++;
        subfolder[i++] = (c == '\\') ? '/' : c;
    }
    subfolder[i] = '\0';
    while (i > 0 && subfolder[i - 1] == '/')
        subfolder[--i] = '\0';

    if (isPfsAlias) {
        // pfs:/pfs0: names the already-mounted filesystem ROOT. Preserve that syntax exactly:
        // pfs0:/CFG -> pfs0:CFG, not pfs0:OPL/CFG. A redundant physical partition label was
        // consumed above only when it matched gOPLPart.
        if (subfolder[0] != '\0')
            n = snprintf(canonical, sizeof(canonical), "pfs0:%s", subfolder);
        else
            n = snprintf(canonical, sizeof(canonical), "pfs0:");
    } else if (subfolder[0] != '\0') {
        size_t prefixLen = strlen(gHDDPrefix);
        if (prefixLen > 0 && (gHDDPrefix[prefixLen - 1] == ':' || gHDDPrefix[prefixLen - 1] == '/'))
            n = snprintf(canonical, sizeof(canonical), "%s%s", gHDDPrefix, subfolder);
        else
            n = snprintf(canonical, sizeof(canonical), "%s/%s", gHDDPrefix, subfolder);
    } else {
        n = snprintf(canonical, sizeof(canonical), "%s", gHDDPrefix);
    }

    if (n < 0 || n >= (int)sizeof(canonical) || n >= pathLen) {
        gLastSaveErrno = ENODEV;
        return -1;
    }

    dir = opendir(canonical);
    if (dir == NULL) {
        LOG("CONFIG APA settings directory %s does not exist\n", canonical);
        gLastSaveErrno = ENOENT;
        return -1;
    }
    closedir(dir);

    snprintf(path, pathLen, "%s", canonical);
    LOG("CONFIG APA settings path -> %s (OPL part %s)\n", path, gOPLPart);
    return 1;
}

static int isApaSettingsPath(const char *path)
{
    return path != NULL && (!strncmp(path, "hdd", 3) || !strncmp(path, "pfs", 3) || path[0] == '+');
}

// Resolve the normal safe HDD settings home after an explicit HDD path cannot be used.
// hddLoadSupportModules owns the policy: read the existing __common/OPL/conf_hdd.cfg redirect
// first, otherwise use __common/OPL/, or an existing +OPL when __common is unavailable. It never
// creates or formats an APA partition. This helper only accepts the resulting mounted PFS namespace.
static int prepareHddSettingsFallback(char *path, int pathLen)
{
    DIR *dir;
    int n;

    if (!hddLoadModulesReady())
        return -1;

    hddLoadSupportModules();
    if (gHDDPrefix == NULL || gHDDPrefix[0] == '\0')
        return -1;

    dir = opendir(gHDDPrefix);
    if (dir == NULL)
        return -1;
    closedir(dir);

    n = snprintf(path, pathLen, "%s", gHDDPrefix);
    return (n >= 0 && n < pathLen) ? 1 : -1;
}

// A memory card only creates a file through "mcN:/FILE", never "mcN:FILE" -- and a bare "mc1:" (a
// launcher's CWD, or what anyone types for "save my settings on mc1") or a compact "mc1:APPS/X" is
// exactly that shape. Insert the slash so every memory-card home has the one form file creation takes.
void normalizeMcPath(char *path, size_t size)
{
    size_t len;

    if (strncmp(path, "mc", 2) || (path[2] != '0' && path[2] != '1') || path[3] != ':' || path[4] == '/')
        return;
    len = strlen(path);
    if (len + 1 >= size)
        return;
    memmove(&path[5], &path[4], len - 3);
    path[4] = '/';
}

// The folder of a memory-card Custom Settings Path, made when it is not there yet: settings go to "a
// folder you choose", and the per-file O_CREAT that writes them cannot make one. Save-time only -- the
// boot-time read must never create anything. 0 = ready (or not a card path); -1 = it could not be made,
// with gLastSaveErrno saying why.
static int createMcSettingsFolder(const char *path)
{
    char folder[sizeof(gCustomSettingsPath)];
    size_t len;
    DIR *dir;

    if (strncmp(path, "mc", 2) || (path[2] != '0' && path[2] != '1') || path[3] != ':')
        return 0;
    snprintf(folder, sizeof(folder), "%s", path);
    len = strlen(folder);
    while (len > 5 && folder[len - 1] == '/')
        folder[--len] = '\0';
    if (len <= 5) // "mcN:/": the card root is always there
        return 0;
    dir = opendir(folder);
    if (dir != NULL) {
        closedir(dir);
        return 0;
    }
    // Each missing level in turn, since mkdir makes only the last one: "mc1:/APPS/OPL" on a card
    // without APPS needs both.
    for (size_t i = 6; i <= len; i++) {
        if ((folder[i] != '/' && folder[i] != '\0') || folder[i - 1] == '/')
            continue;
        char end = folder[i];
        folder[i] = '\0';
        dir = opendir(folder);
        if (dir != NULL)
            closedir(dir);
        else {
            errno = 0;
            if (mkdir(folder, 0777) < 0) {
                gLastSaveErrno = errno != 0 ? errno : EIO;
                return -1;
            }
            LOG("CONFIG created settings folder %s\n", folder);
        }
        folder[i] = end;
    }
    return 0;
}

// Make a typed settings path usable: the user may name a device that is not mounted yet. APA/PFS
// paths are handled first and rewritten to OPL's already-mounted pfs0: data partition; BDM paths
// keep the existing transport resolver. HDD targets have a deterministic safe fallback in
// _saveConfig(); non-HDD explicit targets still fail rather than scattering settings elsewhere.
static int prepareCustomSettingsPath(char *path, int pathLen)
{
    int bdmType = BDM_TYPE_UNKNOWN;

    if (path == NULL || path[0] == '\0')
        return 0;

    normalizeMcPath(path, (size_t)pathLen);

    if (isApaSettingsPath(path))
        return prepareCustomApaSettingsPath(path, pathLen);

    if (strncmp(path, "mass", 4) && strncmp(path, "usb", 3) && strncmp(path, "ata", 3) &&
        strncmp(path, "mx4sio", 6) && strncmp(path, "sd", 2) && strncmp(path, "ilink", 5))
        return 0;

    return bdmResolveBootDir(path, pathLen, "", &bdmType);
}


// Last-resort READ-ONLY discovery for a missing/stale config.path. This exists to recover the
// chicken-and-egg custom-settings value from an already-existing config; it never creates a config,
// never changes APA metadata, and never makes an arbitrary discovered device the permanent save home.
// A known non-MC local boot self-migrates the loaded in-memory sets back to its normal boot home. A
// concrete MC boot preserves the same-card directory it recovered, so its first save cannot relocate
// an already-working MC1 configuration to the card root or the other slot. If the recovered config
// contains Custom Settings Path, _saveConfig will honor it and regenerate config.path on the user's
// next explicit Save Changes.
static int tryReadRecoveryConfigHome(int types, const char *home)
{
    DIR *dir = opendir(home);
    if (dir == NULL)
        return 0;
    closedir(dir);

    configEnd();
    configInit((char *)home);
    int value = configReadMulti(types);
    if (value & CONFIG_OPL)
        LOG("CONFIG recovery found existing settings at %s\n", home);
    return value;
}

static int sameConcreteMcSlot(const char *first, const char *second)
{
    return first != NULL && second != NULL &&
           !strncmp(first, "mc", 2) && !strncmp(second, "mc", 2) &&
           (first[2] == '0' || first[2] == '1') && first[3] == ':' &&
           first[2] == second[2] && second[3] == ':';
}

static void restoreRecoverySaveHome(const char *recoveredHome);

static int bootHomeIsConcreteMc(void)
{
    return !strncmp(gBootDir, "mc", 2) &&
           (gBootDir[2] == '0' || gBootDir[2] == '1') && gBootDir[3] == ':';
}

// The launcher handed over the bare card ("mc1:" / "mc1:/"): there is no boot folder to save beside.
static int bootHomeIsMcRoot(void)
{
    return bootHomeIsConcreteMc() && (gBootDir[4] == '\0' || (gBootDir[4] == '/' && gBootDir[5] == '\0'));
}

static int bootHomeIsKnownMmce(void)
{
    return !strncmp(gBootDir, "mmce", 4) &&
           (gBootDir[4] == '0' || gBootDir[4] == '1') && gBootDir[5] == ':';
}

static int bootPathIsConcreteMass(const char *path)
{
    const char *p;

    if (path == NULL || strncmp(path, "mass", 4) != 0)
        return 0;
    p = path + 4;
    if (*p < '0' || *p > '9')
        return 0;
    while (*p >= '0' && *p <= '9')
        p++;
    return *p == ':';
}

static int bootHomeIsKnownBdm(void)
{
    // A resolved BDM driver or a literal massN: token is a concrete local owner. The latter can
    // remain unclassified while its transport starts, but must not be turned into a broad search.
    return !gBootHomeDeferred && gBootHomeBdm;
}

// A known local boot gets at most its own conventional legacy locations. The normal CWD was read
// before this function, so these probes are migration reads only: <device>:/OPL then the device
// root. They cannot wake USB/ATA/MMCE/other MC slots or turn an absent first-run config into a
// whole-machine discovery pass.
static int tryKnownBootConfigRecovery(int types)
{
    const char *colon = strchr(gBootDir, ':');
    char root[16];
    char legacyHome[2][32];
    int legacyFound[2];
    int value;
    int n;

    if (colon == NULL)
        return 0;

    n = (int)(colon - gBootDir) + 1;
    if (n <= 0 || n >= (int)sizeof(root))
        return 0;
    memcpy(root, gBootDir, n);
    root[n] = '\0';

    snprintf(legacyHome[0], sizeof(legacyHome[0]), "%s/OPL", root);
    legacyFound[0] = tryReadRecoveryConfigHome(types, legacyHome[0]);
    if (legacyFound[0] & CONFIG_OPL) {
        restoreRecoverySaveHome(legacyHome[0]);
        return legacyFound[0];
    }

    snprintf(legacyHome[1], sizeof(legacyHome[1]), "%s/", root);
    legacyFound[1] = tryReadRecoveryConfigHome(types, legacyHome[1]);
    if (legacyFound[1] & CONFIG_OPL) {
        restoreRecoverySaveHome(legacyHome[1]);
        return legacyFound[1];
    }

    // Failed recovery probes re-home every config set. Restore the known source before returning
    // defaults so the first explicit save stays on that same device. configEnd/configInit also
    // discarded the sets the first pass had already read from this home (conf_game.cfg's global
    // game settings, network, last played, apps -- e.g. an official OPL install on a hybrid's
    // exFAT volume), so read them again; only the master stays at defaults.
    configEnd();
    configInit(gBootDir);
    value = configReadMulti(types & ~CONFIG_OPL);

    // Each probe also discarded the previous probe's sets, so a legacy home with no master but its
    // own conf_game.cfg/network/apps (mc0:/OPL on an mc0:/APPS boot) was read and then thrown
    // away. Read the sets the boot home lacks from the legacy home that had them -- same order as
    // the probes, files in the boot home always win -- then point every set back at the boot home
    // so the first explicit save writes them beside the ELF: read-old, write-new.
    for (int i = 0; i < 2; i++) {
        int missing = legacyFound[i] & types & ~CONFIG_OPL & ~value;
        if (missing == 0)
            continue;
        configSetMove(legacyHome[i]);
        value |= configReadMulti(missing);
        configSetMove(gBootDir);
    }
    return value;
}

static void restoreRecoverySaveHome(const char *recoveredHome)
{
    // Recovery is discovery only and never writes by itself. Known local boots normally return to
    // their real boot home, but a concrete MC boot keeps the same-card directory that supplied it.
    // A bare launch has no independent owner, so the existing config home that was
    // actually recovered becomes the next explicit-save home. A deferred network boot cannot save
    // back to its unreadable share during bootstrap, so select a concrete reachable MC home when possible.
    if (gBootHomeDeferred) {
        const char *mcHome = NULL;
        DIR *dir;

        if (!strncmp(recoveredHome, "mc0:", 4))
            mcHome = "mc0:/OPL";
        else if (!strncmp(recoveredHome, "mc1:", 4))
            mcHome = "mc1:/OPL";
        else {
            dir = opendir("mc0:/");
            if (dir != NULL) {
                closedir(dir);
                mcHome = "mc0:/OPL";
            } else {
                dir = opendir("mc1:/");
                if (dir != NULL) {
                    closedir(dir);
                    mcHome = "mc1:/OPL";
                }
            }
        }

        if (mcHome != NULL)
            configSetMove((char *)mcHome);
        else
            configSetMove(NULL); // no concrete MC is reachable: keep the normal fail-visible wildcard home
    } else if (sameConcreteMcSlot(gBootDir, recoveredHome) && bootHomeIsMcRoot()) {
        // A launcher that supplies only "mc1:" as the boot CWD leaves no folder to save beside, so a
        // configuration recovered from the same card (mc1:/OPL) keeps that folder rather than moving
        // to the card root (or to mc0 when both cards are inserted). Every other MC boot saves beside
        // its ELF like any other boot -- settings follow the CWD -- through the branch below: an
        // mc1:/APP_RIPTOPL boot that found older settings in mc1:/OPL reads them once and writes its
        // next save beside the ELF, instead of keeping mc1:/OPL as its home for good. This applies to
        // RiptOPL's OWN settings only; official OPL's conf_opl.cfg is never read.
        configSetMove((char *)recoveredHome);
    } else if (gBootDir[0] != '\0') {
        configSetMove(gBootDir);
    } else {
        // A bare launch has no independent boot owner to restore. The existing config home that
        // recovery actually found is therefore the strongest safe ownership signal; keep that
        // concrete MC/USB home so the next explicit save can persist instead of falling into an
        // unreachable mc?:OPL wildcard. Recovery still performs no write by itself.
        configSetMove((char *)recoveredHome);
    }
}

static int tryMissingConfigPathRecovery(int types)
{
    int value;
    int slots[2] = {0, 1};
    int preferred = preferredMcSlot();

    // A concrete mc1 boot searches mc1 first even when mc0 is also inserted. Otherwise retain the
    // historical mc0->mc1 recovery order. Both slots are still probed directly; sysCheckMC() is
    // deliberately not used as a substitute for trying the second card.
    if (preferred == 1) {
        slots[0] = 1;
        slots[1] = 0;
    }
    for (int i = 0; i < 2; i++) {
        char home[16];

        snprintf(home, sizeof(home), "mc%d:/OPL", slots[i]);
        value = tryReadRecoveryConfigHome(types, home);
        if (value & CONFIG_OPL) {
            restoreRecoverySaveHome(home);
            return value;
        }

        snprintf(home, sizeof(home), "mc%d:/", slots[i]);
        value = tryReadRecoveryConfigHome(types, home);
        if (value & CONFIG_OPL) {
            restoreRecoverySaveHome(home);
            return value;
        }
    }

    // USB is third choice. Force only the USB transport, then inspect only slots whose live driver
    // identity is USB; ATA/MX4SIO/iLink are not pulled into this recovery scan. Check both the device
    // root and the conventional /OPL directory. configRead() itself handles the legacy filename.
    //
    // NOTE for anyone profiling a slow boot: bdmEnsureSourceModules blocks for up to 1500 ms here,
    // and this whole recovery runs whenever the boot dir yielded no readable config. On a host:
    // (emulator) boot that had been every boot, because host: is unclassifiable -- see
    // oplBootDirIsHostFs. The save path no longer reaches this sweep for host:; the read path still
    // does by design, since a host: boot may legitimately keep its settings on a card.
    if (oplBootDirIsHostFs())
        LOG("CONFIG recovery: host: boot, about to spend up to 1500ms probing for USB modules\n");
    if (bdmEnsureSourceModules(BDM_TYPE_USB, 1500)) {
        int slots[MAX_BDM_DEVICES];
        int count = bdmGetDeviceSlotsByType(BDM_TYPE_USB, slots, MAX_BDM_DEVICES);
        for (int i = 0; i < count; i++) {
            char home[32];

            snprintf(home, sizeof(home), "mass%d:/", slots[i]);
            value = tryReadRecoveryConfigHome(types, home);
            if (value & CONFIG_OPL) {
                restoreRecoverySaveHome(home);
                return value;
            }

            snprintf(home, sizeof(home), "mass%d:/OPL", slots[i]);
            value = tryReadRecoveryConfigHome(types, home);
            if (value & CONFIG_OPL) {
                restoreRecoverySaveHome(home);
                return value;
            }
        }
    }

    // MMCE is fourth choice (after MC and USB).
    value = checkLoadConfigMMCE(types);
    if (value & CONFIG_OPL)
        return value;

    // Every failed probe re-homed the config_set filenames. For APA, preserve an already-mounted
    // safe PFS home instead of falling back to the raw hddN: launch namespace. Raw APA remains only
    // a fail-closed launch identity when no writable PFS owner exists at all.
    configEnd();
    if (gBootHomeApa && gHDDPrefix != NULL && gHDDPrefix[0] != '\0')
        configInit(gHDDPrefix);
    else
        configInit(gBootDir[0] != '\0' ? gBootDir : NULL);
    return 0;
}


static int resolveApaBootHome(void);

// The menu (_loadConfig) and autolaunch (miniInit) both call this with the same inputs, so both land on
// the same settings home. Keep it that way: an autolaunch-only branch here is how #545 happened.
int tryAlternateDevice(int types)
{
    char redirectPath[64];
    int value;
    DIR *dir;

    // APA has one deterministic ownership chain. If the first boot-time mount missed while the
    // drive was settling, retry the EXISTING-PFS resolver before reading config.path. This adds no
    // new device class or fallback; checkLoadConfigHDD already performed the same synchronous HDD
    // work below, only too late to expose a PFS-hosted redirect.
    //
    // A failed ATA load is retryable, so the drive may first come up HERE, where the boot-time hybrid
    // check never ran. Resolve through resolveApaBootHome, not a bare mount, or an APA+exFAT hybrid
    // with a late drive lands on __common/OPL and misses its exFAT games (#545, CodeRabbit).
    // Homed on exFAT, it is an ordinary ATA-BDM boot: read that home, then the discovery below runs
    // exactly as it does for an on-time hybrid whose first read missed.
    if (gBootHomeApa && (gHDDPrefix == NULL || gHDDPrefix[0] == '\0')) {
        if (hddLoadModulesReady() && resolveApaBootHome() && gBootHomeHybridExfat) {
            value = configReadMulti(types);
            if (value & CONFIG_OPL)
                return value;
        }
    }

    // The user's Custom Settings Path, if one was set, takes precedence over every discovery probe
    // below -- it is an explicit instruction, not a guess.
    if (readConfigPathRedirect(redirectPath, sizeof(redirectPath))) {
        if (prepareCustomSettingsPath(redirectPath, sizeof(redirectPath)) >= 0) {
            LOG("CONFIG custom settings path -> %s\n", redirectPath);
            configEnd();
            configInit(redirectPath);
            value = configReadMulti(types);
            if (value & CONFIG_OPL)
                return value;

            // A stale pointer must not leave local config_set filenames homed on the missing target.
            // Network boot keeps its existing deferred-home handling below.
            if (!gBootHomeDeferred) {
                configEnd();
                configInit(gBootDir[0] != '\0' ? gBootDir : NULL);
                LOG("CONFIG stale settings redirect failed; restored normal home %s\n", gBootDir);
            }
        } else {
            // Named a BDM device that never mounted.
            LOG("CONFIG custom settings path %s did not mount\n", redirectPath);
        }
    }

    // An APA launch that reaches here is authoritative for APA: an APA+exFAT hybrid was already homed on
    // its exFAT volume by resolveBootDirToMass (#545). After an explicit redirect misses, ONLY the
    // deterministic existing-PFS ownership chain is eligible: __common/OPL/conf_hdd.cfg's valid
    // existing target, otherwise __common/OPL. Never import an unrelated MC/USB master config into
    // an FHDB/APA session; that can resurrect stale Custom Settings Path state and makes the next
    // save destination depend on whichever removable device happened to be inserted.
    if (gBootHomeApa) {
        value = checkLoadConfigHDD(types);
        if (value & CONFIG_OPL)
            return value;

        // No master config yet is a valid first-run state. Keep defaults homed to the mounted safe
        // PFS target so the first explicit Save materializes them there. If PFS is still unavailable,
        // leave the raw APA launch identity only as a fail-closed marker; _saveConfig retries this
        // same safe chain and config.c blocks every raw hddN: config write as defense in depth.
        if (gHDDPrefix != NULL && gHDDPrefix[0] != '\0')
            configSetMove(gHDDPrefix);
        showCfgPopup = 0;
        return 0;
    }

    // A missing file on a known local boot is an ordinary first-run state, not a signal to scan all
    // supported storage. Limit migration reads to the boot device's own conventional legacy homes:
    // same MC slot, the resolved BDM device, or the corresponding MMCE card. This is deliberately
    // before the broad legacy hunt below, whose USB module wait and MMCE probing are unacceptable
    // on a known device that simply has no config yet.
    if (bootHomeIsConcreteMc() || bootHomeIsKnownBdm() || bootHomeIsKnownMmce()) {
        value = tryKnownBootConfigRecovery(types);
        if (value & CONFIG_OPL)
            return value;

        showCfgPopup = 0;
        return value; // the home's other sets, re-read after the recovery miss
    }

    // Bare, deferred-network, and otherwise unclassified boots retain the bounded read-only
    // recovery chain. These have no verified local settings owner to prefer.
    value = tryMissingConfigPathRecovery(types);
    if (value & CONFIG_OPL)
        return value;

    // If OPL was booted from a valid CWD/boot directory (gBootDir is set, e.g. "mc0:/OPL" or "mc0:"),
    // settings are strictly homed to gBootDir. Never probe alternate devices (mass0:/hdd0:) or hijack
    // the save location away from CWD/Memory Card.
    if (gBootDir[0] != '\0') {
        // LEGACY READ-ONLY FALLBACK. Every build before the CWD doctrine -- ours and official alike
        // -- homed settings at mc?:OPL, so every existing install's config lives THERE, not beside
        // the ELF. Without this, upgrading silently reset every user to defaults (reported from
        // hardware as "it didn't read my settings" and "everything is slow like it used to be" --
        // stock art delay and scroll speed are literally the old feel). The user's setup did not
        // change; we moved where we look, so we keep looking in the old place too.
        //
        // READ ONLY, and only the mc?:OPL home -- never other devices (that hijack is what the
        // strict homing exists to prevent). The home is moved straight back to the boot dir, so
        // the very first save writes the config BESIDE THE ELF and this fallback goes quiet for
        // good: read-old, write-new, self-migrating. The load toast names the boot device rather
        // than the card on the one boot that reads legacy -- the save-location toast telling the
        // truth matters more, and after the first save the two agree anyway.
        int homeLeftOnCard = 0;
        if (sysCheckMC() >= 0) {
            char concreteMcHome[16];
            if (!strncmp(gBootDir, "mc0:", 4) || !strncmp(gBootDir, "mc1:", 4)) {
                snprintf(concreteMcHome, sizeof(concreteMcHome), "mc%c:/OPL", gBootDir[2]);
                configSetMove(concreteMcHome); // MC boot: legacy fallback stays on the booted slot
            } else {
                configSetMove(NULL); // non-MC boot: retain the historical mc?:OPL selection
            }
            value = configReadMulti(types);

            // SELF-MIGRATION IS WRONG WHEN THE BOOT DEVICE CANNOT BE READ AT BOOT. The move-back
            // below is what makes this fallback self-retiring on a BDM boot: read the old card copy
            // once, write the next save beside the ELF, never look at the card again. On a network
            // boot it does the opposite of what it promises -- the read above SUCCEEDS (the card is
            // readable with no network at all), and then the home is moved onto a share that cannot
            // be mounted until the config has already been read. The next save goes there, the next
            // boot cannot see it, and the card copy it falls back to is now stale. That is precisely
            // the "settings save but never load back" report from network booters.
            //
            // So for a deferred-home boot the card KEEPS the home: it is the only place that is both
            // writable and readable at boot time, and configSetMove(NULL) has already pointed the
            // notifications there too. Nothing new is written and no card is touched that was not
            // already serving this config -- sysCheckMC() gated the whole branch.
            if (gBootHomeDeferred)
                homeLeftOnCard = 1;
            else if (sameConcreteMcSlot(gBootDir, concreteMcHome)) {
                configSetMove(concreteMcHome); // MC legacy config remains at the exact discovered home
                // configSetMove already made the exact MC1/MC0 home the notification/save owner.
                // Do not overwrite that with a compact/root boot CWD below: failure reporting must
                // name the same directory that configWriteMulti will actually use.
                homeLeftOnCard = 1;
            } else
                configSetMove(gBootDir); // home (and notifications) back on the boot dir: saves self-migrate

            if (value & CONFIG_OPL)
                return value;
        }
        // Only re-point at the boot dir if the home is actually still there. When the card kept it
        // above, saying "boot dir" would make the save-location toast name a device that is not the
        // save location -- and a truthful toast is the entire point of this call.
        if (!homeLeftOnCard)
            configPrepareNotifications(gBootDir);
        showCfgPopup = 0;
        // The card read above can still fill other sets with no master beside them (a deferred
        // network boot's conf_network.cfg, say). They are loaded either way; returning 0 hid them,
        // so the caller never applied the network settings and the next save overwrote them.
        return value;
    }

    // Bare ELF launch without boot directory context: try Memory Card first.
    if (sysCheckMC() >= 0) {
        configPrepareNotifications(gBaseMCDir);
        showCfgPopup = 0;
        return 0;
    }

    // No memory card? Check mass0: or HDD as last resort for bare launches...
    dir = opendir("mass0:");
    if (dir != NULL) {
        closedir(dir);
        configEnd();
        configInit("mass0:");
    } else if (hddLoadModulesReady()) {
        hddLoadSupportModules();
        if (gHDDPrefix != NULL) {
            dir = opendir(gHDDPrefix);
            if (dir != NULL) {
                closedir(dir);
                configEnd();
                configInit(gHDDPrefix);
            }
        }
    }
    showCfgPopup = 0;

    return 0;
}

// The visible path an old "HDD (APA)" Neutrino picker choice migrates to. Defined here rather than
// beside NEUTRINO_MIGRATED_MC_PATH in opl.h: only the APA-owning sources may spell the raw hdd
// namespace. The resolver keys the case-variant probe off the gNeutrinoDevice marker, not this text.
#define NEUTRINO_MIGRATED_APA_PATH "hdd:/neutrino/neutrino.elf"

// Shared reader for the Neutrino-launch globals (args / custom path / -elf switch / global default
// core / device TYPE incl. the legacy device-INDEX migration). Factored out so the interactive
// _loadConfig and the autolaunch miniInit can never drift: the argv/autolaunch path previously read
// NONE of these, so a keyless "Default" game autolaunched with a stale AUTO device and empty global
// args, silently diverging from an interactive launch of the same game.
void configReadNeutrinoGlobals(config_set_t *configOPL)
{
    configGetStrCopy(configOPL, CONFIG_OPL_NEUTRINO_ARGS, gNeutrinoArgs, sizeof(gNeutrinoArgs));
    configGetStrCopy(configOPL, CONFIG_OPL_NEUTRINO_PATH, gNeutrinoPath, sizeof(gNeutrinoPath));
    configGetInt(configOPL, CONFIG_OPL_NEUTRINO_ELF_ARG, &gNeutrinoElfArg);
    configGetInt(configOPL, CONFIG_OPL_NEUTRINO_SHOW_ARGS, &gNeutrinoShowArgs);
    // Global default Loader Core (0=<OPL>, 1=Neutrino). Absent in legacy configs -> keep the reset
    // default (0/<OPL>), so existing installs behave exactly as before this key existed.
    configGetInt(configOPL, CONFIG_OPL_DEFAULT_CORE, &gDefaultCoreLoader);
    configGetInt(configOPL, CONFIG_OPL_NEUTRINO_VIDEO, &gNeutrinoVideoDefault);
    if (gNeutrinoVideoDefault < 0 || gNeutrinoVideoDefault > 5)
        gNeutrinoVideoDefault = 0; // sanitize (indexes system.c gsmVideoTokens at the launch legs)
    configGetInt(configOPL, CONFIG_OPL_NEUTRINO_GSMCOMP, &gNeutrinoGsmCompDefault);
    if (gNeutrinoGsmCompDefault < 0 || gNeutrinoGsmCompDefault > 3)
        gNeutrinoGsmCompDefault = 0;
    // Neutrino Device: prefer the new device-TYPE key; if absent (config predates the picker
    // change), migrate the legacy device-INDEX value.
    if (!configGetInt(configOPL, CONFIG_OPL_NEUTRINO_DEVTYPE, &gNeutrinoDevice)) {
        int legacyDev = 0;
        if (configGetInt(configOPL, CONFIG_OPL_NEUTRINO_DEVICE, &legacyDev)) {
            if (legacyDev == 1 || legacyDev == 2)
                gNeutrinoDevice = NEUTRINO_DEV_MC;
            else
                gNeutrinoDevice = NEUTRINO_DEV_AUTO;
        }
    }
    // Retired picker values (see the enum in opl.h). The device-specific choices -- USB, MX4SIO, MMCE,
    // exFAT, iLink and the strict Game's Device -- were retired to Auto because their launches were not
    // reliable, and they keep loading as Auto. Turning them into active paths here would re-enable
    // exactly those launches and force-load their transports at launch time for users who never
    // chose them again. Memory Card and HDD (APA) are the two choices the runtime still honours: they
    // become the visible full path, and the old value stays behind as a marker so the resolver can
    // repeat that picker's case-tolerant probe. _saveConfig writes both keys, so the marker has to
    // survive a save and reload as well; it is dropped only once the path differs from its migrated
    // form (the Settings page also resets it on any edit).
    if (gNeutrinoDevice < NEUTRINO_DEV_AUTO || gNeutrinoDevice > NEUTRINO_DEV_ILINK)
        gNeutrinoDevice = NEUTRINO_DEV_AUTO;
    if (gNeutrinoPath[0] == '\0') {
        if (gNeutrinoDevice == NEUTRINO_DEV_MC)
            snprintf(gNeutrinoPath, sizeof(gNeutrinoPath), "%s", NEUTRINO_MIGRATED_MC_PATH);
        else if (gNeutrinoDevice == NEUTRINO_DEV_APA_HDD)
            snprintf(gNeutrinoPath, sizeof(gNeutrinoPath), "%s", NEUTRINO_MIGRATED_APA_PATH);
        else
            gNeutrinoDevice = NEUTRINO_DEV_AUTO;
    } else if (!(gNeutrinoDevice == NEUTRINO_DEV_MC && !strcmp(gNeutrinoPath, NEUTRINO_MIGRATED_MC_PATH)) &&
               !(gNeutrinoDevice == NEUTRINO_DEV_APA_HDD && !strcmp(gNeutrinoPath, NEUTRINO_MIGRATED_APA_PATH))) {
        gNeutrinoDevice = NEUTRINO_DEV_AUTO; // an explicit full path is exact: no marker
    }
}

static int bootHomeHasFile(const char *home, const char *name)
{
    size_t len = strlen(home);
    const char *sep = (len > 0 && (home[len - 1] == ':' || home[len - 1] == '/')) ? "" : "/";
    char path[256];
    int fd;

    snprintf(path, sizeof(path), "%s%s%s", home, sep, name);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

// RiptOPL's own master settings file (current or legacy name) in a candidate home.
static int bootHomeHasRiptoplMaster(const char *home)
{
    return bootHomeHasFile(home, CONFIG_OPL_FILENAME) || bootHomeHasFile(home, CONFIG_OPL_FILENAME_LEGACY);
}

// Does a candidate settings home already belong to RiptOPL? Only our own master file or a Custom
// Settings Path redirect counts. Official OPL's conf_opl.cfg is a read-only seed, not ownership, so it
// never outranks settings the user has already saved with RiptOPL somewhere else.
static int bootHomeHasRiptoplSettings(const char *home)
{
    return bootHomeHasRiptoplMaster(home) || bootHomeHasFile(home, configPathRedirectFile);
}

// Why resolveBootDirToMass picked the home it did. Diagnostic builds show it (#545): testers can
// photograph one line instead of reverse-engineering which home a boot chose.
const char *gBootHomeWhy = "not an APA launch";

// The exFAT volume of an APA+exFAT hybrid as "massN:/". The typed resolver loads only the BDM core and the
// ATA transport and waits no longer than the bootstrap budget. No ELF name on purpose: the launcher ELF
// lives on APA, and asking the resolver to prove it on exFAT would burn the whole budget before the folder
// check matched.
static int resolveHybridExfatHome(char *home, int homeLen)
{
    int bdmType = BDM_TYPE_UNKNOWN;

    snprintf(home, homeLen, "ata0:/");
    return bdmResolveBootDirBootstrap(home, homeLen, "", &bdmType) > 0 && bdmType == BDM_TYPE_ATA;
}

// Make the hybrid's exFAT volume the settings home: from here on the boot is an ordinary ATA-BDM boot,
// so the boot-device reconcile enables the ATA transport and saves take the BDM path.
static void adoptHybridExfatHome(const char *launch, const char *home, const char *why)
{
    snprintf(gBootDir, sizeof(gBootDir), "%s", home);
    gBootHomeApa = 0;
    gBootHddCommonFallback = 0;
    gBootHomeBdm = 1;
    gBootDirBdmType = BDM_TYPE_ATA;
    gBootHomeHybridExfat = 1;
    gBootHomeWhy = why;
    LOG("BOOT APA launch %s on an APA+exFAT hybrid -> settings home %s (%s)\n", launch, gBootDir, why);
    configEnd();
    configInit(gBootDir);
}

// The settings home of an APA launch, once the ATA stack is up: a hybrid's exFAT volume or the APA data home.
// 1 = homed; 0 = neither is usable (the caller keeps the launch identity). resolveBootDirToMass runs this at boot,
// and tryAlternateDevice runs it again when the drive only came up on its retry -- one decision, so a late drive
// gets the same home an on-time one does (#545).
static int resolveApaBootHome(void)
{
    char before[sizeof(gBootDir)];
    char exfatHome[BDM_DEVICE_ROOT_MAX];
    int hybrid = hddIsApaMbrHybrid();
    int haveExfatHome = 0;

    snprintf(before, sizeof(before), "%s", gBootDir);

    // APA+exFAT hybrid (PSBBN Definitive's "APA-Jail"): APA holds only the launchers, while the
    // exFAT volume holds official OPL's conf_opl.cfg, the games, CFG and ART -- official OPL
    // never loads APA on such a disk at all, so its settings always live on exFAT. RiptOPL does
    // the same. The one exception is a Custom Settings Path saved in the APA data home: an
    // explicit instruction still decides. RiptOPL settings that older builds saved to the APA
    // data home are CARRIED OVER, read-only, until the first save writes them to exFAT -- keeping
    // them as the home instead made every tester of an older build save to __common/OPL and
    // miss the exFAT games (#545 retest). The menu and autolaunch both come through here.
    if (hybrid && resolveHybridExfatHome(exfatHome, sizeof(exfatHome))) {
        haveExfatHome = 1;
        if (bootHomeHasRiptoplSettings(exfatHome)) {
            adoptHybridExfatHome(before, exfatHome, "hybrid: RiptOPL settings on exFAT");
            return 1; // never mounts PFS
        }
    }

    // With the exFAT home in hand the APA home is only LOOKED at -- for settings to carry over or a
    // Custom Settings Path -- so mount it without creating __common/OPL/, and let it go again unless
    // settings are carried over from it. Otherwise every first run of an APA-Jail disk created that
    // folder and toasted "__common partition mounted" for a partition OPL was not using (CosmicScale).
    int apaHomeWasMounted = gHDDPrefix != NULL;
    if (haveExfatHome)
        hddInspectSupportHome();
    else
        hddLoadSupportModules();
    if (gHDDPrefix != NULL && gHDDPrefix[0] != '\0') {
        DIR *dir = opendir(gHDDPrefix);
        if (dir != NULL) {
            closedir(dir);
            if (haveExfatHome && !bootHomeHasFile(gHDDPrefix, configPathRedirectFile)) {
                if (bootHomeHasRiptoplMaster(gHDDPrefix)) {
                    configSetCarryOverDir(gHDDPrefix);
                    adoptHybridExfatHome(before, exfatHome, "hybrid: exFAT, APA settings carried over");
                } else {
                    if (!apaHomeWasMounted)
                        hddReleaseSupportHome();
                    adoptHybridExfatHome(before, exfatHome, "hybrid: exFAT (first run)");
                }
                return 1;
            }

            gBootHomeWhy = haveExfatHome ? "hybrid: APA (Custom Settings Path in APA home)" :
                           hybrid        ? "APA: hybrid, but exFAT did not mount in time" :
                                           "APA data home";
            snprintf(gBootDir, sizeof(gBootDir), "%s", gHDDPrefix);
            size_t rlen = strlen(gBootDir);
            while (rlen > 0 && gBootDir[rlen - 1] == '/')
                gBootDir[--rlen] = '\0';

            gBootHomeApa = 1;
            if (!strcmp(gOPLPart, "hdd0:__common"))
                gBootHddCommonFallback = 1;
            if (gHDDStartMode == START_MODE_DISABLED)
                gHDDStartMode = START_MODE_AUTO;

            // Preserve rebuild-206's recursion guard: re-home only when resolution changed it.
            if (strcmp(before, gBootDir) != 0) {
                LOG("BOOT resolved APA launch %s -> data home %s (OPL part %s)\n", before, gBootDir, gOPLPart);
                configEnd();
                configInit(gBootDir);
            }
            return 1;
        }
    }

    // No usable APA data home. On a hybrid the exFAT volume is still a real, writable home.
    if (haveExfatHome) {
        if (!apaHomeWasMounted)
            hddReleaseSupportHome();
        adoptHybridExfatHome(before, exfatHome, "hybrid: exFAT (APA data home unusable)");
        return 1;
    }
    return 0;
}

void resolveBootDirToMass(void)
{
    gBootHomeApa = 0;
    gBootHddCommonFallback = 0;
    gBootHomeBdm = 0;
    gBootDirBdmType = BDM_TYPE_UNKNOWN;
    gBootHomeDeferred = 0;
    gBootHomeHybridExfat = 0;
    gBootHomeWhy = "not an APA launch";
    configSetCarryOverDir(NULL);
    if (gBootDir[0] == '\0')
        return;

    // APA-HDD boot: the ELF launch partition and OPL's persistent data partition are separate
    // concepts. FHDB/uLE/HDD-OSD may launch from __sysconf/__common/__contents, but hddsupport
    // alone owns gOPLPart and mounts that data partition on pfs0:. Config follows gHDDPrefix.
    if (!strncmp(gBootDir, "hdd", 3) || !strncmp(gBootDir, "pfs", 3)) {
        if (hddLoadModulesReady() && resolveApaBootHome())
            return;
        gBootHomeWhy = "APA: no data home mounted";
        LOG("BOOT APA boot dir %s could not resolve an HDD data home; keeping launch identity for safe retry\n", gBootDir);
        // The launch transport is still APA even though the persistent data home did not mount.
        // Keep the real launch identity rather than inventing an unmounted pfs0:OPL home. The raw
        // hddN: config firewall makes the first read fail closed, and tryAlternateDevice retries
        // only the existing-partition HDD ownership chain.
        gBootHomeApa = 1;
        gHDDStartMode = START_MODE_AUTO;
        gBootHddCommonFallback = 0;
        return;
    }

    // CD-ROM / ISO boot (PS3 PS2-Classic ISO or PS2 disc boot): cdrom0: is read-only hardware.
    // Re-home the config to the Memory Card (mc?:OPL) so saves land on Memory Card Slot 1/2 (#353).
    if (!strncmp(gBootDir, "cdrom", 5)) {
        LOG("BOOT read-only cdrom boot dir %s -> redirecting config home to MC\n", gBootDir);
        gBootDir[0] = '\0';
        configEnd();
        configInit(NULL);
        return;
    }

    // MMCE boot: the mmceman driver is likewise not loaded at boot time (sysReset loads none of the
    // device stacks), so mmceN: is unreadable exactly when settings must load. Load it (idempotent)
    // and give the card a moment to register its filesystem. mmceN: IS the readable namespace, so no
    // prefix rewrite is needed.
    // NETWORK BOOT. smb0: (ethsupport's ethBase) and udpfs: (udpfssupport's udpfsBase) are the two
    // network filesystems OPL can be launched from, and neither can be mounted before the config is
    // read: the transport needs the static IP -- and SMB the server, share, user and password --
    // which is exactly what we are trying to read. Nothing to resolve, so leave the boot identity
    // completely alone and just record WHY it is unreadable.
    //
    // Shaped like the mmce branch below (plain return, identity preserved), NOT like the hdd/cdrom
    // branches above that blank gBootDir and re-home to configInit(NULL): blanking here would drop
    // this boot into legacy discovery and hand the config home to a plain memory card the user never
    // named, which is the FifthFox hazard those branches exist to avoid everywhere else.
    //
    // The flag's whole job is to let tryAlternateDevice tell "the device is down" apart from "there
    // is no config", so it can stop self-migrating the home onto a device that can never be read at
    // boot. Without it, that fallback reads the user's settings off the memory card correctly and
    // then moves the home back onto the share -- so the next save lands somewhere the next boot
    // cannot see, which is exactly the "my settings save but never load" report.
    //
    // NOT covered, and it cannot be: a UDPBD / UDPFS-BD boot arrives as "massN:/OPL", byte-identical
    // to a USB one. Its only distinguishing evidence is the BDM driver token "udp", which needs the
    // udpbd stack, which needs the static IP from the unreadable config. Those boots fall through to
    // the ordinary massN: resolve, fail to mount, and keep gBootDir -- today's behaviour, unchanged.
    if (!strncmp(gBootDir, "smb", 3) || !strncmp(gBootDir, "udpfs", 5)) {
        LOG("BOOT network boot dir %s -> config home deferred (mc untouched)\n", gBootDir);
        gBootHomeDeferred = 1;
        return;
    }

    if (!strncmp(gBootDir, "mmce", 4)) {
        // NOTE(rebuild): mmceLoadModules() is still the no-op stub here (MMCE is checklist items 1-3,
        // on WAIT), so the driver never loads and the opendir poll below always times out. That lands
        // on the "keep as config home" fallback -- which is the CORRECT feature-off behaviour, and the
        // one that matters: it is precisely the branch that keeps mc untouched. An MMCE boot therefore
        // reads defaults for now and its first successful save lands on mmce once item 1 makes the
        // driver real. Cost is the ~10 x delay(1) poll on an MMCE boot only.
        mmceLoadModules();
        char devRoot[8];
        const char *colon = strchr(gBootDir, ':');
        size_t rootLen = colon ? (size_t)(colon - gBootDir) + 1 : 0;
        if (rootLen > 0 && rootLen < sizeof(devRoot)) {
            memcpy(devRoot, gBootDir, rootLen);
            devRoot[rootLen] = '\0';
            for (int tries = 0; tries < 10; tries++) { // ~2 s total: the driver detects a present card in ms
                DIR *dir = opendir(devRoot);
                if (dir != NULL) {
                    closedir(dir);
                    return; // mounted -- the boot dir is readable as-is
                }
                delay(1);
            }
        }
        // Card still settling after the wait. It served this very ELF milliseconds ago, so it IS present
        // -- it just has not re-registered its mmceman filesystem yet. Do NOT blank gBootDir and re-home
        // the config to a plain memory card here (the old configInit(NULL) fallback): with an empty boot
        // dir, configGetDir() returns the legacy mc?: default, so _saveConfig's checkMCFolder() + the
        // per-file O_CREAT stamped an unwanted mc?:OPL folder and settings onto a SEPARATE plain mc card
        // (FifthFox, HW 2026-07-16). mc is never an MMCE user's config home. Keep mmce as the home: this
        // boot falls back to defaults if the card is still settling, and the first save lands on mmce
        // once it has mounted (a truly dead card fails the save visibly -- the same contract the BDM boot
        // device honours below). mc stays untouched.
        LOG("BOOT MMCE boot device %s not mounted after wait -> keep as config home (mc untouched)\n", gBootDir);
        return;
    }

    char before[sizeof(gBootDir)];
    snprintf(before, sizeof(before), "%s", gBootDir);
    int ret = bdmResolveBootDirBootstrap(gBootDir, sizeof(gBootDir), gBootElfName, &gBootDirBdmType);
    gBootHomeBdm = ret != 0 && (gBootDirBdmType != BDM_TYPE_UNKNOWN || bootPathIsConcreteMass(before));
    if (ret < 0) {
        // The boot device's BDM stack did not mount within the resolve budget. It served this ELF, so it
        // IS present -- do NOT blank gBootDir and re-home the config to a plain mc here (the old
        // configInit(NULL)). An empty boot dir makes configGetDir() fall to the legacy mc?: default, and
        // _saveConfig's checkMCFolder() + the per-file O_CREAT then stamp an mc?:OPL folder + settings
        // onto a plain memory card (FifthFox, HW 2026-07-16 -- extended from the MMCE case above at
        // NathanNeurotic's request). mc is never the boot device's config home. Keep the boot identity as
        // the home (the config sets were already homed there by init()'s configInit): this boot reads
        // defaults if the stack is still coming up, and a save targets the boot device -- failing visibly
        // if it is genuinely gone -- never a plain mc. bdmResolveBootDir leaves gBootDir UNCHANGED on a
        // failed resolve (it only rewrites on success), so the identity here is intact.
        LOG("BOOT boot device %s not mounted during bounded bootstrap resolve -> keep as config home (mc untouched)\n", before);
        return;
    }
    if (ret > 0 && strcmp(before, gBootDir) != 0) {
        LOG("BOOT resolved boot dir %s -> %s\n", before, gBootDir);
        configEnd();
        configInit(gBootDir); // re-point the config sets from the unreadable prefix to the massN: path
    }
}

// One Ember setting's stored value, or EMBER_SETTING_UNSET when the key is absent or out of range. An
// out-of-range value (hand-edited, or from a later build) must not be written into anyone's file.
static int emberSettingGet(config_set_t *config, const char *key, int count)
{
    int value;

    if (!configGetInt(config, key, &value) || value < 0 || value >= count)
        return EMBER_SETTING_UNSET;
    return value;
}

// Store one Ember setting only once the user has changed it here; unset means no key at all.
static void emberSettingSet(config_set_t *config, const char *key, int value)
{
    if (value == EMBER_SETTING_UNSET)
        configRemoveKey(config, key);
    else
        configSetInt(config, key, value);
}

static void emberLoadSettings(config_set_t *configOPL)
{
    int present;

    gEmberDisplay = emberSettingGet(configOPL, CONFIG_OPL_EMBER_DISPLAY_MODE, EMBER_DISPLAY_COUNT);
    // One-time migration from the old key, only while the new one is absent. The old key was written
    // on EVERY save whether or not anyone touched the row, so its 0 cannot be told apart from "never
    // set" and reads as unset. 1 (240) and 2 (480 -- which was always 480i to Ember) were chosen by the
    // user and carry over unchanged, so nobody's picture changes. emberSaveSettings drops the old key.
    if (!configGetInt(configOPL, CONFIG_OPL_EMBER_DISPLAY_MODE, &present)) {
        int legacy = emberSettingGet(configOPL, CONFIG_OPL_EMBER_DISPLAY, EMBER_DISPLAY_480 + 1);
        gEmberDisplay = (legacy > EMBER_DISPLAY_DEFAULT) ? legacy : EMBER_SETTING_UNSET;
    }
    gEmberTiming = emberSettingGet(configOPL, CONFIG_OPL_EMBER_TIMING, EMBER_TIMING_COUNT);
    gEmberDither = emberSettingGet(configOPL, CONFIG_OPL_EMBER_DITHER, EMBER_DITHER_COUNT);
    gEmberShading = emberSettingGet(configOPL, CONFIG_OPL_EMBER_SHADING, EMBER_SHADING_COUNT);
    gEmberController = emberSettingGet(configOPL, CONFIG_OPL_EMBER_CONTROLLER, EMBER_CONTROLLER_COUNT);
}

static void emberSaveSettings(config_set_t *configOPL)
{
    emberSettingSet(configOPL, CONFIG_OPL_EMBER_DISPLAY_MODE, gEmberDisplay);
    emberSettingSet(configOPL, CONFIG_OPL_EMBER_TIMING, gEmberTiming);
    emberSettingSet(configOPL, CONFIG_OPL_EMBER_DITHER, gEmberDither);
    emberSettingSet(configOPL, CONFIG_OPL_EMBER_SHADING, gEmberShading);
    emberSettingSet(configOPL, CONFIG_OPL_EMBER_CONTROLLER, gEmberController);
    configRemoveKey(configOPL, CONFIG_OPL_EMBER_DISPLAY); // migrated on load; never written again
}

// The RA settings, read and normalised the same way for the menu and for Auto Loading (miniInit).
void configReadRA(config_set_t *configOPL)
{
#ifdef RETROACHIEVEMENTS
    const char *temp;
    configGetInt(configOPL, CONFIG_OPL_RA_TELEMETRY, &gRATelemetry);
    configGetInt(configOPL, CONFIG_OPL_RA_BADGES, &gRABadges);
    configGetInt(configOPL, CONFIG_OPL_RA_BADGE_SUFFIX, &gRABadgeSuffix);
    configGetInt(configOPL, CONFIG_OPL_RA_MODE, &gRAMode);
    configGetInt(configOPL, CONFIG_OPL_RA_EXPERIMENTAL_CARDS, &gRAExperimentalCards);
    gRABadgeSuffix = (gRABadgeSuffix == 1); // any stored value but 1 is Prefix
    if (gRAMode != RA_MODE_CADUCEUS)
        gRAMode = RA_MODE_XERABORA;
    gRAExperimentalCards = (gRAExperimentalCards == 1); // anything but an explicit 1 is Off
    int a, b, c, d;
    char extra;
    if (configGetStr(configOPL, CONFIG_OPL_RA_HOST_IP, &temp) &&
        sscanf(temp, "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) == 4 &&
        a >= 0 && a <= 255 && b >= 0 && b <= 255 && c >= 0 && c <= 255 && d >= 0 && d <= 255) {
        gRAHostIp[0] = a;
        gRAHostIp[1] = b;
        gRAHostIp[2] = c;
        gRAHostIp[3] = d;
    }
#else
    (void)configOPL;
#endif
}

void _loadConfig()
{
    int value, themeID = -1, langID = -1;
    const char *temp;
    resolveBootDirToMass(); // usb0:/ata0:/mx4sio0:/APPS -> mass0:/APPS (boot-device massN:) before the first read
    // Discover the resolved CWD (and CWD/LNG) before restoring the saved language.
    lngRefreshBootLanguages(-1);
    int result = configReadMulti(lscstatus);

    if (lscstatus & CONFIG_OPL) {
        if (!(result & CONFIG_OPL)) {
            result = tryAlternateDevice(lscstatus);
        }

        if (result & CONFIG_OPL) {
            config_set_t *configOPL = configGetByType(CONFIG_OPL);

            settingsLoad(configOPL); // every plain key; the special cases below (recovery combo, clamps, migrations) follow
            gFontSize = fntValidFontSize(gFontSize);
            if (gAutosort < AUTOSORT_OFF || gAutosort >= AUTOSORT_COUNT)
                gAutosort = AUTOSORT_AZ; // older builds stored any non-zero as "on"
            gAllGames = gAllGames != 0;
            gShowHidden = gShowHidden != 0;
            configReadRA(configOPL);


            if (getKeyPressed(KEY_TRIANGLE) && getKeyPressed(KEY_CROSS)) {
                // Recovery combo: force 480p PROGRESSIVE (EDTV 640x448p@60, vmode index 3), not
                // Auto -- Auto resolves to region-default interlaced 480i/576i, which is exactly
                // what some modern displays/upscalers fail to sync, leaving the user still blind.
                LOG("--- Triangle + Cross held at boot - forcing Video Mode to 480p (recovery) ---\n");
                gVMode = 3;
                configSetInt(configOPL, CONFIG_OPL_VMODE, gVMode);
            } else if (getKeyPressed(KEY_TRIANGLE) && getKeyPressed(KEY_CIRCLE)) {
                // The other half of recovery. 480p is exactly what an interlaced-only set (a CRT;
                // zackcage6, 09-25) can NOT show, so the combo above left those users as blind as
                // before. Auto (index 0) is the region's standard interlaced mode, NTSC 640x448i or
                // PAL 640x512i -- the one every PS2-era TV syncs.
                LOG("--- Triangle + Circle held at boot - forcing Video Mode to Auto/interlaced (recovery) ---\n");
                gVMode = 0;
                configSetInt(configOPL, CONFIG_OPL_VMODE, gVMode);
            } else {
                configGetInt(configOPL, CONFIG_OPL_VMODE, &gVMode);
            }
            screenPosLoad(configOPL); // this mode's own position, where one was saved (RIPPS2 build 124)



            int legacyView = -1;
            if (configGetStr(configOPL, CONFIG_OPL_THEME, &temp)) {
                themeID = thmFindGuiID(temp);
                legacyView = thmViewFromLegacyTheme(temp); // <Coverflow>/<PSBBN Flow> -> <OPL> + that view
            }

            // Before applyConfig loads the theme, so the saved view is the one that loads. The next save
            // writes <OPL> and theme_view=, which completes the migration of a legacy theme name.
            gThemeViewRequest = legacyView;
            if (configGetStr(configOPL, CONFIG_OPL_THEME_VIEW, &temp))
                gThemeViewRequest = thmViewFromName(temp);

            if (configGetStr(configOPL, CONFIG_OPL_LANGUAGE, &temp))
                langID = lngFindGuiID(temp);

            if (configGetInt(configOPL, CONFIG_OPL_SWAP_SEL_BUTTON, &value))
                gSelectButton = value == 0 ? KEY_CIRCLE : KEY_CROSS;

            sanitizePrefix(gBDMPrefix);
            sanitizePrefix(gETHPrefix);
            if (gAutoStartLastPlayed < 0)
                gAutoStartLastPlayed = 0;
            // resolveBootDirToMass runs before this read. A stored Disabled value must not undo
            // the AUTO fallback for an APA/PFS boot that already mounted and served OPL itself.
            if (gBootHomeApa && gHDDStartMode == START_MODE_DISABLED)
                gHDDStartMode = START_MODE_AUTO;
            // SIO2 pacing migration: silently upgrade the 0/0 defaults to 5/1
            {
                int pacingMigrated = 0;
                configGetInt(configOPL, CONFIG_OPL_MMCE_PACING_MIGR, &pacingMigrated);
                if (!pacingMigrated) {
                    if (gMMCEAckWaitCycles == 0 && gMMCEUseAlarms == 0) {
                        gMMCEAckWaitCycles = 5;
                        gMMCEUseAlarms = 1;
                    }
                    configSetInt(configOPL, CONFIG_OPL_MMCE_PACING_MIGR, 1);
                }
            }
            gCfgTitlesInLists = gCfgTitlesInLists ? 1 : 0;
            if (gPs1Duplicates < PS1_DUP_BOTH || gPs1Duplicates > PS1_DUP_POPSTARTER)
                gPs1Duplicates = PS1_DUP_BOTH;
            if (gDefaultGameView < GAME_VIEW_BOTH || gDefaultGameView > GAME_VIEW_MIXED)
                gDefaultGameView = GAME_VIEW_BOTH;
            if (gAppsDisplay < APPS_DISPLAY_MIXED || gAppsDisplay > APPS_DISPLAY_SPLIT)
                gAppsDisplay = APPS_DISPLAY_MIXED;
            // Where the user left each L3 page last session. Read from the LAST set (the same
            // configReadMulti above filled it), but sequenced HERE, after the two display settings
            // above: the restore validates every remembered position against what its mode can
            // currently display, and a position saved under a since-changed setting is dropped
            // rather than clamped, so that page opens on its default instead of on a view with no
            // rows. Reading it before those two would validate against stale settings.
            libViewLoadFromConfig(configGetByType(CONFIG_LAST));
            emberLoadSettings(configOPL);
            // A boot default-view locked to one type (VCD or ISO) must force the same one-shot
            // rescan the settings dialog does on a view change (gui.c). Without it, libViewActive()
            // short-circuits bdm/hdd/eth NeedsUpdate before the initial-scan trigger and the
            // list stays blank on boot -- a manual SELECT does not recover it (NeedsUpdate still
            // returns 0), only re-toggling the view does. This runs before applyConfig()'s first
            // support scans, so each VCD-capable page consumes the dirty flag on its first refresh.
            if (gDefaultGameView != GAME_VIEW_BOTH)
                libViewMarkAllDirty();
            // The POPSTARTER device picker is retired. Preserve older explicit choices by translating
            // them into the new full-path syntax; current runtime then uses the same deterministic
            // Custom -> Game Device -> MC order for old and new configs. Keep POPS_DEV_CUSTOM when a
            // path exists solely so downgrading to an older RiptOPL still honours that path.
            if (!configGetInt(configOPL, CONFIG_OPL_POPSTARTER_DEVICE, &gPopstarterDevice))
                gPopstarterDevice = (gPopstarterPath[0] != '\0') ? POPS_DEV_CUSTOM : POPS_DEV_DEFAULT;
            if (gPopstarterDevice < POPS_DEV_DEFAULT || gPopstarterDevice > POPS_DEV_ILINK)
                gPopstarterDevice = POPS_DEV_DEFAULT;
            if (gPopstarterPath[0] == '\0') {
                const char *migratedPath = NULL;
                switch (gPopstarterDevice) {
                    case POPS_DEV_MC:
                        migratedPath = "mc:/POPS/POPSTARTER.ELF";
                        break;
                    case POPS_DEV_USB:
                        migratedPath = "usb:/POPS/POPSTARTER.ELF";
                        break;
                    case POPS_DEV_MX4SIO:
                        migratedPath = "mx4sio:/POPS/POPSTARTER.ELF";
                        break;
                    case POPS_DEV_MMCE:
                        migratedPath = "mmce:/POPS/POPSTARTER.ELF";
                        break;
                    case POPS_DEV_EXFAT_HDD:
                        migratedPath = "ata:/POPS/POPSTARTER.ELF";
                        break;
                    case POPS_DEV_ILINK:
                        migratedPath = "ilink:/POPS/POPSTARTER.ELF";
                        break;
                    default:
                        break;
                }
                if (migratedPath != NULL)
                    snprintf(gPopstarterPath, sizeof(gPopstarterPath), "%s", migratedPath);
            }
            gPopstarterDevice = (gPopstarterPath[0] != '\0') ? POPS_DEV_CUSTOM : POPS_DEV_DEFAULT;
            if (!configGetInt(configOPL, CONFIG_OPL_POPSTARTER_RETROGEM_GAMEID, &gPopstarterRetroGemGameID))
                gPopstarterRetroGemGameID = 1;
            // A hand-edited or future-written value must not index past the three enum labels.
            if (gVcdUsbBdmaMode < VCD_USB_BDMA_ASK || gVcdUsbBdmaMode > VCD_USB_BDMA_FAT32)
                gVcdUsbBdmaMode = VCD_USB_BDMA_ASK;
            configReadNeutrinoGlobals(configOPL); // shared with miniInit's autolaunch path
            // Keep the stored domain identical to the Artwork page's enum {0,2,5,8} (item 45), so a
            // hand-edited or legacy value cannot render as a delay the UI is unable to express.
            // An out-of-domain value falls back to 0, matching setDefaults().
            if (gArtDelay != 0 && gArtDelay != 2 && gArtDelay != 5 && gArtDelay != 8)
                gArtDelay = 0;
            // Every settings save writes both plasma colours, so a config saved before the new defaults
            // holds the OLD pair -- official OPL's cyan over black -- even for someone who never touched
            // the Colors page. Exactly that pair means "never customised": give it the new defaults. Any
            // other value is a real choice and is kept.
            if (gDefaultBgColor[0] == 0x28 && gDefaultBgColor[1] == 0xC5 && gDefaultBgColor[2] == 0xF9 &&
                gDefaultPlasBlendColor[0] == 0x00 && gDefaultPlasBlendColor[1] == 0x00 && gDefaultPlasBlendColor[2] == 0x00) {
                gDefaultBgColor[0] = RIPTOPL_PLASMA_BG_R;
                gDefaultBgColor[1] = RIPTOPL_PLASMA_BG_G;
                gDefaultBgColor[2] = RIPTOPL_PLASMA_BG_B;
                gDefaultPlasBlendColor[0] = RIPTOPL_PLASMA_BLEND_R;
                gDefaultPlasBlendColor[1] = RIPTOPL_PLASMA_BLEND_G;
                gDefaultPlasBlendColor[2] = RIPTOPL_PLASMA_BLEND_B;
            }
            // clamp count to {3,5} on load -- defends a hand-edited conf.cfg
            gCoverflowCount = (gCoverflowCount == 5) ? 5 : 3;
            // clamp the remaining coverflow values too -- a hand-edited conf.cfg
            // otherwise feeds unbounded ints into signed render math
            if (gCoverflowCenterScale < 0)
                gCoverflowCenterScale = 0;
            else if (gCoverflowCenterScale > 1000)
                gCoverflowCenterScale = 1000;
            if (gCoverflowAnimSpeed < 0)
                gCoverflowAnimSpeed = 0;
            else if (gCoverflowAnimSpeed > 5000)
                gCoverflowAnimSpeed = 5000;
            gCoverflowDimCovers = gCoverflowDimCovers ? 1 : 0;
            if (gGridLayout < 0 || gGridLayout > GRID_LAYOUTS)
                gGridLayout = 0;
            int udpbdKeyPresent = configGetInt(configOPL, CONFIG_OPL_ENABLE_UDPBD, &gEnableUDPBD);
            // Unified network-protocol selector (single SMAP NIC -> at most one transport per session).
            // Read the new key if present (authoritative); otherwise DERIVE it from the three legacy keys,
            // preserving the historical "network BDM wins over SMB" precedence (imported/hand-edited configs
            // could set both; at boot UDPBD loaded first, so it won). NET_PROTO_UDPFS (filesystem) has no
            // legacy encoding, so it is only ever reached by an explicit new-format value -- backward-safe.
            // The legacy branch keys off the FILE's enable_udpbd, not the defaulted global -- a legacy
            // config must only ever derive from what IT expressed, never inherit a defaulted enable flag.
            if (!configGetInt(configOPL, CONFIG_OPL_NETWORK_PROTOCOL, &gNetworkProtocol)) {
                if (udpbdKeyPresent)
                    gNetworkProtocol = gEnableUDPBD ? ((gNetBootProtocol == NET_BOOT_UDPFS) ? NET_PROTO_UDPFSBD : NET_PROTO_UDPBD) : ((gETHStartMode != START_MODE_DISABLED) ? NET_PROTO_SMB : NET_PROTO_OFF);
                else if (gETHStartMode != START_MODE_DISABLED)
                    gNetworkProtocol = NET_PROTO_SMB;
                // else: the file never expressed ANY network choice -> the shipped default stands (Off)
            }
            // UDPBD (SUDPBDv2) is a first-class protocol, NOT folded away: it is wire-incompatible with
            // UDPRDMA (SUDPBDv2 on 0xBDBD vs UDPFS on 0xF5F6), so users still on the older udpbd-server
            // must be able to keep it. A saved or legacy-derived NET_PROTO_UDPBD is preserved as-is.
            // Re-derive the legacy shadows from the authoritative selector so downstream consumers
            // (ethsupport start path, system.c getDeviceName, bdmsupport) stay consistent no matter which
            // config format was loaded. SMB keeps its prior Auto/Manual start-mode; a fresh SMB pick that
            // had eth_mode=0 gets Manual. Any non-SMB protocol forces SMB off (preserves UDPBD-wins).
            // NOTE(rebuild): the fork also loads the SMB dialect (gSMBDialect, SMBv1/SMB2) here --
            // that returns with checklist item 4; until then the rebuild speaks SMBv1 unconditionally.
            gEnableUDPBD = (gNetworkProtocol == NET_PROTO_UDPBD || gNetworkProtocol == NET_PROTO_UDPFSBD);
            gNetBootProtocol = (gNetworkProtocol == NET_PROTO_UDPFSBD) ? NET_BOOT_UDPFS : NET_BOOT_UDPBD;
            if (gNetworkProtocol == NET_PROTO_SMB) {
                if (gETHStartMode == START_MODE_DISABLED)
                    gETHStartMode = START_MODE_MANUAL;
            } else {
                gETHStartMode = START_MODE_DISABLED;
            }

            // The remembered pick. A live protocol is the pick by definition; with none (Off), use the
            // saved pick, or SMB when there is none yet. Taken BEFORE the reconcile below, so a file that
            // names a protocol but has its start row Off still remembers that protocol.
            if (gNetworkProtocol != NET_PROTO_OFF)
                gNetProtocolPick = gNetworkProtocol;
            else if (!configGetInt(configOPL, CONFIG_OPL_NET_PROTOCOL_PICK, &gNetProtocolPick) ||
                     gNetProtocolPick <= NET_PROTO_OFF || gNetProtocolPick > NET_PROTO_HTTP)
                gNetProtocolPick = NET_PROTO_SMB;

            // Recover UDPFS Access independently of UDPBD. Older configs have no key: infer
            // IMG only if their active/remembered protocol really was UDPFSBD, else Files.
            if (!configGetInt(configOPL, CONFIG_OPL_UDPFS_ACCESS, &gUdpfsAccessMode) ||
                (gUdpfsAccessMode != 0 && gUdpfsAccessMode != 1))
                gUdpfsAccessMode = (gNetProtocolPick == NET_PROTO_UDPFSBD) ? 1 : 0;

            // Network start row (Off/Manual/Auto). A config predating this field has no net_start_mode
            // key -- derive it from the protocol we just resolved so an existing user keeps working:
            //   OFF   -> Off (Row 1); SMB -> its persisted eth_mode (so a prior SMB=Auto survives);
            //   UDPFS -> Manual (matches the old hardcoded UDPFS_MODE start gate); block -> Auto
            //   (matches the old bdm boot-connect for UDPBD/UDPFSBD, where start mode is cosmetic).
            if (!configGetInt(configOPL, CONFIG_OPL_NET_START_MODE, &gNetStartMode)) {
                if (gNetworkProtocol == NET_PROTO_OFF)
                    gNetStartMode = START_MODE_DISABLED;
                else if (gNetworkProtocol == NET_PROTO_SMB)
                    gNetStartMode = gETHStartMode;
                else if (gNetworkProtocol == NET_PROTO_UDPFS)
                    gNetStartMode = START_MODE_MANUAL;
                else
                    gNetStartMode = START_MODE_AUTO; // UDPFSBD / UDPBD block
            }
            // Reconcile the two persisted halves; Network Start Mode is the user's explicit gate.
            // A disabled start row must not be promoted to Manual merely because an older protocol
            // key still says SMB. Conversely, Protocol Off keeps the start row disabled so opening
            // the dialog cannot silently revive a transport.
            if (gNetStartMode == START_MODE_DISABLED || gNetworkProtocol == NET_PROTO_OFF) {
                gNetworkProtocol = NET_PROTO_OFF;
                gNetStartMode = START_MODE_DISABLED;
            } else if (gNetStartMode < START_MODE_MANUAL || gNetStartMode > START_MODE_AUTO) {
                gNetStartMode = START_MODE_MANUAL;
            }
            // Re-derive the legacy shadows after reconciliation, because the gate may have changed
            // a stale live protocol to OFF.
            gEnableUDPBD = (gNetworkProtocol == NET_PROTO_UDPBD || gNetworkProtocol == NET_PROTO_UDPFSBD);
            gNetBootProtocol = (gNetworkProtocol == NET_PROTO_UDPFSBD) ? NET_BOOT_UDPFS : NET_BOOT_UDPBD;
            gETHStartMode = (gNetworkProtocol == NET_PROTO_SMB) ? gNetStartMode : START_MODE_DISABLED;
        }

        // BOOT-DEVICE RECONCILE. The device OPL is RUNNING FROM always has its transport enabled.
        //
        // This existed for ATA alone, and rebuild-137b bolted MX4SIO on beside it. Both were the same
        // rule written twice, and writing it twice is what hid the fact that the two commonest
        // transports had no rule at all. The reasoning was never ATA-specific: the resolve step
        // force-loads whatever transport the boot prefix needs while IGNORING every gEnable* flag -- it
        // has to, because those flags live in the config the resolve exists to make readable -- so the
        // flag has to be repaired AFTER the read or the device OPL booted from has no tab, and the next
        // save persists the contradiction.
        //
        // What that omission actually cost: setDefaults ships gEnableUSB = 0 ("opt-in, like the other
        // BDM transports"), so a USB boot whose config is fresh, missing or unreadable came up with
        // gEnableUSB still 0. bdmUpdateDeviceData's publish gate then reads
        // bdmTransportEnabled(BDM_TYPE_USB) == 0 and WITHHOLDS the page -- no tab for the stick OPL is
        // running from, on the most common setup there is. Same for iLink. The MX4SIO report is what
        // exposed this, but it was never an MX4SIO bug.
        //
        // Runs whether or not a config file was found, AFTER the read so the file cannot overwrite it,
        // and persists so the state stops being contradictory. Setting these inside bdmResolveBootDir
        // instead is ineffective -- it runs before this read.
        //
        // Deliberately NOT touched here:
        //   - gBDMStartMode. This grants the TRANSPORT, not a tab. Config IO reaches massN: directly
        //     and does not need the device page, so "I boot from this stick but browse games on the
        //     HDD only" stays a valid, respected choice. Forcing Auto would overrule it.
        //   - BDM_TYPE_UDPBD. bdmResolveBootDir returns -1 for it without ever resolving (a network
        //     block device has no local mount at config-load time), and gEnableUDPBD is NIC-exclusive
        //     with the SMB/ETH stack -- forcing it on could tear down a working SMB session to serve a
        //     boot device that was never resolved in the first place.
        {
            const char *bootDevKey = NULL;
            int *bootDevFlag = NULL;

            switch (gBootDirBdmType) {
                case BDM_TYPE_USB:
                    bootDevKey = CONFIG_OPL_ENABLE_USB;
                    bootDevFlag = &gEnableUSB;
                    break;
                case BDM_TYPE_ILINK:
                    bootDevKey = CONFIG_OPL_ENABLE_ILINK;
                    bootDevFlag = &gEnableILK;
                    break;
                case BDM_TYPE_SDC:
                    bootDevKey = CONFIG_OPL_ENABLE_MX4SIO;
                    bootDevFlag = &gEnableMX4SIO;
                    break;
                case BDM_TYPE_ATA:
                    bootDevKey = CONFIG_OPL_ENABLE_BDMHDD;
                    bootDevFlag = &gEnableBdmHDD;
                    break;
                default:
                    break;
            }

            if (bootDevFlag != NULL && !*bootDevFlag) {
                LOG("CONFIG boot device is BDM type %d but its transport was off -- enabling %s\n",
                    gBootDirBdmType, bootDevKey);
                *bootDevFlag = 1;
                configSetInt(configGetByType(CONFIG_OPL), bootDevKey, *bootDevFlag);
            }
        }

        // The one place the rule above DOES grant the tab: an APA+exFAT hybrid homed on its exFAT volume,
        // where every game lives. Official OPL starts its BDM page Auto whenever it finds its config on a
        // BDM device, so there the games simply appear. Match that while the master is not the user's own
        // RiptOPL file on exFAT -- defaults, or settings carried over from the APA home,
        // which were saved when the home was APA. Once RiptOPL has saved on exFAT, that choice stands.
        if (gBootHomeHybridExfat && gBDMStartMode == START_MODE_DISABLED &&
            (!(result & CONFIG_OPL) || configOplIsCarryOver())) {
            LOG("CONFIG hybrid exFAT home without RiptOPL settings of its own -- BDM start mode Auto\n");
            gBDMStartMode = START_MODE_AUTO;
            configSetInt(configGetByType(CONFIG_OPL), CONFIG_OPL_BDM_MODE, gBDMStartMode);
        }
    }

    if (lscstatus & CONFIG_NETWORK) {
        // A missing conf_network.cfg only means network defaults once the master load above has run:
        // that already did the whole discovery, or found the home. Re-running it re-reads every set --
        // and a recovery miss ends in configEnd/configInit, which wiped the global game settings, last
        // played and apps that had just loaded.
        // loadConfig() has no caller that asks for network alone; keep that shape working anyway.
        if (!(result & CONFIG_NETWORK) && !(lscstatus & CONFIG_OPL)) {
            result = tryAlternateDevice(lscstatus);
        }

        if (result & CONFIG_NETWORK) {
            config_set_t *configNet = configGetByType(CONFIG_NETWORK);

            configGetInt(configNet, CONFIG_NET_ETH_LINKM, &gETHOpMode);

            configGetInt(configNet, CONFIG_NET_PS2_DHCP, &ps2_ip_use_dhcp);
            configGetInt(configNet, CONFIG_NET_SMB_NBNS, &gPCShareAddressIsNetBIOS);
            configGetStrCopy(configNet, CONFIG_NET_SMB_NB_ADDR, gPCShareNBAddress, sizeof(gPCShareNBAddress));

            if (configGetStr(configNet, CONFIG_NET_SMB_IP_ADDR, &temp))
                sscanf(temp, "%d.%d.%d.%d", &pc_ip[0], &pc_ip[1], &pc_ip[2], &pc_ip[3]);

            configGetInt(configNet, CONFIG_NET_SMB_PORT, &gPCPort);

            if (configGetStr(configNet, CONFIG_NET_HTTP_IP_ADDR, &temp))
                sscanf(temp, "%d.%d.%d.%d", &gHttpServerIp[0], &gHttpServerIp[1], &gHttpServerIp[2], &gHttpServerIp[3]);
            configGetInt(configNet, CONFIG_NET_HTTP_PORT, &gHttpPort);
            configGetStrCopy(configNet, CONFIG_NET_HTTP_BASE_PATH, gHttpBasePath, sizeof(gHttpBasePath));
            httpNormalizeBasePath(gHttpBasePath, sizeof(gHttpBasePath));

            configGetStrCopy(configNet, CONFIG_NET_SMB_SHARE, gPCShareName, sizeof(gPCShareName));
            configGetStrCopy(configNet, CONFIG_NET_SMB_USER, gPCUserName, sizeof(gPCUserName));
            configGetStrCopy(configNet, CONFIG_NET_SMB_PASSW, gPCPassword, sizeof(gPCPassword));

            if (configGetStr(configNet, CONFIG_NET_PS2_IP, &temp))
                sscanf(temp, "%d.%d.%d.%d", &ps2_ip[0], &ps2_ip[1], &ps2_ip[2], &ps2_ip[3]);
            if (configGetStr(configNet, CONFIG_NET_PS2_NETM, &temp))
                sscanf(temp, "%d.%d.%d.%d", &ps2_netmask[0], &ps2_netmask[1], &ps2_netmask[2], &ps2_netmask[3]);
            if (configGetStr(configNet, CONFIG_NET_PS2_GATEW, &temp))
                sscanf(temp, "%d.%d.%d.%d", &ps2_gateway[0], &ps2_gateway[1], &ps2_gateway[2], &ps2_gateway[3]);
            if (configGetStr(configNet, CONFIG_NET_PS2_DNS, &temp))
                sscanf(temp, "%d.%d.%d.%d", &ps2_dns[0], &ps2_dns[1], &ps2_dns[2], &ps2_dns[3]);

            configGetStrCopy(configNet, CONFIG_NET_NBD_DEFAULT_EXPORT, gExportName, sizeof(gExportName));
        }
    }

    // UDP transports bind their ministack to the saved STATIC PS2 IP fields and never consult DHCP.
    // ps2_ip_use_dhcp is preserved as the user's SMB/HTTP preference, so when that preference is DHCP
    // surface an informational boot toast reminding them which address UDP will actually use.
    showNetDhcpPopup = (ps2_ip_use_dhcp &&
                        (gNetworkProtocol == NET_PROTO_UDPFS || gNetworkProtocol == NET_PROTO_UDPFSBD ||
                         gNetworkProtocol == NET_PROTO_UDPBD));

    applyConfig(themeID, langID, 0);
    // applyConfig reloads only a CHANGED theme; the default <OPL> theme loaded at boot still shows its
    // default view. This reloads it once when the saved view differs.
    thmSetView(gThemeViewRequest);

    lscret = result;
    lscstatus = 0;
    // Only claim "Config loaded from ..." when a config file was actually READ. The toast used to
    // fire unconditionally, so with no config anywhere it still announced a device -- on hardware
    // that read as "it loaded MY settings from mass0:" when nothing of the sort happened.
    showCfgPopup = (result & CONFIG_OPL) ? 1 : 0;
}

static int trySaveConfigMMCE(int types)
{
    DIR *dir = opendir("mmce0:");
    if (dir != NULL) {
        closedir(dir);
        configSetMove("mmce0:");
        return configWriteMulti(types);
    }

    dir = opendir("mmce1:");
    if (dir != NULL) {
        closedir(dir);
        configSetMove("mmce1:");
        return configWriteMulti(types);
    }

    return -ENOENT;
}

static int trySaveConfigBDM(int types)
{
    char path[64];
    int bdm_result;

    // SAVE path: current filename only -- no legacy probe. Writing under the legacy name would
    // un-migrate an install that the load path above just migrated forward.
    // check USB
    bdm_result = bdmFindPartition(path, CONFIG_OPL_FILENAME, 1);
    // if not on USB, check BDM HDD
    if (bdm_result == 0) {
        // wait for up to 5 seconds for the HDD to spin up and become accessible...
        if (hddLoadModules() >= 0 && bdmHDDIsPresent(5000)) {
            bdm_result = bdmFindPartition(path, CONFIG_OPL_FILENAME, 1);
        }
    }

    if (bdm_result) {
        configSetMove(path);
        return configWriteMulti(types);
    }

    return -ENOENT;
}

static int trySaveConfigHDD(int types)
{
    hddLoadModules();
    hddLoadSupportModules();
    // Check that the formatted & usable HDD is connected.
    if (hddCheck() == 0 && gHDDPrefix != NULL) {
        configSetMove(gHDDPrefix);
        return configWriteMulti(types);
    }

    return -ENOENT;
}

static int trySaveConfigMC(int types)
{
    configSetMove(NULL);
    return configWriteMulti(types);
}

static int trySaveAlternateDevice(int types)
{
    char pwd[64] = {0};
    int value;

    if (gBootDir[0] != '\0')
        snprintf(pwd, sizeof(pwd), "%s", gBootDir);
    else if (getcwd(pwd, sizeof(pwd)) == NULL)
        pwd[0] = '\0';

    // First, try the device that OPL booted from.
    if (oplBootDirIsHostFs()) {
        // host: is the emulator's host filesystem. No support driver claims that prefix, so
        // oplPath2Mode() returns -1 for it and every boot-device fast path in OPL misses -- this
        // function in particular fell straight through to the all-devices sweep below, which probes
        // both MC slots, then blocks up to 1500 ms in bdmEnsureSourceModules() waiting for USB
        // modules, then scans MMCE. Under an emulator with nothing attached that is the entire cost,
        // paid to look for a home when a perfectly writable one is the directory we booted from.
        // It IS writable, so write there and stop.
        if ((value = configWriteMulti(types)) > 0)
            return value;
        LOG("CONFIG host: boot dir is not writable; falling back to device discovery\n");
    } else if (!strncmp(pwd, "hdd", 3) || !strncmp(pwd, "pfs", 3)) {
        if ((value = trySaveConfigHDD(types)) > 0)
            return value;
    } else if (!strncmp(pwd, "mass", 4) && (pwd[4] == ':' || pwd[5] == ':')) {
        if ((value = trySaveConfigBDM(types)) > 0)
            return value;
    } else if (!strncmp(pwd, "mmce", 4) && (pwd[4] == ':' || pwd[5] == ':')) {
        if ((value = trySaveConfigMMCE(types)) > 0)
            return value;
    }

    // Config was not saved to the boot device. Try all supported devices.
    // Try memory cards
    if (sysCheckMC() >= 0) {
        if ((value = trySaveConfigMC(types)) > 0)
            return value;
    }
    // Try MMCE
    if ((value = trySaveConfigMMCE(types)) > 0)
        return value;
    // Try a USB device
    if ((value = trySaveConfigBDM(types)) > 0)
        return value;
    // Try the HDD
    if ((value = trySaveConfigHDD(types)) > 0)
        return value;

    // We tried everything, but...
    return 0;
}

// configWriteMulti SUMS per-set results, and configWrite returns 1 for an UNMODIFIED set without
// touching the disk -- so a failed write of the master settings can hide behind untouched sibling
// sets and the save reports success ("Settings saved" toast, no retry). Concretely: OPL modified and
// failing returns 0, NETWORK and GAME untouched return 1 each, the sum is 2, and 2 > 0 reads as
// success. configWrite clears a set's modified flag only when its write actually succeeded, and
// _saveConfig configSet*s every set it means to save (marking it modified) -- so any REQUESTED set
// still marked modified after the write IS a failed write. Returns 0 in that case, the raw sum
// otherwise.
static int configWriteChecked(int types)
{
    int result = configWriteMulti(types);
    if (result > 0) {
        for (int bit = 1; bit < (1 << CONFIG_INDEX_COUNT); bit <<= 1) {
            if (types & bit) {
                config_set_t *cfg = configGetByType(bit); // NULL only if a requested set was never allocated
                if (cfg != NULL && cfg->modified())
                    return 0;
            }
        }
    }
    return result;
}

static const char *configFileName(const config_set_t *config)
{
    const char *name;

    if (config == NULL || config->filename() == NULL)
        return NULL;

    name = strrchr(config->filename(), '/');
    if (name != NULL)
        return name + 1;
    name = strrchr(config->filename(), ':');
    return name != NULL ? name + 1 : config->filename();
}

// The source picker does not remount live pfs0:. When the next boot's APA home differs from
// the live one, persist each changed global set through pfs1: first, then let the selector commit.
// This keeps a failed settings write from stranding the next boot on a home that never received it.
static int saveSelectedHddOplHome(int types)
{
    char prefix[64];
    char path[256];
    int selection = hddGetOplHomeSelection();
    int result = 1;

    snprintf(gLastSaveTarget, sizeof(gLastSaveTarget), "%s",
             selection == HDD_OPL_HOME_PLUS ? "hdd0:+OPL" : "hdd0:__common/OPL");
    if (!hddMountSelectedOplHome(prefix, sizeof(prefix))) {
        gLastSaveErrno = EIO;
        return 0;
    }

    for (int bit = 1; bit < (1 << CONFIG_INDEX_COUNT); bit <<= 1) {
        config_set_t *source;
        config_set_t *target;
        const char *name;
        int n;

        if (!(types & bit))
            continue;

        source = configGetByType(bit);
        // configWrite() only materializes a set that actually changed. Preserve that normal
        // contract so a selector-only save neither creates optional empty files nor overwrites an
        // existing target-side config that the user did not edit this session.
        if (source == NULL || !source->modified())
            continue;

        name = configFileName(source);
        n = name == NULL ? -1 : snprintf(path, sizeof(path), "%s%s", prefix, name);
        if (n < 0 || n >= (int)sizeof(path)) {
            gLastSaveErrno = EIO;
            result = 0;
            break;
        }

        // Keep the live sets pointing at their pfs0: home. The target is an isolated copy that
        // carries the live format and values, so pfs1: can be unmounted before selector commit.
        target = configAlloc(source->type(), NULL, path);
        if (target == NULL) {
            gLastSaveErrno = EIO;
            result = 0;
            break;
        }
        target->setFormat(source->format());
        configMerge(target, source);
        target->markModified();

        if (!configWrite(target) || target->modified()) {
            if (gLastSaveErrno == 0)
                gLastSaveErrno = EIO;
            result = 0;
            configFree(target);
            break;
        }
        configFree(target);
        source->clearModified();
    }

    hddUnmountSelectedOplHome();
    return result;
}

static int commitSelectedHddOplHome(void)
{
    if (hddCommitOplHomeSelection())
        return 1;

    snprintf(gLastSaveTarget, sizeof(gLastSaveTarget), "%s", "hdd0:__common/OPL/conf_hdd.cfg");
    gLastSaveErrno = EIO;
    return 0;
}

static void _saveConfig()
{
    char temp[256];
    char customSettingsTarget[sizeof(gCustomSettingsPath)] = {0};
    int customSettingsExplicit = 0;

    if (lscstatus & CONFIG_OPL) {
        config_set_t *configOPL = configGetByType(CONFIG_OPL);
        settingsSave(configOPL);
        screenPosSave(configOPL); // each video mode's position (RIPPS2 build 124)
#ifdef RETROACHIEVEMENTS
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", gRAHostIp[0], gRAHostIp[1], gRAHostIp[2], gRAHostIp[3]);
        configSetStr(configOPL, CONFIG_OPL_RA_HOST_IP, temp);
#endif
        configSetStr(configOPL, CONFIG_OPL_THEME, thmGetValue());
        if (gThemeViewRequest >= 0)
            configSetStr(configOPL, CONFIG_OPL_THEME_VIEW, thmViewName(gThemeViewRequest));
        else
            configRemoveKey(configOPL, CONFIG_OPL_THEME_VIEW);
        configSetStr(configOPL, CONFIG_OPL_LANGUAGE, lngGetValue());
        configSetInt(configOPL, CONFIG_OPL_MMCE_PACING_MIGR, 1);
        emberSaveSettings(configOPL);
        configSetInt(configOPL, CONFIG_OPL_POPSTARTER_DEVICE, gPopstarterDevice);
        configSetInt(configOPL, CONFIG_OPL_POPSTARTER_RETROGEM_GAMEID, gPopstarterRetroGemGameID);
        configSetStr(configOPL, CONFIG_OPL_NEUTRINO_ARGS, gNeutrinoArgs);
        configSetStr(configOPL, CONFIG_OPL_NEUTRINO_PATH, gNeutrinoPath);
        configSetInt(configOPL, CONFIG_OPL_DEFAULT_CORE, gDefaultCoreLoader);
        configSetInt(configOPL, CONFIG_OPL_NEUTRINO_VIDEO, gNeutrinoVideoDefault);
        configSetInt(configOPL, CONFIG_OPL_NEUTRINO_GSMCOMP, gNeutrinoGsmCompDefault);
        configSetInt(configOPL, CONFIG_OPL_NEUTRINO_DEVTYPE, gNeutrinoDevice);
        configSetInt(configOPL, CONFIG_OPL_NEUTRINO_ELF_ARG, gNeutrinoElfArg);
        configSetInt(configOPL, CONFIG_OPL_NEUTRINO_SHOW_ARGS, gNeutrinoShowArgs);
        configSetInt(configOPL, CONFIG_OPL_ENABLE_UDPBD, gEnableUDPBD);
        // Dual-write: the authoritative unified selector PLUS the three legacy keys (derived shadows),
        // so a config saved by this build still boots correctly on an older OPL that only reads the legacy keys.
        // NOTE(rebuild): the fork also persists the SMB dialect here (item 4).
        configSetInt(configOPL, CONFIG_OPL_NETWORK_PROTOCOL, gNetworkProtocol);
        configSetInt(configOPL, CONFIG_OPL_NET_START_MODE, gNetStartMode);
        configSetInt(configOPL, CONFIG_OPL_NET_PROTOCOL_PICK, gNetProtocolPick);
        configSetInt(configOPL, CONFIG_OPL_UDPFS_ACCESS, gUdpfsAccessMode);

        configSetInt(configOPL, CONFIG_OPL_SWAP_SEL_BUTTON, gSelectButton == KEY_CIRCLE ? 0 : 1);
    }

    // Fold in each page's current L3 position. Doing it at WRITE time is what keeps L3 itself free:
    // pressing it never touches storage, and the position rides along with a write this set was
    // already getting -- a game launch, or a settings save (which asks for CONFIG_LAST for exactly
    // this reason). configSetStr marks the set modified only when the text really changed, so a
    // session that never touched L3 still leaves the file alone.
    if (lscstatus & CONFIG_LAST)
        libViewStoreToConfig(configGetByType(CONFIG_LAST));

    if (lscstatus & CONFIG_NETWORK) {
        config_set_t *configNet = configGetByType(CONFIG_NETWORK);

        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3]);
        configSetStr(configNet, CONFIG_NET_PS2_IP, temp);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_netmask[0], ps2_netmask[1], ps2_netmask[2], ps2_netmask[3]);
        configSetStr(configNet, CONFIG_NET_PS2_NETM, temp);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_gateway[0], ps2_gateway[1], ps2_gateway[2], ps2_gateway[3]);
        configSetStr(configNet, CONFIG_NET_PS2_GATEW, temp);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_dns[0], ps2_dns[1], ps2_dns[2], ps2_dns[3]);
        configSetStr(configNet, CONFIG_NET_PS2_DNS, temp);

        configSetInt(configNet, CONFIG_NET_ETH_LINKM, gETHOpMode);
        configSetInt(configNet, CONFIG_NET_PS2_DHCP, ps2_ip_use_dhcp);
        configSetInt(configNet, CONFIG_NET_SMB_NBNS, gPCShareAddressIsNetBIOS);
        configSetStr(configNet, CONFIG_NET_SMB_NB_ADDR, gPCShareNBAddress);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", pc_ip[0], pc_ip[1], pc_ip[2], pc_ip[3]);
        configSetStr(configNet, CONFIG_NET_SMB_IP_ADDR, temp);
        configSetInt(configNet, CONFIG_NET_SMB_PORT, gPCPort);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", gHttpServerIp[0], gHttpServerIp[1], gHttpServerIp[2], gHttpServerIp[3]);
        configSetStr(configNet, CONFIG_NET_HTTP_IP_ADDR, temp);
        configSetInt(configNet, CONFIG_NET_HTTP_PORT, gHttpPort);
        configSetStr(configNet, CONFIG_NET_HTTP_BASE_PATH, gHttpBasePath);
        configSetStr(configNet, CONFIG_NET_SMB_SHARE, gPCShareName);
        configSetStr(configNet, CONFIG_NET_SMB_USER, gPCUserName);
        configSetStr(configNet, CONFIG_NET_SMB_PASSW, gPCPassword);
    }

    // A current APA session keeps pfs0: mounted on its original data home until restart. When the
    // selected next-boot home differs, write only the changed sets through pfs1: and commit the
    // selector only after that write succeeds. The normal automatic +OPL case still uses pfs0:.
    if (gBootHomeApa && gCustomSettingsPath[0] == '\0' && hddOplHomeSelectionNeedsTargetSave()) {
        if (!saveSelectedHddOplHome(lscstatus) ||
            (hddOplHomeSelectionPending() && !commitSelectedHddOplHome())) {
            lscret = 0;
            lscstatus = 0;
            return;
        }
        gLastSaveWasStagedOplHome = 1;
        lscret = 1;
        lscstatus = 0;
        return;
    }

    // APA launch identity is raw partition space, never a writable config filesystem. Even when
    // discovery loaded defaults or recovered settings elsewhere, an ordinary APA save without an
    // explicit Custom Settings Path must resolve the existing-PFS ownership chain NOW. This keeps
    // raw hddN: out of configWrite entirely: configured existing target first, then __common/OPL,
    // otherwise fail visibly without creating/formatting/repairing any APA partition.
    if (gBootHomeApa && gCustomSettingsPath[0] == '\0') {
        char hddSaveHome[64];
        if (prepareHddSettingsFallback(hddSaveHome, sizeof(hddSaveHome)) <= 0) {
            if (gOPLPart[0] != '\0')
                snprintf(gLastSaveTarget, sizeof(gLastSaveTarget), "%s", gOPLPart);
            else
                snprintf(gLastSaveTarget, sizeof(gLastSaveTarget), "%s", "hdd0:__common/OPL");
            gLastSaveErrno = ENODEV;
            lscret = 0;
            lscstatus = 0;
            return;
        }
        configSetMove(hddSaveHome);
    }

    char *path = configGetDir();
    // Only for the legacy no-boot-identity case. With a known boot dir the config home IS the boot
    // dir (or the custom path below); stamping mc?:OPL/ + icons here on an appdir-on-MC boot
    // sprouted an unwanted folder on every save, and rewriting the notification to the mc?:
    // wildcard made the "Settings saved to %s" toast name the wrong place (fork gates this
    // identically; FifthFox HW 2026-07-16 is the incident this comment block cites elsewhere).
    //
    // ...and for a NETWORK boot, whose home is a card for exactly the same reason: it has no boot
    // identity it can WRITE at boot time either. rebuild-143 moved that home onto the card and did
    // not move the folder creation with it, so the per-file O_CREAT below opened into a directory
    // that does not exist and every save failed with ENOENT -- reported as "could not write settings
    // to mc0: (error 2)". The boot dir is NON-empty on that path, so the original gate never fired.
    //
    // This is not the FifthFox case, and the second half of the condition is what proves it: the
    // folder is only ever created when the config home ALREADY IS a card. On a deferred boot it is
    // one solely because tryAlternateDevice put it there, and only after sysCheckMC() found a card
    // that was already serving this config. The incident being guarded against is a home that fell
    // back to mc?:OPL BY ACCIDENT on a device that had a perfectly good home of its own; a network
    // boot has no such home, which is exactly what _STR_SETTINGS_NO_HOME has always told the user.
    if ((gBootDir[0] == '\0' || gBootHomeDeferred) && !strncmp(path, "mc", 2)) {
        checkMCFolder();
        configPrepareNotifications(gBaseMCDir);
    }

    // Custom Settings Path is an explicit destination. Non-HDD targets still fail visibly
    // when unreachable. HDD/APA targets are different: they have a guaranteed safe ownership
    // chain (conf_hdd.cfg target -> __common/OPL), so an invalid HDD target falls back there
    // explicitly and the success dialog tells the user which HDD home was actually used.
    if (gCustomSettingsPath[0] != '\0') {
        snprintf(customSettingsTarget, sizeof(customSettingsTarget), "%s", gCustomSettingsPath);
        // Keep the user-facing spelling for any failure before configSetMove changes configGetDir().
        snprintf(gLastSaveTarget, sizeof(gLastSaveTarget), "%s", gCustomSettingsPath);
        if (prepareCustomSettingsPath(customSettingsTarget, sizeof(customSettingsTarget)) < 0) {
            if (isApaSettingsPath(gCustomSettingsPath) &&
                prepareHddSettingsFallback(customSettingsTarget, sizeof(customSettingsTarget)) > 0) {
                LOG("CONFIG HDD custom settings path %s unavailable; falling back safely to %s\n",
                    gCustomSettingsPath, customSettingsTarget);
                configSetMove(customSettingsTarget);
                snprintf(gLastSaveTarget, sizeof(gLastSaveTarget), "%s", customSettingsTarget);
                gHddSettingsFallbackNotice = 1;
                gLastSaveErrno = 0;
            } else {
                LOG("CONFIG custom settings path %s unreachable; aborting explicit save\n", gCustomSettingsPath);
                if (gLastSaveErrno == 0)
                    gLastSaveErrno = ENODEV;
                lscret = 0;
                lscstatus = 0;
                return;
            }
        } else {
            // A memory-card folder has to exist before the per-file O_CREAT below ("settings not
            // saving on mc1"). Made here, at save time, never by the boot-time read.
            if (createMcSettingsFolder(customSettingsTarget) < 0) {
                LOG("CONFIG custom settings folder %s could not be created (errno %d)\n", customSettingsTarget, gLastSaveErrno);
                lscret = 0;
                lscstatus = 0;
                return;
            }
            configSetMove(customSettingsTarget);
            customSettingsExplicit = 1;
        }
    }

    lscret = configWriteChecked(lscstatus);

    // Save icons for EVERY memory-card save home, not just the legacy mc?:OPL/ one. The block above
    // only stamps icons when there is no boot identity, so an appdir-on-MC boot (settings beside the
    // ELF) saved fine but left the folder showing as "Corrupted Data" in the PS2/PS3 browser. Keyed
    // on the config file's OWN path, after the custom-path re-home and the write, so the icons land
    // in the folder the settings actually went to -- which is what made the old mc?:OPL/ assumption
    // unsafe to run unconditionally. Off mc this is a no-op.
    if (lscret > 0) {
        config_set_t *oplCfg = configGetByType(CONFIG_OPL);
        if (oplCfg != NULL)
            checkMCSaveIcons(oplCfg->filename());
    }

    // Persist the bootstrap pointer only after the explicit config write succeeded. An explicit
    // target owns the whole save attempt: whether it succeeds or fails, do not retry onto the boot
    // device or an alternate device behind the user's back.
    if (customSettingsExplicit) {
        if (lscret > 0 && !writeConfigPathRedirect(customSettingsTarget)) {
            LOG("CONFIG could not persist settings redirect for %s\n", customSettingsTarget);
            gLastSaveErrno = EIO;
            lscret = 0;
        }
        if (lscret > 0 && hddOplHomeSelectionPending() && !commitSelectedHddOplHome())
            lscret = 0;
        lscstatus = 0;
        return;
    }

    // Boot-device save retry: ask the same concrete BDM identity to register once more, then retry
    // the write. A literal massN: remains that exact slot and is never replaced by another device;
    // a typed launch alias may still rewrite to its verified mount. If the boot device is gone the
    // save fails visibly instead of scattering settings elsewhere.
    if (lscret <= 0 && gBootDir[0] != '\0' && gBootHomeBdm) {
        char before[sizeof(gBootDir)];
        snprintf(before, sizeof(before), "%s", gBootDir);
        if (bdmResolveBootDir(gBootDir, sizeof(gBootDir), gBootElfName, &gBootDirBdmType) > 0) {
            if (strcmp(before, gBootDir) != 0) {
                LOG("BOOT re-resolved boot dir for save: %s -> %s\n", before, gBootDir);
                configSetMove(gBootDir); // keep the pending values; re-point the files to the verified mount
            }
            lscret = configWriteChecked(lscstatus);
        }
    }
    // <= 0, not == 0: configWriteChecked can only return 0 or a positive sum today, but the guard
    // costs nothing and a negative would otherwise read as success here.
    // The alternate-device hunt is ONLY for the legacy no-boot-identity case: with a known boot
    // dir, settings live there (or at the custom path) and a failed write must FAIL VISIBLY -- the
    // save-to-CWD doctrine. Scattering them onto whichever other device answers is how settings
    // ended up on mass0: on a memory-card boot.
    if (gBootDir[0] == '\0') {
        if (lscret <= 0)
            lscret = trySaveAlternateDevice(lscstatus);
        // Record where discovery landed so the NEXT boot skips the hunt (the redirect is a
        // legacy-discovery aid; with a known boot dir the custom-path block above owns it).
        if (lscret > 0)
            writeConfigPathRedirect(configGetDir());
    }
    if (lscret > 0 && hddOplHomeSelectionPending() && !commitSelectedHddOplHome())
        lscret = 0;
    lscstatus = 0;
}

void applyConfig(int themeID, int langID, int skipDeviceRefresh)
{
    // NO padRumbleFlush() here, deliberately: _loadConfig() reaches applyConfig from the IO
    // worker, and every libpad call in pad.c is GUI-thread-only (see guiHandleDeferedIO's note).
    // The GUI-thread callers that need a flush do it themselves before calling in.
    // A deliberate settings apply may make new art available, so clear the .tar "no archive
    // anywhere" latch and let it be probed once more. That latch (tar.c s_inactive[]) is write-once
    // and process-wide with no self-clearing path, so without this a user who boots with the loader
    // already ON and no archive present -- then plugs one in -- keeps getting nothing until a
    // reboot. The Artwork page's own toggle-flip re-arm only covers the case where the toggle
    // CHANGES; this covers the rest.
    // Gate archive and directory index invalidation on full device refresh (skipDeviceRefresh == 0).
    // Pure theme/color UI switches (skipDeviceRefresh == 1) keep the indices warm in memory (issue #488)
    // so returning to List Mode does not re-probe USB 1.1 storage.
    if (skipDeviceRefresh == 0) {
        tarInvalidate(TAR_KIND_ART);
        cacheInvalidateFailMemo();
        artIndexInvalidate(); // a full settings apply is the user's own "I changed something, look again"
    }

    // The bound is FAV_MODE, not APP_MODE. The Game Sources picker offers six entries and the last
    // of them is Favourites (guiSettingsShowSources deviceNames[], byte-identical to the fork), so an
    // APP_MODE bound silently rewrote the two choices ABOVE it -- Favourites and MMCE -- back to
    // Apps, on every applyConfig AND every boot. Picking "Favourites" as the startup page snapped
    // the row back to Apps before the value was ever written to the config: the whole feature was
    // dead with its picker entry, its enum value and its config key all present. Data half without
    // the code half, again.
    if (gDefaultDevice < 0 || gDefaultDevice > FAV_MODE)
        gDefaultDevice = MMCE_MODE;
    // Favourites is a valid startup page only while the FAV tab is enabled; otherwise a stale
    // choice would boot into a hidden tab with no way back to a visible one.
    if (gDefaultDevice == FAV_MODE && !gFAVStartMode && !gAllGames)
        gDefaultDevice = MMCE_MODE;
    if (gDefaultDevice == MMCE_MODE && gMMCEStartMode == START_MODE_DISABLED)
        gDefaultDevice = APP_MODE;

    guiUpdateScrollSpeed();

    guiSetFrameHook(&menuUpdateHook);

    int changed = rmSetMode(0);
    if (changed) {
        bgmMute();
        // reinit the graphics...
        thmReloadScreenExtents();
        guiReloadScreenExtents();
    }

    // theme must be set after color, and lng after theme
    changed = thmSetGuiValue(themeID, changed);
    // A theme with SD/HD layouts or alt= pictures is rebuilt for the Widescreen and Alternate Background
    // settings it is now shown with.
    if (!changed)
        changed = thmRefreshDisplayLayout();
    int langChanged = lngSetGuiValue(langID);

    guiUpdateScreenScale();

    // Check if we should refresh device support as well.
    if (skipDeviceRefresh == 0) {
        initAllSupport(0);

        for (int i = 0; i < MODE_COUNT; i++) {
            if ((*oplGetModule(i)).support == NULL)
                continue;

            moduleUpdateMenuInternal(&(*oplGetModule(i)), changed, langChanged);
        }
    } else {
        if (changed) {
            // Same serialization as moduleUpdateMenuInternal: never realloc the per-item art-pair
            // arrays while the GUI thread may be drawing through them.
            guiLock();
            for (int i = 0; i < MODE_COUNT; i++) {
                if ((*oplGetModule(i)).support && (*oplGetModule(i)).subMenu)
                    submenuRebuildCache((*oplGetModule(i)).subMenu);
            }
            guiUnlock();
        }
    }

    bgmUnMute();

#ifdef __DEBUG
    debugApplyConfig();
#endif
}

int saveConfig(int types, int showUI)
{
    char notification[128];
    lscstatus = types;
    lscret = 0;
    // Reset before the attempt. gLastSaveErrno is only ever ASSIGNED at the three real failure sites
    // in configWrite, never cleared -- so a stale value from an earlier save could be reported for a
    // later one, and (worse) a save that never reached configWrite at all reported whatever was left,
    // usually the 0 that produced the nonsensical "(error 0)".
    gLastSaveErrno = 0;
    gLastSaveTarget[0] = '\0';
    gLastSaveWasStagedOplHome = 0;
    gHddSettingsFallbackNotice = 0;

    guiHandleDeferedIO(&lscstatus, _l(_STR_SAVING_SETTINGS), IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&_saveConfig), OPL_DEFERRED_IO_TIMEOUT_MS);

    if (showUI) {
        if (lscret) {
            char *path = configGetDir();
            const char *displayHome = gLastSaveWasStagedOplHome ? gLastSaveTarget : path;
            int savedOnHddHome = gLastSaveWasStagedOplHome ||
                                 (path != NULL && !strncmp(path, "pfs", 3) && gOPLPart[0] != '\0');

            // pfs0: is only the transient mount name. For every successful HDD save, show the
            // physical APA/PFS owner the user can actually find in a partition browser.
            if (savedOnHddHome && !gLastSaveWasStagedOplHome) {
                if (!strcmp(gOPLPart, "hdd0:__common"))
                    displayHome = "hdd0:__common/OPL";
                else
                    displayHome = gOPLPart;
            }

            if (gHddSettingsFallbackNotice || (!gLastSaveWasStagedOplHome && gBootHddCommonFallback && savedOnHddHome))
                snprintf(notification, sizeof(notification), _l(_STR_SETTINGS_HDD_FALLBACK), displayHome);
            else
                snprintf(notification, sizeof(notification), _l(_STR_SETTINGS_SAVED), displayHome);

            guiMsgBox(notification, 0, NULL);
        } else {
            // Distinguish "the write failed" from "the write never ran". guiHandleDeferedIO abandons
            // its wait after OPL_DEFERRED_IO_TIMEOUT_MS and clears the status WITHOUT _saveConfig
            // having executed -- the IO worker is single-threaded and shared with cover-art loads, so
            // a queue full of slow art can starve the save past the bound. In that case nothing ever
            // touched the config, so reporting a write error against the config path is a lie.
            if (gLastDeferredTimedOut)
                snprintf(notification, sizeof(notification), "%s", _l(_STR_ERR_DEVICE_BUSY_TIMEOUT));
            else {
                const char *failedPath = gLastSaveTarget[0] != '\0' ? gLastSaveTarget : configGetDir();
                snprintf(notification, sizeof(notification), _l(_STR_ERROR_SAVING_SETTINGS_TO), failedPath, gLastSaveErrno);
                diagLog("settings: not saved to %s, error %d", failedPath, gLastSaveErrno);
            }
            guiMsgBox(notification, 0, NULL);
        }
    }

    return lscret;
}
