"""The built-in Korium theme matches the original's Korium edition (test/korium-audit-remediation).

- Its cfgs draw attribute=Players, so the built-in theme must load the PLAYERS_1..4 badges.
- An app with no cover shows Korium's own cover, never inceBtion's placeholder or foliage layer.
- Per-game background art starts off, as the original's Korium sets it.
- List Title and Description has a Korium layout, kept off by the theme (list_cfg_text=0).
"""
from pathlib import Path
import json
import re
import zipfile

root = Path(__file__).resolve().parents[2]
read = lambda path: (root / path).read_text(encoding="utf-8")

# The vendored Korium cfgs really ask for the Players badge.
with zipfile.ZipFile(root / "third_party/korium/assets.zip") as package:
    cfgs = [package.read(n).decode("utf-8", "replace") for n in package.namelist() if n.endswith(".cfg")]
assert any(re.search(r"attribute\s*=\s*Players", cfg) for cfg in cfgs), "Korium cfgs no longer use Players"

themes = read("src/gfx/themes.cpp")
builtin = themes[themes.index("    if (!themePath) {\n        for (i = ELF_FORMAT; i <= VMODE_PAL; i++)"):]
builtin = builtin[:builtin.index("\n    }\n") + 6]
assert "#ifdef BUILTIN_KORIUM" in builtin and "for (i = PLAYERS_1; i <= PLAYERS_4; i++)" in builtin, builtin

textures = read("src/gfx/textures.cpp")
korium = {}
for block in re.findall(r"#ifdef BUILTIN_KORIUM\n(.*?)#(?:else|endif)", textures, re.S):
    for texture, source in re.findall(r"\{(\w+), \"[^\"]*\", (&?\w+)\}", block):
        korium[texture] = source
assert korium.get("COVERAPP_DEFAULT") == "&cover_png", korium.get("COVERAPP_DEFAULT")
assert korium.get("CASE_OVERLAY2") == "NULL", korium.get("CASE_OVERLAY2")
for player in range(1, 5):
    assert korium.get(f"PLAYERS_{player}") == f"&Players_{player}_png"

# Nothing references the two inceBtion bitmaps in a Korium build, so they are not embedded either.
assets = read("tools/assets.py")
assert 'pngs = [p for p in pngs if p not in ("coverapp", "case_overlay")]' in assets
assert "gfx/coverapp.png" not in json.loads(read("third_party/korium/UPSTREAM.json"))["files"]

settings = read("src/core/settings.cpp")
bgart = settings[settings.index("#ifdef BUILTIN_KORIUM\n    intSetting(CONFIG_OPL_ENABLE_BGART"):]
bgart = bgart[:bgart.index("#endif")]
assert "&gEnableBGArt, 0)" in bgart.split("#else")[0] and "&gEnableBGArt, 1)" in bgart.split("#else")[1]

# The List Title and Description overlay (misc/korium_list_desc.cfg), merged the way tools/assets.py does it,
# then read the way the theme parser does: a "name:" line sets the key prefix for every later key.
with zipfile.ZipFile(root / "third_party/korium/assets.zip") as package:
    vendored = package.read("misc/conf_theme_OPL.cfg")
assert 'overlay = (Path(ROOT) / "misc/korium_list_desc.cfg").read_bytes()' in assets
assert "list_cfg.write_bytes(overlay[:split] + list_cfg.read_bytes() + overlay[split:])" in assets
overlay = (root / "misc/korium_list_desc.cfg").read_bytes()
split = overlay.index(b"\nmain") + 1
def keys(text):
    prefix, out = "", {}
    for line in text.decode("utf-8", "replace").splitlines():
        line = line.strip()
        if "=" in line:
            k, v = line.split("=", 1)
            out[prefix + k.strip()] = v.strip()
        elif ":" in line:
            prefix = line[:line.index(":")].strip() + "_"
    return out
before, merged = keys(vendored), keys(overlay[:split] + vendored + overlay[split:])
assert merged["list_cfg_text"] == "0", "the theme keeps the description off until the setting says On"
assert "main11_type" not in before and "main10_type" in before, "main11 must be Korium's next free list number"
for block in ("main11", "main12"):
    assert merged[block + "_type_desc"] == "AttributeText" and block + "_type" not in merged, block
for k in (k for k in merged if k.endswith("_enabled_desc")):
    assert k[:-len("_enabled_desc")] + "_type" in before, f"{k} names a block Korium does not have"
assert all(merged.get(k) == v for k, v in before.items()), "the overlay changed one of Korium's own keys"

print("PASS: built-in Korium loads its Players badges, keeps its own app cover, starts with background art off and keeps its list description off")
