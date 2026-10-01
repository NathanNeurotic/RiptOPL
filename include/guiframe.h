#ifndef __GUIFRAME_H
#define __GUIFRAME_H

#include <time.h>
#include "include/opl.h"

// Frame-time watchdog for the debug HUD (and the FPS meter in debug builds). It answers the one question nobody asked about the
// periodic swallowed navigation step: DOES THE EE STALL AT ALL when it happens?
//
// Every theory assumed the menu thread stops for a moment. If it does, this catches the overrun. If frames stay on vsync straight
// THROUGH a swallowed step, the EE polled the pad on time and freepad had nothing to give it -- the input died on the IOP over
// SIO2, and no EE-side work (thread priority, art queues, device probing) could ever have fixed it.
class FrameClock
{
public:
    // Call right after the vsync wait (rmEndFrame): clock() is microseconds on the EE, so this measures whole frames back to back.
    void endFrame();
    // Last frame length in microseconds; the worst frame and the count of frames longer than ~1.5 vsyncs go to the out parameters.
    u32 watchdog(u32 *worstUs, u32 *overruns) const;
#ifdef __DEBUG
    // Smoothed frames per second; 0 until two frames have been measured.
    float fps();
#endif

private:
    clock_t mark_ = 0;
    u32 lastUs_ = 0;
    u32 worstUs_ = 0;
    u32 overruns_ = 0;
#ifdef __DEBUG
    clock_t prev_ = 0;
    clock_t cur_ = 0;
    float fps_ = 0.0f;
#endif
};

extern FrameClock gFrames;

#endif
