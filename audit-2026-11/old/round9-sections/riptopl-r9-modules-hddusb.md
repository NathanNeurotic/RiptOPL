# Round 9 — RiptOPL `modules/hdd/` + `modules/usb/` + `modules/isofs/` full line-read

- **Repo / pin:** RiptOPL clone at `~/workspace/audit-clones-2026-11-13/RiptOPL`, HEAD verified `2e347d5ac885cd0ff73e9ad4143781946396f492`, clone left git-clean, no repo edits.
- **Scope read:** every file under `modules/hdd/`, `modules/usb/`, `modules/isofs/` — 72 files, 14,600 lines (per-file ledger in §C). The task's 26/22/6-file, ~13,714-line figures excluded LICENSE/ORIGIN/Makefile/import files; all of those were read too, so the ledger exceeds the task census.
- **Byte-compare:** md5 census over all 72 files found **no byte-identical file pairs**. Function-level compare of the atad siblings: `ata_ultra_dma_mode` and `ata_multiword_dma_mode` bodies in `modules/hdd/xhdd/xatad.c` are byte-identical to `modules/hdd/atad/src/ps2atad.c`; `ata_device_set_transfer_mode` differs only by `#ifdef`/PIO case; `ata_device_identify` differs only in whitespace (see Dissolved D-11). No USBA files exist in these trees (`USBA_crc32`, filed RIPTOPL-232, lives outside this scope — cross-reference only).
- **Already filed, not re-filed:** RIPTOPL-042, -052, -076, -077, -086, -087, -099, -115, -128, -147, -148, -149, -155, -170, -177, -192, -193, -194, -205, -212, -213, -222, -224, -232. Where a new candidate is adjacent to one of these, the distinct mechanism is stated.
- Nothing here is PS2-build- or hardware-verified; all claims are code-read at the pin. INFERRED marks anything beyond the code in front of me.

---

## A. Candidates (HDDUSB-01…, sorted by benefit)

### HDDUSB-01 — ZSO decompression uses the explicitly-unsafe LZ4 "fast" decoder on a user-supplied game image, with prefix references enabled and the result unchecked
- **Category:** 6 (bug/security)
- **Locations:** `modules/isofs/zso.c:111`; `modules/isofs/lz4.c:500-506`; `modules/isofs/lz4.c:365-498` (generic decoder); `modules/isofs/lz4.h:62,117-126`
- **Problem:** `ziso_read_sector` decompresses each compressed block of a ZSO image (a file the user downloads/copies, i.e. attacker-suppliable) with `LZ4_decompress_fast(tmp, addr, 2048)` and ignores the return value. In this vendored copy, `LZ4_decompress_fast` calls the generic decoder with `endOnOutputSize` and **`withPrefix`** (`lz4.c:505`). Consequences, all verified in the decoder source: (1) the offset sanity check `ref < dest` is skipped for `withPrefix` (`lz4.c:442`), so a crafted match offset can make the decoder copy from memory *before* the 2048-byte output buffer into the sector buffer; (2) with `endOnInput == 0` the decoder does not know the input length — the literal-run loop (`lz4.c:408-413`) and match-length loop (`lz4.c:447-454`) read past the end of the compressed block while bytes stay `0xFF`, and wildcopy reads past the input; the vendored header itself warns the fast variant "may read beyond input buffer in case of malicious data packet" (`lz4.h:117-126`). A crafted ZSO can therefore read/copy out-of-bounds in IOP memory (crash at best, memory disclosure into game data at worst).
- **Fix:** Add the 3-line `LZ4_decompress_safe` wrapper next to `LZ4_decompress_fast` (`LZ4_decompress_generic(source, dest, inputSize, maxOutputSize, endOnInputSize, noPrefix, full, 0)` — the generic decoder already implements the checked mode) and call it from `zso.c:111` as `LZ4_decompress_safe(tmp, addr, r, 2048)`, treating a return `!= 2048` as `-EIO`.
- **Justification if additive:** +~5 lines total, no new code paths — it instantiates an existing, already-linked decoder mode and turns silent corruption into a clean read error.
- **Benefit:** Removes the only crafted-file memory-safety hole found in the isofs tree; every ZSO in the wild is untrusted input.
- **Effort:** S · **Risk:** Low (safe decoder is the same generic core; behaviour on valid ZSOs is identical) · **Confidence:** High (mechanism read directly in `lz4.c`).

### HDDUSB-02 — APA journal write side has no bound: more than 126 dirty cache entries overflow the static journal buffer and write journal headers past the journal area
- **Category:** 6 (bug/security)
- **Locations:** `modules/hdd/apa/src/journal.c:47-56`; `modules/hdd/apa/src/libapa.h:184-189` (`sectors[126]`), `libapa.h:33-34` (journal headers at sectors 10–262); `modules/hdd/apa/src/cache.c:111-126`; `modules/hdd/apa/src/hdd.c:263-271`
- **Problem:** `apaJournalWrite` stores into `journalBuf.sectors[journalBuf.num]` and writes the header to disc sector `(journalBuf.num << 1) + APA_SECTOR_APAL_HEADERS` with no check of `num` against the 126-entry array. `apaCacheFlushAllDirty` calls it once per dirty cache entry, and the cache size is raised without any upper bound by the `-n` module argument (`hdd.c:263-271`: `if (cacheSize < i) cacheSize = i;`). With cacheSize > 126 a flush writes past the static `apa_journal_t` in IOP memory and writes APA headers to disc sectors ≥ 262, i.e. past the journal area into the region `APA_SECTOR_MIN_OSDSTART` protects (`libapa.h:34`). Distinct from filed RIPTOPL-147 (journal *restore* trusting the on-disk count) — this is the write side, driven by a local count.
- **Fix:** In `apaJournalWrite`, `if (journalBuf.num >= 126) return -EIO;` before the transfer; optionally also clamp `-n` to ≤ 126 in `fioGetInput`. One guard suffices (LESS).
- **Benefit:** Bounds both a static-buffer overflow and on-disc writes outside the journal area on the data-integrity-critical flush path.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the missing bound (read in code); **INFERRED** for reachability — it depends on the loader passing `-n` > 126, which no in-tree caller was found to do; with the shipped default cache size this is latent, not live.

### HDDUSB-03 — USB mass-storage connect walks the endpoint descriptor chain with a device-supplied count and stride, unchecked
- **Category:** 6 (bug/security)
- **Locations:** `modules/usb/usbmass_bd/src/usb_mass.c:665-681`
- **Problem:** In `usb_mass_connect`, `epCount = interface->bNumEndpoints` (a byte from the attached device's descriptor) and the walk advances `endpoint = (UsbEndpointDescriptor *)((char *)endpoint + endpoint->bLength)` with no check that `bLength` is non-zero / at least descriptor-sized, and no bound against the end of the static descriptor buffer, calling `usb_bulk_probeEndpoint` (which opens pipes) on whatever it lands on. A broken or malicious device can send the walk out of the descriptor buffer or spin it in place, probing garbage as endpoints. Distinct mechanism from filed RIPTOPL-077 (same function: connect re-reading descriptors without probe's NULL checks).
- **Fix:** Bound the walk by the configuration descriptor's `wTotalLength`: stop when the next endpoint pointer would leave `config … config + wTotalLength`, and require `endpoint->bLength >= sizeof(UsbEndpointDescriptor)` before advancing.
- **Justification if additive:** ~3 lines of guard inside the existing loop; no new state.
- **Benefit:** Closes an untrusted-device descriptor-walk overrun in the USB stack's only mass-storage driver.
- **Effort:** S · **Risk:** Low · **Confidence:** High (walk read in code; descriptor source is the device by construction of the USB stack).

### HDDUSB-04 — `sceAtaSecurityEraseUnit` passes NULL as the IDENTIFY buffer; the helper writes 512 bytes to address 0 and dereferences it
- **Category:** 6 (bug)
- **Locations:** `modules/hdd/atad/src/ps2atad.c:1218` (call in `sceAtaSecurityEraseUnit`); `modules/hdd/atad/src/ps2atad.c:1146-1150` (`ata_get_security_status`); `modules/hdd/atad/src/exports.tab:17` (export 12)
- **Problem:** `ata_get_security_status(device, devinfo, param)` runs `ata_device_identify(device, param)` — a PIO data-in transfer of 256 u16 words into `param` — and on success reads `param[ATA_ID_SECURITY_STATUS]`. `sceAtaSecurityEraseUnit` calls it with `param == NULL` on its exit path, so a successful erase-unit sequence ends by writing the IDENTIFY block to IOP address 0 and reading the security word back from low memory. Grep verified no in-tree caller of `sceAtaSecurityEraseUnit`/`sceAtaSecuritySetPassword` outside `ps2atad.c`, so this is a latent exported-API bug, not a live in-game path.
- **Fix:** Pass the module's existing `ata_param` buffer instead of `NULL` (one word changed).
- **Benefit:** Removes a guaranteed low-memory write on an exported function's normal exit path.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

### HDDUSB-05 — USB READ CAPACITY result stored as sector count without the +1: the last sector of every USB drive is unreachable
- **Category:** 6 (bug)
- **Locations:** `modules/usb/usbmass_bd/src/scsi.c:243` (READ CAPACITY(10)); `modules/usb/usbmass_bd/src/scsi.c:260` (READ CAPACITY(16))
- **Problem:** Both capacity paths store `bd->sectorCount = getBI32(&rc…last_lba)`. Per the SCSI spec the field is the address of the *last* logical block, so the block count is `last_lba + 1`. As written, BDM sees every USB drive one sector short; a filesystem/partition ending exactly at the final LBA loses its last sector. Contrast the ATA BDM path, which takes the true sector count from IDENTIFY (`modules/hdd/atad/src/ps2atad.c:1408`). Side effect in the same lines: a zero-capacity device reports `sectorCount == 0xFFFFFFFF` via the (10) path and is needlessly escalated to READ CAPACITY(16) by the sentinel check at `scsi.c:255`.
- **Fix:** Add `+ 1` at both assignments (and the sentinel comparison becomes `== 0x100000000` in u64 — or test `last_lba == 0xFFFFFFFF` before assignment).
- **Benefit:** Correct capacity for every USB mass device; removes a silent one-sector truncation at the end of the drive.
- **Effort:** S · **Risk:** Low (BDM consumers take `sectorCount` as a count everywhere else) · **Confidence:** High (spec semantics; code read).

### HDDUSB-06 — Device-supplied USB sector size feeds an unchecked `alloca` and unchecked length multiplications
- **Category:** 6 (bug/security)
- **Locations:** `modules/usb/usbmass_bd/src/scsi.c:321` (`__builtin_alloca(sectorSize + 4)` in `scsi_write`); `modules/usb/usbmass_bd/src/scsi.c:244,261` (sectorSize taken from READ CAPACITY `block_length`, checked only `!= 0`); `modules/usb/usbmass_bd/src/scsi.c:156,174` (`data_len = sc * sectorSize` for READ/WRITE queueing)
- **Problem:** `bd->sectorSize` is whatever the attached drive reports. A drive reporting a huge block length (e.g. 0x10000000) makes any misaligned-buffer write `alloca` that much stack in IOP (2 MB total RAM) — instant stack destruction — and makes the `sc * sectorSize` transfer-length products wrap. Real drives report 512/4096, so this needs a broken/malicious device, but the value is fully device-controlled and never range-checked.
- **Fix:** At capacity parse (one place, both paths), reject a `block_length` that is not a power of two in [512, 4096] with the existing "drive left out" error return — the ORIGIN-documented pattern already used for sector size 0 (`scsi.c:246-249`).
- **Justification if additive:** one comparison added to an existing validation block; also removes the need to reason about the alloca and the products downstream.
- **Benefit:** One check kills the alloca overflow and both multiplication wraps.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the data flow (read end-to-end); exploitability is device-dependent by nature.

### HDDUSB-07 — APA DES password engine (~460 lines + tables) is compiled, linked and executed on every open, but its result can never matter in the shipped build
- **Category:** 1 (dead code) / 4 (size)
- **Locations:** `modules/hdd/apa/Makefile:8` (`-DAPA_OSD_VER`); `modules/hdd/apa/src/apa-opt.h:31-38`; `modules/hdd/apa/src/password.c:37-498` vs the stub at `password.c:25-35`; call sites `modules/hdd/apa/src/hdd_fio.c:182,191`
- **Problem:** The shipped Makefile defines `APA_OSD_VER`, under which `APA_ENABLE_PASSWORDS` is *not* defined (`apa-opt.h:31-38`), so `apaPassCmp` is the always-return-0 stub. Yet `password.c`'s full DES implementation (`DESEncryptPassword` and tables, lines 37–498 of 498) is compiled and linked into the IOP module, and `apaEncryptPassword` is still executed on every `apaOpen` via `fioGetInput` (`hdd_fio.c:182,191`) to fill password buffers that the stub comparator then ignores. Pure cost: ~460 lines of IOP code + table bytes, and DES work per open, for a feature the build has switched off.
- **Fix:** Mirror the existing pattern: put the body of `apaEncryptPassword` under `#ifdef APA_ENABLE_PASSWORDS` with a no-op stub otherwise, exactly as `apaPassCmp` already does in the same file. The non-OSD build keeps the full engine.
- **Benefit:** Shrinks the APA IRX and removes pointless per-open crypto work; zero behaviour change in the OSD build.
- **Effort:** S · **Risk:** Low · **Confidence:** High (defines and call sites read).

### HDDUSB-08 — `hdd_blkio.o` is an empty translation unit in the shipped build but is still compiled and linked
- **Category:** 7 (build clutter) / 1 (dead code)
- **Locations:** `modules/hdd/apa/Makefile:11` (`IOP_OBJS` includes `hdd_blkio.o`); `modules/hdd/apa/src/hdd_blkio.c:17,178,295,298,308` (all content under `#ifdef APA_USE_BDM`, `#ifdef APA_USE_IOMANX`, `#ifndef APA_USE_ATAD`); `modules/hdd/apa/Makefile:8` (defines `APA_USE_ATAD`, defines neither BDM nor IOMANX)
- **Problem:** Every statement in `hdd_blkio.c` (415 lines) sits inside a preprocessor region that is false for the shipped define set, so the object contributes nothing but build time and Makefile noise. (Its one live-looking wart — the `vhdd` bound `slot > BLKIO_MAX_VOLUMES` at `hdd_blkio.c:211`, which should be `>=` — is inside the IOMANX region and therefore not compiled; noted here, not filed separately.)
- **Fix:** Drop `hdd_blkio.o` from `IOP_OBJS` in this Makefile; the file stays in tree for the BDM/IOMANX build variants that use it.
- **Benefit:** One less empty object per build; makes the Makefile state what the module actually is.
- **Effort:** S · **Risk:** Low · **Confidence:** High (define set and guards read directly).

### HDDUSB-09 — `apaOpen` compares the password of a cache entry after freeing the entry
- **Category:** 6 (bug, latent)
- **Locations:** `modules/hdd/apa/src/hdd_fio.c:510-512`
- **Problem:** On the main open path, `apaCacheFree(clink)` is called and `apaPassCmp(clink->header->fpwd, …)` then reads through the freed entry. Today this is benign in practice — `apaCacheFree` only relinks the entry, the cache semaphore is held, and no allocation intervenes before the read — but it is a use-after-free by contract: any future change that allocates inside the window silently corrupts the password check.
- **Fix:** Move the `apaPassCmp` call above the `apaCacheFree` (two lines reordered; under the OSD build the comparator is a stub, so this is free).
- **Benefit:** Removes a latent UAF on the most-executed APA path at zero cost.
- **Effort:** S · **Risk:** Low · **Confidence:** High that the ordering is as described; Medium that it can ever bite (hence last of the Cat-6 items).

### HDDUSB-10 — usbd public header declares two exports the module does not implement
- **Category:** 8 (half-finished feature)
- **Locations:** `modules/usb/usbd-ra/include/usbd.h:294-296` (`sceUsbdGetReportDescriptor`, `sceUsbdMultiIsochronousTransfer`, self-commented "these aren't implemented"); `modules/usb/usbd-ra/src/exports.tab` (exports end at 16)
- **Problem:** The header advertises import stubs 17/18 that can never resolve against this module — any driver written against the header links a stub that fails at load. This is the SDK's header carried verbatim, but for this trimmed `usbd-ra` build the declarations are pure trap.
- **Fix:** Delete the two declarations from this copy's header (LESS); if a future driver needs them, implement and export them first.
- **Benefit:** The header stops promising what the module cannot deliver.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

### HDDUSB-11 — Multi-LUN support is plumbed but never used: max-LUN is queried, printed, and thrown away; the CBW LUN field is hardcoded 0
- **Category:** 8 (half-finished feature) / 1 (dead code)
- **Locations:** `modules/usb/usbmass_bd/src/scsi.c:196-197` (result used only by a debug print); `modules/usb/usbmass_bd/src/usb_mass.c:329-357` (`usb_bulk_get_max_lun`); `modules/usb/usbmass_bd/src/usb_mass.c:482` (`ucmd.cbw.lun = 0`)
- **Problem:** The driver goes to the bus to ask each device its maximum LUN during warmup, then discards the answer; every command block wrapper is built with LUN 0, and the BDM device is registered per physical device. Card readers / multi-LUN devices therefore expose only LUN 0, and the query + callback plumbing is dead weight carried through every startup.
- **Fix (LESS direction):** Delete the `get_max_lun` call, its debug print, and the callback assignment (`usb_mass.c:847`) — or, if multi-LUN is actually wanted, that is a feature project (per-LUN BDM devices), not a fix; do not leave the half-state.
- **Benefit:** Removes a bus transaction and dead plumbing from every device startup; the code stops implying support that does not exist.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

### HDDUSB-12 — `xhdd.h` declares two functions that exist nowhere in the HDD modules
- **Category:** 1 (dead code)
- **Locations:** `modules/hdd/xhdd/xhdd.h:2,4` (`ata_device_set_write_cache`, `hdproata_device_set_write_cache`)
- **Problem:** Grep over `modules/hdd/` (and the wider modules tree in scope) finds no definition and no caller for either declaration. A definition of `ata_device_set_write_cache` exists only in `modules/iopcore/cdvdman/hdpro_atad.c` — a different module outside this scope — so within xhdd these are dangling declarations that suggest a write-cache control the module does not have.
- **Fix:** Delete the two declarations.
- **Benefit:** Header matches the module's real surface.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

### HDDUSB-13 — 407-line packed IDENTIFY-device structure maintained to read exactly one field
- **Category:** 3 (overengineering)
- **Locations:** `modules/hdd/xhdd/ata_identify.h` (407 lines); sole consumer `modules/hdd/xhdd/xhdd.c:17` (one 512-byte static instance), `modules/hdd/xhdd/xhdd.c:79` (only `UltraDMASupport` is ever read)
- **Problem:** xhdd carries a full bitfield-by-bitfield model of the ATA IDENTIFY block — hundreds of named fields across 407 lines — and in the whole module exactly one field is consumed. The sibling atad driver reads the same data as raw u16 words (`ata_param[ATA_ID_…]`) with a handful of index constants. Every future edit risks touching a packed-struct definition that exists for one word at offset 88.
- **Fix:** Delete `ata_identify.h`; read word 88 of the identify buffer directly in `xhdd.c`, as atad does. Net ≈ −400 lines.
- **Benefit:** Largest single-file deletion available in these trees with zero behaviour change.
- **Effort:** S–M · **Risk:** Low (one field, one call site) · **Confidence:** High (field-usage grep verified: only `UltraDMASupport` is referenced).

### HDDUSB-14 — `lz4.h` declares the full LZ4 API (~20 functions: compression, streaming, obsolete); `lz4.c` implements exactly one
- **Category:** 7 (build clutter) / 8 (half-finished)
- **Locations:** `modules/isofs/lz4.h` (248 lines — declarations throughout, streaming API at `:158-228`, obsolete API at `:230-241`); `modules/isofs/lz4.c` (ends at line 506; contains only `LZ4_decompress_generic` and `LZ4_decompress_fast`)
- **Problem:** This is a deliberately trimmed vendored copy, but the header was not trimmed with it. Any present or future consumer that includes `lz4.h` and calls e.g. `LZ4_decompress_safe` (declared at `lz4.h:62`) compiles fine and fails only at link time. HDDUSB-01's fix adds the safe wrapper; the rest of the declared API has no implementation and no consumer in tree.
- **Fix:** Trim `lz4.h` to the functions that exist (after HDDUSB-01: `LZ4_decompress_fast` + `LZ4_decompress_safe`), deleting the compression/streaming/obsolete declaration blocks.
- **Benefit:** Header becomes a truthful contract for the vendored copy; link errors become compile-time absences.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

---

## B. Checked-clean / dissolved

- **D-01 — APA read-after-`apaCacheFree` patterns in `apa.c`** (`apaInsertPartition` `apa.c:163-165`, `apaDelete` `apa.c:383-384`, `apaGetNextHeader` `apa.c:512-517`): dissolved — cache free only relinks the entry, no allocation intervenes before the reads, and values are consumed before any call that could reuse the entry. (HDDUSB-09 is the one instance where the read crosses a call boundary worth fixing.)
- **D-02 — `apaCacheGetHeader` free-buffer-empty path** (`cache.c:148-164`): correctly falls through to `-ENOMEM`; checked, clean.
- **D-03 — Dead APA partition-edit machinery in `hdd_fio.c`**: `hddFormat` body after the `-EACCES` return (`hdd_fio.c:347`, ~106 lines), `ioctl2AddSub`/`DeleteLastSub`, `devctlSwapTemp`/`SetOsdMBR`, and the `apaRename`/`apaRemove` bodies (incl. the inconsistent `SignalSema` at `hdd_fio.c:573,581,595` and unchecked on-disc `nsub`) are all unreachable behind `hddReName`/`hddRemove`'s `-EACCES` refusals (`hdd_fio.c:628-629, 849-851`) — already covered by filed RIPTOPL-052/-128, not re-filed.
- **D-04 — APA ioctls**: `HIOCGETSIZE`'s unchecked `parts[]` index (`hdd_fio.c:996`) is filed RIPTOPL-042; `HIOCGETPARTSTART` (`hdd_fio.c:1016`) is not compiled (`APA_SUPPORT_IOCTL_GETPARTSTART` is POSIX-only in `apa-opt.h`); `HDIOC_READSECTOR` (`hdd_fio.c:1176-1179`) is a raw-LBA read by design for the EE-side caller, same trust domain as -042. No new item.
- **D-05 — APA table fence** (`table_fence.h`/`table_fence.c`): verified sound — writes at LBA ≥ 8 cannot touch LBA 0–7; spanning/partial writes into 0–7 are refused unless a whole valid `__mbr` header sits at LBA 0; compile-time offset asserts present.
- **D-06 — `apaGetFormat`'s inverted-looking return** (`apa.c:484`, returns `rv == 0`): used consistently by both callers (`hdd.c:331`, `hdd_blkio.c:126`). Clean.
- **D-07 — usbd hub descriptor fetch** (`hub.c:416-430`, `fetchConfigDescriptors` `wTotalLength` advance): dissolved after tracing the counter/fetchDesc alternation — the advance is by a previously bounds-validated `wTotalLength` and the dereferences stay in-bounds.
- **D-08 — usbd filed items confirmed in place, not re-filed**: `usbd.c:139` wrong TD LUT slot on isochronous halt cleanup = RIPTOPL-192 (its `processDoneQueue_GenTd` twin at `usbd.c:224` is correct); `hcd.c:380-381` unchecked `AllocSysMemory` and `hcd.c:361` ignored `initHardware` result = RIPTOPL-193; `usb_mass.c:137-139` callback remaining-count underflow = RIPTOPL-099; `usb_mass.c` max-LUN uninitialised byte = RIPTOPL-115; connect NULL-checks = RIPTOPL-077.
- **D-09 — `driver.c` callback-thread `SetGP(req->gpSeg)` after `freeIoRequest`** (`driver.c:245-250`): compiled out in this build — `modules/usb/usbd-ra/Makefile:15` sets `-DUSE_GP_REGISTER=0` and `-DMINI_DRIVER` (MINI_DRIVER also selects the small `usbConfig`: 16 devices / 32 endpoints / 64 TDs, 2 KiB thread stacks). Not live.
- **D-10 — usbmass_bd fork state**: `ORIGIN.md` documents 11 deliberate two-drive/start-up fixes over the ps2sdk copy, all present in the code as read (command semaphore, bounded TEST UNIT READY, zero-sector-size rejection, READ/WRITE(16) straddle, ZLP handling, failed-startup slot free, CSW retry/fail, semaphore-create cleanup). The candidates above are what remains beyond those documented fixes.
- **D-11 — atad duplication (byte-compare result)**: `xatad.c`'s `ata_ultra_dma_mode`/`ata_multiword_dma_mode` bodies are byte-identical to `ps2atad.c`'s, `ata_device_set_transfer_mode` differs only by `#ifdef`/PIO case, `ata_device_identify` only by whitespace. Dissolved as a finding: atad and xhdd/xatad are separate IRX modules that cannot link each other, and atad's copies are `static` — the duplication is structural to the IRX model, and de-duplicating would mean a new shared module (MORE, not LESS).
- **D-12 — isofs seeks**: `IsofsLseek` SEEK_END/SEEK_CUR negative results are clamped by the unsigned `position > filesize` check (`isofs.c:520-521`); the >2 GiB read clamp only triggers near EOF where the remainder fits in `int`; `longLseek` (`isofs.c:126-142`) chunks seeks so each `lseek` stays within INT_MAX. Clean.
- **D-13 — ZSO index-cache arithmetic** (`zso.c:38-44`): the 2048 + 257×4 + 64 allocation exactly covers the +64 alignment shift and the 1028-byte index at `tmp+2048` — sound by construction. (`ziso_init` returning 0 on allocation failure at `zso.c:38-46` is a wart, but `ziso_read_sector` handles the NULL cache at `zso.c:132-134` and probing then fails cleanly with `-EIO`; observation only.)
- **D-14 — usbd `mem.c` edges**: `fetchPortElemByNumber` port=0 returning the first child is safe because all callers are 1-based; `freeIoRequest` computing `req - memPool.ioReqBufPtr` before its NULL check (`mem.c:240`) is UB-on-NULL by the letter but the value is guarded before use; `freeDevice` lacks the double-free check its siblings have — INFERRED latent only, no live double-free path found. No candidate.
- **D-15 — atad data path**: `ata_bd_write`'s APA-reserved-region check (`ps2atad.c:1609-1618`) is conservative and correct; `ata_device_sector_io64`'s ICRC retry is bounded at 3 (the unbounded retry is HDPro's, filed RIPTOPL-086). `hdd_blkio.c:211` `slot > BLKIO_MAX_VOLUMES` (should be `>=`) is in uncompiled code — see HDDUSB-08.
- **D-16 — LICENSE files** (`modules/hdd/apa/LICENSE`, `modules/usb/usbmass_bd/LICENSE`): standard AFL-2.0 boilerplate, read and cross-compared (md5s differ only in formatting, 165 vs 167 lines); no code content.

---

## C. Per-file ledger (all files read in full unless noted)

### modules/hdd/ — 36 files, 7,050 lines
| File | Lines |
|---|---|
| apa/LICENSE | 165 |
| apa/Makefile | 25 |
| apa/ORIGIN.txt | 47 |
| apa/src/apa-opt.h | 51 |
| apa/src/apa.c | 525 |
| apa/src/cache.c | 211 |
| apa/src/free.c | 84 |
| apa/src/hdd.c | 384 |
| apa/src/hdd.h | 31 |
| apa/src/hdd_blkio.c | 415 |
| apa/src/hdd_blkio.h | 85 |
| apa/src/hdd_fio.c | 1247 |
| apa/src/hdd_fio.h | 38 |
| apa/src/imports.lst | 84 |
| apa/src/irx_imports.h | 37 |
| apa/src/journal.c | 86 |
| apa/src/libapa.h | 219 |
| apa/src/misc.c | 141 |
| apa/src/password.c | 498 |
| apa/src/table_fence.c | 51 |
| apa/src/table_fence.h | 94 |
| atad/Makefile | 26 |
| atad/ORIGIN.txt | 45 |
| atad/src/exports.tab | 27 |
| atad/src/imports.lst | 56 |
| atad/src/irx_imports.h | 34 |
| atad/src/ps2atad.c | 1646 |
| common/opl-hdd-ioctl.h | 21 |
| xhdd/Makefile | 8 |
| xhdd/ata_identify.h | 407 |
| xhdd/hdpro.c | 11 |
| xhdd/imports.lst | 14 |
| xhdd/irx_imports.h | 10 |
| xhdd/xatad.c | 86 |
| xhdd/xhdd.c | 135 |
| xhdd/xhdd.h | 6 |

### modules/isofs/ — 8 files, 1,688 lines
| File | Lines |
|---|---|
| Makefile | 6 |
| imports.lst | 30 |
| irx_imports.h | 13 |
| isofs.c | 669 |
| lz4.c | 506 |
| lz4.h | 248 |
| zso.c | 158 |
| zso.h | 58 |

### modules/usb/ — 28 files, 5,862 lines
| File | Lines |
|---|---|
| usbd-ra/Makefile | 21 |
| usbd-ra/include/usbd.h | 361 |
| usbd-ra/include/usbd_macro.h | 154 |
| usbd-ra/src/driver.c | 283 |
| usbd-ra/src/driver.h | 31 |
| usbd-ra/src/exports.tab | 23 |
| usbd-ra/src/hcd.c | 544 |
| usbd-ra/src/hcd.h | 28 |
| usbd-ra/src/hub.c | 737 |
| usbd-ra/src/hub.h | 19 |
| usbd-ra/src/imports.lst | 48 |
| usbd-ra/src/interface.c | 452 |
| usbd-ra/src/irx_imports.h | 30 |
| usbd-ra/src/mem.c | 259 |
| usbd-ra/src/mem.h | 45 |
| usbd-ra/src/usbd.c | 410 |
| usbd-ra/src/usbdpriv.h | 328 |
| usbd-ra/src/usbio.c | 319 |
| usbd-ra/src/usbio.h | 28 |
| usbmass_bd/LICENSE | 167 |
| usbmass_bd/ORIGIN.md | 54 |
| usbmass_bd/src/imports.lst | 40 |
| usbmass_bd/src/include/module_debug.h | 36 |
| usbmass_bd/src/include/scsi.h | 37 |
| usbmass_bd/src/irx_imports.h | 30 |
| usbmass_bd/src/main.c | 37 |
| usbmass_bd/src/scsi.c | 440 |
| usbmass_bd/src/usb_mass.c | 901 |

**Total: 72 files, 14,600 lines read. Unread: none.**
