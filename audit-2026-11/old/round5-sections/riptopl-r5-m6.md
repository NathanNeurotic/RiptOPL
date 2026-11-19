# RiptOPL — Round 5, Section R5-M6: pademu remainder + iopcore misc + debug + ds34 EE libs + udpfs tests

- **Repo / pin:** `~/workspace/audit-clones-2026-11-13/RiptOPL` at `2e347d5ac885cd0ff73e9ad4143781946396f492`. Read-only; `git status --porcelain` empty on completion.
- **Brief discrepancy (recorded, not blocking):** the assignment said "25 files, 6,250 lines", but its own enumerated list contains **27 paths totalling 5,268 lines** (`wc -l`). All 27 were read 1..EOF.
- **Prior-art check:** every candidate was grepped against `Sable-RiptOPL.md`, `-ROUND2.md`, `-ROUND3.md`, `-ROUND4.md` and `round4-sections/*.md` (RIPTOPL-001…144) before filing. Round 4 §2a read `modules/pademu/pademu.c` and `modules/pademu/ds34common.c` as *context only* (its own header says so) and assigned them no finding IDs; findings below in those files are new mechanisms, not re-files.
- **Severity weighting:** `modules/debug/ps2link/*` and `modules/debug/udptty-ingame/*` are debug-only modules, never packed into a normal game launch. Their findings are real defects but their reachability is a developer's own LAN/debug session; each says so.

## Coverage ledger (exact, `wc -l` at the pin)

| File | Lines |
|---|---|
| modules/pademu/pademu.c | 589 |
| modules/pademu/padmacro.c | 256 |
| modules/pademu/ds34common.c | 144 |
| modules/pademu/sys_utils.c | 99 |
| modules/iopcore/igrmbox/igrmbox.c | 188 |
| modules/iopcore/patches/apemodpatch/main.c | 128 |
| modules/iopcore/patches/iremsndpatch/main.c | 118 |
| modules/iopcore/imgdrv/imgdrv.c | 98 |
| modules/iopcore/resetspu/resetspu.c | 96 |
| modules/bdmevent/main.c | 88 |
| modules/iopcore/freezediag/freezediag.c | 81 |
| modules/iopcore/patches/f2techioppatch/main.c | 60 |
| modules/iopcore/patches/cleareffects/main.c | 30 |
| modules/debug/ps2link/net_fio.c | 728 |
| modules/debug/ps2link/cmdHandler.c | 395 |
| modules/debug/ps2link/net_fsys.c | 337 |
| modules/debug/ps2link/excepHandler.c | 115 |
| modules/debug/ps2link/nprintf.c | 81 |
| modules/debug/ps2link/ps2link.c | 48 |
| modules/debug/udptty-ingame/udptty.c | 273 |
| modules/ds34bt/ee/libds34bt.c | 181 |
| modules/ds34usb/ee/libds34usb.c | 153 |
| modules/network/udpfs_shared/tests/test_udprdma_session.c | 446 |
| modules/network/udpfs_shared/tests/test_ministack_xchg.c | 304 |
| modules/network/udpfs_shared/tests/test_ministack_announce.c | 107 |
| modules/network/udpfs_shared/tests/test_ministack_conflict.c | 94 |
| modules/network/udpfs_ioman/tests/test_ack_sequence.c | 31 |
| **Total** | **5,268 (27 files)** |

Supporting files read only to verify/dissolve mechanisms (not audited as targets): `modules/pademu/pademu.h`, `include/ds34common.h`, `modules/debug/ps2link/hostlink.h`, `modules/ds34bt/ee/libds34bt.h`, `ee_core/src/freeze_diag_format.c` (caps/NULL contract), `ee_core/src/igrmbox.c:46-59` (arg generation), `modules/bdm/src/bdm.c:19,143-152` (`MAX_CONNECTIONS`, `bdm_get_bd`), `.github/scripts/test_udpfs_transport.py` (test build flags), caller ranges of `padMacroPerform` in `modules/pademu/ds34bt.c` / `ds34usb.c`.

---

## New candidate findings

### R5-M6-01 / ps2link exception handler overruns its own buffer and can `strlen(NULL)` — in the crash handler itself
- **Category:** 6 (bug/security)
- **Locations:** `modules/debug/ps2link/excepHandler.c:12` (`BUFFER_SIZE` definition), `:14` (`excep_buffer`), `:71-95` (`excep_handler2`; `strlen` at `:80`, `memcpy` at `:86`, send at `:94`)
- **Problem:** `BUFFER_SIZE = sizeof(exception_frame_t) + 136`, and `excep_buffer` holds exactly that many bytes. `excep_handler2` then writes: 4 bytes of magic, `memcpy(buffer, frame, BUFFER_SIZE)` — which both **over-reads** `frame` by 136 bytes (the source object is only `sizeof(exception_frame_t)`) and writes `BUFFER_SIZE` bytes starting at offset 4 — then 8 more bytes (`r_epc`, `len`), then `len + 1` bytes of module name. Total written is `sizeof(exception_frame_t) + 149 + len` bytes into a `sizeof(exception_frame_t) + 136`-byte static: an overrun of at least 13 bytes plus the module-name length, into whatever statics follow in BSS, executed precisely when the IOP has already faulted. Independently, `ExceptionGetModuleName` returns `0` when the faulting EPC belongs to no listed module (BIOS ROM, freed module, data-as-code), and `:80` calls `strlen(module_name)` on it unconditionally — a second fault inside the exception handler. Debug-only reachability (ps2link), but this is the component whose only job is to survive a crash.
- **Fix:** Size the copy to `sizeof(exception_frame_t)`, enlarge the buffer by the header/trailer/name space the format actually uses, and treat a NULL module name as an empty string. Justification for added code: the two guards are the fix itself; there is no smaller correct shape.
- **Benefit:** The IOP exception reporter stops corrupting memory and re-faulting on exactly the crashes it exists to report.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — the arithmetic closes from the file alone, for any `sizeof(exception_frame_t)`)

### R5-M6-02 / ps2link `pkoSendSifCmd` copies up to 4 bytes past `rpc_data[1024]` on a full-size command packet
- **Category:** 6
- **Locations:** `modules/debug/ps2link/cmdHandler.c:41` (`rpc_data[1024/4]`), `:137-153` (`pkoSendSifCmd`; `memcpy` at `:143-144`, length clamp at `:146`); packet source `cmdListener` at `:235-240` (`recvfrom` into a 1,024-byte `recvbuf`)
- **Problem:** The clamp is `len > sizeof(rpc_data) ? sizeof(rpc_data) : len` — i.e. up to 1,024 bytes — but the copy destination is `&rpc_data[1]`, offset 4 into the 1,024-byte array. Any command packet with `len ≥ 1021` (a 1,024-byte UDP datagram is the maximum `cmdListener` can deliver, and is all it takes) writes 1–4 bytes past the array, and the final bytes of the source never reach the EE because the DMA length stays `len`. Every EE-side command (EXECEE, WRITEMEM, DUMPMEM, RESET…) funnels through this one function. Debug-only reachability.
- **Fix:** Clamp the copy and the DMA length to `sizeof(rpc_data) - 4` (the space actually remaining after the command word). Justification: one constant change in the existing clamp, no new code paths.
- **Benefit:** Removes a static-buffer overwrite on a network-supplied length and stops silent truncation of the longest commands.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M6-03 / ps2link `pkoExecIop` walks argument strings with a network-supplied `argc` and no bound
- **Category:** 6
- **Locations:** `modules/debug/ps2link/cmdHandler.c:51-115` (`args` at `:74`, `argc = ntohl(...)` at `:75`, walk at `:78-81`, `LoadStartModule` at `:83`)
- **Problem:** `argc` is taken from the UDP packet as a full `u32` and never checked against the packet. The loop `for (i = 0; i < (argc - 1); i++) arglen += strlen(&args[arglen]) + 1;` therefore walks NUL-hunting past the 256-byte `argv` field and past the 1,024-byte `recvbuf` into adjacent memory for any large `argc` — and for `argc == 0`, `argc - 1` is `UINT_MAX`, so it walks essentially forever. The resulting `arglen` is handed to `LoadStartModule` as the argument block length. The packet-length check at the top validates only the header struct size, not the string area the loop consumes. Debug-only reachability, but the walk is unbounded in both count and bytes.
- **Fix:** Bound the loop by the bytes actually remaining in the received packet (stop when `arglen` reaches the end of `argv`/packet, and reject `argc == 0` / implausibly large `argc` up front). Justification: the bound check is the minimum that makes the existing parse safe; no restructure.
- **Benefit:** A malformed EXECIOP packet returns an error instead of reading arbitrary IOP memory and feeding a garbage length to the module loader.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M6-04 / ps2link `pko_read_file` trusts the peer's `nbytes` over the caller's buffer size
- **Category:** 6
- **Locations:** `modules/debug/ps2link/net_fio.c:363-412` (`nbytes = ntohl(readrly->nbytes)` at `:402`, `pko_recv_bytes(..., &buf[0], nbytes)` at `:408`); caller `modules/debug/ps2link/net_fsys.c:139-156` (`fsysRead` passes the ioman caller's `buf`/`size`)
- **Problem:** `length` (the caller's buffer capacity) is sent in the request but never compared with the reply's `nbytes`. A peer that answers with `nbytes > length` — a buggy or hostile ps2link host, or a desynchronised stream where the "reply" is actually file data — makes `pko_recv_bytes` write that many bytes into a buffer sized for `length`. Every other reply field in this file is treated as untrusted enough to byte-swap and log; this one is used as a write length unchecked. Debug-only reachability (the peer is normally the developer's own PC).
- **Fix:** Reject (or clamp-and-drain) a reply whose `nbytes` exceeds the requested `length` before receiving the payload. Justification: one comparison against a value already in scope.
- **Benefit:** The host: filesystem cannot be turned into a heap/stack overwrite of the reading program by a single corrupt reply packet.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M6-05 / ps2link `pko_recv_bytes` spins forever when the peer closes the connection mid-packet
- **Category:** 5 (performance/hang), with a Cat 6 framing footnote
- **Locations:** `modules/debug/ps2link/net_fio.c:86-105` (`pko_recv_bytes`; `recv` at `:94`); framing caller `:107-160` (`pko_accept_pkt`, `hlen` test at `:137`, payload length expression at `:146`)
- **Problem:** The receive loop handles `len < 0` (error) but not `len == 0` — the orderly-shutdown return. On a closed connection `left` never decreases and the loop calls `recv` forever at full speed, wedging the fileio thread (and, because `net_fsys.c` holds `fsys_sema` across every `pko_*` call, every subsequent host: access). A developer restarting the PC-side ps2link host mid-transfer is the ordinary trigger. Footnote: `pko_accept_pkt` also never rejects `hlen < sizeof(pko_pkt_hdr)`; the payload length `hlen - sizeof(...)` only fails safe today by unsigned-wrap-to-negative accident on this target, not by an explicit check.
- **Fix:** Treat a zero-length `recv` as end-of-stream and return an error; add the explicit `hlen < sizeof(pko_pkt_hdr)` rejection beside the existing `hlen > len` one. Justification: two comparisons in the framing path, no new machinery.
- **Benefit:** Host disconnects become a recoverable fileio error instead of a permanent IOP-side spin that also blocks the filesystem semaphore.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M6-06 / ps2link naplink handler uses the RPC payload as a `printf` format string
- **Category:** 6
- **Locations:** `modules/debug/ps2link/nprintf.c:23-35` (`printf(buffer)` at `:28`)
- **Problem:** `naplinkRpcHandler` ignores its `size` parameter and passes the EE-supplied RPC buffer directly as the format argument. Any `%` sequence in a printed string is interpreted (reading varargs that were never passed), and the buffer is not guaranteed NUL-terminated within the 512-byte RPC buffer, so the print can also run past it. The payload here is the developer's own EE code (debug-only), which caps severity — but the handler exists to print arbitrary game/homebrew strings, i.e. exactly the untrusted-with-respect-to-formatting kind of text.
- **Fix:** Print with a constant `"%s"` format (and bound the length by `size`). Justification: one-line change at the single call site.
- **Benefit:** Naplink console output can no longer crash or misread the stack because a logged string contained a percent sign.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M6-07 / udptty's `tty_write` always returns 0 — the write contract is broken and send failures are invisible
- **Category:** 6 (logic)
- **Locations:** `modules/debug/udptty-ingame/udptty.c:225-236` (`udp_send`: `lwip_sendto` result discarded at `:233`, unconditional `return 0` at `:235`), `:259-268` (`tty_write` returns `udp_send`'s result)
- **Problem:** Every layer agrees to lie: `lwip_sendto`'s return is dropped, `udp_send` returns `0` on success and on failure alike, and `tty_write` — an ioman `write` operation, whose contract is to return the byte count — therefore reports **0 bytes written for every write**. Callers that honour the return value (stdio retry-on-short-write loops, the KPRTTY thread's `write(1, …)` at `:148`, any game runtime using the tty device) see permanent zero-progress, and a dead network is indistinguishable from a working one.
- **Fix:** Return `size` from `udp_send` when `lwip_sendto` reports success (and a negative error when it doesn't), so `tty_write` reports the bytes actually handed to the socket.
- **Benefit:** The debug console obeys the ioman contract; short-write handling in stdio works instead of spinning or dropping output, and network failure becomes visible.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M6-08 / igrmbox `mbSendEE` busy-waits on SIF DMA with no timeout, on the only mailbox thread
- **Category:** 5 (busy-wait / hang)
- **Locations:** `modules/iopcore/igrmbox/igrmbox.c:72-88` (`mbSendEE`; retry loop ending `:85`, status spin `:86-87`); sole caller thread `mbThread` at `:133-150`
- **Problem:** `mbSendEE` retries `sceSifSetDma` in a `do…while (!id)` loop and then spins `while (sceSifDmaStat(id) >= 0);` — both unbounded. This runs on `mbThread`, the module's only thread, and the mailbox exists specifically for use while a game is paused/frozen — i.e. precisely when the EE/SIF side may be unresponsive. If a DMA never completes, the thread never returns: no WAKE is ever processed (sound stays hushed — `mbHush` at `:35-55` has set all voice pitches and master volumes to 0), no END frees the buffer, and the in-game menu is dead until reset. The EE side of this same mailbox was already noted in round 3 as deliberately bounded (3 s, with a documented late-answer contract); the IOP side has no bound at all.
- **Fix:** Give both waits the same bounded shape the EE side already uses (a retry/time budget after which `mbSendEE` gives up and `mbThread` continues its loop), so a wedged DMA degrades to a dropped ack instead of a dead mailbox. Justification: the bound is the fix; it mirrors an in-tree contract rather than inventing one.
- **Benefit:** A frozen EE can no longer permanently silence audio and kill the in-game menu's IOP half with one stuck DMA.
- **Effort:** S · **Risk:** Low · **Confidence:** Mechanism High (READ); that a DMA can wedge in the frozen-game scenario is INFERRED from the module's stated purpose

### R5-M6-09 / padmacro keeps its edge-detection state in one global shared by all pads, so two-pad use flickers toggles
- **Category:** 6 (logic)
- **Locations:** `modules/pademu/padmacro.c:47-67` (globals `gPreviousButtons`, `gTurboButton*`, slowdown toggles), `:139-205` (`changeInternalState`; slowdown toggle flips at `:162-180`), `:234-256` (`padMacroPerform`); per-pad callers (context) `modules/pademu/ds34bt.c:1234,1274` and `modules/pademu/ds34usb.c:302-354`, each invoking `padMacroPerform` once per pad per poll
- **Problem:** The macro *settings* are global by design (one config from `_start`), but the *runtime edge state* is global too, while reports arrive interleaved per pad. `gPreviousButtons` therefore usually holds the **other** pad's buttons. In toggle mode the edge test (`previous & mask) != (buttons & mask)`, flip if now pressed) misfires continuously: with pad 1 holding its slowdown button and pad 2 idle, the interleave is *held, 0, held, 0…*, so every pad-1 poll looks like a fresh press and the toggle flips at poll rate — slowdown strobes instead of latching. The turbo-arming state machine (`gTurboButtonWaiting`, fed by whichever pad polls next) has the same cross-pad bleed. Single-pad use — presumably how it was tested — is unaffected, which is the shape that hides it.
- **Fix:** Make the runtime state (`gPreviousButtons`, slowdown toggle states, turbo waiting/mask/counter) a small per-port struct indexed by a port argument to `padMacroPerform`. Justification for added code: the state is genuinely per-input-stream; sharing it is the defect.
- **Benefit:** Toggle slowdown, axis-invert and turbo-arming behave identically on pad 2 as on pad 1, and two players stop toggling each other's macros.
- **Effort:** M · **Risk:** Med (touches the shared entry point of both pademu pad drivers) · **Confidence:** Mechanism READ; poll interleaving INFERRED from the per-pad caller sites

### R5-M6-10 / pademu assumes every SIO pad transfer is at least a full command long — short transfers overrun `in`/`out`
- **Category:** 6
- **Locations:** `modules/pademu/pademu.c:280` and `:366` (`td->in_size - 3` scan bounds), `:375` (`cmd_size + 3 == td->in_size` not-found test), `:421-553` (`pademu_cmd`: unconditional `out[0..2]`, `in[1]`, `in[3]`, writes through `out[8]` at `:449,:540`, 6-byte copies to `&out[3]`), `:555-589` (`pademu_mtap`: `td->out[6]` at `:582` after a 6-byte copy at `:580`, no `out_size` test anywhere)
- **Problem:** Nothing in the dispatch validates a minimum transfer size. `pademu_cmd`'s parameter is even named `out_size` but is only ever used as `out_size - 3` for the `get_data` call (the >18 direction of that call is RIPTOPL-106, already filed — not repeated here); the function's own header writes and command-argument reads (`in[3]`, `in[3+i]` for `i` up to 5, `pad[port].mask` reads at `in[3..6]`) proceed for a transfer of any size, including a 1–3-byte SIO transaction from a game probing the port. The "second command" scans compute `td->in_size - 3` on a `u32` (`pademu.h:47`), so an `in_size < 3` wraps the bound to ~4 billion and the scan reads `td->in[]` far out of bounds; the not-found exit test `cmd_size + 3 == td->in_size` also cannot fire for sizes the scan skipped. `pademu_mtap` likewise writes `td->out[6]` without reference to `td->out_size`. Real DS2 traffic is ≥9 bytes, which is why this survives; the sizes here come from the game's SIO2MAN transfer descriptor, not from a trusted constant.
- **Fix:** Early-out in `pademu`/`pademu_cmd`/`pademu_mtap` when the transfer is smaller than the command being answered needs (and guard the scan bound against `in_size < 3`). Justification: a handful of size tests at the entry points is the minimum correct shape.
- **Benefit:** Short/probing SIO transactions from a game can no longer make the pad emulator read or write outside the transfer buffers it was handed.
- **Effort:** S · **Risk:** Low · **Confidence:** Mechanism High (READ); that a shipped game issues sub-9-byte pad transfers is INFERRED (none identified in-tree)

### R5-M6-11 / apemodpatch patches executable code but leaves its `FlushIcache()` commented out — its sibling patch flushes
- **Category:** 6
- **Locations:** `modules/iopcore/patches/apemodpatch/main.c:104-115` (eight instruction writes incl. two `JAL`s), `:118` (`// FlushIcache(); //Flush instruction cache as instructions were modified.`); contrast `modules/iopcore/patches/iremsndpatch/main.c:110` (same patching pattern, flush present)
- **Problem:** apemodpatch rewrites instructions in a loaded module and immediately `StartModule`s it, with the icache flush the author wrote left commented out. The sibling IREMSND patch — same directory family, same technique, same era — calls `FlushIcache()` with the identical comment, so the tree contains both answers to the same question. If any patched line is already resident in the IOP's icache (the module image region having executed or been fetched before, e.g. address reuse), the CPU can execute the pre-patch instructions and the game's broken locking protocol stays live while the patcher reports success.
- **Fix:** Uncomment the flush (it is already written). No justification needed — this is restoring the author's own line.
- **Benefit:** The Ape Escape patch is guaranteed to be the code that runs, matching the sibling patch's discipline.
- **Effort:** S · **Risk:** Low · **Confidence:** Medium (READ for the asymmetry; whether stale lines can actually be resident for a loaded-but-not-started module is INFERRED)

### R5-M6-12 / iremsndpatch rewrites a module found by numeric ID alone — no identity or size check before eleven fixed-offset writes
- **Category:** 6
- **Locations:** `modules/iopcore/patches/iremsndpatch/main.c:54-118` (`strtol(argv[1], NULL, 16)` at `:64`, list walk by `m->id` only at `:70-77`, pointer reconstructed from two words at `:87-89`, writes at `:94-108` up to `text_start + 0x1ff8`)
- **Problem:** The patch locates its target purely by the module ID passed in argv, then dereferences a thread-ID pointer assembled from a `lui/addiu` pair read at fixed offsets, and writes eleven words at fixed offsets up to `0x1FF8` into `text_start` — without checking the module's name, version, or `text_size`. If the ID is stale or the loaded IREMSND differs from the expected revision, every one of those writes lands in some other module's code (or past a smaller text segment), and the reconstructed pointer is trusted as an `int *` and later dereferenced by `postStopSeseqTick`. apemodpatch/f2techioppatch share the fixed-offset technique but at least load their target file themselves in the same `_start`, tying identity to the path ee_core chose; iremsndpatch's target arrives by number alone.
- **Fix:** Before writing, verify the located module is the expected one (name/version as ee_core identified it) and that `text_size` covers `0x1FFC`; refuse otherwise, as the file already refuses on a missing module. Justification: the check is the only thing standing between a wrong ID and arbitrary code corruption.
- **Benefit:** A mismatched module ID or an unexpected IREMSND revision becomes a clean no-patch instead of silent corruption of an unrelated module.
- **Effort:** S · **Risk:** Low · **Confidence:** Mechanism High (READ); how ee_core derives the ID it passes (context outside this section) is INFERRED to be name-based, which would make this defence-in-depth rather than a live path

### R5-M6-13 / imgdrv's `read_fs` has no offset and no bound against the image size it serves
- **Category:** 6
- **Locations:** `modules/iopcore/imgdrv/imgdrv.c:9-10` (`ioprpimg`/`ioprpsiz` patch points), `:20-27` (`lseek_fs`), `:29-33` (`read_fs`)
- **Problem:** `read_fs` is `memcpy(buffer, (void *)ioprpimg, size); return size;` on every call: it ignores the file position entirely and never compares `size` with `ioprpsiz`. A caller that reads in chunks gets the image's first chunk repeatedly; a caller that asks for more than the image (or more than remains) is silently handed bytes from whatever IOP memory follows the image, reported as a successful full read. `lseek_fs` compounds it: `SEEK_END` returns `ioprpsiz` regardless of the requested offset, everything else returns 0 regardless. The driver survives because its one known consumer reads the image exactly the way the implementation assumes (size query, then one full read) — a contract that exists nowhere except in this code's shape, and that round 4 already saw ee_core defensively working around from the other side (the magic-word scan is bounded and refuses when patch points are missing; nothing bounds the read side).
- **Fix:** Track the position in the file descriptor (or honour the `iop_file_t` offset), and clamp each read to `ioprpsiz - position`, returning 0 at end of image. Justification: position+clamp is the smallest change that makes the driver's answers true for any caller.
- **Benefit:** The IOPRP image driver stops disclosing unrelated IOP memory and mis-serving any consumer that reads incrementally.
- **Effort:** S · **Risk:** Low · **Confidence:** Mechanism High (READ); multi-chunk/oversized reachability INFERRED (the in-tree consumer's single-read pattern is the mitigating fact)

### R5-M6-14 / imgdrv carries dead code: an unused `dummy_fs` and two unused private device-struct typedefs duplicating the SDK's
- **Category:** 1 (dead code)
- **Locations:** `modules/iopcore/imgdrv/imgdrv.c:14-17` (`dummy_fs`), `:40-62` (`iop_device_tm_t`, `iop_device_ops_tm_t`), `:65-90` (ops table: the only references to `dummy_fs` are in the commented-out block at `:79-89`)
- **Problem:** `dummy_fs` is defined and then used nowhere — the ops table fills the slots it was meant for with `NULL`/`IOMAN_RETURN_VALUE(0)` and mentions it only inside a comment. The two `*_tm` typedef structs (23 lines) re-declare the SDK's `iop_device_t`/`iop_device_ops_t` under local names and are never used; the actual device is declared with the real types at `:92-97`. In a 98-line file, ~28 lines are inert.
- **Fix:** Delete `dummy_fs`, both typedefs, and the commented-out ops block. Pure deletion.
- **Benefit:** The whole driver becomes the ~70 lines that do something; no future editor mistakes the private structs for the ones in force.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — no references exist in the file, and the module has no other translation unit)

### R5-M6-15 / Two hand-copied 8-digit hex parsers for module-load arguments already disagree (lowercase)
- **Category:** 2 (duplication)
- **Locations:** `modules/iopcore/igrmbox/igrmbox.c:151-167` (`mbHex8` — accepts `0-9`, `A-F` only); `modules/iopcore/freezediag/freezediag.c:24-45` (`parse_hex8` — additionally accepts `a-f` at `:36-37`)
- **Problem:** Both new IOP diag modules parse the same kind of argument (an 8-hex-digit address string built by ee_core) with their own copy of the same 17-line loop, and the copies have already drifted: freezediag accepts lowercase hex, igrmbox rejects it (returns 0 → `_start` refuses the load and the in-game menu silently never appears). Today ee_core generates uppercase (`ee_core/src/igrmbox.c:48` — `"0123456789ABCDEF"`), so the divergence is latent; it is also exactly how duplicated parsers announce themselves. Neither copy checks that the string is actually 8 characters before reading `s[0..7]`.
- **Fix:** Keep one `static inline` copy in the shared header both module families already draw from (`modules/iopcore/common/`), accepting both cases, and delete the other. Net deletion across the tree.
- **Benefit:** One parser to fix, one acceptance rule for ee_core's address arguments, and the lowercase footgun closes before a generator change finds it.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M6-16 / The two ds34 EE client libraries are one library written twice
- **Category:** 2 (duplication)
- **Locations:** `modules/ds34bt/ee/libds34bt.c` (181 lines) vs `modules/ds34usb/ee/libds34usb.c` (153 lines) — same file skeleton throughout: `rpcbuf[64]`, `inited` flag, identical bounded-bind `*_init` incl. the same three-line comment (`libds34bt.c:24-29` / `libds34usb.c:24-29`), `*_deinit`, `*_reinit_ports`, `*_get_status`, `*_set_rumble`, `*_get_data`, `*_reset` differing in name prefix, RPC id and a handful of command numbers
- **Problem:** Round 4 mapped the four *IOP-side* ds34 copies in depth; these two *EE-side* RPC clients are a fifth/sixth copy of each other. Every function is the same guard-marshal-call-unmarshal shape, so a fix like the bounded bind had to be written (and was written, comment included) twice, and the copy-out-on-failure defect in R5-M6-17 exists in both for the same reason. The genuinely different content is per-bus: the bind RPC id, the command enum, and three extra calls on each side (`init_charging`/`get_version`/`get_features` vs `set_bdaddr`).
- **Fix:** Factor the common client core (bind-with-timeout, rpcbuf, the shared commands) into one shared EE source/header parameterised by bind id and command enum, leaving the bus-specific calls in thin per-bus files. This is the rare dedup where the two copies are in the same layer, same repo area, and already byte-similar in structure.
- **Benefit:** Roughly 120 lines and one whole maintenance surface removed; the next RPC-client fix lands once.
- **Effort:** M · **Risk:** Med (both GUI pad paths link these; a botched merge breaks pad support in the loader UI, IOP side untouched) · **Confidence:** High (READ)

### R5-M6-17 / ds34 EE clients copy `rpcbuf` to the caller even when the RPC failed
- **Category:** 6
- **Locations:** `modules/ds34bt/ee/libds34bt.c:127-141` (`ds34bt_get_data`, `memcpy` at `:138`); `modules/ds34usb/ee/libds34usb.c:131-144` (`ds34usb_get_data`, `memcpy` at `:142`) and `:74-91` (`ds34usb_get_bdaddr`, copy loop at `:88`, gated only by `ret &= rpcbuf[0]`)
- **Problem:** In both `get_data` implementations the 18-byte `memcpy(data, rpcbuf, 18)` runs unconditionally — the RPC result is computed into `ret`, the copy happens regardless, and only then is `ret` returned. A failed call therefore still overwrites the caller's pad-data buffer with stale bytes from the previous successful call (or zeroes at boot), and any caller that uses the buffer without checking the return — the pattern these thin wrappers invite — consumes phantom pad input. `ds34usb_get_bdaddr` does the same to the caller's address buffer; its `ret &= rpcbuf[0]` even reads the reply byte when no reply arrived. Sibling functions in the same files (`get_status`, `get_version`, `get_features`) correctly copy only inside the success branch, so the files disagree with themselves.
- **Fix:** Move the copies inside the `SifCallRpc(...) == 0` success path, matching the sibling functions in the same files. Pure rearrangement.
- **Benefit:** A dropped RPC leaves the caller's last real pad state untouched instead of replaying it as fresh input.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

### R5-M6-18 / `test_ministack_xchg` is the one udpfs host test whose checks all vanish under `-DNDEBUG` — including a side-effecting `assert`
- **Category:** 7 (test code)
- **Locations:** `modules/network/udpfs_shared/tests/test_ministack_xchg.c` (assert-based throughout, e.g. checks at `:178-302`; side-effecting `assert(deliver(&script[0]) == -1)` at `:239`; asserts inside the stubs at `:38,55-77`)
- **Problem:** These five host tests are genuinely behavioural (see Checked-and-clean) — which makes this one's fragility worth separating from RIPTOPL-005's source-grep pattern. Its three siblings use an explicit `check()` counter that always runs and fails the process. `test_ministack_xchg` (304 lines, the largest behavioural surface of the set: round-trip, stray-source, oversize, ARP-failure, bad-args) expresses **every** check as `assert()`. The in-tree runner (`.github/scripts/test_udpfs_transport.py:20-22`) compiles without `-DNDEBUG` and without `-O`, so the asserts are live today — but any future flag change, IDE default, or copied build line that defines `NDEBUG` turns the whole suite into a program that performs the exchanges, checks nothing, prints `PASS`, and — because `deliver()` itself is called inside an assert at `:239` — silently skips a delivery the following scenario depends on. Asserts inside the stubs (`smap_fifo_read`'s bounds asserts) are the suite's only FIFO overrun protection and vanish on the same switch.
- **Fix:** Convert the checks to the siblings' `check()` pattern (or a local equivalent that counts failures and returns non-zero). No new framework; the pattern already exists in the same directory.
- **Benefit:** The exchange suite's verdict survives build-flag drift instead of silently becoming a PASS that tested nothing.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — runner flags included)

---

## Already filed — not repeated

- **RIPTOPL-106** — pademu `get_data` memcpy length unchecked (size > 18 over-read of the pad union, via `pademu.c`'s `out_size - 3`). R5-M6-10 is the opposite direction in a different function (the SIO transfer's *minimum* size vs `pademu_cmd`'s own reads/writes) and does not re-argue it.
- **RIPTOPL-107** — pademu ds34 entry points dropped the standalone port guards. `pademu.c`'s port provenance (SIO register bits 0–3) was its mitigating context; no port-guard finding is re-filed here.
- **RIPTOPL-005** — `.github/scripts` source-text grep "tests". The five udpfs host tests differ materially (they compile and execute production code against scripted stubs — see Checked-and-clean); the only test finding filed, R5-M6-18, is for a defect internal to one of them.
- **Round 3's ee_core note** (Sable-RiptOPL-ROUND3.md §"checked clean") — *EE-side* `ee_core/src/igrmbox.c` `mbPost` is bounded (3 s). R5-M6-08 concerns the *IOP-side* module `modules/iopcore/igrmbox/igrmbox.c`, a different file, whose `mbSendEE` has no bound.
- **RIPTOPL-048 / round-4 POPSLoader-048 pattern (DS4 memset/send lengths)** and **RIPTOPL-083/-092/-105/-108…-114** — all live in the four IOP/pademu ds34 driver copies (`modules/ds34*/iop/`, `modules/pademu/ds34*.c`), none of which are in this section's file list; the EE libs in R5-M6-16/-17 are different files with no prior finding.

## Checked and clean (dissolved on full reading / verification)

- **udpfs host tests generally** (`test_udprdma_session.c`, `test_ministack_announce.c`, `test_ministack_conflict.c`, `test_ack_sequence.c`) — behavioural, not source-scraping: they compile the production `.c` against scripted IOP/network stubs and assert wire-visible outcomes (sequence restart after reconnect, stale-ACK handling, lost window-ACK re-answers, gratuitous-ARP rules, IP-conflict recording, ACK wrap table). Three use always-on `check()` failure counters; `test_ack_sequence.c` is a clean table-driven unit returning non-zero on the first mismatch. Nothing in them resembles the RIPTOPL-005 pattern.
- **freezediag.c** — the scary-looking `len += freeze_diag_format_heap(snapshot, text + len, TEXT_CAP - len)` (incl. a NULL snapshot when `parse_hex8(argv[2])` fails) dissolves against the formatter's contract, verified in `ee_core/src/freeze_diag_format.c`: both functions return 0…cap, never negative, and the heap formatter treats a NULL/!found snapshot as a printable "heap=none" case. `write_report` checks for short writes and falls back mc0→mc1.
- **bdmevent/main.c** — the uninitialised `devices[20]` stack array dissolves: `bdm_get_bd` (`modules/bdm/src/bdm.c:150-151`) fills `min(count, MAX_CONNECTIONS)` entries and `MAX_CONNECTIONS` is exactly 20, with NULL for empty slots, so every element the snapshot loop dereferences was written. (Implicit coupling worth a comment in-tree, but no defect at this pin.)
- **igrmbox `mbDo`** (the half not in R5-M6-08) — BEGIN/SEND validation is exemplary: zero/oversize (`IGRMBOX_BUF_MAX`)/alignment rejection, and the SEND range test uses the overflow-safe `offset > mbBufSize || size > mbBufSize - offset` form; WAKE also frees a dangling buffer on purpose, with the reason commented.
- **ds34common.c translators** — guitar/DS4 `dpad_mapping[]` tables look like a `u8` truncation risk but every entry fits (DS2 dpad bits are 1<<4…1<<7 per `include/ds34common.h:51-54`, max combination 192), and both translators clamp the device-supplied dpad index to `Released` before indexing; pressure emulation is straight bounded copies.
- **sys_utils.c** — `GetExportTable` zero-fills the library struct before the bounded 8-char name copy; `GetExportEntry` is bounds-checked; `HookExportEntry`'s replace-all-matching-entries is the documented hook technique (shared entries must all move), under interrupt suspension.
- **libds34bt/libds34usb bounded bind** — the 3-second `clock()` bind loops are a correct, commented fix shape (elapsed-time comparison, wrap-aware); **libds34bt `get_version`** overflow suspicion dissolves: `hci_information_t` is 14 bytes packed (`libds34bt.h:14-24`), so the receive length 15 and both `memcpy`s fit the 64-byte `rpcbuf` comfortably.
- **net_fio `pko_write_file` wrong segment constant** — it segments writes by `PKO_MAX_READ_SEGMENT` where `PKO_MAX_WRITE_SEGMENT` was meant (comment even says "read reqs"), but both constants are `1460 - sizeof(...)` and the read-reply struct is the larger one, so the chosen segment is the *smaller* of the two: no overflow of the 4,096-byte packet buffer, no observable effect beyond marginally smaller segments. Copy-paste smell, zero consequence at this pin — not filed.
- **net_fsys.c** — the old-ioman function-pointer casts are the module's deliberate legacy ABI, and every operation is serialised by `fsys_sema` as its header comment promises; `dummy5` is the standard unimplemented-op stub. No defect beyond what R5-M6-04/-05 file on the `net_fio` side.
- **resetspu.c** — the register poll is bounded (`0xF01` iterations) and deliberately fail-open before handing the SPU2 to OSDSYS; `nopdelay` is hardware timing, not a missed sleep.
- **cleareffects/main.c** — 30 lines, does exactly its comment; both cores programmed with the same zeroed effect attr.
- **f2techioppatch/main.c** — loads its target itself (identity tied to the path, like apemodpatch), writes two data immediates, starts the module; nothing else to file once R5-M6-11 is scoped to the module that rewrites *instructions*.
- **padmacro.c arithmetic** (outside R5-M6-09's state finding) — `scaleAnalogStick`'s 23/32 scaling stays in `s16` range for all inputs, invert-at-centre guard is deliberate, turbo counter/phase math is bounded by construction.
- **udptty KPRTTY block** — compile-time optional and bounds-checked (`prpos < bsize` before every store); the broadcast address/port are the module's documented design.

## Forced revisions to prior findings

None. No RIPTOPL-001…144 finding is contradicted by anything in these 27 files; the two adjacencies (RIPTOPL-106, round 3's EE-side igrmbox note) are scope boundaries, not corrections.
