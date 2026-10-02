/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

#include "include/guiframe.h"

FrameClock gFrames;

void FrameClock::endFrame()
{
    clock_t nowUs = clock();
    if (mark_ != 0) {
        u32 elapsed = (u32)(nowUs - mark_);
        lastUs_ = elapsed;
        if (elapsed > worstUs_)
            worstUs_ = elapsed;
        if (elapsed > 25000)
            overruns_++;
    }
    mark_ = nowUs;

#ifdef __DEBUG
    // Measure time directly after vsync
    prev_ = cur_;
    cur_ = clock();
#endif
}

u32 FrameClock::watchdog(u32 *worstUs, u32 *overruns) const
{
    if (worstUs)
        *worstUs = worstUs_;
    if (overruns)
        *overruns = overruns_;
    return lastUs_;
}

#ifdef __DEBUG
float FrameClock::fps()
{
    if (prev_ != 0) {
        clock_t diff = cur_ - prev_;
        if (diff == 0)
            diff = 1;

        // Raw FPS value with 2 decimal places
        float rawfps = ((100 * CLOCKS_PER_SEC) / diff) / 100.0f;

        if (fps_ == 0.0f)
            fps_ = rawfps;
        else
            fps_ = fps_ * 0.9f + rawfps / 10.0f; // Smooth FPS value
    }
    return prev_ != 0 ? fps_ : 0.0f;
}
#endif
