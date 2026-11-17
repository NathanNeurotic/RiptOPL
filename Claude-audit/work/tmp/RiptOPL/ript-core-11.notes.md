# ript-core-11 notes

## Done
- all 8 files read in full (config.cpp re-read in attempt 2)
- verification greps done; writing findings file

## Findings to write
1 gsm.cpp:95,199-211 + pggsm.h:64 + host.cpp:202-217 | PrepareGSM cmdline unused, config always non-NULL, field copy of identical struct
2 config.cpp:22-23,630-680,702-705,1228-1234,1269-1298 | configInit/configSetMove copy, configMove + LegacyOplPath forwarders
3 config.cpp:17-18,792-793,918-920,1536 | stale comments
4 config.cpp:1117-1118,1284-1285,1322-1323,1639 | redundant re-checks
5 gsm.cpp:73-86 | two identical GSM read branches
6 lang.cpp:55-83,208-216 | font path built twice via 2 mallocs, ttf/otf copy
7 dir+name join repeated: config.cpp:618-628, diagsave.cpp:30-31, lang.cpp:372-376, 431-435, fmJoin filemgr.cpp:102-112

## Dropped
- pad.h all decls used; #define vs constexpr mix is style
- lang.h all used; filemgr.h all used (fmNameOk/fmTreeSize used internally + host test)
- DT_DIR lead: DT_DIR used unconditionally in lang.cpp:255 etc; guard harmless, 2 lines, skipped
- VMC key snprintf x6, mc? wildcard x3: helper would not remove code
