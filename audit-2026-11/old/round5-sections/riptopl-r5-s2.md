# RiptOPL — Round 5, Section R5-S2 (EE `src/` — gfx / ui / sources / platform / core)

- **Repo / pin:** NathanNeurotic/RiptOPL @ `2e347d5ac885cd0ff73e9ad4143781946396f492`, read-only. `git status --porcelain` empty at finish.
- **Scope:** 15 assigned files, read 1..EOF. No PS2 build exists in this VM — no build or hardware claims; performance magnitudes are structural / INFERRED.
- **Prior-art check:** every candidate below was grepped against `Sable-RiptOPL.md`, `-ROUND2.md`, `-ROUND3.md`, `-ROUND4.md` and `round4-sections/*.md` (RIPTOPL-001…144). None of the findings below duplicates a filed substance.

## Coverage ledger

| File | Lines (exact, at pin) | Read |
|---|---|---|
| src/gfx/themeelems.cpp | 2347 | 1..2347 |
| src/gfx/saveicon.cpp | 1315 | 1..1315 |
| src/gfx/renderman.cpp | 965 | 1..965 |
| src/ui/diaeditors.cpp | 683 | 1..683 |
| src/sources/bdmdevices.cpp | 582 | 1..582 |
| src/platform/ioman.cpp | 471 | 1..471 |
| src/platform/md5.c | 382 | 1..382 |
| src/platform/sysdisc.cpp | 311 | 1..311 |
| src/ui/cfgtitle.cpp | 274 | 1..274 |
| src/ui/guioverlay.cpp | 234 | 1..234 |
| src/ui/guigameid.cpp | 212 | 1..212 |
| src/gfx/artqueue.cpp | 155 | 1..155 |
| src/core/cheatconfig.cpp | 111 | 1..111 |
| src/platform/debug.cpp | 74 | 1..74 |
| src/platform/nbns.cpp | 48 | 1..48 |
| **Total** | **8164** | |

Supporting reads (context only, not audited as sections): `include/renderer.h`, `include/ioqueue.h`, `include/saveicon.h`, `include/artqueue.h`, `include/nbns.h`, `include/guigameid.h`, caller ranges in `src/gfx/themes.cpp` (loading-icon count, hint tokens), `src/ui/dia.cpp:1129` (diaScreenWidth initialisation), `src/sources/ethsupport.cpp` (nbns callers), `src/sources/rahash.cpp` (md5 callers).

## New candidate findings

### R5-S2-01 / Theme-element initialisers dereference unchecked allocations throughout themeelems.cpp
- **Category:** 6 (bugs/security — unchecked malloc)
- **Locations:** `src/gfx/themeelems.cpp:211-212,226-227` (`initMutableText`: `malloc` of the struct is dereferenced on the next line; `malloc` of `value` is `memcpy`'d unchecked), `:275-279` (`calloc` of `alias` is passed straight to `snprintf`), `:703-704` (`initImageTexture`: `calloc` result dereferenced immediately), `:767-768` (`initImageInternalTexture`: same), `:810-811` (`initMutableImage`: `malloc` dereferenced immediately), `:1608-1610` (`initBasic`: `malloc` dereferenced immediately), `:2123-2131` (`initItemsList`: `malloc` written through by `itemsListFit` unchecked)
- **Problem:** Every element constructor in this file allocates and dereferences without a NULL check, so an allocation failure during theme load — on a 32 MB console whose theme textures compete for the same RAM — is an immediate NULL dereference crash instead of a disabled element. The file itself proves the intended pattern twice: `initGameCountText` (`:509-518`) checks both of its allocations and degrades to a disabled element (its comment even cites the OOM case), and `thmParseDeviceList` (`:79-81`) checks its `malloc`. The seven sites above are simply the ones that were never given the same treatment. READ.
- **Fix (words only):** Add the same NULL checks `initGameCountText` already uses at each site: on failure return NULL / leave `elem->extended` unset so the element stays disabled, and free any partially built struct. Justification: the added checks are the only thing standing between an OOM during theme load and a hard crash; they convert it into the file's existing disabled-element path.
- **Benefit:** Theme load survives low memory instead of crashing the GUI.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-S2-02 / diaShowKeyb's caret and backspace operate on bytes, corrupting UTF-8 text
- **Category:** 6 (bugs)
- **Locations:** `src/ui/diaeditors.cpp:130-138` (`len = strlen(text)`, `caret = len` — byte counts, despite the comment calling the caret a position "in characters"), `:298-307` (L1/R1 do `caret--` / `caret++` — one byte), `:362-368` and `:396-403` (both backspace paths `memmove` exactly one byte out), contrasted with `:112-126,225-230` (`diaKeybNextChar`/`diaKeybPrevChar` exist, but are used only for view scrolling)
- **Problem:** With non-ASCII seed text — a Japanese game title in a rename field is this fork's headline use case (see cfgtitle.cpp) — backspace from the end deletes only the trailing byte of a multibyte character, leaving an invalid UTF-8 sequence on screen and in the buffer; L1 can park the caret inside a sequence, and typing there splits it. Pure-ASCII fields (the keyboard only ever inserts ASCII) are unaffected, which is presumably why it has survived. Mechanism READ; reachability INFERRED (requires a field seeded with non-ASCII text).
- **Fix (words only):** Make caret movement and backspace use the existing `diaKeybNextChar`/`diaKeybPrevChar` helpers: L1/R1 step by character, and backspace deletes the byte range of the one character left of the caret. No new machinery — the helpers already exist in the same file.
- **Benefit:** UTF-8 names survive keyboard editing instead of being mangled byte by byte.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the mechanism (READ), Medium on reachability (INFERRED)

### R5-S2-03 / IoQueue's per-type pending counters are never reset, and termination cleanup frees requests without decrementing them
- **Category:** 6 (bugs) — diagnostic impact only
- **Locations:** `src/platform/ioman.cpp:275-278` (counters incremented in `appendLocked`), `:148-149` (decremented only on the normal drain path), `:163-168` (termination cleanup frees every queued request with no decrement), `:201-208` (`start()` resets `blocked_`/`terminate_`/`handlerCount_`/`head_`/`tail_`/`current_` but not `pendingByType_[]`)
- **Problem:** `start()` is explicitly written for a restart ("a refused autolaunch falls back to the menu", `:189-195`). If the previous worker was stopped with requests still queued, those requests are freed by the cleanup loop while their `pendingByType_` counts stay behind, and the new session inherits them permanently. `ioGetPending()` then reports phantom pending requests forever — it feeds the debug HUD (`src/ui/guioverlay.cpp:216`) and a diagnostic read in `src/gfx/textures.cpp:931-932`, i.e. exactly the instruments used to triage "is IO stuck?" reports. Per `include/ioqueue.h` the counters are diagnostic-only, so no functional path is affected. READ.
- **Fix (words only):** Zero `pendingByType_[]` in `start()` alongside the other queue state it already resets (the `totalByType_[]` lifetime totals can stay cumulative — they are totals, not pendings). Justification: one `memset` makes the diagnostic truthful across the restart path `start()` exists for.
- **Benefit:** The IO pending counts shown by the debug HUD / texture diagnostics stay truthful after an IO restart.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-S2-04 / cheatConfigLoadSelections re-normalizes both cheat names for every (saved × cheat) pair
- **Category:** 5 (performance)
- **Locations:** `src/core/cheatconfig.cpp:53-60` (`cheatConfigNamesMatch` normalizes both names on every call), `:44-50` (`cheatConfigIsMasterCode` normalizes again), `:77-89` (the load loops: up to `MAX_CODES` saved keys × `MAX_CODES` cheats, calling both of the above per pair — up to three ≤128-char normalizations per pair; `MAX_CODES` = 250 per `include/cheatman.h:40`)
- **Problem:** The worst case is ~187k name normalizations for a single `cheatConfigLoadSelections` call. The common case is far smaller — the saved-key loop breaks at the first missing key (`:79-80`), so real configs with a handful of picks pay little — but a config carrying many `$CheatSelNNN` keys with few matches pays the quadratic cost on every per-game config load. Magnitude INFERRED (structural count, no timing possible in this VM); mechanism READ.
- **Fix (words only):** Normalize each cheat name once per load into a reused scratch table, and each saved name once per key, then compare the normalized strings directly; drop the per-pair calls to `cheatConfigIsMasterCode`/`cheatConfigNamesMatch` in the inner loop.
- **Benefit:** Selection restore goes from O(saved × cheats) normalizations to O(saved + cheats).
- **Effort:** S · **Risk:** Low · **Confidence:** Medium (mechanism READ, magnitude INFERRED)

## Already filed — not repeated

- **RIPTOPL-029** (CPU plasma rendered every frame): the plasma call sites in this section's file (`themeelems.cpp:1302,1333,1724` — `drawBackgroundPlasma` / `drawBackground`) are the already-filed finding's call sites; substance not re-filed.
- **RIPTOPL-037** (NBNS receive-thread overflow) concerns the IOP module `modules/network/nbns/nbns.c`. This section's `src/platform/nbns.cpp` is the separate 48-line EE RPC client; it is not the filed substance (see Checked and clean).
- **RIPTOPL-084** (theme catalog entry allocations unchecked) is in `themes.cpp`'s catalog loader, a different file and different allocations from R5-S2-01's element constructors in `themeelems.cpp`; noted here only to mark the boundary.
- **RIPTOPL-103** (`thmGetTexture` NULL guard) lives in `themes.cpp`; the call sites in this section's files check the returned texture's `Mem` before use and add no new instance.

## Checked and clean

- **src/gfx/saveicon.cpp — model / icon.sys / card-image parsers:** every file-controlled count is bounded before use (shapes/verts/keys caps at `:131-135,174,183`, RLE decoder clamped to remaining length and pixel count at `:92-115`, texture size checked at `:213`), and the VMC FAT walk is loop-bounded by the validated cluster count (`:362-363,413,442`) with superblock geometry validated before any walk (`:517-520`). Dissolved.
- **src/gfx/saveicon.cpp — PS1 card walk:** block chain is visited-bitmap guarded and count-checked against the declared byte size (`:603-626`); frame count derives from a range-checked type byte (1–3, matching the `siPs1Tex[3]` array). Dissolved.
- **src/platform/md5.c (on its own merits, per brief):** the canonical Deutsch RFC 1321 implementation — padding/length handling in `md5_append`/`md5_finish` is the reference logic, and its in-tree callers (`rahash.cpp`) pass bounded read-chunk sizes and short strings. No defect found; the round-2 cross-repo comparison is not re-filed.
- **src/platform/nbns.cpp:** `nbnsFindName` copies the result address even when the IOP result is an error, but all three callers (`ethsupport.cpp:91,302,1199`) test the return value before touching the address. Dissolved.
- **src/platform/sysdisc.cpp — `sysParseBoot2`:** value copy is bounded by `outSize`, an over-long value is rejected rather than truncated (`:92-98`), and a parse failure falls back to the disc-key derivation (`:248-252`). The derived fallback name's non-initial characters are not letter-checked, but the source is the drive's own disc key, not file data. Dissolved.
- **src/ui/cfgtitle.cpp:** `clean()` is a strict UTF-8 validator with boundary-safe truncation (`:95-126`); `keepTitle`'s dual-copy layout (shown title + sort copy) is internally consistent, including the non-RA build where the sort copy is zero-length. Dissolved.
- **src/gfx/artqueue.cpp:** `promote`'s `head_->prev` write is guarded by `req->prev != NULL` (a queued non-head request implies a non-empty list), `pop`/`detachAll` epoch handling is consistent, and `reset()`'s missing DIntr bracket is contract-covered — `include/artqueue.h` states "The queue must be quiescent" for exactly that call. Dissolved.
- **src/core/cheatconfig.cpp — name-match truncation:** a truncation collision between distinct names is not reachable for stored cheat names, because `gCheats[].name` is itself capped at the same `CHEAT_NAME_MAX` the normalizer uses. (The repeated-normalization cost is filed separately as R5-S2-04.) Dissolved as a correctness candidate.
- **src/ui/diaeditors.cpp — `diaShowColSel` never sets `diaScreenWidth`:** its only caller runs inside the dialog loop in `dia.cpp`, which sets both globals at `:1129` before any item handler fires; `rmGetScreenExtents` also returns constant 640×480. Dissolved.
- **src/ui/diaeditors.cpp — `mask_buffer` bounds in `diaShowKeyb`:** the initial `mask_buffer[len]` write is safe because callers seed `len ≤ maxLen-1`, and every insert path is guarded by `len < maxLen-1` before writing `mask_buffer[len]` / `[len+1]`. Dissolved.
- **src/gfx/renderman.cpp — `setupQuad`/`drawQuad` texture dereferences:** `setupQuad`'s NULL-texture path is safe exactly as its comment specifies (only when the caller supplies explicit sizes), and every in-tree caller sampled passes a live texture or checks `Mem` first. No caller passing NULL with `DIM_UNDEF` was found. Dissolved.
- **src/gfx/themeelems.cpp — `drawStaticImage`'s `bgPicture(...)->source` dereference:** all three sites that assign `drawStaticImage` gate on a non-NULL `defaultTexture` (`initStaticImage`, `initBackground`) or a non-NULL wallpaper (`thmWallpaperApply`, which returns early on NULL), so `bgPicture` cannot be NULL when the function runs. Dissolved.
- **src/gfx/themeelems.cpp — hint token shift (`1 << thmHintToken(...)` at :2289):** the token is a small fixed enum returned by the switch in `themes.cpp:1280-1295`; no shift-width hazard. Dissolved.
- **src/sources/bdmdevices.cpp — `bdmWaitForDevice`'s u32 timer:** unlike the u64 case the same file documents at `:544-546`, both endpoints here are truncated to u32 consistently (`:463,474`), so the subtraction is wrap-correct for any timeout under 2³² bus-clock ticks. Dissolved. (`bdmCheckWritable`'s two static 512 B arrays were considered under RIPTOPL-014's pattern and dismissed as below materiality — 1 KB, deliberately static.)
- **src/ui/guioverlay.cpp — `drawBusy` modulo by `loadingIconCount`:** with `LOAD0_ICON == 0`, the loader loop in `themes.cpp` leaves the count at ≥ 1 whenever a loading icon element exists (and the built-in theme sets it to 8); no zero-divisor path found. Dissolved.
- **src/ui/guigameid.cpp:** packet build peaks at 17 bytes into `data_[32]`, `cleanSerial` is size-bounded and NULL-safe. Dissolved.
- **src/gfx/saveicon.cpp — `siLoadFromCards`' `memcpy(best, path, sizeof(best))`:** the source string's length is pre-checked by the `snprintf >= sizeof(path)` rejection at `:752`, so the copied range always contains the NUL. Dissolved.
- **src/platform/debug.cpp:** in a build with no debug transport defined, `debugSetActive` sets `modulesLoaded` and returns success without loading modules — that is the function's contract for such builds (callers gate on the debug settings), not a half-finished path. Dissolved.

## Forced revisions to prior findings

None. No prior finding (RIPTOPL-001…144) is contradicted or narrowed by this section's reads.
