# ript-core-03.b notes (lens: defects)

Done: ee_core/src/gsm_engine.S 1-1330, ee_core/src/gsm_engine_adv.S 1-424, ee_core/src/igrmbox.c 1-122, ee_core/src/igrmenu.c 1-1134, ee_core/src/igrovl_stub.c 1-68 (all read fully)

Candidates:
- gsm_engine.S:1128-1134 | B_taken zero-extends 16-bit branch offset (andi 0xFFFF + sll 2): backward taken branch with trapped access in delay slot resumes at EPC+4+~0x3FFxx | upstream (blame 2015/2021); reachable via GSM read trap (DRE, CSR polling loops) and TRACK_ONLY write trap (IGR menu opt-in, main.c:150) -> KEEP
- gsm_engine.S:1061-1068 | B_com1 masks REGIMM rt with 0x3F (includes rs bit 21): odd rs -> index past 32-entry BD_t2 -> jr to garbage | same finding as above -> KEEP (merged)
- gsm_engine.S:182,204-205 + gsm_engine_adv.S:136-139 | comments say DTV_1080P only in variant build; ps2.yaml:609,644 define GSM_1080P in base | stale comment, cat 7, rank 1 -> KEEP low
- gsm_engine.S get_access_info / do_load | load with rt=$zero writes MIPS_Regs[0] -> later sd $zero emulated with stale value | needs lw/ld $zero from CSR in game code: speculative -> DROP
- gsm_engine.S B_BC0x/B_BC1x | delay slot of COP1 branch: store re-executed, branch dropped | rare, fix adds code -> DROP
- igrmenu.c stack use on 4 KB IGR stack (padhook.c:108) | estimated ~3 KB worst path | no overflow shown -> DROP
- igrmenu.c igrmRead failure paths (GS left mid local->host) | edge, reset path handles -> DROP
- igrmbox.c mbPost 3 s spin per request | only when IOP thread starved -> DROP
- igrmenu.c igrmHoldBack 252 SENDs at 0.5 ms IOP poll | ~0.13-0.25 s once at RESUME -> DROP
- igrovl_stub.c | no defect found
