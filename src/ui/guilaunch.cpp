#include "include/guiruntime.h"
#include "include/guiint.h" // gGuiScreenWidth, gGuiScreenHeight
#include "include/guiscreens.h"
#include "include/menusys.h"
#include "include/renderman.h"
#include "include/themes.h"
#include "include/themeint.h"

#include <string.h>
#include <time.h>

static GSTEXTURE *ambientBackdropTexture()
{
    if (gTheme == NULL)
        return NULL;
    for (theme_element_t *elem = gTheme->mainElems.first; elem != NULL; elem = elem->next) {
        if (elem->type != ELEM_TYPE_STATIC_IMAGE || elem->extended == NULL)
            continue;
        mutable_image_t *image = (mutable_image_t *)elem->extended;
        if (image->defaultTexture != NULL && !strcmp(image->defaultTexture->name, "psbbn_bg"))
            return &image->defaultTexture->source;
    }
    return NULL;
}

void Gui::playLaunchTransition()
{
    lock();
    const bool converge = gEnableAmbientEffects && ambientBackdropTexture() != NULL;
    unlock();

    if (converge) {
        clock_t started = clock();
        ambient_.beginLaunch(started);
        for (;;) {
            startFrame();
            show();
            endFrame();
            if ((u32)clock() - (u32)started >= (u32)AmbientEffects::kLaunchUs)
                break;
        }
        playLaunchCover();
        ambient_.endLaunch();
        return;
    }
    playLaunchCover();
}

// The game's cover steps forward: the page's content fades out over its background while the selected game's cover
// and name come up in the middle of the screen, hold, and the screen fades to black for the hand-off. A counted
// loop of frames that waits on nothing; it draws only a cover already in memory (the art worker is held by now)
// and is the plain fade to black when there is none.
static const int LAUNCH_IN = 10, LAUNCH_HOLD = 10, LAUNCH_OUT = 8;

void Gui::playLaunchCover()
{
    for (int frame = 1; frame <= LAUNCH_IN + LAUNCH_HOLD + LAUNCH_OUT; ++frame) {
        const float in = frame < LAUNCH_IN ? plasma_.ease((float)frame / LAUNCH_IN) : 1.0f;
        startFrame();
        gScreens.holdContentFade((u8)((1.0f - in) * 0x80));
        show();
        gScreens.holdContentFade(0x80);
        const int drawn = menuRenderLaunchCover(in);
        if (!drawn && frame == 1) { // no cover in memory: today's fade, nothing else
            endFrame();
            guiFadeCurrentScreenToBlack();
            return;
        }
        const int out = frame - LAUNCH_IN - LAUNCH_HOLD;
        if (out > 0)
            rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, GS_SETREG_RGBA(0, 0, 0, (out * 0x80) / LAUNCH_OUT));
        endFrame();
    }
}
