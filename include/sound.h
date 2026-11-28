#ifndef __SOUND_H
#define __SOUND_H

enum SFX {
    SFX_BOOT = 0,
    SFX_CANCEL,
    SFX_CONFIRM,
    SFX_CURSOR,
    SFX_MESSAGE,
    SFX_TRANSITION,
    SFX_BD_CONNECT,
    SFX_BD_DISCONNECT,

    SFX_COUNT
};

void audioInit(void);
void audioEnd(void);
void audioSetVolume(void);

int sfxInit(int bootSnd);
int sfxGetSoundDuration(int id);
void sfxPlay(int id);
// Per-press audsrv RPC wall time (last / max, ms) -- measured on the dispatch thread.
void sfxGetPlayDiag(unsigned int *lastMs, unsigned int *maxMs);
// Silent SFX drops (last totals): stale = cursor ticks aged out at dispatch; full = sounds of any
// id discarded because the dispatch ring was saturated. See the counters' comment in sound.c.
void sfxGetDropDiag(unsigned int *staleDrops, unsigned int *fullDrops);

void bgmStart(void);
void bgmStop(void);
// Signal the BGM threads to stop WITHOUT waiting for them. For deinit, so the decoder stops reading
// the device before that device is torn down, without putting a blocking join at the top of the
// exit path. See the comment on the definition -- this is rebuild-163's fix minus its freeze.
void bgmQuiesce(void);
int isBgmPlaying(void);
// Discretionary storage users (art, cosmetic metadata) should not START a new read while the
// decoded BGM reserve is below its low-water mark. Always true when BGM is off/not yet primed.
int bgmDiscretionaryIoAllowed(void);
void bgmMute(void);
void bgmUnMute(void);

// Per-game background music: <device>ART/<name>_BGM.ogg next to the game's cover art.
// bgmFocusGame() records the music file for the highlighted game (NULL for none). It only copies the
// path, no I/O, so it is safe to call on every focus change. bgmFocusTick() runs once per GUI frame:
// once the focus has rested for a moment (and BGM and art I/O allow it) it queues the existence check and
// the track switch on the IO worker, which plays that game's music, or the theme music when the game has
// none. Neither blocks the GUI thread. Both are GUI thread only.
void bgmFocusGame(const char *path);
void bgmFocusTick(int artBusy); // artBusy: cacheHasPendingArt(), passed in because bgm sits below gfx

/* Nonzero while the BGM decoder may be inside a device read. BGM bypasses ioman entirely and shares
 * the one process-wide fileXio channel with art, so it is invisible to ioGetPending() -- which is
 * why the art worst-open latch samples it separately. */
extern volatile int gBgmInRead;

#endif
