## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- elfldr/loader.c:53  function `_libcglue_init`  (no-other-mention)
- elfldr/loader.c:54  function `_libcglue_deinit`  (no-other-mention)
- elfldr/loader.c:55  function `_libcglue_args_parse`  (no-other-mention)
- elfldr/loader.c:81  function `_ps2sdk_fileXio_deinit`  (no-other-mention)
- include/pggsm.h:20  variable `GSM_ARGS`  (no-other-mention)
- include/pggsm.h:23  function `makeSMODE1`  (no-other-mention)
- include/pggsm.h:33  function `makeSYNCH1`  (no-other-mention)
- include/pggsm.h:38  function `makeSYNCH2`  (no-other-mention)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `__cplusplus`  tested at 190 site(s) in the repo, mentioned in 0 build file(s) [builtin-like]; here: include/disc_key.h:21, include/disc_key.h:27

## Largely identical files (dupes-file-pairs.tsv)
- elfldr/linkfile is 24% the same as POPSLoader/src/elf_loader/src/loader/linkfile (16 cloned normalised lines)
- elfldr/loader.c is 13% the same as Launchstation/loader/loader.c (39 cloned normalised lines)

## Duplicate blocks (dupes-blocks.tsv)
- elfldr/linkfile:55-74  ==  POPSLoader/src/elf_loader/src/loader/linkfile:38-57  (16 normalised lines)
- elfldr/loader.c:104-138  ==  Launchstation/loader/loader.c:117-155  (25 normalised lines)
- elfldr/loader.c:93-111  ==  Launchstation/loader/loader.c:104-125  (14 normalised lines)
