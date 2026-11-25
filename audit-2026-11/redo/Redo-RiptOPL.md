# REDO AUDIT — RiptOPL (blind redo)

- **Repo / pin:** RiptOPL (Open-PS2-Loader fork), branch `rebuild/main`, commit `2e347d5ac885cd0ff73e9ad4143781946396f492` — HEAD verified by every worker before reading and by the coordinator after consolidation; clone read-only throughout, `git status` clean at the end.
- **Method:** independent blind redo. Fourteen parallel workstreams (src EE code, headers/ee_core, SMSTCPIP, SMB/UDPFS network modules, iopcore, BDM family, HDD/USB/VMC, pademu/ds34/iLink/MMCE, third_party+tools+ship, the .github/scripts Python corpus in two halves, workflows/tests, and build/docs/assets) read the tree at the pin. The prior audit was not consulted by any worker or by the coordinator.
- **Rule applied:** LESS, NOT MORE — every fix removes, simplifies, consolidates, or fixes; anything that adds code carries its own justification in the finding.
- **Totals (counted from this file's numbered headings):** 265 findings. Six cross-workstream duplicate pairs were merged at consolidation (hostmap monkeypatch, test-harness boilerplate, skip-to-pass, source-text-only assertions, C++ format-gate exclusion, PyYAML-less CI jobs — each merged finding preserves both filed bodies).
- **Per-category counts:** 1 dead code: 42; 2 duplication: 54; 3 overengineering: 17; 4 size/memory: 11; 5 performance: 12; 6 bugs/security: 98; 7 build/CI/repo clutter: 15; 8 half-finished/redundant features: 16.
- **Order:** by benefit — shipped-code bugs/security first (wire-reachable and data-loss defects ahead of latent ones), then size/memory, duplication, dead code, half-finished features, overengineering, structural performance, and finally test/CI/build/docs integrity.
- **Census corrections recorded by workers:** `.github/scripts/` holds 286 `.py` files (62,272 lines), not 304 files total; Category 5 findings are structural only — nothing here is PS2-build- or hardware-verified.

---

## FINDINGS

## RE-RIPTOPL-001 — SMB1 reply length is server-controlled and overflows the static SMB buffer (filed as W06-001)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/smbman-ra/smb.c:230`, `RiptOPL/modules/network/smbman-ra/smb.c:257-261`; buffer size at `RiptOPL/modules/network/smbman-ra/smb.h:894-895`
- **Problem:** In `GetSMBServerReply()`, the total packet size is taken from `nb_GetSessionMessageLength()`, a 24-bit field in the NetBIOS session header sent by the server (up to ~16 MB). On the `rhdrlen == 0` path the code then calls `RecvData(main_socket, (char *)&SMB_buf.smb, size, 3000)` with that size. `SMB_buf.smb` is `MAX_SMB_BUF + MAX_SMB_BUF_HDR` = 4096 + 128 = 4224 bytes. There is no check that the claimed length fits the buffer before the receive, so a malicious or broken SMB server (or anything that can answer on the share's port) writes past a static IOP buffer. READ.
- **Fix:** Clamp/reject the session-message length against the receive buffer size in `GetSMBServerReply()` before any `RecvData` into `SMB_buf`; fail the request on an oversized claim. This is a bound check on existing code, not a new mechanism.
- **Benefit:** Removes a remotely triggerable static-buffer overflow in the module that runs for every SMB game load.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-002 — SMB1 ReadAndX trusts the server's data length and padding; WriteAndX in the same file was already hardened (filed as W06-003)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/smbman-ra/smb.c:1077-1092`; hardened sibling for contrast at `RiptOPL/modules/network/smbman-ra/smb.c:1164-1170` (approx., the WriteAndX reply checks)
- **Problem:** `smb_ReadAndX()` computes `padding = RRsp->DataOffset - sizeof(ReadAndXResponse_t)` and receives that many bytes into `(RRsp + 1)` inside `SMB_buf` with no bound, then computes a 32-bit `DataLength` from the reply and receives exactly that into the caller's `readbuf` without ever comparing it to the caller's `nbytes`. A server can therefore overflow both the internal buffer (via padding) and the caller's buffer (via DataLength). The neighbouring `smb_WriteAndX()` already validates reply size, WordCount and `Count > nbytes` with an explanatory comment — the same discipline was never applied to the read path. READ.
- **Fix:** Apply the WriteAndX pattern to ReadAndX: reject negative/oversized padding, and clamp or reject `DataLength > nbytes`.
- **Benefit:** The read path is the hot path for game data; this is the same class of overflow as W06-001 on the most-used operation.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-003 — Negotiate Protocol copies a server-controlled key length into an 8-byte field (filed as W06-002)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/smbman-ra/smb.c:403-404`; field at `RiptOPL/modules/network/smbman-ra/smb.h:286`
- **Problem:** `smb_NegotiateProtocol()` does `memcpy(server_specs.EncryptionKey, NPRsp->ByteField, NPRsp->KeyLength)`. `KeyLength` is a server-supplied byte (0–255); `EncryptionKey` is `u8[8]` inside the static `server_specs`. Any server claiming a longer key overflows the struct. The next line passes `&NPRsp->ByteField[NPRsp->KeyLength]` to `getStringField(..., 0)`, i.e. an unbounded string copy starting at a server-chosen offset, which can also read past the received packet. READ.
- **Fix:** Reject `KeyLength > 8` (the LM/NT challenge is defined as 8 bytes) before the copy, and bound the domain-name copy by the bytes actually received.
- **Benefit:** Closes a trivially triggered overflow at the very first exchange with any server.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-004 — SMB1 transaction replies use server offsets as raw indices into the receive buffer (filed as W06-004)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/smbman-ra/smb.c:692`, `:696`, `:699-702` (NetShareEnum), `:833`, `:846` (QueryPathInformation), `:1477`, `:1482` (FindFirst/Next2)
- **Problem:** One mechanism in three operations: `ParamOffset`/`DataOffset` from the server's TRANSACTION response are used directly as byte indices into `SMB_buf.smb.u8buff[]` with no check against the number of bytes actually received. NetShareEnum additionally walks the data area with `strlen()` and a server-supplied u16 comment offset, and loops over a server-supplied `AvailableEntries` count. Out-of-range offsets give out-of-bounds reads of static memory and, in the FindFirst path, copies of attacker-influenced bytes into the caller's file-info struct. READ.
- **Fix:** Validate both offsets (and, for NetShareEnum, the entry count and per-entry string extents) against the received reply length before dereferencing; fail the call otherwise. One shared check at the point each response is unpacked — no new abstraction, the offsets are already handled per-function.
- **Benefit:** Removes OOB reads across share enumeration, stat and directory listing — all reachable by any SMB server the user connects to.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-005 — UDPRDMA receive ISR lets a peer claim an app-header up to 60 bytes into 32-byte (or smaller) buffers (filed as W06-005)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/udpfs_shared/src/udprdma.c:185`, `:191-196`; limit defined at `RiptOPL/modules/network/udpfs_bd/src/udprdma_packet.h:103` (identical copies in the fhi/ioman trees)
- **Problem:** In `_ist()`, the DATA case computes `hdr_size = data_hdr.hdr_word_count * 4`. `hdr_word_count` is a 4-bit field in the wire header, so `hdr_size` can be up to 60. `UDPRDMA_MAX_APP_HDR` is 32. Two consequences, both READ: (a) the discard path declares `uint8_t dummy[UDPRDMA_MAX_APP_HDR]` on the stack and calls `smap_fifo_read(0x30, dummy, hdr_size)` — up to 28 bytes past a stack buffer, in interrupt context; (b) the keep path calls `smap_fifo_read(0x30, s->rx_hdr_buffer, hdr_size)` without comparing against `s->rx_hdr_size`, the size the current receive armed (e.g. the 8-byte `udpfs_msg_result_reply_t`), so a peer can write up to 60 bytes into a much smaller caller buffer. The send side of the same file does check `app_hdr_size > UDPRDMA_MAX_APP_HDR` (udprdma.c:595); the receive side never does. The ministack exchange layer in the same family (`udpfs_ministack/src/ministack_xchg.c`) validates its wire lengths — this layer does not.
- **Fix:** In `_ist()`, reject (drop the packet) when `hdr_size > UDPRDMA_MAX_APP_HDR`, and when a header buffer is armed, also when `hdr_size > s->rx_hdr_size`. Two comparisons in the existing branch; nothing added.
- **Benefit:** Closes a peer-triggerable overflow in the interrupt handler of the UDPFS transport used by all three udpfs editions (the source is shared).
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-006 — `sysWriteNeutrinoCheats` accumulates `snprintf` returns into a length with no truncation check (filed as W03-010)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/src/platform/sysneutrino.cpp:961-990` (accumulation at lines 974–978), weaker twin at `RiptOPL/src/platform/sysneutrino.cpp:1081` / line 1111 (`sysNeutrinoHandRA`)
- **Problem:** The cheat-file writer does `len += snprintf(text + len, size - len, ...)` in a loop over cheat pairs. `snprintf` returns the length it *would* have written, so the first truncation pushes `len` past `size`; from then on `size - len` is negative and converts to a huge `size_t` in the next call, turning a too-long cheat list into a heap overwrite. The number of pairs is bounded by the cheat system (up to 250), and the per-pair text is fixed-format, so whether a real configuration reaches the overflow depends on the buffer size versus pair count — but the pattern itself guarantees that *if* the text ever exceeds the buffer, the failure mode is corruption rather than truncation. The same accumulation pattern appears in `sysNeutrinoHandRA` (line 1111) with more tightly bounded inputs.
- **Fix:** Check each return: if `n < 0 || len + n >= size`, stop and fail the write. Or simpler — compute the required size first from the pair count (the format is fixed-width) and refuse up front. Either removes the negative-size conversion.
- **Justification (fix adds a check):** One comparison per append is the minimum that makes the existing code safe; no new abstraction.
- **Benefit:** A worst case that is currently memory corruption becomes a clean "cheats not written" failure.
- **Effort:** S · **Risk:** Low · **Confidence:** Med — the pattern and its consequence are READ; that a shipping configuration actually overflows the buffer is INFERRED (depends on the caller's buffer size vs. pair count).

## RE-RIPTOPL-007 — APA journal restore trusts the on-disk entry count and a possibly-NULL cache entry (filed as W09-002)

**Category:** 6 bugs/security
**Locations:**
- RiptOPL/modules/hdd/apa/src/journal.c:58-86 (`apaJournalRestore`, loop at :73)
- RiptOPL/modules/hdd/apa/src/libapa.h:183-189 (`apa_journal_t.sectors[126]`)
- RiptOPL/modules/hdd/apa/src/journal.c:47-56 (`apaJournalWrite`, same unbounded `num++` on the write side)

**Problem:** `apaJournalRestore` checks only `journalBuf.magic`, then loops `i < journalBuf.num` reading `journalBuf.sectors[i]` as the *destination LBA* for a 2-sector write. `num` is an `s32` read straight from disk and is never clamped to the 126-entry array: a corrupt or hostile journal header makes the loop read past the static `journalBuf` into adjacent BSS and write header data to whatever LBAs it finds there, at driver init, before any fence-relevant check (the table fence only accepts a valid `__mbr` at LBA 0; other LBAs pass). The same function also dereferences `apaCacheAlloc()`'s result without a NULL check (`clink->header` at :75). `apaJournalWrite` likewise increments `num` with no bound against `sectors[126]`.
**Fix:** In restore, reject/reset when `num < 0 || num > 126` before replaying, and return the journal reset if `apaCacheAlloc()` returns NULL. In write, refuse to append at 126 entries.
**Benefit:** A single corrupt sector in the journal area can no longer turn into arbitrary sector writes during boot.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-008 — mcemu (HDD): an unmapped VMC page silently maps to sector 0 (filed as W09-004)

**Category:** 6 bugs/security
**Locations:**
- RiptOPL/modules/mcemu/device-hdd.c:11-28 (`Mcpage_to_Apasector`)
- RiptOPL/modules/mcemu/device-hdd.c:30-48 (`DeviceWritePage` / `DeviceReadPage` pass the result straight to `sceAtaDmaTransfer`)

**Problem:** `Mcpage_to_Apasector` initialises `sector_to_read = 0` and returns 0 when the requested page falls outside all ten PFS block extents in `vmcSpec[mc_num].blocks[]` — e.g. a VMC image fragmented into more extents than the map holds, or a page past the mapped end. Sector 0 is the APA `__mbr` header sector. Neither caller can distinguish "page legitimately at sector 0" (impossible for a PFS file extent) from "unmapped", so a stray VMC write page is DMA-written over the partition-table header sector instead of failing.
**Fix:** Make the mapper report failure (e.g. return a sentinel the callers check, or check in the two callers for a page that matched no block) and have both device functions return 0 (failure) in that case.
**Benefit:** Removes a path by which virtual-memory-card I/O can overwrite LBA 0 of the game disk.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ — the default-0 return and the unchecked callers are both in the code; the triggering fragmentation state is INFERRED)

## RE-RIPTOPL-009 — SYSTEM.CNF parser can return an unterminated key and reads before its bounds check (filed as W03-001)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/src/platform/ps2cnf.cpp:18`, `RiptOPL/src/platform/ps2cnf.cpp:42-71`
- **Problem:** Three defects in the BOOT2 parser, all READ at the cited lines:
  1. `CNFGetToken` line 18: `for (; isspace((int)*cnf) && cnf < end; cnf++)` — `*cnf` is dereferenced *before* `cnf < end` is evaluated (`&&` runs left to right), so entering with `cnf == end` reads one byte past the buffer.
  2. `CNFGetKey` (lines 42–71) takes no `end` bound at all, and its copy loop (line 54) writes the terminating NUL only on the whitespace branch. A token of `CNF_PATH_LEN_MAX` (64) or more graph characters exits the loop on the count bound with the caller's `bootfile` buffer never NUL-terminated; downstream string use then reads past it.
  3. The `else if (*line == '\0')` branch (lines 62–64) is unreachable: the loop condition already excludes `'\0'`, and the graph/space branches cover the remaining cases it could fire on. Dead code inside a parser.
- **Fix:** Check `cnf < end` before dereferencing in `CNFGetToken`; in `CNFGetKey`, always write `*key = '\0'` on loop exit (one line after the loop covers every path); delete the unreachable branch.
- **Benefit:** Removes an out-of-bounds read and a potential unterminated-string over-read on data parsed from a disc's SYSTEM.CNF — i.e. untrusted input.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-010 — GPT parser trusts header entry size and never checks first_lba ≤ last_lba (filed as W08-002)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/bdm/src/part_driver_gpt.c:68`, `RiptOPL/modules/bdm/src/part_driver_gpt.c:130`, `RiptOPL/modules/bdm/src/part_driver_gpt.c:175`; field defined at `RiptOPL/modules/bdm/src/include/gpt_types.h:36`
- Problem: READ. (a) `entriesPerSector = bd->sectorSize / sizeof(gpt_partition_table_entry)` — the header's own `partition_entry_size` field is never read, so a disk formatted with a non-128-byte entry size (legal per UEFI) is walked at the wrong stride and every entry after the first is garbage. (b) The only sanity check is `first_lba < header.first_lba || last_lba > header.last_lba`; an entry with `first_lba > last_lba` (corrupt or crafted) passes, and `sectorCount = last_lba - first_lba + 1` underflows to a near-2^64 partition length. The RiptOPL `part_read`/`part_write` bounds then "protect" a partition that spans the whole address space — cross-partition writes become possible from a filesystem mounted on it.
- Fix: Reject entries with `first_lba > last_lba`; compute the stride from `partition_entry_size` after checking it is ≥ the struct size and divides the sector size, otherwise fail the mount. No new mechanism — two comparisons and using a field that already exists.
- Benefit: Closes a corrupt-disk → cross-partition-write path; makes non-128-byte-entry GPT disks parse correctly.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-011 — ZSO decompression uses the unbounded LZ4 "fast" decoder on untrusted image data (filed as W07-003)

- **Category:** 6 bugs/security
- **Locations:** RiptOPL/modules/isofs/zso.c:105-112 (call at line 111); RiptOPL/modules/isofs/lz4.c:500-506 (`LZ4_decompress_fast` over the generic decoder)
- **Problem:** Each compressed block is decoded with `LZ4_decompress_fast(tmp_buf, addr, 2048)`. The fast entry point has no input length at all: a corrupt or crafted ZSO block (the block size is only clamped to 2048 before the call) makes the decoder read past the 2048-byte temp buffer and follow match offsets outside the data it was given. The image file is the untrusted input here. READ.
- **Fix in words:** Decode with a bounded variant that knows the compressed block's byte count (`b_size`, already computed in zso.c) and fails the block on malformed input, reusing the generic decoder already in lz4.c.
- **Justification (addition):** One bounded entry point into the existing generic decoder is the smallest change that gives the decoder an input limit; nothing else is added.
- **Benefit:** A bad ZSO becomes a read error instead of out-of-bounds IOP memory access.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-012 — BT DS4 LED/rumble report: 50 bytes initialised, 79 bytes sent (filed as W10-002)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/pademu/ds34bt.c:1146,1179,1198,1206`; `RiptOPL/modules/ds34bt/iop/ds34bt.c:1208,1243,1263` (same function, `hid_LEDRumble`)
- **Problem:** `led_buf` is `PS4_11_REPORT_LEN + 2` = 79 bytes. The DS4 branch memsets only `PS3_01_REPORT_LEN + 2` = 50 bytes, sets fields up to index 13, then sends the full 79 bytes via `HID_command`. Bytes 50–78 are uninitialised stack memory transmitted to the pad on every DS4 LED/rumble update, in both copies of the driver.
- **Fix:** Memset the full `PS4_11_REPORT_LEN + 2` in the DS4 branch (one constant change per copy; one copy after W10-001).
- **Benefit:** Stops leaking IOP stack contents onto the wire and sending garbage report padding.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-013 — ps2ips RPC handlers trust EE-supplied length fields (`optlen`, `malign`) (filed as W05-013)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/ps2ips/ps2ips.c:591-592` (do_setsockopt); `RiptOPL/modules/network/ps2ips/ps2ips.c:402-407,435-440` (do_send/do_sendto)
- **Problem:** do_setsockopt copies `pkt->optlen` bytes from the RPC packet into a 128-byte stack buffer `optval[128]` with no clamp — an optlen above 128 overflows the RPC thread's stack. do_send/do_sendto compute `s_offset = 64 - pkt->malign` and memcpy `pkt->malign` bytes at `lwip_buffer + s_offset`; a malign above 64 makes the offset negative and the copy writes in front of lwip_buffer. Both fields come straight from the request packet. (The EE is the only sender, so exploitability is limited — INFERRED; the missing clamps are READ. This module's own RA-fix history shows packet-field bugs here were real and shipped.)
- **Fix:** Clamp optlen to sizeof(optval) and malign to 64 at the top of the handlers, rejecting larger values.
- **Benefit:** A malformed or version-mismatched EE client cannot corrupt IOP stack memory through the RPC layer.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-014 — UDPFS core returns the server's claimed byte counts without checking them against what arrived (filed as W06-006)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/udpfs_shared/src/udpfs_core.c:114-117` (`_recv_with_result`), `:547-549` (`udpfs_core_dread`)
- **Problem:** `_recv_with_result()` returns `result.result` — a server-supplied int32 — as the number of bytes read, ignoring the actual received length returned by the transport. `udpfs_core_read()` then advances the caller's buffer and totals by that claimed amount, so a server can make the client report (and its caller consume) more bytes than were ever DMA'd. Separately, `udpfs_core_dread()` checks `reply->name_len` only against the caller's `name_max`, not against the bytes actually received (`ret`), so a short reply yields stale bytes from the shared 312-byte `g_rx_buf` copied out as a filename (bounded, but fabricated data presented as a directory entry). READ for both code paths; the downstream impact of the over-claimed read count on EE-side callers is INFERRED.
- **Fix:** Treat the transport's received length as authoritative: fail or clamp when `result.result` exceeds it, and require `sizeof(reply) + name_len <= ret` in dread.
- **Benefit:** The protocol's own result field stops being a second, unchecked source of truth for buffer accounting.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ) / Med for downstream impact (INFERRED)

## RE-RIPTOPL-015 — iLink SCSI layer silently wraps LBAs on drives over 2 TiB (filed as W08-009)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/scsi.c:169-187` (READ/WRITE(10) CDB build), `:244-264` (READ CAPACITY(10) warmup)
- Problem: READ. The CDB builder masks the 64-bit LBA into 4 bytes (the code comment itself notes the 2 TiB limit), and warmup derives `sectorCount` from READ CAPACITY(10), whose `last_lba` saturates at `0xFFFFFFFF` on larger drives. Combined effect: a >2 TiB iLink drive is offered to BDM with a truncated/wrong geometry, and a request past the 2 TiB line wraps to a low LBA — a write lands on the wrong part of the disk instead of failing. Note BDM's own 512-byte-sector refusal does not catch this; the geometry is self-consistent, just wrong.
- Fix (LESS option): In warmup, refuse the device when `last_lba == 0xFFFFFFFF` (the saturation value), the same refuse-don't-mount pattern BDM already uses for unsupported sector sizes. READ CAPACITY(16) support is the *more* option and is not recommended here.
- Benefit: Converts silent data corruption into a clean "drive not offered".
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-016 — tar reader never validates the ustar header checksum, and size parsing ignores base-256 (filed as W03-003)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/src/platform/tar.cpp:75` (`parseOctal`), `RiptOPL/src/platform/tar.cpp:278` (size read); checksum absence verified by grep — no `chksum`/`checksum` reference exists anywhere in tar.cpp
- **Problem:** tar archives here carry theme and art files, i.e. data that did not necessarily come from the user. The reader walks headers trusting the size field without ever verifying the header checksum field (offset 148) that exists precisely to detect a corrupt or crafted header. Separately, `parseOctal` only understands ASCII-octal sizes; a header using the base-256 binary size encoding is misread, producing a wrong stride for the header walk.
- **Fix:** Verify the header checksum before trusting a header (a ~10-line sum over the header block, standard ustar), and treat a base-256 size field as an unsupported archive rather than misparsing it. No new machinery beyond the checksum sum.
- **Justification (fix adds code):** The checksum sum is the smallest possible validation for input the code already walks blindly; the alternative — continuing to trust the size field — is the bug.
- **Benefit:** A corrupt or hostile .tar is rejected at the first bad header instead of steering the entry walk out of the buffer.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + grep census)

## RE-RIPTOPL-017 — TCP header length from the wire is trusted; `pbuf_header` result ignored (filed as W05-009)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/tcp_in.c:120,157-158,1090-1121`; `RiptOPL/modules/network/SMSTCPIP/pbuf.c:482-491`; `RiptOPL/modules/network/SMSTCPIP/include/lwipopts.h:371-400`
- **Problem:** tcp_input checks only that tot_len ≥ sizeof(struct tcp_hdr), then computes `hdrlen = TCPH_HDRLEN(tcphdr)` (up to 15 → 60 bytes) and calls `pbuf_header(p, -(hdrlen*4))` ignoring its return. A segment claiming hdrlen 60 with tot_len 20 passes the minimum check; pbuf_header's forward move does not fail on the lower bound, so the u16 len/tot_len accounting underflows, and tcp_parseopt then walks options bounded by the same untrusted hdrlen. In the in-game build all CHECKSUM_CHECK_* are 0 (lwipopts.h:371-400), so there is not even a checksum gate in front of this path there. (Impact chain INFERRED; the missing validation is READ.)
- **Fix:** Before pbuf_header, drop the segment unless `TCPH_HDRLEN(tcphdr)*4 <= p->tot_len` (and ≥ TCP_HLEN); honour pbuf_header's return.
- **Benefit:** A malformed or hostile segment from the LAN is dropped instead of corrupting pbuf length accounting inside the stack.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the code facts (READ), Med for impact (INFERRED)

## RE-RIPTOPL-018 — smap-ingame's low-level output copies a pbuf chain into a fixed frame buffer without a total-length check (filed as W06-021)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/smap-ingame/main.c:45-70`; buffer size at `RiptOPL/modules/network/smap-ingame/main.h:4`
- **Problem:** `SMapLowLevelOutput()` walks the lwIP pbuf chain `mips_memcpy`ing each segment into a static `FrameBuffer[MAX_FRAME_SIZE]` (1518 bytes) and only afterwards assigns `TotalLength = pOutput->tot_len`; the chain's total is never checked against 1518 first. In practice lwIP builds chains for a single frame, so tot_len should stay within the MTU — which is why this is INFERRED and low — but the function itself enforces nothing, and a chained/oversized pbuf (e.g. from a future lwIP option) overflows a static buffer.
- **Fix:** Check `pOutput->tot_len > MAX_FRAME_SIZE` once at entry and return `ERR_MEM`.
- **Benefit:** One comparison removes a static-buffer overflow at the network egress.
- **Effort:** S · **Risk:** Low · **Confidence:** Low (INFERRED)

---

## RE-RIPTOPL-019 — MMCE `get_gameid` does an unbounded `strcpy` from the card reply into the caller's buffer (filed as W10-021)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/mmce/mmceman/src/mmce_cmds.c:195-224`; caller `RiptOPL/modules/mmce/mmceman/src/mmce_fs.c:1122-1124` (devctl `MMCE_CMD_GET_GAMEID`)
- **Problem:** On a valid reply, `strcpy(ptr, &rdbuf[4])` copies from a 255-byte card-supplied packet into the devctl output buffer. No length is taken, checked, or passed — the devctl `buflen` is ignored by the caller — and nothing guarantees the card's string is NUL-terminated within the packet, so the copy can also read past `rdbuf`. The destination size is unknowable at this layer, which is exactly why the copy must be bounded there.
- **Fix:** Pass the devctl `buflen` down and copy with an explicit bound, treating an unterminated packet as an error.
- **Justification:** This adds a length parameter to one internal function; it is the minimum needed to bound a copy whose source is an external device.
- **Benefit:** A malformed or hostile card reply can no longer overrun an EE-supplied buffer.
- **Effort:** M · **Risk:** Med · **Confidence:** High (READ)

## RE-RIPTOPL-020 — Hardcoded parental-lock master password in a public header (filed as W04-001)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/include/opl.h:56`
- **Problem:** A master password that disables the parental lock is compiled into the binary as a string literal `#define` in a header. (Value deliberately not reproduced here — location only.) Anyone with the source — i.e. anyone, this is a public repo — knows the override for every install's parental lock. The per-user password is a separate config key (`parental_lock_password`, `include/config.h:216`), so the master adds a universal backdoor on top of it.
- **Fix:** Delete the master-password define and the code path that accepts it, leaving only the user-set password. If a recovery path is genuinely needed, it should not be a constant in source.
- **Benefit:** Removes a universal, unchangeable credential from every shipped build.
- **Effort:** S · **Risk:** Low (feature removal is the point; users keep their own password) · **Confidence:** High — READ.

## RE-RIPTOPL-021 — `HIOCGETSIZE` indexes the partition array with an unchecked EE-supplied index (filed as W09-003)

**Category:** 6 bugs/security
**Locations:**
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:995-997 (`case HIOCGETSIZE: rv = fileSlot->parts[*(u32 *)argp].length;`)
- RiptOPL/modules/hdd/apa/src/hdd.h:16-24 (`parts[APA_MAXSUB + 1]` = 65 entries)

**Problem:** The index comes directly from the ioctl2 argument with no comparison against `fileSlot->nsub` or the 65-entry array, and `argp` itself is never NULL-checked. An out-of-range index reads past the file slot (the slot array defaults to a single slot, `apaMaxOpen = 1` at hdd_fio.c:35) and returns adjacent IOP memory to the EE as a partition "length". The neighbouring `ioctl2Transfer` in the same file was already fixed by the fork to be bounds- and overflow-safe; this case was missed.
**Fix:** Reject `*(u32 *)argp > fileSlot->nsub` (and NULL `argp`) with `-EINVAL` before indexing — the same check shape `ioctl2Transfer` already uses.
**Benefit:** Closes an EE-triggerable out-of-bounds read / info return in the raw APA interface.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-022 — `err_to_errno` macro never consults its table: every socket error collapses to EIO (filed as W05-001)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/sockets.c:81-98`
- **Problem:** The table at lines 81–93 maps err_t 0…−10 to distinct errnos (ENOMEM, ECONNRESET, EADDRINUSE, …). The macro at 95–98 tests `((err) < (sizeof(err_to_errno_table)/sizeof(int)))`. `sizeof` yields an unsigned type, so any negative `err` is converted to a huge unsigned value and the comparison is false for every error — the macro always returns EIO. Verified by host-compiling the exact macro text in /tmp: err 0 → 0, err −1…−11 → errno 5 (EIO) for all. Second defect in the same lines: `err_t` runs to ERR_IF = −11 (`include/lwip/err.h`), but the table has only 11 entries (0…−10), so even a corrected comparison would index one past the end for ERR_IF.
- **Fix:** Compare `-(err)` against the table length (or cast the size to a signed type) and add the missing ERR_IF entry.
- **Benefit:** Callers above the IOP (SMB/HTTP drivers, EE RPC) can finally distinguish ENOMEM / ECONNRESET / EADDRINUSE / EHOSTUNREACH instead of seeing EIO for everything; error-handling and retry decisions built on errno become meaningful.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; macro behaviour executed in a host test)

## RE-RIPTOPL-023 — Deferred dialog wait is unbounded while the sibling message wait is bounded (filed as W02-002)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/src/ui/guiwait.cpp:49-113` (`runWithMessage`), `RiptOPL/src/ui/guiwait.cpp:115-141` (`runWithDialog`)
- **Problem:** READ. `runWithMessage()` was carefully bounded: it checks the `ioPutRequest` result, supports `timeoutMs_`, sets `gLastDeferredTimedOut`, and documents why an unbounded wait freezes the GUI on wedged storage. `runWithDialog()` checks the put result but then spins `while (*flag_)` with no timeout at all — the exact failure mode the sibling function's comments describe, one class apart in the same file.
- **Fix in words:** Give `runWithDialog()` the same elapsed-time bound `runWithMessage()` already implements (reuse its limit computation; do not add a second timeout mechanism).
- **Justification (addition):** No new mechanism — it extends an existing in-file mechanism to the one caller that lacks it, closing a documented hang.
- **Benefit:** A wedged deferred IO behind a dialog wait degrades to the caller's failure path instead of a hard freeze.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-024 — Self-update overwrites the running ELF in place and never verifies what landed (filed as W02-003)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/src/ui/selfupdate.cpp:265-340` (`suInstallWorker`)
- **Problem:** READ. The download is SHA-256-verified in RAM, then written over the booted ELF with `WRONLY|CREAT|TRUNC`. After `fileXioClose`, success is reported (`suInstalledRev = suRemoteRev; suResult = SU_OK`) with no re-read or hash of the bytes actually on the device. A short/corrupted write that returns positive counts (media error, card pulled behaviour, driver quirk) leaves a damaged ELF that is reported as a successful update — and the old ELF is already gone, by design (no second file, no rename). This is not a request for a backup (banned, and the file's header explains the space constraint); the missing step is verification of the write itself.
- **Fix in words:** After close, re-open the file read-only, stream it through the same SHA-256 context type already used, and only set `SU_OK` when the on-device hash matches `suSha`; otherwise report `SU_ERR_SWAP` at a new step number. Delete nothing, add no file.
- **Justification (addition):** Adds a read-back pass only; justified by a concrete problem — today a bad write is indistinguishable from a good one and the failure is unrecoverable.
- **Benefit:** A failed update is reported as failed while the user is still in a running ELF, instead of being discovered at next boot.
- **Effort:** M · **Risk:** Med · **Confidence:** High (READ; device behaviour of a bad write is INFERRED)

## RE-RIPTOPL-025 — The default ship/POPS/POPSTARTER.ELF is byte-identical to the DEBUG variant, not the MAIN variant (filed as W11-010)

- Category: 6 bugs / 8 half-finished
- Locations: `RiptOPL/ship/POPS/POPSTARTER.ELF`; `RiptOPL/ship/POPS/POPSTARTER VERSIONS/DEBUG/POPSTARTER.ELF`; `RiptOPL/ship/POPS/POPSTARTER VERSIONS/MAIN/POPSTARTER.ELF` (same in the Example tree)
- Problem: Scripted MD5 census: all five `POPSTARTER VERSIONS/*` ELFs are distinct binaries of identical size (167,700 B), and the top-level default `POPSTARTER.ELF` — the one `package_edition.py` copies into every kit via `ship/POPS` — hashes identically to the `DEBUG` variant (`bbc61fa490df65d9527fcea7c4118170`), not to `MAIN` (`4a39d44dfb477ea747f5ca5e39ee011e`). Nothing in the tree documents the default being the debug build. INFERRED: either the default was copied from the wrong variant folder, or the DEBUG folder actually holds the intended default and is misnamed — one of the two labels is wrong.
- Fix: Decide which variant is the intended default; make `POPSTARTER.ELF` match it and, if DEBUG-as-default is deliberate, say so in one line next to the files (or in the POPS provenance notes).
- Benefit: Users get the POPStarter build the folder names promise; removes a silent mislabel in every shipped kit.
- Effort: S · Risk: Med (changing the default binary changes shipped behaviour; naming/documentation fix alone is Low) · Confidence: Med (hashes READ/scripted; intent INFERRED)

## RE-RIPTOPL-026 — `.bak` staging files can permanently block later writes and are never cleaned up (filed as W01-018)

- **Category:** 6 bugs/security (robustness), with 8
- **Locations:** `RiptOPL/src/sources/vmcrebuild.cpp:9` (design comment), `:124` (`.new`/`.bak` siblings created), `:173-200` (rebuild completion); `RiptOPL/src/sources/vcdsupport.cpp:2206-2208` (`vcdInstallPopsUdpfsNet` refuses with -3 if a `.bak` exists), `:2269-2360` (`vcdWritePopsUdpfsNet` backup/rollback machinery)
- **Problem:** Two subsystems leave backup siblings behind as a permanent state. vmcrebuild's header states the backup is never deleted ("Nothing here ever deletes the card or its backup"), so every rebuilt VMC leaves a full-size `.bak` card image on the device forever. Worse, the POPSTARTER UDPFS network-config writer treats the mere existence of `<file>.bak` as "an interrupted editor save needs recovery" and returns -3 — so one interrupted write makes all future automatic config installs fail until a human deletes a file they have never heard of, on a memory card, from outside RiptOPL.
- **Fix:** For vmcrebuild: delete the `.bak` once the rebuilt card has been verified and swapped in (keep it only on the failure path). For the UDPFS net config: delete the whole backup/rollback layer — write each small config with the existing `vcdSafeWriteFile` + read-back verify (`vcdWritePopsUdpfsFile` already does exactly this) and drop the `.bak`-existence veto.
- **Benefit:** Rebuilt VMCs stop leaking a card-sized file per rebuild; network config writes can no longer be permanently wedged by a leftover backup; ~60 lines of rollback code removed.
- **Effort:** M · **Risk:** Med (the `.bak` is the recovery story for a genuinely interrupted write; the read-back verify remains) · **Confidence:** High — READ.

## RE-RIPTOPL-027 — DEVCTL STOP_UNIT and STOP_ALL do the opposite of their names (filed as W08-005)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/bdm/bdmfs_fatfs/src/fs_driver.c:1019-1030`
- Problem: READ (code); INFERRED (intended semantics, from the names and the EE caller). `USBMASS_DEVCTL_STOP_UNIT` calls `fatfs_fs_driver_stop_all_bd()` — it stops **every** mounted device. `USBMASS_DEVCTL_STOP_ALL` resolves only the addressed volume and calls `fatfs_fs_driver_stop_single_bd(vol)` — it stops **one**. The bodies are swapped relative to the names. The EE side (`src/sources/bdmsupport.cpp`, read for context) issues `STOP_ALL` against one device's path at terminal teardown to park that drive; today that happens to work only because the implementation is inverted. Anyone who "fixes" a caller to match the name, or calls `STOP_UNIT` expecting one drive parked, will spin down every BDM device — the exact failure the EE teardown comment warns about.
- Fix: Swap the two bodies so each name does what it says, and check the one EE call site still gets single-device behaviour by name.
- Benefit: Removes a live landmine at the IOP/EE contract boundary.
- Effort: S · Risk: Med (EE call-site semantics must be re-checked) · Confidence: High for the inversion (READ), Med for which side callers depend on (INFERRED)

## RE-RIPTOPL-028 — APA partition helpers trust on-disk counts and indices (filed as W03-008)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/src/platform/hdd.cpp:405-437` (`hddGetPartitionInfo`), `RiptOPL/src/platform/hdd.cpp:438` onwards (`hddGetFileBlockInfo`)
- **Problem:** `hddGetPartitionInfo` clamps the on-disk sub-partition count only from above (`if (nsub > APA_MAXSUB)`, lines 423–424); a negative `nsub` read from a corrupt or crafted APA header passes through and the caller's loop/count logic inherits it. `hddGetFileBlockInfo` indexes `subs[stat.private_4]` — a value straight from on-disk file metadata — with no check that it lies within the caller's `subs` array.
- **Fix:** Clamp `nsub` at 0 as well as at `APA_MAXSUB`, and bounds-check the `private_4` index against the array length the caller passes (the function already receives a `max` for its output; the input side needs the same treatment).
- **Justification (fix adds checks):** Both values come from disk metadata, the classic untrusted-input case; two comparisons are the smallest possible guard.
- **Benefit:** Corrupt APA metadata produces an error instead of an out-of-bounds read.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-029 — ELF program-header copies are unvalidated in two loaders, while a third loader in the same tree validates them (filed as W03-011)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/src/platform/elfldr_noreset.cpp:233` (`memcpy(eph[i].vaddr, pdata, eph[i].filesz)`), `RiptOPL/src/platform/system.cpp:1359` (`sysLaunchLoaderElf`), contrasted with `RiptOPL/src/platform/system.cpp:1308` (`sysPlaceIgrOverlay`)
- **Problem:** The PT_LOAD copy loops trust each program header's offset/filesz/vaddr against nothing — the blob size is never consulted. `sysPlaceIgrOverlay`, in the same file as one of the offenders, validates all three before copying, so the tree already contains the correct pattern; the other two paths predate or bypass it. The blobs are embedded/trusted today, which is why this is Low rather than High — but a truncated or patched embedded blob becomes an arbitrary copy.
- **Fix:** Apply the same three comparisons `sysPlaceIgrOverlay` already makes, in the two copy loops. No new helper needed; the pattern is in-tree.
- **Benefit:** Consistent validation across all ELF loading paths.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-030 — `do_bind` dereferences unchecked `udp_new()`/`tcp_new()` results (filed as W05-006)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/api_msg.c:420-437` (do_bind), contrasted with `api_msg.c:505-540` (do_connect)
- **Problem:** do_bind's UDP branch calls `udp_new()` and immediately uses the result in udp_setflags/udp_recv/udp_bind; the TCP branch does the same with `tcp_new()` into setup_tcp. Neither checks for NULL. Exhaustion is realistic: MEMP_NUM_UDP_PCB is 1 in every build (`include/lwipopts.h:65`) and MEMP_NUM_TCP_PCB is 1–2. do_connect, in the same file, checks every allocation — the inconsistency is the local fact. A NULL return means a NULL dereference inside the tcpip thread (INFERRED crash consequence; unchecked use is READ).
- **Fix:** NULL-check both allocations in do_bind, set `conn->err = ERR_MEM` and ACK, mirroring do_connect's existing pattern — no new mechanism.
- **Benefit:** Pool exhaustion returns an error to the caller instead of taking down the stack thread.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-031 — Sequential API and `lwip_accept` dereference NULL conn/PCB states the socket layer itself documents (filed as W05-007)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/api_lib.c:422` (netconn_recv), `api_lib.c:538` (netconn_write), `api_lib.c:261-281` (netconn_peer/netconn_addr); `RiptOPL/modules/network/SMSTCPIP/sockets.c:180-196` (lwip_accept); documented state at `sockets.c:949,1086,1288-1293`
- **Problem:** One mechanism, several sites: missing NULL checks on conn/PCB. netconn_recv reads `conn->pcb.tcp->state` and netconn_write calls `tcp_sndbuf(conn->pcb.tcp)` with no NULL check — yet sockets.c itself documents (and patched getsockopt/setsockopt/getsockname for) the state where a failed connect leaves `pcb.tcp == NULL` with the descriptor still open; recv/write were not patched. netconn_peer/netconn_addr lack the `conn == NULL` check their sibling functions have, and netconn_addr's RAW case dereferences `pcb.raw` unchecked. lwip_accept calls `netconn_peer(newconn, …)` without checking netconn_accept's return (api_lib.c:383-398 returns NULL for a NULL/failed conn), and dereferences `*addrlen` and memcpy's into `addr` unconditionally.
- **Fix:** Add the same NULL guards the sibling functions already use (return ERR_CONN / set EBADF-style errno), and check newconn in lwip_accept before netconn_peer.
- **Benefit:** A failed connect followed by a recv/write — a normal application error path — returns an error instead of dereferencing NULL inside the stack.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-032 — `pbuf_realloc` use-after-free chain through `ps2ip_mem_realloc` (filed as W05-008)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/ps2ip.c:558-569`; `RiptOPL/modules/network/SMSTCPIP/mem.c:109-121`; `RiptOPL/modules/network/SMSTCPIP/pbuf.c:438-441`
- **Problem:** `ps2ip_mem_realloc` frees the old block first, then calls AllocSysMemory for the new size; if that fails it returns NULL with the original block already gone (the comment at ps2ip.c:556-557 admits the code assumes this cannot fail). `mem_realloc` propagates the NULL, and `pbuf_realloc` ignores the return value entirely and keeps adjusting and using the chain (`q->len` at pbuf.c:441) — a use-after-free on the allocation-failure path.
- **Fix:** Make ps2ip_mem_realloc allocate the new block before freeing the old one and return the old block on failure (the standard realloc contract); that single change fixes every caller, including pbuf_realloc's unchecked use.
- **Benefit:** Memory pressure degrades to an allocation error instead of corrupting a live pbuf chain.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; failure likelihood INFERRED)

## RE-RIPTOPL-033 — `copy_from_pbuf` walks off the end of the pbuf chain (filed as W05-010)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/ip_frag.c:61-81,153`; `RiptOPL/modules/network/SMSTCPIP/ip.c:241`; `RiptOPL/modules/network/SMSTCPIP/pbuf.c:410-413`
- **Problem:** copy_from_pbuf loops `for (; len; p = p->next)` dereferencing `p->len` with no NULL check. The length comes from the fragment's IPH_LEN, and ip_input's preceding `pbuf_realloc(p, ntohs(IPH_LEN))` cannot grow a chain (pbuf.c:410-413 returns the chain unchanged when new_len ≥ tot_len), so a fragment whose claimed length exceeds the received bytes reaches ip_reass with a shorter chain than the copy loop expects, and the loop dereferences NULL. (Trigger construction INFERRED; the unguarded walk and the no-grow realloc are READ.)
- **Fix:** Stop the walk at chain end (treat as a length error and drop the fragment).
- **Benefit:** A truncated/lying fragment is dropped instead of dereferencing NULL in the reassembly path.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ code, INFERRED trigger)

## RE-RIPTOPL-034 — Socket entry points dereference caller sockaddr pointers unchecked; `lwip_sendto` ignores connect/disconnect errors (filed as W05-020)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/sockets.c:231-232` (lwip_bind), `sockets.c:300,307-308` (lwip_connect), `sockets.c:559-575` (lwip_sendto)
- **Problem:** lwip_bind dereferences `name` (family/port/addr) with no NULL, namelen, or family check; lwip_connect does the same. lwip_sendto dereferences `to` the same way and then ignores the return values of all three of its netconn_connect / netconn_disconnect calls (lines 566, 573, 575), so a failed temporary connect for an unconnected UDP socket still proceeds to send and reports the send result as if the connect had worked.
- **Fix:** NULL/length/family-check the sockaddr arguments (EINVAL-style errno, matching the checks getsockopt already does in this file) and propagate netconn_connect's error in lwip_sendto.
- **Benefit:** Bad arguments from any IOP caller return errors instead of dereferencing NULL inside the stack; UDP sendto stops reporting success on a failed connect.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-035 — `lwip_recvfrom`: error conflated with EOF, and the SMB header index is trusted (filed as W05-021)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/sockets.c:352-402` (signature comment at 352; failure return at 383-390; header copy/index at 401-402); declaration context at `RiptOPL/modules/network/common/smstcpip.h:93-101`
- **Problem:** This tree's exported lwip_recvfrom is not the BSD function: it takes (header, index, payload, plen) for the SMB driver's split header/payload reads. Two local defects: (1) when netconn_recv fails/returns NULL, the function returns 0 with errno left at 0, so a caller cannot distinguish a stack error from an orderly EOF/empty datagram. (2) Line 402 computes `index = ((u8_t*)header)[index] + 4`, reading header[index] with a caller-supplied index that is never checked against the 63 bytes actually copied into the header buffer at line 401 — an out-of-range index reads beyond the copied header (READ code fact; how far a real caller can push it is INFERRED).
- **Fix:** Return −1 with the netconn error mapped through err_to_errno on the failure path, and clamp/validate index against the copied header length before dereferencing it.
- **Benefit:** The SMB receive path — the stack's main in-game workload — reports errors as errors and stops trusting a wire-adjacent index.
- **Effort:** S · **Risk:** Low · **Confidence:** High for (1) (READ), Med for (2) (READ code, INFERRED impact)

## RE-RIPTOPL-036 — NBNS resolver validates only the header size, then reads the answer at a fixed offset (filed as W06-007)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/nbns/nbns.c:179`, `:191`, `:198`
- **Problem:** `nbnsReceiveThread()` accepts any datagram with `length >= sizeof(struct NbHeader)` (12 bytes), runs `decode_name()` on the name field without validating that the datagram actually contains a full 32-byte encoded name plus question trailer, and then copies the resolved address from the fixed offset `sizeof(NbHeader) + 34 + sizeof(NbQuestionTrailer) + 4 + 2 + 2` (byte 58, 4 bytes). A short crafted datagram — anything ≥12 bytes that passes the name-question match — makes the resolver copy stale bytes from the static receive frame and report them as the server's IP address. `decode_name()` itself stays inside the 576-byte frame; the defect is the missing total-length validation before the fixed-offset read. READ.
- **Fix:** Require the datagram length to cover the full expected answer layout (header + name + trailer + address) before decoding/copying; drop shorter packets.
- **Benefit:** Name resolution is how the client finds the SMB server; a spoofed short reply on the LAN currently steers it to an arbitrary address built from stale buffer contents.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-037 — smb_fio path handling can walk before the buffer and can leave stored names unterminated (filed as W06-009)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/smbman-ra/smb_fio.c:312-313` (`prepare_path`), `:660-663` (`smb_dopen`), `:361` (`smb_open`)
- **Problem:** Two local string defects. (a) `prepare_path()` sets `p2 = &path[strlen(path)]` and walks backwards while `*p2` is a slash; for an empty or all-slash path `p2` steps in front of the buffer before the loop condition saves it. (b) `smb_dopen()` does `strncpy(fh->name, path, 255)` into a 256-byte field — a path of ≥255 chars leaves `fh->name` unterminated — and immediately `strcat(fh->name, "\\")` and `strcat(fh->name, "*")`, reading/writing past the field; `smb_open()` has the same unterminated-`strncpy` pattern at line 361. Paths here come from the EE side, so this is robustness against malformed local input rather than a wire attack; severity INFERRED.
- **Fix:** Guard the backward walk with `p2 > path`, and bound the stored name so it is always NUL-terminated with room for the two appended characters (or reject over-long paths, as other drivers in this tree do).
- **Benefit:** Removes two memory-safety defects in the SMB file layer for a few comparisons.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ) for the code patterns; Med (INFERRED) for reachability with a ≥255-char path

## RE-RIPTOPL-038 — Skip-Videos check reads before the path buffer for short filenames (filed as W07-002)

- **Category:** 6 bugs/security
- **Locations:** RiptOPL/modules/iopcore/cdvdman/searchfile.c:215-218
- **Problem:** Under `IOPCORE_COMPAT_0_SKIP_VIDEOS`, the test is `strncmp(&cdvdman_filepath[strlen(cdvdman_filepath) - 6], ".PSS", 4)`. For any searched path shorter than 6 characters, `strlen(...) - 6` underflows (size_t) and `strncmp` reads at a wild address before the static 256-byte buffer. The path comes from the game via `sceCdSearchFile`. READ.
- **Fix in words:** Compute the length once and only do the suffix test when the length is at least 6 (the suffix being tested is 4 chars plus the assumed 2-char stem is the existing intent).
- **Benefit:** Removes an out-of-bounds read on a game-controlled string in the resident driver.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-039 — SMB1 request assembly writes strings into the fixed packet buffer with no bound anywhere (filed as W07-016)

- **Category:** 6 bugs/security
- **Locations:** RiptOPL/modules/iopcore/cdvdman/smb.c:429-460 (`asciiToUtf16` / `setStringField` — unbounded, UTF-16 mode doubles length); call sites at :604-623 (session setup), :695 (tree connect), :783 (open)
- **Problem:** Session-setup, tree-connect and open build their `ByteField` by accumulating `offset += setStringField(...)` with no check against the size of the single static `SMB_buf` (1024 bytes total, command-verified from smb.h `MAX_SMB_BUF 896 + MAX_SMB_BUF_HDR 128`). Today the inputs happen to be fixed-size settings fields, so the total fits — but nothing in the code enforces it: the filename path alone arrives via a 256-byte buffer in device-smb.c and doubles in UTF-16 mode. Any future field growth overflows the packet buffer into adjacent IOP BSS silently. READ; the "fits today" assessment is INFERRED from the field sizes in cdvd_config.h.
- **Fix in words:** Bound the accumulation at the one helper: `setStringField` takes the remaining space and the callers fail the request when it would not fit. No new mechanism — the three call sites already propagate integer results.
- **Justification (addition):** A length parameter on one existing helper is the smallest change that converts an unenforced invariant into an enforced one.
- **Benefit:** The SMB1 logon/open path cannot overrun its only packet buffer, by construction rather than by field-size luck.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ for the missing bound; INFERRED for present-day reachability)

## RE-RIPTOPL-040 — ps2link file read trusts the host's byte count over the caller's buffer (filed as W07-008)

- **Category:** 6 bugs/security
- **Locations:** RiptOPL/modules/debug/ps2link/net_fio.c:385-410 (`nbytes = ntohl(readrly->nbytes)` at line 402; `pko_recv_bytes(..., &buf[0], nbytes)` at line 408)
- **Problem:** `pko_read_file(fd, buf, length)` asks for `length` bytes, then receives however many the reply header claims (`nbytes`, host-controlled) into `buf` with no comparison against `length`. A buggy or hostile host process overflows the IOP caller's buffer. The only validation is `length < 0` on the request side. READ. (Debug module, but it runs resident on the IOP with the game's memory beside it.)
- **Fix in words:** Refuse or truncate when the reply's `nbytes` exceeds the requested `length`, before the data receive.
- **Benefit:** One comparison removes a remote-controlled IOP buffer overflow.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-041 — fs_ioctl fabricates a 1024-byte argument length for fs_ioctl2 (filed as W08-017)

- Category: 6 bugs/security / 8 half-finished
- Locations: `RiptOPL/modules/bdm/bdmfs_fatfs/src/fs_driver.c:940-943`, consumers at `:845-875`
- Problem: READ. The legacy `fs_ioctl` entry point calls `fs_ioctl2(fd, cmd, data, 1024, NULL, 0)` regardless of the caller's real buffer size. `fs_ioctl2` now trusts `datalen` for the VMC allocation commands (`datalen != sizeof(request)` → `-EINVAL`), so those commands can never succeed via `ioctl` — and for any future command that copies `datalen` bytes from `data`, the fabricated length is an over-read waiting to happen. A wrapper that lies about a length is worse than one that refuses.
- Fix: Make `fs_ioctl` return `-EINVAL`/`-ENXIO` for commands that need a sized buffer (or forward only the size-less commands it historically served), instead of inventing 1024.
- Benefit: Removes a fabricated-length contract at a trust boundary.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-042 — mmce devctl dereferences `arg` unchecked in four cases — the fifth case in the same switch checks (filed as W10-024)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/mmce/mmceman/src/mmce_fs.c:1103` (SET_CARD), `:1116` (SET_CHANNEL), `:1153` (ACK_WAIT_CYCLES), `:1158` (SET_ALARMS); contrast `:1137-1142` (SET_CARD_CHANNEL)
- **Problem:** Four devctl cases do `args = *(u32*)arg` with no NULL or `arglen` check. `MMCE_CMD_SET_CARD_CHANNEL`, added later in the same switch, validates `arg == NULL || arglen < 5` first — proving the requirement was known and the older cases were left behind.
- **Fix:** Apply the same `arg`/`arglen` validation to the four older cases.
- **Benefit:** A devctl with a short or NULL argument can no longer fault the IOP.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-043 — APA cache entries are dereferenced after `apaCacheFree` (filed as W09-005)

**Category:** 6 bugs
**Locations:**
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:510-512 (`apaOpen`: `apaCacheFree(clink);` then `apaPassCmp(clink->header->fpwd, …)` / `clink->header->rpwd`)
- RiptOPL/modules/hdd/apa/src/apa.c:508-518 (`apaGetNextHeader`: frees `clink`, then reads `clink->header->next` and `clink->device` to fetch the next header)
- RiptOPL/modules/hdd/apa/src/apa.c:386-390 (`apaDelete`: frees `clink` inside the loop, then reads `clink->device` / `clink->header->prev` — this instance is inside the dead subtree of W09-001)

**Problem:** `apaCacheFree` (cache.c:179-196) drops the use count and, at zero, links the entry back onto the free list, from where `apaCacheAlloc`/`apaCacheGetHeader` hand it out again. Reading `header` fields after the free works today only because the header storage is a static pool and no allocation happens in between at the first two sites (INFERRED mitigation); it is one intervening allocation away from comparing passwords against, or following `next`/`device` from, a different partition's header.
**Fix:** Copy the two password fields (in `apaOpen`) and `next`/`device` (in `apaGetNextHeader`) into locals before freeing — a few lines moved, nothing added.
**Benefit:** Removes a use-after-release idiom from the partition-open and chain-walk paths every HDD launch goes through.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-044 — usbmass_bd SCSI: READ CAPACITY's last-LBA is stored as the sector count (off by one, both variants) (filed as W09-012)

**Category:** 6 bugs
**Locations:**
- RiptOPL/modules/usb/usbmass_bd/src/scsi.c:262-264 (`bd->sectorCount = getBI32(&rc10d.last_lba)`)
- RiptOPL/modules/usb/usbmass_bd/src/scsi.c:281-284 (same assignment from `rc16d.last_lba_*`)
- Contrast: RiptOPL/modules/hdd/atad/src/ps2atad.c (`g_ata_bd[i].sectorCount = devinfo[i].total_sectors…`, a true count from IDENTIFY)

**Problem:** READ CAPACITY (10) and (16) return the LBA of the *last* block, so the block count is that value + 1. Both assignments store it unincremented, so the BDM device for every USB drive reports one sector fewer than the drive has — inconsistent with the ATA BDM device in the same tree, which stores a true count. The practical loss is one 512-byte sector at the end of the volume (INFERRED impact: partition sizing/filesystem end-of-volume calculations inherit the short count).
**Fix:** Store `last_lba + 1` in both places (with the existing `0xffffffff` sentinel check for the 16-byte path kept before the increment).
**Benefit:** USB and ATA block devices report capacity the same way; the final sector of every USB volume becomes addressable.
**Effort:** S · **Risk:** Low · **Confidence:** Med (READ for the assignments; the READ CAPACITY semantics are INFERRED from the field name `last_lba` in the code's own struct)

## RE-RIPTOPL-045 — Partition notification dereferences a possibly-NULL `strchr` result (filed as W02-001)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/src/ui/guinotify.cpp:108-115`
- **Problem:** READ. In `Notifications::show()`, the `showPart_` branch does `col_pos = strchr(gOPLPart, ':'); col_pos++;` with no NULL check, then passes `col_pos` to `snprintf("%s")`. Every sibling branch in the same function (lines 120, 128, 136) does check for NULL — this one does not. If `gOPLPart` ever contains no `':'`, this is a NULL+1 dereference / undefined behaviour on the render path.
- **Fix in words:** Add the same NULL guard the sibling branches already use; skip the partition notice when there is no colon.
- **Benefit:** Removes a crash-class defect on a per-frame render path for one line.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-046 — IOPRP patching runs `strcmp`/`strcpy` on fixed 10-byte ROMDIR names that need not be NUL-terminated (filed as W03-012)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/src/platform/ioprp.cpp:8` (`char fileName[10]`), `RiptOPL/src/platform/ioprp.cpp:43-58` (`patch_IOPRP_image`)
- **Problem:** ROMDIR entry names are a fixed 10-byte field, not a C string; a name that fills all 10 bytes has no terminator. The patch loop calls `strcmp(romdir_in->fileName, "CDVDMAN")` etc. and `strcpy(romdir_out->fileName, romdir_in->fileName)`, both of which read (and for `strcpy`, write) past the field when that happens. Legacy IOPRP images are exactly the kind of input where a full-width name occurs.
- **Fix:** Compare with `strncmp(..., 10)` and copy with a bounded copy of at most 10 bytes — the same treatment the field's format already implies.
- **Benefit:** Removes an over-read/over-write on legacy image parsing.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-047 — Network IP settings are parsed with unchecked `sscanf`, while the RA host IP in the same file is parsed properly (filed as W03-018)

- **Category:** 6 (bugs)
- **Locations:** `RiptOPL/src/app/settingsio.cpp:1931`, `:1936`, `:1950`, `:1952`, `:1954`, `:1956` (unchecked parses); contrast `RiptOPL/src/app/settingsio.cpp:1518-1544` (`configReadRA`)
- **Problem:** Six sites in `_loadConfig` do `sscanf(temp, "%d.%d.%d.%d", &ip[0], ...)` with no return check and no range check: a malformed stored value ("192.168.x", "999.1.1.1", trailing junk) leaves some octets at whatever the globals already held and others overwritten, silently producing a mixed old/new address. The same file's `configReadRA` does this correctly — it requires `sscanf(...) == 4` and range-checks every octet 0–255 — proving the pattern is known in-tree and simply wasn't applied to the network settings.
- **Fix:** Apply the `configReadRA` treatment to the six sites (check the count, range-check, keep the old value on failure). Six small identical edits; if a shared helper is preferred it qualifies under the house rule — the same logic would then exist in 7 places, and sharing removes code overall.
- **Benefit:** A hand-edited or corrupt conf_network.cfg can no longer produce a franken-IP that the UI then displays as if it were the stored one.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-048 — `lngGetFilePath` indexes `languages[langID - 1]` with no range check (filed as W03-029)

- **Category:** 6 (bugs)
- **Locations:** `RiptOPL/src/core/lang.cpp:550`
- **Problem:** The function returns `languages[langID - 1].filePath` directly. A `langID` of 0 (or beyond the table) reads outside the array. Whether a caller can currently supply such an ID was not established in scope — the function's own contract does nothing to prevent it, and its neighbours in the language module do validate.
- **Fix:** Clamp/validate `langID` against the table count at the top of the function and return the default language's path (or NULL, matching what callers already handle for missing languages) when out of range.
- **Benefit:** One bad ID becomes a fallback instead of an out-of-bounds read.
- **Effort:** S · **Risk:** Low · **Confidence:** Med — the unchecked index is READ; caller reachability is INFERRED

## RE-RIPTOPL-049 — `icmp_dest_unreach` uses an unchecked `pbuf_alloc` result (filed as W05-011)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/icmp.c:138-147`; callers at `RiptOPL/modules/network/SMSTCPIP/ip.c:319` and `RiptOPL/modules/network/SMSTCPIP/udp.c:380`
- **Problem:** icmp_dest_unreach allocates `q = pbuf_alloc(PBUF_IP, 8 + IP_HLEN + 8, PBUF_RAM)` and immediately writes through `q->payload` and memcpys into it, with no NULL check. It is reachable from ip_input's default-protocol case and from udp_input's no-matching-PCB case — i.e. from ordinary inbound traffic. In the in-game build MEM_SIZE is 0x400, so RAM allocation failure is not exotic (likelihood INFERRED; the unchecked use is READ).
- **Fix:** NULL-check q and return (freeing nothing) on failure, as other pbuf_alloc call sites in the tree already do.
- **Benefit:** Memory pressure during inbound traffic returns no ICMP reply instead of a NULL dereference.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-050 — `ps2ip_input` interrupt path permanently loses queue slots on allocation failure (filed as W05-012)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/ps2ip.c:189-242` (INTERRUPT_CTX_INPKT path, Makefile default on)
- **Problem:** In the interrupt-context input path, the `u8LastMSG` index is advanced (lines 223-224) before `memp_malloc` is attempted (line 228). On allocation failure the slot is never queued and nothing ever advances `u8FirstMSG` past it, so each failure permanently burns one queue slot and the first/last accounting drifts for the rest of the session. The full/empty checks and index updates are also done with no interrupt suspension. In the same path, InputCB (lines 189-193) reads the ethertype from `pInput->payload` with no minimum-length check and ignores `pbuf_header(pInput, -14)`'s return.
- **Fix:** Allocate first and only advance u8LastMSG on success; check the pbuf_header return and a minimum frame length before reading the ethertype.
- **Benefit:** Transient pool exhaustion costs one dropped packet, not a permanently shrinking input queue.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the sequencing (READ); session-level impact INFERRED

## RE-RIPTOPL-051 — `common/smstcpip.h` import ordinals disagree with SMSTCPIP's export table (filed as W05-014)

- **Category:** 6 bugs / 8 half-finished features
- **Locations:** `RiptOPL/modules/network/common/smstcpip.h:37-42,127-130`; `RiptOPL/modules/network/SMSTCPIP/exports.tab:36-47`
- **Problem:** Counting both tables (export ordinals verified against the comments in exports.tab itself): `I_pbuf_dechain` is declared as import 41, but export slot 41 is pbuf_take; `I_pbuf_take` is declared as import 42, but slot 42 is etharp_lookup_mac — and only in INGAME_UDP builds; `I_inet_aton` (43) and `I_inet_ntoa` (44) have no export slots at all, since the table ends at 42. Any IOP module that uses these four imports from this header binds the wrong function or a nonexistent slot. (pbuf_dechain does not even exist in the stack — see W05-015.)
- **Fix:** Delete the four wrong import declarations from smstcpip.h. Nothing in the stack provides dechain/aton/ntoa as exports, so there is no correct ordinal to substitute; deletion is the LESS fix.
- **Benefit:** Removes a silent wrong-function binding trap for the next module that includes this header.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; both tables read and counted this session)

## RE-RIPTOPL-052 — `recv_udp` leaks the pbuf when the connection has no receive mbox yet (filed as W05-019)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/api_msg.c:89-106` (recv_udp), with setup at `api_msg.c` do_newconn (PCB + recv callback installed before any recvmbox exists; recvmbox is created later in netconn_bind/connect)
- **Problem:** recv_udp delivers the incoming pbuf to `conn->recvmbox` only if it is not SYS_MBOX_NULL; in the NULL case the pbuf is neither delivered nor freed — there is no else branch. The callback is installed at netconn creation, before bind/connect creates the recvmbox, so a datagram arriving in that window leaks one pbuf from a small pool each time. (Reachability window INFERRED; the missing free is READ.)
- **Fix:** Add the one-line else branch: `pbuf_free(p)`.
- **Justification (adds one line):** It is the smallest possible change and matches how every other drop path in the file treats an undeliverable pbuf.
- **Benefit:** Early datagrams cost a drop, not a permanent pbuf-pool leak.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ code, INFERRED reachability)

## RE-RIPTOPL-053 — Peer MSS of 0 is accepted and yields zero-length segments (filed as W05-023)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/tcp_in.c:1104-1108` (tcp_parseopt); `RiptOPL/modules/network/SMSTCPIP/tcp_out.c:128-134` (tcp_enqueue)
- **Problem:** tcp_parseopt clamps the peer's MSS option only from above (`mss > TCP_MSS ? TCP_MSS : mss`). An MSS option value of 0 is stored as pcb->mss = 0, after which tcp_enqueue's segment-length computation produces 0-length segments until the TCP segment pool is exhausted and sends fail with ERR_MEM. (Chain INFERRED from the two read sites; no lower clamp exists at the parse site — READ.)
- **Fix:** Clamp the parsed MSS to a sane minimum (e.g. reject/clamp to at least the minimum the stack itself would send) at the parse site.
- **Benefit:** One malformed SYN option from a peer can no longer wedge the connection's send path.
- **Effort:** S · **Risk:** Low · **Confidence:** Low (mechanism READ, end-to-end effect INFERRED)

## RE-RIPTOPL-054 — httpclient's response reader copies without knowing the caller's buffer size (filed as W06-008)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/httpclient/httpclient.c:225`; mitigating caller at `RiptOPL/modules/network/httpclient/main.c:45-50`
- **Problem:** `HttpGetResponse()` does `memcpy(buffer, ptr, DataAvailable)` (up to 256 bytes per chunk) without checking against the caller's `*length`. Today the only in-tree caller passes a 512-byte `DmaBuffer` with `out_len` pre-clamped to 512, so the defect is latent rather than live — but the function's own contract doesn't enforce it, and the header lives in `modules/network/common/`, inviting other callers. READ.
- **Fix:** Pass the remaining capacity into the copy (the length is already an in/out parameter) and stop at the smaller of `DataAvailable` and remaining space.
- **Benefit:** Removes a latent overflow at an API boundary for the cost of one comparison.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ); live impact Low because of the single clamped caller (READ)

## RE-RIPTOPL-055 — raudp discovery compares a sender address that may never have been filled in (filed as W06-010)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/network/raudp/raudp.c` (`ra_discover`, pinned/caduceus branch)
- **Problem:** In pinned mode the discovery loop inspects `from.sin_addr` after `lwip_recvfrom()` without first checking that the call returned ≥0; on a timeout/error return the `from` struct can hold whatever was there before, and the comparison decides whether a "pinned" server matched. INFERRED from the read of the loop; the surrounding code is otherwise carefully bounded.
- **Fix:** Only evaluate the sender when the receive returned a datagram.
- **Benefit:** The pin check — a trust decision — never runs on uninitialised data.
- **Effort:** S · **Risk:** Low · **Confidence:** Low (INFERRED)

## RE-RIPTOPL-056 — rapull setup leaks earlier allocations when a later one fails (filed as W06-011)

> **WITHDRAWN (Stage B):** No leak exists: `rp_setup` frees the scratch `bits` block on both paths and the sole caller `rp_thread` frees every persistent block on any setup failure (rapull.c:337-347).

- **Category:** 6 (bugs/security) / 4 (size/memory)
- **Locations:** `RiptOPL/modules/network/rapull/rapull.c:130-140`, `:199`
- **Problem:** `rp_setup()` makes five `AllocSysMemory` calls in sequence; if a later one fails, the blocks already allocated are not freed before the failure propagates, leaking IOP memory for the life of the module. The rest of rapull is carefully bounded (READ). INFERRED impact is small (setup-time, few blocks), but the fix is mechanical.
- **Fix:** Free the blocks obtained so far on each failure exit (or allocate into locals and publish only on full success).
- **Benefit:** Failed initialisation leaves no residue in a memory-constrained IOP.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ pattern, INFERRED frequency)

## RE-RIPTOPL-057 — BDM VMC exports dereference the block device with no NULL check (filed as W07-004)

- **Category:** 6 bugs
- **Locations:** RiptOPL/modules/iopcore/cdvdman/device-bdm.c:266-310 (`g_bd->read` at line 279, `g_bd->write` at line 302); contrast RiptOPL/modules/iopcore/cdvdman/device-bdm.c:229-230
- **Problem:** `DeviceReadSectors()` checks `g_bd == NULL` and returns `SCECdErTRMOPN`, but `bdm_readSector()` / `bdm_writeSector()` (the oplutils exports MCEMU calls for VMC pages) take the semaphore and call through `g_bd` unconditionally. `g_bd` is NULL before `bdm_connect_bd()` attaches and is reset to NULL on disconnect, so a VMC access in either window is a NULL function-pointer call on the IOP. READ.
- **Fix in words:** Return 0 (transfer failed, the contract these exports already use) when `g_bd == NULL`, inside the existing semaphore, in both functions.
- **Benefit:** A mistimed VMC access fails the save instead of crashing the IOP.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-058 — HDD DeviceInit retries the APA header read forever (filed as W07-006)

- **Category:** 6 bugs / 8 half-finished
- **Locations:** RiptOPL/modules/iopcore/cdvdman/device-hdd.c:70-73
- **Problem:** `while ((r = sceAtaDmaTransfer(...)) != 0) DelayThread(2000);` has no bound, no timeout and no exit. If the drive/APA header never reads, `_start` never finishes and the failure is an indistinguishable hang. The neighbouring drivers in the same scope all bound their waits (MMCE 10 s, UDPFS 30 s, SMB/SMB2/NFS bounded attempts — all READ in this pass). READ.
- **Fix in words:** Bound the loop with a fixed attempt count in the same style as the other device drivers and fail DeviceInit when it is exhausted.
- **Benefit:** A dead HDD surfaces as a failed init instead of a permanent silent hang.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-059 — HDPro transfer loop retries a failing command indefinitely (filed as W07-007)

- **Category:** 6 bugs
- **Locations:** RiptOPL/modules/iopcore/cdvdman/hdpro_atad.c:692-720 (comment at line 714: "retry indefinitely until the I/O operation succeeds"; `continue` at lines 717 and 719)
- **Problem:** In `sceAtaDmaTransfer()`, if `sceAtaExecCmd()` or `sceAtaWaitResult()` keeps failing, the loop `continue`s without advancing `lba`/`nsectors` and without any retry cap — while holding `ata_io_sema`. A persistently failing device wedges all disc access forever. The non-HDPro atad.c in the same directory caps the equivalent retry at 3. READ.
- **Fix in words:** Give the loop the same bounded retry count as atad.c and return the error when it is exhausted (the semaphore release path already exists below the loop).
- **Benefit:** A failing HDPro device returns an error the game/loader can show instead of locking the driver.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-060 — ps2link receive loop spins forever when the host closes the connection (filed as W07-009)

- **Category:** 6 bugs
- **Locations:** RiptOPL/modules/debug/ps2link/net_fio.c:88-100
- **Problem:** `pko_recv_bytes()` loops `while (left > 0)`, treats only `len < 0` as an error, and does `left -= len`. A `recv()` return of 0 (orderly close) never decreases `left` and never exits — the fileio thread busy-loops forever on a disconnect. READ.
- **Fix in words:** Treat `len == 0` as end-of-stream and return an error, next to the existing `len < 0` check.
- **Benefit:** Host disconnect becomes an error return instead of a permanently spinning IOP thread.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-061 — ps2link exception handler dereferences a possibly-NULL module name and copies unbounded (filed as W07-010)

- **Category:** 6 bugs
- **Locations:** RiptOPL/modules/debug/ps2link/excepHandler.c:55-70 (`ExceptionGetModuleName` returns 0 when no module matches), :78-92 (`strlen(module_name)` at line 80, `memcpy(buffer, frame, BUFFER_SIZE)` at line 86, `memcpy(buffer, module_name, len + 1)` at line 92)
- **Problem:** When the faulting EPC belongs to no listed module, `module_name` is NULL and `strlen(NULL)` faults inside the exception handler itself. Independently, the frame copy writes `BUFFER_SIZE` bytes starting one word into a `BUFFER_SIZE`-byte buffer, and the trailing name copy is bounded only by the name's length, not by the 128 bytes of slack the buffer size allows for it. READ.
- **Fix in words:** Substitute a fixed placeholder string when the lookup fails, and bound both copies by the space actually left in `excep_buffer`.
- **Benefit:** The crash reporter stops being a second crash source.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-062 — udptty write path always reports 0 bytes written and discards the send result (filed as W07-019)

- **Category:** 6 bugs
- **Locations:** RiptOPL/modules/debug/udptty-ingame/udptty.c:225-236 (`udp_send` ignores `lwip_sendto`'s result, returns 0), :259-268 (`tty_write` returns that 0)
- **Problem:** `tty_write` is the `write` op for the console device that `_start` installs as fds 0/1. It returns 0 for every call regardless of size or success, so any writer that loops until its buffer is consumed (the standard `write` contract) spins or believes nothing was ever written, and a failed `sendto` is indistinguishable from success. READ.
- **Fix in words:** Return the byte count on a successful send and an error when `lwip_sendto` fails — i.e. propagate the result that already exists.
- **Benefit:** The debug console obeys the write contract its own setup relies on.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-063 — GPT driver reports success when its scratch allocation fails (filed as W08-001)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/bdm/src/part_driver_gpt.c:38-42`
- Problem: READ. On `AllocSysMemory` failure `part_connect_gpt` returns `0`. In `bdm_try_mount` (`modules/bdm/src/bdm.c`) a `connect_bd` return of 0 means "mounted": the device is bound to the GPT filesystem, marked mounted, the mount callback fires, and no other filesystem (FatFs) is ever tried — yet zero partitions were created. Every other failure path in the same function returns `-1`. An IOP-memory-pressure moment therefore silently bricks the drive for the session instead of falling through.
- Fix: Return `-1` on allocation failure, like the read/signature failure paths in the same function.
- Benefit: One-line correctness fix; failed mounts stay retryable/fall-through-able.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-064 — USBMASS_IOCTL_GET_LBA dereferences a possibly-NULL block device (filed as W08-003)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/bdm/bdmfs_fatfs/src/fs_driver.c:879-891`
- Problem: READ. `GET_LBA` fetches `bd` via `fatfs_fs_driver_get_mounted_bd_from_index(...)` and immediately reads `bd->sectorOffset` with no NULL check. The two neighbouring cases in the same switch prove the pattern is known: `GET_DEVICE_NUMBER` (same function, ~lines 913-935) checks `bd == NULL` and returns `-ENXIO`, and `GET_DRIVERNAME` was already fixed for exactly this NULL copy. A file still open on a volume whose device has disconnected reaches this case and faults the IOP instead of returning an error.
- Fix: Add the same `bd == NULL → -ENXIO` check `GET_DEVICE_NUMBER` already has.
- Benefit: Removes a known-pattern IOP fault on the disconnect race.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-065 — Mount-info index guard is off by one (and admits negatives) (filed as W08-004)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/bdm/bdmfs_fatfs/src/fs_driver.c:137-146`
- Problem: READ. `fatfs_fs_driver_get_mounted_bd_from_index` guards with `mount_info_index > FATFS_FS_DRIVER_MOUNT_INFO_MAX` (10, = `FF_VOLUMES`), so index 10 — one past the last valid slot 9 — reads `fs_driver_mount_info[10]`, out of bounds. Negative indices are also passed through to the array. Every diskio/fs_driver lookup funnels through this one function, so this is the single place to fix it.
- Fix: Change the guard to `mount_info_index < 0 || mount_info_index >= FATFS_FS_DRIVER_MOUNT_INFO_MAX`.
- Benefit: One-line fix at the choke point for all volume lookups.
- Effort: S · Risk: Low · Confidence: High (READ; reachability of index 10 via a crafted `pdrv` is INFERRED)

## RE-RIPTOPL-066 — bdm_disconnect_fs never unmounts anything (filed as W08-006)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/bdm/src/bdm.c:119-141`
- Problem: READ. `bdm_disconnect_fs` walks the mounts and simply sets `g_mount[i].fs = NULL`. It never calls `fs->disconnect_bd(...)` — contrast `bdm_disconnect_bd` (same file) which does call `fs->disconnect_bd`. If a filesystem module ever disconnects while devices are mounted, its volumes stay mounted inside the filesystem (FatFs keeps the volume, open FILs, and its mount-info slot pointing at the block device) while BDM believes nothing is mounted; a later re-connect double-mounts. INFERRED: today no in-tree caller may exercise this, which is precisely why the asymmetry has survived.
- Fix: In the loop, call `fs->disconnect_bd(g_mount[i].cbd ? cbd : bd)` before clearing the pointer — mirroring `bdm_disconnect_bd` — or, if filesystem disconnect is meant to be unsupported, delete `bdm_disconnect_fs`'s mount loop and the export rather than keeping a version that lies.
- Benefit: Disconnect ordering becomes symmetric; no stale FatFs volumes.
- Effort: S · Risk: Med · Confidence: High (READ)

## RE-RIPTOPL-067 — MBR sanity check is tautological and all-or-nothing (filed as W08-007)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/bdm/src/part_driver_mbr.c:11-32` (check), `:85-120` (connect loop)
- Problem: READ. `valid++` runs unconditionally for all four entries, so `valid == 4` is always true and the function reduces to "at least one entry has a non-zero type and a plausible first LBA". Worse, it is all-or-nothing: one non-zero-type entry with `first_lba == 0` or `>= sectorCount` rejects the **whole disk**, even if the other entries are fine — while the connect loop that follows ignores `first_lba` validity entirely and keys only off `sector_count != 0` and `partition_type != 0`. Checker and consumer disagree about what "valid" means.
- Fix: Delete the separate sanity function; fold its one useful test (skip an entry whose `first_lba` is 0 or ≥ the device size) into the existing per-entry loop as a `continue`, so one bad entry costs one partition, not the disk. Net code removed.
- Benefit: Fewer false whole-disk rejections; one definition of a valid entry.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-068 — diskio GET_SECTOR_COUNT truncates a 64-bit sector count to 32 bits (filed as W08-008)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/bdm/bdmfs_fatfs/fatfs/diskio.c:130-132`
- Problem: READ. `*(unsigned int *)buff = mounted_bd->sectorCount;` — `sectorCount` is `u64` in `struct block_device`, and this FatFs build has exFAT + 64-bit LBA enabled (`ffconf.h`: `FF_FS_EXFAT 1`). FatFs uses `GET_SECTOR_COUNT` to size/validate the volume; a partition over 2^32 sectors (2 TiB at 512 B) wraps to a small number, so the volume geometry FatFs believes is wrong for exactly the large drives GPT support exists for.
- Fix: Write the full `LBA_t` value (`*(LBA_t *)buff = mounted_bd->sectorCount;`).
- Benefit: Correct geometry for >2 TiB volumes; one line.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-069 — MX4SIO: unknown CSD structure version is accepted with a zero/stale sector count (filed as W08-010)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/mx4sio_bd/src/spi_sdcard_driver.c:369-391`
- Problem: READ. `spisd_get_card_info` sets `bd.sectorCount` only when `csd_structure == 0` (v1) or `== 1` (v2). Any other value falls through, the function still returns `SPISD_RESULT_OK`, and `sd_detect` (`modules/mx4sio_bd/src/mx4sio.c:611-646`) connects the device to BDM with `sectorCount` still 0 (first insert) or still holding the **previous card's** capacity (after a swap/recovery). BDM then partitions/mounts against a geometry that does not describe the card in the slot.
- Fix: Add the missing else: return `SPISD_RESULT_ERROR` when the CSD structure is neither 0 nor 1.
- Benefit: Bad/unknown cards are refused instead of mounted with phantom geometry.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-070 — MX4SIO recovery re-reads card geometry in place while BDM/FatFs stay mounted (filed as W08-011)

- Category: 6 bugs/security
- Locations: `RiptOPL/modules/mx4sio_bd/src/mx4sio.c:611-646`, `RiptOPL/modules/mx4sio_bd/src/spi_sdcard_driver.c:393-411`
- Problem: READ. When CMD13 status is non-zero, `sd_detect` calls `spisd_recover()`, which re-initialises the card and re-runs `spisd_get_card_info()` — overwriting `bd.sectorCount` — **without** `bdm_disconnect_bd`/`bdm_connect_bd`. Only if recovery outright fails is the device disconnected. So after a successful "recovery" (including the case where the user swapped cards during the glitch), BDM's partition devices, the bd_cache contents, and the mounted FatFs volume all still describe the old card while the block device now describes a different one. Cached metadata from card A can be written back to card B.
- Fix: In `sd_detect`, disconnect from BDM before attempting recovery and reconnect after it succeeds — the connect/disconnect calls already exist in the same function for the other branches.
- Benefit: Cache/partition state can never outlive the card it describes.
- Effort: S · Risk: Med · Confidence: High (READ)

## RE-RIPTOPL-071 — pademu USB: `outEndp` is never initialised, so the OUT endpoint is never opened on first connect (filed as W10-003)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/pademu/ds34usb.c:570-589` (`ds34usb_init`), `:164` and `:175` (`usb_connect`), `:212-217` (disconnect path)
- **Problem:** `ds34usb_init` sets `controlEndp = -1` and `interruptEndp = -1` for each pad but never sets `outEndp`. The pad array is a zero-initialised global, so `outEndp` starts at 0, which fails the `outEndp < 0` test in `usb_connect` — the OUT endpoint is never opened on first connect, and the `outEndp < 0` failure check at :175 also passes wrongly. Only the disconnect path (:217) ever writes −1, masking the bug after a reconnect. The standalone copy initialises it in `_start` (`modules/ds34usb/iop/ds34usb.c`, read this session) — direct proof of drift (W10-001).
- **Fix:** Set `outEndp = -1` in the same init loop.
- **Benefit:** First-connect LED/rumble output path works instead of silently using endpoint 0.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-072 — Standalone ds34 copies index `power_level[]` with an unvalidated device byte (filed as W10-004)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/ds34usb/iop/ds34usb.c:45-47` (table, 6 entries), `:337-338`; `RiptOPL/modules/ds34bt/iop/ds34bt.c:261` (table), `:1332-1333`
- **Problem:** Both standalone copies do `oldled[0] = power_level[report->Power]` guarded only by `report->Power != 0xEE`. `Power` is a byte supplied by the pad; any value 6–0xED (except 0xEE) reads out of bounds on the 6-entry table and writes the garbage byte to the LED state. The pademu copies of the same code guard with `report->Power <= 0x05`; the standalone copies lost that guard.
- **Fix:** Restore the `<= 0x05` bound in both standalone copies (or the single surviving copy after W10-001).
- **Benefit:** Removes a device-controlled out-of-bounds read.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-073 — Standalone ds34usb SET_LED RPC: EE sends 2 bytes, IOP reads 4 (filed as W10-005)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/ds34usb/ee/libds34usb.c:120-129` (`ds34usb_set_led`); `RiptOPL/modules/ds34usb/iop/ds34usb.c:693-699` (`ds34usb_set_led` → `LED`)
- **Problem:** The EE side sends exactly 2 RPC bytes (port + a single `u8 led`). The IOP handler takes `u8 *led` and passes it to `LED`/`LEDRumble`, which read `led[0..3]`. Bytes 2–3 of the "array" are whatever stale data sits in the IOP RPC buffer. The BT pair does this correctly — its EE `set_led` sends 5 bytes (`modules/ds34bt/ee/libds34bt.c:113-125`).
- **Fix:** Make the USB EE function send the same 4 LED bytes the BT one does (port + 4), matching what the IOP side already expects.
- **Justification:** The fix widens an RPC payload by 3 bytes; it adds no new mechanism — it aligns the sender with the receiver's existing, already-written contract.
- **Benefit:** LED state stops depending on stale RPC buffer contents.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-074 — BT `hid_readReport` length validation is absent (standalone) or offset-blind (pademu) (filed as W10-006)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/ds34bt/iop/ds34bt.c:1276-1290` (`hid_readReport`); `RiptOPL/modules/pademu/ds34bt.c:1214,1220,1227,1271-1273`
- **Problem:** One mechanism, two severities. The standalone copy checks nothing: it reads `data[8]`/`data[9]` and casts `&data[11]` to a full report struct regardless of the received `bytes`. The pademu copy checks `bytes < 19` for DS3 but then derives `press_emu` from `bytes < sizeof(struct ds3report)`, ignoring the 11-byte header the report pointer is offset by, and its DS4 branch calls `translate_pad_ds4` with no length check at all. A short or malformed report from a pad is parsed as if complete.
- **Fix:** One minimum-length check per report type, sized as header + struct, at the top of each branch — the check the DS3 branch already half-implements.
- **Benefit:** Short transfers can no longer be parsed past the received data.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-075 — `get_data` memcpy size is never clamped to the 18-byte pad data union (filed as W10-007)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/pademu/ds34bt.c:1340`; `RiptOPL/modules/pademu/ds34usb.c:508`; caller `RiptOPL/modules/pademu/pademu.c:467`
- **Problem:** Both pademu transports implement `get_data` as `mips_memcpy(dst, ds34pad[port].data, size)` where `data` is an 18-byte union in the pad struct. The caller passes `out_size - 3` with no clamp against 18 anywhere in the chain, so the copy length is bounded only by the caller's buffer size, not by the source.
- **Fix:** Clamp `size` to `sizeof(ds34pad[port].data)` inside both `get_data` copies (one after W10-001).
- **Benefit:** Removes a source-side over-read whose length is controlled from outside the driver.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-076 — pademu USB `readReport` has no transfer length at all (filed as W10-008)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/pademu/ds34usb.c:226-241` (`usb_data_cb`), `:293` (`readReport(u8 *data, int pad_idx)`)
- **Problem:** `usb_data_cb` receives the transfer's `bytes` count but `readReport`'s signature has no length parameter — the information is structurally discarded. `readReport` then casts the buffer to ds3/ds4/guitar/shanwan report structs based only on the pad type. A short transfer is parsed at full struct size.
- **Fix:** Pass `bytes` through to `readReport` and reject reports shorter than the struct being cast to, mirroring the BT side's (fixed, per W10-006) checks.
- **Benefit:** Same short-transfer parsing hole as W10-006, closed on the USB transport.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-077 — `usb_config_set` passes a partly uninitialised `led[4]` to LEDRumble (filed as W10-009)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/pademu/ds34usb.c:247-281` (`usb_config_set`), `:440-443` (LEDRumble copying all four bytes into `oldled`); same shape in `RiptOPL/modules/ds34usb/iop/ds34usb.c` (`usb_config_set`)
- **Problem:** `led` is a stack array. The DS3 branch sets only `led[0]` and `led[3]`; the guitar/other branches set none of it; `LEDRumble(led, 0, 0, pad)` is then called unconditionally (except the ShanWan early return) and copies all four bytes into the pad's persistent `oldled` state.
- **Fix:** Zero-initialise `led[4]` at declaration in both copies.
- **Benefit:** LED state no longer seeded from stack garbage on connect.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-078 — One L2CAP buffer, three different transfer sizes (filed as W10-010)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/pademu/ds34bt.c:334` (buffer is `MAX_BUFFER_SIZE + 32` = 96), `:359` (first bulk transfer requests `MAX_BUFFER_SIZE` = 64), `:1088` (requeue in `l2cap_event_cb` requests `MAX_BUFFER_SIZE + 23` = 87)
- **Problem:** The allocation, the initial transfer, and the requeue each use a different size for the same buffer. The initial 64-byte request truncates anything longer until the first requeue, and the 87-byte requeue size matches neither of the other two numbers — there is no single constant a reader can trust.
- **Fix:** Request the buffer's actual size (`MAX_BUFFER_SIZE + 32`) in both transfer calls, or size the buffer to the protocol maximum actually used — one number, everywhere.
- **Benefit:** Transfer truncation behaviour stops depending on which request happened to be queued.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-079 — Standalone USB `set_bdaddr` transfers 24 bytes, writes 23 (filed as W10-011)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/ds34usb/iop/ds34usb.c:802-830`
- **Problem:** The function fills `usb_buf[0]`, the 6 bdaddr bytes at [1..6], and the 16 link-key bytes at [7..22] — 23 bytes — with no memset, then issues a 24-byte transfer. Byte 23 is whatever the shared `usb_buf` held from a previous command.
- **Fix:** Memset `usb_buf` at function entry (as sibling command functions do) or transfer 23 bytes.
- **Benefit:** Stops sending a stale buffer byte as part of a pairing command.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-080 — Standalone USB driver shares one transfer buffer and one result code across both pads (filed as W10-012)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/ds34usb/iop/ds34usb.c:62` (`usb_buf[96]`), `:231-241` (`usb_data_cb` writing the single global result code), `:701-727` (`get_data`)
- **Problem:** `MAX_PADS` is 2, but there is one `usb_buf` and one global transfer-result variable shared by both pads and by the LED/rumble, DS3-init, and bdaddr command paths, which run under different semaphores. `get_data` additionally waits and polls on the same per-pad semaphore the completion callback signals. Interleaved operations on two pads can overwrite each other's buffer and result. (INFERRED: the interleaving itself is reasoned from the semaphore layout, not observed.)
- **Fix:** Make the transfer buffer and result per-pad members of the existing `ds34pad[]` struct — no new mechanism, just moving two globals into the struct that already exists per pad.
- **Justification:** This adds two fields to an existing struct; it is the smallest change that removes the sharing, and it removes the need for the cross-path semaphore care the current layout demands.
- **Benefit:** Two-pad use stops corrupting single-pad state.
- **Effort:** M · **Risk:** Med · **Confidence:** Med (READ for the sharing; INFERRED for the interleaving)

## RE-RIPTOPL-081 — Exported iLink transaction-context accessors index the context array unchecked (filed as W10-015)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/ilink/iLinkman/src/iLink.c:209-212` (`iLinkTrFree`), `:330-360` (`iLinkGetNodeTrSpeed`/`iLinkSetNodeTrSpeed`); contrast `:219` and `:243` in the same file
- **Problem:** `iLinkTrRead` and `iLinkTrWrite` validate `trContext` against `MAX_CONCURRENT_TRANSACTIONS`; `iLinkTrFree` (which writes into `TransactionContexts[trContext]`) and the two speed accessors index the same array with no check. These are exported functions — any other IOP module can pass an arbitrary index and write outside the array.
- **Fix:** Apply the same bounds check the read/write functions already have. No new mechanism.
- **Benefit:** Closes an exported out-of-bounds write.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-082 — `GetBIOSVersion` parses an uninitialised buffer when the ROM open/read fails (filed as W10-016)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/ilink/iLinkman/src/iLink.c:61-72`
- **Problem:** The open and read results for `rom0:ROMVER` are unchecked; if either fails, `strtoul` runs over an uninitialised stack buffer and the module proceeds with a garbage BIOS version.
- **Fix:** Check both results and bail out with the existing failure path if either fails.
- **Benefit:** Module init no longer depends on stack garbage on a failed ROM read.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-083 — `InitializeConfigurationROM` silently drops and leaks every registered extra CROM unit (filed as W10-017)

- **Category:** 6 (bugs/security) / 4 (size/memory)
- **Locations:** `RiptOPL/modules/ilink/iLinkman/src/iLink_crom.c:249-263`; called from `RiptOPL/modules/ilink/iLinkman/src/iLink.c` (`iLinkSetNodeCapabilities`)
- **Problem:** `InitializeConfigurationROM` resets all 16 `ExtraCROMUnits[]` pointers to NULL without freeing them, and rebuilds the ROM without them. It is called not only at module start but also from `iLinkSetNodeCapabilities`, so any unit another module registered via `iLinkAddCROMUnit` is silently unregistered — and its allocation leaked — the first time node capabilities are set.
- **Fix:** Free the unit allocations before clearing the pointers, and do not reinitialise the unit table from the capabilities setter (rebuild the ROM only).
- **Benefit:** Removes a leak and a silent deregistration of other modules' CROM units.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-084 — mmce command functions transmit uninitialised trailing stack bytes (filed as W10-022)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/mmce/mmceman/src/mmce_cmds.c:109-121` (`set_card`: `wrbuf[7]` never written, 8 sent), `:169-180` (`set_channel`: `wrbuf[6]` never written, 7 sent), `:263-271` (`reset`: `wrbuf[3..4]` never written, 5 sent), `:232-244` (`set_gameid`: sends `len + 5`, one byte past the string it filled)
- **Problem:** Four functions in one file send stack buffers whose final byte(s) were never initialised. The file's own newer function shows the intended pattern — `set_card_channel` explicitly writes a `0xFF` termination byte (:301) — the older four were never brought in line.
- **Fix:** Zero each `wrbuf` at declaration (or write the termination byte, matching `set_card_channel`).
- **Benefit:** Card commands stop carrying stack garbage in their padding/termination bytes.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-085 — mmce_fs truncates every filename length into a `u8` (filed as W10-023)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/modules/mmce/mmceman/src/mmce_fs.c:84` (open), `:420` (remove), `:479` (mkdir), `:538` (rmdir), `:604` (dopen), `:849` (getstat), `:964-965` (rename, both names)
- **Problem:** Every name-taking op computes `u8 len = strlen(name) + 1`. The SIO2 transfer API takes a `u8` size (`mmce_sio2.h`), so the protocol genuinely caps at 255 — but nothing rejects a longer name. A name of 256+ bytes wraps the length (a 300-byte name becomes 45), and the card receives a short, unterminated prefix of the path and acts on it.
- **Fix:** Return `-ENAMETOOLONG` when `strlen(name) + 1 > 255`, once per function at the point the length is computed.
- **Benefit:** Over-long paths fail loudly instead of silently operating on a truncated prefix.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-086 — genvmc segfaults when run without arguments (usage printed, then argv[1] dereferenced) (filed as W11-002)

- Category: 6 bugs
- Locations: `RiptOPL/tools/pc/genvmc/src/genvmc.c:399-402`
- Problem: `main` calls `printUsage()` when `argc != 3` but does not return; it falls through to `strtol(argv[1], NULL, 10)`. Verified by execution in /tmp: compiled with the repo's own flags and run with no arguments, it prints usage and dies with SIGSEGV (exit 139). With one argument it proceeds with `argv[2] == NULL` as the output filename and fails later with a misleading "fatal error (-101)".
- Fix: Return `EXIT_FAILURE` immediately after `printUsage()` when `argc != 3`.
- Benefit: A user's first run of the tool stops crashing; wrong-argument use fails with the usage text only.
- Effort: S · Risk: Low · Confidence: High (READ + executed)

## RE-RIPTOPL-087 — ziso.py decompression loops forever on a corrupt or truncated ZSO block (filed as W11-003)

- Category: 6 bugs
- Locations: `RiptOPL/tools/pc/ziso.py:62-75` (`lz4_decompress`), called from `RiptOPL/tools/pc/ziso.py:176-186`
- Problem: On `LZ4BlockError` the helper trims one byte off the input and retries, with no bound. Once the input is exhausted, decompressing `b""` raises the same error and `b""[:-1]` is still `b""`, so the loop never terminates and never raises — the `except Exception` in the caller never fires. Verified by execution: `lz4_decompress(b"\xff\xff\xff", 2048)` did not return within a 5 s timeout. Any damaged `.zso` hangs the tool at 100 % CPU instead of reporting the bad block.
- Fix: Stop when the trimmed input is empty (raise the error), or bound the retries to the original input length.
- Benefit: Corrupt ZSO files produce an error instead of an infinite hang.
- Effort: S · Risk: Low · Confidence: High (READ + executed)

## RE-RIPTOPL-088 — Unbounded `sprintf` in PadEmu info formatting (safe today only by input clamping) (filed as W02-015)

- **Category:** 6 bugs/security
- **Locations:** `RiptOPL/src/ui/guigame.cpp:1158-1173` (`hex_to_str`, `ver_to_str`), callers at `RiptOPL/src/ui/guigame.cpp:1302-1307`, destination buffers at `RiptOPL/src/ui/guigame.cpp:1137-1142` (`vid_str[5]`, `hci_str[26]`, …)
- **Problem:** READ. `bdaddr_to_str` in the same block was converted to `snprintf(addstr, 18, …)`; its two neighbours still use raw `sprintf` into 5- and 26-byte statics. They fit today only because `ver_to_str` clamps `ma` to ≤ 9 and the version strings in `bt_ver_str[]` (lines 1072-1083, READ) are short — an invariant held in three different places, none of them at the write site.
- **Fix in words:** Convert both to `snprintf` with the destination size passed or `sizeof` the known statics at the call sites, matching `bdaddr_to_str` directly above them.
- **Benefit:** The invariant moves to the write site; adding a longer Bluetooth version string can no longer overflow a static.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-089 — File Manager paste path uses aliased `strcpy` inside another call's argument (filed as W02-019)

- **Category:** 6 bugs/security (fragility)
- **Locations:** `RiptOPL/src/ui/guifilemgr.cpp:446` (`fmDirSlash(parent, sizeof(parent), strcpy(target, parent))`), `RiptOPL/src/ui/guifilemgr.cpp:471` (`strcpy(fmClip[keep], fmClip[i])`)
- **Problem:** READ. Line 446 passes `strcpy(target, parent)` as the source argument to a function whose first argument is `parent` itself — correct only under a specific evaluation/aliasing reading, and unreadable enough that a future edit to `fmDirSlash` can silently break it. Line 471 uses unbounded `strcpy` between fixed `FM_PATH_LEN` clip slots (safe only because both slots share the constant — again an invariant held elsewhere).
- **Fix in words:** Split line 446 into two statements (copy, then slash) and use `snprintf(…, FM_PATH_LEN, …)` at line 471, matching the rest of the file's style.
- **Benefit:** Removes an aliasing trap and an unbounded copy for two lines of diff.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-090 — `sysDeleteFolder` uses a `malloc` result without a NULL check (filed as W03-013)

- **Category:** 6 (bugs/security)
- **Locations:** `RiptOPL/src/platform/util.cpp:857-885`
- **Problem:** The recursive folder delete builds child paths into a heap buffer obtained from `malloc` and passes it to `sprintf` without checking for NULL. On a memory-tight console (see W03-019/W03-020 for how much static/lazy memory this app commits), an allocation failure here is a NULL dereference during a destructive operation.
- **Fix:** Check the allocation and bail out of the recursion on failure — the function's callers already handle a failed delete.
- **Benefit:** Allocation failure becomes a reported failed delete instead of a crash mid-delete.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-091 — `getFileSize` truncates file sizes to `int` (filed as W03-014)

- **Category:** 6 (bugs)
- **Locations:** `RiptOPL/src/platform/util.cpp:48`
- **Problem:** `getFileSize` returns `int` from `lseek`, so any file at or above 2 GB — unremarkable for game images on the devices this app manages — reports a negative or wrapped size to every caller. Callers cannot distinguish "huge file" from an error return.
- **Fix:** Return the platform's off_t-width type (or `long`) and let callers that genuinely need an `int` range-check at their own boundary. If every in-tree caller is dealing with small files, the honest minimal fix is a clamp-to-INT_MAX with the error value reserved — but the return type is the lie; fix that.
- **Benefit:** Removes a silent size wrap on exactly the file class the app exists to manage.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; the set of affected callers is INFERRED — they were not all traced)

## RE-RIPTOPL-092 — ratls parses Content-Length two ways, one of them `atoi`; its body-length global is non-static (filed as W03-032)

- **Category:** 6 (bugs) / 2 (duplication)
- **Locations:** `RiptOPL/src/platform/ratls.c:164` (`finishResponse`), `RiptOPL/src/platform/ratls.c:371` (`raTlsBodyLength`), `RiptOPL/src/platform/ratls.c:423-433` (`raTlsDownload` digit loop)
- **Problem:** `raTlsDownload` parses the length with a hand-rolled digit loop that cannot overflow its accumulator unnoticed, while `finishResponse` parses the same header with `atoi` — no overflow behaviour, no validation — so the two halves of one download disagree on what lengths are acceptable. Separately, `int raTlsBodyLength` at line 371 is a non-static global in a .c file, exporting a symbol nothing outside the file needs.
- **Fix:** Use the existing digit-loop parse in both places (delete the `atoi` path) and make the global `static`.
- **Benefit:** One length parser with one overflow story; one less exported symbol.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-093 — OSDHistory's least-used-slot eviction keeps the evicted slot's old timestamp (filed as W03-034)

> **WITHDRAWN (Stage B):** The takeover path does reset the timestamp — `NewEntry->DateStamp = GetTimestamp();` at OSDHistory.cpp:258, inside the very range cited.

- **Category:** 6 (bugs)
- **Locations:** `RiptOPL/src/core/OSDHistory.cpp:225-260`
- **Problem:** When a new title takes over the least-launched slot in the history file, the slot's launch count is reset for the new title but its timestamp is not — the new entry inherits the previous title's last-played time. Any ordering or display that trusts the timestamp then misplaces the new title. (The file's 1776-byte icon write past the nominal array size was checked and is a documented, intentional HDDOSD quirk — not a finding.)
- **Fix:** Reset the timestamp alongside the count when a slot changes owner — one assignment at the takeover site.
- **Benefit:** History ordering by recency stops lying about freshly added titles.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ; the visible symptom depends on how the OSD consumes the file, which is outside this codebase)

---

## RE-RIPTOPL-094 — IGR font: 7 punctuation glyphs are copies of the '?' bitmap (filed as W04-002)

- **Category:** 6 (bugs/security — data correctness)
- **Locations:** `RiptOPL/ee_core/include/igrmfont.h:25,26,27,33,50,54,55,85`
- **Problem:** The 5×7 font table `igrmFontData[68][7]` contains the identical byte row `{0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}` for 8 different characters (count verified by grep: 8 occurrences). One of them is `?` (line 54), where that bitmap is plausible; the other seven — `"` (25), `#` (26), `$` (27), `*` (33), `;` (50), `@` (55), `^` (85) — are placeholders that render as a question mark in the in-game menu. Any cheat name, path, or status text containing those characters displays wrong.
- **Fix:** Draw real 5×7 bitmaps for the seven characters and replace the placeholder rows. No code changes — the table is already indexed by ASCII 32..95, so the rows cannot simply be deleted.
- **Justification (fix adds data):** The characters are reachable in displayed text and currently render as a different character; correct glyph data is the smallest possible fix.
- **Benefit:** In-game menu renders `" # $ * ; @ ^` as themselves instead of `?`.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (rows compared byte-for-byte).

## RE-RIPTOPL-095 — Ten headers have no include guard at all (filed as W04-003)

- **Category:** 6 (bugs/security — build robustness)
- **Locations:** `RiptOPL/include/OSDHistory.h` (101 lines), `RiptOPL/include/ioprp.h` (6), `RiptOPL/include/nbns.h` (25), `RiptOPL/include/ps2cnf.h` (3), `RiptOPL/include/xparam.h` (34), `RiptOPL/ee_core/include/cd_igr_rpc.h` (1), `RiptOPL/ee_core/include/gsm_api.h` (21), `RiptOPL/ee_core/include/gsm_defines.h` (94), `RiptOPL/ee_core/include/modules.h` (94), `RiptOPL/ee_core/include/tlb.h` (1)
- **Problem:** Grep-verified: these 10 headers have neither `#ifndef` guards nor `#pragma once` (no header in scope uses `#pragma once`; no duplicate guard names exist elsewhere). Most hold only declarations or macros, so double inclusion is currently benign — but `ee_core/include/modules.h` defines an `enum OPL_MODULE_ID` plus typedefs with no guard, so any TU that reaches it twice (directly or via two includers) is a hard compile error. The other 128 in-scope headers all follow the guard convention; these are the exceptions.
- **Fix:** Add the project's usual `#ifndef`/`#define`/`#endif` guard to each of the 10, named after the file like its neighbours.
- **Justification (fix adds lines):** ~3 lines per file prevents a real double-inclusion failure class, and matches the convention the other 128 headers already follow. This is the smallest change that closes the hole; no other restructuring.
- **Benefit:** `modules.h` (and the rest) become safe to include from anywhere, in any order.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (grep census over all 148 in-scope headers).

## RE-RIPTOPL-096 — The per-cheat code cap reuses the flattened-list cap, sizing the lazy cheat table at ~1.03 MB; the constants are duplicated in ee_core (filed as W03-019)

- **Category:** 4 (size/memory) / 2 (duplication)
- **Locations:** `RiptOPL/include/cheatman.h:39-41` and `:67` (`code_t codes[MAX_CHEATLIST]`), `RiptOPL/src/core/cheatman.cpp:454` (`ensureCheatTable`; the file's own comment at line 41 states "~1.03 MB cheat table"), `RiptOPL/ee_core/include/cheat_engine.h:27-29`
- **Problem:** `MAX_CHEATLIST` = `MAX_HOOKS*2 + MAX_CODES*2` = 510 is the cap for a *flattened list of all cheats' codes*. It is also used as the `codes[]` dimension *inside each cheat entry*, so the table costs 250 entries × 510 codes × 8 bytes ≈ 1.03 MB (the code's own comment confirms the figure) — allocated lazily, but committed in full the first time cheats are used, on a 32 MB console. No single cheat can legitimately carry 510 codes; the per-cheat dimension is simply the wrong constant reused. The same three constants are also defined a second time in `ee_core/include/cheat_engine.h`, so the two sides of the EE/ee_core interface can drift independently.
- **Fix:** Give the per-cheat array its own, honest cap (the largest code count a single cheat entry can hold by construction of the .cht parser) and size the table from that; keep the flattened-list cap only where a flattened list is meant. Leave the ee_core copy values identical — or better, note the pair must move together; generating one from the other is out of scope for a LESS fix.
- **Benefit:** The lazy table shrinks by roughly an order of magnitude (exact figure depends on the honest per-cheat cap chosen); one less silent coupling between two headers.
- **Effort:** S · **Risk:** Med (if any .cht in the wild carries more codes per cheat than the new cap, it truncates — the parser's own limits should set the number) · **Confidence:** High (READ + the file's own size comment)

## RE-RIPTOPL-097 — BGM keeps a 768 KB static ring buffer resident even though BGM is off by default (filed as W03-020)

- **Category:** 4 (size/memory)
- **Locations:** `RiptOPL/src/core/bgm.cpp:29` (`BGM_RING_BUFFER_COUNT = 192`), `RiptOPL/src/core/bgm.cpp:72` (static buffer)
- **Problem:** `static char bgmBuffer[192][4096]` commits 768 KB of BSS unconditionally — the file's own comment confirms "768 KB buffer". BGM is an opt-in feature that defaults off (the settings default), so on a default install this memory is reserved for the entire session for a feature that never runs. The cheat table (W03-019) shows the tree already accepts the lazy-allocation pattern for exactly this situation; the BGM ring predates or bypasses it.
- **Fix:** Allocate the ring in `bgmStart` and free it in the stop path, mirroring the cheat table's lazy pattern. No size change, no behaviour change while playing.
- **Benefit:** 768 KB returned to the heap on every install that doesn't enable BGM — on this hardware, that is real headroom for art caches and lists.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-098 — HTTP catalog parsing pre-allocates for the maximum catalog and duplicate-checks in O(n²) (filed as W01-008)

- **Category:** 4 size/memory (with 5 performance)
- **Locations:** `RiptOPL/src/sources/httpsupport.cpp:152-237` (`httpParseCatalog`); cap at `RiptOPL/include/httpsupport.h:19` (`HTTP_CATALOG_ROWS_MAX 2048`)
- **Problem:** Every catalog parse — including a 3-game home server — allocates both row arrays at the 2048-row cap up front (`malloc(sizeof(base_game_info_t) * HTTP_CATALOG_ROWS_MAX)` and the same for extras, lines 165-166) and only shrinks by freeing on error. INFERRED from the header field sizes (`include/supportbase.h:22-31`, `httpsupport.h:14-19`): roughly 0.9 MB for the pair at cap. Separately, each accepted row is duplicate-checked by a linear `strcmp` scan over all previously accepted rows (lines ~197-205), i.e. up to 2048×2047/2 ≈ 2.1 million comparisons at cap (derived arithmetically from the cap constant).
- **Fix:** Grow the two arrays as rows are accepted (the realloc-grow pattern already used in hddsupport.cpp's VCD list builder) instead of allocating the cap; the duplicate scan can stay — at realistic catalog sizes it is not the problem, and replacing it with a hash would be MORE, not less.
- **Benefit:** Memory use proportional to the actual catalog on a memory-constrained EE; removes the worst-case allocation from every refresh.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the allocation and scan structure — READ; the ~0.9 MB figure is INFERRED from header field sizes, not a measured `sizeof`.

## RE-RIPTOPL-099 — xhdd: a 407-line Windows IDENTIFY-device struct (and 512-byte static) to read one 16-bit field (filed as W09-010)

**Category:** 4 size/memory / 3 overengineering
**Locations:**
- RiptOPL/modules/hdd/xhdd/ata_identify.h:1-407 (full `IDENTIFY_DEVICE_DATA` bitfield transcription)
- RiptOPL/modules/hdd/xhdd/xhdd.c:17 (`static IDENTIFY_DEVICE_DATA deviceIdentifyData;`), :70-84 (the only field ever read is `UltraDMASupport`)

**Problem:** The devctl `ATA_DEVCTL_GET_HIGHEST_UDMA_MODE` fills the entire packed struct via `ata_device_identify` and then tests a single field. The 407-line header exists for that one read, and its packed bitfield layout is exactly the kind of thing that silently shifts if the header is ever edited.
**Fix:** Delete ata_identify.h; read the identify data into a plain `u16[256]` buffer and test the UDMA-support word directly (INFERRED: word 88 in the standard ATA IDENTIFY layout, which is where the struct places `UltraDMASupport`).
**Benefit:** 407 lines of header and the layout risk gone; the static buffer stays the same 512 bytes the hardware transfer already requires.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ for the single-field use; the word index is INFERRED from the struct layout)

## RE-RIPTOPL-100 — usbd-ra (MINI_DRIVER): isochronous machinery is fully provisioned although no client in the tree can use it (filed as W09-014)

**Category:** 4 size/memory / 1 dead code
**Locations:**
- RiptOPL/modules/usb/usbd-ra/src/usbd.c:38-55 (MINI config still reserves `maxIsoTransfDesc = 0x40`, `maxIoReqs = 0x100`, `maxTransDesc = 0x40`)
- RiptOPL/modules/usb/usbd-ra/src/hcd.c (initHcdStructs allocates the IsoTD pool and the IsoTD→IoReq LUT from those counts)
- RiptOPL/modules/usb/usbd-ra/src/usbio.c:143-207 (`setupIsocronTransfer`), RiptOPL/modules/usb/usbd-ra/src/usbd.c:96-176 (`processDoneQueue_IsoTd`)
- Evidence of disuse: a grep for isochronous-transfer users across `modules/` outside usbd-ra returns nothing; the only LDD in the tree is bulk mass storage (usbmass_bd) plus the built-in hub driver (interrupt endpoints)

**Problem:** The vendored stack is deliberately kept untouched (Makefile comment), but its *configuration* is this fork's own choice, and the MINI profile still pays IOP RAM for 64 isochronous transfer descriptors plus their LUT and the code paths that service them. The iso done-queue handler even contains an index slip consistent with never running: at usbd.c:130, inside the halted-endpoint drain loop over `curTd`, it clears `memPool.hcIsoTdToIoReqLUT[arg - memPool.hcIsoTdBuf]` (the original TD's slot) instead of `curTd`'s.
**Fix:** Shrink the isochronous pool through the driver's existing `_start` argument surface (`itd=`, and `ioreq=` if headroom allows) at the module's load site, rather than editing vendored sources; leave a nonzero token pool if the HCD struct layout requires one.
**Justification (addition):** No addition — this uses the configuration mechanism the vendored driver already exposes.
**Benefit:** IOP RAM returned to the loader/game; no vendored file changes, so the re-vendor story in the Makefile is untouched.
**Effort:** M · **Risk:** Med (pool sizes must be validated against the HCD allocation arithmetic in initHcdStructs) · **Confidence:** Med (READ for config and allocation; INFERRED that no out-of-tree LDD registers an isochronous endpoint against this IRX)

## RE-RIPTOPL-101 — Both mbedTLS build definitions compile all 109 vendored library sources, including dozens that neither configuration can use (filed as W11-014)

- Category: 4 size/memory (build size/time)
- Locations: `RiptOPL/tools/build_edition.py:114` and `RiptOPL/tools/build_edition.py:137` (`"sources": ["third_party/mbedtls/library/*.c"]`); configurations at `RiptOPL/third_party/mbedtls/riptopl_mbedtls_config.h` and `RiptOPL/third_party/mbedtls/riptopl_mbedtls_slim_config.h`
- Problem: The single glob compiles every one of the 109 `.c` files in `third_party/mbedtls/library/` (scripted count) for both the RA (full) and updater (slim) targets. Neither config enables the TLS server, server-side session cache/cookie/ticket, timing, sockets, debug, memory-buffer-allocator or PSA file storage (both configs READ in full; the vendor test at `.github/scripts/test_mbedtls_vendor.py:128-130` asserts several of these stay off), so the corresponding translation units — `ssl_tls12_server.c`, `ssl_tls13_server.c`, `ssl_cache.c`, `ssl_cookie.c`, `ssl_ticket.c`, `timing.c`, `net_sockets.c`, `debug.c`, `psa_its_file.c`, and the unused cipher/hash units (aria, camellia, des, md5, sha1, ripemd160, …) — can only ever produce empty or unreferenced objects. INFERRED (mbedTLS's per-file config-guard pattern; vendored bodies NOT-READ-BY-DESIGN): the ELF cost is mostly absorbed by the guards, so the certain cost is compile time and build-log noise on every edition build, plus a larger attack-surface review burden than the configs suggest. The glob is currently locked in by the vendor test (`.github/scripts/test_mbedtls_vendor.py:73,86` asserts the exact glob), so the waste is policy, not accident.
- Fix: Replace the glob with an explicit per-configuration source list in `build_edition.py` (the files each config's enabled modules need) and update the vendor test to assert the lists instead of the glob.
- Benefit: Faster, quieter edition builds; the compiled vendored surface matches the reviewed configs.
- Effort: M · Risk: Med (a missed required file fails the link loudly, so the risk is detectable, but both configs must be enumerated) · Confidence: Med (configs and glob READ; per-file emptiness INFERRED)

## RE-RIPTOPL-102 — Whole-file staging for art re-grows its buffer by doubling, re-copying on every cover (filed as W02-013)

- **Category:** 4 size/memory (also 5, structural)
- **Locations:** `RiptOPL/src/gfx/textures.cpp:623-735` (`texStageExternalFileIntoMemory`), cap at `RiptOPL/src/gfx/textures.cpp:160` (`TEX_STAGE_MAX_SIZE` = 8 MiB)
- **Problem:** READ. The fallback arm starts at 64 KiB and `realloc`s at double size per growth step, so a large cover is copied repeatedly as it grows; the ceiling permits an 8 MiB transient allocation per art load on a 32 MiB machine, in a path the art worker runs cover after cover. The fast path (exact-size from `lseek`) already exists in the same function — the doubling loop is the device-cannot-report-size arm.
- **Fix in words:** In the loop arm, grow geometrically but in larger steps is *not* the fix; the net-win fix is to cap the staged size at the largest cover the caches actually accept (the decode side already rejects oversize images) instead of 8 MiB, and to read in the existing 32 KiB chunks directly appended without a realloc-copy when the next chunk would cross capacity by less than one chunk. If no smaller cap can be proven from the decode limits, at minimum state the cap from those limits rather than a round 8 MiB.
- **Benefit:** Bounds worst-case transient heap per cover and removes most realloc copies on the slow-device path.
- **Effort:** M · **Risk:** Med · **Confidence:** Med (READ; the "largest accepted cover" value is INFERRED — it must be taken from the decode path before applying)

## RE-RIPTOPL-103 — utf8.h defines a 364-entry table and a function body in the header (filed as W04-013)

- **Category:** 4 (size/memory)
- **Locations:** `RiptOPL/include/utf8.h:21-49`
- **Problem:** The header defines `static const uint8_t utf8d[]` (the Hoehrmann DFA table, 364 bytes) and a non-inline function `uint32_t static utf8Decode(...)` — note also the reversed qualifier order. Because both are header definitions, every including TU gets its own copy of the table. Grep-verified includers: `src/gfx/fntsys.cpp` and `src/gfx/textshape.cpp` — two copies today, and one more for every future includer.
- **Fix:** Declare `utf8Decode` in the header and move the table plus function body into a single `.cpp`. (Marking the function `static inline` would not help — the table is the bulk and would still duplicate.)
- **Justification (fix adds a file):** It removes a 364-byte table copy per includer and is the standard declaration/definition split the rest of the tree uses; net binary size goes down, not up.
- **Benefit:** One copy of the DFA table in the binary regardless of includer count.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (header read in full; includer count from grep).

## RE-RIPTOPL-104 — `USBA_crc32` keeps a 4× oversized table and rebuilds it on every call (filed as W03-006)

- **Category:** 4 (size/memory) / 5 (performance, structural)
- **Locations:** `RiptOPL/src/platform/system.cpp:465-492`
- **Problem:** `crctab` is declared `unsigned int crctab[0x400]` (1,024 entries, ~4 KB) but the algorithm only ever indexes 256 entries — ~3 KB of BSS is permanently wasted. The 256-entry table is also rebuilt from scratch on every call instead of once. Separately, the CRC's seed is whatever value the table-build loop leaves behind rather than a named initial constant: the output is deterministic but non-standard, and other code (the `ul.*` name checksums) depends on its exact values, so the seed must NOT be "corrected" as part of this fix.
- **Fix:** Shrink the array to 256 entries and build the table once (function-local static with a built flag, or a plain static table). Leave the seed computation byte-for-byte as it is.
- **Benefit:** ~3 KB BSS back on a 32 MB console and removal of per-call table construction, with zero behavioural change.
- **Effort:** S · **Risk:** Low (Med if the seed is touched — don't) · **Confidence:** High (READ)

## RE-RIPTOPL-105 — Stack VLAs sized by caller-supplied lengths in keyboard paths (filed as W02-014)

- **Category:** 4 size/memory (also 6)
- **Locations:** `RiptOPL/src/ui/gui.cpp:173` (`char tmp[maxLength]` in `Gui::showKeyboard`), `RiptOPL/src/ui/diaeditors.cpp:215` (`char pre[maxLen]` inside `diaShowKeyb`'s render loop)
- **Problem:** READ both. Both allocate a variable-length array on the EE stack from a length parameter. Today's callers pass bounded sizes (dialog string sizes), so this is not a demonstrated overflow — it is an unbounded-by-construction pattern: any future caller with a file- or config-derived length risks silent stack exhaustion, and VLAs defeat static stack auditing of these threads.
- **Fix in words:** Replace both with fixed buffers at the existing maximum string size (`UI_STRING_SIZE` for the dialog system), truncating to that bound exactly as the surrounding `strncpy` code already does.
- **Benefit:** Stack use becomes auditable and constant; no behaviour change for any current caller.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; "current callers are bounded" is INFERRED from the dialog string types)

## RE-RIPTOPL-106 — `pio_profile` permanently reserves 8 KiB of IOP RAM for a one-shot start-up check (filed as W10-031)

- **Category:** 4 (size/memory)
- **Locations:** `RiptOPL/modules/pccf/pccf_pio_profile.inc:3` (`profile_ref` + `profile_check`, 4,096 bytes each)
- **Problem:** Two static 4 KiB buffers exist solely so the start-up profile validation can compare two reads of the same sectors. They are used once during `pccf_hw_start` and then sit resident for the life of the module.
- **Fix:** Keep one 4 KiB buffer and compare the second read against a checksum of the first, halving the reservation; or validate in 512-byte chunks reusing the sector buffer the ATA layer already has.
- **Benefit:** 4 KiB of IOP memory returned permanently.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ — sizes are arithmetic from the declaration; the one-shot usage is read from `pio_profile` itself)

---

## RE-RIPTOPL-107 — ship/Example is a 5.9 MB snapshot of which the packager reads only 23 files; its full Neutrino tree is not what ships (filed as W11-001)

- Category: 2 duplication / 7 repo clutter
- Locations: `RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/` (whole tree, 137 files, 5,918,101 B — scripted); consumers at `RiptOPL/tools/package_edition.py:137-142` and `.github/scripts/package_release_app.py:24-26,228,238,248,257` (integration evidence)
- Problem: The packager consumes exactly 23 files / 1,752,047 B from this tree (scripted): the 8 `APPS/APP_RIPTOPL` template files, 8 `ART/*.png`, 6 `POPS` files (`CHEATS.TXT`, `IGR_BG.TM2`, `IGR_NO.TM2`, `IGR_YES.TM2`, `POPSTARTER.KELF`, `TROJAN_7.BIN`), and `EMBER/games/Per-Game-Folder/GAME FILES`. The remaining 4,166,054 B is never read by any tool (repo-wide grep): a complete second Neutrino distribution (78 files, 603,340 B, `neutrino/version.txt` = `v1.8.0-61-g7be8de2`, plus `neutrino/udpfs_server/*.py` sources), byte-duplicates of `ship/POPS/*` and `ship/EMBER/*`, and 5 `.url` files that duplicate `tools/pc/*.url`. The Example Neutrino copy is actively misleading: real kits take Neutrino from the pinned upstream/ORBIT archives at package time (`tools/package_edition.py:144-176`), so this snapshot can drift silently while looking like the shipped payload. The directory is also named `*.ZIP` while being a plain directory.
- Fix: Keep only the 23 consumed template files, under a name that does not claim to be a ZIP (e.g. `ship/app-template/`), and delete the rest of the tree — including the whole `neutrino/` snapshot, the duplicated `POPS`/`EMBER` payloads and the `.url` copies (the packager already takes shortcuts from `tools/pc/`). Move the 6 `POPS`-only files and the 1 `EMBER` file into `ship/POPS` / `ship/EMBER` so each kit source lives in one tree.
- Benefit: −4,166,054 B and −114 files of dead weight; one source per shipped file; no stale fake Neutrino in the repo.
- Effort: M · Risk: Med (a human may copy the Example tree by hand as an install reference — INFERRED; the packager does not) · Confidence: High (consumption READ + grep-verified; user-facing role INFERRED)
- **Coordinator note (consolidation):** the POPS/EMBER-duplication limb of this finding is contested by workstream W14, which found the `ship/Example/` tree to be a deliberate, workflow-maintained mirror of `ship/POPS` + `ship/EMBER` (see `ember-watch.yml:130,154` and `.github/scripts/test_bdma_shipped_pairs.py`) and therefore did not file the duplicates. Treat that limb as "mirror exists by design; slim only with the mirror's owners and its guard test updated" rather than plain deletion. The Neutrino-snapshot limb (78 files that are not what ships) and the never-read remainder are uncontested.

## RE-RIPTOPL-108 — The ds34 pad drivers are maintained twice and have drifted in both directions (filed as W10-001)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/modules/pademu/ds34bt.c` (1,425 lines) vs `RiptOPL/modules/ds34bt/iop/ds34bt.c` (1,719 lines) — 915-line diff; `RiptOPL/modules/pademu/ds34usb.c` (616 lines) vs `RiptOPL/modules/ds34usb/iop/ds34usb.c` (998 lines) — 950-line diff; `RiptOPL/modules/ds34bt/iop/smsutils.S` and `RiptOPL/modules/ds34usb/iop/smsutils.S` are byte-identical (md5 `3612cf1bd66a11f0b9e53ac78a5940d7`)
- **Problem:** Two near-complete copies of each transport driver exist (one embedded in pademu, one standalone with RPC glue). They have drifted in *both* directions, so neither is a superset: the standalone copies have fixes pademu lacks (outEndp initialisation, `port >= MAX_PADS` guards, bounded EE bind loops), and pademu has guards the standalone copies lack (the `report->Power <= 0x05` bound of W10-004, the DS3 length check of W10-006, `rrum * 255` rumble scaling that both standalone copies drop). Every pad bug must currently be found and fixed twice, and W10-002/W10-004/W10-006 below are all instances of one copy being fixed and the other not.
- **Fix:** Keep one source per transport and compile it into both IRX flavours (ps2.yaml already assembles modules from explicit source lists); delete the second copy of each driver. Keep the standalone-only RPC/`_start` glue as the thin layer it is.
- **Benefit:** Removes roughly 2,600 lines of duplicate driver code and the entire class of one-copy-fixed bugs below.
- **Effort:** L · **Risk:** Med · **Confidence:** High (READ; diff/md5/wc output run this session)

## RE-RIPTOPL-109 — Three MD4s, two DESes and two LM/NTLM hash implementations for one SMB feature set (filed as W06-012)

- **Category:** 2 (duplication)
- **Locations:** MD4: `RiptOPL/modules/network/smbinit/md4.c` (175 lines), `RiptOPL/modules/network/smbman-ra/md4.c` (182), `RiptOPL/modules/network/smb2man/usmb2/md4c.c` (240). DES: `RiptOPL/modules/network/smbinit/des.c` (564), `RiptOPL/modules/network/smbman-ra/des.c` (568; `diff` = 119 lines, almost all comments/const qualifiers). LM/NTLM: `RiptOPL/modules/network/smbinit/smbauth.c` vs `RiptOPL/modules/network/smbman-ra/auth.c` (`diff` = 113 lines; same `LM_Password_Hash`/`NTLM_Password_Hash`/`LM_Response`, differing in static-vs-exported linkage and a 512-byte static vs stack password buffer). Counts from `wc`/`diff` runs.
- **Problem:** The same cryptographic primitives are maintained in two or three divergent copies across modules that ship in the same product. They have already drifted (different buffer strategies in the two auth implementations), so a fix in one does not reach the others. The modules are separate IRXs, so runtime sharing is impossible — but the udpfs family in this same tree already demonstrates the source-level pattern: one real source under `udpfs_shared/src/`, with 2-line `#include` wrappers per edition.
- **Fix:** Keep one canonical copy of each primitive in a shared source location and have smbinit/smbman-ra compile that source (the udpfs wrapper pattern), deleting the duplicate files. usmb2's MD4 is part of a vendored upstream library; leave it, or apply the same treatment only if the vendoring rules in that folder allow — the smbinit↔smbman-ra pair alone removes one MD4, one DES and one auth implementation.
- **Justification:** The fix adds nothing; it deletes two-to-three copies and keeps one.
- **Benefit:** ~1,300 lines of duplicated crypto (by `wc`) collapse to one maintained copy per primitive; the static-vs-stack password-buffer divergence disappears.
- **Effort:** M · **Risk:** Med (touch two IRX builds; behaviour must stay identical) · **Confidence:** High (READ + measured diffs)

## RE-RIPTOPL-110 — The native PS2 launch sequence is re-implemented once per backend (filed as W01-002)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/sources/bdmlaunch.cpp:606-999` (`BdmLauncher::launch` tail / `bdmLaunchGame`), `RiptOPL/src/sources/ethsupport.cpp:1282-1520` (`EthSource::launch`), `RiptOPL/src/sources/mmcesupport.cpp:1292-1745` (`MmceSource::launch`), `RiptOPL/src/sources/hddsupport.cpp:2572-2850` (`HddSource::launch`), `RiptOPL/src/sources/udpfssupport.cpp:880-1035` (`udpfsLaunchOplCore`), `RiptOPL/src/sources/httpsupport.cpp:735-860` (`HttpSource::launch`)
- **Problem:** Every backend hand-copies the same ordered ritual: VMC slot loop writing into the mcemu IRX via `nativeVmcSlotWord`, `sbPrepare`, last-played save, cheats (`sbLoadCheats` + `sbCheatsMissingContinue`), `sbLoadImage`, per-part ISO size summation, layer-1 probing (`sbGetISO9660MaxLBA`/`sbProbeISO9660`), `mmceSendGameID`, deinit, `sysLaunchLoaderElf`. The per-part size loop is byte-for-byte the same logic in three files (bdmlaunch.cpp:811-859, ethsupport.cpp:1449-1481, mmcesupport.cpp:1480-1512, each headed by the identical "Sum the size of all parts… upstream OPL #1763" comment). Censuses: `sbPrepare` 11 call sites, `sbLoadCheats` 10, `sbLoadImage` 9, `CheckPS2Logo` 10, `mmceSendGameID` 21 references in scope. Each copy has already drifted in small ways (which steps run, in which order, what is closed on bail), so a fix to the ritual must be made six times.
- **Fix:** One shared launch-sequence helper in supportbase.cpp that takes the already-existing per-backend differences as data (prefix, separator, cdvdman IRX + settings pointer, launch mode string) and performs the common steps once; backends keep only what is genuinely device-specific (their VMC info struct, their settings fields, UDPFS's keep-handle leg). Delete the six copies, including the three duplicated size loops.
- **Justification (fix adds a helper):** The same logic already exists in 6 places and sharing removes far more code than the helper adds — this is the stated exception to the no-new-abstraction rule.
- **Benefit:** One place to fix launch-ordering bugs (the file comments record several that were exactly this: steps running before/after a card switch in one backend but not another); net deletion of several hundred lines.
- **Effort:** L · **Risk:** Med (launch paths are the product; each backend's bail-out cleanup must be preserved in the shared version) · **Confidence:** High — READ.

## RE-RIPTOPL-111 — The PS1 launch (POPSTARTER/VCD and Ember/Cue) is re-implemented once per backend (filed as W01-003)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/sources/bdmlaunch.cpp:58-257` (`BdmLauncher::launchCue`/`launchVcd`), `RiptOPL/src/sources/ethsupport.cpp:998-1160` (`EthSource::launchVcd`/`launchCue`), `RiptOPL/src/sources/mmcesupport.cpp:1187-1290` (`MmceSource::launchCue`/`launchVcd`), `RiptOPL/src/sources/udpfssupport.cpp:766-880` (`UdpfsSource::launchVcd`/`launchCue`), `RiptOPL/src/sources/hddsupport.cpp:2130-2360` (`hddDoLaunchEmber`/`hddDoLaunchVcd`, reached from `HddSource::launchVcd` 2308 and `launchCue` 3223)
- **Problem:** Five separate implementations of the same two hand-offs: reject empty/reserved name → resolve core ELF (POPSTARTER via `vcdResolvePopstarter`, Ember via `cueResolveEmber`/`cueResolveEmberBios`) → prepare (`cuePrepareLaunch`, `vcdConfirmPopsFiles`, BDMA equip for VCD) → RetroGEM barcode → deinit with the right exception → `sysLaunchPopstarter`/`sysLaunchEmber`. The shared primitives already exist in cuesupport.cpp/vcdsupport.cpp; what is duplicated is the sequencing, the dialogs, and the bail-out order — and those have visibly diverged (e.g. only MMCE/BDM quiesce art reads before the VCD leg; only UDPFS hard-gates on `vcdBdmaLaunchReady`; HDD needs its own pfs0: remount dance). Favourites then proxies into whichever copy a device owns (favsupport.cpp:1290-1330), so divergence is user-visible from the FAV tab too.
- **Fix:** One shared PS1 launch sequencer per core in vcdsupport.cpp/cuesupport.cpp, parameterised only by device root and the two genuinely device-specific steps (MMCE art quiesce, HDD pfs0: remount, UDPFS driver gate); delete the per-backend copies and keep thin per-device hooks only where a step truly differs.
- **Justification (fix adds a shared sequencer):** Same logic in 5 places; sharing removes code overall.
- **Benefit:** A PS1 launch fix (name validation, barcode, dialog order) lands once; the FAV tab stops inheriting five different behaviours for the same row.
- **Effort:** L · **Risk:** Med · **Confidence:** High — READ.

## RE-RIPTOPL-112 — Two near-identical iLink driver trees in one repo, already diverged (filed as W08-014)

- Category: 2 duplication
- Locations: `RiptOPL/modules/bdmassault/ilink_bd_assault/src/` (whole tree) vs the parallel tree at `RiptOPL/modules/ilink/` (out of scope, compared only)
- Problem: READ via `diff -rq`: the bdmassault tree duplicates the SBP-2 driver, SCSI layer, and the whole iLinkman manager from `modules/ilink/`, and the copies have **already diverged** beyond include paths — e.g. `scsi.c` differs in struct definitions and static asserts, `sbp2_driver.c`, `iLink.c`, `iLink_DMA.c`, `iLink_PHY.c` all differ. Every iLink fix (and this driver's history is a long chain of subtle SBP-2 fixes, per `PROVENANCE.md`) must be ported by hand or one copy silently rots — the POPSTARTER copy is the one least likely to be exercised in normal testing.
- Fix: Generate the bdmassault variant from the `modules/ilink/` sources at build time with its documented deltas (static GUID/model, no cdvdman imports) applied as a small patch step, and delete the copied tree; if the build system cannot do that today, at minimum add a CI byte/semantic diff check that fails when the trees diverge outside an allowlist. Deleting the copy is the goal; the check is the fallback.
- Benefit: One iLink implementation; fixes stop needing double-porting.
- Effort: L · Risk: Med · Confidence: High that the duplication and divergence exist (READ/diff), Med on the generation approach (INFERRED)

## RE-RIPTOPL-113 — `deinit` and `deinitEx` are the same ~90-line teardown written twice (filed as W03-022)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/src/app/opl.cpp:1278-1370` (`deinitEx`), `RiptOPL/src/app/opl.cpp:1372-1455` (`deinit`)
- **Problem:** Script-verified by extracting both bodies and diffing them: 93 vs 84 lines, and the *only* differences are the signature, the module-cleanup loop (deinitEx spares a second mode), and two `launchDiagMark` calls. Everything else — including the long ordering-rationale comment blocks about the art drain, bgm quiesce and fade — is verbatim. Teardown ordering is the most safety-critical sequence in the app (the comments document freezes caused by getting it wrong), and it now has to be maintained in two places; a fix applied to one is a latent bug in the other.
- **Fix:** Delete `deinit` as a separate body: implement it as `deinitEx(exception, modeSelected, -1)` — the spared-mode loop already degenerates to the plain loop when `modeSelected2 < 0` — and add the two diag marks unconditionally in the shared body (they are gated on `gLaunchDiag` at the call). One teardown remains.
- **Benefit:** One teardown sequence to keep correct; ~80 lines removed.
- **Effort:** S · **Risk:** Low (the diff proves the bodies are equivalent modulo the spared mode) · **Confidence:** High (READ + diff census)

## RE-RIPTOPL-114 — udpfs editions still carry per-edition copies of headers that the shared core made unnecessary (filed as W06-013)

- **Category:** 2 (duplication) / 7 (repo clutter)
- **Locations:** `RiptOPL/modules/network/udpfs_bd/src/`, `udpfs_fhi/src/`, `udpfs_ioman/src/` — `udpfs_core.h`, `udpfs_packet.h`, `udprdma.h`, `udprdma_packet.h`, `udprdma_seq.h`; `include/mprintf.h` in six places (bd/fhi/ioman/ministack/smap trees, plus a differing copy in smap_udpbd)
- **Problem:** The `.c` side of the udpfs family was already consolidated: each edition's `udpfs_core.c`/`udprdma.c` is a 2-line wrapper including `udpfs_shared/src/…`, and the three variant Makefiles compile the shared sources with distinct `FEATURE_UDPFS_*` flags (all three editions are live — the root Makefile embeds all three IRXs). The headers were not consolidated: `md5sum` shows the five protocol headers above are byte-identical across all three editions (3 copies each), and `mprintf.h` is byte-identical in five of its six locations. `udpfs_bd/src/main.c` and `udpfs_ioman/src/main.c` are also md5-identical, and that shared main still carries the FHI init branch that is compiled out in the bd/ioman builds. Every protocol change must currently be made (or forgotten) in three places.
- **Fix:** Move the five identical headers next to the shared sources they describe and point the three editions' include paths at them, deleting the per-edition copies; do the same for `mprintf.h`. Keep the per-edition `main.c` (it is genuinely the same file — one copy, referenced by both builds).
- **Justification:** Adds nothing; it is the unfinished half of a consolidation the tree already started.
- **Benefit:** One definition of the wire structs (`udpfs_packet.h`, `udprdma_packet.h`) instead of three that can silently diverge — for a wire protocol, the highest-value copy to eliminate.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ + md5sum-verified)

## RE-RIPTOPL-115 — Two ministacks, three SMAP drivers, and small helpers copied between the udpfs and udpbd stacks (filed as W06-014)

- **Category:** 2 (duplication)
- **Locations:** SMAP driver: `RiptOPL/modules/network/udpfs_smap/src/smap.c`, `RiptOPL/modules/network/smap_udpbd/src/smap.c` (`diff` = 25 lines: udpbd/fhi init hooks and `g_dma_max_block_size` handling), `RiptOPL/modules/network/smap-ingame/smap.c` (651 lines, an older genuinely different variant — single-mode TX, no DNV handling). Ministack: `RiptOPL/modules/network/udpfs_ministack/src/` (split files, ~1,118 lines) vs `RiptOPL/modules/network/smap_udpbd/src/ministack.c` (275 lines, monolithic). Helpers: `parse_ip()` at `RiptOPL/modules/network/udpfs_ministack/src/main.c:18` and `RiptOPL/modules/network/smap_udpbd/src/main.c:19` (identical ~25 lines); `udptty.c` in `udpfs_ministack/src/` and `smap_udpbd/src/` (89 lines each, `diff` = 4 lines — one include).
- **Problem:** The UDPBD stack and the UDPFS stack each carry a full copy of the SMAP driver and of the mini IP/UDP stack. The copies have already needed cross-porting once: `smap_udpbd/src/ministack.c` contains a comment recording that its ARP free-slot bug was fixed by copying udpfs_ministack's logic. smap-ingame's driver is a real variant (different TX model for the lwIP stack) and is excluded from the merge suggestion; the udpbd↔udpfs_smap pair differs by only 25 lines in ~640.
- **Fix:** Merge the udpbd and udpfs_smap SMAP drivers into one source with the existing init-hook difference kept as the per-module glue (the udpfs editions already prove the one-source/multiple-IRX pattern), and likewise share `udptty.c` and `parse_ip()` instead of copying them. Leave smap-ingame's driver and the two ministack shapes alone unless a second cross-port is needed — the ministack merge is the weakest part of this finding.
- **Justification:** Adds nothing net; each shared file replaces two copies.
- **Benefit:** The next SMAP or ARP fix lands once instead of being hand-ported (or missed) across stacks.
- **Effort:** L · **Risk:** Med (two live stacks; needs hardware validation that this audit cannot do) · **Confidence:** High (READ + measured diffs)

## RE-RIPTOPL-116 — Two full ISO9660 directory walkers (plus two copies of the TOC entry struct) in one scope (filed as W07-013)

- **Category:** 2 duplication
- **Locations:** RiptOPL/modules/iopcore/cdvdman/searchfile.c:25-240 (`cdvdman_trimspaces` / `cdvdman_locatefile` / `cdvdman_findfile`); RiptOPL/modules/isofs/isofs.c:187-380 (the same three functions, independently evolved); struct definitions at RiptOPL/modules/iopcore/cdvdman/internal.h (dirTocEntry) and RiptOPL/modules/isofs/isofs.c:15-32
- **Problem:** The same ISO9660 walk — trim, split path, scan TOC records, handle dual-layer — exists twice and has already diverged in exactly the places bugs live: cdvdman clamps names to a 15-byte buffer and compares 12 chars; isofs uses a 40-byte buffer, a memcmp match, and adds a `;1` fixup cdvdman does elsewhere. Each hardening fix in this pass's reading (record-length validation, nameless-record step) had to be present in both copies, written separately. READ (both files, in full).
- **Fix in words:** Move the walker into one shared source compiled by both modules — the precedent already exists in-tree: cdvdman's Makefile compiles `../../isofs/zso.o` and `../../isofs/lz4.o` directly. Keep each module's own read callback and result struct as the only per-module parts.
- **Benefit:** One ISO9660 parser to fix and audit; net lines removed across the two copies.
- **Effort:** L · **Risk:** Med (both copies are on the game boot path; the divergences above must be reconciled deliberately, not averaged) · **Confidence:** High that the duplication exists (READ); Med that a merge is a net win without regression — INFERRED, no hardware here

## RE-RIPTOPL-117 — mmce_fs remove/mkdir/rmdir are three copies of one function (filed as W10-025)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/modules/mmce/mmceman/src/mmce_fs.c:409-466` (remove), `:468-525` (mkdir), `:527-584` (rmdir)
- **Problem:** Three ~58-line functions with identical packet sequences, differing only in the command byte (`MMCE_CMD_FS_REMOVE`/`MKDIR`/`RMDIR`) and log strings. open/dopen repeat the same skeleton a fourth and fifth time.
- **Fix:** One static helper taking the command byte, called by the three ops (and, if it stays a net win, open/dopen).
- **Benefit:** ~110 lines removed; the ENOENT/timeout doctrines documented elsewhere in this file only need maintaining once.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-118 — Three message/hint wrapping boxes re-implement the same wrap-and-draw (filed as W02-007)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/ui/dia.cpp:75-110` (`diaDrawHint`), `RiptOPL/src/ui/guinotify.cpp:47-84` (`Notifications::render`), `RiptOPL/src/ui/guimodal.cpp:90-122` (`guiDrawWrappedLines`/`guiDrawMsgText`)
- **Problem:** READ all three. Each independently: measures text with `fntCalcDimensions`, clamps a box to screen width minus margins, calls `fntWrapText` into a fixed stack buffer (384 / 512 / 512 bytes), computes box height from line count × `MENU_ITEM_HEIGHT`, and draws rect + string. The notification version's own comment says it wraps "the way the settings hint box does (diaDrawHint)" — i.e. the third copy was written knowing the first existed.
- **Fix in words:** Keep the modal one (it is the most general: returns last Y, centres lines) and make the hint/notification paths call it or its wrapping half; delete the other two box-computations.
- **Benefit:** Wrap/clip fixes (a recurring bug class in this codebase's comments) land once instead of three times; ~60 lines removed.
- **Effort:** M · **Risk:** Med · **Confidence:** High (READ)

## RE-RIPTOPL-119 — Three carousel presenters duplicate the framed-cover (overlay) draw block (filed as W02-017)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/gfx/coverflow.cpp` (draw loop overlay branch), `RiptOPL/src/gfx/psbbnflow.cpp` (`drawCover` overlay branch), `RiptOPL/src/gfx/grid.cpp` (`drawCell` overlay branch)
- **Problem:** READ all three. Each presenter independently: resolves the per-item element via `thmGetElemForItem`, fetches the texture with default fallback, then — if `overlayTexture` exists — scales the four inset corners by `drawW/coverElem->width` etc. and calls `rmDrawOverlayPixmap`, plus the optional `overlayTexture2` pixmap. The inset-scaling arithmetic is copy-identical in all three (and a fourth variant exists in themeelems' `drawGameImage`).
- **Fix in words:** One shared draw helper for "cover texture inside its case overlay at (x, y, w, h)" used by the three presenters; each presenter keeps only its layout/animation maths, which is the part that actually differs.
- **Justification (addition):** Shared helper permitted by the main rule: the same logic exists in 3+ places and sharing deletes three copies of the inset arithmetic.
- **Benefit:** Overlay inset fixes (a historically fiddly, hardware-verified area) are made once; ~60 duplicated lines removed.
- **Effort:** M · **Risk:** Med · **Confidence:** High (READ)

## RE-RIPTOPL-120 — Streaming ring-buffer copy logic duplicated almost line-for-line (EE-DMA and IOP-memcpy variants) (filed as W07-014)

- **Category:** 2 duplication
- **Locations:** RiptOPL/modules/iopcore/cdvdman/streaming.c:162-245 (`ReadSectorsEE`), :247-295 (`ReadSectors`)
- **Problem:** Both functions implement the identical two-segment ring walk (wrap segment when `WritePtr <= ReadPtr` and `StStreamed > 0`, then the linear segment), with the same clamping and pointer wrap. They differ only in how the bytes move (SIF DMA descriptors vs `memcpy`) and in when `StReadPtr`/`StStreamed` are committed. A fix to the ring arithmetic must be made twice, in code where an off-by-one corrupts game streams. READ.
- **Fix in words:** Extract the segment computation (up to two ptr/length pairs) into one static helper both functions consume; keep the DMA commit-after-transfer behaviour of the EE variant exactly as is.
- **Benefit:** Ring arithmetic exists once; the two transfer mechanisms stay separate and unchanged.
- **Effort:** M · **Risk:** Med (interrupt-context locking differs subtly between the two) · **Confidence:** High (READ)

## RE-RIPTOPL-121 — freeze_diag_screen.c re-implements freeze_diag_format.c's formatting helpers (filed as W04-008)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/ee_core/src/freeze_diag_screen.c:42-94` (`ln_str`, `ln_dec`, `ln_hex`, `ln_where`); `RiptOPL/ee_core/src/freeze_diag_format.c:22-49` (`put_str`, `put_dec`, `put_hex`) and `RiptOPL/ee_core/src/freeze_diag_format.c:304-321` (`put_where`)
- **Problem:** The two INGAMEDIAG-only files each carry a private copy of the same four operations: append a string, append decimal, append hex, and append a "module+offset" location — including the same module-table lookup logic in `ln_where`/`put_where`. The only difference is the sink type (fixed line buffer vs streaming buffer).
- **Fix:** Keep one implementation of the four helpers, parameterised by the sink the two files already share conceptually (or have screen.c call format.c's helpers through a tiny line-buffer adapter), and delete the other copy. If the host tests genuinely require both files to stay self-contained, say so in one comment and close this as accepted duplication — but today nothing in either file states that.
- **Benefit:** One copy of the number/location formatting to fix; the two diagnostic surfaces can't drift apart.
- **Effort:** M · **Risk:** Low (diagnostic-only code, host-tested) · **Confidence:** High — READ (both implementations read side by side; net-line win is INFERRED, roughly the size of one helper set).

## RE-RIPTOPL-122 — Five near-identical number-to-string converters in igs_api.c (filed as W04-009)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/ee_core/src/igs_api.c:79-152` (`u8todecstr` :79, `u16todecstr` :94, `u32todecstr` :109, `u32tohexstr` :124, `u64tohexstr` :139)
- **Problem:** Five static functions share one body shape — extract digits least-significant-first into a fixed-width, zero-padded, NUL-terminated field — differing only in input width (u8/u16/u32/u64) and radix (10/16). ~74 lines for what is one algorithm.
- **Fix:** Replace the five bodies with one static helper taking a u64 value, radix, and digit count; keep the five names only if call sites are numerous, otherwise call the helper directly and delete the wrappers.
- **Benefit:** One digit-conversion routine to verify; ~50 lines removed.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ.

## RE-RIPTOPL-123 — The ps2ips 48-byte-tail peek/truncate receive workaround exists in three copies (filed as W01-004)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/sources/ranet.cpp:1072-1081` (`Ranet::tcpRecv`), `RiptOPL/src/sources/ratls_ps2.cpp:156-165` (non-RA `ratlsTcpRecv`), `RiptOPL/src/sources/nettime.cpp:117-125` (inlined inside `netTimeAsk`)
- **Problem:** The identical non-obvious arithmetic — peek, then `if (got > 64 && got % 64 > 48) got -= got % 64 - 48;`, then take — is written out three times (census: the `got % 64` pattern occurs at exactly these three sites). The ratls copy even carries the comment "Ranet::tcpRecv (ranet.cpp, RA only)" admitting it is a copy, and it hard-codes the lwIP flag values (`0x08 | 0x01`) where ranet uses named constants. If the ps2ips side-buffer size (RESTS_EBUF_MAX, 48) ever changes, three places must change in lockstep and nothing enforces it.
- **Fix:** Delete the ratls fallback copy and the nettime inline copy; both call the one implementation (ranet's, exported the way `raNetTcpRecv` already exports it for the RA build of ratls). No new helper is needed — one already exists and is already shared in one build flavour.
- **Benefit:** One definition of a workaround whose correctness depends on a constant owned by another module; removes ~15 duplicated lines and the named-vs-magic flag divergence.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (all three bodies opened and compared).

## RE-RIPTOPL-124 — The ~66-line platform prologue is copied into four files (and the app splits); the disc-timing constants inside it are dead in sysvmc.cpp (filed as W03-005)

- **Category:** 2 (duplication) / 1 (dead code)
- **Locations:** `RiptOPL/src/platform/system.cpp:1-66`, `RiptOPL/src/platform/sysvmc.cpp:1-66`, `RiptOPL/src/platform/sysdisc.cpp:1-66`, `RiptOPL/src/platform/sysneutrino.cpp:1-66`; `RiptOPL/src/platform/sysvmc.cpp:15-23` (dead constants); same pattern in `RiptOPL/src/app/autolaunch.cpp:7-72` and `RiptOPL/src/app/lwnbdctl.cpp:7-72`
- **Problem:** Script-verified: the first 66 lines of the four platform files differ by only 2 lines (sysdisc), 4 lines (sysneutrino) and 8 lines (sysvmc) from system.cpp's — the include block and shared constants are copy-pasted per file. Inside that block, `SYS_DISC_DETECT_MS` / `SYS_DISC_REDETECT_MS` / `SYS_DISC_NODISC_MS` are defined in both sysdisc.cpp (where they are used, lines 161/185/186) and sysvmc.cpp:15-23, where grep shows no use at all. The app-layer splits repeat the pattern: autolaunch.cpp and lwnbdctl.cpp carry opl.cpp's full include block, including headers the file never uses (in lwnbdctl.cpp, grep finds zero uses of the tar, art-index and tex-cache facilities its includes import) and even opl.cpp-specific comments (the `sys/stat.h` mkdir note in lwnbdctl.cpp describes BDM autolaunch, a different file's business).
- **Fix:** Delete the unused `SYS_DISC_*` constants from sysvmc.cpp. For the prologue itself, trim each file's include block to what that file references (the compiler already enforces this per file); do not create a new shared mega-header — that would move the problem, not shrink it.
- **Benefit:** Removes dead constants and per-file include drift; each file states its real dependencies.
- **Effort:** S (constants) / M (include trim) · **Risk:** Low · **Confidence:** High (READ + diff/grep census)

## RE-RIPTOPL-125 — The 100 µs/1 ms MSG_DONTWAIT poll with a 10 s silence budget is implemented four times (filed as W07-020)

- **Category:** 2 duplication
- **Locations:** RiptOPL/modules/iopcore/cdvdman/smb.c:270-360 (`smb_RecvTimed` + resync machinery); RiptOPL/modules/iopcore/cdvdman/smb2.c:287-325 (`RecvData`); RiptOPL/modules/iopcore/cdvdman/nfs.c:70-95 (`nfs_sock_read`); RiptOPL/modules/iopcore/cdvdman/device-http.c:20-35 (`RecvBounded`)
- **Problem:** Four sibling transports re-implement the same workaround for the same SMSTCPIP limitation (no socket timeouts, stub select): poll with `MSG_DONTWAIT`, sleep 100 µs for the first ~2 ms then 1 ms, give up after 10 s — three of them carrying the same "#399" comment explaining the same 1 ms lesson, learned and written down three times. The timeout accounting already differs (tick subtraction vs accumulated `waited_us`), so the copies are drifting. READ (all four).
- **Fix in words:** One small shared source file for the poll-receive loop, compiled into each cdvdman flavour the way `../../isofs/zso.o` already is; each transport keeps its own framing on top. Only worth doing when one of these files is next touched — not as a standalone refactor.
- **Benefit:** The IOP network receive policy (and its hard-won timing comment) exists once.
- **Effort:** M · **Risk:** Med (four boot-path flavours; each must keep its own error semantics) · **Confidence:** High that the duplication exists (READ); Med on net win — INFERRED, stated with the "next touch" condition above

---

## RE-RIPTOPL-126 — xatad duplicates atad's DMA-mode tables, and the copies have already diverged in reach (filed as W09-008)

**Category:** 2 duplication
**Locations:**
- RiptOPL/modules/hdd/xhdd/xatad.c:8-52 (`ata_multiword_dma_mode`, `ata_ultra_dma_mode` — same timing values as atad's)
- RiptOPL/modules/hdd/atad/src/ps2atad.c:1486-1529 (atad's `ata_multiword_dma_mode` / `ata_ultra_dma_mode`)
- RiptOPL/modules/hdd/atad/src/ps2atad.c:55-57, 996-1000 (atad's MDMA copy is compiled only under `ATA_MWDMA_MODES`, which the atad Makefile does not define — so atad's copy is dead while xhdd's MDMA branch is live)

**Problem:** Two modules that program the same SPEED-register DMA timing tables carry independent copies of the value tables. One copy is unreachable in its own module, so in practice only xhdd's tables are exercised — exactly the setup where a future timing fix lands in the wrong (dead) copy.
**Fix:** Delete atad's never-compiled MDMA function, and move the two small timing switches into the already-shared `modules/hdd/common/opl-hdd-ioctl.h` include path (or a sibling common header both modules already use) as static inlines, deleting xatad's duplicates.
**Justification (addition):** The shared inline replaces two copies with one — net code removed, no new abstraction beyond the header both modules already share.
**Benefit:** One source of truth for hardware timing values; ~44 lines removed.
**Effort:** M · **Risk:** Med (register-programming code; values must be copied verbatim) · **Confidence:** High (READ)

## RE-RIPTOPL-127 — crc32 and the integer typedef block are copied verbatim across the PC tools (filed as W11-018)

- Category: 2 duplication
- Locations: `RiptOPL/tools/pc/iso2opl/src/iso2opl.c:48-76` and `RiptOPL/tools/pc/opl2iso/src/opl2iso.c:43-66` (`crc32` + `crctab[0x400]` global); typedef blocks at `RiptOPL/tools/pc/genvmc/src/genvmc.h:26-33`, `RiptOPL/tools/pc/iso2opl/src/iso2opl.h:63-70`, `RiptOPL/tools/pc/opl2iso/src/opl2iso.h:118-125`
- Problem: iso2opl and opl2iso are inverse tools over the same `ul.cfg` format and carry byte-identical `crc32()` implementations (including the same global 1 KB table and the same table-regeneration-on-every-call behaviour) — the CRC is the game-name hash that decides the `ul.<CRC>.<ID>.<part>` filenames both tools must agree on, so a divergence would corrupt interoperability, yet nothing keeps the copies in sync. The three PC-tool headers likewise repeat the same 8-line `u8…s64` typedef block. The sharing precondition (same logic in 2+ places, sharing removes code) is met for crc32: one `tools/pc/common/crc32.c`+header linked by both Makefiles is smaller than two copies.
- Fix: Move `crc32`/`crctab` into one shared source under `tools/pc/common/`, link it from the iso2opl and opl2iso Makefiles, and delete both copies. Leave the header typedef blocks alone if the shared header is not also adopted there.
- Benefit: One CRC implementation for the format both tools must agree on; −~55 duplicated lines.
- Effort: M · Risk: Low · Confidence: High (READ)

---

## RE-RIPTOPL-128 — The "NIC is reserved by a UDP transport" predicate exists in three copies (filed as W01-005)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/sources/ranet.cpp:217-231` (`Ranet::nicBusy` + `Ranet::nicReserved`), `RiptOPL/src/sources/ratls_ps2.cpp:143-153` (`ratlsNicReserved`, non-RA branch), `RiptOPL/src/sources/nettime.cpp:138-140` (inlined at the top of `netTimeFetch`)
- **Problem:** The same policy — UDPBD/UDPFS module resident, or a UDP protocol selected in `gNetworkProtocol` — is spelled out three times, in two different shapes (ranet splits it into `nicBusy`/`nicReserved`; ratls and nettime inline the union). The three callers (RA networking, HTTPS, network time) must agree on this predicate or one of them will try to raise SMB on a NIC a UDP transport owns; agreement is currently maintained by hand-copying.
- **Fix:** Delete the ratls and nettime copies; both call the ranet predicate (it is already the canonical one — ratls's own comment calls its copy "Ranet::nicReserved (ranet.cpp, RA only)").
- **Benefit:** One policy definition for NIC ownership; removes two hand-maintained copies of a five-term condition.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ.

## RE-RIPTOPL-129 — The read-only-device CFG-folder helper is duplicated in BDM and ETH — and the copies already disagree (filed as W01-014)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/sources/bdmsupport.cpp:2045-2070` (inside `BdmSource::config`), `RiptOPL/src/sources/ethsupport.cpp:1520-1545` (`ethReadOnlyCfgDir`)
- **Problem:** Both build `"<config home>CFG/"`, keep a `static char madeCfgDir[256]` "already created" memo, strip the trailing slash, and `mkdir` once. They have already diverged: the ETH copy records success on `mkdir == 0 || errno == EEXIST` (ethsupport.cpp:1535), the BDM copy only on `mkdir == 0` (bdmsupport.cpp:~2063) — so on one backend a pre-existing folder created by something else is remembered, on the other it is retried on every selection. The ETH copy's comment even cites the BDM one as its model.
- **Fix:** One helper in supportbase.cpp next to `sbPopulateConfigIn` (which both already call with the resulting dir), with the EEXIST-accepting rule; delete both copies.
- **Justification (fix adds a shared helper):** Same logic in 2 places, already drifted; sharing removes code overall.
- **Benefit:** One mkdir/memo rule for read-only CFG homes; ~25 lines deleted.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ.

## RE-RIPTOPL-130 — ~37-line include block copy-pasted verbatim into split-out GUI files, most of it unused (filed as W02-004)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/ui/guiwait.cpp:10-47`, `RiptOPL/src/ui/guigameid.cpp:10-47`, `RiptOPL/src/ui/guimodal.cpp:9-47` (near-identical block also in `RiptOPL/src/ui/gui.cpp`)
- **Problem:** READ + command-verified: the include blocks of `guiwait.cpp` and `guigameid.cpp` differ only in their final self-header. `guigameid.cpp` (212 lines: a barcode drawer) includes `httpsupport.h` ("httpTestServer() — the Network page Test button"), `tar.h`, `cheatman.h`, `pggsm.h`, `libview.h`, `hddsupport.h`, `appsupport.h`, `ethsupport.h`, `netsupport.h`, `udpfssupport.h`, `mmcesupport.h` — a symbol search of the file body finds none of them used (only the words appear inside comments). `guiwait.cpp` carries the same block with the same unused entries. The comments on the includes even describe other files' use cases, proving the block was copied whole when `gui.cpp` was split.
- **Fix in words:** Trim each split file to the headers whose symbols it actually uses; delete the rest. No shared "kitchen-sink" header — that would preserve the coupling under a new name.
- **Benefit:** ~25 dead includes removed across the two worst files; faster, clearer builds; future splits stop inheriting phantom dependencies.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + grep-verified non-use)

## RE-RIPTOPL-131 — Two near-identical icon+text draw functions in uidraw.cpp (filed as W02-005)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/gfx/uidraw.cpp:13-57`
- **Problem:** READ. `guiDrawIconAndText` and `guiDrawIconAndLiteralText` are the same ~22-line body — icon width computation, pixmap draw, fallback `y += 10`, advance — differing only in whether the caption comes from `_l(textId)` or a literal pointer.
- **Fix in words:** Delete one: keep the literal-text version and make the ID version a one-line call that passes `_l(textId)` (the ID version's callers keep their signatures; the duplicated body goes).
- **Benefit:** One body to fix when icon metrics change; ~20 lines removed.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-132 — Drive enumeration duplicated between Drive Status and the File Manager (filed as W02-006)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/ui/drivestatus.cpp:77-165` (`dsScan`), `RiptOPL/src/ui/guifilemgr.cpp:174-248` (`fmScanDrives`)
- **Problem:** READ both. Both functions walk the same device set in the same order with the same calls: `MAX_BDM_DEVICES` slots via `fileXioDopen`/`bdmReadDriverName`/`dsDriverLabel`, two `mmce%d:/` probes, two memory cards via `mcGetInfo`+`mcSync`, the APA HDD via `hddModulesAreLoaded`/`hddGetSpace` formatted with `dsSizeText`. The File Manager version already reuses `dsDriverLabel`/`dsSizeText` but re-implements the scan itself, so any new device type or probe fix must be made twice.
- **Fix in words:** Have one scan produce a small list of (root, label) pairs and let each screen format/consume it its own way; delete the second enumeration loop.
- **Justification (addition):** The shared helper is justified under the main rule: the same logic exists in 2 places and sharing removes code overall (one of two ~70-line scans is deleted).
- **Benefit:** One place to add/fix a device probe; the two screens can no longer disagree about what drives exist.
- **Effort:** M · **Risk:** Med · **Confidence:** High (READ)

## RE-RIPTOPL-133 — FNV-1a implemented twice, in adjacent art files (filed as W02-008)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/gfx/artworker.cpp:13-24` (`focusHashValue`), `RiptOPL/src/gfx/artindex.cpp:54-71` (`ArtIndex::hashName`)
- **Problem:** READ both. Same algorithm, same constants (2166136261u / 16777619u); the artindex version adds case-folding and a length parameter, the artworker version is the plain NUL-terminated special case of it.
- **Fix in words:** Delete `focusHashValue` and call the artindex hash (or one tiny shared static in a header both already include) with the plain-string arguments.
- **Benefit:** Small, but it is exactly the kind of twin that drifts (one gets a fix, the other does not).
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-134 — Two different parsers for the same SYSTEM.CNF / BOOT2 job (filed as W03-002)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/src/platform/ps2cnf.cpp:16-122` (`CNFGetToken`/`CNFGetKey`/`ps2cnfGetBootFile`), `RiptOPL/src/platform/sysdisc.cpp:75` (`sysParseBoot2`)
- **Problem:** The tree parses SYSTEM.CNF twice with different rules. ps2cnf.cpp is token-based and case-sensitive; `sysParseBoot2` in sysdisc.cpp is a separate line scanner that matches "BOOT2" case-insensitively, including mid-token. The same disc can therefore yield a different boot path depending on which subsystem asks, and parser fixes (see W03-001) land in only one of them.
- **Fix:** Keep one parser — the ps2cnf one, once W03-001 is applied — and have sysdisc call it; delete `sysParseBoot2`. If the case-insensitive behaviour is load-bearing for real discs, fold that one rule into the surviving parser rather than keeping two implementations.
- **Benefit:** One parser to fix, one strictness level for a security-relevant input.
- **Effort:** M · **Risk:** Med (behavioural difference between the two is real; needs a disc-image test) · **Confidence:** High (READ)

## RE-RIPTOPL-135 — Four tar finder functions duplicate one search (filed as W03-004)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/src/platform/tar.cpp:464` (`tarFind`), `RiptOPL/src/platform/tar.cpp:501` (`tarFindPrefix`), `RiptOPL/src/platform/tar.cpp:531` (`tarFindExact`), `RiptOPL/src/platform/tar.cpp:555` (`tarFindPrefixExact`)
- **Problem:** Four exported finders implement the same index sweep, differing only in match predicate (exact vs prefix) and in whether a not-yet-loaded archive is loaded first. The sweep body is repeated four times, so any fix to the walk (bounds, kind filtering) must be made four times.
- **Fix:** One internal sweep taking a match-mode flag; the four public names become one-line forwards or are removed in favour of the single entry point. Net code removed.
- **Benefit:** One walk to audit; the W03-003 validation lands in exactly one place.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-136 — `readLeftJoy` writes the same deadzone switch twice (filed as W03-025)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/src/core/pad.cpp:311-370`
- **Problem:** The X and Y sensitivity switches inside `readLeftJoy` are the same five-case mapping (128/100/80/60/default 100) written out twice, once per axis. They must agree forever — a sensitivity tier added to one axis and not the other is an invisible bug.
- **Fix:** One tiny lookup (a 5-entry table or a 10-line local function) called for both axes. Qualifies under the house rule: identical logic in 2 places, sharing removes code.
- **Benefit:** ~20 lines removed; the two axes cannot drift.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-137 — `sysCheckVMCEx` validates the VMC superblock geometry twice, with different rules (filed as W03-028)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/src/platform/sysvmc.cpp:138-282`
- **Problem:** The function checks the superblock's geometry in two separate passes that do not state the same rule — one accepts `page_size == 512`, the other merely `!= 0` — so which validation a bad image fails depends on which pass reaches it, and a fix to one pass silently leaves the other accepting what it rejected.
- **Fix:** Keep one geometry validation (the stricter, `== 512` rule, since that is what the rest of the VMC code assumes) at the top of the function and delete the second pass.
- **Benefit:** One rule for "is this a usable VMC image", stated once.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-138 — lwnbdctl writes the same bounded pad-start retry block twice (filed as W03-033)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/src/app/lwnbdctl.cpp:196-203` (`loadLwnbdSvr`), `RiptOPL/src/app/lwnbdctl.cpp:233-240` (`unloadLwnbdSvr`)
- **Problem:** Both the NBD load and unload paths contain the same ~8-line block: retry `startPads()` up to 100 times with `delay(50)`, then log if it never started — including near-identical explanatory comments. It is the same policy stated twice in one file, so a change to the bound or the logging must be made twice.
- **Fix:** One file-local helper (`startPadsBounded(const char *where)`) called from both sites. Qualifies under the house rule: identical logic, two places, sharing removes code.
- **Benefit:** ~10 lines removed; the retry policy has one home.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-139 — GSM DTV mode constants defined in three places, in three styles (filed as W04-004)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/include/renderman.h:18,21`; `RiptOPL/ee_core/include/gsm_defines.h:34-35`; `RiptOPL/ee_core/src/gsm_api.c:28-34`
- **Problem:** `GS_MODE_DTV_576P` (0x53) and `GS_MODE_DTV_1080P` (0x54) are defined independently for the GUI (`renderman.h`), for the ee_core assembler (`gsm_defines.h`), and again in C inside `gsm_api.c`, whose own comment admits the copies must be "kept in sync" by hand. The styles also disagree: `renderman.h` uses a `#define` for 576P but `constexpr int` for 1080P; `gsm_defines.h:34-35` writes the two assignments bare, without the `.equ` every other line in that file uses.
- **Fix:** Within ee_core, delete the `gsm_api.c` copies and take the values from one ee_core-side header; in `gsm_defines.h` write the two lines with `.equ` like the rest of the file; in `renderman.h` use one style for both. The GUI/ee_core pair itself is cross-binary and can stay, with the sync comment kept.
- **Benefit:** One fewer hand-synced copy of a hardware-mode constant; a future mode renumber can't silently split the C and asm sides of ee_core.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (all three sites grep-verified).

## RE-RIPTOPL-140 — COMPAT_MODE_* bits defined at four sites; one redefinition is live today (filed as W04-005)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/ee_core/include/ee_core.h:59-66` (full set, modes 1–8); `RiptOPL/ee_core/include/patches.h:14` (`COMPAT_MODE_4` again); `RiptOPL/ee_core/src/asm.S:16-17` (modes 3 and 6 again); `RiptOPL/include/iosupport.h:97-104` (GUI-side full set)
- **Problem:** The same bit values are spelled out four times. The `patches.h:14` copy is not theoretical: `ee_core/src/syshook.c` includes both `ee_core.h` and `patches.h` (verified by grep), so that TU already gets a macro redefinition (benign only because the values happen to match, 0x08). `asm.S` re-derives modes 3 and 6 locally instead of taking them from the header it otherwise mirrors.
- **Fix:** Delete `COMPAT_MODE_4` from `patches.h` (its only includer-with-ee_core.h already has it) and delete the two local defines in `asm.S`, including `ee_core.h`'s values there instead. The `iosupport.h` GUI copy is cross-binary and can stay.
- **Benefit:** One definition per binary for the compat-bit meanings; removes a live redefinition warning and a drift trap.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ.

## RE-RIPTOPL-141 — GSMSourceGSRegs layout hand-mirrored in three representations (filed as W04-006)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/ee_core/include/igs_api.h:126-142` (`struct GSMSourceGSRegs`); `RiptOPL/ee_core/src/ra_overlay.c:46-59` (`struct ra_gs_source_regs`, same 11 u64 fields); `RiptOPL/ee_core/src/igrmenu.c:549-557` (raw `extern u64 GSMSourceGSRegs[11]` plus `SRC_PMODE`/`SRC_SMODE2`/`SRC_DISPFB1`/`SRC_DISPLAY1`/`SRC_DISPFB2`/`SRC_DISPLAY2` index macros); storage at `RiptOPL/ee_core/src/gsm_engine.S:57` (`.space 88`)
- **Problem:** One 88-byte block of recorded GS registers has three separate layout descriptions in the same binary. `ra_overlay.c`'s own comment says "Keep this layout in sync with struct GSMSourceGSRegs in igs_api.h / gsm_engine.S" — i.e. sync is manual. If a field is ever inserted or reordered in the engine, the struct, the duplicate struct, and the index macros must all move together or the overlays read the wrong registers.
- **Fix:** Have `ra_overlay.c` and `igrmenu.c` use the single `struct GSMSourceGSRegs` from `igs_api.h` (both are ee_core TUs; the header is in ee_core's own include dir) and delete `ra_gs_source_regs` and the `SRC_*` index macros.
- **Benefit:** One layout definition for a hardware-register block; the sync comment and two copies go away.
- **Effort:** M · **Risk:** Med (layout-critical data shared with assembly; verify field order matches before/after) · **Confidence:** High — READ (all three representations compared field-by-field; the "single header is includable" step is INFERRED from the shared include dir).

## RE-RIPTOPL-142 — Two macro families for the same GS registers in igs_api.h, plus a third copy in igrmenu.c (filed as W04-007)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/ee_core/include/igs_api.h:35-38` (`GS_BITBLTBUF 0x50`, `GS_TRXPOS 0x51`, `GS_TRXREG 0x52`, `GS_TRXDIR 0x53`); `RiptOPL/ee_core/include/igs_api.h:90-94` (`GS_GSBITBLTBUF 0x50`, `GS_GSTRXPOS 0x51`, `GS_GSTRXREG 0x52`, `GS_GSTRXDIR 0x53`, `GS_GSFINISH 0x61` duplicating `GS_FINISH 0x61` at line 39); `RiptOPL/ee_core/src/igrmenu.c:687-690` (the `GS_*` four defined yet again, locally)
- **Problem:** The same five register addresses carry two names in one header, and a third private copy in igrmenu.c. Both families are in use (`GS_GS*` in `igs_api.c:222-224`, `GS_*` in `igrmenu.c:731-799`), so neither is dead — they are pure aliases with no semantic difference.
- **Fix:** Keep one family (the `GS_*` names, which igrmenu.c already uses), point `igs_api.c` at it, and delete the `GS_GS*` aliases and igrmenu.c's local re-definitions (it can include the header).
- **Benefit:** One name per GS register address; three definition sites become one.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (definitions and call sites grep-verified).

## RE-RIPTOPL-143 — opl.h includes <debug.h> twice and mixes include-path styles (filed as W04-014)

- **Category:** 2 (duplication, minor)
- **Locations:** `RiptOPL/include/opl.h:33` and `RiptOPL/include/opl.h:43` (both `#include <debug.h>`); `RiptOPL/include/opl.h:45` (`#include "config.h"`) vs `RiptOPL/include/opl.h:48` (`#include "include/hddsupport.h"`)
- **Problem:** The same SDK header is included twice, 10 lines apart, in the project's most-included header; and sibling project headers are pulled in with two different path conventions (`"config.h"` bare vs `"include/…"` prefixed) in the same include block, which only both work because of how the include path happens to be set up.
- **Fix:** Delete the second `<debug.h>` include and spell the project includes one way.
- **Benefit:** One less redundant include in a header everything pulls in; one include convention to reason about.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (line numbers grep-verified).

## RE-RIPTOPL-144 — smb2man and nfsman duplicate the same front-end plumbing (filed as W06-015)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/modules/network/smb2man/smb2man.c`, `RiptOPL/modules/network/nfsman/nfsman.c`
- **Problem:** The two network-filesystem front ends independently implement: a civil-date↔filetime conversion (`smb2man_pack_time` / `nfsman_pack_time` — same algorithm, one seeded from FILETIME and one from Unix seconds), bounded socket wait/read/write wrappers with the same poll-loop shape, and identical `malloc`/`free`/`calloc` shims over `AllocSysMemory`. READ in both files. They are separate IRXs loaded as alternatives, so this is source-level duplication only, and the value is lower than W06-012/013.
- **Fix:** Optional and lowest priority in this set: if either file is next touched for another reason, extract the date conversion and the socket wrappers into a shared source both compile (udpfs pattern). Otherwise leave alone — flagging for the record, not urging action.
- **Benefit:** One place for the date math both protocols depend on.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-145 — Two copies of the 8-hex-digit argument parser, and they disagree on lowercase (filed as W07-015)

- **Category:** 2 duplication / 6 bugs
- **Locations:** RiptOPL/modules/iopcore/freezediag/freezediag.c:24-42 (`parse_hex8`, accepts `a-f`); RiptOPL/modules/iopcore/igrmbox/igrmbox.c:151-168 (`mbHex8`, accepts only `A-F`)
- **Problem:** Both modules parse an ee_core-supplied IOP/EE address from `argv[1]` with a hand-copied hex loop. The igrmbox copy dropped the lowercase branch: the same address string that freezediag accepts makes igrmbox return 0, and `_start` then refuses to load (`MODULE_NO_RESIDENT_END`) — the in-game menu silently absent. READ (both).
- **Fix in words:** Make the two parsers identical (restore the lowercase range in igrmbox); they live in different IRXs, so a shared file is optional — identical behaviour is the fix.
- **Benefit:** One less silent load failure; the divergence class disappears.
- **Effort:** S · **Risk:** Low · **Confidence:** High that the code diverges (READ); Med that a lowercase address actually reaches it in practice — INFERRED, ee_core's formatter is outside this scope

## RE-RIPTOPL-146 — bd_defrag read and write are the same function written twice (filed as W08-013)

- Category: 2 duplication
- Locations: `RiptOPL/modules/bdm/libbdm/src/bd_defrag.c:1-101` (both functions, whole file)
- Problem: READ. `bd_defrag_read` and `bd_defrag_write` are line-for-line identical except for the `bd->read`/`bd->write` call and variable names — fragment search, clipping, advance, error paths all duplicated (~45 lines × 2). Any fix to the fragment walk (e.g. the bounds behaviour) has to be made twice, and the header already admits the duplication pressure with a `bd_defrag` backwards-compat alias.
- Fix: One static worker taking a write flag (or a function pointer), with the two public names as one-line wrappers. Net ≈ −40 lines, no new abstraction beyond the merge itself.
- Benefit: Single fragment-walk implementation to audit and fix.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-147 — bdmevent re-derives "unsupported sector size" by hand instead of using BDM's refused state (filed as W08-018)

- Category: 2 duplication / 3 overengineering
- Locations: `RiptOPL/modules/bdmevent/main.c:443-483`; state already owned at `RiptOPL/modules/bdm/src/bdm.c:11-17,154-180`
- Problem: READ. BDM already tracks exactly which devices it refused and why (`refused` flag, set in `bdm_try_mount` with the 0-vs-non-512 distinction spelled out). bdmevent ignores that and re-implements the classification in its snapshot: scan all 20 slots, re-test `parNr == 0 && sectorSize != 0 && sectorSize != 512`, and identify USB roots by spelling `device->name` out character-by-character as `'u','s','b','\0'`. Two implementations of one policy, in two modules, kept in sync only by comments that quote each other's reasoning at length. When the refusal policy changes in BDM, the EE-facing diagnostic (error 500) will silently disagree.
- Fix: Export the refused/unsupported count (or a per-device refused query) from BDM — it already has the flag and already exports `bdm_get_bd` to this module — and delete bdmevent's re-derivation, keeping only the USB-root counting that is genuinely bdmevent's own concern.
- Benefit: One owner for the refusal policy; the diagnostic cannot drift from the behaviour it explains.
- Effort: M · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-148 — mcemu carries an orphan, near-duplicate `ioman_add.h` that nothing in mcemu includes (filed as W09-016)

**Category:** 2 duplication / 1 dead code
**Locations:**
- RiptOPL/modules/mcemu/ioman_add.h:1-48
- RiptOPL/modules/iopcore/cdvdman/ioman_add.h (the other copy — a diff shows the two differ only by an added `#include <iox_stat.h>` and one `lseek` signature)

**Problem:** A grep for `ioman_add` across the tree finds exactly one includer — cdvdman's `internal.h`, which resolves to cdvdman's own copy. The mcemu copy is included by nothing, in a module that uses plain ioman (not iomanX extension ops), and it has already drifted from its sibling, so anyone who does include it gets a subtly different ops table.
**Fix:** Delete `modules/mcemu/ioman_add.h`.
**Benefit:** One less stale ABI header in the tree; no build impact.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ — include search and diff both run)

## RE-RIPTOPL-149 — genvmc: the same `mc_writecluster` error translation is hand-copied at eight sites (filed as W09-017)

**Category:** 2 duplication
**Locations:**
- RiptOPL/modules/vmc/genvmc/genvmc.c:267, 283 (`vmc_mccopy`), :378, 439, 488, 538, 554 (`vmc_mcformat`), :632 (`vmc_rebuild`) — each of the form `if (r == -2) r = -1000; else r = -<site-specific code>;` after a `mc_writecluster` call (grep-verified, 8 sites)

**Problem:** The abort-vs-error mapping for the one cluster-write primitive is re-expressed at every call site, with the site-specific error code as the only variation. Any change to the abort convention (or a ninth site added without it) silently diverges.
**Fix:** One tiny static helper in the same file that takes the raw result and the site's error code and returns the translated code; call sites become one line each. This is the "same logic in 2+ places, sharing removes code" case — eight copies collapse to one.
**Justification (addition):** The helper is smaller than the eight blocks it replaces; net lines decrease.
**Benefit:** Single place for the abort convention; each format/copy/rebuild site shrinks.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-150 — Two hand-rolled malloc/free shims, one per ilink module (filed as W10-018)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/modules/ilink/IEEE1394_bd/src/sbp2_driver.c:809-828`; `RiptOPL/modules/ilink/iLinkman/src/iLink_internal.c:184-203`
- **Problem:** Both modules independently define `malloc`/`free` wrappers around `AllocSysMemory`/`FreeSysMemory` with interrupt suspend/resume — the same ~20 lines twice, free to drift.
- **Fix:** Keep one copy in a shared source file compiled into both IRXs (the build already compiles per-module source lists); delete the other.
- **Benefit:** ~20 lines removed and one less place for allocator behaviour to diverge.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-151 — `scsi_read` and `scsi_write` are copy-paste twins (filed as W10-019)

- **Category:** 2 (duplication)
- **Locations:** `RiptOPL/modules/ilink/IEEE1394_bd/src/scsi.c:304-358`
- **Problem:** The two functions are line-for-line identical except for the write flag passed to `scsi_cmd_rw_sector` — including a duplicated 16-iteration retry loop.
- **Fix:** One static helper taking the flag; both public functions become one-line wrappers (or callers pass the flag directly).
- **Benefit:** ~27 lines removed; retry behaviour can only drift in one place now.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-152 — Two language packers exist: tools/lng_pack.sh is superseded by package_edition.language_pack but still shipped and documented (filed as W11-006)

- Category: 2 duplication / 8 redundant feature
- Locations: `RiptOPL/tools/lng_pack.sh:1-98`; live replacement at `RiptOPL/tools/package_edition.py:86-105`
- Problem: `package_edition.py`'s `language_pack()` already builds `RIPTOPL-LANGS.zip` for every release, including the same Serbian-font-from-Croatian special case (`lng_pack.sh:54-58` vs `package_edition.py:98-103`) and an equivalent README. `lng_pack.sh` is invoked by no workflow, Makefile target or Python tool (grep-verified), yet it is 98 lines of legacy shell that derives its version from `make oplversion`, greps `VERSION =` out of the Makefile and parses `DETAILED_CHANGELOG` — machinery from the pre-ps2build build. Two implementations of one pack means a fix (like the historical "last language silently dropped" fix in `lng_pack.sh:37-40`) lands in only one of them.
- Fix: Delete `tools/lng_pack.sh` and update the two mentions in `docs/LANGUAGES.md` to name `package_edition.py`'s language pack.
- Benefit: −98 lines; one language-pack mechanism, the one releases actually use.
- Effort: S · Risk: Low · Confidence: High (READ + grep-verified)

## RE-RIPTOPL-153 — The "last 10 commits" release-notes block is implemented three times, with three different escaping rules (filed as W11-011)

- Category: 2 duplication
- Locations: `RiptOPL/tools/publish_editions.py:325-333` (`release_notes`), `RiptOPL/tools/publish_lite.py:155-170` (`release_notes`), `RiptOPL/tools/publish_llvm.py:184-208` (`release_notes`)
- Problem: All three publishers generate the same changelog list from `git log --no-merges -10` and markdown-escape commit titles — three times, divergently: `publish_editions.py:331` escapes via `re.sub(r"([\\`*_[\]<>])", …)` on `%H`; `publish_lite.py:161` escapes via a per-character join over `"\\`*_[]<>"`; `publish_llvm.py:184-187` uses `%h` (short hash, then links it as if full) and an escape set built as `"\\*_[]<>" + chr(96)`. A title-escaping fix or format change must currently be made three times and has already drifted. The sharing precondition is met: `publish_llvm.py` already imports `publish_editions` as `common`, and `publish_editions.py` imports `publish_lite`, so one helper placed in `publish_lite` is importable by both others with no new module.
- Fix: One `changelog(sha, git, base)` helper in `publish_lite.py` returning the formatted lines; call it from all three `release_notes`.
- Benefit: One escaping rule for every channel's notes; ~15 duplicated lines removed.
- Effort: M · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-154 — build_edition.py redefines the add() helper it could import from build_configuration.py (filed as W11-012)

- Category: 2 duplication
- Locations: `RiptOPL/tools/build_edition.py:68-69`; original at `RiptOPL/tools/build_configuration.py:14-15`; existing import at `RiptOPL/tools/build_edition.py:13`
- Problem: `build_edition.py` already imports `CONFIGURATIONS, apply_configuration` from `build_configuration`, then defines a second, byte-identical `add(target, field, *values)` two dozen lines later. Two copies of the dedup-append rule for build definitions can drift (e.g. one learning to handle a new field type).
- Fix: Import `add` from `build_configuration` and delete the local copy.
- Benefit: −2 lines and one definition of the rule.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-155 — The ps2build-mirror fallback is implemented twice: once in shell, once in Python, over the same pin contract (filed as W11-015)

- Category: 2 duplication / 3 overengineering
- Locations: `RiptOPL/tools/ps2build_sdk.sh:123-149,347-377` (`mirror_fetch`, `pin_value`, `from_mirror`); `RiptOPL/tools/install_ports.py:89-160` (`mirror_file`, `mirror_pin`, `mirror_archive`)
- Problem: Two independent implementations fetch mirror releases, parse `pin.env`, and verify archives against `SHA256SUMS` whose own hash is pinned — the shell one for the SDK itself, the Python one for the ogg/vorbis packages, driven by the same environment variables (`PS2BUILD_SDK_MIRROR`, `PS2BUILD_SDK_TAG`, `PS2BUILD_SDK_SUMS_SHA256`, `PS2BUILD_SDK_MIRROR_TOKEN`, …). They already differ in detail (the Python side has the HTTPS-downgrade redirect guard and asset-id API path; the shell side has its own token handling in `api_fetch`), so a mirror-protocol fix or a security fix to token handling must be made — and reviewed — twice.
- Fix: Keep one mirror client. The Python implementation is the more complete one; have `ps2build_sdk.sh`'s mirror path call `install_ports.py`'s helpers (or a small shared module both use) instead of its own curl/parse code.
- Benefit: One mirror implementation to fix and audit; the token-handling rules exist once.
- Effort: L · Risk: Med (SDK bootstrap is on every CI path; the shell script currently has no Python dependency at its earliest stage) · Confidence: Med (both READ; net win INFERRED)

## RE-RIPTOPL-156 — `struct hostent` is defined twice because `<netdb.h>` only declares it (filed as W01-006)

- **Category:** 2 duplication
- **Locations:** `RiptOPL/src/sources/ratls_ps2.cpp:39-48`, `RiptOPL/src/sources/nettime.cpp:30-38`
- **Problem:** Both files define lwIP's `hostent` layout locally, with near-identical explanatory comments ("<netdb.h> only declares struct hostent…", "…as in ratls_ps2.cpp"). Two definitions of an ABI layout for the same library struct can silently diverge; the second author already had to know the first file existed.
- **Fix:** Define it once in a header both files already include (or one small existing network header) and delete both local definitions.
- **Justification (fix touches a header):** Removes two definitions and adds one — net fewer definitions of an ABI-critical struct.
- **Benefit:** One ABI definition; ~10 lines deleted.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ.

## RE-RIPTOPL-157 — atad PIO transfer returns success without transferring, and its body is unreachable dead code (filed as W07-001)

- **Category:** 1 dead code / 6 bugs
- **Locations:** RiptOPL/modules/iopcore/cdvdman/atad.c:470-522 (stub + unreachable body), called from RiptOPL/modules/iopcore/cdvdman/atad.c:615
- **Problem:** `ata_pio_transfer()` writes one PIO-data register byte and `return 0;` — every line after that (the real 256-word PIO in/out loops, DRQ and error checks) is unreachable. Its caller in `sceAtaWaitResult()` treats 0 as a completed transfer, so any PIO-type command (e.g. IDENTIFY, type 2 in the command table) reports success with the caller's buffer untouched. READ.
- **Fix in words:** Delete the unreachable body and keep the stub, with its comment stating PIO data transfer is unsupported and returning an error code instead of 0; or, if a PIO command is ever actually needed, restore the body. Do not keep both.
- **Benefit:** Removes ~48 misleading lines; a silent fake-success becomes an explicit error.
- **Effort:** S · **Risk:** Low (the body never executes today; only the return value changes) · **Confidence:** High (READ)

## RE-RIPTOPL-158 — The whole APA partition-editing subtree is retained dead code (~700 lines) (filed as W09-001)

**Category:** 1 dead code / 8 half-finished (refused) features
**Locations:**
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:340-461 (`hddFormat` body after its unconditional `return -EACCES` at :347)
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:519-563 (`apaRemove`, called only from the dead tail of `hddRemove` at :630-636)
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:565-613 (`apaRename`, called only from the dead tail of `hddReName` at :852-858)
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:860-919 (`ioctl2AddSub`, declared `__attribute__((unused))` at :98)
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:921-947 (`ioctl2DeleteLastSub`, `unused` at :99)
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:1028-1066 (`devctlSwapTemp`, `unused` at :100)
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:1068-1091 (`devctlSetOsdMBR`, `unused` at :101)
- RiptOPL/modules/hdd/apa/src/apa.c:105-134 (`apaFillHeader`), :136-168 (`apaInsertPartition`), :213-231 (`apaRemovePartition`), :233-253 (`apaMakeEmpty`), :255-305 (`apaDeleteFixPrev`), :308-361 (`apaDeleteFixNext`), :364-410 (`apaDelete`) — reachable only from the functions above
- RiptOPL/modules/hdd/apa/src/hdd.c:103-107 (`hddCheckPartitionMax`, only caller is dead `ioctl2AddSub`), :109-179 (`hddAddPartitionHere`, only callers are the dead format body and dead `ioctl2AddSub`)
- Knock-on: RiptOPL/modules/hdd/apa/src/cache.c:105-127 (`apaCacheFlushDirty`/`apaCacheFlushAllDirty`) and RiptOPL/modules/hdd/apa/src/journal.c:47-56 (`apaJournalWrite`) — a scripted grep of every `APA_CACHE_FLAG_DIRTY` setter shows all of them are inside the functions above, so in this build nothing ever marks a header dirty and the flush/journal-write path never runs (journal *restore* at startup is live and stays).

**Problem:** The fork deliberately refuses format / create / delete / rename / add-sub / delete-sub / swap-temp / set-osd-mbr with `-EACCES` (documented in `modules/hdd/apa/ORIGIN.txt` item 4), but keeps every refused body "for diffability". That is ~372 lines in hdd_fio.c (counted by script over the ranges above) plus ~255 lines in apa.c plus ~76 lines in hdd.c of compiled, reviewable, misleading code — including one latent defect that only exists because the dead code is kept: `apaRename` calls `SignalSema(fioSema)` on three exit paths (hdd_fio.c:573, 581, 595) although it never waits on that semaphore; its only caller held the semaphore, so if the refusal in `hddReName` is ever lifted the semaphore count is corrupted.
**Fix:** Delete the refused bodies and the functions only they call; leave the one-line `-EACCES` returns in the ops/devctl/ioctl2 dispatchers. Keep `apaJournalRestore`/`apaJournalReset`. The SDK pin in ORIGIN.txt is the diff reference — the bodies do not need to live in the tree to be diffed.
**Benefit:** ~700 fewer lines in the highest-risk driver in the tree; the partition-table write surface becomes exactly what the policy says it is; removes the semaphore-imbalance trap.
**Effort:** M · **Risk:** Med (re-vendor workflow documented in ORIGIN.txt gets simpler, not harder, but this is a deliberate fork policy reversal) · **Confidence:** High (READ — call graph verified by grep over all callers)

## RE-RIPTOPL-159 — The FULL_LWIP subsystem (~627 gated lines) is dead and cannot link if enabled (filed as W05-003)

- **Category:** 1 dead code / 8 half-finished features
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/sockets.c:474-479,623-628,630-810,899-1185,1377-1415` (gates at 474, 623, 630, 899, 1377); `RiptOPL/modules/network/SMSTCPIP/ps2ip.c:97-152`; `RiptOPL/modules/network/SMSTCPIP/netif.c:110-161`; `RiptOPL/modules/network/SMSTCPIP/Makefile` (`FULL_LWIP?= 0`)
- **Problem:** Behind `#ifdef FULL_LWIP` (Makefile default 0) sit complete implementations of lwip_read, lwip_write, lwip_select, lwip_getpeername, lwip_getsockname, lwip_getsockopt, lwip_ioctl, ps2ip_getconfig/setconfig and netif_remove/netif_find. They are unreachable in practice for two independent reasons: (1) exports.tab stubs their slots unconditionally, even in a FULL_LWIP=1 build (W05-002); (2) the FULL_LWIP setconfig path calls dhcp_start/dhcp_stop, but there is no dhcp.c anywhere under `modules/network/` (`find` returned 0) — enabling the flag cannot link (INFERRED link failure; file absence is READ). Supporting detail: lwip_select's timeout path is also broken inside the gate — it compares the result of `sys_sem_wait_timeout` (a macro returning 0/1, `include/lwip/sys.h:81`) against `SYS_ARCH_TIMEOUT` (−418), so the timeout branch can never run. A comment block at `include/lwipopts.h:313-364` still claims getsockopt was exported; it was not.
- **Fix:** Delete the FULL_LWIP-gated blocks, the Makefile option, and the stale lwipopts.h comment. If the feature is ever wanted, it has to be finished as one piece (exports + DHCP), not resurrected from this gate.
- **Benefit:** ~627 lines of unmaintainable, untestable socket code leave the tree; nobody can flip a Makefile flag into a link failure.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ; link failure INFERRED from verified file absence)

## RE-RIPTOPL-160 — APA password DES (~460 lines) runs in a build where password comparison is compiled out (filed as W09-007)

**Category:** 1 dead code / 3 overengineering
**Locations:**
- RiptOPL/modules/hdd/apa/src/password.c:39-498 (`apaEncryptPassword` + the full DES-ECB implementation)
- RiptOPL/modules/hdd/apa/src/password.c:25-36 (`apaPassCmp` returns 0 unconditionally when `APA_ENABLE_PASSWORDS` is undefined)
- RiptOPL/modules/hdd/apa/src/apa-opt.h:31-38 (the `APA_OSD_VER` branch used by this Makefile does **not** define `APA_ENABLE_PASSWORDS`; only the non-OSD branch at :36 does)
- RiptOPL/modules/hdd/apa/src/hdd_fio.c:175-195 (`fioGetInput` DES-encrypts both passwords on every partition open/getstat/remove parse)

**Problem:** In the OSD build that this repo actually ships, no password comparison can ever fail, yet every `hddOpen`/`hddGetStat` that carries passwords in its name string runs two full DES encryptions whose outputs are then "compared" by a no-op, and the ~460-line DES implementation is linked into the IRX solely to produce those values (its other producers are the dead format path of W09-001). On-disk headers written by other tools are unaffected: opening them never depended on the encrypted values matching in this build.
**Fix:** In the OSD configuration, make `apaEncryptPassword` a no-op alongside `apaPassCmp` and drop `password.o`'s DES body (keep the two entry points so callers and the re-vendor diff stay trivial).
**Benefit:** ~460 lines and a per-open DES computation removed from a memory-constrained IOP module.
**Effort:** S · **Risk:** Med (changes bytes written if a partition is ever created by another build variant from these sources — creation is already refused, W09-001) · **Confidence:** High (READ for the config and call sites; INFERRED that no out-of-tree consumer depends on the encrypted fields)

## RE-RIPTOPL-161 — `hdd_blkio.c` compiles to an empty object in this build (415 lines of other builds' code) (filed as W09-006)

**Category:** 1 dead code / 7 build clutter
**Locations:**
- RiptOPL/modules/hdd/apa/src/hdd_blkio.c:1-415 (BDM filesystem-driver variant at :22-169 under `#ifdef APA_USE_BDM`; VHDD variant at :171-303 under `#ifdef APA_USE_IOMANX`; the generic `blkIo*` definitions at :305-415 under `#ifndef APA_USE_ATAD`)
- RiptOPL/modules/hdd/apa/Makefile (`IOP_CFLAGS += -DAPA_OSD_VER -DAPA_USE_DEV9 -DAPA_USE_ATAD`; `hdd_blkio.o` in `IOP_OBJS`)
- RiptOPL/modules/hdd/apa/src/hdd_blkio.h:14-63 (the ATAD build's `blkIo*` are static inlines routing to the table fence — the .c file contributes nothing)

**Problem:** This build defines `APA_USE_ATAD` and neither `APA_USE_BDM` nor `APA_USE_IOMANX`, so every function in hdd_blkio.c is preprocessed away, yet the file is still compiled and linked as an (empty) object and still has to be read, formatted and re-vendored as if it were live. Its BDM/VHDD mount code duplicates journal-restore/format-check logic that also lives in hdd.c's ATAD startup path.
**Fix:** Drop `hdd_blkio.o` from this Makefile's `IOP_OBJS` and delete the file from the vendored tree (it remains available at the SDK pin in ORIGIN.txt if another variant is ever built).
**Benefit:** 415 fewer lines carried in the APA module; one less place where mount/startup logic appears to live.
**Effort:** S · **Risk:** Med (touches the documented re-vendor set) · **Confidence:** High (READ — flags and guards verified)

## RE-RIPTOPL-162 — audio/bgm.ogg (324,182 bytes) is an orphan: not embedded, not packaged, and not the BGM file the packages actually ship (filed as W14-008)

**Category:** 1 dead code / 4 size

**Locations:**
- `RiptOPL/audio/bgm.ogg` — 324,182 bytes, the largest file in `audio/` (census: `audio/` = 9 files, 533,398 bytes; the other 8 are the `.adp` effects).
- Not embedded: `tools/assets.py:62,182-183` embeds only the eight `.adp` names; `Makefile:178-183` says the same for the legacy build ("intentionally NOT compiled into the ELF … kept in the repo only as a reference/default track").
- Not the shipped track: the release app template's BGM is `ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/APPS/APP_RIPTOPL/bgm.ogg`, consumed by `.github/scripts/package_release_app.py:24-30,235-236` — and its md5 (`af3e8d4b117d57161cfdcaa4fdb9b370`) differs from `audio/bgm.ogg` (`a639ef8b7b69c10c6868f8fef3129fba`), so the repo file is not even the source of the packaged one.
- Runtime never reads a repo path: `src/core/bgm.cpp:620-638` loads BGM from, in order, the active disk theme's `sound/bgm.ogg`, a configured BGM path, and the built-in themes' data-home `bgm.ogg` — the last of which is the packaged file above, not this one. A repo-wide grep for `audio/` consumers found only the `.adp` embed rules and the Makefile comment.

**Problem:** A 324 KB media file with no consumer: the build skips it on purpose, the packager ships a different file under the same name, and the loader's three BGM sources are all on-device paths. It survives only as a "reference" copy that does not match the reference it supposedly provides.

**Fix in words:** Delete `audio/bgm.ogg`. If a reference track is genuinely wanted, the packaged `ship/Example/…/bgm.ogg` already is one, in the place the packager reads.

**Benefit:** 324,182 bytes — 61% of `audio/` — removed, and one of two different files named `bgm.ogg` stops masquerading as the shipped default.

**Effort:** S · **Risk:** Low · **Confidence:** Med (READ for embed/packager/runtime paths and both md5s; INFERRED that no out-of-repo consumer exists — the "no consumer" claim rests on the repo-wide grep reported above)

---

## RE-RIPTOPL-163 — mem.c carries a full heap allocator that is dead in every build, plus a callerless `mem_reallocm` (filed as W05-016)

- **Category:** 1 dead code
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/mem.c:123-377,379-390`; `RiptOPL/modules/network/SMSTCPIP/include/lwipopts.h:23`; `RiptOPL/modules/network/SMSTCPIP/include/arch/sys_arch.h:29-37`
- **Problem:** lwipopts.h sets MEM_LIBC_MALLOC 1 unconditionally, so the ~255-line lwIP heap allocator in mem.c (lines 123–377) compiles out of every build; all allocation routes through the mem_clib_* → ps2ip_mem_* wrappers. Separately, `mem_reallocm` (mem.c:379-390) has zero callers in the tree (grep-verified) and is wrong anyway: on the grow path it memcpys `newsize` bytes from the old, smaller block.
- **Fix:** Delete the dead allocator body and mem_reallocm; keep the libc-malloc wrapper functions that are actually used.
- **Benefit:** ~265 lines of never-compiled, subtly wrong allocator code leave the tree; mem.c becomes exactly the shim it is in practice.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + grep-verified)

## RE-RIPTOPL-164 — iso2opl.c keeps ~90 lines of superseded implementations commented out beside the live ones (filed as W11-017)

- Category: 1 dead code
- Locations: `RiptOPL/tools/pc/iso2opl/src/iso2opl.c:95-128` (commented `check_cfg` open/read version), `RiptOPL/tools/pc/iso2opl/src/iso2opl.c:169-195` (commented `write_cfg` version), `RiptOPL/tools/pc/iso2opl/src/iso2opl.c:226-273` (commented `write_parts` version); also `RiptOPL/tools/pc/iso2opl/src/isofs.c:161-173,1055-1058,1078-1080` (commented debug dump and open/close/lseek remnants)
- Problem: Each of iso2opl's three file-writing functions carries its previous POSIX-fd implementation in a block comment directly above the live `FILE*` implementation, and isofs.c carries smaller commented remnants of the same migration. The commented code is not compiled, not referenced, and duplicates the live logic closely enough that a reader must diff them to know which is real. Git history already preserves the old versions.
- Fix: Delete the commented blocks.
- Benefit: −~100 lines; the live implementation is the only one a reader can mistake for current.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-165 — loopif is a complete dead module: file, header, and gated glue all ship but can never run (filed as W05-017)

- **Category:** 1 dead code
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/loopif.c` (whole file, 113 lines); `RiptOPL/modules/network/SMSTCPIP/include/netif/loopif.h` (39 lines); `RiptOPL/modules/network/SMSTCPIP/ps2ip.c:279-294`; `RiptOPL/modules/network/SMSTCPIP/Makefile` (IOP_OBJS has no loopif.o)
- **Problem:** loopif.c is entirely wrapped in `#if LWIP_HAVE_LOOPIF`, which defaults to 0 (opt.h:337) and is set nowhere; the file is also absent from the Makefile object list. ps2ip.c's AddLoopIF/LoopIF glue is gated out by the same macro. The loopback interface can never exist in this tree.
- **Fix:** Delete loopif.c, loopif.h, and the gated AddLoopIF block in ps2ip.c.
- **Benefit:** ~150 lines of impossible code removed; one less phantom interface for readers to reason about.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-166 — usbmass_bd: the unselectable ASYNC implementation of `usb_queue_cmd` is carried alongside the live one (filed as W09-011)

**Category:** 1 dead code
**Locations:**
- RiptOPL/modules/usb/usbmass_bd/src/usb_mass.c:16 (`// #define ASYNC` — commented out, and nothing in the Makefile defines it)
- RiptOPL/modules/usb/usbmass_bd/src/usb_mass.c:445-458 (`scsi_cmd_callback`, `#else` branch only)
- RiptOPL/modules/usb/usbmass_bd/src/usb_mass.c:525-556 (the ASYNC half of `usb_queue_cmd`, including its own TODO at :449 about unhandled errors)

**Problem:** Two complete command-submission strategies live in one function behind a macro that cannot be set from the build. The dead half bypasses the command semaphore that ORIGIN.md documents as the two-drive fix, so it is also a trap: anyone re-enabling it reintroduces the shared-`ucmd` race the fork just fixed.
**Fix:** Delete the `#else` (ASYNC) branch, `scsi_cmd_callback`, and the `ASYNC` guards; keep the synchronous path unconditional.
**Benefit:** ~45 lines removed and the two-drive serialisation can no longer be compiled away by flipping a define.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-167 — Feature-off protocol code retained: `ip_forward` and debug-only `ip_lookup` (filed as W05-026)

- **Category:** 1 dead code
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/ip.c:79-113` (ip_lookup, `#ifdef LWIP_DEBUG`), `RiptOPL/modules/network/SMSTCPIP/ip.c:139-199` (ip_forward, IP_FORWARD = 0)
- **Problem:** ip_forward (~60 lines) is compiled out by IP_FORWARD 0 in every build and nothing in the tree enables forwarding; ip_lookup exists only under LWIP_DEBUG, which is defined nowhere in the tree. Both are dead in this tree's copies regardless of their upstream status.
- **Fix:** Delete both functions (and the ip_lookup declaration use in ip.h if it becomes unreferenced — ip.h:57 declares ip_lookup unconditionally, so the declaration should go with it or be guarded consistently).
- **Benefit:** ~95 lines of never-compiled routing/debug code removed from the IP layer readers must audit.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

---

## RE-RIPTOPL-168 — ziso.py carries a Python-3-broken helper and a never-called "future" feature (filed as W11-004)

- Category: 1 dead code / 8 half-finished
- Locations: `RiptOPL/tools/pc/ziso.py:44-50` (`hexdump`), `RiptOPL/tools/pc/ziso.py:369-405` (`load_sector_table`)
- Problem: `hexdump` is never called anywhere in the file, and could not run if it were: it calls `ord(i)` on the ints produced by iterating `bytes` under Python 3, which raises `TypeError`. `load_sector_table` is likewise never called; its own comment says "In future we will support NC" — a per-sector compression-level feature that was never wired into `parse_args` or `compress_zso`. The tool itself is also outside the PC-tools build: `tools/pc/Makefile` builds only iso2opl, opl2iso and genvmc, and no repo file references `ziso.py` (grep-verified).
- Fix: Delete both functions (47 lines). Keep the tool itself, which is a usable standalone ZSO compressor.
- Benefit: −47 lines of dead/broken code; no behaviour change.
- Effort: S · Risk: Low · Confidence: High (READ + grep-verified)

## RE-RIPTOPL-169 — tools/lang_decompiler.py is dead: no caller anywhere, and it duplicates lang_compiler's YAML representer (filed as W11-005)

- Category: 1 dead code / 2 duplication
- Locations: `RiptOPL/tools/lang_decompiler.py:1-60` (whole file); duplicate at `RiptOPL/tools/lang_decompiler.py:5-15` vs `RiptOPL/tools/lang_compiler.py:30-40`
- Problem: A repo-wide search finds zero references to `lang_decompiler` — no Makefile target, workflow, doc or script invokes it. It is a top-level script (no `__main__` guard, no arguments) that rewrites `lng_src/*.yml` from `lng/*.lng` as a side effect of being run, i.e. an accident hazard sitting next to live tools. Its `represent_str` YAML representer is a copy of the one in `lang_compiler.py`.
- Fix: Delete `tools/lang_decompiler.py`. The live direction (YAML → .lng) is `lang_compiler.py`, driven by `tools/assets.py:103-118`.
- Benefit: −60 lines; removes a script that silently rewrites translation sources if run.
- Effort: S · Risk: Low · Confidence: High (READ + grep-verified)

## RE-RIPTOPL-170 — usmb2 entry points that this build never calls, one of them unsafe if ever enabled (filed as W06-017)

- **Category:** 1 (dead code) / 8 (half-finished features)
- **Locations:** `RiptOPL/modules/network/smb2man/usmb2/usmb2.c` (`usmb2_open`, `usmb2_opendir`, `usmb2_readdir` with the copy at `:792`, `usmb2_size` at `:1031`, `usmb2_init_context`, `usmb2_negotiateprotocol`); build defines in `RiptOPL/ps2.yaml` (smb2man target)
- **Problem:** The smb2man build defines `USMB2_FEATURE_WRITE, NTLM, UNICODE, STAT, MKDIR, UNLINK, RENAME` — but **not** OPENDIR — and compiles with `-ffunction-sections` + `--gc-sections`. smb2man.c's actual call set is: build_request, close, create, mkdir, pread, pwrite, rename, rmdir, sessionsetup, stat, treeconnect, unlink (grep-verified). So the directory-reading and several other vendored entry points are stripped at link time and cost nothing at runtime — but they sit in the source as an attractive nuisance: `usmb2_readdir()` memcpy's a server-controlled length (`OutputBufferLength`-derived `u32`) into the caller's dirent buffer with no clamp, and `usmb2_size()` returns `le64toh(*(uint32_t *)&buf[16])` — a 32-bit read widened to 64. If anyone defines OPENDIR to "finish" directory support, the readdir hole goes live as-is. Directory listing in smb2man.c is in fact implemented separately (its querydir path clamps lengths against `SMB2MAN_DIRBUF`, READ).
- **Fix:** Delete the unused entry points from the vendored copy (the folder already carries RiptOPL patches and documents them), or at minimum delete `usmb2_readdir`/`usmb2_size`. Do not "complete" OPENDIR support on top of this code.
- **Benefit:** Removes a latent wire-controlled overflow that is one `#define` away from shipping, and shrinks a vendored file to what the product uses.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + build flags verified in ps2.yaml)

## RE-RIPTOPL-171 — SMB1 ReadAndX carries a permanently-dead non-custom receive branch (filed as W07-011)

- **Category:** 1 dead code
- **Locations:** RiptOPL/modules/iopcore/cdvdman/smb.c:23 (`#define USE_CUSTOM_RECV 1`, unconditional), :997-1020 (the `#else` branch of `smb_ReadAndX`)
- **Problem:** `USE_CUSTOM_RECV` is defined to 1 at the top of the same file with no build option anywhere that undefines it, so the `#else` implementation of the read path (~24 lines, using `GetSMBServerReply` + separate padding/data receives) can never compile. It is a second, divergent copy of the most security-sensitive receive logic in the file, kept in sync by nobody. READ + grep (no other definition/undef in scope).
- **Fix in words:** Delete the `#else` branch and the macro, leaving the one receive path that actually ships.
- **Benefit:** One SMB1 read path to audit instead of two; ~24 lines gone.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-172 — Header declarations with no definition anywhere in the tree (filed as W05-015)

- **Category:** 1 dead code
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/include/lwip/api.h:104-114,155` (netbuf_alloc, netbuf_free, netbuf_chain, netbuf_data, netbuf_next, netconn_err); `include/lwip/pbuf.h:108,114` (pbuf_ref_chain, pbuf_dechain); `include/lwip/sys.h:97` (sys_now); `include/lwip/tcp.h:427,453` (tcp_pcb_copy, tcp_rexmit_seg); `include/lwip/memp.h:59` (memp_realloc)
- **Problem:** Each of these is declared in a header and defined in no .c file in the tree (grep-verified: declaration sites only). netbuf_first is additionally declared at api.h:114 and then defined as a macro at api.h:121. Any future caller links against air.
- **Fix:** Delete the declarations.
- **Benefit:** Headers stop promising an API the stack does not have; the dechain declaration feeding W05-014 disappears at the source.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + grep-verified)

## RE-RIPTOPL-173 — `do_accept` is a registered no-op that never ACKs (filed as W05-018)

- **Category:** 1 dead code
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/api_msg.c:664-689,813-820`; `RiptOPL/modules/network/SMSTCPIP/include/lwip/api_msg.h:55`; `RiptOPL/modules/network/SMSTCPIP/api_lib.c:383-398`
- **Problem:** do_accept's body only emits debug lines per netconn type and — unlike its neighbours (do_listen ends with TCPIP_APIMSG_ACK) — never ACKs the message. It is registered in the `__decode__` table for API_MSG_ACCEPT, but nothing in the tree ever posts API_MSG_ACCEPT (grep: no occurrence in any .c file); netconn_accept fetches the accept mbox directly and bypasses the message entirely. If anything ever did post it, the caller would wait on an ACK that never comes.
- **Fix:** Delete do_accept, its decode-table entry, and the API_MSG_ACCEPT enum value.
- **Benefit:** Removes a hang-in-waiting from the API message table; the accept path that actually exists stays untouched.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + grep-verified)

## RE-RIPTOPL-174 — The non-core-locking API path in tcpip.c is unreachable and silently drops calls (filed as W05-025)

- **Category:** 1 dead code / 6 bugs
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/tcpip.c:137-156`; `RiptOPL/modules/network/SMSTCPIP/include/lwip/opt.h:64-65`
- **Problem:** LWIP_TCPIP_CORE_LOCKING defaults to 1 in opt.h and lwipopts.h does not override it, so tcpip_apimsg always takes the synchronous locked branch (lines 140-143). The `#else` branch — post the message to the tcpip thread — is dead in every configured build, and it is also wrong: if memp_malloc(MEMP_TCPIP_MSG) fails there, the API call is silently dropped with conn->err left stale, so a caller would block or read a previous error. (Branch deadness READ from config; drop behaviour READ from the branch body.)
- **Fix:** Delete the non-locking branch of tcpip_apimsg and state the core-locking requirement in lwipopts.h, or fix the drop — deleting is LESS and matches every build this tree produces.
- **Benefit:** One less silently-lossy path for a future config flip to fall into.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ)

## RE-RIPTOPL-175 — Dead code in smap_udpbd's transfer layer: an uncalled copy routine and a never-used event flag (filed as W06-016)

- **Category:** 1 (dead code)
- **Locations:** `RiptOPL/modules/network/smap_udpbd/src/xfer.c:80` (`CopyFromFIFO`), `:12` and `:56` (`tx_done_ev`)
- **Problem:** `CopyFromFIFO` is defined and never called anywhere in the module (verified by grep: only the definition exists). `tx_done_ev` is created in `xfer_init()` and never waited on or signalled anywhere else (grep: only definition + creation). Both are leftovers of an earlier transfer design; udpfs_smap's xfer.c — the sibling implementation — gets by without equivalents in its live paths.
- **Fix:** Delete both. If `CopyFromFIFO` is wanted as a utility, it belongs with the copy routine that is actually used, not as a second copy.
- **Benefit:** ~30 dead lines and one useless IOP event flag removed from a shipped IRX.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + grep-verified absence of callers)

## RE-RIPTOPL-176 — raudp leftovers: a write-only diagnostic counter and heartbeat fields kept for a removed feature (filed as W06-019)

- **Category:** 1 (dead code) / 8 (redundant features)
- **Locations:** `RiptOPL/modules/network/raudp/raudp.c:771`, `:875`, `:924-925` (`ra_disc_us`); heartbeat sender in the same file
- **Problem:** `ra_disc_us` is accumulated in two places and read only by its own overflow guard — a counter that produces nothing ("retained for bounded diagnostic accounting", but nothing ever reads the account). Separately, the heartbeat packet permanently sends three zero fields whose only purpose is keeping a client parser happy for a badge-upload feature that no longer exists. READ.
- **Fix:** Delete the counter and its increments. Keep the heartbeat's wire layout (external parser) but drop the explanatory dead fields only if the parser tolerates it — if not, keep the zeros and delete just the counter; the finding's actionable half is the counter.
- **Benefit:** Small, honest shrink of a timing-sensitive discovery loop.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-177 — BDM: `g_bd_sectors_per_sector` is written but never read; the sector factor is hardcoded instead (filed as W07-005)

- **Category:** 1 dead code / 6 bugs (latent)
- **Locations:** RiptOPL/modules/iopcore/cdvdman/device-bdm.c:22 (declaration), :106 (only write), :234 and :244 (hardcoded `lsn * 4` / `sectors * 4`)
- **Problem:** Command-verified: the variable has exactly two occurrences — declaration and assignment `(2048 / bd->sectorSize)` at attach. Nothing reads it; `DeviceReadSectors()` always multiplies by 4, which is only correct for a 512-byte-sector device. For any attached device with a different sector size the conversion the variable was computed for is silently not applied. READ + grep census.
- **Fix in words:** Either use the stored factor in the two `bd_defrag()` calls or delete the variable and state the 512-byte assumption at the attach check (the PCCF path already enforces 512). One of the two — not both mechanisms.
- **Benefit:** Removes a write-only variable and closes a latent wrong-LBA path.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; the wrong-LBA consequence for non-512 devices is INFERRED — no such device was tested)

## RE-RIPTOPL-178 — smb.h declares a function that does not exist and ~150 constants the in-game clients never use (filed as W07-012)

- **Category:** 1 dead code
- **Locations:** RiptOPL/modules/iopcore/cdvdman/smb.h:501 (`smb_SessionSetupTreeConnect` — command-verified: no definition and no caller anywhere in the repo tree searched); RiptOPL/modules/iopcore/cdvdman/smb.h:55-95 (TRANS2 offset tables) and the SMB command/info-level blocks (census: 153 `SMB_` defines in the header; grep over smb.c, smb2.c, nfs.c finds zero uses of the TRANS2, info-level, or raw/transaction command groups)
- **Problem:** The header is shared by three alternative clients (SMB1, SMB2, NFS) that between them use a small fraction of it. The dead prototype is the sharp edge: anything linking against it fails only at load time. The unused constant blocks are pure audit noise in a security-sensitive header. READ + grep census.
- **Fix in words:** Delete the dead prototype; delete the constant groups with zero uses in all three clients. Keep every define any of the three files references.
- **Benefit:** Smaller shared header; no phantom API.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + command-run census)

## RE-RIPTOPL-179 — imgdrv keeps dead code and hand-rolled duplicates of the ioman types it already includes (filed as W07-018)

- **Category:** 1 dead code / 2 duplication
- **Locations:** RiptOPL/modules/iopcore/imgdrv/imgdrv.c:13-16 (`dummy_fs`, referenced only inside a commented-out block at :73-81), :39-95 (private `_iop_device_tm` / `_iop_device_ops_tm` struct definitions duplicating `iop_device_t` / `iop_device_ops_t`)
- **Problem:** The file includes `<ioman.h>` and then redefines the device and ops structs under local names, truncating the ops table at `lseek` — a maintenance trap if the real ops layout is ever consulted. `dummy_fs()` is never referenced by live code. READ.
- **Fix in words:** Delete `dummy_fs` and the two private struct definitions; declare the device with the real ioman types like every other driver in this scope.
- **Benefit:** ~40 lines of misleading duplication gone from a module on the IOP-reset path.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-180 — MX4SIO CRC16 support is compiled in but permanently switched off; CRC7 mode selection is broken-but-lucky (filed as W08-012)

- Category: 1 dead code / 8 half-finished
- Locations: `RiptOPL/modules/mx4sio_bd/Makefile` (`IOP_OBJS` includes `crc16.o`), `RiptOPL/modules/mx4sio_bd/src/mx4sio.h:37,45-46`, `RiptOPL/modules/mx4sio_bd/src/mx4sio.c:42-49`, `RiptOPL/modules/mx4sio_bd/src/spi_sdcard_driver.c:463-475`; `RiptOPL/modules/mx4sio_bd/src/spi_sdcard_driver_config.h:22`, `RiptOPL/modules/mx4sio_bd/src/spi_sdcard_crc7.c:5-45`
- Problem: READ. (a) `CONFIG_USE_CRC16` is commented out in `mx4sio.h`, so the only caller of `crc16()` is `#ifdef`'d away and the RX ISR explicitly discards the two CRC bytes — yet `crc16.c` is still compiled and linked into every mx4sio build, and `dma_command_t` carries a `crc[512]` "FIXME" field under the same dead flag. Read data is therefore never CRC-checked, and the dead object ships anyway. (b) The config selects `SD_CRC7_MODE_RAM_TABLE`, but `spi_sdcard_crc7.c:7` tests `SD_CRC7_MODE == CRC7_MODE_RAM_TABLE` — a macro name that does not exist (missing `SD_` prefix) — so the RAM-table branch (and `crc7_generate_table`, which nothing calls) is silently skipped and the ROM table is used. It works by accident; anyone "fixing" the config to match its comment breaks the build/selection.
- Fix: Pick one state per mechanism and delete the other: for CRC16, either enable it end-to-end or delete `crc16.c/.h`, the Makefile object, and the `#ifdef` blocks; for CRC7, set the config to `SD_CRC7_MODE_ROM_TABLE` (what actually runs) and delete the RAM-table branch and its generator.
- Benefit: Removes a dead object from every build and a config that lies about what is compiled.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-181 — Dead field: fatfs mount-info `mount_status` is declared and never used (filed as W08-019)

- Category: 1 dead code
- Locations: `RiptOPL/modules/bdm/bdmfs_fatfs/src/include/fs_driver.h:11`
- Problem: READ via grep over the module: `mount_status` appears only in the struct definition — no read or write anywhere in `fs_driver.c` or elsewhere in the module. Mount state is actually carried by `mounted_bd != NULL`, so the field is a second, permanently-stale source of truth for anyone reading the struct.
- Fix: Delete the field.
- Benefit: One less misleading field in the central mount struct.
- Effort: S · Risk: Low · Confidence: High (READ)

---

## RE-RIPTOPL-182 — ps2atad carries whole subsystems behind macros its Makefile never defines (filed as W09-018)

**Category:** 1 dead code / 7 build clutter
**Locations:**
- RiptOPL/modules/hdd/atad/Makefile (defines only `ATA_USE_DEV9=1`, `ATA_GAMESTAR_WORKAROUND=1`, `ATA_ENABLE_BDM=1`)
- RiptOPL/modules/hdd/atad/src/ps2atad.c:29-31, 213-215 and the AIFDEV9 blocks throughout (incl. `aif_tune_drive` and the AIF interrupt callback) — `ATA_USE_AIFDEV9` never defined
- RiptOPL/modules/hdd/atad/src/ps2atad.c:1454-1485 (`ata_pio_mode`'s `ATA_ALL_PIO_MODES` table — never defined; the live path is the single-value `#else`), :1486+ (`ATA_MWDMA_MODES`, see W09-008), the `ATA_SCE_AUTH_HDD` block in `ata_init_devices`, and the `ATA_ENABLE_MAXUDMA` branch

**Problem:** Because the file is vendored, every reader (and every re-vendor diff) has to reason about four configuration axes this build can never take, including an entire second host-controller backend (AIFDEV9). The live DEV9 path is interleaved with the dead one at almost every function.
**Fix:** Leave the vendored file alone in this pass, but record the dead axes in ORIGIN.txt next to the other local-change notes so the re-vendor procedure strips them deliberately instead of re-reviewing them; strip them in the tree only if the fork accepts one more documented local change.
**Benefit:** Review and re-vendor effort stops being spent on unreachable backends; no behavioural change proposed blind.
**Effort:** S (documentation) · **Risk:** Low · **Confidence:** High (READ — Makefile flags vs. `#ifdef` census)

## RE-RIPTOPL-183 — Broken, unused `PACKET_TCODE` macro (filed as W10-014)

- **Category:** 1 (dead code)
- **Locations:** `RiptOPL/modules/ilink/iLinkman/src/include/iLink_internal.h:92`
- **Problem:** `#define PACKET_TCODE (((x) >> 4) & 0xF)` is an object-like macro whose body references an undefined `x` — it cannot compile if ever used. A tree search shows it is never used.
- **Fix:** Delete the macro.
- **Benefit:** Removes a landmine that breaks the build the first time anyone reaches for it.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-184 — `DeinitIEEE1394` is dead and incomplete (filed as W10-020)

- **Category:** 1 (dead code) / 8 (half-finished features)
- **Locations:** `RiptOPL/modules/ilink/IEEE1394_bd/src/sbp2_driver.c:802-807` (definition), `:20,93` (the `writeBuffer` it never frees); declaration `RiptOPL/modules/ilink/IEEE1394_bd/src/include/sbp2_disk.h:180`
- **Problem:** Nothing in `modules/ilink` calls `DeinitIEEE1394`. As written it also would not finish the job: it terminates the thread and deletes the event flag but never frees the heap `writeBuffer`, never deletes `sbp2_io_sema`, and never disables the bus. It reads as an unload path that was started and abandoned.
- **Fix:** Delete the function and its declaration. If module unload is ever actually required, it should be written complete at that point.
- **Benefit:** Removes a false promise of clean unload.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; caller search run this session)

## RE-RIPTOPL-185 — sio2man_hook carries a permanently disabled mx4sio port-blocking feature (filed as W10-027)

- **Category:** 1 (dead code)
- **Locations:** `RiptOPL/modules/mmce/mmceman/src/sio2man_hook.c:26-29` (`PORT_NR` defined only inside `#if 0`), `:58-85`, `:115-121`, `:275-281` (all `#ifdef PORT_NR`)
- **Problem:** `PORT_NR` can never be defined — its only definition sits inside `#if 0` with the comment "Only needed for mx4sio" — so the transfer-filter function, its two hook wrappers, and the hook/unhook code for export 25 all compile out, permanently. It is leftover from the MX4SIO driver this file was taken from.
- **Fix:** Delete the `#if 0` block and every `PORT_NR`-guarded block (~35 lines).
- **Benefit:** The hook file shrinks to what mmceman actually does; nobody has to re-derive that the filter is inert.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-186 — pccf ships a formatter that is never included and a diagnostic entry point that is never defined (filed as W10-029)

- **Category:** 1 (dead code) / 7 (repo clutter)
- **Locations:** `RiptOPL/modules/pccf/format.inc` (whole file, 57 lines); `RiptOPL/modules/pccf/pcdiag_ata.h:20-21` (`pcdiag_ata_observe`)
- **Problem:** A repo-wide search finds `format.inc` referenced by nothing but the git index — no source includes it, so its `format_line` mini-printf is dead weight in every checkout. `pcdiag_ata_observe` is declared in the header but has no definition and no caller anywhere in the tree: a leftover of the diagnostic program the driver was adapted from.
- **Fix:** Delete `format.inc` and the `pcdiag_ata_observe` declaration.
- **Benefit:** Two fewer phantom surfaces for a reader to chase.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; searches run this session)

## RE-RIPTOPL-187 — Retired ps2sdk fallback trees keep dead source files, and ps2sdk-socket's README describes the retired build as current (filed as W11-009)

- Category: 1 dead code / 7 repo clutter
- Locations: `RiptOPL/third_party/ps2sdk-socket/socket.c`, `RiptOPL/third_party/ps2sdk-socket/fdman.h`, `RiptOPL/third_party/ps2sdk-socket/README.md:3-7` vs `RiptOPL/third_party/ps2sdk-socket/UPSTREAM:4-6`; `RiptOPL/third_party/ps2sdk-ppctty/irx_imports.h:1-21` vs `RiptOPL/third_party/ps2sdk-ppctty/UPSTREAM:3-6`
- Problem: Both trees are provenance-only now. `ps2sdk-socket/UPSTREAM` says the fallback was "retired from build graphs… no fallback archive is linked", and the build confirms it: no build file references `socket.c`/`fdman.h` (grep-verified), and `tools/build_edition.py:253-257` copies only `LICENSE` and `UPSTREAM` from these trees into non-standard kits. Yet `ps2sdk-socket/README.md` still says in the present tense "Its build compiles `socket`, `bind`, `recvfrom`, `sendto` and `setsockopt` into a separate archive" — a reader (or a future agent) will believe a second socket implementation is live. `ps2sdk-ppctty/irx_imports.h` is the import header of a driver the tree's own `UPSTREAM` says is no longer bundled ("No bundled driver is built"); nothing includes it.
- Fix: Delete `socket.c`, `fdman.h` and `irx_imports.h`; rewrite `ps2sdk-socket/README.md` to past tense matching its `UPSTREAM` (provenance only). Keep `LICENSE`/`UPSTREAM` in both trees — they are what the kits actually redistribute.
- Benefit: −3 dead files (~21 KB tree shrinks to its redistributed records); the docs stop describing a phantom second socket stack.
- Effort: S · Risk: Low · Confidence: High (READ + grep-verified)

## RE-RIPTOPL-188 — tools/pc/xeRAbora.url is dead: the packager hardcodes its contents instead of copying the file (filed as W11-013)

- Category: 1 dead code / 2 duplication
- Locations: `RiptOPL/tools/pc/xeRAbora.url:1-2`; `RiptOPL/tools/package_edition.py:182-186`
- Problem: `package_edition.py` copies the other five kit shortcuts from `tools/pc/*.url` by name (lines 182-184), but for xeRAbora it ignores the existing `tools/pc/xeRAbora.url` and writes the identical two lines inline (line 184: `[InternetShortcut]\nURL=https://github.com/hacan359/xerabora/releases/tag/v1.0.1\n` — byte-identical to the file, READ). The file is referenced nowhere (grep-verified), so a future xeRAbora version bump edited in the obvious place — the `.url` file beside the other shortcuts — would silently not ship.
- Fix: Add `"xeRAbora.url": "xeRAbora.url"` to the existing `shortcuts` dict for RA kits and delete the inline `write_text`. (Or delete the file and keep the inline write — but the dict route matches the other five.)
- Benefit: One place per shortcut; the version pin lives with its siblings.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-189 — Dead and no-op code: placeholder flag ORs, commented-out launch/teardown blocks, a commented-out size calculation (filed as W01-019)

- **Category:** 1 dead code
- **Locations:** `RiptOPL/src/sources/bdmlaunch.cpp:983` (`settings->common.fakemodule_flags |= 0 /* TODO! fake ilinkman ? */;`) and `:986-987` (MX4SIO branch `|= 0;`), `RiptOPL/src/sources/mmcesupport.cpp:1740-1747` (commented-out `/* No autolaunch yet else { miniDeinit… } */` block), `RiptOPL/src/sources/bdmsupport.cpp:2156-2157` and `RiptOPL/src/sources/mmcesupport.cpp:1801-1802` (commented-out `if ((exception & UNMOUNT_EXCEPTION) == 0) …` stubs in both cleanUps), `RiptOPL/src/sources/mmcesupport.cpp:1819` (commented-out STOP_ALL devctl in shutdown, under a comment describing it as required), `RiptOPL/src/sources/supportbase.cpp:650-668` (TODO + commented-out UL size-calculation loop)
- **Problem:** Statements that do nothing (`|= 0`), an unresolved TODO sitting in a launch dispatch, and four blocks of commented-out code — two of them in teardown paths where a reader cannot tell whether the commented behaviour is missing by accident or by decision. The mmcesupport shutdown case is the sharpest: the comment says the STOP UNIT command is "required by some HDDs", the line that would do it is commented out, and the BDM backend implements the same thing live (bdmsupport.cpp shutdown, gated on `gDeinitTerminal`).
- **Fix:** Delete all of it. For the MMCE STOP_ALL line, make the decision explicit in the commit that removes it (either wire it like BDM's, gated the same way, or record in one line why MMCE must not send it) — do not leave it as a comment.
- **Benefit:** Launch dispatch and teardown read as decided code; ~35 lines removed; one latent "is this a bug?" question closed.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ.

## RE-RIPTOPL-190 — Retired main-menu slot kept in the menu ID enum (filed as W02-016)

- **Category:** 1 dead code / 8 half-finished
- **Locations:** `RiptOPL/src/ui/menusys.cpp:52-55` (`MENU_RETIRED_NETWORK_UPDATE`)
- **Problem:** READ. The enum keeps a placeholder for a removed Network Update item "to keep subsequent menu IDs stable". Menu IDs in this enum are internal dispatch values assigned in this same file; nothing in the read code persists them (config stores settings IDs, not these). The placeholder is dead weight that every reader must reason about.
- **Fix in words:** Delete the placeholder and renumber, after a grep confirming no persisted use of the numeric values (the check, not the renumber, is the work).
- **Benefit:** One less phantom menu item; enum matches the actual main menu.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ; "not persisted" is INFERRED — the grep is part of the fix)

## RE-RIPTOPL-191 — `vmc_groups.cpp` ships dead data: malformed title IDs that can never match, and duplicated IDs (filed as W03-021)

- **Category:** 1 (dead code)
- **Locations:** `RiptOPL/src/core/vmc_groups.cpp:14` onwards (the `titles_XEBP_100_*` tables)
- **Problem:** Script census over the file's string literals (run in this session): 1,144 ID-like literals extracted. Eight do not match the `XXXX_NNN.NN` shape every real title ID follows — `SLUS_20754`, `SLUS_20824`, `SCES_50422`, `SLES_50422`, `SCES_51154`, `SLES_51154` (missing the `.NN` suffix), `SLES-5350` (wrong separator and shape), and `SSCES_540.95` (five-letter prefix). A lookup keyed on real startup IDs can never equal these strings, so those entries are dead weight that also *fail to do their job* — the games they were meant to group will not group. Thirteen IDs appear more than once across the tables, including `SLES_546.24` and `SLUS_216.62` twice within a single group each, where the duplicate cannot change the result.
- **Fix:** Correct the eight malformed IDs to their real forms (they are recognisably typos of real IDs — missing suffix / doubled letter) and delete the within-group duplicates. Cross-group duplicates should be checked once against intent before removal.
- **Benefit:** Eight games group as intended; the table stops carrying entries that provably do nothing.
- **Effort:** S · **Risk:** Low · **Confidence:** High (script census, this session; the "typo of a real ID" reading is INFERRED from shape)

## RE-RIPTOPL-192 — `PadManager::initialize` returns 1 on every path, making `startOne`'s failure branch dead; `keyPressed` lacks the bounds guard its siblings have (filed as W03-024)

- **Category:** 1 (dead code) / 6 (bugs)
- **Locations:** `RiptOPL/src/core/pad.cpp:188-297` (`initialize`; `return 1` at lines 209, 235, 248, 256, 296), `RiptOPL/src/core/pad.cpp:1136` (`startOne`), `RiptOPL/src/core/pad.cpp:1049` (`keyPressed`)
- **Problem:** Every exit from `initialize` — not connected, not DualShock, no actuators, success — returns 1. `startOne` nevertheless treats `!initialize(pad)` as failure and returns 0, a branch that can never run; `startPads` therefore counts every opened port as started regardless of what init found. The return value is pure noise: it documents a contract the function does not honour. Separately, `keyPressed(id)` indexes `keyToPad[id]` with no range check, while `key`, `keyOn` and `setButtonDelay` in the same file all guard `id` to 1–16 first — an inconsistent guard that turns any out-of-range caller into an out-of-bounds read of a 17-entry table.
- **Fix:** Make `initialize` return void (delete the dead check in `startOne`) — or, if pad counting is supposed to reflect init success, return 0 on the not-connected path only; either way, one honest contract. Add the same two-line guard to `keyPressed` that its siblings already have.
- **Benefit:** Removes a dead branch and a fake contract from the pad bring-up path; closes the one unguarded index in the key API.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-193 — `sfxGetDropDiag` is a stub that reports zeros for counters that no longer exist (filed as W03-026)

- **Category:** 1 (dead code)
- **Locations:** `RiptOPL/src/core/sfx.cpp:305-312`; declared in `RiptOPL/include/sound.h:28`
- **Problem:** The function unconditionally writes 0 to both outputs. The drop counters it once reported belonged to the old multi-voice SFX pool, which the same file's comments explain was replaced by the shared-voice design — the thing being counted no longer exists. The debug HUD still calls it (outside this scope) and dutifully displays the zeros as data.
- **Fix:** Delete the function, its declaration, and the HUD call/display. If a drop counter is wanted again, it should count something the current design actually does.
- **Benefit:** Removes a diagnostic that reports fiction; one less API surface in sound.h.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; the HUD call site is outside scope and was confirmed by grep only)

## RE-RIPTOPL-194 — httpclient: an empty `HttpStreamEnd` kept "for symmetry", and `HttpSendGetRequest` skips the init guard (filed as W03-027)

- **Category:** 1 (dead code) / 6 (bugs)
- **Locations:** `RiptOPL/src/platform/httpclient.cpp:157` (`HttpStreamEnd`), `RiptOPL/src/platform/httpclient.cpp:61` (`HttpSendGetRequest`), guard present at `:38-42` (`HttpEstabConnection`)
- **Problem:** `HttpStreamEnd` is an empty function retained for API symmetry with `HttpStreamStart` — a no-op every caller must still call and every reader must still check. In the same file, `HttpEstabConnection` guards on `HttpIsInitialized()` but `HttpSendGetRequest` does not, so the request path can run against an uninitialised stack if a caller reaches it first.
- **Fix:** Delete `HttpStreamEnd` and its call sites; add the same one-line guard to `HttpSendGetRequest`.
- **Benefit:** One less no-op in the API; consistent preconditions across the file's entry points.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + grep census of `HttpIsInitialized` uses)

## RE-RIPTOPL-195 — ioman.h documents a queue-removal function that does not exist (filed as W04-010)

- **Category:** 1 (dead code — dead documentation)
- **Locations:** `RiptOPL/include/ioman.h:38-41`
- **Problem:** A doc comment ("removes all requests of a given type from the queue / @param type … / @return the count of the requests removed") sits between two real declarations, but no function declaration follows it, and a grep of `src/` + `include/` finds no implementation and no caller for any such removal function. The comment describes an API that was removed (or never landed), and a reader will hunt for it.
- **Fix:** Delete the orphaned comment block. (If the function is actually wanted, that's a separate feature decision — nothing in the tree calls it.)
- **Benefit:** The header stops advertising a nonexistent API.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (comment, absence of declaration, and absence of implementation all verified).

## RE-RIPTOPL-196 — Remove_Padhook is declared but never defined and never called (filed as W04-011)

- **Category:** 1 (dead code)
- **Locations:** `RiptOPL/ee_core/include/padhook.h:25`
- **Problem:** `void Remove_Padhook(void);` is declared in the header, but a recursive grep of all of `ee_core/` finds no definition and no call site — only the declaration itself. (Contrast with the genuinely-used hook installers in the same subsystem, which are defined in `padhook.c` / `cheat_api.c` and called.) Anyone calling it would fail to link.
- **Fix:** Delete the declaration.
- **Benefit:** Header stops promising a function the binary doesn't have.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (grep over the whole ee_core tree).

## RE-RIPTOPL-197 — Twelve gfx/ files (29,792 bytes) are never embedded in any flavour: seven device icons the code deliberately dropped, and four byte-identical duplicates whose blobs the code already shares (filed as W14-007)

**Category:** 1 dead code / 4 size (asset census, script + md5)

**Locations:**
- Census (script, at pin): `gfx/` = 142 files, 443,330 bytes. `tools/assets.py:49-60,166-171` embeds 128 PNGs in the default build (104 base + 24 glyphs), plus `ra_mark` only for the RA flavour and `apps_case` only for the Korium theme — leaving 14 files unembedded in the default build, of which those two are flavour-used and the 12 below are used by no flavour.
- `RiptOPL/gfx/Device_1.png` (582 B), `Device_2.png` (742 B), `Device_3.png` (886 B), `Device_4.png` (899 B), `Device_5.png` (600 B), `Device_6.png` (784 B), `Device_all.png` (1,016 B), `no_Device.png` (676 B) — `src/gfx/textures.cpp:260-266` maps `DEVICE_1`…`DEVICE_ALL` to `NULL` data, and the comment at `src/gfx/textures.cpp:269-277` states the device-indicator icons were "removed from the embedded themes' info page, so no longer embedded (dropped from PNG_ASSETS)" and the `no_Device` fallback was "dropped with the device indicator; not embedded". The names survive only so a disk theme can override by filename — a theme ships its own PNGs; it does not read this repo folder.
- Byte-identical duplicates (md5, script): `gfx/logo4.png` = `gfx/logo3.png` (8,569 B, md5 `84eb33a7d0daa1321fd2a07185dc6508`), `gfx/logo5.png` = `gfx/logo2.png` (7,161 B, `ca7048eb7168928716e42ee648c05572`), `gfx/logo6.png` = `gfx/logo1.png` (6,545 B, `1651f8d83156fabefe580ab610a96636`), `gfx/no_Rating.png` = `gfx/Rating_0.png` (1,332 B, `dd241f101796a79a67cd957ea6f6a17d`). The code already exploits exactly this: `src/gfx/textures.cpp` maps `LOGO4/5/6` to `&logo3_png`/`&logo2_png`/`&logo1_png` and `NO_RATING_PICTURE` to `&Rating_0_png`, with comments saying the files are byte-identical and the blob is shared.

**Problem:** 12 source files totalling 29,792 bytes (6,185 B device icons + 23,607 B duplicate copies) sit in the asset folder that every contributor treats as "what gets embedded", while the build embeds none of them and the code comments already record the decision to drop/share them. They are pure repo weight and a standing invitation to edit a file that changes nothing.

**Fix in words:** Delete the 12 files. Keep the texture-table names and the shared-blob mappings in `src/gfx/textures.cpp` exactly as they are (disk-theme override by filename is unaffected — those themes carry their own files).

**Benefit:** `gfx/` becomes an exact list of what a build can embed; 29,792 bytes and four duplicate-maintenance traps removed.

**Effort:** S · **Risk:** Low · **Confidence:** High (READ — embed list, texture table and md5s all script-verified)

---

## RE-RIPTOPL-198 — ARP entries never expire: `etharp_tmr` is never scheduled because `LWIP_ARP` is defined nowhere (filed as W05-004)

- **Category:** 8 half-finished features / 6 bugs
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/timers.c:103-125`; `RiptOPL/modules/network/SMSTCPIP/etharp.c:191,209-229`; `RiptOPL/modules/network/SMSTCPIP/include/lwip/opt.h:138`
- **Problem:** `arp_timer()` and its scheduling inside `sys_timeouts_init()` are wrapped in `#if LWIP_ARP`. A grep for any definition of LWIP_ARP across SMSTCPIP (headers, lwipopts.h, Makefile) returns 0 hits, so both compile out and `etharp_tmr()` never runs: ARP entries never age, ARP_MAXAGE/ARP_MAXPENDING are dead constants, and a stale gateway/server MAC persists for the whole session (with ARP_TABLE_SIZE only 2–4, lwipopts.h). `sys_timeouts_init()` itself is left as an empty function. Separately, `etharp_init()` (etharp.c:191) has no caller anywhere in the tree (harmless today because the table is statically zero-initialised to ETHARP_STATE_EMPTY). The memp sizing comment at opt.h:138 — `(LWIP_TCP + LWIP_ARP)` timeouts — shows the ARP timer was expected to exist.
- **Fix:** Define LWIP_ARP to 1 in lwipopts.h (one line; restores the intended periodic aging), or, if permanent entries are actually desired, delete `etharp_tmr`/`arp_timer` and say so in the config. The one-line define is the smallest change that makes the shipped code do what it says.
- **Justification (fix adds a define):** It activates code already in the tree rather than adding a new mechanism; the alternative (deleting the timer) changes observable behaviour more.
- **Benefit:** ARP cache behaves as the code and comments describe; a changed router/server MAC is picked up without a reboot.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + grep-verified absence)

## RE-RIPTOPL-199 — EE RPC socket surface is wired end-to-end to an empty stub (filed as W05-002)

- **Category:** 8 half-finished/redundant features
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/exports.tab:17-24,30`; `RiptOPL/modules/network/SMSTCPIP/ps2ip.c:268-271`; `RiptOPL/modules/network/ps2ips/ps2ips.c:468-480,482-513,515-525,526-556,558-575`; `RiptOPL/modules/network/common/smstcpip.h:56-57,109-121,147-150`
- **Problem:** Export slots for lwip_select (14), lwip_ioctl (15), lwip_getpeername (16), lwip_getsockname (17), lwip_getsockopt (18), ps2ip_setconfig (20), ps2ip_getconfig (21) and netif_find (27) are all `ps2ip_Stub` in every build — and `ps2ip_Stub` is an empty `void` function. Yet ps2ips implements and dispatches RPC handlers for exactly these operations (do_select, do_ioctlsocket, do_getsockname, do_getpeername, do_getsockopt, do_getconfig, do_setconfig), imports the symbols, and `common/smstcpip.h` declares matching imports for other IOP modules. Every EE-side select/ioctl/getsockopt/getsockname/getpeername/getconfig/setconfig call therefore lands on a no-op and the caller reads back whatever the return register happened to hold (INFERRED ABI consequence; the stub wiring itself is READ).
- **Fix:** Decide once: either export the real implementations (they exist, gated — see W05-003) or delete the ps2ips handlers, the ps2ips imports, and the `smstcpip.h` import declarations so the missing feature fails loudly at link time instead of silently at runtime. Deleting is the LESS option if nothing on the EE actually needs these calls.
- **Benefit:** Removes a silent-garbage result path from the EE↔IOP RPC surface; whichever way it is decided, behaviour becomes explicit.
- **Effort:** M · **Risk:** Med (EE callers, out of scope here, may unknowingly depend on the garbage return) · **Confidence:** High (READ)

## RE-RIPTOPL-200 — DESR/DVR support is a half-finished subsystem whose own header says it cannot claim to work (filed as W01-011)

- **Category:** 8 half-finished features
- **Locations:** `RiptOPL/src/sources/hddsupport.cpp:52-310` (DVR state, driver bring-up, scan, publish, cleanup), `:1884` (scan called from `HddSource::update`), `:2470-2530` (`hddLaunchDvrIso`), `:2614` (dispatch from `HddSource::launch`), `:3247` (reset in `hddIopWasReset`)
- **Problem:** ~260 lines of scan/publish/launch machinery (plus hooks in update, launch, cleanup, shutdown, and IOP-reset) exist for PSX DESR DVR partitions, gated on a hardware capability bit and an enable flag, while the block comment at its head states the unresolved blocker verbatim: "TODO (DESR hardware gate): load DVRP modules after switching to PSX mode without destroying the GUI's other IOP clients… Do not claim general DESR boot compatibility before that initialization has been validated." On all non-DESR hardware the code is inert; on DESR hardware its core precondition is admittedly unvalidated, and its launches depend on an external Neutrino DVR build.
- **Fix:** Delete the DVR subsystem (scan, publish, launch leg, and its hooks) until the DVRP-initialisation question is settled and validated on DESR hardware; re-add it then as one finished piece. If it is in fact validated, the cheaper fix is to delete the stale TODO and say so — the current state (full machinery + "do not claim this works") is the worst option.
- **Benefit:** Removes ~260 lines and five hook points from the HDD backend that every HDD change must currently step around.
- **Effort:** M · **Risk:** Med (DESR users, if any, lose the read-only DVR list) · **Confidence:** High — READ (the half-finished status is the code's own comment, not an inference).

## RE-RIPTOPL-201 — `mmce_fs_ioctl` is registered but returns success for every command (filed as W10-028)

- **Category:** 8 (half-finished features)
- **Locations:** `RiptOPL/modules/mmce/mmceman/src/mmce_fs.c:402-405`, registered at `:1194`
- **Problem:** The ops table wires `ioctl` to a stub that returns 0 unconditionally — every ioctl, known or not, reports success and does nothing. The same table handles genuinely unsupported ops honestly via `NOT_SUPPORTED_OP` (−ENOTSUP) everywhere else.
- **Fix:** Return `-ENOTSUP` from the stub (or point the slot at `NOT_SUPPORTED_OP` and delete the stub).
- **Benefit:** Callers stop believing ioctls took effect when nothing happened.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-202 — SOCK_RAW is accepted, then silently hollow (filed as W05-005)

- **Category:** 8 half-finished features
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/sockets.c:586-589`; `RiptOPL/modules/network/SMSTCPIP/api_msg.c` (all `#if LWIP_RAW` cases, e.g. lines 297, 355, 412, 444, 496, 547, 598, 626, 669, 696, 739, 787); `RiptOPL/modules/network/SMSTCPIP/include/lwip/opt.h:111-113`; `RiptOPL/modules/network/SMSTCPIP/memp.c:62,75,89-91`; `RiptOPL/modules/network/SMSTCPIP/include/lwip/raw.h` (whole file, 75 lines)
- **Problem:** `lwip_socket` accepts SOCK_RAW and creates a NETCONN_RAW netconn, but LWIP_RAW is defined nowhere and there is no raw.c in the tree (`find` = 0), so no PCB is ever created for it: do_bind/do_send for RAW fall through their compiled-out cases and report success while doing nothing. Meanwhile the memp pool still reserves MEMP_NUM_RAW_PCB = 4 entries in every build, and raw.h ships a full API (raw_new/raw_bind/raw_send/…) with no implementation behind it.
- **Fix:** Reject SOCK_RAW in lwip_socket with a protocol-not-supported error, and delete the RAW memp pool entries and raw.h. (Actually implementing raw sockets is the MORE option and is not justified by any in-tree user.)
- **Benefit:** socket() stops returning a descriptor that can never work; a small permanent IOP memory saving from the dead pool.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ + grep/find verified)

## RE-RIPTOPL-203 — `configRead` walks a five-rung filename fallback chain of accumulated migrations (filed as W03-016)

- **Category:** 8 (half-finished/redundant features)
- **Locations:** `RiptOPL/src/core/config.cpp:1419-1495`
- **Problem:** Reading one config set tries, in order: the current filename, the wOPL filename, the carry-over directory with the current name, the legacy filename, and the carry-over directory with the legacy name. Each rung is a past migration that was never retired. The cost is not just code: with five candidate locations, "which file did my settings come from" has five answers, and every future rename adds a sixth rung instead of replacing one.
- **Fix:** Keep the current name plus at most one legacy rung (the one current users can still plausibly have), and delete the carry-over-directory duplicates — the carry-over concept is already handled at the settings-home level (see W03-017), so doing it again per-file is the redundancy.
- **Benefit:** Fewer silent sources for the same settings; migrations become finite.
- **Effort:** M · **Risk:** Med (an install old enough to need a deleted rung falls back to defaults once, then saves in the current name) · **Confidence:** High (READ)

## RE-RIPTOPL-204 — Favourites permanently carry a foreign-format import parser for other loaders' `favourites.bin` (filed as W01-020)

- **Category:** 8 redundant features
- **Locations:** `RiptOPL/src/sources/favsupport.cpp:143-165` (`favMapWoplMode` mode-translation table), `:167-230` (`favReadWoplFile`), `:240-270` (import branch inside `favParseImage`)
- **Problem:** Because favourites.bin lives in a shared OPL config directory, RiptOPL permanently ships a read-only importer for uOPL/wOPL's header-less raw-struct format — including a mode-number translation table for a foreign enum, salvage/malformed rules, and a "recognised but not editable" state that the rest of the store must honour. This is migration code with no retirement condition: it runs on every load whose header is not OFAV (including a merely corrupt RiptOPL file, which then gets interpreted as a foreign list), forever.
- **Fix:** Make the import a one-time conversion: when a foreign file is imported and the store is next saved, it is already written as OFAV — so add the missing half: after a successful import, immediately rewrite the file in OFAV format (when the directory is writable) and delete the foreign parser once that conversion path has shipped for a release. Short of that, gate the import behind one explicit "import favourites from another loader" action instead of the every-load fallback.
- **Benefit:** Removes ~90 lines of foreign-format parsing and one ambiguous state (corrupt-native vs foreign) from every favourites load; the shared-directory behaviour users rely on is preserved by the conversion.
- **Effort:** M · **Risk:** Med (a user who actively shares one favourites.bin between RiptOPL and uOPL/wOPL in both directions would lose the return leg — the file already becomes OFAV on any RiptOPL save today, so that workflow is already one-way in practice) · **Confidence:** High — READ; the "already one-way in practice" clause is INFERRED from `favWriteFile` always stamping the OFAV format (favsupport.cpp:425-545).

---

## RE-RIPTOPL-205 — Launch diagnostics are switched off by a hardcoded header constant, not a build option (filed as W04-015)

- **Category:** 8 (half-finished/redundant features)
- **Locations:** `RiptOPL/include/launchdiag.h:41` (`#define LAUNCH_DIAG 0`); consumers `RiptOPL/elfldr/loader.c` and `RiptOPL/src/platform/launchdiag.cpp` (grep-verified: 3 use sites outside the header)
- **Problem:** A complete diagnostic system — staged background-colour markers plus a counted white-flash refusal code for failed game handoffs, documented at length in the header — is compiled out in the shipped tree because the toggle is a literal `0` in the header. Every other optional feature in this tree is gated by the build (e.g. the flavour flags in `ee_core/Makefile`: IGS, RETROACHIEVEMENTS, GSM1080P, PADEMU). Enabling launch diagnostics requires editing a source header, so in practice the code is dead in every normal build and rots untested.
- **Fix:** Either gate it from the Makefile like the other flavours (`-DLAUNCH_DIAG=1` opt-in, header keeps `#ifndef` default 0), or — if nobody uses it — delete the marker calls and the refusal-flash machinery. Keeping a hand-edited header constant is the worst of both.
- **Benefit:** The feature becomes reachable through the normal build switch, or the dead machinery stops costing maintenance.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (header and loader read in full; use-site count from grep).

## RE-RIPTOPL-206 — httpclient carries the scaffolding of chunked transfer encoding that was never implemented (filed as W06-018)

- **Category:** 8 (half-finished features) / 1 (dead code)
- **Locations:** `RiptOPL/modules/network/httpclient/httpclient.c:130` (`enum TRANFER_ENCODING`), `:160` (Transfer-Encoding handling)
- **Problem:** The header parser recognises `Transfer-Encoding:` and there is an enum for encodings, but chunked decoding was never implemented — responses are read purely by `Content-Length`. The dead scaffolding suggests chunked support exists when it does not; a chunked-only server silently yields a wrong/empty body rather than an error. READ.
- **Fix:** Delete the unused enum and the inert header branch, and (the only functional change) fail explicitly when a response is chunked instead of misreading it. Implementing chunked decoding is the "more" option and is not recommended without a concrete server that needs it.
- **Benefit:** The code stops advertising a capability it doesn't have; failure becomes loud instead of silent.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-207 — xhdd: HDPro transfer-mode stub reports success for doing nothing; two header prototypes have no definition (filed as W09-009)

**Category:** 8 half-finished feature / 6 bugs
**Locations:**
- RiptOPL/modules/hdd/xhdd/hdpro.c:8-11 (`hdproata_device_set_transfer_mode` returns 0 unconditionally, empty body)
- RiptOPL/modules/hdd/xhdd/xhdd.c:43-48 (devctl `ATA_DEVCTL_SET_TRANSFER_MODE` returns that 0 to the EE when `isHDPro`)
- RiptOPL/modules/hdd/xhdd/xhdd.h:2, 4 (`ata_device_set_write_cache`, `hdproata_device_set_write_cache` declared; neither is defined anywhere in modules/hdd/xhdd — the definitions with the first name live in the unrelated cdvdman module)

**Problem:** On an HDPro setup the EE is told the transfer mode was set when nothing was programmed — a silent no-op success is worse than an error, because the caller proceeds on a false assumption about the bus mode. The header also advertises two write-cache functions this module does not contain.
**Fix:** Return a negative "not supported" code from the HDPro stub (and let the devctl propagate it), and delete the two orphan prototypes from xhdd.h.
**Benefit:** Honest failure instead of silent misconfiguration on HDPro; header matches the module.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-208 — usbd-ra public header declares two exports that do not exist (filed as W09-019)

**Category:** 8 half-finished features
**Locations:**
- RiptOPL/modules/usb/usbd-ra/include/usbd.h:294-296, 336-337 (`sceUsbdGetReportDescriptor`, `sceUsbdMultiIsochronousTransfer` — declared, given import slots 17/18, with the header's own comment "these aren't implemented")
- RiptOPL/modules/usb/usbd-ra/src/exports.tab (export table ends at slot 16, `sceUsbdChangeThreadPriority`)

**Problem:** Any module that imports slots 17/18 against this IRX links against entries the driver never exports. The header documents the gap, but the import macros remain available for a future caller to trip over.
**Fix:** Remove the two declarations and their `I_` import macros from the vendored header as a documented local change (the Makefile already carries one such documented deviation policy), or leave exactly as-is if byte-level vendor purity is preferred over API honesty — but do not leave it unrecorded; add it to the Makefile's deviation note either way.
**Benefit:** The header's contract matches the export table.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ)

---

## RE-RIPTOPL-209 — The mmce "set alarms" setting is honoured by only two of the five transfer paths (filed as W10-026)

- **Category:** 8 (half-finished features)
- **Locations:** `RiptOPL/modules/mmce/mmceman/src/mmce_sio2.c:278,381` (checked in `rx_dma`/`rx_mixed`) vs `:211` (`tx_rx_pio`, always alarms) and `:492,533,570` (`tx_mixed` stages, always alarm); setting exposed via `RiptOPL/modules/mmce/mmceman/src/mmce_fs.c:1157-1160`
- **Problem:** `MMCE_SETTINGS_SET_ALARMS` toggles `mmce_sio2_use_alarm`, but `tx_rx_pio` — the path every command and every filesystem packet goes through — ignores the flag entirely, as do the DMA stages of `tx_mixed`. Turning alarms "off" changes behaviour only for bulk reads.
- **Fix:** Either honour the flag in `tx_rx_pio` and `tx_mixed` too, or delete the setting and its devctl case. LESS favours deleting a setting that cannot do what it says unless a user of the off state is known.
- **Benefit:** The devctl surface stops advertising a control that mostly does nothing.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-210 — PSUManager integration is a claim without a runner: the C++ validator and the vendored tests are never built or run (filed as W11-008)

- Category: 8 half-finished
- Locations: `RiptOPL/third_party/PSUManager/README.md:12-16`; `RiptOPL/third_party/PSUManager/src/core/tests/PsuArchiveTests.cpp`; `RiptOPL/third_party/PSUManager/src/core/CMakeLists.txt:9-13`
- Problem: The README states the independent C++ validator (`.github/scripts/verify_psu_with_psumanager.cpp`, which includes `third_party/PSUManager/src/core/PsuArchive.h`) "loads each save through this vendored core and compares every member byte for byte" — but a repo-wide search finds no workflow, test or script that compiles or runs that validator: `tools/run_tests.sh` runs only `.github/scripts/test_*.py` and the HTTP tests, and no `test_*.py` references it. Likewise the vendored core's own test suite (`PsuArchiveTests.cpp`, wired only into the vendored `CMakeLists.txt` under `BUILD_TESTING`) is never built — nothing in the repo runs CMake on this tree. What actually validates PSUs today is the packager's own Python `inspect_psu` (`.github/scripts/package_release_app.py`), i.e. the same code that wrote them, not the independent implementation the README describes.
- Fix: Either wire the existing validator into `tools/run_tests.sh` (one g++ line against `PsuArchive.cpp`/`PsuDate.cpp`) so the claim becomes true, or delete the orphan validator and the vendored tests and correct the README to say the core is preserved for provenance only. The first option adds one command to an existing runner — no new machinery.
- Justification (for the wiring option): it makes an already-written, already-claimed independent check real; the alternative (deletion) adds nothing and is equally acceptable under LESS-NOT-MORE.
- Benefit: The release PSU validation either genuinely is independent, or the docs stop claiming it is.
- Effort: M · Risk: Low · Confidence: Med (validator and tests READ; "never run" INFERRED from exhaustive grep of workflows, test runners and scripts)

## RE-RIPTOPL-211 — `ps2ip_ShutDown` is exported but does nothing (filed as W05-022)

- **Category:** 8 half-finished features
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/ps2ip.c:273-278`; `RiptOPL/modules/network/SMSTCPIP/exports.tab:4`
- **Problem:** The module's shutdown export simply `return 1;` — no thread termination, no mbox/semaphore cleanup, no netif removal. Anything that unloads or restarts the stack believing this hook works leaks the whole stack state.
- **Fix:** Either implement the teardown or point the export slot at ps2ip_Stub (the slot must stay for ordinal stability) and delete the fake implementation, so the absence is honest.
- **Benefit:** No false contract at the module boundary; ~6 lines and one misleading symbol gone in the LESS variant.
- **Effort:** S (stub variant) · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-212 — Neutrino / custom-loader path resolution is a ~630-line probing subsystem for "find one ELF" (filed as W01-001)

- **Category:** 3 overengineering
- **Locations:** `RiptOPL/src/sources/supportbase.cpp:1969-2590` (the resolver family: `sbNeutrinoInstallComplete` 1969, `sbNeutrinoResolved` 1994, `sbNeutrinoProbeGameDevice` 2027, `sbResolveCustomLoaderPath` 2235-2488, `sbResolveMigratedApaNeutrino` 2489, `sbProbeNeutrinoMc` 2517, `sbResolveMigratedNeutrino` 2539, `sbResolveNeutrinoPath` 2551)
- **Problem:** Resolving one `neutrino.elf` / POPSTARTER path takes ~632 lines (measured: lines 1969-2600). `sbResolveCustomLoaderPath` alone is ~254 lines of per-alias branches (mc, mmce, smb, udpfs, udpbd/udpfsbd/udpfsd, pfs, hdd, rom, typed mass/BDM), each with slot/`?`-wildcard handling; on top of it sit a game-device probe (2 bases × case forms), an 8-candidate memory-card probe, and two "migrated install" resolvers (plain + APA variant) that re-probe the same locations under different spellings. Census: 51 references to the `sbResolveNeutrino*`/`sbNeutrino*` family across the scope. Every new device spelling historically grows another branch here instead of the search space shrinking.
- **Fix:** Cut the search space, not the code around it: keep (a) the user-entered full path used verbatim if the file exists, and (b) one probe of the active game device's prefix. Delete the migrated-install resolvers, the multi-spelling MC probe list, and the per-alias wildcard/slot guessing; a path that does not resolve then reports "not found" with the path the user can fix, which `vcdDescribePopstarterLookup`-style reporting already does for POPSTARTER.
- **Benefit:** Removes several hundred lines of the most branch-dense code in the scope and the whole class of "resolved to the wrong device's copy" behaviour; resolution becomes predictable enough to describe in one sentence.
- **Effort:** L · **Risk:** Med (a layout that only the deleted probing found will stop auto-resolving; the user-entered path remains as the explicit route) · **Confidence:** High — READ (all cited ranges opened; line counts measured).

## RE-RIPTOPL-213 — Boot-directory resolution is a 496-line file with two budget ladders and three transport tiers (filed as W01-013)

- **Category:** 3 overengineering
- **Locations:** `RiptOPL/src/sources/bdmboot.cpp:254-471` (`BdmBoot::resolveWithBudget`), `:472-485` (`resolve` = 3000+10000+3000+8000 ms budgets; `resolveBootstrap` = 1500+5000+1500+3500 ms), `:118-250` (literal-slot resolver and identity reads)
- **Problem:** Finding the folder RiptOPL booted from takes a dedicated file with: a literal massN: resolver, a broader typed-alias resolver, a weak "fallback slot" held across the whole scan and consumed last, transport escalation tiers (silent loads first, then DEV9/ATA with spin-up waits), and two different budget ladders depending on whether the call is "full" or "bootstrap". The full ladder can spend 22 seconds of budgeted waiting (3000+10000+3000+8000 ms, from the call at 475-476) resolving a path that is, in the common case, simply argv[0]'s device.
- **Fix:** Resolve the literal slot named by the boot path, once, with one bounded budget; if that fails, fall back to the legacy discovery the code already retains, instead of escalating through every transport with per-tier budgets. Delete the second ladder and the fallback-slot mechanism.
- **Benefit:** Boot/config-load behaviour becomes explainable and testable; removes most of a 496-line file and a class of "boot hung for ~20 s on the splash" failure shapes (structural statement only — no hardware behaviour is claimed).
- **Effort:** L · **Risk:** Med (exotic boot layouts — typed aliases, bare `mass:` — lose their dedicated search and must use an explicit path) · **Confidence:** High — READ.

## RE-RIPTOPL-214 — A ~360-line asynchronous state machine exists to show a short disc ID in a caption (filed as W01-012)

- **Category:** 3 overengineering
- **Locations:** `RiptOPL/src/sources/vcdsupport.cpp:262-620` (`VCD_ID_PROBE_BUDGET`/`VCD_ID_MEMO_MAX`/`VCD_ID_MAX_RETRIES` at 262-264, the `vcd_id_memo` linked list and `idMemoFind`/`noteScanDir`, `resolveRowId` 413, the one-slot DIntr/EIntr request queue `requestRowId`/`resolveQueuedDisplayId` 487-580, `rowIdCached` 585-620)
- **Problem:** To display `AAAA_NNN.NN` instead of a filename, the code maintains a 512-entry session memo with five states (UNREQUESTED/QUEUED/DEFERRED/RESOLVED/ABSENT), retry counters with frame-based backoff, a per-scan budget of 8 disc opens, a one-slot interrupt-guarded async request, and deferral rules based on whether art or BGM IO is busy — plus a canonicalisation step (`vcdCanonDisplayId`) so disc-read IDs render in the same shape as filename-parsed ones. The zero-IO filename parser (`vcdExtractGameId`) already exists in the same file and covers conventionally named libraries.
- **Fix:** Delete the disc-probing half: keep `vcdExtractGameId` filename parsing as the only display-ID source, and delete the memo, the async request slot, the retry/defer state machine, and `resolveRowId`'s device reads. Libraries whose filenames carry no ID then show the filename — the same fallback the code already uses when its budget runs out.
- **Benefit:** Removes ~360 lines, a linked-list allocation per scanned game, and all device IO from a cosmetic caption path.
- **Effort:** M · **Risk:** Med (a real, user-visible regression for badly named VCD libraries — the issue history in the comments shows at least one reporter with exactly that library; this is a deliberate trade the maintainer should make explicitly) · **Confidence:** High — READ.

## RE-RIPTOPL-215 — MMCE GameID card-switch waiting is six overlapping wait/settle mechanisms (filed as W01-007)

- **Category:** 3 overengineering (with 2 duplication)
- **Locations:** `RiptOPL/src/sources/mmcesupport.cpp:146-353` (`MmceSupport::sendGameID`, with its own start-poll loop), `:314-353` (`waitCardReady`), `:373-415` (`waitSwapDoneOn`), `:417-423` (`waitSwapDone`), `:434-449` (`waitBeforeHandoff`), `:451-498` (`waitHistorySwitch`), `:500-520` (`gameIdSettle`), `:598-630` (`settleAfterSwitch`)
- **Problem:** One physical event — "the SD2PSX card is switching folders; wait until it answers again" — is waited out by at least six distinct functions, each with its own poll interval, silence bound, failure counter, START-press latch handling, and tri-state result convention (`sendGameID` returns 1/0/-1 with "truthy on purpose" semantics its callers must know). The constants alone fill a screen (MMCE_GAMEID_START_MS, …QUERY_FAILURES, …QUERY_SILENT, CARD_READY_MAX_S, CARD_READY_SILENT_MS, HISTORY_SWITCH_WINDOW_MS, GAMEID_WAIT_TICKS, ART_ABORT_WAIT_TICKS). Call sites must compose them correctly (`waitSwapDone` → `settleAfterSwitch` → `waitBeforeHandoff` → `settleAfterSwitch` again appears in the launch legs at 1412-1434 and 1658-1729), and the comments record multiple production hangs caused by composing them in the wrong order or missing one.
- **Fix:** Collapse to two primitives: one "wait until this device answers presence and is not busy, or START/timeout" poll, and one "wait until a directory open on this device succeeds" fs-settle. Rewrite the six entry points as thin calls to those two (or delete the ones whose callers can call the primitives directly — `waitSwapDone` is already a one-line forwarder). Keep the observed behaviours (silence = swapping, START skips) inside the two primitives, once.
- **Benefit:** The card-switch wait becomes auditable in one place; launch legs stop composing 3-4 waits by hand; removes an estimated third of this ~490-line region.
- **Effort:** L · **Risk:** Med (this code guards real, hardware-reported freezes; the consolidation must preserve the silence/START semantics exactly — structural judgement only, no hardware claim is made here) · **Confidence:** High — READ.

## RE-RIPTOPL-216 — Multi-view theme fallback *synthesizes* whole theme views by copying config blocks (filed as W02-010)

- **Category:** 3 overengineering
- **Locations:** `RiptOPL/src/gfx/themeviews.cpp:343-544` (fallback-view synthesis: `thmViewSynthesize`, `thmSynthCopyBlock`, `thmSynthFindTemplate`, family/presenter tables), consumed at `RiptOPL/src/gfx/themes.cpp:1478-1491`
- **Problem:** READ (themeviews.cpp in full). When a theme lacks the requested view, this machinery walks up to 64 slots × 6 families, finds template blocks in a second config set, and writes synthesized `<view>.<family><j>_*` keys back into the theme's own config — a config-rewriting engine (~200 lines plus its scan/parse/inherit layers above it) whose entire purpose is to avoid showing the theme's native view. The simpler behaviour — a theme draws the views it declares; an undeclared view falls back to the native/list view — needs none of it.
- **Fix in words:** Delete the synthesis section and let `thmViewChoose`'s existing default-view fallback be the only fallback. Themes that want a view declare it, as they already must for it to look right.
- **Benefit:** ~200+ lines and an entire config-mutation surface removed; theme loading stops writing keys the theme author never wrote, so theme bugs become reproducible from the theme file alone.
- **Effort:** M · **Risk:** Med · **Confidence:** Med (READ; the user-visible cost — auto-generated views disappear — is INFERRED from the code's stated purpose)

## RE-RIPTOPL-217 — Neutrino TOML sync carries two rewriters plus machinery that exists only to repair its own writes (filed as W03-009)

- **Category:** 3 (overengineering) / 8 (redundant features)
- **Locations:** `RiptOPL/src/platform/sysneutrino.cpp:303` (`sysTomlTornTailCut`), `RiptOPL/src/platform/sysneutrino.cpp:382` (`sysSyncNeutrinoUdpfsToml`), `RiptOPL/src/platform/sysneutrino.cpp:522` (`sysTomlRewriteSmbArgs`)
- **Problem:** The same job — updating Neutrino's TOML config in place — is done two different ways in one file: a naive `strstr(toml, "\"ip=")` string patch for the UDPFS sync, and a full TOML lexer/rewriter for the SMB args. On top of that, `sysTomlTornTailCut` (~90 lines) exists solely to detect and cut file tails torn by this same code's in-place truncate-and-rewrite strategy, and both sync paths carry a restore-original-on-short-write fallback. A meaningful share of this subsystem is self-inflicted: complexity spent surviving a write strategy, duplicated across two implementations of the edit itself.
- **Fix:** One writer, one strategy: route both syncs through the existing lexer-based rewriter and write the result in one pass. Once writes are single-pass from a complete in-memory buffer, the torn-tail repair and the duplicate restore logic have nothing left to repair and can be deleted.
- **Benefit:** Removes an entire repair subsystem and a second TOML implementation; the remaining path is the one that already parses properly.
- **Effort:** L · **Risk:** Med (touches launch config writes; needs on-hardware launch checks per repo rules) · **Confidence:** High (READ)

## RE-RIPTOPL-218 — Settings-home discovery is a second, larger fallback labyrinth — and save-side discovery disagrees with load-side (filed as W03-017)

- **Category:** 3 (overengineering) / 8 (redundant features)
- **Locations:** `RiptOPL/src/app/settingsio.cpp:938` (`tryAlternateDevice`), `RiptOPL/src/app/settingsio.cpp:853` (`tryMissingConfigPathRecovery`), `RiptOPL/src/app/settingsio.cpp:743` (`tryKnownBootConfigRecovery`), `RiptOPL/src/app/settingsio.cpp:2047` (`trySaveAlternateDevice`)
- **Problem:** When the boot home yields no config, load-side discovery cascades through: the config.path redirect, an APA-only chain, a known-boot-device recovery (two legacy locations on the boot device, with per-set re-reads and re-homing), then a broad recovery sweep (both MC slots × two locations, USB slots × two locations, MMCE slots), then a legacy mc?:OPL read with self-migration logic that itself branches on deferred/network boots. Save-side discovery (`trySaveAlternateDevice`) is a *separate* cascade with a *different* device order (boot device, MC, MMCE, BDM, HDD vs. load's MC, USB, MMCE, HDD). Each layer is individually justified in its comments by a field report; collectively they mean the settings home is decided by ~1,500 lines of branching state (boot class × device presence × which probe hit), and load and save can pick different homes for the same machine state.
- **Fix:** Collapse to one decision table used by both directions: (1) explicit custom path, (2) boot home, (3) the single legacy mc?:OPL migration read, (4) defaults homed at the boot home. Delete the broad multi-device sweep and the per-set recovery re-reads; a config on some *other* device than the boot device is what the custom-path setting exists for.
- **Benefit:** Load and save agree by construction; the "settings saved but never load back" class of bug (which the comments show recurring) loses its main mechanism.
- **Effort:** L · **Risk:** High if done casually — this is the most field-scarred code in scope; it should only move with a hardware test matrix per boot class, per repo rules · **Confidence:** High that the sprawl and the load/save order divergence exist (READ); the four-step table is a recommendation, INFERRED as sufficient

## RE-RIPTOPL-219 — `configWrite` stacks three separate save-safety mechanisms (filed as W03-015)

- **Category:** 3 (overengineering)
- **Locations:** `RiptOPL/src/core/config.cpp:1496-1699` (`CONFIG_MAX_RESTORE_BYTES` at line 1496, `configWrite` at line 1498)
- **Problem:** A single save currently goes through three independent safety layers: a RAM snapshot-and-restore of the previous file (capped at 256 KB by `CONFIG_MAX_RESTORE_BYTES`), a read-back byte-compare with a rescue path, and a special single-open carve-out for memory cards — with per-device branching woven through them. The comments tie the layers to real incidents (issues #184/#245/#340), so this is not speculative machinery; but three overlapping mechanisms means three behaviours to reason about per device, and the snapshot layer alone can allocate up to 256 KB transiently on a 32 MB machine to protect a settings file.
- **Fix:** Consolidate to one mechanism — the read-back compare is the only one that verifies what actually landed — and delete the snapshot-restore layer and the device carve-outs it forced. This is a deliberate, hardware-test-first change, not a drive-by: if hardware shows a device genuinely needs its carve-out, that device keeps exactly one, documented.
- **Benefit:** One save path to validate; up to 256 KB of transient allocation gone; save failures have one meaning.
- **Effort:** M · **Risk:** Med (hardware-motivated history; repo rules require real-hardware validation before calling this done) · **Confidence:** High that the three layers coexist (READ); Med that one suffices (INFERRED — needs hardware evidence)

## RE-RIPTOPL-220 — host.cpp builds a four-interface service layer with exactly one implementation of each interface (filed as W03-023)

- **Category:** 3 (overengineering)
- **Locations:** `RiptOPL/src/app/host.cpp:26` (`OplUi`), `:74` (`OplApp`), `:98` (`OplLibrary`), `:115` (`OplLaunch`) — file is 292 lines
- **Problem:** `UiServices`, `AppServices`, `LibraryServices` and `LaunchServices` are abstract interfaces whose implementations (grep-verified across `src/` and `include/`) exist exactly once each, all in this file, and almost every method is a one-line forward to a free function that already existed (`guiMsgBox`, `::deinit`, `libViewActive`, …). The vtable layer buys nothing a direct call doesn't: there is no second implementation, no test double in-tree, and the forwarding bodies must be touched alongside every signature change anyway. INFERRED: if the seam exists to break a circular dependency, that is not visible from the code in scope.
- **Fix:** If no second implementation is planned, delete the interface layer and call the free functions directly; keep the `host` namespace only where it genuinely hides a dependency cycle, and say so in a comment. If a second implementation *is* planned, this finding is void — but then the plan should be written down where the next reader can find it.
- **Benefit:** ~290 lines of forwarding and four interface headers' worth of indirection removed from every call path that crosses the app/support boundary.
- **Effort:** M · **Risk:** Med (call sites are numerous) · **Confidence:** Med (single-implementation fact is grep-verified; whether the seam is load-bearing for the build graph is INFERRED)

## RE-RIPTOPL-221 — Xerabora mode carries a hand-rolled JSON parser and maximum-size buffers for a paged list (filed as W01-009)

- **Category:** 3 overengineering (with 4 size/memory)
- **Locations:** `RiptOPL/src/sources/achievements.cpp:137-150` (Xerabora block, `XERA_DOC_MAX = 512 * 1024`, `XERA_LIST_MAX = 1024`), `:182-350` (`jsonString`/`jsonSkip`/`jsonObject` and the member callbacks), `:352-445` (`achievementsParseXerabora` / `achievementsXeraboraPage`, `malloc(XERA_DOC_MAX)` at 427)
- **Problem:** This is the only JSON parser in the entire scope (census: `json` appears in no other `src/sources/` file) — ~170 lines of hand-written JSON tokenising maintained for one PC-side service's responses. Each fetch allocates a fixed 512 KiB document buffer and the parse allocates a fixed 1024-entry list, regardless of the actual set size, because the design parses the whole document into a cached list and serves pages from it.
- **Fix:** Parse the document streaming, page by page, the way the Caduceus path in the same file already consumes its TSV pages (`ACH_PAGE_SIZE` at a time), and delete the JSON object-walker plus the two fixed maximum allocations. If Xerabora mode itself is no longer used, the simpler fix is to delete the mode (lines 137-445 and its dispatch in `loadXerabora`) outright.
- **Benefit:** Removes the scope's only JSON parser and up to ~512 KiB + 1024 entries of per-fetch allocation; one RA list-consumption shape instead of two.
- **Effort:** M · **Risk:** Med (Xerabora page/filter behaviour must be preserved if the mode stays) · **Confidence:** High — READ.

## RE-RIPTOPL-222 — UDPFS art lookup maintains an 8-spelling case-discovery subsystem for one folder name (filed as W01-015)

- **Category:** 3 overengineering
- **Locations:** `RiptOPL/src/sources/udpfssupport.cpp:58-66` (`udpfsArtCaseNames[8]` = ART, ARt, ArT, Art, aRT, aRt, arT, art), `:460-580` (`udpfsArtFolderHasFiles`, `udpfsDiscoverArtFolder`), `:1204-1220` (`UdpfsSource::image` using the resolved index)
- **Problem:** Because a case-sensitive server distinguishes `ART` from `art`, the backend enumerates all 8 capitalisations, lists the root, opens candidates to see which are populated, arbitrates between an empty auto-created `ART` and a populated `art`, and invalidates the art-failure memo and art index when the answer lands — ~120 lines plus a per-session state flag, for the spelling of a three-letter folder. The same file already solves the identical problem for THM/LNG more simply (list the root, accept any case variant found, udpfssupport.cpp:380-460).
- **Fix:** Treat ART like THM/LNG in the same file: list the root once, take the first case-insensitive `ART` match that opens, and remember its spelling. Delete the 8-entry table, the populated-vs-empty arbitration, and `udpfsArtFolderHasFiles`.
- **Benefit:** One case-handling pattern in the file instead of two; ~80 lines removed; the pathological both-spellings-exist case degrades to "first match wins", as it already does for themes and languages.
- **Effort:** M · **Risk:** Low · **Confidence:** High — READ.

## RE-RIPTOPL-223 — Cheat loading stacks four fallback attempts per launch (filed as W01-017)

- **Category:** 3 overengineering (fallback stacking)
- **Locations:** `RiptOPL/src/sources/supportbase.cpp:1741-1845` (`sbLoadCheats`); tar probe at 1755-1790, loose-file fallback after it, case retries described and implemented at 1796-1845
- **Problem:** One launch's cheat lookup tries, in order: a member of `CHT/cht.tar`, the loose `CHT/<id>.cht`, the same name in lowercase, and the same name in UPPERCASE — each attempt is a device open (and the tar attempt is an archive open + member scan) on whatever slow transport hosts the game, on the launch path, for a feature most games do not use (no `.cht` at all is, by the code's own comment at 1678, "the expected case"). Every attempt also has its own logging and its own "found but broken vs absent" distinction that the next attempt must not shadow.
- **Fix:** Keep the tar (it is the documented bulk format) and one loose-file lookup using the exact stored startup spelling; delete the two case-variant retries and tell users the file must match the game ID's case, as the ID itself is canonical uppercase per the comment at 1766.
- **Benefit:** Up to three fewer device round-trips on every cheat-enabled launch and a materially simpler absence/broken distinction; ~40 lines removed.
- **Effort:** S · **Risk:** Low (a user with a mis-cased loose `.cht` and no tar loses cheats until they rename the file) · **Confidence:** High — READ.

## RE-RIPTOPL-224 — libview: a two-ring, pending/commit state machine to remember which list a page shows (filed as W02-011)

- **Category:** 3 overengineering
- **Locations:** `RiptOPL/src/ui/libview.cpp:1-365` (whole file)
- **Problem:** READ in full. Remembering "PS2 or PS1 list per page" is implemented as: two parallel rings (`retainedView`, `mixedView` + `mixedViewInitialized`), per-BDM-slot *and* per-BDM-type entries (`LIB_VIEW_ENTRIES`, `bdmSlotType`), a staged pending view with its own "uses mixed ring" flag (`pendingView`, `pendingViewValid`, `pendingViewUsesMixedRing`), dirty flags, and a one-char-per-mode config encoding with two keys. Each concept is individually defended in comments, but the total state space (which ring? staged or committed? slot or type entry?) is what a reader must hold to change anything here.
- **Fix in words:** Collapse to one stored position per mode (the BDM-type seeding can stay as an initial value copied into the slot entry at attach, which `libViewBdmAttach` already effectively does), and commit a staged view by simply assigning it — delete the second ring and the pending-uses-which-ring flag. If Mixed truly needs a distinct start position, that is one extra byte per mode, not a parallel ring with its own initialized flags.
- **Benefit:** Roughly a third of the file's state variables disappear; the L3 toggle path becomes assign-and-rebuild.
- **Effort:** L · **Risk:** Med · **Confidence:** Med (READ; simplification shape is INFERRED)

## RE-RIPTOPL-225 — One-line C forwarding wrappers layered over every C++ class split (filed as W02-009)

- **Category:** 3 overengineering
- **Locations:** `RiptOPL/src/ui/gui.cpp:470-492` (25 wrappers in this file alone, e.g. `guiLock`→`gGui.lock()`), plus the same tail block in `RiptOPL/src/ui/guiscreens.cpp:99-109`, `RiptOPL/src/ui/guinotify.cpp:205-218`, `RiptOPL/src/ui/guisplash.cpp:245-266`, `RiptOPL/src/ui/cfgtitle.cpp:269-274`, `RiptOPL/src/gfx/artindex.cpp:26-44`
- **Problem:** READ. Each "Split out of gui.cpp/texcache.cpp" refactor introduced a class (`Gui`, `ScreenManager`, `Notifications`, `BootSplash`, `CfgTitles`, `ArtIndex`) and then re-exported every method as a free C function that does nothing but forward. Callers use the free functions; the classes are never used directly outside their own file. The class layer therefore buys no encapsulation — it is a second API surface that must be kept in sync by hand (declaration in a header, method, wrapper) for every operation.
- **Fix in words:** Pick one surface per subsystem and delete the other: either call the global object's methods directly at call sites and delete the wrappers, or flatten the classes back to free functions and delete the class. Do it per-file as files are touched, not as a big-bang rewrite.
- **Benefit:** One declaration + one definition per operation instead of three; removes a whole class of "wrapper exists, method doesn't" (and vice-versa) drift.
- **Effort:** L (total; S per file) · **Risk:** Med · **Confidence:** High (READ)

## RE-RIPTOPL-226 — `pcdiag_cf_probe`'s sampling loop produces only traces the production caller throws away (filed as W10-030)

- **Category:** 3 (overengineering) / 5 (performance, structural)
- **Locations:** `RiptOPL/modules/pccf/pcdiag_cf.c:40-71`; production caller `RiptOPL/modules/pccf/pccf_hw.c:56-59,283` (passes `quiet_trace`)
- **Problem:** The probe's actual decision is complete before the loop: candidate check and COR check at the top determine pass/fail. The remaining loop — 3 samples × 2 spaces × (10 byte reads + 5 word reads), plus 500 ms + 200 ms of waits — feeds only `trace()` calls, never influences the return value, and in the production driver the trace callback is `quiet_trace`, which discards everything by design. Every card insertion pays ~700 ms and 90 register reads for output nobody receives.
- **Fix:** In the production path keep the candidate/COR validation and drop the sampling loop (the loop belongs in the separate diagnostic build the file header describes).
- **Benefit:** Removes ~700 ms of start-up latency per card and ~30 lines of inert diagnostic code from the shipped driver.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-227 — Grid / PSBBN / Coverflow presenters each re-walk the whole game list per frame and per keypress (filed as W02-012)

- **Category:** 5 performance (structural)
- **Locations:** `RiptOPL/src/gfx/grid.cpp:504-510` (draw: count/index walk), `RiptOPL/src/gfx/psbbnflow.cpp:252-256` (draw: countRows walk), `RiptOPL/src/ui/menusys.cpp` `menuGridRow`/`menuGridPage`/`menuGridAtRowStart` (each walks `submenu` head→current to recompute count+index), plus the wrap-walk window builders in `RiptOPL/src/gfx/coverflow.cpp` (drawCoverFlow) and `RiptOPL/src/gfx/psbbnflow.cpp` (tail builder)
- **Problem:** READ. The submenu is a singly-linked list with no stored count or index. Every Grid frame walks it once just to count; every grid keypress walks it again in menusys (up to three walks for one key: at-row-start check, row step, page step); PSBBN Flow walks it per frame for its `n/N` counter. Cost is O(list length) per frame/keypress on the GUI thread, growing with library size, for values that change only when the list is rebuilt or the cursor moves one step.
- **Fix in words:** Store count and current index where the list is already maintained (append/destroy/cursor move in menusys) and have the three presenters read them instead of recounting. No new subsystem — two integers on the existing menu item.
- **Justification (addition):** Two cached integers, justified by removing an unbounded per-frame walk in three presenters plus three navigation helpers.
- **Benefit:** Frame and navigation cost becomes O(1) in library size for all three views.
- **Effort:** M · **Risk:** Med · **Confidence:** High (READ — walks verified; per-frame call of the draw functions is from their `drawElem` registration, READ)

## RE-RIPTOPL-228 — Cheat-config save walks 250 key removals; load silently drops every pick after the first gap (filed as W03-031)

- **Category:** 5 (performance, structural) / 6 (bugs)
- **Locations:** `RiptOPL/src/core/cheatconfig.cpp:103` (removal inside the save loop)
- **Problem:** Saving cheat selections loops over all 250 slots calling `configRemoveKey` for each — every call is a walk of the config key list, so a save is 250 list-walks before the writes even start. On load, the reader stops at the first missing `$CheatSelNNN` key, so a sparse key set (exactly what the save side produces after removals) silently drops every selection after the gap.
- **Fix:** On save, remove the `$CheatSel` prefix range in one pass over the key list instead of 250 searches. On load, skip missing indices instead of stopping — or, if stop-at-gap is the intended format contract, have save write a contiguous set; the two sides must agree, and today they don't.
- **Benefit:** Save cost stops scaling with the slot cap; stored selections survive a save/load round-trip.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-229 — `fmChildren` re-lists from scratch on every ×4 growth step (filed as W03-030)

- **Category:** 5 (performance, structural)
- **Locations:** `RiptOPL/src/core/filemgr.cpp:225`
- **Problem:** The directory-listing helper grows its result array by ×4 up to a 65,536-entry cap, and on each growth it re-runs the full directory enumeration (readdir + stat per entry) from the beginning rather than resuming. Structurally that makes listing a large folder quadratic in readdir/stat work, and the cap itself permits an allocation on the order of 9 MB for one folder listing on a 32 MB console (entry size × 65,536; INFERRED from the struct and cap as read).
- **Fix:** Enumerate once into the final pass (count first, or append without restarting), and lower the cap to something a real folder on a PS2 device can plausibly hold. No new machinery — the single-pass shape is the simpler one.
- **Benefit:** Large-folder listings stop re-statting the same entries repeatedly; worst-case allocation shrinks with the cap.
- **Effort:** M · **Risk:** Low · **Confidence:** Med — the restart-on-growth structure is READ; the ~9 MB figure is INFERRED arithmetic from read values

## RE-RIPTOPL-230 — All Games gathering walks every source's full list twice per rebuild (filed as W01-016)

- **Category:** 5 performance (structural)
- **Locations:** `RiptOPL/src/sources/favsupport.cpp:1747-1762` (`FavSupport::gatherAllGames`), `:1621-1705` (`gatherPass`), `:1584-1586` (`kAllGamesModes`, 15 modes)
- **Problem:** `gatherAllGames` calls `gatherPass(NULL, 0, shelf)` to count, allocates, then calls `gatherPass(rows, cap, shelf)` to fill — so every rebuild performs the entire traversal twice: 15 modes × forced ISO and PS1 views × per-row `name()`/`favResolveStoredId` calls, including the ETH resolver path. The traversal is pure in-memory work over already-loaded lists, but it runs on the IO worker during a menu rebuild, doubled for no benefit beyond knowing the count first.
- **Fix:** Single pass into a realloc-grown array (the grow pattern already used in hddsupport.cpp's list builder), then publish. Delete the counting pass.
- **Benefit:** Halves the All Games rebuild work; ~5 lines net removed and one fewer full traversal of every source list.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ.

## RE-RIPTOPL-231 — cdvdman S-command path busy-spins on hardware registers and retries up to 2500 times (filed as W07-017)

- **Category:** 5 performance (structural) / 6 bugs
- **Locations:** RiptOPL/modules/iopcore/cdvdman/cdvdman.c:954-956 (`while (CDVDreg_SDATAIN & 0x80) ;`), :982-998 (`cdvdman_sendSCmd` retry loop, `retryCount <= 2500` with `DelayThread(2000)` per retry)
- **Problem:** `cdvdman_writeSCmd()` contains three register-polling loops with no delay and no bound; if the mechacon never sets the expected bits, the calling thread spins at full speed forever, holding `cdvdman_scmdsema`. The wrapper then retries the whole command up to 2500 times (~5 s of delays) before returning — and returns 1 (success) regardless of whether any attempt worked. READ. (Structural claim only; no hardware behaviour asserted.)
- **Fix in words:** Bound the register polls with the same delay-and-count pattern the retry wrapper already uses, and return the last attempt's result instead of unconditional 1.
- **Benefit:** A wedged S-command path fails visibly instead of spinning with the semaphore held; callers stop being told a failed command succeeded.
- **Effort:** M · **Risk:** Med (S-command timing is hardware-facing; the bound must stay generous) · **Confidence:** High for the code structure (READ); the failure mode on real hardware is INFERRED

## RE-RIPTOPL-232 — usbmass_bd SCSI: a failing sector is retried 16 times back-to-back, each retry through full USB reset recovery (filed as W09-013)

**Category:** 5 performance (structural)
**Locations:**
- RiptOPL/modules/usb/usbmass_bd/src/scsi.c:14 (`SCSI_MAX_RETRIES 16`), :310-330 (`scsi_read` retry loop), :356-375 (`scsi_write` retry loop)
- RiptOPL/modules/usb/usbmass_bd/src/usb_mass.c:317-340 (`usb_bulk_manage_status` performs reset recovery on a failed status)

**Problem:** Neither retry loop has a delay, a reset of its own, or an early exit for permanent errors; each failed attempt already triggers bulk reset recovery inside the status path. A dead or removed drive therefore burns 16 full command+reset cycles per chunk before reporting `-EIO`, on the BDM caller's thread. (Structural observation only — no timing/hardware claim.)
**Fix:** Reduce the loops to a small number of attempts (e.g. 3) — the command layer below already does recovery per attempt; no new retry machinery.
**Benefit:** Failing USB I/O returns in a fraction of the attempts; less bus churn against a drive that is already erroring.
**Effort:** S · **Risk:** Low · **Confidence:** Med (READ for the loop structure; INFERRED that transient errors needing >3 attempts are rarer than the stall cost)

## RE-RIPTOPL-233 — Block cache keys blocks by unaligned request sector, defeating its own geometry (filed as W08-016)

- Category: 5 performance (structural)
- Locations: `RiptOPL/modules/bdm/libbdm/src/bd_cache.c:88-171`
- Problem: READ. On a miss, `_read` refills 8 sectors starting at the **requested** sector and publishes the slot under that unaligned start (`c->sector[blkidx_best] = sector`). `_contains`/`_overlaps` then only match requests fully inside that arbitrary window. Sequential-but-offset access patterns (e.g. reads at 1, 9, 17… vs 0, 8, 16…) populate overlapping duplicate blocks, evict each other, and halve (or worse) the effective 128 KiB cache — on the driver every FAT/exFAT device reads metadata through. This is a structural claim only; no hardware behaviour is asserted.
- Fix: Align the refill and the slot key down to an 8-sector boundary (`sector & ~7`) and serve the request from the offset inside the block — the offset arithmetic already exists in the hit path.
- Benefit: One canonical block per 8-sector window; cache hit rate stops depending on caller alignment.
- Effort: S · Risk: Med (touches the most safety-critical file in the set; the existing host test for this cache should be extended, not bypassed) · Confidence: High (READ)

## RE-RIPTOPL-234 — smap_udpbd's transmit holds its semaphore across the whole retry sleep loop (filed as W06-020)

- **Category:** 5 (performance, structural only)
- **Locations:** `RiptOPL/modules/network/smap_udpbd/src/xfer.c:201-211`
- **Problem:** `smap_transmit()` takes `tx_sema` at entry and holds it across its `DelayThread(100)` retry loop, so one stalled transmit serialises every other sender for the full retry duration. The sibling implementation in `udpfs_smap/src/xfer.c` releases and re-acquires per attempt. This is a structural observation about lock scope in code READ in both files; no hardware behaviour is claimed.
- **Fix:** Match the sibling: drop the semaphore around the sleep and re-acquire for each transmit attempt.
- **Benefit:** A single slow frame stops blocking unrelated transmits in the udpbd stack.
- **Effort:** S · **Risk:** Med (concurrency change in a driver; needs on-hardware validation) · **Confidence:** Med (READ, structural)

## RE-RIPTOPL-235 — `sysShutdownDev9` retries in a delay-free spin (filed as W03-007)

- **Category:** 5 (performance, structural)
- **Locations:** `RiptOPL/src/platform/system.cpp:264-285` (retry loop at line 278)
- **Problem:** The DEV9-off path loops `while (retry-- > 0 && fileXioDevctl("dev9x:", DDIOC_OFF, ...) < 0)` with no delay or yield between attempts. Structurally, a failing devctl is retried at full speed for the whole retry budget, burning the thread for no benefit — the callee's state cannot change faster because we yielded.
- **Fix:** Add the same small bounded delay the codebase already uses in its other bounded waits (e.g. pad.cpp's poll delays), or cut the retry count. No new mechanism.
- **Benefit:** The shutdown path stops busy-spinning; behaviour otherwise identical.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-236 — Debug art HUD builds a ~35-field formatted string every frame (filed as W02-018)

- **Category:** 5 performance (structural) / 4 size
- **Locations:** `RiptOPL/src/ui/guioverlay.cpp` (`Overlays::drawArtHud`, the `artdbg[224]` snprintf and its ~60-line explanatory comment block)
- **Problem:** READ. When debug is enabled, every frame gathers ~15 diagnostic counters from five subsystems and formats a 224-byte, ~35-conversion snprintf, solely to draw one line. The comment block explaining the fields is longer than the code. This is debug-only, so the cost is bounded to debug use — but it sits in the always-compiled overlay path and its field list has accreted one incident at a time (each field's comment cites a different past bug).
- **Fix in words:** Format the HUD at a reduced cadence (e.g. every 15th frame — the values are human-read diagnostics) or split it into two shorter lines formatted on alternate frames. Do not add fields without removing one.
- **Benefit:** Debug builds stop paying a full multi-subsystem format on every frame; the HUD stays available for the diagnostics it exists for.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RE-RIPTOPL-237 — Boot-splash glow redraws the logo 13 times per frame (filed as W02-020)

- **Category:** 5 performance (structural)
- **Locations:** `RiptOPL/src/ui/guisplash.cpp:36-62` (`BootSplash::drawLogo`)
- **Problem:** READ. The halo effect is 8 inner taps + 4 outer taps + the logo itself = 13 full-logo pixmap draws every frame for the whole boot splash, to produce a blur that a single pre-tinted, slightly larger draw cannot exactly match but that costs 13× the texture submission of the logo on the weakest part of the boot path. This is a judgement call, not a defect: the effect is deliberate and documented.
- **Fix in words:** If boot-frame cost matters, drop the 4 outer taps (weakest contribution, half alpha) and keep the 8 inner taps; otherwise delete nothing and close this as accepted cost.
- **Benefit:** ~30% fewer splash pixmap submissions if applied; zero if the effect is valued as-is.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ; visual impact is INFERRED — needs eyes on hardware)

---

## RE-RIPTOPL-238 — Select event machinery runs on every netconn event in builds that have no select (filed as W05-024)

- **Category:** 5 performance (structural) / 1 dead code
- **Locations:** `RiptOPL/modules/network/SMSTCPIP/sockets.c:812-889` (event_callback)
- **Problem:** event_callback walks the select_cb_list and lazily creates the select semaphore on netconn events, but lwip_select exists only under FULL_LWIP (W05-003) and its export slot is stubbed (W05-002), so in every build that actually ships the list is always empty and the semaphore creation is pure overhead on the event path. (Structural observation only; no timing claimed.)
- **Fix:** Gate event_callback's list walk/semaphore creation on the same condition that provides lwip_select, or delete it together with W05-003.
- **Benefit:** The per-event callback becomes a no-op stub in shipped builds instead of list/semaphore bookkeeping for a feature that cannot be called.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ)

## RE-RIPTOPL-239 — 57 second-half test files (12,240 lines) are referenced by no workflow, and three `tests/` suites run only through them / through a runner no workflow invokes (filed as W13-001)

> **WITHDRAWN (Stage B):** The 57 files do run in CI — ps2build.yml:71 runs `tools/run_tests.sh -k`, which globs every `.github/scripts/test_*.py`; only the narrow residue survives, that `tests/smb2-live/run.sh` is invoked by no workflow or runner.

**Category:** 7 (build/CI/repo clutter), with 8 (half-finished features) for the `tests/` suites
**Locations:**
- Unwired scripts (verified by script: every `.github/workflows/*.yml` was concatenated and each filename, plus the two glob patterns in use — `test_ripps2_*.py` and `test_ilink_*.py` — was matched against the second-half list; 57 test files matched nothing):
  `RiptOPL/.github/scripts/test_netblock_oplcore.py`, `test_neutrino_cheats.py`, `test_neutrino_pademu.py`, `test_neutrino_ra.py`, `test_neutrino_toml_repair.py`, `test_osd_defaults.py`, `test_pad_handoff.py`, `test_pccf.py`, `test_pfs_vmc_inode.py`, `test_ps1_retrogem_metadata.py`, `test_ps1_save_icons.py`, `test_ps2ips_dns.py`, `test_ps2ips_recv_tail.py`, `test_psbbn_flow.py`, `test_psu_timestamps.py`, `test_ra_badge_rows.py`, `test_ra_cards_setting.py`, `test_ra_chunk_integrity.py`, `test_ra_client_mode.py`, `test_ra_ingame_events.py`, `test_ra_language_isolation.py`, `test_ra_launch_resources.py`, `test_ra_menu_link.py`, `test_ra_module_startup.py`, `test_ra_ps1.py`, `test_ra_receive_frame.py`, `test_ra_runtime_budget.py`, `test_ra_support_check.py`, `test_ra_upstream_client.py`, `test_ra_xerabora_browser.py`, `test_ratls_host.py`, `test_retrogem_ember.py`, `test_save_icons.py`, `test_screen_pos_per_mode.py`, `test_selfupdate_host.py`, `test_sfx_oneshot_cut.py`, `test_smap_tx_base.py`, `test_smb2_menu_wait_bounds.py`, `test_smb_vmc_io_result.py`, `test_smbman_ra_write_count.py`, `test_theme_metadata.py`, `test_title_compat_defaults.py`, `test_udpbd_read_only.py`, `test_udpfs_files_oplcore.py`, `test_udpfs_languages.py`, `test_udpfs_network_hardening.py`, `test_udpfs_smap_fifo_tail.py`, `test_ulcfg_rebuild.py`, `test_vmc_contiguity.py`, `test_vmc_fatfs_images.py`, `test_vmc_pfs_reserve.py`, `test_vmc_rebuild.py`, `test_vmc_rebuild_fatfs.py`, `test_warning_gate.py`, `test_yaopl_ripps2_b125_ports.py`, `test_zso_probe_fd.py`, `test_zso_validation.py` (all under `RiptOPL/.github/scripts/`)
- Whole clusters inside that list: **15 of the RetroAchievements tests** (`test_ra_*` above; the wired RA tests are only `test_ra_disc.py`, `test_ra_mode6.py`, `test_ra_sender.py`), **7 VMC tests**, and the only drivers for three `tests/` suites:
- `RiptOPL/tests/pccf/test_ata_rw.c`, `RiptOPL/tests/pccf/test_bd.c`, `RiptOPL/tests/pccf/test_profile.c` (plus `RiptOPL/tests/pccf/stubs/*.h`) — compiled and run only by the unwired `test_pccf.py`.
- `RiptOPL/tests/ratls/host_main.c` (334 lines) — driven only by the unwired `test_ratls_host.py`. `RiptOPL/docs/CHECK-FOR-UPDATES.md:57` describes the TLS download as "Host-tested: … (`.github/scripts/test_ratls_host.py`)", i.e. the docs present it as running coverage.
- `RiptOPL/tests/smb2-live/browse.c` (258 lines), `RiptOPL/tests/smb2-live/keepalive.c` (407 lines), `RiptOPL/tests/smb2-live/readreply.c` (163 lines) — exercised only by `RiptOPL/tests/smb2-live/run.sh`, which no workflow invokes (workflows use only `tests/smb2-live/utf16.c` directly, `tests/smb2-live/launchname.c` via `test_smb_launch_name.py`, `tests/smb2-live/ingame.c` via `test_smb2_logon_parity.py`, and `tests/smb2-live/longname.c` via `tests/nfs-live/run.sh` for the NFS driver only). `run.sh:53-55` exits 2 before building anything if the PS2SDK `ps2smb.h`, `smbd`, or `timeout` is missing, and its header says it must be run as root — so even the no-server case, `readreply.c` (hand-built replies on a socketpair, `run.sh:233`), is gated behind a Samba install and an SDK checkout.

**Problem:** READ (workflow census by script; every listed file run by hand and passing in `/tmp/w13repo`). These are not stubs: several are among the largest and most thorough suites in the half (e.g. `test_smb_vmc_io_result.py` and `test_vmc_rebuild.py` at ~700/600 lines, `test_ra_ps1.py` at 593 lines, `test_ratls_host.py` driving real TLS 1.2/1.3 handshakes against local servers). Because the wired suites are enumerated file-by-file in `flavours.yml` (100+ individual `python3 .github/scripts/test_*.py` lines), any new test that nobody adds to that list is silently inert — which is evidently what happened 57 times. There is no run-all entry point for `.github/scripts` tests at all; the only glob loops in any workflow are the `test_ripps2_*.py` and `test_ilink_*.py` loops in `flavours.yml`.
**Fix in words:** Wire or delete, file by file. The cheapest wiring that cannot silently recur is one loop job in the style the repo already uses for `test_ripps2_*.py` (`for t in .github/scripts/test_*.py; do python3 "$t"; done`, minus an explicit, short exclusion list for suites with real external needs). Split `tests/smb2-live/readreply.c` (and `utf16.c`, already split) out of `run.sh` into the host CI steps, since it needs no server; leave the genuinely live Samba cases in `run.sh` but label it manual-only in its header instead of letting it read as the suite's runner. Any file the maintainer does not want in CI should be deleted rather than kept as a pass-by-inspection artefact.
**Benefit:** 12,240 lines of existing, currently-passing regression coverage start actually guarding the tree — in particular the RA launch/TLS surface and the VMC rebuild surface — and the "forgot to wire it" failure mode closes for future tests.
**Effort:** M (one loop job + exclusion list; the suites themselves already pass)
**Risk:** Med — a loop job will surface environment differences (e.g. suites needing `bs4`/`yaml`/a compiler) as red CI until the exclusion/install list is right; that is the point, but it is a visible transition.
**Confidence:** High — READ (census script + full manual run of every listed file).

---

## RE-RIPTOPL-240 — 18 first-half suites assert on source text only and never execute any code; several pin exact whitespace, so they fail on behaviour-preserving edits and pass on behaviour-breaking ones that keep the strings (filed as W12-005; merged with W13-007)

- Category: 6 bugs/security (test validity)
- Locations (all READ; classified by script — no `subprocess`, assertions only over `read_text()`/regex): `test_about_credits_layout.py`, `test_artindex_network.py`, `test_cdvdman_own_stack.py`, `test_config_null_allocation.py`, `test_counter_unclipped.py`, `test_dialog_gui_lock.py`, `test_dialog_hints.py`, `test_ds34usb_report_transfer.py`, `test_ee_hook_context.py` (see W12-007), `test_error_dialog_render.py`, `test_keyboard_keycaps.py`, `test_korium_builtin_assets.py`, `test_language_page.py`, `test_logo_art_toggle.py`, `test_mbedtls_vendor.py`, `test_menu_crash_catcher.py`, `test_menu_tab_sound_unlocked.py`, `test_mx4sio_host_boot.py`. Worst example READ in full: `RiptOPL/.github/scripts/test_mx4sio_host_boot.py:13-16` asserts literal strings including exact spacing and an embedded `\n` (`'if (gEnableMX4SIO && !mx4sioModLoaded && !oplBootDirIsHostFs())\n        return 1;'`).
- Problem: 106 sibling suites compile the extracted production function and run it; these 18 check that source *looks* a certain way. A clang-format reflow, a renamed local, or an equivalent-expression refactor fails them with no behaviour change; conversely a change that alters runtime behaviour while preserving the pinned substrings passes. They encode source shape as correctness. All 18 currently pass (command-run), which proves only that the strings are present today.
- Fix: for each, either (a) convert to the compile-and-run harness pattern its siblings already use, where the guarded behaviour is executable on the host, or (b) loosen to the semantic token (function called / guard present) where it is not, or (c) delete it where a compile-harness suite already covers the same function. Do not add new text suites of this form.
- Benefit: the suite starts measuring behaviour; refactor noise failures disappear.
- Effort: L · Risk: Med · Confidence: High for the classification and the mx4sio example (READ); INFERRED for which of (a)/(b)/(c) fits each individual suite.

**Merged finding (filed as W13-007 — Whitespace-exact source-text assertions are the *only* coverage for several behavioural claims):**

**Category:** 6 (bugs/security — test validity)
**Locations:**
- `RiptOPL/.github/scripts/test_ra_support_check.py` (whole file, 42 lines) — asserts substrings and index ordering inside `menuCheckSelectedGameSupportLocked`, `menuInitVcdMenu` and `Ranet::askPc`; compiles nothing. (Also unwired — W13-001.)
- `RiptOPL/.github/scripts/test_sfx_oneshot_cut.py:14-27` — slices `SoundEffects::play` by regex and asserts the branch text; compiles nothing.
- `RiptOPL/.github/scripts/test_smap_tx_base.py:28-51` — regex-checks that each SMAP driver's TX-descriptor assignment spells `+ SMAP_TX_BASE`; compiles nothing.
- `RiptOPL/.github/scripts/test_udpfs_network_hardening.py` (whole file, 79 lines) — DEV9 gating and the IP-conflict notice are checked purely as substring presence/ordering, including exact-whitespace strings such as `"sysShutdownDev9();\n        return;"`.
- `RiptOPL/.github/scripts/test_yaopl_ripps2_b125_ports.py` (whole file, 69 lines) — five unrelated ports checked as substring presence, several with embedded newlines/indentation.
- `RiptOPL/.github/scripts/test_title_compat_defaults.py:44-66` — the per-core *routing* half is exact-string matching (e.g. `'if (sourceMode == UDPFS_MODE || bdmSupportIsUDPBD(support) || bdmModeIsUDPBD(sourceMode))\n        return coreChoice != 0;'`); the data-table half of the same file (lines 15-32, parsing the real table and comparing every entry) is a genuine check and is not part of this finding.

**Problem:** READ (each file opened in full). These tests fail on a clang-format reflow, a variable rename, or an equivalent refactor, and pass on a semantic regression that keeps the spelled-out text (a condition moved to a helper, a guard made unreachable, a call added after the checked one). They are also the only coverage for their claims — there is no companion compiled harness for the RA support-check routing, the SMAP TX-base rule, or the DEV9 gating order. (Contrast, in the same half: `test_smb1_vmc_share.py` is also text-based but *mutation-checks itself* — it rewrites the guards and requires its own predicate to fail — and the majority of the half compiles the real function and runs it. The finding is only about the unguarded pure-text set above.)
**Fix in words:** For each listed claim, either (a) promote it to the compiled-harness pattern the neighbouring suites already use — extract the function and run the case — or (b) if the claim is genuinely just "this call exists in this file", keep one substring check per claim, stripped of embedded whitespace/newlines, and drop the ordering chains that duplicate what a compiled test should prove. Delete the exact-whitespace variants either way.
**Benefit:** Reformatting stops breaking tests, and semantic regressions stop passing them.
**Effort:** M
**Risk:** Low
**Confidence:** High — READ.

---

## RE-RIPTOPL-241 — Deliberately weakened assertions in the wired host tests: one literally vacuous `CHECK`, one either-way `CHECK`, and a known data defect that is counted but never asserted (filed as W13-003)

**Category:** 6 (bugs/security — test validity)
**Locations:**
- `RiptOPL/tests/host/test_config.cpp:56` — `CHECK(num(a, "$Foo", -5) != 0 || true); // non-numeric string: just must not crash`. The `|| true` makes the condition true by construction; the `CHECK` can never fail. The only real coverage is "did not crash", which the ASan/UBSan build already provides without a fake assertion.
- `RiptOPL/tests/host/test_config.cpp:131` — `CHECK(num(c, "compat") == 9 || num(c, "compat") == -999); // BOM only stripped on file reads; must not crash`. Passes whether BOM handling works (9) or the key is silently lost (-999, the caller's default). Either outcome is recorded as correct, so a BOM regression — or a BOM fix — is invisible.
- `RiptOPL/tests/host/test_vmc_groups.cpp:16-17,29-30,35-36` — the test counts duplicate title IDs across memory-card groups (`duplicates++`) and non-11-char IDs (`oddShape++`), under the comment `KNOWN DATA ISSUE (reported, not asserted): some titles are listed in more than one group, which makes the later group unreachable for them`. The counts are only `printf`d. **Measured in this audit's run of `tests/host/run.sh`:** `info: 97 groups, 1034 distinct titles, 12 duplicate entries, 8 odd-shaped ids` followed by `PASS: vmc_groups`. Twelve titles are unreachable in their later group *today*, and the suite that knows it reports PASS.

**Problem:** READ (all three sites opened; the vmc_groups numbers are the test's own output from a run in `/tmp/w13repo`). These are the only three "must not crash / not asserted" weakenings in the whole in-scope tree (verified by grep over both halves of the scope), which makes them cheap to fix and hard to justify keeping: they train readers to trust a PASS line that, at these three points, asserts nothing or asserts both outcomes.
**Fix in words:** For line 56, delete the `CHECK` and keep the bare call (the crash coverage is the sanitizer's). For line 131, pick the intended BOM behaviour and assert exactly that one value. For vmc_groups, either fix the 12 duplicate / 8 odd-shaped entries in the table in `src/core/vmc_groups.cpp` and then assert `duplicates == 0 && oddShape == 0`, or — if some duplicates are intentional — assert the exact known counts so any *new* duplicate fails. Do not keep a counter that is printed and ignored.
**Benefit:** Three PASS lines start meaning something; the vmc_groups data defect stops growing silently.
**Effort:** S (assertions) / M (if the table data itself is cleaned)
**Risk:** Low
**Confidence:** High — READ + measured run output.

---

## RE-RIPTOPL-242 — Skip-to-pass: three places convert "cannot run" into a green result (filed as W12-010; merged with W13-002)

- Category: 6 bugs/security (test validity)
- Locations: `RiptOPL/tools/run_tests.sh:8` (if `bs4` is unimportable, `test_site_sync.py`/`test_wiki_sync.py` are echoed as `SKIP` and the loop continues — the run still ends "all tests passed"); `RiptOPL/.github/scripts/test_iso9660_records.py:112` (`@unittest.skipUnless(CC, 'no host C compiler')` on the whole test class); `RiptOPL/.github/scripts/test_install_ports.py:405,411` (`skipTest` when `cc`/`ar` are missing or the host compiler does not emit ELF)
- Problem: in the environments these guards exist for (a bare local machine, a slim container), the suites report success having asserted nothing, and nothing in the output distinguishes "passed" from "never ran" at the workflow level (exit code 0 in all three mechanisms — READ for run_tests.sh and the unittest decorators; the install_ports skips are conditional paths READ at the cited lines). The flavours workflow mitigates this for site/wiki sync by installing the deps in dedicated venv jobs — which makes the run_tests.sh skip purely a local false-green. The compiler skips are defensible on a dev laptop; they are not defensible as silent passes in any CI job that relies on the glob runner alone (see W12-001/W12-002).
- Fix: in `run_tests.sh`, make a missing `bs4` a failure with the message "install .github/requirements-docs.txt" instead of a skip (CI installs it; locally the message tells the user how to get the real run). Leave the two compiler `skipTest`s in place for direct local runs, but note in both files that CI must provide `cc`/`ar` — it currently does (ubuntu runners), so this half is documentation, not behaviour change.
- Benefit: a green run_tests.sh run means every suite actually ran.
- Effort: S · Risk: Low · Confidence: High (READ)

---

**Merged finding (filed as W13-002 — Skip-to-pass: behavioural sub-checks that exit 0 when their library/compiler is absent):**

**Category:** 6 (bugs/security — test validity)
**Locations:**
- `RiptOPL/.github/scripts/test_zso_validation.py:166-169` — `cc = shutil.which('gcc') or shutil.which('cc'); if cc is None: print('SKIP: no host C compiler'); return 1 if bad else 0`. The entire compiled ZSO reader harness is skipped and the script returns 0 whenever the static call-site list is clean.
- `RiptOPL/.github/scripts/test_ripps2_art_port.py:131-138` — probes for `jpeglib.h` and `from PIL import Image`; on either failure it prints `skip JPG decode: host has no libjpeg headers or Pillow` and falls through to exit 0. **Observed live in this audit's run:** this VM has Pillow but no `jpeglib.h`, and the script's output was `… libjpeg linked | skip JPG decode: host has no …` with exit code 0 — the actual JPEG decoder (baseline/progressive/grayscale/downscale/non-JPG refusal, lines 139-195) did not run. The wiring job (`flavours.yml`, migration-parity job) installs only `requirements.txt` (PyYAML) — no libjpeg headers and no Pillow — so whether CI runs this half depends on whatever the runner image happens to preinstall (INFERRED for the GitHub image; READ for this VM and for the workflow's install steps).
- `RiptOPL/.github/scripts/test_ripps2_gui_port.py:192-196` — if `pkg-config --cflags/--libs freetype2` fails, `check_weight` prints `… the glyph check is SKIPPED` and returns; the script exits 0. (In this VM pkg-config succeeded and the glyph check did run — `weight ok` — so this gate is environment-dependent, exactly the problem.)
- `RiptOPL/.github/scripts/test_neutrino_cheats.py:19-21,207-210` — `try: import tomllib except ImportError: tomllib = None`, and `toml_of()` then returns `(body, None)`; every parsed-TOML assertion is guarded by `if data is not None`, so on Python < 3.11 the "written file parses as TOML and holds the codes" check silently degrades to a text check. Lower severity than the others (CI's Python is ≥ 3.11 and the raw-text checks still run), but the same pattern.

**Problem:** READ (all four sites opened; the art-port skip reproduced). A green run of these scripts does not mean the named behaviour was tested — it means it was tested *or* the host lacked a package. Nothing in the output distinguishes the two in CI logs unless someone reads the stdout of a passing job.
**Fix in words:** Make the environment part of the job, not of the test: install the needed package in the wiring workflow step (libjpeg headers + Pillow for the art port, FreeType for the gui port — both are one apt/pip line), and change each in-test gate from "print skip, exit 0" to a hard failure when the prerequisite is missing. Delete the `tomllib = None` fallback and require Python ≥ 3.11, which CI already has. If a check is genuinely meant to be optional, it should not share a script/exit code with mandatory checks — split it out and name it optional.
**Benefit:** A green tick regains a single meaning; the JPG decoder and the FreeType weight logic — both user-visible rendering paths — cannot silently lose their only host coverage when a runner image changes.
**Effort:** S
**Risk:** Low
**Confidence:** High — READ, with the art-port skip reproduced in this audit's own run.

---

## RE-RIPTOPL-243 — The extract-a-C-function harness boilerplate is copy-pasted into 52 suites instead of living in one place (filed as W12-004; merged with W13-006)

- Category: 2 duplication
- Locations: mechanism census (command-run) across the 143 in-scope files: 52 files define their own `def function(...)` extractor terminating a body at the first `'\n}\n'`; 24 define `def read(...)`; 46 re-implement the `failures = []` / collect-and-`sys.exit(1)` tail; 141 recompute `Path(__file__).resolve().parents[2]` as repo root. Representative copies READ in full: `RiptOPL/.github/scripts/test_accurate_read_rate.py:22-27`, `RiptOPL/.github/scripts/test_config_escape_roundtrip.py:8-10`, `RiptOPL/.github/scripts/test_net_recv_poll.py:16-29` (a fourth, brace-counting variant of the same extractor).
- Problem: this is the one case where the shared-helper test is met: identical logic in 52+ places, and sharing removes code overall. The copies have already diverged (index-based vs brace-counting extraction), so a fix to extraction fragility — the index variant silently truncates any function containing an unindented `}` in a string, macro or `#if 0` block — must currently be made dozens of times. READ.
- Fix: one small module in `.github/scripts/` (root resolution, `read()`, both extractors, failure tail) imported by the suites; delete the per-file copies as they are touched. No new framework — a single plain module, mirroring the existing `hostmap.py` import pattern.
- Justification (adds a file): required — the module is ~40 lines replacing ~10+ duplicated lines × 100+ suites repo-wide; net deletion, and it makes the extractor fixable in one place.
- Benefit: hundreds of lines removed across the suite; one extraction semantics instead of four.
- Effort: L · Risk: Med (touching many suites; do it incrementally) · Confidence: High (READ)

**Merged finding (filed as W13-006 — The function-extraction / host-compile harness is re-implemented in 76 second-half files, with divergent semantics):**

**Category:** 2 (duplication)
**Locations:** (mechanism census by script over the second half)
- 76 of 143 files define their own top-level extractor (`def function(`, `function_text(`, `extract(`, `extract_function(`, `c_function(`, `driver_function(`, `function_body(`), e.g. `RiptOPL/.github/scripts/test_neutrino_args_editor.py:10-14`, `test_pccf.py:20-23`, `test_psbbn_flow.py:27-29`, `test_smb2_logon_parity.py:88-93`, `test_ra_chunk_integrity.py:11-13`. Extractor-shaped top-level definitions total **640 lines across 83 files** (AST-measured).
- The extractors do not agree: 28 second-half files slice at the first `"\n}"` (e.g. `test_sfx_oneshot_cut.py:14-15`, `test_ra_support_check.py:15-17`), others brace-count (`test_ps1_duplicates.py:19-27`, `test_pad_handoff.py:27-35`), others regex for the signature first (`test_neutrino_args_editor.py:11`). A function whose body contains a column-0 `}` in a string, a macro, or a raw literal is sliced differently depending on which suite extracts it.
- 33 second-half files also re-implement the same `failures = []` + `check()/append` + summary-exit scaffold.
- Worst pairwise duplication (line-set similarity, script-measured, lines > 25 chars): `RiptOPL/.github/scripts/test_pad_missed_read.py` ↔ `test_pad_mode_lock.py` share 84 long lines (51% of the smaller file — the whole libpad stub, extractor and compile driver are copied); `test_smb_vmc_io_result.py` ↔ `test_smbman_ra_write_count.py` share 78 long lines (38%).

**Problem:** READ (census + AST count by script; the named files opened). Each suite's extractor is a private dialect, so a fix to extraction (e.g. handling a new C++ construct) must be made dozens of times, and two suites can extract different text for the same production function and both pass. This is the one place in the scope where the LESS rule's sharing test is clearly met: the same logic exists in 76 places, and one shared implementation is strictly less code than 76 copies.
**Fix in words:** Extract exactly one shared helper module in `.github/scripts` (the directory already shares modules this way — `hostmap.py`, and the `tools/` imports in the release tests) providing `read_source`, `function_body` (brace-counting version only) and `compile_and_run`, and delete the per-file copies as each suite is touched. Do not build a framework: three functions, no configuration. Suites whose extractor intentionally differs (e.g. `--ref` handling in `test_pad_missed_read.py`) keep that difference locally on top of the shared primitive.
**Justification (addition):** One ~80-line module replacing ~640 lines of extractor definitions plus 33 scaffolds is a large net deletion; it is justified by the concrete, measured duplication above, not by future-proofing.
**Benefit:** One extraction semantics for the whole suite; a production function that defeats extraction fails everywhere consistently instead of passing in the suites with the weaker slicer.
**Effort:** L (mechanical, but 76 files)
**Risk:** Med — mass-editing test files can itself break suites; do it suite-by-suite with a run after each, which the current per-file runnability makes cheap.
**Confidence:** High — READ (counts are script/AST outputs, not estimates).

---

## RE-RIPTOPL-244 — hostmap.py monkeypatches `pathlib.Path.read_text` globally so 11 suites can keep testing a source dialect that no longer exists (filed as W12-006; merged with W13-004)

- Category: 3 overengineering / 6 test validity
- Locations: `RiptOPL/.github/scripts/hostmap.py:44-55` (`_orig_read_text` capture; `pathlib.Path.read_text = _read_text` at import time; `unhost()` rewriting `host::ui()/app()/library()` calls back to pre-refactor names such as `guiMsgBox`, `deinit`, `libViewActive`); importers (command-run census, 11 in scope): `test_app_launch_args.py`, `test_autolaunch_settings.py`, `test_bdm_keep_page_on_reapply.py`, `test_bdm_private_allocation.py`, `test_bdm_scan_retry.py`, `test_cheat_load.py`, `test_ember_disc_selection.py`, `test_lang_format_guard.py`, `test_language_discovery.py`, `test_language_switch.py`, `test_native_vmc_layout.py`. The module's own docstring (READ, lines 1-7) states the purpose: mocks are "named after the OLD functions", so the patch makes production source read as if the host:: refactor never happened.
- Problem: the suites do not test the source in the repo; they test a regex-translated variant of it, against mocks named for a dead API. Any host:: call missing from the `HOST_TO_OLD` table silently reaches the mock layer untranslated (compile failure at best, wrong-name pass at worst — INFERRED), and the global patch is import-order-dependent: it rewrites **every** `.cpp` read in the process, including reads by code that never asked for translation. This is a compatibility shim for tests, kept alive instead of updating the mocks once.
- Fix: rename the mocks in the 11 suites to the current `host::` names, drop `import hostmap`, and delete `hostmap.py`. One-time edit; removes a whole translation layer and its regex table (~40 entries).
- Benefit: tests read the real source; one 55-line module and its failure modes gone.
- Effort: M · Risk: Med · Confidence: High (READ)

**Merged finding (filed as W13-004 — Seven second-half suites compile a *rewritten* copy of the production source: `hostmap.py` monkey-patches `Path.read_text` to map `host::` calls back to pre-refactor names):**

**Category:** 6 (bugs/security — test validity), with 3 (overengineering)
**Locations:**
- Mechanism: `RiptOPL/.github/scripts/hostmap.py:41-55` — at import time it replaces `pathlib.Path.read_text` process-wide; for any `.cpp` file under a `src` path (except `host.cpp`) it returns the text with every `host::ui()/app()/library()` call regex-rewritten to the old direct-call name (`guiMsgBox`, `deinit`, `libViewActive`, …), with two special cases that also *insert* a `_l(...)` wrapper (`missingFileId`, `warningId`, lines 34-39).
- Second-half importers (the patch is a no-op-looking `import hostmap  # noqa: F401`): `RiptOPL/.github/scripts/test_neutrino_argv_budget.py:12`, `test_neutrino_bsdfs.py:24`, `test_neutrino_cheats.py:25`, `test_neutrino_ra.py:14`, `test_neutrino_dvr_contract.py:9`, `test_ra_disc.py:7`, `test_udpfs_themes.py:24`.

**Problem:** READ (hostmap.py read in full; all seven import sites verified). The docstring is honest — "the extracted logic is unchanged" — but that is exactly the gap: what these harnesses compile is not the production function as written. The real `host::` dispatch layer (which object, which method, the `_l()` indirection for the two ID-based calls, argument order at the real call site) is edited out before compilation and replaced with mocks of the *old* API, so a defect introduced in the `host::` layer itself, or a new `host::` method that has no entry in `HOST_TO_OLD`, is invisible to these suites (a missing entry surfaces only as a compile error against the mock, if the extracted body happens to include the call). The patch is also process-global and keyed on path parts, so any other `.cpp` read in the same process — including reads a future edit adds for data, not code — is silently rewritten too.
**Fix in words:** Retire the translation instead of extending it: give the seven harnesses mocks named after the real interface (a tiny fake `host::ui()/app()/library()` returning recording stubs, in the shape `tests/host/host_env.h` already uses for the C++ host tests) and delete the `read_text` patch. That is a one-time mock rename per harness; afterwards the extracted code is byte-for-byte the production code. Justification for the (small) addition of a mock header: it *removes* a global monkey-patch and a 37-entry translation table, and it is the same pattern the repo's own C++ host tests already use — net less mechanism, not more.
**Benefit:** Seven suites start testing the code that ships, including its call sites; a whole class of "test passed on text that no longer exists in the tree" failures disappears.
**Effort:** M
**Risk:** Med — the harnesses' mocks need the real signatures, so the first conversion will surface any place the old-name mock had drifted from the real one (which is the point, but it is work).
**Confidence:** High — READ.

---

## RE-RIPTOPL-245 — test_ee_hook_context.py verifies EE assembly with a hand-written Python MIPS interpreter instead of executing anything (filed as W12-007)

- Category: 3 overengineering / 6 test validity
- Locations: `RiptOPL/.github/scripts/test_ee_hook_context.py:19-95+` (136 lines total; `run()` parses `ee_core/src/asm.S`, then interprets opcodes in Python: `nop`, `la/lui`, `daddu/addiu/andi/sltu/slti`, `sd/sw`, `ld/lw`, branches, `jal`, `jr` — READ)
- Problem: the suite's verdicts are only as correct as this private re-implementation of MIPS semantics (delay slots, sign extension, clobbers are modelled by hand in the interpreter, per its own docstring: "an instruction-level ABI fixture, not MIPS execution"). A bug in the interpreter and a bug in the assembly can cancel out, and an interpreter gap reads as an assembly failure. It is the "tests a private re-implementation instead of the real code" pattern, applied to the interpreter rather than the code under test. The docstring is honest about the limitation, and the suite passes (command-run); the risk is divergence as either side evolves. INFERRED risk, READ mechanism.
- Fix: keep the fixture only for the register-clobber invariants that genuinely cannot run on the host, and state that boundary in the file; do not extend the interpreter to new opcodes — new hook checks should assemble/link the real asm (as native CI already does) rather than grow the simulator. Simplest net win available today: delete any interpreter opcode branches no current assertion exercises (to be confirmed by coverage before deleting).
- Benefit: stops investment in a parallel MIPS implementation; keeps the one check it is actually for.
- Effort: M · Risk: Med · Confidence: Med (READ mechanism; INFERRED divergence risk)

## RE-RIPTOPL-246 — Dead pre-C++-conversion layout branches: six suites still select between the old `src/*.c` tree and the current `src/**/*.cpp` tree at runtime (filed as W13-005)

**Category:** 1 (dead code), with 8 (half-finished migration)
**Locations:**
- `RiptOPL/.github/scripts/test_pad_handoff.py:18-19,106-108` — `PAD = CPP if CPP.exists() else ROOT / 'src/pad.c'`, `UNLOAD = 'PadManager::unloadOne' if CPP.exists() else 'unloadPad'`, plus two more `CPP.exists()` ternaries in the assertions.
- `RiptOPL/.github/scripts/test_ps1_duplicates.py:13` and its `if CPP: … else: …` settings-wiring branch — the `else` reads `src/opl.c` / `lng_tmpl/_base.yml`.
- `RiptOPL/.github/scripts/test_screen_pos_per_mode.py:25-31` — `cpp = (root / 'src/app/opl.cpp').exists()` selecting among `src/app/opl.cpp`/`src/opl.c`, `src/core/settings.cpp`/`src/opl.c`, `src/app/settingsio.cpp`/`src/opl.c`, `src/ui/guisettings.cpp`/`src/gui.c`, `lng/tmpl/_base.yml`/`lng_tmpl/_base.yml`.
- `RiptOPL/.github/scripts/test_ripps2_boot_video_mode.py:22` plus the `CONFIG = … if cpp else …` / `ACCESS = 'cfg.name' if cpp else 'cfg.filename'` branches — the `else` harness is a different (C-struct) definition of `config_set_t`.
- `RiptOPL/.github/scripts/test_ripps2_vmc_saves.py:29,313-315,454` — `src/vmcsaves.c` if it exists else `src/core/vmcsaves.cpp`; compiler, `-x` flag and `-std` all branch on the suffix; `src/guigame.c` vs `src/ui/guigame.cpp` at line 454.
- `RiptOPL/.github/scripts/test_udpbd_read_only.py:18,25-44` — `src()` returns the first existing of each pair (`src/sources/bdmdevices.cpp` / `src/bdmsupport.c`, `src/ui/guigame.cpp` / `src/guigame.c`, …), so every call site carries a dead second path.
- `RiptOPL/.github/scripts/test_zso_validation.py:22,145-146` — `CPP = (ROOT / 'src/platform/util.cpp').exists()` selecting the call-site file list.

**Problem:** READ, and the dead side verified absent at the pin: `src/pad.c`, `src/opl.c`, `src/bdmsupport.c`, `src/guigame.c`, `src/menusys.c`, `src/supportbase.c`, `src/vmcsaves.c`, `src/cuesupport.c` and `lng_tmpl/_base.yml` do not exist in the tree; `src/core/pad.cpp`, `src/app/opl.cpp` and `src/platform/util.cpp` do. Every `else` branch is therefore unreachable, and has been since the C→C++ conversion these tests were ported across. The cost is not just clutter: in `test_udpbd_read_only.py` the fallback is *first-existing wins*, so if a current file is ever renamed, the suite silently starts reading a different file instead of failing at the missing path; in `test_ripps2_boot_video_mode.py` the dead branch carries a whole second harness prelude.
**Fix in words:** Delete the selection logic and name the current path unconditionally in all six files (and let a missing file fail loudly, which is what a regression test should do). No replacement mechanism.
**Benefit:** Six suites shrink, their failure messages point at the real file, and a rename can no longer be masked by a fallback.
**Effort:** S
**Risk:** Low
**Confidence:** High — READ + existence check at the pinned HEAD.

---

## RE-RIPTOPL-247 — `test_config.cpp` pins a lossy libconfig round-trip as correct behaviour, labelled only "characterisation" (filed as W13-009)

**Category:** 8 (half-finished/redundant features)
**Locations:**
- `RiptOPL/tests/host/test_config.cpp:2-4` — header: these are "Characterisation tests … These pin the behaviour the planned Settings/ConfigSet rewrite must keep".
- `RiptOPL/tests/host/test_config.cpp:102-107` — a wOPL/libconfig file containing `gsm : { x = 1; y = "two"; }` is read into flat keys `gsm_x`/`gsm_y`, and the test asserts the file is written back with `gsm_x = 1;` *flat*, commenting: "members of a group named gsm/cheat/pademu/padmacro/osd are read as `<group>_<key>`; an unknown member like gsm_x is written back flat, only the known per-game keys regroup."

**Problem:** INFERRED from READ code. The in-memory round-trip is lossless (the test re-reads the written file and gets the same keys), so this is not a proven data-loss bug. What is pinned is an on-disk *shape change*: a grouped libconfig section comes back ungrouped for any member that is not in the known per-game key list. If any external reader of these shared files (the header says the files are "shared with wOPL/OPL-Launcher") depends on the grouping, the rewrite silently changes the file it produces — and the test suite now *requires* that behaviour, so a future fix would have to fight the test. A characterisation pin is the right tool during a rewrite; kept indefinitely, it becomes the specification.
**Fix in words:** Decide, in the planned Settings/ConfigSet rewrite the header already names, whether flat write-back is the intended format. If yes, rename the comment from "CHARACTERISATION" to a statement of intended behaviour so future readers know it is a decision. If no, fix the writer to regroup and flip this one assertion. Do not add machinery either way.
**Benefit:** A pinned accident stops masquerading as a pinned decision (or vice versa).
**Effort:** S
**Risk:** Low
**Confidence:** Low — the code and comment are READ; whether the shape change harms any external reader is INFERRED and not verifiable from this scope (no wOPL/OPL-Launcher source in the clone).

---

## RE-RIPTOPL-248 — flavours.yml hand-enumerates test files in ~30 steps, duplicating the glob runner; the two lists already disagree (filed as W12-002)

- Category: 2 duplication / 7 build/CI
- Locations: `RiptOPL/.github/workflows/flavours.yml:18-359` (five host-test jobs listing suites one per line); `RiptOPL/tools/run_tests.sh:5-9` (glob over the same directory)
- Problem: command-run census: workflows explicitly name 154 distinct `test_*.py` files; the repo has 278. In this worker's half alone, 58 suites are named in **no** workflow (e.g. `test_accurate_read_rate.py`, `test_artindex_network.py`, `test_build_driver.py`, `test_dialog_hints.py`, `test_ee_hook_context.py`, `test_igr_polling.py`, `test_mc_folders.py`, `test_mbedtls_vendor.py` — full list derived by script) and run in CI only via the ps2build/rolling glob. Conversely, every rename or deletion must be edited in flavours.yml by hand or that job fails on a missing file. Two mechanisms maintain one list; they have already drifted apart. READ (counts by script).
- Fix: delete the per-file enumeration in flavours' host-test jobs and call `tools/run_tests.sh` (keeping the two venv jobs for site/wiki sync, which need extra packages); keep job splitting only if wall-clock requires it, splitting by directory glob, not by filename.
- Benefit: one test list, maintained nowhere; new suites are picked up automatically in every job; net deletion of ~150 workflow lines.
- Effort: M · Risk: Med (job-time/profile changes) · Confidence: High (READ)

## RE-RIPTOPL-249 — docs-sync.yml embeds a ~150-line Python program inside YAML, with the same content written twice (Markdown and HTML literals), and no test covers it (filed as W12-008)

- Category: 2 duplication / 8 half-finished
- Locations: `RiptOPL/.github/workflows/docs-sync.yml:40-160 approx.` (`shell: python` step: badge rewrite, External Tools section built once as a Markdown f-string and again as an HTML f-string with the same ~13 tool entries, authors, URLs and descriptions duplicated literal-for-literal; commit-message delta logic)
- Problem: this is production code — it rewrites README.md and the live gh-pages site and pushes both — living where it cannot be imported, linted, or run by any suite (no `test_*.py` references it; command-run grep). Every tool entry must be edited twice, in two syntaxes, inside YAML string escaping; a slip in either copy desynchronises README from the site silently. READ.
- Fix: move the step's Python into `.github/scripts/` as a normal script (docs-sync keeps only checkout + invocation), and build both outputs from one entry table (name, URL, author, blurb) rendered to Markdown and HTML. No new abstraction beyond the table the duplication already implies.
- Justification (restructure): the script file replaces, not adds to, the YAML block; the entry table deletes the second literal copy of every entry.
- Benefit: testable, single-source content; workflow shrinks by ~150 lines.
- Effort: M · Risk: Med (it publishes the live site; validate output diff before first run) · Confidence: High (READ)

## RE-RIPTOPL-250 — Docs dependency pins exist in three places; two workflows inline them instead of using requirements-docs.txt (filed as W12-009)

- Category: 2 duplication / 7 build/CI
- Locations: `RiptOPL/.github/requirements-docs.txt:1-4` (`beautifulsoup4==4.15.0`, `markdownify==1.2.0`, `markdown-it-py==4.0.0`, `PyYAML==6.0.2`); inlined duplicates: `RiptOPL/.github/workflows/docs-sync.yml:184` and `RiptOPL/.github/workflows/flavours.yml:350` (both `pip install beautifulsoup4==4.15.0 markdown-it-py==4.0.0`); correct single-source use: `wiki-publish.yml:96`, `flavours.yml` wiki job (`-r .github/requirements-docs.txt`)
- Problem: the inline copies are today a *subset* (site_sync.py imports only `bs4` + `markdown_it` — READ at `site_sync.py:51-52` — so the subset is currently correct), but the versions now live in three spots: bump `requirements-docs.txt` and the two inline installs silently stay behind, splitting the site generator's environment between the docs-sync and flavours jobs. READ.
- Fix: replace both inline pin lists with `pip install -r .github/requirements-docs.txt` (installing markdownify/PyYAML into that venv too is harmless) and delete the literal pins.
- Benefit: one place to bump; generator runs on identical packages in every job.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-251 — The main CI workflow runs the full host-test suite with no Python dependencies installed; four suites import PyYAML and only pass if the runner image happens to preinstall it (filed as W12-001; merged with W13-008)

- Category: 7 build/CI
- Locations: `RiptOPL/.github/workflows/ps2build.yml:69-71` (assets step, then `tools/run_tests.sh -k`, no `pip install` step anywhere in the file — verified by grep); `RiptOPL/requirements.txt:1` (sole content `PyYAML==6.0.3`); importers in scope: `RiptOPL/.github/scripts/test_audit_translations.py:9`, `RiptOPL/.github/scripts/test_build_driver.py:9`, `RiptOPL/.github/scripts/test_llvm_release.py:11`, `RiptOPL/.github/scripts/test_menu_text_fit.py:31` (all `import yaml`)
- Problem: `tools/run_tests.sh` runs every `.github/scripts/test_*.py` by glob. `configurations.yml`, `rolling-release.yml` and `rolling-llvm.yml` all do `python -m pip install -r requirements.txt` first; `ps2build.yml` — the compiler-validation workflow that runs on every push/PR — does not. The four YAML-importing suites therefore depend on the GitHub runner image's preinstalled packages, an unpinned, invisible dependency: an image change turns them into import errors, and locally they pass only because PyYAML happens to be installed (verified: all 137 pass in this VM, where `yaml` is importable). READ.
- Fix: add the same one-line `pip install -r requirements.txt` step to ps2build.yml before the host-tests step (mirrors three sibling workflows; adds nothing new).
- Benefit: the test gate's inputs become declared and pinned; removes a silent environment dependency.
- Effort: S · Risk: Low · Confidence: High (READ)

**Merged finding (filed as W13-008 — The `udpfs-host-tests` CI job runs two PyYAML-importing suites but never installs PyYAML):**

**Category:** 7 (build/CI)
**Locations:**
- `RiptOPL/.github/scripts/test_udpfs_transport.py:8` — `import yaml` (top level; the suite cannot start without it).
- `RiptOPL/.github/scripts/test_udpfs_shared_transport.py:3` — `import yaml`.
- `RiptOPL/.github/workflows/flavours.yml:320-339` — the `udpfs-host-tests` job that runs both files has no `pip install` step at all (checkout, then the test commands). The jobs that do install it are different jobs: the migration job installs the repo-root `requirements.txt` (`PyYAML==6.0.3`), and the site/wiki job installs into throwaway venvs. Jobs do not share environments.
- Same exposure, currently dormant because the file is unwired (W13-001): `RiptOPL/.github/scripts/test_ra_language_isolation.py:9` — `import yaml`.

**Problem:** READ (workflow job read in full; imports verified). The suites pass in CI today only because the `ubuntu-24.04` runner image happens to ship a PyYAML the workflow never asked for (INFERRED — the workflow itself provides no other source for the module; locally, this VM's system Python has PyYAML 6.0.1 and both suites pass). If the runner image drops it, two networking suites go red for an unrelated reason — or, worse, someone "fixes" it by skipping them (the W13-002 pattern).
**Fix in words:** Add the same one line the migration job already uses (`python3 -m pip install -r requirements.txt`) to the `udpfs-host-tests` job. Nothing else.
**Benefit:** The job's dependencies are the ones it declares; a runner-image change cannot silently change what is tested.
**Effort:** S
**Risk:** Low
**Confidence:** High for the missing install step (READ); Med for the claim that the runner image is the current source of PyYAML (INFERRED).

---

## RE-RIPTOPL-252 — The format gate never checks a single C++ file, and the local and CI format mechanisms disagree about what is excluded (filed as W14-002; merged with W12-003)

**Category:** 7 build/CI/repo clutter

**Locations:**
- `RiptOPL/.github/workflows/check-format.yml:5-19` — trigger paths are only `**.h`, `**.c`, `.clang-format`, `.clang-format-ignore` and the workflow itself; a change touching only `.cpp` files never triggers the job.
- `RiptOPL/.github/workflows/check-format.yml:31-37` — the lint step sets `extensions: 'h,c'` and `exclude: './third_party'` only.
- Script counts at the pin: 130 `.cpp` files repo-wide outside `third_party` (115 under `src`/`include`/`ee_core`/`modules` alone) — i.e. the entire C++ edition this repo now is — versus 254 `.c` and 431 `.h`.
- `RiptOPL/.clang-format-ignore:1-20` — 20 vendored/do-not-reformat paths (`modules/network/smbman-ra`, `modules/usb/usbd-ra`, `modules/bdm/bdmfs_fatfs`, `modules/network/smb2man/usmb2`, `modules/network/nfsman/unfs4`, `tests/host`, …). The CI step does not pass this file or its contents as exclusions; the exclusion it does pass (`./third_party`) is a different, shorter list.
- `RiptOPL/Makefile:541-546` — the local `format`/`format-check` targets use a second mechanism entirely: `tools/download_cfla.sh` clones `third_party/clang-format-lint-action` at pinned commit `c3b2c943e924028b93a707a5b1b017976ab8d50c` and runs its `run-clang-format.py -r .`, while CI uses `DoozyX/clang-format-lint-action@v0.18.2` as an action.
- `RiptOPL/README.md:35` carries a "Format" badge for this workflow on a repo whose badge line 41 says "Written in C++".

**Problem:** The gate CI claims (and the README badges) does not cover the language the codebase is written in: a badly formatted `.cpp` change neither triggers nor is checked by the workflow. The two format paths that do exist diverge — the ignore file that protects vendored code from reformatting is honoured by the local script path (INFERRED: `run-clang-format.py` reads `.clang-format-ignore` from the working directory) but is not wired into the CI action's `exclude`, so CI and local runs can disagree in both directions.

**Fix in words:** Make the one CI job the only gate: add `cpp` (and `hpp` if any appear) to its trigger paths and `extensions`, and pass the `.clang-format-ignore` entries as the action's exclusions (or generate the exclude list from that file) so vendored paths are skipped in CI exactly as locally. Delete the Makefile `format`/`format-check`/`download_cfla` mechanism with W14-001 rather than maintaining a second one.

**Justification (for the extension, not a new system):** this adds no new tool — it widens the existing job's file list to the files the project actually contains, which is the concrete bug.

**Benefit:** The format badge becomes true for the C++ tree; vendored files stop being a CI-vs-local disagreement source.

**Effort:** S · **Risk:** Low (first widened run may surface pre-existing `.cpp` formatting debt — that is the gate working, and can be landed as one format-only commit) · **Confidence:** High for the workflow contents and file counts (READ, script-counted); Med for the ignore-file behaviour of the two runners (INFERRED)

---

**Merged finding (filed as W12-003 — Format gate never sees C++: trigger paths and the lint action are both restricted to `.h`/`.c`):**

- Category: 7 build/CI
- Locations: `RiptOPL/.github/workflows/check-format.yml:5-16` (push/PR `paths: **.h, **.c, …`) and `:32-37` (`extensions: 'h,c'`)
- Problem: command-run census of the source tree: 431 `.h`, 254 `.c`, **115 `.cpp`** files (plus `.hpp`-style headers under the `.h` count). No `.cpp` change triggers the workflow, and even when triggered the action is told to check only `h,c`. The entire C++ UI/source layer (`src/**/*.cpp`) is outside the format gate that the rest of the tree is held to. READ; whether the exclusion was deliberate is INFERRED-unknown — no comment in the workflow explains it.
- Fix: add `**.cpp` (and `cpp` to `extensions:`), or, if the exclusion is deliberate, write that reason in the workflow so it stops looking like an oversight.
- Benefit: one format standard for the whole tree, or an explicitly documented exception.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-253 — The top-level Makefile is a dead second build system whose duplicated lists have already drifted from the live one, yet tests and a packaging script still read it as a data source (filed as W14-001)

**Category:** 7 build/CI/repo clutter (with 1 dead code, 2 duplication)

**Locations:**
- `RiptOPL/Makefile:1197-1203` — the only EE compile rules are `$(EE_OBJS_DIR)%.o: $(EE_SRC_DIR)%.c` and the `asm/` equivalent. There is no `.cpp` rule, and `src/` is now six subdirectories of `.cpp` files.
- `RiptOPL/Makefile:136-141` (`FRONTEND_OBJS`, 53 objects) vs the tree: script count found 119 `src/**/*.cpp|*.c` files; 57 of them (e.g. `achievements`, `artload`, `autolaunch`, `coverflow`, `diaeditors`, `gamemanage`, `grid`, `psbbnflow`, `selfupdate`, `sfx`, `sysneutrino`, `themeviews`, `vmcrebuild`) are named nowhere in the Makefile.
- `RiptOPL/Makefile:156-168` (`PNG_ASSETS`, 108 real names) vs `RiptOPL/tools/assets.py:49-60` (104 base + 24 generated glyph names = 128): the Makefile list is missing 27 assets the live build embeds — `psbbn_bg`, `techwritescode`, `grid_devices` and all 24 `glyph_standard_*` / `glyph_white_*`. The code references those symbols: `src/gfx/textures.cpp:65,320,329` (`psbbn_bg_png`, `techwritescode_png`), `src/gfx/themes.cpp:1191-1192` (extern `glyph_<set>_<name>_png`).
- `RiptOPL/Makefile:189` (`MISC_OBJS`, 7 entries) vs `RiptOPL/tools/assets.py:68-78` (`MISC`, 9 entries): the Makefile never embeds `theme_grid.cfg` / `theme_psbbn_flow.cfg`, whose symbols the code requires — `src/gfx/themes.cpp:35-38,1346-1348`.
- `RiptOPL/Makefile:1213` — `INTERNAL_LANGUAGE_C = src/lang_internal.c`; the generated file actually lives at `src/core/lang_internal.c` (`tools/assets.py:117`, `ps2.yaml` riptopl sources, `.gitignore:72`). `src/lang_internal.c` does not exist.
- `RiptOPL/Makefile:142-152` (`IOP_OBJS`/`EECORE_OBJS`) vs `RiptOPL/ps2.yaml` riptopl target `embed_irx` (85 entries, script-counted): no Makefile object or rule covers `smb2man`, `nfsman`, `smb2_cdvdman`, `nfs_cdvdman`, `pccf_bd`/`pccf_game`/`pccf_cdvdman`, or `igrmbox`, all of which `ps2.yaml` defines as targets and embeds.
- Duplicated constants, already the drift surface above: version `Makefile:1-4` = `tools/assets.py:31`; `TRANSLATIONS` `Makefile:191-193` = `tools/assets.py:44-46`.
- Readers that keep the dead file load-bearing: `tools/lng_pack.sh:23-27` greps the version out of the Makefile; `.github/scripts/test_bdm_index.py:146`, `test_menu_crash_catcher.py:88`, `test_smb1_vmc_share.py:87` and `test_apa_table_safety.py:215-224` read the top-level Makefile as a spec.
- The repo itself says so: `docs/PS2BUILD-MIGRATION.md:75` ("the legacy Makefile cannot compile the converted C++ sources"), `:112` ("The Makefile can no longer build src/ (no .cpp rule)"), `:106` (open item: "Makefile removal"), and `README.md:893-895` ("The legacy Makefile … [is] not supported here").

**Problem:** Two build definitions for one product. The Makefile one cannot compile the current tree at all (no C++ rule, flat `src/*.c` layout, wrong generated-file path), and where its lists overlap the live `tools/assets.py`/`ps2.yaml` lists they have silently diverged (27 embedded PNGs, 2 theme configs, whole IOP targets missing). Anyone who runs `make` gets a broken or wrong build; anyone who edits a list in one place desyncs the other; and because host tests and `lng_pack.sh` parse the Makefile, it cannot simply be ignored — its stale values are asserted as truth in CI.

**Fix in words:** Delete the top-level Makefile's build system. Move its last live data to the single owners that already hold copies — version numbers and the translation list already exist in `tools/assets.py:31,44-46` — point `tools/lng_pack.sh` at `tools/assets.py version-string` (which already exists) and re-point the four host tests listed above at `ps2.yaml`/`tools/assets.py`, as other tests already do (`.github/scripts/test_mbedtls_vendor.py:7` notes exactly this conversion for its own check). Then delete `Makefile`. The 45 module-level Makefiles and `modules/Rules.bin.make` (215 B, included by e.g. `modules/bdmevent/Makefile:7`, `modules/iopcore/cdvdman/Makefile:104`) are reachable only through the dead top-level build — except `modules/bdmassault/ilink_bd_assault/Makefile`, which `.github/workflows/flavours.yml:372-375` invokes directly — so delete the rest in the same pass after a grep confirms no workflow/test references each one.

**Benefit:** One build definition; the 27-asset / 2-config / 6-target drift class disappears; `make` stops being a trap that produces a plausible-looking broken ELF; ~52 KB Makefile plus the legacy module Makefiles leave the tree.

**Effort:** L · **Risk:** Med (tests and `lng_pack.sh` must be re-pointed first, or they break) · **Confidence:** High (READ — every list compared by script at the pin)

---

## RE-RIPTOPL-254 — BDMA driver identity is a hand-maintained table of exact byte sizes that churns every driver rebuild (filed as W01-010)

- **Category:** 7 build/repo clutter
- **Locations:** `RiptOPL/src/sources/vcdsupport.cpp:1699-1707` (`vcdBdmaPairSig`), used at `:1710-1716` (`vcdBdmaPairMatches`), `:1755-1765` (`vcdBdmaPairIsShipped`), and by the launch/equip validation at `:1768-1830`
- **Problem:** Whether the driver pair on a memory card / in a POPS folder is "this release's" is decided by comparing file sizes against seven hard-coded byte pairs. The table's own comment records three different historical sizes for the iLink driver alone (21609 → 27145 → 27769), i.e. every BDMAssault driver rebuild silently invalidates the table until someone hand-edits two numbers per affected mode — and until they do, launches re-equip (copy two modules onto the MC) or refuse a pair that is in fact current.
- **Fix:** Generate the table at build time from the actual shipped POPS/ driver files instead of maintaining it by hand in C++; the comparison logic stays exactly as is. (Deleting the size check entirely was considered and rejected here: the comments show a real stale-pair failure it guards against.)
- **Justification (fix adds a build step):** It replaces a recurring manual edit that has already been needed at least three times for one driver; no runtime code is added.
- **Benefit:** The signature can never drift from the drivers actually shipped; one recurring release-time edit disappears.
- **Effort:** M · **Risk:** Low · **Confidence:** High — READ.

## RE-RIPTOPL-255 — tar.h's header comment claims CFG/CHT kinds are unwired; the code wires them (filed as W04-012)

- **Category:** 7 (build/CI/repo clutter — stale documentation with a deletion trap)
- **Locations:** `RiptOPL/include/tar.h:4-8` (comment), `RiptOPL/include/tar.h:53-63` (`TarEntryCfg`, `TarEntryCht`); contradicted by `RiptOPL/src/platform/tar.cpp:61-62` and `RiptOPL/src/sources/supportbase.cpp:1761-1784` (outside this worker's scope; found by grep while checking the structs for users)
- **Problem:** The comment says "RiptOPL wires ONLY the ART kind… the CFG/CHT kinds are kept intact so they can be wired later." That is no longer true: `tar.cpp` registers all kinds in its table (using `sizeof(TarEntryCfg)` / `sizeof(TarEntryCht)` as the on-disk entry strides), and `supportbase.cpp` actively reads cheats out of `CHT/cht.tar` via `tarFind(TAR_KIND_CHT, …)`. The comment invites a future cleanup to delete two structs that are load-bearing for the cache file layout.
- **Fix:** Rewrite the comment to state what is actually wired (ART and CHT at minimum) and that the entry structs define on-disk stride and must not be resized without bumping `ARC_VERSION`. Delete nothing.
- **Benefit:** Removes a comment that points a future editor at deleting live, format-defining types.
- **Effort:** S · **Risk:** Low · **Confidence:** High — READ (header read in full; consumers grep-verified).

## RE-RIPTOPL-256 — pad.h mixes constexpr and #define for one sequential key enum (filed as W04-016)

- **Category:** 7 (repo clutter, minor)
- **Locations:** `RiptOPL/include/pad.h:6-21`
- **Problem:** The `KEY_*` constants 1–16 are declared as `constexpr int` except four in the middle of the sequence — `KEY_CROSS` (10), `KEY_CIRCLE` (11), `KEY_TRIANGLE` (12), `KEY_R1` (13) — which are `#define`s. Nothing in the header explains the split; a reader cannot tell whether those four must stay macros (e.g. for a preprocessor conditional somewhere) or whether it's an editing accident. The same mixed style appears for the GS modes in `renderman.h` (see W04-004).
- **Fix:** Make all 16 one style (constexpr, matching the majority), unless a `#ifdef KEY_…` use exists — in which case comment why those four are macros.
- **Benefit:** One obvious-correct way to add the next key constant.
- **Effort:** S · **Risk:** Low · **Confidence:** Med — READ for the header itself; INFERRED that no preprocessor use requires the macro form (no such use was searched for across the whole tree).

---

## Checked and deliberately NOT filed

- `ee_core/include/padpatterns.h` pattern/mask tables: entry counts verified equal for all 7 pattern/mask pairs by script (22/22, 19/19, 18/18, 16/16, 24/24, 25/25, 23/23) — no mask/table-size mismatch.
- `GSMFlags` struct (`ee_core/src/gsm_api.c`) vs `.space 20` in `gsm_engine.S`: sizes agree.
- `DTV_1080P` block in `gsm_engine_adv.S` is `#ifdef GSM_1080P`-gated and the Makefile forwards the flavour flag — an intentional variant build, not dead code.
- Header-declared ee_core functions checked against definitions across C and assembly: the asm.h hooks, `dve_*`, `Enable/Disable_GSBreakpoint`, and `HookSetupThread` are all defined in `.S` files or `cheat_api.c`. Only `Remove_Padhook` (W04-011) has no definition.
- `include/dialogs.h` placeholder enum slots (`CFG_ENABLEUDPBD`, `CFG_NETBOOTPROTOCOL`, `COMPAT_RETIRED_DOWNLOAD_DEFAULTS`): the file itself documents that the IDs must be preserved for dialog-ID stability — deliberate, not dead weight.
- The class-plus-free-function-forwarder pattern in many `include/` headers: several headers document that host-test harnesses scrape the exact free-function signatures, which justifies the forwarders; not filed.
- No in-scope header is never-included (basename search across `src/`, `include/`, `ee_core/`, `elfldr/`, `modules/` found zero).

---

## RE-RIPTOPL-257 — Byte-identical 48,500-byte IRX blob stored twice in bdmassault (filed as W08-015)

- Category: 7 build/CI/repo clutter
- Locations: `RiptOPL/modules/bdmassault/usbd.irx.ilink`, `RiptOPL/modules/bdmassault/usbd.irx.usbexfat`
- Problem: READ via `sha256sum` run this session: both files hash identically (and `PROVENANCE.md`'s own dedupe note says the mx4sio usbd is the same bytes again — the embedded copy is already deduplicated at the ELF level, only the repo copies are not). The repo carries the same 48,500-byte binary under two names so the POPSTARTER paste step can pick by filename.
- Fix: Keep one canonical file and create the second name at package/paste time (the equip code already selects by device type), or document the pair as generated in the build that assembles `ship/POPS/`. Do not keep two checked-in copies that can drift independently — if only one is ever re-vendored, the hashes in `PROVENANCE.md` stop telling the truth about the other.
- Benefit: −48,500 repo bytes and one less silent-drift pair.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-258 — atad ORIGIN.txt states a change the code does not contain (filed as W09-015)

**Category:** 7 repo clutter / documentation drift
**Locations:**
- RiptOPL/modules/hdd/atad/ORIGIN.txt, local-change item 4 ("The `g_ata_bd[i].path` assignment is DROPPED…")
- RiptOPL/modules/hdd/atad/src/ps2atad.c:359 (`g_ata_bd[i].path = "ata";` — present, with its own comment)

**Problem:** The provenance file that the re-vendor procedure depends on says the `.path` assignment was dropped for ABI reasons, but the vendored source assigns it. A future re-vendor following ORIGIN.txt will either "re-drop" a field the current build relies on for `ata<k>:` naming, or waste the investigation re-deriving which document is wrong.
**Fix:** Correct ORIGIN.txt item 4 to describe what the code does (assignment kept, with the ps2sdk commit the comment cites), or — if the ABI concern in the note is the true one — remove the assignment and keep the note. One of the two, deliberately.
**Benefit:** The re-vendor checklist becomes trustworthy again for this module.
**Effort:** S · **Risk:** Low · **Confidence:** High (READ — both texts in hand)

## RE-RIPTOPL-259 — freepad "vendoring" exists only as a provenance note; the mechanism it describes does not exist (filed as W10-013)

- **Category:** 7 (build/CI/repo clutter) / 8 (half-finished features)
- **Locations:** `RiptOPL/modules/freepad/PROVENANCE.md:3-6` (the only file in the directory); `RiptOPL/.gitignore:15` (`*.irx`); `RiptOPL/Makefile:1121`
- **Problem:** PROVENANCE.md states that a vendored `freepad.irx` (36,741 bytes, sha256 tabled in the file) is "installed over the container stock freepad" by `.github/scripts/install_coherent_freepad.sh`. That script does not exist anywhere in the tree (repo-wide search: the only hit is inside PROVENANCE.md itself), the binary itself is excluded by `.gitignore`, and the top-level Makefile builds padman from the PS2SDK's stock `freepad.irx`. The directory documents a supply-chain step that never happens.
- **Fix:** Delete `modules/freepad/` and its provenance note until the install mechanism actually exists; if the vendored binary is ever really wired in, the note can come back with it.
- **Benefit:** Removes a provenance claim a reader (or auditor) will trust and be misled by.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ; absence verified by repo-wide search this session)

## RE-RIPTOPL-260 — Translation downloads are unpinned, contradicting the "pinned" claim in tools/assets.py (filed as W11-007)

- Category: 7 build/repo (reproducibility)
- Locations: `RiptOPL/tools/download_lng.sh:5-10`; claim at `RiptOPL/tools/assets.py:5`; contrast `RiptOPL/tools/download_lwNBD.sh:6-9` and `RiptOPL/tools/download_cfla.sh:5-9`
- Problem: `download_lwNBD.sh` and `download_cfla.sh` check out exact commits. `download_lng.sh` clones branch `main` of the lang repo and on update does `git reset --hard origin/main` — whatever upstream has that day is what gets compiled into `lng/*.lng` by `assets.py lang` and embedded in the release language pack, with no commit or digest recorded anywhere. `assets.py`'s own docstring says the fetch step is "pinned by tools/download_*.sh", which is false for the translations — the largest of the three downloads.
- Fix: Pin `download_lng.sh` to a commit the same way `download_lwNBD.sh` does (clone, then `git checkout <commit>`), and bump the pin deliberately.
- Benefit: Reproducible language packs; a bad upstream lang commit cannot silently enter a release build.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-261 — tools/assets.py bakes transient agent scratch directories into the release version anchor (filed as W11-016)

- Category: 7 repo clutter
- Locations: `RiptOPL/tools/assets.py:203-204`
- Problem: `opl_version()` computes the version's anchor commit by excluding paths from `git log`, and the exclusion list names local agent/tooling scratch dirs — `.claude`, `.agents`, `.codex`, `.codex-tmp-nhddl`, `.codex-tmp-wle-r3z` — alongside real repo paths. These are one-developer, one-machine temp directories (two are named for specific past tasks); they are not in the repository, so the exclusions do nothing in any clean checkout, and they fossilise a moment of one workstation's state into every future release's version logic.
- Fix: Drop the five scratch-dir entries from the exclusion list (keep `.github`, `notes`, `obj` and the other real paths).
- Benefit: Version logic describes the repository, not a workstation; −5 misleading entries.
- Effort: S · Risk: Low · Confidence: High (READ)

## RE-RIPTOPL-262 — Current user/contributor docs still teach the legacy Makefile build — commands, flags and variant names that no longer exist (filed as W14-003)

**Category:** 7 build/CI/repo clutter (docs contradict code)

**Locations (doc line → code truth):**
- `RiptOPL/README.md:443` — "1080p GSM … is built directly into all standard builds (`make GSM1080P=1`)". There is no working `make` build (W14-001); in the live build `GSM_1080P` is an unconditional define in `ps2.yaml:609,644` with no `GSM1080P` variable to set.
- `RiptOPL/docs/GSM.md:211-216` — "Notes for builders: `GSM1080P` is the Makefile variable; it defaults to `1`, and … the `Makefile` adds `-DGSM_1080P` … and forwards `GSM1080P=1` on to `ee_core/Makefile`. … Building with `GSM1080P=0` drops both 1080p rows and shortens the list to 29 entries." The 31/29 counts themselves are correct (`src/core/gsm.cpp:106-151`: 29 rows + 2 under `#ifdef GSM_1080P`), but no supported builder offers the `GSM1080P=0` build described: `tools/build_configuration.py:7-10` lists 15 configurations, none of them a GSM-off one, and `ps2.yaml` defines `GSM_1080P` unconditionally.
- `RiptOPL/docs/LANGUAGES.md:27,76,88-99` — build flow headed "`make languages`", "The `TRANSLATIONS` list in the `Makefile` decides which `lang_<Language>.lng` files get built", and a "Building just the languages" section whose only command is `make languages`. CI actually runs `tools/assets.py all` (`.github/workflows/ps2build.yml`, "Assets" step), and `tools/assets.py:44-46,106-124` holds and uses its own `TRANSLATIONS` list.
- `RiptOPL/lng/README.md:24-26,42,67,82` and `RiptOPL/lng/fork/README.md:16` — translator instructions are `make download_lng` / `make languages` throughout.
- `RiptOPL/docs/IGR.md:110-117` — IGS is explained via Makefile `EXTRA_FEATURES`/`IGS` defaults, and the IGS-enabled release loaders are "the variants named `-extra1` … in `pademu0`/`pademu1` and optional `-ds5` flavours", buildable with "`make IGS=1` (or `EXTRA_FEATURES=1`)"; `:120,204` reuse `EXTRA_FEATURES`/`-extra1`. The real mechanism is the `extras` / `extras_ds5` configurations: `tools/build_configuration.py:37-40` adds `__RTL`+`IGS` for those names, `docs/BUILD-CONFIGURATIONS.md:13-14,50-51` lists the variant folders as `pademu0`, `ds5`, `extras`, `extras_ds5`, and `README.md`'s own IGR table already says `extras/` and `extras_ds5/`. No `-extra1` or `pademu1` variant exists.
- `RiptOPL/docs/AUTOLAUNCH-GLOBAL-SETTINGS.md:139` — diagnostic build recipe is "`make clean` followed by `make OPLDIAG=1 release`"; the live equivalent is the `diag` configuration (`tools/build_configuration.py`, `diag` adds `__OPLDIAG`; `docs/BUILD-CONFIGURATIONS.md:53`).
- `RiptOPL/docs/HTTP.md:168-169` — "Both IRX rules depend on it in the top-level Makefile, because make cannot see inside a sub-make and a stale IRX otherwise builds clean locally and fails CI." There are no IRX rules in the live build; `http_cdvdman` is a `ps2.yaml` target.

**Problem:** A user or contributor following the current (non-dated) documentation runs commands that cannot work, looks for release variants under names that are never published (`-extra1`, `pademu1`), and is told a `GSM1080P=0` build exists when the supported builder cannot produce one. Each doc was true for the legacy C edition and was not rewritten when the build moved.

**Fix in words:** In each location above, replace the Makefile command/flag/variant name with the live one — `python tools/build_edition.py <edition> --configuration <name>`, `tools/assets.py lang`, the `extras`/`extras_ds5`/`diag` configuration names — and delete the `GSM1080P=0` builder paragraph in `docs/GSM.md`, keeping the (correct) mode-count facts. No new documentation structure; this is substitution in place.

**Benefit:** Every build instruction in the current docs is runnable as written; variant names in docs match the ZIPs users actually download.

**Effort:** M · **Risk:** Low · **Confidence:** High (READ — both sides cited)

---

## RE-RIPTOPL-263 — The migration doc's status section and a workflow comment describe flavours/CI that have since landed, as if they were still missing (filed as W14-004)

**Category:** 8 half-finished/redundant features (stale status docs)

**Locations:**
- `RiptOPL/docs/PS2BUILD-MIGRATION.md:103-106` — "`flavours.yml` builds the main flavour only; the … DIAG and RA are gone until `ps2.yaml` can express them", "`rolling-release.yml` still builds with `make` and will fail where it triggers; not ported", open items include "other flavours (RA, debug/DIAG, DUALSENSE …)".
- Contradicted by: `RiptOPL/.github/workflows/rolling-release.yml:122-136` (runs `tools/assets.py fetch`/`lang` then `tools/build_edition.py` per edition/configuration — no `make`); `RiptOPL/.github/workflows/configurations.yml:23-42` (17 edition/configuration matrix entries × 2 toolchains, including `ra` and `diag`-family configurations); `RiptOPL/docs/BUILD-CONFIGURATIONS.md:45-61` (15 configurations incl. `ds5`, `diag`, debug family).
- `RiptOPL/.github/workflows/flavours.yml:401-405` — comment in the live workflow itself: "The RA and DIAG flavours return when ps2.yaml can express them."
- Mitigation, stated for fairness: `docs/PS2BUILD-MIGRATION.md:3-7` carries a banner saying the planning sections "describe earlier migration choices … They are not the supported release build commands", and `docs/README.md:57-69` files it under "Migration and history". The sentences above are nonetheless phrased as present-tense status ("still builds", "will fail", "builds the main flavour only") inside the section a reader lands on for CI status.

**Problem:** Anyone assessing what CI covers from these texts concludes RA/DIAG/DS5 configurations and the rolling release do not exist or are broken, when all of them are live. The workflow comment repeats the stale claim at the exact place a maintainer would check it.

**Fix in words:** Delete the stale status sentences (`docs/PS2BUILD-MIGRATION.md:103-106` list entries for flavours and rolling-release, and the `flavours.yml:401-405` sentence) rather than rewriting the history — the dated plan below them can stay as history under its existing banner.

**Benefit:** CI-status claims match the workflows; no reader is sent to "port" a release pipeline that already runs.

**Effort:** S · **Risk:** Low · **Confidence:** High (READ)

---

## RE-RIPTOPL-264 — The language count is 31 in the README badge and the languages doc; the build builds 33 (filed as W14-005)

**Category:** 7 build/CI/repo clutter (docs contradict code)

**Locations:**
- `RiptOPL/README.md:40` — badge "Translations-31 languages".
- `RiptOPL/docs/LANGUAGES.md:69` — "the initial bulk fill of the 31 fork languages".
- Truth at the pin, script-counted: `tools/assets.py:44-46` `TRANSLATIONS` = 33 names; `Makefile:191-193` agrees at 33; `lng/fork/` contains 33 `<Language>.yml` overlay files (plus its `README.md`). English is generated separately (`tools/assets.py:110-111`), so the loader offers English + 33 translations.

**Problem:** Both public counts understate the shipped set by two, and they will keep drifting because the number is hand-typed in prose in two places while the authoritative list lives in code.

**Fix in words:** Change both numbers to 33 — or, better and smaller long-term, drop the number from the badge/doc sentence entirely ("Translations" without a count) so there is nothing left to drift. Do not add a generator for one badge.

**Benefit:** Public claims match the language pack the release actually builds.

**Effort:** S · **Risk:** Low · **Confidence:** High (READ, script-counted)

---

## RE-RIPTOPL-265 — Current reference docs cite source files at their pre-C++ flat paths, which do not exist (filed as W14-006)

**Category:** 7 build/CI/repo clutter (docs contradict code)

**Locations (doc line → actual file, verified to exist):**
- `RiptOPL/docs/APA-SAFETY.md:43,47,49,50` — cites `src/hddsupport.c` (four times) and `src/hdd.c`; actual: `src/sources/hddsupport.cpp`, `src/platform/hdd.cpp`. This is the current APA-safety reference, linked from `README.md` and `docs/README.md` — the doc a user follows before touching a disk whose table may be corrupt.
- `RiptOPL/docs/GSM.md:223-224` — cites `src/guigame.c` and `src/dialogs.c`; actual: `src/ui/guigame.cpp`, `src/ui/dialogs.cpp`.
- `RiptOPL/docs/HTTP.md:161` — cites `src/httpcatalog.c`; actual: `src/platform/httpcatalog.cpp`.
- `RiptOPL/docs/HTTP.md:164` — the host-test command is printed as `sh tools/tools/pc/http/tests/run.sh` (doubled `tools/`); the real script is `tools/pc/http/tests/run.sh` (verified present, with `compare_catalog.py` beside it — the same doc's `:161` names that file at its correct path).
- `RiptOPL/docs/RETROACHIEVEMENTS.md:520` — cites `src/md5.c`; actual: `src/platform/md5.c` (the file `.clang-format-ignore:6` and the Makefile's own vendoring comments already know by its new path).
- `RiptOPL/docs/LANGUAGES.md:29-30` — the generated English fallback is placed at `src/lang_internal.c` and described as the enum "every `.c` file uses"; actual generated path is `src/core/lang_internal.c` (`tools/assets.py:117`), and the consumers are `.cpp`.

**Problem:** One mechanism, repeated: when `src/*.c` was split into `src/{app,core,gfx,platform,sources,ui}/*.cpp`, the current reference docs were not re-pointed. Every one of these citations fails as a path a reader can open, and in `docs/HTTP.md:164` as a command a reader can run. (The same flat paths appear throughout the dated plan/handoff docs — e.g. `docs/EMBER-INTEGRATION-PLAN.md`, `docs/COMMUNITY_REPORTS_HANDOFF_2026-10-02.md` — but `docs/README.md:65-69` explicitly labels those as dated baselines, so they are not filed here.)

**Fix in words:** Substitute the actual paths above in the six current-doc locations; no other text changes.

**Benefit:** Citations in the safety, GSM, HTTP, RA and language references resolve to real files; the HTTP test command runs as printed.

**Effort:** S · **Risk:** Low · **Confidence:** High (READ — existence of every actual path verified by script)

---

---

## COVERAGE AND FILES NOT READ

Per-workstream coverage statements, verbatim from the section files (`~/workspace/Sable-audit-redo/redo-sections/`). Each lists what was read and what was not.


### From redo-W01.md

## COVERAGE

All 31 files in scope were opened and read in full (paged reads where the reader capped output). Line counts are the `wc -l` values measured in this session; total 31,201.

| File | Lines | Read |
|---|---|---|
| achievements.cpp | 797 | yes, full |
| appscan.cpp | 248 | yes, full |
| appsupport.cpp | 1444 | yes, full |
| bdmboot.cpp | 496 | yes, full |
| bdmdevices.cpp | 582 | yes, full |
| bdmlaunch.cpp | 999 | yes, full (incl. 956-end) |
| bdmsupport.cpp | 2408 | yes, full (incl. 1249-end, 2255-end) |
| cuesupport.cpp | 1288 | yes, full |
| discsupport.cpp | 183 | yes, full |
| ethsupport.cpp | 1664 | yes, full (incl. 1260-end) |
| favsupport.cpp | 1833 | yes, full (incl. 1273-end) |
| folderbrowse.cpp | 130 | yes, full |
| gamemanage.cpp | 347 | yes, full |
| gamesource.cpp | 67 | yes, full |
| hddsupport.cpp | 3383 | yes, full (incl. 1176-end, 2330-end) |
| httpsupport.cpp | 924 | yes, full |
| mmcesupport.cpp | 1855 | yes, full (incl. 1003-end) |
| netsupport.cpp | 501 | yes, full |
| nettime.cpp | 158 | yes, full |
| rabadge.cpp | 220 | yes, full |
| rahash.cpp | 971 | yes, full |
| rahunt.cpp | 213 | yes, full |
| ranet.cpp | 1136 | yes, full |
| rapopslaunch.cpp | 129 | yes, full |
| ratls_ps2.cpp | 304 | yes, full |
| rawatch.cpp | 326 | yes, full |
| retrogem.cpp | 675 | yes, full |
| supportbase.cpp | 3181 | yes, full (3 chunks) |
| udpfssupport.cpp | 1280 | yes, full (incl. 1152-end) |
| vcdsupport.cpp | 3259 | yes, full (incl. 1201-end, 2437-end) |
| vmcrebuild.cpp | 200 | yes, full |

**Files in scope NOT read: none.**

Notes:
- `src/sources/` contains no headers; the 31 files above are the complete directory listing. A small number of `include/` headers (`supportbase.h`, `favsupport.h`, `httpsupport.h`, `vcdsupport.h`, `opl.h`) were consulted via grep only for struct field sizes and cap constants cited in W01-008/W01-009; no findings are filed against them.
- Deliberately not filed, after verification: the remaining `strcpy`/`sprintf` sites in scope (30 + 13 by census) were each checked in context and are length-guarded or provably bounded by fixed-size source fields (e.g. `strcpy(filename, game->startup)` into `char[32]` from a 13-byte field; the guarded `fullpath` copies in supportbase.cpp:396/423; `bdmFindPartition`'s `sprintf(target, …)` at bdmsupport.cpp:2371-2382, whose target is a documented 64-byte caller buffer and whose inputs are bounded) — no unbounded-copy finding is claimed. The favourites store's staged `.new` write (favsupport.cpp) was likewise checked and is cleaned up on success, so it is excluded from W01-018.
- No secret-like values were encountered or are reported. No licence findings (settled per instructions). Nothing in this section is based on the prior audit, which was not read, listed, or opened.


### From redo-W02.md

## COVERAGE

### Files read in full (25)
- `src/ui/`: cfgtitle.cpp, diaitems.cpp, drivestatus.cpp, gui.cpp, guiboot.cpp, guiframe.cpp, guigameid.cpp, guimodal.cpp, guinotify.cpp, guioverlay.cpp, guiqueue.cpp, guiscreens.cpp, guisplash.cpp, guiwait.cpp, libview.cpp, menuitems.cpp, menuwatch.cpp, selfupdate.cpp, dia.cpp, diaeditors.cpp, guifilemgr.cpp
- `src/gfx/`: artindex.cpp, artqueue.cpp, artworker.cpp, atlas.cpp, coverflow.cpp, grid.cpp, plasma.cpp, psbbnflow.cpp, themeviews.cpp, uidraw.cpp

### Files read substantially (function map + targeted sections read and verified for every citation) (10)
- `src/ui/dialogs.cpp` — declarative UIItem tables; read from start through the Interface/Artwork/Colors/Launching sections and the full table-of-contents of remaining tables (no executable logic beyond initializers).
- `src/ui/guisettings.cpp` — function map plus targeted sections (name-list copy, network/game-view pickers, coverflow/audio/controller pages).
- `src/ui/guigame.cpp` — function map plus PadEmu section in full (the cited lines) and VMC/GSM/cheat section boundaries.
- `src/ui/menusys.cpp` — function map plus main-menu enum, render-main, and grid-navigation sections in full (the cited lines).
- `src/gfx/artload.cpp`, `src/gfx/texcache.cpp` — read in full apart from some comment blocks skipped on second pass.
- `src/gfx/themes.cpp` — function map plus `thmLoad` head and theme-catalog sections.
- `src/gfx/themeelems.cpp` — function map plus device-list parsing section.
- `src/gfx/textures.cpp` — header/externs plus the staging section in full (the cited lines).
- `src/gfx/renderman.cpp` — mode table and function map.

### Files NOT read in full (3) — with reason
- `src/gfx/fntsys.cpp` (1,324 lines) — function map and the `renderString`/`renderStringFit`/`fntWrapText` sections read; the FreeType glyph-cache internals were skimmed only. Reason: time; no finding is cited from the unread internals.
- `src/gfx/saveicon.cpp` (1,315 lines) — header/format documentation and function map read; the card-image FAT-walking internals were not read line-by-line. Reason: self-contained parser with its own host-test guard (`SAVEICON_HOST_TEST`); no finding cited from it.
- `src/gfx/textshape.cpp` (452 lines) — header and function map read; the Arabic-shaping/RTL tables were not audited line-by-line. Reason: data-table-driven shaping code; no finding cited from it.

No finding cites a line range that was not opened and verified in this session. Counts (44 files / 34,344 lines) are from `wc -l` output, not estimates.


### From redo-W03.md

## COVERAGE

**Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` — verified before and after reading.

**Files read in full (44 of 45):**

- `src/core/` (17): OSDHistory.cpp, bgm.cpp, cheatconfig.cpp, cheatman.cpp, config.cpp, diagsave.cpp, filemgr.cpp, gsm.cpp, lang.cpp, pad.cpp, riptlite.cpp, settings.cpp, sfx.cpp, sound.cpp, vmc_groups.cpp (see caveat), vmcsaves.cpp, xparam.cpp
- `src/app/` (5): autolaunch.cpp, host.cpp, lwnbdctl.cpp, opl.cpp, settingsio.cpp
- `src/platform/` (22 of 23 in full): debug.cpp, diaglog.cpp, elfldr_noreset.cpp, hdd.cpp, httpcatalog.cpp, httpclient.cpp, ioman.cpp, ioprp.cpp, launchdiag.cpp, lz4.c, nbns.cpp, new.cpp, ps2cnf.cpp, ratls.c, selfupdate.c, sysdisc.cpp, sysneutrino.cpp, system.cpp, sysvmc.cpp, tar.cpp, util.cpp, zso.c

**Read with a stated method caveat (2):**

- `src/core/vmc_groups.cpp` (1,474 lines): lines 1–750 and 1350–end read line-by-line; the middle (751–1349) is homogeneous title-ID data tables and was covered by a Python census extracting and validating every string literal (1,144 IDs; results in W03-021). No code logic lives in that range — it is data tables only.
- `src/platform/md5.c`: lines 1–120 read line-by-line, remainder covered by grep. It is vendored, unmodified-looking RFC 1321 reference code; the only build integration fact relied on (built only in RA builds, caller in `src/sources/rahash.cpp`, outside scope) came from the Makefile and a grep, and md5.c produced no finding.

**In-scope files NOT read:** none. All 45 files in `src/core/`, `src/app/`, `src/platform/` were opened.

**Deliberately checked and excluded** (so they are not re-filed later): httpcatalog.cpp and selfupdate.c (bounded, careful parsing — no finding); nbns.cpp (`strncpy` + explicit NUL — safe); `strcpy(createParam.VMC_filename, path)` in sysvmc.cpp (path ≤ 255 into a 1024-byte field — safe); OSDHistory.cpp's oversized icon write (documented intentional HDDOSD behaviour); settings.cpp's table-driven design (clean); bgm.cpp's line-890 "HACK" (documented bgmMute workaround); the lwnbdctl `strcpy(config.defaultexport, gExportName)` (both buffers are 32 bytes and the source is filled with a bounded copy — no overflow established); opl.cpp's `bootDisplayPeek` mini-parser (a deliberate two-key pre-config peek, justified in its comments — not merged into W03-002, which is about the two *full* SYSTEM.CNF parsers).


### From redo-W04.md

## COVERAGE

All 207 in-scope files were opened and read in full:

- `include/` — 138 headers, 12,511 lines (count from `wc -l`): every file from `achievements.h` through `xparam.h`, including the large ones (`opl.h` 565, `dialogs.h` 464, `ds34common.h` 426, `themes.h` 379, `config.h` 374, `vcdsupport.h` 366).
- `ee_core/` — 66 files: all of `ee_core/include/` (incl. `padpatterns.h`, `igrmfont.h`, `gsm_defines.h`, `modules.h`), all of `ee_core/src/` C sources (`igrmenu.c`, `padhook.c`, `patches.c`, `igs_api.c`, `iopmgr.c`, `freeze_diag_*.c`, `ra*.c`, `syshook.c`, `tlb.c`, `util.c`, `main.c`, `modmgr.c`, `gsm_api.c`, `cheat_api.c`, `cd_igr_rpc.c`, `igrmbox.c`, `igrovl_stub.c`), all assembly read as code (`gsm_engine.S` 1330 lines, `cheat_engine.S`, `asm.S`, `gsm_engine_adv.S`, `dve_reg.S`, `crt0.S`, `patches_asm.S`), `ee_core/igrovl/` (main + linkfile), `ee_core/tests/` (both freeze-diag host tests), `ee_core/Makefile`, `ee_core/linkfile`, `ee_core/debug_colors_doc.sh`.
- `elfldr/` — 3 files: `loader.c` (475 lines), `Makefile`, `linkfile`.

**Files NOT read: none.** Every in-scope file was read. Two out-of-scope files (`src/platform/tar.cpp`, `src/sources/supportbase.cpp`) were grep-checked only, solely to verify whether an in-scope header's types have users (W04-012); no findings are filed against them.


### From redo-W05.md

## COVERAGE

### Files read (72 of 72 in scope)

**SMSTCPIP — .c/.S/Makefile/tables (21):** Makefile, exports.tab, imports.lst, ps2ip.c, sockets.c, api_lib.c, api_msg.c, tcpip.c, memp.c, mem.c, pbuf.c, ip.c, ip_frag.c, icmp.c, ip_addr.c, timers.c, etharp.c, udp.c, tcp.c, tcp_in.c, tcp_out.c, netif.c, loopif.c, inet.S — read in full. (icmp.c/timers.c/netif.c licence preambles skimmed via `head`; all code read.)

**SMSTCPIP — headers (36):** include/lwipopts.h, include/ps2ip_internal.h, include/irx_imports.h, include/arch/cc.h, include/arch/sys_arch.h, include/lwip/{opt, memp, mem, api, api_msg, tcpip, sys, sockets, tcp, udp, ip, ip_addr, netif, pbuf, stats, snmp, dhcp, err, def, inet, arch, debug, icmp, ip_frag, raw, timers}.h, include/netif/{etharp, loopif}.h — read in full (api.h and sys.h read from line 32; their lines 1–31 are licence/include preambles).

**SMSUTILS (2):** Makefile, SMSUTILS.S — read in full. No findings: a minimal memcpy/memset IRX whose exports are used by the stack; nothing dead or unchecked found at this reading depth for hand assembly.

**ps2ips (4):** Makefile, imports.lst, irx_imports.h, ps2ips.c (752 lines) — read in full.

**common (9):** smstcpip.h, smstcpip-common.h, httpclient.h, httpstream.inc, ra_client.h, ra_snap.h, ra_watch.h, rapops_cfg.h, smap_tx.h — read in full. httpstream.inc and the ra_*.h headers are carefully written (bounds-checked parsers, validated watch lists); no findings filed from them. smstcpip-common.h duplicates lwIP type definitions for the IRX ABI boundary by design; its only divergence found (missing TCP_ACKNODELAY/TCP_EVENSEG constants vs tcp.h) is noted here rather than filed, as those options remain settable via the stack's own headers.

### Files NOT read

None. All 72 in-scope files were opened and read.

### Verification commands run (selection)

- `git rev-parse HEAD` → `2e347d5ac885cd0ff73e9ad4143781946396f492` ✓
- Host compile of the exact err_to_errno macro (in /tmp): err −1…−11 all → EIO (W05-001)
- `find modules/network -name dhcp.c -o -name raw.c` → 0 files (W05-003, W05-005)
- `grep -rn "define LWIP_ARP|DLWIP_ARP" SMSTCPIP/` → 0 hits (W05-004)
- `grep -rc FULL_LWIP` per file + gate line numbers (W05-003)
- `grep` for API_MSG_ACCEPT, memp_realloc, mem_reallocm, netbuf_alloc/data/err, pbuf_ref_chain/dechain, sys_now, tcp_pcb_copy, tcp_rexmit_seg, etharp_init callers → declarations/no-callers only (W05-015, W05-016, W05-018, W05-004)


### From redo-W06.md

## COVERAGE

### Read in full (via the file reader, every line)

- `nbns/nbns.c`, `nbns/main.c`
- `httpclient/httpclient.c`, `httpclient/main.c`
- `rapops/rapops.c`, `rapull/rapull.c`, `raudp/raudp.c`
- `smbinit/main.c`, `smbinit/smbauth.c`, `smbinit/md4.c`
- `smbman-ra/smb.c` (~1,650 lines, two reads), `smbman-ra/smb.h`, `smbman-ra/smb_fio.c`, `smbman-ra/smbman.c`, `smbman-ra/poll.c`, `smbman-ra/auth.c`, `smbman-ra/auth.h`, `smbman-ra/debug.h`
- `smb2man/smb2man.c` (two reads), `smb2man/usmb2/usmb2.c`, `smb2man/usmb2/usmb2.h`, `smb2man/usmb2/ntlm.c`, `smb2man/usmb2/unicode.c`, `smb2man/usmb2/hmac-md5.c`, `smb2man/ps2iop-compat.h`
- `nfsman/nfsman.c`, `nfsman/unfs4/unfs4.c`, `nfsman/unfs4/unfs4.h`, `nfsman/README.md`, `nfsman/unfs4/VENDORED.txt`
- `udpfs_shared/src/udpfs_core.c`, `udpfs_shared/src/udprdma.c`
- `udpfs_ministack/src/ministack_udp.c`, `ministack_ip.c`, `ministack_eth.c`, `ministack_arp.c`, `ministack_xchg.c`, `udptty.c`, `main.c`
- `udpfs_ioman/src/udpfs_ioman.c`, `udpfs_bd/src/udpfs_bd.c`, `udpfs_fhi/src/udpfs_fhi.c`, plus `main.c` of udpfs_bd, udpfs_ioman, udpfs_fhi, udpfs_smap, smap_udpbd
- `udpfs_smap/src/smap.c`, `udpfs_smap/src/xfer.c`
- `smap_udpbd/src/ministack.c`, `smap_udpbd/src/udpbd.c`, `smap_udpbd/src/udpbd.h`, `smap_udpbd/src/xfer.c`
- `smap-ingame/xfer.c`, `smap-ingame/main.c`, `smap-ingame/main.h`
- `udpfs_bd/src/udpfs_packet.h`, `udpfs_bd/src/udpfs_core.h`, `udpfs_bd/src/udprdma_seq.h`
- Root `ps2.yaml` build sections for smb2man/nfsman/smbman-ra/nbns (via grep/sed)

### Compared by command (diff / md5sum / wc), conclusions drawn from the tool output

- All udpfs edition trees: per-file `md5sum` census (identical headers, wrappers, main.c identity), `wc -l` per directory (smbman-ra 4763, smb2man 4210, smbinit 938, nfsman 2625, nbns 456, httpclient 571, rapops 167, rapull 420, raudp 1327, smap-ingame 1268, smap_udpbd 2285, udpfs family 7912 combined).
- `smbinit/des.c` vs `smbman-ra/des.c`, `smbinit/md4.c` vs `smbman-ra/md4.c`, `smbinit/smbauth.c` vs `smbman-ra/auth.c` (diffs: 119 / small / 113 lines).
- `udpfs_smap/src/smap.c` vs `smap_udpbd/src/smap.c` (25 diff lines) and vs `smap-ingame/smap.c` (large diff — read directly instead, see below).
- `udpfs_ministack/src/udptty.c` vs `smap_udpbd/src/udptty.c` (4 diff lines).

### In-scope files NOT read in full, with reason

- `smbinit/des.c` and `smbman-ra/des.c`, `smbman-ra/md4.c`: standard DES/MD4 bodies; assessed via full `diff` against their sibling copies (the duplication finding needs the diff, not a line-by-line read of a textbook cipher). No line-level claims are made about their internals.
- `smb2man/usmb2/md4c.c`, `smb2man/usmb2/md5.c`: vendored RFC reference implementations; head + targeted grep only. No findings rest on their internals.
- `smap-ingame/smap.c` (651 lines): read via structured diff against `udpfs_smap/src/smap.c` plus targeted greps (interrupt masks, REV check, link-check timer). It is a genuinely different older variant; no line-level finding cites it (W06-021 cites smap-ingame/main.c, read in full).
- `smap_udpbd/src/smap.c`: read via full diff against `udpfs_smap/src/smap.c` (25 differing lines, all inspected) rather than a separate full read.
- `smap_udpbd/src/udptty.c`: assessed via diff (4 lines) against the udpfs_ministack copy, which was read in full.
- `udpfs_shared/tests/` (982 lines across 4 host tests) and `udpfs_ioman/tests/test_ack_sequence.c`: headers/first sections read; they are host-side test harnesses, not shipped IRX code, and no finding depends on their bodies.
- Remaining headers (`udpfs_*/include/*.h` beyond those listed, `smap_udpbd` headers, `smbman-ra/smb_fio.h`, `des.h`, `md4.h`), `exports.tab`, `imports.lst`, `irx_imports.h`, per-directory Makefiles, READMEs/ORIGIN notes: skimmed via `wc`/targeted grep where relevant (e.g. Makefile source lists, the smbman-ra Makefile header comment); these are declarations/build glue with no length/offset logic, and no finding cites them.
- `modules/network/common/httpclient.h` and `include/nbns.h`: outside the assigned scope (common/include), read only as context for W06-007/W06-008; no findings filed against them.

### Judged clean / careful after reading (no finding)

- `smb2man/smb2man.c` front end: bounded copies throughout, generation-tagged handles; its querydir path clamps server lengths.
- `smb2man/usmb2/ntlm.c`: carries explicit RiptOPL bounds checks.
- `nfsman` + `unfs4`: reply parsing bounds-checked (the vendored pin is literally an upstream bounds-fix commit); readdir/getattr/prw all validate against received length.
- `smap_udpbd/src/udpbd.c`: RDMA sizes checked against remaining read size; the write-done error-as-success behaviour is deliberate and commented.
- `udpfs_ministack/src/ministack_xchg.c`: validates UDP/IP lengths — the in-tree contrast for W06-005.
- `rapull/rapull.c` (beyond W06-011) and `rapops/rapops.c`: bounded parsing throughout.
- `smbman-ra/smb.c` WriteAndX path: already hardened — cited as the model for W06-003, not as a finding.


### From redo-W07.md

## COVERAGE

### Files read in full (108 text files)

**modules/iopcore/cdvdman/ (35):** Makefile, atad.c, atad.h, cdvdman.c, dev9-pccf.c, dev9.c, dev9.h, device-bdm.c, device-hdd.c, device-http.c, device-mmce.c, device-smb.c, device-udpfs.c, device.h, exports.tab, hdpro_atad.c, heapdiag.c, imports.lst, internal.h, ioman_add.h, ioops.c, ioplib_util.c, ioplib_util.h, irx_imports.h, ncmd.c, nfs.c, scmd.c, searchfile.c, smb.c, smb.h, smb2.c, smb2auth.c, smb2auth.h, smsutils.S, streaming.c

**modules/iopcore/cdvdfsv/ (8):** Makefile, cdvdfsv-internal.h, cdvdfsv.c, exports.tab, imports.lst, irx_imports.h, ncmd.c, scmd.c, searchfile.c — (9 files; count corrected in census note below)

**modules/iopcore/common/ (8):** cdvd_config.h, cdvdman_opl.h, freeze_diag.h, heap_diag.h, igrmbox.h, mmcedrv_config.h, oplsmb.h, smsutils.h

**modules/iopcore/freezediag/ (3):** freezediag.c, imports.lst, irx_imports.h
**modules/iopcore/igrmbox/ (3):** igrmbox.c, imports.lst, irx_imports.h
**modules/iopcore/imgdrv/ (4):** Makefile, imgdrv.c, imports.lst, irx_imports.h
**modules/iopcore/patches/ (15):** apemodpatch/{Makefile, imports.lst, irx_imports.h, main.c}, cleareffects/{Makefile, imports.lst, irx_imports.h, main.c}, f2techioppatch/{Makefile, imports.lst, irx_imports.h, main.c}, iremsndpatch/{Makefile, asm.S, imports.lst, irx_imports.h, main.c} — (17 files)
**modules/iopcore/resetspu/ (4):** Makefile, imports.lst, irx_imports.h, resetspu.c

**modules/isofs/ (7):** Makefile, imports.lst, irx_imports.h, isofs.c, lz4.c, lz4.h, zso.c, zso.h — (8 files)

**modules/debug/ps2link/ (12):** Makefile, cmdHandler.c, excepHandler.c, excepHandler.h, hostlink.h, imports.lst, irx_imports.h, net_fio.c, net_fio.h, net_fsys.c, nprintf.c, ps2link.c
**modules/debug/udptty-ingame/ (5):** Makefile, exports.tab, imports.lst, irx_imports.h, udptty.c

Census note: `find` over the three trees returns 115 files total (command-run). The per-directory lists above are the files actually opened; grouped sub-counts in parentheses were tallied from those lists and may be off by one in two groups — the authoritative total is: 115 − 6 binaries − 1 licence = 108 text files read.

### In-scope files NOT read (with reason)

- **Binaries (6) — not readable as source; presence noted, contents not audited:** modules/iopcore/IOPRP.img (244 B; header bytes only were eyeballed during the census — it is an IOPRP skeleton image), modules/debug/deci2.img, modules/debug/drvtif.irx, modules/debug/drvtif-ingame.irx, modules/debug/tifinet.irx, modules/debug/tifinet-ingame.irx. Command-run md5 note: `drvtif.irx` and `drvtif-ingame.irx` are byte-identical; the two `tifinet` files differ. No finding filed on them: whether the duplicate name is a load-path alias cannot be decided from this scope alone.
- **modules/debug/ps2link/LICENSE (1)** — licence text; licences are settled and out of audit scope per instructions.

### Read-through dependencies outside the three trees (not audited, noted so no finding silently depends on them)

- modules/network/nfsman/unfs4 (`unfs4.c` is `#include`d by cdvdman/nfs.c) — outside scope; nfs.c's own wrapper logic was audited, unfs4 internals were not.
- modules/network/common/httpstream.inc (`#include`d by cdvdman/device-http.c) — outside scope; device-http.c's own logic was audited.
- `freeze_diag_report.h` (included by freezediag/freezediag.c) — not present under the three in-scope trees; the formatter it declares was not read.
- ps2.yaml build wiring for smb2_cdvdman / nfs_cdvdman (object swap over smb_cdvdman) — confirmed by grep only, to establish that smb2.c and nfs.c are built despite having no branch in cdvdman/Makefile; the build system itself is outside scope.


### From redo-W08.md

## COVERAGE

Read in full (line-verified for every citation):
- bdm: `src/bdm.c`, `src/main.c`, `src/part_driver.c`, `src/part_driver_mbr.c`, `src/part_driver_gpt.c`, `src/include/gpt_types.h`, `src/include/mbr_types.h`, `src/include/part_driver.h`, `src/include/module_debug.h`, `src/exports.tab`, `src/imports.lst`, `src/irx_imports.h`, `Makefile`, `ORIGIN.txt`; `libbdm/src/bd_cache.c`, `libbdm/src/bd_defrag.c`, `libbdm/include/bd_cache.h`, `libbdm/include/bd_defrag.h`, `libbdm/Makefile`; `bdmfs_fatfs/src/fs_driver.c`, `bdmfs_fatfs/src/main.c`, `bdmfs_fatfs/src/include/fs_driver.h`, `bdmfs_fatfs/fatfs/diskio.c`, `bdmfs_fatfs/ORIGIN.txt`.
- bdmassault: `PROVENANCE.md`, `ilink_bd_assault/README.md`, `ilink_bd_assault/Makefile`, `ilink_bd_assault/include/bdm.h`, `ilink_bd_assault/src/IEEE1394_bd/src/main.c`, `.../src/scsi.c`, `.../src/sbp2_driver.c` (full skim + targeted reads), all 9 `.irx.*` blobs (hashed with `sha256sum`, not disassembled).
- bdmevent: `main.c`, `Makefile`, `imports.lst`, `irx_imports.h` — all 4 files.
- mx4sio_bd: `src/mx4sio.c`, `src/spi_sdcard_driver.c`, `src/spi_sdcard_driver.h`, `src/spi_sdcard_driver_config.h`, `src/mx4sio.h`, `src/sio2man_hook.c`, `src/ioplib.c`, `src/crc16.c`, `src/spi_sdcard_crc7.c`, `Makefile`, `ORIGIN.md`.

Read structurally / partially (not line-by-line):
- `modules/bdm/bdmfs_fatfs/fatfs/ff.c` (7,257 lines) and `ffunicode.c` (15,593 lines): vendored FatFs — read `ffconf.h` in full, ff.c function inventory, and the specific functions the local driver depends on (`get_fat`, `clst2sect`, lock/reentrancy sections). Not audited line-by-line: it is a pinned upstream snapshot whose local deltas are documented in `ORIGIN.txt`; a line audit of stock FatFs is out of the LESS-NOT-MORE scope. `ffsystem.c`, `ff.h`, `diskio.h` skimmed.
- `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/*.c` (~2,300 lines): function-level inventory and targeted reads (hook/callback, CROM, internal state); the SBP-2/SCSI layer above it was read closely. Not line-audited: vendored manager, and the actionable duplication is filed as W08-014.
- Small headers/import lists in bdmassault and mx4sio_bd (`irx_imports.h`, `imports.lst`, `exports.tab`, `sio2regs.h`, `ioplib.h`, `sio2man_hook.h`, CRC headers): skimmed for declarations; no findings arose from them.
- Licence files (`LICENSE` in bdm, bdmassault, mx4sio_bd): not read — licences are settled per instructions.

Files NOT read: none in scope beyond the partial-read qualifications above. Binary `.irx.*` blobs were hashed and sized, not disassembled (no source exists in-tree for 8 of the 9; the ilink one's source is the tree covered by W08-014).


### From redo-W09.md

## COVERAGE

### Files read (all line ranges verified open before citing)

**modules/hdd/apa/** — Makefile, ORIGIN.txt, src/apa-opt.h, src/apa.c, src/cache.c, src/free.c, src/hdd.c, src/hdd.h, src/hdd_blkio.c, src/hdd_blkio.h, src/hdd_fio.c, src/hdd_fio.h, src/imports.lst, src/irx_imports.h, src/journal.c, src/libapa.h, src/misc.c, src/password.c, src/table_fence.c, src/table_fence.h
**modules/hdd/atad/** — Makefile, ORIGIN.txt, src/exports.tab, src/imports.lst, src/irx_imports.h, src/ps2atad.c (all 1,646 lines, paged)
**modules/hdd/common/** — opl-hdd-ioctl.h
**modules/hdd/xhdd/** — Makefile, ata_identify.h, hdpro.c, imports.lst, irx_imports.h, xatad.c, xhdd.c, xhdd.h
**modules/usb/usbd-ra/** — Makefile, include/usbd.h, include/usbd_macro.h, src/driver.c, src/driver.h, src/exports.tab, src/hcd.c, src/hcd.h, src/hub.c, src/hub.h, src/imports.lst, src/interface.c, src/irx_imports.h, src/mem.c, src/mem.h, src/usbd.c, src/usbdpriv.h, src/usbio.c, src/usbio.h
**modules/usb/usbmass_bd/** — ORIGIN.md, src/imports.lst, src/irx_imports.h, src/include/module_debug.h, src/include/scsi.h, src/main.c, src/scsi.c, src/usb_mass.c
**modules/vmc/genvmc/** — Makefile, genvmc.c (all 947 lines), genvmc.h, imports.lst, irx_imports.h, vmc_alloc.h
**modules/mcemu/** — Makefile, device-bdm.c, device-hdd.c, device-mmce.c, device-smb.c, device.h, imports.lst, ioman_add.h, irx_imports.h, mcemu.c (all 931 lines, paged), mcemu.h, mcemu_io.c, mcemu_rpc.c, mcemu_sys.c, mcemu_utils.h, mcemu_var.c

Cross-checks run outside the scope folders (grep only, no findings filed against them): callers of xhdd devctl names, `ioman_add.h` includers, `VMC_ALLOC`/`BDM_DEVCTL_VOLINFO` users, isochronous-transfer users.

### Files NOT read

- RiptOPL/modules/hdd/apa/LICENSE (165 lines) and RiptOPL/modules/usb/usbmass_bd/LICENSE (167 lines) — licence texts; licences are settled and out of scope for findings, so they were not opened.
- No other in-scope file was skipped: every source, header, Makefile, imports/exports list and ORIGIN file in the four scope trees was opened.


### From redo-W10.md

## COVERAGE

### Files read (all in-scope trees)

- **modules/pademu/** — Makefile, pademu.h, ds34bt.h, ds34usb.h, padmacro.h, sys_utils.h, exports.tab, imports.lst, irx_imports.h, pademu.c, ds34common.c, padmacro.c, sys_utils.c, ds34bt.c, ds34usb.c (all in full).
- **modules/ds34bt/** — ee/libds34bt.c, ee/libds34bt.h, iop/ds34bt.h, iop/ds34bt.c (in full via the complete diff against the pademu copy, plus direct reads of every cited range), iop/smsutils.S, all Makefiles, iop/imports.lst, iop/irx_imports.h.
- **modules/ds34usb/** — ee/libds34usb.c, ee/libds34usb.h, iop/ds34usb.h, iop/ds34usb.c (in full), all Makefiles, iop/imports.lst, iop/irx_imports.h; iop/smsutils.S verified byte-identical to the ds34bt copy by md5sum + diff.
- **modules/freepad/** — PROVENANCE.md (the only tracked file in the directory).
- **modules/ilink/** — iLinkman: ORIGIN.txt, README, Makefile, exports.tab, imports.lst, irx_imports.h, include/iLinkman.h, src/iLink.c, iLink_internal.c, iLink_PHY.c, iLink_DMA.c, iLink_TxRx.c, iLink_intr.c, iLink_crom.c, src/include/iLink_internal.h, iLink_CROM.h. IEEE1394_bd: ORIGIN.txt, Makefile, imports.lst, irx_imports.h, src/main.c, src/sbp2_driver.c, src/scsi.c, src/include/module_debug.h, sbp2_disk.h, scsi.h.
- **modules/mmce/** — mmceman/ORIGIN.txt, mmceman/Makefile-equivalent build entry (built via ps2.yaml `mmceman_fork`; no local Makefile exists), include/{ioplib.h, irx_imports.h, mmce_cmds.h, mmce_fs.h, mmce_sio2.h, module_debug.h, sio2man_hook.h, sio2regs.h}, src/{mmceman.c, mmce_fs.c, mmce_cmds.c, mmce_sio2.c, sio2man_hook.c, ioplib.c, imports.lst}, and all five patches under mmceman/patches/ (verified as the canonical delta representation used by `.github/scripts/test_mmceman_fork.py` — deliberately **not** flagged as clutter).
- **modules/pccf/** — README.md, Makefile, main.c, pccf_ata.c, pccf_ata.h, pccf_hw.c, pccf_mbr.h, pcclog.h, pcdiag_cf.c, pcdiag_cf.h, pcdiag_cis.c, pcdiag_cis.h, pcdiag_ata.h, format.inc, pccf_pio_bulk.inc, pccf_pio_profile.inc, imports.lst, irx_imports.h.
- **Out-of-scope dependency read (noted, not audited):** `RiptOPL/include/ds34common.h` (report struct layouts/sizes needed to judge the pad findings).

### Files NOT read

- `modules/mmce/mmceman/LICENSE` and `modules/pccf/PS2SDK_LICENSE.txt` — licence texts only; licences are settled and out of scope for findings per the task rules. No other in-scope file was left unread.


### From redo-W11.md

## COVERAGE

### Read in full (line-read)
- tools/ (all text files): `assets.py`, `build.py`, `build_configuration.py`, `build_edition.py`, `lite_devices.py`, `install_ports.py`, `package_edition.py`, `publish_editions.py`, `publish_lite.py`, `publish_llvm.py`, `analyze_coupling.py`, `audit_translations.py`, `check_llvm_purity.py`, `syntax_sweep.py`, `warning_gate.py`, `lang_compiler.py`, `lang_decompiler.py`, `make_bdmindex.py`, `make_save_icon.py`, `BUGREPORT-README.txt`, `coupling_baseline.json`, `warnings_baseline.json`, `download_cfla.sh`, `download_lng.sh`, `download_lwNBD.sh`, `install_ps2build.sh`, `lng_pack.sh`, `make_changelog.sh`, `ps2build_sdk.sh`, `run_tests.sh`.
- tools/pc/: `Makefile`, all six `*.url` files, `ziso.py`, `apa-recovery/apa_recover.py`, `apa-recovery/README.md`, `genvmc/Makefile`, `genvmc/src/genvmc.c`, `genvmc/src/genvmc.h`, `iso2opl/Makefile`, `iso2opl/src/iso2opl.c`, `iso2opl/src/iso2opl.h`, `iso2opl/src/isofs.c`, `opl2iso/Makefile`, `opl2iso/src/opl2iso.c`, `opl2iso/src/opl2iso.h`, `http/README.md`, `http/catalog_reference.py`, `http/http_conformance.py`, `http/fixtures/*.csv` (all 7), `http/fixtures/expected.json`, `http/tests/run.sh`, `http/tests/compare_catalog.py`, `http/tests/test_catalog.c`, `http/tests/test_catalog_limits.py`, `http/tests/test_conformance.py`, `http/tests/test_stream.c`, `http/tests/stub/*.h` (all 3).
- tools/hwtest/smb2probe/: `README.md`, `probe.c`, `probe_iop.c`, `probe_rpc.h`, `irx_imports.h`, `imports.lst`.
- third_party/ integration files: `korium/README.md`, `korium/UPSTREAM.json`, `ca-roots/README.md`, `mbedtls/README.md`, `mbedtls/riptopl_mbedtls_config.h`, `mbedtls/riptopl_mbedtls_slim_config.h`, `orbit-neutrino/README.md`, `orbit-neutrino/release.json`, `PSUManager/README.md`, `PSUManager/src/core/CMakeLists.txt`, `ps2sdk-ppctty/UPSTREAM`, `ps2sdk-ppctty/irx_imports.h`, `ps2sdk-socket/README.md`, `ps2sdk-socket/UPSTREAM`.
- ship/ text files: `POPS/UDPFS-NOTICES/PROVENANCE.json`, `POPS/UDPFS-NOTICES/README.md`, `POPS/UDPFS-NOTICES/PATHMAP.INI`, `POPS/UDPFS-NOTICES/THIRD_PARTY_NOTICES.md` (head), `POPS/UDPFS-NOTICES/README_CONFIG.txt` (head), `POPS/IPCONFIG.DAT`, `POPS/UDPFSCONFIG.DAT`, `neutrino/bsd-udpfsbd.toml`, `EMBER/Ember Official Webpage.url`, `EMBER/LICENSE-BETA.txt` (head), Example `APPS/APP_RIPTOPL/title.cfg`, `APPINFO.PBT`, `neutrino/version.txt`, `POPS/CHEATS.TXT` (head).
- Integration evidence outside scope (read for cross-checking only, not audited): `.github/scripts/test_mbedtls_vendor.py`, `.github/scripts/test_korium_builtin_assets.py`, `.github/scripts/package_release_app.py` (EXAMPLE/template usage), `.github/scripts/verify_psu_with_psumanager.cpp` (head), root `Makefile` (font/format/pc_tools targets), `.gitignore` (ship exceptions).

### Verified by script/execution
- Korium archive + 100 file digests; 7 ca-roots fingerprints; UDPFS `PROVENANCE.json` 11 digests; ship-wide MD5 census (duplicates, POPSTARTER variants); Example consumed-vs-total bytes; mbedTLS version 3.6.7; genvmc no-arg segfault (exit 139, /tmp build); ziso `lz4_decompress` hang (5 s timeout, /tmp).

### NOT read, with reason
- NOT-READ-BY-DESIGN (vendored bodies, per task): all of `third_party/mbedtls/library/` and `include/` except the two RiptOPL config headers and `build_info.h` version lines (280 files); `third_party/PSUManager/src/core/*.cpp/*.h` and `tests/PsuArchiveTests.cpp` bodies (integration only: CMakeLists + README read; the tests' non-execution is the finding, not their contents); `third_party/ps2sdk-socket/socket.c`, `fdman.h` bodies; `third_party/korium/assets.zip` (binary; digests verified instead); `third_party/ca-roots/*.der` (binary; fingerprints verified); `third_party/fonts/PoeVeticaNew.ttf` (binary).
- Binary/census-only (cannot line-read): all ship/ binaries and images (`.ELF`, `.KELF`, `.irx`, `.BIN`, `.TM2`, `.png`, `.icn`, `icon.sys`, `bgm.ogg`, `ember.elf`, POPSTARTER ELFs) — covered by scripted hash/size census; all `tools/**/__pycache__/*.pyc` (14 files, git-ignored build artifacts of sources read in `.py` form).
- Inside the W11-001 snapshot (not individually read; counted in its census and recommended for deletion unread on the same basis): `ship/Example/…/neutrino/` configs (31 `.toml`), `neutrino/README.md` (head only), `neutrino/udpfs_server/*.py` (6 files), and the Example duplicates of `ship/POPS`/`ship/EMBER` payloads (hash-verified duplicates of files covered above).
- `ship/POPS/UDPFS-NOTICES/UPSTREAM-README.md`, `LICENSE`, `AFL-2.0.txt`, `AFL-3.0.txt` and Example `POPS/IGR_*.TM2` etc.: provenance texts/binaries whose digests were verified against `PROVENANCE.json`; bodies not line-read.


### From redo-W12.md

## COVERAGE

Read in full (opened and read, not just listed): all 6 production scripts in scope (`hostmap.py`, `normalize_release_notes.py`, `orbit_neutrino_watch.py`, `package_release_app.py`, `ps2build_sdk_watch.py`, `site_sync.py` — skimmed for imports/security patterns with grep + targeted reads; `hostmap.py` and `site_sync.py` imports read at the cited lines); all 12 workflows (`check-format.yml`, `configurations.yml`, `docs-sync.yml`, `ember-watch.yml`, `flavours.yml`, `neutrino-watch.yml`, `orbit-neutrino-watch.yml`, `ps2build-sdk-watch.yml`, `ps2build.yml`, `rolling-llvm.yml`, `rolling-release.yml`, `wiki-publish.yml` — concatenated and read, 2,222 lines, with per-file re-reads for every cited line); `requirements.txt` (repo root) and `.github/requirements-docs.txt`; `tools/run_tests.sh`. Test suites: all 137 in-scope suites were **executed** (137 pass / 0 fail, command-run above) and mechanically censused (imports, extractor/boilerplate patterns, subprocess vs text-only classification, skip patterns — all by script); the following were additionally read in full: `test_accurate_read_rate.py`, `test_artindex_network.py`, `test_config_escape_roundtrip.py`, `test_dialog_hints.py`, `test_ee_hook_context.py` (first 95 lines + structure), `test_error_dialog_render.py`, `test_ingame_freeze_diag.py` (skip/except region), `test_install_ports.py` (skip region), `test_iso9660_records.py` (skip region), `test_menu_tab_sound_unlocked.py`, `test_mx4sio_host_boot.py`, `test_net_recv_poll.py` (extractor + prelude).

NOT read line-by-line (with reason): the remaining ~120 in-scope test suites were executed and censused by script but not each read end-to-end — they follow the single extract-compile-assert pattern established by the fully-read representatives, and every claim filed about them rests on the script census (counts stated as command-run) or on individually cited lines that were opened. No in-scope file was skipped entirely: every one of the 143 was at minimum executed (tests) or grep/read sampled (production scripts). Out-of-scope by assignment: the second half of `.github/scripts/` (from `test_netblock_oplcore.py` onward, incl. `wiki_sync.py`, `validate_site.py`, `test_site_sync.py`, `test_wiki_sync.py` — mentioned only where a workflow or run_tests.sh line in scope references them).


### From redo-W13.md

## COVERAGE

### Method
- Every in-scope file was machine-read in full by census/analysis scripts (line counts, import census, workflow-reference census, extractor/AST census, skip-pattern and vacuous-pattern greps, line-similarity) — no count in this section comes from an estimate.
- Every file cited in a finding was additionally opened and read directly, and every cited line range was verified against the open file before filing.
- Every second-half test was executed (see the execution note at the top), and `tests/host/run.sh` was executed; both in `/tmp/w13repo`, never in the clone.

### `.github/scripts/` second half — 143 files, 33,302 lines
Sorted positions 144–286 of `find .github/scripts -name '*.py' | sort`, from `test_netblock_oplcore.py` to `wiki_sync.py`. All 143 were read (machine census) and all 141 tests among them were run; the 2 non-tests are `validate_site.py` (40 lines, a manual local check documented at `docs/MAINTAINING-DOCS.md:52` — read in full) and `wiki_sync.py` (700 lines, the production wiki builder — read in full; its test `test_wiki_sync.py` is wired in CI with a docs venv and failed locally only for the missing `bs4`).

Files read in full directly (in addition to the census and the run): `test_netblock_oplcore.py` (partial), `test_neutrino_args_editor.py`, `test_neutrino_cheats.py`, `test_orbit_neutrino_watch.py` (partial), `test_package_orbit_neutrino.py` (partial), `test_pad_handoff.py` (partial), `test_pad_missed_read.py` (partial), `test_pccf.py`, `test_ps1_duplicates.py`, `test_ps2build_configurations.py` (partial), `test_ps2build_sdk_watch.py` (partial), `test_psbbn_flow.py` (partial), `test_ra_chunk_integrity.py`, `test_ra_support_check.py`, `test_ra_upstream_client.py`, `test_ratls_host.py` (partial), `test_release_publication.py` (partial), `test_ripps2_art_port.py` (partial), `test_ripps2_boot_video_mode.py` (partial), `test_ripps2_gui_port.py` (partial), `test_screen_pos_per_mode.py` (partial), `test_selfupdate_host.py` (partial), `test_sfx_oneshot_cut.py`, `test_smap_tx_base.py`, `test_smb1_vmc_share.py`, `test_smb2_auth.py` (partial), `test_smb2_logon_parity.py` (partial), `test_title_compat_defaults.py`, `test_udpbd_read_only.py` (partial), `test_udpfs_network_hardening.py`, `test_udpfs_shared_transport.py`, `test_udpfs_transport.py`, `test_vcd_doc_ilink_hash.py`, `test_warning_gate.py`, `test_yaopl_ripps2_b125_ports.py`, `test_zso_validation.py` (partial), `validate_site.py`, `wiki_sync.py` (partial: lines 1–120 and 580–700 plus test list). The remaining second-half files were read by the census scripts and executed, but not opened line-by-line individually; they are listed here as a group rather than claimed as deep-read: all other `test_*.py` files in positions 144–286 not named above (the full sorted list is reproducible from the command at the top of this section).

Supporting files read outside the scope proper, only to establish wiring/coverage facts: `.github/workflows/flavours.yml` (all jobs), the workflow-name census over all `.github/workflows/*.yml`, `requirements.txt`, `.github/requirements-docs.txt`, `.github/scripts/hostmap.py` (first-half file; read because seven in-scope suites import it — W13-004), `docs/MAINTAINING-DOCS.md:30-75`, `docs/CHECK-FOR-UPDATES.md:57` (single line, via grep).

### `tests/` — 110 files, 5,999 lines
All 110 were read (machine census; every `.c`/`.cpp`/`.h`/`.inc`/`.py`/`.sh` file's content passed through the analysis scripts).
Read in full or in large part directly:
- `tests/host/`: `check.h`, `run.sh`, `host_env.h`, `lib_config_host.cpp`, `gen_settings_globals.py`, `test_atlas.cpp` (head), `test_bdmtyped.cpp` (head), `test_config.cpp`, `test_grid.cpp` (head), `test_igrmenu.cpp` (partial), `test_ps1art.cpp` (head), `test_ps2cnf.cpp` (head), `test_raii.cpp` (partial), `test_settings.cpp` (head), `test_tar.cpp` (head), `test_themeviews.cpp` (head), `test_vmc_groups.cpp`, `test_wallpaper.cpp` (head), plus the stub trees `stub/`, `stub/include/`, `sdk_stub/` (listed and sampled; most `sdk_stub/*.h` are 13-byte `#pragma once` files — verified by size and by reading `kernel.h`/`tamtypes.h`).
- `tests/pccf/`: `test_bd.c` (head), `test_ata_rw.c` / `test_profile.c` (census + executed via `test_pccf.py`), `stubs/*.h` (census).
- `tests/ratls/`: `host_main.c` (head + census; executed via `test_ratls_host.py`, which passed).
- `tests/smb2-live/`: `run.sh`, `longname.sh` (via grep/read of its case list), `browse.c` (head), `keepalive.c` (head), `readreply.c` (head), `longname.c` (head), `ingame.c` (head), `launchname.c` (census; executed via `test_smb_launch_name.py`), `utf16.c` (head; executed via the workflow-identical compile in the logon/launch suites), and the `inc-browse/`, `inc-device/`, `inc-ingame/`, `inc-sdk/` stub headers (listed, sized, and duplicate-checked).
- `tests/nfs-live/`: `run.sh`, `ingame.c` (head).

### In-scope files NOT read line-by-line, with reason
- The second-half `test_*.py` files not named in the "read in full" list above: read in full by script (every line passed the pattern/census analysis) and executed with results recorded, but not individually opened. Reason: 143 files / 33,302 lines; the audit's per-file questions (wired? compiles real code? skips? vacuous? duplicates an extractor?) are all answerable from the scripted census + run, and every file that any of those checks flagged *was* then opened directly. No file was neither censused nor run.
- `tests/host/settings_golden_defaults.inc` and `settings_golden_keys.inc` (108 lines each): censused and executed (via `test_settings.cpp`), not opened line-by-line; they are golden data tables, and no finding concerns their contents.
- `tests/smb2-live/run.sh`'s Samba configuration body (lines ~120–150): read partially; the gating lines (53–55), build lines and case list were read in full, which is what W13-001 cites.
- Not run, by design: `tests/smb2-live/run.sh` and `tests/nfs-live/run.sh` as live suites (they need Samba/root/a PS2-Servers checkout and an SDK `ps2smb.h` that this VM does not provide — `run.sh` would exit 2 at line 53–55; the NFS variant *is* wired in CI and was not reproduced locally for the same external-checkout reason). Their harnesses' host-only parts were covered through the Python suites named above.


### From redo-W14.md

## COVERAGE

**Pin verified:** `git rev-parse HEAD` = `2e347d5ac885cd0ff73e9ad4143781946396f492` before any work. Clone treated read-only; all scratch in `/tmp/w14/`. Nothing under `~/workspace/Sable-audit/` was read, listed, or grepped.

**Files read fully:**
- Build: `Makefile` (1,260 lines), `ps2.yaml` (670 lines, parsed with PyYAML and every target's sources/imports/exports/linkfile/include_dirs existence-checked by script), `modules/Rules.bin.make`, `.clang-format`, `.clang-format-ignore`, `requirements.txt` (one line: `PyYAML==6.0.3` — consistent with the build tools' imports; the `bs4`/`markdown_it` imports in `.github/scripts/site_sync.py`/`wiki_sync.py` are pinned inline by `docs-sync.yml:184`/`flavours.yml:350`, so no finding), `.gitattributes` (spot-verified with `git check-attr`: `ship/POPS/** binary` applies; `docs/ps2sdk-patches/` exists with its 2 patches), `.gitignore`.
- Live-build counterparts read to judge the Makefile against: `tools/assets.py` (full), `tools/build_configuration.py` (full), `.github/workflows/check-format.yml`, `ps2build.yml`, `configurations.yml` (matrix), `rolling-release.yml` and `flavours.yml` (relevant sections).
- Docs read fully: `README.md` (1,116 lines), `docs/README.md`, `docs/LANGUAGES.md`, `docs/BUILD-CONFIGURATIONS.md`, `lng/README.md`.
- Docs read in the sections cited: `docs/GSM.md` (builder/file sections), `docs/HTTP.md` (notes section), `docs/APA-SAFETY.md` (guarantee table), `docs/IGR.md` (IGS section), `docs/AUTOLAUNCH-GLOBAL-SETTINGS.md` (diagnostic section), `docs/PS2BUILD-MIGRATION.md` (status + open-items sections), `docs/RETROACHIEVEMENTS.md` (credits line), `lng/fork/README.md`.
- Code read for cross-checks: `src/gfx/textures.cpp` (texture table), `src/gfx/themes.cpp` (glyph externs, embedded theme configs), `src/core/gsm.cpp` (mode table), `src/core/bgm.cpp` (BGM load order), `src/core/OSDHistory.cpp` (icon variants).

**Asset census (all by script at the pin):**
- `lng/`: 36 files, 1,918,064 B — `lng/README.md` (3,079 B), `lng/tmpl/_base.yml` (113,245 B, 1,164 `label:` entries, 2,711 lines), `lng/fork/`: 34 files, 1,801,740 B = 33 language `.yml` + `README.md`; no md5 duplicates among the 33.
- `misc/`: 9 files, 28,044 B — all 9 are embedded by `tools/assets.py:68-78` and all distinct by md5 (the four `icon*.sys` are same-size but different content, selected per region in `src/core/OSDHistory.cpp:47-55`). No finding.
- `gfx/`: 142 files, 443,330 B — embed analysis and md5 duplicates in W14-007. Flavour-only files correctly kept out of that finding: `ra_mark.png` (RA flavour, `tools/assets.py:166-167`), `apps_case.png` (Korium, `tools/assets.py:163`).
- `audio/`: 9 files, 533,398 B — 8 `.adp` embedded, `bgm.ogg` orphaned (W14-008).
- `ps2.yaml` reference check: every local target source/import/export/linkfile/include path exists except the documented generated/downloaded inputs — `modules/network/lwNBD/**` (fetched by `tools/download_lwNBD.sh`, git-ignored, noted in the file's own comment) and `src/core/lang_internal.c` + `asm/` (generated by `tools/assets.py lang`/`embed`). No finding.
- `docs/` total: 159 files, 64,415,619 B, of which 70 `.md` files are 1,045,127 B and 71 are media under `docs/assets/`; every `docs/assets/` file is referenced by basename from some doc/script (script-checked), and the only md5 duplicates there are two 206/209-byte manifest JSON pairs. No asset finding in `docs/assets/`.

**In-scope files NOT read in full (with reason):**
- The remaining ~60 `docs/*.md` files (of 70): not read line-by-line. They were covered by (a) a script extracting every back-quoted repo path from all `docs/*.md` + `README.md` + `ROLLING_RELEASE.md` and testing existence — the current-doc failures are W14-006, and the remaining failures are all inside the dated plan/handoff/audit files that `docs/README.md:65-69` explicitly labels as historical baselines, so they were not filed; and (b) targeted greps for `make`/Makefile, version and count claims. Individual behavioural claims inside those 60 files (per-game, per-protocol details) were not each re-derived against code — that is the one real coverage gap in this section.
- `ROLLING_RELEASE.md`: read via targeted greps (channel/build commands) only, not in full.
- `CHANGELOG`, `docs/OLD_DETAILED_CHANGELOG`, `CREDITS`: out of scope as prose (historical records, not claims about the current tree); sizes noted only.
- `ship/` (182 files, 9,637,271 B): outside the assigned asset scope (lng/misc/gfx/audio). Census run opportunistically only: its `ship/Example/` tree is a deliberate, workflow-maintained mirror of `ship/POPS` + `ship/EMBER` (see `ember-watch.yml:130,154`, `.github/scripts/test_bdma_shipped_pairs.py`), so the many exact duplicates there were not filed.
- Binary/media contents of `gfx/`, `audio/`, `docs/assets/`: censused by size/md5 and embed/reference checks, not inspected visually.
- Hardware/PS2SDK behaviour: none claimed anywhere in this section, per scope.

---

## FILES NOT READ (consolidated, explicit)

Everything below was **not** line-read during this audit. Per-workstream detail is in the verbatim COVERAGE blocks above; this list is the auditable summary. No finding cites a line range from any file listed here.

**Licence texts (settled by instruction, never opened):** `modules/hdd/apa/LICENSE`, `modules/usb/usbmass_bd/LICENSE`, `modules/mmce/mmceman/LICENSE`, `modules/pccf/PS2SDK_LICENSE.txt`, the ps2link licence (W07 scope), and the `LICENSE` files in `modules/bdm`, `modules/bdmassault`, `modules/mx4sio_bd`. Licence preambles inside otherwise-read source files (SMSTCPIP `icmp.c`/`timers.c`/`netif.c`, headers `api.h`/`sys.h`) were skimmed, code read.

**Binaries — hashed/sized by script, not disassembled:** `modules/iopcore/IOPRP.img`, `modules/debug/deci2.img`, `modules/debug/drvtif.irx`, `modules/debug/drvtif-ingame.irx` (byte-identical pair, noted without a finding), `modules/debug/tifinet.irx`, `modules/debug/tifinet-ingame.irx`; the 9 `.irx.*` blobs in `modules/bdmassault`; all `ship/` binaries and images (`.ELF`, `.KELF`, `.irx`, `.BIN`, `.TM2`, `.png`, `.icn`, `bgm.ogg`, POPSTARTER ELFs); 14 git-ignored `tools/**/__pycache__/*.pyc`.

**Vendored bodies — integration-level only, by design:** `third_party/mbedtls` library and include trees (~280 files; version/pin and build-list admission audited, bodies not read), `third_party/PSUManager` core sources and vendored test bodies (integration audited), `third_party/ps2sdk-socket/socket.c` and `fdman.h`, the korium archive, ca-roots DER certificates and font binaries (digests/fingerprints verified by script instead of reading); `modules/bdm/bdmfs_fatfs/fatfs/ff.c` (7,257 lines) and `ffunicode.c` (15,593 lines) — function inventory plus the functions the local driver depends on, not a line audit (pinned upstream FatFs).

**Partial reads (structure/diff, not line-by-line):** `src/gfx/fntsys.cpp` (FreeType glyph-cache internals skimmed), `src/gfx/saveicon.cpp` (card-image FAT-walking internals), `src/gfx/textshape.cpp` (Arabic/RTL shaping tables); `modules/network/smbinit/des.c` and `modules/network/smbman-ra/des.c`/`md4.c` (assessed by full diff — the finding is the measured duplication), `modules/network/smap_udpbd/src/smap.c` and `udptty.c` (diffs vs the fully-read udpfs_smap copies), `modules/network/smap-ingame/smap.c` (structured diff + targeted greps), vendored `usmb2/md4c.c`/`md5.c` (head-only), the udpfs host test harnesses (~982 lines, not shipped code), `bdmassault` iLinkman manager sources (function inventory + targeted reads), and declaration/build-glue headers across scopes (`exports.tab`, `imports.lst`, `irx_imports.h`, per-directory Makefiles, small headers) — skimmed; no finding cites them. Inside the `ship/Example` snapshot: its 31 neutrino `.toml` configs, 6 `udpfs_server/*.py` files and hash-verified duplicate payloads were censused, not line-read. `src/platform/vmc_groups.cpp` lines 751–1349 are data tables covered by a script census of every string literal rather than prose reading; `src/platform/md5.c` read lines 1–120 plus grep (vendored RFC 1321).

**Test corpus method limit:** all 286 `.github/scripts/*.py` suites were executed (W12 half: 137 pass / 0 fail; W13 half: 139 pass, 2 failing only on a missing `bs4` in this VM, 1 exceeding a 90 s observation cap and recorded slow/not-observed) and script-censused, but roughly 120 suites per half were not each read end-to-end — they follow one extract-compile-assert pattern, and every filed claim about them rests on the census or on individually opened lines (the fully-read representatives are named in the COVERAGE blocks). `tests/host/settings_golden_defaults.inc` and `settings_golden_keys.inc` were censused/executed, not opened line-by-line (golden data tables).

**Docs gap (the one real coverage gap):** of ~70 `docs/*.md` files, the assigned build/root documents and the cited sections of GSM/HTTP/APA-SAFETY/IGR/AUTOLAUNCH/PS2BUILD-MIGRATION/RETROACHIEVEMENTS were read fully; the remaining ~60 were covered by a scripted extraction of every back-quoted repo path (existence-tested) plus targeted greps — their individual behavioural claims were **not** each re-derived against code. `ROLLING_RELEASE.md` was grep-checked only.

**Not run:** `tests/smb2-live/` and `tests/nfs-live/` as live suites (they require Samba/root, an SDK `ps2smb.h`, and a PS2-Servers checkout this VM does not provide; their host-only parts were covered through the Python suites that drive the same harnesses). No PS2 build was attempted (no PS2SDK in this VM): all Category 5 findings are structural, and nothing in this report is hardware-verified.

**Read-through dependencies flagged as not audited:** `freeze_diag_report.h` (included by ee_core freezediag code; not present in the audited trees), and the out-of-tree `unfs4.c` dependency noted by W07 (its `httpstream.inc` sibling was read under W05's scope).
