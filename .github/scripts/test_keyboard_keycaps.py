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
    if "rmGetScreenExtentsNative(&wn, &hn)" not in b or "diaBoldStep(h, hn)" not in b or "diaBoldStep(w, wn)" not in b:
        fail("the bold offsets are not worked out from the display extents")
    step = re.search(r"static int diaBoldStep\(int units, int native\)\n\{(.*?)\n\}\n", text, re.S)
    if not step or "(units + native / 2) / native" not in step.group(1) or "step < 1 ? 1 : step" not in step.group(1):
        fail("diaBoldStep is not round(units / native) with a floor of 1")

    def bold_step(units, native):  # the same sum, to check the steps it gives for the modes RiptOPL has
        st = (units + native // 2) // native if native > 0 else 1
        return max(st, 1)
    for units, native, want in ((480, 448, 1), (480, 480, 1), (480, 512, 1), (480, 224, 2), (480, 256, 2),
                                (480, 1080, 1), (640, 640, 1), (640, 512, 1), (640, 1920, 1)):
        if bold_step(units, native) != want:
            fail(f"bold step for {units} units on {native} display pixels is {bold_step(units, native)}, not {want}")
    if "ALIGN_HCENTER" not in body:
        fail("key glyph is not centred in its cap")
    # The selected glyph must contrast with the selected cap on any palette: an opaque cap in the selection
    # colour, the glyph switched to black or white by that colour's luminance (not left in the text colour).
    sel = re.search(r"if \(selected\) \{(.*?)\n    \}", body, re.S)
    if not sel or "colour = diaKeyCapSelGlyph(colour);" not in sel.group(1) or "0x80)" not in sel.group(1):
        fail("the selected key does not switch its glyph to a colour that contrasts with an opaque cap")
    pick = re.search(r"static u64 diaKeyCapSelGlyph\(u64 colour\)\n\{(.*?)\n\}\n", text, re.S)
    if not pick or "luma >= 0x80 ? GS_SETREG_RGBA(0, 0, 0, alpha) : GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, alpha)" not in pick.group(1):
        fail("diaKeyCapSelGlyph does not pick black or white by the selection colour's luminance")

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
