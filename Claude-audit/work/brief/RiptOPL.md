# RiptOPL orientation brief

Audited commit `b00e773bf28c1790b458ab53f25caad69e79f237` (2026-11-16). Paths are relative to the repo root. INFERRED marks anything not directly verified. Inventory: 2381 tracked files (`inventory/RiptOPL.tsv`).

## 1. What the project is

RiptOPL is a fork of Open PS2 Loader (Ifcaro, jimmikaelkael), a PS2 homebrew game loader with a menu ELF, an in-game EE core and many IOP modules (`README.md:21-25`). This repo is the "C++ / ps2build edition" main line; the C code base continues as the legacy NathanNeurotic/Open-PS2-Loader edition (`README.md:17-19`). It ships several editions (Korium, RetroAchievements, IncebtIon, per-device Lite) plus debug and variant builds (`ROLLING_RELEASE.md:3-16`).

Mostly upstream or upstream-derived: `ee_core/`, `elfldr/`, `modules/iopcore/`, `modules/mcemu/`, `modules/pademu/`, `modules/ds34bt/`, `modules/ds34usb/`, `modules/vmc/genvmc/`, `modules/network/SMSTCPIP`, `SMSUTILS`, `smap-ingame` (Open PS2 Loader headers; fork edits inside), all of `third_party/`, and the vendored module trees in section 5. Mostly first-party: `src/` (120 files, 90,518 lines, C++), `tools/`, `tests/`, `.github/scripts/`, `docs/`.

## 2. Build

- Real build: ps2build reading `ps2.yaml` (670 lines). `tools/build.py` runs `ps2build build --toolchain X --out-dir Y --config ...` and requires ps2build >= v2026.10.13 (`tools/build.py:15`). Pins: `.github/ps2build-sdk.env`. GCC and LLVM toolchains; LLVM builds eecore with -Oz (`tools/build.py:69-75`).
- Asset step: `tools/assets.py` (fetch, lang, embed, version) replaces the Makefile asset rules. It bin2c's PNG, font, audio and misc files into gitignored `asm/*.c` (`tools/assets.py:132-188`) and writes `asm/opl_version.h` (`:217-224`).
- Editions: `tools/build_edition.py` (`EDITIONS :18-25`, `LITES :33-36`, `definition() :72-167`) rewrites ps2.yaml in memory. Configurations: `tools/build_configuration.py` (`CONFIGURATIONS :7-11`). Lite: `tools/lite_devices.py` (`DEVICES :22-42`).
- Shipped artifacts (`ROLLING_RELEASE.md:94-119`, `tools/build_edition.py:217-246`): `RIPTOPL.ELF` / `RIPTOPL-RA.ELF` (packed), `APP_RIPTOPL*.psu`, `RIPTOPL-<edition>.zip`, `RIPTOPLITE-<DEVICE>.ELF`, loose `IEEE1394_bd_mini.irx` and `iLinkman.irx`, `RIPTOPL-LANGS.zip`, `RIPTOPL-src.zip`, `BUILD-MANIFEST.json`, `SHA256SUMS.txt`, VARIANTS/DEBUG/BUGREPORT zips.
- Base defines: `GSM_1080P, PADEMU, IGR_MENU` for eecore (`ps2.yaml:609`) and the main ELF (`ps2.yaml:644`).
- Overlay flags (off unless the edition or configuration selects them): `BUILTIN_KORIUM` (`build_edition.py:84`); `RETROACHIEVEMENTS`, `IGR_OVERLAY` (`:85-92`); `RIPTOPL_UPDATER` with `RIPTOPL_UPDATE_TAG_*` (standard builds only, `:130-145`); `RIPTOPLITE_<DEV>` (`lite_devices.py:22-42`); pademu off (`build_configuration.py:23-27`); `DS5_ENABLE` (`:28-29`); extras drop `IGR_MENU` (`:30-36`) and add `__RTL`, `IGS` (`:37-40`); `_DTL_T10000` (`:41-44`); `__OPLDIAG` (`:45-52`); `INGAMEDIAG` (`:57-78`); `__DEBUG`, `TTY_UDP`/`TTY_PPC_UART` (`:79-95`); `__INGAME_DEBUG`, `__LOAD_DEBUG_MODULES`, `__IOPCORE_DEBUG` (`:96-112`); `__EESIO_DEBUG` (`:113-117`).
- Legacy: the top-level `Makefile` (1260 lines) and per-module Makefiles are not the build (`README.md:890-899`, `ROLLING_RELEASE.md:44`, `.github/workflows/flavours.yml:401`). The Makefile still lists flat `src/*.c` objects that no longer exist. Its variables, for comparison: `EXTRA_FEATURES ?= 0` (:28), `RTL` (:32), `IGS` (:34), `PADEMU ?= 1` (:37), `DUALSENSE ?= 0` (:43), `GSM1080P ?= 1` (:57), `RETROACHIEVEMENTS ?= 0` (:72), `DTL_T10000 ?= 0` (:75), `NOT_PACKED ?= 0` (:78), `DEBUG`/`EESIO_DEBUG`/`INGAME_DEBUG`/`DECI2_DEBUG` (:81-84), `OPLDIAG ?= 0` (:89), `TTY_APPROACH ?= UDP` (:91). Only CI use of make: `modules/bdmassault/ilink_bd_assault` (`flavours.yml:363-398`). `tools/lng_pack.sh` and `.github/scripts/build_rolling_extras.sh` still call make.
- CI: `ps2build.yml` (gcc+llvm, tests, warning gate), `configurations.yml` (17 targets x 2 toolchains), `flavours.yml` (host tests), `rolling-release.yml` (matrix `:30-80`), `rolling-llvm.yml`, `check-format.yml` (clang-format 12 on `.c`/`.h` only), `docs-sync.yml`, `wiki-publish.yml`, four scheduled watchers.

## 3. What is compiled into the shipped product

- Main ELF target `riptopl` (`ps2.yaml:639-652`): source list at `:641` (`src/app`, `src/core`, `src/gfx`, `src/platform`, `src/sources`, `src/ui` .cpp files, `src/core/lang_internal.c`, `src/platform/zso.c`, `lz4.c`, glob `asm/*.c`); libs `:650`; `pack: true` `:651`.
- Added only by overlays: `src/platform/md5.c`, `ratls.c`, `selfupdate.c`, `debug.cpp`; `src/ui/selfupdate.cpp`; `src/sources/` rawatch, rahash, ranet, rabadge, discsupport, achievements, rahunt, rapopslaunch, ratls_ps2 (`build_edition.py:96-98,130-145`; `build_configuration.py:79-95`).
- `eecore` (`ps2.yaml:604-613`, from `ee_core/`, -Os) and `elfldr` (`:615-624`) are EE programs embedded in the main ELF. RA adds `ee_core` ra.c, ra_overlay.c, ra_pulse.c (`build_edition.py:108`) and the `igrovl` overlay (`:157-166`). `ds34usb_ee`, `ds34bt_ee` are EE libs (`ps2.yaml:627-636`).
- IOP modules built from repo sources: `ps2.yaml:4-601` (isofs, bdmevent, imgdrv, resetspu, igrmbox, patches, genvmc, cdvdfsv, SMSUTILS, smbinit, nbns, httpclient, raudp, xhdd, udptty_ingame, ps2ips_fork, smbman_ra, smb2man, nfsman, smap_ingame, SMSTCPIP, lwnbdsvr, smap_udpbd, five udpfs modules, ata_bd, ps2hdd_osd, bdm, bdmfs_fatfs, iLinkman, IEEE1394_bd, mmceman, mx4sio_bd, usbmass_bd, usbd; cdvdman variants `:394-530`; mcemu variants `:539-563`; pademu `:571-596`).
- IRX taken prebuilt from ps2build SDK packages (no repo source): pvrdrv, dvrfile, usbhdfsd, libsd, audsrv, hdpro_atad, poweroff, dev9, ps2ip-nm, smap, netman, iomanx, filexio, sio2man, padman, mcman, mcserv, mmcedrv, mmceigr, fs-osd, eesync-nano, udnl.
- Embedding: every IRX in `embed_irx` (`ps2.yaml:652`) is linked into the ELF as a blob and loaded at runtime with `sysLoadModuleBuffer` (`src/platform/system.cpp`). Lite replaces dropped IRX with zero-size stubs in generated `build/lite/<dev>/lite_irx_stubs.c` (`lite_devices.py:81-112`).
- Embedded data: `gfx/` PNGs, `third_party/fonts/PoeVeticaNew.ttf`, `audio/*.adp`, `misc/` icons and theme cfgs, `modules/iopcore/IOPRP.img`, `third_party/ca-roots/*.der` (RA/updater), Korium assets from `third_party/korium/assets.zip` (`tools/assets.py:46-75,132-188`).

## 4. What is NOT shipped

- `tests/` (110 files: host, nfs-live, pccf, ratls, smb2-live), `ee_core/tests/`, `modules/network/udpfs_ioman/tests/`, `modules/network/udpfs_shared/tests/`: run by `tools/run_tests.sh` and `tests/host/run.sh`.
- `.github/scripts/` (293 entries): about 280 `test_*.py` source-text tests run by `flavours.yml`; `site_sync.py`, `wiki_sync.py`, `orbit_neutrino_watch.py`, `ps2build_sdk_watch.py` run by workflows; `package_release_app.py` imported by `tools/package_edition.py:18`; `hostmap.py`, `normalize_release_notes.py`, `ripps2_text_cases.inc` used by tests. `build_rolling_extras.sh`, `irx_manifest.sh`, `asset_version.sh`, `release_channel.sh`, `validate_site.py`: no workflow caller found (references only in docs, the legacy Makefile `:61,:113`, or themselves).
- `tools/`: build, packaging, publish, lint and translation scripts; `tools/hwtest/smb2probe` (targets `ps2.yaml:655-670`, "not shipped in any package"); `tools/pc/` host utilities (iso2opl, opl2iso, genvmc, ziso.py, apa-recovery).
- `docs/` (159 files, 64.4 MB; `docs/assets` 53.7 MB, `docs/community-report-evidence` 9.5 MB): user guides plus dated plans, audits and checkpoints; read by `docs-sync.yml` / `wiki-publish.yml`.
- `ship/` (182 files, 9.6 MB): third-party payloads copied into install kits, not the ELF (`tools/package_edition.py:137,167`). `ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/` (137 files, a directory mirroring `ship/`) is referenced by `.github/scripts/package_release_app.py:24`.
- `modules/bdmassault/` prebuilt drivers, `modules/freepad/PROVENANCE.md`, `modules/debug/` DECI2 blobs, `third_party/PSUManager`, `third_party/ps2sdk-socket`, `third_party/ps2sdk-ppctty`: see sections 5 and 8.
- `lng/` (36 files): language sources compiled to `.lng` and packaged separately as `RIPTOPL-LANGS.zip`.

## 5. Vendored third-party code and generated files

Vendored, with provenance file:
- `third_party/mbedtls/` (280 files, 205,284 lines): mbedTLS 3.6.7 archive subset including upstream-generated sources (README).
- `third_party/PSUManager/` (README, pinned commit; packaging validator only).
- `third_party/ps2sdk-socket/` (UPSTREAM: "retired from build graphs"); `third_party/ps2sdk-ppctty/` (UPSTREAM: no bundled driver built).
- `modules/bdm/` (ORIGIN.txt, ps2sdk bdm + libbdm); `modules/bdm/bdmfs_fatfs/` (ORIGIN.txt, ps2sdk + FatFs).
- `modules/hdd/apa/`, `modules/hdd/atad/` (ORIGIN.txt; atad changes marked `/* FORK */`).
- `modules/ilink/iLinkman/`, `modules/ilink/IEEE1394_bd/` (ORIGIN.txt).
- `modules/mmce/mmceman/` (ORIGIN.txt; fixes kept as `patches/*.patch`).
- `modules/mx4sio_bd/`, `modules/usb/usbmass_bd/` (ORIGIN.md).
- `modules/network/smb2man/usmb2/`, `modules/network/nfsman/unfs4/` (VENDORED.txt).
- `modules/network/udpfs_smap`, `udpfs_ministack`, `udpfs_bd`, `udpfs_ioman`, `udpfs_fhi`, `udpfs_shared`, `smap_udpbd` (ORIGIN.txt each; neutrino / NHDDL).
- `modules/debug/ps2link/` (own LICENSE; UDP debug configs only).

Vendored, provenance only in comments:
- `modules/usb/usbd-ra/` (`Makefile:744-764`), `modules/network/ps2ips/` (`Makefile:962-982`), `modules/network/smbman-ra/` (`Makefile:989-1014`).
- `modules/isofs/lz4.c`, `lz4.h` (LZ4); `src/platform/md5.c` + `include/md5.h` (Aladdin md5); `modules/network/smbinit/des.c`, `md4.c`; `src/platform/tar.cpp` (ported from wOPL); `src/core/xparam.cpp` title table (DECKARD configs).
- `modules/network/SMSTCPIP` (57 files, 16,720 lines): lwIP-derived, INFERRED from `LWIP_NOASSERT` at `ps2.yaml:199`.
- `modules/pccf/`: carries `PS2SDK_LICENSE.txt`; origin not read (INFERRED partly ps2sdk-derived).

Data tables and binary assets:
- `modules/bdm/bdmfs_fatfs/fatfs/ffunicode.c` (1.97 MB, 15,593 lines): FatFs Unicode tables.
- `third_party/ca-roots/*.der`, `third_party/korium/assets.zip` + `UPSTREAM.json`, `third_party/fonts/PoeVeticaNew.ttf`, `third_party/orbit-neutrino/release.json` (pin only).
- Prebuilt binaries: `modules/bdmassault/usbd.irx.*`, `usbhdfsd.irx.*` (PROVENANCE.md); `modules/debug/*.irx`, `deci2.img`; `modules/iopcore/IOPRP.img`; `ship/EMBER/ember.elf`; `ship/POPS/*`; `.github/scripts/fixtures/`.

Generated and gitignored (`.gitignore:58-74,110-115`): `asm/*.c`, `asm/opl_version.h`, `src/core/lang_internal.c`, `include/lang_autogen.h`, `lng/lang_*.lng`, `modules/network/lwNBD` (downloaded), `build/`, `dist/`, `.ps2build-*.yaml`.

## 6. Runtime architecture

- Entry: `main` at `src/app/opl.cpp:2054`; thread priority 31 (`:2064`); `setBootDir(argv[0])` (`:2078`); autolaunch on argv (`:2090-2107`); `init()` (`:2109`); deferred init queued with `ioPutRequest` (`:2116`); `guiIntroLoop()` (`:2118`); `guiMainLoop()` (`:2131`).
- UI loop: `Gui::mainLoop()` at `src/ui/gui.cpp:324-380` (per frame: start frame, pad poll, draw, overlays, notifications, `execDeferred`, end frame, input dispatch). Main-thread waits: `guiHandleDeferedIO` (`src/ui/guiwait.cpp:149`) and `Gui::renderProbeFrame()` (`gui.cpp:396`).
- I/O model: one IO worker thread with a request queue (`src/platform/ioman.cpp`; static 96 KB stack `:42-44`, `CreateThread :253`, `ioInit :412`, `ioPutRequest :417`, `ioBlockOpsTimed :467`, `ioBlockOps :472`). List refresh goes through `IO_MENU_UPDATE_DEFFERED` (`opl.cpp:1691`).
- Other EE threads: art worker (`src/gfx/artload.cpp:842-848`), BGM decode and BGM I/O (`src/core/bgm.cpp:689,701`), menu watchdog in diag builds (`src/ui/menuwatch.cpp:577`).
- Config: `settings_riptopl.cfg`, legacy `conf_riptopl.cfg` (`include/config.h:28-29`); `conf_last.cfg`, `conf_apps.cfg`, `conf_network.cfg`, `conf_game.cfg`, `mc?:SYS-CONF/IPCONFIG.DAT` (`src/core/config.cpp:27-33`); base dir `mc?:OPL` (`opl.cpp:1502`); `hdd0:__common/OPL/conf_hdd.cfg` (`src/app/settingsio.cpp:2210`); saves through `guiHandleDeferedIO(..., &_saveConfig, ...)` (`settingsio.cpp:2569`); `favourites.bin`, `recents.bin` (`include/favsupport.h:8-9,184`).
- Other card writes: `mc?:/SYS-CONF/USBD.IRX`, `USBHDFSD.IRX` (`src/sources/supportbase.cpp:924-977`); `mc?:/POPSTARTER/{usbd.irx,usbhdfsd.irx}` (`src/sources/vcdsupport.cpp:1210-1224`); `mc0:/RIPTFRZ.TXT` in ingamediag builds (`ee_core/src/freeze_diag.c:250`).
- Network, menu side: dev9, netman, smap, ps2ip, fork ps2ips, httpclient loaded in `src/sources/netsupport.cpp:192-215`; SMB1/SMB2/NFS/NBNS in `src/sources/ethsupport.cpp:476-581`; mbedTLS via `src/platform/ratls.c`; NBD server `src/app/lwnbdctl.cpp`. In-game: SMSTCPIP + smap_ingame with cdvdman's own clients (`modules/iopcore/cdvdman/smb.c`, `smb2.c`, `nfs.c`), UDPFS/UDPBD chains.
- Device backends: `src/sources/` (supportbase, bdmsupport, hddsupport, ethsupport, mmcesupport, udpfssupport, httpsupport, appsupport, vcdsupport, favsupport). External cores: `src/platform/sysneutrino.cpp`.

## 7. Repo conventions relevant to dead or duplicate code

- `AGENTS.md` and `CLAUDE.md` are identical and contain only an attribution policy; no code-level "do not remove" rules.
- `.clang-format-ignore` is the do-not-reformat list for vendored trees (plus `tests/host`).
- Vendored trees keep ORIGIN/VENDORED/PROVENANCE files; several state a byte-identical baseline, so fork changes are reviewable as later commits.
- Naming: `_fork` suffix in ps2.yaml targets means a repo-local copy replacing an SDK package; `-ra` suffix is historical (`Makefile:744-764`).
- Intentional duplicates: per-device cdvdman and mcemu variants built from one source with different defines; `udpfs_bd`/`udpfs_fhi`/`udpfs_ioman` carry identical headers and one-line wrappers including `udpfs_shared/src/*.c`; `src/platform/lz4.c` and `zso.c` include `modules/isofs` sources; `flavours.yml:363-398` requires three `usbhdfsd.irx.ilink` copies to be byte-identical; `ship/Example/` mirrors `ship/`.
- Generated files are never committed (section 5).
- Load-bearing comments: `GSM1080P` default (`Makefile:49-55`); `bgm.ogg` not embedded, "do not add bgm.o back here" (`Makefile:178-183`).
- `lite_devices.py:100-103` fails the build if a ps2.yaml module is unclassified.
- Mechanical evidence already exists in `evidence/RiptOPL/` (dupes, cond, unreferenced symbols and files).

## 8. Open questions for the audit lead

1. First-party files inside vendor folders: `third_party/mbedtls/riptopl_mbedtls_config.h`, `riptopl_mbedtls_slim_config.h`; `modules/hdd/apa` table fence; `modules/network/smb2man/smb2man.c`; `modules/network/nfsman/nfsman.c`; `modules/mmce/mmceman/patches/`; `modules/bdmassault/ilink_bd_assault/`.
2. Vendored code inside first-party folders: `src/platform/md5.c`, `src/platform/tar.cpp`, `src/core/xparam.cpp` table, `modules/isofs/lz4.*`.
3. Is the legacy `Makefile` tree (top level and per module) in audit scope as live code, or as clutter only?
4. `modules/bdmassault/PROVENANCE.md` says the drivers are embedded gzipped; no `src/` or `tools/` reference to that folder was found in this edition.
5. `modules/debug` DECI2 blobs: no ps2build configuration defines `__DECI2_DEBUG` (INFERRED unused).
6. `modules/freepad/PROVENANCE.md` names `.github/scripts/install_coherent_freepad.sh`, which is absent.
7. Treat inherited Open PS2 Loader trees (`ee_core`, `modules/iopcore`, SMSTCPIP) as first-party or upstream-derived?
8. Are `docs/` media and `ship/` payloads (74 MB together) in scope for the clutter category?
