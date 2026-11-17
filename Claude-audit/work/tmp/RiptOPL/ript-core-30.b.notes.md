# ript-core-30.b notes (lens defects)

Done: all 13 files read fully (nettime.cpp, rabadge.h/.cpp, rahash.h/.cpp, rahunt.h/.cpp, ranet.h/.cpp, rapopslaunch.h/.cpp, ratls_ps2.cpp, rawatch.h).

Checked and dropped:
- rabadge race UI vs IO: dropped, raBadgeText only called on IO thread (opl.cpp:800, cfgtitle.cpp:149 via opl.cpp:842); rows get strdup copies.
- hddReadSectors 2 sectors into header: header is 1024 bytes (hdl_layout.h:32 static assert). dropped.
- askMstack memcpy: devctl returns payload length (test_ministack_xchg.c:194), data 1016. dropped.
- rapops ipcfg: RAPOPS_IPCFG_MAX 64 >= 48. dropped.
- rahunt list 237 KB is transient malloc in supportbase.cpp:2750. dropped.
- nettime/ratls/httpGet blocking connect: not demonstrable, dropped.
- findEntry huge dir_size: theoretical, dropped.

Findings to write:
1. rabadge cache count*168 B held per list, only read during the same rebuild (rank 3, cat 4)
2. rabadge 2 stats per row per rebuild incl smb0: (rank 2, cat 5)
3. ranet rx_ 2048 but reads capped 992; netTestLink static rx[256] separate (rank 2, cat 4)
4. netTestLink absolute clock deadline + ms overflow (rank 1, cat 6)
5. askPc RAG1 chunks stay broadcast, any responder accepted (rank 1, cat 6)
