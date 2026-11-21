# Check for Updates

The main menu's **Check for Updates** entry (just above About) asks GitHub whether the release this
build came from has a newer build, and on request replaces the running ELF with it. Nothing happens
until you choose the entry: RiptOPL never checks or downloads on its own.

## Which builds have it

Only two full standard editions support Check for Updates:

| Build | GitHub release | ELF replaced |
| --- | --- | --- |
| RiptOPL (LLVM) | `rolling` | `RIPTOPL.ELF` |
| RetroAchievements (GCC) | `rolling-ra` | `RIPTOPL-RA.ELF` |

Korium, IncebtIon and the old independent LLVM channel are retired. To move
from one of those channels, install a current standard RiptOPL ELF manually
once. Older builds continue checking the release tag they were built for.

RiptOPLite, debug and variant builds do not have it, so a build is never replaced by a different kind
of build. The channel is fixed at build time (`tools/build_edition.py update_channel`).

## What it needs

- RiptOPL started from a memory card (`mc0:`/`mc1:`) or a USB/exFAT drive (`massN:`). Other places
  (HDD, network, MMCE) are refused before anything is downloaded.
- A network adapter with internet access. It uses the same network settings as SMB (Settings > Network).
  While UDPBD or UDPFS owns the network adapter, it refuses and says so.
- The console clock set to the current date (Settings > General & System > Date and Time): the GitHub certificates cannot be checked without it.

## What it does

1. Downloads the channel's `SHA256SUMS.txt`. The release's build number comes from its versioned zip
   names (for example, `RIPTOPL-v1.2.0-Beta-4257.zip`), and the ELF's expected SHA-256 from its line.
2. Compares that build number with the running one (the number in the About screen's version). Equal
   or older: "already the newest". A build without a number (a local build) is not compared.
3. On your OK, downloads the ELF into memory. The first bytes must be a PS2 ELF header; the size is
   limited to 8 MB.
4. Checks the SHA-256 of what was downloaded against `SHA256SUMS.txt`. A mismatch changes nothing.
5. Overwrites the running ELF in place under its own name, as Launchstation does. No second file, rename or backup is made.
   The old ELF is emptied before the new one is written, so a card too full for the new ELF can leave it
   damaged. If the write fails part-way the ELF may be damaged: the message
   gives the step (1 open, 2 write, 3 close) and the error code, and the ELF must be copied to the
   device again.

The new build starts the next time RiptOPL is opened. Settings, themes and other files are not touched.

## Security

HTTPS with certificate, host name and date always verified, against four built-in roots only: Sectigo
E46 and R46 (github.com) and ISRG X1 and X2 (GitHub's release download host). Redirects are followed
only to `github.com`, `release-assets.githubusercontent.com` and `objects.githubusercontent.com`, at
most three times. The RetroAchievements builds use their existing TLS (TLS 1.3 and 1.2); the others carry
a TLS 1.2-only build of the same mbedTLS (`third_party/mbedtls/riptopl_mbedtls_slim_config.h`).

## Status

Host-tested: the TLS download (`.github/scripts/test_ratls_host.py`) and the parsing
(`.github/scripts/test_selfupdate_host.py`, against the real `SHA256SUMS.txt` files). NOT TESTED on
hardware or in PCSX2: the real GitHub certificate chains, the in-place write to a real memory card and the full download.
