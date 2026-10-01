#ifndef __PAD_H
#define __PAD_H

// PAD handling

constexpr int KEY_LEFT = 1;
constexpr int KEY_DOWN = 2;
constexpr int KEY_RIGHT = 3;
constexpr int KEY_UP = 4;
constexpr int KEY_START = 5;
constexpr int KEY_R3 = 6;
constexpr int KEY_L3 = 7;
constexpr int KEY_SELECT = 8;
constexpr int KEY_SQUARE = 9;
#define KEY_CROSS    10
#define KEY_CIRCLE   11
#define KEY_TRIANGLE 12
#define KEY_R1       13
constexpr int KEY_L1 = 14;
constexpr int KEY_R2 = 15;
constexpr int KEY_L2 = 16;

int startPads();
int readPads();
void unloadPads();
void unloadPadsEx(int keepIopRpc);

int getKey(int num);

int getKeyOn(int num);
int getKeyPressed(int num);

/** Sets the repetition delay for the specified button
 * @param button id (KEY_XXX values)
 * @param btndelay the delay in miliseconds per repeat (clamped by framerate!) */
void setButtonDelay(int button, int btndelay);



/** Store's the button delay into specified integer array (has to have 16 items) */
void padStoreSettings(int *buffer);

/** Restore's the button delay from specified integer array (has to have 16 items) */
void padRestoreSettings(int *buffer);

/** During a screen transition, seed and freeze the key-on baseline with the triggering sample.
 * This prevents the trigger from replaying on the destination while preserving a different button
 * that is still held when the destination appears. */
void padFreezeEdgeBaseline(int freeze);

/** Starts a menu rumble pulse on every connected pad that has actuators.
 * @param durationMs pulse length in ms (clamped internally)
 * @param large large-engine strength 0..255; the small engine runs for the whole pulse */
void padRumble(int durationMs, int large);

/** Stops any rumble pulse in flight. Call before blocking the GUI thread -- the decay only runs
 * from readPads(), so a pulse started just before a blocking read would outlast it. */
void padRumbleFlush(void);

/** Debug HUD: longest run of polls where the pad could not be READ (freepad not ready -> SIO2 or
 * IOP-side), and longest run of polls that read fine but carried no buttons (fresh, empty sample ->
 * the press never reached freepad). Whichever grows when an input is lost names the fault's half.
 */
void padGetFaultCounters(u32 *maxNotReady, u32 *maxReadyEmpty);

/** Debug HUD: reports detected actuators and alignment status for pad 0 and pad 1. */
void padGetActuatorDiag(int *act0, int *aligned0, int *act1, int *aligned1);

#endif
