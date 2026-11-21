# ript-core-26 notes

## Done (all read in full, re-read in attempt 2)
- src/sources/cuesupport.cpp 1-1288
- include/discsupport.h 1-8
- src/sources/discsupport.cpp 1-183
- include/ethsupport.h 1-141
- src/sources/ethsupport.cpp 1-1671
- include/favsupport.h 1-192

## Verified candidates (attempt 2) -> findings
1 Ember launchCue preflight x4 (+hdd) | tests test_ember_disc_selection.py pin legs
2 last_played save block x9 (eth,bdmlaunch,hdd,mmce,udpfs,http)
3 PS2 logo probe x5 (eth x2, mmce, udpfs x2)
4 ethReadOnlyCfgDir == bdmsupport.cpp:2052-2068
5 prefix separator rule x6 (cueSep, vcdSep, inline eth/fav/supportbase/cue:127)
6 CueSupport + DiscSupport wrapper classes; DiscSupport::launch declared, never defined
7 one-line forwarders eth (11) + fav (10) | many tests reference names
8 EthSupport reference accessors (12)
9 ethsupport repeated reset/free/guard blocks, update tail == count
10 cuesupport small dups: value tables, ext loop, free pairs x9
11 cueEmberFolder constant accessor | 5 tests extract it
12 cuesupport defensive checks on static helpers
13 VCD_POPSNET message mapping eth vs appsupport
14 SMB2 diagLog breadcrumbs x13
15 stale comments
Dropped: lead dup eth 227-244 vs udpfs (override boilerplate); ethResolveIsoFavourite lead (used by favsupport.cpp:649)
