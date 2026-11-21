## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- modules/iopcore/cdvdman/ncmd.c:55  function `sceCdReadCdda`  (only-prototype-or-export-mentions) | also mentioned: modules/iopcore/cdvdman/exports.tab:44 DECLARE_EXPORT(sceCdReadCdda)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `ALT_READ_CORE`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: modules/iopcore/cdvdman/irx_imports.h:25

## Largely identical files (dupes-file-pairs.tsv)
- modules/iopcore/cdvdman/hdpro_atad.c is 4% the same as RiptOPL/modules/iopcore/cdvdman/atad.c (20 cloned normalised lines)
- modules/iopcore/cdvdman/internal.h is 10% the same as RiptOPL/modules/isofs/isofs.c (14 cloned normalised lines)
- modules/iopcore/cdvdman/internal.h is 9% the same as RiptOPL/.github/scripts/test_stream_iop_deadlocks.py (13 cloned normalised lines)
- modules/iopcore/cdvdman/ioman_add.h is 90% the same as RiptOPL/modules/mcemu/ioman_add.h (36 cloned normalised lines)

## Duplicate blocks (dupes-blocks.tsv)
- modules/iopcore/cdvdman/hdpro_atad.c:402-428  ==  RiptOPL/modules/iopcore/cdvdman/atad.c:363-389  (20 normalised lines)
- modules/iopcore/cdvdman/internal.h:69-85  ==  RiptOPL/.github/scripts/test_stream_iop_deadlocks.py:42-58  (13 normalised lines)
- modules/iopcore/cdvdman/internal.h:96-111  ==  RiptOPL/modules/isofs/isofs.c:19-34  (14 normalised lines)
- modules/iopcore/cdvdman/ioman_add.h:27-48  ==  RiptOPL/modules/mcemu/ioman_add.h:25-46  (20 normalised lines)
- modules/iopcore/cdvdman/ioman_add.h:6-25  ==  RiptOPL/modules/mcemu/ioman_add.h:4-23  (16 normalised lines)

## TODO/FIXME/HACK style markers (cond-markers.tsv)
- modules/iopcore/cdvdman/device-mmce.c:224  //TODO: For VMCs
