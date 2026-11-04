# Fork-gap status - November 1, 2026

This refresh re-reads every record of the [October 29 register](../2026-10-29-refresh/FULL-REFRESH.md) against the code as it stands today, rechecks every tracked upstream head, and names the wins taken since. It supersedes the availability conclusions of the October 29 refresh; the October 29 document stays as dated evidence and its 301 records keep their IDs. Differences are comparison opportunities with shared work credited; they are not promised ports or a defect count.

## Implementation checkpoint - November 2 evening, 2026 (November 3 UTC)

This checkpoint supersedes implementation-status claims in the dated snapshots below. Default source heads checked: RiptOPL `22c319f72c046a583917fb73852f7de1d359bd8d`; legacy Open-PS2-Loader `dfa28822d324ae23b653a2a7585815eaac0cb671`. A pushed draft is implemented branch work, not merged source, a published release or console acceptance. This documentation/site update itself remains a draft until merged and published.

| Work | Source and review state | Acceptance limits |
| --- | --- | --- |
| YA-21 / YA-22 / YA-25 GUI locks | Merged: [RiptOPL #264](https://github.com/NathanNeurotic/RiptOPL/pull/264) / [legacy #1225](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1225). PRESENT in source. | Language-swap WIP branches remain separate, unmerged and outside this implementation approval. |
| DT6 IOP memory relief | Merged: [RiptOPL #270](https://github.com/NathanNeurotic/RiptOPL/pull/270) / [legacy #1231](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1231). PRESENT in source. | Builds and host checks do not establish console/game acceptance. |
| RI54 List Title / Description | Feature merged in #276/#1235. Follow-up [#283](https://github.com/NathanNeurotic/RiptOPL/pull/283) / [#1242](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1242) remains draft: production-reader coverage and a stale-pointer guard, not a second feature implementation. Refreshed host fixture passes 288 draw cases in each edition. | Console acceptance remains separate. |
| RI55 pad profiles | [legacy #1243](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1243) is an implemented, held draft. RiptOPL has preserved WIP only. [RIPPS2 v1.2.9-beta](https://github.com/akilluminati47/RIPPS2/releases/tag/v1.2.9-beta), build 129, was published November 1; the old wait-for-b129 condition is obsolete. | Current-base conflicts and controller/console acceptance remain. No extra pad behavior was ported here. |
| Counter #287 | [RiptOPL #288](https://github.com/NathanNeurotic/RiptOPL/pull/288) / [legacy #1247](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1247) fix an allocation-failure buffer defect. The reported Beta-4045 manifests already contain #271's refresh fix. | Host simulated counts and allocation failures pass; actual first-boot discovery, theme, return lifecycle and reporter retest remain unproven. |
| Console i.LINK ID | [RiptOPL #290](https://github.com/NathanNeurotic/RiptOPL/pull/290) / [legacy #1248](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1248): implemented native-engine drafts, focused host checks and target builds passed. Optional per-game `$ILinkID`: 16 hex digits, uppercase on save; empty/zero uses hardware. Neutrino/RA external runtimes are unchanged. | Console-bound content and gameplay NOT TESTED. This is not an established Tales of Rebirth fix. |
| Tales source candidates | [#268](https://github.com/NathanNeurotic/RiptOPL/pull/268)/[#1229](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1229) and [#262](https://github.com/NathanNeurotic/RiptOPL/pull/262)/[#1222](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1222) retain their holds after mechanical base refreshes. Stream hardening is separate from the Abyss freeze. | #250 is a held test-build package, not a merge candidate. Rebirth #285/#1246 lacks an exact failing build, core, backend, image identity and useful good/bad comparison. |
| ShanWan identity | [#244](https://github.com/NathanNeurotic/RiptOPL/pull/244)/[#1206](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1206) refreshed; C/header token comparison confirms comments only. | No remapping or controller acceptance claim. |
| Launchstation Move | [Launchstation #270](https://github.com/NathanNeurotic/Launchstation/pull/270), implemented draft at `8bb2915`: bounded 256 KiB Move-only manifest; uncertain directory completeness retains the source. Focused failure fixtures and pinned LLVM build passed. | No filesystem transaction/rollback claim and no console acceptance. Launchstation's earlier paste WIP has the same complete tree as master; no recovery patch is needed. |
| Volventura startup | New [#289](https://github.com/NathanNeurotic/RiptOPL/issues/289) records reporter success with the special 4017 DODGE-RAM ELF from MASS0/MMCE0, with/without USB. The downloaded artifact's zero-filled hole covers the reported bad-read window. | Address-sensitive failure remains under investigation. Physical RAM failure versus boot-chain/modchip interference is unresolved; this does not validate #259's different alignment experiment or current nightly gameplay. HDD paste versus launch failure is ambiguous. |

Release evidence: the inspected Beta-4045 rolling manifests identify source `22c319f`. This establishes their source identity, not that these draft changes have been published. Historical register totals below remain dated; no full 311-record re-audit is claimed by this focused checkpoint.

## Update - November 2, 2026

Two corrections to the November 1 snapshot, the ports opened since, and a second upstream recheck. The sections after this one keep their November 1 values; the two corrections are also made where they apply.

**Corrections.**

- **DT6 was counted twice.** The October 29 register already carries it (KH2 SMB+PADEMU memory-footprint correction, from [OPL-DS4RTBT](https://github.com/MiguelQueiroz010/OPL-DS4RTBT), the downstream fork by [Miguel Queiroz (MiguelQueiroz010)](https://github.com/MiguelQueiroz010)); the November 1 refresh added the same change again as a new record when it found it upstream in [OPL-DualSense-BT](https://github.com/rodrigoGalvanDev/OPL-DualSense-BT) [8bb581a](https://github.com/rodrigoGalvanDev/OPL-DualSense-BT/commit/8bb581ada8cfb7a08f96f6e82091b5a43f1125a4). The duplicate is gone: November 1 had 310 records (301 carried + 9 new) and 113 MISSING, not 311 and 114.
- **[YAOPL](https://github.com/yaopl/yaopl) was not fully taken.** A third lock fix by [Roman Kelesidis (belomaxorka)](https://github.com/belomaxorka), [b45a592d](https://github.com/yaopl/yaopl/commit/b45a592d), was still open: a theme or language that the IO thread loads was published, and the old one freed, without the GUI lock. It is new record **YA-25**, ported in the same drafts as YA-21 and YA-22, together with one theme switch and one language switch at a time.

**Ports opened since November 1.** All are draft tandem PRs, NOT TESTED on hardware:

| Record | November 2 status | Where |
| --- | --- | --- |
| YA-21, YA-22, YA-25 (new): dialogs, IO-thread list operations, and theme and language swaps under the GUI lock | PARTIAL | [RiptOPL #264](https://github.com/NathanNeurotic/RiptOPL/pull/264) / [legacy #1225](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1225) |
| DT6 in-game IOP memory relief, about 26 KB: IP fragmentation off on transmit, reassembly kept with a 1536-byte buffer, USBD pools sized for PADEMU when the game is not on USB, transfer descriptors left at their default; two old reassembly bugs in `ip_frag.c` fixed on the way | PARTIAL | [RiptOPL #270](https://github.com/NathanNeurotic/RiptOPL/pull/270) / [legacy #1231](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1231) |
| RI54 List Title / Description: Settings > Interface, Theme / On / Off, default Theme (today's look) | PARTIAL | [RiptOPL #276](https://github.com/NathanNeurotic/RiptOPL/pull/276) / [legacy #1235](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1235) |
| RI55 .pad controller profiles and Set Up A USB Pad | MISSING; port started, legacy first | not yet open |

**Upstream recheck.** On November 2 at UTC 14:16, `git ls-remote` of the 11 tracked repositories that carry open records ([RIPPS2](https://github.com/akilluminati47/RIPPS2), YAOPL as [yaopl/yaopl](https://github.com/yaopl/yaopl) and [belomaxorka/yaopl](https://github.com/belomaxorka/yaopl), [LUNA](https://github.com/dnunezx/LUNA), [OPL-DualSense-BT](https://github.com/rodrigoGalvanDev/OPL-DualSense-BT), [OPL-DS4RTBT](https://github.com/MiguelQueiroz010/OPL-DS4RTBT), [DKWDRV](https://github.com/DKWDRV/DKWDRV), [OPL-JUMP](https://github.com/juankyt505/OPL-JUMP), [BDMAssault](https://github.com/Okeanos86/BDMAssault), [neutrino](https://github.com/rickgaiser/neutrino) and upstream [Open-PS2-Loader](https://github.com/ps2homebrew/Open-PS2-Loader)) and 2 untracked ones; the result is [`recheck-2026-11-02.json`](https://nathanneurotic.github.io/RiptOPL/audit/2026-11-01/recheck-2026-11-02.json).

- [RIPPS2](https://github.com/akilluminati47/RIPPS2) ([akilluminati47](https://github.com/akilluminati47)) is the only tracked default-branch head that moved; other branch and tag changes are listed separately. v1.2.9-beta (build 129) came out on November 1, after the November 1 recheck, so RI55 is released work now. main is at f9d33c2 with build 130 in testing (long videos open, VIDEO.TXT, PS1 names read from the game's drive, START rows, memory card copies and listing). The listed tests do not cover pad behavior, so pad impact remains unassessed; it will be compared once it is released.
- [LUNA](https://github.com/dnunezx/LUNA) ([dnunezx](https://github.com/dnunezx)) gained a build-3.1.3-slovak branch, a translation build.
- [ELFWD](https://github.com/Okeanos86/ELFWD) by [Okeanos86](https://github.com/Okeanos86) (v2.0.0) is not in the register: a generic ELF forwarder that can disguise argv[0], reset the IOP and load IRX modules, and apply RAM patches from ELFWD.CFG. A companion tool; no record. [sd2cloud](https://github.com/oMrRexD/sd2cloud), the other untracked one, has not moved since it was last looked at (main 0216488).

| Status | November 1 (corrected) | November 2 (311: 301 carried + 10 new) |
| --- | --- | --- |
| PRESENT | 72 | 72 |
| PARTIAL | 37 | 40 |
| MISSING | 113 | 111 |
| REGRESSED | 0 | 0 |
| INTENTIONAL DIFFERENCE | 44 | 44 |
| BLOCKED | 4 | 4 |
| UNKNOWN | 40 | 40 |

Each record's November 2 status and note are the `nov2_status` and `nov2_note` fields of [`id-status.json`](https://nathanneurotic.github.io/RiptOPL/audit/2026-11-01/id-status.json). Of the November 1 list of what is worth a look, DT6 and RI54 are now drafts and RI55 is being ported; **RI38 Recent Games capacity** leads what is left.

## Baselines

| Edition | Source baseline read | Notes |
| --- | --- | --- |
| RiptOPL C++/ps2build | [rebuild/main 96852359](https://github.com/NathanNeurotic/RiptOPL/commit/96852359da353a5de5a3244c2fdc611c7b3bd30a) | Boot Theme Wait (#256) and start-up timing (#254) merged the same day |
| Open-PS2-Loader legacy C | [rebuild/main b2d6b3b1](https://github.com/NathanNeurotic/Open-PS2-Loader/commit/b2d6b3b1) | tandem of the above (#1218, #1216) |

For the merged work listed at the November 1 baseline, **NOT TESTED** means no console acceptance was recorded. Draft entries in the later checkpoint and update remain unmerged even when marked **NOT TESTED**; source presence is not acceptance.

## Status vocabulary

Every record in the November 2 snapshot carries one of seven statuses: **PRESENT**, **PARTIAL**, **MISSING**, **REGRESSED**, **INTENTIONAL DIFFERENCE**, **BLOCKED**, **UNKNOWN**. The October 29 classification buckets map onto them as follows; the per-record result, with the records re-read by hand called out, is in [`id-status.json`](https://nathanneurotic.github.io/RiptOPL/audit/2026-11-01/id-status.json).

| October 29 bucket | November 1 status |
| --- | --- |
| Already present / equivalent scope; Merged / hardware unverified | PRESENT (merged records stay NOT TESTED) |
| Partial overlap / remaining difference | PARTIAL |
| Prior source finding / reassess; Source comparison candidate | MISSING |
| Companion / separate scope; Declined; Historical / withdrawn; Intentional design difference | INTENTIONAL DIFFERENCE |
| Known upstream limitation | BLOCKED |
| Experimental source / unvalidated | UNKNOWN |

| Status | Records (310: 301 carried + 9 new; corrected November 2) |
| --- | --- |
| PRESENT | 72 |
| PARTIAL | 37 |
| MISSING | 113 |
| REGRESSED | 0 |
| INTENTIONAL DIFFERENCE | 44 |
| BLOCKED | 4 |
| UNKNOWN | 40 |

No regression was found: nothing that the October 29 register recorded as present is absent at today's baselines.

## What closed since October 29

All of the work the October 30 publication note listed as pending is merged in both editions: the list-rebuild follow-up, Coverflow variants (#199 / #1167), Grid (#196 / #1164), silent in-game pause (#200 / #1168), official-config import (#190 / #1159) and menu-freeze diagnostics (#191 / #1160). On top of those:

| Record | Status | Where |
| --- | --- | --- |
| YA-20 shared message wrapping | PRESENT | RiptOPL #215 / legacy #1177 |
| YA-24 (new) PNG decode hardening | PRESENT | #245 / #1207, from [YAOPL](https://github.com/yaopl/yaopl) |
| YA-23 (new) delete/rename/launch act on the row the dialog was opened for | PRESENT | #251 / #1213, from YAOPL 5211110 |
| RI52 (new) game size via lseek64 when stat reports 0 on MMCE | PRESENT | #255 / #1217, from [RIPPS2](https://github.com/akilluminati47/RIPPS2) bd7615f |
| RI53 (new) PS1 Duplicates setting | PRESENT | #255 / #1217, from RIPPS2 a53f936 |
| RI1 / B1 / LU14 file manager and PSU export/import | PRESENT | #219 / #1180 |
| RI3 / RI12 in-game menu, RA pages, power off | PRESENT | #184 / #1155, #200 / #1168 |
| NV2 (new) Neutrino SMB launch path | PRESENT | #234 / #1196, #243 / #1205 |
| YA-06 DualShock launch handoff | INTENTIONAL DIFFERENCE | analog-lock follow-up #246 / #1208 |
| S1 ATA DRDY wait | INTENTIONAL DIFFERENCE | ours is bounded (30 s BSY + 5 s DRDY); Stable OPL waits without bound |

Every one of these is NOT TESTED on hardware.

## Wins taken in this refresh

Two YAOPL fixes by [Roman Kelesidis (belomaxorka)](https://github.com/belomaxorka) were still open and are now draft tandem PRs [RiptOPL #264](https://github.com/NathanNeurotic/RiptOPL/pull/264) / [legacy #1225](https://github.com/NathanNeurotic/Open-PS2-Loader/pull/1225) (records YA-21, YA-22, PARTIAL until merged):

- **Dialogs draw under the GUI lock** ([ce6323f](https://github.com/yaopl/yaopl/commit/ce6323f)). The IO thread frees a replaced theme or language under `guiLock`; the settings dialogs, keyboard, number pad and colour picker drew without it, so a dialog open while a device with the configured theme or language came up could draw freed strings, icons and font slots.
- **IO-thread list operations under the GUI lock** ([4c713a3](https://github.com/yaopl/yaopl/commit/4c713a3)). `updateMenuFromGameList` and `menuDeferredUpdate` drained the queued row operations without the lock while the GUI thread drew the same rows.

A host test now checks both invariants on the source in CI. NOT TESTED on hardware.

## Upstream heads rechecked

All **141 tracked identities** were rechecked with `git ls-remote` on November 1 (UTC 17:00): **20 tracked heads moved, 25 identities gained or changed refs, and 66 other identities only lost refs outside `refs/heads/` and `refs/tags/` (for example, Based-Skid/iLaunchELF); none were unavailable**. The full result is [`repository-heads.json`](https://nathanneurotic.github.io/RiptOPL/audit/2026-11-01/repository-heads.json). What moved that matters:

| Project | Head | Assessment |
| --- | --- | --- |
| [RIPPS2](https://github.com/akilluminati47/RIPPS2) ([akilluminati47](https://github.com/akilluminati47)) | main c0d30866; v1.2.8-beta released (b126-b128); branch b129 | b126's card-Neutrino icon and 75 s launch wait are RIPPS2-specific. **RI54 List Title / Description** (released) and **RI55 .pad controller profiles / Set Up A USB Pad** (b129, unreleased) are MISSING and are product calls, not oversights. |
| [YAOPL](https://github.com/yaopl/yaopl) ([belomaxorka](https://github.com/belomaxorka)) | master 6955d0be; branches fix/stale-current, debug/ds3-metrics | PNG hardening and the row race are in; the two lock fixes are the drafts above. A third, b45a592d, was missed here: see the November 2 update. |
| [LUNA](https://github.com/dnunezx/LUNA) ([dnunezx](https://github.com/dnunezx)) | main ab52117; hotfix/3.1.3-mmce-gameid | Wrapper-side MMCE GameID hotfix; not applicable to this frontend. Register LU records unchanged. |
| [OPL-DualSense-BT](https://github.com/rodrigoGalvanDev/OPL-DualSense-BT) ([rodrigoGalvanDev](https://github.com/rodrigoGalvanDev)) | 7188a53, tag 1.2 | 8bb581a adds DS4 clones, RTL8761B, multi-pad, a Controller Setup page and multitap. **DT6** (an October 29 record; this is its code, counted again as new until the November 2 correction): in-game IOP memory relief for KH2 over SMB (lwIP IP_REASSEMBLY/IP_FRAG off, about 7.3 KB; USBD pool trim when only PADEMU loads, about 28 KB). MISSING; turning IP fragmentation off changes behaviour on large-MTU SMB paths, so it needs a decision before any port. |
| [DKWDRV](https://github.com/DKWDRV/DKWDRV) | 1.7.6v | Documentation and release only. |
| [OPL-JUMP](https://github.com/juankyt505/OPL-JUMP) | 5e103b9 | Content unchanged; JUMP1 filename filter stays PARTIAL (product call). |
| [OrbitPS2-Manager](https://github.com/Luden02/OrbitPS2-Manager) | moved | Desktop companion; moved-head assessment UNKNOWN because this checkpoint does not document inspection. Prior OM1 / OM2 desktop migration/batch-ZSO gaps are not reclassified here. |
| [neutrino](https://github.com/rickgaiser/neutrino) | 1bff25b | The SMB work is already taken (NV2). |
| ps2homebrew/Open-PS2-Loader | unchanged | No new upstream master commits to compare. |

Our own repositories (RiptOPL, Open-PS2-Loader, Launchstation, launcHER, POPSLoader, PS2-Servers, PS2SP) account for seven of the twenty moved heads.

## Still MISSING and worth a look

In rough order of value; none is claimed by another thread:

1. **RI38 Recent Games capacity**: ours holds 10 entries, RIPPS2 holds 99. PARTIAL, small.
2. **DT6 in-game IOP memory relief** (above): needs the MTU decision first.
3. **RI54 List Title / Description** and **RI55 .pad profiles**: RIPPS2 feature work, product calls.
4. **RI10 / RI37** persistent library snapshot with Trust Cache: large; stays a comparison.
5. **O8 VRAM texture budget clamp, BL2 bounded artwork prefetch, LU37 / LU41 budgeted and direction-aware cover loading**: artwork pipeline comparisons, medium.
6. **RI25 / 27 / 29 / 34 / 39 / 40 / 41 / 42 / 43 / 45 / 46, NV1, H14**: unchanged source comparison candidates from October 29.

Work owned by other threads and deliberately left alone here: SMB2 restore (#257 / #258, #1219 / #1220), MMCE launch regression, the Tales of the Abyss freeze (#262 / #1222), GSM 1080p 4:3 (#260 / #1221) and the bug-report build (#261 / #1223).

## Method and limits

Per-record verdicts come from reading the current source of both editions, not from PR titles. The November 1 head recheck uses the October 29 heads file as its baseline. The November 2 `git ls-remote --heads --tags` recheck from the cloud container compares against the November 1 `repository-heads.json` snapshot; its moved-head result is relative to that snapshot. A moved head was inspected only where this document says so. Rolling release assets can change without a tag; release metadata was not re-pulled. No console code ran.

Evidence set: [FORK-GAP-STATUS-2026-11-01.md](https://nathanneurotic.github.io/RiptOPL/audit/2026-11-01/FORK-GAP-STATUS-2026-11-01.md) · [id-status.json](https://nathanneurotic.github.io/RiptOPL/audit/2026-11-01/id-status.json) · [repository-heads.json](https://nathanneurotic.github.io/RiptOPL/audit/2026-11-01/repository-heads.json) · [recheck-2026-11-02.json](https://nathanneurotic.github.io/RiptOPL/audit/2026-11-01/recheck-2026-11-02.json). The searchable register stays at [fork-gaps.html#at-a-glance](https://nathanneurotic.github.io/RiptOPL/fork-gaps.html#at-a-glance).
