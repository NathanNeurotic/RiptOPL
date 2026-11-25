/*
 Hold L3 layout switch: covers fly from their old place to their new one, after NULD's view switch
 (danielnuld, github.com/danielnuld/nuld, launcher.c ease_to / animate). See include/coverglide.h.
 Licenced under Academic Free License version 3.0
 */

#include "include/coverglide.h"

#include <math.h>
#include <stdint.h>

#define GLIDE_COVERS   48    // more than any layout shows at once (Grid 7x2 is 14, plus the page that slides out)
#define GLIDE_RATE     0.16f // NULD's rate while switching
#define GLIDE_WAVE     2     // frames per step away from the selected game before a cover moves
#define GLIDE_WAVE_MAX 10    // steps; so no cover waits more than 20 frames
#define GLIDE_FRAMES   120   // a glide never runs longer than this, whatever a theme does

typedef struct
{
    uint32_t key;     // 0 = free
    uint32_t seen;    // the frame its box was last handed in
    uint32_t stepped; // the frame it last moved, so a cover drawn twice in a frame moves once
    glide_box_t to;   // where its layout puts it
    glide_box_t at;   // where it is on its way
    float alpha;      // 0..1; a cover the old layout did not show fades in
    int wave;         // frames still to wait; -1 until its first draw in this glide
} glide_cover_t;

static glide_cover_t covers[GLIDE_COVERS];
static uint32_t now = 1, started;
static int active, begun, moving, movedLast; // begun: the new layout has drawn a cover

static uint32_t glideKey(const char *title, int id)
{
    uint32_t h = 2166136261u;
    if (title != NULL)
        for (const unsigned char *p = (const unsigned char *)title; *p; p++)
            h = (h ^ *p) * 16777619u;
    h ^= (uint32_t)id * 2654435761u;
    return h ? h : 1;
}

static glide_cover_t *glideFind(uint32_t key)
{
    for (int i = 0; i < GLIDE_COVERS; i++)
        if (covers[i].key == key)
            return &covers[i];
    return NULL;
}

// A free entry, else the one handed in longest ago.
static glide_cover_t *glideNew(uint32_t key)
{
    glide_cover_t *e = &covers[0];
    for (int i = 0; i < GLIDE_COVERS && e->key != 0; i++)
        if (covers[i].key == 0 || covers[i].seen < e->seen)
            e = &covers[i];
    e->key = key;
    e->stepped = 0;
    e->alpha = 1.0f;
    e->wave = -1;
    return e;
}

static void easeTo(float *v, float to, float snap)
{
    *v += (to - *v) * GLIDE_RATE;
    if (fabsf(to - *v) < snap)
        *v = to;
}

void coverGlideFrame(void)
{
    now++;
    movedLast = moving;
    moving = 0;
    // Done once no cover moved last frame (the new layout's first few frames aside), or at the limit. Frames drawn
    // while the theme loads, before the new layout has drawn any cover, do not count.
    if (active && ((begun && now - started > 3 && !movedLast) || now - started > GLIDE_FRAMES))
        active = 0;
}

void coverGlideStart(void)
{
    for (int i = 0; i < GLIDE_COVERS; i++) {
        glide_cover_t *e = &covers[i];
        if (e->key == 0 || e->seen + 1 < now) { // not on screen just now
            e->key = 0;
            continue;
        }
        if (!active) { // a switch during a glide starts each cover from where it is on its way
            e->at = e->to;
            e->alpha = 1.0f;
        }
        e->wave = -1;
        e->stepped = 0;
    }
    active = 1;
    begun = 0;
    moving = 1;
    started = now;
}

int coverGlideActive(void)
{
    return active;
}

int coverGlide(const char *title, int id, int steps, glide_box_t *box)
{
    uint32_t key = glideKey(title, id);
    glide_cover_t *e = glideFind(key);
    if (e == NULL) {
        e = glideNew(key);
        e->at = *box;
        e->alpha = active ? 0.0f : 1.0f; // new to the screen in a glide: fades in where it lands
    }
    e->to = *box;
    e->seen = now;
    if (!active)
        return 0x80;
    if (!begun) {
        begun = 1;
        started = now;
    }

    if (e->wave < 0) {
        if (steps > GLIDE_WAVE_MAX)
            steps = GLIDE_WAVE_MAX;
        e->wave = steps > 0 ? GLIDE_WAVE * steps : 0;
    }
    if (e->stepped != now) {
        e->stepped = now;
        if (e->wave > 0)
            e->wave--;
        else {
            easeTo(&e->at.x, e->to.x, 0.3f);
            easeTo(&e->at.y, e->to.y, 0.3f);
            easeTo(&e->at.w, e->to.w, 0.3f);
            easeTo(&e->at.h, e->to.h, 0.3f);
            easeTo(&e->alpha, 1.0f, 0.004f);
        }
    }
    if (e->at.x != e->to.x || e->at.y != e->to.y || e->at.w != e->to.w || e->at.h != e->to.h || e->alpha < 1.0f)
        moving = 1;
    *box = e->at;
    return (int)(e->alpha * 0x80 + 0.5f);
}
