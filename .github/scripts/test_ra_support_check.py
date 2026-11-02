"""RA: check game support reaches every launch path that can carry telemetry.

Ported from the original's test_ra_udpfs_check. Neutrino and UDPFS launches carry the RA kit's ORBIT
agent (sbNeutrinoPrepareRA), so the check must hash their games instead of refusing; a PS2 favourite
is checked on its source device; POPStarter rows get the RA entries, Ember rows do not.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
menusys = (ROOT / "src/ui/menusys.cpp").read_text(encoding="utf-8")
ranet = (ROOT / "src/sources/ranet.cpp").read_text(encoding="utf-8")
base = (ROOT / "lng/tmpl/_base.yml").read_text(encoding="utf-8")


def body(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


check = body(menusys, "static void menuCheckSelectedGameSupportLocked(")
assert "UDPFS_MODE" not in check and "_STR_RA_NA_UDPFS" not in check, "UDPFS games are still refused"
assert "gameMenuCoreIsNeutrino" not in check and "_STR_RA_NA_NEUTRINO" not in check, "Neutrino games are still refused"
generic = check.index("gsOf(support)->hasGame() && gsOf(support)->hasPrefix()")
assert "sbHashGameDeferred(prefix, g->name, g->extension, g->startup, g->format)" in check[generic:]
fav = check.index("gFavSupport.sourceItem(gid, &sourceView, &gid)")
assert fav < check.index("if (rowView == LIB_VIEW_PS1)"), "a favourite must resolve before the device branches"
for udpfs_leg in ("src/sources/udpfssupport.cpp", "src/sources/bdmlaunch.cpp"):
    assert "sbNeutrinoPrepareRA(" in (ROOT / udpfs_leg).read_text(encoding="utf-8"), udpfs_leg
print("PASS: the support check hashes Neutrino, UDPFS and favourite games")

vcd_menu = body(menusys, "void menuInitVcdMenu(int isEmber, int readOnly)")
ra = vcd_menu.index("#ifdef RETROACHIEVEMENTS")
assert "if (!isEmber)" in vcd_menu[ra:] and "GAME_RA_CHECK" in vcd_menu[ra:] and "GAME_RA_ACHIEVEMENTS" in vcd_menu[ra:]
app = body(menusys, "void menuHandleInputAppMenu()")
for action in ("menuCheckSelectedGameSupport();", "sbTestPCLinkDeferred();", "menuOpenSelectedVcdAchievements();"):
    assert action in app, action
print("PASS: POPStarter rows have the RA entries; Ember rows do not")

# A list that reached memory but could not be saved is not plain success: the launch reads the file.
ask = body(ranet, "int Ranet::askPc(")
assert "_STR_RA_WL_NOT_SAVED" in ask and "- label: RA_WL_NOT_SAVED" in base
print("PASS: an unsaved watch list is reported")
