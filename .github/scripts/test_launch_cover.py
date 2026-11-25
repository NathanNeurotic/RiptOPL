"""The game launch animation is a counted loop that draws only art already in memory."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]


def body(path, signature):
    source = (root / path).read_text(encoding="utf-8")
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 0, brace
    while end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
        if depth == 0:
            return source[start:end]
    raise AssertionError(signature)


launch = body("src/ui/guilaunch.cpp", "void Gui::playLaunchCover()")
# A fixed number of frames, with no wait on a clock, a device or a thread.
assert re.search(r"for \(int frame = 1; frame <= LAUNCH_IN \+ LAUNCH_HOLD \+ LAUNCH_OUT; \+\+frame\)", launch)
for waits in ("while", "clock(", "DelayThread", "WaitSema", "ioPutRequest", "cacheGetTexture"):
    assert waits not in launch, waits
assert launch.count("startFrame();") == 1 and launch.count("endFrame();") == 2
# No cover in memory: the plain fade it replaced, decided on the first frame.
assert "if (!drawn && frame == 1)" in launch and "guiFadeCurrentScreenToBlack();" in launch
# The held fade never outlives the frame that set it.
assert launch.index("holdContentFade((u8)") < launch.index("show();") < launch.index("holdContentFade(0x80);")

cover = body("src/gfx/themeelems.cpp", "GSTEXTURE *thmResidentCover(")
assert "getGameImageCached(" in cover and "getGameImageTexture" not in cover, "the launch must never ask for a load"
draw = body("src/ui/menusys.cpp", "int menuRenderLaunchCover(float in)")
assert "thmResidentCover(" in draw and "getGameImageTexture" not in draw
assert draw.count("WaitSema(menuSemaId);") == 1 and draw.count("SignalSema(menuSemaId);") == 1 and "return drawn;" in draw
assert draw.index("rmSetFade((u8)(in * 0x80)") < draw.index("rmSetFade(0x80, 0.0f);")

# Both ways into the launch transition end in it, and the launch still happens after the art worker is held.
transition = body("src/ui/guilaunch.cpp", "void Gui::playLaunchTransition()")
assert transition.count("playLaunchCover();") == 2
opl = (root / "src/app/opl.cpp").read_text(encoding="utf-8")
for m in re.finditer(r"guiPlayLaunchTransition\(\);", opl):
    assert "cacheHoldArtStarts();" in opl[m.start() - 200:m.start()]
# The screen size comes from guiint.h, which neither file gets through another header.
for path in ("src/ui/guilaunch.cpp", "src/ui/menusys.cpp"):
    assert '#include "include/guiint.h"' in (root / path).read_text(encoding="utf-8"), path
print("PASS: the launch animation is bounded and draws only resident art")
