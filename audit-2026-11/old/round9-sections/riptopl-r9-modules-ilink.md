# RiptOPL Round 9 — `modules/ilink/` + `modules/bdmassault/` + `modules/pccf/` full line-read

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` (verified `git rev-parse HEAD`; clone
`~/workspace/audit-clones-2026-11-13/RiptOPL` left git-clean, read-only).
READ = line-read 1..EOF at the pin in this pass; INFERRED = reasoned from READ code,
not executed. No build or hardware claims. Nothing below re-files RIPTOPL-021, -047,
-156, -172 or -184; pccf's already-filed RIPTOPL-240 (CIS dump trace work) and -241
(`pcdiag_cf_probe` gate-only diagnostic) are likewise not re-filed. The R5-M5
section's dissolved list (assault static GUID, device-numbering drift, CROM
endianness strategies, `iLinkAddCROMUnit` export-only `nQuads`, `GetBIOSVersion`,
assault `rcd` memset) is carried forward in Checked-clean, not re-litigated.

Method for the pair census: every file under `modules/ilink/` was mapped to its
`modules/bdmassault/ilink_bd_assault/` counterpart (`iLinkman/X → src/iLinkman/X`,
`IEEE1394_bd/X → src/IEEE1394_bd/X`), md5summed on both sides, and every diverged
pair was read as a full `diff -u`. The ilink side of every pair was line-read in
full; on the assault side, `iLink.c`, `iLink_crom.c`, `iLink_internal.c`,
`sbp2_driver.c`, `scsi.c`, `main.c` and all headers/build files were line-read in
full, and `iLink_intr.c` / `iLink_TxRx.c` / `iLink_DMA.c` / `iLink_PHY.c` were read
as complete diffs against the fully-read ilink text (every differing line
inspected; non-differing lines are byte-identical to text already read in full).

---

## (b) ilink ↔ bdmassault pair census (md5 evidence)

22 same-name pairs: **3 identical, 19 diverged, 0 pairs missing on either side**
within the two driver subtrees. 6 ilink-only files and 10 assault-only files sit
outside the pairing (listed after the table).

| # | Pair (ilink path ↔ assault path under `ilink_bd_assault/`) | ilink md5 (first 12) | assault md5 (first 12) | Verdict | Divergence summary |
|---|---|---|---|---|---|
| 1 | `IEEE1394_bd/src/imports.lst` ↔ `src/IEEE1394_bd/src/imports.lst` | `07b82b08bf01` | `2ee94b23572f` | Diverged | Assault copy lacks `I_PollEventFlag` (its sbp2 half gets polling via the combined wrapper imports instead); otherwise same list. Assault copy is **unused** by the assault build — see ILINK-04. |
| 2 | `IEEE1394_bd/src/include/module_debug.h` ↔ `src/IEEE1394_bd/src/include/module_debug.h` | `2850cc0f69e0` | `278d75d3b921` | Diverged | ilink defines `U64_2XU32` as a declaring macro + `DEBUG_U64_2XU32` no-op twin; assault defines it as a bare expression macro `((u32*)val)[1], ((u32*)val)[0]`. Equivalent at their respective call sites (assault passes `&var`; ilink declares `var_u32[]`). |
| 3 | `IEEE1394_bd/src/include/sbp2_disk.h` ↔ `src/IEEE1394_bd/src/include/sbp2_disk.h` | `23a0b793eded` | `affdd11e6e5e` | Diverged | Two behavioural deltas: assault `CDBs[12]` vs ilink `CDBs[16]`, and assault `CDB_DATA_SIZE(v) ((v))` **unmasked** vs ilink `((v & 0xffff))`. The CDB-size half is the header side of RIPTOPL-156 (not re-filed); the unmasked macro is dissolved below (queue_cmd's `data_len ≤ 65024` guard makes overflow unreachable in-tree). Plus `extern` removal on the two prototypes. |
| 4 | `IEEE1394_bd/src/include/scsi.h` ↔ `src/IEEE1394_bd/src/include/scsi.h` | `ba2c1904ce40` | `0541f33a7f86` | Diverged | Assault drops `scsi_interface.devNr` and the whole SCSI opcode enum (its `scsi.c` uses literals — the code side of RIPTOPL-156's older base). |
| 5 | `IEEE1394_bd/src/irx_imports.h` ↔ `src/IEEE1394_bd/src/irx_imports.h` | `0cccbb9638c5` | `0cccbb9638c5` | **Identical** | — |
| 6 | `IEEE1394_bd/src/main.c` ↔ `src/IEEE1394_bd/src/main.c` | `4e793455dff0` | `f6288b120add` | Diverged | Intentional variant: assault renames `_start` → `BD_iLink_start`, comments out `IRX_ID` (the combined IRX carries iLinkman's identity). |
| 7 | `IEEE1394_bd/src/sbp2_driver.c` ↔ `src/IEEE1394_bd/src/sbp2_driver.c` | `70cc7a260d3c` | `1abb7c427073` | Diverged | Include paths; login-retry comment reworded for POPSTARTER; assault omits `scsi.devNr = i` at init and the whole `sbp2FreeDevNr()` + its call (device numbering — dissolved in R5-M5: assault numbers by scsi slot, which a failed warm-up frees, so the ilink failure mode does not reproduce). All #1018-era hardening is present in both. |
| 8 | `IEEE1394_bd/src/scsi.c` ↔ `src/IEEE1394_bd/src/scsi.c` | `d3a4a563b910` | `4b70a8b90604` | Diverged | The RIPTOPL-156 delta (no RC16 / READ\|WRITE 16, literal opcodes, no `static_assert`s) plus: assault `scsi_init` assigns `g_scsi_bd[i].devNr = i`, ilink instead copies `scsi->devNr` + `bd->path = "ilink"` at connect. Warm-up budget, sector-size guard and publish-after-warmup are in both. |
| 9 | `iLinkman/README` ↔ `src/iLinkman/README` | `c54f59d60931` | `c54f59d60931` | **Identical** | — |
| 10 | `iLinkman/include/iLinkman.h` ↔ `src/iLinkman/include/iLinkman.h` | `4f9fc2278c71` | `fe11b02c91af` | Diverged | `extern` keyword removed from every prototype (assault, for its single-TU-link build). No signature changes. |
| 11 | `iLinkman/src/exports.tab` ↔ `src/iLinkman/src/exports.tab` | `10f18627cca8` | `dd9993ae1b68` | Diverged | Stub renamed `_retonly` → `_funcret`, defined **before** the table in both iLinkman copies. The assault copy is **unused** (the wrapper `src/exports.tab` is the built one, with `_funcret` after `END_EXPORT_TABLE`) — see ILINK-04. |
| 12 | `iLinkman/src/iLink.c` ↔ `src/iLinkman/src/iLink.c` | `2e7195e839ee` | `21ecc2708a38` | Diverged | Include path; clang-format alignment; intentional chaining: assault `_start` calls `BD_iLink_start()` after registering iLinkman exports and unwinds via `_exit` on refusal. Transaction-context code (incl. RIPTOPL-172's three functions) is otherwise identical. |
| 13 | `iLinkman/src/iLink_DMA.c` ↔ `src/iLinkman/src/iLink_DMA.c` | `4b6ab3838a39` | `dd346574d152` | Diverged | Include path + alignment only. Zero substantive divergence. |
| 14 | `iLinkman/src/iLink_PHY.c` ↔ `src/iLinkman/src/iLink_PHY.c` | `c71f5d9d3a46` | `6414691d8553` | Diverged | Include path only (1 line). Zero substantive divergence. |
| 15 | `iLinkman/src/iLink_TxRx.c` ↔ `src/iLinkman/src/iLink_TxRx.c` | `4f29547a62ca` | `2a053a965c9b` | Diverged | Include path + alignment + one comment reflow. Zero substantive divergence. |
| 16 | `iLinkman/src/iLink_crom.c` ↔ `src/iLinkman/src/iLink_crom.c` | `337c84d4d31a` | `6117c1fb5247` | Diverged | The RIPTOPL-184 delta (assault: no `memset`, no `VendorID` assignment) inside two internally consistent endianness strategies (assault: natural-order header + `ieee1394Swab32`/`BSWAP16` at build; ilink: reversed-field header, no swab — R5-M5 verified equivalent in intent). Everything else, incl. ILINK-01 and ILINK-02 below, is identical. |
| 17 | `iLinkman/src/iLink_internal.c` ↔ `src/iLinkman/src/iLink_internal.c` | `988f7cfb479f` | `1756fb2d61c1` | Diverged | Intentional variants, all documented in `modules/bdmassault/PROVENANCE.md:42-45` / `ilink_bd_assault/README.md:14-17`: assault `GetConsoleIDs` returns a static GUID/model (cdvdman crashes POPSTARTER); `dmac_enable/disable` vs `sceEnable/DisableDMAChannel`; assault wraps `malloc`/`free` in `#ifdef USE_CUSTOM_ILINKMAN_MALLOC_AND_FREE` (never defined anywhere — assault therefore links sbp2_driver.o's `malloc`/`free` in the one IRX, R5-M5). |
| 18 | `iLinkman/src/iLink_intr.c` ↔ `src/iLinkman/src/iLink_intr.c` | `b666d6c1da97` | `c7f3c4bca5f4` | Diverged | Include path + alignment only. Zero substantive divergence (all response-matching / generation / boundary hardening is in both). |
| 19 | `iLinkman/src/imports.lst` ↔ `src/iLinkman/src/imports.lst` | `106390a6ca9b` | `75d40a990a0e` | Diverged | Only the dmacman block differs (`sceEnable/DisableDMAChannel` vs `dmac_enable/disable`). Assault copy is **unused** by the assault build and still carries the cdvdman block — see ILINK-04. |
| 20 | `iLinkman/src/include/iLink_CROM.h` ↔ `src/iLinkman/src/include/iLink_CROM.h` | `642fcb23cc80` | `8036a276fa94` | Diverged | Field-order strategy twin of pair 16: assault structs are in wire (BE) order with `BusName[4]`/`Vendor_Name[4]` as byte arrays; ilink structs are in host order with `u32` text fields. Same sizes. |
| 21 | `iLinkman/src/include/iLink_internal.h` ↔ `src/iLinkman/src/include/iLink_internal.h` | `7860ba8d653b` | `ae055e569ba5` | Diverged | `extern` removal; assault declares `void *malloc(int nBytes)` (signed) vs ilink `malloc(unsigned int)` — a prototype/definition mismatch *within* the assault tree when its guarded `malloc` is compiled out (dissolved: the definition that actually links is sbp2_driver.c's `malloc(int)`, which matches the assault prototype). |
| 22 | `iLinkman/src/irx_imports.h` ↔ `src/iLinkman/src/irx_imports.h` | `ab0f97c2d276` | `ab0f97c2d276` | **Identical** | Assault copy is nevertheless **unused** (the wrapper `src/irx_imports.h` is the built one) — see ILINK-04. |
Count check: 8 IEEE1394_bd pairs + 14 iLinkman pairs = **22 pairs**. Full md5s were taken for all 44 files; the first-12 prefixes above are the census evidence, and full hashes reproduce with `md5sum` at the pin.

**Outside the pairing:**
- ilink-only (6): `IEEE1394_bd/{LICENSE, Makefile, ORIGIN.txt}`, `iLinkman/{LICENSE, Makefile, ORIGIN.txt}` — the assault build replaces the two Makefiles with its own combined one and drops the ORIGIN files.
- assault-only inside `ilink_bd_assault/` (10): `Makefile`, `README.md`, `include/bdm.h` (a local BDM header copy — the ilink tree uses the SDK's), `src/{exports.tab, imports.lst, irx_imports.h}` (the combined-IRX build files that *are* used), `src/iLinkman/doc/{Changelog,KnownIssues,Notes,TODO}.txt` (2011–2012 upstream notes, carried only by the assault copy).
- assault top level (not part of the driver pairing): `LICENSE` (md5 `9eac3bba…`, BDMAssault's AFL copy — differs from the ilink LICENSEs `bf71d1c0…`), `PROVENANCE.md`, and 9 prebuilt `.irx.*` binaries. All 9 sha256 values in `PROVENANCE.md:14-22` were recomputed and **match the shipped bytes exactly**; `usbd.irx.ilink` is byte-identical to `usbd.irx.usbexfat` (md5 `a77650a0ad356077b990d21bde6affc4` both), as claimed. One provenance *sentence* is wrong — see ILINK-12.

---

## (a) Candidate findings (benefit order)

### ILINK-01 / `InitializeConfigurationROM` leaks the old CROM and every extra unit, and silently drops all added units, on every `iLinkSetNodeCapabilities` call — both copies / Category 6 / 4

- **Locations:** `modules/ilink/iLinkman/src/iLink_crom.c:249-262` (`InitializeConfigurationROM`); identical text at `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_crom.c:249-262`. Callers: `modules/ilink/iLinkman/src/iLink.c:110` (`_start`) and `:270` (`iLinkSetNodeCapabilities`); assault `iLink.c:110,274`. Publication consumer: `modules/ilink/iLinkman/src/iLink_intr.c:324-327` (assault twin at the same lines).
- **Problem:** The function resets state by *forgetting* allocations instead of freeing them. `ExtraCROMUnits[i] = NULL` (line 254) discards up to 16 `malloc`'d unit buffers (from `iLinkAddCROMUnit`, `iLink_crom.c:65-90`) without `free`; `ConfigurationROM = NULL` (line 256) discards the previous CROM buffer, defeating the `free(ConfigurationROM)` that `BuildConfigurationROM` itself performs when it publishes a replacement (`iLink_crom.c:388-391`). At `_start` this is harmless (all pointers are already NULL). But `InitializeConfigurationROM` is also called by the **exported** `iLinkSetNodeCapabilities` (export 7), so every capabilities change (a) leaks the old CROM (~200 B + extras) and every extra-unit allocation in IOP memory, (b) silently removes every unit any module previously added with `iLinkAddCROMUnit` from the published Configuration ROM, and (c) arms a worse state: because the pointer was nulled but `ConfigurationROMSize` keeps its old value, if `BuildConfigurationROM`'s `malloc` then fails (it returns early, `iLink_crom.c:295-297`, leaving both globals untouched), the read-request handler's CROM test `(offset_low - 0x400) < ConfigurationROMSize` still passes and it forms a response pointer as `(unsigned int)ConfigurationROM + …` = NULL + offset, serving a read response from low IOP memory instead of refusing. READ for the leak, the unit drop and the stale size; the malloc-failure-plus-1394-read trigger is INFERRED (needs an allocation failure at exactly that moment).
- **Fix:** In `InitializeConfigurationROM`, free what is being discarded before clearing it (`free(ExtraCROMUnits[i])` per non-NULL slot; let `BuildConfigurationROM`'s existing replace-and-free handle the CROM buffer by *not* nulling `ConfigurationROM` first), and set `ConfigurationROMSize = 0` until the new build publishes. Apply to both copies.
- **Justification (if additive):** No addition — the fix reuses the file's own `free` wrapper and the replace logic `BuildConfigurationROM` already has; it deletes the pointer-nulling that causes all three effects.
- **Benefit:** Removes a per-call IOP leak on an exported path, an undocumented "all CROM units vanish" side effect, and a NULL-based response pointer on allocation failure.
- **Effort:** S
- **Risk:** Low — `_start` behaviour is unchanged (pointers are NULL there); the only behaviour change is on re-initialisation, where freeing replaces leaking.
- **Confidence:** High (READ) for leak + unit drop; Medium (INFERRED trigger) for the stale-size publication arm.

### ILINK-02 / `iLinkFindUnit` accepts a negative `UnitInList` and indexes `NodeData[-1]` — both copies / Category 6

- **Locations:** `modules/ilink/iLinkman/src/iLink_crom.c:193-208` (`iLinkFindUnit`); identical at `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_crom.c:193-208`. Array: `unsigned int NodeData[63]` at `modules/ilink/iLinkman/src/iLink_intr.c:30` (assault twin same). Export 23 in both `exports.tab`s.
- **Problem:** The only guard is `if (UnitInList < nNodes)`. `UnitInList` is a signed `int` parameter of an exported library function, so `UnitInList = -1` (or any negative) satisfies the test whenever `nNodes ≥ 0` and the function immediately reads `NodeData[UnitInList]` — one or more words *before* the array, in IOP memory — and feeds the value into a node-ID computation and a CROM walk against a remote node. This is the same exported-API class as RIPTOPL-172 (which covered the three `trContext` functions in `iLink.c`) but a different function and a different array; it was not in that finding's scope. The in-tree caller is safe (`sbp2_driver.c` passes loop indices `0 … nNodes-1` in both copies), so today this is reachable only by another IOP module linked against iLinkman — exactly the exposure RIPTOPL-172 already established as file-worthy for this API.
- **Fix:** Change the guard to `if (UnitInList >= 0 && UnitInList < nNodes)`, in both copies — the same one-comparison form `iLinkTrRead`/`iLinkTrWrite` already use for their index.
- **Justification (if additive):** One comparison, copied from the validation pattern the same library already established; no new mechanism.
- **Benefit:** Closes the last unguarded caller-supplied index into a driver-global array in the iLinkman export surface.
- **Effort:** S
- **Risk:** Low — no in-tree caller passes a negative index.
- **Confidence:** High (READ — guard, parameter type, array declaration and export status all in-tree).

### ILINK-03 / The SBP-2 layer attaches up to 5 drives, the SCSI layer can only ever publish 2 — drives 3–5 are logged in and warm-up-retried as if their failure were transient — both copies / Category 8 / 5

- **Locations:** `modules/ilink/IEEE1394_bd/src/sbp2_driver.c:18-19` (`MAX_DEVICES 5`, `SBP2Devices[5]`) vs `modules/ilink/IEEE1394_bd/src/scsi.c:92-93` (`NUM_DEVICES 2`, `g_scsi_bd[2]`); assault twins at `sbp2_driver.c:18-19` and `scsi.c:80-81`. Failure path: `scsi_connect` returns -1 when no `g_scsi_bd` slot is free (`scsi.c:391-420` ilink), `initConfigureSBP2Device` maps that to -3 (`sbp2_driver.c:278-280`), and `sbp2AttachUnits` counts it as `missing++` — the retryable kind (`sbp2_driver.c:540-551`).
- **Problem:** The two layers disagree about the driver's capacity, and the disagreement is invisible in operation. A third, fourth or fifth SBP-2 unit on the bus gets a full bring-up — CROM parse, exclusive LOGIN, fetch-agent setup, INQUIRY / TEST UNIT READY / READ CAPACITY warm-up — and then fails at the last step for a compile-time reason that no retry can change. The attach machinery treats it exactly like a transient warm-up failure: it is retried on the 1 s / 2 s / 4 s / 5 s back-off (bounded to 8 retries only while another drive is attached, `sbp2_driver.c:571-580`; otherwise re-tried at every bus reset, indefinitely). Each futile cycle also takes and holds an exclusive login on a drive the system can never use. Nothing in either file, or in `ORIGIN.txt` / the assault README, states that 2 is the intended published limit and 5 is slot headroom — the 5 exists, per the sbp2 comments, to hold *reserved* slots for lost-status logins of the same ≤2 real drives, but the attach loop cannot distinguish "third distinct drive" from "same drive, third slot". READ for the caps and the failure/retry path; how often a 1394 bus here carries >2 SBP-2 drives is INFERRED (rare — which is why this is a half-finished-capacity finding, not a hot bug).
- **Fix:** Make the attach loop stop at the publishable capacity: in `sbp2AttachUnits`, count currently-published plus in-flight `g_scsi_bd` occupants (or export a `scsi_free_slots()`-style count from `scsi.c`) and skip — without LOGIN — units beyond it, instead of warming them up to a guaranteed -3. The smaller alternative is reducing `MAX_DEVICES` to 2, but that deletes the reserved-slot headroom the login-retention design uses, so the skip is the LESS change that preserves intent.
- **Justification (if additive):** The skip reuses the attach loop's existing `continue` path for unusable units (the same shape as the "no management agent" and "bad speed" skips already in the function); no new state is kept.
- **Benefit:** A drive the driver can never serve stops consuming logins, warm-up cycles and transport time, and its absence from BDM becomes a capacity fact instead of an endless "trying again" state.
- **Effort:** S–M
- **Risk:** Medium — the counting must include slots held by reserved (lost-status) logins of real drives, or a re-attaching drive could be skipped in favour of a third drive; the fix has to count published + configuring + login-holding slots, as `sbp2FreeDevNr` already enumerates.
- **Confidence:** High (READ) on the cap mismatch and retry path; Medium (INFERRED) on real-world frequency.

### ILINK-04 / The assault tree carries five build files its only Makefile never reads (244 lines) — one of them still contains the cdvdman imports the tree exists to remove / Category 7 / 2

- **Locations:** Unused in `modules/bdmassault/ilink_bd_assault/`: `src/iLinkman/src/imports.lst` (74 lines), `src/iLinkman/src/irx_imports.h` (32), `src/iLinkman/src/exports.tab` (30), `src/IEEE1394_bd/src/imports.lst` (73), `src/IEEE1394_bd/src/irx_imports.h` (35). The build that CI runs (`.github/workflows/flavours.yml:372-375`, `make -C modules/bdmassault/ilink_bd_assault`) uses only `Makefile:9-15,30-36`: `exports.o`/`imports.o` come from `src/` (the wrapper `src/exports.tab`, `src/imports.lst`, `src/irx_imports.h`), and the pattern rules compile only `%.c` from the two subtrees. No include path in `Makefile:11` reaches the nested copies.
- **Problem:** These are the standalone two-IRX build files from the ilink tree, carried into a single-IRX build that superseded them, and two of them are actively misleading. (1) `src/iLinkman/src/imports.lst:1-4` still declares the `cdvdman_IMPORTS` block (`I_sceCdRI`, `I_sceCdRM`) — the exact imports `ilink_bd_assault/README.md:15` and `modules/bdmassault/PROVENANCE.md:42-45` say were removed because they crash POPSTARTER. Anyone who builds the nested iLinkman standalone, or copies that imports file into the combined build, silently reintroduces the documented crash. (2) The nested `src/iLinkman/src/exports.tab:1` defines `_funcret(){}` *before* `DECLARE_EXPORT_TABLE`, while the built wrapper `src/exports.tab:30` defines it *after* `END_EXPORT_TABLE` — the placement `README.md:18` credits for correct `iopfixup` export headers; the dead copy preserves the pre-fix arrangement as if it were current. In the ilink tree the corresponding five files are live (separate IRX builds), so this is assault-only residue, not a pair defect.
- **Fix:** Delete the five unused files from `ilink_bd_assault/`. If a standalone-build reference is wanted, the ilink tree already is that reference.
- **Benefit:** −244 lines; removes a POPSTARTER-crash trap and a stale export-layout example from the tree a maintainer is most likely to copy build files from.
- **Effort:** S
- **Risk:** Low — the combined Makefile and CI job provably do not reference them (paths enumerated above); a host `make` in the container described by the README is the verification.
- **Confidence:** High (READ — Makefile object/pattern rules, CI invocation, and both exports.tab placements all inspected).

### ILINK-05 / ilink `_start` ignores `GetConsoleIDs` failure and publishes a zero EUI-64 and empty model name / Category 6

- **Locations:** `modules/ilink/iLinkman/src/iLink.c:105-110` (result discarded); the failing function `modules/ilink/iLinkman/src/iLink_internal.c:57-76` (returns -1 when `sceCdRI`/`sceCdRM` fails); consumers `modules/ilink/iLinkman/src/iLink_crom.c:356-357,375-379` (Node Unique ID + Bus Info quadlets built from `ConsoleGUID`/`ConsoleModelName`).
- **Problem:** `_start` memsets `ConsoleModelName` to 0, zeroes `ConsoleGUID`, calls `GetConsoleIDs`, and proceeds regardless of its return value. On a CD-identity read failure the driver therefore loads normally and publishes a Configuration ROM whose node-unique ID is 0x0000000000000000 and whose model text is empty — an EUI-64 that is both invalid and identical on every console that hits the failure, served to any 1394 node that reads the ROM (and returned by the exported `iLinkGetLocalNodeEUI64`). The assault copy cannot fail this way (its `GetConsoleIDs` is the static substitute, `iLink_internal.c:56-64` assault) — this is an ilink-only divergence in failure handling, and `GetConsoleIDs`' own error return + debug messages show failure was an anticipated outcome that the caller never acts on. READ for the discarded result; how `sceCdRI` fails on a healthy console is INFERRED (rare — the value is a robustness/identity defect, not a known field failure).
- **Fix:** In ilink `_start`, treat a `GetConsoleIDs` failure like the other initialisation failures already in the function: log and return `MODULE_NO_RESIDENT_END` before `InitializeConfigurationROM()`.
- **Justification (if additive):** No addition — it routes an existing error return into the failure path the same function already uses twice for semaphore/thread creation.
- **Benefit:** The driver refuses to load instead of advertising a zero, duplicated identity on the bus.
- **Effort:** S
- **Risk:** Low — on any console where the CD identity read succeeds (the normal case, and the only one the README's hardware list exercises) nothing changes.
- **Confidence:** High (READ) on the mechanism; Low–Medium (INFERRED) on trigger frequency.

### ILINK-06 / Dead imports in all four IOP builds in scope (15 stubs with no caller in their own module) / Category 1 / 7

- **Locations:** `modules/ilink/IEEE1394_bd/src/imports.lst`: iLinkman block `:8` (`I_iLinkResetSBus`), `:9` (`I_iLinkSetNodeCapabilities`), `:17` (`I_iLinkTrRead`), `:20` (`I_iLinkSetNodeTrSpeed`); thbase block `:52` (`I_USec2SysClock`), `:53-54` (`I_SetAlarm`, `I_CancelAlarm`); thevent block `:61` (`I_ClearEventFlag`), `:63` (`I_iSetEventFlag`). `modules/ilink/iLinkman/src/imports.lst`: `:16` (`I_QueryIntrContext`), `:66` (`I_PollEventFlag`). `modules/bdmassault/ilink_bd_assault/src/imports.lst`: `:11` (`I_QueryIntrContext`), `:53` (`I_USec2SysClock`), `:60` (`I_iClearEventFlag`). `modules/pccf/imports.lst` (`I_memcpy` in the sysclib block).
- **Problem:** Each listed stub has zero call sites in its own module's translation units (grep-verified this pass over every `.c`/`.inc` in the respective module, including macro-mediated uses: `M_PRINTF`→`printf`, `CreateMutex`, and the event-flag family were each checked by name so macro-hidden uses are not counted as dead — e.g. sbp2's `PollEventFlag` use is in the *IEEE1394_bd* module, whose own list keeps it; the dead one is iLinkman's separate copy). The pattern is copy-carry: the IEEE1394_bd list inherited iLinkman's alarm/event entries although alarms live only in iLinkman's `iLink_TxRx.c`, and it imports four iLinkman functions the SBP-2 layer never calls (it uses `iLinkReadCROM`, not `iLinkTrRead`). IRX import stubs occupy resident table space in every embedded copy and falsely document the module's real dependencies — in the IEEE1394_bd case, they hide which iLinkman entry points the disk layer actually needs.
- **Fix:** Delete the 15 listed `imports.lst` lines (9 + 2 + 3 + 1 per build above).
- **Benefit:** Smaller import tables in both ilink IRXs, the combined assault IRX and both pccf IRXs; each module's declared dependency surface becomes the true one.
- **Effort:** S
- **Risk:** Low — every entry was verified caller-free within its own module; a link of each IRX is the check (an import needed only by a *different* module is that module's own list's business, as the existing split already assumes).
- **Confidence:** High (READ + grep-verified, with the macro-mediation checks stated above).

### ILINK-07 / Dead driver surface kept in both copies: an uncallable deinit, two phantom prototypes, four dead struct fields and a broken macro / Category 1

- **Locations (both copies unless noted):** `DeinitIEEE1394` — defined `modules/ilink/IEEE1394_bd/src/sbp2_driver.c:802-807` (assault `:775-780`), declared `sbp2_disk.h:180`; prototypes with no definition anywhere: `PHTSendResponse` and `iLinkIntrRegister0Handler` at `modules/ilink/iLinkman/src/include/iLink_internal.h:315,322` (assault `:315,322`); `struct SBP2Device` fields `sectorSize` / `maxLBA` / `cache` at `sbp2_disk.h:157-159` and write-only `loginID` at `:144` (assigned `sbp2_driver.c:695` ilink / `:668` assault, never read); `#define PACKET_TCODE (((x) >> 4) & 0xF)` at `iLink_internal.h:92`; self-labelled dead defines `CDB_PAGE_SIZE` / `CDB_PAGE_TABLE_PRESENT` ("Not in use", `sbp2_disk.h:17-18`) and unused event bits `WRITE_REQ_INCOMING` / `READ_REQ_INCOMING` / `ERROR_TIME_OUT` (`sbp2_disk.h:25,26,28` — only `BUS_RESET_COMPLETE` is used).
- **Problem:** A census of the surface that looks load-bearing but is not. `DeinitIEEE1394` can never run: IEEE1394_bd has no export table (it is a standalone IRX whose only entry is `_start`), the assault combined IRX exports only iLinkman's table, and no caller exists in-tree — yet its body (terminate/delete the callback thread) implies a teardown contract that also silently omits the semaphore, event flag, `writeBuffer` and every `trContext`, so even a future caller would get a half-teardown. The two `iLink_internal.h` prototypes declare functions that do not exist in either tree (a caller would link-fail). The four `SBP2Device` fields cost 5 devices × 16 B of resident `.bss` (the `bd->sectorSize` hits in `scsi.c` are the *block_device* field of the same name, a different struct — verified) and `loginID` is stored on every login and read by nobody, not even the logout path that was never implemented. `PACKET_TCODE` is an object-like macro whose expansion references an undeclared `x`; any use would be a compile error, and there is no use.
- **Fix:** Delete the `DeinitIEEE1394` body + prototype, the two phantom prototypes, the four fields + the `loginID` assignment, `PACKET_TCODE`, and the five dead defines, in both copies. If a deinit is ever wanted, it should be written against the current init's full resource list, not revived from this stub.
- **Benefit:** −~30 lines ×2 copies and ~80 B resident `.bss`; the headers stop advertising a teardown and two functions that do not exist.
- **Effort:** S
- **Risk:** Low — all removals are grep-verified unreferenced at the pin (definition, declaration and assignment sites enumerated above are the only occurrences).
- **Confidence:** High (READ + grep-verified).

### ILINK-08 / pccf carries a `time_us` clock plumbing chain that is written once and read never / Category 1

- **Locations:** `modules/pccf/pccf_ata.h:17` (field `unsigned int (*time_us)(void)` in `pccf_ata_t`); definition `modules/pccf/pccf_hw.c:194-200` (`time_us()`); sole assignment `modules/pccf/pccf_hw.c:294` (`ata->time_us = time_us;`).
- **Problem:** The field is populated on every successful `pccf_hw_start` and consumed nowhere — no read of `ata->time_us` or call through it exists in `modules/pccf/` or in the host tests that include these sources (grep-verified this pass). It is the remnant of a timing hook the ATA layer never adopted (all ATA waits are the bounded millisecond polls in `pccf_ata.c:23-46`). Unlike the neighbouring counters (`commands`, `sector_reads`), which at least gate trace output, this one has no consumer of any kind, and it keeps a `GetSystemTime`/`SysClock2USec` call chain alive in the resident driver for nothing.
- **Fix:** Delete the field, the `time_us()` function and the assignment. (The thbase `I_GetSystemTime`/`I_SysClock2USec` imports stay — `pccf_hw.c`'s profile code path and `sbp2`-style elapsed helpers are not in this module; in pccf they are used only by the deleted function, so the two import lines can go too if a link confirms no other user — at the pin there is none in `modules/pccf/*.c` outside `time_us`.)
- **Benefit:** −~10 lines and one dead function pointer per `pccf_ata_t`; the ops/session struct documents only hooks that exist.
- **Effort:** S
- **Risk:** Low — no reader exists to break; host tests (`tests/pccf/`) compile these sources and would catch a missed consumer.
- **Confidence:** High (READ + grep-verified).

### ILINK-09 / `pcdiag_ata.h` declares a diagnostic entry point, `pcdiag_ata_observe`, that was never implemented — and an unused read-limit constant beside it / Category 1 / 8

- **Locations:** `modules/pccf/pcdiag_ata.h:20-21` (prototype + a three-line contract comment describing return values for "IDENTIFY and LBA0 read"); `modules/pccf/pcdiag_ata.h:6` (`PCDIAG_ATA_READ_LIMIT 6U`).
- **Problem:** No definition of `pcdiag_ata_observe` and no caller of it exists anywhere in the repository (grep-verified, tests included), and `PCDIAG_ATA_READ_LIMIT` is referenced by nothing. The work the comment describes is actually done by `pccf_ata_attach` (`pccf_ata.c:128-206`, declared in `pccf_ata.h`), so the header preserves the *diagnostic-phase* API as if it were still a stage of the driver: a future caller would link-fail, and a reader auditing the module's surface counts a phantom stage. This is the header residue of the DIAG5 → production port described in `pccf_ata.c:1-3`.
- **Fix:** Delete the prototype, its contract comment and `PCDIAG_ATA_READ_LIMIT` from `pcdiag_ata.h`. The live declarations in the same header (`pcdiag_ata_ops_t`, the poll limit) stay.
- **Benefit:** The pccf header surface becomes exactly the implemented driver; −5 lines.
- **Effort:** S
- **Risk:** Low — no definition or caller exists to orphan.
- **Confidence:** High (READ + repo-wide grep).

### ILINK-10 / `modules/pccf/format.inc` is a 57-line mini-printf included by nothing / Category 1 / 7

- **Locations:** `modules/pccf/format.inc:1-57` (whole file: `putc_b`, `number`, `format_line`); includer census: repo-wide grep for `format.inc` and for `format_line` returns only the file itself. Build touch-point: `modules/pccf/Makefile:18` (`$(IOP_OBJS): $(wildcard *.h *.inc) Makefile`) makes every object depend on it.
- **Problem:** The file's internal logic is bounded and correct (it was read in full; R5's parent pass likewise found the mini-printf itself clean) — but no translation unit in the production build, the `ps2.yaml` build (`ps2.yaml:490-508` lists the five `.c` files), or the host tests ever includes it, because production tracing is the no-op `pccardTrace` stub (`main.c:19`) and the bounded formatter was never wired to it. It survives as an object-dependency of every pccf object while contributing no code to either IRX: pure build clutter whose presence implies a logging path that does not exist.
- **Fix:** Delete `format.inc`. (If formatted logging is ever wanted, the file is in git history and in the upstream PS2CF import this module was adapted from.)
- **Benefit:** −57 lines and one wildcard dependency; the module's file list matches the code that ships.
- **Effort:** S
- **Risk:** Low — zero includers is a mechanical, re-checkable fact at the pin.
- **Confidence:** High (READ + repo-wide grep).

### ILINK-11 / pccf keeps two 512-byte static scratch buffers resident forever although their lifetimes never overlap / Category 4

- **Locations:** `modules/pccf/pccf_ata.c:6` (`static unsigned char identify[512]`, used only inside `pccf_ata_attach`, `:128-206` — after attach, only the parsed strings/fields in `pccf_ata_t` are used); `modules/pccf/pccf_ata.c:230` (`static unsigned char verify[512]` inside `pccf_ata_write`, used only for its read-back compare).
- **Problem:** Both buffers live in the resident IRX's `.bss` for the module's whole lifetime, but `identify` is dead storage after the single attach-time IDENTIFY transfer, and `verify` is touched only during writes — which can only happen after a successful attach (`cf_write` requires `ata.ready`, `main.c:54-97`). 512 of the 1,024 bytes is therefore permanently wasted on the IOP, in a module whose other resident costs were already worth auditing (cf. the 8 KiB profile pair, which at least has a live function). READ.
- **Fix:** Overlay them: move `verify` out of `pccf_ata_write` to file scope as a union (or a second file-static pointer assignment) sharing `identify`'s storage, since no call path can be inside attach and a write simultaneously (all requests are serialised by `main.c`'s `io_lock`, and attach precedes `bdm_connect_bd`).
- **Benefit:** −512 B resident `.bss` in both pccf IRX variants.
- **Effort:** S
- **Risk:** Low — the non-overlap is structural (attach-once at `_start` vs. post-connect writes, under one semaphore); host tests exercise both paths.
- **Confidence:** High (READ — use sites and lifetime ordering are all in the two files).

### ILINK-12 / `PROVENANCE.md`'s dedupe claim names a binary that does not exist, and its re-vendor count is stale / Category 7

- **Locations:** `modules/bdmassault/PROVENANCE.md:24-25` ("`usbd.irx.mx4sio` and `usbd.irx.ilink` are byte-identical to `usbd.irx.usbexfat`"); re-vendor instruction `:110-111` ("add the mx4sio usbd as an **eighth** file"). Directory census at the pin: 9 `.irx.*` files — `usbd.irx.{ata, ilink, mmce, usbexfat}` and `usbhdfsd.irx.{ata, ilink, mmce, mx4sio, usbexfat}`; there is no `usbd.irx.mx4sio`.
- **Problem:** The hashes themselves are impeccable — all 9 sha256 values in the provenance table were recomputed this pass and match the shipped bytes, and the ilink half of the dedupe claim is true (md5 `a77650a0…` for both `usbd.irx.ilink` and `usbd.irx.usbexfat`). But the sentence also asserts a tenth file that was never vendored, and the re-vendor note counts from a file set ("eighth file") that no longer matches the directory (9 files, so a new one would be tenth). A maintainer following the re-vendor steps would either hunt for a missing file or "correct" the wrong thing. READ (directory listing + hash recomputation).
- **Fix:** Delete "`usbd.irx.mx4sio` and" from the dedupe sentence (leaving the verified ilink claim), and change the re-vendor note to "a tenth file".
- **Benefit:** The provenance document — the file whose entire job is byte-level trust for these binaries — stops asserting a file that isn't there.
- **Effort:** S
- **Risk:** Low — text only; the verified table is untouched.
- **Confidence:** High (READ + recomputed hashes).

---

## (c) Checked-clean / dissolved list

1. **All #1018-era hardening is present in both driver copies** (re-verified by full diff this pass): status matching/retirement, generation checks, response node/label/type/length matching, bounded warm-up and login budgets, sector-size guards, publish-after-warmup. `iLink_intr.c`, `iLink_TxRx.c`, `iLink_DMA.c`, `iLink_PHY.c` have zero substantive divergence (census pairs 13–15, 18).
2. **RIPTOPL-172's neighbours are guarded:** `iLinkTrRead`/`iLinkTrWrite` range-check `trContext`, `buffer` and `nBytes` (`iLink.c:217-267` both copies); `iLinkDeleteCROMUnit` range-checks `id` (`iLink_crom.c:93-103`). ILINK-02 is the one remaining unguarded index in that API.
3. **`iLinkAddCROMUnit` unchecked `nQuads`** (`iLink_crom.c:65-90`, both copies) — carried dissolved from R5-M5: export-only, no in-tree caller, no remote path found; `nQuads * 4` wrap is real arithmetic but has no reachable trigger at the pin. Not filed.
4. **Assault static GUID/model** (`iLink_internal.c:56-64` assault) — dissolved per R5-M5: documented in `PROVENANCE.md:42-45` as the deliberate substitute for the cdvdman calls that crash POPSTARTER.
5. **Assault device numbering** (no `sbp2FreeDevNr`, `devNr` from the scsi slot at `scsi_init`) — dissolved per R5-M5: a failed warm-up frees the scsi slot (`bd->priv = NULL`), so a lone drive still lands on device 0; the ilink failure mode (numbering by *SBP-2* slot, whose reservation survives a lost status) does not reproduce. `bd->path == NULL` in the assault copy is NULL-guarded at its consumers.
6. **CROM endianness strategies** — dissolved per R5-M5: assault natural-order structs + swab-at-build vs ilink reversed-field structs + no swab are two internally consistent ways to emit the same wire bytes, verified against each copy's own `iLink_CROM.h`. The only defect inside either strategy remains RIPTOPL-184.
7. **Assault `CDB_DATA_SIZE(v) ((v))` unmasked** (`sbp2_disk.h` assault) — dissolved: the only CDB construction site, `sbp2_queue_cmd` (both copies), rejects `data_len > XFER_BLOCK_SIZE` (65,024 < 65,536) before composing `misc`, so no data length reaching the macro can set bits above 15. Latent only for a future caller that bypasses `queue_cmd`.
8. **Assault `CDBs[12]`** — not separately filed: it is the header half of RIPTOPL-156 (the assault SCSI layer issues no CDB longer than 12 bytes, and `queue_cmd`'s `cmd_len > sizeof(cdb->CDBs)` check enforces exactly that).
9. **Read/Write request handlers' memory surface** (`iLink_intr.c`, both copies) — dissolved per R5-M5 and re-verified: `REQ_CHECK_MEM_BOUNDARIES` is defined at `iLink_internal.h:15` in both trees; out-of-CROM reads are confined to the CROM or the checked 2 MB IOP window, rejected writes drain their payload identically to accepted ones, and `ResponseHandler`'s payload drain arithmetic was re-derived quadlet-by-quadlet this pass (header + payload + trailer consumption never exceeds the `nQuads` the UBUF thread supplied, on both the copy and error paths).
10. **`PHYPacketHandler` `NodeData[nNodes++]`** (`iLink_intr.c:98-99`, both copies) — carried dissolved from R5-M5: the array is sized to the 1394 maximum node count (63) and filled only during the hardware self-ID phase.
11. **`GetBIOSVersion` unchecked `open`/`read`** (`iLink.c:61-72`, both copies) — carried dissolved from R5-M5: it reads `rom0:ROMVER`, present on every target console; no reachable failure path identified. (Distinct from ILINK-05, whose function *has* a designed error return that its caller ignores.)
12. **Assault `scsi_warmup` not zeroing `rcd`** — dissolved per R5-M5: the buffer is read only after a success return, on which the target fills the full 8-byte response.
13. **ilink RC16 fallback:** if READ CAPACITY 16 fails or returns block length 0 after an RC10 sentinel, the ilink copy publishes exactly 2³² sectors with the RC10 block length (`scsi.c:278-294` ilink). Dissolved as non-corrupting: `scsi_cmd_rw_sector` escalates by LBA (READ/WRITE 16 for `lba > 0xffffffff`), not by the published count, so no access wraps; the effect is truncated capacity on a >2 TB drive whose RC16 is broken — degraded, bounded, and the mirror image of RIPTOPL-156's wrap, which is why it is noted here rather than filed.
14. **SBP-2 retry loops in `scsi_read`/`scsi_write`** (16 retries, both copies) — dissolved: a retry re-issues the *same* LBA with the *same* buffer, so a write whose status was lost is idempotent; the publish-once rule inside `sbp2PublishCommand` (whose comment states the double-execution concern) governs the ORB level, and no second *distinct* write is generated by the retry.
15. **`sbp2PublishCommand` ignoring `iLinkTrWrite`'s result** — deliberate, documented in-tree (`ORIGIN.txt` item 4: a timeout can follow target acceptance; the wait must complete before storage is reused). Not filed.
16. **`iLinkReadCROM` return value** — checked: it returns the last quadlet read's result, and any quadlet that exhausts its 10 retries breaks the loop with a negative result; both in-tree callers (`ParseUnitCROM`, `initSBP2Disk`) test `< 0`. No partial-success-is-success path found.
17. **pccf ATA/CIS core** — `wait_status` is bounded (2,000 ms poll limit, `pcdiag_ata.h:5`) and latches `ready = 0` on any failure; IDENTIFY validation (signature pair, checksum byte, word-106 logical-sector gate, LBA48 extent checks) is complete; `pccf_ata_read`/`write` bound `lba`/`count` against `ata->sectors` before the extended-command decision, and the no-`issue_command` fallback cannot be reached for an extended transfer. `pcdiag_cis.c`'s tuple walk is bus-limit and tuple-limit bounded, its 254-byte payload buffer is exact-fit for the maximum non-`0xFF` length byte, and the CONFIG/CFTABLE decoders' malformed paths all set `info->malformed`. The two already-filed pccf items (RIPTOPL-240, -241) are the only trace/diagnostic findings in this directory.
18. **pccf MBR gate** (`pccf_mbr.h:9-25`) — partition extents are checked against the device sector count, GPT-protective `0xEE` is rejected outright, and only FAT/NTFS-type entries count as usable. The narrow type list (no `0x06`/`0x0E` FAT16 variants) is a deliberate-looking restriction consistent with the BDM/FatFs consumer; flagged here as INFERRED intent, not filed as a defect.
19. **pccf `pio_profile` / profile buffers** (`pccf_pio_profile.inc`) — read in full; the triple-read validation logic was already parent-read and declared clean in R5, and the `sectors < 4096` → restart path is single-shot (`pio_fallback_restart` prevents a loop, `pccf_hw.c:296-306`). Not re-filed.
20. **bdmassault provenance hashes** — all 9 sha256 values in `PROVENANCE.md:14-22` recomputed and matching; `ship/POPS/usbhdfsd.irx.ilink` and `ship/POPS/usbd.irx.ilink` match the `modules/bdmassault/` copies. The only provenance defect is ILINK-12's phantom-file sentence.
21. **Assault wrapper `irx_imports.h` still `#include <cdvdman.h>`** (`src/irx_imports.h:20`) — header-only; imports are controlled by `imports.lst`, whose wrapper copy has no cdvdman block (verified), and the shipped `usbhdfsd.irx.ilink` contains no cdvdman import strings. Not filed; the dangerous copy of that mistake is the *unused* nested imports file in ILINK-04.
22. **License/ORIGIN/README texts** — the two ilink LICENSE files and `modules/pccf/PS2SDK_LICENSE.txt` are byte-identical (md5 `bf71d1c0…`); the assault LICENSE is BDMAssault's own AFL copy. Attribution files, not clutter. The assault-only `doc/*.txt` set (221 lines, 2011–2012 upstream notes) is historical documentation for a vendored driver — retained deliberately in other vendored trees in this repo; not filed.

---

## (d) Per-file coverage ledger (exact; `wc -l` at the pin)

### modules/ilink/ — 28 files, 4,757 lines

| File | Lines | Status |
|---|---|---|
| `IEEE1394_bd/LICENSE` | 165 | Hash/identity-checked (identical to `iLinkman/LICENSE` and pccf license) |
| `IEEE1394_bd/Makefile` | 36 | READ |
| `IEEE1394_bd/ORIGIN.txt` | 118 | READ |
| `IEEE1394_bd/src/imports.lst` | 74 | READ |
| `IEEE1394_bd/src/include/module_debug.h` | 33 | READ |
| `IEEE1394_bd/src/include/sbp2_disk.h` | 183 | READ |
| `IEEE1394_bd/src/include/scsi.h` | 34 | READ |
| `IEEE1394_bd/src/irx_imports.h` | 35 | READ |
| `IEEE1394_bd/src/main.c` | 33 | READ |
| `IEEE1394_bd/src/sbp2_driver.c` | 828 | READ |
| `IEEE1394_bd/src/scsi.c` | 455 | READ |
| `iLinkman/LICENSE` | 165 | Hash/identity-checked (see above) |
| `iLinkman/Makefile` | 31 | READ |
| `iLinkman/ORIGIN.txt` | 29 | READ |
| `iLinkman/README` | 64 | READ |
| `iLinkman/include/iLinkman.h` | 88 | READ |
| `iLinkman/src/exports.tab` | 31 | READ |
| `iLinkman/src/iLink.c` | 365 | READ |
| `iLinkman/src/iLink_DMA.c` | 132 | READ |
| `iLinkman/src/iLink_PHY.c` | 74 | READ |
| `iLinkman/src/iLink_TxRx.c` | 211 | READ |
| `iLinkman/src/iLink_crom.c` | 396 | READ |
| `iLinkman/src/iLink_internal.c` | 203 | READ |
| `iLinkman/src/iLink_intr.c` | 469 | READ |
| `iLinkman/src/imports.lst` | 74 | READ |
| `iLinkman/src/include/iLink_CROM.h` | 72 | READ |
| `iLinkman/src/include/iLink_internal.h` | 327 | READ |
| `iLinkman/src/irx_imports.h` | 32 | READ |

### modules/bdmassault/ — 43 files, 5,694 `wc -l` lines (incl. 741 pseudo-lines across 9 binaries, counted by bytes below)

| File | Lines | Status |
|---|---|---|
| `LICENSE` | 166 | READ (BDMAssault AFL copy; differs from ilink LICENSE) |
| `PROVENANCE.md` | 111 | READ + all 9 sha256 recomputed |
| `ilink_bd_assault/Makefile` | 36 | READ |
| `ilink_bd_assault/README.md` | 116 | READ |
| `ilink_bd_assault/include/bdm.h` | 77 | READ |
| `ilink_bd_assault/src/exports.tab` | 30 | READ |
| `ilink_bd_assault/src/imports.lst` | 77 | READ |
| `ilink_bd_assault/src/irx_imports.h` | 36 | READ |
| `ilink_bd_assault/src/IEEE1394_bd/src/imports.lst` | 73 | READ |
| `ilink_bd_assault/src/IEEE1394_bd/src/include/module_debug.h` | 22 | READ |
| `ilink_bd_assault/src/IEEE1394_bd/src/include/sbp2_disk.h` | 183 | READ (full diff vs fully-read ilink twin + all deltas inspected) |
| `ilink_bd_assault/src/IEEE1394_bd/src/include/scsi.h` | 19 | READ |
| `ilink_bd_assault/src/IEEE1394_bd/src/irx_imports.h` | 35 | READ |
| `ilink_bd_assault/src/IEEE1394_bd/src/main.c` | 33 | READ |
| `ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c` | 801 | READ |
| `ilink_bd_assault/src/IEEE1394_bd/src/scsi.c` | 386 | READ |
| `ilink_bd_assault/src/iLinkman/README` | 64 | READ (byte-identical to ilink twin) |
| `ilink_bd_assault/src/iLinkman/doc/Changelog.txt` | 196 | READ |
| `ilink_bd_assault/src/iLinkman/doc/KnownIssues.txt` | 6 | READ |
| `ilink_bd_assault/src/iLinkman/doc/Notes.txt` | 8 | READ |
| `ilink_bd_assault/src/iLinkman/doc/TODO.txt` | 11 | READ |
| `ilink_bd_assault/src/iLinkman/include/iLinkman.h` | 88 | READ (full diff vs fully-read ilink twin) |
| `ilink_bd_assault/src/iLinkman/src/exports.tab` | 30 | READ |
| `ilink_bd_assault/src/iLinkman/src/iLink.c` | 369 | READ |
| `ilink_bd_assault/src/iLinkman/src/iLink_DMA.c` | 132 | READ (full diff vs fully-read ilink twin; include-path + alignment only) |
| `ilink_bd_assault/src/iLinkman/src/iLink_PHY.c` | 74 | READ (full diff; include-path only) |
| `ilink_bd_assault/src/iLinkman/src/iLink_TxRx.c` | 212 | READ (full diff; include-path + alignment only) |
| `ilink_bd_assault/src/iLinkman/src/iLink_crom.c` | 398 | READ |
| `ilink_bd_assault/src/iLinkman/src/iLink_internal.c` | 190 | READ |
| `ilink_bd_assault/src/iLinkman/src/iLink_intr.c` | 469 | READ (full diff vs fully-read ilink twin; include-path + alignment only) |
| `ilink_bd_assault/src/iLinkman/src/imports.lst` | 74 | READ |
| `ilink_bd_assault/src/iLinkman/src/include/iLink_CROM.h` | 72 | READ |
| `ilink_bd_assault/src/iLinkman/src/include/iLink_internal.h` | 327 | READ (full diff vs fully-read ilink twin) |
| `ilink_bd_assault/src/iLinkman/src/irx_imports.h` | 32 | READ (byte-identical to ilink twin) |
| `usbd.irx.ata` | 42,749 B | Binary: md5/sha256 taken, `file` identified (IOP ELF) |
| `usbd.irx.ilink` | 48,500 B | Binary: md5/sha256 taken; identical to `usbd.irx.usbexfat` |
| `usbd.irx.mmce` | 11,841 B | Binary: md5/sha256 taken |
| `usbd.irx.usbexfat` | 48,500 B | Binary: md5/sha256 taken; identical to `usbd.irx.ilink` |
| `usbhdfsd.irx.ata` | 21,837 B | Binary: md5/sha256 taken |
| `usbhdfsd.irx.ilink` | 27,769 B | Binary: md5/sha256 taken; matches `ship/POPS/` copy |
| `usbhdfsd.irx.mmce` | 19,733 B | Binary: md5/sha256 taken |
| `usbhdfsd.irx.mx4sio` | 14,993 B | Binary: md5/sha256 taken |
| `usbhdfsd.irx.usbexfat` | 34,508 B | Binary: md5/sha256 taken |

### modules/pccf/ — 19 files, 1,483 lines

| File | Lines | Status |
|---|---|---|
| `Makefile` | 18 | READ |
| `PS2SDK_LICENSE.txt` | 165 | Hash/identity-checked (identical to ilink LICENSEs) |
| `README.md` | 10 | READ |
| `format.inc` | 57 | READ |
| `imports.lst` | 20 | READ |
| `irx_imports.h` | 8 | READ |
| `main.c` | 154 | READ |
| `pccf_ata.c` | 278 | READ |
| `pccf_ata.h` | 33 | READ |
| `pccf_hw.c` | 313 | READ |
| `pccf_mbr.h` | 26 | READ |
| `pccf_pio_bulk.inc` | 30 | READ |
| `pccf_pio_profile.inc` | 28 | READ |
| `pcclog.h` | 4 | READ |
| `pcdiag_ata.h` | 22 | READ |
| `pcdiag_cf.c` | 72 | READ |
| `pcdiag_cf.h` | 19 | READ |
| `pcdiag_cis.c` | 196 | READ |
| `pcdiag_cis.h` | 30 | READ |

**Totals:** 90 files; 11,934 `wc -l` lines across the three trees (4,757 + 5,694 +
1,483), of which the 9 `.irx.*` binaries contribute 741 pseudo-lines and are
accounted by byte size + hashes above. Every source, header, build and text file
was line-read in full except the three license texts (495 lines total), which were
hash/identity-verified as byte-identical AFL texts rather than audited as code —
named here explicitly. **Unread: none.**
