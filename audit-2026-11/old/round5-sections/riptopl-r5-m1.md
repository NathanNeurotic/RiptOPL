# RiptOPL — Round 5, Section R5-M1: cdvdman (cdvd / device backends) + cdvdfsv

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` · READ ONLY · All files read 1..EOF (READ unless marked INFERRED). No PS2 build/hardware in this VM; performance magnitudes are structural/INFERRED.

## Coverage ledger

| File | Lines |
|---|---|
| modules/iopcore/cdvdman/smb.c | 1,177 |
| modules/iopcore/cdvdman/dev9.c | 658 |
| modules/iopcore/cdvdman/ioops.c | 647 |
| modules/iopcore/cdvdman/streaming.c | 455 |
| modules/iopcore/cdvdman/ioplib_util.c | 392 |
| modules/iopcore/cdvdman/nfs.c | 364 |
| modules/iopcore/cdvdman/device-bdm.c | 310 |
| modules/iopcore/cdvdman/smb2auth.c | 307 |
| modules/iopcore/cdvdman/device-mmce.c | 272 |
| modules/iopcore/cdvdman/scmd.c | 268 |
| modules/iopcore/cdvdman/device-smb.c | 252 |
| modules/iopcore/cdvdman/device-udpfs.c | 244 |
| modules/iopcore/cdvdman/heapdiag.c | 179 |
| modules/iopcore/cdvdman/device-http.c | 170 |
| modules/iopcore/cdvdman/device-hdd.c | 150 |
| modules/iopcore/cdvdman/dev9-pccf.c | 19 |
| modules/iopcore/cdvdfsv/ncmd.c | 451 |
| modules/iopcore/cdvdfsv/cdvdfsv.c | 288 |
| modules/iopcore/cdvdfsv/scmd.c | 186 |
| modules/iopcore/cdvdfsv/searchfile.c | 81 |
| **Total** | **6,870 (20 files)** |

Supporting checks (grep, READ): `HDL_NUM_PART_SPECS`/APA header struct in `modules/iopcore/cdvdman/atad.h`; repo-wide uses of `g_bd_sectors_per_sector`, `bdm_readSector`/`bdm_writeSector`, `USE_CUSTOM_RECV`, `SpdRegisterIntrHandler`/`dev9Register*DmaCb`, `smb2auth_hmac_init`; `SEEK_END` siblings (`modules/isofs/isofs.c`, `modules/network/smbman-ra/smb_fio.c`); cdvdman `Makefile` debug defines; BDM `sectorSize` producers (`modules/ilink/IEEE1394_bd/src/scsi.c`, `modules/hdd/atad/src/ps2atad.c`, `modules/network/smap_udpbd/src/udpbd.c`, `modules/bdm/libbdm/src/bd_cache.c`).

---

## New candidate findings

### R5-M1-01 / HDD device trusts the on-disc APA header's partition count — stored truncated into a `u8`, then used to walk a 65-entry table
- **Category:** 6 (bugs/security)
- **Locations:** `modules/iopcore/cdvdman/device-hdd.c:20` (`static unsigned char NumParts;`), `:75` (bounded `memcpy` of the specs table), `:80` (`NumParts = apaHeader.num_partitions;`), `:35` (walk loop), `:118-124` (`DeviceReadSectors` indexing `cdvdman_partspecs[CurrentPart]`); source struct `modules/iopcore/cdvdman/atad.h:96-97` (`int num_partitions; hdl_partspecs_t part_specs[HDL_NUM_PART_SPECS]` with `HDL_NUM_PART_SPECS` = 65, `atad.h:84`).
- **Problem:** The APA header is read raw from the HDD (`:70-73`) and its `num_partitions` is taken verbatim. The copy into `cdvdman_partspecs[65]` is safe — the source field is itself a 65-entry array — but the count is not: (a) any value 66–255 makes `cdvdman_get_part_specs` walk past the 65-entry static table into adjacent BSS, comparing attacker/corruption-controlled "partitions"; a match also sets `CurrentPart` to that out-of-range index, which `DeviceReadSectors` then indexes directly; (b) the destination is `unsigned char`, so a header value of e.g. 256 truncates to 0 (every read then fails with `-ENXIO`/TRMOPN) and 321 truncates to 65 — the effective count silently depends on the value mod 256. A corrupt or crafted HDD image (HDL-format discs are routinely built by PC tools) is the input; no validation exists anywhere in this file.
- **Fix:** Clamp/reject at the single point of ingestion in `DeviceInit`: if `apaHeader.num_partitions` is outside `1..HDL_NUM_PART_SPECS`, treat the header as invalid instead of storing the count; store into a type at least as wide as the source field. Justification: one comparison at ingestion removes both the OOB walk and the truncation aliasing for every consumer; no new mechanism.
- **Benefit:** Removes an out-of-bounds read (and a wrong-partition read) driven by on-disc data in the in-game HDD path.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M1-02 / HDD `DeviceInit` retries the APA-header DMA forever — the one unbounded init wait in a directory where every sibling wait is bounded on purpose
- **Category:** 5 (performance/hang)
- **Locations:** `modules/iopcore/cdvdman/device-hdd.c:70-73`.
- **Problem:** `while ((r = sceAtaDmaTransfer(...)) != 0) { DPRINTF(...); DelayThread(2000); }` has no attempt bound and no failure exit. A drive/ATAD state that never returns 0 (damaged drive, wedged bus, the PIO-dead variant interaction space covered by RIPTOPL-068/138 in `atad.c`) hangs IOP initialisation silently and permanently. The neighbouring device backends in the same directory all bound exactly this wait and say why in comments: `device-mmce.c` (100 × 100 ms, then fail-fast reads), `device-udpfs.c` (300 × 100 ms, then fail-fast), `device-http.c` (50 × 10 ms). The HDD backend is the outlier.
- **Fix:** Bound the loop the same way its siblings are bounded (fixed attempt budget, then proceed to the fail-fast state / return so reads error visibly). Justification: reuses the established in-directory pattern; adds one counter, removes an infinite hang.
- **Benefit:** A dead HDD path surfaces as read errors instead of an undiagnosable freeze at game start.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M1-03 / `cdrom_read` retries a failed `sceCdRead` forever, in all three phases, while holding the I/O semaphore
- **Category:** 5 (performance/hang)
- **Locations:** `modules/iopcore/cdvdman/ioops.c:305-306`, `:321-322`, `:334-335` (three identical `while (sceCdRead(...) == 0) DelayThread(10000);` loops inside `cdrom_read`).
- **Problem:** If the underlying read keeps failing (device error, removed/failed backend), each phase spins indefinitely with the `cdrom_io_sema` held, so the game's filesystem thread — and every other cdrom op queued on the same semaphore — hangs with no error ever returned. The sibling EE-read path in `modules/iopcore/cdvdfsv/ncmd.c` (`cdvd_readee`) treats `sceCdRead(...) == 0` as a failure: it records `SCECdErREADCF` and returns. The two halves of the same disc-read subsystem disagree about what a 0 return means, and the ioops half's interpretation has no exit.
- **Fix:** Give each retry loop a bounded attempt count and fall through to the same error treatment `ncmd.c` already uses (set the read error, return the bytes read so far / error). Justification: aligns this file with the in-subsystem precedent; adds a counter, removes an infinite semaphore-holding spin.
- **Benefit:** Permanent read failures become errors a game can surface instead of a total filesystem freeze.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M1-04 / cdvdfsv's `sysmemSendEE` busy-spins twice with no yield and no bound — the pattern streaming.c was fixed for, one module over
- **Category:** 5 (performance)
- **Locations:** `modules/iopcore/cdvdfsv/cdvdfsv.c:253-271` (`sysmemSendEE`): the `sceSifSetDma` retry `do { suspend; id = sceSifSetDma(...); resume; } while (!id);` at `:260-267`, and the empty completion spin `while (sceSifDmaStat(id) >= 0);` at `:269-270`.
- **Problem:** Neither wait yields, sleeps, or times out. Every cdvdfsv reply path funnels through this function (readee completions, readchain progress, searchfile results), so a full SIF DMA tag queue or a stuck transfer stalls the RPC thread in a tight loop. The same hazard is diagnosed in detail in the in-tree comment at `modules/iopcore/cdvdman/streaming.c:215-232`, where the equivalent `sceSifSetDma` loop was rewritten to `DelayThread(500)` between attempts precisely because spinning starves the IOP; the completion spin in streaming.c is already filed separately as RIPTOPL-045 — this cdvdfsv pair is a different file and was not covered by it.
- **Fix:** Wait the way streaming.c now does: yield/sleep inside the `sceSifSetDma` retry, and sleep inside the `sceSifDmaStat` wait. Justification: copies the maintainers' own fixed pattern from the sibling module; no new mechanism.
- **Benefit:** Removes two unbounded spins from the EE↔IOP reply path every disc RPC depends on.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M1-05 / BDM device computes `g_bd_sectors_per_sector` on every attach and never reads it — disc reads hardcode ×4 (512-byte sectors) instead
- **Category:** 1 (dead code) with a Category 6 consequence
- **Locations:** `modules/iopcore/cdvdman/device-bdm.c:22` (declaration, initialised 4), `:106` (the only write, `(2048 / bd->sectorSize)` in `bdm_connect_bd`), `:234` and `:244` (`DeviceReadSectors` calling `bd_defrag(..., lsn * 4, ..., sectors * 4)` / bounce count `4`).
- **Problem:** A repo-wide grep finds exactly two references to `g_bd_sectors_per_sector`, both in this file: declaration and assignment. It is write-only state. Meanwhile the read path converts CD sectors (2048 B) to device sectors with a literal `* 4`, which is only correct when `bd->sectorSize == 512`. That premise is not universal in-tree: `IEEE1394_bd` takes `sectorSize` from the device's block length (`modules/ilink/IEEE1394_bd/src/scsi.c:264-282`), the ATA BDM driver can report an ATA logical sector size (`modules/hdd/atad/src/ps2atad.c:1407`), UDPBD takes it from the server (`modules/network/smap_udpbd/src/udpbd.c:311`), and `bd_cache.c:210` explicitly special-cases `sectorSize != 512`. So the variable exists because the conversion is device-dependent, the conversion ignores it, and the two silently disagree exactly in the cases the variable was computed for. (Whether a non-512 device currently reaches this cdvdman variant is INFERRED — the dead state and the hardcoded conversion are READ.)
- **Fix:** Choose one truth: either use `g_bd_sectors_per_sector` in `DeviceReadSectors` in place of the literal 4s, or delete the variable and state the 512-only assumption where the device is matched (the PCCF branch at `:103` already validates `sectorSize == 512` explicitly, which is the honest form of the assumption). Deleting the variable is the LESS-NOT-MORE default if 512 is in fact guaranteed for every attachable device.
- **Benefit:** Removes dead per-attach state and the trap where a future non-512 device reads at 4×/¼× the correct LBA.
- **Effort:** S · **Risk:** Low · **Confidence:** High for dead/inconsistent (READ), Medium for live device impact (INFERRED)

### R5-M1-06 / BDM VMC exports dereference `g_bd` with no NULL check — the only unguarded `g_bd` uses in the file
- **Category:** 6 (bugs/security)
- **Locations:** `modules/iopcore/cdvdman/device-bdm.c:279` (`bdm_readSector`: `rv = g_bd->read(...)`), `:302` (`bdm_writeSector`: `rv = g_bd->write(...)`); contrast `:229-230` (`DeviceReadSectors` returns `SCECdErTRMOPN` when `g_bd == NULL`), `:192`, `:200-208`, `:183-197` (all other uses guarded).
- **Problem:** `g_bd` is set to NULL by `bdm_disconnect_bd` and starts NULL. mcemu reaches these two functions through the oplutils imports (`modules/mcemu/mcemu_utils.h:44-48`) as its VMC page backend, and their documented contract (file comment at `:259-266`) is "return 1 on full transfer, 0 otherwise" so mcemu can report a failed page instead of acknowledging a lost save. A call made before attach or after a disconnect instead dereferences a NULL function pointer and crashes the IOP — the failure mode the contract exists to avoid. Every other `g_bd` consumer in the same file guards; these two are the exception.
- **Fix:** Return 0 when `g_bd == NULL` at the top of both functions (before taking the semaphore is fine; the disconnect path nulls it under the same semaphore discipline the functions already use). Justification: one comparison per function, reusing the guard idiom the file already uses at `:229`.
- **Benefit:** A VMC access racing a device detach fails as a reported page error instead of an IOP crash.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M1-07 / SMB1 `smb_ReadAndX` carries a permanently dead second receive implementation (`USE_CUSTOM_RECV` is unconditionally defined), plus the `rhdrlen` plumbing that only it used
- **Category:** 1 (dead code)
- **Locations:** `modules/iopcore/cdvdman/smb.c:23` (`#define USE_CUSTOM_RECV 1`, unconditional — repo-wide grep finds no other definition, no `#undef`, no Makefile flag), `:920-922` (`int padding;` declared only for the dead branch), `:997-1020` (the `#else` body of `smb_ReadAndX`), `:376` (`GetSMBServerReply(..., int rhdrlen)`) whose only non-zero caller is the dead branch at `:998` (all seven live callers — `:509, :628, :703, :787, :850, :1091` — pass 0).
- **Problem:** ~24 lines implement the pre-custom receive path (reply via `GetSMBServerReply` + separate padding/data `RecvData` calls) that can never compile in any in-repo build. It is also the *unhardened* twin of the live path: it trusts the response's `DataLength` with none of the live branch's DataOffset/DataLength/reply-size validation (the hardening the live branch's long comments document). Dead code that looks like a plausible receive path — and is the less safe of the two — is exactly the copy a future editor can resurrect by deleting one `#define`.
- **Fix:** Delete the `#else` branch, the `#ifdef` scaffolding in `smb_ReadAndX`, and the now-constant `rhdrlen` parameter of `GetSMBServerReply` (with its `size = (rhdrlen == 0) ? ... : rhdrlen` branch). Pure deletion; the live path is untouched.
- **Benefit:** One receive implementation to audit; the unvalidated variant stops existing as a one-line resurrection away.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M1-08 / dev9.c's PCMCIA probe trio (~55 lines) is behind a plain `DEBUG` define that no in-repo build of this module ever sets
- **Category:** 1 (dead code)
- **Locations:** `modules/iopcore/cdvdman/dev9.c:77-78` (prototype), `:460-512` (`pcic_get_cardtype`, `pcic_get_voltage`, `pcmcia_device_probe`), `:563-565` (sole call site, also `#ifdef DEBUG`).
- **Problem:** The three functions compile only under `#ifdef DEBUG`. This file is built solely as `dev9.o` by `modules/iopcore/cdvdman/Makefile:99`, and that Makefile's debug switches define `DEV9_DEBUG` (`:59`) and `__IOPCORE_DEBUG` (`:95`) — never plain `DEBUG` (the one `-DDEBUG` in the tree, `modules/iopcore/cdvdfsv/Makefile:7`, belongs to a different module that does not compile this file). So in every build this repo can produce, the probe trio and its call site are preprocessed away, while still being maintained as if live (this is vendored PS2SDK heritage code, which explains, but does not un-dead, it).
- **Fix:** Delete the three `#ifdef DEBUG` blocks, or fold the probe under the module's actual `DEV9_DEBUG` switch if maintainers want it reachable. Deletion is the LESS-NOT-MORE default.
- **Benefit:** ~55 fewer lines of never-compiled code in a hardware driver that is otherwise read as authoritative.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the in-repo build claim (READ — Makefile grep); the functions could only revive under an out-of-repo build system defining `DEBUG`.

### R5-M1-09 / Streaming ring-buffer drain is written twice: `ReadSectors` (memcpy) and `ReadSectorsEE` (SIF DMA) are one algorithm with two bodies
- **Category:** 2 (duplication)
- **Locations:** `modules/iopcore/cdvdman/streaming.c:162-245` (`ReadSectorsEE`) and `:247-301` (`ReadSectors`); both called only from `sceCdStRead` (`:411-455`) depending on the buffer's EE/IOP bit.
- **Problem:** Both functions implement the identical two-segment ring drain — wrap segment when `StWritePtr <= StReadPtr && StStreamed > 0`, then linear segment when `StReadPtr < StWritePtr`, with the same clamping (`min(segment, maxcount - result)`), the same pointer wrap-to-0, and the same `StStreamed` accounting. They differ only in the transfer step (in-place `memcpy` under suspended interrupts vs building ≤2 `SifDmaTransfer_t` entries and doing the DMA bookkeeping after). Every fix to the ring arithmetic must be made twice, in bodies long enough that a one-sided fix is easy to miss — the file's own history (RIPTOPL-045 and the #185 fill-restart fix) is a sequence of exactly such single-spot stream fixes.
- **Fix:** Factor the shared part — computing the (up to) two segment descriptors {src, length} and applying the resulting read-pointer/streamed update — into one helper, leaving each function only its transfer step (DMA submission for EE, `memcpy` for IOP). Justification: the helper *replaces* two copies of the arithmetic; it is a net deletion of ~40 lines, not a new abstraction layer.
- **Benefit:** Ring-buffer correctness lives in one place; the EE and IOP stream paths cannot drift apart.
- **Effort:** M · **Risk:** Medium (interrupt-context code; the EE variant's post-DMA state update ordering must be preserved exactly) · **Confidence:** High (READ)

---

## Already filed — not repeated

- **RIPTOPL-045** — `streaming.c` streaming DMA wait empty busy-spin: still present at the pin at `modules/iopcore/cdvdman/streaming.c:234-235` (inside `ReadSectorsEE`). Not re-filed; R5-M1-04 is the distinct cdvdfsv instance.
- **RIPTOPL-046** — two complete SMB1 stacks (`cdvdman/smb.c` vs `smbman-ra/smb.c`, 19 shared functions). `smb.c` was re-read in full per the brief; the new substance found is filed as R5-M1-07 only. The round-2 clearance of this file's receive path was re-verified (see Checked and clean).
- **RIPTOPL-041** — the `.PSS` short-name OOB is in `modules/iopcore/cdvdman/searchfile.c`, a *different* file from this section's `modules/iopcore/cdvdfsv/searchfile.c` (81 lines, RPC dispatch only). No equivalent defect exists in the cdvdfsv file (see Checked and clean).
- **RIPTOPL-028** — HTTP as a game source (feature-level, `device-http.c` is its cdvdman leg). The file was re-read; no new code-level substance beyond that finding.
- Round-2 cleared, re-verified, not re-filed: `device-smb.c` `sprintf` sites (`:115-138`) with the computed 215-of-256 bound; `device-http.c`'s `RecvBounded`/`SendData` are live via the `httpstream.inc` inclusion (dead-code sweep negative).

## Checked and clean (dissolved candidates, one line each)

- `smb.c` live receive path — NBSS length is validated against the fixed `SMB_buf` and the SMB header minimum before any receive (`GetSMBServerReply`), and ReadAndX validates status, `DataOffset`, `DataLength` vs both the request and the received reply size, draining refused replies so the stream stays in step; the round-2 clearance stands.
- `smb.c` negotiate/session setup — server-supplied domain length is clamped to the destination before `memcpy`; username/password are `strncpy`'d with explicit termination; the LM-fallback `goto` re-zeroes the request packet first.
- `smb2auth.c` HMAC long-key handling truncates keys >64 B instead of hashing them (RFC 2104 deviation) — dissolved: every in-tree caller passes a 16-byte key (verified by grep across `smb2.c`, `smb2man.c`, tests), so the branch is unreachable as built.
- `smb2auth.c` SHA-256 `static u32 w[64]` — deliberate, documented stack-saving; safe because smb2 serialises all requests. MD4/MD5/SHA-256 constants and padding (incl. the `used > 56` two-block path) read correctly.
- `nfs.c` `nfs_sock_read` — the `us` variable is always assigned (100 µs/1 ms) before the loop increment can consume it, because a successful receive returns first; 10 s total budget is real. Path conversion in `smb_OpenAndX` bounds-checks against `CDVDMAN_SMB_PATH_MAX` before the semaphore is taken (no leak); the unchecked `bind()` result is a documented fallback (privileged port "if one can be bound"); `malloc → NULL` macroing of unfs4 is fenced to functions the file declares static and never calls.
- `device-smb.c` `DeviceReadSectors` — part-walk returns success with nothing read if `lsn` is beyond the last part; not filed: LSN bounding against disc size is enforced by the cdvdman core/isofs layers outside this section's files, and no in-file caller can produce it. The ZSO short-read zero-padding check (`got <= want - 2048`) is arithmetically sound since `want ≥ 2048` for any non-zero sector count.
- `device-udpfs.c` — retry accounting (`attempt = 3` sentinel), 512-alignment and zero-size rejection in the VMC path, and the word-alignment bounce logic all read correctly; `(u32)lsn` truncation is unreachable at disc sizes the format supports.
- `device-mmce.c` — the `int64_t` get-size declaration, bounded 10 s init, NULL-export guard, fail-fast/lost latching, and last-attempt (not counter) result judgement all read correctly; this file is the standard the R5-M1-02 fix points at.
- `device-http.c` — `DeviceReadSectors` validates `lsn`/`sectors` against the settings image size (incl. the `0x7fffffff/2048` overflow guard) before any request; the 10 s `RecvBounded` budget covers the whole response via the per-attempt `readStart` reset; `DeviceFSInit` wait is bounded at 500 ms.
- `heapdiag.c` — the stale-record wipe strides 16 bytes (`p += 4` on a `u32 *`) by design: the record struct is `aligned(16)`, so only 16-byte-aligned slots can hold a copy; free-of-untracked/NULL accounting quirks affect only the diagnostic counters, in an INGAMEDIAG-only file.
- `ioplib_util.c` — `hookMODLOAD` ignores `getModInfo`'s return, unlike the guarded callers in this section, but modload is necessarily resident (it loaded cdvdman), so the uninitialised-exports hazard the sibling files guard against cannot occur here; `Hook_AllocSysMemory`'s bound (`size <= 0x7FFFFFFF - (1792 + 256)`) exactly matches the maximum pad (2048); fake-module `strcpy` targets are short in-file constants into a 56-byte field.
- `ioops.c` (remainder) — `cdrom_purifyPath` is capacity-correct in all three branches; `cdrom_dread` validates record length against both the 33-byte minimum-plus-name and the sector end before copying the name; `cdrom_lseek`'s `SEEK_END` as `filesize - offset` matches the sibling implementation in `modules/isofs/isofs.c` exactly (shared SCE-derived convention, not a local deviation).
- `cdvdman/scmd.c` — `sceCdRI` builds its 9-byte reply in a 16-byte buffer in both branches; `sceCdRM` copies exactly the 16 bytes its literal holds; `sceCdRC` vs `sceCdReadClock` are two ABI entries differing by the BCD masking, not a mergeable duplicate.
- `cdvdfsv/scmd.c` — unimplemented enum commands fall to the default error return; `cdapplySCmd_t`'s pointer field and the fixed 16-byte ApplySCmd output are the libcdvd RPC protocol, written into a 1,024-byte RPC buffer.
- `cdvdfsv/searchfile.c` — size dispatch (292/296/300-byte packet generations) cannot overrun: the RPC buffer is 304 bytes, names stay inside the received packet structs, and `pktsize` sent back to EE is header-only in all branches.
- `cdvdfsv/ncmd.c` — `cdvd_readee` alignment arithmetic verified by hand (b1len = 64 − (addr mod 64) ∈ 1..63 fits `buf1[64]`; head + middle + tail account for exactly `nbytes`); `cdvd_readchain`'s entry-65 read-position pointer and `0xFFFFFFFF` terminators are the EE protocol, with the 64-entry loop inside the 1,024-byte buffer; `cdvdSt_read` breaks before the for-loop increment can consume a failed read count.
- `streaming.c` (remainder) — `sceCdStInit` divides by game-supplied `bankmax` without a zero check; not filed: values come from the game's own libcdvd streaming setup (trusted, always non-zero in the API's contract), and the maintainers' #185 rework of this file left it by design. `StmScheduleCb`'s `(>= 0) ? 0 : 0x00704000` return is the alarm re-arm convention, not a typo.
- `dev9.c` (remainder) — `SpdDmaTransfer`'s channel spin carries an explicit throughput rationale in its comment and waits on a hardware completion bit; `SpdRegisterIntrHandler`/`dev9Register*DmaCb` index their tables unchecked, but every in-tree caller passes a constant (0, 1, 5, 6) and IOP modules are trusted code, not data — dissolved; `Dev9RegisterPowerOffHandler` in the same file does bounds-check its index.
- `dev9-pccf.c` — three hardware-inert stubs returning NODEV by design (the PCCF backend owns the bus); nothing to simplify without breaking the ATAD/DEV9 import shape.
- `device-bdm.c` (remainder) — `bdm_disconnect_bd`, `DeviceStop`, and the PCCF `DeviceDeinit` all NULL-guard `g_bd` (the contrast that makes R5-M1-06 a local inconsistency, not a file-wide pattern); the iLink dual-token matching is thoroughly justified in its comment and widens nothing for other drivers.

## Forced revisions to prior findings

None. RIPTOPL-045's spin is confirmed still present at the pin (same lines, `streaming.c:234-235`); no prior finding in this section's scope was contradicted by the full re-read.
