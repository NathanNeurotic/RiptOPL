# Claude audit: RiptOPL

Status: IN PROGRESS. 2 of 154 reading passes are in; finding IDs are provisional and will be renumbered until this line says COMPLETE.

- Repository: NathanNeurotic/RiptOPL
- Audited branch: rebuild/main
- Audited commit: b00e773bf28c1790b458ab53f25caad69e79f237
- Audit date: 2026-11-16
- Mode: read-only review. This branch adds only the `Claude-audit/` folder; no source, build or workflow file is changed.

All `path:line` references are pinned to the audited commit above. Each finding was raised by a reader that read the cited files line by line, then re-checked by a second, independent pass that re-read the cited lines and re-ran the reference searches. Findings that did not survive that check were dropped.

## Totals

- Findings in this report: 10. 11 more raised findings are waiting for their second check and are not shown yet.
- Raised by readers: 21. Dropped as wrong or unsupported on re-check: 0. Corrected on re-check: 5. Merged as duplicates: 0.

| Category | Count |
|---|---:|
| 1 Dead code | 3 |
| 2 Duplication | 0 |
| 3 Overengineering | 4 |
| 4 Size and memory | 0 |
| 5 Performance | 0 |
| 6 Bugs and security | 0 |
| 7 Build/CI/repo clutter | 3 |
| 8 Half-finished or redundant features | 0 |

| Benefit rank | Meaning | Count |
|---|---|---:|
| 5 | major: real crash/data-loss/security bug, or >= 1000 lines / >= 50 KB / >= 100 KB RAM / multi-second freeze | 0 |
| 4 | large: >= 300 lines / >= 10 KB / >= 32 KB RAM, clear reliability or responsiveness gain | 0 |
| 3 | moderate: >= 80 lines / >= 2 KB, minor bug, redundant CI job or card write | 0 |
| 2 | small: >= 15 lines, a few hundred bytes | 3 |
| 1 | trivial: a few lines, cosmetic | 7 |

## Findings (sorted by benefit)

### Benefit rank 2

#### RIPTOPL-001: BgmPlayer: single-instance class reached only through two thread trampolines and five one-line forwarding wrappers

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/bgmplayer.h:1-50`; `RiptOPL/src/core/bgm.cpp:56-66`; `RiptOPL/src/core/bgm.cpp:903-928`
- Problem: BgmPlayer has exactly one instance, the file-static gBgm (bgm.cpp:56), and no code outside bgm.cpp names the class (include/bgmplayer.h is included only by bgm.cpp:14). Every outside use goes through the C-style sound.h functions, which only forward (bgm.cpp:905-928), and the thread bodies need two static trampolines (bgm.cpp:58-66) because they are member functions. The 768 KB ring already had to stay outside the class (bgm.cpp:72), and the running flags (bgm.cpp:73) are plain globals shared with sound.cpp through soundint.h:12.
- Fix: Make the BgmPlayer members file-static variables in bgm.cpp and the member functions static functions, name the public ones directly bgmStart/bgmStop/bgmQuiesce/isBgmPlaying/bgmDiscretionaryIoAllowed, pass the thread functions to CreateThread directly, and delete include/bgmplayer.h.
- Benefit: ~55 lines removed (50-line header replaced by ~15 static declarations, 9 trampoline lines, ~20 forwarding lines). No ELF change expected beyond a few bytes of call indirection (INFERRED).
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-002: cheatman parser: parse_line wrapper re-tests is_cheat_code, make_code has a redundant zero branch, term_str callback has one caller

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/cheatman.cpp:149-158`; `RiptOPL/src/core/cheatman.cpp:184-208`; `RiptOPL/src/core/cheatman.cpp:224-240`; `RiptOPL/src/core/cheatman.cpp:341-355`
- Problem: parse_buf already calls is_cheat_code (:346) and enters the parser branch for code lines and empty lines; parse_line (:187-200) runs is_cheat_code again only to return a zero code for the empty-line case, plus a per-line LOG. make_code pre-zeroes address/value and then has a failure branch that zeroes again (:149-153). term_str (:227-240) is a generic callback walker with a NULL check whose only caller passes is_cmt_str, which re-runs strlen at every character.
- Fix: In parse_buf, call make_code directly when is_cheat_code(line) is true and skip the line otherwise; delete parse_line. In make_code delete the sscanf failure branch: its only caller passes lines holding exactly CODE_DIGITS hex digits, so both fields always parse. Replace term_str+is_cmt_str with one inline scan that cuts the line at the first '#' or '//'.
- Benefit: ~30 lines removed; a few hundred bytes of .text at most (INFERRED; static functions, likely inlined already).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-003: OSDHistory: unused date getters, no-op DEBUG_PRINTF calls, commented-out loop and three stray includes of OSDHistory.h

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/OSDHistory.h:83-85`; `RiptOPL/src/core/OSDHistory.cpp:33`; `RiptOPL/src/core/OSDHistory.cpp:134`; `RiptOPL/src/core/OSDHistory.cpp:139`; `RiptOPL/src/core/OSDHistory.cpp:145-149`; `RiptOPL/src/core/OSDHistory.cpp:155-162`; `RiptOPL/src/core/OSDHistory.cpp:217-223`; `RiptOPL/src/core/OSDHistory.cpp:45-58`; `RiptOPL/src/platform/sysvmc.cpp:41`; `RiptOPL/src/platform/sysneutrino.cpp:42`; `RiptOPL/src/platform/sysdisc.cpp:43`
- Problem: osdHistoryGetYear/Month/Date have no use anywhere. DEBUG_PRINTF is defined empty at :33 and never redefined, so its 7 calls compile to nothing. :217-223 is a commented-out loop from the original. The three switch arms at :45-58 all set the same size. sysvmc.cpp, sysneutrino.cpp and sysdisc.cpp include OSDHistory.h but use nothing from it (only system.cpp:1350,1378 calls AddHistoryRecordUsingFullPath).
- Fix: Delete the three getters, the DEBUG_PRINTF define and its calls, and the commented-out loop; set size once after the switch (or pick only the icon pointer in it); drop the OSDHistory.h include from sysvmc.cpp, sysneutrino.cpp and sysdisc.cpp.
- Benefit: ~25 lines removed; no ELF change (constexpr getters and empty macros generate no code) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

### Benefit rank 1

#### RIPTOPL-004: OSDHistory.h carries a 79-line block of disassembly notes from the original OSDSYS

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/OSDHistory.h:3-81`
- Problem: Lines 3-73 are reverse-engineering notes (hex dumps of record bytes and a pseudo-C transcription of OSDSYS 1.00 code at fixed addresses) that the ported implementation in OSDHistory.cpp:126-264 already embodies; only the 'Conclusion' field layout at :74-80 documents the struct below it. The block is upstream-derived (INFERRED: same notes as upstream OPL).
- Fix: Keep the 6-line field layout (:74-80) next to struct HistoryEntry and delete the rest of the comment.
- Benefit: ~72 comment lines removed; no binary effect.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-005: load_cheats: read_text_file's maxsize option is never used and the parse tail duplicates load_cheats_buf

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/cheatman.cpp:387-393`; `RiptOPL/src/core/cheatman.cpp:411-415`; `RiptOPL/src/core/cheatman.cpp:467-507`
- Problem: read_text_file's only caller passes maxsize 0 (:476), so the size-limit branch (:411-415) is dead. load_cheats (:467-489) repeats load_cheats_buf's ensureCheatTable/parse_buf/mode-return sequence (:495-507).
- Fix: Drop the maxsize parameter and its branch. In load_cheats_buf call ensureCheatTable before the NULL test, so a NULL buffer still clears the table; then make load_cheats read the file, call load_cheats_buf(buf), free the buffer and return that result. Do not simply read first and clear later: a failed read must still leave an empty table, or set_cheats_list (host.cpp:156) can flatten the previous game's cheats on a launch that continues past the error.
- Benefit: ~12 lines removed; no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-006: bgm.cpp: stale section banner, orphaned member comment and two unused includes

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/core/bgm.cpp:8`; `RiptOPL/src/core/bgm.cpp:19`; `RiptOPL/src/core/bgm.cpp:21-22`; `RiptOPL/src/core/bgm.cpp:116-120`
- Problem: The 'Theme Sound Effects' banner (:21-22) heads nothing: sound effects moved to sfx.cpp. :116-120 describes bytesPerMs_ and roomTarget_, which now live in include/bgmplayer.h:46-47, and sits above an unrelated function. Nothing in bgm.cpp calls a pad.h function, and the only timer-like call, SetAlarm, is a kernel.h syscall that opl.h:5 already brings in (INFERRED for timer.h).
- Fix: Delete the banner, move or delete the orphaned comment, and drop the timer.h and pad.h includes.
- Benefit: ~9 lines removed; no binary effect.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-007: cheatman.h: CHEAT_VERSION, CR and SPACE are never used

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/cheatman.h:37`; `RiptOPL/include/cheatman.h:48-49`
- Problem: No C/C++ source uses CHEAT_VERSION, CR or SPACE from this header; NUL and LF (:46-47) are the only character constants the cheat code uses. CHEAT_VERSION's only other mention is a text anchor in .github/scripts/test_cheat_capacity.py:24.
- Fix: Delete the three definitions and move the test's slice anchor to the next line (for example 'constexpr int MAX_HOOKS').
- Benefit: 3 lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-008: parse_buf computes each cheat's Select-mode default that cheatConfigLoadSelections always recomputes

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/cheatman.cpp:27`; `RiptOPL/src/core/cheatman.cpp:367-369`; `RiptOPL/src/core/cheatconfig.cpp:71-73`
- Problem: parse_buf sets enabled = (gCheatMode == 0) || cheatConfigIsMasterCode(name) for every cheat. In Select mode (load returns 1) both load paths call sbCheatsSelect (supportbase.cpp:1785,1840), whose cheatConfigLoadSelections first resets every entry to cheatConfigIsMasterCode(name) (cheatconfig.cpp:72-73). The parser's master-code test is therefore overwritten every time, and it is the only reason cheatman.cpp includes cheatconfig.h.
- Fix: Set enabled = 1 in parse_buf (Auto mode runs every cheat; Select mode is reset by cheatConfigLoadSelections), drop the cheatconfig.h include from cheatman.cpp, and drop or rewrite the text check in .github/scripts/test_neutrino_cheats.py:335 that requires the old line.
- Benefit: ~3 lines and one header dependency removed; saves one name normalisation (one 129-byte stack buffer and a few strcmp) per parsed cheat (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-009: Dead setting key CONFIG_OPL_ENABLE_ANALOG_NAV and its dialog id UICFG_ENABLE_ANALOG_NAV

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/config.h:114`; `RiptOPL/include/dialogs.h:27`
- Problem: The Left Stick Navigation option was removed (dialogs.cpp:2263-2268 says it is never coming back), but its config key and its UI id remain. Neither name, nor the key string enable_analog_nav, is used anywhere else.
- Fix: Delete both lines.
- Benefit: 2 lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-010: config.h comments point to files that no longer exist (src/libview.c, opl.c, src/config.c)

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/config.h:125`; `RiptOPL/include/config.h:168`; `RiptOPL/include/config.h:311`
- Problem: The comments name libViewStoreToConfig in src/libview.c, readConfigPathRedirect() in opl.c and src/config.c; the code now lives in src/ui/libview.cpp:351, src/app/settingsio.cpp:296 and src/core/config.cpp, and none of the named .c files exists.
- Fix: Correct the three paths.
- Benefit: Cosmetic; 0 lines removed.
- Effort: S. Risk: Low. Confidence: High (READ)

## Stale branches and tags (list only)

Listed for information; nothing here is a recommendation to delete without checking. Snapshot of the remote at audit time: 290 branches besides the default, 5 tags. "Open PR" marks a branch that is the head of an open pull request.

### Branches fully merged into rebuild/main (67)

| Branch | Last commit | Note |
|---|---|---|
| `claude/project-thread-6vq8sh` | 2026-10-27 |  |
| `claude/project-thread-8mgzui` | 2026-10-27 |  |
| `claude/project-thread-ng8s1m` | 2026-10-27 |  |
| `claude/project-thread-rxkpyt` | 2026-10-27 |  |
| `claude/project-thread-uu434q` | 2026-10-27 |  |
| `claude/project-thread-7os0js` | 2026-10-28 |  |
| `claude/project-thread-8p1nns` | 2026-10-28 |  |
| `claude/project-thread-8p1nns-lba48` | 2026-10-28 |  |
| `claude/project-thread-8p1nns-ripps2-docs` | 2026-10-28 |  |
| `claude/project-thread-8p1nns-views-docs` | 2026-10-28 |  |
| `claude/project-thread-8p1nns-ya2` | 2026-10-28 |  |
| `claude/project-thread-djicky` | 2026-10-28 |  |
| `claude/project-thread-j4ngtl` | 2026-10-28 |  |
| `claude/project-thread-jbitz4` | 2026-10-28 |  |
| `claude/project-thread-rupufe` | 2026-10-28 |  |
| `claude/project-thread-skxuai` | 2026-10-28 |  |
| `claude/project-thread-skxuai-grid` | 2026-10-28 |  |
| `claude/project-thread-skxuai-views` | 2026-10-28 |  |
| `claude/project-thread-vrn490` | 2026-10-28 |  |
| `claude/project-thread-xtzetq` | 2026-10-28 |  |
| `claude/merge-reviewed-batch` | 2026-10-29 |  |
| `claude/position-per-mode` | 2026-10-29 |  |
| `claude/project-thread-559h02` | 2026-10-29 |  |
| `claude/project-thread-lh5rt2` | 2026-10-29 |  |
| `claude/project-thread-nkjtgl` | 2026-10-29 |  |
| `claude/project-thread-qefkwr` | 2026-10-29 |  |
| `claude/project-thread-s5yoo8` | 2026-10-29 |  |
| `claude/ra-xerabora-1.0.1` | 2026-10-29 |  |
| `claude/review-followups` | 2026-10-29 |  |
| `claude/smap-tx-base` | 2026-10-29 |  |
| `claude/smb-read-closed` | 2026-10-29 |  |
| `claude/udpbd-read-only` | 2026-10-29 |  |
| `claude/udpfs-files-docs` | 2026-10-29 |  |
| `claude/ya02-iso9660-records` | 2026-10-29 |  |
| `claude/ya03-zso-validation` | 2026-10-29 |  |
| `claude/ya06-ds2-handoff` | 2026-10-29 |  |
| `claude/ya07-ds34usb-transfers` | 2026-10-29 |  |
| `claude/ya09-mmce-silent-switch` | 2026-10-29 |  |
| `claude/ya12-bdm-index-dots` | 2026-10-29 |  |
| `claude/ya14-mmce-igr-strings` | 2026-10-29 |  |
| `claude/ya17-usb-host-stop` | 2026-10-29 |  |
| `codex/ya01-history-filename-bounds` | 2026-10-29 |  |
| `codex/ya04-font-terminator` | 2026-10-29 |  |
| `codex/ya05-cheat-capacity` | 2026-10-29 |  |
| `codex/ya15-disc-key-semaphore` | 2026-10-29 |  |
| `codex/ya16-vmc-fragment-diagnostics` | 2026-10-29 |  |
| `claude/project-thread-h361ue` | 2026-10-30 |  |
| `claude/project-thread-h361ue-allgames-hide` | 2026-10-30 |  |
| `claude/project-thread-h361ue-library` | 2026-10-30 |  |
| `claude/project-thread-h361ue-views` | 2026-10-30 |  |
| `codex/fork-gaps-refresh-20261029-docs` | 2026-10-30 |  |
| `claude/project-thread-sanilb` | 2026-10-31 |  |
| `claude/project-thread-jiysgr-listcfg2` | 2026-11-03 |  |
| `codex/ci-action-warnings-20261103` | 2026-11-03 |  |
| `codex/controller-report-clarification` | 2026-11-03 |  |
| `codex/counter-allocation-safety` | 2026-11-03 |  |
| `codex/counter-review` | 2026-11-03 |  |
| `codex/docs-edition-captures` | 2026-11-03 |  |
| `codex/docs-ui-captures` | 2026-11-03 |  |
| `codex/mmce-ember-scan-retry` | 2026-11-03 |  |
| `docs/fix-app-handoff-path-scope` | 2026-11-03 |  |
| `feature/riptopllvm-release` | 2026-11-03 |  |
| `claude/project-thread-4dvtik` | 2026-11-04 |  |
| `codex/console-ilink-id` | 2026-11-04 |  |
| `fix/release-upload-retry-502-20261104` | 2026-11-04 |  |
| `fix/saved-theme-render-queue` | 2026-11-04 |  |
| `investigate/saved-theme-boot-freeze-20261103` | 2026-11-04 |  |

### Unmerged branches with no commit in the 30 days before the audit (3)

| Branch | Last commit | Ahead | Behind | Note |
|---|---|---:|---:|---|
| `codex/docs-audit-site` | 2026-10-01 | 187 | 4282 |  |
| `ps2build` | 2026-10-01 | 70 | 868 |  |
| `test/korium-audit-remediation` | 2026-10-12 | 134 | 775 |  |

### Tags (5)

| Tag | Date |
|---|---|
| `rolling-incebtion` | 2026-11-16 |
| `rolling-korium` | 2026-11-16 |
| `rolling-lite` | 2026-11-16 |
| `rolling-llvm` | 2026-11-16 |
| `rolling-ra` | 2026-11-16 |

## Files not read line by line (and why)

Vendored or generated content, reviewed structurally:

- language string tables: 36 files, 24210 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.
- lwIP-derived SMSTCPIP network stack (upstream OPL/SMS): 57 files, 16720 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.
- vendored FatFs (ChaN) sources: 7 files, 24099 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.
- vendored mbedTLS library: 280 files, 205284 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.

Not read as text:

- 365 files, 71.0 MB: binary file (covered by the asset/clutter inventory, not read as text). Mostly under `gfx` (142), `ship` (114), `docs` (71), `modules` (15), `audio` (9), `third_party` (9), `misc` (5).
- 6 files, 0.0 MB: empty file. Mostly under `ship` (6).

Not read yet (reading still in progress): 1630 files in 83 chunks.
