## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- include/ioman.h:8  macro `IO_ERR_UNKNOWN_REQUEST_TYPE`  (no-other-mention)
- include/nbns.h:21  macro `nbnsman_IMPORTS_start`  (no-other-mention)
- include/nbns.h:22  macro `nbnsman_IMPORTS_end`  (no-other-mention)
- include/nbns.h:24  macro `I_nbnsFindName`  (no-other-mention)
- src/platform/new.cpp:10  function `operator new`  (no-other-mention)
- src/platform/new.cpp:18  function `operator new[]`  (no-other-mention)
- src/platform/new.cpp:33  function `operator delete`  (no-other-mention)
- src/platform/new.cpp:38  function `operator delete[]`  (no-other-mention)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `ARCH_IS_BIG_ENDIAN`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: src/platform/md5.c:59
- `RIPTOPL_UPDATER`  tested at 6 site(s) in the repo, mentioned in 3 build file(s); here: include/ratls.h:11
- `_IOP`  tested at 33 site(s) in the repo, mentioned in 1 build file(s) [builtin-like]; here: include/nbns.h:20
- `__cplusplus`  tested at 190 site(s) in the repo, mentioned in 0 build file(s) [builtin-like]; here: include/hdd.h:88, include/hdd.h:92

## Byte-identical files (dupes-exact-files.tsv)
- include/md5.h is byte-identical to: PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.h

## Largely identical files (dupes-file-pairs.tsv)
- include/md5.h is 100% the same as PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.h (52 cloned normalised lines)
- include/md5.h is 44% the same as PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.c (23 cloned normalised lines)
- include/md5.h is 44% the same as RiptOPL/src/platform/md5.c (23 cloned normalised lines)
- src/platform/md5.c is 8% the same as PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.h (23 cloned normalised lines)
- src/platform/md5.c is 8% the same as RiptOPL/include/md5.h (23 cloned normalised lines)
- src/platform/md5.c is 98% the same as PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.c (274 cloned normalised lines)

## Duplicate blocks (dupes-blocks.tsv)
- include/md5.h:2-91  ==  PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.h:2-91  (52 normalised lines)
- include/md5.h:4-34  ==  PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.c:4-34  (23 normalised lines)
- include/md5.h:4-34  ==  RiptOPL/src/platform/md5.c:4-34  (23 normalised lines)
- src/platform/md5.c:139-364  ==  PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.c:139-364  (151 normalised lines)
- src/platform/md5.c:2-51  ==  PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.c:2-51  (39 normalised lines)
- src/platform/md5.c:4-34  ==  PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.h:4-34  (23 normalised lines)
- src/platform/md5.c:4-34  ==  RiptOPL/include/md5.h:4-34  (23 normalised lines)
- src/platform/md5.c:55-149  ==  PS2-Servers/vendor/retroachievements/rcheevos/src/rhash/md5.c:55-149  (84 normalised lines)
