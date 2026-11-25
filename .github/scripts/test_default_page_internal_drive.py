"""Default Menu "HDD Games" opens the internal drive's page when that drive is exFAT or FAT32.

HDD_MODE is the APA page only. An exFAT/FAT32 internal drive lists its games on a massN: page, which appears
once the drive has attached, and with (APA) Start Mode off HDD_MODE is not registered at all. The boot used
to fall through to the MMCE page every time.

This compiles the real code from src/app/opl.cpp (the block in deferredInit and oplBootPageArrived) on the
host and checks:
- the internal drive's page is taken at once when it is already up;
- otherwise the boot waits for it and moves there when it appears, once;
- it does not move after the user has pressed anything;
- a USB page appearing does not take the boot, and does not cancel the wait;
- an APA page that is up keeps "HDD Games" as before;
- Internal HDD (exFAT) off, or another Default Menu, leaves everything as it was;
and statically that the BDM page code reports a page that became visible.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
opl = (root / 'src/app/opl.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
devices = (root / 'src/sources/bdmdevices.cpp').read_text(encoding='utf-8')
support = (root / 'src/sources/bdmsupport.cpp').read_text(encoding='utf-8')

start = opl.index('    gBootAwaitsInternalDrive = 0;\n    if (bootMode == HDD_MODE && gEnableBdmHDD')
end = opl.index('    // All Games:', start)
block = opl[start:end]
after_start = opl.index('    if (bootMode != HDD_MODE)\n        gBootAwaitsInternalDrive = 0;', end)
after = opl[after_start:opl.index('\n', opl.index('gBootAwaitsInternalDrive = 0;', after_start)) + 1]
fstart = opl.index('void oplBootPageArrived(int mode)')
arrived = opl[fstart:opl.index('\n}\n', fstart) + 3]

assert 'host::app().bootPageArrived(itemList->mode);' in devices, 'a BDM page that becomes visible must be reported'
assert devices.index('menuItem.visible = 1;') < devices.index('host::app().bootPageArrived(itemList->mode);')
assert 'data->bdmDeviceType == BDM_TYPE_ATA' in support[support.index('int bdmModeIsATA(int mode)'):][:400]

HARNESS = r'''
#include <cstdio>
#include <cstddef>
enum { BDM_MODE = 0, BDM_MODE_LAST = 4, ETH_MODE = 5, HDD_MODE = 6, APP_MODE = 7, MMCE_MODE = 8, FAV_MODE = 9, MODE_COUNT = 10 };
enum { GUI_OP_SELECT_MENU = 1 };
struct menu_item_t { int visible; };
struct module_t { void *support; menu_item_t menuItem; };
struct gui_update_t { struct { menu_item_t *menu; } menu; };
static module_t list_support[MODE_COUNT];
static int ata_slot = -1, gEnableBdmHDD, KeyPressedOnce, gAllGames, gBootAwaitsInternalDrive;
static int bdmModeIsATA(int mode) { return mode == ata_slot; }
static gui_update_t op;
static menu_item_t *selected;
static int selects;
static gui_update_t *guiOpCreate(int) { return &op; }
static void guiDeferUpdate(gui_update_t *u) { selected = u->menu.menu; selects++; }
ARRIVED
static int boot(int bootMode)
{
BLOCK
    if (gAllGames && list_support[FAV_MODE].support != NULL)
        bootMode = FAV_MODE;
AFTER
    return bootMode;
}
static int something;
static void reset(void)
{
    for (int i = 0; i < MODE_COUNT; i++) { list_support[i].support = NULL; list_support[i].menuItem.visible = 0; }
    list_support[MMCE_MODE].support = &something; list_support[MMCE_MODE].menuItem.visible = 1;
    for (int i = BDM_MODE; i <= BDM_MODE_LAST; i++) list_support[i].support = &something;
    list_support[FAV_MODE].support = &something;
    ata_slot = -1; gEnableBdmHDD = 1; KeyPressedOnce = 0; gAllGames = 0; gBootAwaitsInternalDrive = 0;
    selected = NULL; selects = 0;
}
static int fails;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main()
{
    // The drive's page is already up: take it at once.
    reset(); ata_slot = 2; list_support[2].menuItem.visible = 1; list_support[0].menuItem.visible = 1;
    CHECK(boot(HDD_MODE) == 2); CHECK(!gBootAwaitsInternalDrive);

    // Not up yet: the old fallback runs for now, then the page takes the boot when it appears.
    reset();
    CHECK(boot(HDD_MODE) == HDD_MODE); CHECK(gBootAwaitsInternalDrive);
    ata_slot = 1;
    list_support[0].menuItem.visible = 1; oplBootPageArrived(0); // a USB stick first
    CHECK(selects == 0); CHECK(gBootAwaitsInternalDrive);
    list_support[1].menuItem.visible = 1; oplBootPageArrived(1);
    CHECK(selects == 1); CHECK(selected == &list_support[1].menuItem); CHECK(!gBootAwaitsInternalDrive);
    oplBootPageArrived(1); // a replug later never moves the page again
    CHECK(selects == 1);

    // The user pressed something first: the page they are on stays.
    reset(); boot(HDD_MODE); ata_slot = 1; KeyPressedOnce = 1; oplBootPageArrived(1);
    CHECK(selects == 0); CHECK(!gBootAwaitsInternalDrive);

    // An APA page that is up keeps "HDD Games".
    reset(); list_support[HDD_MODE].support = &something; list_support[HDD_MODE].menuItem.visible = 1;
    CHECK(boot(HDD_MODE) == HDD_MODE); CHECK(!gBootAwaitsInternalDrive);

    // Internal HDD (exFAT) off: nothing waits.
    reset(); gEnableBdmHDD = 0;
    CHECK(boot(HDD_MODE) == HDD_MODE); CHECK(!gBootAwaitsInternalDrive);
    ata_slot = 1; oplBootPageArrived(1); CHECK(selects == 0);

    // Another Default Menu: nothing waits, and the drive appearing moves nothing.
    reset();
    CHECK(boot(MMCE_MODE) == MMCE_MODE); CHECK(!gBootAwaitsInternalDrive);
    ata_slot = 1; oplBootPageArrived(1); CHECK(selects == 0);

    // All Games takes the boot: nothing waits.
    reset(); gAllGames = 1;
    CHECK(boot(HDD_MODE) == FAV_MODE); CHECK(!gBootAwaitsInternalDrive);
    return fails != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'boot.cpp'
    src.write_text(HARNESS.replace('ARRIVED', arrived).replace('BLOCK', block).replace('AFTER', after))
    exe = Path(tmp) / 'boot'
    subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Werror', '-o', str(exe), str(src)], check=True)
    subprocess.run([str(exe)], check=True, timeout=60)
print('Default Menu HDD Games: the internal exFAT/FAT32 page takes the boot, at once or when it appears')
