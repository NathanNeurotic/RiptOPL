#ifndef UIDRAW_H
#define UIDRAW_H

#include <tamtypes.h>
#include "include/menusys.h"

// Draw `iconId` (theme texture) followed by the translated `textId`; returns the x after the text.
int guiDrawIconAndText(int iconId, int textId, int font, int x, int y, u64 color);
int guiDrawIconAndLiteralText(int iconId, const char *text, int font, int x, int y, u64 color); // untranslated caption
// Start x that centres a linked list of hints / aligns an array of hints (align: 1 = centre, 2 = right).
int guiAlignSubMenuHints(int hintCount, int *textID, int *iconID, int font, int width, int align);

#endif
