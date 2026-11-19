# RiptOPL — Round 5, Section R5-M3 (HDD/APA remainder + BDM + ISOFS + PCCF)

- **Repo / pin:** `~/workspace/audit-clones-2026-11-13/RiptOPL` at `2e347d5ac885cd0ff73e9ad4143781946396f492` (verified `git rev-parse HEAD` before reading; `git status --porcelain` empty at finish).
- **Mode:** READ ONLY. No build or hardware claims — no PS2 build exists in this VM; performance magnitudes are structural / INFERRED.
- **Dedup:** every candidate was grep-checked against `Sable-RiptOPL.md`, `Sable-RiptOPL-ROUND2.md`, `Sable-RiptOPL-ROUND3.md`, `Sable-RiptOPL-ROUND4.md` and `round4-sections/*.md` (RIPTOPL-001…144) before filing.
- **Assignment discrepancy (recorded, not hidden):** the brief said 25 files / 5,679 lines; the 26 paths actually listed total **5,909 lines** by `wc -l`. All 26 were read 1..EOF.

## Coverage ledger

| File | Lines (wc -l) | Read |
|---|---:|---|
| modules/hdd/apa/src/hdd_blkio.c | 415 | 1..EOF |
| modules/hdd/apa/src/hdd.c | 384 | 1..EOF |
| modules/hdd/apa/src/cache.c | 211 | 1..EOF |
| modules/hdd/apa/src/misc.c | 141 | 1..EOF |
| modules/hdd/xhdd/xhdd.c | 135 | 1..EOF |
| modules/hdd/apa/src/journal.c | 86 | 1..EOF |
| modules/hdd/xhdd/xatad.c | 86 | 1..EOF |
| modules/hdd/apa/src/free.c | 84 | 1..EOF |
| modules/hdd/apa/src/table_fence.c | 51 | 1..EOF |
| modules/bdm/bdmfs_fatfs/src/fs_driver.c | 1093 | 1..EOF |
| modules/bdm/src/bdm.c | 275 | 1..EOF |
| modules/bdm/libbdm/src/bd_cache.c | 269 | 1..EOF |
| modules/bdm/src/part_driver_gpt.c | 184 | 1..EOF |
| modules/bdm/bdmfs_fatfs/fatfs/diskio.c | 169 | 1..EOF |
| modules/bdm/src/part_driver.c | 143 | 1..EOF |
| modules/bdm/src/part_driver_mbr.c | 134 | 1..EOF |
| modules/bdm/libbdm/src/bd_defrag.c | 101 | 1..EOF |
| modules/bdm/bdmfs_fatfs/src/main.c | 67 | 1..EOF |
| modules/bdm/src/main.c | 41 | 1..EOF |
| modules/isofs/isofs.c | 669 | 1..EOF |
| modules/isofs/zso.c | 158 | 1..EOF |
| modules/pccf/pccf_hw.c | 313 | 1..EOF |
| modules/pccf/pccf_ata.c | 278 | 1..EOF |
| modules/pccf/pcdiag_cis.c | 196 | 1..EOF |
| modules/pccf/main.c | 154 | 1..EOF |
| modules/pccf/pcdiag_cf.c | 72 | 1..EOF |
| **Total** | **5,909** | **26 files** |

Supporting reads (not in the ledger, used only to settle candidates): `modules/hdd/apa/src/libapa.h` (journal/cache structs), `modules/hdd/apa/src/hdd_blkio.h` (`BLKIO_MAX_VOLUMES` 2 vs 10 by build), `modules/hdd/apa/src/table_fence.h` (fence scope: writes at LBA < 8 only), `modules/hdd/apa/src/apa.c:460-485` (`apaGetFormat` return convention), `modules/bdm/bdmfs_fatfs/fatfs/ff.c` (`GET_SECTOR_COUNT` consumers), `ps2.yaml` pccf targets, EE caller grep for the devctl stop codes. The two `.inc` files included by `pccf_hw.c` (`pccf_pio_bulk.inc`, `pccf_pio_profile.inc`) were **not** assigned and were not read; nothing below depends on them.

---

## New candidate findings

### R5-M3-01 / APA journal restore trusts the on-disk entry count and replays to unvalidated sectors
- **Category:** 6 (bugs/security)
- **Locations:** `modules/hdd/apa/src/journal.c:57-86` (`apaJournalRestore`); count used at `:69` and `:73`; destination taken from `journalBuf.sectors[i]` at `:77`; array definition `modules/hdd/apa/src/libapa.h:184-189` (`s32 num; u32 sectors[126];`); write side `journal.c:47-55` (`apaJournalWrite`, `journalBuf.num++` with no bound).
- **Problem:** The journal header is read straight off the disk and only its magic is checked. `num` is a signed 32-bit on-disk field with **no clamp** against the 126-entry `sectors[]` array (or against the 252-sector journal-header area, `APA_SECTOR_APAL_HEADERS` 10–262). A corrupt or crafted HDD with `num > 126` makes the restore loop (a) read destination sector numbers out of bounds past the static `journalBuf` (whatever BSS follows it becomes a target sector), (b) read its source headers ever further past the journal area into live partition data (`sector += 2` per entry), and (c) write those 2-sector blocks to the bogus destinations — at mount/probe time, before any game is loaded; with `num` near INT_MAX it is also an effective IOP hang doing disk writes throughout. Even for `num ≤ 126` no entry is validated as a plausible partition-header sector. The APA table fence (`table_fence.h`) does not mitigate this: it only restricts writes touching LBA 0–7, and replay destinations are ≥ 8 by construction of the attack. The write side has the symmetric missing bound (`apaJournalWrite` increments past 126 if more than 126 headers are ever flushed in one pass — today bounded only by the loader-controlled cache size).
- **Fix (words only):** Reject/clamp in `apaJournalRestore`: if `num < 0 || num > 126`, treat the journal as absent (reset and return success-with-no-replay, or return `-EIO` — but do not loop). Add the same `num >= 126` refusal in `apaJournalWrite`. Justification: adds a bounds check — it is the only thing standing between on-disk data and arbitrary-sector writes during recovery.
- **Benefit:** A damaged journal degrades to "no replay" instead of corrupting unrelated disk regions or hanging the IOP at startup.
- **Effort:** S · **Risk:** Low (legitimate journals are written by `apaJournalWrite` from a ≤ cache-size pool of headers and cannot exceed 126 in any configuration the loader ships) · **Confidence:** High, READ (count, array size, loop and both transfer calls all read at the pin; BSS contents past the array are INFERRED, the out-of-bounds read/write mechanism is not).

### R5-M3-02 / `apaJournalRestore` dereferences a possibly-NULL cache allocation
- **Category:** 6
- **Locations:** `modules/hdd/apa/src/journal.c:72` (`clink = apaCacheAlloc();`), used unconditionally at `:74` (`clink->header`) and `:82` (`apaCacheFree(clink)` — which itself NULL-guards, the loop does not).
- **Problem:** `apaCacheAlloc` returns NULL when the cache pool is exhausted — the contract is demonstrated by checked call sites elsewhere in the driver (e.g. `apa.c:58`, cited in round 4). Restore runs not only at first probe but on every BDM/vhdd mount (`hdd_blkio.c:121,263`), i.e. at a moment when another device's headers can hold the small (default 3-entry) pool. NULL here turns the first journal read into a DMA transfer to a near-NULL buffer and an IOP crash during device probing. This is the same defect pattern round 4 filed for `apaGetFormat` in `apa.c` (RIPTOPL-076), in a file/function round 4 did not read — not a re-file.
- **Fix (words only):** NULL-test `clink` after `:72`; on NULL skip replay, still reset the journal, return `-ENOMEM`. Justification: adds one check on an allocation whose NULL contract the same driver already honours elsewhere.
- **Benefit:** Cache pressure at mount time fails one device's journal replay instead of crashing the IOP.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-M3-03 / A failed journal replay is reported as success — and the journal is erased anyway
- **Category:** 6
- **Locations:** `modules/hdd/apa/src/journal.c:73-85` (loop `break`s on transfer error at `:76`/`:80`, then falls through to `apaCacheFree` and `return apaJournalReset(device);` at `:85`); callers that trust the return: `modules/hdd/apa/src/hdd.c:331-334`, `modules/hdd/apa/src/hdd_blkio.c:121-124,263-266`.
- **Problem:** If any replay read/write fails mid-sequence, `ret` holds `-EIO`, but the function's return value is whatever `apaJournalReset` returns (normally 0). Two losses compound: (1) callers proceed as if recovery completed, although the partition table is in a partially-replayed state; (2) the reset **erases the journal**, destroying the only copy of the headers that were not replayed, so the next boot cannot finish the job either. Recovery metadata is discarded at exactly the moment it is known to be needed.
- **Fix (words only):** Preserve `ret`: if the loop broke on error, return it without resetting the journal (leave the journal on disk for a later retry), and only reset after a fully successful replay.
- **Benefit:** Partial recovery stays recoverable and visible to callers instead of silently becoming permanent table damage.
- **Effort:** S · **Risk:** Low–Med (a permanently unreadable journal sector would now be retried each mount instead of discarded — that is the conservative direction for a recovery log) · **Confidence:** High, READ.

### R5-M3-04 / APA cache flush clears the dirty flag and drops errors on failed header writes
- **Category:** 6
- **Locations:** `modules/hdd/apa/src/cache.c:87-105` (`apaCacheTransfer` executes `clink->flags &= ~APA_CACHE_FLAG_DIRTY;` at `:101` unconditionally, success or failure); `cache.c:111-126` (`apaCacheFlushAllDirty` ignores the returns of `apaJournalWrite` `:117`, `apaJournalFlush`, and the per-entry `apaCacheTransfer` `:123`, and returns `apaJournalReset(device)`).
- **Problem:** The flush sequence is journal-write → journal-flush → header writes → journal reset. If a header write to the disk fails (bad sector, device dropout), the entry is nevertheless marked clean, the failure is reported to nobody, and the journal is then reset — erasing the backup copy of the header that never made it to its home sector. The partition table on disk can therefore silently diverge from what every layer above believes was written, with the recovery path dismantled in the same operation.
- **Fix (words only):** In `apaCacheTransfer`, clear the dirty flag only when the transfer returned 0; in `apaCacheFlushAllDirty`, propagate the first error and skip `apaJournalReset` when any journal or header write failed. Justification: changes only error handling of existing writes; no new mechanism.
- **Benefit:** A failed table write stays dirty, journaled, and reported instead of being laundered into a successful flush.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-M3-05 / Volume-index guards use `>` instead of `>=` (and miss negatives), admitting a one-past-the-array access
- **Category:** 6
- **Locations:** `modules/hdd/apa/src/hdd_blkio.c:211-227` (`hdd_blkio_vhdd_validate`: `if (slot > BLKIO_MAX_VOLUMES)` at `:216`, then `&hdd_blkio_vhdd_diskinfo[slot]` at `:221`; write side `hdd_blkio_vhdd_mount` stores `diskinfo->fd` via the same index at `:251` and indexes `hddDevices[slot]` at `:259-272`); sibling guard `modules/bdm/bdmfs_fatfs/src/fs_driver.c:137-145` (`if (mount_info_index > FATFS_FS_DRIVER_MOUNT_INFO_MAX)` at `:140`, array read at `:143`); negative side `modules/hdd/apa/src/hdd_blkio.c:98-106` (`>=` checked at `:101`, negative `device` not checked before `fs_driver_mount_info[mount_info_index]` at `:104`).
- **Problem:** In the non-ATAD build `BLKIO_MAX_VOLUMES` is 10 (`hdd_blkio.h:65`), so valid slots are 0–9, but `slot == 10` passes validation and indexes one entry past both static arrays — in `vhdd_mount` that is an out-of-bounds **write** (`diskinfo->fd = fd`). The fatfs helper repeats the slip for `FF_VOLUMES` = 10 volumes (index 10 reads one past `fs_driver_mount_info`). The unit/slot values originate from the ioman unit field via `hdd_fio.c`'s mount path (`hdd_blkio_vhdd_mount(f->unit, …)`), i.e. from the device string a caller asks to mount. The BDM-side getter additionally accepts negative indices. Each site is one comparison away from the guard its neighbour already writes correctly (`hdd_blkio.c:101` uses `>=`).
- **Fix (words only):** Change both `>` guards to `>=` and add `index < 0` rejection in the getters/validator. Justification: pure bounds checks on indices that reach static arrays.
- **Benefit:** Closes a one-slot out-of-bounds read/write in three mount/transfer entry points for the cost of three comparisons.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the arithmetic (READ); Med for reachability of `unit == 10` from a real mount request (INFERRED from the `hdd_fio.c` caller, which belongs to a prior round's file).

### R5-M3-06 / `fs_ioctl2` `USBMASS_IOCTL_GET_LBA` dereferences a NULL block device — the one sibling case the in-file fix missed
- **Category:** 6
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:879-891` (`bd = fatfs_fs_driver_get_mounted_bd_from_index(file->obj.fs->pdrv);` at `:888`, dereferenced as `bd->sectorOffset` at `:890` with no NULL test).
- **Problem:** The two neighbouring cases in the same switch were already fixed for exactly this hazard: `USBMASS_IOCTL_GET_DRIVERNAME` (`:893-902`, with a comment recording that the SDK version faulted when the device had gone away) and `USBMASS_IOCTL_GET_DEVICE_NUMBER` (`:912-930`) both NULL-check the same lookup. `GET_LBA` checks only its output buffer (`:882-886`) and then dereferences the possibly-NULL `bd`. A file still open on a volume whose device was disconnected (USB removal between open and ioctl2 — the precise scenario the sibling comment describes) faults the IOP instead of returning `-ENXIO`. `get_frag_list` in the same file (`:756`) also checks the identical lookup, making `GET_LBA` the lone exception in the file.
- **Fix (words only):** Give `GET_LBA` the same `bd == NULL → -ENXIO` guard its siblings use. Justification: adds the check the file's own established pattern requires.
- **Benefit:** Device removal during an open file's lifetime returns an error instead of crashing the filesystem driver.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (all four call sites of the lookup in this switch/file compared directly).

### R5-M3-07 / GPT parser never validates an entry's extent: reversed or off-device ranges become huge partitions
- **Category:** 6
- **Locations:** `modules/bdm/src/part_driver_gpt.c:130` (only sanity check: `first_lba < header->first_lba || last_lba > header->last_lba`); `:175` (`sectorCount = last_lba - first_lba + 1`); `:68` (`entriesPerSector = bd->sectorSize / sizeof(gpt_partition_table_entry)` — the header's own entry-size field is never consulted); bounds that trust the result: `modules/bdm/src/part_driver.c:47-90` (`part_read`/`part_write` bound I/O only against `bd->sectorCount`).
- **Problem:** Three trust gaps in one parse: (1) an entry with `first_lba > last_lba` passes the `:130` check whenever both values lie inside the header's usable range, and `last - first + 1` then underflows to an astronomically large `sectorCount`; (2) the header's `last_lba` is itself disk-supplied and is never checked against `bd->sectorCount`, so a partition can be registered extending past the physical device; (3) the stride assumes every GPT uses 128-byte entries, ignoring the header's entry-size field, so a spec-legal disk with larger entries is parsed at the wrong offsets. Because `part_read`/`part_write` deliberately use the partition's `sectorCount` as their only fence ("never write past the partition end into a neighbouring partition", `part_driver.c:63`), a poisoned count disarms that fence: the resulting pseudo-device can read/write across neighbouring partitions, gated only by whatever the raw device driver below happens to refuse.
- **Fix (words only):** In the `:130` sanity check also require `first_lba <= last_lba` and `last_lba < bd->sectorCount`; refuse or clamp when the header's entry size differs from `sizeof(gpt_partition_table_entry)`. Justification: adds validation of on-disk geometry before it becomes the I/O fence.
- **Benefit:** A corrupt/malicious GPT can no longer mint a partition whose bounds defeat the partition driver's own containment check.
- **Effort:** S · **Risk:** Low (no conforming GPT — entries inside the device, 128-byte entries — is affected) · **Confidence:** High, READ (underflow arithmetic and the missing comparisons are direct; that lower drivers might also refuse some out-of-range I/O is INFERRED and does not restore the fence).

### R5-M3-08 / MBR/GPT connect paths accept a zero/short sector read as success and parse the scratch buffer anyway
- **Category:** 6
- **Locations:** `modules/bdm/src/part_driver_gpt.c:48-49` (header read), `:103-104` (entry-sector reads), `modules/bdm/src/part_driver_mbr.c:60-61` (MBR read) — all test only `ret < 0`.
- **Problem:** Every block-device read in this stack returns a **sector count** on success (the convention the glue layers enforce explicitly, e.g. `diskio.c`: `res == count ? RES_OK : RES_ERROR`, and `bd_cache.c`'s refill fix, which was written because a short read once passed stale bytes off as the requested sector). These three sites treat a return of 0 — nothing read — as success. The header cases are mostly saved by their signature checks against uninitialised `AllocSysMemory` scratch. The GPT **entry-sector** case is not: a 0-return re-parses the previous sector's buffer under advanced indices, registering duplicate pseudo-partitions for ranges already mounted (a second mount of the same filesystem volume, with independent write paths to it).
- **Fix (words only):** Change the three tests to `ret != 1` (freeing the scratch buffer on the failure path, as today). Justification: aligns these reads with the return-count contract the rest of the same stack already enforces.
- **Benefit:** A flaky device at mount time fails the partition scan instead of double-mounting a volume from stale bytes.
- **Effort:** S · **Risk:** Low · **Confidence:** Med, INFERRED (the count-return contract is READ from `diskio.c`/`bd_cache.c`; no in-tree device driver was proven to return 0 on failure — the defect is that these sites would not notice if one did).

### R5-M3-09 / MBR sanity check is half tautology, and partition extents are never checked against the device
- **Category:** 6 (with a Category 1 component)
- **Locations:** `modules/bdm/src/part_driver_mbr.c:11-32` (`partitions_sanity_check_mbr`: `valid++` at `:29` executes for every entry unconditionally, so `return (valid == 4) && (active > 0);` at `:32` reduces to `active > 0`); `:94-129` (mount loop uses `sector_count` at `:124` with no extent test; the only range check anywhere is `first_lba >= bd->sectorCount` inside the sanity function at `:22`).
- **Problem:** The function presents as a four-part validity check but `valid` can never be anything but 4 — dead arithmetic that makes the check look stronger than it is. Substantively, an entry whose `first_lba` is in range but whose `sector_count` runs far past the end of the device (or over the following partitions) is accepted verbatim as the pseudo-device's `sectorCount`, and — as in R5-M3-07 — `part_read`/`part_write` treat that count as the containment fence. A crafted/corrupt MBR thus defines partitions the driver will happily write outside the physical disk layout.
- **Fix (words only):** Delete the `valid` counter (return the `active > 0` test the code actually performs) and add the extent test `first_lba + sector_count <= bd->sectorCount` (overflow-safe form) to the sanity check or the mount loop. Justification: the added test is the MBR counterpart of the GPT header-bound check; the deletion is the LESS-NOT-MORE half.
- **Benefit:** The check says what it does, and MBR partitions are fenced to the device that hosts them.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-M3-10 / isofs ignores every `cdEmuRead` failure: failed reads return stale sectors as good data
- **Category:** 6
- **Locations:** `modules/isofs/isofs.c:261` (`cdvdman_locatefile` discards the result of the TOC sector read, then parses `cdvdman_buf`); `:448, :462, :472` (`IsofsRead`'s three phases discard the result, advance `fh->position` and return the full requested count regardless).
- **Problem:** `cdEmuRead` has a real error contract — both implementations return `-EIO` on failure (`:173-182`), and the one caller that checks it (`ProbeISO9660`, `:558`) depends on that. The locate and read paths do not check it at all. A failed TOC read makes `cdvdman_locatefile` walk whatever the shared 2048-byte buffer held from a previous sector or a previous lookup, and can return a directory entry — LBA and size included — for a file that is not there. A failed data read makes `IsofsRead` copy the static bounce buffer's previous contents (phase 1/3) or leave the caller's buffer untouched (phase 2), report complete success, and advance the file position past the gap. This is the exact failure mode `bd_cache.c`'s in-repo fix comment describes eliminating for BDM ("a failed or short refill left the buffer holding the evicted block, which was then returned as a successful read"); the ISO path — the game-data path — still has it.
- **Fix (words only):** Propagate the error: `cdvdman_locatefile` returns NULL when the TOC read fails; `IsofsRead` returns `-EIO` (or the bytes genuinely read before the failing phase) and does not advance the position past unread data. Justification: uses the error contract the read functions already provide; no new machinery.
- **Benefit:** A scratched image / dropped device surfaces as an I/O error at open/read instead of silently substituting another sector's bytes for game data.
- **Effort:** S–M · **Risk:** Low · **Confidence:** High, READ.

### R5-M3-11 / `bd_defrag_read` and `bd_defrag_write` are the same 45-line traversal pasted twice
- **Category:** 2 (duplication)
- **Locations:** `modules/bdm/libbdm/src/bd_defrag.c:8-52` and `:54-101`.
- **Problem:** The two functions are line-for-line identical — fragment search, clipping, buffer/sector advancement — except for the single `bd->read` vs `bd->write` call and variable names. Any future bounds or clipping fix must be made (and can be forgotten) in two places; the file is 101 lines for one algorithm. This is first-party libbdm code, not a vendored copy.
- **Fix (words only):** Collapse to one static traversal taking the operation (function pointer or a read/write flag), with the two public entry points as thin wrappers preserving the existing ABI.
- **Benefit:** ~45 fewer lines; fragment-clipping logic exists exactly once.
- **Effort:** S · **Risk:** Low (mechanical merge; both bodies verified identical in structure at the pin) · **Confidence:** High, READ.

### R5-M3-12 / A register-sampling diagnostic (`pcdiag_cf_probe`) runs on every production start, acting only as a gate
- **Category:** 8 (half-finished/redundant feature) / 3
- **Locations:** `modules/pccf/pcdiag_cf.c:11-72` (whole function); sole production caller `modules/pccf/pccf_hw.c:283`, passing `quiet_trace` (`pccf_hw.c:56-59`) as its trace callback; file header `pcdiag_cf.c:1-9` describes it as a DIAG4 observation pass ("register reads only; sector access was not attempted").
- **Problem:** The probe's stated purpose is diagnostic observation — 3 samples × 2 selector spaces × 8- and 16-bit register reads, with waits of 500 ms + 2×100 ms — and every trace string it emits goes to a no-op callback in both shipped drivers (and to the discarding `pccardTrace` stub in `modules/pccf/main.c:19`). Its only production effects are (a) re-checking `pcdiag_cis_cf_candidate`, which the caller already established in `read_cf_profile`, (b) a COR-index gate, and (c) ~700 ms of settle delay. A 72-line diagnostic harness is being carried in the resident IOP driver — in both the GUI and game builds (`ps2.yaml` lists `pcdiag_cf.c` in `pccf_bd` and `pccf_game`) — to perform one register comparison.
- **Fix (words only):** Replace the call with the two parts that carry production weight — an explicit settle wait and the COR readback check (index 0, no soft-reset) — and move the sampling probe to the diagnostic build/tests where its traces go somewhere.
- **Benefit:** Removes a full diagnostic file from two resident drivers while keeping the COR gate and reset-settle behaviour that startup actually depends on.
- **Effort:** M (the settle wait must be preserved deliberately, not as a side effect) · **Risk:** Med (the 500 ms post-reset wait may be load-bearing for real CF bridges — the fix names it explicitly for that reason; hardware confirmation is the user's, as always) · **Confidence:** High that the traces are discarded and the gate is the only logic consumed (READ); Med on which waits are hardware-load-bearing (INFERRED).

### R5-M3-13 / CIS dump formats hex dumps and text decodes for a trace callback that is a no-op at the only production call site
- **Category:** 4 (size/memory waste) / 3
- **Locations:** `modules/pccf/pcdiag_cis.c:159-179` (per-tuple hex-line construction: hex table, `line[49]` formatting loop), trace-only decode branches in `describe` at `:47-75` (MANFID and VERS_1 text extraction), `tuple_name` string table at `:6-41`; sole production caller passes `quiet_trace`: `modules/pccf/pccf_hw.c:56-64`. No other caller exists in-tree (grep-verified, tests included).
- **Problem:** `pcdiag_cis_dump_info` does real per-byte formatting work — and carries ~36 tuple-name strings plus decode logic — exclusively to feed its `trace` parameter. The only production caller supplies a function that discards its arguments, so on real hardware every CIS parse at every PCCF start formats output nobody can ever read, and the strings occupy resident IOP memory in both pccf builds. The parsing side effects that matter (`function`, `config_base`, `register_mask`, `memory_index_zero`, `malformed`, `complete`, `linked`) are interleaved with, but separable from, the trace-only branches.
- **Fix (words only):** Strip the trace-only formatting (hex-dump loop, VERS_1/MANFID text decode, tuple-name table) from the production parse, keeping `describe`'s info-extracting branches (FUNCID / CONFIG / CFTABLE); keep the verbose dumper, if wanted, in the tests/diagnostic tree where a real trace sink exists.
- **Benefit:** Smaller resident driver and a CIS parse that does only the work its result structure needs.
- **Effort:** M · **Risk:** Low–Med (must preserve `describe`'s `malformed`/info assignments exactly; host-testable via `tests/pccf`) · **Confidence:** High, READ.

### R5-M3-14 / `apaCacheInit` leaks the header block when the second allocation fails
- **Category:** 6 (minor resource handling)
- **Locations:** `modules/hdd/apa/src/cache.c:26-44` (header array allocated at `:32`; on `cacheBuf` allocation failure at `:33-35` the function returns `-ENOMEM` without freeing it).
- **Problem:** A partial-init failure leaves the (largest of the two) allocations — `size × sizeof(apa_header_t)` of IOP system memory — permanently held by a module that then refuses to go resident (`hdd.c` treats cache-init failure as fatal). IOP memory is the scarcest resource in this stack (cf. `bd_cache.c`'s own comment on cache allocations starving the network drivers); a failed load attempt should not consume it forever.
- **Fix (words only):** Free the header block on the `cacheBuf == NULL` path before returning. Justification: completes the function's own cleanup contract; one call.
- **Benefit:** Failed APA init returns all of its memory; retries/other drivers are unaffected.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### R5-M3-15 / Non-IOP `apaGetTime` converts to "JST" with the wrong sign (host/test builds only)
- **Category:** 6 (minor, non-shipped path)
- **Locations:** `modules/hdd/apa/src/misc.c:93-111` (`rawtime += (-9 * 60 * 60);` under the comment `// Convert to JST`, followed by `gmtime_r`).
- **Problem:** `time()` returns a UTC epoch regardless of locale; obtaining JST (UTC+9) wall-clock via `gmtime` requires **adding** 9 hours. The code subtracts 9, producing UTC−9 — 18 hours behind the intended JST timestamp. The shipped IOP branch (`:64-92`, CD-clock based) is unaffected; the wrong branch serves host-side builds/tools of this driver, where partition timestamps would be stamped 18 h off.
- **Fix (words only):** Change the adjustment to `+9 * 60 * 60`.
- **Benefit:** Host-built APA tooling stamps the same time the IOP driver would.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the arithmetic (READ); Low for practical impact (host-only path, no in-tree host consumer of APA timestamps was identified in this section's files).

---

## Already filed — not repeated

- **RIPTOPL-052 + RIPTOPL-128** — the dead partition-edit machinery: `hddAddPartitionHere` (`modules/hdd/apa/src/hdd.c:93-179`) is the `hdd.c` half RIPTOPL-128 already proposes deleting, including its unsafe internals (unchecked `apaCacheGetHeader` result). Read in full here; not re-filed.
- **RIPTOPL-034** — commented-out/dead code markers, which already cites the `#if 0` block in `cache.c:55-66` (`apaCacheDeinit`'s disabled per-entry free). Not re-filed; the live tail of that function was checked separately (see Checked and clean).
- **Round 2 cleared item** — `modules/isofs/isofs.c`'s bounded `strcpy` (now at `:243`): re-verified guarded by the preceding length check; the new isofs substance is R5-M3-10 only.
- **RIPTOPL-012** — FatFs unicode tables (`ffunicode.c`): vendored and excluded from this section by the brief; `diskio.c`/`fs_driver.c` were audited as first-party glue only.

## Checked and clean (dissolved candidates)

- **`apaGetFormat` "inverted" status handling** (`hdd.c:331`, `hdd_blkio.c:126,263-266`): looks backwards, dissolves on reading the callee — `apa.c:461-485` shows it returns a boolean (`return rv == 0;`), so `!= 0` means success and the status decrement is correct at all three call sites.
- **`apaCacheAlloc` / `apaCacheGetHeader` empty-pool paths** (`cache.c:129-211`): both correctly return NULL / `-ENOMEM` when the free list is empty; the defect is in specific callers (R5-M3-02), not the allocator.
- **`apaCacheDeinit`** (`cache.c:47-69`): freeing `cacheBuf[1].header` frees the whole header array because the init loop assigns the base pointer to entry 1 first; the `cacheSize > 1` guard is unreachable in practice (loader cache size starts at 3 and the `-n` option only raises it).
- **`apaGetFreeSectors` nested halving loops** (`free.c:48-84`): terminate — each outer pass either adds a ≥ min-size block (bounded by `totalLBA`) or falls through to the `minsize >= partMax` exit; the odd reset of `partMax` in the `for` header is faithful upstream behaviour, noted in the code's own comment.
- **`hdd.c` argument parsing** (`:250-273`): `-o 0`/negative values produce a zero/negative allocation size that fails cleanly into `hddInitError`; arguments are loader-supplied, not data-supplied.
- **`hdd_blkio_vhdd_mount` seek failure** (`hdd_blkio.c:232-282`): a failed/short `lseek64` yields a bogus sector count, but the APA format probe then fails, `status != 0`, and the cleanup path closes the fd — no mount results.
- **`fs_driver.c` `get_frag_list` / `vmc_allocate_contiguous`** (`:741-870`): the fork's hardening holds — chain length bounded by file size (cycle-safe), cluster ranges, fs type, extent count and sector-size all validated; no new substance.
- **`USBMASS_DEVCTL_STOP_UNIT` / `STOP_ALL` apparently swapped bodies** (`fs_driver.c:1019-1030`): dissolves against usage — the sole in-tree EE caller (`src/sources/bdmsupport.cpp:2180`, supporting grep) issues `STOP_ALL` to stop its own source device before launch, which is what the current body does; `STOP_UNIT` has no in-tree caller. Renaming bodies would break the live caller.
- **`diskio.c` `GET_SECTOR_COUNT` 32-bit truncation** (`:132-134`): the value is consumed only by FatFs `create_partition`/`f_mkfs` paths (grep-verified in `ff.c`), and this driver's format op is a hard `EIO` stub (`fs_driver.c` ops table) — the truncation is not reachable in the shipped configuration.
- **`GetGPTPartitionNameAscii` unterminated output** (`part_driver_gpt.c:17-22`): the sole caller's buffer is `char partName[37] = {0}` and the loop writes at most 36 chars, leaving the terminator intact.
- **`pcdiag_cis.c` `data[254]` payload buffer**: a length byte of `0xff` is handled as a chain terminator before any payload copy (`:149-152`), so the maximum copied payload is 254 bytes — exactly the buffer size.
- **`zso.c` decompression inputs**: index offsets are monotonicity- and size-checked against the caller's buffer before any decompress (`:81-93`), the slice window refuses short index reads, and `ziso_read_sector` returns 0 when the index cache was never allocated, which its caller converts to `-EIO`. Residual trust in `LZ4_decompress_fast` on a well-formed-offset stream is the format's design, not a new finding.
- **`isofs.c` directory-record walk**: the round-5 hardening is present and correct — record length ≥ 33 + name length and record-within-sector are both enforced before the name compare (`:285-287`), and nameless records now advance `tocPos` (`:307-310`).
- **`IsofsLseek` SEEK_END sign**: position is clamped to the file size on every path, so no state corruption is reachable; which sign convention EE callers use could not be established from this section's files, so no behavioural claim is filed.
- **`bd_cache.c` slot accounting**: reserve-before-alloc / release-after-free is balanced on every path, including both allocation-failure branches, and the `sectorSize != 512` refusal precedes reservation. Weight growth on repeated hits can wrap an `int` after ~10⁶ hits on one block, but the hit path clamps negatives to 0 — a cache-quality quirk, not a correctness defect.
- **`part_driver.c` `part_read`/`part_write`**: the bounds arithmetic itself (`sector > count || count > sectorCount - sector`) is overflow-safe; its weakness is entirely in the `sectorCount` values the MBR/GPT parsers hand it (R5-M3-07, R5-M3-09).
- **`bdm_disconnect_fs` never unmounts filesystems** (`bdm.c:119-139`): no in-tree caller exists (grep-verified across the repo; the function is export-only), so the asymmetry with `bdm_disconnect_bd` is not live behaviour.
- **`xhdd.c` `ATA_DEVCTL_READ_PARTITION_SECTOR` always reads LBA 0**: by design — the code's own comment and the EE probe it serves make it an MBR/readiness read, and the `buflen % 512` precondition is checked. The double `sceAtaInit` is a documented single bounded retry.
- **`pccf_hw_stop` restore order** (`pccf_hw.c:204-218`): `save16[6]` (0x147e) is restored last, after the loop over the other six — deliberate sequencing (power/ctrl register restored after the values it gates), not an off-by-one.
- **`pccf_ata.c` per-sector fallback loop** (`:233-238`): unreachable with the in-tree host (both fast issue paths are installed unconditionally after attach), but it is the ops-table fallback the host-test harness exercises — a library seam, not dead code.
- **`table_fence.c`**: compile-time layout asserts cover every offset the fence reads; the write gate's logic matches its header's stated contract. No finding.

---

*End of section R5-M3. 15 new candidate findings (R5-M3-01…15), 4 already-filed items noted, 21 checked-and-clean entries. No forced revision to any prior finding: round 4's APA NULL-allocation finding (RIPTOPL-076) was confirmed to be a different function/file (`apa.c`), and the `apaGetFormat` boolean-return convention was verified rather than contradicted.*
