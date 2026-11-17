# ript-module-05 notes

Done: ds34usb/iop/Makefile, ds34usb.c, ds34usb.h, imports.lst, irx_imports.h, smsutils.S

Candidates:
- ds34usb.c:675-678,693-699,863-872,882,887,904-906,919-921 + ee/libds34usb.c:52-59,120-129 + .h:16,21 | RPC INIT and SET_LED never sent by EE (ds34usb_set_led/reinit_ports unused) | checked grep
- ds34usb.c:309-345 (DS3) and 347-443 (DS4) re-implement translate_pad_ds3/ds4 in modules/pademu/ds34common.c:44-144; ds34bt/iop/ds34bt.c:1342-1422 same | DS5 block 444-552 dup of DS4 | check test_ds34usb_report_transfer.py
- smsutils.S byte-identical in ds34usb/iop, ds34bt/iop, iopcore/cdvdman; ps2.yaml can point to one copy
- ds34usb.h:45-69 enum eHID: 13 of 15 values unused
- imports.lst:30 I_sceUsbdSetPrivateData unused
- irx_imports.h includes many unused headers; byte-identical with ds34bt
- ds34usb/iop/Makefile legacy (ps2.yaml:589-594 is build); DPRINTF comment line 14; Rumble/readReport prototypes 74,76 unneeded
- get_bdaddr DS5 branch 763-779 == DS3 branch except report id
- _start 954-955 sema=-1 then CreateMutex: redundant
- ps2hdd_osd defines: APA_OSD_VER APA_USE_DEV9 APA_USE_ATAD (ps2.yaml:293). Not BDM/IOMANX/GPT/XOSD/POSIX
- ORIGIN.txt item 4: refused code kept after early returns in hdd_fio.c for diffability
- test_apa_table_safety.py:217 checks legacy apa Makefile contains table_fence.o
