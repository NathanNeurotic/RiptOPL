#!/usr/bin/env python3
"""Host regression test for About dialog layout and tester credits.

Ports upstream PR #944:
1. The Quality Assurance rows credit the testers.
2. The obsolete in-app Financial Support section is removed from diaAbout.
3. Every build credits the same people: no RETROACHIEVEMENTS-only rows, and everyone CREDITS, README and
   CHANGELOG name has a place (testers under QA, the rest under Special Thanks).
4. The page is taller than the screen now, so D-pad Up/Down slide it instead of closing it.
"""
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[2]
dialogs_cpp = (root / "src/ui/dialogs.cpp").read_text(encoding="utf-8")

dia_about_start = dialogs_cpp.find("struct UIItem diaAbout[] = {")
assert dia_about_start != -1, "diaAbout not found in dialogs.cpp"
dia_about_end = dialogs_cpp.find("{UI_TERMINATOR}};", dia_about_start)
assert dia_about_end != -1, "diaAbout terminator not found"
dia_about = dialogs_cpp[dia_about_start : dia_about_end + len("{UI_TERMINATOR}};")]

# 1. Financial support section removed from diaAbout
assert "_STR_FINANCIAL_SUPPORT" not in dia_about, "Financial support section should not be in diaAbout"

# 2. The third Quality Assurance row
assert '"CosmicScale, eliminator, TnA, V3ditata"' in dia_about, "the third QA row should credit CosmicScale, eliminator, TnA and V3ditata"

# 3. QA rows should contain all expected contributors
assert "algol, Berion, Blade1984, El_Patas, EP, gledson999, jolek, lee4, Vapor" in dia_about
assert "LocalH, RandQalan, ShaolinAssassin, yoshi314, zero35, zackcage6" in dia_about

print("PASS: About dialog layout has testers under QA, no financial section, and trimmed spacing")

# 4. NathanNeurotic is credited among the coders, on a fifth row; Network update shares one row with its name so the
#    dialog keeps its height
assert '"NathanNeurotic - danielnuld - sahlberg - techwritescode"' in dia_about, "NathanNeurotic, danielnuld and sahlberg should be credited in diaAbout"
assert '"Akilluminati47 - belomaxorka - darkladark - mihhru"' in dia_about
net = dia_about.find("_STR_NET_UPDATE")
assert net != -1, "Network update row is present"
icyson = dia_about.find('"icyson55"', net)
assert icyson != -1, "icyson55 is credited on the Network update row"
assert icyson < dia_about.find("UI_BREAK", net), "Network update and icyson55 share a row"
print("PASS: NathanNeurotic credited; Network update shares one row with icyson55")

# 6. One set of rows for every build, with everyone CREDITS, README and CHANGELOG name
assert "#ifdef" not in dia_about and "#else" not in dia_about, "every build shows the same credits"
assert '"hacan359 - oMrRexD - hominem.te.esse - ifcaro - izdubar"' in dia_about
for name in ["AcidReach", "Aerosol", "Aislinn", "AndrewBento", "bodvenomz", "Davood TiTaN", "Ethyls", "FifthFox",
             "jennyjan107-web", "lucaslmgv", "nuno6573", "Volventura", "Wolf3s", "mystyq", "Jay-Jay-OPL", "pcm720",
             "Docmine17", "AdityaKumar7209", "Irfanlesnar", "zokzot", "fjtrujy", "Eugene Plotnikov", "Marcus R. Brown",
             "Eric Young", "Gageformer", "krHACKen", "Okeanos", "israpps", "YouKnow-sys", "Luden", "shaan",
             "TheRealNextria", "L. Peter Deutsch", "ps2dev"]:
    assert name in dia_about, name + " should be credited on the About page"
thanks = dia_about.find("_STR_ABOUT_THANKS")
assert dia_about.find("_STR_QANDA") < thanks < dia_about.find("_STR_NET_UPDATE"), "Special Thanks sits between QA and Network update"
base = (root / "lng/tmpl/_base.yml").read_text(encoding="utf-8")
assert "label: ABOUT_THANKS" in base
dia_cpp = (root / "src/ui/dia.cpp").read_text(encoding="utf-8")
page = dia_cpp[dia_cpp.find("void diaShowPage("):]
page = page[: page.find("\n}\n")]
assert "getKeyPressed(KEY_DOWN)" in page and "getKeyPressed(KEY_UP)" in page, "D-pad slides a tall page"
assert "key == KEY_UP || key == KEY_DOWN" in page, "Up/Down do not close a tall page"
print("PASS: every build shows every name; a tall About page slides with the D-pad")

# 5. Project Home follows Support Forums; no OK button: any button closes the page (diaShowPage)
forum = dia_about.find("_STR_FORUM_DETAILS")
home = dia_about.find("_STR_PROJECT_HOME")
assert forum != -1 and home > forum, "Project Home sits below Support Forums"
assert '"github.com/NathanNeurotic/RiptOPL"' in dia_about[home:]
assert "UI_OK" not in dia_about, "About has no OK button"
gui_cpp = (root / "src/ui/gui.cpp").read_text(encoding="utf-8")
assert "diaShowPage(diaAbout);" in gui_cpp, "About closes on any button"
print("PASS: Project Home row present; About closes on any button")
