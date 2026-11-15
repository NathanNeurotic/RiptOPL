# Sable Audit — RiptOPL — ROUND 5

- **Repo / pin:** NathanNeurotic/RiptOPL @ `2e347d5ac885cd0ff73e9ad4143781946396f492` (rebuild/main). The pin was re-verified live at round start (`git ls-remote` == clone HEAD) and the clone was **unchanged** — no delta round, no revalidation duties beyond the spot re-confirmations noted below. `git status --porcelain` empty at finish.
- **Mode:** READ ONLY, per the audit's standing rules. LESS NOT MORE governs every fix. No PS2 build exists in this VM (no PS2SDK) and nothing in this round is hardware-verified; Category 5 stays structural. No secret-like values are printed anywhere in this report.
- **Method:** Round 4's ledger left a defined remainder: EE `src/` below the round-4 tier, the `modules/` files no round had enumerated, the rest of `tools/`, and small leftovers. That remainder was inventoried first (per-file `wc -l` against the union of rounds 1–4 read lists), then read **1..EOF** in 15 sections (EE src ×7, modules ×6, tools ×2; section reports preserved in `round5-sections/riptopl-r5-*.md`). The parent additionally read the last unassigned residue personally (896 lines: `cdvdman/ncmd.c`, mx4sio `sio2man_hook.c`/`ioplib.c`/`crc16.c`, `udpfs_ministack/udptty.c`, `usbmass_bd/main.c`, `xhdd/hdpro.c`, six 2-line UDPFS include shims, and the three `pccf` `.inc` files, 115 lines) — one finding below (RIPTOPL-224) is parent-read and marked as such. Headline mechanisms were spot-verified by the parent against the clone before consolidation (see below); all reproduced.
- **Parent spot-verification (this round):** RIPTOPL-145 (unclamped `RecvData` at `smbman-ra/smb.c:261`), RIPTOPL-147 (journal loop + 126-entry array at `libapa.h:188`), RIPTOPL-149 (`int num_partitions` → `unsigned char NumParts`, walk at `device-hdd.c:35`), RIPTOPL-158 (CNFGetKey cap vs `CNF_PATH_LEN_MAX 64` and the caller's 64-byte buffer, traced end-to-end), RIPTOPL-174 (icon.sys write and its deliberate-size comment at `OSDHistory.cpp:27,59`), RIPTOPL-151 (iso2opl `strcpy(boot_path, p2)` site), RIPTOPL-184 (assault CROM: no `VendorID` assignment exists in the file — grep-verified absence alongside the section's diff evidence). One framing correction from verification: RIPTOPL-174's oversized write is **deliberate and commented in-tree** (HDDOSD demands 1,776 bytes); the finding is filed as an out-of-bounds *read* with build-dependent padding content, not as an accidental overflow, and its fix preserves the on-card size exactly.
- **Revalidations at the pin (still open, not re-filed):** RIPTOPL-045 (streaming.c DMA spin, `streaming.c:234-235`), RIPTOPL-038 (SMB filename copies), X-31 (UDPRDMA unclamped wire header; the SMS clamp still not cross-ported). No prior finding (RIPTOPL-001…144) was contradicted by this round's full reads; two are extended/narrowed by location only, next section.

## Extensions and narrowing notes (not new findings)

- **RIPTOPL-064 extended by location:** the half-discarded `long_multiply` helper also exists in the PC tool at `tools/pc/genvmc/src/genvmc.c:144-165`, sole call `:313-315`, `lo` likewise never read. Same substance, second location.
- **RIPTOPL-026 narrowed in one respect:** `publish_llvm.py` now imports `publish_editions` as `common` and reuses its upload/verify plumbing, so the "triplicated plumbing" half of that finding has been partly consolidated upstream since it was filed. What remains triplicated is the release-notes construction (three implementations of the same changelog loop/escape set) and the SHA256SUMS parsing block in the three `verify()` functions. The finding's substance — three publishers with duplicated logic — stands; its plumbing clause should be read with this note.

---

## Findings (RIPTOPL-145 … RIPTOPL-280, benefit order)

### RIPTOPL-145 / GetSMBServerReply receives whatever length the server's session header claims, into a 4,224-byte static buffer

- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:226-266` (`GetSMBServerReply`, esp. `:257-263`); buffer definition `smb.c:37-83`; sizes at `modules/network/smbman-ra/smb.h:894-895` (`MAX_SMB_BUF 4096`, `MAX_SMB_BUF_HDR 128`).
- **Problem:** After the reply's 4-byte NetBIOS session header is read, `totalpkt_size = nb_GetSessionMessageLength()` is a server-supplied 24-bit value (up to 16,777,215). When `rhdrlen == 0` — the path used by negotiate, session setup, tree connect, share enum, query, create, close, delete, rename, find, echo, logoff — that value is passed unclamped to `RecvData(..., (char *)&SMB_buf.smb, size, ...)` whose destination is the 4,224-byte static `SMB_buf`. Nothing compares the claimed length against the buffer, against the server's negotiated `MaxBufferSize`, or against `CLIENT_MAX_BUFFER_SIZE` (65,535, itself 15× the buffer) that this same file advertises to the server at `smb.c:494`. A conforming-but-generous server can overflow it; a hostile or broken one can write megabytes past a static IOP buffer. The sibling in-game stack does the opposite (round 2 cleared cdvdman `smb.c` precisely for its "explicit NBSS length rejection"). Not covered by RIPTOPL-038, which is the FindFirstNext2 filename copy downstream of a received packet.
- **Fix:** Reject (return -2) any session length larger than the receive buffer before the body read, in `GetSMBServerReply` only — one comparison at the single choke point every command shares.
- **Justification (adds a check):** the length is wire data and the destination is fixed-size; there is no smaller fix, and the check deletes no behaviour for conforming servers (replies are capped by the negotiated buffer size in practice).
- **Benefit:** Closes an unbounded server-controlled write that sits under every menu-side SMB1 operation.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-146 / smb_ReadAndX trusts the response's DataOffset and DataLength — both overflow, one into the caller's buffer

- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:1042-1094` (esp. `:1077-1089`).
- **Problem:** Two server-controlled fields are used unchecked. (1) `padding = RRsp->DataOffset - sizeof(ReadAndXResponse_t)`; if a response claims a large DataOffset, `RecvData(main_socket, (char *)(RRsp + 1), padding, ...)` writes up to ~65 KB past the response header inside the 4,224-byte `SMB_buf` (and if DataOffset < header size, the negative padding is silently skipped while the stream is now desynchronised). (2) `DataLength` (32-bit assembly of the response's High/Low fields) is passed straight to `RecvData(main_socket, readbuf, DataLength, ...)` — never compared with `nbytes`, the amount the caller actually requested and sized its buffer for. `smb_ReadFile` (`:1096-1120`) loops on the returned count, so an over-long DataLength both overflows the caller's read buffer and corrupts the loop's accounting. The write side of the same file was already hardened this way (`smb.c:1170` rejects a Count greater than what was sent); the read side was not.
- **Fix:** Clamp/reject in `smb_ReadAndX`: fail the read if `DataLength > nbytes`, and if `DataOffset` is less than the header size or the implied padding exceeds the packet buffer.
- **Justification (adds checks):** both values are wire data feeding copy lengths; the write path in the same file already establishes this exact validation pattern, so the fix is a transcription of in-file precedent, not new machinery.
- **Benefit:** Removes a server-controlled overflow of caller-owned game/VMC read buffers on the hot data path.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-147 / APA journal restore trusts the on-disk entry count and replays to unvalidated sectors

- **Category:** 6 (bugs/security)
- **Locations:** `modules/hdd/apa/src/journal.c:57-86` (`apaJournalRestore`); count used at `:69` and `:73`; destination taken from `journalBuf.sectors[i]` at `:77`; array definition `modules/hdd/apa/src/libapa.h:184-189` (`s32 num; u32 sectors[126];`); write side `journal.c:47-55` (`apaJournalWrite`, `journalBuf.num++` with no bound).
- **Problem:** The journal header is read straight off the disk and only its magic is checked. `num` is a signed 32-bit on-disk field with **no clamp** against the 126-entry `sectors[]` array (or against the 252-sector journal-header area, `APA_SECTOR_APAL_HEADERS` 10–262). A corrupt or crafted HDD with `num > 126` makes the restore loop (a) read destination sector numbers out of bounds past the static `journalBuf` (whatever BSS follows it becomes a target sector), (b) read its source headers ever further past the journal area into live partition data (`sector += 2` per entry), and (c) write those 2-sector blocks to the bogus destinations — at mount/probe time, before any game is loaded; with `num` near INT_MAX it is also an effective IOP hang doing disk writes throughout. Even for `num ≤ 126` no entry is validated as a plausible partition-header sector. The APA table fence (`table_fence.h`) does not mitigate this: it only restricts writes touching LBA 0–7, and replay destinations are ≥ 8 by construction of the attack. The write side has the symmetric missing bound (`apaJournalWrite` increments past 126 if more than 126 headers are ever flushed in one pass — today bounded only by the loader-controlled cache size).
- **Fix (words only):** Reject/clamp in `apaJournalRestore`: if `num < 0 || num > 126`, treat the journal as absent (reset and return success-with-no-replay, or return `-EIO` — but do not loop). Add the same `num >= 126` refusal in `apaJournalWrite`. Justification: adds a bounds check — it is the only thing standing between on-disk data and arbitrary-sector writes during recovery.
- **Benefit:** A damaged journal degrades to "no replay" instead of corrupting unrelated disk regions or hanging the IOP at startup.
- **Effort:** S · **Risk:** Low (legitimate journals are written by `apaJournalWrite` from a ≤ cache-size pool of headers and cannot exceed 126 in any configuration the loader ships) · **Confidence:** High, READ (count, array size, loop and both transfer calls all read at the pin; BSS contents past the array are INFERRED, the out-of-bounds read/write mechanism is not).

### RIPTOPL-148 / isofs ignores every `cdEmuRead` failure: failed reads return stale sectors as good data

- **Category:** 6
- **Locations:** `modules/isofs/isofs.c:261` (`cdvdman_locatefile` discards the result of the TOC sector read, then parses `cdvdman_buf`); `:448, :462, :472` (`IsofsRead`'s three phases discard the result, advance `fh->position` and return the full requested count regardless).
- **Problem:** `cdEmuRead` has a real error contract — both implementations return `-EIO` on failure (`:173-182`), and the one caller that checks it (`ProbeISO9660`, `:558`) depends on that. The locate and read paths do not check it at all. A failed TOC read makes `cdvdman_locatefile` walk whatever the shared 2048-byte buffer held from a previous sector or a previous lookup, and can return a directory entry — LBA and size included — for a file that is not there. A failed data read makes `IsofsRead` copy the static bounce buffer's previous contents (phase 1/3) or leave the caller's buffer untouched (phase 2), report complete success, and advance the file position past the gap. This is the exact failure mode `bd_cache.c`'s in-repo fix comment describes eliminating for BDM ("a failed or short refill left the buffer holding the evicted block, which was then returned as a successful read"); the ISO path — the game-data path — still has it.
- **Fix (words only):** Propagate the error: `cdvdman_locatefile` returns NULL when the TOC read fails; `IsofsRead` returns `-EIO` (or the bytes genuinely read before the failing phase) and does not advance the position past unread data. Justification: uses the error contract the read functions already provide; no new machinery.
- **Benefit:** A scratched image / dropped device surfaces as an I/O error at open/read instead of silently substituting another sector's bytes for game data.
- **Effort:** S–M · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-149 / HDD device trusts the on-disc APA header's partition count — stored truncated into a `u8`, then used to walk a 65-entry table

- **Category:** 6 (bugs/security)
- **Locations:** `modules/iopcore/cdvdman/device-hdd.c:20` (`static unsigned char NumParts;`), `:75` (bounded `memcpy` of the specs table), `:80` (`NumParts = apaHeader.num_partitions;`), `:35` (walk loop), `:118-124` (`DeviceReadSectors` indexing `cdvdman_partspecs[CurrentPart]`); source struct `modules/iopcore/cdvdman/atad.h:96-97` (`int num_partitions; hdl_partspecs_t part_specs[HDL_NUM_PART_SPECS]` with `HDL_NUM_PART_SPECS` = 65, `atad.h:84`).
- **Problem:** The APA header is read raw from the HDD (`:70-73`) and its `num_partitions` is taken verbatim. The copy into `cdvdman_partspecs[65]` is safe — the source field is itself a 65-entry array — but the count is not: (a) any value 66–255 makes `cdvdman_get_part_specs` walk past the 65-entry static table into adjacent BSS, comparing attacker/corruption-controlled "partitions"; a match also sets `CurrentPart` to that out-of-range index, which `DeviceReadSectors` then indexes directly; (b) the destination is `unsigned char`, so a header value of e.g. 256 truncates to 0 (every read then fails with `-ENXIO`/TRMOPN) and 321 truncates to 65 — the effective count silently depends on the value mod 256. A corrupt or crafted HDD image (HDL-format discs are routinely built by PC tools) is the input; no validation exists anywhere in this file.
- **Fix:** Clamp/reject at the single point of ingestion in `DeviceInit`: if `apaHeader.num_partitions` is outside `1..HDL_NUM_PART_SPECS`, treat the header as invalid instead of storing the count; store into a type at least as wide as the source field. Justification: one comparison at ingestion removes both the OOB walk and the truncation aliasing for every consumer; no new mechanism.
- **Benefit:** Removes an out-of-bounds read (and a wrong-partition read) driven by on-disc data in the in-game HDD path.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-150 / smb_NetShareEnum walks the response with strlen and server-supplied offsets, none of them bounded

- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:639-722` (esp. `:692-719`).
- **Problem:** The RAP response is parsed entirely on trust: `ParamOffset` indexes `SMB_buf.smb.u16buff[]`/`.s16buff[]` (`:692`, `:696`) and `DataOffset` becomes the base pointer `data` (`:699`), with no check that either lies inside the bytes actually received. `AvailableEntries` is a signed 16-bit server value driving the loop; each iteration calls `strlen(p)` three-plus times on a pointer advanced by `strlen(p) + 1 + padding + 4` (`:705-718`) — a response without a NUL inside the received bytes walks `p` arbitrarily far past the static buffer, reading (and the loop's type test at `:707` dereferences) out of bounds. The share comment is fetched through a second server-supplied u16 offset used as an index from `data` (`:712`) with no bound at all. Any of these is reachable by a broken or hostile share-enumeration reply before a single share is listed.
- **Fix:** Bound the parse by the received length returned from `GetSMBServerReply`: reject ParamOffset/DataOffset outside it, replace the `strlen` walk with a remaining-bytes-bounded scan, and check the comment offset against the remaining data before dereferencing.
- **Justification (adds checks):** every offset and terminator here is wire data; the fix is confined to this one parser and mirrors the bounded-parse pattern already used in `ministack_xchg.c`'s reply handler in the same tree.
- **Benefit:** Share browsing — the first thing a user does against an untrusted NAS — stops being an unbounded read/write primitive.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-151 / iso2opl trusts SYSTEM.CNF sizes and strings from the ISO in three places — a crafted ISO overflows a global buffer, a stack buffer, and dereferences NULL

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

### RIPTOPL-152 / VMC-sourced save and file names are never validated, and the folder export writes them into filesystem paths

- **Category:** 6
- **Locations:** `src/core/vmcsaves.cpp:453` (`vsLoadVmc`: `snprintf(s->name, "%.32s", dir.name)`), `src/core/vmcsaves.cpp:469` (same for each file's `e.name`), `src/core/vmcsaves.cpp:720-746` (`vsWriteFolder`: `mkdir("%s%s", parent, s->name)`, per-file `open("%s%s/%s", parent, s->name, f->name, O_CREAT|O_TRUNC)`)
- **Problem:** The same file establishes the invariant and then skips it on one of three loaders. `vsNameOk` (`:767`: 1–32 bytes, no `/`, not `.`/`..`) is enforced for PSU-loaded names (`:796`, `:822`) and at `vsWritePsu` (`:867`, with the comment "a name from a card (`../x`) never leaves the folder"), and `vsLoadFolder` validates its name argument. `vsLoadVmc` validates nothing: a damaged or crafted VMC image whose root folder name is `../x` (or contains a `/`), or whose file entries do, is copied verbatim into `vs_save_t`, and `vmcSavesExport` / `vmcSavesPsuToFolder` then hand it to `vsWriteFolder`, which mkdirs and creates/truncates files at the resulting path — outside the SAVES folder the user picked. The input is a card image file, i.e. exactly the untrusted-input class the PSU leg already defends against.
- **Fix:** Run `vsNameOk` on the save name and every file name in `vsLoadVmc` (fail with `VMCSAVES_ERR_NAME`, as `vsLoadPsu` does). Justification: this adds a check, and it is the check the same file already applies to the two sibling loaders; it makes the stated invariant true for all three.
- **Benefit:** A crafted VMC image can no longer write outside the export folder; the three loaders behave identically on names.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-153 / Negotiate copies the server's KeyLength (up to 255) into an 8-byte field

- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:403`; destination `modules/network/smbman-ra/smb.h:286` (`u8 EncryptionKey[8]`, the last field of `server_specs_t`).
- **Problem:** `memcpy(server_specs.EncryptionKey, NPRsp->ByteField, NPRsp->KeyLength)` — `KeyLength` is a server-supplied byte in the negotiate response, never checked. A value above 8 writes past `server_specs_t` into adjacent static storage during connection setup, before authentication. Round 2's clearance of the sibling stack noted cdvdman `smb.c` has explicit KeyLength checks; this copy does not. Distinct from RIPTOPL-038, which covers the `getStringField` call on the next line (`:404`).
- **Fix:** Reject the negotiate reply (goto negotiate_error) when `KeyLength > sizeof(server_specs.EncryptionKey)`.
- **Justification (adds a check):** one comparison on wire data at the only copy site.
- **Benefit:** Closes a pre-auth, server-controlled static-buffer overrun in the connection handshake.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-154 / smb_QueryPathInformation casts structures at the response's unchecked DataOffset

- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:833` and `:846` (the same pattern recurs at `:1482` in `smb_FindFirstNext2`, whose downstream copy is already RIPTOPL-038).
- **Problem:** `QPIRsp->smbTrans.DataOffset` is a server-supplied u16 used directly as an index into `SMB_buf.smb.u8buff[]` and cast to `BasicFileInfo_t *` / `StandardFileInfo_t *`; eight 64-bit fields are then read out of it. An offset near 65,535 reads ~64 KB past the 4,224-byte static buffer and returns whatever it finds as file times, attributes and — critically — `EndOfFile`, the size the rest of the stack trusts for the legacy-open path (`smb_LegacyOpenAndX`, `:1004-1007`). No check compares the offset (plus the structure size) with the received packet length.
- **Fix:** Reject the reply if `DataOffset + sizeof(the structure being cast)` exceeds the received length, at both cast sites.
- **Justification (adds a check):** the offset is wire data; two comparisons at two sites, no new state.
- **Benefit:** File size/attribute queries against a bad server stop manufacturing sizes from out-of-bounds memory.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-155 / isofs `TocEntryCopy` writes the terminator at the disc-supplied filename length, past its 129-byte field

- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/iso2opl/src/isofs.c:287-298` — Joliet branch caps `filenamelen = filenameLength / 2` (≤127, safe); the ISO9660 branch sets `filenamelen = internalTocEntry->filenameLength` (a raw `u8` from the directory record, up to 255), copies with `strncpy(tocEntry->filename, …, 128)`, then executes `tocEntry->filename[filenamelen] = 0;`
- **Problem:** The copy is bounded but the terminator write is not. A directory record with `filenameLength` > 129 writes a zero byte up to ~126 bytes past `filename[129]` — into the stack `localTocEntry` in `FindPath` or the caller's `TocEntry` elsewhere. Real ISO9660 names are short; a crafted ISO's directory sector is not bound by that, and directory records are parsed before any file is opened.
- **Fix:** Clamp `filenamelen` to 128 in the non-Joliet branch (the value the `strncpy` already assumes).
- **Justification (fix adds a check):** one comparison on an untrusted length byte; the alternative — trusting it — is the bug.
- **Benefit:** crafted directory sectors can no longer write outside the TOC entry during a plain file lookup.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-156 / Assault SCSI layer silently truncates LBAs at 2^32 sectors — no READ CAPACITY 16 / READ|WRITE 16 / Category 6

- **Locations:** `modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/scsi.c:136-155` (`scsi_cmd_rw_sector`), `:211-219` (warm-up capacity), `:129-134` (only capacity command) vs `modules/ilink/IEEE1394_bd/src/scsi.c:148-199`, `:278-294`
- **Problem (READ):** The ilink copy escalates: RC10 sentinel (`sectorCount == 0x100000000`) triggers READ CAPACITY 16, and `scsi_cmd_rw_sector` issues READ/WRITE 16 for `lba > 0xffffffff`. The assault copy has only the 10-byte forms: its CDB packs `(lba & 0xFF000000) >> 24` etc., discarding every LBA bit above 31, and its warm-up sets `bd->sectorCount = (u32)getBI32(last_lba) + 1` with no sentinel handling — a >2 TB drive (512-byte sectors) reports the RC10 sentinel `0xFFFFFFFF`, is published to BDM with `sectorCount = 2^32`, and any access at or beyond sector 2^32 wraps to the start of the disk: a write there overwrites the partition table / filesystem head. The copy's own comment (assault :144-145) admits the commands "need to be updated to 64bit LBA SCSI commands… For now the 32bit LBA will only support up to 2TB drives" — but nothing enforces the 2 TB ceiling at warm-up, so the documented limit is not a guard.
- **Fix:** Smallest safe form: in assault `scsi_warmup`, reject (return -1) when RC10 `last_lba == 0xFFFFFFFF`, so an oversize drive is never offered to BDM instead of being offered corruptible. Full form: port the ilink copy's RC16 + READ/WRITE 16 blocks. Justification: the reject is one comparison against a sentinel the code already reads; the port copies existing in-repo code, no new design.
- **Benefit:** Removes a silent wrap-to-sector-0 write path on >2 TB FireWire drives in the POPSTARTER assault build.
- **Effort:** S (reject) / M (port) · **Risk:** Low (reject) / Med (port) · **Confidence:** High, READ (truncation is arithmetic in the CDB packing; the in-code comment confirms the limitation is known but unguarded).

### RIPTOPL-157 — mcemu HDD backend: a page that maps to no APA fragment is silently translated to sector 0 — the APA header sector

- **Category:** 6 (missing error path converts an unmapped page into a valid, critical LBA)
- **Locations:** `modules/mcemu/device-hdd.c:11-28` (`Mcpage_to_Apasector`; `sector_to_read` initialised to 0 at `:17` and returned unchanged when no `blocks[i]` range matches), callers `:30-48` (`DeviceWritePage`/`DeviceReadPage` pass the result straight to `sceAtaDmaTransfer`)
- **Problem:** READ. The fragment walk has no "not found" outcome: any page outside all ten fragment ranges — a gap in the table, a corrupt/truncated VMC spec, or a page beyond the image — returns LBA 0, and both callers transfer one sector there unconditionally. For a write, LBA 0 is the HDD's APA partition header sector: the exact sector whose corruption makes the whole partition table unreadable. This is the destructive sink downstream of RIPTOPL-072 (mcemu.c accepts any page number; CardSize never used as a bound) — but it is **not** closed by 072's fix alone, because even a validated-in-range page can fall in a fragment-table gap, and this function still cannot say so.
- **Fix (words):** Give `Mcpage_to_Apasector` a failure result (e.g. return a bool and the sector via pointer, or reserve an error sentinel the callers test) and have `DeviceWritePage`/`DeviceReadPage` return 0 without issuing the DMA transfer when the page is unmapped. *Justification (adds a check): one comparison per caller; there is no existing error path in this file to reuse.*
- **Benefit:** An unmapped VMC page fails one card operation instead of reading — or overwriting — the disk's partition header.
- **Effort:** S · **Risk:** Low (legitimately mapped pages, including a fragment that genuinely starts at sector 0 of its partition area, are distinguished by the found-flag, not the value) · **Confidence:** High on the mechanism (READ); trigger reachability via a gapped/corrupt spec is INFERRED, via RIPTOPL-072's unchecked page numbers is READ in that finding.

### RIPTOPL-158 / SYSTEM.CNF boot token of ≥64 printable chars is returned unterminated

- **Category:** 6 (bug/security — disc-supplied data)
- **Locations:** src/platform/ps2cnf.cpp:42-68 (`CNFGetKey`); cap `CNF_PATH_LEN_MAX 64` at include/ps2cnf.h:1; sole caller's buffer `char ps2disc_boot[CNF_PATH_LEN_MAX]` at src/sources/supportbase.cpp:116
- **Problem:** `CNFGetKey` copies graph characters until a space, NUL, or `i == CNF_PATH_LEN_MAX`. In the cap case it returns without ever writing a `'\0'`: a SYSTEM.CNF whose BOOT2 value is 64+ printable characters fills the caller's 64-byte stack buffer completely, and the caller then consumes it as a C string, reading past the buffer. Real disc boot paths are far shorter, so this needs a crafted/corrupt CNF — but SYSTEM.CNF is exactly the untrusted-input surface this parser exists for.
- **Fix:** Stop the copy at `CNF_PATH_LEN_MAX - 1` so the existing space/NUL branch (or a final explicit terminator after the loop) always lands inside the buffer.
- **Justification (fix changes a bound):** one comparison; no new code paths, and no legitimate boot filename approaches 63 chars.
- **Benefit:** Closes an unterminated-string read on disc-controlled data at the parser's only call site.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — loop, cap and caller buffer size all verified)

### RIPTOPL-159 / PadManager::start() can index padData_ past MAX_PADS — the cap break sits outside the slot loop / Category 6

- **Locations:** `src/core/pad.cpp:1155-1191` (`start()`), esp. `padData_[padCount_]` at `:1175` and the only cap check `if (padCount_ >= MAX_PADS) break;` at `:1188-1189`; array declared `pad_data_t padData_[MAX_PADS]` (MAX_PADS 4) at `include/padmanager.h:84`.
- **Problem:** The `MAX_PADS` break is tested only after a whole port's slot loop finishes, never inside it. `padCount_` therefore only needs to be 1–3 when a multi-slot port is entered for the slot loop to run past the array: e.g. port 0 with one slot (count 1), then a multitap port with 4 slots drives count 2, 3, 4 and then takes `&padData_[4]` — writing `port/slot/state/needsInit/...` into the members that follow the array in PadManager (`paddata_`, `oldpaddata_`, `heldUnknown_`, `delaycnt_[16]`, `paddelay_[16]`, rumble state) and passing that slot's 256-byte `padBuf` (which overlaps the same members) to `padPortOpen` as a DMA buffer in `startOne()` (`pad.cpp:1136-1153`). The overflow is not gated on pads actually being present: `initialize()` returns 1 on every path (`pad.cpp:209, 235, 248, 256, 296` — including "not connected"), so `startOne()` succeeds for every opened slot and every slot increments the count. Reachability of a 4-slot port depends on libpad's `padGetSlotMax()` returning 4 for a multitap port (INFERRED from libpad convention; the loop structure itself is READ).
- **Fix:** Move the cap inside the slot loop (break out of both loops as soon as `padCount_ >= MAX_PADS` before taking `&padData_[padCount_]`). Justification: this adds one comparison to a boot-only loop — it is the smallest change that makes the existing cap actually enforce the array bound the code already intends.
- **Benefit:** Removes a boot-time heap/static corruption path on a two-multitap (or 1 + multitap) rig, in the subsystem every launch depends on.
- **Effort:** S. **Risk:** Low. **Confidence:** High for the loop defect (READ); Med for hardware reachability (INFERRED libpad slot semantics; not hardware-verified).

### RIPTOPL-160 / GPT parser never validates an entry's extent: reversed or off-device ranges become huge partitions

- **Category:** 6
- **Locations:** `modules/bdm/src/part_driver_gpt.c:130` (only sanity check: `first_lba < header->first_lba || last_lba > header->last_lba`); `:175` (`sectorCount = last_lba - first_lba + 1`); `:68` (`entriesPerSector = bd->sectorSize / sizeof(gpt_partition_table_entry)` — the header's own entry-size field is never consulted); bounds that trust the result: `modules/bdm/src/part_driver.c:47-90` (`part_read`/`part_write` bound I/O only against `bd->sectorCount`).
- **Problem:** Three trust gaps in one parse: (1) an entry with `first_lba > last_lba` passes the `:130` check whenever both values lie inside the header's usable range, and `last - first + 1` then underflows to an astronomically large `sectorCount`; (2) the header's `last_lba` is itself disk-supplied and is never checked against `bd->sectorCount`, so a partition can be registered extending past the physical device; (3) the stride assumes every GPT uses 128-byte entries, ignoring the header's entry-size field, so a spec-legal disk with larger entries is parsed at the wrong offsets. Because `part_read`/`part_write` deliberately use the partition's `sectorCount` as their only fence ("never write past the partition end into a neighbouring partition", `part_driver.c:63`), a poisoned count disarms that fence: the resulting pseudo-device can read/write across neighbouring partitions, gated only by whatever the raw device driver below happens to refuse.
- **Fix (words only):** In the `:130` sanity check also require `first_lba <= last_lba` and `last_lba < bd->sectorCount`; refuse or clamp when the header's entry size differs from `sizeof(gpt_partition_table_entry)`. Justification: adds validation of on-disk geometry before it becomes the I/O fence.
- **Benefit:** A corrupt/malicious GPT can no longer mint a partition whose bounds defeat the partition driver's own containment check.
- **Effort:** S · **Risk:** Low (no conforming GPT — entries inside the device, 128-byte entries — is affected) · **Confidence:** High, READ (underflow arithmetic and the missing comparisons are direct; that lower drivers might also refuse some out-of-range I/O is INFERRED and does not restore the fence).

### RIPTOPL-161 / MBR sanity check is half tautology, and partition extents are never checked against the device

- **Category:** 6 (with a Category 1 component)
- **Locations:** `modules/bdm/src/part_driver_mbr.c:11-32` (`partitions_sanity_check_mbr`: `valid++` at `:29` executes for every entry unconditionally, so `return (valid == 4) && (active > 0);` at `:32` reduces to `active > 0`); `:94-129` (mount loop uses `sector_count` at `:124` with no extent test; the only range check anywhere is `first_lba >= bd->sectorCount` inside the sanity function at `:22`).
- **Problem:** The function presents as a four-part validity check but `valid` can never be anything but 4 — dead arithmetic that makes the check look stronger than it is. Substantively, an entry whose `first_lba` is in range but whose `sector_count` runs far past the end of the device (or over the following partitions) is accepted verbatim as the pseudo-device's `sectorCount`, and — as in R5-M3-07 — `part_read`/`part_write` treat that count as the containment fence. A crafted/corrupt MBR thus defines partitions the driver will happily write outside the physical disk layout.
- **Fix (words only):** Delete the `valid` counter (return the `active > 0` test the code actually performs) and add the extent test `first_lba + sector_count <= bd->sectorCount` (overflow-safe form) to the sanity check or the mount loop. Justification: the added test is the MBR counterpart of the GPT header-bound check; the deletion is the LESS-NOT-MORE half.
- **Benefit:** The check says what it does, and MBR partitions are fenced to the device that hosts them.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-162 / pademu assumes every SIO pad transfer is at least a full command long — short transfers overrun `in`/`out`

- **Category:** 6
- **Locations:** `modules/pademu/pademu.c:280` and `:366` (`td->in_size - 3` scan bounds), `:375` (`cmd_size + 3 == td->in_size` not-found test), `:421-553` (`pademu_cmd`: unconditional `out[0..2]`, `in[1]`, `in[3]`, writes through `out[8]` at `:449,:540`, 6-byte copies to `&out[3]`), `:555-589` (`pademu_mtap`: `td->out[6]` at `:582` after a 6-byte copy at `:580`, no `out_size` test anywhere)
- **Problem:** Nothing in the dispatch validates a minimum transfer size. `pademu_cmd`'s parameter is even named `out_size` but is only ever used as `out_size - 3` for the `get_data` call (the >18 direction of that call is RIPTOPL-106, already filed — not repeated here); the function's own header writes and command-argument reads (`in[3]`, `in[3+i]` for `i` up to 5, `pad[port].mask` reads at `in[3..6]`) proceed for a transfer of any size, including a 1–3-byte SIO transaction from a game probing the port. The "second command" scans compute `td->in_size - 3` on a `u32` (`pademu.h:47`), so an `in_size < 3` wraps the bound to ~4 billion and the scan reads `td->in[]` far out of bounds; the not-found exit test `cmd_size + 3 == td->in_size` also cannot fire for sizes the scan skipped. `pademu_mtap` likewise writes `td->out[6]` without reference to `td->out_size`. Real DS2 traffic is ≥9 bytes, which is why this survives; the sizes here come from the game's SIO2MAN transfer descriptor, not from a trusted constant.
- **Fix:** Early-out in `pademu`/`pademu_cmd`/`pademu_mtap` when the transfer is smaller than the command being answered needs (and guard the scan bound against `in_size < 3`). Justification: a handful of size tests at the entry points is the minimum correct shape.
- **Benefit:** Short/probing SIO transactions from a game can no longer make the pad emulator read or write outside the transfer buffers it was handed.
- **Effort:** S · **Risk:** Low · **Confidence:** Mechanism High (READ); that a shipped game issues sub-9-byte pad transfers is INFERRED (none identified in-tree)

### RIPTOPL-163 — `scsi_warmup` accepts any non-zero sector size a device reports; the size prints can divide by zero and every transfer length is `sectorSize × count` in 32 bits

- **Category:** 6 (device-supplied value trusted as divisor/multiplier)
- **Locations:** `modules/usb/usbmass_bd/src/scsi.c:243-249` (READ_CAPACITY(10) block length stored; only `== 0` rejected), `:259-261` (READ_CAPACITY(16) block length adopted on a `!= 0` test only), `:253-254` and `:266-267` (capacity prints), `:156` and `:174` (`bd->sectorSize * sectorCount` as the CBW transfer length in `scsi_cmd_rw_sector`)
- **Problem:** READ for the arithmetic. The device-supplied block length is trusted after a single zero check. (1) Both warmup prints compute `sectorCount / ((1000*1000) / sectorSize)` and the `(1024*1024)` twin: for any reported sector size above 1,000,000 the inner division yields 0 and the outer one divides by zero (on the IOP's MIPS core that yields an undefined quotient rather than a trap — INFERRED; the certain impact is garbage diagnostics from live `M_PRINTF` code). (2) The same value multiplies every READ/WRITE transfer length at `:156`/`:174` in 32-bit arithmetic: a device reporting e.g. 0x20000-byte blocks wraps the product for payloads above 32 KiB, so CBW length, BDM buffer expectation and sector count disagree for the life of the device. The in-file comment at `:246-248` shows the zero case was a deliberate local fix — it stops one step short.
- **Fix (words):** Extend the existing check at `:246` from `== 0` to a plausibility test — sector size a power of two in [512, 4096] (every real USB mass-storage block size) — and apply the same test to the capacity-16 value before adopting it at `:261`. *Justification (adds a check): one condition in each of two places closes a divide-by-zero and a per-transfer length wrap driven entirely by device-supplied data.*
- **Benefit:** A broken or hostile device is rejected at warmup instead of corrupting transfer-length arithmetic on every subsequent I/O.
- **Effort:** S · **Risk:** Low (a hypothetical >4096-byte-block device would be rejected; the current code cannot serve one correctly anyway) · **Confidence:** High on the unchecked arithmetic (READ); Medium on real-world trigger (needs a misreporting device).

### RIPTOPL-164 / ps2link exception handler overruns its own buffer and can `strlen(NULL)` — in the crash handler itself

- **Category:** 6 (bug/security)
- **Locations:** `modules/debug/ps2link/excepHandler.c:12` (`BUFFER_SIZE` definition), `:14` (`excep_buffer`), `:71-95` (`excep_handler2`; `strlen` at `:80`, `memcpy` at `:86`, send at `:94`)
- **Problem:** `BUFFER_SIZE = sizeof(exception_frame_t) + 136`, and `excep_buffer` holds exactly that many bytes. `excep_handler2` then writes: 4 bytes of magic, `memcpy(buffer, frame, BUFFER_SIZE)` — which both **over-reads** `frame` by 136 bytes (the source object is only `sizeof(exception_frame_t)`) and writes `BUFFER_SIZE` bytes starting at offset 4 — then 8 more bytes (`r_epc`, `len`), then `len + 1` bytes of module name. Total written is `sizeof(exception_frame_t) + 149 + len` bytes into a `sizeof(exception_frame_t) + 136`-byte static: an overrun of at least 13 bytes plus the module-name length, into whatever statics follow in BSS, executed precisely when the IOP has already faulted. Independently, `ExceptionGetModuleName` returns `0` when the faulting EPC belongs to no listed module (BIOS ROM, freed module, data-as-code), and `:80` calls `strlen(module_name)` on it unconditionally — a second fault inside the exception handler. Debug-only reachability (ps2link), but this is the component whose only job is to survive a crash.
- **Fix:** Size the copy to `sizeof(exception_frame_t)`, enlarge the buffer by the header/trailer/name space the format actually uses, and treat a NULL module name as an empty string. Justification for added code: the two guards are the fix itself; there is no smaller correct shape.
- **Benefit:** The IOP exception reporter stops corrupting memory and re-faulting on exactly the crashes it exists to report.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — the arithmetic closes from the file alone, for any `sizeof(exception_frame_t)`)

### RIPTOPL-165 / ps2link `pkoSendSifCmd` copies up to 4 bytes past `rpc_data[1024]` on a full-size command packet

- **Category:** 6
- **Locations:** `modules/debug/ps2link/cmdHandler.c:41` (`rpc_data[1024/4]`), `:137-153` (`pkoSendSifCmd`; `memcpy` at `:143-144`, length clamp at `:146`); packet source `cmdListener` at `:235-240` (`recvfrom` into a 1,024-byte `recvbuf`)
- **Problem:** The clamp is `len > sizeof(rpc_data) ? sizeof(rpc_data) : len` — i.e. up to 1,024 bytes — but the copy destination is `&rpc_data[1]`, offset 4 into the 1,024-byte array. Any command packet with `len ≥ 1021` (a 1,024-byte UDP datagram is the maximum `cmdListener` can deliver, and is all it takes) writes 1–4 bytes past the array, and the final bytes of the source never reach the EE because the DMA length stays `len`. Every EE-side command (EXECEE, WRITEMEM, DUMPMEM, RESET…) funnels through this one function. Debug-only reachability.
- **Fix:** Clamp the copy and the DMA length to `sizeof(rpc_data) - 4` (the space actually remaining after the command word). Justification: one constant change in the existing clamp, no new code paths.
- **Benefit:** Removes a static-buffer overwrite on a network-supplied length and stops silent truncation of the longest commands.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-166 / ps2link `pkoExecIop` walks argument strings with a network-supplied `argc` and no bound

- **Category:** 6
- **Locations:** `modules/debug/ps2link/cmdHandler.c:51-115` (`args` at `:74`, `argc = ntohl(...)` at `:75`, walk at `:78-81`, `LoadStartModule` at `:83`)
- **Problem:** `argc` is taken from the UDP packet as a full `u32` and never checked against the packet. The loop `for (i = 0; i < (argc - 1); i++) arglen += strlen(&args[arglen]) + 1;` therefore walks NUL-hunting past the 256-byte `argv` field and past the 1,024-byte `recvbuf` into adjacent memory for any large `argc` — and for `argc == 0`, `argc - 1` is `UINT_MAX`, so it walks essentially forever. The resulting `arglen` is handed to `LoadStartModule` as the argument block length. The packet-length check at the top validates only the header struct size, not the string area the loop consumes. Debug-only reachability, but the walk is unbounded in both count and bytes.
- **Fix:** Bound the loop by the bytes actually remaining in the received packet (stop when `arglen` reaches the end of `argv`/packet, and reject `argc == 0` / implausibly large `argc` up front). Justification: the bound check is the minimum that makes the existing parse safe; no restructure.
- **Benefit:** A malformed EXECIOP packet returns an error instead of reading arbitrary IOP memory and feeding a garbage length to the module loader.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-167 / ps2link `pko_read_file` trusts the peer's `nbytes` over the caller's buffer size

- **Category:** 6
- **Locations:** `modules/debug/ps2link/net_fio.c:363-412` (`nbytes = ntohl(readrly->nbytes)` at `:402`, `pko_recv_bytes(..., &buf[0], nbytes)` at `:408`); caller `modules/debug/ps2link/net_fsys.c:139-156` (`fsysRead` passes the ioman caller's `buf`/`size`)
- **Problem:** `length` (the caller's buffer capacity) is sent in the request but never compared with the reply's `nbytes`. A peer that answers with `nbytes > length` — a buggy or hostile ps2link host, or a desynchronised stream where the "reply" is actually file data — makes `pko_recv_bytes` write that many bytes into a buffer sized for `length`. Every other reply field in this file is treated as untrusted enough to byte-swap and log; this one is used as a write length unchecked. Debug-only reachability (the peer is normally the developer's own PC).
- **Fix:** Reject (or clamp-and-drain) a reply whose `nbytes` exceeds the requested `length` before receiving the payload. Justification: one comparison against a value already in scope.
- **Benefit:** The host: filesystem cannot be turned into a heap/stack overwrite of the reading program by a single corrupt reply packet.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-168 / ziso.py: decompression trusts the ZSO header — block size, alignment and index length are unvalidated

- **Category:** 6 (unchecked lengths/counts from file data)
- **Locations:** tools/pc/ziso.py:107-115 (`read_zso_header`), tools/pc/ziso.py:135-141 (only checks: magic, `block_size == 0`, `total_bytes == 0`, `header_size != 24`, `ver > 1`), tools/pc/ziso.py:160 (`index << align`)
- **Problem:** Nothing requires `block_size` to be a multiple of 2048 / power of two (the compress side enforces a multiple of 2048 for its own input), bounds `align` (a *signed* header byte), checks the index read actually returned `4 * index_count` bytes, or compares `total_bytes` with the real file size. Executed against crafted files on a /tmp copy: (a) a <24-byte file dies with an uncaught `struct.error` in `read_zso_header`; (b) `block_size=1, total_bytes=1000000` dies with an uncaught `struct.error: unpack requires a buffer of 4000004 bytes` at :141 — with a larger claimed total the same line first attempts an index allocation proportional to `total_bytes/block_size`; (c) header `align=-1` dies with an uncaught `ValueError: negative shift count` at :160.
- **Fix:** After the existing header check, reject a non-2048-multiple or out-of-range `block_size`, an `align` outside 0..~6, and a short index read, with the same "ziso file format error" exit the existing checks use. *Justification for adding checks:* the values come from an untrusted file header and currently reach an allocation size and a shift count unchecked; the checks are the smallest change that keeps the failure on the tool's own error path.
- **Benefit:** Malformed ZSOs produce the tool's own one-line format error instead of tracebacks, a hang-adjacent giant allocation, or a crash mid-write of the output ISO (the output file is already open and partially written when these fire).
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ + executed on /tmp copies.

### RIPTOPL-169 / Volume-index guards use `>` instead of `>=` (and miss negatives), admitting a one-past-the-array access

- **Category:** 6
- **Locations:** `modules/hdd/apa/src/hdd_blkio.c:211-227` (`hdd_blkio_vhdd_validate`: `if (slot > BLKIO_MAX_VOLUMES)` at `:216`, then `&hdd_blkio_vhdd_diskinfo[slot]` at `:221`; write side `hdd_blkio_vhdd_mount` stores `diskinfo->fd` via the same index at `:251` and indexes `hddDevices[slot]` at `:259-272`); sibling guard `modules/bdm/bdmfs_fatfs/src/fs_driver.c:137-145` (`if (mount_info_index > FATFS_FS_DRIVER_MOUNT_INFO_MAX)` at `:140`, array read at `:143`); negative side `modules/hdd/apa/src/hdd_blkio.c:98-106` (`>=` checked at `:101`, negative `device` not checked before `fs_driver_mount_info[mount_info_index]` at `:104`).
- **Problem:** In the non-ATAD build `BLKIO_MAX_VOLUMES` is 10 (`hdd_blkio.h:65`), so valid slots are 0–9, but `slot == 10` passes validation and indexes one entry past both static arrays — in `vhdd_mount` that is an out-of-bounds **write** (`diskinfo->fd = fd`). The fatfs helper repeats the slip for `FF_VOLUMES` = 10 volumes (index 10 reads one past `fs_driver_mount_info`). The unit/slot values originate from the ioman unit field via `hdd_fio.c`'s mount path (`hdd_blkio_vhdd_mount(f->unit, …)`), i.e. from the device string a caller asks to mount. The BDM-side getter additionally accepts negative indices. Each site is one comparison away from the guard its neighbour already writes correctly (`hdd_blkio.c:101` uses `>=`).
- **Fix (words only):** Change both `>` guards to `>=` and add `index < 0` rejection in the getters/validator. Justification: pure bounds checks on indices that reach static arrays.
- **Benefit:** Closes a one-slot out-of-bounds read/write in three mount/transfer entry points for the cost of three comparisons.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the arithmetic (READ); Med for reachability of `unit == 10` from a real mount request (INFERRED from the `hdd_fio.c` caller, which belongs to a prior round's file).

### RIPTOPL-170 / `fs_ioctl2` `USBMASS_IOCTL_GET_LBA` dereferences a NULL block device — the one sibling case the in-file fix missed

- **Category:** 6
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:879-891` (`bd = fatfs_fs_driver_get_mounted_bd_from_index(file->obj.fs->pdrv);` at `:888`, dereferenced as `bd->sectorOffset` at `:890` with no NULL test).
- **Problem:** The two neighbouring cases in the same switch were already fixed for exactly this hazard: `USBMASS_IOCTL_GET_DRIVERNAME` (`:893-902`, with a comment recording that the SDK version faulted when the device had gone away) and `USBMASS_IOCTL_GET_DEVICE_NUMBER` (`:912-930`) both NULL-check the same lookup. `GET_LBA` checks only its output buffer (`:882-886`) and then dereferences the possibly-NULL `bd`. A file still open on a volume whose device was disconnected (USB removal between open and ioctl2 — the precise scenario the sibling comment describes) faults the IOP instead of returning `-ENXIO`. `get_frag_list` in the same file (`:756`) also checks the identical lookup, making `GET_LBA` the lone exception in the file.
- **Fix (words only):** Give `GET_LBA` the same `bd == NULL → -ENXIO` guard its siblings use. Justification: adds the check the file's own established pattern requires.
- **Benefit:** Device removal during an open file's lifetime returns an error instead of crashing the filesystem driver.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (all four call sites of the lookup in this switch/file compared directly).

### RIPTOPL-171 / BDM VMC exports dereference `g_bd` with no NULL check — the only unguarded `g_bd` uses in the file

- **Category:** 6 (bugs/security)
- **Locations:** `modules/iopcore/cdvdman/device-bdm.c:279` (`bdm_readSector`: `rv = g_bd->read(...)`), `:302` (`bdm_writeSector`: `rv = g_bd->write(...)`); contrast `:229-230` (`DeviceReadSectors` returns `SCECdErTRMOPN` when `g_bd == NULL`), `:192`, `:200-208`, `:183-197` (all other uses guarded).
- **Problem:** `g_bd` is set to NULL by `bdm_disconnect_bd` and starts NULL. mcemu reaches these two functions through the oplutils imports (`modules/mcemu/mcemu_utils.h:44-48`) as its VMC page backend, and their documented contract (file comment at `:259-266`) is "return 1 on full transfer, 0 otherwise" so mcemu can report a failed page instead of acknowledging a lost save. A call made before attach or after a disconnect instead dereferences a NULL function pointer and crashes the IOP — the failure mode the contract exists to avoid. Every other `g_bd` consumer in the same file guards; these two are the exception.
- **Fix:** Return 0 when `g_bd == NULL` at the top of both functions (before taking the semaphore is fine; the disconnect path nulls it under the same semaphore discipline the functions already use). Justification: one comparison per function, reusing the guard idiom the file already uses at `:229`.
- **Benefit:** A VMC access racing a device detach fails as a reported page error instead of an IOP crash.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-172 / iLinkTrFree / iLinkGetNodeTrSpeed / iLinkSetNodeTrSpeed index TransactionContexts with no range check — in both copies / Category 6

- **Locations:** `modules/ilink/iLinkman/src/iLink.c:209-212` (`iLinkTrFree`), `:330-343` (`iLinkGetNodeTrSpeed`), `:345-360` (`iLinkSetNodeTrSpeed`); assault twins at `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink.c:213`, `:334`, `:349`. Array: `TransactionContexts[10]` (`MAX_CONCURRENT_TRANSACTIONS`, iLink_internal.h:280 in both trees).
- **Problem (READ):** The same file's `iLinkTrRead`/`iLinkTrWrite` were hardened to reject `trContext < 0 || trContext >= MAX_CONCURRENT_TRANSACTIONS` (ilink :219-221, :243-245), but the other three exported entry points that take a `trContext` were not: `iLinkTrFree` writes `TransactionContexts[trContext].IsConnected = 0`, `iLinkSetNodeTrSpeed` writes `.speed`, and `iLinkGetNodeTrSpeed` reads `.speed`, all at a caller-supplied index with no check. These are iLinkman library exports, so any IOP module linked against it — not just sbp2, whose indices come from `iLinkTrAlloc` and are safe — can drive an out-of-bounds read/write in IOP memory. Present identically in both copies; never filed (grep of rounds 1-4 for these symbols: no hits).
- **Fix:** Apply the range check the file already uses in TrRead/TrWrite to the three functions (early return / -1002 as each signature allows). Justification: adds a check, but it is a verbatim copy of the check already established three functions away in the same file — consistency, not new policy.
- **Benefit:** Closes the remaining unchecked-index half of the exported transaction-context API in both driver builds.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ. (Severity framing: local IOP callers only; no remote trigger was found in the read — this is an exported-API robustness gap, not a network hole.)

### RIPTOPL-173 / imgdrv's `read_fs` has no offset and no bound against the image size it serves

- **Category:** 6
- **Locations:** `modules/iopcore/imgdrv/imgdrv.c:9-10` (`ioprpimg`/`ioprpsiz` patch points), `:20-27` (`lseek_fs`), `:29-33` (`read_fs`)
- **Problem:** `read_fs` is `memcpy(buffer, (void *)ioprpimg, size); return size;` on every call: it ignores the file position entirely and never compares `size` with `ioprpsiz`. A caller that reads in chunks gets the image's first chunk repeatedly; a caller that asks for more than the image (or more than remains) is silently handed bytes from whatever IOP memory follows the image, reported as a successful full read. `lseek_fs` compounds it: `SEEK_END` returns `ioprpsiz` regardless of the requested offset, everything else returns 0 regardless. The driver survives because its one known consumer reads the image exactly the way the implementation assumes (size query, then one full read) — a contract that exists nowhere except in this code's shape, and that round 4 already saw ee_core defensively working around from the other side (the magic-word scan is bounded and refuses when patch points are missing; nothing bounds the read side).
- **Fix:** Track the position in the file descriptor (or honour the `iop_file_t` offset), and clamp each read to `ioprpsiz - position`, returning 0 at end of image. Justification: position+clamp is the smallest change that makes the driver's answers true for any caller.
- **Benefit:** The IOPRP image driver stops disclosing unrelated IOP memory and mis-serving any consumer that reads incrementally.
- **Effort:** S · **Risk:** Low · **Confidence:** Mechanism High (READ); multi-chunk/oversized reachability INFERRED (the in-tree consumer's single-read pattern is the mitigating fact)

### RIPTOPL-174 / System-data icon.sys is written 1776 bytes from a 964-byte icon array

- **Category:** 6 (bug/security — out-of-bounds read)
- **Locations:** src/core/OSDHistory.cpp:27 (`SONY_SYSDATA_ICON_SYS_SIZE = 1776`), src/core/OSDHistory.cpp:40-59 (`CreateSystemDataFolder`, write at :59); source arrays built from misc/icon_A.sys, misc/icon_J.sys, misc/icon_C.sys (964 bytes each, READ via file size at the pin)
- **Problem:** All three region icons are 964-byte files, but the folder creator writes 1776 bytes from the array — 812 bytes past its end, copying whatever `.rodata` follows it into the memory card's icon.sys. The in-code comment acknowledges writing "past the end of the icon" (the 1776 size is what HDDOSD demands), but the padding content is uninitialised-for-this-purpose adjacent data, not zeros, and the read itself is UB that a different link order can change silently.
- **Fix:** Write the real 964 bytes, then write the remaining 812 bytes from a small zero buffer, keeping the on-card file exactly 1776 bytes.
- **Justification (fix adds a pad write):** it replaces an 812-byte out-of-bounds read with defined content while preserving the deliberate 1776-byte file size the OSD requires — net behaviour on the card is identical-or-cleaner.
- **Benefit:** Removes an OOB read whose payload varies with link layout; icon.sys padding becomes deterministic zeros.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — constant, write site, and all three source file sizes verified)

### RIPTOPL-175 / `readFileBuffer` memcpy's into an unchecked `malloc` on the language-file path / Category 6

- **Locations:** `src/platform/util.cpp:631-634`.
- **Problem:** In the `allocResult` branch, `*outBuf = (char *)malloc((lineSize + 1) * sizeof(char));` is followed immediately by `memcpy(*outBuf, fileBuffer->lastPtr, lineSize + 1);` with no NULL check — a failed allocation on a fragmented EE heap is a NULL-pointer write of up to a full line, inside the parser loop. This branch is live: `src/core/lang.cpp:163` opens language files with `openFileBuffer(path, O_RDONLY, 1, 1024)` (allocResult = 1), so every `.lng` line goes through it. Every neighbouring allocation in this same file *is* checked (`openFileBuffer` :473-487, `openFileBufferBuffer` :527-534, `readFile` :352-357), making this the one inconsistency.
- **Fix:** Add the NULL check the rest of the file already uses: on failure, return the line unallocated (or 0) instead of copying. Justification for adding a check: one comparison on a cited crash path, following the file's own established pattern; no new mechanism.
- **Benefit:** One crash-on-low-memory path removed from language loading.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ, including the live caller).

### RIPTOPL-176 / Theme-element initialisers dereference unchecked allocations throughout themeelems.cpp

- **Category:** 6 (bugs/security — unchecked malloc)
- **Locations:** `src/gfx/themeelems.cpp:211-212,226-227` (`initMutableText`: `malloc` of the struct is dereferenced on the next line; `malloc` of `value` is `memcpy`'d unchecked), `:275-279` (`calloc` of `alias` is passed straight to `snprintf`), `:703-704` (`initImageTexture`: `calloc` result dereferenced immediately), `:767-768` (`initImageInternalTexture`: same), `:810-811` (`initMutableImage`: `malloc` dereferenced immediately), `:1608-1610` (`initBasic`: `malloc` dereferenced immediately), `:2123-2131` (`initItemsList`: `malloc` written through by `itemsListFit` unchecked)
- **Problem:** Every element constructor in this file allocates and dereferences without a NULL check, so an allocation failure during theme load — on a 32 MB console whose theme textures compete for the same RAM — is an immediate NULL dereference crash instead of a disabled element. The file itself proves the intended pattern twice: `initGameCountText` (`:509-518`) checks both of its allocations and degrades to a disabled element (its comment even cites the OOM case), and `thmParseDeviceList` (`:79-81`) checks its `malloc`. The seven sites above are simply the ones that were never given the same treatment. READ.
- **Fix (words only):** Add the same NULL checks `initGameCountText` already uses at each site: on failure return NULL / leave `elem->extended` unset so the element stays disabled, and free any partially built struct. Justification: the added checks are the only thing standing between an OOM during theme load and a hard crash; they convert it into the file's existing disabled-element path.
- **Benefit:** Theme load survives low memory instead of crashing the GUI.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-177 / `apaJournalRestore` dereferences a possibly-NULL cache allocation

- **Category:** 6
- **Locations:** `modules/hdd/apa/src/journal.c:72` (`clink = apaCacheAlloc();`), used unconditionally at `:74` (`clink->header`) and `:82` (`apaCacheFree(clink)` — which itself NULL-guards, the loop does not).
- **Problem:** `apaCacheAlloc` returns NULL when the cache pool is exhausted — the contract is demonstrated by checked call sites elsewhere in the driver (e.g. `apa.c:58`, cited in round 4). Restore runs not only at first probe but on every BDM/vhdd mount (`hdd_blkio.c:121,263`), i.e. at a moment when another device's headers can hold the small (default 3-entry) pool. NULL here turns the first journal read into a DMA transfer to a near-NULL buffer and an IOP crash during device probing. This is the same defect pattern round 4 filed for `apaGetFormat` in `apa.c` (RIPTOPL-076), in a file/function round 4 did not read — not a re-file.
- **Fix (words only):** NULL-test `clink` after `:72`; on NULL skip replay, still reset the journal, return `-ENOMEM`. Justification: adds one check on an allocation whose NULL contract the same driver already honours elsewhere.
- **Benefit:** Cache pressure at mount time fails one device's journal replay instead of crashing the IOP.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-178 / MBR/GPT connect paths accept a zero/short sector read as success and parse the scratch buffer anyway

- **Category:** 6
- **Locations:** `modules/bdm/src/part_driver_gpt.c:48-49` (header read), `:103-104` (entry-sector reads), `modules/bdm/src/part_driver_mbr.c:60-61` (MBR read) — all test only `ret < 0`.
- **Problem:** Every block-device read in this stack returns a **sector count** on success (the convention the glue layers enforce explicitly, e.g. `diskio.c`: `res == count ? RES_OK : RES_ERROR`, and `bd_cache.c`'s refill fix, which was written because a short read once passed stale bytes off as the requested sector). These three sites treat a return of 0 — nothing read — as success. The header cases are mostly saved by their signature checks against uninitialised `AllocSysMemory` scratch. The GPT **entry-sector** case is not: a 0-return re-parses the previous sector's buffer under advanced indices, registering duplicate pseudo-partitions for ranges already mounted (a second mount of the same filesystem volume, with independent write paths to it).
- **Fix (words only):** Change the three tests to `ret != 1` (freeing the scratch buffer on the failure path, as today). Justification: aligns these reads with the return-count contract the rest of the same stack already enforces.
- **Benefit:** A flaky device at mount time fails the partition scan instead of double-mounting a volume from stale bytes.
- **Effort:** S · **Risk:** Low · **Confidence:** Med, INFERRED (the count-return contract is READ from `diskio.c`/`bd_cache.c`; no in-tree device driver was proven to return 0 on failure — the defect is that these sites would not notice if one did).

### RIPTOPL-179 — The unplug NULL-priv guard covers scsi warmup commands but not `scsi_cmd_rw_sector` or the read/write chunk loops

- **Category:** 6 (guard applied to one half of a guarded set; race)
- **Locations:** guards present at `modules/usb/usbmass_bd/src/scsi.c:78` (`scsi_cmd`), `:129` (`scsi_cmd_read_capacity16`), `:193`/`:230` (warmup); absent at `:139-176` (`scsi_cmd_rw_sector` dereferences `scsi->queue_cmd` at `:156`/`:174` with `scsi` taken unchecked from `bd->priv` at `:141`) and at `:287` / `:330` (`scsi_read`/`scsi_write` read `scsi->max_sectors` from a possibly-NULL priv on every chunk); priv is nulled on disconnect at `:416` (`scsi_disconnect`)
- **Problem:** READ for the asymmetry; INFERRED for concurrent reachability. The file's own comments (`:78` "unplugged during start-up", `:397` "not unplugged during start-up") show the maintainers did not rely on BDM serialising disconnect against in-flight work during warmup — but the data path re-checks nothing: if a disconnect lands while a multi-chunk read/write is in flight, the next chunk dereferences NULL instead of failing with −EIO. Whether BDM serialises disconnect against in-flight I/O is not established in-tree; if it does, this is latent.
- **Fix (words):** Mirror the existing guard: return −1 from `scsi_cmd_rw_sector` when `bd->priv == NULL` (same shape as `:78`), which also covers both chunk loops since every chunk funnels through it; and test the captured priv once at `scsi_read`/`scsi_write` entry. *Justification (adds checks): two token-level guards copied from the same file's own pattern.*
- **Benefit:** The data path gets the same unplug behaviour the warmup path already promises.
- **Effort:** S · **Risk:** Low (a NULL priv can only mean the device is gone) · **Confidence:** Medium — asymmetry READ, exploitability INFERRED (depends on BDM threading).

### RIPTOPL-180 / UDPBD takes its block-device geometry from the INFO reply with no sanity check

- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smap_udpbd/src/udpbd.c:307-316` (`_cmd_info_reply`); geometry consumers `:93` (`g_read_size = count * g_udpbd.sectorSize`), `:161-162, 201-202` (bounds arithmetic).
- **Problem:** On the first INFO_REPLY, two raw FIFO words become `g_udpbd.sectorSize` and `g_udpbd.sectorCount` and the device is immediately connected to BDM. Nothing validates them: a sectorSize of 0 makes every read compute `g_read_size = 0` and wait out its alarm as a timeout (a permanently erroring device, not a refused one); a non-512 sectorSize desynchronises the write path, which hardcodes 512-byte RDMA payloads (`:230-239`); an inflated sectorCount advertises capacity the server does not have. The reply is accepted from any LAN sender whose packet carries the current `g_cmdid` — a 3-bit rotating value (`:90, 204`, masked `& 0x7`) that starts at 0, the value the initial INFO exchange uses. This file's RDMA receive path was already hardened against wire-controlled sizes (the comment at `:353-356` says exactly that); the geometry that frames all of it was not.
- **Fix:** In `_cmd_info_reply`, refuse to connect unless sectorSize is 512 (the only size the rest of the driver supports) and sectorCount is non-zero.
- **Justification (adds a check):** two comparisons at the single point geometry enters the driver; conforming servers are unaffected.
- **Benefit:** A stray, spoofed or corrupt INFO reply can no longer register a nonsense block device with FatFs.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ; the spoofability leg is INFERRED from the 3-bit cmdid and broadcast design, the missing validation is READ)

---

### RIPTOPL-181 / opl2iso takes the output filename from raw ul.cfg bytes — a name containing `/` or `..` writes outside the CD/DVD folder

- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/opl2iso/src/opl2iso.c:227-229` — `snprintf(rname, 255, "%s/%s.%s.iso", mediatype, temp_ID, grecord.name);` then `fopen(rname, "wb")` (`:235` area), where `grecord.name` is 32 raw bytes read from ul.cfg by `findGame` with no character screening.
- **Problem:** ul.cfg collections circulate with games; a record name of the form `../../something` (or containing an absolute-path fragment after the media prefix) makes the exporter truncate/create a file outside the intended `CD/`/`DVD/` directory. The `snprintf` bounds the length, not the meaning.
- **Fix:** Reject (or sanitise) a record name containing `/`, `\` or a leading `.` before building `rname`.
- **Justification (fix adds a check):** the name is file-format data, not a path; screening separators is the smallest change that keeps it one.
- **Benefit:** exporting a downloaded ul.cfg collection can't write outside the export folders.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (formatting and open sites read in full; exploitation needs a hostile ul.cfg, hence Low severity weighting).

### RIPTOPL-182 / A 32-character game name is stored unterminated in ul.cfg, and the duplicate check then compares past the field

- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/iso2opl/src/iso2opl.c:520` (main rejects only `strlen(argv[3]) > 32`), `:159` (`strncpy(cfg.name, game_name, 32)` — no terminator when the name is exactly 32 chars), `:121` (`check_cfg`'s `strcmp(cfg.name, game_name)`).
- **Problem:** The tool's own maximum-length name produces a record whose `name[32]` has no NUL. `check_cfg`'s `strcmp` then reads on into `image`, `parts`, `media` and `pad`, so a reinstall of the same 32-char game can never match on name (the comparison includes the `ul.…` image bytes) — only the image-ID check still catches it. For a *foreign* ul.cfg with no zero byte anywhere in the trailing 32 bytes, the same `strcmp` reads past the 64-byte stack record entirely. (For records write_cfg itself made, `pad[0]` is zero from its `memset`, bounding the over-read inside the record.)
- **Fix:** Reject a 32-char name at install time (limit 31 + terminator) or compare the name field with `strncmp(..., 32)` in `check_cfg`.
- **Justification (fix adds a bound):** the field is fixed-width on disk; string functions are the wrong tool for it, and the bound is the fix.
- **Benefit:** duplicate detection works at the documented maximum name length; foreign ul.cfg records can't drive a stack over-read.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-183 / iso2opl builds every output path with `sprintf` into 256-byte buffers from an unbounded drive argument

- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/iso2opl/src/iso2opl.c:88-92` (`check_cfg`), `:151-155` (`write_cfg`), `:219-223` (`write_parts`) — `sprintf(cfg_path / part_path, "%s/…", drive, …)` into `char cfg_path[256]` / `char part_path[256]`; `main` (`:520`) validates only `argv[3]`'s length (the game name), never `argv[2]` (the drive/path), `argv[1]`, or `GameID`.
- **Problem:** A destination path argument longer than ~240 chars overflows a stack buffer in three functions before any file is touched. The user's own command line is the input, so severity is low — but long install paths (deep SMB shares, the `\\MyComputer\PS2SMB` form the usage text itself advertises) are a legitimate shape for this argument.
- **Fix:** Convert the five `sprintf` path builds to `snprintf(..., sizeof(buf), ...)` and reject a truncated result; opl2iso in the same tree already uses `snprintf` for its output name.
- **Justification (fix adds bounds):** path length here is externally supplied; bounded formatting is the minimal change.
- **Benefit:** long-path installs fail cleanly instead of corrupting the stack.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-184 / Assault BuildConfigurationROM publishes an uninitialized VendorID quadlet / Category 6

- **Locations:** `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_crom.c:295-307` (assault) vs `modules/ilink/iLinkman/src/iLink_crom.c:295-304` (ilink)
- **Problem (READ):** The ilink copy zeroes the freshly malloc'd CROM buffer (`memset(CROM_Buffer, 0, CROMSize)`, ilink :298) and assigns `RootDirectory->VendorID = (IEEE1394_CROM_VENDOR << 24) | 0x80046` (ilink :304). The assault copy does neither: no `memset` anywhere in its `BuildConfigurationROM`, and of the five `Root_Directory` fields (its own `iLink_CROM.h:40-47`) it assigns only four — `VendorID` is never written. The unwritten quadlet is then byte-swapped in place (`ieee1394Swab32(...Root_Directory...)`, assault :307) and covered by the directory CRC16 (assault :318-320), so the published Configuration ROM is self-consistent but its vendor entry is whatever `AllocSysMemory` returned — heap garbage, different every build/boot, served to any external 1394 node that reads the root directory. Every other struct in the function is fully assigned, so VendorID is the only garbage field; the missing memset is what makes the omission silent.
- **Fix:** Port the ilink copy's two lines to the assault copy: zero the buffer after the malloc succeeds, and assign `VendorID` before the swab. Justification: adds nothing new — it restores the two lines the twin copy already has; no new logic.
- **Benefit:** The assault driver's advertised vendor identity becomes the intended Sony OUI entry instead of nondeterministic memory contents.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (assignment-by-assignment comparison of both function bodies against the assault header's struct definition).

### RIPTOPL-185 — Three MMCE command builders transmit uninitialised stack bytes as packet padding

- **Category:** 6 (uninitialised memory sent on the wire)
- **Locations:** `modules/mmce/mmceman/src/mmce_cmds.c:109-121` (`mmce_cmd_set_card`: `wrbuf[0x8]`, indices 0–6 written, byte 7 sent via `sizeof(wrbuf)`), `:169-180` (`mmce_cmd_set_channel`: `wrbuf[0x7]`, indices 0–5 written, byte 6 sent), `:263-271` (`mmce_cmd_reset`: `wrbuf[0x5]`, indices 0–2 written, bytes 3–4 sent)
- **Problem:** READ. `mmce_sio2_tx_rx_pio` copies exactly `tx_size` bytes from the buffer (mmce_sio2.c:203-205), and these three builders pass `sizeof(wrbuf)` while leaving the tail bytes unwritten — whatever was on the IOP stack goes to the card as packet padding. The sibling `mmce_cmd_ping` (`:14-23`) explicitly zeroes its trailing bytes, which is in-file evidence the convention is zeroed padding, not don't-care. Impact is bounded (the card firmware most likely ignores trailing bytes — INFERRED), but the bytes are nondeterministic and the leak is real.
- **Fix (words):** Zero-initialise the three arrays at declaration (`= {0}`), matching ping's explicit zeroing. No new code paths.
- **Benefit:** Packets are deterministic; no IOP stack content leaves the driver.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — write sets and transmit lengths are both in-tree).

### RIPTOPL-186 / iremsndpatch rewrites a module found by numeric ID alone — no identity or size check before eleven fixed-offset writes

- **Category:** 6
- **Locations:** `modules/iopcore/patches/iremsndpatch/main.c:54-118` (`strtol(argv[1], NULL, 16)` at `:64`, list walk by `m->id` only at `:70-77`, pointer reconstructed from two words at `:87-89`, writes at `:94-108` up to `text_start + 0x1ff8`)
- **Problem:** The patch locates its target purely by the module ID passed in argv, then dereferences a thread-ID pointer assembled from a `lui/addiu` pair read at fixed offsets, and writes eleven words at fixed offsets up to `0x1FF8` into `text_start` — without checking the module's name, version, or `text_size`. If the ID is stale or the loaded IREMSND differs from the expected revision, every one of those writes lands in some other module's code (or past a smaller text segment), and the reconstructed pointer is trusted as an `int *` and later dereferenced by `postStopSeseqTick`. apemodpatch/f2techioppatch share the fixed-offset technique but at least load their target file themselves in the same `_start`, tying identity to the path ee_core chose; iremsndpatch's target arrives by number alone.
- **Fix:** Before writing, verify the located module is the expected one (name/version as ee_core identified it) and that `text_size` covers `0x1FFC`; refuse otherwise, as the file already refuses on a missing module. Justification: the check is the only thing standing between a wrong ID and arbitrary code corruption.
- **Benefit:** A mismatched module ID or an unexpected IREMSND revision becomes a clean no-patch instead of silent corruption of an unrelated module.
- **Effort:** S · **Risk:** Low · **Confidence:** Mechanism High (READ); how ee_core derives the ID it passes (context outside this section) is INFERRED to be name-based, which would make this defence-in-depth rather than a live path

### RIPTOPL-187 / ds34 EE clients copy `rpcbuf` to the caller even when the RPC failed

- **Category:** 6
- **Locations:** `modules/ds34bt/ee/libds34bt.c:127-141` (`ds34bt_get_data`, `memcpy` at `:138`); `modules/ds34usb/ee/libds34usb.c:131-144` (`ds34usb_get_data`, `memcpy` at `:142`) and `:74-91` (`ds34usb_get_bdaddr`, copy loop at `:88`, gated only by `ret &= rpcbuf[0]`)
- **Problem:** In both `get_data` implementations the 18-byte `memcpy(data, rpcbuf, 18)` runs unconditionally — the RPC result is computed into `ret`, the copy happens regardless, and only then is `ret` returned. A failed call therefore still overwrites the caller's pad-data buffer with stale bytes from the previous successful call (or zeroes at boot), and any caller that uses the buffer without checking the return — the pattern these thin wrappers invite — consumes phantom pad input. `ds34usb_get_bdaddr` does the same to the caller's address buffer; its `ret &= rpcbuf[0]` even reads the reply byte when no reply arrived. Sibling functions in the same files (`get_status`, `get_version`, `get_features`) correctly copy only inside the success branch, so the files disagree with themselves.
- **Fix:** Move the copies inside the `SifCallRpc(...) == 0` success path, matching the sibling functions in the same files. Pure rearrangement.
- **Benefit:** A dropped RPC leaves the caller's last real pad state untouched instead of replaying it as fresh input.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-188 / RetroGEM GameID reader seeks to disc-supplied LBAs with 32-bit offset arithmetic and no bound against the image size

- **Category:** 6
- **Locations:** `src/sources/retrogem.cpp:188` (`offset = reader->baseOffset + (long)lba * sectorSize;` in `retrogemReadSector`), fed by `src/sources/retrogem.cpp:346-347` (root-record LBA/size from the PVD) and `src/sources/retrogem.cpp:373-374` (SYSTEM.CNF file LBA/size from a directory record)
- **Problem:** The root-directory LBA and the SYSTEM.CNF extent LBA are read verbatim from the image being identified — i.e. from untrusted file/disc data (.VCD/.BIN/.CUE targets). On the EE, `long` is 32-bit, so `lba * 2048/2352` for a large crafted LBA wraps, and no value is ever checked against the actual image file's length before `fseek`. Memory stays safe (every read is a fixed 2,048-byte sector into a stack buffer), so the consequence is bounded — a wrapped seek reads the wrong sector and a crafted image can steer the parser to a forged SYSTEM.CNF elsewhere in the file, producing a wrong GameID/barcode for the RetroGEM display — but the parser validates every other field it consumes (record lengths, name lengths, sector types) and leaves this one unchecked.
- **Fix:** Reject an LBA whose byte offset cannot be represented / lies past the image length before seeking (one range check in `retrogemReadSector`, where layout and base offset are already known). Justification for adding a check: it is the single unvalidated disc-controlled offset in an otherwise fully validating parser, and the check is one comparison at one choke point.
- **Benefit:** A malformed or hostile image can only ever fail identification, never alias another part of the file as its SYSTEM.CNF.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the arithmetic (READ), INFERRED on impact (no memory unsafety found; impact is a wrong/absent GameID).

### RIPTOPL-189 / ratls compares HTTP transfer/content token values case-sensitively, while header names are matched case-insensitively

- **Category:** 6
- **Locations:** `src/platform/ratls.c:159` and `src/platform/ratls.c:171` (`!strncmp(value, "chunked", 7)`), `src/platform/ratls.c:430` (`strncmp(value, "identity", 8)`)
- **Problem:** `headerValue` matches header *names* case-insensitively (the `| 0x20` fold), but the *values* that decide the body framing are compared as exact lowercase. HTTP token values are case-insensitive (RFC 9110 §5.6.2 / RFC 9112 chunked coding), and servers do emit `Chunked`. For `raTlsRequest` the consequence is framing confusion, not a memory error: `responseComplete` treats a `Chunked` body as close-delimited and `finishResponse` hands the raw chunk-framed bytes to the RetroAchievements JSON parser, so the lookup fails for that server even though the response was well-formed. For `raTlsDownload` any `Transfer-Encoding` header is rejected outright, so the exposure there is only the `Content-Encoding: Identity` false-reject of an uncompressed body.
- **Fix:** Compare these three values with the same case-insensitive discipline `headerValue` already applies to names (a small local token-compare helper used at the three sites). Justification: this adds a helper, and it is the correctness condition for parsing a standards-conformant response; the alternative — accepting the body — is the failure being fixed.
- **Benefit:** HTTPS lookups and update downloads stop failing against servers that capitalise standard tokens.
- **Effort:** S / **Risk:** Low / **Confidence:** Med (mechanism READ; which deployed servers capitalise is INFERRED)

### RIPTOPL-190 / Partition notification dereferences `strchr(gOPLPart, ':') + 1` with no NULL check

- **Category:** 6
- **Locations:** `src/ui/guinotify.cpp:110-115` (`Notifications::show`, `showPart_` branch)
- **Problem:** `col_pos = strchr(gOPLPart, ':'); col_pos++;` — if `gOPLPart` ever contains no colon, `col_pos` becomes `(char *)1` and the following `snprintf(..., "%s", col_pos)` reads from address 1: an EE exception on the notification path. The three sibling popups in the **same function** (`showCfgPopup`, `showTheme_`, `showLang_`) all guard the identical `strchr` result with `!= NULL`; this branch is the only one that does not. Reachability today is low — every writer found stores an `hdd0:...` string — but `gOPLPart` is also populated from partition-candidate/config data and nothing enforces or documents the colon invariant at this consumer.
- **Fix:** Guard the result exactly as the sibling branches in the same function already do (skip or fall back to the whole string when there is no colon). Deletion-style fix: no new code pattern, the function's own idiom.
- **Benefit:** Removes a crash-on-bad-data path from a purely cosmetic popup.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the mechanism (READ — this function and all `gOPLPart` writers grepped), Low on present-day reachability.

### RIPTOPL-191 / lngGetFilePath indexes languages[langID-1] with no range check

- **Category:** 6 (bug — latent OOB index)
- **Locations:** src/core/lang.cpp:550-556 (`lngGetFilePath`); sole caller src/ui/guinotify.cpp:135 passes `lngGetGuiValue()`
- **Problem:** `lngGetFilePath(0)` — the value `lngGetGuiValue()` returns whenever English is active — reads `languages[-1]`. Today the only call site sits behind the `showLang_` popup flag, which is only raised after a file language was applied, so the bad index is not reached in practice (INFERRED from the popup's purpose; the flag's setters are outside this section's files). The function is a public API in include/lang.h with no stated precondition, one new caller away from a heap under-read whose garbage pointer is then printed by snprintf `%s`.
- **Fix:** Return NULL (or an empty string) for `langID < 1 || langID > nLanguages`, mirroring the bounds discipline `lngSetGuiValue` already applies to the same id space at src/core/lang.cpp:485.
- **Justification (fix adds a check):** one comparison at a cold UI path; makes the API safe for its documented id domain (0 = English = no file).
- **Benefit:** Removes a latent under-read in a public accessor.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ for the missing check and the id semantics; INFERRED that the current sole caller cannot pass 0)

### RIPTOPL-192 — usbd (inherited PS2SDK): the isochronous halted-endpoint cleanup clears the wrong TD→request LUT slot

- **Category:** 6 (copy-paste index bug; stale LUT entry)
- **Locations:** `modules/usb/usbd-ra/src/usbd.c:139` (inside `processDoneQueue_IsoTd`'s halted-endpoint walk: `memPool.hcIsoTdToIoReqLUT[arg - memPool.hcIsoTdBuf] = NULL;` where the loop variable is `curTd`); correct sibling at `usbd.c:224` (`processDoneQueue_GenTd` clears `[tdListPos - memPool.hcTdBuf]`)
- **Problem:** READ. `arg`'s LUT entry was already cleared at `usbd.c:109` before the walk begins. Each queued IsoTD freed in the walk therefore keeps its LUT entry pointing at a request that is then completed as `USB_RC_ABORTED` and returned to the free pool (or handed to a callback). The stale entry survives until that TD slot is re-queued; any done-queue processing that consults the slot in between resurrects a freed — potentially already reallocated — `IoRequest`. Provenance: **inherited, untouched PS2SDK** per the Makefile header (see provenance note); impact in OPL's own deployment is latent because no in-tree USB client driver uses isochronous endpoints (mass storage is bulk, pads are interrupt — INFERRED from the in-tree drivers).
- **Fix (words):** Clear `[curTd - memPool.hcIsoTdBuf]` at `:139`, matching the GenTd sibling one function below. *Justification (changes a line, adds none): a one-token correction of an index that is provably the wrong slot.*
- **Benefit:** Halted isochronous endpoints leave no stale request pointers behind.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the defect (READ, with the correct pattern in the same file); Low on present-day impact (no ISOC consumer in-tree).

### RIPTOPL-193 — usbd (inherited PS2SDK): initialisation failures are swallowed at every level, and the main pool allocation is never NULL-checked

- **Category:** 6 / 8 (unchecked allocation; discarded error returns on the init path)
- **Locations:** `modules/usb/usbd-ra/src/hcd.c:380-381` (`memBuf = AllocSysMemory(...)` immediately followed by `memset(memBuf, 0, memSize)` with no NULL test); `hcd.c:361` (`initHardware()` return ignored — it returns −1 on host-controller reset timeout at `:342-344`); `hcd.c:535`/`:538` (`initHcdStructs()` / `initHubDriver()` returns ignored by `hcdInit` — `initHubDriver` *does* check its own allocation and return −1, `hub.c:49-53`); `modules/usb/usbd-ra/src/usbd.c:406` (`hcdInit()` return ignored by `_start`, which returns `MODULE_RESIDENT_END` regardless — even though `hcdInit` returns 1 when interrupt-handler registration fails, `hcd.c:512-516`)
- **Problem:** READ. Three distinct failure shapes converge on the same outcome — the module loads as resident and clients bind to a half-initialised USB stack: (1) pool allocation failure is a memset-to-NULL crash instead of a clean failure; (2) a host controller that never completes reset is driven on anyway; (3) hub-driver allocation failure (the one place that *does* report) is discarded by its caller. Provenance: **inherited, untouched PS2SDK** per the Makefile header.
- **Fix (words):** Check `memBuf` before the memset and propagate: `initHcdStructs` returns the failure, `hcdInit` returns it, `_start` returns `MODULE_NO_RESIDENT_END`. *Justification (adds checks): four return-value tests on an init path that already computes every one of these results — no new machinery, just not discarding them.*
- **Benefit:** A failed USB init fails the module load visibly instead of crashing in memset or leaving pad/storage drivers bound to a dead stack.
- **Effort:** S · **Risk:** Low (success paths untouched) · **Confidence:** High (READ — every discarded return is quoted above).

### RIPTOPL-194 / `isofs_ReadSect` computes the byte offset in 32 bits — directory extents past 4 GiB wrap

- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/iso2opl/src/isofs.c:195` — `offset = lsn * 2048;` where `lsn` is `u32`: the multiply wraps modulo 2³² before widening to `s64`. Contrast `isofs_Read` (`:981`): `offset = (((s64)fh->lsn) * 2048) + fh->position;` — the file-data path widens first.
- **Problem:** `ReadSect` serves the volume descriptors (sectors 16-19, always low — safe) and the directory cache (`CachedDirInfo.sector_start`, taken from a directory's TOC extent). A directory extent at sector ≥ 2,097,152 (≥ 4 GiB into a DVD9 ISO) is read from the wrapped, wrong offset and parsed as if it were the directory. Real ISOs keep directories low, so this is a correctness cliff rather than a memory bug — but it is silent: the outcome is "file not found" or the wrong record, not an error.
- **Fix:** Widen before multiplying: `offset = (s64)lsn * 2048;` (same for `nbytes`, which is bounded by the 32-sector cache in practice).
- **Justification (fix changes one expression):** it makes `ReadSect` match the already-correct arithmetic in `isofs_Read` in the same file.
- **Benefit:** directory lookups stay correct anywhere in an 8.5 GB image.
- **Effort:** S · **Risk:** Low · **Confidence:** Med, READ for the arithmetic, INFERRED for real-world reachability (no shipped ISO with a >4 GiB directory extent was examined).

### RIPTOPL-195 / http_conformance.py reads response bodies with no size bound

- **Category:** 6 (unbounded read from an untrusted network peer)
- **Locations:** tools/pc/http/http_conformance.py:58-68 (`get`: `body = resp.read()`), consumed at :193-197 (catalog) and :105-153 (`check_range`)
- **Problem:** Every response body is read fully into memory before any length validation happens. The catalog's 256 KB cap (`catalog_reference.CATALOG_BYTES_MAX`) is only applied by `parse_catalog` *after* the whole body has been downloaded and stored — the file-input path in catalog_reference.py:419-422 deliberately reads only `CATALOG_BYTES_MAX + 1` bytes for exactly this reason, and the network path defeats it. Likewise a server that ignores `Range` and answers a one-sector probe with a whole multi-GB ISO is fully buffered before `check_range` compares `len(body)` with the requested length. The tool's entire purpose is pointing it at servers whose behaviour is *not* trusted ("fails on exactly the things that would corrupt a disc read").
- **Fix:** In `get`, read at most a caller-supplied cap + 1 byte (catalog: `CATALOG_BYTES_MAX + 1`; range reads: the requested length + 1) and treat an over-long body as the length failure the checks already report. *Justification for adding a bound:* it is the same bound the reference parser already defines, applied at the point of read instead of after it.
- **Benefit:** A broken or hostile server can no longer OOM the conformance checker with the first response; over-long answers are reported as the failures they are.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-196 / ps2link naplink handler uses the RPC payload as a `printf` format string

- **Category:** 6
- **Locations:** `modules/debug/ps2link/nprintf.c:23-35` (`printf(buffer)` at `:28`)
- **Problem:** `naplinkRpcHandler` ignores its `size` parameter and passes the EE-supplied RPC buffer directly as the format argument. Any `%` sequence in a printed string is interpreted (reading varargs that were never passed), and the buffer is not guaranteed NUL-terminated within the 512-byte RPC buffer, so the print can also run past it. The payload here is the developer's own EE code (debug-only), which caps severity — but the handler exists to print arbitrary game/homebrew strings, i.e. exactly the untrusted-with-respect-to-formatting kind of text.
- **Fix:** Print with a constant `"%s"` format (and bound the length by `size`). Justification: one-line change at the single call site.
- **Benefit:** Naplink console output can no longer crash or misread the stack because a logged string contained a percent sign.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-197 / diaShowKeyb's caret and backspace operate on bytes, corrupting UTF-8 text

- **Category:** 6 (bugs)
- **Locations:** `src/ui/diaeditors.cpp:130-138` (`len = strlen(text)`, `caret = len` — byte counts, despite the comment calling the caret a position "in characters"), `:298-307` (L1/R1 do `caret--` / `caret++` — one byte), `:362-368` and `:396-403` (both backspace paths `memmove` exactly one byte out), contrasted with `:112-126,225-230` (`diaKeybNextChar`/`diaKeybPrevChar` exist, but are used only for view scrolling)
- **Problem:** With non-ASCII seed text — a Japanese game title in a rename field is this fork's headline use case (see cfgtitle.cpp) — backspace from the end deletes only the trailing byte of a multibyte character, leaving an invalid UTF-8 sequence on screen and in the buffer; L1 can park the caret inside a sequence, and typing there splits it. Pure-ASCII fields (the keyboard only ever inserts ASCII) are unaffected, which is presumably why it has survived. Mechanism READ; reachability INFERRED (requires a field seeded with non-ASCII text).
- **Fix (words only):** Make caret movement and backspace use the existing `diaKeybNextChar`/`diaKeybPrevChar` helpers: L1/R1 step by character, and backspace deletes the byte range of the one character left of the caret. No new machinery — the helpers already exist in the same file.
- **Benefit:** UTF-8 names survive keyboard editing instead of being mangled byte by byte.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the mechanism (READ), Medium on reachability (INFERRED)

### RIPTOPL-198 / apemodpatch patches executable code but leaves its `FlushIcache()` commented out — its sibling patch flushes

- **Category:** 6
- **Locations:** `modules/iopcore/patches/apemodpatch/main.c:104-115` (eight instruction writes incl. two `JAL`s), `:118` (`// FlushIcache(); //Flush instruction cache as instructions were modified.`); contrast `modules/iopcore/patches/iremsndpatch/main.c:110` (same patching pattern, flush present)
- **Problem:** apemodpatch rewrites instructions in a loaded module and immediately `StartModule`s it, with the icache flush the author wrote left commented out. The sibling IREMSND patch — same directory family, same technique, same era — calls `FlushIcache()` with the identical comment, so the tree contains both answers to the same question. If any patched line is already resident in the IOP's icache (the module image region having executed or been fetched before, e.g. address reuse), the CPU can execute the pre-patch instructions and the game's broken locking protocol stays live while the patcher reports success.
- **Fix:** Uncomment the flush (it is already written). No justification needed — this is restoring the author's own line.
- **Benefit:** The Ape Escape patch is guaranteed to be the code that runs, matching the sibling patch's discipline.
- **Effort:** S · **Risk:** Low · **Confidence:** Medium (READ for the asymmetry; whether stale lines can actually be resident for a loaded-but-not-started module is INFERRED)

### RIPTOPL-199 / genvmc prints usage on a wrong argument count and then dereferences the missing arguments anyway

- **Category:** 6 (bugs/security)
- **Locations:** `tools/pc/genvmc/src/genvmc.c:399-413` — `if (argc != 3) printUsage();` with no return, falling through to `strtol(argv[1], NULL, 10)` and `vmc_mcformat(argv[2], …)`.
- **Problem:** Run with no arguments, `argv[1]` is NULL and `strtol(NULL, …)` crashes; run with one argument, `argv[2]` is NULL and reaches `fopen`. The usage text is printed and the crash follows it. (Run with *extra* arguments it also proceeds, silently ignoring them.)
- **Fix:** Return `EXIT_FAILURE` after `printUsage()` when `argc != 3`.
- **Justification (fix adds a check):** it is the missing half of the validation the code already attempts.
- **Benefit:** the tool's most common misuse becomes an error message instead of a segfault.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (crash behaviour of `strtol(NULL)` is standard-libc INFERRED; the fall-through is READ).

### RIPTOPL-200 / ziso.py: argument validation lets `-c` be omitted and `-b 0` through, both crash

- **Category:** 6 (unchecked CLI values reach arithmetic)
- **Locations:** tools/pc/ziso.py:336-366 (`parse_args`; `level` defaults to `None`, the only check is `bsize % 2048 != 0` at :362), tools/pc/ziso.py:405-410 (`main` treats only `level == 0` as decompress), tools/pc/ziso.py:233 (division by `block_size`)
- **Problem:** Executed on a /tmp copy: running with no `-c` at all crashes with `TypeError: %d format: a real number is required, not NoneType` inside `show_comp_info`; `-b 0` passes the multiple-of-2048 test (`0 % 2048 == 0`) and crashes with `ZeroDivisionError` at the `total_block` division; negative multiples of 2048 pass the same test. Non-numeric `-c`/`-b` values raise an uncaught `ValueError` from `int(a)`.
- **Fix:** In `parse_args`, require `level is not None`, `0 <= level <= 12`, and `bsize > 0`, exiting through the existing `usage()` path on violation. *Justification:* these are the values every later division, shift and LZ4 call trusts; validating them at the single entry point is smaller than guarding each use.
- **Benefit:** The three most likely user mistakes produce usage text instead of tracebacks (one of them after the output file has been created).
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ + executed on a /tmp copy.

### RIPTOPL-201 — MX4SIO multi-block write: the CMD55 and ACMD23 results are assigned and immediately overwritten, never tested

- **Category:** 3 / 6 (dead result assignments around a protocol step)
- **Locations:** `modules/mx4sio_bd/src/spi_sdcard_driver.c:511-518` (`spisd_write_multi_begin`: `results = spisd_send_cmd(CMD55, 0);` at `:513`, overwritten by `results = spisd_send_cmd(ACMD23, count);` at `:514`, overwritten again by `results = spisd_send_cmd(CMD25, sector);` at `:518`)
- **Problem:** READ. The pre-erase sequence exists to be checked — the init path in the same file tests every CMD55/ACMD41 reply (`:191-195`, `:235-239`) — but here neither reply is ever read: if CMD55 fails, ACMD23 is an illegal command to the card; if ACMD23 fails, the write proceeds without pre-erase. The failure is silent in both cases, and the two assignments are dead stores that make the sequence look validated when it is not.
- **Fix (words):** Either test CMD55's reply before issuing ACMD23 and ACMD23's before CMD25 (failing the begin on error, the shape the init loops already use), or — if pre-erase is intentionally best-effort — drop the assignments and comment that. Do not keep the appearance of a check without the check.
- **Benefit:** A card that rejects the pre-erase sequence fails (or is documented as best-effort) at write-begin instead of mid-protocol.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — the overwrite chain is three consecutive statements).

---

### RIPTOPL-202 / Plasma texture allocation is never checked, but draw() writes through it unconditionally

- **Category:** 6 (bug — unchecked allocation)
- **Locations:** src/gfx/plasma.cpp:177 (`init()` memalign), src/gfx/plasma.cpp:235-255 (`draw()` writes `tex_.Mem + kWidth * row_` and passes `&tex_` to rmDrawPixmap); init/draw are driven unconditionally from src/ui/gui.cpp:85,146
- **Problem:** `init()` stores `memalign(128, 4096)` into `tex_.Mem` with no NULL test — unlike `shutdown()` in the same file, which does test it. If the 4 KB allocation ever fails (heap exhausted late in a session), every subsequent frame writes 32×6 RGBA pixels through a NULL base and hands the texture to the renderer, instead of simply having no animated background.
- **Fix:** Guard in `draw()` (return when `tex_.Mem == NULL`), matching the guard `shutdown()` already has.
- **Justification (fix adds a check):** one comparison on a per-frame path, converting a guaranteed NULL-write crash into a missing background.
- **Benefit:** Allocation failure degrades to a static menu instead of a crash loop.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ for the missing check and unconditional use; INFERRED that memalign can fail in practice — 4 KB, so this is a hardening item, not a live bug report)

### RIPTOPL-203 / ziso.py: corrupt LZ4 block can hang decompression forever, and the trim-loop can "decode" garbage

- **Category:** 6 (bug / robustness on untrusted file input)
- **Locations:** tools/pc/ziso.py:62-72 (`lz4_decompress`), called from tools/pc/ziso.py:130-196 (`decompress_zso`)
- **Problem:** On `LZ4BlockError` the helper deletes one trailing byte and retries, with no floor. When the input is exhausted the slice stops shrinking (`b""[:-1] == b""`), so the `while True` loop never terminates. Executed against a /tmp copy: `lz4_decompress(b"\xff", 2048)` and `lz4_decompress(b"\xff\xff\xff", 2048)` were still spinning when killed by a 3 s timeout, while `b"\x00"` returns immediately. The same loop has a second failure mode: trimming can reach a byte string LZ4 accepts, returning wrong data of the wrong length (executed: `b"garbage-not-lz4"` "decodes" to 6 bytes) — only the caller's later `len(dec_data) != block_size` check turns that into an abort rather than silent corruption.
- **Fix:** Delete the trim-and-retry loop; call `lz4.block.decompress` once and let a failure be an error (decompress_zso already has an exception path that reports the block and exits).
- **Benefit:** A damaged or hostile .zso can no longer hang the documented PC decompressor indefinitely; corrupt blocks fail at the block, with its offset.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ + executed on a /tmp copy.

### RIPTOPL-204 / ziso.py: compression silently discards a trailing partial block

- **Category:** 6 (silent data loss)
- **Locations:** tools/pc/ziso.py:233 (`total_block = total_bytes // block_size`), loop at tools/pc/ziso.py:247-310, header still records the true `total_bytes` at tools/pc/ziso.py:228-231
- **Problem:** Both compress and decompress iterate only over whole blocks. An input whose size is not a multiple of the block size loses its tail with no warning, while printing "ziso compress completed". Executed round-trip on a /tmp copy: a 3,000-byte input at the default 2,048 block size compressed and decompressed "completed" and came back as 2,048 bytes — 952 bytes silently gone. Real ISOs are normally block-aligned, which is exactly why the one misaligned file a user feeds it will be trusted.
- **Fix:** Refuse (or pad with an explicit warning) when `total_bytes % block_size != 0` at the top of `compress_zso`, mirroring the format error path. *Justification for adding a check:* it converts silent data loss into a refused operation; no new feature is added.
- **Benefit:** The tool can no longer produce a "successful" ZSO that is missing the end of the source image.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ + executed on a /tmp copy.

### RIPTOPL-205 / HDD `DeviceInit` retries the APA-header DMA forever — the one unbounded init wait in a directory where every sibling wait is bounded on purpose

- **Category:** 5 (performance/hang)
- **Locations:** `modules/iopcore/cdvdman/device-hdd.c:70-73`.
- **Problem:** `while ((r = sceAtaDmaTransfer(...)) != 0) { DPRINTF(...); DelayThread(2000); }` has no attempt bound and no failure exit. A drive/ATAD state that never returns 0 (damaged drive, wedged bus, the PIO-dead variant interaction space covered by RIPTOPL-068/138 in `atad.c`) hangs IOP initialisation silently and permanently. The neighbouring device backends in the same directory all bound exactly this wait and say why in comments: `device-mmce.c` (100 × 100 ms, then fail-fast reads), `device-udpfs.c` (300 × 100 ms, then fail-fast), `device-http.c` (50 × 10 ms). The HDD backend is the outlier.
- **Fix:** Bound the loop the same way its siblings are bounded (fixed attempt budget, then proceed to the fail-fast state / return so reads error visibly). Justification: reuses the established in-directory pattern; adds one counter, removes an infinite hang.
- **Benefit:** A dead HDD path surfaces as read errors instead of an undiagnosable freeze at game start.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-206 / `cdrom_read` retries a failed `sceCdRead` forever, in all three phases, while holding the I/O semaphore

- **Category:** 5 (performance/hang)
- **Locations:** `modules/iopcore/cdvdman/ioops.c:305-306`, `:321-322`, `:334-335` (three identical `while (sceCdRead(...) == 0) DelayThread(10000);` loops inside `cdrom_read`).
- **Problem:** If the underlying read keeps failing (device error, removed/failed backend), each phase spins indefinitely with the `cdrom_io_sema` held, so the game's filesystem thread — and every other cdrom op queued on the same semaphore — hangs with no error ever returned. The sibling EE-read path in `modules/iopcore/cdvdfsv/ncmd.c` (`cdvd_readee`) treats `sceCdRead(...) == 0` as a failure: it records `SCECdErREADCF` and returns. The two halves of the same disc-read subsystem disagree about what a 0 return means, and the ioops half's interpretation has no exit.
- **Fix:** Give each retry loop a bounded attempt count and fall through to the same error treatment `ncmd.c` already uses (set the read error, return the bytes read so far / error). Justification: aligns this file with the in-subsystem precedent; adds a counter, removes an infinite semaphore-holding spin.
- **Benefit:** Permanent read failures become errors a game can surface instead of a total filesystem freeze.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-207 / ps2link `pko_recv_bytes` spins forever when the peer closes the connection mid-packet

- **Category:** 5 (performance/hang), with a Cat 6 framing footnote
- **Locations:** `modules/debug/ps2link/net_fio.c:86-105` (`pko_recv_bytes`; `recv` at `:94`); framing caller `:107-160` (`pko_accept_pkt`, `hlen` test at `:137`, payload length expression at `:146`)
- **Problem:** The receive loop handles `len < 0` (error) but not `len == 0` — the orderly-shutdown return. On a closed connection `left` never decreases and the loop calls `recv` forever at full speed, wedging the fileio thread (and, because `net_fsys.c` holds `fsys_sema` across every `pko_*` call, every subsequent host: access). A developer restarting the PC-side ps2link host mid-transfer is the ordinary trigger. Footnote: `pko_accept_pkt` also never rejects `hlen < sizeof(pko_pkt_hdr)`; the payload length `hlen - sizeof(...)` only fails safe today by unsigned-wrap-to-negative accident on this target, not by an explicit check.
- **Fix:** Treat a zero-length `recv` as end-of-stream and return an error; add the explicit `hlen < sizeof(pko_pkt_hdr)` rejection beside the existing `hlen > len` one. Justification: two comparisons in the framing path, no new machinery.
- **Benefit:** Host disconnects become a recoverable fileio error instead of a permanent IOP-side spin that also blocks the filesystem semaphore.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-208 / igrmbox `mbSendEE` busy-waits on SIF DMA with no timeout, on the only mailbox thread

- **Category:** 5 (busy-wait / hang)
- **Locations:** `modules/iopcore/igrmbox/igrmbox.c:72-88` (`mbSendEE`; retry loop ending `:85`, status spin `:86-87`); sole caller thread `mbThread` at `:133-150`
- **Problem:** `mbSendEE` retries `sceSifSetDma` in a `do…while (!id)` loop and then spins `while (sceSifDmaStat(id) >= 0);` — both unbounded. This runs on `mbThread`, the module's only thread, and the mailbox exists specifically for use while a game is paused/frozen — i.e. precisely when the EE/SIF side may be unresponsive. If a DMA never completes, the thread never returns: no WAKE is ever processed (sound stays hushed — `mbHush` at `:35-55` has set all voice pitches and master volumes to 0), no END frees the buffer, and the in-game menu is dead until reset. The EE side of this same mailbox was already noted in round 3 as deliberately bounded (3 s, with a documented late-answer contract); the IOP side has no bound at all.
- **Fix:** Give both waits the same bounded shape the EE side already uses (a retry/time budget after which `mbSendEE` gives up and `mbThread` continues its loop), so a wedged DMA degrades to a dropped ack instead of a dead mailbox. Justification: the bound is the fix; it mirrors an in-tree contract rather than inventing one.
- **Benefit:** A frozen EE can no longer permanently silence audio and kill the in-game menu's IOP half with one stuck DMA.
- **Effort:** S · **Risk:** Low · **Confidence:** Mechanism High (READ); that a DMA can wedge in the frozen-game scenario is INFERRED from the module's stated purpose

### RIPTOPL-209 / smb_Disconnect drains the socket with an unbounded blocking recv loop

- **Category:** 5 (performance/blocking)
- **Locations:** `modules/network/smbman-ra/smb.c:1624-1640` (loop at `:1632-1633`).
- **Problem:** After `shutdown(main_socket, SHUT_WR)`, the code loops `while (recv(main_socket, &dummy, sizeof(dummy), 0) > 0);` on a blocking socket with no timeout, no byte bound and no poll. A server that keeps the connection open and trickles (or simply never closes its side) parks the calling IOP thread inside disconnect forever — the exact lockup class `RecvTimeout`'s long comment at `smb.c:148-155` says this module exists to avoid, applied everywhere except here. The sibling in-game stack's disconnect poll loop sleeps with backoff (round-2 clearance note).
- **Fix:** Bound the drain: poll with the module's existing `RecvTimeout` helper and a small total budget (e.g. a fixed iteration/time cap), then close regardless.
- **Justification (adds a bound):** it reuses the file's own timeout helper; no new mechanism, and the drain's purpose (let sent data land before close) survives a bounded wait.
- **Benefit:** Disconnect/reconnect against a wedged server cannot hang the SMB device thread permanently.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-210 / cdvdfsv's `sysmemSendEE` busy-spins twice with no yield and no bound — the pattern streaming.c was fixed for, one module over

- **Category:** 5 (performance)
- **Locations:** `modules/iopcore/cdvdfsv/cdvdfsv.c:253-271` (`sysmemSendEE`): the `sceSifSetDma` retry `do { suspend; id = sceSifSetDma(...); resume; } while (!id);` at `:260-267`, and the empty completion spin `while (sceSifDmaStat(id) >= 0);` at `:269-270`.
- **Problem:** Neither wait yields, sleeps, or times out. Every cdvdfsv reply path funnels through this function (readee completions, readchain progress, searchfile results), so a full SIF DMA tag queue or a stuck transfer stalls the RPC thread in a tight loop. The same hazard is diagnosed in detail in the in-tree comment at `modules/iopcore/cdvdman/streaming.c:215-232`, where the equivalent `sceSifSetDma` loop was rewritten to `DelayThread(500)` between attempts precisely because spinning starves the IOP; the completion spin in streaming.c is already filed separately as RIPTOPL-045 — this cdvdfsv pair is a different file and was not covered by it.
- **Fix:** Wait the way streaming.c now does: yield/sleep inside the `sceSifSetDma` retry, and sleep inside the `sceSifDmaStat` wait. Justification: copies the maintainers' own fixed pattern from the sibling module; no new mechanism.
- **Benefit:** Removes two unbounded spins from the EE↔IOP reply path every disc RPC depends on.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-211 / VMC save delete frees at most 64 file chains; the rest leak on the card forever

- **Category:** 6
- **Locations:** `src/core/vmcsaves.cpp:1181` (`u32_t files[SAVE_FILES]` in `vmcSavesDelete`), `src/core/vmcsaves.cpp:1193` (`for (i = 2; i < vsDirLen(dir.length) && nfiles < SAVE_FILES; i++)`)
- **Problem:** `vmcSavesDelete` collects the first cluster of each file into a fixed 64-entry array before hiding the root entry and freeing chains. A save folder may legally hold more entries than that — the directory length the code itself trusts goes to `DIR_MAX` 4096, and `vsLoadVmc` treats >64 files as a copy error (`VMCSAVES_ERR_TOO_BIG`) rather than an impossibility. For file 65 onward, the chain is never collected, the root entry is hidden and the folder's own chain is freed, so those clusters stay marked used in the FAT with nothing naming them: permanent space loss on the card image, recoverable only by reformatting. The delete is offered for any save `vmcSavesList` shows, and that lister does not count files.
- **Fix:** Delete the `files[]` array and free each file's chain inside the enumeration loop (the entries are read while the save is whole, and a file's chain is independent of the folder's chain, which is still freed last); keep the current order of hiding the root entry before flushing the FAT. This is a net deletion — the storage cap, not the freeing, is what is removed.
- **Benefit:** Deleting a large save returns all of its clusters; the card no longer accumulates orphaned space.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-212 / A failed journal replay is reported as success — and the journal is erased anyway

- **Category:** 6
- **Locations:** `modules/hdd/apa/src/journal.c:73-85` (loop `break`s on transfer error at `:76`/`:80`, then falls through to `apaCacheFree` and `return apaJournalReset(device);` at `:85`); callers that trust the return: `modules/hdd/apa/src/hdd.c:331-334`, `modules/hdd/apa/src/hdd_blkio.c:121-124,263-266`.
- **Problem:** If any replay read/write fails mid-sequence, `ret` holds `-EIO`, but the function's return value is whatever `apaJournalReset` returns (normally 0). Two losses compound: (1) callers proceed as if recovery completed, although the partition table is in a partially-replayed state; (2) the reset **erases the journal**, destroying the only copy of the headers that were not replayed, so the next boot cannot finish the job either. Recovery metadata is discarded at exactly the moment it is known to be needed.
- **Fix (words only):** Preserve `ret`: if the loop broke on error, return it without resetting the journal (leave the journal on disk for a later retry), and only reset after a fully successful replay.
- **Benefit:** Partial recovery stays recoverable and visible to callers instead of silently becoming permanent table damage.
- **Effort:** S · **Risk:** Low–Med (a permanently unreadable journal sector would now be retried each mount instead of discarded — that is the conservative direction for a recovery log) · **Confidence:** High, READ.

### RIPTOPL-213 / APA cache flush clears the dirty flag and drops errors on failed header writes

- **Category:** 6
- **Locations:** `modules/hdd/apa/src/cache.c:87-105` (`apaCacheTransfer` executes `clink->flags &= ~APA_CACHE_FLAG_DIRTY;` at `:101` unconditionally, success or failure); `cache.c:111-126` (`apaCacheFlushAllDirty` ignores the returns of `apaJournalWrite` `:117`, `apaJournalFlush`, and the per-entry `apaCacheTransfer` `:123`, and returns `apaJournalReset(device)`).
- **Problem:** The flush sequence is journal-write → journal-flush → header writes → journal reset. If a header write to the disk fails (bad sector, device dropout), the entry is nevertheless marked clean, the failure is reported to nobody, and the journal is then reset — erasing the backup copy of the header that never made it to its home sector. The partition table on disk can therefore silently diverge from what every layer above believes was written, with the recovery path dismantled in the same operation.
- **Fix (words only):** In `apaCacheTransfer`, clear the dirty flag only when the transfer returned 0; in `apaCacheFlushAllDirty`, propagate the first error and skip `apaJournalReset` when any journal or header write failed. Justification: changes only error handling of existing writes; no new mechanism.
- **Benefit:** A failed table write stays dirty, journaled, and reported instead of being laundered into a successful flush.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-214 / APPS scan requires `d_type == DT_DIR` exactly — the failure mode themes.cpp already documented and fixed for MMCE

- **Category:** 6
- **Locations:** `src/sources/appscan.cpp:103` (`if (pdirent->d_type != DT_DIR) continue;` inside `scanApps`)
- **Problem:** Every app folder on a scanned device must arrive with `d_type == DT_DIR` or it is skipped before its `title.cfg` is even attempted. The repo itself records that this cannot be trusted: the comment at `src/gfx/themes.cpp:995-1002` (#152) explains that d_type is derived from iomanX mode bits, that MMCE card firmware fills stat.mode verbatim, and that clone firmwares may not speak the FIO_S_* dialect — "every entry then degrades to DT_UNKNOWN. That is why languages still listed … while themes vanished (this required == DT_DIR exactly)". `oplScanApps` scans every enabled prefix, MMCE included, so on such a card the APPS page scans zero apps with no error shown. (READ for the gate and the in-repo precedent; INFERRED that MMCE APPS folders hit the same dirent path, since scanApps uses the same POSIX readdir the theme catalog walks.)
- **Fix:** Delete the `d_type` gate in `scanApps` and let the existing `title.cfg` open/read decide: a non-directory entry produces a `dir/title.cfg` path whose `configRead` fails, and `appScanCallback` already returns 1 ("no boot/title", not counted, not fatal) for it. If the extra failed open per plain file is judged too costly on slow shares, fall back to the themes pattern — probe with `opendir()` only for entries that are not positively DT_DIR. Justification (for the probe variant only): it adds an open on the already-suspect subset, exactly as the accepted #152 fix does; the plain deletion adds nothing.
- **Benefit:** APPS discovery works on the same firmware/driver class that already broke theme discovery once; removes a silent total failure of one page.
- **Effort:** S / **Risk:** Low (deletion) to Med (probe variant) / **Confidence:** High (READ)

### RIPTOPL-215 / "Remove settings" leaves the Neutrino and Ember per-game keys behind

- **Category:** 6 (bug)
- **Locations:** src/ui/guigame.cpp:2121-2170 (`guiGameRemoveSettings`); the keys it misses are written at src/ui/guigame.cpp:2014-2050 (`guiGameApplyConfig`) and src/ui/guisettings.cpp:3396ff (Ember keys, writer evidence)
- **Problem:** `guiGameRemoveSettings` removes ~30 per-game keys (config source, DMA, core loader, compat, i.Link ID, DNAS, alt startup, parental lock, all GSM/Cheats/OSD/PadEmu keys, VMC names and disables) — but never removes `CONFIG_ITEM_NEUTRINO_ARGS`, `CONFIG_ITEM_NEUTRINO_VIDEO`, `CONFIG_ITEM_NEUTRINO_GSMCOMP` or `CONFIG_ITEM_NEUTRINO_BSDFS`, all four of which `guiGameApplyConfig` in the same file writes per-game, nor the four per-game Ember keys (`CONFIG_ITEM_EMBER_TIMING/DITHER/SHADING/CONTROLLER`, read back by `cueSaveGameSettings` in src/sources/cuesupport.cpp). A user who resets a game's settings to defaults still launches it with the old Neutrino args/video overrides (and old Ember settings) silently in force.
- **Fix:** Add the eight `configRemoveKey` calls to `guiGameRemoveSettings`, alongside the existing removals.
- **Justification (fix adds calls):** the removal list is the function's whole job; the eight keys were added to the save path after the remove path was written and never mirrored — this restores the documented "remove settings" contract rather than adding a feature.
- **Benefit:** Reset actually resets; removes a class of "my defaults won't take" reports against Neutrino/Ember games.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — full removal list and full ApplyConfig write list compared line by line)

### RIPTOPL-216 / VMC group table: two groups can never be returned, 8 title IDs can never match, 5 IDs are duplicated inside their own group

- **Category:** 6 (bugs — data) / 1 (dead data)
- **Locations:** `src/core/vmc_groups.cpp:1452-1473` (`getGroupIdForTitleId`, first-match linear scan over the table at `:1352`); identical arrays `titles_XEBP_100_71` (`:1143-1146`) and `titles_XEBP_100_72` (`:1149-1152`); `titles_XEBP_100_53` (`:929`) vs `titles_XEBP_000_12` (`:1271`); non-canonical IDs at `:443-444`, `:464-465`, `:472-473`, `:498`, `:614`; intra-group duplicates at `:665,668`, `:675,676`, `:1028,1029`, `:1053,1057`, `:1222,1226`
- **Problem:** Script-verified at the pin: 97 groups, 1,046 title IDs. (a) `titles_XEBP_100_72` is byte-identical to `titles_XEBP_100_71` (same 3 IDs), and `titles_XEBP_000_12`'s 4 IDs are exactly `titles_XEBP_100_53`'s 4 IDs. The lookup returns the FIRST match and the 100-series precedes the 000-series in the table, so group `XEBP_100.72` and group `XEBP_000.12` can never be returned — games belonging to those groups get the wrong shared-VMC name (callers at `src/ui/guigame.cpp:529,702` build the default VMC filename from the returned group). (b) 8 entries are not in the canonical `XXXX_###.##` form every other entry and every caller-supplied startup string uses — `"SLUS_20754"`, `"SLUS_20824"`, `"SCES_50422"`, `"SLES_50422"`, `"SCES_51154"`, `"SLES_51154"` (missing `.`), `"SLES-5350"` (`-` for `_`, and short), `"SSCES_540.95"` (doubled S) — so they can never `strcmp`-match and are dead. (c) 5 IDs appear twice within a single group (`SLES_546.24`, `SLUS_216.62`, `SLPS_731.03`, `SLPS_251.69`, `SLES_820.21`); the second copy is unreachable. Whether 100.72 / 000.12 are wrong data or wrong duplicates of their twins cannot be settled from this repo — one of each pair is wrong either way (INFERRED which).
- **Fix:** Verify the two shadowed groups and the 8 malformed IDs against the upstream VMC-group source data, correct the entries that have a real distinct value, and delete the ones that do not; delete the 5 intra-group second copies. (Correction, not addition — no justification needed.)
- **Benefit:** Two VMC groups become reachable (correct shared VMC names for their games); ~13 dead strings removed; the table becomes auditable by a duplicate/format scan.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — arrays, table order, first-match scan, and counts all verified at the pin; only the correct replacement values are external data)

### RIPTOPL-217 / padmacro keeps its edge-detection state in one global shared by all pads, so two-pad use flickers toggles

- **Category:** 6 (logic)
- **Locations:** `modules/pademu/padmacro.c:47-67` (globals `gPreviousButtons`, `gTurboButton*`, slowdown toggles), `:139-205` (`changeInternalState`; slowdown toggle flips at `:162-180`), `:234-256` (`padMacroPerform`); per-pad callers (context) `modules/pademu/ds34bt.c:1234,1274` and `modules/pademu/ds34usb.c:302-354`, each invoking `padMacroPerform` once per pad per poll
- **Problem:** The macro *settings* are global by design (one config from `_start`), but the *runtime edge state* is global too, while reports arrive interleaved per pad. `gPreviousButtons` therefore usually holds the **other** pad's buttons. In toggle mode the edge test (`previous & mask) != (buttons & mask)`, flip if now pressed) misfires continuously: with pad 1 holding its slowdown button and pad 2 idle, the interleave is *held, 0, held, 0…*, so every pad-1 poll looks like a fresh press and the toggle flips at poll rate — slowdown strobes instead of latching. The turbo-arming state machine (`gTurboButtonWaiting`, fed by whichever pad polls next) has the same cross-pad bleed. Single-pad use — presumably how it was tested — is unaffected, which is the shape that hides it.
- **Fix:** Make the runtime state (`gPreviousButtons`, slowdown toggle states, turbo waiting/mask/counter) a small per-port struct indexed by a port argument to `padMacroPerform`. Justification for added code: the state is genuinely per-input-stream; sharing it is the defect.
- **Benefit:** Toggle slowdown, axis-invert and turbo-arming behave identically on pad 2 as on pad 1, and two players stop toggling each other's macros.
- **Effort:** M · **Risk:** Med (touches the shared entry point of both pademu pad drivers) · **Confidence:** Mechanism READ; poll interleaving INFERRED from the per-pad caller sites

### RIPTOPL-218 / udptty's `tty_write` always returns 0 — the write contract is broken and send failures are invisible

- **Category:** 6 (logic)
- **Locations:** `modules/debug/udptty-ingame/udptty.c:225-236` (`udp_send`: `lwip_sendto` result discarded at `:233`, unconditional `return 0` at `:235`), `:259-268` (`tty_write` returns `udp_send`'s result)
- **Problem:** Every layer agrees to lie: `lwip_sendto`'s return is dropped, `udp_send` returns `0` on success and on failure alike, and `tty_write` — an ioman `write` operation, whose contract is to return the byte count — therefore reports **0 bytes written for every write**. Callers that honour the return value (stdio retry-on-short-write loops, the KPRTTY thread's `write(1, …)` at `:148`, any game runtime using the tty device) see permanent zero-progress, and a dead network is indistinguishable from a working one.
- **Fix:** Return `size` from `udp_send` when `lwip_sendto` reports success (and a negative error when it doesn't), so `tty_write` reports the bytes actually handed to the socket.
- **Benefit:** The debug console obeys the ioman contract; short-write handling in stdio works instead of spinning or dropping output, and network failure becomes visible.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-219 / opl2iso's copy loop spins forever on a read error

- **Category:** 6 (bugs/security — robustness)
- **Locations:** `tools/pc/opl2iso/src/opl2iso.c:260-266` — `while (!feof(fsrc)) { read = fread(block, 1, 65536, fsrc); if (read != fwrite(...)) … }`.
- **Problem:** `feof` is only set by an end-of-file read, not by an error read. A part file on failing media (read returns 0 with the error indicator set) loops forever writing nothing and printing the spinner — the write-side check can't fire because `0 == fwrite(...,0,...)`. Missing/truncated parts are the normal failure mode of this tool's input, so the loop shape matters.
- **Fix:** Loop on the `fread` result (`while ((read = fread(...)) > 0)`) and check `ferror(fsrc)` after it.
- **Justification (fix adds a check):** replaces the known-broken `feof` idiom with the read result the loop already has.
- **Benefit:** a bad part file produces the tool's existing "resulting file will be invalid"-class failure instead of a hang.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (hang requires a live I/O error — INFERRED trigger, READ loop shape).

### RIPTOPL-220 / History eviction compares against a stale timestamp and can evict the wrong record

- **Category:** 6 (bug)
- **Locations:** src/core/OSDHistory.cpp:168-186 (`AddHistoryRecord` least-used scan; victim consumed at :246)
- **Problem:** When an entry with a strictly lower `LaunchCount` is found, `LeastUsedRecord`/`LeastUsedRecordLaunchCount` are updated but `LeastUsedRecordTimestamp` is not reset to that entry's `DateStamp`. The timestamp still holds the minimum stamp of the *previous* (higher-count) candidate set, so a later entry tied on the new low count is only chosen if its stamp beats a stamp from a different count class. Worked example (READ from the code): counts/stamps (5,100), (3,500), (3,200) leave the victim as the (3,500) record instead of the least-recent (3,200).
- **Fix:** When the launch-count branch takes a new minimum, also set `LeastUsedRecordTimestamp = HistoryEntries[i].DateStamp`.
- **Benefit:** Eviction actually picks the least-recently-launched among the least-launched, as the two-key comparison intends; the archived history.old record is the intended one.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — control flow traced; impact is victim choice only, no memory-safety effect)

### RIPTOPL-221 / IoQueue's per-type pending counters are never reset, and termination cleanup frees requests without decrementing them

- **Category:** 6 (bugs) — diagnostic impact only
- **Locations:** `src/platform/ioman.cpp:275-278` (counters incremented in `appendLocked`), `:148-149` (decremented only on the normal drain path), `:163-168` (termination cleanup frees every queued request with no decrement), `:201-208` (`start()` resets `blocked_`/`terminate_`/`handlerCount_`/`head_`/`tail_`/`current_` but not `pendingByType_[]`)
- **Problem:** `start()` is explicitly written for a restart ("a refused autolaunch falls back to the menu", `:189-195`). If the previous worker was stopped with requests still queued, those requests are freed by the cleanup loop while their `pendingByType_` counts stay behind, and the new session inherits them permanently. `ioGetPending()` then reports phantom pending requests forever — it feeds the debug HUD (`src/ui/guioverlay.cpp:216`) and a diagnostic read in `src/gfx/textures.cpp:931-932`, i.e. exactly the instruments used to triage "is IO stuck?" reports. Per `include/ioqueue.h` the counters are diagnostic-only, so no functional path is affected. READ.
- **Fix (words only):** Zero `pendingByType_[]` in `start()` alongside the other queue state it already resets (the `totalByType_[]` lifetime totals can stay cumulative — they are totals, not pendings). Justification: one `memset` makes the diagnostic truthful across the restart path `start()` exists for.
- **Benefit:** The IO pending counts shown by the debug HUD / texture diagnostics stay truthful after an IO restart.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-222 / `apaCacheInit` leaks the header block when the second allocation fails

- **Category:** 6 (minor resource handling)
- **Locations:** `modules/hdd/apa/src/cache.c:26-44` (header array allocated at `:32`; on `cacheBuf` allocation failure at `:33-35` the function returns `-ENOMEM` without freeing it).
- **Problem:** A partial-init failure leaves the (largest of the two) allocations — `size × sizeof(apa_header_t)` of IOP system memory — permanently held by a module that then refuses to go resident (`hdd.c` treats cache-init failure as fatal). IOP memory is the scarcest resource in this stack (cf. `bd_cache.c`'s own comment on cache allocations starving the network drivers); a failed load attempt should not consume it forever.
- **Fix (words only):** Free the header block on the `cacheBuf == NULL` path before returning. Justification: completes the function's own cleanup contract; one call.
- **Benefit:** Failed APA init returns all of its memory; retries/other drivers are unaffected.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-223 / Non-IOP `apaGetTime` converts to "JST" with the wrong sign (host/test builds only)

- **Category:** 6 (minor, non-shipped path)
- **Locations:** `modules/hdd/apa/src/misc.c:93-111` (`rawtime += (-9 * 60 * 60);` under the comment `// Convert to JST`, followed by `gmtime_r`).
- **Problem:** `time()` returns a UTC epoch regardless of locale; obtaining JST (UTC+9) wall-clock via `gmtime` requires **adding** 9 hours. The code subtracts 9, producing UTC−9 — 18 hours behind the intended JST timestamp. The shipped IOP branch (`:64-92`, CD-clock based) is unaffected; the wrong branch serves host-side builds/tools of this driver, where partition timestamps would be stamped 18 h off.
- **Fix (words only):** Change the adjustment to `+9 * 60 * 60`.
- **Benefit:** Host-built APA tooling stamps the same time the IOP driver would.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the arithmetic (READ); Low for practical impact (host-only path, no in-tree host consumer of APA timestamps was identified in this section's files).

---

### RIPTOPL-224 / HDPro leg of the ATA set-transfer-mode ioctl reports success and does nothing (parent-read)
- **Category:** 8 (half-finished feature — silent no-op behind a success return)
- **Locations:** `modules/hdd/xhdd/hdpro.c:8-11` (the whole function); dispatch at `modules/hdd/xhdd/xhdd.c:45-47`; working sibling `modules/hdd/xhdd/xatad.c:55` (`ata_device_set_transfer_mode`)
- **Problem:** The EE-facing ATA_SET_MODE ioctl has two legs: the standard leg calls xatad's real implementation; the HDPro leg calls `hdproata_device_set_transfer_mode`, an 11-line file whose entire body is `return 0;`. A request to set a UDMA/PIO mode on an HDPro-equipped setup is therefore acknowledged as done while no mode is ever programmed. Whether HDPro hardware needs the mode set at all cannot be settled from this tree (INFERRED); what is READ is that the interface promises an operation one of its two implementations silently skips — the same success-shaped stub pattern as RIPTOPL-068, at minimum scale.
- **Fix:** Either implement the HDPro mode set, or return an explicit not-supported error from the stub so callers stop believing the mode was applied. Deleting the pretence is the minimum.
- **Benefit:** One launch-path ioctl stops reporting an operation it never performed.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the stub and its reachability (READ — file and dispatch both read by the parent this round); Med for practical impact (INFERRED hardware need)

### RIPTOPL-225 — The `use_alarm` setting is honoured by the SIO2 receive paths and ignored by every transmit path

- **Category:** 8 (half-applied control surface)
- **Locations:** setting written by `mmce_sio2_set_use_alarm` at `modules/mmce/mmceman/src/mmce_sio2.c:176-179`; honoured at `:278-279`/`:291-292` (rx_dma) and `:381-382`/`:394-395` (rx_mixed); ignored at `:211` (`tx_rx_pio` — used by **every** command — calls `SetAlarm` unconditionally) and `:492`, `:533`, `:570` (tx_mixed's three stages, unconditional `SetAlarm`/`CancelAlarm`)
- **Problem:** READ. A caller that disables alarm-based timeouts through the settings surface gets them disabled for reads only; every write chunk and every PIO command still installs the 2 s alarm. Whatever incompatibility the setting exists to avoid (its purpose is not documented in-tree — INFERRED) therefore still applies to exactly half the traffic, and the setting reads as global while being path-local. Since `tx_rx_pio` cannot run alarm-free, no transfer in this driver actually runs end-to-end with alarms off — evidence the setting may be vestigial rather than merely unguarded.
- **Fix (words):** A decision, not just code: either guard tx_mixed's three `SetAlarm`/`CancelAlarm` pairs with the same `if (mmce_sio2_use_alarm)` the rx functions use, or — the LESS direction, supported by the `tx_rx_pio` evidence — delete the setting and its setter/devctl surface entirely. Do not leave it half-applied.
- **Benefit:** The settings surface means what it says, one way or the other.
- **Effort:** S either way · **Risk:** Low · **Confidence:** High (READ asymmetry); the right direction is a maintainer call.

### RIPTOPL-226 / HTTP catalog parser allocates the 2,048-row cap (~0.9 MB) for every catalog, however small

- **Category:** 4
- **Locations:** `src/sources/httpsupport.cpp:165-166` (`httpParseCatalog`); cap at `include/httpsupport.h:19` (`HTTP_CATALOG_ROWS_MAX 2048`); entry size from `include/supportbase.h` (`base_game_info_t`, ~188 B) and the local `http_catalog_extra_t` (~260 B, dominated by `path[256]`)
- **Problem:** Every catalog fetch first mallocs both arrays at the maximum row count — roughly 2048 × 188 B ≈ 385 KB of games plus 2048 × 260 B ≈ 532 KB of extras, ~0.9 MB total — even though a real `games.csv` holds a handful to a few hundred rows, and the byte cap (256 KB) already bounds the input. `HttpSource::update` parses the new catalog while the previous catalog is still resident (it is freed only by `setCatalog`), so a refresh peaks at roughly two cap-sized catalogs plus the fetch buffer, on every refresh, in EE RAM that is also holding themes, art and the game lists. The Test Server path parses a second, throwaway catalog the same way. The parser already knows how to fail cleanly; it does not need the rows pre-allocated at cap.
- **Fix:** Count lines first (the buffer is already fully in memory) or grow the arrays geometrically as rows are accepted; keep `HTTP_CATALOG_ROWS_MAX` as the acceptance limit, not the allocation size.
- **Benefit:** Typical catalog memory falls from ~0.9 MB to a few KB–tens of KB per parse and removes the double-catalog refresh peak — meaningful heap/f fragmentation headroom on the EE.
- **Effort:** S–M · **Risk:** Low (note: `test_catalog_limits.py` extracts this function verbatim, so the harness contract must be kept) · **Confidence:** High, READ (constants and struct layouts read; sizes are arithmetic from them).

### RIPTOPL-227 / XeRAbora parse allocates the 1,024-entry cap (~232 KB) on every fetch, on top of the resident previous list and the 512 KB document

- **Category:** 4
- **Locations:** `src/sources/achievements.cpp:145` (`XERA_LIST_MAX = 1024`, `XERA_DOC_MAX = 512 * 1024`), `src/sources/achievements.cpp:356` (`malloc(XERA_LIST_MAX * sizeof(*list))` in `achievementsParseXerabora`), `src/sources/achievements.cpp:369-370` (cap-and-truncate comment)
- **Problem:** `achievement_entry_t` is 5 ints + `icon[33] + title[57] + description[101] + date[20]` = 232 bytes with padding (READ from `include/achievements.h:19-21`), so the parse allocation is ~232 KB, sized by the cap rather than by the document. During a refetch, the previous `xeraList` (another ~232 KB at the same cap) is still resident — it is freed only after a successful parse — and `xeraFetch` simultaneously holds the 512 KB document buffer: roughly 1 MB of transient heap for one menu fetch on a 32 MB machine (INFERRED arithmetic from the READ sizes). The comment at the cap says the bound is "larger than any RA set", and sets are parsed into it whether they hold 12 entries or 1,200.
- **Fix:** Size the allocation from the document (count the array's objects in a first pass, or grow the array as entries are appended) and free the old `xeraList` before replacing it once the new document has parsed. No new subsystem; the parser already walks the array once.
- **Benefit:** A typical fetch's transient footprint drops from ~1 MB to the size of the actual set plus the document; the resident cache shrinks to the real entry count.
- **Effort:** M / **Risk:** Med / **Confidence:** Med (sizes READ, magnitudes INFERRED)

### RIPTOPL-228 / ArtIndex retains cap-sized allocations (48 KB per directory) however few entries the directory holds / Category 4

- **Locations:** `src/gfx/artindex.cpp:191` (`malloc(kMaxEntries * sizeof(unsigned int))` for hashes), `src/gfx/artindex.cpp:199` (`malloc(kMaxEntries * sizeof(Pic))` for pics), retained at :281-286; constants at `include/artindexint.h:29-38` (`kSlots` 3, `kMaxEntries` 4096, `Pic` = 8 bytes at :40-45).
- **Problem:** Every directory sweep allocates the 4,096-entry cap up front — 16,384 B of hashes + 32,768 B of pics = 49,152 B — and the slot then *keeps those allocations* for its lifetime (`slot->hashes`/`slot->pics` are published as-is; only `count`/`picCount` record how much is used). The names buffer, by contrast, starts at 16 KB and grows to fit (:200, :231-238) — the file already knows the grow-to-fit pattern. A typical ART folder holds tens to hundreds of files, so with three slots the index permanently holds ~144 KB of mostly-empty arrays (3 × 49,152 B) to answer absence questions about directories that needed a few KB. This is the same shape as RIPTOPL-121 (VCD scan allocates its 2,048-entry cap per folder) in a different, previously unread file.
- **Fix:** Shrink to fit after the sweep (one `realloc` per buffer down to `count`/`picCount` before publishing, mirroring how `names` is sized to use), or grow both buffers incrementally like `names`.
- **Benefit:** ~40+ KB per indexed directory returned on realistic libraries; ~120 KB across the three slots (arithmetic from the in-tree constants — INFERRED magnitudes, READ mechanism).
- **Effort:** S. **Risk:** Low (shrink happens on the owner thread before `valid` is published, the same publication point the code already guards). **Confidence:** High (READ).

### RIPTOPL-229 / Glyph-cache pages are allocated from the raw codepoint, before the face is ever asked if it has the glyph / Category 4 (6-adjacent)

- **Locations:** `src/gfx/fntsys.cpp:581-621` (`cacheGlyph`: page computed at :594-600, load attempted only at :617), `src/gfx/fntsys.cpp:187-221` (`prepareGlyphCachePage`: `realloc` to `(pageid + 1)` pointers at :190); constants at `include/fontsystem.h:22` (`GLYPH_CACHE_PAGE_SIZE` 256) and the entry struct at `include/fontsystem.h:41-61` (36 bytes/entry).
- **Problem:** `cacheGlyph` derives `pageid = gid / 256` directly from the decoded codepoint and grows the page-pointer array plus a full 256-entry page (256 × 36 B = 9,216 B) *before* `FT_Load_Char` discovers whether the face contains the character at all; on load error the empty page stays allocated. The Höhrmann decoder in `include/utf8.h` accepts codepoints up to U+10FFFF, so a single astral-plane character in a theme or translation string (an emoji in a game name is enough) forces the pointer array to 4,353 entries (~17.4 KB at 4 B/pointer) plus the 9.2 KB page — ~26 KB retained per font until the next cache flush — for a glyph the font does not have. The only `FT_Get_Char_Index` pre-check in the function (:586-591) exists solely to pick the fallback font; the primary font's own coverage is never consulted before allocating.
- **Fix:** Ask the face first: when `FT_Get_Char_Index(font->face, gid) == 0` (and no fallback supplies it), return NULL before `prepareGlyphCachePage`. This is a deletion-shaped guard using a call the function already makes. Justification for adding a check: it removes a file/string-driven allocation whose size the input controls, using an existing FreeType query — no new mechanism.
- **Benefit:** Per font, up to ~26 KB of retained cache avoided per out-of-coverage high codepoint, and no empty pages retained after failed loads (magnitudes are arithmetic from the header constants — INFERRED, not measured).
- **Effort:** S. **Risk:** Low (a `.notdef`-rendering behaviour change for missing glyphs is the only visible difference; measurement callers already treat NULL as "skip"). **Confidence:** High READ (mechanism); magnitude INFERRED.

### RIPTOPL-230 / diagSave keeps a second, permanent 16 KB copy of the whole diagnostic ring

- **Category:** 4
- **Locations:** `src/core/diagsave.cpp:21` (`static char copy[DIAG_SIZE]`, DIAG_SIZE = 16 KB at `:17`), duplicating `src/platform/diaglog.cpp:16` (`static char diagRing[DIAG_SIZE]`)
- **Problem:** The ring itself is the feature and earns its 16 KB. The copy is a second 16 KB of permanent BSS used only for the duration of `diagSave()` — a user-triggered save from Drive Status, or the pre-launch save — purely so `diagCopy` can assemble the wrapped ring into a contiguous buffer before two `write()` calls. `diagSave` runs in ordinary thread context (it opens and writes a file); nothing about it needs static storage.
- **Fix:** Allocate the snapshot on the heap inside `diagSave` and free it on exit (the early-return paths included), or write the ring in its two wrapped segments and drop the copy entirely. Justification (heap variant): it trades a small, bounded allocation on a rare, file-writing path for 16 KB of permanent BSS.
- **Benefit:** 16 KB of EE BSS returned permanently; the diagnostics feature keeps its full ring.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-231 / `load_cheats` reads a `.cht` file of any size into RAM whole, opting out of the cap its own reader provides / Category 4 (6-adjacent)

- **Locations:** `src/core/cheatman.cpp:476` (`buf = read_text_file(cheatfile, 0);` — maxsize 0 = "arbitrary size"), `src/core/cheatman.cpp:393-422` (`read_text_file`: the `maxsize` rejection at :411-415 and the `malloc(filesize + 1)` at :417).
- **Problem:** `read_text_file` was built with an explicit size cap and `load_cheats` is its caller that disables it. The parser downstream can only ever use a bounded prefix of any file: lines are capped at 255 chars (`CHEAT_LINE_MAX`, :305), entries at `MAX_CODES` = 250 cheats (`include/cheatman.h:40`, enforced at :360), and each entry's codes at the fixed `codes[]` array. A `.cht` file larger than that usable bound — a plausible community cheat pack, or a corrupt/oversized file on the card — is still `malloc`'d in full (plus the ~1.03 MB cheat table from `ensureCheatTable`) on a 32 MB machine, and a failed allocation just fails the launch's cheat load. There is no second caller pattern to copy: `load_cheats_buf` receives an already-resident tar member.
- **Fix:** Pass a real `maxsize` to `read_text_file` sized from the parser's own limits (250 cheats × their maximum useful lines), so an oversized file is rejected with the existing "too large" log instead of allocated.
- **Benefit:** Cheat loading's memory cost becomes bounded by what the parser can consume; oversized files fail with a log line instead of an allocation of arbitrary size.
- **Effort:** S. **Risk:** Low (a legitimate file larger than the derived cap would be rejected — the cap must be derived generously from the stated limits). **Confidence:** High (READ).

### RIPTOPL-232 / `USBA_crc32` rebuilds its constant CRC table on every call, and the table is 4× oversized

- **Category:** 5 (with a Cat 4 facet)
- **Locations:** `src/platform/system.cpp:465` (`static unsigned int crctab[0x400]`), `src/platform/system.cpp:475-489` (table build + data loop in `USBA_crc32`)
- **Problem:** The 256-entry table is a mathematical constant, yet it is recomputed with 256×8 iterations on **every** call. Callers invoke it per USBLD entry and per part-path construction (`src/sources/supportbase.cpp` path builder and size walk, `src/sources/ethsupport.cpp` launch settings), so a library scan with USBLD titles pays the rebuild once per game/part instead of once per process. Separately, `crctab` is declared with 0x400 entries (4 KB of BSS) while both the build loop and the data loop only ever touch indices 0–255 (the index is `byte ^ ((crc >> 24) & 0xFF)`), so 768 entries / 3 KB are permanently dead BSS on a 32 MB machine.
- **Fix:** Build the table once (a one-time initialised flag, or a precomputed 256-entry constant table) and shrink the array to 256 entries. The table **values** must not change: the initial `crc` state and the reversed table layout are shared with the PC twins (`tools/pc/iso2opl`, `tools/pc/opl2iso` carry the identical function) and the resulting checksum is baked into on-disk `ul.%08X.` filenames — any "standard CRC32" cleanup would orphan existing USBLD installs.
- **Benefit:** Removes ~2,048 multiply/xor steps per call from scan/launch paths; frees 3 KB BSS.
- **Effort:** S · **Risk:** Low (values unchanged; the risk is only in touching the algorithm, which the fix does not) · **Confidence:** High, READ (function and caller sites read; per-scan magnitude INFERRED from call sites).

### RIPTOPL-233 / Settings dialogs re-run the full fit-gap layout search — including text wrapping — on every frame

- **Category:** 5
- **Locations:** `src/ui/dia.cpp:616-624` (`diaSettingsFitGap`), called at `src/ui/dia.cpp:640` (every `diaRenderUI` frame), walking `diaLayoutBottom` (`src/ui/dia.cpp:568`); the render pass then measures every row again at `src/ui/dia.cpp:690`
- **Problem:** `diaRenderUI` runs per frame for as long as a dialog is open. On a Settings page it first calls `diaSettingsFitGap`, which re-walks the **entire** item list once per candidate gap (from `spacingH` down to the 7 px floor — more iterations the larger the user's font scale), and each walk re-measures and, for row-leading labels, re-wraps text (`diaWrapExtra` → `fntWrapText` into the 1 KB scratch buffer). The render pass immediately afterwards walks the same list and re-wraps/re-measures the same labels a second time to draw them. The final content bottom depends on the gap only as a per-row additive term, so the search does not need a full re-layout per candidate, and none of its inputs (item list, font, screen size) change between frames of a static dialog.
- **Fix:** Compute the layout once per call without the gap term and solve for the largest fitting gap arithmetically (or cache the chosen gap keyed on the dialog pointer + font scale + screen height, invalidated when any of them change). No behaviour change: same gap choice, computed once.
- **Benefit:** Removes multiple full-list measure/wrap passes from a 60 Hz UI loop on every Settings page; most visible on the long pages (General, Network) with large fonts. Magnitude INFERRED (structural; no hardware timing in this VM).
- **Effort:** M · **Risk:** Low–Med (the gap choice must remain bit-identical, including the already-fits case) · **Confidence:** High on the repeated-work structure (READ), INFERRED on frame cost.

### RIPTOPL-234 / Boot-dir resolver re-opens the boot ELF and boot folder on every slot, every 100 ms, for the whole budget

- **Category:** 5
- **Locations:** `src/sources/bdmboot.cpp:387-400` (per-scan `bdmReadDeviceIdentity` + `bootElfPresent` + `bootDirPresent` per slot), poll cadence at `src/sources/bdmboot.cpp:468`; file defined at `src/sources/bdmboot.cpp:44` (`bootDirPresent`, a Dopen/Dclose) and `src/sources/bdmboot.cpp:65` (`bootElfPresent`, an open/close)
- **Problem:** The legacy `resolveWithBudget` scan does not cache anything between iterations: each 100 ms pass over up to `MAX_BDM_DEVICES` slots performs an identity probe (Dopen + ioctls + Dclose), an open/close of `<tail>/<elfName>`, and a Dopen/Dclose of the boot folder — for up to the full 14 s budget (bootstrap: ~6.5 s), i.e. on the order of a hundred full re-scans in the worst case, all during boot or a settings save, on the same slow mass devices whose late arrival is the reason for polling. A slot that has answered once with a given driver/identity cannot change which ELF file it holds within one resolve; only not-yet-mounted slots need re-probing.
- **Fix:** Remember per-slot identity/ELF/dir results across iterations of one resolve and only re-probe slots that have not yet answered (the literal-slot resolver in the same file already polls a single known slot instead of re-scanning everything).
- **Benefit:** Worst-case boot resolution drops from ~100 full device scans to one scan plus probes of still-missing slots; less fileXio traffic competing with the very enumeration being waited on. Magnitude INFERRED.
- **Effort:** M · **Risk:** Med (the duplicate-install tie-breaking — literal slot first, definitive-ELF over folder-presence — is subtle and documented; caching must preserve it) · **Confidence:** High on the repeated-IO structure (READ), INFERRED on wall-clock saving.

### RIPTOPL-235 / filemgr: `fmChildren` re-reads the whole directory from scratch at every capacity growth

- **Category:** 5
- **Locations:** `src/core/filemgr.cpp:225-248` (`fmChildren`); `fmList` at `:199-223`; per-entry `stat()` in `fmIsDir` at `:174-190`
- **Problem:** `fmChildren` starts at cap 64 and, whenever a directory fills the buffer, `free()`s the list and re-runs `fmList` from entry zero at 4× capacity (64 → 256 → 1024 → 4096 …). `fmList` itself calls `fmIsDir` — a `stat()` per entry — for every entry it returns. A directory of N > 64 entries is therefore listed and stat'ed ~2× over (≈620 stat calls for 300 entries, ≈10,440 for 5,000, vs N in a grow-in-place design), on the devices where a stat is an RPC (USB/MC/SMB). `fmChildren` backs every copy, delete and size walk (`fmCopyTree`, `fmDeleteTree`, `fmTreeWalk`), so large folders pay this on each operation.
- **Fix:** Grow the buffer in place (`realloc`) and resume filling from the count already read instead of restarting the listing; if the device `readdir` cannot resume into a grown buffer portably, keep the restart but start from a larger first capacity so ordinary folders never restart. Deletion of the redundant passes, not new machinery.
- **Benefit:** Roughly halves the directory-listing + stat work for every folder over 64 entries in copy/delete/size operations (INFERRED, structural).
- **Effort:** M · **Risk:** Med (readdir-resume semantics vary by device driver) · **Confidence:** High for the repeated work (READ); Med for the resume variant being portable

### RIPTOPL-236 / filemgr: `fmRemove` applies the memory-card 4-try retry (with 50 ms pauses) to every device

- **Category:** 5
- **Locations:** `src/core/filemgr.cpp:468-477` (`fmRemove`); contrast the card-gated retries in the same file — `fmMkdirDev` breaks out immediately for non-card paths (`:396-408`), the open retries in `fmCopyFile` are gated on `fmIsMc` (`:352,361`)
- **Problem:** The file header scopes the retry rule to cards ("the first access after a card is detected answers 'changed card'"), and every other retry in the file is gated on `fmIsMc()`. `fmRemove` is not: on mass/SMB/HDD a genuinely failing `remove()`/`rmdir()` runs 4 attempts plus a 50 ms `fmPause()` after each try — including after the last one — and `fmDeleteTree` (`:479-498`) calls it once per tree entry, so a failing recursive delete pays ~150 ms of pure pause per entry before reporting `FM_ERR_DELETE`. INFERRED magnitude (structural: 3–4 pauses × 50 ms × entries).
- **Fix:** Gate the loop the same way `fmMkdirDev` does: one attempt for non-card paths, the 4-try loop only for `mc0:`/`mc1:`, and no pause after the final attempt.
- **Benefit:** Failed deletes on non-card devices return after one attempt instead of four plus ~150–200 ms; card behaviour unchanged.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — the asymmetry against the file's own other helpers is in the text)

### RIPTOPL-237 / Grid drawing re-walks the whole submenu list every frame just to learn the count and the selected index / Category 5

- **Locations:** `src/gfx/grid.cpp:503-508` (the `for (row = menu->item->submenu; …)` count/index walk at the top of `drawGrid`).
- **Problem:** `drawGrid` is a per-frame draw callback, and before it can compute the page it walks the entire submenu linked list once to count every row and find the selected row's index. On a page backed by a large library (thousands of games on HDD/BDM) that is an O(n) pointer chase per frame — 60 times a second — for two integers, after which the function walks the list twice more (to `gridSlideFrom` and to `first`, :538-551) for the covers it actually draws. The count/index pair is exactly the kind of value the menu system already tracks as the list is built and the cursor moves; recomputing it in the renderer is the per-frame waste pattern, distinct from the bounded walks to the visible page.
- **Fix:** Take count/index from the menu state the caller already maintains (or cache them keyed on the list head + selected item and invalidate when either changes — the slide state at :184-188 already keys cached state on the menu pointer the same way).
- **Benefit:** Per-frame cost on Grid pages becomes proportional to the visible page (≤ 24 covers) instead of the whole library (magnitude INFERRED — structural, no hardware measurement).
- **Effort:** M. **Risk:** Med (a stale cached count would mis-page; the invalidation key must be the list rebuild, not the frame). **Confidence:** High READ for the walk's existence and per-frame placement; INFERRED for user-visible magnitude.

### RIPTOPL-238 / ps1DropTwins recomputes the Ember rows' twin keys once per VCD row

- **Category:** 5 (performance)
- **Locations:** src/sources/cuesupport.cpp:1145-1175 (`ps1DropTwins`; inner call at :1158)
- **Problem:** The twin match is a full VCD×CUE cross product. `ps1TwinKey(rows[j].name)` — a strlen plus prefix/disc-ID scan — is recomputed for every (i, j) pair, i.e. up to `vcdCount` times per CUE row, although a row's key never changes during the pass. At the list caps (both halves can reach four figures) that is millions of redundant key derivations on a refresh path, on top of the strcasecmp the algorithm actually needs.
- **Fix:** Derive each row's twin key once into a scratch pointer array before the cross product and index it in both loops; free it with the existing `drop` array.
- **Justification (fix adds a scratch array):** it converts the key-derivation work from O(n·m) to O(n+m); the array is one pointer per row, freed on the same path as `drop`.
- **Benefit:** Refresh-time twin filtering stops re-deriving identical keys; structural saving grows with library size (magnitude INFERRED — no hardware timing available).
- **Effort:** S · **Risk:** Low · **Confidence:** High for the redundant computation (READ); Low for user-visible magnitude (INFERRED)

### RIPTOPL-239 / cheatConfigLoadSelections re-normalizes both cheat names for every (saved × cheat) pair

- **Category:** 5 (performance)
- **Locations:** `src/core/cheatconfig.cpp:53-60` (`cheatConfigNamesMatch` normalizes both names on every call), `:44-50` (`cheatConfigIsMasterCode` normalizes again), `:77-89` (the load loops: up to `MAX_CODES` saved keys × `MAX_CODES` cheats, calling both of the above per pair — up to three ≤128-char normalizations per pair; `MAX_CODES` = 250 per `include/cheatman.h:40`)
- **Problem:** The worst case is ~187k name normalizations for a single `cheatConfigLoadSelections` call. The common case is far smaller — the saved-key loop breaks at the first missing key (`:79-80`), so real configs with a handful of picks pay little — but a config carrying many `$CheatSelNNN` keys with few matches pays the quadratic cost on every per-game config load. Magnitude INFERRED (structural count, no timing possible in this VM); mechanism READ.
- **Fix (words only):** Normalize each cheat name once per load into a reused scratch table, and each saved name once per key, then compare the normalized strings directly; drop the per-pair calls to `cheatConfigIsMasterCode`/`cheatConfigNamesMatch` in the inner loop.
- **Benefit:** Selection restore goes from O(saved × cheats) normalizations to O(saved + cheats).
- **Effort:** S · **Risk:** Low · **Confidence:** Medium (mechanism READ, magnitude INFERRED)

### RIPTOPL-240 / CIS dump formats hex dumps and text decodes for a trace callback that is a no-op at the only production call site

- **Category:** 4 (size/memory waste) / 3
- **Locations:** `modules/pccf/pcdiag_cis.c:159-179` (per-tuple hex-line construction: hex table, `line[49]` formatting loop), trace-only decode branches in `describe` at `:47-75` (MANFID and VERS_1 text extraction), `tuple_name` string table at `:6-41`; sole production caller passes `quiet_trace`: `modules/pccf/pccf_hw.c:56-64`. No other caller exists in-tree (grep-verified, tests included).
- **Problem:** `pcdiag_cis_dump_info` does real per-byte formatting work — and carries ~36 tuple-name strings plus decode logic — exclusively to feed its `trace` parameter. The only production caller supplies a function that discards its arguments, so on real hardware every CIS parse at every PCCF start formats output nobody can ever read, and the strings occupy resident IOP memory in both pccf builds. The parsing side effects that matter (`function`, `config_base`, `register_mask`, `memory_index_zero`, `malformed`, `complete`, `linked`) are interleaved with, but separable from, the trace-only branches.
- **Fix (words only):** Strip the trace-only formatting (hex-dump loop, VERS_1/MANFID text decode, tuple-name table) from the production parse, keeping `describe`'s info-extracting branches (FUNCID / CONFIG / CFTABLE); keep the verbose dumper, if wanted, in the tests/diagnostic tree where a real trace sink exists.
- **Benefit:** Smaller resident driver and a CIS parse that does only the work its result structure needs.
- **Effort:** M · **Risk:** Low–Med (must preserve `describe`'s `malformed`/info assignments exactly; host-testable via `tests/pccf`) · **Confidence:** High, READ.

### RIPTOPL-241 / A register-sampling diagnostic (`pcdiag_cf_probe`) runs on every production start, acting only as a gate

- **Category:** 8 (half-finished/redundant feature) / 3
- **Locations:** `modules/pccf/pcdiag_cf.c:11-72` (whole function); sole production caller `modules/pccf/pccf_hw.c:283`, passing `quiet_trace` (`pccf_hw.c:56-59`) as its trace callback; file header `pcdiag_cf.c:1-9` describes it as a DIAG4 observation pass ("register reads only; sector access was not attempted").
- **Problem:** The probe's stated purpose is diagnostic observation — 3 samples × 2 selector spaces × 8- and 16-bit register reads, with waits of 500 ms + 2×100 ms — and every trace string it emits goes to a no-op callback in both shipped drivers (and to the discarding `pccardTrace` stub in `modules/pccf/main.c:19`). Its only production effects are (a) re-checking `pcdiag_cis_cf_candidate`, which the caller already established in `read_cf_profile`, (b) a COR-index gate, and (c) ~700 ms of settle delay. A 72-line diagnostic harness is being carried in the resident IOP driver — in both the GUI and game builds (`ps2.yaml` lists `pcdiag_cf.c` in `pccf_bd` and `pccf_game`) — to perform one register comparison.
- **Fix (words only):** Replace the call with the two parts that carry production weight — an explicit settle wait and the COR readback check (index 0, no soft-reset) — and move the sampling probe to the diagnostic build/tests where its traces go somewhere.
- **Benefit:** Removes a full diagnostic file from two resident drivers while keeping the COR gate and reset-settle behaviour that startup actually depends on.
- **Effort:** M (the settle wait must be preserved deliberately, not as a side effect) · **Risk:** Med (the 500 ms post-reset wait may be load-bearing for real CF bridges — the fix names it explicitly for that reason; hardware confirmation is the user's, as always) · **Confidence:** High that the traces are discarded and the gate is the only logic consumed (READ); Med on which waits are hardware-load-bearing (INFERRED).

### RIPTOPL-242 / deinit() and deinitEx() are the same ~85-line teardown written twice / Category 2

- **Locations:** `src/app/opl.cpp:1278-1370` (`deinitEx`) and `src/app/opl.cpp:1372-1455` (`deinit`).
- **Problem:** A direct diff of the two bodies shows they are line-identical except for the signature, the per-module cleanup leg (deinitEx's spared-mode loop vs deinit's `deinitAllSupport` call) and deinitEx's two extra `launchDiagMark` calls. The entire shared sequence — the ~40 lines of explanatory comments, fade/banner, `cacheShutdownArtLoads()`, `bgmQuiesce()`, the bounded `ioBlockOpsTimed` drain, `cacheEnd`, pad unload, and the `audioEnd/ioEnd/guiEnd/menuEnd/lngEnd/fntEnd/thmEnd/rmEnd/menuWatchStop/configEnd` tail — exists in two copies. This is exactly the file region where ordering bugs are load-bearing (the comments document three separate order-sensitive incidents: #382 BGM race, the art-worker join, the drain-unit mistake); any future ordering fix must be made twice, identically, or the Neutrino handoff and the normal launch/exit paths silently diverge.
- **Fix:** One shared teardown body taking the cleanup leg (and the extra diag marks) as its only variation; `deinit` and `deinitEx` become thin wrappers that supply it. No behaviour change intended.
- **Benefit:** ~80 lines deleted; one place to fix teardown ordering for every launch/exit path.
- **Effort:** M. **Risk:** Med (teardown ordering is hardware-sensitive; the merge must be diff-verified line by line). **Confidence:** High (READ — diff performed).

### RIPTOPL-243 / Two ministack implementations are maintained in parallel: smap_udpbd's monolith vs udpfs_ministack's split files

- **Category:** 2 (duplication)
- **Locations:** `modules/network/smap_udpbd/src/ministack.c` (275) against `modules/network/udpfs_ministack/src/ministack_eth.c` (28) + `ministack_ip.c` (91) + `ministack_udp.c` (68) + `ministack_arp.c` (211): the same `eth_packet_init`/`ip_packet_init`/`udp_packet_init`, `ip_checksum`, `eth/ip_packet_send_ll`, `udp_bind`, `arp_add_entry`, `handle_rx_eth/ipv4/udp/arp`, `ms_ip_set/get_ip` and the compiled-in default `ip_addr`, in two dialects (FIFO-register reads in the monolith, pre-read header callbacks in the split — the RX plumbing genuinely differs; the packet construction, checksum, ARP table logic and send path do not). `parse_ip` is additionally duplicated verbatim at `modules/network/smap_udpbd/src/main.c:383-407` and `modules/network/udpfs_ministack/src/main.c:205-229`.
- **Problem:** The monolith's own comment at `ministack.c:139-148` documents the cost: its `arp_add_entry` carried a slot-test bug the split copy never had, so UDPBD broadcast every frame while UDPFS did not — one fix, one copy, silent divergence. Distinct from RIPTOPL-127 (the three `smap.c` driver copies) and from the round-1/2 UDPFS header findings (the bd/fhi/ioman copies of `udpfs_core.*`, a different duplication axis).
- **Fix:** Extract the shared packet-build/checksum/ARP-table core into one source both IRXs compile (the modules already share `udpfs_shared/` by directory for UDPRDMA), leaving each stack only its RX-delivery dialect; deduplicate `parse_ip` the same way.
- **Benefit:** ARP/IP/UDP construction fixes land once; the documented broadcast-everything class of one-sided bug stops recurring.
- **Effort:** L · **Risk:** Med (two IRXs load in different contexts; the split stack's ARP gained conflict-detection/replacement policy the monolith lacks, which must be preserved, not flattened) · **Confidence:** High (READ, function-level comparison)

### RIPTOPL-244 / The two ds34 EE client libraries are one library written twice

- **Category:** 2 (duplication)
- **Locations:** `modules/ds34bt/ee/libds34bt.c` (181 lines) vs `modules/ds34usb/ee/libds34usb.c` (153 lines) — same file skeleton throughout: `rpcbuf[64]`, `inited` flag, identical bounded-bind `*_init` incl. the same three-line comment (`libds34bt.c:24-29` / `libds34usb.c:24-29`), `*_deinit`, `*_reinit_ports`, `*_get_status`, `*_set_rumble`, `*_get_data`, `*_reset` differing in name prefix, RPC id and a handful of command numbers
- **Problem:** Round 4 mapped the four *IOP-side* ds34 copies in depth; these two *EE-side* RPC clients are a fifth/sixth copy of each other. Every function is the same guard-marshal-call-unmarshal shape, so a fix like the bounded bind had to be written (and was written, comment included) twice, and the copy-out-on-failure defect in R5-M6-17 exists in both for the same reason. The genuinely different content is per-bus: the bind RPC id, the command enum, and three extra calls on each side (`init_charging`/`get_version`/`get_features` vs `set_bdaddr`).
- **Fix:** Factor the common client core (bind-with-timeout, rpcbuf, the shared commands) into one shared EE source/header parameterised by bind id and command enum, leaving the bus-specific calls in thin per-bus files. This is the rare dedup where the two copies are in the same layer, same repo area, and already byte-similar in structure.
- **Benefit:** Roughly 120 lines and one whole maintenance surface removed; the next RPC-client fix lands once.
- **Effort:** M · **Risk:** Med (both GUI pad paths link these; a botched merge breaks pad support in the loader UI, IOP side untouched) · **Confidence:** High (READ)

### RIPTOPL-245 / Two bespoke TOML scanners in one file, with divergent rules for the same grammar

- **Category:** 2 (with Cat 3 character)
- **Locations:** `src/platform/sysneutrino.cpp:303-380` (`sysTomlTornTailCut`) and `src/platform/sysneutrino.cpp:522-635` (`sysTomlRewriteSmbArgs`)
- **Problem:** sysneutrino.cpp hand-rolls the TOML lexer twice: once to validate/repair a torn file tail (comments, all four string forms including multi-line strings, bracket depth, line-completeness), and once to rewrite module arg strings (comments, basic and literal strings — but multi-line strings are a hard -1 there). Each is ~80–115 lines of character-loop state machine, and any TOML corner case (escapes, triple quotes, comment-in-string) must be reasoned about, and fixed, in both — they already disagree about multi-line strings, which is the drift this duplication invites. Together with `sysTomlEscape` and the `strstr("\"ip=")` shortcut in the third sync function, one file carries four separate stances on how a TOML file is read.
- **Fix:** Factor the shared token walk (skip comment / skip-or-copy string / track depth) into one scanner both transforms drive with a callback, or reduce the torn-tail checker to the subset the rewriter's lexer already proves. Deletion-preferred: the torn-tail function's full grammar is more than its job needs — it only has to find the last complete point within 32 bytes of the end of a file this program itself writes.
- **Benefit:** One TOML lexer to keep correct instead of two divergent ones, in the file that rewrites the user's Neutrino configs in place.
- **Effort:** L / **Risk:** High (both functions rewrite user files; behaviour must be preserved byte-for-byte, host-testable via the existing test pattern) / **Confidence:** High (READ)

### RIPTOPL-246 / MD4 and the LM/NTLM auth helpers exist as two divergent copies (smbinit vs smbman-ra)

- **Category:** 2 (duplication)
- **Locations:** `modules/network/smbinit/md4.c` (175) vs `modules/network/smbman-ra/md4.c` (182) — 55 diff lines (whitespace-normalised), load-bearing divergence: smbinit uses a stack `buffer[128]`, smbman-ra a union'd aligned buffer, const differs on `transform`'s parameter; `modules/network/smbinit/smbauth.c` (139) vs `modules/network/smbman-ra/auth.c` (93) — the same `LM_Password_Hash` / `NTLM_Password_Hash` / `LM_Response` trio in both (the 255-char password bound was added to both copies — the fix-propagation cost already paid twice), smbinit adding `GenerateLMHashes`/`SmbInitHashPassword` on top.
- **Problem:** This is RIPTOPL-056's proven failure shape (the two DES copies, one patched one not) repeated one layer down: the hash and response code that SMB1 authentication stands on is maintained in two places, in two modules that must agree byte-for-byte on the wire. Round 3 filed the DES pair; the MD4/auth pair beside it was not filed.
- **Fix:** Share one MD4 + auth source by inclusion between the two modules (the pattern the tree already uses for `httpstream.inc` and `unfs4.c`), keeping each module's own entry points (`SmbInitHashPassword` stays smbinit-only).
- **Benefit:** The next auth fix lands once; the two stacks cannot silently diverge on hash behaviour again.
- **Effort:** M · **Risk:** Med (two build contexts: EE-linked smbinit vs IOP smbman-ra — signatures/types differ slightly and must be reconciled) · **Confidence:** High (READ, diff-measured)

### RIPTOPL-247 / tar.cpp's four find functions are one lookup written four times / Category 2

- **Locations:** `src/platform/tar.cpp:464-499` (`tarFind`), `:501-529` (`tarFindPrefix`), `:531-553` (`tarFindExact`), `:555-571` (`tarFindPrefixExact`).
- **Problem:** The four functions share the same entry-walk loop, the same `./` + `ART/|CFG/|CHT/` query cleaning, and the same basename extraction; they differ only in (a) prologue — lazy-load/`s_inactive` handling vs require-exact — and (b) the final comparison (full-name vs prefix). Note on scope: this is **not** the cache/parse path cleared in round 2 (`tarReadCache`/`tarParseFile`); the find family was outside that clearance, which is why this is filed as new substance.
- **Fix:** One internal walk parameterised by exact/prefix and by the prologue result; the four public names become one-line wrappers preserving every current contract (including `tarFind`'s side effect of discarding an exact index).
- **Benefit:** ~115 lines reduced to ~50; the name-cleaning rules (a historically bug-prone area in this file) get a single home.
- **Effort:** S. **Risk:** Low (behaviour-preserving if the `tarFind` exact-discard side effect is kept). **Confidence:** High (READ).

### RIPTOPL-248 / vmc_groups: 97 hand-written `count_*` variables are the boilerplate that already produced one out-of-bounds walk

- **Category:** 2 / 3
- **Locations:** `src/core/vmc_groups.cpp` — one `static const size_t count_XEBP_*` per array throughout `:15-1350` (97 of them), consumed by the table at `:1352-1449`; the failure mode is documented in the file itself at `:1152,1158,1164` ("was sizeof(titles_XEBP_100_71): copy-paste numerator overstated the count and walked past the array")
- **Problem:** Every group pays three hand-maintained names (array, count variable, table row) that must agree. The in-file comments record that this exact pattern already shipped a wrong-numerator count that walked past an array end, in at least four groups. The count variables carry no information — each is `sizeof(array)/sizeof(array[0])` of the array directly above it.
- **Fix:** Build the table rows with a macro (or an inline helper) that takes the array name and computes its count at the table site, and delete all 97 `count_*` variables. The group-ID strings stay as they are.
- **Benefit:** ~97 lines deleted; the wrong-count class of bug becomes impossible rather than comment-documented.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-249 / `bd_defrag_read` and `bd_defrag_write` are the same 45-line traversal pasted twice

- **Category:** 2 (duplication)
- **Locations:** `modules/bdm/libbdm/src/bd_defrag.c:8-52` and `:54-101`.
- **Problem:** The two functions are line-for-line identical — fragment search, clipping, buffer/sector advancement — except for the single `bd->read` vs `bd->write` call and variable names. Any future bounds or clipping fix must be made (and can be forgotten) in two places; the file is 101 lines for one algorithm. This is first-party libbdm code, not a vendored copy.
- **Fix (words only):** Collapse to one static traversal taking the operation (function pointer or a read/write flag), with the two public entry points as thin wrappers preserving the existing ABI.
- **Benefit:** ~45 fewer lines; fragment-clipping logic exists exactly once.
- **Effort:** S · **Risk:** Low (mechanical merge; both bodies verified identical in structure at the pin) · **Confidence:** High, READ.

### RIPTOPL-250 / Streaming ring-buffer drain is written twice: `ReadSectors` (memcpy) and `ReadSectorsEE` (SIF DMA) are one algorithm with two bodies

- **Category:** 2 (duplication)
- **Locations:** `modules/iopcore/cdvdman/streaming.c:162-245` (`ReadSectorsEE`) and `:247-301` (`ReadSectors`); both called only from `sceCdStRead` (`:411-455`) depending on the buffer's EE/IOP bit.
- **Problem:** Both functions implement the identical two-segment ring drain — wrap segment when `StWritePtr <= StReadPtr && StStreamed > 0`, then linear segment when `StReadPtr < StWritePtr`, with the same clamping (`min(segment, maxcount - result)`), the same pointer wrap-to-0, and the same `StStreamed` accounting. They differ only in the transfer step (in-place `memcpy` under suspended interrupts vs building ≤2 `SifDmaTransfer_t` entries and doing the DMA bookkeeping after). Every fix to the ring arithmetic must be made twice, in bodies long enough that a one-sided fix is easy to miss — the file's own history (RIPTOPL-045 and the #185 fill-restart fix) is a sequence of exactly such single-spot stream fixes.
- **Fix:** Factor the shared part — computing the (up to) two segment descriptors {src, length} and applying the resulting read-pointer/streamed update — into one helper, leaving each function only its transfer step (DMA submission for EE, `memcpy` for IOP). Justification: the helper *replaces* two copies of the arithmetic; it is a net deletion of ~40 lines, not a new abstraction layer.
- **Benefit:** Ring-buffer correctness lives in one place; the EE and IOP stream paths cannot drift apart.
- **Effort:** M · **Risk:** Medium (interrupt-context code; the EE variant's post-DMA state update ordering must be preserved exactly) · **Confidence:** High (READ)

---

### RIPTOPL-251 / The bounded "poll identity until budget expires" loop is written three times in bdmboot.cpp

- **Category:** 2
- **Locations:** `src/sources/bdmboot.cpp:180-189` (known-type literal-slot poll), `src/sources/bdmboot.cpp:216-225` (per-tier literal-slot poll, inside `resolveLiteralMassSlot`, `src/sources/bdmboot.cpp:151`), and the larger scan/poll loop in `resolveWithBudget` (`src/sources/bdmboot.cpp:372-468`)
- **Problem:** One file implements the same pattern — snapshot `GetTimerSystemTime()`, probe, compare elapsed against a millisecond budget, `DelayThread(100 * 1000)`, repeat — three times, with the escalation/budget bookkeeping re-derived at each site. This file's own comments record two bugs that lived in exactly this pattern (a u32 timer snapshot that broke post-boot resolves, and fallback ordering that latched the wrong device); each fix had to be reasoned about per copy, and the copies' budgets/escalation rules have already drifted apart in structure if not yet in effect.
- **Fix:** Extract the one poll-until helper (probe callback + budget + escalation hook) and let the three call sites supply their probe; delete the two inline re-implementations.
- **Benefit:** One place to fix timer/budget defects in the boot-device resolver; roughly 60–90 lines removed from the most failure-prone boot path in the tree.
- **Effort:** M · **Risk:** Med (boot-path code; the three sites' subtle budget differences must be preserved as parameters, not flattened) · **Confidence:** High, READ.

### RIPTOPL-252 / ranet: the "RAA1 OK" reply-header parse is implemented twice, with a third partial re-parse beside one copy

- **Category:** 2
- **Locations:** `src/sources/ranet.cpp:612-665` (`Ranet::askPc`: 5-number `sscanf` with 3-number fallback, title trailing-space trim, then a separate `sscanf(rx_ + 8, "%d %d", &total, &chunks)` re-parse at `:649`) and `src/sources/ranet.cpp:1051-1068` (`Ranet::xeraboraIdentify`: the same 5-vs-3-number parse and title trim again)
- **Problem:** The protocol's two reply shapes (newer 5-number, older 3-number, title may itself start with digits) are decoded independently in two functions, and `askPc` additionally re-parses fields it already extracted because the two extractions were never unified. Any protocol-shape fix has to be made (and can be missed) in two places; the copies have already drifted in what they do with the result (`askPc` validates totals via `raExpectedChunkLength`, `xeraboraIdentify` does not look at bytes/chunks at all).
- **Fix:** One static parse helper returning bytes/chunks/counts/title-offset (or failure), called by both; delete the second and third `sscanf` passes.
- **Benefit:** ~30 lines removed; one place owns the reply-shape rules.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-253 / The MMCE candidate-slot scan is written twice in one file / Category 2

- **Locations:** `src/sources/mmcesupport.cpp:176-198` (`sendGameID`: `cands[3]` / `tried[2]` / slot-parse / presence-devctl loop) and `src/sources/mmcesupport.cpp:561-587` (`resetCards`: the same loop).
- **Problem:** Both functions build the identical candidate list — resolved device root first, then `"mmce0:/"`, `"mmce1:/"`, skipping NULL/short entries, parsing the slot digit from `cands[c][4]`, de-duplicating via `tried[2]`, and probing presence with `fileXioDevctl(cands[c], 0x1, …)` — as two hand-maintained copies ~25 lines each. They have already drifted in the small (the send version copies the winning candidate back into `mmceDevice` and applies the VMC-slot mask; the reset version counts resets and waits out the busy bit), which is how this pattern rots: a future fix to candidate eligibility (a third slot form, a presence-probe change) must be made twice, in a file whose other probes (`detectSlot` :665-676, `setPrefix` :678-731, `getDeviceRoot` :521-545) are further variations on the same question.
- **Fix:** One static helper that invokes a callback (or fills a small array) for each present candidate slot; both callers keep their per-slot bodies. Deletion of one copy of the enumeration, no behaviour change.
- **Benefit:** One enumeration to fix; the two launch/reset paths stop drifting apart.
- **Effort:** S. **Risk:** Med (both copies sit on launch paths; the helper must preserve candidate order and the send path's fallback-copy exactly). **Confidence:** High (READ).

### RIPTOPL-254 / ratls_ps2: `raHttpsGet` and `raHttpsDownload` duplicate the whole DNS → socket → connect → deadline preamble

- **Category:** 2
- **Locations:** `src/sources/ratls_ps2.cpp:205-252` (`raHttpsGet`) and `:257-304` (`raHttpsDownload`) — identical clock check, NIC-reserved check, `gethostbyname` handling, `sockaddr_in` fill, socket creation, connect failure path, and post-connect deadline check (~30 lines each), differing only in the anchor arrays and the final `raTlsRequest` / `raTlsDownload` call
- **Problem:** Two copies of the connection preamble in one 304-line file, gated by different `#ifdef`s (RETROACHIEVEMENTS / RIPTOPL_UPDATER), so a fix to the shared part (e.g. the deadline or address handling) can land in one build flavour and not the other.
- **Fix:** One static helper that takes the start time and returns a connected socket (or the existing RATLS_ERR_* code), called by both entry points; the per-caller anchors and request call stay.
- **Benefit:** ~25 lines removed; one preamble to fix.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-255 / The MX4SIO/MMCE quiesce block is copy-pasted verbatim in both PS1 launch legs

- **Category:** 2
- **Locations:** `src/sources/bdmlaunch.cpp:103-108` (`BdmLauncher::launchCue`) and `src/sources/bdmlaunch.cpp:197-202` (`BdmLauncher::launchVcd`)
- **Problem:** The identical gate — `if (bdmDeviceType == BDM_TYPE_SDC) { if (!cacheAbortMmceImageLoadsTimed(500)) { warning(...); return; } }`, including its rationale comment — exists twice in one file, in the two launch legs that must stay in lock-step (both hand off to an external PS1 core over the shared SIO2 bus). The 500 ms settle budget and the error string are exactly the kind of constant that will be tuned once and forgotten in the other copy. (Distinct from RIPTOPL-018, which files the per-source PS2 launch legs across five files; this duplication is inside one file, between the two PS1 legs.)
- **Fix:** One small private helper on `BdmLauncher` (e.g. `quiesceMmceForPs1Handoff`) called from both legs; delete the second copy.
- **Benefit:** One place for the settle budget/failure behaviour; ~6 lines removed.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-256 / Two near-identical icon+caption draw functions in uidraw.cpp

- **Category:** 2 (duplication)
- **Locations:** src/gfx/uidraw.cpp:12-34 (`guiDrawIconAndText`), src/gfx/uidraw.cpp:36-59 (`guiDrawIconAndLiteralText`)
- **Problem:** The two functions are line-for-line the same body (texture fetch, width from aspect, draw, x advance, string render); the only difference is the last argument: `_l(textId)` vs a caller-supplied `const char *`. Every fix to icon metrics or the no-texture fallback must be made twice (the in-file comment at :69 already treats the pair as one behaviour).
- **Fix:** Keep one body taking `const char *`; make `guiDrawIconAndText` a thin wrapper that resolves `_l(textId)` and forwards. All existing call sites (dia.cpp, themeelems.cpp, guimodal.cpp) keep their signatures.
- **Benefit:** ~20 lines removed; one implementation of the hint/icon layout math.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — bodies diffed visually; both call sets enumerated by grep)

### RIPTOPL-257 / cacheGetTextureEx repeats its ~20-line in-flight request adoption block verbatim

- **Category:** 2
- **Locations:** `src/gfx/texcache.cpp:259-284` (the `*cacheId`-keyed fast path) and `src/gfx/texcache.cpp:314-339` (the reunion-by-value loop)
- **Problem:** When a slot's request is still loading, both paths perform the identical adoption ritual — stamp `entry->lastUsed = guiFrameId`, load `entry->qr`, adopt `req->focusEpoch = gArtW.focusEpoch`, and promote on priority unless the request is SIO2 while navigation is active — including the same explanatory comment copied with it. The two copies guard the cache's most race-sensitive invariant (a queued request must be re-stamped or the worker cancels it as stale, the failure narrated at length further down the same function), so a future fix to the ritual applied to one path and not the other reintroduces that class of bug on exactly one of the two routes a row can take.
- **Fix:** Extract the ritual into one small helper both paths call (the reunion copy additionally assigns `*cacheId`/`*UID` before calling it). No new state, no new allocation.
- **Benefit:** One adoption ritual to fix and to audit in the per-frame art path.
- **Effort:** S / **Risk:** Med (per-frame hot path; behaviour must stay identical) / **Confidence:** High (READ)

---

### RIPTOPL-258 / Apps linked-list node allocation is copy-pasted twice, and its if/else collapses to a plain prepend

- **Category:** 2
- **Locations:** `src/sources/appsupport.cpp:675-691` (`addAppsLegacyList`) and `src/sources/appsupport.cpp:734-750` (`appScanCallback`)
- **Problem:** Both list builders contain the same ~15-line block: an if/else on whether the list is empty whose two branches do the *same* `malloc` and differ only in the `next` assignment — `NULL` in the empty branch, the old head in the other. But the old head *is* NULL in the empty case, so the branch structure encodes nothing: one prepend (`app->next = *appsLinkedList; *appsLinkedList = app;`) plus the shared NULL check covers both branches at both sites.
- **Fix:** Replace both blocks with the single prepend form (or one small node-allocating helper both call). Net deletion of roughly 20 lines across the two sites, no behaviour change — list order is identical because the empty-list prepend already produced `next = NULL`.
- **Benefit:** The allocation-failure handling for app discovery lives in one shape instead of four branch copies.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ)

### RIPTOPL-259 / Two hand-copied 8-digit hex parsers for module-load arguments already disagree (lowercase)

- **Category:** 2 (duplication)
- **Locations:** `modules/iopcore/igrmbox/igrmbox.c:151-167` (`mbHex8` — accepts `0-9`, `A-F` only); `modules/iopcore/freezediag/freezediag.c:24-45` (`parse_hex8` — additionally accepts `a-f` at `:36-37`)
- **Problem:** Both new IOP diag modules parse the same kind of argument (an 8-hex-digit address string built by ee_core) with their own copy of the same 17-line loop, and the copies have already drifted: freezediag accepts lowercase hex, igrmbox rejects it (returns 0 → `_start` refuses the load and the in-game menu silently never appears). Today ee_core generates uppercase (`ee_core/src/igrmbox.c:48` — `"0123456789ABCDEF"`), so the divergence is latent; it is also exactly how duplicated parsers announce themselves. Neither copy checks that the string is actually 8 characters before reading `s[0..7]`.
- **Fix:** Keep one `static inline` copy in the shared header both module families already draw from (`modules/iopcore/common/`), accepting both cases, and delete the other. Net deletion across the tree.
- **Benefit:** One parser to fix, one acceptance rule for ee_core's address arguments, and the lowercase footgun closes before a generator change finds it.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-260 / The `__RTL` renderString is a second, ~150-line text renderer that the live build can never select / Category 1

- **Locations:** `src/gfx/fntsys.cpp:809-961` (`isRTL`, `isWeak`, `FontSystem::renderSubRTL`, and the `#else` branch `renderString`); the live branch is `src/gfx/fntsys.cpp:709-807`.
- **Problem:** Two complete `renderString` implementations coexist behind `#ifndef __RTL` / `#else`. Nothing in the live build defines `__RTL`: `ps2.yaml` (the build definition that actually builds the program) contains no `__RTL` in any target's `defines:` list (grep-verified across the file); the only place that ever added `-D__RTL` is the legacy `Makefile:227-228`, which is already filed as unbuildable (RIPTOPL-003). The dead branch is also functionally superseded: the live branch now shapes Arabic/Persian itself via `txtShape` (`src/gfx/fntsys.cpp:742-747`), which is the stated reason the shaping exists in both `renderString` and `calcDimensionsLocked`. So ~150 lines of renderer — including its own kerning, clipping and RTL-run logic that must be read and kept consistent at every renderer change — are maintained for a configuration no shipped build can produce.
- **Fix:** Delete the `#else` branch, `isRTL`, `isWeak` and `renderSubRTL` from this file (the matching `__RTL` declarations in `include/fontsystem.h:116` and the branch in `src/ui/gui.cpp:157` are outside this section but are the same deletion). If RTL-via-bidi is ever wanted again, the `txtShape` path is where it now lives.
- **Benefit:** One renderer instead of two; every future renderString fix stops needing a second, untestable copy kept in sync by hand.
- **Effort:** S. **Risk:** Low (the branch is unreachable in the live build; the risk is only if an out-of-tree build still defines `__RTL` through the dead Makefile). **Confidence:** High READ for the code and the ps2.yaml/Makefile grep; the "no out-of-tree user" part is INFERRED.

### RIPTOPL-261 / SMB1 `smb_ReadAndX` carries a permanently dead second receive implementation (`USE_CUSTOM_RECV` is unconditionally defined), plus the `rhdrlen` plumbing that only it used

- **Category:** 1 (dead code)
- **Locations:** `modules/iopcore/cdvdman/smb.c:23` (`#define USE_CUSTOM_RECV 1`, unconditional — repo-wide grep finds no other definition, no `#undef`, no Makefile flag), `:920-922` (`int padding;` declared only for the dead branch), `:997-1020` (the `#else` body of `smb_ReadAndX`), `:376` (`GetSMBServerReply(..., int rhdrlen)`) whose only non-zero caller is the dead branch at `:998` (all seven live callers — `:509, :628, :703, :787, :850, :1091` — pass 0).
- **Problem:** ~24 lines implement the pre-custom receive path (reply via `GetSMBServerReply` + separate padding/data `RecvData` calls) that can never compile in any in-repo build. It is also the *unhardened* twin of the live path: it trusts the response's `DataLength` with none of the live branch's DataOffset/DataLength/reply-size validation (the hardening the live branch's long comments document). Dead code that looks like a plausible receive path — and is the less safe of the two — is exactly the copy a future editor can resurrect by deleting one `#define`.
- **Fix:** Delete the `#else` branch, the `#ifdef` scaffolding in `smb_ReadAndX`, and the now-constant `rhdrlen` parameter of `GetSMBServerReply` (with its `size = (rhdrlen == 0) ? ... : rhdrlen` branch). Pure deletion; the live path is untouched.
- **Benefit:** One receive implementation to audit; the unvalidated variant stops existing as a one-line resurrection away.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-262 / BDM device computes `g_bd_sectors_per_sector` on every attach and never reads it — disc reads hardcode ×4 (512-byte sectors) instead

- **Category:** 1 (dead code) with a Category 6 consequence
- **Locations:** `modules/iopcore/cdvdman/device-bdm.c:22` (declaration, initialised 4), `:106` (the only write, `(2048 / bd->sectorSize)` in `bdm_connect_bd`), `:234` and `:244` (`DeviceReadSectors` calling `bd_defrag(..., lsn * 4, ..., sectors * 4)` / bounce count `4`).
- **Problem:** A repo-wide grep finds exactly two references to `g_bd_sectors_per_sector`, both in this file: declaration and assignment. It is write-only state. Meanwhile the read path converts CD sectors (2048 B) to device sectors with a literal `* 4`, which is only correct when `bd->sectorSize == 512`. That premise is not universal in-tree: `IEEE1394_bd` takes `sectorSize` from the device's block length (`modules/ilink/IEEE1394_bd/src/scsi.c:264-282`), the ATA BDM driver can report an ATA logical sector size (`modules/hdd/atad/src/ps2atad.c:1407`), UDPBD takes it from the server (`modules/network/smap_udpbd/src/udpbd.c:311`), and `bd_cache.c:210` explicitly special-cases `sectorSize != 512`. So the variable exists because the conversion is device-dependent, the conversion ignores it, and the two silently disagree exactly in the cases the variable was computed for. (Whether a non-512 device currently reaches this cdvdman variant is INFERRED — the dead state and the hardcoded conversion are READ.)
- **Fix:** Choose one truth: either use `g_bd_sectors_per_sector` in `DeviceReadSectors` in place of the literal 4s, or delete the variable and state the 512-only assumption where the device is matched (the PCCF branch at `:103` already validates `sectorSize == 512` explicitly, which is the honest form of the assumption). Deleting the variable is the LESS-NOT-MORE default if 512 is in fact guaranteed for every attachable device.
- **Benefit:** Removes dead per-attach state and the trap where a future non-512 device reads at 4×/¼× the correct LBA.
- **Effort:** S · **Risk:** Low · **Confidence:** High for dead/inconsistent (READ), Medium for live device impact (INFERRED)

### RIPTOPL-263 — `mmce_sio2_rx_dma` (66 lines) has no caller anywhere in the tree — dead code in RiptOPL

- **Category:** 1 (dead code)
- **Locations:** `modules/mmce/mmceman/src/mmce_sio2.c:236-302` (function, self-described "Simplified read for sectors / MMCEDRV"); prototype `modules/mmce/mmceman/include/mmce_sio2.h:40`
- **Problem:** READ, caller set established by tree-wide grep at the pin: the only two hits for `mmce_sio2_rx_dma` in the whole repository are the definition and its prototype. Unlike the sister project where an unbuilt driver file still references it, this tree's `modules/mmce/mmceman/src/` contains no `mmcedrv.c` at all — every live transfer goes through `tx_rx_pio` / `rx_mixed` / `tx_mixed`. The function is maintained-looking dead weight (it even participates in the `use_alarm` guard pattern, R5-M4-06, which future readers must keep consistent for a path that never runs).
- **Fix (words):** Delete the function and its prototype.
- **Benefit:** 66 lines plus a prototype gone; one fewer transfer path to audit for SIO2 timeout/alarm behaviour.
- **Effort:** S · **Risk:** Low (grep-verified zero callers; if an MMCEDRV client is ever revived it returns with that client) · **Confidence:** High (READ + grep).

### RIPTOPL-264 — CRC7 "RAM table" mode is unreachable: both preprocessor tests use a macro name that does not exist, so the config's selected mode silently isn't

- **Category:** 1 / 8 (dead branch + mislabelled configuration)
- **Locations:** `modules/mx4sio_bd/src/spi_sdcard_crc7.c:7` (`#if SD_CRC7_MODE == CRC7_MODE_RAM_TABLE`) and `modules/mx4sio_bd/src/spi_sdcard_crc7.h:10` (same test); the config defines only `SD_CRC7_MODE_RAM_TABLE` (=2) at `modules/mx4sio_bd/src/spi_sdcard_driver_config.h:15` and selects it at `:19-23` (`SD_CRC7_MODE (SD_CRC7_MODE_RAM_TABLE)` when not building a bootloader)
- **Problem:** READ (preprocessor semantics: an undefined identifier in `#if` evaluates to 0). The config selects RAM-table mode (2), the outer test at crc7.c:5 correctly enters the table branch, but the inner test compares 2 against the undefined `CRC7_MODE_RAM_TABLE` (0) and falls into the `#else` ROM-table branch at crc7.c:27. Consequences: (1) the RAM-table array and `crc7_generate_table()` (crc7.c:8-26) are never compiled — dead code carrying a maintenance promise; (2) the header never declares `crc7_generate_table`, and no caller exists anywhere (grep-verified), which is the only reason this works — if anyone "fixes" the typo alone, RAM mode activates with an uninitialised all-zero table and every command CRC breaks; (3) what actually ships is the 256-byte ROM table, which is arguably the better outcome — it just isn't what the config says.
- **Fix (words):** Delete the RAM-table branch (crc7.c:7-26), the config option, and the header's conditional declaration; state ROM-table mode plainly in the config. Net deletion, and it removes the fix-the-typo trap.
- **Benefit:** ~20 dead lines and one config lie removed; the mode the binary actually uses is the mode the config names.
- **Effort:** S · **Risk:** Low (the compiled behaviour — ROM table — is unchanged) · **Confidence:** High (READ; macro expansion is deterministic).

### RIPTOPL-265 / dev9.c's PCMCIA probe trio (~55 lines) is behind a plain `DEBUG` define that no in-repo build of this module ever sets

- **Category:** 1 (dead code)
- **Locations:** `modules/iopcore/cdvdman/dev9.c:77-78` (prototype), `:460-512` (`pcic_get_cardtype`, `pcic_get_voltage`, `pcmcia_device_probe`), `:563-565` (sole call site, also `#ifdef DEBUG`).
- **Problem:** The three functions compile only under `#ifdef DEBUG`. This file is built solely as `dev9.o` by `modules/iopcore/cdvdman/Makefile:99`, and that Makefile's debug switches define `DEV9_DEBUG` (`:59`) and `__IOPCORE_DEBUG` (`:95`) — never plain `DEBUG` (the one `-DDEBUG` in the tree, `modules/iopcore/cdvdfsv/Makefile:7`, belongs to a different module that does not compile this file). So in every build this repo can produce, the probe trio and its call site are preprocessed away, while still being maintained as if live (this is vendored PS2SDK heritage code, which explains, but does not un-dead, it).
- **Fix:** Delete the three `#ifdef DEBUG` blocks, or fold the probe under the module's actual `DEV9_DEBUG` switch if maintainers want it reachable. Deletion is the LESS-NOT-MORE default.
- **Benefit:** ~55 fewer lines of never-compiled code in a hardware driver that is otherwise read as authoritative.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the in-repo build claim (READ — Makefile grep); the functions could only revive under an out-of-repo build system defining `DEBUG`.

### RIPTOPL-266 / tools/lang_decompiler.py is dead: no caller, no docs, no test, and it runs on import

- **Category:** 1 (dead code), with a Cat 2 minor
- **Locations:** tools/lang_decompiler.py:1-60 (whole file; top-level statements at :16-60, no `__main__` guard, no shebang), duplicated helper at tools/lang_decompiler.py:4-14 vs tools/lang_compiler.py:34-43
- **Problem:** A repo-wide grep for `lang_decompiler` (workflows, Makefile, ps2.yaml, docs, tests, other tools, shell scripts) returns zero hits outside the file itself. It is the reverse of `lang_compiler.py` (built `lng/*.lng` → `lng_src/*.yml`), a one-off migration shape: the live pipeline goes the other way (`assets.py lang()` downloads `lng_src/` and compiles it), so running this script today would overwrite the downloaded upstream translations with decompiled output. It also executes at import (module-level `open('lng/tmpl/_base.yml')` with CWD-relative paths), and its `represent_str` is a verbatim copy of `lang_compiler.py`'s.
- **Fix:** Delete the file (60 lines). If a future .lng→YAML migration is ever needed, `lang_compiler.py` plus git history hold everything required to recreate it.
- **Benefit:** One less unmaintained entry point that, if run from the repo root, silently rewrites `lng_src/`; removes a second copy of the YAML representer to keep in sync.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (full-repo invocation grep; contrast lang_compiler, which assets.py invokes per language).

### RIPTOPL-267 / ziso.py: dead code and a dead option inside the live tool

- **Category:** 1 (dead code), with a Cat 2 minor
- **Locations:** tools/pc/ziso.py:44-47 (`hexdump`), tools/pc/ziso.py:369-402 (`load_sector_table`), tools/pc/ziso.py:349 (`-a` sets `DEFAULT_ALIGN`) vs tools/pc/ziso.py:223,227 (compress seeds `align` from `DEFAULT_ALIGN` then unconditionally overwrites it with `total_bytes // 2 ** 31`), tools/pc/ziso.py:55-58 (`lz4_compress_mp` duplicates the body of `lz4_compress` at :50-52)
- **Problem:** `hexdump` is never called anywhere and is Python-2 code — iterating `bytes` yields ints, so its `ord(i)` would raise `TypeError` if it ever were called. `load_sector_table` is never called; its own comment says "In future we will support NC". The documented `-a align` option is parsed into `DEFAULT_ALIGN` and then ignored: `compress_zso` always recomputes alignment from file size, so a user who follows `usage()` and passes `-a 6` for the "fast/large" alignment gets a file written with a different alignment than requested, with no notice. `lz4_compress_mp` is a 4-line copy of `lz4_compress` kept only because `Pool.map` needs a one-argument callable.
- **Fix:** Delete `hexdump` and `load_sector_table` (~47 lines); either honour `-a` in `compress_zso` (take the maximum of the requested and the size-forced alignment) or delete the `-a` parsing and its usage line; fold `lz4_compress_mp` into a one-line lambda/partial at the `pool.map_async` call site.
- **Benefit:** ~50 lines gone from a user-facing tool; the help text stops promising an option that does nothing.
- **Effort:** S · **Risk:** Low (the `-a` half is Med if any user's scripts depend on the current ignore-it behaviour — none can depend on its *effect*, since it has none) · **Confidence:** High, READ (call-site grep over the file and repo).

### RIPTOPL-268 / ~90 lines of commented-out POSIX implementations are kept beside the stdio ones in the PC tools

- **Category:** 1 (dead code)
- **Locations:** `tools/pc/iso2opl/src/iso2opl.c:95-113` (open/read `check_cfg`), `:169-184` (open/write `write_cfg`), `:225-271` (open/write `write_parts`); `tools/pc/iso2opl/src/isofs.c:166-168, 172-180, 1051-1053, 1059-1060, 1080` (lseek/read/close fragments and a hex-dump block).
- **Problem:** Each live stdio function carries its abandoned descriptor-based twin as a block comment, plus stray commented debug code in isofs. The twins must be re-read and discounted by anyone auditing the live path (they contain the same buffer sizes and error codes, inviting copy-back of superseded code), and they are the only "documentation" of a port that already happened.
- **Fix:** Delete the commented blocks; version control is the archive.
- **Benefit:** `iso2opl.c` drops from 608 to ~520 lines and `isofs.c` sheds its dead fragments; one implementation per function remains to audit.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-269 / Dead disc-probe constants copied into both split files; the only live copy is in sysdisc.cpp

- **Category:** 1
- **Locations:** `src/platform/sysneutrino.cpp:11-25`, `src/platform/sysvmc.cpp:11-25` and `src/platform/system.cpp:17-25` (fourth copy, added at consolidation: S5 read system.cpp whole this round and the constants are equally unused there) (identical `#include <delaythread.h>` / `<time.h>` preamble plus `SYS_DISC_DETECT_MS`, `SYS_DISC_REDETECT_MS`, `SYS_DISC_NODISC_MS`)
- **Problem:** Both files carry the disc-probe budget block verbatim from the system.cpp split, and neither file ever uses the constants: a tree-wide grep shows the only uses are in `src/platform/sysdisc.cpp` (which defines its own copy), and in each of these two files the names occur exactly three times — the definitions. The constants are `constexpr` (internal linkage), so the cost is not BSS; it is three more copies of a tuned value set (1,000 / 4,000 / 2,000 ms) that can drift independently of the copy that actually governs disc waits. (The fourth copy, in `src/platform/system.cpp:17-25`, is included above at consolidation on the same evidence.)
- **Fix:** Delete the constant block (and the two includes that only serve it) from `sysneutrino.cpp` and `sysvmc.cpp`. Pure deletion.
- **Benefit:** One live definition of the disc budgets instead of four; no reader of the Neutrino or VMC files is misled into thinking those budgets apply there.
- **Effort:** S / **Risk:** Low / **Confidence:** High (READ, grep-verified)

### RIPTOPL-270 / imgdrv carries dead code: an unused `dummy_fs` and two unused private device-struct typedefs duplicating the SDK's

- **Category:** 1 (dead code)
- **Locations:** `modules/iopcore/imgdrv/imgdrv.c:14-17` (`dummy_fs`), `:40-62` (`iop_device_tm_t`, `iop_device_ops_tm_t`), `:65-90` (ops table: the only references to `dummy_fs` are in the commented-out block at `:79-89`)
- **Problem:** `dummy_fs` is defined and then used nowhere — the ops table fills the slots it was meant for with `NULL`/`IOMAN_RETURN_VALUE(0)` and mentions it only inside a comment. The two `*_tm` typedef structs (23 lines) re-declare the SDK's `iop_device_t`/`iop_device_ops_t` under local names and are never used; the actual device is declared with the real types at `:92-97`. In a 98-line file, ~28 lines are inert.
- **Fix:** Delete `dummy_fs`, both typedefs, and the commented-out ops block. Pure deletion.
- **Benefit:** The whole driver becomes the ~70 lines that do something; no future editor mistakes the private structs for the ones in force.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — no references exist in the file, and the module has no other translation unit)

### RIPTOPL-271 / poll.c's invalid-fd tracking is dead: `ifd` is never set, so POLLNVAL can never be returned

- **Category:** 1 (dead code) / 6 (latent correctness)
- **Locations:** `modules/network/smbman-ra/poll.c:20, 26, 37, 73-74`.
- **Problem:** `fd_set ifd` is `FD_ZERO`'d and read in both loops (`FD_ISSET(fd, &ifd)` guards the duplicate-fd skip at `:37` and selects the `POLLNVAL` result at `:73-74`) but no code path ever `FD_SET`s it. The POLLNVAL branch is unreachable, duplicate fds in the array are processed twice instead of being marked invalid, and a caller polling a bad descriptor gets a silent timeout-shaped answer instead of POLLNVAL. The only in-tree caller (`smb.c` `RecvTimeout`) polls one fd, so today's impact is nil — this is filed as dead code carrying a false promise, not as an active bug.
- **Fix:** Delete the `ifd` set and both branches that consult it (the loops simplify to the direct FD_ISSET tests), or — if POLLNVAL semantics are ever wanted — set the bit where the comment intends; deletion is the LESS-NOT-MORE default.
- **Benefit:** ~10 lines of misleading machinery gone; the file stops claiming an error behaviour it does not implement.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### RIPTOPL-272 / textures: staged-open diagnostic latch for an already-superseded hypothesis is permanent production code

- **Category:** 8 (half-finished diagnostic)
- **Locations:** `src/gfx/textures.cpp:512-585` (the OE counter comment + `gTexStagedOpenNonEnoent` at `:526`, the "PHASE TIMING" comment + `gTexLastOpenMs`/`gTexLastMissOpenMs` at `:547-548`, the "WORST-CASE LATCH" comment + `gTexWorstOpen*` statics + `texDebugWorstOpen` at `:566-585`) and the per-open sampling at `:931-945`; sole consumer `src/ui/guioverlay.cpp:194,213` (debug HUD)
- **Problem:** ~70 lines of comment and 7 globals exist to answer one hardware question (are slow staged opens contention or device cost?). The file's own OE comment records that the first hypothesis this instrumentation served was already reframed by the fix in `texStagedOpenIsAbsence`, and the latch comment says its predecessor fields "cannot answer the question they were added for". Every staged art open in every build pays two `ioGetPending()` calls, a `clock()` pair and the latch bookkeeping (cheap individually — `ioGetPending` is a plain queue count, `src/platform/ioman.cpp:397` — but pure overhead once the question is answered or abandoned), and the question can only be closed by a hardware measurement, not by more code.
- **Fix:** Once the maintainer takes the hardware reading the latch was built for, delete the latch, the OE counter and the phase-timing globals together with their HUD fields; until then this is a decision to record, not code to add. If the reading never happens, the deletion is the answer.
- **Benefit:** ~80 lines (comments, globals, sampling, HUD plumbing) removed; the staged-open hot path returns to open-and-check-errno only.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ for the code and its sole consumer; whether the diagnostic is still wanted is the maintainer's call)

### RIPTOPL-273 / sfxGetDropDiag() is a stub that always reports zero drops, while the debug HUD presents its counters as a live instrument / Category 8

- **Locations:** `src/core/sfx.cpp:305-312` (the stub); consumer `src/ui/guioverlay.cpp:203` and the `SX%u/%u` field in the HUD format string at `guioverlay.cpp:214` (cross-checked, outside assigned files).
- **Problem:** `sfxGetDropDiag()` unconditionally writes 0 to both outputs. The HUD nevertheless renders `SX<stale>/<full>` and its comment describes the counters as drops from "the saturated 8-deep ring" — a mechanism the current `sfx.cpp` no longer contains (the pool/ring was replaced by the shared-voice + 50 ms coalesce design at `sfx.cpp:71-84, 395-439`, whose early-return drops in `SoundEffects::play()` are counted nowhere). Net effect: a diagnostic built for issue #364 ("effects cut…") can only ever show SX0/0, which reads as evidence that no sound was dropped precisely when the coalesce/cursor gates are dropping them by design. A half-landed instrument is worse than none: it falsifies the diagnosis it exists to support.
- **Fix:** Delete `sfxGetDropDiag`, the `SX` field and its HUD comment (LESS: the drop concept died with the ring design). If the maintainer wants drop visibility, the alternative is counting the two early returns in `play()` — but that is new instrumentation, not a repair of this stub. Justification for preferring deletion: the current design's drops are deliberate coalescing, already documented at the drop sites.
- **Benefit:** Removes a permanently-zero HUD field and a stale mechanism description; ~10 lines plus the HUD field.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — stub body and HUD consumption both read).

### RIPTOPL-274 / Dead dialog read-back block at the end of the Compatibility dialog

- **Category:** 1 (dead code)
- **Locations:** src/ui/guigame.cpp:1818-1823, src/ui/guigame.cpp:1832-1835 (tail of `guiGameShowCompatConfig`, function at :1722)
- **Problem:** After the dialog closes, the function recomputes `compatMode` from the mode checkboxes and reads `dmaMode`, `coreLoader`, `hexid` and `altStartup` out of diaCompatConfig into the file statics. None of those writes is ever consumed: the only consumers are `guiGameSaveConfig`/`guiGameTestSettings`, and both go through `guiGameApplyConfig` (src/ui/guigame.cpp:1852), which recomputes `compatMode` from the same dialog (:1859-1865) and re-reads DMA (:1876), Loader (:1881), GameID and AltStartup from the dialog before using any of them. `guiGameLoadConfig` (:2540) likewise reloads `coreLoader` from the config set. The only live statement in the block is the COMPAT_LOADFROMDISC branch (:1825-1830), which must stay.
- **Fix:** Delete the compat-mode loop and the four diaGet* read-backs; keep the `if (result)` LOADFROMDISC branch.
- **Benefit:** ~11 dead lines removed; one less place that falsely implies the dialog's values are captured at close time rather than at save/test time.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — all readers/writers of the five statics enumerated by grep in this file; the statics are file-local)

### RIPTOPL-275 / `GetModStorageLocation` takes a `compatFlags` parameter it never uses

- **Category:** 1
- **Locations:** `src/platform/system.cpp:755` (definition; body reads only `startup`), sole caller at `src/platform/system.cpp:1398`
- **Problem:** The static helper's second parameter is dead: the two-entry `mod_storage_location_list` lookup keys on the game ID alone. The parameter invites a reader (and the next patch author) to believe module-storage placement is compat-flag-sensitive when it is not — in a launch path where placement mistakes are expensive (the IGR-menu/overlay placement code in the same function chain reasons carefully about exactly this address).
- **Fix:** Delete the parameter and update the one call site.
- **Benefit:** One less misleading input in the launch path; trivially less code.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-276 / tools/syntax_sweep.py is not run by anything and cannot fail if it is

- **Category:** 7 (unwired check / CI clutter asymmetry)
- **Locations:** tools/syntax_sweep.py:1-37 (whole file; result handling at :33-37 prints error counts, no `sys.exit` anywhere in the file)
- **Problem:** The sweep is the only coverage for code behind `#ifdef` flavour defines the default build never compiles (RA, IGS, RTL, DTL, debug variants — its own docstring). Yet no workflow and no test runner references it (grep of `.github/workflows/` and `tools/run_tests.sh`: zero hits; it is mentioned only in docs/CPP-CONVERSION-PLAN.md as a manual verification step), and even when run by hand it always exits 0 — it prints `== SET: N errors` and stops. Its sibling ratchets are both wired and can fail: `warning_gate.py --check` and `analyze_coupling.py --check` run in ps2build.yml:79-81 and exit non-zero on regressions. So an ifdef-only syntax break is invisible unless someone runs this script manually *and reads its stdout*.
- **Fix:** Either wire it next to the other two ratchets in ps2build.yml with a non-zero exit when any set reports errors, or delete it and let the configurations workflow stand as the flavour coverage. (Wiring it is an addition of CI time; the LESS-NOT-MORE default is to pick one — the current state, a checker that exists but neither runs nor can fail, is the worst of both.)
- **Benefit:** Flavour-only code either gets a real gate or stops pretending to have one.
- **Effort:** S (delete) / M (wire + fix exit status) · **Risk:** Low · **Confidence:** High, READ.

### RIPTOPL-277 / tools/make_save_icon.py's `--check` mode is never invoked; the generator is referenced nowhere

- **Category:** 7 (unwired drift check)
- **Locations:** tools/make_save_icon.py:1-214 (whole file; `--check` at :196-206)
- **Problem:** A repo-wide grep for `make_save_icon` returns zero hits outside the file itself — no workflow, test, or doc names it. Its `--check` mode exists precisely to catch the committed `misc/list.icn` / `misc/icon.sys` drifting from the generator, and nothing runs it. The committed assets *are* structurally validated by `.github/scripts/test_save_icon_set.py` (flavours.yml:99), but that test re-derives the format by hand and never executes the generator, so an edit to the generator that changes its output (e.g. to the winding fix the file's own comments describe from #314) would pass CI while the shipped icon silently stays the old one — or the reverse. The sibling generator does not have this gap: `test_bdm_index.py:135-144` copies `make_bdmindex.py` into a temp tree, runs it, and diffs every output picture.
- **Fix:** Extend the existing save-icon test to run `make_save_icon.py --check` the way test_bdm_index.py runs its generator, or delete the `--check` mode and state in the docstring that regeneration is manual and unverified. *Justification for the test option:* it reuses an existing test file and an existing `--check` entry point; no new harness is added.
- **Benefit:** Generator and committed icon can no longer drift apart undetected, in either direction.
- **Effort:** S · **Risk:** Low · **Confidence:** Med, READ (in-repo invocation is provably absent; use outside the repo cannot be observed from here).

### RIPTOPL-278 / `test_ministack_xchg` is the one udpfs host test whose checks all vanish under `-DNDEBUG` — including a side-effecting `assert`

- **Category:** 7 (test code)
- **Locations:** `modules/network/udpfs_shared/tests/test_ministack_xchg.c` (assert-based throughout, e.g. checks at `:178-302`; side-effecting `assert(deliver(&script[0]) == -1)` at `:239`; asserts inside the stubs at `:38,55-77`)
- **Problem:** These five host tests are genuinely behavioural (see Checked-and-clean) — which makes this one's fragility worth separating from RIPTOPL-005's source-grep pattern. Its three siblings use an explicit `check()` counter that always runs and fails the process. `test_ministack_xchg` (304 lines, the largest behavioural surface of the set: round-trip, stray-source, oversize, ARP-failure, bad-args) expresses **every** check as `assert()`. The in-tree runner (`.github/scripts/test_udpfs_transport.py:20-22`) compiles without `-DNDEBUG` and without `-O`, so the asserts are live today — but any future flag change, IDE default, or copied build line that defines `NDEBUG` turns the whole suite into a program that performs the exchanges, checks nothing, prints `PASS`, and — because `deliver()` itself is called inside an assert at `:239` — silently skips a delivery the following scenario depends on. Asserts inside the stubs (`smap_fifo_read`'s bounds asserts) are the suite's only FIFO overrun protection and vanish on the same switch.
- **Fix:** Convert the checks to the siblings' `check()` pattern (or a local equivalent that counts failures and returns non-zero). No new framework; the pattern already exists in the same directory.
- **Benefit:** The exchange suite's verdict survives build-flag drift instead of silently becoming a PASS that tested nothing.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — runner flags included)

---

### RIPTOPL-279 / test_catalog's de-duplication memory silently stops at 512 unique startups

- **Category:** 8 (half-finished feature — test harness fidelity)
- **Locations:** `tools/pc/http/tests/test_catalog.c:30` (`static char seen[512][...]`), `:89-92` — when `seenCount` reaches 512, new accepted startups are no longer recorded, but the row is still printed `ACC`.
- **Problem:** The harness's stated contract (header comment) is first-record-wins de-duplication mirroring `httpsupport.cpp`, diffed against the Python reference. Past 512 unique startups, a later duplicate prints `ACC` here while the (unbounded) reference prints the duplicate outcome — the two sides diverge for a reason that has nothing to do with the parser under test, and a smaller fixture would never reveal it.
- **Fix:** Grow `seen` dynamically, or print a hard harness error when the cap is hit instead of silently degrading.
- **Justification (fix adds code):** the harness is the oracle for a cross-language contract; a loud cap preserves that, a silent one voids it.
- **Benefit:** the catalog differ stays trustworthy at any fixture size.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (trigger — a >512-startup fixture — not present in-tree as far as this section's files show; INFERRED).

### RIPTOPL-280 / test_settings snapshots the settings table into fixed 200-entry arrays with no bound check

- **Category:** 8 (half-finished feature — test harness fidelity)
- **Locations:** `tests/host/test_settings.cpp:51` — `int expectInt[200]; char expectStr[200][256]; unsigned char expectCol[200][3];`, filled at `:52-60` with `idx++` per `kSettings` row and no comparison against 200.
- **Problem:** The table currently has 109 rows (counted in `src/core/settings.cpp` / the golden keys include), so the cap is invisible today; the day the table passes 200 rows the *test itself* stack-smashes instead of failing with a message, and the failure will look like a settings bug. The golden-keys array right above it is sized from its include; the snapshot arrays are a magic number.
- **Fix:** Size the arrays from the golden key count (`sizeof(kGoldenKeys)/sizeof(kGoldenKeys[0])`, already computed as `n` at `:23`) or assert `idx < 200` in the fill loops.
- **Justification (fix adds a bound):** one assert ties the harness's capacity to the table it mirrors.
- **Benefit:** future table growth fails as a test message, not as memory corruption in the test rig.
- **Effort:** S · **Risk:** Low · **Confidence:** High, READ (row count verified by grep of the settings table, outside this section's files).

---

---

## Checked and clean (condensed; full dissolution reasoning in `round5-sections/riptopl-r5-*.md`)

~250 candidates were dissolved on full reading this round — the established standard (a suspicion that does not survive the whole file is recorded with its reasoning, never filed). Per section: S1 13, S2 18, S3 20, S4 22, S5 21, S6 14, S7 16, M1 20, M2 13, M3 21, M4 16, M5 9, M6 15, T1 15, T2 ~17, plus the parent's residue reads. The dissolutions most worth recording centrally:

- The five UDPFS host tests are genuinely behavioural (compile-and-execute against scripted stubs), **not** RIPTOPL-005-style source greps — the .github corpus characterisation does not extend to them (M6; their one internal defect is RIPTOPL-278).
- `apaGetFormat`'s apparently inverted status handling is correct — it returns a boolean (`rv == 0`) (M3). The USBMASS STOP_UNIT/STOP_ALL "swap" matches its sole EE caller (M3). The CIS `data[254]` buffer is exactly fitted because a 0xff length is a chain terminator (M3).
- `cdvdman/smb.c`'s live receive path is validated (NBSS length, DataOffset/DataLength vs request and reply) — the round-2 clearance stands; only its dead twin branch is filed (RIPTOPL-261) (M1).
- Every #1018-era iLink hardening fix is present in **both** ilink copies; `iLink_intr/TxRx/DMA/PHY` have zero substantive divergence (M5).
- `ncmd.c` (cdvdman), the mx4sio `sio2man_hook.c`/`ioplib.c`/`crc16.c`, `udpfs_ministack/udptty.c`, `usbmass_bd/main.c`, the six UDPFS include shims, and the three `pccf` `.inc` files (fixed-count alignment-gated PIO loops; a bounded mini-printf; a triple-read-validated timing profile) were read by the parent and are clean. `sceCdReadDiskID`'s 5-byte probe of the caller's buffer is contract-shaped (the SDK DiskID buffer is ≥5 bytes at every in-tree caller class) — noted, not filed.
- `apa_recover.py` is the most carefully fenced file in the tools tree (writes refused outside LBA 0–7, read-only until backup + typed confirmation, `xb` backup creation) (T2). `elfldr/loader.c`'s segment checks run in the safe order and its unchecked entry point is the loader's function, not a defect (T1).

## Coverage ledger (cumulative, rounds 1–5, at the pin)

| Area | Total | Read 1..EOF | Status |
|---|---|---|---|
| EE `src/` (.cpp/.c) | 90,453 lines / 120 files | 33,336 (rounds 2–4) + **57,117 (this round)** | **Complete** — every file read end-to-end |
| `ee_core/` (.c) | 8,838 | 8,395 (rounds 3–4) + 443 igrovl/tests (this round) | **Complete** |
| `modules/` first-party (.c) | ~68,000 of 105,545 | All files across rounds 1–5; this round closed 33,935 + the parent's 896-line residue + 12 shim lines | **Complete** (see named exceptions) |
| `modules/` diff-compared pairs | 4,196 | DES pair 1,132 (round 3); bdmassault side 3,064 vs ilink side read in full (this round) | Compared exhaustively, hunk-classified (M5: 94 stripped hunks = 32 variant / 48 formatting / 7 drift / 7 suspected-bug) |
| `modules/` vendored by framing (.c) | 37,655 | — | Out of scope by the audit's framing since pass 1: FatFs trio, SMSTCPIP, usmb2, `unfs4.c`, `lz4.c` — glue around them audited, internals not |
| `tools/` + `tests/host` + `elfldr` | 11,733 (this round: T1 6,402 C + T2 5,331 Python) | All | **Complete** |
| Assembly (.S) | 3,854 (ee_core 7 files + `smsutils.S` ×3) | 0 | **Unread** — outside every round's C/C++/Python scope (named remainder, see verdict) |
| Headers (include/, ee_core/include, modules/**) | 35,590 | Consulted throughout | Not line-audited as a corpus |
| `.github/scripts` Python test corpus | 59,927 / 278 files | Characterised (RIPTOPL-005); individual files read where a finding touched them | Not line-read (named remainder) |
| `.github` other scripts + workflows | 2,345 + 2,222 | Censused in cross rounds; invocation-grepped this round (T2) | Not line-audited end-to-end |
| `third_party/` | 204,965 | — | Vendored; out of scope by framing |
| Non-code (lng data, docs, ship/, assets) | — | Inventory level (pass 1) | Not source |

## FORMAL VERDICT — RiptOPL exhaustion

**RiptOPL is exhausted at source level in this environment, on the audit's stated scope.** As of this round, every line of shipped first-party C/C++ and Python source — the EE program (`src/`), the in-game core (`ee_core/` C), the IOP module tree's first-party files (`modules/`), and the PC/host tooling (`tools/`, `elfldr/`, `tests/host`) — has been read end-to-end at pin `2e347d5ac885`, across five rounds totalling 280 findings (RIPTOPL-001…280) and 15 formal revisions. The per-area table above is complete: there is no first-party `.c`/`.cpp`/`.py` file left at "unread" status. What remains is explicitly *not* that class of source, and is named here so the verdict cannot be over-read:

1. **Assembly sources (3,854 lines)** — ee_core's engine/patch asm and the three `smsutils.S` copies were outside every round's language scope and remain unread. They are the one body of shipped first-party *text source* a future round could still take, with the caveat that their correctness questions are disproportionately hardware questions.
2. **The `.github/scripts` Python test corpus (59,927 lines)** — host test code, not shipped code. It was characterised as a corpus in round 1 (692 `read_text` call sites, counted exactly) and its systemic defect class is filed (RIPTOPL-005); individual files were read wherever a round-5 finding touched them. Line-auditing all 278 files would audit the test rig, not the product.
3. **Headers as a corpus (35,590 lines)** — consulted for constants, layouts and contracts in every round; never line-audited as a standalone corpus.
4. **Vendored stacks (37,655 lines of modules .c; 204,965 lines of `third_party/`)** — excluded by the audit's own framing from pass 1 (FatFs, SMSTCPIP, usmb2, unfs4, lz4, and the third-party tree). Their glue, call sites and local modifications were audited; their internals were not, by design.
5. **Workflows and non-code trees** — censused and inventoried, not line-audited.

Separately, three things are **un-auditable in this environment no matter how much is read**, and no round has claimed otherwise: PS2 build results (no PS2SDK exists in this VM), real-hardware behaviour (the user's standard — a build is not proof — applies to every performance and timing statement in all five rounds, which are structural/INFERRED as marked), and live network interop (the sandbox blocks UDP sendto; wire-format agreement was proven at source level only).

**Consequence:** a further RiptOPL round on this pin and this scope would be re-reading, not auditing. Productive continuations are exactly: (a) the pin moves (delta round), (b) scope is extended to the assembly sources, the test corpus, or vendored internals, or (c) a PS2 build/hardware rig becomes available and the INFERRED claims can be promoted or struck. Until one of those happens, the RiptOPL file is closed — 280 findings, with the Cat 6 cluster at RIPTOPL-145…202 as the fix-first list.
