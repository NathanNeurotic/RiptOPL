#!/usr/bin/env python3
"""Link one IOP module with ld.lld and correct the address halves ld.lld's partial link (-r) gets wrong.

ld.lld -r moves each "address of something static" (relocations R_MIPS_HI16 + R_MIPS_LO16 against a section)
to the place the section has in the module. It moves the high half without looking at the low half, so the
carry between the two is lost whenever only the full address needs it: the address is then 64 KiB off. The
proper fix is in the linker (lld/ELF/InputSection.cpp, copyRelocations); this step is for linkers without it.

After the link, every such pair is worked out again from the files that were linked (the link map says where
each of their sections went) and the high half in the module is set to the right value. Each low half the
linker wrote must be the one worked out here, and each high half must be either right or wrong in exactly the
known way; anything else stops the build, because it would mean this step does not understand the link.
A linker that has the fix leaves nothing to correct. A line is printed for a module with corrections; with
LINK_IOP_REPORT set in the environment, for every module (how many halves were checked).

Usage: link_iop.py LINKER ARGUMENT...      (ARGUMENT... as given to the linker, "-o OUTPUT" among them)
       link_iop.py --repair OUTPUT MAP     (correct an existing partial link from its -Map file)
"""
import os
import re
import struct
import subprocess
import sys

SHT_SYMTAB, SHT_NOBITS, SHT_REL = 2, 8, 9
SHF_MERGE, SHF_STRINGS = 0x10, 0x20
STT_SECTION = 3
R_MIPS_HI16, R_MIPS_LO16 = 5, 6


class LinkNotUnderstood(Exception):
    pass


class Elf:
    def __init__(self, data):
        if data[:6] != b'\x7fELF\x01\x01':
            raise LinkNotUnderstood('not a 32-bit little-endian ELF file')
        self.data = data
        shoff, = struct.unpack_from('<I', data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2E)
        # name, type, flags, addr, offset, size, link, info, addralign, entsize
        self.sections = [struct.unpack_from('<10I', data, shoff + i * shentsize) for i in range(shnum)]
        names = self.sections[shstrndx]
        self.names = [self._text(names[4] + s[0]) for s in self.sections]
        self.symbols = []
        for s in self.sections:
            if s[1] == SHT_SYMTAB:
                # name, value, size, info, other, shndx
                self.symbols = [struct.unpack_from('<IIIBBH', data, s[4] + i * 16) for i in range(s[5] // 16)]

    def _text(self, offset):
        return self.data[offset:self.data.index(b'\0', offset)].decode('latin1')

    def content(self, index):
        s = self.sections[index]
        return b'' if s[1] == SHT_NOBITS else self.data[s[4]:s[4] + s[5]]

    def relocations(self, index):
        """(offset, type, symbol) of every REL entry that applies to section `index`, in table order."""
        found = []
        for s in self.sections:
            if s[1] == SHT_REL and s[7] == index:
                for i in range(s[5] // 8):
                    offset, info = struct.unpack_from('<II', self.data, s[4] + i * 8)
                    found.append((offset, info & 0xFF, info >> 8))
        return found


def archive_members(path):
    """[(member name, bytes)] of a GNU, BSD or thin ar archive."""
    data = open(path, 'rb').read()
    thin = data.startswith(b'!<thin>\n')
    if not thin and not data.startswith(b'!<arch>\n'):
        raise LinkNotUnderstood(f'{path}: not an archive')
    members, long_names, o = [], b'', 8
    while o + 60 <= len(data):
        name = data[o:o + 16].rstrip(b' ')
        size = int(data[o + 48:o + 58])
        o += 60
        body, stored = data[o:o + size], size
        if name == b'//':
            long_names = body
        elif name not in (b'/', b'/SYM64/', b'__.SYMDEF'):
            if name.startswith(b'#1/'):
                length = int(name[3:])
                name, body = body[:length].rstrip(b'\0'), body[length:]
            elif name.startswith(b'/'):
                start = int(name[1:])
                name = long_names[start:long_names.index(b'\n', start)].rstrip(b'/')
            else:
                name = name.rstrip(b'/')
            name = name.decode('latin1')
            if thin:
                body, stored = open(os.path.join(os.path.dirname(path), name), 'rb').read(), 0
            members.append((os.path.basename(name), body))
        o += stored + (stored & 1)
    return members


def parse_map(path):
    """{output section: [(offset in it, size, input file as the linker names it, input section)]}"""
    layout, current, base = {}, None, 0
    line_format = re.compile(r'^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+ ( *)(\S.*)$')
    for line in open(path, encoding='latin1'):
        m = line_format.match(line.rstrip('\n'))
        if not m:
            continue
        address, size, depth, text = int(m.group(1), 16), int(m.group(2), 16), len(m.group(3)), m.group(4)
        if depth == 0:
            current, base = layout.setdefault(text, []), address
        elif depth == 8 and current is not None:
            piece = re.match(r'^(.*):\(([^()]*)\)$', text)
            if piece:
                current.append((address - base, size, piece.group(1), piece.group(2)))
    return layout


class Repair:
    def __init__(self, output, map_path):
        self.output = output
        self.out = Elf(bytearray(open(output, 'rb').read()))
        self.layout = parse_map(map_path)
        self.archives = {}
        self.placed = {}    # input file -> {input section name: [(output section, offset, size)]}
        self.merged = {}    # input section name -> [(output section, offset, size)] (strings and constants joined by the linker)
        for section, pieces in self.layout.items():
            for offset, size, name, input_section in pieces:
                if name == '<internal>':
                    self.merged.setdefault(input_section, []).append((section, offset, size))
                else:
                    self.placed.setdefault(name, {}).setdefault(input_section, []).append((section, offset, size))
        self.out_index = {name: i for i, name in enumerate(self.out.names)}
        self.checked = self.corrected = 0

    def candidates(self, name):
        """The ELF files `name` in the link map can be: one object file, or the members of that name in an archive."""
        if os.path.isfile(name):
            return [Elf(open(name, 'rb').read())]
        m = re.match(r'^(.*)\(([^()]+)\)$', name)
        if not m or not os.path.isfile(m.group(1)):
            raise LinkNotUnderstood(f'{name}: named in the link map but not found')
        if m.group(1) not in self.archives:
            self.archives[m.group(1)] = archive_members(m.group(1))
        found = [Elf(body) for member, body in self.archives[m.group(1)] if member == os.path.basename(m.group(2))]
        if not found:
            raise LinkNotUnderstood(f'{name}: no such member in the archive')
        return found

    def places(self, elf, placed):
        """{input section index: (output section, offset)} for the sections of `elf` the link map lists."""
        where = {}
        for section_name, spots in placed.items():
            indexes = [i for i, n in enumerate(elf.names) if n == section_name]
            if len(indexes) != len(spots):
                indexes = [i for i in indexes if not elf.sections[i][2] & SHF_MERGE]
            if len(indexes) != len(spots) or any(elf.sections[i][5] != s[2] for i, s in zip(indexes, spots)):
                raise LinkNotUnderstood(f'section {section_name} does not match the link map')
            for i, (section, offset, _) in zip(indexes, spots):
                where[i] = (section, offset)
        return where

    def merged_place(self, elf, index, addend):
        """Where the linker put the string (or constant) that `addend` points into: (output section, offset)."""
        section = elf.sections[index]
        data, unit = elf.content(index), section[9] or 1
        if not 0 <= addend < len(data):
            raise LinkNotUnderstood(f'address {addend:#x} outside {elf.names[index]}')
        if section[2] & SHF_STRINGS:
            start = addend - addend % unit
            while start >= unit and any(data[start - unit:start]):
                start -= unit
            end = start
            while end < len(data) and any(data[end:end + unit]):
                end += unit
            end += unit
        else:
            start = addend - addend % unit
            end = start + unit
        piece = data[start:end]
        for out_section, offset, size in self.merged.get(elf.names[index], []):
            region = self.out.content(self.out_index[out_section])[offset:offset + size]
            at = -1
            while True:
                at = region.find(piece, at + 1)
                if at < 0:
                    break
                boundary = at == 0 or not section[2] & SHF_STRINGS or not any(region[at - unit:at])
                if at % unit == 0 and boundary:
                    return out_section, offset + at + (addend - start)
        raise LinkNotUnderstood(f'a string of {elf.names[index]} was not found in the module')

    def target(self, elf, where, index, addend):
        if index in where:
            return where[index][0], where[index][1] + addend
        if elf.sections[index][2] & SHF_MERGE:
            return self.merged_place(elf, index, addend)
        raise LinkNotUnderstood(f'section {elf.names[index]} is not in the link map')

    def changes(self, elf, placed):
        """[(file offset in the module, right high half)] for one linked file; raises when it does not match."""
        where = self.places(elf, placed)
        changes = []
        checked = 0
        for index, (out_section, base) in where.items():
            out = self.out.sections[self.out_index[out_section]]
            if out[1] == SHT_NOBITS:
                continue
            data = elf.content(index)
            relocations = elf.relocations(index)
            highs = {}
            for n, (offset, kind, symbol) in enumerate(relocations):
                if kind not in (R_MIPS_HI16, R_MIPS_LO16) or elf.symbols[symbol][3] & 0xF != STT_SECTION:
                    continue
                to = elf.symbols[symbol][5]
                field = out[4] + base + offset
                written, = struct.unpack_from('<H', self.out.data, field)
                half, = struct.unpack_from('<h', data, offset)
                if kind == R_MIPS_LO16:
                    # A low half belongs to the high half before it (same section); one on its own stands alone.
                    full = (highs.get(symbol, 0) << 16) + half
                    if written != self.target(elf, where, to, full)[1] & 0xFFFF:
                        raise LinkNotUnderstood(f'low half at {out_section}+{base + offset:#x} is not the expected one')
                    checked += 1
                    continue
                highs[symbol] = half
                low = next((struct.unpack_from('<h', data, o)[0] for o, k, s in relocations[n + 1:]
                            if k == R_MIPS_LO16 and s == symbol), None)
                if low is None:
                    continue    # a high half on its own: the linker has nothing to pair it with either
                right = ((self.target(elf, where, to, (half << 16) + low)[1] + 0x8000) >> 16) & 0xFFFF
                try:    # what the linker writes when it moves the high half without its low half
                    known = ((self.target(elf, where, to, half << 16)[1] + 0x8000) >> 16) & 0xFFFF
                except LinkNotUnderstood:
                    known = right
                if written not in (right, known):
                    raise LinkNotUnderstood(f'high half at {out_section}+{base + offset:#x} is neither right nor wrong in the known way')
                checked += 1
                if written != right:
                    changes.append((field, right))
        return changes, checked

    def run(self):
        if not self.placed:
            raise LinkNotUnderstood('the link map names no linked file')
        for name, placed in self.placed.items():
            problem = None
            for elf in self.candidates(name):
                try:
                    changes, checked = self.changes(elf, placed)
                except LinkNotUnderstood as e:
                    problem = e
                    continue
                for field, right in changes:
                    struct.pack_into('<H', self.out.data, field, right)
                self.checked += checked
                self.corrected += len(changes)
                break
            else:
                raise LinkNotUnderstood(f'{name}: {problem}')
        if self.corrected:
            with open(self.output, 'wb') as f:
                f.write(self.out.data)
        return self.checked, self.corrected


def repair(output, map_path):
    try:
        checked, corrected = Repair(output, map_path).run()
    except (LinkNotUnderstood, OSError, ValueError, IndexError, struct.error) as e:
        print(f'link_iop: {output}: {e}', file=sys.stderr)
        try:
            os.remove(output)   # never leave a module that was not checked for the next build step
        except OSError:
            pass
        return 1
    if corrected or os.environ.get('LINK_IOP_REPORT'):
        print(f'link_iop: {output}: {corrected} of {checked} address halves corrected')
    return 0


def main(argv):
    if len(argv) == 3 and argv[0] == '--repair':
        return repair(argv[1], argv[2])
    if len(argv) < 3 or '-o' not in argv[:-1]:
        print(__doc__, file=sys.stderr)
        return 2
    output = argv[argv.index('-o') + 1]
    status = subprocess.call([*argv, '-Map=' + output + '.map'])
    return status or repair(output, output + '.map')


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
