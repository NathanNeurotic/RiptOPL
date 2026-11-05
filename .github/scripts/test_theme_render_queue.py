"""Exercise the production theme publish tail against a retained HIRES upload queue.

gsKit HIRES replays its last queue until a flip replaces it. Inline upload tags
refer to EE texture pixels, so a CPU frame lock alone cannot make those freeable.
This models that ownership contract, not PS2 DMA execution or console acceptance.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
renderer = (root / 'src/gfx/renderman.cpp').read_text()
themes = (root / 'src/gfx/themes.cpp').read_text()
retire = re.search(r'^void Renderer::discardFrame\(void\)\n\{.*?^\}', renderer, re.M | re.S).group(0)
wrapper = re.search(r'^void rmDiscardFrame\(void\)\n\{.*?^\}', renderer, re.M | re.S).group(0)
load = themes[themes.index('static int thmLoad('):themes.index('int thmRefreshFontSize(')]
publish = load[load.rindex('    host::ui().lock();'):load.rindex('    return 0;')]
assert publish.index('rmDiscardFrame();') < publish.index('thmFree(curT);') < publish.index('host::ui().unlock();')

fixture = r'''
#include <cassert>
#include <cstdio>
#include <stdexcept>
#define THEME_BOOT_TRACE(...) ((void)0)
struct Theme { int pixels; };
static Theme *gTheme;
static Theme *replayed;
static bool locked;
static int syncs, flips, frees;
static int loadedFontSize, gFontSize;
static void *gsGlobal;
struct UI {
    void lock() { assert(!locked); locked=true; }
    void unlock() { assert(locked); locked=false; }
};
namespace host { static UI instance; UI &ui() { return instance; } }
class Renderer { public: bool hires_; void discardFrame(void); };
static Renderer gRenderer;
static void gsKit_hires_sync(void *) { assert(locked); ++syncs; }
static void gsKit_hires_flip(void *) {
    assert(locked && syncs==flips+1);
    // The between-frame draw queue is empty. Flip waits for the previous DMA,
    // changes the queue the HIRES worker replays, and resets the old queue.
    replayed=nullptr; ++flips;
}
@RETIRE@
@WRAPPER@
static void psbbnFlowReset() { assert(locked); }
static void thmFree(Theme *old) {
    assert(locked);
    if (old && replayed==old)
        throw std::runtime_error("retained HIRES upload references the freed theme");
    if (old) ++frees;
}
static void publish(Theme *curT, Theme *newT) {
@PUBLISH@
}
static void run(bool hires, bool first) {
    Theme old{}, next{};
    gRenderer.hires_=hires; gTheme=first?nullptr:&old;
    replayed=hires && !first?&old:nullptr;
    locked=false; syncs=flips=frees=0;
    publish(gTheme,&next);
    assert(gTheme==&next && !locked && frees==!first);
    assert(syncs==(hires && !first) && flips==syncs);
}
int main() {
    try { run(false,false); run(true,true); run(true,false); }
    catch (const std::runtime_error &e) { std::fprintf(stderr,"%s\n",e.what()); return 3; }
}
'''
with tempfile.TemporaryDirectory(prefix='theme-render-queue-') as folder:
    folder = Path(folder)
    for name, tail, expected in [('fixed', publish, 0), ('before', publish.replace('rmDiscardFrame();', '(void)0;'), 3)]:
        # Before control retains the old publish/free sequence while omitting only the fix.
        source = fixture.replace('@RETIRE@', retire).replace('@WRAPPER@', wrapper).replace('@PUBLISH@', tail)
        if name == 'before':
            source = source.replace('assert(syncs==(hires && !first) && flips==syncs);', '')
        cpp = folder / (name + '.cpp')
        exe = folder / name
        cpp.write_text(source)
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', str(cpp), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        assert result.returncode == expected, (name, result.returncode, result.stderr)
        if name == 'before':
            assert 'retained HIRES upload references the freed theme' in result.stderr
print('PASS: old theme retained through HIRES queue retirement; baseline fails, fixed path passes; non-HIRES and first load do not flip')
