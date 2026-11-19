# RiptOPL Round 7 — Assembly Sources End-to-End

- **Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` (verified `git rev-parse HEAD`, tree git-clean)
- **Clone:** `~/workspace/audit-clones-2026-11-13/RiptOPL` — READ ONLY, no edits made
- **Scope / lines read:** 4,777 lines, every line of all 13 files:
  `ee_core/src/gsm_engine.S` (1330), `ee_core/src/cheat_engine.S` (682), `modules/network/SMSTCPIP/inet.S` (676), `ee_core/src/asm.S` (486), `ee_core/src/gsm_engine_adv.S` (424), `modules/network/SMSUTILS/SMSUTILS.S` (217), `ee_core/src/dve_reg.S` (187), `modules/iopcore/cdvdman/smsutils.S` (180), `modules/ds34usb/iop/smsutils.S` (180), `modules/ds34bt/iop/smsutils.S` (180), `ee_core/src/crt0.S` (146), `ee_core/src/patches_asm.S` (59), `modules/iopcore/patches/iremsndpatch/asm.S` (30).
- **Defect classes only:** (1) wrong constants vs the consuming C, (2) stack/register discipline visible in source, (3) dead exported symbols (full census), (4) divergence among the three `smsutils.S` copies, (5) `gsm_engine.S` vs `gsm_engine_adv.S` overlap/dead parts. No style opinions filed.
- **Labels:** READ = established from source in front of me. INFERRED = mechanism read, consequence depends on runtime/linked-image facts not in the tree.
- **Result:** 5 candidates (1 wrong-constant, 2 register-discipline, 2 dead-code). Everything else dissolved to checked-clean with reasoning in §6.

---

## Candidate findings (unnumbered — coordinator numbers them)

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

## §3 Dead-symbol census (class 3 — every `.global`/`.globl` in scope)

Method (READ): extracted every `.globl`/`.global` from the 13 files, then grepped the **whole tree** (`*.c`, `*.h`, `*.S`, `*.tab`, `*.lst`) per symbol. Counts below are C/H/tab/lst references; asm-internal uses are noted explicitly because several symbols are asm-to-asm only by design.

| Symbol | Defined at | External refs | Verdict |
|---|---|---|---|
| `MIPS_Regs` | `gsm_engine.S:43` | 0 in C/H; used throughout `gsm_engine.S` (:284,352,368,374,985,1091,…) | Live (asm-internal); `.globl` unnecessary but harmless |
| `Old_SetGsCrt` | `gsm_engine.S:46` | 3 (`gsm_api.c:24,136,145`) | Live |
| `GSMSourceSetGsCrt` | `gsm_engine.S:49` | 9 | Live |
| `GSMDestSetGsCrt` | `gsm_engine.S:52` | 6 | Live |
| `GSMSourceGSRegs` | `gsm_engine.S:57` | 29 | Live |
| `GSMDestGSRegs` | `gsm_engine.S:62` | 7 | Live |
| `GSMFlags` | `gsm_engine.S:65` | 21 | Live |
| `Hook_SetGsCrt` | `gsm_engine.S:145` | 5 (`gsm_api.c:80,137`) | Live |
| `GSHandler` | `gsm_engine.S:281` | 4 (`gsm_api.c:81,163`) | Live |
| `Enable_GSBreakpoint` | `gsm_engine.S:1268` | 2 (`gsm_api.h:15`, `gsm_api.c:169`) + asm `jal` :251 | Live |
| `Disable_GSBreakpoint` | `gsm_engine.S:1317` | 2 (`gsm_api.h:16`, `gsm_api.c:180`) | Live |
| `EngineHandler` | `cheat_engine.S:77` | 0 in C/H — reached via the `jal_EngineHandler` instruction word copied into game code by `HookSetupThread` (`cheat_engine.S:598-599`) | Live (non-obvious mechanism, READ) |
| `jal_EngineHandler` | `cheat_engine.S:74` | 0 in C/H; read as data at `cheat_engine.S:598` | Live — it *is* the `jal` opcode that gets patched in |
| `eh_backup` | `cheat_engine.S:140` | 0 in C/H; written at `cheat_engine.S:592-595`, executed as the tail of `EngineHandler` | Live |
| `CodeHandler` | `cheat_engine.S:157` | 3 | Live |
| `HookSetupThread` | `cheat_engine.S:569` | 2 (`cheat_engine.h:33`) | Live |
| `Old_SetupThread` | `cheat_engine.S:653` | 3 | Live |
| **`j_SetupThread`** | **`.globl` only, `cheat_engine.S:54` — no label exists** | **0** | **DEAD — candidate above** |
| `numhooks` / `hooklist` | `cheat_engine.S:659,661` | 2 / 4 | Live |
| `numcodes` / `codelist` | `cheat_engine.S:667,669` | 4 / 5 | Live |
| `codeset` / `codeentry` / `codedone` | `cheat_engine.S:640,675,677` | 4 / 7 / 3 | Live |
| **`engine_info`** | `cheat_engine.S:627` | **0 everywhere** | **DEAD (defined, unread) — candidate above** |
| **`ip_cksum`** | `inet.S:62` | **0 in C** (internal twin `_ip_cksum` is the used entry) | **DEAD export alias — candidate above** |
| `inet_chksum` | `inet.S:233` | 7 | Live |
| `inet_chksum_pseudo` | `inet.S:253` | 11 | Live |
| `inet_chksum_pbuf` | `inet.S:351` | 2 | Live |
| `inet_aton` / `inet_addr` / `inet_ntoa` | `inet.S:406,613,627` | 5 / 20 / 5 | Live |
| `htons` / `ntohs` / `htonl` / `ntohl` | `inet.S:38,39,48,50` | 121 / 62 / 62 / 99 | Live |
| **`g_argbuf`** | **`.globl` only, `asm.S:66` — no label exists** | **0** | **DEAD — candidate above** |
| `Hook_SifSetDma` / `Hook_SifSetReg` / `Hook_ExecPS2` / `Hook_CreateThread` / `Hook_Exit` / `CleanExecPS2` / `iResetEE` | `asm.S:110,200,251,308,383,391,479` | 2 each (`asm.h:17-23` + `syshook.c`) | Live (but see the `Hook_ExecPS2` candidate) |
| `dveStat1` / `dveStat2` | `dve_reg.S:19,22` | 0 in C/H; asm-to-asm: written `dve_reg.S:65,70`, read `gsm_engine_adv.S:86,92` (via `.extern` :18-19) | Live |
| `dve_get_reg` / `dve_set_reg` / `dve_prepare_bus` | `dve_reg.S:123,158,34` | 1 / 4 / 2 | Live |
| `mips_memcpy` / `mips_memset` | all four smsutils copies | 66 / 45 + export/import tables | Live (see delay-slot candidate) |
| `_start` (`SMSUTILS.S:26`, `crt0.S:36`) | — | IRX entry point / linker entry | Live |
| `_exit` | `crt0.S:113` | 19 | Live |
| `_root` (not `.globl`; `.ent` only) | `crt0.S:136` | referenced by `crt0.S:66` (`la $8, _root`) | Live, internal |
| `SRWI_IncrementCntrlFlag` | `patches_asm.S:28` | 5 (`patches.c:294,300,305,307,310`) | Live |
| `RnC3_AlwaysAllocMem` | `patches_asm.S:49` | 2 (`patches.c:325,363`) | Live |
| `_lock_RpcFunc` | `iremsndpatch/asm.S:12` | 2 (`iremsndpatch/main.c:30,97`) | Live |

Non-global `.ent` routines checked for internal reachability and all reachable: `_SifSetDma`, `_Apply_Mode3`, `InitRegsExecPS2` (all `asm.S`, called/jumped in-file), `DTV_576P`/`DTV_1080P` (see §5), `_dummy` (export-table filler, `SMSUTILS.S:188`), `setdve_576P` is C (`gsm_api.c:219`).

---

## §4 The three `smsutils.S` copies — divergence verdict (class 4)

**There is no divergence. None.** (READ)

```
3612cf1bd66a11f0b9e53ac78a5940d7  modules/iopcore/cdvdman/smsutils.S
3612cf1bd66a11f0b9e53ac78a5940d7  modules/ds34usb/iop/smsutils.S
3612cf1bd66a11f0b9e53ac78a5940d7  modules/ds34bt/iop/smsutils.S
```

`diff -u` between every pair is empty — the three 180-line files are byte-identical, so there are zero diff lines to classify as intentional-drift vs suspected-bug. Consequences, both directions:

- Any defect in one is in all three — and the `mips_memcpy` delay-slot candidate in §2 is exactly such a shared defect (it is also in the fourth sibling below).
- The three consumers do **not** declare the same contract for the identical code: cdvdman re-exports the routines under the `void *`-returning declaration in `modules/iopcore/common/smsutils.h:18,21`, while ds34usb/ds34bt declare them `void` (`modules/ds34usb/iop/ds34usb.h:71-72`, `modules/ds34bt/iop/ds34bt.h:253-254`). That declaration split is in the headers, not the asm, and is why the shared delay-slot defect is live-contract-relevant only on the cdvdman/SMSUTILS side.

The fourth sibling, `modules/network/SMSUTILS/SMSUTILS.S` (217 lines, md5 `f7dfc2506cef391477866c8542e6e6b6`), differs from the three only by its IRX wrapper, classified line-by-line (READ, full diff taken):

| Diff | Classification | Mechanism |
|---|---|---|
| Header comment drops "by Eugene Plotnikov" (`SMSUTILS.S:8`) | Intentional drift (cosmetic) | No code effect |
| `.globl mips_memcpy/mips_memset` replaced by `.globl _start` + a `_start` that saves `$ra` on a 4-byte frame, `jal RegisterLibraryEntries` with `__exp_smsutils` in `$a0` (delay slot), restores and returns 0 (`SMSUTILS.S:23-35`) | Intentional — this copy is a standalone IRX library; the other three are linked directly into their modules | Stack balanced (-4/+4), `$ra` saved/restored around the `jal` — checked clean |
| Whitespace-only realignment of load/store offsets in the 16-byte copy loops (`SMSUTILS.S:61-71`, `105-111` vs `smsutils.S:51-60`, `105-111`) | Intentional drift (formatting) | Instruction-for-instruction identical |
| Trailing export table `__exp_smsutils` (entries: `_start, _dummy, _dummy, _dummy, mips_memcpy, mips_memset` — `SMSUTILS.S:193-205`) and loadcore import stub `RegisterLibraryEntries` (`SMSUTILS.S:207-217`) | Intentional — library registration the linked-in copies don't need | Entry ordinals cross-check clean against the consumer header: `smsutils.h:19` `DECLARE_IMPORT(4, mips_memcpy)` and `:22` `DECLARE_IMPORT(5, mips_memset)` match table positions 4 and 5 exactly (READ, both sides) |

No suspected-bug divergence exists between any of the copies.

---

## §5 `gsm_engine.S` vs `gsm_engine_adv.S` — overlap / dead parts (class 5)

**Structure (READ):** `gsm_engine_adv.S` is not a translation unit. It has no `.globl` at all, appears in no Makefile object list (`ee_core/Makefile:16` builds only `gsm_engine.o`), and is textually included at `ee_core/src/gsm_engine.S:1260` (`#include "gsm_engine_adv.S"` — its own header says so at `gsm_engine_adv.S:11`). Treating it as a standalone file would double-count its lines; its 424 lines split into three parts:

1. **`DTV_576P` (`gsm_engine_adv.S:23-126`) — live, always compiled.** Reached from `Hook_SetGsCrt` at `gsm_engine.S:228` (`jal DTV_576P`) when the target mode is `GS_MODE_DTV_576P` and the console lacks native 576P. Stack discipline checked clean: -0x10 frame, `$ra` saved at :26 and restored on both exits (:122 and the delay-slot load on the `DTV576P_end` branch), all inner `jal`s (`setdve_576P` :83, `dve_set_reg` :97-119) pass arguments in delay slots, and its `$v0/$v1/$a0` clobbers are caller-saved registers its caller does not need afterwards.
2. **`DTV_1080P` (`gsm_engine_adv.S:140-236`) — conditionally dead, deliberately.** The whole routine sits inside `#ifdef GSM_1080P`, and its only dispatch is inside the matching `#ifdef` at `gsm_engine.S:182-216`. In a default build **both** halves vanish together — there is no configuration in which the dispatch exists without the routine or vice-versa (both keyed on the same macro, set only by the variant build per `ee_core/Makefile:35`). It makes no `jal`, so its lack of a stack frame / `$ra` save is correct, not a defect. All seven 64-bit raster constants it builds were re-assembled by hand from their `lui/ori/dsll` chains and match their own comments exactly (SMODE1 `0x00000003422304B1`/`…4222…`/`…4220…`, SYNCH1 `0x000344200B04182D` new-GS / `0x000344200B043829` old-GS, SYNCH2 `0x0020FB61`/`0x0020EB63`, SYNCV `0x0150E00201C00005`, SRFSH 4) — READ, checked clean. The routine is documented in-source as not hardware-validated (`gsm_engine_adv.S:136-139`); that is a testing gap, not an asm defect, and is already disclosed where it lives.
3. **GS register write handlers + SMODE1/SRFSH/SYNCH/SYNCV reference tables (`gsm_engine_adv.S:238-424`) — live, and this is the part the filename hides.** `have_SMODE1_write`, `have_SRFSH_write`, `have_SYNCH1_write`, `have_SYNCH2_write`, `have_SYNCV_write` are the branch targets of `GSHandler`'s `normal_GS_write` dispatch at `gsm_engine.S:541,545,547,549,551`. Because the include point (:1260) is *after* `.end GSHandler` (:1258), these handlers assemble into the same object immediately following the handler that jumps to them — forward branches within one TU, fully reachable, not dead.

**Overlap:** No code is duplicated between the two files. The split is by register family: PMODE/SMODE2/DISPFB/DISPLAY/CSR handling lives in `gsm_engine.S` proper, SMODE1/SRFSH/SYNCH1/SYNCH2/SYNCV handling lives in the included file; each `store_v0_as_*` / `have_*_write` label exists exactly once across the pair (verified by label census). The only repeated *pattern* is the two DTV routines each programming the SMODE1→SYNCH1→SYNCH2→SYNCV→SMODE2→SRFSH sequence with mode-specific constants — that is the hardware-required sequence per mode, not copy-paste dead weight, and their constants differ in nearly every word (576P constants likewise hand-verified against their comments: SMODE1 `0x00000017404B0504`/`…4049…`, SYNCH1 `0x000402E02003C827`, SYNCH2 `0x0019CA67`, SYNCV `0x00A9000002700005` — all exact, READ).

One comment/code mismatch in this area, dissolved rather than filed: `gsm_engine_adv.S:69-71` comments `*GS_SRFSH=0;` but stores `4` (`li $v1, 4`). The **code** is the consistent side — `DTV_1080P` also stores 4 (`gsm_engine_adv.S:203-205`, comment agrees there), the SRFSH reference table in the same file gives 4 for the sibling DTV-480P mode (:339), and `gsm_api.c:220` states 576P's DVE parameters are "exactly the same as the 480P mode's". Stale comment, correct constant; no candidate.

---

## §6 Checked-clean / dissolved (with the cross-check that dissolved each)

**Constants vs C (class 1) — all exact, both sides cited:**
- GSM data layouts: `GSMFlags` `.space 20` (`gsm_engine.S:65`) and offsets `gsm_defines.h:74-94` (incl. `TRACK_ONLY=19`) match the packed C struct field-for-field (`gsm_api.c:58-74`: 2×`u32` + 11×`u8` = 19, `track_only` at 19, total 20). `GSMSourceGSRegs`/`GSMDestGSRegs` `.space 88` = 11×`u64` (`gsm_api.c:43-56`, `igs_api.h:126-139`); `*SetGsCrt` `.space 6` = 3×`s16` (`gsm_api.c:36-41`); `GSMAdapts` `.space 18` vs `gsm_defines.h:88-92` (8+8+1+1). `MIPS_Regs` `.space 0x220` (`gsm_engine.S:43`): highest save is `sq` at offset `0x210` (:319), ending exactly at `0x220`.
- `GS_MODE_DTV_576P=0x53` / `GS_MODE_DTV_1080P=0x54`: `gsm_defines.h:34-35` = `include/renderman.h:18,21` = `gsm_api.c:34` (the C file even documents the three-way sync at :32-33).
- GS register offsets `gsm_defines.h:18-31` (GS_BASE `0x12000000`, CSR `0x1000`, …): consistent with C's absolute definitions (`igs_api.h:100-101`, CSR/IMR page at `0x12001000`/`0x12001010`) and with `Enable_GSBreakpoint`'s masks — `TRAP_MASK 0x1FFFEF0F` (`gsm_defines.h:17`) vs the TRACK_ONLY variant `0x1FFFFF0F` (`gsm_engine.S:1278`) differ in exactly bit 12, which is precisely the bit that excludes the `+0x1000` CSR page under base `0x12000000`, as its comment claims; BPC controls `0x60280000` vs `0x20280000` (:1298,1303) differ in exactly the read-enable bit the comments describe.
- `asm.S:16-17` `COMPAT_MODE_3 0x04` / `COMPAT_MODE_6 0x20` = `ee_core/include/ee_core.h:61,64` = `include/iosupport.h:99,102`.
- `asm.S` `Install_PadOpen_Hook` arguments `0x00100000, 0x01ff0000, PADOPEN_HOOK(0)` (:282-285 and :354-357) = the C call at `syshook.c:60` verbatim.
- `Hook_SifSetDma` field checks (`asm.S:113-135`): `SifDmaTransfer_t.attr` at `0x0C`, `.size` at `0x08` (SDK struct, src at 0/dest at 4/size at 8/attr at 12), reset-packet `psize` at src+0 and `fcode 0x80000003` at src+8 — consistent with `SifCmdHeader` layout (psize/daddr/fcode) and the reboot-trap purpose documented in `syshook.c:50-60`; the `0x44`/`0x68`/`0x70` values are protocol constants of the IOP-reboot SIF command with no contradicting in-tree definition.
- `Hook_CreateThread` offsets `0x14`/`0x18` (`asm.S:321,329`) = `ee_thread_t.initial_priority`/`current_priority` in the ps2sdk header that the C prototype in `asm.h:20` is compiled against (status 0, func 4, stack 8, stack_size 0xC, gp 0x10, priorities 0x14/0x18). INFERRED-clean: the SDK header is not vendored in-tree, but no in-tree code contradicts it.
- `cheat_engine.S:33-35` `MAX_HOOKS 5` / `MAX_CODES 250` = `cheat_engine.h:27-28`; `.fill` sizes (:662,670) are exactly `MAX*2` words, and the C loader bounds-checks against the same products (`cheat_api.c:97-105`). `type_tab` (`cheat_engine.S:530-546`) has exactly 16 entries for the 4-bit type field decoded at :190-193, with 8/9/A/B/F intentionally 0 (checked at :194 `beqz $t5, next`).
- DVE constants agree across the asm/asm boundary: `dve_prepare_bus` accepts `dveStat1 & 0xFE == 0x1C` and halts on `0x1D` (`dve_reg.S:72-84`); `DTV_576P` tests the same `& 0xFE == 0x1C` and `dveStat2 & 0xF0 == 0x20` pair (`gsm_engine_adv.S:86-94`).
- `crt0.S` syscall numbers (SetupThread 60 :68, SetupHeap 61 :76, FlushCache 100 :82, Exit 4 :130, ExitThread 35 :137) are the standard ps2sdk numbers; the file's only in-tree consumer relationship is `main.c:23` `int isInit` matching `la $4, isInit` at `crt0.S:37`. The stray `.extern _isInit` at `crt0.S:23` names a symbol that is never used — a `.extern` with no reference creates no relocation, so it is inert (dissolved, not the same class as the filed dangling `.globl`s, which purport to *export*).
- `patches_asm.S`: `SRWI_IncrementCntrlFlag`'s flag address `0x003E0000-17388 = 0x003DBC14` and its Status mask `lui $at,1` = bit 16 have **no in-tree C counterpart to check against** (the C side, `patches.c:294-310`, supplies only the four JAL patch sites in the game's binary) — unverifiable in-tree, not filed. `RnC3_AlwaysAllocMem`'s `0x20000000` OR sets exactly bit 29, matching the requirement spelled out in `patches.c:345-352`, and its `0x3D8` store offset is a game-binary constant likewise without an in-tree counterpart.
- `inet.S` byte-swap routines hand-executed on test values: `htons(0x1234)=0x3412`, `htonl(0x11223344)=0x44332211` — both correct (READ by manual execution of `inet.S:38-60`). `inet_chksum_pseudo`'s 5th-argument load at `72($sp)` (:268) is exactly right for its -56 frame under o32 (caller's `sp+16` stack-arg slot). Export ordinals for smsutils cross-checked in §4.

**Stack / register discipline (class 2) — balanced everywhere else (READ):**
- `asm.S`: `_SifSetDma` (:88-99) and `_Apply_Mode3` (:163-189) switch to the `_end` stack and restore `$sp` from the saved slot (no `addiu` owed); `Hook_CreateThread`'s -0x60 swapped-stack frame saves and restores all 11 registers it lists (:340-372); `Hook_SifSetDma`/`Hook_SifSetReg`/`Hook_Exit` use the `sw …, 8($sp)` syscall-EPC rewrite idiom and clobber only caller-saved registers; `CleanExecPS2`/`InitRegsExecPS2` wipe registers *deliberately* (documented at :402-404) and preserve `$a0-$a3/$ra` by design.
- `cheat_engine.S`: `EngineHandler`'s -0xe0 frame is exactly 28 slots × 8 bytes, saves :79-106 and restores :111-147 are mirror images including `$ra/$gp/$fp`; `CodeHandler` makes no `jal` and needs no frame (all transfers are internal `jr/b`); `HookSetupThread` -0x10/`$ra` pair is balanced (:570-609) and it provably does not touch the syscall arguments `$a0-$a3/$t0` its header comment (:559-565) forbids modifying.
- `gsm_engine.S`: `Hook_SetGsCrt` -0x10/`$ra` balanced on every exit path including the TRACK_ONLY early branch (:146-258); `GSHandler` is an exception handler, not an ABI function — its save set (`sq` :286-321, offsets 0x10-0x210, HI/LO at 0x200/0x210) and the union of its two restore blocks (`exit_GSHandler` :988-1022 + `GSHandler_Final_Exit` :1245-1253) cover every register it clobbers, with `$t0-$t2` intentionally deferred to the final block because the exit path itself uses them; its internal `jal get_access_info` / `jal have_DISPLAY_write` / `jal have_DISPFB_write` targets never `jal` further and return via `jr $ra` to the right place (the DISPFB pair deliberately load the return value in the `jal` delay slot, :649-650,661-662).
- `inet.S`: every non-leaf routine's frame balances — `inet_chksum` -8/`$ra` (:234-251), `inet_chksum_pseudo` -56 with `$s0-$s7+$ra` all restored (:254-349; its use of `$ra` as a data register at :309-319 is safe because the saved copy is reloaded at :339 and the two `bal _htons` return addresses are consumed before then), `inet_chksum_pbuf` -40 (:352-404), `inet_aton` -40 with `$ra` used as scratch but reloaded from `36($sp)` on **every** exit path (:509,558, and via :19 at :558-562), `inet_addr` -32 (:614-625). `inet_ntoa` is a leaf that touches only caller-saved registers and its own -8 scratch (its final `sb $zero, -1($a3)` in the `jr` delay slot writes the string NUL exactly where the loop over-advanced — intentional).
- `dve_reg.S`: `dve_prepare_bus` -0x10/`$ra` is restored on all three exits (expbay-checked :85, expbay-written :95 delay slot, pcmcia :113) before the common `jr $ra` / `addiu $sp` delay at :116-117; `dve_get_reg`/`dve_set_reg` are leaves. (The one dead loop inside it is filed separately.)
- `crt0.S`: no stack frames to balance (it *creates* the stack via the SetupThread syscall at :62-70); `_args` reservation `4+16*4+256` at :144 matches the argc/16-argv/256-byte layout `main.c` consumes.
- `SMSUTILS.S` `_start` -4/`$ra` balanced (:27-35). `iremsndpatch/asm.S` `_lock_RpcFunc` -0x18/`$ra` balanced with the restore in the `jr` delay slot (:15-29); its reliance on `$s2` already holding `$a0` and its use of `$s0/$s1` as save slots across `jal _lock` is a contract with the *patched game's* prologue at `main.c:97`'s patch site — the IREMSND binary is not in this tree, so correctness there is unverifiable in-tree (INFERRED) and the wrapper's own comment (`asm.S:13`) states the `$s2` half of the contract explicitly. Not filed.
- `gsm_engine.S` `get_access_info` zero-extends the instruction's 16-bit offset instead of sign-extending it (:370-371): dissolved as speculative — GS register offsets reachable from any sane base value in game code are the small positive `.equ` offsets, and no in-tree or cited game evidence shows a negative-offset GS access; without such evidence this is a hypothetical, not a defect.

**Other dissolved items:**
- `DTV_576P` comment "``$a0 = GCONT setting``" (`gsm_engine_adv.S:28`) vs code `sll $a0, $a3, 25` (:29): the **code** agrees with its caller, which computes the GCONT bit into `$a3` at `gsm_engine.S:224` (`andi $a3, $v1, 1`, the delay slot of the preceding `beqz`). Comment names the wrong register; the value flow is correct. Not filed (comment only).
- `gsm_engine.S:344` comment "``op_num*2``" on a `sll …, 2` (×4, correct for a `.word` table): comment arithmetic wrong, code right. Not filed.
- `inet_aton`'s duplicated numeric label `10:` (`inet.S:426` and :550): gas numeric-local labels resolve `10b` to the nearest preceding definition, and every reference was traced to its intended head (:490→:426; :572,589,610→:550). Legal and correct as written. Not filed.
- `SRWI_IncrementCntrlFlag`'s spin on Status bit 16 after `di` (`patches_asm.S:30-37`): the EE `di`/EIE idiom from the SDK-era patch literature; no in-tree header (`ee_cop0_defs.h` is SDK-provided, not vendored) contradicts the bit choice. INFERRED-clean, not filed.

---

## Method note

Every file was read in full. Cross-checks were whole-tree greps (census), pairwise `diff -u` + `md5sum` (smsutils copies), hand re-assembly of all DTV raster constants from their `lui/ori/dsll` chains, hand execution of `htons`/`htonl` on test vectors, and delay-slot tracing of every `jal`/`jr`/`bal` in the 13 files. Nothing was built and nothing was run on hardware; all runtime-consequence statements are labelled INFERRED where they depend on the linked image or on game/kernel behaviour outside this tree.
