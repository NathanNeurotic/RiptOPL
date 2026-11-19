# RiptOPL Round 9 — modules/bdm/ full line-read

- Repo: RiptOPL, pin `2e347d5ac885cd0ff73e9ad4143781946396f492` (verified `git rev-parse HEAD` at start; clone left git-clean, no edits).
- Scope: every `.c/.h/.S/.s/.cpp` under `modules/bdm/` — 27 files, 26,787 lines. Vendored status: `src/` + `libbdm/` are vendored from ps2dev/ps2sdk (pin in `modules/bdm/ORIGIN.txt`) with documented RiptOPL local changes; `bdmfs_fatfs/fatfs/` is vendored FatFs R0.16 (fjtrujy/FatFs) plus ps2sdk `bdmfs_fatfs/src` glue (`modules/bdm/bdmfs_fatfs/ORIGIN.txt`). For pristine vendored FatFs code only dead-in-build / local-divergence / integration defects are numbered; pristine upstream behaviour is listed as dissolved/observed, not filed.
- Cross-referenced, NOT re-filed: RIPTOPL-012, -091, -160, -161, -163, -170, -171, -178, -179, -249, -262.
- Candidates are sorted by benefit, highest first.

---

## BDM-01 — GPT connect returns success (0) when its scratch allocation fails, so BDM claims the device and FatFs is never tried

- **Category:** 6 (bug)
- **Locations:** `modules/bdm/src/part_driver_gpt.c:37-42`
- **Problem:** On `AllocSysMemory` failure `part_connect_gpt` does `return 0`. In BDM's mount protocol 0 means "mounted": `bdm_try_mount` (`modules/bdm/src/bdm.c`, `bdm_try_mount`) records `mount->fs = g_fs[i]` on a 0 return and stops offering the device to any other filesystem. A transient IOP allocation failure at plug time therefore permanently wedges the device as "mounted to GPT" — no partitions, no FatFs attempt, no retry until replug. Every other failure path in the same function returns -1, and the MBR driver's allocation failure (`part_driver_mbr.c`, `part_connect_mbr`) also returns -1, so this is a lone inconsistency, not a convention.
- **Fix in words:** Return -1 on the allocation-failure path, like the function's other failures.
- **Benefit:** Removes a silent, unrecoverable "drive never appears" failure mode triggered by ordinary IOP memory pressure.
- **Effort:** S
- **Risk:** Low — the -1 path is already the exercised failure behaviour of both partition drivers.
- **Confidence:** READ

## BDM-02 — `USBMASS_DEVCTL_STOP_UNIT` and `USBMASS_DEVCTL_STOP_ALL` bodies are swapped in `fs_devctl`

- **Category:** 6 (bug)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:1019-1031` (helpers at `fs_driver.c:177-195`)
- **Problem:** `case USBMASS_DEVCTL_STOP_UNIT:` calls `fatfs_fs_driver_stop_all_bd()` — it unmounts and stops **every** mounted volume. `case USBMASS_DEVCTL_STOP_ALL:` resolves the caller's one volume and calls `fatfs_fs_driver_stop_single_bd(vol)` — it stops only that one. The two bodies do exactly the opposite of what their command names say. A caller asking to stop one unit (e.g. before ejecting one USB stick) tears down all filesystems; a caller asking to stop everything leaves all but one device running. The constant values themselves live in the PS2SDK `usbhdfsd-common.h` (outside this tree), so the swap verdict rests on the names, which are unambiguous, and on the helper names inside this same file, which match the opposite call sites.
- **Fix in words:** Swap the two case bodies: STOP_UNIT stops the resolved single volume, STOP_ALL stops all volumes.
- **Benefit:** Devctl callers get the stop scope they asked for; prevents collateral unmount of unrelated drives and incomplete shutdown on STOP_ALL.
- **Effort:** S
- **Risk:** Low — if any in-tree caller had compensated for the swap it would be visible; no caller of either devctl exists elsewhere in this repo (grep-verified).
- **Confidence:** READ (bodies and call sites); INFERRED (constant semantics, from names — definitions are in the external SDK header)

## BDM-03 — GPT scan's fallback path trusts the header's entry count unbounded: a crafted disk can make mount read sectors indefinitely

- **Category:** 6 (bug / security)
- **Locations:** `modules/bdm/src/part_driver_gpt.c:84-98` (clamp at `:86-95` applies only inside the plausible-extent branch)
- **Problem:** The RiptOPL clamp on `entryCount` (cross-ref the round-9 GPT work behind RIPTOPL-160/-161) only runs when `first_lba > partition_table_lba` and the span is ≤ `GPT_MAX_ARRAY_SECTORS`. Any other header keeps `stopAtUnused = 1` **and the header's raw `partition_count`** (a disk-controlled u32, up to ~4 billion). The scan loop then reads one sector per `entriesPerSector` entries until it happens to hit an all-zero type GUID. A crafted GPT header with a huge count and a table of non-zero-type entries that fail the LBA sanity check (`continue`, no mount, no termination) makes the BDM thread walk the disk sector-by-sector at mount time — effectively hanging device enumeration. The header's CRC32s, `header_size`, `current_lba` and revision are never validated either (the code's own TODO at `part_driver_gpt.c` admits the CRC omission); notably the vendored FatFs parser in the same tree (`fatfs/ff.c`, `test_gpt_header`) does validate header CRC, entry size and caps the count at 128. Distinct from RIPTOPL-160/-161: those are partition-extent validation gaps; this is the scan-length bound that exists only on one of the two scan paths.
- **Fix in words:** Apply a hard entry/sector budget on both paths (e.g. clamp `entryCount` to the same 2048-sector array budget regardless of the `first_lba` heuristic, stopping the scan when the budget is spent), keeping the stop-at-first-unused behaviour inside that budget.
- **Justification:** The fix adds one comparison/clamp reusing the existing `GPT_MAX_ARRAY_SECTORS` constant — no new state or tables.
- **Benefit:** Mount-time denial of service from a crafted/corrupt disk becomes a bounded 2048-sector worst case on every header shape.
- **Effort:** S
- **Risk:** Low — real GPT arrays are ≤128 entries by spec convention and already fit the budget on the plausible path.
- **Confidence:** READ

## BDM-04 — GPT parser ignores the header's `partition_entry_size` and assumes every entry is 128 bytes

- **Category:** 6 (bug)
- **Locations:** `modules/bdm/src/part_driver_gpt.c:68` (`entriesPerSector = bd->sectorSize / sizeof(gpt_partition_table_entry)`); field defined and never read at `modules/bdm/src/include/gpt_types.h:36`
- **Problem:** UEFI requires entry size to be a multiple of 128, not necessarily 128. The parser never reads `pGptHeader->partition_entry_size`; it strides the array by `sizeof(gpt_partition_table_entry)` (128). On a disk formatted with 256-byte entries (produced by some partitioning tools), every entry after the first is read at the wrong offset — GUIDs and LBAs are garbage, so real partitions are missed or phantom bounds are computed from mid-entry bytes. The vendored FatFs GPT check in the same tree rejects any header whose entry size is not exactly 128 (`fatfs/ff.c`, `test_gpt_header`); the BDM driver neither honours the size nor rejects a mismatch.
- **Fix in words:** Either stride by the header's `partition_entry_size` (validating it is a non-zero multiple of 128 no larger than the sector size), or reject headers whose entry size is not 128, matching the FatFs parser's rule. The reject form is the LESS change.
- **Benefit:** GPT disks with non-128-byte entries stop being silently misparsed.
- **Effort:** S
- **Risk:** Low for the reject form (such disks currently misparse anyway); Medium for the stride form (new parsing arithmetic).
- **Confidence:** READ

## BDM-05 — GPT connect lacks the partition-device filter the MBR connect has, so every partition pseudo-device is re-probed as a GPT disk

- **Category:** 6 (bug) / 3 (overengineering-by-omission)
- **Locations:** `modules/bdm/src/part_driver_gpt.c:23-50` (no filter present); contrast `modules/bdm/src/part_driver_mbr.c:46-49`
- **Problem:** `part_connect_mbr` refuses any block device with `sectorOffset != 0` because such a device is a partition, not a raw disk. `part_connect_gpt` has no equivalent check (nor a `parNr` check): BDM offers every newly created partition pseudo-device to the GPT driver too, which reads the partition's own LBA 1 and interprets whatever filesystem bytes live there as a GPT header. Normally the signature check saves it, but the probe is wasted work on every partition mount, and a partition whose data at offset 512 begins with the bytes `EFI PART` plus a plausible header would spawn nested phantom partitions (bounded only by `MAX_PARTITIONS` and the BDM-03 budget). The asymmetry shows the filter was simply never carried over.
- **Fix in words:** Add the same `sectorOffset != 0` early return at the top of `part_connect_gpt`.
- **Benefit:** Removes a wasted probe per partition and a false-positive nested-partition path, for one copied guard.
- **Effort:** S
- **Risk:** Low — a genuine GPT header can only live on a raw device (offset 0); GPT-on-partition is not a supported layout anywhere else in this stack.
- **Confidence:** READ

## BDM-06 — `fatfs_fs_driver_get_mounted_bd_from_index` bounds check is off by one and admits negative indices

- **Category:** 6 (bug / security)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:137-147` (check at `:140`)
- **Problem:** `if (mount_info_index > FATFS_FS_DRIVER_MOUNT_INFO_MAX) return NULL;` — with `FF_VOLUMES` = 10 the array has indices 0–9, so index 10 passes the check and reads `fs_driver_mount_info[10]`, one element past the array, returning whatever pointer-sized garbage sits there as a `struct block_device *`. Negative indices pass too and index before the array. Callers feed it FatFs `pdrv` values (`fatfs/diskio.c` `disk_status`/`disk_initialize`/`disk_read`/`disk_write`/`disk_ioctl`, and `fs_driver.c` `get_frag_list` / `fs_ioctl2` via `fs->pdrv`); those are volume-bounded in the current FatFs configuration, which is why this is latent rather than live — but the function's one job is to be the guard, and the guard is wrong at both ends.
- **Fix in words:** Reject `mount_info_index < 0 || mount_info_index >= FATFS_FS_DRIVER_MOUNT_INFO_MAX`.
- **Benefit:** The single choke point every diskio call passes through actually bounds its index; one-character-class fix.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ (the check and array size); INFERRED (present unreachability of index 10 via FatFs `pdrv`)

## BDM-07 — Typed-driver pool is never reclaimed: after 8 distinct device paths, later device types mount but are unreachable by name; `InitFS` also resets the count without unregistering

- **Category:** 8 (half-finished feature) / 6 (bug)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:196-250` (pool + `fs_ensure_typed_driver`), `:1089` (count reset in `InitFS`); no `DelDrv` for typed drivers anywhere in the file
- **Problem:** Each unique `bd->path` (other than `"mass"`) permanently consumes one of `FS_MAX_TYPED_DRIVERS` (8) iomanX registrations. Nothing ever unregisters one — not `disconnect_bd`, not `InitFS` (which instead zeroes `fs_typed_driver_count`, orphaning the still-registered drivers and letting a re-init register duplicates/overwrite live slots). Once the pool is exhausted, `fs_ensure_typed_driver` silently gives up: the volume still mounts in FatFs, but no iomanX device exists under that path prefix, so EE code addressing the device by its typed name (the entire point of the pool) fails with no diagnostic beyond a debug print compiled out in release. The feature is finished on the connect path and unfinished on every lifecycle path.
- **Fix in words:** Unregister and free a typed driver when its last mounted volume disconnects (reference-count by path in `disconnect_bd`), and in `InitFS` `DelDrv` any still-registered typed names before resetting the count.
- **Justification:** Adds only lifecycle bookkeeping for registrations the code already makes; no new mechanism.
- **Benefit:** Typed-name access survives arbitrary plug/unplug mixes instead of silently dying after the eighth device type.
- **Effort:** M
- **Risk:** Medium — iomanX unregister-while-open semantics need care; gate the free on zero mounted volumes for that path.
- **Confidence:** READ

## BDM-08 — `fs_dread` can hand EE an unterminated directory name for a maximum-length (255-char) LFN

- **Category:** 6 (bug)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:650`
- **Problem:** `strncpy(buffer->name, fno.fname, 255);` copies into `iox_dirent_t.name[256]`. `FILINFO.fname` holds up to `FF_LFN_BUF` (255) characters plus its terminator. When a directory entry's name is exactly 255 characters, `strncpy` copies 255 non-NUL bytes and — per strncpy semantics — writes no terminator; byte 255 of the destination keeps whatever the previous `dread` call (or the EE caller's buffer) left there. If that byte is non-zero, EE code reading the name as a C string runs past the field. Every other name copy in this driver is either bounded with an explicit terminator (`fs_ensure_typed_driver`) or goes through FatFs' own terminated buffers.
- **Fix in words:** Copy with bound 255 and explicitly write `buffer->name[255] = '\0'` afterwards (or copy 256 and rely on `fno.fname`'s own terminator — the explicit write is the smaller, clearer change).
- **Benefit:** Closes an EE-visible over-read on legal, if rare, maximum-length filenames.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ

## BDM-09 — One global semaphore serializes all FatFs I/O on all volumes, defeating FatFs' own per-volume reentrancy

- **Category:** 5 (performance, structural only)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:83-117` (`_fs_lock`/`_fs_unlock`); held across blocking device I/O in every op, e.g. `fs_read`/`fs_write`/`fs_open`/`connect_bd` (which holds it across `f_mount`)
- **Problem:** Every operation on every volume — reads on an internal HDD partition, directory scans on USB, VMC checks on MX4SIO — takes the same `_fs_lock` for the full duration of the underlying device I/O. FatFs itself is built reentrant (`FF_FS_REENTRANT 1`, per-volume mutexes in `fatfs/ffsystem.c`), and the driver's shared state is limited to the mount table and the FIL/DIR pools, yet the lock granularity is "the whole subsystem". A slow device (spinning USB, network-backed block device) stalls unrelated volumes' I/O behind it. This is structural, not a tuning issue: no constant changes it.
- **Fix in words:** Narrow the lock to the shared tables (mount-info lookup/assignment and FIL/DIR pool allocation), and let FatFs' per-volume mutex carry the per-call exclusion it already provides; `connect_bd`/`disconnect_bd` keep the wide lock only around mount-table mutation, not across `f_mount`'s device reads.
- **Justification:** Removes locking rather than adding any; the per-volume mutex it relies on already exists in the build.
- **Benefit:** Independent devices stop head-of-line blocking each other; in-game and UI I/O on different volumes can overlap.
- **Effort:** L
- **Risk:** High if done casually — the FIL/DIR pools and `fs->lfnbuf`/`dirbuf` sharing under `FF_USE_LFN 2` (stack buffers are per-call, safe) need a careful audit first. Filed as a structural observation with a direction, not a quick win.
- **Confidence:** READ (lock scope); INFERRED (magnitude of user-visible stalls)

## BDM-10 — Static FIL/DIR pools are sized far beyond anything this driver can be asked to serve (128 FIL + 16 DIR in IOP BSS)

- **Category:** 4 (size/memory)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:305-309`
- **Problem:** `fil_structures[128]` and `dir_structures[16]` are permanent IOP BSS. With exFAT enabled and tiny-buffer mode, each `FIL`/`DIR` carries a full `FFOBJID` (including the exFAT containing-directory fields) — on the order of 80–100 bytes each, so the pools cost roughly 12–15 KB of the ~2 MB IOP forever, to serve an iomanX filesystem whose realistic concurrency is the EE loader's handful of open files plus in-game use through entirely separate drivers. Nothing in the tree opens files through this driver at anywhere near 128-wide concurrency; the mount table itself caps volumes at 10. The number looks inherited, not chosen.
- **Fix in words:** Size the pools to a justified bound (e.g. 32 files / 8 dirs, still multiples of the 10-volume cap) and let `fs_find_free_*`'s existing `-EMFILE` path do its job.
- **Benefit:** Reclaims on the order of 9–11 KB of IOP BSS permanently, in the subsystem (BDM) whose cache sizing already proved IOP headroom is the binding constraint (see `libbdm` cache cap, `bd_cache.c` `CACHED_DEVICES_MAX` comment).
- **Effort:** S
- **Risk:** Medium — an unnoticed consumer opening >32 files through `mass:`/typed drivers would newly hit `-EMFILE`; needs a usage check on the EE side before shrinking.
- **Confidence:** INFERRED (pool-entry size and the concurrency claim are estimated from the structs and in-tree usage, not measured from a built IRX)

## BDM-11 — `ff.c` carries a local debug divergence that the vendoring record does not list

- **Category:** 7 (build/repo clutter — vendored-diff hygiene)
- **Locations:** `modules/bdm/bdmfs_fatfs/fatfs/ff.c:26` (`#include "module_debug.h"`), `modules/bdm/bdmfs_fatfs/fatfs/ff.c:3521-3523` (debug hook inside `mount_volume`)
- **Problem:** `modules/bdm/bdmfs_fatfs/ORIGIN.txt` lists five local changes to the FatFs tree and states the sources "keep their upstream formatting so they diff cleanly against the pins", but this sixth change is not in the list: `ff.c` includes the driver's `module_debug.h` and `mount_volume` contains an unconditional `u64 winsect = fs->winsect;` plus `DEBUG_U64_2XU32`/`M_DEBUG` calls. The variable is dead in non-DEBUG builds (assigned, used only by the compiled-out debug macros), and the divergence means a future re-vendor diff will show an unexplained hunk in the middle of upstream's most security-sensitive function. Either the change or the record is wrong; the record is what the next maintainer will trust.
- **Fix in words:** Add the hook to ORIGIN.txt's local-changes list (preferred — the debug output is presumably wanted), or drop the three lines. Do not leave code and record disagreeing.
- **Benefit:** Re-vendoring against the next FatFs pin stays a mechanical diff instead of an archaeology exercise.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ

## BDM-12 — `fs_read`/`fs_write` collapse every FatFs error to a 0 return, indistinguishable from EOF / zero-progress

- **Category:** 6 (bug)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:465` (`fs_write`), `:484` (`fs_read`)
- **Problem:** Both return `(ret == FR_OK) ? count : 0`. A disk error mid-transfer, an invalidated object, or a removed device all surface to EE as a clean 0 — EOF for readers, "wrote nothing, no error" for writers. Callers that loop until error or completion cannot tell a dead device from end-of-file. The rest of the same file returns negative errnos (`fs_open`, `fs_close`, `fs_remove`, …), so the convention exists in-file; these two ops are the exception.
- **Fix in words:** Return a negative errno (e.g. `-EIO`) when the FatFs call fails, keeping the byte count on success.
- **Benefit:** EE-side error handling (retry, abort, user message) becomes possible on real device failures instead of silent truncation.
- **Effort:** S
- **Risk:** Medium — a caller that treats any non-negative short count as EOF already copes; a caller that loops on 0 expecting progress currently spins, which the change also fixes. Behaviour change needs a call-site glance on the EE side.
- **Confidence:** READ

## BDM-13 — `bdm_get_bd` silently leaves the tail of the caller's array unwritten, and `bdm_connect_bd` silently drops devices when the mount table is full

- **Category:** 6 (bug)
- **Locations:** `modules/bdm/src/bdm.c:143-152` (`bdm_get_bd`), `modules/bdm/src/bdm.c:51-71` (`bdm_connect_bd`)
- **Problem:** `bdm_get_bd(pbd, count)` fills only `min(count, MAX_CONNECTIONS)` entries and gives the caller no count back and no terminator guarantee — a caller passing `count > 20` (or simply expecting all `count` slots written) reads uninitialised pointers from its own array. Symmetrically, `bdm_connect_bd` falls off the end of its slot loop with no log line when all 20 slots are taken: the device is dropped with less trace than the sector-size refusal path (which carefully logs and callbacks, `bdm.c` `bdm_try_mount`). Both are exported BDM API surface (`modules/bdm/src/exports.tab`), consumed by other modules that cannot see the table size.
- **Fix in words:** In `bdm_get_bd`, NULL-fill the remainder of the requested array; in `bdm_connect_bd`, add the same style of `M_PRINTF` refusal line when no slot is free. No API change.
- **Benefit:** Removes an uninitialised-pointer read for API consumers and makes table exhaustion diagnosable instead of silent.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ (the loops); INFERRED (whether any current consumer passes count > 20 — none in this scope can be checked, the export is cross-module)

## BDM-14 — Dead definitions: `mount_status` field never read or written; unused GPT GUID and attribute macros

- **Category:** 1 (dead code)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/include/fs_driver.h:11` (`int mount_status`); `modules/bdm/src/include/gpt_types.h:44` (`MS_BASIC_DATA_PARTITION_GUID`), `:56` (`GPT_PART_ATTR_PLATFORM_REQUIRED`), `:58` (`GPT_PART_ATTR_LEGACY_BOOTABLE`), `:60-62` (`GPT_PART_ATTR_TYPE_SHIFT`/`MASK`/`SPECIFIC`), `:65-68` (`GPT_BDP_ATTR_*`)
- **Problem:** `mount_status` is declared in every mount-info entry and never touched anywhere (grep-verified across the tree) — the mount state it implies is actually carried by `mounted_bd != NULL`. In `gpt_types.h`, of the four GUID constants only `NULL_GUID`, `MS_RESERVED_PARTITION_GUID` and `EFI_SYSTEM_PARTITION` are used; `MS_BASIC_DATA_PARTITION_GUID` (the one GUID a data-partition filter would want) is defined and unused, as are all the attribute macros except `GPT_PART_ATTR_IGNORE` (`part_driver_gpt.c:153`). The header also carries a TODO (`part_driver_gpt.c`, "Check type specific partition flags: read-only, hidden, etc.") that these macros were presumably staged for and never wired to — including `GPT_BDP_ATTR_READ_ONLY`, which matters: a read-only GPT partition is currently mounted fully writable (cross-ref RIPTOPL-091's read-only-path theme; the read-only *attribute* itself is unimplemented, not just unenforced).
- **Fix in words:** Delete `mount_status` and the unused macros/GUID, or wire `GPT_BDP_ATTR_READ_ONLY` + `GPT_PART_ATTR_PLATFORM_REQUIRED` into the mount decision if read-only partitions are meant to be honoured. Deletion is the LESS default; keep this finding as the record that the read-only attribute is unimplemented either way.
- **Benefit:** Removes a fake state field and a header that promises partition-attribute handling the driver does not do.
- **Effort:** S
- **Risk:** Low for deletion (all grep-verified unused in-tree; the header is internal to `modules/bdm/src`).
- **Confidence:** READ

## BDM-15 — MBR sanity check's `valid` counter is a tautology: it counts every entry unconditionally and then requires the count to be 4

- **Category:** 1 (dead code) / 3 (overengineering)
- **Locations:** `modules/bdm/src/part_driver_mbr.c:11-33` (`valid` declared `:16`, incremented unconditionally `:29`, tested `:32`)
- **Problem:** `valid++` runs for every one of the 4 entries regardless of content, so `valid == 4` is always true and the function's result is exactly `active > 0` plus the early veto inside the loop. The counter, its comment ("Considered at least a valid partition"), and half the return expression are machinery that computes a constant. It misleads readers about what is being validated — in a function whose actual veto behaviour (one typed entry with `first_lba` 0 or past the device rejects the whole disk) is the part worth seeing clearly.
- **Fix in words:** Delete the `valid` variable and return `active > 0` (preserving the in-loop early `return 0` veto exactly).
- **Benefit:** The sanity check says what it does; no behaviour change.
- **Effort:** S
- **Risk:** Low — equivalence is by inspection of the three uses.
- **Confidence:** READ

## BDM-16 — `part_read` and `part_write` are near-identical copies (bounds check + debug block + forwarding)

- **Category:** 2 (duplication)
- **Locations:** `modules/bdm/src/part_driver.c:46-90`
- **Problem:** The two functions differ only in the final forwarding call; the NULL checks, the RiptOPL partition-bounds check, and the DEBUG block are copied verbatim. This is exactly the shape that produced the already-filed bd_defrag duplication (RIPTOPL-249, cross-ref — that finding covers `libbdm/src/bd_defrag.c` only, not this pair): a future bounds fix applied to one twin and not the other re-opens a partition-escape on one direction. The bounds check is the security-relevant part of both functions, which is what makes the duplication worth more than its line count.
- **Fix in words:** Factor the shared prologue (priv/bounds validation returning the translated sector or an error) into one small static helper both wrappers call. Net lines decrease.
- **Benefit:** One place to maintain the partition-bounds invariant for both directions.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ

## BDM-17 — FatFs is configured for OEM code page 869 (Greek 2), so non-ASCII short-name handling runs through Greek tables on a US user's disks

- **Category:** 3 (overengineering / misconfiguration)
- **Locations:** `modules/bdm/bdmfs_fatfs/fatfs/include/ffconf.h:87` (`FF_CODE_PAGE 869`)
- **Problem:** With `FF_LFN_UNICODE 0`, every SFN up-casing, OEM↔Unicode conversion of 8.3 names, and LFN-to-SFN fallback conversion goes through the CP869 tables (`ff.c` `ExCvt` = `TBL_CT869`; `ffunicode.c` `uc869`, the only SBCS table compiled in). CP869 is Greek 2; the disks this driver mounts in practice are written by Windows PCs in CP437/CP850 locales. Bytes ≥ 0x80 in short filenames (accented European names, symbols) therefore case-fold and convert by Greek rules — names can fail to match, display wrong, or generate different SFNs than the PC that wrote the disk expects. The value appears inherited from the upstream fork's default rather than chosen (ORIGIN.txt lists only `FF_USE_EXPAND` as a deliberate ffconf change). This is a compatibility judgement call, not a clear bug: changing the code page changes how existing on-disk SFNs decode, which is precisely why it should be a recorded decision instead of an inherited accident.
- **Fix in words:** Decide deliberately: if Western-locale disks are the target, set `FF_CODE_PAGE 437` (the FatFs default) at the next re-vendor and note it in ORIGIN.txt; if 869 is intentional, record why in ORIGIN.txt and close this finding.
- **Benefit:** Non-ASCII filename behaviour becomes a chosen, documented property instead of an inherited surprise.
- **Effort:** S
- **Risk:** Medium — any code-page change re-decodes existing SFN-only names on users' disks differently; needs the user's call, not a silent swap.
- **Confidence:** READ (the configuration and which tables compile); INFERRED (that 869 is inherited rather than deliberate — ORIGIN.txt is silent about it)

---

## Checked and clean / dissolved candidates

- **`libbdm/src/bd_cache.c` (269 lines)** — the RiptOPL refill fix, slot cap (`CACHED_DEVICES_MAX 2`), interrupt-guarded slot accounting, NULL-alloc and non-512-sector refusals all read line-by-line and hold up: invalidation precedes refill, publication only on a full 8-sector read, direct-read fallback returns the driver's real result, `_invalidate` on write covers partial overlaps, invalid slots (`0xFFFF…`) cannot false-hit `_contains`. The `0xFFFF… + 8` wrap in `_overlaps` was checked by hand and cannot produce a false overlap for any real sector. No new finding beyond the already-filed cache history in ORIGIN.txt.
- **`libbdm/src/bd_defrag.c` (101 lines)** — duplication of the read/write traversal is RIPTOPL-249, not re-filed. Otherwise clean: fragment search, clipping arithmetic (the u64→u16 narrowing in the clip branch provably stays ≤ the u16 `count_left`), and the not-found `-1` path (including `fragcount == 0`) are correct. Its O(fragments²) rescan is bounded by the small fragment lists its one in-tree consumer (cdvdman device-bdm) passes; not filed.
- **`fs_driver.c` `get_frag_list` + `vmc_allocate_contiguous` (the RiptOPL #991/#994 work)** — read in full: chain walk is length-bounded, cycle/premature-EOC/bad-cluster cases all return errors, fragment emission checks the 32-bit count overflow, the VMC reserve path validates version, size whitelist, write mode, fs type and 512-byte sectors before `f_expand`. `USBMASS_IOCTL_GET_LBA`'s NULL-`bd` dereference in the same switch is RIPTOPL-170, not re-filed. No new finding.
- **`fatfs/ffsystem.c` (230 lines)** — ~150 lines are sample branches for Win32/uITRON/uC/OS-II/FreeRTOS/CMSIS-RTOS that can never compile here (`OS_TYPE 5`). Dissolved, not filed: the file is a pinned vendored sample and ORIGIN.txt's re-vendor procedure depends on diffing cleanly against the pin; editing it buys no binary size (the branches are preprocessor-dead). Noted instead: the IOP `ff_mutex_take` ignores `FF_FS_TIMEOUT` (blocking `WaitSema`) — pristine upstream sample behaviour, recorded here as an observation only.
- **`fatfs/ff.c` config-disabled APIs** — verified by guard inspection across the full read: `f_mkfs`/`f_fdisk`/`create_partition`/`make_rand`/`xsum32` (`FF_USE_MKFS 0`, ~1,400 lines), `f_chdir`/`f_chdrive`/`f_getcwd` (`FF_FS_RPATH 0`), `f_findfirst`/`f_findnext`/`pattern_match` (`FF_USE_FIND 0`), `f_chmod`/`f_utime` (`FF_USE_CHMOD 0`), `f_getlabel`/`f_setlabel` (`FF_USE_LABEL 0`), `f_forward` (`FF_USE_FORWARD 0`), `f_gets`/`f_putc`/`f_puts`/`f_printf` (`FF_USE_STRFUNC 0`), `f_setcp` (`FF_CODE_PAGE != 0`), fast-seek CLMT (`FF_USE_FASTSEEK 0`), and the `FF_FS_LOCK` share table are all correctly `#if`-guarded and cost no IOP binary size. Pristine upstream internals (mount/FAT/dir logic) were read in full; no local divergence was found anywhere except BDM-11, and no pristine upstream bug is numbered, per the vendored lens.
- **`fatfs/ffunicode.c` (15,593 lines)** — with `FF_CODE_PAGE 869`, only the `uc869` table (lines 15200-15211), the SBCS conversion pair (15221-15265) and `ff_wtoupper` (15464-15592) compile. The four DBCS table blocks (932: 38-1892, 936: 1894-7350, 949: 7352-11622, 950: 11624-15006) and the other sixteen SBCS tables were read as data: every non-data line in those regions was enumerated and is only a table declaration, brace, or `#if`/`#endif` — there is no hidden code in the tables. The compiled-out bulk is source weight only, zero IOP cost; the compiled-table concern is RIPTOPL-012, not re-filed.
- **`fatfs/diskio.c` (169 lines)** — `GET_SECTOR_COUNT` truncates the u64 `sectorCount` into an `unsigned int` and `GET_BLOCK_SIZE` returns 0, but both commands are only consumed by `f_mkfs`/`f_fdisk`, which are compiled out (above); `disk_read`/`disk_write` pass FatFs' `UINT count` into the u16-count block-device API, but FatFs clips direct transfers to one cluster and the largest legal cluster (32,768 sectors, exFAT) still fits a u16. Dissolved as unreachable-in-this-configuration; would revive if `FF_USE_MKFS` is ever enabled.
- **MBR partition extent behaviour** — `part_connect_mbr` mounting entries whose `sector_count` runs past the device end, and the sanity check's whole-disk veto, were considered and dissolved into RIPTOPL-160/-161 plus ORIGIN.txt's *deliberately unchanged* note (the ATA driver reports `0xFFFFFFFF` sectors past 2 TiB; requiring fit would hide whole drives). Not re-filed.
- **`part_driver_gpt.c` name conversion** — `GetGPTPartitionNameAscii` writes exactly 36 chars into `partName[37]`, whose last byte is zero-initialised once and never overwritten, so the `%s` print is always terminated. The GPT signature `M_DEBUG("…%s…", pGptHeader->signature)` over-read is real but exists only in DEBUG builds (macro compiled out otherwise); dissolved as debug-only. Clean.
- **`bdm.c` disconnect paths** — `bdm_disconnect_fs` does clear `g_mount[i].fs` for mounts using the departing filesystem before removing it from `g_fs`, so no stale-fs mount survives; `bdm_disconnect_bd`'s missing `break` is harmless (a `bd` pointer can occupy only one slot unless a driver double-connects, which no in-scope driver does). Dissolved.
- **`fs_driver.c` miscellany checked and dissolved** — `fs_open` returning 1 on success and the `flags & O_RDONLY` test depend on PS2SDK iomanX flag values defined outside this tree (in-repo evidence, `tests/smb2-live/inc-browse/iomanX.h`, shows `FIO_O_RDONLY 0x0001`, consistent with the test working); `fs_lseek64`'s unvalidated `whence`/negative offsets land in FatFs `f_lseek`, which clips in read mode; `USBMASS_IOCTL_GET_DRIVERNAME`'s `*(int *)(mounted_bd->name)` packs the name's first bytes as an ID — odd but deliberate-looking and NULL-guarded by local change 3; the legacy `fs_ioctl` wrapper's fixed `datalen 1024` cannot reach the VMC paths (they require exact-size ioctl2), by design. None filed.
- **Byte-compare census** — no two source files inside `modules/bdm/` are byte-identical (all 27 md5s distinct). Same-name copies: `module_debug.h` ×3 (bdm `src`, `libbdm`, `bdmfs_fatfs`) — all differ (prefix string `BDM:` vs `FatFs:`, `MINI_DRIVER` handling, `M_DEBUG` empty-macro form); the two `irx_imports.h` differ by design (different import sets); the two `main.c` are different modules (`bdm` vs `bdmfs`). Against the rest of `modules/`: `module_debug.h` copies in mmceman/mx4sio_bd/usbmass_bd are all distinct variants of the same pattern — a repo-wide pattern, not a bdm-scope duplication finding. `bd_defrag`'s only external consumer is `modules/iopcore/cdvdman/device-bdm.c` (outside scope, noted for BDM-16 context). No same-name `.c` copy of any bdm source exists elsewhere in `modules/`.

---

## Ledger — every file read, with line counts

| # | File | Lines | Notes |
|---|------|------:|-------|
| 1 | modules/bdm/src/bdm.c | 275 | local (vendored SDK + RiptOPL changes) |
| 2 | modules/bdm/src/main.c | 41 | local |
| 3 | modules/bdm/src/part_driver.c | 143 | local |
| 4 | modules/bdm/src/part_driver_mbr.c | 134 | local |
| 5 | modules/bdm/src/part_driver_gpt.c | 184 | local |
| 6 | modules/bdm/src/irx_imports.h | 28 | local |
| 7 | modules/bdm/src/include/part_driver.h | 33 | local |
| 8 | modules/bdm/src/include/gpt_types.h | 70 | local |
| 9 | modules/bdm/src/include/mbr_types.h | 47 | local |
| 10 | modules/bdm/src/include/module_debug.h | 35 | local |
| 11 | modules/bdm/libbdm/src/bd_cache.c | 269 | local |
| 12 | modules/bdm/libbdm/src/bd_defrag.c | 101 | local |
| 13 | modules/bdm/libbdm/include/bd_cache.h | 30 | local |
| 14 | modules/bdm/libbdm/include/bd_defrag.h | 30 | local |
| 15 | modules/bdm/libbdm/src/include/module_debug.h | 36 | local |
| 16 | modules/bdm/bdmfs_fatfs/src/fs_driver.c | 1093 | local (ps2sdk glue + RiptOPL changes) |
| 17 | modules/bdm/bdmfs_fatfs/src/main.c | 67 | local |
| 18 | modules/bdm/bdmfs_fatfs/src/irx_imports.h | 28 | local |
| 19 | modules/bdm/bdmfs_fatfs/src/include/fs_driver.h | 21 | local |
| 20 | modules/bdm/bdmfs_fatfs/src/include/module_debug.h | 24 | local |
| 21 | modules/bdm/bdmfs_fatfs/fatfs/ff.c | 7257 | vendored FatFs R0.16 (one local debug divergence, BDM-11) |
| 22 | modules/bdm/bdmfs_fatfs/fatfs/ffunicode.c | 15593 | vendored FatFs; DBCS/SBCS tables read as data, all code sections read in full |
| 23 | modules/bdm/bdmfs_fatfs/fatfs/ffsystem.c | 230 | vendored FatFs sample (OS_TYPE 5 branch live) |
| 24 | modules/bdm/bdmfs_fatfs/fatfs/diskio.c | 169 | vendored FatFs glue skeleton, ps2sdk-adapted |
| 25 | modules/bdm/bdmfs_fatfs/fatfs/include/ff.h | 457 | vendored FatFs |
| 26 | modules/bdm/bdmfs_fatfs/fatfs/include/ffconf.h | 315 | vendored FatFs config (FF_USE_EXPAND local change) |
| 27 | modules/bdm/bdmfs_fatfs/fatfs/include/diskio.h | 77 | vendored FatFs |
| | **Total** | **26787** | |

- **Unread:** none. Zero files unread; zero lines unread. (Supporting non-source files also read for context, not counted in the 27: `modules/bdm/ORIGIN.txt`, `modules/bdm/bdmfs_fatfs/ORIGIN.txt`, `modules/bdm/Makefile`, `modules/bdm/libbdm/Makefile`, `modules/bdm/src/exports.tab`, both `imports.lst`, and the `bdmfs_fatfs_fork` entry in `ps2.yaml`.)
- **Candidate count:** 17 (BDM-01 … BDM-17).
