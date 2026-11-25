# RiptOPL Round 9 — modules/network netstack (SMSTCPIP / SMSUTILS / ps2ips)

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` (verified HEAD, clone left git-clean).
Scope read in full: `modules/network/SMSTCPIP/` (vendored SMS-lineage lwIP, ~1.3/1.4-era,
heavily locally patched), `modules/network/SMSUTILS/`, `modules/network/ps2ips/`.
17,801 lines across 63 files — see ledger at the end. Unread: **zero**.

Vendored-status note: SMSTCPIP is a fork of old lwIP with many deliberate local changes
(RA fixes, SMB-shaped `lwip_recvfrom`, core-locking API model). Findings below distinguish
local divergence from inherited vintage behaviour. Pristine-upstream defects with no local
aggravation are listed as unnumbered observations in the checked section, per the round-9
vendored lens.

Already filed in this scope (cross-referenced, NOT re-filed): RIPTOPL-067 (ps2ips
`do_setsockopt` EE length → 128 B stack buffer), RIPTOPL-080 (ps2ips
`do_accept`/`do_getsockname`/`do_getpeername` uninitialised `addrlen`), RIPTOPL-100
(ps2ips `do_getsockopt` always copies 128 B), RIPTOPL-282 (`mips_memcpy` delay-slot return
bug — see SMSUTILS note in checked section; the in-scope copy is one of the known four,
no fifth copy exists, no divergence in the function bodies).

RIPTOPL-271 (`poll.c` invalid-fd tracking): **no `poll.c` exists in this scope.** The only
`poll.c` in the repo is `modules/network/smbman-ra/poll.c`, a different tree. Nothing in
SMSTCPIP/SMSUTILS/ps2ips implements or wraps `poll()`; the nearest mechanism is
`lwip_select`, covered by NETSTACK-02. Stated explicitly per instructions: distinct file,
not in scope, no cross-reference needed beyond this note.

---

## Candidates (sorted by benefit)

### NETSTACK-01 — ARP entries can never expire: `etharp_tmr()` has no caller because `LWIP_ARP` is defined nowhere

- **Category:** 6 (bug) / 8 (half-finished wiring)
- **Locations:**
  - `modules/network/SMSTCPIP/timers.c:103-124` — the only `etharp_tmr()` caller
    (`arp_timer`) and its scheduling in `sys_timeouts_init()` are both inside `#if LWIP_ARP`
  - `modules/network/SMSTCPIP/include/lwip/opt.h` (whole file, read) and
    `modules/network/SMSTCPIP/include/lwipopts.h` (whole file, read) — neither defines
    `LWIP_ARP`; a repo-wide grep finds no definition anywhere, so the preprocessor treats
    it as 0 in both places
  - `modules/network/SMSTCPIP/etharp.c:209-229` — `etharp_tmr()` itself (increments
    `ctime`, expires STABLE entries at `ARP_MAXAGE` and PENDING entries at
    `ARP_MAXPENDING`, etharp.c:105-108)
- **Problem:** ARP ageing is compiled out. STABLE entries never expire (20-minute
  `ARP_MAXAGE` is dead code) and PENDING entries never time out (20-second
  `ARP_MAXPENDING` is dead code). With `ARP_TABLE_SIZE` of only 2–4
  (lwipopts.h:238-248) and `ETHARP_ALWAYS_INSERT 0`, one PENDING entry for a dead host
  permanently consumes a slot (`find_arp_entry`, etharp.c:243-280, can only recycle
  EMPTY or STABLE entries), and a gateway whose MAC changes (router replaced, VM moved)
  leaves a stale STABLE entry that `etharp_output` keeps using — all off-LAN traffic
  black-holes until the console reboots. `update_arp_entry` only refreshes an entry when
  that host happens to send us a packet.
- **Fix:** Define `LWIP_ARP 1` in `lwipopts.h`, and in the same change raise
  `MEMP_NUM_SYS_TIMEOUT` where it is currently 1 (lwipopts.h:121-125, the non-INGAME_UDP
  in-game build): the ARP timer and the on-demand TCP timer
  (timers.c:88-101, `tcp_timer_needed`) must coexist, and `sys_timeout()` silently drops
  a registration when the pool is empty (timers.c:170-174), so enabling the ARP timer
  without the extra slot can starve the TCP timer instead.
- **Justification if additive:** One define plus one pool count; no new code — it
  reconnects code that is already written, tested by upstream design, and present in
  the object (`timers.o` is in the Makefile object list).
- **Benefit:** Restores the only ARP cache maintenance the stack has; removes a
  permanent-wedge failure mode on a 2–4-entry table.
- **Effort:** S
- **Risk:** Low–Medium — behaviour change is limited to entry expiry, but the timeout-pool
  sizing must land with it (see Fix).
- **Confidence:** High (READ + repo-wide grep: no `LWIP_ARP` definition, no other
  `etharp_tmr` caller in the tree).

### NETSTACK-02 — Eight `ps2ip` exports are an empty stub, while in-tree modules import and call them

- **Category:** 8 (half-finished feature) / 6 (bug — indeterminate return values)
- **Locations:**
  - `modules/network/SMSTCPIP/exports.tab:17-24,30` — slots 14 (`lwip_select`),
    15 (`lwip_ioctl`), 16 (`lwip_getpeername`), 17 (`lwip_getsockname`),
    18 (`lwip_getsockopt`), 20 (`ps2ip_setconfig`), 21 (`ps2ip_getconfig`) and
    27 (`netif_find`) are all `DECLARE_EXPORT(ps2ip_Stub)`; `ps2ip_Stub` is an empty
    function (`modules/network/SMSTCPIP/ps2ip.c:274-277`)
  - The real implementations exist but are compiled out in the default build
    (`FULL_LWIP ?= 0`, `modules/network/SMSTCPIP/Makefile:8,31-33`):
    `modules/network/SMSTCPIP/sockets.c:630` (`lwip_select`/`lwip_selscan`),
    `sockets.c:899` (`lwip_getpeername`/`lwip_getsockname`/`lwip_getsockopt`),
    `sockets.c:1377` (`lwip_ioctl`), `modules/network/SMSTCPIP/ps2ip.c:97`
    (`ps2ip_getconfig`/`ps2ip_setconfig`), `modules/network/SMSTCPIP/netif.c:110`
    (`netif_find`/`netif_remove`)
  - Consumers (corroboration, outside this section's scope):
    `modules/network/common/smstcpip.h:56-57,109-119,147-157` declares imports 14–21
    and 27 for every consumer of the shared header;
    `modules/network/smbman-ra/imports.lst:13` imports `I_lwip_select`;
    `modules/network/smb2man/imports.lst:13-15`, `modules/network/nfsman/imports.lst:14-16`
    and `modules/network/httpclient/imports.lst:15,18-19` import `lwip_select`,
    `lwip_ioctl` and/or `lwip_getsockopt`
  - In-scope consumer: `modules/network/ps2ips/ps2ips.c` imports all of them
    (`modules/network/ps2ips/imports.lst`, ps2ip block) and dispatches RPCs to them
    (`do_select` :573, `do_ioctlsocket` :592, `do_getsockname` :601,
    `do_getpeername` :619, `do_getsockopt` :637, `do_getconfig` :562,
    `do_setconfig` :568)
- **Problem:** The stack's Makefile comment says the FULL_LWIP functions "were removed
  since OPL does not use them", but OPL's own in-game drivers do import them. Any call
  lands in `ps2ip_Stub`, which returns whatever happens to be in `$v0` — so a
  `select()` can report a random positive count, a `getsockopt()` "succeeds" with
  uninitialised output (compounding RIPTOPL-100's 128-byte copy in the ps2ips pairing),
  and `ps2ip_getconfig()` reports garbage as the interface configuration. Note the
  menu pairing is *not* affected: the menu runs ps2sdk's prebuilt `ps2ip-nm.irx`
  (lwipopts.h:76-79), which implements these; this defect is specific to pairing
  consumers with the in-tree stack, which is exactly the in-game configuration
  (top-level Makefile builds SMSTCPIP only as the in-game driver).
- **Fix:** Decide the contract one way or the other. LESS-NOT-MORE option: replace the
  single shared stub at these slots with tiny per-slot stubs that return a defined
  error (`-1`/`ERR_IF` as each signature allows), so callers fail loudly instead of
  reading garbage. Full option (additive, needs justification): build SMSTCPIP with
  `FULL_LWIP=1` — the implementations already exist and are locally maintained
  (sockets.c carries recent local fixes inside the FULL_LWIP regions) — and delete
  the stub rows.
- **Justification if additive:** Only the FULL_LWIP option is additive, and only in
  the sense of compiling code already in the tree; it is justified if any in-game
  consumer actually calls `select`/`ioctl`/`getsockopt` on its live path (the
  imports say they were linked for a reason).
- **Benefit:** Eliminates a whole class of silent garbage-result failures across
  SMB2/NFS/HTTP in-game drivers and the ps2ips RPC surface.
- **Effort:** S (error stubs) / M (FULL_LWIP flip + validation)
- **Risk:** Low (error stubs) / Medium (FULL_LWIP flip — newly live code paths,
  incl. the RIPTOPL-067/-080/-100 ps2ips surface becoming reachable against this stack)
- **Confidence:** High on the stub/import/gating facts (READ + grep). Medium
  (INFERRED) on which consumers call these on live paths — call sites sit in other
  round-9 sections (smbman-ra, smb2man, nfsman, httpclient).

### NETSTACK-03 — Client netconns never get a semaphore, so `netconn_write()` busy-spins on `WaitSema(0)`

- **Category:** 5 (performance, structural) / 6 (bug)
- **Locations:**
  - `modules/network/SMSTCPIP/api_lib.c:157` — `netconn_new_with_proto_and_callback`
    sets `conn->sem = SYS_SEM_NULL` and no code path ever creates it for client conns
    (grep-verified across api_lib.c/api_msg.c/sockets.c)
  - `modules/network/SMSTCPIP/api_lib.c:538-539` — `netconn_write()` waits on
    `conn->sem` whenever `tcp_sndbuf()` hits 0; `:563` — same wait on the `ERR_MEM`
    retry path. `SYS_SEM_NULL` is 0
    (`modules/network/SMSTCPIP/include/arch/sys_arch.h:11`) and
    `sys_sem_wait` is a direct `WaitSema` macro
    (`modules/network/SMSTCPIP/include/lwip/sys.h:82`), i.e. `WaitSema(0)`, an
    invalid semaphore ID that returns an error immediately
  - Contrast: accepted conns *do* get one —
    `modules/network/SMSTCPIP/api_msg.c:259` (`accept_function`:
    `newconn->sem = sys_sem_new(0)`) — and the signal side already guards on it:
    `poll_tcp`/`sent_tcp` only signal `if (conn->sem != SYS_SEM_NULL)`
    (api_msg.c:175-177, 195-197), while `netconn_close`'s wait is likewise guarded
    (api_lib.c:592-593). Only the write waits are unguarded.
- **Problem:** `TCP_SND_BUF` is 2,920 bytes (lwipopts.h:202, `TCP_MSS * 2`). Any
  `netconn_write()` larger than the currently free send buffer — routine for SMB
  writes — reaches the wait loop, `WaitSema(0)` fails instantly, and the caller
  thread spins at full speed, re-checking `tcp_sndbuf()`, until ACKs free space.
  The stack still works, but the writer thread burns the IOP instead of blocking,
  and the ERR_MEM path spins the same way.
- **Fix:** Create the semaphore in `netconn_new_with_proto_and_callback`, exactly
  as `accept_function` already does (`sys_sem_new(0)`, with the existing
  delete-path handling in `netconn_delete`, api_lib.c:228-230, which already frees
  it when non-NULL). One line plus a NULL check.
- **Justification if additive:** Not additive in spirit — it restores the object
  the API was designed around; every other participant (accept path, signal side,
  close path, delete path) already assumes it can exist.
- **Benefit:** Blocking instead of spinning on every large send; removes a
  priority-inversion-style IOP stall during game SMB streaming.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High on the missing creation (READ + grep); the
  `WaitSema(0)`-returns-immediately step is INFERRED from `SYS_SEM_NULL == 0` and
  standard IOP semaphore semantics (not execution-verified).

### NETSTACK-04 — `lwip_accept()` dereferences a NULL netconn that `err_tcp()` itself posts

- **Category:** 6 (bug/security — NULL deref in the stack's socket layer)
- **Locations:**
  - `modules/network/SMSTCPIP/sockets.c:164-186` — `lwip_accept()` passes the
    `netconn_accept()` result straight to `netconn_peer(newconn, ...)` with no
    NULL check
  - `modules/network/SMSTCPIP/api_msg.c:202-213` — `err_tcp()` posts NULL to the
    accept mbox (`sys_mbox_post(conn->acceptmbox, NULL)`) when a listening
    connection errors, which is exactly what wakes `netconn_accept()` with a NULL
    return (`modules/network/SMSTCPIP/api_lib.c:395-408`)
  - `modules/network/SMSTCPIP/api_lib.c:243-270` — `netconn_peer()` switches on
    `conn->type` unconditionally: NULL in, IOP exception out
- **Problem:** A listening socket whose PCB errors (reset during handshake, abort)
  deterministically posts the NULL that crashes the next `accept()` call. This is
  not a theoretical NULL: the in-tree error path manufactures it.
- **Fix:** Return `-1`/`ENOBUFS` from `lwip_accept()` when `newconn == NULL`
  (3 lines), matching the check the same function already does for its
  `alloc_socket()` failure immediately below.
- **Benefit:** Removes a remotely triggerable IOP crash on any listening socket.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High (READ, full in-tree chain).

### NETSTACK-05 — `do_bind()` uses lazily allocated PCBs without the NULL checks its sibling handlers have

- **Category:** 6 (bug — NULL deref under memory pressure)
- **Locations:**
  - `modules/network/SMSTCPIP/api_msg.c:420,425,430` — `do_bind()` calls
    `udp_new()` three times and immediately calls `udp_setflags()`/`udp_recv()`
    on the result; `:436-437` — same pattern for TCP: `tcp_new()` then
    `setup_tcp()` unchecked; the subsequent bind switch (api_msg.c:443-475) then
    dereferences the PCB again
  - Contrast (same file): `do_newconn` checks every allocation
    (api_msg.c:305-341) and `do_connect` checks every allocation
    (api_msg.c:504-541, including TCP at :535-541)
  - `modules/network/SMSTCPIP/include/lwipopts.h:82` — `MEMP_NUM_UDP_PCB 1`:
    the UDP pool has exactly one PCB in every build from this file, so "pool
    exhausted" is a normal state, not an edge case
- **Problem:** `netconn_bind()` posts `API_MSG_BIND` without checking
  `conn->err` (api_lib.c:280-309), so a conn whose PCB creation already failed
  in `do_newconn` reaches `do_bind`, retries allocation against an exhausted
  pool, gets NULL, and the stack's own thread dereferences it
  (`udp_setflags` writes `pcb->flags`; `setup_tcp` writes through `pcb`).
- **Fix:** Copy the guard pattern `do_connect` already uses into `do_bind`
  (set `msg->conn->err = ERR_MEM`, ACK, return). No new mechanism.
- **Benefit:** Pool exhaustion returns an error instead of crashing the TCP/IP
  thread — with a 1-entry UDP pool this is reachable by an ordinary second UDP
  socket.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High (READ; the asymmetry with the two sibling handlers is
  in-file evidence).

### NETSTACK-06 — ps2ips `do_send()`/`do_sendto()` trust the EE-supplied `malign` field completely

- **Category:** 6 (bug/security — unchecked EE RPC length)
- **Locations:**
  - `modules/network/ps2ips/ps2ips.c:402-418` (`do_send`) and `:435-451`
    (`do_sendto`): `s_offset = 64 - pkt->malign;` then
    `memcpy(lwip_buffer + s_offset, pkt->malign_buff, pkt->malign)` and
    `sceSifGetOtherData(..., sendlen - pkt->malign, ...)` — no range check on
    `malign`, `length`, or their relationship anywhere in either handler
- **Problem:** `malign` comes straight from the RPC packet. The design assumes
  `0 <= malign <= 64` (it is the EE buffer's misalignment). `malign > 64` makes
  `s_offset` negative and the memcpy writes *before* `lwip_buffer`;
  `malign > sendlen` makes the `sceSifGetOtherData` size negative (a huge
  unsigned count at the SIF layer). The RA-hardened code around it (the
  `BUFF_SIZE + 64` buffer, ps2ips.c:84-86) is all predicated on offsets staying
  inside `[0, 64]`. Distinct from RIPTOPL-067 (the `do_setsockopt` length) —
  same class, different handler, different buffer.
- **Fix:** Reject the request at handler entry when
  `malign < 0 || malign > 64 || length < malign` (return -1 in `ptr[0]`). Two
  comparisons per handler; deletion-shaped, no new code paths.
- **Benefit:** Closes an EE→IOP memory-corruption primitive in the RPC server
  that every EE program links against.
- **Effort:** S
- **Risk:** Low — the stock EE library computes `malign` from address
  alignment, so legitimate values are 0..63 and unaffected.
- **Confidence:** High on the missing check (READ). Exploitability beyond a
  malformed/buggy EE caller is INFERRED (the stock libps2ips never sends such
  values).

### NETSTACK-07 — `tcp_keepalive()` transmits a segment whose TCP flags byte is uninitialised pool memory

- **Category:** 6 (bug)
- **Locations:**
  - `modules/network/SMSTCPIP/tcp_out.c:530-570` — `tcp_keepalive()` fills
    src/dest/seqno/ackno/wnd/urgp, sets the header length via
    `TCPH_HDRLEN_SET` (which *preserves* the existing flags bits,
    tcp.h `TCPH_HDRLEN_SET` macro), computes the checksum and sends — but never
    calls `TCPH_FLAGS_SET`. Every other segment constructor in the same file
    does: `tcp_output`'s ACK path (tcp_out.c:360), `tcp_rst` (:485),
    `tcp_enqueue` (:220)
  - The pbuf comes from `pbuf_alloc(PBUF_IP, TCP_HLEN, PBUF_RAM)` (tcp_out.c:542);
    with `MEM_LIBC_MALLOC 1` that memory is `AllocSysMemory` output
    (ps2ip.c:524-533) — not zeroed
- **Problem:** A keepalive probe goes on the wire with whatever flags the
  recycled memory held — potentially SYN/RST/FIN bits set — which can reset
  or confuse the very connection the probe exists to preserve. Reachable
  whenever a connection has `SOF_KEEPALIVE` (settable via the exported
  `lwip_setsockopt` SO_KEEPALIVE, sockets.c:1197-1330) and goes idle past
  `pcb->keepalive + keep_cnt * TCP_KEEPINTVL` (tcp.c:632-644).
- **Fix:** One line: `TCPH_FLAGS_SET(tcphdr, TCP_ACK);` alongside the other
  field initialisations.
- **Benefit:** Keepalive probes become the well-formed ACK-only segments the
  peer expects.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High that no setter exists in the function (READ; the three
  sibling constructors establish the pattern). Which garbage values occur in
  practice is INFERRED (depends on pool recycling).

### NETSTACK-08 — `netconn_recv()`/`netconn_write()` dereference `conn->pcb.tcp` where the tree itself elsewhere guards it

- **Category:** 6 (bug — NULL deref on errored connections)
- **Locations:**
  - `modules/network/SMSTCPIP/api_lib.c:422` — `netconn_recv()`:
    `if (conn->pcb.tcp->state == LISTEN)`; `:538` — `netconn_write()`:
    `tcp_sndbuf(conn->pcb.tcp)` inside its wait loop
  - `modules/network/SMSTCPIP/api_msg.c:190` — `err_tcp()` sets
    `conn->pcb.tcp = NULL` on any fatal TCP error; the state the guards must
    handle is therefore a designed-in state
  - The guard pattern already exists in-tree for exactly this situation:
    `modules/network/SMSTCPIP/sockets.c` ~950 (`lwip_getsockname`, "Failed
    connects may free the PCB but leave the descriptor open"), ~1090
    (`lwip_getsockopt`), ~1300 (`lwip_setsockopt`) — all check
    `pcb.tcp == NULL` — and `netconn_peer` (api_lib.c:243-270) checks per-type
- **Problem:** `netconn_recv`'s earlier `conn->err != ERR_OK` early-return
  covers the common case, but the socket-layer callers run in other threads
  without the core lock (direct netconn API), so an error delivered between
  the check and the dereference — and `netconn_write`'s loop, which re-reads
  `pcb.tcp` on every spin iteration (see NETSTACK-03) — can dereference the
  NULLed PCB instead of returning `ERR_CONN`/`ERR_ABRT`.
- **Fix:** Apply the tree's own existing guard (`pcb.tcp == NULL` → return
  `ERR_CONN`) at both sites. No new mechanism; consistency with three
  existing sites.
- **Benefit:** Errored/desynced sockets fail with an error code instead of an
  IOP exception.
- **Effort:** S
- **Risk:** Low
- **Confidence:** Medium — the unguarded dereferences and the NULL-setting
  error path are READ facts; the inter-thread window that makes them live is
  INFERRED from the core-locking call model (tcpip.h:43-50).

### NETSTACK-09 — `copy_from_pbuf()` walks off the end of the pbuf chain when a fragment's IP length exceeds the delivered bytes

- **Category:** 6 (bug/security — wire-input NULL deref)
- **Locations:**
  - `modules/network/SMSTCPIP/ip_frag.c:61-81` — the copy loop is
    `for (; len; p = p->next) { ... p->len ... }` with no `p == NULL` stop
  - Call site `modules/network/SMSTCPIP/ip_frag.c:170` — `ip_reass()` passes
    `len` derived from the fragment header's `IPH_LEN()` field;
    `modules/network/SMSTCPIP/ip.c:219` (`ip_input`) calls
    `pbuf_realloc(p, IPH_LEN(iphdr))` first, but `pbuf_realloc` only shrinks
    a chain (`pbuf.c:320-375`) — it cannot extend the chain to match a forged
    length
- **Problem:** `IP_REASSEMBLY 1` in all non-in-game builds and in-game with
  the RA sizing (lwipopts.h:212-230). A fragment whose IP total-length field
  claims more payload than the Ethernet frame actually delivered leaves
  `len` greater than the chain's real bytes; the copy loop advances past the
  last pbuf and dereferences NULL in the TCP/IP thread.
- **Fix:** Stop the loop at `p == NULL` (one condition), returning the short
  copy so the existing reassembly timeout discards the datagram.
- **Benefit:** A malformed LAN fragment can no longer crash the stack.
- **Effort:** S
- **Risk:** Low
- **Confidence:** Medium — INFERRED in one step: it depends on SMAP delivering
  frames whose byte count can be smaller than a forged IPH_LEN (Ethernet
  minimum-frame padding may mask small overclaims; large ones cannot be
  masked).

### NETSTACK-10 — ps2ips `do_recv()`/`do_recvfrom()` accept a negative EE `length`, inverting the RA buffer-offset arithmetic

- **Category:** 6 (bug/security — unchecked EE RPC length)
- **Locations:**
  - `modules/network/ps2ips/ps2ips.c:183-207` (`do_recv`) and `:330-344`
    (`do_recvfrom`): `if (recv_pkt->length <= 64) srest = recv_pkt->length;`
    then `s_offset = 64 - srest;` and
    `recvlen = MIN(BUFF_SIZE, recv_pkt->length)` — no lower bound on `length`
- **Problem:** The RA sizing fix made `lwip_buffer` exactly `BUFF_SIZE + 64`
  (ps2ips.c:84-86) on the assumption `0 <= s_offset <= 64`. A negative
  `length` makes `srest` negative and `s_offset` greater than 64, pushing
  the receive destination outside the headroom the fix reserved, and passes a
  negative length into `recv()`/`recvfrom()`. Sibling handlers got RA fixes
  for their offset maths; the sign case was not covered. Distinct from
  RIPTOPL-067/-080/-100 (different handlers and mechanisms).
- **Fix:** Clamp at handler entry: treat `length < 0` as 0 / reject, before
  the `srest` computation. One comparison per handler.
- **Benefit:** The RA overflow fixes hold for the full range of EE-supplied
  inputs, not just non-negative ones.
- **Effort:** S
- **Risk:** Low
- **Confidence:** Medium — the arithmetic inversion is READ; how the paired
  stack's `recv()` treats a negative length is INFERRED (differs between the
  ps2sdk menu stack and the in-tree stack).

### NETSTACK-11 — ICMP error replies dereference `pbuf_alloc()` results unchecked and copy from the offending packet unchecked

- **Category:** 6 (bug — NULL deref under pool pressure)
- **Locations:**
  - `modules/network/SMSTCPIP/icmp.c:138-151` (`icmp_dest_unreach`) and
    `:170-175` (`icmp_time_exceeded`): `q = pbuf_alloc(PBUF_IP, ...)` is
    dereferenced (`idur = q->payload`) with no NULL check, and both then
    `memcpy` `IP_HLEN + 8` bytes from `p->payload` without confirming `p`
    carries that many bytes
- **Problem:** In-game `PBUF_POOL_SIZE` is 8 (lwipopts.h:165). ICMP errors
  are generated precisely when traffic is arriving — i.e. when the pool is
  most contended — so an allocation failure lands on the input path as a
  NULL dereference in the TCP/IP thread.
- **Fix:** `if (q == NULL) return;` in both functions (the surrounding code
  already treats ICMP as best-effort everywhere else).
- **Benefit:** Removes a load-correlated crash in the error-reporting path.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High on the missing checks (READ); Medium on live
  reachability (pool exhaustion timing is INFERRED).

### NETSTACK-12 — Dead code and build clutter: an unbuilt loopback driver, a DHCP header with no client, a RAW pool with no RAW stack, and nine header declarations with no definitions

- **Category:** 1 (dead code) / 7 (build clutter)
- **Locations:**
  - `modules/network/SMSTCPIP/loopif.c` (113 lines) + `include/netif/loopif.h`
    (39) — `loopif.o` is not in the Makefile object list
    (`modules/network/SMSTCPIP/Makefile:11-13`) and `LWIP_HAVE_LOOPIF`
    defaults to 0 and is never enabled (opt.h; grep-verified), so
    `ps2ip.c`'s `AddLoopIF` call compiles its body out too
  - `modules/network/SMSTCPIP/include/lwip/dhcp.h` (212 lines) — a full DHCP
    client API with no `dhcp.c` anywhere in the tree; `LWIP_DHCP 0`
    (lwipopts.h) and `PS2IP_DHCP` is never defined, so the `dhcp_start`/
    `dhcp_stop` branches in `ps2ip.c`'s FULL_LWIP config functions are
    unreachable twice over
  - `modules/network/SMSTCPIP/include/lwip/raw.h` (75) + the
    `MEMP_NUM_RAW_PCB` pool slots reserved in `memp.c:95-118` — `LWIP_RAW`
    is never defined anywhere in the repo (grep-verified), so no raw PCB
    can ever be allocated; the pool bytes are pure waste on a 2 MB IOP
  - Declared but never defined (grep-verified zero `.c` hits for each):
    `tcp_rexmit_seg` (include/lwip/tcp.h), `tcpip_callback`
    (include/lwip/tcpip.h), `memp_realloc` (include/lwip/memp.h),
    `netbuf_alloc`/`netbuf_data`/`netbuf_chain`/`netbuf_next` and
    `netconn_err` (include/lwip/api.h:70,90,115,124,150 region),
    `pbuf_ref_chain`/`pbuf_dechain` (include/lwip/pbuf.h:108,114),
    `sys_now` (include/lwip/sys.h)
  - `modules/network/SMSTCPIP/api_msg.c:665-668` — `do_accept()` is a no-op
    that never ACKs; `API_MSG_ACCEPT` is never posted by any code
    (grep-verified), so the decode-table entry is dead weight
  - `modules/network/SMSTCPIP/udp.c:88-147` (`udp_lookup`) and
    `ip.c:83-118` (`ip_lookup`) — debug-only helpers compiled out unless
    `LWIP_DEBUG`, whose `udp_lookup` returns 1 on *both* the match and
    no-match paths
- **Problem:** ~370 lines of source/header that cannot execute, plus pool
  memory reserved for a compiled-out protocol, invite exactly the
  misreading this stack already suffers from (e.g. NETSTACK-02's stub
  confusion): a reader cannot tell which of loopback/DHCP/RAW/select the
  stack actually has.
- **Fix:** Delete `loopif.c`/`loopif.h`, `dhcp.h`, `raw.h` and the
  raw-pool entry; delete the nine undefined declarations and the dead
  `do_accept` table entry. All deletions, no replacements.
- **Benefit:** Smaller, honest tree; removes the RAW pool's static bytes;
  future readers stop auditing phantom features.
- **Effort:** S
- **Risk:** Low — everything listed is provably unreferenced in-tree; a
  build after deletion is the validation gate.
- **Confidence:** High (READ + grep-verified absence of definitions,
  callers and build references).

### NETSTACK-13 — TCP initial sequence numbers are trivially predictable and IP IDs are a plain counter (inherited)

- **Category:** 6 (security, inherited vintage lwIP — not a local divergence)
- **Locations:**
  - `modules/network/SMSTCPIP/tcp.c` (`tcp_next_iss`, ~line 1075):
    `static u32_t iss = 6510; iss += tcp_ticks; return iss;` — `tcp_ticks`
    advances twice a second, so ISS values are predictable to within a few
    units by anyone who can observe or provoke one connection
  - `modules/network/SMSTCPIP/ip.c` (`ip_output_if`): `static u16_t ip_id`
    incremented by 1 per packet
- **Problem:** Predictable ISS enables blind TCP spoofing/reset injection
  against OPL's SMB/NFS sessions from another LAN host; sequential IP IDs
  make fragment reassembly (NETSTACK-09's surface) easier to poison. Both
  are stock behaviour for this lwIP vintage — the tree neither introduced
  nor aggravated them.
- **Fix:** None proposed under LESS-NOT-MORE: a real fix is additive (an
  entropy source and ISS randomisation) and the threat model is a trusted
  home LAN. Filed so the parent can accept-and-close knowingly rather than
  discover it later.
- **Benefit:** Record completeness only.
- **Effort:** — (no fix proposed)
- **Risk:** — 
- **Confidence:** High (READ).

---

## Checked-clean / dissolved

- **SMSUTILS.S (RIPTOPL-282 cross-reference):** `modules/network/SMSUTILS/SMSUTILS.S`
  (md5 `f7dfc2506cef391477866c8542e6e6b6`) differs from the other three copies
  (ds34bt, ds34usb, cdvdman — all md5 `3612cf1bd66a11f0b9e53ac78a5940d7`) only in the
  IRX wrapper: it adds `_start`/`RegisterLibraryEntries` and the `smsutils` export
  table (SMSUTILS.S:190-216) and drops the `.globl` exports the embedded copies
  carry. The `mips_memcpy`/`mips_memset` bodies are instruction-identical,
  including the filed defect: the exit `jr $ra` at SMSUTILS.S:138 has
  `mips_memset`'s first instruction (`move $v0, $a0`, :141) in its delay slot.
  It is one of the four known copies — **no fifth copy, no divergence in the
  defect** — so nothing new is filed. The two `smsutils.h` files elsewhere in
  the repo are import headers, not copies.
- **No `poll.c` in scope** — see the RIPTOPL-271 note in the header.
- **ps2ips `do_connect` using `_rpc_buffer` instead of the `rpcBuffer`
  parameter (ps2ips.c:~140):** `sceSifRegisterRpc` registers `_rpc_buffer`
  as the receive buffer, so the parameter and the global are the same memory
  in practice. Inconsistency, not a bug. Dissolved.
- **ps2ips `do_getconfig` passing `rpcBuffer` as both the netif name and the
  `t_ip_info*`:** matches the ps2sdk design — `netif_name` is the first field
  of `t_ip_info` (ps2ip_internal.h:20-29) — and the path is FULL_LWIP-gated
  besides. Dissolved.
- **ps2ips `do_recvfrom` `fromlen`:** already fixed locally (initialised
  before the call, RA-fix comment). The remaining addrlen issues in
  `do_accept`/`do_getsockname`/`do_getpeername` are RIPTOPL-080. Not re-filed.
- **ps2ips DNS handlers** (`do_gethostbyname` dereferencing `h_addr_list[0]`,
  `do_dns_getserver`'s unchecked server index): both depend on ps2sdk-side
  `gethostbyname`/`dns_getserver` behaviour that is not in this tree, so no
  in-scope defect can be established. Noted, not filed. INFERRED risk only.
- **mem.c's internal allocator (mem.c:131-390):** dead under
  `MEM_LIBC_MALLOC 1` (lwipopts.h), but it is entirely in the `#else` branch
  — zero static or code cost in the built object. Not a finding.
- **In-game `CHECKSUM_CHECK_IP/UDP/TCP/ICMP = 0` (lwipopts.h, INGAME_DRIVER
  block):** documented deliberate local choice (rely on Ethernet FCS);
  non-in-game builds keep the opt.h default of 1. Not a finding.
- **Static-memory accounting (Category 4, measured from source):**
  `pbuf_pool_memory` = `PBUF_POOL_SIZE × MEM_ALIGN_SIZE(PBUF_POOL_BUFSIZE +
  sizeof(struct pbuf))` ≈ 25 × 1,556 ≈ 38.9 KB in the (unshipped) non-in-game
  configuration, 8 × 1,556 ≈ 12.4 KB in-game (pbuf.c:~97; struct pbuf = 16 B
  per pbuf.h:66-95 — byte total INFERRED from the struct layout);
  `ip_reassbuf` = `IP_HLEN + IP_REASS_BUFSIZE` = 5,780 B stock / 1,556 B
  in-game, plus the reassembly bitmap (ip_frag.c:~100-103); the 1,500-byte
  `ip_frag` staging buffer exists only when `IP_FRAG=1` (excluded in-game);
  `memp_memory` is formula-sized by the pool table (memp.c:84-118, ~2 KB,
  INFERRED); ps2ips statics: `_rpc_buffer` 2,048 B, `lwip_buffer` 1,088 B,
  `rests` 144 B (ps2ips.c:88-94). No reduction candidate is filed: the large
  pools are already config-scaled for the in-game build, and the only pure
  waste found (the RAW pool) is inside NETSTACK-12.
- **Local hardening already present and verified while reading** (do not
  re-file as missing): `lwip_close` preserves the socket/PCB when
  `netconn_delete` fails (sockets.c:~267-285); `lwip_recvfrom`'s SMB
  header/index split carries segment-bound guards (sockets.c:~398-419);
  failed-connect PCB guards in `lwip_getsockname`/`lwip_getsockopt`/
  `lwip_setsockopt`; `do_delconn` frees PCBs left in CLOSED state
  (api_msg.c:~360-365); `do_connect` wakes and frees the PCB on
  SYN-enqueue failure (api_msg.c:~590-610); `recv_tcp` publishes the buffer
  before invoking the recv callback (api_msg.c:~135-145); `err_tcp` orders
  EOF before the readable signal (api_msg.c:~195-205); `ip_reass` bitmap
  completeness bound and +1 bitmap sizing (ip_frag.c:~101-103, ~205-215);
  the `rests_fit` data-loss fix in ps2ips `do_recv`/`do_recvfrom`.
- **Pristine-upstream observations (unnumbered, per the vendored lens):**
  `tcp_parseopt`/`tcp_input` header-length validation gaps; `udp_input` not
  validating the UDP length field against the pbuf; `etharp_arp_input`
  updating the cache from any ARP sender; `tcpip_apimsg` silently dropping
  the message if `MEMP_TCPIP_MSG` allocation fails in the non-locking path
  (compiled out here — core locking is on); `recv_udp` leaking the pbuf
  when `recvmbox` is NULL (unreachable in practice: a UDP PCB has no local
  port before bind/connect creates the mbox); `inet_ntoa`'s static 16-byte
  buffer (documented non-reentrant in inet.h:49).
- **`inet.S` checksum assembly (676 lines, read in full):** the fixed
  `struct pbuf` field offsets it uses (payload at 4, len at 10) match this
  tree's `struct pbuf` (pbuf.h:66-95). Arithmetic correctness of the
  unrolled checksum was **not** execution-verified — stated plainly rather
  than claimed.
- **`tcpip_apimsg` under core locking** runs the decode directly in the
  caller's thread (api_msg.h:98-99, tcpip.c) — the documented design, and
  the reason NETSTACK-08's race window exists but stays narrow. Not itself
  a finding.

---

## Per-file ledger (wc -l; every file below was read in full)

### modules/network/SMSTCPIP/ — 16,720 lines, 57 files

| File | Lines |
|---|---:|
| `modules/network/SMSTCPIP/api_lib.c` | 599 |
| `modules/network/SMSTCPIP/api_msg.c` | 824 |
| `modules/network/SMSTCPIP/etharp.c` | 848 |
| `modules/network/SMSTCPIP/icmp.c` | 199 |
| `modules/network/SMSTCPIP/inet.S` | 676 |
| `modules/network/SMSTCPIP/ip.c` | 404 |
| `modules/network/SMSTCPIP/ip_addr.c` | 38 |
| `modules/network/SMSTCPIP/ip_frag.c` | 337 |
| `modules/network/SMSTCPIP/loopif.c` | 113 |
| `modules/network/SMSTCPIP/mem.c` | 390 |
| `modules/network/SMSTCPIP/memp.c` | 270 |
| `modules/network/SMSTCPIP/netif.c` | 239 |
| `modules/network/SMSTCPIP/pbuf.c` | 802 |
| `modules/network/SMSTCPIP/ps2ip.c` | 570 |
| `modules/network/SMSTCPIP/sockets.c` | 1415 |
| `modules/network/SMSTCPIP/tcp.c` | 1082 |
| `modules/network/SMSTCPIP/tcp_in.c` | 1125 |
| `modules/network/SMSTCPIP/tcp_out.c` | 570 |
| `modules/network/SMSTCPIP/tcpip.c` | 171 |
| `modules/network/SMSTCPIP/timers.c` | 290 |
| `modules/network/SMSTCPIP/udp.c` | 786 |
| `modules/network/SMSTCPIP/Makefile` | 45 |
| `modules/network/SMSTCPIP/exports.tab` | 48 |
| `modules/network/SMSTCPIP/imports.lst` | 57 |
| `modules/network/SMSTCPIP/include/arch/cc.h` | 17 |
| `modules/network/SMSTCPIP/include/arch/sys_arch.h` | 22 |
| `modules/network/SMSTCPIP/include/irx_imports.h` | 27 |
| `modules/network/SMSTCPIP/include/lwip/api.h` | 157 |
| `modules/network/SMSTCPIP/include/lwip/api_msg.h` | 101 |
| `modules/network/SMSTCPIP/include/lwip/arch.h` | 216 |
| `modules/network/SMSTCPIP/include/lwip/debug.h` | 96 |
| `modules/network/SMSTCPIP/include/lwip/def.h` | 46 |
| `modules/network/SMSTCPIP/include/lwip/dhcp.h` | 212 |
| `modules/network/SMSTCPIP/include/lwip/err.h` | 69 |
| `modules/network/SMSTCPIP/include/lwip/icmp.h` | 113 |
| `modules/network/SMSTCPIP/include/lwip/inet.h` | 83 |
| `modules/network/SMSTCPIP/include/lwip/ip.h` | 156 |
| `modules/network/SMSTCPIP/include/lwip/ip_addr.h` | 133 |
| `modules/network/SMSTCPIP/include/lwip/ip_frag.h` | 44 |
| `modules/network/SMSTCPIP/include/lwip/mem.h` | 60 |
| `modules/network/SMSTCPIP/include/lwip/memp.h` | 62 |
| `modules/network/SMSTCPIP/include/lwip/netif.h` | 141 |
| `modules/network/SMSTCPIP/include/lwip/opt.h` | 692 |
| `modules/network/SMSTCPIP/include/lwip/pbuf.h` | 116 |
| `modules/network/SMSTCPIP/include/lwip/raw.h` | 75 |
| `modules/network/SMSTCPIP/include/lwip/snmp.h` | 178 |
| `modules/network/SMSTCPIP/include/lwip/sockets.h` | 259 |
| `modules/network/SMSTCPIP/include/lwip/stats.h` | 153 |
| `modules/network/SMSTCPIP/include/lwip/sys.h` | 194 |
| `modules/network/SMSTCPIP/include/lwip/tcp.h` | 524 |
| `modules/network/SMSTCPIP/include/lwip/tcpip.h` | 91 |
| `modules/network/SMSTCPIP/include/lwip/timers.h` | 101 |
| `modules/network/SMSTCPIP/include/lwip/udp.h` | 105 |
| `modules/network/SMSTCPIP/include/lwipopts.h` | 402 |
| `modules/network/SMSTCPIP/include/netif/etharp.h` | 100 |
| `modules/network/SMSTCPIP/include/netif/loopif.h` | 39 |
| `modules/network/SMSTCPIP/include/ps2ip_internal.h` | 38 |

### modules/network/SMSUTILS/ — 225 lines, 2 files

| File | Lines |
|---|---:|
| `modules/network/SMSUTILS/SMSUTILS.S` | 217 |
| `modules/network/SMSUTILS/Makefile` | 8 |

### modules/network/ps2ips/ — 856 lines, 4 files

| File | Lines |
|---|---:|
| `modules/network/ps2ips/ps2ips.c` | 752 |
| `modules/network/ps2ips/imports.lst` | 59 |
| `modules/network/ps2ips/irx_imports.h` | 27 |
| `modules/network/ps2ips/Makefile` | 18 |

### Totals

- **Total: 17,801 lines across 63 files** (16,720 + 225 + 856).
- **Unread: none.** Every file in the three trees was line-read in full.
- Note on the brief's figure: the task brief estimated ~17,566 lines; the
  ledger above counts every file in the trees including Makefiles, export/
  import tables and all headers, which accounts for the difference.
