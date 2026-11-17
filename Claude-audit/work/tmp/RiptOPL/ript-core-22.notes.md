# ript-core-22 notes

Done: src/platform/system.cpp 1-1689 (full); src/platform/sysvmc.cpp 1-285 (full); include/tar.h 1-102 (full); src/platform/tar.cpp 1-715 (full)
ALL READING DONE. Now verifying.

Candidates (system.cpp):
- system.cpp:13-25 | SYS_DISC_* constants + time.h include unused here (sysLaunchDisc lives in sysdisc.cpp) | grep names repo-wide
- system.cpp:69-76 | createVMCparam_t typedef unused in system.cpp | check sysvmc.cpp copy
- system.cpp:99 | ELF_MAGIC unused; :1319,1331 use literal 1 instead of ELF_PT_LOAD | grep
- system.cpp:132-135 | guiEnd/menuEnd/thmEnd/rmEnd prototypes unused | grep definitions
- system.cpp:26-57 | include block copied to sysdisc/sysvmc/sysneutrino; several unused here | spot-check
- system.cpp:755 | GetModStorageLocation compatFlags param unused | only caller 1398
- system.cpp:9-11,316-319,993-999,1025-1037,1042-1044,1068-1122,1191-1193,1400-1401 | __DECI2_DEBUG blocks; no build defines | grep build files
- system.cpp:465 | crctab[0x400] only 256 used; rebuilt per call | check USBA_crc32 callers
- system.cpp:857-1013 | ~35 repeated info/ptr pairs; 917-934 UDPFS dup | helper would cut lines
- system.cpp:705-741 | patchlist mode field always "" | trivial
- system.cpp:792-793 | BDM_ATA_MODE branch same as default else | trivial
- system.cpp:1218,1224 | enforceLanguage logged twice | trivial
- check callers: sysGetDiscID, sysInitDECI2, sysIsPccfOwned, sysTryClaimPccf, sysReleasePccf, USBA_crc32, sysLaunchEmber, sysLaunchPopstarter

Candidates (sysvmc.cpp):
- sysvmc.cpp:7-23 | DECI2 include, delaythread/time.h, SYS_DISC_* constants copied, unused here | grep in file
- sysvmc.cpp:24-59 | most includes unused | spot check
- sysvmc.cpp:168-185 vs 188-198 | two copies of the superblock read loop + validation | merge
- sysvmc.cpp:281-284 | sysCheckVMC thin wrapper | count callers

Candidates (tar):
- tar.cpp:467-574 | tarFind/tarFindExact and tarFindPrefix/tarFindPrefixExact duplicate loops | merge into 2 static helpers
- tar.cpp:460-465 vs 609-613 | tarClose == tarInvalidate | callers
- tar.h:56-68, tar.cpp:61-62 | CFG/CHT kinds wired? | grep TAR_KIND_CFG/CHT
- tarGetDevicePrefix, tarThemePath, tarFindPrefixExact callers
- tar.cpp:234-237 dup of 243-246 reset | trivial
