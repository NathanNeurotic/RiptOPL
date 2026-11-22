// HOST-TEST: sdk-headers
// Launch eligibility reads the live theme under the GUI lock, then releases it for frame rendering.
#include "check.h"
#include <tamtypes.h>
#include <string.h>
typedef struct
{
    int init_count;
    int max_count;
    int option;
} ee_sema_t;
#include "include/guiruntime.h"
#include "include/themes.h"
#include "include/themeint.h"

static int lockDepth;
static int unlockedThemeReads;
static int frames;
static int fades;
static int launchBegins;
static int launchEnds;

int testLaunchThemeStrcmp(const char *a, const char *b)
{
    if (lockDepth != 1)
        unlockedThemeReads++;
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

void Gui::lock()
{
    lockDepth++;
}

void Gui::unlock()
{
    lockDepth--;
    CHECK(lockDepth >= 0);
}

void Gui::startFrame()
{
    CHECK(lockDepth == 0);
    lock();
}

void Gui::show()
{
    CHECK(lockDepth == 1);
}

void Gui::endFrame()
{
    CHECK(lockDepth == 1);
    unlock();
    frames++;
}

void AmbientEffects::beginLaunch(clock_t now)
{
    (void)now;
    launchBegins++;
}

void AmbientEffects::endLaunch()
{
    launchEnds++;
}

void guiFadeCurrentScreenToBlack(void)
{
    CHECK(lockDepth == 0);
    fades++;
}

theme_t *gTheme;
int gEnableAmbientEffects = 1;

#define strcmp testLaunchThemeStrcmp
#include "../../src/ui/guilaunch.cpp"
#undef strcmp

static void resetCounts()
{
    lockDepth = 0;
    unlockedThemeReads = 0;
    frames = 0;
    fades = 0;
    launchBegins = 0;
    launchEnds = 0;
}

int main()
{
    image_texture_t texture = {};
    texture.name = (char *)"psbbn_bg";
    mutable_image_t image = {};
    image.defaultTexture = &texture;
    theme_element_t element = {};
    element.type = ELEM_TYPE_STATIC_IMAGE;
    element.extended = &image;
    theme_t theme = {};
    theme.mainElems.first = &element;
    gTheme = &theme;

    Gui gui;
    resetCounts();
    gui.playLaunchTransition();
    CHECK(unlockedThemeReads == 0);
    CHECK(launchBegins == 1 && launchEnds == 1);
    CHECK(frames > 1 && fades == 1);
    CHECK(lockDepth == 0);

    gEnableAmbientEffects = 0;
    resetCounts();
    gui.playLaunchTransition();
    CHECK(unlockedThemeReads == 0);
    CHECK(launchBegins == 0 && launchEnds == 0);
    CHECK(frames == 0 && fades == 1);
    CHECK(lockDepth == 0);

    TEST_DONE("GUI launch");
}
