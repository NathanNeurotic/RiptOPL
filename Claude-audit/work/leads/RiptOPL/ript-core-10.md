## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- include/OSDHistory.h:83  function `osdHistoryGetYear`  (no-other-mention)
- include/OSDHistory.h:84  function `osdHistoryGetMonth`  (no-other-mention)
- include/OSDHistory.h:85  function `osdHistoryGetDate`  (no-other-mention)
- include/config.h:114  macro `CONFIG_OPL_ENABLE_ANALOG_NAV`  (no-other-mention)

## Comment blocks that look like code (cond-commented-code.tsv)
- include/OSDHistory.h:3-81  39 code-like lines
- src/core/OSDHistory.cpp:217-223  4 code-like lines

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `__cplusplus`  tested at 190 site(s) in the repo, mentioned in 0 build file(s) [builtin-like]; here: include/config.h:279, include/config.h:317

## TODO/FIXME/HACK style markers (cond-markers.tsv)
- src/core/bgm.cpp:890  // HACK: BGM stutters while perfroming certain tasks, mute during these operations and unmute once completed.
