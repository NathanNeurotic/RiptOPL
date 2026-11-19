# RiptOPL extraction

## Verdicts
| ID | Title | Effort | Risk | Verdict |
|---|---|---|---|---|
| RE-RIPTOPL-001 | SMB1 reply length is server-controlled and overflows the static SMB buffer | S | Low | VERIFIED |
| RE-RIPTOPL-002 | SMB1 ReadAndX trusts the server's data length and padding; WriteAndX in the same file was already hardened | S | Low | VERIFIED |
| RE-RIPTOPL-003 | Negotiate Protocol copies a server-controlled key length into an 8-byte field | S | Low | VERIFIED |
| RE-RIPTOPL-004 | SMB1 transaction replies use server offsets as raw indices into the receive buffer | M | Low | VERIFIED |
| RE-RIPTOPL-005 | UDPRDMA receive ISR lets a peer claim an app-header up to 60 bytes into 32-byte (or smaller) buffers | S | Low | VERIFIED |
| RE-RIPTOPL-006 | `sysWriteNeutrinoCheats` accumulates `snprintf` returns into a length with no truncation check | S | Low | WEAKENED |
| RE-RIPTOPL-007 | APA journal restore trusts the on-disk entry count and a possibly-NULL cache entry | S | Low | VERIFIED |
| RE-RIPTOPL-008 | mcemu (HDD): an unmapped VMC page silently maps to sector 0 | S | Low | VERIFIED |
| RE-RIPTOPL-009 | SYSTEM.CNF parser can return an unterminated key and reads before its bounds check | S | Low | VERIFIED |
| RE-RIPTOPL-010 | GPT parser trusts header entry size and never checks first_lba ≤ last_lba | S | Low | VERIFIED |
| RE-RIPTOPL-011 | ZSO decompression uses the unbounded LZ4 "fast" decoder on untrusted image data | S | Low | VERIFIED |
| RE-RIPTOPL-012 | BT DS4 LED/rumble report: 50 bytes initialised, 79 bytes sent | S | Low | VERIFIED |
| RE-RIPTOPL-013 | ps2ips RPC handlers trust EE-supplied length fields (`optlen`, `malign`) | S | Low | VERIFIED |
| RE-RIPTOPL-014 | UDPFS core returns the server's claimed byte counts without checking them against what arrived | S | Low | VERIFIED |
| RE-RIPTOPL-015 | iLink SCSI layer silently wraps LBAs on drives over 2 TiB | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-016 | tar reader never validates the ustar header checksum, and size parsing ignores base-256 | S | Low | VERIFIED |
| RE-RIPTOPL-017 | TCP header length from the wire is trusted; `pbuf_header` result ignored | S | Low | VERIFIED |
| RE-RIPTOPL-018 | smap-ingame's low-level output copies a pbuf chain into a fixed frame buffer without a total-length check | S | Low | VERIFIED |
| RE-RIPTOPL-019 | MMCE `get_gameid` does an unbounded `strcpy` from the card reply into the caller's buffer | M | Med | VERIFIED |
| RE-RIPTOPL-020 | Hardcoded parental-lock master password in a public header | S | Low | VERIFIED |
| RE-RIPTOPL-021 | `HIOCGETSIZE` indexes the partition array with an unchecked EE-supplied index | S | Low | VERIFIED |
| RE-RIPTOPL-022 | `err_to_errno` macro never consults its table: every socket error collapses to EIO | S | Low | VERIFIED |
| RE-RIPTOPL-023 | Deferred dialog wait is unbounded while the sibling message wait is bounded | S | Low | VERIFIED |
| RE-RIPTOPL-024 | Self-update overwrites the running ELF in place and never verifies what landed | M | Med | VERIFIED |
| RE-RIPTOPL-025 | The default ship/POPS/POPSTARTER.ELF is byte-identical to the DEBUG variant, not the MAIN variant | S | Med | VERIFIED |
| RE-RIPTOPL-026 | `.bak` staging files can permanently block later writes and are never cleaned up | M | Med | VERIFIED |
| RE-RIPTOPL-027 | DEVCTL STOP_UNIT and STOP_ALL do the opposite of their names | S | Med | VERIFIED |
| RE-RIPTOPL-028 | APA partition helpers trust on-disk counts and indices | S | Low | WEAKENED |
| RE-RIPTOPL-029 | ELF program-header copies are unvalidated in two loaders, while a third loader in the same tree validates them | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-030 | `do_bind` dereferences unchecked `udp_new()`/`tcp_new()` results | S | Low | VERIFIED |
| RE-RIPTOPL-031 | Sequential API and `lwip_accept` dereference NULL conn/PCB states the socket layer itself documents | S | Low | VERIFIED |
| RE-RIPTOPL-032 | `pbuf_realloc` use-after-free chain through `ps2ip_mem_realloc` | S | Low | VERIFIED |
| RE-RIPTOPL-033 | `copy_from_pbuf` walks off the end of the pbuf chain | S | Low | VERIFIED |
| RE-RIPTOPL-034 | Socket entry points dereference caller sockaddr pointers unchecked; `lwip_sendto` ignores connect/disconnect errors | S | Low | VERIFIED |
| RE-RIPTOPL-035 | `lwip_recvfrom`: error conflated with EOF, and the SMB header index is trusted | S | Low | WEAKENED |
| RE-RIPTOPL-036 | NBNS resolver validates only the header size, then reads the answer at a fixed offset | S | Low | VERIFIED |
| RE-RIPTOPL-037 | smb_fio path handling can walk before the buffer and can leave stored names unterminated | S | Low | WEAKENED |
| RE-RIPTOPL-038 | Skip-Videos check reads before the path buffer for short filenames | S | Low | VERIFIED |
| RE-RIPTOPL-039 | SMB1 request assembly writes strings into the fixed packet buffer with no bound anywhere | S | Low | VERIFIED |
| RE-RIPTOPL-040 | ps2link file read trusts the host's byte count over the caller's buffer | S | Low | VERIFIED |
| RE-RIPTOPL-041 | fs_ioctl fabricates a 1024-byte argument length for fs_ioctl2 | S | Low | VERIFIED |
| RE-RIPTOPL-042 | mmce devctl dereferences `arg` unchecked in four cases — the fifth case in the same switch checks | S | Low | VERIFIED |
| RE-RIPTOPL-043 | APA cache entries are dereferenced after `apaCacheFree` | S | Low | VERIFIED |
| RE-RIPTOPL-044 | usbmass_bd SCSI: READ CAPACITY's last-LBA is stored as the sector count (off by one, both variants) | S | Low | VERIFIED |
| RE-RIPTOPL-045 | Partition notification dereferences a possibly-NULL `strchr` result | S | Low | VERIFIED |
| RE-RIPTOPL-046 | IOPRP patching runs `strcmp`/`strcpy` on fixed 10-byte ROMDIR names that need not be NUL-terminated | S | Low | VERIFIED |
| RE-RIPTOPL-047 | Network IP settings are parsed with unchecked `sscanf`, while the RA host IP in the same file is parsed properly | S | Low | VERIFIED |
| RE-RIPTOPL-048 | `lngGetFilePath` indexes `languages[langID - 1]` with no range check | S | Low | VERIFIED |
| RE-RIPTOPL-049 | `icmp_dest_unreach` uses an unchecked `pbuf_alloc` result | S | Low | VERIFIED |
| RE-RIPTOPL-050 | `ps2ip_input` interrupt path permanently loses queue slots on allocation failure | S | Low | VERIFIED |
| RE-RIPTOPL-051 | `common/smstcpip.h` import ordinals disagree with SMSTCPIP's export table | S | Low | VERIFIED |
| RE-RIPTOPL-052 | `recv_udp` leaks the pbuf when the connection has no receive mbox yet | S | Low | WEAKENED |
| RE-RIPTOPL-053 | Peer MSS of 0 is accepted and yields zero-length segments | S | Low | VERIFIED |
| RE-RIPTOPL-054 | httpclient's response reader copies without knowing the caller's buffer size | S | Low | VERIFIED |
| RE-RIPTOPL-055 | raudp discovery compares a sender address that may never have been filled in | S | Low | WEAKENED |
| RE-RIPTOPL-056 | rapull setup leaks earlier allocations when a later one fails | S | Low | REFUTED |
| RE-RIPTOPL-057 | BDM VMC exports dereference the block device with no NULL check | S | Low | VERIFIED |
| RE-RIPTOPL-058 | HDD DeviceInit retries the APA header read forever | S | Low | VERIFIED |
| RE-RIPTOPL-059 | HDPro transfer loop retries a failing command indefinitely | S | Low | VERIFIED |
| RE-RIPTOPL-060 | ps2link receive loop spins forever when the host closes the connection | S | Low | VERIFIED |
| RE-RIPTOPL-061 | ps2link exception handler dereferences a possibly-NULL module name and copies unbounded | S | Low | VERIFIED |
| RE-RIPTOPL-062 | udptty write path always reports 0 bytes written and discards the send result | S | Low | VERIFIED |
| RE-RIPTOPL-063 | GPT driver reports success when its scratch allocation fails | S | Low | VERIFIED |
| RE-RIPTOPL-064 | USBMASS_IOCTL_GET_LBA dereferences a possibly-NULL block device | S | Low | VERIFIED |
| RE-RIPTOPL-065 | Mount-info index guard is off by one (and admits negatives) | S | Low | VERIFIED |
| RE-RIPTOPL-066 | bdm_disconnect_fs never unmounts anything | S | Med | VERIFIED |
| RE-RIPTOPL-067 | MBR sanity check is tautological and all-or-nothing | S | Low | VERIFIED |
| RE-RIPTOPL-068 | diskio GET_SECTOR_COUNT truncates a 64-bit sector count to 32 bits | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-069 | MX4SIO: unknown CSD structure version is accepted with a zero/stale sector count | S | Low | VERIFIED |
| RE-RIPTOPL-070 | MX4SIO recovery re-reads card geometry in place while BDM/FatFs stay mounted | S | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-071 | pademu USB: `outEndp` is never initialised, so the OUT endpoint is never opened on first connect | S | Low | VERIFIED |
| RE-RIPTOPL-072 | Standalone ds34 copies index `power_level[]` with an unvalidated device byte | S | Low | VERIFIED |
| RE-RIPTOPL-073 | Standalone ds34usb SET_LED RPC: EE sends 2 bytes, IOP reads 4 | S | Low | VERIFIED |
| RE-RIPTOPL-074 | BT `hid_readReport` length validation is absent (standalone) or offset-blind (pademu) | S | Low | VERIFIED |
| RE-RIPTOPL-075 | `get_data` memcpy size is never clamped to the 18-byte pad data union | S | Low | VERIFIED |
| RE-RIPTOPL-076 | pademu USB `readReport` has no transfer length at all | S | Low | VERIFIED |
| RE-RIPTOPL-077 | `usb_config_set` passes a partly uninitialised `led[4]` to LEDRumble | S | Low | VERIFIED |
| RE-RIPTOPL-078 | One L2CAP buffer, three different transfer sizes | S | Low | VERIFIED |
| RE-RIPTOPL-079 | Standalone USB `set_bdaddr` transfers 24 bytes, writes 23 | S | Low | VERIFIED |
| RE-RIPTOPL-080 | Standalone USB driver shares one transfer buffer and one result code across both pads | M | Med | WEAKENED |
| RE-RIPTOPL-081 | Exported iLink transaction-context accessors index the context array unchecked | S | Low | VERIFIED |
| RE-RIPTOPL-082 | `GetBIOSVersion` parses an uninitialised buffer when the ROM open/read fails | S | Low | VERIFIED |
| RE-RIPTOPL-083 | `InitializeConfigurationROM` silently drops and leaks every registered extra CROM unit | S | Low | VERIFIED |
| RE-RIPTOPL-084 | mmce command functions transmit uninitialised trailing stack bytes | S | Low | VERIFIED |
| RE-RIPTOPL-085 | mmce_fs truncates every filename length into a `u8` | S | Low | VERIFIED |
| RE-RIPTOPL-086 | genvmc segfaults when run without arguments (usage printed, then argv[1] dereferenced) | S | Low | VERIFIED |
| RE-RIPTOPL-087 | ziso.py decompression loops forever on a corrupt or truncated ZSO block | S | Low | VERIFIED |
| RE-RIPTOPL-088 | Unbounded `sprintf` in PadEmu info formatting (safe today only by input clamping) | S | Low | VERIFIED |
| RE-RIPTOPL-089 | File Manager paste path uses aliased `strcpy` inside another call's argument | S | Low | WEAKENED |
| RE-RIPTOPL-090 | `sysDeleteFolder` uses a `malloc` result without a NULL check | S | Low | VERIFIED |
| RE-RIPTOPL-091 | `getFileSize` truncates file sizes to `int` | S | Low | VERIFIED |
| RE-RIPTOPL-092 | ratls parses Content-Length two ways, one of them `atoi`; its body-length global is non-static | S | Low | WEAKENED |
| RE-RIPTOPL-093 | OSDHistory's least-used-slot eviction keeps the evicted slot's old timestamp | S | Low | REFUTED |
| RE-RIPTOPL-094 | IGR font: 7 punctuation glyphs are copies of the '?' bitmap | S | Low | VERIFIED |
| RE-RIPTOPL-095 | Ten headers have no include guard at all | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-096 | The per-cheat code cap reuses the flattened-list cap, sizing the lazy cheat table at ~1.03 MB; the constants are duplicated in ee_core | S | Med | WEAKENED |
| RE-RIPTOPL-097 | BGM keeps a 768 KB static ring buffer resident even though BGM is off by default | S | Low | VERIFIED |
| RE-RIPTOPL-098 | HTTP catalog parsing pre-allocates for the maximum catalog and duplicate-checks in O(n²) | S | Low | VERIFIED |
| RE-RIPTOPL-099 | xhdd: a 407-line Windows IDENTIFY-device struct (and 512-byte static) to read one 16-bit field | S | Low | VERIFIED |
| RE-RIPTOPL-100 | usbd-ra (MINI_DRIVER): isochronous machinery is fully provisioned although no client in the tree can use it | M | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-101 | Both mbedTLS build definitions compile all 109 vendored library sources, including dozens that neither configuration can use | M | Med | VERIFIED |
| RE-RIPTOPL-102 | Whole-file staging for art re-grows its buffer by doubling, re-copying on every cover | M | Med | VERIFIED |
| RE-RIPTOPL-103 | utf8.h defines a 364-entry table and a function body in the header | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-104 | `USBA_crc32` keeps a 4× oversized table and rebuilds it on every call | S | Low | VERIFIED |
| RE-RIPTOPL-105 | Stack VLAs sized by caller-supplied lengths in keyboard paths | S | Low | VERIFIED |
| RE-RIPTOPL-106 | `pio_profile` permanently reserves 8 KiB of IOP RAM for a one-shot start-up check | S | Low | VERIFIED |
| RE-RIPTOPL-107 | ship/Example is a 5.9 MB snapshot of which the packager reads only 23 files; its full Neutrino tree is not what ships | M | Med | VERIFIED |
| RE-RIPTOPL-108 | The ds34 pad drivers are maintained twice and have drifted in both directions | L | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-109 | Three MD4s, two DESes and two LM/NTLM hash implementations for one SMB feature set | M | Med | VERIFIED |
| RE-RIPTOPL-110 | The native PS2 launch sequence is re-implemented once per backend | L | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-111 | The PS1 launch (POPSTARTER/VCD and Ember/Cue) is re-implemented once per backend | L | Med | VERIFIED |
| RE-RIPTOPL-112 | Two near-identical iLink driver trees in one repo, already diverged | L | Med | VERIFIED |
| RE-RIPTOPL-113 | `deinit` and `deinitEx` are the same ~90-line teardown written twice | S | Low | VERIFIED |
| RE-RIPTOPL-114 | udpfs editions still carry per-edition copies of headers that the shared core made unnecessary | M | Low | VERIFIED |
| RE-RIPTOPL-115 | Two ministacks, three SMAP drivers, and small helpers copied between the udpfs and udpbd stacks | L | Med | VERIFIED |
| RE-RIPTOPL-116 | Two full ISO9660 directory walkers (plus two copies of the TOC entry struct) in one scope | L | Med | VERIFIED |
| RE-RIPTOPL-117 | mmce_fs remove/mkdir/rmdir are three copies of one function | M | Low | VERIFIED |
| RE-RIPTOPL-118 | Three message/hint wrapping boxes re-implement the same wrap-and-draw | M | Med | WEAKENED |
| RE-RIPTOPL-119 | Three carousel presenters duplicate the framed-cover (overlay) draw block | M | Med | VERIFIED |
| RE-RIPTOPL-120 | Streaming ring-buffer copy logic duplicated almost line-for-line (EE-DMA and IOP-memcpy variants) | M | Med | VERIFIED |
| RE-RIPTOPL-121 | freeze_diag_screen.c re-implements freeze_diag_format.c's formatting helpers | M | Low | VERIFIED |
| RE-RIPTOPL-122 | Five near-identical number-to-string converters in igs_api.c | S | Low | VERIFIED |
| RE-RIPTOPL-123 | The ps2ips 48-byte-tail peek/truncate receive workaround exists in three copies | S | Low | WEAKENED |
| RE-RIPTOPL-124 | The ~66-line platform prologue is copied into four files (and the app splits); the disc-timing constants inside it are dead in sysvmc.cpp | S | Low | VERIFIED |
| RE-RIPTOPL-125 | The 100 µs/1 ms MSG_DONTWAIT poll with a 10 s silence budget is implemented four times | M | Med | WEAKENED |
| RE-RIPTOPL-126 | xatad duplicates atad's DMA-mode tables, and the copies have already diverged in reach | M | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-127 | crc32 and the integer typedef block are copied verbatim across the PC tools | M | Low | VERIFIED |
| RE-RIPTOPL-128 | The "NIC is reserved by a UDP transport" predicate exists in three copies | S | Low | WEAKENED |
| RE-RIPTOPL-129 | The read-only-device CFG-folder helper is duplicated in BDM and ETH — and the copies already disagree | S | Low | VERIFIED |
| RE-RIPTOPL-130 | ~37-line include block copy-pasted verbatim into split-out GUI files, most of it unused | S | Low | VERIFIED |
| RE-RIPTOPL-131 | Two near-identical icon+text draw functions in uidraw.cpp | S | Low | VERIFIED |
| RE-RIPTOPL-132 | Drive enumeration duplicated between Drive Status and the File Manager | M | Med | VERIFIED |
| RE-RIPTOPL-133 | FNV-1a implemented twice, in adjacent art files | S | Low | WEAKENED |
| RE-RIPTOPL-134 | Two different parsers for the same SYSTEM.CNF / BOOT2 job | M | Med | VERIFIED |
| RE-RIPTOPL-135 | Four tar finder functions duplicate one search | S | Low | VERIFIED |
| RE-RIPTOPL-136 | `readLeftJoy` writes the same deadzone switch twice | S | Low | VERIFIED |
| RE-RIPTOPL-137 | `sysCheckVMCEx` validates the VMC superblock geometry twice, with different rules | S | Low | WEAKENED |
| RE-RIPTOPL-138 | lwnbdctl writes the same bounded pad-start retry block twice | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-139 | GSM DTV mode constants defined in three places, in three styles | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-140 | COMPAT_MODE_* bits defined at four sites; one redefinition is live today | S | Low | WEAKENED |
| RE-RIPTOPL-141 | GSMSourceGSRegs layout hand-mirrored in three representations | M | Med | VERIFIED |
| RE-RIPTOPL-142 | Two macro families for the same GS registers in igs_api.h, plus a third copy in igrmenu.c | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-143 | opl.h includes <debug.h> twice and mixes include-path styles | S | Low | VERIFIED |
| RE-RIPTOPL-144 | smb2man and nfsman duplicate the same front-end plumbing | M | Low | VERIFIED |
| RE-RIPTOPL-145 | Two copies of the 8-hex-digit argument parser, and they disagree on lowercase | S | Low | WEAKENED |
| RE-RIPTOPL-146 | bd_defrag read and write are the same function written twice | S | Low | VERIFIED |
| RE-RIPTOPL-147 | bdmevent re-derives "unsupported sector size" by hand instead of using BDM's refused state | M | Low | WEAKENED |
| RE-RIPTOPL-148 | mcemu carries an orphan, near-duplicate `ioman_add.h` that nothing in mcemu includes | S | Low | VERIFIED |
| RE-RIPTOPL-149 | genvmc: the same `mc_writecluster` error translation is hand-copied at eight sites | S | Low | VERIFIED |
| RE-RIPTOPL-150 | Two hand-rolled malloc/free shims, one per ilink module | M | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-151 | `scsi_read` and `scsi_write` are copy-paste twins | S | Low | VERIFIED |
| RE-RIPTOPL-152 | Two language packers exist: tools/lng_pack.sh is superseded by package_edition.language_pack but still shipped and documented | S | Low | VERIFIED |
| RE-RIPTOPL-153 | The "last 10 commits" release-notes block is implemented three times, with three different escaping rules | M | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-154 | build_edition.py redefines the add() helper it could import from build_configuration.py | S | Low | VERIFIED |
| RE-RIPTOPL-155 | The ps2build-mirror fallback is implemented twice: once in shell, once in Python, over the same pin contract | L | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-156 | `struct hostent` is defined twice because `<netdb.h>` only declares it | S | Low | VERIFIED |
| RE-RIPTOPL-157 | atad PIO transfer returns success without transferring, and its body is unreachable dead code | S | Low | VERIFIED |
| RE-RIPTOPL-158 | The whole APA partition-editing subtree is retained dead code (~700 lines) | M | Med | VERIFIED |
| RE-RIPTOPL-159 | The FULL_LWIP subsystem (~627 gated lines) is dead and cannot link if enabled | M | Low | WEAKENED |
| RE-RIPTOPL-160 | APA password DES (~460 lines) runs in a build where password comparison is compiled out | S | Med | VERIFIED |
| RE-RIPTOPL-161 | `hdd_blkio.c` compiles to an empty object in this build (415 lines of other builds' code) | S | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-162 | audio/bgm.ogg (324,182 bytes) is an orphan: not embedded, not packaged, and not the BGM file the packages actually ship | S | Low | VERIFIED |
| RE-RIPTOPL-163 | mem.c carries a full heap allocator that is dead in every build, plus a callerless `mem_reallocm` | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-164 | iso2opl.c keeps ~90 lines of superseded implementations commented out beside the live ones | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-165 | loopif is a complete dead module: file, header, and gated glue all ship but can never run | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-166 | usbmass_bd: the unselectable ASYNC implementation of `usb_queue_cmd` is carried alongside the live one | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-167 | Feature-off protocol code retained: `ip_forward` and debug-only `ip_lookup` | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-168 | ziso.py carries a Python-3-broken helper and a never-called "future" feature | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-169 | tools/lang_decompiler.py is dead: no caller anywhere, and it duplicates lang_compiler's YAML representer | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-170 | usmb2 entry points that this build never calls, one of them unsafe if ever enabled | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-171 | SMB1 ReadAndX carries a permanently-dead non-custom receive branch | S | Low | VERIFIED |
| RE-RIPTOPL-172 | Header declarations with no definition anywhere in the tree | S | Low | VERIFIED |
| RE-RIPTOPL-173 | `do_accept` is a registered no-op that never ACKs | S | Low | VERIFIED |
| RE-RIPTOPL-174 | The non-core-locking API path in tcpip.c is unreachable and silently drops calls | S | Low | VERIFIED |
| RE-RIPTOPL-175 | Dead code in smap_udpbd's transfer layer: an uncalled copy routine and a never-used event flag | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-176 | raudp leftovers: a write-only diagnostic counter and heartbeat fields kept for a removed feature | S | Low | VERIFIED |
| RE-RIPTOPL-177 | BDM: `g_bd_sectors_per_sector` is written but never read; the sector factor is hardcoded instead | S | Low | VERIFIED |
| RE-RIPTOPL-178 | smb.h declares a function that does not exist and ~150 constants the in-game clients never use | S | Low | VERIFIED |
| RE-RIPTOPL-179 | imgdrv keeps dead code and hand-rolled duplicates of the ioman types it already includes | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-180 | MX4SIO CRC16 support is compiled in but permanently switched off; CRC7 mode selection is broken-but-lucky | S | Low | VERIFIED |
| RE-RIPTOPL-181 | Dead field: fatfs mount-info `mount_status` is declared and never used | S | Low | VERIFIED |
| RE-RIPTOPL-182 | ps2atad carries whole subsystems behind macros its Makefile never defines | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-183 | Broken, unused `PACKET_TCODE` macro | S | Low | VERIFIED |
| RE-RIPTOPL-184 | `DeinitIEEE1394` is dead and incomplete | S | Low | VERIFIED |
| RE-RIPTOPL-185 | sio2man_hook carries a permanently disabled mx4sio port-blocking feature | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-186 | pccf ships a formatter that is never included and a diagnostic entry point that is never defined | S | Low | VERIFIED |
| RE-RIPTOPL-187 | Retired ps2sdk fallback trees keep dead source files, and ps2sdk-socket's README describes the retired build as current | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-188 | tools/pc/xeRAbora.url is dead: the packager hardcodes its contents instead of copying the file | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-189 | Dead and no-op code: placeholder flag ORs, commented-out launch/teardown blocks, a commented-out size calculation | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-190 | Retired main-menu slot kept in the menu ID enum | S | Low | VERIFIED |
| RE-RIPTOPL-191 | `vmc_groups.cpp` ships dead data: malformed title IDs that can never match, and duplicated IDs | S | Low | VERIFIED |
| RE-RIPTOPL-192 | `PadManager::initialize` returns 1 on every path, making `startOne`'s failure branch dead; `keyPressed` lacks the bounds guard its siblings have | S | Low | VERIFIED |
| RE-RIPTOPL-193 | `sfxGetDropDiag` is a stub that reports zeros for counters that no longer exist | S | Low | VERIFIED |
| RE-RIPTOPL-194 | httpclient: an empty `HttpStreamEnd` kept "for symmetry", and `HttpSendGetRequest` skips the init guard | S | Low | WEAKENED |
| RE-RIPTOPL-195 | ioman.h documents a queue-removal function that does not exist | S | Low | VERIFIED |
| RE-RIPTOPL-196 | Remove_Padhook is declared but never defined and never called | S | Low | VERIFIED |
| RE-RIPTOPL-197 | Twelve gfx/ files (29,792 bytes) are never embedded in any flavour: seven device icons the code deliberately dropped, and four byte-identical duplicates whose blobs the code already shares | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-198 | ARP entries never expire: `etharp_tmr` is never scheduled because `LWIP_ARP` is defined nowhere | S | Low | VERIFIED |
| RE-RIPTOPL-199 | EE RPC socket surface is wired end-to-end to an empty stub | M | Med | VERIFIED |
| RE-RIPTOPL-200 | DESR/DVR support is a half-finished subsystem whose own header says it cannot claim to work | M | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-201 | `mmce_fs_ioctl` is registered but returns success for every command | S | Low | VERIFIED |
| RE-RIPTOPL-202 | SOCK_RAW is accepted, then silently hollow | S | Low | VERIFIED |
| RE-RIPTOPL-203 | `configRead` walks a five-rung filename fallback chain of accumulated migrations | M | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-204 | Favourites permanently carry a foreign-format import parser for other loaders' `favourites.bin` | M | Med | VERIFIED |
| RE-RIPTOPL-205 | Launch diagnostics are switched off by a hardcoded header constant, not a build option | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-206 | httpclient carries the scaffolding of chunked transfer encoding that was never implemented | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-207 | xhdd: HDPro transfer-mode stub reports success for doing nothing; two header prototypes have no definition | S | Low | VERIFIED |
| RE-RIPTOPL-208 | usbd-ra public header declares two exports that do not exist | S | Low | VERIFIED |
| RE-RIPTOPL-209 | The mmce "set alarms" setting is honoured by only two of the five transfer paths | S | Low | VERIFIED |
| RE-RIPTOPL-210 | PSUManager integration is a claim without a runner: the C++ validator and the vendored tests are never built or run | M | Low | VERIFIED |
| RE-RIPTOPL-211 | `ps2ip_ShutDown` is exported but does nothing | S | Low | VERIFIED |
| RE-RIPTOPL-212 | Neutrino / custom-loader path resolution is a ~630-line probing subsystem for "find one ELF" | L | Med | VERIFIED |
| RE-RIPTOPL-213 | Boot-directory resolution is a 496-line file with two budget ladders and three transport tiers | L | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-214 | A ~360-line asynchronous state machine exists to show a short disc ID in a caption | M | Med | VERIFIED |
| RE-RIPTOPL-215 | MMCE GameID card-switch waiting is six overlapping wait/settle mechanisms | L | Med | VERIFIED |
| RE-RIPTOPL-216 | Multi-view theme fallback *synthesizes* whole theme views by copying config blocks | M | Med | VERIFIED |
| RE-RIPTOPL-217 | Neutrino TOML sync carries two rewriters plus machinery that exists only to repair its own writes | L | Med | VERIFIED |
| RE-RIPTOPL-218 | Settings-home discovery is a second, larger fallback labyrinth — and save-side discovery disagrees with load-side | L | High | VERIFIED |
| RE-RIPTOPL-219 | `configWrite` stacks three separate save-safety mechanisms | M | Med | VERIFIED |
| RE-RIPTOPL-220 | host.cpp builds a four-interface service layer with exactly one implementation of each interface | M | Med | VERIFIED |
| RE-RIPTOPL-221 | Xerabora mode carries a hand-rolled JSON parser and maximum-size buffers for a paged list | M | Med | VERIFIED |
| RE-RIPTOPL-222 | UDPFS art lookup maintains an 8-spelling case-discovery subsystem for one folder name | M | Low | VERIFIED |
| RE-RIPTOPL-223 | Cheat loading stacks four fallback attempts per launch | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-224 | libview: a two-ring, pending/commit state machine to remember which list a page shows | L | Med | VERIFIED |
| RE-RIPTOPL-225 | One-line C forwarding wrappers layered over every C++ class split | L | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-226 | `pcdiag_cf_probe`'s sampling loop produces only traces the production caller throws away | M | Low | VERIFIED |
| RE-RIPTOPL-227 | Grid / PSBBN / Coverflow presenters each re-walk the whole game list per frame and per keypress | M | Med | VERIFIED |
| RE-RIPTOPL-228 | Cheat-config save walks 250 key removals; load silently drops every pick after the first gap | S | Low | WEAKENED |
| RE-RIPTOPL-229 | `fmChildren` re-lists from scratch on every ×4 growth step | M | Low | VERIFIED |
| RE-RIPTOPL-230 | All Games gathering walks every source's full list twice per rebuild | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-231 | cdvdman S-command path busy-spins on hardware registers and retries up to 2500 times | M | Med | VERIFIED-DRIFT |
| RE-RIPTOPL-232 | usbmass_bd SCSI: a failing sector is retried 16 times back-to-back, each retry through full USB reset recovery | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-233 | Block cache keys blocks by unaligned request sector, defeating its own geometry | S | Med | VERIFIED |
| RE-RIPTOPL-234 | smap_udpbd's transmit holds its semaphore across the whole retry sleep loop | S | Med | VERIFIED |
| RE-RIPTOPL-235 | `sysShutdownDev9` retries in a delay-free spin | S | Low | VERIFIED |
| RE-RIPTOPL-236 | Debug art HUD builds a ~35-field formatted string every frame | S | Low | VERIFIED |
| RE-RIPTOPL-237 | Boot-splash glow redraws the logo 13 times per frame | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-238 | Select event machinery runs on every netconn event in builds that have no select | S | Low | WEAKENED |
| RE-RIPTOPL-239 | 57 second-half test files (12,240 lines) are referenced by no workflow, and three `tests/` suites run only through them / through a runner no workflow invokes | M | Med | REFUTED |
| RE-RIPTOPL-240 | 18 first-half suites assert on source text only and never execute any code; several pin exact whitespace, so they fail on behaviour-preserving edits and pass on behaviour-breaking ones that keep the strings | L | Med | VERIFIED |
| RE-RIPTOPL-241 | Deliberately weakened assertions in the wired host tests: one literally vacuous `CHECK`, one either-way `CHECK`, and a known data defect that is counted but never asserted | S | Low | VERIFIED |
| RE-RIPTOPL-242 | Skip-to-pass: three places convert "cannot run" into a green result | S | Low | VERIFIED |
| RE-RIPTOPL-243 | The extract-a-C-function harness boilerplate is copy-pasted into 52 suites instead of living in one place | L | Med | VERIFIED |
| RE-RIPTOPL-244 | hostmap.py monkeypatches `pathlib.Path.read_text` globally so 11 suites can keep testing a source dialect that no longer exists | M | Med | VERIFIED |
| RE-RIPTOPL-245 | test_ee_hook_context.py verifies EE assembly with a hand-written Python MIPS interpreter instead of executing anything | M | Med | VERIFIED |
| RE-RIPTOPL-246 | Dead pre-C++-conversion layout branches: six suites still select between the old `src/*.c` tree and the current `src/**/*.cpp` tree at runtime | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-247 | `test_config.cpp` pins a lossy libconfig round-trip as correct behaviour, labelled only "characterisation" | S | Low | VERIFIED |
| RE-RIPTOPL-248 | flavours.yml hand-enumerates test files in ~30 steps, duplicating the glob runner; the two lists already disagree | M | Med | VERIFIED |
| RE-RIPTOPL-249 | docs-sync.yml embeds a ~150-line Python program inside YAML, with the same content written twice (Markdown and HTML literals), and no test covers it | M | Med | VERIFIED |
| RE-RIPTOPL-250 | Docs dependency pins exist in three places; two workflows inline them instead of using requirements-docs.txt | S | Low | VERIFIED |
| RE-RIPTOPL-251 | The main CI workflow runs the full host-test suite with no Python dependencies installed; four suites import PyYAML and only pass if the runner image happens to preinstall it | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-252 | The format gate never checks a single C++ file, and the local and CI format mechanisms disagree about what is excluded | S | Low | VERIFIED |
| RE-RIPTOPL-253 | The top-level Makefile is a dead second build system whose duplicated lists have already drifted from the live one, yet tests and a packaging script still read it as a data source | L | Med | VERIFIED |
| RE-RIPTOPL-254 | BDMA driver identity is a hand-maintained table of exact byte sizes that churns every driver rebuild | M | Low | VERIFIED |
| RE-RIPTOPL-255 | tar.h's header comment claims CFG/CHT kinds are unwired; the code wires them | S | Low | VERIFIED |
| RE-RIPTOPL-256 | pad.h mixes constexpr and #define for one sequential key enum | S | Low | VERIFIED |
| RE-RIPTOPL-257 | Byte-identical 48,500-byte IRX blob stored twice in bdmassault | S | Low | VERIFIED |
| RE-RIPTOPL-258 | atad ORIGIN.txt states a change the code does not contain | S | Low | VERIFIED |
| RE-RIPTOPL-259 | freepad "vendoring" exists only as a provenance note; the mechanism it describes does not exist | S | Low | VERIFIED |
| RE-RIPTOPL-260 | Translation downloads are unpinned, contradicting the "pinned" claim in tools/assets.py | S | Low | VERIFIED |
| RE-RIPTOPL-261 | tools/assets.py bakes transient agent scratch directories into the release version anchor | S | Low | VERIFIED |
| RE-RIPTOPL-262 | Current user/contributor docs still teach the legacy Makefile build — commands, flags and variant names that no longer exist | M | Low | VERIFIED |
| RE-RIPTOPL-263 | The migration doc's status section and a workflow comment describe flavours/CI that have since landed, as if they were still missing | S | Low | VERIFIED-DRIFT |
| RE-RIPTOPL-264 | The language count is 31 in the README badge and the languages doc; the build builds 33 | S | Low | VERIFIED |
| RE-RIPTOPL-265 | Current reference docs cite source files at their pre-C++ flat paths, which do not exist | S | Low | VERIFIED |

## Narrowed scopes
- RE-RIPTOPL-006: Latent unsafe `snprintf`-accumulation idiom only: the buffer is sized `512 + pairs*32` and each pair emits a fixed 28-char line with caller-bounded header inputs, so no reachable input truncates or makes `size - len` negative — no live heap overwrite as filed.
- RE-RIPTOPL-028: Only the `hddGetFileBlockInfo` half holds — `subs[stat.private_4]` is indexed unchecked at hdd.cpp:438-446; the negative-`nsub` half does not hold as stated, because `nsub` is an unsigned on-disk field and a value ≥2^31 makes the copy loop never execute and returns a bogus negative count rather than an out-of-bounds walk.
- RE-RIPTOPL-035: Only the error/EOF conflation holds (`lwip_recvfrom` returns 0 with errno 0 on `netconn_recv` failure); the header-index half is latent only — the sole in-tree caller passes the constant index 49, inside the 63 bytes copied into the 1024-byte `SMB_buf`, and downstream code already guards `index > avail_len`.
- RE-RIPTOPL-037: Only part (b) holds — `smb_dopen`/`smb_open` can leave stored names unterminated; part (a) is refuted, because `prepare_path`'s backward-walk condition is false on the NUL terminator for every input, so `p2` never decrements — the loop is dead, not a walk before the buffer.
- RE-RIPTOPL-052: The missing `else pbuf_free(p)` in `recv_udp` is real, but the claimed arrival window is not established: every entry point creates the recvmbox before the PCB can match a packet, and before bind/connect the PCB's local port is 0, so no concrete in-tree sequence reaches the NULL-mbox state with a deliverable datagram.
- RE-RIPTOPL-055: The uninitialised/stale `from` read in `ra_discover` is real, but it feeds only a skip-only `continue` gate; peer acceptance additionally requires `got >= 8`, the `"RAO1 OK "` prefix, a non-broadcast IP and ARP resolution, so a garbage `from` can at most spuriously skip one poll — it cannot steer the trust/pinning decision.
- RE-RIPTOPL-080: The shared `usb_buf`/`usb_resulCode` are real, but two-pad RPC interleaving cannot occur as framed because all pad operations are serialised through one `rpc_sf`/`SifRpcLoop` thread; the true hazard is narrower — the driver's own async USB configuration callback and a pending interrupt transfer completing into the shared buffer/result.
- RE-RIPTOPL-089: Both cited lines exist, but there is no present correctness or security defect: at guifilemgr.cpp:446 the copy's result is the same under any argument evaluation order and source/destination are distinct arrays, and at :471 the `fmClip` slots are distinct and bounded-filled — only a readability/fragility smell remains.
- RE-RIPTOPL-092: Only the Content-Length parsing half holds (dual parsing is real, with a third `atoi` site at ratls.c:157); the `make raTlsBodyLength static` half is refuted — it is a deliberate `extern` in include/ratls.h read by src/ui/selfupdate.cpp to size the download buffer, so making it static would break the link.
- RE-RIPTOPL-096: Reuse, ~1.03 MB size and ee_core duplication hold, but the stated basis for a smaller per-cheat cap is circular: the parser's own per-cheat bound is the same `MAX_CHEATLIST` constant, so by the finding's derivation rule the cap is 510 — a smaller cap needs the flattened-list justification and a parser-bound change in the same edit.
- RE-RIPTOPL-118: Only two copies match the claimed measure/clamp/draw-rect shape (`diaDrawHint` in dia.cpp and `Notifications::render` in guinotify.cpp); the modal copy measures nothing, clamps no box and draws no rect, sharing only the `fntWrapText` + `lines * MENU_ITEM_HEIGHT` pattern — a full duplicate pair plus a partial third, with the ~60-line saving overstated.
- RE-RIPTOPL-123: The three peek/truncate copies are exact, but the prescribed fix breaks non-RA builds: `ranet.cpp` is compiled only in RA editions while `ratls_ps2.cpp` also builds in non-RA builds and `nettime.cpp` builds in every edition — consolidation needs a shared home in an always-compiled source, not a call into ranet.
- RE-RIPTOPL-125: The graduated 100 µs/1 ms poll with the `#399` comment exists in only 3 of the 4 claimed transports (SMB, SMB2, NFS); the HTTP `RecvBounded` copy polls at a flat 1 ms with no `#399` comment and shares only the poll + 10 s budget shape, so a helper extracted from the three would not drop into HTTP unchanged.
- RE-RIPTOPL-128: The three predicate copies are exact, but the prescribed fix breaks non-RA builds for the same reason as RE-RIPTOPL-123: ranet is RA-only while `nettime.cpp` builds in every edition and `ratls_ps2.cpp` in non-RA updater editions — consolidation requires moving the predicate to an always-compiled source.
- RE-RIPTOPL-133: The FNV-1a core is duplicated, but the artworker version is not the plain special case of the artindex one: `hashName` case-folds `a-z` before hashing and `focusHashValue` does not, so folding them changes focus-change semantics — true scope is one shared FNV core with a case-fold flag.
- RE-RIPTOPL-137: The two geometry blocks are mutually exclusive per call (strict block for size-only callers, lenient block when a superblock is present), not two validation passes over one call; keeping only the `== 512` rule as prescribed would delete the only validation superblock callers get and impose 512 on a path that deliberately accepts other page sizes.
- RE-RIPTOPL-140: The four definition sites hold, but the `patches.h` redefinition is a legal identical redefinition, and the prescribed `asm.S` fix is not asm-viable as stated — `ee_core.h` is a C header with externs/enums/prototypes that cannot be included from the assembler TU, so a fix needs an asm-safe shared defines header; deleting the `patches.h` copy remains sound.
- RE-RIPTOPL-145: The parser divergence is real (`mbHex8` lacks `a-f`), but the claimed live consequence has zero current reachability: both in-tree producers format addresses with uppercase-only hex, so no lowercase string can reach igrmbox from this tree — a latent parser inconsistency worth aligning, not a live load failure.
- RE-RIPTOPL-147: The cited bdmevent location does not exist (the file is 88 lines; the snapshot is at :12-56), BDM's `refused` flag is private to bdm.c and not exported so the fix needs a new export rather than reuse, and bdmevent's policy deliberately differs from BDM's on `sectorSize == 0` / `parNr` — only a partially-overlapping re-derivation with an intentionally different diagnostic rule remains.
- RE-RIPTOPL-159: The ~627 gated lines, export stubs and the `lwip_select` timeout bug are real, but the headline 'cannot link if enabled' mechanism is defeated: the dhcp calls are double-gated by `#if LWIP_DHCP`, which is 0 unless `PS2IP_DHCP` is defined (nothing defines it), so they compile out in a plain `FULL_LWIP=1` build — true scope is a default-off, stubbed, internally buggy dead subsystem.
- RE-RIPTOPL-194: The `HttpSendGetRequest` init-guard half holds, but the `HttpStreamEnd` half is narrower and its fix unsafe as worded: the EE stub has zero EE callers, and its name and shared header declaration belong also to a live IOP `HttpStreamEnd` — delete only the EE stub definition, keep the declaration and the IOP teardown.
- RE-RIPTOPL-228: The 250-walk save cost stands, but the claimed round-trip loss does not occur: after removals the save side rewrites picks contiguously from 000, so files this code writes are gap-free — the load drop-after-gap is a latent fragility reachable only via a hand-edited or foreign-written config.
- RE-RIPTOPL-238: The `select_cb_list` walk is dead in shipped builds (`lwip_select` is `FULL_LWIP`-gated and its export stubbed), but the semaphore/`rcvevent` bookkeeping in the same function is live — `rcvevent` is consumed by the ungated `lwip_recvfrom` non-blocking check — so only the list walk may be gated/deleted; semaphore creation and the event counters must stay.

## Defeating evidence
- RE-RIPTOPL-056: The sole caller `rp_thread` frees every persistent block on any `rp_setup()` failure (rapull.c:337-347: `rp_win`, `rp_entry_off`, `rp_node_val`, `rp_stage`, `rp_list`), and `rp_setup` itself frees the scratch `bits` block on both paths — no allocation survives a failed setup, so there is no lifetime leak.
- RE-RIPTOPL-093: The takeover path resets the timestamp in the very range cited: the shared initialisation block sets `NewEntry->DateStamp = GetTimestamp();` at src/core/OSDHistory.cpp:258 alongside `LaunchCount = 1` at :255, so the new entry does not inherit the evicted slot's timestamp.
- RE-RIPTOPL-239: The 57 files are run in CI: .github/workflows/ps2build.yml:71 runs `tools/run_tests.sh -k`, whose loop at tools/run_tests.sh:5-9 globs every `.github/scripts/test_*.py` — the headline and loop-job fix fall; only the narrow residue survives, that `tests/smb2-live/run.sh` is invoked by no workflow or runner.

## Candidates
### CAND-01
- Label: do_bind RAW branch `raw_new()` unchecked (api_msg.c)
- Mechanism: In `do_bind`, the RAW branch assigns `raw_new(...)` to `pcb.raw` and immediately calls `raw_recv` on it with no NULL check, so PCB-pool exhaustion dereferences NULL in the tcpip thread.
- Locations: modules/network/SMSTCPIP/api_msg.c:414-416
- Proposed class: EXTENSION of RE-RIPTOPL-030
- Justification: Same function and same unchecked-allocation mechanism as RE-RIPTOPL-030, which files only the UDP (`udp_new()`) and TCP (`tcp_new()`) branches — the RAW branch is an additional uncovered instance its fix should cover.

### CAND-02
- Label: ps2ips do_send/do_sendto `sendlen - pkt->malign` underflow
- Mechanism: In do_send/do_sendto, `sceSifGetOtherData(..., sendlen - pkt->malign, 0)` underflows the length argument when a malformed `malign` exceeds `sendlen` (= `MIN(BUFF_SIZE, pkt->length)`), independently of the malign memcpy RE-RIPTOPL-013 files.
- Locations: modules/network/ps2ips/ps2ips.c:415,448
- Proposed class: EXTENSION of RE-RIPTOPL-013
- Justification: Same unvalidated `pkt->malign` field and same handler fix site as RE-RIPTOPL-013, but an additional consequence/site (the GetOtherData length) that RE-RIPTOPL-013's memcpy/offset account does not cover.

### CAND-03
- Label: ZSO `c_buff += b_size` advances by the unclamped index-derived size
- Mechanism: In the ZSO block loop only the per-block copy length is clamped to 2048, while `c_buff += b_size` advances the source pointer by the unclamped index-derived `b_size`, so a crafted index can steer later blocks' source reads well past the compressed data before the decoder is reached.
- Locations: modules/isofs/zso.c:105-120
- Proposed class: EXTENSION of RE-RIPTOPL-011
- Justification: Same ZSO read loop and same untrusted block-size field as RE-RIPTOPL-011, but an additional unclamped use (pointer advance) beyond the decoder call RE-RIPTOPL-011 files.

### CAND-04
- Label: `icmp_time_exceeded` repeats RE-RIPTOPL-049's unchecked `pbuf_alloc` pattern
- Mechanism: `icmp_time_exceeded` allocates `q = pbuf_alloc(...)` and then writes through `q->payload` with no NULL check, verbatim the RE-RIPTOPL-049 pattern; it is gated by `#if IP_FORWARD`, which is off in this build, so it is currently dead but is a second copy of the defect if forwarding is enabled.
- Locations: modules/network/SMSTCPIP/icmp.c:170
- Proposed class: EXTENSION of RE-RIPTOPL-049
- Justification: Same unchecked-`pbuf_alloc` mechanism in the same file as RE-RIPTOPL-049, at an additional function/site RE-RIPTOPL-049 (icmp_dest_unreach only) does not cover.

### CAND-05
- Label: `recv_raw` shares `recv_udp`'s missing-free shape
- Mechanism: `recv_raw` likewise posts the pbuf only when `conn->recvmbox != SYS_MBOX_NULL` and has no else-`pbuf_free`, so an undeliverable pbuf is neither delivered nor freed (with the same reachability caveat as RE-RIPTOPL-052).
- Locations: modules/network/SMSTCPIP/api_msg.c:~45
- Proposed class: EXTENSION of RE-RIPTOPL-052
- Justification: Same missing-free mechanism and same file as RE-RIPTOPL-052, at an additional callback (`recv_raw`) that RE-RIPTOPL-052 (`recv_udp` only) does not cover.

### CAND-06
- Label: `prepare_path` trailing-slash strip is dead code
- Mechanism: Because `p2` starts on the NUL terminator, the trailing-slash/backslash strip loop's condition is false on the first test for every input, so trailing separators are never stripped and the copy loop at :318 copies through `p2` inclusive — prepared paths retain separators the code intends to remove (memory-safe, functional only).
- Locations: modules/network/smbman-ra/smb_fio.c:312-314
- Proposed class: NEW
- Justification: Distinct functional mechanism not filed in the redo corpus: RE-RIPTOPL-037(a) filed the opposite claim at these lines (a dangerous walk before the buffer), which verification refuted — no redo finding files the never-strips defect.

### CAND-07
- Label: mmce `MMCE_CMD_SET_GAMEID` uses `arg` as a string with no NULL/arglen check
- Mechanism: The `MMCE_CMD_SET_GAMEID` devctl case does `char *str = (char*)arg; mmce_cmd_set_gameid(str);` with no `arg == NULL` or `arglen` validation, so a NULL or short devctl argument is consumed as a string.
- Locations: modules/mmce/mmceman/src/mmce_fs.c:~1129
- Proposed class: EXTENSION of RE-RIPTOPL-042
- Justification: Same devctl switch and same unchecked-`arg` mechanism as RE-RIPTOPL-042 — a fifth, uncounted site in its family (string argument rather than `*(u32*)arg`).

### CAND-08
- Label: `mmce_cmd_set_gameid` `strcpy` of a full game ID into a fixed 255-byte stack `wrbuf` after u8 length truncation
- Mechanism: `u8 len = strlen(ptr) + 1` truncates, then `strcpy(str, ptr)` copies the full string into the fixed `u8 wrbuf[0xFF]` stack buffer regardless of `len`, so a game ID longer than ~250 bytes overflows the IOP stack buffer before the `len + 5` send is reached.
- Locations: modules/mmce/mmceman/src/mmce_cmds.c:230,241
- Proposed class: NEW
- Justification: Distinct mechanism as verification states: RE-RIPTOPL-084 covers only this function's one trailing uninitialised byte and RE-RIPTOPL-085 covers filename-length truncation in mmce_fs.c — neither files a full-string stack overflow in mmce_cmds.c.

### CAND-09
- Label: OSDHistory victim scan: `LeastUsedRecordTimestamp` not reset when a strictly smaller `LaunchCount` is found
- Mechanism: In the least-used scan, when a strictly smaller `LaunchCount` is found `LeastUsedRecord`/`LeastUsedRecordLaunchCount` are updated but `LeastUsedRecordTimestamp` is not reset to that record's `DateStamp`, so subsequent equal-count comparisons are made against a previous candidate's timestamp (or `INT_MAX`) and the oldest tie-break can pick the wrong victim.
- Locations: src/core/OSDHistory.cpp:174-183
- Proposed class: NEW
- Justification: Distinct mechanism in the victim-selection scan, explicitly distinct from RE-RIPTOPL-093's (refuted) claim about the new entry's fields after takeover, and covered by no other redo finding.

### CAND-10
- Label: Third keyboard-path VLA, `src/ui/menusys.cpp:306` `char newName[nameLength]`
- Mechanism: `char newName[nameLength]` is a stack VLA whose length comes from the per-backend `gsOf(support)->nameLength(support, row.id)`, guarded only by `nameLength <= 0`, and it feeds `Gui::showKeyboard(newName, nameLength)` at :309 — a caller whose length is device-derived, not a fixed dialog size.
- Locations: src/ui/menusys.cpp:306
- Proposed class: EXTENSION of RE-RIPTOPL-105
- Justification: Verify report states it extends RE-RIPTOPL-105: same caller-supplied-length VLA mechanism, at a third location RE-RIPTOPL-105 (gui.cpp/diaeditors.cpp only) does not cover.

### CAND-11
- Label: PC-tool `crctab[0x400]` oversize in iso2opl and opl2iso
- Mechanism: Both PC tools declare `u32 crctab[0x400]` with only indices 0-255 filled/used and rebuild the table on every `crc32()` call — the same 4x oversize plus per-call rebuild defect as RE-RIPTOPL-104.
- Locations: tools/pc/iso2opl/src/iso2opl.c:15; tools/pc/opl2iso/src/opl2iso.c:14
- Proposed class: EXTENSION of RE-RIPTOPL-104
- Justification: Same defect mechanism as RE-RIPTOPL-104 (which covers only src/platform/system.cpp); RE-RIPTOPL-127 covers these files' crc32 duplication but not the oversize/per-call rebuild.

### CAND-12
- Label: Broken `CRC7_MODE_RAM_TABLE` test also in `spi_sdcard_crc7.h`
- Mechanism: The header also tests `SD_CRC7_MODE == CRC7_MODE_RAM_TABLE` (a nonexistent macro), so the `crc7_generate_table` extern declaration is compiled out by the same accident as the definition in spi_sdcard_crc7.c.
- Locations: modules/mx4sio_bd/src/spi_sdcard_crc7.h:10
- Proposed class: EXTENSION of RE-RIPTOPL-180
- Justification: Verify report states it extends RE-RIPTOPL-180: same broken macro test, at the header location RE-RIPTOPL-180 (which cites only the .c) does not cover.

### CAND-13
- Label: Second broken `PACKET_TCODE` copy at bdmassault `iLink_internal.h:92`
- Mechanism: An identical copy of the broken object-like `PACKET_TCODE` macro (body referencing an undefined `x`, never used) exists in the bdmassault tree, so deleting only the modules/ilink copy leaves the landmine in place.
- Locations: modules/bdmassault/ilink_bd_assault/src/iLinkman/src/include/iLink_internal.h:92
- Proposed class: EXTENSION of RE-RIPTOPL-183
- Justification: Verify report states it extends RE-RIPTOPL-183: identical macro/mechanism, at a second copy RE-RIPTOPL-183 (modules/ilink only) does not cover.

### CAND-14
- Label: Second dead `DeinitIEEE1394` copy in bdmassault `sbp2_driver.c`
- Mechanism: A second copy of the dead, uncalled `DeinitIEEE1394` definition (with its declaration) exists in the bdmassault tree, likewise uncalled.
- Locations: modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:775
- Proposed class: EXTENSION of RE-RIPTOPL-184
- Justification: Verify report states it extends RE-RIPTOPL-184: same dead function/mechanism, at a second copy RE-RIPTOPL-184 (modules/ilink only) does not cover.

### CAND-15
- Label: Fix-completeness for RE-RIPTOPL-171: `test_smb_read_closed.py` hard-exits if `USE_CUSTOM_RECV` disappears
- Mechanism: `.github/scripts/test_smb_read_closed.py:64-65` hard-exits if `USE_CUSTOM_RECV` is no longer defined ('update this check'), so deleting the macro as RE-RIPTOPL-171 prescribes must update that host test in the same change or CI goes red.
- Locations: .github/scripts/test_smb_read_closed.py:64-65
- Proposed class: REJECTED
- Justification: Not a defect: the test is an intentional guard whose message directs the updater, and the note is only a fix-completeness instruction for RE-RIPTOPL-171 — no standalone defect mechanism is filed.

### CAND-16
- Label: Guard gap wider than RE-RIPTOPL-194 states: EE `HttpStreamBegin` and `HttpStreamRead` also lack the init guard
- Mechanism: In src/platform/httpclient.cpp, `HttpStreamBegin` (:95) and `HttpStreamRead` (:128) also lack the `HttpIsInitialized()` guard that `HttpEstabConnection` has, so the inconsistency is file-wide, not specific to `HttpSendGetRequest`.
- Locations: src/platform/httpclient.cpp:95,128
- Proposed class: EXTENSION of RE-RIPTOPL-194
- Justification: Same file and same missing-init-guard mechanism as RE-RIPTOPL-194, at two additional entry points RE-RIPTOPL-194 (HttpSendGetRequest only) does not cover.

### CAND-17
- Label: `pbuf_dechain` phantom declaration also an IOP import at `smstcpip.h:39-40`
- Mechanism: `pbuf_dechain` is additionally declared as an IOP import, `DECLARE_IMPORT(41, pbuf_dechain)`, at modules/network/common/smstcpip.h:39-40, i.e. the phantom declaration exists on both sides of the EE/IOP boundary.
- Locations: modules/network/common/smstcpip.h:39-40
- Proposed class: REJECTED
- Justification: Already fully covered by RE-RIPTOPL-051, which files exactly this declaration (smstcpip.h:37-42, `I_pbuf_dechain` as import 41) and prescribes deleting it — a duplicate, not an extension of RE-RIPTOPL-172's lwIP-header copy.

### CAND-18
- Label: `mmce_fs_ioctl2` returns 0 for every unknown command
- Mechanism: `mmce_fs_ioctl2` initialises `res` to 0, its `default:` case is empty, and every unknown `cmd` returns 0 (success) — only `MMCE_CMD_IOCTL_GET_FD` is handled — the same silent-success defect as RE-RIPTOPL-201, in the adjacent ioctl2 slot.
- Locations: modules/mmce/mmceman/src/mmce_fs.c:1169-1183
- Proposed class: EXTENSION of RE-RIPTOPL-201
- Justification: Same silent-success mechanism as RE-RIPTOPL-201, at a separate uncovered instance (ioctl2 slot :1213) that RE-RIPTOPL-201 (ioctl stub :402-405, slot :1194 only) does not cite.

### CAND-19
- Label: Same stale tar comment in `src/platform/tar.cpp:9`
- Mechanism: `src/platform/tar.cpp:9` repeats the stale 'RiptOPL wires only TAR_KIND_ART' comment that RE-RIPTOPL-255 files against include/tar.h, so the 255 fix applied only to the header would leave the comment misleading readers of the implementation file.
- Locations: src/platform/tar.cpp:9
- Proposed class: EXTENSION of RE-RIPTOPL-255
- Justification: Same stale comment/mechanism as RE-RIPTOPL-255, at an additional location (tar.cpp) that RE-RIPTOPL-255 (tar.h:4-8 only) does not cover.

### CAND-20
- Label: RE-RIPTOPL-261's scratch-dir exclusion list duplicated verbatim at `Makefile:111`
- Mechanism: The five agent scratch dirs filed in RE-RIPTOPL-261 at tools/assets.py:203-204 also appear verbatim in the dead Makefile's version anchor at Makefile:111 (`CODE_ANCHOR`); it disappears if RE-RIPTOPL-253's Makefile deletion lands, but 261's fix as scoped cleans only the assets.py copy.
- Locations: Makefile:111
- Proposed class: EXTENSION of RE-RIPTOPL-261
- Justification: Same scratch-dir exclusion list/mechanism as RE-RIPTOPL-261, at an additional verbatim copy (Makefile) that RE-RIPTOPL-261 (assets.py only) does not cover.

### CAND-21
- Label: `test_ra_language_isolation.py`'s PyYAML exposure is live via ps2build/run_tests.sh
- Mechanism: `test_ra_language_isolation.py` imports yaml at :9, is not named in flavours.yml, and runs via ps2build.yml -> tools/run_tests.sh with no PyYAML install, so it has exactly the uninstalled-PyYAML exposure RE-RIPTOPL-251 files — live, not dormant as 251's merged note states.
- Locations: .github/scripts/test_ra_language_isolation.py:9
- Proposed class: EXTENSION of RE-RIPTOPL-251
- Justification: Same no-install PyYAML mechanism as RE-RIPTOPL-251, as an additional live instance beyond its four listed suites (251's merged note mentions the file only as dormant/unwired, which verification refuted).
