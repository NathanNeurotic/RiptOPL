# C++/ps2build migration: production parity ledger

The continuation tracker, [issue #20](https://github.com/NathanNeurotic/RiptOPL/issues/20), is closed.
For the current state, including fixes merged since the port and the [RIPPS2](https://gitlab.com/akilluminati247/RIPPS2) audit, read
[Fork-gap status: 2026-10-28](FORK-GAP-STATUS-2026-10-28.md).

## Status at 2026-10-28

The port is merged: [#77](https://github.com/NathanNeurotic/RiptOPL/pull/77) (merge `894dca36`,
2026-10-27) reached parity with Open-PS2-Loader `38ea46a`, and the final sweep against `4cfde79c`
found every merged change present except Korium items K1 to K3, which were ported inside #77
(`7c21f5c0`). The October 27 and 28 fixes (#90, #94, #95, #96 here; #1074 to #1080 in the
original) are in both repositories; Open-PS2-Loader `94e82f2b` is the current parity point. The
RA browser, client mode, HTTPS lookup, ORBIT handoff, DVR and PC CARD CF rows below are ported,
which supersedes the "Remaining gaps found by this audit" list in the `823411d` refresh. All of it
is source and host tested only; none of it is tested on hardware.

## Native BDM read alignment (2026-10-19)

Original `35af507b6a78a10c6b567c70e067742cf59097cf` includes upstream OPL #1783's
unaligned-read fix, absent from this migration. This continuation ports only
that BDM read change, excluding the same commit's unrelated game patch entries.
The actual-function host regression fails on the old driver-facing buffer
alignment and passes with the aligned bounce buffer, including short reads,
destination guards and semaphore checks. Aligned reads keep their fast path.
This does not establish iLink hardware acceptance or a causal link to #63.
Compiler/package validation and the existing PR queue remain separate gates.
## Refresh at original `823411d` (2026-10-26)

First-parent merges in `b35ff38c..823411d`, audited against this branch. Every
row is source/host-tested only; none is hardware-tested.

| Original work | Migration status |
| --- | --- |
| #1016 Slow Reads, #1026 BDMA equip pair, #995 ZSO fd 0, #1009 Beat Down defaults | Ported in `16ddf04`, `958c4f1`, `f78e425`, `c8b04e2`. |
| #991/#983 fragment budget, #980 tab sound, #994/#1015/#986 native VMC extent and Rebuild | Ported in `40a877c`, `81d7eee`, `3c7efe5`, `bab7ceb`. |
| #1037/#1035/#1017/#1043 UDPFS art and languages, language page | Ported in `4e19fa1`, `8d7d437`, `b03feae`, `38a9f6a`. |
| #1034 Ember discs.txt, #1038 CRT font size, #1049 RetroGEM keys | Ported in `1f3f743`, `601e497`, `8da926a` + `c72582f`. |
| #1039 RA mark placement | Ported in `07b4a38`. |
| #973/#990/#999/#1004 power-off, IGR wakeups, Fatal Fury BA2 | Ported in `bfc9c1c`. |
| #1022/#1024/#1031/#1042 i.LINK drivers | Ported in `240b84b`. Legacy 23,452-byte driver acceptance removed; 27,145 bytes is current. |
| #977/#1023 PSBBN Flow | Ported in `08f6eb0`, plus a fix: disk-theme discovery no longer drops a live built-in selection. |
| #1005 MMCE end of directory | Ported in `f5fa0b2` as an embedded `mmceman_fork`. The ps2build package also lacked the close-slot and ENOENT fixes, so all four ship. |
| Original PS1 scan hardening (pre-`b35ff38c`) | Ported with this refresh: a listing cut short, an unreadable game folder or a failed allocation keeps the last-good Ember list, and rows grow on demand instead of reserving 2,048. |
| Original 3D `SaveIcon` element (fork-gaps OR1) | Ported with this refresh. Each source names a PS1 row's VMC folder through `GameSource::ps1SaveDir`, and favourites resolve VMC paths through `rowPrefix`, so the element adds no upward dependency. Ethernet and HDD rows, Ember and POPSTARTER, are covered as in the original. Default off. NOT TESTED on hardware. |
| Original settings hint sweep (`-1` hints) | Ported with this refresh: 138 dialog rows now carry the original's hint (93 strings, translations carried over). Rows for features RiptOPL does not have yet (DVR, PC CARD CF, RA mode/host IP/experimental cards, Neutrino argument preview) get theirs with those features. |
| Original `tools/audit_translations.py` | Ported with this refresh and run by the host tests. Fixed on the way: after a build has merged the overlays into `lng_src/`, the original reports a bad overlay string as a community warning, so `--strict` passes it (original issue #1055). |
| Original Show Launch Arguments (`neutrino_show_args`) | Ported with this refresh. The preflight shows the exact composed argv and Back stays in the menu with no overflow warning; autolaunch and the real handoff never ask. The question goes through `host::ui().confirmId`, so the platform layer gains no UI dependency. Default off. NOT TESTED on hardware. |
| Original real media size for CDVDMAN (upstream OPL #1763) | Ported with this refresh. `cdvdman_settings_common` gains `mediaLsnCount` (HTTP tripwires move by 4), `searchfile.c` prefers it over the PVD, and BDM, SMB, MMCE, HDD and HTTP fill it: a ZSO counts by its header, an unmeasured part makes it 0 (keep the PVD). `sbPrepare` clears it. NOT TESTED on hardware. |
| Original cheat selection memory (upstream OPL #1748) and Neutrino cheats | Ported with this refresh. Select-mode picks are saved by name in the game CFG (`$CheatSelNNN`), master codes stay on, and Neutrino launches get the enabled codes as `-cfg=riptopl-cheats` from all four Neutrino legs. The hand-over is split by layer: `sysNeutrinoHandCheats` (platform) writes the codes it is given, `sbNeutrinoHandCheats` (sources) reads the cheat engine. Fixed on the way: the UDPFS and MMCE Neutrino legs went back to the menu with the cdvdman patch zone still patched, so the next launch's `sbPrepare` failed. NOT TESTED on hardware. |
| Original in-game freeze report (INGAMEDIAG) | Ported with this refresh as the `ingamediag` configuration (korium and incebtion; it refuses RA, which runs IGR from VBLANK). After a freeze the reset combo records the game's threads and CDVDMAN's disc/VMC activity, fetches the IOP record with bounded RPC waits, and `freezediag.irx` writes `mc0:/RIPTFRZ.TXT`. Every hunk sits behind `#ifdef INGAMEDIAG`; standard builds are unchanged. As in the original, a failed writer load never traps IGR in the out-of-IOP-memory loop. NOT TESTED on hardware. |
| Original memory-card folders (`test_mc_folders.py`) | Ported with this refresh. A card Custom Settings Path (`mc1:`, `mc1:OPL`, a folder not yet on the card) is normalized and its folders are made at save time; an `mc?:` boot dir resolves to the card holding the ELF; only a bare `mcN:` boot keeps a recovered same-card folder as its save home. Reading the BDMA setting or checking the launch environment no longer creates an empty `POPSTARTER` folder (shown as Corrupted Data); a folder is made only when there are modules to put in it, and it always gets icons. NOT TESTED on hardware. |
| Original HDD module lifetime (`test_hdd_module_lifetime.py`) | Ported with this refresh. Repeated ATA readiness probes share the one DEV9 owner taken by the first successful load instead of inflating the count; page re-entry reuses the resident modules without re-running the 1 s settle; a non-terminal shutdown (game launch) no longer idles the drive a BDM-over-ATA game may still read; exit idles it and releases DEV9 exactly once. A failed DEV9 load is now an error rather than a silent attempt to load ATAD. NOT TESTED on hardware. |
| Original #868 custom font size, #961 NULL config, config escape round trip | Ported with this refresh. `fntReadData` passed an uninitialized size to `readFile()`, which treats any positive value as the exact expected size, so a valid custom theme TTF could fail and fall back to the default font. A failed config allocation no longer opens the game menu with stale values (Triangle stays on the list), and the game-settings load/save/remove handlers refuse a NULL config. The escape round-trip test was already satisfied and is added for coverage. NOT TESTED on hardware. |
| Original IGR polling test (`test_igr_polling.py`, #984) | Ported with this refresh. The VBLANK_END handler is split as in the original into `IGR_CheckInputs` (pad and power-button sampling) and `IGR_StopDmaAndGs`, with the original's design note on why the two alarm-based samplers must not return. Behaviour is unchanged; the test compiles the real handler, worker, installer and reset hook for standard and RA builds. `IGR_RequestReset` and the GS tracker stop arrive with RA client mode and experimental cards. NOT TESTED on hardware. |
| Original Ember missing-core test (`test_ember_scan_missing_core.py`) | Ported with this refresh. The scan side was already here; the launch messages were not. A missing POPSTARTER.ELF on BDM, SMB, MMCE and HDD now names the path that was looked for (the custom path when set; on HDD `hdd0:__common/POPS/POPSTARTER.ELF`, not the internal mount). HDD Ember names the partition (`hdd0:__.EMBER/EMBER/ember.elf`), a partition that will not mount no longer claims ember.elf is missing, and an HDD PS1 favourite whose game is gone no longer says POPSTARTER is missing. NOT TESTED on hardware. |
| Original L3 PS1 rescan (`test_libview_l3_rescan.py`) | Ported with this refresh. L3 at a device root now marks the page dirty, so a BDM device whose ISO list already loaded this generation scans its PS1 library instead of committing an empty, never-scanned PS1 list. NOT TESTED on hardware. |
| Original mcemu VMC slots (`test_mcemu_slots.py`) | Ported with this refresh. BDM and HDD already reused the slot position; SMB and MMCE still searched for the marker each launch, so after any launch that came back to the menu the next game silently ran without its VMC (and SMB read past the image). Both now use `nativeVmcSlotWord` with their own cache. NOT TESTED on hardware. |
| Original patch zone restore (`test_patch_zone_restore.py`) | Ported with this refresh. SMB, HDD, HTTP and MMCE now return when `sbPrepare` finds no patch zone instead of writing settings at a stale index, and the MMCE slot-lost and ISO-open failures restore the zone before returning, so the next MMCE launch can find it. NOT TESTED on hardware. |
| Original theme Title fallback (`test_theme_metadata.py`) | Ported with this refresh. A `Title` attribute text with no (or an empty) Title in the game's metadata shows the library name instead of nothing, without writing a made-up Title back to the CFG. Other attributes are never invented. NOT TESTED on hardware. |
| Original VMC contiguity probe (`test_vmc_contiguity.py`) | Ported with this refresh. The after-creation check asked bdmfs_fatfs `CHECK_CHAIN`, which answers 0 for any failure as well as for a fragmented file, and trusted MMCE's success return for an ioctl it does not implement. It now asks for the fragment count and requires `GET_LBA` to replace its sentinel first, so an unknown answer stays silent instead of warning that a good VMC is fragmented. NOT TESTED on hardware. |
| Original BDM ATA readiness (`test_bdm_ata_readiness.py`) | Ported with this refresh. With APA Off, a drive still spinning up when ATAD probed at module load was never registered with BDM: the HDD loader settles after ATAD/XHDD init and nothing re-probed. The BDM ATA arm now issues one read-only `xhdd0:` `ATA_DEVCTL_IS_48BIT` devctl after a successful load, so xhdd re-runs the retryable `sceAtaInit`; the residency latch is kept for an absent drive so idle refreshes do not loop. The test compiles the real loader, atad and xhdd code and fails against the previous arm. NOT TESTED on hardware. |
| Original DESR DVR ISO discovery (#955 opt-in, `test_dvr_scan_lifecycle.py`) | Ported with this refresh. With Game Sources > DESR DVR ISOs on (default off) and the DVRP capability bit set, the HDD page loads `dvrdrv`/`dvrfile` once, mounts `dvr_pfs0:`/`dvr_pfs1:` read-only and lists their ISOs after the HDL rows. A row launches only through the DVR Neutrino backend; a failed or truncated scan keeps the last-good rows; teardown keeps the mounts for that launch alone. Ordinary PS2 models never load a module. NOT TESTED on hardware. |
| Original PC CARD CF (#955, `test_pccf.py`, `tests/pccf`) | Ported with this refresh. With Game Sources > PC CARD CF on (default off) an early Japanese PC CARD console (ROMVER 0100/0101/0120 J, no PSXVER) claims DEV9 exclusively, loads `pccf_bd` and publishes a "cf" BDM page; games launch natively through `pccf_cdvdman` plus `pccf_game` in `BDM_CF_MODE`, with the game's DEV9/ATAD faked, no VMC, no PS1 rows and no RA telemetry. DEV9, SMAP and NBD refuse while CF owns the slot. Fixed in the port: `sysShutdownDev9` takes the same lock as the CF claim; the original's CF-page Refresh reprobe is not ported because it can never run (original issue #1061). NOT TESTED on hardware. |
| Original Neutrino PADEMU notice and core selection (`test_neutrino_pademu.py`) | Ported with this refresh. Every Neutrino leg tells the user, after the argument preflight and never during autolaunch, when the game has pad emulation on, because Neutrino has none. The game menu's core check now follows a Favorite's owning device and treats HTTP and CF as native-only and UDPFS/UDPBD as Neutrino-only. Also added on the DVR leg, which the original leaves out. |
| Original ps2ips DNS answer (`test_ps2ips_dns.py`) | Ported with this refresh. `gethostbyname` copied the address list's pointer instead of the IPv4 address it points at, so every name lookup on the menu stack returned garbage. NOT TESTED on hardware. |
| Original RA language isolation (`test_ra_language_isolation.py`) | Ported with this refresh. The language compiler now keeps `RA_`, `HINT_RA_` and `CAD_` strings out of standard ELFs. |
| Original RA menu link (`test_ra_verified_peer`, `test_ra_mstack_link`, `test_ra_caduceus_host`, `test_ra_nic_reserved`, `test_ra_udpfs_check`; merged into `test_ra_menu_link.py`) | Ported with this refresh. RA edition: Settings > Network gains RA Mode (Xerabora or Caduceus) and RA companion IP. A support check, link test or tracked launch in Caduceus mode first asks the session bridge on the companion or SMB host and never broadcasts; an explicit companion IP makes Xerabora unicast. Unicast replies are taken only from the asked host, and only a completed check or link test sets the passive game's peer, for the mode and hosts it was verified under. While UDPFS owns the NIC the requests go through the ministack's new `mstack:` device; WAIT gets 20 paced rounds, `RAA1 NO <reason>` is shown, a failed bind or a short watch-list write is refused. The original's link test prints a literal `%s` for an invalid explicit host; RiptOPL prints the console address. In-game Caduceus discovery follows in the client-mode port. NOT TESTED on hardware. |
| Original RA in-game client mode (`test_ra_client_mode.py`, `test_ra_receive_frame.py`, `test_ra_sender.py`, `test_ra_launch_resources.py`; event handling in `test_ra_ingame_events.py`) | Ported with this refresh. `raudp` is the original's: Caduceus and pinned-host discovery that never broadcasts, strict `RAO1 OK`, routed peers through the gateway's MAC, and on SMB/HTTP a passive mode that opens no socket, takes notices the SMAP driver sets aside (`SMAPReadNotice`) and holds a snapshot while a received frame waits. The PC's `RAR1` leaves the game as IGR does (not under Mode 6); unlock events carry points and a title behind a commit word, and the dead badge upload is gone. The load argument follows `RA_ARG_*`, the IOP buffer is sized to the accepted snapshot and freed, and an optional `raudp` that fails or declines residency no longer hangs the game in the -400 loop. Fixed in the port: RiptOPL's RA edition built the in-game SMAP driver without `RETROACHIEVEMENTS`, so it never exported the transmit call `raudp` imports; the original's pulse stops a running flash on a torn event. NOT TESTED on hardware. |
| Original Caduceus cards (`test_caduceus_overlay.py`, `test_ra_cards_setting.py`; reservation in `test_ra_runtime_budget.py`) | Ported with this refresh: the card renderer, its GS tracker and the `ra_experimental_cards` opt-in (Off unless exactly 1, Caduceus with telemetry only). The launch reserves the cache-aligned packet behind the watch list only when the opt-in is on and it fits below the 1 MB or 32 MB limit, else the pulse runs. Fixed in the port: a torn event no longer stops a running card or flash; it waits for the next frame. NOT TESTED on hardware. |
| Original achievement browser (`test_caduceus_browser.py`, `test_ra_xerabora_browser.py`) | Ported with this refresh: Caduceus account pages over the paired bridge and the xeRAbora library and sets, the main-menu entry named after RA Mode (rebuilt when it changes), **View achievements** on ISO game cards, and the launch, test-launch and network-settings guards while a page loads. Not yet: VCD cards (with PS1 support). The HTTPS game-ID lookup is the next row. Fixed in the port: Xerabora discovery accepted any `RAO1` reply rather than `RAO1 OK`; a PC still identifying the image (`RAA1 WAIT` after every round) and a UDP transport holding the network adapter were both reported as "turn on OPEN TO THE NETWORK". NOT TESTED on hardware. |
| Original HTTPS game-ID lookup (`test_ratls_host.py`, `test_mbedtls_vendor.py`, the HTTPS parts of `test_ra_xerabora_browser.py`) | Ported with this refresh: mbedTLS 3.6.7 as an RA-only static library (checked byte-for-byte against the release archive), the TLS client, the PS2 socket/clock/entropy half, the roots embedded by `tools/assets.py`, the game-ID lookup ahead of the library title match, the unset-clock message, and `LICENSE-MBEDTLS.txt` in the RA package. Fixed in the port: a chunked body was taken as complete whenever the bytes so far ended `0\r\n\r\n`, even inside a chunk, and then failed to de-chunk; a TCP read longer than 64 bytes whose unaligned tail exceeded 48 bytes lost the excess in ps2ips (`Ranet::tcpRecv` now peeks and reads whole 64-byte blocks, for both HTTPS and the xeRAbora HTTP fetch); and GTS Root R2, which Mozilla's store no longer carries, is not trusted. NOT TESTED on hardware. |
| Original ORBIT Neutrino RA handoff (`test_neutrino_ra.py`, `test_package_orbit_neutrino.py`, the RA cases of `test_neutrino_argv_budget.py`) | Ported with this refresh: `sbNeutrinoPrepareRA` on the BDM, APA, MMCE and UDPFS legs, `-cfg=ript-ra -ra=` ahead of `--b`, the untracked fallback when the two arguments overflow the budget, and the RA kit's `neutrino/` staged from the pinned ORBIT v1.0.2 ZIP (digest and entries checked before any write; standard kits refuse its files). Fixed in the port: the runtime marker is compared in full (the original compared 26 of its 28 bytes); the "launching untracked" notice appears only when dropping the RA arguments is what lets the launch fit, not before an overflow refusal; and ORBIT's stock `IEEE1394_bd_mini.irx` (#1018) is replaced by RiptOPL's driver as in the standard kit. NOT TESTED on hardware. |
| #1044 release build versions | Already covered: every RiptOPL build job checks out with `fetch-depth: 0`. |
| #1008, #1007, #992, #981, #1013, #1001, #989 | Already present before this refresh. |
| #1047, #998, #1025 | Original-only documentation; nothing to port. |

Remaining gaps found by this audit (superseded: all ported by #77, see Status at 2026-10-28):
- RA mode/host IP/experimental cards and PS1 watch keys in the RA badge.
- Found by comparing the original's host tests with this branch's: the
  Caduceus achievement browser, Xerabora browser, RA client mode, verified peer,
  RA over TLS and RA over Neutrino. (DVR and PC CARD CF are now ported.)

## Refresh at original `b35ff38c` (2026-10-18)

New original work after `eab6c37c`:

| Original work | Migration status |
| --- | --- |
| #975 false exFAT PS1 module warning | Adapted to `vcdPopsUdpfsCleanModules` across all enumerated USB volumes: only ambiguous EINTR opens get a complete POPS-directory absence check. Real IO errors, found/unreadable modules and failed directory scans remain guarded; no module deletion. Focused host tests pass. |
| #973 IGR alarm/input starvation | Newly merged upstream; needs a separate source/ABI audit against the migration's current pad-hook lifetime, reset and RA snapshot changes. Not imported or claimed fixed by the #975 port. |

Pending destination PRs #57 (DVR launch contract) and #58 (generation-checked
browse prefetch) remain independent of this checkpoint. Both pass CI but require
completed review evidence before merge. The older comparison rows below retain
their historical source baseline.
## IGR #973 installation prerequisite (2026-10-18)

Source baseline: original `b35ff38c`; independent destination base #56 `b56e0b19`.
The current port adapts zero-initialized worker descriptors, failed creation/start
handling, failed-start cleanup and retryable interrupt registration. A VBLANK
handler is enabled only after a live worker and successful registration exist.
The existing lifetime regression now injects those failures in standard and RA
modes, retaining pending-combo/power state and reopen tests.

This does not migrate standard builds from VBLANK to EE alarms. Input sampling,
shutdown context, reset callback generation/cancellation and alarm-allocation
recovery from original #973 remain unported. No hardware compatibility or input
starvation fix is claimed by this installation-only prerequisite.
## EE hook context prerequisite (2026-10-18)

Independent base merged #58 `1c4822fc`; upstream reference `b35ff38c`.
Port only `_SifSetDma`, `Hook_ExecPS2` and `Hook_CreateThread` GP/register
context from the original assembly. Load EE core `_gp` before entering C;
returning DMA/thread hooks restore caller GP/SP/RA, and the thread hook also
preserves argument/temporary registers across hook discovery. Keep DMA's C
return value. ExecPS2 retains its non-returning register-scrub behavior.

A source-instruction fixture models delay slots, C-call register clobbers,
stack-frame bounds, bypass paths, forwarded arguments and DMA return values.
It fails on the old GP context and passes with the port. This model is not
native instruction execution or console evidence. GCC/LLVM assembly/link and
hardware behavior remain distinct checks. No alarm-sampling or shutdown change.

## Refresh at original `eab6c37c` (2026-10-17)

Destination comparison base: `82a11663` (through merged destination PR #55).
Release enablement and later work landed elsewhere while an older checkout was
preserved locally; inspect current source before treating old ledger rows as gaps.

| Original work | Current migration status |
| --- | --- |
| #971 low-memory config/file IO | Adapted in this continuation: allocate before truncation, retain the first write/close errno, close failed file reads, and skip unused MC/PFS verification snapshots. Actual C++ method fault injection fails on the old body and passes on the new one. No storage-driver or recovery-policy replacement. |
| #962 SIO2MAN, #963/#966 parental locks, #964 logo-art control | Implementations and focused tests are present through destination #50; not reimplemented in this batch. |
| #967 black handoff/video-mode fades | Present through destination #48. |
| Config allocation/null wrappers | Destination #49 already guards the read/allocation boundary and ConfigSet free-function wrappers, with host tests. Do not redo the earlier #961 null-read report; audit settings-state behavior separately. |
| #968/#970 VMC budget/config reuse | Present through destination #51/#52. |
| #917 CJK fallback, #845 game-type labels, About/QA layout | Present through destination #53/#54/#55. |
| #947 DESR/DVR and merged #955 PC CARD CF | DVR device mapping, companion/unsafe-argument preflight and filesystem argv contract are now adapted and host-tested. Discovery, GameSource rows, driver loading, mount ownership and cleanup are now ported too (the opt-in `enable_dvr` setting, default off; `test_dvr_scan_lifecycle.py`); hardware acceptance remains; see DESR-DVR.md. #955's PCCF targets, driver and handoff are now ported too (`test_pccf.py`). Do not import #965's explicitly unmerged runtime replacement. |
| Older #959, RA/browser/ORBIT/HTTPS, SaveIcon, defaults, docs | Continue capability-by-capability auditing; these newer ports do not establish full migration parity. Old rows below are historical comparison evidence, not a fresh absence claim. |

All builds, release packages and hardware acceptance remain distinct gates.

## Historical continuation records

Merged original #959 (`2b838e09`) is partially adapted: PR #45 preserves EE
pad-hook installation across IOP buffer invalidation and bounds masked scans.
The next subset loads settings configuration in the foreground and resolves
Favorites source-device bus policy. Async queue generations/deduplication are adapted in the current prefetch port;
failed/null settings consumers and assembly GP/register handling remain open.
PRs #18/#19 and the full site import #17 are merged; Pages and the independent
wiki import are published. GCC/LLVM, host gates and all three complete release
candidate kits pass. Publication and physical-console acceptance remain open.
PR #21 merged the useful changes from PRs #12–#16 as `6988499b`; the originals
are closed as superseded. See the checkpoint for exact final CI/artifact refs.
PR #22 merged production #922 (`0a1a593a`) into `d54dfc57`. Fresh integration
GCC/LLVM, host and all three kit jobs pass; downloaded kits pass the publisher's
independent verification against that exact integration SHA. The absent newer
RA browser/Caduceus methods remain part of the broader RA port.

Branch `codex/favorites-parity-903` adapts Favorites from production #903
(`a73fb2e4`) and #904 (`906cdafe`) to `FavSupport`, `Notifications` and the C++
theme engine. PR #23 is merged as `daae402e`, with fresh integration compiler,
host and three-kit CI plus independent downloaded artifact verification passing.
PR #24 merged as `7bcb77b3`; its 32-build GCC/LLVM configuration matrix and
three standard kits pass fresh CI and independent artifact verification.
PR #25 merged POPS #904/#909 and UL bounds/recovery as `4157d5a2`; all final
integration gates, 32 configuration artifacts and three kits pass verification.
PR #27 merged older feature gaps #885/#887/#889/#898; PR #29 merged automatic
VMC creation and newer Ember missing-core behavior; PR #30 handles truncated
port downloads. Final integration `39eda4b8` passes compiler/host/32-configuration
and three-kit checks, including independent downloaded package verification.
PR #31 merged Favorites read/write failure safeguards and sdk-core v2026.10.13
native sockets at `d74bb85b`; final compiler/host/32-configuration/three-kit CI
and independent downloaded artifact verification pass. Durable replacement is
still open. PR #32 merged optional CFG list titles at `cd787705`; final native, compiler,
host, 32-configuration and three-kit validation passes, including all 35
downloaded package checks. Pages #33 deployed and scoped Wiki guidance is
published. PR #34 merged the VMC follow-ups at `01affe4d`, with final compiler/host/32-configuration/three-kit checks and all 35 downloaded packages verified. The current branch ports Favorites staged recovery; see the checkpoint.

This ledger records verified ports into the C++/ps2build repository. It does not
claim that the entire public repository is synchronized. The public source
snapshot used for the ports below is Open-PS2-Loader commit
`6d04b40e8d857368110265a18ec9fc90b7c316c4` for the initial rows,
`0a1a593ade393d2c1d28bafb09ea8f3c5cb63706` for #922, and
`90566eb863f9ed3771cdbc9702a5611b55f8ddd9` for the POPS continuation. The older
feature bodies were compared at `a0ca55c54cb32b801423a4fdc800968ff4f62c2d`.

| Public change | Destination state before this port | Port and scope |
| --- | --- | --- |
| #885: quoted custom-core configuration values | Quoted values retained escaping on each read; repeated saves could grow/corrupt backslashes and quotes. | Decode only escaped backslash and quote before enforcing the stored-value bound; preserve unknown escapes and bare values. Actual config save/read sanitizer coverage includes repeated cycles and maximum escaped values; the old body fails the same test. Existing custom path consumers remain C++ methods. |
| #887: Ember image filtering | Any subdirectory could become a game row, including VMC/group/nested-only folders. | Require a top-level case-insensitive CUE, EXE or BIN image in each game directory; inspect the directory instead of trusting d_type. Actual scan tests cover both separator forms and misleading entry types. PR #29 subsequently adapts newer missing-core listing and diagnostics; see AUTOMATIC-VMC.md and the checkpoint. |
| #889: VMC failure recovery and Neutrino warnings (partial) | Failed/short cluster writes leaked the abort semaphore; the explicit VMC dialog checked contiguity even for Neutrino. | Release the semaphore on failed/short writes and failed seeks, stop before writing after seek failure, and skip contiguity probes/warnings for effective Neutrino selection. Actual C recovery tests fail against the old body; existing non-destructive VMC checks remain. PR #29 subsequently adapts the automatic-VMC launch hook through GameSource; see AUTOMATIC-VMC.md. |
| #898: checked explicit SMB refresh | Explicit ETH Refresh did not probe a live SMB session; unconditional reconnect would disturb ART/BGM handles. | Run ECHO through EthSupport and serialized IO; healthy sessions retain handles while requesting a deferred list refresh, failed/missing sessions use existing reconnect. Recheck ownership after ECHO; coalesce probes while upgrading queued/in-flight probes for forced settings reconnects. Actual methods/menu tests cover rejection, cancellation and both request orders. |
| #903/#904: Favorites feedback, default view and centered title | Indexed removal cleared the source star before persistence; failed source toggles still played success/reloaded; new Favorites opened on PS2 only; stars followed the title. | Return the actual removal result before clearing the star; render saving/success/failure feedback independently of courtesy notices and only confirm/reload successful writes. New configs open All while remembered shelves still win. Preserve centered title anchors with symmetric room and a leading star; ordinary lists use a fixed leading column. Execute actual C++ actions/removal/config decoding/toast timing/layout with fault injection and clock-wrap coverage. This changes UI commit ordering, not disk atomicity: PR #31 subsequently handles short transfers and close failures; truncating replacement and interruption recovery remain open. |
| #922: NIC reservation, DEV9 gates and IP-conflict notice | Existing RA menu methods could start SMB before selected UDP modules loaded; three UDP chains ignored DEV9 failure; no IP-clash query/notice. | Keep `Ranet` and `BdmSupport` methods, reserve selected UDP protocols only for menu actions, propagate DEV9 usability and balance failed references in Files/on-demand/boot paths. Append ministack export slot 7 and ioman devctl; queue one IO-thread notice for the GUI hook. Compile the actual C++ guards/refcount/notice and actual ARP handler in regressions. Newer RA browser/Caduceus absent; legacy Makefile/filter changes inapplicable to this SDK/edition CI. |
| #904: UDPFS operation serialization | Filesystem RPC callers and watchdog discovery shared one transport without a semaphore; teardown left the watchdog alive. | Serialize all operations that touch transport/FD state with the watchdog, stop it before teardown, retain 64-bit seek results and unlocked interrupt completion. Favorites and POPS adaptations are recorded separately in this table. |
| #904/#909: POPS UDPFS launch/config and rollback | UDPFS listed Ember only, lacked a POPSTARTER network driver/config editor and probed unsupported UL files. | Adapt to C++ source classes and host UI services; validate every live USB config before teardown, equip/verify the pinned Alpha pair, preserve configs/recovery backups on failed multi-volume replacement, retain partial-scan retry and avoid UDPFS UL probes. Propagate destination close errors and preserve last-good lists on output allocation failure. Official driver/config/license bytes and launch/rollback failures have host coverage; hardware acceptance remains open. |
| #908: cumulative ACK validation | Filesystem used the sequence helper; IMG still used the ineffective signed comparison after a 12-bit mask. | Both modes use the same sequence helper, including wrap and ambiguous half-ring behavior. |
| #913: canonical transport | The core was identical in both variants, while ACK behavior had already diverged. | Move the unchanged core and updated transport to `udpfs_shared/src`; retain per-variant wrappers, headers, defines, imports, module names and SDK targets. |
| #921: reconnect, stale ACK and discovery ARP | Reconnect retained dead-session counters; Files retried on stale ACKs; discovery did not announce the PS2 address. | Reset TX/RX state on successful discovery; keep waiting on the same alarm after stale ACKs in both send APIs; announce IP/MAC before bound broadcasts, excluding udptty and unicast. |
| #907 and #909: preserve existing VMC saves | A resize request unlinked the old image before asynchronous creation. An open error could start a truncating create against an unreadable image. | Port into `sysvmc.cpp`: explicit Delete is required to replace any existing different-size image, including malformed files. Prove absence with a completed directory scan, reject truncated paths, and propagate create submission errors to the dialog. Existing same-size images remain untouched. The POPS UDPFS config rollback portion of #909 is adapted separately below. |
| #910: image cache construction | The C++ class checked its own allocation but dereferenced failed prefix, suffix or entry allocations and accepted invalid counts. | Keep placement construction and private fields; validate counts and suffix, release all partially allocated fields and destroy the partial C++ object on failure. Compile the constructor with the actual class declaration in the allocation test. |
| #912: menu and art index allocation | Page, row and hint allocations were unchecked. Theme changes could publish a half-allocated ID/UID pair. | Check all allocations, unlock on failed page insertion, and replace both art arrays together. On failure discard the old theme's arrays to prevent indexing them with a new theme size. |
| #914: queue snapshot and append lifetime | `IoQueue::pendingCount()` read its head before taking its semaphore. Failed GUI appends overwrote navigation pointers with NULL. | Lock before reading the C++ queue head; keep tail, cursor, page start and autostart state on failed append. The legacy `submenuRemoveItem` routine is absent from this branch, so its backlink fix is not applicable. |
| #915: BDM private allocation | Failed per-slot allocation could register a page with NULL private data; settings re-apply never retried it. | Allocate at the registration boundary, hide/disable failed slots, retry later, and fail closed in init/cleanup/VMC/prefix callbacks. Retain `BdmSource`, template lookup, host services and identified-page retention. Skip NULL/empty app prefixes. |
| #911: IOP reset packet and failure handling | Unbounded outbound copies, unchecked intercepted lengths and a failed IOP heap allocation could corrupt memory or DMA to zero. The MIPS trampoline forced success. | Validate packet/payload/suffix capacities, reject oversized outbound commands, stop on failed allocation, unwind services and counters, prevent ELF loading after failed reset, and preserve C's return value through the assembly trampoline. Standard and RA lifecycle tests execute the production routines. |
| #906: RA Mode 6 snapshots (partial) | Mode 6 skipped the IGR handler that drove RA snapshots. | Add the RA-only snapshot handler after successful, telemetry-ready reset; failed interrupt registration remains retryable and repeated installation is idempotent. The older RA runtime has no `IGR_RequestReset` client command, so that part requires the broader RA runtime migration. The edition overlay now builds/packages RA as well as standard kits; runtime parity and hardware acceptance remain open. |

SDK adaptations retain `u32` for IOP `WaitEventFlag` outputs and add the thread
imports required by filesystem teardown. The C++ `GameSource` implementation,
protocol selector, network ownership and EE launch flow are retained.

Regression tests compile the production transport through each SDK target's
wrapper, feature defines and include paths. They exercise timeout/retry limits,
stale ACKs, both send APIs, reconnect sequence state, ACK wrap, actual ARP packet
fields and announcement order, plus filesystem operation overlap and error
unlock. The saved pre-port transports and discovery implementation fail these
same regression scenarios. Both GCC and LLVM builds are required.

Real-console acceptance remains open: server unavailable at boot, late server
startup, server restart/settings changes, Files and IMG modes, sustained reads,
Ember handoff, and normal SMB/UDPBD selection. Host tests and SDK builds do not
prove loss tolerance, performance, driver teardown safety on hardware, or full
public feature parity.

## Remaining migration work

The runtime-budget continuation refreshes production to
`c5800e4ff026018c6bd10cca64fc2720a0e9eaad`, including new QA credits and preserved
RE4/VMC/IGR evidence. Those docs/credits and prior diagnostics remain unadapted.
Current branch coordinates 4,096 entries/128 nodes, nine parts, 20 KB transfer,
validated set-sized protected storage and one/two/three-frame cadence through
C++ LaunchServices. See RA-RUNTIME-BUDGETS.md and issue #20 for validation.
Earlier notes to raise these budgets are superseded by this implementation;
newer timing/client/peer/overlay/browser/ORBIT/HTTPS behavior remains missing.

Latest refresh: production `6a3f12f2d22dbbf95bf2a1cf18f914ad6ed66145` includes
merged freeze diagnostics #942 and About/QA layout #944. They remain unadapted.
The current RA transfer batch adapts original `f0943360`'s exact fixed-stride
coverage checks, rejects parser-refused lists before persistence and fixes
binary-separator consumption (original #946). Actual C++ transfer tests and
all 62 Python/HTTP/seven C++ host suites pass; native/CI/package checks are
pending. Full RA/HTTPS/browser/client/ORBIT and peer verification remain open.
The existing 16 KB network-list ceiling is deliberately retained pending a
coordinated runtime/watch/snapshot budget port.

VMC automatic format validation is merged in #38 at `3acd135c`; its final
compiler/host/32-configuration/three-kit checks and all 35 final package checks
pass. Earlier statements below that this is unresolved in C++ are historical.

The latest source audit is pinned to `fb00066fe7c806a415cd019177bcf0227068bf52`,
including original #938/#939/#940 repairs of reported #934/#935/#936.
Merged #932 adds optional CFG list titles; #937 updates the original Pages guide.
Open original #733 (cover art flow, head `35653d79484e70b65024e972352e0718c1a48626`)
and #687 (DO NOT MERGE, head `dba97ff725ac4dbca22c1f69ccb40443e03e696e`)
remain tracked separately from production. Newer RA/browser changes
#923/#926/#927/#929/#930 are still outside the individual ports above.

| Area | Evidence and next boundary |
| --- | --- |
| POPS UDPFS and Favorites storage recovery | Favorites feedback/view/title changes are adapted above. Public #904/#909 POPS UDPFS launch, driver preparation and rollback are adapted in the current branch. The UL follow-up reproduces and fixes a trailing-record heap overwrite, accumulates partial reads and retains cached state on malformed/failed reads; its real-layout regression runs under ASan/UBSan and fails on the pre-fix scanner. Verify final compiler/edition CI and console acceptance; interruption recovery remains separate. PR #31 handles short transfers and close failures. Implement interruption-safe replacement/recovery across supported devices; the upstream in-place writer is not atomic. |
| RA runtime and standard/RA isolation | Public #894/#899/#906/#916/#923 include newer RA runtime/browser/client behavior and ORBIT handoff. Only the snapshot subset of #906 is covered here. Require explicit standard/RA targets, package isolation and launch/resource validation before claiming RA parity. |
| Configuration matrix and release packaging | Three complete ps2build edition kits pass CI. The recovered PADEMU-disabled, DS5/extras, DTL, diagnostic, debug and UART definitions are adapted through the edition builder with isolated configuration stages and release rejection guards. PR #24 passed all 32 GCC/LLVM configuration builds and artifact verification. No-IGR is runtime Mode 6 (`0x20`), whose current hook gates and host reset/snapshot tests agree with production; no separate compile-time flavour was found. DECI2, language/font/debug distribution bundles and publication remain open. Keep three release channels and one SDK. |
| Existing feature PRs | #17/#18/#19/#21/#22/#23/#24 are merged. #21 retained the useful Ember, Neutrino, docs/evidence, splash and updater work from #12–#16; those originals are closed as superseded. Exact CI/artifact refs and local recovery are recorded in the checkpoint. |
| Earlier feature/config parity | #885/#887/#889/#898, automatic VMC and newer Ember missing-core behavior are merged and verified through final integration `39eda4b8`. Remaining UI/font/settings changes require individual body-level comparisons. PR titles or test-count differences alone do not establish parity. |
| Optional CFG list titles (#932) | Adapted to C++ cache and GameSource with default-off setting, title sorting, RA prefix preservation and worker refresh. Merged in PR #32, final compiler/host/32-configuration/three-kit CI and downloaded artifact checks pass at `cd787705`. Pages #33 and Wiki guidance are published. Console acceptance remains open. |
| CJK and game-type labels | CJK fallback (#917) is adapted through FontSystem, including independent fallback face/glyph cache lookup and settings home font loading (`font_CJK.ttf`, `font_Japanese.ttf`). The optional `game_type_labels` setting (#845) is adapted with default-off [PS1]/[PS2] list prefixes in `drawItemsList`, master config persistence and Interface settings toggle. |
| VMC #939/#940 follow-ups | PR #34 releases the abort lock before cluster IO, preserves failed-seek/short-write handling, follows effective loader policy, waits for slow working jobs, bounds cancel cleanup and re-probes created cards. It re-resolves Favorites and validates source/row identity after yielding. Real-source regressions, native LLVM and final compiler/host/configuration/kit checks pass at `01affe4d`. Full-size failed-format retry is a separate shared defect reported as original #941, still unresolved here. |
| Favorites #938/#940 staged recovery | Merged PR #36 at `9a868e82` adapts bounded image reads, complete-primary precedence, native recovery-only restore-before-restaging and exact staged/primary readback through existing C++ storage helpers. IO/unknown-version/oversized stores remain read-only. Original #943 proves the ENOSPC overwrite fallback loses the last complete copy; this port refuses that edit instead. Host/native/final GCC/LLVM, 32 configurations and three kits pass; all 35 final packages verify. Device/power-loss acceptance remains pending. |
| Failed-format VMC retry (original #941) | Current branch adds read-only header/geometry validation through GameSource before automatic reuse and after creation. Full-size erased images and IO failures refuse assignment; valid formatted Neutrino odd-size cards remain eligible. No implicit deletion/reformatting. Actual-source regression fails on the old probe and passes with seek/read/close/overflow faults. Host suites pass; native/CI/package and physical-device acceptance remain pending. |
| IGR pad-buffer lifetime (original #951) | Adapt the open candidate's buffer invalidation before intercepted reset, state-preserving reinstall and interrupt-state-preserving pad publication. This branch retains VBLANK input handling rather than importing the upstream polling worker. Standard/RA actual-function regressions cover stale buffers, libpad layouts, pending state and failed resets. Source-backed defect; target/review/console evidence remains separate, and no RE4 causal claim is made. |
| Coverflow page animation (original #952) | Adapted to actual C++ page methods with changed-selection gating, both wraps and unchanged flat-theme/page targets. Sanitizer regression exercises real methods and fails on the pre-port source. Target/package and console evidence remain separate. |
| Fresh-start guide (original #953) | README/theme claims corrected and DEFAULTS.md added for verified C++ values. Runtime defaults now match the original: 4:3, audio and menu rumble (#1070) off, center scale 45. Saved settings still win. |
| Hook rescan and cache visibility (open original #959) | Keep EE PadOpen installation latched across IOP buffer invalidation and frozen pad frames; callbacks establish installed state, and actual patch writes flush D/I caches. Standard/RA and real-patcher regressions cover the distinction. Upstream polling KSU logic is not directly applicable to this interrupt-driven branch; assembly ABI and Favorites config changes remain separate unported audits. No hardware/RE4 causal claim. |
| Hardware acceptance | Validate boot, USB/MX4SIO/MMCE/ATA discovery, hotplug, device allocation recovery, existing VMC handling, game launch/IGR, Files/IMG reconnect and other network protocols. LLVM floating-point behavior/performance also remains unproven. |

The new regression tests execute the adapted C++ allocation, queue and GUI
paths, plus standard/RA reset and snapshot handler variants. Standard and RA
builds/packages pass; hardware and full RA behavior remain separate gates.
