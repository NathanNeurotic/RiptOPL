# Sable — RiptOPL ROUND 9, THIRD-PARTY PASS A (vendored stacks: mbedtls, korium, orbit-neutrino, ps2sdk-ppctty, ps2sdk-socket)

**Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` (rebuild/main) — clone HEAD == live head (`git ls-remote`) verified equal at pass start and at pass end; clone git-clean throughout; read-only held.
**Scope:** the five named `third_party/` subsets, under the round-9 vendored lens: findings only where vendored code is (a) dead in this build, (b) locally divergent from pristine upstream in a way that introduces a defect, (c) size cost without use, or (d) local integration (glue / config headers / feature macros) wrong. Pristine upstream bugs are unnumbered observations (§4), never findings.
**Subset as measured by this pass:** 293 files, 207,337 text lines (mbedtls 280 files / 205,284 lines; korium 3 / 121 + a 100-entry zip; orbit-neutrino 2 / 47; ps2sdk-ppctty 3 / 194; ps2sdk-socket 5 / 691).
**Section evidence:** `round9-sections/riptopl-r9-mbedtls-census.md` (109-row preprocessor census), `round9-sections/riptopl-r9-korium-orbit.md` (claims tables + integration traces).

## COUNT — 12 findings, RIPTOPL-500…511 (grep-verified against this file's headings)

| ID | Source | Title (short) | Cat |
|---|---|---|---|
| RIPTOPL-500 | korium | The korium theme-contract test is orphaned — no workflow runs it | 7 |
| RIPTOPL-501 | mbedtls | Glob compiles 36,695 lines of config-empty TUs in every mbedTLS build (61,273 in slim builds) | 5 |
| RIPTOPL-502 | mbedtls | The vendor test never hashes the vendored tree — the "byte-for-byte" pin is unenforced | 7 |
| RIPTOPL-503 | orbit | Orbit README says one i.LINK file is replaced after staging; the packer replaces two | 8 |
| RIPTOPL-504 | korium | Korium manifest completeness enforced by nobody — unlisted zip entries silently dropped | 6 |
| RIPTOPL-505 | korium | Korium extraction dir never cleaned — a removed asset can keep shipping from a dirty tree | 6 |
| RIPTOPL-506 | socket | ps2sdk-socket README describes the retired compat build in the present tense | 8 |
| RIPTOPL-507 | korium | Korium README's "Korium and RetroAchievements builds" misstates which editions read the archive | 8 |
| RIPTOPL-508 | mbedtls | `hkdf.c` compiles under RA but nothing calls its API; only the `MBEDTLS_HKDF_C` define is load-bearing | 1 |
| RIPTOPL-509 | mbedtls | `mps_reader.c` compiles under RA (538 lines) with no consumer anywhere in the tree | 1 |
| RIPTOPL-510 | ppctty | `irx_imports.h` is an unreferenced remnant of the removed ppctty source fallback | 1 |
| RIPTOPL-511 | mbedtls | Vendored mbedTLS `Makefile`/`CMakeLists.txt` (811 lines) are inert in every RiptOPL flow — recorded; retention recommended | 7 |

**Headline verifications (executed this pass, not trusted):** the vendored mbedTLS `include/` + `library/` + `LICENSE` are **byte-for-byte identical** to the official `mbedtls-3.6.7.tar.bz2` release archive whose SHA-256 equals the value pinned in `third_party/mbedtls/README.md` (diff over all 276 files: zero differences) — so category (b) yields **zero** mbedtls findings. `third_party/ps2sdk-socket/socket.c` and `fdman.h` are **byte-identical** to ps2dev/ps2sdk at the pinned commit `ac92a9f6…` (executed cmp) — zero local modifications. The korium pin is fully self-consistent (archive digest + all 100 per-file digests verified by execution). All remaining findings are therefore in classes (a), (c) and (d): dead-in-build weight, unenforced pins, and documentation that contradicts the build.

---

## Per-library built / not-built verdicts

- **mbedtls — BUILT and LINKED, in exactly two shapes.** RA editions: ee_lib target from `sources = ["third_party/mbedtls/library/*.c"]` with `MBEDTLS_CONFIG_FILE="riptopl_mbedtls_config.h"` (`tools/build_edition.py:114-115`), linked into the main target (`tools/build_edition.py:118`). Non-RA standard builds with an update channel: same glob with the slim config (`tools/build_edition.py:137-141`). All other builds (Lite, debug, variants without a channel) compile and link none of it (`third_party/mbedtls/README.md:5-7`; enforced by `.github/scripts/test_mbedtls_vendor.py:92`). The library is consumed only as a TLS **client** by `src/platform/ratls.c` + `src/sources/ratls_ps2.cpp`.
- **korium — NOT COMPILED (build-time data).** `assets.zip` is read by `tools/assets.py:156-173` (`embed()`) for korium-theme editions only (`korium`, `ra`, `lite`, `lite_ra` — `tools/build_edition.py:19-24,34-35,201,208`), with archive + per-file digest verification inside `embed()` itself.
- **orbit-neutrino — NOT BUILT (pin metadata only).** The directory holds no code. At package time `tools/package_edition.py:161-168` downloads the pinned NULD zip (or takes `--orbit-archive`) and `stage_orbit` (`tools/package_edition.py:28-78`) verifies its SHA-256 against `release.json` before extracting `neutrino/` into RA kits.
- **ps2sdk-ppctty — NOT BUILT.** The driver resolves from the installed ps2build SDK (`tools/build_configuration.py:84-85`; `.github/scripts/test_ps2build_configurations.py:106` asserts `ppctty` is *not* a target). The directory contributes only `LICENSE` + `UPSTREAM` to `_uart` kits' licence staging (`tools/build_edition.py:253-257`).
- **ps2sdk-socket — NOT BUILT.** The `ps2sdk_socket_compat` archive "is no longer generated or linked" (`docs/NATIVE-SDK-SOCKETS.md:10`; absence asserted at `.github/scripts/test_ps2build_configurations.py:67-68` and `.github/scripts/test_release_editions.py:27-28`; native-sockets comment at `tools/build_edition.py:126-127`). The directory contributes only `LICENSE` + `UPSTREAM` to RA kits' licence staging (`tools/build_edition.py:253-257`); the source is retained for provenance per its `UPSTREAM`.

---

## 1. Findings

### RIPTOPL-500 — The korium theme-contract test is orphaned: no workflow runs it
- **Category:** 7 (build/CI)
- **Locations:**
  - `.github/scripts/test_korium_builtin_assets.py:1-45` (whole file — Players badges, korium app cover, background-art default)
  - `.github/workflows/flavours.yml:28-62` (the enumerated test list — ~35 scripts named individually, including both orbit tests at `:60-61`) and the only glob loops at `:65` (`test_ripps2_*.py`) and `:103` (`test_ilink_*.py` + named files): neither matches this filename; a grep for `test_korium` across `.github/workflows/` returns nothing (verified by coordinator)
- **Problem (READ):** The test that guards the built-in korium theme contract — the regression class its own header ties to the `test/korium-audit-remediation` branch — is executed by no CI job. A change to `src/gfx/themes.cpp`, `src/gfx/textures.cpp`, `src/core/settings.cpp`, or the `coverapp`/`case_overlay` exclusion line in `tools/assets.py` can break the shipped korium theme with CI fully green. The pin's *digests* are still enforced, but at build time inside `tools/assets.py` (§2), not by this test.
- **Fix (LESS):** Add one line naming the test to the flavours.yml test step, beside the orbit tests. No new machinery.
- **Benefit:** The remediation the test was written for stays remediated; the korium theme contract becomes a CI gate instead of a file that merely exists.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High — workflow region read in full; filename grep over `.github/`, `tools/`, `Makefile` returns zero references (coordinator-verified).

### RIPTOPL-501 — Glob source list compiles 36,695 lines of config-empty mbedTLS TUs in every mbedTLS build (61,273 lines in updater builds)
- **Category:** 5 (performance — build time; archive size)
- **Locations:**
  - `tools/build_edition.py:114-115` (RA target) and `tools/build_edition.py:137-138` (slim target): both `sources = ["third_party/mbedtls/library/*.c"]`
  - Census: `round9-sections/riptopl-r9-mbedtls-census.md` §3–§4 (all 109 `library/*.c`, 129,300 lines, classified by host `gcc -E` under each config + 15 files spot-read)
- **Problem (READ, preprocessor-verified):** 55 of the 109 files (36,695 lines, 28.4% of the library's C lines) compile to **empty objects under both** RiptOPL configs — server TLS (`ssl_tls12_server.c` 4,311 + `ssl_tls13_server.c` 3,624 lines), unused ciphers/hashes (ARIA, Camellia, DES, MD5, SHA-1, SHA-3, RIPEMD160), X.509 write/CRL/CSR, PEM, PKCS#5/7/12, DHM, ECJPAKE, LMS, PSA storage/ITS/SE, networking, timing, threading, debug, version. Under the slim config a further 20 files (24,578 lines) are also empty — 75/109 files, 61,273 lines, 47.4%. The vendored tree itself must stay byte-identical (it is a verified release archive); the waste is entirely in RiptOPL's own target definitions. Guard-defying cases the census caught: `entropy_poll.c` is empty in both despite `MBEDTLS_ENTROPY_C` being on (its body is killed by `MBEDTLS_NO_PLATFORM_ENTROPY` at `library/entropy_poll.c:30` and the absent NV-seed option); `block_cipher.c` is empty via the *derived* macro `MBEDTLS_BLOCK_CIPHER_C` (`include/mbedtls/config_adjust_legacy_crypto.h:372-374`); `bignum_mod.c`/`bignum_mod_raw.c` are empty because `MBEDTLS_ECP_WITH_MPI_UINT` exists only as a commented option in the unused stock config.
- **Fix (LESS):** Replace the glob in each of the two target definitions with that config's explicit LIVE+PARTIAL source list from the census (RA: 54 files; slim: 34 files). Do not touch `third_party/mbedtls/`. The glob assertion in RiptOPL's own vendor test (`.github/scripts/test_mbedtls_vendor.py:73` and `:86`, which currently require `sources == ["third_party/mbedtls/library/*.c"]`) must be updated in the same change.
- **Justification (fix changes a build definition):** removes work, adds no code to the product; the two lists are derived mechanically from the preprocessor census, and a wrongly dropped LIVE file surfaces as an undefined symbol at link, never as silent behaviour change; the host TLS test (`.github/scripts/test_ratls_host.py`) globs independently and still exercises the full tree.
- **Benefit:** 55 fewer compile invocations and archive members in every mbedTLS build, 75 in updater builds, with a proportional build-time saving. Expected ELF change: none (empty objects resolve no symbols) — stated plainly: this is build time and archive bulk, not shipped size.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High for the census (preprocessor pass under both configs + spot reads); the build-time saving's magnitude is INFERRED (no timed PS2 build exists in this VM).

### RIPTOPL-502 — The vendor test never hashes the vendored tree: mbedTLS's "byte-for-byte" pin is unenforced
- **Category:** 7 (build/CI — enforcement gap on a security boundary)
- **Locations:**
  - `.github/scripts/test_mbedtls_vendor.py:34-41` — the entire tree check: README exists, contains `3.6.7` and *a* regex match for any 64-hex string, `LICENSE` is a file, two generated sources exist, every `library/*.c` head carries an Apache-2.0 SPDX line
  - `third_party/mbedtls/README.md:11-13` — the claim being left unenforced: "Vendored: `library/`, `include/` and `LICENSE`, byte-for-byte" + the archive SHA-256
- **Problem (READ):** Nothing in CI compares a single vendored byte against the pinned archive. The README check accepts *any* 64-hex string, not the archive's digest; no file in `library/` or `include/` is hashed anywhere. A local edit to any of the 276 vendored files — the TLS stack both the RA lookup and Check for Updates verify downloads with — would pass the vendor test silently as long as its SPDX header survived. This pass verified by execution that the tree **is** currently byte-identical to the pinned archive (diff over all 276 files + archive SHA-256 match), so this is an enforcement gap, not a present divergence.
- **Fix (LESS):** Extend `test_mbedtls_vendor.py` to hash the vendored tree (e.g. one digest over the sorted per-file SHA-256 list for `library/` + `include/` + `LICENSE`) and compare it to a pinned constant beside the existing root pins — roughly ten lines in a file that already pins seven root fingerprints the same way (`test_mbedtls_vendor.py:20-28`).
- **Justification (fix ADDS a check):** concrete problem — the pin is the integrity boundary for the project's only TLS implementation, the README asserts the property in words, and the test that exists to protect the vendoring does not test it. The fix is a comparison, not a system.
- **Benefit:** Any future accidental or deliberate local patch to vendored mbedTLS fails CI immediately instead of shipping invisibly; the README's central claim becomes true by construction.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High — test read in full; current byte-identity executed by the coordinator this pass.

### RIPTOPL-503 — Orbit README says one i.LINK file is replaced after staging; the packer replaces two
- **Category:** 8 (documentation contradicts the shipped behaviour)
- **Locations:**
  - `third_party/orbit-neutrino/README.md:13-15` — "One file is replaced afterwards, exactly as in the standard kit: `modules/IEEE1394_bd_mini.irx` becomes RiptOPL's i.LINK driver"
  - `tools/package_edition.py:172-186` — the replacement loop runs over **both** `IEEE1394_bd_mini.irx` (`ilink_bd_sha256`) and `iLinkman.irx` (`ilink_manager_sha256`), in both kits, refusing to package if either is missing or digest-mismatched
  - `tools/package_edition.py:64-67` — the `RA-EDITION-NEUTRINO.txt` label the same program writes into every RA kit correctly names both files
- **Problem (READ):** The code's own comment (`tools/package_edition.py:172-176`) says the manager replacement is load-bearing — `iLinkman.irx` "carries the TX cancellation, alarm and receive-FIFO fixes, so the driver alone leaves them out". The README in the pin directory describes half the substitution. A reviewer trusting it misunderstands what the shipped runtime is, and could "simplify" the packer to match the doc, silently dropping the manager fixes.
- **Fix (LESS):** Correct the README sentence to name both files and the reason (doc-only, one sentence).
- **Benefit:** The pin's documentation matches the shipped bytes and the kit's own generated label.
- **Effort:** S
- **Risk:** Low (doc-only)
- **Confidence:** High — both sides read in full; coordinator-verified at `tools/package_edition.py:173-178`.

### RIPTOPL-504 — Korium manifest completeness is enforced by nobody: unlisted zip entries are silently dropped
- **Category:** 6 (latent bug in build tooling)
- **Locations:**
  - `tools/assets.py:166-173` — `embed()` iterates `manifest["files"].items()` only; a zip entry absent from the manifest is never read, extracted, or reported
  - `.github/scripts/test_korium_builtin_assets.py:38` — the only completeness-adjacent assert in the tree checks a *single* named key's absence (`gfx/coverapp.png`)
- **Problem (READ):** The korium README's update instruction is "Update the archive and manifest together" (`third_party/korium/README.md:10-11`), but nothing checks the pair's completeness. If a future theme import adds a file to `assets.zip` and updates `archive_sha256` but forgets the `files` entry, the build succeeds and the new asset is silently never extracted or embedded. The reverse direction fails only by accident — an uncaught `KeyError` traceback from `package.read(name)` instead of the clean `sys.exit` used for digest mismatches. This pass verified by execution that the sets are equal *today* (100 = 100); nothing keeps them equal.
- **Fix (LESS):** In `embed()`, assert `set(package.namelist()) == set(manifest["files"])` before extracting — one comparison at the point where the archive is already open and hashed.
- **Justification (fix ADDS a check):** concrete failure mode named above; the check is a single set comparison inside an existing verification block, not new machinery.
- **Benefit:** A partial manifest update fails the build loudly instead of silently shipping a theme missing its new assets.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High — the loop and the absence of any set comparison were read directly; current equality is execution-verified, so this is a gap, not a live defect.

### RIPTOPL-505 — Korium extraction directory is never cleaned: a removed asset can keep shipping from a dirty build tree
- **Category:** 6 (latent bug in build tooling)
- **Locations:**
  - `tools/assets.py:146-148` — `embed()` deletes stale `asm/*.c` before generating
  - `tools/assets.py:162-173` — extraction into `build/theme-assets/korium` writes files and never deletes any
  - `tools/assets.py:195-200` — the themed lookup uses a file if it *exists* in the theme dir, else falls back in-tree
- **Problem (READ mechanism, INFERRED outcome):** The extraction dir accumulates across runs. If a later manifest revision *removes* a file (a texture deliberately dropped from the pin), the previously extracted copy remains in a reused local build tree and the existence-based lookup at `:195-200` will still find and embed it — bypassing the manifest and its digests entirely for that file. CI's fresh runners mask this; local edition builds (the documented workflow) do not.
- **Fix (LESS):** Clear `theme_dir` at the start of the korium branch of `embed()`, mirroring the `asm/*.c` cleanup three lines earlier in the same function.
- **Benefit:** What a korium build embeds is always exactly what the current manifest lists and hashes.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High for the mechanism (no cleanup exists in the read code); the stale-file-ships outcome is INFERRED from the lookup, not executed.

### RIPTOPL-506 — ps2sdk-socket's README describes the retired compat build in the present tense
- **Category:** 8 (documentation contradicts the build)
- **Locations:**
  - `third_party/ps2sdk-socket/README.md:5-7` — "Its build compiles `socket`, `bind`, `recvfrom`, `sendto` and `setsockopt` into a separate archive" and "Reassess the compatibility archive when the SDK supplies these functions itself"
  - `third_party/ps2sdk-socket/UPSTREAM:4-6` — "retired from build graphs after sdk-core v2026.10.13 added native libcglue socket/netdb/select entrypoints… no fallback archive is linked"
  - `docs/NATIVE-SDK-SOCKETS.md:10` — "the `ps2sdk_socket_compat` archive is no longer generated or linked"; `tools/build_edition.py:126-127` (comment: keep descriptor ownership "in the SDK, not a second archive")
- **Problem (READ):** Both README statements are overtaken: the archive is not compiled by any build, and the reassessment trigger already fired (that is *why* it was retired). A reader of the vendored directory alone — the person most likely to touch the fallback — gets the opposite of the truth from the directory's own README, while the correct account sits in `UPSTREAM` and the docs. The retained source itself is *not* the finding: its retention is a deliberate, documented provenance decision (`UPSTREAM:5-6`, `docs/NATIVE-SDK-SOCKETS.md:22-23`), and this pass verified `socket.c`/`fdman.h` are byte-identical to the pinned ps2sdk commit.
- **Fix (LESS):** Rewrite the README's two sentences in the past tense (retired after sdk-core v2026.10.13; source retained for provenance — see `UPSTREAM`). No code change.
- **Benefit:** The directory describes itself correctly; nobody resurrects a dead archive on the README's authority.
- **Effort:** S
- **Risk:** Low (doc-only)
- **Confidence:** High — all four sources read.

### RIPTOPL-507 — Korium README's "Korium and RetroAchievements builds" misstates which editions read the archive
- **Category:** 8 (documentation imprecision with provenance consequences)
- **Locations:**
  - `third_party/korium/README.md:8` — "`tools/assets.py` reads this archive for Korium and RetroAchievements builds"
  - `tools/build_edition.py:19-24,34-35` — theme mapping: archive read by `korium`, `ra`, `lite`, `lite_ra`; the RetroAchievements-IncebtIon edition (`ra_incebtion`) has theme `incebtion` and never reads it
  - `tools/assets.py:152-156` — the branch is chosen by `RIPTOPL_THEME`, not by RA-ness
- **Problem (READ):** As written, a reader expects every RA build to carry the korium pin's assets and would look for korium provenance in the RA-IncebtIon kit, which has none; the two Lite editions that *do* read the archive go unmentioned.
- **Fix (LESS):** Reword to name the mechanism: "`tools/assets.py` reads this archive for every build whose theme is Korium (the Korium, RetroAchievements, Lite and Lite-RA editions)".
- **Benefit:** Provenance expectations match the edition matrix.
- **Effort:** S
- **Risk:** Low (doc-only)
- **Confidence:** High.

### RIPTOPL-508 — `hkdf.c` compiles under RA but nothing calls its API; only the `MBEDTLS_HKDF_C` define is load-bearing
- **Category:** 1 (dead code — live-but-unpulled)
- **Locations:**
  - `third_party/mbedtls/library/hkdf.c:9` (guard; 161 lines, LIVE under RA per census)
  - `third_party/mbedtls/library/ssl_tls13_keys.c:180` — the actual HKDF use in TLS 1.3: `psa_key_derivation_setup(…, PSA_ALG_HKDF_EXPAND(…))`, i.e. the PSA implementation inside `psa_crypto.c`
  - `third_party/mbedtls/include/mbedtls/config_adjust_psa_from_legacy.h:121-130` — `MBEDTLS_HKDF_C` ⇒ `PSA_WANT_ALG_HKDF*` derivation
- **Problem (READ + grep):** The legacy `mbedtls_hkdf()` / `mbedtls_hkdf_extract()` / `mbedtls_hkdf_expand()` defined in `hkdf.c` have zero callers in any other `library/*.c` file (whole-library grep). The file compiles into the RA archive and can never be pulled by the client-only consumers. The trap: the *define* must stay — removing `MBEDTLS_HKDF_C` from `riptopl_mbedtls_config.h` instead of the file would un-derive `PSA_WANT_ALG_HKDF*` and break TLS 1.3 key derivation.
- **Fix (LESS):** If RIPTOPL-501's explicit source lists are adopted, omit `hkdf.c` from the RA list — and keep the define untouched. Not separately actionable under the glob.
- **Benefit:** One fewer substantive TU per RA build; removes the standing suggestion that the legacy HKDF API is available/used.
- **Effort:** S (given RIPTOPL-501)
- **Risk:** Low, provided the define is kept as specified; the wrong fix (dropping the define) is a TLS 1.3 break the host test's TLS 1.3 cases would catch.
- **Confidence:** INFERRED — grep-level call-graph proof plus standard static-archive pull semantics; no PS2 link map was produced in this VM.

### RIPTOPL-509 — `mps_reader.c` compiles under RA (538 lines) with no consumer anywhere in the tree
- **Category:** 1 (dead code — live-but-unpulled)
- **Locations:**
  - `third_party/mbedtls/library/mps_reader.c:10` — sole guard is `MBEDTLS_SSL_PROTO_TLS1_3`, so it compiles substantive code under RA
  - Absence verified by grep for `mps_reader` / `mbedtls_mps_` over all other `library/*.c` and `include/mbedtls/*.h` files: zero hits (coordinator-verified); its companion `mps_trace.c` is EMPTY under both configs (census §4)
- **Problem (READ + grep):** The 3.6.7 TLS 1.3 stack in this tree never calls the MPS reader — it is an unintegrated upstream component the glob sweeps into every RA build.
- **Fix (LESS):** If RIPTOPL-501's explicit source lists are adopted, omit `mps_reader.c` from the RA list. No config change.
- **Benefit:** 538 fewer lines compiled per RA build; no functional change (no caller exists to break).
- **Effort:** S (given RIPTOPL-501)
- **Risk:** Low — a hidden caller would surface as an undefined symbol at link; the host test links the full client path.
- **Confidence:** INFERRED — whole-tree reference search plus archive pull semantics; no PS2 link map produced in this VM.

### RIPTOPL-510 — `third_party/ps2sdk-ppctty/irx_imports.h` is an unreferenced remnant of the removed source fallback
- **Category:** 1 (dead code)
- **Locations:**
  - `third_party/ps2sdk-ppctty/irx_imports.h:1-21` (whole file — a stock ps2sdk IOP imports header)
  - `third_party/ps2sdk-ppctty/UPSTREAM:5-6` — "The previous source fallback at ac92a9f… was removed after verifying the released GCC and LLVM packages"
  - `docs/BUILD-CONFIGURATIONS.md:70-71` — the directory "retains the AFL-2.0 license and provenance notice" (no third file is accounted for)
  - `tools/build_edition.py:253-257` — kit staging copies only `LICENSE` and `UPSTREAM` from this directory; `.github/scripts/test_ps2build_configurations.py:107-108` asserts only those two files
- **Problem (READ):** The fallback's source was removed, but this one source file of it survives. Nothing compiles it (the directory holds no ppctty source and the driver resolves from the installed SDK — see verdicts), no include path names the directory, staging never copies it, no test references it (whole-tree grep for the path returns only the `LICENSE`/`UPSTREAM` references above).
- **Fix (LESS):** Delete the file. The directory then matches its documented contents exactly.
- **Benefit:** One less phantom source file implying a bundled driver that does not exist.
- **Effort:** S
- **Risk:** Low — provably unreferenced.
- **Confidence:** High — whole-tree reference search; staging and test contents read.

### RIPTOPL-511 — Vendored mbedTLS `Makefile`/`CMakeLists.txt` (811 lines) are inert in every RiptOPL flow — recorded; retention recommended
- **Category:** 7 (repo clutter — non-use proven, deletion *not* recommended)
- **Locations:**
  - `third_party/mbedtls/library/Makefile` (410 lines; fatal-errors at `:13-22` unless `../framework/exported.make` exists — no `framework/` directory is vendored)
  - `third_party/mbedtls/library/CMakeLists.txt` (379 lines), `third_party/mbedtls/include/CMakeLists.txt` (22 lines)
  - Negative evidence: census §5 — repo-wide search over `tools/`, `.github/`, `ps2.yaml` for any reference returns nothing; production builds are ps2build targets generated by `tools/build_edition.py`; `tools/syntax_sweep.py:22` uses include paths only; both test harnesses compile the glob directly with a host compiler
- **Problem (READ):** 811 lines / 24,259 bytes of upstream build system that nothing executes, one of which actively misleads (invoking `library/Makefile` stops with a fatal missing-framework error). Filed so the non-use is proven rather than assumed — the same status as RIPTOPL-510's header, except the remedy differs:
- **Fix (LESS):** **Keep them.** Deleting saves 24 KB of repo bulk and removes a misleading entry point, but breaks the byte-for-byte release-archive fidelity that RIPTOPL-502's pin discipline and the README's wholesale-replacement update procedure (`third_party/mbedtls/README.md` "Updating") depend on — 24 KB is not worth complicating every future re-vendor diff. If repository size is ever independently valued, delete all three together and amend the README's "byte-for-byte" wording in the same commit.
- **Benefit:** None claimed for retention beyond fidelity; the finding's value is the verified non-use record.
- **Effort:** S either way
- **Risk:** Low for builds under either option (nothing references the files — grep-verified).
- **Confidence:** High on non-use (exhaustive reference search + both harnesses read); keep-vs-delete is explicitly the maintainer's call.

---

## 2. Per-library ledger (present vs read)

### mbedtls — 280 files, 205,284 lines, 7,658,231 bytes
Present: `include/` 99 files / 53,333 lines (97 `.h` + `CMakeLists.txt` + `.gitignore`); `library/` 177 files / 151,212 lines (109 `.c` = 129,300 lines, 65 internal `.h` = 21,112 lines, `Makefile` 410, `CMakeLists.txt` 379, `.gitignore`); root: `README.md` 29, `LICENSE` 553, `riptopl_mbedtls_config.h` 85, `riptopl_mbedtls_slim_config.h` 72.
Read in full: `README.md`, both config headers (coordinator). Census: all 109 `.c` classified LIVE/PARTIAL/EMPTY under **both** configs by host preprocessing (`gcc -E`, surviving-line counts), with 15 files spot-read directly and 4 more guard skeletons read (worker; named in census §Method). Regions read by coordinator: `library/entropy.c:38-67` (hardware-poll registration), `library/x509_crt.c:2695-2739` (software `inet_pton` region).
Verified by execution instead of reading: **all 276 vendored files byte-compared against the official 3.6.7 release archive (archive SHA-256 == README pin) — zero differences; `LICENSE` cmp-identical.** Under the vendored lens this executed zero-diff is the complete verification for divergence class (b).
Unread (named): the pristine upstream bodies of the 109 `library/*.c` and 162 headers beyond the census classification, the spot reads, and the two regions above — deliberate: pristine upstream defects are observations, not findings, and no line-read can out-verify a zero byte diff for local divergence. The two `.gitignore` files (1 line each) were not read.
Integration read in full (outside the subset, cited as evidence): `include/ratls.h` (86), `src/platform/ratls.c` (459), `src/sources/ratls_ps2.cpp` (304), `.github/scripts/test_mbedtls_vendor.py` (142), plus `tools/build_edition.py` regions `:95-169`, `:235-294`.

### korium — 3 files, 589,318 bytes
Present: `README.md` 13 lines, `UPSTREAM.json` 108 lines, `assets.zip` 579,186 bytes / 100 entries.
Read in full: `README.md`, `UPSTREAM.json` (all 100 digests present and execution-verified). `assets.zip`: all 100 entries enumerated by name and SHA-256-verified against the manifest by execution (archive digest also matches) — **verified, not line-read**.
Unread (named): the *contents* of the zip's 100 entries (98 theme PNGs — binary graphics; 2 theme `.cfg` files, `misc/conf_theme_OPL.cfg` and `misc/theme_coverflow.cfg` — their text was asserted on by regex in the orphaned test, not read by this pass).
Integration read in full (worker): `tools/assets.py` (270), `.github/scripts/test_korium_builtin_assets.py` (45), `tools/build_edition.py` (335).

### orbit-neutrino — 2 files, 47 lines, 3,110 bytes
Present and read in full: `README.md` (38), `release.json` (9). Nothing in-directory is unread. The pinned NULD zip itself is not in the repo (downloaded at package time); its contents are binary-class and outside this pass.
Integration read in full (worker): `tools/package_edition.py` (239), `.github/workflows/orbit-neutrino-watch.yml` (88), `.github/scripts/test_package_orbit_neutrino.py` (95), `.github/scripts/orbit_neutrino_watch.py` (157); regions of `package_release_app.py`, `flavours.yml`, `rolling-release.yml`, `sysneutrino.cpp`, `supportbase.cpp` as cited in the section file. Claims tally for the README: 16 claims — 8 MATCH, 5 partial (in-tree half MATCH, upstream behaviour UNVERIFIABLE), 1 CONTRADICTED (RIPTOPL-503), 3 UNVERIFIABLE.

### ps2sdk-ppctty — 3 files, 194 lines, 9,936 bytes
Read in full: `UPSTREAM` (7), `irx_imports.h` (21). Not line-read: `LICENSE` (166) — cmp-verified byte-identical to `third_party/ps2sdk-socket/LICENSE`, the stock AFL-2.0 text.

### ps2sdk-socket — 5 files, 691 lines, 21,414 bytes
Read in full: `README.md` (7), `UPSTREAM` (9), `fdman.h` (38), `socket.c` (471). Verified by execution: `socket.c` and `fdman.h` are **byte-identical** to ps2dev/ps2sdk `ee/libcglue` at the pinned commit `ac92a9f657d2e531dd8f060250b07f2a5ac6dea5` — the README's "Unmodified" claim is true, and there are no local modifications to defect-hunt. Not line-read: `LICENSE` (166, see ppctty).

---

## 3. Config-surface verification (no findings — checked clean)

Both RiptOPL config headers were read line-for-line and cross-checked against the code and the vendor test:
- Client-only in both: no `MBEDTLS_SSL_SRV_C`, `MBEDTLS_FS_IO`, `MBEDTLS_NET_C`, `MBEDTLS_TIMING_C`, `MBEDTLS_DEBUG_C`, `MBEDTLS_SSL_RENEGOTIATION` (also test-asserted, `test_mbedtls_vendor.py:124-133`). No SHA-1, no CBC cipher mode, no DTLS in either config — the TLS 1.2 surface is ECDHE + AES-GCM (+ ChaCha20-Poly1305 in RA only).
- The slim header's self-description ("without TLS 1.3, PSA, ChaCha20-Poly1305, X25519, SHA-224 and RSA-PSS") matches its measured EMPTY delta **exactly**: the 20 slim-only empty files are precisely the TLS 1.3 + PSA + ChaCha/HKDF set (census §4). Its "about 69 KB less code (34 KB packed)" figure is INFERRED — not build-verified in this VM.
- Entropy wiring verified at `library/entropy.c:48-52`: with `MBEDTLS_NO_PLATFORM_ENTROPY` + `MBEDTLS_ENTROPY_HARDWARE_ALT` (both configs), RiptOPL's `mbedtls_hardware_poll` is registered as the **only** entropy source, as a STRONG source.
- Glue verified in `src/platform/ratls.c`: `MBEDTLS_SSL_VERIFY_REQUIRED` (`:276`), `mbedtls_ssl_set_hostname` (`:282`), anchors parsed with fail-closed error (`:268-270`), clock checked before any network use (`ratls_ps2.cpp` + `raTlsClockOk`), PSA RNG vs CTR_DRBG selected by the same macro that selects the config (`MBEDTLS_USE_PSA_CRYPTO`). Root counts agree everywhere: RA lookup anchors = 5 (GTS R1/R3/R4 + ISRG X1/X2, `ratls_ps2.cpp:238-244`), updater anchors = 4 (Sectigo E46/R46 + ISRG X1/X2, `:283-288`), 7 distinct pinned in `test_mbedtls_vendor.py:20-28` and present in `third_party/ca-roots/`. `RA_ORBIT_SNAPSHOT_MAX 1216` == `release.json` `snapshot_bytes_max` 1216.
- Orbit staging verified sound (worker §4): digest checked before the zip is opened, 8 required non-empty entries + `-ra=<file>` marker checked before any write, refusal-before-write is test-proven and CI-wired (`flavours.yml:60`).

## 4. Upstream observations (unnumbered — pristine code, not findings)

- **Entropy strength is an estimate, honestly labelled in-tree.** `mbedtls_hardware_poll` (`src/sources/ratls_ps2.cpp:127-147`, first-party glue) folds 8 COP0-Count jitter samples per byte across `DelayThread(1)` switches, "credited at 1 bit per sample" by its own comment. The PS2 has no hardware RNG; the construction is reasonable and the crediting is conservative, but the true entropy rate is INFERRED — it cannot be measured from source. Everything else in both TLS stacks rests on this one source (by design, §3).
- **`MBEDTLS_TEST_SW_INET_PTON`** (both configs) is a test-named upstream bypass define used as the production mechanism for certificate IP-SAN parsing on a libc without `inet_pton` (consumed at `library/x509_crt.c:2728`; upstream's own comment at `:2726` calls it "a bypass define to force testing of this code"). In 3.6 there is no production-named alternative; the use is upstream-sanctioned. Observation only.
- **`mbedtls_ms_time()` wraps.** The glue (`src/sources/ratls_ps2.cpp:103-106`) derives it from a 32-bit microsecond `clock()`, so the millisecond value repeats every ~71.6 minutes. Impact INFERRED minimal: no DTLS is enabled, and TLS 1.3 uses ms-time only for session-ticket bookkeeping in this client. Noted, not filed.
- **`psa_crypto.c` surface (INFERRED, worker census §6):** the pulled PSA object (9,532 lines, RA) compiles the full key-management API although the consumers name only `psa_crypto_init()` and TLS-internal ephemeral paths use a subset. Archive pull granularity is the whole object; whether the EE link also applies `--gc-sections` to it is not verifiable in this VM. Unnumbered.
- **`stage_orbit` accepts any top-level folder name** (`tools/package_edition.py:35-40` checks only the second path component): neutralized by the whole-archive digest pin — the prefix cannot vary for the pinned bytes. Not filed (worker §9).
- **Downloaded NULD zip lands outside the temp kit dir** (`tools/package_edition.py:163-166` writes to `ROOT/build`, INFERRED from `:140` + tempfile semantics) and is not cleaned up with the kit. Minor; not filed.
- **`gfx/ra_mark.png`** is extracted but unconsumed in non-RA korium builds (the same archive serves RA builds, where all 100 entries are consumed). Splitting the archive per edition would add machinery for one file. Not filed (worker §9).
- **`error.c` / `version.c` / `version_features.c` are EMPTY in both configs** (`MBEDTLS_ERROR_C` / `MBEDTLS_VERSION_C` live only in the unused stock config): `mbedtls_strerror()` does not exist in either build. Neither consumer calls it today — a future diagnostic call would fail at link time, not compile time. Recorded under RIPTOPL-501's census; not separately filed.

## 5. Residue — what remains unread in this subset, exactly

1. **mbedTLS pristine bodies** (109 `.c` + 162 headers beyond census/spot/region reads): closed for divergence by the executed zero-diff; a defect-hunt inside pristine upstream is outside the vendored lens by the task's own rule. Reopens only if the tree ever diverges from its archive (which RIPTOPL-502's fix would make CI-visible).
2. **Korium zip entry contents**: 98 PNGs (binary) + 2 `.cfg` theme configs (text, ~2 files) — the only genuinely unread *text* left in the subset; the `.cfg` pair is a small, well-defined remainder if a future pass wants the theme-config semantics.
3. **The two AFL LICENSE texts** (166 lines each, byte-identical copies): stock licence text, not line-read; licences are settled per the audit's standing rules.
4. **The pinned NULD runtime's contents**: not in this repo; binary-class, belongs to the round-9 binary/IRX pass, not this one.
5. **No PS2 build or link was run in this VM** (no PS2SDK): every "unpulled from the archive" statement is grep + pull-semantics INFERRED, and RIPTOPL-501's build-time saving is unmeasured. Hardware behaviour of everything above is, as in every round, unverified.
