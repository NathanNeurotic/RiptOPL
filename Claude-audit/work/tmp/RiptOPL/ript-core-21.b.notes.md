# ript-core-21.b notes (lens defects)

## Done (read in full) -- second attempt re-read all five files in full
- src/platform/ratls.c 1-457
- src/platform/selfupdate.c 1-140
- src/platform/sysdisc.cpp 1-311
- src/platform/sysneutrino.cpp 1-1466
- include/system.h 1-150

## Verified -> findings
- sysdisc.cpp:158 + system.cpp:498 | sceCdStatus() == SCECdErOPENS never true (status vs error code; scmd.c:60-68,115-119) | KEEP rank 2
- sysdisc.cpp:134,185-188,197 | absolute clock() deadlines not wrap-safe (repo says 32-bit us counter: ratls_ps2.cpp:167, guiwait.cpp:97-98) | KEEP rank 1
- sysneutrino.cpp:384-385,639-640 | 16,916 B .bss MEASURED (nm), two pairs never live together | KEEP rank 2

## Dropped
- getDeviceName udp post-teardown: udpbdLoadedProtocol_ never reset (bdmsupport.cpp:739, bdmsupportcore.h:21,41)
- cheats snprintf overflow: startup <= 12 (GAME_STARTUP_MAX), fixed text < 512
- ratls spin: bioSend/bioRecv sleep + deadline (ratls_ps2.cpp:174-211)
- want*10 overflow: max_body <= 8 MB (selfupdate.cpp:82-83)
- ratls concurrency/stack: both callers on IO worker (96 KB stack)
- disc key read waste: cost unknown, possible PS2LOGO dependency unknown
- keep-IOP failure post-teardown: preflight runs same budget checks
- -Wwrite-strings: warning only, not a defect
