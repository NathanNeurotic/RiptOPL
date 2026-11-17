# Notes ript-core-13.b (lens defects)

## Done
- src/core/vmc_groups.cpp 1-1474 (read fully, re-read in attempt 2)
- include/vmcsaves.h 1-55
- src/core/vmcsaves.cpp 1-1257
- include/xparam.h 1-34
- src/core/xparam.cpp 1-246 (no defect found; ResetDeckardXParams 18 module loads at boot: cost unverifiable, dropped)

## Findings to write (all verified)
1. vmcsaves name traversal (vsLoadVmc 453,469 -> vsWriteFolder 727,739-740), rank 3 INFERRED device '..' handling
2. vmcSavesDelete 64-cap leak, files[] unneeded (1181-1182,1193-1195,1202-1204), rank 2
3. vsIo per-page calls (173-184), rank 2
4. static s/pad 4924 B bss MEASURED (1123, 862), stack 0x20000, rank 2
5. vsLoadFolder double stat + list (636,645-647,660,667,702-705), rank 1
6. vmc_groups malformed ids + unreachable groups 100.72, 000.12, rank 2
7. vmc_groups pointer arrays 4184 B .data MEASURED -> char[][12], rank 3

## Next
- write findings/RiptOPL/ript-core-13.b.json and validate
