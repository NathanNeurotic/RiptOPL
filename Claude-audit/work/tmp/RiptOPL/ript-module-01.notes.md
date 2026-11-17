# ript-module-01 notes
Done: misc/conf_theme_OPL.cfg, misc/theme_coverflow.cfg, misc/theme_grid.cfg, misc/theme_psbbn_flow.cfg (all full)

Candidates:
- misc/theme_coverflow.cfg:15-22,192-219,236-252,1 ; misc/theme_grid.cfg:1-5,186-213,230-246 | comment/commented-out blocks embedded raw into ELF via tools/assets.py MISC bin2c (coverflow 1917 B comments, grid 2239 B measured wc) | config parser skips # (config.cpp:1132-1137) -> pure dead bytes
- stale "src/themes.c" reference coverflow:1, grid:5 (now src/gfx/themes.cpp)
- info0..info16 page identical across 3 cfgs (data dup, no include mechanism -> probably not report)
