# ript-core-09 notes (src/app/settingsio.cpp, structure lens)

Done: lines 1-2621 read. Verification done; writing findings.

Confirmed:
- 239-249,255,856-865,1045-1052,1070-1075 | concrete-MC handling unreachable | confirmed (gBootDir writers, gBootHomeDeferred only smb/udpfs, 1012 early return; callers 1562,1918, opl.cpp:1894)
- 2052-2053,2058-2068 | trySaveAlternateDevice only called with empty gBootDir -> host: branch dead | confirmed (util.cpp:980-985)
- 1560,1777-1778,1916-1919,1976 | _loadConfig dead bits | confirmed (only caller opl.cpp:1719, lscstatus CONFIG_ALL)
- 88-118,1984-2001,348-354/368-372 | small dups | confirmed
- includes 13,15,20,24,25,29-32,36,40,41,44,46,50,52-54,56,58,61-62,68-71 | unused | hdrcheck script, Med
- 1373-1376,1383-1386,1405-1410 | stale comments | mmceLoadModules real at mmcesupport.cpp:733-762
- IP parse/format repeated | no existing helper; config.cpp:760-765 too
Dropped: join-separator helper (net ~5 lines, behaviour differs per site), bootHomeIsKnownBdm wrapper.
