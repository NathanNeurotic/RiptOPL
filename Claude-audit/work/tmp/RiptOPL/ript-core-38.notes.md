ript-core-38 notes
Done: all 11 files read fully (guigameid.cpp, guimodal.h/.cpp, guinotify.h/.cpp, guioverlay.h/.cpp, guiqueue.h/.cpp, guiscreens.h/.cpp)

Candidates:
- guigameid.cpp:22-58, guimodal.cpp:9-46 | copy-pasted 35-include block with stale comments; sound.h twice | check which headers needed, other copies (guiwait, gui.cpp, guisettings)
- guigameid.cpp:7-20,165-196 | long history comment, NOTE(rebuild) stale | clutter
- guigameid.cpp:96-98 | double cap 11 | redundant
- guigameid.cpp:157-202 | two near-identical frame loops | merge
- guimodal.cpp:81-84,125 + guimodal.h:38-39 | wrapText forwarder; duplicate of guiDrawMsgText | check
- guimodal.cpp frame (black rect + 2 rules) x5 | grep other copies
- guimodal.cpp:130,152,154,292,316,319,430 | inline ternaries dup guiConfirmKey etc | check
- guimodal.h:19 begin virtual no override? | grep
- guimodal.cpp:169 guiIsActive callers?
- guimodal.cpp:448 gCheats NULL check type?
- guinotify.cpp:178-187 | RA absolute deadline wraps (bug) | RA build only
- guinotify.cpp:171-198 renderRA only via drawRA | merge
- guinotify.cpp:118-139 3 dup blocks | small
- guinotify/guioverlay/guiscreens C forwarders | check callers
- guioverlay.cpp:136-190 | huge investigation comment | clutter
- guiqueue enqueue return value used? shutdown used?
- guiscreens transitioning/setCurrent used? dispatchInput null check
- guinotify.h showPartition/setToast/drawToast used?
