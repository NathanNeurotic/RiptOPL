#!/usr/bin/env python3
"""Group compiler warnings of a build log by flag. usage: warnings.py <log> <repo_root> <out.txt>"""
import collections
import re
import sys

log, repo_root, out = sys.argv[1], sys.argv[2].replace("\\", "/").rstrip("/"), sys.argv[3]
raw = open(log, "rb").read()
text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8", "replace")
pat = re.compile(r"^(.*?):(\d+):(?:(\d+):)?\s+warning:\s+(.*?)(?:\s+\[(-W[^\]]+)\])?\s*$")


def norm(p):
    p = p.replace("\\", "/")
    i = p.lower().find(repo_root.lower())
    if i >= 0:
        p = p[i + len(repo_root):].lstrip("/")
    m = re.search(r"(?i)/ps2build/(.*)$", p)
    if m:
        p = "<sdk>/" + m.group(1)
    while p.startswith("../"):
        p = p[3:]
    return p


seen, rows, total = set(), [], 0
for line in text.splitlines():
    m = pat.match(line.strip())
    if not m:
        continue
    total += 1
    key = (norm(m.group(1)), int(m.group(2)), m.group(4), m.group(5) or "(no flag)")
    if key in seen:
        continue
    seen.add(key)
    rows.append(key)
by_flag = collections.Counter(r[3] for r in rows)
with open(out, "w", encoding="utf-8") as f:
    f.write(f"# Compiler warnings in {log.replace(chr(92), '/').split('/')[-1]}\n")
    f.write(f"# warning lines: {total}   unique (file,line,message,flag): {len(rows)}\n")
    f.write("# The same header warning repeated for several translation units counts once in 'unique'.\n\n")
    f.write("## by flag (unique)\n")
    for flag, n in by_flag.most_common():
        f.write(f"{n:6d}  {flag}\n")
    f.write("\n## by flag and top-level area (unique)\n")
    area = collections.Counter()
    for r in rows:
        parts = r[0].split("/")
        a = "/".join(parts[:2]) if parts[0] in ("modules", "third_party", "src", "<sdk>") else parts[0]
        area[(r[3], a)] += 1
    for (flag, a), n in sorted(area.items(), key=lambda kv: (kv[0][0], -kv[1])):
        f.write(f"{n:6d}  {flag:40s} {a}\n")
    f.write("\n## every unique warning (file:line: message [flag])\n")
    for r in sorted(rows, key=lambda r: (r[3], r[0], r[1])):
        f.write(f"{r[0]}:{r[1]}: {r[2]} [{r[3]}]\n")
print("warnings", total, "unique", len(rows))
