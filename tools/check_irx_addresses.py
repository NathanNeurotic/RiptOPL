#!/usr/bin/env python3
"""Check every IOP module (IRX) for code addresses that point outside the module.

An IRX loads at an address the console picks, so each place that builds an address from two instructions
(lui + addiu/lw/sw, relocations R_MIPS_HI16 + R_MIPS_LO16) holds the address the module would have at 0.
That address is (high half << 16) + the low half taken as a SIGNED 16-bit number, and the console's loader
adds the load address to exactly that.

ld.lld's partial link (-r) can write the high half without the carry of a low half of 0x8000 or more, when
the target is a static function or static data and its object file lies across a 32 KiB boundary of the
module. The pair then says "64 KiB lower", and the module uses a wrong address on the console. Found in
the rolling build of commit 5881885: the ingamediag build's memory-record hooks in bdm_ata_cdvdman (the console stopped while the
IOP restarted, before the game loaded) and, in the normal build, the three DEV9 interrupt handlers of
smb_cdvdman and one callback in the SDK's ps2ip.

A pair is reported when the address it encodes is below 0, or more than 32 KiB past the end of the module
(the gp pointer legitimately sits up to 0x7FF0 past a section). A wrong pair whose result still lands inside
a module larger than 64 KiB cannot be told from a right one, so a clean result does not prove such a module.

Usage: check_irx_addresses.py [--repair] PATH [PATH ...]   (IRX files, ELF files with IRX modules embedded, or folders)
Exit status: 0 nothing found, 1 at least one address outside its module, 2 no module was found to check.

--repair corrects what it finds, in the files themselves: a pair outside its module is 64 KiB off and only one
of the two neighbours lies inside, so its high half is set to that one. It is for modules that come ready-made
(the SDK's); modules built here are linked through tools/link_iop.py, which corrects every pair, also those
this check cannot see. Corrected pairs are counted and no longer reported.
"""
import struct
import sys
from pathlib import Path

ELF_MAGIC = b'\x7fELF\x01\x01\x01'
IRX_TYPES = (0xFF80, 0xFF81)
R_MIPS_HI16, R_MIPS_LO16 = 5, 6
GP_REACH = 0x8000


def scan_irx(e):
    """Return (name, memory size, pairs checked, [(section, offset, encoded address, offset in e, right high half)]).

    The right high half is None when neither neighbouring 64 KiB step puts the address inside the module."""
    shoff, = struct.unpack_from('<I', e, 0x20)
    phoff, = struct.unpack_from('<I', e, 0x1C)
    phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from('<HHHHH', e, 0x2A)
    if shoff == 0 or shnum == 0 or shoff + shnum * shentsize > len(e) or shstrndx >= shnum:
        return None
    secs = [struct.unpack_from('<10I', e, shoff + i * shentsize) for i in range(shnum)]
    strtab = secs[shstrndx]

    def name(n):
        s = e[strtab[4] + n:strtab[4] + n + 64]
        return s.split(b'\0')[0].decode('latin1')

    memsz = 0
    for i in range(phnum):
        p = struct.unpack_from('<8I', e, phoff + i * phentsize)
        if p[0] == 1:
            memsz = max(memsz, p[2] + p[5])
    module = '?'
    for s in secs:
        if name(s[0]) == '.iopmod' and s[5] > 27:
            module = e[s[4] + 26:s[4] + s[5]].split(b'\0')[0].decode('latin1') or '?'
    bad = []
    pairs = 0
    for s in secs:
        if s[1] != 9 or s[7] >= shnum:  # SHT_REL
            continue
        target = secs[s[7]]
        taddr, toff, tsize = target[3], target[4], target[5]
        rels = [struct.unpack_from('<II', e, s[4] + i * 8) for i in range(s[5] // 8)]
        for i, (r_off, r_info) in enumerate(rels):
            if (r_info & 0xFF) != R_MIPS_HI16:
                continue
            j = i + 1
            while j < len(rels) and (rels[j][1] & 0xFF) == R_MIPS_HI16:
                j += 1
            if j >= len(rels) or (rels[j][1] & 0xFF) != R_MIPS_LO16:
                continue
            if not (taddr <= r_off < taddr + tsize and taddr <= rels[j][0] < taddr + tsize):
                continue
            hi = struct.unpack_from('<I', e, toff + r_off - taddr)[0] & 0xFFFF
            lo = struct.unpack_from('<I', e, toff + rels[j][0] - taddr)[0] & 0xFFFF
            address = (hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)
            pairs += 1
            if address < 0 or address > memsz + GP_REACH:
                moved = address + 0x10000 if address < 0 else address - 0x10000
                right = (hi + (1 if address < 0 else -1)) & 0xFFFF if 0 <= moved <= memsz + GP_REACH else None
                bad.append((name(target[0]), r_off, address, toff + r_off - taddr, right))
    return module, memsz, pairs, bad


def scan_file(path):
    data = path.read_bytes()
    if not data.startswith(b'\x7fELF'):
        return []
    starts = []
    o = 0
    while True:
        o = data.find(ELF_MAGIC, o)
        if o < 0:
            break
        if o + 0x34 <= len(data) and struct.unpack_from('<H', data, o + 16)[0] in IRX_TYPES:
            starts.append(o)
        o += 4
    results = []
    for n, a in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else len(data)
        found = scan_irx(data[a:end])
        if found:
            results.append((a, *found))
    return results


def repair_file(path):
    """Correct every repairable pair of `path` in place; return how many were corrected."""
    data = bytearray(path.read_bytes())
    corrected = 0
    for start, _, _, _, bad in scan_file(path):
        for _, _, _, field, right in bad:
            if right is not None:
                struct.pack_into('<H', data, start + field, right)
                corrected += 1
    if corrected:
        path.write_bytes(data)
    return corrected


def main(argv):
    repair = '--repair' in argv
    argv = [a for a in argv if a != '--repair']
    files = []
    for arg in argv:
        p = Path(arg)
        files.extend(sorted(q for q in p.rglob('*') if q.is_file()) if p.is_dir() else [p])
    modules = pairs_total = bad_total = corrected = 0
    for f in files:
        if repair:
            try:
                done = repair_file(f)
            except OSError as e:
                print(f'{f}: not corrected: {e}')
            else:
                corrected += done
                if done:
                    print(f'{f}: {done} address pairs corrected')
        for offset, module, memsz, pairs, bad in scan_file(f):
            modules += 1
            pairs_total += pairs
            bad_total += len(bad)
            for section, r_off, address, _, _ in bad:
                where = f'{f}' + (f' (module at file offset 0x{offset:x})' if offset else '')
                print(f'{where}: {module}: {section}+0x{r_off:x} encodes address '
                      f'{"-" if address < 0 else ""}0x{abs(address):x}, outside the module (0..0x{memsz:x})')
    print(f'{modules} IOP modules, {pairs_total} address pairs checked, {bad_total} outside their module'
          + (f', {corrected} corrected' if repair else ''))
    if modules == 0:
        return 2
    return 1 if bad_total else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
