#!/usr/bin/env python3
"""Module-coupling report for src/*.cpp: dependency cycles, layer violations, dead exports.

Heuristic (regex, not a parser): a symbol is "defined" by the src file holding its column-0
non-static definition; a file "uses" it if the identifier appears anywhere in its text.
Edit LAYERS to match the target architecture in docs/CPP-CONVERSION-PLAN.md.
    tools/analyze_coupling.py [--check]    (--check exits 1 on any upward dependency)
"""
import collections, glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYERS = [
    "system ioman util tar ps2cnf ioprp atlas httpclient nbns launchdiag elfldr_noreset debug httpcatalog hdd md5 lz4 zso",
    "config lang xparam cheatman gsm pad sound OSDHistory vmc_groups settings",
    "renderman textures texcache fntsys themes uidraw",
    "supportbase bdmsupport hddsupport ethsupport appsupport mmcesupport udpfssupport httpsupport favsupport vcdsupport cuesupport discsupport netsupport artindex folderbrowse rabadge ranet rawatch rahash retrogem",
    "menusys dia dialogs gui guigame libview",
    "opl host",
]
layer = {n: i for i, l in enumerate(LAYERS) for n in l.split()}

files = {os.path.basename(f)[:-4]: open(f, errors="replace").read() for f in glob.glob(f"{ROOT}/src/*.cpp")}
pat = re.compile(r"^(?!static|typedef|#|//|\s)([A-Za-z_][\w \*\t]*?[\s\*])([A-Za-z_]\w*)\s*\(([^;{]*?)\)\s*\n?\{", re.M)
defs = {}
for n, t in files.items():
    for m in pat.finditer(t):
        if m.group(2) not in ("if", "while", "for", "switch", "main"):
            defs.setdefault(m.group(2), n)
def code_only(text):
    """Drop comments and string/char literals so words in prose do not count as references."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)
    return text

# Identifiers that are not member names (obj.name, p->name, ns::name): those are not references to a free function.
IDENT = re.compile(r"(?<![\w.>:])[A-Za-z_]\w*\b")
tok = {n: collections.Counter(IDENT.findall(code_only(t))) for n, t in files.items()}

edges = collections.defaultdict(collections.Counter)
up = collections.defaultdict(list)
for sym, owner in defs.items():
    for n, c in tok.items():
        if n != owner and c[sym]:
            edges[n][owner] += 1
            if n in layer and owner in layer and layer[n] < layer[owner]:
                up[(n, owner)].append(sym)

# strongly connected components (Tarjan)
sys.setrecursionlimit(10000)
g = {n: set(edges[n]) for n in files}
idx, low, st, on, sccs = {}, {}, [], set(), []
def sc(v):
    idx[v] = low[v] = len(idx); st.append(v); on.add(v)
    for w in g[v]:
        if w not in idx: sc(w); low[v] = min(low[v], low[w])
        elif w in on: low[v] = min(low[v], idx[w])
    if low[v] == idx[v]:
        comp = []
        while True:
            w = st.pop(); on.discard(w); comp.append(w)
            if w == v: break
        sccs.append(comp)
for v in files:
    if v not in idx: sc(v)
big = max(sccs, key=len)
print(f"modules: {len(files)}; largest dependency cycle: {len(big)} modules")
print("  " + " ".join(sorted(big)))

unlayered = sorted(set(files) - set(layer))
if unlayered: print("unlayered:", " ".join(unlayered))
tot = sum(len(v) for v in up.values())
print(f"\nupward dependencies: {tot} symbols across {len(up)} module pairs")
for (a, b), s in sorted(up.items(), key=lambda x: -len(x[1])):
    print(f"  {a}(L{layer[a]}) -> {b}(L{layer[b]}): {len(s)}  {', '.join(sorted(s)[:6])}")

dead = {n: sorted(d for d in {s for s, o in defs.items() if o == n} if not any(tok[m][d] for m in files if m != n)) for n in files}
print(f"\nnon-static functions no other src file mentions: {sum(len(v) for v in dead.values())}")
for n, l in sorted(dead.items(), key=lambda x: -len(x[1])):
    if l: print(f"  {n} ({len(l)}): {', '.join(l)}")
if "--check" in sys.argv and tot:
    sys.exit(1)
