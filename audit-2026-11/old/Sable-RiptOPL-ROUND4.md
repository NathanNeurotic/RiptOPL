# Sable Audit — RiptOPL — ROUND 4

- **Repo:** NathanNeurotic/RiptOPL, branch `rebuild/main`.
- **Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` — **unchanged.** Verified at round start: `git ls-remote origin rebuild/main` returned exactly the round-3 pin and the clone's HEAD matched it; no fetch, no delta, and therefore no delta-driven revalidation duties. The clone was left with a clean `git status` and HEAD at the pin (re-verified at round end).
- **Rules:** READ ONLY in the repo (no edits/commits/pushes/PRs/branches/tags/releases/workflows). LESS NOT MORE governs every fix. Exact `path:line` at this pin; READ vs INFERRED labelled; secret-like values never printed (none encountered in this repo's scope this round). **No PS2 build exists in this VM (no PS2SDK) and nothing in this report is hardware-verified** — per the project's standing rule a build is not proof; Category 5 remains structural everywhere.
- **Scope:** Round 3's still-unread list, in its order — Task 1: the ten largest unread EE `src/` files end-to-end (22,632 lines); Task 2: the `modules/` remainders (ds34 pairs line-diffed, smb_fio.c, mcemu.c, usb_mass.c, mx4sio.c, the cdvdman ATAD pair, ps2ips.c with a claim-by-claim verification of its bounds commentary, apa.c, password.c, udpfs_ioman.c, SMAP ×3); Task 3: the `ee_core/src` remainders in full (2,176 lines), which completes `ee_core/src` across rounds 2–4.
- **Method, stated honestly:** six section agents each read their assigned files 1..EOF and produced full section reports (preserved in `round4-sections/riptopl-r4-*.md`); Task 3 was read personally by the parent agent; the parent then spot-verified the headline mechanism of every section against the clone (all reproduced) and personally traced the one candidate a section left unresolved (R4-1a-13, `sbRename` — dissolved, see Checked-and-clean). Section findings are therefore section-read and parent-spot-checked, not all parent-re-read line by line.
- **Result: 79 new findings, RIPTOPL-066 … RIPTOPL-144**, from 80 candidates (one dissolved on the parent's trace). Running audit total for RiptOPL: **144 findings** (36 + 15 + 14 + 79 across rounds 1–4, with round overlaps as previously recorded).

## Revalidations (prior findings touched by this round's files)

- **RIPTOPL-013** (.bak backups, vcdsupport.cpp): still true, no line drift — `vcdsupport.cpp:2206`, `:2287`, retain-on-failed-restore comment at `:2326`.
- **RIPTOPL-016** (ethsupport launch-leg SMB `strcpy`): still true at this pin, unchanged in substance.
- **RIPTOPL-019** (supportbase scan probe): still true — the folder-browse probe is `opendir(fullpath)` at `supportbase.cpp:397`, at the edge of the filed range.
- **RIPTOPL-044** (UDPFS dread): its `udpfs_ioman.c:347` call site was read this round and is **cleared on the ioman side** — `strncpy(dirent->name, name, 255)` plus explicit NUL at `[255]`; the core-side defect stands as filed and is not repeated.
- **Round-2 cleared item** (games.bin cache load, supportbase.cpp:168-212): revalidated clean — allocation + exact-count `fread` check intact. (Its *path composition* is a new defect — RIPTOPL-089.)

## Revisions

None. No round 1–3 finding was refuted by this round's evidence. RIPTOPL-128 extends RIPTOPL-052's established deletion (unreachable partition-edit machinery) to its other half in `apa.c` under a new number, following the RIPTOPL-047 precedent; RIPTOPL-052's text itself is unchanged.

## ps2ips.c bounds commentary — verification verdict (assignment requirement)

Every bounds/safety claim in `modules/network/ps2ips/ps2ips.c`'s own commentary was located and re-derived (full table in `round4-sections/riptopl-r4-2c-atad-ps2ips-apa-smap.md`): **13 claims examined — 11 VERIFIED, 0 REFUTED, 1 UNVERIFIABLE in-repo** (V11, the `fd_set` byte-identity cast — both layouts live in external PS2SDK/lwIP headers). Sub-claims inside V1, V5, V9 and V10 rest on external EE/lwIP facts and are flagged INFERRED there. The file's bounds commentary is, on this evidence, unusually accurate — and notably, all three ps2ips findings this round (RIPTOPL-067, -080, -100) sit in code the commentary does **not** claim is safe; two were already recorded as seen-but-unfixed in `docs/PS2SDK-NOTES.md`.

---

## Findings (benefit order)

### RIPTOPL-066 — BT DS4 output report: buffer zeroed with the DS3 length, sent with the DS4 length — in both Bluetooth copies
- **Category:** 6 (bugs/security)
- **Locations:** `modules/ds34bt/iop/ds34bt.c:1208` (buffer `led_buf[PS4_11_REPORT_LEN + 2]` = 79 B), `:1243` (`mips_memset(led_buf, 0, PS3_01_REPORT_LEN + 2)` = 50 B), `:1263` (send length 79); same defect in `modules/pademu/ds34bt.c:1146,1179,1198`. Constants READ from both headers: `PS3_01_REPORT_LEN` = 48, `PS4_11_REPORT_LEN` = 77.
- **Problem:** This is the exact POPSLOADER-048 defect class, present here in **both** of RiptOPL's BT driver copies. The DS4 branch of the LED/rumble sender zeroes only 50 of the 79-byte report buffer, assigns only indices 0–13, and transmits all 79 bytes — indices 50–78 (29 bytes) are uninitialised IOP stack sent to the controller in every DS4 LED/rumble report. Contrast: both USB copies memset the whole buffer before sending, and the BT DS3 branch in both copies is correct (48-byte template, send 50).
- **Fix (words):** Zero the full `led_buf` (its declared size) in the DS4 branch of both BT copies. One constant per copy; do not shrink the send length — the DS4 report is 77 bytes.
- **Benefit:** Stops leaking 29 stack bytes per report to the controller; the report tail becomes deterministic.
- **Effort:** S · **Risk:** Low (the bytes are supposed to be zero) · **Confidence:** High (READ — arithmetic fully determined by the quoted lines and header constants).

### RIPTOPL-067 — ps2ips `do_setsockopt` copies an EE-supplied length into a 128-byte stack buffer with no bound
- **Category:** 6 (bugs/security)
- **Locations:** `modules/network/ps2ips/ps2ips.c:583-596`; copy at `:592` (`memcpy(optval, pkt->buffer, optlen)`), `optlen` taken unchecked from the RPC packet at `:591`, destination `unsigned char optval[128]` at `:587`.
- **Problem:** Nothing clamps `optlen` to 128 (or to the request payload) before the `memcpy`, so a request with `optlen > 128` overwrites the RPC thread's stack (0x800 bytes) past `optval` by a caller-controlled amount. The caller is the console's own EE code, so this is a robustness/integrity hole rather than a remote exploit; it converts any EE-side bug into IOP memory corruption inside the network stack. The file's receive path was otherwise hardened to exactly this standard (see the verdict above). The defect is already recorded as seen-but-unfixed in `docs/PS2SDK-NOTES.md` §ps2ips; it has never carried a finding ID until now.
- **Fix (words):** Refuse the call when `optlen` exceeds `sizeof(optval)`, returning an error the way the function's other results are returned, before the copy. One comparison; nothing added.
- **Benefit:** Closes the only unbounded EE→IOP copy left in the RPC server.
- **Effort:** S · **Risk:** Low (no legitimate sockopt value for lwIP-supported options approaches 128 bytes) · **Confidence:** High (READ).

### RIPTOPL-068 — cdvdman `atad.c`: the PIO transfer function returns success before transferring anything; ~45 lines below it are unreachable
- **Category:** 6 (bugs/security) / 1 (dead code) / 8
- **Locations:** `modules/iopcore/cdvdman/atad.c` — `ata_pio_transfer` `:470-516`, early exit at `:473-474`; sole caller is the PIO branch of `sceAtaWaitResult` at `:615`.
- **Problem:** The function opens with a register write and `return 0;`. Everything after — status/ERR check, DRQ check, the PIO-in and PIO-out loops (`:476-516`) — can never execute. Every non-DMA command type routes through it per block, including IDENTIFY DEVICE (table type 0x02) and SMART (type 0x07): such a command completes with success while the caller's buffer is never filled and nothing is ever written to the device. The hdpro sibling's copy of the same function has no early return and performs the transfer with a checksum — the two ATAD variants in the same directory disagree about whether PIO works. Reachability today is latent (READ): no in-tree cdvdman caller issues a PIO-type command — `device-hdd.c` uses DMA types and flush only — so the trap fires on the first future PIO path, and fails silently (success + stale buffer), the worst shape for a probe.
- **Fix (words):** Decide once. If cdvdman is DMA-only by design, delete the dead body and make the PIO branch return `ATA_RES_ERR_CMD` for types it cannot service, so the refusal is explicit. If PIO is meant to work, delete the two early-exit lines and let the complete existing body run. Do not keep a success-returning stub over a full implementation.
- **Benefit:** Removes a silent-success trap from the in-game ATA driver and ~45 lines of review surface (deletion branch), or restores IDENTIFY/SMART (repair branch).
- **Effort:** S · **Risk:** Low for deletion (no live caller changes behaviour); Medium for repair (PIO timing wants a hardware bench pass — not attempted) · **Confidence:** High on deadness and inter-variant divergence (READ); INFERRED whether the stub was deliberate — the tree records no reason.

### RIPTOPL-069 — mcemu trusts the card spec's PageSize against fixed 512-byte data / 32-byte ECC buffers
- **Category:** 6 (bugs/security)
- **Locations:** `modules/mcemu/mcemu.c:791` (erase fill), `:806-818` (ECC loop), `:853-856` (read cap), `:900-903` (write copy); buffer sizes in `modules/mcemu/mcemu_var.c:61-62` (`mcdatabuf[0x200]`, `mceccbuf[0x20]`, shared by both emulated cards).
- **Problem:** Nothing in the file bounds `mcd->cspec.PageSize`, yet it sizes operations directly on the two fixed buffers: the erase `memset`, the ECC loop (which also walks off the 32-byte ECC buffer above ~0x440), and the read/write caps. The file itself proves non-512 values are contemplated — the BDM erase fast path is gated on `PageSize == 0x200` and everything else falls through to the unbounded path. `cspec` is copied from the VMC image's parameters (set up outside this file — that hop is INFERRED), so a malformed or mismatched VMC image overflows IOP static buffers shared by both cards.
- **Fix (words):** Reject or clamp any card whose PageSize exceeds the data-buffer size, once, at configuration/first use in this file — instead of checking at every memcpy.
- **Justification (fix adds a check):** One comparison closes four separate overflow sites at once.
- **Benefit:** A bad VMC spec degrades to "card not present" instead of IOP corruption.
- **Effort:** S · **Risk:** Low (genuine PS2 cards use 512-byte pages and pass unchanged) · **Confidence:** Medium — in-file mechanism READ and unconditional; trigger depends on the out-of-file spec path (INFERRED).

### RIPTOPL-070 — SMB share-picker trusts the IOP's returned share count against a 128-entry stack array
- **Category:** 6 (bugs/security)
- **Locations:** `src/sources/ethsupport.cpp:828-858` — `ShareEntry_t sharelist[128]` on the stack, `maxent = 128`, then the devctl-returned `count` sizes a `malloc` and indexes `sharelist[i]` with no clamp.
- **Problem:** `maxent` is a request to the driver; the EE side never verifies the reply honoured it. A driver (either SMB dialect — the path is shared) returning `count > 128` makes the loop read past a stack array and publish garbage as share names, and sizes the heap allocation from the same unchecked value. The codebase's own convention elsewhere is to clamp external counts at the EE boundary regardless of the producer's promise (favsupport caps stored counts at FAV_MAX_ITEMS with that exact comment; themes' listDir takes an explicit remaining-slots bound). This site is the exception. Distinct from RIPTOPL-016 (launch-leg credential copies; this is share enumeration).
- **Fix (words):** Clamp `count` to 128 immediately after the devctl returns, before the allocation and the loop.
- **Benefit:** A mis-reporting driver degrades to a truncated share list instead of a stack out-of-bounds read.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the missing clamp (READ); INFERRED that a resident driver can return >maxent — the finding is the absent EE-side bound, per the codebase's own stated convention.

### RIPTOPL-071 — Ethernet link-mode from the config file indexes a 4-entry table in ee_core with no range check
- **Category:** 6 (bugs/security)
- **Locations:** `ee_core/src/util.c:74-79` (`SmapLinkModeArgs[4]`), only guard at `:96` (`!= ETH_OP_MODE_AUTO`), indexing copy at `:99`. Producer chain, all READ: `src/app/settingsio.cpp:1924` (`configGetInt(..., &gETHOpMode)`, no clamp) → `src/platform/system.cpp:1561` (assigned into the `unsigned char` field, `ee_core/include/coreconfig.h:46`). Valid modes are 1–4 (`include/opl.h:109-113`; `ETH_OP_MODE_COUNT` = 5 is a count, not a mode).
- **Problem:** The link mode is an integer in the user's hand-editable network config. The GUI widget constrains interactive values, but nothing on the file-load path validates the range. A value of 5–255 survives into ee_core, passes the `!= AUTO` test, and indexes `SmapLinkModeArgs` out of bounds; `strncpy` then copies 6 bytes from a wild pointer into the SMAP ipconfig argument string, or faults — inside `set_ipconfig()`, which runs on every ee_core init for network modes. Same trust-boundary shape as RIPTOPL-016, one layer deeper, and cross-reference RIPTOPL-073 (the same file's unvalidated IP parsing).
- **Fix (words):** Clamp where consumed — in `set_ipconfig`, treat any mode outside 1–4 as AUTO; or clamp at the settingsio load site. The ee_core end cannot be bypassed by another producer.
- **Justification (fix adds a check):** A single range comparison on a cited out-of-bounds index driven by a user-editable file — the minimum that removes the defect.
- **Benefit:** A hand-edited or cross-version config value stops being an out-of-bounds read at every network game launch.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — full chain read end to end).

### RIPTOPL-072 — mcemu accepts any page number from the game packet; CardSize is never used as a bound
- **Category:** 6 (bugs/security)
- **Locations:** `modules/mcemu/mcemu.c:603,609,614` (entry points), `:758-800` (erase), `:822-829` (read start), `:871-878` (write start).
- **Problem:** The page number is a raw signed int assembled from the SIO packet and stored/used verbatim. `MceEraseBlock` writes `BlockSize` pages starting at `page + i`; reads/writes assign `rpage`/`wpage` unchecked. Nowhere is any page compared with `mcd->cspec.CardSize` — the field exists, is even reported back to the game (case 0x26), but is never used for validation. Negative and beyond-end pages flow to the device layer, whose offset is `page * PageSize` (device files, cross-referenced) — reads/writes outside the VMC image, wrapping the file offset for negative/huge pages.
- **Fix (words):** Validate the start page (and `page + BlockSize` for erase) against `cspec.CardSize` at the three entry points, failing the command via the existing error path (`result = 0` / `io_error`).
- **Justification (fix adds checks):** Three small range checks reusing an error path that already exists; no new machinery.
- **Benefit:** A buggy or hostile game/MCMAN sequence can no longer read or erase outside the virtual card image.
- **Effort:** S · **Risk:** Low (in-range traffic untouched) · **Confidence:** Medium-high — absence of the check is READ (whole file read; CardSize has no other use); exact downstream offset behaviour INFERRED from the device files.

### RIPTOPL-073 — Network IP strings are parsed with unchecked `sscanf` at six sites, unlike the validating RA parser in the same file
- **Category:** 6 (bugs/security)
- **Locations:** `src/app/settingsio.cpp:1931` (SMB IP), `:1936` (HTTP IP), `:1950` (PS2 IP), `:1952` (netmask), `:1954` (gateway), `:1956` (DNS); the correct pattern already in the same file at `:1534` (`configReadRA`).
- **Problem:** All six sites are bare `sscanf(temp, "%d.%d.%d.%d", ...)` with no return-value check and no 0–255 range check, writing into `int[4]` globals. A hand-edited value like `999.1.2.3`, `-5.0.0.1` or `1.2.3` is accepted as-is; partial parses silently mix file octets with previous/default octets. The values are later formatted into the `ip=%d.%d.%d.%d` module argument and SMB targets, so nonsense reaches the network stack instead of being rejected at load. The RA parser at `:1534` requires exactly 4 conversions, a trailing-character check, and per-octet range checks — the standard exists in-file and was not applied here. Sibling of RIPTOPL-071 (same load path, different mechanism — string parse vs integer index).
- **Fix (words):** Route all six sites through one small validated parse helper with the RA parser's contract (exactly 4 fields, no trailing junk, each octet 0–255; on failure keep defaults and log).
- **Justification (fix adds a helper):** It replaces six unvalidated copies with one validated one — net code shrinks.
- **Benefit:** A malformed network config can no longer produce a mixed/garbage IP configuration; one parser to audit instead of six.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-074 — smb_fio's shared path scratch buffers are filled before the I/O lock is taken
- **Category:** 6 (bugs/security — concurrency)
- **Locations:** `modules/network/smbman-ra/smb_fio.c:343-345` (open; pattern repeats at `:484-486, 511-513, 536-538, 637-639, 737-739, 769-772, 797-799`).
- **Problem:** Every path-taking operation builds its path in the file-scope globals `smb_curpath`/`smb_secpath` via `prepare_path` and only then takes `smb_io_lock()`. The lock protects the SMB transaction, not the string the transaction is about: two overlapping operations let the second `prepare_path` overwrite the buffer before the first operation consumes it, so the first acts on the second's file (wrong file opened/deleted/stat'ed). `smb_rename` is the sharp case — `oldpath` is a pointer into the shared buffer, not a private copy. (The `prepare_path` internals are RIPTOPL-040's sites and are not re-filed; this finding is the lock ordering around them.)
- **Fix (words):** Move each `prepare_path` call to after `smb_io_lock()` (rename needs the same move for both of its calls).
- **Justification:** No addition at all — a pure reordering of existing lines.
- **Benefit:** The existing semaphore actually covers the data it was meant to cover; removes a wrong-file class of bug under multithreaded EE load.
- **Effort:** S (eight call sites, mechanical) · **Risk:** Low (lock hold grows by one bounded string copy) · **Confidence:** Medium — ordering is READ; whether two iomanX calls can overlap on this IOP build is INFERRED (the driver itself assumes concurrency elsewhere: it owns an I/O semaphore and a keepalive thread).

### RIPTOPL-075 — smb_fio dereferences the file handle before (or without) any NULL check in five entry points
- **Category:** 6 (bugs/security)
- **Locations:** `modules/network/smbman-ra/smb_fio.c:375-383` (close), `:424-427` (read), `:448-451` (write), `:684-696` (dread — no check at all), `:832-837` (lseek64 — no check at all).
- **Problem:** `smb_close` reads `fh->smb_fid` in its guard at `:378` before the `if (fh)` test at `:383` — the intended check sits where it can never protect anything. Read/write repeat the shape; dread and lseek64 use handle fields with no validation whatsoever. A call on a file whose open failed (privdata NULL) faults the IOP instead of returning `-EBADF`.
- **Fix (words):** Test `fh == NULL` first in the close/read/write guards, and add the same guard (plus fid check) to dread and lseek64 before any field is read.
- **Justification (fix adds checks):** A few comparisons; no new code paths or allocations. The NULL test already exists in `smb_close`, just in a position where it can never fire.
- **Benefit:** Malformed/failed-open call sequences return an error instead of crashing the SMB filesystem driver.
- **Effort:** S · **Risk:** Low (only changes behaviour on paths that currently fault) · **Confidence:** High on the mechanism (READ); Medium on real-world reachability (depends on iomanX calling after a failed open — INFERRED).

### RIPTOPL-076 — apa `apaGetFormat` dereferences an unchecked cache allocation
- **Category:** 6 (bugs/security)
- **Locations:** `modules/hdd/apa/src/apa.c:461-467` (`clink = apaCacheAlloc();` followed immediately by `apaReadHeader(device, clink->header, 0)`); contrast the same file's checked call at `:58`.
- **Problem:** `apaCacheAlloc` returns NULL when the cache pool is exhausted — its contract is demonstrated by the checked call site at `:58` (`if (!(clink = apaCacheAlloc())) return -ENOMEM;`) and by `cache.c`. `apaGetFormat` passes `clink->header` to `apaReadHeader`, which DMA-reads into it, with no NULL test: cache exhaustion turns a format probe into a NULL-pointer DMA write and an IOP crash. The function is live — called from `hdd.c:331` and `hdd_blkio.c:126,263` during device probing (READ).
- **Fix (words):** Copy the guard the same file already uses at `:58`: on allocation failure, return `-ENOMEM` before touching `clink` (`*format` is already zeroed before the call).
- **Benefit:** A probed-under-pressure driver fails the probe instead of crashing the IOP.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-077 — `usb_mass_connect` re-reads descriptors with none of `usb_mass_probe`'s NULL checks
- **Category:** 6 (bugs/security)
- **Locations:** `modules/usb/usbmass_bd/src/usb_mass.c:664-682`; contrast probe's checks at `:597-620`.
- **Problem:** `usb_mass_probe` checks every descriptor it touches; `usb_mass_connect`, run later for the same device, checks none: it dereferences the config descriptor (possibly NULL), advances by the device-supplied `config->bLength`, reads `interface->bNumEndpoints`, and walks endpoints by each descriptor's device-supplied `bLength`, passing possibly-NULL endpoints to `usb_bulk_probeEndpoint`. A device removed between probe and connect, a scan failure, or a zero/oversized `bLength` becomes a NULL dereference or a walk through arbitrary USBD static-descriptor memory interpreted as endpoints (whose "addresses" are then opened as pipes).
- **Fix (words):** Apply the same NULL checks probe already has before each dereference, and stop the endpoint walk when the pointer is NULL or `bLength` is smaller than an endpoint descriptor.
- **Justification (fix adds checks):** It reuses checks that already exist twenty lines earlier in the same file — two comparisons are LESS than any new validation helper.
- **Benefit:** A flaky or malicious USB device can no longer crash the BDM USB driver at attach time.
- **Effort:** S · **Risk:** Low (devices that pass probe pass these identically) · **Confidence:** High on the missing checks (READ, side-by-side with probe); the exploitable-descriptor scenario is INFERRED (descriptor bytes originate from the device).

### RIPTOPL-078 — All dialog text rows share 32-byte storage, silently truncating the Custom Settings Path (64) and BGM Path (128) on any Settings round-trip
- **Category:** 6 (bugs/security — silent data loss)
- **Locations:** `src/ui/dialogs.cpp:24` (Custom Settings Path row), `:35` (BGM Path row); storage type `include/dia.h:56` (`UI_STRING_SIZE` 32); truncation at `src/ui/diaitems.cpp:130-144`; round-trips at `src/ui/guisettings.cpp:2375/2398` (settings path, destination `gCustomSettingsPath[64]`) and `:1979/1954` (BGM path, destination `gDefaultBGMPath[128]`).
- **Problem:** Every `UI_STRING` row stores text in two `char[32]` arrays; `diaSetString` truncates into that storage and the on-screen keyboard edits a `char tmp[32]`. The longer path rows that work (ExitTo / POPSTARTER / Neutrino) work only because guisettings gives them full-size private edit buffers plus handlers. These two rows never got that treatment: a custom settings path or BGM path longer than 31 characters — valid in the config file, and for the settings path explicitly supported to 63 chars by all of settingsio's machinery — is silently cut to 31 chars the moment the user opens the containing Settings page and confirms; the next save persists the truncated path (and, for the settings path, writes a `config.path` redirect pointing at the wrong folder — cf. RIPTOPL-120).
- **Fix (words):** Give these two rows the same treatment as the other path rows (full-size edit buffer + the existing handler pattern in guisettings), or widen only the storage for path-class rows. Do **not** raise `UI_STRING_SIZE` globally — that grows every row's storage for two rows' benefit.
- **Justification (fix adds storage):** Tied to this cited silent-truncation bug; the alternative (capping the config keys at 31) would delete a documented settingsio feature.
- **Benefit:** Two settings stop being silently corrupted by merely opening their page.
- **Effort:** M · **Risk:** Medium (touch only the two rows' set/get call sites) · **Confidence:** High (READ — full chain read: storage → size → truncation → round-trip → destination sizes).

### RIPTOPL-079 — config.path MC mirror creates the OPL folder on the wrong card
- **Category:** 6 (bugs/security)
- **Locations:** `src/app/settingsio.cpp:361-367` (mirror leg of `writeConfigPathRedirect`); contrast `:242-262` (`preferredMcSlot` / `mcConfigPathRedirect`).
- **Problem:** The mirror target is deliberately built from `preferredMcSlot()` — the concrete boot slot, so an mc1 boot mirrors to mc1 even with both cards inserted. But when the first open fails because `mcN:/OPL` doesn't exist yet, the folder-creation fallback ignores that slot: `snprintf(mcDir, ..., "mc%d:/OPL", sysCheckMC())` at `:364`, and `sysCheckMC()` returns the *first present* card. On an mc1 boot with both cards inserted it creates `mc0:/OPL` (a folder on a card this boot does not use — the exact failure mode the rest of the file guards against at length), the retry still opens the mc1 path whose folder was never created, and because a deferred (network) boot returns `mirrorOk`, the whole redirect save reports failure. If `sysCheckMC()` returns −1 the mkdir target is literally `mc-1:/OPL`.
- **Fix (words):** Build `mcDir` from the same slot that produced `mcPath` (return the slot from `mcConfigPathRedirect` or recompute with `preferredMcSlot()`); delete the `sysCheckMC()` call from this leg.
- **Benefit:** Network-boot settings redirect actually persists on dual-card mc1 setups; no stray folder stamped on the other card.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the mechanism (READ); the on-hardware symptom is INFERRED.

### RIPTOPL-080 — ps2ips `do_accept` / `do_getsockname` / `do_getpeername` pass an uninitialised `addrlen` and copy the address struct even on failure
- **Category:** 6 (bugs/security)
- **Locations:** `modules/network/ps2ips/ps2ips.c` — `do_accept` (`addrlen` declared `:92`), `do_getsockname` (`:531`), `do_getpeername` (`:547`); contrast `do_recvfrom`, where the identical parameter **was** fixed (`int fromlen = sizeof(struct sockaddr);` at `:289`, with an "RA fix" comment).
- **Problem:** (a) lwIP treats `addrlen` as value-result (per the file's own recvfrom comment, it copies `min(*fromlen, 16)` bytes): an uninitialised stack value below 16 truncates the copied address, and 0 copies none — the EE receives a zeroed or partial sockaddr while the return value reports success. This is the same defect class the file already fixed in `do_recvfrom`; the three sibling handlers were left unfixed. (b) All three copy the local `sockaddr` into the reply packet regardless of the call's return value; on failure the struct is untouched stack memory, returned to the EE as if it were an address. Also already recorded as seen-but-unfixed in `docs/PS2SDK-NOTES.md` §ps2ips; no prior finding ID.
- **Fix (words):** Initialise each `addrlen` to `sizeof(struct sockaddr)` before the call (the `do_recvfrom` line, copied), and copy `addr` into the reply only on success. Three small edits reusing the file's own fixed pattern.
- **Benefit:** Accepted/queried peer addresses stop being intermittently zeroed; a failed call stops leaking stack contents into an EE-visible struct.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-081 — udpfs_ioman open handles keep their pre-disconnect server handle numbers across a watchdog reconnect
- **Category:** 6 (bugs/security)
- **Locations:** `modules/network/udpfs_ioman/src/udpfs_ioman.c` — handle table `:41-47` (`g_fds[8]`), validation `:66-80` (`_validate_fd`), watchdog `:98-107`.
- **Problem:** The watchdog heals a dropped session by calling `udpfs_core_init()` whenever the connection check goes false. Nothing in that transition touches `g_fds`: slots keep the `server_handle` integers issued by the **old** server session. `_validate_fd` checks only that the session is currently connected and the local slot is occupied — there is no session generation or epoch anywhere in the file (grep, READ). After a heal, the next read/write/lseek on a still-open file sends the stale integer to the new session. What the server does with it is server-side (INFERRED): at best an error; at worst, if the new session has already issued the same small integer for a different file, the operation lands on the wrong file. The window is realistic — the watchdog exists precisely because servers are hand-started, restarted, and time out mid-use.
- **Fix (words):** Invalidate the table at the moment the watchdog observes the disconnected state (mark every slot free before re-initialising the core), so post-heal operations fail with `-EBADF`/`-EIO` and the EE re-opens, instead of silently reusing numbers from a dead session. The invalidation loop already exists at init (`:128-130`).
- **Benefit:** A server restart can no longer alias an open OPL file handle onto an unrelated server-side file.
- **Effort:** S · **Risk:** Low (files open across a disconnect are unusable in practice today; this makes it explicit) · **Confidence:** High for the missing invalidation in this file (READ); INFERRED for the wrong-file outcome (depends on the server's per-session handle allocation, audited under PS2-Servers, not here).

### RIPTOPL-082 — mcemu Sio2McEmu writes fixed/derived response lengths never checked against the packet's own transfer length
- **Category:** 6 (bugs/security)
- **Locations:** `modules/mcemu/mcemu.c:591` (decoded length), `:618-627` (case 0x26), `:636-640` (case 0x28), `:651-663` (case 0x43).
- **Problem:** The handler decodes a per-command length from the control word and the `SioResponse` helper honours it — but three cases do not: 0x26 writes 13 bytes regardless of length; 0x28 writes a 5th byte after a 4-byte response; 0x43 discards the control-word length, substitutes `wdma[2]`, and writes up to `rdma[260]`. Nothing verifies the caller's DMA segment for that command holds 13, 5, or `length + 6` bytes respectively. A packet whose control-word length disagrees with its command byte (malformed or mismatched MCMAN/game build) writes past its segment into the next command's segment or past the buffer.
- **Fix (words):** Gate each case on the control-word length (`>= 13`, `>= 5`, and for 0x43 require `wdma[2]` to fit the segment the control word describes), failing the command via the existing `result = 0` path.
- **Justification (fix adds checks):** Three comparisons reusing the existing error path; closes the only response paths that ignore the length the handler already decoded.
- **Benefit:** Malformed SIO packets corrupt nothing outside their own response slot.
- **Effort:** S · **Risk:** Low (well-formed MCMAN packets already satisfy all three bounds) · **Confidence:** Medium — the unconditional writes are READ; that a real caller's segment can be smaller is INFERRED (segments are sized by the SIO2 packet builder outside this file).

### RIPTOPL-083 — Pademu USB: `ds34usb_init` never initialises `outEndp` to −1, so the first connection never opens the output endpoint
- **Category:** 6 (bugs/security)
- **Locations:** `modules/pademu/ds34usb.c:571-616` (init loop; the omission), dependent tests at `:164,175,212-217,437`; standalone counterpart `modules/ds34usb/iop/ds34usb.c:958`.
- **Problem:** The pademu init loop initialises every sibling field to −1 — semaphores, `controlEndp`, `interruptEndp` — but never `outEndp`. The pad array is file-scope, so `outEndp` starts at 0. Consequences on the first connection after module load: `usb_connect` opens the OUT endpoint only `if (outEndp < 0)` — false, so it is never opened; the acceptance check passes for the wrong reason, so the pad runs with no output path; DS4 `LEDRumble` issues its transfer against endpoint id 0 (whatever that denotes in USBD — INFERRED, outside these files); and `usb_release` closes endpoint 0 as if real, only then setting −1 — so the defect self-heals after the first disconnect/replug cycle, exactly the shape that hides it in testing.
- **Fix (words):** Initialise `outEndp = -1` in the `ds34usb_init` loop alongside `controlEndp`/`interruptEndp`, matching the standalone `_start`.
- **Benefit:** First-plug DS3/DS4 LED and rumble work in pademu instead of only after a replug; removes a spurious `UsbCloseEndpoint(0)`.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — omission, the −1 convention, and all dependent tests are quoted; C static zero-initialisation is a language guarantee).

### RIPTOPL-084 — Theme catalog entry allocations are used without a NULL check
- **Category:** 6 (bugs/security)
- **Locations:** `src/gfx/themes.cpp:1045` (`name = malloc(...)` then `memcpy` on the next line), `:1050-1051` (`filePath = malloc(...)` then `sprintf`) — both in `ThemeCatalog::readEntry`.
- **Problem:** `readEntry` runs for every `thm_`-prefixed entry on every device scan. Both allocations are dereferenced on the next line with no NULL check. On an EE already carrying a built theme, textures and game lists, a failed small malloc during a scan (a second device coming up is exactly when memory is tightest) is a write through NULL, crashing the loader during enumeration — not a graceful "theme skipped". Every neighbouring allocation site checks (`favStrdup`, `favBuildImage`, `thmLoad`'s own `newT` malloc, `cloneImageCache`'s caller); this pair does not. Distinct from the round-2 CLEARED themes item (that was the `sprintf` destination sizing; this is the missing allocation check).
- **Fix (words):** Check both allocations; on failure, free the first if it succeeded, leave the slot unused and return the index unchanged (the theme is simply not listed, as a non-directory candidate is not listed).
- **Benefit:** A theme scan under memory pressure degrades to a missing theme instead of a crash.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-085 — `smb_lseek64`: SEEK_END ignores its offset, and no seek is floored at zero
- **Category:** 6 (bugs/security)
- **Locations:** `modules/network/smbman-ra/smb_fio.c:832-868`; SEEK_END at `:854-856`.
- **Problem:** (1) `SEEK_END` sets `r = fh->filesize` and discards `pos` entirely — `lseek(fd, -N, SEEK_END)`, the standard "read the trailer" pattern, lands on EOF and the following read clamps to zero bytes. (2) SEEK_SET and SEEK_CUR check only the upper bound: a negative SET, or a CUR seek past the start of the file, is accepted and stored as a negative `fh->position`, which subsequent reads/writes split into 32-bit offset halves and send to the server as a near-2^64 offset.
- **Fix (words):** Make SEEK_END compute `filesize + pos`, and reject any computed result below 0 in all three modes before assigning `fh->position`.
- **Justification:** Two comparisons and one addition on an existing value; no new state.
- **Benefit:** Correct trailer/relative-from-end seeks; a bad seek can no longer poison the handle's position for every later I/O.
- **Effort:** S · **Risk:** Low (SEEK_END with `pos == 0`, the common pattern, is unchanged) · **Confidence:** High (READ, pure in-file arithmetic).

### RIPTOPL-086 — HDPro ATAD: transfer retries a failing chunk forever, with no delay, while holding the I/O semaphore
- **Category:** 6 (bugs/security) / 5 (performance)
- **Locations:** `modules/iopcore/cdvdman/hdpro_atad.c` — transfer loop `:692-720`, explicit retry at `:714` (`//Unlike ATAD, retry indefinitely…`), bare `continue` at `:717,719`; semaphore taken at `:688`, released only after the loop.
- **Problem:** On **any** error from `sceAtaExecCmd`/`sceAtaWaitResult` — not only transient CRC errors — the same chunk is re-issued immediately, forever: no retry cap, no back-off (`DelayThread` never appears in the loop), no error-class filter. A permanent failure (failing/removed disk, wedged HDPro bridge, a persistently rejected command) spins the calling IOP thread at full speed re-programming the bridge registers, and because `ata_io_sema` is held for the whole transfer, every other ATA user — including the shutdown flush path — blocks behind it permanently. The game never gets an error it could react to; the console wedges. The sibling driver shows the intended shape: `atad.c:700` retries at most 3 times and only on `ATA_RES_ERR_ICRC`, then returns the error with the semaphore released.
- **Fix (words):** Bound the retry like the sibling: retry only on the transient error class, at most a small fixed number of times, with a short delay between attempts; return the final error when the budget is spent. A change to existing lines, nothing added.
- **Benefit:** A dead disk on HDPro hardware becomes a returned error instead of a permanent, semaphore-holding console wedge.
- **Effort:** S · **Risk:** Low–Medium (behaviour changes from "hang" to "fail"; any caller secretly depending on infinite retry was depending on a hang) · **Confidence:** High (READ).

### RIPTOPL-087 — `apaFindPartition` converts a chain-walk I/O error into a clean "not found" — and one live caller then returns success with an unfilled stat
- **Category:** 6 (bugs/security)
- **Locations:** `modules/hdd/apa/src/apa.c:170-193`; live caller `modules/hdd/apa/src/hdd_fio.c` `hddGetStat` at `:782-789` (READ).
- **Problem:** The walk distinguishes two exits internally — chain ended cleanly (`*err = -ENOENT`) versus a header read failed mid-chain (`*err`, e.g. `-EIO`) — and then deliberately erases the second: on walk error it executes `*err = 0; return NULL;`. Callers cannot distinguish "partition does not exist" from "the disk stopped answering". In `hddGetStat` the consequence is concrete: it tests only the returned pointer, skips the stat fill, and returns `rv == 0` — getstat reports success and leaves the caller's stat struct unfilled. Other callers inherit the same confusion at lower stakes.
- **Fix (words):** Delete the error-erasing tail: when the walk ends with `*err != 0`, return NULL with `*err` intact; keep the `-ENOENT` assignment only for the clean-end case. No caller needs a new branch — `hddGetStat` already propagates `rv`.
- **Benefit:** Disk failures surface as errors instead of phantom success/ENOENT; one branch removed, none added.
- **Effort:** S · **Risk:** Low (callers that ignored `rv` on NULL still get NULL) · **Confidence:** High (READ).

### RIPTOPL-088 — `smb_devctl` validates neither buffer length; Echo/GetShareList trust caller length/address fields
- **Category:** 6 (bugs/security)
- **Locations:** `modules/network/smbman-ra/smb_fio.c:1077-1084` (`arglen`/`buflen` accepted then discarded), `:1061-1065` (Echo), `:996-1002` (GetShareList).
- **Problem:** Every devctl handler casts `arg`/`bufp` to a full struct without checking the supplied lengths. Two concrete consumers: `smb_EchoServer` passes the caller-supplied `len` — for a struct whose echo field is 256 bytes — unchecked into `smb_Echo`; `smb_GetShareList` computes DMA destinations as `EE_addr + shareindex * sizeof(ShareEntry_t)` for up to a caller-supplied `maxent`, with neither the count nor the address arithmetic sanity-checked in this file.
- **Fix (words):** Stop discarding `arglen`/`buflen`: require each command's minimum struct size before casting, and clamp the echo length to the echo field and `maxent` to a sane bound at this entry point.
- **Justification (fix adds validation):** A small validation block at one choke point protects eight handlers at once; the alternative — per-handler checks scattered over the file — is more, not less.
- **Benefit:** A malformed devctl from any EE-side caller can no longer drive out-of-bounds reads/DMA through the logon/echo/share paths.
- **Effort:** S · **Risk:** Low (minimum sizes taken from the existing struct definitions) · **Confidence:** Medium-high — the missing validation is READ; the size of the downstream overrun depends on `smb.c`'s packet buffer (INFERRED, cross-referenced only).

### RIPTOPL-089 — The last two unbounded `sprintf` calls in the EE sources build the games.bin path into a 256-byte stack buffer
- **Category:** 6 (bugs/security)
- **Locations:** `src/sources/supportbase.cpp:170` (`char filename[256]`), `:177` and `:269` (`sprintf(filename, "%s/games.bin", path)`).
- **Problem:** A repo-wide grep confirms these are the only unbounded `sprintf` calls left in either file audited in section 1a — everything else uses `snprintf`. `path` arrives composed into its own `char[256]` with `snprintf`, so it can legitimately reach 255 chars; appending `/games.bin` then writes up to ~10 bytes past `filename` on the stack. Today's device prefixes are short, so this is latent — but it is exactly the class the rest of the file has been systematically cleaned of, and a longer prefix/subpath combination is the only trigger needed. (The cache-load logic itself was revalidated clean this round — see Revalidations; this finding is only the path composition.)
- **Fix (words):** Change both `sprintf` calls to `snprintf(filename, sizeof(filename), …)` and treat truncation the way neighbouring code treats other truncated paths. Two-word change per site, no new code.
- **Benefit:** Removes the last unbounded string copy in 6,440 audited lines; the cache path fails closed instead of corrupting the stack.
- **Effort:** S · **Risk:** Low (a truncated path simply misses the cache, the already-handled case) · **Confidence:** High on the mechanism and buffer arithmetic (READ); INFERRED (low impact today) for reachability, because current prefixes are short.

### RIPTOPL-090 — `vcdIsHiddenDisc` accumulates an unbounded disc number into an `int` — signed overflow from a crafted filename
- **Category:** 6 (bugs/security)
- **Locations:** `src/sources/vcdsupport.cpp:990-1011`, accumulation at `:1007`.
- **Problem:** For each "(disc"/"[cd"/"(disk" token, the code runs `num = num * 10 + digit` over the whole digit run, checking `num >= 2` only afterwards. The digit run is bounded only by the filename (basename cap 160 chars) — a file named `Game (Disc 99999999999999999999).VCD` overflows a signed 32-bit `int` mid-loop: undefined behaviour, and the wrapped value can land below 2, misclassifying a disc-2+ file as disc 1. The input is filesystem-supplied — precisely the untrusted side of this parser.
- **Fix (words):** Return 1 the moment `num` first reaches ≥ 2 inside the digit loop. That is exact, not approximate: appending decimal digits to a value already ≥ 2 can only increase it, so no later digit can change the answer — and `num` then never exceeds 29.
- **Benefit:** Removes a signed-overflow UB on untrusted filenames; multi-disc detection gets marginally faster.
- **Effort:** S · **Risk:** None (monotonicity argument above) · **Confidence:** High (READ).

### RIPTOPL-091 — `bdmIsReadOnlyPath` parses the mass unit with the unbounded accumulate that `bdmParseBootStem` — in the same file — explicitly fixed
- **Category:** 6 (bugs/security)
- **Locations:** `src/sources/bdmsupport.cpp:887-897` (accumulate at `:893`); contrast `:179-215` (`bdmParseBootStem`).
- **Problem:** `while (*p >= '0' && *p <= '9') unit = unit * 10 + (*p++ - '0');` has no digit-count bound, so a path like `mass99999999999999999999:/…` overflows signed `int` (UB) during the accumulate. `bdmParseBootStem`, 700 lines earlier in the same file, documents exactly this failure ("an unbounded accumulate … would run signed arithmetic into UB on a mangled path") and caps the parse at 2 digits. The hardening was applied to the boot-path parser and missed this one. Probably benign today — `bdmModeIsReadOnly` range-checks its argument, so most wrapped values return 0 — but a wrapped value landing inside the mode range would report an unrelated device's read-only state, and the sole caller (`BdmSource::config`) uses that answer to decide where per-game settings for a read-only device are stored. The input is a config home path, not raw network data — robustness, not a claimed exploit.
- **Fix (words):** Reuse the same bound as `bdmParseBootStem` (reject >2 digits, or call that parser). Deletion-shaped: replace the hand accumulate with the existing bounded helper.
- **Benefit:** Removes the file's last unbounded unit parse; the two parsers can no longer disagree about what a mass path is.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-092 — Standalone ds34 copies index `power_level[]` with the unguarded device-supplied battery byte; pademu copies guard it
- **Category:** 6 (bugs/security)
- **Locations:** `modules/ds34bt/iop/ds34bt.c:261-263` (table), `:1332-1333` (use); `modules/ds34usb/iop/ds34usb.c:45-47` (table), `:337-338` (use). Guarded counterparts: `modules/pademu/ds34bt.c:1251-1252`, `modules/pademu/ds34usb.c:332-333`.
- **Problem:** `power_level[]` has exactly 6 entries in all four files. Both standalone copies index it with `report->Power` — a full device-supplied byte — excluding only 0xEE (charging). Any other out-of-range value a real, clone, or malfunctioning pad reports while PS is held (0x06–0xED, 0xEF–0xFF) reads past the 6-byte table into adjacent statics and uses the byte as an LED pattern. (One mitigating quirk, standalone BT only: when its sticky pressure-emulation flag is on — RIPTOPL-105 — it first overwrites `report->Power = 0x05`, accidentally bounding the index on that path.) Both pademu copies already carry the fix: `if (report->Power <= 0x05)`.
- **Fix (words):** Port the pademu `<= 0x05` guard to both standalone copies; keep the existing 0xEE charging handling, which lives outside this expression.
- **Benefit:** Removes an out-of-bounds static read driven by an untrusted peripheral byte, for the cost of the guard the sibling copies already prove acceptable.
- **Effort:** S · **Risk:** Low (a pad reporting a nonsense level simply keeps its previous LED pattern — the pademu behaviour) · **Confidence:** High (READ).

### RIPTOPL-093 — SMAP: the DEV9 capability check is a no-op in both neutrino copies (operator precedence); the ingame copy has the correct form
- **Category:** 6 (bugs/security) / 2 (duplication — drift between copies)
- **Locations:** `modules/network/udpfs_smap/src/smap.c:516` and `modules/network/smap_udpbd/src/smap.c:522` — both read `if (!SPD_REG16(SPD_R_REV_3) & SPD_CAPS_SMAP) return -1;`. Correct form in-tree: `modules/network/smap-ingame/smap.c:541`, and the same idiom correctly parenthesised at `modules/hdd/atad/src/ps2atad.c:282` and `modules/iopcore/cdvdman/atad.c:197`.
- **Problem:** In C, `!` binds tighter than `&`, so the expression parses as `(!register) & SPD_CAPS_SMAP`. `!x` is only ever 0 or 1, so the condition can be nonzero only when the register is exactly 0 **and** the mask has bit 0 set. For any normal nonzero revision register the check evaluates to 0 and never fires: both drivers proceed to reset FIFOs, program the EMAC and register interrupt handlers on hardware where the SMAP capability may be absent, instead of failing init with −1 as intended. (The exact value of `SPD_CAPS_SMAP` lives in the PS2SDK headers, not this repo: the "only bit 0 could ever fire it" statement is READ from C precedence; whether it can fire at all on real register values is INFERRED.)
- **Fix (words):** Parenthesise both copies to the ingame form — test `(register & SPD_CAPS_SMAP) == 0`. Two one-line edits; one if RIPTOPL-127's merge happens first.
- **Benefit:** Init fails cleanly on SMAP-less DEV9 hardware instead of programming a non-existent EMAC.
- **Effort:** S · **Risk:** Low (on hardware where SMAP works today the bit is set and behaviour is unchanged) · **Confidence:** High (READ — precedence and the in-tree correct forms).

### RIPTOPL-094 — `smb_dread` can hand the EE an unterminated directory name
- **Category:** 6 (bugs/security)
- **Locations:** `modules/network/smbman-ra/smb_fio.c:714` (`strncpy(dirent->name, info->FileName, 256)`; destination layout `char name[256]` verified in `tests/smb2-live/inc-browse/iomanX.h:24-28`).
- **Problem:** `info->FileName` is the server-supplied name. A name of 256+ bytes fills `dirent->name` completely; `strncpy` writes no terminator and nothing afterwards forces `name[255] = '\0'`. The EE-side consumer reads a string that runs past the field. Distinct from RIPTOPL-040's three sites — different buffer, different copy, different consumer — and therefore filed separately, cross-referenced here so the two are not conflated.
- **Fix (words):** Copy at most 255 bytes and explicitly terminate the last byte of `dirent->name`.
- **Benefit:** Long-named directory entries truncate cleanly instead of leaking adjacent dirent memory into the EE's string handling.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the missing terminator (READ); the over-long name itself is server-controlled.

### RIPTOPL-095 — A libconfig group header whose name is too long leaves the *previous* group in force, mis-filing following scalars
- **Category:** 6 (bugs/security) — inside the RIPTOPL-020 compat layer; a new defect within it, the layer's existence is not re-filed
- **Locations:** `src/core/config.cpp:825-840` — group-header branch of `cfgReadLibconfigLine`.
- **Problem:** When a group name meets or exceeds `groupSize` (48), the branch silently does nothing and returns — crucially, it does **not** clear `group`. Every following scalar is then composed as `<previous-group>_<key>` and stored under the wrong composed key. Compare the scalar branch of the same function, which refuses an over-long key outright ("never truncate into a hit"): the group branch's failure mode is worse than dropping the group — it actively mis-files its contents under whichever group came before (benign only at file start, when `group` is empty). A hand-edited or third-party libconfig file with a long group name corrupts silently; no log line is emitted.
- **Fix (words):** On an unusable group header, clear `group` (and log the malformed line, as the legacy reader does) so following scalars land at root rather than under a stale prefix.
- **Benefit:** A malformed foreign file degrades to root-level keys instead of silently re-homing a whole group's settings.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-096 — UDPFS theme discovery latches on a possibly partial root listing; its two sibling discovery functions refuse to
- **Category:** 6 (bugs/security)
- **Locations:** `src/sources/udpfssupport.cpp:386-411` (`udpfsDiscoverThemes`); contrast `:424-455` (`udpfsDiscoverLanguages`) and `:505-540` (`udpfsDiscoverArtFolder`) in the same file.
- **Problem:** The function's own header comment explains it scans for a THM folder **in any case** because an empty auto-created `THM` must not hide a populated `thm` beside it. But its `readdir` loop captures no `errno`, so a listing that ends on a transient read error (UDPFS is a network filesystem; the server or link can drop mid-listing) is indistinguishable from a clean end-of-listing. If the first case-variant was already seen and a second, populated variant sat later in the listing, the function latches `udpfsThemesScanned = 1` for the whole session with only the empty variant registered — permanently reproducing, via a network hiccup, the exact "themes disappear for the session" failure the any-case scan was written to prevent. The languages and ART discovery functions in the same file both treat a read error as "retry later, never latch" (ART's comment: "an incomplete root listing must not become a permanent answer"); themes is the one that was not.
- **Fix (words):** Capture `errno` around the `readdir` loop exactly as `udpfsDiscoverLanguages` does, and do not set `udpfsThemesScanned` when the listing ended in error.
- **Benefit:** A mid-listing network error costs one retry on the next poll instead of a session-long loss of a case-variant theme folder.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-097 — One-shot probe latches inside bdmsupport survive an IOP reset that `bdmIopWasReset` cannot reach
- **Category:** 6 (bugs/security) / 8 (half-finished reset handling)
- **Locations:** `src/sources/bdmsupport.cpp:62-70` (`bdmIopWasReset`) vs function-local statics at `:1384`, `:1415`, `:1462`, `:1472`, and `:1806` (`lastModuleLoadGen` in `needsUpdate`).
- **Problem:** `bdmIopWasReset` exists precisely because "an in-menu IOP reset unloads every module, but these latches kept reading 'loaded'" — and it clears every *global* latch. The one-shot *function-local* statics cannot be cleared from there: after a reset, the transports reload (globals cleared) but the USB/MX4SIO settle-and-second-probe legs and the iLink recheck never run again this session, because their `*SecondProbeDone`/`ilinkProbeDone` flags are still 1 — the fast-stick/slow-card cases those probes were added for silently lose their only retry until reboot. `lastModuleLoadGen` also survives: if no generation bump follows the reset, `needsUpdate`'s module-queue gate can suppress re-queueing the block-module load for the current generation.
- **Fix (words):** Move the four probe flags (and `lastModuleLoadGen`) to file-scope statics and clear them in `bdmIopWasReset`, next to the latches it already clears — completing the reset function's stated contract rather than adding a new mechanism.
- **Benefit:** Post-reset device discovery behaves like first-boot discovery again.
- **Effort:** S · **Risk:** Low–Medium (re-running a bounded 600 ms settle probe after a reset is the pre-existing first-boot cost) · **Confidence:** High on the latch inventory and reset contract (READ); the post-reset on-hardware symptom is INFERRED.

### RIPTOPL-098 — mcemu `hookDmac_request` dereferences `_SysAlloc`'s result unchecked, and a repeated sio2in request leaks the packet
- **Category:** 6 (bugs/security)
- **Locations:** `modules/mcemu/mcemu.c:489-505` (free at `:511`).
- **Problem:** The channel-11 (sio2in) branch stores `_SysAlloc(sizeof(Sio2Packet))` into `temp_packet` and dereferences it on the next line with no NULL test — while the channel-12 branch *does* test `temp_packet == NULL`, making the omission conspicuous. Two further defects on the same lines: a second channel-11 request before channel 12 overwrites the global pointer and leaks the first allocation permanently; and the debug print reads `((u8 *)temp_packet->wrmaddr)[1]` without any check that the caller's buffer holds 2 bytes. The file's own comments note games that are "tight on IOP memory with mcemu resident" — allocation failure here is not theoretical.
- **Fix (words):** Return failure from the channel-11 case if the allocation returns NULL, and free/ignore an already-pending packet before replacing it.
- **Benefit:** Memory pressure during a patched mc2_s1 transfer returns an error instead of crashing the IOP; repeated requests stop leaking.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the unchecked dereference (READ); Medium on the leak's frequency (depends on mc2_s1's request pattern, INFERRED).

### RIPTOPL-099 — usb_mass transfer callback trusts the reported byte count; `remaining` can underflow into an unbounded transfer
- **Category:** 6 (bugs/security)
- **Locations:** `modules/usb/usbmass_bd/src/usb_mass.c:133-149`; chunk size chosen at `:121-131`.
- **Problem:** Each issued chunk is at most 4096 bytes and at most `remaining`, but the completion callback never verifies `bytes <=` the chunk just issued before doing `remaining -= bytes; buffer += bytes;` (unsigned arithmetic). A USBD/device error path that reports more bytes than requested wraps `remaining` to ~4 billion, after which the loop keeps issuing 4096-byte transfers marching through IOP memory past the caller's buffer — for a read, overwriting whatever follows it.
- **Fix (words):** Clamp/reject in the callback: if `bytes` exceeds the outstanding chunk length, treat the transfer as failed (signal the semaphore with an error) instead of advancing.
- **Justification (fix adds a check):** One comparison at the single place the count is consumed; no other site can fix it.
- **Benefit:** A misreported completion fails one SCSI command instead of streaming device data over IOP memory.
- **Effort:** S · **Risk:** Low (conforming completions never trip it) · **Confidence:** Medium — the unchecked arithmetic is READ; whether USBD can actually over-report is INFERRED (a device/USBD contract, not verifiable from this file).

### RIPTOPL-100 — ps2ips `do_getsockopt` always copies 128 bytes into the reply, whatever the call actually returned
- **Category:** 6 (bugs/security, minor)
- **Locations:** `modules/network/ps2ips/ps2ips.c:560-575`; copy at `:574`.
- **Problem:** The local `optval[128]` is filled by `getsockopt` with `optlen` initialised to 128, but the reply copy ignores both the return value and the resulting `optlen`: on failure, or for any option shorter than 128 bytes (in practice all of them), the tail of the EE-visible reply buffer is whatever was on the RPC thread's stack. The reply's own `optlen` field is set correctly, so a correct EE caller reads only `optlen` bytes — the impact is stack-byte disclosure into a buffer the EE owns, plus a trap for any caller that trusts the full 128. Filed as the low-grade mirror of RIPTOPL-067, in the same function pair.
- **Fix (words):** Copy only the returned `optlen` bytes on success (and zero or omit the buffer on failure). One line changed.
- **Benefit:** Reply contents become exactly what the syscall produced.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-101 — The config save's restore path writes the snapshot back with a single unchecked `write()` and logs success unconditionally
- **Category:** 6 (bugs/security)
- **Locations:** `src/core/config.cpp:1677-1685` (failure path of `configWrite`).
- **Problem:** This snapshot/restore machinery exists specifically for flaky devices — the surrounding comments describe a wedged HDD/MMCE emptying a good file via O_TRUNC and then failing the flush. The restore is the last line of defence on exactly that class of device, yet it issues one bare `write()` (return ignored), ignores `close()`, and then logs "restored %d original bytes" with the full `originalLen` regardless of how many bytes were actually written. A partial restore — the expected failure shape on the device class that brought us here — leaves a truncated config on disk while the log asserts a full restore, sending later diagnosis in the wrong direction. (The user-facing result is still an honest "save failed"; the harm is the half-restored file plus the false log claim.) Elsewhere in this codebase the pattern is done right: favsupport's `favWriteWhole` accumulates partial writes and read-back-verifies.
- **Fix (words):** Loop the restore write until the snapshot is fully written or an error occurs (the `favWriteWhole` pattern already in this codebase), check `close`, and log the byte count actually restored.
- **Benefit:** The anti-wipe guard either restores the whole file or says so honestly in the log.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-102 — `ConfigSet::getColor` reports success for a malformed colour value
- **Category:** 6 (bugs/security)
- **Locations:** `src/core/config.cpp:437-445`; the discarded result comes from `strToColor` at `:36-63`.
- **Problem:** `getColor` calls `strToColor(valref, color)` and returns 1 unconditionally, discarding the parser's return value. `strToColor` returns 0 for a value not starting with `#` or containing a non-hex first character — after having zeroed the caller's 3-byte colour array. Any present-but-malformed colour in a hand-edited config (e.g. `text_color=red`) is therefore reported as a successfully read colour of black — or a partially parsed colour when the corruption is mid-string — instead of "no usable value", which is what callers need in order to keep their default. The parser already computes the failure; the getter throws it away.
- **Fix (words):** Return `strToColor`'s result from `getColor` (the zero-first behaviour callers may rely on is preserved — `strToColor` itself does the zeroing).
- **Benefit:** A corrupt colour value falls back to the caller's default instead of silently painting UI text black.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the parser/getter mechanism (READ); INFERRED that callers treat a 0 return as "keep default" — that is the getter's contract everywhere else in this class (`getInt`/`getStr`).

### RIPTOPL-103 — `thmGetTexture` dereferences `gTheme` with no NULL guard, unlike every sibling accessor
- **Category:** 6 (bugs/security)
- **Locations:** `src/gfx/themes.cpp:938-951`.
- **Problem:** `gTheme` is NULL before the first `thmLoad`, and the file's own sibling accessors all guard (`thmGlyphSetRefresh`, `thmRefreshFontSize`, `thmSetViewLocked`, `isDecoratorCoverImage` each test for NULL first). `thmGetTexture` is the one public accessor that does not — after only an `id` range check it takes `&gTheme->textures[id]`. Any draw-path caller running before the first successful load, or after an abandoned load, dereferences NULL instead of receiving the NULL this function already returns for an invalid/unloaded texture — plainly its intended contract.
- **Fix (words):** Return NULL when `gTheme == NULL`, alongside the existing range check.
- **Benefit:** The texture accessor honours its own "NULL means nothing to draw" contract in the pre-load window.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the missing guard (READ); that a caller actually reaches it pre-load today is INFERRED.

### RIPTOPL-104 — `ConfigSet::clone` drops the set's on-disk format, silently breaking format inheritance for the clone
- **Category:** 6 (bugs/security) — inside the RIPTOPL-020 compat layer; a new defect within it
- **Locations:** `src/core/config.cpp:330-344` (`clone`), against `:287` (`init` sets `format_ = CFG_FMT_LEGACY`) and the write-side honouring at `:1588`.
- **Problem:** The config system's stated invariant (the file's own header comments) is *whatever format we read is the format we write back — we never convert a user's file*. `clone()` builds the destination through `create()`→`init()`, which resets `format_` to LEGACY, and never copies the source's `format_`. A clone of a set read from a wOPL/libconfig file therefore carries LEGACY, and if that clone is ever handed to `configWrite`, the file is rewritten in legacy syntax — the exact conversion the layer exists to prevent, destroying the file for wOPL. Containment today is verified: the sole in-tree caller of `configClone` (grep, this round) is the test-launch path in `src/ui/guigame.cpp:2197`, whose comment states the clone is never persisted — so this is **latent**, filed because `clone` is a general API whose contract comment says "deep copy", and format is part of the set's state everywhere except here.
- **Fix (words):** Copy `format_` in `clone()` alongside the entries (one assignment). Do not change the test-launch caller.
- **Benefit:** The deep-copy API actually copies the set's write-back semantics; the never-convert invariant stops depending on every future caller remembering not to save a clone.
- **Effort:** S · **Risk:** Low · **Confidence:** High for the mechanism and the sole-caller containment (READ); INFERRED that no out-of-tree caller writes a clone.

### RIPTOPL-105 — Standalone BT: the pressure-emulation flag is global, sticky, and cross-pad
- **Category:** 6 (bugs/security)
- **Locations:** `modules/ds34bt/iop/ds34bt.c:358` (flag), `:820` (only reset, in `ds34pad_init`), `:1295-1298` (set/use), `:1317` (side effect on `report->Power`); pademu counterpart `modules/pademu/ds34bt.c:1215-1225`.
- **Problem:** A single 21-byte DS3 report from **either** pad sets the one global flag, and from then on **both** pads have their real pressure bytes discarded and pressures synthesised as digital × 255, until all pads are re-initialised. The trigger is also narrower than the pademu copy's: exactly `bytes == 21`, where pademu treats any report shorter than the full DS3 report struct as pressure-less *for that report only* (and drops reports under 19 bytes outright). The emu path additionally forces `report->Power = 0x05` (`:1317`) — which accidentally bounds the RIPTOPL-092 index on that path.
- **Fix (words):** Make the decision per report (and per pad) as the pademu copy does: short report → emulate pressures for that report; full report → use the real pressure bytes. Delete the sticky global write.
- **Benefit:** One pad with a short-report clone/firmware no longer degrades the other pad's analog pressures for the whole session.
- **Effort:** S · **Risk:** Low (a pad that genuinely alternates report sizes flips modes per report — precisely the pademu behaviour already shipped in the sibling copy) · **Confidence:** Mechanism READ; how often a 21-byte report occurs in the field is INFERRED.

### RIPTOPL-106 — `get_data` memcpy length is unchecked in all four ds34 copies; only the pademu copies expose a variable length
- **Category:** 6 (bugs/security)
- **Locations:** `modules/ds34bt/iop/ds34bt.c:1519,1529`; `modules/pademu/ds34bt.c:1334,1340`; `modules/ds34usb/iop/ds34usb.c:701,725`; `modules/pademu/ds34usb.c:479,508`. The pad data union is 18 bytes in all four headers.
- **Problem:** None of the four `get_data` implementations clamps `size` to the 18-byte source. In the standalone copies the exposure is theoretical — their only caller is their own RPC dispatch, which hardcodes 18. In the pademu copies the length is genuinely caller-supplied: `pademu.c:467` passes `out_size - 3`, derived from the SIO transfer size. A size above 18 reads past the pad's data union into following struct fields (and, for the last pad, past the `ds34pad` array) and copies those bytes into the game's pad-response buffer.
- **Fix (words):** Clamp the copy length to 18 inside all four `get_data` functions, so the contract holds regardless of caller.
- **Benefit:** A one-line-per-file guarantee at the only place the bound is known; the standalone copies gain it before anyone reuses their RPC size field.
- **Effort:** S · **Risk:** Low (no legitimate caller can want bytes past the 18-byte report) · **Confidence:** Code READ; whether a game/SIO path ever presents a size > 18 is INFERRED (normal DS2 poll frames are 21 bytes total — exactly 18 after the 3-byte header — so the clamp would be a no-op for them).

### RIPTOPL-107 — Pademu ds34 copies dropped the standalone port guards on every public entry point; pademu USB `set_mode` also dropped the semaphore
- **Category:** 6 (bugs/security — hardening)
- **Locations:** Pademu BT `modules/pademu/ds34bt.c:1323,1334,1348,1360`; pademu USB `modules/pademu/ds34usb.c:468,479,528,544,555` (`set_mode` at `:528-534` takes no semaphore). Standalone guards: `modules/ds34bt/iop/ds34bt.c:1451-1454,1521-1524`; `modules/ds34usb/iop/ds34usb.c:687,703,851`.
- **Problem:** The pademu entry points index `ds34pad[port]` (and take `ds34pad[port].sema`) as their first action, with no range check; any out-of-range or negative `port` is an immediate out-of-bounds access on a static array, and USB `set_mode`'s unsynchronised `analog_btn` write races the `readReport`/`get_data` path that reads and toggles the same byte under the semaphore. Why this is low today (INFERRED from the caller, read for context): the sole in-tree caller, `modules/pademu/pademu.c`, derives `port` from SIO register bits (0–3) or literals 0/1 — inside pademu's MAX_PADS 4 — so no current call overflows. The standalone copies could not rely on this because their port arrives as an EE-supplied RPC byte; the guard is defence-in-depth for the pademu copies, one caller change away from being load-bearing, and it is the project's own convention.
- **Fix (words):** Restore the `port >= MAX_PADS` early-out (plus a negative check, since these signatures take `int`) in the eight pademu entry points, and put `ds34usb_set_mode` under the pad semaphore like its siblings. Do not add the standalone AUTHORIZED gates — pademu's caller already gates on RUNNING status.
- **Benefit:** The in-process API becomes as safe to call as the RPC one; removes the one unsynchronised state write in the USB copy.
- **Effort:** S · **Risk:** Low (in-range callers unaffected) · **Confidence:** Code READ; present-day unreachability INFERRED from the single caller.

### RIPTOPL-108 — ds34 report-input guards present in some copies only: standalone BT has no minimum-length check; standalone USB DS3 lacks the null-report rejection
- **Category:** 6 (bugs/security)
- **Locations:** Standalone BT `hid_readReport` at `modules/ds34bt/iop/ds34bt.c:1275` (no length check; contrasts pademu BT guards at `modules/pademu/ds34bt.c:1214-1217`); standalone USB DS3 parse at `modules/ds34usb/iop/ds34usb.c:315-345` (no null-report rejection; the rejection exists at standalone BT `:1285-1286`, in pademu BT's DS3 branch, and at `modules/pademu/ds34usb.c:318-319`).
- **Problem:** (a) A short or malformed BT interrupt packet is parsed as a full report in standalone BT; the source is a static buffer, so the tail is the previous report's bytes — phantom held buttons/sticks for one cycle rather than a crash. (b) On standalone USB, the all-zero "null report" the other three copies explicitly reject (it is emitted around LED/rumble commands) is accepted, overwriting pad state with zeros — centred sticks read as 0,0 (full deflection), all pressures 0 — for one poll. Both USB copies call `readReport` with no length argument at all, so guard (a) cannot be ported to USB without a signature change; guard (b) needs no length.
- **Fix (words):** Add the pademu BT minimum-length early-out to standalone BT's `hid_readReport`; add the two-line null-report rejection to standalone USB's DS3 branch. Do not restructure the USB signature in this fix.
- **Benefit:** The standalone copies stop acting on packets every sibling copy already considers invalid.
- **Effort:** S · **Risk:** Low (the guards only drop reports the other copies drop today) · **Confidence:** High (READ).

### RIPTOPL-109 — Standalone USB: the DS3 enable report is staged in the shared `usb_buf` that `LEDRumble` wipes immediately afterwards, with no completion wait
- **Category:** 6 (bugs/security)
- **Locations:** `modules/ds34usb/iop/ds34usb.c:305-313` (staging + control transfer with NULL callback), called from `usb_config_set` at `:274`, followed by `LEDRumble` at `:283` whose first act is `mips_memset(usb_buf, 0, sizeof(usb_buf))` at `:560`. The pademu copy fixed exactly this: `modules/pademu/ds34usb.c:279-285` — a dedicated static buffer with the comment "Own buffer: nothing waits for this transfer, and LEDRumble() rewrites usb_buf right after it."
- **Problem:** The pademu copy's own comment establishes that nothing waits for this transfer to complete. If the control transfer reads its data buffer asynchronously (the premise the pademu author coded against), the standalone sequence can deliver a zeroed/LED-shaped buffer as the DS3 enable report (feature 0xF4) — the enable may silently not take effect on some adapters/timing. The standalone BT driver stages its equivalent init in a private stack buffer, so this exposure is unique to standalone USB.
- **Fix (words):** Give the standalone USB enable report its own static 4-byte buffer, exactly as the pademu copy does; no other change.
- **Benefit:** Removes a race the sibling copy already diagnosed and fixed; less code than any wait-based alternative.
- **Effort:** S · **Risk:** Low (4 bytes of BSS) · **Confidence:** Sequence and pademu comment READ; that USBD actually consumes the buffer asynchronously is INFERRED (the pademu comment is the in-tree evidence; USBD source was not re-read for this section).

### RIPTOPL-110 — Standalone USB `ds34usb_set_bdaddr`: the DS4 pairing report sends 24 bytes; byte 23 is never written
- **Category:** 6 (bugs/security)
- **Locations:** `modules/ds34usb/iop/ds34usb.c:802-831` (writes at `:823,826-829`; 24-byte transfer at `:831`). No pademu counterpart (the API was dropped there).
- **Problem:** The DS4 branch writes indices 0–22 (1 report byte + 6 address + 16 link-key) and transfers 24 bytes; index 23 is whatever the module's single shared `usb_buf` previously held — by pairing time it has typically carried pad input and LED reports, so byte 23 is a leftover report byte; on a virgin module it is BSS zero. Nothing in the function memsets the buffer first (contrast `LEDRumble`, which does).
- **Fix (words):** Zero `usb_buf` (or at least the sent range) at the top of `ds34usb_set_bdaddr`, or send the 23 bytes actually written if the report is defined as 23 — the former is the safe choice absent protocol documentation in-tree.
- **Benefit:** The pairing feature report becomes deterministic.
- **Effort:** S · **Risk:** Low (byte 23 is padding on any reading of the writes) · **Confidence:** Write/transfer lengths READ; whether the receiving device inspects byte 23 is INFERRED (likely ignored padding — hence low severity).

### RIPTOPL-111 — Pademu BT: the charging-blink flag `oldled[3]` is set in `hid_readReport` but never cleared there (DS3 and DS4)
- **Category:** 6 (bugs/security — state drift)
- **Locations:** `modules/pademu/ds34bt.c:1266-1269` (DS3, no else), `:1305-1307` (DS4, no else). Clearing counterparts: `modules/ds34bt/iop/ds34bt.c:1337-1340,1434-1437`; `modules/pademu/ds34usb.c:346-349,382-385`; `modules/ds34usb/iop/ds34usb.c:342-345` — all if/else.
- **Problem:** Within pademu BT's report path nothing ever writes `oldled[3]` back to 0: `ds34pad_clear` does not touch `oldled`, the config-response path zeroes it once at connection time, and the LED function's trailer copies `led[3]` into `oldled[3]` — but its in-tree caller passes `oldled` itself, a self-copy. So once a DS3 reports charging (0xEE) or near-empty (0x01), or a DS4 reports plugged-in-and-not-full, the LED blinks for the rest of the connection — after unplugging, after charging completes, after the battery recovers.
- **Fix (words):** Add the missing else-clear in both pademu BT branches, mirroring the other three copies.
- **Benefit:** LED state again tracks the live battery state, as in the other three copies.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ — set/clear sites enumerated by grep over the whole file; the three sibling copies all clear).

### RIPTOPL-112 — Pademu USB `ds34usb_get_data` overwrites its analog-mode return value with the LED/rumble transfer result
- **Category:** 6 (bugs/security)
- **Locations:** `modules/pademu/ds34usb.c:508-520`; BT pademu counterpart `modules/pademu/ds34bt.c:1334-1345` (never clobbered); caller contract at `modules/pademu/pademu.c:467-471` (context read).
- **Problem:** The function's contract is to return the pad's analog-mode bit: the caller compares it against the current mode and adopts it on mismatch. But whenever a rumble/LED update is pending — which includes the first poll after init (`update_rum = 1` at init) and any poll in which `readReport` just set it — the tail call overwrites `ret` with the `UsbInterruptTransfer` result (0 on success). A pad toggled into analog mode therefore reports digital for that poll and is flipped back by the caller logic, then re-detected on a later poll.
- **Fix (words):** Keep the two results separate — save the analog return value before the LED/rumble tail and return that, using the transfer result only for the wait/error logging, as the code already does internally.
- **Benefit:** Analog/digital mode reported by pademu USB matches the pad's actual toggle state on every poll, matching BT pademu.
- **Effort:** S · **Risk:** Low · **Confidence:** Mechanism READ; the visible symptom (a one-frame mode flicker at toggle/first poll) is INFERRED from the caller's compare-and-adopt logic.

### RIPTOPL-113 — Pademu BT: fake-DS3 LED reports lost the fake transaction header (0xA2) in the port
- **Category:** 6 (bugs/security — dropped special case)
- **Locations:** Standalone `modules/ds34bt/iop/ds34bt.c:1212-1215` (`if (isfake) led_buf[0] = 0xA2; else … = HID_THDR_SET_REPORT_OUTPUT`); pademu `modules/pademu/ds34bt.c:1150-1151` (unconditional).
- **Problem:** The pademu copy kept the rest of the fake-DS3 machinery — the `enable_fake` option, the genuine-MAC table, and the fake rumble clamp — but not the one place the standalone driver changes the wire format for a fake pad: the first byte of the DS3 output report. If fake DS3s need the 0xA2 data header to accept LED/rumble reports (the only evident purpose of the special case), pademu's fake-DS3 support enables detection and then sends reports the pad may ignore — LED/rumble dead on exactly the pads the option exists for.
- **Fix (words):** Restore the `isfake` choice of the first byte in the pademu DS3 branch; no other change.
- **Benefit:** Makes the pademu `enable_fake` option self-consistent.
- **Effort:** S · **Risk:** Low (genuine pads already use the other branch in both copies) · **Confidence:** Divergence READ; device consequence INFERRED (no fake-pad behaviour is documented in-tree beyond the special case's existence).

### RIPTOPL-114 — DS4 rumble scaling `rrum * 255` exists only in the pademu copies and is correct only if `rrum` is a boolean
- **Category:** 6 (bugs/security — semantic divergence between copies)
- **Locations:** Pademu BT `modules/pademu/ds34bt.c:1186`; pademu USB `modules/pademu/ds34usb.c:424`. Pass-through counterparts: standalone BT `modules/ds34bt/iop/ds34bt.c:1250`; standalone USB `modules/ds34usb/iop/ds34usb.c:586`. All four DS3 branches pass `rrum` through unscaled.
- **Problem:** `rrum` is a `u8`; the product is truncated back into a `u8` field. For `rrum ∈ {0,1}` this is a sensible boolean→full-scale conversion (and the pademu caller context supports a boolean reading: the DS2 right/small motor is an on/off actuator). For any magnitude value it wraps non-monotonically: 2→254, 128→128, and a full-strength 255→**1**. The standalone copies, and the DS3 branch of the very same pademu functions, treat the byte as a magnitude. The two readings cannot both be right for the same input byte; the copies disagree about the contract and nothing in-tree documents it.
- **Fix (words):** Decide the contract once and make all four DS4 branches match it — if the byte is boolean, scale by `rrum ? 255 : 0` (identical result, no wrap for stray values >1); if it is a magnitude, drop the multiplication in the pademu copies. Either way, leave a one-line comment stating the expected range, since the next porter will otherwise re-derive it wrong.
- **Benefit:** Eliminates a wrap that turns maximum rumble into almost none under the magnitude reading, and documents an undocumented cross-copy contract.
- **Effort:** S · **Risk:** Low–Medium (choosing the wrong reading changes rumble feel for one caller family — which is why this is flagged as a decision, not silently normalised) · **Confidence:** INFERRED (divergence and arithmetic READ; the true input range depends on callers outside the four files).

### RIPTOPL-115 — `usb_bulk_get_max_lun` returns an uninitialised, signed byte when the device answers short
- **Category:** 6 (bugs/security)
- **Locations:** `modules/usb/usbmass_bd/src/usb_mass.c:329-365`; the return at `:355`.
- **Problem:** `char max_lun;` is uninitialised and signed; the function requests 1 byte via control transfer and, on a successful completion, returns `max_lun` without ever checking the callback's recorded `returnSize`. Sibling code in the same file treats a short CSW as invalid, but here a zero-byte successful completion returns whatever was on the stack, and a device byte of 0x80–0xFF sign-extends to a negative "count". Mitigating fact, stated plainly: the sole in-tree caller (`scsi.c:196-197`) logs the result and immediately overwrites `stat`, so today's blast radius is a garbage debug value and a broken contract for the function-pointer interface — not a live overflow.
- **Fix (words):** Initialise the byte to 0, use an unsigned byte, and only accept the value when `returnSize == 1`.
- **Benefit:** The LUN contract becomes deterministic; a future caller cannot inherit a stack-garbage count.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the defect (READ); low on present-day impact (caller behaviour verified in `scsi.c`).

### RIPTOPL-116 — smb_fio `DMA_sendEE` busy-spins twice with no bound
- **Category:** 6 (bugs/security — unbounded spin)
- **Locations:** `modules/network/smbman-ra/smb_fio.c:872-888`.
- **Problem:** Both loops are unbounded and yield nothing: the `sceSifSetDma` retry loop, and the `while (sceSifDmaStat(id) >= 0);` completion spin. If the SIF DMA channel is wedged or the transfer never completes, the calling IOP thread (share enumeration, under the I/O lock) spins forever and the whole SMB driver is dead behind it.
- **Fix (words):** Bound both loops with a retry/poll budget and fail the devctl (return an error, release the I/O lock via the existing unlock path) when it is exhausted.
- **Justification (fix adds a bound):** A counter and an error return at one helper; justified by a concrete hang, and LESS than adding timeout machinery per caller.
- **Benefit:** A wedged DMA degrades to a failed share-list request instead of a hung IOP.
- **Effort:** S · **Risk:** Low–Medium (the budget must be generous enough for a legitimate 512-byte DMA — it is short; any reasonable budget suffices) · **Confidence:** High (READ — no bound exists in the code).

### RIPTOPL-117 — mx4sio's DMA ISRs busy-spin on the SIO2 status bit with no bound — inside interrupt context
- **Category:** 6 (bugs/security — unbounded spin)
- **Locations:** `modules/mx4sio_bd/src/mx4sio.c:39-40` (RX ISR), `:57` (per-byte polling routine also called from the ISR), `:97-98` (TX ISR); ISR registration at `:196-206`.
- **Problem:** Both DMA-completion ISRs end in `while ((inl_sio2_stat6c_get() & (1 << 12)) == 0);` with no bound, in interrupt context. If the card is pulled or wedges mid-DMA so bit 12 never sets, the IOP never leaves the interrupt handler: no threads run, lower/equal-priority interrupts are never serviced, and the console hard-hangs with no recovery path (the card-detect thread that might notice removal can never execute).
- **Fix (words):** Give the in-ISR waits a bounded iteration budget; on exhaustion, set `cmd.abort`, signal completion through the existing event-flag path, and return — let thread context own any longer waiting.
- **Justification (fix adds a bound):** A counter in two ISRs; justified by a total-system hang, and LESS than moving the PIO-completion wait out of the ISRs (the larger refactor).
- **Benefit:** A dying/removed SD card fails the transfer instead of freezing the console.
- **Effort:** S–M (must preserve the abort/event-flag handshake both ISRs already use) · **Risk:** Medium (ISR timing is delicate; the budget must comfortably exceed a healthy PIO completion) · **Confidence:** High (READ — no bound exists; ISR context is explicit in the registration).

### RIPTOPL-118 — The same unbounded SIO2 status spin is open-coded in all seven thread-context PIO helpers in mx4sio
- **Category:** 6 (bugs/security — unbounded spin) / 2 (duplication of the pattern)
- **Locations:** `modules/mx4sio_bd/src/mx4sio.c:254, 293, 333, 375, 417, 464, 525`.
- **Problem:** Every byte/block PIO primitive ends its transfer with the identical unbounded poll (`write_byte`, `write_dummy`, `wait_equal`, `wait_not_equal`, `wait_equal_masked`, `rx_pio` per 256-byte chunk, `tx_pio` per chunk). The `wait_*` functions do have outer byte-count budgets — but a single stuck byte defeats them, because the inner poll never returns. These run in thread context while holding the SIO2 lock, so one stuck transfer also blocks memory-card (mcemu) and pad access that shares SIO2, indefinitely. Distinct from RIPTOPL-117 (interrupt context, DMA completion); same file, same bit, different failure domain.
- **Fix (words):** Factor the poll into one bounded wait helper with an error return, and have the seven sites propagate that error the way `mx_sio2_wait_equal` already propagates timeouts to its callers.
- **Justification:** ADD-neutral to LESS — one helper replaces seven open-coded loops, and the timeout idiom already exists in this file.
- **Benefit:** Card removal mid-command becomes a recoverable SPI error (the driver above already handles those) instead of a permanent SIO2 lock-out.
- **Effort:** M (seven call sites, one file) · **Risk:** Low–Medium (timeouts must exceed worst-case healthy SIO2 completion at the slow baud setting) · **Confidence:** High (READ — seven identical unbounded loops).

### RIPTOPL-119 — SMAP: in both neutrino copies `HandleRxIntr` never counts a packet, so the link-check suppression both `smap.c` copies implement is dead
- **Category:** 6 (bugs/security) / 2 (duplication — drift)
- **Locations:** `modules/network/udpfs_smap/src/xfer.c` — counter declared `:162`, zeroed `:169`, returned `:203`, never incremented (grep-complete for the file); identical shape in `modules/network/smap_udpbd/src/xfer.c` (`:111,119,151`). Consumers: `modules/network/udpfs_smap/src/smap.c:431` and `modules/network/smap_udpbd/src/smap.c:437`, intent stated at `udpfs_smap/src/smap.c:442-446`. Contrast: `modules/network/smap-ingame/xfer.c:212` does increment (ingame's `smap.c` discards the return and polls unconditionally — a coherent, different design).
- **Problem:** Because the return is permanently 0, the reset flag is permanently false and the 3-strike counter decrements on every 1-second link-check event regardless of traffic: `CheckLinkStatus` runs roughly every 3 seconds even mid-transfer. `CheckLinkStatus` itself is guarded (it only re-runs PHY init when the BMSR link bit reads clear), so the standing cost is a PHY register read every ~3 s — negligible; the hazard is the tail case (INFERRED): a transient BMSR clear under load triggers a full PHY re-initialisation in the middle of active UDPFS/UDPBD traffic, in exactly the situation the counter was designed to prevent.
- **Fix (words):** Increment the counter once per processed frame in both `xfer.c` copies (the ingame line, in the neutrino loops' frame-processed position) — or, if the suppression is not wanted, delete the reset-flag/counter logic from both `smap.c` copies and poll unconditionally like ingame. Either way the two halves must agree; today they describe a mechanism that does not exist.
- **Benefit:** The documented traffic-aware link check either works or stops pretending to; removes a mid-transfer PHY-reset tail risk.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the dead counter (READ); INFERRED for the transient-BMSR consequence.

### RIPTOPL-120 — The `config.path` bootstrap redirect is rewritten non-atomically (O_TRUNC, two writes, unchecked close), and the write leg is duplicated
- **Category:** 6 (bugs/security) / 2 (duplication)
- **Locations:** `src/app/settingsio.cpp:348-354` (primary leg), `:361-377` (MC mirror leg) in `writeConfigPathRedirect`.
- **Problem:** Both legs open the live redirect file with `O_WRONLY | O_CREAT | O_TRUNC`, then issue two separate writes (path, then `"\n"`), discard `close()`'s return, and derive success only from the two write counts. There is no temp-file-plus-rename and no single combined write, so a reset/power loss (or a failing card) between truncate and completion leaves an empty or partial `config.path`. The reader tolerates that by returning 0 — the Custom Settings Path bootstrap is silently lost and the next boot falls back to discovery/defaults, with no error anywhere. This file is otherwise extremely careful about exactly this class of loss; the redirect file — the one artifact that exists solely to survive a reboot — is the one written least defensively. (The payload configs' own write discipline lives in config.cpp; its restore path is RIPTOPL-101.)
- **Fix (words):** Compose the full line (path + `"\n"`) in one stack buffer and issue one `write`; check `close()`; factor the identical open/write/close sequence into one static helper used by both legs (deleting the second copy). A temp+rename would be stronger still but is an ADD — adopt it only if the single-write form is judged insufficient; the single-write + helper form is the LESS-NOT-MORE fix.
- **Benefit:** Shrinks the corruption window to one write and removes ~10 duplicated lines; mirror and primary can no longer drift.
- **Effort:** S · **Risk:** Low · **Confidence:** High on the mechanism (READ); power-loss-mid-write timing is INFERRED (no hardware test).

### RIPTOPL-121 — VCD scan allocates the 2,048-entry cap (~532 KB) for every folder, however small
- **Category:** 4 (size/memory)
- **Locations:** `src/sources/vcdsupport.cpp:653` (scan allocation); second full-size copy at `:1037` (`vcdFillGameList`).
- **Problem:** `calloc(VCD_MAX_ITEMS, sizeof(vcd_entry_t))` allocates 2048 × 260 B = **532,480 B unconditionally** at the start of every scan, whether the POPS folder holds 2 games or 2,000. The list is then copied into a second allocation (up to another ~385 KB at the cap) before the first is freed — peak transient heap for one scan approaches ~0.9 MB on a 32 MB EE, for a data set whose common case is tens of entries (~5 KB). The cap is a correctness bound, not a working-set estimate; nothing in the function needs random access to unwritten slots. (Entry size 260 B derived from the header layout, READ.)
- **Fix (words):** Grow the array instead of pre-sizing it — start at a small capacity (e.g. 64 entries) and double with `realloc` when the count reaches capacity, failing the scan exactly as the current OOM branch does if a grow fails. The existing `count < VCD_MAX_ITEMS` loop condition stays as the hard cap.
- **Justification (fix adds a grow branch):** Adds only a capacity variable and a grow branch inside code that already exists; removes a fixed half-megabyte reservation — a net win on the scarcest resource in the system.
- **Benefit:** Per-scan heap peak drops from ~532 KB (+ copy) to roughly the real library size; removes the largest single transient allocation on the list-build path.
- **Effort:** S–M · **Risk:** Low (one function, existing OOM semantics reused) · **Confidence:** High (READ — allocation size is arithmetic from header constants).

### RIPTOPL-122 — BDMA launch validation is computed three times per launch, each time re-walking the memory card
- **Category:** 2 (duplication) / 5 (performance)
- **Locations:** `src/sources/vcdsupport.cpp:1769-1838` (the two validators + OR wrapper), `:1848,1876` (`vcdEnsureBdmaForLaunch`), `:1899,1903` (`vcdApplyUsbModeForLaunch`).
- **Problem:** `vcdBdmaEnvironmentValid` and `vcdBdmaManualPairMatches` each independently: probe both memory cards for the POPSTARTER folder, compose the same module paths, and stat both modules. `EnvironmentValid` additionally reads the BDMA mode marker, which itself re-probes the cards — so one "valid?" question costs ~4 directory probes and 3 file opens. `vcdBdmaLaunchReady` is just the OR of the two. The launch paths then recompute the pair: ensure evaluates both, equips if needed, and re-runs `EnvironmentValid`; the USB-mode path repeats the pattern; the external UDPFS caller also gates on `LaunchReady`, stacking another full evaluation into the same launch. All of this IO is on mc0:/mc1: — among the slowest devices in the system — in front of a launch the file's own comments say must stay a "simple handoff".
- **Fix (words):** Collapse the two validators into one function that finds the POPSTARTER folder once, reads the marker once, stats both modules once, and returns a three-way result (valid-by-marker / valid-manual-pair / invalid). All three consumers use that single result; the post-equip re-check reuses the same function (one evaluation, not a second pair). This deletes one of the two near-identical bodies outright.
- **Benefit:** Roughly 2–3× fewer MC opens/probes per VCD launch validation, and the marked-vs-manual policy lives in exactly one place (today a size-signature or marker-rule change must be made consistently in two bodies).
- **Effort:** M · **Risk:** Medium-low (the marked/manual distinction and the FAT32 special case must be preserved exactly in the merged result; behaviour is otherwise pure query code with no writes) · **Confidence:** High (READ — call graph and per-call IO enumerated from the code; no timing claimed).

### RIPTOPL-123 — VCD scan is quadratic twice: per-entry cue dedup scans + per-entry linear memo walk
- **Category:** 5 (performance, structural)
- **Locations:** `src/sources/vcdsupport.cpp:695-715` (cue/VCD dedup) with `noteScanDir` → `idMemoFind` at `:308-345`, called per entry at `:716`.
- **Problem:** Two nested-loop costs multiply the scan's readdir loop. (a) UDPFS cue handling: each `.cue` row linear-scans all previously kept rows, and each subsequent `.VCD` row does the same scan again while any cue exists — O(n²) string compares over up to 2,048 rows. (b) Independently, **every** scanned entry (all devices) calls `noteScanDir`, whose first act is a linear `strcmp` walk of the session memo, which grows to its 512-entry cap as the scan proceeds — a full folder costs on the order of 2,048 × ~256 average memo probes ≈ 500k `strcmp`s of bookkeeping on top of the directory read, on the IO worker that also serves artwork and BGM. Neither loop is bounded by anything small.
- **Fix (words):** (a) Do the cue/VCD twin suppression once, after collection — the entries are already `qsort`ed when autosort is on; where it is off, sort a throwaway copy of the name order for dedup only, then keep first occurrences adjacently. That deletes both inner scans. (b) For the memo: build the scan's memo notes into a temporary array and merge once; if that merge is judged too much new code, ship (a) alone and record (b) as accepted — the memo cap (512) bounds it, the scan does not.
- **Justification:** (a) adds no persistent structure — it reuses the sort the file already performs. (b) is explicitly conditional; no new subsystem proposed.
- **Benefit:** Scan cost returns to ~O(n log n); large POPS folders (the 2,048 cap exists because they occur) stop paying quadratic bookkeeping on every refresh.
- **Effort:** M · **Risk:** Medium for (a) — twin-suppression semantics ("a real .VCD wins", cue pair counts once) must be preserved exactly, including when autosort is off · **Confidence:** High for the loop structure and bounds (READ); INFERRED for real-world impact (no profiling possible here; typical folders are far below the cap).

### RIPTOPL-124 — `FavSource::image` rescans the whole favourites store from zero for every art request — the sibling lookup in the same file already has the fix
- **Category:** 5 (performance)
- **Locations:** `src/sources/favsupport.cpp:1321-1408` (`image`); contrast `:919-960` (`artMode`).
- **Problem:** `artMode`'s comment states: "All Games can hold thousands of rows and this runs for every art request, so search outward from the last match" — the neighbour-hint optimisation was applied to `artMode`, the *device-mode* lookup, but not to `image`, the lookup that actually fetches the art, although both are called per art request, both scan the same array, and `image`'s per-entry work is heavier (a `startup()` call into the owner support per row, plus a game-ID extraction fallback per PS1 row). On an absolute-path miss it runs a **second** full scan in the passthrough loop. Covers near the end of a large All Games list pay the full scan every time; the rows asked about are, per artMode's own comment, almost always neighbours of the previous match.
- **Fix (words):** Give `image` the same outward-from-last-match search `artMode` already implements (share the hint member or replicate the pattern in this one function). No new structure; the pattern exists in the same file for the same array.
- **Benefit:** Per-cover cost on large Favourites/All Games lists drops from a full scan to a neighbourhood probe in the common sequential-browsing case.
- **Effort:** S · **Risk:** Low (search order changes only; the scan still covers every entry before answering −1) · **Confidence:** High on structure and per-entry calls (READ); the wall-clock saving is INFERRED — nothing was profiled.

### RIPTOPL-125 — `bdmHDDIsPresent` can burn its timeout twice on one call, on the settings-save path
- **Category:** 5 (performance / blocking)
- **Locations:** `src/sources/bdmsupport.cpp:2276-2323`; caller `src/app/settingsio.cpp:2015` passes 5000 ms.
- **Problem:** When the device wait *succeeds* but the answered device turns out not to be ATA, neither `timedout` branch is taken, so step 3 runs a full second `DelayThread(timeoutMs * 1000)` before rescanning — after already waiting up to `timeoutMs` for a device that did answer. On the settings-save path that is up to ~10 s of IO-worker blocking for a single probe (5 s wait + 5 s sleep), inside a deferred-IO save whose overall abandon bound is 30 s. The other branches set `timedout = 1` and skip the second sleep — only the "device answered but is not ATA" branch pays double.
- **Fix (words):** Charge the step-3 sleep only the *remaining* budget (`timeoutMs` minus elapsed wait), or skip it when the wait already consumed the budget. No new mechanism.
- **Benefit:** Worst-case save-path probe drops from ~2× to 1× the requested timeout.
- **Effort:** S · **Risk:** Low (the rescan still happens; only the redundant full-length sleep shrinks) · **Confidence:** High on the arithmetic (READ); actual on-hardware save latency is INFERRED.

### RIPTOPL-126 — The IPCONFIG.DAT line is implemented three times in one file, with two parsers of different strictness for the same format
- **Category:** 2 (duplication) / 6 (parsing consistency)
- **Locations:** `src/sources/vcdsupport.cpp:2471-2478` (`vcdParseIpConfig`, sscanf), `:2118-2145` (`vcdUdpfsParseQuad`, strict), `:2796-2802` (`vcdBuildIpConfig`), `:2279-2282` (UDPFS writer's inline snprintf of the same format).
- **Problem:** The `"<IP> <MASK> <GW>"` line exists as: a lax parser (sscanf + range check), a strict parser (digit-by-digit, rejects an octet the moment it passes 255, requires a delimiter or end after the last octet), and two separate serializers. The two parsers disagree on real inputs: the sscanf form accepts trailing junk after the final octet (its last `%d` happily parses `4` out of `4xyz` and still returns 12 conversions) and accepts embedded junk the strict form rejects — so a file the POPSTARTER-net editor calls valid can be one the UDPFS-net editor calls invalid, and vice versa, for files in the same format family read by the same external program. `%d` conversion of an over-long digit string in sscanf is also implementation-defined territory the strict parser was clearly written to avoid — for the *other* struct. (The strict parser itself was verified clean this round — see Checked-and-clean.)
- **Fix (words):** Delete `vcdParseIpConfig`'s sscanf body and implement it on top of `vcdUdpfsParseQuad` (skip-space, parse quad, three times — the UDPFS reader already shows the exact pattern), so both net editors share one strictness. Leave the two serializers: they write two different structs, and merging them would need a common struct or an adapter — that would be MORE, not less.
- **Benefit:** One definition of a valid IPCONFIG line; removes the sscanf-overflow surface from this file entirely.
- **Effort:** S–M · **Risk:** Low–Medium (files previously accepted only by the lax parser — trailing junk — become "invalid" in the editor, which is the repair-don't-shadow behaviour the surrounding code already prefers; POPSTARTER's own tolerance is out of scope and unchanged on disk) · **Confidence:** High for the duplication and the sscanf acceptance mechanism (READ); INFERRED for how many real-world files sit in the disagreement gap (expected: ~none).

### RIPTOPL-127 — SMAP: three stored copies of one Ethernet driver; the two neutrino copies are one file twice, the ingame copy is a different generation that cannot be merged with them
- **Category:** 2 (duplication)
- **Locations:** `modules/network/smap-ingame/` (`smap.c` 651 + `xfer.c` 339 + `main.c` 218); `modules/network/smap_udpbd/src/` (641 + 217 + 85); `modules/network/udpfs_smap/src/` (639 + 279 + 26). Provenance (READ, in-tree `ORIGIN.txt` files): udpfs copy vendored from neutrino; udpbd copy vendored from nhddl, itself neutrino-lineage; ingame copy is the OPL/SMSTCPIP in-tree driver. (Not covered by RIPTOPL-048, which is the UDPFS headers — SMAP is new.)
- **Problem:** Measured this round: udpbd ↔ udpfs `smap.c` differ by **25 raw diff lines** (comment-stripped: 8 additions / 9 deletions) — exactly the udpbd init hooks, the udpfs DMA-block-size global, and one parameter spelling. Ingame ↔ either neutrino copy is ~530 raw diff lines — a genuinely different driver generation (interrupt model, stack glue, assembly FIFO path, link-check design). Every PHY/EMAC fix must currently be made up to three times, and RIPTOPL-093 and RIPTOPL-119 are the proof it already doesn't happen evenly: both are defects present in **both** neutrino copies and absent (in different ways) in ingame. The three-copy state is partly forced — the three IRXs load in mutually exclusive contexts and register the same module names, so the copies cannot collapse to one binary. The neutrino pair's `smap.c` **source** has no such excuse: at 8/9 stripped lines of difference it is one file maintained twice. Their `xfer.c` gap (165 diff lines) is real design, unlike their `smap.c`.
- **Fix (words):** (1) Merge the neutrino pair's `smap.c` into one shared source consumed by both modules via the repository's own established shim pattern (the `udpfs_shared` `#include` shims of RIPTOPL-048), with the three genuine deltas selected by one build-time define each module already effectively has. (2) Leave ingame alone: its divergence is architectural, and forcing it into the same file would add more conditional bulk than it removes. Record the decision in both neutrino `ORIGIN.txt` files so the next re-vendor preserves the sharing instead of re-forking the file.
- **Justification (fix uses the shared-include pattern):** It replaces two ~640-line bodies with one; net source drops by roughly 600 lines and the pattern is copied from the tree's own UDPFS precedent, not invented.
- **Benefit:** One place for SMAP PHY/init fixes in the neutrino line; RIPTOPL-093/-119-class drift stops recurring there.
- **Effort:** M · **Risk:** Medium (touches two shipped IRXs' build inputs; wants a bench pass per module — a build is not proof) · **Confidence:** High (READ — diffs computed this round; provenance from the in-tree ORIGIN files).

### RIPTOPL-128 — `apa.c` carries the other half of RIPTOPL-052's unreachable partition-edit machinery (~250 lines), and its internals are not safe to resurrect
- **Category:** 1 (dead code)
- **Locations:** `modules/hdd/apa/src/apa.c` — `apaFillHeader` `:105-134`, `apaInsertPartition` `:136-168`, `apaRemovePartition` `:213-233`, `apaDeleteFixPrev` `:255-306`, `apaDeleteFixNext` `:308-362`, `apaDelete` `:364-410`.
- **Problem:** RIPTOPL-052 established the principle for `hdd_fio.c`: the loader never edits the partition table, so the editing implementation is pure audit surface in a disk-destroying driver. The same implementation's core lives in `apa.c` and was not covered. Every in-tree caller sits behind the `-EACCES` refusals catalogued in RIPTOPL-052 (via `hddAddPartitionHere` ← the dead `hddFormat`/`ioctl2AddSub` bodies), and `apaOpen` independently refuses creation. Worse, this half is not merely dead but **unsafe to re-enable**, which "keep for diffability" arguments tend to discover too late: `apaInsertPartition` dereferences `clink_empty->header` with no NULL test (`:154,156`); it calls `apaCacheFree(clink_this)` and **then** reads `clink_this->header` fields as `apaFillHeader` arguments (`:163-166`) — safe today only because `apaCacheFree` is a non-destructive return-to-pool, so any cache change silently turns it into a use-after-realloc; and its halving loop terminates only when a block's length exactly equals the requested size — `length >>= 1` reaches 0 for a non-divisor size and the loop then splits a zero-length block at its own start indefinitely.
- **Fix (words):** Extend RIPTOPL-052's deletion to these six functions in `apa.c` (and `hddAddPartitionHere` in `hdd.c` if the cross-file step is accepted), keeping the refusing stubs in `hdd_fio.c` as the only partition-edit surface. The PS2SDK upstream file remains the diffability reference, as RIPTOPL-052 already decided.
- **Benefit:** ~250 further lines removed from the APA driver; the "never edits partitions" property becomes checkable from `apa.c` + the ops table alone; three latent defects leave with the code.
- **Effort:** M (caller sweep across `hdd.c`/`hdd_fio.c` must be re-confirmed at edit time) · **Risk:** Low (unreachable code; same caveat as RIPTOPL-052 — a build is not proof, but unreachable-code deletion changes no runtime path) · **Confidence:** High for the bodies and the latent defects (READ); caller classification follows round 3's full `hdd_fio.c` read plus this round's grep of every caller.

### RIPTOPL-129 — The network block-device load chain is implemented twice in bdmsupport, with divergent latch/dev9 handling
- **Category:** 2 (duplication)
- **Locations:** `src/sources/bdmsupport.cpp:751-825` (`ensureNetworkSourceModules`) vs `:1325-1375` (network leg of `bdmLoadBlockDeviceModules`).
- **Problem:** Both legs build the same `ip=%d.%d.%d.%d` argument, call `sysInitDev9()`, and load the same chains in the same order (UDPFS: smap → ministack with ip arg → bd; UDPBD: smap_udpbd with ip arg), setting the same latches. They already differ in the details that matter: the ensure version writes the member protocol field directly (including after the SMAP step, before the ministack has succeeded), the worker version goes through the setter; the worker wraps loads in the diag stage helpers and releases dev9 only `if (!udpbdNicClaimed)` at the end, the ensure version releases it inline. Any future IRX-chain change (order, args, a fourth module) must be made in both, and a one-sided edit leaves the custom-path loader and the menu loader describing different resident transports — the exact failure the file's long comment says the loaded-protocol identity exists to prevent. (The ensure leg's semaphore handling was traced this round and is clean — see Checked-and-clean; only the chain duplication is filed.)
- **Fix (words):** One static chain-loader (protocol + diag-stage flag in, result + latch updates out) called by both entry points; keep each caller's own locking/timeout policy outside it.
- **Benefit:** ~50 lines deleted; one chain definition, so the two entry points cannot diverge again.
- **Effort:** M · **Risk:** Medium (semaphore ownership differs between callers — ensure holds the load lock across its leg; the worker takes it for the whole function) · **Confidence:** High (READ).

### RIPTOPL-130 — The per-slot identity-enumeration loop is copy-pasted at least four times in bdmsupport, and the network wait loop re-runs it from slot 0 every 100 ms
- **Category:** 2 (duplication) / 5 (performance)
- **Locations:** `src/sources/bdmsupport.cpp:495-525`, `:527-553`, `:555-585`, `:631-675`; a fifth copy in the `__DEBUG` resolver at `:372-450`; polling caller at `:815-822`.
- **Problem:** Four separate loops each hand-roll the same probe — compose `mass%d:/` → open/identity read (driver-name ioctl + device-number ioctl) → determine device type — differing only in what they do with a match (first root / all slots / typed-index counting / summary). Beyond the maintenance cost, `ensureNetworkSourceModules` waits for the UDPBD root by calling the first-root variant in a 100 ms poll until timeout, and every poll restarts the scan at slot 0 with fresh open+ioctl round-trips on every preceding slot — on the IO worker, for the whole timeout, when the target device is by definition the last one the scan reaches.
- **Fix (words):** One enumeration helper that fills a small per-slot identity array once; the four consumers read from it. The wait loop can then poll the helper (or just the one slot range) instead of re-probing from zero. Pure restructure.
- **Benefit:** One probe implementation to harden (cf. RIPTOPL-091, which is what divergent copies cost); materially fewer IOP round-trips during network-source waits.
- **Effort:** M · **Risk:** Medium (callers have subtly different tolerance for a failed device-number ioctl — one is documented in-file; the helper must preserve "driver name alone is enough") · **Confidence:** High (READ).

### RIPTOPL-131 — Per-device config load/save legs in settingsio are copy-pasted per slot and per direction
- **Category:** 2 (duplication)
- **Locations:** `src/app/settingsio.cpp:88-118` (MMCE load) / `:1984-2001` (MMCE save), same shape at `:120-149` / `:2028-2040` (HDD) and in the BDM/MC save legs (`:2003-2047`).
- **Problem:** The MMCE load function contains two near-identical legs differing only in the literal `"mmce0:"`/`"mmce1:"` (opendir → closedir → end/init → read-multi → set start-mode AUTO → return). The save function repeats the two-leg pattern a second time in the other direction. Each additional slot or policy tweak must be edited in four places, and the legs have already had to grow slot-preference logic elsewhere in this file (the recovery path's slot-order array) that these older legs do not share.
- **Fix (words):** One loop over a two-entry slot table inside each function, or one shared probe helper parameterised by direction. Pure deletion/restructure, no new behaviour.
- **Benefit:** ~25 lines deleted; load and save slot policy cannot diverge again.
- **Effort:** S · **Risk:** Low–Medium (must preserve the early-return-on-first-success and the AUTO-mode side effect per leg) · **Confidence:** High (READ).

### RIPTOPL-132 — The IP-quad row pattern and the POPSTARTER network tables duplicate each other in dialogs.cpp
- **Category:** 2 (duplication)
- **Locations:** `src/ui/dialogs.cpp:44-283` (`diaNetConfig`), `:283-352` (`diaPopsUdpfsConfig`), `:354-450` (`diaPopsNetConfig`).
- **Problem:** One IPv4 address costs 9–11 table rows here (4 int rows + 3 dot labels + label/spacer/break), and the pattern is written out in full at least ten times in `diaNetConfig` alone. More pointedly, `diaPopsUdpfsConfig` and `diaPopsNetConfig` define the *same* fields with the *same* ids (IP/mask/gateway/SMB-IP quads) in two separate tables — the UDPFS sub-dialog is a near-copy of the top half of the SMB dialog — so any change to those rows (range, hint, ordering) must be made twice and can silently drift.
- **Fix (words):** Introduce one C macro that expands to a full quad's rows inside these initialisers (the tables are plain array initialisers, so a multi-row macro is a pure deletion), and express the shared POPSTARTER quads once. No runtime change.
- **Benefit:** Roughly 150–200 table lines deletable; the duplicated POPSTARTER fields get a single definition.
- **Effort:** M · **Risk:** Low–Medium (initialiser order is display order — expand in place, change nothing else) · **Confidence:** High (READ).

### RIPTOPL-133 — The twelve-family theme roster is hand-copied in at least six places in themes.cpp
- **Category:** 2 (duplication)
- **Locations:** `src/gfx/themes.cpp` — identical 12-entry group arrays built independently at `:196-198` and `:334-336`; twelve per-family calls at `:313-324`; 56 explicit per-family calls across the validate/coverage/text-room/link/promote passes at `:533-652`; twelve families freed by name in `thmFree`; an 11-entry "others" array (same roster minus main) at `:1130-1134`.
- **Problem:** The roster of element families (main/info × games/apps/favs/vcd/favsVcd/favsApps) is a single logical list with no single representation in this file. The favsVcd/favsApps families are the newest additions, and each had to be threaded through every one of these enumerations by hand; the file's own comments record two bugs that were exactly a missed enumeration (a family the slot-based pass never reached, a decorator the split pass never recognised). The next family — or the next per-family pass — has the same six-plus chances to be silently skipped for one family, producing a page that alone loses its decorator split, its text rooms, or its recolour, with no compile error.
- **Fix (words):** One file-local helper/array that yields all twelve family pointers (with main flagged), consumed by the passes that treat families uniformly; the genuinely special cases (main's recolour, the fav-kind background rule) keep their explicit code and take the roster from the same source. This deletes five of the six copies; it adds no abstraction beyond the one table the file already rebuilds six times.
- **Justification:** The one shared roster array replaces six hand-maintained copies — net lines and net decision points both drop.
- **Benefit:** Adding a family or a per-family pass becomes a one-line roster change instead of a scavenger hunt; the class of "family X was missed by pass Y" bugs closes.
- **Effort:** M · **Risk:** Low–Medium (touches the theme-load path broadly; behaviour for the current twelve families is unchanged by construction, but wants a theme-switch bench pass) · **Confidence:** High (READ).

### RIPTOPL-134 — One Neutrino resolve can probe the same 8 memory-card candidates twice, from three overlapping variant tables
- **Category:** 2 (duplication) / 5 (performance)
- **Locations:** `src/sources/supportbase.cpp:2555-2585` (`sbResolveNeutrinoPath`), `:2539-2547` (migrated tier), `:2517-2537` (MC probe), `:2027-2068` (game-device probe), `:2491-2515` (migrated-APA tier).
- **Problem:** The resolve order is custom → migrated → game-device → MC. When the retired picker choice was MC, the migrated tier *is* the MC probe — all 8 candidates, each costing an existence probe (open/close) plus, on a hit-shaped path, an install-complete check (two more opens). If that fails and the game-device probe also fails, tier 3 runs the identical MC probe **again** — the same 8 candidates, re-opened, in the same resolve, on memory-card IO, immediately before declaring "no complete install found". Separately, the case-variant knowledge is tabulated three times with three different memberships (game-device forms, migrated-APA variants, MC candidates) — an editor fixing one table silently leaves the others stale.
- **Fix (words):** In `sbResolveNeutrinoPath`, skip the tier-3 MC probe when the migrated tier already ran it (a one-line condition on the MC device having been tried). That is the whole fix proposed here; unifying the three tables is *not* proposed — their bases genuinely differ, and a shared table-builder would be MORE machinery.
- **Benefit:** Faster, quieter failure when Neutrino is absent; no change to any success path (the first probe that hits still returns immediately). Deletes up to 8 redundant open-probes (plus their install-complete probes) from the not-found path — the path every user without a card install pays at every Neutrino launch attempt.
- **Effort:** S · **Risk:** Low (the migrated tier and tier 3 return identical results for the MC candidate set by construction — same function) · **Confidence:** High (READ).

### RIPTOPL-135 — `vcdGetFileSize` and `vcdSourceFileSize` are the same function written twice, one a superset of the other
- **Category:** 2 (duplication)
- **Locations:** `src/sources/vcdsupport.cpp:1720-1731` and `:1734-1756`.
- **Problem:** `vcdGetFileSize` opens a path and returns `lseek(fd, 0, SEEK_END)`. Fourteen lines later `vcdSourceFileSize` opens a path, does the identical `lseek`, and only differs when `lseek` fails — then it counts the file by reading it through a 16 KB heap buffer (the MMCE-newlib fallback documented in its comment). Every caller of the first function (the MC pair checks) wants exactly "size or −1"; on a real memory card `lseek` works, so the fallback would never trigger there, and where it could trigger its answer is strictly more correct than −1 ("absent").
- **Fix (words):** Delete `vcdGetFileSize` and call `vcdSourceFileSize` at its two remaining call sites (or keep one name). No caller changes behaviour on any device where `lseek` succeeds.
- **Benefit:** One size-probe implementation to keep correct (the SEEK_END quirk is currently documented in two places, `vcdSafeCopyFile` making a third). ~12 lines deleted.
- **Effort:** S · **Risk:** Low (on a hypothetical MC driver without SEEK_END the fallback reads ≤ ~62 KB driver files to size them — bounded and rare) · **Confidence:** High (READ).

### RIPTOPL-136 — `diaPadEmuInfo` hand-enumerates all 64 Bluetooth feature bits as literal rows
- **Category:** 2 (duplication) / 4 (size/memory)
- **Locations:** `src/ui/dialogs.cpp:1741-1913` (feature grid at `:1797-1911`); same unrolled pattern in miniature at `:1969-2003` (12 rows) and `:2044-2081` (16 rows).
- **Problem:** The table contains 64 individually written label rows (`PADCFG_FEAT_START + 0` through `+ 63`), laid out in a transposed order (each visual row runs +0,+8,+16,…,+56, then +1,+9,…), with the `"00."`–`"63."` captions also written out literally. That is ~115 lines of table for one repeating pattern, and any renumbering or layout slip is invisible to the compiler — a transposed pair would simply label the wrong feature bit in the BT info screen.
- **Fix (words):** Build the feature grid (and, if touched anyway, the file-manager/drive-status row runs) into a static `UIItem` array at first use with a loop — the dialog arrays are already mutated at runtime by consumers, so a lazily initialised table is consistent with existing practice. Keep the transposed index arithmetic in exactly one place, commented.
- **Benefit:** ~100 lines deleted from this file; the bit-to-label mapping becomes provable by reading one loop.
- **Effort:** M · **Risk:** Medium (row order/ids must be preserved exactly — display-only dialog, no persisted state) · **Confidence:** High (READ).

### RIPTOPL-137 — `prepareCustomApaSettingsPath` is a ~190-line hand-rolled parser with three ad-hoc separator-eating loops and a dual-purpose buffer
- **Category:** 3 (overengineering)
- **Locations:** `src/app/settingsio.cpp:400-590` (whole function); separator loops at `:451-458`, `:477-495`, `:505-512`; buffer reuse at `:400-402` vs `:518-527`.
- **Problem:** The function re-implements path tokenisation at least three times — one loop to skip separators after a pfs alias, one while-chain to consume partition label + redundant `pfs:` components in the hdd branch, one to consume a redundant `OPL` component — each with slightly different terminator sets. The 64-byte `subfolder` buffer serves two unrelated meanings in sequence (first the APA partition label for the `gOPLPart` comparison, then the remaining sub-path), so a reader must track which lifetime is live. The function is correct on the paths traced (its output bound was verified — see Checked-and-clean), but its size is out of proportion to the other path normalisers in the same file (`normalizeMcPath` does its job in 14 lines), and every new APA spelling (the comments show several already) has historically been added as another special-case block.
- **Fix (words):** Split the path once into components (device / partition-label / rest) with a single separator-skipping helper, then express the alias rules as data checks on those components; do not add new spelling branches without removing an equivalent special case. No behaviour change intended.
- **Benefit:** The highest-risk string code in the settings path becomes auditable in one pass; future APA spellings stop accreting blocks.
- **Effort:** L · **Risk:** Medium (this parser guards real partition-ownership checks — refactor only behind the existing reject behaviour, characterisation first) · **Confidence:** High on structure (READ); no claim any current spelling is mis-parsed.

### RIPTOPL-138 — Both cdvdman ATAD variants: `sceAtaInit` returns a device-info struct that nothing ever populates
- **Category:** 1 (dead code) / 8 (half-finished feature)
- **Locations:** `modules/iopcore/cdvdman/atad.c` — struct declared `:74`, returned `:732-735`, read by the shutdown callback `:761`; `modules/iopcore/cdvdman/hdpro_atad.c` — same pattern at `:84` / `:736-739` (no shutdown callback at all). Tree-wide grep (READ): no write to the struct or any field of it exists in either file or anywhere under `modules/iopcore/cdvdman/`.
- **Problem:** `exists` is permanently 0, `total_sectors`/`has_packet`/`security_status` permanently 0. Consequences: (1) `atad.c`'s registered DEV9 power-off handler (`if (atad_devinfo.exists) ata_device_standby_immediate(0);`) can never fire — the STANDBY IMMEDIATE on shutdown that the file header says exists to avoid an "emergency park" is dead; the drive is never parked by this driver. (2) `sceAtaInit` is exported from cdvdman, so any consumer reading the struct gets plausible-looking zeros rather than an error.
- **Fix (words):** Either populate the struct where the disk is actually identified (the cdvdman HDD init reads the APA header via DMA — set `exists` there, or in `atad_start` after the ready wait succeeds), or delete the pretence: remove the struct and the shutdown check and state in the header that this variant does not park the drive. The first is one assignment; the second is a deletion. Keeping both halves is the current, worst state.
- **Benefit:** The shutdown-park either works or is honestly absent; no future reader trusts a zeroed `total_sectors`.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-139 — A permanent 4 KB static buffer to streaming-search an 8-byte magic
- **Category:** 4 (size/memory)
- **Locations:** `src/sources/vcdsupport.cpp:3187` (`static unsigned char buf[4096]` inside `vcdRaModuleStatus`).
- **Problem:** The function scans `POPS/MODULE_9.IRX` for the 8-byte `RIPTRA01` magic using a rolling `matched` counter that carries partial matches across chunk boundaries — because the matcher is explicitly chunk-boundary-safe, the chunk size is irrelevant to correctness, yet the buffer is a **permanent static** 4,096 B of BSS for a function on a launch-confirm path. A 256 B buffer performs identically (at most 16× more `read` calls of a file that is itself a small IRX module), and as a static it need not grow the stack either. (Deliberate, bounded, documented statics elsewhere in these files were checked and are not filed — see Checked-and-clean.)
- **Fix (words):** Shrink the static buffer from 4096 to 256 bytes. One number.
- **Benefit:** 3,840 B of permanent BSS returned, for zero behavioural change.
- **Effort:** S · **Risk:** Low (boundary safety is by construction of the `matched` state machine, verified in the read) · **Confidence:** High (READ).

### RIPTOPL-140 — `bdmDeviceIsATA` stack-allocates a whole per-device struct to hold one 32-byte driver string, and double-checks the match
- **Category:** 4 (size/memory) / 3 (overengineering, minor)
- **Locations:** `src/sources/bdmsupport.cpp:2244-2263`.
- **Problem:** The function declares the full `bdm_device_data_t` (roots, prefixes, game-array pointers, counts and flags) solely to fill its driver-name field via `bdmReadDriverName`. It then returns `(!strcmp(driver, "ata") && strlen(driver) == 3)`, where the `strlen == 3` clause is fully implied by the `strcmp` against a 3-char literal. Every other identity reader in the file uses a plain `char driver[32]`.
- **Fix (words):** Use `char driver[32]` like the rest of the file and return the `strcmp` result alone. Net deletion.
- **Benefit:** Smaller stack frame on a function called in the HDD-presence scan loop; one less struct-layout dependency.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-141 — `VCD_ID_PROBE_BUDGET` is dead: the scan-time probe budget it names no longer exists
- **Category:** 1 (dead code) / 8 (half-finished migration)
- **Locations:** `src/sources/vcdsupport.cpp:262`; stale framing comment at `:255-261`.
- **Problem:** `constexpr int VCD_ID_PROBE_BUDGET = 8; // discs opened per scan` has **exactly one hit** in a repo-wide grep over `src/` + `include/`: its definition. The design comment above it still describes the disc read as "fenced three ways", fence 3 being "a per-scan budget caps how many discs one build may open" — but the shipped design is lazy and one-slot: scans do no disc IO at all, and resolution happens one settled row at a time through the io worker. The constant and the fence-3 sentence are residue of the superseded scan-time resolver, and they misdescribe the current system to the next reader (the budget reads as a live safety limit; nothing enforces it because nothing needs it).
- **Fix (words):** Delete the constant and the fence-3 clause from the comment, renumbering the remaining two fences. Nothing else references either.
- **Benefit:** One less phantom safety mechanism; the design comment again describes the code that exists.
- **Effort:** S · **Risk:** Low (no references) · **Confidence:** High (READ — grep-verified zero uses).

### RIPTOPL-142 — Self-described unreachable clamp in the VCD scan loop
- **Category:** 1 (dead code)
- **Locations:** `src/sources/vcdsupport.cpp:692-693`.
- **Problem:** Inside the scan loop, after `if (baseLen > ISO_GAME_NAME_MAX) { …; continue; }` (160) has already rejected every long name, the code runs `if (baseLen > VCD_NAME_MAX - 1) baseLen = VCD_NAME_MAX - 1;` — with its own comment: "unreachable after the cap above; kept as a belt". The comment is correct: the second threshold is 255, and no value that survived the 160 cap can exceed it. Dead weight that invites a future editor to wonder which cap is real.
- **Fix (words):** Delete the two lines. The 160 cap above it is the single, documented, launchability-driven limit.
- **Benefit:** One cap, one place, no false second line of defence.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-143 — `sbReadList` carries a commented-out size calculator and a dead re-normalisation
- **Category:** 1 (dead code)
- **Locations:** `src/sources/supportbase.cpp:650-668` and `:602` (vs `:590`).
- **Problem:** (a) Inside the ul.cfg read loop, ~19 lines are two nested comment blocks: a TODO note and a fully commented-out per-part size loop. It is the only commented-out code body of its size in either file audited in section 1a, it sits inside a hot loop where it taxes every reader, and its TODO has no owner or trigger. (b) A few lines earlier the flow normalises `if (count < 0) count = 0;` at `:590`, then inside the `fd >= 0` branch repeats the identical reset at `:602` — unreachable: between the two, `count` is only increased when the increment is non-negative. Dead defence that suggests a failure mode the code above it already excludes. (The surrounding ul.cfg arithmetic was verified clean this round — see Checked-and-clean.)
- **Fix (words):** Delete both comment blocks (if the size idea is ever revived it belongs in an issue, not the loop) and delete the second `count < 0` reset. No live line changes.
- **Benefit:** The UL read loop reads as what it does; one normalisation point for the count invariant. ~21 lines deleted.
- **Effort:** S · **Risk:** Low · **Confidence:** High (READ).

### RIPTOPL-144 — `thmLoadResource`'s `psm` parameter is dead: accepted at ~10 call sites, never used
- **Category:** 1 (dead code)
- **Locations:** `src/gfx/themes.cpp:1061` (signature); body at `:1063-1074`.
- **Problem:** Every caller passes `GS_PSM_CT32`, which reads as "this load honours a pixel format" and honours nothing — the body calls only two helpers, neither of which takes a pixel format. A future caller passing a different psm for a palette/CLUT texture would silently get CT32 behaviour. This is the same false-affordance shape as a dead config key: the parameter costs a register/stack slot per call and, worse, a reader's trust.
- **Fix (words):** Delete the parameter and its argument at all call sites. Nothing is added.
- **Benefit:** ~10 call sites lose a misleading argument; the function's real contract (theme file, else built-in) is what the signature says.
- **Effort:** S · **Risk:** Low (mechanical; no behaviour change — the value is provably unused) · **Confidence:** High (READ).

---

## Divergence tallies (sibling-copy comparisons, as tasked)

Full tables live in the section files (`round4-sections/riptopl-r4-2a-ds34.md`, `riptopl-r4-2c-atad-ps2ips-apa-smap.md`); tallies and the load-bearing rows:

- **DS34 Bluetooth pair** (`modules/ds34bt/iop/ds34bt.c` vs `modules/pademu/ds34bt.c`; real + stripped diffs run): intentional variant 11 (pad count 2→4, IRX/RPC shell vs in-process API, standalone-only services, report-translation delegation, analog-toggle feature, …) · drift 6 (incl. two verified-equivalent encodings) · suspected bug 9 → findings RIPTOPL-066, -092, -105, -107, -108, -111, -113, -114 and the BT half of -106.
- **DS34 USB pair** (`modules/ds34usb/iop/ds34usb.c` vs `modules/pademu/ds34usb.c`): intentional variant 10 (guitar support pademu-only, DS5 standalone-only, per-pad data buffers, the pademu-side enable-buffer fix) · drift 4 · suspected bug 8 → findings RIPTOPL-083, -092, -106, -107, -108, -109, -110, -112, -114.
- **SMAP ×3:** intentional variant 8 · drift 4 · suspected bug 2 (RIPTOPL-093, -119 — each present in both neutrino copies) · plus the standing duplication verdict RIPTOPL-127. Raw diff lines: ingame↔udpbd 530, ingame↔udpfs 536, udpbd↔udpfs **25**.

## Checked and clean (candidates that dissolved on full read — condensed; full reasoning in the section files)

**Parent-traced this round:**
- **`sbRename`'s fixed 32-byte `memcpy` from `newname` (`src/sources/supportbase.cpp:1286`) — DISSOLVED.** A section flagged it INFERRED (the callee reads a fixed `UL_GAME_NAME_MAX` = 32 bytes from a `char *` whose contract states no minimum size). The parent traced the whole chain: the copy executes only when the game is `GAME_FORMAT_USBLD` (guard immediately above it); the sole producer of rename calls (`src/ui/menusys.cpp`, the rename UI) allocates `char newName[nameLength]` with `nameLength` from the source's own `nameLength()`; and all four `sbRename` callers' `nameLength()` implementations (bdmsupport, ethsupport, mmcesupport, udpfssupport — each read) return exactly `UL_GAME_NAME_MAX + 1` = 33 when the game is USBLD format, and a larger value otherwise. On every path that can reach the copy, the caller's buffer is therefore ≥ 33 bytes. The contract is fragile-by-inspection but provably unbreakable in-tree; not filed. (`mmcesupport.cpp` was read only at the targeted ranges for this trace — it remains on the still-unread list below.)

**Task 3 (ee_core remainders):** `set_ipconfig` string handling is exact-fit by design (3×16 + 9 + 6 = 63 ≤ 64); the modmgr/util DMA-stat empty spins are the standard upstream loader idiom with no local yield-fix precedent (unlike RIPTOPL-045) — not filed; `ResetIopSpecial` is hardened (arg bounds, NULL-checked IOPRP alloc, bounded magic scan); RA args are length-capped; `SetupCheats`/`LinkImage` carry the fork's bounds hardening; the modmgr required-transport fail-loud contract is deliberate; syshook/main/gsm_api/tlb/igrovl_stub/cd_igr_rpc clean.

**Section 1a (vcdsupport/supportbase):** the strict IPCONFIG parser and mask-contiguity proof (the model parser RIPTOPL-126 consolidates onto); `vcdReadNetFile` rejects short reads rather than parsing partial configs; `vcdSafeCopyFile`/`vcdSafeWriteFile` (MC free-space gate, partial-write loops, unlink-on-failure); `vcdWritePopsPathmap`'s snprintf accounting (truncation becomes the documented −4, never a wrap); `scanForISO`'s two `strcpy` sites (both preceded by explicit total-length guards — copies clean; the RIPTOPL-019 probe inefficiency stands separately); `isValidIsoName`, `GetStartupExecName`, `sbReadList`'s count/record arithmetic, `sbPrepare`'s patch-zone scan; the Neutrino arg assembler's truncation contract (sole UI caller tests the return); `vcdSep` vs `vcdDirSep` (look duplicated, are not); the cross-file custom→device→MC resolution policy (already shares its hard part — a shared engine would be MORE); the render-path memo walk (deliberately bounded design; the scan-side multiplication is RIPTOPL-123); `sbRaHunt`'s 8 KB static and the memo statics (sized by explicit constants with in-code rationale — deliberate, unlike RIPTOPL-139); `ProbeZISO` sector fallback fencing.

**Section 1b (settingsio/dialogs/bdmsupport):** `normalizeMcPath` memmove arithmetic; `createMcSettingsFolder`'s level loop; `writeConfigPathRedirect` returning `mirrorOk` for deferred boots (deliberate and correct — the MC mirror is the only copy the next boot can read); `readConfigPathRedirect` bounds; `prepareCustomApaSettingsPath`'s output bound (source globally capped at 64; over-long compositions rejected, not truncated — its complexity is RIPTOPL-137, its bound is clean); `configWriteChecked`'s modified-flag re-audit; the BDM save retry's same-identity re-resolution; the recovery path's bounded 1500 ms USB probe (documented, read-only, accepted as designed); the "legacy callers" sub-dialog tables (all referenced from guisettings — not dead); handler-mediated long-path rows (excluded from RIPTOPL-078 precisely because they have the full-size buffers); SMB share/user/password dialog rows (destinations are 32/17-byte globals — no truncation); `diaDateTimeConfig` ranges; `bdmFindPartition`'s unbounded `sprintf` (dissolves on its only call sites: 64-byte caller buffer + 32-byte prefix cap = 40 max); mode-as-slot uses (`BDM_MODE = 0`, so modes *are* slot indices); the compile-gated diagnostic apparatus (no release cost, deliberate hardware-triage tool); `bdmReadDriverName`'s retracted-probe reasoning; `ensureNetworkSourceModules`' semaphore discipline (every early return traced — no leak; its chain duplication is RIPTOPL-129); `bdmProbeMassSlots` summary bounds; the cleanUp/shutdown free-leg overlap (below finding threshold).

**Section 1c (themes/fav/config/eth/udpfs):** `free(cfgBuf)` after `tarThemeRead` (callee NULLs the pointer on every path — `free(NULL)` is safe); readEntry filePath length arithmetic (exact for every reachable caller — all pass 1-byte separators; the `sprintf` itself is the round-2 CLEARED item, not re-opened); `promoteItemTextElements` NULL-`prev` trace; `validateItemsList` insertion (a first element is guaranteed by the preceding pass); the wOPL binary reader read in full for new defects inside the RIPTOPL-020 layer — bounds-checked throughout, no new defect beyond RIPTOPL-095/-104; the native favourites parser version gate and count cap; `gatherAllGames` two-pass gather (fill pass bounds-checked per row — growth truncates, never overflows); `favTagDuplicates` display-buffer arithmetic (exact); `smbConnect` credential `strncpy` (destinations 256 B vs sources 32 B — zero-padded); `ethTryNeutrino` credential copies (length-guarded immediately above); `ethReadOnlyCfgDir` empty-home early return; `cfgWriteLibconfigLine` buffer sizing (worst case re-added: 567 ≤ 576); `ConfigSet::find` key comparison (stored keys always NUL-terminated in-field); UDPFS discovery cadence (latched flag tested before any server probe — no per-poll opens); the art-case index bound; udpfssupport's launch legs (their duplication is RIPTOPL-018, already filed; handle cleanup traced clean on every failure return).

**Section 2a (ds34):** all other report senders' length discipline (BT DS3 both copies; USB both copies; `hid_initDS34`); HCI/L2CAP command lengths (every builder fully writes its buffer); the shared tables' values identical in all four files and safely indexed except RIPTOPL-092; dpad handling equivalent across copies; the HCI disconnect path's pad-index guard; the USB connect endpoint-walk guard (present in both USB copies); the BT first/re-arm transfer-size asymmetry (identical in both copies, fields fit — recorded because it was checked); DS4 touchpad gating equivalent; standalone USB `get_bdaddr` read lengths; the pademu USB guitar fall-through (inert — no DS3/DS4 branch can match); pademu BT's DS3 LED rewrite (internally consistent; group offsets verified against the standalone template encoding); negative-port reachability in the standalone RPC paths (dispatcher passes a `u8`, so the upper-bound test suffices there — this does not extend to RIPTOPL-107).

**Section 2b (smb_fio/mcemu/usb_mass/mx4sio):** negative `size` into smb read/write (dissolved in `smb.c`: the loops no-op on a negative count — the negative *position* route is real and is RIPTOPL-085); `FileTimeToDate` arithmetic traced correct end-to-end (the apparent leap-day underflow is the correct remainder correction); the GetShareList/OpenShare `sprintf` sites (both fit exactly for terminated inputs: 23 ≤ 64, and 274 = 274); the share-enumeration loop discipline (the unchecked `maxent` itself is RIPTOPL-088); the keepalive thread (all under the I/O semaphore, blocks on semaphores — no spin); `smb_closeAll` lock context; mcemu's BDM erase chunk clamping (the unclamped *start page* is RIPTOPL-072); `SioResponse` (the one response helper that honours its length); mcemu port indexing (card index masked in both hook and emulator); the inactive-card memset (sized by the packet's own declared length); the MceRead/MceWrite page-cap memcpys (sound given a valid PageSize — the trust itself is RIPTOPL-069); usb CBW command copy (no local guard, but every in-tree caller passes 12- or 16-byte commands into the 16-byte field — noted so a future longer-CDB caller re-opens it); CSW validation (short CSW invalidates, zero-length retries once, signature **and** tag must match); the shared `ucmd`/`tag` statics (semaphore held across the whole non-ASYNC command); the ASYNC `#else` implementation (a compile-time alternative behind a commented define — not dead code by neglect); `usb_mass_update`'s settle delay (deliberate, matches cited practice); mx4sio's CRC-indexed ISR lines (preprocessed out — `CONFIG_USE_CRC16` is not defined; would need re-checking if ever enabled); the `wait_*` outer loops (all bounded — only the inner poll is RIPTOPL-118); `reverse_buffer`'s int/uint mismatch (unreachable at in-tree call magnitudes); the SD detect thread (a sleep loop, not a spin); module_start's 4-byte read into a 4-byte int.

**Section 2c (atad/ps2ips/apa/password/udpfs_ioman/smap):** the 48-bit sector-count encoding (65536 → register 0 is the correct LBA48 encoding; 256 → 0 likewise on the 28-bit path); the PIO loop sentinel arithmetic (`--0` yields exactly the `(u32)-1` comparand — ugly, correct); `ata_dma_complete`'s `dma_stat` (assigned inside the polling condition before any read); flush-without-semaphore (the cdvdman caller serialises externally under the device lock); duplicate `ata_cmd_table` entries (inert — first match wins, both entries agree; below finding threshold, recorded so they are not re-hunted); hdpro's `gen_ata_wait_busy` session handling (internally consistent across callers); hdpro's PIO type-8 branch (unreachable identically in both variants and in upstream-shaped code; the live problem is RIPTOPL-068); ps2ips `_rpc_buffer` vs `rpcBuffer` naming (the same memory in the only registration — fragile style, not a bug); shared `rests`/`lwip_buffer` globals (handlers dispatch serially on one thread); `do_send`/`do_sendto` alignment arithmetic (the alignment remainder is produced by PS2SDK's own EE library, not wire data — INFERRED-safe-by-contract); negative recv/send lengths (rejected by lwIP before any buffer write); apa read-after-`apaCacheFree` chain walks (dissolve for the reason round 3 established — the free is a non-destructive return-to-pool and the reads complete before the next allocation; **exception:** the `apaInsertPartition` instance feeds a function that allocates and is carried inside RIPTOPL-128); `apaCheckSum`'s GPT double loop (arithmetically correct, option not defined in this build); the MBR checksum fallback (documented GPT-compat shape; any other LBA still requires the full sum); `apaGetPartErrorName`'s "clear" tail (now a log-only stub by deliberate, documented design); `apaGetPartErrorSector`'s union aliasing (by on-disk-format design; allocation **is** NULL-checked there); password.c's DES (all table indices provably in range; round arrays respected); password.c's DES is **not** a third copy of RIPTOPL-056's DES (different construction and purpose — APA partition-password storage vs NTLM/LM authentication; not interchangeable, not a merge candidate); `apaEncryptPassword`'s unaligned reads (4-byte-aligned fields in every in-tree caller — contract-shaped); udpfs_ioman lock coverage (the macro's deadlock reasoning checks out; the unwrapped ops all return `-EIO` without touching shared state); `udpfs_close` skipping the connected check (correct asymmetry — a handle must be closable after the session drops; the stale-handle problem is on the *use* side, RIPTOPL-081); `udpfs_ioctl2` cmd 0x80 (returns −1 for a free slot; exposes nothing beyond the caller's own handle); the watchdog/deinit interlock (semaphore held while terminating; deleted only after the thread is gone); SMAP's unbounded TX busy-waits in all three copies (documented blocking-transmit contract in the ORIGIN files, matching the Sony-original model — dissolved as a finding; distinct from RIPTOPL-086, where the retry also holds a semaphore unrelated disk I/O needs); ingame `SMapLowLevelOutput`'s pbuf-chain copy (bounded by the MTU the same file sets + 14-byte header = 1514 ≤ 1518; INFERRED from the same files' configuration); the FIFO PIO full-word reads past a non-multiple-of-4 tail (extra bytes go only to the FIFO register, never to a memory buffer); udpbd `smap.c` lacking the DMA-block-size global (its `xfer.c` has no consumer for the value — consistent, not drift).

## Coverage ledger (exact)

All files below read 1..EOF at pin `2e347d5ac885` unless noted. **Round total: 40,307 lines read end-to-end.**

**Task 1 — EE `src/` (22,632 lines, completes the round-4 tier of `src/`):**

| File | Lines |
|---|---|
| `src/sources/vcdsupport.cpp` | 3,259 |
| `src/sources/supportbase.cpp` | 3,181 |
| `src/app/settingsio.cpp` | 2,611 |
| `src/ui/dialogs.cpp` | 2,409 |
| `src/sources/bdmsupport.cpp` | 2,408 |
| `src/gfx/themes.cpp` | 2,247 |
| `src/sources/favsupport.cpp` | 1,833 |
| `src/core/config.cpp` | 1,740 |
| `src/sources/ethsupport.cpp` | 1,664 |
| `src/sources/udpfssupport.cpp` | 1,280 |

**Task 2 — `modules/` remainders (15,499 lines):**

| File(s) | Lines |
|---|---|
| `modules/ds34bt/iop/ds34bt.c` + `modules/pademu/ds34bt.c` (full + stripped diffs run) | 1,719 + 1,425 |
| `modules/ds34usb/iop/ds34usb.c` + `modules/pademu/ds34usb.c` (full + stripped diffs run) | 998 + 616 |
| `modules/network/smbman-ra/smb_fio.c` (RIPTOPL-040 sites read, not re-filed) | 1,130 |
| `modules/mcemu/mcemu.c` (RIPTOPL-051 sites read, not re-filed) | 931 |
| `modules/usb/usbmass_bd/src/usb_mass.c` | 901 |
| `modules/mx4sio_bd/src/mx4sio.c` (RIPTOPL-049 covers sibling files only) | 847 |
| `modules/iopcore/cdvdman/atad.c` + `hdpro_atad.c` | 796 + 756 |
| `modules/network/ps2ips/ps2ips.c` (incl. the 13-claim verification table) | 752 |
| `modules/hdd/apa/src/apa.c` + `password.c` | 525 + 498 |
| `modules/network/udpfs_ioman/src/udpfs_ioman.c` | 510 |
| SMAP ×3: `smap.c` (651/641/639) + `main.c` (218/85/26) + `xfer.c` (339/217/279); raw + comment-stripped diffs of all three `smap.c` and `xfer.c` | 3,095 |

**Task 3 — `ee_core/src` remainders (2,176 lines, read personally by the parent agent):** `iopmgr.c` 552, `modmgr.c` 285, `syshook.c` 284, `gsm_api.c` 245, `util.c` 198, `main.c` 185, `cheat_api.c` 185, `tlb.c` 148, `igrovl_stub.c` 68, `cd_igr_rpc.c` 26. **`ee_core/src` is now fully read across rounds 2–4.**

Supporting files were consulted only to verify mechanisms (headers for constants, caller ranges, `cache.c`'s free, the ps2ips host tests read as documentation — not run) and are cited inside the findings they support; every cross-file dependence is marked INFERRED where it matters.

## Still unread after Round 4 (stated plainly)

- **EE `src/` below the round-4 tier** — never read end-to-end in any round: `src/ui/guigame.cpp` (2,617; grep/targeted contact only — the `configClone` caller check at `:2197`), `src/gfx/themeelems.cpp` (2,347), `src/core/opl.cpp` (2,132), `src/sources/mmcesupport.cpp` (1,855; targeted ranges only — the `sbRename` call-site trace this round), and every `src/` file below those.
- **Vendored stacks** remain out of scope as in every round: SMSTCPIP/lwIP, FatFs, mbedTLS, usmb2 and unfs4 bodies (their RiptOPL-side glue was read where assigned).
- **`modules/` files not enumerated** in Task 2 remain at their prior-round status.
- **No PS2 build or hardware verification exists** for anything in this report; Category 5 stays structural.

*End of Round 4 RiptOPL report. Read-only throughout; clone clean at the pin on completion.*
