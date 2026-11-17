#!/usr/bin/env python3
"""Parse a GNU ld link map and write size evidence.

usage: mapstats.py <map> <outdir> <prefix> <repo_root> <build_dir>

Writes (all under <outdir>, names start with <prefix>):
  -archive-members.txt     the "Archive member included" table, grouped by archive
  -discarded-summary.txt   count/bytes of "Discarded input sections", by kind and by origin
  -discarded-firstparty.txt  discarded sections that come from first-party objects
  -per-object.tsv          bytes each input file contributes to each output section group
  -per-object-top.txt      readable top lists per output section group
  -zero-contribution.txt   objects on the link line that contribute 0 bytes to the output
"""
import collections
import os
import re
import sys

mp, outdir, prefix, repo_root, build_dir = sys.argv[1:6]
repo_root = repo_root.replace("\\", "/").rstrip("/")
build_dir = build_dir.replace("\\", "/").rstrip("/")
text = open(mp, encoding="utf-8", errors="replace").read().replace("\r\n", "\n")
lines = text.split("\n")


def norm(path):
    p = path.replace("\\", "/")
    for root, tag in ((build_dir, "<build>"), (repo_root, "")):
        low = p.lower()
        idx = low.find(root.lower())
        if idx >= 0:
            p = tag + p[idx + len(root):]
            if not tag:
                p = p.lstrip("/")
    m = re.search(r"(?i)/ps2build/(.*)$", p)
    if m:
        p = "<sdk>/" + m.group(1)
    return p


def category(path):
    """sdk | blob-irx (embedded IRX/ELF) | blob-asset (bin2c of gfx/audio/misc) | vendored | first-party"""
    n = norm(path)
    if n.startswith("<sdk>"):
        return "sdk"
    if n.startswith("obj/_embed/"):
        return "blob-irx"
    if re.match(r"^obj/[^/]+/asm/", n):
        return "blob-asset"
    if n.startswith("lib/libmbedtls.a") or "/third_party/" in n or "/lwNBD/" in n:
        return "vendored"
    return "first-party"


def first_party(path):
    """True for objects compiled from the repository's own sources (not SDK, not blobs, not vendored)."""
    return category(path) == "first-party"


def section_index(title):
    for i, line in enumerate(lines):
        if line.strip() == title:
            return i
    return -1


i_arch = section_index("Archive member included to satisfy reference by file (symbol)")
i_disc = section_index("Discarded input sections")
i_memc = section_index("Memory Configuration")
i_map = section_index("Linker script and memory map")
i_alloc = section_index("Allocating common symbols")

# ---------------------------------------------------------------- archive members
ends = sorted(x for x in (i_disc, i_memc, i_map, i_alloc, len(lines)) if x > i_arch)
members = collections.OrderedDict()
if i_arch >= 0:
    block = [l for l in lines[i_arch + 1:ends[0]] if l.strip()]
    cur = None
    for l in block:
        if not l.startswith(" "):
            cur = l.strip()
            # the reason may be on the same line
            m = re.match(r"^(\S+\(\S+\))\s+(.*)$", cur)
            if m:
                members[m.group(1)] = m.group(2).strip()
                cur = None
            else:
                members[cur] = ""
        elif cur:
            members[cur] = l.strip()
            cur = None
by_archive = collections.defaultdict(list)
for mem, why in members.items():
    m = re.match(r"^(.*)\(([^()]+)\)$", mem)
    arc, obj = (m.group(1), m.group(2)) if m else (mem, "")
    by_archive[norm(arc)].append((obj, norm(why)))
with open(os.path.join(outdir, prefix + "-archive-members.txt"), "w", encoding="utf-8") as f:
    f.write(f"# Archive members pulled into the link, from {os.path.basename(mp)} (\"Archive member included ...\")\n")
    f.write(f"# total members: {len(members)} from {len(by_archive)} archives\n\n")
    f.write("## members per archive\n")
    for arc, objs in sorted(by_archive.items(), key=lambda kv: -len(kv[1])):
        f.write(f"{len(objs):5d}  {arc}\n")
    f.write("\n## detail (member <- first referencing file (symbol))\n")
    for arc, objs in sorted(by_archive.items()):
        f.write(f"\n{arc}  [{len(objs)}]\n")
        for obj, why in objs:
            f.write(f"    {obj:40s} <- {why}\n")

# ---------------------------------------------------------------- generic "section addr size file" reader
ENTRY = re.compile(r"^\s(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$")
NAMEONLY = re.compile(r"^\s(\S+)$")
CONT = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$")
FILL = re.compile(r"^\s\*fill\*\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")


def read_inputs(block):
    """Yield (section, addr, size, file) for input-section lines in a map block."""
    pending = None
    for l in block:
        m = ENTRY.match(l)
        if m:
            pending = None
            yield m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4).strip()
            continue
        m = NAMEONLY.match(l)
        if m:
            pending = m.group(1)
            continue
        m = CONT.match(l)
        if m and pending:
            yield pending, int(m.group(1), 16), int(m.group(2), 16), m.group(3).strip()
        pending = None


# ---------------------------------------------------------------- discarded sections
disc = []
if i_disc >= 0:
    end = min(x for x in (i_memc, i_map, len(lines)) if x > i_disc)
    disc = list(read_inputs(lines[i_disc + 1:end]))


def kind(sec):
    for k in (".text", ".rodata", ".data", ".sdata", ".sbss", ".bss", ".lit", ".eh_frame", ".gcc_except_table",
              ".debug", ".comment", ".mdebug", ".reginfo", ".pdr", ".gnu.attributes", ".MIPS", ".note", ".ctors",
              ".dtors", ".init_array", ".fini_array", ".group", ".iop", ".rel"):
        if sec == k or sec.startswith(k + ".") or sec.startswith(k):
            return k
    return "other"


ALLOC = {".text", ".rodata", ".data", ".sdata", ".sbss", ".bss", ".lit", ".eh_frame", ".gcc_except_table", ".ctors",
         ".dtors", ".init_array", ".fini_array"}
with open(os.path.join(outdir, prefix + "-discarded-summary.txt"), "w", encoding="utf-8") as f:
    f.write(f"# \"Discarded input sections\" of {os.path.basename(mp)}\n")
    f.write("# NOTE: the list also holds non-loaded metadata (.comment, .pdr, .mdebug, .gnu.attributes, .reginfo, COMDAT\n")
    f.write("# .group duplicates). Only the allocatable kinds are code/data that --gc-sections or COMDAT folding removed.\n")
    f.write("# A section discarded here is NOT in the ELF: it is evidence of unused source, not of shipped bloat.\n\n")
    f.write(f"total entries: {len(disc)}   total bytes: {sum(s for _, _, s, _ in disc)}\n")
    nz = [d for d in disc if d[2]]
    f.write(f"non-empty entries: {len(nz)}\n\n")
    f.write("## by section kind (entries, bytes)\n")
    k = collections.defaultdict(lambda: [0, 0])
    for sec, _, size, _ in disc:
        k[kind(sec)][0] += 1
        k[kind(sec)][1] += size
    for name, (n, b) in sorted(k.items(), key=lambda kv: -kv[1][1]):
        f.write(f"{name:20s} {n:7d} {b:10d}{'   (allocatable)' if name in ALLOC else ''}\n")
    f.write("\n## allocatable kinds only, by origin (entries, bytes)\n")
    o = collections.defaultdict(lambda: [0, 0])
    for sec, _, size, file in disc:
        if kind(sec) in ALLOC and size:
            key = f"{category(file):12s} {kind(sec)}"
            o[key][0] += 1
            o[key][1] += size
    for name, (n, b) in sorted(o.items()):
        f.write(f"{name:34s} {n:7d} {b:10d}\n")
    f.write("\n## allocatable discarded bytes per input file (top 60)\n")
    pf = collections.defaultdict(lambda: [0, 0])
    for sec, _, size, file in disc:
        if kind(sec) in ALLOC and size:
            pf[norm(file)][0] += 1
            pf[norm(file)][1] += size
    for name, (n, b) in sorted(pf.items(), key=lambda kv: -kv[1][1])[:60]:
        f.write(f"{b:9d} {n:5d}  {name}\n")

with open(os.path.join(outdir, prefix + "-discarded-firstparty.txt"), "w", encoding="utf-8") as f:
    f.write(f"# First-party discarded input sections of {os.path.basename(mp)} (allocatable kinds, size > 0)\n")
    f.write("# columns: bytes  section  object\n")
    f.write("# A function/data section listed here was compiled but dropped by --gc-sections (unreferenced in THIS\n")
    f.write("# build configuration) or is a COMDAT duplicate (inline/template copy kept from another object).\n")
    f.write("# It is a CANDIDATE for dead code: check other editions/configurations before deleting the source.\n\n")
    rows = [(size, sec, norm(file)) for sec, _, size, file in disc
            if kind(sec) in ALLOC and size and first_party(file)]
    f.write(f"# entries: {len(rows)}   bytes: {sum(r[0] for r in rows)}\n\n")
    for size, sec, file in sorted(rows, key=lambda r: (r[2], -r[0], r[1])):
        f.write(f"{size:8d}  {sec}  {file}\n")

# ---------------------------------------------------------------- memory map: per-object contribution
OUTSEC = re.compile(r"^(\.[A-Za-z0-9_.]+|[A-Za-z_][A-Za-z0-9_.]*)(\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+).*)?$")
per = collections.defaultdict(lambda: collections.defaultdict(int))
outsizes = collections.OrderedDict()
cur_out = None
pending = None
loaded = []
start = i_map + 1 if i_map >= 0 else 0
for l in lines[start:]:
    if l.startswith("LOAD "):
        loaded.append(l[5:].strip())
        continue
    if l.startswith("OUTPUT(") or l.startswith("Cross Reference Table"):
        break
    if l and not l[0].isspace():
        m = OUTSEC.match(l)
        if m:
            cur_out = m.group(1)
            if m.group(4):
                outsizes[cur_out] = int(m.group(4), 16)
            pending = None
            continue
    if cur_out is None:
        continue
    m = FILL.match(l)
    if m:
        per[cur_out]["*fill* (alignment padding)"] += int(m.group(2), 16)
        pending = None
        continue
    if l.startswith(" ") and not l.startswith("  "):
        m = ENTRY.match(l)
        if m:
            sec, size, file = m.group(1), int(m.group(3), 16), m.group(4).strip()
            if not file.startswith("0x") and "=" not in file.split()[0] and not sec.startswith("*"):
                if sec == "*fill*":
                    per[cur_out]["*fill*"] += size
                else:
                    per[cur_out][norm(file)] += size
            pending = None
            continue
        m = NAMEONLY.match(l)
        if m:
            pending = m.group(1)
            continue
    m = CONT.match(l)
    if m and pending and not l.startswith("                0x") is False:
        pass
    if m and pending:
        size, file = int(m.group(2), 16), m.group(3).strip()
        if pending == "*fill*":
            per[cur_out]["*fill*"] += size
        elif re.match(r"^[\w.$]+$", pending) or pending.startswith("."):
            per[cur_out][norm(file)] += size
        pending = None
    elif m and cur_out and l.startswith(cur_out[:1]) is False and cur_out not in outsizes:
        pass
    else:
        if l.strip() and not NAMEONLY.match(l):
            pending = None
    # output section whose address/size is on the following line
    if cur_out not in outsizes:
        m2 = re.match(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s*$", l)
        if m2:
            outsizes[cur_out] = int(m2.group(2), 16)


def group(out):
    if out in (".text", ".init", ".fini"):
        return "text"
    if out in (".rodata", ".rdata", ".eh_frame", ".gcc_except_table", ".lit4", ".lit8", ".ctors", ".dtors",
               ".init_array", ".fini_array", ".reginfo"):
        return "rodata"
    if out in (".data", ".sdata", ".tdata"):
        return "data"
    if out in (".bss", ".sbss", ".tbss"):
        return "bss"
    return None


GROUPS = ("text", "rodata", "data", "bss")
tot = collections.defaultdict(lambda: collections.defaultdict(int))
for out, files in per.items():
    g = group(out)
    if not g:
        continue
    for file, size in files.items():
        tot[file][g] += size
with open(os.path.join(outdir, prefix + "-per-object.tsv"), "w", encoding="utf-8") as f:
    f.write("file\ttext\trodata\tdata\tbss\tfile_bytes(text+rodata+data)\tram_bytes(all)\n")
    for file, g in sorted(tot.items(), key=lambda kv: -sum(kv[1].values())):
        f.write("\t".join([file] + [str(g[x]) for x in GROUPS] +
                          [str(g["text"] + g["rodata"] + g["data"]), str(sum(g.values()))]) + "\n")


def archive_of(file):
    m = re.match(r"^(.*\.a)\(([^()]+)\)$", file)
    return m.group(1) if m else None


with open(os.path.join(outdir, prefix + "-per-object-top.txt"), "w", encoding="utf-8") as f:
    f.write(f"# Per-input-file contribution to {os.path.basename(mp)} (bytes), parsed from the memory map\n")
    f.write("# groups: text=.text  rodata=.rodata/.eh_frame/.lit*/.ctors/.dtors/.reginfo  data=.data/.sdata  bss=.bss/.sbss\n")
    f.write("# obj/<target>/<source path>.o = object built from this repository (source = same path with its extension);\n")
    f.write("# obj/_embed/<name>_irx.o = embedded IRX/ELF blob; lib/lib*.a = library built by this build;\n")
    f.write("# <sdk>/... = ps2build SDK or toolchain archive member\n\n")
    f.write("## output section sizes seen in the map\n")
    for out, size in outsizes.items():
        if size:
            f.write(f"{out:24s} {size:10d}\n")
    f.write("\n## group totals (sum of attributed input sections, includes *fill*)\n")
    for g in GROUPS:
        f.write(f"{g:8s} {sum(v[g] for v in tot.values()):10d}\n")
    # per archive rollup
    cat = collections.defaultdict(lambda: collections.defaultdict(int))
    for file, g in tot.items():
        c = "padding" if file.startswith("*fill*") else category(file)
        for x in GROUPS:
            cat[c][x] += g[x]
    f.write("\n## rollup by category (text rodata data bss | total)\n")
    f.write("# first-party = this repo's own sources; vendored = third_party/mbedtls; blob-irx = embedded IRX/ELF modules\n")
    f.write("# (obj/_embed); blob-asset = bin2c of gfx/audio/misc/fonts/certs (asm/*.c); sdk = ps2build SDK + toolchain libs\n")
    for key, g in sorted(cat.items(), key=lambda kv: -sum(kv[1].values())):
        f.write(f"{g['text']:9d} {g['rodata']:9d} {g['data']:9d} {g['bss']:9d} | {sum(g.values()):9d}  {key}\n")
    roll = collections.defaultdict(lambda: collections.defaultdict(int))
    for file, g in tot.items():
        key = archive_of(file) or ("[first-party objects + generated blobs]" if file.startswith("<build>") else file)
        for x in GROUPS:
            roll[key][x] += g[x]
    f.write("\n## rollup by archive / origin (text rodata data bss | total)\n")
    for key, g in sorted(roll.items(), key=lambda kv: -sum(kv[1].values())):
        f.write(f"{g['text']:9d} {g['rodata']:9d} {g['data']:9d} {g['bss']:9d} | {sum(g.values()):9d}  {key}\n")
    for g in GROUPS:
        f.write(f"\n## top 60 contributors to {g}\n")
        for file, v in sorted(tot.items(), key=lambda kv: -kv[1][g])[:60]:
            if v[g]:
                f.write(f"{v[g]:9d}  {file}\n")

# ---------------------------------------------------------------- objects with zero contribution
with open(os.path.join(outdir, prefix + "-zero-contribution.txt"), "w", encoding="utf-8") as f:
    f.write(f"# Objects/archives named on the link line (LOAD lines of {os.path.basename(mp)}) that contribute 0 bytes\n")
    f.write("# to .text/.rodata/.data/.bss of the output. For an archive: no member was pulled in.\n\n")
    contributing = set(tot)
    used_archives = {archive_of(x) for x in tot if archive_of(x)}
    n = 0
    for item in loaded:
        ni = norm(item)
        if ni.endswith(".a"):
            if ni not in used_archives:
                f.write(f"archive, no member used: {ni}\n")
                n += 1
        elif ni not in contributing or sum(tot[ni].values()) == 0:
            f.write(f"object, 0 bytes: {ni}\n")
            n += 1
    f.write(f"\n# {n} of {len(loaded)} link inputs contribute nothing\n")
print("members", len(members), "discarded", len(disc), "outsections", len(outsizes), "inputs", len(tot), "LOAD", len(loaded))
