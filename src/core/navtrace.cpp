// Opt-in, single-session investigation ELF. No network or per-poll file logging.
#include "include/navtrace.h"

#ifdef OPL_NAV_INVESTIGATION
#include "include/navgapguard.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NAVTRACE_HOST
#include "include/opl.h"
#include "include/config.h"
#include "include/diaglog.h"
#include "include/fntsys.h"
#include "include/gui.h"
#include "include/guiint.h"
#include "include/renderman.h"
#include "include/themes.h"
#include <kernel.h>
#endif

namespace
{
using U = unsigned int;
struct Event
{
    U us, sequence, kind, a, b, c, d, e;
};
constexpr U RECORDS = 8192;
static Event events[RECORDS]; // 256 KiB, .bss; bounded, no capture-time allocations.
static U head, total, sequence, nowUs, previousUs, frameDelta, rawButtons;
static U idleSince, burstSince, lastAttempt, captures, currentPage;
static U work[7];
static int lastSource = -999, lastRow = -999;
static bool dirty, idleTracking, candidate;
static volatile int saving;
static NavigationGapGuard guard;
static char savedPath[300], status[340] = "Recreate 3-5 times; rest 5 seconds after each";
struct Path
{
    U hash;
    char text[112];
};
static Path paths[32];
static U pathCount;

static U microseconds()
{
    return (U)((unsigned long long)clock() * 1000000ULL / CLOCKS_PER_SEC);
}

static void record(U kind, U a = 0, U b = 0, U c = 0, U d = 0, U e = 0)
{
    if (saving)
        return;
    U at = microseconds();
    int on = DIntr();
    if (!saving) {
        events[head] = {at, ++sequence, kind, a, b, c, d, e};
        head = (head + 1) % RECORDS;
        total++;
    }
    if (on)
        EIntr();
}

static U pathHash(const char *path)
{
    if (path == NULL)
        return 0;
    U h = 2166136261U;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        h = (h ^ *p) * 16777619U;
    int on = DIntr();
    if (!saving) {
        U i;
        for (i = 0; i < pathCount && paths[i].hash != h; ++i) {}
        if (i == pathCount && pathCount < 32) {
            paths[i].hash = h;
            U j = 0;
            for (; j + 1 < sizeof(paths[i].text) && path[j]; ++j)
                paths[i].text[j] = path[j];
            paths[i].text[j] = 0;
            pathCount++;
        }
    }
    if (on)
        EIntr();
    return h;
}

static bool writeAll(int fd, const char *bytes, unsigned int size)
{
    while (size) {
        int n = write(fd, bytes, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0 || (U)n > size)
            return false;
        bytes += n;
        size -= n;
    }
    return true;
}

static bool text(int fd, const char *line) { return writeAll(fd, line, strlen(line)); }

static bool exportTo(const char *directory, U count, U start, bool forced)
{
    if (directory == NULL || directory[0] == 0)
        return false;
    char path[300], line[4096];
    size_t n = strlen(directory);
    int size = snprintf(path, sizeof(path), "%s%sNAVTRACE.TXT", directory,
                        directory[n - 1] == '/' || directory[n - 1] == ':' ? "" : "/");
    if (size < 0 || size >= (int)sizeof(path))
        return false;
    int fd = open(path, O_WRONLY | O_CREAT, 0666);
    if (fd < 0)
        return false;
    bool ok = lseek(fd, 0, SEEK_END) >= 0;
    snprintf(line, sizeof(line),
             "\r\nBEGIN NAVTRACE capture=%u mode=%s build=%s at_us=%u forced=%d\r\n"
             "boot=%s settings=%s page=%u source=%d row=%d\r\n"
             "art=%d bgart=%d saveicons=%d rumble=%d sfx=%d bgm=%d mx4sio=%d\r\n"
             "ring_events=%u total_events=%u wrapped=%d guard_max_us=%u\r\n"
             "Rows: us seq kind a b c d e (values after kind are hexadecimal)\r\n"
             "kind 1 pad: port|slot<<8|SDKstate<<16|length<<24, mode|raw_active_low_buttons<<16, DMA88, DMA216, index\r\n"
             "kind 2 frame: old, raw_merged, delivered, unknown_nav, repeat_elapsed_units\r\n"
             "kind 3 key: key_id, edge(1)/repeat(0), timer_before_reset, 0, 0\r\n"
             "kind 4 work: category, begin(1)/end(0), path_hash, result, active_count\r\n"
             "work categories: 1 art, 2 hidden, 3 SaveIcon, 4 InfoCFG/size, 5 SFX RPC, 6 actuator RPC\r\n"
             "kind 5 selection: page, source, row, 0, 0\r\n"
             "kind 6 own export end: captures, success, export_duration_us, 0, 0\r\n"
             "DMA88/216 are the two SDK new-buffer frame words; nonzero read length does not establish freshness.\r\n",
             captures + 1, candidate ? "B-gap-guard" : "A-normal", OPL_VERSION, nowUs, forced,
             gBootDir, configGetHomePath(), currentPage, lastSource, lastRow,
             gEnableArt, gEnableBGArt, gEnableSaveIcons, gEnableRumble, gEnableSFX, gEnableBGM, gEnableMX4SIO,
             count, total, total > RECORDS, NavigationGapGuard::MAX_GAP_US);
    ok = ok && text(fd, line);
    for (U i = 0; ok && i < pathCount; ++i) {
        snprintf(line, sizeof(line), "path %08x %s\r\n", paths[i].hash, paths[i].text);
        ok = text(fd, line);
    }
    for (U i = 0; ok && i < count; ++i) {
        const auto &r = events[(start + i) % RECORDS];
        int length = snprintf(line, sizeof(line), "%u %u %u %08x %08x %08x %08x %08x\r\n",
                              r.us, r.sequence, r.kind, r.a, r.b, r.c, r.d, r.e);
        ok = length > 0 && length < (int)sizeof(line) && writeAll(fd, line, length);
    }
    static char diag[16 * 1024];
    int wrapped = 0, diagLen = diagCopy(diag, sizeof(diag), &wrapped);
    ok = ok && text(fd, "DIAG RAM LOG (may contain earlier events):\r\n") &&
         writeAll(fd, diag, diagLen) && text(fd, "\r\nEND NAVTRACE complete=1\r\n");
    if (close(fd) < 0)
        ok = false;
    if (ok)
        snprintf(savedPath, sizeof(savedPath), "%s", path);
    return ok;
}
} // namespace

void navTraceBegin()
{
    nowUs = microseconds();
    frameDelta = nowUs - previousUs;
    previousUs = nowUs;
    rawButtons = 0;
    guard.begin(candidate, nowUs);
}

void navTracePad(U index, int port, int slot, int state, int length, int mode, U buttons, const void *dma)
{
    if (state == 0 && length == 0)
        return;                      // unused stable-disconnected slots do not fill the ring.
    const U *words = (const U *)dma; // padRead has already synchronized this DMA buffer.
    record(1, (port & 255) | ((slot & 255) << 8) | ((state & 255) << 16) | ((length & 255) << 24),
           (mode & 255) | ((buttons & 0xffff) << 16), words[88 / 4], words[216 / 4], index);
}

U navTraceSample(U index, int connected, int valid, U buttons)
{
    if (valid)
        rawButtons |= buttons;
    return guard.sample(index, connected != 0, valid != 0, buttons);
}

U navTraceUnknown() { return guard.unknown(); }

void navTraceFrame(U before, U after, U elapsed, int clean)
{
    record(2, before, rawButtons, after, guard.unknown(), elapsed);
    if ((rawButtons | after) & NavigationGapGuard::NAV_MASK) {
        if (!dirty)
            burstSince = nowUs;
        dirty = true;
    }
    if (!clean || rawButtons || after || frameDelta > 100000) {
        idleTracking = false;
    } else if (!idleTracking) {
        idleTracking = true;
        idleSince = nowUs;
    }
}

void navTraceKey(int id, int edge, int timer) { record(3, id, edge, timer); }

void navTraceWork(int kind, int begin, const char *path, int result)
{
    if (kind < 1 || kind > 6)
        return;
    U hash = pathHash(path);
    int on = DIntr();
    if (begin)
        work[kind]++;
    else if (work[kind])
        work[kind]--;
    U active = work[kind];
    if (on)
        EIntr();
    record(4, kind, begin, hash, result, active);
}

void navTraceSelection(int page, int source, int row)
{
    if ((int)currentPage != page || source != lastSource || row != lastRow) {
        currentPage = page;
        lastSource = source;
        lastRow = row;
        record(5, page, source, row);
    }
}

void navTraceAutoSave()
{
    if (!dirty || saving)
        return;
    U at = microseconds();
    bool idle = idleTracking && at - idleSince >= 3000000;
    bool forced = at - burstSince >= 45000000;
    if ((!idle && !forced) || (lastAttempt && at - lastAttempt < 5000000))
        return;
    if (!forced)
        for (U i = 1; i <= 6; ++i)
            if (work[i])
                return;
    lastAttempt = at;
    int on = DIntr();
    saving = 1;
    U count = total < RECORDS ? total : RECORDS;
    U start = total < RECORDS ? 0 : head;
    if (on)
        EIntr();
    bool ok = exportTo(gBootDir, count, start, forced) ||
              exportTo(configGetHomePath(), count, start, forced);
    for (int device = 0; !ok && device < 9; ++device) {
        char root[16];
        snprintf(root, sizeof(root), "mass%d:/", device);
        ok = exportTo(root, count, start, forced);
    }
    if (!ok)
        ok = exportTo("mc0:/", count, start, forced) || exportTo("mc1:/", count, start, forced);
    if (ok) {
        ++captures;
        lastAttempt = 0; // throttle failures, not successful consecutive recreations.
        snprintf(status, sizeof(status), "Saved %u capture(s): %s", captures, savedPath);
        head = total = pathCount = 0;
        dirty = false;
        // Alternate only at verified idle boundaries. Forced exports never change
        // input semantics underneath a held key. Both modes remain in the log.
        if (idle)
            candidate = !candidate;
    } else {
        snprintf(status, sizeof(status), "Log write failed; keep device connected, rest to retry");
    }
    U finished = microseconds();
    previousUs = finished; // exclude our own export from the next frame-time sample.
    idleTracking = false;
    saving = 0;
    record(6, captures, ok, finished - at);
}

void navTraceDraw()
{
#ifndef NAVTRACE_HOST
    char line[120];
    snprintf(line, sizeof(line), "NAV investigation %s - recreate, then rest 5 seconds",
             candidate ? "B" : "A");
    rmDrawRect(0, 0, gGuiScreenWidth, 46, GS_SETREG_RGBA(0, 0, 0, 0x70));
    const u64 white = GS_SETREG_RGBA(0xff, 0xff, 0xff, 0x80);
    fntRenderString(gTheme->fonts[0], 8, 3, 0, gGuiScreenWidth - 16, 20, line, white);
    fntRenderString(gTheme->fonts[0], 8, 24, 0, gGuiScreenWidth - 16, 20, status, white);
#endif
}
#endif
