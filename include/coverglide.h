#ifndef __COVERGLIDE_H
#define __COVERGLIDE_H

// Hold L3 layout switch, after NULD's view switch (danielnuld, github.com/danielnuld/nuld): every cover on screen
// flies from where it was to where the new layout puts it, closing 16 % of the remaining way each frame, the covers
// starting in a wave out from the selected game (2 frames per step away, at most 20). A cover the old layout did not
// show fades in where it lands. Nothing else on the page moves or fades.
//
// The four places that draw a game's cover (Grid, Coverflow, PSBBN Flow, a list theme's cover picture) hand their box
// to coverGlide every frame. Boxes are centres and sizes in the units the draw call takes (ALIGN_CENTER,
// SCALING_RATIO). Covers are matched by game (title and id), so no layout needs to know about another.

typedef struct
{
    float x, y, w, h;
} glide_box_t;

// Once per frame of the main page, before its elements draw.
void coverGlideFrame(void);
// Hold L3, just before the layout changes: the covers drawn on the last frame start from where they were.
void coverGlideStart(void);
// While a glide runs (from coverGlideStart until every cover has settled).
int coverGlideActive(void);
// A cover about to be drawn at `box`, `steps` away from the selected game (0 = the selected one). Records the box;
// while a glide runs, replaces it with where the cover is on its way and returns the alpha (0..0x80) to draw it at.
// Returns 0x80 and leaves `box` alone otherwise.
int coverGlide(const char *title, int id, int steps, glide_box_t *box);

#endif
