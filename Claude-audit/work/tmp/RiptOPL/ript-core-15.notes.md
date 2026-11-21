# ript-core-15 notes (lens structure)

## Done
- src/gfx/fntsys.cpp 1-1324 DONE (re-read in attempt 2)
- include/grid.h 1-114 DONE
- src/gfx/grid.cpp 1-714 DONE
- include/plasma.h 1-36 DONE
- src/gfx/plasma.cpp 1-260 DONE
- include/psbbnflow.h 1-138 DONE
- src/gfx/psbbnflow.cpp 1-313 DONE
- include/renderman.h 1-169 DONE
ALL READ. Findings written and validated OK (9 findings).

## Candidates
- src/gfx/fntsys.cpp:709-962 | two renderString bodies under #ifndef __RTL / #else; __RTL path (isRTL,isWeak,renderSubRTL) vs txtShape path | check which builds define __RTL, whether txtShape does reorder, fontsystem.h renderSubRTL decl
- src/gfx/fntsys.cpp:776-783,837-844,909-916,1245-1252 | kerning blocks unreachable: previous starts 0 and is only assigned inside if(previous) | check GlyphRun in fontsystem.h, no other writer of previous
- src/gfx/fntsys.cpp:151-161,169 | fntCacheFlushPage clears a page that is freed on the next line | check callers
- src/gfx/fntsys.cpp:1260-1324 | fnt* forwarders to single static FontSystem gFonts | read fontsystem.h, fntsys.h; count
- src/gfx/fntsys.cpp:1135-1195 vs 1049-1133 | fntFitString vs fntWrapText two wrappers | check callers
- src/gfx/fntsys.cpp:549,558,561,569,603-606,613-615,576 | commented-out LOGs, commented safeguard, NULL-face log then deref, extra printf arg | trivial group
- src/gfx/fntsys.cpp:975-978 vs 1060,1085,1120 | fntTextWidth helper exists but fntWrapText repeats rmUnScaleX(fntCalcDimensions()) | trivial
- src/gfx/fntsys.cpp:683-691,757-762 | stale TODO/notes comments | trivial
- fntLoadDefault(path) | check callers pass non-NULL anywhere
- grid.h helpers gridAtRowStart/gridPageStep/gridRowStep/gridSlideEase/gridClamp users
- grid.cpp:320-333 overlay draw dup with psbbnflow.cpp:162-176 and coverflow.cpp?
- grid.cpp:482 + psbbnflow.cpp:188-191 pageName dup of menuItemGetText
- grid.cpp:681,707 flip key read twice
- plasma.cpp:40-49 #else gcc<=3 branch dead; 84-92 commented code; 231-241 history comment + blend alias; ease() users
- plasma.h:6 "src/plasma.cpp" stale path
- renderman.h: check every decl has def+users (rmSetReflectionYOffset, rmDrawFramedRect, rmDrawTriangles, rmSetDisplayOffset, rmSetOverscan, rmSetAspectRatio, rmIsWideDisplay, rmGetHsync, gCol*, gDefaultAlpha, DIM_UNDEF, GS_MODE_DTV_576P, rmInit/rmEnd)
- renderman.h:38-54 vs POPSLoader graphics.h:18-34 cross-repo (likely not actionable)
- psbbnflow: psbbnFlowTrigger/Reset/IsMoving users
