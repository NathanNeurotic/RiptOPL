# Check for Updates

The main menu's **Check for Updates** entry, just above **Date and Time**, asks GitHub whether the release this
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
- A network adapter with working DNS and internet access. The updater uses **Settings → Network** to
  reach GitHub over HTTPS. The SMB version only controls access to SMB shares. RiptOPL temporarily
  switches network stacks when UDPBD or UDPFS owns the adapter, then restores the chosen protocol.
- The console clock set to the current date and time. Open **Date and Time** in the main menu to
  enter local time manually or choose **Set from Internet**. If Check for Updates
  detects an unset clock, it offers to open this dialog and retries the check after you set the clock.

If an update check fails, the message distinguishes a connection failure, a secure connection or
certificate failure, a timeout, and an unusable release response. Include the exact message and the
build version from **About** when reporting a failure.

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

HTTPS verifies the certificate, host name and date against four built-in roots. GitHub uses Sectigo
E46 and R46, and its release download host uses ISRG X1 and X2. Redirects are followed
only to `github.com`, `release-assets.githubusercontent.com` and `objects.githubusercontent.com`, at
most three times. The RetroAchievements build uses TLS 1.3 and 1.2, with RSA-PSS for TLS 1.3 handshakes
when the server uses an RSA certificate. The standard LLVM build uses TLS 1.2.

## Status

Host tests cover TLS downloads and checksum parsing. The updater's portable HTTPS code also downloaded
the live `rolling` and `rolling-ra` releases and verified their ELF hashes with both TLS configurations.
The PS2 network path and in-place write to a physical memory card still need console validation.
