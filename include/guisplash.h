#ifndef __GUISPLASH_H
#define __GUISPLASH_H

#include "include/guiboot.h"

// The boot splash: the glowing logo, the version and status lines, and the intro loop that fades it out.
class BootSplash
{
public:
    BootStatus status;

    // Draw one splash frame at the given fade alpha (0..0x80).
    void render(int alpha);
    // One standalone splash frame at full opacity (the boot "loading" screen).
    void renderScreen();
    // Run the intro until the splash has faded out.
    void run();
    // The intro is a splash, never a generic I/O-spinner screen.
    bool active() const { return active_; }
    // Set once the intro loop has finished: Last Played Auto Start counts down from here.
    bool ended() const { return ended_; }

private:
    void drawLogo(GSTEXTURE *logo, int cx, int cy, int alpha);

    bool active_ = false;
    bool ended_ = false;
};

extern BootSplash gSplash;

#endif
