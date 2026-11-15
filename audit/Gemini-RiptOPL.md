# RiptOPL Audit Report (`Gemini-RiptOPL.md`)

**Repository:** `NathanNeurotic/RiptOPL`  
**Audited commit:** `rebuild/main@2e347d5a` (`2` remote branches, `5` remote tags; local tags `rolling-incebtion`, `rolling-korium`, `rolling-lite`, `rolling-llvm`, `rolling-ra`)  
**Tracked files / Size:** `2,381` files, `101,134,908 B`  
**Host test run (Windows 11 / Python 3.14 / MinGW):** `216/278` `.github/scripts/test_*.py` scripts passed, `50` failed, `12` timed out in `130.3s`. Beyond Windows MinGW `-fsanitize=address,undefined` and POSIX `<sys/select.h>` failures in C/C++ harnesses, Python-level portability failures include `test_package_orbit_neutrino.py` (CRLF `ORBIT-RA.txt`, see `RIPTOPL-006`), `test_selfupdate_host.py` (Windows backslash path match), `test_ripps2_boot_video_mode.py` (`WinError 123` `mc0:` dir), `test_wiki_sync.py` (missing `markdownify`), and `test_ps2build_sdk_watch.py` (`failures=1, errors=6, skipped=2`). Linux `flavours.yml` targets remain unverified locally.

---

## Cross-Repo Notes

Shared multi-repo duplication and driver defects live once in `Gemini-SUMMARY.md`:
- **`CROSS-001`..`CROSS-011`:** POPS/Ember kit pin churn, copied IOP driver trees across `Launchstation`/`RiptOPL`/`POPSLoader`, hardcoded/embedded credentials, `libds34bt`/`libds34usb` duplication and RPC alignment issues, `udpfs` shared core bugs, KELF/MagicGate duplication, release packaging, updater parity, PS2DEV CMake scaffolding, `httpcatalog`/HTTP server parity, and AI instruction clutter.
- **`CROSS-012`:** Shared `ds34usb`, `ds34bt`, and `pademu` USB descriptor traversal, `NULL`, `rpc_sf`/`argv`, `CancelAlarm`, `power_level`, and init-leak defects across `RiptOPL`, `Launchstation`, and `POPSLoader` (menu DualSense vs `usb_pademu` gap remains RiptOPL-local below).
- **`CROSS-013`:** Shared `SMSTCPIP` lwIP `tcp_slowtmr` keepalive UAF, `err_to_errno` signed/unsigned promotion, `tcp_input` `hdrlen`/`pbuf_header`/OOSEQs, `udp_send`/`icmp_*`/`api_lib.c`/`api_msg.c`/`etharp_query` OOM leaks, `ps2ip.c` `netif_name`, and dead `loopif.{c,h}` / `tcpip_callback` shared with `Simple-Media-System` (`tcp_parseopt`, `snd_nxt`, `lwip_recvfrom` `plen`, `lwip_select`, `tcp_keepalive`, `ethip_hdr`, `LWIP_MAX`/`MIN`, `udp_lookup`, and stubbed exports remain RiptOPL-local below).
- **`CROSS-014`:** Shared `smbman` (`modules/network/smbman-ra/`) IOP concurrency, `prepare_path`, missing `fh` checks, `NetShareEnum`/`QueryPathInformation`/`Echo`, `devctl`, `FileTimeToDate`, teardown, and dead `POLLNVAL` bugs shared with `Launchstation` and `Simple-Media-System` (`smb_ReadAndX`, `EncryptionKey[8]`, and `smb_FindFirstNext2` unpatched regressions stay in `RiptOPL` below; `RiptOPL` also links `udptty`/`mprintf` duplicates).
- **`CROSS-015`:** Shared IOP storage bugs across `RiptOPL` and `Launchstation`: `usbd-ra`, `usbmass_bd`, `mx4sio_bd`, `mmceman` (`mmce_cmd_set_gameid`/`get_gameid`, uninit SIO2 `wrbuf`, unlocked `mmce_fs_update_unit`, unaligned DMA, `sio2man_hook`), `ps2hdd_osd`/`apa` (`apaOpen` `clink` UAF + unchecked `fioSema`), 5-file SIO2 duplication between `mmceman` and `mx4sio_bd`, and shared dead `mmcedrv_config.h` (RiptOPL-only `usbmass_bd/scsi.c`, `usbd-ra` `type==0`/`usbd.h` typo/unused `handleIoReqList`, and `hdd_fio.c` dead tail/helpers stay local below).
- **`CROSS-016`:** Shared `udpfs` IOP protocol, `InitPHY`, `!SPD_REG16 & SPD_CAPS_SMAP`, `HandleRxIntr` `NumPacketsReceived`, `CopyToFIFO` TX tail, and `mprintf.h` duplication across `Launchstation`, `RiptOPL`, and `Simple-Media-System` (RiptOPL-only `udprdma` `dummy[32]` stack overflow, `smap-ingame` lowercase hex bug, and 3-way SMAP duplication stay local below).
- **`CROSS-017`:** Shared `ps2ips` (`modules/network/ps2ips/ps2ips.c`), `smb2man` (`usmb2` short-reply stale `u->buf` + `hmac-md5.c` `k_pad[65]` + duplicate `usmb2` crypto vs `smb2auth.c`), `nfsman` (`unfs4`), and identical `linkfile` copies shared with `Launchstation` (`do_setsockopt` stack overflow, `do_gethostbyname` `NULL` deref, and `SMB_DEVCTL_LOGON` premature teardown stay in `RiptOPL` below).

---

## Findings (Sorted by Benefit)

### RIPTOPL-001: Oversized `docs/` (`64.43 MB`) with UI media and completed AI migration checkpoints
- **Category:** 7 (Build/CI/repo clutter)
- **Location(s):** `RiptOPL/docs/assets/` (`53,658,102 B`, `docs/assets/ui/` `52,604,208 B`), `RiptOPL/docs/community-report-evidence/` (`9,525,527 B`), `RiptOPL/docs/*.md` (`70` files, `1,057,610 B`, including `MIGRATION-CHECKPOINT-2026-10-12.md`, `CPP-CONVERSION-PLAN.md`, `UPSTREAM-PARITY-2026-10-12.md`, `EMBER-INTEGRATION-PLAN.md`, `OLD_DETAILED_CHANGELOG`), `RiptOPL/.github/scripts/hostmap.py` (`3,388 B`), `RiptOPL/tools/syntax_sweep.py` (`2,463 B`), `RiptOPL/.github/scripts/irx_manifest.sh` (`3,829 B`)
- **Problem:** `docs/` accounts for `64,433,620 B` (`63.7%` of the working tree), driven by `52.6 MB` of `.gif`/`.mp4` recordings, `9.5 MB` of bug-report images, and finished AI checkpoint/plan docs (the sole references to `hostmap.py`, `syntax_sweep.py`, and `irx_manifest.sh`). Note `.github/workflows/docs-sync.yml` and `test_site_sync.py` read site assets if published from `docs/assets/`.
- **Fix:** Move release/wiki GIFs, MP4s, and evidence images to GitHub Release assets or a separate docs repo, and prune completed plan/changelog docs plus the 3 migration scripts (`9,680 B`).
- **Benefit:** `~63 MB` working-tree reduction plus `9,680 B` of one-off scripts removed.
- **Effort:** S. **Risk:** Low (update `docs-sync.yml` URLs or remove only non-site assets). **Confidence:** High (`READ`).

### RIPTOPL-002: Redundant `ship/Example` payload (`4.52 MB`), duplicate `modules/bdmassault` binaries, and bugs inside vendored `neutrino/`
- **Category:** 1, 6, 7 (Dead code, Bugs and security, Build/CI/repo clutter)
- **Location(s):** `RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/` (`137` files, `5,921,731 B`), `RiptOPL/ship/POPS/` (`39` files, `2,100,274 B`), `RiptOPL/ship/EMBER/ember.elf` (`1,611,956 B`), `RiptOPL/modules/bdmassault/`, `RiptOPL/.github/scripts/package_release_app.py:24-37, 227-260`, `RiptOPL/tools/package_edition.py:136-143`, `RiptOPL/.github/workflows/flavours.yml:361-397`, `RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/neutrino/udpfs_server/{udpfs_server.py:549-565, 723, 737, 774, 1408, 1554-1606, compressed_iso/zso.py:126, 139-140}`
- **Problem:** Packaging reads only `22` of `137` files (`1,752,047 B`) in `ship/Example/`; the other `115` files (`4,169,684 B`) are unused `neutrino/` copies (`78` files, `606,970 B`, containing unreachable code at `udpfs_server.py:558-565`, a `tuple` `self.tx_buffer = ()` crash on reconnect, duplicate ACKs, and unmasked `0x80000000` ZSO offsets at `zso.py:140`) or duplicates of `ship/EMBER/`, `ship/POPS/`, and `bdmassault` (`11` more duplicate IRXs totaling `353,438 B` sit in `ship/POPS/`, forced into sync by `flavours.yml:361-397`).
- **Fix:** Move the `6` POPS templates into `ship/POPS/`, copy `modules/bdmassault/{usbd,usbhdfsd}.irx.*` directly during packaging in `tools/package_edition.py`, and delete the `115` unused/duplicate `ship/Example/` files plus `11` duplicate `ship/POPS/` IRXs (if `neutrino/` is ever retained, reset `self.tx_buffer = []` and mask `0x7fffffff` in `zso.py:140`).
- **Benefit:** `4,523,122 B` (`4.31 MB`, `126` files) removed from git and simplifies the 3-way binary sync CI job.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-003: Permanent `768 KB` `.bss` allocation for `bgmBuffer` even when BGM is disabled
- **Category:** 4 (Size and memory)
- **Location(s):** `RiptOPL/src/core/bgm.cpp:29-30, 72`
- **Problem:** `static char bgmBuffer[BGM_RING_BUFFER_COUNT][BGM_RING_BUFFER_SIZE]` (`192 * 4096`) statically reserves `786,432 B` (`768 KB`) in EE `.bss` for the entire process lifetime even when BGM is off or no audio track exists.
- **Fix:** Allocate `bgmBuffer` dynamically when `BgmPlayer::start()` opens an Ogg stream and free it on stop/unload.
- **Benefit:** Reclaims `786,432 B` (`768 KB`) of static EE RAM whenever BGM is disabled or inactive.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-004: `212 KB` static stacks (`ART`/`ioman`/`menuwatch`), `~120 KB` `.bss` scratch (`diagsave`/`rahash`/`sysneutrino`/etc.), `64 KiB` stack `sharelist`, and `25.6 KB` stack `CheckPS2Logo`
- **Category:** 4, 5 (Size and memory, Performance)
- **Location(s):** `RiptOPL/src/core/diagsave.cpp:17, 21`, `RiptOPL/src/platform/diaglog.cpp:14-16`, `RiptOPL/include/artworker.h:17`, `RiptOPL/src/gfx/artload.cpp:55`, `RiptOPL/src/platform/ioman.cpp:42-44`, `RiptOPL/src/ui/menuwatch.cpp:42-71`, `RiptOPL/src/sources/ethsupport.cpp:829-838`, `RiptOPL/src/sources/ranet.cpp:443, 620-650, 859`, `RiptOPL/src/sources/rahash.cpp:485, 592, 746`, `RiptOPL/src/sources/achievements.cpp:512, 677`, `RiptOPL/src/sources/httpsupport.cpp:482`, `RiptOPL/src/sources/vcdsupport.cpp:3187`, `RiptOPL/src/platform/util.cpp:784-792`, `RiptOPL/src/platform/sysneutrino.cpp:382-385, 637-640`, `RiptOPL/.github/scripts/test_ra_disc.py`
- **Problem:** EE memory is needlessly pinned or spiked across multiple subsystems:
  1. `ART_THREAD_STACK_SIZE` and `ioman` `THREAD_STACK_SIZE` reserve `196,608 B` (`192 KB`), while `__OPLDIAG` reserves `68,864 B` in `menuwatch.cpp` and `diagSave()` reserves `16,384 B` in `.bss` (`diagsave.cpp:21`) solely to snapshot `diagRing`.
  2. Mutually exclusive RA/HTTP/VCD/Neutrino buffers pin `~104 KB` of idle `.bss`: `sysneutrino.cpp` (`16,916 B` TOML rewriters), `vcdsupport.cpp:3187` (`4,096 B`), `achievements.cpp` (`3,072 B`), `httpsupport.cpp:482` (`2,048 B`), `ranet.cpp:443, 859` (`320 B` despite `Ranet::rx_[2048]`; lines `620-650` also `sscanf(rx_ + 8, ...)` twice), and `rahash.cpp` (`64 KB` `g_chunk`, `8 KB` `g_hdl_sector`, `~4 KB+` local `static`s).
  3. `EthSource::update()` puts unaligned `ShareEntry_t sharelist[128]` (`65,536 B`) on the `96 KiB` `ioman` thread stack (`ethsupport.cpp:829-838`), and `CheckPS2Logo()` puts `25,600 B` on the launch stack (`util.cpp:784-792`).
- **Fix:** Stream `diagRing` directly in small chunks without `diagsave`'s `16 KB` snapshot, size `ART`/`ioman` stacks by high-water measurement, convert transient `.bss`/huge-stack buffers (`sharelist` via 64-byte aligned `memalign`, `CheckPS2Logo`, `sysneutrino` TOML, `vcdraModuleStatus`, `achievements`, `httpsupport`, `rahash` updating `test_ra_disc.py`) to short-lived heap/stack storage, and collapse `ranet.cpp`'s double `sscanf`.
- **Justification:** Converting `sharelist[128]` to 64-byte-aligned transient heap allocation (`NULL`-checked) prevents IO stack overflow and enforces hardware DMA alignment.
- **Benefit:** Saves `~40+ KB` permanent `.bss` unconditionally (up to `~120 KB` with `g_chunk`/`menuwatch`), reclaims `~64-128 KB` stack RAM after tuning (`INFERRED`), and prevents a `64 KiB` stack spike on SMB share lists.
- **Effort:** M. **Risk:** Low (`diagsave`/local `.bss`/`sharelist`/`CheckPS2Logo`), Med (thread stacks & `test_ra_disc.py`). **Confidence:** High (`READ`).

### RIPTOPL-005: `ee_core` `SyncDCache((void *)opcode, ...)` passes integer `0x10000025` instead of `&opcode`
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/ee_core/src/patches.c:259-267`
- **Problem:** `apply_capcom_protection_patch` sets `u32 opcode = 0x10000025;` and calls `SyncDCache((void *)opcode, (void *)((unsigned int)&opcode + sizeof(opcode)));` before `smem_write(..., &opcode, ...)`, passing an unrelated constant as the start address instead of `&opcode` (`INFERRED` cache-coherency failure on EE).
- **Fix:** Pass `(void *)&opcode` as the first argument to `SyncDCache`.
- **Benefit:** Fixes D-cache flushing for the Capcom protection IOP patch.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-006: `ee_core` `_strncmp` returns `-1` on matching shorter strings and `modmgr.c` leaks `iopmem` on RPC error/`NOWAIT`
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/ee_core/src/util.c:24-50`, `RiptOPL/ee_core/src/main.c:49-76, 82`, `RiptOPL/ee_core/src/patches.c:918`, `RiptOPL/ee_core/src/modmgr.c:94-133`
- **Problem:** `_strncmp(s1, s2, length)` returns `-1` when two identical strings terminate with `'\0'` before `length` expires (`util.c:42-43`), breaking `13` checks in `main.c` and `patches.c:918` (meanwhile `_strcpy` scans `src` twice via `strlen` + `strncpy`). In `modmgr.c:94-133`, `SifAllocIopHeap(modsize)` (`iopmem`) leaks on `SifCallRpc < 0` (`125-126`) and whenever `mode & SIF_RPC_M_NOWAIT` skips waiting (`128-129`).
- **Fix:** Return `(*a == *b) ? 0 : -1` when reaching `'\0'` in `_strncmp`, rewrite `_strcpy` as a single pass, and free `iopmem` on RPC failure (or wait/track async completion before freeing `iopmem`).
- **Justification:** Prevents EE startup/game-ID comparison failures and scarce IOP heap leaks across module loads.
- **Benefit:** Fixes string equality across `ee_core` boot/patches and closes an IOP heap leak.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-007: `BdmSource`/`EthSource`/`Udfps`/`Support` `vcd`/`bdm`/`cdvdman` unpatched `udprdma` stack overflow, `ps2ips` `do_setsockopt` overflow, and `smbman-ra` `smb_ReadAndX`/`EncryptionKey` overflows
- **Category:** 4, 6 (Size and memory, Bugs and security)
- **Location(s):** `RiptOPL/modules/network/udpfs_shared/src/udprdma.c:185-199`, `RiptOPL/modules/network/udpfs_{bd,fhi,ioman}/src/udprdma_packet.h:89, 103`, `RiptOPL/modules/network/udpfs_shared/src/udpfs_core.c:91`, `RiptOPL/modules/network/ps2ips/ps2ips.c:584, 591-592, 606-612`, `RiptOPL/modules/network/smbman-ra/smb.h:894-895`, `RiptOPL/modules/network/smbman-ra/smb.c:43, 403-404, 1077-1089, 1477-1498`, `RiptOPL/modules/network/smb2man/smb2man.c:1573-1595`
- **Problem:** Beyond the cross-repo baselines in `CROSS-014`/`CROSS-016`/`CROSS-017`, `RiptOPL` still carries four unpatched IOP network safety regressions:
  1. `udprdma.c:185-199` reads up to `60` header bytes (`hdr_word_count * 4`) into stack `uint8_t dummy[32]` (`28`-byte overflow) and writes into `s->rx_hdr_buffer` without checking `hdr_size <= s->rx_hdr_size`.
  2. `ps2ips.c:584, 591-592` copies caller `optlen` bytes into `unsigned char optval[128]` with zero upper or negative bound check in `do_setsockopt`, and `do_gethostbyname` (`606-612`) dereferences `ret->h_addr_list[0]` without checking `!= NULL`.
  3. `smbman-ra/smb.c` lacks Launchstation's `smb_ReadAndX` bounds hardening (`1077-1089` passes network `padding` and `DataLength` straight to `RecvData(..., readbuf, DataLength)` with no clamp against `MAX_SMB_BUF` or caller `nbytes`), copies `NPRsp->KeyLength` into 8-byte `EncryptionKey` without checking `<= 8` (`403-404`), and omits bounds in `smb_FindFirstNext2` (`1477-1498`).
  4. `smb2man.c:1573-1595` calls `smb2man_teardown()` and clears `conn_valid` inside `SMB_DEVCTL_LOGON` before validating `conn_ip` and `smb2man_latin1`.
- **Fix:** Clamp/drain `hdr_size` in `udprdma.c`, bound `do_setsockopt` `0 <= optlen <= 128` and check `h_addr_list[0]` in `ps2ips.c`, port the `smb_ReadAndX` / `KeyLength <= 8` / `FindFirstNext2` bounds checks to `smbman-ra`, and validate `SMB_DEVCTL_LOGON` arguments before tearing down `conn_valid`.
- **Justification:** Closes remote peer/RPC stack and heap buffer overflows and invalid-arg session drops in RiptOPL's IOP network stack.
- **Benefit:** Eliminates four high-impact memory corruption bugs in active UDPFS/SMB1/SMB2/PS2IPS drivers.
- **Effort:** S (`udprdma`/`ps2ips`/`smb2man`), M (`smbman-ra`). **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-008: `SMSTCPIP` `tcp_parseopt` infinite loop/OOB read, `snd_nxt` corruption, `lwip_recvfrom`/`select` bugs, and dead `udp_lookup`/stubs
- **Category:** 1, 5, 6, 8 (Dead code, Performance, Bugs and security, Half-finished features)
- **Location(s):** `RiptOPL/modules/network/SMSTCPIP/tcp_in.c:762-772, 1085-1124`, `RiptOPL/modules/network/SMSTCPIP/sockets.c:417-424, 749, 765`, `RiptOPL/modules/network/SMSTCPIP/include/lwip/sys.h:70, 81`, `RiptOPL/modules/network/SMSTCPIP/tcp_out.c:548-555`, `RiptOPL/modules/network/SMSTCPIP/include/netif/etharp.h:76-81`, `RiptOPL/modules/network/SMSTCPIP/include/lwip/def.h:38-39`, `RiptOPL/modules/network/SMSTCPIP/udp.c:84-139`, `RiptOPL/modules/network/SMSTCPIP/ps2ip.c:268-278`, `RiptOPL/modules/network/SMSTCPIP/exports.tab:1-48`
- **Problem:** Beyond shared defects in `CROSS-013`, `RiptOPL`'s `SMSTCPIP` fork contains `u8_t c` wraparound in `tcp_parseopt()` (`opts[c+1] >= 218` loops forever, plus 3-byte OOB option reads), stores the freed `next` segment's `seqno` into `pcb->snd_nxt` (`tcp_in.c:762-772`), lets negative `plen` in `lwip_recvfrom` underflow `u16_t avail_len`, breaks `FULL_LWIP` `lwip_select` timeouts (`sys_sem_wait_timeout` returns `0`/`1`, never `SYS_ARCH_TIMEOUT = -418`), omits `TCPH_FLAGS_SET` in `tcp_keepalive` and `PACK_STRUCT_STRUCT` on `ethip_hdr` and parentheses on `LWIP_MAX`/`MIN`, leaves broken pointer math in `#ifdef LWIP_DEBUG` `udp_lookup`, and retains `ps2ip_Stub`/`lwip_select` stub exports.
- **Fix:** Widen `c` to `u16_t` with `c + optlen <= max` bounds in `tcp_parseopt`, fix `snd_nxt`, reject `plen < 0`, compare `sys_sem_wait_timeout` properly in `lwip_select`, fix `tcp_keepalive`/`ethip_hdr`/`LWIP_MAX`/`MIN`, and delete or fix dead `udp_lookup` and unused stubs.
- **Justification:** Prevents remote crafted-packet IOP hangs and socket/select corruption under `FULL_LWIP=1` and `INGAME_DRIVER`.
- **Benefit:** Fixes RiptOPL-specific lwIP hangs, memory bounds, and stale debug paths.
- **Effort:** M. **Risk:** Med. **Confidence:** High (`READ`).

### RIPTOPL-009: `bdmfs_fatfs` 9 `NULL` `VolumeStr` crash, OOB `mount_info`, `GET_LBA` `NULL` `bd`/unaligned `u64`, and double mutex overhead
- **Category:** 3, 4, 5, 6 (Overengineering, Size and memory, Performance, Bugs and security)
- **Location(s):** `RiptOPL/modules/bdm/bdmfs_fatfs/fatfs/include/ffconf.h:87, 181, 185-190, 295`, `RiptOPL/modules/bdm/bdmfs_fatfs/fatfs/ff.c:483, 907-936, 3249-3256, 3770-3783`, `RiptOPL/modules/bdm/bdmfs_fatfs/fatfs/ffsystem.c:69-224`, `RiptOPL/modules/bdm/bdmfs_fatfs/src/fs_driver.c:32-34, 65-110, 137-145, 645, 650, 657, 870-875, 888-902`
- **Problem:** `ffconf.h` defines `FF_VOLUMES 10`, `FF_STR_VOLUME_ID 1`, and only `"mass"`, leaving `VolumeStr[1..9] == NULL` so `get_ldnumber()` dereferences `NULL` on any non-`"mass:"` named path (`usb:`, `mc0:`, etc.); `fatfs_fs_driver_get_mounted_bd_from_index` checks `> 10` instead of `< 0 || >= 10`; `USBMASS_IOCTL_GET_LBA` omits `bd == NULL` and writes unaligned `*(u64 *)rdata`; `fs_dread` lacks explicit NUL termination; `fs_reset` ignores `_fs_init_lock()` failure; and `FF_FS_REENTRANT 1` compiles 11 unused FatFs mutexes inside `fs_driver.c`'s existing `_fs_lock()` (while `FF_CODE_PAGE 869` embeds Greek `uc869` tables and `USBMASS_IOCTL_RENAME` returns `-ENOENT` as an uncalled stub).
- **Fix:** Populate or `NULL`-guard `VolumeStr[]`, bound `mount_info_index` `[0..9]`, `NULL`-check `bd` and `memcpy` `u64` in `GET_LBA`, NUL-terminate `fs_dread`, propagate `_fs_init_lock()`, set `FF_FS_REENTRANT 0`, and prune the unused `USBMASS_IOCTL_RENAME` stub.
- **Justification:** Prevents IOP crashes on non-`mass:` paths and detached devices while reclaiming 11 unnecessary IOP kernel semaphores.
- **Benefit:** Closes 4 safety bugs in `bdmfs_fatfs` and removes redundant semaphore locking per FatFs operation.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-010: `bdm` unlocked `g_mount[]`/`g_fs[]` races, GPT OOM returning `0`, inverted LBA underflow, and missing recursion/short-read guards
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/modules/bdm/src/bdm.c:51-193, 207-212`, `RiptOPL/modules/bdm/src/part_driver_gpt.c:38-54, 130, 175`, `RiptOPL/modules/bdm/src/part_driver_mbr.c:48-49, 60-61`, `RiptOPL/modules/bdm/ORIGIN.txt:53-55`
- **Problem:** `bdm_connect_bd`/`bdm_disconnect_bd`/`bdm_try_mount` mutate and destroy `g_mount[]`/`g_fs[]`/`cbd` across driver threads without locking (`bdm_get_bd` also leaves tail pointers uninitialized if `count > MAX_CONNECTIONS`), `part_driver_gpt.c` returns `0` (success) on `AllocSysMemory == NULL`, allows `first_lba > last_lba` underflow, omits `bd->sectorOffset != 0` recursion guard, and both MBR/GPT drivers check `ret < 0` instead of `ret != 1` on sector 0/1 reads.
- **Fix:** Synchronize `g_mount[]`/`g_fs[]` access with an IOP semaphore/critical section, zero-fill trailing `bdm_get_bd` slots, return `-1` on GPT `AllocSysMemory` OOM, enforce `first_lba <= last_lba`, `bd->sectorOffset == 0`, and `ret == 1`.
- **Justification:** Prevents hot-unplug use-after-free races and corrupt partition-table parsing across BDM devices.
- **Benefit:** Hardens BDM device mount/unmount concurrency and GPT/MBR partition parsing.
- **Effort:** M. **Risk:** Med. **Confidence:** High (`READ`).

### RIPTOPL-011: `cdvdman` `bdm_disconnect_bd()` `bdm_io_sema` deadlock, VMC `NULL` `g_bd`, infinite APA/HDPro loops, and `dev9.c` OOB callbacks
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/modules/iopcore/cdvdman/device-bdm.c:106, 112-124, 229-232, 268-309`, `RiptOPL/modules/iopcore/cdvdman/device-hdd.c:22, 70-80`, `RiptOPL/modules/iopcore/cdvdman/hdpro_atad.c:714-719`, `RiptOPL/modules/iopcore/cdvdman/dev9.c:63-67, 164-186, 410-417`
- **Problem:** `bdm_disconnect_bd()` acquires `bdm_io_sema` (`120`) and never calls `SignalSema(bdm_io_sema)`, `bdm_readSector`/`bdm_writeSector` never re-check `g_bd == NULL` under lock (`g_bd_sectors_per_sector = 2048 / bd->sectorSize` is also dead and divides by `0` if zero), `device-hdd.c:70-80` loops infinitely on read errors and trusts `apaHeader.num_partitions` without clamping to `HDL_NUM_PART_SPECS`, `hdpro_atad.c:714-719` spins forever holding `ata_io_sema` on errors, and `dev9.c` registers `intr`/`predma`/`postdma`/`poweroff` callbacks without `0 <= idx < N` bounds checks.
- **Fix:** Always release `bdm_io_sema` in `bdm_disconnect_bd()`, check `g_bd != NULL` under lock, drop dead `g_bd_sectors_per_sector`, add retry limits and clamp `NumParts <= HDL_NUM_PART_SPECS`, and add full `0 <= idx < N` checks in `dev9.c`.
- **Justification:** Eliminates a permanent VMC/disconnect semaphore deadlock, `NULL` device dereference, boot hangs, and callback OOB writes.
- **Benefit:** Hardens in-game BDM/HDD/HDPro/DEV9 backends inside `cdvdman`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-012: `cdvdfsv`/`cdvdman` raw-sector 2048-vs-2328/2340 B DMA overrun, `CDSC_IO_SEMA` `fno == 0` signal, logo unscrambler `lsn >= 13`, and fake `sendSCmd` success
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/modules/iopcore/cdvdfsv/ncmd.c:118-123, 190, 201-215, 288-306, 384-388`, `RiptOPL/modules/iopcore/cdvdman/cdvdman.c:587-598, 882-889, 982-998`
- **Problem:** `cdvd_readee()` reads `2048`-byte sectors (`mode = NULL`) while DMAing `2328`/`2340` bytes per sector to EE (overreading `fsvRbuf`); `cdvd_readiopm()` ignores `*(int *)buf`; `cbrpc_cdvdNcmds()` passes `fno` to `CDSC_IO_SEMA` (`fno == 0` signals `cdrom_io_sema` without prior wait); `cdvdman.c:587-598` unscrambles all `SectorsToRead` bytes whenever `lsn < 13`, corrupting sectors `>= 13`; and `cdvdman_sendSCmd()` returns `1` with uninitialized `out` after exhausting all `2500` retries.
- **Fix:** Pass `&r->mode` (or reject unsupported raw sizes), write `cdvd_readiopm` status, acquire/release `CDSC_IO_SEMA` explicitly with `1`/`0`, cap logo unscrambling to sectors `< 13`, and return `0` when `cdvdman_sendSCmd` times out.
- **Justification:** Stops raw CDVD DMA overreads, semaphore balance corruption, early-disc sector corruption, and bogus S-command success.
- **Benefit:** Fixes four correctness and data-integrity defects in `cdvdman` / `cdvdfsv`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-013: `cdvdman` ISO9660/streaming bugs (`searchfile` underflow, signed `cdrom_read`, inverted `SEEK_END`, 1-sector `dread`, `bankmax == 0`)
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/modules/iopcore/cdvdman/searchfile.c:216-220`, `RiptOPL/modules/iopcore/cdvdman/ioops.c:295-315, 329-343, 369-377, 429-461`, `RiptOPL/modules/iopcore/cdvdman/streaming.c:132`
- **Problem:** `searchfile.c:217-218` underflows `strlen(cdvdman_filepath) - 6` when checking `.PSS` on short paths; `cdrom_read` promotes negative `size` to unsigned `(u32)` and omits `sceCdGetError()` checks; `cdrom_lseek` (`SEEK_END`) subtracts `offset` (`filesize - offset`) and wraps negative positions up to `filesize`; `cdrom_dread` stops after sector `0` (`> 2048 - 34`) and skips all 1-character filenames instead of only `\x00`/`\x01`; and `sceCdStInit` divides by `bankmax` unchecked.
- **Fix:** Guard `len >= 6`, reject `size < 0` and inspect `sceCdGetError()`, fix `SEEK_END` (`filesize + offset` clamped to `[0, filesize]`), advance multi-sector directory offsets in `cdrom_dread` filtering only `\x00`/`\x01`, and check `bankmax != 0`.
- **Justification:** Fixes OOB reads, broken `read`/`lseek`/`dread` POSIX semantics, and divide-by-zero traps inside in-game `cdvdman`.
- **Benefit:** Resolves five filesystem and streaming edge-case bugs in `cdvdman`.
- **Effort:** M. **Risk:** Med. **Confidence:** High (`READ`).

### RIPTOPL-014: `cdvdman` `smb.c` (`2e347d5`) ignores `Ecode`, `u16 FIDs[i] = -1` fails `fd >= 0`, `smb2.c` leaks TCP sync, and `device-smb.c` trusts ISO `NumParts`
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/modules/iopcore/common/cdvd_config.h:57-73`, `RiptOPL/modules/iopcore/cdvdman/smb.c:28, 511, 786, 796, 1158-1162`, `RiptOPL/modules/iopcore/cdvdman/smb2.c:287-319, 989-1019, 1074-1087, 1136-1140`, `RiptOPL/modules/iopcore/cdvdman/device-smb.c:105, 121-131`
- **Problem:** `smb_NegotiateProtocol` and `smb_OpenAndXOnce` test only `Eclass == STATUS_SUCCESS` instead of `SMB_STATUS(h)` (and `786` adds a stray `+ 1` to NetBIOS packet length); `smb_CloseAll` sets `u16 FIDs[i] = -1` (`0xFFFF`), which compares `>= 0` when loaded into `int fd`; `smb2.c` fails to call `smb_Disconnect()` on EOF/timeout/mid-frame errors; and `device-smb.c` allows `NumParts > 1` on single-file ISOs, aliasing `smb_ip`/`smb_share` as `FIDs[1..]`.
- **Fix:** Check full `SMB_STATUS` and remove `+ 1` at `smb.c:786`, track invalid FIDs against `0xFFFF` consistently, disconnect on unrecoverable `smb2.c` framing errors, and enforce `NumParts == 1` for non-USBLD ISOs.
- **Justification:** Prevents masked NTSTATUS errors, bogus `0xFFFF` close storms, SMB2 TCP desync, and credential memory aliasing.
- **Benefit:** Fixes four protocol and handle-safety bugs across SMB1/SMB2 `cdvdman`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-015: `BgmPlayer::quiesce()`/`stop()` signal kernel ID `0` or deleted handles when inactive, plus unchecked `ov_info`
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/include/bgmplayer.h:18, 35-40`, `RiptOPL/src/core/bgm.cpp:720-734, 761-781, 820-881, 915-923`, `RiptOPL/src/app/opl.cpp:1324, 1418`
- **Problem:** Every `deinitEx()` and `deinit()` calls `bgmQuiesce()` -> `BgmPlayer::quiesce()`, which checks only `!gAudioInitialized` (not `live_`) and invokes `SignalSema(0)` / `WakeupThread(0)` (or stale IDs left unreset by `teardown()`) whenever BGM is disabled or failed startup (`BgmPlayer::stop()` similarly omits `!live_`, and `BgmPlayer::start()` skips checking `ov_info() == NULL`).
- **Fix:** Initialize and reset kernel IDs to `-1`, return immediately from `quiesce()`/`stop()` when `!live_`, guard semaphore/thread calls with `>= 0`, and check `ov_info() != NULL`.
- **Justification:** Stops every game launch/exit with BGM off from signaling kernel object ID `0` or recycled kernel handles.
- **Benefit:** Eliminates kernel handle corruption and null-dereference crashes in `BgmPlayer`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-016: `PadManager::start()` overflows `padData_[MAX_PADS]` across multitap ports and `keyPressed()` lacks bounds checks
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/include/padmanager.h:9, 83-84`, `RiptOPL/src/core/pad.cpp:79, 1002, 1036, 1049-1055, 1063, 1170-1190, 1273-1275`
- **Problem:** `PadManager::start()` checks `padCount_ >= MAX_PADS` (`4`) only in the outer `port` loop, letting a multitap on port 1 push `padCount_` up to `6` inside the `slot` loop and corrupt adjacent `PadManager` fields; `keyPressed(int id)` also omits the `0 < id < 17` guard present in `key()`, `keyOn()`, and `setButtonDelay()`.
- **Fix:** Add `&& padCount_ < MAX_PADS` to the inner `slot` loop and add `if ((id <= 0) || (id >= 17)) return 0;` in `PadManager::keyPressed()`.
- **Justification:** Prevents out-of-bounds memory corruption on dual-port multitaps and invalid button lookups.
- **Benefit:** Fixes controller initialization OOB writes and key-lookup OOB reads.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-017: Unlocked `TAR_KIND_ART` index teardown races with Art worker reads; duplicate `tarFind*` loops
- **Category:** 2, 6 (Duplication, Bugs and security)
- **Location(s):** `RiptOPL/src/platform/tar.cpp:65-72, 92-100, 464, 475-610, 620-712`, `RiptOPL/src/gfx/artload.cpp:63-137, 663-665`, `RiptOPL/src/ui/guisettings.cpp:1803-1804`, `RiptOPL/src/app/settingsio.cpp:2479-2480`
- **Problem:** Unlike `TAR_KIND_THM` (`s_thmSema`), `TAR_KIND_ART` runs `tarLoadFile`/`tarFindPrefix*`/`tarRead` on the Art worker concurrently with UI/IO `tarInvalidate(TAR_KIND_ART)` (`tarCloseInternal()` frees `s_index`), creating a cross-thread UAF race. Four `tarFind*` functions also duplicate scan loops (`464-571`).
- **Fix:** Protect all `TAR_KIND_*` state and reads with a per-kind semaphore and unify `tarFind`/`tarFindExact` and `tarFindPrefix`/`tarFindPrefixExact`.
- **Justification:** Closes a cross-thread use-after-free when `art.tar` is invalidated while Art worker loads cover art.
- **Benefit:** Eliminates `art.tar` UAF races and removes ~40 duplicated search lines.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-018: `readFileBuffer()` returns unowned interior pointers on blank `.lng` lines (double-free), plus dangling `lngEnd` and `languages[-1]`
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/src/platform/util.cpp:614-644, 873-874`, `RiptOPL/src/core/lang.cpp:42-53, 163-173, 223-226, 459-473, 545-556`, `RiptOPL/src/ui/guinotify.cpp:134-135`
- **Problem:** When `readFileBuffer()` hits an empty line (`lineSize == 0`), it returns an interior pointer into `fileBuffer->buffer` even when `allocResult == 1`, causing `lngLoadFromFile()` -> `lngFreeFromFile()` and `closeFileBuffer()` to double-free on any blank `.lng` line; `util.cpp:632, 873` and `lang.cpp:167` ignore `malloc`/`calloc` `NULL`; `lngEnd()` leaves dangling `lang_strs`/`guiLangNames` pointers; and `lngGetFilePath(0)` indexes `languages[-1]`.
- **Fix:** Allocate empty strings when `allocResult != 0`, `NULL`-check `malloc`/`calloc`, reset `lang_strs = internalEnglish` / `guiLangNames = NULL` / `guiLangID = 0` in `lngEnd()`, and validate `0 < langID <= nLanguages`.
- **Justification:** Prevents heap double-frees on `.lng` files containing blank lines and OOM/teardown crashes.
- **Benefit:** Eliminates five memory-safety bugs across `util.cpp` and `lang.cpp`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-019: `hddGetSpace()` treats `HDIOC_TOTALSECTOR` errors as multi-TB drives and `hddGetFileBlockInfo()` omits `subs[]`/`inode` bounds checks
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/src/platform/hdd.cpp:15, 34-46, 420-431, 438-469`, `RiptOPL/include/hdd.h:54, 72, 96`, `RiptOPL/include/hddsupport.h:59-67`, `RiptOPL/src/sources/hddsupport.cpp:2555-2564`, `RiptOPL/src/ui/drivestatus.cpp:116`, `RiptOPL/src/ui/guifilemgr.cpp:210`
- **Problem:** `hddGetSpace()` casts negative `fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, ...)` errors directly to `u32`, reporting `> 0` multi-TB capacity; `hddGetFileBlockInfo()` reads `subs[stat.private_4]` with no `subCount`/`APA_MAXSUB` bound (leaving tail stack `parts[]` in `hddsupport.cpp:2555-2564` uninitialized) and does not clamp `inode->number_data <= PFS_INODE_MAX_BLOCKS`; and global `IOBuffer[2048]` lacks synchronization.
- **Fix:** Check signed `int res <= 0` in `hddGetSpace()`, pass `subCount` and guard `stat.private_4 < subCount && stat.private_4 <= APA_MAXSUB` and `inode->number_data <= PFS_INODE_MAX_BLOCKS` in `hddGetFileBlockInfo()`.
- **Justification:** Prevents bogus multi-TB UI reports on APA errors and OOB/uninitialized stack reads on PFS block queries.
- **Benefit:** Fixes HDD capacity error handling and PFS block-info bounds safety.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-020: `IoQueue` leaks kernel semaphore `printSema_` and retains stale pending counters on restart
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/src/platform/ioman.cpp:163-176, 204-219, 441-445`, `RiptOPL/include/ioqueue.h:39-40, 77, 84-85`, `RiptOPL/src/app/autolaunch.cpp:260, 283, 304`
- **Problem:** `IoQueue::workerLoop()` deletes `procSema_` and `endSema_` on exit but leaks `printSema_`, and `IoQueue::start()` does not zero `pendingByType_[]`/`totalByType_[]` after discarded nodes (`INFERRED` leak and stale `ioGetPending()` counts across autolaunch fallback restarts).
- **Fix:** Zero `pendingByType_[]`/`totalByType_[]` in `IoQueue::start()`, and reuse or cleanly delete prior `printSema_` once `workerRunning_ == 0`.
- **Justification:** Prevents kernel semaphore exhaustion and bogus pending-I/O states on `ioInit()` restarts.
- **Benefit:** Fixes semaphore lifecycle and pending-count tracking in `ioman.cpp`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-021: Overlapping CI build matrices (`33` duplicate builds per push/PR) and `94` CI-omitted test scripts
- **Category:** 7 (Build/CI/repo clutter)
- **Location(s):** `RiptOPL/.github/workflows/configurations.yml:1-107`, `RiptOPL/.github/workflows/ps2build.yml:1-102`, `RiptOPL/.github/workflows/rolling-release.yml:1-95`, `RiptOPL/.github/workflows/rolling-llvm.yml:1-90`, `RiptOPL/.github/workflows/flavours.yml:1-493`, `RiptOPL/.github/scripts/test_*.py` (`278` scripts on `2e347d5a`)
- **Problem:** Every push/PR fires `configurations.yml` (`34` builds), `ps2build.yml` (`2` builds), `rolling-release.yml` (`50` GCC builds), and `rolling-llvm.yml` (`33` LLVM builds), where `33` jobs repeat identical `(edition, configuration, toolchain)` triples, while `~94` `.github/scripts/test_*.py` scripts are absent from `.github/workflows/*.yml`.
- **Fix:** Delete `configurations.yml` (folding its `3` unique pairs into `rolling-release.yml`/`rolling-llvm.yml`), restrict `ps2build.yml` to `workflow_dispatch`, and wire or prune the unscheduled tests in `flavours.yml`.
- **Benefit:** Removes `33` redundant CI builds per push/PR and resolves dead-script vs missing-CI drift.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-022: `usbmass_bd/scsi.c` off-by-one `sectorCount`, bit-7 sense-key check, `alloca` risk; `isofs.c` unchecked seek/read/`slash` order; `usbd-ra` typos/OOB
- **Category:** 1, 6 (Dead code, Bugs and security)
- **Location(s):** `RiptOPL/modules/usb/usbmass_bd/src/scsi.c:218, 243, 321`, `RiptOPL/modules/ilink/IEEE1394_bd/src/scsi.c:263`, `RiptOPL/modules/isofs/isofs.c:164-170, 228, 261`, `RiptOPL/modules/usb/usbd-ra/src/hcd.c:192-198`, `RiptOPL/modules/usb/usbd-ra/include/usbd.h:333-334`, `RiptOPL/modules/usb/usbd-ra/src/usbio.h:21, 26`, `RiptOPL/modules/usb/usbd-ra/src/irx_imports.h:26, 28`, `RiptOPL/modules/usb/usbd-ra/include/usbd_macro.h:21`
- **Problem:** Beyond shared IOP USB bugs (`CROSS-015`), RiptOPL carries RiptOPL-local storage/USB defects: `usbmass_bd/src/scsi.c:243` omits `+ 1` on `last_lba` (making `sectorCount` one sector small), checks `sd.error_code == 0x70` without masking bit 7 (`& 0x7f`), and uses `__builtin_alloca(sectorSize + 4)` on the IOP stack; `isofs.c` ignores `longLseek`/`lseek`/`cdEmuRead` errors and computes `(u32)slash - (u32)p` before checking `slash != NULL`; and `usbd-ra` omits `< staticDeviceDescEndPtr` on `type == 0`, misspells `sceUsbd*` autoloader symbols in `usbd.h`, repeats `handleIoReqList`, imports unused `thmsgbx.h`/`vblank.h`, and declares non-`inline` `static` in `usbd_macro.h`.
- **Fix:** Add `+ 1` to `sectorCount`, mask `(sd.error_code & 0x7f) == 0x70`, replace `__builtin_alloca`, validate `isofs.c` seek/read returns and `slash != NULL` first, check `descBuf` end, and fix `usbd.h`/headers in `usbd-ra`.
- **Justification:** Prevents missing the final USB sector, missed SCSI errors, IOP stack overflow, `isofs` null-pointer arithmetic, and broken `usbd.h` autoloader symbols.
- **Benefit:** Resolves 10 RiptOPL-local bugs and header cleanups in `usbmass_bd`, `isofs`, and `usbd-ra`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-023: `genvmc` stats data race, unchecked `GENVMC_DEVCTL_STATUS` buffer, divide-by-zero risks, and wrong card-type boolean
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/modules/vmc/genvmc/genvmc.c:237-258, 336, 372-375, 691-692, 723-728, 769-770, 809-816, 882-888, 898, 921-931`
- **Problem:** `GENVMC_DEVCTL_STATUS` writes `statusVMCparam_t` into `buf` without checking `buf != NULL && buflen >= sizeof(statusVMCparam_t)`, `VMC_create_thread` updates `genvmc_stats` outside `genvmc_io_sema`, `VMC_size_mb`/`VMC_blocksize` and superblock geometry fields are divided (`clusters_per_card / 99`, `pagesize`, `pages_per_cluster`) without non-zero checks, `vmc_mccopy` joins `sceMcDetectCard` and `sceMcGetCardType != 2` with `&&` instead of `||`, `CreateMutex` errors are ignored, and copy errors fall through to format-error text.
- **Fix:** Check `buf`/`buflen`, lock `genvmc_stats` under `genvmc_io_sema`, validate non-zero card/superblock dimensions before dividing, change `&&` to `||` in `vmc_mccopy`, check `CreateMutex`, and distinguish error codes.
- **Justification:** Eliminates user-pointer OOB writes, stats races, divide-by-zero traps, and non-PS2 card misdetection in VMC creation.
- **Benefit:** Hardens `genvmc` creation and MC cloning.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-024: `ps2link` overflows, `printf(buffer)` format bug, EOF spin, and `udptty-ingame` init/teardown leaks
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/modules/debug/ps2link/cmdHandler.c:75-83, 141-154`, `RiptOPL/modules/debug/ps2link/net_fio.c:93-100, 137-146, 402-408`, `RiptOPL/modules/debug/ps2link/nprintf.c:28`, `RiptOPL/modules/debug/udptty-ingame/udptty.c:173-180, 193-204, 216-221`
- **Problem:** `pkoSendSifCmd` overflows `rpc_data[256]` by 4 bytes (`&rpc_data[1]` with `len <= sizeof(rpc_data)`), `pkoExecIop` underflows `argc - 1` when `argc == 0` and scans `argv` unbounded, `net_fio.c` spins forever on `recv() == 0` (also omitting `hlen >= sizeof(pko_pkt_hdr)` and `0 <= nbytes <= length` like `Launchstation/iop/ps2host`), `nprintf.c:28` calls `printf(buffer)`, and `udptty.c` ignores init errors, closes fds `0`/`1` before `AddDrv`, and leaks semaphores/event flags/threads on `_shutdown`.
- **Fix:** Bound `len <= sizeof(rpc_data) - 4`, check `argc > 0` and `argv` length, exit `pko_recv_bytes` on `recv() == 0` with `hlen`/`nbytes` bounds, call `printf("%s", buffer)`, and add error checks/cleanup order in `udptty.c`.
- **Justification:** Closes remote/RPC buffer overflows, format-string UB, EOF busy-loops, and TTY teardown leaks.
- **Benefit:** Hardens in-game debug modules `ps2link` and `udptty-ingame`.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-025: `apemodpatch`/`f2techioppatch` skip `FlushIcache()`, unchecked `CreateSema`/`CreateEventFlag`, and `igrmbox` busy-wait
- **Category:** 5, 6 (Performance, Bugs and security)
- **Location(s):** `RiptOPL/modules/iopcore/patches/apemodpatch/main.c:97, 102, 105-118` (`imports.lst:3`), `RiptOPL/modules/iopcore/patches/f2techioppatch/main.c:48-54`, `RiptOPL/modules/iopcore/patches/iremsndpatch/main.c:84, 93-105`, `RiptOPL/modules/iopcore/igrmbox/igrmbox.c:81-87`
- **Problem:** `apemodpatch` comments out `FlushIcache()` (`118`) and `f2techioppatch` omits it completely after patching instructions in `m->text_start`; `apemodpatch` and `iremsndpatch` ignore `< 0` returns from `CreateSema`/`CreateEventFlag`; and `igrmbox.c:81-87` busy-waits in two tight unyielding/unbounded `sceSifSetDma`/`sceSifDmaStat` loops.
- **Fix:** Call `FlushIcache()` after patching instructions, validate `CreateSema`/`CreateEventFlag` handles, and add `DelayThread()` + retry bounds in `mbSendEE()`.
- **Justification:** Prevents IOP instruction-cache staleness, invalid kernel handle errors, and unyielding SIF DMA loops.
- **Benefit:** Fixes reliability hazards across sound patches and the IGR mailbox.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-026: `ee_core/src/igs_api.c` infinite `DBGCOL_BLNK` error hangs, byte-by-byte `fioPutc` RPCs, and hardcoded `"mc1:/"` / hi-mem literal
- **Category:** 5, 6 (Performance, Bugs and security)
- **Location(s):** `RiptOPL/ee_core/src/igs_api.c:64, 335-372, 403-413, 549-551, 599-666`
- **Problem:** Seven error branches (`412-665`) enter infinite `DBGCOL_BLNK(..., true, ...)` blink loops without calling `fioClose`, `SaveTextFile` (`549-551`) sends ~`1 KB` one byte at a time via `fioPutc` RPCs instead of one `fioWrite`, pixel loops call `FastDelay(1)`/`BGCOLND` per pixel (`335-372`), and `"mc1:/"` plus a raw hi-mem address (`64`) are hardcoded.
- **Fix:** Replace infinite error loops with finite blinks + `fioClose` + error return, batch `SaveTextFile` with `fioWrite`, strip or gate `FastDelay(1)`, and replace hardcoded `"mc1:/"` / literal addresses.
- **Justification:** Prevents permanent console freezes and leaked descriptors on screenshot/dump failures.
- **Benefit:** Eliminates ~1,000 SIF RPCs per text dump and avoids fatal screenshot error hangs.
- **Effort:** M. **Risk:** Med. **Confidence:** High (`READ`).

### RIPTOPL-027: Windows packaging writes CRLF `ORBIT-RA.txt`, silently breaking RA-Neutrino runtime detection
- **Category:** 6 (Bugs and security)
- **Location(s):** `RiptOPL/tools/package_edition.py:57-62`, `RiptOPL/src/platform/sysneutrino.cpp:1054-1060`, `RiptOPL/.github/scripts/test_package_orbit_neutrino.py:56-66`
- **Problem:** `stage_orbit()` writes `"ORBIT-RA.txt"` via `write_text(..., encoding="utf-8")` without `newline="\n"`, producing `\r\n` on Windows hosts and failing `sysNeutrinoRARuntime()`'s exact `memcmp(..., 28)` `\n` check.
- **Fix:** Pass `newline="\n"` (or write UTF-8 bytes) in `stage_orbit()`.
- **Benefit:** Fixes Windows kit packaging of `ORBIT-RA.txt` and resolves the failing `test_package_orbit_neutrino.py` host test.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ` + test failure observed).

### RIPTOPL-028: `Gui` UI crash/state bugs (`menuHandleInputGameMenu` NULL, `guinotify.cpp` `strchr`+1 & timer wrap, `guigame` leak & missing `$Neutrino*` clear, 31-char truncation, `strncpy`, `SettingsShell` discard)
- **Category:** 4, 6 (Size and memory, Bugs and security)
- **Location(s):** `RiptOPL/src/ui/menusys.cpp:2928-2932, 3133-3261, 3339-3345`, `RiptOPL/src/ui/guigame.cpp:248-262, 293, 511-545, 562, 598, 819, 2012-2051, 2121-2172`, `RiptOPL/src/ui/guinotify.cpp:89, 110-120, 148, 178-190`, `RiptOPL/src/ui/gui.cpp:171-176`, `RiptOPL/src/ui/diaeditors.cpp:130`, `RiptOPL/include/dia.h:56-61`, `RiptOPL/include/opl.h:445, 484`, `RiptOPL/src/ui/dialogs.cpp:478, 2225`, `RiptOPL/src/ui/guisettings.cpp:1851-1865, 1946-1979, 2012-2034, 2171-2195, 2375-2410, 2697-2776, 3043-3099, 3243-3247`
- **Problem:** Seven UI correctness and memory-safety bugs affect the menu:
  1. `menuHandleInputGameMenu` (`menusys.cpp:3206-3260`) dereferences `selected_item->item->current->item.id` without checking `!= NULL` under concurrent list refreshes.
  2. `guiGameResolveEffectiveCore` (`guigame.cpp:248-262`) leaks `config_set_t *gameConfig` whenever `configSet == NULL`.
  3. `Notifications::show` (`guinotify.cpp:110-115, 178-190`) increments `strchr(gOPLPart, ':')` before checking `!= NULL` (reading `0x1` if `':'` is absent) and checks `clock() >= raNoticeTimer_` without wrap-safe unsigned subtraction.
  4. `Gui::showKeyboard` (`gui.cpp:171-176`) and `guiGameShowVMCConfig` (`guigame.cpp:511-545`) call `strncpy` without NUL termination (and leave `vmc[32]` uninitialized when empty or risk `maxLength <= 0` VLA).
  5. `CFG_DEFAULT_BGM_PATH` (`char[128]`) and `CFG_CUSTOMCFGPATH` (`char[64]`) bind to 32-byte `UI_STRING` controls (`dia.h:56-61`) without long-path preservation, truncating paths at 31 chars when saving audio/general settings.
  6. `guiGameRemoveSettings` (`guigame.cpp:2121-2172`) omits removing the four `CONFIG_ITEM_NEUTRINO_*` keys.
  7. `SettingsShell` "Exit without saving" (`guisettings.cpp:3243-3247`) calls `restoreSourceState()` without restoring live Audio/General/Interface/Color/Controller globals or `CONFIG_OPL_THEME`.
- **Fix:** Guard `selected_item->item->current` (or snapshot via `menuRowCopy`), call `configFree(gameConfig)`, check `col_pos != NULL` and use wrap-safe timer math, initialize/NUL-terminate keyboard and VMC buffers, preserve long BGM/custom-config strings, call `configRemoveKey` for `CONFIG_ITEM_NEUTRINO_*`, and restore/reload non-source globals on Settings discard.
- **Justification:** Stops NULL crashes, invalid pointer reads, heap leaks, unterminated string UB, silent path clipping, and partial settings discard.
- **Benefit:** Fixes seven UI correctness and memory bugs in `src/ui/`.
- **Effort:** S (1-6), M (7). **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-029: 4x duplicated ISO9660/DVD-DL/ZSO geometry probe causes up to 5 sequential opens per launch and leaves `probed_fd` dangling
- **Category:** 2, 5, 6 (Duplication, Performance, Bugs and security)
- **Location(s):** `RiptOPL/src/sources/bdmlaunch.cpp:838-880`, `RiptOPL/src/sources/ethsupport.cpp:1453-1501`, `RiptOPL/src/sources/mmcesupport.cpp:1484-1532`, `RiptOPL/src/sources/udpfssupport.cpp:939-965`, `RiptOPL/src/sources/supportbase.cpp:731-815`
- **Problem:** Four backends duplicate a 35-to-50 line launch probe sequence opening and closing `part 0` up to 5 times in a row (size, `CheckPS2Logo`, `sbGetISO9660MaxLBA`, `sbGetMediaLsnCount`, `sbProbeISO9660`). Meanwhile `sbGetMediaLsnCount()` (`supportbase.cpp:750-760`) calls `ProbeZISO(fd)` and closes `fd`, leaving global `probed_fd` (`742-743`) pointing at the closed descriptor.
- **Fix:** Extract one `sbResolveDiscGeometry(...)` helper in `supportbase.cpp` keeping `part 0` open once across size/ZSO/logo/MaxLBA/PVD probes, and clear `probed_fd = -1` (or pass state cleanly) on close.
- **Benefit:** Removes `~130` duplicated lines, cuts `2` to `4` redundant `open`+`ProbeZISO` cycles per MMCE/SMB/UDPFS launch, and eliminates a dangling global fd.
- **Effort:** M. **Risk:** Low-Med. **Confidence:** High (`READ`).

### RIPTOPL-030: Broken `pccf_hw_start` `700 ms`..`1.5 s` no-op probe stall, synchronous sector re-reads, `8 KiB` boot-only `.bss`, and 407-line `ata_identify.h`
- **Category:** 3, 4, 5 (Overengineering, Size and memory, Performance)
- **Location(s):** `RiptOPL/modules/pccf/pccf_hw.c:180, 279-303`, `RiptOPL/modules/pccf/pcdiag_cf.c:42, 68`, `RiptOPL/modules/pccf/pccf_ata.c:230, 266-269`, `RiptOPL/modules/pccf/pccf_pio_profile.inc:3-28`, `RiptOPL/modules/hdd/xhdd/ata_identify.h:1-407`, `RiptOPL/modules/hdd/xhdd/xhdd.c:72-81`, `RiptOPL/modules/mcemu/mcemu.c:489-494`
- **Problem:** `pccf_hw_start` sleeps `700 ms` (`> 1.5 s` on recursive retry) executing 90 MMIO reads solely to feed a no-op `quiet_trace` callback; `pccf_ata.c` re-reads every written sector for verification (`pio_write_sector` also hardcodes `0x001a10bbU` instead of `pio_data_timing`); `pccf_pio_profile.inc` pins `8,192 B` `.bss` for two hardcoded CF strings; `xhdd/ata_identify.h` imports 407 lines to read 1 field; and `mcemu.c:489-494` calls unchecked `_SysAlloc(sizeof(Sio2Packet))` with interrupts disabled for a single global packet.
- **Fix:** Remove the no-op `pcdiag_cf_probe(..., quiet_trace)` call and non-debug write verification, use `pio_data_timing`, free or drop `pccf_pio_profile.inc`, replace `ata_identify.h` with one offset constant, and make `mcemu.c`'s `temp_packet` static.
- **Benefit:** Cuts `700 ms` to `1.5 s` of dead CompactFlash init stall, removes `8 KiB` IOP `.bss` + `407` lines of header bloat, and fixes an unchecked `_SysAlloc` ISR crash vector.
- **Effort:** S. **Risk:** Low (`xhdd`/`mcemu`/`pccf_pio_profile`), Med (`pccf_hw_start`). **Confidence:** High (`READ`).

### RIPTOPL-031: Dead legacy `Makefile` (`1,260` lines), obsolete `third_party/ps2sdk-socket/`, `IOPRP.img`, sub-Makefile drift, and `4` orphaned scripts
- **Category:** 1, 7, 8 (Dead code, Build/CI/repo clutter, Half-finished features)
- **Location(s):** `RiptOPL/Makefile:1-1260` (`841, 990-1019, 1043, 1179`), `RiptOPL/third_party/ps2sdk-socket/` (`22,105 B`), `RiptOPL/tools/build_edition.py:125-126, 253`, `RiptOPL/docs/PS2BUILD-MIGRATION.md:3, 75`, `RiptOPL/modules/iopcore/IOPRP.img` (`244 B`), `RiptOPL/modules/iopcore/cdvdman/Makefile:1-88`, `RiptOPL/modules/bdm/Makefile`, `RiptOPL/modules/bdm/libbdm/Makefile`, `RiptOPL/modules/network/udpfs_ministack/Makefile:9`, `RiptOPL/tools/{download_cfla.sh, lang_decompiler.py}`, `RiptOPL/.github/scripts/{release_channel.sh, asset_version.sh}`, `RiptOPL/.github/scripts/test_smb1_vmc_share.py`, `RiptOPL/.github/workflows/flavours.yml:186-187, 401`
- **Problem:** Root `Makefile` (`1,260` lines at `2e347d5a`) cannot compile the C++ UI after the `ps2build` migration (`flavours.yml:401`), references SDK IRXs instead of vendored forks (`bdmfs_fatfs`, `hdproatad`), is the sole consumer of `244`-byte `IOPRP.img` and `download_cfla.sh`/`release_channel.sh`, while at `2e347d5a` `.github/scripts/test_{smb1_vmc_share,apa_table_safety,bdm_index,menu_crash_catcher,ripps2_art_port,udpfs_files_oplcore}.py`, `tools/lng_pack.sh:23-27`, and `Makefile:990-1019` still parse/run against root `Makefile`. Meanwhile `third_party/ps2sdk-socket/` (`22,105 B`) is superseded by `sdk-core v2026.10.13`, `cdvdman/Makefile` lacks `smb2`/`nfs`/`pccf` targets, `udpfs_ministack/Makefile:9` omits `ministack_xchg.o`, and `lang_decompiler.py` / `asset_version.sh` have zero callers.
- **Fix:** Decouple `test_smb1_vmc_share.py` (and the remaining test/pack scripts) from root `Makefile`, then delete root `Makefile`, `third_party/ps2sdk-socket/`, `tools/build_edition.py:253`, `IOPRP.img`, stale sub-Makefiles (or sync `udpfs_ministack/Makefile:9`), and the `4` orphaned scripts.
- **Benefit:** Eliminates `1,260` lines of stale root `Makefile` + `26.4 KB` of dead files and resolves SDK-vs-fork build drift.
- **Effort:** M (decoupling `Makefile` test targets), S (remaining deletions). **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-032: `smap-ingame` broken lowercase hex parser, 3-way SMAP driver bugs & intra-`modules/network/` duplication
- **Category:** 1, 2, 3, 6 (Dead code, Duplication, Overengineering, Bugs and security)
- **Location(s):** `RiptOPL/modules/network/smap-ingame/smap.c:130-139, 486-487`, `RiptOPL/modules/network/smap_udpbd/src/{smap.c:139-273, 449, 522, xfer.c:80-151, udptty.h}`, `RiptOPL/modules/network/udpfs_shared/`, `RiptOPL/modules/network/udpfs_{bd,fhi,ioman}/src/{udpfs_core.{c,h}, udprdma.{c,h}, udpfs_packet.h, udprdma_packet.h, udprdma_seq.h, irx_imports.h, main.c, exports.tab}`, `RiptOPL/modules/network/udpfs_ministack/include/udptty.h`, `RiptOPL/modules/network/smbman-ra/auth.c:19-93`, `RiptOPL/modules/network/smbinit/smbauth.c:21-97`
- **Problem:** Beyond the shared `udpfs_smap` defects in `CROSS-016`:
  1. `smap-ingame/smap.c:486-487` corrupts lowercase `'a'..'f'` MAC hex via `character - 'a' - 0x57` and shares `InitPHY`'s `if (i <= 0) return -1;` bug; `smap_udpbd/src/xfer.c:80-106` retains unpatched `(u32 *)` RX/TX tails and shared SMAP driver bugs across three parallel SMAP trees (`smap-ingame/`, `smap_udpbd/src/`, `udpfs_smap/src/`).
  2. `udpfs_{bd,fhi,ioman}` duplicate `69.3 KB` of identical headers (`~46.2 KB` excess across 21 files) plus `6` forwarding `.c` stubs, `udpfs_bd` == `udpfs_ioman` `main.c` (`947 B`) & `exports.tab` (`295 B`), `udptty.h` (`62 B`), and DES/MD4 NTLM crypto across `smbman-ra/auth.c` and `smbinit/smbauth.c`.
- **Fix:** Fix lowercase hex parsing in `smap-ingame` and RX/TX/PHY bugs in `smap_udpbd`, share common SMAP/UDPFS/NTLM headers and implementations under `udpfs_shared/`/`common/`, and remove duplicate forwarding stubs.
- **Justification:** Fixing lowercase hex and RX `(u32 *)` tails restores MAC overrides and prevents unaligned DMA/tail corruption.
- **Benefit:** Fixes MAC parsing and RX tails while removing `~46.2+ KB` of duplicated UDPFS/SMAP files and stopping bug drift across 3 SMAP forks.
- **Effort:** S (UDPFS headers/hex fix), L (3-way SMAP merge). **Risk:** Low (`udpfs_shared`/hex), Med (SMAP merge). **Confidence:** High (`READ`).

### RIPTOPL-033: Intra-repo IOP duplication (`ilink` vs `ilink_bd_assault` `32` files, `3x smsutils.S`, `ds34*` vs `pademu`, `cdvdman` `atad`/`smb`/`mcemu`)
- **Category:** 2 (Duplication)
- **Location(s):** `RiptOPL/modules/ilink/{IEEE1394_bd,iLinkman}` (`28` files, `4,757 L`, `184,290 B`) vs `RiptOPL/modules/bdmassault/ilink_bd_assault` (`32` files, `4,677 L`, `181,566 B`), `RiptOPL/modules/ds34bt/iop/smsutils.S` == `RiptOPL/modules/ds34usb/iop/smsutils.S` == `RiptOPL/modules/iopcore/cdvdman/smsutils.S` (`180 L` each) vs `RiptOPL/modules/network/SMSUTILS/SMSUTILS.S` (`217 L`), `RiptOPL/modules/ds34{bt,usb}/iop/` vs `RiptOPL/modules/pademu/{ds34bt.c,ds34usb.c,ds34common.c:13-23,89-99}`, `RiptOPL/modules/mcemu/{ioman_add.h,mcemu_sys.c}` vs `RiptOPL/modules/iopcore/cdvdman/ioman_add.h` & `RiptOPL/modules/pademu/sys_utils.c`, `RiptOPL/modules/iopcore/cdvdman/{atad.c,hdpro_atad.c,smb.c,smb2.c,nfs.c}`
- **Problem:** `RiptOPL/modules/` maintains parallel intra-repo forks: full `ilink` vs `ilink_bd_assault` stacks (`4,677 L`), four copies of `smsutils.S`, separate menu `ds34*` vs in-game `pademu` BT/USB HID stacks (repeating `dpad_mapping[]` inside `ds34common.c`), duplicated `ioman_add.h` / `sys_utils.c`, and duplicated ATA/SMB helpers (`atad.c` vs `hdpro_atad.c` `336` matching lines, `smb.c` vs `smb2.c` vs `nfs.c` `smb_ReadCD`/`smb_CloseAll`). *(Note `mmceman`/`mx4sio_bd` 5-file SIO2 duplication is consolidated with `Launchstation` in `CROSS-015`.)*
- **Fix:** Derive `ilink_bd_assault` from shared `modules/ilink` sources, point `cdvdman`/`ds34*` to one `smsutils.S`, share HID transport helpers between `ds34*` and `pademu`, collapse `dpad_mapping[]` / `ioman_add.h` / `sys_utils.c`, and extract shared `cdvdman` ATA/SMB helpers.
- **Benefit:** Eliminates thousands of duplicated IOP lines and keeps driver/HID bug fixes synchronized across targets.
- **Effort:** M-L. **Risk:** Low-Med. **Confidence:** High (`READ`).

### RIPTOPL-034: `FavSource::ps1SaveDir`/`rowPrefix` stale `entry->id` & cold `HddSource` failure; `vcdScanOpenDir` missing cache invalidation; `sbPrepare()` scratch IRX forcing `smb_cdvdman` in `RIPTOPLITE_UDPFS`
- **Category:** 3, 4, 6 (Overengineering, Size and memory, Bugs and security)
- **Location(s):** `RiptOPL/src/sources/favsupport.cpp:700-728, 1800-1833`, `RiptOPL/src/sources/hddsupport.cpp:2324, 3316-3336, 3370`, `RiptOPL/src/sources/vcdsupport.cpp:626-742, 1031-1032`, `RiptOPL/src/sources/udpfssupport.cpp:1099-1120`, `RiptOPL/src/sources/mmcesupport.cpp:1374-1376`, `RiptOPL/src/sources/ethsupport.cpp:1229-1231`, `RiptOPL/src/sources/supportbase.cpp:1004-1110`, `RiptOPL/tools/lite_devices.py:35-39`
- **Problem:** Three source-layer defects affect launch and PS1 browsing:
  1. Name-addressed PS1 Favourites pass stale `entry->id` into `owner`'s `ps1RowKind()` (wrong row or OOB after BDM slot shifts/rescans), and `HddSource::ps1SaveDir()` fails cold when `vcdParts == NULL` instead of calling `buildVcdGameList()`.
  2. `vcdScanOpenDir()` omits `cacheInvalidateFailMemo()` despite `vcdsupport.cpp:1031-1032` stating it moved there.
  3. Neutrino launch paths pass `&smb_cdvdman_irx` / `&mmce_cdvdman_irx` as dummy scratch buffers to `sbPrepare()`, forcing `RIPTOPLITE_UDPFS` (`tools/lite_devices.py:35-39`) to embed an otherwise unused `smb_cdvdman` IRX.
- **Fix:** Resolve current row index (and call `buildVcdGameList()` when needed) for `FAV_KIND_VCD`/`CUE`, call `cacheInvalidateFailMemo()` on `vcdScanOpenDir` success, and support `cdvdman_irx == NULL` in `sbPrepare()`/`sbCheatsMissingContinue()`/`sbUnprepare()` so `RIPTOPLITE_UDPFS` drops `"smb_cdvdman"`.
- **Justification:** Minimal guards/calls restore PS1 Favourites/artwork invalidation and let Lite UDPFS remove an unused IRX binary.
- **Benefit:** Eliminates unused `smb_cdvdman` from `RIPTOPLITE_UDPFS` and fixes PS1 Favourites + VCD artwork cache invalidation.
- **Effort:** M. **Risk:** Low-Med (update `test_neutrino_cheats.py`/`test_udpfs_files_oplcore.py`). **Confidence:** High (`READ`).

### RIPTOPL-035: Redundant `O(N)` UI backdrop `guiShow()` behind opaque `gColBlack`, blocking I/O RPCs, and unthrottled `nettime` spin
- **Category:** 2, 5 (Duplication, Performance)
- **Location(s):** `RiptOPL/src/gfx/renderman.cpp:122`, `RiptOPL/src/ui/guiwait.cpp:102-112, 200-214`, `RiptOPL/src/ui/guimodal.cpp:224-228, 272-278, 304-309, 394-400`, `RiptOPL/src/sources/hddsupport.cpp:1328-1333`, `RiptOPL/src/sources/vcdsupport.cpp:1769-1848`, `RiptOPL/src/sources/ranet.cpp:443, 453-454`, `RiptOPL/src/sources/ratls_ps2.cpp:41-48, 149-172`, `RiptOPL/src/sources/nettime.cpp:31-38, 73-76, 109-120, 138-140`
- **Problem:** `guiRenderTextScreen` (called every frame in wait/download loops) and four modal `draw()` methods call `guiShow()` right before covering the whole screen with opaque black `gColBlack` (`alpha 0x80`); `hddsupport.cpp:1328` mounts/unmounts `pfs1:` right before `fileXioMount`; `vcdsupport.cpp` runs 3 separate MC/BDMA directory scans on VCD launch; `ranet.cpp:453` calls `MSTACK_DEVCTL_GET_IP` four times for 4 bytes; and `nettime.cpp`/`ratls_ps2.cpp`/`ranet.cpp` triplicate PS2IPS DMA-aligned `tcpRecv`/clock/`nicReserved` helpers while `nettime.cpp:119-120` loops without `DelayThread`.
- **Fix:** Drop `guiShow()` before full-screen `gColBlack` fills, remove `hddPartitionMountableAt` before `fileXioMount`, pass BDMA probe results across the VCD helpers, fetch `MSTACK_DEVCTL_GET_IP` once, and extract one PS2IPS helper with `DelayThread` throttling on non-blocking retry.
- **Justification:** Adding `DelayThread` inside the unified retry helper eliminates a busy spin loop on slow NTP replies.
- **Benefit:** Removes wasted per-frame menu/art redraws during wait screens, cuts synchronous IOP/MC RPCs on boot/VCD launch, and removes 3-way socket duplication.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-036: `iso2opl` DVD9 `lsn * 2048` 32-bit overflow, `systemcnf_buf`/`ElfPath[8]` overflows, `opl2iso` unchecked `ftell`/`fread`, `ziso.py` infinite loop, and `genvmc` CLI bug
- **Category:** 1, 6 (Dead code, Bugs and security)
- **Location(s):** `RiptOPL/tools/pc/iso2opl/src/{isofs.c:195, 663-669, iso2opl.c:16, 338-341, 425-427}`, `RiptOPL/tools/pc/opl2iso/src/opl2iso.c:91-148, 234-260`, `RiptOPL/tools/pc/ziso.py:44-47, 62-71, 139, 223-233, 348-400`, `RiptOPL/tools/pc/genvmc/src/genvmc.c:195-196, 246, 251, 399-406`, `RiptOPL/tools/pc/http/tests/{test_catalog.c:30, 44-46, 89-92, run.sh:21, 42-45}`
- **Problem:** PC conversion/test utilities hold six memory/control bugs: `iso2opl` overflows `lsn * 2048` on DVD9 ISOs, reads unbounded `fsize` into `systemcnf_buf[65536]`, and dereferences `ElfPath[8]` / `strstr(";1")` unchecked; `opl2iso` casts negative `ftell` to `size_t`, ignores `fread`, fails without `CD`/`DVD` dirs, and loops on `feof`; `ziso.py` loops forever on empty compressed input, overrides `-a`, drops partial trailing blocks, fails tuple `%` formatting, and keeps dead `hexdump`/`load_sector_table`; `genvmc` falls through after `printUsage()`, `strcat`s across `mcdi->magic` into `version`, and over-allocates `+ 0xFF`; and `test_catalog.c` checks only `512` of `2048` `seen` rows (with `run.sh` using predictable `/tmp` paths).
- **Fix:** Cast `(s64)lsn`, bound `systemcnf_buf` and `ElfPath`, check `ftell`/`fread`/positive byte counts, fix `ziso.py` loop/`-a`/ceiling/dead code, exit after `printUsage()` and separate struct copies in `genvmc`, raise `seen` to `2048` (`CATALOG_ROWS_MAX`), and use `mktemp`.
- **Justification:** Prevents crashes, DVD9 corruption, host tool hangs, and truncated test validation in shipped PC utilities.
- **Benefit:** Fixes memory safety and data-corruption bugs across four PC tools and HTTP catalog tests.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-037: `iso2opl`/`opl2iso` signed-`char` OOB read in `crc32()` and duplicate code
- **Category:** 2, 6 (Duplication, Bugs and security)
- **Location(s):** `RiptOPL/tools/pc/iso2opl/src/iso2opl.c:15, 48-71`, `RiptOPL/tools/pc/opl2iso/src/opl2iso.c:14, 43-66` (`src/platform/system.cpp:465-492`)
- **Problem:** Both `iso2opl` and `opl2iso` copy-paste a `crc32()` indexing `crctab[]` with signed `char`, reading negative indices on UTF-8 bytes `>= 0x80`, without explicit `crc` initialization, and rebuilding the 256-entry table every call.
- **Fix:** Share one helper using `unsigned char byte`, explicit initial `crc`, and one-time table init (matching `USBA_crc32()` in `system.cpp`).
- **Justification:** Eliminates OOB table reads and non-ASCII `ul.` filename mismatches with OPL.
- **Benefit:** Fixes signed-char OOB/hash mismatch and removes `24` duplicate lines.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-038: Menu `DS5` missing from in-game `usb_pademu`, unfinished `mx4sio_bd` `CRC16`, and unbuilt `ASYNC` in `usbmass_bd`
- **Category:** 1, 8 (Dead code, Half-finished or redundant features)
- **Location(s):** `RiptOPL/tools/build_configuration.py:29`, `RiptOPL/ps2.yaml:589`, `RiptOPL/modules/ds34usb/iop/ds34usb.h:10`, `RiptOPL/modules/pademu/ds34usb.c:87-96`, `RiptOPL/modules/pademu/ds34usb.h:8-13`, `RiptOPL/modules/mx4sio_bd/src/mx4sio.h:37, 45-47`, `RiptOPL/modules/mx4sio_bd/src/spi_sdcard_driver.c:138`, `RiptOPL/modules/usb/usbmass_bd/src/usb_mass.c:17, 102, 117, 218, 378, 461-470, 492-538`
- **Problem:** `DS5_ENABLE` only builds into menu `ds34usb`, missing in-game `pademu/ds34usb` (so DualSense disconnects when launching games); `mx4sio.h` carries dead `// #define CONFIG_USE_CRC16` stubs; and `usb_mass.c` carries unbuilt, unlocked `// #define ASYNC` code.
- **Fix:** Wire DualSense into `pademu/ds34usb` (or document menu-only), and delete the dead `CONFIG_USE_CRC16` and `// #define ASYNC` stubs.
- **Benefit:** Resolves the menu-vs-in-game DualSense gap and removes half-finished IOP preprocessor clutter.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-039: `BdmSource::config` omits `EEXIST` (`mkdir` retry on every cursor move) plus `ethReadOnlyCfgDir` dup, and `filemgr` MC path mismatch
- **Category:** 2, 6 (Duplication, Bugs and security)
- **Location(s):** `RiptOPL/src/sources/bdmsupport.cpp:2052-2068`, `RiptOPL/src/sources/ethsupport.cpp:1521-1539`, `RiptOPL/src/core/filemgr.cpp:9-10, 88-95, 369-371, 468-477`, `RiptOPL/src/ui/guifilemgr.cpp:204, 458, 506`
- **Problem:** `BdmSource::config` duplicates `ethReadOnlyCfgDir` (`512 B` total `.bss`) and omits `|| errno == EEXIST`, re-issuing `mkdir()` on every game cursor move once `CFG/` exists; `fmRemove()` and `fmCopyFile()`'s rollback pass unnormalized `"mc0:/..."` strings to `remove`/`rmdir` bypassing `fmDevPath()`.
- **Fix:** Share one `supportbase` helper checking `|| errno == EEXIST` and route `fmRemove()`/`fmCopyFile()` rollback through `fmDevPath()`.
- **Justification:** Eliminates per-cursor-move `mkdir()` I/O on BDM/MMCE and fixes MC deletion/rollback path normalization.
- **Benefit:** Halves path-cache `.bss` (`512 -> 256 B`), removes redundant `mkdir()` RPCs, and fixes MC path normalization.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-040: `cheatman.cpp` phantom cheats from indented comments, `OSDHistory.cpp` tie-break/MC/NUL bugs, and `sysLoadModuleBuffer()` `MAX_MODULES` order
- **Category:** 1, 6 (Dead code, Bugs and security)
- **Location(s):** `RiptOPL/src/core/cheatman.cpp:205-288, 342-375`, `RiptOPL/src/core/OSDHistory.cpp:126-183, 245-259`, `RiptOPL/include/OSDHistory.h:91-96`, `RiptOPL/src/platform/system.cpp:69-76, 132-172`
- **Problem:** `cheatman.cpp:342-352` strips indented comments leaving whitespace and ignores `trim_str() == -1`, treating spaces as cheat titles; `OSDHistory.cpp` leaves `mcType`/`format` uninitialized on MC errors, fails to reset `LeastUsedRecordTimestamp` on lower `LaunchCount`, and skips trailing zero-fill on `NewEntry->name`; and `system.cpp` checks `MAX_MODULES` (`64`) before checking already-loaded modules while keeping an unused stale `createVMCparam_t` typedef and 4 dead forward declarations.
- **Fix:** Require `trim_str(line) > 0`, initialize `mcType`/`format` + update `LeastUsedRecordTimestamp` + `memset` `name` in `OSDHistory.cpp`, scan `g_sysLoadedModBuffer[]` before `MAX_MODULES`, and delete the dead `system.cpp` declarations.
- **Justification:** Prevents corrupted `.cht`/history entries and duplicate-load failures at `64` resident IRXs.
- **Benefit:** Fixes cheat parsing, MC history eviction, and `sysLoadModuleBuffer` reuse while trimming dead code.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-041: Unbounded hardware busy-wait loops (`mx4sio_bd` ISRs, `iLink_DMA.c`), blocking `SPISD_ASSERT`, non-`volatile` `mcemu` spin, and `raudp` resident leak
- **Category:** 4, 5, 6 (Size and memory, Performance, Bugs and security)
- **Location(s):** `RiptOPL/modules/mx4sio_bd/src/mx4sio.c:39, 97, 254, 293, 333, 375, 417, 464, 525`, `RiptOPL/modules/mx4sio_bd/src/spi_sdcard_driver_config.h:9-10`, `RiptOPL/modules/ilink/iLinkman/src/iLink_DMA.c:40`, `RiptOPL/modules/mcemu/mcemu.c:10, 54-60`, `RiptOPL/modules/network/raudp/raudp.c:1284-1285`
- **Problem:** `mx4sio.c` (including ISRs) and `iLink_DMA.c` spin forever on hardware flags, `SPISD_ASSERT(cond)` spins `while (!cond) {}` without parens, `mcemu.c` polls non-`volatile` `readyToGo` after ignoring thread errors, and `raudp` returns `MODULE_RESIDENT_END` when `ra_src_ip` is `0` or `INADDR_BROADCAST`.
- **Fix:** Add bounded hardware timeouts, parenthesize/fail-fast `SPISD_ASSERT`, mark `readyToGo` `volatile` with thread error aborts, and return `MODULE_NO_RESIDENT_END` on invalid `ra_src_ip`.
- **Justification:** Prevents permanent IOP lockups on hardware glitches/unconfigured RA UDP and stops wasting IOP RAM.
- **Benefit:** Hardens IOP waits and reclaims `raudp` IOP RAM when unconfigured.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-042: `12` unembedded `gfx/*.png` files (`29,792 B`) and dead `NULL` `textures.cpp` entries
- **Category:** 1, 4 (Dead code, Size and memory)
- **Location(s):** `RiptOPL/gfx/{Device_{1..6},Device_all,no_Device,no_Rating,logo{4..6}}.png`, `RiptOPL/src/gfx/textures.cpp:241-266, 297-309`, `RiptOPL/tools/assets.py:46-60`
- **Problem:** Twelve PNG files (`29,792 B`, several identical copies of `Rating_0.png` / `logo1..3.png`) are skipped by `tools/assets.py`, while `textures.cpp` keeps `8` retired `NULL` rows only for enum alignment.
- **Fix:** Delete the `12` unembedded PNGs and remove retired `Device_*`/`no_Device` slots if themes do not index them directly.
- **Benefit:** Removes `29,792 B` (`12` dead asset files) and `9` retired texture table rows.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-043: Internal duplication across `ee_core`, `platform`, `sources`, `ui`, and `Release` scripts (`~500+` lines + `80 L` `deinit`, `120 L` `ps2cnf`)
- **Category:** 1, 2, 6 (Dead code, Duplication, Bugs and security)
- **Location(s):** `RiptOPL/src/platform/ps2cnf.cpp:42-120` (`include/ps2cnf.h:3`) vs `RiptOPL/src/platform/sysdisc.cpp:75-107`, `RiptOPL/src/app/opl.cpp:607-613, 1278-1455`, `RiptOPL/ee_core/src/{patches.c:552-856, igs_api.c:79-152, ra_overlay.c:46-61, 159-175, 369-393, ra_pulse.c:42, 73-94, igrmenu.c:196, 783-857, 920-932, freeze_diag_format.c:28}`, `RiptOPL/ee_core/include/igs_api.h:126-139`, `RiptOPL/src/sources/appsupport.cpp:1148-1167` vs `ethsupport.cpp:1014-1033`, `RiptOPL/src/platform/sysneutrino.cpp:279-291` vs `supportbase.cpp:1955-1967`, `RiptOPL/src/sources/bdmsupport.cpp:751-821, 1325-1370`, `RiptOPL/src/sources/vcdsupport.cpp:61-153, 366-409, 781-787, 1495-1500, 1606-1610, 1880-1914`, `RiptOPL/src/ui/{drivestatus.cpp:86-140, guifilemgr.cpp:181-216}`, `RiptOPL/tools/publish_{editions,lite,llvm}.py`, `RiptOPL/tools/make_changelog.sh`
- **Problem:** Multiple subsystems repeat internal helpers:
  1. Legacy `ps2cnf.cpp` (`120` lines, with unchecked `ftell`, unterminated `CNFGetKey`, dead `else if`, control-byte stall) duplicates `sysParseBoot2()`.
  2. `opl.cpp` `deinit()` (`84` lines) duplicates `deinitEx()` (`-1`).
  3. `ee_core` repeats 3x IOP module loaders (`patches.c`), 5 integer formatters (`igs_api.c`), `GSMSourceGSRegs`/`u32dec`/event-poll loops (`ra_overlay` vs `ra_pulse`/`igrmenu`/`freeze_diag_format`), two separate `128`-byte `ra_event` buffers in scarce `ram84`, and a duplicate aligned `pkt[28]` stack buffer inside `4 KiB` `IGR_Stack` (`igrmHoldSave` vs `igrmHoldBack`).
  4. EE sources/UI/tools duplicate `vcd_popsnet_ensure_t` toast boxes (`appsupport`/`ethsupport`), `LastPathSeparator` (`sysneutrino`/`supportbase`), UDPFS/UDPBD loaders (`bdmsupport`), 3x PS1 ID matchers + 3x basenames + 2x error ladders (`vcdsupport`), drive enumeration (`drivestatus`/`guifilemgr`), SHA256/markdown helpers across `publish_*.py` (with stale `"ORBIT RA runtime"` in `publish_llvm.py:152` and non-`mktemp` `/tmp` files in `make_changelog.sh`).
- **Fix:** Replace `ps2cnf.cpp` with `sysParseBoot2()`, implement `deinit()` via `deinitEx(exception, modeSelected, -1)`, extract shared `ee_core` IOP-loader / int-format / `u32dec` / event helpers + merge `ra_event` in `ram84` + pass `pkt + 28` in `igrmHoldSave`, share `vcd`/drive/`LastPathSeparator` helpers, unify `publish_*.py` utilities, and switch `make_changelog.sh` to `mktemp`.
- **Justification:** Bounds `ftell`/`CNFGetKey` when dropping `ps2cnf.cpp` and saves scarce `ram84`/`IGR_Stack` space.
- **Benefit:** Eliminates `~450+` lines of duplicated code across `src/`, `ee_core/`, and `tools/`, reclaims `128-192 B` in `ram84` and `~175 B` on `IGR_Stack`, and closes `ps2cnf.cpp` bugs.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-044: Unnecessary `bd_defrag_write` / `bdm_fork` vs SDK split, dead `atad`/`smb`/`imgdrv`/`patches`/`hdd_fio` code, and `udptty_ingame` in release `ps2.yaml`
- **Category:** 1, 2, 7 (Dead code, Duplication, Build/CI/repo clutter)
- **Location(s):** `RiptOPL/modules/bdm/libbdm/{src/bd_defrag.c:7-101, include/bd_defrag.h:24-28}`, `RiptOPL/ps2.yaml:297-304, 365-369, 456-518, 652`, `RiptOPL/tools/build_configuration.py:101-102`, `RiptOPL/src/platform/system.cpp:992-1006`, `RiptOPL/modules/iopcore/cdvdman/{atad.c:92, 470-517, 614-619, smb.c:23, 49, 918-922, 997-1020, device-smb.c:25, 50, exports.tab:1-55, 138-141}`, `RiptOPL/modules/iopcore/imgdrv/imgdrv.c:13-58`, `RiptOPL/modules/iopcore/patches/{f2techioppatch/main.c:17-18, iremsndpatch/asm.S:8}`, `RiptOPL/modules/hdd/apa/src/hdd_fio.c:98-101, 347-454, 860-969, 1028-1088`, `RiptOPL/modules/pccf/{pcdiag_ata.h:20, pccf_ata.h:17, pccf_hw.c:194-294, imports.lst:12-13, irx_imports.h:7, main.c:87-95}`, `RiptOPL/modules/pademu/padmacro.c:3-4`
- **Problem:** `ps2.yaml:652` embeds `udptty_ingame` unconditionally in release `riptopl` despite `build_configuration.py` injecting it for debug builds; `bd_defrag_write` is uncalled and duplicates `bd_defrag_read` while BDM `cdvdman` targets link SDK `bdm` instead of `bdm_fork`; `atad.c:470-516` short-circuits PIO commands with early `return 0;` and repeats `{ATA_C_STANDBY_IMMEDIATE, 0x1}`; `smb.c` (`#define USE_CUSTOM_RECV 1`), `imgdrv.c`, `patches`, `pccf`, `pademu/padmacro.c`, and `hdd_fio.c` (`hddFormat` + 4 `__attribute__((unused))` helpers, one leaking `clink`) hold hundreds of dead/uncalled lines (`cdvdman/exports.tab` also keeps dead comments and unused exports).
- **Fix:** Drop unconditional `udptty_ingame` from `ps2.yaml:652`, align `bdm_fork` vs SDK linkage and remove `bd_defrag_write`, strip dead `atad`/PIO/SMB/`imgdrv`/`patches`/`pccf`/`hdd_fio` code (plus `DeleteSema(io_lock)` in `cf_stop`).
- **Benefit:** Trims release IRX weight (`udptty_ingame`) and removes ~`300+` dead IOP lines.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-045: Single-TU private headers (`16` files), stateless `CueSupport`, `void **` IRX param casts, duplicated static data in headers, and `include` bloat
- **Category:** 1, 2, 3, 4, 7 (Dead code, Duplication, Overengineering, Size and memory, Build clutter)
- **Location(s):** `RiptOPL/include/{appsource,bdmboot,bdmlauncher,bdmsupportcore,folderbrowseint,hddint,mmceint,neutrinolauncher,supportbaseint,bgmplayer,ds34common,guigameid,guimodal,renderer,settingshell,soundeffects}.h`, `RiptOPL/include/cuesupport.h:227-235`, `RiptOPL/include/{supportbase.h:71, system.h:126, extern_irx.h:4, texcache.h:245, xparam.h:1-30, disc_key.h:40-59, utf8.h:21-38, opl.h:33,43}`, `RiptOPL/src/gfx/themes.cpp:1827`, `RiptOPL/src/sources/{udpfssupport.cpp:5,17, supportbase.cpp:15,19,29,31,590-603, appscan.cpp:7-67, bdmboot.cpp:1-36, bdmdevices.cpp:1-36, bdmlaunch.cpp:1-39,978-997, hddsupport.cpp:2750,2832, mmcesupport.cpp:1376,1745}`
- **Problem:** `16` headers in `include/` are included by only 1 `.cpp` file each; `class CueSupport` wraps one free function; `sbPrepare`/`sysLaunchLoaderElf` force `static_cast<void **>(irx)` across 4 launchers due to `void **` params; `cacheCancelPendingImageLoads` is a dead empty inline; `include/xparam.h` has no header guard; `disc_key.h` and `utf8.h` duplicate `128 B` DMA buffers and `368 B` lookup tables into two TUs each; `supportbase.cpp` repeats `if (count < 0) count = 0;`; and 4 `.cpp` files carry copy-pasted 35-to-60 line include blocks with 4 verbatim duplicate `#include` lines.
- **Fix:** Inline the `16` single-TU headers into their owning `.cpp` files, remove `class CueSupport`, change IRX parameters to `void *`, delete `cacheCancelPendingImageLoads` and duplicate `#include`/clamp lines, guard `xparam.h`, and move `sysReadDiscKey` and `utf8d` out-of-line.
- **Benefit:** Removes `16` unnecessary headers, eliminates duplicate `.bss`/`.rodata` tables across TUs, removes unsafe `static_cast<void **>` casts, and cuts rebuild churn.
- **Effort:** M (`16` headers), S (header/cast cleanups). **Risk:** Low. **Confidence:** High (`READ`).

### RIPTOPL-046: Broken `.o` recipes in `tools/pc/*/Makefile`, `check-format.yml`/`docs-sync.yml` CI gaps, brittle test regex, and Windows host test portability
- **Category:** 6, 7 (Bugs and security, Build/CI/repo clutter)
- **Location(s):** `RiptOPL/tools/pc/{genvmc,iso2opl}/Makefile:21-23`, `RiptOPL/tools/pc/opl2iso/Makefile:19-21`, `RiptOPL/.github/workflows/{check-format.yml:6-18,35, docs-sync.yml:1-92, ember-watch.yml:31}`, `RiptOPL/.github/scripts/{test_igr_menu_no_room.py:157, test_selfupdate_host.py, test_ripps2_boot_video_mode.py, test_ps2build_sdk_watch.py}`
- **Problem:** All three PC tool Makefiles discard `.o` targets and recompile `src/*.c` on every link (`iso2opl` also omits `isofs.o`); `check-format.yml` skips `.cpp`/`.hpp` files; `docs-sync.yml` lacks a repository ownership guard; `test_igr_menu_no_room.py:157` matches formatting-sensitive single-line regexes; and pure-Python host tests fail on Windows backslashes (`test_selfupdate_host.py`), `mc0:` colon directories (`test_ripps2_boot_video_mode.py`), and `test_ps2build_sdk_watch.py`.
- **Fix:** Link `$(OBJECTS)` in `tools/pc/*/Makefile`, add `**.cpp`/`**.hpp` to `check-format.yml`, guard `docs-sync.yml` on upstream repo, relax syntax regex in `test_igr_menu_no_room.py`, and normalize `.as_posix()` / synthetic host paths in Windows-failing Python tests.
- **Benefit:** Fixes incremental PC tool builds and CI/host-test reliability across OSes.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ` + test failures observed).

---

## Files Not Read

- **Exhaustively read (`1,062+` files across `7` code shards + context):** All `40` files in `src/{app,core,platform}/*`, all `44` files in `src/{gfx,ui}/*`, all `168` files in `include/*` & `src/sources/*`, all `201` files in `ee_core/*`, `tools/*`, & `.github/workflows/*`, all `113` files in `modules/{bdm,iopcore}/*` (`ffunicode.c` pure hex literal rows skipped after `python -B` check; `IOPRP.img` `244 B` binary skipped), all `255` + `5` context files in `modules/network/*` (`16` non-code `.txt`/`README`/`LICENSE`/`.patch` files skipped), and all `241` files in `modules/*` (`ilink*`, `mcemu`, `pademu`, `ds34*`, `usb/*`, `hdd/*`, `isofs`, `pccf`, `mx4sio_bd`, `mmce/*`, `vmc/*`, `debug/*`) were read completely (`100%` of assigned code shards).
- **Excluded generated/vendored/docs/test suites:** Pure hex rows in `modules/bdm/bdmfs_fatfs/fatfs/ffunicode.c` (`46-959, 971-1886, 1901-4616, 4629-7344, 7359-9481, 9494-11616, 11631-13309, 13321-15003`), `modules/network/` metadata/license/patch text files (`16` unread files), `third_party/` vendored trees (`mbedtls/`, `korium/`, `PSUManager/`), `lng_src/`, `docs/` (`70` `.md` + `docs/assets/`), `site/`, and individual `.github/scripts/test_*.py` scripts beyond the executed `278`-script batch and inspected parity targets.
