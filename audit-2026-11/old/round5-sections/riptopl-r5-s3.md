# RiptOPL — Round 5, Section R5-S3 (EE src/, second tier)

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492`. Read-only; `git status --porcelain` empty at start and end. No build or hardware claims; performance magnitudes are structural/INFERRED.

## Coverage ledger

| File | Lines read |
|---|---|
| src/app/opl.cpp | 1–2132 (2132) |
| src/core/pad.cpp | 1–1321 (1321) |
| src/sources/rahash.cpp | 1–971 (971) |
| src/platform/tar.cpp | 1–712 (712) |
| src/ui/menuwatch.cpp | 1–584 (584) |
| src/ui/gui.cpp | 1–492 (492) |
| src/core/sfx.cpp | 1–439 (439) |
| src/gfx/psbbnflow.cpp | 1–313 (313) |
| src/app/lwnbdctl.cpp | 1–277 (277) |
| src/core/xparam.cpp | 1–246 (246) |
| src/core/gsm.cpp | 1–212 (212) |
| src/sources/nettime.cpp | 1–158 (158) |
| src/core/riptlite.cpp | 1–139 (139) |
| src/ui/guiqueue.cpp | 1–89 (89) |
| src/sources/gamesource.cpp | 1–67 (67) |
| **Total** | **8,152** |

Cross-checks outside the assigned files (mechanism verification only): `include/padmanager.h` (padData_ layout, MAX_PADS), `include/psbbnflow.h` (flow constants), `src/core/config.cpp:401-410` (configGetStrCopy termination), `src/ui/guioverlay.cpp:195-215` (SX HUD consumption), `src/ui/menusys.cpp:302-309` plus all `nameLength()` implementations (keyboard VLA bound), guigame.cpp grep for `SETTINGS_PERGAME_MIXED` reachability.

## New candidate findings

### R5-S3-01 / PadManager::start() can index padData_ past MAX_PADS — the cap break sits outside the slot loop / Category 6
- **Locations:** `src/core/pad.cpp:1155-1191` (`start()`), esp. `padData_[padCount_]` at `:1175` and the only cap check `if (padCount_ >= MAX_PADS) break;` at `:1188-1189`; array declared `pad_data_t padData_[MAX_PADS]` (MAX_PADS 4) at `include/padmanager.h:84`.
- **Problem:** The `MAX_PADS` break is tested only after a whole port's slot loop finishes, never inside it. `padCount_` therefore only needs to be 1–3 when a multi-slot port is entered for the slot loop to run past the array: e.g. port 0 with one slot (count 1), then a multitap port with 4 slots drives count 2, 3, 4 and then takes `&padData_[4]` — writing `port/slot/state/needsInit/...` into the members that follow the array in PadManager (`paddata_`, `oldpaddata_`, `heldUnknown_`, `delaycnt_[16]`, `paddelay_[16]`, rumble state) and passing that slot's 256-byte `padBuf` (which overlaps the same members) to `padPortOpen` as a DMA buffer in `startOne()` (`pad.cpp:1136-1153`). The overflow is not gated on pads actually being present: `initialize()` returns 1 on every path (`pad.cpp:209, 235, 248, 256, 296` — including "not connected"), so `startOne()` succeeds for every opened slot and every slot increments the count. Reachability of a 4-slot port depends on libpad's `padGetSlotMax()` returning 4 for a multitap port (INFERRED from libpad convention; the loop structure itself is READ).
- **Fix:** Move the cap inside the slot loop (break out of both loops as soon as `padCount_ >= MAX_PADS` before taking `&padData_[padCount_]`). Justification: this adds one comparison to a boot-only loop — it is the smallest change that makes the existing cap actually enforce the array bound the code already intends.
- **Benefit:** Removes a boot-time heap/static corruption path on a two-multitap (or 1 + multitap) rig, in the subsystem every launch depends on.
- **Effort:** S. **Risk:** Low. **Confidence:** High for the loop defect (READ); Med for hardware reachability (INFERRED libpad slot semantics; not hardware-verified).

### R5-S3-02 / deinit() and deinitEx() are the same ~85-line teardown written twice / Category 2
- **Locations:** `src/app/opl.cpp:1278-1370` (`deinitEx`) and `src/app/opl.cpp:1372-1455` (`deinit`).
- **Problem:** A direct diff of the two bodies shows they are line-identical except for the signature, the per-module cleanup leg (deinitEx's spared-mode loop vs deinit's `deinitAllSupport` call) and deinitEx's two extra `launchDiagMark` calls. The entire shared sequence — the ~40 lines of explanatory comments, fade/banner, `cacheShutdownArtLoads()`, `bgmQuiesce()`, the bounded `ioBlockOpsTimed` drain, `cacheEnd`, pad unload, and the `audioEnd/ioEnd/guiEnd/menuEnd/lngEnd/fntEnd/thmEnd/rmEnd/menuWatchStop/configEnd` tail — exists in two copies. This is exactly the file region where ordering bugs are load-bearing (the comments document three separate order-sensitive incidents: #382 BGM race, the art-worker join, the drain-unit mistake); any future ordering fix must be made twice, identically, or the Neutrino handoff and the normal launch/exit paths silently diverge.
- **Fix:** One shared teardown body taking the cleanup leg (and the extra diag marks) as its only variation; `deinit` and `deinitEx` become thin wrappers that supply it. No behaviour change intended.
- **Benefit:** ~80 lines deleted; one place to fix teardown ordering for every launch/exit path.
- **Effort:** M. **Risk:** Med (teardown ordering is hardware-sensitive; the merge must be diff-verified line by line). **Confidence:** High (READ — diff performed).

### R5-S3-03 / sfxGetDropDiag() is a stub that always reports zero drops, while the debug HUD presents its counters as a live instrument / Category 8
- **Locations:** `src/core/sfx.cpp:305-312` (the stub); consumer `src/ui/guioverlay.cpp:203` and the `SX%u/%u` field in the HUD format string at `guioverlay.cpp:214` (cross-checked, outside assigned files).
- **Problem:** `sfxGetDropDiag()` unconditionally writes 0 to both outputs. The HUD nevertheless renders `SX<stale>/<full>` and its comment describes the counters as drops from "the saturated 8-deep ring" — a mechanism the current `sfx.cpp` no longer contains (the pool/ring was replaced by the shared-voice + 50 ms coalesce design at `sfx.cpp:71-84, 395-439`, whose early-return drops in `SoundEffects::play()` are counted nowhere). Net effect: a diagnostic built for issue #364 ("effects cut…") can only ever show SX0/0, which reads as evidence that no sound was dropped precisely when the coalesce/cursor gates are dropping them by design. A half-landed instrument is worse than none: it falsifies the diagnosis it exists to support.
- **Fix:** Delete `sfxGetDropDiag`, the `SX` field and its HUD comment (LESS: the drop concept died with the ring design). If the maintainer wants drop visibility, the alternative is counting the two early returns in `play()` — but that is new instrumentation, not a repair of this stub. Justification for preferring deletion: the current design's drops are deliberate coalescing, already documented at the drop sites.
- **Benefit:** Removes a permanently-zero HUD field and a stale mechanism description; ~10 lines plus the HUD field.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — stub body and HUD consumption both read).

### R5-S3-04 / tar.cpp's four find functions are one lookup written four times / Category 2
- **Locations:** `src/platform/tar.cpp:464-499` (`tarFind`), `:501-529` (`tarFindPrefix`), `:531-553` (`tarFindExact`), `:555-571` (`tarFindPrefixExact`).
- **Problem:** The four functions share the same entry-walk loop, the same `./` + `ART/|CFG/|CHT/` query cleaning, and the same basename extraction; they differ only in (a) prologue — lazy-load/`s_inactive` handling vs require-exact — and (b) the final comparison (full-name vs prefix). Note on scope: this is **not** the cache/parse path cleared in round 2 (`tarReadCache`/`tarParseFile`); the find family was outside that clearance, which is why this is filed as new substance.
- **Fix:** One internal walk parameterised by exact/prefix and by the prologue result; the four public names become one-line wrappers preserving every current contract (including `tarFind`'s side effect of discarding an exact index).
- **Benefit:** ~115 lines reduced to ~50; the name-cleaning rules (a historically bug-prone area in this file) get a single home.
- **Effort:** S. **Risk:** Low (behaviour-preserving if the `tarFind` exact-discard side effect is kept). **Confidence:** High (READ).

## Already filed — not repeated
- **RIPTOPL-027** — NBD server (lwnbdsvr + `src/app/lwnbdctl.cpp`) embedded in every build incl. Lite. This section re-read lwnbdctl.cpp end-to-end; no new substance beyond the embedding question (its bounded pad-retry and preflight/teardown split are deliberate #307 fixes).
- **RIPTOPL-029** — per-frame CPU plasma background, call sites in `src/ui/gui.cpp` (`Gui::drawBackground`/show path). Not re-filed.
- **RIPTOPL-036** — diagnostic build's 8-second static hold (`DelayThread(8000000)`) in opl.cpp's autolaunch diagnostic (now in `miniInit`, `__OPLDIAG`-only). Not re-filed.
- **Round-2 clearance, tar.cpp cache+parse path** — `tarReadCache` entry-count division guard, rawSize re-cap on load, and the parse path stand as cleared; nothing in that path is re-filed here. (R5-S3-04 concerns the separate find family, outside that clearance.)

## Checked and clean
- **pad.cpp keyPressed() lacks the bounds check key()/keyOn() have** — dissolved: every caller passes a compile-time `KEY_*` constant (grep-verified across src/), so the OOB `keyToPad[id]` read is unreachable today; noted only as an inconsistency.
- **pad.cpp remainder (waitReady/initialize/readOne/rumble/lock/self-heal)** — bounded waits, verified-idle gates and re-send tails are deliberate, hardware-documented mechanisms; no unchecked index or unbounded loop found. `actuatorDiag` reading padData_[0..1] is safe: fixed 4-entry member array, zero-initialised.
- **opl.cpp init() pad-wait loop is unbounded** — deliberate and documented (a port that never opens draws the splash after 10 s and keeps waiting; a controller-less boot must not proceed differently). Not a finding.
- **opl.cpp formatErrorMessage / error-message publish chain** — hand-rolled formatter never passes language text to printf, bounds every copy against `errorMessage[256]`; append/clear-by-id logic re-publishes correctly.
- **opl.cpp oplPath2Mode mass-slot parse** — digit accumulation could overflow `slot` only on an absurdly long digit run, but the result is range-checked (`BDM_MODE..BDM_MODE_LAST`) before use; no other path consumes it.
- **opl.cpp bootDisplayPeek/bootPeekKey** — line-piece handling, `mc` + non-`?` gate and atoi clamping downstream are correct.
- **rahash.cpp ISO9660 walker** — directory-record walk is bounded by both the directory extent (`limit`) and the record length; name/version comparison, HDL extent overlap/overflow validation, PS-X payload cap (`payload > RA_HASH_MAX_EXEC - ISO_SECTOR` before adding the header) and VCD sector translation all check out. `hashHdl`'s startup `strncmp` is against a fixed-size descriptor field and fails safe.
- **tar.cpp cache/parse/read paths (outside R5-S3-04)** — per round-2 clearance plus this full re-read: cache entry-count validated by division, offsets fail safe in `tarRead` (short read returns 0), THM split/read/span locking is consistent; `tarThemeRead`'s `*buf = NULL` contract (round-4 cross-check) reconfirmed at `tar.cpp:676-680` region (`tarThemeRead` sets `*buf = NULL` before any return).
- **menuwatch.cpp** — report builder clamps via `add()`; bounded libmc waits; `bootColour`'s 5 s hold is the documented colour-first protocol; large static buffers are `__OPLDIAG`-only and deliberate. Crash stack scan validates SP range/alignment before reading.
- **gui.cpp showKeyboard() VLA `char tmp[maxLength]`** — dissolved: all callers pass `sizeof()` of fixed buffers or a `nameLength()` that every source implements as a small named constant (FavSource's `strlen(fav->text)+1` is bounded by the fav record size).
- **gui.cpp handleOp / deferred-op handling** — OOM paths free/retain correctly in concert with guiqueue's drain (which frees both node and op); GUI_OP_SORT tail recompute is correct.
- **guiqueue.cpp** — enqueue OOM frees the op (incl. the RA text) so ownership cannot leak; drain clears the tail inside the lock. Shutdown leaking still-queued nodes is exit-path only.
- **sfx.cpp (outside R5-S3-03)** — readFile guards the ADPCM sample-count header against short files, frees non-builtin buffers after load, single shared voice is a documented design; `soundDuration(id)` is only called with the `SFX_BOOT` constant.
- **psbbnflow.cpp** — vertex writes are within the static arrays for all three draw paths (ring 6·ROUND_POINTS, tile fan 3·ROUND_POINTS, card 24 ≤ MAX_TRIS·3 colours); starfield modulo uses non-zero constant ranges (584−56); off-screen/degenerate cards are rejected before drawing.
- **lwnbdctl.cpp `strcpy(config.defaultexport, gExportName)`** — dissolved: `gExportName[32]` is filled only by `configGetStrCopy(..., sizeof(gExportName))`, whose implementation force-terminates at `length-1`, so the 32-byte destination cannot overflow.
- **xparam.cpp** — `sprintf` sites in `ResetDeckardXParams` write into fixed offsets of a 30-byte buffer with manually placed terminators and fit exactly; title compares are fixed 11-byte `strncmp`s.
- **gsm.cpp** — `gGSMVMode` is clamped against `sizeof(predef_vmode)` before indexing (the comment's "unreachable" wording for `SETTINGS_PERGAME_MIXED` is stale — guigame.cpp offers the value — but the branch itself is correct and reachable, so no half-finished feature).
- **nettime.cpp** — Date parser validates field widths, the GMT terminator, month-table alignment (`(m - months) % 3`) and per-month day ranges incl. leap years; all waits are deadline-bounded; the two hosts are compile-time constants so the 128-byte request cannot truncate.
- **riptlite.cpp** — remembered values are accepted only when `strlen < 16`, matching `gRemembered[][16]`; the device-key values are numeric enums, so the length gate cannot drop a legitimate value.
- **gamesource.cpp** — defaults are inert by design; `ps1RowKind`'s unconditional `*ember` write is safe (every caller passes `&ember`).

## Forced revisions to prior findings
None. No prior RIPTOPL-001…144 finding was contradicted by this section's full reads; the tar.cpp round-2 clearance stands as scoped (see R5-S3-04's scope note).
