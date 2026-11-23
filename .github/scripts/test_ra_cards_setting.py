"""Exercise the experimental-card setting: its load rule, its dialog row and its telemetry-only control."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
settings = (root / 'src/core/settings.cpp').read_text()
settingsio = (root / 'src/app/settingsio.cpp').read_text()
gui = (root / 'src/ui/guisettings.cpp').read_text()
start = gui.index('        int raTelemetry;')
control = gui[start:gui.index('    }', start)]
load = next(line.strip() for line in settingsio.splitlines() if line.strip().startswith('gRAExperimentalCards =')).split('//')[0]
program = r'''
#include <assert.h>
#include <stdio.h>
enum { NETCFG_RA_MODE, NETCFG_RA_TELEMETRY, NETCFG_RA_EXPERIMENTAL_CARDS };
#define RA_MODE_CADUCEUS 1
static int diaRAConfig, mode, telemetry, enabled, gRAExperimentalCards;
static void diaGetInt(int dialog, int id, int *out) {
    (void)dialog;
    assert(id == NETCFG_RA_TELEMETRY);
    *out = telemetry;
}
static void diaSetEnabled(int dialog, int id, int value) {
    (void)dialog; assert(id == NETCFG_RA_EXPERIMENTAL_CARDS); enabled = value;
}
static void update(void) {
''' + control + r'''
}
static int loaded(int stored) { gRAExperimentalCards = stored; ''' + load + r''' return gRAExperimentalCards; }
int main(void) {
    assert(loaded(1) == 1);
    const int bad[] = {0, 2, -1, 99};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        assert(loaded(bad[i]) == 0); /* anything but an explicit 1 is Off */
    for (mode = 0; mode <= 1; mode++)
        for (telemetry = 0; telemetry <= 1; telemetry++) {
            gRAExperimentalCards = 1; enabled = -1; update();
            assert(enabled == telemetry); /* either client */
            assert(gRAExperimentalCards == 1); /* an inactive row keeps the saved choice */
        }
    puts("PASS: cards load Off unless explicitly 1, and the row is live with telemetry for either client");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    source = Path(temp) / 'cards.c'
    exe = Path(temp) / 'cards.exe'
    source.write_text(program)
    subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)

# Default Off, saved and loaded with every other plain key, and wired both ways in the dialog.
assert 'intSetting(CONFIG_OPL_RA_EXPERIMENTAL_CARDS, &gRAExperimentalCards, 0)' in settings
assert 'diaSetInt(diaRAConfig, NETCFG_RA_EXPERIMENTAL_CARDS, gRAExperimentalCards);' in gui
assert 'diaGetInt(diaRAConfig, NETCFG_RA_EXPERIMENTAL_CARDS, &gRAExperimentalCards);' in gui
dialogs = (root / 'src/ui/dialogs.cpp').read_text()
assert '{UI_BOOL, NETCFG_RA_EXPERIMENTAL_CARDS, 1, 1, _STR_HINT_RA_EXPERIMENTAL_CARDS, 0, 0, {.intvalue = {0, 0}}}' in dialogs
# The rows live on the RetroAchievements page, which only a RetroAchievements build has; the Network page keeps none.
ra_page = dialogs[dialogs.index('struct UIItem diaRAConfig[] = {'):dialogs.index('struct UIItem diaNetConfig[] = {')]
net_page = dialogs[dialogs.index('struct UIItem diaNetConfig[] = {'):]
net_page = net_page[:net_page.index('{UI_TERMINATOR}')]
assert ra_page.count('NETCFG_RA_') == 9 and 'NETCFG_RA_' not in net_page and '_STR_RA_' not in net_page
assert '#ifdef RETROACHIEVEMENTS\n// RetroAchievements page' in dialogs and '{UI_TERMINATOR}};\n#endif\n' in ra_page
net_fn = gui[gui.index('int guiShowNetConfig(void)'):gui.index('// Core path fields use a full-size backing buffer.')]
assert 'NETCFG_RA_' not in net_fn and 'gRA' not in net_fn
assert '    SETTINGS_NETWORK,\n#ifdef RETROACHIEVEMENTS\n    SETTINGS_RA,' in gui
assert '        _l(_STR_MENU_NETWORK),\n#ifdef RETROACHIEVEMENTS\n        "RetroAchievements",\n#endif\n' in gui
assert '#ifdef RETROACHIEVEMENTS\n            case SETTINGS_RA:\n                result = guiShowRAConfig();\n                break;\n#endif' in gui
print('PASS: default Off, dialog load/apply, the hint row, and a RetroAchievements page of its own')
