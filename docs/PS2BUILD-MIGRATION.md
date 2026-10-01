# Migration plan: Makefile + ps2sdk -> ps2build

Status: MAIN FLAVOUR PORTED (GSM1080P + PADEMU, no debug/RA/IGS/RTL/DTL). Other flavours still
build through the Makefile. Build:

    tools/install_ps2build.sh      # ps2build + ogg/vorbis packages (not shipped by the SDK)
    tools/assets.py all            # translations, lwNBD, bin2c blobs, asm/opl_version.h
    ps2build build                 # -> build/bin/riptopl-packed.elf

Not hardware-tested. Open items: other flavours, `usbd_mini` -G16384 equivalence, audsrv
(SDK `audio` + `audsrv-compat` replaces the old audsrv), `elfldr.elf` size (143 KB), CI cutover
(.github/workflows/ps2build.yml runs alongside the old jobs), Makefile removal.
EE sources in src/ are now C++ (.cpp). Kept as C: src/lz4.c and src/zso.c (shared with the IOP
modules via #include), src/lang_internal.c (generated), src/md5.c (vendored). C/C++ boundary headers
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
- `libvorbis`, `libvorbisfile`, `libogg`: not in the SDK, but released at git.techwritescode.dev/ps2/{ogg,vorbis}; `tools/install_ps2build.sh` installs them into packages/world.
- `libwnbdsvr` / lwNBD and `lwnbdsvr.irx`: already fetched by `tools/download_lwNBD.sh`. Pin it
  and build it as in-repo targets.
- `libaudsrv`: use `audsrv-compat` + `embed_irx: ["audio:audsrv"]`. The SDK audsrv is a different
  implementation (package `audio`), so this is a behaviour change. Hardware-test audio.
- `-lfile`, `-lint-action`, `-level`, `-list`, `-liopreboot`: grep noise from comments, not libs.
  Verify `iopreboot` really is an in-repo source.
- Fork-vendored IRXs stay in-repo as `iop` targets and shadow the packages: `usbd_mini` (pinned
  pre-2024-09-04), `bdm`, `ps2ips`, `smbman`. Do not use the package versions of these.

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
- bin2c of resources (gfx, audio, fonts, lng_tmpl) into generated `.c`
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
