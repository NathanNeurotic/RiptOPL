# ript-core-13 notes

## Done
- src/core/vmc_groups.cpp 1-1474 (read full)
- include/vmcsaves.h 1-55 (read full)
- include/xparam.h 1-34 (read full)
- src/core/xparam.cpp 1-246 (read full)
- src/core/vmcsaves.cpp 1-1257 (read full)

## Pending
- none: findings file written and validated OK (7 findings)

## Candidates
- src/core/vmc_groups.cpp:5-10,14-1349,1352-1451 | 97 title arrays + 97 count_ constants + struct; table could be one flat array (groupId,titleId pairs or NULL-terminated) dropping ~97 count lines | check callers of getGroupIdForTitleId, include/vmc_groups.h
- src/core/vmc_groups.cpp:1149-1152 vs 1143-1146 | group 100.72 title list identical to 100.71: unreachable (first match wins) | verify; also dup entries within arrays (668 dup of 665; 676; 1029; 1057; 1226) and malformed IDs (443,444,464,465,472,473,498,614) that can never match normal IDs -> check what format callers pass
- src/core/vmc_groups.cpp:1272-1275 vs 936-939 | 000.12 titles all already in 100.53 -> unreachable group
- src/core/vmc_groups.cpp:1153,1158,1172,1180 | stale "was sizeof(...)" history comments
- include/xparam.h:12-29 | 18 PARAM_* constexpr never used? grep
- include/xparam.h:1-34 | no include guard (header) - trivial
- src/core/xparam.cpp:12-13 | params_ arrays non-static non-const globals
- src/core/xparam.cpp:117-180 | ResetDeckardXParams caller? ApplyDeckardXParam caller? grep
- src/core/xparam.cpp:79-80,109-110,225-226 and 97-98,112-113 | repeated memcpy+SifLoadModule pairs (small)
- include/vmcsaves.h:4 | comment refers to vmcsaves.c (file is .cpp) - trivial
- include/vmcsaves.h API: check callers of each of the 12 functions
- vmcsaves.cpp:427-434 vs 364-372 | vsReadIn inner loop re-implements vsReadChain -> call it
- vmcsaves.cpp:643 vs 767-770 | vsLoadFolder name check duplicates vsNameOk
- vmcsaves.cpp:145-169 | vsReadAll/vsWriteAll near-identical (trivial)
- vmcsaves.cpp:1123 | comment says ~2.5 KB, struct is ~3.9 KB (64*60+56) trivial
