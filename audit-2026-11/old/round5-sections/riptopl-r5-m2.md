# RiptOPL — Round 5, Section R5-M2: modules/network (smbman-ra + smbinit + http/rap + udpfs family)

- **Repo / pin:** NathanNeurotic/RiptOPL @ `2e347d5ac885cd0ff73e9ad4143781946396f492` (verified `git rev-parse HEAD`; `git status --porcelain` empty at start and end — read-only throughout).
- **Brief correction:** the assignment said "25 files, 6,415 lines". The list as given enumerates **29 files**, and `wc -l` at the pin totals **6,402 lines**. All 29 were read 1..EOF.
- **Prior-filing check:** every candidate below was grepped against `Sable-RiptOPL.md`, `Sable-RiptOPL-ROUND2.md`, `Sable-RiptOPL-ROUND3.md`, `Sable-RiptOPL-ROUND4.md` and `round4-sections/*.md` (function name + file path) before filing. Already-filed substance is listed, not repeated.
- No build or hardware claims; performance magnitudes are structural/INFERRED. No secret-like values printed (password handling is described by location and bound only).

## Coverage ledger (all READ 1..EOF)

| File | Lines |
|---|---|
| modules/network/smbman-ra/smb.c | 1640 |
| modules/network/smbman-ra/md4.c | 182 |
| modules/network/smbman-ra/auth.c | 93 |
| modules/network/smbman-ra/poll.c | 91 |
| modules/network/smbman-ra/smbman.c | 35 |
| modules/network/smbinit/smbauth.c | 139 |
| modules/network/smbinit/md4.c | 175 |
| modules/network/smbinit/main.c | 19 |
| modules/network/httpclient/httpclient.c | 402 |
| modules/network/httpclient/main.c | 159 |
| modules/network/rapull/rapull.c | 409 |
| modules/network/rapops/rapops.c | 156 |
| modules/network/nbns/main.c | 76 |
| modules/network/udpfs_shared/src/udprdma.c | 738 |
| modules/network/smap_udpbd/src/udpbd.c | 507 |
| modules/network/smap_udpbd/src/ministack.c | 275 |
| modules/network/udpfs_ministack/src/ministack_arp.c | 211 |
| modules/network/udpfs_ministack/src/ministack_xchg.c | 203 |
| modules/network/udpfs_bd/src/udpfs_bd.c | 193 |
| modules/network/udpfs_fhi/src/udpfs_fhi.c | 143 |
| modules/network/udpfs_ministack/src/ministack_ip.c | 91 |
| modules/network/smap_udpbd/src/udptty.c | 89 |
| modules/network/smap_udpbd/src/main.c | 85 |
| modules/network/udpfs_ministack/src/main.c | 72 |
| modules/network/udpfs_ministack/src/ministack_udp.c | 68 |
| modules/network/udpfs_ministack/src/ministack_eth.c | 28 |
| modules/network/udpfs_ioman/src/main.c | 47 |
| modules/network/udpfs_bd/src/main.c | 47 |
| modules/network/udpfs_fhi/src/main.c | 29 |
| **Total** | **6,402** |

Supporting spot-reads (context only, not ledger files): `modules/network/smbman-ra/smb.h` (server_specs_t layout, MAX_SMB_BUF), round-2/round-3 report texts for RIPTOPL-038/046/056 and cross-repo X-31.

---

## New candidate findings

### R5-M2-01 / GetSMBServerReply receives whatever length the server's session header claims, into a 4,224-byte static buffer
- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:226-266` (`GetSMBServerReply`, esp. `:257-263`); buffer definition `smb.c:37-83`; sizes at `modules/network/smbman-ra/smb.h:894-895` (`MAX_SMB_BUF 4096`, `MAX_SMB_BUF_HDR 128`).
- **Problem:** After the reply's 4-byte NetBIOS session header is read, `totalpkt_size = nb_GetSessionMessageLength()` is a server-supplied 24-bit value (up to 16,777,215). When `rhdrlen == 0` — the path used by negotiate, session setup, tree connect, share enum, query, create, close, delete, rename, find, echo, logoff — that value is passed unclamped to `RecvData(..., (char *)&SMB_buf.smb, size, ...)` whose destination is the 4,224-byte static `SMB_buf`. Nothing compares the claimed length against the buffer, against the server's negotiated `MaxBufferSize`, or against `CLIENT_MAX_BUFFER_SIZE` (65,535, itself 15× the buffer) that this same file advertises to the server at `smb.c:494`. A conforming-but-generous server can overflow it; a hostile or broken one can write megabytes past a static IOP buffer. The sibling in-game stack does the opposite (round 2 cleared cdvdman `smb.c` precisely for its "explicit NBSS length rejection"). Not covered by RIPTOPL-038, which is the FindFirstNext2 filename copy downstream of a received packet.
- **Fix:** Reject (return -2) any session length larger than the receive buffer before the body read, in `GetSMBServerReply` only — one comparison at the single choke point every command shares.
- **Justification (adds a check):** the length is wire data and the destination is fixed-size; there is no smaller fix, and the check deletes no behaviour for conforming servers (replies are capped by the negotiated buffer size in practice).
- **Benefit:** Closes an unbounded server-controlled write that sits under every menu-side SMB1 operation.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M2-02 / smb_ReadAndX trusts the response's DataOffset and DataLength — both overflow, one into the caller's buffer
- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:1042-1094` (esp. `:1077-1089`).
- **Problem:** Two server-controlled fields are used unchecked. (1) `padding = RRsp->DataOffset - sizeof(ReadAndXResponse_t)`; if a response claims a large DataOffset, `RecvData(main_socket, (char *)(RRsp + 1), padding, ...)` writes up to ~65 KB past the response header inside the 4,224-byte `SMB_buf` (and if DataOffset < header size, the negative padding is silently skipped while the stream is now desynchronised). (2) `DataLength` (32-bit assembly of the response's High/Low fields) is passed straight to `RecvData(main_socket, readbuf, DataLength, ...)` — never compared with `nbytes`, the amount the caller actually requested and sized its buffer for. `smb_ReadFile` (`:1096-1120`) loops on the returned count, so an over-long DataLength both overflows the caller's read buffer and corrupts the loop's accounting. The write side of the same file was already hardened this way (`smb.c:1170` rejects a Count greater than what was sent); the read side was not.
- **Fix:** Clamp/reject in `smb_ReadAndX`: fail the read if `DataLength > nbytes`, and if `DataOffset` is less than the header size or the implied padding exceeds the packet buffer.
- **Justification (adds checks):** both values are wire data feeding copy lengths; the write path in the same file already establishes this exact validation pattern, so the fix is a transcription of in-file precedent, not new machinery.
- **Benefit:** Removes a server-controlled overflow of caller-owned game/VMC read buffers on the hot data path.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M2-03 / smb_NetShareEnum walks the response with strlen and server-supplied offsets, none of them bounded
- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:639-722` (esp. `:692-719`).
- **Problem:** The RAP response is parsed entirely on trust: `ParamOffset` indexes `SMB_buf.smb.u16buff[]`/`.s16buff[]` (`:692`, `:696`) and `DataOffset` becomes the base pointer `data` (`:699`), with no check that either lies inside the bytes actually received. `AvailableEntries` is a signed 16-bit server value driving the loop; each iteration calls `strlen(p)` three-plus times on a pointer advanced by `strlen(p) + 1 + padding + 4` (`:705-718`) — a response without a NUL inside the received bytes walks `p` arbitrarily far past the static buffer, reading (and the loop's type test at `:707` dereferences) out of bounds. The share comment is fetched through a second server-supplied u16 offset used as an index from `data` (`:712`) with no bound at all. Any of these is reachable by a broken or hostile share-enumeration reply before a single share is listed.
- **Fix:** Bound the parse by the received length returned from `GetSMBServerReply`: reject ParamOffset/DataOffset outside it, replace the `strlen` walk with a remaining-bytes-bounded scan, and check the comment offset against the remaining data before dereferencing.
- **Justification (adds checks):** every offset and terminator here is wire data; the fix is confined to this one parser and mirrors the bounded-parse pattern already used in `ministack_xchg.c`'s reply handler in the same tree.
- **Benefit:** Share browsing — the first thing a user does against an untrusted NAS — stops being an unbounded read/write primitive.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ)

### R5-M2-04 / smb_QueryPathInformation casts structures at the response's unchecked DataOffset
- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:833` and `:846` (the same pattern recurs at `:1482` in `smb_FindFirstNext2`, whose downstream copy is already RIPTOPL-038).
- **Problem:** `QPIRsp->smbTrans.DataOffset` is a server-supplied u16 used directly as an index into `SMB_buf.smb.u8buff[]` and cast to `BasicFileInfo_t *` / `StandardFileInfo_t *`; eight 64-bit fields are then read out of it. An offset near 65,535 reads ~64 KB past the 4,224-byte static buffer and returns whatever it finds as file times, attributes and — critically — `EndOfFile`, the size the rest of the stack trusts for the legacy-open path (`smb_LegacyOpenAndX`, `:1004-1007`). No check compares the offset (plus the structure size) with the received packet length.
- **Fix:** Reject the reply if `DataOffset + sizeof(the structure being cast)` exceeds the received length, at both cast sites.
- **Justification (adds a check):** the offset is wire data; two comparisons at two sites, no new state.
- **Benefit:** File size/attribute queries against a bad server stop manufacturing sizes from out-of-bounds memory.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M2-05 / Negotiate copies the server's KeyLength (up to 255) into an 8-byte field
- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smbman-ra/smb.c:403`; destination `modules/network/smbman-ra/smb.h:286` (`u8 EncryptionKey[8]`, the last field of `server_specs_t`).
- **Problem:** `memcpy(server_specs.EncryptionKey, NPRsp->ByteField, NPRsp->KeyLength)` — `KeyLength` is a server-supplied byte in the negotiate response, never checked. A value above 8 writes past `server_specs_t` into adjacent static storage during connection setup, before authentication. Round 2's clearance of the sibling stack noted cdvdman `smb.c` has explicit KeyLength checks; this copy does not. Distinct from RIPTOPL-038, which covers the `getStringField` call on the next line (`:404`).
- **Fix:** Reject the negotiate reply (goto negotiate_error) when `KeyLength > sizeof(server_specs.EncryptionKey)`.
- **Justification (adds a check):** one comparison on wire data at the only copy site.
- **Benefit:** Closes a pre-auth, server-controlled static-buffer overrun in the connection handshake.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M2-06 / MD4 and the LM/NTLM auth helpers exist as two divergent copies (smbinit vs smbman-ra)
- **Category:** 2 (duplication)
- **Locations:** `modules/network/smbinit/md4.c` (175) vs `modules/network/smbman-ra/md4.c` (182) — 55 diff lines (whitespace-normalised), load-bearing divergence: smbinit uses a stack `buffer[128]`, smbman-ra a union'd aligned buffer, const differs on `transform`'s parameter; `modules/network/smbinit/smbauth.c` (139) vs `modules/network/smbman-ra/auth.c` (93) — the same `LM_Password_Hash` / `NTLM_Password_Hash` / `LM_Response` trio in both (the 255-char password bound was added to both copies — the fix-propagation cost already paid twice), smbinit adding `GenerateLMHashes`/`SmbInitHashPassword` on top.
- **Problem:** This is RIPTOPL-056's proven failure shape (the two DES copies, one patched one not) repeated one layer down: the hash and response code that SMB1 authentication stands on is maintained in two places, in two modules that must agree byte-for-byte on the wire. Round 3 filed the DES pair; the MD4/auth pair beside it was not filed.
- **Fix:** Share one MD4 + auth source by inclusion between the two modules (the pattern the tree already uses for `httpstream.inc` and `unfs4.c`), keeping each module's own entry points (`SmbInitHashPassword` stays smbinit-only).
- **Benefit:** The next auth fix lands once; the two stacks cannot silently diverge on hash behaviour again.
- **Effort:** M · **Risk:** Med (two build contexts: EE-linked smbinit vs IOP smbman-ra — signatures/types differ slightly and must be reconciled) · **Confidence:** High (READ, diff-measured)

### R5-M2-07 / Two ministack implementations are maintained in parallel: smap_udpbd's monolith vs udpfs_ministack's split files
- **Category:** 2 (duplication)
- **Locations:** `modules/network/smap_udpbd/src/ministack.c` (275) against `modules/network/udpfs_ministack/src/ministack_eth.c` (28) + `ministack_ip.c` (91) + `ministack_udp.c` (68) + `ministack_arp.c` (211): the same `eth_packet_init`/`ip_packet_init`/`udp_packet_init`, `ip_checksum`, `eth/ip_packet_send_ll`, `udp_bind`, `arp_add_entry`, `handle_rx_eth/ipv4/udp/arp`, `ms_ip_set/get_ip` and the compiled-in default `ip_addr`, in two dialects (FIFO-register reads in the monolith, pre-read header callbacks in the split — the RX plumbing genuinely differs; the packet construction, checksum, ARP table logic and send path do not). `parse_ip` is additionally duplicated verbatim at `modules/network/smap_udpbd/src/main.c:383-407` and `modules/network/udpfs_ministack/src/main.c:205-229`.
- **Problem:** The monolith's own comment at `ministack.c:139-148` documents the cost: its `arp_add_entry` carried a slot-test bug the split copy never had, so UDPBD broadcast every frame while UDPFS did not — one fix, one copy, silent divergence. Distinct from RIPTOPL-127 (the three `smap.c` driver copies) and from the round-1/2 UDPFS header findings (the bd/fhi/ioman copies of `udpfs_core.*`, a different duplication axis).
- **Fix:** Extract the shared packet-build/checksum/ARP-table core into one source both IRXs compile (the modules already share `udpfs_shared/` by directory for UDPRDMA), leaving each stack only its RX-delivery dialect; deduplicate `parse_ip` the same way.
- **Benefit:** ARP/IP/UDP construction fixes land once; the documented broadcast-everything class of one-sided bug stops recurring.
- **Effort:** L · **Risk:** Med (two IRXs load in different contexts; the split stack's ARP gained conflict-detection/replacement policy the monolith lacks, which must be preserved, not flattened) · **Confidence:** High (READ, function-level comparison)

### R5-M2-08 / poll.c's invalid-fd tracking is dead: `ifd` is never set, so POLLNVAL can never be returned
- **Category:** 1 (dead code) / 6 (latent correctness)
- **Locations:** `modules/network/smbman-ra/poll.c:20, 26, 37, 73-74`.
- **Problem:** `fd_set ifd` is `FD_ZERO`'d and read in both loops (`FD_ISSET(fd, &ifd)` guards the duplicate-fd skip at `:37` and selects the `POLLNVAL` result at `:73-74`) but no code path ever `FD_SET`s it. The POLLNVAL branch is unreachable, duplicate fds in the array are processed twice instead of being marked invalid, and a caller polling a bad descriptor gets a silent timeout-shaped answer instead of POLLNVAL. The only in-tree caller (`smb.c` `RecvTimeout`) polls one fd, so today's impact is nil — this is filed as dead code carrying a false promise, not as an active bug.
- **Fix:** Delete the `ifd` set and both branches that consult it (the loops simplify to the direct FD_ISSET tests), or — if POLLNVAL semantics are ever wanted — set the bit where the comment intends; deletion is the LESS-NOT-MORE default.
- **Benefit:** ~10 lines of misleading machinery gone; the file stops claiming an error behaviour it does not implement.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M2-09 / smb_Disconnect drains the socket with an unbounded blocking recv loop
- **Category:** 5 (performance/blocking)
- **Locations:** `modules/network/smbman-ra/smb.c:1624-1640` (loop at `:1632-1633`).
- **Problem:** After `shutdown(main_socket, SHUT_WR)`, the code loops `while (recv(main_socket, &dummy, sizeof(dummy), 0) > 0);` on a blocking socket with no timeout, no byte bound and no poll. A server that keeps the connection open and trickles (or simply never closes its side) parks the calling IOP thread inside disconnect forever — the exact lockup class `RecvTimeout`'s long comment at `smb.c:148-155` says this module exists to avoid, applied everywhere except here. The sibling in-game stack's disconnect poll loop sleeps with backoff (round-2 clearance note).
- **Fix:** Bound the drain: poll with the module's existing `RecvTimeout` helper and a small total budget (e.g. a fixed iteration/time cap), then close regardless.
- **Justification (adds a bound):** it reuses the file's own timeout helper; no new mechanism, and the drain's purpose (let sent data land before close) survives a bounded wait.
- **Benefit:** Disconnect/reconnect against a wedged server cannot hang the SMB device thread permanently.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M2-10 / UDPBD takes its block-device geometry from the INFO reply with no sanity check
- **Category:** 6 (bug/security)
- **Locations:** `modules/network/smap_udpbd/src/udpbd.c:307-316` (`_cmd_info_reply`); geometry consumers `:93` (`g_read_size = count * g_udpbd.sectorSize`), `:161-162, 201-202` (bounds arithmetic).
- **Problem:** On the first INFO_REPLY, two raw FIFO words become `g_udpbd.sectorSize` and `g_udpbd.sectorCount` and the device is immediately connected to BDM. Nothing validates them: a sectorSize of 0 makes every read compute `g_read_size = 0` and wait out its alarm as a timeout (a permanently erroring device, not a refused one); a non-512 sectorSize desynchronises the write path, which hardcodes 512-byte RDMA payloads (`:230-239`); an inflated sectorCount advertises capacity the server does not have. The reply is accepted from any LAN sender whose packet carries the current `g_cmdid` — a 3-bit rotating value (`:90, 204`, masked `& 0x7`) that starts at 0, the value the initial INFO exchange uses. This file's RDMA receive path was already hardened against wire-controlled sizes (the comment at `:353-356` says exactly that); the geometry that frames all of it was not.
- **Fix:** In `_cmd_info_reply`, refuse to connect unless sectorSize is 512 (the only size the rest of the driver supports) and sectorCount is non-zero.
- **Justification (adds a check):** two comparisons at the single point geometry enters the driver; conforming servers are unaffected.
- **Benefit:** A stray, spoofed or corrupt INFO reply can no longer register a nonsense block device with FatFs.
- **Effort:** S · **Risk:** Low · **Confidence:** Med (READ; the spoofability leg is INFERRED from the 3-bit cmdid and broadcast design, the missing validation is READ)

---

## Already filed — not repeated

- **RIPTOPL-038** — `getStringField`/`utf16ToAscii` unbounded server filename copy (smb.c:288-352, call sites :404, :1498). Re-read in full; the finding stands as filed. (R5-M2-05 above is the separate `memcpy` at :403 that feeds it.)
- **X-31 (cross-repo, round 3)** — UDPRDMA `_ist` wire-controlled app-header size: `udprdma.c:185-199`, the `rx_hdr_buffer` copy gated by nothing but `rx_hdr_received == 0` and the drain into `dummy[UDPRDMA_MAX_APP_HDR]`. Re-read in full at the pin; the code is unchanged in substance and the SMS clamp has still not been cross-ported. Ledger closed, not re-filed. (Relatedly, `rx_hdr_size` is stored at `udprdma.c:736` and still never read anywhere in the file — part of X-31's fix surface, noted here only for completeness.)
- **RIPTOPL-043** — UDPFS per-read result-count clamp gap (in `udpfs_core.c`, outside this section's files; referenced by X-32). Not re-examined here.
- **RIPTOPL-046** — the two parallel SMB1 stacks (cdvdman `smb.c` vs smbman-ra `smb.c`, 19 shared functions). smb.c re-read in full confirms the pair's state; R5-M2-06 is the auth/hash layer of the same disease in a different module pair, not a repeat.
- **RIPTOPL-056** — the two DES copies (smbinit `des.c` vs smbman-ra `des.c`). DES sources were not in this section's file list; R5-M2-06 covers the adjacent MD4/auth pair only.
- **Round-1 UDPFS identity finding** — `udpfs_bd/src/main.c` and `udpfs_ioman/src/main.c` are byte-identical (verified again by diff this round: IDENTICAL), already filed with the `udpfs_core.*` identity set.
- **Round-4 smb_fio devctl finding (Sable-RiptOPL-ROUND4.md, smb_EchoServer leg)** — caller-supplied `len` reaching `smb_Echo` (smb.c:1565-1598, the `(u16)len` memcpy/memcmp) unchecked. The callee side re-read here; the substance is already filed from the caller side.
- **Round-2 clearance, httpclient.c** — the 512-byte request buffer in `HttpSendGetRequest` (httpclient.c:358-372) is bounded by the RPC struct field sizes with EE-side NUL termination; re-read confirms nothing outside that argument changed. Not re-filed in either direction.

## Checked and clean (dissolved on full reading)

- **udprdma.c beyond X-31** — data-payload receive clamps `xfer_size` to the remaining rx buffer (`:204-212`); `_send_data_ll`'s `memcpy` into `pkt_data.extra` is gated by `udprdma_send_ll`'s `app_hdr_size > UDPRDMA_MAX_APP_HDR` rejection (`:595`); wait loops use `WaitEventFlag` (round-2 note stands); the SetAlarm-failure path (`:105-116`) fails the wait instead of hanging. Clean.
- **udpbd.c RDMA receive** — the remaining-bytes rejection at `:357-363` plus cmdpkt ordering (`:333`) and MTU bound (`:344`) close the payload path; the write-done/error semantics at `:281-288` are deliberate and documented. (Geometry intake is R5-M2-10; the payload path itself is clean.)
- **ministack_xchg.c** — reply handler validates UDP length against IP length before the FIFO read (`:273-281`), clamps to `MSTACK_XCHG_REPLY_MAX`, and the devctl entry validates `arglen` against the claimed `req->len` (`:353-356`). Exemplary; clean.
- **ministack_arp.c (udpfs)** — conflict detection, own-address refusal and full-table replacement policy are deliberate and commented; `arp_resolve` is bounded to 5 retries. Clean.
- **rapull.c** — every list/node/bytes input is range-checked at `_start` (`:380-383`) and again in `rp_frame_values` (`:280`); the `rp_setup` partial-allocation path's leftover buffers are freed by the thread cleanup at `:338-345`. The TODOs in the header are honesty, not defects. Clean.
- **rapops.c** — blob offsets/sizes validated before `LoadModuleBuffer` (`:32-33`); the argv hex block exactly fits its 61-byte buffer (45 + 15 + NUL); config counts re-validated before use (`:105-108`). Clean.
- **httpclient.c body path** — `HttpGetResponse` clamps `ContentLength` to the caller's `*length` before the body read (`:232-233`) and reports short transfers as `-EPIPE`; the `mtime[]` month/day bytes index `months[]`/`daysInMonth[]` (`:331, 371`) from OPL's own EE-side date struct, not from wire data (INFERRED from the RPC struct path in `httpclient/main.c` and `src/platform/httpclient.cpp:72` — the EE date producer itself is outside this section). No wire-reachable overflow found outside the round-2 clearance.
- **httpclient/main.c** — RxBuffer was deliberately grown to 1024 for the streaming args struct (`:19-22`); stream `wanted` is clamped both directions (`:90-93`); SEND_GET clamps `out_len` to the DMA buffer (`:45-48`). The `while (sceSifSetDma(...) == 0);` spins (`:60, 104`) are the standard IOP DMA-submission idiom under suspended interrupts, not a poll loop on external state. Clean.
- **smb.c remaining commands** — Close/Delete/ManageDirectory/Rename/TreeDisconnect/LogOff request construction uses only caller-side strings through the (separately filed, RIPTOPL-038) string helpers; WriteAndX's Count validation (`:1170`) and the ByteCount wrap fix (`:30-33`) are present and correct; `smb_OpenAndX`'s EBUSY legacy-retry logic is deliberate and documented. Clean apart from the findings above.
- **poll.c mechanics** — select-based wait is correct for its single-fd caller; only the dead `ifd` machinery is filed (R5-M2-08).
- **nbns/main.c, smbman.c, smbinit/main.c, udpfs main.c stubs, udpfs_bd.c, udpfs_fhi.c** — registration/watchdog glue; bd/fhi callbacks clamp sector ranges before calling the core (`udpfs_bd.c:45-49, 71-75`), watchdog teardown placement is deliberate and documented. Clean.
- **udptty.c** — write path is a semaphore-guarded single packet send; size truncation at the u16 length field is unreachable for tty-sized writes. Clean.
- **Duplication spot-checks that dissolved:** `udpfs_bd/src/udpfs_bd.c` vs `udpfs_fhi/src/udpfs_fhi.c` share only the watchdog *pattern* (different contracts: BDM registration vs export library + semaphore) — intentional parallel structure, not copy-paste. smbinit `main.c` is a 19-line negotiate-and-exit stub with no counterpart. `ip_addr`/default-IP constants in both ministacks are part of R5-M2-07's shared core, not a separate finding.

## Forced revisions to prior findings

None. No prior finding (RIPTOPL-001…144, X-31) was contradicted by this section's full reads; two were confirmed still-open at the pin (RIPTOPL-038's call sites, X-31's unported clamp).

## Method note

Line counts are `wc -l` at the pin; every ledger file was read with `cat -n` in full (smb.c and udprdma.c in two paginated passes each). Diff measurements for R5-M2-06/07 are from `diff` on the in-tree copies at the pin. `git status --porcelain` re-checked empty after all reads.
