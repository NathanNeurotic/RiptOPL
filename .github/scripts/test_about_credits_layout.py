#!/usr/bin/env python3
"""Host regression test for About dialog layout and tester credits.

Ports upstream PR #944:
1. The Quality Assurance rows credit the testers.
2. The obsolete in-app Financial Support section is removed from diaAbout.
3. Excess UI_BREAK rows are trimmed to prevent vertical overflow in the fixed non-scrolling dialog.
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
assert '"NathanNeurotic - sahlberg - techwritescode"' in dia_about, "NathanNeurotic and sahlberg should be credited in diaAbout"
assert '"Akilluminati47 - belomaxorka - darkladark - mihhru"' in dia_about
net = dia_about.find("_STR_NET_UPDATE")
assert net != -1, "Network update row is present"
icyson = dia_about.find('"icyson55"', net)
assert icyson != -1, "icyson55 is credited on the Network update row"
assert icyson < dia_about.find("UI_BREAK", net), "Network update and icyson55 share a row"
print("PASS: NathanNeurotic credited; About keeps its row count")

# 5. Project Home follows Support Forums; no OK button: any button closes the page (diaShowPage)
forum = dia_about.find("_STR_FORUM_DETAILS")
home = dia_about.find("_STR_PROJECT_HOME")
assert forum != -1 and home > forum, "Project Home sits below Support Forums"
assert '"github.com/NathanNeurotic/RiptOPL"' in dia_about[home:]
assert "UI_OK" not in dia_about, "About has no OK button"
gui_cpp = (root / "src/ui/gui.cpp").read_text(encoding="utf-8")
assert "diaShowPage(diaAbout);" in gui_cpp, "About closes on any button"
print("PASS: Project Home row present; About closes on any button")
