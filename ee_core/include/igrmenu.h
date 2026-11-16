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
    // The menu did not open: the combo leaves as before, and near the end of that reset the reason is blinked three
    // times (padhook.c IGR_Thread, docs/IGR.md). Each value is the colour blinked, GS BGCOLOR 0xBBGGRR: a table of them
    // is 16 bytes of .data that Korium's LLVM ee_core does not have. Anything above IGR_MENU_POWEROFF is one of these.
    IGR_MENU_CANNOT_DRAW = 0x808080, // grey: the menu is not there to run (RA editions: no overlay for this launch)
    IGR_MENU_NO_FRAME = 0xFF0080,    // purple: no DISPFB write recorded by the GS tracker, or a format / size the panel cannot use
    IGR_MENU_DRAW_FAILED = 0x80FF00, // mint: the first page did not go through (VIF1 did not take the lines)
    IGR_MENU_GS_BUSY = 0x00FFFF,     // yellow: the game's channel 1 or 2 transfer did not end, or its VIF1 is stopped or reading back
};

// Draws the panel over the paused game and reads the pad until a choice is made. The caller has
// stopped every game thread and held the game's interrupts; buttons() returns libpad's mask (1 = held).
int IGR_MenuRun(u16 (*buttons)(void));

#endif
