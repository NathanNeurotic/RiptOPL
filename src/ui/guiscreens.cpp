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
    // The game list and its Info page are both theme pages over the same background: only what is in front of it
    // fades, and quicker, instead of the whole screen going to black and back (the screens' own content fade,
    // menuRenderElements). The other screens draw a different background, so they keep the dip through black.
    const Handler *mainPage = &screenHandlers[GUI_SCREEN_MAIN], *infoPage = &screenHandlers[GUI_SCREEN_INFO];
    contentOnly_ = (current() == mainPage && target_ == infoPage) || (current() == infoPage && target_ == mainPage);
}

// renders the screen and handles inputs. Also handles screen transitions between numerous
// screen handlers. Fade transition code written by Maximus32
void ScreenManager::show(const PlasmaBackground &plasma)
{
    // is there a transmission effect going on or are
    // we in a normal rendering state?
    if (target_ && contentOnly_) {
        // 8 frames out, 8 in (about a quarter of a second), the content alone; the background stays.
        const int half = 8;
        float t = transIndex_ < half ? (float)(transIndex_ + 1) / half : (float)(2 * half - transIndex_) / half;
        fade_ = (u8)((1.0f - plasma.ease(t)) * 0x80);
        if (held_ < fade_)
            fade_ = held_;
        rise_ = 0.0f;
        (transIndex_ < half ? current() : target_)->renderScreen();
        fade_ = 0x80;
        if (++transIndex_ >= 2 * half) {
            current_ = target_;
            target_ = nullptr;
        }
    } else if (target_) {
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
        if (held_ < 0x80) {
            fade_ = held_;
            rise_ = 0.0f;
        }
        // render with the set screen handler
        current()->renderScreen();
        fade_ = 0x80;
        rise_ = 0.0f;
    }
}

void ScreenManager::dispatchInput()
{
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

u8 guiContentFade(float *rise)
{
    return gScreens.contentFade(rise);
}
