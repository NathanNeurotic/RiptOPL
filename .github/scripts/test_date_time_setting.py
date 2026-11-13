"""General & System -> Date and Time sets the console clock in the console's own time zone.

- guisettings.cpp: guiClockDaysIn / guiClockShift are compiled and run on the PC. The PS2 clock keeps
  Japan time, so local = clock + (zone - 540 min) + 60 min with daylight saving, and the write is the
  same shift backwards. Every zone from -12 h to +14 h (quarter hours), with and without daylight
  saving, must round-trip across day, month, year and leap-day carries, and a shift that leaves
  2000-2099 (the clock's two-digit year) must be refused.
- dialogs.cpp / guisettings.cpp / _base.yml: the row is on General & System, its button reopens the page
  (nothing is saved), and the strings exist.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
failures = []


def read(rel):
    return (root / rel).read_text(encoding='utf-8').replace('\r\n', '\n')


def function_text(source, signature):
    match = re.search(r'^' + re.escape(signature) + r'[^;{]*\)\s*\{', source, re.M)
    if match is None:
        failures.append('guisettings.cpp: %s...) not found' % signature)
        return ''
    end = source.index('\n}', match.start())
    return source[match.start():end] + '\n}\n'


settings = read('src/ui/guisettings.cpp')
dialogs = read('src/ui/dialogs.cpp')
base = read('lng/tmpl/_base.yml')

funcs = function_text(settings, 'static int guiClockDaysIn(') + function_text(settings, 'static int guiClockShift(')

HARNESS = r'''
#include <stdio.h>
@FUNCS@
static int ref_days(int y, int m) { static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31}; return d[m-1] + (m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0)); }
/* minutes since 2000-01-01 00:00 */
static long serial(const int *t) { long n = 0; int y, m; for (y = 2000; y < t[0]; y++) n += 365 + (ref_days(y, 2) == 29); for (m = 1; m < t[1]; m++) n += ref_days(t[0], m); return ((n + t[2] - 1) * 24 + t[3]) * 60 + t[4]; }
int main(void) {
    static const int dates[][5] = {{2000,1,1,0,0},{2000,1,1,23,59},{2000,2,28,22,0},{2000,2,29,23,30},{2000,12,31,23,59},
        {2026,11,12,14,52},{2027,2,28,23,59},{2027,3,1,0,0},{2028,2,29,0,15},{2028,12,31,12,0},{2099,12,31,0,0},{2099,12,31,23,59},
        {2099,1,1,0,0},{2050,6,30,23,45},{2050,7,1,0,10}};
    int bad = 0, zone, dst, i, k, cases = 0;
    for (zone = -720; zone <= 840; zone += 15)
        for (dst = 0; dst < 2; dst++)
            for (i = 0; i < (int)(sizeof(dates) / sizeof(dates[0])); i++) {
                int off = zone - 540 + 60 * dst, t[5], u[5], ok;
                for (k = 0; k < 5; k++) t[k] = u[k] = dates[i][k];
                ok = guiClockShift(t, off);
                long want = serial(u) + off;
                int inRange = want >= 0 && want < serial((int[]){2100,1,1,0,0});
                cases++;
                if (ok != inRange) { printf("FAIL range zone %d dst %d date %d: ok %d want %d\n", zone, dst, i, ok, inRange); bad = 1; continue; }
                if (!ok) continue;
                if (serial(t) != want || t[2] > ref_days(t[0], t[1]) || t[3] > 23 || t[4] > 59) { printf("FAIL shift zone %d dst %d date %d\n", zone, dst, i); bad = 1; continue; }
                if (!guiClockShift(t, -off)) { printf("FAIL back zone %d dst %d date %d\n", zone, dst, i); bad = 1; continue; }
                for (k = 0; k < 5; k++) if (t[k] != u[k]) { printf("FAIL round trip zone %d dst %d date %d\n", zone, dst, i); bad = 1; break; }
            }
    { int y, m; for (y = 2000; y <= 2099; y++) for (m = 1; m <= 12; m++) if (guiClockDaysIn(y, m) != ref_days(y, m)) { printf("FAIL days %d-%d\n", y, m); bad = 1; } }
    { int t[5] = {2026,1,1,0,0}; if (guiClockShift(t, 1440) || guiClockShift(t, -1440)) { printf("FAIL a whole-day shift must be refused\n"); bad = 1; } }
    if (!bad) printf("PASS: %d clock shifts round-trip (zones -12 h..+14 h, daylight saving, carries, leap days, 2000-2099 edge)\n", cases);
    return bad;
}
'''

if funcs:
    cc = shutil.which('cc') or shutil.which('gcc')
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / 't.c'
        c.write_text(HARNESS.replace('@FUNCS@', funcs), encoding='utf-8')
        exe = Path(d) / 't'
        subprocess.run([cc, '-std=gnu99', '-Wall', '-Werror', str(c), '-o', str(exe)], check=True)
        if subprocess.run([str(exe)]).returncode != 0:
            failures.append('clock shift harness failed')

# The local offset uses PS2SDK's readers (sign, unset config and DTL-T10000 handled there).
if 'configGetTimezone() - 540 + (configIsDaylightSavingEnabled() ? 60 : 0)' not in settings:
    failures.append('guiClockLocalOffset must be configGetTimezone() - 540 (+60 with daylight saving)')
# The write rebuilds the request on each try (the reply overwrites it) and checks the command error bit.
show = function_text(settings, 'static void guiSettingsShowDateTime(')
write = function_text(settings, 'static int guiClockWrite(')
retry = re.search(r'for \(int i = 0; i < 2; i\+\+\) \{(.*?)\n    \}', write, re.S)
body = retry.group(1) if retry else ''
if 'sceCdWriteClock(&clk) && !(clk.stat & CdlRTCStatCommandError)' not in body or not 0 <= body.find('memset(&clk') < body.find('sceCdWriteClock'):
    failures.append('guiClockWrite: rebuild clk before each sceCdWriteClock and check CdlRTCStatCommandError')
# Set from Internet: UTC from the Date header goes to the clock's Japan time (+9 h).
if 'guiClockShift(t, 540) && guiClockWrite(t,' not in show:
    failures.append('guiSettingsShowDateTime: Set from Internet must shift UTC by +540 minutes before writing')
if 'clk.month & 0x7F' not in show:
    failures.append('guiSettingsShowDateTime: mask the month century bit when reading')
# The row sits on General & System and its button only reopens the page (no save, no reapply).
diaconfig = dialogs[dialogs.index('struct UIItem diaConfig[]'):]
diaconfig = diaconfig[:diaconfig.index('{UI_TERMINATOR}')]
if 'CFG_DATETIME_BUTTON' not in diaconfig:
    failures.append('dialogs.cpp: the Date and Time button must be on General & System (diaConfig)')
general = function_text(settings, 'static int guiSettingsShowGeneral(')
m = re.search(r'if \(result == CFG_DATETIME_BUTTON\) \{(.*?)\}', general, re.S)
if m is None or 'goto reshow_general' not in m.group(1) or 'markSavePending' in m.group(1):
    failures.append('guiSettingsShowGeneral: the Date and Time button must reopen the page without arming a save')
for label in ('DATE_TIME', 'HINT_DATE_TIME', 'DATE', 'TIME', 'DATE_TIME_ERROR', 'DATE_TIME_SYNC', 'HINT_DATE_TIME_SYNC',
              'DATE_TIME_SYNCING', 'DATE_TIME_SYNCED', 'DATE_TIME_SYNC_ERROR'):
    if not re.search(r'^(?:- |\s+)label: %s$' % label, base, re.M):
        failures.append('_base.yml: label %s missing' % label)

# nettime.cpp: the HTTP Date parser, compiled and run on the PC.
nettime = read('src/sources/nettime.cpp')
m = re.search(r'^int netTimeParseDate\(.*?\n\}\n', nettime, re.M | re.S)
if m is None:
    failures.append('nettime.cpp: netTimeParseDate not found')
else:
    PARSE = r'''
#include <stdio.h>
#include <string.h>
#include <strings.h>
@FUNC@
static int check(const char *h, int ok, int y, int mo, int d, int hh, int mi, int ss) {
    int t[6] = {0}; int got = netTimeParseDate(h, t);
    if (got != ok || (ok && (t[0] != y || t[1] != mo || t[2] != d || t[3] != hh || t[4] != mi || t[5] != ss))) { printf("FAIL parse: %s\n", h); return 1; }
    return 0;
}
int main(void) {
    int bad = 0;
    bad |= check("HTTP/1.1 301 Moved\r\ncontent-length: 0\r\ndate: Fri, 13 Nov 2026 03:37:42 GMT\r\n\r\n", 1, 2026, 11, 13, 3, 37, 42);
    bad |= check("HTTP/1.1 200 OK\r\nDate: Mon, 01 Jan 2035 00:00:00 GMT\r\n\r\n", 1, 2035, 1, 1, 0, 0, 0);
    bad |= check("HTTP/1.1 200 OK\r\nDATE: Tue, 29 Feb 2028 23:59:60 GMT\r\n", 1, 2028, 2, 29, 23, 59, 60);
    bad |= check("HTTP/1.1 200 OK\r\nX-Date: Fri, 13 Nov 2026 03:37:42 GMT\r\n\r\n", 0, 0, 0, 0, 0, 0, 0);
    bad |= check("HTTP/1.1 200 OK\r\nDate: Fri, 13 Foo 2026 03:37:42 GMT\r\n", 0, 0, 0, 0, 0, 0, 0);
    bad |= check("HTTP/1.1 200 OK\r\nDate: Fri, 13 eba 2026 03:37:42 GMT\r\n", 0, 0, 0, 0, 0, 0, 0);
    bad |= check("HTTP/1.1 200 OK\r\nDate: Fri, 13 Nov 1999 03:37:42 GMT\r\n", 0, 0, 0, 0, 0, 0, 0);
    bad |= check("HTTP/1.1 200 OK\r\nDate: Fri, 13 Nov 2026 24:00:00 GMT\r\n", 0, 0, 0, 0, 0, 0, 0);
    bad |= check("HTTP/1.1 200 OK\r\nDate: Fri, 13 Nov 2026\r\n", 0, 0, 0, 0, 0, 0, 0);
    bad |= check("HTTP/1.1 200 OK\r\n\r\n", 0, 0, 0, 0, 0, 0, 0);
    if (!bad) printf("PASS: HTTP Date header parser (case, leap second, bad month, range, truncated, missing)\n");
    return bad;
}
'''
    cc = shutil.which('cc') or shutil.which('gcc')
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / 'p.c'
        c.write_text(PARSE.replace('@FUNC@', m.group(0)), encoding='utf-8')
        exe = Path(d) / 'p'
        subprocess.run([cc, '-std=gnu99', '-Wall', '-Werror', str(c), '-o', str(exe)], check=True)
        if subprocess.run([str(exe)]).returncode != 0:
            failures.append('HTTP Date parser harness failed')

if failures:
    for f in failures:
        print('FAIL:', f)
    sys.exit(1)
print('PASS: Date and Time row, dialog, strings and console-clock write')
