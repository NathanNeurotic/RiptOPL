# Sable — RiptOPL ROUND 9 (third_party remainder line-read, pass B)

**Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` (rebuild/main) — clone HEAD == pin and live head (`git ls-remote origin rebuild/main`) re-verified equal at pass start and end; no delta, no revalidations owed. Clone git-clean throughout; read-only held.
**Scope:** the round-9 vendored lens — findings only where vendored material is (a) dead in this build, (b) locally divergent from upstream in a way that introduces a defect, (c) shipped size/RAM cost without use, or (d) locally integrated/configured wrong. Pristine upstream defects are unnumbered observations, not findings.
**Exact set:** `third_party/` contains exactly 8 entries. The stacks pass owns five (`mbedtls`, `korium`, `orbit-neutrino`, `ps2sdk-ppctty`, `ps2sdk-socket`). This pass owns the remaining three, read in full: **`third_party/PSUManager`, `third_party/ca-roots`, `third_party/fonts`** — 18 files, 52,868 bytes. No overlap ambiguity arose: nothing in these three trees belongs to the stacks set. Note on the brief's fourth named item: **there is no `third_party/Rules.bin.make`** at this pin — the tree's only `Rules.bin.make` is `modules/Rules.bin.make`, which belongs to the modules pass, not this one.

**Result: 2 new findings, RIPTOPL-600…601** (grep-verified against this file's headings — see the count line at the end).

## Findings index

| ID | Component | Title (short) | Cat |
|---|---|---|---|
| RIPTOPL-600 | ca-roots | Both Sectigo roots are embedded into RA builds that have no update channel and therefore contain no code that can reference them | 4 |
| RIPTOPL-601 | PSUManager | The vendored CMakeLists and its 142-line test suite are dead in this build — no CMake project, workflow, or script in RiptOPL ever builds or runs them | 7 |

---

### Title: Both Sectigo roots are embedded into RA builds that have no update channel and therefore contain no code that can reference them
- **Category:** (4) Size/memory — shipped bytes without use, arising from a local integration condition (lens c/d)
- **Locations:**
  - Data: `third_party/ca-roots/sectigo-e46.der` (574 B), `third_party/ca-roots/sectigo-r46.der` (1,422 B) — 1,996 B total
  - Generation condition: `tools/assets.py:166-172` — the embed block is gated on `RIPTOPL_RA == "1" or RIPTOPL_UPDATER == "1"`, but the per-file test at :170 is `if os.environ.get("RIPTOPL_RA") == "1" or der.stem in UPDATER_ROOTS:` — RA alone admits **every** `*.der` in the directory
  - Env wiring: `tools/build_edition.py:203` sets `RIPTOPL_UPDATER` from `update_channel(...)`; `tools/build_edition.py:52-66` returns `None` for **all lite editions and all non-standard configurations** — so lite-ra and every RA variant/debug configuration run embed with `RIPTOPL_RA=1`, `RIPTOPL_UPDATER=0`
  - Sole consumers, both compiled only under the updater define: extern declarations `src/sources/ratls_ps2.cpp:63-66` (`#ifdef RIPTOPL_UPDATER`) and the only use, the `raHttpsDownload` anchor table at `src/sources/ratls_ps2.cpp:251-264` (inside `#ifdef RIPTOPL_UPDATER` at :251). The RA lookup's own anchors (`src/sources/ratls_ps2.cpp:208-212`, `#ifdef RETROACHIEVEMENTS`) list only the three GTS roots and the two ISRG roots
  - Compilation into the ELF: `ps2.yaml:641` — the main EE target's source list ends with the glob `"asm/*.c"`, so every file `embed` generates is compiled into the main target
- **Problem (READ generation + unreferenceability; INFERRED final ELF retention):** In an RA build without an update channel, `embed` generates `asm/ratls_root_sectigoe46.c` and `asm/ratls_root_sectigor46.c` from these two DER files, the build compiles them into the main EE target, and no compiled code in that build can name their symbols — the only externs and the only anchor table that use them are preprocessed out with `RIPTOPL_UPDATER`. The shipped editions affected: lite-ra (a published lite edition whose whole point is a small ELF) and all RA non-standard configurations. Standard RA editions are unaffected — they have a channel, define `RIPTOPL_UPDATER`, and use the pair for github.com (READ at `ratls_ps2.cpp:261-264`). Whether the unreferenced arrays survive into the final linked ELF depends on the ps2build main target's linker garbage-collection behaviour, which cannot be settled from source in this VM (no PS2 build exists here) — INFERRED. The generation and compilation of dead data, and the absence of any possible referencer, are READ.
- **Fix (LESS):** Narrow the per-file condition at `tools/assets.py:170` so the Sectigo pair is emitted only when the updater is on — e.g. admit a file when `(RIPTOPL_RA == "1" and der.stem not in the Sectigo pair) or der.stem in UPDATER_ROOTS` under the existing :166 gate. No new list or machinery: `UPDATER_ROOTS` (`tools/assets.py:129`) already names the four updater roots, and the RA five are exactly the directory minus the Sectigo pair.
- **Benefit:** 1,996 B of DER (plus bin2c array overhead) no longer generated or compiled into lite-ra and RA variant/debug ELFs; the embed output for every flavour then matches exactly the anchors its compiled code can use.
- **Effort:** S (one condition in one line)
- **Risk:** Low — standard RA and updater builds are byte-identical before and after; the RA lookup's anchor set (GTS×3 + ISRG×2) is untouched.
- **Confidence:** High on the mechanism (embed condition, edition matrix, ifdef-gated consumers, and the `asm/*.c` glob are all READ); INFERRED on final linked-ELF byte retention, as stated.

### Title: The vendored PSUManager CMakeLists and its 142-line test suite are dead in this build — no CMake project, workflow, or script in RiptOPL ever builds or runs them
- **Category:** (7) Build/CI/repo clutter — vendored build scaffolding dead in this build (lens a)
- **Locations:**
  - `third_party/PSUManager/src/core/CMakeLists.txt:1-13` (defines `psu-core` and, under `BUILD_TESTING`, `psu-core-tests`)
  - `third_party/PSUManager/src/core/tests/PsuArchiveTests.cpp:1-142` (the suite only that CMakeLists builds)
- **Problem (READ, negatives verified by whole-tree grep):** Nothing in RiptOPL consumes this CMakeLists. There is no root `CMakeLists.txt`; a whole-tree grep for `add_subdirectory` finds only mbedtls-internal lines (`third_party/mbedtls/library/CMakeLists.txt:344`); a grep of `.github/workflows/` for `cmake` returns no files; and `PsuArchiveTests`, `psu-core-tests`, and `BUILD_TESTING` have **zero references anywhere outside the vendored tree itself**. The vendored core's only in-repo consumer, the host checker `.github/scripts/verify_psu_with_psumanager.cpp`, bypasses CMake entirely — it pulls the headers in by relative path (`verify_psu_with_psumanager.cpp:13`: `#include "../../third_party/PSUManager/src/core/PsuArchive.h"`) and is itself never invoked (that is RIPTOPL-286, already filed; not re-filed here). So the one suite that would catch a bad future re-vendor of this format code — and it is a genuinely non-vacuous suite: byte-identical round-trip including a multi-cluster file, timestamp preservation, garbage and path-traversal rejection (tests at `PsuArchiveTests.cpp:36-125`) — runs nowhere. The vendored README's stated reason for keeping the tests is preservation for future maintenance (`third_party/PSUManager/README.md:15-16`), which is a legitimate intent; the finding is the narrower, provable one: as build scaffolding in *this* repo they are inert, and the `BUILD_TESTING` wiring implies a runnable suite that no RiptOPL job runs.
- **Fix (LESS — deletion):** Delete `third_party/PSUManager/src/core/CMakeLists.txt` and `third_party/PSUManager/src/core/tests/` from the vendored copy (155 lines). Upstream retains both at the pinned commit — the vendored README (`third_party/PSUManager/README.md:4-6`) already records the exact upstream commit to re-fetch them from. The alternative (wiring the suite into the host test runner) is *more* machinery and is justified only if the maintainer wants the suite live; the audit's recommendation under LESS-NOT-MORE is deletion.
- **Benefit:** The vendored copy becomes exactly what this repo's build actually uses (2 `.cpp` + 3 `.h` + licence + README); no dead build file advertising a test target that cannot run here.
- **Effort:** S
- **Risk:** Low — provably unreferenced; recoverable from the pinned upstream commit at any time.
- **Confidence:** High — every negative above is a whole-tree grep executed at the pin.

---

## Component verdicts (built / not built, from the build files)

**`third_party/PSUManager` — NOT BUILT into anything at the pin.** Host-side release tooling only, and even there only potentially: its sole in-repo consumer is the never-invoked checker of RIPTOPL-286, which compiles `PsuArchive.cpp`/`PsuDate.cpp` as loose sources, not via the vendored CMake. Never linked into a PS2 ELF, never in installable ZIPs (the vendored README's claim to that effect, `third_party/PSUManager/README.md:16-17`, is consistent with everything found). **Divergence: NONE — verified by execution.** The vendored `src/core/` was diffed recursively against upstream `techwritescode/PSUManager` at the README's pinned commit `6c4d371ebda0709814c87c4b01f9a1a5b817c71b` (which is also upstream's current HEAD): `diff -r` exit 0, all 8 files byte-identical. The README's "copied without modification" claim is true. Consequently there are no lens-(b) findings in this tree; the source-level defects found in the line-read are upstream's and are recorded as observations below.

**`third_party/ca-roots` — BUILT (as embedded data), with one dead-weight exception (RIPTOPL-600).** `tools/assets.py embed` turns each `.der` into `asm/ratls_root_<name>.c` via bin2c (`tools/assets.py:169-172`); the main EE target compiles `asm/*.c` (`ps2.yaml:641`); consumers are the anchor tables in `src/sources/ratls_ps2.cpp:208-212` (RA: GTS×3 + ISRG×2) and `:261-264` (updater: Sectigo×2 + ISRG×2). Per flavour: RA standard — 7 generated, 7 usable; RA without a channel — 7 generated, 5 usable (→ RIPTOPL-600); non-RA with a channel — 4 generated, 4 used; no-TLS flavours — 0 generated. **Data audit (executed, all clean):** 7 certificates, 7,351 B total; `openssl x509` parse of every file — all self-signed roots, subjects matching the README table exactly (GTS Root R1/R3/R4, ISRG Root X1/X2, Sectigo E46/R46), **no duplicates** (7 distinct MD5s — the two 525-byte GTS files are different certificates), **none expired and none not-yet-valid** at the audit date (notAfter 2035-06-04 … 2046-03-21, all notBefore in the past), and **all 7 SHA-256 fingerprints match the README's table byte-for-byte**. The set is also pinned by `.github/scripts/test_mbedtls_vendor.py`, which asserts the directory contains exactly these 7 files and that each fingerprint appears in the README. The one structural note: `assets.py:169` globs `*.der`, so any file dropped into the directory would be auto-embedded into RA builds — mitigated today by that test's exact-listing assertion; recorded as an observation, not a finding.

**`third_party/fonts` — BUILT into every EE build, and used.** One file, `PoeVeticaNew.ttf` (17,380 B), the only font file anywhere in the tree (whole-tree census: no other `.ttf`/`.otf`). Embedded unconditionally by both build paths: `Makefile:176` (`GFX_OBJS` includes `poeveticanew.o`) with the bin2c rule at `Makefile:1130-1131`, and `tools/assets.py:181` in `embed`. Consumed as the built-in default font: `src/gfx/fntsys.cpp:87-88` (externs), `:377-379` (a NULL path returns the embedded font — themes supply their own only when a path is given, `:380-386`). **Data audit (executed):** valid TTF, 14 tables, 242 glyphs, cmap of 245 codepoints spanning U+0020–U+FB02 — Latin, Latin-1 supplement, punctuation, vulgar fractions, and the `fi`/`fl` ligatures; name table consistent (family "POE Vetica New"). Coverage is **Latin-only**: no Cyrillic, Greek, CJK, kana, Hebrew, or Arabic codepoints — a data fact about the built-in font (non-Latin languages necessarily rely on theme-supplied fonts via the `fntReadData` path); not a defect under the vendored lens, recorded as an observation. No duplicate copy of the font exists in-tree; no size-without-use (it is the default UI font in every flavour).

## Unnumbered observations (pristine upstream — vendored PSUManager core, byte-identical to upstream, see verdict above)

1. **`PsuArchive::Create` silently truncates a >4 GiB source file.** `third_party/PSUManager/src/core/PsuArchive.cpp:196` casts the 64-bit size to `uint32_t` for the dirent length while `CopyFileBytes` (`:197`) writes the full size and the padding at `:198` is computed from the truncated value — a malformed archive. Unreachable in practice: the on-disk format's length field is itself 32-bit (`PsuFormat.h:23`), and `.psu` files are memory-card saves. Upstream design bound, host tool only.
2. **`FromRawDate` never validates on-disk date fields.** `third_party/PSUManager/src/core/PsuDate.cpp:32-42` feeds the raw fields (month 0 or >12, day 0, year up to 65535) straight to `mktime`, which normalizes or returns −1; a corrupt archive therefore yields silently wrong entry timestamps instead of a format error. `Load` validates names and modes but not dates.
3. **`Load` bounds the entry count but not each entry's data extent** (`PsuArchive.cpp:103-107` vs `:128-141`); a truncated final entry is caught only indirectly by the stream-state check after the padding seek. Behaviour verified by reading; the vendored tests cover the header-garbage case only.
4. Checked-clean, for the record: extraction path traversal is guarded (`ValidateStoredName` rejects separators and `..` at both `Load`, `PsuArchive.cpp:124`, and `Create`, `:171-175`, before `ExtractAll` joins names at `:157`); `WriteZeroPadding`'s count is always < `kClusterSize` (`:198`, round-up minus size ∈ [0, 1023]); `MakeDirEntry`'s name copy (`:44`) is bounded by the ≤447-char validation plus the zero-initialised entry. No defect found in `PsuDate.cpp`'s stat/utime timestamp preservation or in `PsuFormat.h`'s 512-byte record layout (static-asserted at `PsuFormat.h:36`).

## Ledger — exact set, all read in full

| File | Lines / bytes | How read |
|---|---|---|
| `third_party/PSUManager/LICENSE` | 121 lines | full (CC0 text; licences are settled — no findings taken from it) |
| `third_party/PSUManager/README.md` | 18 lines | full; claims verified (pin exact, diff empty) |
| `third_party/PSUManager/src/core/CMakeLists.txt` | 13 lines | full |
| `third_party/PSUManager/src/core/PsuArchive.cpp` | 200 lines | full |
| `third_party/PSUManager/src/core/PsuArchive.h` | 54 lines | full |
| `third_party/PSUManager/src/core/PsuDate.cpp` | 87 lines | full |
| `third_party/PSUManager/src/core/PsuDate.h` | 19 lines | full |
| `third_party/PSUManager/src/core/PsuFormat.h` | 57 lines | full |
| `third_party/PSUManager/src/core/tests/PsuArchiveTests.cpp` | 142 lines | full |
| `third_party/ca-roots/README.md` | 26 lines | full; every row verified against the DER bytes |
| `third_party/ca-roots/gtsr1.der` | 1,371 B | openssl parse: subject/issuer/dates/SHA-256 |
| `third_party/ca-roots/gtsr3.der` | 525 B | openssl parse |
| `third_party/ca-roots/gtsr4.der` | 525 B | openssl parse |
| `third_party/ca-roots/isrgrootx1.der` | 1,391 B | openssl parse |
| `third_party/ca-roots/isrg-root-x2.der` | 543 B | openssl parse |
| `third_party/ca-roots/sectigo-e46.der` | 574 B | openssl parse |
| `third_party/ca-roots/sectigo-r46.der` | 1,422 B | openssl parse |
| `third_party/fonts/PoeVeticaNew.ttf` | 17,380 B | fontTools parse: all 14 tables, cmap (245 codepoints) enumerated, name table |

PSUManager text total: 711 lines, all read. Data artefacts total: 7,351 B (certs) + 17,380 B (font), all parsed. Integration evidence read outside the set (for the built/not-built verdicts only): `tools/assets.py:125-190,255-285`, `tools/build_edition.py:18-66,85-150,195-215`, `ps2.yaml:641`, `src/sources/ratls_ps2.cpp:1-70,195-275`, `src/gfx/fntsys.cpp:80-95,365-390`, `Makefile:176,1120-1145`, `.github/scripts/verify_psu_with_psumanager.cpp:1-40`, `.github/scripts/test_mbedtls_vendor.py` (roots section). Upstream comparison: `techwritescode/PSUManager@6c4d371` cloned outside the repo and `diff -r`'d (exit 0), then deleted.

**COUNT (grep-verified against this file's finding headings): 2 findings — RIPTOPL-600, RIPTOPL-601.**

**Residue:** none in this set. All 18 files are read/parsed in full, both data artefacts are audited, divergence is settled by execution, and built/not-built is determined for all three components from the build files. Out of scope by assignment and untouched here: the five stacks-pass trees, `modules/Rules.bin.make`, and any ELF-retention proof for RIPTOPL-600 (requires a PS2 build, which does not exist in this VM — nothing in this pass is hardware- or build-verified).
