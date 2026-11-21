# ript-core-23 notes

Files read in full (all six, re-read in attempt 2): util.cpp 1-991, zso.c 1-95, achievements.h 1-51, achievements.cpp 1-797, appscan.cpp 1-248, appsupport.h 1-39.

## Verified (attempt 2)
- KEEP: appscan.cpp:7-68 include block copied (stale comments; same comments in menuitems/lwnbdctl/autolaunch/settingsio/opl)
- KEEP: util.cpp max() unused (util.h:46), htab unused, commented LOGs in readFileBuffer, redundant else 906-907
- KEEP: util.cpp checkMC four opendirs -> early returns
- KEEP: Xerabora 'A' Hardcore filter always empty (hardcore parsed only for 'G'; menusys.cpp:1238-1239 cycles 4 filters)
- KEEP: achievements.h exports 4 internal-only funcs (tests include source textually)
- KEEP: achievements.cpp small redundancies (597-599/625-627, 721 vs 728, inline hex vs fromHex)
- KEEP: appscan.cpp legacy lookups duplicate prefix loop + probe
- KEEP: oplGetAppImageByMode forwarder
- DROPPED: getmcID, fromHex, GetSystem*, CheckPS2Logo, delay, sanitizePrefix, openFileBufferBuffer, checkMCFolder all used; appsupport.h consts all used; zso.c nothing
- Next: write findings JSON and validate
