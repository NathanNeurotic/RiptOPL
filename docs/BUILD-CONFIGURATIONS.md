# ps2build configuration candidates

The SDK has three release channels and one shared built-in multi-view theme.
Standard RiptOPL is LLVM/Clang (`rolling`), RetroAchievements is GCC
(`rolling-ra`) and standard/RA RiptOPLite are GCC (`rolling-lite`).
Korium, IncebtIon and the separate RiptOPLLVM release are historical,
not current release editions.

`--configuration` selects optional controller, diagnostic or debug behavior
within an edition. Configuration builds are not new release channels.
`rolling` carries `RIPTOPL-VARIANTS-<version>.zip` for
`pademu0`, `ds5`, `extras` and `extras_ds5`;
`RIPTOPL-DEBUG-<version>.zip` for the other nonstandard configurations;
and `RIPTOPL-BUGREPORT-<version>.zip` for the unpacked `diag` build.
`rolling-ra` carries `RIPTOPL-RA-VARIANTS-<version>.zip` with the
supported PADEMU0 and unpacked loaders. All archives preserve their
applicable manifest, licensing, source and compiler provenance.

Install the SDK described in [PS2BUILD-MIGRATION.md](PS2BUILD-MIGRATION.md)
and use the standard edition builder:

```sh
python tools/build_edition.py riptopl --configuration ds5
python tools/build_edition.py ra --configuration pademu0
python tools/build_edition.py riptopl --configuration iopcore_debug_uart --toolchain llvm
```

Omitting `--toolchain` chooses LLVM for `riptopl` and GCC for
`ra` or `lite`. Explicit alternate-toolchain builds are test
candidates, not published channel updates. Each build regenerates
edition artwork, manifests and version strings. Configuration builds
have their own object tree and output stage, separate from release kits.

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
| `ingamediag` | In-game freeze report: after a freeze (the IGR combo, a power-button tap, or 15 s with no controller data) the report shows on the TV, then a power-button tap writes `mc0:/RIPTFRZ.TXT`, including the game's IOP allocations and 0xFF overrun runs. Refused with RetroAchievements. |

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
`dist/configurations/` directory, with the EE core map, RiptOPL's `LICENSE` and `CREDITS`,
`licenses/usmb2/` (every loader embeds usmb2) and any other applicable license/
provenance files. They never write `dist/editions/`, which is
reserved for standard release packaging. Both the packager and publisher reject
a manifest naming a nonstandard configuration, even if its checksums are valid.
Historical standard manifests without this new field remain verifiable.

The `Rolling Release` PR and integration matrix verifies standard
RiptOPL and its optional configurations on LLVM, standard and PADEMU0
RetroAchievements on GCC, and eight standard plus eight RA Lite
device configurations on GCC. `ps2build-configurations` independently
checks additional requested compiler/configuration combinations.
Not every cross-product is a supported production release.
Host tests check build-definition and output-tree isolation.

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
