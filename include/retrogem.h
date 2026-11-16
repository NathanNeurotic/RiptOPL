/*
  Copyright 2026, Open-PS2-Loader contributors
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#ifndef __RETROGEM_H
#define __RETROGEM_H

#include <tamtypes.h>
#include <stddef.h>

constexpr int RETROGEM_GAMEID_MAX = 13; // "LSP99016.101" + NUL

// Normalize a serial token for metadata: case/separators, SLUSP alias and LSP layout.
// Invalid or oversized tokens fail rather than truncating into another game's key.
int retrogemNormalizeGameID(const char *raw, char *out, size_t maxLen);

// The disc's own serial from SYSTEM.CNF (BOOT/BOOT2) inside a POPStarter .VCD (1 MiB header).
// 1 on success; a human game/file name is never accepted as a GameID.
int retrogemGetVcdGameID(const char *vcdPath, char *gameID, size_t maxLen);

// The same for an Ember image: .cue (its data-track file), .bin/.iso (raw 2352 or cooked 2048) or
// .vcd. A bare PS-X EXE has no disc filesystem and yields 0.
int retrogemGetPs1ImageGameID(const char *imagePath, char *gameID, size_t maxLen);

// Draw the RetroGEM optical barcode for `frames` frames immediately before the ELF handoff.
void displayRetroGemGameID(const char *gameID, int frames);

#endif // __RETROGEM_H
