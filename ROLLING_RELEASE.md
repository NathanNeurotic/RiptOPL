# RiptOPL releases on ps2build

This repository uses one pinned **ps2build SDK** and three release channels:

| Edition | Channel | Release compiler | Built-in theme |
| --- | --- | --- | --- |
| RiptOPL (GitHub Latest) | `rolling` | **LLVM/Clang** | Unified `<OPL>` multi-view theme |
| RetroAchievements | `rolling-ra` | **GCC** | The same unified theme |
| RiptOPLite and RiptOPLite RA | `rolling-lite` | **GCC** | The same unified theme |

Korium and IncebtIon are retired as **separate editions**. The historical
`rolling-llvm` release is superseded by LLVM becoming the standard RiptOPL
compiler. Existing release history, third-party licenses and human credits
remain intact. RetroAchievements and every Lite variant stay on GCC until
LLVM performance and hardware behavior have been accepted.

The full RiptOPL and RetroAchievements editions retain their install kits,
language packs, release bundles, verified Neutrino companions and separate
compiler provenance. RiptOPLite carries device-specific standalone ELFs;
RiptOPLite RA adds its required pinned RA companion runtime in each device ZIP.
The supported Lite game transports are USB, MX4SIO, i.LINK, HDD, SMB,
UDPBD, UDPFS and MMCE (eight standard and eight RA builds).

All editions use the same theme configuration, code and embedded theme assets,
regardless of compiler. GCC and LLVM validation is not a substitute for
testing the actual devices, launch cores, VMC and IGR paths on a PS2.

## Build and package

Install/update the pinned SDK and assets, then build from the repository root:

```sh
SDK="${PS2DEV_INSTALL_DIR:-$HOME/.local/share/ps2build}"
sh tools/ps2build_sdk.sh --llvm "$SDK"
PS2BUILD_TOOLCHAIN=all sh tools/install_ps2build.sh
python tools/assets.py fetch
python tools/assets.py lang
python tools/build_edition.py riptopl
python tools/build_edition.py ra
python tools/build_edition.py lite --device usb
python tools/build_edition.py lite_ra --device usb
```

By default, `riptopl` selects LLVM and `ra`, `lite` and `lite_ra`
select GCC. Specify `--configuration` for optional full-edition builds,
or `--toolchain` for an explicitly experimental compiler candidate;
the latter cannot take over the production updater channel.

Build editions sequentially when sharing one checkout: assets and the version
header are regenerated for every build, while compiler-specific object trees
remain isolated. The pinned SDK and selected compiler are recorded in the
build manifest. The RA-only socket compatibility code preserves libc's
descriptor table, without installing a second SDK.

Standard full editions stage their ELF, resolved definition and build manifest
under `dist/editions/<edition>`. Lite builds instead stage under
`dist/lite/<device>-gcc` and Lite RA under `dist/lite/ra-<device>-gcc`.
The manifest records source SHA, clean/dirty state,
edition, ps2build version, installed component versions, compiler choice,
definition/ELF hashes and CI run URL. Dirty builds are useful development probes
but are rejected by release packaging.

For a standard full RiptOPL kit, download the official Neutrino
`latest` release's `neutrino_*.7z` archive and package:

```sh
python tools/package_edition.py riptopl --neutrino-archive path/to/neutrino_BUILD.7z --compiler llvm
python tools/package_edition.py ra --compiler gcc
```

The RA packager uses its pinned and verified RA runtime instead of the
upstream archive. Python, PyYAML, Git and 7z are required where applicable.
Package from a fresh staging directory, not a mixture of source revisions
or previous artifacts.

## Download contract

The two full channels each carry:

- A versioned install kit: `RIPTOPL.zip` for RiptOPL,
  `RIPTOPL-RetroAchievements.zip` for RA before publication renaming.
- The matching packed loader: `RIPTOPL.ELF` or `RIPTOPL-RA.ELF`,
  and `APP_RIPTOPL.psu` or `APP_RIPTOPL-RA.psu`.
- Exact-source snapshot, translation archive, build manifest, SHA-256
  checksum list, upstream credits and applicable licenses, including usmb2.
- Full installation resources, including the correct PS1/POPS/BDMA and
  Neutrino companions. RA ships the pinned RA runtime and its required files.

`rolling` also publishes `RIPTOPL-VARIANTS.zip` (PADEMU0, DS5,
extras and extras+DS5), `RIPTOPL-DEBUG.zip` (debug and diagnostic
configurations), and `RIPTOPL-BUGREPORT.zip` (unpacked diag build).
`rolling-ra` publishes `RIPTOPL-RA-VARIANTS.zip`, including the
supported PADEMU0 and unpacked RA loaders.

`rolling-lite` publishes eight `RIPTOPLITE-<DEVICE>.ELF` files,
eight `RIPTOPLITE-RA-<DEVICE>.zip` files containing their pinned
RA companions, and verified licenses, source/provenance, checksum and
manifest assets. There is no Lite full-install kit. Published archive
filenames carry version suffixes; the installed ELF and PSU filenames
remain stable. See [RiptOPLite](docs/RIPTOPLITE.md) and
[Build configurations](docs/BUILD-CONFIGURATIONS.md).

## Preview and publication

`Rolling Release` builds and verifies both full install kits and the Lite release on pull requests,
integration pushes and manual dispatch. It uploads candidates as Actions
artifacts without publishing; see [Test builds](docs/TEST-BUILDS.md) for
downloading one.

Publication requires a verified integration push or manual dispatch with `publish=true` from `rebuild/main` in
`NathanNeurotic/RiptOPL`. All required channel builds must pass. The publisher verifies
source SHA, clean state, SDK consistency, exact file inventory, source ZIP
commit, installed ELF/PSU bytes and all checksums before touching GitHub.
An integration branch that advanced during the build must be rebuilt instead.
GitHub-reported asset sizes/digests and final tag SHA are verified on publication.
RiptOPL (`rolling`) owns GitHub's Latest marker; RetroAchievements and RiptOPLite have their own stable URLs.

The three GitHub release updates are sequential, not an atomic transaction.
A network/API failure can leave a partial publication and must be inspected
before retrying. No historical production releases are deleted or rewritten.

Build/test/package success is separate from console acceptance. Test the exact
edition and source SHA on the relevant console, storage backend, core, settings,
game launch, save/VMC and IGR paths before calling a change console-tested.
