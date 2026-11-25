#!/usr/bin/env python3
"""Host regression test for the About page.

1. The links (Support Forums, Project Home) come first, right under the page header.
2. Then one wall of names with no headings, the same in every build (no RETROACHIEVEMENTS-only rows), holding
   everyone CREDITS, README and CHANGELOG credit.
3. No Financial Support section and no OK button: any button closes the page (diaShowPage).
4. The page is taller than the screen, so D-pad Up/Down slide it instead of closing it.
"""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
dialogs_cpp = (root / "src/ui/dialogs.cpp").read_text(encoding="utf-8")

dia_about_start = dialogs_cpp.find("struct UIItem diaAbout[] = {")
assert dia_about_start != -1, "diaAbout not found in dialogs.cpp"
dia_about_end = dialogs_cpp.find("{UI_TERMINATOR}};", dia_about_start)
assert dia_about_end != -1, "diaAbout terminator not found"
dia_about = dialogs_cpp[dia_about_start : dia_about_end + len("{UI_TERMINATOR}};")]

assert "_STR_FINANCIAL_SUPPORT" not in dia_about, "Financial support section should not be in diaAbout"
assert "#ifdef" not in dia_about and "#else" not in dia_about, "every build shows the same names"

# 1. Links first
forum = dia_about.find("_STR_FORUM_DETAILS")
home = dia_about.find("_STR_PROJECT_HOME")
assert forum != -1 and home > forum, "Project Home sits below Support Forums"
assert '"psx-place.com"' in dia_about[forum:home]
assert '"github.com/NathanNeurotic/RiptOPL"' in dia_about[home:]
first_name = dia_about.find('"NathanNeurotic - ')
assert first_name != -1, "the wall of names starts with NathanNeurotic"
assert home < first_name, "the links sit above the names, right under the header"
print("PASS: links right under the page header")

# 2. One wall of names, no credit headings
for heading in ("_STR_DEVS", "_STR_QANDA", "_STR_NET_UPDATE", "_STR_ABOUT_THANKS"):
    assert heading not in dia_about, heading + " heading should be gone"
wall = " - ".join(re.findall(r'\{\.label = \{"([^"]+)", -1\}\}', dia_about[first_name - 40 :]))
names = [n.strip() for n in wall.split(" - ")]
expected = """NathanNeurotic BatRastard bbsan2k belek666 crazyc dlanor doctorxyz hacan359 oMrRexD hominem.te.esse ifcaro
izdubar jimmikaelkael KrahJohlito kr_ps2 Maximus32 misfire Polo35 reprep saildot4k SP193 volca Akilluminati47
belomaxorka darkladark mihhru danielnuld sahlberg techwritescode zokzot fjtrujy Gageformer krHACKen Okeanos israpps
algol Berion Blade1984 El_Patas EP gledson999 jolek lee4 Vapor LocalH RandQalan ShaolinAssassin yoshi314 zero35
zackcage6 CosmicScale eliminator TnA V3ditata AcidReach Aerosol Aislinn AndrewBento bodvenomz Ethyls FifthFox
jennyjan107-web lucaslmgv nuno6573 Volventura icyson55""".split()
expected += ["Eugene Plotnikov", "Marcus R. Brown", "Eric Young", "L. Peter Deutsch", "Davood TiTaN"]
for name in expected:
    assert name in names, name + " should be on the About page"
assert len(names) == len(set(names)), "each name appears once"
print("PASS: one wall of %d names, no headings" % len(names))

# 3. No OK button; any button closes the page
assert "UI_OK" not in dia_about, "About has no OK button"
gui_cpp = (root / "src/ui/gui.cpp").read_text(encoding="utf-8")
assert "diaShowPage(diaAbout);" in gui_cpp, "About closes on any button"

# 4. A tall page slides with the D-pad
dia_cpp = (root / "src/ui/dia.cpp").read_text(encoding="utf-8")
page = dia_cpp[dia_cpp.find("void diaShowPage(") :]
page = page[: page.find("\n}\n")]
assert "getKeyPressed(KEY_DOWN)" in page and "getKeyPressed(KEY_UP)" in page, "D-pad slides a tall page"
assert "key == KEY_UP || key == KEY_DOWN" in page, "Up/Down do not close a tall page"
print("PASS: no OK button; a tall About page slides with the D-pad")
