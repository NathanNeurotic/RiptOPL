# ADVERSARIAL VERIFICATION — Redo Audit, RiptOPL

- **Subject:** `~/workspace/Sable-audit-redo/Redo-RiptOPL.md` — 265 findings (RE-RIPTOPL-001…265).
- **Code:** clone `~/workspace/audit-clones-2026-11-13/RiptOPL` at pin `2e347d5ac885cd0ff73e9ad4143781946396f492` (branch rebuild/main). HEAD verified by every verifier before reading and by the coordinator after consolidation; `git status --porcelain` empty throughout — the clone was never modified.
- **Method:** refute-first. Eight independent verifiers, one per contiguous block of ~33 findings. For every finding: every cited file/function/line opened at the pin; the mechanism re-derived from scratch (bounds, signedness, widths, arithmetic); an active search for the guard that would kill the claim (pre-validating callers, sibling checks, clamping, dead reachability); the prescribed fix checked for sanity and LESS-NOT-MORE compliance; a mechanism-level duplicate scan against the rest of the report (bodies, not headings).
- **Scope note:** nothing under `~/workspace/Sable-audit/` was read by any verifier or by the coordinator; the only sources were the redo report and the clone.
- **Coordinator spot-checks:** the coordinator independently re-read the code behind all three REFUTED verdicts and one WEAKENED verdict (006) before accepting them — see "Coordinator spot-checks" below. All four hold.

## Closing tally (counted from the master table in this file)

| Verdict | Count |
|---|---|
| VERIFIED | 192 |
| VERIFIED-DRIFT | 47 |
| WEAKENED | 23 |
| REFUTED | 3 |
| BLOCKED-ENV | 0 |
| **Total** | **265** |

Reading of the tally: 192 findings (72%) hold exactly as filed. 47 (18%) hold in mechanism and fix but carry citation or figure errors — corrected values are in the per-range reasoning appendix. 23 (9%) are real but narrower than claimed (true scope stated per finding). 3 (1%) do not hold. No finding needed hardware or a PS2 build to settle, so BLOCKED-ENV is empty — every verdict below rests on code read at the pin (two findings, 086/087 in the 067–099 block, were additionally reproduced by execution on /tmp copies: a no-arg SIGSEGV and a decompressor hang).

## Master verdict table

| ID | Verdict | Evidence (file:line) |
|---|---|---|

| RE-RIPTOPL-001 | VERIFIED | smb.c:100-110 24-bit length; smb.c:257-261 unchecked RecvData into SMB_buf.smb; smb.h:894-895 4096+128 |
| RE-RIPTOPL-002 | VERIFIED | smb.c:1077-1092 padding/DataLength unchecked vs nbytes; hardened sibling smb.c:1164-1170 |
| RE-RIPTOPL-003 | VERIFIED | smb.c:403-404 memcpy KeyLength (u8, server) into smb.h:286 EncryptionKey[8] |
| RE-RIPTOPL-004 | VERIFIED | smb.c:692-718, 833-846, 1477-1482 offsets used as raw u8buff indices, no received-length check |
| RE-RIPTOPL-005 | VERIFIED | udprdma.c:185 hdr_size up to 60 (4-bit field, udprdma_packet.h:89); udprdma.c:191-196 dummy[32]/rx_hdr_buffer unchecked vs rx_hdr_size; send-side check udprdma.c:595 |
| RE-RIPTOPL-006 | WEAKENED | sysneutrino.cpp:961-990 pattern real, but allocation 512+pairs*32 covers fixed-format output; see reasoning |
| RE-RIPTOPL-007 | VERIFIED | journal.c:47-56 write-side num++ unbounded; journal.c:58-86 restore loops on s32 num (libapa.h:183-189, sectors[126]) and derefs apaCacheAlloc() result (cache.c:194-202 can return NULL) unchecked at journal.c:72-75 |
| RE-RIPTOPL-008 | VERIFIED | device-hdd.c:11-28 default sector_to_read=0; device-hdd.c:30-48 callers pass result straight to sceAtaDmaTransfer |
| RE-RIPTOPL-009 | VERIFIED | ps2cnf.cpp:18 deref before bound; ps2cnf.cpp:42-71 no end bound, NUL only on whitespace branch, dead else-if at 62-64 |
| RE-RIPTOPL-010 | VERIFIED | part_driver_gpt.c:68 stride from sizeof not partition_entry_size (gpt_types.h:36, field never read in the driver); part_driver_gpt.c:130/175 no first_lba<=last_lba check, sectorCount underflows |
| RE-RIPTOPL-011 | VERIFIED | zso.c:105-112 LZ4_decompress_fast with clamped-only block size; lz4.c:500-506 fast entry passes srcSize 0 to generic decoder |
| RE-RIPTOPL-012 | VERIFIED | ds34bt.c (pademu):1146,1179,1198,1206 — led_buf 0x4D+2=79, memset 0x30+2=50, full 79 sent; same at ds34bt/iop/ds34bt.c:1243; lengths ds34bt.h:224,228 |
| RE-RIPTOPL-013 | VERIFIED | ps2ips.c:591-592 memcpy optlen into optval[128]; ps2ips.c:402-407,435-440 s_offset=64-malign, memcpy malign bytes, no clamp |
| RE-RIPTOPL-014 | VERIFIED | udpfs_core.c:114-117 returns server result.result ignoring udprdma_recv length; udpfs_core.c:547-549 name_len checked only vs name_max, not vs ret |
| RE-RIPTOPL-015 | VERIFIED-DRIFT | Mechanism at scsi.c:136-155 and scsi.c:211-219, not the cited 169-187 / 244-264; see reasoning |
| RE-RIPTOPL-016 | VERIFIED | tar.cpp:75 parseOctal octal-only; tar.cpp:278 size read; grep: no chksum/checksum anywhere in tar.cpp |
| RE-RIPTOPL-017 | VERIFIED | tcp_in.c:120 IP-header check only; tcp_in.c:157-158 pbuf_header(-(hdrlen*4)) return ignored; pbuf.c:482-491 forward move passes lower-bound check and u16 len/tot_len underflows; lwipopts.h:371-400 CHECKSUM_CHECK_*=0 |
| RE-RIPTOPL-018 | VERIFIED | smap-ingame/main.c:45-70 chain copied into FrameBuffer with no tot_len check; main.h:4 MAX_FRAME_SIZE 1518 |
| RE-RIPTOPL-019 | VERIFIED | mmce_cmds.c:195-224 strcpy(ptr,&rdbuf[4]) from 255-byte packet; mmce_fs.c:1122-1124 caller passes buf, buflen ignored |
| RE-RIPTOPL-020 | VERIFIED | include/opl.h:56 master-pass define (value not reproduced); accepted at src/ui/guisettings.cpp:1659; per-user key separate |
| RE-RIPTOPL-021 | VERIFIED | hdd_fio.c:995-997 parts[*(u32*)argp] unchecked, argp not NULL-checked; hdd.h:16-24 parts[APA_MAXSUB+1]; contrast ioctl2Transfer check hdd_fio.c:274-277 |
| RE-RIPTOPL-022 | VERIFIED | sockets.c:81-98 table 11 entries (0..-10), macro compares signed err against unsigned sizeof so every negative err yields EIO; err.h:61 ERR_IF=-11 has no table entry |
| RE-RIPTOPL-023 | VERIFIED | guiwait.cpp:49-113 runWithMessage bounded (timeoutMs_); guiwait.cpp:115-141 runWithDialog spins while(*flag_) with no timeout |
| RE-RIPTOPL-024 | VERIFIED | selfupdate.cpp:265-340 SHA verified in RAM, in-place WRONLY\|CREAT\|TRUNC write, SU_OK set after close with no read-back/hash of on-device bytes |
| RE-RIPTOPL-025 | VERIFIED | md5 ship/POPS/POPSTARTER.ELF = bbc61fa490df65d9527fcea7c4118170 = DEBUG variant; MAIN = 4a39d44dfb477ea747f5ca5e39ee011e; all 167,700 B |
| RE-RIPTOPL-026 | VERIFIED | vmcrebuild.cpp:9 design comment (backup never deleted), :124, :173-200; vcdsupport.cpp:2206-2208 install veto on existing .bak (-3), :2269-2360 write path also vetoes on pre-existing .bak at entry |
| RE-RIPTOPL-027 | VERIFIED | fs_driver.c:1019-1030 STOP_UNIT calls stop_all_bd, STOP_ALL resolves one volume and calls stop_single_bd; EE caller bdmsupport.cpp:2180 uses STOP_ALL per-device |
| RE-RIPTOPL-028 | WEAKENED | hdd.cpp:438-446 private_4 index unchecked (holds); hdd.cpp:405-437 negative-nsub half does not hold as stated; see reasoning |
| RE-RIPTOPL-029 | VERIFIED-DRIFT | Unvalidated copies real at elfldr_noreset.cpp:233-238 and system.cpp:1642-1651 (cited 1359 is the function entry); validated contrast system.cpp:1308-1324 |
| RE-RIPTOPL-030 | VERIFIED | api_msg.c:408-437 do_bind udp_new()/tcp_new() used unchecked; api_msg.c:505-540 do_connect checks every allocation; lwipopts.h:65,70-72 MEMP_NUM_UDP_PCB 1, TCP_PCB 1-2 |
| RE-RIPTOPL-031 | VERIFIED | api_lib.c:422 netconn_recv reads pcb.tcp->state, api_lib.c:538 netconn_write tcp_sndbuf(pcb.tcp), neither checks pcb.tcp NULL; api_lib.c:261-281 netconn_peer/addr lack conn==NULL check (peer checks pcb, not conn); sockets.c:180-196 lwip_accept uses newconn and *addrlen/addr unchecked; documented NULL-pcb state sockets.c:950,1086,1290 |
| RE-RIPTOPL-032 | VERIFIED | ps2ip.c:558-569 frees old block before AllocSysMemory, returns NULL on failure; mem.c:109-121 propagates; pbuf.c:438-441 ignores mem_realloc return and keeps using chain |
| RE-RIPTOPL-033 | VERIFIED | ip_frag.c:61-81 loop for(;len;p=p->next) derefs p->len with no NULL check; ip_frag.c:153-170 len from IPH_LEN; ip.c:241 pbuf_realloc(p,IPH_LEN) and pbuf.c:410-413 realloc cannot grow |
| RE-RIPTOPL-034 | VERIFIED | `modules/network/SMSTCPIP/sockets.c:231-232,300,307-308` deref `name` unchecked; `:566,573,575` ignore `netconn_connect`/`disconnect` returns; contrast checked `lwip_getsockopt` NULL guard in same file |
| RE-RIPTOPL-035 | WEAKENED | Failure path real (`sockets.c:387-390` returns 0, errno 0), but index half narrowed — see reasoning |
| RE-RIPTOPL-036 | VERIFIED | `modules/network/nbns/nbns.c:179` only checks `length >= 12`; `:198` copies address from fixed offset byte 58 (12+34+4+4+2+2) of the 576-byte static frame |
| RE-RIPTOPL-037 | WEAKENED | Part (b) real (`smb_fio.c:660-663`, `:361`), part (a) refuted — see reasoning |
| RE-RIPTOPL-038 | VERIFIED | `modules/iopcore/cdvdman/searchfile.c:217-218` `strlen(cdvdman_filepath) - 6` underflows for paths < 6 chars; buffer is the 256-byte static at `:175` filled from the game's search name at `:191` |
| RE-RIPTOPL-039 | VERIFIED | `modules/iopcore/cdvdman/smb.c:447-459` `setStringField`/`asciiToUtf16` unbounded; call sites `:604-623,695,783` accumulate into the 1024-byte `SMB_buf` (`smb.h:512-513`, `smb.c:77`); inputs are fixed fields (Username[36], PrimaryDomain[32] in `modules/iopcore/common/oplsmb.h:9,15`) so "fits today, unenforced" is accurate as scoped |
| RE-RIPTOPL-040 | VERIFIED | `modules/debug/ps2link/net_fio.c:402` `nbytes = ntohl(readrly->nbytes)`; `:408` receives `nbytes` into caller's `buf` with no comparison to requested `length` (only `length < 0` checked) |
| RE-RIPTOPL-041 | VERIFIED | `modules/bdm/bdmfs_fatfs/src/fs_driver.c:940-943` `fs_ioctl` fabricates `datalen=1024`; `fs_ioctl2` at `:861-866` requires `datalen == sizeof(vmc_alloc_request_t)` for VMC commands, so they can never succeed via `fs_ioctl` |
| RE-RIPTOPL-042 | VERIFIED | `modules/mmce/mmceman/src/mmce_fs.c:1103,1116,1153,1158` `args = *(u32*)arg` unchecked; contrast `:1137-1142` SET_CARD_CHANNEL validates `arg == NULL \|\| arglen < 5` |
| RE-RIPTOPL-043 | VERIFIED | `modules/hdd/apa/src/hdd_fio.c:510-512` `apaCacheFree(clink)` then `apaPassCmp(clink->header->fpwd…)`; `modules/hdd/apa/src/apa.c:508-518` frees then reads `clink->header->next`/`clink->device`; `apaCacheFree` relinks at `nused==0` (`modules/hdd/apa/src/cache.c:179-196`) |
| RE-RIPTOPL-044 | VERIFIED | `modules/usb/usbmass_bd/src/scsi.c:262` and `:281` store `last_lba` directly as `sectorCount` (struct field is named `last_lba` in-tree); READ CAPACITY returns the last addressable LBA, so count = value + 1 |
| RE-RIPTOPL-045 | VERIFIED | `src/ui/guinotify.cpp:108-115` `col_pos = strchr(gOPLPart, ':'); col_pos++;` unchecked; sibling branches at `:120,128,136` all NULL-check |
| RE-RIPTOPL-046 | VERIFIED | `src/platform/ioprp.cpp:8` `char fileName[10]`; `:43-58` `strcmp`/`strcpy` on the fixed-width ROMDIR field (note: the 1-byte `strcpy` spill lands in `extinfo_size`, reassigned on the next line — the over-read is the live half) |
| RE-RIPTOPL-047 | VERIFIED | `src/app/settingsio.cpp:1931,1936,1950,1952,1954,1956` six unchecked `sscanf("%d.%d.%d.%d")` (exact lines match); contrast `configReadRA` in same file requiring `== 4` plus 0–255 range checks |
| RE-RIPTOPL-048 | VERIFIED | `src/core/lang.cpp:550-556` `languages[langID - 1]` with no range check; finding honestly scopes caller reachability as unestablished (sole in-tree caller passes `lngGetGuiValue()`) |
| RE-RIPTOPL-049 | VERIFIED | `modules/network/SMSTCPIP/icmp.c:138-147` `q = pbuf_alloc(...)` written through with no NULL check; callers `ip.c:319`, `udp.c:380` exact |
| RE-RIPTOPL-050 | VERIFIED | `modules/network/SMSTCPIP/ps2ip.c:224` advances `u8LastMSG` before `memp_malloc` at `:228`; failure frees the pbuf and returns with the slot never queued; `InputCB` at `:189-193` reads ethertype with no length check and ignores `pbuf_header` result; Makefile default `INTERRUPT_CTX_INPKT ?= 1` |
| RE-RIPTOPL-051 | VERIFIED | Counted both tables: `modules/network/common/smstcpip.h` declares dechain=41/take=42/aton=43/ntoa=44; `modules/network/SMSTCPIP/exports.tab` slot 41 is `pbuf_take`, 42 is `etharp_lookup_mac` (INGAME_UDP only), table ends at 42 — all four header imports are wrong or nonexistent |
| RE-RIPTOPL-052 | WEAKENED | Missing `pbuf_free` else-branch real (`api_msg.c:89-106`), but the claimed arrival window is not established — see reasoning |
| RE-RIPTOPL-053 | VERIFIED | `modules/network/SMSTCPIP/tcp_in.c:1107-1108` clamps MSS only from above, 0 stored; `tcp_out.c:128-134` then computes `seglen = 0` and loops allocating segments until `TCP_SND_QUEUELEN`/pool exhaustion → `memerr` |
| RE-RIPTOPL-054 | VERIFIED | `modules/network/httpclient/httpclient.c:225` `memcpy(buffer, ptr, DataAvailable)` (≤256, `HTTP_WORK_BUFFER_SIZE` at `:128`) ignores caller's `*length`; sole caller passes the 512-byte `DmaBuffer` with `out_len` pre-clamped (`modules/network/httpclient/main.c:45-50`) — latent exactly as claimed |
| RE-RIPTOPL-055 | WEAKENED | Uninitialised/stale `from` read in the pinned branch is real (`raudp.c:897-899`), but it cannot steer the trust decision as claimed — see reasoning |
| RE-RIPTOPL-056 | REFUTED | Sole caller frees every persistent block on setup failure (`rapull.c:337-347`) — see reasoning |
| RE-RIPTOPL-057 | VERIFIED | `modules/iopcore/cdvdman/device-bdm.c:279,302` `g_bd->read/write` unconditional in the MCEMU exports; contrast `DeviceReadSectors` NULL check at `:229-230` |
| RE-RIPTOPL-058 | VERIFIED | `modules/iopcore/cdvdman/device-hdd.c:70-73` unbounded `while ((r = sceAtaDmaTransfer(...)) != 0) DelayThread(2000);` in `DeviceInit` |
| RE-RIPTOPL-059 | VERIFIED | `modules/iopcore/cdvdman/hdpro_atad.c:714-719` comment admits "retry indefinitely", `continue` advances neither LBA nor a retry counter while holding `ata_io_sema`; non-HDPro `atad.c:700` caps the equivalent loop at 3 retries |
| RE-RIPTOPL-060 | VERIFIED | `modules/debug/ps2link/net_fio.c:88-100` `pko_recv_bytes` treats only `len < 0` as error; `recv()` returning 0 leaves `left` unchanged forever |
| RE-RIPTOPL-061 | VERIFIED | `modules/debug/ps2link/excepHandler.c:55-70` lookup returns 0 on no match; `:80` `strlen(module_name)` on NULL; `:86` copies `BUFFER_SIZE` bytes starting one word into a `BUFFER_SIZE`-byte buffer (`:12-14`); `:92` name copy bounded only by name length |
| RE-RIPTOPL-062 | VERIFIED | `modules/debug/udptty-ingame/udptty.c:225-236` `udp_send` discards `lwip_sendto` result and returns 0; `:259-268` `tty_write` returns that 0 for every write |
| RE-RIPTOPL-063 | VERIFIED | `modules/bdm/src/part_driver_gpt.c:38-42` returns `0` on `AllocSysMemory` failure; `modules/bdm/src/bdm.c:184` treats `connect_bd(...) == 0` as mounted, so FatFs is never tried; other failure paths in the same function return `-1` |
| RE-RIPTOPL-064 | VERIFIED | `modules/bdm/bdmfs_fatfs/src/fs_driver.c:879-891` GET_LBA derefs `bd->sectorOffset` with no NULL check; neighbouring GET_DEVICE_NUMBER checks `bd == NULL → -ENXIO`, GET_DRIVERNAME already fixed the same way |
| RE-RIPTOPL-065 | VERIFIED | `modules/bdm/bdmfs_fatfs/src/fs_driver.c:137-146` guard is `index > MAX` where MAX = array count = `FF_VOLUMES` = 10 (`fatfs/include/ffconf.h:181`; array declared `:32`, macro `:34`), so index 10 and all negatives index the array out of bounds |
| RE-RIPTOPL-066 | VERIFIED | `modules/bdm/src/bdm.c:119-141` `bdm_disconnect_fs` only NULLs `g_mount[i].fs`, never calls `fs->disconnect_bd`; contrast `bdm_disconnect_bd` at `:83` which does; no in-tree caller exercises it today, as the finding itself states |
| RE-RIPTOPL-067 | VERIFIED | `valid++` unconditional and early whole-disk `return 0` at `modules/bdm/src/part_driver_mbr.c:29,23-24`; connect loop at `:96-124` keys only on `sector_count`/`partition_type` |
| RE-RIPTOPL-068 | VERIFIED-DRIFT | Truncation real at `modules/bdm/bdmfs_fatfs/fatfs/diskio.c:131`, but the only consumers are f_mkfs/f_fdisk (`fatfs/ff.c:5922,6152`), not mount validation |
| RE-RIPTOPL-069 | VERIFIED | `bd.sectorCount` set only for CSD v0/v1, unconditional `SPISD_RESULT_OK` at `modules/mx4sio_bd/src/spi_sdcard_driver.c:373-390`; `bd` initialised with `sectorCount 0` at `:61` |
| RE-RIPTOPL-070 | VERIFIED-DRIFT | No disconnect on successful recovery at `modules/mx4sio_bd/src/mx4sio.c:632-641` (disconnect only on failure, `:639`), but bd_cache is write-through/invalidate-on-write (`modules/bdm/libbdm/src/bd_cache.c:173-183`), not write-back |
| RE-RIPTOPL-071 | VERIFIED | `ds34usb_init` sets `controlEndp`/`interruptEndp=-1` but never `outEndp` at `modules/pademu/ds34usb.c:588-589`; BSS-zero global at `:73`; standalone `_start` does set it at `modules/ds34usb/iop/ds34usb.c:958` |
| RE-RIPTOPL-072 | VERIFIED | 6-entry `power_level[]` indexed by full `uint8_t Power` (`include/ds34common.h:216`) guarded only by `!= 0xEE` at `modules/ds34usb/iop/ds34usb.c:337-338` and `modules/ds34bt/iop/ds34bt.c:1332-1333`; pademu guards `<= 0x05` at `modules/pademu/ds34usb.c:332`, `modules/pademu/ds34bt.c:1251` |
| RE-RIPTOPL-073 | VERIFIED | EE sends 2 RPC bytes at `modules/ds34usb/ee/libds34usb.c:120-129`; IOP passes `data+1` at `modules/ds34usb/iop/ds34usb.c:920` into `LEDRumble`, which reads `led[0..3]` at `:589-624`; BT EE sends 5 bytes at `modules/ds34bt/ee/libds34bt.c:113-125` |
| RE-RIPTOPL-074 | VERIFIED | Standalone reads `data[8]`/`data[9]` and casts `&data[11]` with no length check at `modules/ds34bt/iop/ds34bt.c:1276-1282`; pademu compares `bytes < sizeof(struct ds3report)` (=47) ignoring the 11-byte offset at `modules/pademu/ds34bt.c:1220-1227`, DS4 branch unchecked at `:1271-1273` |
| RE-RIPTOPL-075 | VERIFIED | `mips_memcpy(dst, ds34pad[port].data, size)` unclamped at `modules/pademu/ds34bt.c:1340` and `modules/pademu/ds34usb.c:508`; source is the 18-byte union (`modules/pademu/ds34usb.h`, `ds34bt.h`); caller passes `out_size - 3` at `modules/pademu/pademu.c:467` |
| RE-RIPTOPL-076 | VERIFIED | `usb_data_cb` receives `bytes` but discards it at `modules/pademu/ds34usb.c:226-236`; `readReport(u8 *data, int pad_idx)` at `:293` has no length parameter and casts by pad type at `:296-352` |
| RE-RIPTOPL-077 | VERIFIED | `u8 led[4]` partly set (DS3: only `[0]`,`[3]`; guitar: none) then `LEDRumble(led,0,0,pad)` at `modules/pademu/ds34usb.c:250,264-276`; `LEDRumble` copies all four into `oldled` at `:440-443`; same shape at `modules/ds34usb/iop/ds34usb.c:253-282,621-624` |
| RE-RIPTOPL-078 | VERIFIED | One buffer, three sizes: allocation 96 at `modules/pademu/ds34bt.c:334`, first transfer 64 at `:359`, requeue 87 at `:1088` |
| RE-RIPTOPL-079 | VERIFIED | 24-byte transfer at `modules/ds34usb/iop/ds34usb.c:830` after writing only indices 0–22 (`:822-828`: 1 + 6 bdaddr + 16 link-key = 23 bytes) |
| RE-RIPTOPL-080 | WEAKENED | Sharing is real (one `usb_buf` at `modules/ds34usb/iop/ds34usb.c:62`, one `usb_resulCode` at `:231-239`, `MAX_PADS 2` at `:20`), but RPC dispatch is serialised, so the claimed two-pad RPC interleave cannot occur as framed |
| RE-RIPTOPL-081 | VERIFIED | Exported (exports.tab #013/#021/#022) `iLinkTrFree` writes unchecked at `modules/ilink/iLinkman/src/iLink.c:209-212`, speed accessors index unchecked at `:336,357`; siblings validate at `:219,243`; `MAX_CONCURRENT_TRANSACTIONS 10` at `src/include/iLink_internal.h:280` |
| RE-RIPTOPL-082 | VERIFIED | `open`/`read`/`close` results all unchecked, `strtoul` over uninitialised `romver_str[5]` at `modules/ilink/iLinkman/src/iLink.c:61-72`; function is live — `REQ_CHECK_CONSOLE_VERSION` is defined at `src/include/iLink_internal.h:16` and the result gates `_start` at `iLink.c:88` |
| RE-RIPTOPL-083 | VERIFIED | `InitializeConfigurationROM` NULLs all 16 `ExtraCROMUnits[]` without `free` at `modules/ilink/iLinkman/src/iLink_crom.c:249-263`; units are `malloc`'d at `:74`; called from the capabilities setter at `modules/ilink/iLinkman/src/iLink.c:270` as well as `_start:110` |
| RE-RIPTOPL-084 | VERIFIED | Unwritten trailing bytes sent in all four: `set_card` `wrbuf[7]` (`modules/mmce/mmceman/src/mmce_cmds.c:109-121`), `set_channel` `wrbuf[6]` (`:169-180`), `reset` `wrbuf[3..4]` (`:263-271`), `set_gameid` sends `len+5`, one past the filled string (`:232-244`); intended pattern at `:301` |
| RE-RIPTOPL-085 | VERIFIED | `u8 len = strlen(name) + 1` at all eight cited sites in `modules/mmce/mmceman/src/mmce_fs.c:84,420,479,538,604,849,964-965`; transfer API takes `u8 tx_size` (`modules/mmce/mmceman/include/mmce_sio2.h`); 300-byte name → 301 mod 256 = 45 |
| RE-RIPTOPL-086 | VERIFIED | No `return` after `printUsage()` at `tools/pc/genvmc/src/genvmc.c:399-402`; reproduced in /tmp: no-arg run prints usage then SIGSEGV (exit 139), one-arg run fails later with `fatal error (-101)` |
| RE-RIPTOPL-087 | VERIFIED | Unbounded trim-and-retry loop at `tools/pc/ziso.py:62-71` (`b""[:-1]` is `b""`); reproduced in /tmp: `lz4_decompress(b"\xff\xff\xff", 2048)` did not return within 5 s; caller `except` at `:176-182` therefore never fires |
| RE-RIPTOPL-088 | VERIFIED | Raw `sprintf` in `hex_to_str`/`ver_to_str` at `src/ui/guigame.cpp:1158-1173` into `vid_str[5]`/`hci_str[26]` statics at `:1137-1142`, beside `snprintf`-converted `bdaddr_to_str` at `:1151-1156`; safety rests on the `ma > 9` clamp at `:1167` and short `bt_ver_str[]` at `:1072-1083` |
| RE-RIPTOPL-089 | WEAKENED | Both cited lines exist (`src/ui/guifilemgr.cpp:446,471`), but no correctness depends on evaluation order and both copies are safe by construction today — a readability smell, narrower than the aliasing-trap framing |
| RE-RIPTOPL-090 | VERIFIED | `malloc` result passed straight to `sprintf` unchecked at `src/platform/util.cpp:873-874` (the sibling list-node allocations at `:885,890` in the same function are checked) |
| RE-RIPTOPL-091 | VERIFIED | `int getFileSize(int fd)` stores `lseek` in an `int` at `src/platform/util.cpp:48-53`; game-image caller accumulates it as a size at `src/sources/supportbase.cpp:662` |
| RE-RIPTOPL-092 | WEAKENED | Dual Content-Length parsing is real (`atoi` at `src/platform/ratls.c:177` vs bounded digit loop at `:423-433`), but the "make `raTlsBodyLength` static" half is refuted: it is a deliberate extern consumed outside the file |
| RE-RIPTOPL-093 | REFUTED | The takeover path resets the timestamp in the very range cited: `NewEntry->DateStamp = GetTimestamp();` at `src/core/OSDHistory.cpp:258`, alongside `LaunchCount = 1` at `:255` |
| RE-RIPTOPL-094 | VERIFIED | Byte-identical `?` row `{0x0e,0x11,0x01,0x02,0x04,0x00,0x04}` occurs exactly 8 times at `ee_core/include/igrmfont.h:25,26,27,33,50,54,55,85` (grep-counted); only line 54 is `?` itself |
| RE-RIPTOPL-095 | VERIFIED-DRIFT | The 10 named headers are exactly the unguarded set (independently re-grepped), but the census figures are off: the scope holds 169 headers, 159 guarded — not 148 / 128 |
| RE-RIPTOPL-096 | WEAKENED | Reuse, ~1.03 MB size and ee_core duplication all check out (`include/cheatman.h:39-41,67`; `src/core/cheatman.cpp:454-457`; `ee_core/include/cheat_engine.h:27-29`), but the parser's own per-cheat bound is the same constant, so the stated basis for a smaller cap is circular |
| RE-RIPTOPL-097 | VERIFIED | `static char bgmBuffer[192][4096]` (768 KB BSS) at `src/core/bgm.cpp:29,72`; BGM defaults off at `src/core/settings.cpp:175` (`intSetting(CONFIG_OPL_BGM, &gEnableBGM, 0)`) |
| RE-RIPTOPL-098 | VERIFIED | Both arrays allocated at the 2048 cap at `src/sources/httpsupport.cpp:165-166`, duplicate scan at `:197-202`; sizes re-derived: `base_game_info_t` ≈188 B + extra ≈260 B → ≈0.9 MB at cap; realloc-grow pattern exists at `src/sources/hddsupport.cpp:1568` |
| RE-RIPTOPL-099 | VERIFIED | 407-line header (`modules/hdd/xhdd/ata_identify.h`), 512-byte static (`sizeof` measured) at `modules/hdd/xhdd/xhdd.c:17`, sole field read `UltraDMASupport` at `:79` — ATA IDENTIFY word 88, as the fix assumes |
| RE-RIPTOPL-100 | VERIFIED-DRIFT | MINI iso pool real (`modules/usb/usbd-ra/src/usbd.c:50-55`), but slip is at `usbd.c:139`, not :130, and `setupIsocronTransfer` starts at `usbio.c:152`, not :143 |
| RE-RIPTOPL-101 | VERIFIED | Glob at `tools/build_edition.py:114,137`; 109 `.c` files counted; neither config defines server/cache/timing/net/debug modules (`third_party/mbedtls/riptopl_mbedtls_config.h`, `riptopl_mbedtls_slim_config.h`) |
| RE-RIPTOPL-102 | VERIFIED | Doubling realloc loop + 8 MiB cap at `src/gfx/textures.cpp:160,684-700`; exact-size `lseek` fast path at `textures.cpp:641` |
| RE-RIPTOPL-103 | VERIFIED-DRIFT | Header definitions real (`include/utf8.h:21-49`, includers `src/gfx/fntsys.cpp:12`, `src/gfx/textshape.cpp:12`) but the table is 400 entries, not 364 |
| RE-RIPTOPL-104 | VERIFIED | `crctab[0x400]` at `src/platform/system.cpp:465`, only indices 0–255 ever used/filled (:476-487), table rebuilt per call |
| RE-RIPTOPL-105 | VERIFIED | VLAs at `src/ui/gui.cpp:173` (`char tmp[maxLength]`) and `src/ui/diaeditors.cpp:215` (`char pre[maxLen]`) |
| RE-RIPTOPL-106 | VERIFIED | Two 4,096-byte static unions at `modules/pccf/pccf_pio_profile.inc:3`, used only inside `pio_profile` called from `modules/pccf/pccf_hw.c:295` |
| RE-RIPTOPL-107 | VERIFIED | Scripted recount exact: 137 files / 5,918,101 B total, 23 consumed / 1,752,047 B, remainder 4,166,054 B, Neutrino subtree 78 files / 603,340 B (`ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/neutrino/version.txt` = `v1.8.0-61-g7be8de2`) |
| RE-RIPTOPL-108 | VERIFIED-DRIFT | File line counts exact (1425/1719/616/998) and `smsutils.S` md5 identical, but diff outputs are 785 and 834 lines, not 915 and 950 |
| RE-RIPTOPL-109 | VERIFIED | All `wc`/`diff` figures exact: MD4 175/182/240, DES 564/568 with 119-line diff, auth diff 113 lines (`modules/network/smbinit/`, `smbman-ra/`, `smb2man/usmb2/md4c.c`) |
| RE-RIPTOPL-110 | VERIFIED-DRIFT | Six launch copies real, but censuses are inflated and the BDM per-part loop is not the identical-comment loop shared by ETH/MMCE |
| RE-RIPTOPL-111 | VERIFIED | Five PS1 launch copies at the cited sites (e.g. `src/sources/hddsupport.cpp:2161,2237,2308,3223`); FAV proxy at `src/sources/favsupport.cpp:1290-1330` |
| RE-RIPTOPL-112 | VERIFIED | `diff -rq modules/bdmassault/ilink_bd_assault/src modules/ilink` shows divergence in `scsi.c`, `sbp2_driver.c`, `iLink.c`, `iLink_DMA.c`, `iLink_PHY.c` and more |
| RE-RIPTOPL-113 | VERIFIED | Bodies 93 vs 84 lines (`src/app/opl.cpp:1278-1370,1372-1455`); only differences are signature, spared-mode loop vs `deinitAllSupport` (`opl.cpp:607`), and `launchDiagMark(7)/(8)` |
| RE-RIPTOPL-114 | VERIFIED | `md5sum`: five protocol headers byte-identical ×3 editions; `mprintf.h` identical in 5 of 6 locations (differing copy is `smap_udpbd`); `udpfs_bd`/`udpfs_ioman` `main.c` md5-identical |
| RE-RIPTOPL-115 | VERIFIED | SMAP `diff` = exactly 25 lines (639 vs 641 lines); `udptty.c` 89 lines each, diff = 1 include; `parse_ip` bodies identical at `udpfs_ministack/src/main.c:18` / `smap_udpbd/src/main.c:19` |
| RE-RIPTOPL-116 | VERIFIED | Walker duplicated at `modules/iopcore/cdvdman/searchfile.c:25-240` and `modules/isofs/isofs.c:187-380`; `dirTocEntry` defined at `cdvdman/internal.h:96` and `isofs.c:19`; precedent `../../isofs/zso.o` in cdvdman `Makefile:1` |
| RE-RIPTOPL-117 | VERIFIED | `mmce_fs_remove/mkdir/rmdir` at `modules/mmce/mmceman/src/mmce_fs.c:409,468,527`, identical packet sequence differing only in command byte |
| RE-RIPTOPL-118 | WEAKENED | Two copies match the claimed pattern (`src/ui/dia.cpp:75-110`, `src/ui/guinotify.cpp:47-84`); the modal copy does not measure, clamp or draw a rect |
| RE-RIPTOPL-119 | VERIFIED | Overlay inset arithmetic copy-identical in `src/gfx/coverflow.cpp:337-345`, `src/gfx/psbbnflow.cpp:162-175`, `src/gfx/grid.cpp:320-330` |
| RE-RIPTOPL-120 | VERIFIED | Ring walk duplicated at `modules/iopcore/cdvdman/streaming.c:162-245` (`ReadSectorsEE`) and `:247-295` (`ReadSectors`) |
| RE-RIPTOPL-121 | VERIFIED | Helper sets at `ee_core/src/freeze_diag_screen.c:42-94` and `ee_core/src/freeze_diag_format.c:22-49,304-321`, including the same module-table lookup in `ln_where`/`put_where` |
| RE-RIPTOPL-122 | VERIFIED | Five converters at `ee_core/src/igs_api.c:79,94,109,124,139`, one body shape differing only in width/radix |
| RE-RIPTOPL-123 | WEAKENED | Three copies exact (`src/sources/ranet.cpp:1077-1078`, `ratls_ps2.cpp:161-162`, `nettime.cpp:117-118`), but the prescribed fix breaks non-RA builds |
| RE-RIPTOPL-124 | VERIFIED | First-66-line diffs vs `system.cpp`: sysdisc 2 changed lines, sysneutrino 4, sysvmc 8; dead `SYS_DISC_*` constants at `src/platform/sysvmc.cpp:15-23` (used only in `sysdisc.cpp:161,185,186`) |
| RE-RIPTOPL-125 | WEAKENED | Graduated 100 µs/1 ms poll + `#399` comment in only 3 of 4 (`smb.c:302-304`, `smb2.c:310`, `nfs.c:78`); the HTTP copy polls at a flat 1 ms |
| RE-RIPTOPL-126 | VERIFIED-DRIFT | Tables byte-identical and atad MDMA copy dead (`modules/hdd/atad/src/ps2atad.c:1485-1505`, Makefile defines no `ATA_MWDMA_MODES`), but atad does not include the "already-shared" header |
| RE-RIPTOPL-127 | VERIFIED | `crc32` bodies byte-identical in `tools/pc/iso2opl/src/iso2opl.c:48-76` and `tools/pc/opl2iso/src/opl2iso.c:43-66`, incl. `crctab[0x400]` globals; typedef blocks match at the three cited headers |
| RE-RIPTOPL-128 | WEAKENED | Three predicate copies exact (`src/sources/ranet.cpp:217-231`, `ratls_ps2.cpp:143-153`, `nettime.cpp:138-140`), but the prescribed fix breaks non-RA builds |
| RE-RIPTOPL-129 | VERIFIED | Divergence real: ETH records success on `mkdir == 0 \|\| errno == EEXIST` (`src/sources/ethsupport.cpp:1535`), BDM only on `mkdir == 0` (`src/sources/bdmsupport.cpp:2063`) |
| RE-RIPTOPL-130 | VERIFIED | `#include` lists of `guiwait.cpp`/`guigameid.cpp` (38 each) differ only in the final self-header; cited unused headers appear in `guigameid.cpp` only inside comments |
| RE-RIPTOPL-131 | VERIFIED | `guiDrawIconAndText`/`guiDrawIconAndLiteralText` at `src/gfx/uidraw.cpp:13-57` are the same body modulo `_l(textId)` vs literal |
| RE-RIPTOPL-132 | VERIFIED | `dsScan` (`src/ui/drivestatus.cpp:77-165`) and `fmScanDrives` (`src/ui/guifilemgr.cpp:174-248`) walk the same BDM/MMCE/MC/APA sequence with the same calls |
| RE-RIPTOPL-133 | WEAKENED | Same FNV constants at `src/gfx/artworker.cpp:13-24` and `src/gfx/artindex.cpp:54-71`, but `hashName` case-folds and `focusHashValue` does not — not a special case |
| RE-RIPTOPL-134 | VERIFIED | Token/case-sensitive parser `src/platform/ps2cnf.cpp:16-122` vs case-insensitive line scanner `src/platform/sysdisc.cpp:75` |
| RE-RIPTOPL-135 | VERIFIED | Four near-identical sweeps `src/platform/tar.cpp:464,501,531,555` differing only in predicate + load-on-demand guard |
| RE-RIPTOPL-136 | VERIFIED | Identical 0:128/1:100/2:80/3:60/default:100 switches for X and Y at `src/core/pad.cpp:317-350` |
| RE-RIPTOPL-137 | WEAKENED | Two geometry blocks exist at `src/platform/sysvmc.cpp:180-181` and `:211-215`, but they are mutually exclusive per call, not two passes over one call |
| RE-RIPTOPL-138 | VERIFIED-DRIFT | Bounded `startPads()` retry duplicated at `src/app/lwnbdctl.cpp:195-203` and `:232-240`; the "near-identical comments" claim does not hold |
| RE-RIPTOPL-139 | VERIFIED-DRIFT | 1080P defined at all three sites, but 576P is **not** defined in `ee_core/src/gsm_api.c` (only 1080P at `:34`); 576P has only two sites |
| RE-RIPTOPL-140 | WEAKENED | Four definition sites confirmed (`ee_core/include/patches.h:14`, `ee_core/src/asm.S:16-17`), but the prescribed `asm.S` fix (take `ee_core.h` values there) is not asm-viable as stated |
| RE-RIPTOPL-141 | VERIFIED | Same 11×u64 layout in `ee_core/include/igs_api.h:126-142`, `ee_core/src/ra_overlay.c:46-59`, index macros `ee_core/src/igrmenu.c:549-557`; storage `.space 88` at `ee_core/src/gsm_engine.S:57` |
| RE-RIPTOPL-142 | VERIFIED-DRIFT | Alias families confirmed, corrected lines: first family `ee_core/include/igs_api.h:35-39`, `GS_GS*` family `:91-95` (not 90-94); third copy `ee_core/src/igrmenu.c:687-690` |
| RE-RIPTOPL-143 | VERIFIED | `#include <debug.h>` twice at `include/opl.h:33` and `:43`; `"config.h"` bare at `:45` vs `"include/hddsupport.h"` at `:48` |
| RE-RIPTOPL-144 | VERIFIED | Same civil-date algorithm `modules/network/smb2man/smb2man.c:1073` / `modules/network/nfsman/nfsman.c:865`, near-identical wait/read/write wrappers and `AllocSysMemory` shims in both |
| RE-RIPTOPL-145 | WEAKENED | Parsers do diverge (`modules/iopcore/igrmbox/igrmbox.c:151-168` lacks `a-f`), but both in-tree producers emit uppercase only (`ee_core/src/igrmbox.c:48,57-59`; `ee_core/src/freeze_diag.c:254,273-274`) — lowercase never reaches it |
| RE-RIPTOPL-146 | VERIFIED | `bd_defrag_read`/`bd_defrag_write` twins in `modules/bdm/libbdm/src/bd_defrag.c:1-101`, differing only in `bd->read`/`bd->write` and constness |
| RE-RIPTOPL-147 | WEAKENED | Cited location `modules/bdmevent/main.c:443-483` does not exist (file is 88 lines; snapshot is `:12-56`); BDM `refused` is private and not exported, and bdmevent's policy deliberately differs on sectorSize 0 / parNr |
| RE-RIPTOPL-148 | VERIFIED | `modules/mcemu/ioman_add.h` included by nothing; only includer in tree is cdvdman's `modules/iopcore/cdvdman/internal.h:28`; diff vs cdvdman copy is only `<iox_stat.h>` + one `lseek` signature |
| RE-RIPTOPL-149 | VERIFIED | Eight `mc_writecluster` `-2 → -1000` translation sites at `modules/vmc/genvmc/genvmc.c:267,283,378,439,488,538,554,632` with site-specific else codes |
| RE-RIPTOPL-150 | VERIFIED-DRIFT | ~20-line malloc/free shims at `modules/ilink/IEEE1394_bd/src/sbp2_driver.c:809-828` and `modules/ilink/iLinkman/src/iLink_internal.c:184-203`, but signatures differ: `malloc(int)` vs `malloc(unsigned int)` |
| RE-RIPTOPL-151 | VERIFIED | `scsi_read`/`scsi_write` twins at `modules/ilink/IEEE1394_bd/src/scsi.c:304-358`, identical except write flag 0/1 to `scsi_cmd_rw_sector`; 16-iteration retry (`SCSI_MAX_RETRIES` :15) duplicated |
| RE-RIPTOPL-152 | VERIFIED | `tools/lng_pack.sh:1-98` (98 lines) uninvoked by any workflow/Makefile/Python (grep-verified); live `language_pack()` at `tools/package_edition.py:86-105` incl. Serbian-from-Croatian case `:98-103` |
| RE-RIPTOPL-153 | VERIFIED-DRIFT | Three `release_notes` implementations confirmed, but the three escape **sets are identical** (``\ ` * _ [ ] < >``); real drift is only implementation style and `%H` vs `%h` at `tools/publish_llvm.py:184` |
| RE-RIPTOPL-154 | VERIFIED | `add()` at `tools/build_edition.py:68-69` is byte-identical to `tools/build_configuration.py:14-15` (diff-verified); import already exists at `tools/build_edition.py:13` |
| RE-RIPTOPL-155 | VERIFIED-DRIFT | Two mirror implementations confirmed; corrected starts: shell `mirror_fetch` at `tools/ps2build_sdk.sh:109` (not 123), `from_mirror` at `:348`; Python `mirror_file` at `tools/install_ports.py:83` (not 89) |
| RE-RIPTOPL-156 | VERIFIED | Identical local `struct hostent` definitions at `src/sources/ratls_ps2.cpp:39-48` and `src/sources/nettime.cpp:30-38` |
| RE-RIPTOPL-157 | VERIFIED | Stub `SPD_REG8(SPD_R_PIO_DATA) = 0; return 0;` at `modules/iopcore/cdvdman/atad.c:470-474`, real body unreachable `:476-522`; PIO caller at `:615`, IDENTIFY is type 2 in table at `:92` |
| RE-RIPTOPL-158 | VERIFIED | Refused bodies after `return -EACCES` at `modules/hdd/apa/src/hdd_fio.c:347` etc.; all `APA_CACHE_FLAG_DIRTY` setters in tree lie inside the cited dead functions; `apaRename` stray `SignalSema(fioSema)` at `:573,581,595` confirmed |
| RE-RIPTOPL-159 | WEAKENED | ~627 gated lines and stubbed exports confirmed, and `lwip_select` timeout bug at `modules/network/SMSTCPIP/sockets.c:749-765` is real — but the "cannot link" dhcp mechanism is defeated: dhcp calls are double-gated by `#if LWIP_DHCP` |
| RE-RIPTOPL-160 | VERIFIED | `apaPassCmp` no-op without `APA_ENABLE_PASSWORDS` at `modules/hdd/apa/src/password.c:25-36`; OSD branch `modules/hdd/apa/src/apa-opt.h:31-38` does not define it; DES body `:39-498` (~460 lines); live encryption at `modules/hdd/apa/src/hdd_fio.c:182,191` |
| RE-RIPTOPL-161 | VERIFIED-DRIFT | File preprocesses empty under this build's flags, but corrected VHDD/IOMANX range is `modules/hdd/apa/src/hdd_blkio.c:178-293` (not 171-303); BDM `:17-176`, generic `:295-415`, Makefile flags/`hdd_blkio.o` confirmed |
| RE-RIPTOPL-162 | VERIFIED | `audio/bgm.ogg` 324,182 B, audio/ = 9 files / 533,398 B; not in `tools/assets.py:62,182-183`; shipped copy md5 `af3e8d4b117d57161cfdcaa4fdb9b370` ≠ repo `a639ef8b7b69c10c6868f8fef3129fba`; runtime paths at `src/core/bgm.cpp:620-638` |
| RE-RIPTOPL-163 | VERIFIED-DRIFT | Dead allocator at `modules/network/SMSTCPIP/mem.c:123-377` and callerless, over-reading `mem_reallocm` at `:379-390` confirmed; corrected sys_arch citation: wrappers at `include/arch/sys_arch.h:12-20` (file is 22 lines, not 29-37) |
| RE-RIPTOPL-164 | VERIFIED-DRIFT | Commented-out predecessor blocks exist beside all three live functions in `tools/pc/iso2opl/src/iso2opl.c`, but cited ranges overstate them (e.g. `check_cfg` block ends at `:112`, `write_cfg` block at `:179`, not 128/195) |
| RE-RIPTOPL-165 | VERIFIED-DRIFT | `loopif.c` (113 lines) + `loopif.h` (39 lines) dead, `LWIP_HAVE_LOOPIF` defaults 0 at `include/lwip/opt.h:337`, no `loopif.o` in Makefile — but ps2ip glue also exists at `ps2ip.c:65-67` and `:311-313`, not only `:279-294` |
| RE-RIPTOPL-166 | VERIFIED-DRIFT | ASYNC path is dead and bypasses the semaphore — `modules/usb/usbmass_bd/src/usb_mass.c:17,440-453,469,508-538` (cited ranges off) |
| RE-RIPTOPL-167 | VERIFIED-DRIFT | `ip_forward`/`ip_lookup` never compile — `modules/network/SMSTCPIP/ip.c:79-113,139-199`; declaration is at `include/lwip/ip.h:46`, not :57 |
| RE-RIPTOPL-168 | VERIFIED-DRIFT | Dead/broken helpers real — `tools/pc/ziso.py:44-47,369-400`; but README does reference the tool — `README.md:862,872,878` |
| RE-RIPTOPL-169 | VERIFIED-DRIFT | Whole-file dead, zero references — `tools/lang_decompiler.py:1-60`; representer at :4, not :5 |
| RE-RIPTOPL-170 | VERIFIED-DRIFT | Uncalled entry points + unclamped copy — `modules/network/smb2man/usmb2/usmb2.c:792,1031`; opendir/readdir are `#ifdef`-excluded at :743,820, not gc-stripped |
| RE-RIPTOPL-171 | VERIFIED | `modules/iopcore/cdvdman/smb.c:23` (`#define USE_CUSTOM_RECV 1`), dead `#else` at :997-1020 |
| RE-RIPTOPL-172 | VERIFIED | All 12 declarations exact, no definitions in any `.c` — `modules/network/SMSTCPIP/include/lwip/api.h:104-114,155`, `pbuf.h:108,114`, `sys.h:97`, `tcp.h:427,453`, `memp.h:59` |
| RE-RIPTOPL-173 | VERIFIED | No ACK, never posted, bypassed — `modules/network/SMSTCPIP/api_msg.c:664-689,820`, `api_lib.c:383-398` |
| RE-RIPTOPL-174 | VERIFIED | Dead, silently-dropping `#else` — `modules/network/SMSTCPIP/tcpip.c:137-156`, `include/lwip/opt.h:64-65` |
| RE-RIPTOPL-175 | VERIFIED-DRIFT | Both dead — `modules/network/smap_udpbd/src/xfer.c:80,12,56`; benefit overcounts lines (~19, not ~30) |
| RE-RIPTOPL-176 | VERIFIED | Write-only counter + zero heartbeat fields — `modules/network/raudp/raudp.c:771,875,924-925,1171,1174,1177` |
| RE-RIPTOPL-177 | VERIFIED | Write-only variable, hardcoded factor — `modules/iopcore/cdvdman/device-bdm.c:22,106,234,244` (tree census: exactly 2 occurrences) |
| RE-RIPTOPL-178 | VERIFIED | Phantom prototype + 153 defines, TRANS2 group unused — `modules/iopcore/cdvdman/smb.h:501,62-93` |
| RE-RIPTOPL-179 | VERIFIED-DRIFT | Dead `dummy_fs` + unused typedefs — `modules/iopcore/imgdrv/imgdrv.c:13-16,39-58` (cited struct range :39-95 overbroad) |
| RE-RIPTOPL-180 | VERIFIED | CRC16 compiled-but-off; CRC7 test names a nonexistent macro — `modules/mx4sio_bd/src/mx4sio.h:37,45-46`, `mx4sio.c:42-49`, `spi_sdcard_crc7.c:5-45`, `spi_sdcard_driver_config.h:22` |
| RE-RIPTOPL-181 | VERIFIED | Field occurs only in its struct — `modules/bdm/bdmfs_fatfs/src/include/fs_driver.h:11` |
| RE-RIPTOPL-182 | VERIFIED-DRIFT | Four never-defined axes confirmed tree-wide; Makefile defines only the 3 claimed flags — `modules/hdd/atad/Makefile`, `src/ps2atad.c:1458,1485,1361,1399` (two cited ranges off) |
| RE-RIPTOPL-183 | VERIFIED | Broken object-like macro, never used — `modules/ilink/iLinkman/src/include/iLink_internal.h:92` |
| RE-RIPTOPL-184 | VERIFIED | Dead and incomplete deinit — `modules/ilink/IEEE1394_bd/src/sbp2_driver.c:802-807`, `writeBuffer` at :20,93, decl `include/sbp2_disk.h:180` |
| RE-RIPTOPL-185 | VERIFIED-DRIFT | All cited `PORT_NR` blocks inert — `modules/mmce/mmceman/src/sio2man_hook.c:26-29,58-85,115-121,275-281`; one further guard at :44-46 uncited |
| RE-RIPTOPL-186 | VERIFIED | Unincluded formatter + undefined diagnostic entry — `modules/pccf/format.inc` (57 lines), `modules/pccf/pcdiag_ata.h:20-21` |
| RE-RIPTOPL-187 | VERIFIED-DRIFT | Retired trees + present-tense README — `third_party/ps2sdk-socket/UPSTREAM:4-6` vs `README.md:3-7`, `tools/build_edition.py:253-257`; byte figure overstates (11,487 B, not ~21 KB) |
| RE-RIPTOPL-188 | VERIFIED-DRIFT | File dead, contents duplicated inline — `tools/pc/xeRAbora.url:1-2`, `tools/package_edition.py:184`; prescribed dict fix would drop the RA-only gate at :183 |
| RE-RIPTOPL-189 | VERIFIED-DRIFT | All no-op/commented sites real — `src/sources/bdmlaunch.cpp:983,986`, `mmcesupport.cpp:1819`, `bdmsupport.cpp:2156-2157`, `supportbase.cpp:650-665`; autolaunch block is :1735-1741, not :1740-1747 |
| RE-RIPTOPL-190 | VERIFIED | Placeholder only occurs at its definition — `src/ui/menusys.cpp:51-55` |
| RE-RIPTOPL-191 | VERIFIED | Census reproduced exactly: 1,144 literals, same 8 malformed, 13 duplicated IDs — `src/core/vmc_groups.cpp:443-444,464-465,472-473,498,614,665-676` |
| RE-RIPTOPL-192 | VERIFIED | `initialize` returns 1 at every exit; unguarded index — `src/core/pad.cpp:209,235,248,256,296,1049-1052,1136-1146` vs guards at :1002,1036,1063 |
| RE-RIPTOPL-193 | VERIFIED | Stub writes only zeros; HUD displays them — `src/core/sfx.cpp:305-311`, `include/sound.h:28`, call at `src/ui/guioverlay.cpp:203` |
| RE-RIPTOPL-194 | WEAKENED | EE stub is dead but has zero EE callers, and its name/declaration are shared with a live IOP function — `src/platform/httpclient.cpp:157,61` vs `modules/network/common/httpstream.inc:105-112` |
| RE-RIPTOPL-195 | VERIFIED | Orphan comment, no declaration/implementation follows — `include/ioman.h:38-41` |
| RE-RIPTOPL-196 | VERIFIED | Sole occurrence in the whole tree is the declaration — `ee_core/include/padhook.h:25` |
| RE-RIPTOPL-197 | VERIFIED-DRIFT | Census, sizes and all 4 md5s reproduced exactly — `gfx/` (142 files, 443,330 B; 12 files = 29,792 B), `src/gfx/textures.cpp:260-266,299-301,243`; two citation ranges off |
| RE-RIPTOPL-198 | VERIFIED | `LWIP_ARP` defined nowhere; timer compiles out; `etharp_init` uncalled — `modules/network/SMSTCPIP/timers.c:103-125`, `etharp.c:191,209-229`, `include/lwip/opt.h:138` |
| RE-RIPTOPL-199 | VERIFIED | `modules/network/SMSTCPIP/exports.tab:17-24,30` stubs; `ps2ip.c:268-271` empty `ps2ip_Stub`; real impls gated `#ifdef FULL_LWIP` (`ps2ip.c:97`, `sockets.c:630,1377`); `ps2ips.c:468-575` handlers dispatch to them |
| RE-RIPTOPL-200 | VERIFIED-DRIFT | `src/sources/hddsupport.cpp:59-62` TODO verbatim; machinery is ~295 lines, not ~260 (see reasoning) |
| RE-RIPTOPL-201 | VERIFIED | `modules/mmce/mmceman/src/mmce_fs.c:402-405` returns 0 unconditionally; registered `:1194`; sibling slots use `NOT_SUPPORTED_OP` (`:15-17` returns `-ENOTSUP`) |
| RE-RIPTOPL-202 | VERIFIED | `modules/network/SMSTCPIP/sockets.c:586-589` accepts SOCK_RAW; `LWIP_RAW` defined nowhere in tree (incl. `include/lwipopts.h`); no `raw.c`; `api_msg.c:297ff` RAW cases compiled out, `err` preset `ERR_OK`; `memp.c:75,89-91` + `opt.h:111-113` reserve 4 RAW PCBs; `raw.h` 75 lines, no implementation |
| RE-RIPTOPL-203 | VERIFIED-DRIFT | `src/core/config.cpp:1419-1495` five candidate locations exist, but the rung order/type split differs from the finding (see reasoning) |
| RE-RIPTOPL-204 | VERIFIED | `src/sources/favsupport.cpp:143-165` mode table, `:167-230` `favReadWoplFile`, `:240-270` import branch on any non-OFAV header; `favWriteFile` stamps OFAV |
| RE-RIPTOPL-205 | VERIFIED-DRIFT | `include/launchdiag.h:41` `#define LAUNCH_DIAG 0`; gated-site count and consumer list are off (see reasoning) |
| RE-RIPTOPL-206 | VERIFIED-DRIFT | `modules/network/httpclient/httpclient.c:130-133` unused enum; the Transfer-Encoding branch at `:160-165` is commented out, not live (see reasoning) |
| RE-RIPTOPL-207 | VERIFIED | `modules/hdd/xhdd/hdpro.c:8-11` stub returns 0; `xhdd.c:43-48` propagates it when `isHDPro`; `xhdd.h:2,4` prototypes have no definition in `modules/hdd/xhdd` (the same-named definition is in `modules/iopcore/cdvdman/hdpro_atad.c:742`) |
| RE-RIPTOPL-208 | VERIFIED | `modules/usb/usbd-ra/include/usbd.h:294-296` declarations + `:336-337` import slots 17/18 with "these aren't implemented" comment; `src/exports.tab` ends at slot 16 |
| RE-RIPTOPL-209 | VERIFIED | `modules/mmce/mmceman/src/mmce_sio2.c:278,381` honour `mmce_sio2_use_alarm`; `:211` (`tx_rx_pio`) and `:492,533,570` (`tx_mixed`) call `SetAlarm` unconditionally; setting at `mmce_fs.c:1157-1160` |
| RE-RIPTOPL-210 | VERIFIED | `third_party/PSUManager/README.md:12-16` claim; validator `.github/scripts/verify_psu_with_psumanager.cpp` exists but no workflow/runner references it (`tools/run_tests.sh` runs only `test_*.py`, HTTP tests, `tests/host/run.sh` — none mention PSU); vendored tests wired only under `BUILD_TESTING` in `src/core/CMakeLists.txt:9-13`; live validation is `inspect_psu` in `package_release_app.py:177` |
| RE-RIPTOPL-211 | VERIFIED | `modules/network/SMSTCPIP/ps2ip.c:273-278` `ps2ip_ShutDown` is only `return 1;`; export slot `exports.tab:4` |
| RE-RIPTOPL-212 | VERIFIED | `src/sources/supportbase.cpp:1969-2600` resolver family (632 lines); `sbResolveCustomLoaderPath` 2235-2488 = 254 lines; migrated/MC probes at 2489/2517/2539 |
| RE-RIPTOPL-213 | VERIFIED-DRIFT | `src/sources/bdmboot.cpp:254-471,472-485` two ladders exist as cited, but the full ladder sums to 24 s, not 22 s (see reasoning) |
| RE-RIPTOPL-214 | VERIFIED | `src/sources/vcdsupport.cpp:262-264` budget/memo/retry constants; memo list `:274-360`; `resolveRowId:413`; queue `requestRowId:536`/`resolveQueuedDisplayId:504`; `rowIdCached:593-620`; filename parser `vcdExtractGameId:61` |
| RE-RIPTOPL-215 | VERIFIED | `src/sources/mmcesupport.cpp:146` `sendGameID` (own poll loop), `:314` `waitCardReady`, `:373` `waitSwapDoneOn`, `:417` `waitSwapDone` (one-line forwarder), `:434` `waitBeforeHandoff`, `:451` `waitHistorySwitch`, `:500` `gameIdSettle`, `:605` `settleAfterSwitch`; composed chains at `:1412-1434,1658-1729` |
| RE-RIPTOPL-216 | VERIFIED | `src/gfx/themeviews.cpp:416,432,453` synth family, `THM_SYNTH_MAX_SLOTS 64` at `:376`; consumed `src/gfx/themes.cpp:1478-1491`; `thmViewChoose:168` already falls back to `defaultView` |
| RE-RIPTOPL-217 | VERIFIED | `src/platform/sysneutrino.cpp:303` `sysTomlTornTailCut`, `:382` UDPFS sync using `strstr(toml, "\"ip=")` at `:429`, `:522` lexer rewriter for SMB; restore-on-short-write at `:455-458` and `:742` |
| RE-RIPTOPL-218 | VERIFIED | `src/app/settingsio.cpp:743,853,938` load-side recovery cascade; `:2047` separate save-side cascade with different device order (boot → HDD → BDM → MMCE, then MC → MMCE → BDM → HDD) |
| RE-RIPTOPL-219 | VERIFIED | `src/core/config.cpp:1496-1699` `configWrite`: RAM snapshot (cap `:1496`, alloc `:1558`), MC single-open carve-out (`:1547ff`), read-back `memcmp` verify/rescue (`:1647`) |
| RE-RIPTOPL-220 | VERIFIED | `src/app/host.cpp` 292 lines; sole implementations `OplUi:26`, `OplApp:74`, `OplLibrary:98`, `OplLaunch:115`; interfaces only in `include/host.h:27,60,87,104`; bodies are one-line forwards |
| RE-RIPTOPL-221 | VERIFIED | `src/sources/achievements.cpp:144-145` `XERA_DOC_MAX`/`XERA_LIST_MAX`; JSON walker `:182-350`; parse/page `:352-445`; `malloc(XERA_DOC_MAX)` at `:427`; no other `src/sources/` file contains a JSON parser |
| RE-RIPTOPL-222 | VERIFIED | `src/sources/udpfssupport.cpp:62-63` 8-spelling table; `:459,492` `udpfsArtFolderHasFiles`/`udpfsDiscoverArtFolder`; `:1209` resolved index used in `image`; THM/LNG solved by `strcasecmp` root listing at `:382,419` |
| RE-RIPTOPL-223 | VERIFIED-DRIFT | `src/sources/supportbase.cpp:1741-1845` four attempts exist, but the case variation is extension-only in both tar and loose paths, not filename re-casing (see reasoning) |
| RE-RIPTOPL-224 | VERIFIED | `src/ui/libview.cpp` 365 lines; `retainedView:22`, `mixedView`+`mixedViewInitialized:31-32`, `pendingView`/`pendingViewValid`/`pendingViewUsesMixedRing:34-36`, `bdmSlotType:37` |
| RE-RIPTOPL-225 | VERIFIED-DRIFT | Wrapper blocks exist at all cited files, but `gui.cpp` holds 22 wrappers in the cited range, not 25 (see reasoning) |
| RE-RIPTOPL-226 | VERIFIED | `modules/pccf/pcdiag_cf.c:16-71`: pass/fail decided by candidate + COR checks before the loop; loop is 3 samples × 2 spaces × (10+5) reads with 500 ms + 2×100 ms waits feeding only `trace()`; production caller `pccf_hw.c:283` passes `quiet_trace` (`:56-59`), which discards everything |
| RE-RIPTOPL-227 | VERIFIED | `src/gfx/grid.cpp:504-510` count/index walk per draw; `src/gfx/psbbnflow.cpp:252-256` countRows walk; `src/ui/menusys.cpp:2584,2624,2660` `menuGridRow`/`menuGridPage`/`menuGridAtRowStart` each re-walk `submenu` |
| RE-RIPTOPL-228 | WEAKENED | `src/core/cheatconfig.cpp:99-104` 250 `configRemoveKey` calls (MAX_CODES=250, `include/cheatman.h:40`) — but save writes picks contiguously, so the claimed round-trip loss does not occur (see reasoning) |
| RE-RIPTOPL-229 | VERIFIED | `src/core/filemgr.cpp:225-250` `fmChildren` re-runs `fmList` from scratch on each ×4 growth to cap 65536; `fm_entry_t` is 144 bytes (`include/filemgr.h:10-15`: `name[128]`+`int`+`unsigned long long`) → 65536 × 144 = 9,437,184 B ≈ 9 MB as claimed |
| RE-RIPTOPL-230 | VERIFIED-DRIFT | `src/sources/favsupport.cpp:1747-1762` double `gatherPass` (count then fill) confirmed, but `kAllGamesModes` has 14 modes, not 15 (see reasoning) |
| RE-RIPTOPL-231 | VERIFIED-DRIFT | `modules/iopcore/cdvdman/cdvdman.c:982-998` retry wrapper (2500 × `DelayThread(2000)`, unconditional `return 1`) confirmed, but `cdvdman_writeSCmd` contains four unbounded register polls, not three (see reasoning) |
| RE-RIPTOPL-232 | VERIFIED-DRIFT | 16-retry loops exist with no delay, but at `modules/usb/usbmass_bd/src/scsi.c:289` (read) and `:339` (write), not :310-330/:356-375 |
| RE-RIPTOPL-233 | VERIFIED | Unaligned refill + slot key at `modules/bdm/libbdm/src/bd_cache.c:160-164` (`read(...,sector,...,8)`, `c->sector[blkidx_best] = sector`); geometry 8×512×32 = 128 KiB at `:9-10` |
| RE-RIPTOPL-234 | VERIFIED | `modules/network/smap_udpbd/src/xfer.c:201-214` holds `tx_sema` across `DelayThread(100)` loop; sibling `modules/network/udpfs_smap/src/xfer.c:253-277` releases/re-acquires per attempt |
| RE-RIPTOPL-235 | VERIFIED | Delay-free spin `src/platform/system.cpp:277-280` (`retry = 100; while (retry-- > 0 && fileXioDevctl(...) < 0) ;`) |
| RE-RIPTOPL-236 | VERIFIED | `src/ui/guioverlay.cpp:54` calls `drawArtHud()` every frame when `gEnableDebug`; 224-byte buffer + 32-conversion `snprintf` at `:145,211` |
| RE-RIPTOPL-237 | VERIFIED-DRIFT | 8 inner + 4 outer + 1 logo draws at `src/ui/guisplash.cpp:57-67`, but only when `tapAlpha > 0` (`:57`), not unconditionally every frame |
| RE-RIPTOPL-238 | WEAKENED | Select list walk is dead in shipped builds (`modules/network/SMSTCPIP/sockets.c:862-889`, `lwip_select` gated at `:671` under `FULL_LWIP`, export stubbed in `exports.tab:17`), but the semaphore/`rcvevent` bookkeeping in the same function is live |
| RE-RIPTOPL-239 | REFUTED | The 57 files are run in CI: `.github/workflows/ps2build.yml:71` runs `tools/run_tests.sh -k`, whose loop at `tools/run_tests.sh:5-9` globs every `.github/scripts/test_*.py` |
| RE-RIPTOPL-240 | VERIFIED | All 18 listed suites have no `subprocess`/compile step and assert over source text; worst example exact at `.github/scripts/test_mx4sio_host_boot.py:15-16` including embedded `\n` + indentation |
| RE-RIPTOPL-241 | VERIFIED | Vacuous/either-way CHECKs at `tests/host/test_config.cpp:56` and `:131`; unasserted counters at `tests/host/test_vmc_groups.cpp:16-17,29-36` — reproduced run: `97 groups, 1034 distinct titles, 12 duplicate entries, 8 odd-shaped ids` then `PASS` |
| RE-RIPTOPL-242 | VERIFIED | Skip-to-pass at `tools/run_tests.sh:8`, `.github/scripts/test_iso9660_records.py:112`, `test_install_ports.py:405,411`, plus merged sites `test_zso_validation.py:166-169`, `test_ripps2_art_port.py:131-138`, `test_ripps2_gui_port.py:192-196`, `test_neutrino_cheats.py:19-21,207-210` |
| RE-RIPTOPL-243 | VERIFIED | Extractor/scaffold duplication is pervasive (own census over all 278 suites: 107 `def function(`, 43 `def read(`, 78 `failures = []`); divergent index vs brace-counting extractors at `test_config_escape_roundtrip.py:8-10` vs `test_net_recv_poll.py:16-29` |
| RE-RIPTOPL-244 | VERIFIED | Global monkey-patch at `.github/scripts/hostmap.py:41-55` (`pathlib.Path.read_text = _read_text`, `unhost()` rewrites `host::` calls to pre-refactor names); 18 importers total (11 first-half + 7 second-half listed) |
| RE-RIPTOPL-245 | VERIFIED | Hand-written Python MIPS interpreter at `.github/scripts/test_ee_hook_context.py:19-95` (136 lines total), docstring itself says "not MIPS execution" |
| RE-RIPTOPL-246 | VERIFIED-DRIFT | Dead pre-C++ branches verified in the listed files (e.g. `test_pad_handoff.py:18-19,106-108`; old `src/*.c` paths absent, `src/core/pad.cpp`/`src/app/opl.cpp`/`src/platform/util.cpp` present) — but there are **seven** suites, not six |
| RE-RIPTOPL-247 | VERIFIED | Lossy round-trip pinned as characterisation at `tests/host/test_config.cpp:2-4` and `:102-107` (`gsm_x = 1;` written back flat); finding itself correctly labels the external impact INFERRED/Low |
| RE-RIPTOPL-248 | VERIFIED | `flavours.yml` explicitly names 154 distinct `test_*.py` of 278 in tree (own census matches); glob runner is `tools/run_tests.sh:5-9` — two mechanisms, already drifted |
| RE-RIPTOPL-249 | VERIFIED | ~150-line `shell: python` step at `.github/workflows/docs-sync.yml:43-160`; same External Tools entries written as Markdown f-string (`:68`) and HTML f-string (`:78`); no test references it |
| RE-RIPTOPL-250 | VERIFIED | Pins in `.github/requirements-docs.txt:1-4`; inline duplicates at `.github/workflows/docs-sync.yml:184` and `flavours.yml:350`; single-source uses at `wiki-publish.yml:96` (as `src/.github/requirements-docs.txt`) and flavours wiki job `:355` |
| RE-RIPTOPL-251 | VERIFIED-DRIFT | Main claim holds: `.github/workflows/ps2build.yml:69-71` has no `pip install` anywhere, `requirements.txt:1` is `PyYAML==6.0.3`, four cited suites `import yaml` — but the merged note that `test_ra_language_isolation.py` is "unwired/dormant" is wrong (see 239) |
| RE-RIPTOPL-252 | VERIFIED | Format gate is `h,c`-only at `.github/workflows/check-format.yml:5-19,31-37`; counts reconcile under the stated scope (130 `.cpp` repo-wide outside `third_party`; 115 `.cpp` / 254 `.c` / 431 `.h` under `src`/`include`/`ee_core`/`modules`); local mechanism at `Makefile:541-546` differs and ignores are not passed to CI |
| RE-RIPTOPL-253 | VERIFIED | Dead Makefile: only `.c` EE rules at `Makefile:1197-1203`, no `.cpp` rule; drift verified (assets `tools/assets.py:49-60` 104+24 glyph names vs Makefile PNG list missing `psbbn_bg`/`glyph_*`; `INTERNAL_LANGUAGE_C` at `Makefile:1213` points at nonexistent `src/lang_internal.c`, real generated path `tools/assets.py:117`); repo itself says so at `docs/PS2BUILD-MIGRATION.md:75,112` |
| RE-RIPTOPL-254 | VERIFIED | Hand-maintained size table (7 entries, three historical iLink sizes in comment) at `src/sources/vcdsupport.cpp:1699-1707`, used at `:1712-1717,1758-1765,1768-1798` |
| RE-RIPTOPL-255 | VERIFIED | Stale comment at `include/tar.h:4-8`; structs at `:53-63`; contradicted by kind table at `src/platform/tar.cpp:59-62` and CHT use at `src/sources/supportbase.cpp:1761-1784` |
| RE-RIPTOPL-256 | VERIFIED | Mixed style at `include/pad.h:6-21`: 12 `constexpr int` vs `#define KEY_CROSS/CIRCLE/TRIANGLE/R1` at `:15-18`; repo-wide grep found no `#ifdef KEY_` / `defined(KEY_` use requiring macro form |
| RE-RIPTOPL-257 | VERIFIED | `modules/bdmassault/usbd.irx.ilink` and `usbd.irx.usbexfat` both 48,500 bytes, identical SHA-256 `5ea4818ba1cf5207…` (recomputed); dedupe note at `modules/bdmassault/PROVENANCE.md:24-25` |
| RE-RIPTOPL-258 | VERIFIED | `modules/hdd/atad/ORIGIN.txt` item 4 (lines 17-20) says `.path` assignment is DROPPED; code assigns it at `modules/hdd/atad/src/ps2atad.c:359` |
| RE-RIPTOPL-259 | VERIFIED | `modules/freepad/` contains only `PROVENANCE.md:3-6`; `.github/scripts/install_coherent_freepad.sh` exists nowhere else in tree (grep: sole hit is the note itself); `*.irx` ignored at `.gitignore:15`; Makefile builds padman from stock SDK freepad at `Makefile:1121` |
| RE-RIPTOPL-260 | VERIFIED | `tools/download_lng.sh:5-10` clones branch `main` + `git reset --hard origin/main`; "pinned" claim at `tools/assets.py:5`; pinned contrasts at `tools/download_lwNBD.sh:6-9`, `tools/download_cfla.sh:5-9` |
| RE-RIPTOPL-261 | VERIFIED | Scratch dirs in version anchor exclusion list at `tools/assets.py:203-204` (`.claude`, `.agents`, `.codex`, `.codex-tmp-nhddl`, `.codex-tmp-wle-r3z`) |
| RE-RIPTOPL-262 | VERIFIED | Legacy-Makefile docs verified line-by-line: `README.md:443`, `docs/GSM.md:211-216`, `docs/LANGUAGES.md:27,76,88-99`, `lng/README.md:24-26,42,67,82`, `docs/IGR.md:110-117,120,204`, `docs/AUTOLAUNCH-GLOBAL-SETTINGS.md:139`, `docs/HTTP.md:168-169`; live truths in `ps2.yaml:609,644` (unconditional `GSM_1080P`) and `tools/build_configuration.py:7-10,37-40` (15 configurations incl. `extras`/`extras_ds5`/`diag`, no `-extra1`/`pademu1`) |
| RE-RIPTOPL-263 | VERIFIED-DRIFT | Stale status text is real but the flavours sentence is at `docs/PS2BUILD-MIGRATION.md:101`, not inside the cited `:103-106` (rolling-release sentence is `:104`, open items `:106`); workflow comment at `.github/workflows/flavours.yml:401-405` verified |
| RE-RIPTOPL-264 | VERIFIED | Badge/doc say 31 (`README.md:40`, `docs/LANGUAGES.md:69`); script count: `tools/assets.py` TRANSLATIONS = 33, `Makefile:191-193` = 33, `lng/fork/` = 33 `.yml` files |
| RE-RIPTOPL-265 | VERIFIED | All cited flat paths absent and all actual paths present: `docs/APA-SAFETY.md:43,47,49,50`, `docs/GSM.md:223-224`, `docs/HTTP.md:161,164` (doubled `tools/tools/`; real `tools/pc/http/tests/run.sh` exists), `docs/RETROACHIEVEMENTS.md:520`, `docs/LANGUAGES.md:29-30` |

## Coordinator spot-checks (independent re-reads by the coordinator)

### RE-RIPTOPL-056 — REFUTED confirmed
Finding claimed `rp_setup()` leaks earlier `AllocSysMemory` blocks when a later allocation fails (rapull.c:130-140, :199). Re-read: `rp_setup` allocates `bits`, `rp_win`, `rp_entry_off`, `rp_node_val` and, on any NULL, frees `bits` and returns 0 (rapull.c:141-145); `bits` is freed again after use on the success path. The remaining globals are freed by the sole caller `rp_thread` on any `rp_setup()` failure — `if (rp_win) FreeSysMemory(rp_win);` and likewise `rp_entry_off`, `rp_node_val`, `rp_stage` (rapull.c:337-347). The later `rp_stage` allocation's NULL return also lands in that same caller cleanup. No path leaves a block held. The refutation stands.

### RE-RIPTOPL-093 — REFUTED confirmed
Finding claimed OSDHistory's least-used-slot eviction resets the launch count but not the timestamp (OSDHistory.cpp:225-260). Re-read: the takeover branch copies out the victim record, then the shared initialisation block sets `NewEntry->LaunchCount = 1;` **and** `NewEntry->DateStamp = GetTimestamp();` (OSDHistory.cpp:255-258) — inside the very range the finding cites. The premise is false. The refutation stands.

### RE-RIPTOPL-239 — REFUTED confirmed (headline; narrow sub-claim survives)
Finding claimed 57 `.github/scripts/test_*.py` files are referenced by no workflow. Its census concatenated workflow YAML and matched filenames/globs, missing one indirection: `.github/workflows/ps2build.yml:71` runs `tools/run_tests.sh -k`, and `tools/run_tests.sh` loops `for t in .github/scripts/test_*.py` — every listed file is wired and runs in CI. The headline and the proposed loop-job fix fall. The narrow sub-claim — `tests/smb2-live/run.sh` is invoked by no workflow or runner script — survives and is the finding's only true residue. The refutation stands.

### RE-RIPTOPL-006 — WEAKENED confirmed
Finding claimed the unchecked `snprintf`-accumulation in `sysWriteNeutrinoCheats` can become a heap overwrite. Re-read (sysneutrino.cpp:961-978): the idiom is exactly as filed, but the buffer is sized `512 + pairs*32` and each pair emits a fixed 28-char line (`"    0x%08X, 0x%08X,\n"`), with header/`startup`/`userCfg` inputs caller-bounded — the would-be length never reaches the buffer size on any reachable input, so `size - len` never goes negative. Latent hardening item if the format ever grows; not a live overwrite. The weakening stands.

## Candidates index (new defects noticed while verifying — kept separate, never merged into verdicts)

21 candidates were logged across the eight blocks; full text is in each range's appendix below.

- **001–033 (3):** `do_bind` RAW branch `raw_new()` unchecked (api_msg.c) — same mechanism as RE-RIPTOPL-030, its fix should cover it · ps2ips do_send/do_sendto `sendlen - pkt->malign` underflow when malign > sendlen (ps2ips.c:415,448), adjacent to RE-RIPTOPL-013 · ZSO `c_buff += b_size` advances by the unclamped index-derived size while only the copy length is clamped (zso.c:105-120), adjacent to RE-RIPTOPL-011.
- **034–066 (4):** `icmp_time_exceeded` (icmp.c:170) repeats 049's unchecked `pbuf_alloc` pattern, gated by `#if IP_FORWARD` · `recv_raw` (api_msg.c ~:45) shares `recv_udp`'s missing-free shape (cf. 052) · `prepare_path` trailing-slash strip is dead code (the corrected reading of 037a) · mmce `MMCE_CMD_SET_GAMEID` (~mmce_fs.c:1129) uses `arg` as a string with no NULL/arglen check — a fifth site in 042's family.
- **067–099 (2):** `mmce_cmd_set_gameid` (mmce_cmds.c:230,241) `strcpy` of a full game ID into a fixed 255-byte stack `wrbuf` after u8 length truncation — IOP stack overflow for IDs >~250 bytes, distinct from 084/085 · OSDHistory victim scan (OSDHistory.cpp:174-183): `LeastUsedRecordTimestamp` not reset when a strictly smaller `LaunchCount` is found, so the equal-count tie-break compares against the wrong record.
- **100–132 (2):** third keyboard-path VLA, `src/ui/menusys.cpp:306` `char newName[nameLength]`, feeding `showKeyboard` at :309 (extends 105) · PC-tool `crctab[0x400]` oversize at `tools/pc/iso2opl/src/iso2opl.c:15` and `tools/pc/opl2iso/src/opl2iso.c:14` (same defect as 104, uncovered there).
- **133–165 (0):** none — the one extra defect noticed (apaOpen use-after-`apaCacheFree`, hdd_fio.c:510-512) is already filed as RE-RIPTOPL-043.
- **166–198 (6):** broken `CRC7_MODE_RAM_TABLE` test also in `spi_sdcard_crc7.h:10` (extends 180) · second broken `PACKET_TCODE` copy at bdmassault `iLink_internal.h:92` (extends 183) · second dead `DeinitIEEE1394` copy in bdmassault `sbp2_driver.c:775` (extends 184) · fix-completeness for 171: `test_smb_read_closed.py:64-65` hard-exits if `USE_CUSTOM_RECV` disappears, so the deletion must update that test · guard gap wider than 194 states: EE `HttpStreamBegin` (:95) and `HttpStreamRead` (:128) in httpclient.cpp also lack the init guard · `pbuf_dechain` phantom declaration also an IOP import at `modules/network/common/smstcpip.h:39-40` (extends 172).
- **199–231 (1):** `mmce_fs_ioctl2` (mmce_fs.c:1169-1183, slot :1213) returns 0 for every unknown command — same silent-success defect as RE-RIPTOPL-201, in the adjacent slot 201 does not cite.
- **232–265 (3):** `src/platform/tar.cpp:9` repeats the stale "wires only ART" comment filed in 255 against tar.h · RE-RIPTOPL-261's scratch-dir exclusion list duplicated verbatim at `Makefile:111` · `test_ra_language_isolation.py`'s PyYAML exposure is live via ps2build/run_tests.sh — an extra instance of 251's defect beyond its four listed suites.

## Appendix — per-range reasoning, duplicate checks, and candidates (verbatim from the eight verification sections)

### Range RE-RIPTOPL-001-033

## Reasoning for non-VERIFIED verdicts

### RE-RIPTOPL-006 — WEAKENED
The C pattern is exactly as claimed (sysneutrino.cpp:974-978): `len += snprintf(text + len, size - len, ...)` with no return check, so *if* `len` ever passed `size`, `size - len` (int, negative) would convert to a huge `size_t` and the failure mode would be corruption, not truncation. What does not hold is reachability with any in-tree input, which the finding itself only rated Med/INFERRED:
- Buffer is sized for the output: `size = 512 + pairs * 32` (sysneutrino.cpp:963). Each pair emits a fixed-format line `    0x%08X, 0x%08X,\n` = 28 chars < the 32 budgeted per pair.
- `pairs` is bounded by the caller: supportbase.cpp:1853-1860 counts to at most `MAX_CHEATLIST / 2` over a NUL-terminated cheat list.
- The variable header inputs are bounded: `userCfg` is capped at 64 bytes and validated by `sysNeutrinoCfgNameSafe` (sysneutrino.cpp:995-1008); `startup` is the game's startup ID string from the game record, not an arbitrary-length path. The fixed header text plus these inputs fits well inside the 512-byte headroom.
- The cited twin `sysNeutrinoHandRA` (sysneutrino.cpp:1081/1111) writes into `text[1024]` from fixed module stanzas plus dotted-quad IPs — even tighter.
True scope: a latent unsafe idiom worth the one-line guard, but no shipping configuration was shown (and none was found in this pass) that actually truncates, so the claimed heap overwrite is not reachable as filed. Fix as prescribed is sane and LESS-compliant if applied as hardening, not as a live-bug fix.

### RE-RIPTOPL-015 — VERIFIED-DRIFT
Mechanism holds, citations are off:
- The READ/WRITE(10) CDB build that masks the 64-bit LBA into 4 bytes (with the code's own 2 TiB comment) is `scsi_cmd_rw_sector` at scsi.c:136-155 (comData[2..5] at 148-151), not the cited :169-187 (those lines are inside `scsi_warmup`'s inquiry/ready loop).
- The READ CAPACITY(10) warmup derivation is at scsi.c:211-219: `bd->sectorCount = (u64)(u32)getBI32(&rcd.last_lba) + 1;` — not the cited :244-264 (those lines are `scsi_read`'s retry loop).
- With `last_lba == 0xFFFFFFFF` (saturation on >2 TiB drives) sectorCount becomes exactly 2^32 sectors, self-consistent but wrong, and any LBA past 2^32-1 wraps in the CDB — a write lands low instead of failing. BDM's sector-size refusal (scsi.c:221-225) does not catch it.
- Prescribed fix (refuse on the saturation value in warmup) is the LESS option and is sane. Corrected citations: scsi.c:136-155 and scsi.c:211-219.

### RE-RIPTOPL-028 — WEAKENED
Two halves, different fates:
- `hddGetFileBlockInfo` half HOLDS: hdd.cpp:446 `lba = subs[stat.private_4].start + stat.private_5;` indexes the caller's `subs` array (callers pass `parts[APA_MAXSUB+1]`, 65 entries — hddsupport.cpp:2561-2564) with a value taken from filesystem stat metadata, with no bounds check anywhere in the function (hdd.cpp:438-470). A crafted/foreign PFS stat with private_4 >= 65 reads out of bounds and uses the result as an LBA.
- `hddGetPartitionInfo` negative-`nsub` half DOES NOT HOLD as stated: the on-disk field is unsigned — `u32 nsub` in both libapa.h:75 and include/hdd.h:34. hdd.cpp:422 does `int nsub = header->nsub;`, so only a header value >= 2^31 converts to a negative int; in that case the copy loop `for (i = 0; i < nsub; i++)` (hdd.cpp:426) never executes and the function returns `nsub + 1`, still negative, which callers treat in the error/negative-count path — no caller loop inherits a negative count and walks with it. The real residual on this half is only that values in (APA_MAXSUB, 2^31) are already clamped (hdd.cpp:423-424) and values >= 2^31 return a bogus negative count instead of a clean error — a robustness nit, not the claimed out-of-bounds path.
True scope: one unchecked index (private_4) in hddGetFileBlockInfo; the prescribed bounds check for that index is sane and LESS-compliant. The nsub clamp-at-0 fix is harmless but defends a path that does not exist as described.

### RE-RIPTOPL-029 — VERIFIED-DRIFT
Mechanism and contrast hold; one citation points at the function entry rather than the copy:
- elfldr_noreset.cpp:233-238: PT_LOAD loop `memcpy(eph[i].vaddr, pdata, eph[i].filesz)` with no check of offset/filesz/vaddr against the blob — as cited.
- system.cpp: the `sysLaunchLoaderElf` copy is at system.cpp:1642-1651 (`pdata = boot_elf + eph[i].offset; memcpy(eph[i].vaddr, pdata, eph[i].filesz)`), not at the cited :1359, which is the function's entry line (the finding does name the function in its location, so the site is findable, but the line is ~290 lines off the copy).
- The validated contrast is real and in the same file: sysPlaceIgrOverlay at system.cpp:1308-1324 checks vaddr range and `offset + filesz > size_igrovl_irx` before copying.
- Prescribed fix (apply the same three comparisons in the two loops, no new helper) is LESS-compliant. Corrected citation: system.cpp:1642-1651.

## Candidates (new defects noticed while verifying — not merged into verdicts)

1. **do_bind RAW branch is also unchecked** — api_msg.c:414-416 (approx., in do_bind): `msg->conn->pcb.raw = raw_new(...); raw_recv(msg->conn->pcb.raw, ...)` with no NULL check, the same mechanism RE-RIPTOPL-030 files for UDP/TCP in the same switch. The 030 fix should cover this branch too.
2. **ps2ips do_send/do_sendto length underflow adjacent to RE-RIPTOPL-013** — ps2ips.c:415,448: `sceSifGetOtherData(..., sendlen - pkt->malign, 0)`; when a malformed `malign` exceeds `sendlen` (= MIN(BUFF_SIZE, pkt->length)) the length argument underflows independently of the malign_buff memcpy the finding files.
3. **ZSO block-pointer advance is unclamped, adjacent to RE-RIPTOPL-011** — zso.c:105-120: only the per-block copy length `r` is clamped to 2048; `c_buff += b_size` advances by the unclamped index-derived `b_size`, so a crafted index can steer later blocks' source reads well past the compressed data before the decoder is even reached.

### Range RE-RIPTOPL-034-066

## Reasoning for non-VERIFIED verdicts

### RE-RIPTOPL-035 — WEAKENED
Part (1) holds exactly: `sockets.c:387-390` — on `netconn_recv` returning NULL the function does `sock_set_errno(sock, 0); return 0;`. The in-tree caller makes the conflation live: `smb_RecvTimed` (`modules/iopcore/cdvdman/smb.c:288-292`) treats `result == 0` as orderly EOF and disconnects, so a stack error is indistinguishable from EOF there.
Part (2) is narrower than claimed. The only in-tree split-header caller passes a compile-time constant index of 49 (`smb.c:953`, `smb_RecvTimed(main_socket, &SMB_buf, 49, readbuf, nbytes)`), well inside the 63 bytes copied at `sockets.c:401`, and the header buffer is the 1024-byte `SMB_buf`, not a 63-byte allocation — so the read at `sockets.c:402` cannot be pushed out of range by any in-tree caller or by wire data (the index is a caller argument, not a packet field). The current code also already guards downstream use: `sockets.c:410-415` treats `avail_len < 63 || index > avail_len` by delivering the segment whole into the header. What survives is a latent API-contract defect (an arbitrary future caller passing `index >= 63` would read un-copied bytes of its own header buffer), not the wire-adjacent trust problem the headline implies. Fix direction (validate index before dereferencing it) remains sane.

### RE-RIPTOPL-037 — WEAKENED
Part (b) holds: `smb_dopen` (`smb_fio.c:660-663`) does `strncpy(fh->name, path, 255)` into the 256-byte field (`:69,78`), then `strlen(fh->name)` and two `strcat`s — a prepared path of ≥255 chars (prepared paths can run to `SMB_PATH_MAX` 1024, `:85`) leaves the field unterminated and the appends read/write past it. `smb_open` at `:361` has the same unterminated pattern with `strncpy(..., SMB_NAME_MAX)` = 256 into a 256-byte field.
Part (a) is refuted. At `smb_fio.c:312-313`, `p2 = &path[strlen(path)]` points at the NUL terminator; the loop condition `(*p2 == '\\' || *p2 == '/')` is false on the first test for **every** input — empty, all-slash, or normal — so `p2` never decrements at all and can never step in front of the buffer. The loop is dead, not dangerous (see Candidate 3 for the functional consequence). The finding's stated mechanism for (a) ("steps in front of the buffer before the loop condition saves it") inverts the actual evaluation order: the condition is tested before any decrement.

### RE-RIPTOPL-052 — WEAKENED
The code defect is real: `recv_udp` (`api_msg.c:89-106`) delivers only when `conn->recvmbox != SYS_MBOX_NULL` and has no else branch, so in that state the pbuf is neither delivered nor freed (contrast the `conn == NULL` and netbuf-alloc-failure paths in the same function, which do free).
The claimed arrival window, however, is not established. Every netconn entry point that can make a UDP PCB receivable creates the recvmbox **before** the PCB can match a packet: `netconn_bind` (`api_lib.c:293-294`), `netconn_connect` (`api_lib.c:322-323`), and `netconn_recv` (`api_lib.c:412`) all create it when NULL, and binding/connecting is what gives the PCB a local port/address for `udp_input` to match. Between `do_newconn` installing `recv_udp` (`api_msg.c:283+`) and any bind/connect, the PCB's local port is 0 and no ordinary datagram is delivered to it. The finding labels the window INFERRED; on re-derivation no concrete in-tree sequence reaches the NULL-mbox state with a deliverable datagram. The one-line fix is still correct hardening, but as a live leak the claim is unproven.

### RE-RIPTOPL-055 — WEAKENED
The uninitialised-data read is real: in `ra_discover` (`modules/network/raudp/raudp.c:793+`), `from` is an uninitialised stack `sockaddr_in`; after the caduceus block, the pinned check at `:897-899` reads `from.sin_addr.s_addr` without first requiring `got > 0`, and this stack's `lwip_recvfrom` does not write `from` on its error/EWOULDBLOCK paths — so on a timeout the comparison runs on stale (previous iteration) or uninitialised bytes.
The security consequence is overstated. That comparison can only `continue` (skip); acceptance of a peer additionally requires `got >= 8`, the `"RAO1 OK "` prefix, a non-broadcast IP, and a successful ARP MAC resolution (`:902-915`), and the caduceus branch proper does check `got > 0` before using `from` (`:884-894`). A garbage `from` therefore causes at most a spurious skip of one poll iteration — it cannot cause a wrong server to be pinned or trusted. True scope: an uninitialised/stale read feeding a skip-only gate, worth the one-line `got > 0` guard, not a trust-decision bypass.

### RE-RIPTOPL-056 — REFUTED
The finding claims `rp_setup()`'s allocations leak "for the life of the module" when a later allocation fails because earlier blocks "are not freed before the failure propagates". The defeating evidence is the sole caller. `rp_setup` (`modules/network/rapull/rapull.c:130`) is called from exactly one site, `rp_thread` at `:337`, and that caller frees every persistent block on any setup failure:

```c
if (!rp_setup()) {
    if (rp_win)       FreeSysMemory(rp_win);
    if (rp_entry_off) FreeSysMemory(rp_entry_off);
    if (rp_node_val)  FreeSysMemory(rp_node_val);
    if (rp_stage)     FreeSysMemory(rp_stage);
    FreeSysMemory((void *)rp_list);
    ExitDeleteThread();
    return;
}
```

(`rapull.c:337-347`.) Inside `rp_setup` itself, the scratch `bits` block is freed on both the partial-allocation path (`:141-145`, which frees `bits` before returning 0 — contrary to the finding's "not freed" framing for that block) and the normal path (`:195`). The `rp_stage` failure at `:199-201` likewise returns 0 into the same caller cleanup. No allocation survives a failed setup; there is no lifetime leak. The prescribed fix (free-on-failure inside `rp_setup`) would duplicate cleanup the caller already performs.

## Candidates (new defects noticed while verifying — not merged into verdicts)

1. **Sibling unchecked `pbuf_alloc` in `icmp_time_exceeded`** — `modules/network/SMSTCPIP/icmp.c:170` repeats RE-RIPTOPL-049's pattern verbatim (`q = pbuf_alloc(...)` then writes through `q->payload` with no NULL check). It is gated by `#if IP_FORWARD`, which is off in this build, so it is currently dead — but it is a second copy of the same defect if forwarding is ever enabled.
2. **`recv_raw` shares `recv_udp`'s missing-free shape** — `modules/network/SMSTCPIP/api_msg.c` `recv_raw` (starting ~`:45`) likewise posts only when `conn->recvmbox != SYS_MBOX_NULL` with no else-`pbuf_free`. Same reachability caveat as RE-RIPTOPL-052 applies.
3. **`prepare_path`'s trailing-slash strip is dead code (functional, not memory-safety)** — `modules/network/smbman-ra/smb_fio.c:312-314`: because `p2` starts on the NUL terminator, trailing slashes/backslashes are never stripped, and the copy loop at `:318` copies through `p2` inclusive. Prepared paths therefore retain trailing separators the code intends to remove (the copy loop's own bound keeps this memory-safe). This is the corrected reading of RE-RIPTOPL-037(a).
4. **A fifth unchecked devctl `arg` use in mmce** — `modules/mmce/mmceman/src/mmce_fs.c` `MMCE_CMD_SET_GAMEID` case (~`:1129`) does `char *str = (char*)arg; mmce_cmd_set_gameid(str);` with no `arg == NULL`/`arglen` validation — the same family as RE-RIPTOPL-042's four counted cases, uncounted there (string argument rather than `*(u32*)arg`).

### Range RE-RIPTOPL-067-099

## Reasoning for non-VERIFIED verdicts

### RE-RIPTOPL-068 — VERIFIED-DRIFT
The defect and fix are exactly right: `modules/bdm/bdmfs_fatfs/fatfs/diskio.c:131` writes a `u64 sectorCount` (struct definition in the PS2SDK `bdm.h` this module compiles against; the in-tree copy at `modules/bdmassault/ilink_bd_assault/include/bdm.h:36` also declares `u64 sectorCount`) through an `unsigned int *`, and this FatFs build has `FF_FS_EXFAT 1` and `FF_LBA64 1` (`modules/bdm/bdmfs_fatfs/fatfs/include/ffconf.h:218,246`), so `LBA_t` is `QWORD` and `*(LBA_t *)buff = mounted_bd->sectorCount;` is the correct one-line fix. Two details drift. (1) Scope of harm: a tree-wide grep shows the only `GET_SECTOR_COUNT` consumers are `create_partition` (`fatfs/ff.c:5922`, feeding f_fdisk) and `f_mkfs` (`fatfs/ff.c:6152`) — the mount path calls only `GET_SECTOR_SIZE` (`fatfs/ff.c:3515`). So the wrong geometry bites formatting/partitioning of >2 TiB volumes, not runtime mount "size/validate" as the finding states. (2) Residue: because the caller passes an uninitialised `LBA_t sz_drv`/`sz_vol`, the 32-bit write leaves the high word as stack garbage rather than producing a clean wrap "to a small number" — the outcome is wrong either way, but not the tidy wrap described.

### RE-RIPTOPL-070 — VERIFIED-DRIFT
The core mechanism holds completely: in `sd_detect` (`modules/mx4sio_bd/src/mx4sio.c:611-646`), a non-zero CMD13 status triggers `spisd_recover()` (`modules/mx4sio_bd/src/spi_sdcard_driver.c:393-411`), which re-runs `spisd_get_card_info()` and overwrites `bd.sectorCount` in place; `bdm_disconnect_bd` is called only when recovery fails (`mx4sio.c:636-641`). Partition devices, the mounted FatFs volume and the cache therefore survive a successful "recovery" — including a mid-glitch card swap — describing a card that may no longer be in the slot, and the prescribed disconnect-before / reconnect-after fix uses calls already present in the same function. The drift is one harm clause: "the bd_cache contents … Cached metadata from card A can be written back to card B." The BDM cache is not write-back — `_write` invalidates the cached range and writes through (`modules/bdm/libbdm/src/bd_cache.c:173-183`), and `_flush` is a pass-through (`:185-192`). Nothing cached is ever written back. The real stale-cache harm is the read side: post-swap reads can hit card A's cached blocks and FatFs can compute later writes from that stale metadata (the cache's own comment at `bd_cache.c:151-156` describes exactly this corruption chain), compounded by FatFs's own sector window. Conclusion unchanged; the write-back clause should be corrected to stale-reads-feed-later-writes.

### RE-RIPTOPL-080 — WEAKENED
The sharing facts are all verified: `MAX_PADS` is 2 (`modules/ds34usb/iop/ds34usb.c:20`), there is a single `static u8 usb_buf[MAX_BUFFER_SIZE + 32]` (`:62`), a single global `usb_resulCode` written by `usb_data_cb` for both pads (`:231-242`), and `ds34usb_get_data` (`:701-727`) waits on and polls the per-pad `sema` that the shared-result callback signals. What does not survive is the headline interleave as framed — "Interleaved operations on two pads can overwrite each other's buffer and result." All pad operations enter through one RPC server function (`rpc_sf`, `:901-933`) dispatched by a single `SifRpcLoop` thread (`:893-898`), so two pads' RPC operations are serialised and cannot interleave with each other. The genuine hazard is narrower and mostly single-pad: asynchronous contexts that bypass the RPC thread — the USB configuration callback (`usb_config_set`, invoked via `UsbSetDeviceConfiguration`) running `LEDRumble` over the shared `usb_buf`, and an interrupt transfer left pending by a `TransferWait` timeout completing into `usb_buf` / flipping the shared `usb_resulCode` while a later operation (either pad) uses them. The finding itself labels the interleaving INFERRED at Med confidence. True scope: shared buffer/result is unsafe against the driver's own async completion/callback paths, not against concurrent two-pad RPC use. The per-pad-struct fix remains sane and LESS-compliant.

### RE-RIPTOPL-089 — WEAKENED
Both cited lines exist verbatim: `fmDirSlash(parent, sizeof(parent), strcpy(target, parent))` at `src/ui/guifilemgr.cpp:446` and `strcpy(fmClip[keep], fmClip[i])` at `:471`. But the claimed risk — "correct only under a specific evaluation/aliasing reading" — overstates it. At line 446 the `strcpy` is an argument expression whose side effect completes before `fmDirSlash` is entered under ordinary C++ sequencing (argument evaluation order cannot change the result: the copy's value and effect are the same in any order), and inside `fmDirSlash` (`:407-416`) the destination `parent` and the source `target` are two distinct `FM_PATH_LEN` arrays, so no aliasing exists in the callee at all. At line 471, source and destination are distinct slots of the same `fmClip` array of `FM_PATH_LEN` (`:62`), and every slot is filled through the bounded `fmPath(..., FM_PATH_LEN, ...)` (`:380`), so the unbounded `strcpy` cannot overrun as the code stands. What remains is a genuine readability/maintainability smell — a nested copy-then-transform that invites a future edit to break it, and an unbounded copy whose safety is by-construction rather than by-check — which is what the finding's own "(fragility)" tag concedes. The two-line fix is harmless and in-style, but there is no present correctness or security defect.

### RE-RIPTOPL-092 — WEAKENED (one half refuted)
The parsing half holds, and is in fact understated: `finishResponse` parses Content-Length with `atoi` at `src/platform/ratls.c:177`, `raTlsDownload` parses the same header with a bounded digit loop that bails before overflow at `:423-433` — and there is a third site the finding does not count, `responseComplete`'s `atoi` at `:157`. The global half is refuted. `int raTlsBodyLength;` at `:371` is not "a symbol nothing outside the file needs": it is declared `extern int raTlsBodyLength;` in the public header (`include/ratls.h:66`) and is read by `src/ui/selfupdate.cpp:183,185,201` to size and allocate the self-update download buffer. The prescribed fix — "make the global `static`" — would break that consumer and fail to link. True scope: unify the Content-Length parsing on the existing bounded digit loop (three sites, not two); leave the global's linkage alone.

### RE-RIPTOPL-093 — REFUTED
The claim is that when a new title takes over the least-launched slot, "the slot's launch count is reset for the new title but its timestamp is not — the new entry inherits the previous title's last-played time." The cited range itself defeats it. In `src/core/OSDHistory.cpp:244-259`, the blank-slot and eviction paths converge on a single "Initialize the new entry" block that sets, together: `strncpy(NewEntry->name, …)` (`:254`), `NewEntry->LaunchCount = 1;` (`:255`), `NewEntry->bitmask = 1;` (`:256`), `NewEntry->ShiftAmount = 0;` (`:257`), and `NewEntry->DateStamp = GetTimestamp();` (`:258`). The eviction branch (`:245-251`, `NewEntry = &HistoryEntries[LeastUsedRecord]`) falls straight into that block. The new entry therefore gets a fresh timestamp, not the evicted slot's. The prescribed "one assignment at the takeover site" already exists. (A different, real wart in the victim-selection scan above it is listed under Candidates — it is not this finding's claim.)

### RE-RIPTOPL-095 — VERIFIED-DRIFT
The substantive claim is exact. Independently re-grepping every header under `include/` and `ee_core/include/` for `#ifndef` / `#pragma once` returns precisely the 10 named files as unguarded — no more, no fewer — and every per-file line count in the finding matches `wc -l` (101/6/25/3/34/1/21/94/94/1). `ee_core/include/modules.h` does define `enum OPL_MODULE_ID` plus the `irxptr_t`/`irxtab_t` typedefs with no guard, so the double-inclusion hard-error scenario is real; no header in scope uses `#pragma once`, and no duplicate guard names exist in scope (both re-checked). The census figures around that core are wrong and internally inconsistent: the finding says "the other 128 in-scope headers" and "all 148 in-scope headers", which do not even sum (128 + 10 = 138 ≠ 148), while the actual scope as cited contains 169 headers, of which 159 are guarded. Corrected values: **169 total, 159 guarded, 10 unguarded**.

### RE-RIPTOPL-096 — WEAKENED
Everything measurable checks out: `MAX_CHEATLIST = MAX_HOOKS*2 + MAX_CODES*2 = 510` (`include/cheatman.h:39-41`), reused as the per-entry `code_t codes[MAX_CHEATLIST]` dimension (`:67`), table allocated as `MAX_CODES * sizeof(cheat_entry_t)` in `ensureCheatTable` (`src/core/cheatman.cpp:454-457`) at ≈250 × (129 + 510×8 + 4) ≈ 1.03 MB (the file's own comments at `cheatman.cpp:41,448` agree), and the same three constants duplicated in `ee_core/include/cheat_engine.h:27-29`. The weakening is in the fix's stated basis. The finding asserts "No single cheat can legitimately carry 510 codes" and directs that the new per-cheat cap be "the largest code count a single cheat entry can hold by construction of the .cht parser" — but the parser's per-cheat bound *is* this constant: `if (cheat_index >= 0 && code_index < MAX_CHEATLIST) gCheats[cheat_index].codes[code_index++] = code;` (`src/core/cheatman.cpp:373`), with `code_index` reset per cheat name (`:351`). By the finding's own derivation rule, the honest cap is 510 and nothing shrinks. A smaller cap is still justifiable — the flattened engine list `gCheatList[MAX_CHEATLIST]` (`cheatman.cpp:35`) holds at most 255 codes total across *all* cheats (two `u32` words each, enforced at `:524-525`), so per-cheat storage beyond that can never reach the engine — but that is a different argument from the one the finding makes, and any shrink must change the parser bound at `:373` in the same edit or the parser will write past the smaller array. True scope: waste and cross-header duplication verified; the fix needs the flattened-list justification and a parser-bound change to be safe.

## Candidates (new defects noticed while verifying — not merged into any verdict)

1. **`mmce_cmd_set_gameid` stack overflow on long game IDs** — `modules/mmce/mmceman/src/mmce_cmds.c:230,232,241`: `u8 len = strlen(ptr) + 1` truncates, then `strcpy(str, ptr)` copies the *full* string into the fixed `u8 wrbuf[0xFF]` stack buffer regardless of `len`. A game ID longer than ~250 bytes overflows the IOP stack buffer before the `len + 5` send at `:244` is reached. Distinct mechanism from RE-RIPTOPL-084 (which covers only this function's one trailing uninitialised byte) and from RE-RIPTOPL-085 (filename-length truncation in `mmce_fs.c`); same `u8`-length family, worse consequence.
2. **OSDHistory least-used victim scan does not track the victim's timestamp** — `src/core/OSDHistory.cpp:174-183`: when a strictly smaller `LaunchCount` is found, `LeastUsedRecord`/`LeastUsedRecordLaunchCount` are updated (`:174-177`) but `LeastUsedRecordTimestamp` is not reset to that record's `DateStamp`, so subsequent equal-count comparisons (`:178-183`) are made against a previous candidate's timestamp (or `INT_MAX`). Among equal-count records the "oldest" tie-break can therefore pick the wrong victim. Distinct from RE-RIPTOPL-093's (refuted) claim, which concerned the *new* entry's fields after takeover.

### Range RE-RIPTOPL-100-132

## Reasoning for non-VERIFIED verdicts

### RE-RIPTOPL-100 — VERIFIED-DRIFT
Mechanism holds: the MINI profile reserves `maxIsoTransfDesc = 0x40`, `maxTransDesc = 0x40`, `maxIoReqs = 0x100`; `initHcdStructs` allocates the IsoTD pool and the IsoTD→IoReq LUT from those counts (`modules/usb/usbd-ra/src/hcd.c:366-375,457-462`); no isochronous client exists in the tree (grep for isochronous users outside `usbd-ra` returns nothing; clients are `usbmass_bd` bulk and the hub driver). The halted-endpoint drain slip is real: inside the `curTd` loop the code clears the *original* TD's LUT slot. Corrections: (1) the MINI block is at `usbd.c:50-55` — line 38 is the *non*-MINI `maxIsoTransfDesc = 0x80`; (2) the slip is at `usbd.c:139`, not :130; (3) `setupIsocronTransfer` starts at `usbio.c:152`, not :143; (4) `processDoneQueue_IsoTd` starts at `usbd.c:97`, not :96. Scope note the finding misses: the main loader loads usbd with zero arguments (`src/platform/system.cpp:411`, `sysLoadModuleBuffer(&usbd_irx, size_usbd_irx, 0, NULL)`), so the full pool is paid there — but ee_core already shrinks it for the pad stack (`ee_core/src/iopmgr.c:223`: `itd=2\0ioreq=24`), which both confirms the fix mechanism works and narrows "fully provisioned" to the main load. Fix (use the existing `itd=`/`ioreq=` argument surface) is LESS-NOT-MORE compliant.

### RE-RIPTOPL-103 — VERIFIED-DRIFT
Mechanism and fix hold: `include/utf8.h` defines the table and a non-inline `uint32_t static utf8Decode(...)` (reversed qualifiers confirmed) in the header; exactly two includers (`src/gfx/fntsys.cpp:12`, `src/gfx/textshape.cpp:12`). Correction: the table is **400 entries (400 bytes)**, not 364 — counted entry-by-entry (256 byte-class entries + 144 transition entries, `include/utf8.h:21-36`). The finding understates its own saving by 36 bytes per includer. Location range :21-49 covers table (:21-36) plus function (:38-49) correctly.

### RE-RIPTOPL-108 — VERIFIED-DRIFT
Duplication and bidirectional drift hold: file line counts are exact (pademu `ds34bt.c` 1425 vs standalone 1719; pademu `ds34usb.c` 616 vs standalone 998); both `smsutils.S` copies are md5-identical (`3612cf1bd66a11f0b9e53ac78a5940d7`); standalone-only `outEndp` initialisation confirmed (`modules/ds34usb/iop/ds34usb.c:168-169,221,958`) and the pademu-only `report->Power` bound confirmed (`modules/pademu/ds34bt.c:1240` vs unguarded use at `modules/ds34bt/iop/ds34bt.c:1333`). Corrections: the diffs measure **785 lines (bt) and 834 lines (usb)** of `diff` output, not 915 and 950. The "~2,600 lines removed" benefit is also loose: deleting the pademu copies removes 2,041 lines; deleting the standalone copies removes 2,717.

### RE-RIPTOPL-110 — VERIFIED-DRIFT
The six-copy launch ritual and its drift are real (all six cited functions exist at the cited ranges; ETH and MMCE per-part loops are byte-identical including the "Sum the size of all parts… upstream OPL #1763" comment at `ethsupport.cpp:1449` / `mmcesupport.cpp:1480`). Corrections: (1) censuses are inflated — actual call sites (excluding definitions and comments) are `sbPrepare` **8** (not 11), `sbLoadCheats` **9** (not 10), `sbLoadImage` **8 calls** (9 only counting the definition), `CheckPS2Logo` **7 calls** (not 10), `mmceSendGameID` **14 calls / 25 total references** in `src/` (not 21); (2) the "per-part size loop is byte-for-byte the same logic in three files, each headed by the identical comment" claim fails for BDM: the loop at `bdmlaunch.cpp:811-859` is a fragment-list loop (`fileXioIoctl2`/`USBMASS_IOCTL_GET_FRAGLIST`) with the size summation folded in and no such comment — only ETH and MMCE share the identical loop+comment.

### RE-RIPTOPL-118 — WEAKENED
Two of the three copies match the claimed shape exactly: `diaDrawHint` (`src/ui/dia.cpp:75-110`, 384-byte wrap buffer) and `Notifications::render` (`src/ui/guinotify.cpp:47-84`, 512-byte buffer, comment explicitly citing `diaDrawHint`). The modal copy does not: `guiDrawMsgText` (`src/ui/guimodal.cpp:115-122`) wraps into 512 bytes at a fixed `gGuiScreenWidth - 120` width with no `fntCalcDimensions` measurement and no box clamp, and `guiDrawWrappedLines` (:90-113) draws no rect at all — it centres pre-wrapped lines using a separate 192-byte per-line buffer. So "each independently measures with `fntCalcDimensions`, clamps a box, and draws rect + string" is true of two copies, not three; the genuinely shared part is only the `fntWrapText` + `lines * MENU_ITEM_HEIGHT` pattern. True scope: one full duplicate pair plus a partial third; the ~60-line saving is overstated.

### RE-RIPTOPL-123 — WEAKENED
The duplication is exact: the `got > 64 && got % 64 > 48` peek/truncate pattern occurs at exactly the three cited sites and nowhere else (grep-verified), including the named-vs-magic flag divergence (`0x08 | 0x01` in the ratls/nettime copies). The prescribed fix is not sound as written: it says to delete the ratls and nettime copies and have both call ranet's implementation via `raNetTcpRecv`. But `ranet.cpp` is compiled **only in RA editions** (`tools/build_edition.py:95-98`, sources added under `if options["ra"]`), while `ratls_ps2.cpp` is also compiled in non-RA updater/slim builds (`tools/build_edition.py:137-142` adds it with the slim mbedTLS target) and `nettime.cpp` is in the base source list for every edition (`ps2.yaml:641`). That is precisely why the non-RA ratls branch carries its own copy under `#else` (`ratls_ps2.cpp:147-165`). Deleting those copies in favour of `raNetTcpRecv` would break the non-RA link. True scope: duplication real; the fix needs a shared home outside the RA-only file (e.g. the implementation moved to a always-compiled source), not a call into ranet.

### RE-RIPTOPL-125 — WEAKENED
Three of the four transports implement the full graduated poll exactly as claimed — `smb.c:302-304` (`DelayThread(polls++ < 20 ? 100 : 1000)`), `smb2.c:287-325` (`waited_us` accounting), `nfs.c:70-95` — and those three carry the `#399` comment. The fourth does not: `RecvBounded` in `device-http.c:20-35` polls with a flat `DelayThread(1000)` — there is no 100 µs phase and no `#399` comment; it shares only the `MSG_DONTWAIT` poll + 10 s budget shape, with tick-subtraction accounting. True scope: the graduated-poll duplication is 3×, not 4×; the HTTP copy is a partial sibling, so a shared helper extracted from the three would not drop into the HTTP transport unchanged.

### RE-RIPTOPL-126 — VERIFIED-DRIFT
Duplication and divergence-in-reach hold: the timing values in `xatad.c:8-52` and `ps2atad.c:1486-1529` are byte-identical; atad's MDMA copy sits under `#ifdef ATA_MWDMA_MODES` (`ps2atad.c:1485-1505`) and the atad Makefile (`modules/hdd/atad/Makefile:7`) defines `ATA_USE_DEV9/GAMESTAR_WORKAROUND/ENABLE_BDM` but not `ATA_MWDMA_MODES`, so that copy is dead while xhdd's is live. Correction: the fix's premise that `opl-hdd-ioctl.h` is a header "both modules already use" is wrong for atad — the header lives at `modules/hdd/common/opl-hdd-ioctl.h` and is included only by the xhdd modules (`xatad.c:5`, `xhdd.c:8`, `hdpro.c:5`, via `-I../common` in `modules/hdd/xhdd/Makefile:4`); `ps2atad.c` does not include it and the atad Makefile has no `-I../common`. The fix therefore also needs an include-path change in atad, not just a move into an already-shared header.

### RE-RIPTOPL-128 — WEAKENED
The duplication is exact: `Ranet::nicBusy`/`nicReserved` (`ranet.cpp:217-231`), the non-RA `ratlsNicReserved` copy (`ratls_ps2.cpp:143-153`, with the admitting comment), and the inlined union at the top of `netTimeFetch` (`nettime.cpp:138-140`) spell out the same five-term predicate. The prescribed fix — "delete the ratls and nettime copies; both call the ranet predicate" — fails for the same build reason as RE-RIPTOPL-123: ranet is RA-only (`tools/build_edition.py:95-98`), while `nettime.cpp` builds in every edition (`ps2.yaml:641`) and `ratls_ps2.cpp` builds in non-RA updater editions; the RA branch of ratls already does call `raNetNicReserved()` (`ratls_ps2.cpp:140`). The copies exist in the `#else` branch because the canonical predicate is not linked there. True scope: duplication real; consolidation requires moving the predicate to an always-compiled source, not calling into ranet.

## Candidates (new defects noticed while verifying — not merged into verdicts)

1. **Third keyboard-path VLA, device-derived length** — `src/ui/menusys.cpp:306`: `char newName[nameLength]` where `nameLength` comes from `gsOf(support)->nameLength(support, row.id)` (:302, per-backend implementations in every `*support.cpp`), guarded only by `nameLength <= 0`. It then feeds `Gui::showKeyboard(newName, nameLength)` (:309), i.e. it is both another instance of the RE-RIPTOPL-105 pattern and a caller whose length is not a fixed dialog size.
2. **PC-tool `crctab[0x400]` oversize** — the same 4× oversized table as RE-RIPTOPL-104 exists in both PC tools: `tools/pc/iso2opl/src/iso2opl.c:15` and `tools/pc/opl2iso/src/opl2iso.c:14` (`u32 crctab[0x400]`, only indices 0–255 filled/used, rebuilt on every `crc32()` call). RE-RIPTOPL-127 covers these files' duplication but not the oversize/per-call rebuild.

### Range RE-RIPTOPL-133-165

## Reasoning for non-VERIFIED verdicts

### RE-RIPTOPL-133 — WEAKENED
Duplication of the FNV-1a core is real: both use seed 2166136261u and prime 16777619u (`artworker.cpp:13-24`, `artindex.cpp:54-71`). But the finding's central simplification — "the artworker version is the plain NUL-terminated special case of it" — is false. `ArtIndex::hashName` uppercases `a-z` before hashing (`artindex.cpp:60-62`); `focusHashValue` does not. They therefore disagree on any mixed-case string. The prescribed first fix (delete `focusHashValue` and call the artindex hash) changes semantics: `ArtWorker::updateFocus` (`artworker.cpp:25-34`) early-returns when list + length + hash match, so a focus change that only changes letter case would, under the folded hash, be treated as "no change" and the focus epoch would not advance. True scope: one shared FNV core with a case-fold flag (the finding's parenthetical shared-static alternative), not a direct substitution. Hence narrower than claimed.

### RE-RIPTOPL-137 — WEAKENED
Two geometry blocks exist, but they never both execute on one call, so "validates twice" overstates it. Order in `sysCheckVMCEx` (`sysvmc.cpp:138-282`): if `formatProbe && vmc_superblock == NULL`, `vmc_superblock` is first set to a local `autoSuperblock`; the strict block (`page_size != 512` rejected, `:180-181`) is guarded by `vmc_superblock == NULL`, so it runs only for size-only callers. The lenient block (`page_size != 0`, `:211-213`, plus magic/type/non-zero cluster checks and, for formatProbe, an additional `page_size == 512` requirement at `:215`) runs only when a superblock is (or was made) present. The passes are alternative validations for different caller modes with different failure semantics (immediate `SYS_VMC_REPLACE_REQUIRES_DELETE`/`SYS_VMC_CREATE_FAILED` return vs setting `size = 0`). The prescribed fix — keep only the `== 512` rule at the top and delete the second pass — would, as written, delete the only validation superblock callers get and would impose 512 on a path that deliberately accepts other page sizes subject to the size equation. Real scope: divergent duplicate *statements* of the geometry rule in one function; consolidation must preserve both caller modes and the formatProbe extras.

### RE-RIPTOPL-138 — VERIFIED-DRIFT
The retry loop is duplicated as claimed: load path `lwnbdctl.cpp:195-203` (`padStatus`/`padTries`, 100 tries, `delay(50)`, log on failure) and unload path `:232-240` (`ret`/`tries`, same bound/delay/log shape). Drift: (1) the finding's "including near-identical explanatory comments" is wrong — the load site has a one-line comment ("init all pads, but never wedge the status screen…", `:193`), the unload site has a long `/* Bounded retry (#307)… */` block (`:228-231`); they are not near-identical. (2) Second-site range is more precisely `:232-240`. Mechanism and helper fix hold.

### RE-RIPTOPL-139 — VERIFIED-DRIFT
Corrected census: `GS_MODE_DTV_1080P` (0x54) is defined three times — `include/renderman.h:21` (`constexpr int`), `ee_core/include/gsm_defines.h:35` (bare, no `.equ`), `ee_core/src/gsm_api.c:34` (`#define`, with the "kept in sync" comment at `:31-33`). `GS_MODE_DTV_576P` (0x53) is defined only **twice** — `renderman.h:18` and `gsm_defines.h:34` — and is *not* defined in `gsm_api.c` at all (grep: the only 576P-related items there are `gs576P_param`/`setdve_576P`, a different mechanism). The finding's "again in C inside gsm_api.c" and its fix ("delete the gsm_api.c copies", plural) therefore apply to exactly one constant, not both. Style-drift claims for `renderman.h` and `gsm_defines.h` verified as written.

### RE-RIPTOPL-140 — WEAKENED
The four-site census and the `patches.h` half hold: `ee_core.h:59-66`, `iosupport.h:97-104`, `patches.h:14` (`COMPAT_MODE_4 0x08`), `asm.S:16-17` (modes 3, 6); `ee_core/src/syshook.c` includes both `ee_core.h` (`:10`) and `patches.h` (`:15`). Two qualifications narrow it: (1) the `patches.h` redefinition is a *legal identical* redefinition (same replacement list `0x08`; comments are stripped), so it produces no required diagnostic — "live today" is true only textually. (2) The prescribed fix for `asm.S` — delete its local defines and take `ee_core.h`'s values there — is not viable as stated: `asm.S` is assembler, includes only asm-safe headers (`ee_cop0_defs.h`, `syscallnr.h`, `as_reg_compat.h` at `:11-14`), while `ee_core.h` is a C header carrying `extern` declarations, enums and C prototypes (`ee_core.h:36-70`) that cannot be included from that TU. A fix needs an asm-safe shared defines header, which is a new file/abstraction the finding does not propose. Deleting the `patches.h` copy remains sound.

### RE-RIPTOPL-142 — VERIFIED-DRIFT
Substance verified: plain address constants exist in both families (`igs_api.h` `GS_*` and `GS_GS*`), both are used (`GS_GS*` in `ee_core/src/igs_api.c:222-230`; `GS_*` in `igrmenu.c:731-805`), and `igrmenu.c:687-690` redefines the four `GS_*` locally. Line drift: first family is `:35-39` (including `GS_FINISH 0x61` at `:39`, which the finding itself cites), and the `GS_GS*` family is `:91-95`, not `:90-94`. Note the `GS_GS*_SET(...)` constructor macros at `:76-89` are *not* duplicates and are correctly not part of the claim.

### RE-RIPTOPL-145 — WEAKENED
The code divergence is exactly as claimed: `parse_hex8` (`freezediag.c:24-42`) accepts `a-f`; `mbHex8` (`igrmbox.c:151-168`) does not, and `_start` (`igrmbox.c:174`) refuses to load on a 0 parse. But the claimed consequence — a lowercase address reaching igrmbox in practice and silently killing the in-game menu — is defeated by both in-tree producers, which the finding left as INFERRED/Med: ee_core's igrmbox producer formats with `hex[] = "0123456789ABCDEF"` (`ee_core/src/igrmbox.c:48,57-59`), and freezediag's producer does the same (`ee_core/src/freeze_diag.c:254,273-278`). No lowercase string can reach either parser from this tree. True scope: a latent inconsistency between two hand-copied parsers, worth aligning, with zero current reachability — not a live load failure.

### RE-RIPTOPL-147 — WEAKENED
Three defects narrow this finding substantially. (1) Citation failure: `modules/bdmevent/main.c:443-483` does not exist — the file is 88 lines long; the snapshot function `bdm_diag_fill_snapshot` is at `:12-56`. (The BDM-side citations `modules/bdm/src/bdm.c:11-17,154-180` do resolve.) (2) The fix's premise is wrong: BDM's `refused` flag lives in `struct bdm_mounts`, which is *private to bdm.c* (`bdm.c:11-20`); the already-exported `bdm_get_bd` (`bdm.c:143-151`, `exports.tab`) returns only `block_device*` pointers and exposes no refused state. The fix therefore requires a *new* export, not reuse of an existing one. (3) The policies are not identical: BDM refuses any device with `sectorSize != 512` including 0 (`bdm.c:169-180`), while bdmevent deliberately counts only `parNr == 0 && sectorSize != 0 && sectorSize != 512` (`bdmevent/main.c:38-45`, with a long comment explaining that counting 0 produced false error-500 reports for empty card readers). The char-by-char `'u','s','b','\0'` USB-root test and the 20-slot scan are as claimed. Real scope: a partially-overlapping re-derivation with an intentionally different diagnostic rule, plus a wrong location citation.

### RE-RIPTOPL-150 — VERIFIED-DRIFT
Both shims verified (~20 lines each, `CpuSuspendIntr`/`AllocSysMemory(ALLOC_FIRST,…)`/`FreeSysMemory` pattern). Drift not mentioned in the finding: the signatures already differ — `void *malloc(int NumBytes)` in sbp2_driver.c vs `void *malloc(unsigned int nBytes)` in iLink_internal.c (plus local name `buffer` vs `result`). Also note the "shared source" fix saves source lines only; each IRX still compiles its own copy, so there is no IOP-RAM saving. Mechanism holds.

### RE-RIPTOPL-153 — VERIFIED-DRIFT
Three separate `release_notes` implementations of the same `git log --no-merges -10` block confirmed at the cited lines, and the import precondition for sharing is real (`publish_llvm.py:17-18` imports `publish_editions as common` and `publish_lite`; `publish_editions.py:19` imports `publish_lite`). Drift: the claim of "three different escaping rules" overstates the divergence — all three escape exactly the same character set (backslash, backtick, `*`, `_`, `[`, `]`, `<`, `>`): editions via regex class at `:331`, lite via per-character join over `"\\`*_[]<>"` at `:161`, llvm via `"\\*_[]<>" + chr(96)` at `:187`. What actually drifted is implementation style and hash form: editions/lite use `%H` and slice `commit[:8]` for display; llvm uses `%h` at `:184` and links the short hash directly (still a valid commit URL). One helper remains a sane fix, but there are not three escaping *rules* to reconcile.

### RE-RIPTOPL-155 — VERIFIED-DRIFT
Two implementations over the same pin contract confirmed (shell: `tools/ps2build_sdk.sh` `mirror_fetch`/`pin_value`/`from_mirror`; Python: `tools/install_ports.py` `mirror_file`/`mirror_pin`/`mirror_archive`, the latter with the HTTPS/API asset-id path). Citation drift: shell `mirror_fetch` begins at `:109`, not `:123` (range 123-149 covers its tail plus `pin_value`); `from_mirror` begins at `:348`, not `:347`; Python `mirror_file` begins at `:83`, not `:89`. The finding's own caveats stand and were re-checked: effort L / risk Med, and the shell script is not wholly Python-free (it already shells to `python3` for JSON asset-id parsing at `:103,158,164` when `jq` is absent), which slightly weakens its stated "no Python dependency at its earliest stage" risk note in the *other* direction — the consolidation is more feasible than the risk line implies, but the in-memory `(url, data)` Python interface vs the shell's file-based pin flow still makes it a substantial merge.

### RE-RIPTOPL-159 — WEAKENED
Verified parts: `FULL_LWIP ?= 0` (`Makefile:8,31-32`); gated ranges sum to exactly the claimed ~627 lines (sockets.c cited ranges = 519, ps2ip.c `:97-152` = 56, netif.c `:110-161` = 52; total 627); `exports.tab` stubs the lwip_select/ioctl/getpeername/getsockname/getsockopt and ps2ip_get/setconfig slots unconditionally; the `lwip_select` timeout bug is real — `sys_sem_wait_timeout` is a macro returning 0/1 (`include/lwip/sys.h:81`), so `if (i == SYS_ARCH_TIMEOUT)` (`sockets.c:765`, −418) can never fire. Defeated part: the headline "cannot link if enabled" rests solely on dhcp, and that mechanism does not hold — the `dhcp_start`/`dhcp_stop` calls in `ps2ip_setconfig` are inside `#if LWIP_DHCP` (`ps2ip.c:140-148`), and `LWIP_DHCP` is 0 unless `PS2IP_DHCP` is defined (`include/lwipopts.h:269-280`), which nothing in the tree defines. In a plain `FULL_LWIP=1` build the dhcp references compile out; no other unresolved-symbol blocker was demonstrated (the gated `netif_find`/`netif_remove` are gated consistently with their gated callers). (There is also no `dhcp.c` under `modules/network/` — only `include/lwip/dhcp.h` — but absence alone does not create a link failure given the guard.) True scope: a default-off, export-stubbed, internally buggy subsystem that is dead in shipped builds; "cannot link" is unproven on the cited evidence. Whether a `FULL_LWIP=1` build actually links would require a PS2 IOP build, not attempted here.

### RE-RIPTOPL-161 — VERIFIED-DRIFT
Empty-object mechanism verified end-to-end: Makefile defines `-DAPA_OSD_VER -DAPA_USE_DEV9 -DAPA_USE_ATAD` and lists `hdd_blkio.o` (`modules/hdd/apa/Makefile:8,11`); every function in the file sits under `#ifdef APA_USE_BDM` / `#ifdef APA_USE_IOMANX` / `#ifndef APA_USE_ATAD`; the ATAD build's `blkIo*` are static inlines in `hdd_blkio.h:14-63`. Only range drift: the IOMANX/VHDD block is `:178-293`, not `:171-303`.

### RE-RIPTOPL-163 — VERIFIED-DRIFT
Allocator mechanism verified: `MEM_LIBC_MALLOC 1` at `include/lwipopts.h:23`; the heap allocator occupies the `#else` branch `mem.c:123-377`; `mem_reallocm` (`mem.c:379-390`) has zero callers in the tree (only its declaration at `include/lwip/mem.h:50`) and does `mips_memcpy(nmem, rmem, newsize)` — an over-read of the old block on the grow path, as claimed. Citation drift: the `mem_clib_* → ps2ip_mem_*` mapping is at `include/arch/sys_arch.h:12-20`; the file is only 22 lines long, so the cited `:29-37` does not exist.

### RE-RIPTOPL-164 — VERIFIED-DRIFT
Commented predecessor implementations exist beside all three live `FILE*` functions, plus the isofs.c remnants, and the ~90-line total is about right. But the cited ranges overstate each block by including live code: the `check_cfg` comment block ends at `iso2opl.c:112` (`} */`), not :128; the `write_cfg` block ends at `:179`, not :195; the `write_parts` block is roughly `:232-271` within the cited :226-273. In isofs.c the remnants sit at `:1051-1054`, `:1059-1060`, `:1080` and the debug dump after `:173`, i.e. at the edges of, rather than inside, some cited ranges. Deletion fix is sane and LESS-NOT-MORE compliant.

### RE-RIPTOPL-165 — VERIFIED-DRIFT
Dead-module mechanism verified: `loopif.c` is wholly under `#if LWIP_HAVE_LOOPIF` (`:34-113`), the macro defaults to 0 (`include/lwip/opt.h:336-337`) and is set nowhere (grep), `loopif.o` is absent from the Makefile `IOP_OBJS`, and file+header total 152 lines ≈ the claimed ~150. Citation incompleteness: the gated ps2ip glue is not only `ps2ip.c:279-294` (`AddLoopIF` definition) — the `LoopIF` storage declaration at `ps2ip.c:65-67` and the `AddLoopIF()` call in `_start` at `ps2ip.c:311-313` are gated by the same macro and must be deleted in the same pass; the fix as worded ("the gated AddLoopIF block") would leave them behind (harmless, since still gated, but incomplete).

## Duplicate check
No finding in 133–165 is a mechanism-level duplicate of another finding in the report. Closest adjacencies checked by body, not heading: 159 (FULL_LWIP gated bodies) vs the exports-stub finding filed as W05-002 / RE-RIPTOPL-199 (EE RPC surface) — different mechanism (gated implementations vs stubbed export slots), and 159 explicitly cross-references it; 165 (loopif, `LWIP_HAVE_LOOPIF`) uses a different gate, file and Makefile absence from 159's `FULL_LWIP`; 163 (mem.c allocator, `MEM_LIBC_MALLOC`) is a different file/gate from both; 133/156/150 are distinct duplication sites in distinct subsystems.

## Candidates
none — the one additional defect noticed while verifying 158/160 (`apaOpen` dereferencing a cache entry after `apaCacheFree` at `modules/hdd/apa/src/hdd_fio.c:510-512`) is already filed as RE-RIPTOPL-043, so it is not a new candidate.

### Range RE-RIPTOPL-166-198

## Reasoning for non-VERIFIED verdicts

### RE-RIPTOPL-166 — VERIFIED-DRIFT
Mechanism holds in full. `ASYNC` is never defined: its only "definition" is commented out and no Makefile/ps2.yaml entry defines it (tree grep). The ASYNC half of `usb_queue_cmd` therefore never compiles, and it genuinely bypasses the two-drive fix: `WaitSema(usb_mass_cmd_sema)`/`SignalSema` sit under `#ifndef ASYNC` (`usb_mass.c:468-470,506`), while the ASYNC branch drives the shared static `ucmd` via `sceUsbdBulkTransfer` callbacks — exactly the race `ORIGIN.md` ("one command at a time across both drives", change 1) documents as fixed. The synchronous helpers (`perform_bulk_transfer` :117, `usb_bulk_reset` :218, `usb_bulk_command` :378) are likewise `#ifndef ASYNC`-guarded, so the prescribed fix (delete ASYNC branch + callback, drop the guards) is coherent and LESS-compliant. Benefit ~45 lines is accurate (≈14 lines callback + ≈31 lines queue `#else` + guards).
Citation corrections: `// #define ASYNC` is at **:17**, not :16. `scsi_cmd_callback` is at **:440-453** (guard `#else` at :438), not :445-458. The ASYNC half of `usb_queue_cmd` is at **:508-538**, not :525-556. The TODO about unhandled async errors is at :449 — inside `scsi_cmd_callback`, not inside the `usb_queue_cmd` range the finding attaches it to.

### RE-RIPTOPL-167 — VERIFIED-DRIFT
Mechanism holds. `IP_FORWARD` is 0 in both `include/lwip/opt.h:221-222` and `include/lwipopts.h:230`, and nothing defines it to 1; `ip_forward` (`ip.c:139-199`, 61 lines) and its call site (:262-265) compile out. `LWIP_DEBUG` is defined nowhere in the tree (grep for `#define LWIP_DEBUG` returns only `LWIP_DEBUGF`/`LWIP_DEBUG_TIMERNAMES`), so `ip_lookup` (`ip.c:79-113`, 35 lines) never compiles. Combined ≈96 lines vs the claimed ~95 — accurate. Deleting both is LESS-compliant.
Citation correction: the finding sends the reader to `ip.h:57` for the unconditional `ip_lookup` declaration; the declaration is at **`include/lwip/ip.h:46`** (line 57 is `#define IP_PROTO_ICMP`). The fix note (declaration must go or be guarded with the function) remains correct.

### RE-RIPTOPL-168 — VERIFIED-DRIFT
Core holds. `hexdump` (`tools/pc/ziso.py:44-47`) and `load_sector_table` (:369-400) have no call sites anywhere (grep: definitions only). The Python-3 defect is real: iterating `bytes` yields `int`s, and `ord(int)` raises `TypeError`. The "In future we will support NC" comment is at :370 verbatim, and neither function is wired into `parse_args`/`compress_zso`. `tools/pc/Makefile` builds only `iso2opl`, `opl2iso`, `genvmc` — confirmed. Deleting both functions and keeping the tool is the right, LESS-compliant fix.
Corrections: (1) The claim "no repo file references `ziso.py` (grep-verified)" is **false** — `README.md:862` documents it and :872/:878 give example invocations. The tool is documented and standalone-usable; only the two functions are dead. This does not change the fix (the finding already keeps the tool), but the grep claim must not be repeated. (2) Line counts: the two functions total ≈40 lines including separating blanks (4 + 32 code lines), not 47; cited ranges :44-50 and :369-405 overshoot both function ends.

### RE-RIPTOPL-169 — VERIFIED-DRIFT
Mechanism holds. Tree-wide grep finds zero references to `lang_decompiler` (no Makefile, workflow, doc, or script). The file is exactly 60 lines, runs at import (no `__main__` guard, no arguments) and rewrites `lng_src/*.yml` from `lng/*.lng` as a side effect — the accident hazard is real. The live direction is confirmed at `tools/assets.py:103-118` (`lang()` invoking `tools/lang_compiler.py`). Deletion is LESS-compliant.
Citation correction (1-line shift only): `represent_str` in the decompiler starts at **:4** (body :4-14), cited as :5-15. The compiler copy is at `tools/lang_compiler.py:34-45`, inside the cited :30-40 window.

### RE-RIPTOPL-170 — VERIFIED-DRIFT
Mechanism holds. `ps2.yaml` smb2man target defines exactly the seven `USMB2_FEATURE_*` flags claimed, without OPENDIR, and builds with `-ffunction-sections` + `-Wl,--gc-sections` (`ps2.yaml:169-171`). `smb2man.c`'s call set is as claimed (grep: `usmb2_build_request/close/create/mkdir/pread/pwrite/rename/rmdir/sessionsetup/stat/treeconnect/unlink`; its directory path at `smb2man.c:1254` clamps against `SMB2MAN_DIRBUF`). No caller exists for `usmb2_open/opendir/readdir/size/init_context` (the only external hits are provenance notes; `ps2iop-compat.h:15` even comments "smb2man never calls usmb2_open()"). The hazards are real: `usmb2_readdir` at `usmb2.c:792` does `memcpy(&dh[16+4], &usmb2->buf[8], u32)` where `u32 = le32toh(*(uint32_t *)&usmb2->buf[4])` is reply-controlled, with no clamp against the 252-byte `OutputBufferLength` it requested (:780, `MAX_DIR_SIZE-4`) or the 272-byte directory handle `usmb2_create` allocates (:740); `usmb2_size` returns at :1031 `le64toh(*(uint32_t *)&buf[16])` — a 32-bit read passed through a 64-bit conversion (function defined at :1002, declared `int` in `usmb2.h:91`).
Correction: `usmb2_opendir`/`usmb2_readdir` are not "stripped at link time" — they sit inside `#ifdef USMB2_FEATURE_OPENDIR` (`usmb2.c:743-820`) and are excluded by the preprocessor. The gc-stripped set is `usmb2_open` (:717), `usmb2_size` (:1002), `usmb2_init_context` (:1034) and its internal callee `usmb2_negotiateprotocol` (:504). The conclusion is unchanged and arguably stronger: the readdir hole is exactly one `-D` away, as claimed.

### RE-RIPTOPL-175 — VERIFIED-DRIFT
Mechanism holds. In `smap_udpbd`, `CopyFromFIFO` (`src/xfer.c:80-93`) has no call site (module grep: definition only) and `tx_done_ev` (:12 declaration, :56 `CreateEventFlag`) is never waited on or signalled anywhere. The sibling claim checks out: `udpfs_smap/src/xfer.c` has only `CopyToFIFO` and no `tx_done_ev`. (The other sibling, `smap-ingame/xfer.c:81,204`, does use its own `CopyFromFIFO` — different module, not the one cited.) Deletion is LESS-compliant.
Figure correction: the removable block is ≈19 lines (14 for `CopyFromFIFO` + declaration/creation and its exclusive `event_info` setup), not "~30 dead lines".

### RE-RIPTOPL-179 — VERIFIED-DRIFT
Mechanism holds and is slightly stronger than claimed. `dummy_fs` (`imgdrv.c:13-16`) has no live reference — its name appears only in comments (:66, :70 beside `NULL` entries, and the commented-out ops block :72-82). The private typedefs `_iop_device_tm`/`_iop_device_ops_tm` (:39-58) truncate the ops table at `lseek` (:57) — and they are not merely duplicative, they are **entirely unused**: the file already declares its device with the real types, `iop_device_ops_t my_device_ops` (:62) and `iop_device_t my_device` (:85). The fix therefore reduces to pure deletion, which is even more LESS-compliant than the finding's "declare with the real types" wording.
Citation correction: the private struct definitions span **:39-58**, not :39-95 (the :59-95 span is the live ops table, device, and `_start`); the commented-out block is :72-82, cited as :73-81.

### RE-RIPTOPL-182 — VERIFIED-DRIFT
Mechanism holds. `modules/hdd/atad/Makefile` defines exactly `ATA_USE_DEV9=1`, `ATA_GAMESTAR_WORKAROUND=1`, `ATA_ENABLE_BDM=1` (in `IOP_CFLAGS`), and a tree-wide grep finds no definition of `ATA_USE_AIFDEV9`, `ATA_ALL_PIO_MODES`, `ATA_MWDMA_MODES`, `ATA_SCE_AUTH_HDD`, or `ATA_ENABLE_MAXUDMA` anywhere (no `#define`, no `-D`, no ps2.yaml entry). The dead axes are present as claimed: AIFDEV9 branches throughout (e.g. :33-35, :201-207 incl. the `aif_tune_drive` declaration, :1008-1010), the `ATA_ALL_PIO_MODES` table in `ata_pio_mode` (:1458-1478, live path is the single-value `#else` :1479-1482), `ata_multiword_dma_mode` under `ATA_MWDMA_MODES` (:1485+), `ATA_SCE_AUTH_HDD` at :1399, `ATA_ENABLE_MAXUDMA` at :1361. The file is vendored (pin in `ORIGIN.txt`) and the prescribed fix — document in `ORIGIN.txt`, don't strip blind — is the correct LESS/posture call.
Citation corrections: the include block cited as :29-31 is the DEV9 include at **:30-32** (AIFDEV9 include at :33-35); the range cited as :213-215 for AIFDEV9 is actually the `ata_pio_mode` declaration (:213) and the `ATA_MWDMA_MODES` declaration guard (:214-215).

### RE-RIPTOPL-185 — VERIFIED-DRIFT
Mechanism holds. `PORT_NR`'s only definition is inside `#if 0` with the "Only needed for mx4sio" comment (`sio2man_hook.c:26-29`), so every `#ifdef PORT_NR` block compiles out permanently: the transfer filter and its two wrappers (:58-85), the export-25 hook (:115-117) and relink (:119-121), and the unhook block (:275-281) — all cited ranges exact. Deletion is LESS-compliant.
Completeness correction: one further `PORT_NR`-guarded block is uncited — the `m_25_psio2_transfer` field inside `struct sio2man_hook_data` at **:44-46**. The fix's "every `PORT_NR`-guarded block" wording covers it, but the location list does not.

### RE-RIPTOPL-187 — VERIFIED-DRIFT
Mechanism holds. `third_party/ps2sdk-socket/UPSTREAM:4-6` says the fallback was "retired from build graphs… no fallback archive is linked"; no build file references `socket.c`/`fdman.h`; `tools/build_edition.py:253-257` copies only `LICENSE` and `UPSTREAM` from both trees into kits. `README.md`'s present-tense claim ("Its build compiles `socket`, `bind`, `recvfrom`, `sendto` and `setsockopt` into a separate archive") sits inside the cited :3-7 span and does contradict the `UPSTREAM`. `ps2sdk-ppctty/irx_imports.h` is exactly the cited 21 lines (:1-21), its `UPSTREAM:3-6` says "No bundled driver is built", and nothing includes it. Keeping `LICENSE`/`UPSTREAM` and deleting the three source files is LESS-compliant.
Figure correction: the three files total **11,487 bytes** (`socket.c` 10,018 + `fdman.h` 1,043 + `irx_imports.h` 426), not "~21 KB".

### RE-RIPTOPL-188 — VERIFIED-DRIFT
Mechanism holds. `tools/pc/xeRAbora.url:1-2` contains exactly the two lines that `tools/package_edition.py:184` writes inline (`[InternetShortcut]\nURL=https://github.com/hacan359/xerabora/releases/tag/v1.0.1\n`); the file is referenced nowhere (grep), while the other kit shortcuts are copied from `tools/pc/*.url` by the loop at :181-182 over the dict at :179-180 (4 same-name + the `udpfs-server.url` → `android-udpfs-server.url` rename = the "other five"). The bump-in-the-wrong-place hazard is real.
Fix correction (the prescribed fix as written is wrong): adding `"xeRAbora.url": "xeRAbora.url"` to the unconditional `shortcuts` dict would ship the file in **every** kit, whereas the inline write is gated on `if options["ra"]:` (:183) — and `package_release_app.py:332` requires `xeRAbora.url` specifically in RA packages. The correct LESS fix is to copy from the file **inside the existing RA gate**: `(kit / "xeRAbora.url")` written via `shutil.copyfile(ROOT / "tools/pc/xeRAbora.url", …)` under `if options["ra"]:`, deleting the inline `write_text`.

### RE-RIPTOPL-189 — VERIFIED-DRIFT
All claimed sites verified. `bdmlaunch.cpp:983` (`|= 0 /* TODO! fake ilinkman ? */`) and :986 (MX4SIO `|= 0;`) exact. The commented UNMOUNT_EXCEPTION stubs are exact at `bdmsupport.cpp:2156-2157` and `mmcesupport.cpp:1801-1802`. The MMCE shutdown line is exact at `mmcesupport.cpp:1819` (commented `fileXioDevctl("mass:", USBMASS_DEVCTL_STOP_ALL, …)` under the "required by some HDDs" comment at :1818), and the BDM counterpart is live and gated exactly as claimed (`bdmsupport.cpp:2179-2180`, `if (gDeinitTerminal)`). The `supportbase.cpp` TODO + commented UL size loop is at :650-665, inside the cited :650-668. Deletion plus an explicit STOP_ALL decision is LESS-compliant.
Citation correction: the commented-out autolaunch block in `mmcesupport.cpp` spans **:1735-1741**, not :1740-1747 (lines 1743-1746 after it are live code).

### RE-RIPTOPL-194 — WEAKENED
The guard half holds exactly: `HttpEstabConnection` guards on `HttpIsInitialized()` at `src/platform/httpclient.cpp:42-43`; `HttpSendGetRequest` (:61) has no such guard, and it is the only other `HttpIsInitialized` use in the file. Adding the same one-line guard is sane.
The `HttpStreamEnd` half is materially narrower and its fix is unsafe as worded:
1. The EE function at `src/platform/httpclient.cpp:157-161` is indeed an empty body whose comment states the symmetry rationale — but it has **zero EE callers**, not "a no-op every caller must still call". EE callers use `HttpStreamBegin`/`HttpStreamRead` (e.g. `src/sources/httpsupport.cpp:272,344`) and never call `End` (grep over `src/`: definition only).
2. The name is shared with a **live, non-empty IOP function** of the same name: `modules/network/common/httpstream.inc:105-112` clears the stream state (`live/socket/remaining/stageLen/stageOff`), called from `httpstream.inc:127`, `modules/network/httpclient/main.c:41`, and `modules/iopcore/cdvdman/device-http.c:53`. Both sides share the single declaration at `modules/network/common/httpclient.h:29` (the header the EE file includes). "Delete `HttpStreamEnd` and its call sites" applied without the EE/IOP distinction would delete the IOP teardown and its declaration.
True scope: delete only the EE stub definition in `src/platform/httpclient.cpp` (it has no call sites to remove, and the shared header declaration must stay for the IOP side), plus add the init guard to `HttpSendGetRequest`. That is a smaller, different fix than prescribed.

### RE-RIPTOPL-197 — VERIFIED-DRIFT
Every quantitative claim reproduced by script: `gfx/` = 142 files, 443,330 bytes; default embed = 128 PNGs (104 base in `PNG_ASSETS` + 24 generated glyphs), plus `ra_mark` only under `RIPTOPL_RA=1` and `apps_case` only for the Korium theme, leaving exactly the claimed 14 unembedded-in-default, of which the 12 named files are used by no flavour. All 12 sizes exact (device set 6,185 B; duplicate copies 23,607 B; total 29,792 B). All four md5s match exactly: logo4=logo3 `84eb33a7d0daa1321fd2a07185dc6508`, logo5=logo2 `ca7048eb7168928716e42ee648c05572`, logo6=logo1 `1651f8d83156fabefe580ab610a96636`, no_Rating=Rating_0 `dd241f101796a79a67cd957ea6f6a17d`. The texture table does what is claimed: `DEVICE_1…DEVICE_ALL` and `no_Device` map to `NULL` (`textures.cpp:260-266,242`), `LOGO4/5/6` share `&logo3_png/&logo2_png/&logo1_png` (:299-301) and `NO_RATING_PICTURE` shares `&Rating_0_png` (:243), with comments recording the sharing. Deleting the 12 files while keeping the names/mappings is LESS-compliant and does not affect disk-theme override (themes carry their own PNGs).
Citation corrections: the device-icon comment is at `textures.cpp:258-259` and the `no_Device` comment at :241, not ":269-277". In `tools/assets.py`, `PNG_ASSETS` starts at :46 (cited :49-60) and the flavour additions are at :163-165 (cited :166-171, which is the RA trust-anchor loop).

## Candidates (new defects noticed while verifying — not merged into any verdict)

1. **Same broken CRC7 macro test in the header (extends RE-RIPTOPL-180):** `modules/mx4sio_bd/src/spi_sdcard_crc7.h:10` also tests `SD_CRC7_MODE == CRC7_MODE_RAM_TABLE` (nonexistent macro), so the `crc7_generate_table` extern declaration is compiled out by the same accident as the definition at `spi_sdcard_crc7.c:7`. Finding 180 cites only the `.c`.
2. **Second copy of the broken `PACKET_TCODE` macro (extends RE-RIPTOPL-183):** identical line at `modules/bdmassault/ilink_bd_assault/src/iLinkman/src/include/iLink_internal.h:92`. Deleting only the `modules/ilink` copy leaves the landmine in the assault tree.
3. **Second dead copy of `DeinitIEEE1394` (extends RE-RIPTOPL-184):** definition at `modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:775`, declaration at its `sbp2_disk.h:180`; likewise uncalled.
4. **Fix-completeness for RE-RIPTOPL-171:** `.github/scripts/test_smb_read_closed.py:64-65` hard-exits if `USE_CUSTOM_RECV` is no longer defined ("update this check"). Deleting the macro as prescribed must update that host test in the same change, or CI goes red.
5. **Guard gap is wider than RE-RIPTOPL-194 states:** in `src/platform/httpclient.cpp`, `HttpStreamBegin` (:95) and `HttpStreamRead` (:128) also lack the `HttpIsInitialized()` guard that `HttpEstabConnection` has — the inconsistency is file-wide, not specific to `HttpSendGetRequest`.
6. **Cross-reference note for RE-RIPTOPL-172:** `pbuf_dechain` is additionally declared as an IOP import at `modules/network/common/smstcpip.h:39-40` (`DECLARE_IMPORT(41, pbuf_dechain)`), i.e. the phantom declaration exists on both sides of the EE/IOP boundary; finding 172 covers only the lwIP header copy.

### Range RE-RIPTOPL-199-231

## Reasoning for non-VERIFIED verdicts

### RE-RIPTOPL-200 — VERIFIED-DRIFT
Mechanism and citations hold exactly: the TODO at `hddsupport.cpp:59-62` is verbatim, scan is called from `HddSource::update` (`:1884`), launch dispatches at `:2614`, reset at `:3247`, all gated on `gEnableDVR` + `SPD_CAPS_DVR` (`:82-100`). Figure correction: the "~260 lines" counts only the 52-310 block (259 lines). The launch leg `hddLaunchDvrIso` is a further ~35 lines (`:2470-2504`), plus publish/cleanup helpers and five hook points, so the subsystem the fix would delete is ~295+ lines. The finding understates its own case; the Benefit line ("removes ~260 lines") should read ~295.

### RE-RIPTOPL-203 — VERIFIED-DRIFT
Five candidate locations exist, but not in the stated order for one set, and not all for every set. In `configRead` (`config.cpp:1435-1490`) the actual sequence is: (1) current filename, (2) wOPL filename, (3) legacy filename in the same directory — OPL sets only, (4) carry-over directory with current name, then legacy name — OPL sets only. The carry-over-with-current-name rung the finding lists third exists separately for non-OPL global sets (guarded `type() != CONFIG_OPL`, third position for those sets) and is never taken by OPL sets. Corrected statement: an OPL config set can be read from five locations in the order current → wOPL → legacy → carry-over-current → carry-over-legacy. The redundancy claim and fix stand.

### RE-RIPTOPL-205 — VERIFIED-DRIFT
Core claim holds: `LAUNCH_DIAG` is a literal `0` at `include/launchdiag.h:41`, not a build flag, and with it 0 every call compiles to an inline no-op (header `:73-89`). Two figure errors: (a) "3 use sites outside the header" — there are exactly two `#if LAUNCH_DIAG` sites, `elfldr/loader.c:151` and `src/platform/launchdiag.cpp:5`; (b) the consumer list (loader.c + launchdiag.cpp only) badly understates reach — the header is included by ~10 files with live call sites in `src/app/opl.cpp:1301-1432`, `src/platform/elfldr_noreset.cpp:140-244`, `src/sources/bdmlaunch.cpp:394-474`, `src/platform/sysneutrino.cpp:1393`, etc. The finding is weaker than reality, not wrong in direction.

### RE-RIPTOPL-206 — VERIFIED-DRIFT
No chunked decoding exists and bodies are read by `Content-Length` only (`httpclient.c:231-244`) — mechanism holds, and the file's own header comment (`:2`) admits it. But the finding presents the Transfer-Encoding handling at `:160` as live parser code: lines 160-165 sit inside a `/* … */` comment block, and the `TransferEncoding` variable itself is commented out (`:138` `// static char TransferEncoding;`). The only live residue is the unused `enum TRANFER_ENCODING` at `:130-133`. Corrected fix emphasis: delete the enum and the commented-out branch; the "fail explicitly on chunked" half is unaffected (a chunked response currently just yields `ContentLength = -1` handling, never a TE check).

### RE-RIPTOPL-213 — VERIFIED-DRIFT
Structure holds exactly: 496-line file, `resolveWithBudget:254-471`, literal-slot resolver `:118-250`, fallback slot held and consumed last (`:375-408,449-460`), two ladders at `:472-485`. Arithmetic error: the full ladder's own numbers sum to 3000+10000+3000+8000 = **24,000 ms (24 s)**, not the "22 seconds" stated twice. (Structurally the budgets are per-tier escalations inside the scan — `budgetMs` assigned at `:361`, `+= mx4sioExtraBudgetMs` at `:434`, `+= expensiveExtraBudgetMs` at `:446` — so 24 s is the ladder total as the finding frames it, not a guaranteed single-call wait.)

### RE-RIPTOPL-223 — VERIFIED-DRIFT
Four attempts per launch exist, but the finding mis-describes what varies. The startup ID is never re-cased; only the extension is, in **both** paths: the tar probe tries `<file>.cht` then `<file>.CHT` (`supportbase.cpp:1766-1772`, inside the cited 1755-1790 range), and the loose path loops over `chtExt[] = {"cht", "CHT"}` (`:1801-1807`). So the stack is tar×2 + loose×2, not "tar, loose exact, lowercase, UPPERCASE" as four distinct filename spellings. The fix ("delete the two case-variant retries") is actually correct as written once read as deleting both `.CHT` retries; the Problem text's attempt enumeration should be corrected.

### RE-RIPTOPL-225 — VERIFIED-DRIFT
The wrapper pattern is real at every cited site (`guiscreens.cpp:99-109`, `guinotify.cpp:205-218`, `guisplash.cpp:245-266`, `cfgtitle.cpp:269-274` — six one-liners, `artindex.cpp:26-44`). Count correction for the headline file: `gui.cpp:471-492` contains **22** forwarding wrappers (`guiInit`…`guiUpdateScreenScale`), not 25. No other claim in the finding depends on the count.

### RE-RIPTOPL-228 — WEAKENED
Save half verified: `cheatConfigSaveSelections` (`cheatconfig.cpp:99-104`) loops all `MAX_CODES` = 250 slots (`include/cheatman.h:40`) calling `configRemoveKey` per slot. Load half is real code — `cheatConfigLoadSelections` breaks at the first missing `$CheatSelNNN` (`:79-82`) — but the finding's causal claim fails: the save side does **not** produce a sparse set. After the removals it rewrites picks with its own contiguous counter (`:105-111`, `n++` per enabled pick, keys `%03d` from 000), so any file this code writes is gap-free and round-trips correctly. No other code in the tree removes individual `$CheatSel` keys (grep over `src/`+`include/` finds the prefix only in `cheatconfig.cpp` and `include/config.h:83`). True scope: the 250-walk save cost stands; the load drop-after-gap is a latent fragility reachable only via a hand-edited or foreign-written config file, not via the save/load round-trip the finding claims is broken.

### RE-RIPTOPL-230 — VERIFIED-DRIFT
Double traversal verified verbatim: `gatherAllGames` (`favsupport.cpp:1749-1755`) calls `gatherPass(NULL, 0, shelf)` to count, `calloc`s, then `gatherPass(rows, cap, shelf)` to fill. Count correction: `kAllGamesModes` (`:1584-1585`) lists **14** modes (HDD, BDM, BDM1-BDM7, MMCE, ETH, UDPFS, HTTP, APP), not 15. The doubling claim and fix are unaffected.

### RE-RIPTOPL-231 — VERIFIED-DRIFT
Wrapper claims verified exactly: `cdvdman_sendSCmd` (`cdvdman.c:982-998`) retries while `r == 0` with `DelayThread(2000)` and `++retryCount <= 2500` (~5 s), then falls through to unconditional `return 1`. Count correction: `cdvdman_writeSCmd` (`:926-979`) contains **four** unbounded, delay-free register polls, not three — the entry drain `do { (void)CDVDreg_SDATAOUT; } while (!(CDVDreg_SDATAIN & 0x40));` (`:939-943`), the cited `while (CDVDreg_SDATAIN & 0x80);` (`:954-956`), the output-read do-while (`:961-968`), and the flush do-while (`:971-975`) — all while holding `cdvdman_scmdsema` (taken `:931`, released `:976`). The finding understates the spin count.

## Candidates (new defects noticed while verifying — not merged into verdicts)

- **CAND-1 — `mmce_fs_ioctl2` has the same silent-success defect as RE-RIPTOPL-201, in the adjacent op slot.** `modules/mmce/mmceman/src/mmce_fs.c:1169-1183`: `res` initialises to 0, the `default:` case is empty, and every unknown `cmd` returns 0 (success) — only `MMCE_CMD_IOCTL_GET_FD` is handled. Registered as the `ioctl2` slot at `:1213`. RE-RIPTOPL-201 cites only the `ioctl` stub (`:402-405`, slot `:1194`), so this is a separate, uncovered instance of the same mechanism; the 201 fix should be extended to it or it should be filed separately.

### Range RE-RIPTOPL-232-265

## Reasoning for non-VERIFIED verdicts

### RE-RIPTOPL-232 — VERIFIED-DRIFT
Mechanism holds: `SCSI_MAX_RETRIES 16` at `scsi.c:14`; both loops retry the full
`scsi_cmd_rw_sector` call with no delay and no permanent-error early exit, returning
`-EIO` only after all 16 fail. Citations are off: the read loop is at `scsi.c:289-296`
and the write loop at `scsi.c:339-346`, not :310-330 / :356-375. The status function is
at `usb_mass.c:305-326`, not :317-340. One further narrowing: reset recovery is **not**
performed on every failed attempt — `usb_bulk_manage_status` (`usb_mass.c:320-323`)
calls `usb_bulk_reset(dev, 3)` only when the CSW read failed, the signature/tag
mismatch, or `csw.status == 2`. A plain check-condition failure (status 1) retries 16
times without a reset. The fix (reduce to ~3 attempts) remains sane and LESS-compliant.

### RE-RIPTOPL-237 — VERIFIED-DRIFT
The draw count is exact when the glow is visible: 8 inner taps (`guisplash.cpp:60-61`),
4 outer taps (`:63-64`), plus the logo itself (`:67`) = 13. But the taps are gated by
`if (tapAlpha > 0)` at `:57`, where `tapAlpha = alpha * strength * 0.16` — during the
fade-in/out ends of the splash (small `alpha`) `tapAlpha` truncates to 0 and only the
single logo draw happens. "13 full-logo draws every frame for the whole boot splash"
overstates that edge. Core judgement (deliberate effect, ~30% saving from dropping the
4 outer taps = 4/13) stands.

### RE-RIPTOPL-238 — WEAKENED
True part: in shipped builds `lwip_select` is compiled only under `FULL_LWIP`
(`sockets.c:671`, gate at `:623/:630`) and its export slot is `ps2ip_Stub`
(`exports.tab:17`), and `ps2.yaml:199-201` builds SMSTCPIP with no `FULL_LWIP`
define — so `select_cb_list` is always empty and the `while (1)` list walk at
`sockets.c:862-889` does one wasted traversal per event.
Defeating/narrowing evidence for the rest: `event_callback` is not select-only
machinery. The same semaphore protects the `sock->rcvevent++ / --` and
`sendevent` updates at `sockets.c:843-856`, and `rcvevent` is consumed by the
**ungated** `lwip_recvfrom` non-blocking check at `sockets.c:375`
(`(flags & MSG_DONTWAIT) && rcvevent <= 0` → `EWOULDBLOCK`) — a live path in
shipped builds. The semaphore is also created only once (`if (!selectsem)`), not
per event. So the prescribed fix as worded ("gate list walk/semaphore creation")
would break live recv bookkeeping if the semaphore half were applied. True scope:
gate/delete only the `select_cb_list` walk; keep semaphore creation and the
event-counter update.

### RE-RIPTOPL-239 — REFUTED
The central claim — 57 files "referenced by no workflow", "silently inert", "no
run-all entry point" — does not hold. `.github/workflows/ps2build.yml:71` (the
workflow that runs on every push/PR) runs `tools/run_tests.sh -k`, and
`tools/run_tests.sh:5-9` loops `for t in .github/scripts/test_*.py` — i.e. every
one of the 57 named files **is** executed in CI, transitively through the glob
runner. The finding's stated census method (concatenate workflow YAML, match
filenames + the two flavours glob patterns) simply missed this transitive glob —
a method its own sibling findings describe correctly (RE-RIPTOPL-248 acknowledges
the ps2build/rolling glob runner; RE-RIPTOPL-251 states ps2build runs the full
suite via `run_tests.sh`). Consequences that fall with it: `test_pccf.py` and
`test_ratls_host.py` are wired, so the `tests/pccf/*` and `tests/ratls/host_main.c`
suites are not "only" driven by unwired scripts; and the proposed fix (add a loop
job) would duplicate a loop that already runs.
What survives, narrowly: the counts themselves are exact (57 files, 12,240 lines
— recomputed), the RA/VMC cluster counts are right, and the `tests/smb2-live/`
sub-claim stands on its own: `tests/smb2-live/run.sh` is invoked by no workflow
(its exit-2 prerequisite gate is at `run.sh:53-55`, the no-server `readreply` case
at `run.sh:233`), so `browse.c` (258 lines), `keepalive.c` (407) and
`readreply.c` (163) have no CI coverage. That residual is a much smaller finding
than the one filed.

### RE-RIPTOPL-246 — VERIFIED-DRIFT
Every cited dead branch verified, and the dead side is absent at the pin
(`src/pad.c`, `src/opl.c`, `src/bdmsupport.c`, `src/guigame.c`, `src/vmcsaves.c`,
`lng_tmpl/_base.yml` do not exist; the `.cpp` alternatives do). But the finding
says "six suites" in its heading, body and fix while listing **seven**:
`test_pad_handoff.py`, `test_ps1_duplicates.py`, `test_screen_pos_per_mode.py`,
`test_ripps2_boot_video_mode.py`, `test_ripps2_vmc_saves.py`,
`test_udpbd_read_only.py`, `test_zso_validation.py`. Corrected count: 7.

### RE-RIPTOPL-251 — VERIFIED-DRIFT
Main finding verified in full (no `pip install` step anywhere in `ps2build.yml`;
four cited `import yaml` suites at the cited lines; three sibling workflows do
install `requirements.txt`). The merged W13-008 half also holds for the
`udpfs-host-tests` job (no install step; `test_udpfs_transport.py:8` and
`test_udpfs_shared_transport.py:3` import yaml; job header is at
`flavours.yml:320`, steps run to ~:343). Drift: the merged note calls
`test_ra_language_isolation.py` "currently dormant because the file is unwired
(W13-001)" — it is not unwired; it runs via `ps2build.yml` → `tools/run_tests.sh`
(see RE-RIPTOPL-239), where it has exactly the same uninstalled-PyYAML exposure,
so the exposure is broader than the merged note states, not dormant.

### RE-RIPTOPL-263 — VERIFIED-DRIFT
Substance verified: `rolling-release.yml:122-136` uses `tools/assets.py` +
`tools/build_edition.py` (no `make` build), `configurations.yml:23-42` runs
17 edition/configuration entries × 2 toolchains including `ra` and `diag`-family,
`docs/BUILD-CONFIGURATIONS.md` lists 15 configurations, and the stale comment at
`flavours.yml:401-405` is exact. Citation drift: the "flavours.yml builds the main
flavour only" sentence is at `docs/PS2BUILD-MIGRATION.md:101`, not within the
cited `:103-106`; `:104` (rolling-release) and `:106` (open items) are correct.
The finding's own fairness note about the `:3-7` banner is also accurate.

## Candidates (new defects noticed while verifying — not merged into verdicts)

1. **Same stale tar comment in the .cpp**: `src/platform/tar.cpp:9` repeats the
   "RiptOPL wires only TAR_KIND_ART" claim that RE-RIPTOPL-255 files against
   `include/tar.h:4-8`. The 255 fix should be applied to both places or the
   comment will still mislead from the implementation file.
2. **Scratch-dir exclusion list is duplicated**: the five agent scratch dirs
   filed in RE-RIPTOPL-261 at `tools/assets.py:203-204` also appear verbatim in
   the dead Makefile's anchor at `Makefile:111` (`CODE_ANCHOR`). It disappears
   if RE-RIPTOPL-253's Makefile deletion lands, but 261's fix as scoped only
   cleans the `assets.py` copy.
3. **`test_ra_language_isolation.py` PyYAML exposure is live, not dormant**:
   it imports yaml (`test_ra_language_isolation.py:9`), is not named in
   `flavours.yml`, and runs via `ps2build.yml` → `tools/run_tests.sh` with no
   PyYAML install — an additional instance of RE-RIPTOPL-251's main defect,
   beyond the four suites that finding lists.
