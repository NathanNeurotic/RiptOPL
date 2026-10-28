# Migration plan: Makefile + ps2sdk -> ps2build

> **Current release contract (2026-10-12):** this repository uses only ps2build.
> Build Korium, RetroAchievements and IncebtIon with `tools/build_edition.py`;
> see [ROLLING_RELEASE.md](../ROLLING_RELEASE.md). The planning sections below
> describe earlier migration choices, including configurations still awaiting
> parity. They are not the supported release build commands.

Optional controller/diagnostic/debug configurations use the same edition builder;
see [BUILD-CONFIGURATIONS.md](BUILD-CONFIGURATIONS.md). They have isolated candidate
outputs and cannot be published as the three standard release kits.

Status: MAIN FLAVOUR PORTED (GSM1080P + PADEMU, no debug/RA/IGS/RTL/DTL). Other flavours still
need SDK definitions; the legacy Makefile cannot compile the converted C++ sources. Build:

    tools/install_ps2build.sh      # pinned ps2build SDK if missing + ogg/vorbis packages (not shipped by the SDK)
    tools/assets.py all            # translations, lwNBD, bin2c blobs, asm/opl_version.h
    python3 tools/build.py         # -> build/bin/riptopl-packed.elf

### GCC and LLVM setup (PR #18 and follow-up)

The LLVM source compatibility changes are in PR #18. Use ps2build and sdk-world
`v2026.10.12.2` or newer, sdk-core `v2026.10.12.1` or newer, and the separately
installed `llvm-toolchain v0.3.3` or newer.
The default suite update does not install the optional LLVM compiler:

```sh
ps2build update
ps2build update llvm-toolchain
python tools/install_ports.py --toolchain all
python tools/assets.py all
python tools/build.py --toolchain gcc --out-dir build/gcc
python tools/build.py --toolchain llvm --out-dir build/llvm
```

Use `python3` instead of `python` on Linux hosts that expose only that command.
`install_ports.py` finds the SDK beside `ps2build` on PATH, or accepts `--sdk PATH`.
It installs the matching released ogg/vorbis archives into `packages/world` for GCC and
`packages-llvm/world` for LLVM. It preserves each archive's license and reports the downloaded
archive's SHA-256. `OGG_VERSION` and `VORBIS_VERSION` can pin release tags. The existing Linux
bootstrap script delegates to it, with `PS2BUILD_TOOLCHAIN` selecting the compiler (default GCC).

Native Windows asset preparation needs Git for Windows, Python with PyYAML, and the installed
SDK. It uses Git's own shell for the existing download scripts, the running Python interpreter
for translations, and the SDK's native `bin2c.exe`; WSL is not needed for this build step.

The warning gate accepts UTF-8 and PowerShell UTF-16 logs, and Unix and Windows source paths.
Its existing baseline is for GCC; passing a build or a warning check does not establish console
acceptance, LLVM/GCC runtime equivalence, or full parity with the public repository.
The `ps2build` CI workflow builds both compilers and uploads separately named artifacts. The
existing GCC warning ratchet remains mandatory; LLVM needs its own reviewed warning policy.
Clang currently reports that `-fsingle-precision-constant` is unsupported, so floating-point code
still needs a precision/performance audit before assuming the GCC and LLVM results are equivalent.
ps2build `v2026.10.12.2` fixes project-header dependency tracking by using `-I`
for project `include_dirs` on both compilers. A local native probe changed only
a header and confirmed object rebuilds and changed bytes on GCC and LLVM,
without the previous `-Xclang -sys-header-deps` workaround. The workaround is
removed; the wrapper requires this fixed release or newer. Package/toolchain
headers remain system headers on LLVM. UART configurations now embed the native
sdk-world `ppctty` driver; the RA-only socket compatibility archive remains
necessary because released cglue still omits those socket entry points.

The native Windows LLVM `v0.3.2` linker also crashed once in its winpthreads thread pool
with exception `0xC0000008` during the October 12 parity build. An unchanged retry linked
successfully. This is an observed intermittent tool failure, not a source diagnostic or a
confirmed fix; retain the failed log and check the final link result before packaging.

The current parity ports and remaining work are tracked in
[`UPSTREAM-PARITY-2026-10-12.md`](UPSTREAM-PARITY-2026-10-12.md). The host suite now also
exercises VMC preservation, BDM private allocation recovery, C++ image/menu allocations,
queue/GUI lifetime failures, reset packet bounds and failure propagation, and the RA-only
Mode 6 snapshot handler. Host RA coverage does not establish a complete RA SDK flavour.

Emulator-tested through the C++ conversion (builds up to IoQueue/ArtQueue confirmed working); not hardware-tested.
Sources under src/ are now C++ (see CPP-CONVERSION-PLAN.md), so the Makefile can no longer build them.

CI (GitHub Actions):
- Every ps2build job installs the SDK pinned in `.github/ps2build-sdk.env` with `tools/ps2build_sdk.sh`: an
  `actions/cache` restore keyed on that file, else git.techwritescode.dev pinned per component
  (`ps2build update <component> --version <tag>`, never "latest"), else, only when that host fails, the checksummed release
  on [NathanNeurotic/ps2build-mirror](https://github.com/NathanNeurotic/ps2build-mirror). `tools/install_ps2build.sh` then
  adds the pinned ogg/vorbis packages the same way: official host first, the mirror release when it is down. `PS2BUILD_HOME`/`PS2DEV_INSTALL_DIR` point at
  `$GITHUB_WORKSPACE/.ps2dev`. To move to a newer SDK, replace the pin file with a mirror release's `pin.env` in a PR.
- `flavours.yml` builds the main flavour only; the PS2DEV/OFFICIAL x PINNED/ROLLING container matrix, DIAG and RA are gone until
  `ps2.yaml` can express them. A transient HTTP/2 stream error while downloading the toolchain was seen once (re-run fixed it).
- `check-format.yml` (clang-format 12 on `.h`/`.c`): `tests/host` is ignored; headers must be formatted with clang-format 12.
- `rolling-release.yml` still builds with `make` and will fail where it triggers; not ported.

Open items: other flavours (RA, debug/DIAG, DUALSENSE, pinned/rolling), `rolling-release` port, Makefile removal,
`usbd_mini` is built with -G0 (-G16384 broke ds34usb), audsrv (SDK `audio` + `audsrv-compat` replaces the old audsrv),
`elfldr.elf` size (143 KB), hardware testing. `dist/` (handed-out test ELFs) is git-ignored and untracked.
EE sources in src/ are now C++ (.cpp). Kept as C: src/platform/lz4.c and src/platform/zso.c (shared with the IOP
modules via #include), src/core/lang_internal.c (generated), src/platform/md5.c (vendored). C/C++ boundary headers
carry extern "C": modules/isofs/zso.h, modules/ds34bt/ee/libds34bt.h, modules/ds34usb/ee/libds34usb.h,
hddReadSectors in include/hdd.h. The Makefile can no longer build src/ (no .cpp rule).
Original plan (written before ps2build was installed) follows. ps2build v2026.09.29 (sdk-core v2026.09.29.1,
sdk-world v2026.09.30). Reference: `ps2build explain`. Install:
`curl -sSfL https://git.techwritescode.dev/ps2/install/raw/branch/master/install.sh | sh`
(glibc Linux only, no Alpine; installs to `~/.local/share/ps2build`, no other deps).

## ps2build model (from `ps2build explain`)
- `ps2.yaml` (or `ps2workspace.yaml` with `members:` for several projects in one ninja graph).
- Target types: `ee` (-> .elf), `iop` (-> .irx), `ee_lib`, `iop_lib`.
- Per-target fields: `sources` (globs ok, no `**`), `include_dirs`, `defines`, `cflags`, `ldflags`,
  `libs`, `headers`, `linkfile`, `opt`, `debug`, `embed_irx`, `output_bin`, `pack`,
  `imports_lst`, `exports_tab`.
- `embed_irx: [name]` or `[name:symbol]` runs bin2c and generates `<name>_irx` / `size_<name>_irx`.
  It also accepts installed packages that expose a driver artifact, e.g. `audio:audsrv`.
- `pack: true` replaces the Makefile's ELF packing step.
- Not supported: lib targets linking other libs; dedicated dvp type.

## Package coverage vs. what the Makefile uses
Provided by the SDK packages: kernel, cdvd, filexio, mc, patches, poweroff, vux, padx, netman,
ps2ips, ps2ip-nm, smap, dev9, libsd, bdm/bdmfs_fatfs, usbd, usbd_mini, usbhdfsd,
usbmass_bd(_mini), IEEE1394_bd(_mini), iLinkman, mx4sio_bd(_mini), eesync-nano, mcman, mcserv,
cdvdfsv, udnl, udnl-t300, smbman, mmceman/mmcedrv/mmceigr, ioptrap, siocookie, gskit, dmakit,
freetype, png, z, elf-loader-nocolour, `audio` (+ `audsrv-compat`).

GAPS (must be vendored or built in-repo):
- `libvorbis`, `libvorbisfile`, `libogg`: not in the SDK, but released at git.techwritescode.dev/ps2/{ogg,vorbis} and mirrored with the SDK; `tools/install_ps2build.sh` installs the pinned versions into packages/world.
- `libwnbdsvr` / lwNBD and `lwnbdsvr.irx`: already fetched by `tools/download_lwNBD.sh`. Pin it
  and build it as in-repo targets.
- `libaudsrv`: use `audsrv-compat` + `embed_irx: ["audio:audsrv"]`. The SDK audsrv is a different
  implementation (package `audio`), so this is a behaviour change. Hardware-test audio.
- `-lfile`, `-lint-action`, `-level`, `-list`, `-liopreboot`: grep noise from comments, not libs.
  Verify `iopreboot` really is an in-repo source.
- Fork-vendored IRXs stay in-repo as `iop` targets and shadow the packages: `usbd_mini` (pinned
  pre-2024-09-04), `bdm`, `ps2ips`, `smbman`, `iLinkman`, `IEEE1394_bd`, `mmceman`. Do not use
  the package versions of these. `mmceman` is ps2-mmce's db3e93f0 plus four fixes the package
  lacks (modules/mmce/mmceman/ORIGIN.txt); `mmcedrv`/`mmceigr` still come from the package.

## Layout
Use a `ps2workspace.yaml` (members: `modules/*`, `ee_core`, `elfldr`, top-level) so `embed_irx`
can reference targets across directories in one graph. The top-level `ps2.yaml` holds the main
`ee` target with `embed_irx` of everything the current Makefile embeds.

## Split of work
ps2build: all IOP modules, `ee_core`, `elfldr`, the main ELF (`pack: true`).
Asset script (`tools/assets.py`, replaces the non-compile Makefile parts), run BEFORE ps2build:
- fetch `download_lng`, `download_lwNBD` (pinned + sha256)
- `lang_compiler.py` -> `.lng` + internal language C/H
- version header (`oplversion`), changelog
- bin2c of resources (gfx, audio, fonts, lng/tmpl) into generated `.c`
- release zip / VPKD bundle
Generated `.c` files are listed in `sources` (globs, so the script must run first).
ps2build has no pre-build hook: CI and the dev wrapper call `assets.py && ps2build build`.

## Flavour flags
ps2build has no variables, so each flavour needs a generated or separate `ps2.yaml`
(PADEMU, DUALSENSE, GSM1080P, RETROACHIEVEMENTS, RTL, IGS, DTL_T10000, NOT_PACKED, debug modes
-> `defines`, `sources`, `embed_irx` lists). Options:
1. `tools/assets.py --flavour X` templates `ps2.yaml` from a base + flavour overlays (preferred).
2. One checked-in yaml per shipped flavour (duplication, drift risk).
Build each flavour into its own `-o build-<flavour>` dir. That kills the stale-.o/`make clean`
hazard for RETROACHIEVEMENTS. Keep GSM1080P=1 default (video-mode index stability).

## Steps
1. Baseline from the current container build: hash the ELF, every IRX (`irx_manifest.sh`),
   and the generated `.c` files.
2. Inventory: per-target source lists, defines and libs, extracted from the Makefile and
   `modules/*/Makefile` (incl. `Rules.bin.make`, `Makefile.iopglobal` flags).
3. Write `tools/assets.py`; diff its output against `make`'s generated tree.
4. Spike: `ps2build init` + one module (`modules/isofs`), confirm the `.irx` runs. Then the
   remaining IOP modules (imports_lst/exports_tab where the `.irx` exports).
5. `ee_core`, `elfldr` (custom `linkfile`), then the main ELF. Fix API drift against the new
   headers. Vendor vorbis/ogg/lwNBD here.
6. Compare vs. baseline. IRX bit-identity is unlikely; gate is a hardware boot test per flavour.
7. CI: drop `ps2dev/ps2dev` containers and the `apk add` step in `rolling-release.yml` /
   `flavours.yml`. Install ps2build (pin a version via `PS2DEV_INSTALL_VERSION`), run
   assets + build. Run old and new in parallel for one release, then delete the Makefile.
8. Update `README.md`, `ROLLING_RELEASE.md`, `build_rolling_extras.sh`.

## Risks
- SDK drift: ps2build's bundled IRX/lib sources differ from the ps2dev container's; expect
  API fixes and behaviour differences (audsrv especially).
- Flavour matrix blow-up without variables (see above).
- No pre-build hook: forgetting the asset script yields a stale or missing generated tree.
- ps2build is a dated-version tool (`v2026.09.x`): pin it in CI.
