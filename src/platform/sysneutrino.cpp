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
#include "include/diaglog.h" // the hardware log, DIAG.TXT (RIPPS2 build 115)
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
#include <usbhdfsd-common.h>          // USBMASS_IOCTL_GET_LBA / _CHECK_CHAIN -- VMC contiguity probe
#include "include/ps2sdkapi_compat.h" // ps2sdk_get_iop_fd -- the ioctls above take the IOP-side fd

// Neutrino launch: argument/device mapping, the preflight and sysLaunchNeutrino. Split out of system.cpp.

// --- Neutrino external-core launch (per-game $CoreLoader) --------------------
// Maps an OPL block-device driver token to Neutrino's -bsd backend name. Uses
// exact strcmp per token (NOT a strncmp prefix test): a prefix test matches
// "sdc" against "sd" first and mis-maps MX4SIO to ilink. Mirrors our hardened
// bdmDriverIs* matchers. Returns "unsupported" for anything
// Neutrino cannot back (e.g. eth/smb), which the caller treats as a hard stop.
static const char *getDeviceName(const char *driver)
{
    if (driver == NULL)
        return "unsupported";
    if (!strcmp(driver, "usb"))
        return "usb";
    if (!strcmp(driver, "ilink") || !strcmp(driver, "sd"))
        return "ilink";
    if (!strcmp(driver, "mx4sio") || !strcmp(driver, "sdc"))
        return "mx4sio";
    if (!strcmp(driver, "ata"))
        return "ata";
    if (!strcmp(driver, "apa"))
        return "apa";
    if (!strcmp(driver, "mmce"))
        return "mmce";
    if (!strcmp(driver, "dvr"))
        return "dvr";
    if (!strcmp(driver, "udp"))
        // Both BLOCK transports register the "udp" BDM token; the RESIDENT protocol picks the Neutrino
        // backing store: -bsd=udpbd (smap_udpbd / SUDPBDv2) vs -bsd=udpfsbd (the udpfs_bd UDPRDMA toml).
        return (bdmGetLoadedNetProtocol() == NET_BOOT_UDPFS) ? "udpfsbd" : "udpbd";
    if (!strcmp(driver, "udpfs"))
        // UDPFS *filesystem* device (udpfssupport / udpfs_ioman): the stock Neutrino -bsd token loads
        // config/bsd-udpfs.toml (the FHI filesystem driver), then opens -dvd=udpfs:<path> by name -- no
        // massN: block device and no fraglist. Distinct from the "udp" block tokens above.
        return "udpfs";
    return "unsupported";
}

// Re-encodes the OPL compatmask into Neutrino's -gc concatenated-digit format.
// Forwards OPL bits 1,2,3,5 plus Mode 7 (COMPAT_MODE_7) -- a Neutrino-only flag with
// no OPL ee-core effect (greyed under the OPL core) that emits -gc=7 "fix game buffer
// overrun". result[] is sized off COMPAT_MODE_COUNT
// and every append is bounds-guarded so widening the set later cannot overflow.
// NOTE: Neutrino's -gc numbering is Neutrino's own, not OPL's bitmask semantics;
// the OPL<->Neutrino correspondence must be hardware-verified before widening.
static int convertCompatmaskToModes(int compatmask)
{
    char result[COMPAT_MODE_COUNT + 2];
    int pos = 0;

    if ((compatmask & COMPAT_MODE_1) && pos < (int)sizeof(result) - 1)
        result[pos++] = '1';
    if ((compatmask & COMPAT_MODE_2) && pos < (int)sizeof(result) - 1)
        result[pos++] = '2';
    if ((compatmask & COMPAT_MODE_3) && pos < (int)sizeof(result) - 1)
        result[pos++] = '3';
    if ((compatmask & COMPAT_MODE_5) && pos < (int)sizeof(result) - 1)
        result[pos++] = '5';
    if ((compatmask & COMPAT_MODE_7) && pos < (int)sizeof(result) - 1)
        result[pos++] = '7'; // Neutrino "IOP: fix game buffer overrun"; greyed under the OPL core

    result[pos] = '\0';
    return atoi(result);
}

// Append either Neutrino options (before --b) or ELF arguments (after --b) from one
// scope. Each pass copies src because strtok edits buf; both buffers stay live until
// ExecPS2 consumes argv. A global --b must not turn per-game Neutrino options into
// ELF arguments, so the two scopes are assembled before their tails.
static int appendArgTokens(char **argv, int argc, int argvMax, char *buf, int bufSize,
                           const char *src, int elfArgs, int *hasBreak)
{
    if (src == NULL || src[0] == '\0')
        return argc;

    snprintf(buf, bufSize, "%s", src);

    char *tok = strtok(buf, " \t");
    int afterBreak = 0;
    while (tok != NULL && argc < argvMax) {
        if (!afterBreak && strcmp(tok, "--b") == 0) {
            afterBreak = 1;
            *hasBreak = 1;
            tok = strtok(NULL, " \t");
            continue;
        }
        // A leading '$' marks a user-disabled flag (NHDDL convention): keep it in the
        // args field for easy re-enabling, but don't forward it to Neutrino. Neutrino
        // flags are all '-'-prefixed, so '$' never collides with a real argument.
        if (afterBreak == elfArgs && tok[0] != '$')
            argv[argc++] = tok;
        tok = strtok(NULL, " \t");
    }
    return argc;
}

// Does `s` look like a retail boot-file name (AAAA_NNN.NN, e.g. SLUS_123.45)? Gates the opt-in
// -elf=cdrom0: emission below to startups neutrino can actually resolve a GameID from.
static int sysStartupShapeOk(const char *s)
{
    int i;
    if (s == NULL || strlen(s) != 11 || s[4] != '_' || s[8] != '.')
        return 0;
    for (i = 0; i < 4; i++)
        if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9')))
            return 0;
    for (i = 5; i <= 10; i++) {
        if (i == 8)
            continue;
        if (s[i] < '0' || s[i] > '9')
            return 0;
    }
    return 1;
}

// True if `args` contains `flag` as an ACTIVE token -- i.e. NOT $-disabled (the leading-$ convention
// that appendArgTokens strips at dispatch). Lets an auto-emitted flag stay suppressed only by a flag
// the user is actually forwarding, not by one they turned off with `$`.
// Does `args` contain an ACTIVE (non-$-disabled) occurrence of `flag` as its OWN token? `flag` is
// either an exact switch ("-dbc") or a key prefix ending in '=' ("-gsm="). A raw strstr is not
// enough: "-dbc" also occurs inside path-valued tokens like -elf=my-dbc-game.elf (PR #96 review),
// which must NOT count -- so a hit needs a token boundary on BOTH sides (start-of-string or
// whitespace before; end-of-string or whitespace after, unless the flag itself is a key prefix
// whose value legitimately continues). This also fixes the same latent false positive for -gsm=.
static int neutrinoArgHasActiveFlag(const char *args, const char *flag)
{
    if (args == NULL)
        return 0;
    size_t flen = strlen(flag);
    int keyPrefix = (flen > 0 && flag[flen - 1] == '=');
    for (const char *p = args; *p != '\0';) {
        while (*p == ' ' || *p == '\t')
            p++;
        const char *end = p;
        while (*end != '\0' && *end != ' ' && *end != '\t')
            end++;
        size_t len = end - p;
        if (len == 3 && !strncmp(p, "--b", 3))
            break;
        if (len >= flen && !strncmp(p, flag, flen) && (keyPrefix || len == flen))
            return 1;
        p = end;
    }
    return 0;
}

// The -bsdfs Neutrino will actually run with, so the preflight and the argv builder agree on it.
// Read the user tokens the way Neutrino parses them: the global args, then the per-game args (the
// order appendArgTokens adds them), last one wins, a $-prefixed token is switched off, and --b
// ends Neutrino options within each scope. Their ELF tails are appended after both scopes. A typed
// -bsdfs= found that way beats the per-game picker. mmce/udpfs are fileid backends with no fs layer.
// Returns 1 exfat / 2 hdl / 3 bd; 0 = Neutrino's own default (exfat) or a typed value it does not
// name.
static int neutrinoEffectiveBsdfs(const char *deviceName, int neutrinoBsdfs, const char *extraArgs)
{
    static const char *const names[] = {"", "exfat", "hdl", "bd"};
    const char *sources[2] = {gNeutrinoArgs, extraArgs};
    int typed = 0, value = 0;

    for (int s = 0; s < 2; s++) {
        for (const char *p = sources[s]; p != NULL && *p != '\0';) {
            while (*p == ' ' || *p == '\t')
                p++;
            const char *end = p;
            while (*end != '\0' && *end != ' ' && *end != '\t')
                end++;
            int len = (int)(end - p);
            if (len == 3 && !strncmp(p, "--b", 3))
                break;
            if (len > 7 && !strncmp(p, "-bsdfs=", 7)) {
                typed = 1;
                value = 0;
                for (int i = 1; i <= 3; i++) {
                    if ((int)strlen(names[i]) == len - 7 && !strncmp(p + 7, names[i], len - 7))
                        value = i;
                }
            }
            p = end;
        }
    }
    if (!strcmp(deviceName, "mmce") || !strcmp(deviceName, "udpfs") || !strcmp(deviceName, "dvr"))
        return 0;
    if (typed)
        return value;
    return (neutrinoBsdfs >= 1 && neutrinoBsdfs <= 3) ? neutrinoBsdfs : 0;
}

// Hand the game off to an external Neutrino ELF instead of OPL's embedded core.
// Hardened vs the wOPL original: NULL-guarded; an "unsupported" device aborts
// (never launches -bsd=unsupported); DISTINCT argv buffers (wOPL reused ONE
// buffer for both -bsd=ata and -bsdfs=hdl, clobbering -bsd=ata on HDD); argv[32]
// with per-append bounds guards (wOPL's argv[6] was exactly full). User-supplied
// flags (global gNeutrinoArgs + the per-game extraArgs) are tokenized and appended last.
// Rewrite the "ip=A.B.C.D" token inside <neutrino dir>/config/bsd-<device>.toml to the PS2's configured
// static IP. Neutrino's ministack takes its IP from that toml -- hardcoded 192.168.1.10 in the stock and
// bundled files -- NOT from anything OPL used while browsing, so any mismatch means the UDPFS server's
// discovery reply never routes back and the game black-screens after a perfectly healthy list. Δ6: now
// runs PRE-deinit via sysNeutrinoPreflight (every mount is up, the GUI is alive), so a hard failure can
// abort to the menu with a toast instead of dying invisibly post-teardown. Anchored to the stock `"ip=`
// quoting so a comment mentioning ip= can never be clobbered.
// Returns 0 = OK to launch (synced / already-in-sync / benign leave-alone), <0 = abort the launch:
// the toml is MISSING (neutrino cannot load its bsd config at all) or was mangled-then-restored (the
// old ip survives, so the boot would black-screen on any other subnet).
static const char *sysLastPathSeparator(const char *path)
{
    const char *slash;
    const char *backslash;

    if (path == NULL)
        return NULL;
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        return backslash;
    return slash;
}

// The most a torn tail can be: one shortened ip loses at most 8 characters (15 down to 7), and a
// card may have been through a few such rewrites before this fix.
#define NEUTRINO_TOML_TORN_TAIL_MAX 32

// Length of the toml with a torn tail cut off, `len` when there is none, or -1 when it is broken some
// other way -- a hand-edited file is the user's to fix and is left alone. Reads just enough TOML for
// this: comments, the four string forms and bracket depth. A line is complete at its end, or as soon
// as its key's value or its table header closes; past that point only spaces and a comment may follow,
// which is how a tear on a final line WITHOUT a newline (`env = ["EE"]"]`) is found. The cut is the
// last complete point, and only a short tail is ever dropped.
static int sysTomlTornTailCut(const char *text, int len)
{
    int depth = 0, complete = 0, i = 0;
    int lineStart = 1, header = 0, assigned = 0, ended = 0;

    while (i < len) {
        char c = text[i];

        if (c == '\n') {
            i++;
            if (depth == 0)
                complete = i;
            lineStart = 1;
            ended = 0;
            if (depth == 0) // a multi-line array keeps its key's line going until it closes
                header = assigned = 0;
            continue;
        }
        if (c == '#') {
            while (i < len && text[i] != '\n')
                i++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            i++;
            continue;
        }
        if (ended)
            goto broken; // something after a finished value or header on the same line
        if (lineStart && depth == 0 && c == '[')
            header = 1;
        lineStart = 0;
        if (c == '"' || c == '\'') {
            if (i + 2 < len && text[i + 1] == c && text[i + 2] == c) {
                // Multi-line string: runs to the next triple quote, across lines.
                for (i += 3; i + 2 < len && !(text[i] == c && text[i + 1] == c && text[i + 2] == c); i++) {
                    if (c == '"' && text[i] == '\\')
                        i++;
                }
                if (i + 2 >= len)
                    goto broken;
                i += 3;
            } else {
                // Single-line string: must close before the line ends.
                for (i++; i < len && text[i] != c && text[i] != '\n'; i++) {
                    if (c == '"' && text[i] == '\\' && i + 1 < len && text[i + 1] != '\n')
                        i++;
                }
                if (i >= len || text[i] != c)
                    goto broken;
                i++;
            }
            if (depth == 0 && assigned) {
                ended = 1;
                complete = i;
            }
            continue;
        }
        if (c == '=' && depth == 0)
            assigned = 1;
        if (c == '[' || c == '{')
            depth++;
        else if ((c == ']' || c == '}') && --depth < 0)
            goto broken;
        i++;
        if ((c == ']' || c == '}') && depth == 0 && (assigned || header)) {
            ended = 1;
            complete = i;
        }
    }
    if (depth == 0)
        return len;

broken:
    if (complete == 0 || len - complete > NEUTRINO_TOML_TORN_TAIL_MAX)
        return -1;
    return complete;
}

static int sysSyncNeutrinoUdpfsToml(const char *neutrinoPath, const char *deviceName)
{
    static char toml[4096]; // Δ6: raised from 2048 -- an annotated toml no longer gets skipped
    static char updated[4096 + 20];
    char tomlPath[288];
    char newIp[20];
    int fd, len;

    const char *slash = sysLastPathSeparator(neutrinoPath);
    if (slash == NULL)
        return 0; // custom flat path -- no dir to anchor on; old hand-edit contract
    snprintf(tomlPath, sizeof(tomlPath), "%.*sconfig%cbsd-%s.toml",
             (int)(slash - neutrinoPath) + 1, neutrinoPath, *slash, deviceName);

    fd = open(tomlPath, O_RDONLY);
    if (fd < 0) {
        LOG("[NEUTRINO] no %s -- neutrino cannot boot this transport without it\n", tomlPath);
        return -1; // missing bsd toml = guaranteed post-teardown failure -> abort while GUI is alive
    }
    // Accumulate: a short read must not become a rewrite that drops the rest of the file.
    len = 0;
    for (int got; len < (int)sizeof(toml) - 1 && (got = read(fd, toml + len, sizeof(toml) - 1 - len)) != 0; len += got) {
        if (got < 0) {
            len = got;
            break;
        }
    }
    close(fd);
    if (len <= 0)
        return -1; // unreadable/empty bsd toml -> same guaranteed failure
    if (len >= (int)sizeof(toml) - 1) {
        LOG("[NEUTRINO] %s larger than %d -- ip= left as-is\n", tomlPath, (int)sizeof(toml));
        return 0; // exotic hand-built toml: proceed on the old hand-edit contract
    }
    toml[len] = '\0';

    // A tail torn by an earlier sync sits after the ip, so the ip already matches and nothing below
    // would rewrite the file again: every later launch fails the same way. Cut it off here, and
    // write the file back below even when the ip is unchanged (original #1011).
    int cut = sysTomlTornTailCut(toml, len);
    int repaired = cut >= 0 && cut < len;
    if (repaired) {
        LOG("[NEUTRINO] %s has %d stray byte(s) after its end -- removing them\n", tomlPath, len - cut);
        len = cut;
        toml[len] = '\0';
    }

    char *tok = strstr(toml, "\"ip=");
    if (tok == NULL) {
        LOG("[NEUTRINO] %s has no \"ip= token -- left as-is\n", tomlPath);
        return 0; // hand-restructured toml: the user owns the ip; proceed
    }
    char *valStart = tok + 4;
    char *valEnd = valStart;
    while (*valEnd == '.' || (*valEnd >= '0' && *valEnd <= '9'))
        valEnd++;

    snprintf(newIp, sizeof(newIp), "%d.%d.%d.%d", ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3]);
    if (!repaired && (int)strlen(newIp) == (int)(valEnd - valStart) && !strncmp(valStart, newIp, strlen(newIp)))
        return 0; // already in sync -- don't touch the file

    snprintf(updated, sizeof(updated), "%.*s%s%s", (int)(valStart - toml), toml, newIp, valEnd);

    // mcman truncates an existing file only in its O_CREAT branch.
    fd = open(tomlPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        LOG("[NEUTRINO] cannot rewrite %s (%d) -- ip= left as-is\n", tomlPath, fd);
        return 0; // write-protected media: proceed on the old hand-edit contract
    }
    int expected = (int)strlen(updated);
    int written = write(fd, updated, expected);
    close(fd);
    if (written != expected) {
        // O_TRUNC already emptied the file; a short write would leave a mangled toml behind. Best
        // recovery without atomic-rename support across every PS2 filesystem: put the ORIGINAL
        // content back so the user is no worse off than before the sync (old hand-edit contract).
        LOG("[NEUTRINO] short write to %s (%d/%d) -- restoring original toml\n", tomlPath, written, expected);
        fd = open(tomlPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) {
            write(fd, toml, len);
            close(fd);
        }
        return -1; // the OLD ip survives -> the boot would black-screen; abort while GUI is alive
    }
    LOG("[NEUTRINO] synced %s ip= -> %s\n", tomlPath, newIp);
    return 0;
}

static int sysNeutrinoDvrPreflight(const char *neutrinoPath, const char *extraArgs)
{
    // This runs before teardown. DVR needs Neutrino's PSX-mode transition;
    // quickboot and a user-supplied block backend would bypass that contract.
    const char *sources[] = {gNeutrinoArgs, extraArgs};
    for (int i = 0; i < 2; i++) {
        if (neutrinoArgHasActiveFlag(sources[i], "-qb")) {
            host::ui().warningId(_STR_NEUTRINO_DVR_QUICKBOOT, 6);
            return -1;
        }
        if (neutrinoArgHasActiveFlag(sources[i], "-bsd=") || neutrinoArgHasActiveFlag(sources[i], "-bsdfs=")) {
            host::ui().warningId(_STR_NEUTRINO_DVR_BACKEND_OVERRIDE, 6);
            return -1;
        }
    }

    const char *slash = sysLastPathSeparator(neutrinoPath);
    if (slash == NULL) {
        host::ui().missingFileId(_STR_NEUTRINO_DVR_FILE_MISSING, neutrinoPath);
        return -1;
    }
    const int dirLen = (int)(slash - neutrinoPath);
    static const char *const modules[] = {"dvrdrv.irx", "dvrfile.irx", "dvrfile_fhi.irx"};
    for (int i = -1; i < 3; i++) {
        char path[336] = {0};
        int n = i < 0 ? snprintf(path, sizeof(path), "%.*s%cconfig%cbsd-dvr.toml", dirLen, neutrinoPath, *slash, *slash) :
                        snprintf(path, sizeof(path), "%.*s%cmodules%c%s", dirLen, neutrinoPath, *slash, *slash, modules[i]);
        if (n < 0 || n >= (int)sizeof(path)) {
            host::ui().missingFileId(_STR_NEUTRINO_DVR_FILE_MISSING, path);
            return -1;
        }
        int fd = open(path, O_RDONLY);
        if (fd < 0 && i < 0) {
            n = snprintf(path, sizeof(path), "%.*s%cbsd-dvr.toml", dirLen, neutrinoPath, *slash);
            if (n >= 0 && n < (int)sizeof(path))
                fd = open(path, O_RDONLY);
        }
        if (fd < 0) {
            host::ui().missingFileId(_STR_NEUTRINO_DVR_FILE_MISSING, path);
            return -1;
        }
        close(fd);
    }
    return 0;
}

// Δ6 (NHDDL parity): everything that can FAIL a Neutrino launch and is checkable pre-teardown runs
// HERE, called by every device leg BEFORE deinitEx -- the GUI is alive for a toast and nothing has
// been torn down, so a failure is "stay in the menu", not a post-teardown black screen. Returns
// 0 = proceed; <0 = the Neutrino leg cannot run (a toast was shown): the caller aborts, or on a local
// BDM device falls back to the native core. neutrinoBsdfs/extraArgs/bdDevNr are what the leg will
// pass to sysLaunchNeutrino (0 / NULL / -1 for legs without a block device).
int sysNeutrinoPreflight(const char *driver, const char *neutrinoPath, int neutrinoBsdfs, const char *extraArgs, int bdDevNr)
{
    if (driver == NULL || neutrinoPath == NULL)
        return -1;

    const char *deviceName = getDeviceName(driver);
    if (!strcmp(deviceName, "unsupported")) {
        LOG("[NEUTRINO] preflight: unsupported device '%s'\n", driver);
        host::ui().warningId(_STR_NEUTRINO_DEV_UNSUPPORTED, 6);
        return -1;
    }

    if (!strcmp(deviceName, "dvr") && sysNeutrinoDvrPreflight(neutrinoPath, extraArgs) < 0)
        return -1;

    // -bsdfs=bd names the raw bdm device "<driver><devNr>p0". A mount that never reported its device
    // number cannot be named, and guessing 0 opens the WRONG device when two share a driver (a second
    // stick is usb1). ATA is exempt: one synthetic device, always 0 -- the native leg's device-number
    // check in bdmLaunchGame exempts it for the same reason. Whether bd came from the picker or was
    // typed makes no difference. Nor does a typed -dvd=: it is a tail token the argv budget can
    // drop, which would leave our device-0 tuple in charge after the teardown (CodeRabbit, #762).
    // The native leg refuses a device with no number outright; this refuses only its bd launches.
    if (neutrinoEffectiveBsdfs(deviceName, neutrinoBsdfs, extraArgs) == 3 && bdDevNr < 0 && strcmp(deviceName, "ata") != 0) {
        LOG("[NEUTRINO] preflight: -bsdfs=bd on '%s' with no device number\n", driver);
        host::ui().warningId(_STR_NEUTRINO_BD_NO_DEVICE_NUMBER, 6);
        return -1;
    }

    // Network transports: sync the bsd toml ip= NOW, while the toml's device is mounted and a
    // failure can still be reported (see sysSyncNeutrinoUdpfsToml for the abort conditions).
    if (!strcmp(deviceName, "udpfs") || !strcmp(deviceName, "udpfsbd") || !strcmp(deviceName, "udpbd")) {
        if (sysSyncNeutrinoUdpfsToml(neutrinoPath, deviceName) < 0) {
            host::ui().warningId(_STR_NEUTRINO_TOML_SYNC_FAILED, 6);
            return -1;
        }
    }
    return 0;
}

// sysRunNeutrinoLaunch's preflight result when the user backed out of the argument preview: the
// launch stops, but nothing is wrong with the arguments, so no overflow warning follows.
#define NEUTRINO_LAUNCH_DECLINED -2

// One readable line for the launch preview: entries joined by single spaces, and an entry that
// itself contains a space or tab (a spaced ISO or VMC path) wrapped in quotes, so it still reads as
// the ONE argument Neutrino receives. The budget check has already capped the strings at 256 bytes.
static void sysFormatNeutrinoArgv(char *out, int outSize, int argc, char *const *argv)
{
    int len = 0;
    int i;

    if (out == NULL || outSize <= 0)
        return;
    out[0] = '\0';
    for (i = 0; i < argc && len < outSize - 1; i++) {
        int quote = strchr(argv[i], ' ') != NULL || strchr(argv[i], '\t') != NULL;
        int n = snprintf(out + len, outSize - len, "%s%s%s%s", i ? " " : "", quote ? "\"" : "", argv[i], quote ? "\"" : "");
        if (n < 0)
            break;
        len += n;
    }
}

// Neutrino cheats. Neutrino reads PS2RD codes from a config file's eecore.cheats array (rickgaiser/neutrino
// 0469cc31; the bundled build has it, v1.8.0 itself does not), so a launch with cheats writes one into
// Neutrino's folder and names it with -cfg=. Set pre-teardown by sysNeutrinoHandCheats, read by
// sysRunNeutrinoLaunch for its preflight and its real run alike, so both compose the same argv.
#define NEUTRINO_CHEAT_CFG "riptopl-cheats"

static char sysNeutrinoCheatArg[24];     // "-cfg=riptopl-cheats", or "" for no cheats
static char sysNeutrinoCheatStartup[32]; // the game that file was written for

// The value of the last ACTIVE `flag` (a key prefix such as "-cwd=") in the global then the per-game
// args, read the way Neutrino reads them: last one wins, a $-prefixed token is switched off and --b
// ends the options in each scope. 1 = found.
static int neutrinoArgActiveValue(const char *extraArgs, const char *flag, char *out, int outSize)
{
    const char *sources[2] = {gNeutrinoArgs, extraArgs};
    int flen = (int)strlen(flag), found = 0;

    for (int s = 0; s < 2; s++) {
        for (const char *p = sources[s]; p != NULL && *p != '\0';) {
            while (*p == ' ' || *p == '\t')
                p++;
            const char *end = p;
            while (*end != '\0' && *end != ' ' && *end != '\t')
                end++;
            int len = (int)(end - p);
            if (len == 3 && !strncmp(p, "--b", 3))
                break;
            if (len > flen && !strncmp(p, flag, flen)) {
                snprintf(out, outSize, "%.*s", len - flen, p + flen);
                found = 1;
            }
            p = end;
        }
    }
    return found;
}

// Where Neutrino will look for -cfg=NEUTRINO_CHEAT_CFG: config/ under its working directory -- the -cwd=
// the user forwards, else the folder holding neutrino.elf, which is what sysRunNeutrinoLaunch passes --
// or that directory itself in the flat layout, which Neutrino falls back to when config/system.toml is
// absent. 0 = built; -1 = no directory to anchor on.
static int sysNeutrinoCheatPath(const char *neutrinoPath, const char *extraArgs, char *out, int outSize)
{
    char dir[256], probe[300];
    char sep = '/';
    int fd, len;

    if (neutrinoArgActiveValue(extraArgs, "-cwd=", dir, sizeof(dir))) {
        len = (int)strlen(dir);
        if (len == 0)
            return -1;
        if (strchr(dir, '\\') != NULL)
            sep = '\\';
        if (dir[len - 1] != '/' && dir[len - 1] != '\\' && dir[len - 1] != ':' && len + 1 < (int)sizeof(dir)) {
            dir[len++] = sep;
            dir[len] = '\0';
        }
    } else {
        const char *slash = sysLastPathSeparator(neutrinoPath);
        if (slash == NULL)
            return -1;
        snprintf(dir, sizeof(dir), "%.*s", (int)(slash - neutrinoPath) + 1, neutrinoPath);
        sep = *slash;
    }
    // hddN: is the raw APA namespace, where O_CREAT CREATES A PARTITION (config.c's configPathIsRawApa
    // guards its own writes the same way). A custom Neutrino path can name it; never open anything
    // there -- only mounted filesystems (pfsN:, massN:, mcN:, ...) take the file.
    if (!strncmp(dir, "hdd", 3) && dir[3] >= '0' && dir[3] <= '9' && dir[4] == ':')
        return -1;

    snprintf(probe, sizeof(probe), "%sconfig%csystem.toml", dir, sep);
    fd = open(probe, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        snprintf(out, outSize, "%sconfig%c%s.toml", dir, sep, NEUTRINO_CHEAT_CFG);
    } else
        snprintf(out, outSize, "%s%s.toml", dir, NEUTRINO_CHEAT_CFG);
    return 0;
}

// A -cfg name we can safely quote into a TOML string: no quote, backslash or control character.
static int sysNeutrinoCfgNameSafe(const char *name)
{
    for (; *name != '\0'; name++) {
        if (*name == '"' || *name == '\\' || (unsigned char)*name < 0x20)
            return 0;
    }
    return 1;
}

// The enabled codes as a Neutrino config file. `depends` keeps a -cfg the user forwards: Neutrino reads
// only the last -cfg -- this one -- and loads theirs through it. 0 = written in full.
static int sysWriteNeutrinoCheats(const char *path, const char *startup, const char *userCfg, const u32 *list, int pairs)
{
    int size = 512 + pairs * 32, len, fd, written, i;
    char *text = static_cast<char *>(malloc(size));

    if (text == NULL)
        return -1;
    len = snprintf(text, size,
                   "# Written by RiptOPL for every Neutrino launch with cheats on: the game's enabled\n"
                   "# PS2RD codes. Rewritten each time; safe to delete.\n"
                   "name = \"RiptOPL cheats for %s\"\n",
                   startup);
    if (userCfg[0] != '\0')
        len += snprintf(text + len, size - len, "depends = [\"%s\"]\n", userCfg);
    len += snprintf(text + len, size - len, "eecore.cheats = [\n");
    for (i = 0; i < pairs; i++)
        len += snprintf(text + len, size - len, "    0x%08X, 0x%08X,\n", (unsigned int)list[i * 2], (unsigned int)list[i * 2 + 1]);
    len += snprintf(text + len, size - len, "]\n");

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        free(text);
        return -1;
    }
    written = write(fd, text, len);
    close(fd);
    free(text);
    if (written != len) {
        unlink(path); // never leave half a list where a later launch could name it
        return -1;
    }
    return 0;
}

int sysNeutrinoHandCheats(const char *startup, const char *neutrinoPath, const char *extraArgs, const u32 *list, int pairs)
{
    char path[320], userCfg[64];

    sysNeutrinoCheatArg[0] = '\0';
    sysNeutrinoCheatStartup[0] = '\0';
    if (startup == NULL || neutrinoPath == NULL || list == NULL || pairs <= 0)
        return 0;

    if (!neutrinoArgActiveValue(extraArgs, "-cfg=", userCfg, sizeof(userCfg)) || !strcmp(userCfg, NEUTRINO_CHEAT_CFG) ||
        !sysNeutrinoCfgNameSafe(userCfg))
        userCfg[0] = '\0';
    if (sysNeutrinoCheatPath(neutrinoPath, extraArgs, path, sizeof(path)) < 0 ||
        sysWriteNeutrinoCheats(path, startup, userCfg, list, pairs) < 0) {
        LOG("[NEUTRINO] cheats for %s could not be written next to %s\n", startup, neutrinoPath);
        // A read-only share or a full card: an autolaunch has no one to ask and goes on without them.
        if (!host::ui().active())
            return 0;
        return host::ui().confirmId(_STR_NEUTRINO_CHEATS_WRITE_FAILED, NULL) ? 0 : -1;
    }
    snprintf(sysNeutrinoCheatArg, sizeof(sysNeutrinoCheatArg), "-cfg=%s", NEUTRINO_CHEAT_CFG);
    snprintf(sysNeutrinoCheatStartup, sizeof(sysNeutrinoCheatStartup), "%s", startup);
    LOG("[NEUTRINO] %d cheat code(s) for %s -> %s\n", pairs, startup, path);
    return 0;
}

#ifdef RETROACHIEVEMENTS
// RA through Neutrino: the RA kit's neutrino/ is ORBIT's runtime (third_party/orbit-neutrino), whose
// raagent.irx speaks xeRAbora's wire protocol. sbNeutrinoPrepareRA decides whether a game is tracked;
// sysNeutrinoHandRA writes its files and sysRunNeutrinoLaunch appends -cfg=ript-ra and this argument.
static char sysNeutrinoRAArg[336], sysNeutrinoRAStartup[32];

// The effective -cwd, user overrides included, else the folder holding neutrino.elf; with a trailing '/'.
// 0 = usable; -1 = none, or one we must not write under (hddN: is raw APA, where O_CREAT makes a partition).
static int sysNeutrinoRADir(const char *neutrinoPath, const char *extraArgs, char *dir, int size)
{
    if (!neutrinoArgActiveValue(extraArgs, "-cwd=", dir, size)) {
        const char *slash = sysLastPathSeparator(neutrinoPath);
        if (slash == NULL || slash - neutrinoPath + 1 >= size)
            return -1;
        snprintf(dir, size, "%.*s", (int)(slash - neutrinoPath) + 1, neutrinoPath);
    }
    int len = (int)strlen(dir);
    if (len == 0 || len + 1 >= size || strchr(dir, '\\') != NULL || strchr(dir, '"') != NULL ||
        (!strncmp(dir, "hdd", 3) && dir[3] >= '0' && dir[3] <= '9' && dir[4] == ':'))
        return -1;
    if (dir[len - 1] != '/' && dir[len - 1] != ':')
        strcpy(dir + len, "/");
    return 0;
}

int sysNeutrinoRARuntime(const char *neutrinoPath, const char *extraArgs)
{
    char dir[256], path[300], text[40];
    int fd, len;

    if (neutrinoPath == NULL || sysNeutrinoRADir(neutrinoPath, extraArgs, dir, sizeof(dir)) < 0)
        return 0;
    // Only the RA kit's runtime carries this marker: a stock Neutrino is never sent unknown options.
    snprintf(path, sizeof(path), "%sORBIT-RA.txt", dir);
    if ((fd = open(path, O_RDONLY)) < 0)
        return 0;
    len = read(fd, text, sizeof(text));
    close(fd);
    if (len < 28 || memcmp(text, "RiptOPL ORBIT RA handoff v1\n", 28))
        return 0;
    snprintf(path, sizeof(path), "%smodules/raagent.irx", dir);
    if ((fd = open(path, O_RDONLY)) < 0)
        return 0;
    close(fd);
    return 1;
}

static int sysNeutrinoRAWrite(const char *path, const void *data, int size)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return -1;
    int written = write(fd, data, size);
    if (close(fd) < 0 || written != size) {
        unlink(path);
        return -1;
    }
    return 0;
}

int sysNeutrinoHandRA(const char *startup, const char *neutrinoPath, const char *extraArgs, int networkGame,
                      const unsigned char *ip, const unsigned char *peer, const void *watch, int size)
{
    char dir[256], config[320], path[340], userCfg[64], text[1024];
    int len;

    sysNeutrinoRAArg[0] = '\0';
    sysNeutrinoRAStartup[0] = '\0';
    if (startup == NULL)
        return 0;
    // Next to the cheats file: config/ under the working directory, or the directory itself.
    if (sysNeutrinoRADir(neutrinoPath, extraArgs, dir, sizeof(dir)) < 0 ||
        sysNeutrinoCheatPath(neutrinoPath, extraArgs, config, sizeof(config)) < 0)
        return -1;
    config[strlen(config) - strlen(NEUTRINO_CHEAT_CFG ".toml")] = '\0'; // keep its directory
    if (snprintf(path, sizeof(path), "%sript-ra.wl", config) >= (int)sizeof(path) || sysNeutrinoRAWrite(path, watch, size) < 0)
        return -1;
    // ript-ra is the -cfg Neutrino keeps (the last); it loads the cheats file or the user's own through depends.
    if (sysNeutrinoCheatArg[0] && !strcmp(sysNeutrinoCheatStartup, startup))
        snprintf(userCfg, sizeof(userCfg), "%s", NEUTRINO_CHEAT_CFG);
    else if (!neutrinoArgActiveValue(extraArgs, "-cfg=", userCfg, sizeof(userCfg)))
        userCfg[0] = '\0';
    if (!sysNeutrinoCfgNameSafe(userCfg) || !strcmp(userCfg, "ript-ra"))
        return -1;
    len = snprintf(text, sizeof(text), "# Written by RiptOPL for an RA launch through ORBIT Neutrino. Safe to delete.\n"
                                       "name = \"RiptOPL RA\"\ndepends = [%s%s%s%s%s]\n",
                   networkGame ? "" : "\"i_dev9_hidden\"", userCfg[0] && !networkGame ? ", " : "",
                   userCfg[0] ? "\"" : "", userCfg, userCfg[0] ? "\"" : "");
    // A network game's own transport already loads SMAP and the stack.
    if (!networkGame)
        len += snprintf(text + len, sizeof(text) - len,
                        "[[module]]\nfile = \"smap.irx\"\nenv = [\"EE\"]\n"
                        "[[module]]\nfile = \"ministack.irx\"\nargs = [\"ip=%u.%u.%u.%u\"]\nenv = [\"EE\"]\n",
                        ip[0], ip[1], ip[2], ip[3]);
    len += snprintf(text + len, sizeof(text) - len,
                    "[[module]]\nfile = \"raagent.irx\"\nargs = [\"srv=%u.%u.%u.%u\", \"me=%u.%u.%u.%u\"]\nenv = [\"EE\"]\nload_order = 30\n",
                    peer[0], peer[1], peer[2], peer[3], ip[0], ip[1], ip[2], ip[3]);
    if (snprintf(path, sizeof(path), "%sript-ra.toml", config) >= (int)sizeof(path) || sysNeutrinoRAWrite(path, text, len) < 0)
        return -1;
    snprintf(sysNeutrinoRAArg, sizeof(sysNeutrinoRAArg), "-ra=%sript-ra.wl", config);
    snprintf(sysNeutrinoRAStartup, sizeof(sysNeutrinoRAStartup), "%s", startup);
    return 0;
}
#endif

// Settings > Game Launching > Show Launch Arguments: before the teardown, show the exact argv this
// launch will hand to Neutrino -- the array sysRunNeutrinoLaunch just composed, not a reconstruction --
// and let the user back out. Autolaunch has no menu to answer it, so it launches as configured.
// Returns 1 = launch, 0 = the user chose Back.
static int sysNeutrinoConfirmArgv(int argc, char *const *argv)
{
    char joined[384];

    if (!gNeutrinoShowArgs || !host::ui().active())
        return 1;
    sysFormatNeutrinoArgv(joined, sizeof(joined), argc, argv);
    return host::ui().confirmId(_STR_NEUTRINO_ARGS_CONFIRM, joined);
}

static int sysRunNeutrinoLaunch(const char *driver, const char *path, const char *startup, int compatmask, int EnablePS2Logo, const char *neutrinoPath, const char *extraArgs, int neutrinoVideo, int neutrinoGsmComp, int neutrinoBsdfs, int bdDevNr, const neutrino_vmc_args_t *vmcArgs, int checkOnly)
{
    if (neutrinoPath == NULL || driver == NULL || path == NULL) {
        LOG("[NEUTRINO] null arg, abort\n");
        if (!checkOnly)
            launchDiagRefuse(LAUNCHDIAG_REFUSE_ARGS); // blink code 1
        return -1;
    }

    const char *deviceName = getDeviceName(driver);
    if (!strcmp(deviceName, "unsupported")) {
        LOG("[NEUTRINO] unsupported device '%s', abort\n", driver);
        if (!checkOnly)
            launchDiagRefuse(LAUNCHDIAG_REFUSE_ARGS); // blink code 1
        return -1;
    }

    // HW-confirmed (issue #56, AndrewBento, PSXMemCard Gen2): Neutrino's -logo on the mmce backend
    // black-screens GAME-DEPENDENTLY ("depends a bit on luck which game") -- and the identical
    // failure reproduces on NHDDL, so it is a Neutrino/backend interaction, not this launcher. The
    // mechanism (neutrino ee_core main.c): -logo routes the boot through rom0:PS2LOGO, and ee_core's
    // extra LoadExecPS2 pass reboots the IOP (New_Reset_Iop2) before PS2LOGO runs, dropping the
    // resident stack a keep-IOP backend reads the game through. udpfs/udpfsbd/udpbd are that same
    // keep-IOP class (HW, nuno6573: udpfs lists fine but every launch black-screens with the global
    // logo toggle on), so the GLOBAL PS2-Logo toggle is suppressed for all three until that is
    // fixed upstream. Keyed on deviceName, not driver: the udpfs BLOCK leg arrives here as driver
    // "udp" and only resolves to "udpfsbd" above (the udpbd leg resolves to "udpbd" by protocol).
    // Parity: NHDDL's logo toggle defaults off and wLaunchELF has no logo path at all, so neither
    // sends -logo in practice. Deliberate escape hatch: a user-supplied "-logo" in the global or
    // per-game Neutrino args still passes through the tokenizer untouched.
    if (EnablePS2Logo &&
        (!strcmp(deviceName, "mmce") || !strcmp(deviceName, "udpfs") || !strcmp(deviceName, "udpfsbd") ||
         !strcmp(deviceName, "udpbd") || !strcmp(deviceName, "dvr"))) {
        LOG("[NEUTRINO] -logo suppressed on %s (issue #56 class: PS2LOGO pass reboots the IOP)\n", deviceName);
        EnablePS2Logo = 0;
    }

    char bsd[16];
    char bsdfs[16];
    char filePath[288];        // "-dvd=hdl:" (9) + up to a 255-char path + NUL — avoid truncation (B2)
    char cwdArg[288];          // "-cwd=" + the neutrino.elf install dir (auto; Neutrino finds its config/modules there)
    char compatModes[32] = ""; // stays empty when no compat modes are forwarded (B1)
    char globalArgsBuf[256];   // mutable copy of gNeutrinoArgs for tokenizing (tokens point in)
    char extraArgsBuf[256];    // mutable copy of the per-game extraArgs for tokenizing
    char *argv[32];            // compose beyond the kernel budget so overflow is detected, never silently truncated
    int argc = 0;
    // 14, NOT sizeof(argv): the kernel args area holds at most 15 strings per ExecPS2 hop (ps2sdk
    // exit.c SetArg, SETARG_MAX_ARGS 15 -- and ExecPS2 forwards the UNCLAMPED count, so overflowing
    // makes the kernel read string bytes as argv pointers). Hop 1 prepends the elfldr child's load
    // path, spending one slot, so the target argv must stay <= 14 entries.
    const int argvMax = sizeof(argv) / sizeof(argv[0]);

    // Target argv[0] = neutrino.elf's own path (NHDDL convention). sysLoadELFKeepIOP forwards
    // argv verbatim -- argv[0] included -- so this must be supplied explicitly here (POPSTARTER
    // launches use the same loader with a selector as argv[0] instead).
    if (argc < argvMax)
        argv[argc++] = (char *)neutrinoPath;

    if (!strcmp(deviceName, "apa")) {
        snprintf(bsd, sizeof(bsd), "-bsd=ata");
        if (argc < argvMax)
            argv[argc++] = bsd;
        snprintf(bsdfs, sizeof(bsdfs), "-bsdfs=hdl");
        if (argc < argvMax)
            argv[argc++] = bsdfs;
        snprintf(filePath, sizeof(filePath), "-dvd=hdl:%s", path);
        if (argc < argvMax)
            argv[argc++] = filePath;
    } else {
        snprintf(bsd, sizeof(bsd), "-bsd=%s", deviceName);
        if (argc < argvMax)
            argv[argc++] = bsd;
        // Per-game -bsdfs override (parity-audit #11), Auto(0) = today's bytes exactly (no -bsdfs,
        // bare -dvd). Valid driver values are ONLY exfat/hdl/bd; the -dvd prefix changes in lockstep
        // (verified vs rickgaiser/neutrino main.c usage: "-bsdfs=hdl -dvd=hdl:..." and
        // "-bsdfs=bd -dvd=bdfs:..."; exfat is the default fs and takes the bare path unchanged --
        // any ':'-bearing -dvd value is file-mode, opened through the selected fs). NEVER emitted
        // for mmce/udpfs: they are fileid backends with no filesystem layer (Neutrino forces
        // sBSDFS="no" there regardless -- this guard just keeps the argv clean). A user-typed
        // -bsdfs= wins over the picker, and -dvd is shaped for it: a typed bd with our bare file
        // path could never open (CodeRabbit, #762). We emit the effective value OURSELVES even when
        // it was typed: this slot is a core arg the budget below never drops, while the user's copy
        // is a tail token it may -- keeping a bdfs: -dvd without its -bsdfs=bd would fail after the
        // teardown. The duplicate agrees with theirs (it IS their last token). A user-typed -dvd=
        // wins on its own because user tokens are appended after these and Neutrino's arg parse is
        // last-wins.
        static const char *const bsdfsTokens[] = {"", "exfat", "hdl", "bd"};
        static const char *const bsdfsDvdPrefix[] = {"", "", "hdl:", "bdfs:"};
        int fsOverride = neutrinoEffectiveBsdfs(deviceName, neutrinoBsdfs, extraArgs);
        if (fsOverride) {
            snprintf(bsdfs, sizeof(bsdfs), "-bsdfs=%s", bsdfsTokens[fsOverride]);
            if (argc < argvMax)
                argv[argc++] = bsdfs;
        }
        if (fsOverride == 3) {
            // bdfs does not take a file path: its open() strcmp's the path against the raw bdm
            // device tuple "<name><devNr>p<parNr>" (neutrino iop/bdfs/src/bdfs.c, and its own usage
            // example "-dvd=bdfs:udp0p0"), so the old "-dvd=bdfs:massN:/..." could NEVER open --
            // setup_dvd_iso failed and the whole neutrino boot silently returned. Compose the tuple
            // from the live driver token and the caller's bdm device number (a second stick on the
            // same driver is usb1, not usb0); partition stays 0. -1 reaches here only for ATA (one
            // synthetic device, so 0 is right); sysNeutrinoPreflight refuses every other driver.
            snprintf(filePath, sizeof(filePath), "-dvd=bdfs:%s%dp0", driver, bdDevNr >= 0 ? bdDevNr : 0);
        } else {
            snprintf(filePath, sizeof(filePath), "-dvd=%s%s", bsdfsDvdPrefix[fsOverride], path);
        }
        if (argc < argvMax)
            argv[argc++] = filePath;

        /* QUICKBOOT IS THE SECOND HALF OF THE KEEP-IOP HANDOFF, and without it the first half is
           self-defeating on USB and iLink. elfldr deliberately does NOT reset the IOP, so Neutrino
           inherits OPL's live iomanX/fileXio/bdm/bdmfs_fatfs and its mounts -- that is the entire
           point. But Neutrino's DEFAULT boot then performs its own IOP reset, which destroys exactly
           what we just preserved, leaving it to re-initialise the block device itself before it can
           reach the ISO.

           Symptom when only the first half ships: the handoff completes (no OSDSYS -- the loader
           did its job) and then the screen stays black, because Neutrino reset away the stack the
           game path depends on. -qb keeps the inherited environment and skips that reset.

           USB, iLink, UDPFS (both filesystem and block variants) and UDPBD: all launch through this
           inherited environment. For UDPFS, an IOP reset destroys the resident ministack/ioman
           state and triggers an immediate secondary DISCOVERY from the same socket that collides
           with active server streams. UDPBD without -qb, on hardware (FatBaldDad, 09-25, PR #756):
           the handoff reached Neutrino, then the link dropped, a NEW client rediscovered the server,
           read the image's MBR and exFAT metadata, never read the game and quit to the browser --
           Neutrino's own reset rebuilt the stack our -dvd path refers to. NHDDL passes -qb for every
           mode except HDL (launcher.c), UDPBD included. Other backends stay on the normal boot path
           until hardware says otherwise. A user-typed -qb in the global or per-game args wins -- do
           not emit a second copy.

           Composed before the budget check so an overfull launch is refused rather than silently
           losing -qb and reproducing the black screen. */
        if ((!strcmp(deviceName, "usb") || !strcmp(deviceName, "ilink") ||
             !strcmp(deviceName, "udpfs") || !strcmp(deviceName, "udpfsbd") || !strcmp(deviceName, "udpbd")) &&
            argc < argvMax &&
            !neutrinoArgHasActiveFlag(gNeutrinoArgs, "-qb") && !neutrinoArgHasActiveFlag(extraArgs, "-qb"))
            argv[argc++] = "-qb";
    }

    // Only forward -gc when at least one compat mode is set: Neutrino treats
    // -gc=0 as an explicit mode (IOP fast reads), NOT a no-op, so passing it for
    // a game with no OPL modes selected would force unwanted behavior (B1).
    int gcModes = convertCompatmaskToModes(compatmask);
    if (gcModes > 0) {
        snprintf(compatModes, sizeof(compatModes), "-gc=%d", gcModes);
        if (argc < argvMax)
            argv[argc++] = compatModes;
    }

    // Global toggles auto-emit -dbc/-logo; the per-game/global args screens now carry them as
    // structured bools too (forwarded via the tokenizer below), so suppress the auto-emit when the
    // user's args already hold an active (non-$-disabled) copy -- one flag, emitted once.
    if (gEnableDebug && argc < argvMax &&
        !neutrinoArgHasActiveFlag(gNeutrinoArgs, "-dbc") && !neutrinoArgHasActiveFlag(extraArgs, "-dbc"))
        argv[argc++] = "-dbc";

    if (EnablePS2Logo && argc < argvMax &&
        !neutrinoArgHasActiveFlag(gNeutrinoArgs, "-logo") && !neutrinoArgHasActiveFlag(extraArgs, "-logo"))
        argv[argc++] = "-logo";

    // Per-game Neutrino video mode (-gsm): 1=fp1 (240p), 2=fp2 (480p), 3=1080ix1 (1080i); 0/out-of-range
    // = no -gsm. Neutrino is LAST-wins on -gsm and ABORTS the boot on a malformed value, so emit exactly
    // ONE: skip the structured token when the user already typed a -gsm into the global or per-game args,
    // letting that explicit value win (precedence confirmed vs rickgaiser/neutrino ee/loader/src/main.c).
    int userHasGsm = neutrinoArgHasActiveFlag(gNeutrinoArgs, "-gsm=") ||
                     neutrinoArgHasActiveFlag(extraArgs, "-gsm=");
    if (neutrinoVideo >= 1 && neutrinoVideo <= 5 && !userHasGsm && argc < argvMax) {
        // -gsm=v:c grammar (neutrino main.c parse_gsm_flags): v in {fp1, fp2, 1080ix1, 1080ix2,
        // 1080ix3}, optional :c in {1,2,3} = field-flipping compatibility type. NEVER emit a bare
        // ":c" -- an unrecognized -gsm value aborts the whole neutrino boot -- so the comp half is
        // ignored unless a video mode is set (the GUI hint says as much).
        static const char *const gsmVideoTokens[] = {"", "fp1", "fp2", "1080ix1", "1080ix2", "1080ix3"};
        static char gsmArg[24]; // outlives argv[] until the ExecPS2 handoff below
        if (neutrinoGsmComp >= 1 && neutrinoGsmComp <= 3)
            snprintf(gsmArg, sizeof(gsmArg), "-gsm=%s:%d", gsmVideoTokens[neutrinoVideo], neutrinoGsmComp);
        else
            snprintf(gsmArg, sizeof(gsmArg), "-gsm=%s", gsmVideoTokens[neutrinoVideo]);
        argv[argc++] = gsmArg;
    }

    // Parity Delta-10, enabled by default via settings_riptopl.cfg "neutrino_elf_arg"=1
    // (exposed in Game Launching): hand neutrino the boot ELF path directly so its per-GameID
    // config/<GameID>.toml compat lookup can resolve pre-reset. Shape-guarded to AAAA_NNN.NN
    // startups and skipped when the user already forwards an -elf= (structured field or free
    // text) -- neutrino must see exactly one.
    static char elfArg[40]; // outlives argv[] until the ExecPS2 handoff
    if (gNeutrinoElfArg && sysStartupShapeOk(startup) && argc < argvMax &&
        !neutrinoArgHasActiveFlag(gNeutrinoArgs, "-elf=") && !neutrinoArgHasActiveFlag(extraArgs, "-elf=")) {
        snprintf(elfArg, sizeof(elfArg), "-elf=cdrom0:\\%s;1", startup);
        argv[argc++] = elfArg;
        LOG("[NEUTRINO] neutrino_elf_arg=1: emitting %s\n", elfArg);
    }

    // VMC slots (#47): emit each configured "-mcN=...bin" as its OWN argv entry. These come from
    // sbBuildVmcNeutrinoArgs (caller storage, alive across the launch) and must NOT pass through the
    // whitespace tokenizer below -- a VMC name with a space would otherwise be split into two args
    // and Neutrino would receive a truncated, unopenable card path (silent no-mount).
    if (vmcArgs != NULL) {
        int slot;
        for (slot = 0; slot < NEUTRINO_VMC_SLOTS; slot++) {
            if (vmcArgs->arg[slot][0] != '\0' && argc < argvMax)
                argv[argc++] = (char *)vmcArgs->arg[slot];
        }
    }

    // Auto -cwd: point Neutrino at the directory holding neutrino.elf so it loads its config / extra
    // modules relative to its install location, UNLESS the user already supplied one (precedence like
    // -gsm above). Without it Neutrino launches with no working dir, so a setup that keeps config next
    // to the ELF would not be found. mc:NEUTRINO/neutrino.elf paths also contain a '/', so this works
    // for memory-card installs too.
    int userHasCwd = neutrinoArgHasActiveFlag(gNeutrinoArgs, "-cwd=") ||
                     neutrinoArgHasActiveFlag(extraArgs, "-cwd=");
    if (!userHasCwd) {
        const char *slash = sysLastPathSeparator(neutrinoPath);
        if (slash != NULL) {
            int dirLen = (int)(slash - neutrinoPath) + 1; // keep the device's native trailing separator
            snprintf(cwdArg, sizeof(cwdArg), "-cwd=%.*s", dirLen, neutrinoPath);
            if (argc < argvMax)
                argv[argc++] = cwdArg;
        }
    }

    // Δ6: the bsd toml ip= sync (network transports) now runs PRE-deinit inside sysNeutrinoPreflight
    // -- called by every device leg before deinitEx -- so a failure aborts to a live menu with a
    // toast instead of dying invisibly here, post-teardown. Nothing to do at this point.

    // Append user-supplied Neutrino flags: global defaults first, then the per-game
    // string (so a game can extend the global set). Both are tokenized on whitespace.
    int globalBreak = 0, gameBreak = 0;
    argc = appendArgTokens(argv, argc, argvMax, globalArgsBuf, sizeof(globalArgsBuf), gNeutrinoArgs, 0, &globalBreak);
    argc = appendArgTokens(argv, argc, argvMax, extraArgsBuf, sizeof(extraArgsBuf), extraArgs, 0, &gameBreak);
    // Cheats (sysNeutrinoHandCheats): after the user's own options, so the -cfg Neutrino keeps -- its
    // last -- is ours, which loads theirs through `depends`. Only for the game the file was written for.
    if (sysNeutrinoCheatArg[0] != '\0' && startup != NULL && !strcmp(startup, sysNeutrinoCheatStartup)) {
        if (argc >= argvMax)
            return -1; // refuse in the preflight rather than boot without the cheats the user enabled
        argv[argc++] = sysNeutrinoCheatArg;
    }
#ifdef RETROACHIEVEMENTS
    static char raCfgArg[] = "-cfg=ript-ra";
    if (sysNeutrinoRAArg[0] != '\0' && startup != NULL && !strcmp(startup, sysNeutrinoRAStartup)) {
        if (argc + 2 > argvMax)
            return -1;
        argv[argc++] = raCfgArg;
        argv[argc++] = sysNeutrinoRAArg;
    }
#endif
    static char breakArg[] = "--b"; // writable: argv is char *, and a literal would add a -Wwrite-strings warning
    if ((globalBreak || gameBreak) && argc < argvMax)
        argv[argc++] = breakArg;
    argc = appendArgTokens(argv, argc, argvMax, globalArgsBuf, sizeof(globalArgsBuf), gNeutrinoArgs, 1, &globalBreak);
    argc = appendArgTokens(argv, argc, argvMax, extraArgsBuf, sizeof(extraArgsBuf), extraArgs, 1, &gameBreak);
    if (gLaunchDiag && !checkOnly)
        launchDiagMark(9); // argv composed -- the budget check below can still refuse (blink code 2)

    // ExecPS2 argv BYTE budget (verified vs ps2sdk exit.c SetArg + the crt0 args struct): each hop
    // carries its strings in ONE 256-byte pool, every NUL included. Hop 1 packs the child's load
    // path PLUS this argv -- neutrinoPath is counted TWICE (loadpath and target argv[0]). SetArg's
    // copy is UNbounded, so exceeding the pool corrupts rather than truncates. Refuse the whole
    // launch before teardown when preflighting; never silently discard a requested option.
    {
        int pool = (int)strlen(neutrinoPath) + 1; // hop-1 child argv[0] = the load path
        int i;
        for (i = 0; i < argc; i++)
            pool += (int)strlen(argv[i]) + 1;
        if (argc > 14 || pool > 256) {
            LOG("[NEUTRINO] argv exceeds launch budget: %d entries (14 max), %d bytes (256 max)\n", argc, pool);
            if (!checkOnly)
                launchDiagRefuse(LAUNCHDIAG_REFUSE_CORE_ARGV); // blink code 2 if the preflight was bypassed
            return -1;
        }
    }

    if (checkOnly)
        return sysNeutrinoConfirmArgv(argc, argv) ? 0 : NEUTRINO_LAUNCH_DECLINED;

    // Log the FULL argv (not just bsd/dvd/compat) so the VMC -mc args are verifiable on hardware (#47).
    LOG("[NEUTRINO] elf=%s argc=%d\n", neutrinoPath, argc);
    {
        int i;
        for (i = 0; i < argc; i++)
            LOG("[NEUTRINO]   argv[%d]=%s\n", i, argv[i]);
    }

    // Hand off WITHOUT the elf-loader IOP reset (NHDDL parity, vendored elfldr/): Neutrino reads
    // its config/modules (-cwd) and opens the game ISO through OUR still-mounted devices, and only
    // then does its own IOP reset -- the old resetting handoff left it a bare SIO2MAN/MCMAN/MCSERV
    // IOP, so any USB/BDM-hosted neutrino.elf setup black-screened to OSDSYS. The callers keep both
    // the game device and the neutrino.elf device mounted (deinitEx).
    if (sysLoadELFKeepIOP(neutrinoPath, "", argc, argv) < 0)
        LOG("[NEUTRINO] keep-IOP handoff failed for %s\n", neutrinoPath);
    return 0;
}

int sysNeutrinoArgsPreflight(const char *driver, const char *path, const char *startup, int compatmask, int EnablePS2Logo, const char *neutrinoPath, const char *extraArgs, int neutrinoVideo, int neutrinoGsmComp, int neutrinoBsdfs, int bdDevNr, const neutrino_vmc_args_t *vmcArgs)
{
    int result = sysRunNeutrinoLaunch(driver, path, startup, compatmask, EnablePS2Logo, neutrinoPath, extraArgs, neutrinoVideo, neutrinoGsmComp, neutrinoBsdfs, bdDevNr, vmcArgs, 1);
#ifdef RETROACHIEVEMENTS
    // The RA handoff's two arguments must never cost the player the game: when they push the argv over
    // the launch budget, launch without them. Warn only when they were the cause.
    if (result < 0 && result != NEUTRINO_LAUNCH_DECLINED && sysNeutrinoRAArg[0] != '\0' && startup != NULL &&
        !strcmp(startup, sysNeutrinoRAStartup)) {
        sysNeutrinoRAArg[0] = '\0';
        sysNeutrinoRAStartup[0] = '\0';
        result = sysRunNeutrinoLaunch(driver, path, startup, compatmask, EnablePS2Logo, neutrinoPath, extraArgs, neutrinoVideo, neutrinoGsmComp, neutrinoBsdfs, bdDevNr, vmcArgs, 1);
        if (result == 0) { // declined on the preview: nothing launches, so nothing to warn about
            LOG("[RA] handoff arguments exceed the launch budget for %s; launching untracked\n", startup);
            host::ui().warningId(_STR_RA_NEUTRINO_UNTRACKED, 6);
        }
    }
#endif
    if (result == NEUTRINO_LAUNCH_DECLINED)
        return -1; // Back on the argument preview: stay in the menu, nothing to warn about
    if (result < 0) {
        host::ui().warningId(_STR_NEUTRINO_LAUNCH_ARGS_OVERFLOW, 6);
        return -1;
    }
    return 0;
}

void sysLaunchNeutrino(const char *driver, const char *path, const char *startup, int compatmask, int EnablePS2Logo, const char *neutrinoPath, const char *extraArgs, int neutrinoVideo, int neutrinoGsmComp, int neutrinoBsdfs, int bdDevNr, const neutrino_vmc_args_t *vmcArgs)
{
    diagLog("launch: Neutrino %s, %s %s (%s)", neutrinoPath != NULL ? neutrinoPath : "?", driver != NULL ? driver : "?",
            path != NULL ? path : "?", startup != NULL ? startup : "?");
    sysRunNeutrinoLaunch(driver, path, startup, compatmask, EnablePS2Logo, neutrinoPath, extraArgs, neutrinoVideo, neutrinoGsmComp, neutrinoBsdfs, bdDevNr, vmcArgs, 0);
}
