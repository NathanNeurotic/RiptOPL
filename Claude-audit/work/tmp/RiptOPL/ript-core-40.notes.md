# ript-core-40 notes
Read fully: guisplash.cpp 1-266, guiwait.h 1-49, guiwait.cpp 1-232, libview.h 1-92, libview.cpp 1-365, menuitems.cpp 1-529, menusys.h 1-204.

Candidates:
- guisplash.cpp:70-73 | only LOGO0 drawn; LOGO_PICTURE (logo.png) never drawn since logo0 always embedded; LOGO1..6 decoded each theme load, never drawn; gfx/logo4-6.png not embedded (stray) | CONFIRMED (textures.cpp:120-124,291-301; themes.cpp:1683-1697; textures.h:102-112; themes.h:281; assets.py:53-54). T8 512x128 = 64KB each.
- guisplash.cpp:72, 104 stale comment names (guiDrawBootLogo, gBootStickyLabel) | confirmed no other refs
- guiwait: guiGameHandleDeferedIO unused -> runWithDialog, ui ctor, run() dispatch dead; DeferredIoWait class collapses into guiHandleDeferedIO | confirmed gui.h:197 only
- guiwait.cpp:9-45 include block mostly unused (copy of gui.cpp list) ; sound.h twice | confirmed by read
- guiwait.cpp:162-165 orphan comment
- menuitems.cpp:7-68 include block with stale comments (mkdir, fileXio autolaunch, mmceLoadModules, tarInvalidate) none used in file
- libview.h:79 "libview.c" stale; config.h:125 "src/libview.c"
- libview.cpp:22-26 std::array lambda for one entry (trivial)
- libview.cpp:167 redundant range check (trivial)
- menusys.h: symbol counts pending (bg task b0mzvphhy); line 82-83 duplicated doc comment; line 98 "NULL until it lands" stale
