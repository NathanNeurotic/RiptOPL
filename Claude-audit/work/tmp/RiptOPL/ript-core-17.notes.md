# ript-core-17 notes (lens structure)

## Done (read every line; re-read in attempt 2)
- src/gfx/textshape.cpp 1-452
- include/textures.h 1-238
- src/gfx/textures.cpp 1-1438

## Verified -> findings
- F1 worst-open latch + gBgmInRead only reader (textures.cpp 550-584, 929-943; textures.h 231-234; guioverlay 193-194,211-213; bgm.cpp/sound.h gBgmInRead)
- F2 stale/contradictory comments 495-525, 661-667, textures.h 222-223, guioverlay 179-182
- F3 unembedded gfx PNGs Device_*, no_Device, no_Rating, logo4-6 (29,792 bytes)
- F4 commented-out enum/extern blocks textures.h 36-42, textures.cpp 57-61
- F5 jsonPutUtf8 (achievements.cpp 158-178) duplicates txtPut
- F6 small leftovers: ERR_MISSING_ALPHA, maxSize, identity ternaries, (void)filePath, inner texIsJpg

## Dropped
- texLoadFromMemory, txtZeroWidth, inline helpers, RTL code (Arabic/Persian langs exist): used
- texture_t.id: test_release_editions.py asserts id==i (ordering guard), keep
- Device_* NULL table rows: names reachable from theme cfgs, keep
- tarThemeRead twice: cheap string scan, skip
- fit arithmetic twice, fast-path fallback: too small / needed
