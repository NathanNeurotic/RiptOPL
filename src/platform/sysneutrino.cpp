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
#include "include/ps2sdkapi_compat.h"       // ps2sdk_get_iop_fd -- the ioctls above take the IOP-side fd

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
    if (!strcmp(deviceName, "mmce") || !strcmp(deviceName, "udpfs"))
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
    len = read(fd, toml, sizeof(toml) - 1);
    close(fd);
    if (len <= 0)
        return -1; // unreadable/empty bsd toml -> same guaranteed failure
    if (len >= (int)sizeof(toml) - 1) {
        LOG("[NEUTRINO] %s larger than %d -- ip= left as-is\n", tomlPath, (int)sizeof(toml));
        return 0; // exotic hand-built toml: proceed on the old hand-edit contract
    }
    toml[len] = '\0';

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
    if ((int)strlen(newIp) == (int)(valEnd - valStart) && !strncmp(valStart, newIp, strlen(newIp)))
        return 0; // already in sync -- don't touch the file

    snprintf(updated, sizeof(updated), "%.*s%s%s", (int)(valStart - toml), toml, newIp, valEnd);

    fd = open(tomlPath, O_WRONLY | O_TRUNC);
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
        fd = open(tomlPath, O_WRONLY | O_TRUNC);
        if (fd >= 0) {
            write(fd, toml, len);
            close(fd);
        }
        return -1; // the OLD ip survives -> the boot would black-screen; abort while GUI is alive
    }
    LOG("[NEUTRINO] synced %s ip= -> %s\n", tomlPath, newIp);
    return 0;
}

// Δ6 (NHDDL parity): everything that can FAIL a Neutrino launch and is checkable pre-teardown runs
// HERE, called by every device leg BEFORE deinitEx -- the GUI is alive for a toast and nothing has
// been torn down, so a failure is "stay in the menu", not a post-teardown black screen. Returns
// 0 = proceed; <0 = abort (a toast was shown). neutrinoBsdfs/extraArgs/bdDevNr are what the leg will
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
         !strcmp(deviceName, "udpbd"))) {
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
        return 0;

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
    if (sysRunNeutrinoLaunch(driver, path, startup, compatmask, EnablePS2Logo, neutrinoPath, extraArgs, neutrinoVideo, neutrinoGsmComp, neutrinoBsdfs, bdDevNr, vmcArgs, 1) < 0) {
        host::ui().warningId(_STR_NEUTRINO_LAUNCH_ARGS_OVERFLOW, 6);
        return -1;
    }
    return 0;
}

void sysLaunchNeutrino(const char *driver, const char *path, const char *startup, int compatmask, int EnablePS2Logo, const char *neutrinoPath, const char *extraArgs, int neutrinoVideo, int neutrinoGsmComp, int neutrinoBsdfs, int bdDevNr, const neutrino_vmc_args_t *vmcArgs)
{
    sysRunNeutrinoLaunch(driver, path, startup, compatmask, EnablePS2Logo, neutrinoPath, extraArgs, neutrinoVideo, neutrinoGsmComp, neutrinoBsdfs, bdDevNr, vmcArgs, 0);
}
