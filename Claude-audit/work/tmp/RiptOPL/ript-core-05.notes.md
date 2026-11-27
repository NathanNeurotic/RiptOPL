# ript-core-05 notes (lens structure)

Files done (all read in full, re-read on second attempt): ee_core/src/patches.c 1-1002, patches_asm.S 1-59, ra.c 1-399, ra_overlay.c 1-420, ra_pulse.c 1-126, syshook.c 1-284, tlb.c 1-148, util.c 1-198

Verified (becoming findings):
- patches.c 555-575/676-694/838-856 three IOP patch loaders; CopyToIop exists util.c:132 (waits for DMA)
- patches.c NIBBLE2CHAR bug; iremsndpatch main.c:65 strtol base 16
- patches.c SOS PAL no-op (114, 535-540, 592-594), commented pSifLoadModule 798..828, DotHack dup branches 438-445, Eutechnyx dup 728-758
- patches.c:263 SyncDCache wrong arg (INFERRED harmless)
- ra.c RA_PROBE never defined by builds (ra.h:7-11; iopmgr.c 238-247, 342)
- ra_overlay/ra_pulse: two 128-byte event buffers, only one used per launch; duplicate validation
- ra_overlay small dead bits: tracked_display pmode NULL, 232 recheck, pixel_x/y save-restore
- ra_overlay struct copy of GSMSourceGSRegs (igs_api.h:126-142)
- syshook OSD field copies -> struct assignment (also system.cpp:1591-1598)
- tlb.c TLBInfo indirection, kprintf, stdio, write-only NumWiredEntries
- util.c unused externs 15-21, //foo 190; syshook 118-119 commented line
Dropped: x31 lead (parse of 0x31), RA_EXPERIMENTAL_CARD_DMA (documented dev switch with test), syshook globals (all used by asm.S/iopmgr.c)
