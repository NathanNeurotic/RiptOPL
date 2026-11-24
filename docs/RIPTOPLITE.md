# RiptOPLite: one loader per game device

`rolling-lite` carries one RiptOPL loader per game device, for consoles that only play from one.
Each GCC build includes only the game drivers needed for its device, so the ELF is smaller.
Both standard and RetroAchievements Lite builds use the same built-in PSBBN theme as full RiptOPL.

| Device | Standard | RetroAchievements | Game drivers it carries |
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

RetroAchievements needs files outside the loader: NULD's (formerly ORBIT) Neutrino RA runtime replaces upstream
Neutrino for every RA launch (see [RetroAchievements](RETROACHIEVEMENTS.md#neutrino-in-the-ra-edition)).
Each `RIPTOPLITE-RA-<DEVICE>.zip` therefore carries the loader's folder
(`APPS/APP_RIPTOPLITE-RA-<DEVICE>/`) and its PSU, `neutrino/` (copied from the verified RA install kit
of the same build), the xeRAbora shortcut, the licences, the build manifest and a README. The standard
Lite builds use the companion folders from the full RiptOPL kit, so they ship as a bare ELF and a PSU.

## Folders and PSUs

Every RiptOPL loader has its own folder, named `APP_` plus its ELF name, and a PSU of the same name:

| Loader | Folder and PSU |
| --- | --- |
| `RIPTOPL.ELF` | `APP_RIPTOPL` |
| `RIPTOPL-RA.ELF` | `APP_RIPTOPL-RA` |
| `RIPTOPLITE-USB.ELF` | `APP_RIPTOPLITE-USB` |
| `RIPTOPLITE-RA-USB.ELF` | `APP_RIPTOPLITE-RA-USB` |
| `RIPTOPL-PADEMU0.ELF` (variants ZIP) | `APP_RIPTOPL-PADEMU0` |

`APP_RIPTOPLITE-<DEVICE>.psu` and `APP_RIPTOPLITE-RA-<DEVICE>.psu` are published beside the ELFs. Import
one to a memory card, or put the ELF in a folder of that name under `APPS/` on any other device. Check
for Updates replaces the running ELF where it is, so it works in any folder.

The first time a loader starts from its own folder, that folder has no settings yet. It then reads the
full loader's settings once from the folder beside it (`APP_RIPTOPL`, or `APP_RIPTOPL-RA` for a
RetroAchievements build) and saves in its own folder from then on. The full loader's folder is never
changed or deleted. A RiptOPLite ELF that already lives in `APP_RIPTOPL` keeps working as before.

## Settings

The game page and protocol of the one device are fixed by the build, and their rows are not shown on
Game Sources or Network. The device's **Start Mode** (Off, Manual or Auto) is a single row on Game
Sources and starts **Off**: turn it on to see your games. It is saved as `lite_start_mode`, apart from
the full loader's own start modes. Every other setting works as in the full loader.

In its own folder, a lite loader keeps its own settings file, started from a copy of the full
loader's (see Folders and PSUs). A lite ELF kept in the full loader's folder shares that file: it
reads the full loader's device settings when it starts and writes them back unchanged when it saves,
so switching between a lite and the full loader never changes which devices the full one uses.

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
