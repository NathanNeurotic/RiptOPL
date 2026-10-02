#ifndef __GUIOVERLAY_H
#define __GUIOVERLAY_H

// What is drawn on top of every screen: the I/O busy spinner, the debug HUDs, the Last Played Auto Start countdown, and the
// sub-menu hint row.
class Overlays
{
public:
    // One frame of overlays (call after the screen has rendered).
    void draw();
    void drawBusy(int alpha);
    void drawSubMenuHints();
    // Set only for the guiShow() call inside guiRenderProbeFrame: while a probe wait owns the screen, the menu's hint row is
    // skipped so the probe's Cancel hint is the only thing drawn on the bottom-right strip (issue #465).
    void setProbeFrame(bool active) { probeFrame_ = active; }

private:
    void drawAutoStart(bool pending);
    void drawDebugHud();
    void drawArtHud();

    int busyAlpha_ = 0x00; // fully transparent
    bool probeFrame_ = false;
};

extern Overlays gOverlays;

#endif
