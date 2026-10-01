#ifndef __DIAINT_H
#define __DIAINT_H

// Internal to the dialog code (dia.cpp, diaeditors.cpp, diaitems.cpp); not for other modules.

#include "include/dia.h"

// UI spacing of the dialogues (pixels between consecutive items)
#define UI_SPACING_H 10

// Native-size screen extents the dialog drawing code works in; set when a dialog or editor starts.
extern int diaScreenWidth;
extern int diaScreenHeight;

void diaDrawBoundingBox(int x, int y, int w, int h, int focus);
int diaShowNumPad(char *text, int maxLen, int minv, int maxv);
int diaShowColSel(unsigned char *r, unsigned char *g, unsigned char *b);
struct UIItem *diaFindByID(struct UIItem *ui, int id);

#endif
