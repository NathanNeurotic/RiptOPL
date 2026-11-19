# RiptOPL — Round 5, Section S7 (EE src/: Neutrino launch, APPS source, VMC saves, achievements, texcache, netsupport, ratls, small UI/platform files)

- **Pin:** 2e347d5ac885cd0ff73e9ad4143781946396f492 (verified `git rev-parse HEAD`; `git status --porcelain` empty at start and end)
- **Scope:** 15 files, 8,153 lines, every file read 1..EOF. Read-only; no edits in the repo.
- **Method note:** No PS2 build exists in this VM — no build or hardware claims. Performance/size magnitudes are structural/INFERRED from struct sizes and constants in the code. Two suspicions were verified against code outside the section (`src/sources/ranet.cpp`, `modules/vmc/genvmc/genvmc.h`, `include/configset.h`, `src/gfx/themes.cpp`) and dissolved or confirmed on that evidence; those are marked as such.

## Coverage ledger

| File | Lines | Read |
|---|---|---|
| src/platform/sysneutrino.cpp | 1,466 | 1..EOF |
| src/sources/appsupport.cpp | 1,444 | 1..EOF |
| src/core/vmcsaves.cpp | 1,257 | 1..EOF |
| src/sources/achievements.cpp | 797 | 1..EOF |
| src/gfx/texcache.cpp | 634 | 1..EOF |
| src/sources/netsupport.cpp | 501 | 1..EOF |
| src/platform/ratls.c | 459 | 1..EOF |
| src/ui/libview.cpp | 365 | 1..EOF |
| src/platform/sysvmc.cpp | 285 | 1..EOF |
| src/sources/appscan.cpp | 248 | 1..EOF |
| src/ui/guiwait.cpp | 232 | 1..EOF |
| src/ui/drivestatus.cpp | 165 | 1..EOF |
| src/platform/selfupdate.c | 140 | 1..EOF |
| src/platform/diaglog.cpp | 105 | 1..EOF |
| src/core/diagsave.cpp | 55 | 1..EOF |
| **Total** | **8,153** | |

---

## New candidate findings

### R5-S7-01 / APPS scan requires `d_type == DT_DIR` exactly — the failure mode themes.cpp already documented and fixed for MMCE
- **Category:** 6
- **Locations:** `src/sources/appscan.cpp:103` (`if (pdirent->d_type != DT_DIR) continue;` inside `scanApps`)
- **Problem:** Every app folder on a scanned device must arrive with `d_type == DT_DIR` or it is skipped before its `title.cfg` is even attempted. The repo itself records that this cannot be trusted: the comment at `src/gfx/themes.cpp:995-1002` (#152) explains that d_type is derived from iomanX mode bits, that MMCE card firmware fills stat.mode verbatim, and that clone firmwares may not speak the FIO_S_* dialect — "every entry then degrades to DT_UNKNOWN. That is why languages still listed … while themes vanished (this required == DT_DIR exactly)". `oplScanApps` scans every enabled prefix, MMCE included, so on such a card the APPS page scans zero apps with no error shown. (READ for the gate and the in-repo precedent; INFERRED that MMCE APPS folders hit the same dirent path, since scanApps uses the same POSIX readdir the theme catalog walks.)
- **Fix:** Delete the `d_type` gate in `scanApps` and let the existing `title.cfg` open/read decide: a non-directory entry produces a `dir/title.cfg` path whose `configRead` fails, and `appScanCallback` already returns 1 ("no boot/title", not counted, not fatal) for it. If the extra failed open per plain file is judged too costly on slow shares, fall back to the themes pattern — probe with `opendir()` only for entries that are not positively DT_DIR. Justification (for the probe variant only): it adds an open on the already-suspect subset, exactly as the accepted #152 fix does; the plain deletion adds nothing.
- **Benefit:** APPS discovery works on the same firmware/driver class that already broke theme discovery once; removes a silent total failure of one page.
- **Effort:** S / **Risk:** Low (deletion) to Med (probe variant) / **Confidence:** High (READ)

### R5-S7-02 / VMC-sourced save and file names are never validated, and the folder export writes them into filesystem paths
- **Category:** 6
- **Locations:** `src/core/vmcsaves.cpp:453` (`vsLoadVmc`: `snprintf(s->name, "%.32s", dir.name)`), `src/core/vmcsaves.cpp:469` (same for each file's `e.name`), `src/core/vmcsaves.cpp:720-746` (`vsWriteFolder`: `mkdir("%s%s", parent, s->name)`, per-file `open("%s%s/%s", parent, s->name, f->name, O_CREAT|O_TRUNC)`)
- **Problem:** The same file establishes the invariant and then skips it on one of three loaders. `vsNameOk` (`:767`: 1–32 bytes, no `/`, not `.`/`..`) is enforced for PSU-loaded names (`:796`, `:822`) and at `vsWritePsu` (`:867`, with the comment "a name from a card (`../x`) never leaves the folder"), and `vsLoadFolder` validates its name argument. `vsLoadVmc` validates nothing: a damaged or crafted VMC image whose root folder name is `../x` (or contains a `/`), or whose file entries do, is copied verbatim into `vs_save_t`, and `vmcSavesExport` / `vmcSavesPsuToFolder` then hand it to `vsWriteFolder`, which mkdirs and creates/truncates files at the resulting path — outside the SAVES folder the user picked. The input is a card image file, i.e. exactly the untrusted-input class the PSU leg already defends against.
- **Fix:** Run `vsNameOk` on the save name and every file name in `vsLoadVmc` (fail with `VMCSAVES_ERR_NAME`, as `vsLoadPsu` does). Justification: this adds a check, and it is the check the same file already applies to the two sibling loaders; it makes the stated invariant true for all three.
- **Benefit:** A crafted VMC image can no longer write outside the export folder; the three loaders behave identically on names.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### R5-S7-03 / VMC save delete frees at most 64 file chains; the rest leak on the card forever
- **Category:** 6
- **Locations:** `src/core/vmcsaves.cpp:1181` (`u32_t files[SAVE_FILES]` in `vmcSavesDelete`), `src/core/vmcsaves.cpp:1193` (`for (i = 2; i < vsDirLen(dir.length) && nfiles < SAVE_FILES; i++)`)
- **Problem:** `vmcSavesDelete` collects the first cluster of each file into a fixed 64-entry array before hiding the root entry and freeing chains. A save folder may legally hold more entries than that — the directory length the code itself trusts goes to `DIR_MAX` 4096, and `vsLoadVmc` treats >64 files as a copy error (`VMCSAVES_ERR_TOO_BIG`) rather than an impossibility. For file 65 onward, the chain is never collected, the root entry is hidden and the folder's own chain is freed, so those clusters stay marked used in the FAT with nothing naming them: permanent space loss on the card image, recoverable only by reformatting. The delete is offered for any save `vmcSavesList` shows, and that lister does not count files.
- **Fix:** Delete the `files[]` array and free each file's chain inside the enumeration loop (the entries are read while the save is whole, and a file's chain is independent of the folder's chain, which is still freed last); keep the current order of hiding the root entry before flushing the FAT. This is a net deletion — the storage cap, not the freeing, is what is removed.
- **Benefit:** Deleting a large save returns all of its clusters; the card no longer accumulates orphaned space.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### R5-S7-04 / XeRAbora parse allocates the 1,024-entry cap (~232 KB) on every fetch, on top of the resident previous list and the 512 KB document
- **Category:** 4
- **Locations:** `src/sources/achievements.cpp:145` (`XERA_LIST_MAX = 1024`, `XERA_DOC_MAX = 512 * 1024`), `src/sources/achievements.cpp:356` (`malloc(XERA_LIST_MAX * sizeof(*list))` in `achievementsParseXerabora`), `src/sources/achievements.cpp:369-370` (cap-and-truncate comment)
- **Problem:** `achievement_entry_t` is 5 ints + `icon[33] + title[57] + description[101] + date[20]` = 232 bytes with padding (READ from `include/achievements.h:19-21`), so the parse allocation is ~232 KB, sized by the cap rather than by the document. During a refetch, the previous `xeraList` (another ~232 KB at the same cap) is still resident — it is freed only after a successful parse — and `xeraFetch` simultaneously holds the 512 KB document buffer: roughly 1 MB of transient heap for one menu fetch on a 32 MB machine (INFERRED arithmetic from the READ sizes). The comment at the cap says the bound is "larger than any RA set", and sets are parsed into it whether they hold 12 entries or 1,200.
- **Fix:** Size the allocation from the document (count the array's objects in a first pass, or grow the array as entries are appended) and free the old `xeraList` before replacing it once the new document has parsed. No new subsystem; the parser already walks the array once.
- **Benefit:** A typical fetch's transient footprint drops from ~1 MB to the size of the actual set plus the document; the resident cache shrinks to the real entry count.
- **Effort:** M / **Risk:** Med / **Confidence:** Med (sizes READ, magnitudes INFERRED)

### R5-S7-05 / diagSave keeps a second, permanent 16 KB copy of the whole diagnostic ring
- **Category:** 4
- **Locations:** `src/core/diagsave.cpp:21` (`static char copy[DIAG_SIZE]`, DIAG_SIZE = 16 KB at `:17`), duplicating `src/platform/diaglog.cpp:16` (`static char diagRing[DIAG_SIZE]`)
- **Problem:** The ring itself is the feature and earns its 16 KB. The copy is a second 16 KB of permanent BSS used only for the duration of `diagSave()` — a user-triggered save from Drive Status, or the pre-launch save — purely so `diagCopy` can assemble the wrapped ring into a contiguous buffer before two `write()` calls. `diagSave` runs in ordinary thread context (it opens and writes a file); nothing about it needs static storage.
- **Fix:** Allocate the snapshot on the heap inside `diagSave` and free it on exit (the early-return paths included), or write the ring in its two wrapped segments and drop the copy entirely. Justification (heap variant): it trades a small, bounded allocation on a rare, file-writing path for 16 KB of permanent BSS.
- **Benefit:** 16 KB of EE BSS returned permanently; the diagnostics feature keeps its full ring.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### R5-S7-06 / Dead disc-probe constants copied into both split files; the only live copy is in sysdisc.cpp
- **Category:** 1
- **Locations:** `src/platform/sysneutrino.cpp:11-25` and `src/platform/sysvmc.cpp:11-25` (identical `#include <delaythread.h>` / `<time.h>` preamble plus `SYS_DISC_DETECT_MS`, `SYS_DISC_REDETECT_MS`, `SYS_DISC_NODISC_MS`)
- **Problem:** Both files carry the disc-probe budget block verbatim from the system.cpp split, and neither file ever uses the constants: a tree-wide grep shows the only uses are in `src/platform/sysdisc.cpp` (which defines its own copy), and in each of these two files the names occur exactly three times — the definitions. The constants are `constexpr` (internal linkage), so the cost is not BSS; it is three more copies of a tuned value set (1,000 / 4,000 / 2,000 ms) that can drift independently of the copy that actually governs disc waits. (A fourth copy exists in `src/platform/system.cpp:17-25`, outside this section's files; flagged for the parent, not filed here.)
- **Fix:** Delete the constant block (and the two includes that only serve it) from `sysneutrino.cpp` and `sysvmc.cpp`. Pure deletion.
- **Benefit:** One live definition of the disc budgets instead of four; no reader of the Neutrino or VMC files is misled into thinking those budgets apply there.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ, grep-verified)

### R5-S7-07 / ratls compares HTTP transfer/content token values case-sensitively, while header names are matched case-insensitively
- **Category:** 6
- **Locations:** `src/platform/ratls.c:159` and `src/platform/ratls.c:171` (`!strncmp(value, "chunked", 7)`), `src/platform/ratls.c:430` (`strncmp(value, "identity", 8)`)
- **Problem:** `headerValue` matches header *names* case-insensitively (the `| 0x20` fold), but the *values* that decide the body framing are compared as exact lowercase. HTTP token values are case-insensitive (RFC 9110 §5.6.2 / RFC 9112 chunked coding), and servers do emit `Chunked`. For `raTlsRequest` the consequence is framing confusion, not a memory error: `responseComplete` treats a `Chunked` body as close-delimited and `finishResponse` hands the raw chunk-framed bytes to the RetroAchievements JSON parser, so the lookup fails for that server even though the response was well-formed. For `raTlsDownload` any `Transfer-Encoding` header is rejected outright, so the exposure there is only the `Content-Encoding: Identity` false-reject of an uncompressed body.
- **Fix:** Compare these three values with the same case-insensitive discipline `headerValue` already applies to names (a small local token-compare helper used at the three sites). Justification: this adds a helper, and it is the correctness condition for parsing a standards-conformant response; the alternative — accepting the body — is the failure being fixed.
- **Benefit:** HTTPS lookups and update downloads stop failing against servers that capitalise standard tokens.
- **Effort:** S / **Risk:** Low / **Confidence:** Med (mechanism READ; which deployed servers capitalise is INFERRED)

### R5-S7-08 / Two bespoke TOML scanners in one file, with divergent rules for the same grammar
- **Category:** 2 (with Cat 3 character)
- **Locations:** `src/platform/sysneutrino.cpp:303-380` (`sysTomlTornTailCut`) and `src/platform/sysneutrino.cpp:522-635` (`sysTomlRewriteSmbArgs`)
- **Problem:** sysneutrino.cpp hand-rolls the TOML lexer twice: once to validate/repair a torn file tail (comments, all four string forms including multi-line strings, bracket depth, line-completeness), and once to rewrite module arg strings (comments, basic and literal strings — but multi-line strings are a hard -1 there). Each is ~80–115 lines of character-loop state machine, and any TOML corner case (escapes, triple quotes, comment-in-string) must be reasoned about, and fixed, in both — they already disagree about multi-line strings, which is the drift this duplication invites. Together with `sysTomlEscape` and the `strstr("\"ip=")` shortcut in the third sync function, one file carries four separate stances on how a TOML file is read.
- **Fix:** Factor the shared token walk (skip comment / skip-or-copy string / track depth) into one scanner both transforms drive with a callback, or reduce the torn-tail checker to the subset the rewriter's lexer already proves. Deletion-preferred: the torn-tail function's full grammar is more than its job needs — it only has to find the last complete point within 32 bytes of the end of a file this program itself writes.
- **Benefit:** One TOML lexer to keep correct instead of two divergent ones, in the file that rewrites the user's Neutrino configs in place.
- **Effort:** L / **Risk:** High (both functions rewrite user files; behaviour must be preserved byte-for-byte, host-testable via the existing test pattern) / **Confidence:** High (READ)

### R5-S7-09 / Apps linked-list node allocation is copy-pasted twice, and its if/else collapses to a plain prepend
- **Category:** 2
- **Locations:** `src/sources/appsupport.cpp:675-691` (`addAppsLegacyList`) and `src/sources/appsupport.cpp:734-750` (`appScanCallback`)
- **Problem:** Both list builders contain the same ~15-line block: an if/else on whether the list is empty whose two branches do the *same* `malloc` and differ only in the `next` assignment — `NULL` in the empty branch, the old head in the other. But the old head *is* NULL in the empty case, so the branch structure encodes nothing: one prepend (`app->next = *appsLinkedList; *appsLinkedList = app;`) plus the shared NULL check covers both branches at both sites.
- **Fix:** Replace both blocks with the single prepend form (or one small node-allocating helper both call). Net deletion of roughly 20 lines across the two sites, no behaviour change — list order is identical because the empty-list prepend already produced `next = NULL`.
- **Benefit:** The allocation-failure handling for app discovery lives in one shape instead of four branch copies.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### R5-S7-10 / cacheGetTextureEx repeats its ~20-line in-flight request adoption block verbatim
- **Category:** 2
- **Locations:** `src/gfx/texcache.cpp:259-284` (the `*cacheId`-keyed fast path) and `src/gfx/texcache.cpp:314-339` (the reunion-by-value loop)
- **Problem:** When a slot's request is still loading, both paths perform the identical adoption ritual — stamp `entry->lastUsed = guiFrameId`, load `entry->qr`, adopt `req->focusEpoch = gArtW.focusEpoch`, and promote on priority unless the request is SIO2 while navigation is active — including the same explanatory comment copied with it. The two copies guard the cache's most race-sensitive invariant (a queued request must be re-stamped or the worker cancels it as stale, the failure narrated at length further down the same function), so a future fix to the ritual applied to one path and not the other reintroduces that class of bug on exactly one of the two routes a row can take.
- **Fix:** Extract the ritual into one small helper both paths call (the reunion copy additionally assigns `*cacheId`/`*UID` before calling it). No new state, no new allocation.
- **Benefit:** One adoption ritual to fix and to audit in the per-frame art path.
- **Effort:** S / **Risk:** Med (per-frame hot path; behaviour must stay identical) / **Confidence:** High (READ)

---

## Already filed — not repeated

- **RIPTOPL-014** (permanent multi-KB static buffers) — names both sysneutrino `toml[4096]` statics (now in `sysSyncNeutrinoUdpfsToml`, `src/platform/sysneutrino.cpp:382` ff., and `sysNeutrinoSmbPreflight`, `:637` ff.) and the achievements `doc[2048]` (`raHttpsGameId`) / `response[1024]` (`loadPage`) statics. Read at the pin; not re-filed. The `updated[]` companion buffers at the same two sysneutrino sites are the same mechanism in the same functions and are treated as covered by 014's "share one scratch buffer" fix.
- **RIPTOPL-018** (per-source game-launch leg copy-pasted across call sites) — `AppSource::launch` in appsupport.cpp is the APPS instance of that pattern; nothing new about it was found beyond the already-filed duplication.
- **RIPTOPL-050** (self-update holds the whole replacement ELF in EE RAM) — `src/platform/selfupdate.c` is only the portable parser half of that feature; no new defect found in it (see Checked and clean).
- **RIPTOPL-005** (grep-assertion "tests") — `test_selfupdate_host.py`, which exercises selfupdate.c, is the genuine behavioural test round 1 already singled out to keep; not re-filed.
- **X-18** (cross-repo: Launchstation writes `title = %s`-style title.cfg that RiptOPL's parser drops / the both-keys requirement) — the title+boot requirement in `appScanCallback` (`src/sources/appsupport.cpp`, `appScanCallback`) is that finding's RiptOPL side; not re-filed, per the section brief.
- **RIPTOPL-073 / -126** (unvalidated IP parsing; IPCONFIG.DAT implemented three times) — netsupport.cpp's `applyIpConfig`/`readNetConfig` are single, structured implementations over `ps2ip_getconfig`; they are not instances of those findings, and nothing equivalent was found in this section's files.
- **RIPTOPL-134** (one Neutrino resolve probes the same MC candidates twice from overlapping tables) — sysneutrino.cpp's module-file and bsdfs token tables are different mechanisms (per-protocol module names; typed-arg resolution); checked and not the 134 mechanism.

## Checked and clean (candidates that dissolved on full reading / verification)

- **achievements.cpp `xeraFetch` length arithmetic** — `achievementsParseXerabora(kind, doc + body, length)` looks like an over-read because the sibling HTTPS caller passes `length - body`; verified against the implementation (`src/sources/ranet.cpp`, `Ranet::httpGet`): that API sets `*length` to the *body* length only, so the call is exact. Two APIs, two conventions, both call sites correct.
- **texcache.cpp `cacheInitCache` failure path** — `allocation_failed` frees `content_`/`prefix_` before they are all assigned, which looks like freeing garbage; but the object is value-initialised (`new (cache) image_cache_t()`), which zero-initialises every member, so the frees are of NULL or owned pointers and `activeRequests_` starts at 0.
- **sysneutrino.cpp `sysWriteNeutrinoCheats` snprintf accumulation** — `len += snprintf(...)` is unchecked, but the reachable inputs bound it: the caller caps pairs at 255 (`MAX_CHEATLIST` = 510 in supportbase's caller), each pair line is 28 bytes against 32 bytes of per-pair slack in the allocation, and header + `depends` stay well under the 512-byte base for any startup/userCfg the callers can supply.
- **appsupport.cpp `AppSource::rename` strncpy** — `strncpy(value, cur->val, sizeof(value))` into `char value[256]` cannot leave the copy unterminated: `config_value_t::val` is `char[256]` *including* the terminator (`include/configset.h:13-19`), so the source is at most 255 chars.
- **sysvmc.cpp `strcpy(createParam.VMC_filename, path)`** — the destination field is `char[1024]` (`modules/vmc/genvmc/genvmc.h:28`) and `path` is a 256-byte buffer already snprintf-checked; unreachable to overflow.
- **vmcsaves.cpp `vsLoadVmc` size accounting** — `total += f->size` is u32 and could wrap, but the per-file `f->size > SAVE_MAX_SIZE` test fires independently of the wrapped total, so no oversized allocation follows.
- **ratls.c `dechunk`** — chunk sizes are sign-checked, checked against the remaining length before either the `body[in + size]` framing test or the in-place `memmove`; the trailer walk requires the terminating blank line.
- **drivestatus.cpp `dsSizeText`** — the KB→MB/GB/TB shifts and fraction masks were verified arithmetically (thresholds 2^10 / 2^20 / 2^30 KB with matching shifts); HDD values are correctly halved from 512-byte sectors before the call.
- **netsupport.cpp `waitValidState`** — the alarm/semaphore pairing cannot strand a signal into a recycled semaphore: `WaitSema` can only be woken by the one outstanding alarm it just set, and the wait is bounded (30 retries) with the full-table case handled.
- **libview.cpp BDM saved-entry indexing** — `libViewBdmAttach`/`libViewCommitPending` index `bdmSlotType[]` and the per-type saved slots only after the `known`/range checks; an unrecognised driver type falls back to defaults without touching the saved slots.
- **sysneutrino.cpp `convertCompatmaskToModes`** — the digit-string → `atoi` → `-gc=%d` round-trip is exact because the composed digits never carry a leading zero; every append is bounds-guarded against `COMPAT_MODE_COUNT + 2`.
- **guiwait.cpp `runWithDialog` unbounded wait** — the sibling `runWithMessage` documents that `timeoutMs <= 0` deliberately keeps the original unbounded wait for one caller class; the dialog variant shares that contract, and its `ioPutRequest` failure path (the actual hang defect of that shape) is guarded in both variants.
- **achievements.cpp JSON `\u` decoding** — malformed hex digits degrade to code point 0 rather than failing or overrunning, and `jsonPutUtf8` never splits a sequence across the output bound; ugly input produces a wrong glyph, not a bad write.
- **appsupport.cpp `config()` size text** — `snprintf(tmp[8], "%.2f", MiB)` can truncate an absurd ELF size to a wrong display string, but it is bounded, display-only, and reaches no parser.
- **selfupdate.c parsers** — `suParseSums` validates all 64 hex digits before decoding, `suSplitUrl` is an explicit host allowlist with a size check, and `suJoin` rejects snprintf truncation; no new defect under the already-filed RIPTOPL-050.
- **texcache.cpp `cacheDestroyCache` drain units** — `DelayThread(1000)` is microseconds (1 ms) and the loop count is `ART_FOREGROUND_DRAIN_MS` (500), so the drain really is ~500 ms; the leak-rather-than-free fallback is deliberate and logged.

## Notes for the parent

- A fourth dead copy of the `SYS_DISC_*` constants (R5-S7-06) exists in `src/platform/system.cpp:17-25`, outside this section's assigned files; whichever section owns system.cpp should apply the same deletion there.
- No forced revision to any prior finding (RIPTOPL-001…144) arose from this section.
