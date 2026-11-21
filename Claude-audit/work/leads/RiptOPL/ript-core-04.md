## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- ee_core/src/main.c:33  function `_ps2sdk_timezone_update`  (no-other-mention)
- ee_core/src/modmgr.c:269  function `ListModules`  (only-prototype-or-export-mentions) | also mentioned: ee_core/include/modmgr.h:95 void ListModules(void);

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `IGR_MENU`  tested at 29 site(s) in the repo, mentioned in 9 build file(s); here: ee_core/src/iopmgr.c:19
- `IGR_OVERLAY`  tested at 7 site(s) in the repo, mentioned in 3 build file(s); here: ee_core/src/padhook.c:514
- `INGAMEDIAG`  tested at 38 site(s) in the repo, mentioned in 6 build file(s); here: ee_core/src/iopmgr.c:23, ee_core/src/iopmgr.c:470, ee_core/src/modmgr.c:206, ee_core/src/padhook.c:47
