# ript-core-11.b notes (lens defects)

## Done (all assigned lines read)
- src/core/config.cpp 1-1740 (read fully; re-read 1-130, 200-430, 560-730, 1340-1740 for verification)
- src/core/diagsave.cpp, include/filemgr.h, src/core/filemgr.cpp, src/core/gsm.cpp, include/lang.h, src/core/lang.cpp, include/pad.h

## Verified -> findings
1. filemgr.cpp:453-466,564-573 cleanup after FM_ERR_EXISTS deletes pre-existing target (mc swap trigger) - rank 3
2. configset.h:13-20 fixed 308 B entries - rank 3 INFERRED
3. config.cpp:1673-1685 restore check vs old size, not intended size - rank 2
4. diagsave.cpp:17-21 static 16 KB copy - rank 2
5. filemgr.cpp:225-248, 468-477 relisting + per-remove probes - rank 2
6. lang.cpp:550-556 lngGetFilePath(0) from guinotify.cpp:135 - rank 1

## Dropped
- config.cpp:1511-1517 (checkFile rewrites mc? in place, so filename is concrete after first open)
- config.cpp init() leak (all configInit calls preceded by configEnd)
- configOpenSingleShot (openFileBufferBuffer copies)
- lang boot rescans (few calls), calloc unchecked, lngEnd (terminal), pad.h ok, gsm ok, diagCopy non-negative

## Status: writing findings JSON
