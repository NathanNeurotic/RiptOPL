# ript-core-14 notes (lens structure)

## Done
- ALL 11 files read in full. Verification done. Writing findings JSON.

## Confirmed findings
1 redundant Abort-before-Cancel at 6 call sites (lwnbdctl 141, guiwait 67, themes 1820, appsupport 789, hdd 2178, 2267)
2 gArtW.refused never written; r never printed; 5 HUD getters
3 stale/orphan comments artload/artworker.h/artqueue.h/coverflow
4 copied include blocks with unused headers artload 16-19, texcache 13/15/16, coverflow 11-21
5 impossible-state checks (artload 515-517, 542-551, waitTicks<0, atlas 73-74, artindex 335/381, coverflow 304, artload 897-898)
6 artindex small dups (picCmp/hashCmp, free triples, reject preamble)
7 ArtIndex class + artindexint.h + forwarders
8 Atlas PSM generality only for tests
9 FNV-1a duplicate artworker.cpp 13-22 vs artindex.cpp 54-67
## Refuted
- ~Atlas lead: used by raii::DestroyFree (raii.h:89)
- coverflow/themes dup lead: include list only (folded into 4)
