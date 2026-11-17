#!/usr/bin/env python3
"""Draws themes/thm_PS2Classic/ps2bg.png: a 160x120, 8-bit palette picture (about 19 KiB in
texture memory) that the theme stretches over the screen. Original art: a deep blue gradient with a
row of faint towers. Run from the repository root; needs Pillow."""
import random
from PIL import Image, ImageDraw

W, H = 160, 120
img = Image.new("RGB", (W, H))
px = img.load()
for y in range(H):
    t = y / (H - 1)
    # near-black navy at the top, a soft blue glow low down
    r = int(2 + 10 * t * t)
    g = int(5 + 34 * t * t)
    b = int(16 + 96 * t ** 1.6)
    for x in range(W):
        px[x, y] = (r, g, b)

d = ImageDraw.Draw(img, "RGBA")
rnd = random.Random(2)
x = 4
while x < W - 4:
    w = rnd.choice((4, 5, 6, 8))
    h = rnd.randint(18, 70)
    top = H - 14 - h
    d.rectangle([x, top, x + w - 1, H], fill=(22, 60, 150, 70))
    d.line([x, top, x + w - 1, top], fill=(110, 170, 255, 120))
    d.line([x, top, x, H], fill=(60, 120, 220, 60))
    x += w + rnd.choice((3, 5, 8, 11))
# floor glow
for y in range(H - 14, H):
    a = int(90 * (y - (H - 14)) / 14)
    d.line([0, y, W, y], fill=(40, 100, 220, a))

img.quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(
    "themes/thm_PS2Classic/ps2bg.png", optimize=True)
