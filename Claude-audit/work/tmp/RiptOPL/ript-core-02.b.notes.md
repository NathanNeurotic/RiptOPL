# ript-core-02.b notes (defects lens)

STATUS: findings written, validator OK (3 findings).

Done: asm.S 1-486, cd_igr_rpc.c 1-26, cheat_api.c 1-185, cheat_engine.S 1-682, crt0.S 1-146, dve_reg.S 1-187,
freeze_diag.c 1-297, freeze_diag_format.c 1-444, freeze_diag_screen.c 1-405, gsm_api.c 1-245 (all read in full).

Candidates:
- ee_core/src/cheat_api.c:40-71 + main.c:116-117 | LinkImage runs in eecoreInit before LoadExecPS2/sysLoadElf wipes memory and loads the game (syshook.c:117,126) -> .img patches never land / gate never matches | KEEP (checked main.c:168-182, syshook.c:86-147, host.cpp:161-163, git 97421c95)
- ee_core/src/freeze_diag.c:213 | shutdown_cd gets only .server, not .buf; ps2sdk SifCallRpc sends data to cd->buf -> 16 bytes DMAed to IOP 0, arg not delivered (server buf stale, happens to be 0) | KEEP rank 1, SDK part INFERRED
- ee_core/src/cd_igr_rpc.c:21-22 | uncached store + NOWBDC with no FlushCache: dirty cached stack line can be written back over the value before the SIF DMA; freeze_diag.c:185,214 flushes first | KEEP rank 2 Low INFERRED
- DROPPED: asm.S hooks share _end stack (theoretical concurrency); freeze diag TLB remap (speculative); dve 0x1D halt (upstream, 576P gated to ROM<2.10 in gsm.cpp:191); LinkImage unaligned offset (cheat data writes memory by design); stale heap record (heapdiag.c:141-146 clears old ones); igrmFontData dup (IGR_MENU off in ingamediag, build_configuration.py:30-35).
