# ript-core-29.b notes (lens defects)

## Done
- src/sources/httpsupport.cpp 1-924 DONE (re-read in attempt 2)
- include/mmcesupport.h 1-44 DONE
- src/sources/mmcesupport.cpp 1-1855 DONE
- include/netsupport.h 1-108 DONE
- src/sources/netsupport.cpp 1-501 DONE
- include/nettime.h 1-21 DONE

## Candidates
- httpsupport.cpp:165-166 | 2048-row arrays ~890 KB | KEEP F1 rank 4
- httpsupport.cpp:482 | static probe[2048] .bss MEASURED nm:362 0x800 | KEEP F2 rank 2
- mmcesupport.cpp:1399-1437 vs 1540-1550 | Game ID + history before cheats prompt | KEEP F3 rank 2
- netsupport.cpp:190-241 partial-failure latch returns 0 later | DROPPED (misreport only, INFERRED, unrealistic)
- netsupport.cpp:82 SetAlarm u16 overflow | DROPPED: hsync max 31 kHz -> 31000 fits
- netsupport.cpp:219 ps2ip_init unchecked after ps2ips load ignored | DROPPED (SDK code not verifiable, IOP OOM only)
- (dropped list from earlier: httpsupport 352, 421, 398, 197; mmce init leak; prefix[40])

## Next: write findings JSON and validate
