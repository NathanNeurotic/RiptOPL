/*
  Copyright 2009-2010 volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

// The dialog value editors: on-screen keyboard, number pad, colour selector. Split out of dia.cpp.

#include "include/opl.h"
#include "include/dia.h"
#include "include/diaint.h"
#include "include/gui.h"
#include "include/lang.h"
#include "include/pad.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/themes.h"
#include "include/util.h"
#include "include/sound.h"

int diaScreenWidth;
int diaScreenHeight;

// Right edge available to the keyboard's caret hint: CANCEL is drawn at x=500 on the same row.
constexpr int KEYB_HINT_END = 490;
// Width of the keyboard's text field: it starts at x=50 and the separator lines end at x=615.
constexpr int KEYB_TEXT_W = 540;
// Utility stuff
constexpr int KEYB_MODE = 2;
constexpr int KEYB_WIDTH = 12;
constexpr int KEYB_HEIGHT = 4;
constexpr int KEYB_ITEMS = (KEYB_WIDTH * KEYB_HEIGHT);

void diaDrawBoundingBox(int x, int y, int w, int h, int focus)
{
    u64 color = focus ? gTheme->selTextColor : gTheme->textColor;

    color |= GS_SETREG_RGBA(0, 0, 0, 0xFF);
    color &= gColFocus;

    rmDrawRect(x - 5, y, w + 10, h + 10, color);
}

// Rendered width of text[from..to), in the same 640-wide units the caret has always been placed in.
// scratch must hold to - from + 1 bytes.
static int diaKeybTextWidth(const char *text, int from, int to, char *scratch)
{
    memcpy(scratch, text + from, to - from);
    scratch[to - from] = '\0';
    return rmUnScaleX(fntCalcDimensions(gTheme->fonts[0], scratch));
}

// Next / previous character start, so scrolling never begins the field inside a UTF-8 sequence.
static int diaKeybNextChar(const char *text, int i, int limit)
{
    do {
        i++;
    } while (i < limit && (((unsigned char)text[i]) & 0xC0) == 0x80);
    return i;
}

static int diaKeybPrevChar(const char *text, int i)
{
    do {
        i--;
    } while (i > 0 && (((unsigned char)text[i]) & 0xC0) == 0x80);
    return i;
}

int diaShowKeyb(char *text, int maxLen, int hide_text, const char *title)
{
    int i, j, len = strlen(text), selkeyb = 0, x, w;
    int selchar = 0, selcommand = -1;
    // Insertion point, in characters from the start of the string. Starts at the END, so typing and
    // backspace behave exactly as they always did until the user actually moves it (#466).
    //
    // L1/R1 rather than the d-pad: Left/Right already walk the on-screen key grid and hand off to
    // the command column at its edges, which is the primary control on this screen. The request
    // offers the shoulder buttons as its own alternative for that reason.
    int caret = len;
    // Blink phase. NOT guiFrameId: that only advances in guiStartFrame(), which this screen never
    // calls -- it drives its own rmStartFrame/rmEndFrame loop -- so guiFrameId is frozen for as
    // long as the keyboard is open and the caret held whatever state it was in (#466: "the cursor
    // is now complete but lacks the flicking behaviour").
    int caretFrame = 0;
    // First byte shown in the text field. Values longer than the field (a full custom core path)
    // scroll so the caret stays in view instead of running off the right edge of the screen.
    int view = 0;
    char c[2] = {'\0', '\0'}, *mask_buffer;
    static const char keyb0[KEYB_ITEMS] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', '`', ':'};

    static const char keyb1[KEYB_ITEMS] = {
        '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '|',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', '~', '.'};
    const char *keyb = keyb0;

    char *commands[KEYB_HEIGHT] = {_l(_STR_BACKSPACE), _l(_STR_SPACE), _l(_STR_ENTER), _l(_STR_MODE)};
    GSTEXTURE *cmdicons[KEYB_HEIGHT];
    cmdicons[0] = thmGetTexture(SQUARE_ICON);
    cmdicons[1] = thmGetTexture(TRIANGLE_ICON);
    cmdicons[2] = thmGetTexture(START_ICON);
    cmdicons[3] = thmGetTexture(SELECT_ICON);

    rmGetScreenExtents(&diaScreenWidth, &diaScreenHeight);

    if (hide_text) {
        if ((mask_buffer = static_cast<char*>(malloc(maxLen))) != NULL) {
            memset(mask_buffer, '*', len);
            mask_buffer[len] = '\0';
        } else {
            // Allocation failed: fall back to showing the text rather than
            // rendering through a NULL mask buffer.
            hide_text = 0;
        }
    } else {
        mask_buffer = NULL;
    }

    while (1) {
        readPads();
        caretFrame++;

        rmStartFrame();
        if (guiDrawBGSettings() == 0)
            guiDrawBGPlasma();
        rmDrawRect(0, 0, diaScreenWidth, diaScreenHeight, gColDarker);

        // Title
        if (title != NULL) {
            // Ends where the separating line does: "..." when a long (translated) title would run past it
            fntRenderStringFit(gTheme->fonts[0], 25, 20, ALIGN_NONE, 615 - 25, title, gTheme->textColor, 0);
            // separating line
            rmDrawLine(25, 38, 615, 38, gColWhite);
        }

        // Text
        {
            const char *shown = hide_text ? mask_buffer : text;
            char pre[maxLen];
            int n = (caret < len) ? caret : len;

            // Horizontal scroll. The start of the field moves only when it has to: right when the
            // caret passes the right edge, left when the caret passes the left edge, and back left
            // whenever everything from one character further left fits again (after a delete near
            // the end), so the field never shows a scrolled-away head beside empty space.
            if (view > n)
                view = n;
            while (view < n && diaKeybTextWidth(shown, view, n, pre) > KEYB_TEXT_W)
                view = diaKeybNextChar(shown, view, n);
            while (view > 0) {
                int prev = diaKeybPrevChar(shown, view);
                if (diaKeybTextWidth(shown, prev, len, pre) > KEYB_TEXT_W)
                    break;
                view = prev;
            }
            fntRenderString(gTheme->fonts[0], 50, 120, ALIGN_NONE, KEYB_TEXT_W, 20, shown + view, gTheme->textColor);

            // Caret. Measured from the substring to its LEFT rather than assuming a character width,
            // because the theme font is proportional. Full row height -- UI_SPACING_H is HALF a row,
            // which is the "half cursor" reported on #466 -- and blinking, as the request asked.
            if ((caretFrame >> 4) & 1)
                rmDrawRect(50 + diaKeybTextWidth(shown, view, n, pre), 120, 2, 20, gColWhite);
        }

        // separating line for simpler orientation
        rmDrawLine(25, 138, 615, 138, gColWhite);

        for (j = 0; j < KEYB_HEIGHT; j++) {
            for (i = 0; i < KEYB_WIDTH; i++) {
                c[0] = keyb[i + j * KEYB_WIDTH];

                x = 50 + i * 31;
                w = fntRenderString(gTheme->fonts[0], x, 170 + 3 * UI_SPACING_H * j, ALIGN_NONE, 0, 0, c, gTheme->uiTextColor) - x;
                if ((i + j * KEYB_WIDTH) == selchar)
                    diaDrawBoundingBox(x, 170 + 3 * UI_SPACING_H * j, w, UI_SPACING_H, 0);
            }
        }

        // Commands
        for (i = 0; i < KEYB_HEIGHT; i++) {
            if (cmdicons[i]) {
                int w = (cmdicons[i]->Width * 20) / cmdicons[i]->Height;
                int h = 20;
                rmDrawPixmap(cmdicons[i], 436, 170 + 3 * UI_SPACING_H * i, ALIGN_NONE, w, h, SCALING_RATIO, gDefaultCol, 0);
            }

            x = 477;
            w = fntRenderString(gTheme->fonts[0], x, 170 + 3 * UI_SPACING_H * i, ALIGN_NONE, 0, 0, commands[i], gTheme->uiTextColor) - x;
            if (i == selcommand)
                diaDrawBoundingBox(x, 170 + 3 * UI_SPACING_H * i, w, UI_SPACING_H, 0);
        }

        // Caret hint, laid out as the Settings peer-page footer does it: [L1] label [R1], using the
        // button TEXTURES rather than a written "L1"/"R1" so disk themes can restyle them. Without
        // this the caret is undiscoverable -- nothing else on this screen uses the shoulder buttons.
        {
            GSTEXTURE *l1Tex = thmGetTexture(L1_ICON);
            GSTEXTURE *r1Tex = thmGetTexture(R1_ICON);
            int l1W = l1Tex ? (l1Tex->Width * 20) / l1Tex->Height : 0;
            int r1W = r1Tex ? (r1Tex->Width * 20) / r1Tex->Height : 0;
            int hintX = 50;

            // fntRenderString's width is size_t, and 0 means UNBOUNDED -- so a negative budget
            // would wrap to a huge value and disable the very clipping this computes. Work it out
            // signed first and draw nothing at all if it does not fit.
            int avail = KEYB_HINT_END - rmWideScale(l1W) - rmWideScale(r1W) - 16 - hintX;
            if (avail > 0) {
                if (l1Tex && l1Tex->Mem)
                    rmDrawPixmap(l1Tex, hintX, 417, ALIGN_NONE, l1W, 20, SCALING_RATIO, gDefaultCol, 0);
                hintX += rmWideScale(l1W) + 8;
                hintX = fntRenderString(gTheme->fonts[0], hintX, 417, ALIGN_NONE, avail, 20,
                                        _l(_STR_KEYB_MOVE_CURSOR), gTheme->textColor);
                hintX += 8;
                if (r1Tex && r1Tex->Mem && hintX + rmWideScale(r1W) <= KEYB_HINT_END)
                    rmDrawPixmap(r1Tex, hintX, 417, ALIGN_NONE, r1W, 20, SCALING_RATIO, gDefaultCol, 0);
            }
        }

        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_CANCEL, gTheme->fonts[0], 500, 417, gTheme->textColor);

        rmEndFrame();

        if (getKey(KEY_L1)) { // caret left
            if (caret > 0) {
                sfxPlay(SFX_CURSOR);
                caret--;
            }
        } else if (getKey(KEY_R1)) { // caret right
            if (caret < len) {
                sfxPlay(SFX_CURSOR);
                caret++;
            }
        } else if (getKey(KEY_LEFT)) {
            sfxPlay(SFX_CURSOR);
            if (selchar > -1) {
                if (selchar % KEYB_WIDTH)
                    selchar--;
                else {
                    selcommand = selchar / KEYB_WIDTH;
                    selchar = -1;
                }
            } else {
                selchar = (selcommand + 1) * KEYB_WIDTH - 1;
                selcommand = -1;
            }
        } else if (getKey(KEY_RIGHT)) {
            sfxPlay(SFX_CURSOR);
            if (selchar > -1) {
                if ((selchar + 1) % KEYB_WIDTH)
                    selchar++;
                else {
                    selcommand = selchar / KEYB_WIDTH;
                    selchar = -1;
                }
            } else {
                selchar = selcommand * KEYB_WIDTH;
                selcommand = -1;
            }
        } else if (getKey(KEY_UP)) {
            sfxPlay(SFX_CURSOR);
            if (selchar > -1)
                selchar = (selchar + KEYB_ITEMS - KEYB_WIDTH) % KEYB_ITEMS;
            else
                selcommand = (selcommand + KEYB_HEIGHT - 1) % KEYB_HEIGHT;
        } else if (getKey(KEY_DOWN)) {
            sfxPlay(SFX_CURSOR);
            if (selchar > -1)
                selchar = (selchar + KEYB_WIDTH) % KEYB_ITEMS;
            else
                selcommand = (selcommand + 1) % KEYB_HEIGHT;
        } else if (getKeyOn(gSelectButton)) {
            if (len < (maxLen - 1) && selchar > -1) {
                sfxPlay(SFX_CONFIRM);
                // Insert AT the caret: shift the tail (and its NUL) right by one, drop the
                // character in, and follow it. With the caret at the end this is the old append.
                memmove(&text[caret + 1], &text[caret], len - caret + 1);
                text[caret] = keyb[selchar];
                if (mask_buffer != NULL) {
                    mask_buffer[len] = '*';
                    mask_buffer[len + 1] = '\0';
                }

                len++;
                caret++;
            } else if (selcommand == 0) {
                sfxPlay(SFX_CANCEL);
                if (caret > 0) { // BACKSPACE -- removes the character LEFT of the caret
                    memmove(&text[caret - 1], &text[caret], len - caret + 1);
                    len--;
                    caret--;
                    if (mask_buffer != NULL)
                        mask_buffer[len] = '\0';
                }
            } else if (selcommand == 1) {
                sfxPlay(SFX_CONFIRM);
                if (len < (maxLen - 1)) { // SPACE -- inserted at the caret, like any other character
                    memmove(&text[caret + 1], &text[caret], len - caret + 1);
                    text[caret] = ' ';
                    if (mask_buffer != NULL) {
                        mask_buffer[len] = '*';
                        mask_buffer[len + 1] = '\0';
                    }

                    len++;
                    caret++;
                }
            } else if (selcommand == 2) {
                sfxPlay(SFX_CONFIRM);
                if (mask_buffer != NULL)
                    free(mask_buffer);
                return 1; // ENTER
            } else if (selcommand == 3) {
                sfxPlay(SFX_CONFIRM);
                selkeyb = (selkeyb + 1) % KEYB_MODE; // MODE
                if (selkeyb == 0)
                    keyb = keyb0;
                if (selkeyb == 1)
                    keyb = keyb1;
            }
        } else if (getKey(KEY_SQUARE)) {
            if (caret > 0) { // BACKSPACE -- same caret-relative delete as the command column
                sfxPlay(SFX_CANCEL);
                memmove(&text[caret - 1], &text[caret], len - caret + 1);
                len--;
                caret--;
                if (mask_buffer != NULL)
                    mask_buffer[len] = '\0';
            }
        } else if (getKey(KEY_TRIANGLE)) {
            if (len < (maxLen - 1) && selchar > -1) { // SPACE
                sfxPlay(SFX_CONFIRM);
                memmove(&text[caret + 1], &text[caret], len - caret + 1);
                text[caret] = ' ';
                if (mask_buffer != NULL) {
                    mask_buffer[len] = '*';
                    mask_buffer[len + 1] = '\0';
                }

                len++;
                caret++;
            }
        } else if (getKeyOn(KEY_START)) {
            sfxPlay(SFX_CONFIRM);
            if (mask_buffer != NULL)
                free(mask_buffer);
            return 1; // ENTER
        } else if (getKeyOn(KEY_SELECT)) {
            selkeyb = (selkeyb + 1) % KEYB_MODE; // MODE
            sfxPlay(SFX_CONFIRM);
            if (selkeyb == 0)
                keyb = keyb0;
            if (selkeyb == 1)
                keyb = keyb1;
        }

        if (getKey(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
            sfxPlay(SFX_CANCEL);
            break;
        }
    }


    if (mask_buffer != NULL)
        free(mask_buffer);

    return 0;
}

/* NUMERIC ENTRY PAD.

   A deliberately small 3x4 pad, not the full diaShowKeyb() grid. Routing int fields through the
   text keyboard technically works -- its top row is 1234567890 -- but it answers "I do not want to
   hold up/down" with "then hunt through 48 keys instead", which is the same problem wearing a hat.

   '-' toggles the sign of the whole entry rather than inserting a character (there is no valid
   entry with a minus anywhere but the front) and is inert, and drawn dim, on an unsigned field.
   '<' is backspace, so the pad is complete on its own without reaching for a shoulder button. */
constexpr int NUMPAD_WIDTH = 3;
constexpr int NUMPAD_HEIGHT = 4;
constexpr int NUMPAD_ITEMS = (NUMPAD_WIDTH * NUMPAD_HEIGHT);

int diaShowNumPad(char *text, int maxLen, int minv, int maxv)
{
    static const char numpad[NUMPAD_ITEMS] = {
        '1', '2', '3',
        '4', '5', '6',
        '7', '8', '9',
        '-', '0', '<'};
    const int allowNegative = (minv < 0);
    int len = strlen(text), sel = 0, i, j, x, w;
    char c[2] = {'\0', '\0'};
    char range[32];

    snprintf(range, sizeof(range), "%d - %d", minv, maxv);
    rmGetScreenExtents(&diaScreenWidth, &diaScreenHeight);

    while (1) {
        readPads();

        rmStartFrame();
        if (guiDrawBGSettings() == 0)
            guiDrawBGPlasma();
        rmDrawRect(0, 0, diaScreenWidth, diaScreenHeight, gColDarker);

        // The entry so far, with the accepted range beside it. An octet and a port look identical
        // on screen otherwise, and guessing which one you are in is how you end up with 255 ports.
        fntRenderString(gTheme->fonts[0], 50, 120, ALIGN_NONE, 0, 0, text, gTheme->textColor);
        fntRenderString(gTheme->fonts[0], 400, 120, ALIGN_NONE, 0, 0, range, gTheme->uiTextColor);
        rmDrawLine(25, 138, 615, 138, gColWhite);

        for (j = 0; j < NUMPAD_HEIGHT; j++) {
            for (i = 0; i < NUMPAD_WIDTH; i++) {
                int idx = i + j * NUMPAD_WIDTH;
                u64 col = gTheme->uiTextColor;

                c[0] = numpad[idx];
                if ((c[0] == '-') && !allowNegative)
                    col = gTheme->textColor;

                x = 270 + i * 40;
                w = fntRenderString(gTheme->fonts[0], x, 170 + 3 * UI_SPACING_H * j, ALIGN_NONE, 0, 0, c, col) - x;
                if (idx == sel)
                    diaDrawBoundingBox(x, 170 + 3 * UI_SPACING_H * j, w, UI_SPACING_H, 0);
            }
        }

        guiDrawIconAndText(SQUARE_ICON, _STR_BACKSPACE, gTheme->fonts[0], 50, 417, gTheme->textColor);
        guiDrawIconAndText(START_ICON, _STR_ENTER, gTheme->fonts[0], 250, 417, gTheme->textColor);
        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_CANCEL, gTheme->fonts[0], 500, 417, gTheme->textColor);

        rmEndFrame();

        // Wrap within the row on left/right and within the column on up/down: on a 3-wide pad the
        // alternative is walking the cursor off 9 to reach 1.
        if (getKey(KEY_LEFT)) {
            sfxPlay(SFX_CURSOR);
            sel = (sel % NUMPAD_WIDTH) ? sel - 1 : sel + NUMPAD_WIDTH - 1;
        } else if (getKey(KEY_RIGHT)) {
            sfxPlay(SFX_CURSOR);
            sel = ((sel + 1) % NUMPAD_WIDTH) ? sel + 1 : sel - NUMPAD_WIDTH + 1;
        } else if (getKey(KEY_UP)) {
            sfxPlay(SFX_CURSOR);
            sel = (sel + NUMPAD_ITEMS - NUMPAD_WIDTH) % NUMPAD_ITEMS;
        } else if (getKey(KEY_DOWN)) {
            sfxPlay(SFX_CURSOR);
            sel = (sel + NUMPAD_WIDTH) % NUMPAD_ITEMS;
        } else if (getKeyOn(gSelectButton)) {
            c[0] = numpad[sel];
            if (c[0] == '<') {
                if (len > 0) {
                    sfxPlay(SFX_CANCEL);
                    text[--len] = '\0';
                }
            } else if (c[0] == '-') {
                if (!allowNegative)
                    sfxPlay(SFX_CANCEL);
                else if (text[0] == '-') {
                    sfxPlay(SFX_CONFIRM);
                    memmove(text, text + 1, len); // len bytes == the remaining chars AND the NUL
                    len--;
                } else if (len < (maxLen - 1)) {
                    sfxPlay(SFX_CONFIRM);
                    memmove(text + 1, text, len + 1);
                    text[0] = '-';
                    len++;
                }
            } else if (len < (maxLen - 1)) {
                sfxPlay(SFX_CONFIRM);
                text[len++] = c[0];
                text[len] = '\0';
            }
        } else if (getKey(KEY_SQUARE)) { // BACKSPACE
            if (len > 0) {
                sfxPlay(SFX_CANCEL);
                text[--len] = '\0';
            }
        } else if (getKeyOn(KEY_TRIANGLE)) { // CLEAR -- the same button that opened the pad
            sfxPlay(SFX_CANCEL);
            len = 0;
            text[0] = '\0';
        } else if (getKeyOn(KEY_START)) {
            sfxPlay(SFX_CONFIRM);
            return 1; // ENTER
        }

        if (getKey(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
            sfxPlay(SFX_CANCEL);
            return 0;
        }
    }
}

static int colPadSettings[16];

int diaShowColSel(unsigned char *r, unsigned char *g, unsigned char *b)
{
    int selc = 0;
    int ret = 0;
    unsigned char col[3];

    padStoreSettings(colPadSettings);

    col[0] = *r;
    col[1] = *g;
    col[2] = *b;
    setButtonDelay(KEY_LEFT, 1);
    setButtonDelay(KEY_RIGHT, 1);

    while (1) {
        readPads();

        rmStartFrame();
        if (guiDrawBGSettings() == 0)
            guiDrawBGPlasma();
        rmDrawRect(0, 0, diaScreenWidth, diaScreenHeight, gColDarker);

        // "Color selection"
        fntRenderString(gTheme->fonts[0], 50, 50, ALIGN_NONE, 0, 0, _l(_STR_COLOR_SELECTION), GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));

        // 3 bars representing the colors...
        size_t co;
        int x, y;

        for (co = 0; co < 3; ++co) {
            unsigned char cc[3] = {0, 0, 0};
            cc[co] = col[co];

            x = 75;
            y = 75 + co * 25;

            u64 dcol = GS_SETREG_RGBA(cc[0], cc[1], cc[2], 0x80);

            if (selc == co)
                rmDrawRect(x, y, 200, 20, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
            else
                rmDrawRect(x, y, 200, 20, GS_SETREG_RGBA(0x20, 0x20, 0x20, 0x80));

            rmDrawRect(x + 2, y + 2, 190.0f * (cc[co] * 100 / 255) / 100, 16, dcol);
        }

        // target color itself
        u64 dcol = GS_SETREG_RGBA(col[0], col[1], col[2], 0x80);

        x = 300;
        y = 75;

        rmDrawRect(x, y, 70, 70, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
        rmDrawRect(x + 5, y + 5, 60, 60, dcol);

        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON, _STR_OK, gTheme->fonts[0], 420, 417, gTheme->textColor);
        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_CANCEL, gTheme->fonts[0], 500, 417, gTheme->textColor);

        rmEndFrame();

        /*
          Confirm/cancel are tested FIRST, and outside the direction chain.

          The direction arms below use getKey() under setButtonDelay(..., 1), so while a direction
          is physically held getKey() reports true on EVERY frame. With confirm/cancel chained onto
          the same else-if ladder they were unreachable for the entire duration of a hold: press X
          while still leaning on the D-pad and nothing happened. getKeyOn() is edge-triggered, so
          hoisting it cannot steal a repeat from the directions.
        */
        if (getKeyOn(gSelectButton)) {
            sfxPlay(SFX_CONFIRM);
            *r = col[0];
            *g = col[1];
            *b = col[2];
            ret = 1;
            break;
        }
        if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
            sfxPlay(SFX_CANCEL);
            ret = 0;
            break;
        }

        if (getKey(KEY_LEFT)) {
            if (col[selc] > 0) {
                col[selc]--;
                sfxPlay(SFX_CURSOR);
            }
        } else if (getKey(KEY_RIGHT)) {
            if (col[selc] < 255) {
                col[selc]++;
                sfxPlay(SFX_CURSOR);
            }
        } else if (getKey(KEY_UP)) {
            if (selc > 0) {
                selc--;
                sfxPlay(SFX_CURSOR);
            }
        } else if (getKey(KEY_DOWN)) {
            if (selc < 2) {
                selc++;
                sfxPlay(SFX_CURSOR);
            }
        }
    }

    padRestoreSettings(colPadSettings);
    return ret;
}



// ----------------------------------------------------------------------------