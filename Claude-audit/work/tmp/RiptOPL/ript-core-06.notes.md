# ript-core-06 notes
Done: ALL 38 files read in full. Symbol sweeps done (tmp/RiptOPL/core06-counts*.txt).

Findings to write:
1 DECI2 debug mode never built (extern_irx.h:34-38,170-172; system.cpp 9-11,316-319,993-999,1025-1037,1042-1046,1068-1122,1191-1193,1400-1401; debug.cpp 20-33; opl.cpp 2056-2058; system.h 122-124; modules/debug blobs 91.6 KB) rank 3
2 loader.c simplifications (398-416 redundant argc test, 447-457/461-471 dup tail, 4x error cleanup 267-303) rank 2
3 pggsm.h GSM_ARGS + makeSMODE1/SYNCH1/SYNCH2 unused (20, 23-41) rank 2
4 MODE_FLAG_NO_UPDATE set never tested (iosupport.h:94; appsupport.cpp:598; favsupport.cpp:974; httpsupport.cpp:513-515) rank 1
5 LAUNCH_DIAG hard 0: loader.c 140-155,396,455,469 rank 1
6 RA_EXPERIMENTAL_CARD_DMA always 1 (ra_features.h:4-11 + 6 uses) rank 1
7 elfldr/Makefile legacy rank 1
8 disc_key.h static inline w/ static buffers in 2 TUs rank 1
9 stale comments: loader.c:17-30,140; fontsystem.h:6; ioqueue.h:5-6; iosupport.h:22-24
Dropped: _libcglue_*/_ps2sdk_fileXio_* leads (weak overrides), ds34common enums (partly used), class members (all used), Launchstation loader dup (cross-repo).
