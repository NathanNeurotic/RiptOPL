#include "include/guiruntime.h"
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
        guiFadeCurrentScreenToBlack();
        ambient_.endLaunch();
        return;
    }
    guiFadeCurrentScreenToBlack();
}
