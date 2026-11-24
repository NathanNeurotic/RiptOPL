"""Every loader has its own memory-card folder, and the first start there keeps the settings.

Every loader's folder and PSU is "APP_" plus its ELF name: APP_RIPTOPL, APP_RIPTOPL-RA,
APP_RIPTOPLITE-USB, APP_RIPTOPLITE-RA-USB. A RiptOPLite loader started from its own folder for the
first time has no settings there, so it reads the full loader's folder beside it (APP_RIPTOPL, or
APP_RIPTOPL-RA for an RA build) once, and saves beside itself. This compiles the real variantSiblingHome()
(src/app/settingsio.cpp) on the host and checks the folder it picks, then checks that the recovery reads
it before the device's legacy homes and that nothing in that path writes or deletes.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "tools"))
src = (root / "src/app/settingsio.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")

import lite_devices  # noqa: E402
import publish_lite  # noqa: E402


def function_text(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise ValueError(signature)


helper = function_text(src, "static int variantSiblingHome(")
CASES = [
    ("mc0:/APP_RIPTOPLITE-USB", "mc0:/APP_RIPTOPL", "mc0:/APP_RIPTOPL-RA"),
    ("mc1:/APP_RIPTOPLITE-RA-MMCE/", "mc1:/APP_RIPTOPL", "mc1:/APP_RIPTOPL-RA"),
    ("mass0:/APPS/APP_RIPTOPLITE-HDD", "mass0:/APPS/APP_RIPTOPL", "mass0:/APPS/APP_RIPTOPL-RA"),
    ("mmce0:APP_RIPTOPLITE-SMB", "mmce0:APP_RIPTOPL", "mmce0:APP_RIPTOPL-RA"),
    ("mc0:/APP_RIPTOPL", "", ""),          # the full loader's own folder: no sibling read
    ("mc0:/APP_RIPTOPL-RA", "mc0:/APP_RIPTOPL", ""),  # RA's own folder: the RA build reads nothing
    ("mass0:/APPS/APP_RIPTOPL-PADEMU0", "mass0:/APPS/APP_RIPTOPL", "mass0:/APPS/APP_RIPTOPL-RA"),
    ("mc0:/APP_RIPTOPL-RA-PADEMU0", "mc0:/APP_RIPTOPL", "mc0:/APP_RIPTOPL-RA"),
    ("mc0:/APPS_RIPTOPL", "", ""),         # not one of our folder names
    ("mc0:/APP_RiptOPL-X", "", ""),
    ("mass0:/APPS", "", ""),               # a loader kept loose in APPS
    ("mc0:/", "", ""),
    ("mc0:", "", ""),
    ("", "", ""),
]
program = """
#include <stdio.h>
#include <string.h>
char gBootDir[256];
%s
int main(void)
{
    static const char *dirs[] = {%s};
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        char home[256] = "";
        snprintf(gBootDir, sizeof(gBootDir), "%%s", dirs[i]);
        if (!variantSiblingHome(home, sizeof(home)))
            home[0] = 0;
        printf("%%s\\n", home);
    }
    return 0;
}
""" % (helper, ", ".join(f'"{d}"' for d, _, _ in CASES))

failures = []
with tempfile.TemporaryDirectory() as temp:
    for ra, column in ((False, 1), (True, 2)):
        source = Path(temp) / "t.c"
        source.write_text(program, encoding="utf-8")
        exe = Path(temp) / ("ra" if ra else "std")
        flags = ["-DRETROACHIEVEMENTS"] if ra else []
        subprocess.run(["cc", "-std=c99", "-Wall", "-Werror", *flags, "-o", str(exe), str(source)], check=True)
        got = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.split("\n")
        for (boot, *want), line in zip(CASES, got):
            if line != want[column - 1]:
                failures.append(f"{'RA' if ra else 'standard'} boot {boot!r}: got {line!r}, want {want[column - 1]!r}")

# Small buffer: a sibling that does not fit is refused, never cut short.
with tempfile.TemporaryDirectory() as temp:
    source = Path(temp) / "t.c"
    source.write_text("#include <stdio.h>\n#include <string.h>\nchar gBootDir[256];\n" + helper +
                      '\nint main(void){char h[12];snprintf(gBootDir,256,"mc0:/APP_RIPTOPLITE-USB");'
                      'return variantSiblingHome(h,sizeof(h));}\n', encoding="utf-8")
    exe = Path(temp) / "small"
    subprocess.run(["cc", "-std=c99", "-Wall", "-Werror", "-o", str(exe), str(source)], check=True)
    if subprocess.run([str(exe)]).returncode != 0:
        failures.append("a sibling path longer than the buffer must be refused")

recovery = function_text(src, "static int tryKnownBootConfigRecovery(")
if recovery.index("variantSiblingHome(") > recovery.index('"%s/OPL"'):
    failures.append("the full loader's folder must be read before <device>:/OPL")
for word in ("remove(", "unlink(", "rmdir(", "mkdir(", "configWrite", "rename("):
    if word in recovery or word in helper:
        failures.append(f"the settings recovery must not write or delete ({word})")

# Every loader's folder is APP_ + its ELF name, and every PSU name a memory card accepts.
for device in lite_devices.DEVICES:
    for ra in (False, True):
        elf = lite_devices.elf_name(device, ra)
        folder = publish_lite.app_folder(elf)
        if folder != "APP_" + elf[:-4] or not folder.isupper() or len(folder) > 31:
            failures.append(f"bad folder for {elf}: {folder}")
        if folder + ".psu" not in publish_lite.expected_assets():
            failures.append(f"{folder}.psu is not published")

if failures:
    print("\n".join(failures))
    sys.exit(1)
print(f"lite folders: {len(CASES) * 2} boot folders, recovery order and {len(lite_devices.DEVICES) * 2} PSU names checked")
