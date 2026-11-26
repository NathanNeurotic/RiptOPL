"""The theme publish keeps the last frame on screen while a HIRES queue still uploads the old theme's pixels.

gsKit HIRES replays its last queue until a flip replaces it. Inline upload tags refer to EE texture
pixels, so the old theme's pixels must outlive that replay. rmReleaseTexture (texmanager.cpp) keeps
pixels a live queue still uploads until that queue is retired; test_hires_texture_ownership.py
models that. So the publish must not flip to an empty frame (rmDiscardFrame): under HIRES that
showed a black screen for the whole theme load, the hold-L3 layout switch included.
This checks the source contract, not PS2 DMA execution or console acceptance.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
renderer = (root / 'src/gfx/renderman.cpp').read_text()
themes = (root / 'src/gfx/themes.cpp').read_text()
load = themes[themes.index('static int thmLoad('):themes.index('int thmRefreshFontSize(')]
publish = load[load.rindex('    host::ui().lock();'):load.rindex('    return 0;')]
free_start = themes.index('static void thmFree(')
thm_free = themes[free_start:themes.index('\n}\n', free_start)]

failures = []
if 'rmDiscardFrame' in publish:
    failures.append('the theme publish flips HIRES to an empty frame (black until the next frame)')
if not (publish.index('gTheme = newT;') < publish.index('thmFree(curT);') < publish.index('host::ui().unlock();')):
    failures.append('the theme publish no longer swaps and frees the old theme under the GUI lock')
if 'rmReleaseTexture(' not in thm_free or re.search(r'\bfree\([^)]*->Mem\)|texFree\(', thm_free):
    failures.append('thmFree frees theme pixels without the texture manager, which keeps what a live frame uploads')
release = re.search(r'^void rmReleaseTexture\(GSTEXTURE \*txt\)\n\{.*?^\}', renderer, re.M | re.S).group(0)
if 'gsKit_TexManager_release' not in release:
    failures.append('rmReleaseTexture does not defer pixels through the texture manager')
draw_quad = re.search(r'^void Renderer::drawQuad\(rm_quad_t \*q\)\n\{.*?^\}', renderer, re.M | re.S).group(0)
if not re.search(r'if \(gsKit_TexManager_bind\(gsGlobal, q->txt\) == GSKIT_TEXMGR_BIND_DEFERRED\) \{\n'
                 r'        if \(fixedAlpha\)\n'
                 r'            gsKit_set_primalpha\(gsGlobal, gDefaultAlpha, 0\);\n'
                 r'        return;\n    \}', draw_quad):
    failures.append('deferred CT24 draw must restore fixed alpha')

if failures:
    for failure in failures:
        print('FAIL:', failure)
    sys.exit(1)
print('PASS: the theme publish keeps the last frame up (no flip to an empty frame); old pixels go through the texture manager')
