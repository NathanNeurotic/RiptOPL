# RiptOPLite: one loader per game device

`rolling-lite` carries one RiptOPL loader per game device, for consoles that only play from one.
Each is the Korium edition with that device's drivers alone, so the ELF is smaller and starts with
less to load. Every build is Korium (built-in Korium Komblete theme); the RetroAchievements builds
are Korium with RA on.

| Device | Korium | RetroAchievements | Game drivers it carries |
| --- | --- | --- | --- |
| USB | `RIPTOPLITE-USB.ELF` | `RIPTOPLITE-RA-USB.zip` | BDM, USB mass storage |
| MX4SIO | `RIPTOPLITE-MX4SIO.ELF` | `RIPTOPLITE-RA-MX4SIO.zip` | BDM, MX4SIO |
| iLink | `RIPTOPLITE-ILINK.ELF` | `RIPTOPLITE-RA-ILINK.zip` | BDM, iLinkman, IEEE1394 |
| HDD | `RIPTOPLITE-HDD.ELF` | `RIPTOPLITE-RA-HDD.zip` | APA/PFS, exFAT on the internal drive, HD Pro, DESR DVR |
| SMB | `RIPTOPLITE-SMB.ELF` | `RIPTOPLITE-RA-SMB.zip` | smbman, smb2man, NBNS, in-game SMB |
| UDPBD | `RIPTOPLITE-UDPBD.ELF` | `RIPTOPLITE-RA-UDPBD.zip` | BDM, SMAP UDPBD |
| UDPFS | `RIPTOPLITE-UDPFS.ELF` | `RIPTOPLITE-RA-UDPFS.zip` | UDPFS Files and IMG |
| MMCE | `RIPTOPLITE-MMCE.ELF` | `RIPTOPLITE-RA-MMCE.zip` | MMCE games page |

HTTP and PC Card CF have no lite build.

## What every lite build keeps

- **MMCE modules.** Game ID and the IGR boot-card switch use them whatever the game device.
- **The network stack.** netman, SMAP, ps2ip, the HTTP client and the NBD server.
- **BDM core and the USB block driver.** A lite loader started from a USB stick can still read its
  settings there, and the NBD server's ATA driver needs BDM.
- **RetroAchievements builds also keep** everything RA uses on any device: the in-game SMAP and
  SMSTCPIP pair that carries telemetry, the RA modules (raudp, rapull, rapops, the IGR overlay), the
  Disc page, and the SMB share drivers (smbman, smb2man, NBNS), which the achievements page reads
  `ART/CADUCEUS.KEY` and badges from.

## Why the RA builds are ZIPs

RetroAchievements needs files outside the loader: ORBIT's Neutrino RA runtime replaces upstream
Neutrino for every RA launch (see [RetroAchievements](RETROACHIEVEMENTS.md#neutrino-in-the-ra-edition)).
Each `RIPTOPLITE-RA-<DEVICE>.zip` therefore carries the ELF, `neutrino/` (copied from the verified
RA install kit of the same build), the xeRAbora shortcut, the licences, the build manifest and a
README. The Korium lite builds need nothing the standard Korium kit does not already install, so they
are bare ELFs that replace `RIPTOPL.ELF`.

## Settings

The game page, start mode and protocol of the one device are fixed by the build, and their rows are
not shown on Game Sources or Network. Every other setting works as in the full loader.

The settings file is shared with the full loader. A lite loader reads the full loader's device
settings when it starts and writes them back unchanged when it saves, so switching between a lite
and the full loader never changes which devices the full one uses.

A lite loader cannot read settings from a device it has no driver for. Keep the settings on its own
device, a memory card or a USB stick.

## How it is built

`python tools/build_edition.py lite --device <device>` (and `lite_ra`). `tools/lite_devices.py`
lists what each device keeps. The other devices' IRX are left out of the ELF; their symbols become
zero-sized stubs that `sysLoadModuleBuffer` refuses. `include/riptlite.h` turns the other devices'
loaders off at compile time, and `src/core/riptlite.cpp` holds their settings off whatever the
settings file says. `.github/scripts/test_riptoplite.py` checks each definition and the channel's contents.

`tools/publish_lite.py` checks every lite build against the commit and SDK of the full kits before
`rolling-lite` is published. That channel is never marked GitHub's Latest release.
