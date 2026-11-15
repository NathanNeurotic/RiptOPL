# RiptOPL Round 7 — Headers as a Corpus (function-like macros & static inlines)

Pin: `2e347d5a` (`fix(smb1): share VMC reads without concurrent writers (#420)`), clone
`~/workspace/audit-clones-2026-11-13/RiptOPL`, git-clean, read-only.
Corpus: all `*.h` under `include/`, `src/`, `ee_core/`, `modules/`, `tools/`, `elfldr/`,
excluding `third_party` — **439 headers, 35,808 lines** (matches the brief exactly;
`tools/` and `elfldr/` contribute no first-party function-like macros).

## Census (method shown)

Function-like macro definitions were extracted with a continuation-joining parser
(`#define NAME(args) body`, backslash-continuations merged before analysis):

| Class | Count |
|---|---|
| Function-like macro definitions, raw (all headers) | **714** |
| — in `modules/` | 661 |
| — in `ee_core/` | 40 |
| — in `include/` | 13 |
| Size/count/length/offset/index/alignment arithmetic (name or body computes a size, count, length, offset, index, bound, align-up or ceil-div; incl. `sizeof` bodies and the `FD_*` index macros) | **74 raw / 51 distinct** (rest are copies of the same macro in duplicated files, e.g. `udprdma_packet.h` ×3, `sio2regs.h` ×2, `sbp2_disk.h` ×2) |
| Macros whose body uses `sizeof` | 13 |
| Macros evaluating an argument two or more times | 74 |
| `static inline` functions in headers | **109** |

**Headline negative result (READ):** there is **no `ARRAY_SIZE`-family macro anywhere in
the header corpus** — no `#define … sizeof(x)/sizeof(x[0])`, no `sizeof(*ARR)/sizeof(ARR[0])`
pattern, in any spelling. The sibling repo's `GET_XPARAM_DB_SIZE ≡ 1` defect class
(`sizeof(*ARR)/sizeof(ARR[0])` ≡ 1) therefore has **no instance to file here**: the only
`sizeof(a)/sizeof(a[0])` expressions in the whole tree are two open-coded loop bounds in
`.c` files (`modules/mmce/mmceman/src/sio2man_hook.c:68,273` and the `mx4sio_bd` copies),
both written correctly as `sizeof(td->regdata)/sizeof(td->regdata[0])` /
`sizeof(g_hook_data)/sizeof(g_hook_data[0])` (array, not pointer — READ, out of this
section's header scope, noted for completeness).

## Candidate findings

**None filed.** Every size/count/index macro was expanded by hand (below); no expansion
is concretely wrong. Every double-evaluation and parenthesisation suspect was checked
against its call sites as the brief requires, and every one dissolved (documented under
Checked-clean). Every `static inline` was read for bounds/termination defects; none
survives with a reachable defect. Filing any of the dissolved items would require
exactly the call-site evidence that does not exist.

## Checked-clean — size/count/index macros verified CORRECT (expansion shown)

1. **`MEM_ALIGN_SIZE(size)` / `MEM_ALIGN(addr)`** — `modules/network/SMSTCPIP/include/lwip/mem.h:53,57`
   `((size) + MEM_ALIGNMENT - 1) & ~(MEM_ALIGNMENT - 1)`. With `MEM_ALIGNMENT=4`,
   `size=13`: `(13+3) & ~3 = 16 & ~3 = 16` ✓; `size=16 → 19 & ~3 = 16` ✓. Standard align-up.
2. **`RA_SNAP_TRAILER_OFF(bytes)`** — `modules/network/common/ra_snap.h:62`:
   `RA_SNAP_HDR + (((bytes)+3) & ~3)`, `RA_SNAP_HDR = sizeof(struct ra_snap) = 64`
   (8 u32 = 32 + `game_id[16]` = 48 + 2 u32 = 56 + `pad[2]` = 64 ✓).
   `bytes=100 → 64 + 100 = 164` ✓ (100 already 4-aligned); `bytes=101 → 64+104 = 168` ✓.
3. **`RA_SNAP_DMA_SIZE(bytes)`** — `ra_snap.h:65`: `(TRAILER_OFF + 4 + 15) & ~15`.
   `bytes=100 → (164+4+15) & ~15 = 183 & ~15 = 176` = round-up-16 of 168 ✓ (trailer word included).
4. **`RA_SNAP_TOTAL_FOR(bytes)`** — `ra_snap.h:69`: `(DMA_SIZE + 63) & ~63` — round-up-64 of the above ✓.
5. **`RA_SNAP_SEND_EVERY(bytes)`** — `ra_snap.h:80-81`:
   `(((bytes + CHUNK-1)/CHUNK + PARTS_PER_FRAME-1)/PARTS_PER_FRAME)` with
   `RA_SNAP_CHUNK_BYTES = 1472-191 = 1281` (`ra_watch.h:42`), `PARTS_PER_FRAME = 3` (`ra_watch.h:50`).
   Nested ceil-div is compositional: `bytes=11529` (= `RA_SNAP_MAX_BYTES`, 1281×9):
   `ceil(11529/1281)=9`, `ceil(9/3)=3` ✓. `bytes=1281 → ceil=1 → (1+2)/3 = 1` ✓.
6. **`RA_EE_WORK_BYTES(count, nodes)`** — `ra_snap.h:79`:
   `RA_SNAP_TOTAL + (count)*4 + (nodes)*16`. Comment promises 2 source + 2 scratch words
   per node = 16 B ✓, one word per watch entry = 4 B ✓. Both call sites
   (`ee_core/src/ra.c:133,154`) compare against `config->raWorkBytes` **before** use, and
   the header itself warns "Counts must be validated before evaluating this size" —
   validation exists upstream in `raWatchEntriesValid` (item 12). READ.
7. **`IGR_CHEATS_SPARE_WORDS`** — `ee_core/include/igrcheats.h:33`:
   `(1 + 250*2 + 3) & ~3 = 504 & ~3 = 504` words; layout needs 1 + 500 = 501, rounded up
   to a multiple of 4 ✓ (504 ≥ 501, no truncation).
8. **`IGR_CHEATS_BYTES(count, pairs)`** — `igrcheats.h:49-50`:
   `sizeof(igr_cheats_t) + 504*4 + (count)*sizeof(igr_cheat_t) + (pairs)*8`
   = `16 + 2016 + count*32 + pairs*8`. Struct sizes verified from the header itself:
   `igr_cheats_t` = 4×u32 = 16 ✓; `igr_cheat_t` = 2+2+1+3+24 = 32 ✓; one code pair =
   2×u32 = 8 ✓. Cross-checked against the pointer macros: `IGR_CHEATS_ROWS(h)` =
   header (16 B) + spare (504 words = 2016 B) — the same prefix `IGR_CHEATS_BYTES`
   charges ✓; `IGR_CHEATS_CODES(h)` = rows + `count` rows ✓.
9. **`FD_SET(n,p)` / `FD_CLR` / `FD_ISSET`** — `modules/network/SMSTCPIP/include/lwip/sockets.h:203-205`
   and `modules/network/common/smstcpip-common.h:288-290`:
   `(p)->fd_bits[(n)/8] |= (1 << ((n)&7))` with `fd_bits` an `unsigned char[(FD_SETSIZE+7)/8]`,
   `FD_SETSIZE 16` → 2 bytes. `n=15 → fd_bits[1] |= 1<<7` ✓ (byte-array indexing is
   consistent end to end — this is **not** the 32-bit-word BSD layout, and both the
   struct and the macros agree). Reachability of the unchecked index: the SMSTCPIP
   stack issues only fds `0 .. NUM_SOCKETS-1` with `NUM_SOCKETS = MEMP_NUM_NETCONN = 4`
   (`sockets.c:49`, `opt.h:149`; RA opts lower it to 2/1), so no stack-issued fd can
   reach index 2. Caller-supplied fds in `modules/network/smbman-ra/poll.c:37,44` are
   SMSTCPIP sockets from the same allocator (INFERRED bound, no concrete OOB site found).
10. **`LZ4_COMPRESSBOUND(isize)`** — `modules/isofs/lz4.h:86`:
    `(isize) + ((isize)/255) + 16`, guarded to 0 above `LZ4_MAX_INPUT_SIZE` — the stock
    LZ4 worst-case bound ✓ (`isize=255 → 255+1+16 = 272` ✓).
11. **`btoi(b)` / `itob(i)`** — `modules/iopcore/cdvdman/internal.h:67-68`:
    `btoi: (b)/16*10 + (b)%16` — `0x59 → 5*10+9 = 59` ✓; `itob: (i)/10*16 + (i)%10` —
    `59 → 5*16+9 = 0x59` ✓. Call sites (`fatfs/diskio.c:153-158`) pass struct fields, no side effects.
12. **`raWatchEntriesValid`** (static inline, counted here for its arithmetic) —
    `modules/network/common/ra_watch.h:63-77`: rejects `count > RA_WATCH_MAX (4096)`,
    per-entry `RA_WATCH_SIZE = (e)>>28` restricted to {1,2,4}, `sum` ≤ 4096×4 = 16384
    (no int overflow), returns `sum == bytes` ✓.
13. **`RA_WATCH_PACK(addr, size)` / `RA_NODE_PACK(parent, from_node, size)`** —
    `ra_watch.h:59,152-153`: pack `size<<28 | addr & 0x0FFFFFFF` and
    `size<<13 | from_node<<12 | parent & 0x0FFF` are exact inverses of
    `RA_WATCH_ADDR/SIZE` and `RA_NODE_PARENT/FROM_NODE/SIZE` (`ra_watch.h:57-58,149-151`) ✓.
14. **`UDPRDMA_HDR_SIZE(hdr)` / `UDPRDMA_PAYLOAD_SIZE(hdr)`** — `modules/network/udpfs_{bd,fhi,ioman}/src/udprdma_packet.h:95,97`:
    `hdr_word_count * 4` (words→bytes ✓); payload = header + `data_byte_count` ✓.
    `PAYLOAD_SIZE` evaluates `hdr` twice, but every call site passes a plain struct
    variable (grep of `modules/network/udpfs_*` — READ), so no double-eval defect.
15. **`GET_*` field extractors** — `ee_core/include/igs_api.h:103-114`: all twelve
    shift/mask pairs match the GS register layouts, e.g. DISPFB FBP `>>0 &0x1FF`,
    FBW `>>9 &0x3F`, PSM `>>15 &0x1F`, DBX `>>32 &0x7FF`, DBY `>>43 &0x7FF` ✓.
    `GET_DISPLAY_DH(x) = >>44 &0x7FF` was specifically suspected (DH at 43?) and is
    **confirmed correct by two independent in-repo sources**: `include/pggsm.h:49`
    packs DH at `<<44`, and `ee_core/src/igs_api.c:489` decodes `(display1 >> 44) & 0x7FF`
    open-coded. All call sites (`igs_api.c:717-771`) pass plain variables.
16. **`GS_GIFTAG` field shifts** — `ee_core/include/igs_api.h:67-73`: NLOOP@0, EOP@15,
    PRE@46, PRIM@47, FLG@58, NREG@60 — the hardware GIFtag layout ✓ (values verified;
    parenthesisation dissolved separately below).
17. **`TCP_TCPLEN(seg)`** — `modules/network/SMSTCPIP/include/lwip/tcp.h:209`:
    `len + ((FLAGS & TCP_FIN || FLAGS & TCP_SYN) ? 1 : 0)`. `&` binds tighter than `||`
    in C, so this is `(FLAGS & FIN) || (FLAGS & SYN)` — the intended SYN/FIN-counts-one
    sequence-space rule ✓ (verified by precedence, not by eyeball).
18. **`MEM_ALIGN` family, `CDIOC_FNUM`, `GET_OPL_MOD_ID/SIZE`** (`ee_core/include/modules.h:92,94`:
    `id = x>>24`, `size = x & 0x00FFFFFF` — complementary split of the packed word ✓),
    **`_IOR/_IOW`** (`sockets.h:175,177`: `sizeof(t) & IOCPARM_MASK(0x7f)` at `<<16` ✓).

## Checked-clean — dissolved parenthesisation / double-evaluation suspects

Per the brief these are filed **only** with a call site that breaks; none has one.

- **`LWIP_MAX(x,y)` / `LWIP_MIN(x,y)`** — `modules/network/SMSTCPIP/include/lwip/def.h:38-39`:
  `(x) > (y) ? (x) : (y)` — no outer parentheses, both arguments evaluated twice.
  **All three call sites are safe** (READ): `tcp.c:607` and `tcp_out.c:325` are plain
  assignments of field reads; `tcp_in.c:695`
  `ssthresh = LWIP_MAX((pcb->snd_max - pcb->lastack) / 2, 2 * pcb->mss)` — assignment
  binds looser than `?:`, so the expansion groups correctly, and both arguments are
  side-effect-free. Latent hazard for future callers, not a defect at this pin.
- **`MIN(x,y)`** — `modules/isofs/zso.h:19`: `((x < y) ? x : y)`, arguments
  unparenthesised and double-evaluated. Sole call site `zso.c:106`: `MIN(b_size, 2048)`
  — two side-effect-free operands; expansion correct there. Dissolved.
- **`GS_GIFTAG` / `GS_GSBITBLTBUF_SET` / `GS_GSTRXREG_SET` / `GS_GSTRXPOS_SET`** —
  `ee_core/include/igs_api.h:67,76,81,84`: bodies are top-level `|`-chains with no
  enclosing parentheses. All four call sites (`ee_core/src/igs_api.c:219,221,223,225`)
  are plain assignments `DMA64Packet[n] = MACRO(...);`, where the chain is the whole
  right-hand side and groups correctly. No call site embeds them under a tighter
  operator (`&`, `==`, `<<`). Dissolved.
- **74 double-evaluating macros** (incl. `BSWAP32/BSWAP16` ×2 files, `htonl/htons` in
  `smstcpip-common.h:304,308`, `FD_*`, `IGR_CHEATS_CODES(h)` — `h` twice via
  `IGR_CHEATS_ROWS(h) + (h)->count`): every call site of the arithmetic members was
  grepped; **no call site passes `++`, `--`, or a function call as the repeated
  argument** (broad grep over all `.c` for the pattern returned zero hits; spot-verified
  for `BSWAP32` in `sbp2_driver.c:699,761`, `iLink_crom.c:42,232`, `IGR_CHEATS_*` in
  `ee_core/src/igrmenu.c:294,459,985` — all plain variables/array elements). Dissolved.
- No function-like macro in the corpus is a multi-statement macro missing
  `do { } while (0)` at a call site that breaks: the statement macros that need it
  have it (`TCP_REG`, `TCP_RMV`, `LWIP_DEBUGF`, `LOG_ENABLE` variants in
  `include/oplog.h:12,18,26`).

## Checked-clean — static inline functions (109 read; bounds/termination only)

Notable verified-correct, with the arithmetic shown:

- **`gridTopRow/gridPageStep/gridRowStep/gridClamp/gridLayoutPreset`** — `include/grid.h:16-102`:
  every entry point rejects `count<=0`, out-of-range `index`, `rows/columns<1`;
  `gridTopRow`: `index` clamped to `count-1` before `index/columns/rows*rows`;
  `gridPageStep` (46-57) returns `count-1` exactly when `index+page >= count` but a
  next page exists; `gridLayoutPreset` bounds `layout` to `1..GRID_LAYOUTS(4)` before
  indexing `sd/hd[layout-1]` ✓.
- **`igrCheatsBuild` / `igrCheatsFit`** — `ee_core/include/igrcheats.h:55,74`:
  `Build` checks `n + row[i].count > IGR_CHEAT_SET_CODES(250)` **and**
  `row[i].first + row[i].count > b->pairs` before the inner copy; writes
  `set[1+n*2], set[2+n*2]` with `n < 250` → max index 500, inside the 504-word spare
  layout of item 7 ✓.
- **`eeResetPayloadLength`** — `ee_core/include/reset_arg_bounds.h:9`: rejects
  `packetLength < 10`, `packetLength > packetCapacity`, `commandCapacity < 7` before
  computing `payload > commandCapacity - 7` (no unsigned underflow — the `< 7` guard
  precedes the subtraction) ✓.
- **`apaFenceIsValidMbrHeader` / `apaFenceWriteAllowed`** — `modules/hdd/apa/src/table_fence.h:49,85`:
  the 1 KB checksum loop reads words `1..255` (bytes ≤ 1023) and the MBR-magic loop
  bytes `0x100..0x11D`; reachable only when `lba==0 && nsectors==2` (buf = 1024 B), so
  every read is in-bounds **by the caller's own gate** ✓. Writes touching LBA 0-7 in
  any other shape are refused; `lba >= 8` cannot touch the table sector ✓.
- **`bdmParseMassSlot` / `bdmFormatTypedPath`** — `include/bdmtyped.h:37,60`:
  digit loop returns -1 on a 3rd digit (slot ≤ 99, no int overflow possible);
  `snprintf` result checked `ret >= outLen` with `out` emptied on failure ✓.
- **`oplParseILinkID`** — `include/ilinkid.h:9`: `strlen == 16` checked before the
  16-iteration loop; output zeroed first, filled only from a fully-valid parse ✓.
- **`texPngHeaderIsSmallT8`** — `include/textures.h:182`: `len < 29` rejected before
  any read; highest index touched is `h[25]` ✓.
- **`raCaduceusSessionReply` / `raClientStarts`** — `modules/network/common/ra_client.h:12,23`:
  fixed-index reads reach `reply[38]` and the body scan is bounded by the prefix
  lengths; the sole caller (`modules/network/raudp/raudp.c:888`) passes `rx[192]`,
  NUL-terminated at `rx[got]` after `lwip_recvfrom(..., sizeof(rx)-1, ...)` — all reads
  stay inside the 192-byte buffer ✓ (no length parameter: fragile shape, no defect).
- **`psbbnFlowCard` / `psbbnFlowRoundRect`** — `include/psbbnflow.h:52,119`:
  `slot` clamped to `PSBBN_FLOW_TAIL+1 = 7`, `slots` has `TAIL+2 = 8` rows, `b` index
  ≤ 7 ✓; `RoundRect` writes exactly `4 corners × 5 steps × 2 = 40` floats and the
  caller allocates `float[PSBBN_FLOW_ROUND_POINTS * 2] = float[40]`
  (`src/gfx/psbbnflow.cpp:87`) ✓.
- **`translate_pad_shanwan`** — `include/ds34common.h:128`: hat index `dir = in[2] & 0x0F`
  clamped `if (dir > 8) dir = 8` before `hat[dir]` on a 9-entry table ✓.
- **`inl_sio2_regN_set/get`, `inl_sio2_portN_*`** — `modules/mmce/mmceman/include/sio2regs.h:182-188`
  (and `mx4sio_bd` copies): `N` itself is unchecked in the helpers, but the register
  file is exactly 16 words (`SIO2_REG_BASE + 0x200`, port controls begin at `+0x240`)
  and the DMA callers cap the loop: `elements_do = elements > 16 ? 16 : elements`
  (`modules/mmce/mmceman/src/mmce_sio2.c:267` and siblings) — `N ≤ 15` at every
  DMA site, literals elsewhere ✓.
- **`udprdma_ack_covers`** — `modules/network/udpfs_{bd,fhi,ioman}/src/udprdma_seq.h:9`:
  `((acknowledged - sent) & 0x0FFF) < 0x0800` — forward half-ring covers, the ambiguous
  exactly-2048 difference excluded, matching its own comment ✓ (×3 identical copies).
- **`nativeVmcSlotWord`** — `include/nativevmc.h:16`: scan loop `i <= (size-payload)/4`
  reads `words[i]` with `i*4 + 4 <= size - payload + 4`; in-bounds for the caller's
  payload (a marker word + payload ≥ 4 B — INFERRED from the marker design, no
  violating call site found).
- Remaining inlines are one-line forwarders/stubs with no index arithmetic
  (`blkIo*` in `modules/hdd/apa/src/hdd_blkio.h:22-65`, `eth/ip/udp_packet_send`
  wrappers in the two ministacks, `diaglog.h`/`menuwatch.h`/`launchdiag.h` no-ops,
  `sysReadDiscKey` in `include/disc_key.h:40` — NULL-checked, fixed 12 B request /
  24 B response RPC sizes).

## Scope notes

- READ vs INFERRED: all expansions above are READ from the pin and expanded by hand;
  the two INFERRED items are flagged inline (FD_SET caller-fd bound in `poll.c`,
  `nativeVmcSlotWord` payload size).
- Object-like macros (`#define NAME value`, e.g. `RA_SNAP_HEAD_BYTES 191`,
  `RA_WATCH_MAX 4096`) are outside this section's function-like brief; the two named
  were used as inputs to verified expansions above.
- Nothing in this section was built or run; all verdicts are source-level at the pin.
