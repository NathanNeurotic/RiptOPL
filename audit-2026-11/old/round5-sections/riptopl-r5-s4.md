# RiptOPL — Round 5, Section R5-S4 (EE src/: mmce / fntsys / util / grid / cheatman / themeviews / artindex / gamemanage / host / elfldr / diaitems / httpclient / ioprp / launchdiag / new / lz4)

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` (verified `git rev-parse HEAD`). Read-only; `git status --porcelain` empty at finish. Every assigned file read 1..EOF. No build or hardware claims; performance/memory magnitudes are structural arithmetic from in-tree constants (INFERRED where stated).

## Coverage ledger

| File | Lines (wc -l) | Read |
|---|---|---|
| src/sources/mmcesupport.cpp | 1855 | 1–1855 |
| src/gfx/fntsys.cpp | 1324 | 1–1324 |
| src/platform/util.cpp | 991 | 1–991 |
| src/gfx/grid.cpp | 714 | 1–714 |
| src/core/cheatman.cpp | 680 | 1–680 |
| src/gfx/themeviews.cpp | 544 | 1–544 |
| src/gfx/artindex.cpp | 453 | 1–453 |
| src/sources/gamemanage.cpp | 347 | 1–347 |
| src/app/host.cpp | 292 | 1–292 |
| src/platform/elfldr_noreset.cpp | 262 | 1–262 |
| src/ui/diaitems.cpp | 230 | 1–230 |
| src/platform/httpclient.cpp | 161 | 1–161 |
| src/platform/ioprp.cpp | 149 | 1–149 |
| src/platform/launchdiag.cpp | 86 | 1–86 |
| src/platform/new.cpp | 61 | 1–61 |
| src/platform/lz4.c | 0 (1 line, no trailing newline; 33 bytes) | whole file |
| **Total** | **8,149** | **16 files** |

Supporting reads outside the assignment (mechanism checks only): `include/utf8.h`, `include/artindexint.h`, `include/fontsystem.h`, `include/grid.h`, `include/gamemanage.h`, `include/cheatman.h`, `modules/network/common/httpclient.h`, `ps2.yaml` (define/source lists), `src/platform/system.cpp:825-860` (ioprp caller).

## New candidate findings

### R5-S4-01 / The `__RTL` renderString is a second, ~150-line text renderer that the live build can never select / Category 1
- **Locations:** `src/gfx/fntsys.cpp:809-961` (`isRTL`, `isWeak`, `FontSystem::renderSubRTL`, and the `#else` branch `renderString`); the live branch is `src/gfx/fntsys.cpp:709-807`.
- **Problem:** Two complete `renderString` implementations coexist behind `#ifndef __RTL` / `#else`. Nothing in the live build defines `__RTL`: `ps2.yaml` (the build definition that actually builds the program) contains no `__RTL` in any target's `defines:` list (grep-verified across the file); the only place that ever added `-D__RTL` is the legacy `Makefile:227-228`, which is already filed as unbuildable (RIPTOPL-003). The dead branch is also functionally superseded: the live branch now shapes Arabic/Persian itself via `txtShape` (`src/gfx/fntsys.cpp:742-747`), which is the stated reason the shaping exists in both `renderString` and `calcDimensionsLocked`. So ~150 lines of renderer — including its own kerning, clipping and RTL-run logic that must be read and kept consistent at every renderer change — are maintained for a configuration no shipped build can produce.
- **Fix:** Delete the `#else` branch, `isRTL`, `isWeak` and `renderSubRTL` from this file (the matching `__RTL` declarations in `include/fontsystem.h:116` and the branch in `src/ui/gui.cpp:157` are outside this section but are the same deletion). If RTL-via-bidi is ever wanted again, the `txtShape` path is where it now lives.
- **Benefit:** One renderer instead of two; every future renderString fix stops needing a second, untestable copy kept in sync by hand.
- **Effort:** S. **Risk:** Low (the branch is unreachable in the live build; the risk is only if an out-of-tree build still defines `__RTL` through the dead Makefile). **Confidence:** High READ for the code and the ps2.yaml/Makefile grep; the "no out-of-tree user" part is INFERRED.

### R5-S4-02 / Glyph-cache pages are allocated from the raw codepoint, before the face is ever asked if it has the glyph / Category 4 (6-adjacent)
- **Locations:** `src/gfx/fntsys.cpp:581-621` (`cacheGlyph`: page computed at :594-600, load attempted only at :617), `src/gfx/fntsys.cpp:187-221` (`prepareGlyphCachePage`: `realloc` to `(pageid + 1)` pointers at :190); constants at `include/fontsystem.h:22` (`GLYPH_CACHE_PAGE_SIZE` 256) and the entry struct at `include/fontsystem.h:41-61` (36 bytes/entry).
- **Problem:** `cacheGlyph` derives `pageid = gid / 256` directly from the decoded codepoint and grows the page-pointer array plus a full 256-entry page (256 × 36 B = 9,216 B) *before* `FT_Load_Char` discovers whether the face contains the character at all; on load error the empty page stays allocated. The Höhrmann decoder in `include/utf8.h` accepts codepoints up to U+10FFFF, so a single astral-plane character in a theme or translation string (an emoji in a game name is enough) forces the pointer array to 4,353 entries (~17.4 KB at 4 B/pointer) plus the 9.2 KB page — ~26 KB retained per font until the next cache flush — for a glyph the font does not have. The only `FT_Get_Char_Index` pre-check in the function (:586-591) exists solely to pick the fallback font; the primary font's own coverage is never consulted before allocating.
- **Fix:** Ask the face first: when `FT_Get_Char_Index(font->face, gid) == 0` (and no fallback supplies it), return NULL before `prepareGlyphCachePage`. This is a deletion-shaped guard using a call the function already makes. Justification for adding a check: it removes a file/string-driven allocation whose size the input controls, using an existing FreeType query — no new mechanism.
- **Benefit:** Per font, up to ~26 KB of retained cache avoided per out-of-coverage high codepoint, and no empty pages retained after failed loads (magnitudes are arithmetic from the header constants — INFERRED, not measured).
- **Effort:** S. **Risk:** Low (a `.notdef`-rendering behaviour change for missing glyphs is the only visible difference; measurement callers already treat NULL as "skip"). **Confidence:** High READ (mechanism); magnitude INFERRED.

### R5-S4-03 / `readFileBuffer` memcpy's into an unchecked `malloc` on the language-file path / Category 6
- **Locations:** `src/platform/util.cpp:631-634`.
- **Problem:** In the `allocResult` branch, `*outBuf = (char *)malloc((lineSize + 1) * sizeof(char));` is followed immediately by `memcpy(*outBuf, fileBuffer->lastPtr, lineSize + 1);` with no NULL check — a failed allocation on a fragmented EE heap is a NULL-pointer write of up to a full line, inside the parser loop. This branch is live: `src/core/lang.cpp:163` opens language files with `openFileBuffer(path, O_RDONLY, 1, 1024)` (allocResult = 1), so every `.lng` line goes through it. Every neighbouring allocation in this same file *is* checked (`openFileBuffer` :473-487, `openFileBufferBuffer` :527-534, `readFile` :352-357), making this the one inconsistency.
- **Fix:** Add the NULL check the rest of the file already uses: on failure, return the line unallocated (or 0) instead of copying. Justification for adding a check: one comparison on a cited crash path, following the file's own established pattern; no new mechanism.
- **Benefit:** One crash-on-low-memory path removed from language loading.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ, including the live caller).

### R5-S4-04 / `load_cheats` reads a `.cht` file of any size into RAM whole, opting out of the cap its own reader provides / Category 4 (6-adjacent)
- **Locations:** `src/core/cheatman.cpp:476` (`buf = read_text_file(cheatfile, 0);` — maxsize 0 = "arbitrary size"), `src/core/cheatman.cpp:393-422` (`read_text_file`: the `maxsize` rejection at :411-415 and the `malloc(filesize + 1)` at :417).
- **Problem:** `read_text_file` was built with an explicit size cap and `load_cheats` is its caller that disables it. The parser downstream can only ever use a bounded prefix of any file: lines are capped at 255 chars (`CHEAT_LINE_MAX`, :305), entries at `MAX_CODES` = 250 cheats (`include/cheatman.h:40`, enforced at :360), and each entry's codes at the fixed `codes[]` array. A `.cht` file larger than that usable bound — a plausible community cheat pack, or a corrupt/oversized file on the card — is still `malloc`'d in full (plus the ~1.03 MB cheat table from `ensureCheatTable`) on a 32 MB machine, and a failed allocation just fails the launch's cheat load. There is no second caller pattern to copy: `load_cheats_buf` receives an already-resident tar member.
- **Fix:** Pass a real `maxsize` to `read_text_file` sized from the parser's own limits (250 cheats × their maximum useful lines), so an oversized file is rejected with the existing "too large" log instead of allocated.
- **Benefit:** Cheat loading's memory cost becomes bounded by what the parser can consume; oversized files fail with a log line instead of an allocation of arbitrary size.
- **Effort:** S. **Risk:** Low (a legitimate file larger than the derived cap would be rejected — the cap must be derived generously from the stated limits). **Confidence:** High (READ).

### R5-S4-05 / ArtIndex retains cap-sized allocations (48 KB per directory) however few entries the directory holds / Category 4
- **Locations:** `src/gfx/artindex.cpp:191` (`malloc(kMaxEntries * sizeof(unsigned int))` for hashes), `src/gfx/artindex.cpp:199` (`malloc(kMaxEntries * sizeof(Pic))` for pics), retained at :281-286; constants at `include/artindexint.h:29-38` (`kSlots` 3, `kMaxEntries` 4096, `Pic` = 8 bytes at :40-45).
- **Problem:** Every directory sweep allocates the 4,096-entry cap up front — 16,384 B of hashes + 32,768 B of pics = 49,152 B — and the slot then *keeps those allocations* for its lifetime (`slot->hashes`/`slot->pics` are published as-is; only `count`/`picCount` record how much is used). The names buffer, by contrast, starts at 16 KB and grows to fit (:200, :231-238) — the file already knows the grow-to-fit pattern. A typical ART folder holds tens to hundreds of files, so with three slots the index permanently holds ~144 KB of mostly-empty arrays (3 × 49,152 B) to answer absence questions about directories that needed a few KB. This is the same shape as RIPTOPL-121 (VCD scan allocates its 2,048-entry cap per folder) in a different, previously unread file.
- **Fix:** Shrink to fit after the sweep (one `realloc` per buffer down to `count`/`picCount` before publishing, mirroring how `names` is sized to use), or grow both buffers incrementally like `names`.
- **Benefit:** ~40+ KB per indexed directory returned on realistic libraries; ~120 KB across the three slots (arithmetic from the in-tree constants — INFERRED magnitudes, READ mechanism).
- **Effort:** S. **Risk:** Low (shrink happens on the owner thread before `valid` is published, the same publication point the code already guards). **Confidence:** High (READ).

### R5-S4-06 / Grid drawing re-walks the whole submenu list every frame just to learn the count and the selected index / Category 5
- **Locations:** `src/gfx/grid.cpp:503-508` (the `for (row = menu->item->submenu; …)` count/index walk at the top of `drawGrid`).
- **Problem:** `drawGrid` is a per-frame draw callback, and before it can compute the page it walks the entire submenu linked list once to count every row and find the selected row's index. On a page backed by a large library (thousands of games on HDD/BDM) that is an O(n) pointer chase per frame — 60 times a second — for two integers, after which the function walks the list twice more (to `gridSlideFrom` and to `first`, :538-551) for the covers it actually draws. The count/index pair is exactly the kind of value the menu system already tracks as the list is built and the cursor moves; recomputing it in the renderer is the per-frame waste pattern, distinct from the bounded walks to the visible page.
- **Fix:** Take count/index from the menu state the caller already maintains (or cache them keyed on the list head + selected item and invalidate when either changes — the slide state at :184-188 already keys cached state on the menu pointer the same way).
- **Benefit:** Per-frame cost on Grid pages becomes proportional to the visible page (≤ 24 covers) instead of the whole library (magnitude INFERRED — structural, no hardware measurement).
- **Effort:** M. **Risk:** Med (a stale cached count would mis-page; the invalidation key must be the list rebuild, not the frame). **Confidence:** High READ for the walk's existence and per-frame placement; INFERRED for user-visible magnitude.

### R5-S4-07 / The MMCE candidate-slot scan is written twice in one file / Category 2
- **Locations:** `src/sources/mmcesupport.cpp:176-198` (`sendGameID`: `cands[3]` / `tried[2]` / slot-parse / presence-devctl loop) and `src/sources/mmcesupport.cpp:561-587` (`resetCards`: the same loop).
- **Problem:** Both functions build the identical candidate list — resolved device root first, then `"mmce0:/"`, `"mmce1:/"`, skipping NULL/short entries, parsing the slot digit from `cands[c][4]`, de-duplicating via `tried[2]`, and probing presence with `fileXioDevctl(cands[c], 0x1, …)` — as two hand-maintained copies ~25 lines each. They have already drifted in the small (the send version copies the winning candidate back into `mmceDevice` and applies the VMC-slot mask; the reset version counts resets and waits out the busy bit), which is how this pattern rots: a future fix to candidate eligibility (a third slot form, a presence-probe change) must be made twice, in a file whose other probes (`detectSlot` :665-676, `setPrefix` :678-731, `getDeviceRoot` :521-545) are further variations on the same question.
- **Fix:** One static helper that invokes a callback (or fills a small array) for each present candidate slot; both callers keep their per-slot bodies. Deletion of one copy of the enumeration, no behaviour change.
- **Benefit:** One enumeration to fix; the two launch/reset paths stop drifting apart.
- **Effort:** S. **Risk:** Med (both copies sit on launch paths; the helper must preserve candidate order and the send path's fallback-copy exactly). **Confidence:** High (READ).

## Already filed — not repeated

- `src/sources/mmcesupport.cpp:1556` `strcpy(filename, game->startup)` — already filed in round 1 as one of the six per-source launch-leg `strcpy` sites (the RIPTOPL-018 location list, `Sable-RiptOPL.md`); the surrounding launch leg's copy-paste across sources is that finding's substance.
- `src/ui/diaitems.cpp:130-144` (`diaSetString` truncating into 32-byte `UI_STRING_SIZE` storage) — the mechanism of **RIPTOPL-078**; read in full here, no new substance.
- `src/platform/util.cpp:873-874` (`sysDeleteFolder`'s unchecked `malloc` + `sprintf`) — **RIPTOPL-015**, unchanged at the pin.
- `src/gfx/fntsys.cpp:744` (`static char shaped[2048]`) and `:1219` (`static char joined[2048]`) — two of the enumerated locations of **RIPTOPL-014** (permanent static buffers). The same class instance in this section's files, `src/core/cheatman.cpp:39` (`static u32 gImage[MAX_IMAGEWORDS]` = 4 KB permanent BSS for the optional .img feature), is noted here as belonging to that finding's class rather than re-filed.
- `src/platform/httpclient.cpp` whole file — the EE-side field termination was cleared in round 2 (`Sable-RiptOPL-ROUND2.md`: the IOP request buffer is bounded by the RPC struct fields and the EE side NUL-terminates all three fields). Full read here closes the ledger: `HttpEstabConnection` :45-46, `HttpSendGetRequest` :66-79 and `HttpStreamBegin` :103-112 (memset-zeroed args + `strncpy` to size-1) all terminate as recorded; **no new substance**. The HTTP source stack's existence is RIPTOPL-028.
- `src/sources/mmcesupport.cpp` `sbRename` trace — dissolved by the parent in round 4 (buffer provably ≥ 33 bytes on every reachable path); the full read here produced no new evidence against that verdict, and it is not resurrected.

## Checked and clean (candidates that dissolved on full read)

- **mmcesupport.cpp card-wait family** (`waitCardReady` :314, `waitSwapDoneOn` :373, `waitHistorySwitch` :451, `gameIdSettle` :500): four similar poll loops, but each waits a different phase with different bounds/START semantics, extensively documented — merging them is risk, not simplification. Dissolved as a duplication candidate.
- **mmcesupport.cpp `refreshArtRoots` :642** reads `prefix[len - 1]` with `len` possibly 0 — unreachable: the function returns early on an empty prefix at :636-637 before the length test. Dissolved.
- **mmcesupport.cpp `ps1SaveDir` :1840** writes `partition[0]` without a NULL/size check — the sole caller (`src/gfx/themeelems.cpp:2167`) always passes real fixed arrays, the same contract every other source's `ps1SaveDir` relies on. Dissolved (contract-shaped).
- **mmcesupport.cpp launch VMC loop :1448-1478** — `nativeVmcSlotWord` is consulted per slot with a full-payload bound and the `memcpy` is gated on `slotWord >= 0`; the Neutrino leg closes both VMC fds before the GameID push (:1623-1626) and the native bail paths close them too (:1544-1548, :1575-1578, :1693-1696). No leak found on re-read.
- **fntsys.cpp `cacheGlyph` NULL-face path :613-617** logs a NULL face and proceeds to `FT_Load_Char` — callers (`renderString`, `calcDimensionsLocked`) only run on slots the loader marked valid, and the default slot is loaded in `init()`; no in-tree path renders with an unloaded slot. Contract-shaped; dissolved.
- **fntsys.cpp `fntRenderStringFit` marquee maths :1006-1036** — binary searches are monotone in prefix width per call, the `at[]` table is capped at 255 chars with the end sentinel written at :1004, and the truncation branch caps by byte offset before formatting (:1025-1026). Dissolved.
- **util.cpp `delay()` :941-950** (NOP busy-wait) — deliberate hardware-timing primitive; every caller found documents its ~0.25 s tick unit (e.g. `src/app/opl.cpp:1268`, `src/gfx/artload.cpp:42`). Bounded at every call site. Dissolved as a Cat 5 candidate.
- **util.cpp `CheckPS2Logo` `strcat` :834-837** — the destination `text[1024]` is filled by a `snprintf` whose format and arguments are fixed-length (~55 chars worst case), so the appends cannot approach the bound. Dissolved.
- **util.cpp `checkMCFolder` :255 `mcID & 1`** — `mcID` holds the *character* `'0'`/`'1'` (:90/:95), and `'0' & 1 == 0`, `'1' & 1 == 1`, so the arithmetic is correct, if fragile-looking. Dissolved.
- **util.cpp `writeFileBuffer` flush/direct-write split :653-671** — the direct-write branch is only reached after the buffered flush test, `totalQueued`/`writeError` bookkeeping is consistent with `closeFileBuffer` :674-701. Dissolved.
- **cheatman.cpp `parse_buf` caps** — cheat-entry guard at :360 (`cheat_index + 1 >= MAX_CODES`), per-entry code guard at :373 (`code_index < MAX_CHEATLIST`, which is the `codes[]` array's own size, `include/cheatman.h:67`), and the line-length cap at :334-335 all check out against the allocations. Dissolved.
- **cheatman.cpp `trim_str` :261-288** — the all-whitespace case returns via `is_empty_str` before the first/last scans, so neither scan can run off the string. Dissolved.
- **themeviews.cpp (whole file)** — every key/value construction is `snprintf`-bounded (`CONFIG_KEY_NAME_LEN`/`CONFIG_KEY_VALUE_LEN`); the entry-walking copiers (`thmApplyDisplayLayout` :279, `thmApplyAltBackground` :324, `thmSynthCopyBlock` :432) append only keys that their own suffix/prefix tests skip, as their comments claim; view indices are range-checked before any `1u << v` shift (:16, :170, :217). The one asymmetry — `thmViewNative`'s unbounded `main<j>` scan (:73) vs the 64-slot cap in the synth path (:376) — terminates at the first absent block under the contiguous-numbering rule the whole file documents; not filed.
- **artindex.cpp `findPicture` name walk :396-404** — `name[baseLen]` is only evaluated after `strncasecmp(name, base, baseLen) == 0`, which guarantees `name` has at least `baseLen` chars, so the read lands inside the stored name or its NUL. The names-overflow path (:230-236) frees `names` and the end-of-build check (:250-253) then frees `pics` too, so no dangling offsets are published. Dissolved (both were the file's scariest-looking lines).
- **gamemanage.cpp hidden-list paths** — `hiddenSet`'s unhide rollback (:208-216) restores names/ps1/count exactly; `hiddenSave`'s count-0 unlink (:160-166) probes existence first; `hiddenAppend` checks both the 512 cap and the malloc (:79-84). `gmCollect`'s unlistable-folder fallback costs a bounded 33 art probes + ≤2 cfg/cht probes, on a user-initiated delete plan only. Dissolved.
- **host.cpp (whole file)** — pure forwarding layer over `gui*`/`opl*` functions; the two composed-message buffers (:32 `text[384]`, :62 `text[480]`) are `snprintf`-bounded. The `fillCoreConfig` overlay test (:178) trusts the `raWorkBytes()` reservation contract, which returns the list size alone unless the packet fits — consistent at the only producer. Dissolved.
- **elfldr_noreset.cpp ELF program-header walk :227-237** — `phoff`/`phnum`/`filesz` are used unchecked, but the image is the build's own embedded `elfldr_irx`, not file/network data; the argv path in the same function *is* hardened (kernel 15-arg/256-byte budget enforced at :190-206 before the VLA at :211 is declared). Input-trust-shaped; dissolved.
- **diaitems.cpp `diaGetString` :113-128** — a negative `length` would reach `strncpy` as a huge `size_t`, but every caller passes a `sizeof` buffer length; the terminator write is guarded by `length > 0`. Contract-shaped; dissolved.
- **ioprp.cpp (whole file)** — `patch_IOPRP_image` has no destination-size parameter, but its sole caller over-provisions by construction (`src/platform/system.cpp:836`: `size_IOPRP_img + size_cdvdman_irx + size_cdvdfsv_irx + size_eesync_irx + 256`, malloc NULL-checked at :841-846) and every input is an embedded blob from the same build; the `strcpy` into `romdir_out->fileName[10]` copies names from that same trusted ROMDIR. Dissolved.
- **launchdiag.cpp infinite park loop :73-83** — deliberate: `LAUNCH_DIAG` is 0 by default (`include/launchdiag.h:41`), and the loop only runs in a diag build after the launch has already been refused past teardown, so a human can photograph the refusal code. Same family as RIPTOPL-036's diagnostic hold, by design. Dissolved.
- **new.cpp (whole file)** — operator new/delete over malloc/free with a documented abort-on-exhaustion contract (:3-6); the nothrow forms return NULL as required. Dissolved.
- **lz4.c** — a single-line `#include "../modules/isofs/lz4.c"` shim (the same include-shim pattern round 2 established for shared IOP code), live in the EE target's source list (`ps2.yaml:641`). Not a copy, not dead. Dissolved.

## Forced revisions to prior findings

None. No prior finding (RIPTOPL-001…144) is contradicted by this section's full reads; the two prior items touching these files (RIPTOPL-015, RIPTOPL-078) were re-verified at the pin, and round 4's `sbRename` dissolution stands.
