# ript-core-10 notes (lens structure)

## Done
- All 8 files read fully. Verification done. findings/RiptOPL/ript-core-10.json written, validator OK (10 findings).

## Dropped candidates
- config.h: every CONFIG_*/CFG_* macro and API function has at least one user except CONFIG_OPL_ENABLE_ANALOG_NAV (reported).
- cheatman.cpp set_cheats_list tail, freeCheatTable wrapper, bgm quiesce/stop overlap: too small or not a clean win.
- cheatconfig.cpp normalisation cost: other lens (perf).
