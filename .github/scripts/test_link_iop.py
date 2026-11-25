"""tools/link_iop.py corrects the address halves ld.lld -r gets wrong, and stops when a link does not match.

Builds two small object files and the module ld.lld -r makes of them (the second file's code lands across the
32 KiB mark, so the address of its static function and of one of its strings come out 64 KiB off), with the
link map, and checks:
  - the two wrong high halves are corrected and nothing else in the module changes;
  - a second run changes nothing; the same holds with the second file inside an archive;
  - a module whose low half is not the expected one is refused and removed;
  - the command form runs the linker, adds -Map, and corrects the result;
  - tools/build.py routes the IOP link rules of an LLVM build through this step, once.
"""
from pathlib import Path
import importlib.util
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, root / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tool = load('link_iop')
driver = load('build')

TEXT, DATA, STRINGS = 1, 2, 3   # section and section-symbol numbers in the object files below


def elf(sections):
    """sections: (name, type, flags, bytes, link, info, entsize); returns an ELF relocatable file."""
    names = b'\0'
    body = b''
    headers = bytes(40)
    for name, kind, flags, data, link, info, entsize in sections + [('.shstrtab', 3, 0, None, 0, 0, 0)]:
        name_at = len(names)
        names += name.encode() + b'\0'
        if data is None:
            data = names
        headers += struct.pack('<10I', name_at, kind, flags, 0, 0x34 + len(body), len(data), link, info, 4, entsize)
        body += data + bytes(-len(data) % 4)
    count = len(sections) + 2
    return struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01' + bytes(9), 1, 8, 1, 0, 0, 0x34 + len(body), 0,
                       0x34, 0, 0, 40, count, count - 1) + body + headers


def pair(high, low):
    return struct.pack('<II', 0x3C020000 | high, 0x24420000 | (low & 0xFFFF))


def object_file(text, data, strings, relocations):
    symbols = bytes(16) + b''.join(struct.pack('<IIIBBH', 0, 0, 0, 3, 0, n) for n in (TEXT, DATA, STRINGS))
    rel = b''.join(struct.pack('<II', offset, (symbol << 8) | kind) for offset, kind, symbol in relocations)
    return elf([('.text', 1, 6, text, 0, 0, 0), ('.data', 1, 3, data, 0, 0, 0),
                ('.rodata.str1.1', 1, 0x32, strings, 0, 0, 1), ('.symtab', 2, 0, symbols, 5, 4, 16),
                ('.strtab', 3, 0, b'\0', 0, 0, 0), ('.rel.text', 9, 0, rel, 4, TEXT, 8)])


# a.o: 0x7000 bytes of code, one address of its own data. b.o: lands at .text+0x7000.
a_text = pair(0, 8) + bytes(0x7000 - 8)
a = object_file(a_text, bytes(16), b'def\0zzz\0', [(0, 5, DATA), (4, 6, DATA)])
# b.o: its static function at +0x1294 (module .text+0x8294), its data +4, its strings "abc" (+0) and "def" (+4),
# a second low half for the function's address, and a high half with no low half.
b_text = pair(0, 0x1294) + pair(0, 4) + pair(0, 0) + pair(0, 4) + struct.pack('<II', 0x8C431294, 0x3C020000) + bytes(0x2000 - 40)
b = object_file(b_text, bytes(16), b'abc\0def\0',
                [(0, 5, TEXT), (4, 6, TEXT), (8, 5, DATA), (12, 6, DATA), (16, 5, STRINGS), (20, 6, STRINGS),
                 (24, 5, STRINGS), (28, 6, STRINGS), (32, 6, TEXT), (36, 5, DATA)])

# What ld.lld -r writes: every low half right; a high half moved by the place of the section alone (and, for a
# string, by the place of the section's FIRST string). Strings are joined at .rodata+0x7ffc: "def" "zzz" "abc".
RODATA = bytes(0x7FFC) + b'def\0zzz\0abc\0'
linked_b = (pair(0, 0x8294) + pair(0, 0x14) + pair(1, 0x8004) + pair(1, 0x7FFC)
            + struct.pack('<II', 0x8C438294, 0x3C020000) + bytes(0x2000 - 40))
right_b = (pair(1, 0x8294) + pair(0, 0x14) + pair(1, 0x8004) + pair(0, 0x7FFC)
           + struct.pack('<II', 0x8C438294, 0x3C020000) + bytes(0x2000 - 40))


def module(b_part):
    return elf([('.text', 1, 6, a_text + b_part, 0, 0, 0), ('.rodata', 1, 0x32, RODATA, 0, 0, 1),
                ('.data', 1, 3, bytes(32), 0, 0, 0)])


def link_map(b_name):
    return f'''     VMA      LMA     Size Align Out     In      Symbol
       0        0     9000    16 .text
       0        0     7000    16         a.o:(.text)
       0        0        8     1                 entry
    7000     7000     2000    16         {b_name}:(.text)
       0        0     8008     1 .rodata
    7ffc     7ffc        c     1         <internal>:(.rodata.str1.1)
       0        0       20    16 .data
       0        0       10    16         a.o:(.data)
      10       10       10    16         {b_name}:(.data)
'''


def archive(members):
    out = b'!<arch>\n'
    long_names = b''.join(name.encode() + b'/\n' for name, _ in members)
    entries = [('//', long_names)]
    at = 0
    for name, data in members:
        entries.append(('/%d' % at, data))
        at += len(name) + 2
    for name, data in entries:
        out += ('%-16s%-12d%-6d%-6d%-8s%-10d`\n' % (name, 0, 0, 0, '644', len(data))).encode() + data + b'\n' * (len(data) & 1)
    return out


with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'a.o').write_bytes(a)
    (tmp / 'b.o').write_bytes(b)
    (tmp / 'libb.a').write_bytes(archive([('some_other_member_with_a_long_name.o', a), ('b.o', b)]))
    for b_name in ('b.o', 'libb.a(b.o)'):
        (tmp / 'out.o').write_bytes(module(linked_b))
        (tmp / 'out.map').write_text(link_map(b_name))
        run = subprocess.run([sys.executable, str(root / 'tools/link_iop.py'), '--repair', 'out.o', 'out.map'],
                             cwd=tmp, capture_output=True, text=True)
        assert run.returncode == 0 and '2 of 9 address halves corrected' in run.stdout, (run.stdout, run.stderr)
        assert (tmp / 'out.o').read_bytes() == module(right_b), b_name
        run = subprocess.run([sys.executable, str(root / 'tools/link_iop.py'), '--repair', 'out.o', 'out.map'],
                             cwd=tmp, capture_output=True, text=True)
        assert run.returncode == 0 and run.stdout == '' and (tmp / 'out.o').read_bytes() == module(right_b), run

    # A low half that is not the expected one: the link is not understood, the module must not be used.
    (tmp / 'out.o').write_bytes(module(linked_b.replace(pair(0, 0x14), pair(0, 0x18))))
    run = subprocess.run([sys.executable, str(root / 'tools/link_iop.py'), '--repair', 'out.o', 'out.map'],
                         cwd=tmp, capture_output=True, text=True)
    assert run.returncode == 1 and 'low half' in run.stderr and not (tmp / 'out.o').exists(), run
    # A high half that is neither right nor wrong in the known way.
    (tmp / 'out.o').write_bytes(module(linked_b.replace(pair(0, 0x8294), pair(3, 0x8294))))
    run = subprocess.run([sys.executable, str(root / 'tools/link_iop.py'), '--repair', 'out.o', 'out.map'],
                         cwd=tmp, capture_output=True, text=True)
    assert run.returncode == 1 and 'high half' in run.stderr and not (tmp / 'out.o').exists(), run
    # A file the link map names that is not there.
    (tmp / 'out.o').write_bytes(module(linked_b))
    (tmp / 'out.map').write_text(link_map('missing.o'))
    run = subprocess.run([sys.executable, str(root / 'tools/link_iop.py'), '--repair', 'out.o', 'out.map'],
                         cwd=tmp, capture_output=True, text=True)
    assert run.returncode == 1 and 'missing.o' in run.stderr and not (tmp / 'out.o').exists(), run

    # The command form: a stand-in linker writes the module and, because -Map= was added, the map.
    (tmp / 'linked.bin').write_bytes(module(linked_b))
    (tmp / 'linked.map').write_text(link_map('b.o'))
    (tmp / 'linker.py').write_text(
        'import shutil, sys\n'
        'if "--fail" in sys.argv: sys.exit(7)\n'
        'out = sys.argv[sys.argv.index("-o") + 1]\n'
        'assert sys.argv[-1] == "-Map=" + out + ".map", sys.argv\n'
        'shutil.copy("linked.bin", out); shutil.copy("linked.map", out + ".map")\n')
    command = [sys.executable, str(root / 'tools/link_iop.py'), sys.executable, 'linker.py', '-Tlinkfile', '-o', 'mod.o', 'a.o', 'b.o', '-r']
    run = subprocess.run(command, cwd=tmp, capture_output=True, text=True)
    assert run.returncode == 0 and (tmp / 'mod.o').read_bytes() == module(right_b), run
    run = subprocess.run(command + ['--fail'], cwd=tmp, capture_output=True, text=True)
    assert run.returncode == 7, run

    # tools/build.py: both IOP link rules go through the step, once; other rules stay; no rule is an error.
    ninja = tmp / 'build.ninja'
    ninja.write_text('rule ee_link\n  command = ld.lld -T$linkfile -o $out $in\n'
                     'rule iop_link\n  command = /sdk/llvm/bin/ld.lld -T$linkfile -o $out $in $flags $libs\n  description = LINK(iop) $out\n'
                     'rule iop_link_rsp\n  command = /sdk/llvm/bin/ld.lld -T$linkfile -o $out @$out.rsp\n  rspfile = $out.rsp\n')
    driver.route_iop_links(ninja)
    once = ninja.read_text()
    assert once.count('link_iop.py') == 2 and 'rule ee_link\n  command = ld.lld ' in once, once
    assert 'link_iop.py /sdk/llvm/bin/ld.lld -T$linkfile -o $out $in $flags $libs\n' in once, once
    assert 'link_iop.py /sdk/llvm/bin/ld.lld -T$linkfile -o $out @$out.rsp\n  rspfile = $out.rsp\n' in once, once
    driver.route_iop_links(ninja)
    assert ninja.read_text() == once
    ninja.write_text('rule ee_link\n  command = ld.lld -o $out $in\n')
    try:
        driver.route_iop_links(ninja)
    except RuntimeError as e:
        assert 'no IOP link rule' in str(e)
    else:
        raise AssertionError('a build file without an IOP link rule must be refused')
print('link_iop: wrong high halves are corrected, a link that does not match is refused')
