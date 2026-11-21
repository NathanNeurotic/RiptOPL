# ript-core-02 notes (lens structure)

## Files done (all read in full, re-read in second attempt)
- all 10 files, every line

## Verified candidates -> findings
- gsm_api.c:108-117 constant fix flags; engine sites gsm_engine.S 596-601,624-626,678-680,697-699,715-717,937-939; adv 307-312,345-350,369-374,390-395,415-420; syncv stored 104 but Target_SYNCV only read behind SYNCV_fix=0
- cheat_api.c:181-184 CheatLiveFits forwarder; igrovl.h:34, igrovl_side.h:20, igrovl_stub.c:34, igrmenu.c:1028
- gsm_api.c:196-214 vs 233-245 tracker dup; callers padhook.c:588-596, main.c:140-151
- crt0.S 14-18,85-93,114-128,145-146 ctor/dtor dead (no _init/_fini in ee_core, -nostartfiles -nostdlib)
- dead asm decls: asm.S 37-40,44-45,64,66; cheat_engine.S 27-30,43-45,54,64,108,624-628; crt0.S 23; dve_reg.h 15; HookSetupThread ra save 570-571,608-609
- COP0 count copies: freeze_diag.c 36-37,53-60; screen.c 268-276; igrmenu.c 698-707; igrmbox.c 24,36-42; ra.c 54-61; padhook.c 126,197-202
- formatter dup format.c 16-57,304-325 vs screen.c 36-94; screen.h 14-15
- cheat_api.c 50-51,55-56 impossible checks; 122-137 wrappers
- freeze_diag.c 26 vs cd_igr_rpc.c 15 literal; fetch_iop return unused 156,201-202,239

## Dropped
- dveStat2 (read in gsm_engine_adv.S:92); reset tail dup (not equivalent); GIFTAG/power dup with igrmenu (different paths/builds); MAKE_J unique
