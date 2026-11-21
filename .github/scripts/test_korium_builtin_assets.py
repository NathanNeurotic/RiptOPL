"""The built-in PSBBN theme assets, fonts, configs and engine wiring replace legacy Korium/IncebtIon assets."""
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parents[2]
read = lambda path: (root / path).read_text(encoding="utf-8")

# Embedded PNGs and font in gfx/, third_party/fonts/, tools/assets.py, include/textures.h, src/gfx/textures.cpp.
assets = read("tools/assets.py")
png_block = assets[assets.index('PNG_ASSETS = """'):assets.index('AUDIO = ')]
for removed in ("incebtion", "ip", "case_overlay"):
    assert not re.search(rf"\b{removed}\b", png_block), f"unused legacy asset {removed} still in PNG_ASSETS"
for required in ("Players_1", "Players_2", "Players_3", "Players_4", "bezel_top", "bezel_bottom", "psbbn_bg"):
    assert re.search(rf"\b{required}\b", png_block), f"missing {required} in PNG_ASSETS"
    assert (root / f"gfx/{required}.png").is_file(), f"missing gfx/{required}.png"
assert not (root / "gfx/incebtion.png").exists() and not (root / "gfx/ip.png").exists()
for retired in ("misc/korium_list_desc.cfg", "misc/theme_grid_korium.cfg", "third_party/korium/assets.zip"):
    assert not (root / retired).exists(), f"retired theme asset still shipped: {retired}"
legacy_assets = read("Makefile").split("PNG_ASSETS = ", 1)[1].split("\n\n", 1)[0]
for retired in ("incebtion", "ip", "case_overlay"):
    assert not re.search(rf"\b{retired}\b", legacy_assets), f"legacy Makefile still embeds {retired}"
assert 'bin2c("third_party/fonts/ripps2_roboto.ttf", "ripps2roboto", "ripps2roboto_raw")' in assets
assert (root / "third_party/fonts/ripps2_roboto.ttf").is_file()
assert (root / "third_party/fonts/ROBOTO-OFL.txt").is_file()

for bezel in ("bezel_top", "bezel_bottom"):
    png = (root / f"gfx/{bezel}.png").read_bytes()
    assert png[:8] == b"\x89PNG\r\n\x1a\n" and png[24] == 8, f"{bezel}.png must be an 8-bit PNG"
assert struct.unpack(">II", (root / "gfx/bezel_top.png").read_bytes()[16:24]) == (640, 63)
assert struct.unpack(">II", (root / "gfx/bezel_bottom.png").read_bytes()[16:24]) == (640, 64)

tex_h = read("include/textures.h")
tex_c = read("src/gfx/textures.cpp")
for enum_name, str_name, sym_name in (
    ("PLAYERS_1", "Players_1", "&Players_1_png"),
    ("PLAYERS_2", "Players_2", "&Players_2_png"),
    ("PLAYERS_3", "Players_3", "&Players_3_png"),
    ("PLAYERS_4", "Players_4", "&Players_4_png"),
    ("BEZEL_TOP", "bezel_top", "&bezel_top_png"),
    ("BEZEL_BOTTOM", "bezel_bottom", "&bezel_bottom_png"),
    ("COVERAPP_DEFAULT", "coverapp", "&coverapp_png"),
    ("APPS_CASE_OVERLAY", "apps_case", "&case_png"),
):
    assert enum_name in tex_h
    assert f'{{{enum_name}, "{str_name}", {sym_name}}}' in tex_c, (enum_name, str_name, sym_name)
assert "&incebtion_png" not in tex_c and "&ip_png" not in tex_c and "&case_overlay_png" not in tex_c

# Engine wiring: built-in font aliases, SCRn, aligned=2, ItemsList hidden/highlight, #Name zero-I/O, badge_set=ripps2.
themes = read("src/gfx/themes.cpp")
elems = read("src/gfx/themeelems.cpp")
fnt = read("src/gfx/fntsys.cpp")
assert 'thmLoadFonts(themeConfig, themePath ? themePath : "", newT);' in themes
assert 'for (i = PLAYERS_1; i <= PLAYERS_4; i++)' in themes
assert "newT->badgeSet = configGetStr" in themes
assert '"builtin:roboto"' in fnt and '"builtin:ripflow"' in fnt and "&ripps2roboto_raw" in fnt
assert "ALIGN_RIGHT" in elems and "THM_HL_BAR" in elems and "drawNameAttributeText" in elems
assert '!strcmp(cfgPattern, "SCRn")' in elems

# The single unified built-in cfg carries all four views, uses the PSBBN fonts and info page, and keeps Colors editable.
cfg = read("misc/conf_theme_OPL.cfg")
assert "font1=builtin:roboto" in cfg and "font13=builtin:ripflow" in cfg, "misc/conf_theme_OPL.cfg"
assert "value=RiptOPL" in cfg and "attribute=#Name" in cfg and "pattern=SCRn" in cfg, "misc/conf_theme_OPL.cfg"
assert "coverflow.main4:" in cfg and "psbbn.main2:" in cfg and "grid.main4:" in cfg, "misc/conf_theme_OPL.cfg must declare all views"
assert not re.search(r"^(?:(?:list|coverflow|psbbn|grid)\.)?(?:bg|text|ui|sel|title)_color=", cfg, re.M), "misc/conf_theme_OPL.cfg must keep Colors picker editable"

print("PASS: built-in PSBBN theme assets, fonts, configs and engine wiring passed")
