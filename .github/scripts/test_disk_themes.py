"""Checks the example disk themes under themes/thm_*/ against the rules in docs/THEME_ENGINE.md.

For every theme: the cfg parses, each block family is numbered without a gap, every picture a block names
exists (in the theme folder or as a built-in gfx/ picture), pictures are 8-bit palette PNGs of at most
256x256 (so they stay light in texture memory), and the whole folder stays under 64 KiB."""
from pathlib import Path
import re
import struct
import sys

root = Path(__file__).resolve().parents[2]
themes = sorted((root / 'themes').glob('thm_*'))
assert themes, 'no themes/thm_* folder'
builtin = {p.stem for p in (root / 'gfx').glob('*.png')} | {'cover', 'coverapp', 'disc', 'screen', 'screens', 'case', 'case_overlay', 'missing'}
image_keys = ('default', 'overlay', 'overlay2', 'alt')
errors = []

for theme in themes:
    cfg = theme / 'conf_theme.cfg'
    if not cfg.is_file():
        errors.append(f'{theme.name}: no conf_theme.cfg')
        continue
    blocks = {}
    current = None
    for n, raw in enumerate(cfg.read_text(encoding='utf-8').splitlines(), 1):
        line = raw.split(';')[0].rstrip()
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        m = re.fullmatch(r'([A-Za-z0-9_.]+):', line.strip())
        if m:
            current = m.group(1)
            blocks[current] = {}
            continue
        if '=' not in line:
            errors.append(f'{theme.name}:{n}: not key=value: {raw!r}')
            continue
        k, v = (s.strip() for s in line.strip().split('=', 1))
        if line[0] in ' \t' and current:
            blocks[current][k] = v
        else:
            current = None
    # contiguous numbering per family prefix (main0, main1, ... / appsMain..., info...)
    fams = {}
    for name in blocks:
        m = re.fullmatch(r'(?:[a-z]+\.)?([A-Za-z]+?)(\d+)', name)
        if m:
            fams.setdefault(m.group(1), []).append(int(m.group(2)))
    for fam in ('main', 'info'):
        nums = sorted(fams.get(fam, []))
        if nums != list(range(len(nums))):
            errors.append(f'{theme.name}: {fam}N blocks are not 0..N-1 without a gap: {nums}')
    for name, props in blocks.items():
        if name.startswith(('main', 'info')) and name[4:].isdigit() or name[:4] in ('main', 'info'):
            if 'type' not in props and 'type_desc' not in props:
                errors.append(f'{theme.name}: block {name} has no type')
        for key in image_keys:
            ref = props.get(key)
            if ref and not (theme / f'{ref}.png').is_file() and ref not in builtin:
                errors.append(f'{theme.name}: block {name} {key}={ref} is neither in the folder nor built in')
    if not any(p.get('type') == 'ItemsList' for p in blocks.values()):
        errors.append(f'{theme.name}: needs one plain ItemsList')
    total = 0
    for f in theme.rglob('*'):
        if not f.is_file():
            continue
        total += f.stat().st_size
        if f.suffix.lower() == '.png':
            h = f.read_bytes()[:33]
            w, ht, depth, ctype = struct.unpack('>IIBB', h[16:26])
            if ctype != 3 or depth != 8:
                errors.append(f'{theme.name}/{f.name}: not an 8-bit palette PNG')
            if w > 256 or ht > 256:
                errors.append(f'{theme.name}/{f.name}: {w}x{ht} is over 256x256')
    if total > 64 * 1024:
        errors.append(f'{theme.name}: folder is {total} bytes, over 64 KiB')

if errors:
    print('\n'.join(errors))
    sys.exit(1)
print(f'{len(themes)} disk theme(s) ok')
