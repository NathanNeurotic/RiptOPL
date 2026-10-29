#ifndef __PADMANAGER_H
#define __PADMANAGER_H

// The pad subsystem behind pad.h: libpad ports, per-button repeat timers, the menu rumble pulse and the
// debug-HUD fault counters. One instance lives in src/core/pad.cpp; the pad.h functions forward to it.

#include <libpad.h>

constexpr int MAX_PADS = 4;

struct pad_data_t
{
    int port, slot;
    int state;
    u32 paddata;
    u32 oldpaddata;
    struct padButtonStatus buttons;

    // pad_dma_buf is provided by the user, one buf for each pad
    // contains the pad's current state
    char padBuf[256] __attribute__((aligned(64)));

    char actAlign[6];
    int actuators;
    int actAligned;  // padSetActAlign confirmed; see padRumbleRealign()
    int needsInit;   // reconnect-edge re-init pending; runs once the verified-idle gate opens (see readPads)
    int readOk;      // the last native read delivered a sample (rightStick trusts no older one)
    u32 sampleTicks; // cpu_ticks() of the last delivered sample (see PAD_MISS_HOLD_TICKS)
};

class PadManager
{
public:
    constexpr PadManager() = default;
    PadManager(const PadManager &) = delete;
    PadManager &operator=(const PadManager &) = delete;

    void faultCounters(u32 *maxNotReady, u32 *maxReadyEmpty);
    void rightStick(int *x, int *y);
    void actuatorDiag(int *act0, int *aligned0, int *act1, int *aligned1);
    int read();
    int key(int id);
    int keyOn(int id);
    int keyPressed(int id);
    void setButtonDelay(int button, int btndelay);
    void unloadEx(int keepIopRpc);
    void unload();
    int start();
    void storeSettings(int *buffer);
    void restoreSettings(int *buffer);
    void freezeEdgeBaseline(int freeze);
    void rumble(int durationMs, int large);
    void rumbleFlush(void);

private:
    int waitReady(struct pad_data_t *pad);
    int initialize(struct pad_data_t *pad);
    void updateState(struct pad_data_t *pad, int state);
    u32 readLeftJoy(struct pad_data_t *pad, u32 pdata);
    int readOne(struct pad_data_t *pad, int *pollClean);
    int keyDelay(int id, int repeat);
    void rumbleRealign(struct pad_data_t *pad);
    void actSet(struct pad_data_t *pad, int small, int large);
    void pademuStopAll(void);
    u32 pollElapsedMs(void);
    void advanceRepeatTimers(u32 elapsedMs);
    void unloadOne(struct pad_data_t *pad);
    int startOne(struct pad_data_t *pad);

    /// milliseconds between the last two polls (see pollElapsedMs)
    u32 elapsedMs_ = 0;
    // PAD FAULT DISCRIMINATOR (debug HUD): longest runs of "freepad not ready" and "ready but empty" polls.
    u32 notReadyRun_ = 0, maxNotReadyRun_ = 0;
    u32 emptyRun_ = 0, maxEmptyRun_ = 0;
    unsigned short padCount_ = 0;
    pad_data_t padData_[MAX_PADS] = {};
    // gathered pad data
    u32 paddata_ = 0;
    u32 oldpaddata_ = 0;
    // Buttons in paddata_ only because a pad missed this poll (see PAD_MISS_HOLD_TICKS).
    u32 heldUnknown_ = 0;
    int delaycnt_[16] = {};
    int paddelay_[16] = {};
    // Menu rumble pulse state (the PADEMU poll path drives the DS3/DS4/DS5 motors from it too).
    u32 rumbleStartTicks_ = 0;
    u32 rumbleDurTicks_ = 0;
    int rumbleActive_ = 0;
    int rumbleLarge_ = 0;
    int rumbleOnRepeats_ = 0;
    int rumbleOffRepeats_ = 0;
    // Milliseconds of continuously VERIFIED idle polls (see PAD_SELF_HEAL_IDLE_MS).
    u32 padIdleMs_ = 0;
    int edgeBaselineFrozen_ = 0;
};

#endif
