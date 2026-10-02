#ifndef __SOUNDEFFECTS_H
#define __SOUNDEFFECTS_H

// Menu sound effects: adpcm one-shots loaded into audsrv, the rotating cursor channels and the play-time diagnostics. One
// instance lives in src/core/sfx.cpp; the sfx* functions in sound.h forward to it.

#include <audsrv.h>
#include "sound.h"

struct sfxEffect
{
    const char *name;
    void *buffer;
    int size;
    int builtin;
    int duration_ms;
};

class SoundEffects
{
public:
    SoundEffects() = default;
    SoundEffects(const SoundEffects &) = delete;
    SoundEffects &operator=(const SoundEffects &) = delete;

    int init(int bootSnd);
    int soundDuration(int id);
    void playDiag(unsigned int *lastMs, unsigned int *maxMs);
    void play(int id);

private:
    int readFile(const char *full_path, struct sfxEffect *sfx);
    void initDefaults(void);
    int load(struct sfxEffect *sfxData, audsrv_adpcm_t *sfx);

    struct sfxEffect files_[SFX_COUNT] = {
        {"boot.adp"},
        {"cancel.adp"},
        {"confirm.adp"},
        {"cursor.adp"},
        {"message.adp"},
        {"transition.adp"},
        {"bd_connect.adp"},
        {"bd_disconnect.adp"},
    };
    struct audsrv_adpcm_t adpcm_[SFX_COUNT] = {};
    int cursorChannel_ = 0;
    unsigned int lastPlayMs_ = 0;
    unsigned int maxPlayMs_ = 0;
};

#endif
