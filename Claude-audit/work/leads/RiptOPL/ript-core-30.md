## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/sources/ranet.cpp:276  function `askMstack`  (only-prototype-or-export-mentions) | also mentioned: include/ranet.h:73 int askMstack(struct sockaddr_in *to, const char *req, char *out, int asklen);; src/sources/ranet.cpp:320 return askMstack(to, req, out, asklen);

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `RIPTOPL_UPDATER`  tested at 6 site(s) in the repo, mentioned in 3 build file(s); here: src/sources/ratls_ps2.cpp:260, src/sources/ratls_ps2.cpp:63

## Largely identical files (dupes-file-pairs.tsv)
- src/sources/ratls_ps2.cpp repeats itself: 14 cloned normalised lines inside the file (6%)

## Duplicate blocks (dupes-blocks.tsv)
- src/sources/ratls_ps2.cpp:231-246  ==  RiptOPL/src/sources/ratls_ps2.cpp:283-298  (14 normalised lines)
- src/sources/ratls_ps2.cpp:283-298  ==  RiptOPL/src/sources/ratls_ps2.cpp:231-246  (14 normalised lines)
