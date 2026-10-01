#ifndef __BGMPLAYER_H
#define __BGMPLAYER_H

// Theme background music: an ogg decode thread feeding a ring buffer and a playback thread feeding audsrv. One
// instance lives in src/core/bgm.cpp; the bgm* functions in sound.h forward to it.

#include <vorbis/vorbisfile.h>

class BgmPlayer
{
public:
    constexpr BgmPlayer() = default;
    BgmPlayer(const BgmPlayer &) = delete;
    BgmPlayer &operator=(const BgmPlayer &) = delete;

    int discretionaryIoAllowed(void);
    void start(void);
    void quiesce(void);
    void stop(void);
    int isPlaying(void);

    // Thread bodies (entered through the trampolines in bgm.cpp).
    void threadMain(void *arg);
    void ioThreadMain(void *arg);

private:
    void adjustBufferedChunks(int delta);
    int waitForRoom(int want);
    int queueChunk(const char *chunk);
    int tryLoadPath(const char *bgmPath, const char *source);
    int load(void);
    int init(void);
    void teardown(void);

    int threadId_ = 0, ioThreadId_ = 0;
    int outSema_ = 0, inSema_ = 0;
    // Shared between the BGM playback thread and the main thread; volatile so updates are observed across threads
    // rather than cached in a register.
    volatile unsigned char terminate_ = 0, playing_ = 0;
    volatile unsigned char rdPtr_ = 0, wrPtr_ = 0;
    volatile int bufferedChunks_ = 0;
    volatile unsigned char bufferPrimed_ = 0;
    volatile unsigned char ioThrottle_ = 0;
    OggVorbis_File *vorbisFile_ = NULL;
    int bytesPerMs_ = 0;
    int roomTarget_ = 4096; // BGM_RING_BUFFER_SIZE (asserted in bgm.cpp)
};

#endif
