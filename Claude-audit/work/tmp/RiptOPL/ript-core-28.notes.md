# ript-core-28 notes (structure lens)

## Done so far
- src/sources/hddsupport.cpp lines 1-3383 read (complete, re-read in second attempt)
- include/httpsupport.h lines 1-91 read (complete)
- Candidates verified by grep; writing findings JSON next

## Verified findings (to write)
1 forwarders 3232-3303 + hddint.h methods (cat3, rank2)
2 last_played block x9 across sources (cat2, rank2)
3 hddPartitionMountable == ...At; redundant probe 1328-1332 (rank1)
4 redundant guard 1018 after return 985 (rank1)
5 3x paired realloc in buildVcdGameList (rank1)
6 renameVcd duplicate mount branches (rank1)
7 __common POPS borrowing x3 + misplaced comment 2071-2078 (rank1)
8 games.bin cache helpers return codes unused (rank1)
9 pfsDiagHddCheckResult/Ps2fsResult only feed LOG (empty in release) (rank1)
10 cleanUp duplicated free block (rank1)
11 hddInit prototype no definition, stale comments 2971/3039 (rank1)
12 httpsupport.h included unused in 4 ui files (rank1)
13 two sector reads detect/hybrid duplicate (rank1)
14 hddDvrScan root copy/scanned/double gEnableDVR (rank1)
15 changelog narrative comments + 361-364 dup of hddint.h 98-101 (rank1)

## Dropped
- RIPTOPLITE lead: defined by tools/lite_devices.py:106 -> live
- exported functions all have callers
- HttpSupport members all used (in httpsupport.cpp)
