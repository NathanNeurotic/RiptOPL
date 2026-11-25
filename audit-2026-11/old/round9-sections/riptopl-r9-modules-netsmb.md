# RiptOPL Round 9 — modules/network SMB/NFS/NBNS/HTTP full line-read (NETSMB)

- Repo: `~/workspace/audit-clones-2026-11-13/RiptOPL`
- Pin verified at start: `2e347d5ac885cd0ff73e9ad4143781946396f492` (`git rev-parse HEAD` matched; `git status --porcelain` empty). Clone left git-clean; no edits in the repo.
- Scope read in full: every `.c/.h/.S/.s` under `modules/network/smbman-ra/`, `smb2man/`, `nfsman/`, `smbinit/`, `nbns/`, `common/`, `httpclient/` — 14,675 lines (ledger at end). Additionally read in full because `httpclient.c` `#include`s it: `modules/network/common/httpstream.inc` (334 lines, not a `.c/.h` file, so outside the 14,675 ledger total).
- Already filed and NOT re-filed here (cross-referenced where a new finding touches the same code): RIPTOPL-016, -037, -038, -040, -046, -053, -056, -074, -075, -085, -088, -094, -116, -145, -146, -150, -153, -154, -209, -246, -261.
- No secret-like values were encountered or are printed.

---

## Candidates (sorted by benefit)

### NETSMB-01 — FileTimeToDate converts ~half of all dates to garbage (SMB1 browse/stat timestamps)
- **Category:** 6 bugs
- **Locations:** `modules/network/smbman-ra/smb_fio.c:558-611` (definition; leap-day subtraction at `:571-576`), used at `modules/network/smbman-ra/smb_fio.c:614-616` (`smb_statFiller`, feeding `smb_dread`/`smb_getstat`)
- **Problem:** The FILETIME→calendar conversion computes `years = total_seconds / 365 days`, then subtracts the *cumulative* leap-day count (`leapdays = years/4 - years/100 + years/400`, ~103 for the 2020s) from the *remainder* day count in a `u16`. Late in each year the 365-division has already rolled into the next year, the remainder is smaller than `leapdays`, and `days -= leapdays` underflows. Executed proof (host replication of the exact integer ops, including `u16` widths): 2026-11-14 12:34:56 converts to year **2028, month 1, day 65124**; 2026-12-31 likewise; 2025-03-01 converts to Feb 29 (separate `days > daysPerMonth[i]` boundary error at `:583-589`). Sampling the 1st/15th/28th of every month 2020–2030: **198 of 396 dates convert wrong**. Every SMB1 directory listing and stat for files modified on those dates shows a corrupt date. smb2man and nfsman each carry a correct civil-from-days converter (`smb2man_pack_time` / `nfsman_pack_time`); this is a third, divergent, broken copy.
- **Fix:** Delete the `years`/`leapdays` algorithm and replace the body of `FileTimeToDate` with the civil-from-days conversion already proven in-tree (days-since-epoch → era/yoe/doy arithmetic), keeping the same `datetime[8]` output layout.
- **Justification if additive:** Replacement, not addition: ~35 lines of arithmetic removed, ~20 lines of already-in-tree arithmetic reused. Net negative.
- **Benefit:** Correct timestamps for roughly half the calendar in the SMB1 browser; removes a third divergent date converter.
- **Effort:** S
- **Risk:** Low — pure function, host-testable against known FILETIMEs before/after.
- **Confidence:** READ (algorithm executed on host; counts above are measured, not estimated)

### NETSMB-02 — smb_FindFirstNext2 trusts response ParamOffset/DataOffset/FileNameLen with no bounds at all
- **Category:** 6 bugs/security
- **Locations:** `modules/network/smbman-ra/smb.c:1474-1498`
- **Problem:** The response param block is taken from `&SMB_buf.smb.u8buff[FFNRsp->smbTrans.ParamOffset]` (with a `- 2` variant for FIND_NEXT2 at `:1480`), the data block from `&SMB_buf.smb.u8buff[FFNRsp->smbTrans.DataOffset]` (`:1482`), and the name copied with `getStringField(info->FileName, FFRspData->FileName, FFRspData->FileNameLen)` (`:1498`). All three values are server-supplied `u16`/`u32` and none is checked against the received packet size or the 4,224-byte `SMB_buf`. A hostile/broken server sets `DataOffset` near 65535 and the client reads a `FindFirst2ResponseData_t` far past the static buffer; `FileNameLen` then drives `utf16ToAscii`/`strncpy` in `getStringField` (`smb.c:336-352`) to read that far-away memory and write `FileNameLen`-bounded output into the caller's `SearchInfo_t`, which in `smb_fio.c` is the 4,096-byte static `SearchBuf` — overrunning it into the adjacent `smb_curdir`/`smb_curpath` statics. Distinct from RIPTOPL-154 (same pattern in `smb_QueryPathInformation`); this function was not covered by it.
- **Fix:** Before either dereference, require `ParamOffset` and `DataOffset` to lie inside the received length returned by `GetSMBServerReply`, and clamp `FileNameLen` to the bytes actually remaining between `FFRspData->FileName` and the packet end; return `-EIO` otherwise.
- **Benefit:** Closes a server-controlled OOB read + static-buffer overflow on the directory-listing path — the most-used SMB1 operation.
- **Effort:** S
- **Risk:** Low — rejects only packets no conforming server sends.
- **Confidence:** READ

### NETSMB-03 — Negotiate response domain name is `strcpy`'d unbounded into a 64-byte field
- **Category:** 6 bugs/security
- **Locations:** `modules/network/smbman-ra/smb.c:336-352` (`getStringField`, `inbytes == 0` branch at `:346-348`), called at `modules/network/smbman-ra/smb.c:404`; destination `modules/network/smbman-ra/smb.h:285` (`char PrimaryDomainServerName[64]`)
- **Problem:** `smb_NegotiateProtocol` copies the server's primary-domain string with `getStringField(..., 0)`. With `inbytes == 0` and no `SERVER_CAP_UNICODE`, that is a plain `strcpy` (and in the Unicode branch, `utf16ToAscii` with `inbytes == 0` loops until a NUL wchar). The source is the received packet at `&NPRsp->ByteField[KeyLength]`; nothing bounds the copy to the 64-byte destination or the scan to the received packet. A negotiate response whose domain field runs more than 63 non-NUL bytes overwrites the rest of `server_specs_t` (including `EncryptionKey` at `smb.h:286`) and beyond. Distinct from RIPTOPL-153 (the `KeyLength`→`EncryptionKey` memcpy in the same packet, different sink).
- **Fix:** Pass the remaining received length as `inbytes` and make the `inbytes == 0` branch bounded the same way (copy at most `dest size - 1`); one call-site change plus a size parameter, or clamp inside this single call.
- **Benefit:** Removes a pre-auth, server-controlled overflow of a global struct during the first packet exchange.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### NETSMB-04 — smb_closeAll() closes FIDs but never frees the handles — stale slots leak and FIDs can be re-closed on a later session
- **Category:** 6 bugs
- **Locations:** `modules/network/smbman-ra/smb_fio.c:402-413` (called from `smb_LogOff` and `smb_CloseShare` in the same file)
- **Problem:** `smb_closeAll` sends `smb_Close` for every handle with `smb_fid != -1` but leaves `fh->f`, `fh->smb_fid`, `fh->mode` untouched. Consequences: (1) `smbman_getfilefreeslot()` only reuses slots with `fh->f == NULL`, so after one logoff/close-share every previously opened slot is occupied forever — repeated reconnect cycles exhaust all 32 slots and opens start failing `-EMFILE`; (2) a later `smb_close` on a stale handle re-sends CLOSE for the old FID on the *new* session, where that FID may now belong to a different file. Compare `smb_close` itself, which does `memset(fh, 0, sizeof(FHANDLE)); fh->smb_fid = -1;`.
- **Fix:** After the `smb_Close` call in `smb_closeAll`, clear the slot exactly as `smb_close` does (or factor the clearing into the loop). Deletion-shaped: no new state.
- **Benefit:** Fixes a permanent handle leak across reconnects and a wrong-file close hazard.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### NETSMB-05 — usmb2_build_request drains a server-declared SPL of any size; no reply-size ceiling
- **Category:** 6 bugs/security (also 5 performance — lock held while draining)
- **Locations:** `modules/network/smb2man/usmb2/usmb2.c:331` (only check is `spl < 64`), drain loop at `modules/network/smb2man/usmb2/usmb2.c:469-480`
- **Problem:** After the header, the reply's remaining length is whatever the server's 32-bit SPL says. Leftover bytes are discarded in a `while (spl)` loop reading a buffer-full at a time, with no upper bound tied to what a reply can legitimately be (context buffer 1,024 + the caller's `indata`, at most 64 KiB in smb2man). A broken or hostile server declaring, say, 100 MB keeps the driver — holding the smb2man semaphore — receiving/discarding (or timing out chunk by chunk) instead of failing the request. The SMB1 side's equivalent trust was filed as RIPTOPL-145; the SMB2 side has no equivalent filing.
- **Fix:** Fail (`socket_failed`) when `spl` exceeds `sizeof(usmb2->buf) + indatacount` before reading the body — the same shape as unfs4's record-mark ceiling (NETSMB-06 context, `unfs4.c` already does this).
- **Benefit:** One comparison removes an unbounded, lock-holding drain on every SMB2 request.
- **Effort:** S
- **Risk:** Low — legitimate replies are bounded by the negotiated read/write sizes the driver itself clamps to 64 KiB.
- **Confidence:** READ

### NETSMB-06 — Replies are matched to requests only when signing is on (usmb2 MessageId) or never (unfs4 xid)
- **Category:** 6 bugs/security
- **Locations:** `modules/network/smb2man/usmb2/usmb2.c:404-407` (MessageId checked only inside the `verify` branch); `modules/network/nfsman/unfs4/unfs4.c:475-530` (`unfs4_rpc` non-TINY_TCP path never compares the reply xid at `buf[4..7]` with `unfs4->xid` set at `:483`; the xid comparison exists only in the TINY_TCP-only `recv_reply_from_socket` at `:169-172`, which this build never compiles)
- **Problem:** On an unsigned SMB2 session, `usmb2_build_request` accepts the next frame on the wire as the reply to the current command regardless of its MessageId — the code's own comment at `usmb2.c:398-403` explains why a *verified* reply must answer this request, but the check is gated on `verify`. A delayed/duplicated/mis-sequenced frame is then parsed as e.g. a CREATE reply and its "FID" used for reads/writes. unfs4 has the same hole without even a signed mode: COMPOUND status and results are read from fixed offsets (`unfs4.c:520-529`) of whatever record arrived.
- **Fix:** Move the MessageId comparison out of the `verify` gate in usmb2 (fail the request on mismatch, as the verify path already does); in `unfs4_rpc`, compare `get32(&b[4])` to `unfs4->xid` after the record mark and fail on mismatch. Two comparisons total.
- **Benefit:** Both vendored clients stop interpreting one request's reply as another's — a correctness as well as security property on shared/flaky networks.
- **Effort:** S
- **Risk:** Low — conforming servers always answer in order with the matching id; mismatch today means silent misparse.
- **Confidence:** READ

### NETSMB-07 — smb2man_querydir trusts the reply's OutputBufferLength over the bytes actually delivered
- **Category:** 6 bugs/security
- **Locations:** `modules/network/smb2man/smb2man.c:1241-1269` (length taken at `:1264-1265`)
- **Problem:** `d->len` is set from the server-supplied `OutputBufferLength` field (clamped only to the 4,096-byte `SMB2MAN_DIRBUF`), not from how many bytes `usmb2_build_request` actually placed in `d->data`. If the field claims more than the frame carried, `smb2man_dread` (`smb2man.c:1272+`) parses stale bytes from the previous listing as directory entries (names, sizes, dates shown to the user; a stale `NextEntryOffset` chain is followed within the clamped length). The READ path in the same vendored stack was already hardened against exactly this (the `usmb2_prw` reply-length cross-check in `usmb2.c`); QUERY_DIRECTORY was not.
- **Fix:** Record the delivered byte count from `usmb2_build_request` (frame length minus header/body offsets, as the READ check computes it) and set `d->len = min(claimed, delivered)`.
- **Benefit:** Directory listings can no longer be fabricated out of the previous listing's leftovers by a lying length field.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### NETSMB-08 — httpclient RPC handlers use EE-supplied strings unterminated (only the streaming op terminates them)
- **Category:** 6 bugs/security
- **Locations:** `modules/network/httpclient/main.c:36` (`CONN_ESTAB` passes `->server` straight through), `modules/network/httpclient/main.c:44-60` (`SEND_GET_REQ` passes `UserAgent`/`host`/`uri`/`mtime` through); sink at `modules/network/httpclient/httpclient.c:362-371` (`sprintf`/`strcat` into `char buffer[512]`, `months[mtime[1]]` indexed by an unchecked RPC byte)
- **Problem:** `HTTP_CLIENT_CMD_STREAM_BEGIN` explicitly NUL-terminates `a->host`/`a->uri` with a comment calling the RPC struct a trust boundary (`main.c`, stream-begin case). The older handlers do not: a `server[30]`, `host[30]`, `uri[128]` or `UserAgent[16]` field filled without a NUL makes `ResolveHostname`/`gethostbyname` and the `sprintf("%s"...)` chain in `HttpSendGetRequest` read past the field into the rest of the 1,024-byte RPC receive buffer and write past `buffer[512]` on the RPC thread's 0x800-byte stack. Separately, `mtime[1]` (a raw RPC byte, 0–255) indexes the 12-entry `months[]` array unchecked at `httpclient.c:371`, and `GetDayInWeek` loops on the same byte.
- **Fix:** Terminate the four string fields in the two older handlers exactly as the stream-begin handler already does (4 one-line stores), and reject `mtime[1] > 11` before formatting. No new mechanism — copying the in-file precedent.
- **Benefit:** Closes a stack-overflow + OOB-read pair on the RPC boundary the module itself already treats as untrusted elsewhere.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### NETSMB-09 — NBNS receive thread copies the answer address from a fixed offset without checking the datagram is that long
- **Category:** 6 bugs/security
- **Locations:** `modules/network/nbns/nbns.c:179` (only length check: `>= sizeof(struct NbHeader)`, 12 bytes), `:189` (positive-response test), `:198` (address `memcpy` from `frame[12 + 34 + 4 + 2 + 2]`, i.e. offset 54, 4 bytes)
- **Problem:** Any UDP datagram of ≥12 bytes with the QUERY/response opcode bits, RCODE 0, `ANCount == 1`, a decodable name matching the pending record and the right transaction id is accepted, and the "address" is copied from bytes 54–57 of the static 576-byte `frame` — even if the datagram was, say, 20 bytes long. The bytes beyond the received length are whatever the previous datagram left in `frame`, so a short spoofed/malformed response can install a stale, attacker-influenced IP as the resolved server address and wake the waiter with success. (The name decode itself is RIPTOPL-037; this is the unchecked address read after it.)
- **Fix:** Require `length >= sizeof(struct NbHeader) + 34 + sizeof(struct NbQuestionTrailer) + 4 + 2 + 2 + 4` before the `memcpy` — one comparison against constants already in the expression.
- **Benefit:** A resolved SMB server IP can no longer come from bytes the datagram never contained.
- **Effort:** S
- **Risk:** Low — real NBNS answers are always full-size.
- **Confidence:** READ

### NETSMB-10 — smb_lseek64 accepts negative positions (SEEK_SET/SEEK_CUR check only the upper bound)
- **Category:** 6 bugs
- **Locations:** `modules/network/smbman-ra/smb_fio.c:833-868` (`SEEK_CUR` at `:840-846`, `SEEK_SET` at `:847-853`)
- **Problem:** Both cases reject `r > fh->filesize` but never `r < 0`, so `lseek(fd, -5, SEEK_SET)` or a SEEK_CUR that steps before 0 stores a negative `fh->position`. The next `smb_read` computes `size = fh->filesize - fh->position` (larger than the file) and `smb_ReadFile` casts the negative offset to a huge unsigned SMB offset. Both sibling drivers check this (`smb2man_seek`: `if (base + offset < 0) return -EINVAL;`; nfsman's lseek64 likewise) — smbman-ra is the odd one out.
- **Fix:** Add the same `r < 0` rejection the two sibling drivers already have (one condition per case, or one after the switch).
- **Benefit:** Brings the third driver's seek contract into line; removes a negative-offset read primitive against the server.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### NETSMB-11 — smb_dopen appends "\\*" to fh->name with strcat after a 255-char strncpy — overflow and unterminated-name read
- **Category:** 6 bugs/security
- **Locations:** `modules/network/smbman-ra/smb_fio.c:660-663`
- **Problem:** `strncpy(fh->name, path, 255)` into `char name[256]` leaves a 255-char path unterminated; the next line reads `fh->name[strlen(fh->name) - 1]` (scanning past the field for a NUL), and the two `strcat`s then append up to 3 more bytes at wherever that scan lands — past the 256-byte field of the static `smbman_fdhandles[]` array for long directory paths. Cross-reference RIPTOPL-040 (the unterminated-`strncpy` pattern in this file); the `strcat` sink here is separate.
- **Fix:** Bound the copy to `sizeof(fh->name) - 3` (leaving room for `\*` + NUL) and force termination, as the open path should; the search pattern then always fits.
- **Benefit:** Removes a static-array overflow reachable with a merely long (not malicious) directory path.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### NETSMB-12 — prepare_path's trailing-slash scan is dead code that starts on the NUL terminator
- **Category:** 1 dead code (with a latent 6 if "fixed" naively)
- **Locations:** `modules/network/smbman-ra/smb_fio.c:297-330` (`p2` scan at `:312-314`; `strncpy` at `:303`)
- **Problem:** `p2 = &path[strlen(path)]` points at the terminating NUL, so `while ((*p2 == '\\') || (*p2 == '/')) p2--;` never executes — the trailing-separator stripping the comment promises does not happen, and the copy loop's `p <= p2` bound instead copies the NUL itself into `full_path` and appends the backslash after it. The apparatus costs a scan setup per path operation and misleads maintainers; worse, the obvious one-character "fix" (`strlen(path) - 1`) underflows to `path[-1]` for empty/all-slash paths.
- **Fix:** Delete the `p2` scan and bound the copy loop by the string end directly (`*p != '\0'`), which is what the code already effectively does. Pure deletion/simplification.
- **Benefit:** Removes dead logic from the path builder every SMB1 operation passes through, and removes the underflow trap for the next editor.
- **Effort:** S
- **Risk:** Low — behaviour is unchanged because the scan never ran.
- **Confidence:** READ

### NETSMB-13 — Vendored clients carry entry points and whole transports this build never uses (usmb2/unfs4 dead-in-build)
- **Category:** 1 dead code / 7 build clutter
- **Locations:** `modules/network/smb2man/usmb2/usmb2.c:504-531` (`usmb2_negotiateprotocol`, called only by the also-unused `usmb2_init_context` at `:1034-1085`), `:717-742` (`usmb2_open` — `modules/network/smb2man/ps2iop-compat.h:15` itself notes "smb2man never calls usmb2_open()"), `:744-830` (`usmb2_opendir`/`usmb2_readdir`, additionally gated on `USMB2_FEATURE_OPENDIR`, which the build does not define), `:1002-1032` (`usmb2_size`); `modules/network/nfsman/unfs4/unfs4.c:884-916` (`unfs4_init_context`, unused — nfsman does its own connect+mount); the `USMB2_FEATURE_TINY_TCP` / `UNFS4_FEATURE_TINY_TCP` (Z80) transport blocks throughout both files (e.g. `usmb2.c:39-153`, `unfs4.c:60-190`)
- **Problem:** smb2man drives usmb2 exclusively through `usmb2_create`/`usmb2_close`/`usmb2_prw`/`usmb2_stat`/`usmb2_mkdir`/`usmb2_delete`/`usmb2_rename`/`usmb2_sessionsetup`/`usmb2_treeconnect`/`usmb2_build_request` (grep-verified call census of `smb2man.c`), and nfsman likewise never calls `unfs4_init_context`. Roughly 400 lines of vendored source are compiled-or-gated for transports and front doors this firmware does not have, including a second, unchecked directory-reader (`usmb2_readdir`'s `memcpy(&dh[16 + 4], &usmb2->buf[8], u32)` with a server-supplied `u32`) that invites accidental reuse.
- **Fix:** Delete the unused entry points and TINY_TCP blocks from the vendored copies at the next vendored-sync, recording the deletions in `VENDORED.txt`/`riptopl.patch` like the existing local patches; keep the upstream files' used core untouched.
- **Justification if additive:** Pure deletion.
- **Benefit:** Smaller audit surface in the two security-critical protocol stacks; removes a latent unchecked `memcpy` from reachability-by-accident.
- **Effort:** M (mostly vendored-bookkeeping, not code risk)
- **Risk:** Low for resident size — INFERRED: the build already uses `-ffunction-sections` + `--gc-sections` (`ps2.yaml` smb2man/nfsman entries), so most of this is likely stripped from the IRX today; the win is source/audit size, not RAM.
- **Confidence:** READ for the call census and feature gates; INFERRED for the linker-stripping remark

### NETSMB-14 — smb.h carries 44 protocol-offset defines and ~30 command defines nothing in the module references
- **Category:** 1 dead code / 7 build clutter
- **Locations:** `modules/network/smbman-ra/smb.h:46-95` (`SMB_OFFSET_*`, `SMB_TRANS2_REQ_OFFSET_*`, `SMB_TRANS2_RSP_OFFSET_*` — 44 defines), `modules/network/smbman-ra/smb.h:155-253` (`SMB_COM_*` block)
- **Problem:** Grep over the module (and the repo's other consumers of this header) finds zero uses of any `SMB_OFFSET_*`/`SMB_TRANS2_*_OFFSET_*` define — the code uses the packed structs, not byte offsets. Likewise sampled `SMB_COM_*` values for commands the client never sends (`SMB_COM_READ_RAW`, `SMB_COM_WRITE_MPX`, `SMB_COM_COPY`, `SMB_COM_MOVE`, `SMB_COM_SEARCH`, `SMB_COM_FIND`, `SMB_COM_OPEN_PRINT_FILE`, `SMB_COM_READ_BULK`, `SMB_COM_NT_TRANSACT`, `SMB_COM_LOCK_BYTE_RANGE`, …) have zero references in `smbman-ra/*.c`. ~90 lines of header imply machinery that does not exist.
- **Fix:** Delete the offset blocks and the unreferenced command defines (keep the ~15 commands actually used). Pure deletion.
- **Benefit:** Header shrinks to the protocol the module actually speaks; less to re-audit every round.
- **Effort:** S
- **Risk:** Low — compile is the check; a missed use fails the build loudly, not silently.
- **Confidence:** READ (grep census)

### NETSMB-15 — poll()'s duplicate-fd guard set is never populated, so POLLNVAL/duplicate handling is dead
- **Category:** 1 dead code / 6 bugs (minor)
- **Locations:** `modules/network/smbman-ra/poll.c:24-40` (`FD_ZERO(&ifd)` at `:26`; the `FD_ISSET(fd, &ifd)` skip at `:38`), `:68-77` (`revents = POLLNVAL` branch on the same always-empty set)
- **Problem:** `ifd` is zeroed and tested in both loops but no code path ever sets a bit in it. The first loop's duplicate-fd suppression therefore never triggers (a duplicated fd is `FD_SET` twice and counted twice), and the second loop's `POLLNVAL` assignment is unreachable. The only caller passes a single fd, so impact today is nil — but the function advertises POSIX semantics it does not implement.
- **Fix:** Either delete the `ifd` machinery (LESS: the sole caller uses `nfds == 1`) or populate it where the comment intends. Deletion preferred.
- **Benefit:** ~10 lines of misleading machinery gone from a synchronisation primitive.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### NETSMB-16 — NTLMv2 client challenge is the constant 0x01×8 and the timestamp is zero (vendored usmb2)
- **Category:** 6 bugs/security
- **Locations:** `modules/network/smb2man/usmb2/ntlm.c:230-236` (blob zeroed at `:230-232`; `memset(..., 1, 8)` client challenge at `:235-236`, marked `QQQQ` upstream)
- **Problem:** The NTLMv2 client challenge — the client's only freshness contribution — is hard-coded to eight 0x01 bytes, and the blob timestamp is left at 0. A captured NTLMv2 response is therefore a pure function of the server challenge and password hash, with no client-side nonce: anything that can elicit or replay server challenges faces a fully deterministic response, and servers that log/validate timestamps see a constant 1601-01-01. This is upstream usmb2 behaviour carried in vendored, but it is live in this build's only SMB2 authentication path.
- **Fix:** Fill the 8 challenge bytes (and the timestamp) from a local entropy/time source at auth time — smb2man already has a clock path (its keepalive ticks / `GetSystemTime` equivalents on the IOP). Small, contained to `ntlm_generate_auth`.
- **Justification if additive:** A few lines replacing a `memset`; required for the NTLMv2 construction to provide the freshness it exists for.
- **Benefit:** Restores client-side freshness to every SMB2 logon.
- **Effort:** S
- **Risk:** Low — servers accept any client challenge value.
- **Confidence:** READ (the constant is in the code; the protocol consequence is standard NTLMv2 semantics)

### NETSMB-17 — smb_Connect copies the server IP with strncpy(…, 16) and no termination guarantee; two later sprintf("%s") consumers trust it
- **Category:** 6 bugs
- **Locations:** `modules/network/smbman-ra/smb.c:1601-1618` (`strncpy(server_specs.ServerIP, SMBServerIP, 16)` at `:1618`; field is `char ServerIP[16]` in `server_specs_t`, `smb.h`)
- **Problem:** If the EE-supplied IP string occupies all 16 bytes without a NUL, `server_specs.ServerIP` is left unterminated, and `smb_GetShareList` / `smb_OpenShare` in `smb_fio.c` later `sprintf(tree_str, "\\\\%s\\IPC$", specs->ServerIP)` / `"\\\\%s\\%s"` — reading past the field into `MaxBufferSize` and beyond until a zero byte happens to appear. (With a terminated ≤15-char IP, `smb_OpenShare`'s `tree_str[274]` is an exact fit for a 255-char share name — see checked-clean; the unterminated-IP case is the one that breaks it.)
- **Fix:** Force `server_specs.ServerIP[15] = '\0'` after the `strncpy` (one line), matching the termination the smb2man LOGON handler already performs on its copy.
- **Benefit:** Removes an over-read feeding two `sprintf`s for the price of one store.
- **Effort:** S
- **Risk:** Low
- **Confidence:** INFERRED (depends on the EE-side `smbLogOn_in_t.serverIP` field being fillable to all 16 bytes without a NUL — the struct lives in ps2sdk's `ps2smb.h`, outside this scope; the IOP-side missing terminator is READ)

---

## Byte-compare of sibling copies (md5sum / diff)

| Pair | md5 (in-scope copy) | md5 (sibling) | Verdict |
|---|---|---|---|
| `smbman-ra/md4.c` (182 ln) vs `smbinit/md4.c` (175 ln) | `e80d2421ef54fcbd9aa59ebea1a00e0a` | `eaaeae79742bebc59554921b5bd88e2a` | Diverged (see below) |
| `smbman-ra/md4.h` (7 ln) vs `smbinit/md4.h` (15 ln) | `b1da2628cff174aafa92a134dca315d1` | `6c8f08a4dfa3d2a526aee325731f14dd` | Diverged: guard style + const only |
| `smbman-ra/des.c` (568 ln) vs `smbinit/des.c` (564 ln) | `8c7d69cd5b29f073cd18aa0596f6a099` | `f6acde875c740566b2aad591d8175b56` | Diverged (see below) |
| `smbman-ra/des.h` (7 ln) vs `smbinit/des.h` (15 ln) | `e98b10437ee593e9e571e0dfd8f58b96` | `b385eb583f86ced46105a8309037170e` | Diverged: guard style + const only |
| `smb2man/usmb2/md4.h` (43 ln) | `084d5c837c008c8b3374afe7cff7c999` | — | A third, unrelated MD4: the RFC 1320 streaming API (`MD4Init/Update/Final`, `md4c.c`), not a copy of the two above. |
| `smbman-ra/smb.c` / `smb.h` vs `modules/iopcore/cdvdman/smb.c` / `smb.h` (sibling outside this scope's directories) | `5beebbacdaf3e62853302a95784fe77e` / `a79d84bdd8be96e4a0ca47e4f852bcbf` | `8d56c68e6631b83abf532a40e9a409ae` / `dd801d61203be2cbdf14c730983959bf` | Heavily diverged fork, not a copy: 2,268 diff lines for `smb.c` (1,640 vs 1,177 lines). `smb.h` diff is small: copyright/ps2smb include, info-level names (`SMB_QUERY_FILE_*` vs `SMB_FIND_FILE_*`), and the RA copy's additions (`STATUS_SHARING_VIOLATION`/`DOSERR_SHARING_VIOLATION`, `server_specs_t`, `PathInformation_t`). The cdvdman side was **not** line-read (outside scope); this is a compare result only. |

Divergence detail (functional, beyond the already-filed RIPTOPL-056/-246 duplication):
- **md4.c:** The two copies differ in includes, in `transform()` const-ness, and in the final-block handling: the smbman-ra copy stores the bit-length through a union (`buffer.uibuff[56 / sizeof(unsigned int)]`, alignment-safe), while the smbinit copy still uses unaligned casts `*((unsigned int *)&buffer[56])` / `&buffer[120]` on a plain `unsigned char buffer[128]` stack array. Same algorithm, one copy alignment-fixed, one not.
- **des.c:** All S-box/SP tables are value-identical (diff shows no table changes). Differences are: a large comment block's indentation, const-ness of `key7TOkey8`/`DES_createkeys`/`DES` parameters, and key-storage placement — smbman-ra hoists `DES_Keys[128]` to file scope with `__attribute__((aligned(16)))`; smbinit keeps it function-static with no alignment attribute. Local-variable placement (`s`, `u`) also differs cosmetically.

---

## Checked-clean / dissolved

- **smb2man `smb2man_dread` FileDirectoryInformation layout** — name at `e + 64`, `FileNameLength` at `e + 60`, attributes at `e + 56`, size at `e + 40`: verified against [MS-FSCC] FileDirectoryInformation (the EaSize/ShortName fields belong to the *Both* variant). Correct as written.
- **httpstream.inc stage-buffer cap concern — dissolved:** `gStream.stageLen` is capped at `HTTP_CLIENT_STREAM_CHUNK` (2048), but the header block that feeds it is capped at `HTTP_HDR_MAX` (1024), so `hlen - hdrEnd` can never reach the cap; no body bytes can be silently dropped there.
- **httpstream.inc `ParseDecimal` / `u64ToDec` / header parsing** — overflow guard, duplicate-`Content-Length` rejection, 206-vs-`Content-Range` cross-check and chunked/encoding refusal all read as correct and deliberately strict.
- **smbinit `GenerateLMHashes` 48-byte hash write — dissolved:** it writes 24 + 24 bytes into `ss->Password`, which is `char Password[48]` in `modules/iopcore/common/oplsmb.h` ("either PlainText, either hashed"). Exact fit by design.
- **smb_fio `smb_OpenShare` `tree_str[274]` — dissolved as an overflow:** `\\` + IP (≤15) + `\` + ShareName (≤255, NUL at [255] in the ps2sdk struct as mirrored in `tests/smb2-live/inc-sdk/ps2smb.h`) + NUL = 274 exactly. Exact-fit fragile, but not over — the live hazard is the unterminated-IP input to it (NETSMB-17).
- **unfs4 `get_attrs`** — bitmap-word count and attribute length are both bounded against `end` before any field is read; the `n >= 20` minimum exactly covers the seconds half of TIME_MODIFY it reads.
- **unfs4 `unfs4_readdir` `dir->de.name[len] = 0` write — dissolved:** `get_attrs` must first succeed past the name, consuming ≥28 bytes of attribute block, so the NUL write always lands inside `dir->buf[1024]`.
- **unfs4 `unfs4_prw` result offsets (`RES(u, 2)`)** — verified against the result layout (PUTFH result 8 bytes, then READ/WRITE result): READ count and WRITE count both land where the code reads them.
- **unfs4 `unfs4_rpc` short-read handling — dissolved:** `read_reply` ignores `read_from_socket`'s return, but every failure path there closes the fd (`socket_failed`), and `unfs4_rpc` re-checks `unfs4->fd < 0` before parsing status.
- **smb2man negotiate reply offsets** (`buf[2]` signing bit, dialect at `buf[4]`, `rd32 buf[32]/buf[36]` read/write maxima, CREATE `EndOfFile` at `buf[48]`, QUERY_INFO size at `buf[16]`) — all verified against the SMB2 response layouts and the way `usmb2_build_request` stages body bytes in `buf`.
- **nfsman URL/name handling** — `nfsman_parse_url` bounds (IP length, port via overflow-checked `nfsman_number`, path < 200, option parsing) and `nfsman_connect_devctl`'s `memchr` termination checks read clean; `nfsman_open`'s filehandle copy is bounded by `get_fh`'s `UNFS4_MAX_FH` check in unfs4.
- **smb2man credential handling** — `smb2man_latin1` refuses (rather than truncates) over-long/non-Latin-1 credentials; `conn_user`/`conn_password` ≤63 chars always fit usmb2's 64-byte fields.
- **httpclient legacy path limitations (chunked encoding unimplemented, body read stops at first short read)** — documented at `httpclient.c:1-2` and `:386-395` and superseded in-tree by the streaming path; a known, stated limitation of a compatibility ABI, not a new finding.
- **Duplication already covered, not re-filed:** smb2man/nfsman shared front-end shape (allocator, bounded socket wrappers, `*_pack_time`, slot/generation handles) → RIPTOPL-053 (NETSMB-01's fix should still reuse the pack_time algorithm rather than add a fourth copy); MD4/DES/LM-NTLM copies → RIPTOPL-056/-246 (byte-compare deltas above are the round-9 evidence update for those items).

---

## Ledger — every in-scope file line-read in full

Line counts are `wc -l` at the pin. `.S`/`.s` files in scope: none exist in these directories.

### modules/network/smbman-ra/ — 4,763
| File | Lines |
|---|---:|
| auth.c | 93 |
| auth.h | 14 |
| debug.h | 17 |
| des.c | 568 |
| des.h | 7 |
| irx_imports.h | 27 |
| md4.c | 182 |
| md4.h | 7 |
| poll.c | 91 |
| poll.h | 25 |
| smb.c | 1,640 |
| smb.h | 897 |
| smb_fio.c | 1,130 |
| smb_fio.h | 30 |
| smbman.c | 35 |

### modules/network/smb2man/ — 4,210
| File | Lines |
|---|---:|
| irx_imports.h | 28 |
| ps2iop-compat.h | 29 |
| smb2man.c | 1,780 |
| smb2man_devctl.h | 28 |
| usmb2/hmac-md5.c | 99 |
| usmb2/hmac-md5.h | 40 |
| usmb2/md4.h | 43 |
| usmb2/md4c.c | 240 |
| usmb2/md5.c | 190 |
| usmb2/md5.h | 23 |
| usmb2/ntlm.c | 263 |
| usmb2/ntlm.h | 15 |
| usmb2/unicode.c | 204 |
| usmb2/unicode.h | 13 |
| usmb2/usmb2.c | 1,085 |
| usmb2/usmb2.h | 130 |

### modules/network/nfsman/ — 2,625
| File | Lines |
|---|---:|
| irx_imports.h | 27 |
| nfsman.c | 1,407 |
| nfsman.h | 33 |
| ps2iop-compat.h | 17 |
| unfs4/unfs4.c | 916 |
| unfs4/unfs4.h | 225 |

### modules/network/smbinit/ — 938
| File | Lines |
|---|---:|
| des.c | 564 |
| des.h | 15 |
| irx_imports.h | 10 |
| main.c | 19 |
| md4.c | 175 |
| md4.h | 15 |
| smbauth.c | 139 |
| smbauth.h | 1 |

### modules/network/nbns/ — 456
| File | Lines |
|---|---:|
| irx_imports.h | 9 |
| main.c | 76 |
| nbns.c | 371 |

### modules/network/common/ — 1,112
| File | Lines |
|---|---:|
| httpclient.h | 152 |
| ra_client.h | 53 |
| ra_snap.h | 124 |
| ra_watch.h | 155 |
| rapops_cfg.h | 52 |
| smap_tx.h | 27 |
| smstcpip-common.h | 389 |
| smstcpip.h | 160 |

### modules/network/httpclient/ — 571
| File | Lines |
|---|---:|
| httpclient.c | 402 |
| irx_imports.h | 10 |
| main.c | 159 |

### Total
**14,675 lines read in full. Unread in scope: none (zero).**
Extra, outside the extension ledger but read in full because it is `#include`d by an in-scope file: `modules/network/common/httpstream.inc` (334 lines).
Headers referenced from outside the scope directories and therefore not part of this ledger: `nbns.h`, `oplsmb.h`, ps2sdk's `ps2smb.h` (noted where a finding depends on them — NETSMB-17).
