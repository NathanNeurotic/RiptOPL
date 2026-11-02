# RiptOPL releases on ps2build

This repository uses **one SDK: ps2build**. Its three release editions are:

| Edition | Channel | Built-in theme | Runtime |
| --- | --- | --- | --- |
| Korium | `rolling-korium` | Korium Komblete | Standard |
| RetroAchievements | `rolling-ra` | Korium Komblete | RA enabled |
| IncebtIon | `rolling-incebtion` | IncebtIon | Standard |

The RetroAchievements channel also carries the same kit built with the IncebtIon theme
(edition `ra_incebtion`), as the legacy edition's RA release carries one kit per theme.

GCC is the release compiler. GCC and LLVM are both validated by `ps2build.yml`;
they are compiler choices within ps2build, not separate SDK release flavours.
The old ps2dev/official pinned and rolling container releases are legacy-edition
history, not build targets here. No Makefile is used by the release workflow.

## Build and package

Install/update ps2build, then run from the repository root:

```sh
ps2build update
sh tools/install_ps2build.sh
python tools/assets.py fetch
python tools/assets.py lang
python tools/build_edition.py korium
python tools/build_edition.py ra
python tools/build_edition.py incebtion
python tools/build_edition.py ra_incebtion
```

Local edition builds must run sequentially: generated assets and the version
header are shared. Object files are isolated under `build/editions/<edition>-gcc`.
Each build regenerates its complete asset set, removing any prior edition's RA
mark or theme blobs. An LLVM candidate can use `--toolchain llvm` after
`ps2build update llvm-toolchain`; publishable kits require GCC.

Korium assets/configs retain their exact upstream revision, original credits,
license and per-file hashes under `third_party/korium`. The RA-only socket glue
is a small pinned implementation from ps2sdk, not another SDK installation or
toolchain. sdk-core v2026.10.12.1's libcglue archive lacks the five POSIX entry
points used by RA. The compatibility archive preserves libc's descriptor table.
Reassess it when the rolling ps2build SDK supplies those functions.

Each edition stages its ELF, resolved definition and build manifest under
`dist/editions/<edition>`. The manifest records source SHA, clean/dirty state,
edition, ps2build version, installed component versions, compiler choice,
definition/ELF hashes and CI run URL. Dirty builds are useful development probes
but are rejected by release packaging.

Download the official Neutrino `latest` release's `neutrino_*.7z` archive and run:

```sh
python tools/package_edition.py korium --neutrino-archive path/to/neutrino_BUILD.7z
```

Repeat for `ra`, `incebtion` and `ra_incebtion`. Python, PyYAML, Git and 7z are required.
Use a fresh output directory/workspace for a new package attempt; packaging
refuses to mix assets with an earlier attempt. The companion archive filename
and SHA-256 are recorded, along with the complete installed file inventory.

## Download contract

Each channel carries exactly these assets (the app/ELF names differ for RA):

- `RIPTOPL-<edition>.zip`: full install kit, including APPS, matching ART, app
  PSU, POPS/BDMA variants, licensed Ember, official Neutrino plus the UDPFS IMG
  config, external-tool shortcuts, license, credits, the libsmb2 licence and manifest.
- `RIPTOPL.ELF` / `RIPTOPL-RA.ELF`: the exact packed loader used in that kit.
- `APP_RIPTOPL.psu` / `APP_RIPTOPL-RA.psu`: only the app's direct files, with
  deterministic SAS timestamps and metadata matching the packaged ELF.
- `RIPTOPL-src.zip`: Git's source archive of the exact built commit.
- `RIPTOPL-LANGS.zip`: every translation and the font its script needs, one folder per language.
- `LICENSE-LIBSMB2.txt`: the GNU LGPL-2.1 licence of [libsmb2](https://github.com/sahlberg/libsmb2),
  by [Ronnie Sahlberg](https://github.com/sahlberg), which every loader embeds for SMB2 browsing.
  The kit and the VARIANTS, DEBUG and RA-VARIANTS zips carry the same file; the bug-report zip
  carries the same text under `licenses/libsmb2/`.
- `BUILD-MANIFEST.json` and `SHA256SUMS.txt`: build identity and asset checksums.

The Korium and IncebtIon channels also carry `RIPTOPL-VARIANTS.zip` (pademu0, ds5, extras, extras_ds5)
and `RIPTOPL-DEBUG.zip` (every debug and diagnostic configuration), each built with that channel's
theme: one folder per configuration with its RIPTOPL.ELF and manifest, each checked against the
channel kit's commit and SDK components. They also carry `RIPTOPL-BUGREPORT.zip`, the channel's
diag build before compression, with a README on what to send (see
[Launch diagnostics](docs/LAUNCH-DIAGNOSTICS.md#bug-report-build)).
The RetroAchievements channel also carries `RIPTOPL-RA-VARIANTS.zip` (pademu0) and the
IncebtIon-theme kit as `RIPTOPL-RetroAchievements-IncebtIon.zip`, `RIPTOPL-RA-IncebtIon.ELF`,
`APP_RIPTOPL-RA-IncebtIon.psu` and `RIPTOPL-RA-VARIANTS-IncebtIon.zip`; inside that kit the app is
still `APP_RIPTOPL-RA`.
See [Build configurations](docs/BUILD-CONFIGURATIONS.md).

Standard kits reject RA resources. The RA kit adds the xeRAbora client shortcut
and replaces upstream Neutrino with ORBIT's RA runtime (see
[RetroAchievements](docs/RETROACHIEVEMENTS.md#neutrino-in-the-ra-edition)). No BIOS is
redistributed.

## Preview and publication

`Rolling Release` builds and verifies all three install kits on pull requests,
integration pushes and manual dispatch. It uploads candidates as Actions
artifacts without publishing; see [Test builds](docs/TEST-BUILDS.md) for
downloading one.

Publication requires manual dispatch with `publish=true` from `rebuild/main` in
`NathanNeurotic/RiptOPL`. All three builds must pass. The publisher verifies
source SHA, clean state, SDK consistency, exact file inventory, source ZIP
commit, installed ELF/PSU bytes and all checksums before touching GitHub.
An integration branch that advanced during the build must be rebuilt instead.
GitHub-reported asset sizes/digests and final tag SHA are verified on publication.
IncebtIon owns GitHub's Latest marker; Korium and RA have their own stable URLs.

The three GitHub release updates are sequential, not an atomic transaction.
A network/API failure can leave a partial publication and must be inspected
before retrying. No historical production releases are deleted or rewritten.

Build/test/package success is separate from console acceptance. Test the exact
edition and source SHA on the relevant console, storage backend, core, settings,
game launch, save/VMC and IGR paths before calling a change console-tested.
