# ript-core-38.b notes (lens defects)

Done: all 11 files read fully (guigameid.cpp, guimodal.h/.cpp, guinotify.h/.cpp, guioverlay.h/.cpp, guiqueue.h/.cpp, guiscreens.h/.cpp).

Candidates:
- src/ui/guimodal.cpp:226,274,306,396 | guiShow() full menu render each frame then opaque gColBlack rect; also guiwait.cpp:204-214 | KEEP (cat 5)
- src/ui/guimodal.cpp:435-444 | CheatScreen::finish always save+configWrite; cheatconfig.cpp:100-110 removeKey marks modified even if unchanged | KEEP (cat 5)
- src/ui/guinotify.cpp:178-190 | RA notice absolute deadline vs clock wrap (header guinotify.h:33-35 warns); same in sysdisc.cpp:134-137,185-197, menuwatch.cpp:223-232 | KEEP rank 1
- guinotify show() strchr(gOPLPart) NULL | DROPPED: gOPLPart always "hdd0:..."
- guioverlay drawBusy modulo loadingIconCount | DROPPED: count >= 1 (themes.cpp:1702-1712)
- CheatScreen with 0 cheats | DROPPED: harmless
- guiqueue drain lock / raText ownership | DROPPED: handled in gui.cpp:212-223
