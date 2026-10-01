#ifndef __GUIGAMEID_H
#define __GUIGAMEID_H

#include "include/opl.h"

// The CosmicScale "GameID" barcode: a serial turned into a start/length/payload/checksum packet and drawn as clock + data bars.
class GameIdBarcode
{
public:
    explicit GameIdBarcode(const char *startup);
    bool valid() const { return length_ > 0; }
    // Draw the barcode once: per data bit (MSB-first) a magenta clock column + a cyan(1)/yellow(0) column.
    void draw() const;
    // Hold the barcode for a few frames, then leave two clean black frames behind (#269).
    void show() const;

private:
    // Normalise a startup id into the serial the GameID device expects.
    static void cleanSerial(const char *startup, char *out, int outSize);
    // Build the packet from a cleaned serial; returns its length in bytes.
    static int buildPacket(u8 *data, const char *serial);

    u8 data_[32];
    int length_ = 0;
};

#endif
