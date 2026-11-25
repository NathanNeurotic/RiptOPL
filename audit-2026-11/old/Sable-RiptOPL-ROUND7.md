# Sable — RiptOPL ROUND 7 (SCOPE EXTENSION: assembly, headers-as-corpus, test corpus, docs-vs-code)

**Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` (rebuild/main) — live head re-verified equal at round start and end; no delta, no revalidations owed. Clone git-clean throughout; read-only held.
**Scope:** exactly the round-5 verdict's named remainders. Rounds 1–5 filed RIPTOPL-001…280 on C/C++/Python source; this round takes the four residue classes: (1) assembly, (2) headers as a corpus, (3) the `.github/scripts` test corpus, (4) docs prose vs code.
**Result: 15 new findings, RIPTOPL-281…295.** Section evidence: `round7-sections/riptopl-r7-asm.md`, `riptopl-r7-headers.md`, `riptopl-r7-scripts.md`, `riptopl-r7-docs.md`. Headline mechanisms (281, 282, 292) were spot-verified by the coordinator against the clone before numbering; all reproduced.

## Findings index

| ID | Source | Title (short) | Cat |
|---|---|---|---|
| RIPTOPL-281 | asm | GSHandler REGIMM decode masks 0x3F into a 32-entry table — odd `rs` jumps out of bounds | 6 |
| RIPTOPL-282 | asm | `mips_memcpy` return value overwritten by `mips_memset`'s first instruction in its delay slot — all 4 copies | 6 |
| RIPTOPL-283 | asm | `Hook_ExecPS2` clobbers callee-saved $s0–$s3 on paths that tail-call the return-capable original | 6 |
| RIPTOPL-284 | asm | `dve_set_reg` always returns 0 — `dve_prepare_bus` retry loop can never repeat | 6 |
| RIPTOPL-285 | asm | Dead exports: 2 dangling `.globl`s with no definition anywhere + 2 defined exports with zero references | 1 |
| RIPTOPL-286 | scripts S-3 | Shipped PSU verified only by its author's mirror-image checker; the independent PSUManager cross-checker is never invoked (and generated credits claim it is) | 7 |
| RIPTOPL-287 | scripts S-1 | Four release scripts dead at pin while their headers + Makefile still present them as the live release path | 8 |
| RIPTOPL-288 | scripts S-2 | `normalize_release_notes.py` is test-only; shipped notes come from a second, duplicated renderer | 8 |
| RIPTOPL-289 | scripts S-4 | `build_rolling_extras.sh` variant/debug loops can move a stale `opl.elf` into a release dir (latent; file dead per -287) | 7 |
| RIPTOPL-290 | scripts S-5 | `tools/run_tests.sh` silently SKIPs site/wiki suites when `bs4` absent; the invoking job installs nothing | 8 |
| RIPTOPL-291 | scripts S-6 | Compiler-absence skip-to-pass in three suites (conditional vacuous passes) | 8 |
| RIPTOPL-292 | docs | README says menu vibration is on by default; the fresh-start default is Off | 8 |
| RIPTOPL-293 | docs | ROLLING_RELEASE download contract names ZIPs unversioned; every published archive carries `-<version>` | 8 |
| RIPTOPL-294 | docs | README collapses the Neutrino picker's three distinct 1080i modes into a nonexistent single "1080i" | 8 |
| RIPTOPL-295 | docs | BUILD-CONFIGURATIONS says the publisher refuses ELFs not starting 0x80 in; it accepts the LLD 0x100 start | 8 |

**Lines read this round:** assembly 4,777 / 4,777 (13 files — the verdict's 3,854 plus SMSTCPIP `inet.S`, SMSUTILS.S and the iremsndpatch asm, which the verdict had classed with the vendored trees); headers 35,808 (439 files, census + full arithmetic pass); scripts corpus 3,170 lines in full (24 files — all generators/verifiers/watches + 13 test/verifier files) + 278 test files mechanism-scanned and workflow-mapped of 63,283 total; docs 16 files in full, 67 claim groups checked.

---

## TASK 1 — Assembly (findings RIPTOPL-281…285)

Census results that frame the findings: the three `smsutils.S` copies are **byte-identical** (same md5, empty pairwise diffs) — zero divergence; the fourth sibling `SMSUTILS.S` differs only by its IRX wrapper, export ordinals 4/5 matching `smsutils.h`. `gsm_engine_adv.S` is not a translation unit (included at `gsm_engine.S:1260`); `DTV_576P` live, `DTV_1080P` dead only together with its dispatch in non-variant builds, lines 238–424 are live GSHandler register handlers — no duplication with `gsm_engine.S`. Dead-symbol census covered every `.globl`/`.global` in scope (~54 exports): exactly the 4 dead items in RIPTOPL-285; all others live, including non-obvious mechanisms (EngineHandler reached via the copied jal word; MIPS_Regs/dveStat asm-internal only).

### RIPTOPL-281…285 (section candidates 1–5, in order)

### Title: GSHandler branch-delay-slot REGIMM decode masks 6 bits into a 32-entry table — odd `rs` reads past `BD_t2` and jumps to whatever word follows it
- **Category:** (1) Wrong constant / jump-table bounds
- **Locations:**
  - `ee_core/src/gsm_engine.S:1061-1071` (`B_com1`): `srl $k1, $k0, 16` / `andi $k1, $k1, 0x3F` / `sll $k1, $k1, 2` / `lw $t0, 0($k1)` / `jr $t0`
  - Table: `ee_core/src/gsm_engine.S:119-127` (`BD_t2:` — 8 rows × 4 = **32 entries**, indices `0x00-0x1F`)
  - The code's own comment at `gsm_engine.S:1062` says the field is "range 0x00-0x1F"
- **Problem (READ mechanism, INFERRED landing spot):** `B_com1` handles opcode 1 (REGIMM: BLTZ/BGEZ/BLTZL/BGEZL/BLTZAL/BGEZAL/…). In the MIPS encoding the sub-opcode is bits 20:16 — **5 bits**. After `srl $k1, $k0, 16`, bit 21 of the instruction (the low bit of the `rs` field) sits in bit 5 of `$k1`. Masking with `0x3F` instead of `0x1F` keeps that bit, so the table index is `subop + 32·(rs & 1)`. For any REGIMM branch whose tested register is odd-numbered (e.g. `bgez $a1, …`, `bltz $t1, …`, `bgez $sp, …`) the index is 32–63, `lw $t0` reads 0–124 bytes **past the end of `BD_t2`**, and `jr $t0` transfers control to that unrelated word treated as an address. This path is only taken when a GS-register store sits in the delay slot of such a branch and traps into `GSHandler` — rare, but the handler exists precisely to emulate arbitrary game code. Every sibling decode in the same file masks correctly: the primary opcode decode at `gsm_engine.S:1048-1050` uses `0x3F` correctly (the primary opcode really is 6 bits — `B_com1`'s mask looks copy-pasted from it), and all register-field decodes (`gsm_engine.S:1089`, `1144`, `1163`, `1176`, `1189`, `1202`, `1208`, and `get_access_info` at `363-365`) use `0x1F`. What word actually sits after `BD_t2` in the linked ee_core image is not determinable from source (INFERRED), so the failure mode ranges from an immediate crash (word = 0 or a data/instruction encoding) to a wild jump.
- **Fix (LESS):** One constant: `andi $k1, $k1, 0x1F` at `gsm_engine.S:1063`. No new code.
- **Benefit:** Removes an out-of-bounds table read and uncontrolled jump in the level-2 exception handler for a whole class of ordinary game branches.
- **Effort:** S (one word; needs a real-hardware or RIPPCSX2 regression pass because GSHandler is exception-path code)
- **Risk:** Low — the change only affects indices that are currently out of bounds; indices 0–31 are bit-identical.
- **Confidence:** High on the mechanism (mask, field width, and table size are all in the cited lines); INFERRED on which concrete word is consumed at runtime.

### Title: `mips_memcpy` return value is overwritten by `mips_memset`'s first instruction executing in its `jr $ra` delay slot — all four copies
- **Category:** (2) Register discipline / branch delay slot
- **Locations:**
  - `modules/iopcore/cdvdman/smsutils.S:127-131`, `modules/ds34usb/iop/smsutils.S:127-131`, `modules/ds34bt/iop/smsutils.S:127-131` (identical lines), and `modules/network/SMSUTILS/SMSUTILS.S:137-141`
  - Contract side: `modules/iopcore/common/smsutils.h:18` — `extern void *mips_memcpy(void *, const void *, size_t);` (and `#define memcpy mips_memcpy` at :29); exported to other modules at `modules/iopcore/cdvdman/exports.tab:238` (`DECLARE_EXPORT(mips_memcpy)`), imported e.g. at `modules/iopcore/cdvdfsv/imports.lst:64`
- **Problem (READ):** Each file sets `.set noreorder` (e.g. `smsutils.S:19`), so the assembler inserts nothing. `mips_memcpy`'s exit is `1: jr $ra` with **no delay-slot instruction of its own**; the physically next assembled instruction is `mips_memset:`'s prologue `move $v0, $a0`, which therefore executes as the delay slot on every return. Entry saved the return value correctly (`move $v0, $a0` at `smsutils.S:28`), but by exit `$a0` has been advanced to `dest + len`, so callers receive `dest + len`, not `dest`. The neighbouring routine shows the intended pattern: `mips_memset` falls through to `_dummy: jr $ra / nop` (`smsutils.S:177-180`) and returns correctly. No in-tree consumer was found that assigns the return value (grep for `= mips_memcpy(` / `= memcpy(` across `modules/` is empty), and the ds34 copies are declared `void` in their own headers (`modules/ds34bt/iop/ds34bt.h:253`, `modules/ds34usb/iop/ds34usb.h:71`), so today this is a **latent** contract violation — but the smsutils library header promises `void *`, cdvdman re-exports the symbol to separately-built modules, and any present or future importer using standard `memcpy` return semantics (e.g. chained `memcpy` idiom) silently gets a pointer off by `len`.
- **Fix (LESS):** Add the missing `nop` after the exit `jr $ra` in each of the four copies (one word each) — or equivalently route the exit through the existing `_dummy`-style `jr/nop`. No logic change.
- **Benefit:** The exported `memcpy` honours its declared contract in all four copies; removes a trap for any importer that uses the return value.
- **Effort:** S
- **Risk:** Low — the added word only occupies a slot that currently executes an unintended instruction; copy behaviour and timing of the loops are unchanged.
- **Confidence:** High (noreorder + label adjacency + header contract are all READ). Impact today is INFERRED-latent: no in-tree return-value consumer exists.

### Title: `Hook_ExecPS2` clobbers callee-saved `$s0-$s3` on the paths that tail-call the original, return-capable `Old_ExecPS2`
- **Category:** (2) Register discipline (caller-saved/callee-saved across a call boundary)
- **Locations:**
  - `ee_core/src/asm.S:255-300` — writes at :259 (`daddu $s0, $a0`, in the delay slot of the entry-range check, so it executes on **both** paths), :261-262 (`$s1`, `$s2`), :268 (`$s3`, delay slot of the COMPAT_MODE_6 check); bypass exit at :296-300 (`1: lw $v0, Old_ExecPS2 / jr $v0`)
  - Other side: `ee_core/src/syshook.c:41` — `int (*Old_ExecPS2)(void *entry, void *gp, int num_args, char *args[]);` (a returning function type), installed at `syshook.c:244-245` (`Old_ExecPS2 = GetSyscallHandler(__NR__ExecPS2); SetSyscall(__NR__ExecPS2, &Hook_ExecPS2);`)
- **Problem (READ clobber, INFERRED consequence):** The function's own comment (`asm.S:253`) assumes preservation is unnecessary "as ExecPS2 won't return". That is true only of the full path (which deliberately wipes all registers via `InitRegsExecPS2` before tail-calling). The two bypass paths — entry `< 0x00100000`, or COMPAT_MODE_6 set — reach `Old_ExecPS2` with `$s0` (always), resp. `$s0-$s3`, already overwritten with copies of `$a0-$a3`, and nothing anywhere in the function restores them. If the original ExecPS2 handler returns (its declared type returns `int`; failure to load/execute is the return case), the game thread resumes with its callee-saved registers destroyed. Sibling syscall hooks in the same file treat live registers as the caller's and preserve them: `Hook_CreateThread` (`asm.S:339-372`) saves/restores `$a0-$a3/$t0-$t3/$gp/$ra` around its C call, and `Hook_SifSetReg` touches only caller-saved registers. Whether the EE kernel's syscall dispatcher independently saves/restores `$s` registers around a `SetSyscall` handler cannot be settled from this tree (INFERRED) — but the hook's own neighbours are written as if it does not.
- **Fix (LESS):** Move the four `daddu $sX` saves below both bypass checks (they are only needed on the full path, where `$a0-$a3` are still live), so the bypass tail-call is reached with `$s0-$s3` untouched. No new saves/restores, no stack change.
- **Benefit:** A failed/bypassed ExecPS2 can no longer corrupt the calling game's callee-saved state.
- **Effort:** S
- **Risk:** Low–Medium — the full path is unchanged (the saves still happen before the stack switch at :274 and the `jal`s that rely on `$s` surviving C calls); bypass path changes only in no longer clobbering.
- **Confidence:** Medium — clobber and bypass reachability are READ; that a return actually reaches game code with these registers live is INFERRED from the `int (*)(…)` type and sibling-hook practice.

### Title: Dead exported symbols — two dangling `.globl` declarations with no definition anywhere, plus two defined exports with zero references
- **Category:** (3) Dead routines / dead exports (full census in §3)
- **Locations:**
  - `ee_core/src/asm.S:66` — `.globl g_argbuf`: **no `g_argbuf:` label exists in any file in the tree** and no reference exists (whole-tree grep returns only this line)
  - `ee_core/src/cheat_engine.S:54` — `.globl j_SetupThread`: likewise no label and no reference anywhere (only this line)
  - `ee_core/src/cheat_engine.S:64,627-628` — `engine_info` (`.word 0x00000002 /* version 0.2 */`, comment "used by the loader"): zero references in C, headers, or asm outside its own definition
  - `modules/network/SMSTCPIP/inet.S:21,62` — `.globl ip_cksum`: zero C references; all in-file callers use its twin label `_ip_cksum` (`inet.S:236,275,365` via `bal _ip_cksum`)
- **Problem (READ):** The two dangling declarations export symbols that do not exist; they survive only because an unreferenced undefined `.globl` produces no relocation and the linker stays silent — they are residue of removed code and will mislead any future reader or symbol audit. `engine_info` and the `ip_cksum` alias are defined but unread by anything in the tree (the loader-side reader `engine_info`'s comment asserts does not exist in this tree; `inet_chksum*` is the live C entry — 7 C references — not `ip_cksum`).
- **Fix (LESS — deletions only):** Delete the two dangling `.globl` lines (`asm.S:66`, `cheat_engine.S:54`). For `engine_info` / `ip_cksum`, delete the `.globl` (keep the data/code) unless an out-of-tree binary-layout consumer is known — that decision is the maintainer's; the census evidence is here either way.
- **Benefit:** Symbol table matches reality; future dead-code and ABI audits stop tripping over phantom exports.
- **Effort:** S
- **Risk:** Low for the dangling pair (provably undefined and unreferenced). Low–Medium for the defined pair — hence split into a maintainer decision rather than a flat deletion.
- **Confidence:** High — census is a whole-tree grep for every `.global`/`.globl` symbol (see §3), re-run per symbol.

### Title: `dve_set_reg` unconditionally returns 0, so `dve_prepare_bus`'s write-until-nonzero retry loop can never repeat
- **Category:** (3) Dead code (never-taken branch), arising from a register-contract mismatch
- **Locations:**
  - `ee_core/src/dve_reg.S:179-186` — `dve_set_reg` exit: `jr $ra` with delay slot `move $v0, $zero` (always returns 0); declared `int dve_set_reg(u32 reg, u32 val);` at `ee_core/include/dve_reg.h:16`
  - `ee_core/src/dve_reg.S:88-92` — `dve_expbay_dve_write_loop: jal dve_set_reg … bnez $v0, dve_expbay_dve_write_loop` — the `bnez` is never taken
- **Problem (READ):** The loop in `dve_prepare_bus` is written as if `dve_set_reg` reports failure in `$v0` and register `0xB1` must be rewritten until it succeeds. The callee as shipped always zeroes `$v0` in its return delay slot, so exactly one write ever happens and the loop scaffolding (loop label, second `addiu $a0, $zero, 0xB1` in the `bnez` delay slot at :92) is dead. Either the retry was lost when `dve_set_reg` was reduced to its current form, or the loop is vestigial — in-tree evidence cannot distinguish intent (INFERRED), but the dead branch itself is provable from the two cited sites. The only other asm caller, `DTV_576P` (`ee_core/src/gsm_engine_adv.S:97-119`), calls `dve_set_reg` straight-line and ignores `$v0`, as does the C caller path (`ee_core/src/gsm_api.c:223-225`, return ignored).
- **Fix (LESS):** Delete the loop: replace :88-92 with a single `jal dve_set_reg` sequence (behaviour bit-identical to today). Restoring a real status return in `dve_set_reg` is the alternative, but that is *more* code justified only by hardware evidence that the `0xB1` write can fail — none exists in-tree.
- **Benefit:** Removes a misleading retry contract from a bus-initialisation path; the next reader no longer assumes `0xB1` is verified-written.
- **Effort:** S
- **Risk:** Low — deleting a never-taken branch cannot change executed behaviour.
- **Confidence:** High on deadness (READ both sides); Low on whether the original retry intent mattered on hardware.

---


---

## TASK 2 — Headers as a corpus: NEGATIVE RESULT (no findings)

Census (READ, exact): 439 first-party headers, 35,808 lines; **714 function-like macro definitions** (661 in modules/, 40 in ee_core/, 13 in include/; tools/ and elfldr/ contribute none); **74 raw / 51 distinct** size/count/length/offset/index/alignment arithmetic macros (the rest are copies in duplicated files — udprdma_packet.h ×3, sio2regs.h ×2, sbp2_disk.h ×2); 13 macros use sizeof; 74 macros evaluate an argument ≥2 times; 109 static inline functions.

**Every one of the 51 distinct arithmetic macros was expanded by hand with a concrete example; zero expand wrong.** The headline negative result: **there is no ARRAY_SIZE-family macro anywhere in the header corpus** — the `sizeof(*ARR)/sizeof(ARR[0]) ≡ 1` defect class (LAUNCHSTATION-119) has no instance here; the only `sizeof(a)/sizeof(a[0])` expressions in the tree are two correctly-written open-coded loop bounds in .c files.

Verified-correct groups (18 documented in the section file, expansions shown): the RA snapshot family (`ra_snap.h:62,65,69,79-81` — TRAILER_OFF/DMA_SIZE/TOTAL_FOR align-up chain, SEND_EVERY nested ceil-div 11529 → 9 chunks → 3 frames, EE_WORK_BYTES; call-site validation at ee_core/src/ra.c:133,154); IGR_CHEATS_BYTES + SPARE_WORDS (igrcheats.h:33,49 — 504-word spare vs 501 needed, cross-checked against both struct sizes); all 12 GET_* extractors in igs_api.h incl. GET_DISPLAY_DH at :114 (suspected >>43 vs >>44 off-by-one; confirmed correct by two independent in-repo sources); MEM_ALIGN family; FD_SET/CLR/ISSET byte-indexing vs the 2-byte fd_set; LZ4_COMPRESSBOUND; RA_WATCH/NODE pack↔extract inverse pairs; UDPRDMA sizes ×3 copies; GS_GIFTAG shifts; _IOR/_IOW; GET_OPL_MOD_ID/SIZE; CDIOC_FNUM.

Dissolved with evidence: LWIP_MAX/MIN (no outer parens, double-evaluating — all 3 call sites pass pure operands); zso MIN (sole call site MIN(b_size, 2048)); GS_GIFTAG/BITBLTBUF/TRXPOS/TRXREG (no outer parens — all 4 call sites are whole-RHS assignments); all 74 double-evaluating macros (no call site in the tree passes ++/-- or a function call). 109 static inlines read for bounds/termination defects — none reachable-defective (notable verified: grid.h navigation set, igrCheatsBuild, apaFence pair, bdmParseMassSlot, psbbnFlowCard/RoundRect with a caller buffer of exactly 40 floats, sio2 regN with callers capped at the 16 hardware registers).

---

## TASK 3 — .github/scripts test corpus (findings RIPTOPL-286…291)

Ledger summary: 303 files under .github/scripts (63,283 lines incl. fixtures + 1 binary .pyc; 302 tracked). Read in full: 24 files / 3,170 lines (every non-test script + 13 test/verifier files). Targeted: 278 files — every remaining test_*.py pattern-scanned for its failure mechanism and workflow-mapped; the largest and every scan-flagged candidate additionally line-read at the mechanism site. Unread: 1 (the git-ignored untracked __pycache__ .pyc). Corpus-level negative result: otherwise unusually non-vacuous — no assert True / self-comparisons / swallowed assertions found; all 104 tests absent from flavours.yml are in fact run by tools/run_tests.sh (ps2build.yml:71); workflow for-loop steps propagate failures (verified experimentally under the Actions bash -eo pipefail shell). Numbering: 286 = S-3, 287 = S-1, 288 = S-2, 289 = S-4, 290 = S-5, 291 = S-6. (S-7, orbit_neutrino_watch's by-design unknown-status path, is recorded in the section file, not counted.)

### Finding S-1 — Four release scripts are dead at pin, while their own headers (and the Makefile) still present them as the live release path — Cat 8 (stale generator corpus / misleading provenance), READ

- `.github/scripts/build_rolling_extras.sh:2-18` (header: "Called by each ps2dev build job in rolling-release.yml"), `.github/scripts/irx_manifest.sh:2-3` ("Called by each build job in rolling-release.yml"), `.github/scripts/asset_version.sh:10` (usage as the release file-name version), `.github/scripts/release_channel.sh:2-3` ("Run from each rolling-release build job BEFORE `make oplversion`").
- Repo-wide grep at pin (READ): **no caller** for any of the four in `.github/workflows/`, `tools/`, or the `Makefile`. The only other references are comments — `Makefile:61` ("see .github/scripts/build_rolling_extras.sh") and `Makefile:113` (release_channel) — and docs (`docs/DOCUMENTATION-GAPS.md:140,277`, `docs/PS2BUILD-MIGRATION.md:175,188`). The live release path is `tools/build_edition.py` / `tools/package_edition.py` / `tools/publish_editions.py` (`.github/workflows/rolling-release.yml:136,174,225`).
- Impact: anyone debugging a rolling release from these headers debugs a pipeline that does not run; `irx_manifest.sh`'s stated purpose — an SDK driver-swap early-warning manifest — **does not exist in current releases at all**. LESS-NOT-MORE: delete all four (and the two Makefile comment pointers), or restore a caller; keeping both pipelines' documentation is the worst option.

### Finding S-2 — `normalize_release_notes.py` is likewise test-only at pin; the shipped notes are produced by a second, duplicated implementation — Cat 8, READ

- `.github/scripts/normalize_release_notes.py:1` claims "Compact release notes shared by publication and normalization", and `:65-78` (`changelog()`) / `:81-114` (`render()`) are a full renderer.
- Repo-wide grep (READ): the only importer/caller is `.github/scripts/test_release_notes.py:14-17`. Publication actually uses `tools/publish_editions.py:325` `release_notes()` (called at `tools/publish_editions.py:610`), which re-implements the same `--no-merges -10` changelog walk and markdown escaping independently (`tools/publish_editions.py:328-336`, READ).
- Impact: `test_release_notes.py` (a thorough suite — real temp git history, READ targeted) validates a renderer that does not ship, while the renderer that does ship is covered only by `test_release_publication.py` for its own copy. The two can drift silently (e.g. the Neutrino marker contract at `normalize_release_notes.py:104-105` — "Neutrino Watch consumes this exact phrase" — is asserted against the dead copy). LESS-NOT-MORE: delete the module + fold its unique tests onto `tools/publish_editions.release_notes`, or make publication import it.

### Finding S-3 — The shipped PSU is verified only by its own author's mirror-image checker; the independent PSUManager cross-checker exists in-tree and is never invoked — Cat 7 (circular verification of a shipped artefact), READ

- `.github/scripts/package_release_app.py:157-174` (`build_psu`) writes the PSU; `:177-211` (`inspect_psu`) reads it back. Both sides share the same constants and helpers: `PSU_ENTRY` struct (`:38`), `psu_date()` (`:144-147`), and `sas_timestamp()` (`:67-102`). `stage()` self-checks at `:263-265` and `verify()` at `:402-409` — a systematic error in the shared date packing, struct layout or SAS slot math passes both directions.
- The independent implementation exists: `.github/scripts/verify_psu_with_psumanager.cpp:1` ("Cross-check the release packager's PSU against techwritescode's PSUManager core"), `:47-79` (loads via `psu::PsuArchive`, compares member set and bytes, returns 1 on any difference). Repo-wide grep (READ): **no invocation anywhere** — no workflow, no `tools/*.py`, no Makefile. `tools/package_edition.py:205-207` calls only `app.verify(...)` (the same-module checker).
- Worse, the claim has already propagated: `docs-sync.yml`'s generated credits text (`.github/workflows/docs-sync.yml:68`, READ) tells users PSUManager is "used by RiptOPL release tooling to independently validate `APP_RIPTOPL.psu` and `APP_RIPTOPL-RA.psu`" and that "the exact upstream core … used by CI [is] preserved in `third_party/PSUManager/`". At pin, CI does no such thing.
- Fix shape (LESS): wire the `.cpp` into `tools/package_edition.py` after `app.verify` (compile once per packaging run), or delete the `.cpp` and correct the credits sentence. Do not keep the claim without the call.

### Finding S-4 — `build_rolling_extras.sh`: the variant/debug loops can move a stale `opl.elf` into a release directory — the exact hazard the same file guards against for RA — Cat 7 (latent; file is dead per S-1), READ + INFERRED mechanism

- RA loop, guarded: `.github/scripts/build_rolling_extras.sh:70-83` — clean failure is fatal-to-variant (`:75-78`), and `rm -f RIPTOPL.ELF` (`:83`) precedes the build precisely "so … the `[ -f ]` test below could pass on somebody else's loader".
- Variant loop, unguarded: `:31` `make clean >/dev/null 2>&1 || true`, then `:32` `if make … && [ -f opl.elf ]; then mv opl.elf rolling/variants/…`. Debug loop identical at `:97-99`. If `make clean` fails (the `|| true` swallows it) and the build then fails before linking, a leftover `opl.elf` from the previous configuration satisfies `[ -f opl.elf ]` and is renamed as the new variant — a wrong-configuration ELF under a trusted name. INFERRED: not executed (would require writing into the repo); the mechanism is fully determined by `:31-34` + the file's own RA commentary at `:70-83`.
- Also by design (documented `:16`): every loop is best-effort — a wholly failed RA/variant/debug set still exits 0. If this file is ever revived, the publish side must assert non-emptiness; at pin the correct action is S-1's deletion.

### Generator dispositions (no finding)

- `package_release_app.py` (449, read-in-full): otherwise strict — ELF magic + non-empty at `:220`, companion count fence at `:160-161`, template key exactly-once fences at `:124-125,139-140`, PSU-vs-folder byte equality at `:264-265`, ZIP duplicate/misplaced-app/required-entry/empty-entry fences at `:277-382`, pinned-UDPFS hash check at `:388-390`, wrong-edition fences at `:394-399`, ELF identity at `:400-401`. Minor (not a finding): a missing pinned UDPFS member raises `KeyError` from `package.read` at `:389`, which `main()`'s `except (OSError, ValueError, zipfile.BadZipFile)` at `:444` does not catch — still exits non-zero via traceback, only uglier.
- `asset_version.sh` (11, read-in-full): `sed -E 's/-[0-9a-f]{7}$//'` at `:11` — correct for the documented shape; dead per S-1.
- `irx_manifest.sh` (72, read-in-full): hash-first-then-header at `:24-30` is sound; the `exit 0` on a missing SDK IRX dir (`:27-30`) is a deliberate warn-and-continue for a diagnostic file; dead per S-1.
- `release_channel.sh` (23, read-in-full): non-Korium refs exit 0 printing nothing (`:12-15`) as documented; `git fetch … || true` at `:18` is followed by a hard `git fetch origin rebuild/main` at `:19` under `set -eu`, so a failed fetch still fails the step; dead per S-1.
- `site_sync.py` (339, read-in-full; invoked `.github/workflows/docs-sync.yml:185-193`): `--check` exits 1 on staleness (`:331-333`); regeneration is content-compared before writing (`:311-315,322-329`). The `--md-page` skip at `:306-308` (page not yet on gh-pages) is documented at `:24-26` and mirrored in the workflow's guarded `git add` (docs-sync.yml, READ) — by design, not a vacuous gate. Note: the RA page itself has no such guard — a missing `retroachievements.html` raises `FileNotFoundError` at `:309`, i.e. fails loudly. Good.
- `wiki_sync.py` (700, read-in-full; invoked `.github/workflows/wiki-publish.yml:98`): conversion is fenced by visible-text invariants (`convert_site` at `:307-317`: re-renders and raises on any text change, plus table/pre count checks; `convert_doc` at `:392-397`: same text/code check for repository documents), dirty-checkout refusal and post-generation re-check in `main()`, unmanaged-page refusal before writing. `validate(pages, stubs)` at file lines 604-625: stub pages' anchors are unchecked **by documented design** (`test_wiki_sync.py` comments, READ) and stubs are used only in the PR-CI test, never in the publish path (`main()` calls `validate(pages)` with no stubs). Not a finding.
- `orbit_neutrino_watch.py` (163, read-in-full): `decide()` at `:58-73` is exact — tag mismatch, missing/renamed asset, missing digest (`unverified`, `:68-69`) and digest mismatch all produce a non-`current` status; it never edits the pin (`:8`). The workflow consumes `status`/`issue.md` (`.github/workflows/orbit-neutrino-watch.yml:56,62`). No finding.
- `ps2build_sdk_watch.py` (452, read-in-full): network-failure → `status=unknown` + return 0 (file lines ~383-394, READ) is documented behaviour for a weekly watch (`:23-24`: "unknown means an official release could not be read… the workflow does nothing until its next run"), while a 404 on a pinned package returns 1 (fail-so-a-person-looks). Mirror releases are pinned only after SHA256SUMS-vs-pin.env agreement (`checked_release`, file lines 227-244). `--check` mode returns 1 on any unpinned/malformed pin. No finding.
- `validate_site.py` (40, read-in-full): raises `ValueError` on duplicate IDs / missing targets / missing anchors (`:32-33`); uncaught → non-zero exit. No caller found in workflows at pin (grep, READ) — it is a manual/local tool; noted, not counted as a gate.
- `hostmap.py` (55, read-in-full): monkeypatches `pathlib.Path.read_text` (`:55`) to rewrite `host::…` calls back to legacy names for source-extraction tests that import it. Documented at `:1-7`. Risk is bounded: an unmapped new `host::` call remains in the extracted C++ and fails compilation of the harness rather than passing silently (INFERRED from the harness pattern). No finding.
- `verify_psu_with_psumanager.cpp` (80, read-in-full): itself correct and strict (`:52-72`); its defect is purely that nothing runs it — see S-3.

## 3. Priority (b) — vacuous-gate hunt: what was checked, and the three conditional findings

Checked across all 278 `test_*.py` (pattern scan, READ; candidates line-read):

- **No assert-nothing bodies, no `assert True`, no literal self-comparisons, no skipped-by-default suites.** Zero hits for `assert True`, `define assert`, `NDEBUG`, constant asserts, or same-operand comparisons (AST + regex scan).
- **No swallowed assertions.** Every `except …: pass/continue` in the corpus was line-read: they are expected-exception probes (e.g. `test_ingame_freeze_diag.py` — `editions.definition("ra","ingamediag")` must raise `ValueError`, file lines ~100-105, with the non-raising path appending to FAILURES), TLS negative-case server threads (`test_ratls_host.py:173-177` area), or timeout-to-failure-list converters (`test_ilink_bd_reset.py`, `test_udpfs_smap_fifo_tail.py` — timeouts *return failure strings*). None wraps an assertion.
- **Exit codes are not discarded.** Harnesses either use `check=True` or explicitly branch on `returncode` → `sys.exit(1)` (tail-verified for every file the scan initially flagged as mechanism-less: `test_counter_unclipped.py`, `test_hdd_module_lifetime.py`, `test_smb2_negotiate_retry.py`, `test_stream_iop_deadlocks.py`, `test_vmode_safe_area.py`, `test_zso_validation.py`, `test_udpfs_smap_fifo_tail.py`, `test_hires_line_blend.py` — all real gates; `test_counter_unclipped.py` even floors its match count, `seen < 2` → exit 1).
- **Fixtures are not generated by the code under test.** `fixtures/selfupdate/rolling-*.txt` are captured real `SHA256SUMS.txt` files (dated 2026-11-12 in `test_selfupdate_host.py:3-5`, READ); the test parses expected digest/revision from the fixture text and compares against the *production C parser's* output (`test_selfupdate_host.py:136-162`) — parser-vs-capture, not code-vs-itself. `fixtures/xerabora-alpha16/*` are an upstream xeRAbora snapshot compiled against the production RA client (`test_ra_upstream_client.py:21-30`, READ). No circular fixture found.
- **`hostmap.py` tests** compile extracted production functions against legacy-named mocks — the logic under test is the real extracted body (see §2); not a self-comparison.

### Finding S-5 — `tools/run_tests.sh` silently SKIPs the site/wiki suites when `bs4` is absent, and the job that runs it does not install it — Cat 8 (conditional vacuous pass), READ

- `tools/run_tests.sh:8`: `case "$t" in *test_site_sync.py|*test_wiki_sync.py) python3 -c "import bs4" … || { echo "SKIP $t (needs bs4)"; continue; };; esac` — a skip is indistinguishable from a pass in the final tally (`:13` counts only failures).
- The invoking job, `.github/workflows/ps2build.yml:71` (`tools/run_tests.sh -k`), installs no Python packages (workflow READ). Whether the runner image happens to carry `bs4` decides whether ~361 lines of generator tests execute there.
- Mitigation (READ): `flavours.yml:351,356` runs both suites in dedicated venvs with pinned deps, on the same triggers — so the suites are not unrun in CI overall. The defect is narrower: the ps2build job's "Host tests" step can report success for two suites it never ran, and a future flavours restructure would silently leave zero coverage. LESS fix: make the skip a failure when `CI` is set, or drop the special case and let the import error fail.

### Finding S-6 — Compiler-absence skips that pass: `test_iso9660_records.py` (whole suite), one test in `test_release_editions.py`, one class in `test_install_ports.py` — Cat 8 (conditional), READ

- `test_iso9660_records.py:112` `@unittest.skipUnless(CC, 'no host C compiler')` — with no `cc`/`gcc`, the entire ISO-9660 walk suite (both `cdvdman_locatefile` copies) reports "skipped" and exits 0.
- `test_release_editions.py` (file lines ~40-42): `if compiler is None: self.skipTest("g++ unavailable; required in Linux CI")` — the message itself says the compiler is *required* in CI, yet absence is a skip, not a failure.
- `test_install_ports.py:405,411`: `skipTest("no host cc/ar")` / `skipTest("host cc does not make ELF objects")`.
- In the actual CI images `cc`/`g++` exist, so today these run; the vacuity is conditional on a leaner runner or a local `run_tests.sh` invocation, where "all tests passed" would be printed for suites that never executed. Contrast `test_smb2_negotiate_retry.py` (tail, READ): no compiler → `sys.exit('No host C compiler found')` = exit 1. That is the corpus's own stricter pattern; align the three files to it.


---

## TASK 4 — Docs prose vs code (findings RIPTOPL-292…295)

16 docs read in full (README.md, ROLLING_RELEASE.md + 14 prioritised docs); 67 claim groups checked: 56 CONFIRMED, 4 CONTRADICTED (below), 7 UNVERIFIABLE from source (listed in the section file, not filed). Context: the docs are otherwise exceptionally aligned — every DEFAULTS.md fresh-start default, and the 64-fragment / 128 MiB fence / 8 MB update / 8–64 MB VMC / 128-byte CFG-title / 12-row File Manager / 10-recents / OFAV-v3 limits all verified at exact path:line. All four fixes are one-line doc-side edits; no code change recommended for any of them.

### Docs — README says menu vibration is on by default; the fresh-start default is Off

- **Severity:** Medium (user-facing default; a fresh install silently does the opposite of what the README promises, and the repo's own defaults page disagrees with the README).
- **Doc (READ):** `README.md:258` — "Controller vibration in the menus **(on by default)**".
- **Code (READ):** `src/core/settings.cpp:173` — `intSetting(CONFIG_OPL_RUMBLE, &gEnableRumble, 0)`, with the table comment at `src/core/settings.cpp:170-171`: "Audio and menu rumble start off, like the original".
- **Corroborating docs (READ):** `docs/DEFAULTS.md:23` lists "Controller Vibration in Menus | Off"; `docs/CONTROLS.md` (Rumble section) says it "is off on a fresh install". The README is the outlier on both sides.
- **Fix shape (LESS):** change the README parenthetical to "(off by default)". No code change.

### Docs — ROLLING_RELEASE download contract names the published ZIPs without their version suffix; every published archive is renamed to carry the version

- **Severity:** Medium (a user hunting a release page for the literally-named files will not find them; the same document set elsewhere states the opposite rule).
- **Doc (READ):** `ROLLING_RELEASE.md:102` — "`RIPTOPL-src.zip`: Git's source archive of the exact built commit"; `ROLLING_RELEASE.md:103` — "`RIPTOPL-LANGS.zip`"; likewise the unversioned bundle names at `ROLLING_RELEASE.md:110` ("`RIPTOPL-VARIANTS.zip`"), `ROLLING_RELEASE.md:111` ("`RIPTOPL-DEBUG.zip`"), `ROLLING_RELEASE.md:113` ("`RIPTOPL-BUGREPORT.zip`") and `ROLLING_RELEASE.md:117-118` ("`RIPTOPL-RA-VARIANTS-IncebtIon.zip`"), all under the heading claim at `ROLLING_RELEASE.md:94`: "Each channel carries exactly these assets".
- **Code (READ):** `tools/publish_editions.py:286-289` — `versioned()`: "every archive ends with the version (`RIPTOPL-Korium-v1.2.0-Beta-3925.zip`)"; `tools/publish_editions.py:292`ff `name_assets()` renames every `.zip` in the channel folder and rewrites `SHA256SUMS.txt` with the versioned names before upload. The unversioned names exist only as pre-publication staging names (`tools/publish_editions.py:163`, archive created as `RIPTOPL-{name}.zip`).
- **Corroborating doc (READ):** `docs/BUILD-CONFIGURATIONS.md:28-29` states the published rule correctly: "Every archive name ends with the build version, for example `RIPTOPL-Korium-v1.2.0-Beta-3925.zip`; the loader ELFs, PSUs, `LICENSE-USMB2.txt`, `BUILD-MANIFEST.json` and `SHA256SUMS.txt` keep fixed names". The fixed-name half of the ROLLING contract (ELF/PSU/manifest/sums/`LICENSE-USMB2.txt`) **is** confirmed against the same code; only the ZIP names are wrong.
- **Fix shape (LESS):** add `-<version>` to the ZIP names in the download contract, matching `BUILD-CONFIGURATIONS.md`. No code change.

### Docs — README collapses the Neutrino Video picker's three distinct 1080i modes into a single "1080i" entry that does not exist

- **Severity:** Low (a user following the README looks for one "1080i" choice; the picker instead forces a choice among three scaling variants the README never mentions).
- **Doc (READ):** `README.md:426-427` — "offers a structured **Neutrino Video** picker (Off / 240p / 480p / 1080i)".
- **Code (READ):** per-game picker `src/ui/guigame.cpp:1754` — `{"Off", "240p", "480p", "1080i x1", "1080i x2", "1080i x3", Default}`; global picker `src/ui/guisettings.cpp:2801` — `{"Off", "240p", "480p", "1080i x1", "1080i x2", "1080i x3"}`. There is no plain "1080i" entry in either picker.
- **Fix shape (LESS):** write "(Off / 240p / 480p / 1080i x1 / x2 / x3)" in the README. No code change.

### Docs — BUILD-CONFIGURATIONS says the bug-report publisher refuses an ELF whose program does not start 0x80 bytes in; the publisher accepts the LLVM/LLD 0x100 start

- **Severity:** Low (maintainer-facing; an LLVM bug-report build that the doc says would be refused is in fact publishable, and the RiptOPLLVM channel does publish a BUGREPORT bundle).
- **Doc (READ):** `docs/BUILD-CONFIGURATIONS.md:22-23` — "The publisher refuses one that is packed or whose program does not start 0x80 bytes into the file."
- **Code (READ):** `tools/publish_editions.py:59-62` — `check_unpacked_layout()` docstring: "LLD emits additional non-loadable MIPS headers, so its first segment starts at **0x100 rather than GNU ld's 0x80**"; `tools/publish_editions.py:72-74` — the refusal test compares the segment offset against a computed 128-byte-aligned header end (`segment_offset`), not against a fixed `0x80`.
- **Fix shape (LESS):** reword to "does not start immediately after its headers on a 128-byte boundary (0x80 for GNU ld, 0x100 for LLD)". No code change.

---


---

## UPDATED RESIDUE STATEMENT (after round 7)

Round 7 closed three of the round-5 verdict's four residue classes and bounded the fourth:

1. **Assembly — CLOSED.** All 4,777 lines of first-party `.S`/`.s` (13 files, a superset of the verdict's 3,854) read end-to-end; dead-symbol census complete; sibling-copy divergence nil (byte-identical). No assembly remains unread. What remains is not reading: whether RIPTOPL-281's out-of-bounds `jr` is reachable from a real game's instruction stream, and the hardware effect of -282/-283, are console questions — INFERRED at source level, as every round has labelled them.
2. **Headers as a corpus — CLOSED for the audited defect classes.** All 714 function-like macros censused; all 51 distinct size/count/index arithmetic macros hand-expanded (zero wrong); all 109 static inlines read for bounds/termination. Header *declarations* (struct layouts, prototypes, constants) remain consulted-not-audited as a corpus, as in every round — a layout-vs-consumer audit is a different pass, not a remainder of this one.
3. **Test corpus — BOUNDED, not line-closed.** 24 files read in full (all generators/verifiers/watches) + 278 test files mechanism-scanned and workflow-mapped; 1 unread file (an untracked `.pyc`). Deliberately not done: line-by-line reading of all 278 near-identical grep-assertion suites — their systemic class is RIPTOPL-005 (pass 1) and their failure mechanisms are now individually verified non-vacuous except -290/-291's conditional skips. A full line-read remains possible; its expected yield is stated honestly as low.
4. **Docs — CLOSED for the prioritised user-facing set.** 16 docs, 67 claim groups: 56 confirmed, 4 contradicted (-292…-295), 7 unverifiable from source (release-process and hardware claims; listed in the section file). Internal planning/handoff docs (FORK-GAP-*, *-PLAN, handoffs) were out-of-class by the task's own framing and remain unaudited as prose.

**Still un-auditable in this environment (unchanged, no round has claimed otherwise):** PS2 build results (no PS2SDK), real-hardware behaviour, live network interop. **Still excluded by the audit's standing framing:** vendored-stack internals (37,655 modules lines + 204,965 third_party lines) — round 7 touched only their asm stragglers (inet.S, SMSUTILS.S) where they sat inside the assembly scope.

**RiptOPL running total: 295 findings (RIPTOPL-001…295).** The repo remains exhausted on C/C++/Python source (rounds 1–5 verdict) and is now also closed on assembly, headers (defect-class scope), and prioritised docs; the single bounded remainder is a full line-read of the 278 test suites, with low expected yield stated in advance.
