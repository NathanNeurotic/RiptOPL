# ript-core-05.b notes (lens defects)

## Done (all 8 files read fully in attempt 2)
- patches.c, patches_asm.S, ra.c, ra_overlay.c, ra_pulse.c, syshook.c, tlb.c (lead x31 = literal 0x31, false), util.c

## Verified -> findings
- patches.c:160,565-566 NIBBLE2CHAR; iremsndpatch/main.c:65 strtol base 16 -> wrong module patched (rank 3)
- patches.c:408 ZZ scan 0x1c0000+0x1f00000 = 0x20c0000 > 32MB; tlb.c:84-92 mapping ends 0x1FFFFFF (rank 2)
- util.c:96-100 ETHOpMode unchecked; settingsio.cpp:1924 no clamp; system.cpp:1561; netsupport.cpp:408-423 default auto (rank 2)
- patches.c:263 SyncDCache(value) + smem_write is CPU copy (libkernel smem.o, eecore-archive-members.txt:20) (rank 1)
- ra_pulse.c:26-28,111-123 PMODE EN1-only forced and left; ra_overlay.c:185-210 handles both; main.c:127-152 tracker (rank 2, Low)

## Dropped
- ProSnowboarder overscan (stays in RAM), &path[8], mode mapping (design), ra.c bounds (validated by raWatchEntriesValid, RA_EE_WORK_BYTES), ra_overlay buffers (sizes ok, 64-aligned via host.cpp:129,178), Install_IGR idempotent, ra_snap_iop cleared on reset (iopmgr.c:408), BIK char pattern alignment (aligned by layout in practice), WipeUserMemory alignment (_end 0x97000)

## Status: findings JSON written, validator OK (5 findings)
