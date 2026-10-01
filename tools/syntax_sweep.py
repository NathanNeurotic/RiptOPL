"""Compile every src/*.cpp -fsyntax-only under each flavour's define set, so code behind #ifdef that the
default build never sees (RA, IGS, RTL, DTL, debug variants) still compiles as C++.
Needs build/compile_commands.json (run `ps2build generate` first).   tools/syntax_sweep.py [SET ...]
EESIO needs the siocookie header package and fails on SIOCookie.h in a stock SDK; that is not a C++ error.
"""
import json, subprocess, shlex, concurrent.futures as cf, collections, glob, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
cc = json.load(open(os.path.join(ROOT, 'build', 'compile_commands.json')))
cmds = {c['file']: c for c in cc}
first = [f for f in cmds if f.endswith('.cpp') and '/src/' in f][0]
c = cmds[first]
base = shlex.split(c['command']) if 'command' in c else c['arguments']
def mk(f, defs):
    out = []; skip = 0
    for x in base:
        if skip: skip -= 1; continue
        if x in ('-o', '-MF'): skip = 1; continue
        if x in ('-MMD', '-c') or x == first: continue
        out.append(x)
    return out + defs + ['-fsyntax-only', '-w', f]
RA_ONLY = {'discsupport','rabadge','rahash','ranet','rawatch'}
sets = {'RA': ['-DRETROACHIEVEMENTS'], 'IGS': ['-DIGS'], 'RTL': ['-D__RTL'], 'DEBUG': ['-D__DEBUG','-D__OPLDIAG','-DTTY_UDP','-g'], 'INGAME': ['-D__DEBUG','-D__OPLDIAG','-DTTY_UDP','-D__INGAME_DEBUG'], 'DTL': ['-D_DTL_T10000'], 'PPC': ['-D__DEBUG','-DTTY_PPC_UART'], 'EESIO': ['-D__DEBUG','-D__EESIO_DEBUG'], 'DECI2': ['-D__DEBUG','-D__DECI2_DEBUG','-D__INGAME_DEBUG']}
def run(args):
    r = subprocess.run(args, capture_output=True, text=True, cwd=os.path.join(ROOT, 'build'))
    return r.stderr
only = sys.argv[1:] or list(sets)
for name in only:
    defs = sets[name]
    files = sorted(glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True))
    if name != 'RA': files = [f for f in files if os.path.basename(f)[:-4] not in RA_ONLY]
    errs = []
    with cf.ThreadPoolExecutor(8) as ex:
        for err in ex.map(run, [mk(f, defs) for f in files]):
            errs += [os.path.relpath(l.split(':')[0], ROOT) + ':' + ':'.join(l.split(':')[1:])[:170] for l in err.split('\n') if 'error' in l and ('/src/' in l or '/modules/' in l or '/include/' in l)]
    print(f"== {name}: {len(errs)} errors")
    for l in errs[:40]: print("  ", l)
