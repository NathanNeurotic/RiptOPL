# ript-module-02 notes
Done: modules/bdm/src/* (12 files); bdmassault LICENSE, Makefile, include/bdm.h, IEEE1394_bd/src/* (all), src/exports.tab, iLinkman README, iLinkman.h, iLinkman/src/exports.tab, iLink.c, iLink_DMA.c, iLink_PHY.c, iLink_TxRx.c
Remaining: iLink_crom.c, iLink_internal.c, iLink_intr.c
Candidates:
- bdm part_driver_gpt.c:68-74 entriesPerSector<=0 unreachable (bdm.c:169-180 refuses !=512) | blame
- bdm part_driver_mbr.c:12-33 valid always 4 | upstream rank1
- bdm gpt_types.h:44,56,58,60-68 unused | upstream rank1
- bdm part_driver.c:8-9 unused includes
- BIG: ilink_bd_assault/src/{IEEE1394_bd,iLinkman} near-copy of modules/ilink (diffs: sbp2 57 lines (missing sbp2FreeDevNr devNr fix), scsi.c older (no READ16), main.c _start rename+IRX_ID commented, iLinkman small). bdm.h differs (no path field) -> needs ifdef. ORIGIN says "change to one belongs in both". 
- Unused in Makefile build: IEEE1394_bd/src/imports.lst (lacks PollEventFlag which sbp2 uses -> proof), IEEE1394_bd/src/irx_imports.h, iLinkman/src/exports.tab, iLinkman/src/imports.lst, iLinkman/src/irx_imports.h (Makefile IOP_SRC_DIR=src/ uses src/imports.lst, src/exports.tab, src/irx_imports.h). No .c includes irx_imports.h. tests reference dirs - check tests don't read those files.
- sbp2_driver.c:775-780 DeinitIEEE1394 + iLinkIntrCBThreadID global: no caller (check)
- sbp2_driver.c:45 commented-out; :13 usbhdfsd-common.h include (where?)
- sbp2_driver.c:619-628 SendManagementORB mode always LOGIN
- sbp2_get_max_lun / get_max_lun field / scsi.c:170-171 only logs | check ilink copy too
- scsi.c:238-290 scsi_read/scsi_write identical except flag (both copies)
- scsi.c:28-46 elapsed time code dup of sbp2ElapsedUs sbp2_driver.c:143-155 (both copies)
- scsi.c:17-19,186 SCSI_READY_TRIES redundant w/ 30s budget
- src/exports.tab: combined irx registers iLinkman lib exports nobody imports under POPSTARTER -> keeps unused funcs (INFERRED gc-sections)
- iLink_DMA.c:62,68 ByteSwap unused param; iLink_TxRx.c:150-152 commented code | upstream rank1
