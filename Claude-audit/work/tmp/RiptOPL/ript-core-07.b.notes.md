# ript-core-07.b notes (lens defects)

Files done (all read_full): raii.h, renderer.h, selfupdate.h, sema_lock.h, settingshell.h, soundeffects.h, soundint.h,
sourceregistry.h, supportbaseint.h, texcacheint.h, themecatalog.h, themeint.h, utf8.h, util.h, autolaunch.cpp, host.h,
host.cpp, lwnbdctl.cpp, opl.h

Checked candidates:
- KEEP lwnbdctl.cpp:77-78,150,195-203,228-239 | delay() tick ~0.25s (util.cpp:941, opl.cpp:1269-1277 repo note); IO drain 1000 ticks ~250 s; pad retries delay(50) ~12.5 s x100. Art drain 500 is ms (artload.cpp:426-449) -> fine.
- KEEP autolaunch.cpp:203-249 | retry does delay(6)+delay(6) -> ~24 s not ~12 s documented. rank 1
- KEEP settingshell.h:70-73 | mmceDialog_ 256x40 B = 10 KB for <=45 rows (dialogs.cpp:816-942); nm gSettingsShell 0x50bc MEASURED. rank 2
- KEEP utf8.h:21-49 | two copies _ZL5utf8d 0x190 each in nm (fntsys.cpp, textshape.cpp). rank 1
- DROP gExportName strcpy: both 32, configGetStrCopy sizeof.
- DROP isValidIsoName OLD_ISO: size>=17 guarantees offsets; name[161].
- DROP argc: main gates argc>=5.
- DROP igrCheats stale: system.cpp:1455-1468 always calls igrCheatsPlace before fillCoreConfig.
- DROP raNetPeerIP byte order: network order consistent with supportbase/rapopslaunch.
- DROP sourceregistry module() unchecked: fav records with bad mode filtered by favResolve (favsupport.cpp:720,751,835).
- DROP wakeCount_ volatile: function-call barriers.
