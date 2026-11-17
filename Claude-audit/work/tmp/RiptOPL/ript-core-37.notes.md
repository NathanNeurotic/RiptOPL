ript-core-37 notes
Done: all 6 files read fully. Checks done (grep): guigame.h decls all used; GameIdBarcode only in guigameid.cpp; FrameClock used gui.cpp/guioverlay.cpp;
genvmc.h already included by sysvmc/vmcrebuild/bdmsupport/hddsupport; system.cpp:69-76 createVMCparam_t unused;
guiGameVMCSavesSync == vmcRebuildSync (vmcrebuild.cpp:89-103); fmIsMcPath == fmIsMc filemgr.cpp:82, suIsMc selfupdate.cpp:117;
GSM_1080P in both base define lists, no tool removes it; compat statics dead stores confirmed.
Dropped: PadEmu initial visibility dup (updater runs after first frame), fmScanDrives vs dsScan (helper would not net-remove), AutoCreateVmc loader vs Resolve (not provably equal).
Writing findings file next.
