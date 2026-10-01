// Menu hint rendering helpers shared by the GUI and the theme renderer: draw an icon with a translated caption and
// measure/align a row of such hints. They only need the theme textures, the renderer, fonts and the language table, so
// they live with the graphics layer; gui.cpp and themes.cpp both call them.
#include "include/opl.h"
#include "include/uidraw.h"
#include "include/renderman.h"
#include "include/menusys.h"
#include "include/fntsys.h"
#include "include/lang.h"
#include "include/themes.h"

int guiDrawIconAndText(int iconId, int textId, int font, int x, int y, u64 color)
{
    GSTEXTURE *iconTex = thmGetTexture(iconId);
    int w = 0;
    int h = 20;

    if (iconTex) {
        w = (iconTex->Width * 20) / iconTex->Height;
    }

    if (iconTex && iconTex->Mem) {
        y += h >> 1;
        rmDrawPixmap(iconTex, x, y, ALIGN_VCENTER, w, h, SCALING_RATIO, gDefaultCol, 0);
        x += rmWideScale(w) + 2;
    } else {
        // HACK: font is aligned to VCENTER, the default height icon height is 20
        y += 10;
    }

    x = fntRenderString(font, x, y, ALIGN_VCENTER, 0, 0, _l(textId), color);

    return x;
}

int guiDrawIconAndLiteralText(int iconId, const char *text, int font, int x, int y, u64 color)
{
    GSTEXTURE *iconTex = thmGetTexture(iconId);
    int w = 0;
    int h = 20;

    if (iconTex) {
        w = (iconTex->Width * 20) / iconTex->Height;
    }

    if (iconTex && iconTex->Mem) {
        y += h >> 1;
        rmDrawPixmap(iconTex, x, y, ALIGN_VCENTER, w, h, SCALING_RATIO, gDefaultCol, 0);
        x += rmWideScale(w) + 2;
    } else {
        y += 10;
    }

    x = fntRenderString(font, x, y, ALIGN_VCENTER, 0, 0, text, color);

    return x;
}

int guiAlignMenuHints(menu_hint_item_t *hint, int font, int width)
{
    int screenWidth, screenHeight;
    rmGetScreenExtents(&screenWidth, &screenHeight);
    int x = screenWidth;
    int w;

    for (; hint; hint = hint->next) {
        GSTEXTURE *iconTex = thmGetTexture(hint->icon_id);
        // A disk theme with use_default=0 that omits circle.png/cross.png leaves this texture NULL
        // (thmGetTexture's documented contract); guiDrawIconAndText in this same file already guards.
        w = iconTex ? (iconTex->Width * 20) / iconTex->Height : 20;
        char *text = _l(hint->text_id);

        x -= rmWideScale(w) + 2;
        x -= rmUnScaleX(fntCalcDimensions(font, text));
        if (hint->next != NULL)
            x -= width;
    }

    // align center
    x /= 2;

    return x;
}

int guiAlignSubMenuHints(int hintCount, int *textID, int *iconID, int font, int width, int align)
{
    int screenWidth, screenHeight;
    rmGetScreenExtents(&screenWidth, &screenHeight);
    int x = screenWidth;
    int i, w;

    for (i = 0; i < hintCount; i++) {
        GSTEXTURE *iconTex = thmGetTexture(iconID[i]);
        // A disk theme with use_default=0 that omits circle.png/cross.png leaves this texture NULL
        // (thmGetTexture's documented contract); guiDrawIconAndText in this same file already guards.
        w = iconTex ? (iconTex->Width * 20) / iconTex->Height : 20;
        char *text = _l(textID[i]);

        x -= rmWideScale(w) + 2;
        x -= rmUnScaleX(fntCalcDimensions(font, text));
        if (i != (hintCount - 1))
            x -= width;
    }

    if (align == 1) // align center
        x /= 2;

    if (align == 2) // align right
        x -= 20;

    return x;
}
