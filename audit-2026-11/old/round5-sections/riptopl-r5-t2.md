# Sable Audit — RiptOPL — Round 5, Section R5-T2 (tools/ Python build/publish/lang tooling)

- **Repo / pin:** NathanNeurotic/RiptOPL at `2e347d5ac885cd0ff73e9ad4143781946396f492`, read-only. `git status --porcelain` empty at start and at finish.
- **Method:** every assigned file read 1..EOF. Invocation verified by grepping the whole repo (workflows, `ps2.yaml`, `Makefile`, docs, `.github/scripts/`, `tools/`) for each script name before any dead-code claim. Prior reports `Sable-RiptOPL.md`, `-ROUND2.md`, `-ROUND3.md`, `-ROUND4.md` and `round4-sections/*.md` grepped for every assigned file/function before filing. Executed evidence was produced only against copies in `/tmp` (`/tmp/ziso_test.py`, `/tmp/ziso_probe/`); nothing was run inside the repo. No PS2 build exists in this VM — no build/hardware claims.
- **Line-count note:** the brief's stated total (5,631) is 300 over the sum of its own per-file counts; `wc -l` at the pin gives **5,331** total, and the per-file counts below match `wc -l` exactly.

## Coverage ledger

| File | Lines | Read | Invocation verdict |
|---|---|---|---|
| tools/publish_editions.py | 635 | 1..EOF | Live: `.github/workflows/rolling-release.yml:225`; imported by publish_llvm |
| tools/pc/apa-recovery/apa_recover.py | 438 | 1..EOF | Live: user-facing (README.md:940, docs/APA-SAFETY.md), tested by `.github/scripts/test_apa_recover.py` (flavours.yml:123) |
| tools/pc/http/catalog_reference.py | 433 | 1..EOF | Live: imported by http_conformance and tools/pc/http/tests (run via tools/pc/http/tests/run.sh ← tools/run_tests.sh) |
| tools/pc/ziso.py | 420 | 1..EOF | Live: user-facing PC tool, README.md:862-878 |
| tools/pc/http/http_conformance.py | 366 | 1..EOF | Live: manual conformance harness (tools/pc/http/README.md:41), unit-tested by its tests/ |
| tools/install_ports.py | 345 | 1..EOF | Live: tools/install_ps2build.sh:23; `LIBRARIES`/`download` imported by build_edition/package_edition; tested in flavours.yml |
| tools/build_edition.py | 315 | 1..EOF | Live: rolling-release.yml / rolling-llvm.yml / configurations.yml |
| tools/assets.py | 270 | 1..EOF | Live: workflows use `all`/`fetch`/`lang`; build_edition.py invokes `embed`/`version`; `release` exercised by `.github/scripts/test_release_publication.py:659ff` |
| tools/publish_llvm.py | 248 | 1..EOF | Live: rolling-llvm.yml:211,245 |
| tools/package_edition.py | 239 | 1..EOF | Live: rolling-release.yml:174,178; rolling-llvm.yml:163,167 |
| tools/make_save_icon.py | 214 | 1..EOF | **No in-repo invocation** (see R5-T2-09) |
| tools/lang_compiler.py | 214 | 1..EOF | Live: invoked per-language by tools/assets.py `lang()` |
| tools/publish_lite.py | 186 | 1..EOF | Live: module of publish_editions (import + assemble/verify/release_notes) |
| tools/make_bdmindex.py | 120 | 1..EOF | Live: run and output-checked by `.github/scripts/test_bdm_index.py:135-144` |
| tools/build_configuration.py | 118 | 1..EOF | Live: imported by build_edition and publish_editions |
| tools/audit_translations.py | 116 | 1..EOF | Live: docs/MAINTAINING-DOCS.md:55; tested by `.github/scripts/test_audit_translations.py` |
| tools/lite_devices.py | 113 | 1..EOF | Live: imported by build_edition, publish_lite, publish_llvm |
| tools/pc/http/tests/test_catalog_limits.py | 96 | 1..EOF | Live: tools/pc/http/tests/run.sh:44 |
| tools/analyze_coupling.py | 93 | 1..EOF | Live: ps2build.yml:81 (`--check`) |
| tools/warning_gate.py | 82 | 1..EOF | Live: ps2build.yml:79 (`--check`); tested by test_warning_gate.py |
| tools/build.py | 80 | 1..EOF | Live: ps2build.yml:74; `build()` imported by build_edition |
| tools/lang_decompiler.py | 60 | 1..EOF | **Dead: zero references anywhere in the repo** (see R5-T2-06) |
| tools/pc/http/tests/test_conformance.py | 47 | 1..EOF | Live: tools/pc/http/tests/run.sh:45 |
| tools/check_llvm_purity.py | 46 | 1..EOF | Live: rolling-llvm.yml:125 |
| tools/syntax_sweep.py | 37 | 1..EOF | **No workflow/test invocation; cannot fail** (see R5-T2-08) |
| **Total** | **5,331** | | |

## New candidate findings

### R5-T2-01 / ziso.py: corrupt LZ4 block can hang decompression forever, and the trim-loop can "decode" garbage
- **Category:** 6 (bug / robustness on untrusted file input)
- **Locations:** tools/pc/ziso.py:62-72 (`lz4_decompress`), called from tools/pc/ziso.py:130-196 (`decompress_zso`)
- **Problem:** On `LZ4BlockError` the helper deletes one trailing byte and retries, with no floor. When the input is exhausted the slice stops shrinking (`b""[:-1] == b""`), so the `while True` loop never terminates. Executed against a /tmp copy: `lz4_decompress(b"\xff", 2048)` and `lz4_decompress(b"\xff\xff\xff", 2048)` were still spinning when killed by a 3 s timeout, while `b"\x00"` returns immediately. The same loop has a second failure mode: trimming can reach a byte string LZ4 accepts, returning wrong data of the wrong length (executed: `b"garbage-not-lz4"` "decodes" to 6 bytes) — only the caller's later `len(dec_data) != block_size` check turns that into an abort rather than silent corruption.
- **Fix:** Delete the trim-and-retry loop; call `lz4.block.decompress` once and let a failure be an error (decompress_zso already has an exception path that reports the block and exits).
- **Benefit:** A damaged or hostile .zso can no longer hang the documented PC decompressor indefinitely; corrupt blocks fail at the block, with its offset.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ + executed on a /tmp copy.

### R5-T2-02 / ziso.py: decompression trusts the ZSO header — block size, alignment and index length are unvalidated
- **Category:** 6 (unchecked lengths/counts from file data)
- **Locations:** tools/pc/ziso.py:107-115 (`read_zso_header`), tools/pc/ziso.py:135-141 (only checks: magic, `block_size == 0`, `total_bytes == 0`, `header_size != 24`, `ver > 1`), tools/pc/ziso.py:160 (`index << align`)
- **Problem:** Nothing requires `block_size` to be a multiple of 2048 / power of two (the compress side enforces a multiple of 2048 for its own input), bounds `align` (a *signed* header byte), checks the index read actually returned `4 * index_count` bytes, or compares `total_bytes` with the real file size. Executed against crafted files on a /tmp copy: (a) a <24-byte file dies with an uncaught `struct.error` in `read_zso_header`; (b) `block_size=1, total_bytes=1000000` dies with an uncaught `struct.error: unpack requires a buffer of 4000004 bytes` at :141 — with a larger claimed total the same line first attempts an index allocation proportional to `total_bytes/block_size`; (c) header `align=-1` dies with an uncaught `ValueError: negative shift count` at :160.
- **Fix:** After the existing header check, reject a non-2048-multiple or out-of-range `block_size`, an `align` outside 0..~6, and a short index read, with the same "ziso file format error" exit the existing checks use. *Justification for adding checks:* the values come from an untrusted file header and currently reach an allocation size and a shift count unchecked; the checks are the smallest change that keeps the failure on the tool's own error path.
- **Benefit:** Malformed ZSOs produce the tool's own one-line format error instead of tracebacks, a hang-adjacent giant allocation, or a crash mid-write of the output ISO (the output file is already open and partially written when these fire).
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ + executed on /tmp copies.

### R5-T2-03 / ziso.py: compression silently discards a trailing partial block
- **Category:** 6 (silent data loss)
- **Locations:** tools/pc/ziso.py:233 (`total_block = total_bytes // block_size`), loop at tools/pc/ziso.py:247-310, header still records the true `total_bytes` at tools/pc/ziso.py:228-231
- **Problem:** Both compress and decompress iterate only over whole blocks. An input whose size is not a multiple of the block size loses its tail with no warning, while printing "ziso compress completed". Executed round-trip on a /tmp copy: a 3,000-byte input at the default 2,048 block size compressed and decompressed "completed" and came back as 2,048 bytes — 952 bytes silently gone. Real ISOs are normally block-aligned, which is exactly why the one misaligned file a user feeds it will be trusted.
- **Fix:** Refuse (or pad with an explicit warning) when `total_bytes % block_size != 0` at the top of `compress_zso`, mirroring the format error path. *Justification for adding a check:* it converts silent data loss into a refused operation; no new feature is added.
- **Benefit:** The tool can no longer produce a "successful" ZSO that is missing the end of the source image.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ + executed on a /tmp copy.

### R5-T2-04 / ziso.py: dead code and a dead option inside the live tool
- **Category:** 1 (dead code), with a Cat 2 minor
- **Locations:** tools/pc/ziso.py:44-47 (`hexdump`), tools/pc/ziso.py:369-402 (`load_sector_table`), tools/pc/ziso.py:349 (`-a` sets `DEFAULT_ALIGN`) vs tools/pc/ziso.py:223,227 (compress seeds `align` from `DEFAULT_ALIGN` then unconditionally overwrites it with `total_bytes // 2 ** 31`), tools/pc/ziso.py:55-58 (`lz4_compress_mp` duplicates the body of `lz4_compress` at :50-52)
- **Problem:** `hexdump` is never called anywhere and is Python-2 code — iterating `bytes` yields ints, so its `ord(i)` would raise `TypeError` if it ever were called. `load_sector_table` is never called; its own comment says "In future we will support NC". The documented `-a align` option is parsed into `DEFAULT_ALIGN` and then ignored: `compress_zso` always recomputes alignment from file size, so a user who follows `usage()` and passes `-a 6` for the "fast/large" alignment gets a file written with a different alignment than requested, with no notice. `lz4_compress_mp` is a 4-line copy of `lz4_compress` kept only because `Pool.map` needs a one-argument callable.
- **Fix:** Delete `hexdump` and `load_sector_table` (~47 lines); either honour `-a` in `compress_zso` (take the maximum of the requested and the size-forced alignment) or delete the `-a` parsing and its usage line; fold `lz4_compress_mp` into a one-line lambda/partial at the `pool.map_async` call site.
- **Benefit:** ~50 lines gone from a user-facing tool; the help text stops promising an option that does nothing.
- **Effort:** S · **Risk:** Low (the `-a` half is Med if any user's scripts depend on the current ignore-it behaviour — none can depend on its *effect*, since it has none) · **Confidence:** High, READ (call-site grep over the file and repo).

### R5-T2-05 / ziso.py: argument validation lets `-c` be omitted and `-b 0` through, both crash
- **Category:** 6 (unchecked CLI values reach arithmetic)
- **Locations:** tools/pc/ziso.py:336-366 (`parse_args`; `level` defaults to `None`, the only check is `bsize % 2048 != 0` at :362), tools/pc/ziso.py:405-410 (`main` treats only `level == 0` as decompress), tools/pc/ziso.py:233 (division by `block_size`)
- **Problem:** Executed on a /tmp copy: running with no `-c` at all crashes with `TypeError: %d format: a real number is required, not NoneType` inside `show_comp_info`; `-b 0` passes the multiple-of-2048 test (`0 % 2048 == 0`) and crashes with `ZeroDivisionError` at the `total_block` division; negative multiples of 2048 pass the same test. Non-numeric `-c`/`-b` values raise an uncaught `ValueError` from `int(a)`.
- **Fix:** In `parse_args`, require `level is not None`, `0 <= level <= 12`, and `bsize > 0`, exiting through the existing `usage()` path on violation. *Justification:* these are the values every later division, shift and LZ4 call trusts; validating them at the single entry point is smaller than guarding each use.
- **Benefit:** The three most likely user mistakes produce usage text instead of tracebacks (one of them after the output file has been created).
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ + executed on a /tmp copy.

### R5-T2-06 / tools/lang_decompiler.py is dead: no caller, no docs, no test, and it runs on import
- **Category:** 1 (dead code), with a Cat 2 minor
- **Locations:** tools/lang_decompiler.py:1-60 (whole file; top-level statements at :16-60, no `__main__` guard, no shebang), duplicated helper at tools/lang_decompiler.py:4-14 vs tools/lang_compiler.py:34-43
- **Problem:** A repo-wide grep for `lang_decompiler` (workflows, Makefile, ps2.yaml, docs, tests, other tools, shell scripts) returns zero hits outside the file itself. It is the reverse of `lang_compiler.py` (built `lng/*.lng` → `lng_src/*.yml`), a one-off migration shape: the live pipeline goes the other way (`assets.py lang()` downloads `lng_src/` and compiles it), so running this script today would overwrite the downloaded upstream translations with decompiled output. It also executes at import (module-level `open('lng/tmpl/_base.yml')` with CWD-relative paths), and its `represent_str` is a verbatim copy of `lang_compiler.py`'s.
- **Fix:** Delete the file (60 lines). If a future .lng→YAML migration is ever needed, `lang_compiler.py` plus git history hold everything required to recreate it.
- **Benefit:** One less unmaintained entry point that, if run from the repo root, silently rewrites `lng_src/`; removes a second copy of the YAML representer to keep in sync.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (full-repo invocation grep; contrast lang_compiler, which assets.py invokes per language).

### R5-T2-07 / http_conformance.py reads response bodies with no size bound
- **Category:** 6 (unbounded read from an untrusted network peer)
- **Locations:** tools/pc/http/http_conformance.py:58-68 (`get`: `body = resp.read()`), consumed at :193-197 (catalog) and :105-153 (`check_range`)
- **Problem:** Every response body is read fully into memory before any length validation happens. The catalog's 256 KB cap (`catalog_reference.CATALOG_BYTES_MAX`) is only applied by `parse_catalog` *after* the whole body has been downloaded and stored — the file-input path in catalog_reference.py:419-422 deliberately reads only `CATALOG_BYTES_MAX + 1` bytes for exactly this reason, and the network path defeats it. Likewise a server that ignores `Range` and answers a one-sector probe with a whole multi-GB ISO is fully buffered before `check_range` compares `len(body)` with the requested length. The tool's entire purpose is pointing it at servers whose behaviour is *not* trusted ("fails on exactly the things that would corrupt a disc read").
- **Fix:** In `get`, read at most a caller-supplied cap + 1 byte (catalog: `CATALOG_BYTES_MAX + 1`; range reads: the requested length + 1) and treat an over-long body as the length failure the checks already report. *Justification for adding a bound:* it is the same bound the reference parser already defines, applied at the point of read instead of after it.
- **Benefit:** A broken or hostile server can no longer OOM the conformance checker with the first response; over-long answers are reported as the failures they are.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-T2-08 / tools/syntax_sweep.py is not run by anything and cannot fail if it is
- **Category:** 7 (unwired check / CI clutter asymmetry)
- **Locations:** tools/syntax_sweep.py:1-37 (whole file; result handling at :33-37 prints error counts, no `sys.exit` anywhere in the file)
- **Problem:** The sweep is the only coverage for code behind `#ifdef` flavour defines the default build never compiles (RA, IGS, RTL, DTL, debug variants — its own docstring). Yet no workflow and no test runner references it (grep of `.github/workflows/` and `tools/run_tests.sh`: zero hits; it is mentioned only in docs/CPP-CONVERSION-PLAN.md as a manual verification step), and even when run by hand it always exits 0 — it prints `== SET: N errors` and stops. Its sibling ratchets are both wired and can fail: `warning_gate.py --check` and `analyze_coupling.py --check` run in ps2build.yml:79-81 and exit non-zero on regressions. So an ifdef-only syntax break is invisible unless someone runs this script manually *and reads its stdout*.
- **Fix:** Either wire it next to the other two ratchets in ps2build.yml with a non-zero exit when any set reports errors, or delete it and let the configurations workflow stand as the flavour coverage. (Wiring it is an addition of CI time; the LESS-NOT-MORE default is to pick one — the current state, a checker that exists but neither runs nor can fail, is the worst of both.)
- **Benefit:** Flavour-only code either gets a real gate or stops pretending to have one.
- **Effort:** S (delete) / M (wire + fix exit status) · **Risk:** Low · **Confidence:** High, READ.

### R5-T2-09 / tools/make_save_icon.py's `--check` mode is never invoked; the generator is referenced nowhere
- **Category:** 7 (unwired drift check)
- **Locations:** tools/make_save_icon.py:1-214 (whole file; `--check` at :196-206)
- **Problem:** A repo-wide grep for `make_save_icon` returns zero hits outside the file itself — no workflow, test, or doc names it. Its `--check` mode exists precisely to catch the committed `misc/list.icn` / `misc/icon.sys` drifting from the generator, and nothing runs it. The committed assets *are* structurally validated by `.github/scripts/test_save_icon_set.py` (flavours.yml:99), but that test re-derives the format by hand and never executes the generator, so an edit to the generator that changes its output (e.g. to the winding fix the file's own comments describe from #314) would pass CI while the shipped icon silently stays the old one — or the reverse. The sibling generator does not have this gap: `test_bdm_index.py:135-144` copies `make_bdmindex.py` into a temp tree, runs it, and diffs every output picture.
- **Fix:** Extend the existing save-icon test to run `make_save_icon.py --check` the way test_bdm_index.py runs its generator, or delete the `--check` mode and state in the docstring that regeneration is manual and unverified. *Justification for the test option:* it reuses an existing test file and an existing `--check` entry point; no new harness is added.
- **Benefit:** Generator and committed icon can no longer drift apart undetected, in either direction.
- **Effort:** S · **Risk:** Low · **Confidence:** Med, READ (in-repo invocation is provably absent; use outside the repo cannot be observed from here).

## Already filed — not repeated

- **RIPTOPL-026** (three publishing scripts re-implement the same release plumbing) — still the governing finding for publish_editions.py / publish_llvm.py / publish_lite.py. Update from this read, not a new finding: publish_llvm.py now *imports* publish_editions as `common` and reuses `gh`, `upload_verified`, `update_tag`, `refresh_release_date`, `remove_stale_archives`, `verify`, `bundle_configurations`, `name_assets`, `release_version` and `lite_candidates`, so the upload/rollback plumbing is consolidated; what remains triplicated is the release-notes construction — the same git-log changelog loop with the same markdown-escape set implemented three ways (publish_editions.py:325-340 via `re.sub`, publish_lite.py:155-167 and publish_llvm.py:137-195 via per-character joins) — and the SHA256SUMS parsing block, which appears in near-identical form in publish_editions.py `verify()`, publish_lite.py `verify()` and publish_llvm.py `verify()`.
- **RIPTOPL-007** (edition × configuration × toolchain × device matrix) — covers build_edition.py's EDITIONS/LITES tables, build_configuration.py's 15 configurations and lite_devices.py's device set as a surface; not re-filed.
- **RIPTOPL-027** (NBD server embedded in every build) — its locations already include lite_devices.py's `ALWAYS` set keeping `lwnbdsvr`/`ata_bd` in every lite build.
- **RIPTOPL-003 / RIPTOPL-033** (dead Makefile; 35-language list duplicated Makefile ↔ tools/assets.py:42 and the fork-overlay translation set) — assets.py's TRANSLATIONS list is that finding's location; audit_translations.py merely parses the same list back out of assets.py (audit_translations.py:45-50) and adds no third copy.
- **RIPTOPL-011** (~130 PNG assets bin2c-embedded, both glyph sets) — covers assets.py's PNG_ASSETS/glyph lists at :46-60.
- **RIPTOPL-022** (Korium assets stored as a zip, unzipped at build time) — covers the assets.py:141-160 extraction; this read confirms that path verifies the archive SHA-256 and every per-file digest from UPSTREAM.json and rejects destinations escaping the theme dir (assets.py:146-152), i.e. the extraction itself is safely written.
- **RIPTOPL-006** (mbedTLS vendored and globbed into two editions) — its locations are build_edition.py:114,137; the two `mbedtls` target definitions are still there as filed.
- **RIPTOPL-028** (HTTP as a game source) — covers the HTTP stack the pc/http tools serve; catalog_reference.py / http_conformance.py are that feature's contract harness, assessed here only for their own defects (R5-T2-07).

## Checked and clean

- **apa_recover.py (438 lines)** — the most carefully fenced file in the section: write path is bounded by real checks, not asserts (Disk.write refuses anything outside LBA 0-7 and non-sector multiples, apa_recover.py:170-176), the disk is opened read-only and only reopened writable after backup + typed confirmation (:106-113, :404-416), chain walking is loop-safe and end-bounded (`walk_chain`), the GPT-hybrid case is refused before any repair (:337-339), the backup is created with `xb` so it cannot overwrite, and post-repair verification re-runs the same format/fence/chain checks. Candidates that dissolved: zeroing LBA 2-5 when the table was unreadable (that region is __mbr reserved space, the backup records the bad sectors by number, and the sectors were unreadable before the tool touched them); `checksum()` assuming a 1 KB buffer (every call site passes exactly a 2-sector header read or a slice of one).
- **install_ports.py** — `unpack()` validates every member (absolute path, `..`, drive colon, symlink via external attributes) *before* `extractall` (:92-101); the mirror path pins SHA256SUMS by hash and each archive against it (:157-168); the token is sent only over HTTPS, is not copied across redirects, and is stripped from the llvm-objcopy environment (:44-47, :317-319); `_locals_after_globals`' ELF32/ELF64 offsets were re-derived field by field and are correct, and its parse failures fail toward repairing. The official (non-mirror) download carries no checksum, but it comes from the GitHub API's own asset list over HTTPS — a trust decision, not an oversight, given the mirror fallback exists for when that host fails.
- **package_edition.py** — `stage_orbit()` hashes the pinned archive before reading it, rejects absolute/`..`/backslash member paths, detects duplicate payload entries after prefix-stripping, and requires the RA handoff marker before writing anything; the upstream Neutrino 7z path inspects the `-slt` listing for traversal and link members before extraction (:176-186). The `.github/scripts/package_release_app.py` import is a deliberate, tested coupling (test_package_orbit_neutrino.py, test_llvm_release.py), not a hidden one.
- **publish_editions.py** (beyond RIPTOPL-026) — `verify()` rejects path-like or duplicate SHA256SUMS names, cross-checks kit contents against the standalone assets byte for byte, and refuses mixed SDK suites across editions; `upload_verified()` verifies rollback backups against the recorded remote digest *before* any destructive `--clobber`, and re-checks remote state after a failed upload before retrying. `check_unpacked_layout()`'s ELF program-header arithmetic was re-derived and is sound.
- **catalog_reference.py** — the parser the C side is held to: whole-catalog byte cap is measured before decoding, per-row length caps reject rather than truncate, `validate_path` rejects absolute URLs/paths, backslashes, empty and `..` segments, and duplicate startups keep the first record. Its `--emit-expected` mode can regenerate the fixtures' expected.json, but it is documented "review the diff!" and is not part of any automated run.
- **assets.py `release` command** — looked dead (no workflow calls it) and its `/tmp/DETAILED_CHANGELOG` header looked stale-prone; both dissolved: `.github/scripts/test_release_publication.py:659ff` exercises it directly as "a loader download too", and `make_changelog.sh` truncates and rewrites that header as its first action, before any of its exit paths, so a leftover header cannot be picked up (assets.py's own pre-run deletion of a stale *output* DETAILED_CHANGELOG covers the other file).
- **build.py / build_edition.py LLVM `-Oz` rule** — the same eecore stack-guard workaround appears in both (build.py:69-76, build_edition.py:151-156), but the two entry points build different definitions (plain ps2.yaml vs per-edition), and each comment names the other; a shared helper would have to cross the same import the files already use for `build()`. Left as deliberate duplication.
- **build_edition.add() / build_configuration.add()** — an identical 2-line dedup-append helper in two files that already import from each other; below the threshold where extracting it saves anything.
- **analyze_coupling.py** — basename keying of src modules would collide if two layers ever grew a same-named .cpp; there are none today (115 files checked), so it is a latent property of a documented regex heuristic, not a present defect. Its `--check` ratchet and exit behaviour are correct.
- **warning_gate.py, audit_translations.py, lang_compiler.py, lite_devices.py, build_configuration.py, check_llvm_purity.py, make_bdmindex.py, make_save_icon.py's generator maths, test_catalog_limits.py, test_conformance.py** — read in full; the defects that suggested themselves on a first pass (substring `"debug" in configuration` matching, `format_tokens` index arithmetic, the RLE encoder's run splitting, the catalog-limits harness's regex extraction of `httpParseCatalog`) each dissolved on full reading: inputs are validated against fixed tuples before the substring tests run, the regex extraction fails loudly rather than silently when production source changes, and the icon RLE/winding logic is corroborated by the independent structural test in test_save_icon_set.py.
- **http_conformance.py apart from R5-T2-07** — range/Content-Range/Content-Length validation, the size-probe hardening, chunked/multipart refusal and the cross-read agreement check are thorough; a malformed `--base` port raises before any check runs, but that is the operator's own argument, not peer data.

## Forced revisions to prior findings

None. No prior RIPTOPL-001…144 finding is contradicted by this section; RIPTOPL-026 is narrowed in one respect (the upload plumbing it described as triplicated is now shared via publish_llvm's import of publish_editions), which is recorded under "Already filed" rather than as a revision, since the finding's substance — three publishers, duplicated notes/checksum logic — still stands.
