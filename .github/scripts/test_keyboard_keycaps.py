#!/usr/bin/env python3
"""On-screen keyboard keys are drawn as readable keycaps (src/ui/diaeditors.cpp).

zackcage6 (2026-11-09): the rename / IGR-path keyboard's letters and specials could not be seen and
'.' looked like an empty space. The key grid drew bare glyphs in uiTextColor (default 0x5868B4, a dim
blue under the darkened background) with a selection box only as wide as the glyph.

This checks the source, not a console: every key of the grid goes through diaDrawKeyCap, the key
glyphs use the theme's text colour drawn bold, and the cap geometry the source declares keeps caps inside the
grid (no overlap between neighbours or rows, the last column clear of the command icons at x=436).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src/ui/diaeditors.cpp"


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def const(text, name):
    # The whole value must be one integer literal: take the rest of the declaration line, drop comments (block
    # comments anywhere in it, then a line comment) and a closing ";", and require nothing but digits to be left.
    # A partial read of an expression such as "10 / 2" or "10 /* c */ / 2" would give 10 and let an overlap through.
    n = re.escape(name)
    m = re.search(r"^[ \t]*(?:constexpr int {0}[ \t]*=|#define {0}(?=[ \t]))(.*)$".format(n), text, re.M)
    if not m:
        fail(f"{name} not declared")
    value = re.sub(r"/\*.*?\*/", " ", m.group(1)).split("//", 1)[0].strip()
    if value.endswith(";"):
        value = value[:-1].strip()
    if not re.fullmatch(r"\d+", value):
        fail(f"{name} not declared as a plain integer (value {value!r})")
    return int(value)


def main():
    text = SRC.read_text(encoding="utf-8")

    helper = re.search(r"static void diaDrawKeyCap\(.*?\n}\n", text, re.S)
    if not helper:
        fail("diaDrawKeyCap missing")
    body = helper.group(0)
    if "uiTextColor" in body:
        fail("key glyphs must not use uiTextColor")
    if "diaDrawBoldText(" not in body or "label, colour)" not in body:
        fail("key glyph is not drawn bold in the colour it is given")

    # Bold for a CRT (zackcage6, 2026-11-10): four copies, one display pixel right and one display line down, the
    # step worked out from the display extents so that it is one line whether the frame is 224 or 448 lines tall.
    bold = re.search(r"static int diaDrawBoldText\(.*?\n}\n", text, re.S)
    if not bold:
        fail("diaDrawBoldText missing")
    b = bold.group(0)
    if b.count("fntRenderString(gTheme->fonts[0], ") != 4 or "x + dx, y + dy" not in b or "x, y + dy" not in b or "x + dx, y," not in b:
        fail("diaDrawBoldText does not draw the text four times, offset right, down and both")
    if "diaBoldStep(x, rmScaleX)" not in b or "diaBoldStep(y, rmScaleY)" not in b:
        fail("the bold offsets are not found at the position drawn, through rmScaleX/rmScaleY")
    step = re.search(r"static int diaBoldStep\(int at, int \(\*scale\)\(int\)\)\n\{(.*?)\n\}\n", text, re.S)
    if not step or "while (step < 8 && scale(at + step) == scale(at))" not in step.group(1):
        fail("diaBoldStep does not search for the smallest step that moves the position")

    # The renderer places text with a floor, v * display / virtual (renderman scaleX/scaleY). With the search, the
    # bold copy is one display pixel or line away (one unit where a unit is larger) at every position, every display size and every
    # overscan; a fixed step in 640x480 units gave none at some rows (zero offset, no thickening).
    def bold_offset(at, display, virtual):
        scale = lambda v: v * display // virtual
        st = 1
        while st < 8 and scale(at + st) == scale(at):
            st += 1
        return scale(at + st) - scale(at)
    for virtual, displays in ((480, range(200, 1081)), (640, range(500, 1921))):
        for display in displays:
            for at in range(0, virtual):
                off = bold_offset(at, display, virtual)
                # one pixel or line where a unit is at most one; where a unit is bigger, the one unit it takes
                want_max = 1 if display <= virtual else -(-display // virtual)
                if not 1 <= off <= want_max:
                    fail(f"bold offset at {at} of {virtual} on {display} display pixels is {off}, not 1..{want_max}")

    keyb = re.search(r"int diaShowKeyb\(.*?\n}\n", text, re.S).group(0)
    if "diaDrawBoldText(50, 120, ALIGN_NONE, KEYB_TEXT_W, 20, shown + view, gTheme->textColor);" not in keyb:
        fail("the text being typed is not drawn bold")
    grid = re.search(r"for \(j = 0; j < KEYB_HEIGHT; j\+\+\) \{(.*?)\n        \}\n", keyb, re.S)
    if not grid:
        fail("keyboard grid loop not found")
    g = grid.group(1)
    if "diaDrawKeyCap(" not in g or "gTheme->textColor)" not in g:
        fail("keyboard grid does not draw keycaps in the theme text colour")
    if "uiTextColor" in g or "diaDrawBoundingBox" in g:
        fail("keyboard grid still draws bare uiTextColor glyphs or the glyph-width box")

    m = re.search(r"diaDrawKeyCap\((\d+) \+ i \* KEYB_PITCH, (\d+) \+ 3 \* UI_SPACING_H \* j", g)
    if not m:
        fail("keycap position expression changed; update this test")
    x0, y0 = int(m.group(1)), int(m.group(2))
    pitch, cap_w, cap_h = const(text, "KEYB_PITCH"), const(text, "KEYB_CAP_W"), const(text, "KEYB_CAP_H")
    width, height = const(text, "KEYB_WIDTH"), const(text, "KEYB_HEIGHT")
    # UI_SPACING_H lives in include/diaint.h (RiptOPL) or src/dia.c (OPL); read it from either.
    header = ROOT / "include/diaint.h"
    spacing_src = text + (header.read_text(encoding="utf-8") if header.exists() else "")
    row_pitch = 3 * const(spacing_src, "UI_SPACING_H")

    if cap_w >= pitch:
        fail(f"caps overlap horizontally: width {cap_w} >= pitch {pitch}")
    if cap_h >= row_pitch:
        fail(f"caps overlap vertically: height {cap_h} >= row pitch {row_pitch}")
    right = x0 + (width - 1) * pitch + cap_w
    if right >= 436:
        fail(f"last column ends at {right}, into the command icons at 436")
    top, bottom = y0, y0 + (height - 1) * row_pitch + cap_h
    if top <= 138 or bottom >= 417:
        fail(f"grid {top}..{bottom} crosses the text separator (138) or the footer (417)")

    cmd = re.search(r"// Commands\n(.*?)\n        \}\n", keyb, re.S)
    if not cmd or "uiTextColor" in cmd.group(1) or "commands[i], gTheme->textColor)" not in cmd.group(1):
        fail("the command column does not draw its labels in the theme text colour")
    pad = re.search(r"int diaShowNumPad\(.*?\n}\n", text, re.S).group(0)
    if "diaDrawKeyCap(" not in pad or "u64 col = gTheme->textColor;" not in pad:
        fail("the number pad does not draw keycaps in the text colour")

    print(f"PASS: keyboard and number pad keys are keycaps in the text colour, commands too; grid x {x0}..{right}, y {top}..{bottom}")


if __name__ == "__main__":
    main()
