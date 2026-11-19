# RiptOPL — Round 7 — Docs prose vs code

- **Repo / pin (READ):** `NathanNeurotic/RiptOPL` at `2e347d5ac885cd0ff73e9ad4143781946396f492`, clone `~/workspace/audit-clones-2026-11-13/RiptOPL`, git-clean at start of section. READ ONLY; no files in the clone were modified.
- **Scope:** user-facing prose docs checked claim-by-claim against the implementation at the pin. Defaults were checked in `src/core/settings.cpp` / `src/app/opl.cpp` (`setDefaults`) / `src/app/settingsio.cpp`; limits in headers and support modules; packaging claims in `tools/package_edition.py`, `tools/publish_editions.py`, `tools/publish_llvm.py`, `tools/build_edition.py`, `tools/build_configuration.py`, `tools/lite_devices.py` and `.github/workflows/configurations.yml`.
- **Method:** three outcomes only — CONTRADICTION (candidate finding, both citations quoted), CONFIRMED (one line, code cite), UNVERIFIABLE from source (listed, not filed). Wording/style nits were not filed.
- **Counts:** docs read in full **16**; claim groups checked **67** = confirmed **56**, contradicted **4**, unverifiable **7**. (`docs/SETTINGS.md` was used only as a cross-reference for the DEFAULTS "Library" row and is not counted as a full read.)

## Docs read in full (16)

`README.md`, `ROLLING_RELEASE.md`, `docs/DEFAULTS.md`, `docs/COMPRESSED-IMAGES.md`, `docs/EXFAT.md`, `docs/AUTOLAUNCH-GLOBAL-SETTINGS.md`, `docs/AUTOMATIC-VMC.md`, `docs/CFG-TITLES.md`, `docs/CONTROLS.md`, `docs/BUILD-CONFIGURATIONS.md`, `docs/APA-SAFETY.md`, `docs/CHECK-FOR-UPDATES.md`, `docs/FAVORITES-STORAGE.md`, `docs/FILE_MANAGER.md`, `docs/CONSOLE-ILINK-ID.md`, `docs/DESR-DVR.md`.

## Out-of-class ledger (not audited in this section, per task)

Internal planning / handoff / audit-history docs under `docs/`, noted and skipped: `FORK-GAP-ECOSYSTEM-AUDIT-2026-10-29.md`, `FORK-GAP-FULL-REFRESH-2026-10-29.md`, `FORK-GAP-REFRESH-2026-10-29.md`, `FORK-GAP-STATUS-2026-10-28.md`, `FORK-GAP-STATUS-2026-11-01.md`, `COMMUNITY_REPORTS_HANDOFF_2026-10-02.md`, `CPP-CONVERSION-PLAN.md`, `DOCUMENTATION-GAPS.md`, `EMBER-INTEGRATION-PLAN.md`, `HTTP-INTEGRATION-PLAN.md`, `RETROACHIEVEMENTS-INTEGRATION-PLAN.md`, `MULTI-VIEW-THEMES-PLAN.md`, `YAOPL-PARITY-PLAN-2026-10-28.md`, `MIGRATION-CHECKPOINT-2026-10-12.md`, `UPSTREAM-PARITY-2026-10-12.md`, `HARDWARE-FIRST-REPORT-2026-10-18.md`, `ILINK-DISCOVERY-PARITY-2026-10-18.md`, `ILINK_CODE_AUDIT_2026-10-22.md`, `ILINK_TIMING_INVESTIGATION_2026-10-21.md`, `VMC-AUDIT-2026-10-08.md`, `COUNTER-287.md`, `TALES-REBIRTH-285.md`, `SAVED-THEME-BOOT-FREEZE.md`, `TECH_CONTRIBUTOR_ROADMAP_2026-10-03.md`, `PS2BUILD-MIGRATION.md`, `PS2SDK-NOTES.md`, `MAINTAINING-DOCS.md`, `FORK-COLLABORATION.md`, `TEST-BUILDS.md`. All other `docs/*.md` files were out of the prioritised set for this section and were not read in full.

---

## Candidate findings (unnumbered)

### Docs — README says menu vibration is on by default; the fresh-start default is Off

- **Severity:** Medium (user-facing default; a fresh install silently does the opposite of what the README promises, and the repo's own defaults page disagrees with the README).
- **Doc (READ):** `README.md:258` — "Controller vibration in the menus **(on by default)**".
- **Code (READ):** `src/core/settings.cpp:173` — `intSetting(CONFIG_OPL_RUMBLE, &gEnableRumble, 0)`, with the table comment at `src/core/settings.cpp:170-171`: "Audio and menu rumble start off, like the original".
- **Corroborating docs (READ):** `docs/DEFAULTS.md:23` lists "Controller Vibration in Menus | Off"; `docs/CONTROLS.md` (Rumble section) says it "is off on a fresh install". The README is the outlier on both sides.
- **Fix shape (LESS):** change the README parenthetical to "(off by default)". No code change.

### Docs — ROLLING_RELEASE download contract names the published ZIPs without their version suffix; every published archive is renamed to carry the version

- **Severity:** Medium (a user hunting a release page for the literally-named files will not find them; the same document set elsewhere states the opposite rule).
- **Doc (READ):** `ROLLING_RELEASE.md:102` — "`RIPTOPL-src.zip`: Git's source archive of the exact built commit"; `ROLLING_RELEASE.md:103` — "`RIPTOPL-LANGS.zip`"; likewise the unversioned bundle names at `ROLLING_RELEASE.md:110` ("`RIPTOPL-VARIANTS.zip`"), `ROLLING_RELEASE.md:111` ("`RIPTOPL-DEBUG.zip`"), `ROLLING_RELEASE.md:113` ("`RIPTOPL-BUGREPORT.zip`") and `ROLLING_RELEASE.md:117-118` ("`RIPTOPL-RA-VARIANTS-IncebtIon.zip`"), all under the heading claim at `ROLLING_RELEASE.md:94`: "Each channel carries exactly these assets".
- **Code (READ):** `tools/publish_editions.py:286-289` — `versioned()`: "every archive ends with the version (`RIPTOPL-Korium-v1.2.0-Beta-3925.zip`)"; `tools/publish_editions.py:292`ff `name_assets()` renames every `.zip` in the channel folder and rewrites `SHA256SUMS.txt` with the versioned names before upload. The unversioned names exist only as pre-publication staging names (`tools/publish_editions.py:163`, archive created as `RIPTOPL-{name}.zip`).
- **Corroborating doc (READ):** `docs/BUILD-CONFIGURATIONS.md:28-29` states the published rule correctly: "Every archive name ends with the build version, for example `RIPTOPL-Korium-v1.2.0-Beta-3925.zip`; the loader ELFs, PSUs, `LICENSE-USMB2.txt`, `BUILD-MANIFEST.json` and `SHA256SUMS.txt` keep fixed names". The fixed-name half of the ROLLING contract (ELF/PSU/manifest/sums/`LICENSE-USMB2.txt`) **is** confirmed against the same code; only the ZIP names are wrong.
- **Fix shape (LESS):** add `-<version>` to the ZIP names in the download contract, matching `BUILD-CONFIGURATIONS.md`. No code change.

### Docs — README collapses the Neutrino Video picker's three distinct 1080i modes into a single "1080i" entry that does not exist

- **Severity:** Low (a user following the README looks for one "1080i" choice; the picker instead forces a choice among three scaling variants the README never mentions).
- **Doc (READ):** `README.md:426-427` — "offers a structured **Neutrino Video** picker (Off / 240p / 480p / 1080i)".
- **Code (READ):** per-game picker `src/ui/guigame.cpp:1754` — `{"Off", "240p", "480p", "1080i x1", "1080i x2", "1080i x3", Default}`; global picker `src/ui/guisettings.cpp:2801` — `{"Off", "240p", "480p", "1080i x1", "1080i x2", "1080i x3"}`. There is no plain "1080i" entry in either picker.
- **Fix shape (LESS):** write "(Off / 240p / 480p / 1080i x1 / x2 / x3)" in the README. No code change.

### Docs — BUILD-CONFIGURATIONS says the bug-report publisher refuses an ELF whose program does not start 0x80 bytes in; the publisher accepts the LLVM/LLD 0x100 start

- **Severity:** Low (maintainer-facing; an LLVM bug-report build that the doc says would be refused is in fact publishable, and the RiptOPLLVM channel does publish a BUGREPORT bundle).
- **Doc (READ):** `docs/BUILD-CONFIGURATIONS.md:22-23` — "The publisher refuses one that is packed or whose program does not start 0x80 bytes into the file."
- **Code (READ):** `tools/publish_editions.py:59-62` — `check_unpacked_layout()` docstring: "LLD emits additional non-loadable MIPS headers, so its first segment starts at **0x100 rather than GNU ld's 0x80**"; `tools/publish_editions.py:72-74` — the refusal test compares the segment offset against a computed 128-byte-aligned header end (`segment_offset`), not against a fixed `0x80`.
- **Fix shape (LESS):** reword to "does not start immediately after its headers on a 128-byte boundary (0x80 for GNU ld, 0x100 for LLD)". No code change.

---

## Confirmed (claim + code cite; all READ)

### DEFAULTS.md / fresh-start defaults

1. Widescreen off / 4:3 — `docs/DEFAULTS.md:11` vs `src/core/settings.cpp:76` (`gWideScreen, 0`).
2. Menu video mode Auto — `docs/DEFAULTS.md:12` vs `src/core/settings.cpp:84` (`gVMode, 0`, load handled specially; 0 = Auto in the renderman table).
3. Theme `<OPL>`, Theme View default List for `<OPL>` — `docs/DEFAULTS.md:13-14` vs `src/gfx/themes.cpp:1507` (`defaultView = THM_VIEW_LIST` for the built-in theme).
4. Alternate Background off; Boot Theme Wait off (0) — `docs/DEFAULTS.md:16-17` vs `src/core/settings.cpp:80` (`gThemeAltBackground, 0`) and `src/core/settings.cpp:82` (`gBootThemeWait, 0`).
5. Cover/disc artwork on — `docs/DEFAULTS.md:18` vs `src/core/settings.cpp:73` (`gEnableArt, 1`) plus the disc/logo art defaults = 1 in the same table.
6. Per-game background art Off in Korium/RA, On in IncebtIon — `docs/DEFAULTS.md:19` vs `src/core/settings.cpp:140` (`#ifdef BUILTIN_KORIUM … 0`) / `src/core/settings.cpp:142` (else-branch `1`).
7. Notifications on — `docs/DEFAULTS.md:20` vs `src/core/settings.cpp:63` (`gEnableNotifications, 1`).
8. PS2 logo on — `docs/DEFAULTS.md:21` vs `src/core/settings.cpp:90` (`gPS2Logo, 1`).
9. Sound effects / boot sound / BGM off, stored volume 90 — `docs/DEFAULTS.md:22` vs `src/core/settings.cpp:172-178` (`gEnableSFX/gEnableBootSND/gEnableBGM = 0`; all three volumes `90`).
10. Controller Vibration off (DEFAULTS/CONTROLS wording) — `docs/DEFAULTS.md:23` vs `src/core/settings.cpp:173` (`gEnableRumble, 0`). (The README's opposite wording is the finding above.)
11. Remember last played off — `docs/DEFAULTS.md:24` vs `src/core/settings.cpp:103` (`gRememberLastPlayed, 0`).
12. Exit To = OSDSYS (console's own exit) — `docs/DEFAULTS.md:25` vs `src/core/settings.cpp:183` (`gExitMenu, EXIT_MENU_CONSOLE`).
13. Network Connectivity off — `docs/DEFAULTS.md:26` vs `src/app/opl.cpp:1587` (`gNetworkProtocol = NET_PROTO_OFF`) and `src/core/settings.cpp` net start mode default Disabled.
14. SMB Version fresh = SMB2/3, pre-existing file counts as SMBv1 — `docs/DEFAULTS.md:27` vs `src/app/opl.cpp:1591` (`gSMBDialect = SMB_DIALECT_V2` in `setDefaults`) and `src/app/settingsio.cpp:1775-1776` (missing/non-V2 key on load ⇒ `SMB_DIALECT_V1`).
15. NFS Export `/`, NFS Writes off — `docs/DEFAULTS.md:28` vs `src/app/opl.cpp:1542-1543` (`strcpy(gNfsExport, "/")`, `gNfsWrites = 0`).
16. Automatic VMC creation off — `docs/DEFAULTS.md:29` vs `src/core/settings.cpp:97` (`gAutoCreateVmc, 0`).
17. All Games List off; Show Hidden Games off — `docs/DEFAULTS.md:30-31` vs `src/core/settings.cpp:121-122` (both `0`).
18. Sort Games A-to-Z, out-of-range saved value reads as A-to-Z — `docs/DEFAULTS.md:32` vs `src/core/settings.cpp` (`gAutosort` default `1` = `AUTOSORT_AZ`, `include/opl.h:389`) and `src/app/settingsio.cpp:1573-1574` (out-of-range ⇒ `AUTOSORT_AZ`).
19. GameID barcode PS1 half On / PS2 half Off — `docs/DEFAULTS.md:35` vs `src/app/opl.cpp:1564` (`gPopstarterRetroGemGameID = 1`) and `src/core/settings.cpp:125` (`gApplyGameID, 0`).
20. Coverflow starts at 3 covers, centre scale 45, 200 ms animation, dimming off — `docs/DEFAULTS.md:45-46` vs `src/gfx/coverflow.cpp:30-33` (`3`, `45`, `200`, `0`).
21. PSBBN Flow 25-frame glide, Coverflow settings do not apply — `docs/DEFAULTS.md:50-51` vs `include/psbbnflow.h:14` (`PSBBN_FLOW_FRAMES 25`).
22. 720p/1080i always drawn 16:9 — `docs/DEFAULTS.md:44` vs `src/gfx/renderman.cpp:74-75` (both rows `RM_ARATIO_16_9`).

### README.md network / HDD / apps claims

23. Static network defaults PS2 `192.168.1.10`, PC `192.168.1.100`, share `games`, user `guest`, SMB port `445` — `README.md:622-623` vs `src/app/opl.cpp:1518` (PS2 `.10`), `src/app/opl.cpp:1530` (PC `.100`), `src/app/opl.cpp:1547-1548` (`"games"` / `"guest"`), `src/app/opl.cpp:1535` (`gPCPort = 445`).
24. HTTP port default `1100` — `README.md:287` vs `src/app/opl.cpp:1540` (`gHttpPort = 1100`).
25. SMB2/3 offers 2.0.2 + 2.1 first, then 3.0/3.0.2, never 3.1.1 — `README.md:609-611` vs `modules/network/smb2man/smb2man.c:513-516` (same two-stage offer, same dialect set).
26. HDD data-home priority: usable `hdd_partition` in `__common/OPL/conf_hdd.cfg`, then existing `+OPL`, then `__common/OPL/` — `README.md:709` vs `src/sources/hddsupport.cpp:1087-1157` (documented order implemented in that sequence).
27. APPS Display defaults to Mixed — `README.md:246` vs `src/core/settings.cpp:127` (`gAppsDisplay, APPS_DISPLAY_MIXED`).
28. In-Game Menu off by default — `README.md:470` vs `src/core/settings.cpp:98` (`gIgrMenu, 0`).
29. VCD BDMA Apply on Launch on by default — `README.md:717` vs `src/core/settings.cpp:134` (`gBdmaApplyOnLaunch, 1`).
30. PC CARD CF and DESR DVR off by default — `README.md:170-171` vs `src/core/settings.cpp:166-167` (`gEnablePCCF, 0`, `gEnableDVR, 0`).
31. POPSTARTER package: five builds (MAIN/DEBUG/USBDELAY/USBDELAY_DEBUG/USBDELAY_LONGER_DEBUG) and the shipped `POPS/POPSTARTER.ELF` is the DEBUG build — `README.md:386-394` vs `ship/POPS/POPSTARTER VERSIONS/` (exactly those five folders) and byte-identity (READ, `md5sum`): `ship/POPS/POPSTARTER.ELF` and `ship/POPS/POPSTARTER VERSIONS/DEBUG/POPSTARTER.ELF` both `bbc61fa490df65d9527fcea7c4118170`, MAIN differs.
32. Ember disc preference `.cue` over `.exe` over `.bin` — `README.md:323` vs `src/sources/cuesupport.cpp:346-350` ("in ITS priority order (.cue beats .exe beats .bin)").
33. GSM 1080p selection is triple-confirmed — `README.md:444` vs `src/ui/guigame.cpp:1589-1591` ("Force a THREE-STEP confirmation before it can reach disk").

### EXFAT.md / APA-SAFETY.md

34. Native OPL shares a bounded 64-entry fragment table — `docs/EXFAT.md:34`, `README.md:585` vs `modules/iopcore/common/cdvd_config.h:125` (`BDM_MAX_FRAGS 64`) and `include/neutrinolauncher.h:28` (Neutrino `kMaxFrags = 64`).
35. ATA BDM write fence protects an APA disk's first 128 MiB — `docs/EXFAT.md:69`, `docs/APA-SAFETY.md:48` vs `modules/hdd/atad/src/ps2atad.c:1607` (`ATA_BD_APA_RESERVED_SECTORS (1024 * 256)` sectors × 512 B = 128 MiB).
36. APA-Jail hybrid recognition: valid APA header + `0x55AA` + FAT/exFAT MBR entry starting beyond `0x40000` — `docs/AUTOLAUNCH-GLOBAL-SETTINGS.md:72-74`, `docs/EXFAT.md:57` vs `src/sources/hddsupport.cpp:883` (`HDD_APA_RESERVED_SECTORS 0x40000`) and `hddIsApaMbrHybrid()` in the same file (header, signature and entry-start checks in that order).
37. More than 64 sub-partitions refused — `docs/APA-SAFETY.md:44` vs `modules/hdd/apa/src/hdd_fio.c:496`ff (`nsub > APA_MAXSUB` refused; 64-entry `subs` array).
38. Code 402 = APA table unreadable, code 500 = non-512-byte logical sectors — `docs/APA-SAFETY.md:45,49` vs `include/iosupport.h:65` (402 comment) and `include/iosupport.h:68` (`ERROR_BDM_UNSUPPORTED_SECTOR_SIZE = 500`).
39. Table write fence admits only a complete checksummed `__mbr` header at LBA 0 into LBA 0–7 — `docs/APA-SAFETY.md:41` vs `modules/hdd/apa/src/table_fence.h:20-24` and the `apaFenceIsValidMbrHeader()` checks (magic, 1 KB checksum, Sony MBR magic, `__mbr` id, start 0, links inside the disk).

### AUTOMATIC-VMC.md / CFG-TITLES.md / FAVORITES-STORAGE.md

40. Auto-VMC creates an 8 MB slot-1 card (`_0`), slot 2 untouched — `docs/AUTOMATIC-VMC.md:4,10,15` vs `src/ui/guigame.cpp:660` (`AUTO_VMC_SIZE_MB = 8`) and `src/ui/guigame.cpp:706` (`"%s_0"` name, slot-0-only path).
41. Native core accepts only 8/16/32/64 MB existing cards; Neutrino accepts a formatted odd-size card as-is — `docs/AUTOMATIC-VMC.md:12,69` vs `src/ui/guigame.cpp:753` (`size != 8 && size != 16 && size != 32 && size != 64` rejected only when `loader != 1`).
42. After Cancel, up to 600 more status polls before giving up — `docs/AUTOMATIC-VMC.md:31` vs `src/ui/guigame.cpp:661` (`AUTO_VMC_CANCEL_FRAMES = 10 * 60` = 600 loop iterations, each performing the status devctl).
43. CFG titles shortened to the 128-byte title buffer — `docs/CFG-TITLES.md:15` vs `include/cfgtitle.h:7` (`CFG_TITLE_MAX = 128`); the setting itself defaults off and saves as `cfg_titles_in_lists=1` vs `src/core/settings.cpp:128` (`gCfgTitlesInLists, 0`).
44. Favourites format is OFAV v3 with v1/v2 read support — `docs/FAVORITES-STORAGE.md:4` vs `include/favsupport.h:11` (`FAV_VERSION 3`) and `src/sources/favsupport.cpp:259` (`ver < 1 || ver > FAV_VERSION` rejected).
45. `recents.bin` holds the last 10 games, newest first — `docs/FAVORITES-STORAGE.md:64` vs `include/favsupport.h:10` (`RECENT_MAX_ITEMS = 10`).

### CHECK-FOR-UPDATES.md / FILE_MANAGER.md / CONTROLS.md / CONSOLE-ILINK-ID.md / DESR-DVR.md / BUILD-CONFIGURATIONS.md / ROLLING_RELEASE.md

46. Update ELF download limited to 8 MB; redirects at most three, GitHub hosts only; four roots (Sectigo E46/R46, ISRG X1/X2) — `docs/CHECK-FOR-UPDATES.md:37,50-52` vs `src/ui/selfupdate.cpp:82` (`SU_ELF_MAX = 8 * 1024 * 1024`), `src/ui/selfupdate.cpp:143-155` (hop loop `hop < 4`, non-GitHub redirect refused), `include/ratls.h:78` (roots comment). Channel/ELF table (`docs/CHECK-FOR-UPDATES.md:12-17`) vs `tools/build_edition.py:52-66` (`update_channel()`: standard builds only, lite/variant/debug return `None`; RA-IncebtIon ELF name derived by label) and `src/ui/selfupdate.cpp:47-66` (tag/ELF defines).
47. File Manager: 12 entries per page; a file over 4 GB is refused before starting on a non-exFAT (FAT) BDM drive — `docs/FILE_MANAGER.md:31,76` vs `include/guifilemgr.h:6` (`FM_ROWS 12`) and `src/core/filemgr.cpp:304` (`o[3] != 4 && size > 0xFFFFFFFF` ⇒ refuse; FatFs type 4 = exFAT).
48. Controls timings: hold L3 half a second to switch theme view; keep-a-video-mode needs a ~2 s Accept hold with a 10 s revert — `docs/CONTROLS.md` (L3 row; "Keeping a new video mode") vs `src/ui/menusys.cpp:2736` (`MENU_VIEW_HOLD_MS 500`), `src/ui/guisettings.cpp:62` (`VMODE_KEEP_HOLD_MS = 2000`) and `include/opl.h:67` (`OPL_VMODE_CHANGE_CONFIRMATION_TIMEOUT_MS 10000`).
49. Boot recovery combos write the forced mode back: Triangle+Cross ⇒ 480p (vmode 3), Triangle+Circle ⇒ Auto (vmode 0) — `docs/CONTROLS.md` (At boot) vs `src/app/settingsio.cpp:1582-1598` (both branches set `gVMode` and `configSetInt(CONFIG_OPL_VMODE, …)`).
50. Power button: one press powers off, two presses reset — `docs/CONTROLS.md` (In-game) vs `ee_core/src/padhook.c:388-390` (1 press ⇒ poweroff combo, 2 presses ⇒ reset combo). IGR screenshot combo (shoulders + Up, GSM-gated) vs `ee_core/src/padhook.c:339`.
51. Console i.LINK ID: exactly 16 hex digits / 8 bytes, `$ILinkID` key, empty or all-zero removes the override — `docs/CONSOLE-ILINK-ID.md:3,5` vs `include/ilinkid.h:9` (`oplParseILinkID(const char *text, u8 id[8])`) and `src/sources/supportbase.cpp:1119-1122` (parse, fallback warning on invalid value).
52. DESR DVR preflight file order: `config/bsd-dvr.toml` (or adjacent `bsd-dvr.toml`), then `modules/dvrdrv.irx`, `dvrfile.irx`, `dvrfile_fhi.irx`; active `-qb`/`-bsd=`/`-bsdfs=` refused — `docs/DESR-DVR.md:13-21` vs `src/platform/sysneutrino.cpp:760-768` (flag refusals) and `src/platform/sysneutrino.cpp:776-800` (probe loop: config first at `i = -1`, then the modules array in that order). DVR ISO scan roots/subdirs (root, `/DVD`, `/CD` on `__xcontents`/`__xdata`) vs `src/sources/hddsupport.cpp:220` (`subdirs[] = {"/", "/DVD", "/CD"}`) and `src/sources/hddsupport.cpp:244`.
53. Build configurations: 15 configurations total / 14 nonstandard; workflow matrix is 34 builds — `docs/BUILD-CONFIGURATIONS.md:86-90` vs `tools/build_configuration.py:7` (`CONFIGURATIONS` tuple, 15 entries incl. `standard`) and `.github/workflows/configurations.yml:26-42` (14 Korium + RA `pademu0` + RA `ingame_debug` + IncebtIon `extras_ds5` = 17 targets × 2 toolchains = 34).
54. RiptOPLLVM carries all eight RiptOPLite devices in standard and RA form, Korium-only kits, no IncebtIon — `ROLLING_RELEASE.md:22-27` vs `tools/lite_devices.py` (`DEVICES` dict, 8 entries) and `tools/publish_llvm.py:31-48` (`expected_files()`: Korium + RA kits/bundles + `elf_name(device)` and RA variant for every device).
55. Cheat-file failure rule: a `cht.tar` member empty or over 1 MB counts as present-but-unreadable, not missing — `docs/AUTOLAUNCH-GLOBAL-SETTINGS.md:111-113`, `README.md:837` vs `src/sources/supportbase.cpp:1625` (`CHT_TAR_MEMBER_MAX (1024 * 1024)`).
56. Auto Loading diagnostics: 8 s autolaunch hold; `mass0:` wait bounded to about 12 s — `docs/AUTOLAUNCH-GLOBAL-SETTINGS.md:107,139-147` vs `src/app/opl.cpp:1951` (`DelayThread(8000000)`) and `src/app/autolaunch.cpp:204` ("delay() ticks are ~0.25 s, so about 12 s").

> Note on the count: the 56 numbered items above are claim *groups* — some items bundle closely-coupled sub-claims checked against a single code site (e.g. items 5, 9, 46, 52). No confirmed item rests on INFERRED evidence; every cite above was READ at the pin.

---

## Unverifiable from source (not filed)

1. **PS2-Servers compression behaviour** (`docs/COMPRESSED-IMAGES.md:20-23`): "CSO is built in; ZSO needs LZ4 and CHD needs libchdr; Edge supports CSO and ZSO, with no CHD decoder" — that is the separate PS2-Servers repository's behaviour; nothing in this clone can confirm or refute it.
2. **Ember BIOS size** (`README.md:308`): "`EMBER/bios.bin` (exactly 512 KB)" — RiptOPL only checks the file's existence (`cueResolveEmberBios`, `src/sources/bdmlaunch.cpp:83-86`); the 512 KB requirement belongs to the bundled Ember binary, whose source is not in this repo.
3. **Bundled Neutrino version in the shipped kits** (`README.md:154`): "v1.8.0-61-g7be8de2 from 2026-08-31", and the SMB/NFS backend merge dates (2026-10-31 / 2026-11-12) — the Neutrino archive is fetched at packaging time (`tools/package_edition.py`, `--neutrino-archive`); no archive or version string is pinned in this source tree to check against.
4. **APA "up to 2 TB"** (`README.md:709`): the APA driver's LBA48 path exists in `modules/hdd/atad`, but no source constant states a 2 TB APA ceiling; the figure is a format/ecosystem property, not verifiable from this code.
5. **IGS stack-byte measurements** (`docs/BUILD-CONFIGURATIONS.md:93-95`): "2,712 bytes … 3,072-byte linker guard … 8,128 bytes with GCC `-Os` and 6,040 with LLVM `-Oz`" — historical build outputs; the guard's existence is in the linker script, but the three measured remainders cannot be re-derived from source without performing the builds (out of scope for a read-only docs pass).
6. **sceCdRI growth 104 → 232 bytes** (`docs/CONSOLE-ILINK-ID.md:13`): a past GCC measurement of an embedded backend; not reproducible from source text alone.
7. **Hardware-behaviour claims** throughout (e.g. `docs/CONTROLS.md` on DualShock analog-mode rumble availability, `docs/EXFAT.md:13` on ATA speed neutrality): the docs themselves mark console acceptance as pending; source can show the code path exists but cannot establish the hardware claim.

---

## Section summary

The prioritised user-facing docs are unusually well-aligned with the code at this pin: every fresh-start default in `DEFAULTS.md`, every numeric limit checked (64 fragments, 128 MiB fence, 8 MB update cap, 8/16/32/64 MB VMC sizes, 128-byte CFG title buffer, 12-row File Manager pages, 10 recents, OFAV v3), and every packaging-scope claim in `ROLLING_RELEASE.md`/`BUILD-CONFIGURATIONS.md` verified except the four contradictions above — one user-visible default inverted in the README (rumble), one download contract naming files that are renamed before publication, one picker description that hides a three-way choice, and one publisher rule stated for GNU ld only. All four fixes are doc-side, one line each; no code change is recommended by this section.
