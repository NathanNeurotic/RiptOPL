# ript-core-22.b notes (lens: defects)

## Done
- src/platform/system.cpp 1-1689 (read fully; re-read in attempt 2)
- src/platform/sysvmc.cpp 1-285 (read fully)
- include/tar.h 1-102 (read fully)
- src/platform/tar.cpp 1-715 (read fully)

## Verified candidates -> findings
- tar.cpp:609-613 + art worker (artload.cpp:98-136,659-666, prio 72) | tarInvalidate on GUI/IO thread frees ART index under art worker; truncated sidecar persisted; s_dev cleared | VERIFIED callers settingsio.cpp:2479-2483, guisettings.cpp:1801-1804, no hold/drain around applyConfig
- tar.cpp:389-424,467-477,504-514 | HDD exact and generic share TAR_KIND_ART slot; FAV/APP mixed lists reload index each alternation | VERIFIED texcache.cpp:187-207
- tar.cpp:293-311,363 | extra lseek RPC per member + linear realloc growth on cold scan | INFERRED cost
- system.cpp:1577-1582 | %d ps2_ip into char[16]; ps2_ip unvalidated from config (settingsio.cpp:1950, config.cpp:752) | bounded to 48 B in the 3 fields; rank 1
- system.cpp:465 crctab 4096 B .bss (nm 0x1000) only 1 KB used; tar.cpp:73 s_zeroBlock 512 B rodata (nm 0x200)
- tar.h:56,62 + tar.cpp:254-359 | CFG/CHT filename[20] vs nameMax 23; tarReadCache names not re-terminated (tar.cpp:167-171)
- system.cpp:1435-1447 + host.cpp:132-146 | module storage end never checked vs 0xD0000 (linkfile); RA work area bounded only at 1 MB, cheats use 0xD0000 | Low confidence, INFERRED sizes

## Dropped
- ioprp malloc fail (system.cpp:839-852), sysGetDiscID blocking (user-initiated, guigame.cpp:1826), dev9 retry loop, module table full order, sysPlaceIgrOverlay trusted blob, sysvmc.cpp (nothing in lens), USBA_crc32 33-byte read (name buffers 161 B, UL names terminated at 32)
