# RiptOPL Round 9 — modules/network: UDPFS family, SMAP drivers, RA IOP modules

- Repo: `~/workspace/audit-clones-2026-11-13/RiptOPL`
- Pin verified: HEAD = `2e347d5ac885cd0ff73e9ad4143781946396f492` (matches assignment); clone left git-clean, no repo edits.
- Scope read in full: `udpfs_bd/`, `udpfs_fhi/`, `udpfs_ioman/`, `udpfs_ministack/`, `udpfs_shared/`, `udpfs_smap/`, `smap-ingame/`, `smap_udpbd/`, `rapops/`, `rapull/`, `raudp/` — **14,342 lines** (assignment estimate 13,379 undercounts tests/headers; the ledger in §D counts every file in the eleven directories).
- Already filed and NOT re-filed here (cross-referenced where the line-read touched them): RIPTOPL-008/-048 (core/header triplication), -043 (server per-read count trusted), -044 (dread name length vs bytes received), -081 (ioman stale server handles across reconnect), -093 (SMAP DEV9 capability check no-op in both neutrino copies — note UDPFS-14 below is a *different, adjacent* expression in the same function), -119 (HandleRxIntr never counts a packet in both neutrino copies — confirmed for `udpfs_smap/src/xfer.c:159-204` and `smap_udpbd/src/xfer.c:108-152`; the smap-ingame copy *does* count, `smap-ingame/xfer.c:212`), -127 (three stored SMAP drivers), -180 (UDPBD INFO-reply geometry unchecked), -218 (udptty tty_write returns 0 — **not** either copy in this scope: both in-scope copies return `size`, see census), -243 (two ministack implementations), -252 (ranet "RAA1 OK" parse — EE side; the IOP twin in `raudp.c` requires `"RAO1 OK "`, see checked-clean §C).

---

## A. Candidates (sorted by benefit)

### UDPFS-01 — UDPRDMA receive IST trusts the wire `hdr_word_count` and overruns the caller's app-header buffer
- Category: 6 (bug/security — wire-supplied length)
- Locations:
  - `modules/network/udpfs_shared/src/udprdma.c:185-199` (`_ist`, DATA case: `hdr_size = data_hdr.hdr_word_count * 4`, then `smap_fifo_read(0x30, s->rx_hdr_buffer, hdr_size)` or into the stack `dummy[UDPRDMA_MAX_APP_HDR]`)
  - `modules/network/udpfs_bd/src/udprdma_packet.h:503` (`hdr_word_count` is a 4-bit field → `hdr_size` can be 0–60 bytes; identical header in fhi/ioman)
  - `modules/network/udpfs_bd/src/udprdma_packet.h:517` (`UDPRDMA_MAX_APP_HDR 32` — the dummy stack buffer is 32 bytes, the wire can demand 60)
  - `modules/network/udpfs_shared/src/udpfs_core.c:91,94` (`_recv_with_result` registers an **8-byte** stack struct `udpfs_msg_result_reply_t` as the app-header buffer)
  - `modules/network/udpfs_shared/src/udprdma.c:71,736` (`rx_hdr_size` is stored by `udprdma_set_rx_app_header` and **never read anywhere else** — grep-verified)
- Problem: A DATA packet whose `hdr_word_count` exceeds the registered header size makes the interrupt-context `_ist` copy up to 60 FIFO bytes into an 8-byte stack buffer in `_recv_with_result` (every file/block read reply), or 60 bytes into the 32-byte `dummy[]` stack array when no header buffer is registered. The one field that records the expected size (`rx_hdr_size`) is written and never consulted. Any host on the LAN that answers the broadcast discovery (or spoofs the server's address after it) controls this field; no authentication exists anywhere in UDPRDMA.
- Fix: In `_ist`, clamp: `if (hdr_size > s->rx_hdr_size) hdr_size = s->rx_hdr_size;` for the registered-buffer path (still FIFO-skip the remainder so the data offset stays correct), and clamp the dummy path to `UDPRDMA_MAX_APP_HDR`. Two comparisons, no new code paths.
- Justification if additive: N/A (pure clamp on existing reads).
- Benefit: Removes a remotely triggerable stack overwrite in interrupt context on the read hot path of all three UDPFS variants (the shared source compiles into bd, fhi and ioman).
- Effort: S
- Risk: Low (packets with an oversized header field are already protocol violations; conforming servers send exactly the registered size).
- Confidence: High (field width, buffer sizes and the unread `rx_hdr_size` all read directly at the lines cited).

### UDPFS-02 — `udpfs_core_get_sector_count` truncates capacity to a signed `int`: shares ≥ 1 TiB can never register as a block device
- Category: 6 (bug — width truncation)
- Locations:
  - `modules/network/udpfs_shared/src/udpfs_core.c:738` (return type `int`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:783-788` (`uint64_t total_bytes` → `uint32_t sector_count = total_bytes >> 9` → `return sector_count;`)
  - `modules/network/udpfs_bd/src/udpfs_bd.c:163-173` (`ret = udpfs_core_get_sector_count(0); if (ret < 0) return 0; … g_udpbd.sectorCount = ret;`)
- Problem: At 512-byte sectors, 2³¹ sectors = 1 TiB. For any share ≥ 1 TiB the `uint32_t` sector count has its top bit set and the implicit conversion to the `int` return makes it negative; `udpfs_bd_try_connect` reads every negative value as an error and retries forever — the device silently never appears, with no size-related diagnostic. Between 2 TiB and 4 TiB the count additionally wraps to a small positive value and a wrong, too-small device would register.
- Fix: Return `int64_t` (or `uint32_t` with a separate error out-param) from `udpfs_core_get_sector_count` and store into `sectorCount` without the signed round-trip; reject only a genuine negative reply `result` (already checked at core:777-780).
- Justification if additive: N/A (signature widening only).
- Benefit: UDPFS-BD works with the multi-TB shares that are the normal case for a games drive; today it fails silently at exactly those sizes.
- Effort: S
- Risk: Low (one caller in scope: udpfs_bd; FHI never calls it — grep-verified).
- Confidence: High (arithmetic is fully determined by the cited lines).

### UDPFS-03 — Fixed-size UDPFS replies are never length-checked; a short reply is parsed out of stale buffer contents
- Category: 6 (bug/security — wire-supplied length)
- Locations:
  - `modules/network/udpfs_shared/src/udpfs_core.c:48-80` (`_send_recv`/`_request` return the received byte count; every caller checks only `ret < 0`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:338-353` (open: trusts `reply->handle` from the shared `g_rx_buf`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:484-497` (lseek: trusts `reply->position_*`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:587-607` (getstat: trusts all stat fields)
  - `modules/network/udpfs_shared/src/udpfs_core.c:766-788` (get_sector_count: trusts `reply->size/hisize`)
  - `modules/network/udpfs_shared/src/udpfs_core.c:148-153` (`_path_op`: trusts `reply->result`)
- Problem: `udprdma_recv` returns as soon as a FIN packet completes the transfer, whatever its payload size. A reply shorter than the expected struct leaves the tail of the destination buffer holding the *previous* reply's bytes (all IOMAN replies share the one static `g_rx_buf[312]`), and the caller then acts on the mixture — e.g. `udpfs_core_open` can return a stale server handle from an earlier open, and `get_sector_count` a stale capacity. Distinct from RIPTOPL-044, which covers the variable-length dread *name*; this is the fixed-struct half of the same reply path.
- Fix: In `_request`, fail with `-EIO` when `ret < (int)reply_max` for the fixed-size callers (open/lseek/getstat/path-op replies are exact-size by protocol); dread keeps its own variable-length handling.
- Justification if additive: N/A (one comparison in the shared helper).
- Benefit: Closes stale-handle/stale-stat acceptance on every filesystem operation; makes a truncated or mis-sequenced reply a clean I/O error instead of state confusion.
- Effort: S
- Risk: Low (a conforming server always sends the full struct; the RDMA layer pads to 4 bytes, and struct sizes here are multiples of 4).
- Confidence: High (helper and all call sites read; no minimum-length check exists anywhere on this path).

### UDPFS-04 — `smap_udpbd` write path hardcodes 512-byte sectors and ignores the INFO-reply sector size the read path honours
- Category: 6 (bug)
- Locations:
  - `modules/network/smap_udpbd/src/udpbd.c:230-240` (`pkt.bt.block_count = 4; // 4 x 128b = 512b`, `udp_packet_send_ll(…, buffer, 512)`, `buffer += 512`)
  - `modules/network/smap_udpbd/src/udpbd.c:82-93` (the read path, by contrast, computes `g_read_size = count * g_udpbd.sectorSize`)
  - `modules/network/smap_udpbd/src/udpbd.c:307-316` (`_cmd_info_reply` stores the wire `sectorSize` into `g_udpbd.sectorSize`)
- Problem: If a server reports a sector size other than 512, reads honour it and writes silently send 512 bytes per sector and advance the source pointer by 512 — writing the wrong bytes to the wrong places while reporting success. RIPTOPL-180 covers the missing *validation* of the INFO geometry; this is the separate defect that the write path does not use even a valid, accepted geometry.
- Fix: Drive the write chunking off `g_udpbd.sectorSize` (per-sector send length and pointer advance), the way `_udpbd_read` already does; if non-512 geometries are not to be supported, reject them in `_cmd_info_reply` instead of registering the device.
- Justification if additive: N/A.
- Benefit: Write correctness for any non-512 geometry; removes a silent data-corruption path on a writable block device.
- Effort: S
- Risk: Low (512 remains the behaviour for every 512-reporting server).
- Confidence: High (both paths read side by side at the cited lines).

### UDPFS-05 — `smap_udpbd`'s ministack carries a fully dead ARP cache: `arp_add_entry` has no caller and the RX path never learns
- Category: 1 (dead code)
- Locations:
  - `modules/network/smap_udpbd/src/ministack.c:9-15` (`arp_table[8]`)
  - `modules/network/smap_udpbd/src/ministack.c:120-162` (`arp_add_entry` — grep-verified: no caller anywhere in `smap_udpbd/`)
  - `modules/network/smap_udpbd/src/ministack.c:164-208` (`handle_rx_arp` replies but never calls `arp_add_entry`)
  - `modules/network/smap_udpbd/src/ministack.c:31-53` (`ip_packet_init` sets the Ethernet destination via `eth_packet_init`, i.e. always broadcast — nothing ever consults the table; there is not even an `arp_lookup` in this copy)
  - `modules/network/smap_udpbd/src/ministack.h:235` (the dead declaration)
- Problem: ~45 lines plus an 80-byte table exist solely to be written by a function nothing calls, in a module whose every frame is broadcast anyway (as the long comment inside `arp_add_entry` itself, ministack.c:139-148, already explains). This is the divergent half of the two-ministack duplication (RIPTOPL-243): the udpfs_ministack copy's ARP cache is live and load-bearing; this one is vestigial.
- Fix: Delete `arp_add_entry`, `arp_table` and the header declaration from the smap_udpbd copy.
- Justification if additive: N/A (deletion).
- Benefit: Removes a misleading "almost-fixed" subsystem (its in-code comment narrates a bug fix for a function that cannot execute) from the module a re-vendor will diff against upstream.
- Effort: S
- Risk: Low (no caller exists; the module is self-contained and exports nothing — its exports.tab is empty by design).
- Confidence: High (repo-wide grep for the symbol in the module, plus full read of its only plausible call sites).

### UDPFS-06 — Dead FHI export table and `_start` branch in `udpfs_bd` and `udpfs_ioman` reference symbols that exist nowhere
- Category: 1 (dead code)
- Locations:
  - `modules/network/udpfs_bd/src/exports.tab:1-12` and `modules/network/udpfs_ioman/src/exports.tab:1-12` (byte-identical files, md5 `9c3ae0a998aac0996b65b7f7f5a87104`: an `#ifdef FEATURE_UDPFS_FHI` table exporting `fhi_size`/`fhi_read`/`fhi_write`)
  - `modules/network/udpfs_bd/src/main.c:12-14,38-44` and `modules/network/udpfs_ioman/src/main.c:12-14,38-44` (byte-identical files, md5 `0ebbaea6004625aa3a319af597927c01`: the matching `#ifdef FEATURE_UDPFS_FHI` branch calling `fhi_init()` and registering `_exp_fhi`)
  - Grep-verified: `fhi_size`, `fhi_read`, `fhi_write`, `fhi_init` are defined **nowhere** in the tree; the real FHI module is self-contained (`udpfs_fhi/src/main.c`, `udpfs_fhi/src/exports.tab` exporting library `udpfsfhi`), and no Makefile defines `FEATURE_UDPFS_FHI` for the bd/ioman builds (their Makefiles define only `FEATURE_UDPFS_BD` / `FEATURE_UDPFS_IOMAN`).
- Problem: Two copies of a variant branch that can never be compiled in these modules, naming an export ABI (`fhi`) the project deliberately abandoned (see `udpfs_fhi/ORIGIN.txt`: the library was renamed `udpfsfhi` precisely so it "never answers for Neutrino's `fhi`"). If anyone ever did define the flag for these builds, the link would fail on four undefined symbols.
- Fix: Delete the `#ifdef FEATURE_UDPFS_FHI` block from both exports.tab files and both main.c files (keep the bd/ioman branches, which are live).
- Justification if additive: N/A (deletion).
- Benefit: Two fewer phantom ABIs in the shared template files every variant copies; the FHI story lives in exactly one module.
- Effort: S
- Risk: Low (guarded by a flag no build sets; deletion is whitespace to the shipped IRXs).
- Confidence: High (grep + both Makefiles + the fhi module's own files all read).

### UDPFS-07 — `udpfs_ministack`'s PS2SDK Makefile omits `ministack_xchg.o`, which its own `main.c` calls — the two build manifests disagree
- Category: 7 (build clutter / divergent build descriptions)
- Locations:
  - `modules/network/udpfs_ministack/Makefile` (`IOP_OBJS = exports.o main.o ministack_arp.o ministack_eth.o ministack_ip.o ministack_udp.o udptty.o imports.o` — no `ministack_xchg.o`)
  - `modules/network/udpfs_ministack/src/main.c:12,69` (`mstack_xchg_init()` declared and called)
  - `ps2.yaml:239` (the ps2build manifest *does* list `ministack_xchg.c` — the only one of the eleven modules whose two manifests differ in source set; bd/fhi/ioman/smap/smap_udpbd source lists match theirs)
- Problem: The module is described twice. The manifest that ships (ps2.yaml) includes the `mstack:` exchange device; the PS2SDK Makefile kept beside the sources for re-vendor parity does not, so a Makefile build of this directory fails at link with an undefined `mstack_xchg_init` — or worse, invites a future maintainer to "fix" it by deleting the call and silently dropping the RA menu link from Makefile-built IRXs.
- Fix: Add `ministack_xchg.o` to the Makefile's `IOP_OBJS` (after `ministack_udp.o`), matching ps2.yaml.
- Justification if additive: One object name restoring manifest parity; no code change.
- Benefit: Both build routes produce the same module; the re-vendor instructions in ORIGIN.txt stop pointing at a broken recipe.
- Effort: S
- Risk: Low.
- Confidence: High (both manifests and the call site read directly).

### UDPFS-08 — Both ministack RX dispatchers will call through a NULL handler for a packet addressed to port 0
- Category: 6 (bug — wire-reachable NULL call)
- Locations:
  - `modules/network/udpfs_ministack/src/ministack_udp.c:61-64` (`for i<4: if (dport == udp_ports[i].port_src) return udp_ports[i].handler(...)` — empty slots have `port_src == 0` and `handler == NULL`)
  - `modules/network/smap_udpbd/src/ministack.c:220-223` (same pattern in the monolith copy)
- Problem: Any UDP datagram arriving with destination port 0 matches the first empty port slot and is dispatched through its NULL handler pointer, in SMAP receive context. Port 0 is reserved and no legitimate sender uses it, but the packet is entirely wire-controlled and the ministacks perform no earlier port sanity check.
- Fix: Skip empty slots: `if (udp_ports[i].handler != NULL && dport == udp_ports[i].port_src)` in both copies (in the monolith, compare in its existing raw-order convention).
- Justification if additive: N/A (guard on existing condition).
- Benefit: Removes a one-packet IOP crash from the receive path of both network stacks.
- Effort: S
- Risk: Low.
- Confidence: Medium-High (the dispatch logic is read directly; INFERRED that no earlier layer filters destination port 0 — the SMAP RX path in scope passes all frames to `handle_rx_eth` unfiltered).

### UDPFS-09 — `smap_udpbd` xfer creates an event flag nothing ever waits on, and defines a FIFO-copy helper nothing calls
- Category: 1 (dead code)
- Locations:
  - `modules/network/smap_udpbd/src/xfer.c:12,53-56` (`tx_done_ev` assigned from `CreateEventFlag` in `xfer_init`; grep-verified: no other reference in the module)
  - `modules/network/smap_udpbd/src/xfer.c:80-93` (`CopyFromFIFO` — grep-verified: no caller; this copy's `HandleRxIntr` hands frames to the ministack, which reads the FIFO itself through registers)
- Problem: A kernel event flag is allocated on every load for no consumer, and a ~14-line DMA/PIO copy helper survives from the upstream layout this module no longer uses. Both are pure residue next to the live `tx_sema` in the same file.
- Fix: Delete `tx_done_ev` (declaration, creation block) and `CopyFromFIFO`.
- Justification if additive: N/A (deletion).
- Benefit: One fewer kernel object per load; less upstream residue to re-diff on re-vendor.
- Effort: S
- Risk: Low.
- Confidence: High (grep over the whole module for both symbols).

### UDPFS-10 — `udprdma_inform` (server mode) and the core/transport teardown pair are dead in the shipped variants
- Category: 1 (dead code)
- Locations:
  - `modules/network/udpfs_shared/src/udprdma.c:533-538` (`udprdma_inform` — grep-verified: no caller in any of the eleven directories; all shipped variants are discovery *clients*)
  - `modules/network/udpfs_shared/src/udprdma.c:464-477` (`udprdma_destroy`) and `modules/network/udpfs_shared/src/udpfs_core.c:283-290` (`udpfs_core_exit`) — the only caller of either in scope is the ioman deinit path, `modules/network/udpfs_ioman/src/udpfs_ioman.c:164`; the BD and FHI builds compile both functions in and can never reach them (their modules have no deinit/unload path)
- Problem: Server-mode surface and teardown code ride in every IRX although the product only ever runs the client half. `udprdma_destroy`'s own body concedes the ministack cannot unbind ("UDP socket cleanup not implemented"), so even the one reachable path (ioman deinit) frees an event flag and leaks the port slot by design.
- Fix: Delete `udprdma_inform` outright. Keep the teardown pair only if the ioman deinit path is considered live; otherwise delete all three and the `udpfs_core_exit` declaration in `udpfs_core.h`.
- Justification if additive: N/A (deletion).
- Benefit: Smaller IOP footprint in all three variants (IOP RAM is ~2 MB and every resident IRX counts); removes an uncallable API that a reader must otherwise reason about.
- Effort: S
- Risk: Low for `udprdma_inform`; Medium for the teardown pair (only if some out-of-scope EE path unloads udpfs_ioman — none found in scope).
- Confidence: High for `udprdma_inform` (grep over the full scope); Medium for the teardown pair (out-of-scope unloaders cannot be excluded from this section alone).

### UDPFS-11 — `udpfs_fhi/ORIGIN.txt` states there is no write export; the module exports one and implements it
- Category: 7 (documentation/build drift)
- Locations:
  - `modules/network/udpfs_fhi/ORIGIN.txt` ("No write export: cdvdman only reads the disc image")
  - `modules/network/udpfs_fhi/src/exports.tab` (`[6] DECLARE_EXPORT(udpfs_fhi_write)` — the file's own header comment documents slots [4] read, [5] connected, [6] write)
  - `modules/network/udpfs_fhi/src/udpfs_fhi.c:58-74` (full `udpfs_fhi_write` implementation over `udpfs_core_bwrite`, for read-write VMC handles)
- Problem: The provenance file — the document a re-vendor follows to decide what to keep — denies the existence of a live, exported, implemented write path. Acting on it during a re-vendor would silently drop VMC saving over UDPFS.
- Fix: Correct the ORIGIN.txt sentence to match the export table (write exists for VMC; the disc image path is read-only by handle, not by missing export).
- Justification if additive: N/A (one sentence).
- Benefit: The re-vendor checklist stops being a trap for a working feature.
- Effort: S
- Risk: Low.
- Confidence: High (all three files read).

### UDPFS-12 — RA-under-POPS is shipped with its two load-bearing unknowns still open (half-finished feature)
- Category: 8 (half-finished feature)
- Locations:
  - `modules/network/rapops/rapops.c:111-112` (`TODO: POPS may bring its own DEV9; whether ours then fails to load or replaces it is not known. SMAP needs either one.`)
  - `modules/network/rapops/rapops.c:124-125` (`TODO: no EE event buffer under POPS yet, so no notices in game` — the raudp argument is built with event address 0 at :128)
  - `modules/network/rapull/rapull.c:179-185` (header TODOs: no out-of-band startup/EE-read diagnostic, EE cache-lag unmeasured, snapshot entries not frame-tied)
  - `modules/network/rapull/rapull.c:268-273` (`rp_valid` TODO: PS1 scratchpad reads as zero; where POPS keeps it is not checked — watch entries in 0x200000–0x2003FF silently report 0)
- Problem: The POPS RA path works only if POPS does not bring its own DEV9 (untested either way, by the code's own admission), can never deliver unlock/reset notices (event buffer hard-wired to 0), and silently reports zeros for scratchpad watch entries. These are documented in-tree, so this is a tracking finding, not a discovery: the feature's limits live in comments, not in any user-facing place.
- Fix: No code change proposed here (LESS NOT MORE): surface the three limits in the RA feature's user documentation and close the DEV9 question with one real-hardware POPS test before the path is advertised.
- Justification if additive: N/A.
- Benefit: Prevents a "RA works under POPS" claim the code itself does not support.
- Effort: S (documentation) / M (hardware test — not verifiable in this VM).
- Risk: Low.
- Confidence: High that the gaps exist (they are the authors' own TODOs, read at the cited lines); no claim is made about POPS's actual DEV9 behaviour.

### UDPFS-13 — `rapull`'s `rp_frame_values(read=0)` mode is dead: the parameter and its branches serve a call that never happens
- Category: 1 (dead code)
- Locations:
  - `modules/network/rapull/rapull.c:406` (`rp_frame_values(int read)`)
  - `modules/network/rapull/rapull.c:459-460,472-474` (the `!read` branches zeroing chain addresses)
  - `modules/network/rapull/rapull.c:507-511` (the thread loop — the only call site — always passes `1`)
- Problem: The `read` parameter, its guard in the window-read loop (:413) and the two `!read` branches exist for an initialisation frame that no caller ever requests; the snapshot is only ever published from real reads (correctly — the file's header explains why a synthetic snapshot must never be published, which is exactly why the dead mode can never be re-enabled safely).
- Fix: Drop the parameter and the `!read` branches; keep the always-read path byte-identical.
- Justification if additive: N/A (deletion).
- Benefit: Removes a mode whose reactivation would violate the module's own stated safety rule (no fabricated snapshots).
- Effort: S
- Risk: Low.
- Confidence: High (single call site, read directly).

### UDPFS-14 — INFERRED: SMAP capability check in both neutrino copies is mis-parenthesised (`!x & MASK`), correct only by accident of the mask's value
- Category: 6 (bug — latent)
- Locations:
  - `modules/network/udpfs_smap/src/smap.c:516` (`if (!SPD_REG16(SPD_R_REV_3) & SPD_CAPS_SMAP) return -1;`)
  - `modules/network/smap_udpbd/src/smap.c:522` (identical expression)
  - `modules/network/smap-ingame/smap.c:541` (the third copy has the correct form: `if ((SPD_REG16(SPD_R_REV_3) & SPD_CAPS_SMAP) == 0)`)
- Problem: C precedence makes this `(!rev3) & SPD_CAPS_SMAP`: the result is non-zero only when `rev3 == 0` *and* the mask's bit 0 is set. It therefore behaves like the intended test only if `SPD_CAPS_SMAP` is exactly 1; for any multi-bit or higher-bit mask the check silently never fires (compare RIPTOPL-093, which filed the *REV_1* gate in the smap_udpbd copy as a no-op — this is the adjacent REV_3 expression, present in both neutrino copies). INFERRED: `SPD_CAPS_SMAP` is defined in the PS2SDK `dev9.h`, which is not in this tree, so its value could not be verified from the pin.
- Fix: Rewrite both copies in the smap-ingame form, `(SPD_REG16(SPD_R_REV_3) & SPD_CAPS_SMAP) == 0`.
- Justification if additive: N/A (parenthesisation).
- Benefit: The capability gate means what it says in all three copies, independent of an SDK constant's value.
- Effort: S
- Risk: Low (if the mask is 1, behaviour is unchanged; if it is not, the current behaviour is wrong anyway).
- Confidence: Medium (expression and divergence verified in-tree; the mask value is INFERRED, as marked).

### UDPFS-15 — INFERRED: `udpfs_ministack` registers its RX callback and takes over the tty before the duplicate-load check
- Category: 6 (bug — lifecycle ordering)
- Locations:
  - `modules/network/udpfs_ministack/src/main.c:49` (`smap_register_rx_callback(handle_rx_eth, 44)` — installs a pointer into this module copy in the resident SMAP driver)
  - `modules/network/udpfs_ministack/src/main.c:61` (`udptty_init()` — closes fds 0/1 and re-registers the tty driver)
  - `modules/network/udpfs_ministack/src/main.c:63-66` (only *after* both: `RegisterLibraryEntries` failure returns `MODULE_NO_RESIDENT_END`, freeing this copy)
  - Contrast `modules/network/udpfs_ministack/src/ministack_xchg.c:11,199-203` and `main.c:68-69`, where the same authors deliberately register the `mstack:` device only once resident, "a second load's memory is freed, and `mstack` must not point into it" — the rx-callback registration two statements earlier has exactly the exposure the xchg comment guards against.
- Problem: On a duplicate load, the resident SMAP driver is left holding a receive callback into freed module memory, and the tty has been closed/re-registered by a module that is about to vanish. INFERRED: whether a duplicate load of this IRX can actually occur through RiptOPL's loader paths was not traced from this scope.
- Fix: Move `smap_register_rx_callback` and `udptty_init()` below the `RegisterLibraryEntries` check, matching the ordering the xchg registration already uses in the same function.
- Justification if additive: N/A (reorder only).
- Benefit: Duplicate-load safety consistent with the module's own stated rule.
- Effort: S
- Risk: Low.
- Confidence: Medium (ordering read directly; reachability INFERRED, as marked).

### UDPFS-16 — `smap_udpbd`'s transmit path has no link-up wait and no failure exit: `smap_transmit` retries the TX ring forever
- Category: 5/6 (performance/robustness — structural)
- Locations:
  - `modules/network/smap_udpbd/src/xfer.c:201-217` (`while (HandleTxReqs(...) < 0) DelayThread(100);` — unbounded, and the function then returns 0 unconditionally)
  - Contrast `modules/network/udpfs_smap/src/xfer.c:253-279` (the sibling copy waits up to 2 s for `SmapIsInitialized` and returns -1 if the link never comes up — a local fix its ORIGIN.txt records; the monolith copy never got it)
- Problem: In the monolith, any caller transmitting before the driver is initialised, or after the TX ring wedges, spins at 100 µs intervals forever while holding `tx_sema`, and success is reported regardless. This is the same divergence class as UDPFS-05: local hardening applied to one SMAP copy (RIPTOPL-127 family) and not the other.
- Fix: Port the sibling's bounded initialisation wait + -1 return into this copy's `smap_transmit`; leave the established blocking-while-healthy contract intact, as the sibling does.
- Justification if additive: N/A (port of existing in-tree code).
- Benefit: A wedged or not-yet-up adapter surfaces as an error to udpbd instead of a silent infinite spin holding the TX semaphore.
- Effort: S
- Risk: Low-Medium (touches the monolith's only TX entry; behaviour while healthy is unchanged).
- Confidence: High (both copies read in full; the sibling's fix and its rationale are documented in `udpfs_smap/ORIGIN.txt`).

### UDPFS-17 — `udpfs_core_close` discards the close reply entirely and always reports success
- Category: 6 (bug — minor)
- Locations: `modules/network/udpfs_shared/src/udpfs_core.c:359-382` (the reply buffer is filled by `_request` and never inspected — no `msg_type` check, no `result` check; `return 0` unconditional)
- Problem: Every other core operation validates its reply type; close alone cannot distinguish success, a server error, or a mis-sequenced packet. The ioman wrapper frees the local fd regardless (`udpfs_ioman.c:214-218`), so a failed server-side close leaks the server handle invisibly until the peer times it out (per `udpfs_fhi/ORIGIN.txt`, peers time out after 1 h of silence by default).
- Fix: Check `reply->msg_type == UDPFS_MSG_CLOSE_REPLY` and return `reply->result` when negative, matching the pattern used by the neighbouring operations.
- Justification if additive: N/A.
- Benefit: Server handle leaks become visible errors instead of hour-long ghosts.
- Effort: S
- Risk: Low.
- Confidence: High.

### UDPFS-18 — `raudp`'s discovery-elapsed counter `ra_disc_us` is written, overflow-guarded, and never read
- Category: 1 (dead code)
- Locations:
  - `modules/network/raudp/raudp.c:770-771` ("Discovery elapsed time (retained for bounded diagnostic accounting)")
  - `modules/network/raudp/raudp.c:875,924-925` (the only writes; grep-verified: no read, no packet field, no log uses it — the RA15 header fields are built from other counters at :661-666)
- Problem: A counter maintained across the unbounded discovery loop, with its own saturation guard, feeds no diagnostic that exists. The comment claims a purpose the code does not fulfil.
- Fix: Delete the counter and its two accumulation sites (the discovery cadence is driven by `tries`, not by this value — verified at :863-927).
- Justification if additive: N/A (deletion).
- Benefit: Removes misleading "accounting" from the discovery loop of an in-game module where every resident byte and branch is paid during gameplay.
- Effort: S
- Risk: Low.
- Confidence: High (grep + full read of `ra_discover`).

### UDPFS-19 — `mstack:` exchange silently truncates an over-long reply, indistinguishable from a complete one
- Category: 6 (bug — minor)
- Locations:
  - `modules/network/udpfs_ministack/src/ministack_xchg.c:68-70` (`len = udplen - sizeof(udp_header_t); if (len > MSTACK_XCHG_REPLY_MAX) len = MSTACK_XCHG_REPLY_MAX;`)
  - `modules/network/udpfs_ministack/src/ministack_xchg.c:116-120` (the truncated length is returned as the reply length with no flag)
- Problem: The length validation immediately above (:63-67) is careful — UDP length is checked against the IP length before any FIFO read — but a reply longer than the 1016-byte cap is then *cut* rather than rejected, and the EE caller receives a syntactically valid, silently incomplete RA response. For a text request/reply protocol (RAP1/RAQ1 family), a truncated tail is worse than an error: it parses as a shorter answer.
- Fix: Reject instead of clamp: `if (len > MSTACK_XCHG_REPLY_MAX) return -1;` in the handler (the frame is dropped and the exchange times out, which the EE already handles).
- Justification if additive: N/A.
- Benefit: Oversized replies fail loudly through the existing timeout path instead of corrupting RA state with partial data.
- Effort: S
- Risk: Low (any reply over the cap is already unusable whole).
- Confidence: High (handler and caller read in full; the reply struct's fixed 1024-byte size is at `mstack_xchg.h:32-38`).

---

## B. Duplication census (byte-level, md5sum at the pin)

| Family | Copies | Verdict |
|---|---|---|
| `udpfs_core.c` ×4 | `udpfs_bd/src`, `udpfs_fhi/src`, `udpfs_ioman/src` — all md5 `19773c8121f01706cbb1dbb226a0a4f1`, **2 lines each**: a comment + `#include "../../udpfs_shared/src/udpfs_core.c"`. Canonical body: `udpfs_shared/src/udpfs_core.c`, 791 lines, md5 `22c485e6974f0925db33efaee7d047a2` | **De-duplicated by construction.** The three "copies" are identical shims; there is exactly one implementation. (RIPTOPL-008's triplication is resolved for the `.c` files; the headers below are not.) |
| `udprdma.c` ×4 | Same layout: three 2-line shims, md5 `74c6e70cde8daf09b8d20418525e9acf`; canonical `udpfs_shared/src/udprdma.c`, 738 lines, md5 `3dbc9be2b27af7abbdea33ff652e735f` | **Identical shims + one canonical body**, as above. |
| Headers ×3 (`udpfs_bd`/`udpfs_fhi`/`udpfs_ioman` `src/`) | `udpfs_core.h` (81 ln) md5 `527004edde3e2729980f099720df9a54` ×3; `udpfs_packet.h` (262 ln) `312ede37c189dbd147ff463752aff08b` ×3; `udprdma.h` (152 ln) `9aa456793757e0f56207f702dd23b457` ×3; `udprdma_packet.h` (135 ln) `e33faf90889f96c3f2cebea58155d78e` ×3; `udprdma_seq.h` (14 ln) `9d91cfe448ed5e8f820b28a35517f242` ×3 | **Byte-identical in all three variants** (785 lines stored three times). `udpfs_fhi/ORIGIN.txt` states a CI script (`test_udpfs_shared_transport.py`) checks this identity — the triplication is guarded, not accidental, but it is still three stored copies of every header. |
| `include/mprintf.h` ×5 | `udpfs_bd`, `udpfs_fhi`, `udpfs_ioman`, `udpfs_ministack`, `udpfs_smap` — all md5 `8724e63cccd71708f4dfe106113c08ec` (23 ln). A sixth copy lives in `smap_udpbd/src/include/mprintf.h`, md5 `5b00e5f08d52e74031277ce7d3f32053` | Five identical; the smap_udpbd copy **diverged** (different hash, same size). |
| `include/main.h` (udpfs variants) | bd md5 `4a3010d313a5483168207172fbe1c8a3`, fhi `f38b484d96fc82130d1a1d71632d28bb`, ioman `962fb820282cfc60f6c11db5c0e1bc26` — all 10 lines | Diverged **only** by the `MODNAME` string (UDPFS / UDPFS_FHI / UDPFS_IO), as intended. |
| Variant `main.c` | bd and ioman byte-identical, md5 `0ebbaea6004625aa3a319af597927c01` (47 ln, multi-variant `#ifdef` template — see UDPFS-06); fhi has its own, md5 `abd8239975697cd47fc3ce2e39dc94d9` (29 ln) | Two identical + one genuinely separate. |
| Variant `exports.tab` | bd and ioman byte-identical, md5 `9c3ae0a998aac0996b65b7f7f5a87104`; fhi its own (library `udpfsfhi`) | As main.c. |
| `udptty.c` ×2 in scope (task brief said ×3; only two copies exist in these eleven directories) | `udpfs_ministack/src/udptty.c` md5 `5a52c09ea35c4da84a3fd33fd82f5b3f`; `smap_udpbd/src/udptty.c` md5 `91f4d844b2eccc2d5066e43c22794547` — `diff` = **exactly one line**: the include (`ministack_udp.h` vs `ministack.h`), line 3. `udptty.h` ×2 byte-identical, md5 `d0786ac979bb89018832d03ab038de08` | **Near-identical (88/89 lines shared).** Both copies' `ttyWrite` **returns `size`** — RIPTOPL-218's always-returns-0 copy is a third copy outside this scope, not either of these. |
| `smap.c` ×3 | `udpfs_smap/src/smap.c` 639 ln md5 `b2eeed040864ecf2f470bb9e918b049e`; `smap_udpbd/src/smap.c` 641 ln md5 `004fc4d37d480e3cd479ecfb06b56a8a` — **25 diff lines** (smap_udpbd adds `udpbd.h`/`udptty.h` includes, calls `udptty_init()`/`udpbd_init()` from its interrupt thread, and lacks the `g_dma_max_block_size` REV_1 gate); `smap-ingame/smap.c` 651 ln md5 `02889bfdbc281ee4131f4fbb3205e82e` — **536 diff lines** vs udpfs_smap | Two near-identical neutrino copies + one substantially diverged lwIP-era copy (different interrupt model: ingame handles RXDNV/TXDNV; neutrino copies mask them out). Family filed as RIPTOPL-127; the per-copy divergences above are the new detail. |
| `xfer.c` ×3 | `udpfs_smap` 279 ln md5 `2154cb5e3d4fd95f88d14a77dec07eb8` (callback + `smap_fifo_read` model); `smap_udpbd` 217 ln md5 `940e245b45c1b44b4c05f89b805cd48e` (frame-pointer model, no fifo_read); `smap-ingame` 339 ln md5 `c78fb2fc7faa8fa7a1756a703ccb43ed` (lwIP pbuf model + RA notice tap) | **All three diverged** — same ancestor, three different receive architectures. Not interchangeable. |
| ministack ×2 | `udpfs_ministack/` (split: arp 211 + eth 28 + ip 91 + udp 68 + xchg 203 ln) vs `smap_udpbd/src/ministack.c` (275 ln monolith) + `ministack.h` (240 ln) | Same lineage, **structurally diverged** (family filed as RIPTOPL-243). Material divergences found this pass: (a) ARP — split copy learns + announces + detects conflicts, monolith's cache is dead (UDPFS-05); (b) TX port byte order — split copy `htons(socket->port_src)` (`ministack_udp.c:46`), monolith stores/compares raw (`ministack.c:113,221`) — each self-consistent with its own RX compare, opposite conventions; (c) only the split copy has `ministack_xchg.c` (the `mstack:` device); (d) only the split copy validates nothing about RX IP destination, while the monolith validates nothing either — both dispatch purely on UDP port. |
| `parse_ip()` ×2 | `udpfs_ministack/src/main.c:18-42` and `smap_udpbd/src/main.c:526-550` | **Textually identical** 25-line function in two modules (the smap_udpbd monolith needs it because its ministack is internal). |
| SMAP `SmapDriverData` struct / `main.h` | `udpfs_smap/include/main.h` (MODNAME "SMAP") vs `smap_udpbd/src/include/main.h` (MODNAME "SMAP_driver") — same 41-line struct definition | Diverged by module name only; the struct is stored twice. |

Census summary: the UDPFS transport itself is now honestly single-sourced (3 identical 2-line shims + 1 canonical body per file); the remaining true triplication is the **785 lines of protocol headers ×3** (guarded by a CI identity check) and the **SMAP driver family** (2 near-identical copies + 1 diverged ancestor, ~1,931 lines total).

---

## C. Checked-clean / dissolved

- **UDPRDMA IST data path clamping** (`udprdma.c:203-213`): `data_byte_count` *is* clamped to the remaining rx-buffer space before the FIFO read — the data half of the IST is bounded; only the app-header half is not (UDPFS-01).
- **`_cmd_read_rdma` wire geometry** (`smap_udpbd/src/udpbd.c:318-383`): `block_shift` is a 4-bit field (max 15, `udpbd.h:93`), but the derived `size` is checked against `RDMA_MAX_PAYLOAD` (:344) and against `g_read_size` (:357, the RIPTOPL-180-adjacent local hardening) before the DMA at :374, and the DMA block parameters are arithmetically consistent with the validated `size`. No overflow found beyond what -180 already files.
- **UDPBD write-done error swallowing** (`udpbd.c:281-288`): returning `count` for a server-refused write is deliberate, commented, and matches the cited upstream behaviour (FatFs livelock avoidance). Not a defect on the evidence in-tree.
- **`mstack_xchg` RX length validation** (`ministack_xchg.c:62-67`): UDP length is checked against IP length before the FIFO read — the careful pattern UDPFS-01/03 are missing elsewhere. (Its truncation choice is separately filed as UDPFS-19.)
- **`raudp` PC-message authentication** (`raudp.c:951-954,1073,1134,1217`): all three ingress roads (lwIP poll, raw RX drain, SMAP notice) gate on `ra_from_pc` before `ra_handle_pc` can raise a reset/unlock event; the discovery reply additionally requires `"RAO1 OK "` (`raudp.c:902`) — the IOP-side twin of the parse filed EE-side as RIPTOPL-252 is *stricter* here, not a duplicate defect.
- **`raudp` snapshot tear detection** (`raudp.c:673-699`): seq/trailer double-check around the staged copy, byte count clamped to `RA_SNAP_MAX_BYTES`; `ra_game_id` copy in `_start` is bounded by `RA_ARG_ID_MAX` = 15 (`modules/network/common/ra_snap.h:120`) into a 16-byte buffer — fits.
- **`rapops` blob bounds** (`rapops.c:27-44`): offset/size/alignment all validated against `RAPOPS_BLOB_BYTES` before `LoadModuleBuffer`; the `start_rapull` argument buffer (61 bytes) exactly fits its worst case (45 + 15 + NUL).
- **`rapull` watch-window construction** (`rapull.c:286-385`): bitmap/window caps fail closed (`rp_setup` returns 0, thread frees and exits); entry offsets use binary search over ordered windows; out-of-range entries publish 0 via `RP_NONE`, never stale stage bytes.
- **Blocked ARP resolution on the send path**: `udpfs_ministack`'s `arp_resolve` (5 × 100 ms) is reached from `ip_packet_init`, but UDPRDMA pre-builds its packet headers at create/discover time, so the block is bounded to connection setup, not per-packet. Dissolved.
- **`smap_transmit` unbounded TX retry in `udpfs_smap`** (`xfer.c:267-278`): the blocking-while-healthy contract is explicitly documented in `udpfs_smap/ORIGIN.txt` and the copy *does* have the link-up guard — dissolved for this copy; the unguarded smap_udpbd copy is UDPFS-16.
- **`udpfs_core_write` chunk arithmetic** (`udpfs_core.c:164-237`): `total_chunks` is `uint16_t` and would wrap above ~91 MB in one call, but every in-scope caller passes iomanX/BDM sizes orders of magnitude smaller, and WRITE_DATA's `chunk_size` field is itself 16-bit by protocol. Not filed; noted here as the boundary of what was checked.
- **FHI handle lifetime across IOP reset** (`udpfs_fhi.c:1-15` + ORIGIN): relies on server-side `ResetPeer` handle retention — that is the filed RIPTOPL-081 concern on the ioman side; the FHI half adds no new mechanism.
- **`udpfs_ministack` Makefile export order / `udpfs_smap` Makefile obj-dir rules**: verbose but load-bearing workarounds for ps2dev iopfixup, documented in both Makefiles and ORIGIN.txt files. Not clutter.

---

## D. Per-file ledger (every file in scope, lines read)

| Directory | Files and lines |
|---|---|
| udpfs_bd (1,074) | Makefile 19, ORIGIN.txt 38, include/main.h 10, include/mprintf.h 23, src/exports.tab 12, src/imports.lst 66, src/irx_imports.h 18, src/main.c 47, src/udpfs_bd.c 193, src/udpfs_core.c 2, src/udpfs_core.h 81, src/udpfs_packet.h 262, src/udprdma.c 2, src/udprdma.h 152, src/udprdma_packet.h 135, src/udprdma_seq.h 14 |
| udpfs_fhi (994) | Makefile 19, ORIGIN.txt 24, include/main.h 10, include/mprintf.h 23, src/exports.tab 13, src/imports.lst 67, src/irx_imports.h 18, src/main.c 29, src/udpfs_core.c 2, src/udpfs_core.h 81, src/udpfs_fhi.c 143, src/udpfs_packet.h 262, src/udprdma.c 2, src/udprdma.h 152, src/udprdma_packet.h 135, src/udprdma_seq.h 14 |
| udpfs_ioman (1,435) | Makefile 19, ORIGIN.txt 48, include/main.h 10, include/mprintf.h 23, src/exports.tab 12, src/imports.lst 69, src/irx_imports.h 18, src/main.c 47, src/udpfs_core.c 2, src/udpfs_core.h 81, src/udpfs_ioman.c 510, src/udpfs_packet.h 262, src/udprdma.c 2, src/udprdma.h 152, src/udprdma_packet.h 135, src/udprdma_seq.h 14, tests/test_ack_sequence.c 31 |
| udpfs_ministack (1,219) | Makefile 26, ORIGIN.txt 26, include/main.h 7, include/ministack_arp.h 37, include/ministack_eth.h 68, include/ministack_ip.h 77, include/ministack_udp.h 64, include/mprintf.h 23, include/mstack.h 17, include/mstack_xchg.h 42, include/udptty.h 6, src/exports.tab 13, src/imports.lst 36, src/irx_imports.h 15, src/main.c 72, src/ministack_arp.c 211, src/ministack_eth.c 28, src/ministack_ip.c 91, src/ministack_udp.c 68, src/ministack_xchg.c 203, src/udptty.c 89 |
| udpfs_shared (2,621) | README.md 30, src/udpfs_core.c 791, src/udprdma.c 738, tests/test_ministack_announce.c 107, tests/test_ministack_conflict.c 94, tests/test_ministack_xchg.c 304, tests/test_udprdma_session.c 446, tests/stubs/iomanX.h 62, tests/stubs/irx.h 4, tests/stubs/thbase.h 18, tests/stubs/thevent.h 27 |
| udpfs_smap (1,220) | Makefile 26, ORIGIN.txt 26, include/irx_imports.h 16, include/main.h 41, include/mprintf.h 23, include/smap.h 41, include/xfer.h 41, src/exports.tab 13, src/imports.lst 49, src/main.c 26, src/smap.c 639, src/xfer.c 279 |
| smap-ingame (1,358) | Makefile 18, exports.tab 10, imports.lst 62, irx_imports.h 21, main.c 218, main.h 34, smap.c 651, xfer.c 339, xfer.h 5 |
| smap_udpbd (2,428) | Makefile 15, ORIGIN.txt 45, README.md 9, src/exports.tab 7, src/imports.lst 66, src/main.c 85, src/ministack.c 275, src/ministack.h 240, src/smap.c 641, src/udpbd.c 507, src/udpbd.h 123, src/udptty.c 89, src/udptty.h 6, src/xfer.c 217, src/include/irx_imports.h 18, src/include/main.h 41, src/include/mprintf.h 23, src/include/xfer.h 21 |
| rapops (179) | imports.lst 12, irx_imports.h 11, rapops.c 156 |
| rapull (436) | imports.lst 16, irx_imports.h 11, rapull.c 409 |
| raudp (1,378) | Makefile 8, exports.tab 9, imports.lst 34, irx_imports.h 19, raudp.c 1,308 |
| **Total** | **14,342 lines across 149 files** |

**Unread: none (zero).** Every file in the eleven directories was read in full, including tests, stubs, ORIGIN/README prose, export/import tables and Makefiles. Two out-of-scope files were consulted only to verify bounds cited above: `modules/network/common/ra_snap.h` (RA_ARG_ID_MAX) and `ps2.yaml` (build-manifest comparison for UDPFS-07).
