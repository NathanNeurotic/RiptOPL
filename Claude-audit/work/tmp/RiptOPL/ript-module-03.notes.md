Done: all 6 bdmassault files (iLinkman/src/imports.lst, iLink_CROM.h, iLink_internal.h, iLinkman/src/irx_imports.h, src/imports.lst, src/irx_imports.h)

Candidates:
- bdmassault/.../iLinkman/src/imports.lst 1-74 + iLinkman/src/irx_imports.h 1-32 (+ iLinkman/src/exports.tab, IEEE1394_bd/src/imports.lst, irx_imports.h) | unused: Makefile builds only src/imports.lst, src/exports.tab; no .c includes irx_imports.h | CHECKED (no ref in tests either). iLinkman imports.lst still lists cdvdman which README says removed.
- src/irx_imports.h:32-34 SIF_CALLBACKS_12_13 never defined; :20 cdvdman.h include stale | trivial, group
- iLink_internal.h:16 REQ_CHECK_CONSOLE_VERSION also -D in Makefile:10 | redundant define
- iLink_internal.h unused macros (~37) + PACKET_TCODE malformed; CROM.h 4 macros; prototypes iLinkIntrRegister0Handler:315, PHTSendResponse:322 no definition | both copies (modules/ilink same)
- dup bdmassault iLinkman vs modules/ilink/iLinkman (88%) intentional, ORIGIN.txt says keep both in sync | consider mention
