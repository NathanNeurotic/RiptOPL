#!/usr/bin/env python3
"""USBD looks at the root-hub ports when the menu loads it, and never in a game.

A stick left plugged in across an IOP reboot gives the next USBD no root-hub interrupt, so it stays
missing until it is replugged. modules/usb/usbd-ra/src/usbd.c disables both root ports and runs the
root-hub change handler once after hcdInit() when it is loaded without options. The handler's own
attach resets one port at a time. This checks the three facts the fix depends on:
  1. usbd.c disables both ports and calls handleRhsc() under the driver lock, only after a successful
     hcdInit() and only for argc < 2, and writes no PORT_RESET of its own;
  2. the menu loads USBD with no options (argc 1);
  3. ee_core loads USBD with options in every game (argc >= 2), so a game's IOP reboot is unchanged.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def read(path):
    with open(os.path.join(ROOT, path), encoding='utf-8') as f:
        return f.read()


def fail(msg):
    sys.exit('FAIL: ' + msg)


usbd = read('modules/usb/usbd-ra/src/usbd.c')
m = re.search(r'if \(hcdInit\(\) == 0 && argc < 2\) \{\s*'
              r'memPool\.ohciRegs->HcRhPortStatus\[0\] = BIT\(PORT_CONNECTION\);\s*'
              r'memPool\.ohciRegs->HcRhPortStatus\[1\] = BIT\(PORT_CONNECTION\);\s*'
              r'usbdLock\(\);\s*handleRhsc\(\);\s*usbdUnlock\(\);\s*\}', usbd)
if not m:
    fail('usbd.c does not disable both root ports and run handleRhsc() after hcdInit() for a load without options')
if usbd.count('hcdInit()') != 1 or 'BIT(PORT_RESET)' in usbd:
    fail('usbd.c should call hcdInit() once and leave port resets to the gated attach path')

system = read('src/platform/system.cpp')
if not re.search(r'sysLoadModuleBuffer\(&usbd_irx, size_usbd_irx, 0, NULL\);', system):
    fail('the menu no longer loads USBD without options: the root-port scan would not run there')

iopmgr = read('ee_core/src/iopmgr.c')
loads = re.findall(r'LoadOPLModule\(OPL_MODULE_ID_USBD, 0, (\w+), (\w+)\);', iopmgr)
if loads != [('usbd_args_len', 'usbd_args')]:
    fail('ee_core should load USBD once, from usbd_args')
if 'const char *usbd_args = "thpri=2,3";' not in iopmgr or 'int usbd_args_len = 11;' not in iopmgr:
    fail('ee_core no longer passes thpri=2,3 to USBD in game: a game would get the root-port scan')

print('OK: menu USBD scans the root ports once; in-game USBD always gets options and does not')
