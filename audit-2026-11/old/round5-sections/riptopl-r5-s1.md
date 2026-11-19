# RiptOPL — Round 5, Section R5-S1 (EE src/, second tier)

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` · Read-only audit · Every assigned file read 1..EOF.
Cross-checked against RIPTOPL-001…144 (rounds 1–4 reports + round-4 section files) before filing.

## Coverage ledger

| File | Lines read |
|---|---|
| src/ui/guigame.cpp | 2617 |
| src/sources/cuesupport.cpp | 1288 |
| src/core/bgm.cpp | 928 |
| src/ui/guifilemgr.cpp | 682 |
| src/core/lang.cpp | 556 |
| src/platform/hdd.cpp | 469 |
| src/gfx/coverflow.cpp | 367 |
| src/core/OSDHistory.cpp | 298 |
| src/gfx/plasma.cpp | 260 |
| src/sources/rahunt.cpp | 213 |
| src/sources/vmcrebuild.cpp | 200 |
| src/platform/ps2cnf.cpp | 120 |
| src/gfx/uidraw.cpp | 86 |
| src/core/sound.cpp | 72 |
| **Total** | **8156** |

## New candidate findings

### R5-S1-01 / Dead dialog read-back block at the end of the Compatibility dialog
- **Category:** 1 (dead code)
- **Locations:** src/ui/guigame.cpp:1818-1823, src/ui/guigame.cpp:1832-1835 (tail of `guiGameShowCompatConfig`, function at :1722)
- **Problem:** After the dialog closes, the function recomputes `compatMode` from the mode checkboxes and reads `dmaMode`, `coreLoader`, `hexid` and `altStartup` out of diaCompatConfig into the file statics. None of those writes is ever consumed: the only consumers are `guiGameSaveConfig`/`guiGameTestSettings`, and both go through `guiGameApplyConfig` (src/ui/guigame.cpp:1852), which recomputes `compatMode` from the same dialog (:1859-1865) and re-reads DMA (:1876), Loader (:1881), GameID and AltStartup from the dialog before using any of them. `guiGameLoadConfig` (:2540) likewise reloads `coreLoader` from the config set. The only live statement in the block is the COMPAT_LOADFROMDISC branch (:1825-1830), which must stay.
- **Fix:** Delete the compat-mode loop and the four diaGet* read-backs; keep the `if (result)` LOADFROMDISC branch.
- **Benefit:** ~11 dead lines removed; one less place that falsely implies the dialog's values are captured at close time rather than at save/test time.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — all readers/writers of the five statics enumerated by grep in this file; the statics are file-local)

### R5-S1-02 / "Remove settings" leaves the Neutrino and Ember per-game keys behind
- **Category:** 6 (bug)
- **Locations:** src/ui/guigame.cpp:2121-2170 (`guiGameRemoveSettings`); the keys it misses are written at src/ui/guigame.cpp:2014-2050 (`guiGameApplyConfig`) and src/ui/guisettings.cpp:3396ff (Ember keys, writer evidence)
- **Problem:** `guiGameRemoveSettings` removes ~30 per-game keys (config source, DMA, core loader, compat, i.Link ID, DNAS, alt startup, parental lock, all GSM/Cheats/OSD/PadEmu keys, VMC names and disables) — but never removes `CONFIG_ITEM_NEUTRINO_ARGS`, `CONFIG_ITEM_NEUTRINO_VIDEO`, `CONFIG_ITEM_NEUTRINO_GSMCOMP` or `CONFIG_ITEM_NEUTRINO_BSDFS`, all four of which `guiGameApplyConfig` in the same file writes per-game, nor the four per-game Ember keys (`CONFIG_ITEM_EMBER_TIMING/DITHER/SHADING/CONTROLLER`, read back by `cueSaveGameSettings` in src/sources/cuesupport.cpp). A user who resets a game's settings to defaults still launches it with the old Neutrino args/video overrides (and old Ember settings) silently in force.
- **Fix:** Add the eight `configRemoveKey` calls to `guiGameRemoveSettings`, alongside the existing removals.
- **Justification (fix adds calls):** the removal list is the function's whole job; the eight keys were added to the save path after the remove path was written and never mirrored — this restores the documented "remove settings" contract rather than adding a feature.
- **Benefit:** Reset actually resets; removes a class of "my defaults won't take" reports against Neutrino/Ember games.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — full removal list and full ApplyConfig write list compared line by line)

### R5-S1-03 / System-data icon.sys is written 1776 bytes from a 964-byte icon array
- **Category:** 6 (bug/security — out-of-bounds read)
- **Locations:** src/core/OSDHistory.cpp:27 (`SONY_SYSDATA_ICON_SYS_SIZE = 1776`), src/core/OSDHistory.cpp:40-59 (`CreateSystemDataFolder`, write at :59); source arrays built from misc/icon_A.sys, misc/icon_J.sys, misc/icon_C.sys (964 bytes each, READ via file size at the pin)
- **Problem:** All three region icons are 964-byte files, but the folder creator writes 1776 bytes from the array — 812 bytes past its end, copying whatever `.rodata` follows it into the memory card's icon.sys. The in-code comment acknowledges writing "past the end of the icon" (the 1776 size is what HDDOSD demands), but the padding content is uninitialised-for-this-purpose adjacent data, not zeros, and the read itself is UB that a different link order can change silently.
- **Fix:** Write the real 964 bytes, then write the remaining 812 bytes from a small zero buffer, keeping the on-card file exactly 1776 bytes.
- **Justification (fix adds a pad write):** it replaces an 812-byte out-of-bounds read with defined content while preserving the deliberate 1776-byte file size the OSD requires — net behaviour on the card is identical-or-cleaner.
- **Benefit:** Removes an OOB read whose payload varies with link layout; icon.sys padding becomes deterministic zeros.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — constant, write site, and all three source file sizes verified)

### R5-S1-04 / History eviction compares against a stale timestamp and can evict the wrong record
- **Category:** 6 (bug)
- **Locations:** src/core/OSDHistory.cpp:168-186 (`AddHistoryRecord` least-used scan; victim consumed at :246)
- **Problem:** When an entry with a strictly lower `LaunchCount` is found, `LeastUsedRecord`/`LeastUsedRecordLaunchCount` are updated but `LeastUsedRecordTimestamp` is not reset to that entry's `DateStamp`. The timestamp still holds the minimum stamp of the *previous* (higher-count) candidate set, so a later entry tied on the new low count is only chosen if its stamp beats a stamp from a different count class. Worked example (READ from the code): counts/stamps (5,100), (3,500), (3,200) leave the victim as the (3,500) record instead of the least-recent (3,200).
- **Fix:** When the launch-count branch takes a new minimum, also set `LeastUsedRecordTimestamp = HistoryEntries[i].DateStamp`.
- **Benefit:** Eviction actually picks the least-recently-launched among the least-launched, as the two-key comparison intends; the archived history.old record is the intended one.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — control flow traced; impact is victim choice only, no memory-safety effect)

### R5-S1-05 / SYSTEM.CNF boot token of ≥64 printable chars is returned unterminated
- **Category:** 6 (bug/security — disc-supplied data)
- **Locations:** src/platform/ps2cnf.cpp:42-68 (`CNFGetKey`); cap `CNF_PATH_LEN_MAX 64` at include/ps2cnf.h:1; sole caller's buffer `char ps2disc_boot[CNF_PATH_LEN_MAX]` at src/sources/supportbase.cpp:116
- **Problem:** `CNFGetKey` copies graph characters until a space, NUL, or `i == CNF_PATH_LEN_MAX`. In the cap case it returns without ever writing a `'\0'`: a SYSTEM.CNF whose BOOT2 value is 64+ printable characters fills the caller's 64-byte stack buffer completely, and the caller then consumes it as a C string, reading past the buffer. Real disc boot paths are far shorter, so this needs a crafted/corrupt CNF — but SYSTEM.CNF is exactly the untrusted-input surface this parser exists for.
- **Fix:** Stop the copy at `CNF_PATH_LEN_MAX - 1` so the existing space/NUL branch (or a final explicit terminator after the loop) always lands inside the buffer.
- **Justification (fix changes a bound):** one comparison; no new code paths, and no legitimate boot filename approaches 63 chars.
- **Benefit:** Closes an unterminated-string read on disc-controlled data at the parser's only call site.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — loop, cap and caller buffer size all verified)

### R5-S1-06 / Two near-identical icon+caption draw functions in uidraw.cpp
- **Category:** 2 (duplication)
- **Locations:** src/gfx/uidraw.cpp:12-34 (`guiDrawIconAndText`), src/gfx/uidraw.cpp:36-59 (`guiDrawIconAndLiteralText`)
- **Problem:** The two functions are line-for-line the same body (texture fetch, width from aspect, draw, x advance, string render); the only difference is the last argument: `_l(textId)` vs a caller-supplied `const char *`. Every fix to icon metrics or the no-texture fallback must be made twice (the in-file comment at :69 already treats the pair as one behaviour).
- **Fix:** Keep one body taking `const char *`; make `guiDrawIconAndText` a thin wrapper that resolves `_l(textId)` and forwards. All existing call sites (dia.cpp, themeelems.cpp, guimodal.cpp) keep their signatures.
- **Benefit:** ~20 lines removed; one implementation of the hint/icon layout math.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — bodies diffed visually; both call sets enumerated by grep)

### R5-S1-07 / Plasma texture allocation is never checked, but draw() writes through it unconditionally
- **Category:** 6 (bug — unchecked allocation)
- **Locations:** src/gfx/plasma.cpp:177 (`init()` memalign), src/gfx/plasma.cpp:235-255 (`draw()` writes `tex_.Mem + kWidth * row_` and passes `&tex_` to rmDrawPixmap); init/draw are driven unconditionally from src/ui/gui.cpp:85,146
- **Problem:** `init()` stores `memalign(128, 4096)` into `tex_.Mem` with no NULL test — unlike `shutdown()` in the same file, which does test it. If the 4 KB allocation ever fails (heap exhausted late in a session), every subsequent frame writes 32×6 RGBA pixels through a NULL base and hands the texture to the renderer, instead of simply having no animated background.
- **Fix:** Guard in `draw()` (return when `tex_.Mem == NULL`), matching the guard `shutdown()` already has.
- **Justification (fix adds a check):** one comparison on a per-frame path, converting a guaranteed NULL-write crash into a missing background.
- **Benefit:** Allocation failure degrades to a static menu instead of a crash loop.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ for the missing check and unconditional use; INFERRED that memalign can fail in practice — 4 KB, so this is a hardening item, not a live bug report)

### R5-S1-08 / lngGetFilePath indexes languages[langID-1] with no range check
- **Category:** 6 (bug — latent OOB index)
- **Locations:** src/core/lang.cpp:550-556 (`lngGetFilePath`); sole caller src/ui/guinotify.cpp:135 passes `lngGetGuiValue()`
- **Problem:** `lngGetFilePath(0)` — the value `lngGetGuiValue()` returns whenever English is active — reads `languages[-1]`. Today the only call site sits behind the `showLang_` popup flag, which is only raised after a file language was applied, so the bad index is not reached in practice (INFERRED from the popup's purpose; the flag's setters are outside this section's files). The function is a public API in include/lang.h with no stated precondition, one new caller away from a heap under-read whose garbage pointer is then printed by snprintf `%s`.
- **Fix:** Return NULL (or an empty string) for `langID < 1 || langID > nLanguages`, mirroring the bounds discipline `lngSetGuiValue` already applies to the same id space at src/core/lang.cpp:485.
- **Justification (fix adds a check):** one comparison at a cold UI path; makes the API safe for its documented id domain (0 = English = no file).
- **Benefit:** Removes a latent under-read in a public accessor.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ for the missing check and the id semantics; INFERRED that the current sole caller cannot pass 0)

### R5-S1-09 / ps1DropTwins recomputes the Ember rows' twin keys once per VCD row
- **Category:** 5 (performance)
- **Locations:** src/sources/cuesupport.cpp:1145-1175 (`ps1DropTwins`; inner call at :1158)
- **Problem:** The twin match is a full VCD×CUE cross product. `ps1TwinKey(rows[j].name)` — a strlen plus prefix/disc-ID scan — is recomputed for every (i, j) pair, i.e. up to `vcdCount` times per CUE row, although a row's key never changes during the pass. At the list caps (both halves can reach four figures) that is millions of redundant key derivations on a refresh path, on top of the strcasecmp the algorithm actually needs.
- **Fix:** Derive each row's twin key once into a scratch pointer array before the cross product and index it in both loops; free it with the existing `drop` array.
- **Justification (fix adds a scratch array):** it converts the key-derivation work from O(n·m) to O(n+m); the array is one pointer per row, freed on the same path as `drop`.
- **Benefit:** Refresh-time twin filtering stops re-deriving identical keys; structural saving grows with library size (magnitude INFERRED — no hardware timing available).
- **Effort:** S · **Risk:** Low · **Confidence:** High for the redundant computation (READ); Low for user-visible magnitude (INFERRED)

## Already filed — not repeated

- **RIPTOPL-017** — the five `sprintf` sites in src/core/OSDHistory.cpp (:41, :76, :91, :106, :153) into 64/32-byte buffers. (R5-S1-03/-04 above are different defects in the same file.)
- **RIPTOPL-029** — plasma background computed on the CPU every frame (src/gfx/plasma.cpp as a whole). R5-S1-07 is only the unchecked allocation inside it.
- **RIPTOPL-013** — the `.bak` copy retained by the VMC rebuild flow (src/sources/vmcrebuild.cpp) — deliberate safety copy, already filed with a surface-and-offer-delete fix; the rest of vmcrebuild.cpp re-read clean this round.
- **RIPTOPL-104** — `ConfigSet::clone` drops the on-disk format; its sole-caller containment note names the test-launch path in src/ui/guigame.cpp (`guiGameTestSettings`, clone at ~:2197 at the round-4 pin). The clone usage itself is not re-filed.

## Checked and clean (dissolved candidates)

- **cuesupport.cpp disc-list/settings machinery** — `cueReadDiscList` bounds bytes (8192), line length and entry count and never publishes a partial list; `cueRewriteSettingsFile` restores the original on write failure; `cueScanDir` grows by realloc instead of reserving CUE_MAX_ITEMS and preserves the last-good list on any failure. No finding.
- **cuesupport `saveIconPs1Directory` strlen(root) before the name checks** — dissolved: every caller (bdmsupport.cpp:2395, ethsupport.cpp:1656, hddsupport.cpp:3342-3353) passes a string literal or an owned device prefix, never NULL.
- **bgm.cpp 768 KB ring buffer (192×4096)** — deliberate, commented headroom for device/list bursts (#364 in-code); not waste by the audit's standard. The reopen-on-dead-descriptor source, the top-bit-serial Ogg fix and the audsrv room polling are all documented workarounds with bounded retries; no defect found.
- **hdd.cpp `hddGetFileBlockInfo` `number_data < max` test** — looks like an off-by-one, but include/hddsupport.h:65-67 documents the bound as intentional ("number_data must stay below it", 11 = inode segment + 10 blocks). Dissolved.
- **hdd.cpp APA/HDL enumeration** — partial directory reads now propagate as errors instead of publishing partial lists; the POPS/Ember partition walk is one shared enumerator with a name-test parameter (dedupe already done). No finding.
- **coverflow.cpp** — padded-window draw, lookahead warming and painter's-order rendering are each budget-capped and commented with the hardware report they answer; the animating-only padding draw keeps rest-state cost unchanged. No finding.
- **plasma `smoothStep` table index** — `noise()` only passes fractional parts in [0,1), and the public `ease()` caller's two ratios (guiscreens.cpp:70,76) are bounded to ≤ 1.0, i.e. index ≤ 256 = the last valid entry. Dissolved.
- **guigame.cpp `strncpy(vmc, VMCName, sizeof(vmc))` at :514** — source and destination are both 32-byte name buffers and the source is filled either by configGetVMC or by diaGetString with the same size; a non-terminated 32-byte source could not be proven from the assigned files. Dissolved (noted, not filed).
- **guigame.cpp PadEmu bitfield packing** (`guiGamePadEmuUpdater`) — convoluted OR-then-mask construction, but each field's set/clear pair was traced and agrees with `guiGameLoadPadEmuConfig`'s decode shifts. Ugly, not wrong; no finding.
- **guifilemgr.cpp** — clipboard/paste/delete flows check write-enable per operation, never replace an existing name on paste, keep un-moved cut entries on the clipboard after a partial failure, and the four modal allocations are all freed on the single exit path (including the OOM path). The `fmDirSlash(parent, …, strcpy(target, parent))` construction at :446 is an in-place normalisation via a scratch buffer — odd, but the buffers are distinct and sized alike. No finding.
- **lang.cpp language-switch publication** — the double-free/use-after-free shape the comments describe is genuinely handled: the old table is freed under the GUI lock with the *old* `guiLangID`/`nValidEntries`, and `lngLoadFromFile` completes short files with internal-English pointers that the `nValidEntries` bound correctly excludes from freeing. Format-string argument checking (`lngFormatArgs`/`lngFormatFits`) is sound. No finding beyond R5-S1-08.
- **rahunt.cpp** — field copies are capped by the parser (`o < RA_HUNT_DESC-1`, explicit NUL, title pre-truncated at RA_HUNT_TITLE-1); `huntLines` accounts the "..." suffix and the NUL in its capacity test. No finding.
- **vmcrebuild.cpp** — sibling-name length arithmetic fits exactly (len < 256, 4-char extension + NUL), existence is proven by directory enumeration rather than open errors, and the copy is only ever dropped while the original card is verified in place. No finding beyond the already-filed .bak item.
- **sound.cpp** — 72 lines; volume fan-out and the BGM-stop condition in `audioEnd` (playing OR either thread flag) match the leak case the comment describes. No finding.
- **ps2cnf.cpp `CNFGetToken` whitespace loop dereferences before its end check** — dissolved at the only caller: `cnf_end` points at the caller-written `'\0'` inside the 1024-byte stack buffer (size is clamped to 1023), so the dereference stays in bounds. R5-S1-05 is the live defect in this file.

## Notes

- No forced revision to any RIPTOPL-001…144 finding: nothing read this round contradicts a prior finding; RIPTOPL-017/029/013/104 remain accurate at this pin.
- No PS2 build or hardware verification exists in this VM; all performance magnitude statements are structural/INFERRED as marked.
