/*
 Copyright 2022, Thanks to SP193
 Licenced under Academic Free License version 3.0
 Review OpenPS2Loader README & LICENSE files for further details.
 */

#include <audsrv.h>
#include <timer.h>
#include <vorbis/vorbisfile.h>
#include <delaythread.h>

#include "include/sound.h"
#include "include/soundint.h"
#include "include/opl.h"
#include "include/ioman.h"
#include "include/themes.h"
#include "include/pad.h"

/*--    Theme Sound Effects    ----------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------*/

int gAudioInitialized = 0;

/*--    General Audio    ------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------*/

void audioInit(void)
{
    if (!gAudioInitialized) {
        if (audsrv_init() != 0) {
            LOG("AUDIO: Failed to initialize audsrv\n");
            LOG("AUDIO: Audsrv returned error string: %s\n", audsrv_get_error_string());
            return;
        }
        gAudioInitialized = 1;
    }
}

void audioEnd(void)
{
    if (!gAudioInitialized) {
        LOG("AUDIO: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    /* Stop BGM if it is still running: bgmIsPlaying covers the normal case;
     * bgmIoThreadRunning/bgmThreadRunning cover the case where a settings-toggle
     * caused the IO thread to self-exit (setting terminateFlag=1/bgmIsPlaying=0)
     * but bgmDeinit() was never called, leaking the semaphores and thread handles. */
    if (isBgmPlaying() || bgmIoThreadRunning || bgmThreadRunning)
        bgmStop();

    audsrv_quit();
    gAudioInitialized = 0;
}

void audioSetVolume(void)
{
    int i;

    if (!gAudioInitialized) {
        LOG("AUDIO: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    for (i = 1; i < SFX_COUNT; i++)
        audsrv_adpcm_set_volume(i, gSFXVolume);

    sfxSetCursorChannelsVolume(gSFXVolume);
    audsrv_adpcm_set_volume(0, gBootSndVolume);
    audsrv_set_volume(gBGMVolume);
}
