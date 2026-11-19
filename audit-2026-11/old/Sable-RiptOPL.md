# Sable Audit — RiptOPL

- **Auditor:** Sable
- **Pin:** `rebuild/main` @ `677812185ac5aab96bf697f21919a5729307ec4a` (2026-11-13, "Check for Updates: stop keeping a .BAK backup of the old ELF"), checked 2026-11-13
- **Scope:** Read-only. No files in the repo were edited. Clone: `~/workspace/audit-clones-2026-11-13/RiptOPL`
- **Files counted:** 2,380 tracked files; ~457,186 lines of C/C++/headers repo-wide; `src/`+`include/` = 102,989 lines; 685 files under `modules/`, 317 under `.github/`, 311 under `third_party/`, 182 under `ship/`, 159 under `docs/`
- **Evidence run:** `tools/run_tests.sh` runs 277 `.github/scripts/test_*.py` files serially — it did not finish within 120 s in this sandbox and was stopped (no failure observed before stopping). One suite run individually passed: `python3 .github/scripts/test_selfupdate_host.py` → "PASS: selfupdate host tests (52 checks)". No PS2 build was attempted (no ps2build/SDK in this sandbox) — all size figures for ELF/IRX impact are estimates labelled as such.
- **Secrets:** None found in the sweeps run (no hard-coded password/token assignments in `src/`, `include/`, `ee_core/`, `tools/`). No secret-like values are printed in this report.
- **Main rule applied:** LESS, NOT MORE. Every fix below deletes or simplifies; the few that add anything carry a justification.

Findings are sorted by benefit, descending.

---

## RIPTOPL-001 / docs/assets is 53.7 MB of duplicated tour media in the source repo / Category 7
- **Location(s):** RiptOPL/docs/assets/ui/library-tour.gif, RiptOPL/docs/assets/ui/incebtion/library-tour.gif, RiptOPL/docs/assets/ui/info-navigation.gif, RiptOPL/docs/assets/ui/incebtion/info-navigation.gif, RiptOPL/docs/assets/ui/settings-navigation.gif (+ matching `.mp4` files in both directories)
- **Problem:** `docs/assets` totals 53,655,730 bytes of tracked media. The same three UI tours exist twice (plain and `incebtion/`), and each exists as both an animated GIF (up to 10.9 MB) and an MP4 of the same tour — four copies of the same footage per tour.
- **Fix:** Keep one format (MP4) in one directory, delete the other three copies of each tour, and host any historical variant outside the git repo.
- **Benefit:** ~35–45 MB of repo/clone size removed (estimate from the file sizes read).
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — file inventory and sizes read directly).

## RIPTOPL-002 / ship/Example is a second, committed copy of the whole ship tree / Category 2
- **Location(s):** RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/ (137 tracked files, 5,918,101 bytes) duplicating RiptOPL/ship/POPS/, RiptOPL/ship/EMBER/, RiptOPL/ship/neutrino/; consumed as a template by RiptOPL/.github/scripts/package_release_app.py:4, RiptOPL/.github/scripts/package_release_app.py:24
- **Problem:** Every shipped binary and asset (PATCH_5.BIN, POPSTARTER variants, IRX files, art, neutrino configs) is stored twice; `diff` confirms byte-identical copies (e.g. `ship/POPS/PATCH_5.BIN` vs the Example copy). Every asset update must be made — and verified — in two trees, and workflows already contain special-case checks for exactly this drift (`.github/workflows/flavours.yml` ilink copy check, `ember-watch.yml` lines 130/154 adding both trees).
- **Fix:** Generate the Example folder at packaging time from `ship/` (package_release_app.py already stages a package from it) and delete the committed duplicate tree; keep only the small template-only files (APPINFO.PBT, title.cfg, icon set) if any are truly template-only.
- **Benefit:** ~5.9 MB repo size and 137 files removed; one whole class of two-tree drift checks removed (estimate).
- **Effort:** M. **Risk:** Med. **Confidence:** High (READ).

## RIPTOPL-003 / A 1,251-line Makefile that can no longer build the program is kept as a second build definition / Category 7
- **Location(s):** RiptOPL/Makefile:1, RiptOPL/Makefile:136, RiptOPL/Makefile:156, RiptOPL/Makefile:191, RiptOPL/Makefile:220; RiptOPL/.github/workflows/flavours.yml:399; RiptOPL/tools/assets.py:2; live build definition at RiptOPL/ps2.yaml:639 (main target) and RiptOPL/tools/build_edition.py:18
- **Problem:** CI itself states "The Makefile can no longer build src/ (it is C++ now); ps2build is the build" (flavours.yml:399) and assets.py says its steps "used to live in the Makefile. ps2build handles all code." Yet the full legacy Makefile survives with its own object lists, PNG asset list, translation list and library list that duplicate ps2.yaml/tools — and things still scrape it: `tools/lng_pack.sh:23-27` parses the version out of Makefile lines 1–4, and multiple test scripts assert on its contents (e.g. `.github/scripts/test_udpfs_files_oplcore.py:56-61,177`, `test_menu_crash_catcher.py:88`, `test_apa_table_safety.py:215-224`).
- **Fix:** Delete the top-level Makefile; move the four version numbers into ps2.yaml (or a one-line version file that lng_pack.sh and assets.py both read), and delete/retarget the tests that assert on Makefile text.
- **Benefit:** 1,251 lines removed plus the per-change double-bookkeeping between two build definitions (estimate: also removes a recurring source of stale-flag bugs of the kind the Makefile's own comments document).
- **Effort:** M. **Risk:** Med. **Confidence:** High (READ).

## RIPTOPL-004 / 273 remote branches, 225 of them claude/* agent branches / Category 7
- **Location(s):** Repository refs at pin (listed only, per instructions): 273 remote branches including `origin/claude/project-thread-*` (dozens), `origin/claude/bgm-decode-spin`, `origin/claude/igr-*` (six IGR variants) and similar one-session branches; 5 tags
- **Problem:** Hundreds of stale agent/session branches make branch lists, CI branch filters (`ps2build.yml` builds `claude/**`) and "what is live?" unanswerable at a glance, and `claude/**` pushes still trigger full ps2build CI.
- **Fix:** Delete merged/abandoned remote branches in bulk (list only — this audit changes nothing), and stop triggering CI on `claude/**`.
- **Benefit:** 200+ refs removed (count read at pin); faster ref advertisement on every clone/fetch.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — `git branch -r` count).

## RIPTOPL-005 / 277 Python "tests" in .github/scripts, most of them source-text grep assertions / Category 7
- **Location(s):** RiptOPL/.github/scripts/ (285 Python files, ~62,363 lines total in `.github/scripts`); RiptOPL/tools/run_tests.sh:1; RiptOPL/.github/workflows/flavours.yml (157 test-invocation lines)
- **Problem:** 266 of the 277 test files use `read_text` (692 call sites) — i.e. they assert that source files contain/forbid particular strings rather than executing behaviour. The suite is serial, did not finish in 120 s here, and every refactor must update tests that encode the current implementation's wording. This is a second, parallel specification of the code, written in regex.
- **Fix:** Delete the pure text-assertion tests whose underlying behaviour is covered by the host C++ tests in `tests/host/` (14 suites) or by a build; keep the genuine behavioural host tests (e.g. test_selfupdate_host.py, which compiles and runs the real parser — 52 checks, passing). Do not replace them with a new framework.
- **Benefit:** Tens of thousands of lines removed (estimate: >30,000 of the ~62k lines in .github/scripts); CI and local test time cut proportionally.
- **Effort:** L. **Risk:** Med. **Confidence:** High (READ — counts and run observed).

## RIPTOPL-006 / mbedTLS is vendored whole and globbed whole into two editions / Category 4
- **Location(s):** RiptOPL/third_party/mbedtls/ (7.9 MB, 109 files in `library/`); RiptOPL/tools/build_edition.py:114, RiptOPL/tools/build_edition.py:137 (both append a target with `"sources": ["third_party/mbedtls/library/*.c"]`)
- **Problem:** Both the RetroAchievements edition and the slim updater edition compile *every* mbedTLS library source — all ciphers, all curves, PKCS modules, PSA storage, etc. — for two narrow jobs (one HTTPS game-ID lookup; one GitHub release fetch). The slim config header reduces runtime features, but the glob still compiles the full library and relies on the linker to discard it.
- **Fix:** List only the mbedTLS sources the two call paths actually link (TLS 1.2 client, X.509, the one cipher suite family, SHA-256, CTR-DRBG/entropy) instead of the `library/*.c` glob, in both places.
- **Benefit:** Estimate: tens of KB of ELF size and a large cut in build time for RA/updater editions; exact figure requires a build this sandbox cannot run (INFERRED magnitude, READ mechanism).
- **Effort:** M. **Risk:** Med. **Confidence:** Med (READ).

## RIPTOPL-007 / Edition × configuration × toolchain × device matrix is the project's largest surface / Category 3
- **Location(s):** RiptOPL/tools/build_edition.py:18 (4 editions + 2 lite meta-editions), RiptOPL/tools/build_configuration.py:7 (15 configurations), RiptOPL/tools/lite_devices.py (8 lite devices × plain/RA), RiptOPL/.github/workflows/configurations.yml, RiptOPL/.github/workflows/rolling-release.yml, RiptOPL/.github/workflows/rolling-llvm.yml
- **Problem:** 4 editions × 15 configurations × 2 toolchains, plus 16 lite ELFs, is hundreds of distinct binaries from one codebase; the build tooling alone for slicing them is ~2,491 lines (`ps2.yaml` 670 + build/build_configuration/build_edition/package/publish scripts). Most combinations can never be hardware-tested, which contradicts the project's own "real hardware before done" standard.
- **Fix:** Cut the configuration list to the ones actually published or used for diagnosis (standard, one debug, diag, dtl) and drop configurations nobody ships (e.g. the four-way debug-UART cross-product); keep editions/devices that have a named user.
- **Benefit:** Estimate: hundreds of lines of build tooling and whole CI matrix legs removed; every remaining binary becomes testable.
- **Effort:** M. **Risk:** Med. **Confidence:** High (READ).

## RIPTOPL-008 / UDPFS IOP stack is copy-pasted three times, byte-identical / Category 2
- **Location(s):** RiptOPL/modules/network/udpfs_bd/src/udpfs_core.c, RiptOPL/modules/network/udpfs_fhi/src/udpfs_core.c, RiptOPL/modules/network/udpfs_ioman/src/udpfs_core.c (identical MD5); same for `udprdma.c`, `udpfs_core.h`, `udpfs_packet.h`, `udprdma.h`, `udprdma_packet.h`, `udprdma_seq.h`, `irx_imports.h` in the same three directories; `main.c`/`exports.tab` identical between udpfs_bd and udpfs_ioman; `mprintf.h` identical in five directories (udpfs_bd/fhi/ioman/ministack/smap `include/`)
- **Problem:** The same UDPFS core is maintained in three (partly five) places. Any protocol or bug fix must be applied three times and verified three times; the copies can only drift silently.
- **Fix:** Keep one copy of the shared sources and have the three IRX targets in ps2.yaml reference that single directory, with only the genuinely different `main.c` glue per module. This is the one shared-helper case that clearly pays: identical files, net deletion.
- **Benefit:** Estimate: ~2 of every 3 copies of the UDPFS core removed — several thousand lines and two whole maintenance surfaces (exact line total not summed per-file; INFERRED from identical hashes and directory listings READ).
- **Effort:** M. **Risk:** Med. **Confidence:** High (READ — MD5-identical file groups).

## RIPTOPL-009 / Twelve workflows, 2,220 lines, six of them scheduled watchers/publishers / Category 7
- **Location(s):** RiptOPL/.github/workflows/ (12 files, 2,220 lines): ember-watch.yml:18 (every 6 h), neutrino-watch.yml:26 and orbit-neutrino-watch.yml:18 (same cron, two separate watchers for two Neutrino upstreams), ps2build-sdk-watch.yml:27 (weekly), docs-sync.yml:15 (daily), wiki-publish.yml:20 (daily), flavours.yml (493 lines), rolling-release.yml (225), rolling-llvm.yml (245), ps2build.yml, configurations.yml, check-format.yml
- **Problem:** Scheduled bots that poll upstreams, rewrite docs/badges, republish the wiki and dispatch rebuilds run constantly and can each break the repo unattended; two watchers watch near-identical upstreams on the same tick.
- **Fix:** Merge the two Neutrino watchers into one workflow with two jobs (or drop the orbit one to manual dispatch), and drop docs-sync and wiki-publish to workflow_dispatch only — badges/wiki do not need daily bot commits.
- **Benefit:** Estimate: 300–500 workflow lines removed and ~50 scheduled runs/month eliminated.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

## RIPTOPL-010 / Six POPSTARTER variants ship in the bundle, including three DEBUG builds / Category 8
- **Location(s):** RiptOPL/ship/POPS/POPSTARTER VERSIONS/{MAIN,DEBUG,USBDELAY,USBDELAY_DEBUG,USBDELAY_LONGER_DEBUG}/POPSTARTER.ELF plus RiptOPL/ship/POPS/POPSTARTER.ELF (13 tracked paths match `POPSTARTER`), and the same set again under ship/Example (see RIPTOPL-002)
- **Problem:** End users are shipped five alternative POPSTARTER builds, three of them explicitly debug builds, inside the standard bundle. That is support surface and bundle size for a diagnostic matrix users were never meant to choose between.
- **Fix:** Ship the one default POPSTARTER.ELF; move the delay/debug variants to a separate diagnostics download.
- **Benefit:** Estimate: several hundred KB to ~1 MB per bundle copy removed (×2 while RIPTOPL-002 stands), and one whole class of "which POPSTARTER did you use?" support questions.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — file inventory).

## RIPTOPL-011 / ~130 PNG assets are bin2c-embedded into every ELF, including both button-glyph sets / Category 4
- **Location(s):** RiptOPL/tools/assets.py:46 (PNG_ASSETS list), RiptOPL/tools/assets.py:59 (adds 24 `glyph_standard_*`/`glyph_white_*` PNGs), RiptOPL/tools/assets.py:173; RiptOPL/gfx/ (142 files, 443,330 bytes of PNG; glyph sets alone 43,859 bytes); consumers at RiptOPL/src/gfx/themes.cpp:1185
- **Problem:** Every embedded PNG is dead weight in RAM and ELF for the modes that never draw it: six Rating icons, eleven Scan_* mode icons, five Index families, logo0–logo3, and *both* button-glyph sets when the Interface setting selects only one at a time.
- **Fix:** Embed one glyph set and load the alternate from the theme folder like other theme art; drop the per-scan-mode icon family in favour of the existing text labels (the settings UI already names the modes in strings).
- **Benefit:** Estimate: 40–100 KB of ELF/RAM removed.
- **Effort:** M. **Risk:** Med. **Confidence:** High (READ — asset list and embedding loop read; runtime-selectability of one glyph set READ at themes.cpp:1185-1214).

## RIPTOPL-012 / FatFs unicode tables for all code pages compiled into the IOP for one fixed SBCS code page / Category 4
- **Location(s):** RiptOPL/modules/bdm/bdmfs_fatfs/fatfs/ffunicode.c (15,593 lines, 1,959,402 bytes of source), RiptOPL/modules/bdm/bdmfs_fatfs/fatfs/include/ffconf.h:87 (`#define FF_CODE_PAGE 869`), RiptOPL/ps2.yaml:310 (bdmfs_fatfs target compiles ffunicode.c), call sites RiptOPL/modules/bdm/bdmfs_fatfs/fatfs/ff.c:796, ff.c:878
- **Problem:** The BDM FatFs driver is configured for exactly one static SBCS code page (869), yet ships the full multi-code-page conversion tables of ffunicode.c into an IOP module, where RAM is ~2 MB total. The conversion functions take the code page as a runtime argument, so the linker cannot prune the other pages' tables.
- **Fix:** Trim ffunicode.c to the CP869 tables (it is a vendored, pinned copy — ORIGIN.txt already records it as such) or generate the single-page table at build time.
- **Benefit:** Estimate: several KB to tens of KB of IOP RAM in bdmfs_fatfs.irx (magnitude INFERRED; configuration and inclusion READ).
- **Effort:** M. **Risk:** Med. **Confidence:** Med (READ).

## RIPTOPL-013 / .bak backups are still written to memory cards by the POPSTARTER/UDPFS config editor and the VMC rebuild / Category 3
- **Location(s):** RiptOPL/src/sources/vcdsupport.cpp:2206, RiptOPL/src/sources/vcdsupport.cpp:2287, RiptOPL/src/sources/vcdsupport.cpp:2326 ("retain .bak for manual recovery"); RiptOPL/include/vmcrebuild.h:5, RiptOPL/include/vmcrebuild.h:12, RiptOPL/src/sources/vmcrebuild.cpp:124, RiptOPL/src/sources/vmcrebuild.cpp:173
- **Problem:** The HEAD commit just removed the updater's .BAK on principle, but two other features still create `.bak` files on 8 MB memory cards: the UDPFS config editor backs up both config files before every save, and a VMC rebuild permanently leaves the entire old card image as `<name>.bak` beside the new one — doubling a VMC's card footprint forever, with states (`VMC_REBUILD_LEFTOVER`) that then block future rebuilds until the user manually deletes files.
- **Fix:** For vcdsupport, keep the in-RAM rollback (oldData is already held in memory at vcdsupport.cpp:2287ff) and drop the on-card `.bak` write; for vmcrebuild, this is a deliberate safety copy — surface its size in the success dialog and offer deletion there instead of leaving it silently (no new mechanism: the dialog already exists).
- **Benefit:** Up to one full VMC image of card space per rebuild, and two small files per config save, on 8 MB cards (sizes READ from the code paths).
- **Effort:** S. **Risk:** Med. **Confidence:** High (READ).

## RIPTOPL-014 / Permanent multi-KB static buffers in BSS for occasional operations / Category 4
- **Location(s):** RiptOPL/src/platform/sysneutrino.cpp:384 (`static char toml[4096]`), RiptOPL/src/platform/sysneutrino.cpp:639 (a second `static char toml[4096]`), RiptOPL/src/sources/achievements.cpp:512 (`static char doc[2048]`, comment: "about 750 bytes of headers today"), RiptOPL/src/sources/achievements.cpp:677 (`static char response[1024]`), RiptOPL/src/gfx/fntsys.cpp:744 (`static char shaped[2048]`), RiptOPL/src/gfx/fntsys.cpp:1219 (`static char joined[2048]`), RiptOPL/include/ranet.h:89 (`char rx_[2048]`)
- **Problem:** At least ~15 KB of EE BSS is permanently reserved for buffers used only while parsing a Neutrino TOML, fetching an achievements page, or shaping one text run — on a 32 MB machine where ee_core, game lists and the texture cache compete for the same RAM.
- **Fix:** Shrink to the documented need (achievements doc is stated at ~750 bytes) and share one scratch buffer for the two sysneutrino TOML parses instead of two file-statics; leave the font-lock buffers if profiling shows contention, but halve them to the maximum shaped-run length actually enforced by callers.
- **Benefit:** Estimate: 6–10 KB of permanent RAM removed.
- **Effort:** S. **Risk:** Med. **Confidence:** High (READ).

## RIPTOPL-015 / sysDeleteFolder uses a malloc'd path without checking the allocation / Category 6
- **Location(s):** RiptOPL/src/platform/util.cpp:873 (allocation), RiptOPL/src/platform/util.cpp:874 (`sprintf(path, "%s/%s", folder, dirent->d_name)` immediately after)
- **Problem:** In the recursive folder delete, the first `malloc(strlen(folder) + strlen(dirent->d_name) + 2)` result is used unchecked — a failed allocation on a fragmented EE heap writes through NULL and crashes during a destructive operation. The surrounding code *does* check later allocations (util.cpp:885, :890, :899, :913), so this one is an inconsistency, not a style.
- **Fix:** Add the same NULL check the neighbouring lines already have and skip/abort that entry on failure. Justification for adding a check: it is one comparison on a cited crash path in a delete routine; it follows the file's own existing pattern and adds no new mechanism.
- **Benefit:** One crash-on-low-memory path removed from a recursive delete.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

## RIPTOPL-016 / SMB credentials and share names strcpy'd into fixed cdvdman settings fields / Category 6
- **Location(s):** RiptOPL/src/sources/ethsupport.cpp:1192, RiptOPL/src/sources/ethsupport.cpp:1193, RiptOPL/src/sources/ethsupport.cpp:1194, RiptOPL/src/sources/ethsupport.cpp:1444, RiptOPL/src/sources/ethsupport.cpp:1445, RiptOPL/src/sources/ethsupport.cpp:1446 (also :1435 `strcpy(settings->smb_prefix, gETHPrefix)`)
- **Problem:** User/share/password strings from the config file are `strcpy`'d into the fixed-size fields of the cdvdman settings block. One call site (ethsupport.cpp:~1185, the Neutrino path) length-checks the same strings first; the two sites above do not — they rely on every writer of `gPCUserName`/`gPCPassword`/`gPCShareName` having clamped them, which a hand-edited or migrated config file does not guarantee. 61 `strcpy`/`strcat` call sites and 29 raw `sprintf` sites exist across `src/`+`include/` in total (counted).
- **Fix:** Replace these seven `strcpy`s with the bounded copy the codebase already uses elsewhere (`snprintf(dst, sizeof-field, "%s", src)` idiom, as at ethsupport.cpp:1180 for nfsExport); no new helper needed.
- **Benefit:** Removes a config-driven buffer-overflow path into ee_core's launch settings.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

## RIPTOPL-017 / sprintf into 32/64-byte buffers in OSD history handling / Category 6
- **Location(s):** RiptOPL/src/core/OSDHistory.cpp:41, RiptOPL/src/core/OSDHistory.cpp:76, RiptOPL/src/core/OSDHistory.cpp:91, RiptOPL/src/core/OSDHistory.cpp:106 (`sprintf(fullpath[64], "%s/icon.sys" …)`), RiptOPL/src/core/OSDHistory.cpp:153 (`sprintf(path[32], "mc%d:/%s", i, GetSystemDataPath())`)
- **Problem:** Four helpers format a caller-supplied path into a fixed 64-byte buffer with unbounded `sprintf`. It fits only as long as every caller's path stays short — an assumption nothing in these functions enforces.
- **Fix:** Same one-line change as RIPTOPL-016: `snprintf` with the existing buffer size, at these five sites.
- **Benefit:** Removes the overflow if a longer system-data path is ever introduced; no behaviour change today.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

## RIPTOPL-018 / The per-source game-launch leg is copy-pasted across six call sites in five files / Category 2
- **Location(s):** `strcpy(filename, game->startup);` at RiptOPL/src/sources/bdmlaunch.cpp:904, RiptOPL/src/sources/ethsupport.cpp:1504, RiptOPL/src/sources/hddsupport.cpp:2787, RiptOPL/src/sources/mmcesupport.cpp:1556, RiptOPL/src/sources/udpfssupport.cpp:977, RiptOPL/src/sources/udpfssupport.cpp:1134
- **Problem:** Each device source re-implements the same "prepare filename + cdvdman settings + launch" sequence (the surrounding functions were sampled in ethsupport and share the same structure). A fix to launch semantics must be made five times — the RetroAchievements integration notes in the Makefile already record that only four of the legs could ever carry one feature because the others diverge.
- **Justification for a shared helper:** The same sequence verifiably appears in 5 files; one shared prepare-launch routine taking the already-existing settings block removes more code than it adds.
- **Fix:** Extract the identical prepare-and-fill-settings portion (not the device-specific connection parts) into supportbase, which all five sources already depend on.
- **Benefit:** Estimate: several hundred lines removed across five files; one place to fix launch bugs.
- **Effort:** L. **Risk:** Med. **Confidence:** Med (READ for the identical lines and ethsupport's full leg; INFERRED for full-leg equivalence in the other three files).

## RIPTOPL-019 / Directory scan probes every entry with a second opendir / Category 5
- **Location(s):** RiptOPL/src/sources/supportbase.cpp:373, RiptOPL/src/sources/supportbase.cpp:390, RiptOPL/src/sources/supportbase.cpp:397 (`DIR *probe = opendir(fullpath);`)
- **Problem:** The game-list scan cannot trust the dirent type on some devices, so for candidate entries it opens the full path again as a directory just to test whether it is one. On USB sticks, SMB shares and memory cards that is an extra open/close round-trip per file, on the scan that decides how long the UI waits for a game list.
- **Fix:** Cache the probe result per directory scan (the scan already walks each directory once), or restrict the probe to the devices whose dirent type is known-bad instead of all devices. No new subsystem — a condition on the existing probe.
- **Benefit:** Estimate: one device round-trip saved per subdirectory entry per scan; largest on SMB/UDPFS where each open is a network exchange.
- **Effort:** S. **Risk:** Med. **Confidence:** High (READ — code and its own comment at :390-397).

## RIPTOPL-020 / Permanent foreign-format compatibility layers for wOPL configs and favourites / Category 3
- **Location(s):** RiptOPL/src/core/config.cpp:82 (dual-syntax read), RiptOPL/src/core/config.cpp:918, RiptOPL/src/core/config.cpp:961 (wOPL per-game key table), RiptOPL/src/core/config.cpp:1261 (wOPL file-relocation map, function at :1278-1300); RiptOPL/src/sources/favsupport.cpp:162, RiptOPL/src/sources/favsupport.cpp:163, RiptOPL/src/sources/favsupport.cpp:335 (binary wOPL favourites struct reader)
- **Problem:** The loader permanently carries two other programs' config syntaxes, a per-game key-translation table, a file-relocation map for files another build renamed, and a binary parser for wOPL's 32-byte favourites struct. Every config change must be tested against formats this program never writes.
- **Fix:** Time-box the migration: import wOPL formats once, on first run, into RiptOPL's own files (the config system already auto-migrates its own legacy names — config.cpp:28-35), then delete the perpetual read-compat code.
- **Benefit:** Estimate: 200–400 lines across config.cpp (1,740 lines) and favsupport.cpp (1,833 lines) removed, plus the test files that exist only for these formats.
- **Effort:** M. **Risk:** Med. **Confidence:** High (READ).

## RIPTOPL-021 / iLink driver maintained twice, already diverged / Category 2
- **Location(s):** RiptOPL/modules/ilink/iLinkman/src/iLink.c (365 lines) and RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink.c (369 lines) — same filename, same `!!HACK!!` S100 comment at :351/:355 respectively, different content (`diff` confirms)
- **Problem:** Two copies of the same IEEE1394 driver exist — one for the loader, one frozen for the POPSTARTER assault build — and they have already drifted by four lines, so a hardware fix to one silently misses the other.
- **Fix:** Keep the assault copy only as a pinned *binary* (it already ships as prebuilt `usbd.irx.ilink`/`usbhdfsd.irx.ilink` artefacts with a CI equality check in flavours.yml) and delete its duplicated source tree, or build both from the one source. Either way: one source.
- **Benefit:** 369 lines and one driver maintenance surface removed.
- **Effort:** S. **Risk:** Med. **Confidence:** High (READ).

## RIPTOPL-022 / Korium theme assets are stored as a zip inside the repo and unzipped at build time / Category 7
- **Location(s):** RiptOPL/third_party/korium/assets.zip (579,186 bytes), RiptOPL/third_party/korium/UPSTREAM.json, RiptOPL/tools/assets.py:141, RiptOPL/tools/assets.py:143 (opens the zip, verifies SHA-256 per file, extracts to `build/theme-assets/korium`)
- **Problem:** The default theme's PNGs — the same kind of files that live as plain files in `gfx/` for the built-in theme — are stored as a single binary blob. Nothing in the repo can be diffed, reviewed or partially updated; a one-icon change replaces the whole archive, and the build needs a bespoke verified-extraction step for it.
- **Fix:** Commit the extracted PNGs as plain files under `third_party/korium/gfx/` (same layout the code already falls back to at assets.py:180) and delete the zip and the extraction/digest code.
- **Benefit:** ~580 KB binary blob and ~15 lines of extraction code removed; theme changes become reviewable (net repo size roughly neutral, reviewability is the win — stated honestly).
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

## RIPTOPL-023 / AGENTS.md and CLAUDE.md are the same file twice / Category 2
- **Location(s):** RiptOPL/AGENTS.md, RiptOPL/CLAUDE.md (identical MD5 `f93ecc98…`, 15 lines each)
- **Problem:** Two agent-instruction files with identical content will inevitably diverge; a third copy of the same policy effectively exists in `.claude/settings.json`'s attribution settings.
- **Fix:** Keep one file and delete the other (whichever the tooling reads second can be a one-line pointer — or nothing, if only one tool is actually used).
- **Benefit:** 15 lines and one guaranteed future inconsistency removed.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

## RIPTOPL-024 / audio/bgm.ogg: a 324 KB track kept "only as a reference" — and a second, different copy in the ship bundle / Category 7
- **Location(s):** RiptOPL/audio/bgm.ogg (324,182 bytes; Makefile comment block at RiptOPL/Makefile:178 states it is intentionally NOT compiled in and "kept in the repo only as a reference/default track"), RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/APPS/APP_RIPTOPL/bgm.ogg (369,632 bytes — a different file)
- **Problem:** Two different "default" BGM tracks totalling ~694 KB sit in the repo; the audio/ one is by its own documentation never loaded by the program (runtime BGM comes from themes/configured paths).
- **Fix:** Delete `audio/bgm.ogg`; the ship bundle's copy is the one users actually receive.
- **Benefit:** 324 KB repo size removed.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

## RIPTOPL-025 / Seventy docs files including dated checkpoint/plan archaeology / Category 7
- **Location(s):** RiptOPL/docs/ (159 files, ~62 MB with assets; 70 Markdown files) including RiptOPL/docs/MIGRATION-CHECKPOINT-2026-10-12.md (64 KB), RiptOPL/docs/UPSTREAM-PARITY-2026-10-12.md, RiptOPL/docs/CPP-CONVERSION-PLAN.md, RiptOPL/docs/YAOPL-PARITY-PLAN-2026-10-28.md, RiptOPL/docs/RETROACHIEVEMENTS-INTEGRATION-PLAN.md alongside RiptOPL/docs/RETROACHIEVEMENTS.md, RiptOPL/docs/COMMUNITY_REPORTS_HANDOFF_2026-10-02.md and RiptOPL/docs/community-report-evidence/ (multi-MB PNG evidence files)
- **Problem:** Completed migration checkpoints and superseded integration *plans* are kept next to the live feature docs they planned, so a reader cannot tell which document describes the program as it is. The evidence PNGs are 1.4–2.0 MB each.
- **Fix:** Delete the dated checkpoint/plan/handoff documents whose work has landed (git history preserves them) and the community-report evidence images; keep the current-state docs (GSM.md, NEUTRINO.md, SETTINGS.md class).
- **Benefit:** Estimate: 15–25 files and ~8 MB removed; docs become trustworthy again.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — inventory and sizes).

## RIPTOPL-026 / Three publishing scripts re-implement the same release plumbing / Category 2
- **Location(s):** RiptOPL/tools/publish_editions.py (635 lines), RiptOPL/tools/publish_llvm.py (248 lines), RiptOPL/tools/publish_lite.py (186 lines); plus RiptOPL/tools/package_edition.py (239 lines) and RiptOPL/tools/build_edition.py:52 (channel logic the publishers depend on)
- **Problem:** Three publishers for three channels of the same release process duplicate staging, naming, checksum and upload logic; a fix to e.g. checksum generation must be made in up to three places.
- **Justification for consolidation:** Same logic in 3 places, and merging them into publish_edition.py's existing channel parameter (build_edition.py already models channels as data at :52-70) removes code overall.
- **Fix:** One publisher driven by the existing channel table; delete the two channel-specific scripts.
- **Benefit:** Estimate: 250–350 lines removed.
- **Effort:** M. **Risk:** Med. **Confidence:** Med (READ — sizes and shared channel model; INFERRED for exact overlap fraction).

## RIPTOPL-027 / The NBD server is embedded in every build, including every Lite build / Category 8
- **Location(s):** RiptOPL/tools/lite_devices.py ALWAYS set (`"lwnbdsvr"`, `"ata_bd"` kept for every device, with the reasoning in the module docstring), RiptOPL/src/app/lwnbdctl.cpp, IOP target `lwnbdsvr` in RiptOPL/ps2.yaml:210
- **Problem:** A network block-device *server* — the PS2 exporting its HDD to a PC — is a power-user data-recovery tool, yet its IOP module and EE control code ride in every ELF including the single-device Lite builds whose entire purpose is leaving things out. The lite docstring keeps it deliberately, but that makes "Lite" mean "everything except other game sources".
- **Fix:** Make lwnbdsvr a configuration (the configuration system at build_configuration.py:7 already exists for exactly such switches) rather than an always-embed; default it on for the full editions only.
- **Benefit:** Estimate: one IOP module (~10–20 KB, INFERRED) plus lwnbdctl's EE code removed from 16 lite ELFs.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — the ALWAYS list and its docstring).

## RIPTOPL-028 / HTTP as a game source: a full extra source stack for the least-capable transport / Category 8
- **Location(s):** RiptOPL/src/sources/httpsupport.cpp (924 lines), RiptOPL/include/httpsupport.h:37, RiptOPL/src/platform/httpcatalog.cpp (265 lines), RiptOPL/src/platform/httpclient.cpp (161 lines), `http_cdvdman` target in RiptOPL/ps2.yaml:447
- **Problem:** HTTP gets the full per-source treatment — EE source class, catalog fetcher, IOP client and its own cdvdman variant — parallel to BDM/ETH/HDD/MMCE/UDPFS. It is the only source that cannot serve many game formats natively and it multiplies the per-source cost documented in RIPTOPL-018. (Assessed as a removal *candidate*: this audit cannot verify from code how many users run it.)
- **Fix:** If no maintainer can name its users, delete the HTTP source stack and its cdvdman variant; games over HTTP remain reachable via any HTTP→SMB/UDPFS bridge on the PC side (PS2-Servers' job, not the loader's).
- **Benefit:** Estimate: ~1,350 EE lines plus one IOP cdvdman variant and one catalog subsystem removed.
- **Effort:** M. **Risk:** High. **Confidence:** Med (READ for size/structure; INFERRED for user count — flagged honestly as the decision the maintainer must make).

## RIPTOPL-029 / CPU plasma background is rendered every frame / Category 5
- **Location(s):** RiptOPL/src/gfx/plasma.cpp (260 lines; TODOs at :145 and :151 note the mixing should move to VU0), RiptOPL/src/ui/gui.cpp:85, RiptOPL/src/ui/gui.cpp:136 (plasma shown as the default screen background), call sites RiptOPL/src/gfx/themeelems.cpp:1302, :1333, :1724
- **Problem:** The default menu background is a per-pixel CPU plasma effect computed continuously on the EE, in a UI whose first requirement is never to stutter while background workers stream art and index games. Its own TODOs concede the computation is in the wrong place.
- **Fix:** Pre-render a small tile/gradient texture once at init (themes already support static background images — themeelems.cpp:914-928) and delete the per-frame generator, or keep the effect but render it to a texture once and reuse it until the palette setting changes.
- **Benefit:** Estimate: one full-screen CPU effect per frame removed from the menu loop; 260 lines reduced to a static-texture path that already exists.
- **Effort:** M. **Risk:** Med. **Confidence:** Med (READ — file, TODOs and call sites; frame cost INFERRED, not profiled on hardware).

## RIPTOPL-030 / Monolith source files concentrate the UI and per-device logic / Category 3
- **Location(s):** RiptOPL/src/ui/guisettings.cpp (3,497 lines), RiptOPL/src/ui/menusys.cpp (3,400), RiptOPL/src/sources/hddsupport.cpp (3,383), RiptOPL/src/sources/vcdsupport.cpp (3,259), RiptOPL/src/sources/supportbase.cpp (3,181), RiptOPL/src/app/settingsio.cpp (2,611), RiptOPL/src/ui/dialogs.cpp (2,409), RiptOPL/src/sources/bdmsupport.cpp (2,408)
- **Problem:** Eight files over 2,400 lines hold most of the program. This is not a call for new abstraction — it is why every change needs a 277-file grep-test suite (RIPTOPL-005) to feel safe, and why copy-paste like RIPTOPL-018 survives: nobody can hold a 3,400-line file in review.
- **Fix:** No restructure on its own. As each of the deletions above lands (wOPL layers, launch legs, HTTP source), let these files shrink; split a file only when a deletion leaves a clean seam. Explicitly do *not* start a refactoring programme.
- **Benefit:** Indirect: reviewability; measured only as the lines the other findings remove from these files (estimate 1,000+).
- **Effort:** L. **Risk:** Med. **Confidence:** High (READ — line counts).

## RIPTOPL-031 / tests/host stub headers: twenty identical empty SDK stubs / Category 7
- **Location(s):** RiptOPL/tests/host/sdk_stub/ (20 files with identical MD5: debug.h, dmaKit.h, iopcontrol.h, kernel.h, libcdvd.h, libmc.h, libpad.h, loadfile.h, netman.h, sifrpc.h, etc.) plus a second stub tree at RiptOPL/tests/host/stub/ with duplicated names (tamtypes.h, ioman.h exist in both)
- **Problem:** Two parallel stub include trees for the host tests, one of them twenty copies of the same empty file. When a host test needs a real declaration, nobody can tell which tree is authoritative.
- **Fix:** Collapse to one stub directory; keep the empty stubs (they are one line each in effect) but in one place.
- **Benefit:** ~20 files and one include tree removed; small in lines, real in confusion.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — MD5-identical group).

## RIPTOPL-032 / Tracked prebuilt debug IRX binaries in modules/debug / Category 7
- **Location(s):** RiptOPL/modules/debug/drvtif.irx, RiptOPL/modules/debug/drvtif-ingame.irx, RiptOPL/modules/debug/tifinet.irx, RiptOPL/modules/debug/tifinet-ingame.irx
- **Problem:** Four compiled IOP binaries are tracked with no adjacent source or build rule found in this pass, while every other IRX in the loader is built from source by ps2.yaml. Either they are DECI2 debug inputs that only a DTL-T10000 owner can use, or they are outputs that escaped `.gitignore`.
- **Fix:** If they are SDK-derived inputs, move them beside the other documented prebuilts with an ORIGIN note; if outputs, delete and ignore them. (This audit lists them as a question with evidence, not a certainty.)
- **Benefit:** Small bytes; removes "is this source or output?" ambiguity for four binaries.
- **Effort:** S. **Risk:** Low. **Confidence:** Med (INFERRED — absence of a build rule is a negative result from the greps run).

## RIPTOPL-033 / 35 translations maintained as a fork overlay on top of a downloaded set / Category 3
- **Location(s):** RiptOPL/Makefile:191 and RiptOPL/tools/assets.py:42 (the same 35-language TRANSLATIONS list, duplicated), RiptOPL/lng/fork/ (35 YAML files), RiptOPL/tools/assets.py:98, RiptOPL/tools/assets.py:105 (downloads lng_src at build time, compiles each language plus the fork overlay)
- **Problem:** The language list itself is duplicated between the dead Makefile and assets.py, and every build downloads and recompiles 35 languages, most of which have no active translator in this fork (the fork overlay exists precisely because upstream files lack this fork's strings).
- **Fix:** Delete the Makefile copy of the list (goes with RIPTOPL-003); ship the languages the fork actually maintains and let the rest be user-installed `.lng` files, which the loader already supports loading from storage.
- **Benefit:** Build no longer depends on a network fetch for translations; per-language maintenance becomes honest. Lines: the duplicated list (~10) plus overlay files for unmaintained languages (count not asserted).
- **Effort:** M. **Risk:** Med. **Confidence:** Med (READ — duplication and build flow; INFERRED for translator activity).

## RIPTOPL-034 / Commented-out and legacy code markers in first-party code / Category 1
- **Location(s):** `#if 0` at RiptOPL/modules/hdd/apa/src/cache.c:55, RiptOPL/modules/mmce/mmceman/src/sio2man_hook.c:26, RiptOPL/modules/usb/usbd-ra/src/usbd.c:200; FIXME at RiptOPL/src/core/config.cpp:17 ("We should not need this function" — the function is still there and still used); HACK workaround at RiptOPL/src/core/bgm.cpp:890; TODO at RiptOPL/src/sources/bdmlaunch.cpp:983 (`fakemodule_flags |= 0 /* TODO! fake ilinkman ? */` — a no-op statement kept as a reminder)
- **Problem:** Individually tiny, collectively these are the visible residue of unfinished cleanups in first-party code (vendored lwIP TODOs under modules/network/SMSTCPIP are excluded — that is upstream's file). bdmlaunch.cpp:983 is literally a zero-effect statement.
- **Fix:** Delete the three `#if 0` blocks and the no-op line; either do the config.cpp:17 FIXME (use newlib stat, as it says) or downgrade it to a plain comment describing why the function exists.
- **Benefit:** Estimate: 50–150 lines removed; no behaviour change.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

## RIPTOPL-035 / README is 1,116 lines and doubles as the manual, the changelog and the feature spec / Category 7
- **Location(s):** RiptOPL/README.md (1,116 lines), RiptOPL/CHANGELOG (340 lines), RiptOPL/ROLLING_RELEASE.md, plus the per-feature docs under RiptOPL/docs/
- **Problem:** The README contains full feature documentation (e.g. the GSM 1080p section at README.md:443) that duplicates docs/GSM.md and the settings docs, so fixes must be written twice and the two copies drift.
- **Fix:** Cut the README to what a landing page needs (what it is, download, install, links into docs/) and delete the duplicated prose; docs/ already holds the long form.
- **Benefit:** Estimate: 600–800 README lines removed and one copy of each duplicated section.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

## RIPTOPL-036 / Diagnostic build holds a static screen for a full 8 seconds / Category 5
- **Location(s):** RiptOPL/src/app/opl.cpp:1929 (`char diagnostic[1536]`), RiptOPL/src/app/opl.cpp:1951 (`DelayThread(8000000);` — 8 s, after drawing the diagnostic for 2 frames)
- **Problem:** In the diagnostic path the boot blocks for eight full seconds displaying the message. That is presumably intentional for readability, but 8 s is longer than a user needs to photograph a screen, and it is paid on every diag boot failure.
- **Fix:** Wait for a pad press with a shorter timeout (the pad code is already initialised in this path's callers), or reduce to 3 s. No new mechanism.
- **Benefit:** Up to 5 s per diagnostic boot.
- **Effort:** S. **Risk:** Low. **Confidence:** Med (READ — the lines; the function's exact gating was sampled, not traced end-to-end).

---

## Test evidence (results only)

- `python3 .github/scripts/test_selfupdate_host.py` — **PASS** (52 checks) at the pin.
- `sh tools/run_tests.sh` — runs all 277 test files serially; **did not complete within 120 s** in this sandbox and was stopped by the auditor; no failure had been printed before it was stopped. Reported as evidence for RIPTOPL-005, not as a pass or a fail.
- No PS2/EE/IOP build was run: the ps2build toolchain is not present in this sandbox, so no ELF/IRX sizes are claimed as measured. Every size benefit above is an estimate from tracked file sizes, which were measured.

## Files not read and why

- **Vendored third-party source** (`third_party/mbedtls/library/*.c` beyond the build glob, `modules/network/SMSTCPIP/` lwIP, `modules/bdm/bdmfs_fatfs/fatfs/ff.c` beyond the cited ranges, ps2sdk-derived module sources under `modules/`): read at inventory/config level only — they are pinned upstream copies; auditing them line-by-line would produce findings for upstream, not for this repo. The exceptions (ffunicode/ffconf, UDPFS copies, iLink copies) are cited above.
- **Binary assets** (PNG/ICN/TM2/ADP/DER files, `ship/**/*.ELF|irx|BIN|KELF`, `third_party/korium/assets.zip` contents beyond its listing): inventoried by name/size/hash, not decoded. Note: one tracked file is a KELF (`ship/POPS/POPSTARTER.KELF`) — per instructions nothing about key material is reported; it is counted only as a shipped binary under RIPTOPL-010/002.
- **ee_core/** (14,379 lines): inventoried and sampled (configuration overlays in tools/build_configuration.py read in full); not line-audited. Its per-feature sources (ra.c, igs_api.c, freeze_diag*.c) are accounted for through the edition/configuration findings rather than individually.
- **Generated/compiled language files** (`lng/*.lng` are build outputs of the YAML read), `asm/` generated bin2c output (not tracked), and `.github/scripts/fixtures/`: not read.
- **Most of `src/` beyond the files cited**: the cited files were read in the ranges quoted; other `src/` files were grep-swept (unsafe functions, TODO/FIXME, `#if 0`, `.bak`, large arrays, infinite loops, malloc counts — 161 malloc sites in `src/`) but not read end-to-end. Findings claim only what the cited lines show.
- **Docs prose** beyond inventory/sizes and the README spot-checks: not read for technical accuracy (licences excluded per instructions in any case).

## Categories unfinished

- **Category 6 (bugs/security)** is *partially* covered: the unsafe-string and unchecked-allocation sweeps were run repo-wide in `src/`, but IOP module C code (`modules/`, `ee_core/`) was not swept to the same depth, and no dynamic testing on hardware or emulator was performed. A dedicated IOP pass could be run separately.
- **Category 5 (performance)** is covered only where code structure makes the cost self-evident (RIPTOPL-019, -029, -036); nothing was profiled on real hardware, per the project's own standard that emulator/build results are not hardware proof.
- All other categories (1, 2, 3, 4, 7, 8) are covered for the EE/UI/build/repo surface at this pin.
