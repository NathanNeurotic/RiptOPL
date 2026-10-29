# ps2build configuration candidates

This repository has one SDK and three release editions: Korium, RetroAchievements
and IncebtIon. `--configuration` selects optional controller, diagnostic or debug
settings within an edition. GCC and LLVM select compilers. Configuration
candidates are not extra release channels and cannot replace standard release kits.

Releases carry them for every theme, as the original did: the Korium and IncebtIon
channels each publish `RIPTOPL-VARIANTS-<version>.zip` (`pademu0`, `ds5`, `extras`, `extras_ds5`)
and `RIPTOPL-DEBUG-<version>.zip` (every other nonstandard configuration), built with that channel's
theme, one folder per configuration with its GCC `RIPTOPL.ELF` and build manifest; the
RetroAchievements channel publishes `RIPTOPL-RA-VARIANTS-<version>.zip` (`pademu0`, as `RIPTOPL-RA.ELF`)
and `RIPTOPL-RA-VARIANTS-IncebtIon-<version>.zip` for its IncebtIon-theme kit. The
publisher packs them only from builds of the released commit and SDK suite. Every channel
also publishes `RIPTOPL-LANGS-<version>.zip`, the translations and their fonts. Every archive
name ends with the build version, for example `RIPTOPL-Korium-v1.2.0-Beta-3925.zip`; the
loader ELFs, PSUs, `BUILD-MANIFEST.json` and `SHA256SUMS.txt` keep fixed names, so their download
links never change. The release notes name the version at the top.

Prepare the SDK/ports and assets as described in [PS2BUILD-MIGRATION.md](PS2BUILD-MIGRATION.md),
then use the existing builder:

```sh
python tools/build_edition.py korium --configuration ds5 --toolchain gcc
python tools/build_edition.py ra --configuration pademu0 --toolchain llvm
python tools/build_edition.py korium --configuration iopcore_debug_uart --toolchain llvm
```

Omitting `--configuration` selects `standard` and preserves existing release
commands. Every build regenerates edition artwork, RA marks and the displayed
version. Nonstandard version strings also name their configuration.

| Configuration | Behavior selected |
| --- | --- |
| `standard` | Normal edition defaults: GSM1080P and PADEMU; no optional DS5/debug/extras flags. |
| `pademu0` | Disable PADEMU in the menu, EE core and memory-card modules; omit DS34 EE libraries and pad-emulator/DS34 embedded modules. |
| `ds5` | Enable the existing optional USB DualSense implementation. |
| `extras` | Enable RTL language support and in-game screenshots in the menu/core. |
| `extras_ds5` | Combine extras and USB DualSense. |
| `dtl` | Select the DTL-T10000 menu/core flags and matching `udnl-t300` boot module. |
| `diag` | Enable field diagnostics without a TTY debugger. |
| `debug` | Menu-side UDP TTY, IOPTRAP and PS2LINK. |
| `debug_uart` | Menu-side PPC UART TTY and IOPTRAP. |
| `ingame_debug` | UDP menu/in-game debug modules and CDVDMAN diagnostics; retain normal memory-card modules. |
| `iopcore_debug` | Also enable memory-card module diagnostics. |
| `eesio_debug` | EE serial logging via the installed SIOCookie package, plus the menu debug resources. |
| `ingame_debug_uart` | PPC UART menu/in-game debug modules and CDVDMAN diagnostics. |
| `iopcore_debug_uart` | Also enable memory-card module diagnostics. |
| `ingamediag` | In-game freeze report: after a freeze, the IGR combo writes `mc0:/RIPTFRZ.TXT`. Refused with RetroAchievements. |

Menu/core transport flags agree (`TTY_PPC_UART` versus `TTY_PPC` is the existing
interface between those components). In-game debug removes SMSTCPIP's reduced
`INGAME_DRIVER` API while preserving RA's `INGAME_UDP`. CDVDMAN debug graphs keep
their DEV9 source/import requirements, including BDM-ATA's `DEV9_DEBUG`.

PPC TTY now resolves the native `ppctty` driver from sdk-world `v2026.10.12.2`
or newer for the selected compiler. The previous pinned source fallback is
removed; `third_party/ps2sdk-ppctty` retains the AFL-2.0 license and provenance
notice for candidate distributions. Actual SDK component versions are recorded
in each build manifest. SIOCookie, IOPTRAP and the DTL boot driver also use installed packages.
PPC TTY checks for a Deckard IOP and declines residency on older IOP hardware.
Compilation alone does not establish working serial output on a console.

Nonstandard object trees are isolated under
`build/configurations/<edition>-<configuration>-<compiler>/`. Candidate ELF,
resolved definition and build manifest are staged under the matching
`dist/configurations/` directory, with the EE core map and applicable license/
provenance files. They never write `dist/editions/`, which is
reserved for standard release packaging. Both the packager and publisher reject
a manifest naming a nonstandard configuration, even if its checksums are valid.
Historical standard manifests without this new field remain verifiable.

The `ps2build-configurations` workflow covers all 14 nonstandard configurations
on Korium, plus RA without PADEMU, RA with in-game UDP debug, and IncebtIon with
extras/DS5, on both compilers. This is 34 selected builds, not every possible
edition/configuration cross-product. Host tests cover definition/ABI isolation
across all three editions and all 15 configurations, separate stage paths and
negative packaging/publishing cases.

With IGS enabled, LLVM `-Os` left only 2,712 bytes for the EE core's stack and
correctly failed its 3,072-byte linker guard. Core-only comparison builds left
8,128 bytes with GCC `-Os` and 6,040 with LLVM `-Oz`. The builder selects `-Oz`
only for LLVM's IGS-enabled core; other targets/configurations retain their
optimization settings. The memory regions and stack assertion are unchanged.
Candidate core maps retain the evidence for each build. Console screenshot
behavior and compiler performance still require hardware validation.

There is no verified compile-time no-IGR switch in the compared legacy/current
source. Its behavior/configuration audit remains in [issue #20](https://github.com/NathanNeurotic/RiptOPL/issues/20),
alongside DECI2 coverage, console
acceptance and release publication. Do not infer those capabilities from a
successful configuration build.
