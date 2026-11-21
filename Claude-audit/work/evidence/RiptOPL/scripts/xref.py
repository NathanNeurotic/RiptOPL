#!/usr/bin/env python3
"""Cross-reference candidates: global symbols defined in one object and referenced by no other object
of the same link.

usage: xref.py <build_dir> <repo_root> <sdk_dir> <outdir> <prefix>

For every link edge in build.ninja (EE ELF or IOP IRX) the first-party objects are the .o files named on
the link line plus the members of static libraries built by the same ninja graph. One nm call and one
objdump -r call per toolchain cover all objects (response file, so few process starts).

Columns added to make the list more useful:
  own_obj_reloc  yes = a relocation in the defining object (outside .pdr/.debug/.eh_frame and outside the
                 symbol's own -ffunction-sections section) names the symbol, so it is used inside its own
                 file and is only a "could be static" candidate; no = nothing in any first-party object
                 refers to it by name.
  ee_elf         for EE links: kept = still in the linked ELF after --gc-sections; gc = removed.
                 n/a for IRX links (ld -r, no --gc-sections: everything compiled is shipped).

CANDIDATES ONLY. Not visible to this analysis: references from SDK libraries/crt0 (main, _start, newlib
and ps2sdk hooks, C++ operator new/delete), symbols reached through assembler macros or computed
addresses, IRX entry points named by the linker script, and other editions/configurations of the build
(RA, debug, lite, pademu0 ...). An "own_obj_reloc=no, ee_elf=kept" row is such an outside reference.
"""
import collections
import json
import os
import re
import subprocess
import sys
import tempfile

build_dir, repo_root, sdk, outdir, prefix = [a.replace("\\", "/").rstrip("/") for a in sys.argv[1:6]]
ninja = open(build_dir + "/build.ninja", encoding="utf-8", errors="replace").read()
ninja = ninja.replace("$\n", "")


def unesc(tok):
    return tok.replace("$:", ":").replace("$ ", " ")


def split(s):
    out, cur, i = [], "", 0
    while i < len(s):
        c = s[i]
        if c == "$" and i + 1 < len(s):
            cur += s[i:i + 2]
            i += 2
            continue
        if c == " ":
            if cur:
                out.append(unesc(cur))
            cur = ""
        else:
            cur += c
        i += 1
    if cur:
        out.append(unesc(cur))
    return out


links, archives = {}, {}
lines = ninja.split("\n")
for idx, line in enumerate(lines):
    m = re.match(r"^build (\S+): (\w+) (.*)$", line)
    if not m:
        continue
    out, rule, rest = unesc(m.group(1)), m.group(2), m.group(3)
    explicit = rest.split(" | ")[0].split(" || ")[0]
    ins = split(explicit)
    if rule in ("ee_ar", "iop_ar"):
        archives[out] = ins
    elif rule in ("ee_link", "ee_link_cxx", "ee_link_cxx_rsp", "iop_link"):
        libs = ""
        for follow in lines[idx + 1:idx + 6]:
            if follow.startswith("  libs = "):
                libs = follow[9:]
            if follow.startswith("build "):
                break
        links[out] = {"rule": rule, "objs": [i for i in ins if i.endswith(".o")],
                      "libs": [unesc(t)[2:] for t in split(libs) if unesc(t).startswith("-l")]}

lib_by_name = {}
for arc, objs in archives.items():
    name = os.path.basename(arc)
    if name.startswith("lib") and name.endswith(".a"):
        lib_by_name[name[3:-2]] = (arc, objs)
for link in links.values():
    link["libobjs"] = []
    for lib in link["libs"]:
        if lib in lib_by_name:
            link["libobjs"] += lib_by_name[lib][1]

src_of = {}
for e in json.load(open(build_dir + "/compile_commands.json", encoding="utf-8")):
    out = e["output"].replace("\\", "/")
    f = e["file"].replace("\\", "/")
    if f.lower().startswith(build_dir.lower()):
        f = "<build>" + f[len(build_dir):]
    elif f.lower().startswith(repo_root.lower()):
        f = f[len(repo_root):].lstrip("/")
    src_of[out] = f


def is_iop(link):
    return link["rule"] == "iop_link"


def tool_on(tool, args, objs):
    with tempfile.NamedTemporaryFile("w", suffix=".rsp", delete=False) as rsp:
        for o in objs:
            rsp.write('"' + (build_dir + "/" + o) + '"\n')
    try:
        r = subprocess.run([tool, *args, "@" + rsp.name], capture_output=True, text=True, errors="replace")
    finally:
        os.unlink(rsp.name)
    return r.stdout


def rel(path):
    path = path.replace("\\", "/")
    return path[len(build_dir) + 1:] if path.lower().startswith(build_dir.lower()) else path


EE = sdk + "/toolchain/ee/bin/mips64r5900el-ps2-elf-"
IOP = sdk + "/toolchain/iop/bin/mipsel-none-elf-"
ee_objs, iop_objs = set(), set()
for link in links.values():
    (iop_objs if is_iop(link) else ee_objs).update(link["objs"] + link["libobjs"])
missing = [o for o in ee_objs | iop_objs if not os.path.isfile(build_dir + "/" + o)]
ee_objs -= set(missing)
iop_objs -= set(missing)

defined = collections.defaultdict(dict)   # obj -> sym -> (type, size)
undefined = collections.defaultdict(set)  # obj -> syms
relocs = collections.defaultdict(lambda: collections.defaultdict(set))  # obj -> sym -> {sections}
pat = re.compile(r"^(.*\.o):\s*([0-9a-fA-F]+)?\s*([0-9a-fA-F]+)?\s*([A-Za-z?])\s+(\S+)$")
SKIP_SEC = (".pdr", ".debug", ".eh_frame", ".mdebug", ".gnu", ".comment")
for prefix_tool, objs in ((EE, sorted(ee_objs)), (IOP, sorted(iop_objs))):
    if not objs:
        continue
    for flag in (["-A", "-S", "-g", "--defined-only"], ["-A", "-u"]):
        for line in tool_on(prefix_tool + "nm.exe", flag, objs).splitlines():
            m = pat.match(line.strip())
            if not m:
                continue
            obj, typ, sym = rel(m.group(1)), m.group(4), m.group(5)
            if typ == "U":
                undefined[obj].add(sym)
            elif "-g" in flag:
                defined[obj][sym] = (typ, int(m.group(3), 16) if m.group(3) else 0)
    cur_obj = cur_sec = None
    for line in tool_on(prefix_tool + "objdump.exe", ["-r"], objs).splitlines():
        m = re.match(r"^(.*\.o):\s+file format", line)
        if m:
            cur_obj, cur_sec = rel(m.group(1)), None
            continue
        m = re.match(r"^RELOCATION RECORDS FOR \[(.*)\]:", line)
        if m:
            cur_sec = m.group(1)
            continue
        if cur_obj is None or cur_sec is None or cur_sec.startswith(SKIP_SEC):
            continue
        m = re.match(r"^[0-9a-fA-F]+\s+R_\S+\s+(\S+)$", line.strip())
        if m:
            sym = re.sub(r"[+-]0x[0-9a-fA-F]+$", "", m.group(1))
            relocs[cur_obj][sym].add(cur_sec)

final_syms = {}
for out in links:
    if not is_iop(links[out]) and os.path.isfile(build_dir + "/" + out):
        r = subprocess.run([EE + "nm.exe", build_dir + "/" + out], capture_output=True, text=True, errors="replace")
        final_syms[out] = {l.split()[-1] for l in r.stdout.splitlines() if l.strip()}

LIB_HOOKS = {"main", "_start", "__start", "_init", "_fini", "_retonly", "_exp_table", "_irx_id", "_gp"}
agg = collections.defaultdict(lambda: {"links": 0, "unref": 0, "type": "", "size": 0, "kept": 0, "ee": 0, "own": 0})
for out, link in sorted(links.items()):
    objs = [o for o in link["objs"] + link["libobjs"] if o not in missing]
    refs = collections.Counter()
    for o in objs:
        for s in undefined[o]:
            refs[s] += 1
    for o in objs:
        src = src_of.get(o, "<obj>/" + o)
        for sym, (typ, size) in defined[o].items():
            if typ in "WVwvU":
                continue
            a = agg[(src, sym)]
            a["links"] += 1
            a["type"], a["size"] = typ, max(a["size"], size)
            if not is_iop(link):
                a["ee"] += 1
                a["kept"] += 1 if sym in final_syms.get(out, ()) else 0
            own = {s for s in relocs[o].get(sym, ()) if s not in (".text." + sym, ".rodata." + sym, ".data." + sym)}
            a["own"] += 1 if own else 0
            if refs[sym] == 0:
                a["unref"] += 1


def demangle(names):
    try:
        r = subprocess.run([EE + "c++filt.exe"], input="\n".join(names) + "\n", capture_output=True, text=True,
                           errors="replace")
        out = r.stdout.split("\n")
        if len(out) >= len(names):
            return dict(zip(names, out))
    except OSError:
        pass
    return {n: n for n in names}


dm = demangle(sorted({k[1] for k in agg}))


def origin(src):
    if src.startswith("<build>/generated/") or src.startswith("asm/") or src.startswith("<obj>/"):
        return "generated"
    if src.startswith("third_party/") or src.startswith("modules/network/lwNBD"):
        return "vendored"
    return "first-party"


def state(a):
    return "n/a" if not a["ee"] else ("kept" if a["kept"] else "gc")


def verdict(a):
    own = a["own"] > 0
    st = state(a)
    if own:
        return "used only in its own file (could be static)"
    if st == "gc":
        return "unreferenced, removed by --gc-sections (source-only dead code candidate)"
    if st == "kept":
        return "kept by an outside reference (SDK/crt0/linker script) or address taken"
    return "unreferenced by name in its IRX link: SHIPPED dead-code candidate (or entry/table use)"


os.makedirs(outdir, exist_ok=True)
cand = [(k, a) for k, a in agg.items() if a["unref"] == a["links"] and k[1] not in LIB_HOOKS]
with open(os.path.join(outdir, prefix + "-xref-candidates.tsv"), "w", encoding="utf-8") as f:
    f.write("# CANDIDATE LIST ONLY (limits: header of scripts/xref.py). A row = a global, non-weak symbol that no OTHER\n")
    f.write("# object of the same link references, in EVERY link of this build (Korium standard, GCC) that compiles it.\n")
    f.write("# own_obj_reloc: yes = named by a relocation inside its own object (used there; could be static);\n")
    f.write("#   no = no first-party object refers to it by name.\n")
    f.write("# ee_elf: kept/gc = present in / removed from the linked EE ELF; n/a = IRX link (no --gc-sections there,\n")
    f.write("#   so every compiled IRX function is shipped).\n")
    f.write("origin\tsource\tsymbol\tdemangled\ttype\tsize\tlinks_defined\town_obj_reloc\tee_elf\tverdict\n")
    for (src, sym), a in sorted(cand, key=lambda kv: (origin(kv[0][0]), kv[0][0], -kv[1]["size"], kv[0][1])):
        f.write("\t".join([origin(src), src, sym, dm.get(sym, sym), a["type"], str(a["size"]), str(a["links"]),
                           "yes" if a["own"] else "no", state(a), verdict(a)]) + "\n")

with open(os.path.join(outdir, prefix + "-xref-summary.txt"), "w", encoding="utf-8") as f:
    f.write("# Cross-reference candidate summary (CANDIDATES ONLY, see the .tsv header and scripts/xref.py)\n")
    f.write(f"links analysed: {len(links)}   objects: EE {len(ee_objs)}, IOP {len(iop_objs)}   missing objects: {len(missing)}\n")
    f.write(f"global non-weak definitions (source,symbol pairs): {len(agg)}\n")
    f.write(f"candidates (unreferenced by other objects in every link): {len(cand)}\n\n")
    c, b = collections.Counter(), collections.Counter()
    for (src, sym), a in cand:
        key = (origin(src), "func" if a["type"] in "Tt" else "data", "own=yes" if a["own"] else "own=no", state(a))
        c[key] += 1
        b[key] += a["size"]
    f.write("origin        kind  own_obj_reloc ee_elf  count     bytes(nm size)\n")
    for key in sorted(c):
        f.write(f"{key[0]:13s} {key[1]:5s} {key[2]:13s} {key[3]:6s} {c[key]:6d} {b[key]:10d}\n")
    for title, test in (
            ("IRX/helper symbols with no reference by name anywhere (shipped dead-code candidates)",
             lambda src, a: origin(src) == "first-party" and not a["own"] and state(a) == "n/a"),
            ("EE symbols removed by --gc-sections and unreferenced (source-only dead-code candidates)",
             lambda src, a: origin(src) == "first-party" and not a["own"] and state(a) == "gc"),
            ("EE symbols used only inside their own file (could be static)",
             lambda src, a: origin(src) == "first-party" and a["own"] and state(a) != "n/a")):
        per = collections.defaultdict(lambda: [0, 0])
        for (src, sym), a in cand:
            if test(src, a):
                per[src][0] += 1
                per[src][1] += a["size"]
        f.write(f"\n## {title}: per source file (count, bytes), top 60 of {len(per)} files, "
                f"{sum(v[0] for v in per.values())} symbols, {sum(v[1] for v in per.values())} bytes\n")
        for src, (cnt, size) in sorted(per.items(), key=lambda kv: -kv[1][1])[:60]:
            f.write(f"{cnt:5d} {size:9d}  {src}\n")
    if missing:
        f.write("\n## objects named by the ninja graph but not present (not built)\n")
        for o in sorted(missing):
            f.write(o + "\n")
print("links", len(links), "candidates", len(cand), "missing", len(missing))
