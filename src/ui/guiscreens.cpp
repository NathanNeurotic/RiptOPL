/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */

#include "include/opl.h"
#include "include/gui.h"
#include "include/guiint.h"
#include "include/guiscreens.h"
#include "include/plasma.h"
#include "include/menusys.h"
#include "include/renderman.h"
#include "include/sound.h"

ScreenManager gScreens;

static const int DIP_HALF = 8; // frames to black in a dip (hold L3 layout switch); the fade back takes one less

static const ScreenManager::Handler screenHandlers[] = {
    {&menuHandleInputMain, &menuRenderMain, 0},
    {&menuHandleInputMenu, &menuRenderMenu, 1},
    {&menuHandleInputInfo, &menuRenderInfo, 1},
    {&menuHandleInputGameMenu, &menuRenderGameMenu, 1},
    {&menuHandleInputAppMenu, &menuRenderAppMenu, 1},
#ifdef RETROACHIEVEMENTS
    {&menuHandleInputAchievements, &menuRenderAchievements, 1},
#endif
};

const ScreenManager::Handler *ScreenManager::current()
{
    if (current_ == nullptr)
        current_ = &screenHandlers[GUI_SCREEN_MENU]; // default screen handler (menu screen)
    return current_;
}

bool ScreenManager::inMenu()
{
    return current()->inMenu != 0;
}

void ScreenManager::setCurrent(int screen)
{
    current_ = &screenHandlers[screen];
}

void ScreenManager::switchTo(int screen)
{
    // Only initiate the transition once or else we could get stuck in an infinite loop.
    if (target_ != nullptr)
        return;
    sfxPlay(SFX_TRANSITION);
    transIndex_ = 0;
    target_ = &screenHandlers[screen];
}

// renders the screen and handles inputs. Also handles screen transitions between numerous
// screen handlers. Fade transition code written by Maximus32
void ScreenManager::show(const PlasmaBackground &plasma)
{
    // is there a transmission effect going on or are
    // we in a normal rendering state?
    if (target_) {
        u8 alpha;
        const u8 transition_frames = 26;
        if (transIndex_ < (transition_frames / 2)) {
            // Fade-out old screen
            // index: 0..7
            // alpha: 1..8 * transition_step
            current()->renderScreen();
            alpha = plasma.ease((float)(transIndex_ + 1) / (transition_frames / 2)) * 0x80;
        } else {
            // Fade-in new screen
            // index: 8..15
            // alpha: 8..1 * transition_step
            target_->renderScreen();
            alpha = plasma.ease((float)(transition_frames - transIndex_) / (transition_frames / 2)) * 0x80;
        }

        // Overlay the actual "fade"
        rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, GS_SETREG_RGBA(0x00, 0x00, 0x00, alpha));

        // Advance the effect
        transIndex_++;
        if (transIndex_ >= transition_frames) {
            current_ = target_;
            target_ = nullptr;
        }
    } else {
        // render with the set screen handler
        current()->renderScreen();
        if (dipIndex_ != 0) {
            // Out over DIP_HALF frames to black, the change, then back in over DIP_HALF - 1 frames.
            const int i = dipIndex_;
            const float t = (float)(i <= DIP_HALF ? i : 2 * DIP_HALF - i) / DIP_HALF;
            rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, GS_SETREG_RGBA(0x00, 0x00, 0x00, (u8)(plasma.ease(t) * 0x80)));
            dipIndex_ = i + 1 < 2 * DIP_HALF ? i + 1 : 0;
        }
    }
}

void ScreenManager::dipTo(void (*change)(void))
{
    if (target_ || dipIndex_ != 0)
        return;
    dipChange_ = change;
    dipIndex_ = 1;
}


void ScreenManager::dispatchInput()
{
    if (dipIndex_ != 0) {
        // The black frame is on screen: make the change now, between frames, so the next frame shows it.
        if (dipChange_ != nullptr && dipIndex_ > DIP_HALF) {
            void (*change)(void) = dipChange_;
            dipChange_ = nullptr;
            change();
        }
        return;
    }
    if (!target_ && current())
        current()->handleInput();
}

// The C-style entry points other files use.
int guiScreenInMenu(void)
{
    return gScreens.inMenu();
}

void guiSwitchScreen(int target)
{
    gScreens.switchTo(target);
}

void guiDipScreen(void (*change)(void))
{
    gScreens.dipTo(change);
}
