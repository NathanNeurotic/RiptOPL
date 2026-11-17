# ript-core-21 notes (lens structure)

Done: all 5 files read in full. Verification done; writing findings.

Findings planned:
1 include prelude + SYS_DISC constants copied into sysneutrino/sysvmc/system.cpp (only sysdisc uses constants)
2 toml read + write-restore duplicated (sysSync vs SmbPreflight), two static buffer pairs ~16.9 KB bss, share one -> 8212 B; cheats/RA write helpers
3 three token walks (HasActiveFlag, EffectiveBsdfs, ArgActiveValue)
4 sysLastPathSeparator == supportbase sbLastPathSeparator (1955-1967), inline copies util.cpp:288-292
5 guards that cannot fail: convertCompatmaskToModes, argc<argvMax before appendArgTokens
6 stale/misplaced comments (sysneutrino 190-198, 263-278, 1186-1189, 1363-1365; sysdisc 109-117; system.h 81-82)
Dropped: system.h symbols all used; ratls/selfupdate exports all used.
