# RiptOPL Round 9 — Korium + Orbit/Neutrino local integration

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` — verified `git rev-parse HEAD` equals the pin and `git status --porcelain` is empty at start and finish of this pass.
Scope note: the coordinator already executed the korium digest pass (archive digest + all 100 per-file digests match `UPSTREAM.json`, no unlisted/missing entries) and verified `RA_ORBIT_SNAPSHOT_MAX 1216` at `src/sources/supportbase.cpp:1879` against `release.json`. Neither was redone. This section line-reads the **local integration** of the two pins only.

---

## 1. Per-file read ledger

| File | Lines | Read in full |
|---|---|---|
| `tools/assets.py` | 270 | Y |
| `tools/package_edition.py` | 239 | Y |
| `.github/scripts/test_korium_builtin_assets.py` | 45 | Y |
| `.github/workflows/orbit-neutrino-watch.yml` | 88 | Y |
| `third_party/korium/README.md` | 13 | Y |
| `third_party/orbit-neutrino/README.md` | 38 | Y |
| `third_party/orbit-neutrino/release.json` | 9 | Y |
| `third_party/korium/UPSTREAM.json` | 109 (head + structure; digest values not re-verified, per coordinator pass) | Partial — keys/structure and first entries read; file/digest enumeration via script |
| `.github/scripts/test_package_orbit_neutrino.py` | 95 | Y |
| `.github/scripts/orbit_neutrino_watch.py` | 157 | Y |
| `tools/build_edition.py` | 335 | Y |
| `.github/scripts/package_release_app.py` | region 320–345 only | N — relevant region only |
| `.github/workflows/flavours.yml` | region 20–110 only | N — test-wiring region only |
| `.github/workflows/rolling-release.yml` | region 135–180 only | N — packaging region only |
| `.github/workflows/neutrino-watch.yml` | region 19–27 only (cron) | N — schedule comparison only |
| `src/platform/sysneutrino.cpp` | region 1045–1075 only | N — marker-check region only |
| `src/sources/supportbase.cpp` | regions 1875–1905 only | N — snapshot/handover region only (line 1879 per coordinator) |
| `include/textures.h` | region 100–155 only | N — texture enum region only |
| `src/gfx/textures.cpp` | region 330–365 only | N — RA_MARK table region only |

Established by script (not a line-read): at the pin, `set(assets.zip namelist) == set(UPSTREAM.json["files"])`, 100 entries each; the consumed-entry computation in §2.3 was run against `tools/assets.py`'s own `PNG_ASSETS` list.

---

## 2. Korium integration trace (`tools/assets.py`)

### 2.1 When the archive is read at all

- `embed()` is the only consumer. The theme is chosen from the environment, not from the edition directly: `tools/assets.py:152-154` reads `RIPTOPL_THEME`, accepts only `korium`/`incebtion` (default `incebtion`), and exits on anything else.
- The archive branch runs only when `theme == "korium"`: `tools/assets.py:156-173`.
- `tools/build_edition.py:201-202` sets `RIPTOPL_THEME`/`RIPTOPL_RA` from the edition table and `tools/build_edition.py:208` runs `tools/assets.py embed` for every edition build. The edition→theme mapping is `tools/build_edition.py:19-24,34-35`:
  - theme `korium` (archive read): `korium`, `ra`, `lite`, `lite_ra`
  - theme `incebtion` (archive **not** read): `incebtion`, `ra_incebtion`
- There is no orbit/neutrino staging anywhere in `tools/assets.py` — no reference to `orbit`, `neutrino`, or `third_party/orbit-neutrino` in the file. Orbit staging lives solely in `tools/package_edition.py` (§4).

### 2.2 How the archive is consumed

`tools/assets.py:158-173`, in order:

1. `manifest = json.loads(UPSTREAM.json)` (`:159`).
2. Archive-level check: `hashlib.sha256(archive.read_bytes()) != manifest["archive_sha256"]` → `sys.exit("Korium asset archive digest mismatch")` (`:160-161`). **The manifest is consulted at build time**, in `embed()`, not only by tests.
3. Extraction target is `build/theme-assets/korium` (`:162`) — never the in-tree `gfx/` or `misc/` directories.
4. Extraction iterates **`manifest["files"].items()` only** (`:166`). For each listed name: the resolved destination must stay inside `theme_dir` or the build exits (`:167-169`); `package.read(name)` is hashed and compared to the manifest digest, mismatch → `sys.exit` (`:170-172`); only then is the file written (`:173`).

Consequences, all directly from that loop:

- **Entries present in the zip but absent from `manifest["files"]` are never read, never extracted, and never reported.** They are silently dropped. The archive digest binds the zip to `archive_sha256`, but nothing binds the zip's entry set to the `files` key set (see KO-3).
- **A manifest entry missing from the zip** makes `package.read(name)` raise `KeyError`, which is not caught in `embed()` — the build fails, but with a traceback rather than the clean `sys.exit` used for digest mismatches. Fail-closed, ugly exit. (Behaviour INFERRED from `zipfile` semantics + the absence of a handler; the code path itself was read.)
- Extraction is **not cleaned first**: `embed()` deletes stale `asm/*.c` (`tools/assets.py:146-148`) but never clears `build/theme-assets/korium`, so files extracted by an earlier manifest revision linger in a reused local build tree (see KO-4).

### 2.3 Which extracted entries are actually used

After extraction, `embed()` builds the PNG list at `tools/assets.py:182-201`:

- `coverapp` and `case_overlay` are removed from the list for korium builds (`:184`, comment at `:183`: Korium maps neither).
- Appended for korium builds (`:186`): `apps_case`, `case_ps1`, `disc_ps1`, `Players_1`…`Players_4`.
- `ra_mark` is appended after those, only when `RIPTOPL_RA == "1"` (`:187-188`).
- Per PNG (`:195-201`): the symbol name stays positional (`incebtion`→`background`, `ip`→`background_info` renaming at `:196`); the themed file `gfx/<name>.png` is used if it exists, otherwise the build falls back to the in-tree `gfx/<a>.png`. A special case at `:197-199` maps inceBtion's multi-dot `Index<p>_<n>` names onto korium's five single `Index_<p>` pictures.
- Of the `MISC` table, only `conf_theme_OPL` and `theme_coverflow` are taken from the theme dir in a korium build (`:206-208`); all other misc entries (including `theme_psbbn_flow`, `theme_grid`) stay in-tree.

Computed against the pin (script, §1): in an **RA korium build all 100 zip entries are consumed**; in a **non-RA korium build exactly one extracted entry is never consumed — `gfx/ra_mark.png`**. Conversely, 34 PNG names in a korium build fall back to in-tree graphics (the 24 button-glyph variants, `Index2_*`/`Index3_*`/`Index4_*` beyond the five-dot remap source, `psbbn_bg`, `techwritescode`, `grid_devices`) — by design, per the fallback at `:200`.

### 2.4 What `test_korium_builtin_assets.py` does and does not enforce

Does (`.github/scripts/test_korium_builtin_assets.py`):

- Opens `assets.zip`, decodes every `.cfg` entry, asserts at least one matches `attribute\s*=\s*Players` (`:15-18`).
- Asserts `src/gfx/themes.cpp`'s built-in-theme loop contains `#ifdef BUILTIN_KORIUM` and the `PLAYERS_1..PLAYERS_4` load loop (`:20-23`).
- Parses the `#ifdef BUILTIN_KORIUM` blocks of `src/gfx/textures.cpp` and asserts `COVERAPP_DEFAULT == &cover_png`, `CASE_OVERLAY2 == NULL`, `PLAYERS_<n> == &Players_<n>_png` (`:25-33`).
- Asserts `tools/assets.py` contains the verbatim `coverapp`/`case_overlay` exclusion line (`:36-37`) and that `gfx/coverapp.png` is not a key in `UPSTREAM.json["files"]` (`:38`).
- Asserts `src/core/settings.cpp` defaults background art to 0 under `BUILTIN_KORIUM` and 1 otherwise (`:40-43`).

Does **not**:

- Import `hashlib` at all; it checks **neither the archive digest nor any of the 100 per-file digests**.
- Check entry completeness in either direction (zip set vs `files` set), beyond the single negative key check for `gfx/coverapp.png` (`:38`).
- Read `UPSTREAM.json`'s `repository`/`commit`/`branch` fields.
- Exercise `embed()` or the extraction/path-safety code in `tools/assets.py` — its only contact with that file is one verbatim string match (`:37`).
- **It is not wired into CI at all** — see §3.

---

## 3. Korium enforcement-gap analysis (who verifies the pin, where)

| Check | Enforced by | Where |
|---|---|---|
| Archive SHA-256 vs `archive_sha256` | `tools/assets.py` `embed()` at build time | `tools/assets.py:160-161` — only runs when a korium-theme edition is built (`tools/build_edition.py:201,208`) |
| All 100 per-file SHA-256 digests | `tools/assets.py` `embed()` at build time | `tools/assets.py:166-172` — same condition as above |
| Extraction path safety (no escape from `build/theme-assets/korium`) | `tools/assets.py` `embed()` | `tools/assets.py:167-169` |
| Zip entry set == manifest `files` key set (completeness) | **Nobody** | Absence: the extraction loop iterates the manifest only (`tools/assets.py:166`); the test checks one absent key only (`.github/scripts/test_korium_builtin_assets.py:38`) |
| `repository`/`commit`/`branch` provenance fields in `UPSTREAM.json` | **Nobody** | No in-tree reader of those fields was found by grep |
| Players-badge / own-cover / BGArt-off theme contract | `.github/scripts/test_korium_builtin_assets.py` | **Not wired into any workflow** — grep for `test_korium` across `.github/workflows/` returns nothing; `.github/workflows/flavours.yml:28-62` enumerates ~35 test scripts by name (including both orbit tests at `:60-61`) and this file is not among them, and neither of flavours.yml's two glob loops (`:65` `test_ripps2_*.py`, `:103` `test_ilink_*.py` + named files) matches it |

Net: the korium pin's **digests are genuinely enforced, but only as a side effect of building a korium-theme edition**. Rolling workflows do build those editions (`.github/workflows/rolling-release.yml`, region read: `tools/build_edition.py` invoked per matrix edition), so in practice the digest gate runs in CI — but no CI job exists whose job is "verify the korium pin", the dedicated contract test is orphaned, and completeness/provenance are unenforced anywhere.

---

## 4. Orbit/Neutrino staging trace (`tools/package_edition.py`)

### 4.1 Where the NULD zip comes from at package time

- `package()` runs the orbit path only for RA editions: `tools/package_edition.py:161-168`.
- The pin is loaded from `third_party/orbit-neutrino/release.json` via `ORBIT_PIN` (`:24`, read at `:162`).
- If `--orbit-archive` was not supplied (`:163-166`): the target path is `kit.parent / pin["asset"]` and the bytes are fetched with `install_ports.download` from the URL constructed out of the pin itself — `https://github.com/{pin['repository']}/releases/download/{pin['tag']}/{pin['asset']}` (`:165`). (Note: `kit` is the TemporaryDirectory itself, so `kit.parent` is `ROOT/build` — the downloaded zip is written outside the temp kit dir and is not cleaned up with it. INFERRED from `:140` + `tempfile` semantics; minor, noted for completeness, not filed.)
- Rolling CI relies on exactly this: `.github/workflows/rolling-release.yml` (region 152–173) skips the upstream Neutrino `.7z` download for `ra*` editions because "the RA kit independently downloads and verifies the pinned ORBIT runtime", and calls `tools/package_edition.py <edition>` with no `--orbit-archive`.

### 4.2 `stage_orbit` verification — is `release.json`'s sha256 actually verified before extraction?

Yes. `stage_orbit(archive, destination, pin)` at `tools/package_edition.py:28-78`:

1. **Digest first, before any write**: `hashlib.sha256(archive.read_bytes()) != pin["sha256"]` → `ValueError("ORBIT release digest mismatch")` (`:30-31`). This happens before the zip is even opened.
2. Full in-memory read with path safety (`:33-45`): absolute paths, `..` parts, or backslashes in `entry.orig_filename` → `ValueError`; directories and any entry whose second path component is not `neutrino` are **skipped** (comment `:38`: "the GPL ORBIT launcher beside neutrino/ is not ours to ship"); the top-level folder name itself (`path.parts[0]`) is **not** checked — any prefix is accepted; a duplicate relative path → `ValueError`. The payload dict is keyed by the path **below** `neutrino/`.
3. Required entries, each required **non-empty** (`payload.get(name)` falsy check), `:41-44`: `neutrino.elf`, `modules/ee_core.elf`, `modules/raagent.irx`, `modules/IEEE1394_bd_mini.irx`, `modules/iLinkman.irx`, `config/system.toml`, `config/bsd-udpfs.toml`, `LICENSE`.
4. Content check: `b"-ra=<file>"` must appear in `payload["neutrino.elf"]` (`:45-46`).
5. Only after all of the above does anything get written (`:47-50`), followed by the generated provenance files: `ORBIT-RA.json` (the pin, verbatim JSON dump, `:51`), `ORBIT-RA.txt` (the `RiptOPL ORBIT RA handoff v1` marker whose first line `sysNeutrinoRARuntime` compares — `src/platform/sysneutrino.cpp:1054-1063`, region read), and `RA-EDITION-NEUTRINO.txt` (`:52-69`).
6. This behaviour is itself test-covered and CI-wired: `.github/scripts/test_package_orbit_neutrino.py:31-84` (stages-only-`neutrino/`, marker contract vs `sysneutrino.cpp`, snapshot-limit equality vs the pin, refusal-before-any-write incl. digest/missing/unsafe/duplicate cases, and the pin's literal repository/tag/sha256 at `:86-90`), run by `.github/workflows/flavours.yml:60`.

Contrast with korium: orbit has a dedicated, CI-wired staging test; korium's dedicated test is unwired (§3).

### 4.3 The IEEE1394 replacement — actually a pair

After either staging branch, `package()` unconditionally replaces **two** modules in `kit/neutrino/modules/` (`tools/package_edition.py:172-186`): `IEEE1394_bd_mini.irx` (manifest key `ilink_bd_sha256`) **and** `iLinkman.irx` (`ilink_manager_sha256`). Each replacement first requires that upstream shipped the file and that RiptOPL's staged build matches the build manifest's digest, and refuses to package otherwise (`:180-184`); the comment at `:172-176` explains why both: the manager "carries the TX cancellation, alarm and receive-FIFO fixes, so the driver alone leaves them out". This applies identically to the standard kit (upstream Neutrino branch, `:169-179`) and the RA kit. Note the staged orbit copies of both modules are therefore required-present by `stage_orbit` (`:41-42`) but their bytes are always discarded and overwritten here.

Downstream enforcement of the RA/standard split: `.github/scripts/package_release_app.py:333-336` (region read) — RA packages must contain `neutrino/ORBIT-RA.json`, `neutrino/ORBIT-RA.txt`, `neutrino/RA-EDITION-NEUTRINO.txt`, `neutrino/LICENSE`, `neutrino/modules/ee_core.elf`, `neutrino/modules/raagent.irx`; a non-RA package containing any `raagent`/`ORBIT-RA.`/`RA-EDITION-NEUTRINO.` name is rejected. `tools/package_edition.py:226-230` additionally re-scans the finished standard-kit zip for RA leakage.

---

## 5. Claims tables

### 5.1 `third_party/orbit-neutrino/README.md` (38 lines)

| # | Claim (README line) | Verdict | Evidence |
|---|---|---|---|
| O-1 | Pin is NULD v1.2.12, source `danielnuld/nuld` at `0a6bedb6…` (README:3-4) | MATCH | `third_party/orbit-neutrino/release.json:2-5` — repository, tag, source_commit, asset all agree |
| O-2 | Daniel/ORBIT supplies loader, EE core, `raagent.irx`; Neutrino by Maximus32; protocol/agent by hacan359/xeRAbora (README:6-8) | UNVERIFIABLE | Upstream authorship; in-tree text merely repeats it (`tools/package_edition.py:53-56`). No in-tree proof either way |
| O-3 | Fork is AFL-3.0; its LICENSE travels with the runtime (README:8-9) | MATCH (in-tree half) | `LICENSE` is a required non-empty staged entry (`tools/package_edition.py:41-44`) and a required RA-kit file (`.github/scripts/package_release_app.py:333-334`). The licence's own text was not read |
| O-4 | The GPL-3.0 ORBIT launcher is neither bundled nor linked (README:9-10) | MATCH (bundled) / UNVERIFIABLE (linked) | Bundled: non-`neutrino/` entries are skipped (`tools/package_edition.py:37-38`) and the staging test asserts the launcher is absent (`.github/scripts/test_package_orbit_neutrino.py:40-43`). Linked: binary property, not checkable in-tree |
| O-5 | `stage_orbit` checks the whole ZIP against `release.json`'s SHA-256 and every required entry before writing, then extracts only `neutrino/` (README:12-13) | MATCH | `tools/package_edition.py:30-31` (digest before open/write), `:41-46` (8 required non-empty entries + `-ra=` check), `:37-38` (only `neutrino/` staged), writes begin at `:47` |
| O-6 | "One file is replaced afterwards, exactly as in the standard kit: `modules/IEEE1394_bd_mini.irx`" (README:13-15) | **CONTRADICTED** | `tools/package_edition.py:180-186` replaces **two** files — `IEEE1394_bd_mini.irx` **and** `iLinkman.irx` — in both kits; the code comment (`:172-176`) says the pair is the point. The kit's own generated label contradicts the README too: `RA-EDITION-NEUTRINO.txt` text at `tools/package_edition.py:66-67` says "Only modules/iLinkman.irx and modules/IEEE1394_bd_mini.irx are replaced". See KO-1 |
| O-7 | Standard kits keep official upstream Neutrino; only the RA kit may contain `raagent.irx`, `ORBIT-RA.*`, `RA-EDITION-NEUTRINO.txt`; `package_release_app.py` enforces both directions (README:15-18) | MATCH | `.github/scripts/package_release_app.py:333-336` (region read); plus the post-zip leak scan at `tools/package_edition.py:226-230` |
| O-8 | `sbNeutrinoPrepareRA` hands over only when `sysNeutrinoRARuntime` finds `ORBIT-RA.txt` starting with `RiptOPL ORBIT RA handoff v1` and `raagent.irx` present (README:20-23) | MATCH | `src/platform/sysneutrino.cpp:1054-1067` (region read): marker `memcmp` over the first line, then `raagent.irx` open check; called from `src/sources/supportbase.cpp:1897` (region read) |
| O-9 | v1.2.12 sends each snapshot in one packet; sets whose direct values exceed 1,216 bytes stay untracked (README:23-24) | MATCH (in-tree half) | `release.json:7` `snapshot_bytes_max: 1216`; `src/sources/supportbase.cpp:1879` `RA_ORBIT_SNAPSHOT_MAX 1216` (coordinator-verified) and `:1894-1897` (region read) gates handover on `GetWatchBytes() > RA_ORBIT_SNAPSHOT_MAX`. The upstream one-packet behaviour itself is UNVERIFIABLE in-tree |
| O-10 | It reads direct entries only, so a pointer-chain tail is ignored and chain achievements do not unlock (README:24-26) | MATCH (in-tree half) | `release.json:8` `pointer_chains: false`; `src/sources/supportbase.cpp:1894-1896` (region read) states and implements the same; `.github/scripts/test_package_orbit_neutrino.py:65-69` asserts both. Upstream behaviour UNVERIFIABLE in-tree |
| O-11 | It speaks the xeRAbora wire protocol, which Caduceus's engine also receives, so both RA modes use it (README:26-27) | MATCH (in-tree half) | `src/sources/supportbase.cpp:1888` (region read): both `RA_MODE_XERABORA` and `RA_MODE_CADUCEUS` pass the same gate. Wire compatibility with the upstream agent is UNVERIFIABLE in-tree |
| O-12 | v1.2.12 can pause the game ~2.5 s under an unlock box on `RAU1 <id> <points>`; RiptOPL hands over no titles and does not claim console unlock cards (README:27-29) | UNVERIFIABLE | No in-tree code or test read in this pass speaks to the 2.5 s pause or `RAU1`; the "RiptOPL hands over no titles" half is an absence claim not positively provable from the regions read |
| O-13 | ORBIT is Neutrino v1.8.0 based; Neutrino cheats are not available through it (README:29) | MATCH (in-tree half) | The generated kit label asserts the same base version (`tools/package_edition.py:64-68`) and the staging test asserts that label (`.github/scripts/test_package_orbit_neutrino.py:44-47`). The upstream base version itself is UNVERIFIABLE in-tree |
| O-14 | On a pin move, update `release.json`, `RA_ORBIT_SNAPSHOT_MAX` and the tests together (README:31-33) | MATCH (as a described obligation; the couplings exist) | All three exist and are cross-asserted: `.github/scripts/test_package_orbit_neutrino.py:65-69` (limit vs pin), `:86-90` (pin literals) |
| O-15 | `orbit-neutrino-watch.yml` checks ORBIT's latest release on Neutrino Watch's schedule and opens the "ORBIT Neutrino RA runtime update" issue when it differs; it never edits the pin (README:34-36) | MATCH | See §6. One omission, not a contradiction: the workflow also *refreshes* an open issue and *closes* it when the pin matches again (`.github/workflows/orbit-neutrino-watch.yml:71-84`) |
| O-16 | "NOT TESTED on hardware" (README:38) | UNVERIFIABLE | Status statement; nothing in-tree can confirm or refute it |

Tally: 16 claims — MATCH 7, MATCH-with-unverifiable-upstream-half 4 (O-3 counted MATCH, O-4/O-9/O-10/O-11/O-13 partial — see rows), CONTRADICTED 1 (O-6), UNVERIFIABLE 2 (O-2, O-12, O-16 — 3 rows fully unverifiable). Precise count by row: MATCH 8 (O-1, O-3, O-5, O-7, O-8, O-14, O-15, plus O-4's bundled half), partial MATCH/UNVERIFIABLE 5 (O-4 linked half, O-9, O-10, O-11, O-13), CONTRADICTED 1 (O-6), UNVERIFIABLE 3 (O-2, O-12, O-16). Rows total 16 with O-4 split across two categories.

### 5.2 `third_party/korium/README.md` (13 lines)

| # | Claim (README line) | Verdict | Evidence |
|---|---|---|---|
| K-1 | Graphics by Berion; original theme script by KrahJohlito and Ripto (README:3) | UNVERIFIABLE | Attribution; no in-tree corroboration read (the zip's retained config comments, README:5-6, were not opened) |
| K-2 | Built-in port preserved from NathanNeurotic/Open-PS2-Loader's Korium branch at the exact revision in `UPSTREAM.json` (README:4-5) | MATCH (manifest half) | `third_party/korium/UPSTREAM.json:2-4` records exactly that repository, commit `7962b278…`, branch `test/korium-audit-remediation`. Whether the zip's contents truly derive from that revision is UNVERIFIABLE in-tree |
| K-3 | Each file and the archive have SHA-256 digests (README:5-6) | MATCH | `UPSTREAM.json:5` (`archive_sha256`) + `files` map (100 entries, structure read); coordinator verified all values |
| K-4 | The repository's AFL-3.0 license applies to this downstream port (README:6) | UNVERIFIABLE | Legal statement; no licence text for the theme was read in this pass |
| K-5 | `tools/assets.py` reads this archive for Korium and RetroAchievements builds (README:8) | MATCH with a material exception (borderline CONTRADICTED as written) | True for editions `korium` and `ra` (`tools/build_edition.py:19-20,201,208` + `tools/assets.py:156`). But the RA edition `ra_incebtion` (RetroAchievements-IncebtIon) has theme `incebtion` and never reads the archive (`tools/build_edition.py:23-24`), and the two Lite editions that *do* read it (`lite`, `lite_ra`, `:34-35`) go unmentioned. See KO-2 |
| K-6 | It does not overwrite the IncebtIon source graphics or configs (README:8-9) | MATCH | Extraction goes to `build/theme-assets/korium` (`tools/assets.py:162`); in-tree `gfx/`/`misc/` paths are only ever read as fallback/source (`:200`, `:206-208`) |
| K-7 | Backgrounds keep their existing enum positions (README:9-10) | MATCH | In `embed()` the background symbols keep their `PNG_ASSETS` slots — `incebtion`/`ip` are renamed to `background`/`background_info` only as themed *file* names, while the emitted symbol stays `f"{a}_png"` (`tools/assets.py:195-201`). (The C++ enum itself, `include/textures.h` region read, was not exhaustively position-checked — that half is INFERRED from the assets.py ordering) |
| K-8 | Additional PS1 and players textures are appended before the guarded RA mark (README:10) | MATCH | Both layers agree: `tools/assets.py:186-188` appends `case_ps1`, `disc_ps1`, `Players_1..4` (plus `apps_case`) before the `RIPTOPL_RA`-guarded `ra_mark` append; `include/textures.h:125-132` places `CASE_PS1`, `DISC_PS1`, `PLAYERS_1..4` under `#ifdef BUILTIN_KORIUM`, ahead of `RA_MARK` under `#ifdef RETROACHIEVEMENTS` at `:147-151` |
| K-9 | "Update the archive and manifest together when deliberately importing another tested upstream theme revision" (README:10-11) | MATCH (as instruction; partially enforced) | The build refuses a mismatched pair (`tools/assets.py:160-161,170-172`), so updating one without the other fails a korium build — but completeness of the pair is not checked (KO-3), and nothing checks that the imported revision was "tested" |

Tally: 9 claims — MATCH 5 (K-3, K-6, K-7, K-8, K-9), MATCH-with-exception 1 (K-5), MATCH-manifest-half/UNVERIFIABLE-provenance 1 (K-2), UNVERIFIABLE 2 (K-1, K-4), CONTRADICTED 0 outright (K-5 is the borderline row).

---

## 6. Orbit watch workflow verification (`.github/workflows/orbit-neutrino-watch.yml`, 88 lines)

Against README claim O-15 and the workflow's own header (`:3-13`):

- **Schedule = Neutrino Watch's schedule: MATCH.** Cron `41 5 1,11,21 * *` (`:17-19`) is byte-identical to `.github/workflows/neutrino-watch.yml:26` (region read), and the comment at `:18` says so.
- **Checks the latest release, pre-releases included: MATCH.** The run queries all published releases, filters out drafts, requires an `(ORBIT|NULD)-*.zip` asset, and takes the newest by `published_at` (`:48-53`) — deliberately not `releases/latest`, which would skip pre-releases.
- **Opens an issue when it differs: MATCH, and more.** `.github/scripts/orbit_neutrino_watch.py` (read in full) decides `current`/`changed`/`unverified` by comparing tag and archive digest to the pin (`:56-73`); the workflow creates or refreshes the "ORBIT Neutrino RA runtime update" issue on `changed` (`orbit-neutrino-watch.yml:71-78`), closes it on `current` (`:79-84`), and when GitHub reports no asset digest it downloads the archive and hashes it itself before deciding (`:58-64`).
- **Never edits the pin: MATCH.** Workflow permissions are `contents: read` + `issues: write` only (`:21-23`); no step writes to the repository; the helper script's docstring and generated issue body both state the watch never edits the pin (`.github/scripts/orbit_neutrino_watch.py:9`, `:128-129`). The issue it opens *suggests* a new `release.json` for a human to apply (`:94-131`).
- Guard: the job only runs on `NathanNeurotic/RiptOPL` (`:32`). The watch's own tests are CI-wired (`.github/workflows/flavours.yml:61` runs `test_orbit_neutrino_watch.py`).

No contradiction found in this file.

---

## 7. Other in-tree consumers found by grep

| Consumer | What it does | Citation |
|---|---|---|
| `tools/build_edition.py` | Sets `RIPTOPL_THEME`/`RIPTOPL_RA` per edition and runs `assets.py embed` — the korium archive's only entry point | `tools/build_edition.py:19-24,34-35,201-208` |
| `.github/workflows/flavours.yml` | Runs both orbit tests; does **not** run the korium test | `:60-61` vs the full enumerated list `:28-62` |
| `.github/workflows/rolling-release.yml` (and `rolling-llvm.yml`, grep only — INFERRED identical) | RA packaging path: skips upstream Neutrino download, calls `package_edition.py` which downloads + verifies the pinned ORBIT zip itself | `rolling-release.yml` region 152–173 (read) |
| `.github/scripts/package_release_app.py` | Kit verification: requires orbit marker files in RA kits, rejects them in standard kits | `:333-336` (region read) |
| `.github/scripts/test_package_orbit_neutrino.py` | Dedicated staging/pin test for orbit | read in full, §4.2 |
| `.github/scripts/orbit_neutrino_watch.py` + `test_orbit_neutrino_watch.py` | The watch's decision logic and its tests | script read in full; test not read (existence + CI wiring at `flavours.yml:61` only) |
| `.github/scripts/normalize_release_notes.py`, `release_channel.sh` | Mention the korium *edition/channel* names only; do not touch `third_party/korium` | grep hits at `normalize_release_notes.py:84-86`, `release_channel.sh:6,13` — name-only, not consumers of the pin |
| `src/gfx/themes.cpp`, `src/gfx/textures.cpp`, `src/core/settings.cpp`, `include/textures.h` | Consume the `BUILTIN_KORIUM` build define, not the archive directly | via `.github/scripts/test_korium_builtin_assets.py:20-43` and regions read |

No other build definition, workflow, or packaging file references `third_party/korium` or `third_party/orbit-neutrino`.

---

## 8. Candidate findings (unnumbered — KO-* are local to this section)

### KO-1 — Orbit README says one i.LINK file is replaced; the packer replaces two
- **Category:** Doc-vs-code contradiction
- **Locations:** `third_party/orbit-neutrino/README.md:13-15`; `tools/package_edition.py:172-186`; `tools/package_edition.py:64-67`
- **Problem:** The README states "One file is replaced afterwards… `modules/IEEE1394_bd_mini.irx`". The code replaces `IEEE1394_bd_mini.irx` **and** `iLinkman.irx` in every kit, and its own comment explains the manager replacement is load-bearing (TX cancellation, alarm, receive-FIFO fixes — "the driver alone leaves them out"). The README also contradicts the label text the same program writes into every RA kit, which correctly names both files. A reviewer trusting the README would misunderstand what the shipped runtime actually is and could "fix" the packer to match the doc, silently dropping the manager fixes.
- **Fix:** Correct the README sentence to name both files and the reason (one sentence changed; LESS-NOT-MORE — no code change).
- **Benefit:** The pin's documentation matches the shipped bytes and the kit's own label.
- **Effort:** S
- **Risk:** Low (doc-only)
- **Confidence:** High — both sides read in full.

### KO-2 — Korium README's "Korium and RetroAchievements builds" overclaims the archive's reach
- **Category:** Doc-vs-code contradiction (imprecision)
- **Locations:** `third_party/korium/README.md:8`; `tools/build_edition.py:19-24,34-35`; `tools/assets.py:152-156`
- **Problem:** The archive is read by theme, not by RA-ness: the RetroAchievements-IncebtIon edition (`ra_incebtion`, theme `incebtion`) never reads it, while the Lite and Lite-RA editions do and go unmentioned. As written, a reader would expect every RA build to carry the korium pin's assets and would look for korium provenance in a kit that has none.
- **Fix:** Reword to name the mechanism: "`tools/assets.py` reads this archive for every build whose theme is Korium (the Korium, RetroAchievements, Lite and Lite-RA editions)".
- **Benefit:** Provenance expectations match the edition matrix.
- **Effort:** S
- **Risk:** Low (doc-only)
- **Confidence:** High.

### KO-3 — Korium manifest completeness is enforced by nobody: unlisted zip entries are silently dropped
- **Category:** Unenforced pin
- **Locations:** `tools/assets.py:166-173`; `.github/scripts/test_korium_builtin_assets.py:38`
- **Problem:** `embed()` iterates `manifest["files"]` only. If a future theme import adds a file to `assets.zip` and updates `archive_sha256` but forgets the `files` entry, the build succeeds and the new asset is silently never extracted — the exact failure the README's "update the archive and manifest together" instruction (README:10-11) exists to prevent, with no check behind it. The reverse direction is only fail-by-accident (an uncaught `KeyError` from `package.read`). The one completeness-adjacent assert in the tree checks a single named key's absence. The coordinator verified the sets are equal *today*; nothing keeps them equal.
- **Fix:** In `embed()` (or the korium test), assert `set(package.namelist()) == set(manifest["files"])` before extracting — one comparison at the point the archive is already open and hashed.
- **Benefit:** A partial manifest update fails the build loudly instead of silently shipping a theme missing its new assets.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High — the loop and the absence of any set comparison were read directly; current equality is coordinator/script-verified, so this is a gap, not a live defect.

### KO-4 — Korium extraction directory is never cleaned, so a removed asset can keep shipping from a dirty build tree
- **Category:** Dead/stale integration (build hygiene)
- **Locations:** `tools/assets.py:146-148` (only `asm/*.c` is cleaned), `:162-173` (extraction writes, never deletes)
- **Problem:** `build/theme-assets/korium` accumulates across runs. If a later manifest revision *removes* a file (e.g. a texture deliberately dropped from the pin), the previously extracted copy remains on disk in a reused local build tree, and the themed lookup at `tools/assets.py:195-200` will still find and embed it — bypassing the manifest and its digests entirely for that file. CI's fresh runners mask this; local edition builds (the documented workflow) do not.
- **Fix:** Clear `theme_dir` (or extract into a fresh directory) at the start of the korium branch of `embed()`, mirroring the existing `asm/*.c` cleanup three lines earlier in the same function.
- **Benefit:** What a korium build embeds is always exactly what the current manifest lists and hashes.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High for the mechanism (no cleanup exists in the read code); the stale-file-ships outcome is INFERRED from the `:195-200` existence-based lookup, not executed.

### KO-5 — The korium contract test is orphaned: no workflow runs it
- **Category:** Unenforced pin / dead integration
- **Locations:** `.github/scripts/test_korium_builtin_assets.py` (whole file); `.github/workflows/flavours.yml:28-62` (enumerated test list), `:65`, `:103` (the only glob loops)
- **Problem:** The test guarding the korium theme contract (Players badges loaded, korium's own app cover, background art off by default — regressions the file's own header ties to a remediation branch) is referenced by no workflow: flavours.yml names ~35 test scripts individually, including both orbit tests, and neither glob loop matches `test_korium_builtin_assets.py`. A change to `themes.cpp`, `textures.cpp`, `settings.cpp`, or the assets.py exclusion line can break the built-in korium theme with CI fully green. (The digest half of the pin is still build-enforced per §3; this gap is specifically the theme-contract test.)
- **Fix:** Add one line to the flavours.yml test step beside the orbit tests. No new machinery.
- **Benefit:** The remediation the test was written for stays remediated.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High — grep across `.github/workflows/` for the filename returns nothing, and the flavours.yml test region was read.

---

## 9. Non-findings (checked, deliberately not filed)

- **Orbit staging is sound as documented** (apart from KO-1's prose): digest verified before the zip is opened, required-entry and `-ra=` checks complete before any write, refusal-before-write is test-proven (`.github/scripts/test_package_orbit_neutrino.py:72-84`).
- **`stage_orbit` accepts any top-level folder name** (`path.parts[0]` unchecked, `tools/package_edition.py:35-40`): not filed — the whole-archive digest pin fixes the exact bytes, so the prefix cannot vary for the pinned archive; the staging test deliberately uses a synthetic prefix.
- **One extracted-but-unused korium entry** (`gfx/ra_mark.png` in non-RA korium builds, §2.3): not filed as size-without-use — the same archive serves the RA build, where all 100 entries are consumed; splitting the archive per edition would add machinery for 1 file.
- **Korium digests are not checked by the dedicated test**: not filed separately — they *are* checked at build time by `embed()` (§3); the enforcement gap that remains is completeness (KO-3) and the orphaned test (KO-5).
