/*
  igrmenu.h the in-game menu (RIPPS2's, by akilluminati47); see src/igrmenu.c.
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#ifndef IGRMENU_H
#define IGRMENU_H

#include <tamtypes.h>

enum {
    IGR_MENU_RESUME = 0, // back to the game
    IGR_MENU_EXIT,       // EXIT GAME, or the power button twice: the IGR path, as the combo always did
    IGR_MENU_POWEROFF,   // POWER OFF, or the power button once
    // The menu did not open: the combo leaves as before. Each answer is the colour its reason is blinked in (GS BGCOLOR,
    // 0xBBGGRR: padhook.c IGR_Thread, docs/IGR.md), so no table of them is kept. Anything above IGR_MENU_POWEROFF is one.
    IGR_MENU_CANNOT_DRAW = 0x808080, // grey: the menu is not there to run (RA editions: no overlay for this launch)
    IGR_MENU_NO_FRAME = 0xFF0080,    // purple: no DISPFB write recorded by the GS tracker, or a format / size the panel cannot use
    IGR_MENU_DRAW_FAILED = 0x80FF00, // mint: the first page did not go through (VIF1 did not take the lines)
    // The idle check refused the menu
    IGR_MENU_GS_BUSY = 0x00FFFF,     // yellow: a transfer of the game's on channel 1 (VIF1) or 2 (GIF) did not end, or a MFIFO did not empty
    IGR_MENU_VIF_STALLED = 0x0000FF, // red: both ended, VIF1's status alone refused
};

// Draws the panel over the paused game and reads the pad until a choice is made. The caller has
// stopped every game thread and held the game's interrupts; buttons() returns libpad's mask (1 = held).
int IGR_MenuRun(u16 (*buttons)(void));

#endif
