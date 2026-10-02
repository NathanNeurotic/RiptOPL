#ifndef __SOUNDINT_H
#define __SOUNDINT_H

// Internals shared between sound.cpp (audsrv bring-up, volumes) and sfx.cpp / bgm.cpp. Not public API.

#include "sound.h"

extern int gAudioInitialized; // audsrv_init() succeeded

void sfxSetCursorChannelsVolume(int volume);

extern volatile unsigned char bgmThreadRunning, bgmIoThreadRunning; // set while the BGM threads run (bgm.cpp)

#endif
