#ifndef __GUIRUNTIME_H
#define __GUIRUNTIME_H

#include "include/guiqueue.h"
#include "include/plasma.h"
#include "include/gui.h"

// The GUI runtime: owns the frame lock, the deferred-op queue, the background plasma and the main loop. The guiXxx functions in
// gui.h forward to the one instance (gGui).
class Gui
{
public:
    void init();
    void shutdown();

    // The frame lock. A no-op until init() has created the semaphore: lngInit/thmInit rebuild the GUI name lists BEFORE guiInit
    // (opl.c init order), and everything is single-threaded until the GUI/IO threads exist.
    void lock();
    void unlock();
    void startFrame();
    void endFrame();

    // One frame of the active screen.
    void show();
    void drawBackground();
    void reloadScreenExtents();

    void mainLoop();
    void setFrameHook(gui_callback_t hook) { frameHook_ = hook; }

    // Deferred ops: queued from any thread, applied by the GUI thread once a frame.
    int defer(struct gui_update_t *op);
    void execDeferred();
    // The boot's deferred ops have all run.
    bool initComplete() const { return initComplete_; }

    // Render ONE frame of the current screen plus the busy animation for a synchronous main-thread wait; 1 = the user cancelled.
    int renderProbeFrame();

    static struct gui_update_t *createOp(gui_op_type_t type);
    // Re-apply the Scroll Speed setting to the pad repeat delays / the aspect ratio.
    void updateScrollSpeed();
    void updateScreenScale();

    int showKeyboard(char *value, int maxLength);
    void showAbout();

private:
    void handleOp(struct gui_update_t *item);
    void pollPads();
    static void handleOpThunk(struct gui_update_t *item);

    bool terminate_ = false;
    bool initComplete_ = false;
    bool lockReady_ = false; // false = semaphore not created yet
    s32 lockSema_ = 0;
    ee_sema_t queueSema_ = {};
    gui_callback_t frameHook_ = nullptr;
    DeferredOpQueue ops_;
    PlasmaBackground plasma_;
};

extern Gui gGui;

#endif
