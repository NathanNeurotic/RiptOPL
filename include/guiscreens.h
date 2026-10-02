#ifndef __GUISCREENS_H
#define __GUISCREENS_H

class PlasmaBackground;

// The screen handlers (main, menu, info, game menu, app menu) and the fade transition between them.
class ScreenManager
{
public:
    struct Handler
    {
        void (*handleInput)(void);
        void (*renderScreen)(void);
        short inMenu;
    };

    // Whether the active screen is one of the in-menu screens (dialogs use it to pick their backdrop).
    bool inMenu();
    // Jump straight to a screen without the fade.
    void setCurrent(int screen);
    // Start the fade to `screen`; ignored while one is already running (or it could loop forever).
    void switchTo(int screen);
    bool transitioning() const { return target_ != nullptr; }
    // Render the active screen, or the fade between two; `plasma` supplies the ease curve.
    void show(const PlasmaBackground &plasma);
    // Run the active screen's input handler unless a fade is running.
    void dispatchInput();

private:
    const Handler *current();

    const Handler *current_ = nullptr; // lazily the menu screen
    const Handler *target_ = nullptr;
    int transIndex_ = 0;
};

extern ScreenManager gScreens;

#endif
