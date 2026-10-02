#!/usr/bin/env python3
"""Module-coupling report for src/*.cpp: dependency cycles, layer violations, dead exports.

Heuristic (regex, not a parser): a symbol is "defined" by the src file holding its column-0
non-static definition; a file "uses" it if the identifier appears anywhere in its text.
Edit LAYERS to match the target architecture in docs/CPP-CONVERSION-PLAN.md.
    tools/analyze_coupling.py [--check|--update]
        --check exits 1 on any upward dependency beyond tools/coupling_baseline.json (a ratchet: new edges fail,
        existing ones may only shrink); --update rewrites the baseline after a cleanup.
"""
import collections, glob, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The layer of a module is its directory under src/, lowest first. A module may depend on its own layer and
# below; a dependency on a higher layer is an "upward" edge.
LAYERS = ["platform", "core", "gfx", "sources", "ui", "app"]
BASELINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "coupling_baseline.json")
paths = sorted(glob.glob(f"{ROOT}/src/*/*.cpp"))
layer = {os.path.basename(p)[:-4]: LAYERS.index(os.path.basename(os.path.dirname(p))) for p in paths}

files = {os.path.basename(f)[:-4]: open(f, errors="replace").read() for f in paths}
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
current = {f"{a}->{b}": len(sy) for (a, b), sy in up.items()}
if "--update" in sys.argv:
    json.dump(dict(sorted(current.items())), open(BASELINE, "w"), indent=0, sort_keys=True)
    print(f"\nwrote {BASELINE}")
if "--check" in sys.argv:
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    bad = {k: (v, base.get(k, 0)) for k, v in current.items() if v > base.get(k, 0)}
    fixed = {k for k in base if current.get(k, 0) < base[k]}
    for k, (v, was) in sorted(bad.items()):
        print(f"NEW upward dependency {k}: {v} symbol(s), baseline {was}")
    if fixed:
        print(f"{len(fixed)} upward pair(s) shrank: run --update to lock in the improvement")
    if bad:
        sys.exit(1)
