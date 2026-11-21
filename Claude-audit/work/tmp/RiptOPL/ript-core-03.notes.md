# ript-core-03 notes (lens structure)

## Done (all lines read; re-read in attempt 2 for exact lines)
- ee_core/src/gsm_engine.S 1-1330 DONE
- ee_core/src/gsm_engine_adv.S 1-424 DONE
- ee_core/src/igrmbox.c 1-122 DONE
- ee_core/src/igrmenu.c 1-1134 DONE
- ee_core/src/igrovl_stub.c 1-68 DONE

## Verified -> findings
- F1 COP0 Count helper + 294912000 constant copied: igrmbox.c 24,36-42; igrmenu.c 698,701-707; padhook.c 126,197-202; freeze_diag.c 53-60; freeze_diag_screen.c 268-276; ra.c 54-61
- F2 GSM engine dead instrs: gsm.S 1055,1069 (la t1 op_t), 1295-1296 (mfbpc a3), 1299-1300 (TRACK_ONLY reload), 676,695 + GSMAdapts 69 + gsm_defines.h 89-92 (Adapted_DISPLAY unused), labels 1119,1257; adv.S 14-15 duplicate include
- F3 GSM_1080P always defined (ps2.yaml 609/644, never removed by tools): stale "variant build only" comments gsm.S 182,204-205; adv.S 138-139
- F4 igrmWaitClear == igrmWaitFor(...,0): igrmenu.c 709-716, 774-781; callers 746,1119,1120
- F5 GSMSourceGSRegs layout declared 4 ways: igrmenu.c 547-557, igrovl_stub.c 22-23, igs_api.h 119-142, ra_overlay.c 41-60
- F6 igrovlFlushCache wrapper: igrovl_stub.c 25-29, igrovl.h 40 (FlushCache void, INFERRED)
- F7 igrmLen = strlen: igrmenu.c 154-160 (strlen available: ee_core.h includes string.h; overlay defines strlen igrovl_main.c 43-50)

## Dropped
- raBuilt (test uses 0 implicitly), gameId copy (GameID not NUL-terminated), PAD_* (no header copy), IGR_MENU_HOST_TEST (tests use), result codes (padhook uses), upstream asm repeated blocks (style only)
