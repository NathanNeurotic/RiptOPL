## Files that no other file mentions by name (files-unreferenced.tsv)
- ee_core/debug_colors_doc.sh  (14 lines; mentioned in 0 docs)

## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- ee_core/include/igrmfont.h:17  macro `G_POINTER`  (no-other-mention)
- ee_core/include/igs_api.h:100  macro `GS_IMR`  (no-other-mention)
- ee_core/include/igs_api.h:103  macro `GET_PMODE_EN1`  (no-other-mention)
- ee_core/include/igs_api.h:105  macro `GET_SMODE2_INT`  (no-other-mention)
- ee_core/include/igs_api.h:106  macro `GET_SMODE2_FFMD`  (no-other-mention)
- ee_core/include/igs_api.h:107  macro `GET_SMODE2_DPMS`  (no-other-mention)
- ee_core/include/igs_api.h:112  macro `GET_DISPFB_DBX`  (no-other-mention)
- ee_core/include/igs_api.h:113  macro `GET_DISPFB_DBY`  (no-other-mention)
- ee_core/include/igs_api.h:39  macro `GS_FINISH`  (no-other-mention)
- ee_core/include/igs_api.h:60  macro `GS_D1_TADR`  (no-other-mention)
- ee_core/include/igs_api.h:61  macro `GS_D1_ASR0`  (no-other-mention)
- ee_core/include/igs_api.h:62  macro `GS_D1_ASR1`  (no-other-mention)
- ee_core/include/padhook.h:33  macro `DEV9_R_146C`  (no-other-mention)
- ee_core/include/padhook.h:34  macro `DEV9_R_146E`  (no-other-mention)
- ee_core/include/padhook.h:84  macro `IGR_LIBPAD_NONE`  (no-other-mention)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `IGR_MENU`  tested at 29 site(s) in the repo, mentioned in 9 build file(s); here: ee_core/include/modules.h:55
- `INGAMEDIAG`  tested at 38 site(s) in the repo, mentioned in 6 build file(s); here: ee_core/include/freeze_diag_core.h:13, ee_core/include/modules.h:61

## Largely identical files (dupes-file-pairs.tsv)
- ee_core/include/padpatterns.h repeats itself: 27 cloned normalised lines inside the file (15%)

## Duplicate blocks (dupes-blocks.tsv)
- ee_core/include/padpatterns.h:231-245  ==  RiptOPL/ee_core/include/padpatterns.h:290-304  (15 normalised lines)
- ee_core/include/padpatterns.h:236-247  ==  RiptOPL/ee_core/include/padpatterns.h:351-362  (12 normalised lines)
- ee_core/include/padpatterns.h:290-304  ==  RiptOPL/ee_core/include/padpatterns.h:231-245  (15 normalised lines)
- ee_core/include/padpatterns.h:351-362  ==  RiptOPL/ee_core/include/padpatterns.h:236-247  (12 normalised lines)
