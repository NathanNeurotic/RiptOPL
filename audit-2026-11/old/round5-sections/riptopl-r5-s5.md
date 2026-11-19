# RiptOPL — Round 5, Section R5-S5 (EE src/: system, dialogs, BDM launch/boot, HTTP, RetroGEM/RA, splash/notify/modal, disc, ZSO, frame, art worker)

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` · Read-only audit · No build or hardware claims; performance magnitudes are structural/INFERRED unless marked READ.

## Coverage ledger

| File | Lines read | Range |
|---|---|---|
| src/platform/system.cpp | 1,689 | 1..EOF (whole file this round; RIPTOPL-071 previously covered only the producer chain) |
| src/ui/dia.cpp | 1,329 | 1..EOF |
| src/sources/bdmlaunch.cpp | 999 | 1..EOF |
| src/sources/httpsupport.cpp | 924 | 1..EOF |
| src/sources/retrogem.cpp | 675 | 1..EOF |
| src/sources/bdmboot.cpp | 496 | 1..EOF |
| src/ui/guimodal.cpp | 453 | 1..EOF |
| src/sources/rawatch.cpp | 326 | 1..EOF |
| src/app/autolaunch.cpp | 307 | 1..EOF |
| src/ui/guisplash.cpp | 266 | 1..EOF |
| src/ui/guinotify.cpp | 218 | 1..EOF |
| src/sources/discsupport.cpp | 183 | 1..EOF |
| src/sources/rapopslaunch.cpp | 129 | 1..EOF |
| src/platform/zso.c | 94 | 1..EOF |
| src/ui/guiframe.cpp | 58 | 1..EOF |
| src/gfx/artworker.cpp | 44 | 1..EOF |
| **Total** | **8,190** | 16 files |

Supporting reads (context only, not audited as sections): `include/httpsupport.h`, `include/supportbase.h` (struct size), `include/guinotify.h`, `include/gamesource.h`/`include/iosupport.h` (flag values), `include/rawatch.h`, `modules/network/common/ra_watch.h`, `modules/network/common/rapops_cfg.h`, `ee_core/include/coreconfig.h`, plus caller greps for `USBA_crc32`, `gOPLPart` writers and `showPartition()`.

## New candidate findings

### R5-S5-01 / `USBA_crc32` rebuilds its constant CRC table on every call, and the table is 4× oversized
- **Category:** 5 (with a Cat 4 facet)
- **Locations:** `src/platform/system.cpp:465` (`static unsigned int crctab[0x400]`), `src/platform/system.cpp:475-489` (table build + data loop in `USBA_crc32`)
- **Problem:** The 256-entry table is a mathematical constant, yet it is recomputed with 256×8 iterations on **every** call. Callers invoke it per USBLD entry and per part-path construction (`src/sources/supportbase.cpp` path builder and size walk, `src/sources/ethsupport.cpp` launch settings), so a library scan with USBLD titles pays the rebuild once per game/part instead of once per process. Separately, `crctab` is declared with 0x400 entries (4 KB of BSS) while both the build loop and the data loop only ever touch indices 0–255 (the index is `byte ^ ((crc >> 24) & 0xFF)`), so 768 entries / 3 KB are permanently dead BSS on a 32 MB machine.
- **Fix:** Build the table once (a one-time initialised flag, or a precomputed 256-entry constant table) and shrink the array to 256 entries. The table **values** must not change: the initial `crc` state and the reversed table layout are shared with the PC twins (`tools/pc/iso2opl`, `tools/pc/opl2iso` carry the identical function) and the resulting checksum is baked into on-disk `ul.%08X.` filenames — any "standard CRC32" cleanup would orphan existing USBLD installs.
- **Benefit:** Removes ~2,048 multiply/xor steps per call from scan/launch paths; frees 3 KB BSS.
- **Effort:** S · **Risk:** Low (values unchanged; the risk is only in touching the algorithm, which the fix does not) · **Confidence:** High, READ (function and caller sites read; per-scan magnitude INFERRED from call sites).

### R5-S5-02 / Partition notification dereferences `strchr(gOPLPart, ':') + 1` with no NULL check
- **Category:** 6
- **Locations:** `src/ui/guinotify.cpp:110-115` (`Notifications::show`, `showPart_` branch)
- **Problem:** `col_pos = strchr(gOPLPart, ':'); col_pos++;` — if `gOPLPart` ever contains no colon, `col_pos` becomes `(char *)1` and the following `snprintf(..., "%s", col_pos)` reads from address 1: an EE exception on the notification path. The three sibling popups in the **same function** (`showCfgPopup`, `showTheme_`, `showLang_`) all guard the identical `strchr` result with `!= NULL`; this branch is the only one that does not. Reachability today is low — every writer found stores an `hdd0:...` string — but `gOPLPart` is also populated from partition-candidate/config data and nothing enforces or documents the colon invariant at this consumer.
- **Fix:** Guard the result exactly as the sibling branches in the same function already do (skip or fall back to the whole string when there is no colon). Deletion-style fix: no new code pattern, the function's own idiom.
- **Benefit:** Removes a crash-on-bad-data path from a purely cosmetic popup.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the mechanism (READ — this function and all `gOPLPart` writers grepped), Low on present-day reachability.

### R5-S5-03 / RetroGEM GameID reader seeks to disc-supplied LBAs with 32-bit offset arithmetic and no bound against the image size
- **Category:** 6
- **Locations:** `src/sources/retrogem.cpp:188` (`offset = reader->baseOffset + (long)lba * sectorSize;` in `retrogemReadSector`), fed by `src/sources/retrogem.cpp:346-347` (root-record LBA/size from the PVD) and `src/sources/retrogem.cpp:373-374` (SYSTEM.CNF file LBA/size from a directory record)
- **Problem:** The root-directory LBA and the SYSTEM.CNF extent LBA are read verbatim from the image being identified — i.e. from untrusted file/disc data (.VCD/.BIN/.CUE targets). On the EE, `long` is 32-bit, so `lba * 2048/2352` for a large crafted LBA wraps, and no value is ever checked against the actual image file's length before `fseek`. Memory stays safe (every read is a fixed 2,048-byte sector into a stack buffer), so the consequence is bounded — a wrapped seek reads the wrong sector and a crafted image can steer the parser to a forged SYSTEM.CNF elsewhere in the file, producing a wrong GameID/barcode for the RetroGEM display — but the parser validates every other field it consumes (record lengths, name lengths, sector types) and leaves this one unchecked.
- **Fix:** Reject an LBA whose byte offset cannot be represented / lies past the image length before seeking (one range check in `retrogemReadSector`, where layout and base offset are already known). Justification for adding a check: it is the single unvalidated disc-controlled offset in an otherwise fully validating parser, and the check is one comparison at one choke point.
- **Benefit:** A malformed or hostile image can only ever fail identification, never alias another part of the file as its SYSTEM.CNF.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the arithmetic (READ), INFERRED on impact (no memory unsafety found; impact is a wrong/absent GameID).

### R5-S5-04 / Settings dialogs re-run the full fit-gap layout search — including text wrapping — on every frame
- **Category:** 5
- **Locations:** `src/ui/dia.cpp:616-624` (`diaSettingsFitGap`), called at `src/ui/dia.cpp:640` (every `diaRenderUI` frame), walking `diaLayoutBottom` (`src/ui/dia.cpp:568`); the render pass then measures every row again at `src/ui/dia.cpp:690`
- **Problem:** `diaRenderUI` runs per frame for as long as a dialog is open. On a Settings page it first calls `diaSettingsFitGap`, which re-walks the **entire** item list once per candidate gap (from `spacingH` down to the 7 px floor — more iterations the larger the user's font scale), and each walk re-measures and, for row-leading labels, re-wraps text (`diaWrapExtra` → `fntWrapText` into the 1 KB scratch buffer). The render pass immediately afterwards walks the same list and re-wraps/re-measures the same labels a second time to draw them. The final content bottom depends on the gap only as a per-row additive term, so the search does not need a full re-layout per candidate, and none of its inputs (item list, font, screen size) change between frames of a static dialog.
- **Fix:** Compute the layout once per call without the gap term and solve for the largest fitting gap arithmetically (or cache the chosen gap keyed on the dialog pointer + font scale + screen height, invalidated when any of them change). No behaviour change: same gap choice, computed once.
- **Benefit:** Removes multiple full-list measure/wrap passes from a 60 Hz UI loop on every Settings page; most visible on the long pages (General, Network) with large fonts. Magnitude INFERRED (structural; no hardware timing in this VM).
- **Effort:** M · **Risk:** Low–Med (the gap choice must remain bit-identical, including the already-fits case) · **Confidence:** High on the repeated-work structure (READ), INFERRED on frame cost.

### R5-S5-05 / HTTP catalog parser allocates the 2,048-row cap (~0.9 MB) for every catalog, however small
- **Category:** 4
- **Locations:** `src/sources/httpsupport.cpp:165-166` (`httpParseCatalog`); cap at `include/httpsupport.h:19` (`HTTP_CATALOG_ROWS_MAX 2048`); entry size from `include/supportbase.h` (`base_game_info_t`, ~188 B) and the local `http_catalog_extra_t` (~260 B, dominated by `path[256]`)
- **Problem:** Every catalog fetch first mallocs both arrays at the maximum row count — roughly 2048 × 188 B ≈ 385 KB of games plus 2048 × 260 B ≈ 532 KB of extras, ~0.9 MB total — even though a real `games.csv` holds a handful to a few hundred rows, and the byte cap (256 KB) already bounds the input. `HttpSource::update` parses the new catalog while the previous catalog is still resident (it is freed only by `setCatalog`), so a refresh peaks at roughly two cap-sized catalogs plus the fetch buffer, on every refresh, in EE RAM that is also holding themes, art and the game lists. The Test Server path parses a second, throwaway catalog the same way. The parser already knows how to fail cleanly; it does not need the rows pre-allocated at cap.
- **Fix:** Count lines first (the buffer is already fully in memory) or grow the arrays geometrically as rows are accepted; keep `HTTP_CATALOG_ROWS_MAX` as the acceptance limit, not the allocation size.
- **Benefit:** Typical catalog memory falls from ~0.9 MB to a few KB–tens of KB per parse and removes the double-catalog refresh peak — meaningful heap/f fragmentation headroom on the EE.
- **Effort:** S–M · **Risk:** Low (note: `test_catalog_limits.py` extracts this function verbatim, so the harness contract must be kept) · **Confidence:** High, READ (constants and struct layouts read; sizes are arithmetic from them).

### R5-S5-06 / The bounded "poll identity until budget expires" loop is written three times in bdmboot.cpp
- **Category:** 2
- **Locations:** `src/sources/bdmboot.cpp:180-189` (known-type literal-slot poll), `src/sources/bdmboot.cpp:216-225` (per-tier literal-slot poll, inside `resolveLiteralMassSlot`, `src/sources/bdmboot.cpp:151`), and the larger scan/poll loop in `resolveWithBudget` (`src/sources/bdmboot.cpp:372-468`)
- **Problem:** One file implements the same pattern — snapshot `GetTimerSystemTime()`, probe, compare elapsed against a millisecond budget, `DelayThread(100 * 1000)`, repeat — three times, with the escalation/budget bookkeeping re-derived at each site. This file's own comments record two bugs that lived in exactly this pattern (a u32 timer snapshot that broke post-boot resolves, and fallback ordering that latched the wrong device); each fix had to be reasoned about per copy, and the copies' budgets/escalation rules have already drifted apart in structure if not yet in effect.
- **Fix:** Extract the one poll-until helper (probe callback + budget + escalation hook) and let the three call sites supply their probe; delete the two inline re-implementations.
- **Benefit:** One place to fix timer/budget defects in the boot-device resolver; roughly 60–90 lines removed from the most failure-prone boot path in the tree.
- **Effort:** M · **Risk:** Med (boot-path code; the three sites' subtle budget differences must be preserved as parameters, not flattened) · **Confidence:** High, READ.

### R5-S5-07 / Boot-dir resolver re-opens the boot ELF and boot folder on every slot, every 100 ms, for the whole budget
- **Category:** 5
- **Locations:** `src/sources/bdmboot.cpp:387-400` (per-scan `bdmReadDeviceIdentity` + `bootElfPresent` + `bootDirPresent` per slot), poll cadence at `src/sources/bdmboot.cpp:468`; file defined at `src/sources/bdmboot.cpp:44` (`bootDirPresent`, a Dopen/Dclose) and `src/sources/bdmboot.cpp:65` (`bootElfPresent`, an open/close)
- **Problem:** The legacy `resolveWithBudget` scan does not cache anything between iterations: each 100 ms pass over up to `MAX_BDM_DEVICES` slots performs an identity probe (Dopen + ioctls + Dclose), an open/close of `<tail>/<elfName>`, and a Dopen/Dclose of the boot folder — for up to the full 14 s budget (bootstrap: ~6.5 s), i.e. on the order of a hundred full re-scans in the worst case, all during boot or a settings save, on the same slow mass devices whose late arrival is the reason for polling. A slot that has answered once with a given driver/identity cannot change which ELF file it holds within one resolve; only not-yet-mounted slots need re-probing.
- **Fix:** Remember per-slot identity/ELF/dir results across iterations of one resolve and only re-probe slots that have not yet answered (the literal-slot resolver in the same file already polls a single known slot instead of re-scanning everything).
- **Benefit:** Worst-case boot resolution drops from ~100 full device scans to one scan plus probes of still-missing slots; less fileXio traffic competing with the very enumeration being waited on. Magnitude INFERRED.
- **Effort:** M · **Risk:** Med (the duplicate-install tie-breaking — literal slot first, definitive-ELF over folder-presence — is subtle and documented; caching must preserve it) · **Confidence:** High on the repeated-IO structure (READ), INFERRED on wall-clock saving.

### R5-S5-08 / The MX4SIO/MMCE quiesce block is copy-pasted verbatim in both PS1 launch legs
- **Category:** 2
- **Locations:** `src/sources/bdmlaunch.cpp:103-108` (`BdmLauncher::launchCue`) and `src/sources/bdmlaunch.cpp:197-202` (`BdmLauncher::launchVcd`)
- **Problem:** The identical gate — `if (bdmDeviceType == BDM_TYPE_SDC) { if (!cacheAbortMmceImageLoadsTimed(500)) { warning(...); return; } }`, including its rationale comment — exists twice in one file, in the two launch legs that must stay in lock-step (both hand off to an external PS1 core over the shared SIO2 bus). The 500 ms settle budget and the error string are exactly the kind of constant that will be tuned once and forgotten in the other copy. (Distinct from RIPTOPL-018, which files the per-source PS2 launch legs across five files; this duplication is inside one file, between the two PS1 legs.)
- **Fix:** One small private helper on `BdmLauncher` (e.g. `quiesceMmceForPs1Handoff`) called from both legs; delete the second copy.
- **Benefit:** One place for the settle budget/failure behaviour; ~6 lines removed.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-S5-09 / `GetModStorageLocation` takes a `compatFlags` parameter it never uses
- **Category:** 1
- **Locations:** `src/platform/system.cpp:755` (definition; body reads only `startup`), sole caller at `src/platform/system.cpp:1398`
- **Problem:** The static helper's second parameter is dead: the two-entry `mod_storage_location_list` lookup keys on the game ID alone. The parameter invites a reader (and the next patch author) to believe module-storage placement is compat-flag-sensitive when it is not — in a launch path where placement mistakes are expensive (the IGR-menu/overlay placement code in the same function chain reasons carefully about exactly this address).
- **Fix:** Delete the parameter and update the one call site.
- **Benefit:** One less misleading input in the launch path; trivially less code.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

## Already filed — not repeated

- **RIPTOPL-018** — the per-source launch-leg duplication, whose location list includes `strcpy(filename, game->startup)` at `src/sources/bdmlaunch.cpp:904` (still present verbatim at the pin; the surrounding leg is that finding's substance).
- **RIPTOPL-028** — HTTP as a game source assessed as a removal candidate as a whole stack; R5-S5-05 above is a new, narrower memory finding inside it, not a re-file of the stack assessment.
- **RIPTOPL-034** — commented-out/legacy markers, including the zero-effect `settings->common.fakemodule_flags |= 0 /* TODO! fake ilinkman ? */` at `src/sources/bdmlaunch.cpp:983` (still present; the sibling `|= 0` at :986 for MX4SIO is the same idiom in the dispatch that R5-S5's read confirms now has the pre-dispatch guard documented in `BdmLauncher::launch`).
- **RIPTOPL-071** — the Ethernet link-mode producer chain through `src/platform/system.cpp` (config → `config->g_ps2_ETHOpMode`): not re-filed; the rest of `system.cpp` was read whole this round per the section note.
- **RIPTOPL-078** — the shared 32-byte dialog string storage: `dia.cpp`'s `char tmp[32]` edit buffer in `diaHandleInput` is the same mechanism and is covered by it; not re-filed.

## Checked and clean (candidates that dissolved on a full read)

- **`USBA_crc32` "uninitialised `count`/`crc`"** — dissolved: `count` is the table-build inner-loop variable and exits at 0, which is exactly the start index the data loop needs; `crc`'s initial value is the last table entry, deterministic and shared with the PC tools. The defect is the rebuild/waste (R5-S5-01), not correctness.
- **`sprintf` of IP strings into `EECoreConfig_t`** (`sysLaunchLoaderElf`) — dissolved: destination `g_ps2_ip[16]` (ee_core/include/coreconfig.h:43) fits a maximal dotted quad (15 chars + NUL) exactly; the netblock variant prints unsigned bytes with `%d`, same bound.
- **`sysLaunchLoaderElf`'s ELF program-header copy loop** — dissolved as an external-data issue: the ELF is the embedded `eecore_irx`, not file/disc input. (`sysPlaceIgrOverlay`, by contrast, fully validates magic, segment bounds and the header ABI before copying — read and clean.)
- **`sendIrxKernelRAM` module-table count** — dissolved: the module count is bounded by construction (≈33 conditional entries of 8 B) into a kilobyte-scale storage region; no input controls the count.
- **`sysShutdownDev9`'s 100-try devctl spin** — dissolved as a finding: it is bounded, each `fileXioDevctl` is a synchronous RPC, and it runs only on the DEV9 power-off path; the bound is deliberate and commented. Likewise `sysReset`'s `SifIopReset`/`SifIopSync` spins are the standard SDK boot idiom with no alternative primitive.
- **`exitFmcbDropsBrowser` / `exitCardUpdateIgnoresBrowser`** — clean: line parsing is bounded (`line[48]` with length clamp), the LoadBOOTer probe checks `lseek`/`read` results exactly, and all card/USB opens are closed on every path read.
- **`diaHandleInput` enum handling** — clean: out-of-range indices are counted-and-clamped in both `diaEnumText` and the input handler before any indexing.
- **`httpParseCatalog` duplicate check is O(n²)** — dissolved as a finding: capped at 2,048 rows over ≤12-char startup strings, once per fetch; negligible next to the allocation cost filed as R5-S5-05.
- **`HttpSource` constructor setting `enabled = MODE_FLAG_NO_UPDATE`** — dissolved: every consumer of `enabled` found by grep tests truthiness only, and `HttpSource::init` sets it to 1 before the source is used; the in-code comment already records the preserved behaviour. Wrong-looking, presently harmless.
- **`httpTestServer` parse-result handling** — clean: `httpParseCatalog` leaves its out-pointers NULL on every failure path read, the `rows < 0` case skips the probe loop, and both frees are NULL-safe.
- **`rapopslaunch` `ipcfg` assembly** — dissolved: three maximal dotted quads plus separators total 48 B against `RAPOPS_IPCFG_MAX` 64 (modules/network/common/rapops_cfg.h), with each `snprintf` size-bounded; the blob-fill loop clamps offsets against `RAPOPS_BLOB_BYTES` before every `memcpy`.
- **`rawatch` watch/node list loading** — clean: count/bytes are range-checked against `RA_WATCH_MAX`/`RA_SNAP_MAX_BYTES` before any read or copy in both the file and network paths, node chains are fully validated in `takeNodes`, and the in-memory cache key is safe because a startup is at most `GAME_STARTUP_MAX + 1` (13) chars against `watchStartup_[16]`.
- **`retrogem` ISO9660 record/CNF parsing (other than R5-S5-03)** — clean: directory-record length, name length and CNF token copies are all bounds-checked; `displayRetroGemGameID` caps the ID at 11 chars into a 64 B packet (max 18 B used).
- **`bdmlaunch` Neutrino `fragBudgetOk` count handling** — clean on counts: negative ioctl results are rejected for both the ISO and each VMC before any summing, and the over-budget drop-a-VMC prompt logic was read in full.
- **`autolaunch` BDM filename handling** — clean: `isValidIsoName`'s length output is initialised and checked, over-long/unsupported names bail, and both `strncpy` legs are followed by explicit NUL stores within the field sizes.
- **`zso.c` sector-at-a-time HDD reads** — dissolved: `probed_fd`/`probed_lba` are set only by the logo/ZSO probe paths (util.cpp, hddsupport.cpp, supportbase.cpp), which read a few sectors; batching would save nothing measurable. The fd/LBA sentinel split is fragile-looking but the invariant is maintained by all three setters found.
- **`guisplash` boot-logo glow (12 extra pixmap draws/frame)** — dissolved: splash-lifetime only, and it replaced a worse per-frame animation; not a steady-state cost.
- **`guimodal` `CheatScreen` with zero cheats** — dissolved: `gCheats` is a fixed `MAX_CODES` array, the draw loop skips empty names, and a stray toggle on the empty slot has no visible or persisted effect found.
- **`guiframe` FPS integer truncation** — `__DEBUG`-only code; the non-debug watchdog path is exact.
- **`artworker` focus tracking by length + FNV-1a hash without a string compare** — dissolved as a finding: the only consequence of a (crafted, same-length) hash collision is a stale art-priority epoch; no correctness or safety effect.
- **`discsupport`** — clean in full: boot-path identity parsing is length-checked, the busy latch is RAII-released on every exit of the worker, and all notice buffers are `snprintf`-bounded.

## Forced revisions to prior findings

None. No prior finding (RIPTOPL-001…144) was contradicted by this section's full reads; the two system.cpp-adjacent items (RIPTOPL-071 chain, RIPTOPL-034's bdmlaunch marker) were re-verified present at the pin at their current line numbers.
