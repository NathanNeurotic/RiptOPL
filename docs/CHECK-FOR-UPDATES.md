# Check for Updates

The main menu's **Check for Updates** entry (just above About) asks GitHub whether the release this
build came from has a newer build, and on request replaces the running ELF with it. Nothing happens
until you choose the entry: RiptOPL never checks or downloads on its own.

## Which builds have it

| Build | GitHub release | Matched by |
| --- | --- | --- |
| RiptOPL (LLVM) | `rolling` | Standard, controller variants and diagnostic configurations |
| RetroAchievements (GCC) | `rolling-ra` | Standard or PadEmu Off, packed or unpacked |
| RiptOPLite (GCC) | `rolling-lite` | Selected device and RetroAchievements support |

Korium, IncebtIon and the old independent LLVM channel are retired. To move
from one of those channels, install a current standard RiptOPL ELF manually
once. Older builds continue checking the release tag they were built for.

Install an updater-capable build manually once if your existing build lacks this update path.
The release channel and configuration are fixed at build time. Where a release ships two formats,
the updater reads the running ELF's headers from disk to select the asset. The file can be renamed.
Local builds using an alternate compiler have no matching asset on the rolling channels.

## What it needs

- RiptOPL started from a memory card (`mc0:`/`mc1:`), a directly accessible MMCE card (`mmceN:`),
  a BDM drive (`massN:`), or an internal APA partition named in the launch path. PSBBN Definitive
  Project's launcher can use `hdd0:__system:pfs:/launcher/RIPTOPL-RA.ELF`. An unqualified PFS mount
  alias has no partition identity after the IOP resets.
- A network adapter with internet access. It uses the same network settings as SMB (Settings > Network).
  RiptOPL temporarily switches the adapter away from UDPBD or UDPFS when necessary.
- The console clock set to the current date (Settings > General & System > Date and Time): the GitHub certificates cannot be checked without it.

## What it does

1. Downloads the channel's `SHA256SUMS.txt`. The release's build number comes from its versioned zip
   names (for example, `RIPTOPL-v1.2.0-Beta-4257.zip`), and the ELF's expected SHA-256 from its line.
2. Compares that build number with the running one (the number in the About screen's version). Equal
   or older: "already the newest". A build without a number (a local build) is not compared.
3. On your OK, downloads the ELF into memory. The first bytes must be a PS2 ELF header; the size is
   limited to 8 MB.
4. Checks the SHA-256 and packing format before writing.
5. On APA, writes a sibling `.update` file and flushes it before moving the original to `.bak` and
   renaming the new file into place. The partition needs space for the new ELF alongside the old one.
   If an interrupted swap leaves a `.bak` and the launcher file is missing, rename the backup to the
   original filename before retrying. An existing staging or backup file stops a new install.
   Memory-card, BDM and MMCE installs overwrite in place to minimize space use. If a write fails in
   place, reinstall the ELF manually. Error messages include the write step and error code.

After installation, choose whether to launch the updated ELF now or start it the next time you open RiptOPL.
Settings and themes stay in place.

## Security

HTTPS with certificate, host name and date always verified, against four built-in roots only: Sectigo
E46 and R46 (github.com) and ISRG X1 and X2 (GitHub's release download host). Redirects are followed
only to `github.com`, `release-assets.githubusercontent.com` and `objects.githubusercontent.com`, at
most three times. The RetroAchievements builds use their existing TLS (TLS 1.3 and 1.2); the others carry
a TLS 1.2-only build of the same mbedTLS (`third_party/mbedtls/riptopl_mbedtls_slim_config.h`).

## Status

Host checks cover TLS, checksum parsing, packed and unpacked ELF detection, and APA launch path parsing.
The full download and install on a physical PS2, including PSBBN's APA launcher directory, still need a console test.
