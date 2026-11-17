# ript-core-16 notes (lens structure)

## Done (all read in full)
- src/gfx/renderman.cpp 1-965 (also read include/renderer.h and include/renderman.h for context)
- include/saveicon.h 1-100
- src/gfx/saveicon.cpp 1-1315
- include/texcache.h 1-280
- src/gfx/texcache.cpp 1-634
- include/textshape.h 1-28

## Candidates (to verify by grep)
- src/gfx/renderman.cpp:817-965 | 34 rm* one-line forwarders to the single static Renderer gRenderer; class in include/renderer.h is a singleton | count callers, check nothing else uses Renderer
- src/gfx/renderman.cpp:16 | __VRAM_SIZE macro | grep usage
- src/gfx/renderman.cpp:115-119, 462-463 | orphan comments whose variables moved to renderer.h | trivial
- src/gfx/renderman.cpp:271 | commented-out ATE line | trivial
- src/gfx/renderman.cpp:372-377 | getScreenExtents returns constants 640x480 | grep callers of rmGetScreenExtents
- src/gfx/renderman.cpp:121-126 | gCol* consts | grep each for use
- src/gfx/renderman.cpp:774-787, 942-955 | scaleXi/scaleYi/unScaleX | grep callers
- src/gfx/renderman.cpp:812-815 | getHsync + hsync column | grep rmGetHsync
- rmDrawQuad, rmDrawLine, rmInvalidateTexture, rmUnloadTexture, rmSetAspectRatio, rmWideScale, rmGetScreenExtentsNative | grep callers
- src/gfx/saveicon.cpp:578-602 | saveIconFromPs1CardImage serial param: only shipped caller (897) passes NULL, so pass loop + filename match is test-only | grep tests
- src/gfx/saveicon.cpp:650-655 | siNameLaunchable re-implements cueNameLaunchable (comment says so) | read cuesupport
- src/gfx/saveicon.cpp:664-686 | siReadFile whole-file read helper | look for other copies (reusable helper)
- src/gfx/saveicon.cpp:700-707 vs 893-898 | open/lseek/card-image/close twice | small
- src/gfx/saveicon.cpp:1099-1104 vs 1129-1133, 1063-1072 vs 1122-1127 | settle+queue and want-set blocks twice | small
- src/gfx/saveicon.cpp:1136-1175 | nine separate work-buffer mallocs | maybe skip
- include/saveicon.h | check every exported function + SAVEICON_* macro for callers (host tests?)
- include/texcache.h:101 | requestQueued no caller; texcache.cpp:520 increments activeRequests_ directly | grep
- include/texcache.h:238-247 | cacheCancelPendingImageLoads empty inline stub | grep callers
- include/texcache.h:79,81,97 | userId/count/isPrefixRelative accessors | grep
- include/texcache.h:188-206 | cacheDebug* HUD getters | grep callers; `refused` counter for a depth cap that is gone
- include/texcache.h comments 20-47, 141-150, 219-232, 238-244 ; texcache.cpp 409-421, 482-512 | history narrative, refers to themes.c/opl.c, to removed backpressure cap, wrong priorities | stale comments finding
- src/gfx/texcache.cpp:7-16 | includes left from split with artload.cpp (artindex, textures, tar, pad, sound, util) | grep symbols
- src/gfx/texcache.cpp:33-41 | redundant zero assignments after memset | trivial
- src/gfx/texcache.cpp:151-162 vs 191-202 | FAV/APP proxy mode resolution twice | grep favGetArtMode for third copy
- src/gfx/texcache.cpp:263-273 vs 318-328 (+525) | in-flight adoption block twice with same comment; inner req != NULL check always true | small
- src/gfx/texcache.cpp:385 | suffix_ != NULL always true | trivial
- src/gfx/texcache.cpp:631-634 | cacheGetTexture wrapper | grep callers
- include/textshape.h | check each txt* used outside textshape.cpp
