# RiptOPL — Round 5, Section R5-S6 (EE src/, batch 6)

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` · Read-only audit · No build or hardware claims anywhere; performance magnitudes are structural / INFERRED.

## Coverage ledger

| File | Lines | Read |
|---|---|---|
| src/core/vmc_groups.cpp | 1,474 | 1..EOF |
| src/gfx/textures.cpp | 1,438 | 1..EOF |
| src/sources/ranet.cpp | 1,136 | 1..EOF |
| src/gfx/artload.cpp | 904 | 1..EOF |
| src/core/filemgr.cpp | 636 | 1..EOF (full read this round; round 2 read only the #417 delta + `fmMcRename`) |
| src/ui/menuitems.cpp | 529 | 1..EOF |
| src/gfx/textshape.cpp | 452 | 1..EOF |
| src/core/settings.cpp | 325 | 1..EOF |
| src/sources/ratls_ps2.cpp | 304 | 1..EOF |
| src/platform/httpcatalog.cpp | 265 | 1..EOF |
| src/sources/rabadge.cpp | 220 | 1..EOF |
| src/gfx/atlas.cpp | 178 | 1..EOF |
| src/sources/folderbrowse.cpp | 130 | 1..EOF |
| src/ui/guiscreens.cpp | 109 | 1..EOF |
| src/ui/guiboot.cpp | 53 | 1..EOF |
| **Total** | **8,153** | **15 files** |

Cross-checks outside the assignment (supporting evidence only): `include/folderbrowseint.h` (sub_ sizing), `include/gui.h` (screen constants), `src/ui/guigame.cpp:493,529,702` (vmc_groups callers), `src/ui/menusys.cpp:1075,3118` (`achReturnScreen` values), `src/sources/achievements.cpp:434,522` and `src/ui/selfupdate.cpp:151` (ratls/ranet callers), `src/app/settingsio.cpp:1571,1588-1598,2223` (settings VMODE flow), `src/platform/ioman.cpp:397` (`ioGetPending`), `src/ui/guioverlay.cpp:194,213` (textures diagnostic consumer), `src/gfx/fntsys.cpp:534,567` (Atlas use).

## New candidate findings

### R5-S6-01 / VMC group table: two groups can never be returned, 8 title IDs can never match, 5 IDs are duplicated inside their own group
- **Category:** 6 (bugs — data) / 1 (dead data)
- **Locations:** `src/core/vmc_groups.cpp:1452-1473` (`getGroupIdForTitleId`, first-match linear scan over the table at `:1352`); identical arrays `titles_XEBP_100_71` (`:1143-1146`) and `titles_XEBP_100_72` (`:1149-1152`); `titles_XEBP_100_53` (`:929`) vs `titles_XEBP_000_12` (`:1271`); non-canonical IDs at `:443-444`, `:464-465`, `:472-473`, `:498`, `:614`; intra-group duplicates at `:665,668`, `:675,676`, `:1028,1029`, `:1053,1057`, `:1222,1226`
- **Problem:** Script-verified at the pin: 97 groups, 1,046 title IDs. (a) `titles_XEBP_100_72` is byte-identical to `titles_XEBP_100_71` (same 3 IDs), and `titles_XEBP_000_12`'s 4 IDs are exactly `titles_XEBP_100_53`'s 4 IDs. The lookup returns the FIRST match and the 100-series precedes the 000-series in the table, so group `XEBP_100.72` and group `XEBP_000.12` can never be returned — games belonging to those groups get the wrong shared-VMC name (callers at `src/ui/guigame.cpp:529,702` build the default VMC filename from the returned group). (b) 8 entries are not in the canonical `XXXX_###.##` form every other entry and every caller-supplied startup string uses — `"SLUS_20754"`, `"SLUS_20824"`, `"SCES_50422"`, `"SLES_50422"`, `"SCES_51154"`, `"SLES_51154"` (missing `.`), `"SLES-5350"` (`-` for `_`, and short), `"SSCES_540.95"` (doubled S) — so they can never `strcmp`-match and are dead. (c) 5 IDs appear twice within a single group (`SLES_546.24`, `SLUS_216.62`, `SLPS_731.03`, `SLPS_251.69`, `SLES_820.21`); the second copy is unreachable. Whether 100.72 / 000.12 are wrong data or wrong duplicates of their twins cannot be settled from this repo — one of each pair is wrong either way (INFERRED which).
- **Fix:** Verify the two shadowed groups and the 8 malformed IDs against the upstream VMC-group source data, correct the entries that have a real distinct value, and delete the ones that do not; delete the 5 intra-group second copies. (Correction, not addition — no justification needed.)
- **Benefit:** Two VMC groups become reachable (correct shared VMC names for their games); ~13 dead strings removed; the table becomes auditable by a duplicate/format scan.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — arrays, table order, first-match scan, and counts all verified at the pin; only the correct replacement values are external data)

### R5-S6-02 / vmc_groups: 97 hand-written `count_*` variables are the boilerplate that already produced one out-of-bounds walk
- **Category:** 2 / 3
- **Locations:** `src/core/vmc_groups.cpp` — one `static const size_t count_XEBP_*` per array throughout `:15-1350` (97 of them), consumed by the table at `:1352-1449`; the failure mode is documented in the file itself at `:1152,1158,1164` ("was sizeof(titles_XEBP_100_71): copy-paste numerator overstated the count and walked past the array")
- **Problem:** Every group pays three hand-maintained names (array, count variable, table row) that must agree. The in-file comments record that this exact pattern already shipped a wrong-numerator count that walked past an array end, in at least four groups. The count variables carry no information — each is `sizeof(array)/sizeof(array[0])` of the array directly above it.
- **Fix:** Build the table rows with a macro (or an inline helper) that takes the array name and computes its count at the table site, and delete all 97 `count_*` variables. The group-ID strings stay as they are.
- **Benefit:** ~97 lines deleted; the wrong-count class of bug becomes impossible rather than comment-documented.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-S6-03 / filemgr: `fmRemove` applies the memory-card 4-try retry (with 50 ms pauses) to every device
- **Category:** 5
- **Locations:** `src/core/filemgr.cpp:468-477` (`fmRemove`); contrast the card-gated retries in the same file — `fmMkdirDev` breaks out immediately for non-card paths (`:396-408`), the open retries in `fmCopyFile` are gated on `fmIsMc` (`:352,361`)
- **Problem:** The file header scopes the retry rule to cards ("the first access after a card is detected answers 'changed card'"), and every other retry in the file is gated on `fmIsMc()`. `fmRemove` is not: on mass/SMB/HDD a genuinely failing `remove()`/`rmdir()` runs 4 attempts plus a 50 ms `fmPause()` after each try — including after the last one — and `fmDeleteTree` (`:479-498`) calls it once per tree entry, so a failing recursive delete pays ~150 ms of pure pause per entry before reporting `FM_ERR_DELETE`. INFERRED magnitude (structural: 3–4 pauses × 50 ms × entries).
- **Fix:** Gate the loop the same way `fmMkdirDev` does: one attempt for non-card paths, the 4-try loop only for `mc0:`/`mc1:`, and no pause after the final attempt.
- **Benefit:** Failed deletes on non-card devices return after one attempt instead of four plus ~150–200 ms; card behaviour unchanged.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — the asymmetry against the file's own other helpers is in the text)

### R5-S6-04 / filemgr: `fmChildren` re-reads the whole directory from scratch at every capacity growth
- **Category:** 5
- **Locations:** `src/core/filemgr.cpp:225-248` (`fmChildren`); `fmList` at `:199-223`; per-entry `stat()` in `fmIsDir` at `:174-190`
- **Problem:** `fmChildren` starts at cap 64 and, whenever a directory fills the buffer, `free()`s the list and re-runs `fmList` from entry zero at 4× capacity (64 → 256 → 1024 → 4096 …). `fmList` itself calls `fmIsDir` — a `stat()` per entry — for every entry it returns. A directory of N > 64 entries is therefore listed and stat'ed ~2× over (≈620 stat calls for 300 entries, ≈10,440 for 5,000, vs N in a grow-in-place design), on the devices where a stat is an RPC (USB/MC/SMB). `fmChildren` backs every copy, delete and size walk (`fmCopyTree`, `fmDeleteTree`, `fmTreeWalk`), so large folders pay this on each operation.
- **Fix:** Grow the buffer in place (`realloc`) and resume filling from the count already read instead of restarting the listing; if the device `readdir` cannot resume into a grown buffer portably, keep the restart but start from a larger first capacity so ordinary folders never restart. Deletion of the redundant passes, not new machinery.
- **Benefit:** Roughly halves the directory-listing + stat work for every folder over 64 entries in copy/delete/size operations (INFERRED, structural).
- **Effort:** M · **Risk:** Med (readdir-resume semantics vary by device driver) · **Confidence:** High for the repeated work (READ); Med for the resume variant being portable

### R5-S6-05 / ranet: the "RAA1 OK" reply-header parse is implemented twice, with a third partial re-parse beside one copy
- **Category:** 2
- **Locations:** `src/sources/ranet.cpp:612-665` (`Ranet::askPc`: 5-number `sscanf` with 3-number fallback, title trailing-space trim, then a separate `sscanf(rx_ + 8, "%d %d", &total, &chunks)` re-parse at `:649`) and `src/sources/ranet.cpp:1051-1068` (`Ranet::xeraboraIdentify`: the same 5-vs-3-number parse and title trim again)
- **Problem:** The protocol's two reply shapes (newer 5-number, older 3-number, title may itself start with digits) are decoded independently in two functions, and `askPc` additionally re-parses fields it already extracted because the two extractions were never unified. Any protocol-shape fix has to be made (and can be missed) in two places; the copies have already drifted in what they do with the result (`askPc` validates totals via `raExpectedChunkLength`, `xeraboraIdentify` does not look at bytes/chunks at all).
- **Fix:** One static parse helper returning bytes/chunks/counts/title-offset (or failure), called by both; delete the second and third `sscanf` passes.
- **Benefit:** ~30 lines removed; one place owns the reply-shape rules.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-S6-06 / ratls_ps2: `raHttpsGet` and `raHttpsDownload` duplicate the whole DNS → socket → connect → deadline preamble
- **Category:** 2
- **Locations:** `src/sources/ratls_ps2.cpp:205-252` (`raHttpsGet`) and `:257-304` (`raHttpsDownload`) — identical clock check, NIC-reserved check, `gethostbyname` handling, `sockaddr_in` fill, socket creation, connect failure path, and post-connect deadline check (~30 lines each), differing only in the anchor arrays and the final `raTlsRequest` / `raTlsDownload` call
- **Problem:** Two copies of the connection preamble in one 304-line file, gated by different `#ifdef`s (RETROACHIEVEMENTS / RIPTOPL_UPDATER), so a fix to the shared part (e.g. the deadline or address handling) can land in one build flavour and not the other.
- **Fix:** One static helper that takes the start time and returns a connected socket (or the existing RATLS_ERR_* code), called by both entry points; the per-caller anchors and request call stay.
- **Benefit:** ~25 lines removed; one preamble to fix.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-S6-07 / textures: staged-open diagnostic latch for an already-superseded hypothesis is permanent production code
- **Category:** 8 (half-finished diagnostic)
- **Locations:** `src/gfx/textures.cpp:512-585` (the OE counter comment + `gTexStagedOpenNonEnoent` at `:526`, the "PHASE TIMING" comment + `gTexLastOpenMs`/`gTexLastMissOpenMs` at `:547-548`, the "WORST-CASE LATCH" comment + `gTexWorstOpen*` statics + `texDebugWorstOpen` at `:566-585`) and the per-open sampling at `:931-945`; sole consumer `src/ui/guioverlay.cpp:194,213` (debug HUD)
- **Problem:** ~70 lines of comment and 7 globals exist to answer one hardware question (are slow staged opens contention or device cost?). The file's own OE comment records that the first hypothesis this instrumentation served was already reframed by the fix in `texStagedOpenIsAbsence`, and the latch comment says its predecessor fields "cannot answer the question they were added for". Every staged art open in every build pays two `ioGetPending()` calls, a `clock()` pair and the latch bookkeeping (cheap individually — `ioGetPending` is a plain queue count, `src/platform/ioman.cpp:397` — but pure overhead once the question is answered or abandoned), and the question can only be closed by a hardware measurement, not by more code.
- **Fix:** Once the maintainer takes the hardware reading the latch was built for, delete the latch, the OE counter and the phase-timing globals together with their HUD fields; until then this is a decision to record, not code to add. If the reading never happens, the deletion is the answer.
- **Benefit:** ~80 lines (comments, globals, sampling, HUD plumbing) removed; the staged-open hot path returns to open-and-check-errno only.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ for the code and its sole consumer; whether the diagnostic is still wanted is the maintainer's call)

## Already filed — not repeated

- **RIPTOPL-028** (round 1): the HTTP game-source stack as a removal candidate, which already covers `src/platform/httpcatalog.cpp` (265 lines) as part of that stack. The parser itself was read in full this round and is otherwise clean (see below); no new parser finding is filed.

## Checked and clean (candidates that dissolved on full reading)

- **httpcatalog.cpp parser** — RFC4180 split with quote handling, control-char / path-traversal / extension checks in `httpCheckPath`, field-count and length caps all enforced; the derived-path arm also passes through `httpCheckPath`, so a title containing `..` or `/` segments is rejected. No overflow or unchecked-length path found.
- **folderbrowse.cpp `descend` `strcpy`** — dissolved: `joined` is `FOLDER_SUB_MAX` and `sub_[mode]` is `char[MODE_COUNT][FOLDER_SUB_MAX]` (`include/folderbrowseint.h:31`), and the copy is gated on `len < sizeof(sub_[mode])`. Root resets, dirty flags and mode gating all consistent.
- **guiscreens.cpp unchecked `screenHandlers[screen]` index** — dissolved: all callers pass `GUI_SCREEN_*` constants, and the one variable caller (`guiSwitchScreen(achReturnScreen)`, `src/ui/menusys.cpp:1211`) only ever holds `GUI_SCREEN_GAME_MENU` / `GUI_SCREEN_APP_MENU` (`:1075,3118`); `GUI_SCREEN_ACHIEVEMENTS` exists only under the same `#ifdef` that adds its handler row.
- **settings.cpp VMODE double-write suspicion** — dissolved: the table row is `loadPlain=false` so `settingsLoad` skips it; the `configSetInt` calls in `src/app/settingsio.cpp:1588,1596` write into the in-memory config set during the boot recovery combo (before the manual `configGetInt` at `:1598`), and `settingsSave` (called once, `settingsio.cpp:2223`) is the single file-save writer of the key.
- **textures.cpp unbounded memory reader (`texReadMemFunction`)** — dissolved: it serves only embedded internal PNGs via `internalDefault[texId].texture`; all user-supplied buffers (staged files, .tar entries) go through the bounded `texReadMemBoundedFunction`. The staged-file fast path and the JPG path check sizes against `TEX_STAGE_MAX_SIZE` / `texSizeValidate` before allocation; the JPG double-stage for a JPG misnamed `.png` is a deliberate documented fallback and only affects misnamed files.
- **ratls_ps2.cpp `bioRecv` static bounce buffer** — no re-entrancy found: a TLS session's recv callback is never nested inside another session, and both entry points run one session at a time on their calling worker (INFERRED from the call sites in `achievements.cpp` / `selfupdate.cpp`). The non-RA duplicates of `nicReserved`/`tcpRecv` in this file are deliberate: `ranet.cpp` compiles to nothing without RETROACHIEVEMENTS, and the file header says so.
- **ranet.cpp `caduceusPage` 40-attempt loop / `ask` drain loop** — dissolved: a silent peer breaks the loop on the first failed `ask` (≤0), WAIT replies are fast, and the drain loop is non-blocking `recvfrom` that terminates when the socket empties.
- **ranet.cpp `httpGet` `*length` without a NULL check** — dissolved: the sole caller (`src/sources/achievements.cpp:434`) always passes `&length`; same for `raHttpsGet` (`achievements.cpp:522`).
- **artload.cpp `artTarFindPiece` `alias + 4`** — dissolved: the alias is built from an `"ART/…"` stem by `texArtAlias`, so stripping exactly the 4-char prefix is correct, and `hasAlias` is false when the stem `snprintf` truncated.
- **filemgr.cpp `fmChildren` 65,536-entry cap** — the silent-truncation tail (n == cap at 65,536 accepted as complete) is real but unreachable in practice: 65,536 `fm_entry_t` (≈128-byte name + fields) is ~9 MB of EE RAM, so the `malloc` fails first and returns the −1 memory error. Not filed.
- **filemgr.cpp remainder** (beyond round 2's `fmMcRename` revalidation) — `fmCopyFile` closes the create race with `O_EXCL`, verifies copied size by re-open, and removes short copies; `fmTarget`/`fmUnder` refuse copy-into-self; `fmParent`'s `strcat` is covered by its `n + 2 > size` guard. No new substance.
- **menuitems.cpp** — the launch path's identity re-checks (startup string copied to the stack, re-validated after the GameID hold, config ownership on every early return) and the favourite-kind/core routing all hold up; handler entry points receive `curMenu` from the menu table in `src/app/opl.cpp:441-444`, never NULL.
- **guiboot.cpp** — the sticky-pointer publish is a single aligned pointer to static/owned storage as documented; `setStickyCopy`'s double-buffer target selection is correct for both the external-string and copy-buffer cases.
- **atlas.cpp** — `place()` reserves width+1 × height+1 before `copyData`, so the copy cannot leave the surface; `create()` bails on either allocation failure and `~Atlas` tolerates both NULLs. `placeIn` recursion depth grows with the split tree but each split strictly consumes area (font-glyph use, `src/gfx/fntsys.cpp:567`).
- **textshape.cpp** — the Arabic presentation-form table is generated data; its gaps correspond to codepoints without Unicode presentation forms, and tatweel (U+0640) is handled specially in `txtJoinsNext`/`txtJoinsPrev`. `txtShape`'s static buffers are used only from `fntsys.cpp` text rendering. No defect substantiated.
- **rabadge.cpp** — `watchListExists` guards NULL/empty serial and bounds both `snprintf` paths at 256; `refresh` frees and rebuilds the slot cache in the documented order (clear before the badges-off early return); VCD key derivation re-checks the snprintf result.
- **settings.cpp screenPos trio** — mode indices are range-checked in store/recall/mode-changed, and load marks a mode set only when all three keys parse.

## Forced revisions to prior findings

None. No R5-S6 reading contradicts RIPTOPL-001…144; the only overlap is RIPTOPL-028, listed above.
