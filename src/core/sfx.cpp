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
#include "include/soundeffects.h"
#include "include/opl.h"
#include "include/ioman.h"
#include "include/themes.h"
#include "include/pad.h"

/*--    Theme Sound Effects    ----------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------*/

// Sound effects (adpcm one-shots, cursor channels, menu rumble). Split out of sound.cpp.

extern unsigned char boot_adp[];
extern unsigned int size_boot_adp;

extern unsigned char cancel_adp[];
extern unsigned int size_cancel_adp;

extern unsigned char confirm_adp[];
extern unsigned int size_confirm_adp;

extern unsigned char cursor_adp[];
extern unsigned int size_cursor_adp;

extern unsigned char message_adp[];
extern unsigned int size_message_adp;

extern unsigned char transition_adp[];
extern unsigned int size_transition_adp;

extern unsigned char bd_connect_adp[];
extern unsigned int size_bd_connect_adp;

extern unsigned char bd_disconnect_adp[];
extern unsigned int size_bd_disconnect_adp;




#define CURSOR_SFX_CHANNEL_BASE  SFX_COUNT
constexpr int CURSOR_SFX_CHANNEL_COUNT = 6;


static int sfxGetCursorChannel(int slot)
{
    return CURSOR_SFX_CHANNEL_BASE + slot;
}

void sfxSetCursorChannelsVolume(int volume)
{
    for (int i = 0; i < CURSOR_SFX_CHANNEL_COUNT; i++)
        audsrv_adpcm_set_volume_and_pan(sfxGetCursorChannel(i), volume, 0);
}

// COALESCE the one-shot UI sounds instead of interrupting them.
//
// #575 is two of these raised on the SAME frame -- confirming a rename plays SFX_CONFIRM in
// diaShowKeyb and then SFX_TRANSITION from the guiSwitchScreen right after it -- which stacked into
// one distorted noise. Sharing a channel fixed that but broke worse: audsrv has no way to stop an
// adpcm voice, so replaying a BUSY channel left it wedged and the NEXT press went silent (#504, and
// #575's own follow-up).
//
// So never interrupt. Each sound keeps its own channel, and a one-shot arriving within a frame of
// the previous one is simply dropped -- that is the double-raise, and one sound is what was asked
// for either way. Deliberate presses are far more than a frame apart and all still play.
constexpr int SFX_ONESHOT_COALESCE_MS = 50;

static int sfxIsUiOneShot(int id)
{
    return (id == SFX_CONFIRM) || (id == SFX_CANCEL) || (id == SFX_MESSAGE) || (id == SFX_TRANSITION);
}

// Returns 0 if the specified file was read. The sfxEffect structure will not be updated unless the file is successfully read.
int SoundEffects::readFile(const char *full_path, struct sfxEffect *sfx)
{
    int adpcm;
    void *buffer;
    int ret, size;

    LOG("SFX: readFile('%s')\n", full_path);

    adpcm = open(full_path, O_RDONLY);
    if (adpcm < 0) {
        LOG("SFX: %s: Failed to open adpcm file %s\n", __FUNCTION__, full_path);
        return -ENOENT;
    }

    size = lseek(adpcm, 0, SEEK_END);
    if (size <= 0) {
        LOG("SFX: lseek failed or empty file: %s\n", full_path);
        close(adpcm);
        return -EIO;
    }
    lseek(adpcm, 0L, SEEK_SET);

    buffer = memalign(64, size);
    if (buffer == NULL) {
        LOG("SFX: Failed to allocate memory for SFX\n");
        close(adpcm);
        return -ENOMEM;
    }

    ret = read(adpcm, buffer, size);
    close(adpcm);

    if (ret != size) {
        LOG("SFX: Failed to read SFX: %d (expected %d)\n", ret, size);
        free(buffer);
        return -EIO;
    }

    sfx->buffer = buffer;
    sfx->size = size;
    sfx->builtin = 0;

    return 0;
}

static int sfxCalculateSoundDuration(int nSamples)
{
    float sampleRate = 44100; // 44.1kHz

    // Return duration in milliseconds
    return (nSamples / sampleRate) * 1000;
}

void SoundEffects::initDefaults(void)
{
    int i;

    for (i = 0; i < SFX_COUNT; i++)
        files_[i].builtin = 1;

    files_[SFX_BOOT].buffer = boot_adp;
    files_[SFX_BOOT].size = size_boot_adp;
    files_[SFX_CANCEL].buffer = cancel_adp;
    files_[SFX_CANCEL].size = size_cancel_adp;
    files_[SFX_CONFIRM].buffer = confirm_adp;
    files_[SFX_CONFIRM].size = size_confirm_adp;
    files_[SFX_CURSOR].buffer = cursor_adp;
    files_[SFX_CURSOR].size = size_cursor_adp;
    files_[SFX_MESSAGE].buffer = message_adp;
    files_[SFX_MESSAGE].size = size_message_adp;
    files_[SFX_TRANSITION].buffer = transition_adp;
    files_[SFX_TRANSITION].size = size_transition_adp;
    files_[SFX_BD_CONNECT].buffer = bd_connect_adp;
    files_[SFX_BD_CONNECT].size = size_bd_connect_adp;
    files_[SFX_BD_DISCONNECT].buffer = bd_disconnect_adp;
    files_[SFX_BD_DISCONNECT].size = size_bd_disconnect_adp;
}

// Returns 0 (AUDSRV_ERR_NOERROR) if the sound was loaded successfully.
int SoundEffects::load(struct sfxEffect *sfxData, audsrv_adpcm_t *sfx)
{
    int ret;

    // Calculate duration based on number of samples. The sample count lives at
    // u32 offset 3 (bytes 12-15); guard against a SFX file shorter than that
    // header so we don't read past the buffer.
    if (sfxData->size >= (int)(4 * sizeof(u32)))
        sfxData->duration_ms = sfxCalculateSoundDuration(((u32 *)sfxData->buffer)[3]);
    else
        sfxData->duration_ms = 0;
    // Estimate duration based on filesize, if the ADPCM header was 0
    if (sfxData->duration_ms == 0)
        sfxData->duration_ms = sfxData->size / 47;

    ret = audsrv_load_adpcm(sfx, sfxData->buffer, sfxData->size);
    if (sfxData->builtin == 0) {
        free(sfxData->buffer);
        sfxData->buffer = NULL; // Mark the buffer as freed.
    }

    return ret;
}

// Returns number of audio files successfully loaded, < 0 if an unrecoverable error occurred.
int SoundEffects::init(int bootSnd)
{
    char sound_path[256];
    char full_path[256];
    int ret, loaded;
    int thmSfxEnabled = 0;
    int i = 1;

    if (!gAudioInitialized) {
        LOG("SFX: %s: ERROR: not initialized!\n", __FUNCTION__);
        return -1;
    }

    audsrv_adpcm_init();
    cursorChannel_ = 0;
    initDefaults();
    audioSetVolume();

    // Check default theme is not current theme
    int themeID = thmGetGuiValue();
    char *thmPath = thmGetFilePath(themeID);
    if (thmPath != NULL) { // NULL for <OPL> + the built-in <Coverflow> -> use default adpcm_
        // Get theme path for adpcm_
        snprintf(sound_path, sizeof(sound_path), "%ssound", thmPath);

        // Check for custom adpcm_ folder
        DIR *dir = opendir(sound_path);
        if (dir != NULL) {
            thmSfxEnabled = 1;
            closedir(dir);
        }
    }

    loaded = 0;
    i = bootSnd ? 0 : 1;
    for (; i < SFX_COUNT; i++) {
        if (thmSfxEnabled) {
            snprintf(full_path, sizeof(full_path), "%s/%s", sound_path, files_[i].name);
            ret = readFile(full_path, &files_[i]);
            if (ret != 0) {
                LOG("SFX: %s could not be loaded. Using default sound %d.\n", full_path, ret);
            }
        } else
            snprintf(full_path, sizeof(full_path), "builtin/%s", files_[i].name);

        ret = load(&files_[i], &adpcm_[i]);
        if (ret == 0) {
            LOG("SFX: Loaded %s, size=%d, duration=%dms\n", full_path, files_[i].size, files_[i].duration_ms);
            loaded++;
        } else {
            LOG("SFX: failed to load %s, error %d\n", full_path, ret);
        }
    }

    return loaded;
}

int SoundEffects::soundDuration(int id)
{
    if (!gAudioInitialized) {
        LOG("SFX: %s: ERROR: not initialized!\n", __FUNCTION__);
        return 0;
    }

    return files_[id].duration_ms;
}

// Debug-Colors instrumentation (#271/#272): per-press audsrv RPC wall time. audsrv_ch_play_adpcm
// is a SYNCHRONOUS SIF RPC on the GUI thread (one per nav press); under IOP contention it stalls
// the menu mid-navigation. Always measured (two tick reads), rendered only when Settings ->
// Debug Colors is on (gui.c / dia.c).
constexpr int SFX_CLOCKS_PER_MS = 147456;  // EE cpu_ticks() rate

/* MENU RUMBLE TIMING -- the RETROLauncher model, which is the reference for feedback that actually
   reads as feedback on hardware. Two shapes, and the split matters more than either value:

     NAVIGATION is a LONG, SOFT big-motor thump. Length is what makes it perceptible -- a DualShock's
     eccentric mass needs roughly 50-100 ms just to reach amplitude you can feel, so a short pulse is
     inaudible to the hand no matter how hard it is driven. This was previously 35 ms at 64/255,
     i.e. below the motor's spin-up time: correct commands, no sensation.

     DECISION is a SHORTER, HARDER bump. It happens once, so it can afford to be crisp.

   The old values had this backwards -- the constant, per-step event was the short weak one and the
   one-shot events were the long strong ones.

   Held navigation cannot simply repeat the full thump: steps arrive faster than 240 ms and the
   feedback smears into one continuous drone. Repeats are therefore scaled to a fraction of the
   OBSERVED step interval, with a floor below which we stay silent rather than buzz. */
constexpr int RUMBLE_NAV_MS = 240;
constexpr int RUMBLE_NAV_LEVEL = 0x50;
constexpr int RUMBLE_NAV_REPEAT_WINDOW_MS = 400;
constexpr int RUMBLE_NAV_REPEAT_DUTY_PCT = 45;
constexpr int RUMBLE_NAV_MIN_MS = 80;
constexpr int RUMBLE_DECISION_MS = 110;
constexpr int RUMBLE_DECISION_LEVEL = 0x60;

void SoundEffects::playDiag(unsigned int *lastMs, unsigned int *maxMs)
{
    if (lastMs)
        *lastMs = lastPlayMs_;
    if (maxMs)
        *maxMs = maxPlayMs_;
}

void sfxGetDropDiag(unsigned int *staleDrops, unsigned int *fullDrops)
{
    if (staleDrops)
        *staleDrops = 0;
    if (fullDrops)
        *fullDrops = 0;
}

static void sfxRumble(int id)
{
    if (!gEnableRumble)
        return;

    switch (id) {
        case SFX_CURSOR: {
            // AN ERM HAS TO SPIN UP BEFORE IT CAN BE FELT. This was a 35 ms pulse at 64/255, and a
            // DualShock's eccentric mass needs roughly 50-100 ms just to reach perceptible
            // amplitude -- the stop command arrived before the motor had meaningfully moved, so
            // navigation rumble was inaudible to the hand even when every command was delivered
            // correctly. Length matters more than strength here.
            //
            // The old fixed 80 ms lockout also cannot survive a longer pulse: a held scroll would
            // re-arm faster than the pulse ends and the "thump per step" becomes one continuous
            // buzz. So scale to the OBSERVED step interval instead -- the first step of a scroll
            // gets the full thump, measured repeats ride at a fraction of their own cadence, and
            // steps coming in faster than the motor can answer stay silent rather than smearing
            // into a drone.
            static u32 lastCursorRumbleTicks;
            static int haveCursorInterval;
            u32 now = cpu_ticks();
            int durationMs = RUMBLE_NAV_MS;

            if (haveCursorInterval) {
                u32 intervalMs = (now - lastCursorRumbleTicks) / SFX_CLOCKS_PER_MS;
                if (intervalMs < RUMBLE_NAV_REPEAT_WINDOW_MS) {
                    int dutyMs = (int)((intervalMs * RUMBLE_NAV_REPEAT_DUTY_PCT) / 100);
                    if (dutyMs < RUMBLE_NAV_MIN_MS)
                        return; // faster than an ERM can answer -- a buzz, not feedback
                    if (dutyMs < durationMs)
                        durationMs = dutyMs;
                }
            }

            lastCursorRumbleTicks = now;
            haveCursorInterval = 1;
            padRumble(durationMs, RUMBLE_NAV_LEVEL);
            break;
        }
        // One-shot events all share the decision bump. RETROLauncher does not give each of these its
        // own signature, and inventing six was how they ended up longer and stronger than the
        // navigation thump they are supposed to punctuate.
        case SFX_CONFIRM:
        case SFX_CANCEL:
        case SFX_MESSAGE:
        case SFX_TRANSITION:
        case SFX_BD_CONNECT:
        case SFX_BD_DISCONNECT:
            padRumble(RUMBLE_DECISION_MS, RUMBLE_DECISION_LEVEL);
            break;
        default:
            break;
    }
}

void SoundEffects::play(int id)
{
    int channel;

    sfxRumble(id);

    if (!gAudioInitialized) {
        LOG("SFX: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    if (gEnableSFX) {
        u32 sfxStartTicks = cpu_ticks();
        if (id == SFX_CURSOR) {
            static u32 lastCursorTicks = 0;
            if (lastCursorTicks != 0) {
                u32 interval = (sfxStartTicks - lastCursorTicks) / SFX_CLOCKS_PER_MS;
                if (interval < 45)
                    return;
            }
            lastCursorTicks = sfxStartTicks;

            int chosenSlot = cursorChannel_;

            cursorChannel_ = (cursorChannel_ + 1) % CURSOR_SFX_CHANNEL_COUNT;
            channel = sfxGetCursorChannel(chosenSlot);

            // Volumes are configured once by audioSetVolume(). Replaying a
            // rotation channel replaces its old sample in one SIF RPC.
            audsrv_ch_play_adpcm(channel, &adpcm_[id]);
        } else {
            if (sfxIsUiOneShot(id)) { // see SFX_ONESHOT_COALESCE_MS
                static u32 lastOneShotTicks = 0;
                if (lastOneShotTicks != 0 &&
                    (sfxStartTicks - lastOneShotTicks) / SFX_CLOCKS_PER_MS < SFX_ONESHOT_COALESCE_MS)
                    return;
                lastOneShotTicks = sfxStartTicks;
            }
            audsrv_ch_play_adpcm(id, &adpcm_[id]);
        }

        unsigned int costMs = (unsigned int)((cpu_ticks() - sfxStartTicks) / SFX_CLOCKS_PER_MS);
        lastPlayMs_ = costMs;
        if (costMs > maxPlayMs_)
            maxPlayMs_ = costMs;
    }
}

static SoundEffects gSfx;

// ---- sound.h API ----------------------------------------------------------------------------

int sfxInit(int bootSnd)
{
    return gSfx.init(bootSnd);
}

int sfxGetSoundDuration(int id)
{
    return gSfx.soundDuration(id);
}

void sfxGetPlayDiag(unsigned int *lastMs, unsigned int *maxMs)
{
    gSfx.playDiag(lastMs, maxMs);
}

void sfxPlay(int id)
{
    gSfx.play(id);
}
