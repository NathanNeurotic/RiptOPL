## Files that no other file mentions by name (files-unreferenced.tsv)
- tools/pc/ziso.py  (420 lines; mentioned in 1 docs)

## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- tools/hwtest/smb2probe/probe_rpc.h:9  macro `PROBE_REPLY_WORDS`  (no-other-mention)
- tools/pc/http/tests/stub/ps2ip.h:17  macro `SHUT_RDWR_STUB`  (no-other-mention)
- tools/pc/opl2iso/src/opl2iso.c:193  function `exportGame`  (only-prototype-or-export-mentions) | also mentioned: tools/pc/opl2iso/src/opl2iso.c:296 return exportGame(argv[1]);
- tools/pc/opl2iso/src/opl2iso.c:80  function `listGames`  (only-prototype-or-export-mentions) | also mentioned: tools/pc/opl2iso/src/opl2iso.c:294 return listGames();
- tools/pc/ziso.py:369  function `load_sector_table`  (no-other-mention)
- tools/pc/ziso.py:44  function `hexdump`  (no-other-mention)

## Comment blocks that look like code (cond-commented-code.tsv)
- tools/pc/genvmc/src/genvmc.c:408-411  4 code-like lines
- tools/pc/iso2opl/src/iso2opl.c:169-179  6 code-like lines
- tools/pc/iso2opl/src/iso2opl.c:225-271  34 code-like lines
- tools/pc/iso2opl/src/iso2opl.c:95-112  16 code-like lines
- tools/pc/iso2opl/src/isofs.c:1051-1053  3 code-like lines
- tools/pc/iso2opl/src/isofs.c:172-179  5 code-like lines

## Byte-identical files (dupes-exact-files.tsv)
- tools/pc/OPL-PS1-AIO-Converter-GUI.url is byte-identical to: RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/OPL-PS1-AIO-Converter-GUI.url
- tools/pc/OrbitPS2-Manager.url is byte-identical to: RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/OrbitPS2-Manager.url
- tools/pc/PS2-Servers.url is byte-identical to: RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/PS2-Servers.url
- tools/pc/PS2RD-CHT-Manager.url is byte-identical to: RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/PS2RD-CHT-Manager.url
- tools/pc/udpfs-server.url is byte-identical to: RiptOPL/ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP/android-udpfs-server.url

## Largely identical files (dupes-file-pairs.tsv)
- tools/pc/genvmc/src/genvmc.c is 37% the same as RiptOPL/modules/vmc/genvmc/genvmc.c (104 cloned normalised lines)
- tools/pc/genvmc/src/genvmc.c is 9% the same as Launchstation/iop/vmcman/src/mcman-internal.h (28 cloned normalised lines)
- tools/pc/iso2opl/src/iso2opl.c is 3% the same as RiptOPL/tools/pc/opl2iso/src/opl2iso.c (14 cloned normalised lines)
- tools/pc/opl2iso/src/opl2iso.c is 8% the same as RiptOPL/tools/pc/iso2opl/src/iso2opl.c (14 cloned normalised lines)

## Duplicate blocks (dupes-blocks.tsv)
- tools/pc/genvmc/src/genvmc.c:199-212  ==  RiptOPL/modules/vmc/genvmc/genvmc.c:356-369  (12 normalised lines)
- tools/pc/genvmc/src/genvmc.c:220-243  ==  RiptOPL/modules/vmc/genvmc/genvmc.c:381-404  (14 normalised lines)
- tools/pc/genvmc/src/genvmc.c:296-315  ==  RiptOPL/modules/vmc/genvmc/genvmc.c:468-487  (16 normalised lines)
- tools/pc/genvmc/src/genvmc.c:316-357  ==  RiptOPL/modules/vmc/genvmc/genvmc.c:491-532  (31 normalised lines)
- tools/pc/genvmc/src/genvmc.c:37-67  ==  RiptOPL/modules/vmc/genvmc/genvmc.c:89-119  (31 normalised lines)
- tools/pc/genvmc/src/genvmc.c:40-67  ==  Launchstation/iop/vmcman/src/mcman-internal.h:300-327  (28 normalised lines)
- tools/pc/iso2opl/src/iso2opl.c:48-70  ==  RiptOPL/tools/pc/opl2iso/src/opl2iso.c:43-65  (14 normalised lines)
- tools/pc/opl2iso/src/opl2iso.c:43-65  ==  RiptOPL/tools/pc/iso2opl/src/iso2opl.c:48-70  (14 normalised lines)
