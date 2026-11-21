# ript-core-30 notes (structure lens)

## Done
- src/sources/nettime.cpp 1-158
- include/rabadge.h 1-47
- src/sources/rabadge.cpp 1-220
- include/rahash.h 1-36
- src/sources/rahash.cpp 1-971
- include/rahunt.h 1-33
- src/sources/rahunt.cpp 1-213
- include/ranet.h 1-117
- src/sources/ranet.cpp 1-1136
- include/rapopslaunch.h 1-8
- src/sources/rapopslaunch.cpp 1-129
- src/sources/ratls_ps2.cpp 1-313
- include/rawatch.h 1-65
ALL READ. Now verifying candidates.

## Candidates
- src/sources/nettime.cpp:31-38 | struct hostent redeclared locally | also ratls_ps2.cpp:41-48
- src/sources/nettime.cpp:108-126 | peek+recv 64-byte quirk loop | ranet.cpp:1072-1080 tcpRecv, ratls_ps2.cpp:156-164 copy
- include/rabadge.h:18-45 + rabadge.cpp:50-60 | RaBadge class one instance plus free forwarders | same pattern ranet.h:15-115/ranet.cpp:148-209, rawatch.h:11-63; check callers of gRanet/gRaWatch/gRaBadge directly
- src/sources/rabadge.cpp:208-210 | text() slot lookup duplicates first loop of slotFor | small
- src/sources/rahash.cpp:71-153 | RaHash class single instance, forwarders
- src/sources/rahash.cpp:525-547, 634-649, 799-818 | md5 chunk loop three times
- src/sources/rahash.cpp:773-780 vs 836-842 | PSX.EXE fallback dup
- src/sources/rahash.cpp:877-902 vs 171-181 | hex table twice
- src/sources/rahash.cpp:915-917 vs 928-930 | lower-hex validation loop twice
- include/rahash.h:9,22 | raHashSetStepLog, raVcdBootName callers? | grep
- ranet.cpp:276 askMstack lead: WRONG, called at 320. drop
- ranet.cpp:619-635 vs 1051-1059 | RAA1 OK 5/3-number parse + title trim duplicated
- ranet.cpp:897-912 vs 1010-1013 | RAP1 + RAO1 OK check
- ratls_ps2.cpp:213-257 vs 266-312 | gethostbyname/connect/expired block duplicated (lead)
- ratls_ps2.cpp:137-165 | non-RA copies of nicReserved/tcpRecv (dup of ranet); RA wrappers forward
- ratls_ps2.cpp:174-188 | expired computed twice; idle ternary 3x
