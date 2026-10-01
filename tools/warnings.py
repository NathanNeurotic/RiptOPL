#!/usr/bin/env python3
"""Warning baseline for the ps2build main flavour.

    ps2build build 2>&1 | tee build.log       (clean build: warnings only print when compiling)
    tools/warnings.py build.log               summary by warning flag and by file
    tools/warnings.py build.log --check       fail if any (file, flag) count exceeds the baseline
    tools/warnings.py build.log --update      rewrite tools/warnings_baseline.json

Only warnings in this repository's own code are counted (src/, ee_core/, elfldr/, modules/, include/);
SDK headers and the downloaded lwNBD tree are ignored. Counts are per (file, flag), so refactors that
move code between files show up, but unrelated edits do not.
"""
import collections, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, "tools", "warnings_baseline.json")
OWN = ("src/", "ee_core/", "elfldr/", "modules/", "include/")
SKIP = ("modules/network/lwNBD/",)

rx = re.compile(r"^(/[^:\s]+):\d+:\d+: warning: .*\[(-W[\w+-]+)\]\s*$")
rx_plain = re.compile(r"^(/[^:\s]+):\d+:\d+: warning: (.*)$")

def parse(path):
    counts = collections.Counter()
    seen = set()
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = rx.match(line)
        flag = None
        if m: f, flag = m.groups()
        else:
            m = rx_plain.match(line)
            if not m: continue
            f, msg = m.groups(); flag = "(" + re.sub(r"[‘’'].*", "", msg).strip()[:40] + ")"
        rel = os.path.relpath(f, ROOT)
        if not rel.startswith(OWN) or rel.startswith(SKIP): continue
        # the same header warning repeats once per including TU: count it once per (file, line text)
        key = (rel, flag, line)
        if key in seen: continue
        seen.add(key)
        counts[f"{rel}|{flag}"] += 1
    return counts

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args: sys.exit(__doc__)
    counts = parse(args[0])
    byflag = collections.Counter(); byfile = collections.Counter()
    for k, n in counts.items():
        f, flag = k.split("|"); byflag[flag] += n; byfile[f] += n
    if "--update" in sys.argv:
        json.dump(dict(sorted(counts.items())), open(BASELINE, "w"), indent=0, sort_keys=True); open(BASELINE, "a").write("\n")
        print(f"baseline written: {sum(counts.values())} warnings in {len(byfile)} files")
        return
    if "--check" in sys.argv:
        base = json.load(open(BASELINE))
        worse = {k: (base.get(k, 0), n) for k, n in counts.items() if n > base.get(k, 0)}
        better = sum(base.get(k, 0) - counts.get(k, 0) for k in base if counts.get(k, 0) < base[k])
        for k, (b, n) in sorted(worse.items()): print(f"NEW  {k}: {b} -> {n}")
        print(f"{sum(counts.values())} warnings (baseline {sum(base.values())}); {len(worse)} regressions, {better} fixed")
        sys.exit(1 if worse else 0)
    print(f"{sum(counts.values())} warnings in {len(byfile)} files")
    for flag, n in byflag.most_common(): print(f"  {n:4d} {flag}")
    print("top files:")
    for f, n in byfile.most_common(12): print(f"  {n:4d} {f}")

main()
