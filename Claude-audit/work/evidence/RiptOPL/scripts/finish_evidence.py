#!/usr/bin/env python3
"""Evidence-only post-processing of the finished Korium/GCC build (read-only on the build tree).

usage: finish_evidence.py <build_dir> <dist_dir> <raw_out_dir> <sdk_root>
Writes: artifacts.tsv, riptopl-top80-symbols.txt, riptopl-stack-heap.txt, riptopl-embedded-blobs.tsv,
irx-modules.tsv, compile-link-flags.txt, extra-warnings-summary.txt
"""
import collections
import json
import os
import re
import shlex
import subprocess
import sys

import yaml

bdir, ddir, out, sdk = [a.replace("\\", "/").rstrip("/") for a in sys.argv[1:5]]
EEBIN = sdk + "/toolchain/ee/bin/mips64r5900el-ps2-elf-"
W = lambda n: open(os.path.join(out, n), "w", encoding="utf-8", newline="\n")

# ---- 1. artifacts
with W("artifacts.tsv") as f:
    f.write("# every file in <build>/bin and <dist>/editions/korium, bytes (os.stat)\npath\tbytes\n")
    for root, base in ((bdir + "/bin", "build/editions/korium-gcc/bin"), (ddir, "dist/editions/korium")):
        for n in sorted(os.listdir(root), key=str.lower):
            f.write(f"{base}/{n}\t{os.path.getsize(root + '/' + n)}\n")
    u, p = os.path.getsize(bdir + "/bin/riptopl.elf"), os.path.getsize(bdir + "/bin/riptopl-packed.elf")
    f.write(f"# riptopl.elf (stripped, unpacked) {u} -> riptopl-packed.elf {p}: packed/unpacked = {p / u:.4f}\n")

# ---- 2. top 80 symbols (nm -S on the unstripped ELF, demangled)
unstripped = bdir + "/obj/riptopl/riptopl.unstripped.elf"
nm = subprocess.run([EEBIN + "nm.exe", "-S", "-C", "--size-sort", "-r", unstripped], capture_output=True, text=True).stdout
syms = []
for line in nm.splitlines():
    parts = line.split(None, 3)
    if len(parts) == 4:
        addr, size, typ, name = parts
        sz = int(size, 16)
        if sz < 0x80000000:
            syms.append((sz, typ, name, addr))
syms.sort(key=lambda s: -s[0])
with W("riptopl-top80-symbols.txt") as f:
    f.write("# mips64r5900el-ps2-elf-nm -S -C --size-sort -r obj/riptopl/riptopl.unstripped.elf | top 80 by size\n")
    f.write("# type: T/t=.text  D/d=.data  R/r=.rodata  B/b=.bss (upper = global, lower = local)\n")
    f.write("bytes\ttype\taddr\tsymbol\n")
    for sz, typ, name, addr in syms[:80]:
        f.write(f"{sz}\t{typ}\t{addr}\t{name}\n")
    tot = collections.Counter()
    for sz, typ, _, _ in syms:
        tot[typ.upper()] += sz
    f.write("\n# sum of nm sizes by type (all symbols): " + ", ".join(f"{k}={v}" for k, v in sorted(tot.items())) + "\n")

# ---- 7. stack / heap
want = {"_stack_size", "_heap_size", "_stack", "_end", "_end_bss", "_fbss", "_gp", "end"}
nm_all = subprocess.run([EEBIN + "nm.exe", unstripped], capture_output=True, text=True).stdout
with W("riptopl-stack-heap.txt") as f:
    f.write("# nm obj/riptopl/riptopl.unstripped.elf (linker-provided symbols)\n")
    for line in nm_all.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] in want:
            f.write(line + "\n")
    eec = subprocess.run([EEBIN + "nm.exe", bdir + "/obj/eecore/eecore.unstripped.elf"], capture_output=True, text=True).stdout
    f.write("\n# nm obj/eecore/eecore.unstripped.elf\n")
    for line in eec.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] in want:
            f.write(line + "\n")
    f.write("\n# grep in first-party sources for _stack_size/_heap_size overrides: see BUILD-EVIDENCE.md section 7\n")

# ---- 6. IRX modules + 2. embedded blobs
res = yaml.safe_load(open(ddir + "/ps2.resolved.yaml", encoding="utf-8"))
targets = {t["name"]: t for t in res.get("targets", [])}
main = targets["riptopl"]
emb = {}
for e in main.get("embed_irx", []):
    tgt, _, sym = str(e).partition(":")
    emb[tgt] = sym or tgt
per = {}
for line in open(out + "/riptopl-per-object.tsv", encoding="utf-8"):
    p = line.rstrip("\n").split("\t")
    if len(p) >= 5 and p[1].isdigit():
        per[p[0]] = int(p[1]) + int(p[2]) + int(p[3])
loose = set(os.listdir(ddir))
with W("irx-modules.tsv") as f:
    f.write("# every IRX/ELF in build/editions/korium-gcc/bin; embedded = named in riptopl embed_irx (ps2.resolved.yaml)\n")
    f.write("# blob_bytes = bytes of obj/_embed/<symbol>_irx.o in opl.map (riptopl-per-object.tsv); loose = copied into dist/editions/korium\n")
    f.write("file\tbytes\ttarget\tembedded_as\tblob_bytes_in_elf\tloose_in_dist\n")
    for n in sorted(os.listdir(bdir + "/bin"), key=str.lower):
        if not n.endswith((".irx", ".elf")) or n.startswith("riptopl"):
            continue
        tgt = n.rsplit(".", 1)[0]
        sym = emb.get(tgt, "")
        bb = per.get(f"obj/_embed/{sym}_irx.o", "") if sym else ""
        lz = "yes" if any(l.lower().startswith(tgt.replace("_fork", "").lower()) and l.lower().endswith(".irx") for l in loose) else ""
        f.write(f"{n}\t{os.path.getsize(bdir + '/bin/' + n)}\t{tgt}\t{sym}\t{bb}\t{lz}\n")
    f.write("\n# embed_irx entries with no bin/ file from this repo (prebuilt SDK package IRX):\n")
    built = {n.rsplit('.', 1)[0] for n in os.listdir(bdir + "/bin")}
    for tgt, sym in emb.items():
        if tgt not in built:
            f.write(f"{tgt}\t{sym}\t{per.get(f'obj/_embed/{sym}_irx.o', '')}\n")
with W("riptopl-embedded-blobs.tsv") as f:
    f.write("# embedded blobs in riptopl.elf (all land in .data), bytes from opl.map via riptopl-per-object.tsv\n")
    f.write("kind\tobject\tbytes\n")
    rows = [(("irx/elf" if "/_embed/" in k else "asset"), k, v) for k, v in per.items() if "/_embed/" in k or "/asm/" in k]
    rows.sort(key=lambda r: (r[0], -r[2]))
    sums = collections.Counter()
    for kind, k, v in rows:
        f.write(f"{kind}\t{k}\t{v}\n")
        sums[kind] += v
    f.write("# totals: " + ", ".join(f"{k} {v} bytes in {sum(1 for r in rows if r[0] == k)} objects" for k, v in sums.items()) + "\n")

# ---- 3. compile / link flags
cc = json.load(open(bdir + "/compile_commands.json", encoding="utf-8"))
bytgt = collections.defaultdict(lambda: collections.Counter())
for e in cc:
    o = e["output"].replace("\\", "/")
    m = re.search(r"obj/([^/]+)/", o)
    t = m.group(1) if m else "?"
    a = e["arguments"]
    c = bytgt[t]
    c["tus"] += 1
    c["ffunction-sections"] += "-ffunction-sections" in a
    c["fdata-sections"] += "-fdata-sections" in a
    for x in a:
        if re.fullmatch(r"-O[0-3sz]", x):
            c["opt" + x] += 1
            break
ninja = open(bdir + "/build.ninja", encoding="utf-8").read().replace("$\n", " ")
links = {}
for m in re.finditer(r"^build (\S+?\.notiopmod\.elf|\S+?\.unstripped\.elf): (\w+) .*?\n((?:  .*\n)*)", ninja, re.M):
    var = dict(re.findall(r"^  (\w+) = (.*)$", m.group(3), re.M))
    links[m.group(1)] = var.get("flags", "").replace("$ ", " ")
with W("compile-link-flags.txt") as f:
    f.write("# from compile_commands.json and build.ninja of build/editions/korium-gcc\n")
    f.write("## per target: TUs, TUs with -ffunction-sections, with -fdata-sections, optimisation level counts\n")
    for t in sorted(bytgt):
        c = bytgt[t]
        opts = " ".join(f"{k[3:]}:{v}" for k, v in sorted(c.items()) if k.startswith("opt"))
        f.write(f"{t:32s} tus={c['tus']:4d} ffs={c['ffunction-sections']:4d} fds={c['fdata-sections']:4d} {opts}\n")
    f.write("\n## link edges: --gc-sections present? (all links in build.ninja)\n")
    gc = sum("--gc-sections" in v for v in links.values())
    f.write(f"# {gc} of {len(links)} links pass -Wl,--gc-sections\n")
    for k in sorted(links):
        v = links[k]
        f.write(f"{'gc ' if '--gc-sections' in v else '-- '} {k}\n")
    for k in ("obj/riptopl/riptopl.unstripped.elf", "obj/eecore/eecore.unstripped.elf"):
        f.write(f"\n## flags of {k}\n{links.get(k, '?')}\n")

# ---- 4. extra warnings summary
rows = []
for line in open(out + "/extra-warnings.tsv", encoding="utf-8"):
    if line.startswith(("#", "flag\t")):
        continue
    p = line.rstrip("\n").split("\t")
    if len(p) >= 6:
        rows.append(p)
with W("extra-warnings-summary.txt") as f:
    f.write(f"# grouping of extra-warnings.tsv ({len(rows)} unique warnings); NOT the shipped flag set\n")
    f.write("## by flag (unique warnings)\n")
    for k, v in collections.Counter(r[0] for r in rows).most_common():
        f.write(f"{v:6d}  {k}\n")
    def area(p):
        if p.startswith("<sdk>"):
            return "<sdk>"
        s = p.split("/")
        return "/".join(s[:2]) if s[0] in ("src", "modules") else s[0]
    f.write("\n## by flag and area (unique warnings)\n")
    for (fl, ar), v in sorted(collections.Counter((r[0], area(r[1])) for r in rows).items(), key=lambda x: (-x[1], x[0])):
        f.write(f"{v:6d}  {fl:36s} {ar}\n")
    un = [r for r in rows if r[0] in ("-Wunused-function", "-Wunused-variable", "-Wunused-const-variable", "-Wunused-but-set-variable", "-Wunused-macros", "-Wunused-parameter")]
    f.write("\n## unused-* warnings in first-party src/ (all translation units warn = unused in every variant)\n")
    for r in un:
        if r[1].startswith("src/") and r[0] != "-Wunused-parameter" and r[0] != "-Wunused-macros":
            f.write("\t".join(r) + "\n")
print("done")
