# RiptOPL — Round 5, Section R5-T1: PC/host tools, elfldr, ee_core leftovers, tests/host

- **Repo / pin:** NathanNeurotic/RiptOPL @ `2e347d5ac885cd0ff73e9ad4143781946396f492` (verified `git rev-parse HEAD`; worktree clean at start and end)
- **Mode:** READ ONLY. No edits, commits, pushes, PRs. No PS2 build exists in this VM — no build or hardware claims; performance magnitudes are structural/INFERRED.
- **Scope note:** the assignment said "24 files" but enumerates 26; all 26 were read 1..EOF. These are host-side tools/tests (developer's PC), except `elfldr/loader.c` (EE child loader), `ee_core/igrovl/igrovl_main.c` (EE overlay) and the two `tools/hwtest/smb2probe` programs (EE + IOP hardware test). Cat 6 severity is weighted accordingly — but crafted ISO/VMC/ul.cfg inputs ARE realistic inputs to iso2opl/genvmc/opl2iso, so parser bounds still matter there.
- **Dedup:** every candidate was grepped against `Sable-RiptOPL.md`, `-ROUND2`, `-ROUND3`, `-ROUND4` and `round4-sections/*.md` (RIPTOPL-001…144) plus the cross-repo reports before filing. Hits are listed under "Already filed — not repeated".

## Coverage ledger

| File | Lines | Read |
|---|---|---|
| tools/pc/iso2opl/src/isofs.c | 1,089 | 1..EOF |
| tools/pc/iso2opl/src/iso2opl.c | 608 | 1..EOF |
| tools/hwtest/smb2probe/probe.c | 580 | 1..EOF |
| elfldr/loader.c | 475 | 1..EOF |
| tools/pc/genvmc/src/genvmc.c | 422 | 1..EOF |
| tools/pc/http/tests/test_stream.c | 333 | 1..EOF |
| tools/pc/opl2iso/src/opl2iso.c | 300 | 1..EOF |
| tools/hwtest/smb2probe/probe_iop.c | 143 | 1..EOF |
| tools/pc/http/tests/test_catalog.c | 109 | 1..EOF |
| ee_core/igrovl/igrovl_main.c | 50 | 1..EOF |
| ee_core/tests/test_freeze_diag_format.c | 257 | 1..EOF |
| ee_core/tests/test_freeze_diag_screen.c | 136 | 1..EOF |
| tests/host/test_themeviews.cpp | 371 | 1..EOF |
| tests/host/test_ps1art.cpp | 326 | 1..EOF |
| tests/host/test_igrmenu.cpp | 238 | 1..EOF |
| tests/host/test_config.cpp | 196 | 1..EOF |
| tests/host/test_tar.cpp | 163 | 1..EOF |
| tests/host/test_wallpaper.cpp | 90 | 1..EOF |
| tests/host/test_settings.cpp | 88 | 1..EOF |
| tests/host/test_raii.cpp | 85 | 1..EOF |
| tests/host/test_grid.cpp | 82 | 1..EOF |
| tests/host/test_atlas.cpp | 81 | 1..EOF |
| tests/host/test_bdmtyped.cpp | 69 | 1..EOF |
| tests/host/test_ps2cnf.cpp | 57 | 1..EOF |
| tests/host/test_vmc_groups.cpp | 47 | 1..EOF |
| tests/host/lib_config_host.cpp | 7 | 1..EOF |
| **Total** | **6,402** | **26 files** |

Supporting headers read for context (not in the line count): `tools/pc/iso2opl/src/iso2opl.h`, `tools/pc/opl2iso/src/opl2iso.h`, `tools/pc/genvmc/src/genvmc.h`.

---

## Findings

### R5-T1-01 / iso2opl trusts SYSTEM.CNF sizes and strings from the ISO in three places — a crafted ISO overflows a global buffer, a stack buffer, and dereferences NULL
- **Category:** 6 (bugs/security)
- **Locations:**
  - `tools/pc/iso2opl/src/iso2opl.c:16` — `u8 systemcnf_buf[65536];`
  - `tools/pc/iso2opl/src/iso2opl.c:338-341` — `fsize = isofs_Seek(fd, 0, SEEK_END); … r = isofs_Read(fd, systemcnf_buf, fsize);` with no comparison of `fsize` against `sizeof(systemcnf_buf)`; `isofs_Read` (`tools/pc/iso2opl/src/isofs.c:1009-1010` area, `isofs_Read` body) clamps only against the file's own size, never the destination.
  - `tools/pc/iso2opl/src/iso2opl.c:374` — `strcpy(boot_path, p2);` where `boot_path` is the caller's `char ElfPath[256]` (`iso2opl.c:389`) and `p2` points into the 64 KB `systemcnf_buf`: one long BOOT2 line overflows the stack frame.
  - `tools/pc/iso2opl/src/iso2opl.c:425-427` — `strcpy(GameID, &ElfPath[8]); p = strstr(GameID, ";1"); *p = 0;` — `p` is dereferenced with no NULL check, and if the BOOT2 value is shorter than 8 chars, `&ElfPath[8]` reads past the string's terminator into uninitialised stack.
- **Problem:** The ISO's directory entry sets SYSTEM.CNF's claimed size, and its contents set the BOOT2 line length and shape. All three are attacker-controlled in the realistic case (an ISO downloaded from the internet and installed with iso2opl): a >64 KiB SYSTEM.CNF smashes the BSS buffer before the `r != fsize` check is ever reached; a BOOT2 path that lacks `;1` (PS1-style `BOOT`, an extensionless path, a homebrew CNF) crashes the tool outright instead of printing its own "failed to parse" diagnostic.
- **Fix:** Reject `fsize > sizeof(systemcnf_buf)` before reading; copy the BOOT2 value with an explicit length bound into `ElfPath`; NULL-check the `strstr` result and return the existing `-3` parse error; require the BOOT2 value to be at least the `cdrom0:\` prefix length before indexing `[8]`.
- **Justification (fix adds checks):** each check guards a copy whose length comes from the input file; there is no smaller deletion that makes the copies safe.
- **Benefit:** iso2opl survives (and correctly rejects) malformed/crafted ISOs; the existing error paths become reachable.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (all four sites read in full; the missing bound is the whole function `ParseSYSTEMCNF`).

### R5-T1-02 / isofs `TocEntryCopy` writes the terminator at the disc-supplied filename length, past its 129-byte field
- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/iso2opl/src/isofs.c:287-298` — Joliet branch caps `filenamelen = filenameLength / 2` (≤127, safe); the ISO9660 branch sets `filenamelen = internalTocEntry->filenameLength` (a raw `u8` from the directory record, up to 255), copies with `strncpy(tocEntry->filename, …, 128)`, then executes `tocEntry->filename[filenamelen] = 0;`
- **Problem:** The copy is bounded but the terminator write is not. A directory record with `filenameLength` > 129 writes a zero byte up to ~126 bytes past `filename[129]` — into the stack `localTocEntry` in `FindPath` or the caller's `TocEntry` elsewhere. Real ISO9660 names are short; a crafted ISO's directory sector is not bound by that, and directory records are parsed before any file is opened.
- **Fix:** Clamp `filenamelen` to 128 in the non-Joliet branch (the value the `strncpy` already assumes).
- **Justification (fix adds a check):** one comparison on an untrusted length byte; the alternative — trusting it — is the bug.
- **Benefit:** crafted directory sectors can no longer write outside the TOC entry during a plain file lookup.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-T1-03 / genvmc prints usage on a wrong argument count and then dereferences the missing arguments anyway
- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/genvmc/src/genvmc.c:399-413` — `if (argc != 3) printUsage();` with no return, falling through to `strtol(argv[1], NULL, 10)` and `vmc_mcformat(argv[2], …)`.
- **Problem:** Run with no arguments, `argv[1]` is NULL and `strtol(NULL, …)` crashes; run with one argument, `argv[2]` is NULL and reaches `fopen`. The usage text is printed and the crash follows it. (Run with *extra* arguments it also proceeds, silently ignoring them.)
- **Fix:** Return `EXIT_FAILURE` after `printUsage()` when `argc != 3`.
- **Justification (fix adds a check):** it is the missing half of the validation the code already attempts.
- **Benefit:** the tool's most common misuse becomes an error message instead of a segfault.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (crash behaviour of `strtol(NULL)` is standard-libc INFERRED; the fall-through is READ).

### R5-T1-04 / iso2opl builds every output path with `sprintf` into 256-byte buffers from an unbounded drive argument
- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/iso2opl/src/iso2opl.c:88-92` (`check_cfg`), `:151-155` (`write_cfg`), `:219-223` (`write_parts`) — `sprintf(cfg_path / part_path, "%s/…", drive, …)` into `char cfg_path[256]` / `char part_path[256]`; `main` (`:520`) validates only `argv[3]`'s length (the game name), never `argv[2]` (the drive/path), `argv[1]`, or `GameID`.
- **Problem:** A destination path argument longer than ~240 chars overflows a stack buffer in three functions before any file is touched. The user's own command line is the input, so severity is low — but long install paths (deep SMB shares, the `\\MyComputer\PS2SMB` form the usage text itself advertises) are a legitimate shape for this argument.
- **Fix:** Convert the five `sprintf` path builds to `snprintf(..., sizeof(buf), ...)` and reject a truncated result; opl2iso in the same tree already uses `snprintf` for its output name.
- **Justification (fix adds bounds):** path length here is externally supplied; bounded formatting is the minimal change.
- **Benefit:** long-path installs fail cleanly instead of corrupting the stack.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-T1-05 / A 32-character game name is stored unterminated in ul.cfg, and the duplicate check then compares past the field
- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/iso2opl/src/iso2opl.c:520` (main rejects only `strlen(argv[3]) > 32`), `:159` (`strncpy(cfg.name, game_name, 32)` — no terminator when the name is exactly 32 chars), `:121` (`check_cfg`'s `strcmp(cfg.name, game_name)`).
- **Problem:** The tool's own maximum-length name produces a record whose `name[32]` has no NUL. `check_cfg`'s `strcmp` then reads on into `image`, `parts`, `media` and `pad`, so a reinstall of the same 32-char game can never match on name (the comparison includes the `ul.…` image bytes) — only the image-ID check still catches it. For a *foreign* ul.cfg with no zero byte anywhere in the trailing 32 bytes, the same `strcmp` reads past the 64-byte stack record entirely. (For records write_cfg itself made, `pad[0]` is zero from its `memset`, bounding the over-read inside the record.)
- **Fix:** Reject a 32-char name at install time (limit 31 + terminator) or compare the name field with `strncmp(..., 32)` in `check_cfg`.
- **Justification (fix adds a bound):** the field is fixed-width on disk; string functions are the wrong tool for it, and the bound is the fix.
- **Benefit:** duplicate detection works at the documented maximum name length; foreign ul.cfg records can't drive a stack over-read.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-T1-06 / opl2iso takes the output filename from raw ul.cfg bytes — a name containing `/` or `..` writes outside the CD/DVD folder
- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/opl2iso/src/opl2iso.c:227-229` — `snprintf(rname, 255, "%s/%s.%s.iso", mediatype, temp_ID, grecord.name);` then `fopen(rname, "wb")` (`:235` area), where `grecord.name` is 32 raw bytes read from ul.cfg by `findGame` with no character screening.
- **Problem:** ul.cfg collections circulate with games; a record name of the form `../../something` (or containing an absolute-path fragment after the media prefix) makes the exporter truncate/create a file outside the intended `CD/`/`DVD/` directory. The `snprintf` bounds the length, not the meaning.
- **Fix:** Reject (or sanitise) a record name containing `/`, `\` or a leading `.` before building `rname`.
- **Justification (fix adds a check):** the name is file-format data, not a path; screening separators is the smallest change that keeps it one.
- **Benefit:** exporting a downloaded ul.cfg collection can't write outside the export folders.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (formatting and open sites read in full; exploitation needs a hostile ul.cfg, hence Low severity weighting).

### R5-T1-07 / opl2iso's copy loop spins forever on a read error
- **Category:** 6 (bugs/security — robustness)
- **Locations:** `tools/pc/opl2iso/src/opl2iso.c:260-266` — `while (!feof(fsrc)) { read = fread(block, 1, 65536, fsrc); if (read != fwrite(...)) … }`.
- **Problem:** `feof` is only set by an end-of-file read, not by an error read. A part file on failing media (read returns 0 with the error indicator set) loops forever writing nothing and printing the spinner — the write-side check can't fire because `0 == fwrite(...,0,...)`. Missing/truncated parts are the normal failure mode of this tool's input, so the loop shape matters.
- **Fix:** Loop on the `fread` result (`while ((read = fread(...)) > 0)`) and check `ferror(fsrc)` after it.
- **Justification (fix adds a check):** replaces the known-broken `feof` idiom with the read result the loop already has.
- **Benefit:** a bad part file produces the tool's existing "resulting file will be invalid"-class failure instead of a hang.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (hang requires a live I/O error — INFERRED trigger, READ loop shape).

### R5-T1-08 / `isofs_ReadSect` computes the byte offset in 32 bits — directory extents past 4 GiB wrap
- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/iso2opl/src/isofs.c:195` — `offset = lsn * 2048;` where `lsn` is `u32`: the multiply wraps modulo 2³² before widening to `s64`. Contrast `isofs_Read` (`:981`): `offset = (((s64)fh->lsn) * 2048) + fh->position;` — the file-data path widens first.
- **Problem:** `ReadSect` serves the volume descriptors (sectors 16-19, always low — safe) and the directory cache (`CachedDirInfo.sector_start`, taken from a directory's TOC extent). A directory extent at sector ≥ 2,097,152 (≥ 4 GiB into a DVD9 ISO) is read from the wrapped, wrong offset and parsed as if it were the directory. Real ISOs keep directories low, so this is a correctness cliff rather than a memory bug — but it is silent: the outcome is "file not found" or the wrong record, not an error.
- **Fix:** Widen before multiplying: `offset = (s64)lsn * 2048;` (same for `nbytes`, which is bounded by the 32-sector cache in practice).
- **Justification (fix changes one expression):** it makes `ReadSect` match the already-correct arithmetic in `isofs_Read` in the same file.
- **Benefit:** directory lookups stay correct anywhere in an 8.5 GB image.
- **Effort:** S · **Risk:** Low · **Confidence:** Med, READ for the arithmetic, INFERRED for real-world reachability (no shipped ISO with a >4 GiB directory extent was examined).

### R5-T1-09 / ~90 lines of commented-out POSIX implementations are kept beside the stdio ones in the PC tools
- **Category:** 1 (dead code)
- **Locations:** `tools/pc/iso2opl/src/iso2opl.c:95-113` (open/read `check_cfg`), `:169-184` (open/write `write_cfg`), `:225-271` (open/write `write_parts`); `tools/pc/iso2opl/src/isofs.c:166-168, 172-180, 1051-1053, 1059-1060, 1080` (lseek/read/close fragments and a hex-dump block).
- **Problem:** Each live stdio function carries its abandoned descriptor-based twin as a block comment, plus stray commented debug code in isofs. The twins must be re-read and discounted by anyone auditing the live path (they contain the same buffer sizes and error codes, inviting copy-back of superseded code), and they are the only "documentation" of a port that already happened.
- **Fix:** Delete the commented blocks; version control is the archive.
- **Benefit:** `iso2opl.c` drops from 608 to ~520 lines and `isofs.c` sheds its dead fragments; one implementation per function remains to audit.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-T1-10 / test_catalog's de-duplication memory silently stops at 512 unique startups
- **Category:** 8 (half-finished feature — test harness fidelity)
- **Locations:** `tools/pc/http/tests/test_catalog.c:30` (`static char seen[512][...]`), `:89-92` — when `seenCount` reaches 512, new accepted startups are no longer recorded, but the row is still printed `ACC`.
- **Problem:** The harness's stated contract (header comment) is first-record-wins de-duplication mirroring `httpsupport.cpp`, diffed against the Python reference. Past 512 unique startups, a later duplicate prints `ACC` here while the (unbounded) reference prints the duplicate outcome — the two sides diverge for a reason that has nothing to do with the parser under test, and a smaller fixture would never reveal it.
- **Fix:** Grow `seen` dynamically, or print a hard harness error when the cap is hit instead of silently degrading.
- **Justification (fix adds code):** the harness is the oracle for a cross-language contract; a loud cap preserves that, a silent one voids it.
- **Benefit:** the catalog differ stays trustworthy at any fixture size.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (trigger — a >512-startup fixture — not present in-tree as far as this section's files show; INFERRED).

### R5-T1-11 / test_settings snapshots the settings table into fixed 200-entry arrays with no bound check
- **Category:** 8 (half-finished feature — test harness fidelity)
- **Locations:** `tests/host/test_settings.cpp:51` — `int expectInt[200]; char expectStr[200][256]; unsigned char expectCol[200][3];`, filled at `:52-60` with `idx++` per `kSettings` row and no comparison against 200.
- **Problem:** The table currently has 109 rows (counted in `src/core/settings.cpp` / the golden keys include), so the cap is invisible today; the day the table passes 200 rows the *test itself* stack-smashes instead of failing with a message, and the failure will look like a settings bug. The golden-keys array right above it is sized from its include; the snapshot arrays are a magic number.
- **Fix:** Size the arrays from the golden key count (`sizeof(kGoldenKeys)/sizeof(kGoldenKeys[0])`, already computed as `n` at `:23`) or assert `idx < 200` in the fill loops.
- **Justification (fix adds a bound):** one assert ties the harness's capacity to the table it mirrors.
- **Benefit:** future table growth fails as a test message, not as memory corruption in the test rig.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (row count verified by grep of the settings table, outside this section's files).

---

## Already filed — not repeated

- **CRC-32 text ×3 (EE + iso2opl + opl2iso)** — cross-repo **X-28** (`Sable-CROSSREPO-ROUND2.md:309-313`, locations include `tools/pc/iso2opl/src/iso2opl.c:48-70` and `tools/pc/opl2iso/src/opl2iso.c:43-65`). Not re-filed, per the assignment note. Corollary checked here: the `crc32` functions' running `crc` is **not** uninitialised — it deterministically carries the last table-build value into the data loop, identically in all copies, which is exactly why the three agree on ul. names. Changing the seed in one copy would break that agreement; the remedy is X-28's shared source, not a local seed fix.
- **`long_multiply` half-discarded** — **RIPTOPL-064** covers the IOP module copy (`modules/vmc/genvmc/genvmc.c:142-162`, only `hi` consumed). The PC tool carries the same helper text at `tools/pc/genvmc/src/genvmc.c:144-165` with its sole call at `:313-315`, where `lo` is likewise never read after the call. Recorded here as an additional location of the same substance, not a new finding.
- **tests/host stub-header duplication** — **RIPTOPL-031** (20 identical empty SDK stubs + the second stub tree). The test sources in this section merely consume those stubs; no new instance filed.
- **Text-assertion Python tests vs. host tests** — **RIPTOPL-005** already covers the `.github/scripts` side and names `tests/host/` as the genuine behavioural suite; the suites read here confirm that characterisation (see Checked and clean).

## Checked and clean

- **elfldr/loader.c `loadElfViaFileXio` bounds** — segment checks run in the safe order (`filesz > memsz`, `vaddr < 0x100000`, `memsz > GetMemorySize()`, then the subtraction clause), `phentsize`/`shentsize` are required to match the local structs before striding, and `readAll` turns any short read into a clean `-1` and the `SifLoadElf` fallback. The unchecked ELF `entry` is not a defect: executing the user-chosen ELF is the loader's function, and a PT_LOAD must exist before entry is used. The `LINKER_ONLY` libc stubs are deliberate, documented, and placed in a `/DISCARD/` section so accidental use fails the link loudly.
- **elfldr `-la=`/`-reset-iop` argument stripping** — `argc--` after consuming the trailing flag keeps `ExecPS2(argc - 1, &argv[1])` consistent (loader path excluded, target's own argv[0] preserved), and the post-strip `argc < 2` re-check catches a flags-only invocation.
- **smb2probe `probe_iop.c` SetAlarm idiom** — `b[4] == 0` as "alarms can be set" matches in-repo IOP usage (`modules/ds34usb/iop/ds34usb.c` treats `SetAlarm(...) == 0` as success); the dummy alarm is cancelled on that path and harmless if it fires. SQUEEZE is bounded by the EE side's `squeeze > 0` guard and by `AllocSysMemory` returning NULL.
- **smb2probe `probe.c` log path** — `plog` bounds every append against the 24 KB `logbuf` (excess lines drop, never overflow); `dump()` clamps its terminator index for both the success and the `-999` timeout value; `strncpy(..., sizeof-1)` targets are static/zeroed, so termination holds.
- **genvmc format arithmetic** — at the enforced 1–1024 MB range the IFC cap (`ifc_length <= 32`) is unreachable (max needed is 17), the 16-cluster erase stride always divides the card size (whole megabytes × 1024 clusters), and the magic+version concatenation (28 + 7 chars + NUL) fits the 40-byte superblock field. No defect found in `vmc_mcformat` beyond R5-T1-03.
- **isofs `FindPath` `strcat(CachedDirInfo.pathname, ...)`** — dissolves on tracing the callers: appended components plus separators never exceed the caller's original path, itself capped at 1024 by `__splitpath`/`isofs_Cache_Dir`'s static buffer, so the 1024-byte cache pathname cannot overflow for reachable inputs.
- **isofs `__splitpath` `dir[255] = 0`** — the 255-char truncation is the documented ISO9660 path-length restriction stated in the function's own comment, not a silent bug.
- **isofs `isofs_Open` ordering** — `isofs_FindFile` runs before the `fd == MAX_FDHANDLES` check (wasted lookup on a full table), but `fh` is never dereferenced on that path; the check precedes the `memset`. Waste only, below filing weight.
- **isofs `isofs_Seek` signed arithmetic** — the `int r` position math could overflow in the abstract, but the sole caller (`ParseSYSTEMCNF`) uses SEEK_SET/SEEK_END on one small file; no reachable overflow established.
- **opl2iso cfg record handling** — the deliberate contrast with R5-T1-05: its `cfg_t` is padded (`name[33]`, `image[16]`, header comment says so) and both readers `memset` the extra byte before `fread`, so its `strcmp`/`strncmp` uses are terminated. Correct as written.
- **PC isofs.c vs. console isofs module** — same libcdvd ancestry, but the PC copy is a `FILE*`-based host program and the console one an IOP module; different contracts and environments, so this is a sync-awareness note, not a merge/deletion candidate.
- **test_stream.c / test_freeze_diag_\*.c** — stub state is reset per case through `feed()`/fresh reports; the deadline tests deliberately cover clock wrap and shared deadlines; truncation, game-id termination and heap-record bounds are all asserted with canary bytes (`small[32] == 'X'`). No concrete gap found.
- **test_config.cpp's `|| true` check (`:52`)** — reads as a tautology but functions as an explicit no-crash smoke expression: a crash in `configGetInt` on a non-numeric value would still fail the test binary. Intentional, and commented as such.
- **Remaining tests/host suites** (themeviews, ps1art, igrmenu, tar, wallpaper, raii, grid, atlas, bdmtyped, ps2cnf, vmc_groups, lib_config_host) — read in full; assertions track the documented contracts of the units under test (including edge inputs: NULL, oversize, stale indices, unterminated names). The one data anomaly present — duplicate/odd-shaped title IDs in `vmc_groups` — is already self-reported in that test's header comment ("KNOWN DATA ISSUE (reported, not asserted)"). No concrete correctness or coverage gap established.
- **ee_core/igrovl/igrovl_main.c** — 50 lines: header struct publication, two one-line API forwards, and the documented local `strlen` (the overlay links no libc). Nothing unused, nothing unbounded (no string input crosses it).

## Forced revisions to prior findings

None. No prior RIPTOPL-001…144 or X-series finding is contradicted by this section; two are extended by location only (X-28 corollary and RIPTOPL-064's PC-tool instance, both under "Already filed").
