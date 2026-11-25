"""tools/check_irx_addresses.py reports an address pair that points outside its module, and nothing else.

Builds small IRX-shaped ELF files in memory: one whose HI16/LO16 pairs are right (a low half below 0x8000,
a low half of 0x8000 or more with the carry in the high half, and a gp pointer just past the module), and
one where the carry is missing, as ld.lld -r wrote it for the static handlers in the rolling build of commit 5881885.
Also checks that the handlers found broken in that build are no longer static.
"""
from pathlib import Path
import importlib.util
import struct
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('check_irx_addresses', root / 'tools/check_irx_addresses.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def irx(pairs, memsz=0x9000):
    """pairs: (high half, low half) for one lui/addiu each."""
    text = b''.join(struct.pack('<II', 0x3C010000 | hi, 0x24210000 | lo) for hi, lo in pairs)
    rel = b''.join(struct.pack('<II', i * 8, (1 << 8) | 5) + struct.pack('<II', i * 8 + 4, (1 << 8) | 6)
                   for i in range(len(pairs)))
    names = b'\0.text\0.rel.text\0.shstrtab\0'
    toff = 0x34 + 0x20
    roff = toff + len(text)
    soff = roff + len(rel)
    shoff = soff + len(names)
    header = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01' + bytes(9), 0xFF80, 8, 1, 0, 0x34, shoff, 0,
                         0x34, 0x20, 1, 0x28, 4, 3)
    ph = struct.pack('<8I', 1, toff, 0, 0, len(text), memsz, 7, 16)
    sh = bytes(0x28)
    sh += struct.pack('<10I', 1, 1, 6, 0, toff, len(text), 0, 0, 4, 0)
    sh += struct.pack('<10I', 7, 9, 0, 0, roff, len(rel), 0, 1, 4, 8)
    sh += struct.pack('<10I', 17, 3, 0, 0, soff, len(names), 0, 0, 1, 0)
    return header + ph + text + rel + names + sh


good = irx([(0, 0x7CC4), (1, 0x9114), (1, 0x0000), (1, 0x0FF0)])  # 0x7CC4, 0x9114, 0x10000 (gp), 0x10FF0 (gp)
bad = irx([(0, 0x7CC4), (0, 0x8294), (2, 0x9000)])                # -0x7D6C and 0x19000: both outside 0..0x9000

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'good.irx').write_bytes(good)
    (tmp / 'bad.irx').write_bytes(bad)
    (tmp / 'embedded.elf').write_bytes(b'\x7fELF' + bytes(60) + good + bytes(8) + bad)
    (tmp / 'notes.txt').write_text('not an ELF')

    found = tool.scan_file(tmp / 'good.irx')
    assert len(found) == 1 and found[0][3] == 4 and found[0][4] == [], found
    found = tool.scan_file(tmp / 'bad.irx')
    assert [(o, a) for _, o, a in found[0][4]] == [(8, -0x7D6C), (16, 0x19000)], found
    found = tool.scan_file(tmp / 'embedded.elf')
    assert [len(f[4]) for f in found] == [0, 2], found
    assert tool.scan_file(tmp / 'notes.txt') == []

    assert tool.main([str(tmp / 'good.irx')]) == 0
    assert tool.main([str(tmp / 'bad.irx')]) == 1
    assert tool.main([str(tmp)]) == 1
    assert tool.main([str(tmp / 'notes.txt')]) == 2

dev9 = (root / 'modules/iopcore/cdvdman/dev9.c').read_text()
heap = (root / 'modules/iopcore/cdvdman/heapdiag.c').read_text()
for source, names in ((dev9, ('int dev9_intr_dispatch(int flag)', 'int pcmcia_intr(void *unused)', 'int expbay_intr(void *unused)')),
                      (heap, ('void *hook_AllocSysMemory(int mode, int size, void *ptr)', 'int hook_FreeSysMemory(void *ptr)'))):
    for name in names:
        assert ('\n' + name + '\n{') in source, name + ' must be defined'
        assert ('static ' + name) not in source, name + ' must not be static (its address is stored at run time)'
print('check_irx_addresses: wrong pairs are reported, right pairs and the gp pointer are not')
