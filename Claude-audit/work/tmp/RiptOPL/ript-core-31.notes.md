# ript-core-31 notes (lens structure)

Files done (all lines read, re-read in attempt 2):
- src/sources/rawatch.cpp 1-318 DONE
- include/retrogem.h 1-30 DONE
- src/sources/retrogem.cpp 1-675 DONE
- include/supportbase.h 1-185 DONE

Verified candidates (attempt 2):
- rawatch.cpp:34-72 + rawatch.h:7-63 | RaWatch class + 8 forwarders, gRaWatch used only in rawatch.cpp | KEEP (rank 2); test_ra_runtime_budget.py:188-190 instantiates RaWatch
- rawatch.cpp:164-174 vs 252-274, 296-297 vs 83-84 | duplicated header + node-tail validation | KEEP rank 2
- retrogem.cpp:142-172 | legacy serial parser, only adds trailing-junk acceptance | KEEP rank 2, risk Med
- retrogem.cpp:97-99,58-61,628-629, retrogem.h:15-17, includes 8,11,12,15,17 | trivial dead | KEEP rank 1
- vcdsupport.cpp:358-409,456-457 | vcdCanonDisplayId identity on retrogem output | KEEP rank 2
- retrogem.cpp ISO walker vs rahash.cpp 403-501,663-763,826-846 | duplicate VCD SYSTEM.CNF readers | KEEP rank 2
- retrogemHasExt vs fmHasSuffix (guifilemgr.cpp:74-78) vs httpEndsWithNoCase (httpcatalog.cpp:118) | rank 1
- supportbase.h:11 ISO_GAME_FNAME_MAX unused | rank 1
- supportbase.h:130-131 + supportbase.cpp:2085-2088 sbNeutrinoDeinitException forwarder | rank 1
- stale comments rawatch.cpp:119-121, supportbase.h:48-51,57,99-103 | rank 1
- all other supportbase.h prototypes: defined and called (checked)
