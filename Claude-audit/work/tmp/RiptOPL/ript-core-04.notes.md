# ript-core-04 notes (structure lens)

## Done
- ee_core/src/igs_api.c 1-790 (read full)
- ee_core/src/iopmgr.c 1-552 (read full)
- ee_core/src/main.c 1-185 (read full)
- ee_core/src/modmgr.c 1-285 (read full)
- ee_core/src/padhook.c 1-1110 (read full)
- DONE: findings written and validated OK (12 findings)

## Candidates
- igs_api.c:79-152 | five number-to-string functions, three decimal ones identical bodies, two hex identical bodies | check IGS build flag (extras only), check util.c for existing helper
- igs_api.c:71-77 | ClearBuffer hand-rolled memset | memset available in ee_core?
- igs_api.c:168 | Screenshot params dimensions, pixel_size unused | confirm
- igs_api.c:63 | commented-out workaround line | trivial
- igs_api.c:754-757 | CT16S case duplicates CT16 case | trivial merge
- iopmgr.c:51-55 | RA_TelemetryWanted takes config and ignores it; wrapper for RA_SnapBytes()>0 | 4 callers
- iopmgr.c:59-68 | RA_Hex32 vs igs u32tohexstr / other hex helpers in ee_core | grep
- iopmgr.c:167-171 | `|| 1` under __LOAD_DEBUG_MODULES
- iopmgr.c:183-185 | __DECI2_DEBUG branch: no build defines (brief q5) | check ps2.yaml, tools
- iopmgr.c:189-191 | TTY_PPC branch | check build defines TTY_PPC vs TTY_PPC_UART
- iopmgr.c:244-246, 342 | RA_PROBE ladder (lab) | where defined, always 0?
- iopmgr.c:275-280, 301-302 | empty switch cases | trivial
- iopmgr.c:264-273 | ETH/HTTP duplicate SMSTCPIP+SMAP load | small
- iopmgr.c:389-394, 408, 474-477 | ra_snap_iop = 0 redundant
- iopmgr.c:32 vs 416 | _iop_reboot_count extern vs iop_reboot_count
- main.c:33 | _ps2sdk_timezone_update lead: weak override, not dead
- main.c:40 | eecoreInit argc/argv unused, always returns 0
- modmgr.c:92,103-104 | commented-out code lines
- modmgr.c:269-285 | ListModules under __EESIO_DEBUG, lead says no callers | grep
- modmgr.c:239-263 | LoadElf | callers?
- padhook.c:303,305-306,371-378,409-413 | IGR_CheckInputs runs only in VBLANK handler (699) but keeps DI/EI + ee_kmode_enter/exit and comments from removed polling worker | check ee_kmode_enter def
- padhook.c:1022-1028 vs 1063-1069 | hook address chosen twice; libpad/libversion set twice (1034-1035, 1075-1076)
- padhook.c:1087 | commented-out line
- padhook.c:853-857 Reset_Padhook, 757 Invalidate_PadBuffer, 673 IGR_Exit | callers?
