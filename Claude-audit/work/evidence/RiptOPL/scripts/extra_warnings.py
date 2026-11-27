#!/usr/bin/env python3
"""Evidence-only warning pass: re-run every first-party compile command of compile_commands.json with
a wider warning set, compiling to assembly on the null device (-S -o NUL), so warnings that need the
whole translation unit (-Wunused-function, -Wunused-variable for statics) are produced too. Nothing is
written into the build tree (no object output, no -MF).

usage: extra_warnings.py <build_dir> <repo_root> <out_log> <out_tsv> [jobs]

Skipped: generated sources (bin2c blobs, IRX import/export stubs), third_party/ and modules/network/lwNBD.
-Wno-unused* flags of the original command are dropped so unused code shows; everything else is unchanged.
A source compiled for several targets (for example cdvdman for 13 devices) is checked once per target; the
TSV says in how many of those translation units each warning appeared ("tus_warn/tus_total"): an "unused"
warning that appears in fewer than all of them is used in some variant.
"""
import collections
import concurrent.futures
import json
import re
import subprocess
import sys

build_dir, repo_root, out_log, out_tsv = [a.replace("\\", "/").rstrip("/") for a in sys.argv[1:5]]
jobs = int(sys.argv[5]) if len(sys.argv) > 5 else 12
EXTRA_C = ["-Wall", "-Wextra", "-Wunused", "-Wunused-macros", "-Wunused-const-variable", "-Wredundant-decls", "-Wmissing-declarations",
           "-Wimplicit-fallthrough", "-Wduplicated-cond", "-Wduplicated-branches", "-Wlogical-op",
           "-Wno-sign-compare", "-Wno-missing-field-initializers", "-Wno-pedantic"]
cmds = []
for e in json.load(open(build_dir + "/compile_commands.json", encoding="utf-8")):
    src = e["file"].replace("\\", "/")
    if src.lower().startswith(build_dir.lower()) or not src.lower().startswith(repo_root.lower()):
        continue
    rel = src[len(repo_root):].lstrip("/")
    if rel.startswith(("asm/", "third_party/", "modules/network/lwNBD/")) or rel.endswith((".S", ".s")):
        continue
    args, out, skip = [], [], 0
    for a in e["arguments"]:
        if skip:
            skip -= 1
            continue
        if a in ("-o", "-MF"):
            skip = 1
            continue
        if a in ("-c", "-MMD", "-MD", "-w") or a.startswith("-Wno-unused"):
            continue
        args.append(a)
    extra = list(EXTRA_C)
    if rel.endswith(".c"):
        extra += ["-Wmissing-prototypes", "-Wstrict-prototypes", "-Wold-style-definition", "-Wimplicit-function-declaration"]
    cmds.append((rel, e["output"], args + ["-S", "-o", "NUL"] + extra))


def run(item):
    rel, out, args = item
    r = subprocess.run(args, cwd=build_dir, capture_output=True, text=True, errors="replace")
    return rel, out, r.returncode, r.stderr


pat = re.compile(r"^(.*?):(\d+):(?:(\d+):)?\s+(warning|error):\s+(.*?)(?:\s+\[(-W[^\]]+)\])?\s*$")


def norm(p):
    p = p.replace("\\", "/")
    i = p.lower().find(repo_root.lower())
    if i >= 0:
        return p[i + len(repo_root):].lstrip("/")
    m = re.search(r"(?i)/ps2build/(.*)$", p)
    return "<sdk>/" + m.group(1) if m else p


tus = collections.Counter(c[0] for c in cmds)
hits = collections.defaultdict(set)
failed = []
with open(out_log, "w", encoding="utf-8") as log, concurrent.futures.ThreadPoolExecutor(jobs) as pool:
    for rel, out, code, err in pool.map(run, cmds):
        log.write(f"### {rel} -> {out} (exit {code})\n{err}\n")
        if code:
            failed.append((rel, out))
        for line in err.splitlines():
            m = pat.match(line.strip())
            if m:
                hits[(m.group(6) or "(no flag)" if m.group(4) == "warning" else "ERROR", norm(m.group(1)),
                      int(m.group(2)), m.group(5))].add(out)
with open(out_tsv, "w", encoding="utf-8") as f:
    f.write("# Evidence-only pass (scripts/extra_warnings.py): -S -o NUL -Wall -Wextra -Wunused -Wunused-macros ...\n")
    f.write(f"# translation units checked: {len(cmds)}   unique sources: {len(tus)}   failed: {len(failed)}\n")
    f.write("# tus_total is blank for a warning located in a header (it can come from any includer).\n")
    f.write("flag\tfile\tline\tmessage\ttus_warn\ttus_total\n")
    for (flag, file, line, msg), outs in sorted(hits.items()):
        f.write("\t".join([flag, file, str(line), msg, str(len(outs)), str(tus[file]) if file in tus else ""]) + "\n")
    by = collections.Counter(k[0] for k in hits)
    f.write("\n# ---- unique warnings by flag ----\n")
    for flag, n in by.most_common():
        f.write(f"# {n:6d}  {flag}\n")
    if failed:
        f.write("\n# ---- translation units whose warning pass failed ----\n")
        for rel, out in failed:
            f.write(f"# {rel} -> {out}\n")
print("tus", len(cmds), "unique warnings", len(hits), "failed", len(failed))
