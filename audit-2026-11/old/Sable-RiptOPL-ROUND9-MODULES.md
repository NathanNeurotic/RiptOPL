# Sable — RiptOPL ROUND 9 (modules/ FULL LINE-READ)

**Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` (rebuild/main) — live head re-verified equal at pass start and end (`git ls-remote` + clone HEAD); no delta, no revalidations owed. Clone git-clean throughout; read-only held.
**Scope:** every source file under `modules/` line-read in full — 493 source files (.c/.h/.S/.s/.cpp), 128,148 lines by the coordinator's `wc` census, across bdm, bdmassault, bdmevent, debug, ds34bt, ds34usb, freepad (no source — PROVENANCE.md only), hdd, ilink, iopcore, isofs, mcemu, mmce, mx4sio_bd, network, pademu, pccf, usb, vmc, plus the root `Rules.bin.make` (build file, not source). Section ledgers (which also count Makefiles, import/export tables, ORIGIN/LICENSE prose read alongside) total ≈134,400 lines across the eight section files in `round9-sections/riptopl-r9-modules-*.md`, which carry the full per-file ledgers and checked-clean lists.
**Method:** eight parallel line-reads by module group, each briefed with the already-filed findings in its scope; coordinator de-duplication against RIPTOPL-001…343 and X-01…X-53, coordinator spot-verification of headline mechanisms in the clone (BDM-02 swap, IOPCORE-02, NETSMB-03, NETSTACK-01's absent define, HDDUSB-05, UDPFS-02, ILINK-02 all reproduced at the cited lines).

## COUNT — read this first

**119 new findings are delivered in this file, all numbered.** **100 are numbered RIPTOPL-400…499** (the block allocated to this pass), in benefit order. **The other 19 are numbered RIPTOPL-512…530** in the continuation section after RIPTOPL-499: they were delivered with this pass as its Appendix A, complete in full audit format but unnumbered because the 400–499 block was full, and have since been numbered in appendix order; their section-candidate IDs are retained only in the renumbering map below. **RIPTOPL-500…511 and RIPTOPL-600…601 belong to the parallel round-9 third-party passes and do not appear in this file** — the gap is block allocation, not missing modules findings. **The count was grep-verified against this file: exactly 100 `RIPTOPL-4xx` finding headings and exactly 19 finding headings in RIPTOPL-512…530.**

128 candidates were produced by the section reads. **9 were dropped as duplicates of already-filed findings** (verified against the original texts, not titles):

| Dropped candidate | Already filed as | Same mechanism at |
|---|---|---|
| UDPFS-01 (UDPRDMA hdr overrun) | **X-31** | `udpfs_shared/src/udprdma.c:185-199` — identical. One new detail for X-31's record: `rx_hdr_size` is stored (`udprdma.c:71,736`) and never read anywhere — the clamp X-31 prescribes can use it directly. |
| IOPCORE-03 (HDD DeviceInit infinite retry) | RIPTOPL-205 | `iopcore/cdvdman/device-hdd.c:70-73` — identical |
| IOPCORE-05 (BDM VMC exports g_bd) | RIPTOPL-171 | `device-bdm.c:279,302` — identical |
| IOPCORE-16 (smb.c dead receive path) | RIPTOPL-261 | `cdvdman/smb.c:23,997-1020` — identical |
| IOPCORE-19 (hex parsers disagree) | RIPTOPL-259 | `igrmbox.c:151-167` vs `freezediag.c:24-45` — identical pair |
| IOPCORE-20 (g_bd_sectors_per_sector) | RIPTOPL-262 | `device-bdm.c:22,106` — identical |
| IOPCORE-23 (imgdrv read_fs) | RIPTOPL-173 | `imgdrv.c:29-33` — identical |
| NETSMB-15 (poll.c ifd never set) | RIPTOPL-271 | `smbman-ra/poll.c:20,26,37,73-74` — identical |
| BDM-15 (MBR `valid` tautology) | RIPTOPL-161 | `part_driver_mbr.c:11-32` — identical (161's first half) |

Two candidates extend earlier findings without duplicating them and are numbered with the extension stated in their text: RIPTOPL-455 (igrmbox is a **third** copy of RIPTOPL-210's spin pattern, in a module -210's fix would not touch) and RIPTOPL-456 (the no-size-check patch surface of RIPTOPL-186, extended to all three patch modules).

**Renumbering map — Appendix A section-candidate IDs → allocated numbers** (in appendix order; this table is the only place the 19 candidate IDs are retained):

| Candidate ID | Now numbered |
|---|---|
| UDPFS-07 | RIPTOPL-512 |
| IOPCORE-13 | RIPTOPL-513 |
| IOPCORE-18 | RIPTOPL-514 |
| NETSMB-12 | RIPTOPL-515 |
| NETSTACK-13 | RIPTOPL-516 |
| BDM-11 | RIPTOPL-517 |
| BDM-14 | RIPTOPL-518 |
| BDM-16 | RIPTOPL-519 |
| BDM-17 | RIPTOPL-520 |
| HDDUSB-12 | RIPTOPL-521 |
| ILINK-08 | RIPTOPL-522 |
| ILINK-09 | RIPTOPL-523 |
| ILINK-10 | RIPTOPL-524 |
| ILINK-12 | RIPTOPL-525 |
| PADS-08 | RIPTOPL-526 |
| PADS-10 | RIPTOPL-527 |
| UDPFS-11 | RIPTOPL-528 |
| UDPFS-12 | RIPTOPL-529 |
| IOPCORE-26 | RIPTOPL-530 |

**Cumulative RiptOPL numbering after this pass: RIPTOPL-001…499 plus RIPTOPL-512…530 numbered — 119 findings in this file (400–499 and 512–530).**

## Duplication census (coordinator, byte-level, whole modules/ tree)

17 identical-content groups among the 493 source files. The headline: the UDPFS transport is now honestly single-sourced — `udpfs_core.c` ×4 and `udprdma.c` ×4 are three byte-identical 2-line `#include` shims (md5 `19773c81…` / `74c6e70c…`) plus one canonical body each in `udpfs_shared/` (refining RIPTOPL-008/-048 for the .c files). The remaining true triplication is **785 lines of UDPFS protocol headers ×3**, byte-identical and guarded by a CI identity check. `mprintf.h` exists ×5 identical + 1 diverged (smap_udpbd). Per-family detail (ds34 matrix, ilink↔bdmassault 22 pairs: 3 identical / 19 diverged / 0 missing, SMAP ×3, ministack ×2, md4/des pairs, ioplib pair) is in the section files and summarised in each finding that relies on it. No ds34 copy pair is byte-identical except `smsutils.S`; no new ds34 defect was found — the X-49 matrix stands, extended only by the copy census in `round9-sections/riptopl-r9-modules-pads.md` §B.

## Findings index (benefit order)

| ID | Section candidate | Title (short) |
|---|---|---|
| RIPTOPL-400 | NETSMB-02 | smb_FindFirstNext2 trusts response ParamOffset/DataOffset/FileNameLen with no bounds at all |
| RIPTOPL-401 | NETSMB-03 | Negotiate response domain name is `strcpy`'d unbounded into a 64-byte field |
| RIPTOPL-402 | HDDUSB-01 | ZSO decompression uses the explicitly-unsafe LZ4 "fast" decoder on a user-supplied game image, with prefix ref |
| RIPTOPL-403 | NETSTACK-04 | `lwip_accept()` dereferences a NULL netconn that `err_tcp()` itself posts |
| RIPTOPL-404 | BDM-03 | GPT scan's fallback path trusts the header's entry count unbounded: a crafted disk can make mount read sectors |
| RIPTOPL-405 | IOPCORE-02 | HDD APA header `num_partitions` is trusted past the 65-entry partition table |
| RIPTOPL-406 | HDDUSB-02 | APA journal write side has no bound: more than 126 dirty cache entries overflow the static journal buffer and  |
| RIPTOPL-407 | HDDUSB-03 | USB mass-storage connect walks the endpoint descriptor chain with a device-supplied count and stride, unchecke |
| RIPTOPL-408 | HDDUSB-06 | Device-supplied USB sector size feeds an unchecked `alloca` and unchecked length multiplications |
| RIPTOPL-409 | NETSTACK-06 | ps2ips `do_send()`/`do_sendto()` trust the EE-supplied `malign` field completely |
| RIPTOPL-410 | NETSTACK-10 | ps2ips `do_recv()`/`do_recvfrom()` accept a negative EE `length`, inverting the RA buffer-offset arithmetic |
| RIPTOPL-411 | IOPCORE-01 | HDPro `sceAtaDmaTransfer` retries a failing transfer forever, holding the ATA semaphore |
| RIPTOPL-412 | NETSTACK-01 | ARP entries can never expire: `etharp_tmr()` has no caller because `LWIP_ARP` is defined nowhere |
| RIPTOPL-413 | PADS-01 | genvmc's physical-card copy divides by, and indexes with, unchecked card-supplied superblock geometry |
| RIPTOPL-414 | NETSMB-09 | NBNS receive thread copies the answer address from a fixed offset without checking the datagram is that long |
| RIPTOPL-415 | NETSMB-08 | httpclient RPC handlers use EE-supplied strings unterminated (only the streaming op terminates them) |
| RIPTOPL-416 | BDM-06 | `fatfs_fs_driver_get_mounted_bd_from_index` bounds check is off by one and admits negative indices |
| RIPTOPL-417 | ILINK-01 | `InitializeConfigurationROM` leaks the old CROM and every extra unit, and silently drops all added units, on e |
| RIPTOPL-418 | ILINK-02 | `iLinkFindUnit` accepts a negative `UnitInList` and indexes `NodeData[-1]` — both copies / Category 6 |
| RIPTOPL-419 | IOPCORE-09 | DEV9 callback-registration exports index their tables with unchecked (and possibly negative) indices |
| RIPTOPL-420 | UDPFS-03 | Fixed-size UDPFS replies are never length-checked; a short reply is parsed out of stale buffer contents |
| RIPTOPL-421 | UDPFS-02 | `udpfs_core_get_sector_count` truncates capacity to a signed `int`: shares ≥ 1 TiB can never register as a blo |
| RIPTOPL-422 | UDPFS-08 | Both ministack RX dispatchers will call through a NULL handler for a packet addressed to port 0 |
| RIPTOPL-423 | NETSTACK-05 | `do_bind()` uses lazily allocated PCBs without the NULL checks its sibling handlers have |
| RIPTOPL-424 | NETSTACK-09 | `copy_from_pbuf()` walks off the end of the pbuf chain when a fragment's IP length exceeds the delivered bytes |
| RIPTOPL-425 | NETSTACK-11 | ICMP error replies dereference `pbuf_alloc()` results unchecked and copy from the offending packet unchecked |
| RIPTOPL-426 | NETSTACK-03 | Client netconns never get a semaphore, so `netconn_write()` busy-spins on `WaitSema(0)` |
| RIPTOPL-427 | IOPCORE-08 | DEV9 `SpdDmaTransfer` busy-waits forever for DMA completion, semaphore held |
| RIPTOPL-428 | IOPCORE-04 | SCMD path: unbounded hardware-register spins, and `cdvdman_sendSCmd` always reports success |
| RIPTOPL-429 | BDM-01 | GPT connect returns success (0) when its scratch allocation fails, so BDM claims the device and FatFs is never |
| RIPTOPL-430 | BDM-02 | `USBMASS_DEVCTL_STOP_UNIT` and `USBMASS_DEVCTL_STOP_ALL` bodies are swapped in `fs_devctl` |
| RIPTOPL-431 | NETSMB-01 | FileTimeToDate converts ~half of all dates to garbage (SMB1 browse/stat timestamps) |
| RIPTOPL-432 | NETSMB-05 | usmb2_build_request drains a server-declared SPL of any size; no reply-size ceiling |
| RIPTOPL-433 | NETSMB-06 | Replies are matched to requests only when signing is on (usmb2 MessageId) or never (unfs4 xid) |
| RIPTOPL-434 | NETSMB-07 | smb2man_querydir trusts the reply's OutputBufferLength over the bytes actually delivered |
| RIPTOPL-435 | NETSMB-04 | smb_closeAll() closes FIDs but never frees the handles — stale slots leak and FIDs can be re-closed on a later |
| RIPTOPL-436 | HDDUSB-05 | USB READ CAPACITY result stored as sector count without the +1: the last sector of every USB drive is unreacha |
| RIPTOPL-437 | IOPCORE-06 | SMB (USBLD multipart) read past the last part returns success with the buffer untouched |
| RIPTOPL-438 | UDPFS-04 | `smap_udpbd` write path hardcodes 512-byte sectors and ignores the INFO-reply sector size the read path honour |
| RIPTOPL-439 | IOPCORE-07 | `sceCdStInit` divides by the game's `bankmax` with no validation |
| RIPTOPL-440 | PADS-03 | mcemu FastIO hooks dereference the FastIO allocation without ever checking it exists; the read hook also DMA-t |
| RIPTOPL-441 | PADS-04 | `mmce_fs_open`/`dopen` failure exits leave `file->privdata` pointing at a still-free handle slot, which the ne |
| RIPTOPL-442 | BDM-12 | `fs_read`/`fs_write` collapse every FatFs error to a 0 return, indistinguishable from EOF / zero-progress |
| RIPTOPL-443 | IOPCORE-24 | `smb_CloseAll`'s −1 sentinel can never be stored in the u16 FID table (all three SMB-family files) |
| RIPTOPL-444 | NETSMB-10 | smb_lseek64 accepts negative positions (SEEK_SET/SEEK_CUR check only the upper bound) |
| RIPTOPL-445 | NETSMB-11 | smb_dopen appends "\\*" to fh->name with strcat after a 255-char strncpy — overflow and unterminated-name read |
| RIPTOPL-446 | BDM-04 | GPT parser ignores the header's `partition_entry_size` and assumes every entry is 128 bytes |
| RIPTOPL-447 | BDM-05 | GPT connect lacks the partition-device filter the MBR connect has, so every partition pseudo-device is re-prob |
| RIPTOPL-448 | HDDUSB-04 | `sceAtaSecurityEraseUnit` passes NULL as the IDENTIFY buffer; the helper writes 512 bytes to address 0 and der |
| RIPTOPL-449 | IOPCORE-21 | DVD TOC builder marks single-layer discs as dual-layer whenever DVDDL emulation is off |
| RIPTOPL-450 | IOPCORE-22 | `cdrom_lseek` SEEK_END subtracts the offset instead of adding it |
| RIPTOPL-451 | NETSTACK-07 | `tcp_keepalive()` transmits a segment whose TCP flags byte is uninitialised pool memory |
| RIPTOPL-452 | NETSTACK-08 | `netconn_recv()`/`netconn_write()` dereference `conn->pcb.tcp` where the tree itself elsewhere guards it |
| RIPTOPL-453 | UDPFS-16 | `smap_udpbd`'s transmit path has no link-up wait and no failure exit: `smap_transmit` retries the TX ring fore |
| RIPTOPL-454 | IOPCORE-11 | Streaming EE read busy-spins on `sceSifDmaStat` with no bound |
| RIPTOPL-455 | IOPCORE-10 | igrmbox `mbSendEE` is a third copy of the filed sysmemSendEE spin pattern — with no yield at all |
| RIPTOPL-456 | IOPCORE-12 | Game patch modules write fixed offsets into a target module's text with no size or version check |
| RIPTOPL-457 | PADS-02 | pademu `_start` reads 8 bytes of `argv[1]` with no length check |
| RIPTOPL-458 | PADS-05 | genvmc STATUS devctl copies to the caller's buffer with none of the validation the CREATE case applies in the  |
| RIPTOPL-459 | NETSMB-17 | smb_Connect copies the server IP with strncpy(…, 16) and no termination guarantee; two later sprintf("%s") con |
| RIPTOPL-460 | NETSMB-16 | NTLMv2 client challenge is the constant 0x01×8 and the timestamp is zero (vendored usmb2) |
| RIPTOPL-461 | BDM-08 | `fs_dread` can hand EE an unterminated directory name for a maximum-length (255-char) LFN |
| RIPTOPL-462 | BDM-13 | `bdm_get_bd` silently leaves the tail of the caller's array unwritten, and `bdm_connect_bd` silently drops dev |
| RIPTOPL-463 | UDPFS-19 | `mstack:` exchange silently truncates an over-long reply, indistinguishable from a complete one |
| RIPTOPL-464 | UDPFS-17 | `udpfs_core_close` discards the close reply entirely and always reports success |
| RIPTOPL-465 | IOPCORE-14 | CDVDFSV stream read returns an uninitialised error word when `sectors == 0` |
| RIPTOPL-466 | IOPCORE-15 | `hookMODLOAD` uses `getModInfo`'s result without checking it — the one unhardened caller left in scope |
| RIPTOPL-467 | HDDUSB-09 | `apaOpen` compares the password of a cache entry after freeing the entry |
| RIPTOPL-468 | UDPFS-14 | INFERRED: SMAP capability check in both neutrino copies is mis-parenthesised (`!x & MASK`), correct only by ac |
| RIPTOPL-469 | UDPFS-15 | INFERRED: `udpfs_ministack` registers its RX callback and takes over the tty before the duplicate-load check |
| RIPTOPL-470 | NETSTACK-02 | Eight `ps2ip` exports are an empty stub, while in-tree modules import and call them |
| RIPTOPL-471 | BDM-07 | Typed-driver pool is never reclaimed: after 8 distinct device paths, later device types mount but are unreacha |
| RIPTOPL-472 | ILINK-03 | The SBP-2 layer attaches up to 5 drives, the SCSI layer can only ever publish 2 — drives 3–5 are logged in and |
| RIPTOPL-473 | ILINK-05 | ilink `_start` ignores `GetConsoleIDs` failure and publishes a zero EUI-64 and empty model name / Category 6 |
| RIPTOPL-474 | PADS-06 | Both mcemu ports share the single 512-byte page data buffer |
| RIPTOPL-475 | BDM-09 | One global semaphore serializes all FatFs I/O on all volumes, defeating FatFs' own per-volume reentrancy |
| RIPTOPL-476 | HDDUSB-07 | APA DES password engine (~460 lines + tables) is compiled, linked and executed on every open, but its result c |
| RIPTOPL-477 | IOPCORE-25 | Identical `smb_ReadCD` + `smb_CloseAll` bodies triplicated across the three protocol implementations |
| RIPTOPL-478 | PADS-07 | `ioplib.c`/`ioplib.h` are byte-identical vendored copies in mmceman and mx4sio_bd, and their `sio2man_hook.c`  |
| RIPTOPL-479 | UDPFS-05 | `smap_udpbd`'s ministack carries a fully dead ARP cache: `arp_add_entry` has no caller and the RX path never l |
| RIPTOPL-480 | UDPFS-06 | Dead FHI export table and `_start` branch in `udpfs_bd` and `udpfs_ioman` reference symbols that exist nowhere |
| RIPTOPL-481 | UDPFS-10 | `udprdma_inform` (server mode) and the core/transport teardown pair are dead in the shipped variants |
| RIPTOPL-482 | NETSTACK-12 | Dead code and build clutter: an unbuilt loopback driver, a DHCP header with no client, a RAW pool with no RAW  |
| RIPTOPL-483 | NETSMB-13 | Vendored clients carry entry points and whole transports this build never uses (usmb2/unfs4 dead-in-build) |
| RIPTOPL-484 | IOPCORE-17 | smb.h declares a function that does not exist and ~200 lines of never-referenced protocol constants |
| RIPTOPL-485 | NETSMB-14 | smb.h carries 44 protocol-offset defines and ~30 command defines nothing in the module references |
| RIPTOPL-486 | ILINK-04 | The assault tree carries five build files its only Makefile never reads (244 lines) — one of them still contai |
| RIPTOPL-487 | ILINK-07 | Dead driver surface kept in both copies: an uncallable deinit, two phantom prototypes, four dead struct fields |
| RIPTOPL-488 | ILINK-06 | Dead imports in all four IOP builds in scope (15 stubs with no caller in their own module) / Category 1 / 7 |
| RIPTOPL-489 | HDDUSB-13 | 407-line packed IDENTIFY-device structure maintained to read exactly one field |
| RIPTOPL-490 | BDM-10 | Static FIL/DIR pools are sized far beyond anything this driver can be asked to serve (128 FIL + 16 DIR in IOP  |
| RIPTOPL-491 | ILINK-11 | pccf keeps two 512-byte static scratch buffers resident forever although their lifetimes never overlap / Categ |
| RIPTOPL-492 | PADS-09 | mx4sio's DMA command struct reserves a 1 KiB CRC table (`crc[512]`, marked FIXME in-tree) |
| RIPTOPL-493 | HDDUSB-08 | `hdd_blkio.o` is an empty translation unit in the shipped build but is still compiled and linked |
| RIPTOPL-494 | HDDUSB-14 | `lz4.h` declares the full LZ4 API (~20 functions: compression, streaming, obsolete); `lz4.c` implements exactl |
| RIPTOPL-495 | HDDUSB-10 | usbd public header declares two exports the module does not implement |
| RIPTOPL-496 | HDDUSB-11 | Multi-LUN support is plumbed but never used: max-LUN is queried, printed, and thrown away; the CBW LUN field i |
| RIPTOPL-497 | UDPFS-09 | `smap_udpbd` xfer creates an event flag nothing ever waits on, and defines a FIFO-copy helper nothing calls |
| RIPTOPL-498 | UDPFS-13 | `rapull`'s `rp_frame_values(read=0)` mode is dead: the parameter and its branches serve a call that never happ |
| RIPTOPL-499 | UDPFS-18 | `raudp`'s discovery-elapsed counter `ra_disc_us` is written, overflow-guarded, and never read |
| — | — | **Continuation (512–530)** — delivered as Appendix A; numbered in appendix order, not re-ranked |
| RIPTOPL-512 | Appendix A | `udpfs_ministack`'s PS2SDK Makefile omits `ministack_xchg.o`, which its own `main.c` calls — the two build manifests disagree |
| RIPTOPL-513 | Appendix A | HDPro 48-bit LBA path drops the upper LBA bytes for LCYL/HCYL |
| RIPTOPL-514 | Appendix A | CDVDFSV reports CDVDMAN version 0x223 while CDVDMAN reports 0x225 |
| RIPTOPL-515 | Appendix A | prepare_path's trailing-slash scan is dead code that starts on the NUL terminator |
| RIPTOPL-516 | Appendix A | TCP initial sequence numbers are trivially predictable and IP IDs are a plain counter (inherited) |
| RIPTOPL-517 | Appendix A | `ff.c` carries a local debug divergence that the vendoring record does not list |
| RIPTOPL-518 | Appendix A | Dead definitions: `mount_status` field never read or written; unused GPT GUID and attribute macros |
| RIPTOPL-519 | Appendix A | `part_read` and `part_write` are near-identical copies (bounds check + debug block + forwarding) |
| RIPTOPL-520 | Appendix A | FatFs is configured for OEM code page 869 (Greek 2), so non-ASCII short-name handling runs through Greek tables |
| RIPTOPL-521 | Appendix A | `xhdd.h` declares two functions that exist nowhere in the HDD modules |
| RIPTOPL-522 | Appendix A | pccf carries a `time_us` clock plumbing chain that is written once and read never |
| RIPTOPL-523 | Appendix A | `pcdiag_ata.h` declares a diagnostic entry point, `pcdiag_ata_observe`, that was never implemented |
| RIPTOPL-524 | Appendix A | `modules/pccf/format.inc` is a 57-line mini-printf included by nothing |
| RIPTOPL-525 | Appendix A | `PROVENANCE.md`'s dedupe claim names a binary that does not exist, and its re-vendor count is stale |
| RIPTOPL-526 | Appendix A | `modules/debug/drvtif.irx` and `drvtif-ingame.irx` are the same bytes under two names |
| RIPTOPL-527 | Appendix A | bdmevent's `irx_imports.h` imports four libraries the module never calls |
| RIPTOPL-528 | Appendix A | `udpfs_fhi/ORIGIN.txt` states there is no write export; the module exports one and implements it |
| RIPTOPL-529 | Appendix A | RA-under-POPS is shipped with its two load-bearing unknowns still open (half-finished feature) |
| RIPTOPL-530 | Appendix A | Small dead items: unused macros, an unreachable duplicate table entry, a never-linked helper |


---

## Findings (benefit order)

### RIPTOPL-400 — smb_FindFirstNext2 trusts response ParamOffset/DataOffset/FileNameLen with no bounds at all
*(Round-9 section candidate NETSMB-02.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/network/smbman-ra/smb.c:1474-1498`
- **Problem:** The response param block is taken from `&SMB_buf.smb.u8buff[FFNRsp->smbTrans.ParamOffset]` (with a `- 2` variant for FIND_NEXT2 at `:1480`), the data block from `&SMB_buf.smb.u8buff[FFNRsp->smbTrans.DataOffset]` (`:1482`), and the name copied with `getStringField(info->FileName, FFRspData->FileName, FFRspData->FileNameLen)` (`:1498`). All three values are server-supplied `u16`/`u32` and none is checked against the received packet size or the 4,224-byte `SMB_buf`. A hostile/broken server sets `DataOffset` near 65535 and the client reads a `FindFirst2ResponseData_t` far past the static buffer; `FileNameLen` then drives `utf16ToAscii`/`strncpy` in `getStringField` (`smb.c:336-352`) to read that far-away memory and write `FileNameLen`-bounded output into the caller's `SearchInfo_t`, which in `smb_fio.c` is the 4,096-byte static `SearchBuf` — overrunning it into the adjacent `smb_curdir`/`smb_curpath` statics. Distinct from RIPTOPL-154 (same pattern in `smb_QueryPathInformation`); this function was not covered by it.
- **Fix:** Before either dereference, require `ParamOffset` and `DataOffset` to lie inside the received length returned by `GetSMBServerReply`, and clamp `FileNameLen` to the bytes actually remaining between `FFRspData->FileName` and the packet end; return `-EIO` otherwise.
- **Benefit:** Closes a server-controlled OOB read + static-buffer overflow on the directory-listing path — the most-used SMB1 operation.
- **Effort:** S
- **Risk:** Low — rejects only packets no conforming server sends.
- **Confidence:** READ

### RIPTOPL-401 — Negotiate response domain name is `strcpy`'d unbounded into a 64-byte field
*(Round-9 section candidate NETSMB-03.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/network/smbman-ra/smb.c:336-352` (`getStringField`, `inbytes == 0` branch at `:346-348`), called at `modules/network/smbman-ra/smb.c:404`; destination `modules/network/smbman-ra/smb.h:285` (`char PrimaryDomainServerName[64]`)
- **Problem:** `smb_NegotiateProtocol` copies the server's primary-domain string with `getStringField(..., 0)`. With `inbytes == 0` and no `SERVER_CAP_UNICODE`, that is a plain `strcpy` (and in the Unicode branch, `utf16ToAscii` with `inbytes == 0` loops until a NUL wchar). The source is the received packet at `&NPRsp->ByteField[KeyLength]`; nothing bounds the copy to the 64-byte destination or the scan to the received packet. A negotiate response whose domain field runs more than 63 non-NUL bytes overwrites the rest of `server_specs_t` (including `EncryptionKey` at `smb.h:286`) and beyond. Distinct from RIPTOPL-153 (the `KeyLength`→`EncryptionKey` memcpy in the same packet, different sink).
- **Fix:** Pass the remaining received length as `inbytes` and make the `inbytes == 0` branch bounded the same way (copy at most `dest size - 1`); one call-site change plus a size parameter, or clamp inside this single call.
- **Benefit:** Removes a pre-auth, server-controlled overflow of a global struct during the first packet exchange.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-402 — ZSO decompression uses the explicitly-unsafe LZ4 "fast" decoder on a user-supplied game image, with prefix references enabled and the result unchecked
*(Round-9 section candidate HDDUSB-01.)*

- **Category:** 6 (bug/security)
- **Locations:** `modules/isofs/zso.c:111`; `modules/isofs/lz4.c:500-506`; `modules/isofs/lz4.c:365-498` (generic decoder); `modules/isofs/lz4.h:62,117-126`
- **Problem:** `ziso_read_sector` decompresses each compressed block of a ZSO image (a file the user downloads/copies, i.e. attacker-suppliable) with `LZ4_decompress_fast(tmp, addr, 2048)` and ignores the return value. In this vendored copy, `LZ4_decompress_fast` calls the generic decoder with `endOnOutputSize` and **`withPrefix`** (`lz4.c:505`). Consequences, all verified in the decoder source: (1) the offset sanity check `ref < dest` is skipped for `withPrefix` (`lz4.c:442`), so a crafted match offset can make the decoder copy from memory *before* the 2048-byte output buffer into the sector buffer; (2) with `endOnInput == 0` the decoder does not know the input length — the literal-run loop (`lz4.c:408-413`) and match-length loop (`lz4.c:447-454`) read past the end of the compressed block while bytes stay `0xFF`, and wildcopy reads past the input; the vendored header itself warns the fast variant "may read beyond input buffer in case of malicious data packet" (`lz4.h:117-126`). A crafted ZSO can therefore read/copy out-of-bounds in IOP memory (crash at best, memory disclosure into game data at worst).
- **Fix:** Add the 3-line `LZ4_decompress_safe` wrapper next to `LZ4_decompress_fast` (`LZ4_decompress_generic(source, dest, inputSize, maxOutputSize, endOnInputSize, noPrefix, full, 0)` — the generic decoder already implements the checked mode) and call it from `zso.c:111` as `LZ4_decompress_safe(tmp, addr, r, 2048)`, treating a return `!= 2048` as `-EIO`.
- **Justification if additive:** +~5 lines total, no new code paths — it instantiates an existing, already-linked decoder mode and turns silent corruption into a clean read error.
- **Benefit:** Removes the only crafted-file memory-safety hole found in the isofs tree; every ZSO in the wild is untrusted input.
- **Effort:** S · **Risk:** Low (safe decoder is the same generic core; behaviour on valid ZSOs is identical) · **Confidence:** High (mechanism read directly in `lz4.c`).

### RIPTOPL-403 — `lwip_accept()` dereferences a NULL netconn that `err_tcp()` itself posts
*(Round-9 section candidate NETSTACK-04.)*


- **Category:** 6 (bug/security — NULL deref in the stack's socket layer)
- **Locations:**
  - `modules/network/SMSTCPIP/sockets.c:164-186` — `lwip_accept()` passes the
    `netconn_accept()` result straight to `netconn_peer(newconn, ...)` with no
    NULL check
  - `modules/network/SMSTCPIP/api_msg.c:202-213` — `err_tcp()` posts NULL to the
    accept mbox (`sys_mbox_post(conn->acceptmbox, NULL)`) when a listening
    connection errors, which is exactly what wakes `netconn_accept()` with a NULL
    return (`modules/network/SMSTCPIP/api_lib.c:395-408`)
  - `modules/network/SMSTCPIP/api_lib.c:243-270` — `netconn_peer()` switches on
    `conn->type` unconditionally: NULL in, IOP exception out
- **Problem:** A listening socket whose PCB errors (reset during handshake, abort)
  deterministically posts the NULL that crashes the next `accept()` call. This is
  not a theoretical NULL: the in-tree error path manufactures it.
- **Fix:** Return `-1`/`ENOBUFS` from `lwip_accept()` when `newconn == NULL`
  (3 lines), matching the check the same function already does for its
  `alloc_socket()` failure immediately below.
- **Benefit:** Removes a remotely triggerable IOP crash on any listening socket.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High (READ, full in-tree chain).

### RIPTOPL-404 — GPT scan's fallback path trusts the header's entry count unbounded: a crafted disk can make mount read sectors indefinitely
*(Round-9 section candidate BDM-03.)*


- **Category:** 6 (bug / security)
- **Locations:** `modules/bdm/src/part_driver_gpt.c:84-98` (clamp at `:86-95` applies only inside the plausible-extent branch)
- **Problem:** The RiptOPL clamp on `entryCount` (cross-ref the round-9 GPT work behind RIPTOPL-160/-161) only runs when `first_lba > partition_table_lba` and the span is ≤ `GPT_MAX_ARRAY_SECTORS`. Any other header keeps `stopAtUnused = 1` **and the header's raw `partition_count`** (a disk-controlled u32, up to ~4 billion). The scan loop then reads one sector per `entriesPerSector` entries until it happens to hit an all-zero type GUID. A crafted GPT header with a huge count and a table of non-zero-type entries that fail the LBA sanity check (`continue`, no mount, no termination) makes the BDM thread walk the disk sector-by-sector at mount time — effectively hanging device enumeration. The header's CRC32s, `header_size`, `current_lba` and revision are never validated either (the code's own TODO at `part_driver_gpt.c` admits the CRC omission); notably the vendored FatFs parser in the same tree (`fatfs/ff.c`, `test_gpt_header`) does validate header CRC, entry size and caps the count at 128. Distinct from RIPTOPL-160/-161: those are partition-extent validation gaps; this is the scan-length bound that exists only on one of the two scan paths.
- **Fix in words:** Apply a hard entry/sector budget on both paths (e.g. clamp `entryCount` to the same 2048-sector array budget regardless of the `first_lba` heuristic, stopping the scan when the budget is spent), keeping the stop-at-first-unused behaviour inside that budget.
- **Justification:** The fix adds one comparison/clamp reusing the existing `GPT_MAX_ARRAY_SECTORS` constant — no new state or tables.
- **Benefit:** Mount-time denial of service from a crafted/corrupt disk becomes a bounded 2048-sector worst case on every header shape.
- **Effort:** S
- **Risk:** Low — real GPT arrays are ≤128 entries by spec convention and already fit the budget on the plausible path.
- **Confidence:** READ

### RIPTOPL-405 — HDD APA header `num_partitions` is trusted past the 65-entry partition table
*(Round-9 section candidate IOPCORE-02.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/iopcore/cdvdman/device-hdd.c:80` (`NumParts = apaHeader.num_partitions`), `device-hdd.c:30-46` (`cdvdman_get_part_specs` loops `i < NumParts` over `cdvdman_partspecs[]`), table size `modules/iopcore/cdvdman/atad.h:97` (`HDL_NUM_PART_SPECS 65`, array at `device-hdd.c:24`)
- **Problem:** `num_partitions` is an `int` read verbatim from the APA header sector on the HDD (HDLoader-written data, i.e. disc/HDD-supplied, not loader-validated in this file). If it exceeds 65, `cdvdman_get_part_specs` walks `ps++` past the 65×12-byte static table into adjacent BSS and treats whatever it finds as partition offset/size, which then drives real ATA LBAs in `DeviceReadSectors` (`device-hdd.c:110-133`). No clamp exists anywhere in the file.
- **Fix:** Clamp at the assignment: `if (NumParts > HDL_NUM_PART_SPECS) NumParts = HDL_NUM_PART_SPECS;` (and reject `< 0`).
- **Justification if additive:** One comparison; no new state.
- **Benefit:** Closes an out-of-bounds static-table walk feeding hardware LBAs, from crafted/corrupt HDD metadata.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-406 — APA journal write side has no bound: more than 126 dirty cache entries overflow the static journal buffer and write journal headers past the journal area
*(Round-9 section candidate HDDUSB-02.)*

- **Category:** 6 (bug/security)
- **Locations:** `modules/hdd/apa/src/journal.c:47-56`; `modules/hdd/apa/src/libapa.h:184-189` (`sectors[126]`), `libapa.h:33-34` (journal headers at sectors 10–262); `modules/hdd/apa/src/cache.c:111-126`; `modules/hdd/apa/src/hdd.c:263-271`
- **Problem:** `apaJournalWrite` stores into `journalBuf.sectors[journalBuf.num]` and writes the header to disc sector `(journalBuf.num << 1) + APA_SECTOR_APAL_HEADERS` with no check of `num` against the 126-entry array. `apaCacheFlushAllDirty` calls it once per dirty cache entry, and the cache size is raised without any upper bound by the `-n` module argument (`hdd.c:263-271`: `if (cacheSize < i) cacheSize = i;`). With cacheSize > 126 a flush writes past the static `apa_journal_t` in IOP memory and writes APA headers to disc sectors ≥ 262, i.e. past the journal area into the region `APA_SECTOR_MIN_OSDSTART` protects (`libapa.h:34`). Distinct from filed RIPTOPL-147 (journal *restore* trusting the on-disk count) — this is the write side, driven by a local count.
- **Fix:** In `apaJournalWrite`, `if (journalBuf.num >= 126) return -EIO;` before the transfer; optionally also clamp `-n` to ≤ 126 in `fioGetInput`. One guard suffices (LESS).
- **Benefit:** Bounds both a static-buffer overflow and on-disc writes outside the journal area on the data-integrity-critical flush path.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the missing bound (read in code); **INFERRED** for reachability — it depends on the loader passing `-n` > 126, which no in-tree caller was found to do; with the shipped default cache size this is latent, not live.

### RIPTOPL-407 — USB mass-storage connect walks the endpoint descriptor chain with a device-supplied count and stride, unchecked
*(Round-9 section candidate HDDUSB-03.)*

- **Category:** 6 (bug/security)
- **Locations:** `modules/usb/usbmass_bd/src/usb_mass.c:665-681`
- **Problem:** In `usb_mass_connect`, `epCount = interface->bNumEndpoints` (a byte from the attached device's descriptor) and the walk advances `endpoint = (UsbEndpointDescriptor *)((char *)endpoint + endpoint->bLength)` with no check that `bLength` is non-zero / at least descriptor-sized, and no bound against the end of the static descriptor buffer, calling `usb_bulk_probeEndpoint` (which opens pipes) on whatever it lands on. A broken or malicious device can send the walk out of the descriptor buffer or spin it in place, probing garbage as endpoints. Distinct mechanism from filed RIPTOPL-077 (same function: connect re-reading descriptors without probe's NULL checks).
- **Fix:** Bound the walk by the configuration descriptor's `wTotalLength`: stop when the next endpoint pointer would leave `config … config + wTotalLength`, and require `endpoint->bLength >= sizeof(UsbEndpointDescriptor)` before advancing.
- **Justification if additive:** ~3 lines of guard inside the existing loop; no new state.
- **Benefit:** Closes an untrusted-device descriptor-walk overrun in the USB stack's only mass-storage driver.
- **Effort:** S · **Risk:** Low · **Confidence:** High (walk read in code; descriptor source is the device by construction of the USB stack).

### RIPTOPL-408 — Device-supplied USB sector size feeds an unchecked `alloca` and unchecked length multiplications
*(Round-9 section candidate HDDUSB-06.)*

- **Category:** 6 (bug/security)
- **Locations:** `modules/usb/usbmass_bd/src/scsi.c:321` (`__builtin_alloca(sectorSize + 4)` in `scsi_write`); `modules/usb/usbmass_bd/src/scsi.c:244,261` (sectorSize taken from READ CAPACITY `block_length`, checked only `!= 0`); `modules/usb/usbmass_bd/src/scsi.c:156,174` (`data_len = sc * sectorSize` for READ/WRITE queueing)
- **Problem:** `bd->sectorSize` is whatever the attached drive reports. A drive reporting a huge block length (e.g. 0x10000000) makes any misaligned-buffer write `alloca` that much stack in IOP (2 MB total RAM) — instant stack destruction — and makes the `sc * sectorSize` transfer-length products wrap. Real drives report 512/4096, so this needs a broken/malicious device, but the value is fully device-controlled and never range-checked.
- **Fix:** At capacity parse (one place, both paths), reject a `block_length` that is not a power of two in [512, 4096] with the existing "drive left out" error return — the ORIGIN-documented pattern already used for sector size 0 (`scsi.c:246-249`).
- **Justification if additive:** one comparison added to an existing validation block; also removes the need to reason about the alloca and the products downstream.
- **Benefit:** One check kills the alloca overflow and both multiplication wraps.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the data flow (read end-to-end); exploitability is device-dependent by nature.

### RIPTOPL-409 — ps2ips `do_send()`/`do_sendto()` trust the EE-supplied `malign` field completely
*(Round-9 section candidate NETSTACK-06.)*


- **Category:** 6 (bug/security — unchecked EE RPC length)
- **Locations:**
  - `modules/network/ps2ips/ps2ips.c:402-418` (`do_send`) and `:435-451`
    (`do_sendto`): `s_offset = 64 - pkt->malign;` then
    `memcpy(lwip_buffer + s_offset, pkt->malign_buff, pkt->malign)` and
    `sceSifGetOtherData(..., sendlen - pkt->malign, ...)` — no range check on
    `malign`, `length`, or their relationship anywhere in either handler
- **Problem:** `malign` comes straight from the RPC packet. The design assumes
  `0 <= malign <= 64` (it is the EE buffer's misalignment). `malign > 64` makes
  `s_offset` negative and the memcpy writes *before* `lwip_buffer`;
  `malign > sendlen` makes the `sceSifGetOtherData` size negative (a huge
  unsigned count at the SIF layer). The RA-hardened code around it (the
  `BUFF_SIZE + 64` buffer, ps2ips.c:84-86) is all predicated on offsets staying
  inside `[0, 64]`. Distinct from RIPTOPL-067 (the `do_setsockopt` length) —
  same class, different handler, different buffer.
- **Fix:** Reject the request at handler entry when
  `malign < 0 || malign > 64 || length < malign` (return -1 in `ptr[0]`). Two
  comparisons per handler; deletion-shaped, no new code paths.
- **Benefit:** Closes an EE→IOP memory-corruption primitive in the RPC server
  that every EE program links against.
- **Effort:** S
- **Risk:** Low — the stock EE library computes `malign` from address
  alignment, so legitimate values are 0..63 and unaffected.
- **Confidence:** High on the missing check (READ). Exploitability beyond a
  malformed/buggy EE caller is INFERRED (the stock libps2ips never sends such
  values).

### RIPTOPL-410 — ps2ips `do_recv()`/`do_recvfrom()` accept a negative EE `length`, inverting the RA buffer-offset arithmetic
*(Round-9 section candidate NETSTACK-10.)*


- **Category:** 6 (bug/security — unchecked EE RPC length)
- **Locations:**
  - `modules/network/ps2ips/ps2ips.c:183-207` (`do_recv`) and `:330-344`
    (`do_recvfrom`): `if (recv_pkt->length <= 64) srest = recv_pkt->length;`
    then `s_offset = 64 - srest;` and
    `recvlen = MIN(BUFF_SIZE, recv_pkt->length)` — no lower bound on `length`
- **Problem:** The RA sizing fix made `lwip_buffer` exactly `BUFF_SIZE + 64`
  (ps2ips.c:84-86) on the assumption `0 <= s_offset <= 64`. A negative
  `length` makes `srest` negative and `s_offset` greater than 64, pushing
  the receive destination outside the headroom the fix reserved, and passes a
  negative length into `recv()`/`recvfrom()`. Sibling handlers got RA fixes
  for their offset maths; the sign case was not covered. Distinct from
  RIPTOPL-067/-080/-100 (different handlers and mechanisms).
- **Fix:** Clamp at handler entry: treat `length < 0` as 0 / reject, before
  the `srest` computation. One comparison per handler.
- **Benefit:** The RA overflow fixes hold for the full range of EE-supplied
  inputs, not just non-negative ones.
- **Effort:** S
- **Risk:** Low
- **Confidence:** Medium — the arithmetic inversion is READ; how the paired
  stack's `recv()` treats a negative length is INFERRED (differs between the
  ps2sdk menu stack and the in-tree stack).

### RIPTOPL-411 — HDPro `sceAtaDmaTransfer` retries a failing transfer forever, holding the ATA semaphore
*(Round-9 section candidate IOPCORE-01.)*

- **Category:** 6 bugs/security (hang) / 5 performance
- **Locations:** `modules/iopcore/cdvdman/hdpro_atad.c:690-722` (loop; comment at :714 "Unlike ATAD, retry indefinitely until the I/O operation succeeds", `continue` at :717 and :719)
- **Problem:** The chunk loop advances only on success. Any persistent error from `sceAtaExecCmd` or `sceAtaWaitResult` (dead/dying drive, bad sector, bus fault) loops the same chunk forever with no retry cap, no delay, and `ata_io_sema` held for the whole loop, so every other ATA user (game reads, VMC writes via the same semaphore) is blocked permanently. The sibling `atad.c:728-746` caps retries at 3 and only for the recoverable ICRC error.
- **Fix:** Mirror atad.c: bounded retries (3) only for a recoverable error class, then return the error; release the semaphore on every exit path.
- **Justification if additive:** None — deletion of the unbounded retry, reuse of the sibling's existing policy.
- **Benefit:** Removes a permanent, silent IOP-wide HDD hang class on the HDPro build.
- **Effort:** S
- **Risk:** Low — error return replaces an infinite loop; callers already handle `SCECdErREAD`.
- **Confidence:** READ

### RIPTOPL-412 — ARP entries can never expire: `etharp_tmr()` has no caller because `LWIP_ARP` is defined nowhere
*(Round-9 section candidate NETSTACK-01.)*


- **Category:** 6 (bug) / 8 (half-finished wiring)
- **Locations:**
  - `modules/network/SMSTCPIP/timers.c:103-124` — the only `etharp_tmr()` caller
    (`arp_timer`) and its scheduling in `sys_timeouts_init()` are both inside `#if LWIP_ARP`
  - `modules/network/SMSTCPIP/include/lwip/opt.h` (whole file, read) and
    `modules/network/SMSTCPIP/include/lwipopts.h` (whole file, read) — neither defines
    `LWIP_ARP`; a repo-wide grep finds no definition anywhere, so the preprocessor treats
    it as 0 in both places
  - `modules/network/SMSTCPIP/etharp.c:209-229` — `etharp_tmr()` itself (increments
    `ctime`, expires STABLE entries at `ARP_MAXAGE` and PENDING entries at
    `ARP_MAXPENDING`, etharp.c:105-108)
- **Problem:** ARP ageing is compiled out. STABLE entries never expire (20-minute
  `ARP_MAXAGE` is dead code) and PENDING entries never time out (20-second
  `ARP_MAXPENDING` is dead code). With `ARP_TABLE_SIZE` of only 2–4
  (lwipopts.h:238-248) and `ETHARP_ALWAYS_INSERT 0`, one PENDING entry for a dead host
  permanently consumes a slot (`find_arp_entry`, etharp.c:243-280, can only recycle
  EMPTY or STABLE entries), and a gateway whose MAC changes (router replaced, VM moved)
  leaves a stale STABLE entry that `etharp_output` keeps using — all off-LAN traffic
  black-holes until the console reboots. `update_arp_entry` only refreshes an entry when
  that host happens to send us a packet.
- **Fix:** Define `LWIP_ARP 1` in `lwipopts.h`, and in the same change raise
  `MEMP_NUM_SYS_TIMEOUT` where it is currently 1 (lwipopts.h:121-125, the non-INGAME_UDP
  in-game build): the ARP timer and the on-demand TCP timer
  (timers.c:88-101, `tcp_timer_needed`) must coexist, and `sys_timeout()` silently drops
  a registration when the pool is empty (timers.c:170-174), so enabling the ARP timer
  without the extra slot can starve the TCP timer instead.
- **Justification if additive:** One define plus one pool count; no new code — it
  reconnects code that is already written, tested by upstream design, and present in
  the object (`timers.o` is in the Makefile object list).
- **Benefit:** Restores the only ARP cache maintenance the stack has; removes a
  permanent-wedge failure mode on a 2–4-entry table.
- **Effort:** S
- **Risk:** Low–Medium — behaviour change is limited to entry expiry, but the timeout-pool
  sizing must land with it (see Fix).
- **Confidence:** High (READ + repo-wide grep: no `LWIP_ARP` definition, no other
  `etharp_tmr` caller in the tree).

### RIPTOPL-413 — genvmc's physical-card copy divides by, and indexes with, unchecked card-supplied superblock geometry
*(Round-9 section candidate PADS-01.)*

- **Category:** 6 (bug/security — device-supplied values)
- **Locations:** `modules/vmc/genvmc/genvmc.c:238-281` (`vmc_mccopy`), esp. `:248-250`, `:258`, `:260`, `:276`
- **Problem:** READ. After checking only the 28-byte magic, `vmc_mccopy` trusts the source card's superblock: `pagesblock = (BLOCKKB*1024)/mcdi->pagesize`, `clustersblock = pagesblock/mcdi->pages_per_cluster`, `blocks = clusters_per_card/clustersblock`, progress `i/(blocks/99)`, and page reads into `&cluster_buf[j * mcdi->pagesize]`. A corrupt/malicious card with `pagesize==0`, `pages_per_cluster==0`, or `clustersblock==0` traps on division by zero on the IOP; `pagesize > 512` walks `cluster_buf` (16 KiB + 16) past its end; `blocks < 99` makes `blocks/99 == 0` and the progress update divides by zero on the first iteration of an otherwise valid small card. The create path (`vmc_mcformat`) sets all of these itself and is safe — only the copy path imports them.
- **Fix:** After the magic check, reject the copy (`-100`-family return) unless `pagesize` is a power-of-two in [128,512] dividing 16 KiB, `pages_per_cluster` is non-zero and divides `pagesblock`, `clustersblock > 0`, and `blocks >= 99` (or compute progress as `i*99/blocks` with a `blocks==0` guard). ~6 lines, deletion-neutral validation only.
- **Benefit:** Removes an IOP trap + heap-adjacent (static buffer) overwrite reachable by inserting a damaged card and choosing Copy — the one genvmc path whose inputs are not RiptOPL's own.
- **Effort:** S · **Risk:** Low (rejects only geometry no real PS2 card reports) · **Confidence:** High (READ, full function)

### RIPTOPL-414 — NBNS receive thread copies the answer address from a fixed offset without checking the datagram is that long
*(Round-9 section candidate NETSMB-09.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/network/nbns/nbns.c:179` (only length check: `>= sizeof(struct NbHeader)`, 12 bytes), `:189` (positive-response test), `:198` (address `memcpy` from `frame[12 + 34 + 4 + 2 + 2]`, i.e. offset 54, 4 bytes)
- **Problem:** Any UDP datagram of ≥12 bytes with the QUERY/response opcode bits, RCODE 0, `ANCount == 1`, a decodable name matching the pending record and the right transaction id is accepted, and the "address" is copied from bytes 54–57 of the static 576-byte `frame` — even if the datagram was, say, 20 bytes long. The bytes beyond the received length are whatever the previous datagram left in `frame`, so a short spoofed/malformed response can install a stale, attacker-influenced IP as the resolved server address and wake the waiter with success. (The name decode itself is RIPTOPL-037; this is the unchecked address read after it.)
- **Fix:** Require `length >= sizeof(struct NbHeader) + 34 + sizeof(struct NbQuestionTrailer) + 4 + 2 + 2 + 4` before the `memcpy` — one comparison against constants already in the expression.
- **Benefit:** A resolved SMB server IP can no longer come from bytes the datagram never contained.
- **Effort:** S
- **Risk:** Low — real NBNS answers are always full-size.
- **Confidence:** READ

### RIPTOPL-415 — httpclient RPC handlers use EE-supplied strings unterminated (only the streaming op terminates them)
*(Round-9 section candidate NETSMB-08.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/network/httpclient/main.c:36` (`CONN_ESTAB` passes `->server` straight through), `modules/network/httpclient/main.c:44-60` (`SEND_GET_REQ` passes `UserAgent`/`host`/`uri`/`mtime` through); sink at `modules/network/httpclient/httpclient.c:362-371` (`sprintf`/`strcat` into `char buffer[512]`, `months[mtime[1]]` indexed by an unchecked RPC byte)
- **Problem:** `HTTP_CLIENT_CMD_STREAM_BEGIN` explicitly NUL-terminates `a->host`/`a->uri` with a comment calling the RPC struct a trust boundary (`main.c`, stream-begin case). The older handlers do not: a `server[30]`, `host[30]`, `uri[128]` or `UserAgent[16]` field filled without a NUL makes `ResolveHostname`/`gethostbyname` and the `sprintf("%s"...)` chain in `HttpSendGetRequest` read past the field into the rest of the 1,024-byte RPC receive buffer and write past `buffer[512]` on the RPC thread's 0x800-byte stack. Separately, `mtime[1]` (a raw RPC byte, 0–255) indexes the 12-entry `months[]` array unchecked at `httpclient.c:371`, and `GetDayInWeek` loops on the same byte.
- **Fix:** Terminate the four string fields in the two older handlers exactly as the stream-begin handler already does (4 one-line stores), and reject `mtime[1] > 11` before formatting. No new mechanism — copying the in-file precedent.
- **Benefit:** Closes a stack-overflow + OOB-read pair on the RPC boundary the module itself already treats as untrusted elsewhere.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-416 — `fatfs_fs_driver_get_mounted_bd_from_index` bounds check is off by one and admits negative indices
*(Round-9 section candidate BDM-06.)*


- **Category:** 6 (bug / security)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:137-147` (check at `:140`)
- **Problem:** `if (mount_info_index > FATFS_FS_DRIVER_MOUNT_INFO_MAX) return NULL;` — with `FF_VOLUMES` = 10 the array has indices 0–9, so index 10 passes the check and reads `fs_driver_mount_info[10]`, one element past the array, returning whatever pointer-sized garbage sits there as a `struct block_device *`. Negative indices pass too and index before the array. Callers feed it FatFs `pdrv` values (`fatfs/diskio.c` `disk_status`/`disk_initialize`/`disk_read`/`disk_write`/`disk_ioctl`, and `fs_driver.c` `get_frag_list` / `fs_ioctl2` via `fs->pdrv`); those are volume-bounded in the current FatFs configuration, which is why this is latent rather than live — but the function's one job is to be the guard, and the guard is wrong at both ends.
- **Fix in words:** Reject `mount_info_index < 0 || mount_info_index >= FATFS_FS_DRIVER_MOUNT_INFO_MAX`.
- **Benefit:** The single choke point every diskio call passes through actually bounds its index; one-character-class fix.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ (the check and array size); INFERRED (present unreachability of index 10 via FatFs `pdrv`)

### RIPTOPL-417 — `InitializeConfigurationROM` leaks the old CROM and every extra unit, and silently drops all added units, on every `iLinkSetNodeCapabilities` call — both copies / Category 6 / 4
*(Round-9 section candidate ILINK-01.)*


- **Locations:** `modules/ilink/iLinkman/src/iLink_crom.c:249-262` (`InitializeConfigurationROM`); identical text at `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_crom.c:249-262`. Callers: `modules/ilink/iLinkman/src/iLink.c:110` (`_start`) and `:270` (`iLinkSetNodeCapabilities`); assault `iLink.c:110,274`. Publication consumer: `modules/ilink/iLinkman/src/iLink_intr.c:324-327` (assault twin at the same lines).
- **Problem:** The function resets state by *forgetting* allocations instead of freeing them. `ExtraCROMUnits[i] = NULL` (line 254) discards up to 16 `malloc`'d unit buffers (from `iLinkAddCROMUnit`, `iLink_crom.c:65-90`) without `free`; `ConfigurationROM = NULL` (line 256) discards the previous CROM buffer, defeating the `free(ConfigurationROM)` that `BuildConfigurationROM` itself performs when it publishes a replacement (`iLink_crom.c:388-391`). At `_start` this is harmless (all pointers are already NULL). But `InitializeConfigurationROM` is also called by the **exported** `iLinkSetNodeCapabilities` (export 7), so every capabilities change (a) leaks the old CROM (~200 B + extras) and every extra-unit allocation in IOP memory, (b) silently removes every unit any module previously added with `iLinkAddCROMUnit` from the published Configuration ROM, and (c) arms a worse state: because the pointer was nulled but `ConfigurationROMSize` keeps its old value, if `BuildConfigurationROM`'s `malloc` then fails (it returns early, `iLink_crom.c:295-297`, leaving both globals untouched), the read-request handler's CROM test `(offset_low - 0x400) < ConfigurationROMSize` still passes and it forms a response pointer as `(unsigned int)ConfigurationROM + …` = NULL + offset, serving a read response from low IOP memory instead of refusing. READ for the leak, the unit drop and the stale size; the malloc-failure-plus-1394-read trigger is INFERRED (needs an allocation failure at exactly that moment).
- **Fix:** In `InitializeConfigurationROM`, free what is being discarded before clearing it (`free(ExtraCROMUnits[i])` per non-NULL slot; let `BuildConfigurationROM`'s existing replace-and-free handle the CROM buffer by *not* nulling `ConfigurationROM` first), and set `ConfigurationROMSize = 0` until the new build publishes. Apply to both copies.
- **Justification (if additive):** No addition — the fix reuses the file's own `free` wrapper and the replace logic `BuildConfigurationROM` already has; it deletes the pointer-nulling that causes all three effects.
- **Benefit:** Removes a per-call IOP leak on an exported path, an undocumented "all CROM units vanish" side effect, and a NULL-based response pointer on allocation failure.
- **Effort:** S
- **Risk:** Low — `_start` behaviour is unchanged (pointers are NULL there); the only behaviour change is on re-initialisation, where freeing replaces leaking.
- **Confidence:** High (READ) for leak + unit drop; Medium (INFERRED trigger) for the stale-size publication arm.

### RIPTOPL-418 — `iLinkFindUnit` accepts a negative `UnitInList` and indexes `NodeData[-1]` — both copies / Category 6
*(Round-9 section candidate ILINK-02.)*


- **Locations:** `modules/ilink/iLinkman/src/iLink_crom.c:193-208` (`iLinkFindUnit`); identical at `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_crom.c:193-208`. Array: `unsigned int NodeData[63]` at `modules/ilink/iLinkman/src/iLink_intr.c:30` (assault twin same). Export 23 in both `exports.tab`s.
- **Problem:** The only guard is `if (UnitInList < nNodes)`. `UnitInList` is a signed `int` parameter of an exported library function, so `UnitInList = -1` (or any negative) satisfies the test whenever `nNodes ≥ 0` and the function immediately reads `NodeData[UnitInList]` — one or more words *before* the array, in IOP memory — and feeds the value into a node-ID computation and a CROM walk against a remote node. This is the same exported-API class as RIPTOPL-172 (which covered the three `trContext` functions in `iLink.c`) but a different function and a different array; it was not in that finding's scope. The in-tree caller is safe (`sbp2_driver.c` passes loop indices `0 … nNodes-1` in both copies), so today this is reachable only by another IOP module linked against iLinkman — exactly the exposure RIPTOPL-172 already established as file-worthy for this API.
- **Fix:** Change the guard to `if (UnitInList >= 0 && UnitInList < nNodes)`, in both copies — the same one-comparison form `iLinkTrRead`/`iLinkTrWrite` already use for their index.
- **Justification (if additive):** One comparison, copied from the validation pattern the same library already established; no new mechanism.
- **Benefit:** Closes the last unguarded caller-supplied index into a driver-global array in the iLinkman export surface.
- **Effort:** S
- **Risk:** Low — no in-tree caller passes a negative index.
- **Confidence:** High (READ — guard, parameter type, array declaration and export status all in-tree).

### RIPTOPL-419 — DEV9 callback-registration exports index their tables with unchecked (and possibly negative) indices
*(Round-9 section candidate IOPCORE-09.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/iopcore/cdvdman/dev9.c:164-168` (`SpdRegisterIntrHandler`: `dev9_intr_cbs[intr]`, table of 16), `dev9.c:176-186` (`dev9RegisterPreDmaCb`/`dev9RegisterPostDmaCb`: tables of 4), `dev9.c:410-417` (`Dev9RegisterPowerOffHandler`: checks `idx < 16` but not `idx >= 0`)
- **Problem:** These are exported functions callable by any IOP module a game loads. An out-of-range `intr`/`ctrl` writes a function pointer into arbitrary adjacent BSS; a negative power-off `idx` writes before `dev9_shutdown_cbs`. The one registration function that does check demonstrates the intended pattern — it is just incomplete (upper bound only) and the other three have no check at all. All in-scope callers pass constants (0, 1, 15), so this is latent, not currently triggered.
- **Fix:** Add the missing range checks (`0 <= intr < 16`, `0 <= ctrl < 4`, `0 <= idx < 16`) and return/ignore on violation, matching `Dev9RegisterPowerOffHandler`'s existing shape.
- **Justification if additive:** Three comparisons in cold registration paths.
- **Benefit:** Closes a BSS-corruption primitive in an exported registration surface.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-420 — Fixed-size UDPFS replies are never length-checked; a short reply is parsed out of stale buffer contents
*(Round-9 section candidate UDPFS-03.)*

- Category: 6 (bug/security — wire-supplied length)
- Locations:
  - `modules/network/udpfs_shared/src/udpfs_core.c:48-80` (`_send_recv`/`_request` return the received byte count; every caller checks only `ret < 0`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:338-353` (open: trusts `reply->handle` from the shared `g_rx_buf`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:484-497` (lseek: trusts `reply->position_*`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:587-607` (getstat: trusts all stat fields)
  - `modules/network/udpfs_shared/src/udpfs_core.c:766-788` (get_sector_count: trusts `reply->size/hisize`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:148-153` (`_path_op`: trusts `reply->result`)
- Problem: `udprdma_recv` returns as soon as a FIN packet completes the transfer, whatever its payload size. A reply shorter than the expected struct leaves the tail of the destination buffer holding the *previous* reply's bytes (all IOMAN replies share the one static `g_rx_buf[312]`), and the caller then acts on the mixture — e.g. `udpfs_core_open` can return a stale server handle from an earlier open, and `get_sector_count` a stale capacity. Distinct from RIPTOPL-044, which covers the variable-length dread *name*; this is the fixed-struct half of the same reply path.
- Fix: In `_request`, fail with `-EIO` when `ret < (int)reply_max` for the fixed-size callers (open/lseek/getstat/path-op replies are exact-size by protocol); dread keeps its own variable-length handling.
- Justification if additive: N/A (one comparison in the shared helper).
- Benefit: Closes stale-handle/stale-stat acceptance on every filesystem operation; makes a truncated or mis-sequenced reply a clean I/O error instead of state confusion.
- Effort: S
- Risk: Low (a conforming server always sends the full struct; the RDMA layer pads to 4 bytes, and struct sizes here are multiples of 4).
- Confidence: High (helper and all call sites read; no minimum-length check exists anywhere on this path).

### RIPTOPL-421 — `udpfs_core_get_sector_count` truncates capacity to a signed `int`: shares ≥ 1 TiB can never register as a block device
*(Round-9 section candidate UDPFS-02.)*

- Category: 6 (bug — width truncation)
- Locations:
  - `modules/network/udpfs_shared/src/udpfs_core.c:738` (return type `int`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:783-788` (`uint64_t total_bytes` → `uint32_t sector_count = total_bytes >> 9` → `return sector_count;`)
  - `modules/network/udpfs_bd/src/udpfs_bd.c:163-173` (`ret = udpfs_core_get_sector_count(0); if (ret < 0) return 0; … g_udpbd.sectorCount = ret;`)
- Problem: At 512-byte sectors, 2³¹ sectors = 1 TiB. For any share ≥ 1 TiB the `uint32_t` sector count has its top bit set and the implicit conversion to the `int` return makes it negative; `udpfs_bd_try_connect` reads every negative value as an error and retries forever — the device silently never appears, with no size-related diagnostic. Between 2 TiB and 4 TiB the count additionally wraps to a small positive value and a wrong, too-small device would register.
- Fix: Return `int64_t` (or `uint32_t` with a separate error out-param) from `udpfs_core_get_sector_count` and store into `sectorCount` without the signed round-trip; reject only a genuine negative reply `result` (already checked at core:777-780).
- Justification if additive: N/A (signature widening only).
- Benefit: UDPFS-BD works with the multi-TB shares that are the normal case for a games drive; today it fails silently at exactly those sizes.
- Effort: S
- Risk: Low (one caller in scope: udpfs_bd; FHI never calls it — grep-verified).
- Confidence: High (arithmetic is fully determined by the cited lines).

### RIPTOPL-422 — Both ministack RX dispatchers will call through a NULL handler for a packet addressed to port 0
*(Round-9 section candidate UDPFS-08.)*

- Category: 6 (bug — wire-reachable NULL call)
- Locations:
  - `modules/network/udpfs_ministack/src/ministack_udp.c:61-64` (`for i<4: if (dport == udp_ports[i].port_src) return udp_ports[i].handler(...)` — empty slots have `port_src == 0` and `handler == NULL`)
  - `modules/network/smap_udpbd/src/ministack.c:220-223` (same pattern in the monolith copy)
- Problem: Any UDP datagram arriving with destination port 0 matches the first empty port slot and is dispatched through its NULL handler pointer, in SMAP receive context. Port 0 is reserved and no legitimate sender uses it, but the packet is entirely wire-controlled and the ministacks perform no earlier port sanity check.
- Fix: Skip empty slots: `if (udp_ports[i].handler != NULL && dport == udp_ports[i].port_src)` in both copies (in the monolith, compare in its existing raw-order convention).
- Justification if additive: N/A (guard on existing condition).
- Benefit: Removes a one-packet IOP crash from the receive path of both network stacks.
- Effort: S
- Risk: Low.
- Confidence: Medium-High (the dispatch logic is read directly; INFERRED that no earlier layer filters destination port 0 — the SMAP RX path in scope passes all frames to `handle_rx_eth` unfiltered).

### RIPTOPL-423 — `do_bind()` uses lazily allocated PCBs without the NULL checks its sibling handlers have
*(Round-9 section candidate NETSTACK-05.)*


- **Category:** 6 (bug — NULL deref under memory pressure)
- **Locations:**
  - `modules/network/SMSTCPIP/api_msg.c:420,425,430` — `do_bind()` calls
    `udp_new()` three times and immediately calls `udp_setflags()`/`udp_recv()`
    on the result; `:436-437` — same pattern for TCP: `tcp_new()` then
    `setup_tcp()` unchecked; the subsequent bind switch (api_msg.c:443-475) then
    dereferences the PCB again
  - Contrast (same file): `do_newconn` checks every allocation
    (api_msg.c:305-341) and `do_connect` checks every allocation
    (api_msg.c:504-541, including TCP at :535-541)
  - `modules/network/SMSTCPIP/include/lwipopts.h:82` — `MEMP_NUM_UDP_PCB 1`:
    the UDP pool has exactly one PCB in every build from this file, so "pool
    exhausted" is a normal state, not an edge case
- **Problem:** `netconn_bind()` posts `API_MSG_BIND` without checking
  `conn->err` (api_lib.c:280-309), so a conn whose PCB creation already failed
  in `do_newconn` reaches `do_bind`, retries allocation against an exhausted
  pool, gets NULL, and the stack's own thread dereferences it
  (`udp_setflags` writes `pcb->flags`; `setup_tcp` writes through `pcb`).
- **Fix:** Copy the guard pattern `do_connect` already uses into `do_bind`
  (set `msg->conn->err = ERR_MEM`, ACK, return). No new mechanism.
- **Benefit:** Pool exhaustion returns an error instead of crashing the TCP/IP
  thread — with a 1-entry UDP pool this is reachable by an ordinary second UDP
  socket.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High (READ; the asymmetry with the two sibling handlers is
  in-file evidence).

### RIPTOPL-424 — `copy_from_pbuf()` walks off the end of the pbuf chain when a fragment's IP length exceeds the delivered bytes
*(Round-9 section candidate NETSTACK-09.)*


- **Category:** 6 (bug/security — wire-input NULL deref)
- **Locations:**
  - `modules/network/SMSTCPIP/ip_frag.c:61-81` — the copy loop is
    `for (; len; p = p->next) { ... p->len ... }` with no `p == NULL` stop
  - Call site `modules/network/SMSTCPIP/ip_frag.c:170` — `ip_reass()` passes
    `len` derived from the fragment header's `IPH_LEN()` field;
    `modules/network/SMSTCPIP/ip.c:219` (`ip_input`) calls
    `pbuf_realloc(p, IPH_LEN(iphdr))` first, but `pbuf_realloc` only shrinks
    a chain (`pbuf.c:320-375`) — it cannot extend the chain to match a forged
    length
- **Problem:** `IP_REASSEMBLY 1` in all non-in-game builds and in-game with
  the RA sizing (lwipopts.h:212-230). A fragment whose IP total-length field
  claims more payload than the Ethernet frame actually delivered leaves
  `len` greater than the chain's real bytes; the copy loop advances past the
  last pbuf and dereferences NULL in the TCP/IP thread.
- **Fix:** Stop the loop at `p == NULL` (one condition), returning the short
  copy so the existing reassembly timeout discards the datagram.
- **Benefit:** A malformed LAN fragment can no longer crash the stack.
- **Effort:** S
- **Risk:** Low
- **Confidence:** Medium — INFERRED in one step: it depends on SMAP delivering
  frames whose byte count can be smaller than a forged IPH_LEN (Ethernet
  minimum-frame padding may mask small overclaims; large ones cannot be
  masked).

### RIPTOPL-425 — ICMP error replies dereference `pbuf_alloc()` results unchecked and copy from the offending packet unchecked
*(Round-9 section candidate NETSTACK-11.)*


- **Category:** 6 (bug — NULL deref under pool pressure)
- **Locations:**
  - `modules/network/SMSTCPIP/icmp.c:138-151` (`icmp_dest_unreach`) and
    `:170-175` (`icmp_time_exceeded`): `q = pbuf_alloc(PBUF_IP, ...)` is
    dereferenced (`idur = q->payload`) with no NULL check, and both then
    `memcpy` `IP_HLEN + 8` bytes from `p->payload` without confirming `p`
    carries that many bytes
- **Problem:** In-game `PBUF_POOL_SIZE` is 8 (lwipopts.h:165). ICMP errors
  are generated precisely when traffic is arriving — i.e. when the pool is
  most contended — so an allocation failure lands on the input path as a
  NULL dereference in the TCP/IP thread.
- **Fix:** `if (q == NULL) return;` in both functions (the surrounding code
  already treats ICMP as best-effort everywhere else).
- **Benefit:** Removes a load-correlated crash in the error-reporting path.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High on the missing checks (READ); Medium on live
  reachability (pool exhaustion timing is INFERRED).

### RIPTOPL-426 — Client netconns never get a semaphore, so `netconn_write()` busy-spins on `WaitSema(0)`
*(Round-9 section candidate NETSTACK-03.)*


- **Category:** 5 (performance, structural) / 6 (bug)
- **Locations:**
  - `modules/network/SMSTCPIP/api_lib.c:157` — `netconn_new_with_proto_and_callback`
    sets `conn->sem = SYS_SEM_NULL` and no code path ever creates it for client conns
    (grep-verified across api_lib.c/api_msg.c/sockets.c)
  - `modules/network/SMSTCPIP/api_lib.c:538-539` — `netconn_write()` waits on
    `conn->sem` whenever `tcp_sndbuf()` hits 0; `:563` — same wait on the `ERR_MEM`
    retry path. `SYS_SEM_NULL` is 0
    (`modules/network/SMSTCPIP/include/arch/sys_arch.h:11`) and
    `sys_sem_wait` is a direct `WaitSema` macro
    (`modules/network/SMSTCPIP/include/lwip/sys.h:82`), i.e. `WaitSema(0)`, an
    invalid semaphore ID that returns an error immediately
  - Contrast: accepted conns *do* get one —
    `modules/network/SMSTCPIP/api_msg.c:259` (`accept_function`:
    `newconn->sem = sys_sem_new(0)`) — and the signal side already guards on it:
    `poll_tcp`/`sent_tcp` only signal `if (conn->sem != SYS_SEM_NULL)`
    (api_msg.c:175-177, 195-197), while `netconn_close`'s wait is likewise guarded
    (api_lib.c:592-593). Only the write waits are unguarded.
- **Problem:** `TCP_SND_BUF` is 2,920 bytes (lwipopts.h:202, `TCP_MSS * 2`). Any
  `netconn_write()` larger than the currently free send buffer — routine for SMB
  writes — reaches the wait loop, `WaitSema(0)` fails instantly, and the caller
  thread spins at full speed, re-checking `tcp_sndbuf()`, until ACKs free space.
  The stack still works, but the writer thread burns the IOP instead of blocking,
  and the ERR_MEM path spins the same way.
- **Fix:** Create the semaphore in `netconn_new_with_proto_and_callback`, exactly
  as `accept_function` already does (`sys_sem_new(0)`, with the existing
  delete-path handling in `netconn_delete`, api_lib.c:228-230, which already frees
  it when non-NULL). One line plus a NULL check.
- **Justification if additive:** Not additive in spirit — it restores the object
  the API was designed around; every other participant (accept path, signal side,
  close path, delete path) already assumes it can exist.
- **Benefit:** Blocking instead of spinning on every large send; removes a
  priority-inversion-style IOP stall during game SMB streaming.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High on the missing creation (READ + grep); the
  `WaitSema(0)`-returns-immediately step is INFERRED from `SYS_SEM_NULL == 0` and
  standard IOP semaphore semantics (not execution-verified).

### RIPTOPL-427 — DEV9 `SpdDmaTransfer` busy-waits forever for DMA completion, semaphore held
*(Round-9 section candidate IOPCORE-08.)*

- **Category:** 5 performance / 6 bugs (hang)
- **Locations:** `modules/iopcore/cdvdman/dev9.c:269-317` (spin at :307-308: `while (dev9_chan->chcr & DMAC_CHCR_TR) { }`)
- **Problem:** The comment says a semaphore wait "hurts throughput", so completion is polled with an empty loop and no timeout, while holding `dma_lock_sem`. If the DMA channel never clears TR (hardware fault, wrong channel state), the IOP spins at full speed forever and every later DEV9 DMA waits on the semaphore behind it. This is the HDD/SMAP data path for the HDD and network builds.
- **Fix:** Bound the poll (iteration cap consistent with the transfer size) and return an error, releasing the semaphore, on expiry — the post-DMA callback already runs unconditionally after the loop.
- **Justification if additive:** None.
- **Benefit:** A stuck DEV9 DMA becomes a recoverable I/O error instead of a total IOP freeze.
- **Effort:** S
- **Risk:** Low-Medium — cap must exceed the slowest legitimate transfer; size-scaling the cap keeps that margin.
- **Confidence:** READ

### RIPTOPL-428 — SCMD path: unbounded hardware-register spins, and `cdvdman_sendSCmd` always reports success
*(Round-9 section candidate IOPCORE-04.)*

- **Category:** 6 bugs / 5 performance
- **Locations:** `modules/iopcore/cdvdman/cdvdman.c:939-973` (three `do { } while (!(CDVDreg_SDATAIN & 0x40))` drain loops and `while (CDVDreg_SDATAIN & 0x80) ;` at :954, all without timeout or yield, holding `cdvdman_scmdsema`); `cdvdman.c:982-997` (`cdvdman_sendSCmd`)
- **Problem:** (1) If the mechacon/CDVD controller never sets the expected SDATAIN bits, `cdvdman_writeSCmd` spins forever in interrupt-enabled thread context with the SCMD semaphore held — clock reads, power-off, GUID/model reads all wedge. (2) Even when `cdvdman_writeSCmd` does fail (returns 0), `cdvdman_sendSCmd` retries it 2,500 times and then `return 1` unconditionally at :997 — the failure result is discarded. Callers that test the result (`ioops.c` CDIOC_READCLOCK / CDIOC_POWEROFF convert `!= 1` to `-EIO`) can therefore never see an SCMD failure, and `sceCdRI`/`sceCdReadClock` return stack/register garbage as valid data after a failed exchange.
- **Fix:** Give the register waits a bounded poll count (the file already bounds the analogous OHCI reset loop at 1,000 in `cdvdman_usb_host_stop`), and make `cdvdman_sendSCmd` return the final `r` instead of literal 1.
- **Justification if additive:** None.
- **Benefit:** SCMD failure becomes visible and bounded instead of an IOP wedge or silently fabricated clock/GUID data.
- **Effort:** S
- **Risk:** Low-Medium — a game that (incorrectly) relies on power-off "succeeding" on a wedged mechacon would now see an error; no in-scope caller does.
- **Confidence:** READ

### RIPTOPL-429 — GPT connect returns success (0) when its scratch allocation fails, so BDM claims the device and FatFs is never tried
*(Round-9 section candidate BDM-01.)*


- **Category:** 6 (bug)
- **Locations:** `modules/bdm/src/part_driver_gpt.c:37-42`
- **Problem:** On `AllocSysMemory` failure `part_connect_gpt` does `return 0`. In BDM's mount protocol 0 means "mounted": `bdm_try_mount` (`modules/bdm/src/bdm.c`, `bdm_try_mount`) records `mount->fs = g_fs[i]` on a 0 return and stops offering the device to any other filesystem. A transient IOP allocation failure at plug time therefore permanently wedges the device as "mounted to GPT" — no partitions, no FatFs attempt, no retry until replug. Every other failure path in the same function returns -1, and the MBR driver's allocation failure (`part_driver_mbr.c`, `part_connect_mbr`) also returns -1, so this is a lone inconsistency, not a convention.
- **Fix in words:** Return -1 on the allocation-failure path, like the function's other failures.
- **Benefit:** Removes a silent, unrecoverable "drive never appears" failure mode triggered by ordinary IOP memory pressure.
- **Effort:** S
- **Risk:** Low — the -1 path is already the exercised failure behaviour of both partition drivers.
- **Confidence:** READ

### RIPTOPL-430 — `USBMASS_DEVCTL_STOP_UNIT` and `USBMASS_DEVCTL_STOP_ALL` bodies are swapped in `fs_devctl`
*(Round-9 section candidate BDM-02.)*


- **Category:** 6 (bug)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:1019-1031` (helpers at `fs_driver.c:177-195`)
- **Problem:** `case USBMASS_DEVCTL_STOP_UNIT:` calls `fatfs_fs_driver_stop_all_bd()` — it unmounts and stops **every** mounted volume. `case USBMASS_DEVCTL_STOP_ALL:` resolves the caller's one volume and calls `fatfs_fs_driver_stop_single_bd(vol)` — it stops only that one. The two bodies do exactly the opposite of what their command names say. A caller asking to stop one unit (e.g. before ejecting one USB stick) tears down all filesystems; a caller asking to stop everything leaves all but one device running. The constant values themselves live in the PS2SDK `usbhdfsd-common.h` (outside this tree), so the swap verdict rests on the names, which are unambiguous, and on the helper names inside this same file, which match the opposite call sites.
- **Fix in words:** Swap the two case bodies: STOP_UNIT stops the resolved single volume, STOP_ALL stops all volumes.
- **Benefit:** Devctl callers get the stop scope they asked for; prevents collateral unmount of unrelated drives and incomplete shutdown on STOP_ALL.
- **Effort:** S
- **Risk:** Low — if any in-tree caller had compensated for the swap it would be visible; no caller of either devctl exists elsewhere in this repo (grep-verified).
- **Confidence:** READ (bodies and call sites); INFERRED (constant semantics, from names — definitions are in the external SDK header)

### RIPTOPL-431 — FileTimeToDate converts ~half of all dates to garbage (SMB1 browse/stat timestamps)
*(Round-9 section candidate NETSMB-01.)*

- **Category:** 6 bugs
- **Locations:** `modules/network/smbman-ra/smb_fio.c:558-611` (definition; leap-day subtraction at `:571-576`), used at `modules/network/smbman-ra/smb_fio.c:614-616` (`smb_statFiller`, feeding `smb_dread`/`smb_getstat`)
- **Problem:** The FILETIME→calendar conversion computes `years = total_seconds / 365 days`, then subtracts the *cumulative* leap-day count (`leapdays = years/4 - years/100 + years/400`, ~103 for the 2020s) from the *remainder* day count in a `u16`. Late in each year the 365-division has already rolled into the next year, the remainder is smaller than `leapdays`, and `days -= leapdays` underflows. Executed proof (host replication of the exact integer ops, including `u16` widths): 2026-11-14 12:34:56 converts to year **2028, month 1, day 65124**; 2026-12-31 likewise; 2025-03-01 converts to Feb 29 (separate `days > daysPerMonth[i]` boundary error at `:583-589`). Sampling the 1st/15th/28th of every month 2020–2030: **198 of 396 dates convert wrong**. Every SMB1 directory listing and stat for files modified on those dates shows a corrupt date. smb2man and nfsman each carry a correct civil-from-days converter (`smb2man_pack_time` / `nfsman_pack_time`); this is a third, divergent, broken copy.
- **Fix:** Delete the `years`/`leapdays` algorithm and replace the body of `FileTimeToDate` with the civil-from-days conversion already proven in-tree (days-since-epoch → era/yoe/doy arithmetic), keeping the same `datetime[8]` output layout.
- **Justification if additive:** Replacement, not addition: ~35 lines of arithmetic removed, ~20 lines of already-in-tree arithmetic reused. Net negative.
- **Benefit:** Correct timestamps for roughly half the calendar in the SMB1 browser; removes a third divergent date converter.
- **Effort:** S
- **Risk:** Low — pure function, host-testable against known FILETIMEs before/after.
- **Confidence:** READ (algorithm executed on host; counts above are measured, not estimated)

### RIPTOPL-432 — usmb2_build_request drains a server-declared SPL of any size; no reply-size ceiling
*(Round-9 section candidate NETSMB-05.)*

- **Category:** 6 bugs/security (also 5 performance — lock held while draining)
- **Locations:** `modules/network/smb2man/usmb2/usmb2.c:331` (only check is `spl < 64`), drain loop at `modules/network/smb2man/usmb2/usmb2.c:469-480`
- **Problem:** After the header, the reply's remaining length is whatever the server's 32-bit SPL says. Leftover bytes are discarded in a `while (spl)` loop reading a buffer-full at a time, with no upper bound tied to what a reply can legitimately be (context buffer 1,024 + the caller's `indata`, at most 64 KiB in smb2man). A broken or hostile server declaring, say, 100 MB keeps the driver — holding the smb2man semaphore — receiving/discarding (or timing out chunk by chunk) instead of failing the request. The SMB1 side's equivalent trust was filed as RIPTOPL-145; the SMB2 side has no equivalent filing.
- **Fix:** Fail (`socket_failed`) when `spl` exceeds `sizeof(usmb2->buf) + indatacount` before reading the body — the same shape as unfs4's record-mark ceiling (NETSMB-06 context, `unfs4.c` already does this).
- **Benefit:** One comparison removes an unbounded, lock-holding drain on every SMB2 request.
- **Effort:** S
- **Risk:** Low — legitimate replies are bounded by the negotiated read/write sizes the driver itself clamps to 64 KiB.
- **Confidence:** READ

### RIPTOPL-433 — Replies are matched to requests only when signing is on (usmb2 MessageId) or never (unfs4 xid)
*(Round-9 section candidate NETSMB-06.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/network/smb2man/usmb2/usmb2.c:404-407` (MessageId checked only inside the `verify` branch); `modules/network/nfsman/unfs4/unfs4.c:475-530` (`unfs4_rpc` non-TINY_TCP path never compares the reply xid at `buf[4..7]` with `unfs4->xid` set at `:483`; the xid comparison exists only in the TINY_TCP-only `recv_reply_from_socket` at `:169-172`, which this build never compiles)
- **Problem:** On an unsigned SMB2 session, `usmb2_build_request` accepts the next frame on the wire as the reply to the current command regardless of its MessageId — the code's own comment at `usmb2.c:398-403` explains why a *verified* reply must answer this request, but the check is gated on `verify`. A delayed/duplicated/mis-sequenced frame is then parsed as e.g. a CREATE reply and its "FID" used for reads/writes. unfs4 has the same hole without even a signed mode: COMPOUND status and results are read from fixed offsets (`unfs4.c:520-529`) of whatever record arrived.
- **Fix:** Move the MessageId comparison out of the `verify` gate in usmb2 (fail the request on mismatch, as the verify path already does); in `unfs4_rpc`, compare `get32(&b[4])` to `unfs4->xid` after the record mark and fail on mismatch. Two comparisons total.
- **Benefit:** Both vendored clients stop interpreting one request's reply as another's — a correctness as well as security property on shared/flaky networks.
- **Effort:** S
- **Risk:** Low — conforming servers always answer in order with the matching id; mismatch today means silent misparse.
- **Confidence:** READ

### RIPTOPL-434 — smb2man_querydir trusts the reply's OutputBufferLength over the bytes actually delivered
*(Round-9 section candidate NETSMB-07.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/network/smb2man/smb2man.c:1241-1269` (length taken at `:1264-1265`)
- **Problem:** `d->len` is set from the server-supplied `OutputBufferLength` field (clamped only to the 4,096-byte `SMB2MAN_DIRBUF`), not from how many bytes `usmb2_build_request` actually placed in `d->data`. If the field claims more than the frame carried, `smb2man_dread` (`smb2man.c:1272+`) parses stale bytes from the previous listing as directory entries (names, sizes, dates shown to the user; a stale `NextEntryOffset` chain is followed within the clamped length). The READ path in the same vendored stack was already hardened against exactly this (the `usmb2_prw` reply-length cross-check in `usmb2.c`); QUERY_DIRECTORY was not.
- **Fix:** Record the delivered byte count from `usmb2_build_request` (frame length minus header/body offsets, as the READ check computes it) and set `d->len = min(claimed, delivered)`.
- **Benefit:** Directory listings can no longer be fabricated out of the previous listing's leftovers by a lying length field.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-435 — smb_closeAll() closes FIDs but never frees the handles — stale slots leak and FIDs can be re-closed on a later session
*(Round-9 section candidate NETSMB-04.)*

- **Category:** 6 bugs
- **Locations:** `modules/network/smbman-ra/smb_fio.c:402-413` (called from `smb_LogOff` and `smb_CloseShare` in the same file)
- **Problem:** `smb_closeAll` sends `smb_Close` for every handle with `smb_fid != -1` but leaves `fh->f`, `fh->smb_fid`, `fh->mode` untouched. Consequences: (1) `smbman_getfilefreeslot()` only reuses slots with `fh->f == NULL`, so after one logoff/close-share every previously opened slot is occupied forever — repeated reconnect cycles exhaust all 32 slots and opens start failing `-EMFILE`; (2) a later `smb_close` on a stale handle re-sends CLOSE for the old FID on the *new* session, where that FID may now belong to a different file. Compare `smb_close` itself, which does `memset(fh, 0, sizeof(FHANDLE)); fh->smb_fid = -1;`.
- **Fix:** After the `smb_Close` call in `smb_closeAll`, clear the slot exactly as `smb_close` does (or factor the clearing into the loop). Deletion-shaped: no new state.
- **Benefit:** Fixes a permanent handle leak across reconnects and a wrong-file close hazard.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-436 — USB READ CAPACITY result stored as sector count without the +1: the last sector of every USB drive is unreachable
*(Round-9 section candidate HDDUSB-05.)*

- **Category:** 6 (bug)
- **Locations:** `modules/usb/usbmass_bd/src/scsi.c:243` (READ CAPACITY(10)); `modules/usb/usbmass_bd/src/scsi.c:260` (READ CAPACITY(16))
- **Problem:** Both capacity paths store `bd->sectorCount = getBI32(&rc…last_lba)`. Per the SCSI spec the field is the address of the *last* logical block, so the block count is `last_lba + 1`. As written, BDM sees every USB drive one sector short; a filesystem/partition ending exactly at the final LBA loses its last sector. Contrast the ATA BDM path, which takes the true sector count from IDENTIFY (`modules/hdd/atad/src/ps2atad.c:1408`). Side effect in the same lines: a zero-capacity device reports `sectorCount == 0xFFFFFFFF` via the (10) path and is needlessly escalated to READ CAPACITY(16) by the sentinel check at `scsi.c:255`.
- **Fix:** Add `+ 1` at both assignments (and the sentinel comparison becomes `== 0x100000000` in u64 — or test `last_lba == 0xFFFFFFFF` before assignment).
- **Benefit:** Correct capacity for every USB mass device; removes a silent one-sector truncation at the end of the drive.
- **Effort:** S · **Risk:** Low (BDM consumers take `sectorCount` as a count everywhere else) · **Confidence:** High (spec semantics; code read).

### RIPTOPL-437 — SMB (USBLD multipart) read past the last part returns success with the buffer untouched
*(Round-9 section candidate IOPCORE-06.)*

- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdman/device-smb.c:175-224` (`DeviceReadSectors`; `rv` initialised `SCECdErNO` at :182, part loop :191-221)
- **Problem:** For `NumParts > 1` each part covers `0x80000` sectors. If `(u32)lsn` falls outside every part's `[lbound, ubound)` window — a read at or beyond `NumParts * 0x80000` — no iteration body runs, nothing is read, and the function still returns `SCECdErNO`. The direct caller that can reach this without the `mediaLsnCount` gate is the ZSO sector cache (`cdvdman.c` `DeviceReadSectorsCached`), whose prefetch window deliberately reads past the current request and can cross the end of the image; those phantom sectors are then cached and served as real data. Single-part images are unaffected (`ubound = 0xFFFFFFFF`).
- **Fix:** Track whether any part matched; return `SCECdErREAD` when none did (or when the loop ends with sectors still unaccounted for).
- **Justification if additive:** One flag/comparison.
- **Benefit:** End-of-image reads on multipart SMB images stop fabricating sector data.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ (code path); INFERRED (that the ZSO prefetch window is the practical trigger)

### RIPTOPL-438 — `smap_udpbd` write path hardcodes 512-byte sectors and ignores the INFO-reply sector size the read path honours
*(Round-9 section candidate UDPFS-04.)*

- Category: 6 (bug)
- Locations:
  - `modules/network/smap_udpbd/src/udpbd.c:230-240` (`pkt.bt.block_count = 4; // 4 x 128b = 512b`, `udp_packet_send_ll(…, buffer, 512)`, `buffer += 512`)
  - `modules/network/smap_udpbd/src/udpbd.c:82-93` (the read path, by contrast, computes `g_read_size = count * g_udpbd.sectorSize`)
  - `modules/network/smap_udpbd/src/udpbd.c:307-316` (`_cmd_info_reply` stores the wire `sectorSize` into `g_udpbd.sectorSize`)
- Problem: If a server reports a sector size other than 512, reads honour it and writes silently send 512 bytes per sector and advance the source pointer by 512 — writing the wrong bytes to the wrong places while reporting success. RIPTOPL-180 covers the missing *validation* of the INFO geometry; this is the separate defect that the write path does not use even a valid, accepted geometry.
- Fix: Drive the write chunking off `g_udpbd.sectorSize` (per-sector send length and pointer advance), the way `_udpbd_read` already does; if non-512 geometries are not to be supported, reject them in `_cmd_info_reply` instead of registering the device.
- Justification if additive: N/A.
- Benefit: Write correctness for any non-512 geometry; removes a silent data-corruption path on a writable block device.
- Effort: S
- Risk: Low (512 remains the behaviour for every 512-reporting server).
- Confidence: High (both paths read side by side at the cited lines).

### RIPTOPL-439 — `sceCdStInit` divides by the game's `bankmax` with no validation
*(Round-9 section candidate IOPCORE-07.)*

- **Category:** 6 bugs (EE RPC / game-supplied input)
- **Locations:** `modules/iopcore/cdvdman/streaming.c:122-141` (`StBanksize = bufmax / bankmax` at :132; results stored into `unsigned short` fields of `SteamingData`, `internal.h:84-96`)
- **Problem:** `bufmax`/`bankmax` arrive unchecked from the game via CDVDFSV (`cdvdfsv/ncmd.c` CDVD_ST_CMD_INIT) or FILEIO devctl. `bankmax == 0` divides by zero (undefined result on MIPS, no trap to catch it); `bankmax > bufmax` yields `StBanksize == 0`, after which `AllocBank` hands out the same bank forever and `Stlsn` never advances — the stream silently stalls; a quotient over 65535 truncates in the `u16` fields and desynchronises `StBufmax` from the game's real buffer, so stream fills can write outside the IOP stream buffer the game supplied.
- **Fix:** Reject `bankmax == 0 || bufmax == 0 || bankmax > bufmax` (return 0, as the other `sceCdSt*` functions do when not initialised) before the division.
- **Justification if additive:** One guard, no new state.
- **Benefit:** A malformed stream-init from any game becomes a refused call instead of corrupt stream state.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ (missing validation and truncation); INFERRED (exact failure manifestation per input)

### RIPTOPL-440 — mcemu FastIO hooks dereference the FastIO allocation without ever checking it exists; the read hook also DMA-transfers a full cluster for a short tail
*(Round-9 section candidate PADS-03.)*

- **Category:** 6 (bug)
- **Locations:** `modules/mcemu/mcemu_rpc.c` — `hookMcman62` (allocation + bind, returns 0 on failure), `hookMcman63` and `hookMcman68` (unchecked `pFastBuf`/`pFastRpcBuf`/`pClientData` use); `sdd.size = MC2_CLUSTER_SIZE` in `hookMcman63` regardless of the chunk's actual `size`
- **Problem:** READ. `hookMcman62` is the only place `pFastBuf` is allocated, and it leaves the globals NULL on allocation or RPC-bind failure. `hookMcman63`/`hookMcman68` — installed as MCMAN exports 63/68 whenever MCMAN ≥ 0x208 registers — use `pFastBuf`/`pClientData` unconditionally, so a game calling sceMcReadFast/WriteFast after a failed 62 dereferences NULL on the IOP. Separately, in `hookMcman63` the DMA descriptor's `size` is hard-coded to `MC2_CLUSTER_SIZE` (0x400) even for the final short chunk where `size < 0x400`, transferring stale buffer bytes past the bytes just read to the EE address (the RPC length field at `rpcbuf[0xC]` does carry the true size, so impact is bounded to an over-transfer, not a wrong length report — stated exactly).
- **Fix:** Early-return an error from 63/68 when `pFastBuf == NULL || pClientData == NULL`; set `sdd.size = size`. 3 lines.
- **Benefit:** Turns a NULL-deref IOP crash on low-memory/bind-failure boots into a clean FastIO failure; stops a 0x400-byte DMA over-transfer on every FastIO read tail.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ, all three functions end-to-end)

### RIPTOPL-441 — `mmce_fs_open`/`dopen` failure exits leave `file->privdata` pointing at a still-free handle slot, which the next open then aliases
*(Round-9 section candidate PADS-04.)*

- **Category:** 6 (bug)
- **Locations:** `modules/mmce/mmceman/src/mmce_fs.c:63-140` (`mmce_fs_open`, P1/P2/P3 timeout and bad-reply exits), `:586-660` (`mmce_fs_dopen`, same shape)
- **Problem:** READ. `open` stores `file->privdata = mmce_fs_find_free_handle()` — a pointer *into* `mmce_fs_fds[]` whose slot value is still `-1` (free) until the card returns an fd. Every pre-fd failure exit returns `-1` without clearing `file->privdata` (contrast the ENOENT exit, which does clear it, and the close patch, which frees the slot on every exit precisely because a stale slot strands/aliases). A failed open therefore leaves iomanX holding a file whose privdata aliases a free slot: the next successful open takes that same slot, and any later operation/close through the failed file reads or frees the *other* file's card fd.
- **Fix:** Set `file->privdata = NULL` on every failure exit of open/dopen (the slot needs no value reset — it was never assigned). ~4 added assignments, no new logic.
- **Benefit:** Removes an fd-aliasing bug in the driver whose fd-pool exhaustion already caused a shipped incident (#120, per the in-tree close patch); failure paths become symmetric with the one correct exit.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ); aliasing consequence INFERRED from iomanX privdata lifetime

### RIPTOPL-442 — `fs_read`/`fs_write` collapse every FatFs error to a 0 return, indistinguishable from EOF / zero-progress
*(Round-9 section candidate BDM-12.)*


- **Category:** 6 (bug)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:465` (`fs_write`), `:484` (`fs_read`)
- **Problem:** Both return `(ret == FR_OK) ? count : 0`. A disk error mid-transfer, an invalidated object, or a removed device all surface to EE as a clean 0 — EOF for readers, "wrote nothing, no error" for writers. Callers that loop until error or completion cannot tell a dead device from end-of-file. The rest of the same file returns negative errnos (`fs_open`, `fs_close`, `fs_remove`, …), so the convention exists in-file; these two ops are the exception.
- **Fix in words:** Return a negative errno (e.g. `-EIO`) when the FatFs call fails, keeping the byte count on success.
- **Benefit:** EE-side error handling (retry, abort, user message) becomes possible on real device failures instead of silent truncation.
- **Effort:** S
- **Risk:** Medium — a caller that treats any non-negative short count as EOF already copes; a caller that loops on 0 expecting progress currently spins, which the change also fixes. Behaviour change needs a call-site glance on the EE side.
- **Confidence:** READ

### RIPTOPL-443 — `smb_CloseAll`'s −1 sentinel can never be stored in the u16 FID table (all three SMB-family files)
**Verification note (2026-11-14): WITHDRAWN — REFUTED on adversarial re-verification: the claimed double-close trigger is defeated by the sole caller's mount-state guard (`device-smb.c:165` checks `smb_mount_state == 1`, `:168` sets it to −1; `DeviceDeinit` delegates to the same guarded `DeviceUnmount`), so the u16/sentinel non-idempotence is latent-only; see Sable-VERIFY-RiptOPL-CAT6.md. Do not act on this finding.**
*(Round-9 section candidate IOPCORE-24.)*

- **Category:** 6 bugs / 2 duplication
- **Locations:** `modules/iopcore/cdvdman/smb.c:1154-1165`, `modules/iopcore/cdvdman/smb2.c:1132-1143`, `modules/iopcore/cdvdman/nfs.c:341-352` (identical bodies); FID storage type `modules/iopcore/common/cdvd_config.h:71` (`u16 FIDs[ISO_MAX_PARTS]`)
- **Problem:** Each copy loops `fd = FIDs[i]; if (fd >= 0) { smb_Close(fd); FIDs[i] = -1; }`. `FIDs` is `u16`, so the `-1` sentinel is stored as 65535 and read back as 65535 — which also passes `fd >= 0`. A second `smb_CloseAll` (DeviceUnmount runs at shutdown and again via DeviceDeinit for SMB) therefore "closes" every slot again: SMB1 sends CLOSE requests for FID 65535, SMB2/NFS return EBADF harmlessly. The guard that was meant to make close-all idempotent does not work in any of the three copies.
- **Fix:** Test `fd != 0xFFFF` (the stored sentinel) instead of `fd >= 0`, in all three copies — or track openness in the per-file tables SMB2/NFS already keep.
- **Justification if additive:** None — a comparison change ×3.
- **Benefit:** Shutdown stops issuing closes for files that were never opened, on every network build.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-444 — smb_lseek64 accepts negative positions (SEEK_SET/SEEK_CUR check only the upper bound)
*(Round-9 section candidate NETSMB-10.)*

- **Category:** 6 bugs
- **Locations:** `modules/network/smbman-ra/smb_fio.c:833-868` (`SEEK_CUR` at `:840-846`, `SEEK_SET` at `:847-853`)
- **Problem:** Both cases reject `r > fh->filesize` but never `r < 0`, so `lseek(fd, -5, SEEK_SET)` or a SEEK_CUR that steps before 0 stores a negative `fh->position`. The next `smb_read` computes `size = fh->filesize - fh->position` (larger than the file) and `smb_ReadFile` casts the negative offset to a huge unsigned SMB offset. Both sibling drivers check this (`smb2man_seek`: `if (base + offset < 0) return -EINVAL;`; nfsman's lseek64 likewise) — smbman-ra is the odd one out.
- **Fix:** Add the same `r < 0` rejection the two sibling drivers already have (one condition per case, or one after the switch).
- **Benefit:** Brings the third driver's seek contract into line; removes a negative-offset read primitive against the server.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-445 — smb_dopen appends "\\*" to fh->name with strcat after a 255-char strncpy — overflow and unterminated-name read
*(Round-9 section candidate NETSMB-11.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/network/smbman-ra/smb_fio.c:660-663`
- **Problem:** `strncpy(fh->name, path, 255)` into `char name[256]` leaves a 255-char path unterminated; the next line reads `fh->name[strlen(fh->name) - 1]` (scanning past the field for a NUL), and the two `strcat`s then append up to 3 more bytes at wherever that scan lands — past the 256-byte field of the static `smbman_fdhandles[]` array for long directory paths. Cross-reference RIPTOPL-040 (the unterminated-`strncpy` pattern in this file); the `strcat` sink here is separate.
- **Fix:** Bound the copy to `sizeof(fh->name) - 3` (leaving room for `\*` + NUL) and force termination, as the open path should; the search pattern then always fits.
- **Benefit:** Removes a static-array overflow reachable with a merely long (not malicious) directory path.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-446 — GPT parser ignores the header's `partition_entry_size` and assumes every entry is 128 bytes
*(Round-9 section candidate BDM-04.)*


- **Category:** 6 (bug)
- **Locations:** `modules/bdm/src/part_driver_gpt.c:68` (`entriesPerSector = bd->sectorSize / sizeof(gpt_partition_table_entry)`); field defined and never read at `modules/bdm/src/include/gpt_types.h:36`
- **Problem:** UEFI requires entry size to be a multiple of 128, not necessarily 128. The parser never reads `pGptHeader->partition_entry_size`; it strides the array by `sizeof(gpt_partition_table_entry)` (128). On a disk formatted with 256-byte entries (produced by some partitioning tools), every entry after the first is read at the wrong offset — GUIDs and LBAs are garbage, so real partitions are missed or phantom bounds are computed from mid-entry bytes. The vendored FatFs GPT check in the same tree rejects any header whose entry size is not exactly 128 (`fatfs/ff.c`, `test_gpt_header`); the BDM driver neither honours the size nor rejects a mismatch.
- **Fix in words:** Either stride by the header's `partition_entry_size` (validating it is a non-zero multiple of 128 no larger than the sector size), or reject headers whose entry size is not 128, matching the FatFs parser's rule. The reject form is the LESS change.
- **Benefit:** GPT disks with non-128-byte entries stop being silently misparsed.
- **Effort:** S
- **Risk:** Low for the reject form (such disks currently misparse anyway); Medium for the stride form (new parsing arithmetic).
- **Confidence:** READ

### RIPTOPL-447 — GPT connect lacks the partition-device filter the MBR connect has, so every partition pseudo-device is re-probed as a GPT disk
*(Round-9 section candidate BDM-05.)*


- **Category:** 6 (bug) / 3 (overengineering-by-omission)
- **Locations:** `modules/bdm/src/part_driver_gpt.c:23-50` (no filter present); contrast `modules/bdm/src/part_driver_mbr.c:46-49`
- **Problem:** `part_connect_mbr` refuses any block device with `sectorOffset != 0` because such a device is a partition, not a raw disk. `part_connect_gpt` has no equivalent check (nor a `parNr` check): BDM offers every newly created partition pseudo-device to the GPT driver too, which reads the partition's own LBA 1 and interprets whatever filesystem bytes live there as a GPT header. Normally the signature check saves it, but the probe is wasted work on every partition mount, and a partition whose data at offset 512 begins with the bytes `EFI PART` plus a plausible header would spawn nested phantom partitions (bounded only by `MAX_PARTITIONS` and the BDM-03 budget). The asymmetry shows the filter was simply never carried over.
- **Fix in words:** Add the same `sectorOffset != 0` early return at the top of `part_connect_gpt`.
- **Benefit:** Removes a wasted probe per partition and a false-positive nested-partition path, for one copied guard.
- **Effort:** S
- **Risk:** Low — a genuine GPT header can only live on a raw device (offset 0); GPT-on-partition is not a supported layout anywhere else in this stack.
- **Confidence:** READ

### RIPTOPL-448 — `sceAtaSecurityEraseUnit` passes NULL as the IDENTIFY buffer; the helper writes 512 bytes to address 0 and dereferences it
*(Round-9 section candidate HDDUSB-04.)*

- **Category:** 6 (bug)
- **Locations:** `modules/hdd/atad/src/ps2atad.c:1218` (call in `sceAtaSecurityEraseUnit`); `modules/hdd/atad/src/ps2atad.c:1146-1150` (`ata_get_security_status`); `modules/hdd/atad/src/exports.tab:17` (export 12)
- **Problem:** `ata_get_security_status(device, devinfo, param)` runs `ata_device_identify(device, param)` — a PIO data-in transfer of 256 u16 words into `param` — and on success reads `param[ATA_ID_SECURITY_STATUS]`. `sceAtaSecurityEraseUnit` calls it with `param == NULL` on its exit path, so a successful erase-unit sequence ends by writing the IDENTIFY block to IOP address 0 and reading the security word back from low memory. Grep verified no in-tree caller of `sceAtaSecurityEraseUnit`/`sceAtaSecuritySetPassword` outside `ps2atad.c`, so this is a latent exported-API bug, not a live in-game path.
- **Fix:** Pass the module's existing `ata_param` buffer instead of `NULL` (one word changed).
- **Benefit:** Removes a guaranteed low-memory write on an exported function's normal exit path.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

### RIPTOPL-449 — DVD TOC builder marks single-layer discs as dual-layer whenever DVDDL emulation is off
*(Round-9 section candidate IOPCORE-21.)*

- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdman/ncmd.c:156-158` (`cdvdman_fill_toc`, DVD branch: `dual = 1` if `(!(flags & IOPCORE_COMPAT_EMU_DVDDL)) || (layer1_start > 0)`)
- **Problem:** As written, `dual` is 1 for every DVD when the EMU_DVDDL compat flag is clear — including ordinary single-layer games whose `layer1_start` is 0. The TOC bytes that follow are then the dual-layer variants (`tocBuff[0] = 0x24`, `[4] = 0x41`, `[14] = 0x21` OTP/PTP) for a disc that has no layer 1, while `maxlsn` at :185 correctly falls back to `mediaLsnCount` when `layer1_start` is 0 — the two halves of the same response disagree. The expected condition for "this is a dual-layer image" is `layer1_start > 0` (compare `sceCdReadDvdDualInfo`, scmd.c, which derives `on_dual` from exactly that).
- **Fix:** Set `dual = (layer1_start > 0)` (keeping the EMU_DVDDL case, which fakes a second layer, as the explicit exception it is in `sceCdReadDvdDualInfo`).
- **Justification if additive:** None — a condition correction.
- **Benefit:** `sceCdGetToc` stops describing single-layer DVDs as dual-layer to games that parse the TOC.
- **Effort:** S
- **Risk:** Medium — TOC bytes are game-visible; needs a hardware check against a game that reads the DVD TOC. Flagged for that reason.
- **Confidence:** INFERRED (condition and its inconsistency with `sceCdReadDvdDualInfo` are READ; that no game depends on the current bytes is not verifiable in this scope)

### RIPTOPL-450 — `cdrom_lseek` SEEK_END subtracts the offset instead of adding it
*(Round-9 section candidate IOPCORE-22.)*

- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdman/ioops.c:352-385` (`SEEK_END` case at :369-371: `r = fh->position = fh->filesize - offset;`)
- **Problem:** POSIX/SCE lseek semantics are `position = filesize + offset` with a (typically negative) offset. As written, `lseek(fd, -N, SEEK_END)` computes `filesize + N`, which the clamp at :376-377 then pins to `filesize` — seeking backwards from EOF always lands on EOF; `lseek(fd, +N, SEEK_END)` moves N bytes *before* EOF. Any FILEIO client that seeks relative to end-of-file (tail reads, footer parsing) gets the wrong position with a success return.
- **Fix:** `fh->position = fh->filesize + offset`, keeping the existing upper clamp and adding the missing lower clamp at 0 (a negative result currently wraps through the `unsigned int position` field and is only caught by the `> filesize` test).
- **Justification if additive:** None.
- **Benefit:** SEEK_END works for the conventional negative-offset call.
- **Effort:** S
- **Risk:** Low-Medium — if any in-game caller was written against the inverted behaviour it would change; no such caller exists in scope.
- **Confidence:** INFERRED (code READ; that callers use conventional semantics is the standard libcdvd/FILEIO contract, not verifiable from this scope alone)

### RIPTOPL-451 — `tcp_keepalive()` transmits a segment whose TCP flags byte is uninitialised pool memory
*(Round-9 section candidate NETSTACK-07.)*


- **Category:** 6 (bug)
- **Locations:**
  - `modules/network/SMSTCPIP/tcp_out.c:530-570` — `tcp_keepalive()` fills
    src/dest/seqno/ackno/wnd/urgp, sets the header length via
    `TCPH_HDRLEN_SET` (which *preserves* the existing flags bits,
    tcp.h `TCPH_HDRLEN_SET` macro), computes the checksum and sends — but never
    calls `TCPH_FLAGS_SET`. Every other segment constructor in the same file
    does: `tcp_output`'s ACK path (tcp_out.c:360), `tcp_rst` (:485),
    `tcp_enqueue` (:220)
  - The pbuf comes from `pbuf_alloc(PBUF_IP, TCP_HLEN, PBUF_RAM)` (tcp_out.c:542);
    with `MEM_LIBC_MALLOC 1` that memory is `AllocSysMemory` output
    (ps2ip.c:524-533) — not zeroed
- **Problem:** A keepalive probe goes on the wire with whatever flags the
  recycled memory held — potentially SYN/RST/FIN bits set — which can reset
  or confuse the very connection the probe exists to preserve. Reachable
  whenever a connection has `SOF_KEEPALIVE` (settable via the exported
  `lwip_setsockopt` SO_KEEPALIVE, sockets.c:1197-1330) and goes idle past
  `pcb->keepalive + keep_cnt * TCP_KEEPINTVL` (tcp.c:632-644).
- **Fix:** One line: `TCPH_FLAGS_SET(tcphdr, TCP_ACK);` alongside the other
  field initialisations.
- **Benefit:** Keepalive probes become the well-formed ACK-only segments the
  peer expects.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High that no setter exists in the function (READ; the three
  sibling constructors establish the pattern). Which garbage values occur in
  practice is INFERRED (depends on pool recycling).

### RIPTOPL-452 — `netconn_recv()`/`netconn_write()` dereference `conn->pcb.tcp` where the tree itself elsewhere guards it
*(Round-9 section candidate NETSTACK-08.)*


- **Category:** 6 (bug — NULL deref on errored connections)
- **Locations:**
  - `modules/network/SMSTCPIP/api_lib.c:422` — `netconn_recv()`:
    `if (conn->pcb.tcp->state == LISTEN)`; `:538` — `netconn_write()`:
    `tcp_sndbuf(conn->pcb.tcp)` inside its wait loop
  - `modules/network/SMSTCPIP/api_msg.c:190` — `err_tcp()` sets
    `conn->pcb.tcp = NULL` on any fatal TCP error; the state the guards must
    handle is therefore a designed-in state
  - The guard pattern already exists in-tree for exactly this situation:
    `modules/network/SMSTCPIP/sockets.c` ~950 (`lwip_getsockname`, "Failed
    connects may free the PCB but leave the descriptor open"), ~1090
    (`lwip_getsockopt`), ~1300 (`lwip_setsockopt`) — all check
    `pcb.tcp == NULL` — and `netconn_peer` (api_lib.c:243-270) checks per-type
- **Problem:** `netconn_recv`'s earlier `conn->err != ERR_OK` early-return
  covers the common case, but the socket-layer callers run in other threads
  without the core lock (direct netconn API), so an error delivered between
  the check and the dereference — and `netconn_write`'s loop, which re-reads
  `pcb.tcp` on every spin iteration (see NETSTACK-03) — can dereference the
  NULLed PCB instead of returning `ERR_CONN`/`ERR_ABRT`.
- **Fix:** Apply the tree's own existing guard (`pcb.tcp == NULL` → return
  `ERR_CONN`) at both sites. No new mechanism; consistency with three
  existing sites.
- **Benefit:** Errored/desynced sockets fail with an error code instead of an
  IOP exception.
- **Effort:** S
- **Risk:** Low
- **Confidence:** Medium — the unguarded dereferences and the NULL-setting
  error path are READ facts; the inter-thread window that makes them live is
  INFERRED from the core-locking call model (tcpip.h:43-50).

### RIPTOPL-453 — `smap_udpbd`'s transmit path has no link-up wait and no failure exit: `smap_transmit` retries the TX ring forever
*(Round-9 section candidate UDPFS-16.)*

- Category: 5/6 (performance/robustness — structural)
- Locations:
  - `modules/network/smap_udpbd/src/xfer.c:201-217` (`while (HandleTxReqs(...) < 0) DelayThread(100);` — unbounded, and the function then returns 0 unconditionally)
  - Contrast `modules/network/udpfs_smap/src/xfer.c:253-279` (the sibling copy waits up to 2 s for `SmapIsInitialized` and returns -1 if the link never comes up — a local fix its ORIGIN.txt records; the monolith copy never got it)
- Problem: In the monolith, any caller transmitting before the driver is initialised, or after the TX ring wedges, spins at 100 µs intervals forever while holding `tx_sema`, and success is reported regardless. This is the same divergence class as UDPFS-05: local hardening applied to one SMAP copy (RIPTOPL-127 family) and not the other.
- Fix: Port the sibling's bounded initialisation wait + -1 return into this copy's `smap_transmit`; leave the established blocking-while-healthy contract intact, as the sibling does.
- Justification if additive: N/A (port of existing in-tree code).
- Benefit: A wedged or not-yet-up adapter surfaces as an error to udpbd instead of a silent infinite spin holding the TX semaphore.
- Effort: S
- Risk: Low-Medium (touches the monolith's only TX entry; behaviour while healthy is unchanged).
- Confidence: High (both copies read in full; the sibling's fix and its rationale are documented in `udpfs_smap/ORIGIN.txt`).

### RIPTOPL-454 — Streaming EE read busy-spins on `sceSifDmaStat` with no bound
*(Round-9 section candidate IOPCORE-11.)*

- **Category:** 5 performance / 6 bugs (hang)
- **Locations:** `modules/iopcore/cdvdman/streaming.c:234-235` (`while (sceSifDmaStat(dmat_id) >= 0) { };` in `ReadSectorsEE`)
- **Problem:** The retry loop directly above it was deliberately fixed to yield (`DelayThread(500)`, comment citing #185: retrying with interrupts suspended hangs the IOP), but the completion wait that follows is still an empty spin. A DMA that never completes wedges the CDVD thread — and with it every disc read — at full CPU. Same pattern family as RIPTOPL-210 but a distinct site in cdvdman proper.
- **Fix:** Yield inside the wait (DelayThread, as above) and/or bound it and fail the read.
- **Justification if additive:** None.
- **Benefit:** Stream reads degrade to an error instead of an IOP freeze on a stuck SIF DMA.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-455 — igrmbox `mbSendEE` is a third copy of the filed sysmemSendEE spin pattern — with no yield at all
*(Round-9 section candidate IOPCORE-10.)*

- **Category:** 2 duplication / 5 performance (cross-reference RIPTOPL-210)
- **Locations:** `modules/iopcore/igrmbox/igrmbox.c:75-88`
- **Problem:** RIPTOPL-210 filed the two unbounded busy-spins in cdvdfsv's `sysmemSendEE`. igrmbox contains the same code again as `mbSendEE`: `do { sceSifSetDma } while (!id);` with interrupts toggled but no delay, followed by `while (sceSifDmaStat(id) >= 0) ;`. While the in-game menu is open this thread is the EE's only mailbox; if the SIF DMA queue stays full or a transfer never completes, the IOP spins here forever instead of answering END/WAKE — the menu can neither close nor resume the game. It is a separate module from CDVDFSV, so a fix applied to -210 alone will not touch it.
- **Fix:** Apply whatever bounded/yielding form -210's fix takes to this copy too (DelayThread between `sceSifSetDma` retries, as `streaming.c` ReadSectorsEE already does at :226-232), and note the copy in -210's fix scope.
- **Justification if additive:** None — same change, second location.
- **Benefit:** The in-game menu's exit path stops sharing CDVDFSV's hang.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-456 — Game patch modules write fixed offsets into a target module's text with no size or version check
*(Round-9 section candidate IOPCORE-12.)*

- **Category:** 6 bugs
- **Locations:** `modules/iopcore/patches/apemodpatch/main.c:105-116` (writes up to `text_start + 0x3d1c`), `modules/iopcore/patches/f2techioppatch/main.c:49-50` (`+0x16bc`), `modules/iopcore/patches/iremsndpatch/main.c:87-108` (reads at `+0xac0/+0xac4`, writes up to `+0x1ff8`)
- **Problem:** All three locate the target module by ID in loadcore's list and immediately dereference hard-coded offsets. None checks `m->text_size` against its largest offset, and none verifies the instruction/word already present at the patch site. A different revision of the target module than the one the patch was written against (the games these target shipped multiple driver revisions) is patched blind: writes land in whatever follows the text in IOP RAM, or overwrite unrelated instructions, with no error path.
- **Fix:** Before patching, require `m->text_size` to exceed the largest offset used, and verify one known original word per patch site (f2tech already records the original value `0x8000` in its comments); abort with the existing "could not find module" style failure otherwise.
- **Justification if additive:** A comparison (and optionally one word compare) per module, in already-existing guard position (`if (m != NULL)`).
- **Benefit:** A mismatched driver revision fails the patch cleanly instead of corrupting IOP memory.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ (absence of any check in all three files)

### RIPTOPL-457 — pademu `_start` reads 8 bytes of `argv[1]` with no length check
*(Round-9 section candidate PADS-02.)*

- **Category:** 6 (bug — unchecked caller-supplied length)
- **Locations:** `modules/pademu/pademu.c:118-125` (file lines: the two `mips_memcpy` calls in `_start`), and `:138` (`argv[1][3]`)
- **Problem:** READ. `if (argc > 1)` is the only guard before `mips_memcpy(&raw, argv[1], 4)` and `mips_memcpy(&macro_settings, argv[1]+4, 4)`. IRX argv strings are NUL-terminated load arguments, not fixed 8-byte records: a loader (or a future EE caller) passing a 1–7-byte argument makes the module read past the string into adjacent IOP memory and interpret it as pad-enable/vibration/multitap bits and macro settings. Every other consumer of this argument in-tree passes a full record, so this is latent, not live — but the module itself never states or checks the contract.
- **Fix:** Require `argc > 1 && strlen(argv[1]) >= 8` (or copy `min(strlen,8)` into a zeroed record) before either copy; keep the defaults otherwise. 1–2 lines.
- **Benefit:** Closes the only unbounded load-argument read in the pad stack; makes the 8-byte contract explicit at the single place it is assumed.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ); live exploitability INFERRED-low (current EE caller passes a full record)

### RIPTOPL-458 — genvmc STATUS devctl copies to the caller's buffer with none of the validation the CREATE case applies in the same switch
*(Round-9 section candidate PADS-05.)*

- **Category:** 6 (bug — inconsistent validation)
- **Locations:** `modules/vmc/genvmc/genvmc.c` `genvmc_devctl` (CREATE case validates `args == NULL || arglen < sizeof(createVMCparam_t)`; STATUS case calls `vmc_status((statusVMCparam_t *)buf)` with no `buf == NULL` / `buflen < sizeof(statusVMCparam_t)` check)
- **Problem:** READ. `vmc_status` unconditionally `memcpy`s 76 bytes to `buf`. A devctl caller passing a short or NULL out-buffer — exactly the class of caller the CREATE case in the same function defends against — writes 76 bytes over whatever follows. In-tree EE caller passes a correct buffer today (INFERRED from the shared header contract), so this is a latent trap for the next caller, not a live overflow.
- **Fix:** Mirror the CREATE check: `if (buf == NULL || buflen < sizeof(statusVMCparam_t)) r = -EINVAL;`. 1 line.
- **Benefit:** Makes the module's only three commands uniformly defensive; cheapest possible close.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ, full devctl + both helpers)

### RIPTOPL-459 — smb_Connect copies the server IP with strncpy(…, 16) and no termination guarantee; two later sprintf("%s") consumers trust it
*(Round-9 section candidate NETSMB-17.)*

- **Category:** 6 bugs
- **Locations:** `modules/network/smbman-ra/smb.c:1601-1618` (`strncpy(server_specs.ServerIP, SMBServerIP, 16)` at `:1618`; field is `char ServerIP[16]` in `server_specs_t`, `smb.h`)
- **Problem:** If the EE-supplied IP string occupies all 16 bytes without a NUL, `server_specs.ServerIP` is left unterminated, and `smb_GetShareList` / `smb_OpenShare` in `smb_fio.c` later `sprintf(tree_str, "\\\\%s\\IPC$", specs->ServerIP)` / `"\\\\%s\\%s"` — reading past the field into `MaxBufferSize` and beyond until a zero byte happens to appear. (With a terminated ≤15-char IP, `smb_OpenShare`'s `tree_str[274]` is an exact fit for a 255-char share name — see checked-clean; the unterminated-IP case is the one that breaks it.)
- **Fix:** Force `server_specs.ServerIP[15] = '\0'` after the `strncpy` (one line), matching the termination the smb2man LOGON handler already performs on its copy.
- **Benefit:** Removes an over-read feeding two `sprintf`s for the price of one store.
- **Effort:** S
- **Risk:** Low
- **Confidence:** INFERRED (depends on the EE-side `smbLogOn_in_t.serverIP` field being fillable to all 16 bytes without a NUL — the struct lives in ps2sdk's `ps2smb.h`, outside this scope; the IOP-side missing terminator is READ)

---

### RIPTOPL-460 — NTLMv2 client challenge is the constant 0x01×8 and the timestamp is zero (vendored usmb2)
*(Round-9 section candidate NETSMB-16.)*

- **Category:** 6 bugs/security
- **Locations:** `modules/network/smb2man/usmb2/ntlm.c:230-236` (blob zeroed at `:230-232`; `memset(..., 1, 8)` client challenge at `:235-236`, marked `QQQQ` upstream)
- **Problem:** The NTLMv2 client challenge — the client's only freshness contribution — is hard-coded to eight 0x01 bytes, and the blob timestamp is left at 0. A captured NTLMv2 response is therefore a pure function of the server challenge and password hash, with no client-side nonce: anything that can elicit or replay server challenges faces a fully deterministic response, and servers that log/validate timestamps see a constant 1601-01-01. This is upstream usmb2 behaviour carried in vendored, but it is live in this build's only SMB2 authentication path.
- **Fix:** Fill the 8 challenge bytes (and the timestamp) from a local entropy/time source at auth time — smb2man already has a clock path (its keepalive ticks / `GetSystemTime` equivalents on the IOP). Small, contained to `ntlm_generate_auth`.
- **Justification if additive:** A few lines replacing a `memset`; required for the NTLMv2 construction to provide the freshness it exists for.
- **Benefit:** Restores client-side freshness to every SMB2 logon.
- **Effort:** S
- **Risk:** Low — servers accept any client challenge value.
- **Confidence:** READ (the constant is in the code; the protocol consequence is standard NTLMv2 semantics)

### RIPTOPL-461 — `fs_dread` can hand EE an unterminated directory name for a maximum-length (255-char) LFN
*(Round-9 section candidate BDM-08.)*


- **Category:** 6 (bug)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:650`
- **Problem:** `strncpy(buffer->name, fno.fname, 255);` copies into `iox_dirent_t.name[256]`. `FILINFO.fname` holds up to `FF_LFN_BUF` (255) characters plus its terminator. When a directory entry's name is exactly 255 characters, `strncpy` copies 255 non-NUL bytes and — per strncpy semantics — writes no terminator; byte 255 of the destination keeps whatever the previous `dread` call (or the EE caller's buffer) left there. If that byte is non-zero, EE code reading the name as a C string runs past the field. Every other name copy in this driver is either bounded with an explicit terminator (`fs_ensure_typed_driver`) or goes through FatFs' own terminated buffers.
- **Fix in words:** Copy with bound 255 and explicitly write `buffer->name[255] = '\0'` afterwards (or copy 256 and rely on `fno.fname`'s own terminator — the explicit write is the smaller, clearer change).
- **Benefit:** Closes an EE-visible over-read on legal, if rare, maximum-length filenames.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ

### RIPTOPL-462 — `bdm_get_bd` silently leaves the tail of the caller's array unwritten, and `bdm_connect_bd` silently drops devices when the mount table is full
*(Round-9 section candidate BDM-13.)*


- **Category:** 6 (bug)
- **Locations:** `modules/bdm/src/bdm.c:143-152` (`bdm_get_bd`), `modules/bdm/src/bdm.c:51-71` (`bdm_connect_bd`)
- **Problem:** `bdm_get_bd(pbd, count)` fills only `min(count, MAX_CONNECTIONS)` entries and gives the caller no count back and no terminator guarantee — a caller passing `count > 20` (or simply expecting all `count` slots written) reads uninitialised pointers from its own array. Symmetrically, `bdm_connect_bd` falls off the end of its slot loop with no log line when all 20 slots are taken: the device is dropped with less trace than the sector-size refusal path (which carefully logs and callbacks, `bdm.c` `bdm_try_mount`). Both are exported BDM API surface (`modules/bdm/src/exports.tab`), consumed by other modules that cannot see the table size.
- **Fix in words:** In `bdm_get_bd`, NULL-fill the remainder of the requested array; in `bdm_connect_bd`, add the same style of `M_PRINTF` refusal line when no slot is free. No API change.
- **Benefit:** Removes an uninitialised-pointer read for API consumers and makes table exhaustion diagnosable instead of silent.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ (the loops); INFERRED (whether any current consumer passes count > 20 — none in this scope can be checked, the export is cross-module)

### RIPTOPL-463 — `mstack:` exchange silently truncates an over-long reply, indistinguishable from a complete one
*(Round-9 section candidate UDPFS-19.)*

- Category: 6 (bug — minor)
- Locations:
  - `modules/network/udpfs_ministack/src/ministack_xchg.c:68-70` (`len = udplen - sizeof(udp_header_t); if (len > MSTACK_XCHG_REPLY_MAX) len = MSTACK_XCHG_REPLY_MAX;`)
  - `modules/network/udpfs_ministack/src/ministack_xchg.c:116-120` (the truncated length is returned as the reply length with no flag)
- Problem: The length validation immediately above (:63-67) is careful — UDP length is checked against the IP length before any FIFO read — but a reply longer than the 1016-byte cap is then *cut* rather than rejected, and the EE caller receives a syntactically valid, silently incomplete RA response. For a text request/reply protocol (RAP1/RAQ1 family), a truncated tail is worse than an error: it parses as a shorter answer.
- Fix: Reject instead of clamp: `if (len > MSTACK_XCHG_REPLY_MAX) return -1;` in the handler (the frame is dropped and the exchange times out, which the EE already handles).
- Justification if additive: N/A.
- Benefit: Oversized replies fail loudly through the existing timeout path instead of corrupting RA state with partial data.
- Effort: S
- Risk: Low (any reply over the cap is already unusable whole).
- Confidence: High (handler and caller read in full; the reply struct's fixed 1024-byte size is at `mstack_xchg.h:32-38`).

---

### RIPTOPL-464 — `udpfs_core_close` discards the close reply entirely and always reports success
*(Round-9 section candidate UDPFS-17.)*

- Category: 6 (bug — minor)
- Locations: `modules/network/udpfs_shared/src/udpfs_core.c:359-382` (the reply buffer is filled by `_request` and never inspected — no `msg_type` check, no `result` check; `return 0` unconditional)
- Problem: Every other core operation validates its reply type; close alone cannot distinguish success, a server error, or a mis-sequenced packet. The ioman wrapper frees the local fd regardless (`udpfs_ioman.c:214-218`), so a failed server-side close leaks the server handle invisibly until the peer times it out (per `udpfs_fhi/ORIGIN.txt`, peers time out after 1 h of silence by default).
- Fix: Check `reply->msg_type == UDPFS_MSG_CLOSE_REPLY` and return `reply->result` when negative, matching the pattern used by the neighbouring operations.
- Justification if additive: N/A.
- Benefit: Server handle leaks become visible errors instead of hour-long ghosts.
- Effort: S
- Risk: Low.
- Confidence: High.

### RIPTOPL-465 — CDVDFSV stream read returns an uninitialised error word when `sectors == 0`
*(Round-9 section candidate IOPCORE-14.)*

- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdfsv/ncmd.c:235-252` (`cdvdSt_read`: `u32 err;` declared at :237, written only by `sceCdStRead` inside the loop, packed into the result at the end)
- **Problem:** With `St->sectors == 0` the loop body never runs, `err` is never assigned, and the function returns `(rpos & 0xFFFF) | (err << 16)` with stack garbage in the high half — the EE's libcdvd reads that as the stream error code. Every other path assigns `err` on every iteration.
- **Fix:** Initialise `u32 err = 0;` (or `SCECdErNO`).
- **Justification if additive:** None.
- **Benefit:** Deterministic RPC result for a legal zero-length stream read.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-466 — `hookMODLOAD` uses `getModInfo`'s result without checking it — the one unhardened caller left in scope
*(Round-9 section candidate IOPCORE-15.)*

- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdman/ioplib_util.c:276-322` (`getModInfo("modload\0", &info);` at :282, then reads/writes `info.exports[7..22]`)
- **Problem:** `getModInfo` returns 0 and leaves `info.exports` uninitialised when the library is absent (documented in the sibling callers). Every other caller in scope was hardened against exactly this: `device-mmce.c` DeviceFSInit, `device-udpfs.c` DeviceFSInit, `device-smb.c` `ps2ip_init`, `device-http.c` `httpEnsureReady`, and ioplib_util's own `hookAllocSysMemory` (:382) and `heapdiag.c`'s install all check the return. `hookMODLOAD` alone dereferences and *writes through* the possibly-garbage pointer — it overwrites MODLOAD's export table through whatever address the stack held. MODLOAD is in practice always resident (it loaded cdvdman), so this is a latent inconsistency, not a live crash — but it is the only place the scope's own established guard pattern was not applied.
- **Fix:** `if (!getModInfo("modload\0", &info)) return;` at the top, matching the sibling callers.
- **Justification if additive:** One guard, pattern already in the same file.
- **Benefit:** Consistency with the scope's hardened convention; removes a garbage-pointer write if the load order ever changes.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-467 — `apaOpen` compares the password of a cache entry after freeing the entry
*(Round-9 section candidate HDDUSB-09.)*

- **Category:** 6 (bug, latent)
- **Locations:** `modules/hdd/apa/src/hdd_fio.c:510-512`
- **Problem:** On the main open path, `apaCacheFree(clink)` is called and `apaPassCmp(clink->header->fpwd, …)` then reads through the freed entry. Today this is benign in practice — `apaCacheFree` only relinks the entry, the cache semaphore is held, and no allocation intervenes before the read — but it is a use-after-free by contract: any future change that allocates inside the window silently corrupts the password check.
- **Fix:** Move the `apaPassCmp` call above the `apaCacheFree` (two lines reordered; under the OSD build the comparator is a stub, so this is free).
- **Benefit:** Removes a latent UAF on the most-executed APA path at zero cost.
- **Effort:** S · **Risk:** Low · **Confidence:** High that the ordering is as described; Medium that it can ever bite (hence last of the Cat-6 items).

### RIPTOPL-468 — INFERRED: SMAP capability check in both neutrino copies is mis-parenthesised (`!x & MASK`), correct only by accident of the mask's value
*(Round-9 section candidate UDPFS-14.)*

- Category: 6 (bug — latent)
- Locations:
  - `modules/network/udpfs_smap/src/smap.c:516` (`if (!SPD_REG16(SPD_R_REV_3) & SPD_CAPS_SMAP) return -1;`)
  - `modules/network/smap_udpbd/src/smap.c:522` (identical expression)
  - `modules/network/smap-ingame/smap.c:541` (the third copy has the correct form: `if ((SPD_REG16(SPD_R_REV_3) & SPD_CAPS_SMAP) == 0)`)
- Problem: C precedence makes this `(!rev3) & SPD_CAPS_SMAP`: the result is non-zero only when `rev3 == 0` *and* the mask's bit 0 is set. It therefore behaves like the intended test only if `SPD_CAPS_SMAP` is exactly 1; for any multi-bit or higher-bit mask the check silently never fires (compare RIPTOPL-093, which filed the *REV_1* gate in the smap_udpbd copy as a no-op — this is the adjacent REV_3 expression, present in both neutrino copies). INFERRED: `SPD_CAPS_SMAP` is defined in the PS2SDK `dev9.h`, which is not in this tree, so its value could not be verified from the pin.
- Fix: Rewrite both copies in the smap-ingame form, `(SPD_REG16(SPD_R_REV_3) & SPD_CAPS_SMAP) == 0`.
- Justification if additive: N/A (parenthesisation).
- Benefit: The capability gate means what it says in all three copies, independent of an SDK constant's value.
- Effort: S
- Risk: Low (if the mask is 1, behaviour is unchanged; if it is not, the current behaviour is wrong anyway).
- Confidence: Medium (expression and divergence verified in-tree; the mask value is INFERRED, as marked).

### RIPTOPL-469 — INFERRED: `udpfs_ministack` registers its RX callback and takes over the tty before the duplicate-load check
*(Round-9 section candidate UDPFS-15.)*

- Category: 6 (bug — lifecycle ordering)
- Locations:
  - `modules/network/udpfs_ministack/src/main.c:49` (`smap_register_rx_callback(handle_rx_eth, 44)` — installs a pointer into this module copy in the resident SMAP driver)
  - `modules/network/udpfs_ministack/src/main.c:61` (`udptty_init()` — closes fds 0/1 and re-registers the tty driver)
  - `modules/network/udpfs_ministack/src/main.c:63-66` (only *after* both: `RegisterLibraryEntries` failure returns `MODULE_NO_RESIDENT_END`, freeing this copy)
  - Contrast `modules/network/udpfs_ministack/src/ministack_xchg.c:11,199-203` and `main.c:68-69`, where the same authors deliberately register the `mstack:` device only once resident, "a second load's memory is freed, and `mstack` must not point into it" — the rx-callback registration two statements earlier has exactly the exposure the xchg comment guards against.
- Problem: On a duplicate load, the resident SMAP driver is left holding a receive callback into freed module memory, and the tty has been closed/re-registered by a module that is about to vanish. INFERRED: whether a duplicate load of this IRX can actually occur through RiptOPL's loader paths was not traced from this scope.
- Fix: Move `smap_register_rx_callback` and `udptty_init()` below the `RegisterLibraryEntries` check, matching the ordering the xchg registration already uses in the same function.
- Justification if additive: N/A (reorder only).
- Benefit: Duplicate-load safety consistent with the module's own stated rule.
- Effort: S
- Risk: Low.
- Confidence: Medium (ordering read directly; reachability INFERRED, as marked).

### RIPTOPL-470 — Eight `ps2ip` exports are an empty stub, while in-tree modules import and call them
*(Round-9 section candidate NETSTACK-02.)*


- **Category:** 8 (half-finished feature) / 6 (bug — indeterminate return values)
- **Locations:**
  - `modules/network/SMSTCPIP/exports.tab:17-24,30` — slots 14 (`lwip_select`),
    15 (`lwip_ioctl`), 16 (`lwip_getpeername`), 17 (`lwip_getsockname`),
    18 (`lwip_getsockopt`), 20 (`ps2ip_setconfig`), 21 (`ps2ip_getconfig`) and
    27 (`netif_find`) are all `DECLARE_EXPORT(ps2ip_Stub)`; `ps2ip_Stub` is an empty
    function (`modules/network/SMSTCPIP/ps2ip.c:274-277`)
  - The real implementations exist but are compiled out in the default build
    (`FULL_LWIP ?= 0`, `modules/network/SMSTCPIP/Makefile:8,31-33`):
    `modules/network/SMSTCPIP/sockets.c:630` (`lwip_select`/`lwip_selscan`),
    `sockets.c:899` (`lwip_getpeername`/`lwip_getsockname`/`lwip_getsockopt`),
    `sockets.c:1377` (`lwip_ioctl`), `modules/network/SMSTCPIP/ps2ip.c:97`
    (`ps2ip_getconfig`/`ps2ip_setconfig`), `modules/network/SMSTCPIP/netif.c:110`
    (`netif_find`/`netif_remove`)
  - Consumers (corroboration, outside this section's scope):
    `modules/network/common/smstcpip.h:56-57,109-119,147-157` declares imports 14–21
    and 27 for every consumer of the shared header;
    `modules/network/smbman-ra/imports.lst:13` imports `I_lwip_select`;
    `modules/network/smb2man/imports.lst:13-15`, `modules/network/nfsman/imports.lst:14-16`
    and `modules/network/httpclient/imports.lst:15,18-19` import `lwip_select`,
    `lwip_ioctl` and/or `lwip_getsockopt`
  - In-scope consumer: `modules/network/ps2ips/ps2ips.c` imports all of them
    (`modules/network/ps2ips/imports.lst`, ps2ip block) and dispatches RPCs to them
    (`do_select` :573, `do_ioctlsocket` :592, `do_getsockname` :601,
    `do_getpeername` :619, `do_getsockopt` :637, `do_getconfig` :562,
    `do_setconfig` :568)
- **Problem:** The stack's Makefile comment says the FULL_LWIP functions "were removed
  since OPL does not use them", but OPL's own in-game drivers do import them. Any call
  lands in `ps2ip_Stub`, which returns whatever happens to be in `$v0` — so a
  `select()` can report a random positive count, a `getsockopt()` "succeeds" with
  uninitialised output (compounding RIPTOPL-100's 128-byte copy in the ps2ips pairing),
  and `ps2ip_getconfig()` reports garbage as the interface configuration. Note the
  menu pairing is *not* affected: the menu runs ps2sdk's prebuilt `ps2ip-nm.irx`
  (lwipopts.h:76-79), which implements these; this defect is specific to pairing
  consumers with the in-tree stack, which is exactly the in-game configuration
  (top-level Makefile builds SMSTCPIP only as the in-game driver).
- **Fix:** Decide the contract one way or the other. LESS-NOT-MORE option: replace the
  single shared stub at these slots with tiny per-slot stubs that return a defined
  error (`-1`/`ERR_IF` as each signature allows), so callers fail loudly instead of
  reading garbage. Full option (additive, needs justification): build SMSTCPIP with
  `FULL_LWIP=1` — the implementations already exist and are locally maintained
  (sockets.c carries recent local fixes inside the FULL_LWIP regions) — and delete
  the stub rows.
- **Justification if additive:** Only the FULL_LWIP option is additive, and only in
  the sense of compiling code already in the tree; it is justified if any in-game
  consumer actually calls `select`/`ioctl`/`getsockopt` on its live path (the
  imports say they were linked for a reason).
- **Benefit:** Eliminates a whole class of silent garbage-result failures across
  SMB2/NFS/HTTP in-game drivers and the ps2ips RPC surface.
- **Effort:** S (error stubs) / M (FULL_LWIP flip + validation)
- **Risk:** Low (error stubs) / Medium (FULL_LWIP flip — newly live code paths,
  incl. the RIPTOPL-067/-080/-100 ps2ips surface becoming reachable against this stack)
- **Confidence:** High on the stub/import/gating facts (READ + grep). Medium
  (INFERRED) on which consumers call these on live paths — call sites sit in other
  round-9 sections (smbman-ra, smb2man, nfsman, httpclient).

### RIPTOPL-471 — Typed-driver pool is never reclaimed: after 8 distinct device paths, later device types mount but are unreachable by name; `InitFS` also resets the count without unregistering
*(Round-9 section candidate BDM-07.)*


- **Category:** 8 (half-finished feature) / 6 (bug)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:196-250` (pool + `fs_ensure_typed_driver`), `:1089` (count reset in `InitFS`); no `DelDrv` for typed drivers anywhere in the file
- **Problem:** Each unique `bd->path` (other than `"mass"`) permanently consumes one of `FS_MAX_TYPED_DRIVERS` (8) iomanX registrations. Nothing ever unregisters one — not `disconnect_bd`, not `InitFS` (which instead zeroes `fs_typed_driver_count`, orphaning the still-registered drivers and letting a re-init register duplicates/overwrite live slots). Once the pool is exhausted, `fs_ensure_typed_driver` silently gives up: the volume still mounts in FatFs, but no iomanX device exists under that path prefix, so EE code addressing the device by its typed name (the entire point of the pool) fails with no diagnostic beyond a debug print compiled out in release. The feature is finished on the connect path and unfinished on every lifecycle path.
- **Fix in words:** Unregister and free a typed driver when its last mounted volume disconnects (reference-count by path in `disconnect_bd`), and in `InitFS` `DelDrv` any still-registered typed names before resetting the count.
- **Justification:** Adds only lifecycle bookkeeping for registrations the code already makes; no new mechanism.
- **Benefit:** Typed-name access survives arbitrary plug/unplug mixes instead of silently dying after the eighth device type.
- **Effort:** M
- **Risk:** Medium — iomanX unregister-while-open semantics need care; gate the free on zero mounted volumes for that path.
- **Confidence:** READ

### RIPTOPL-472 — The SBP-2 layer attaches up to 5 drives, the SCSI layer can only ever publish 2 — drives 3–5 are logged in and warm-up-retried as if their failure were transient — both copies / Category 8 / 5
*(Round-9 section candidate ILINK-03.)*


- **Locations:** `modules/ilink/IEEE1394_bd/src/sbp2_driver.c:18-19` (`MAX_DEVICES 5`, `SBP2Devices[5]`) vs `modules/ilink/IEEE1394_bd/src/scsi.c:92-93` (`NUM_DEVICES 2`, `g_scsi_bd[2]`); assault twins at `sbp2_driver.c:18-19` and `scsi.c:80-81`. Failure path: `scsi_connect` returns -1 when no `g_scsi_bd` slot is free (`scsi.c:391-420` ilink), `initConfigureSBP2Device` maps that to -3 (`sbp2_driver.c:278-280`), and `sbp2AttachUnits` counts it as `missing++` — the retryable kind (`sbp2_driver.c:540-551`).
- **Problem:** The two layers disagree about the driver's capacity, and the disagreement is invisible in operation. A third, fourth or fifth SBP-2 unit on the bus gets a full bring-up — CROM parse, exclusive LOGIN, fetch-agent setup, INQUIRY / TEST UNIT READY / READ CAPACITY warm-up — and then fails at the last step for a compile-time reason that no retry can change. The attach machinery treats it exactly like a transient warm-up failure: it is retried on the 1 s / 2 s / 4 s / 5 s back-off (bounded to 8 retries only while another drive is attached, `sbp2_driver.c:571-580`; otherwise re-tried at every bus reset, indefinitely). Each futile cycle also takes and holds an exclusive login on a drive the system can never use. Nothing in either file, or in `ORIGIN.txt` / the assault README, states that 2 is the intended published limit and 5 is slot headroom — the 5 exists, per the sbp2 comments, to hold *reserved* slots for lost-status logins of the same ≤2 real drives, but the attach loop cannot distinguish "third distinct drive" from "same drive, third slot". READ for the caps and the failure/retry path; how often a 1394 bus here carries >2 SBP-2 drives is INFERRED (rare — which is why this is a half-finished-capacity finding, not a hot bug).
- **Fix:** Make the attach loop stop at the publishable capacity: in `sbp2AttachUnits`, count currently-published plus in-flight `g_scsi_bd` occupants (or export a `scsi_free_slots()`-style count from `scsi.c`) and skip — without LOGIN — units beyond it, instead of warming them up to a guaranteed -3. The smaller alternative is reducing `MAX_DEVICES` to 2, but that deletes the reserved-slot headroom the login-retention design uses, so the skip is the LESS change that preserves intent.
- **Justification (if additive):** The skip reuses the attach loop's existing `continue` path for unusable units (the same shape as the "no management agent" and "bad speed" skips already in the function); no new state is kept.
- **Benefit:** A drive the driver can never serve stops consuming logins, warm-up cycles and transport time, and its absence from BDM becomes a capacity fact instead of an endless "trying again" state.
- **Effort:** S–M
- **Risk:** Medium — the counting must include slots held by reserved (lost-status) logins of real drives, or a re-attaching drive could be skipped in favour of a third drive; the fix has to count published + configuring + login-holding slots, as `sbp2FreeDevNr` already enumerates.
- **Confidence:** High (READ) on the cap mismatch and retry path; Medium (INFERRED) on real-world frequency.

### RIPTOPL-473 — ilink `_start` ignores `GetConsoleIDs` failure and publishes a zero EUI-64 and empty model name / Category 6
*(Round-9 section candidate ILINK-05.)*


- **Locations:** `modules/ilink/iLinkman/src/iLink.c:105-110` (result discarded); the failing function `modules/ilink/iLinkman/src/iLink_internal.c:57-76` (returns -1 when `sceCdRI`/`sceCdRM` fails); consumers `modules/ilink/iLinkman/src/iLink_crom.c:356-357,375-379` (Node Unique ID + Bus Info quadlets built from `ConsoleGUID`/`ConsoleModelName`).
- **Problem:** `_start` memsets `ConsoleModelName` to 0, zeroes `ConsoleGUID`, calls `GetConsoleIDs`, and proceeds regardless of its return value. On a CD-identity read failure the driver therefore loads normally and publishes a Configuration ROM whose node-unique ID is 0x0000000000000000 and whose model text is empty — an EUI-64 that is both invalid and identical on every console that hits the failure, served to any 1394 node that reads the ROM (and returned by the exported `iLinkGetLocalNodeEUI64`). The assault copy cannot fail this way (its `GetConsoleIDs` is the static substitute, `iLink_internal.c:56-64` assault) — this is an ilink-only divergence in failure handling, and `GetConsoleIDs`' own error return + debug messages show failure was an anticipated outcome that the caller never acts on. READ for the discarded result; how `sceCdRI` fails on a healthy console is INFERRED (rare — the value is a robustness/identity defect, not a known field failure).
- **Fix:** In ilink `_start`, treat a `GetConsoleIDs` failure like the other initialisation failures already in the function: log and return `MODULE_NO_RESIDENT_END` before `InitializeConfigurationROM()`.
- **Justification (if additive):** No addition — it routes an existing error return into the failure path the same function already uses twice for semaphore/thread creation.
- **Benefit:** The driver refuses to load instead of advertising a zero, duplicated identity on the bus.
- **Effort:** S
- **Risk:** Low — on any console where the CD identity read succeeds (the normal case, and the only one the README's hardware list exercises) nothing changes.
- **Confidence:** High (READ) on the mechanism; Low–Medium (INFERRED) on trigger frequency.

### RIPTOPL-474 — Both mcemu ports share the single 512-byte page data buffer
*(Round-9 section candidate PADS-06.)*

- **Category:** 2/6 (duplication-by-sharing — correctness hazard)
- **Locations:** `modules/mcemu/mcemu_io.c:31` (`mcds->dbufp = &mcdatabuf[0]` for every port), `modules/mcemu/mcemu_var.c:62` (`u8 mcdatabuf[0x200]` — comment in-tree: "Same buffer for both cards"), `modules/mcemu/mcemu.h:218`
- **Problem:** READ. Unlike the ECC buffer, which is per-port (`mceccbuf[MCEMU_PORTS][0x20]`), the page data buffer is one 512-byte array handed to both `MemoryCard` descriptors. `MceRead`/`MceWrite` stage through `dbufp` with per-card `rdoff/wroff/rpage/wpage` state, so two cards touched in interleaved SIO2 transfers (the multitap/two-slot games mcemu exists for) stage through the same bytes. Whether real interleaving corrupts a page depends on transfer atomicity under the SIO2 lock — INFERRED hazard, not a demonstrated corruption — but the asymmetry with the per-port ECC buffer shows the sharing is an accident of the original design, not a requirement.
- **Fix:** `u8 mcdatabuf[MCEMU_PORTS][0x200]` and index it by port in `mc_configure`. +512 bytes IOP RAM, 2 lines.
- **Justification if additive:** The 512 bytes buy per-port isolation in a 2 MB IOP that already spends far more on far less; the alternative (proving global transfer atomicity forever) costs more certainty than the RAM costs space.
- **Benefit:** Eliminates a whole cross-port contamination class for the price of half a kilobyte.
- **Effort:** S · **Risk:** Low · **Confidence:** High that the sharing exists (READ); Medium that it has bitten in practice (INFERRED)

### RIPTOPL-475 — One global semaphore serializes all FatFs I/O on all volumes, defeating FatFs' own per-volume reentrancy
*(Round-9 section candidate BDM-09.)*


- **Category:** 5 (performance, structural only)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:83-117` (`_fs_lock`/`_fs_unlock`); held across blocking device I/O in every op, e.g. `fs_read`/`fs_write`/`fs_open`/`connect_bd` (which holds it across `f_mount`)
- **Problem:** Every operation on every volume — reads on an internal HDD partition, directory scans on USB, VMC checks on MX4SIO — takes the same `_fs_lock` for the full duration of the underlying device I/O. FatFs itself is built reentrant (`FF_FS_REENTRANT 1`, per-volume mutexes in `fatfs/ffsystem.c`), and the driver's shared state is limited to the mount table and the FIL/DIR pools, yet the lock granularity is "the whole subsystem". A slow device (spinning USB, network-backed block device) stalls unrelated volumes' I/O behind it. This is structural, not a tuning issue: no constant changes it.
- **Fix in words:** Narrow the lock to the shared tables (mount-info lookup/assignment and FIL/DIR pool allocation), and let FatFs' per-volume mutex carry the per-call exclusion it already provides; `connect_bd`/`disconnect_bd` keep the wide lock only around mount-table mutation, not across `f_mount`'s device reads.
- **Justification:** Removes locking rather than adding any; the per-volume mutex it relies on already exists in the build.
- **Benefit:** Independent devices stop head-of-line blocking each other; in-game and UI I/O on different volumes can overlap.
- **Effort:** L
- **Risk:** High if done casually — the FIL/DIR pools and `fs->lfnbuf`/`dirbuf` sharing under `FF_USE_LFN 2` (stack buffers are per-call, safe) need a careful audit first. Filed as a structural observation with a direction, not a quick win.
- **Confidence:** READ (lock scope); INFERRED (magnitude of user-visible stalls)

### RIPTOPL-476 — APA DES password engine (~460 lines + tables) is compiled, linked and executed on every open, but its result can never matter in the shipped build
*(Round-9 section candidate HDDUSB-07.)*

- **Category:** 1 (dead code) / 4 (size)
- **Locations:** `modules/hdd/apa/Makefile:8` (`-DAPA_OSD_VER`); `modules/hdd/apa/src/apa-opt.h:31-38`; `modules/hdd/apa/src/password.c:37-498` vs the stub at `password.c:25-35`; call sites `modules/hdd/apa/src/hdd_fio.c:182,191`
- **Problem:** The shipped Makefile defines `APA_OSD_VER`, under which `APA_ENABLE_PASSWORDS` is *not* defined (`apa-opt.h:31-38`), so `apaPassCmp` is the always-return-0 stub. Yet `password.c`'s full DES implementation (`DESEncryptPassword` and tables, lines 37–498 of 498) is compiled and linked into the IOP module, and `apaEncryptPassword` is still executed on every `apaOpen` via `fioGetInput` (`hdd_fio.c:182,191`) to fill password buffers that the stub comparator then ignores. Pure cost: ~460 lines of IOP code + table bytes, and DES work per open, for a feature the build has switched off.
- **Fix:** Mirror the existing pattern: put the body of `apaEncryptPassword` under `#ifdef APA_ENABLE_PASSWORDS` with a no-op stub otherwise, exactly as `apaPassCmp` already does in the same file. The non-OSD build keeps the full engine.
- **Benefit:** Shrinks the APA IRX and removes pointless per-open crypto work; zero behaviour change in the OSD build.
- **Effort:** S · **Risk:** Low · **Confidence:** High (defines and call sites read).

### RIPTOPL-477 — Identical `smb_ReadCD` + `smb_CloseAll` bodies triplicated across the three protocol implementations
*(Round-9 section candidate IOPCORE-25.)*

- **Category:** 2 duplication
- **Locations:** `smb_ReadCD`: `smb.c:1149-1152`, `smb2.c:1127-1130`, `nfs.c:336-339`; `smb_CloseAll`: see IOPCORE-24
- **Problem:** The three files are alternative builds providing one symbol set (by design), but the two functions that depend on none of the protocol differences are copied verbatim into all three — and have already needed the same fix in three places once (IOPCORE-24 is the proof: the sentinel bug exists in all three copies because the body does). Any future fix to part-offset arithmetic (`lsn * 2048`, `lsn >> 21`) must be made three times.
- **Fix:** Move the two bodies to a small shared compilation unit linked by all three cdvdman variants (the builds already share `device-smb.c` the same way). If the build system makes that awkward, at minimum fix IOPCORE-24 in all three places in one commit and note the triplication at the top of each copy.
- **Justification if additive:** Net deletion (~34 lines become ~17 plus one link reference).
- **Benefit:** One place for FID/part bookkeeping fixes across SMB1, SMB2 and NFS builds.
- **Effort:** M (build wiring across three module variants)
- **Risk:** Medium — touches three build variants; each needs a build check.
- **Confidence:** READ (bodies compared during read; `smb_ReadCD` byte-identical, `smb_CloseAll` byte-identical)

### RIPTOPL-478 — `ioplib.c`/`ioplib.h` are byte-identical vendored copies in mmceman and mx4sio_bd, and their `sio2man_hook.c` twins differ by 3 lines
*(Round-9 section candidate PADS-07.)*

- **Category:** 2 (duplication)
- **Locations:** `modules/mmce/mmceman/src/ioplib.c` ≡ `modules/mx4sio_bd/src/ioplib.c` (md5 `14acaf5b34248d9c9c976580a5175d39`, 101 lines each); `modules/mmce/mmceman/include/ioplib.h` ≡ `modules/mx4sio_bd/src/ioplib.h` (md5 `497aa3554ae65d65d22095670f5fc408`); `modules/mmce/mmceman/src/sio2man_hook.c` (318 lines) vs `modules/mx4sio_bd/src/sio2man_hook.c` (315 lines) — same function list, near-verbatim
- **Problem:** READ + hashed. Two in-tree IOP modules that can be resident in the same boot each carry their own copy of the export-table hook machinery. Any fix to the hook iteration (the class of code that produced the mc1-blocking bug fixed in mmceman's pin) must be made twice and verified twice; the sio2regs.h headers have already drifted (different PCTRL1 macro names for the same registers).
- **Fix:** LESS-NOT-MORE form: do **not** build a shared library. Add a drift guard in the existing style of the tree's other fork tests (a text test asserting the two `ioplib.c` copies stay identical, and naming the 3-line sio2man_hook delta), so the next one-sided edit fails CI instead of shipping. Deleting one copy is wrong while both modules vendor independently-built SDK subtrees.
- **Benefit:** Converts silent divergence into a test failure at near-zero runtime cost.
- **Effort:** S · **Risk:** Low · **Confidence:** High (md5-verified, §B)

### RIPTOPL-479 — `smap_udpbd`'s ministack carries a fully dead ARP cache: `arp_add_entry` has no caller and the RX path never learns
*(Round-9 section candidate UDPFS-05.)*

- Category: 1 (dead code)
- Locations:
  - `modules/network/smap_udpbd/src/ministack.c:9-15` (`arp_table[8]`)
  - `modules/network/smap_udpbd/src/ministack.c:120-162` (`arp_add_entry` — grep-verified: no caller anywhere in `smap_udpbd/`)
  - `modules/network/smap_udpbd/src/ministack.c:164-208` (`handle_rx_arp` replies but never calls `arp_add_entry`)
  - `modules/network/smap_udpbd/src/ministack.c:31-53` (`ip_packet_init` sets the Ethernet destination via `eth_packet_init`, i.e. always broadcast — nothing ever consults the table; there is not even an `arp_lookup` in this copy)
  - `modules/network/smap_udpbd/src/ministack.h:235` (the dead declaration)
- Problem: ~45 lines plus an 80-byte table exist solely to be written by a function nothing calls, in a module whose every frame is broadcast anyway (as the long comment inside `arp_add_entry` itself, ministack.c:139-148, already explains). This is the divergent half of the two-ministack duplication (RIPTOPL-243): the udpfs_ministack copy's ARP cache is live and load-bearing; this one is vestigial.
- Fix: Delete `arp_add_entry`, `arp_table` and the header declaration from the smap_udpbd copy.
- Justification if additive: N/A (deletion).
- Benefit: Removes a misleading "almost-fixed" subsystem (its in-code comment narrates a bug fix for a function that cannot execute) from the module a re-vendor will diff against upstream.
- Effort: S
- Risk: Low (no caller exists; the module is self-contained and exports nothing — its exports.tab is empty by design).
- Confidence: High (repo-wide grep for the symbol in the module, plus full read of its only plausible call sites).

### RIPTOPL-480 — Dead FHI export table and `_start` branch in `udpfs_bd` and `udpfs_ioman` reference symbols that exist nowhere
*(Round-9 section candidate UDPFS-06.)*

- Category: 1 (dead code)
- Locations:
  - `modules/network/udpfs_bd/src/exports.tab:1-12` and `modules/network/udpfs_ioman/src/exports.tab:1-12` (byte-identical files, md5 `9c3ae0a998aac0996b65b7f7f5a87104`: an `#ifdef FEATURE_UDPFS_FHI` table exporting `fhi_size`/`fhi_read`/`fhi_write`)
  - `modules/network/udpfs_bd/src/main.c:12-14,38-44` and `modules/network/udpfs_ioman/src/main.c:12-14,38-44` (byte-identical files, md5 `0ebbaea6004625aa3a319af597927c01`: the matching `#ifdef FEATURE_UDPFS_FHI` branch calling `fhi_init()` and registering `_exp_fhi`)
  - Grep-verified: `fhi_size`, `fhi_read`, `fhi_write`, `fhi_init` are defined **nowhere** in the tree; the real FHI module is self-contained (`udpfs_fhi/src/main.c`, `udpfs_fhi/src/exports.tab` exporting library `udpfsfhi`), and no Makefile defines `FEATURE_UDPFS_FHI` for the bd/ioman builds (their Makefiles define only `FEATURE_UDPFS_BD` / `FEATURE_UDPFS_IOMAN`).
- Problem: Two copies of a variant branch that can never be compiled in these modules, naming an export ABI (`fhi`) the project deliberately abandoned (see `udpfs_fhi/ORIGIN.txt`: the library was renamed `udpfsfhi` precisely so it "never answers for Neutrino's `fhi`"). If anyone ever did define the flag for these builds, the link would fail on four undefined symbols.
- Fix: Delete the `#ifdef FEATURE_UDPFS_FHI` block from both exports.tab files and both main.c files (keep the bd/ioman branches, which are live).
- Justification if additive: N/A (deletion).
- Benefit: Two fewer phantom ABIs in the shared template files every variant copies; the FHI story lives in exactly one module.
- Effort: S
- Risk: Low (guarded by a flag no build sets; deletion is whitespace to the shipped IRXs).
- Confidence: High (grep + both Makefiles + the fhi module's own files all read).

### RIPTOPL-481 — `udprdma_inform` (server mode) and the core/transport teardown pair are dead in the shipped variants
*(Round-9 section candidate UDPFS-10.)*

- Category: 1 (dead code)
- Locations:
  - `modules/network/udpfs_shared/src/udprdma.c:533-538` (`udprdma_inform` — grep-verified: no caller in any of the eleven directories; all shipped variants are discovery *clients*)
  - `modules/network/udpfs_shared/src/udprdma.c:464-477` (`udprdma_destroy`) and `modules/network/udpfs_shared/src/udpfs_core.c:283-290` (`udpfs_core_exit`) — the only caller of either in scope is the ioman deinit path, `modules/network/udpfs_ioman/src/udpfs_ioman.c:164`; the BD and FHI builds compile both functions in and can never reach them (their modules have no deinit/unload path)
- Problem: Server-mode surface and teardown code ride in every IRX although the product only ever runs the client half. `udprdma_destroy`'s own body concedes the ministack cannot unbind ("UDP socket cleanup not implemented"), so even the one reachable path (ioman deinit) frees an event flag and leaks the port slot by design.
- Fix: Delete `udprdma_inform` outright. Keep the teardown pair only if the ioman deinit path is considered live; otherwise delete all three and the `udpfs_core_exit` declaration in `udpfs_core.h`.
- Justification if additive: N/A (deletion).
- Benefit: Smaller IOP footprint in all three variants (IOP RAM is ~2 MB and every resident IRX counts); removes an uncallable API that a reader must otherwise reason about.
- Effort: S
- Risk: Low for `udprdma_inform`; Medium for the teardown pair (only if some out-of-scope EE path unloads udpfs_ioman — none found in scope).
- Confidence: High for `udprdma_inform` (grep over the full scope); Medium for the teardown pair (out-of-scope unloaders cannot be excluded from this section alone).

### RIPTOPL-482 — Dead code and build clutter: an unbuilt loopback driver, a DHCP header with no client, a RAW pool with no RAW stack, and nine header declarations with no definitions
*(Round-9 section candidate NETSTACK-12.)*


- **Category:** 1 (dead code) / 7 (build clutter)
- **Locations:**
  - `modules/network/SMSTCPIP/loopif.c` (113 lines) + `include/netif/loopif.h`
    (39) — `loopif.o` is not in the Makefile object list
    (`modules/network/SMSTCPIP/Makefile:11-13`) and `LWIP_HAVE_LOOPIF`
    defaults to 0 and is never enabled (opt.h; grep-verified), so
    `ps2ip.c`'s `AddLoopIF` call compiles its body out too
  - `modules/network/SMSTCPIP/include/lwip/dhcp.h` (212 lines) — a full DHCP
    client API with no `dhcp.c` anywhere in the tree; `LWIP_DHCP 0`
    (lwipopts.h) and `PS2IP_DHCP` is never defined, so the `dhcp_start`/
    `dhcp_stop` branches in `ps2ip.c`'s FULL_LWIP config functions are
    unreachable twice over
  - `modules/network/SMSTCPIP/include/lwip/raw.h` (75) + the
    `MEMP_NUM_RAW_PCB` pool slots reserved in `memp.c:95-118` — `LWIP_RAW`
    is never defined anywhere in the repo (grep-verified), so no raw PCB
    can ever be allocated; the pool bytes are pure waste on a 2 MB IOP
  - Declared but never defined (grep-verified zero `.c` hits for each):
    `tcp_rexmit_seg` (include/lwip/tcp.h), `tcpip_callback`
    (include/lwip/tcpip.h), `memp_realloc` (include/lwip/memp.h),
    `netbuf_alloc`/`netbuf_data`/`netbuf_chain`/`netbuf_next` and
    `netconn_err` (include/lwip/api.h:70,90,115,124,150 region),
    `pbuf_ref_chain`/`pbuf_dechain` (include/lwip/pbuf.h:108,114),
    `sys_now` (include/lwip/sys.h)
  - `modules/network/SMSTCPIP/api_msg.c:665-668` — `do_accept()` is a no-op
    that never ACKs; `API_MSG_ACCEPT` is never posted by any code
    (grep-verified), so the decode-table entry is dead weight
  - `modules/network/SMSTCPIP/udp.c:88-147` (`udp_lookup`) and
    `ip.c:83-118` (`ip_lookup`) — debug-only helpers compiled out unless
    `LWIP_DEBUG`, whose `udp_lookup` returns 1 on *both* the match and
    no-match paths
- **Problem:** ~370 lines of source/header that cannot execute, plus pool
  memory reserved for a compiled-out protocol, invite exactly the
  misreading this stack already suffers from (e.g. NETSTACK-02's stub
  confusion): a reader cannot tell which of loopback/DHCP/RAW/select the
  stack actually has.
- **Fix:** Delete `loopif.c`/`loopif.h`, `dhcp.h`, `raw.h` and the
  raw-pool entry; delete the nine undefined declarations and the dead
  `do_accept` table entry. All deletions, no replacements.
- **Benefit:** Smaller, honest tree; removes the RAW pool's static bytes;
  future readers stop auditing phantom features.
- **Effort:** S
- **Risk:** Low — everything listed is provably unreferenced in-tree; a
  build after deletion is the validation gate.
- **Confidence:** High (READ + grep-verified absence of definitions,
  callers and build references).

### RIPTOPL-483 — Vendored clients carry entry points and whole transports this build never uses (usmb2/unfs4 dead-in-build)
*(Round-9 section candidate NETSMB-13.)*

- **Category:** 1 dead code / 7 build clutter
- **Locations:** `modules/network/smb2man/usmb2/usmb2.c:504-531` (`usmb2_negotiateprotocol`, called only by the also-unused `usmb2_init_context` at `:1034-1085`), `:717-742` (`usmb2_open` — `modules/network/smb2man/ps2iop-compat.h:15` itself notes "smb2man never calls usmb2_open()"), `:744-830` (`usmb2_opendir`/`usmb2_readdir`, additionally gated on `USMB2_FEATURE_OPENDIR`, which the build does not define), `:1002-1032` (`usmb2_size`); `modules/network/nfsman/unfs4/unfs4.c:884-916` (`unfs4_init_context`, unused — nfsman does its own connect+mount); the `USMB2_FEATURE_TINY_TCP` / `UNFS4_FEATURE_TINY_TCP` (Z80) transport blocks throughout both files (e.g. `usmb2.c:39-153`, `unfs4.c:60-190`)
- **Problem:** smb2man drives usmb2 exclusively through `usmb2_create`/`usmb2_close`/`usmb2_prw`/`usmb2_stat`/`usmb2_mkdir`/`usmb2_delete`/`usmb2_rename`/`usmb2_sessionsetup`/`usmb2_treeconnect`/`usmb2_build_request` (grep-verified call census of `smb2man.c`), and nfsman likewise never calls `unfs4_init_context`. Roughly 400 lines of vendored source are compiled-or-gated for transports and front doors this firmware does not have, including a second, unchecked directory-reader (`usmb2_readdir`'s `memcpy(&dh[16 + 4], &usmb2->buf[8], u32)` with a server-supplied `u32`) that invites accidental reuse.
- **Fix:** Delete the unused entry points and TINY_TCP blocks from the vendored copies at the next vendored-sync, recording the deletions in `VENDORED.txt`/`riptopl.patch` like the existing local patches; keep the upstream files' used core untouched.
- **Justification if additive:** Pure deletion.
- **Benefit:** Smaller audit surface in the two security-critical protocol stacks; removes a latent unchecked `memcpy` from reachability-by-accident.
- **Effort:** M (mostly vendored-bookkeeping, not code risk)
- **Risk:** Low for resident size — INFERRED: the build already uses `-ffunction-sections` + `--gc-sections` (`ps2.yaml` smb2man/nfsman entries), so most of this is likely stripped from the IRX today; the win is source/audit size, not RAM.
- **Confidence:** READ for the call census and feature gates; INFERRED for the linker-stripping remark

### RIPTOPL-484 — smb.h declares a function that does not exist and ~200 lines of never-referenced protocol constants
*(Round-9 section candidate IOPCORE-17.)*

- **Category:** 1 dead code / 7 build clutter
- **Locations:** `modules/iopcore/cdvdman/smb.h:501` (`int smb_SessionSetupTreeConnect(char *share_name);` — grep-verified: no definition and no caller anywhere in the repo tree searched), `smb.h:61-117` (all TRANS2 request/response offsets), `smb.h:125-145` (SMB_INFO_* / find levels), and the bulk of the `SMB_COM_*` command list at `smb.h:181-247` (only NEGOTIATE, SESSION_SETUP_ANDX, TREE_CONNECT_ANDX, OPEN_ANDX, READ_ANDX, WRITE_ANDX, CLOSE, NONE are used by smb.c/smb2.c/nfs.c)
- **Problem:** The phantom prototype invites a link error for anyone who calls it, and the unused constant blocks make the header read as if Transaction2, find-first and ~60 commands were implemented surface. This header is shared by three alternative protocol implementations, so its stated surface is the contract a maintainer audits against.
- **Fix:** Delete the prototype and the unreferenced define blocks (keep any a Makefile or the menu-side smbinit genuinely consumes — grep before deleting, as was done for the prototype).
- **Justification if additive:** Pure deletion.
- **Benefit:** Header surface matches implemented surface across all three SMB-family builds.
- **Effort:** S
- **Risk:** Low (after the grep check the fix itself prescribes)
- **Confidence:** READ (prototype verified by repo-wide grep; constant blocks verified unused within this scope, EE/menu-side use to be re-grepped before deletion)

### RIPTOPL-485 — smb.h carries 44 protocol-offset defines and ~30 command defines nothing in the module references
*(Round-9 section candidate NETSMB-14.)*

- **Category:** 1 dead code / 7 build clutter
- **Locations:** `modules/network/smbman-ra/smb.h:46-95` (`SMB_OFFSET_*`, `SMB_TRANS2_REQ_OFFSET_*`, `SMB_TRANS2_RSP_OFFSET_*` — 44 defines), `modules/network/smbman-ra/smb.h:155-253` (`SMB_COM_*` block)
- **Problem:** Grep over the module (and the repo's other consumers of this header) finds zero uses of any `SMB_OFFSET_*`/`SMB_TRANS2_*_OFFSET_*` define — the code uses the packed structs, not byte offsets. Likewise sampled `SMB_COM_*` values for commands the client never sends (`SMB_COM_READ_RAW`, `SMB_COM_WRITE_MPX`, `SMB_COM_COPY`, `SMB_COM_MOVE`, `SMB_COM_SEARCH`, `SMB_COM_FIND`, `SMB_COM_OPEN_PRINT_FILE`, `SMB_COM_READ_BULK`, `SMB_COM_NT_TRANSACT`, `SMB_COM_LOCK_BYTE_RANGE`, …) have zero references in `smbman-ra/*.c`. ~90 lines of header imply machinery that does not exist.
- **Fix:** Delete the offset blocks and the unreferenced command defines (keep the ~15 commands actually used). Pure deletion.
- **Benefit:** Header shrinks to the protocol the module actually speaks; less to re-audit every round.
- **Effort:** S
- **Risk:** Low — compile is the check; a missed use fails the build loudly, not silently.
- **Confidence:** READ (grep census)

### RIPTOPL-486 — The assault tree carries five build files its only Makefile never reads (244 lines) — one of them still contains the cdvdman imports the tree exists to remove / Category 7 / 2
*(Round-9 section candidate ILINK-04.)*


- **Locations:** Unused in `modules/bdmassault/ilink_bd_assault/`: `src/iLinkman/src/imports.lst` (74 lines), `src/iLinkman/src/irx_imports.h` (32), `src/iLinkman/src/exports.tab` (30), `src/IEEE1394_bd/src/imports.lst` (73), `src/IEEE1394_bd/src/irx_imports.h` (35). The build that CI runs (`.github/workflows/flavours.yml:372-375`, `make -C modules/bdmassault/ilink_bd_assault`) uses only `Makefile:9-15,30-36`: `exports.o`/`imports.o` come from `src/` (the wrapper `src/exports.tab`, `src/imports.lst`, `src/irx_imports.h`), and the pattern rules compile only `%.c` from the two subtrees. No include path in `Makefile:11` reaches the nested copies.
- **Problem:** These are the standalone two-IRX build files from the ilink tree, carried into a single-IRX build that superseded them, and two of them are actively misleading. (1) `src/iLinkman/src/imports.lst:1-4` still declares the `cdvdman_IMPORTS` block (`I_sceCdRI`, `I_sceCdRM`) — the exact imports `ilink_bd_assault/README.md:15` and `modules/bdmassault/PROVENANCE.md:42-45` say were removed because they crash POPSTARTER. Anyone who builds the nested iLinkman standalone, or copies that imports file into the combined build, silently reintroduces the documented crash. (2) The nested `src/iLinkman/src/exports.tab:1` defines `_funcret(){}` *before* `DECLARE_EXPORT_TABLE`, while the built wrapper `src/exports.tab:30` defines it *after* `END_EXPORT_TABLE` — the placement `README.md:18` credits for correct `iopfixup` export headers; the dead copy preserves the pre-fix arrangement as if it were current. In the ilink tree the corresponding five files are live (separate IRX builds), so this is assault-only residue, not a pair defect.
- **Fix:** Delete the five unused files from `ilink_bd_assault/`. If a standalone-build reference is wanted, the ilink tree already is that reference.
- **Benefit:** −244 lines; removes a POPSTARTER-crash trap and a stale export-layout example from the tree a maintainer is most likely to copy build files from.
- **Effort:** S
- **Risk:** Low — the combined Makefile and CI job provably do not reference them (paths enumerated above); a host `make` in the container described by the README is the verification.
- **Confidence:** High (READ — Makefile object/pattern rules, CI invocation, and both exports.tab placements all inspected).

### RIPTOPL-487 — Dead driver surface kept in both copies: an uncallable deinit, two phantom prototypes, four dead struct fields and a broken macro / Category 1
*(Round-9 section candidate ILINK-07.)*


- **Locations (both copies unless noted):** `DeinitIEEE1394` — defined `modules/ilink/IEEE1394_bd/src/sbp2_driver.c:802-807` (assault `:775-780`), declared `sbp2_disk.h:180`; prototypes with no definition anywhere: `PHTSendResponse` and `iLinkIntrRegister0Handler` at `modules/ilink/iLinkman/src/include/iLink_internal.h:315,322` (assault `:315,322`); `struct SBP2Device` fields `sectorSize` / `maxLBA` / `cache` at `sbp2_disk.h:157-159` and write-only `loginID` at `:144` (assigned `sbp2_driver.c:695` ilink / `:668` assault, never read); `#define PACKET_TCODE (((x) >> 4) & 0xF)` at `iLink_internal.h:92`; self-labelled dead defines `CDB_PAGE_SIZE` / `CDB_PAGE_TABLE_PRESENT` ("Not in use", `sbp2_disk.h:17-18`) and unused event bits `WRITE_REQ_INCOMING` / `READ_REQ_INCOMING` / `ERROR_TIME_OUT` (`sbp2_disk.h:25,26,28` — only `BUS_RESET_COMPLETE` is used).
- **Problem:** A census of the surface that looks load-bearing but is not. `DeinitIEEE1394` can never run: IEEE1394_bd has no export table (it is a standalone IRX whose only entry is `_start`), the assault combined IRX exports only iLinkman's table, and no caller exists in-tree — yet its body (terminate/delete the callback thread) implies a teardown contract that also silently omits the semaphore, event flag, `writeBuffer` and every `trContext`, so even a future caller would get a half-teardown. The two `iLink_internal.h` prototypes declare functions that do not exist in either tree (a caller would link-fail). The four `SBP2Device` fields cost 5 devices × 16 B of resident `.bss` (the `bd->sectorSize` hits in `scsi.c` are the *block_device* field of the same name, a different struct — verified) and `loginID` is stored on every login and read by nobody, not even the logout path that was never implemented. `PACKET_TCODE` is an object-like macro whose expansion references an undeclared `x`; any use would be a compile error, and there is no use.
- **Fix:** Delete the `DeinitIEEE1394` body + prototype, the two phantom prototypes, the four fields + the `loginID` assignment, `PACKET_TCODE`, and the five dead defines, in both copies. If a deinit is ever wanted, it should be written against the current init's full resource list, not revived from this stub.
- **Benefit:** −~30 lines ×2 copies and ~80 B resident `.bss`; the headers stop advertising a teardown and two functions that do not exist.
- **Effort:** S
- **Risk:** Low — all removals are grep-verified unreferenced at the pin (definition, declaration and assignment sites enumerated above are the only occurrences).
- **Confidence:** High (READ + grep-verified).

### RIPTOPL-488 — Dead imports in all four IOP builds in scope (15 stubs with no caller in their own module) / Category 1 / 7
*(Round-9 section candidate ILINK-06.)*


- **Locations:** `modules/ilink/IEEE1394_bd/src/imports.lst`: iLinkman block `:8` (`I_iLinkResetSBus`), `:9` (`I_iLinkSetNodeCapabilities`), `:17` (`I_iLinkTrRead`), `:20` (`I_iLinkSetNodeTrSpeed`); thbase block `:52` (`I_USec2SysClock`), `:53-54` (`I_SetAlarm`, `I_CancelAlarm`); thevent block `:61` (`I_ClearEventFlag`), `:63` (`I_iSetEventFlag`). `modules/ilink/iLinkman/src/imports.lst`: `:16` (`I_QueryIntrContext`), `:66` (`I_PollEventFlag`). `modules/bdmassault/ilink_bd_assault/src/imports.lst`: `:11` (`I_QueryIntrContext`), `:53` (`I_USec2SysClock`), `:60` (`I_iClearEventFlag`). `modules/pccf/imports.lst` (`I_memcpy` in the sysclib block).
- **Problem:** Each listed stub has zero call sites in its own module's translation units (grep-verified this pass over every `.c`/`.inc` in the respective module, including macro-mediated uses: `M_PRINTF`→`printf`, `CreateMutex`, and the event-flag family were each checked by name so macro-hidden uses are not counted as dead — e.g. sbp2's `PollEventFlag` use is in the *IEEE1394_bd* module, whose own list keeps it; the dead one is iLinkman's separate copy). The pattern is copy-carry: the IEEE1394_bd list inherited iLinkman's alarm/event entries although alarms live only in iLinkman's `iLink_TxRx.c`, and it imports four iLinkman functions the SBP-2 layer never calls (it uses `iLinkReadCROM`, not `iLinkTrRead`). IRX import stubs occupy resident table space in every embedded copy and falsely document the module's real dependencies — in the IEEE1394_bd case, they hide which iLinkman entry points the disk layer actually needs.
- **Fix:** Delete the 15 listed `imports.lst` lines (9 + 2 + 3 + 1 per build above).
- **Benefit:** Smaller import tables in both ilink IRXs, the combined assault IRX and both pccf IRXs; each module's declared dependency surface becomes the true one.
- **Effort:** S
- **Risk:** Low — every entry was verified caller-free within its own module; a link of each IRX is the check (an import needed only by a *different* module is that module's own list's business, as the existing split already assumes).
- **Confidence:** High (READ + grep-verified, with the macro-mediation checks stated above).

### RIPTOPL-489 — 407-line packed IDENTIFY-device structure maintained to read exactly one field
*(Round-9 section candidate HDDUSB-13.)*

- **Category:** 3 (overengineering)
- **Locations:** `modules/hdd/xhdd/ata_identify.h` (407 lines); sole consumer `modules/hdd/xhdd/xhdd.c:17` (one 512-byte static instance), `modules/hdd/xhdd/xhdd.c:79` (only `UltraDMASupport` is ever read)
- **Problem:** xhdd carries a full bitfield-by-bitfield model of the ATA IDENTIFY block — hundreds of named fields across 407 lines — and in the whole module exactly one field is consumed. The sibling atad driver reads the same data as raw u16 words (`ata_param[ATA_ID_…]`) with a handful of index constants. Every future edit risks touching a packed-struct definition that exists for one word at offset 88.
- **Fix:** Delete `ata_identify.h`; read word 88 of the identify buffer directly in `xhdd.c`, as atad does. Net ≈ −400 lines.
- **Benefit:** Largest single-file deletion available in these trees with zero behaviour change.
- **Effort:** S–M · **Risk:** Low (one field, one call site) · **Confidence:** High (field-usage grep verified: only `UltraDMASupport` is referenced).

### RIPTOPL-490 — Static FIL/DIR pools are sized far beyond anything this driver can be asked to serve (128 FIL + 16 DIR in IOP BSS)
*(Round-9 section candidate BDM-10.)*


- **Category:** 4 (size/memory)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/fs_driver.c:305-309`
- **Problem:** `fil_structures[128]` and `dir_structures[16]` are permanent IOP BSS. With exFAT enabled and tiny-buffer mode, each `FIL`/`DIR` carries a full `FFOBJID` (including the exFAT containing-directory fields) — on the order of 80–100 bytes each, so the pools cost roughly 12–15 KB of the ~2 MB IOP forever, to serve an iomanX filesystem whose realistic concurrency is the EE loader's handful of open files plus in-game use through entirely separate drivers. Nothing in the tree opens files through this driver at anywhere near 128-wide concurrency; the mount table itself caps volumes at 10. The number looks inherited, not chosen.
- **Fix in words:** Size the pools to a justified bound (e.g. 32 files / 8 dirs, still multiples of the 10-volume cap) and let `fs_find_free_*`'s existing `-EMFILE` path do its job.
- **Benefit:** Reclaims on the order of 9–11 KB of IOP BSS permanently, in the subsystem (BDM) whose cache sizing already proved IOP headroom is the binding constraint (see `libbdm` cache cap, `bd_cache.c` `CACHED_DEVICES_MAX` comment).
- **Effort:** S
- **Risk:** Medium — an unnoticed consumer opening >32 files through `mass:`/typed drivers would newly hit `-EMFILE`; needs a usage check on the EE side before shrinking.
- **Confidence:** INFERRED (pool-entry size and the concurrency claim are estimated from the structs and in-tree usage, not measured from a built IRX)

### RIPTOPL-491 — pccf keeps two 512-byte static scratch buffers resident forever although their lifetimes never overlap / Category 4
*(Round-9 section candidate ILINK-11.)*


- **Locations:** `modules/pccf/pccf_ata.c:6` (`static unsigned char identify[512]`, used only inside `pccf_ata_attach`, `:128-206` — after attach, only the parsed strings/fields in `pccf_ata_t` are used); `modules/pccf/pccf_ata.c:230` (`static unsigned char verify[512]` inside `pccf_ata_write`, used only for its read-back compare).
- **Problem:** Both buffers live in the resident IRX's `.bss` for the module's whole lifetime, but `identify` is dead storage after the single attach-time IDENTIFY transfer, and `verify` is touched only during writes — which can only happen after a successful attach (`cf_write` requires `ata.ready`, `main.c:54-97`). 512 of the 1,024 bytes is therefore permanently wasted on the IOP, in a module whose other resident costs were already worth auditing (cf. the 8 KiB profile pair, which at least has a live function). READ.
- **Fix:** Overlay them: move `verify` out of `pccf_ata_write` to file scope as a union (or a second file-static pointer assignment) sharing `identify`'s storage, since no call path can be inside attach and a write simultaneously (all requests are serialised by `main.c`'s `io_lock`, and attach precedes `bdm_connect_bd`).
- **Benefit:** −512 B resident `.bss` in both pccf IRX variants.
- **Effort:** S
- **Risk:** Low — the non-overlap is structural (attach-once at `_start` vs. post-connect writes, under one semaphore); host tests exercise both paths.
- **Confidence:** High (READ — use sites and lifetime ordering are all in the two files).

### RIPTOPL-492 — mx4sio's DMA command struct reserves a 1 KiB CRC table (`crc[512]`, marked FIXME in-tree)
*(Round-9 section candidate PADS-09.)*

- **Category:** 4 (size/memory — IOP)
- **Locations:** `modules/mx4sio_bd/src/mx4sio.h:46` (`uint16_t crc[512]; /*FIXME*/`, inside `dma_command_t`, compiled under `CONFIG_USE_CRC16`)
- **Problem:** READ. A single static command struct carries 512 × u16 = 1,024 bytes of CRC storage — one entry per sector of a maximum-size transfer — whether the transfer is 1 sector or 512. On a ~2 MB IOP this is the largest single discretionary allocation in the module. Whether `CONFIG_USE_CRC16` is enabled in RiptOPL's build decides if the bytes are live or merely source weight (INFERRED: config header read, enablement depends on build defines).
- **Fix:** Cap the table at the driver's real per-DMA chunk (the read path processes at most 16-sector groups elsewhere in this family) and stream/verify per chunk, or document the 512-sector single-DMA requirement next to the FIXME if it is real. Do not shrink blind: the fix is "size the table to the chunking the driver actually does," verified against `spisd_read_multi_do`.
- **Benefit:** Up to ~1 KiB IOP RAM back, and a FIXME replaced by a stated invariant.
- **Effort:** M · **Risk:** Medium (touches the CRC-verified read path; needs hardware re-test) · **Confidence:** Medium

### RIPTOPL-493 — `hdd_blkio.o` is an empty translation unit in the shipped build but is still compiled and linked
*(Round-9 section candidate HDDUSB-08.)*

- **Category:** 7 (build clutter) / 1 (dead code)
- **Locations:** `modules/hdd/apa/Makefile:11` (`IOP_OBJS` includes `hdd_blkio.o`); `modules/hdd/apa/src/hdd_blkio.c:17,178,295,298,308` (all content under `#ifdef APA_USE_BDM`, `#ifdef APA_USE_IOMANX`, `#ifndef APA_USE_ATAD`); `modules/hdd/apa/Makefile:8` (defines `APA_USE_ATAD`, defines neither BDM nor IOMANX)
- **Problem:** Every statement in `hdd_blkio.c` (415 lines) sits inside a preprocessor region that is false for the shipped define set, so the object contributes nothing but build time and Makefile noise. (Its one live-looking wart — the `vhdd` bound `slot > BLKIO_MAX_VOLUMES` at `hdd_blkio.c:211`, which should be `>=` — is inside the IOMANX region and therefore not compiled; noted here, not filed separately.)
- **Fix:** Drop `hdd_blkio.o` from `IOP_OBJS` in this Makefile; the file stays in tree for the BDM/IOMANX build variants that use it.
- **Benefit:** One less empty object per build; makes the Makefile state what the module actually is.
- **Effort:** S · **Risk:** Low · **Confidence:** High (define set and guards read directly).

### RIPTOPL-494 — `lz4.h` declares the full LZ4 API (~20 functions: compression, streaming, obsolete); `lz4.c` implements exactly one
*(Round-9 section candidate HDDUSB-14.)*

- **Category:** 7 (build clutter) / 8 (half-finished)
- **Locations:** `modules/isofs/lz4.h` (248 lines — declarations throughout, streaming API at `:158-228`, obsolete API at `:230-241`); `modules/isofs/lz4.c` (ends at line 506; contains only `LZ4_decompress_generic` and `LZ4_decompress_fast`)
- **Problem:** This is a deliberately trimmed vendored copy, but the header was not trimmed with it. Any present or future consumer that includes `lz4.h` and calls e.g. `LZ4_decompress_safe` (declared at `lz4.h:62`) compiles fine and fails only at link time. HDDUSB-01's fix adds the safe wrapper; the rest of the declared API has no implementation and no consumer in tree.
- **Fix:** Trim `lz4.h` to the functions that exist (after HDDUSB-01: `LZ4_decompress_fast` + `LZ4_decompress_safe`), deleting the compression/streaming/obsolete declaration blocks.
- **Benefit:** Header becomes a truthful contract for the vendored copy; link errors become compile-time absences.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

---

### RIPTOPL-495 — usbd public header declares two exports the module does not implement
*(Round-9 section candidate HDDUSB-10.)*

- **Category:** 8 (half-finished feature)
- **Locations:** `modules/usb/usbd-ra/include/usbd.h:294-296` (`sceUsbdGetReportDescriptor`, `sceUsbdMultiIsochronousTransfer`, self-commented "these aren't implemented"); `modules/usb/usbd-ra/src/exports.tab` (exports end at 16)
- **Problem:** The header advertises import stubs 17/18 that can never resolve against this module — any driver written against the header links a stub that fails at load. This is the SDK's header carried verbatim, but for this trimmed `usbd-ra` build the declarations are pure trap.
- **Fix:** Delete the two declarations from this copy's header (LESS); if a future driver needs them, implement and export them first.
- **Benefit:** The header stops promising what the module cannot deliver.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

### RIPTOPL-496 — Multi-LUN support is plumbed but never used: max-LUN is queried, printed, and thrown away; the CBW LUN field is hardcoded 0
*(Round-9 section candidate HDDUSB-11.)*

- **Category:** 8 (half-finished feature) / 1 (dead code)
- **Locations:** `modules/usb/usbmass_bd/src/scsi.c:196-197` (result used only by a debug print); `modules/usb/usbmass_bd/src/usb_mass.c:329-357` (`usb_bulk_get_max_lun`); `modules/usb/usbmass_bd/src/usb_mass.c:482` (`ucmd.cbw.lun = 0`)
- **Problem:** The driver goes to the bus to ask each device its maximum LUN during warmup, then discards the answer; every command block wrapper is built with LUN 0, and the BDM device is registered per physical device. Card readers / multi-LUN devices therefore expose only LUN 0, and the query + callback plumbing is dead weight carried through every startup.
- **Fix (LESS direction):** Delete the `get_max_lun` call, its debug print, and the callback assignment (`usb_mass.c:847`) — or, if multi-LUN is actually wanted, that is a feature project (per-LUN BDM devices), not a fix; do not leave the half-state.
- **Benefit:** Removes a bus transaction and dead plumbing from every device startup; the code stops implying support that does not exist.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

### RIPTOPL-497 — `smap_udpbd` xfer creates an event flag nothing ever waits on, and defines a FIFO-copy helper nothing calls
*(Round-9 section candidate UDPFS-09.)*

- Category: 1 (dead code)
- Locations:
  - `modules/network/smap_udpbd/src/xfer.c:12,53-56` (`tx_done_ev` assigned from `CreateEventFlag` in `xfer_init`; grep-verified: no other reference in the module)
  - `modules/network/smap_udpbd/src/xfer.c:80-93` (`CopyFromFIFO` — grep-verified: no caller; this copy's `HandleRxIntr` hands frames to the ministack, which reads the FIFO itself through registers)
- Problem: A kernel event flag is allocated on every load for no consumer, and a ~14-line DMA/PIO copy helper survives from the upstream layout this module no longer uses. Both are pure residue next to the live `tx_sema` in the same file.
- Fix: Delete `tx_done_ev` (declaration, creation block) and `CopyFromFIFO`.
- Justification if additive: N/A (deletion).
- Benefit: One fewer kernel object per load; less upstream residue to re-diff on re-vendor.
- Effort: S
- Risk: Low.
- Confidence: High (grep over the whole module for both symbols).

### RIPTOPL-498 — `rapull`'s `rp_frame_values(read=0)` mode is dead: the parameter and its branches serve a call that never happens
*(Round-9 section candidate UDPFS-13.)*

- Category: 1 (dead code)
- Locations:
  - `modules/network/rapull/rapull.c:406` (`rp_frame_values(int read)`)
  - `modules/network/rapull/rapull.c:459-460,472-474` (the `!read` branches zeroing chain addresses)
  - `modules/network/rapull/rapull.c:507-511` (the thread loop — the only call site — always passes `1`)
- Problem: The `read` parameter, its guard in the window-read loop (:413) and the two `!read` branches exist for an initialisation frame that no caller ever requests; the snapshot is only ever published from real reads (correctly — the file's header explains why a synthetic snapshot must never be published, which is exactly why the dead mode can never be re-enabled safely).
- Fix: Drop the parameter and the `!read` branches; keep the always-read path byte-identical.
- Justification if additive: N/A (deletion).
- Benefit: Removes a mode whose reactivation would violate the module's own stated safety rule (no fabricated snapshots).
- Effort: S
- Risk: Low.
- Confidence: High (single call site, read directly).

### RIPTOPL-499 — `raudp`'s discovery-elapsed counter `ra_disc_us` is written, overflow-guarded, and never read
*(Round-9 section candidate UDPFS-18.)*

- Category: 1 (dead code)
- Locations:
  - `modules/network/raudp/raudp.c:770-771` ("Discovery elapsed time (retained for bounded diagnostic accounting)")
  - `modules/network/raudp/raudp.c:875,924-925` (the only writes; grep-verified: no read, no packet field, no log uses it — the RA15 header fields are built from other counters at :661-666)
- Problem: A counter maintained across the unbounded discovery loop, with its own saturation guard, feeds no diagnostic that exists. The comment claims a purpose the code does not fulfil.
- Fix: Delete the counter and its two accumulation sites (the discovery cadence is driven by `tries`, not by this value — verified at :863-927).
- Justification if additive: N/A (deletion).
- Benefit: Removes misleading "accounting" from the discovery loop of an in-game module where every resident byte and branch is paid during gameplay.
- Effort: S
- Risk: Low.
- Confidence: High (grep + full read of `ra_discover`).

---

## Round 9 modules — continuation (512–530)

These 19 findings were delivered with this pass as its Appendix A — finished findings in full audit format, unnumbered at delivery only because the RIPTOPL-400…499 block allocated to this pass was full. They are now numbered RIPTOPL-512…530 in the order they appeared in the appendix (not re-ranked against the 400–499 benefit order); the section-candidate-ID map is in the COUNT section.

### RIPTOPL-512 — `udpfs_ministack`'s PS2SDK Makefile omits `ministack_xchg.o`, which its own `main.c` calls — the two build manifests disagree

- Category: 7 (build clutter / divergent build descriptions)
- Locations:
  - `modules/network/udpfs_ministack/Makefile` (`IOP_OBJS = exports.o main.o ministack_arp.o ministack_eth.o ministack_ip.o ministack_udp.o udptty.o imports.o` — no `ministack_xchg.o`)
  - `modules/network/udpfs_ministack/src/main.c:12,69` (`mstack_xchg_init()` declared and called)
  - `ps2.yaml:239` (the ps2build manifest *does* list `ministack_xchg.c` — the only one of the eleven modules whose two manifests differ in source set; bd/fhi/ioman/smap/smap_udpbd source lists match theirs)
- Problem: The module is described twice. The manifest that ships (ps2.yaml) includes the `mstack:` exchange device; the PS2SDK Makefile kept beside the sources for re-vendor parity does not, so a Makefile build of this directory fails at link with an undefined `mstack_xchg_init` — or worse, invites a future maintainer to "fix" it by deleting the call and silently dropping the RA menu link from Makefile-built IRXs.
- Fix: Add `ministack_xchg.o` to the Makefile's `IOP_OBJS` (after `ministack_udp.o`), matching ps2.yaml.
- Justification if additive: One object name restoring manifest parity; no code change.
- Benefit: Both build routes produce the same module; the re-vendor instructions in ORIGIN.txt stop pointing at a broken recipe.
- Effort: S
- Risk: Low.
- Confidence: High (both manifests and the call site read directly).

### RIPTOPL-513 — HDPro 48-bit LBA path drops the upper LBA bytes for LCYL/HCYL
**Verification note (2026-11-14): WITHDRAWN — REFUTED on adversarial re-verification: the function's `lba` parameter is 32-bit (`hdpro_atad.c:678`; shared prototype `atad.h:55`), so the cited LCYL/HCYL composition is correct and the prescribed fix (copying `atad.c:681-682`) is an identity transformation — the claimed defect is absent; see Sable-VERIFY-RiptOPL-CAT6.md. Do not act on this finding.**

- **Category:** 6 bugs / 2 duplication (divergence from sibling)
- **Locations:** `modules/iopcore/cdvdman/hdpro_atad.c:695-697` vs `modules/iopcore/cdvdman/atad.c:680-682`
- **Problem:** In `sceAtaDmaTransfer`'s LBA48 branch, hdpro_atad composes `sector` the same two-byte way as atad.c, but computes `lcyl = (lba >> 8) & 0xff` and `hcyl = (lba >> 16) & 0xff` — the 28-bit forms. atad.c composes `lcyl = ((lba >> 24) & 0xff00) | ((lba >> 8) & 0xff)` and `hcyl = ((lba >> 32) & 0xff00) | ((lba >> 16) & 0xff)`, whose high bytes `sceAtaExecCmd` writes on the first register pass for EXT commands. In hdpro the first-pass LCYL/HCYL bytes are therefore always 0: LBA bits 32-47 are silently dropped and sectors at or above 2^32 (2 TiB at 512 B) alias onto low sectors. Practical exposure on PS2-sized HDDs is nil today, which is exactly why the divergence has survived — it is a latent wrong-sector write path, not a current data bug.
- **Fix:** Copy atad.c's two composition lines into the HDPro LBA48 branch.
- **Justification if additive:** None — two lines aligned to the sibling.
- **Benefit:** Removes a silent aliasing divergence between the two ATAD variants before any >2 TiB use.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ (the divergence); INFERRED (no current hardware reaches the affected range)

### RIPTOPL-514 — CDVDFSV reports CDVDMAN version 0x223 while CDVDMAN reports 0x225

- **Category:** 7 build clutter / 6 bugs (version skew)
- **Locations:** `modules/iopcore/cdvdfsv/cdvdfsv.c:184-185` (`r->cdvdfsv_ver = 0x223; r->cdvdman_ver = 0x223;`) vs `modules/iopcore/cdvdman/cdvdman.c:125` (`CDVDMAN_MODULE_VERSION 0x225`, returned via `CDSC_GET_VERSION` at cdvdman.c:891)
- **Problem:** The CDINIT RPC answer — the version pair EE-side libcdvd and diagnostics see at init — hardcodes both versions and has drifted two revisions behind the module's own version constant. Anything that gates behaviour or bug reports on the init-reported CDVDMAN version is reading a stale number.
- **Fix:** Report `sceCdSC(CDSC_GET_VERSION, …)` for `cdvdman_ver` (the RPC thread already calls `sceCdSC`), or define the pair once in `common/` next to `CDVDMAN_MODULE_VERSION`.
- **Justification if additive:** None — replaces a literal with the existing source of truth.
- **Benefit:** Init-reported versions stop lying about which cdvdman is resident.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### RIPTOPL-515 — prepare_path's trailing-slash scan is dead code that starts on the NUL terminator

- **Category:** 1 dead code (with a latent 6 if "fixed" naively)
- **Locations:** `modules/network/smbman-ra/smb_fio.c:297-330` (`p2` scan at `:312-314`; `strncpy` at `:303`)
- **Problem:** `p2 = &path[strlen(path)]` points at the terminating NUL, so `while ((*p2 == '\\') || (*p2 == '/')) p2--;` never executes — the trailing-separator stripping the comment promises does not happen, and the copy loop's `p <= p2` bound instead copies the NUL itself into `full_path` and appends the backslash after it. The apparatus costs a scan setup per path operation and misleads maintainers; worse, the obvious one-character "fix" (`strlen(path) - 1`) underflows to `path[-1]` for empty/all-slash paths.
- **Fix:** Delete the `p2` scan and bound the copy loop by the string end directly (`*p != '\0'`), which is what the code already effectively does. Pure deletion/simplification.
- **Benefit:** Removes dead logic from the path builder every SMB1 operation passes through, and removes the underflow trap for the next editor.
- **Effort:** S
- **Risk:** Low — behaviour is unchanged because the scan never ran.
- **Confidence:** READ

### RIPTOPL-516 — TCP initial sequence numbers are trivially predictable and IP IDs are a plain counter (inherited)


- **Category:** 6 (security, inherited vintage lwIP — not a local divergence)
- **Locations:**
  - `modules/network/SMSTCPIP/tcp.c` (`tcp_next_iss`, ~line 1075):
    `static u32_t iss = 6510; iss += tcp_ticks; return iss;` — `tcp_ticks`
    advances twice a second, so ISS values are predictable to within a few
    units by anyone who can observe or provoke one connection
  - `modules/network/SMSTCPIP/ip.c` (`ip_output_if`): `static u16_t ip_id`
    incremented by 1 per packet
- **Problem:** Predictable ISS enables blind TCP spoofing/reset injection
  against OPL's SMB/NFS sessions from another LAN host; sequential IP IDs
  make fragment reassembly (NETSTACK-09's surface) easier to poison. Both
  are stock behaviour for this lwIP vintage — the tree neither introduced
  nor aggravated them.
- **Fix:** None proposed under LESS-NOT-MORE: a real fix is additive (an
  entropy source and ISS randomisation) and the threat model is a trusted
  home LAN. Filed so the parent can accept-and-close knowingly rather than
  discover it later.
- **Benefit:** Record completeness only.
- **Effort:** — (no fix proposed)
- **Risk:** — 
- **Confidence:** High (READ).

---

### RIPTOPL-517 — `ff.c` carries a local debug divergence that the vendoring record does not list


- **Category:** 7 (build/repo clutter — vendored-diff hygiene)
- **Locations:** `modules/bdm/bdmfs_fatfs/fatfs/ff.c:26` (`#include "module_debug.h"`), `modules/bdm/bdmfs_fatfs/fatfs/ff.c:3521-3523` (debug hook inside `mount_volume`)
- **Problem:** `modules/bdm/bdmfs_fatfs/ORIGIN.txt` lists five local changes to the FatFs tree and states the sources "keep their upstream formatting so they diff cleanly against the pins", but this sixth change is not in the list: `ff.c` includes the driver's `module_debug.h` and `mount_volume` contains an unconditional `u64 winsect = fs->winsect;` plus `DEBUG_U64_2XU32`/`M_DEBUG` calls. The variable is dead in non-DEBUG builds (assigned, used only by the compiled-out debug macros), and the divergence means a future re-vendor diff will show an unexplained hunk in the middle of upstream's most security-sensitive function. Either the change or the record is wrong; the record is what the next maintainer will trust.
- **Fix in words:** Add the hook to ORIGIN.txt's local-changes list (preferred — the debug output is presumably wanted), or drop the three lines. Do not leave code and record disagreeing.
- **Benefit:** Re-vendoring against the next FatFs pin stays a mechanical diff instead of an archaeology exercise.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ

### RIPTOPL-518 — Dead definitions: `mount_status` field never read or written; unused GPT GUID and attribute macros


- **Category:** 1 (dead code)
- **Locations:** `modules/bdm/bdmfs_fatfs/src/include/fs_driver.h:11` (`int mount_status`); `modules/bdm/src/include/gpt_types.h:44` (`MS_BASIC_DATA_PARTITION_GUID`), `:56` (`GPT_PART_ATTR_PLATFORM_REQUIRED`), `:58` (`GPT_PART_ATTR_LEGACY_BOOTABLE`), `:60-62` (`GPT_PART_ATTR_TYPE_SHIFT`/`MASK`/`SPECIFIC`), `:65-68` (`GPT_BDP_ATTR_*`)
- **Problem:** `mount_status` is declared in every mount-info entry and never touched anywhere (grep-verified across the tree) — the mount state it implies is actually carried by `mounted_bd != NULL`. In `gpt_types.h`, of the four GUID constants only `NULL_GUID`, `MS_RESERVED_PARTITION_GUID` and `EFI_SYSTEM_PARTITION` are used; `MS_BASIC_DATA_PARTITION_GUID` (the one GUID a data-partition filter would want) is defined and unused, as are all the attribute macros except `GPT_PART_ATTR_IGNORE` (`part_driver_gpt.c:153`). The header also carries a TODO (`part_driver_gpt.c`, "Check type specific partition flags: read-only, hidden, etc.") that these macros were presumably staged for and never wired to — including `GPT_BDP_ATTR_READ_ONLY`, which matters: a read-only GPT partition is currently mounted fully writable (cross-ref RIPTOPL-091's read-only-path theme; the read-only *attribute* itself is unimplemented, not just unenforced).
- **Fix in words:** Delete `mount_status` and the unused macros/GUID, or wire `GPT_BDP_ATTR_READ_ONLY` + `GPT_PART_ATTR_PLATFORM_REQUIRED` into the mount decision if read-only partitions are meant to be honoured. Deletion is the LESS default; keep this finding as the record that the read-only attribute is unimplemented either way.
- **Benefit:** Removes a fake state field and a header that promises partition-attribute handling the driver does not do.
- **Effort:** S
- **Risk:** Low for deletion (all grep-verified unused in-tree; the header is internal to `modules/bdm/src`).
- **Confidence:** READ

### RIPTOPL-519 — `part_read` and `part_write` are near-identical copies (bounds check + debug block + forwarding)


- **Category:** 2 (duplication)
- **Locations:** `modules/bdm/src/part_driver.c:46-90`
- **Problem:** The two functions differ only in the final forwarding call; the NULL checks, the RiptOPL partition-bounds check, and the DEBUG block are copied verbatim. This is exactly the shape that produced the already-filed bd_defrag duplication (RIPTOPL-249, cross-ref — that finding covers `libbdm/src/bd_defrag.c` only, not this pair): a future bounds fix applied to one twin and not the other re-opens a partition-escape on one direction. The bounds check is the security-relevant part of both functions, which is what makes the duplication worth more than its line count.
- **Fix in words:** Factor the shared prologue (priv/bounds validation returning the translated sector or an error) into one small static helper both wrappers call. Net lines decrease.
- **Benefit:** One place to maintain the partition-bounds invariant for both directions.
- **Effort:** S
- **Risk:** Low.
- **Confidence:** READ

### RIPTOPL-520 — FatFs is configured for OEM code page 869 (Greek 2), so non-ASCII short-name handling runs through Greek tables on a US user's disks


- **Category:** 3 (overengineering / misconfiguration)
- **Locations:** `modules/bdm/bdmfs_fatfs/fatfs/include/ffconf.h:87` (`FF_CODE_PAGE 869`)
- **Problem:** With `FF_LFN_UNICODE 0`, every SFN up-casing, OEM↔Unicode conversion of 8.3 names, and LFN-to-SFN fallback conversion goes through the CP869 tables (`ff.c` `ExCvt` = `TBL_CT869`; `ffunicode.c` `uc869`, the only SBCS table compiled in). CP869 is Greek 2; the disks this driver mounts in practice are written by Windows PCs in CP437/CP850 locales. Bytes ≥ 0x80 in short filenames (accented European names, symbols) therefore case-fold and convert by Greek rules — names can fail to match, display wrong, or generate different SFNs than the PC that wrote the disk expects. The value appears inherited from the upstream fork's default rather than chosen (ORIGIN.txt lists only `FF_USE_EXPAND` as a deliberate ffconf change). This is a compatibility judgement call, not a clear bug: changing the code page changes how existing on-disk SFNs decode, which is precisely why it should be a recorded decision instead of an inherited accident.
- **Fix in words:** Decide deliberately: if Western-locale disks are the target, set `FF_CODE_PAGE 437` (the FatFs default) at the next re-vendor and note it in ORIGIN.txt; if 869 is intentional, record why in ORIGIN.txt and close this finding.
- **Benefit:** Non-ASCII filename behaviour becomes a chosen, documented property instead of an inherited surprise.
- **Effort:** S
- **Risk:** Medium — any code-page change re-decodes existing SFN-only names on users' disks differently; needs the user's call, not a silent swap.
- **Confidence:** READ (the configuration and which tables compile); INFERRED (that 869 is inherited rather than deliberate — ORIGIN.txt is silent about it)

---

### RIPTOPL-521 — `xhdd.h` declares two functions that exist nowhere in the HDD modules

- **Category:** 1 (dead code)
- **Locations:** `modules/hdd/xhdd/xhdd.h:2,4` (`ata_device_set_write_cache`, `hdproata_device_set_write_cache`)
- **Problem:** Grep over `modules/hdd/` (and the wider modules tree in scope) finds no definition and no caller for either declaration. A definition of `ata_device_set_write_cache` exists only in `modules/iopcore/cdvdman/hdpro_atad.c` — a different module outside this scope — so within xhdd these are dangling declarations that suggest a write-cache control the module does not have.
- **Fix:** Delete the two declarations.
- **Benefit:** Header matches the module's real surface.
- **Effort:** S · **Risk:** Low · **Confidence:** High.

### RIPTOPL-522 — pccf carries a `time_us` clock plumbing chain that is written once and read never / Category 1


- **Locations:** `modules/pccf/pccf_ata.h:17` (field `unsigned int (*time_us)(void)` in `pccf_ata_t`); definition `modules/pccf/pccf_hw.c:194-200` (`time_us()`); sole assignment `modules/pccf/pccf_hw.c:294` (`ata->time_us = time_us;`).
- **Problem:** The field is populated on every successful `pccf_hw_start` and consumed nowhere — no read of `ata->time_us` or call through it exists in `modules/pccf/` or in the host tests that include these sources (grep-verified this pass). It is the remnant of a timing hook the ATA layer never adopted (all ATA waits are the bounded millisecond polls in `pccf_ata.c:23-46`). Unlike the neighbouring counters (`commands`, `sector_reads`), which at least gate trace output, this one has no consumer of any kind, and it keeps a `GetSystemTime`/`SysClock2USec` call chain alive in the resident driver for nothing.
- **Fix:** Delete the field, the `time_us()` function and the assignment. (The thbase `I_GetSystemTime`/`I_SysClock2USec` imports stay — `pccf_hw.c`'s profile code path and `sbp2`-style elapsed helpers are not in this module; in pccf they are used only by the deleted function, so the two import lines can go too if a link confirms no other user — at the pin there is none in `modules/pccf/*.c` outside `time_us`.)
- **Benefit:** −~10 lines and one dead function pointer per `pccf_ata_t`; the ops/session struct documents only hooks that exist.
- **Effort:** S
- **Risk:** Low — no reader exists to break; host tests (`tests/pccf/`) compile these sources and would catch a missed consumer.
- **Confidence:** High (READ + grep-verified).

### RIPTOPL-523 — `pcdiag_ata.h` declares a diagnostic entry point, `pcdiag_ata_observe`, that was never implemented — and an unused read-limit constant beside it / Category 1 / 8


- **Locations:** `modules/pccf/pcdiag_ata.h:20-21` (prototype + a three-line contract comment describing return values for "IDENTIFY and LBA0 read"); `modules/pccf/pcdiag_ata.h:6` (`PCDIAG_ATA_READ_LIMIT 6U`).
- **Problem:** No definition of `pcdiag_ata_observe` and no caller of it exists anywhere in the repository (grep-verified, tests included), and `PCDIAG_ATA_READ_LIMIT` is referenced by nothing. The work the comment describes is actually done by `pccf_ata_attach` (`pccf_ata.c:128-206`, declared in `pccf_ata.h`), so the header preserves the *diagnostic-phase* API as if it were still a stage of the driver: a future caller would link-fail, and a reader auditing the module's surface counts a phantom stage. This is the header residue of the DIAG5 → production port described in `pccf_ata.c:1-3`.
- **Fix:** Delete the prototype, its contract comment and `PCDIAG_ATA_READ_LIMIT` from `pcdiag_ata.h`. The live declarations in the same header (`pcdiag_ata_ops_t`, the poll limit) stay.
- **Benefit:** The pccf header surface becomes exactly the implemented driver; −5 lines.
- **Effort:** S
- **Risk:** Low — no definition or caller exists to orphan.
- **Confidence:** High (READ + repo-wide grep).

### RIPTOPL-524 — `modules/pccf/format.inc` is a 57-line mini-printf included by nothing / Category 1 / 7


- **Locations:** `modules/pccf/format.inc:1-57` (whole file: `putc_b`, `number`, `format_line`); includer census: repo-wide grep for `format.inc` and for `format_line` returns only the file itself. Build touch-point: `modules/pccf/Makefile:18` (`$(IOP_OBJS): $(wildcard *.h *.inc) Makefile`) makes every object depend on it.
- **Problem:** The file's internal logic is bounded and correct (it was read in full; R5's parent pass likewise found the mini-printf itself clean) — but no translation unit in the production build, the `ps2.yaml` build (`ps2.yaml:490-508` lists the five `.c` files), or the host tests ever includes it, because production tracing is the no-op `pccardTrace` stub (`main.c:19`) and the bounded formatter was never wired to it. It survives as an object-dependency of every pccf object while contributing no code to either IRX: pure build clutter whose presence implies a logging path that does not exist.
- **Fix:** Delete `format.inc`. (If formatted logging is ever wanted, the file is in git history and in the upstream PS2CF import this module was adapted from.)
- **Benefit:** −57 lines and one wildcard dependency; the module's file list matches the code that ships.
- **Effort:** S
- **Risk:** Low — zero includers is a mechanical, re-checkable fact at the pin.
- **Confidence:** High (READ + repo-wide grep).

### RIPTOPL-525 — `PROVENANCE.md`'s dedupe claim names a binary that does not exist, and its re-vendor count is stale / Category 7


- **Locations:** `modules/bdmassault/PROVENANCE.md:24-25` ("`usbd.irx.mx4sio` and `usbd.irx.ilink` are byte-identical to `usbd.irx.usbexfat`"); re-vendor instruction `:110-111` ("add the mx4sio usbd as an **eighth** file"). Directory census at the pin: 9 `.irx.*` files — `usbd.irx.{ata, ilink, mmce, usbexfat}` and `usbhdfsd.irx.{ata, ilink, mmce, mx4sio, usbexfat}`; there is no `usbd.irx.mx4sio`.
- **Problem:** The hashes themselves are impeccable — all 9 sha256 values in the provenance table were recomputed this pass and match the shipped bytes, and the ilink half of the dedupe claim is true (md5 `a77650a0…` for both `usbd.irx.ilink` and `usbd.irx.usbexfat`). But the sentence also asserts a tenth file that was never vendored, and the re-vendor note counts from a file set ("eighth file") that no longer matches the directory (9 files, so a new one would be tenth). A maintainer following the re-vendor steps would either hunt for a missing file or "correct" the wrong thing. READ (directory listing + hash recomputation).
- **Fix:** Delete "`usbd.irx.mx4sio` and" from the dedupe sentence (leaving the verified ilink claim), and change the re-vendor note to "a tenth file".
- **Benefit:** The provenance document — the file whose entire job is byte-level trust for these binaries — stops asserting a file that isn't there.
- **Effort:** S
- **Risk:** Low — text only; the verified table is untouched.
- **Confidence:** High (READ + recomputed hashes).

---

### RIPTOPL-526 — `modules/debug/drvtif.irx` and `drvtif-ingame.irx` are the same bytes under two names

- **Category:** 7 (build clutter) / 1 (dead duplicate)
- **Locations:** `modules/debug/drvtif.irx`, `modules/debug/drvtif-ingame.irx` — both 5,985 bytes, md5 `838e4e7adf1a3207b049fdcd0b84d046`
- **Problem:** READ (hashed). The `-ingame` naming promises a distinct in-game variant (and for `tifinet` the promise is kept: the pair differ, md5 `6c52a13f…` vs `f212f485…`, same 3,301 size). For `drvtif` there is no variant — one of the two tracked prebuilts (see already-filed RIPTOPL-032 for the prebuilt class itself) is pure duplication, and any future real in-game drvtif build risks being "updated" in only one of the two slots.
- **Fix:** Delete one copy and point its consumer at the other, or replace both with a build-time copy step. Net −5,985 tracked bytes.
- **Benefit:** One less stale-binary slot in a directory whose entire risk is staleness.
- **Effort:** S · **Risk:** Low (verify the two embed sites by name before deleting) · **Confidence:** High (md5-identical)

### RIPTOPL-527 — bdmevent's `irx_imports.h` imports four libraries the module never calls

- **Category:** 7 (build clutter)
- **Locations:** `modules/bdmevent/irx_imports.h` (includes `iomanX.h`, `stdio.h`, `sysclib.h`, `sysmem.h`) vs `modules/bdmevent/main.c` (88 lines; calls only `bdm_*` and `sceSif*`) and `modules/bdmevent/imports.lst` (correctly lists only bdm + sifcmd)
- **Problem:** READ. The header's "keep alphabetical" boilerplate was copied from the ps2sdk template and never pruned; the `.lst` is right and the header is wrong, so the two sources of import truth disagree in the smallest module in the tree.
- **Fix:** Delete the four unused includes. −4 lines.
- **Benefit:** Trivial, but removes a disagreement a future editor will resolve the wrong way.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ, whole module)

---

### RIPTOPL-528 — `udpfs_fhi/ORIGIN.txt` states there is no write export; the module exports one and implements it

- Category: 7 (documentation/build drift)
- Locations:
  - `modules/network/udpfs_fhi/ORIGIN.txt` ("No write export: cdvdman only reads the disc image")
  - `modules/network/udpfs_fhi/src/exports.tab` (`[6] DECLARE_EXPORT(udpfs_fhi_write)` — the file's own header comment documents slots [4] read, [5] connected, [6] write)
  - `modules/network/udpfs_fhi/src/udpfs_fhi.c:58-74` (full `udpfs_fhi_write` implementation over `udpfs_core_bwrite`, for read-write VMC handles)
- Problem: The provenance file — the document a re-vendor follows to decide what to keep — denies the existence of a live, exported, implemented write path. Acting on it during a re-vendor would silently drop VMC saving over UDPFS.
- Fix: Correct the ORIGIN.txt sentence to match the export table (write exists for VMC; the disc image path is read-only by handle, not by missing export).
- Justification if additive: N/A (one sentence).
- Benefit: The re-vendor checklist stops being a trap for a working feature.
- Effort: S
- Risk: Low.
- Confidence: High (all three files read).

### RIPTOPL-529 — RA-under-POPS is shipped with its two load-bearing unknowns still open (half-finished feature)

- Category: 8 (half-finished feature)
- Locations:
  - `modules/network/rapops/rapops.c:111-112` (`TODO: POPS may bring its own DEV9; whether ours then fails to load or replaces it is not known. SMAP needs either one.`)
  - `modules/network/rapops/rapops.c:124-125` (`TODO: no EE event buffer under POPS yet, so no notices in game` — the raudp argument is built with event address 0 at :128)
  - `modules/network/rapull/rapull.c:179-185` (header TODOs: no out-of-band startup/EE-read diagnostic, EE cache-lag unmeasured, snapshot entries not frame-tied)
  - `modules/network/rapull/rapull.c:268-273` (`rp_valid` TODO: PS1 scratchpad reads as zero; where POPS keeps it is not checked — watch entries in 0x200000–0x2003FF silently report 0)
- Problem: The POPS RA path works only if POPS does not bring its own DEV9 (untested either way, by the code's own admission), can never deliver unlock/reset notices (event buffer hard-wired to 0), and silently reports zeros for scratchpad watch entries. These are documented in-tree, so this is a tracking finding, not a discovery: the feature's limits live in comments, not in any user-facing place.
- Fix: No code change proposed here (LESS NOT MORE): surface the three limits in the RA feature's user documentation and close the DEV9 question with one real-hardware POPS test before the path is advertised.
- Justification if additive: N/A.
- Benefit: Prevents a "RA works under POPS" claim the code itself does not support.
- Effort: S (documentation) / M (hardware test — not verifiable in this VM).
- Risk: Low.
- Confidence: High that the gaps exist (they are the authors' own TODOs, read at the cited lines); no claim is made about POPS's actual DEV9 behaviour.

### RIPTOPL-530 — Small dead items: unused macros, an unreachable duplicate table entry, a never-linked helper

- **Category:** 1 dead code
- **Locations:** `modules/iopcore/patches/f2techioppatch/main.c:18` (`JMP` macro defined, never used — grep-verified in file); `modules/iopcore/cdvdman/atad.c:92` (ata_cmd_table lists `ATA_C_STANDBY_IMMEDIATE` and `ATA_C_FLUSH_CACHE` twice; the linear search in `sceAtaExecCmd` breaks at the first match, so the second pair is unreachable — same type values, hence harmless but dead); `modules/iopcore/imgdrv/imgdrv.c:13-16` (`dummy_fs` defined, referenced only inside a commented-out ops block at :73-81)
- **Problem:** Three unrelated specks of dead weight, each individually too small to file, together a single cleanup commit. The atad duplicate is the only one that can mislead (it suggests STANDBY/FLUSH have two different types; they do not — both entries are type 1).
- **Fix:** Delete all three.
- **Justification if additive:** Pure deletion.
- **Benefit:** Marginal size/clarity; removes the misleading duplicate ATA entries.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

---
---

## Ledger — what was read (per module group; full per-file ledgers in the section files)

| Group (section file) | Files | Lines | Unread |
|---|---:|---:|---|
| modules/bdm/ (`riptopl-r9-modules-bdm.md`) | 27 source | 26,787 | none — ffunicode.c's ~15.6k lines of code-page tables read as data with every non-data line enumerated; only the CP869 table compiles (RIPTOPL-520 / RIPTOPL-012) |
| modules/network SMB/NFS (`…-netsmb.md`) | 60 source + httpstream.inc | 14,675 + 334 | none |
| modules/network stack (`…-netstack.md`) | 63 (all files in tree) | 17,801 | none |
| modules/network UDPFS/SMAP/RA (`…-udpfs.md`) | 149 (all files in 11 dirs) | 14,342 | none |
| modules/iopcore/ (`…-iopcore.md`) | 63 source | 14,171 | none |
| modules/hdd/ + usb/ + isofs/ (`…-hddusb.md`) | 72 (all files) | 14,600 | none |
| modules/ilink/ + bdmassault/ + pccf/ (`…-ilink.md`) | 90 (all files) | 11,934 | none as source — exceptions named: 3 license texts (495 lines) hash/identity-verified as identical AFL texts rather than line-audited; 9 `.irx.*` binaries accounted by size + sha256 (all 9 match PROVENANCE.md's table); assault `iLink_intr/TxRx/DMA/PHY.c` read as complete diffs against the fully-read ilink twins (every differing line inspected; non-differing lines are byte-identical to text read in full) |
| pads/cards/debug group (`…-pads.md`) | 100+ text files | 20,109 + 46 (freepad PROVENANCE.md) | none — 5 debug binaries hashed/sized only (RIPTOPL-526 is one of them); freepad/ contains no source at the pin (its documented freepad.irx is installed by script, not stored in the directory) |

Coordinator census reconciles the scope: 493 source files / 128,148 lines under `modules/` (`find` + `wc`); section ledgers exceed that figure because they also count the Makefiles, `.lst`/`.tab` tables and prose files read alongside the source. Non-source files under modules/ total 192 (52 `.lst`, 21 `.tab`, Makefiles, patches, docs) — read where a section ledger says so, otherwise not line-audited as prose.

## Residue statement (modules/, after round 9)

**Every source file under `modules/` has now been line-read at the pin.** What remains in this tree is not unread source:

1. **Binaries** — the tracked `.irx` payloads (4 in modules/debug/, 9 in modules/bdmassault/, deci2.img): hashed and sized in this and earlier rounds, never disassembled. (The round-9 cross-repo binary pass owns this class.)
2. **Numbering residue: closed.** The 19 findings delivered unnumbered in this pass's Appendix A are now numbered RIPTOPL-512…530 in the continuation section above (renumbering map in the COUNT section); no bookkeeping residue remains.
3. **External-contract questions** that cannot be settled from this tree, recorded inside the findings that carry them: `SPD_CAPS_SMAP`'s value (PS2SDK dev9.h — RIPTOPL-468), the EE-side fillability of ps2sdk `ps2smb.h` fields (RIPTOPL-459), duplicate-load reachability for udpfs_ministack (RIPTOPL-469).
4. Standing audit-wide limits, unchanged: no PS2 build exists in this VM and nothing here is hardware-verified; Category 5 remains structural; live UDP interop is untestable in the sandbox.

A further modules/ reading round on this pin has no unread class to take. Productive continuations are a pin move, the binary pass, or hardware.
