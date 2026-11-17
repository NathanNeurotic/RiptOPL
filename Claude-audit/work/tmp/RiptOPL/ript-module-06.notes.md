Done: libapa.h, misc.c, password.c, table_fence.c, table_fence.h (all full)
Build: ps2hdd_osd defines APA_OSD_VER => APA_ENABLE_PASSWORDS undefined; APA_SUPPORT_GPT nowhere defined
Candidates:
- libapa.h:25-30,90-114 + hdd_fio.c:395-409,1077-1081 + apa.c:419-425,435-443 | APA_SUPPORT_GPT never defined, dead branches | vendored upstream, rank 1-2
- password.c:27-28 | APA_ENABLE_PASSWORDS undefined in OSD build, apaPassCmp always 0 | rank 1
- misc.c/password.c non-_IOP branches | host-only branches unused (test_apa_recover extracts text only) | rank 1, upstream
- password.c 6x repeated bit-permute loop | upstream DES; could be one helper | upstream; probably skip or rank 1
- cross-repo: libapa/misc/password vs Launchstation iop/ps2hdd_osd | vendored twice
