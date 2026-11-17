#!/usr/bin/env python3
"""Generate themes/thm_PS2Classic/ps2bg.png, the one image of the PS2 Classic theme.

Original art: a dark disc-browser scene (translucent blue towers, glow, glass panels, a
spotlight stage for the cover). Drawn on a 640x480 canvas and stored as a 256x256, 8-bit
palette PNG that the engine stretches over the screen, so it stays small and light.
Run: python tools/make_ps2classic_bg.py [--mock out.png]
"""
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "themes/thm_PS2Classic/ps2bg.png"
W, H = 640, 480
S = 2  # supersample

# Layout shared with conf_theme.cfg (canvas pixels).
HEADER = (24, 12, 616, 58)
LIST = (24, 70, 404, 404)
STAGE = (416, 70, 608, 404)
FOOT = (24, 424, 616, 464)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def vgrad(size, stops):
    w, h = size
    im = Image.new("RGB", size)
    px = im.load()
    for y in range(h):
        t = y / (h - 1)
        for i in range(len(stops) - 1):
            if stops[i][0] <= t <= stops[i + 1][0]:
                u = (t - stops[i][0]) / (stops[i + 1][0] - stops[i][0])
                c = lerp(stops[i][1], stops[i + 1][1], u)
                break
        for x in range(w):
            px[x, y] = c
    return im


def glow(size, cx, cy, rx, ry, color, peak):
    """Radial falloff layer, alpha-composited later."""
    w, h = size
    a = Image.new("L", size, 0)
    px = a.load()
    for y in range(max(0, int(cy - ry)), min(h, int(cy + ry))):
        for x in range(max(0, int(cx - rx)), min(w, int(cx + rx))):
            d = math.hypot((x - cx) / rx, (y - cy) / ry)
            if d < 1:
                px[x, y] = int(peak * (1 - d) ** 2)
    layer = Image.new("RGB", size, color)
    return layer, a


def build():
    sz = (W * S, H * S)
    k = S
    img = vgrad(sz, [(0, (1, 4, 20)), (0.45, (5, 16, 58)), (0.8, (7, 30, 96)), (1, (2, 8, 34))])

    # Ambient glows: low centre, and a soft light behind the stage.
    for cx, cy, rx, ry, col, pk in [
        (320, 470, 520, 220, (30, 110, 255), 150),
        (512, 238, 190, 190, (70, 150, 255), 120),
        (120, 100, 260, 160, (20, 60, 200), 70),
    ]:
        layer, a = glow(sz, cx * k, cy * k, rx * k, ry * k, col, pk)
        img.paste(layer, (0, 0), a)

    # Translucent towers rising from the floor, tapering toward the horizon.
    rnd = random.Random(2)
    tow = Image.new("RGBA", sz, (0, 0, 0, 0))
    td = ImageDraw.Draw(tow)
    xs = [i * 46 + rnd.randint(-10, 10) for i in range(-1, 15)]
    for x in xs:
        wd = rnd.randint(16, 44)
        top = rnd.randint(90, 300)
        x0, x1 = x * k, (x + wd) * k
        for y in range(top * k, H * k, 2):
            t = (y - top * k) / ((H - top) * k)
            al = int(22 + 90 * t)
            td.line([(x0, y), (x1, y)], fill=(60, 140, 255, al), width=2)
        td.line([(x0, top * k), (x0, H * k)], fill=(170, 220, 255, 110), width=k)
        td.line([(x1, top * k), (x1, H * k)], fill=(150, 205, 255, 28), width=k)
        td.rectangle([x0, top * k, x1, top * k + 2 * k], fill=(200, 235, 255, 150))
    tow = tow.filter(ImageFilter.GaussianBlur(0.8 * k))
    img.paste(tow, (0, 0), tow)

    # Sparse particles.
    pd = ImageDraw.Draw(img, "RGBA")
    for _ in range(70):
        x, y = rnd.randint(0, W), rnd.randint(0, int(H * 0.8))
        r = rnd.choice([1, 1, 1, 2])
        al = rnd.randint(60, 190)
        pd.ellipse([(x - r) * k, (y - r) * k, (x + r) * k, (y + r) * k], fill=(190, 225, 255, al))

    def glass(box, r, fill, edge, top_sheen=True):
        nonlocal img
        x0, y0, x1, y1 = [v * k for v in box]
        lay = Image.new("RGBA", sz, (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        d.rounded_rectangle([x0, y0, x1, y1], r * k, fill=fill)
        if top_sheen:
            sh = Image.new("RGBA", sz, (0, 0, 0, 0))
            sd = ImageDraw.Draw(sh)
            for i in range(int((y1 - y0) * 0.4)):
                al = int(34 * (1 - i / ((y1 - y0) * 0.4)))
                sd.line([(x0 + r * k, y0 + i), (x1 - r * k, y0 + i)], fill=(150, 200, 255, al))
            mask = Image.new("L", sz, 0)
            ImageDraw.Draw(mask).rounded_rectangle([x0, y0, x1, y1], r * k, fill=255)
            sh.putalpha(ImageChops.multiply(sh.getchannel("A"), mask))
            lay = Image.alpha_composite(lay, sh)
        d = ImageDraw.Draw(lay)
        d.rounded_rectangle([x0, y0, x1, y1], r * k, outline=edge, width=2 * k)
        d.rounded_rectangle([x0 + 2 * k, y0 + 2 * k, x1 - 2 * k, y1 - 2 * k], (r - 2) * k,
                            outline=(0, 0, 0, 90), width=k)
        img = Image.alpha_composite(img.convert("RGBA"), lay).convert("RGB")

    glass(HEADER, 12, (4, 14, 52, 175), (120, 180, 255, 170))
    glass(LIST, 14, (3, 10, 40, 205), (96, 160, 255, 160))
    glass(STAGE, 14, (6, 22, 78, 135), (110, 175, 255, 150))
    glass(FOOT, 12, (4, 14, 52, 175), (120, 180, 255, 150), top_sheen=False)

    # Stage spotlight and floor ring.
    layer, a = glow(sz, 512 * k, 232 * k, 120 * k, 160 * k, (120, 185, 255), 105)
    mask = Image.new("L", sz, 0)
    ImageDraw.Draw(mask).rounded_rectangle([v * k for v in STAGE], 14 * k, fill=255)
    a = ImageChops.multiply(a, mask)
    img.paste(layer, (0, 0), a)
    fd = ImageDraw.Draw(img, "RGBA")
    for i, (rx, ry, al) in enumerate([(84, 15, 150), (62, 11, 100), (40, 7, 70)]):
        fd.ellipse([(512 - rx) * k, (372 - ry) * k, (512 + rx) * k, (372 + ry) * k],
                   outline=(150, 205, 255, al), width=k + (1 if i == 0 else 0))

    # Divider rail inside the list panel (scroll-track look).
    fd.rounded_rectangle([392 * k, 86 * k, 395 * k, 388 * k], k, fill=(120, 180, 255, 70))

    # Vignette.
    vg = Image.new("L", sz, 0)
    vp = vg.load()
    for y in range(0, H * k, 2):
        for x in range(0, W * k, 2):
            d = max(abs(x / (W * k) - 0.5), abs(y / (H * k) - 0.5) * 1.15)
            v = int(max(0, d - 0.35) * 2.3 * 190)
            vp[x, y] = min(v, 170)
            vp[x + 1, y] = vp[x, y]
            if y + 1 < H * k:
                vp[x, y + 1] = vp[x, y]
                vp[x + 1, y + 1] = vp[x, y]
    img.paste(Image.new("RGB", sz, (0, 0, 8)), (0, 0), vg)
    return img.resize((W, H), Image.LANCZOS)


def save(img):
    small = img.resize((256, 256), Image.LANCZOS)
    q = small.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    q.save(OUT, optimize=True)
    return q


def mock(path, q):
    """Approximate what the engine draws, to judge the look before a build."""
    from PIL import ImageFont
    bg = q.convert("RGB").resize((W, H), Image.BILINEAR)
    im = bg.copy()
    d = ImageDraw.Draw(im, "RGBA")
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 17)
    fb = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 20)
    fs = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
    d.text((320, 35), "USB Games", font=fb, fill=(255, 255, 255), anchor="mm")
    games = ["Ape Escape 2", "Burnout 3: Takedown", "Final Fantasy X", "God of War", "Gran Turismo 4",
             "Jak and Daxter", "Kingdom Hearts", "Metal Gear Solid 3", "Ratchet & Clank", "Shadow of the Colossus"]
    for i, g in enumerate(games):
        y = 96 + i * 30
        if i == 4:
            d.text((46, y), g, font=f, fill=(110, 225, 255), anchor="lm")
        else:
            d.text((46, y), g, font=f, fill=(168, 192, 232), anchor="lm")
    case = Image.open(ROOT / "gfx/case.png").convert("RGBA")
    cov = Image.open(ROOT / "gfx/cover.png").convert("RGBA").resize((184, 256))
    cs = case.resize((184, 256))
    im.paste(cov, (420, 104), cov)
    im.paste(cs, (420, 104), cs)
    ov = Image.open(ROOT / "gfx/case_overlay.png").convert("RGBA").resize((184, 256))
    im.paste(ov, (420, 104), ov)
    d.text((320, 444), "X Run   O Menu   △ Info   □ Options", font=fs, fill=(220, 235, 255), anchor="mm")
    im.save(path)


if __name__ == "__main__":
    art = build()
    q = save(art)
    print(OUT, OUT.stat().st_size, "bytes")
    if "--mock" in sys.argv:
        mock(sys.argv[sys.argv.index("--mock") + 1], q)
