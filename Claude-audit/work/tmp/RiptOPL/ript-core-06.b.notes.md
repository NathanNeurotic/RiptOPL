# ript-core-06.b notes (lens defects)

## Done
- ee_core/tests/test_freeze_diag_format.c 1-257 (host test, nothing)
- ee_core/tests/test_freeze_diag_screen.c 1-136 (host test, nothing)
- elfldr/Makefile 1-27 (legacy make; ps2.yaml:615-624 is the real build)
- elfldr/linkfile 1-86 (re-read attempt 2)
- elfldr/loader.c 1-475 (re-read attempt 2)
- all 33 include/*.h files of the unit read in full (attempt 2)

## Candidates (KEEP = goes in findings)
- KEEP artindexint.h:29-44 + src/gfx/artindex.cpp:191-201 | hashes[4096] 16 KB + pics[4096] 32 KB malloc'd per indexed dir regardless of count, kept until slot reuse, 3 slots -> 144 KB | rank 4
- KEEP elfldr/linkfile:22-24,53-78 | orphan .text.*/.rodata.*/.bss.* -> ~75 section headers + shstrtab in embedded elfldr_irx (21,476 B D, loadable span 16,900 B, raw/riptopl-size-A.txt:68-143); parent reads only ehdr/phdrs/PT_LOAD | rank 2
- KEEP elfldr/loader.c:222 | GetMemorySize() in loop condition, 507,904 calls; POPSLoader loader.c:74 same; Launchstation hoists | rank 2
- KEEP disc_key.h:40-59 | static inline with static buffers included by system.cpp + sysdisc.cpp -> two copies (nm full.txt:4588-4591) | rank 1
- dropped: orphan placement/_stack bug (size -A shows all in bram), trailing-flag eating (theoretical), fileXio entry==0 (theoretical)
