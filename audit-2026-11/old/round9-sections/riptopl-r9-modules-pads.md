# RiptOPL Round 9 — modules/ pads & card drivers (ds34bt, ds34usb, pademu, mcemu, mmce, mx4sio_bd, vmc, debug, bdmevent, freepad)

- Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` — verified `git rev-parse HEAD` at start; clone left git-clean (no repo edits).
- Scope read: every text file under the nine module trees + `modules/freepad/PROVENANCE.md`. Text ledger total **20,109 lines** (per-file table in §D) + 46 lines freepad + 5 prebuilt debug binaries hashed/sized (§D). Task's ~18,776 figure counts a narrower source-only subset; the ledger below is the exact `wc -l` of everything read.
- Already-filed items in scope (RIPTOPL-032, -039, -051, -057, -058, -062, -066, -069, -072, -082, -083, -092, -098, -105…-114, -117, -118, -157, -162, -164…-167, -185, -187, -196, -201, -207, -218, -244, -263; X-49) were cross-referenced and are **not** re-filed. Everything below is beyond them.
- Nothing here is PS2-build- or hardware-verified. Confidence labels say what was actually read.

---

## A. Candidates (sorted by benefit)

### PADS-01 — genvmc's physical-card copy divides by, and indexes with, unchecked card-supplied superblock geometry
- **Category:** 6 (bug/security — device-supplied values)
- **Locations:** `modules/vmc/genvmc/genvmc.c:238-281` (`vmc_mccopy`), esp. `:248-250`, `:258`, `:260`, `:276`
- **Problem:** READ. After checking only the 28-byte magic, `vmc_mccopy` trusts the source card's superblock: `pagesblock = (BLOCKKB*1024)/mcdi->pagesize`, `clustersblock = pagesblock/mcdi->pages_per_cluster`, `blocks = clusters_per_card/clustersblock`, progress `i/(blocks/99)`, and page reads into `&cluster_buf[j * mcdi->pagesize]`. A corrupt/malicious card with `pagesize==0`, `pages_per_cluster==0`, or `clustersblock==0` traps on division by zero on the IOP; `pagesize > 512` walks `cluster_buf` (16 KiB + 16) past its end; `blocks < 99` makes `blocks/99 == 0` and the progress update divides by zero on the first iteration of an otherwise valid small card. The create path (`vmc_mcformat`) sets all of these itself and is safe — only the copy path imports them.
- **Fix:** After the magic check, reject the copy (`-100`-family return) unless `pagesize` is a power-of-two in [128,512] dividing 16 KiB, `pages_per_cluster` is non-zero and divides `pagesblock`, `clustersblock > 0`, and `blocks >= 99` (or compute progress as `i*99/blocks` with a `blocks==0` guard). ~6 lines, deletion-neutral validation only.
- **Benefit:** Removes an IOP trap + heap-adjacent (static buffer) overwrite reachable by inserting a damaged card and choosing Copy — the one genvmc path whose inputs are not RiptOPL's own.
- **Effort:** S · **Risk:** Low (rejects only geometry no real PS2 card reports) · **Confidence:** High (READ, full function)

### PADS-02 — pademu `_start` reads 8 bytes of `argv[1]` with no length check
- **Category:** 6 (bug — unchecked caller-supplied length)
- **Locations:** `modules/pademu/pademu.c:118-125` (file lines: the two `mips_memcpy` calls in `_start`), and `:138` (`argv[1][3]`)
- **Problem:** READ. `if (argc > 1)` is the only guard before `mips_memcpy(&raw, argv[1], 4)` and `mips_memcpy(&macro_settings, argv[1]+4, 4)`. IRX argv strings are NUL-terminated load arguments, not fixed 8-byte records: a loader (or a future EE caller) passing a 1–7-byte argument makes the module read past the string into adjacent IOP memory and interpret it as pad-enable/vibration/multitap bits and macro settings. Every other consumer of this argument in-tree passes a full record, so this is latent, not live — but the module itself never states or checks the contract.
- **Fix:** Require `argc > 1 && strlen(argv[1]) >= 8` (or copy `min(strlen,8)` into a zeroed record) before either copy; keep the defaults otherwise. 1–2 lines.
- **Benefit:** Closes the only unbounded load-argument read in the pad stack; makes the 8-byte contract explicit at the single place it is assumed.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ); live exploitability INFERRED-low (current EE caller passes a full record)

### PADS-03 — mcemu FastIO hooks dereference the FastIO allocation without ever checking it exists; the read hook also DMA-transfers a full cluster for a short tail
- **Category:** 6 (bug)
- **Locations:** `modules/mcemu/mcemu_rpc.c` — `hookMcman62` (allocation + bind, returns 0 on failure), `hookMcman63` and `hookMcman68` (unchecked `pFastBuf`/`pFastRpcBuf`/`pClientData` use); `sdd.size = MC2_CLUSTER_SIZE` in `hookMcman63` regardless of the chunk's actual `size`
- **Problem:** READ. `hookMcman62` is the only place `pFastBuf` is allocated, and it leaves the globals NULL on allocation or RPC-bind failure. `hookMcman63`/`hookMcman68` — installed as MCMAN exports 63/68 whenever MCMAN ≥ 0x208 registers — use `pFastBuf`/`pClientData` unconditionally, so a game calling sceMcReadFast/WriteFast after a failed 62 dereferences NULL on the IOP. Separately, in `hookMcman63` the DMA descriptor's `size` is hard-coded to `MC2_CLUSTER_SIZE` (0x400) even for the final short chunk where `size < 0x400`, transferring stale buffer bytes past the bytes just read to the EE address (the RPC length field at `rpcbuf[0xC]` does carry the true size, so impact is bounded to an over-transfer, not a wrong length report — stated exactly).
- **Fix:** Early-return an error from 63/68 when `pFastBuf == NULL || pClientData == NULL`; set `sdd.size = size`. 3 lines.
- **Benefit:** Turns a NULL-deref IOP crash on low-memory/bind-failure boots into a clean FastIO failure; stops a 0x400-byte DMA over-transfer on every FastIO read tail.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ, all three functions end-to-end)

### PADS-04 — `mmce_fs_open`/`dopen` failure exits leave `file->privdata` pointing at a still-free handle slot, which the next open then aliases
- **Category:** 6 (bug)
- **Locations:** `modules/mmce/mmceman/src/mmce_fs.c:63-140` (`mmce_fs_open`, P1/P2/P3 timeout and bad-reply exits), `:586-660` (`mmce_fs_dopen`, same shape)
- **Problem:** READ. `open` stores `file->privdata = mmce_fs_find_free_handle()` — a pointer *into* `mmce_fs_fds[]` whose slot value is still `-1` (free) until the card returns an fd. Every pre-fd failure exit returns `-1` without clearing `file->privdata` (contrast the ENOENT exit, which does clear it, and the close patch, which frees the slot on every exit precisely because a stale slot strands/aliases). A failed open therefore leaves iomanX holding a file whose privdata aliases a free slot: the next successful open takes that same slot, and any later operation/close through the failed file reads or frees the *other* file's card fd.
- **Fix:** Set `file->privdata = NULL` on every failure exit of open/dopen (the slot needs no value reset — it was never assigned). ~4 added assignments, no new logic.
- **Benefit:** Removes an fd-aliasing bug in the driver whose fd-pool exhaustion already caused a shipped incident (#120, per the in-tree close patch); failure paths become symmetric with the one correct exit.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ); aliasing consequence INFERRED from iomanX privdata lifetime

### PADS-05 — genvmc STATUS devctl copies to the caller's buffer with none of the validation the CREATE case applies in the same switch
- **Category:** 6 (bug — inconsistent validation)
- **Locations:** `modules/vmc/genvmc/genvmc.c` `genvmc_devctl` (CREATE case validates `args == NULL || arglen < sizeof(createVMCparam_t)`; STATUS case calls `vmc_status((statusVMCparam_t *)buf)` with no `buf == NULL` / `buflen < sizeof(statusVMCparam_t)` check)
- **Problem:** READ. `vmc_status` unconditionally `memcpy`s 76 bytes to `buf`. A devctl caller passing a short or NULL out-buffer — exactly the class of caller the CREATE case in the same function defends against — writes 76 bytes over whatever follows. In-tree EE caller passes a correct buffer today (INFERRED from the shared header contract), so this is a latent trap for the next caller, not a live overflow.
- **Fix:** Mirror the CREATE check: `if (buf == NULL || buflen < sizeof(statusVMCparam_t)) r = -EINVAL;`. 1 line.
- **Benefit:** Makes the module's only three commands uniformly defensive; cheapest possible close.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ, full devctl + both helpers)

### PADS-06 — Both mcemu ports share the single 512-byte page data buffer
- **Category:** 2/6 (duplication-by-sharing — correctness hazard)
- **Locations:** `modules/mcemu/mcemu_io.c:31` (`mcds->dbufp = &mcdatabuf[0]` for every port), `modules/mcemu/mcemu_var.c:62` (`u8 mcdatabuf[0x200]` — comment in-tree: "Same buffer for both cards"), `modules/mcemu/mcemu.h:218`
- **Problem:** READ. Unlike the ECC buffer, which is per-port (`mceccbuf[MCEMU_PORTS][0x20]`), the page data buffer is one 512-byte array handed to both `MemoryCard` descriptors. `MceRead`/`MceWrite` stage through `dbufp` with per-card `rdoff/wroff/rpage/wpage` state, so two cards touched in interleaved SIO2 transfers (the multitap/two-slot games mcemu exists for) stage through the same bytes. Whether real interleaving corrupts a page depends on transfer atomicity under the SIO2 lock — INFERRED hazard, not a demonstrated corruption — but the asymmetry with the per-port ECC buffer shows the sharing is an accident of the original design, not a requirement.
- **Fix:** `u8 mcdatabuf[MCEMU_PORTS][0x200]` and index it by port in `mc_configure`. +512 bytes IOP RAM, 2 lines.
- **Justification if additive:** The 512 bytes buy per-port isolation in a 2 MB IOP that already spends far more on far less; the alternative (proving global transfer atomicity forever) costs more certainty than the RAM costs space.
- **Benefit:** Eliminates a whole cross-port contamination class for the price of half a kilobyte.
- **Effort:** S · **Risk:** Low · **Confidence:** High that the sharing exists (READ); Medium that it has bitten in practice (INFERRED)

### PADS-07 — `ioplib.c`/`ioplib.h` are byte-identical vendored copies in mmceman and mx4sio_bd, and their `sio2man_hook.c` twins differ by 3 lines
- **Category:** 2 (duplication)
- **Locations:** `modules/mmce/mmceman/src/ioplib.c` ≡ `modules/mx4sio_bd/src/ioplib.c` (md5 `14acaf5b34248d9c9c976580a5175d39`, 101 lines each); `modules/mmce/mmceman/include/ioplib.h` ≡ `modules/mx4sio_bd/src/ioplib.h` (md5 `497aa3554ae65d65d22095670f5fc408`); `modules/mmce/mmceman/src/sio2man_hook.c` (318 lines) vs `modules/mx4sio_bd/src/sio2man_hook.c` (315 lines) — same function list, near-verbatim
- **Problem:** READ + hashed. Two in-tree IOP modules that can be resident in the same boot each carry their own copy of the export-table hook machinery. Any fix to the hook iteration (the class of code that produced the mc1-blocking bug fixed in mmceman's pin) must be made twice and verified twice; the sio2regs.h headers have already drifted (different PCTRL1 macro names for the same registers).
- **Fix:** LESS-NOT-MORE form: do **not** build a shared library. Add a drift guard in the existing style of the tree's other fork tests (a text test asserting the two `ioplib.c` copies stay identical, and naming the 3-line sio2man_hook delta), so the next one-sided edit fails CI instead of shipping. Deleting one copy is wrong while both modules vendor independently-built SDK subtrees.
- **Benefit:** Converts silent divergence into a test failure at near-zero runtime cost.
- **Effort:** S · **Risk:** Low · **Confidence:** High (md5-verified, §B)

### PADS-08 — `modules/debug/drvtif.irx` and `drvtif-ingame.irx` are the same bytes under two names
- **Category:** 7 (build clutter) / 1 (dead duplicate)
- **Locations:** `modules/debug/drvtif.irx`, `modules/debug/drvtif-ingame.irx` — both 5,985 bytes, md5 `838e4e7adf1a3207b049fdcd0b84d046`
- **Problem:** READ (hashed). The `-ingame` naming promises a distinct in-game variant (and for `tifinet` the promise is kept: the pair differ, md5 `6c52a13f…` vs `f212f485…`, same 3,301 size). For `drvtif` there is no variant — one of the two tracked prebuilts (see already-filed RIPTOPL-032 for the prebuilt class itself) is pure duplication, and any future real in-game drvtif build risks being "updated" in only one of the two slots.
- **Fix:** Delete one copy and point its consumer at the other, or replace both with a build-time copy step. Net −5,985 tracked bytes.
- **Benefit:** One less stale-binary slot in a directory whose entire risk is staleness.
- **Effort:** S · **Risk:** Low (verify the two embed sites by name before deleting) · **Confidence:** High (md5-identical)

### PADS-09 — mx4sio's DMA command struct reserves a 1 KiB CRC table (`crc[512]`, marked FIXME in-tree)
- **Category:** 4 (size/memory — IOP)
- **Locations:** `modules/mx4sio_bd/src/mx4sio.h:46` (`uint16_t crc[512]; /*FIXME*/`, inside `dma_command_t`, compiled under `CONFIG_USE_CRC16`)
- **Problem:** READ. A single static command struct carries 512 × u16 = 1,024 bytes of CRC storage — one entry per sector of a maximum-size transfer — whether the transfer is 1 sector or 512. On a ~2 MB IOP this is the largest single discretionary allocation in the module. Whether `CONFIG_USE_CRC16` is enabled in RiptOPL's build decides if the bytes are live or merely source weight (INFERRED: config header read, enablement depends on build defines).
- **Fix:** Cap the table at the driver's real per-DMA chunk (the read path processes at most 16-sector groups elsewhere in this family) and stream/verify per chunk, or document the 512-sector single-DMA requirement next to the FIXME if it is real. Do not shrink blind: the fix is "size the table to the chunking the driver actually does," verified against `spisd_read_multi_do`.
- **Benefit:** Up to ~1 KiB IOP RAM back, and a FIXME replaced by a stated invariant.
- **Effort:** M · **Risk:** Medium (touches the CRC-verified read path; needs hardware re-test) · **Confidence:** Medium

### PADS-10 — bdmevent's `irx_imports.h` imports four libraries the module never calls
- **Category:** 7 (build clutter)
- **Locations:** `modules/bdmevent/irx_imports.h` (includes `iomanX.h`, `stdio.h`, `sysclib.h`, `sysmem.h`) vs `modules/bdmevent/main.c` (88 lines; calls only `bdm_*` and `sceSif*`) and `modules/bdmevent/imports.lst` (correctly lists only bdm + sifcmd)
- **Problem:** READ. The header's "keep alphabetical" boilerplate was copied from the ps2sdk template and never pruned; the `.lst` is right and the header is wrong, so the two sources of import truth disagree in the smallest module in the tree.
- **Fix:** Delete the four unused includes. −4 lines.
- **Benefit:** Trivial, but removes a disagreement a future editor will resolve the wrong way.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ, whole module)

---

## B. ds34 copy census (md5 evidence, this pin)

| File | Lines | md5 |
|---|---|---|
| `modules/ds34bt/iop/ds34bt.c` (standalone BT) | 1,719 | `ffa59cdd504b05e7959b4d591359fab5` |
| `modules/pademu/ds34bt.c` (pademu BT) | 1,425 | `5be3fb4c5caa13e8632a3e7f6e5697b1` |
| `modules/ds34usb/iop/ds34usb.c` (standalone USB) | 998 | `58fa1fb7b122a105120881d25f6655d8` |
| `modules/pademu/ds34usb.c` (pademu USB) | 616 | `c81aba118c469a8d6b630521c060e1f6` |
| `modules/ds34bt/iop/smsutils.S` | 180 | `3612cf1bd66a11f0b9e53ac78a5940d7` |
| `modules/ds34usb/iop/smsutils.S` | 180 | `3612cf1bd66a11f0b9e53ac78a5940d7` — **byte-identical to the BT copy** |
| `modules/ds34bt/ee/libds34bt.c` | 181 | `10d1e9ad42c7c40076a0a2eaa5ea6e0e` |
| `modules/ds34usb/ee/libds34usb.c` | 153 | `ec94139ca0e46643e9160a506ae4dda8` |
| `modules/ds34bt/iop/ds34bt.h` / `modules/pademu/ds34bt.h` | 256 / 240 | `042d8766…` / `52c19ec9…` (differ) |
| `modules/ds34usb/iop/ds34usb.h` / `modules/pademu/ds34usb.h` | 74 / 88 | `297d213a…` / `5c915840…` (differ) |

Defect presence by copy (already-filed items only; no new ds34 defect found this round — see §C):

- `power_level[report->Power]` unguarded (6-entry table; only `!= 0xEE` checked): **standalone BT** (`ds34bt/iop/ds34bt.c:1333`) and **standalone USB** (`ds34usb/iop/ds34usb.c:338`) — RIPTOPL-092 family. **pademu USB guards** (`report->Power <= 0x05`, `pademu/ds34usb.c:333`); pademu BT routes through its guarded copy. Standalone USB's separate DS4/DS5 path at `:531` masks `Battery & 0x0F` correctly — the unguarded site is the DS3 path only.
- DS3/DS4 memset/send length mismatch (-066): both BT copies, as filed.
- `get_data` unchecked memcpy length (-106): all four IOP copies, as filed.
- EE clients: `libds34bt.c` and `libds34usb.c` are one RPC-client library twice with renamed symbols/RPC IDs (`0x18E3878F` vs `0x18E3878E`) plus BT-only charging/version/features calls — RIPTOPL-244, as filed. Both copy `rpcbuf` results even on RPC failure (-187), as filed.
- `smsutils.S`: the only byte-identical ds34-family pair; `mips_memcpy`/`mips_memset` themselves read clean (bounds handled by the small-size fall-throughs).

## C. Checked-clean / dissolved

- **mmce `patches/` (139 lines, 5 patches):** dissolved as clutter — ORIGIN.txt documents them as already-applied provenance for the vendored pin, and a test (`test_mmceman_fork.py`, per ORIGIN.txt) checks the tree against the commit. Keeping them is the audit trail, not duplication of code.
- **mmce command builders' uninitialised tail bytes** (e.g. `mmce_cmd_set_card` sends 8 bytes, sets 7): already filed as RIPTOPL-185; not re-filed.
- **mmce game-ID `strcpy` both directions** (`mmce_cmds.c:216,241`): already filed as RIPTOPL-039; the `u8 len = strlen(ptr)+1` truncation at `:230` is RIPTOPL-058's family. Not re-filed.
- **pademu short-transfer scans** (`pademu.c` `td->in_size - 3` loops): subsumed by already-filed RIPTOPL-162; not re-filed.
- **udptty `tty_write` returning `udp_send`'s constant 0:** already filed as RIPTOPL-218. `udp_send` ignoring `lwip_sendto`'s result is the same defect's other half, not a new one.
- **mx4sio `spisd_flush`/`spisd_stop` no-ops:** correct for a write-through SPI SD driver with no cache; dissolved.
- **mx4sio `sd_detect_thread` `while(1)` + `DelayThread(1s)`:** a proper polling thread with a delay, not a spin; the filed spins (-117/-118) are the SIO2-status loops, distinct. Dissolved.
- **mmce_sio2 `while(1)` transfer state machine:** bounded by the waiting-stage timeout return and the `bytes_done >= size` break; dissolved.
- **bdmevent `main.c`:** read clean beyond PADS-10 — the sector-size diagnostic correctly excludes `sectorSize == 0` with an in-code rationale; callback counters and the 20-device snapshot loop are bounded.
- **genvmc create/rebuild paths** (excluding PADS-01/-05): the flag/size/blocksize whitelist in `vmc_create`, the `.new`→`.bin` source derivation, exclusive-create ownership tracking, and abort-flag locking all read as deliberate, commented hardening. No new finding.
- **mcemu device backends** (`device-bdm/hdd/mmce/smb.c`, 177 lines total): thin offset×PageSize forwards; their hazards are the already-filed -069/-157 PageSize/backing issues, not new code defects.
- **freepad:** directory contains **no source** — only `PROVENANCE.md` (46 lines), documenting a vendored `freepad.irx` (36,741 bytes, sha256 `463fcb30…` in the doc) that is **not present in this directory** at this pin; it is installed by `.github/scripts/install_coherent_freepad.sh`. Recorded as a ledger fact, not a finding (the vendoring rationale and hardware-pending status are stated in the doc itself).

## D. Per-file ledger (exact `wc -l`; all listed files read)

| Tree | Files | Lines |
|---|---|---|
| ds34bt | Makefile 11, ee/Makefile 15, ee/libds34bt.c 181, ee/libds34bt.h 41, iop/Makefile 8, iop/ds34bt.c 1719, iop/ds34bt.h 256, iop/imports.lst 34, iop/irx_imports.h 37, iop/smsutils.S 180 | 2,482 |
| ds34usb | Makefile 11, ee/Makefile 15, ee/libds34usb.c 153, ee/libds34usb.h 27, iop/Makefile 13, iop/ds34usb.c 998, iop/ds34usb.h 74, iop/imports.lst 33, iop/irx_imports.h 37, iop/smsutils.S 180 | 1,541 |
| pademu | Makefile 26, ds34bt.c 1425, ds34bt.h 240, ds34common.c 144, ds34usb.c 616, ds34usb.h 88, exports.tab 10, imports.lst 49, irx_imports.h 40, pademu.c 589, pademu.h 60, padmacro.c 256, padmacro.h 27, sys_utils.c 99, sys_utils.h 35 | 3,704 |
| mcemu | Makefile 41, device-bdm.c 36, device-hdd.c 52, device-mmce.c 38, device-smb.c 51, device.h 7, imports.lst 88, ioman_add.h 48, irx_imports.h 28, mcemu.c 931, mcemu.h 225, mcemu_io.c 41, mcemu_rpc.c 152, mcemu_sys.c 188, mcemu_utils.h 94, mcemu_var.c 72 | 2,092 |
| mmce | LICENSE 21, ORIGIN.txt 27, include/ioplib.h 40, include/irx_imports.h 33, include/mmce_cmds.h 61, include/mmce_fs.h 6, include/mmce_sio2.h 48, include/module_debug.h 19, include/sio2man_hook.h 33, include/sio2regs.h 201, patches ×5 (65+20+14+23+17) 139, src/imports.lst 55, src/ioplib.c 101, src/mmce_cmds.c 317, src/mmce_fs.c 1238, src/mmce_sio2.c 592, src/mmceman.c 99, src/sio2man_hook.c 318 | 3,327 |
| mx4sio_bd | LICENSE 167, Makefile 28, ORIGIN.md 43, src/crc16.c 27, src/crc16.h 8, src/imports.lst 58, src/ioplib.c 101, src/ioplib.h 40, src/irx_imports.h 31, src/module_debug.h 24, src/mx4sio.c 847, src/mx4sio.h 78, src/sio2man_hook.c 315, src/sio2man_hook.h 33, src/sio2regs.h 174, src/spi_sdcard_crc7.c 83, src/spi_sdcard_crc7.h 16, src/spi_sdcard_driver.c 736, src/spi_sdcard_driver.h 177, src/spi_sdcard_driver_config.h 25 | 2,844* |
| vmc | genvmc/Makefile 10, genvmc.c 947, genvmc.h 44, imports.lst 62, irx_imports.h 18, vmc_alloc.h 21 | 1,102 |
| debug | ps2link/LICENSE 174, ps2link/Makefile 20, ps2link/cmdHandler.c 395, ps2link/excepHandler.c 115, ps2link/excepHandler.h 13, ps2link/hostlink.h 262, ps2link/imports.lst 85, ps2link/irx_imports.h 31, ps2link/net_fio.c 728, ps2link/net_fio.h 35, ps2link/net_fsys.c 337, ps2link/nprintf.c 81, ps2link/ps2link.c 48, udptty-ingame/Makefile 9, udptty-ingame/exports.tab 9, udptty-ingame/imports.lst 55, udptty-ingame/irx_imports.h 31, udptty-ingame/udptty.c 273 | 2,701 |
| bdmevent | Makefile 8, imports.lst 9, irx_imports.h 23, main.c 88 | 128 |
| freepad | PROVENANCE.md 46 (no source files exist) | 46 |

*mx4sio subtotal as summed from the `wc -l` listing; grand total of all text files above: **20,109 lines** (includes LICENSE/ORIGIN/provenance text; the task's narrower source-only estimate was ~18,776).

Binaries (hashed/sized, not line-readable; prebuilt class already filed as RIPTOPL-032): `modules/debug/deci2.img` 73,625 B md5 `a1b36f64…`; `drvtif.irx` ≡ `drvtif-ingame.irx` 5,985 B md5 `838e4e7a…` (PADS-08); `tifinet.irx` 3,301 B md5 `f212f485…` ≠ `tifinet-ingame.irx` 3,301 B md5 `6c52a13f…`.

**Unread: none named — target zero met for text files in scope.** The five debug binaries are inherently non-line-readable and are accounted above by hash/size.
