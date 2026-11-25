# Sable Audit — RiptOPL — ROUND 5, Section R5-M4

Repo: `~/workspace/audit-clones-2026-11-13/RiptOPL` at pin `2e347d5ac885cd0ff73e9ad4143781946396f492`. READ ONLY — no edits; `git status --porcelain` verified empty at finish.
Scope: usbd-ra (PS2SDK usbd fork) + usb scsi + mx4sio remainder + mmce remainder + mcemu remainder.
READ = read in source at the pin in this pass. INFERRED = depends on protocol/hardware/SDK behaviour not in the tree. No PS2 build or hardware claims; performance magnitudes are structural/INFERRED.

**Provenance note (usbd-ra):** `modules/usb/usbd-ra/Makefile` states in its own header that the C source is PS2SDK `iop/usb/usbd` as of `314d87e7` (01.04.2024), **untouched**; only the Makefile is local (exports.o first, `-DMINI_DRIVER -DUSE_GP_REGISTER=0`). Findings R5-M4-08/-09 below are therefore **inherited upstream shape**, filed because RiptOPL ships and depends on this copy (it exists precisely because current PS2SDK usbd regresses on real hardware, per the same header), not because RiptOPL introduced them.

---

## Coverage ledger (exact)

| File | Lines | Read |
|---|---|---|
| `modules/usb/usbd-ra/src/hub.c` | 737 | 1–737 in full |
| `modules/usb/usbd-ra/src/hcd.c` | 544 | 1–544 in full |
| `modules/usb/usbd-ra/src/interface.c` | 452 | 1–452 in full |
| `modules/usb/usbd-ra/src/usbd.c` | 410 | 1–410 in full |
| `modules/usb/usbd-ra/src/usbio.c` | 319 | 1–319 in full |
| `modules/usb/usbd-ra/src/driver.c` | 283 | 1–283 in full |
| `modules/usb/usbd-ra/src/mem.c` | 259 | 1–259 in full |
| `modules/usb/usbmass_bd/src/scsi.c` | 440 | 1–440 in full |
| `modules/mx4sio_bd/src/spi_sdcard_driver.c` | 736 | 1–736 in full |
| `modules/mx4sio_bd/src/spi_sdcard_crc7.c` | 83 | 1–83 in full |
| `modules/mmce/mmceman/src/mmce_sio2.c` | 592 | 1–592 in full |
| `modules/mmce/mmceman/src/mmce_cmds.c` | 317 | 1–317 in full |
| `modules/mmce/mmceman/src/sio2man_hook.c` | 318 | 1–318 in full |
| `modules/mmce/mmceman/src/ioplib.c` | 101 | 1–101 in full |
| `modules/mmce/mmceman/src/mmceman.c` | 99 | 1–99 in full |
| `modules/mcemu/mcemu_sys.c` | 188 | 1–188 in full |
| `modules/mcemu/mcemu_rpc.c` | 152 | 1–152 in full |
| `modules/mcemu/mcemu_var.c` | 72 | 1–72 in full |
| `modules/mcemu/mcemu_io.c` | 41 | 1–41 in full |
| `modules/mcemu/device-hdd.c` | 52 | 1–52 in full |
| `modules/mcemu/device-smb.c` | 51 | 1–51 in full |
| `modules/mcemu/device-mmce.c` | 38 | 1–38 in full |
| `modules/mcemu/device-bdm.c` | 36 | 1–36 in full |
| **Total** | **6,320** | **23 files, all 1..EOF** |

Supporting reads (mechanism checks only, not audited as sections): `modules/usb/usbd-ra/Makefile` (provenance + build defines), `modules/mx4sio_bd/src/spi_sdcard_driver_config.h`, `modules/mx4sio_bd/src/spi_sdcard_crc7.h`, `modules/mmce/mmceman/include/mmce_sio2.h`, `modules/mcemu/mcemu.h` / `device.h` (ranges cited), `src/platform/system.cpp:411` (usbd load args), tree-wide greps for `mmce_sio2_rx_dma`, `crc7_generate_table`, `DeviceWritePages`, SMB `fid` handling.

---

## Findings (benefit order)

### R5-M4-01 — `scsi_warmup` accepts any non-zero sector size a device reports; the size prints can divide by zero and every transfer length is `sectorSize × count` in 32 bits
- **Category:** 6 (device-supplied value trusted as divisor/multiplier)
- **Locations:** `modules/usb/usbmass_bd/src/scsi.c:243-249` (READ_CAPACITY(10) block length stored; only `== 0` rejected), `:259-261` (READ_CAPACITY(16) block length adopted on a `!= 0` test only), `:253-254` and `:266-267` (capacity prints), `:156` and `:174` (`bd->sectorSize * sectorCount` as the CBW transfer length in `scsi_cmd_rw_sector`)
- **Problem:** READ for the arithmetic. The device-supplied block length is trusted after a single zero check. (1) Both warmup prints compute `sectorCount / ((1000*1000) / sectorSize)` and the `(1024*1024)` twin: for any reported sector size above 1,000,000 the inner division yields 0 and the outer one divides by zero (on the IOP's MIPS core that yields an undefined quotient rather than a trap — INFERRED; the certain impact is garbage diagnostics from live `M_PRINTF` code). (2) The same value multiplies every READ/WRITE transfer length at `:156`/`:174` in 32-bit arithmetic: a device reporting e.g. 0x20000-byte blocks wraps the product for payloads above 32 KiB, so CBW length, BDM buffer expectation and sector count disagree for the life of the device. The in-file comment at `:246-248` shows the zero case was a deliberate local fix — it stops one step short.
- **Fix (words):** Extend the existing check at `:246` from `== 0` to a plausibility test — sector size a power of two in [512, 4096] (every real USB mass-storage block size) — and apply the same test to the capacity-16 value before adopting it at `:261`. *Justification (adds a check): one condition in each of two places closes a divide-by-zero and a per-transfer length wrap driven entirely by device-supplied data.*
- **Benefit:** A broken or hostile device is rejected at warmup instead of corrupting transfer-length arithmetic on every subsequent I/O.
- **Effort:** S · **Risk:** Low (a hypothetical >4096-byte-block device would be rejected; the current code cannot serve one correctly anyway) · **Confidence:** High on the unchecked arithmetic (READ); Medium on real-world trigger (needs a misreporting device).

### R5-M4-02 — mcemu HDD backend: a page that maps to no APA fragment is silently translated to sector 0 — the APA header sector
- **Category:** 6 (missing error path converts an unmapped page into a valid, critical LBA)
- **Locations:** `modules/mcemu/device-hdd.c:11-28` (`Mcpage_to_Apasector`; `sector_to_read` initialised to 0 at `:17` and returned unchanged when no `blocks[i]` range matches), callers `:30-48` (`DeviceWritePage`/`DeviceReadPage` pass the result straight to `sceAtaDmaTransfer`)
- **Problem:** READ. The fragment walk has no "not found" outcome: any page outside all ten fragment ranges — a gap in the table, a corrupt/truncated VMC spec, or a page beyond the image — returns LBA 0, and both callers transfer one sector there unconditionally. For a write, LBA 0 is the HDD's APA partition header sector: the exact sector whose corruption makes the whole partition table unreadable. This is the destructive sink downstream of RIPTOPL-072 (mcemu.c accepts any page number; CardSize never used as a bound) — but it is **not** closed by 072's fix alone, because even a validated-in-range page can fall in a fragment-table gap, and this function still cannot say so.
- **Fix (words):** Give `Mcpage_to_Apasector` a failure result (e.g. return a bool and the sector via pointer, or reserve an error sentinel the callers test) and have `DeviceWritePage`/`DeviceReadPage` return 0 without issuing the DMA transfer when the page is unmapped. *Justification (adds a check): one comparison per caller; there is no existing error path in this file to reuse.*
- **Benefit:** An unmapped VMC page fails one card operation instead of reading — or overwriting — the disk's partition header.
- **Effort:** S · **Risk:** Low (legitimately mapped pages, including a fragment that genuinely starts at sector 0 of its partition area, are distinguished by the found-flag, not the value) · **Confidence:** High on the mechanism (READ); trigger reachability via a gapped/corrupt spec is INFERRED, via RIPTOPL-072's unchecked page numbers is READ in that finding.

### R5-M4-03 — The unplug NULL-priv guard covers scsi warmup commands but not `scsi_cmd_rw_sector` or the read/write chunk loops
- **Category:** 6 (guard applied to one half of a guarded set; race)
- **Locations:** guards present at `modules/usb/usbmass_bd/src/scsi.c:78` (`scsi_cmd`), `:129` (`scsi_cmd_read_capacity16`), `:193`/`:230` (warmup); absent at `:139-176` (`scsi_cmd_rw_sector` dereferences `scsi->queue_cmd` at `:156`/`:174` with `scsi` taken unchecked from `bd->priv` at `:141`) and at `:287` / `:330` (`scsi_read`/`scsi_write` read `scsi->max_sectors` from a possibly-NULL priv on every chunk); priv is nulled on disconnect at `:416` (`scsi_disconnect`)
- **Problem:** READ for the asymmetry; INFERRED for concurrent reachability. The file's own comments (`:78` "unplugged during start-up", `:397` "not unplugged during start-up") show the maintainers did not rely on BDM serialising disconnect against in-flight work during warmup — but the data path re-checks nothing: if a disconnect lands while a multi-chunk read/write is in flight, the next chunk dereferences NULL instead of failing with −EIO. Whether BDM serialises disconnect against in-flight I/O is not established in-tree; if it does, this is latent.
- **Fix (words):** Mirror the existing guard: return −1 from `scsi_cmd_rw_sector` when `bd->priv == NULL` (same shape as `:78`), which also covers both chunk loops since every chunk funnels through it; and test the captured priv once at `scsi_read`/`scsi_write` entry. *Justification (adds checks): two token-level guards copied from the same file's own pattern.*
- **Benefit:** The data path gets the same unplug behaviour the warmup path already promises.
- **Effort:** S · **Risk:** Low (a NULL priv can only mean the device is gone) · **Confidence:** Medium — asymmetry READ, exploitability INFERRED (depends on BDM threading).

### R5-M4-04 — Three MMCE command builders transmit uninitialised stack bytes as packet padding
- **Category:** 6 (uninitialised memory sent on the wire)
- **Locations:** `modules/mmce/mmceman/src/mmce_cmds.c:109-121` (`mmce_cmd_set_card`: `wrbuf[0x8]`, indices 0–6 written, byte 7 sent via `sizeof(wrbuf)`), `:169-180` (`mmce_cmd_set_channel`: `wrbuf[0x7]`, indices 0–5 written, byte 6 sent), `:263-271` (`mmce_cmd_reset`: `wrbuf[0x5]`, indices 0–2 written, bytes 3–4 sent)
- **Problem:** READ. `mmce_sio2_tx_rx_pio` copies exactly `tx_size` bytes from the buffer (mmce_sio2.c:203-205), and these three builders pass `sizeof(wrbuf)` while leaving the tail bytes unwritten — whatever was on the IOP stack goes to the card as packet padding. The sibling `mmce_cmd_ping` (`:14-23`) explicitly zeroes its trailing bytes, which is in-file evidence the convention is zeroed padding, not don't-care. Impact is bounded (the card firmware most likely ignores trailing bytes — INFERRED), but the bytes are nondeterministic and the leak is real.
- **Fix (words):** Zero-initialise the three arrays at declaration (`= {0}`), matching ping's explicit zeroing. No new code paths.
- **Benefit:** Packets are deterministic; no IOP stack content leaves the driver.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — write sets and transmit lengths are both in-tree).

### R5-M4-05 — `mmce_sio2_rx_dma` (66 lines) has no caller anywhere in the tree — dead code in RiptOPL
- **Category:** 1 (dead code)
- **Locations:** `modules/mmce/mmceman/src/mmce_sio2.c:236-302` (function, self-described "Simplified read for sectors / MMCEDRV"); prototype `modules/mmce/mmceman/include/mmce_sio2.h:40`
- **Problem:** READ, caller set established by tree-wide grep at the pin: the only two hits for `mmce_sio2_rx_dma` in the whole repository are the definition and its prototype. Unlike the sister project where an unbuilt driver file still references it, this tree's `modules/mmce/mmceman/src/` contains no `mmcedrv.c` at all — every live transfer goes through `tx_rx_pio` / `rx_mixed` / `tx_mixed`. The function is maintained-looking dead weight (it even participates in the `use_alarm` guard pattern, R5-M4-06, which future readers must keep consistent for a path that never runs).
- **Fix (words):** Delete the function and its prototype.
- **Benefit:** 66 lines plus a prototype gone; one fewer transfer path to audit for SIO2 timeout/alarm behaviour.
- **Effort:** S · **Risk:** Low (grep-verified zero callers; if an MMCEDRV client is ever revived it returns with that client) · **Confidence:** High (READ + grep).

### R5-M4-06 — The `use_alarm` setting is honoured by the SIO2 receive paths and ignored by every transmit path
- **Category:** 8 (half-applied control surface)
- **Locations:** setting written by `mmce_sio2_set_use_alarm` at `modules/mmce/mmceman/src/mmce_sio2.c:176-179`; honoured at `:278-279`/`:291-292` (rx_dma) and `:381-382`/`:394-395` (rx_mixed); ignored at `:211` (`tx_rx_pio` — used by **every** command — calls `SetAlarm` unconditionally) and `:492`, `:533`, `:570` (tx_mixed's three stages, unconditional `SetAlarm`/`CancelAlarm`)
- **Problem:** READ. A caller that disables alarm-based timeouts through the settings surface gets them disabled for reads only; every write chunk and every PIO command still installs the 2 s alarm. Whatever incompatibility the setting exists to avoid (its purpose is not documented in-tree — INFERRED) therefore still applies to exactly half the traffic, and the setting reads as global while being path-local. Since `tx_rx_pio` cannot run alarm-free, no transfer in this driver actually runs end-to-end with alarms off — evidence the setting may be vestigial rather than merely unguarded.
- **Fix (words):** A decision, not just code: either guard tx_mixed's three `SetAlarm`/`CancelAlarm` pairs with the same `if (mmce_sio2_use_alarm)` the rx functions use, or — the LESS direction, supported by the `tx_rx_pio` evidence — delete the setting and its setter/devctl surface entirely. Do not leave it half-applied.
- **Benefit:** The settings surface means what it says, one way or the other.
- **Effort:** S either way · **Risk:** Low · **Confidence:** High (READ asymmetry); the right direction is a maintainer call.

### R5-M4-07 — CRC7 "RAM table" mode is unreachable: both preprocessor tests use a macro name that does not exist, so the config's selected mode silently isn't
- **Category:** 1 / 8 (dead branch + mislabelled configuration)
- **Locations:** `modules/mx4sio_bd/src/spi_sdcard_crc7.c:7` (`#if SD_CRC7_MODE == CRC7_MODE_RAM_TABLE`) and `modules/mx4sio_bd/src/spi_sdcard_crc7.h:10` (same test); the config defines only `SD_CRC7_MODE_RAM_TABLE` (=2) at `modules/mx4sio_bd/src/spi_sdcard_driver_config.h:15` and selects it at `:19-23` (`SD_CRC7_MODE (SD_CRC7_MODE_RAM_TABLE)` when not building a bootloader)
- **Problem:** READ (preprocessor semantics: an undefined identifier in `#if` evaluates to 0). The config selects RAM-table mode (2), the outer test at crc7.c:5 correctly enters the table branch, but the inner test compares 2 against the undefined `CRC7_MODE_RAM_TABLE` (0) and falls into the `#else` ROM-table branch at crc7.c:27. Consequences: (1) the RAM-table array and `crc7_generate_table()` (crc7.c:8-26) are never compiled — dead code carrying a maintenance promise; (2) the header never declares `crc7_generate_table`, and no caller exists anywhere (grep-verified), which is the only reason this works — if anyone "fixes" the typo alone, RAM mode activates with an uninitialised all-zero table and every command CRC breaks; (3) what actually ships is the 256-byte ROM table, which is arguably the better outcome — it just isn't what the config says.
- **Fix (words):** Delete the RAM-table branch (crc7.c:7-26), the config option, and the header's conditional declaration; state ROM-table mode plainly in the config. Net deletion, and it removes the fix-the-typo trap.
- **Benefit:** ~20 dead lines and one config lie removed; the mode the binary actually uses is the mode the config names.
- **Effort:** S · **Risk:** Low (the compiled behaviour — ROM table — is unchanged) · **Confidence:** High (READ; macro expansion is deterministic).

### R5-M4-08 — usbd (inherited PS2SDK): the isochronous halted-endpoint cleanup clears the wrong TD→request LUT slot
- **Category:** 6 (copy-paste index bug; stale LUT entry)
- **Locations:** `modules/usb/usbd-ra/src/usbd.c:139` (inside `processDoneQueue_IsoTd`'s halted-endpoint walk: `memPool.hcIsoTdToIoReqLUT[arg - memPool.hcIsoTdBuf] = NULL;` where the loop variable is `curTd`); correct sibling at `usbd.c:224` (`processDoneQueue_GenTd` clears `[tdListPos - memPool.hcTdBuf]`)
- **Problem:** READ. `arg`'s LUT entry was already cleared at `usbd.c:109` before the walk begins. Each queued IsoTD freed in the walk therefore keeps its LUT entry pointing at a request that is then completed as `USB_RC_ABORTED` and returned to the free pool (or handed to a callback). The stale entry survives until that TD slot is re-queued; any done-queue processing that consults the slot in between resurrects a freed — potentially already reallocated — `IoRequest`. Provenance: **inherited, untouched PS2SDK** per the Makefile header (see provenance note); impact in OPL's own deployment is latent because no in-tree USB client driver uses isochronous endpoints (mass storage is bulk, pads are interrupt — INFERRED from the in-tree drivers).
- **Fix (words):** Clear `[curTd - memPool.hcIsoTdBuf]` at `:139`, matching the GenTd sibling one function below. *Justification (changes a line, adds none): a one-token correction of an index that is provably the wrong slot.*
- **Benefit:** Halted isochronous endpoints leave no stale request pointers behind.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the defect (READ, with the correct pattern in the same file); Low on present-day impact (no ISOC consumer in-tree).

### R5-M4-09 — usbd (inherited PS2SDK): initialisation failures are swallowed at every level, and the main pool allocation is never NULL-checked
- **Category:** 6 / 8 (unchecked allocation; discarded error returns on the init path)
- **Locations:** `modules/usb/usbd-ra/src/hcd.c:380-381` (`memBuf = AllocSysMemory(...)` immediately followed by `memset(memBuf, 0, memSize)` with no NULL test); `hcd.c:361` (`initHardware()` return ignored — it returns −1 on host-controller reset timeout at `:342-344`); `hcd.c:535`/`:538` (`initHcdStructs()` / `initHubDriver()` returns ignored by `hcdInit` — `initHubDriver` *does* check its own allocation and return −1, `hub.c:49-53`); `modules/usb/usbd-ra/src/usbd.c:406` (`hcdInit()` return ignored by `_start`, which returns `MODULE_RESIDENT_END` regardless — even though `hcdInit` returns 1 when interrupt-handler registration fails, `hcd.c:512-516`)
- **Problem:** READ. Three distinct failure shapes converge on the same outcome — the module loads as resident and clients bind to a half-initialised USB stack: (1) pool allocation failure is a memset-to-NULL crash instead of a clean failure; (2) a host controller that never completes reset is driven on anyway; (3) hub-driver allocation failure (the one place that *does* report) is discarded by its caller. Provenance: **inherited, untouched PS2SDK** per the Makefile header.
- **Fix (words):** Check `memBuf` before the memset and propagate: `initHcdStructs` returns the failure, `hcdInit` returns it, `_start` returns `MODULE_NO_RESIDENT_END`. *Justification (adds checks): four return-value tests on an init path that already computes every one of these results — no new machinery, just not discarding them.*
- **Benefit:** A failed USB init fails the module load visibly instead of crashing in memset or leaving pad/storage drivers bound to a dead stack.
- **Effort:** S · **Risk:** Low (success paths untouched) · **Confidence:** High (READ — every discarded return is quoted above).

### R5-M4-10 — MX4SIO multi-block write: the CMD55 and ACMD23 results are assigned and immediately overwritten, never tested
- **Category:** 3 / 6 (dead result assignments around a protocol step)
- **Locations:** `modules/mx4sio_bd/src/spi_sdcard_driver.c:511-518` (`spisd_write_multi_begin`: `results = spisd_send_cmd(CMD55, 0);` at `:513`, overwritten by `results = spisd_send_cmd(ACMD23, count);` at `:514`, overwritten again by `results = spisd_send_cmd(CMD25, sector);` at `:518`)
- **Problem:** READ. The pre-erase sequence exists to be checked — the init path in the same file tests every CMD55/ACMD41 reply (`:191-195`, `:235-239`) — but here neither reply is ever read: if CMD55 fails, ACMD23 is an illegal command to the card; if ACMD23 fails, the write proceeds without pre-erase. The failure is silent in both cases, and the two assignments are dead stores that make the sequence look validated when it is not.
- **Fix (words):** Either test CMD55's reply before issuing ACMD23 and ACMD23's before CMD25 (failing the begin on error, the shape the init loops already use), or — if pre-erase is intentionally best-effort — drop the assignments and comment that. Do not keep the appearance of a check without the check.
- **Benefit:** A card that rejects the pre-erase sequence fails (or is documented as best-effort) at write-begin instead of mid-protocol.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — the overwrite chain is three consecutive statements).

---

## Already filed — not repeated

- **RIPTOPL-039** — `mmce_cmds.c:195-257`: game-ID `strcpy` unbounded in both directions (`mmce_cmd_get_gameid` `:214-217`, `mmce_cmd_set_gameid` `:230-244`, incl. the `u8 len = strlen(ptr)+1` narrowing). Read in full this pass; no new substance beyond what is filed. (R5-M4-04 is a different defect in the same file — uninitialised padding in three *other* commands.)
- **RIPTOPL-049** — `sio2man_hook.c` (318 lines) + `ioplib.c` (101 lines) duplicated between mmceman and mx4sio_bd. Both files re-read in full; duplication stands as filed, nothing new inside either copy met the bar (the mmce copy's `PORT_NR` transfer-hook code is compiled out via the `#if 0` at `sio2man_hook.c:26-29` — that is part of the 5-line diff 049 already counts).
- **RIPTOPL-034** — `#if 0` blocks at `sio2man_hook.c:26` and `usbd.c:200` (the latter still present at the pin, inside `processDoneQueue_GenTd`). Not re-filed.
- **RIPTOPL-069 / RIPTOPL-072** — mcemu PageSize trust against `mcemu_var.c:61-62` buffers, and unchecked page numbers with CardSize never used as a bound (mcemu.c). My files are the support cast only: `mcemu_var.c` and `mcemu_io.c:26-35` (the cspec copy into `MemoryCard`) re-read and consistent with both findings as filed. R5-M4-02 is the distinct HDD-backend sink those findings do not cover.
- **RIPTOPL-082 / RIPTOPL-098** — mcemu.c proper (SioResponse lengths; `hookDmac_request` `_SysAlloc` deref). mcemu.c was round 4's file, not mine; `mcemu_sys.c`'s `_SysAlloc` wrapper itself (`:100-110`) is a faithful pass-through and adds nothing.
- **RIPTOPL-115** — `usb_bulk_get_max_lun` returns an uninitialised signed byte on a short reply (usb_mass.c). Its sole caller is in my `scsi.c:196-197` and behaves exactly as 115 states (logs the value, immediately overwrites `stat`). No new substance.
- **RIPTOPL-117 / RIPTOPL-118** — mx4sio.c's unbounded SIO2 status spins (ISR and PIO helpers). mx4sio.c was not assigned this round; `spi_sdcard_driver.c` only *consumes* those helpers.

---

## Checked and clean (dissolved on full read)

1. **driver.c:250 `SetGP(req->gpSeg)` read after `freeIoRequest(req)`** (callbackThreadFunc) — a use-after-free shape in the source, but this build compiles it out: the local Makefile sets `USE_GP_REGISTER=0`, so the block at `:249-255` does not exist in the shipped IRX. Inherited upstream latent only; not filed for this build. READ (Makefile + source).
2. **usbd.c `ParseOptionInput` (`:295-333`)** — the digit test `(*p - '0') > 9` admits characters below `'0'` (negative `NewValue`) and the accumulator has no overflow bound, so module args could write garbage into `usbConfig`. Dissolved as a live defect: OPL loads this IRX with **zero arguments** (`src/platform/system.cpp:411`, `sysLoadModuleBuffer(&usbd_irx, size_usbd_irx, 0, NULL)`), so the parser never runs in this deployment. Latent for any future arg-passing loader. READ.
3. **hub.c `fetchConfigDescriptors` (`:402-440`)** — device-supplied `wTotalLength` advances `staticDeviceDescEndPtr`, but every transfer is preceded by the bound check at `:427` against `maxStaticDescSize`; an oversized descriptor set gets the device ignored, not overflowed. Dissolved. READ.
4. **hub.c `requestDevDescrptCb` (`:444-463`)** — on a short first read it consumes `desc->bMaxPacketSize0` (byte 7); the first peek is always 8 bytes (`hubPeekDeviceDescriptor`, `:479`), so the byte is within the transferred data. Dissolved. READ.
5. **mmce_sio2.c `rx_mixed` / `tx_mixed` chunking (`:305-411`, `:413-593`)** — traced for sizes 0, 255, 256, 4096, 5000: DMA-element/PIO-remainder split, `offset_pio` bookkeeping and `bytes_done` accounting close exactly on `size` in both directions. Dissolved (the alarm-guard asymmetry inside tx_mixed is R5-M4-06, a separate point). READ. The tx_mixed waiting-stage 2-byte reply is tested only for timeout, never content — without the firmware protocol document that cannot be called a defect (INFERRED, not counted).
6. **mmce_sio2.c `tx_rx_pio` NULL buffers (`:181-234`)** — the unguarded `tx_buf[i]`/`rx_buf[i]` loops are safe at the pin: the only NULL-paired call passes size 0 for that direction (`tx_mixed` waiting stage, `:457`), and every `mmce_cmds.c` caller passes real buffers. Dissolved. READ.
7. **mcemu_rpc.c `hookMcman63` never advances `eeaddr` (`:54-97`)** — looks like a multi-cluster overwrite bug, but the per-chunk `sceSifCallRpc` is synchronous: libmc consumes each 1 KiB staging chunk before the next DMA overwrites it, which is why the receive-side sibling (`hookMcman68`) is the one that must advance its address. Dissolved as a staging-buffer handshake. INFERRED (libmc-side consumption is not in-tree); the non-advance itself is READ.
8. **mcemu_rpc.c `hookMcman62` bind-failure path (`:39-48`)** — frees the block and NULLs `pFastBuf` but leaves `pFastRpcBuf`/`pClientData` pointing into it. Dissolved: both pointers are reassigned on the next successful allocation before any use, and `hookMcman63`/`68` only run after `hookMcman62` has returned 1. READ.
9. **device-smb.c `DeviceWritePage` lacks the read path's lazy `smb_OpenAndX`** (`:15-23` vs `:32-35`) — the spec's `fid` is supplied pre-opened in the EE-filled spec struct (`mcemu.h:117-118` documents the field pairing), and a write with an invalid fid fails closed (returns 0). Dissolved. INFERRED for the EE-side fill; the fail-closed shape is READ.
10. **scsi.c `sectorCount = last_lba` without +1 (`:243`, `:260`)** — suspected one-sector under-report, but the mx4sio backend in the same tree stores a true count (`spi_sdcard_driver.c:380`/`:385`), and no in-scope BDM consumer proves which convention this backend's field must follow. Not filed — recorded so it is not re-hunted without a consumer-side proof. READ/INFERRED.
11. **spi_sdcard_driver.c CSD capacity arithmetic (`:373-386`)** — v1: in-spec SDSC maxima keep `blockNr * blockLen` inside u32; v2: `(c_size + 1) * 1024` is exact for SDHC/SDXC. The `(uint32_t)sector` narrowing at the read/write entries (`:618`, `:689`) matches the SD command argument width, and the `<< 9` byte-addressing shift for non-SDHC is the spec's own encoding. Dissolved. READ.
12. **ioplib.c `ioplib_iterateByName` 8-byte name compare (`:29-32`)** — every in-tree target name is ≤ 8 bytes ("loadcore" is exactly 8, compared without needing a NUL); `ioplib_relinkExports`' unchecked `stub->fno` indexing is a loadcore-internal contract, not data-driven. Dissolved. READ.
13. **mem.c `freeIoRequest` computes `req - memPool.ioReqBufPtr` before its NULL test (`:240-242`)** — UB-shaped pointer arithmetic on a possibly-NULL argument, but no in-tree caller passes NULL and nothing is dereferenced. Below finding threshold. READ.
14. **mmce_cmds.c:46 `//TODO:` above `mmce_cmd_get_status`** — the function is complete and has a live caller (`mmce_fs.c:1095`); the marker is a stray comment, below the RIPTOPL-034 threshold for a new filing. READ.
15. **mcemu_sys.c export-table helpers (`:37-95`)** — `GetExportTableSize(NULL)` returns 0, which guards both `GetExportEntry` and `HookExportEntry` against a NULL table. `CalculateEDC`'s `int` counter vs `u32 size` is unreachable at in-tree magnitudes (page-sized buffers). Clean. READ.
16. **mmceman.c `mmce_init` product-ID guard (`:34-39`)** — `id < 5` exactly matches the 5-entry `mmce_product_ids[]` table (`:18`), including the Unknown entry at index 0. Clean. READ.

**Cleared count: 16.**

---

## Section totals

- Files read in full: **23 files, 6,320 lines** (the assignment text said 21 files; its own list enumerates 23, and the line total 6,320 matches the 23-file list exactly).
- New candidate findings: **10** (R5-M4-01 … R5-M4-10).
- Already-filed items confirmed present, not repeated: 8 IDs/groups (RIPTOPL-034, -039, -049, -069, -072, -082, -098, -115, -117/-118).
- Checked and clean: **16**.
- Forced revisions to prior findings: **none**. R5-M4-02 extends RIPTOPL-072's downstream evidence to a sink 072's stated fix does not by itself close; 072's text is unchanged.
