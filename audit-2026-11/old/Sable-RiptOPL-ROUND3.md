# Sable Audit — RiptOPL — ROUND 3

- **Repo:** NathanNeurotic/RiptOPL, branch rebuild/main
- **Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` (verified at start and end of this round; clone `git status` clean throughout — read-only, no edits)
- **Scope of this round:** the still-unread list left by round 2 — (1) ee_core end-to-end, (2) the IOP files round 2 read only at candidate ranges, now in full, (3) the largest unread EE `src/` files in size order, as far as reached, (4) `modules/` remainders (network + storage + cdvdman-adjacent prioritised). Vendored lwIP / FatFs / mbedTLS / usmb2 / unfs4 bodies remain out of scope, as in rounds 1–2.
- **Rules applied:** LESS NOT MORE; exact `path:line` for every claim (line anchors grep-confirmed after the reads, not carried from memory); READ vs INFERRED labelled; nothing from RIPTOPL-001…051 or the round-2 cleared list is repeated — a re-verification note appears only where this round's full read touched an open finding's lines, and says so.
- **No PS2 build exists in this VM and nothing here is hardware-verified.** No build or hardware claims are made.

## Revisions to earlier rounds

None forced. One re-verification note, not a new finding:

- **RIPTOPL-042 (open, round 2) still present at this pin.** The unchecked `fileSlot->parts[*(u32 *)argp]` index is now at `modules/hdd/apa/src/hdd_fio.c:995` (HIOCGETSIZE) and `modules/hdd/apa/src/hdd_fio.c:1015` (HIOCGETPARTSTART) — full read of hdd_fio.c this round confirms the code is unchanged in substance; only line numbers drifted slightly from round 2's citation.

## New findings (RIPTOPL-052 … RIPTOPL-065)

---

### RIPTOPL-052 — ~370 lines of deliberately unreachable partition-editing machinery in the shipped APA driver
- **Category:** 1 (dead code)
- **Locations:** `modules/hdd/apa/src/hdd_fio.c` — hddFormat body `:348-456` (function `:321`, unconditional `return -EACCES` at `:347`); apaRemove `:519-564` (sole caller is hddRemove's dead tail at `:632`); apaRename `:565-614` (sole caller is hddReName's dead tail at `:854`); hddRemove dead tail `:630-637` (function `:615`, refusal at `:629`); hddReName dead tail `:852-859` (function `:845`, refusal at `:851`); ioctl2AddSub `:860-920`; ioctl2DeleteLastSub `:921-948`; devctlSwapTemp `:1028-1067`; devctlSetOsdMBR `:1068-1089` (the last four declared `__attribute__((unused))` at `:98-101`). The file's own comment at `:96` states the editors are "kept for diffability against the SDK but are no longer reachable".
- **Problem:** Roughly 30% of a 1,247-line security-sensitive IOP driver cannot execute. The driver's entire local patch theme is "the loader never edits the partition table" — format, remove and rename all refuse with -EACCES — yet the full format/partition-editing implementation is carried in the source beneath those refusals, where every future reader (and every audit) must re-verify that it really is unreachable. The compiler/linker almost certainly drops the after-return bodies and unreferenced statics from the IRX, so the cost is not binary size; it is review surface in exactly the file where a reachability mistake would be destructive.
- **Fix:** Delete the nine dead bodies and the two dead call tails; keep the refusing stubs (hddFormat/hddRemove/hddReName returning -EACCES) and the `:96` comment rewritten to say the editors were removed, with the SDK named as the place they live. If diffability against ps2sdk is ever needed again, it is one `git show` of the upstream file away.
- **Benefit:** ~370 fewer lines to audit in the one driver that can destroy a disk's partition table; the "never edits partitions" property becomes checkable by reading the ops table alone.
- **Effort:** S · **Risk:** Low (unreachable code; the refusal stubs stay) · **Confidence:** READ

### RIPTOPL-053 — smb2man.c and nfsman.c each carry their own copy of the same IOP front-end (~200 lines × 2)
- **Category:** 2 (duplication)
- **Locations:** `modules/network/smb2man/smb2man.c` and `modules/network/nfsman/nfsman.c` — AllocSysMemory malloc/free/calloc wrapper trio (smb2man.c `:319` region; nfsman.c `:226` region, bodies identical); bounded socket connect/accept trio with the same retry shape (`SMB2MAN_SOCK_RETRIES 8` at smb2man.c:118, loops `:124`/`:138`; `NFSMAN_SOCK_RETRIES 8` at nfsman.c:96, loops `:102`/`:116`); path normaliser (smb2man_fixpath smb2man.c:403; nfsman_fixpath nfsman.c:292); civil-date time packer (smb2man_pack_time smb2man.c:1073, Howard Hinnant civil_from_days noted at `:1086`; nfsman_pack_time nfsman.c:865 — bodies differ only in the epoch-offset first lines); plus the same slot/generation handle scheme and errno-mapper shape in both files. nfsman.c's own header states it copies smb2man's pattern.
- **Problem:** Two network filesystem drivers that must behave identically at the iomanX boundary maintain parallel copies of every shared mechanism. A fix to the socket retry bound, the allocator wrappers or the date conversion lands in one and silently not the other — the same failure mode round 2 found in the two SMB1 stacks (RIPTOPL-046), one layer up.
- **Fix:** Extract the identical pieces into one shared include consumed by both drivers — the repository's own established pattern (httpstream.inc, the unfs4 include, udpfs_shared). Keep the protocol bodies (SMB2 wire code, NFS RPC code) separate; share only the front-end: allocator trio, bounded-socket trio, fixpath, pack_time with the epoch offset as a parameter.
- **Justification (fix adds a file):** The shared include replaces two copies with one; net lines drop by roughly 200 and each mechanism gains a single owner. This is consolidation, not a new abstraction — it copies a pattern the tree already uses three times.
- **Benefit:** ~200 lines removed; one place to fix retry/allocator/date bugs for both network filesystems.
- **Effort:** M · **Risk:** Low–Medium (both drivers build from the same tree; behaviour of the shared pieces is identical today, so the merge is mechanical — but it touches two shipped IRXes and wants a bench pass) · **Confidence:** READ

### RIPTOPL-054 — Three game patches in patches.c re-implement the same SifLoadModule hook scaffold
- **Category:** 2 (duplication)
- **Locations:** `ee_core/src/patches.c` — SOS_SifLoadModuleHook `:509-580`, UltProPinball_SifLoadModuleHook `:625-699`, ShadowMan2_SifLoadModuleHook `:796-860`. Each repeats the same ~40-line sequence: resolve the game's SifLoadModule/SifAllocIopHeap/SifFreeIopHeap/SifLoadModuleBuffer addresses from a per-region table, call the original loader, then on a target-module match run GetOPLModInfo → SifAllocIopHeap → SifSetDma do/while → LoadModuleBuffer do/while → SifFreeIopHeap.
- **Problem:** The DMA/heap plumbing — the part most likely to hide a subtle bug (heap leak on a failed DMA, wrong retry bound) — exists three times. A fix to the transfer sequence must be made, and verified, three times in the most fragile code in ee_core (absolute-address game patches).
- **Fix:** One static helper taking the four resolved addresses and the module id, performing the alloc/DMA/load/free sequence; each hook keeps only its region→address table and its target-module test. The tables — the genuinely per-game part — stay per-game.
- **Benefit:** ~80 lines removed; the IOP-heap patch sequence gets one implementation to review.
- **Effort:** M · **Risk:** Medium (ee_core game patches are hardware-sensitive; each of the three games wants a real-hardware re-test — a build is not proof) · **Confidence:** READ

### RIPTOPL-055 — Per-pixel busy-wait inside the IGS colour-conversion loops
- **Category:** 5 (performance)
- **Locations:** `ee_core/src/igs_api.c` — FastDelay defined `:30-39` (count × 0x300 iterations × 4 NOPs); called once per pixel at `:340` (ConvertColors32), `:356` (ConvertColors24), `:370` (ConvertColors16), each time alongside the per-pixel BGCOLND fade write (the fade macros are real register writes in all builds — ee_core.h:90-91).
- **Problem:** The three converters run over the whole screenshot buffer inside the in-game screenshot path. At ~5,000 EE cycles per FastDelay(1) call (0x300 iterations of a 4-NOP body plus loop overhead — arithmetic, INFERRED), the delays alone cost on the order of 5 s for a 640×448 capture and tens of seconds at 1080p-class buffer sizes, while the game is frozen. The BGCOLND write already produces the fade effect the delay was pacing; the delay adds nothing except wall-clock time per pixel.
- **Fix:** Delete the three FastDelay(1) calls from the conversion loops (FastDelay itself stays — the slice loop in Screenshot uses it once per slice, where a pacing delay is proportionate). If a visible fade pace is wanted, delay once per row or per slice, not per pixel.
- **Benefit:** Screenshot capture stops stalling the frozen game for seconds on a cosmetic pacing delay; three lines removed, nothing added.
- **Effort:** S · **Risk:** Low (the fade becomes as fast as the conversion; no functional change to the saved image) · **Confidence:** READ for the code path; the cycle estimate is INFERRED

### RIPTOPL-056 — Two diverged copies of the same DES implementation in the two SMB1 drivers
- **Category:** 2 (duplication)
- **Locations:** `modules/network/smbinit/des.c` (564 lines) and `modules/network/smbman-ra/des.c` (568 lines). A comment-stripped diff shows 71 differing code lines; the load-bearing divergence is key storage: the smbman-ra copy keeps a static `DES_Keys[128]` buffer and const-correct signatures (`DES_createkeys(const unsigned char *key)`, `key7TOkey8(const …)`), the smbinit copy does neither.
- **Problem:** One of the two copies was patched (static key schedule, const) and the other was not, and nothing in the tree records which behaviour is intended. DES is the authentication core of SMB1 in both the boot-time (smbinit) and in-game (smbman-ra) stacks; a future crypto fix applied to one copy will not reach the other — the same shape as RIPTOPL-046's split SMB1 stacks, at the crypto layer.
- **Fix:** Decide which copy's key-storage behaviour is correct, then have both drivers build that one file (or the repo's shared-include pattern). Do not leave the decision implicit in a diff nobody runs.
- **Benefit:** One DES implementation to audit; the existing divergence is resolved deliberately instead of by whichever copy a future editor happens to open.
- **Effort:** S · **Risk:** Low–Medium (SMB1 logon wants one bench check per stack after unification) · **Confidence:** READ (diff computed this round)

### RIPTOPL-057 — mmce_fs devctl dereferences `arg` unchecked in four cases, beside a fifth case that validates it
- **Category:** 6 (bugs/security)
- **Locations:** `modules/mmce/mmceman/src/mmce_fs.c` — `args = *(u32*)arg;` at `:1103` (MMCE_CMD_SET_CARD), `:1116` (MMCE_CMD_SET_CHANNEL), `:1153` (MMCE_SETTINGS_ACK_WAIT_CYCLES), `:1158` (MMCE_SETTINGS_SET_ALARMS), none checking `arg == NULL` or `arglen`. The adjacent MMCE_CMD_SET_CARD_CHANNEL case (`:1136-1150` region) does check (`arg == NULL || arglen < 5` → error), proving the maintainers consider devctl argument validation in-scope for this function.
- **Problem:** A devctl issued with a NULL or short argument — a malformed EE-side call, a future caller that passes a smaller struct — dereferences it on the IOP and crashes the driver (and with it the game's storage path). The inconsistency is the finding: one case in this very switch was hardened and the other four were not.
- **Fix:** Apply the same guard the SET_CARD_CHANNEL case already has to the four `*(u32*)arg` cases (require arg != NULL and arglen >= 4, else return -1). Four small edits reusing the function's own existing pattern; nothing new introduced.
- **Benefit:** The MMCE driver's devctl surface becomes uniformly crash-safe against bad arguments.
- **Effort:** S · **Risk:** Low · **Confidence:** READ

### RIPTOPL-058 — MMCE filesystem ops truncate every filename length into a u8
- **Category:** 6 (bugs/security)
- **Locations:** `modules/mmce/mmceman/src/mmce_fs.c` — `u8 filename_len = strlen(name) + 1;` at `:84` (open), `:420` (remove), `:479` (mkdir), `:538` (rmdir), `:604` (dopen), `:849` (getstat, as `u8 len`), `:964` and the following line (rename, both names). The truncated value is used as the SIO2 transfer length for the name packet.
- **Problem:** For any name longer than 255 bytes the length wraps mod 256: the driver sends a truncated name with no NUL terminator and the card-side protocol desyncs (the card interprets the following bytes as the next packet). Long paths are reachable in principle through nested directories on the card's SD filesystem. The failure mode is a wedged bus transaction and a timed-out operation rather than a clean error. (Distinct from RIPTOPL-039, which covered the game-ID path in mmce_cmds.c; these are the filesystem ops in mmce_fs.c.)
- **Fix:** One length check at the top of each affected op (or in the two packet helpers they all funnel through): if strlen(name) + 1 > 255, return -ENAMETOOLONG before touching the bus.
- **Benefit:** Over-long names fail cleanly instead of desyncing the SIO2 protocol mid-transaction.
- **Effort:** S · **Risk:** Low · **Confidence:** READ

### RIPTOPL-059 — Five near-identical number-to-string converters in igs_api.c
- **Category:** 2 (duplication)
- **Locations:** `ee_core/src/igs_api.c:79-153` — u8todecstr `:79`, u16todecstr `:94`, u32todecstr `:109` (identical bodies modulo the input type), u32tohexstr `:124`, u64tohexstr `:139` (identical modulo base/width handling).
- **Problem:** ~75 lines implement one algorithm five times for the IGS debug text dump. Any fix to digit emission (leading-zero handling, buffer termination) has five homes.
- **Fix:** One u64-based decimal converter and one u64-based hex converter; the five call sites (all in the same file, `:406-480` region) widen their value to u64. Deletes ~45 lines within a single file — no cross-module sharing, no new dependency for the other ee_core flavours.
- **Benefit:** ~45 lines removed from ee_core; one converter to verify.
- **Effort:** S · **Risk:** Low (output is a debug text file; formatting is host-checkable) · **Confidence:** READ

### RIPTOPL-060 — ps2atad re-reads LBA 0 from the disk on every low-LBA BDM write
- **Category:** 5 (performance)
- **Locations:** `modules/hdd/atad/src/ps2atad.c` — ata_bd_write_hits_apa_reserved `:1609-1625`, called from the BDM write path at `:1622`; the probe is a full `ata_device_sector_io64(device, lba0, 0, 1, ATA_DIR_READ)` at `:1615`, executed on every write whose sector range starts below ATA_BD_APA_RESERVED_SECTORS (262144), with no memoisation of the APA-magic verdict.
- **Problem:** The fork's APA reserved-area guard is the right safety property (BDM writes must not touch a real APA table), but it pays one synchronous disk read per guarded write for a verdict that cannot change while the same disk stays attached: whether LBA 0 carries the APA magic is a property of the disk, not of the write. On a hybrid APA+exFAT disk — exactly the population this guard exists for — every filesystem write near the start of the exFAT volume pays it.
- **Fix:** Cache the verdict per device the first time it is probed; invalidate on device reset/media change (the driver already has attach/reset points). Stated trade-off: a cached verdict could go stale if the disk were reformatted mid-session through a non-BDM path; the invalidation points bound that window, and the same staleness argument applies to the driver's existing cached identity data.
- **Benefit:** Removes one full disk round-trip from every guarded BDM write on hybrid disks.
- **Effort:** S · **Risk:** Low–Medium (verdict staleness is the hazard; mitigate by invalidating wherever the driver re-identifies the device) · **Confidence:** READ (the staleness trade-off is INFERRED from the driver's structure)

### RIPTOPL-061 — SOS PAL patch branch is dead: the hook install is commented out, the hook's PAL half remains
- **Category:** 1 (dead code) / 8 (half-finished feature)
- **Locations:** `ee_core/src/patches.c` — SOSPatch `:581-596`: case 2 (PAL) at `:592-594` contains only the commented-out install line `// _sw(JAL((u32)&SOS_SifLoadModuleHook), 0x001d13f4);` at `:593`. SOS_SifLoadModuleHook `:509-580` still implements and maintains a full case-2 PAL address set (four absolute addresses) that no installed hook can ever reach.
- **Problem:** Either the PAL fix for SOS was deliberately abandoned (then the hook's PAL table and the empty case are dead weight that invites a future editor to re-enable one line without the context that made someone disable it), or it was disabled accidentally (then PAL SOS silently lost its IREMSND patch). The tree as it stands does not say which — the code carries both halves of the decision and neither's reason.
- **Fix:** Decide once, in the commit message: if PAL is abandoned, delete case 2 from both SOSPatch and the hook's address switch; if it is wanted, restore the install line and hardware-test PAL SOS. Do not leave the commented line as the only record.
- **Benefit:** Removes an ambiguous half-state from a game patch; whichever way the decision goes, the code afterwards says one thing.
- **Effort:** S · **Risk:** Low for deletion (PAL behaviour today is "no patch"; deleting the dead half changes nothing at runtime) · **Confidence:** READ

### RIPTOPL-062 — mmce_fs_write returns the card's byte count unclamped; mmce_fs_read clamps it
- **Category:** 6 (bugs/security)
- **Locations:** `modules/mmce/mmceman/src/mmce_fs.c` — mmce_fs_read `:201` clamps at `:272` (`if (bytes_read > size) bytes_read = size;`); mmce_fs_write `:280` assembles bytes_written from the card's reply and returns it with no equivalent clamp (grep confirms no `bytes_written > size` check exists).
- **Problem:** A card (or bus glitch) reporting more bytes written than were requested makes the EE side believe the file position advanced past the end of its own buffer — the exact failure the read side was already defended against. The asymmetry shows the clamp was considered necessary in one direction and missed in the other.
- **Fix:** Mirror the read clamp in write: if bytes_written > size, clamp to size (and treat it as the anomaly it is). One line, copied from the sibling function.
- **Benefit:** Closes the asymmetric trust in card-reported counts.
- **Effort:** S · **Risk:** Low · **Confidence:** READ

### RIPTOPL-063 — NIBBLE2CHAR produces 'k'–'p' for nibbles 10–15
- **Category:** 6 (bugs/security)
- **Locations:** `ee_core/src/patches.c:160` — `#define NIBBLE2CHAR(n) ((n) <= 9 ? '0' + (n) : 'a' + (n))`; used at `:565-566` to build the two-character module-ID string in SOS_SifLoadModuleHook.
- **Problem:** For n in 10–15 the macro yields 'a'+10='k' … 'a'+15='p' instead of 'a'–'f' (it never subtracts 10). The string names the patched IREMSND module instance; a module ID whose either nibble is ≥ 0xA (ID ≥ 0xA0, or low nibble ≥ 0xA) produces a wrong name. Latent today — module IDs in this context normally stay small — but the macro is simply wrong, and it sits in a hook whose whole job is matching a module by identity.
- **Fix:** `'a' + (n) - 10` in the second branch (or delete the macro for a 16-entry lookup string, which is shorter to verify). One line either way.
- **Benefit:** Removes a latent wrong-name bug from a module-identity path.
- **Effort:** S · **Risk:** Low · **Confidence:** READ (impact assessment — IDs rarely reaching 0xA0 in this path — is INFERRED)

### RIPTOPL-064 — genvmc's hand-rolled 32×32 multiply serves one caller, and half its result is discarded
- **Category:** 3 (overengineering) / 1 (dead code)
- **Locations:** `modules/vmc/genvmc/genvmc.c` — long_multiply `:142-162`; sole call at `:461` in vmc_mcformat; only `hi` is consumed (`:462`, `temp = (hi >> 6) - …`); `lo` is computed by the helper and never read anywhere.
- **Problem:** Twenty lines of manual partial-product arithmetic — including a low-half computation no caller uses — duplicate what the MIPS compiler emits natively for a u64 multiply. The hand-rolled version's overflow behaviour in its intermediate sums is exactly the kind of thing nobody wants to re-derive during a review of VMC format code.
- **Fix:** Replace the helper and call with a native `(u32)(((u64)clusters_per_card * 0x10624dd3ULL) >> 32)` for the hi word, deleting the function. The surrounding reciprocal chain (`temp` arithmetic) is untouched, but its equivalence should be host-verified over the real input range (card sizes 8–1024 MB) before the change ships — the arithmetic is host-testable in isolation.
- **Benefit:** ~20 lines removed from a format-critical file; one less hand-rolled primitive to trust.
- **Effort:** S · **Risk:** Low with the host check, Medium without it · **Confidence:** READ for the single-caller/dead-output facts; equivalence of the replacement is INFERRED until host-checked

### RIPTOPL-065 — ra_disc_us in raudp.c is a write-only counter
- **Category:** 1 (dead code)
- **Locations:** `modules/network/raudp/raudp.c` — declared `:771` ("Discovery elapsed time (retained for bounded diagnostic accounting)"), incremented at `:875` and `:924-925`; its only read is its own overflow guard at `:924`. Nothing else in the tree consumes it.
- **Problem:** Three lines and a comment maintain a diagnostic counter that no diagnostic reads. The comment claims a purpose ("retained for … accounting") that the code does not fulfil — the next reader will waste the same search this round did, proving nothing reads it.
- **Fix:** Delete the variable, both increments and the guard. If discovery timing is ever wanted, it can be re-added with a consumer.
- **Benefit:** 3–4 lines and one misleading comment removed.
- **Effort:** S · **Risk:** Low · **Confidence:** READ (tree-wide grep for the symbol)

---

## Checked this round and clean (candidates that dissolved on full read — not findings)

**Task 1 — ee_core:**
- `ee_core/src/ra.c` (383) — heavily defensive: compile-time RA_PROBE ladder, TLB-window guard on watch addresses. No finding.
- `ee_core/src/freeze_diag.c` (297), `freeze_diag_format.c` (444), `freeze_diag_screen.c` (405) — bounded formatting throughout (put_char caps at cap-1); the screen's per-pixel CPU rendering is a one-shot frozen-game diagnostic, by design; the unbounded IOP-reset loops are the standard IGR idiom shared with padhook.c.
- `ee_core/src/igrmenu.c` (1,134, now read in full, not only the #419 hunks) — button-poll spin runs while the game is suspended (by design); hunt/cheats modulo paths guarded by count>0 at entry.
- `ee_core/src/padhook.c` (1,096, now read in full) — same standard IOP-reset idiom; IGR stack sizing shows no evidence of overflow against igrmenu's actual buffer use. Not filed.
- `ee_core/src/ra_overlay.c` (420), `ra_pulse.c` (126), `igrmbox.c` (122) — torn-snapshot double-check before acting on DMA'd events in both RA paths; igrmbox's mbPost wait is bounded (3 s) with a documented late-answer contract; sprite rasteriser clips to the card rectangle.
- Cross-file ee_core helper duplication (igrmenu's igrmNum/igrmCat vs freeze_diag_format's put_dec/put_hex vs freeze_diag_screen's ln_* vs ra.c's helpers): judged structural, not duplication to merge — the files compile into mutually exclusive ee_core flavours (IGR_MENU / INGAMEDIAG / IGS / RA) and freeze_diag_format.c is deliberately dependency-free for host testing. The one concrete in-file instance is filed as RIPTOPL-059.
- patches.c: delayed_cdRead's NOP loop is the intended slow-reads patch; Skip_BIK_Videos' "AAAAAA" comment inaccuracy is cosmetic. Neither filed.

**Task 2 — IOP files read in full:**
- `modules/network/smb2man/smb2man.c` (1,780) — round 2's verdict holds at full read: dread clamps namelen to received bytes, fixpath refuses overflow with -ENAMETOOLONG, latin1 credential validation, slot generations guard stale handles, bounded waits everywhere. Clean apart from the shared front-end filed as RIPTOPL-053.
- `modules/network/nfsman/nfsman.c` (1,407) — URL parser checks before multiplying, rename validates same-export, connect devctl validates NUL termination (extends round 2's clearance to the whole file). Clean apart from RIPTOPL-053.
- `modules/hdd/atad/src/ps2atad.c` (1,646) — a ps2sdk upstream snapshot carrying careful, well-commented fork patches (range checks, 4Kn refusal, APA reserved-area write guard). Filed only the memoisation point (RIPTOPL-060); no upstream-style findings.
- `modules/hdd/apa/src/hdd_fio.c` (1,247) — apart from RIPTOPL-052 and the re-verified RIPTOPL-042: the driver's hardening is real (ioctl2Transfer overflow-safe bounds, nsub clamp in apaOpen). The apaOpen sequence `apaCacheFree(clink)` followed by `apaPassCmp(clink->header->…)` (~:565-577) looks like a use-after-free but dissolves: hddOpen holds fioSema across apaOpen, cache-free is a non-destructive return-to-pool, and apaPassCmp allocates nothing — an inherited upstream pattern that works under the semaphore. Recorded here so it is not re-flagged.
- `modules/mmce/mmceman/src/mmce_fs.c` (1,238) — apart from RIPTOPL-057/058/062: handle-slot discipline is correct on every exit (the close paths free the slot on timeout and error alike, with the reasoning in comments); dread's card-supplied filename length is a u8 (≤255) received into a 256-byte dirent field, so no overflow is reachable there; the ENOENT-vs--1 distinction between card-answered and bus-failure paths is deliberate and documented. mmce_fs_ioctl returning 0 unconditionally matches sibling drivers' stubs; not filed.
- `modules/network/raudp/raudp.c` (1,308) — hardened: source-IP check on PC messages, bounded parses (title capped at 63, receive buffers sizeof-1 with NUL written), torn-snapshot trailer check; ra_discover's lack of an overall timeout is documented design. Clean apart from RIPTOPL-065.
- `modules/vmc/genvmc/genvmc.c` (947) — apart from RIPTOPL-064: job-copy discipline (the create thread works from its own copy of the request), exclusive-create with owns-file tracking, failure-cleanup rules that distinguish pre-open from post-open failures, devctl argument validation, rebuild path that derives the source name from the job's own filename and opens the card read-only. The progress divisions (`i / (blocks / 99)`, `i / (clusters_per_card / 99)`) can divide by zero only for card/size inputs no real caller produces (EE-validated sizes ≥ 8 MB; R3000 DIV-by-zero does not trap), and the quotient feeds a progress percentage only. Not filed.

**Task 3 — EE src/:**
- `src/ui/guisettings.cpp` (3,497), `src/ui/menusys.cpp` (3,400), `src/sources/hddsupport.cpp` (3,383) — read end-to-end; no findings. All three are densely reviewed fork code (generation counters and snapshot/publish discipline in menusys, transactional last-good list publication in hddsupport, settings-shell untouched-page detection in guisettings).
- Two menusys candidates dissolved on verification and are recorded to prevent re-flagging: (a) `_menuSaveConfig` (menusys.cpp:759) re-borrows `itemConfig` for configWrite at `:787` after releasing menuSemaId — but configWrite is NULL-safe (src/core/config.cpp:1498), the competing config swaps are input-driven on a GUI thread that is inside a modal deferred-IO wait for the save's duration, and the browse worker shares the IO thread with the save itself, so no concrete interleaving was established; (b) menuEnd's semaphore-deletion race is acknowledged in the code's own comment (~:1460) as a known shutdown-ordering limitation, deliberately deferred by the maintainers.

**Task 4 — modules/:**
- `modules/iopcore/cdvdman/cdvdman.c` (1,233) — clean: the read-job thread design, EOM clamping against mediaLsnCount, the unaligned/sector-size path's copy bound (nsectors × 2048, not nbytes — a fixed over-read), and the VMC save drain before oplShutdown are all deliberate and documented. cdvdman_writeSCmd's register-polling spins are the inherited Sony SCMD handshake, bounded by hardware protocol; cdvdman_sendSCmd's retry is bounded (2500).
- `modules/iopcore/cdvdman/smb2.c` (1,153) — clean: ReadChunk validates the server's datalen against both the remaining message length and the requested count before receiving into the caller's buffer; the NTLM target-info walk is bounds-checked pair by pair; a _Static_assert guards the AUTHENTICATE message layout against the packet buffer; negotiate is deadline-bounded (10 s, 3 attempts) precisely so a silent server cannot wedge smbinit's synchronous load.

## Coverage ledger (this round)

**Task 1 — ee_core: COMPLETE for the assigned files.**
Read in full: patches.c (1,002), igs_api.c (790), ra.c (383), freeze_diag.c (297), freeze_diag_format.c (444), freeze_diag_screen.c (405), igrmenu.c (1,134), padhook.c (1,096), ra_overlay.c (420), igrmbox.c (122), ra_pulse.c (126) — 6,219 lines.
ee_core/src files NOT read (not in the assignment): iopmgr.c (552), modmgr.c (285), syshook.c (284), gsm_api.c (245), util.c (198), main.c (185), cheat_api.c (185), tlb.c (148), igrovl_stub.c (68), cd_igr_rpc.c (26) — 2,176 lines.

**Task 2 — IOP full reads: COMPLETE.**
smb2man.c (1,780), ps2atad.c (1,646), nfsman.c (1,407), raudp.c (1,308), hdd_fio.c (1,247), mmce_fs.c (1,238), genvmc.c (947) — 9,573 lines, each read end-to-end (hdd_fio.c beyond round 2's cited ranges, per the assignment).

**Task 3 — EE src/ in size order: 3 of 13 files, then stopped in order.**
Read in full, in the verified size order: src/ui/guisettings.cpp (3,497), src/ui/menusys.cpp (3,400), src/sources/hddsupport.cpp (3,383) — 10,280 lines.
Not reached (in order): src/sources/vcdsupport.cpp (3,259), src/sources/supportbase.cpp (3,181), src/app/settingsio.cpp (2,611), src/ui/dialogs.cpp (2,409), src/sources/bdmsupport.cpp (2,408), src/gfx/themes.cpp (2,247), src/sources/favsupport.cpp (1,833), src/core/config.cpp (1,740), src/sources/ethsupport.cpp (1,664), src/sources/udpfssupport.cpp (1,280) — 22,632 lines. Mitigating context from round 2, unchanged: all of src/*.cpp holds only 6 malloc/calloc and 29 raw sprintf sites, every one already covered by a pass-1 finding, round-2 Section A, or the cleared list. Below this tier also unread: guigame.cpp (2,617), themeelems.cpp (2,347), opl.cpp (2,132), mmcesupport.cpp (1,855).

**Task 4 — modules/ remainders: partial, prioritised as assigned.**
Read in full this round: modules/iopcore/cdvdman/cdvdman.c (1,233), modules/iopcore/cdvdman/smb2.c (1,153). Compared byte-for-byte (diff, then comment-stripped diff): modules/network/smbinit/des.c (564) vs modules/network/smbman-ra/des.c (568) → RIPTOPL-056.
Not read (largest first): modules/ds34bt/iop/ds34bt.c (1,719), modules/pademu/ds34bt.c (1,425), modules/network/smbman-ra/smb_fio.c (1,130 — round 2 read it partially), modules/ds34usb/iop/ds34usb.c (998), modules/mcemu/mcemu.c (931 — round 2 read MceRead/MceWrite only), modules/usb/usbmass_bd/src/usb_mass.c (901), modules/mx4sio_bd/src/mx4sio.c (847), modules/iopcore/cdvdman/atad.c (796), modules/iopcore/cdvdman/hdpro_atad.c (756), modules/network/ps2ips/ps2ips.c (752 — round 2 noted its bounds commentary was not independently verified; still not verified), modules/hdd/apa/src/apa.c (525), modules/hdd/apa/src/password.c (498), modules/network/udpfs_ioman/udpfs_ioman.c (510), and the three smap variants (~640 each). The sbp2_driver.c copies (ilink 828 / bdmassault 801) were already covered by RIPTOPL-047 and were not re-read. Vendored and out of scope as before: SMSTCPIP/lwIP, FatFs, mbedTLS, usmb2 (Sahlberg), unfs4, isofs/lz4.c.

## Still unread after round 3 (for the next round's assignment)

1. EE src/, in size order: vcdsupport.cpp (3,259) first, then supportbase.cpp, settingsio.cpp, dialogs.cpp, bdmsupport.cpp, themes.cpp, favsupport.cpp, config.cpp, ethsupport.cpp, udpfssupport.cpp (22,632 lines total).
2. modules/: ds34bt.c (1,719), pademu/ds34bt.c (1,425), smb_fio.c remainder, ds34usb.c (998), mcemu.c remainder (~700 lines beyond round 2's ranges), usb_mass.c (901), mx4sio.c (847), cdvdman/atad.c (796), cdvdman/hdpro_atad.c (756), ps2ips.c (752 — includes verifying its bounds commentary), apa.c (525), password.c (498), udpfs_ioman.c (510), smap ×3.
3. ee_core/src remainders: iopmgr.c (552), modmgr.c (285), syshook.c (284), gsm_api.c (245), util.c (198), main.c (185), cheat_api.c (185), tlb.c (148), igrovl_stub.c (68), cd_igr_rpc.c (26).

*End of round 3. Clone verified clean at `2e347d5ac885` on completion; nothing in the repository was modified.*
