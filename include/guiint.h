#ifndef __GUIINT_H
#define __GUIINT_H

// Internals shared between the gui translation units (gui.cpp core, guisettings.cpp settings screens).
// Not part of the public gui.h API.

extern int gGuiScreenWidth;
extern int gGuiScreenHeight;

void guiStartFrame(void);
void guiEndFrame(void);
void guiDrawOverlays(void);
void guiResetNotifications(void);
void guiShow(void); // one frame of the active screen
void guiDrawBusy(int alpha);
int guiScreenInMenu(void);

// Deep copies of the NULL-terminated name lists (language/theme pickers). Defined in guisettings.cpp.
const char **guiCopyNameList(const char **src);
void guiFreeNameList(const char **list);

#endif
