# ript-core-35.b notes (defects lens)

Done (read_full): include/cfgtitle.h, src/ui/cfgtitle.cpp, include/dia.h, src/ui/dia.cpp, src/ui/diaeditors.cpp, src/ui/diaitems.cpp, include/dialogs.h

Verified / kept:
- diaeditors.cpp:104-109,215-238 | 1-byte VLA overflow when strlen(text)==maxLen; guifilemgr.cpp:523 passes FM_NAME-1 with names up to 127 (filemgr.cpp:208-210) | KEEP rank 3
- diaeditors.cpp:298-307,360-368,395-403 | caret/backspace byte-wise, split UTF-8 (helpers 112-126 exist) | KEEP rank 2 INFERRED encoding
- dia.cpp:839-868 + diaitems.cpp:97-111 | cancel restores page-open def, not pre-edit value | KEEP rank 2

Dropped:
- dialogs.h enum overlap: script showed no duplicate id in any static dialog nor composed page (guisettings.cpp parts lists)
- diaSettingsFitGap per-frame: walks only measure standalone labels, cheap
- cfgtitle stale index: forget before clear, refresh after rebuild (opl.cpp:711-842)
- diaShowColSel gSelectButton == guiConfirmKey (guimodal.cpp:192-195)
- UI_ENUM NULL enumvalues: every enum id has a diaSetEnum call (script)
