# ript-core-33.b notes (lens defects)

## Done
- src/sources/udpfssupport.cpp 1-1280: read fully
- include/vcdsupport.h 1-366: read fully

## Candidates (to verify)
- src/sources/udpfssupport.cpp:382-455 | theme/language discovery never latches when THM or LNG is absent (read-only share), so updateDelay stays GENREFRESH and root is listed twice every poll for the whole session | check sbCreateFolders folder list, how updateDelay is consumed, which thread runs needsUpdate
- src/sources/udpfssupport.cpp:575-638 | needsUpdate does stat RPCs (CD, DVD) each poll | check thread and cadence
- src/sources/udpfssupport.cpp:158-183 | udpfsEnsureReady polls stat with 100 ms sleeps up to timeoutMs | check callers and thread
- src/sources/udpfssupport.cpp:117-121 | sysShutdownDev9 after failed sysInitDev9 | check refcount semantics in system.cpp
- src/sources/udpfssupport.cpp:1103 | sbPrepare on smb_cdvdman_irx as scratch; passed without & while 1113/1120 use &; Lite UDPFS build may stub smb_cdvdman_irx to size 0 -> launch fails | check extern_irx.h declaration, lite_devices.py, sbPrepare
- src/sources/udpfssupport.cpp:1106 | sbPrepare result overwritten by sbGetCompatModes | other lens (overengineering) unless Lite bug holds
- src/sources/udpfssupport.cpp:941-954 | ISO opened twice (size, logo) then more opens in sbGetISO9660MaxLBA/sbProbeISO9660 | check helper bodies; small
- src/sources/udpfssupport.cpp:977,1134 | strcpy(filename[32], game->startup) | check startup field size
- src/sources/udpfssupport.cpp:1070-1073 | last_played config saved before validation; card write even when launch aborts or value unchanged | check saveConfig modified flag
- src/sources/udpfssupport.cpp:796-798 | vcdInstallPopstarterMc and vcdInstallPopsUdpfsNet on every VCD launch | check they skip existing files
- include/vcdsupport.h:19-32 | vcd_entry_t 260 bytes x VCD_MAX_ITEMS 2048 | check allocation strategy in vcdsupport.cpp
- src/sources/udpfssupport.cpp:1019-1024 | mmce_mcemu_irx slot patched before possible abort at slot 1 | check stale slot effect on later MMCE launch
