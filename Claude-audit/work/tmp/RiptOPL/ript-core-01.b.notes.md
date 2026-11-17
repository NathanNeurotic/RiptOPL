# ript-core-01.b notes (lens defects)

## Done
All 36 files read in full (Makefile, debug_colors_doc.sh, linkfile, igrovl/igrovl_main.c, igrovl/linkfile, all ee_core/include/*.h in list).

## Candidates
- reset_arg_bounds.h:4-17 + iopmgr.c:83-92 | suffix 7 but 8 bytes written (host0: NUL) | KEEP rank 1 latent (payload capped 70 by sizeof(arg)=80 INFERRED)
- coreconfig.h:46 + ee_core.h:44-52 + util.c:96-100 + settingsio.cpp:1924 + system.cpp:1561 | eth_linkmode unvalidated -> SmapLinkModeArgs[mode-1] OOB | KEEP rank 2
- coreconfig.h:43 + system.cpp:1580 + settingsio.cpp:1950 + util.c:84-85 | ps2_ip %d into [16] (net-block) | fold into ETH finding
- coreconfig.h:122-135 IgrMenuNoRoom under RA used under IGR_OVERLAY | consistent, drop
- modules.h conditional IDs | consistent across main/core/igrovl, drop
- igs_api.h packed GSM structs | trivial, drop
- igrcheats.h Build/Fit | launcher-built, bounds OK, drop
- padpatterns.h masks verified; .data vs .rodata same ram84, drop
- igrovl strlen self-recursion via ldist | pointer loop not transformed (INFERRED), drop
- linkfile ASSERT fine; Korium stack 5632 bytes (BUILD-EVIDENCE)
