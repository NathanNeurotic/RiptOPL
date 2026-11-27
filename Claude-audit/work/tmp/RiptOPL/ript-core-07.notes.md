# ript-core-07 notes
Done: all 19 files read fully. Checks done. Writing findings JSON.
Findings planned: include blocks (autolaunch/lwnbdctl), host virtual layer, host applyConfig dead + stale comments, SourceRegistry wrapper, utf8d dup (MEASURED 2x400B), IO_CACHE_LOAD_ART, opl.h clutter (BLURT, dup debug.h, opl.c refs), themeint BG_REQUEST + orphan comment, autolaunch cleanup ladders, lwnbdctl small items, raii UniqueFile test-only, bdmFatfsOpenErrno static, gNetBootProtocol shadow.
Refuted leads: raii.h member functions (~UniqueFd, operator=, operator bool, operator(), ~ScopeExit), ~SemaLock are class members used implicitly; DISPLAY_DEFINED is a theme config value (_display=1) handled by else-branches.
