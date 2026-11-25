# Sable — RiptOPL ROUND 8 (test-corpus full line-read)

**Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` (rebuild/main) — live head re-verified at round start (`git ls-remote` == clone HEAD == pin); **no delta, no revalidations owed.** Clone git-clean at start and end; read-only held throughout. Section evidence: `round8-sections/section-01…11.md` (+ `batch-*.txt`, `dup-census.txt`, `fresh-wc.tsv`, `r7-ledger.tsv`).

## Scope and method

Round 7 closed the `.github/scripts` corpus as "BOUNDED, not line-closed": 24 files / 3,170 lines read in full (every non-test script + 13 test/verifier files), the other 278 files (60,094 lines) mechanism-scanned, 1 binary `.pyc` unread. This round line-read **all 278 remainder files in full**, plus a 4-file supplement under `tools/pc/http/tests/` (585 lines) that no round had line-read in full. Read order followed the assignment: (1) the 69 files carrying skip/environment-conditional patterns (18,152 lines, batches 01–03), (2) the remaining Python files largest-first (41,240 lines, batches 04–10), (3) fixtures + supplement (1,287 lines, batch 11). Eleven section agents read in parallel; every candidate below was consolidated by the coordinator, cross-section duplicates merged into single findings with every location, and the six headline mechanisms (RIPTOPL-296, -298, -302, -304, plus the audio-scan glob and the jq skip) were **independently re-verified by the coordinator against the clone** before numbering. One section's tests were also executed at the pin (section 05: all 16 exit 0 — its candidates are latent, as labelled).

**Result: 48 new findings, RIPTOPL-296…343; one formal extension of RIPTOPL-291 (10 new instances, 9 files); one revision of a round-7 negative claim (attached to RIPTOPL-301). RiptOPL total: 343 findings.**

## Final corpus ledger (exact)

| Set | Files | Lines | Status after round 8 |
|---|---|---|---|
| `.github/scripts` read in full, round 7 | 24 | 3,170 | line-closed (round 7) |
| `.github/scripts` read in full, round 8 | 278 | 60,094 | **line-closed (this round)** |
| `.github/scripts/__pycache__/hostmap.cpython-312.pyc` | 1 | — (19 by byte-split `wc`) | unread — binary bytecode cache, git-ignored and untracked at the pin; not a source file |
| **`.github/scripts` total (fresh `wc -l` at pin)** | **303** | **63,283** | **302 tracked source files, 63,264 lines, all line-read** |
| Supplement: `tools/pc/http/tests/` (test_stream.c, test_catalog.c, test_catalog_limits.py, test_conformance.py) | 4 | 585 | line-closed (this round) |

Notes: the round-7 ledger table's per-file counts sum to 63,292; the 9-line difference against the fresh total is entirely the `.pyc` row (28 in the round-7 table vs 19 by fresh byte-split `wc`) — every tracked source file's count matches the round-7 ledger exactly (verified file by file). Round 7's "63,283" headline is the fresh-`wc` figure and is the correct one. C/C++ test files elsewhere (`tests/host/*.cpp`, `ee_core/tests/*.c`, `modules/**/tests/*.c`, `tests/pccf/*.c`) were line-read in rounds 2–5 and are not part of this corpus; `tests/host/gen_settings_globals.py` (21 lines) is a generator inside the round-5-read `tests/host` tree, not a test. Per-file verdict rows for all 282 round-8 files are in `round8-sections/section-01…11.md`.

Batch accounting (round 8): batch 01 — 14 files / 6,800 lines / 4 candidates; 02 — 23 / 6,798 / 7; 03 — 32 / 4,554 / 6; 04 — 12 / 6,799 / 7; 05 — 16 / 6,800 / 4; 06 — 21 / 6,799 / 7; 07 — 28 / 6,797 / 4; 08 — 40 / 6,793 / 5; 09 — 70 / 6,784 / 10; 10 — 11 / 468 / 5; 11 — 15 / 1,287 / 2. Total 282 files / 60,679 lines / 61 candidates → 48 findings + 1 extension after consolidation merges (extractor census merged 5 section candidates into RIPTOPL-313; dead-layout census merged 3 into RIPTOPL-321; 7 skip candidates folded into the RIPTOPL-291 extension).

---

## Findings

### RIPTOPL-296 — The APA raw-namespace scan never sees a C++ source, so the policy it pins is unenforced where the code lives
- **Category:** 6 (bugs/security)
- **Locations:** `.github/scripts/test_apa_table_safety.py:518-537` (`check_raw_apa_namespace()`, the `rglob('*.[ch]')` at :528); production spellings the scan cannot see: `src/app/autolaunch.cpp:91` (`"hdd0:%s"`), `src/app/settingsio.cpp:450,1307` (`"hdd0:__common"`)
- **Problem:** READ, coordinator-verified. The scan's own comment states only three files may spell the raw `hdd0:`/`hdd1:` APA namespace, but `Path.rglob('*.[ch]')` matches only `.c`/`.h` names — executed at the pin it matches 5 files under `src`, 21 under `ee_core/src`, 138 under `include`, and **zero `.cpp` files**, while the tree holds 115 `.cpp` files (vs 26 `.c`). Consequences: (1) existing non-allowed `.cpp` spellings (cited above) are never checked; (2) `RAW_APA_ALLOWED` is itself three `.cpp` paths, so the allowlist is never consulted for the files it names; (3) the `fileXioFormat(` ban in the same loop inherits the blind spot. The check passes today and would pass with a raw-APA regression in any C++ file. Same defect shape as RIPTOPL-297.
- **Fix:** Widen the scan to the suffixes the tree actually uses (`.c`, `.h`, `.cpp`, `.hpp`), then reconcile `RAW_APA_ALLOWED` with the legitimate `.cpp` spellings that surfaces, so the remaining rule is the intended one.
- **Justification:** Test-only change; the allowlist may grow entries — justified because the alternative is a security-policy check that is vacuous for the tree's main language.
- **Benefit:** The APA-namespace policy becomes enforced where new code is actually written.
- **Effort:** M (glob fix is S; the allowlist reconciliation is the real work) / **Risk:** Med (a naïve widening turns CI red on existing spellings until reconciled; which spellings are legitimate needs a human decision) / **Confidence:** High (READ — glob behaviour executed at the pin; coordinator re-verified the glob line and both `.cpp` spellings)

### RIPTOPL-297 — test_audio_threads Part 1 scans only `*.c` after the C++ migration: the `audsrv_wait_audio()` ban is unenforced in all 115 `.cpp` files
- **Category:** 7 (build/CI)
- **Locations:** `.github/scripts/test_audio_threads.py:10` (claim), `:45-51` (scan), `:12,57,63` (stale source name); production lives at `src/core/bgm.cpp` (the path the same file reads for Part 2, :53)
- **Problem:** READ, coordinator-verified. The docstring says Part 1 "statically forbids `audsrv_wait_audio()` in src/", but the scan is `(root / 'src').rglob('*.c')`. At the pin `src/` contains exactly 5 `.c` files (none of them audio code) and 115 `.cpp` files (both counts verified). A call reintroduced in any `.cpp` file — including `bgm.cpp` itself, the BGM player the ban exists for — passes Part 1 untouched. The same C→C++ drift survives in the file's own text: docstring and two failure messages name `src/core/sound.c`, which does not exist at the pin. Same defect shape as RIPTOPL-296.
- **Fix:** Make the Part 1 scan cover `*.cpp` (and `*.c` if kept), keeping the existing `//`-comment stripping (which correctly neutralises the explanatory comment at `bgm.cpp:124`); correct the docstring and failure messages to `src/core/bgm.cpp` / `BgmPlayer::` names.
- **Benefit:** The file's headline guarantee becomes true for the whole tree again.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-298 — test_bdm_partitions asserts GPT partitions far beyond the device mount successfully, encoding RIPTOPL-160's missing extent validation as correct
- **Category:** 6 (bugs/security) — cross-reference RIPTOPL-160; this files the *test*, not the production bug
- **Locations:** `.github/scripts/test_bdm_partitions.py:91,136` (simulated device is exactly 64 sectors), `:238-246` (entry at LBA `0x100000010` asserted connected), `:248-253` (entry at LBA 3000–3009 asserted connected, rationalising comment at :249); production check it normalises at `modules/bdm/src/part_driver_gpt.c:130,172-175`
- **Problem:** READ, coordinator-verified (both assertions and the comment read at the pin). RIPTOPL-160 established that the GPT parser never validates an entry's extent against the device. Two scenarios here assert exactly that missing validation as expected behaviour on a 64-sector device: an entry beginning at `0x100000010` must connect, and an entry at 3000–3009 must connect — the latter with the comment "Only the entry sectors are read at mount, so partitions may lie beyond the simulated disk", which treats a harness artefact as permission for the production behaviour. Unlike the MBR case in the same file (:194-201, dissolved in section 06 — a documented saturation exception), no exception is documented for GPT. If RIPTOPL-160 is ever fixed, these assertions fail for the wrong reason and pressure the fixer to keep the hole; until then they certify it.
- **Fix:** In the two scan-bounding scenarios, either give the simulated device a `sectorCount` covering the entries (the mock's field is independent of its 64-sector backing array) or drop the mount-success assertion and assert only the scan bound (`max_read_sector`) the scenarios were written to prove.
- **Benefit:** The GPT scan-bounding coverage survives a future extent-validation fix, and the suite stops blessing partitions that lie wholly outside the device.
- **Effort:** S / **Risk:** Low / **Confidence:** High that the test asserts beyond-device mounts (READ); Med that no undocumented GPT saturation exception justifies them (INFERRED from the file's own practice of documenting the MBR exception)

### RIPTOPL-299 — test_ratls_host.py only ever sends lowercase HTTP tokens, so the RIPTOPL-189 case-sensitive token defect passes the suite
- **Category:** 6 (bugs/security) — cross-reference RIPTOPL-189; the production bug is not re-filed
- **Locations:** `.github/scripts/test_ratls_host.py:237` (refusal fixture `Transfer-Encoding: chunked`), `:238` (`Content-Encoding: gzip` fixture), `:313-336` (all chunked GET fixtures); production defect at `src/platform/ratls.c:159,171,430` per RIPTOPL-189
- **Problem:** READ. Every chunked fixture uses the exact lowercase value `chunked`, and the only content-encoding fixture uses `gzip` (refused either way). No fixture sends `Chunked`, `CHUNKED`, or `Content-Encoding: Identity`. RIPTOPL-189 established that ratls compares these token values case-sensitively while HTTP tokens are case-insensitive, so a conformant server emitting `Chunked` is mis-framed — and this suite, the only host test for ratls.c, cannot catch it because its fixtures mirror the implementation's assumption.
- **Fix:** Add capitalised-token cases to the existing fixtures: a `Transfer-Encoding: Chunked` GET that must return the same de-chunked body as the lowercase case, and a download with `Content-Encoding: Identity` that must be accepted. (These fail until RIPTOPL-189 is fixed — that is the point; land them with the fix.)
- **Justification:** Adds test cases only, reusing the existing harness.
- **Benefit:** The ratls suite covers the token-casing input space the standard allows.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-300 — Neither ps2cnf host test exercises the ≥64-char BOOT2 value that RIPTOPL-158 flags, so the unterminated-string path has no regression test
- **Category:** 6 (bugs/security) — cross-reference RIPTOPL-158; the production bug is not re-filed
- **Locations:** `.github/scripts/test_cnf_missing_equals.py:1` (claims to "Execute the complete production SYSTEM.CNF file parser"), `:30-38` (full case list; longest BOOT2 value is 21 chars); `tests/host/test_ps2cnf.cpp` (read in full by section 10 — likewise no case over 21 chars); defect site per RIPTOPL-158 at `src/platform/ps2cnf.cpp:54-65`, `include/ps2cnf.h:1` (`CNF_PATH_LEN_MAX 64`)
- **Problem:** READ. The batch file executes the real parser under ASan/UBSan with a strong harness, but every case is far below the 64-char cap, so the one input class already established as defective — a BOOT2 value of 64+ printable chars, returned as success in an unterminated 64-byte buffer — is untested in both suites that cover this parser. A fix for RIPTOPL-158 could regress, or the cap behaviour change shape, with both suites green.
- **Fix:** Add a ≥64-char BOOT2 case (and a 63-char boundary case) to the case list, expecting the terminated/truncated result the RIPTOPL-158 fix defines; land it together with that fix.
- **Justification:** Adds test cases only, reusing the existing harness.
- **Benefit:** The known crafted-CNF defect class gains the regression test both suites lack.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-301 — Release-publication test ends with a self-referential assertion: the "remote unchanged" check cannot fail on values
- **Category:** 6 (bugs/security)
- **Locations:** `.github/scripts/test_release_publication.py:440-468` (`test_backup_without_recorded_digest_aborts_before_any_upload`; final assertion at :468); correct sibling form at :335
- **Problem:** READ. Line 468 is `self.assertEqual(remote, {"stable.ELF": remote["stable.ELF"]})` — the expected value is read out of the dict being checked, so the value comparison is tautological; only a change in the key set can fail it. The preceding assertions (:466-467, no upload / no delete call) do real work, but if `upload_verified` mutated the stored asset entry in place, this test would still pass. The sibling test at :335 captures `original` before the call and compares against it — :468 looks like that assertion copy-adapted incorrectly.
- **Revision recorded:** this instance narrows round 7's corpus-level negative claim ("no self-comparisons found" — that was a mechanism scan for literal `x == x` patterns; this is a dict-literal self-reference the scan could not see). Round 7's claim stands for the patterns it scanned; it does not cover this shape.
- **Fix:** Capture the expected remote dict before invoking `upload_verified` (as :335 does) and assert equality against that snapshot.
- **Benefit:** The fail-closed backup-verification test actually verifies the remote asset's contents are unchanged.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-302 — test_release_editions.py:28 can never fail: it membership-tests a dict of target names
- **Category:** 6 (bugs/security)
- **Locations:** `.github/scripts/test_release_editions.py:20` (dict construction), `:27-28` (the two `assertNotIn` lines); production data from `tools/build_edition.py` `definition()` for all four editions (executed at the pin by section 03)
- **Problem:** READ, coordinator-verified (lines read at the pin). Line 27 is a real check: `"ps2sdk_socket_compat"` not in the loader target's `libs`. Line 28 repeats the literal against `targets`, which line 20 built as `{t["name"]: t for t in config["targets"]}` — `assertNotIn` on a dict tests its keys, i.e. target names. Verified by execution: 82–84 targets per edition, none named `ps2sdk_socket_compat`, and no target's `libs` currently contains it either. The assertion fails only if a target is one day *named* `ps2sdk_socket_compat` — a different event from the one it sits beside. The ee-core (or any non-loader target) could link `ps2sdk_socket_compat` tomorrow and this test would still pass.
- **Fix:** Point line 28 at the intended list — the core target's `libs`, or every target's `libs` in the loop the file already has at :31 — and delete the dict-membership form.
- **Benefit:** An edition-isolation rule the file claims to enforce becomes enforceable for the target it was written for.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ + executed)

### RIPTOPL-303 — test_mbedtls_vendor swallows ValueError for whole edition/configuration combinations, uncounted
- **Category:** 6 (bugs/security)
- **Locations:** `.github/scripts/test_mbedtls_vendor.py:50-56` (the swallow; assertions it gates at :59-92)
- **Problem:** READ. For every edition × configuration × toolchain combination, the test calls `build_edition.definition(...)` inside `try:` and does `except ValueError: continue`, on the stated assumption that any ValueError is "a combination build_configuration.py refuses". Nothing records which combinations were exercised or refused, and no later assertion calls `definition()` again. If `definition()` began raising ValueError for a combination that is valid today, every mbedTLS/ratls/updater wiring assertion for that combination would silently stop running and the file would still print PASS. The set of legitimately refused combinations is itself behaviour worth pinning and is pinned nowhere.
- **Fix:** Pin the expected valid/refused combination set explicitly (or count exercised combinations and assert the count, and assert the known-valid combinations were among them), so a new ValueError on a previously valid combination fails the test.
- **Justification:** Adds a small expected-set literal; removes an unbounded silent skip.
- **Benefit:** A build-definition regression cannot masquerade as a legitimate refusal and strip TLS wiring coverage unnoticed.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-304 — test_game_type_labels.py's "functional harness" tests a private re-implementation, not the production code, with constants that contradict the production enums
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_game_type_labels.py:54-83` (harness-local `formatDisplayName()`), `:85-107`, `:109-119`; production: `src/gfx/themeelems.cpp:2046-2057`, `include/libview.h:32-41`, `include/iosupport.h:10-21`
- **Problem:** READ, coordinator-verified (harness constants and production `LIB_VIEW` enum read at the pin). Sections 1–5 of the file are real string-presence checks on production sources. Section 6, the only compiled/behavioural part, extracts nothing: it defines its own `formatDisplayName()` and asserts against that copy, so any change to the real labelling logic — the `isFolder` branch (folders get `"/"`, never a label; the copy has no folder case), the `libListRowView()` call, the buffer choice — passes unchanged. The copy's constants are also stale against production: the harness sets `APP_MODE 4` and `LIB_VIEW_ALL/ISO/PS1` = 0/1/2, while production has `APP_MODE` = 10 and `LIB_VIEW_ISO = 0, LIB_VIEW_PS1 = 1, LIB_VIEW_ELF = 2, LIB_VIEW_ALL = 3`. Because the copy is self-contained, neither discrepancy can ever be caught by the test itself. Same shape as RIPTOPL-305, -306, -307.
- **Fix:** Delete the private harness and either (a) extract and compile the real labelling branch from `drawItemsList` the way neighbouring suites extract production functions, with the real enums, including a folder row and an ELF-view row; or (b) drop section 6 and keep only the real static checks, renamed so the file no longer claims a functional test.
- **Justification:** Option (a) adds only the extraction/stub scaffolding every sibling suite already has; it replaces, not supplements, the copy.
- **Benefit:** The one behavioural check for an opt-in user-visible feature would fail when the feature's logic changes.
- **Effort:** M / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-305 — PS1 id-keyed art scenarios run against a simplified in-harness model of `ImageCache` and `cacheGetTextureEx`; the production methods in `artload.cpp` are never executed
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_ps1_retrogem_metadata.py:176-282` (harness model), `:335` (the only production art code inserted is `cacheGetTextureIdOrName`), `:344` (the only direct `artload.cpp` check is a string check); production not executed: `src/gfx/artload.cpp:215-295` (`slotKeyedOther`, `holdsFallback`, `markFallback`, `residentByValue`, `adoptFallback`), `src/gfx/texcache.cpp:226` (`cacheGetTextureEx`)
- **Problem:** READ. The harness copies diverge from production in exactly the details the scenarios claim to test: production `adoptFallback` skips `UID == 0` entries, refuses a parked-absent entry whose `failEpoch` differs, distinguishes states by `qr`/`lastUsed`/`texture.Mem` and refreshes `lastUsed`; the harness version parks any ABSENT fallback unconditionally under a 4-state enum. Production `residentByValue` requires `qr == NULL && lastUsed != 0 && texture.Mem != NULL`; the harness returns any READY slot. Production `slotKeyedOther` guards `strlen(value) < sizeof(key)`; the harness omits it. The harness `cacheGetTextureEx` has no eviction at all — the "eviction" scenarios simulate it by manually `memset`-ing slots in the test driver. A repo-wide grep confirms no other test file mentions `adoptFallback`/`residentByValue`, so no other suite covers them either. Same shape as RIPTOPL-304, -306, -307.
- **Fix:** Make the harness execute the production code it now models: extract the five `ImageCache` methods and `cacheGetTextureEx` into the harness (stubbing only the art queue, frame id and fail-epoch globals), and delete the harness re-implementations.
- **Justification:** Adds extraction/stub wiring but removes ~90 lines of parallel model code; net test code shrinks.
- **Benefit:** The id-vs-name fallback, parked-absence and eviction behaviour is verified against the code that ships.
- **Effort:** M / **Risk:** Med (harness surgery around the queue/epoch globals; scenarios unchanged) / **Confidence:** High (READ — harness and production compared line by line)

### RIPTOPL-306 — The keyboard test's exhaustive bold-offset loop tests its own Python copy, not the production function
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_keyboard_keycaps.py:63-83`; production `src/ui/diaeditors.cpp:74-88`
- **Problem:** READ. The production step is checked only by string-matching its `while` condition. The exhaustive sweep that follows (~2.3M combinations) runs `bold_offset()`, a Python function defined inside the test itself with its own `scale = lambda v: v * display // virtual`. Production `diaBoldStep` is never extracted or executed, and the production scale functions (`rmScaleX`/`rmScaleY`) are never read. A production change to the step's initial value, increment, cap or return — or a renderman scale that is not `v * display // virtual` — leaves every assertion green while the property the sweep "proves" no longer holds. The corpus already contains the correct pattern (test_cdvdman_own_stack.py:116-120 mutates production text and requires its checker to reject the mutant). Same shape as RIPTOPL-304, -305, -307.
- **Fix:** Extract the real `diaBoldStep` (and the real scale bodies) into the compiled-harness style the corpus uses and run the sweep against them; or, at minimum, add a mutation check in the cdvdman style. Delete the Python reimplementation once the real function is under test.
- **Justification:** Adds an extraction harness of the kind ~50 sibling files already carry; net test code shrinks when the model is deleted.
- **Benefit:** The one exhaustive (million-case) check in the file starts covering the code that ships.
- **Effort:** M / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-307 — test_ripps2_drive_status.py tests a re-typed copy of the drive-size arithmetic, not the production expression
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_ripps2_drive_status.py:156-168,178-188`; production expressions at `src/ui/drivestatus.cpp` (dsSizeText call sites) and `src/core/filemgr.cpp` (fmRoomFor), string-matched at test :159-160
- **Problem:** READ. The free/total-KB and room-check expressions live as Python string literals; the test asserts those exact strings appear in production, then compiles *its own copies* and runs all the 8 TB / no-wrap arithmetic assertions against the copies. The production expressions themselves are never executed — unlike test_legacy_parity_885_898.py in the same batch, which extracts the actual condition text from guigame.cpp and compiles that. Any production refactor into an equivalent helper makes the string assert fail spuriously, and the two copies must be edited in lockstep forever. Same shape as RIPTOPL-304, -305, -306.
- **Fix:** Extract the three expressions from drivestatus.cpp/filemgr.cpp at test time (the legacy-parity pattern) and compile those, deleting the Python-side literal copies.
- **Benefit:** The arithmetic assertions bind to the code that ships; equivalent refactors stop failing the test.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-308 — Save-icons harness exit code is discarded: a post-print crash or non-zero exit passes
- **Category:** 6 (bugs/security)
- **Locations:** `.github/scripts/test_save_icons.py:357-359` (`run()` returns only `.stdout.strip()` from a `check=False` call); harness `main` returns 0 on every intended path (HARNESS string, :343)
- **Problem:** READ mechanism, INFERRED scenario. Every other harness-running file in its batch checks the child return code; this one never reads it. Most call sites are compensated by exact stdout comparison, so a crash before printing still fails. The residual gap is a failure *after* the expected line is printed — a crash in `saveIconFreeModel`/`fclose` after `printf` in the harness's model/card modes, or any non-zero exit with correct stdout — which reports as a pass. Since the harness exits 0 on all intended paths (errors are reported in-band as `ERR`/`NONE`), checking the return code is strictly stronger at no cost.
- **Fix:** Make `run()` treat a non-zero harness return code as a test failure, the way test_smb_vmc_io_result.py's `run()` does.
- **Benefit:** Crashes in model/card cleanup stop masquerading as passes.
- **Effort:** S / **Risk:** Low / **Confidence:** Med (mechanism READ; no post-print crash observed — the masking scenario is INFERRED)

### RIPTOPL-309 — test_patch_zone_restore: the sbPrepare harness's exit code is never checked; its second half is verified only by C `assert()`s whose failure reaches Python only as a stdout side-effect
- **Category:** 7 (build/CI)
- **Locations:** `.github/scripts/test_patch_zone_restore.py:185` (`check=False`, return code unused), `:186-193` (stdout-only comparison); harness at :100-164 (the iLink-ID loop at :140-163 prints nothing and is checked only by `assert()` at :142,145,147-149,152,157,159-162)
- **Problem:** READ that the return code is discarded; the present-day rescue mechanism is INFERRED. Every other harness launch in its batch uses `check=True` or branches on `returncode`; this one compares only `.stdout` against six `printf` lines — all printed *before* the harness's second half, whose scenarios report failure exclusively by aborting on `assert()`. Today an abort there still fails the Python side, but only indirectly: the captured pipe output is fully buffered and an abort does not flush it, so the six expected lines are lost and the comparison mismatches (standard glibc stdio/abort behaviour, not executed here). Any change that flushes that output first makes an aborted harness present exactly the expected stdout and pass, with its exit status still unread. Sibling shape: RIPTOPL-308.
- **Fix:** Treat a non-zero harness exit as a failure in the same place the stdout is compared.
- **Benefit:** The iLink-ID half of the suite is gated by the harness's own verdict instead of by a buffering accident.
- **Effort:** S / **Risk:** Low / **Confidence:** High on the discarded code (READ); Med on the rescue mechanism (INFERRED)

### RIPTOPL-310 — Two suites have Python sections gated only by bare `assert`: they vanish, with their "ok" prints intact, under `python3 -O` / `PYTHONOPTIMIZE`
- **Category:** 6 (bugs/security)
- **Locations:** `.github/scripts/test_ripps2_backend_port.py:155-176` (asserts at :155,161,166,168,174; unconditional `ok` prints at :162,171,176) and `:539-551`; `.github/scripts/test_vmc_rebuild.py:594-604` (asserts at :600-603); invocations at `.github/workflows/flavours.yml:65-68` and `tools/run_tests.sh:7-11` — neither passes `-O` nor sets `PYTHONOPTIMIZE` (grep-verified, READ)
- **Problem:** READ. Nine Python `Assert` nodes in test_ripps2_backend_port.py are the *only* enforcement for the reset-hook calls, the FreeType registration/absence checks, the MMCE send-wait check, the launch-ordering checks and the `fntNewLibrary` ordering check; the four asserts in test_vmc_rebuild.py are the only enforcement of the rebuild dialog wiring. Everything else in both files is fail-closed. Today CI does not set the flag, so the checks run — this is latent, not current vacuity, and a different mechanism from RIPTOPL-291. If either runner is ever invoked with optimisation on, those sections execute nothing and still print their `ok …` lines.
- **Fix:** Convert those Python-level asserts to the failures-list / explicit-raise pattern the sibling suites already use, keeping the same checks.
- **Benefit:** These suites' pass/fail stops depending on an interpreter flag no workflow documents or pins.
- **Effort:** S / **Risk:** Low / **Confidence:** High on the mechanism (READ); INFERRED on likelihood (no current CI path sets the flag)

### RIPTOPL-311 — `check_frames` in test_dialog_gui_lock.py scans each dialog file as one linear stream, so an unbalanced lock in one function can mask a missing lock in a later one
- **Category:** 6 (bugs/security)
- **Locations:** `.github/scripts/test_dialog_gui_lock.py:143-166` (`check_frames`); the per-function machinery the fix needs already exists in the same file (`function_body` at :64-79)
- **Problem:** READ for the mechanism, INFERRED for the masking scenario. The check's stated rule is per dialog loop: every `rmStartFrame()` preceded by `guiLock()`, `guiUnlock()` only after the matching `rmEndFrame()`. The implementation does not scope that rule to a function or loop: `locked`/`frame` are initialised once per file, updated by a single pass over the whole file, and checked for balance only at end-of-file. Lock state left over from one function remains set while the scan enters the next, so that function's `rmStartFrame()` is judged "locked" by the previous function's lock, and a later function's own `guiUnlock()` clears the stale state with no failure recorded. The scan also runs on raw file text, unlike the same file's other checks, which strip comments first — a call named in a comment would move the state machine.
- **Fix:** Run the same state machine per function body (using the file's existing helpers) on comment-stripped text, and require lock/frame balance at each function's end as well as the file's.
- **Benefit:** A missing `guiUnlock()` or `guiLock()` is attributed to the function that lost it and cannot be cancelled out by an unrelated function.
- **Effort:** S / **Risk:** Low / **Confidence:** Med (mechanism READ; no masking instance exists at the pin — INFERRED scenario)

### RIPTOPL-312 — test_mmceman_fork.py's provenance claim covers the whole vendored tree; the check hashes 2 of its ~15 files — and the one file that changed at the pin bump is not among them
- **Category:** 7 (build/CI/repo clutter — verification claim exceeds the verification)
- **Locations:** `.github/scripts/test_mmceman_fork.py:3-4` (docstring claim), `:21-25` (two-entry `PINNED` table), `:41-42` (the only two hash assertions); `modules/mmce/mmceman/ORIGIN.txt:3-5` (pin-bump delta is `src/sio2man_hook.c`) and `:27` (repeats the tree-level claim); vendored tree: `modules/mmce/mmceman/src/` (6 sources) + `include/` (8 headers)
- **Problem:** READ. The docstring states the provenance half as whole-tree ("the other vendored files are that commit's files unchanged") and `ORIGIN.txt:27` repeats that the test "checks the tree against the commit". The code checks exactly two files: `src/mmce_fs.c` after reverse-patching and `include/mmce_cmds.h`. The other built vendored files — including `src/sio2man_hook.c`, which per ORIGIN.txt is the *only* file that differs between the previous pin and the current pin — carry no hash at all, so an edit to any of them passes CI while both documents claim tree-level provenance. The `PINNED` comment even notes the two hashed files are "unchanged since" the older pin. This is the claim-vs-check shape of RIPTOPL-286 in a new mechanism: not circular, but partial verification presented as whole-tree.
- **Fix:** Either hash the remaining built files from the same pinned commit (one `PINNED` entry per file), or narrow both claims — docstring :3-4 and `ORIGIN.txt:27` — to the two files actually checked. The first is preferred for `src/sio2man_hook.c` at minimum.
- **Justification:** The preferred fix adds hash constants (data, not code) for files already in the tree; no new harness.
- **Benefit:** A silent edit to the vendored driver — including the file the last pin bump exists to update — would fail CI, and the provenance documents would stop claiming a check that does not run.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-313 — The corpus's function-extraction and compile/run helpers exist in 100+ divergent copies; only one extractor in the corpus is comment-aware
- **Category:** 2 (duplication) — the mechanism *underneath* RIPTOPL-005's corpus, censused here for the first time; not a re-file of -005 (which characterised the tests themselves)
- **Locations:** Corpus-wide (AST census at the pin, coordinator-run; `round8-sections/dup-census.txt`). Definition counts: `function` 107 definitions, `read` 43, `run` 37, `function_text` 32, `check` 19, `define` 12, `compile_and_run` 9. Identical-body floor: 21 cross-file identical-body groups (≥4 lines), ~316 duplicated lines beyond first copies — a floor, since near-identical variants do not hash-match. Semantic variants (section-verified, READ): naive first-`\n}` slicing (e.g. `.github/scripts/test_ember_disc_selection.py:18`, `test_settings_fit.py:26`, and 13 more in batch 03 alone), brace-counting (e.g. `test_ilink_bd_reset.py`, `test_bdm_index.py:23`), signature-plus-index forms, and exactly **one** comment/string-aware copy (`test_udpfs_smap_fifo_tail.py:30`). Compile/run helpers diverge in failure semantics (failures-list vs raise vs `check=True`) and even in sanitiser policy: `test_favorites_storage_io.py:28-30` compiles with `-fsanitize=address,undefined` while its sibling `test_favorites_parity.py:28` compiles with none. Representative per-batch censuses: section 01 (≥11 files), section 02 (17 files, 3 variants), section 03 (23 files, 3 semantics), section 06 (13 files), section 08 (identical 12-line `function_text` verbatim in 4 files: `test_config_missing_open_once.py:31`, `test_hires_line_blend.py:27`, `test_hires_thread_priority.py:28`, `test_vmode_safe_area.py:27`)
- **Problem:** READ. Every behavioural suite stands or falls on extracting a production function correctly, yet the extraction logic — the corpus's single most load-bearing helper — exists in dozens of near-copies with materially different failure modes. The naive variant silently truncates a function at the first line consisting of `}` at column 0; brace-counting variants miscount braces inside comments/strings; only one copy skips comments while counting. A truncation does not announce itself: the harness compiles the truncated body and the suite passes or fails for a reason unrelated to the production change. Sharing is demonstrably possible: `hostmap.py` already sits beside the tests and is imported by many suites (e.g. `test_udpfs_themes.py:24-25`, `test_neutrino_argv_budget.py:11-12`).
- **Fix:** Move the comment-aware extractor from test_udpfs_smap_fifo_tail.py into a shared module beside `hostmap.py` (extraction + compile/run with one failure mode: raise naming file and signature), have the suites import it, and delete the local copies; align the favourites `run()` helpers on the sanitiser flags while doing so.
- **Justification:** The shared module is not new code — it is one of the existing copies, promoted; the other copies are deleted. Net deletion.
- **Benefit:** One extraction semantics to trust and fix; a production function whose formatting trips the extractor behaves identically in every suite.
- **Effort:** L (corpus-wide; per-batch conversions are mechanical) / **Risk:** Low (a wrong extraction fails loudly at harness compile time in every current variant; each converted file must be re-run) / **Confidence:** High (READ — census executed; every cited definition read in its section)

### RIPTOPL-314 — The two i.LINK reset suites are one ~340-line harness stored twice, and the copies have already drifted
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_ilink_bd_reset.py:71-341` and `.github/scripts/test_ilink_bd_assault_reset.py:84-357` (STUBS, SCSI_HARNESS, LOGIN_HARNESS, `compile_and_run` in both)
- **Problem:** READ (full diff taken at the pin by section 02). The two files differ essentially in the driver directory they point at, one `bdm.h` stub field, the SCSI opcode spelling, an initializer arity, and case ordering; everything else — simulated drive, simulated clock, all LOGIN cases, all warm-up failure cases, the compile/run plumbing — is duplicated text. The drift is already visible in three places: (i) the assault copy filters a failing harness's stdout and synthesises a crash line for exit codes outside 0/1, the ilink copy returns every line; (ii) the ilink copy gates both harness runs on `if not failures:` (:330), the assault copy runs the SCSI harness unconditionally; (iii) the ilink copy hard-fails extraction if the budget defines are absent (:66-70), the assault copy deliberately tolerates their absence (:71-77). Three different answers to "what does this harness do on broken sources", in two copies of the same harness. This is the test-side mirror of the production duplication in RIPTOPL-047; it is filed against the tests.
- **Fix:** Keep one harness module (the assault copy's shape already generalises — it takes the driver directory as `sys.argv[1]`) and make both suites thin invocations with their per-driver deltas as parameters.
- **Justification:** Adds one shared harness file only by moving existing code into it; net deletion ≈ one whole suite (~330 lines).
- **Benefit:** A harness fix lands once, and the two drivers are thereafter tested by provably the same simulated drive.
- **Effort:** M / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-315 — `test_pad_mode_lock.py` and `test_pad_missed_read.py` each carry a full copy of the pad extraction, libpad stub and simulated-pad scaffolding
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_pad_mode_lock.py:40-47,55-63,65-91,92-143` and `.github/scripts/test_pad_missed_read.py:40-47,55-63,65-91,93-141` (same pieces, same order)
- **Problem:** READ. Measured at the pin, the two files share 187 distinct identical lines. Both extract the same functions from the same production file (`src/core/pad.cpp`) against near-identical simulated-freepad stubs, with the same `PadManager` construction, tick and frame helpers; only the simulated pad behaviour and the scenario `main()` differ. The copies have already drifted in small ways (the LIBPAD stubs differ in the `PAD_MMODE_*` defines each carries). A shared-module precedent exists in the same directory (`hostmap.py`).
- **Fix:** Extract the shared pad-test scaffolding (source reading/--ref, `function_text`, table/constants extraction, LIBPAD stub, harness preamble and frame helpers) into one helper module beside `hostmap.py`; leave each test with only its simulated-pad behaviour and scenarios.
- **Justification:** One new small module, justified by deleting the second copy of ~190 lines; the helper is the code both files already run.
- **Benefit:** One place to update when `pad.cpp` or the libpad stub changes; the two suites cannot drift into testing different extraction lists.
- **Effort:** M / **Risk:** Low / **Confidence:** High (READ — line-level comparison at the pin)

### RIPTOPL-316 — test_udpfs_network_hardening largely re-asserts, at string level, what test_network_parity_922 already proves by compiling
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_udpfs_network_hardening.py:34-75`; `.github/scripts/test_network_parity_922.py:59-143`
- **Problem:** READ. The two files assert the same production facts twice: DEV9 (hardening:36-38 vs parity:88 plus the compiled exercise at parity:59-87), the UDPFS loader gate (hardening:42-44 vs parity:90-92 — identical strings), IP conflict (hardening:62-75 vs parity:134-143 plus the compiled notice lifecycle), BDM chain ordering (overlapping entry points; production carries both names, verified). Genuinely unique to hardening: the `udpfsNicClaimed` retention checks (45-47), the `udpbdNicClaimed` partial-chain check (51-52), the ministack `exports.tab` check (67-69), and the "notice shown once" check (74). Everything else in the 79-line file is a weaker, string-only second copy of a compiled test's assertions, so any production rename must be updated in two places and the copies can drift into asserting different spellings of the same fact.
- **Fix:** Move hardening's four unique checks into test_network_parity_922.py and delete test_udpfs_network_hardening.py.
- **Benefit:** One home for the DEV9/IP-conflict contract; no drift between a compiled proof and its string-level shadow. Net deletion ≈ 60 lines.
- **Effort:** M / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-317 — The FatFs disk-image fixture is duplicated almost verbatim between the VMC and Ember image tests
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_vmc_fatfs_images.py:24-53,244-263`; `.github/scripts/test_ember_fatfs_images.py:52-77,195-216`
- **Problem:** READ. Both files carry the same fixture: a 131,072-sector in-memory disk, the disk_* / get_fattime / ff_mutex stubs, copying ff.c/ffunicode.c and the FatFs headers into a temp build dir, the same module_debug.h stub, and flipping FF_USE_MKFS to 1 in the copied ffconf.h (the VMC copy asserts the substitution landed at :252; the Ember copy at :203 does not). A FatFs pin bump or fixture fix must be made twice, in two slightly divergent copies (even the get_fattime day differs: 18 vs 19).
- **Fix:** Move the shared diskio/mutex/build-dir setup into one helper module under .github/scripts (the hostmap.py precedent) and have both tests use it; keep each test's own extraction and cases.
- **Justification:** Adds one shared helper file; removes ~50 duplicated lines. Net less.
- **Benefit:** One place to update when the vendored FatFs changes; the MKFS-substitution assertion then covers both suites.
- **Effort:** M / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-318 — Install_IGR / IGR_RequestReset installation behaviour is asserted twice, in test_igr_polling.py and test_igr_menu_no_room.py, against two different stub universes
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_igr_polling.py` MODE6_CHECKS (~:640-670), RESET_CHECKS (~:105-140), Install_IGR retry sequence (~:400-440); `.github/scripts/test_igr_menu_no_room.py` GATE_HARNESS Install_IGR tail checks (~:480-515)
- **Problem:** READ (overlap), INFERRED (drift risk). Both files compile padhook.c's Install_IGR and IGR_RequestReset and assert the same installation contract from different angles — first install starts worker+handler with no pending request, a pad reopen with the worker alive creates no second worker and keeps a pending request. The assertions are not textually identical (igr_polling stresses failure retry; igr_menu_no_room stresses the PC-mark lifetime), but the shared core is maintained in two harnesses with unrelated kernel stubs, so a deliberate change to Install_IGR semantics must be mirrored by hand in both.
- **Fix:** Keep the file-specific angles but state the shared installation contract in only one of them and cross-reference it from the other, deleting the duplicated assertions.
- **Benefit:** One place to update when Install_IGR's contract changes.
- **Effort:** S / **Risk:** Low / **Confidence:** Med (both harnesses READ; the boundary of "the same" assertion is a judgement call)

### RIPTOPL-319 — The nine-name BDM latch list is written twice in one file, so the text check and the executable check can drift apart
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_ripps2_backend_port.py:159-161` (latch tuple in the source-text loop) and `:178-179` (the same tuple re-declared for the compiled reset harness)
- **Problem:** READ. The same nine latch names (`iUSBModLoaded` … `udpbdNicClaimed`) are enumerated twice, a few lines apart, for two halves of one check: "each latch is named in `bdmIopWasReset`" (text) and "each latch is actually cleared when it runs" (compiled). If a tenth latch is added to only the first list, the text half covers it and the executable half silently does not — the stronger check is exactly the one that would be skipped.
- **Fix:** Declare the tuple once, above both uses, and have both loops consume it.
- **Benefit:** The executable reset coverage can no longer silently lag the textual coverage when the latch set grows.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-320 — Boot-theme splash assertions duplicated between test_yaopl_ripps2_b125_ports and test_boot_theme_wait
- **Category:** 2 (duplication)
- **Locations:** `.github/scripts/test_yaopl_ripps2_b125_ports.py:56-61`; `.github/scripts/test_boot_theme_wait.py:29-55`
- **Problem:** READ. Item 5 of the yaopl ports file re-asserts the subject of the dedicated boot-theme test: `THEME_HOLD_MAX_S 10`, `themeHoldMs() > 0` and the bounded-hold clock expression are the same facts test_boot_theme_wait.py checks more strongly — it compiles the real `themeHoldMs()`, regex-pins the full hold condition, and pins the setting's default, clamp and dialog wiring. Unique to the yaopl copy: the hold running `gGui.runFrameHook()` (:59-60) and the `thmSavedThemePending` internals (:62-67). The duplicated lines are pure string checks that must be maintained in both files whenever the splash code is reworded.
- **Fix:** Drop the duplicated splash assertions from the yaopl ports file, keeping its two unique checks — or move those two into test_boot_theme_wait.py and drop item 5 from the ports file entirely.
- **Benefit:** The boot-hold contract lives in the file that actually compiles the function it governs. Small net deletion.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-321 — Dead dual-layout (pre-C++ tree) fallbacks referencing files absent at the pin, in fourteen ported tests
- **Category:** 1 (dead code)
- **Locations:** `.github/scripts/test_screen_pos_per_mode.py:25-30`; `test_ripps2_boot_video_mode.py:22-23,37-45,126,129`; `test_game_count_refresh.py:15-16,28-31`; `test_smb_launch_name.py:107-108` (guarded variant — fails loudly if neither path exists); `test_console_ilink_id.py:8,10`; `test_ps1_duplicates.py:13,105-111`; `test_cheat_capacity.py:13-14,103,106`; `test_menu_crash_catcher.py:13-14,62,87-91`; `test_game_size_fallback.py:13-16`; `test_mmce_igr_strings.py:17-20`; `test_font_fit_bounds.py:8,70`; `test_history_filename_bounds.py:12`; `test_disc_key_semaphore.py:75`; `test_menu_text_fit.py:35-56` (the Open-PS2-Loader LAYOUT branch — see caveat). Absences verified at the pin (READ, `test -e`, sections 02/08/09): `src/opl.c`, `src/gui.c`, `src/themes.c`, `src/ethsupport.c`, `src/guigame.c`, `src/supportbase.c`, `src/cheatman.c`, `src/fntsys.c`, `src/OSDHistory.c`, `src/system.c`, `lng_tmpl/_base.yml`, `lng_tmpl/`, `lng_fork/` — none exist; the C++ paths all exist
- **Problem:** READ. These tests were ported from the pre-C++ tree (or, for menu_text_fit, shared with Open-PS2-Loader) and kept a runtime layout probe: at this pin the C branch can never be taken and every path it would read is absent, so the fallback is dead weight that misstates the tree the test pins. It is not a vacuous pass — if the `.cpp` file disappeared, the probe would flip and the test would crash reading a nonexistent fallback instead of reporting the rename — but it is unexecutable selection logic across fourteen files that every reader must reason through, in a corpus where the other files simply name the RiptOPL path directly. Caveat for the menu_text_fit location: that file's docstring says it "finds which tree it is in", i.e. the branch serves a copy of the file living in another repository; whether to keep cross-repo portability is that file's owner's call — flagged Med confidence for that one location. (The same shape in test_igr_menu_no_room.py was dissolved by section 01 because its docstring explicitly documents the sharing; it is recorded in checked-and-clean, not here.)
- **Fix:** In each file, read the C++ path directly and delete the C-layout alternative and its knock-on branches. For test_smb_launch_name.py, keep the loud "update this check" failure for a missing ethsupport source.
- **Benefit:** The suite states one layout — the one that exists — and future renames fail at the real path.
- **Effort:** M (S per file) / **Risk:** Low / **Confidence:** High for the thirteen in-repo locations (READ); Med for the menu_text_fit location (cross-repo sharing is documented in-file)

### RIPTOPL-322 — Dead test helpers: `strip_comments` and `function_body` in the SMB2 logon parity suite are defined and never called
- **Category:** 1 (dead code)
- **Locations:** `.github/scripts/test_smb2_logon_parity.py:62-95` (`strip_comments` :62-87, `function_body` :90-95)
- **Problem:** READ. Each name occurs exactly once in the file — its definition (verified by AST/name count). Nothing in the file calls them, and no other file imports them (the suite is a standalone script; same-named helpers in other test files are independent copies). They are leftovers of a source-scraping approach the suite no longer uses — it now compiles and runs both real clients against a fake server. 34 lines of unexecuted C-comment-stripping logic sit in the header of a 702-line suite, where a reader reasonably assumes they participate in the checks.
- **Fix:** Delete both functions.
- **Benefit:** Less code to read in a suite whose value is its live protocol checks; removes a misleading impression that source text is being inspected.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-323 — test_mx4sio_retry.py runs the whole test_mx4sio_write_acceptance.py suite as its last step, so ps2build executes the acceptance suite twice
- **Category:** 7 (build/CI)
- **Locations:** `.github/scripts/test_mx4sio_retry.py:251`; `.github/scripts/test_mx4sio_write_acceptance.py:1-258` (whole file); `tools/run_tests.sh` (glob runs both files); `.github/workflows/flavours.yml:53` (runs retry only)
- **Problem:** READ. test_mx4sio_retry.py:251 ends `main()` with a `subprocess.run` of the acceptance suite. Under tools/run_tests.sh (ps2build) both files are top-level tests, so the acceptance suite — its ISR harness, two mutant rebuild-and-run cycles, and the card-info harness, each with its own compiles — executes twice per test run. Under flavours.yml the nested call is the acceptance suite's *only* coverage (flavours names retry at :53, never write_acceptance), so the coupling is load-bearing in one workflow and pure duplication in the other; neither file says so.
- **Fix:** Drop the nested invocation from test_mx4sio_retry.py and name test_mx4sio_write_acceptance.py explicitly in flavours.yml next to test_mx4sio_retry.py, so each suite runs exactly once in each workflow.
- **Justification:** Adds one workflow line; removes one subprocess call. Net less.
- **Benefit:** Halves acceptance-suite CI time under ps2build; coverage in flavours becomes visible instead of hidden inside another test.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-324 — `test_bdm_scan_retry.py` runs twice in one workflow (and a third time in the other CI workflow)
- **Category:** 7 (build/CI/repo clutter)
- **Locations:** `.github/workflows/flavours.yml:57` (migration-parity-host-tests job) and `:310` (bdm-host-tests job); third execution via `.github/workflows/ps2build.yml:70-71` running `tools/run_tests.sh -k`, whose glob includes the file
- **Problem:** READ. test_bdm_scan_retry.py is the only test file appearing twice in flavours.yml (verified by counting every `test_*.py` mention in the file: it is the sole count of 2). Both flavours jobs trigger on the same events, so every PR and rebuild/main push compiles and runs its two C++ harnesses twice within one workflow, plus once more in ps2build. The duplication buys nothing: the bdm-host-tests job is the suite's themed home and already runs it alongside the other BDM suites.
- **Fix:** Remove the line from the migration-parity list at flavours.yml:57, keeping the bdm-host-tests entry at :310.
- **Benefit:** One full compile-and-run of a 562-line suite saved per flavours run; coverage unchanged (two executions remain across the two workflows).
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-325 — Ten harness-execution call sites in nine suites run their compiled harness with no timeout, so a hung harness hangs the CI job instead of failing
- **Category:** 7 (build/CI)
- **Locations:** `.github/scripts/test_udpfs_library_launch.py:29-30`; `test_udpbd_read_only.py:156-160`; `test_ps2ips_recv_tail.py:260-264`; `test_ra_sender.py:235-238`; `test_ingame_freeze_diag.py:200,221`; `test_bdm_cache.py:216-220`; `test_ripps2_drive_status.py:40-41`; `test_ripps2_game_manage.py:174-176`; `test_font_flush_frame_lock.py:202-203`; `test_ps1_save_icons.py:168,178`
- **Problem:** READ. These `subprocess.run` calls that execute the compiled test binary (in several files the compile too) pass no `timeout=`, unlike the rest of their batch (e.g. test_mmce_read_recovery.py timeout=60/120, test_mx4sio_retry.py timeout=30/120, test_vmc_fatfs_images.py timeout=90). The risk is concrete for test_ps1_save_icons.py, whose own cases include a corrupt/cyclic save chain that "must not cause an unbounded traversal": if the parser regressed to looping, the test would hang until the GitHub job timeout rather than failing. Same shape for test_ps2ips_recv_tail.py, which exercises receive loops.
- **Fix:** Add a bounded timeout (the corpus's existing 10–90 s range) to the harness-execution `subprocess.run` calls in these files, matching their sibling tests.
- **Justification:** Adds one argument per call site; nothing new.
- **Benefit:** A hang regression becomes a fast, attributable test failure instead of a burned CI job.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-326 — Undocumented sys.argv source overrides in seven tests silently substitute non-pin code
- **Category:** 7 (build/CI/repo clutter — hidden test-mode switches)
- **Locations:** `.github/scripts/test_ra_chunk_integrity.py:8`; `test_pad_hook_cache.py:15`; `test_genvmc_recovery.py:8`; `test_page_animation.py:9`; `test_zso_probe_fd.py:9-10`; `test_font_fit_bounds.py:9-10`; `test_history_filename_bounds.py:13-14`
- **Problem:** READ. Any first command-line argument silently replaces the production source under test: a file path in ra_chunk_integrity, pad_hook_cache, genvmc_recovery and page_animation (`Path(sys.argv[1]) if len(sys.argv) > 1`), and a git ref fed to `git show` in zso_probe_fd, font_fit_bounds and history_filename_bounds. None of these seven files mentions the override in its docstring. The corpus shows the documented form of the same mechanism: test_ul_scan_integrity.py:6,17 requires the literal `--baseline` flag plus exactly one path argument, and test_cheat_capacity.py:3,17-18 documents its optional git ref. As written, a wrapper, a future run_tests.sh change, or a maintainer's stray argument makes the suite report PASS for code that is not at the pin, with nothing in the output saying so. One extra wart in the same lines: zso_probe_fd.py:10 runs `git show` without `cwd=root` (the other two pass it), so its ref resolves in whatever repository the caller's working directory belongs to.
- **Fix:** Gate each override behind an explicit named flag in the ul_scan form (`--baseline <arg>`, exact arity), document it in the docstring, and give zso_probe_fd's `git show` the same `cwd=root` the other two already pass.
- **Justification:** A flag check per file; the baseline-comparison ability is kept, only made visible and opt-in.
- **Benefit:** A green run always means the pinned tree was tested, unless the invocation visibly says otherwise.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-327 — test_io_queue_trims: the deferred-update harness macro-aliases `ioPutRequest` to `ioPutRequestUnlessWaiting`, so the APPS call site in `menuDeferredUpdate` cannot regress to the un-coalesced spelling without failing anything
- **Category:** 7 (build/CI)
- **Locations:** `.github/scripts/test_io_queue_trims.py:244-245` (stub + alias), `:64-69` (spelling count guard, covering only the bdm files concatenated at :44); production call site at `src/app/opl.cpp:900`
- **Problem:** READ. The deferred harness compiles the real `menuDeferredUpdate` against a stub in which `#define ioPutRequest ioPutRequestUnlessWaiting` makes, in the file's own words, "either spelling count as queuing". For the harness's stated question the spelling genuinely does not matter — but the production call at `opl.cpp:900` was specifically changed to the coalescing spelling, with a long comment there explaining that a waiting APPS pass already reads `gShouldAppsUpdate` so a second one must not be queued behind it. That property is untestable through this harness, and the file's only spelling guard counts calls in bdmsupport.cpp/bdmdevices.cpp/bdmboot.cpp — `opl.cpp` is read into a separate variable at :45 and never counted. A one-word regression at `opl.cpp:900` back to plain `ioPutRequest` passes every check in the file that introduced coalescing.
- **Fix:** Extend the existing count guard to the `opl.cpp` text it already loads (require the `menuDeferredUpdate` APPS request to use `ioPutRequestUnlessWaiting`), or drop the macro alias in the deferred harness and give the stub both spellings with distinct counters.
- **Justification:** The first form adds one counted string to a guard that already exists for the sibling call sites; no new mechanism.
- **Benefit:** The duplicate-APPS-pass storm the coalescing fix removed cannot return via the one call site the current guards do not reach.
- **Effort:** S / **Risk:** Low / **Confidence:** High on the alias and the guard's scope (READ); Med on impact (INFERRED — the behavioural difference needs two device attaches in one waiting window, per the production comment)

### RIPTOPL-328 — test_hires_thread_priority exercises only rows 6–11 as "the multi-pass ones"; the four appended multi-pass rows (16–19) get no coverage in the test at all
- **Category:** 8 (half-finished/redundant features — stale fixture, partial vacuous pass)
- **Locations:** `.github/scripts/test_hires_thread_priority.py:118-119` (comment + loop bound), `:128-129`, `:142`; production table at `src/gfx/renderman.cpp:52`ff (rows 16–19 at :102,110-112 are multi-pass exactly like rows 6–11); production mechanism at `src/gfx/renderman.cpp:246` (`hires_ = (rm_mode_table[vmode_].passes > 1)`)
- **Problem:** READ. The test's stated purpose is that leaving *a* multi-pass mode restores the entering thread's priority. Its row set is a hard-coded range frozen before the table's tail appends: 4 of the 10 multi-pass rows — the 448-line HIRES row and all three lineBlend HIRES rows, precisely the newest modes — are never entered or left in any scenario. A regression in `Renderer::setMode`/`end` that mishandles those rows passes this suite silently. The table's own comments (renderman.cpp:80-81,103-107) say rows are only ever appended at the tail because `$VMode` is a stored index, so a fixed 6–11 range is guaranteed to drift again. Companion drift in the same file: RIPTOPL-329.
- **Fix:** Derive the exercised hires set from the extracted production table itself (every row with `passes > 1`) instead of the literal 6–11 range, in all three loops; delete or correct the "Rows 6..11" comment.
- **Benefit:** All 10 current multi-pass rows, and any future appended row, get the priority-restore check the suite advertises.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-329 — NUM_RM_VMODES is hard-coded as 20 in two renderman tests while the third extracts it from the source
- **Category:** 2 (duplication — stale duplicated constant)
- **Locations:** `.github/scripts/test_vmode_safe_area.py:58` (drives the all-rows loop at :80); `.github/scripts/test_hires_thread_priority.py:65` (drives the loop at :142); the sibling that does it right: `.github/scripts/test_hires_line_blend.py:45-46`; production: `src/gfx/renderman.cpp:24` (`NUM_RM_VMODES = 20` — matches today)
- **Problem:** READ. Both hard-coded copies are correct at the pin, but the constant is the loop bound for "check every mode" claims. When the table next grows by tail-append (its documented growth pattern), both tests will silently iterate a prefix of the table and still report "ok", while test_hires_line_blend — testing the same table — keeps covering all rows. The inconsistency proves the extraction pattern was available and not applied.
- **Fix:** Extract the constant from `src/gfx/renderman.cpp` in both files, exactly as test_hires_line_blend.py:45 already does; delete the two literals.
- **Benefit:** The "every mode" loops stay every-mode when the table grows. Complements RIPTOPL-328 (the row-range version of the same drift, in one of the same files).
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ for the duplication and today's match; the future under-coverage follows mechanically — INFERRED)

### RIPTOPL-330 — test_ilink_sbp2_completion.py hard-codes the production login budget and carries a dead placeholder replace
- **Category:** 7 (build/CI)
- **Locations:** `.github/scripts/test_ilink_sbp2_completion.py:311-313` (dead `.replace('@LOGIN_BUDGET@', '5000000')` + prepended literal define); production value at `modules/ilink/IEEE1394_bd/src/sbp2_driver.c:142` and `modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:141` (`SBP2_LOGIN_BUDGET_US (5 * 1000 * 1000)`)
- **Problem:** READ. Two defects in three lines. (i) The `@LOGIN_BUDGET@` token occurs nowhere in the harness text — the only placeholders are `@STATE@` and `@FUNCTIONS@` (:36, :110) — so the replace at :312 is a no-op left behind by an earlier shape of the file. (ii) The budget the extracted `ieee1394_SendManagementORB` is tested against is a hand-copied literal `5000000`, not the define extracted from the driver under test. It happens to equal the production value at this pin, so the suite passes — but if either driver's budget changes, this suite keeps certifying the old 5 s behaviour for both drivers. The sibling suite shows the correct pattern: test_ilink_bd_reset.py:66 extracts `SBP2_LOGIN_BUDGET_US` from the driver source itself.
- **Fix:** Delete the dead replace, and extract the `SBP2_LOGIN_BUDGET_US` define from each driver (as the sibling suite does) instead of prepending a literal.
- **Benefit:** A future budget change is tested at its new value instead of being silently certified at the old one.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-331 — test_catalog_limits.py hard-codes HTTP_STREAM_ERR_TRUNC as -7 instead of extracting it like every other production constant it uses
- **Category:** 8 (half-finished/redundant features — stale copied value)
- **Locations:** `tools/pc/http/tests/test_catalog_limits.py:37` (`#define HTTP_STREAM_ERR_TRUNC -7` in the generated harness preamble); production value at `modules/network/common/httpclient.h:68` (also -7 today)
- **Problem:** READ. The file's whole method is to build the harness from *live* production text: the catalog limits and game constants are regex-extracted from `include/supportbase.h` / `include/httpsupport.h` at :40-43, and `httpParseCatalog` itself is extracted verbatim from `src/sources/httpsupport.cpp` at :48. The one exception is the error code the extracted function stores into `httpLastError` on the over-limit path: the harness predefines it as the literal -7, and the scenario-2 assertion compares the extracted code against the harness's own copy of the constant. The values agree today, so nothing is currently broken — but if production renumbers the `HTTP_STREAM_ERR_*` values, this test stays green while verifying the wrong number, because the extracted function is compiled against the copy, never against the header it ships with.
- **Fix:** Derive the value the same way the neighbouring constants are derived — extract the `#define HTTP_STREAM_ERR_TRUNC` line from `modules/network/common/httpclient.h` into the generated preamble (failing loudly via the existing `extract()` RuntimeError if it disappears), and delete the hard-coded literal.
- **Justification:** Adds one extraction line and removes one literal; the `extract()` helper already exists in the file.
- **Benefit:** The over-limit assertion verifies the production error code itself, so a future renumber is caught instead of silently ratified.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ; divergence latent — values currently match)

### RIPTOPL-332 — Assertions inside possibly-empty iteration, with no count guard, in two tests
- **Category:** 6 (bugs/security — checks that report as pass when their subject disappears)
- **Locations:** `.github/scripts/test_mmce_igr_strings.py:71-75`; `.github/scripts/test_ds34usb_report_transfer.py:109-111`
- **Problem:** READ; both instances verified non-vacuous today but structurally unguarded. (1) test_mmce_igr_strings.py runs the real lang-compiler pipeline for Russian/German/Japanese/Spanish only `if lang in words:` — `words` is built solely from overlay files that exist in `lng/fork/`. All four overlays exist at the pin, so the checks run today; but deleting or renaming one overlay removes its compiled-pipeline check silently — the final PASS line merely prints a smaller overlay count. (2) test_ds34usb_report_transfer.py asserts `data_result` gating for every `readReport(data_buf[port], port);` match found by `re.finditer`, with no assertion that any match exists. Production contains exactly two such calls today (modules/pademu/ds34usb.c:488,504). If the call is renamed or restructured, the loop body never runs and that sub-check passes vacuously. The corpus knows the guard pattern: the same ds34 file asserts `checked > 10` after its CancelAlarm census (:70), and test_yaopl_ripps2_b125_ports.py:28 asserts its findall result is non-empty before iterating.
- **Fix:** In (1), require the four named languages to be present before compiling them. In (2), materialise the finditer matches and assert the expected count (2) before the per-call assertions.
- **Justification:** One guard line each; the existing tests are made non-optional, nothing new is tested.
- **Benefit:** Deleting an overlay or renaming a call site fails the suite instead of quietly shrinking it.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ; present-day counts verified against production)

### RIPTOPL-333 — test_about_credits_layout.py prints PASS for trimmed UI_BREAK rows and a kept row count, but never counts either
- **Category:** 6 (bugs/security)
- **Locations:** `.github/scripts/test_about_credits_layout.py:5-8` (docstring item 3), `:22-29` (the only assertions in the first half), `:31` (print claiming "trimmed spacing"), `:37-41` (the only UI_BREAK use in the file), `:42` (print claiming "About keeps its row count"); production slice at `src/ui/dialogs.cpp:2091` to its terminator (15 UI_BREAK entries at the pin, counted READ)
- **Problem:** READ. Nothing in the file counts `UI_BREAK` entries, total `diaAbout` items, or dialog height. Re-adding excess `{UI_BREAK}` rows anywhere in `diaAbout` — the exact regression docstring item 3 says the test guards, in a fixed non-scrolling dialog — leaves every assertion green while both PASS lines keep asserting the trimmed/kept-height outcome. The one structural check that exists (:41) proves only that Network update and `icyson55` share a row; it is insensitive to rows added or removed anywhere else. Incidental, same file: `import sys` at :10 is never used (AST-checked).
- **Fix:** Assert the exact UI_BREAK count (15 at the pin) and/or the exact diaAbout item count in the sliced array, and drop the unused `sys` import in the same pass.
- **Justification:** Adds one integer assertion; no new mechanism.
- **Benefit:** The vertical-overflow guard the file advertises becomes a guard that can fail.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-334 — test_sfx_oneshot_cut.py's coalesce assertion can be satisfied by the comment inside the captured branch alone
- **Category:** 6 (bugs/security — comment stands in for code in a presence check)
- **Locations:** `.github/scripts/test_sfx_oneshot_cut.py:17` (branch capture regex), `:25-26` (bare-token check); production token sites at `src/core/sfx.cpp:399` (the comment `// see SFX_ONESHOT_COALESCE_MS` sits *after* the opening brace, inside the captured group) and `:402` (the real comparison); definition at `src/core/sfx.cpp:79`
- **Problem:** READ, verified by section 10 re-running the file's own extraction: the captured branch group contains the token twice — once in the :399 comment and once in the :402 comparison — and deleting the comparison line from the captured text leaves the token present, so the assertion still passes. If the coalesce comparison were removed (or replaced by a literal) while the branch comment stayed, the test would keep printing "same-frame double raise coalesced" for code that no longer coalesces. The corpus shows the correct pattern: test_smap_tx_base.py:15-20 strips comments precisely so a commented copy cannot stand in for live code; this file does no stripping.
- **Fix:** Strip comments from the extracted `play()` body before the branch checks (the smap file's approach), or assert the comparison expression (`< SFX_ONESHOT_COALESCE_MS)`) instead of the bare token.
- **Benefit:** The #575 double-raise coalesce guard can actually fail when the coalesce stops being code.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-335 — test_pad_pattern_bounds.py's only ASan-isolated overread case is gated on an argument no CI invocation ever passes
- **Category:** 6 (bugs/security — a vacuous branch inside an otherwise executed test)
- **Locations:** `.github/scripts/test_pad_pattern_bounds.py:22-26` (the `if (argc>1)` heap case in the generated C program), `:40` (unconditional PASS line), `:50` (exe run with `*sys.argv[2:]`), `:8` (`sys.argv[1]` consumed as a source-path override); invocations at `tools/run_tests.sh:9` (no arguments) and `.github/workflows/flavours.yml:38` (no arguments)
- **Problem:** READ, verified by section 10 running the file. The generated C program's heap case — a pattern search over a `malloc(sizeof(u32))` allocation, the only case where a one-word overread crosses an allocation boundary and ASan's heap redzone can catch an overread whose extra word does not complete a match — runs only when the compiled exe receives an argument. Both sanctioned invocations pass none, so the branch is skipped while the program still prints its PASS line claiming the overread checks ran. The remaining short-range cases cannot substitute (their reads stay inside the stack array, so only a return-value mismatch is detectable). The gate is also nearly unreachable by hand: passing one extra argument makes the Python wrapper treat it as the source path and crash; the heap case runs only with an explicit source path *and* a second dummy argument. This is not RIPTOPL-291's mechanism — gcc is present in CI; the branch is dead because of argv plumbing. (The `sys.argv[1]` override here is documented-in-code by position; RIPTOPL-326 covers the undocumented instances.)
- **Fix:** Drop the `argc>1` gate so the heap case always runs (or have the wrapper pass one fixed trigger argument to the exe at :50). Keep the `sys.argv[1]` source override for the Python side only.
- **Benefit:** The suite's only sanitizer proof against a non-matching overread actually executes in CI, and the PASS line stops claiming a check that did not run.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ + executed both invocation shapes)

### RIPTOPL-336 — test_audit_translations.py exercises only one of the tool's two --strict error classes; the unknown-overlay-label error is untested by the tool's only test
- **Category:** 6 (bugs/security — coverage gap in a strict gate)
- **Locations:** `.github/scripts/test_audit_translations.py:26-46` (all three scenarios mutate the same key into a printf-token mismatch); the untested class at `tools/audit_translations.py:70` (`overlay has unknown label`), documented as a --strict failure at `tools/audit_translations.py:8-9`
- **Problem:** READ. This file is the sole test that exercises `tools/audit_translations.py` at all (repo grep: only this file and the tool itself reference it). Its three scenarios thoroughly cover the format-token error, but no scenario adds an overlay label the base does not have, so the tool's other --strict error — an overlay key absent from `lng/tmpl/_base.yml`, which would otherwise be silently dropped from the merged language — can break or be removed without any test failing.
- **Fix:** Add one scenario to the existing temp-tree flow: inject a bogus label into the Swedish overlay and assert `--strict` exits 1 naming it as an overlay error, mirroring the format scenarios.
- **Justification:** Adds one scenario reusing the existing `strict()` helper and fixture tree; no new machinery.
- **Benefit:** Both documented --strict failure modes of the translation gate are pinned, not just the format one.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ; sole-test status grep-verified)

### RIPTOPL-337 — test_conformance.py locks in the empty-catalog run as a clean pass with "0 skipped", although the run skips every range check
- **Category:** 8 (half-finished/redundant features — a skip that reports as a pass)
- **Locations:** `tools/pc/http/tests/test_conformance.py:40-46` (`test_empty_catalog_still_passes`); the behaviour it locks in at `tools/pc/http/http_conformance.py:219-222`
- **Problem:** READ. For an empty catalog the tool prints "Range checks skipped." and "An empty catalog is valid, but it proves nothing about range support.", then returns 0 — and its summary line is built from counters that this path never increments (`res.skipped` is only bumped via `res.skip()`, not on the empty-catalog early return). The test does not merely tolerate this: it asserts the exact summary string `"1 passed, 0 failed, 0 skipped"` and result 0, so the miscounted skip accounting is now the tested contract. A CI reader sees a fully-passing, nothing-skipped conformance run for a server about whose range support nothing was proven — the tool's own prose says so, its counters say the opposite, and the test sides with the counters.
- **Fix:** Make the empty-catalog path count the range section as skipped (route it through `res.skip()` like the other skips) and update the test to expect the honest summary, while keeping result 0 for the valid-empty-catalog decision itself.
- **Benefit:** The conformance summary can no longer present a run that exercised no range reads as a skip-free pass; the test stops encoding the miscount as correct.
- **Effort:** S / **Risk:** Low / **Confidence:** High on the test assertion and counter mechanism (READ); Med on calling result 0 itself wrong — the valid-empty decision is deliberate and is not challenged, only the 0-skipped accounting

### RIPTOPL-338 — `test_pops_udpfs.py` can only run if the gitignored `build/` directory already exists
- **Category:** 7 (build/CI)
- **Locations:** `.github/scripts/test_pops_udpfs.py:171` (`tempfile.TemporaryDirectory(dir=ROOT / 'build')`); `.gitignore:110` (`/build/`)
- **Problem:** READ (mechanism); the CI-masking explanation is INFERRED. `build/` is gitignored and untracked (verified: `git check-ignore`, empty `git ls-files build`), so a fresh checkout has no such directory and `TemporaryDirectory(dir=…)` raises `FileNotFoundError` before a single check runs — the suite fails for an environmental reason unrelated to anything it tests. In the current CI sequences the failure is masked (earlier steps in the same jobs create `build/`). It is the only file in its batch that anchors its scratch space inside the repo tree; every other batch file uses a default-location `TemporaryDirectory()`. Standalone execution — the way run_tests.sh and a developer invoke it — is exactly the case that breaks. Contrast RIPTOPL-339 (four tests that *create* an in-tree temp dir rather than requiring one).
- **Fix:** Drop the `dir=` argument (use the system temp location like the other suites), or create the directory before use if a repo-local temp is genuinely required.
- **Benefit:** The suite runs identically standalone, in CI, and in a fresh clone.
- **Effort:** S / **Risk:** Low / **Confidence:** High for the mechanism (READ); Med for the CI-masking explanation (INFERRED)

### RIPTOPL-339 — Four tests create and use an in-tree build/test-tmp directory instead of the system temp dir
- **Category:** 7 (build/CI/repo clutter)
- **Locations:** `.github/scripts/test_network_parity_922.py:22-24`; `test_ul_scan_integrity.py:119-121`; `test_udpfs_serialization.py:117-119`; `test_build_driver.py:13-14,22,81`
- **Problem:** READ. These four point `tempfile.TemporaryDirectory(dir=...)` at `ROOT / "build/test-tmp"`, creating that directory inside the checkout (the `mkdir(parents=True, exist_ok=True)` calls leave the empty parent behind after every run). test_build_driver.py does the `mkdir` at import time, so merely collecting or importing the module writes into the tree. The other 66 files in their batch use the plain system-temp `TemporaryDirectory()`. Mitigating fact (READ): `build/` is gitignored, so the directory never dirties `git status`. Remaining costs: the four tests fail on a read-only or non-writable checkout where their siblings pass, and an in-tree scratch dir is one `.gitignore` edit away from becoming committed clutter. Contrast RIPTOPL-338.
- **Fix:** Drop the `dir=` argument (and test_build_driver's import-time mkdir) and use the system temp dir like the rest of the corpus — or, if a noexec `/tmp` on some runner is the unstated reason, write that reason in one comment in each file so the divergence is a decision, not an accident.
- **Benefit:** Tests become read-only with respect to the checkout; one temp-dir convention for the whole corpus.
- **Effort:** S / **Risk:** Low / **Confidence:** Med (mechanism READ; the read-only-checkout consequence is INFERRED, and a deliberate noexec-/tmp rationale cannot be excluded from the files alone)

### RIPTOPL-340 — test_smb1_vmc_share is the corpus's only cwd-relative test (in its batch, grep-verified)
- **Category:** 7 (build/CI/repo clutter — invocation fragility)
- **Locations:** `.github/scripts/test_smb1_vmc_share.py:25-27,30,37,87`
- **Problem:** READ. Its `read()` helper opens the path it is given bare — `open(path, ...)` with no repo-root anchor — and the call sites pass `modules/iopcore/cdvdman/smb.c`, `modules/network/smbman-ra/smb.c` and `Makefile`. Every other file in its batch anchors reads at `Path(__file__).resolve().parents[2]` (or an equivalent absolute ROOT). The test passes in CI only because both invoking routes happen to set the working directory to the repo root. Invoked by absolute path from any other directory — the natural way to run one test while debugging — it dies with FileNotFoundError on a healthy tree, reporting a failure that is not in the code under test.
- **Fix:** Anchor `read()` at the same parents[2] ROOT the rest of the corpus uses.
- **Benefit:** The test's result depends on the tree, not on the caller's shell directory.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ; uniqueness grep-verified within the batch)

### RIPTOPL-341 — test_neutrino_bsdfs success message claims 28 preflight scenarios; the harness runs 30
- **Category:** 7 (build/CI/repo clutter — stale expected-output string)
- **Locations:** `.github/scripts/test_neutrino_bsdfs.py:100-131` (30 `scenario(...)` call sites, counted READ); `:209` (the success line claiming 28)
- **Problem:** READ. The printed tally — the only human-facing summary of what the suite covered, and the line that lands in CI logs — understates the scenario count by 2 (the argv half, 8, is accurate). The count was not updated when the last `--b`-scope scenarios were added. Pass/fail is unaffected (the C harness returns `fails ? 1 : 0`), so this is a reporting-integrity defect only — but in a corpus whose other tallies check out exactly (test_handoff_probe.py "9 device scenarios" = 9 calls; test_cheat_load.py "15 scenarios" = 15 calls, both verified), this is the one stale count.
- **Fix:** Drop the hard-coded numbers from the success line, or compute the scenario count in the harness and print that.
- **Benefit:** CI log summaries state the coverage that actually ran.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-342 — test_mbedtls_vendor's docstring still says five pinned roots; seven are pinned and asserted
- **Category:** 7 (build/CI/repo clutter — stale fixture text in a security-sensitive test)
- **Locations:** `.github/scripts/test_mbedtls_vendor.py:5-6` (against :20-28, :113-122, :140)
- **Problem:** READ. The docstring states the test operates on the basis that "only five pinned roots are trusted". The file itself pins seven roots, asserts the on-disk set equals those seven (:118), hashes each against the CA-roots README (:120-122), its own later comment says "Seven pinned roots" (:113), and its PASS line says "seven pinned roots" (:140). The docstring is the only place still carrying the old count — in the one test whose entire subject is which TLS roots the RA/update builds trust, a reader who trusts the header miscounts the trust set by two.
- **Fix:** Correct the docstring count to seven (or drop the number and let PINNED speak).
- **Benefit:** The trust-root count is consistent everywhere it is stated.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-343 — Production `ra_watch.h` header comment is stale: it says the raudp header is 162 bytes; the field table sums to 184 (the fixture is right)
- **Category:** 7 (build/CI/repo clutter) — production-side, routed from this round's fixture comparison; not a test defect
- **Locations:** `modules/network/common/ra_watch.h:34` (the 162-byte comment); the correct figure in the fixture at `.github/scripts/fixtures/xerabora-alpha16/ra_watch.h:38` (184 bytes)
- **Problem:** READ (section 11, both sides diffed line by line). Summing the production field table (tag 4 + labels 43 + widths 98 + separators 38 + trailing space 1) gives 184 — the fixture's comment is right and the production comment is stale. `RA_SNAP_HEAD_BYTES` itself is 191 on both sides, so no behaviour differs; the harm is a wrong size stated in the header of a wire-format file, where a future editor may trust it.
- **Fix:** Correct the comment at `ra_watch.h:34` to 184 bytes.
- **Benefit:** The wire-format header stops misstating its own size.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ — field table summed at the pin)

---

## EXTENSION of RIPTOPL-291 (round 7) — ten further conditional skip-to-pass instances, nine files

RIPTOPL-291 filed the compiler-absence skip-to-pass for three suites (test_iso9660_records.py, test_release_editions.py, test_install_ports.py) and proposed aligning them to the corpus's stricter pattern (no compiler → exit 1, as in test_smb2_negotiate_retry.py). The full line-read finds the same family in **nine more files**, across five distinct triggers. This is recorded as an extension of -291 (no new numbers), so its fix, when applied, covers the whole family:

- **Compiler absence (same trigger as -291):** `test_zso_validation.py:164-169` (with no gcc/cc, `main()` returns 0 whenever the pure-Python call-site scan passes, while every compiled check — refused headers, sliced reads, corrupt index entries — never runs); `test_pad_handoff.py:115-129` (`skipUnless(which('cc'))` guards the file's only compiled test); `test_bdm_index.py:119-128` (all 14 draw cases skipped with failures empty and exit 0 — and its success line at :161 then prints the case count unconditionally, claiming the 14 cases ran).
- **Tool/package absence (never installed by any workflow):** `test_ps2build_sdk_watch.py:582-583` (the fork-filtering jq program — the security-relevant half of the test — is `skipTest`-ed when the jq binary is absent; coordinator-verified; round 7 noted these skips as "same family, lower stakes" without filing them); `test_ripps2_gui_port.py:192-196` (the FreeType glyph-weight behavioural check prints SKIPPED and returns normally when `pkg-config freetype2` fails; no workflow installs FreeType dev files; unconditional success print at :312); `test_ripps2_art_port.py:128-137` (**strongest instance:** the entire JPG-decoder suite — baseline/progressive/grayscale decode, size-box shrinking, non-JPG refusal, abort handling — runs only when both a `jpeglib.h` probe and `from PIL import Image` succeed, and *no workflow installs either dependency*: flavours.yml:25 installs PyYAML only, ps2build installs nothing. On a stock ubuntu-24.04 runner the decoder half plausibly never executes in CI at all — Confidence Med on the base-image contents, High on the mechanism and the no-install facts, both READ).
- **Interpreter feature absence:** `test_neutrino_cheats.py:19-22,206-210,223,253` (on Python < 3.11, `tomllib` is None and both TOML-parse assertions — the only checks that validate what Neutrino will actually read — degrade to no-ops with no failure and no printed notice; the suite still prints its full success line. Not triggered in current CI, whose runners are Python 3.12; triggered by run_tests.sh on any pre-3.11 developer host, the population run_tests.sh exists for).
- **File-existence guard:** `test_usbd_pad_pool.py:90-98` (the raudp MTU checks are wrapped in `if os.path.exists(raudp):` — if raudp.c is renamed, moved or deleted, precisely the event the check exists to catch the consequences of, the whole section is skipped with no failure and no skip message, while the file's own `one()` helper at :59-68 fails loud for every other missing pattern; a nested `'lwip_sendto' in src` guard at :95-98 has the same shape).
- **euid:** `test_file_manager.py:312-321` (read-only-folder copy check guarded by `if os.geteuid() != 0:` with only a printed SKIP in the else) and `:340-342` (the partial-move check guarded by the same condition with **no else at all** — when the suite runs as root, the normal case in containers, there is not even a SKIP line; `failures` stays empty and the file prints "All File Manager checks passed").

Fix shape (unchanged from -291, extended): make each absence a failure in CI in the corpus's own stricter pattern, or count the skip as an incomplete run that run_tests.sh does not tally as "ok". The instances differ in trigger, not in kind: in every case the suite's verdict does not depend on whether its central checks executed.

---

## Checked and clean (corpus-level negatives, stated once)

- **Fixtures are NOT stale — both families, line-verified (section 11).** All 63 selfupdate fixture lines match the production parser's exact format (`src/platform/selfupdate.c:74-119`: 64 hex, two spaces, name from p+66, one `.zip` revision per file — llvm 4256, the other three 4257, a legitimate per-channel lag since each fixture's expectation is derived from its own text). Every xeRAbora define/struct/field-width is identical to production; production only *adds* material the snapshot parser never uses. The one divergence found runs the other way and is filed as RIPTOPL-343.
- **No orphan tests.** `tools/run_tests.sh:7` globs `.github/scripts/test_*.py` and runs each by exit code; ps2build.yml:71 runs it with `-k`. Every corpus file executes in CI (several also in flavours.yml). Files absent from flavours.yml are not unrun — dissolved in sections 01, 04, 08, 09 with the same evidence round 7 established.
- **No test-name collisions.** AST census: no test-function name is defined in 3+ files; all 278 `test_*.py` parse cleanly (one file, test_ps1_scan_recovery.py, carries a UTF-8 BOM — harmless: the interpreter and unittest both handle it; an early census parse failure on it was the census's own decoding, dissolved).
- **No assert-nothing bodies, no swallowed assertions, no exit-code loss beyond the filed instances.** Across 282 full reads, every `except …: pass/continue` resolved to an expected-exception probe, a TLS negative-case thread, or a timeout-to-failure converter; every `subprocess.run` outside RIPTOPL-308/-309/-325 either uses `check=True` or branches on `returncode`. Round 7's pattern-scan negatives are confirmed at line level, with the single narrowing recorded at RIPTOPL-301.
- **Bare-`assert` fragility is latent-only outside RIPTOPL-310.** No workflow or runner passes `-O`/`PYTHONOPTIMIZE` or defines `NDEBUG` (grep-verified in multiple sections); the C-assert harnesses are live in CI as invoked. Dissolved per batch rather than filed per file.
- **The documented dual-layout instance stands dissolved:** test_igr_menu_no_room.py's Open-PS2-Loader LAYOUT branch (:36-42) is documented in its own docstring (:3-4) as a script shared with OPL that selects its tree by file presence — intentional portability, distinguished from RIPTOPL-321's undocumented instances.
- **test_catalog.c's 512-entry dedup ceiling** (tools/pc/http/tests/test_catalog.c:30,89-93) was re-verified present at the pin; it is already RIPTOPL-279 and is not re-filed.
- **The PSU round-trip in test_psu_timestamps.py** is RIPTOPL-286's already-filed circular mechanism; section 09 notes the file additionally carries independent golden-timestamp checks — context for -286's fix, no new finding.
- **Per-batch dissolved lists** (each with its reason) are in `round8-sections/section-01…11.md` — including the deliberate pre-change oracles (test_menu_sort.py's reference sort, test_iso_cache.py's old-code oracle), the both-driver-copies suites (the correct response to the production duplication filed as RIPTOPL-021/047), and the documented MBR saturation exception inside test_bdm_partitions.py (:194-201) that distinguishes RIPTOPL-298's GPT cases.

---

## UPDATED RESIDUE STATEMENT for RiptOPL (after round 8)

**The `.github/scripts` test corpus is LINE-CLOSED.** All 302 tracked source files (63,264 lines) have now been read end-to-end at pin `2e347d5ac885` (24 files / 3,170 lines in round 7; 278 files / 60,094 lines in round 8), plus the 4-file `tools/pc/http/tests/` supplement (585 lines). The single unread corpus item is the git-ignored, untracked `__pycache__` bytecode file — not source. Round 7's verdict "BOUNDED, not line-closed" is superseded.

What remains of RiptOPL, by class, unchanged from the round-5/7 statements except where noted:
- **Assembly (3,854 lines)** — read in round 7 (findings RIPTOPL-281…285); closed at that round's scope.
- **Headers as a corpus (35,590 lines)** — round 7's mechanism pass returned a negative result; headers remain consulted-throughout rather than line-read as a corpus. This is now the largest body of first-party text not line-read in full, and it is a *sampling* residue, not an unread one: every header any finding or test touches was read at its site.
- **Vendored stacks** (37,655 `modules/` lines + 204,965 `third_party/` lines) — out of scope by the audit's standing framing since pass 1; unchanged.
- **Documentation prose** — claims-checked against code in rounds 5 and 7 (RIPTOPL-292…295 and predecessors); prose as prose remains inventoried, not line-audited.
- **Un-auditable in this VM regardless of reading:** PS2 build results, real-hardware behaviour, live network interop. Nothing in any round is hardware-verified; Category 5 remains structural.

A further RiptOPL round on this pin and scope would be re-reading. Productive continuations are exactly: a pin move, a scope extension (headers line-read as a corpus; vendored internals), or a PS2 build/hardware rig.
