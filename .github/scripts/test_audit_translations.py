"""tools/audit_translations.py --strict passes on the shipped overlays, and catches a bad overlay
string both on a fresh checkout and after the build has merged the overlay into lng_src/."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import yaml

root = Path(__file__).resolve().parents[2]
tool = root / 'tools/audit_translations.py'


def strict(tree):
    run = subprocess.run([sys.executable, str(tool), '--strict', '--root', str(tree)], capture_output=True, text=True)
    return run.returncode, run.stdout


with tempfile.TemporaryDirectory(prefix='lngaudit-') as temp:
    tree = Path(temp)
    (tree / 'tools').mkdir()
    shutil.copy(root / 'tools/assets.py', tree / 'tools')
    shutil.copytree(root / 'lng/tmpl', tree / 'lng/tmpl')
    shutil.copytree(root / 'lng/fork', tree / 'lng/fork')
    rc, out = strict(tree)
    assert rc == 0, out

    overlay = tree / 'lng/fork/Swedish.yml'
    data = yaml.safe_load(overlay.read_text(encoding='utf-8'))
    data['translations']['SETTINGS_SAVED'] = 'Inställningar sparade %d'  # English: "Settings saved to %s"
    overlay.write_text(yaml.safe_dump(data, allow_unicode=True), encoding='utf-8')
    rc, out = strict(tree)
    assert rc == 1 and 'Swedish/SETTINGS_SAVED (overlay)' in out, out

    # The build writes the overlay into lng_src/; that must not launder it into a community warning.
    (tree / 'lng_src').mkdir()
    shutil.copy(overlay, tree / 'lng_src/Swedish.yml')
    rc, out = strict(tree)
    assert rc == 1 and 'Swedish/SETTINGS_SAVED (overlay)' in out, out

    # A community translation that differs from the overlay is the community's: a warning only.
    data['translations']['SETTINGS_SAVED'] = 'Inställningar sparade till %s'
    overlay.write_text(yaml.safe_dump(data, allow_unicode=True), encoding='utf-8')
    rc, out = strict(tree)
    assert rc == 0 and 'WARNING: Swedish/SETTINGS_SAVED (upstream)' in out, out

    # overrides: replace upstream at build time, so a bad one is ours: an error, even over upstream.
    data['overrides'] = {'SETTINGS_SAVED': 'Inställningar sparade %d'}
    overlay.write_text(yaml.safe_dump(data, allow_unicode=True), encoding='utf-8')
    rc, out = strict(tree)
    assert rc == 1 and 'Swedish/SETTINGS_SAVED (overlay)' in out, out
print('audit_translations: overlay errors fail --strict before and after the build merges them')
