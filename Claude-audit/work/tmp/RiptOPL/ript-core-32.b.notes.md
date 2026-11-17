# ript-core-32.b notes (lens defects)

Done: src/sources/supportbase.cpp 1-3181 (read_full), include/udpfssupport.h 1-78 (read_full).

Candidates (to verify):
- supportbase.cpp:168-177,269 | sprintf "%s/games.bin" into filename[256], path can be up to 255 (sbReadList path[256] with sub) | check FOLDER_SUB_MAX, prefix lengths
- supportbase.cpp:370-379 | early return when path too long leaks cache.games | same trigger
- supportbase.cpp:640-643 | g->startup[GAME_STARTUP_MAX] write; check base_game_info_t startup size
- supportbase.cpp:604-613,709-713 | ul.cfg size not multiple of record -> whole list rejected incl ISO | check UL record size, upstream behaviour
- supportbase.cpp:1462-1469 | size stat path inserts sep after prefix; sbCreatePath does not | check prefixes in callers
- supportbase.cpp:1275-1295 | USBLD rename crc on full newname vs stored 32-char name | check caller name limits
- supportbase.cpp:1994-2020 | version.txt open/read only for LOG | check LOG in release
- supportbase.cpp:2519-2528 | "mc0:NEUTRINO/NEUTRINO.ELF" spelling; check NEUTRINO_PATH macros
- supportbase.cpp:777-778 | *(u32*)(IOBuffer+80) alignment | check IOBuffer definition
- supportbase.cpp:731-748 | ziso_init alloc per probe? | check zso
- supportbase.cpp:1929-1936 | RA watch header size vs watch list type | check rawatch
