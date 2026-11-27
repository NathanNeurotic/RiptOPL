## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- modules/network/nfsman/unfs4/unfs4.h:175  macro `unfs4_close`  (no-other-mention)
- modules/network/nfsman/unfs4/unfs4.h:193  macro `unfs4_closedir`  (no-other-mention)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `HAVE_SOCK_SIN_LEN`  tested at 2 site(s) in the repo, mentioned in 0 build file(s); here: modules/network/nfsman/unfs4/unfs4.c:906
- `O_EXCL`  tested at 1 site(s) in the repo, mentioned in 2 build file(s); here: modules/network/nfsman/unfs4/unfs4.c:615
- `UNFS4_FEATURE_REASSEMBLY`  tested at 5 site(s) in the repo, mentioned in 0 build file(s); here: modules/network/nfsman/unfs4/unfs4.c:111, modules/network/nfsman/unfs4/unfs4.c:125, modules/network/nfsman/unfs4/unfs4.c:140, modules/network/nfsman/unfs4/unfs4.c:182, modules/network/nfsman/unfs4/unfs4.c:231
- `Z80`  tested at 7 site(s) in the repo, mentioned in 0 build file(s); here: modules/network/nfsman/unfs4/unfs4.c:46, modules/network/nfsman/unfs4/unfs4.h:34

## Largely identical files (dupes-file-pairs.tsv)
- modules/network/nfsman/unfs4/unfs4.c is 2% the same as Launchstation/iop/smb2man/usmb2/usmb2.c (15 cloned normalised lines)
- modules/network/nfsman/unfs4/unfs4.c is 2% the same as RiptOPL/modules/network/smb2man/usmb2/usmb2.c (15 cloned normalised lines)
- modules/network/nfsman/unfs4/unfs4.c is 97% the same as Launchstation/iop/nfsman/unfs4/unfs4.c (502 cloned normalised lines)
- modules/network/nfsman/unfs4/unfs4.h is 92% the same as Launchstation/iop/nfsman/unfs4/unfs4.h (74 cloned normalised lines)
- modules/network/ps2ips/irx_imports.h is 100% the same as Launchstation/iop/ps2ips_rpc/irx_imports.h (19 cloned normalised lines)
- modules/network/ps2ips/ps2ips.c is 77% the same as Launchstation/iop/ps2ips_rpc/ps2ips.c (348 cloned normalised lines)
- modules/network/ps2ips/ps2ips.c repeats itself: 32 cloned normalised lines inside the file (7%)

## Duplicate blocks (dupes-blocks.tsv)
- modules/network/nfsman/unfs4/unfs4.c:155-220  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:155-220  (40 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:213-240  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:213-240  (19 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:240-327  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:240-327  (42 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:30-161  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:30-161  (83 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:30-49  ==  Launchstation/iop/smb2man/usmb2/usmb2.c:3-22  (15 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:30-49  ==  RiptOPL/modules/network/smb2man/usmb2/usmb2.c:3-22  (15 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:331-404  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:331-405  (38 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:395-450  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:396-451  (32 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:460-548  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:477-565  (50 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:553-593  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:570-610  (20 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:625-646  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:645-666  (14 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:648-709  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:668-729  (37 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:719-768  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:740-789  (35 normalised lines)
- modules/network/nfsman/unfs4/unfs4.c:772-915  ==  Launchstation/iop/nfsman/unfs4/unfs4.c:797-940  (92 normalised lines)
- modules/network/nfsman/unfs4/unfs4.h:110-225  ==  Launchstation/iop/nfsman/unfs4/unfs4.h:111-227  (56 normalised lines)
- modules/network/nfsman/unfs4/unfs4.h:36-107  ==  Launchstation/iop/nfsman/unfs4/unfs4.h:37-108  (18 normalised lines)
- modules/network/ps2ips/irx_imports.h:2-27  ==  Launchstation/iop/ps2ips_rpc/irx_imports.h:2-27  (19 normalised lines)
- modules/network/ps2ips/ps2ips.c:196-234  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:156-194  (21 normalised lines)
- modules/network/ps2ips/ps2ips.c:196-234  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:256-294  (21 normalised lines)
- modules/network/ps2ips/ps2ips.c:2-34  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:2-34  (20 normalised lines)
- modules/network/ps2ips/ps2ips.c:211-244  ==  RiptOPL/modules/network/ps2ips/ps2ips.c:338-371  (18 normalised lines)
- modules/network/ps2ips/ps2ips.c:338-361  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:171-194  (13 normalised lines)
- modules/network/ps2ips/ps2ips.c:338-361  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:271-294  (13 normalised lines)
- modules/network/ps2ips/ps2ips.c:338-371  ==  RiptOPL/modules/network/ps2ips/ps2ips.c:211-244  (18 normalised lines)
- modules/network/ps2ips/ps2ips.c:388-530  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:318-460  (76 normalised lines)
- modules/network/ps2ips/ps2ips.c:393-415  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:356-378  (14 normalised lines)
- modules/network/ps2ips/ps2ips.c:393-415  ==  RiptOPL/modules/network/ps2ips/ps2ips.c:426-448  (14 normalised lines)
- modules/network/ps2ips/ps2ips.c:426-448  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:323-345  (14 normalised lines)
- modules/network/ps2ips/ps2ips.c:426-448  ==  RiptOPL/modules/network/ps2ips/ps2ips.c:393-415  (14 normalised lines)
- modules/network/ps2ips/ps2ips.c:549-591  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:483-525  (29 normalised lines)
- modules/network/ps2ips/ps2ips.c:614-745  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:553-684  (78 normalised lines)
- modules/network/ps2ips/ps2ips.c:94-180  ==  Launchstation/iop/ps2ips_rpc/ps2ips.c:63-149  (49 normalised lines)

## TODO/FIXME/HACK style markers (cond-markers.tsv)
- modules/network/rapops/rapops.c:112  /* TODO: POPS may bring its own DEV9; whether ours then fails to load
- modules/network/rapops/rapops.c:126  TODO: no EE event buffer under POPS yet, so no notices in game. */
- modules/network/rapull/rapull.c:114  /* TODO: PS1 scratchpad (0x200000-0x2003FF) reads as zero; where
- modules/network/rapull/rapull.c:23  TODO: add an out-of-band startup/EE-read diagnostic that cannot be
- modules/network/rapull/rapull.c:25  TODO: the EE writes PS1 RAM through its data cache and this reads
- modules/network/rapull/rapull.c:28  TODO: the reads are not tied to a frame of the game; entries of one
