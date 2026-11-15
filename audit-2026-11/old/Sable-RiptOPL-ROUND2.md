# Sable Audit — RiptOPL, ROUND 2 (deep line-audit)

- **Repo:** NathanNeurotic/RiptOPL, branch `rebuild/main`
- **Round-2 pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` (3 commits past the pass-1 pin `677812185ac5`: #420 SMB1 VMC share fix, #419 IGR interrupt hold, #417 "Update ELF in place and fix file manager card rename")
- **Scope of this round:** (A) revalidation of pass-1 findings touching the delta files; (B) line-audit of the IOP `modules/` trees and `ee_core/` that pass 1 only inventoried — network/file parsers and RPC handlers read function by function; (C) an exact read/unread ledger.
- **Method note:** this codebase commentates its own hardening extensively, and most grep candidates dissolved on a full read (see Section C "examined and cleared"). Every finding below was verified by reading the complete function at the round-2 head. Line numbers are at the round-2 head. READ ONLY — no repo files were modified; `git status` verified clean at the end.
- **No PS2 build exists in this sandbox** — no size figure below is presented as hardware- or build-verified.

---

## Section A — Revalidation of pass-1 findings touching the delta

| Finding | Verdict at `2e347d5ac885` | Evidence |
|---|---|---|
| RIPTOPL-003 (dead 1,251-line Makefile) | **STILL TRUE — worse** | The Makefile is now **1,260 lines**; the delta itself edited it (+21 lines, mostly smbman comments). `.github/workflows/flavours.yml:401` still states "The Makefile can no longer build src/". |
| RIPTOPL-004 (branch sprawl) | **STILL TRUE** | **277** remote branches at round-2 head (was 273), 5 tags. |
| RIPTOPL-005 (grep-assertion "tests") | **STILL TRUE** | Now **278** test files. `test_selfupdate_host.py` re-run at the new head: **PASS (52 checks)**. The delta's new `test_smb1_vmc_share.py` is the same source-text-scraping pattern (plain `read()` of source files, asserting on substrings) — the pattern pass 1 flagged, extended by one more file. |
| RIPTOPL-008 (UDPFS IOP core ×3) | **CHANGED — pass-1 correction** | The three `udpfs_core.c` / `udprdma.c` files in `udpfs_bd/`, `udpfs_fhi/`, `udpfs_ioman/` were **already 2-line shims** (`#include "../../udpfs_shared/src/udpfs_core.c"`) **at the pass-1 pin** (verified with `git show 677812185ac5:`). Pass 1's "byte-identical copies" wording was technically true of the shim files but misleading: the `.c` bodies are already shared. What is *not* shared is the headers — see new finding RIPTOPL-048. Pass 1 apologises for the framing; the underlying duplication is smaller and different than stated. |
| RIPTOPL-013 (`.bak` written to memory cards) | **STILL TRUE** | `src/sources/vcdsupport.cpp:2206` and `:2287` still contain `snprintf(..., "%s.bak", ...)` (read verbatim at new head). The delta did not touch vcdsupport or vmcrebuild. |
| RIPTOPL-016 (SMB `strcpy` into cdvdman settings) | **STILL TRUE** | Read verbatim at new head: `src/sources/ethsupport.cpp:1192-1194` (`strcpy(smb.user/pass/share, gPCUserName/gPCPassword/gPCShareName)`) and `:1444-1446` (`strcpy(settings->smb_share/smb_user/smb_password, ...)`). |
| RIPTOPL-021 (iLink driver maintained twice) | **STILL TRUE — and larger than pass 1 stated** | Pass 1 cited `iLink.c` only. The whole `modules/bdmassault/` tree (3,955 lines) is a second copy of `modules/ilink/` (3,970 lines) — see new finding RIPTOPL-047. |
| RIPTOPL-034 (commented-out code markers) | **STILL TRUE** | `modules/mmce/mmceman/src/sio2man_hook.c:26` still opens an `#if 0` block at the new head. |

**Delta files with no pass-1 finding — reviewed fresh, stated honestly:**

- **No pass-1 finding was located in `src/ui/selfupdate.cpp`, `src/core/filemgr.cpp`, `ee_core/src/padhook.c` or `ee_core/src/igrmenu.c`.** Their delta mechanisms were read in full this round:
  - #417 self-update: `selfupdate.cpp` shrank 486 → **424 lines**; it now overwrites the booted ELF **in place** (no rename/copy fallback, no backup file — consistent with the no-`.BAK` direction). One design cost remains and is filed as RIPTOPL-050.
  - #417 file manager: `filemgr.cpp` 599 → **636 lines**; new `fmMcRename` (~`:521-546`) renames memory-card entries via mcserv (`mcSetFileInfo`, flag 0x10) with a bounded 4-try loop and `DelayThread(200*1000)` between tries; the leaf-name length is checked. **Read fully — no bug found.**
  - #419 IGR: `ee_core/src/padhook.c` — `IGR_HoldGs` was replaced by a simpler `IGR_WaitIdle`; the held-interrupt toggling dance was deleted. A net simplification; no finding. The `igrmenu.c` delta is a comment-only reference rename (`:943-946`).
  - #420 SMB1 VMC: `modules/iopcore/cdvdman/smb.c:765-769` — read opens now deny-none (0x40), writable VMC opens deny-write (2|0x20), with an explanatory comment; `modules/network/smbman-ra/smb.c` — NTCreateAndX ShareAccess is now 0x03 for reads (was 0x01) at ~`:876-882`, and `smb_LegacyOpenAndX` gained a `deny_none` parameter (~`:939+`). The Makefile comment block (~`:261-290`, `:986-1018`) documents making smbman-ra fork-wide for this. The mechanism reads correctly; its *parser* surroundings produced RIPTOPL-038 and RIPTOPL-040 below.

---

## Section B — New findings (RIPTOPL-037 …), sorted by benefit

## RIPTOPL-037 / NBNS receive thread can be overflowed by any host on the LAN / Category 6
- **Location(s):** RiptOPL/modules/network/nbns/nbns.c:110-122 (`decode_name`), :177-189 (call site and its only length gate), :198 (fixed-offset `memcpy`)
- **Problem:** `decode_name` loops `for (i = 0; i < name[0]; i += 2, pOut++, p += 2)`, writing one byte per iteration into the caller's stack buffer `char decoded_name[17]` in the receive thread. `name[0]` is a packet-supplied byte: as a signed char it can request up to ~64 output bytes — roughly 47 bytes past the end of a 17-byte stack buffer, inside the NBNS thread. The only gate before the call is `length >= sizeof(struct NbHeader)` (12 bytes); nothing checks that the length byte is 32 (the only legal NetBIOS first-level encoding length) or that the datagram is long enough to hold the name it claims. Separately, `:198` copies 4 address bytes from `frame[58..61]` with no `length >= 62` check, so a short datagram reads stale receive-buffer bytes as an address. NBNS listens on a bound UDP socket: any device on the LAN can send it one datagram.
- **Fix:** In words: before decoding, reject a response whose encoded-length byte is not 32, and reject any datagram shorter than the fixed response layout (through the address field). Two comparisons at the existing gate; no new structure.
- **Benefit:** Removes a remotely triggerable IOP stack overwrite — the highest-severity defect found in either round.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — full function and call site read)

## RIPTOPL-038 / smbman-ra copies the server-supplied filename length with no bound / Category 6
- **Location(s):** RiptOPL/modules/network/smbman-ra/smb.c:336-352 (`getStringField`), :288-318 (`utf16ToAscii` loop), :1498 (call site), :404 (second call site, `inbytes == 0`); RiptOPL/modules/network/smbman-ra/smb.h:310-314 (`SearchInfo_t` ends in flexible `char FileName[]`); RiptOPL/modules/network/smbman-ra/smb_fio.c:88, :694 (the 4,096-byte static `SearchBuf` it lands in)
- **Problem:** `smb_FindFirstNext2` passes the server response's `FileNameLen` (a u32 the server chooses) straight into `getStringField`, which `strncpy`s that many bytes into `info->FileName` and writes a terminator at `out[inbytes]` — no check against the size of `SearchBuf`, the bytes actually received, or anything else. The Unicode branch is the same shape: `utf16ToAscii` writes one output byte per two input bytes until `inbytes` is consumed, unbounded. A broken or hostile SMB server — or a corrupted response — writes arbitrarily far past a static IOP buffer. The SMB2 driver in the same repo does this correctly (`smb2man.c` clamps `namelen` to the bytes actually received before converting), so the fix pattern already exists in-tree.
- **Fix:** In words: clamp the incoming name length to the smaller of the response bytes actually received and the space remaining in `SearchBuf` after the `SearchInfo_t` header, in `getStringField` (both branches), mirroring the smb2man clamp.
- **Benefit:** Removes a server-controlled unbounded write in the menu-side SMB1 stack used for browsing and launching.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RIPTOPL-039 / MMCE game-ID is `strcpy`'d unbounded in both directions / Category 6
- **Location(s):** RiptOPL/modules/mmce/mmceman/src/mmce_cmds.c:196-225 (`mmce_cmd_get_gameid`), :228-241 (`mmce_cmd_set_gameid`); caller RiptOPL/modules/mmce/mmceman/src/mmce_fs.c:1123 (devctl entry at :1075)
- **Problem:** Get: `strcpy(ptr, &rdbuf[4])` copies from a fixed 255-byte card-supplied packet with no guaranteed NUL terminator and no length — the devctl handler receives `buflen` but never passes it to the callee, so a card response without a NUL in the right place copies until the IOP heap happens to hold one. Set: `u8 len = strlen(ptr) + 1` truncates mod 256, then `strcpy(&wrbuf[4], ptr)` writes an EE-supplied string into `u8 wrbuf[0xFF]` on the stack — any string longer than 251 bytes overflows it, and the length byte written to the packet disagrees with the bytes copied whenever the string exceeds 255.
- **Fix:** In words: pass the devctl buffer length into the get handler and copy with that bound (requiring a NUL within the packet); in the set handler, reject strings that do not fit the 251-byte payload instead of truncating the length byte.
- **Justification (fix touches two signatures):** the bound has to come from somewhere; the length already exists at the devctl boundary and is simply not forwarded. No new mechanism.
- **Benefit:** Removes a card-controlled and an EE-controlled overflow pair in the newest storage driver, on the game-ID path the launcher arms at boot.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RIPTOPL-040 / smb_fio builds paths with `strncpy` and never terminates them / Category 6
- **Location(s):** RiptOPL/modules/network/smbman-ra/smb_fio.c:661-663 (`smb_dopen`), :822 (`smb_chdir`), :297-327 (`prepare_path`, consuming site at :303)
- **Problem:** The same defect in three places in one file. `smb_dopen`: `strncpy(fh->name, path, 255)` into `FHANDLE.name[256]`, then immediately `fh->name[strlen(fh->name) - 1]` and two unbounded `strcat`s (`"\\"` then `"*"`). `prepare_path` can legitimately produce paths up to 1,023 chars (`SMB_PATH_MAX` 1024), so a directory path of 255+ chars leaves `name` unterminated (the handle slots are recycled globals, not zeroed per use): `strlen` reads past the field and the `strcat`s write past it. `smb_chdir`: `strncpy(smb_curdir, path, sizeof(smb_curdir) - 1)` with no terminator written — a 1,023-char current directory leaves `smb_curdir` unterminated for `prepare_path`'s `strncpy(..., max_path - 3)` + `strcat` at :303, which then scans and appends past both 1,024-byte statics.
- **Fix:** In words: terminate explicitly after each bounded copy (write the NUL at the last byte of the destination), and have `smb_dopen` reject a path that leaves no room for the `"\\*"` suffix instead of appending blindly.
- **Benefit:** Removes a path-length-triggered overflow in the menu SMB1 filesystem layer; deep directory trees on shares are a normal case, not an exotic one.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RIPTOPL-041 / cdvdman `.PSS` check indexes before the buffer for short filenames / Category 6
- **Location(s):** RiptOPL/modules/iopcore/cdvdman/searchfile.c:231-235
- **Problem:** When `IOPCORE_COMPAT_0_SKIP_VIDEOS` is set, the code compares `&cdvdman_filepath[strlen(cdvdman_filepath) - 6]` against `".PSS"`. A game-supplied search name shorter than 6 characters makes the size_t subtraction wrap and the pointer land before the buffer; the 4-byte compare then reads out of bounds (and on the wrong bytes, so the compat behaviour is also wrong for short names). The rest of `searchfile.c` was recently hardened thoroughly — record lengths, name clamps, bounded copies are all present with comments — which makes this surviving arithmetic slip stand out.
- **Fix:** In words: only run the suffix test when the name is at least 6 characters long.
- **Benefit:** Removes an out-of-bounds read on a game-controlled string in the hot disc-search path.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — whole file read)

## RIPTOPL-042 / APA ioctl2 handlers index the partition table with an unchecked caller-supplied index / Category 6
- **Location(s):** RiptOPL/modules/hdd/apa/src/hdd_fio.c:996 (`HIOCGETSIZE`), :1013-1016 (`HIOCGETPARTSTART`, gated by `APA_SUPPORT_IOCTL_GETPARTSTART` = 1 at RiptOPL/modules/hdd/apa/src/apa-opt.h:13); table size `APA_MAXSUB` 64 + 1 at RiptOPL/modules/iopcore/cdvdman/atad.h:61
- **Problem:** Both handlers compute `fileSlot->parts[*(u32 *)argp]` with the index taken verbatim from the ioctl2 argument. Nothing checks it against the 65-entry `parts[]` array. An out-of-range index reads arbitrary adjacent IOP memory and returns a word of it to the EE caller as a partition "length" or "start" — an information leak across the IOP/EE boundary, and a wild read if the index is large enough to leave mapped memory.
- **Fix:** In words: return an error when the supplied index exceeds `APA_MAXSUB`, in both cases (they sit in the same switch).
- **Benefit:** Closes an unbounded indexed read in the HDD driver's EE-facing interface.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RIPTOPL-043 / UDPFS trusts the server's per-read result count without clamping it to the request / Category 6
- **Location(s):** RiptOPL/modules/network/udpfs_shared/src/udpfs_core.c:96-139 (`_recv_with_result`), :471-505 (`udpfs_core_read`)
- **Problem:** `_recv_with_result` returns `result.result` — a value from the server's reply packet — without checking it against the byte count the client asked for. `udpfs_core_read` then advances the caller's buffer pointer and its total by that value. The DMA receive itself is bounded elsewhere (udprdma's own return is clamped to the transfer size), so the wire cannot overrun the buffer directly; what is corrupted is the *accounting*: a server (or a spoofed reply — UDPFS has no authentication, per pass 1 / the PS2-Servers audit) reporting more bytes than were delivered makes every consumer believe it holds data it never received, and advances stream position past reality. The neighbouring `udpfs_core_dread` shows the intended pattern — it validates `name_len` against the caller's maximum before copying.
- **Fix:** In words: clamp the returned result to the requested size in `_recv_with_result` (or reject a reply that claims more), the same way `dread` validates before trusting.
- **Benefit:** One comparison removes a server-controlled desynchronisation of every UDPFS read stream.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — whole file read)

## RIPTOPL-044 / UDPFS `dread` validates a server name length against the buffer size, but not against the bytes received / Category 6
- **Location(s):** RiptOPL/modules/network/udpfs_shared/src/udpfs_core.c (`udpfs_core_dread`); sole caller RiptOPL/modules/network/udpfs_ioman/udpfs_ioman.c:347 (name_max = 256)
- **Problem:** `dread` checks the reply's `name_len` against the caller's 256-byte maximum, but not against how many bytes the reply packet actually contained. The copy source stays inside the static 312-byte `g_rx_buf`, so this cannot overflow anything; what it can do is copy stale bytes from a previous, longer reply into a directory entry name when a short or malformed reply claims a longer name than it carries.
- **Fix:** In words: also bound `name_len` by the received packet length minus the reply header, before the copy.
- **Benefit:** Small correctness win; directory listings stop showing garbage names on malformed replies. Filed mainly so the UDPFS reply-validation picture is complete next to RIPTOPL-043.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ), impact Low — stated plainly so it is not oversold

## RIPTOPL-045 / Streaming DMA wait is an empty busy-spin, next to code that was fixed for doing exactly that / Category 5
- **Location(s):** RiptOPL/modules/iopcore/cdvdman/streaming.c:234-235
- **Problem:** `while (sceSifDmaStat(dmat_id) >= 0) {};` spins with no yield, no sleep and no timeout while a streaming DMA completes. A few lines earlier, the neighbouring `sceSifSetDma` loop (~:215-232) was deliberately rewritten to yield, with a comment explaining why spinning in this module starves the IOP. This one wait was left behind. On the IOP, a non-yielding spin at thread priority blocks everything scheduled below it for the duration of the transfer.
- **Fix:** In words: wait the same way the neighbouring loop now does (yield/sleep inside the wait), reusing the pattern already established in the same function.
- **Benefit:** Removes a known-harmful spin the maintainers already diagnosed once, in the module every game-read passes through.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ)

## RIPTOPL-046 / Two complete SMB1 client stacks are maintained in parallel, sharing 19 functions / Category 2
- **Location(s):** RiptOPL/modules/iopcore/cdvdman/smb.c (1,177 lines, in-game) and RiptOPL/modules/network/smbman-ra/smb.c (1,640 lines, menu); identical-except-comment-spacing examples: `asciiToUtf16` (cdvdman :429 / smbman-ra :270), `setStringField` (:447 / :322), the `nb_*` NetBIOS helpers (cdvdman :98-117 and counterparts)
- **Problem:** Both files define 36 functions; **19 names exist in both**, and the sampled common functions are character-identical apart from comment spacing. Every protocol fix — including the #420 share-access change in this very delta — must be made twice, in two dialects of the same code, and RIPTOPL-038 exists in one stack precisely because a hardening applied elsewhere never propagated here. The pair is also only half the picture: `smb2.c` (in-game SMB2, same exported names) and `smb2man` repeat the same menu/in-game split for SMB2.
- **Fix:** In words: move the genuinely common SMB1 helpers (string-field conversion, NetBIOS session helpers, header plumbing) into a shared include compiled by both modules. The repo already shares IOP code exactly this way: `modules/network/common/httpstream.inc` is included by both `modules/iopcore/cdvdman/device-http.c:49` and `modules/network/httpclient/httpclient.c:402`, and `unfs4.c` is shared by inclusion between nfsman and cdvdman's NFS device.
- **Justification (fix adds a shared file):** it *removes* one of two copies of ~19 functions and is the established in-repo pattern, not a new mechanism. Honest caveat: the two stacks link into different IOP images, so a shared library is not directly linkable — the `.inc` inclusion pattern is the applicable shape, and per-stack divergences (VMC writes, share modes) stay per-stack.
- **Benefit:** Every future SMB1 fix lands once; the class of bug where one stack is hardened and the other is not (RIPTOPL-038) stops recurring.
- **Effort:** L · **Risk:** Med · **Confidence:** High (READ — function inventories compared mechanically, sampled bodies diffed)

## RIPTOPL-047 / `modules/bdmassault/` is a near-total second copy of `modules/ilink/` (expands RIPTOPL-021) / Category 2
- **Location(s):** RiptOPL/modules/bdmassault/ (3,955 lines of C/H) vs RiptOPL/modules/ilink/ (3,970 lines); `sbp2_driver.c` differs by 68 diff-lines between the copies, `scsi.c` by 187 (include paths and formatting, plus real drift); RiptOPL/modules/bdmassault/PROVENANCE.md documents the embedded-driver-pair purpose
- **Problem:** Pass 1 flagged the duplicated `iLink.c`; the duplication is actually the entire IEEE1394_bd + iLinkman tree, carried twice so bdmassault can embed a matched pair. The copies have already drifted by ~255 diff-lines across just the two files sampled, so a fix to the SBP-2 or SCSI layer in one tree silently does not exist in the other.
- **Fix:** In words: if both trees must build, generate the assault variant from the ilink sources with its different defines/include paths at build time (the build system already parameterises modules per edition) rather than storing a second edited copy of every file.
- **Justification:** no new code is proposed — the fix deletes a stored copy and reuses sources that already exist.
- **Benefit:** ~3,900 lines of duplicated driver stop drifting; FireWire fixes apply to both products at once.
- **Effort:** L · **Risk:** Med · **Confidence:** High (READ — trees inventoried, two largest files diffed)

## RIPTOPL-048 / UDPFS headers are still stored three times (the corrected remainder of RIPTOPL-008) / Category 2
- **Location(s):** `udpfs_packet.h` (262 lines), `udprdma.h` (152), `udpfs_core.h` (81), `udprdma_packet.h` (135), `udprdma_seq.h` (14), `irx_imports.h` (18) — each md5-identical across RiptOPL/modules/network/udpfs_bd/, udpfs_fhi/ and udpfs_ioman/; `mprintf.h` (23 lines) additionally present in 6 module include dirs (udpfs_bd, udpfs_fhi, udpfs_ioman, ministack, smap_udpbd, udpfs_smap)
- **Problem:** The `.c` bodies were unified into `udpfs_shared` (see Section A), but ~662 lines of headers still exist in three identical copies (plus a sixth-copy debug header), so a packet-layout change must be edited in three places to stay consistent — in the one protocol where the three editions disagreeing with each other is a live failure mode.
- **Fix:** In words: point the three modules' include paths at `udpfs_shared`'s headers and delete the per-directory copies — the per-dir Makefiles already reach into `../udpfs_shared/src/` for the sources.
- **Benefit:** ~1,300 lines of duplicate header deleted; one packet definition for one protocol.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — md5-verified identical)

## RIPTOPL-049 / The SIO2 hook layer is duplicated between the MMCE and MX4SIO drivers / Category 2
- **Location(s):** RiptOPL/modules/mmce/mmceman/src/sio2man_hook.c (318 lines) vs RiptOPL/modules/mx4sio_bd/src/sio2man_hook.c (315 lines) — **5 diff-lines total**; RiptOPL/modules/mmce/mmceman/src/ioplib.c and RiptOPL/modules/mx4sio_bd/src/ioplib.c — md5-identical, 101 lines each, headers likewise identical
- **Problem:** Both SIO2-attached storage drivers carry their own copy of the sio2man hooking layer and the IOP-library shim; the hook copies differ by five lines, i.e. they are one file maintained twice (this is the same file whose `#if 0` block pass 1 flagged in RIPTOPL-034 — the MX4SIO copy has its own history). A hook fix for one SD-card driver does not reach the other.
- **Fix:** In words: keep one copy of `ioplib.c` and the hook in a shared location both modules include (the `.inc`/shared-source pattern used elsewhere in these trees), with the five differing lines parameterised by the defines the two builds already pass.
- **Benefit:** ~400 duplicated lines removed; SIO2 hook behaviour stays identical across both card drivers by construction.
- **Effort:** M · **Risk:** Low · **Confidence:** High (READ — diffed and md5-checked)

## RIPTOPL-050 / Self-update holds the entire replacement ELF in EE RAM to verify it / Category 4
- **Location(s):** RiptOPL/src/ui/selfupdate.cpp:11 (design comment), :187 (body sink allocates the Content-Length), ~:90 (`SU_ELF_MAX` = 8 MB cap; the comment at :11 puts the real ELF at ~2.3 MB)
- **Problem:** The downloader buffers the whole update in RAM before the SHA-256 check allows the in-place overwrite. That is a deliberate trade — verification must complete before the only copy of the running program is replaced, and the new in-place design (Section A) rightly refuses a second file or a backup — but ~2.3 MB of the EE's 32 MB is held for the duration of every update, under an 8 MB cap that would let a future larger ELF hold a quarter of RAM.
- **Fix:** In words: if this ever needs to shrink, verify in two passes — hash the download streaming through a small fixed buffer (discarding the body), then download a second time writing straight to the file. That trades network time for RAM and changes the failure modes, so it is a judgement call for the maintainer, not an obvious win; doing nothing is defensible. Filed so the cost is on the record with the reasoning.
- **Benefit:** Potentially ~2.3 MB of EE RAM freed during updates; alternatively, an explicit decision that the current design stands.
- **Effort:** M · **Risk:** Med · **Confidence:** Med (READ; the trade-off is genuinely two-sided — rated honestly rather than as a defect)

## RIPTOPL-051 / mcemu's read/write size guard can never fire / Category 1
**Verification note (2026-11-14, full verification): WITHDRAWN — REFUTED on adversarial re-derivation: `size` is `u32`, so `size <= 0` at mcemu.c:845,896 is a live `size == 0` exit that fires when `size -= csize` consumes the remainder — the guard is functional, not dead; see Sable-VERIFY2-RIPTOPL.md. Do not act on this finding.**
- **Location(s):** RiptOPL/modules/mcemu/mcemu.c:845 and :896 (`MceRead` / `MceWrite`)
- **Problem:** Both functions guard with `if (size <= 0)` on a `u32` size — a comparison that is always false for an unsigned value, so the guard is dead and reads as protection that does not exist. Analysis of the surrounding arithmetic shows no underflow results today (the clamped chunk size stays ≤ size), so this is not an exploitable bug; it is two lines of dead defence in a module that emulates memory cards for running games.
- **Fix:** In words: delete the two dead comparisons (LESS: remove, don't replace), or — if a zero-size early-out was the intent — test for equality with zero, which is the only case the current code could ever have meant.
- **Benefit:** Tiny but real: the VMC hot path stops pretending to check something it doesn't.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — both functions read in full; harmlessness verified, not assumed)

---

## Section C — Read / unread ledger

### Fully read this round (line by line)
- `src/ui/selfupdate.cpp` (424 lines, round-2 version, complete)
- `modules/network/udpfs_shared/src/udpfs_core.c` (791, complete)
- `modules/network/nbns/nbns.c` (371, complete)
- `modules/iopcore/cdvdman/searchfile.c` (316, complete)
- `modules/network/common/httpstream.inc` (complete)
- `src/core/filemgr.cpp` — delta and the new `fmMcRename` in full; remainder from pass-1 sampling
- Complete functions/ranges for every finding and clearance above, including: mmce_cmds.c game-ID handlers + mmce_fs.c devctl, hdd_fio.c ioctl switch, streaming.c DMA waits, smbman-ra smb.c string-field + FindFirstNext2 path, smb_fio.c prepare_path/dopen/chdir/dread, mcemu.c MceRead/MceWrite, tar.cpp cache+parse path, smb2man.c connect/dread path.

### Examined and CLEARED — read fully, deliberately not flagged
- `modules/isofs/isofs.c:243` strcpy — guarded by a preceding length check; the slash branch uses bounded strncpy.
- `modules/iopcore/cdvdman/device-smb.c` sprintf sites (:115-138) — destination bound is documented and computed (max 215 of 256) at `modules/iopcore/common/cdvd_config.h:92`, with a menu-side check in ethsupport.
- cdvdman `smb.c` receive path — explicit NBSS length rejection and KeyLength checks; its poll loop sleeps with backoff. `smb2.c` receive loop drains correctly and its negotiate path is timeout-bounded by design (documented in its header).
- `modules/network/common/httpstream.inc` — exemplary: URI/host length and control-character checks, overflow-checked decimal parsing, chunked/unknown encodings refused.
- `modules/network/httpclient/httpclient.c` — the 512-byte request buffer is bounded in practice by the RPC struct field sizes (`httpclient.h:31-33`), and the EE side NUL-terminates all three fields (`src/platform/httpclient.cpp:66-79`).
- `modules/network/nfsman/nfsman.c` connect devctl — validates NUL termination with `memchr` before any copy. `unfs4.c` — readdir and read/write results are length-checked; it is *shared by inclusion* between nfsman and cdvdman (the good pattern RIPTOPL-046/049 point at).
- `searchfile.c` locatefile — recently hardened (record-length checks, filename clamp, bounded copies, nameless-record fix, all commented).
- `modules/vmc/genvmc/genvmc.c:408-414` — allocation result checked before use. `cdvdman.c` init cache and read-cache window logic — checked, sound; cdvdman threads block on semaphores rather than polling.
- `udpfs_shared/src/udprdma.c` — wait loops use WaitEventFlag (proper yields) at :373 and :659.
- `src/gfx/themes.cpp:1051` sprintf — destination allocated to the exact computed length at :1049-1050. `src/sources/supportbase.cpp` games.bin cache load — allocation and exact-count fread both checked (:168-215).
- `src/platform/tar.cpp` — cache entry-count validated by division against overflow, raw sizes re-capped on load; hardened with explanatory comments.
- `modules/network/smb2man/` — name lengths clamped to received bytes; credential buffers matched sizes with bounded conversion.
- ee_core IGS screenshot path building (`igs_api.c`) — GameID is capped at 16 chars by `coreconfig.h:7`, keeping the 64-byte path buffer safe.
- Dead-code sweep (static functions with ≤1 in-file reference across modules/iopcore, mcemu, vmc, mmce, udpfs, nfsman, isofs, ee_core/src): the only candidates, `device-http.c`'s `RecvBounded`/`SendData`, are both used via the httpstream.inc inclusion. **No dead functions found in the swept trees** — a clean negative result, recorded as such.

### Still unread / not line-audited (honest remainder)
- Most of `ee_core/src` beyond delta hunks and spot ranges: `igrmenu.c` (1,134) and `padhook.c` (1,096) read only at the #419 delta; `patches.c` (1,002), `igs_api.c` (790), `ra.c`, and the `freeze_diag*` files not read end-to-end.
- IOP files read only at candidate ranges, not in full: `smb2man.c` (1,780) outside its connect/dread paths, `ps2atad.c` (1,646), `hdd_fio.c` (1,247) outside the ioctl switch, `mmce_fs.c` (1,238) outside devctl, `nfsman.c` (1,407) outside connect/dread, `raudup.c` (1,308) spot ranges only, `genvmc.c` (947) outside the allocation range, `smb_fio.c` (1,130) outside the cited functions.
- Vendored stacks excluded by scope, as in pass 1: SMSTCPIP/lwIP (`sockets.c` 1,415 etc.), FatFs, mbedTLS.
- The large EE `src/` files remain sampled/targeted, **not** read end-to-end: `guisettings.cpp` (3,497), `menusys.cpp` (3,400), `hddsupport.cpp` (3,383), `vcdsupport.cpp` (3,259), `supportbase.cpp` (3,181), `settingsio.cpp` (2,611), `dialogs.cpp` (2,409), `bdmsupport.cpp` (2,408), `themes.cpp` (2,247), `favsupport.cpp` (1,833), `config.cpp` (1,740), `ethsupport.cpp` (1,664), `udpfssupport.cpp` (1,280). Mitigating fact, verified by grep this round: all of `src/*.cpp` contains only 6 `malloc/calloc` call sites and 29 raw `sprintf` sites, and every one of those sites is either covered by a pass-1 finding (RIPTOPL-015/016/017), revalidated in Section A, or cleared above.
- `modules/iopcore/cdvdman/` device glue beyond the cited files (bdm/hdd/udpfs devices), `ds34bt`/`ds34usb`, `smap-ingame`, `ps2ips.c` (carries extensive in-tree bounds commentary that was not independently verified), `modules/usb/` glue.

### Verification
- `git status --porcelain` in the clone: **empty** (read-only rule held). HEAD unchanged at `2e347d5ac885cd0ff73e9ad4143781946396f492`.
- No build or hardware test was run or claimed; every finding above is a code-read result and is labelled as such.
