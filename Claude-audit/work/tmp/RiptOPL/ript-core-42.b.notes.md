# ript-core-42.b notes (lens defects)

Done: src/ui/menuwatch.cpp 1-584 (read full), src/ui/selfupdate.cpp 1-433 (read full)

Candidates:
- src/ui/selfupdate.cpp:250-251,303-343 | comment promises a free-cluster pre-check before TRUNC overwrite; none exists -> near-full card leaves boot ELF truncated | check git history, platform/selfupdate.c for a fits check, callers
- src/ui/selfupdate.cpp:161-169,230,238 | sums buffer not NUL terminated, passed with len to suParseSums | check suParseSums respects len
- src/ui/selfupdate.cpp:184 | raTlsBodyLength semantics (full length? set before sink? chunked?) | check ratls.c
- src/ui/selfupdate.cpp:128 | +4 margin on mc name length (stale temp-name scheme?) | low value, check history
- src/ui/menuwatch.cpp:536-546,299-351 | watchdog thread not stopped by menuWatchStop; can fire during deinit/launch (>8 s no frames) and touch GS/libmc after IOP reset | check deinit callers, how long launch takes, whether frames drawn
- src/ui/menuwatch.cpp:261-268 | mc fd left open when mcWrite refused/failed, then newlib fallback on same file | minor
- src/ui/menuwatch.cpp:56-57,213-215 | logCopy 12 KB .bss could be avoided (diag only) | check diagCopy
- src/ui/menuwatch.cpp:78-97,128-143 | step names / colours vs enum in include/menuwatch.h | check header
