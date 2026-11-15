# RiptOPL — Round 4, Section 2b: smb_fio.c / mcemu.c / usb_mass.c / mx4sio.c

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` (rebuild/main), confirmed unchanged at audit start (`git rev-parse HEAD`); no fetch, no branch switch, read-only in the repo.
Scope exclusions honoured: RIPTOPL-040 sites in smb_fio.c (prepare_path, smb_dopen :661-663, smb_chdir termination), RIPTOPL-051 sites in mcemu.c (dead `size <= 0` guards, MceRead :845 / MceWrite :896), RIPTOPL-049 (mx4sio sio2man_hook.c/ioplib.c duplication) are **not** re-filed.
Labels: **READ** = mechanism visible in the quoted code at this pin. **INFERRED** = reachability/impact depends on a caller, device, or file outside this section. No build or hardware claims anywhere in this section.

---

## Findings

### R4-2B-01 — FHANDLE is dereferenced before (or without) any NULL check in five smb_fio entry points
- **Category:** 3-adjacent (unchecked pointer / robustness)
- **Path:** `modules/network/smbman-ra/smb_fio.c:375-383, 424-427, 448-451, 684-696, 832-837`
- **Problem + mechanism (READ):** `smb_close` reads the handle before testing it:
  ```c
  FHANDLE *fh = (FHANDLE *)f->privdata;                       // :375
  if ((UID == -1) || (TID == -1) || (fh->smb_fid == -1))      // :378 — derefs fh first
      return -EBADF;
  ...
  if (fh) {                                                    // :383 — the intended check, too late
  ```
  `smb_read` (:427) and `smb_write` (:451) repeat the same guard shape. `smb_dread` (:684-696) has no `fh` check at all before `fh->smb_fid`, and `smb_lseek64` (:835-837) takes the lock and immediately uses `fh->position` with no connection, fid, or NULL validation whatsoever. A call on a file whose open failed / was never completed (privdata NULL) faults the IOP instead of returning `-EBADF`.
- **Fix in words:** Test `fh == NULL` first in the guard of close/read/write, and add the same guard (plus fid check) to dread and lseek64 before any field is read.
- **Justification if ADD:** A few comparisons; no new code paths or allocations. Concrete bug: the NULL test already exists in `smb_close`, just in a position where it can never protect anything.
- **Benefit:** Malformed/failed-open call sequences return an error instead of crashing the SMB filesystem driver.
- **Effort:** Trivial. **Risk:** Minimal — only changes behaviour on paths that currently fault.
- **Confidence:** High on the mechanism (READ); medium on real-world reachability (depends on iomanX calling close/read after a failed open — INFERRED).

### R4-2B-02 — Shared path scratch buffers are filled before the I/O lock is taken
- **Category:** Other — concurrency (outside the 1–6/8 list, flagged anyway)
- **Path:** `modules/network/smbman-ra/smb_fio.c:343-345, 484-486, 511-513, 536-538, 637-639, 737-739, 769-772, 797-799`
- **Problem + mechanism (READ for the ordering, INFERRED for concurrent callers):** Every path-taking operation builds its path in the file-scope globals `smb_curpath`/`smb_secpath` and only then serialises:
  ```c
  char *path = prepare_path(filename, smb_curpath, SMB_PATH_MAX);  // :343
  smb_io_lock();                                                    // :345
  ...
  r = smb_OpenAndX(UID, TID, path, &filesize, flags);               // path consumed inside the lock
  ```
  The lock (`smbman_io_sema`) protects the SMB transaction, not the string the transaction is about. Two overlapping file operations — the second `prepare_path` overwrites `smb_curpath` before the first operation consumes it — make the first operation act on the second operation's file (wrong file opened/deleted/stat'ed). `smb_rename` (:769) is the sharp case: `oldpath` is a pointer into shared `smb_curpath`, not a private copy.
- **Fix in words:** Move the `prepare_path` call to after `smb_io_lock()` in each operation (rename already owns a second scratch buffer; it needs the same move for both calls).
- **Justification if ADD:** No addition at all — a pure reordering of existing lines.
- **Benefit:** The existing semaphore actually covers the data it was meant to cover; removes a wrong-file class of bug under multithreaded EE load.
- **Effort:** Small (eight call sites, mechanical). **Risk:** Low — lock hold time grows by one bounded string copy.
- **Confidence:** Medium. Ordering is READ; whether two iomanX calls can overlap on this IOP build is INFERRED (the driver itself assumes concurrency elsewhere — it owns an I/O semaphore and a keepalive thread).

### R4-2B-03 — smb_lseek64: SEEK_END ignores its offset, and no seek is floored at zero
- **Category:** 3 (offset arithmetic)
- **Path:** `modules/network/smbman-ra/smb_fio.c:832-868`
- **Problem + mechanism (READ):**
  ```c
  case SEEK_CUR:  r = fh->position + pos;
                  if (r > fh->filesize) { r = -EINVAL; ... }   // upper bound only
  case SEEK_SET:  r = pos;
                  if (fh->filesize < pos) { r = -EINVAL; ... }  // upper bound only
  case SEEK_END:  r = fh->filesize;                             // :854-856 — pos discarded entirely
  ...
  fh->position = r;                                             // :866
  ```
  (1) `lseek(fd, -N, SEEK_END)` — the standard "read the trailer" pattern — lands on EOF, not N bytes before it; the following `smb_read` then clamps to zero bytes. (2) A negative SET, or a CUR seek that runs past the start of the file, is accepted and stores a negative `fh->position`; subsequent reads/writes pass that negative s64 to `smb_ReadFile`/`smb_WriteFile`, where it is split into 32-bit offset halves and sent to the server as a near-2^64 offset.
- **Fix in words:** Make SEEK_END compute `filesize + pos`, and reject any computed result below 0 in all three modes before assigning `fh->position`.
- **Justification if ADD:** Two comparisons and one addition on an existing value; no new state.
- **Benefit:** Correct trailer/relative-from-end seeks; a bad seek can no longer poison the handle's position for every later I/O.
- **Effort:** Trivial. **Risk:** Low — SEEK_END with `pos == 0` (the common caller pattern) is unchanged.
- **Confidence:** High (READ, pure in-file arithmetic).

### R4-2B-04 — smb_dread can hand the EE an unterminated directory name
- **Category:** 1 (copy from server data)
- **Path:** `modules/network/smbman-ra/smb_fio.c:714` (destination layout verified in `tests/smb2-live/inc-browse/iomanX.h:24-28`: `char name[256]`)
- **Problem + mechanism (READ):**
  ```c
  strncpy(dirent->name, info->FileName, SMB_NAME_MAX);   // SMB_NAME_MAX == 256 == sizeof(dirent->name)
  ```
  `info->FileName` is the server-supplied name out of `SearchBuf`. A name of 256+ bytes fills `dirent->name` completely; `strncpy` writes no terminator, and nothing in `smb_dread` forces `name[255] = '\0'` afterwards. The EE-side consumer reads a string that runs past the field. (Distinct from the RIPTOPL-040 dopen site: different buffer, different copy, different consumer.)
- **Fix in words:** Copy at most 255 bytes and explicitly terminate the last byte of `dirent->name`.
- **Justification if ADD:** One-byte change of bound plus one assignment; net win — removes an over-read of server-controlled data.
- **Benefit:** Long-named directory entries truncate cleanly instead of leaking adjacent dirent memory into the EE's string handling.
- **Effort:** Trivial. **Risk:** Minimal.
- **Confidence:** High on the missing terminator (READ); the over-long name itself is server-controlled (READ from the FindFirst path's use of the server `FileNameLen` in `smb.c`, cross-referenced, not re-filed here).

### R4-2B-05 — smb_devctl validates neither buffer length, and Echo/GetShareList trust caller length/address fields
- **Category:** 2 (length fields trusted)
- **Path:** `modules/network/smbman-ra/smb_fio.c:1077-1084, 1061-1065, 996-1002`
- **Problem + mechanism (READ):**
  ```c
  (void)arglen;   // :1083
  (void)buflen;   // :1084 — both lengths accepted by the ops table, then discarded
  ```
  Every handler then casts `arg`/`bufp` to a full struct. Two concrete consumers:
  - `smb_EchoServer` (:1064): `return smb_Echo(echo->echo, echo->len);` — `len` is a caller-supplied int for a struct whose echo field is 256 bytes (`include/ps2smb.h`-equivalent layout, `tests/smb2-live/inc-sdk/ps2smb.h:69`); nothing clamps it to that field before `smb_Echo` copies/compares by it.
  - `smb_GetShareList` (:996-1002): DMA destination is computed as `getsharelist->EE_addr + (shareindex * sizeof(ShareEntry_t))` for up to caller-supplied `maxent` entries; neither `maxent` nor the address arithmetic is sanity-checked in this file.
- **Fix in words:** Stop discarding `arglen`/`buflen`: require each command's minimum struct size before casting, and clamp the echo length to the echo field and `maxent` to a sane bound at this entry point.
- **Justification if ADD:** A small validation block at one choke point protects eight handlers at once; the alternative is per-handler checks scattered over the file (more, not less).
- **Benefit:** A malformed devctl from any EE-side caller can no longer drive out-of-bounds reads/DMA through the logon/echo/share paths.
- **Effort:** Small. **Risk:** Low, provided the minimum sizes are taken from the existing struct definitions.
- **Confidence:** Medium-high. The missing validation is READ; the size of the downstream overrun depends on `smb.c`'s packet buffer (INFERRED, cross-referenced only).

### R4-2B-06 — DMA_sendEE busy-spins twice with no bound
- **Category:** 8 (busy-spin on the IOP)
- **Path:** `modules/network/smbman-ra/smb_fio.c:872-888`
- **Problem + mechanism (READ):**
  ```c
  id = 0;
  while (!id) {                                  // :882 — retry sceSifSetDma forever
      CpuSuspendIntr(&oldstate);
      id = sceSifSetDma(&dmat, 1);
      CpuResumeIntr(oldstate);
  }
  while (sceSifDmaStat(id) >= 0)
      ;                                          // :887-888 — spin until DMA completes, forever
  ```
  Both loops are unbounded and yield nothing. If the SIF DMA channel is wedged or the transfer never completes, the calling IOP thread (share enumeration, under the I/O lock) spins forever and the whole SMB driver is dead behind it.
- **Fix in words:** Bound both loops with a retry/poll budget and fail the devctl (return an error, release the I/O lock via the existing unlock path) when it is exhausted.
- **Justification if ADD:** A counter and an error return at one helper; justified by a concrete hang, and LESS than adding timeout machinery per caller.
- **Benefit:** A wedged DMA degrades to a failed share-list request instead of a hung IOP.
- **Effort:** Small. **Risk:** Low-medium — the budget must be generous enough for a legitimate 512-byte DMA (it is short; any reasonable budget suffices).
- **Confidence:** High (READ — no bound exists in the code).

### R4-2B-07 — mcemu trusts cspec.PageSize against fixed 512-byte data / 32-byte ECC buffers
- **Category:** 2 (length field trusted from card-side spec data)
- **Path:** `modules/mcemu/mcemu.c:791, 806-818, 853-856, 900-903` (buffer sizes: `modules/mcemu/mcemu_var.c:61-62` — `mceccbuf[MCEMU_PORTS][0x20]`, `mcdatabuf[0x200]`, shared by both cards)
- **Problem + mechanism (READ for the in-file uses; INFERRED for the spec's origin):** Nothing in this file bounds `mcd->cspec.PageSize`, yet it sizes operations directly on the two fixed buffers:
  ```c
  mips_memset(mcd->dbufp, r, mcd->cspec.PageSize);                       // :791 erase fill
  for (r = 0, i = 0; r < mcd->cspec.PageSize; r += 128, i += 3)
      CalculateECC(&(mcd->dbufp[r]), &(mcd->cbufp[i]));                 // :817-818 do_read
  size = mcd->cspec.PageSize - mcd->rdoff;                              // :854 read cap
  mips_memcpy(&mcd->dbufp[mcd->wroff], buf, size);                      // :903 write (cap :901)
  ```
  The file itself proves non-512 values are contemplated — the BDM erase fast path is gated on `PageSize == 0x200` (:772) and everything else falls through to the unbounded path. Any PageSize above 0x200 overflows `mcdatabuf` on erase/read/write; above ~0x440 the ECC loop also walks off the 32-byte `mceccbuf`. `cspec` is copied in from `vmcSpec` (set up outside this file from the VMC image's parameters), so a malformed or mismatched VMC image is the plausible source — that hop is INFERRED, the overflow arithmetic is not.
- **Fix in words:** Reject (fail `mc_configure`-fed cards / clamp at first use in this file) any card whose PageSize exceeds the data-buffer size, once, instead of checking at every memcpy.
- **Justification if ADD:** One comparison at configuration time; justified by four separate overflow sites it closes at once.
- **Benefit:** A bad VMC spec degrades to "card not present" instead of IOP heap/static corruption shared by both emulated cards.
- **Effort:** Small. **Risk:** Low — genuine PS2 memory cards use 512-byte pages, which pass unchanged.
- **Confidence:** Medium (in-file mechanism READ and unconditional; trigger depends on the out-of-file spec path, INFERRED).

### R4-2B-08 — mcemu accepts any page number from the game packet; CardSize is never used as a bound
- **Category:** 3 (sector/page arithmetic, unchecked range)
- **Path:** `modules/mcemu/mcemu.c:603, 609, 614, 758-800, 822-829, 871-878`
- **Problem + mechanism (READ):** The page number is a raw signed int assembled from the SIO packet and stored/used verbatim:
  ```c
  result = MceEraseBlock(mcd, GetInt(&wdma[2]));   // :603
  result = MceStartWrite(mcd, GetInt(&wdma[2]));   // :609
  result = MceStartRead(mcd, GetInt(&wdma[2]));    // :614
  ```
  `MceStartRead`/`MceStartWrite` just assign `mcd->rpage`/`mcd->wpage`. `MceEraseBlock` then writes `BlockSize` pages starting at `page + i` (:785 BDM path, :794 per-page path). Nowhere in the file is any of these compared with `mcd->cspec.CardSize` — the field exists in the descriptor and is even reported back to the game (case 0x26), but is never used for validation. Negative and beyond-end pages alike flow to the device layer, whose offset is `page * PageSize` (device-*.c, cross-referenced), i.e. reads/writes outside the VMC image, wrapping the file offset for negative/huge pages.
- **Fix in words:** Validate the start page (and, for erase, `page + BlockSize`) against `cspec.CardSize` at the three entry points and fail the command the same way an I/O error already fails it (`result = 0` / `io_error`).
- **Justification if ADD:** Three small range checks reusing an error path that already exists; no new machinery.
- **Benefit:** A buggy or hostile game/MCMAN sequence can no longer read or erase outside the virtual card image (adjacent VMC files / filesystem data on shared media are the realistic neighbours).
- **Effort:** Small. **Risk:** Low — in-range traffic is untouched.
- **Confidence:** Medium-high. Absence of the check is READ (whole file read; `CardSize` has no other use in it); the exact downstream offset behaviour is INFERRED from the device files.

### R4-2B-09 — hookDmac_request dereferences _SysAlloc's result unchecked, and a repeated sio2in request leaks the packet
- **Category:** 4 (unchecked allocation)
- **Path:** `modules/mcemu/mcemu.c:489-505` (free at :511)
- **Problem + mechanism (READ):**
  ```c
  case 11: // sio2in
      temp_packet = _SysAlloc(sizeof(Sio2Packet));   // :489 — result never tested
      temp_packet_version = sio2TransferVersion;
      temp_packet->wrmaddr = addr;                   // :491 — NULL deref on allocation failure
  ```
  The channel-12 branch does test `temp_packet == NULL` (:497), which makes the omission on the allocating branch conspicuous. Two further defects on the same lines: a second channel-11 request before channel 12 overwrites the global pointer and leaks the first allocation permanently; and the debug print at :505 reads `((u8 *)temp_packet->wrmaddr)[1]` without any check that the caller's buffer holds 2 bytes. This file's own comments (:773-776) note games that are "tight on IOP memory with mcemu resident" — allocation failure here is not theoretical.
- **Fix in words:** Return failure from the channel-11 case if the allocation returns NULL, and free/ignore an already-pending packet before replacing it.
- **Justification if ADD:** Two comparisons in one switch case; no new allocation or structure.
- **Benefit:** Memory pressure during a patched mc2_s1 transfer returns an error instead of crashing the IOP; repeated requests stop leaking.
- **Effort:** Trivial. **Risk:** Minimal.
- **Confidence:** High on the unchecked dereference (READ); medium on the leak's frequency (depends on mc2_s1's request pattern, INFERRED).

### R4-2B-10 — Sio2McEmu writes fixed/derived response lengths never checked against the packet's own transfer length
- **Category:** 1 (unbounded write into the caller's DMA buffer)
- **Path:** `modules/mcemu/mcemu.c:591, 618-627, 636-640, 651-663`
- **Problem + mechanism (READ for the writes; INFERRED for buffer sizing):** The per-command length decoded from the control word is `length = (*pctl >> 18) & 0x1FF` (:591), and `SioResponse` honours it — but three cases do not:
  ```c
  case 0x26:  mips_memcpy(&rdma[3], &mcd->cspec, sizeof(McSpec));
              rdma[11] = ...; rdma[12] = mcd->tcode;              // :623-626 — 13 bytes regardless of length
  case 0x28:  SioResponse(mcd, rdma, 4);
              rdma[4] = mcd->tcode;                              // :639-640 — a 5th byte after a 4-byte response
  case 0x43:  length = wdma[2];                                  // :654 — control-word length discarded
              result = MceRead(mcd, &rdma[4], length);
              rdma[length + 4] = CalculateEDC(&rdma[4], length);
              rdma[length + 5] = mcd->tcode;                     // :657-663 — up to rdma[260]
  ```
  Each command's `rdma` segment is the caller's DMA buffer slice for that command; nothing in this handler verifies the slice holds 13, 5, or `length + 6` bytes respectively. A packet whose control-word length disagrees with its command byte (malformed or mismatched MCMAN/game build) writes past the segment — into the next command's segment or past the buffer.
- **Fix in words:** Gate each of the three cases on the control-word length (`length >= 13`, `>= 5`, and for 0x43 require `wdma[2]` to fit the segment the control word describes), failing the command via the existing `result = 0` path when it does not.
- **Justification if ADD:** Three comparisons reusing the existing error path; closes the only response paths that ignore the length the handler already decoded.
- **Benefit:** Malformed SIO packets corrupt nothing outside their own response slot.
- **Effort:** Small. **Risk:** Low — well-formed MCMAN packets already satisfy all three bounds.
- **Confidence:** Medium. The unconditional writes are READ; that a real caller's segment can be smaller is INFERRED (segments are sized by the SIO2 packet builder outside this file).

### R4-2B-11 — usb_mass_connect re-reads descriptors with none of usb_mass_probe's NULL checks, and walks endpoints on device-controlled lengths
- **Category:** 2 (length/structure fields trusted from the device)
- **Path:** `modules/usb/usbmass_bd/src/usb_mass.c:664-682` (contrast :597-620)
- **Problem + mechanism (READ):** `usb_mass_probe` checks every descriptor it touches. `usb_mass_connect`, run later for the same plugged device, checks none:
  ```c
  config = (UsbConfigDescriptor *)sceUsbdScanStaticDescriptor(devId, device, USB_DT_CONFIG);  // :666
  interface = (UsbInterfaceDescriptor *)((char *)config + config->bLength);                    // :668 — NULL/huge config, device-supplied bLength
  dev->interfaceNumber = interface->bInterfaceNumber;                                          // :671
  epCount  = interface->bNumEndpoints;                                                         // :674
  endpoint = (UsbEndpointDescriptor *)sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_ENDPOINT); // :675 — may be NULL
  usb_bulk_probeEndpoint(devId, dev, endpoint);                                                // dereferences endpoint->bmAttributes
  for (i = 1; i < epCount; i++) {
      endpoint = (UsbEndpointDescriptor *)((char *)endpoint + endpoint->bLength);             // :679 — walk on device-supplied bLength
      usb_bulk_probeEndpoint(devId, dev, endpoint);
  }
  ```
  A device removed between probe and connect, a scan failure, or a descriptor set with a zero/oversized `bLength` or inflated `bNumEndpoints` turns this into a NULL dereference or a walk through arbitrary USBD static-descriptor memory interpreted as endpoints (whose "addresses" are then opened as pipes).
- **Fix in words:** Apply the same NULL checks probe already has before each dereference, and stop the endpoint walk when the descriptor pointer is NULL or its `bLength` is smaller than an endpoint descriptor.
- **Justification if ADD:** Reuses checks that already exist twenty lines earlier in the same file — duplication of two comparisons is LESS than any new validation helper.
- **Benefit:** A flaky or malicious USB device can no longer crash the BDM USB driver at attach time.
- **Effort:** Small. **Risk:** Low — devices that pass probe pass these checks identically.
- **Confidence:** High on the missing checks (READ, side-by-side with probe); the exploitable-descriptor scenario is INFERRED (descriptor bytes originate from the device via USBD).

### R4-2B-12 — usb_transfer_callback trusts the reported byte count; `remaining` can underflow into an unbounded transfer
- **Category:** 3 (count arithmetic overflow)
- **Path:** `modules/usb/usbmass_bd/src/usb_mass.c:133-149` (chunk size chosen at :121-131)
- **Problem + mechanism (READ for the arithmetic; INFERRED for a misreporting lower layer):**
  ```c
  if (resultCode == USB_RC_OK) {
      data->remaining -= bytes;    // :138 — unsigned; bytes never compared with the chunk just issued
      data->buffer += bytes;       // :139
  }
  if ((resultCode == USB_RC_OK) && (data->remaining > 0) && (bytes > 0)) {
      ret = perform_bulk_transfer(data);   // issues min(remaining, 4096) at buffer
  ```
  Each chunk is at most `USB_BLOCK_SIZE` (4096) and at most `remaining`, but the callback never verifies `bytes <=` the chunk length. A USBD/device error path that reports more bytes than were requested (or a negative count cast through) wraps `remaining` to ~4 billion, after which the loop keeps issuing 4096-byte transfers marching through IOP memory past the caller's buffer — for a read, overwriting whatever follows it.
- **Fix in words:** Clamp/reject in the callback: if `bytes` exceeds the outstanding chunk length, treat the transfer as failed (signal the semaphore with an error) instead of advancing.
- **Justification if ADD:** One comparison at the single place the count is consumed; no other site can fix it.
- **Benefit:** A misreported completion can fail one SCSI command, not stream device data over IOP memory.
- **Effort:** Trivial. **Risk:** Minimal — conforming completions never trip it.
- **Confidence:** Medium. The unchecked arithmetic is READ; whether USBD can actually over-report is INFERRED (it is a device/USBD contract, not verifiable from this file).

### R4-2B-13 — usb_bulk_get_max_lun returns an uninitialised, signed byte when the device answers short
- **Category:** 2 (device data trusted / uninitialised use)
- **Path:** `modules/usb/usbmass_bd/src/usb_mass.c:329-365`
- **Problem + mechanism (READ):**
  ```c
  char max_lun;                                   // :334 — uninitialised, signed
  ret = sceUsbdControlTransfer(..., 1, &max_lun, usb_callback, ...);   // 1-byte request
  if (ret == USB_RC_OK) { WaitSema(cb_data.sema); ret = cb_data.returnCode; }
  if (ret == USB_RC_OK) {
      ret = max_lun;                              // :355 — cb_data.returnSize never checked
  }
  ```
  `usb_callback` records `returnSize`, and sibling code in this same file treats a short CSW as invalid (:278-283) — but here a zero-byte successful completion returns whatever was on the stack, and a device byte of 0x80-0xFF sign-extends to a negative "count". Mitigating fact, stated plainly: the sole in-tree caller (`scsi.c:196-197`) logs the result and immediately overwrites `stat`, so today's blast radius is a garbage debug value and a broken contract for the function-pointer interface — not a live overflow.
- **Fix in words:** Initialise the byte to 0, use an unsigned byte, and only accept the value when `returnSize == 1`.
- **Justification if ADD:** Three token-level changes in one function; justified because the interface (`scsi_interface.get_max_lun`) invites other callers to trust the result.
- **Benefit:** The LUN contract becomes deterministic; a future caller cannot inherit a stack-garbage count.
- **Effort:** Trivial. **Risk:** None for current behaviour.
- **Confidence:** High on the defect (READ); low on present-day impact (caller behaviour verified in `scsi.c`).

### R4-2B-14 — mx4sio's DMA ISRs busy-spin on the SIO2 status bit with no bound — inside interrupt context
- **Category:** 8 (busy-spin on the IOP)
- **Path:** `modules/mx4sio_bd/src/mx4sio.c:39-40, 57, 97-98`
- **Problem + mechanism (READ):**
  ```c
  /* ISR triggered by the completion of DMA transfer */
  int mx_sio2_dma_isr_rx(void *arg) {
      ...
      while ((inl_sio2_stat6c_get() & (1 << 12)) == 0)
          ;                                        // :39-40 — unbounded, in an ISR
      ...
      cmd.response = mx_sio2_wait_equal(0xFE, READ_TOKEN_TIMEOUT);  // :57 — a per-byte polling
                                                   // routine, also called from this ISR
  }
  int mx_sio2_dma_isr_tx(void *arg) {
      ...
      while ((inl_sio2_stat6c_get() & (1 << 12)) == 0)
          ;                                        // :97-98 — same pattern
  ```
  If the card is pulled or wedges mid-DMA so bit 12 never sets, the IOP never leaves the interrupt handler: no threads run, no other interrupts of lower/equal priority are serviced, and the console hard-hangs with no recovery path (the card-detect thread that might notice removal can never execute).
- **Fix in words:** Give the in-ISR waits a bounded iteration budget; on exhaustion, set `cmd.abort`, signal completion through the existing event-flag path, and return — let thread context own any longer waiting.
- **Justification if ADD:** A counter in two ISRs; justified by a total-system hang, and LESS than moving the PIO-completion wait out of the ISRs (the larger refactor).
- **Benefit:** A dying/removed SD card fails the transfer instead of freezing the console.
- **Effort:** Small-medium (must preserve the abort/event-flag handshake both ISRs already use). **Risk:** Medium — ISR timing is delicate; the budget must comfortably exceed a healthy PIO completion.
- **Confidence:** High (READ — no bound exists; ISR context is explicit in the registration at :196-206).

### R4-2B-15 — The same unbounded SIO2 status spin is repeated in all seven thread-context PIO helpers
- **Category:** 8 (busy-spin on the IOP); also 6-adjacent (the pattern is open-coded seven times)
- **Path:** `modules/mx4sio_bd/src/mx4sio.c:254, 293, 333, 375, 417, 464, 525`
- **Problem + mechanism (READ):** Every byte/block PIO primitive ends its transfer with the identical unbounded poll:
  ```c
  while ((inl_sio2_stat6c_get() & (1 << 12)) == 0)
      ;
  ```
  in `mx_sio2_write_byte` (:254), `mx_sio2_write_dummy` (:293), `mx_sio2_wait_equal` (:333), `mx_sio2_wait_not_equal` (:375), `mx_sio2_wait_equal_masked` (:417), `mx_sio2_rx_pio` (:464, per 256-byte chunk) and `mx_sio2_tx_pio` (:525, per chunk). The `wait_*` functions do have outer byte-count budgets — but a single stuck byte defeats them, because the inner poll never returns. These run in thread context (SD driver) while holding the SIO2 lock taken through the mx4sio/spi path, so one stuck transfer also blocks memory-card (mcemu) and pad access that shares SIO2, indefinitely.
- **Fix in words:** Factor the poll into one bounded wait helper with an error return, and have the seven sites propagate that error the way `mx_sio2_wait_equal` already propagates timeouts to its callers.
- **Justification if ADD:** ADD-neutral to LESS: one helper replaces seven open-coded loops, and the timeout idiom already exists in this file.
- **Benefit:** Card removal mid-command becomes a recoverable SPI error (the driver above already handles those) instead of a permanent SIO2 lock-out.
- **Effort:** Medium (seven call sites, all in one file). **Risk:** Low-medium — timeouts must exceed worst-case healthy SIO2 completion at the slow baud setting.
- **Confidence:** High (READ — seven identical unbounded loops).

---

## Checked and clean (dissolved candidates, with reasoning)

**smb_fio.c**
- **RIPTOPL-040 sites (prepare_path :297-327, smb_dopen :660-663, smb_chdir termination)** — excluded by brief; not re-examined as findings.
- **Negative `size` into smb_read/smb_write (:424-474)** — dissolved: verified in `smb.c` that `smb_ReadFile`/`smb_WriteFile` loop on `while (remaining > 0)` and return the negative count without transferring; a negative size is a no-op error return, not an overflow. (The negative *position* route is real and is filed as R4-2B-03.)
- **FileTimeToDate (:573-618)** — traced end-to-end: the `days -= leapdays` step looks like an underflow but is the correct remainder correction (the 365-day-year division leaves the accumulated leap days in the remainder; subtracting them recovers the day-of-year). No finding for sane or zero FILETIME values.
- **sprintf sites in GetShareList/OpenShare (:970, :1036)** — dissolved: `tree_str[64]` receives at most `\\` + ServerIP (char[16]) + `\IPC$` + NUL = 23 bytes; `tree_str[274]` receives at most 2 + 15 + 1 + 255 (ShareName char[256]) + NUL = exactly 274. Both fit for terminated inputs.
- **CSW/share handling in GetShareList loop (:986-1002)** — the per-index re-enumeration returns on the first error; `shareindex` is bounded by the caller's `maxent` before any DMA (the unchecked-ness of `maxent` itself is R4-2B-05, the loop discipline is fine).
- **Keepalive thread (:186-224)** — echoes and re-logons are all taken under `smbman_io_sema`; no path-buffer use; no spin (blocks on semaphores). Clean.
- **smb_closeAll (:403-415)** — called only from logoff/close-share paths that already hold the I/O lock via devctl; handles it leaves behind are fenced by `TID = -1` checks in every other entry point. Clean within this file.

**mcemu.c**
- **RIPTOPL-051 sites (MceRead :845, MceWrite :896 dead guards)** — excluded by brief.
- **MceEraseBlock BDM chunk loop (:779-789)** — `n` is clamped to both the remaining block pages and `sizeof(erase_buf)/0x200` before every `DeviceWritePages`; the chunking arithmetic itself is sound. (The unclamped *start page* is R4-2B-08.)
- **SioResponse (:745-763)** — refuses `length < 2` and writes exactly `length` bytes; the one response helper that does honour its length. Clean.
- **Port indexing in hookSio2man / Sio2McEmu (:448, :590)** — card index is masked (`ctrl & 0x1`, `*pctl & 0x1`) before indexing `memcards[2]` in both the hook and the emulator; no out-of-bounds card slot. Clean.
- **Inactive-card branch (:665-668)** — `mips_memset(rdma, 0xFF, sd->rdwords * 4)` is sized by the packet's own declared read length, consistent with the buffer the caller provided for that packet. Clean.
- **MceRead/MceWrite page-cap memcpy (:853-856, :900-903)** — given a valid PageSize, `size` is capped to `PageSize - *doff` before every copy, and the multi-page restart recomputes the remainder from `tot_size`. The cap is only as trustworthy as PageSize itself, which is R4-2B-07; no second finding here.

**usb_mass.c**
- **CBW command copy (:484, `memcpy(ucmd.cbw.comData, cmd, cmd_len)` into `comData[16]`)** — dissolved as a live bug: the function has no local guard, but every in-tree caller was checked (`modules/usb/usbmass_bd/src/scsi.c:84, 136, 156, 174`) and passes 12- or 16-byte command arrays, never more than the 16-byte field. Noted here so a future caller with a longer CDB re-opens it.
- **CSW validation (:266-323)** — a short CSW (0 < returnSize ≠ 13) explicitly invalidates the signature, a zero-length CSW triggers one halt-clear + retry, and `usb_bulk_manage_status` only accepts matching signature **and** tag before trusting `status`. The pre-seeded signature/tag in `usb_bulk_status` are overwritten by the 13-byte read or invalidated. Clean.
- **Static `ucmd`/`tag` in usb_queue_cmd (:461-492)** — shared state, but the non-ASYNC path holds `usb_mass_cmd_sema` across the whole command (the in-code comment states exactly this rationale), and the semaphore is created with initial count 1. Clean as compiled.
- **`#else` ASYNC implementation (:545-578) and `scsi_cmd_callback`** — not dead code by neglect: it is the compile-time alternative behind the commented `// #define ASYNC` at :17. Not filed as Cat 5.
- **usb_mass_update DelayThread(5000) (:832)** — a deliberate settle delay after `scsi_connect`, matching the cited usbhdfsd practice; not a spin.

**mx4sio.c**
- **RIPTOPL-049 (sio2man_hook.c / ioplib.c duplication vs mmceman)** — excluded by brief; mx4sio.c itself contains no second copy of those helpers.
- **`cmd.crc[cmd.sectors_transferred]` indexing in the RX ISR (:44-45)** — dissolved as compiled: `CONFIG_USE_CRC16` is not defined (`mx4sio.h`: `// #define CONFIG_USE_CRC16`), so those lines are preprocessed out and the `#else` branch discards the two bytes. Would need re-checking if the option is ever enabled (array is `crc[512]` in `mx4sio.h`).
- **wait_\* outer loops (:315-445)** — the byte-attempt counts (`count`, `READ_TOKEN_TIMEOUT`, 10, 0x80000) are all bounded; only the inner status poll is unbounded, and that is R4-2B-15, not a second finding per function.
- **reverse_buffer (:702-725)** — `int i` vs `uint32_t count` mismatch is unreachable at in-tree call magnitudes (sector/4-scale counts from the SPI driver); no in-file call site passes a large count. Clean at this pin.
- **sd_detect_thread (:668-681)** — 1-second `DelayThread` poll with a `used`-flag skip; a sleep loop, not a busy-spin. Clean.
- **module_start rx_pio into `&rv` (:785)** — reads exactly 4 bytes into a 4-byte int; size-matched. Clean.

---

## Coverage statement

Read end-to-end at pin `2e347d5ac885`, no ranges skipped:

| File | Lines read | Notes |
|---|---|---|
| `modules/network/smbman-ra/smb_fio.c` | 1–1130 (all) | Remainder scope: RIPTOPL-040 sites (prepare_path, dopen termination, chdir termination) read but not re-filed. |
| `modules/mcemu/mcemu.c` | 1–931 (all) | Remainder scope: RIPTOPL-051 sites (MceRead/MceWrite dead guards) read but not re-filed. |
| `modules/usb/usbmass_bd/src/usb_mass.c` | 1–901 (all) | Fresh, first line-read. |
| `modules/mx4sio_bd/src/mx4sio.c` | 1–847 (all) | Fresh, first line-read; RIPTOPL-049 covers sibling files only. |

Supporting files consulted only to verify mechanisms (not audited as sections): `modules/network/smbman-ra/smb.h`, `smb.c` (ReadFile/WriteFile/Echo/FindFirst loops), `tests/smb2-live/inc-sdk/ps2smb.h`, `tests/smb2-live/inc-browse/iomanX.h`, `modules/mcemu/mcemu.h`, `mcemu_var.c`, `mcemu_io.c`, `mcemu_utils.h`, `modules/usb/usbmass_bd/src/scsi.c`, `modules/mx4sio_bd/src/mx4sio.h`, `spi_sdcard_driver.c` (call-site magnitudes only). Every cross-file dependence is marked INFERRED in the finding it supports. Nothing was built, run, or tested on hardware.
