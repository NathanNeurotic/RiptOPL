## Comment blocks that look like code (cond-commented-code.tsv)
- src/gfx/plasma.cpp:85-92  8 code-like lines

## Largely identical files (dupes-file-pairs.tsv)
- include/renderman.h is 16% the same as POPSLoader/src/include/graphics.h (12 cloned normalised lines)
- src/gfx/fntsys.cpp repeats itself: 20 cloned normalised lines inside the file (2%)

## Duplicate blocks (dupes-blocks.tsv)
- include/renderman.h:38-54  ==  POPSLoader/src/include/graphics.h:18-34  (12 normalised lines)
- src/gfx/fntsys.cpp:710-740  ==  RiptOPL/src/gfx/fntsys.cpp:851-881  (20 normalised lines)
- src/gfx/fntsys.cpp:851-881  ==  RiptOPL/src/gfx/fntsys.cpp:710-740  (20 normalised lines)

## TODO/FIXME/HACK style markers (cond-markers.tsv)
- src/gfx/fntsys.cpp:672  // TODO: this seems correct, but the rest of the OPL UI (i.e. spacers) doesn't seem to be correctly scaled.
- src/gfx/fntsys.cpp:683  /* TODO: Ineffective on many parts:
- src/gfx/plasma.cpp:145  // TODO: Low priority... This could be done on VU0 (xyzw for the first 4 mixes)
- src/gfx/plasma.cpp:151  // TODO: The VU0MixVec could as well mix the results (as follows) - might improve performance...
