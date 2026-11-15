# RiptOPL Round 9 — modules/iopcore/ full line-read

- Repo: NathanNeurotic/RiptOPL clone `~/workspace/audit-clones-2026-11-13/RiptOPL`
- Pin verified at start and end of read: `2e347d5ac885cd0ff73e9ad4143781946396f492` (HEAD matches task pin; clone left git-clean, no repo edits)
- Scope: every `.c/.h/.S/.s` under `modules/iopcore/` — 63 files, 14,171 lines, all line-read in full (ledger in §C; unread = 0)
- Cross-referenced, NOT re-filed: RIPTOPL-016 (SMB credential strcpy), -041 (`.PSS` short-name index), -068 (atad.c PIO early-return/unreachable body — the same early `return 0` pattern in `cdvdman/atad.c` `ata_pio_transfer`), -138 (`sceAtaInit` returns never-populated `atad_devinfo` in both `atad.c` and `hdpro_atad.c`), -206 (ioops.c `cdrom_read` infinite `sceCdRead` retry, all three phases), -210 (cdvdfsv `sysmemSendEE` busy-spins — note IOPCORE-10 for a third copy of that pattern in igrmbox), -282 (smsutils.S `mips_memcpy` delay-slot — the `cdvdman/smsutils.S` copy read here matches the filed copy; no fifth copy and no divergence found in scope)
- Confidence tags: READ = the cited lines were read and the mechanism is in them; INFERRED = mechanism read, trigger/impact depends on a caller, a build flag, or hardware behaviour outside this scope.

---

## A. Candidates (sorted by benefit)

### IOPCORE-01 — HDPro `sceAtaDmaTransfer` retries a failing transfer forever, holding the ATA semaphore
- **Category:** 6 bugs/security (hang) / 5 performance
- **Locations:** `modules/iopcore/cdvdman/hdpro_atad.c:690-722` (loop; comment at :714 "Unlike ATAD, retry indefinitely until the I/O operation succeeds", `continue` at :717 and :719)
- **Problem:** The chunk loop advances only on success. Any persistent error from `sceAtaExecCmd` or `sceAtaWaitResult` (dead/dying drive, bad sector, bus fault) loops the same chunk forever with no retry cap, no delay, and `ata_io_sema` held for the whole loop, so every other ATA user (game reads, VMC writes via the same semaphore) is blocked permanently. The sibling `atad.c:728-746` caps retries at 3 and only for the recoverable ICRC error.
- **Fix:** Mirror atad.c: bounded retries (3) only for a recoverable error class, then return the error; release the semaphore on every exit path.
- **Justification if additive:** None — deletion of the unbounded retry, reuse of the sibling's existing policy.
- **Benefit:** Removes a permanent, silent IOP-wide HDD hang class on the HDPro build.
- **Effort:** S
- **Risk:** Low — error return replaces an infinite loop; callers already handle `SCECdErREAD`.
- **Confidence:** READ

### IOPCORE-02 — HDD APA header `num_partitions` is trusted past the 65-entry partition table
- **Category:** 6 bugs/security
- **Locations:** `modules/iopcore/cdvdman/device-hdd.c:80` (`NumParts = apaHeader.num_partitions`), `device-hdd.c:30-46` (`cdvdman_get_part_specs` loops `i < NumParts` over `cdvdman_partspecs[]`), table size `modules/iopcore/cdvdman/atad.h:97` (`HDL_NUM_PART_SPECS 65`, array at `device-hdd.c:24`)
- **Problem:** `num_partitions` is an `int` read verbatim from the APA header sector on the HDD (HDLoader-written data, i.e. disc/HDD-supplied, not loader-validated in this file). If it exceeds 65, `cdvdman_get_part_specs` walks `ps++` past the 65×12-byte static table into adjacent BSS and treats whatever it finds as partition offset/size, which then drives real ATA LBAs in `DeviceReadSectors` (`device-hdd.c:110-133`). No clamp exists anywhere in the file.
- **Fix:** Clamp at the assignment: `if (NumParts > HDL_NUM_PART_SPECS) NumParts = HDL_NUM_PART_SPECS;` (and reject `< 0`).
- **Justification if additive:** One comparison; no new state.
- **Benefit:** Closes an out-of-bounds static-table walk feeding hardware LBAs, from crafted/corrupt HDD metadata.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-03 — HDD `DeviceInit` retries the APA-header read forever during module start
- **Category:** 6 bugs (hang) / 8 half-finished (no failure path)
- **Locations:** `modules/iopcore/cdvdman/device-hdd.c:70-73`
- **Problem:** `while ((r = sceAtaDmaTransfer(...)) != 0) { DelayThread(2000); }` has no attempt limit and no exit. `DeviceInit` runs from cdvdman's `_start`, so a failed/absent/misconfigured HDD (or an `lba_start` pointing at unreadable sectors) hangs the IOP during module load with only a debug print — the launch never reaches any error surface. Every other device backend in scope bounds its init wait (MMCE 100 polls, UDPFS 300 polls, HTTP 50 polls, SMB2 3 attempts, NFS 3 attempts).
- **Fix:** Bound the loop (e.g. a fixed attempt count consistent with the sibling backends) and return with `atad_inited`/media left in the failed state so reads fail fast with `SCECdErREAD`.
- **Justification if additive:** None — a bound on an existing loop.
- **Benefit:** A dead HDD becomes a diagnosable launch failure instead of a black-screen IOP hang.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-04 — SCMD path: unbounded hardware-register spins, and `cdvdman_sendSCmd` always reports success
- **Category:** 6 bugs / 5 performance
- **Locations:** `modules/iopcore/cdvdman/cdvdman.c:939-973` (three `do { } while (!(CDVDreg_SDATAIN & 0x40))` drain loops and `while (CDVDreg_SDATAIN & 0x80) ;` at :954, all without timeout or yield, holding `cdvdman_scmdsema`); `cdvdman.c:982-997` (`cdvdman_sendSCmd`)
- **Problem:** (1) If the mechacon/CDVD controller never sets the expected SDATAIN bits, `cdvdman_writeSCmd` spins forever in interrupt-enabled thread context with the SCMD semaphore held — clock reads, power-off, GUID/model reads all wedge. (2) Even when `cdvdman_writeSCmd` does fail (returns 0), `cdvdman_sendSCmd` retries it 2,500 times and then `return 1` unconditionally at :997 — the failure result is discarded. Callers that test the result (`ioops.c` CDIOC_READCLOCK / CDIOC_POWEROFF convert `!= 1` to `-EIO`) can therefore never see an SCMD failure, and `sceCdRI`/`sceCdReadClock` return stack/register garbage as valid data after a failed exchange.
- **Fix:** Give the register waits a bounded poll count (the file already bounds the analogous OHCI reset loop at 1,000 in `cdvdman_usb_host_stop`), and make `cdvdman_sendSCmd` return the final `r` instead of literal 1.
- **Justification if additive:** None.
- **Benefit:** SCMD failure becomes visible and bounded instead of an IOP wedge or silently fabricated clock/GUID data.
- **Effort:** S
- **Risk:** Low-Medium — a game that (incorrectly) relies on power-off "succeeding" on a wedged mechacon would now see an error; no in-scope caller does.
- **Confidence:** READ

### IOPCORE-05 — BDM VMC exports dereference `g_bd` with no NULL check
- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdman/device-bdm.c:268-310` (`bdm_readSector` calls `g_bd->read` at :279; `bdm_writeSector` calls `g_bd->write` at :302)
- **Problem:** `DeviceReadSectors` in the same file checks `g_bd == NULL` and returns `SCECdErTRMOPN` (:236-237), but the two oplutils VMC exports do not check at all. `g_bd` is NULL before `bdm_connect_bd` attaches a device and is reset to NULL by `bdm_disconnect_bd` (:124-128) and by the PCCF `DeviceDeinit` (:175-183). A VMC page access (MCEMU runs from the IOP reset, per the comment at `device-bdm.c:190-193`) in either window is a NULL function-pointer call — an IOP crash, not a failed save. The sibling VMC paths all guard their pointers (MMCE checks `fp_mmcedrv_*` for NULL, UDPFS checks `fp_udpfs_fhi_*` for NULL).
- **Fix:** `if (g_bd == NULL) return 0;` at the top of both exports (0 is the documented "transfer failed" return for MCEMU).
- **Justification if additive:** Two comparisons, matching the file's own existing check.
- **Benefit:** Removes a crash window around device connect/disconnect for every BDM VMC game.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-06 — SMB (USBLD multipart) read past the last part returns success with the buffer untouched
- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdman/device-smb.c:175-224` (`DeviceReadSectors`; `rv` initialised `SCECdErNO` at :182, part loop :191-221)
- **Problem:** For `NumParts > 1` each part covers `0x80000` sectors. If `(u32)lsn` falls outside every part's `[lbound, ubound)` window — a read at or beyond `NumParts * 0x80000` — no iteration body runs, nothing is read, and the function still returns `SCECdErNO`. The direct caller that can reach this without the `mediaLsnCount` gate is the ZSO sector cache (`cdvdman.c` `DeviceReadSectorsCached`), whose prefetch window deliberately reads past the current request and can cross the end of the image; those phantom sectors are then cached and served as real data. Single-part images are unaffected (`ubound = 0xFFFFFFFF`).
- **Fix:** Track whether any part matched; return `SCECdErREAD` when none did (or when the loop ends with sectors still unaccounted for).
- **Justification if additive:** One flag/comparison.
- **Benefit:** End-of-image reads on multipart SMB images stop fabricating sector data.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ (code path); INFERRED (that the ZSO prefetch window is the practical trigger)

### IOPCORE-07 — `sceCdStInit` divides by the game's `bankmax` with no validation
- **Category:** 6 bugs (EE RPC / game-supplied input)
- **Locations:** `modules/iopcore/cdvdman/streaming.c:122-141` (`StBanksize = bufmax / bankmax` at :132; results stored into `unsigned short` fields of `SteamingData`, `internal.h:84-96`)
- **Problem:** `bufmax`/`bankmax` arrive unchecked from the game via CDVDFSV (`cdvdfsv/ncmd.c` CDVD_ST_CMD_INIT) or FILEIO devctl. `bankmax == 0` divides by zero (undefined result on MIPS, no trap to catch it); `bankmax > bufmax` yields `StBanksize == 0`, after which `AllocBank` hands out the same bank forever and `Stlsn` never advances — the stream silently stalls; a quotient over 65535 truncates in the `u16` fields and desynchronises `StBufmax` from the game's real buffer, so stream fills can write outside the IOP stream buffer the game supplied.
- **Fix:** Reject `bankmax == 0 || bufmax == 0 || bankmax > bufmax` (return 0, as the other `sceCdSt*` functions do when not initialised) before the division.
- **Justification if additive:** One guard, no new state.
- **Benefit:** A malformed stream-init from any game becomes a refused call instead of corrupt stream state.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ (missing validation and truncation); INFERRED (exact failure manifestation per input)

### IOPCORE-08 — DEV9 `SpdDmaTransfer` busy-waits forever for DMA completion, semaphore held
- **Category:** 5 performance / 6 bugs (hang)
- **Locations:** `modules/iopcore/cdvdman/dev9.c:269-317` (spin at :307-308: `while (dev9_chan->chcr & DMAC_CHCR_TR) { }`)
- **Problem:** The comment says a semaphore wait "hurts throughput", so completion is polled with an empty loop and no timeout, while holding `dma_lock_sem`. If the DMA channel never clears TR (hardware fault, wrong channel state), the IOP spins at full speed forever and every later DEV9 DMA waits on the semaphore behind it. This is the HDD/SMAP data path for the HDD and network builds.
- **Fix:** Bound the poll (iteration cap consistent with the transfer size) and return an error, releasing the semaphore, on expiry — the post-DMA callback already runs unconditionally after the loop.
- **Justification if additive:** None.
- **Benefit:** A stuck DEV9 DMA becomes a recoverable I/O error instead of a total IOP freeze.
- **Effort:** S
- **Risk:** Low-Medium — cap must exceed the slowest legitimate transfer; size-scaling the cap keeps that margin.
- **Confidence:** READ

### IOPCORE-09 — DEV9 callback-registration exports index their tables with unchecked (and possibly negative) indices
- **Category:** 6 bugs/security
- **Locations:** `modules/iopcore/cdvdman/dev9.c:164-168` (`SpdRegisterIntrHandler`: `dev9_intr_cbs[intr]`, table of 16), `dev9.c:176-186` (`dev9RegisterPreDmaCb`/`dev9RegisterPostDmaCb`: tables of 4), `dev9.c:410-417` (`Dev9RegisterPowerOffHandler`: checks `idx < 16` but not `idx >= 0`)
- **Problem:** These are exported functions callable by any IOP module a game loads. An out-of-range `intr`/`ctrl` writes a function pointer into arbitrary adjacent BSS; a negative power-off `idx` writes before `dev9_shutdown_cbs`. The one registration function that does check demonstrates the intended pattern — it is just incomplete (upper bound only) and the other three have no check at all. All in-scope callers pass constants (0, 1, 15), so this is latent, not currently triggered.
- **Fix:** Add the missing range checks (`0 <= intr < 16`, `0 <= ctrl < 4`, `0 <= idx < 16`) and return/ignore on violation, matching `Dev9RegisterPowerOffHandler`'s existing shape.
- **Justification if additive:** Three comparisons in cold registration paths.
- **Benefit:** Closes a BSS-corruption primitive in an exported registration surface.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-10 — igrmbox `mbSendEE` is a third copy of the filed sysmemSendEE spin pattern — with no yield at all
- **Category:** 2 duplication / 5 performance (cross-reference RIPTOPL-210)
- **Locations:** `modules/iopcore/igrmbox/igrmbox.c:75-88`
- **Problem:** RIPTOPL-210 filed the two unbounded busy-spins in cdvdfsv's `sysmemSendEE`. igrmbox contains the same code again as `mbSendEE`: `do { sceSifSetDma } while (!id);` with interrupts toggled but no delay, followed by `while (sceSifDmaStat(id) >= 0) ;`. While the in-game menu is open this thread is the EE's only mailbox; if the SIF DMA queue stays full or a transfer never completes, the IOP spins here forever instead of answering END/WAKE — the menu can neither close nor resume the game. It is a separate module from CDVDFSV, so a fix applied to -210 alone will not touch it.
- **Fix:** Apply whatever bounded/yielding form -210's fix takes to this copy too (DelayThread between `sceSifSetDma` retries, as `streaming.c` ReadSectorsEE already does at :226-232), and note the copy in -210's fix scope.
- **Justification if additive:** None — same change, second location.
- **Benefit:** The in-game menu's exit path stops sharing CDVDFSV's hang.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-11 — Streaming EE read busy-spins on `sceSifDmaStat` with no bound
- **Category:** 5 performance / 6 bugs (hang)
- **Locations:** `modules/iopcore/cdvdman/streaming.c:234-235` (`while (sceSifDmaStat(dmat_id) >= 0) { };` in `ReadSectorsEE`)
- **Problem:** The retry loop directly above it was deliberately fixed to yield (`DelayThread(500)`, comment citing #185: retrying with interrupts suspended hangs the IOP), but the completion wait that follows is still an empty spin. A DMA that never completes wedges the CDVD thread — and with it every disc read — at full CPU. Same pattern family as RIPTOPL-210 but a distinct site in cdvdman proper.
- **Fix:** Yield inside the wait (DelayThread, as above) and/or bound it and fail the read.
- **Justification if additive:** None.
- **Benefit:** Stream reads degrade to an error instead of an IOP freeze on a stuck SIF DMA.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-12 — Game patch modules write fixed offsets into a target module's text with no size or version check
- **Category:** 6 bugs
- **Locations:** `modules/iopcore/patches/apemodpatch/main.c:105-116` (writes up to `text_start + 0x3d1c`), `modules/iopcore/patches/f2techioppatch/main.c:49-50` (`+0x16bc`), `modules/iopcore/patches/iremsndpatch/main.c:87-108` (reads at `+0xac0/+0xac4`, writes up to `+0x1ff8`)
- **Problem:** All three locate the target module by ID in loadcore's list and immediately dereference hard-coded offsets. None checks `m->text_size` against its largest offset, and none verifies the instruction/word already present at the patch site. A different revision of the target module than the one the patch was written against (the games these target shipped multiple driver revisions) is patched blind: writes land in whatever follows the text in IOP RAM, or overwrite unrelated instructions, with no error path.
- **Fix:** Before patching, require `m->text_size` to exceed the largest offset used, and verify one known original word per patch site (f2tech already records the original value `0x8000` in its comments); abort with the existing "could not find module" style failure otherwise.
- **Justification if additive:** A comparison (and optionally one word compare) per module, in already-existing guard position (`if (m != NULL)`).
- **Benefit:** A mismatched driver revision fails the patch cleanly instead of corrupting IOP memory.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ (absence of any check in all three files)

### IOPCORE-13 — HDPro 48-bit LBA path drops the upper LBA bytes for LCYL/HCYL
- **Category:** 6 bugs / 2 duplication (divergence from sibling)
- **Locations:** `modules/iopcore/cdvdman/hdpro_atad.c:695-697` vs `modules/iopcore/cdvdman/atad.c:680-682`
- **Problem:** In `sceAtaDmaTransfer`'s LBA48 branch, hdpro_atad composes `sector` the same two-byte way as atad.c, but computes `lcyl = (lba >> 8) & 0xff` and `hcyl = (lba >> 16) & 0xff` — the 28-bit forms. atad.c composes `lcyl = ((lba >> 24) & 0xff00) | ((lba >> 8) & 0xff)` and `hcyl = ((lba >> 32) & 0xff00) | ((lba >> 16) & 0xff)`, whose high bytes `sceAtaExecCmd` writes on the first register pass for EXT commands. In hdpro the first-pass LCYL/HCYL bytes are therefore always 0: LBA bits 32-47 are silently dropped and sectors at or above 2^32 (2 TiB at 512 B) alias onto low sectors. Practical exposure on PS2-sized HDDs is nil today, which is exactly why the divergence has survived — it is a latent wrong-sector write path, not a current data bug.
- **Fix:** Copy atad.c's two composition lines into the HDPro LBA48 branch.
- **Justification if additive:** None — two lines aligned to the sibling.
- **Benefit:** Removes a silent aliasing divergence between the two ATAD variants before any >2 TiB use.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ (the divergence); INFERRED (no current hardware reaches the affected range)

### IOPCORE-14 — CDVDFSV stream read returns an uninitialised error word when `sectors == 0`
- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdfsv/ncmd.c:235-252` (`cdvdSt_read`: `u32 err;` declared at :237, written only by `sceCdStRead` inside the loop, packed into the result at the end)
- **Problem:** With `St->sectors == 0` the loop body never runs, `err` is never assigned, and the function returns `(rpos & 0xFFFF) | (err << 16)` with stack garbage in the high half — the EE's libcdvd reads that as the stream error code. Every other path assigns `err` on every iteration.
- **Fix:** Initialise `u32 err = 0;` (or `SCECdErNO`).
- **Justification if additive:** None.
- **Benefit:** Deterministic RPC result for a legal zero-length stream read.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-15 — `hookMODLOAD` uses `getModInfo`'s result without checking it — the one unhardened caller left in scope
- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdman/ioplib_util.c:276-322` (`getModInfo("modload\0", &info);` at :282, then reads/writes `info.exports[7..22]`)
- **Problem:** `getModInfo` returns 0 and leaves `info.exports` uninitialised when the library is absent (documented in the sibling callers). Every other caller in scope was hardened against exactly this: `device-mmce.c` DeviceFSInit, `device-udpfs.c` DeviceFSInit, `device-smb.c` `ps2ip_init`, `device-http.c` `httpEnsureReady`, and ioplib_util's own `hookAllocSysMemory` (:382) and `heapdiag.c`'s install all check the return. `hookMODLOAD` alone dereferences and *writes through* the possibly-garbage pointer — it overwrites MODLOAD's export table through whatever address the stack held. MODLOAD is in practice always resident (it loaded cdvdman), so this is a latent inconsistency, not a live crash — but it is the only place the scope's own established guard pattern was not applied.
- **Fix:** `if (!getModInfo("modload\0", &info)) return;` at the top, matching the sibling callers.
- **Justification if additive:** One guard, pattern already in the same file.
- **Benefit:** Consistency with the scope's hardened convention; removes a garbage-pointer write if the load order ever changes.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-16 — smb.c carries a permanently dead non-custom receive path
- **Category:** 1 dead code
- **Locations:** `modules/iopcore/cdvdman/smb.c:23` (`#define USE_CUSTOM_RECV 1`, unconditional), `smb.c:997-1020` (the `#else` branch of `smb_ReadAndX`)
- **Problem:** `USE_CUSTOM_RECV` is defined to 1 in the same file with no build option to clear it, so the `#else` implementation of the ReadAndX receive path (~24 lines, duplicating the status/padding/DataLength logic of the live path) can never compile. It is a second copy of receive logic that a reader must audit and a fixer must remember to mirror — in the most security-sensitive parser in the module (its live twin has had several bounds fixes the dead copy does not have).
- **Fix:** Delete the `#else` branch and the `#ifdef`/`#endif` scaffolding in `smb_ReadAndX`; keep the macro only if a Makefile actually toggles it (none in scope does).
- **Justification if additive:** Pure deletion.
- **Benefit:** One receive path to audit and fix, in the SMB1 parser.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-17 — smb.h declares a function that does not exist and ~200 lines of never-referenced protocol constants
- **Category:** 1 dead code / 7 build clutter
- **Locations:** `modules/iopcore/cdvdman/smb.h:501` (`int smb_SessionSetupTreeConnect(char *share_name);` — grep-verified: no definition and no caller anywhere in the repo tree searched), `smb.h:61-117` (all TRANS2 request/response offsets), `smb.h:125-145` (SMB_INFO_* / find levels), and the bulk of the `SMB_COM_*` command list at `smb.h:181-247` (only NEGOTIATE, SESSION_SETUP_ANDX, TREE_CONNECT_ANDX, OPEN_ANDX, READ_ANDX, WRITE_ANDX, CLOSE, NONE are used by smb.c/smb2.c/nfs.c)
- **Problem:** The phantom prototype invites a link error for anyone who calls it, and the unused constant blocks make the header read as if Transaction2, find-first and ~60 commands were implemented surface. This header is shared by three alternative protocol implementations, so its stated surface is the contract a maintainer audits against.
- **Fix:** Delete the prototype and the unreferenced define blocks (keep any a Makefile or the menu-side smbinit genuinely consumes — grep before deleting, as was done for the prototype).
- **Justification if additive:** Pure deletion.
- **Benefit:** Header surface matches implemented surface across all three SMB-family builds.
- **Effort:** S
- **Risk:** Low (after the grep check the fix itself prescribes)
- **Confidence:** READ (prototype verified by repo-wide grep; constant blocks verified unused within this scope, EE/menu-side use to be re-grepped before deletion)

### IOPCORE-18 — CDVDFSV reports CDVDMAN version 0x223 while CDVDMAN reports 0x225
- **Category:** 7 build clutter / 6 bugs (version skew)
- **Locations:** `modules/iopcore/cdvdfsv/cdvdfsv.c:184-185` (`r->cdvdfsv_ver = 0x223; r->cdvdman_ver = 0x223;`) vs `modules/iopcore/cdvdman/cdvdman.c:125` (`CDVDMAN_MODULE_VERSION 0x225`, returned via `CDSC_GET_VERSION` at cdvdman.c:891)
- **Problem:** The CDINIT RPC answer — the version pair EE-side libcdvd and diagnostics see at init — hardcodes both versions and has drifted two revisions behind the module's own version constant. Anything that gates behaviour or bug reports on the init-reported CDVDMAN version is reading a stale number.
- **Fix:** Report `sceCdSC(CDSC_GET_VERSION, …)` for `cdvdman_ver` (the RPC thread already calls `sceCdSC`), or define the pair once in `common/` next to `CDVDMAN_MODULE_VERSION`.
- **Justification if additive:** None — replaces a literal with the existing source of truth.
- **Benefit:** Init-reported versions stop lying about which cdvdman is resident.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-19 — Two incompatible hex-address parsers in sibling modules; igrmbox's rejects lowercase
- **Category:** 2 duplication
- **Locations:** `modules/iopcore/freezediag/freezediag.c:24-42` (`parse_hex8`, accepts `0-9 A-F a-f`) and `modules/iopcore/igrmbox/igrmbox.c:151-167` (`mbHex8`, accepts `0-9 A-F` only)
- **Problem:** Both modules are loaded by ee_core with an eight-hex-digit IOP/EE address argument, and each carries its own copy of the parser. The copies have already diverged: a lowercase address string is valid for freezediag and a module-refusing parse failure (`_start` returns NO_RESIDENT_END, no mailbox) for igrmbox. Today ee_core formats both, presumably uppercase, so this is latent — but the divergence is invisible precisely because the code is duplicated.
- **Fix:** Make `mbHex8` accept lowercase, matching `parse_hex8` (the modules are separate IRXs, so a shared copy is not available without a new common module — LESS NOT MORE says fix the divergence, not the structure).
- **Justification if additive:** Two range comparisons in an existing branch.
- **Benefit:** Both ee_core-loaded helpers accept the same address format.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ (divergence); INFERRED (ee_core currently sends uppercase)

### IOPCORE-20 — BDM: `g_bd_sectors_per_sector` is computed, never used; reads hardcode a 512-byte sector size
- **Category:** 1 dead code / 6 bugs (latent)
- **Locations:** `modules/iopcore/cdvdman/device-bdm.c:22` (declaration), `:106` (only assignment, in `bdm_connect_bd`), `:245-262` (`DeviceReadSectors` hardcodes `lsn * 4` and `sectors * 4`)
- **Problem:** The connect path carefully derives `2048 / bd->sectorSize` and then nothing ever reads the variable. All defrag calls assume 4 block-device sectors per CD sector, i.e. a 512-byte `sectorSize`. A block device with a different sector size would be read at wrong offsets while the one variable that knows better sits unused — either the variable is dead weight or the hardcode is a latent bug; the file currently has both.
- **Fix:** Use `g_bd_sectors_per_sector` in `DeviceReadSectors` in place of the literal 4 (and in the bounce path), or delete the variable and assert `sectorSize == 512` at connect. The first is one identifier swap per site.
- **Justification if additive:** None.
- **Benefit:** Removes a dead variable or a wrong-offset read for non-512-byte block devices — one of the two is currently true.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-21 — DVD TOC builder marks single-layer discs as dual-layer whenever DVDDL emulation is off
- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdman/ncmd.c:156-158` (`cdvdman_fill_toc`, DVD branch: `dual = 1` if `(!(flags & IOPCORE_COMPAT_EMU_DVDDL)) || (layer1_start > 0)`)
- **Problem:** As written, `dual` is 1 for every DVD when the EMU_DVDDL compat flag is clear — including ordinary single-layer games whose `layer1_start` is 0. The TOC bytes that follow are then the dual-layer variants (`tocBuff[0] = 0x24`, `[4] = 0x41`, `[14] = 0x21` OTP/PTP) for a disc that has no layer 1, while `maxlsn` at :185 correctly falls back to `mediaLsnCount` when `layer1_start` is 0 — the two halves of the same response disagree. The expected condition for "this is a dual-layer image" is `layer1_start > 0` (compare `sceCdReadDvdDualInfo`, scmd.c, which derives `on_dual` from exactly that).
- **Fix:** Set `dual = (layer1_start > 0)` (keeping the EMU_DVDDL case, which fakes a second layer, as the explicit exception it is in `sceCdReadDvdDualInfo`).
- **Justification if additive:** None — a condition correction.
- **Benefit:** `sceCdGetToc` stops describing single-layer DVDs as dual-layer to games that parse the TOC.
- **Effort:** S
- **Risk:** Medium — TOC bytes are game-visible; needs a hardware check against a game that reads the DVD TOC. Flagged for that reason.
- **Confidence:** INFERRED (condition and its inconsistency with `sceCdReadDvdDualInfo` are READ; that no game depends on the current bytes is not verifiable in this scope)

### IOPCORE-22 — `cdrom_lseek` SEEK_END subtracts the offset instead of adding it
- **Category:** 6 bugs
- **Locations:** `modules/iopcore/cdvdman/ioops.c:352-385` (`SEEK_END` case at :369-371: `r = fh->position = fh->filesize - offset;`)
- **Problem:** POSIX/SCE lseek semantics are `position = filesize + offset` with a (typically negative) offset. As written, `lseek(fd, -N, SEEK_END)` computes `filesize + N`, which the clamp at :376-377 then pins to `filesize` — seeking backwards from EOF always lands on EOF; `lseek(fd, +N, SEEK_END)` moves N bytes *before* EOF. Any FILEIO client that seeks relative to end-of-file (tail reads, footer parsing) gets the wrong position with a success return.
- **Fix:** `fh->position = fh->filesize + offset`, keeping the existing upper clamp and adding the missing lower clamp at 0 (a negative result currently wraps through the `unsigned int position` field and is only caught by the `> filesize` test).
- **Justification if additive:** None.
- **Benefit:** SEEK_END works for the conventional negative-offset call.
- **Effort:** S
- **Risk:** Low-Medium — if any in-game caller was written against the inverted behaviour it would change; no such caller exists in scope.
- **Confidence:** INFERRED (code READ; that callers use conventional semantics is the standard libcdvd/FILEIO contract, not verifiable from this scope alone)

### IOPCORE-23 — imgdrv `read_fs` ignores both the file position and the image size
- **Category:** 6 bugs
- **Locations:** `modules/iopcore/imgdrv/imgdrv.c:27-31` (`memcpy(buffer, (void *)ioprpimg, size); return size;`), size source `imgdrv.c:10` (`ioprpsiz`, patched in by ee_core)
- **Problem:** Every read returns the image from its first byte regardless of the descriptor's position, and copies the caller's full `size` even past `ioprpsiz` — reading beyond the patched-in IOPRP image into whatever IOP RAM follows it and reporting those bytes as image data. It works today only because the sole consumer (UDNL via MODLOAD) is believed to issue one sequential full-image read pattern that starts at offset 0. A chunked or resumed read silently gets duplicate/wrong module bytes instead of an error.
- **Fix:** Track/derive the offset from `fd`'s position as ioman provides it, and clamp `size` to `ioprpsiz - offset`, returning the short count.
- **Justification if additive:** A clamp and an offset add in one 5-line function.
- **Benefit:** The host: image device stops being one caller-habit away from loading corrupted IOP modules.
- **Effort:** S
- **Risk:** Medium — touches a boot-critical path; needs a real-hardware boot test before acceptance.
- **Confidence:** INFERRED (function READ; consumer's exact read pattern is outside this scope)

### IOPCORE-24 — `smb_CloseAll`'s −1 sentinel can never be stored in the u16 FID table (all three SMB-family files)
- **Category:** 6 bugs / 2 duplication
- **Locations:** `modules/iopcore/cdvdman/smb.c:1154-1165`, `modules/iopcore/cdvdman/smb2.c:1132-1143`, `modules/iopcore/cdvdman/nfs.c:341-352` (identical bodies); FID storage type `modules/iopcore/common/cdvd_config.h:71` (`u16 FIDs[ISO_MAX_PARTS]`)
- **Problem:** Each copy loops `fd = FIDs[i]; if (fd >= 0) { smb_Close(fd); FIDs[i] = -1; }`. `FIDs` is `u16`, so the `-1` sentinel is stored as 65535 and read back as 65535 — which also passes `fd >= 0`. A second `smb_CloseAll` (DeviceUnmount runs at shutdown and again via DeviceDeinit for SMB) therefore "closes" every slot again: SMB1 sends CLOSE requests for FID 65535, SMB2/NFS return EBADF harmlessly. The guard that was meant to make close-all idempotent does not work in any of the three copies.
- **Fix:** Test `fd != 0xFFFF` (the stored sentinel) instead of `fd >= 0`, in all three copies — or track openness in the per-file tables SMB2/NFS already keep.
- **Justification if additive:** None — a comparison change ×3.
- **Benefit:** Shutdown stops issuing closes for files that were never opened, on every network build.
- **Effort:** S
- **Risk:** Low
- **Confidence:** READ

### IOPCORE-25 — Identical `smb_ReadCD` + `smb_CloseAll` bodies triplicated across the three protocol implementations
- **Category:** 2 duplication
- **Locations:** `smb_ReadCD`: `smb.c:1149-1152`, `smb2.c:1127-1130`, `nfs.c:336-339`; `smb_CloseAll`: see IOPCORE-24
- **Problem:** The three files are alternative builds providing one symbol set (by design), but the two functions that depend on none of the protocol differences are copied verbatim into all three — and have already needed the same fix in three places once (IOPCORE-24 is the proof: the sentinel bug exists in all three copies because the body does). Any future fix to part-offset arithmetic (`lsn * 2048`, `lsn >> 21`) must be made three times.
- **Fix:** Move the two bodies to a small shared compilation unit linked by all three cdvdman variants (the builds already share `device-smb.c` the same way). If the build system makes that awkward, at minimum fix IOPCORE-24 in all three places in one commit and note the triplication at the top of each copy.
- **Justification if additive:** Net deletion (~34 lines become ~17 plus one link reference).
- **Benefit:** One place for FID/part bookkeeping fixes across SMB1, SMB2 and NFS builds.
- **Effort:** M (build wiring across three module variants)
- **Risk:** Medium — touches three build variants; each needs a build check.
- **Confidence:** READ (bodies compared during read; `smb_ReadCD` byte-identical, `smb_CloseAll` byte-identical)

### IOPCORE-26 — Small dead items: unused macros, an unreachable duplicate table entry, a never-linked helper
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

## B. Checked-clean / dissolved

- **cdvdman vs cdvdfsv `ncmd.c`/`scmd.c`/`searchfile.c` name twins — NOT duplicates.** md5s differ; diffing shows the cdvdman files are the IOP-side libcdvd implementation and the cdvdfsv files are RPC servers that wrap them (own packet structs, RPC registration). No shared body worth merging.
- **`atad.c` vs `hdpro_atad.c` overall duplication — dissolved as a merge candidate.** ~700 lines of parallel structure, but the backends genuinely differ (DMA vs PIO-only, SPEED registers vs HDPro ROM-window protocol, different command tables). Merging would add abstraction to hardware code for no defect. The one concrete divergence found is filed as IOPCORE-13.
- **SMB1 vs SMB2 vs NFS symbol duplication — by design** (alternative builds, one symbol set, EE picks the module). Only the protocol-independent bodies are filed (IOPCORE-25).
- **device-http.c `DeviceReadSectors` not re-checking `length` against `sectors * 2048` — dissolved.** The included `httpstream.inc` (outside scope, read as supporting evidence) validates a 206 response's Content-Range first/last/total and requires `bodyLen == rangeEnd - rangeStart + 1` before returning success, so `length` is the requested byte count by construction.
- **`cdvdman_buf` / `cdvdman_fs_buf` static sizes (ioops.c:16-17, internal.h CDVDMAN_BUF_SECTORS) — checked.** fs_buf's 8 sectors + 64 B alignment pad are consumed by CDVDFSV's alignment-correction reads (`CDVDMAN_FS_SECTORS`), not waste; cdvdman_buf's 1-2 sectors match its searchfile/unaligned-read use.
- **`sceCdReadDiskID` (cdvdman/ncmd.c) reading the caller's buffer before writing it — checked.** Odd, but it mirrors the SCE original's DiskID-probe semantics and both in-scope callers pass zeroed buffers.
- **cdvdman/searchfile.c `cdvdman_locatefile` returning a pointer into `cdvdman_buf` — checked.** The sole caller consumes LBA/size before any further disc read overwrites the buffer; the directory-recursion path re-reads before reuse. The 12-char `strncmp` match is the documented ISO9660 level-1 assumption, commented in-file.
- **ioops.c `cdrom_dopen` skipping the purify/length path that `cdrom_open`/`cdrom_getstat` take — checked.** Downstream `cdvdman_findfile` bounds the name into a 256-byte static and `cdvdman_locatefile` bounds each segment; no overflow path remains.
- **smb2auth.c HMAC key truncation over 64 bytes — checked.** Documented in `smb2auth.h`; every in-scope key is 16 bytes. Not a defect for any current caller.
- **smb2auth.c `sha256_block` static `w[64]` — checked.** Fully rewritten per block before any use, and all in-scope hashing is serialised by `smb_io_sema` or runs single-threaded at session setup.
- **smb.c `setStringField`/`asciiToUtf16` unbounded writes — checked against buffer geometry.** Inputs are the bounded settings/server-spec fields (≤256-byte paths, ≤48-byte credentials); worst-case UTF-16 expansion fits the 896+128-byte `SMB_buf` union. No overflow path found.
- **smb2.c `put_utf16` (no output-length parameter) — checked per caller.** All inputs are length-bounded before the call (settings fields ≤32 chars, share paths ≤256 chars, `spn` ≤32 chars) and each output buffer's worst case was accounted, including the `_Static_assert` the file itself carries for the AUTHENTICATE message.
- **device-udpfs.c retry loop — checked.** Attempt accounting is correct on all four exits (success, late server, exhausted retries, wait-failure shortcut); VMC path mirrors it.
- **device-mmce.c — checked, no new candidate.** The bounded init (100 polls), fail-fast states, whole-function semaphore, and last-attempt result judgement are all as their comments describe. One stale `//TODO: For VMCs` comment above the implemented `mmce_read_offset` is noted but not filed (cosmetic).
- **resetspu.c busy delays — checked.** `nopdelay` and the bounded status poll are hardware sequencing for SPU reset, not waits on external events.
- **apemodpatch missing `FlushIcache` (iremsndpatch has it) — checked.** apemodpatch patches a module it has just loaded and not yet started; its text has not executed, so there is no stale icache copy to flush. iremsndpatch patches an already-loaded module, where the flush is required. Difference is principled.
- **Patch-module LoadModule/locate boilerplate (×3) — dissolved.** Each patch is a standalone IRX loaded by name; a shared helper would need a new common module to save ~25 lines ×3. Not a LESS-NOT-MORE win.
- **freezediag.c `len` from the format functions — not filed.** Whether `freeze_diag_format` can return negative or over-report lives in `freeze_diag_report.h`'s implementation, which is outside this scope; the call site alone does not establish a defect. Flagged here for the ee_core-side reader.
- **heapdiag.c — checked (INGAMEDIAG only).** The stale-record wipe, eviction policy and unknown-free counting are diagnostic-only inaccuracies at worst (a NULL free counts as "unknown"); no game-visible effect.
- **cdvdman.c `DeviceReadSectorsCached` window logic — checked** (beyond the -206/-016 cross-refs). Failed-window fallback, end-of-image behaviour and the `sectors == MAX_SECTOR_CACHE` passthrough are all as commented.
- **`cdvdman_settings` FID/settings overlay (cdvd_config.h) — checked** beyond RIPTOPL-016: device-smb.c's mount-state machine correctly refuses to re-read aliased fields after a failed mount.

---

## C. Per-file ledger (63 files, 14,171 lines — unread: none)

| File | Lines |
|---|---|
| modules/iopcore/cdvdfsv/cdvdfsv-internal.h | 40 |
| modules/iopcore/cdvdfsv/cdvdfsv.c | 288 |
| modules/iopcore/cdvdfsv/irx_imports.h | 16 |
| modules/iopcore/cdvdfsv/ncmd.c | 451 |
| modules/iopcore/cdvdfsv/scmd.c | 186 |
| modules/iopcore/cdvdfsv/searchfile.c | 81 |
| modules/iopcore/cdvdman/atad.c | 796 |
| modules/iopcore/cdvdman/atad.h | 100 |
| modules/iopcore/cdvdman/cdvdman.c | 1233 |
| modules/iopcore/cdvdman/dev9-pccf.c | 19 |
| modules/iopcore/cdvdman/dev9.c | 658 |
| modules/iopcore/cdvdman/dev9.h | 38 |
| modules/iopcore/cdvdman/device-bdm.c | 310 |
| modules/iopcore/cdvdman/device-hdd.c | 150 |
| modules/iopcore/cdvdman/device-http.c | 170 |
| modules/iopcore/cdvdman/device-mmce.c | 272 |
| modules/iopcore/cdvdman/device-smb.c | 252 |
| modules/iopcore/cdvdman/device-udpfs.c | 244 |
| modules/iopcore/cdvdman/device.h | 10 |
| modules/iopcore/cdvdman/hdpro_atad.c | 756 |
| modules/iopcore/cdvdman/heapdiag.c | 179 |
| modules/iopcore/cdvdman/internal.h | 188 |
| modules/iopcore/cdvdman/ioman_add.h | 50 |
| modules/iopcore/cdvdman/ioops.c | 647 |
| modules/iopcore/cdvdman/ioplib_util.c | 392 |
| modules/iopcore/cdvdman/ioplib_util.h | 21 |
| modules/iopcore/cdvdman/irx_imports.h | 30 |
| modules/iopcore/cdvdman/ncmd.c | 316 |
| modules/iopcore/cdvdman/nfs.c | 364 |
| modules/iopcore/cdvdman/scmd.c | 268 |
| modules/iopcore/cdvdman/searchfile.c | 316 |
| modules/iopcore/cdvdman/smb.c | 1177 |
| modules/iopcore/cdvdman/smb.h | 515 |
| modules/iopcore/cdvdman/smb2.c | 1153 |
| modules/iopcore/cdvdman/smb2auth.c | 307 |
| modules/iopcore/cdvdman/smb2auth.h | 42 |
| modules/iopcore/cdvdman/smsutils.S | 180 |
| modules/iopcore/cdvdman/streaming.c | 455 |
| modules/iopcore/common/cdvd_config.h | 187 |
| modules/iopcore/common/cdvdman_opl.h | 78 |
| modules/iopcore/common/freeze_diag.h | 66 |
| modules/iopcore/common/heap_diag.h | 87 |
| modules/iopcore/common/igrmbox.h | 56 |
| modules/iopcore/common/mmcedrv_config.h | 9 |
| modules/iopcore/common/oplsmb.h | 30 |
| modules/iopcore/common/smsutils.h | 35 |
| modules/iopcore/freezediag/freezediag.c | 81 |
| modules/iopcore/freezediag/irx_imports.h | 11 |
| modules/iopcore/igrmbox/igrmbox.c | 188 |
| modules/iopcore/igrmbox/irx_imports.h | 13 |
| modules/iopcore/imgdrv/imgdrv.c | 98 |
| modules/iopcore/imgdrv/irx_imports.h | 25 |
| modules/iopcore/patches/apemodpatch/irx_imports.h | 15 |
| modules/iopcore/patches/apemodpatch/main.c | 128 |
| modules/iopcore/patches/cleareffects/irx_imports.h | 11 |
| modules/iopcore/patches/cleareffects/main.c | 30 |
| modules/iopcore/patches/f2techioppatch/irx_imports.h | 12 |
| modules/iopcore/patches/f2techioppatch/main.c | 60 |
| modules/iopcore/patches/iremsndpatch/asm.S | 30 |
| modules/iopcore/patches/iremsndpatch/irx_imports.h | 27 |
| modules/iopcore/patches/iremsndpatch/main.c | 118 |
| modules/iopcore/resetspu/irx_imports.h | 10 |
| modules/iopcore/resetspu/resetspu.c | 96 |
| **Total** | **14,171** |

Unread: none (0 files, 0 lines).
