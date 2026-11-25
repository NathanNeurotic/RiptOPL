// TEST ONLY, NEVER MERGE: real-PS2 SMB1 measurement build for issue 536 (ps2rig, booted from ps2link).
// Reads and writes no settings file, records no card history, comes up on the Network (SMB1) page and
// starts the share's last-played game by itself. The game's exit runs RIG_EXIT_PATH (back to ps2link).
#pragma once
#if defined(__mips__)
#define RIG_SMB_TEST    1
#endif
#define RIG_SMB_PORT    1112
#define RIG_SMB_SHARE   "riptest"
#define RIG_STARTUP     "SLUS_999.99"
#define RIG_EXIT_PATH   "mc0:/ps2link/PS2LINK.ELF"
