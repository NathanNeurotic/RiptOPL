# RiptOPL build evidence (Korium edition, standard configuration, GCC)

Audited commit `b00e773bf28c1790b458ab53f25caad69e79f237` (2026-11-16T12:16:05-08:00), built in the separate copy `build/RiptOPL` (git status clean, no tracked file changed). `src/RiptOPL` was only read. Nothing was run on a console or emulator.

This file finishes the work of an earlier build-evidence agent that hit a usage limit before writing a summary. Its build finished (`[1335/1335] PS2PACK`, "Built Korium: 1d8030d7…", `raw/build-korium-gcc.full.log` last 10 lines), so I did not rebuild. I reused its map, nm, warning and cross-reference outputs, added the missing tables (artifacts, top symbols, blobs, IRX, flags, stack/heap), and ran a second, whole-translation-unit warning pass, which its last script edit had prepared but never run.

All `raw/...` paths are relative to `evidence/RiptOPL/`. Build-tree paths are relative to `build/RiptOPL/build/editions/korium-gcc/` unless they say otherwise.

## 0. Commands, tools, deviations

Build (earlier agent). Its exact command line was not saved. I reconstructed it from the log: the log starts with `date` output, then the `tools/assets.py embed` bin2c lines and `OPL_VERSION v1.2.0-Beta-4284-b00e773-PS2BUILD-Korium-GCC`, then ninja; it ends with `tools/build_edition.py`'s "Built Korium: <sha>" (`tools/build_edition.py:263`) and `time` output (`real 59m50.102s`). The command was therefore, INFERRED:

```
cd build/RiptOPL && date && time python tools/build_edition.py korium --toolchain gcc   # configuration defaults to standard
```

`tools/build_edition.py:209-210` runs `tools/assets.py embed` and `version`, and `tools/build.py:45` runs `ps2build build --toolchain gcc --out-dir build/editions/korium-gcc …`. Outputs: `build/RiptOPL/build/editions/korium-gcc/` (ninja tree, `opl.map`, `eecore.map`) and `build/RiptOPL/dist/editions/korium/` (`BUILD-MANIFEST.json` says `"configuration": "standard"`, `"compiler": "gcc"`, `"dirty": false`).

Analysis commands run in this session (all read the build tree and write only to `evidence/RiptOPL/`):

```
python -I evidence/RiptOPL/scripts/finish_evidence.py build/RiptOPL/build/editions/korium-gcc build/RiptOPL/dist/editions/korium evidence/RiptOPL/raw <ps2build root>
python -I evidence/RiptOPL/scripts/extra_warnings.py <build dir> <build/RiptOPL> raw/extra-warnings-S.full.log raw/extra-warnings-S.tsv 12   # 468 TUs, 0 failed, 3m50s
```

`finish_evidence.py` calls `mips64r5900el-ps2-elf-nm -S -C --size-sort -r` and `nm` on `obj/riptopl/riptopl.unstripped.elf` and `obj/eecore/eecore.unstripped.elf`. It also parses `compile_commands.json`, `build.ninja`, `dist/editions/korium/ps2.resolved.yaml`, `raw/riptopl-per-object.tsv` and `raw/extra-warnings.tsv`. The earlier agent's scripts are `scripts/mapstats.py` (map parsing), `scripts/xref.py` (cross-references), `scripts/group_warnings.py` and `scripts/extra_warnings.py`.

Tool versions (`raw/tool-versions.txt`): ps2build v2026.11.06; sdk-core v2026.11.05, sdk-world v2026.11.05, toolchain v2026.11.04, tools v2026.11.04, llvm-toolchain v2026.11.05; EE `mips64r5900el-ps2-elf-gcc` 15.3.1 20260823 with GNU ld 2.45.1.20260126; IOP `mipsel-none-elf-gcc` 15.2.0 with GNU ld 2.45.1; ninja 1.13.2; Python 3.14.7 with PyYAML 6.0.3. lwNBD (downloaded, gitignored) is at commit <redacted>.

Deviations:
1. SDK version: the repo pins `ps2build=v2026.11.10 sdk-core=v2026.11.10` (`.github/ps2build-sdk.env`, quoted in `raw/tool-versions.txt`), but the installed SDK is ps2build v2026.11.06 with sdk-core v2026.11.05. It meets the `tools/build.py:15` minimum (v2026.10.13). The installed SDK was kept, with no system-wide install. Sizes may differ slightly from a CI build.
2. Scope: only one edition was built (Korium, standard configuration, GCC). RA, Lite, IncebtIon, LLVM, debug and variant configurations were not built, so anything reported as unused or discarded here may still be used in those builds.
3. Packaging: `tools/package_edition.py` was not run, so no `.psu`, zip or `SHA256SUMS` was produced. The `dist/` folder holds only what `build_edition.py` copies.
4. The lwNBD download (`modules/network/lwNBD`) was fetched by the earlier agent (INFERRED: `tools/assets.py fetch`, which is not called by `build_edition.py`).
5. Evidence-only warning passes recompiled first-party TUs outside ninja with extra `-W` flags, writing output to `NUL` (no build-tree writes). They are not the shipped flag set.
6. Log trimming: `raw/extra-warnings.full.log` (1.89 MB) and `raw/extra-warnings-S.full.log` (2.57 MB) were reduced to `### <tu>` headers plus warning and error lines (`raw/extra-warnings.trimmed.log`, `raw/extra-warnings-S.trimmed.log`). The full build log is kept as is (166 KB).
7. No ps2.yaml or tracked-file edits and no command-line overrides: every map came from the repo's own ldflags (`ps2.yaml:613`, `ps2.yaml:649`).

## 1. Produced artifacts

Source: `raw/artifacts.tsv` (os.stat of every file in `bin/` and `dist/editions/korium/`).

| artifact | bytes | notes |
|---|---:|---|
| `dist/editions/korium/RIPTOPL.ELF` | 2,275,504 | shipped, packed (identical size to `bin/riptopl-packed.elf`) |
| `bin/riptopl.elf` | 5,116,992 | stripped, unpacked (the input to ps2pack) |
| `obj/riptopl/riptopl.unstripped.elf` | (debug) | `size -A` total 7,692,068 incl. 1,234,559 debug bytes (`raw/riptopl-size-A.txt`) |
| `dist/editions/korium/IEEE1394_bd_mini.irx` | 10,577 | shipped loose AND embedded (section 6) |
| `dist/editions/korium/iLinkman.irx` | 15,713 | shipped loose AND embedded |
| `dist/editions/korium/BUILD-MANIFEST.json` | 1,568 | |
| `dist/editions/korium/ps2.resolved.yaml` | 71,165 | resolved build definition |
| `bin/eecore.elf` | 73,008 | embedded |
| `bin/elfldr.elf` | 21,476 | embedded |
| `bin/smb2probe.elf` | 727,440 | hardware-test tool, not shipped (brief section 4) |
| 66 `bin/*.irx` | see section 6 | |

Packing: ps2pack reports "File compressed, from 5116992 to 2275504 bytes, ratio = 55.53%" (`raw/build-korium-gcc.full.log`, near its end). The packed file is 44.47% of the unpacked size (`raw/artifacts.tsv` footer). The packed ELF has one LOAD segment at 0x01ad6ff0 with FileSiz 0x22b850 and MemSiz 0x230a58 (`raw/riptopl-readelf-headers.txt`).

## 2. Main EE ELF (unpacked)

### Section sizes

From `raw/riptopl-size-A.txt` (`size -A bin/riptopl.elf`):

| section | bytes |
|---|---:|
| .text | 2,102,648 |
| .data | 2,661,826 |
| .rodata | 350,480 |
| .bss | 1,341,532 |
| .eh_frame | 888 |
| .ctors | 40 |
| .reginfo / .MIPS.abiflags / .sdata | 24 / 24 / 4 |

One LOAD segment covers 0x00100000 to 0x007289dc, with FileSiz 0x4e1104 (5,116,164) and MemSiz 0x6289dc (6,457,820) (`raw/riptopl-readelf-headers.txt`). The load image therefore ends at `_end` = 0x007289dc (`raw/riptopl-stack-heap.txt`).

Rollup by origin (`raw/riptopl-per-object-top.txt`, opl.map input sections; columns text / rodata / data / bss, then total):

| origin | text | rodata | data | bss | total |
|---|---:|---:|---:|---:|---:|
| first-party | 787,432 | 143,440 | 83,518 | 1,224,135 | 2,238,525 |
| blob-irx (embedded IRX/ELF) | 0 | 0 | 1,646,855 | 0 | 1,646,855 |
| sdk + toolchain libs | 1,189,184 | 197,462 | 7,104 | 107,522 | 1,501,272 |
| blob-asset (bin2c) | 0 | 0 | 921,975 | 0 | 921,975 |
| vendored (mbedTLS) | 126,016 | 9,961 | 4 | 8,788 | 144,769 |

Embedded blobs make up 2,568,830 of the 2,661,826 .data bytes (96.5%).

### Top 80 symbols by size

The full table is in `raw/riptopl-top80-symbols.txt` (`nm -S -C --size-sort -r` on the unstripped ELF). The first 20:

| bytes | type | symbol |
|---:|---|---|
| 786,432 | b | `bgmBuffer` (`src/core/bgm.cpp`) |
| 118,343 | D | `incebtion_png` (Korium `background.png`) |
| 99,229 | D | `ps2ip_irx` |
| 98,304 | b | `gArtStack` (`src/gfx/artload.cpp`) |
| 98,304 | b | `thread_stack` (`src/platform/ioman.cpp`) |
| 92,528 | D | `boot_adp` |
| 73,008 | D | `eecore_irx` |
| 72,101 | D | `mcman_irx` |
| 64,920 | D | `ip_png` (Korium `background_info.png`) |
| 55,997 | R | `ft_adobe_glyph_list` (FreeType psnames) |
| 55,605 | D | `smb2_cdvdman_irx` |
| 48,853 | D | `smb_cdvdman_irx` |
| 45,789 | D | `bdm_ata_cdvdman_irx` |
| 45,525 | D | `nfs_cdvdman_irx` |
| 45,101 | D | `ingame_smstcpip_irx` |
| 43,773 | D | `http_cdvdman_irx` |
| 42,621 | D | `ps2fs_irx` |
| 41,669 | D | `hdd_cdvdman_irx` |
| 40,920 | b | `hiddenLists` |
| 40,637 | D | `bdm_udp_cdvdman_irx` |

Further down: `gSettingsShell` 20,668 B; `__sbuff` 19,456 B; `diagSave()::copy` 16,384 b; `diagRing` 16,384 b; `vwin8192` 16,384 r; `tonemasks` 22,848 r; `_vfprintf_r` 14,360 T and `_svfprintf_r` 14,264 T (two printf cores); `png_do_read_transformations` 13,436 T. nm sums by type, all symbols: T 2,092,384, D 2,658,605, R 207,282, B 1,338,795.

### Biggest contributors per object file

From `raw/riptopl-per-section-top.txt` (top 25 per section group, plus the top 10 first-party objects for each). Highlights:

- .text: FreeType `sfnt.o` 59,896, `truetype.o` 56,528, `psaux.o` 39,536, `autofit.o` 38,740, `ftbase.o` 35,076. First-party: `src/ui/guisettings.o` 41,976, `src/sources/vcdsupport.o` 34,848, `src/ui/guigame.o` 33,040, `src/sources/hddsupport.o` 31,264, `src/ui/menusys.o` 29,736, `src/sources/supportbase.o` 27,816.
- .rodata: `src/platform/diaglog.o` 126,160 (the largest single rodata input in a standard, non-diagnostic build), FreeType `psnames.o` 62,280, vorbis `window.c.obj` 32,672 and `psy.c.obj` 23,588, FreeType `autofit.o` 23,336.
- .data: `asm/incebtion_png.o` 118,343, `_embed/ps2ip_irx.o` 99,245, `asm/boot.o` 92,532, `_embed/eecore_irx.o` 73,024, `_embed/mcman_irx.o` 72,117, `src/ui/dialogs.o` 66,296 (first-party initialised data), `asm/ip_png.o` 64,920.
- .bss: `src/core/bgm.o` 795,654, `src/platform/ioman.o` 98,340, `src/gfx/artload.o` 98,304, `src/sources/gamemanage.o` 40,924, SDK `fileXio_rpc.o` 31,852, `src/ui/guigame.o` 27,907, `src/ui/guisettings.o` 25,180, `src/platform/sysneutrino.o` 17,036, `src/platform/diaglog.o` 16,392, `src/core/diagsave.o` 16,384.
- Per library (`raw/riptopl-per-object-top.txt`): libfreetype 394,768, libjpeg 261,285, libvorbis 233,676, libc 145,636, libmbedtls 144,769, libpng 93,346, libfilexio 42,796, libm 33,628, libgskit 32,092, libvorbisfile 29,436, libogg 28,373, libz 22,501.

### Embedded blobs

From `raw/riptopl-embedded-blobs.tsv` (all in .data; sizes are opl.map object bytes, which run about 16 bytes over the file size):

- IRX/ELF: 85 objects, 1,646,855 bytes. 63 are built from repo sources (1,203,869 bytes) and 22 are prebuilt SDK IRX (442,986 bytes) (`raw/irx-modules.tsv`). The SDK ones are ps2ip 99,245, mcman 72,117, ps2fs 42,637, usbhdfsd 38,265, padman 36,709, libsd 15,813, smap 15,045, audsrv 14,101, netman 13,093, dvrfile 12,721, dvrdrv 12,065, mmcedrv 11,329, ps2dev9 10,977, iomanx 10,401, mcserv 8,213, hdpro_atad 7,669, udnl 5,889, filexio 5,653, sio2man 5,241, poweroff 3,505, mmceigr 1,385 and eesync 913.
- Assets (bin2c of `asm/*.c`): 156 objects, 921,975 bytes.
  - PNG: 133 objects, 652,559 bytes (largest `incebtion_png` 118,343, `ip_png` 64,920, `techwritescode_png` 21,009, then many device icons at about 13.4 KB each).
  - ADP audio: 8 objects, 209,248 bytes (`boot` 92,532, `transition` 27,012, `bd_disconnect` 22,788, `message` 21,396, `confirm` 21,044, `cancel` 11,012, `bd_connect` 11,412, `cursor` 2,052).
  - Theme cfg: 4 objects, 27,908 bytes.
  - Font: 1 object, `poeveticanew` 17,384 bytes.
  - Save icons: 5 objects, 10,682 bytes.
  - CA roots (.der): 4 objects, 3,946 bytes.
  - IOPRP.img: 1 object, 248 bytes.

## 3. Linker facts

Sources: `ps2.yaml` (src copy), `build.ninja`, `compile_commands.json`; summary in `raw/compile-link-flags.txt`.

- Main ELF `riptopl`: all 267 TUs compile with `-O2 -ffunction-sections -fdata-sections` (`ps2.yaml:645` cflags; ps2build adds `-ffunction-sections`). The link passes `-Wl,--gc-sections -Wl,-zmax-page-size=128 -Wl,-Map,opl.map -lpadx` (`ps2.yaml:649`) with the SDK linkfile `packages/core/startup/share/ee/linkfile`, then strips with `--strip-unneeded --remove-section=.pdr/.comment/.mdebug.abi32/.gnu.attributes`. The packing step is `pack: true` (`ps2.yaml:651`), giving a 44.47% size (section 1).
- `eecore`: 19 TUs at `-Os -ffunction-sections`, no `-fdata-sections`. The link uses `--gc-sections`, the repo linker script `ee_core/linkfile` and `-Wl,-Map,eecore.map` (`ps2.yaml:613`). `elfldr`: `-Os`, `-ffunction-sections -fdata-sections`, `--gc-sections` (`ps2.yaml:620`).
- Vendored mbedTLS (`lib/libmbedtls.a`): 109 TUs at `-O2 -ffunction-sections`, no `-fdata-sections`.
- IOP modules: 63 of the 69 links in `build.ninja` have no `--gc-sections`. Only 6 links have it: eecore, elfldr, riptopl, smb2probe, and the IRX `nfsman` and `smb2man` (`ps2.yaml:171,187`). Those two are also the only IRX compiled with `-ffunction-sections -fdata-sections`. Every other IRX ships every compiled function. Most IRX are `-Os`; `pccf_bd` and `pccf_game` are `-O2` (`raw/compile-link-flags.txt`).
- Libraries on the main link line (`build.ninja`, riptopl `libs`): startup, gskit, dmakit, poweroff, filexio, patches, png, jpeg, z, mc, freetype, vux, cdvd, netman, ps2ips, audsrv-compat, vorbisfile, vorbis, ogg, pad, ds34usb_ee, ds34bt_ee, debug, mbedtls and audio, plus `-lpadx` from ldflags and the toolchain libc, libm, libgcc, libstdc++ and libpthread. mbedtls and audio are not in `ps2.yaml:650`; the edition overlay adds them (INFERRED: updater/ratls sources, brief section 3).
- Archive members pulled in: 509 from 32 archives (`raw/riptopl-archive-members.txt`). libc 182, libjpeg 54, libm 45, libgcc 37, libmbedtls 34, libkernel 28, libpthread 19, libvorbis 18, libpng 14, libfreetype 14, libcglue 11, libz 8, libgskit 7, libpatches 5, libaudio 4, libstdc++ 4, the rest 1 to 3. The same file says which object first pulled each member. eecore: 13 members from 2 archives (`raw/eecore-archive-members.txt`).
- Link inputs that contribute nothing (`raw/riptopl-zero-contribution.txt`): `obj/riptopl/src/ui/menuwatch.o` and `obj/riptopl/src/platform/launchdiag.o` (0 bytes after gc), and `libpad.a` and `libdebug.a` (no member used; `libpadx.a` supplies the pad code). This is 4 of 389 inputs. eecore: `libstartup.a` and `libgcc.a` contribute nothing (`raw/eecore-zero-contribution.txt`).

## 4. Dead-code evidence

### Discarded input sections

From `raw/riptopl-discarded-summary.txt` and `raw/riptopl-discarded-firstparty.txt`. A discarded section is not in the ELF: it is evidence of unused source, not of shipped bytes.

- opl.map lists 6,581 discarded entries, 710,497 bytes in total, most of it non-loaded metadata.
- Allocatable sections removed by `--gc-sections`, by origin:
  - first-party: 272 entries, 21,562 bytes (.text 254 / 21,376; .rodata 16 / 178; .bss 2 / 8). The full list with sizes is in `raw/riptopl-discarded-firstparty.txt`. It includes `lib/libds34bt_ee.a` and `lib/libds34usb_ee.a`.
  - vendored mbedTLS: 242 entries, 37,576 bytes.
  - SDK: 971 entries, 213,396 bytes. The largest are libpng write code (`pngwutil.o` 19,917, `pngwrite.o` 14,153) and zlib `deflate.o` 16,828 and `trees.o` 10,876.
- First-party objects with the most discarded bytes: `src/core/pad.o` 1,592, `src/gfx/renderman.o` 1,552, `src/platform/ratls.o` 1,440, `src/sources/hddsupport.o` 1,380, `src/sources/netsupport.o` 1,160, `src/gfx/fntsys.o` 1,112, `src/ui/gui.o` 1,112, `src/sources/favsupport.o` 1,016.
- eecore: allocatable first-party discards are 13 entries, 144 bytes (`raw/eecore-discarded-firstparty.txt`, `raw/eecore-discarded-summary.txt`).

### Compiler warnings

Shipped flags, real build (`raw/build-warnings.txt`): 100 warning lines, 67 unique.

| flag | unique |
|---|---:|
| -Wsign-compare | 26 |
| -Wwrite-strings | 18 |
| -Wdiscarded-qualifiers | 8 |
| -Wunused-variable | 5 (4 in SDK headers) |
| no flag | 4 |
| -Wpointer-sign | 3 |
| -Wcomment | 1 |
| -Wmissing-declarations | 1 |
| -Wstringop-truncation | 1 |

The 4 warnings with no flag include `raudp_imports.c:11` "initializer-string … too long (12 chars into 8 available)" and the lwNBD `UINT64_MAX`/`UINT64_C` redefinitions.

Evidence-only whole-TU pass (`raw/extra-warnings-S.tsv`, summary `raw/extra-warnings-S-summary.txt`). It ran on 468 TUs (322 unique first-party sources; `asm/`, `third_party/` and lwNBD skipped), with `-Wall -Wextra -Wunused -Wunused-macros -Wunused-const-variable -Wredundant-decls -Wmissing-declarations -Wimplicit-fallthrough -Wduplicated-cond -Wduplicated-branches -Wlogical-op`, plus prototype checks for C. It found 1,163 unique warnings:

| flag | unique |
|---|---:|
| -Wunused-parameter | 439 |
| -Wmissing-prototypes | 243 |
| -Wunused-macros | 134 |
| -Wunused-const-variable | 119 |
| -Wredundant-decls | 82 |
| -Wempty-body | 82 |
| -Wwrite-strings | 18 |
| -Wdiscarded-qualifiers | 8 |
| -Wold-style-declaration | 6 |
| -Wduplicated-branches | 5 |
| -Wunused-variable | 5 |
| -Wimplicit-fallthrough | 4 |
| -Wextra | 4 |
| -Wclobbered | 3 |
| -Wcast-function-type | 2 |
| -Wtype-limits | 2 |
| -Wunterminated-string-initialization | 2 |
| others (each) | 1 |

There were 0 `-Wunused-function` warnings in any TU. The earlier agent's `-fsyntax-only` pass (`raw/extra-warnings.tsv`, 1,030 unique) missed the `-Wunused-const-variable`, `-Wclobbered` and most `-Wduplicated-branches` hits; the `-S` pass supersedes it.

First-party items worth a look are listed in `raw/extra-warnings-S-summary.txt` under "dead-code / bug-shaped":
- unused constants `SYS_DISC_DETECT_MS`, `SYS_DISC_REDETECT_MS` and `SYS_DISC_NODISC_MS` in each of `src/platform/system.cpp:17-25`, `sysneutrino.cpp:15-23` and `sysvmc.cpp:15-23`; also `ELF_MAGIC` (`system.cpp:99`), `CORE_IRX_DEBUG` (`system.cpp:678`), `htab` (`src/platform/util.cpp:728`) and `VCD_ID_PROBE_BUDGET` (`src/sources/vcdsupport.cpp:262`)
- unused variable `restored` (`src/sources/cuesupport.cpp:772`)
- identical branches at `src/gfx/textures.cpp:1304,1334`, `src/sources/httpsupport.cpp:182` and `src/ui/dia.cpp:637`
- variables that may be clobbered by longjmp at `src/gfx/textures.cpp:883-889` (libpng setjmp path)
- fall-through at `src/ui/dia.cpp:334`
- always-false unsigned `< 0` comparisons at `src/gfx/coverflow.cpp:157` and `src/gfx/grid.cpp:211`
- 63 `-Wempty-body` sites in `src/`

All are compiler output only; line numbers refer to the build copy, which is identical to `src/RiptOPL` at the audited commit. Verify each against `src/` before citing.

### Objects compiled but contributing nothing

`src/ui/menuwatch.cpp` and `src/platform/launchdiag.cpp` are compiled into the standard build and contribute 0 bytes (section 3). Diagnostic code that does contribute in a standard build: `src/platform/diaglog.o` with 126,160 rodata and 16,392 bss, and `src/core/diagsave.o` with 16,384 bss (`raw/riptopl-per-section-top.txt`).

## 5. Cross-reference candidates (CANDIDATES ONLY)

Source: `raw/all-xref-summary.txt` and `raw/all-xref-candidates.tsv` (`scripts/xref.py`; limits in its header). The analysis covered 69 links (374 EE objects, 461 IOP objects) and 6,055 global non-weak definitions. 1,499 symbols are referenced by no other object in any link of this build that compiles them.

- First-party functions only used in their own object (own_obj_reloc=yes; could be static): 441 kept in the EE ELF (224,832 bytes) and 223 in IRX (57,004 bytes).
- First-party symbols no first-party object names, removed by `--gc-sections`: 278 functions (20,560 bytes) and 4 data symbols (16 bytes). Per file: `src/core/pad.cpp` 20 / 1,564, `src/gfx/renderman.cpp` 21 / 1,504, `src/sources/hddsupport.cpp` 21 / 1,332, `src/sources/netsupport.cpp` 13 / 1,132, `src/gfx/fntsys.cpp` 13 / 1,104, `src/ui/gui.cpp` 16 / 1,096, `src/sources/favsupport.cpp` 9 / 1,004, `src/platform/ratls.c` 1 / 952.
- First-party symbols no first-party object names but kept in the EE ELF: 16 (7,144 bytes). 12 are 8-16-byte `elfldr/loader.c` SDK-hook overrides (`_libcglue_init`, `InitAlarm`, and others, reached from SDK code). The rest are `Atlas::~Atlas()` (6,488), `mbedtls_hardware_poll` (344, called by mbedTLS), `AppSource::AppSource()` (148) and `GameSource::GameSource(...)` (60).
- IRX symbols with no reference by name anywhere: 54 symbols in 34 files, 5,784 bytes, shipped because IRX links have no gc. The largest are `modules/hdd/apa/src/hdd.c` `hddAddPartitionHere` 756, `apa.c` `apaDelete` 432, `mmce_sio2.c` `mmce_sio2_rx_dma` 420, `usmb2/unicode.c` `utf16_to_utf8` 396, `fatfs/ff.c` `f_truncate` 384 and `smb2man.c` `usmb2_init_context` 284. Many are upstream-derived (vendored apa, FatFs, SMSTCPIP, usmb2).
- Caveat: the script marks every IRX link as having no `--gc-sections`. nfsman and smb2man do pass it (section 3), so their "shipped" rows (`smb2man.c` 3 symbols / 544 bytes, `nfsman.c` 1 / 196, `usmb2/unicode.c`, `usmb2/md5.c`) may in fact be gc'd. Check those `*.notiopmod.elf` files with nm before claiming bytes.

## 6. IRX modules built from source

Source: `raw/irx-modules.tsv` (size of every `bin/` IRX and ELF, embed status from the `riptopl` `embed_irx` list in `ps2.resolved.yaml` and `ps2.yaml:652`, and blob size from opl.map).

- 61 repo-built IRX plus `eecore.elf` and `elfldr.elf` are embedded in the main ELF (63 blobs, 1,203,869 bytes). The largest are `smb2_cdvdman` 55,605, `smb_cdvdman` 48,853, `bdm_ata_cdvdman` 45,789, `nfs_cdvdman` 45,525, `SMSTCPIP` 45,101, `http_cdvdman` 43,773, `hdd_cdvdman` 41,669, `bdm_udp_cdvdman` 40,637, `udpfs_cdvdman` 38,541, `bdmfs_fatfs_fork` 38,325, `hdd_hdpro_cdvdman` 37,389, `pccf_cdvdman` 36,941, `bdm_cdvdman` 35,989, `smbman_ra` 35,309, `mmce_cdvdman` 34,117 and `smb2man` 32,357. The 12 per-device `*_cdvdman.irx` variants alone total 504,828 bytes (awk sum over `raw/irx-modules.tsv`). All file sizes are in the TSV.
- Shipped loose and also embedded: `IEEE1394_bd_mini_fork.irx` (10,577; shipped as `IEEE1394_bd_mini.irx`) and `iLinkman_fork.irx` (15,713; shipped as `iLinkman.irx`).
- Built but neither embedded nor shipped in this edition: `mx4sio_bd_full_fork.irx` 12,289, `raudp.irx` 12,161, `udptty_ingame.irx` 3,129 and `smb2probe_iop.irx` 2,113 (embedded in `smb2probe.elf`, a hardware-test tool). The first three are INFERRED to be used by other editions or configurations; I did not check that.

## 7. Stack, heap, .bss

- Main ELF (`raw/riptopl-stack-heap.txt`, the SDK linkfile `packages/core/startup/share/ee/linkfile:139-141`):
  - `_stack_size` = 0x20000 (128 KB, SDK default); `_stack` = -1 and `_heap_size` = -1 (kernel-chosen top of RAM; the heap takes the rest).
  - No first-party source overrides these: grep for `_stack_size|_heap_size` in `src/`, `include/` and `ee_core/src` matched only `ee_core/src/crt0.S`.
  - `.bss` totals 1,341,532 bytes, running from `_fbss` 0x005e1180 to `_end` 0x007289dc. bgm.o alone has 795,654 bytes of it (768 KB `bgmBuffer`).
  - Static thread stacks in .bss: `thread_stack` 98,304 (IO worker, `src/platform/ioman.cpp`) and `gArtStack` 98,304 (`src/gfx/artload.cpp`).
- eecore: `_stack_size` = 0x1600 (5,632 bytes), from `ee_core/linkfile:108-111` ("any remaining space up to" 0x97000, assert >= 0xC00 at `:119`). `.bss` is 16,000 bytes and `_end` is 0x00097000.
- elfldr: `.bss` 39,680 + 16 bytes (`raw/riptopl-size-A.txt`).

## 8. What-if experiments

Neither agent ran any size experiments (rebuilds with different `-fdata-sections` or `--gc-sections` settings). The only extra runs were the two evidence-only warning passes in section 4, which do not change any output. No measured what-if size numbers exist.
