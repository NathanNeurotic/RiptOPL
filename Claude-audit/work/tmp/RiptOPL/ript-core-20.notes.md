# ript-core-20 notes
Done reading: all 25 files fully.

Candidates:
- src/platform/debug.cpp:20-34 | __DECI2_DEBUG branch never defined | check build files
- src/platform/elfldr_noreset.cpp:150-262 | sysLoadELF/sysLoadELFApp/KeepIOP forwarding wrappers; partition param unused | check callers
- include/launchdiag.h:41 LAUNCH_DIAG 0 hard-coded; launchdiag.cpp whole file dead; call sites | grep LAUNCH_DIAG, gLaunchDiag users, elfldr/loader.c stage 12/13
- src/platform/httpclient.cpp:157-161 HttpStreamEnd empty | callers
- httpclient HttpSendGetRequest/HttpEstabConnection users
- include/ioman.h:8 IO_ERR_UNKNOWN_REQUEST_TYPE unused; :38-40 orphan doc comment
- src/platform/ioman.cpp:16-39 stale banner about removed constant
- IoQueue::total / totalByType_ unused? ; ioGetPendingRequestCount callers; ioPrintf return
- IoQueue::process null check, stale comment
- hdd.cpp: hddIs48bit, hddSetTransferMode, hddGetSpace, hddFlushCache, hddGetPartitionInfo, hddGetFileBlockInfo users
- ioprp.cpp Align_offsets NULL checks always non-null (upstream)
- md5.c/h: users? other md5 impls in repo (mbedtls, rcheevos)?
- nbns.h:20-25 IOP import macros unused
- ps2cnf.cpp:62 unreachable branch; users
- ratls.h:60-66 misplaced comment; raTlsRtcToUtc/raTlsClockOk/ratlsNicBusy users
- new.cpp operator new leads: false (global operators replace libstdc++)

Status: findings written and validated (10 findings). md5/new.cpp/diaglog/hdd/ratls checked: no finding.
