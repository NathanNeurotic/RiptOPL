/*
  Copyright 2009-2010 volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

// Accessors for a dialog definition (UIItem array): find an item by id and read or change its state. Split out of dia.cpp.

#include "include/opl.h"
#include "include/dia.h"
#include "include/diaint.h"

struct UIItem *diaFindByID(struct UIItem *ui, int id)
{
    while (ui->type != UI_TERMINATOR) {
        if (ui->id == id)
            return ui;

        ui++;
    }

    return NULL;
}

void diaSetEnabled(struct UIItem *ui, int id, int enabled)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return;

    item->enabled = enabled;
}

void diaSetShowDefaultWhenEmpty(struct UIItem *ui, int id, int show)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return;

    item->showDefaultWhenEmpty = show;
}

void diaSetVisible(struct UIItem *ui, int id, int visible)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return;

    item->visible = visible;
}

// 1 when any value row now differs from what the page put there. diaSetInt/diaSetString store the
// value as the row's def as well, and editing a row (unlike Cancel, which restores it) leaves def
// alone -- so def is exactly "what this page showed when it opened". A row the page never set keeps
// its static initializer; if that has def != current it reads as changed, the safe direction.
int diaHasChanges(struct UIItem *ui)
{
    for (; ui->type != UI_TERMINATOR; ui++) {
        switch (ui->type) {
            case UI_INT:
            case UI_BOOL:
            case UI_ENUM:
                if (ui->intvalue.current != ui->intvalue.def)
                    return 1;
                break;
            case UI_STRING:
            case UI_PASSWORD:
                if (strncmp(ui->stringvalue.text, ui->stringvalue.def, sizeof(ui->stringvalue.text)) != 0)
                    return 1;
                break;
            default:
                break;
        }
    }

    return 0;
}

int diaGetInt(struct UIItem *ui, int id, int *value)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_INT) || (item->type == UI_BOOL) || (item->type == UI_ENUM)) {
        *value = item->intvalue.current;
        return 1;
    }

    return 0;
}

int diaSetInt(struct UIItem *ui, int id, int value)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_INT) || (item->type == UI_BOOL) || (item->type == UI_ENUM)) {
        item->intvalue.def = value;
        item->intvalue.current = value;
        return 1;
    }

    return 0;
}

int diaGetString(struct UIItem *ui, int id, char *value, int length)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_STRING) || (item->type == UI_PASSWORD)) {
        strncpy(value, item->stringvalue.text, length);
        if (length > 0)
            value[length - 1] = '\0';
        return 1;
    }

    return 0;
}

int diaSetString(struct UIItem *ui, int id, const char *text)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_STRING) || (item->type == UI_PASSWORD)) {
        strncpy(item->stringvalue.def, text, sizeof(item->stringvalue.def));
        item->stringvalue.def[sizeof(item->stringvalue.def) - 1] = '\0';
        strncpy(item->stringvalue.text, text, sizeof(item->stringvalue.text));
        item->stringvalue.text[sizeof(item->stringvalue.text) - 1] = '\0';
        return 1;
    }

    return 0;
}

int diaGetColor(struct UIItem *ui, int id, unsigned char *col)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if (item->type != UI_COLOUR)
        return 0;

    col[0] = item->colourvalue.r;
    col[1] = item->colourvalue.g;
    col[2] = item->colourvalue.b;
    return 1;
}

int diaSetColor(struct UIItem *ui, int id, const unsigned char *col)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if (item->type != UI_COLOUR)
        return 0;

    item->colourvalue.r = col[0];
    item->colourvalue.g = col[1];
    item->colourvalue.b = col[2];
    return 1;
}

int diaSetU64Color(struct UIItem *ui, int id, u64 col)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if (item->type != UI_COLOUR)
        return 0;

    item->colourvalue.r = col & 0xFF;
    col >>= 8;
    item->colourvalue.g = col & 0xFF;
    col >>= 8;
    item->colourvalue.b = col & 0xFF;
    return 1;
}

int diaSetLabel(struct UIItem *ui, int id, const char *text)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_LABEL) || (item->type == UI_BUTTON) || (item->type == UI_HEADER)) {
        item->label.text = text;
        return 1;
    }

    return 0;
}

// LIFETIME CONTRACT: this stores enumvals' RAW POINTER -- no copy. The array must outlive every
// render of the dialog (dialog structs are also reused across openings). A block-scoped array is
// a dangling pointer the moment its brace closes (issue #154: the DMA row rendered the Neutrino
// video-mode list). Use static arrays for pure literals; _l()-localized arrays must stay
// function-scope in the function that executes the dialog (static would freeze the first language).
int diaSetEnum(struct UIItem *ui, int id, const char **enumvals)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if (item->type != UI_ENUM)
        return 0;

    item->intvalue.enumvalues = enumvals;
    return 1;
}
