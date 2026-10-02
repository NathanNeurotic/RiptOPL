/*
  Copyright 2026, Open-PS2-Loader contributors
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#ifndef __RETROGEM_H
#define __RETROGEM_H

#include <tamtypes.h>
#include <stddef.h>
#include <stdio.h>

constexpr int RETROGEM_GAMEID_MAX = 12;

// RetroGEM optical-barcode support (src/sources/retrogem.cpp): the PS1 Game ID
// extraction ladder (SYSTEM.CNF, PVD timestamp, partition/filename fallback)
// and the barcode renderer shown before ELF execution. The known-disc
// timestamp table is a static const member (constant-initialized, EE-safe).
// One instance; the free functions below are thin forwarders.
class RetroGEM
{
public:
    /**
     * Extracts an 11-character PS1 Game ID (e.g., SLUS_000.01 or SLUS-00001) from a .VCD image file
     * using a 4-tier resolution ladder:
     *   1. ISO 9660 SYSTEM.CNF parsing
     *   2. PVD Volume Creation Timestamp lookup
     *   3. Partition / Subpath parsing
     *   4. Filename fallback
     *
     * @param vcdPath Full path to the .VCD file or partition item
     * @param gameID Output buffer for the 11-character Game ID (plus NUL terminator)
     * @param maxLen Size of the output buffer (must be at least 12 bytes)
     * @return 1 on successful extraction, 0 on failure/unresolved
     */
    int getVcdGameId(const char *vcdPath, char *gameID, size_t maxLen);

    /**
     * Renders the RetroGEM optical barcode signal onto the GS framebuffer for the specified number
     * of frames immediately before ELF execution.
     *
     * @param gameID 11-character Game ID ASCII string
     * @param frames Number of frames to hold the barcode (typically 2)
     */
    void displayGameId(const char *gameID, int frames);

private:
    // Table of known PS1 disc PVD volume creation timestamps (16-byte ASCII) for discs that lack
    // SYSTEM.CNF or boot generic targets like PSX.EXE.
    struct genericGameId
    {
        const char timestamp[17]; // 16-char ASCII timestamp + NUL
        const char gameId[12];    // 11-char Game ID + NUL
    };
    static const genericGameId genericIds_[];

    static int isValidIdChar(char c);
    static u8 calculateCrc(const u8 *data, int len);
    int cleanTitleId(const char *raw, char *out, size_t maxLen);
    // Tier 1 & 2 helper: ISO 9660 SYSTEM.CNF parsing and PVD Timestamp lookup
    int parseIsoSector16(FILE *f, char *gameID, size_t maxLen);
};

extern RetroGEM gRetroGEM;

// The free API other translation units call; both forward to gRetroGEM.
int retrogemGetVcdGameID(const char *vcdPath, char *gameID, size_t maxLen);
void displayRetroGemGameID(const char *gameID, int frames);

#endif // __RETROGEM_H
