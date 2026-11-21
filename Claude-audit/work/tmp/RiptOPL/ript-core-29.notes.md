# ript-core-29 notes

## Done
- src/sources/httpsupport.cpp 1-924 (read full)
- include/mmcesupport.h 1-44 (read full)
- src/sources/mmcesupport.cpp 1-1855 (read full)
- include/netsupport.h 1-108 (read full)
- src/sources/netsupport.cpp 1-501 (read full)
- include/nettime.h 1-21 (read full)

## Candidates
- src/sources/httpsupport.cpp:182-185 | if/else with identical branches | trivial, group
- src/sources/httpsupport.cpp:385-396 | clearCatalog duplicates setCatalog(NULL,NULL,0); NULL checks before free | check callers
- src/sources/httpsupport.cpp:421-428 | httpTestServer builds endpoint by hand (same as setEndpoint) | minor
- src/sources/httpsupport.cpp:469-498 | probe loop duplicates httpProbeRead (676-697); static probe[2048] bss | check order/defs
- src/sources/httpsupport.cpp:52 | httpLastError written many places; is it ever read? | grep
- src/sources/httpsupport.cpp:11-33 | includes possibly unused (cheatman, themes, renderman, libview, util) | low value
- src/sources/httpsupport.cpp:915-924 | initOnly param unused | check other GetObject fns
- src/sources/httpsupport.cpp:660-671,898-912 | inert overrides - does GameSource base have defaults? | read gamesource.h
- src/sources/httpsupport.cpp:510-516 | ctor sets enabled = MODE_FLAG_NO_UPDATE oddity | check GameSource
- src/sources/httpsupport.cpp:861-877 vs mmcesupport.cpp:650-663 | same art path composition; other sources too? shared helper exists? | grep texDiscoverArt
- src/sources/mmcesupport.cpp:176-195 vs 562-572 | candidate slot enumeration duplicated (sendGameID / resetCards); mmceSlotOf helper at 98 not used |
- src/sources/mmcesupport.cpp:258-265 vs 279-286 | duplicated wait-ready/return block inside sendGameID |
- src/sources/mmcesupport.cpp:1305-1310,1339,1731-1741 | gAutoLaunchBDMGame branches "No Autolaunch yet" + commented-out block | grep who sets gAutoLaunchBDMGame
- src/sources/mmcesupport.cpp:1801-1802,1818-1819 | commented-out code; usbhdfsd-common.h include (28) only for commented line | grep USBMASS in file
- src/sources/mmcesupport.cpp:1796-1799 vs 1812-1815 vs 1037-1044 | free games/ps1Games repeated 3x |
- src/sources/mmcesupport.cpp:630-648,650-663,1766-1774 | artPrimary always copy of prefix; mmceTryLoadImage single caller | read mmceint.h
- src/sources/mmcesupport.cpp:352-355,516-519,592-595,759-767,786-789,42-45 | C forwarders to gMmceSupport methods | read mmceint.h
- src/sources/mmcesupport.cpp:981-982,1372-1373,1553,1646-1655,1712-1721 | stale/contradictory comments |
- src/sources/mmcesupport.cpp:1560-1590 | launch re-runs detectSlot | maybe skip
- include/mmcesupport.h:9-15 | mmce_vmc_infos_t used elsewhere? | grep
- leads: 798-810 == hddsupport 322-334 is override decl list: drop
- netsupport.cpp:41-44,160-168,244-247,307-310,330-333,349-352,399-402,497-500 + netsupport.h:61-106 | class+forwarder double API; who uses gNetSupport directly? | grep
- netsupport.cpp:99-107 | waitValidLinkState/DhcpState one-line wrappers | trivial
- netsupport.cpp:27-28 | needless forward decl of NetStatusCheckCb (defined at 58 before use 82) | trivial
- netsupport.cpp:1-8, netsupport.h:12 | stale history comments (ethsupport.c, httpsupport.c) | trivial
- netsupport.h:52 netGetConfig, :30 netGetResidentProtocol, :48 netApplyConfig | used? | grep
- nettime.h | netTimeParseDate/netTimeFetch defined/used? | grep
