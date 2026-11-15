# RiptOPL Round 5 — Section R5-M5: ilink ↔ bdmassault duplicate-tree remainder

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` · Read-only · `git status --porcelain` empty at finish.
Scope note: the assignment says "11 files" but enumerates **10**; those 10 are the complete pair set and total exactly the stated 3,166 ilink-side lines. RIPTOPL-021 (duplicated-driver cost) and RIPTOPL-047 (the duplication itself) are **not** re-filed — this section is only the line-level remainder inside the pairs.

## Coverage ledger

ilink side read **1..EOF, all 10 files, 3,166 lines**: `IEEE1394_bd/src/sbp2_driver.c` 828, `IEEE1394_bd/src/scsi.c` 455, `IEEE1394_bd/src/main.c` 33, `iLinkman/src/iLink_intr.c` 469, `iLinkman/src/iLink_crom.c` 396, `iLinkman/src/iLink.c` 365, `iLinkman/src/iLink_TxRx.c` 211, `iLinkman/src/iLink_internal.c` 203, `iLinkman/src/iLink_DMA.c` 132, `iLinkman/src/iLink_PHY.c` 74.
Assault side read in full wherever a stripped hunk landed (all of `iLink_crom.c` BuildConfigurationROM, `iLink_internal.c`, `scsi.c`, `main.c`, `iLink.c` _start region), plus its `iLink_CROM.h` header (the endianness layouts differ per copy — see R5-M5-01 context) and `modules/bdmassault/PROVENANCE.md`.

### Per-file diff tallies (diff lines = `<`+`>` lines; hunks = diff change blocks)

| Pair file | ilink / assault lines | Raw diff lines (hunks) | Comment-stripped diff lines (hunks) |
|---|---|---|---|
| sbp2_driver.c | 828 / 801 | 57 (7) | 26 (6) |
| scsi.c | 455 / 386 | 141 (26) | 126 (25) |
| main.c | 33 / 33 | 10 (4) | 9 (2) |
| iLink_intr.c | 469 / 469 | 24 (10) | 24 (10) |
| iLink_crom.c | 396 / 398 | 96 (25) | 91 (22) |
| iLink.c | 365 / 369 | 26 (9) | 26 (9) |
| iLink_TxRx.c | 211 / 212 | 25 (8) | 22 (7) |
| iLink_internal.c | 203 / 190 | 40 (7) | 35 (7) |
| iLink_DMA.c | 132 / 132 | 10 (5) | 10 (5) |
| iLink_PHY.c | 74 / 74 | 2 (1) | 2 (1) |
| **Total** | **3,166 / 3,064** | **431 (102)** | **371 (94)** |

### Hunk classification (94 stripped hunks, READ, both sides in context)

| Class | Hunks | Where |
|---|---|---|
| Intentional variant | 32 | Include-path layout (`include/…`, every file); single-IRX chaining `BD_iLink_start` (main.c 2, iLink.c 2 + extern); documented static GUID/model in assault `GetConsoleIDs` (PROVENANCE.md: cdvdman crashes POPSTARTER); `dmac_enable/disable` vs `sceEnable/DisableDMAChannel` (2); `USE_CUSTOM_ILINKMAN_MALLOC_AND_FREE` guard (2 — never defined anywhere, so assault crom/sbp2 link against sbp2_driver.o's malloc/free in the one IRX); CROM endianness strategy in assault (13 — natural-order header in its `iLink_CROM.h` + `ieee1394Swab32`/`BSWAP16` at build, vs ilink's reversed-field header + no swab: two internally consistent ways to emit the same BE wire bytes) |
| Formatting / equivalent | 48 | clang-format alignment (all of iLink_intr.c's 9 non-include hunks, iLink_DMA.c 4, iLink_TxRx.c 6, iLink.c 6, iLink_crom.c 7, iLink_internal.c 1, sbp2_driver.c 1); scsi.c equivalents 14 (named SCSI opcodes vs literals, `sizeof(comData)` vs `12`, `U64_2XU32` macro form, assignment order) |
| Drift (no concrete defect) | 7 | sbp2 device-numbering (3 hunks, see Checked-and-clean); scsi.c missing `static_assert`s (2) and `bd->path`/`bd->devNr` copy (2, see Checked-and-clean) |
| Suspected bug | 7 | iLink_crom.c 2 hunks → R5-M5-01; scsi.c 5 hunks → R5-M5-02 |

Headline positive result: **every #1018-era hardening fix is present in both copies** — status matching/retirement, generation checks, response validation, bounded warm-up, sector-size guards. `iLink_intr.c`, `iLink_TxRx.c`, `iLink_DMA.c`, `iLink_PHY.c` have zero substantive divergence.

## Findings

### R5-M5-01 / Assault BuildConfigurationROM publishes an uninitialized VendorID quadlet / Category 6
- **Locations:** `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_crom.c:295-307` (assault) vs `modules/ilink/iLinkman/src/iLink_crom.c:295-304` (ilink)
- **Problem (READ):** The ilink copy zeroes the freshly malloc'd CROM buffer (`memset(CROM_Buffer, 0, CROMSize)`, ilink :298) and assigns `RootDirectory->VendorID = (IEEE1394_CROM_VENDOR << 24) | 0x80046` (ilink :304). The assault copy does neither: no `memset` anywhere in its `BuildConfigurationROM`, and of the five `Root_Directory` fields (its own `iLink_CROM.h:40-47`) it assigns only four — `VendorID` is never written. The unwritten quadlet is then byte-swapped in place (`ieee1394Swab32(...Root_Directory...)`, assault :307) and covered by the directory CRC16 (assault :318-320), so the published Configuration ROM is self-consistent but its vendor entry is whatever `AllocSysMemory` returned — heap garbage, different every build/boot, served to any external 1394 node that reads the root directory. Every other struct in the function is fully assigned, so VendorID is the only garbage field; the missing memset is what makes the omission silent.
- **Fix:** Port the ilink copy's two lines to the assault copy: zero the buffer after the malloc succeeds, and assign `VendorID` before the swab. Justification: adds nothing new — it restores the two lines the twin copy already has; no new logic.
- **Benefit:** The assault driver's advertised vendor identity becomes the intended Sony OUI entry instead of nondeterministic memory contents.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (assignment-by-assignment comparison of both function bodies against the assault header's struct definition).

### R5-M5-02 / Assault SCSI layer silently truncates LBAs at 2^32 sectors — no READ CAPACITY 16 / READ|WRITE 16 / Category 6
- **Locations:** `modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/scsi.c:136-155` (`scsi_cmd_rw_sector`), `:211-219` (warm-up capacity), `:129-134` (only capacity command) vs `modules/ilink/IEEE1394_bd/src/scsi.c:148-199`, `:278-294`
- **Problem (READ):** The ilink copy escalates: RC10 sentinel (`sectorCount == 0x100000000`) triggers READ CAPACITY 16, and `scsi_cmd_rw_sector` issues READ/WRITE 16 for `lba > 0xffffffff`. The assault copy has only the 10-byte forms: its CDB packs `(lba & 0xFF000000) >> 24` etc., discarding every LBA bit above 31, and its warm-up sets `bd->sectorCount = (u32)getBI32(last_lba) + 1` with no sentinel handling — a >2 TB drive (512-byte sectors) reports the RC10 sentinel `0xFFFFFFFF`, is published to BDM with `sectorCount = 2^32`, and any access at or beyond sector 2^32 wraps to the start of the disk: a write there overwrites the partition table / filesystem head. The copy's own comment (assault :144-145) admits the commands "need to be updated to 64bit LBA SCSI commands… For now the 32bit LBA will only support up to 2TB drives" — but nothing enforces the 2 TB ceiling at warm-up, so the documented limit is not a guard.
- **Fix:** Smallest safe form: in assault `scsi_warmup`, reject (return -1) when RC10 `last_lba == 0xFFFFFFFF`, so an oversize drive is never offered to BDM instead of being offered corruptible. Full form: port the ilink copy's RC16 + READ/WRITE 16 blocks. Justification: the reject is one comparison against a sentinel the code already reads; the port copies existing in-repo code, no new design.
- **Benefit:** Removes a silent wrap-to-sector-0 write path on >2 TB FireWire drives in the POPSTARTER assault build.
- **Effort:** S (reject) / M (port) · **Risk:** Low (reject) / Med (port) · **Confidence:** High, READ (truncation is arithmetic in the CDB packing; the in-code comment confirms the limitation is known but unguarded).

### R5-M5-03 / iLinkTrFree / iLinkGetNodeTrSpeed / iLinkSetNodeTrSpeed index TransactionContexts with no range check — in both copies / Category 6
- **Locations:** `modules/ilink/iLinkman/src/iLink.c:209-212` (`iLinkTrFree`), `:330-343` (`iLinkGetNodeTrSpeed`), `:345-360` (`iLinkSetNodeTrSpeed`); assault twins at `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink.c:213`, `:334`, `:349`. Array: `TransactionContexts[10]` (`MAX_CONCURRENT_TRANSACTIONS`, iLink_internal.h:280 in both trees).
- **Problem (READ):** The same file's `iLinkTrRead`/`iLinkTrWrite` were hardened to reject `trContext < 0 || trContext >= MAX_CONCURRENT_TRANSACTIONS` (ilink :219-221, :243-245), but the other three exported entry points that take a `trContext` were not: `iLinkTrFree` writes `TransactionContexts[trContext].IsConnected = 0`, `iLinkSetNodeTrSpeed` writes `.speed`, and `iLinkGetNodeTrSpeed` reads `.speed`, all at a caller-supplied index with no check. These are iLinkman library exports, so any IOP module linked against it — not just sbp2, whose indices come from `iLinkTrAlloc` and are safe — can drive an out-of-bounds read/write in IOP memory. Present identically in both copies; never filed (grep of rounds 1-4 for these symbols: no hits).
- **Fix:** Apply the range check the file already uses in TrRead/TrWrite to the three functions (early return / -1002 as each signature allows). Justification: adds a check, but it is a verbatim copy of the check already established three functions away in the same file — consistency, not new policy.
- **Benefit:** Closes the remaining unchecked-index half of the exported transaction-context API in both driver builds.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ. (Severity framing: local IOP callers only; no remote trigger was found in the read — this is an exported-API robustness gap, not a network hole.)

## Already filed — not repeated
- **RIPTOPL-021** — iLink driver maintained twice / duplicated-driver cost. Not re-filed.
- **RIPTOPL-047** — `modules/bdmassault/` is a near-total second copy of `modules/ilink/` (expands 021). Not re-filed; all divergence above is reported as remainder under it, not as new duplication findings.
- Round-4 bdmsupport finding on function-local `ilinkProbeDone` latch (in `src/sources/bdmsupport.cpp`, outside these pairs) — untouched, not repeated.

## Checked and clean
- **iLink_intr.c / iLink_TxRx.c / iLink_DMA.c / iLink_PHY.c pairs** — stripped diffs are include-path + alignment only; every hardening hunk (response matching, generation checks, alarm cancellation, TX-semaphore release) is byte-equivalent in both copies. No remainder.
- **Assault static GUID/model (`iLink_internal.c:57-64`)** — suspected-bug on first read, dissolved: `modules/bdmassault/PROVENANCE.md` documents it as deliberate (upstream's cdvdman imports crash POPSTARTER, which never loads cdvdman).
- **Assault `_start` chaining to `BD_iLink_start` + `_exit` unwind on failure (iLink.c:160-163, main.c:15)** — intentional single-IRX variant, and its failure path is at least as careful as the ilink copy's.
- **Device numbering divergence (sbp2_driver.c / scsi.c drift hunks)** — ilink numbers by `sbp2FreeDevNr` and copies `scsi->devNr` + `bd->path = "ilink"` at connect; assault never sets `scsi.devNr` and numbers by `g_scsi_bd` slot at `scsi_init`. Not filed: the ilink comment's failure mode (numbering by *SBP2 slot*, whose reservation survives a lost status) does not reproduce in the assault copy — its number comes from the scsi slot, which a failed warm-up frees (`bd->priv = NULL`), so a lone drive still lands on device 0; and `bd->path == NULL` is NULL-guarded at every consumer read (`fs_driver.c:259,275` region) in a single-device POPSTARTER image whose filesystem is the mass driver. Recorded as drift only.
- **ReadRequestHandler arbitrary-read surface (iLink_intr.c, both copies)** — dissolved: `REQ_CHECK_MEM_BOUNDARIES` is defined at `iLink_internal.h:15` in *both* trees, so out-of-CROM reads are rejected unless inside the checked 2 MB IOP window; WriteRequestHandler's boundary check is likewise compiled in both.
- **PHYPacketHandler `NodeData[nNodes++]` (iLink_intr.c:98-99, both copies)** — considered: the array is sized 63, the 1394 maximum node count, and entries are stored only during the hardware self-ID phase; an overflow would need more self-ID quadlets than the bus's physical node limit produces in this driver's two-node use. Not filed.
- **`iLinkAddCROMUnit` unchecked `nQuads` (iLink_crom.c:65-90, both copies)** — considered: `nQuads * 4` could wrap before the malloc/swab, but the function has no in-tree caller in either tree (export-only) and no remote path to it was found in the read. Not filed; noted here so a future export audit can pick it up.
- **`GetBIOSVersion` unchecked `open`/`read` (iLink.c:61-72, both copies)** — considered: a failed read would leave `romver_str` uninitialized before `strtoul`, but it reads `rom0:ROMVER`, present on every target console; no reachable failure path identified. Not filed.
- **Assault `scsi_warmup` not zeroing `rcd` before READ CAPACITY (assault scsi.c:211)** — dissolved: the buffer is read only after a success return, on which the target fills the full 8-byte response; the ilink copy's memset is hygiene, not a guard.
- **CROM endianness strategies generally** — verified equivalent in intent against each copy's own `iLink_CROM.h` field layouts (assault: natural order + swab; ilink: reversed fields, no swab; both BSWAP16 only the bus-info ROM CRC). The only defect inside either strategy is R5-M5-01.
