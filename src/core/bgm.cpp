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

// Theme background music: ogg decode thread, ring buffer and audsrv feeding. Split out of sound.cpp.

/*--    Theme Background Music    -------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------*/

constexpr int BGM_RING_BUFFER_COUNT = 192;  // 768 KB buffer (~4.35s): extra headroom for long device/list bursts (#364)
#define BGM_RING_BUFFER_SIZE       4096
constexpr int BGM_STOP_WAIT_SLICES = 16;
constexpr int BGM_IO_LOW_WATER_CHUNKS = 48;  // ~1.1 s: stop STARTING discretionary device reads
constexpr int BGM_IO_RESUME_WATER_CHUNKS = 80;  // ~1.8 s: hysteresis before artwork/cosmetic IO resumes
// EE priorities are strict (lower number wins): playback=30, Vorbis I/O/decode=31. Playback is
// tiny and normally asleep between audsrv room polls; decode now outranks the background I/O worker (32) while
// sharing the GUI/pad tier (31), whose vsync wait yields naturally. This prevents a long runnable
// background queue from starving refills; the larger ring also covers stalls that are IOP-bound,
// where EE thread priority cannot help.
constexpr int BGM_THREAD_BASE_PRIO = 0x1E;
constexpr int BGM_THREAD_STACK_SIZE = 0x1000;
// Sleep bounds while audsrv's ring has no room for the next piece (see bgmWaitForRoom). The sleep is the
// time the missing room takes to play out, but never under one SPU2 block -- 512 samples at 48 kHz,
// 10.7 ms -- because the IOP frees room a block at a time and a shorter sleep only re-reads the same
// room. A stream that stops draining is polled slowly.
constexpr int BGM_ROOM_POLL_MIN_US = 10000;
constexpr int BGM_ROOM_POLL_MAX_US = 20000;
#define BGM_ROOM_STALL_POLLS       50
#define BGM_ROOM_STALL_US          100000
// Below this the ring measurement in bgmStart is not believable, and it keeps the whole-chunk target.
// The smallest start room of any 16-bit format audsrv accepts is 1168 bytes (11025 Hz mono).
#define BGM_ROOM_TARGET_MIN        256

extern void *_gp;

static int bgmThreadID, bgmIoThreadID;
static int outSema, inSema;
// Shared between the BGM playback thread and the main thread; volatile so updates
// are observed across threads rather than cached in a register.
static volatile unsigned char terminateFlag, bgmIsPlaying;
static volatile unsigned char rdPtr, wrPtr;
static char bgmBuffer[BGM_RING_BUFFER_COUNT][BGM_RING_BUFFER_SIZE];
volatile unsigned char bgmThreadRunning, bgmIoThreadRunning;
static volatile int bgmBufferedChunks = 0;
static volatile unsigned char bgmBufferPrimed = 0;
static volatile unsigned char bgmIoThrottle = 0;

// Nonzero while bgmIoThread may be inside a device read. Read by the art worst-open latch.
volatile int gBgmInRead = 0;

static void bgmAdjustBufferedChunks(int delta)
{
    DIntr();
    bgmBufferedChunks += delta;
    if (bgmBufferedChunks <= 0) {
        bgmBufferedChunks = 0;
        bgmBufferPrimed = 0;
    } else if (bgmBufferedChunks >= BGM_RING_BUFFER_COUNT) {
        bgmBufferedChunks = BGM_RING_BUFFER_COUNT;
    }
    if (bgmBufferedChunks >= BGM_IO_RESUME_WATER_CHUNKS)
        bgmBufferPrimed = 1;
    EIntr();
}

int bgmDiscretionaryIoAllowed(void)
{
    if (!bgmIsPlaying || !gEnableBGM)
        return 1;

    if (!bgmBufferPrimed)
        return 0;

    int buffered = bgmBufferedChunks;
    if (bgmIoThrottle) {
        if (buffered >= BGM_IO_RESUME_WATER_CHUNKS)
            bgmIoThrottle = 0;
    } else if (buffered <= BGM_IO_LOW_WATER_CHUNKS) {
        bgmIoThrottle = 1;
    }

    return !bgmIoThrottle;
}

static u8 bgmThreadStack[BGM_THREAD_STACK_SIZE] __attribute__((aligned(16)));
static u8 bgmIoThreadStack[BGM_THREAD_STACK_SIZE] __attribute__((aligned(16)));

static OggVorbis_File *vorbisFile;

// PCM bytes the stream plays per millisecond, set by bgmStart from its format.
static int bgmBytesPerMs = 0;
// Most room bgmQueueChunk waits for before queueing: one chunk, or less when the audsrv ring is small.
// audsrv sizes its ring at ten SPU2 feeds of the stream format and starts a stream with only half of it
// free, so an 11025 Hz stream never has room for a whole chunk and neither does the start of a 12 kHz
// stereo or a 22/32 kHz mono one. Set by bgmStart.
static int bgmRoomTarget = BGM_RING_BUFFER_SIZE;

// Wait until audsrv has room for `want` bytes. Returns that room, or 0 once asked to terminate.
//
// NOT audsrv_wait_audio(). That RPC parks audsrv's only IOP RPC thread -- and holds the EE library's
// call mutex -- until the IOP play thread frees ring space, which it does only while SPU2 keeps
// completing block transfers. This thread sits in that wait almost all the time, so whenever
// playback stalled, every audsrv call from every other thread queued behind it for good: the menu
// thread's per-press sound effect (sfxPlay starts the rumble pulse first, so the controller kept
// vibrating on a frozen menu), bgmMute() on the way into a launch, any volume change. Polling
// audsrv_available() keeps each call a short round trip, so a stalled stream costs the music and
// nothing else, and terminateFlag is seen within one sleep.
static int bgmWaitForRoom(int want)
{
    int lastRoom = -1, samePolls = 0;

    while (!terminateFlag) {
        int room = audsrv_available();
        int us;

        if (room >= want)
            return room;

        if (room == lastRoom) {
            if (++samePolls == BGM_ROOM_STALL_POLLS)
                LOG("BGM: audsrv ring has not drained for %d polls -- playback stalled\n", samePolls);
        } else {
            samePolls = 0;
            lastRoom = room;
        }

        if (samePolls >= BGM_ROOM_STALL_POLLS) {
            us = BGM_ROOM_STALL_US;
        } else if (room >= 0 && bgmBytesPerMs > 0) {
            // The time the missing room takes to play out.
            us = ((want - room) * 1000) / bgmBytesPerMs;
            if (us < BGM_ROOM_POLL_MIN_US)
                us = BGM_ROOM_POLL_MIN_US;
            else if (us > BGM_ROOM_POLL_MAX_US)
                us = BGM_ROOM_POLL_MAX_US;
        } else {
            us = BGM_ROOM_POLL_MAX_US;
        }
        DelayThread(us);
    }
    return 0;
}

// Queue one decoded chunk, in as many pieces as the audsrv ring has room for. audsrv_play_audio keeps
// what fits and DROPS the rest, so each piece is at most the room just seen and the chunk advances by
// what was actually queued. Returns 0 once asked to terminate.
static int bgmQueueChunk(const char *chunk)
{
    int done = 0;

    while (done < BGM_RING_BUFFER_SIZE) {
        int left = BGM_RING_BUFFER_SIZE - done;
        int room = bgmWaitForRoom(left < bgmRoomTarget ? left : bgmRoomTarget);
        int sent;

        if (room <= 0)
            return 0;
        sent = audsrv_play_audio(chunk + done, room < left ? room : left);
        if (sent > 0)
            done += sent;
        else
            DelayThread(BGM_ROOM_STALL_US); // audsrv refused it; never spin at this priority
    }
    return 1;
}

static void bgmThread(void *arg)
{
    bgmThreadRunning = 1;

    while (!terminateFlag) {
        WaitSema(outSema);
        if (terminateFlag)
            break;

        bgmAdjustBufferedChunks(-1);
        if (!bgmQueueChunk(bgmBuffer[rdPtr]))
            break;
        rdPtr = (rdPtr + 1) % BGM_RING_BUFFER_COUNT;

        SignalSema(inSema);

        // If the ring buffer completely emptied, yield briefly so the decode thread can rebuild
        // a small cushion instead of stuttering chunk-by-chunk on the verge of starvation.
        if (bgmBufferedChunks <= 0 && !terminateFlag && bgmIoThreadRunning) {
            int cushionWait = 0;
            while (!terminateFlag && bgmIoThreadRunning && bgmBufferedChunks < 16 && cushionWait < 20) {
                DelayThread(5 * 1000);
                cushionWait++;
            }
        }
    }

    audsrv_stop_audio();

    rdPtr = 0;
    wrPtr = 0;

    bgmThreadRunning = 0;
    bgmIsPlaying = 0;
}

static void bgmIoThread(void *arg)
{
    int decodeTotal, bitStream;

    bgmIoThreadRunning = 1;
    do {
        WaitSema(inSema);

        if (terminateFlag || !gEnableBGM)
            break;

        decodeTotal = BGM_RING_BUFFER_SIZE;
        int bufferPtr = 0;
        gBgmInRead = 1;
        do {
            int ret = ov_read(vorbisFile, bgmBuffer[wrPtr] + bufferPtr, decodeTotal, 0, 2, 1, &bitStream);
            if (ret > 0) {
                bufferPtr += ret;
                decodeTotal -= ret;
            } else if (ret == OV_HOLE) {
                // Recoverable Vorbis discontinuity. libvorbisfile explicitly permits callers to
                // continue after OV_HOLE; treating it as a fatal device error made one stressed
                // read permanently stop BGM instead of recovering on the next packet.
                LOG("BGM: recoverable Vorbis hole; continuing.\n");
                continue;
            } else if (ret < 0) {
                LOG("BGM: fatal I/O/decode error while reading (%d).\n", ret);
                terminateFlag = 1;
                break;
            } else if (ret == 0) {
                // End of stream: loop the track
                if (ov_pcm_seek(vorbisFile, 0) != 0) {
                    LOG("BGM: cannot rewind (descriptor lost?) -- stopping.\n");
                    terminateFlag = 1;
                    break;
                }
            }
        } while (decodeTotal > 0);
        gBgmInRead = 0;

        if (terminateFlag)
            break;

        wrPtr = (wrPtr + 1) % BGM_RING_BUFFER_COUNT;
        bgmAdjustBufferedChunks(1);
        SignalSema(outSema);
    } while (!terminateFlag && gEnableBGM);

    bgmIoThreadRunning = 0;
    terminateFlag = 1;
    SignalSema(outSema);
}

/* The music file, as the decoder sees it. vorbisfile reads through these callbacks instead of a bare
   FILE*, so a stream whose descriptor dies under it can be reopened and resumed at the same byte.

   Why it dies: SD2PSX (sd2psxtd firmware, 1.4.0 and develop) keeps the descriptor of the command in
   progress in an int, but its close/read/write/lseek handlers receive the descriptor into that int's
   LOW BYTE only. A failed open or stat -- the ul.cfg probe of every MMCE refresh, a cover that does
   not exist -- stores -1 there first, so the next read or lseek of ANY file that is still open goes to
   a negative descriptor and fails. For the music that read came back empty mid-file, the decoder took
   it for the end of the stream, the rewind failed the same way, and the music stopped for good a few
   seconds into the menu. A successful open writes the whole int again, so reopening is the cure. */
constexpr int BGM_REOPEN_TRIES = 4;
constexpr int BGM_REOPEN_RETRY_US = (20 * 1000);

typedef struct
{
    FILE *f;
    long pos;  // where the decoder believes it is
    long size; // -1 when the size could not be read
    char path[256];
} bgm_source_t;

static int bgmSourceReopen(bgm_source_t *src)
{
    int tries;

    for (tries = 0; tries < BGM_REOPEN_TRIES; tries++) {
        FILE *f = fopen(src->path, "rb");
        if (f != NULL) {
            if (fseek(f, src->pos, SEEK_SET) == 0) {
                // Only now close the dead handle: the open above left the card's descriptor state
                // whole, so this close reaches the right file instead of leaking it.
                fclose(src->f);
                src->f = f;
                LOG("BGM: reopened %s at %ld\n", src->path, src->pos);
                return 1;
            }
            fclose(f);
        }
        DelayThread(BGM_REOPEN_RETRY_US);
    }
    LOG("BGM: cannot reopen %s at %ld\n", src->path, src->pos);
    return 0;
}

static size_t bgmSourceRead(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    bgm_source_t *src = (bgm_source_t *)datasource;
    int tries = 0;

    if (size == 0 || nmemb == 0)
        return 0;
    while (1) {
        size_t r = fread(ptr, size, nmemb, src->f);
        if (r > 0) {
            src->pos += (long)(r * size);
            return r;
        }
        // Nothing read. At the real end of the file that is the end of the stream; before it, the
        // descriptor is gone -- reopen and read again.
        if (src->size >= 0 && src->pos >= src->size)
            return 0;
        if (tries++ >= BGM_REOPEN_TRIES || !bgmSourceReopen(src))
            return 0;
    }
}

static int bgmSourceSeek(void *datasource, ogg_int64_t offset, int whence)
{
    bgm_source_t *src = (bgm_source_t *)datasource;
    long target, keep;

    if (whence == SEEK_SET)
        target = (long)offset;
    else if (whence == SEEK_CUR)
        target = src->pos + (long)offset;
    else if (whence == SEEK_END && src->size >= 0)
        target = src->size + (long)offset;
    else
        return -1;
    if (target < 0)
        return -1;

    if (fseek(src->f, target, SEEK_SET) != 0) {
        keep = src->pos;
        src->pos = target;
        if (!bgmSourceReopen(src)) {
            src->pos = keep;
            return -1;
        }
    }
    src->pos = target;
    return 0;
}

static long bgmSourceTell(void *datasource)
{
    return ((bgm_source_t *)datasource)->pos;
}

static int bgmSourceClose(void *datasource)
{
    bgm_source_t *src = (bgm_source_t *)datasource;
    int ret = fclose(src->f);

    free(src);
    return ret;
}

static ov_callbacks bgmSourceCallbacks = {
    bgmSourceRead,
    bgmSourceSeek,
    bgmSourceClose,
    bgmSourceTell,
};

static int bgmTryLoadPath(const char *bgmPath, const char *source)
{
    bgm_source_t *src;
    FILE *bgmFile = fopen(bgmPath, "rb");

    if (bgmFile == NULL) {
        LOG("BGM: %s BGM not found: %s\n", source, bgmPath);
        return 0;
    }

    src = static_cast<bgm_source_t*>(malloc(sizeof(bgm_source_t)));
    if (src == NULL) {
        fclose(bgmFile);
        return 0;
    }
    src->f = bgmFile;
    src->pos = 0;
    snprintf(src->path, sizeof(src->path), "%s", bgmPath);
    src->size = -1;
    if (fseek(bgmFile, 0, SEEK_END) == 0)
        src->size = ftell(bgmFile);
    if (fseek(bgmFile, 0, SEEK_SET) != 0) {
        bgmSourceClose(src);
        return 0;
    }

    if (ov_open_callbacks(src, vorbisFile, NULL, 0, bgmSourceCallbacks) == 0) {
        LOG("BGM: Loaded %s BGM %s (%ld bytes)\n", source, bgmPath, src->size);
        return 1;
    }

    // A failed ov_open_callbacks leaves the datasource to the caller.
    LOG("BGM: %s BGM is not a valid Ogg bitstream: %s\n", source, bgmPath);
    bgmSourceClose(src);
    memset(vorbisFile, 0, sizeof(OggVorbis_File));
    return 0;
}

static int bgmLoad(void)
{
    char bgmPath[256];
    int themeID;

    vorbisFile = static_cast<OggVorbis_File*>(malloc(sizeof(OggVorbis_File)));
    if (vorbisFile == NULL)
        return -ENOMEM;

    memset(vorbisFile, 0, sizeof(OggVorbis_File));

    themeID = thmGetGuiValue();
    char *thmPath = thmGetFilePath(themeID);
    if (thmPath != NULL) { // NULL for <OPL> + the built-in <Coverflow>
        snprintf(bgmPath, sizeof(bgmPath), "%ssound/bgm.ogg", thmPath);
        if (bgmTryLoadPath(bgmPath, "theme"))
            return 0;
    }

    if (gDefaultBGMPath[0] != '\0') {
        snprintf(bgmPath, sizeof(bgmPath), "%s", gDefaultBGMPath);
        if (bgmTryLoadPath(bgmPath, "configured"))
            return 0;
    }

    // Disk themes keep their existing sound/bgm.ogg -> configured-file priority. Only the two
    // built-in themes fall back to this one conventional data-home file after the configured path.
    if (thmPath == NULL && oplGetDefaultThemeBgmPath(bgmPath, sizeof(bgmPath))) {
        if (bgmTryLoadPath(bgmPath, "built-in theme"))
            return 0;
    }

    LOG("BGM: No usable theme, configured, or built-in BGM available.\n");
    free(vorbisFile);
    vorbisFile = NULL;

    return -ENOENT;
}

static int bgmInit(void)
{
    ee_thread_t thread;
    ee_sema_t sema;
    int result;

    terminateFlag = 0;
    rdPtr = 0;
    wrPtr = 0;
    bgmThreadRunning = 0;
    bgmIoThreadRunning = 0;
    bgmBufferedChunks = 0;
    bgmBufferPrimed = 0;
    bgmIoThrottle = 0;

    sema.max_count = BGM_RING_BUFFER_COUNT;
    sema.init_count = BGM_RING_BUFFER_COUNT;
    sema.attr = 0;
    sema.option = (u32) "bgm-in-sema";
    inSema = CreateSema(&sema);

    if (inSema >= 0) {
        sema.max_count = BGM_RING_BUFFER_COUNT;
        sema.init_count = 0;
        sema.attr = 0;
        sema.option = (u32) "bgm-out-sema";
        outSema = CreateSema(&sema);

        if (outSema < 0) {
            DeleteSema(inSema);
            return outSema;
        }
    } else
        return inSema;

    thread.func = reinterpret_cast<void *>(&bgmThread);
    thread.stack = bgmThreadStack;
    thread.stack_size = sizeof(bgmThreadStack);
    thread.gp_reg = &_gp;
    thread.initial_priority = BGM_THREAD_BASE_PRIO;
    thread.attr = 0;
    thread.option = 0;

    // BGM thread will start in DORMANT state.
    bgmThreadID = CreateThread(&thread);

    if (bgmThreadID >= 0) {
        thread.func = reinterpret_cast<void *>(&bgmIoThread);
        thread.stack = bgmIoThreadStack;
        thread.stack_size = sizeof(bgmIoThreadStack);
        thread.gp_reg = &_gp;
        thread.initial_priority = BGM_THREAD_BASE_PRIO + 1;
        thread.attr = 0;
        thread.option = 0;

        // BGM I/O thread will start in DORMANT state.
        bgmIoThreadID = CreateThread(&thread);
        if (bgmIoThreadID >= 0) {
            result = 0;
        } else {
            DeleteSema(inSema);
            DeleteSema(outSema);
            DeleteThread(bgmThreadID);
            result = bgmIoThreadID;
        }
    } else {
        result = bgmThreadID;
        DeleteSema(inSema);
        DeleteSema(outSema);
    }

    return result;
}

static void bgmDeinit(void)
{
    DeleteSema(inSema);
    DeleteSema(outSema);
    DeleteThread(bgmThreadID);
    DeleteThread(bgmIoThreadID);

    if (vorbisFile != NULL) {
        // Vorbisfile takes care of fclose for file-backed sources.
        ov_clear(vorbisFile);
        free(vorbisFile);
        vorbisFile = NULL;
    }
}

static void bgmShutdownDelayCallback(s32 alarm_id, u16 time, void *common)
{
    iWakeupThread((int)common);
}

void bgmStart(void)
{
    struct audsrv_fmt_t audsrvFmt;

    if (!gAudioInitialized) {
        LOG("BGM: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    int ret = bgmInit();
    if (ret >= 0) {
        if (bgmLoad() != 0) {
            bgmDeinit();
            return;
        }

        vorbis_info *vi = ov_info(vorbisFile, -1);
        ov_pcm_seek(vorbisFile, 0);

        audsrvFmt.channels = vi->channels;
        audsrvFmt.freq = vi->rate;
        audsrvFmt.bits = 16;
        bgmBytesPerMs = (int)((vi->rate * vi->channels * 2) / 1000);

        if (audsrv_set_format(&audsrvFmt) != AUDSRV_ERR_NOERROR) {
            // No audsrv upsampler for this rate (16 kHz, say): its ring would never drain. The blocking
            // wait this code used to have hung on that first chunk, holding audsrv, and the menu froze on
            // its first sound effect. Play no music instead.
            LOG("BGM: audsrv cannot play %ld Hz, %d ch -- no music\n", vi->rate, vi->channels);
            bgmDeinit();
            return;
        }
        // A new format starts with nothing queued, and audsrv drains nothing until the first chunk is
        // queued: the room now is all the first wait can ever see, and free plus queued is the whole ring.
        int room = audsrv_available();
        int ring = room + audsrv_queued();
        bgmRoomTarget = BGM_RING_BUFFER_SIZE;
        if (ring / 2 < bgmRoomTarget)
            bgmRoomTarget = ring / 2;
        if (room < bgmRoomTarget)
            bgmRoomTarget = room;
        if (bgmRoomTarget < BGM_ROOM_TARGET_MIN)
            bgmRoomTarget = BGM_RING_BUFFER_SIZE;
        LOG("BGM: %ld Hz, %d ch, audsrv ring %d bytes, queue up to %d at a time\n", vi->rate, vi->channels, ring, bgmRoomTarget);
        audsrv_set_volume(gBGMVolume);

        bgmIsPlaying = 1;

        StartThread(bgmIoThreadID, NULL);

        // Pre-buffer before starting playback thread so the ring buffer is primed and audsrv
        // never suffers from immediate underrun or device contention at startup.
        int prebufferWait = 0;
        while (!terminateFlag && bgmBufferedChunks < BGM_IO_LOW_WATER_CHUNKS && prebufferWait < 100) {
            DelayThread(5 * 1000);
            prebufferWait++;
        }

        if (!terminateFlag && bgmIoThreadRunning)
            StartThread(bgmThreadID, NULL);
    }
}

void bgmQuiesce(void)
{
    if (!gAudioInitialized)
        return;

    // MUTE FIRST, then ask the threads to wind down. Setting terminateFlag only stops the FEEDER;
    // audsrv keeps playing whatever is already queued, and the decode thread stops refilling it --
    // so the last fraction of a second loops or judders while the caller gets on with a teardown
    // that can block for seconds (see LAUNCH_IO_DRAIN_TICKS). That judder is the "audio stutters
    // when a game launches" report, and it is purely cosmetic damage done while we are on the way
    // out. Dropping the volume makes the same interval silent instead.
    //
    // Volume, not audsrv_stop_audio(): the BGM thread may be inside audsrv_play_audio right now,
    // and issuing a competing stop on the same RPC channel from this thread is exactly the class of
    // teardown race that produced the old exit-hang. Muting touches no state the thread owns, and
    // the real stop still happens in audioEnd() where it always did.
    bgmMute();

    terminateFlag = 1;
    SignalSema(inSema);
    SignalSema(outSema);
    WakeupThread(bgmThreadID);
}

void bgmStop(void)
{
    int threadId;

    if (!gAudioInitialized) {
        LOG("BGM: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    LOG("BGM: terminating threads...\n");

    terminateFlag = 1;
    SignalSema(inSema);
    SignalSema(outSema);
    WakeupThread(bgmThreadID);

    threadId = GetThreadId();
    int waits = BGM_STOP_WAIT_SLICES;
    while (bgmIoThreadRunning && waits-- > 0) {
        SetAlarm(200 * 16, &bgmShutdownDelayCallback, (void *)threadId);
        SleepThread();
    }
    waits = BGM_STOP_WAIT_SLICES;
    while (bgmThreadRunning && waits-- > 0) {
        SetAlarm(200 * 16, &bgmShutdownDelayCallback, (void *)threadId);
        SleepThread();
    }

    if (bgmIoThreadRunning || bgmThreadRunning) {
        LOG("BGM: threads did not stop (io=%d play=%d) -- abandoning\n",
            (int)bgmIoThreadRunning, (int)bgmThreadRunning);
        return;
    }

    bgmDeinit();

    LOG("BGM: stopped.\n");
}

int isBgmPlaying(void)
{
    int ret = (int)bgmIsPlaying;

    return ret;
}

// HACK: BGM stutters while perfroming certain tasks, mute during these operations and unmute once completed.
void bgmMute(void)
{
    if (gAudioInitialized)
        audsrv_set_volume(0);
}

void bgmUnMute(void)
{
    if (gAudioInitialized)
        audsrv_set_volume(gBGMVolume);
}

