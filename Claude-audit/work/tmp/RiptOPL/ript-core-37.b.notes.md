Unit ript-core-37.b notes (lens defects)

Done: all six files fully read (guifilemgr.cpp 1-682; guiframe.h; guiframe.cpp; guigame.h; guigame.cpp 1-2617; guigameid.h)
Next: write findings JSON, validate.

Candidates (checked):
- F1 size .bss: guigame.cpp:326-331 vmcSaves arrays + guifilemgr.cpp:374,489 static sel[512] x2 = 30,792 B MEASURED (raw/riptopl-nm-size-sorted.full.txt lines 100,101,177,2421,2422,360,361)
- F2 size heap: guifilemgr.cpp:53,58,619 fmLabel 512 x 160 = 80 KB; folder labels clamp FM_TITLE 48 (238); 80-byte rows save 40 KB transient
- F3 UI freeze no feedback: guigame.cpp:439,442,455,457,467 VMC Saves actions (vmcsaves page-at-a-time I/O, vmcsaves.cpp:173-184) and 1826 sysGetDiscID (system.cpp:494-545 spin-up waits)
- F4 perf: guigame.cpp:371-382,473 both lists re-read after every action incl. declined delete
- F5 perf: guifilemgr.cpp:228,235 reads 964 B icon.sys, title needs 260 (vmcsaves.cpp:46,1061)
- F6 logic: guifilemgr.cpp:449-452,469-479 Cut .psu onto mc unpacks but leaves source and drops clipboard entry (HINT_FM_CUT _base.yml:2333)
Dropped: fmOpen dangling dir (fmList fails before write); diaShowKeyb maxLen ok; enum out-of-range clamped by diaEnumText; coreLoader static overwrite harmless (re-read before save); guiframe clock units unverifiable; hexDiscID 15 ok; PadEmu bit math ok; ver_to_str fits.
