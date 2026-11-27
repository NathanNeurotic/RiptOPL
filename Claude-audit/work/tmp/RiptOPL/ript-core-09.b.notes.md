# ript-core-09.b notes (lens defects)

Done: src/app/settingsio.cpp 1-2621 read fully. Verification done.

Kept:
- F1 settingsio.cpp:1557-1563,962-984,2397-2402 + opl.cpp:1885-1895 | config.path only read when boot home lacks master -> custom path ignored | verified (no remove of old master, rename does not touch disk)
- F2 settingsio.cpp:2368-2371,2419-2420,659-663 + bdmboot.cpp | redirect stores resolved massN: -> boot literal-slot resolve, full budget 14 s, no type | verified budgets
- F3 settingsio.cpp:2419-2424 | config.path rewritten on every save incl CONFIG_LAST at launch | verified callers
- F4 settingsio.cpp:2215 + guiwait.cpp:99-110 | stale queued _saveConfig after timeout runs with lscstatus 0 | verified queue not cancelled
- F5 settingsio.cpp:2452-2458 vs 340-343 | redirect write after alt-device save always refused | verified

Dropped: grid clamp (GRID_LAYOUTS is preset count, 0=theme, correct); mkdir slot mismatch (deferred only, same value); MMCE delay loop (driver now real, marginal); concreteMcHome uninit read (result cannot change); IP sscanf ranges (upstream).
