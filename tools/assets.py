#!/usr/bin/env python3
"""Asset/pre-build steps that used to live in the Makefile. ps2build handles all code.

    tools/assets.py all        fetch + lang + embed + version   (run before `ps2build build`)
    tools/assets.py fetch      lng_src (translations) and lwNBD, pinned by tools/download_*.sh
    tools/assets.py lang       .lng files, src/core/lang_internal.c, include/lang_autogen.h
    tools/assets.py embed      asm/*.c from gfx/audio/misc/font/IOPRP.img via bin2c
    tools/assets.py version    asm/opl_version.h (force-included by ps2.yaml)
    tools/assets.py release    zip RIPTOPL-<ver>.ZIP from the packed ELF (after `ps2build build`)
    tools/assets.py version-string   print OPL_VERSION

Environment: REVISION, RELEASE_CHANNEL, LOCALVERSION (same meaning as the old Makefile),
PS2BUILD_HOME (default: the directory containing ps2build on PATH, then
~/.local/share/ps2build). Windows fetch steps use Git for Windows' shell.
"""
import os
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

VERSION, SUBVERSION, PATCHLEVEL, EXTRAVERSION = 1, 2, 0, "Beta"

_ps2build = shutil.which("ps2build")
PS2BUILD_HOME = os.environ.get(
    "PS2BUILD_HOME",
    os.path.dirname(os.path.realpath(_ps2build)) if _ps2build else os.path.expanduser("~/.local/share/ps2build"),
)
BIN2C = shutil.which("bin2c", path=os.path.join(PS2BUILD_HOME, "tools")) or os.path.join(PS2BUILD_HOME, "tools", "bin2c")

ASM_DIR = "asm"
GEN_DIR = ASM_DIR  # opl_version.h lives next to the generated sources (ps2.yaml include_dirs: [asm])

TRANSLATIONS = """Albanian Arabic Bulgarian Cebuano Croatian Czech Danish Dutch Filipino French
Galician German Greek Hungarian Indonesian Italian Japanese Korean Laotian Persian Polish Portuguese
Portuguese_BR Romana Russian Ryukyuan SChinese Serbian Spanish Swedish TChinese Turkish Vietnamese""".split()

PNG_ASSETS = """load0 load1 load2 load3 load4 load5 load6 load7 usb usb_bd ilk_bd
m4s_bd hdd_bd mmce hdd eth udp_bd udp_fs app cross triangle circle square select start left right
cover disc screen incebtion ip coverapp missing screens
ELF HDL ISO VCD ZSO UL APPS CD DVD Aspect_s Aspect_w Aspect_w1
Aspect_w2 Rating_0
Rating_1 Rating_2 Rating_3 Rating_4 Rating_5 Scan_240p Scan_240p1 Scan_480i Scan_480p
Scan_480p1 Scan_480p2 Scan_480p3 Scan_480p4 Scan_480p5 Scan_576i Scan_576p Scan_720p
Scan_1080i Scan_1080i2 Scan_1080p Vmode_multi Vmode_ntsc Vmode_pal logo
logo0 logo1 logo2 logo3 case
Index_0 Index_1 Index_2 Index_3 Index_4 Index2_0 Index2_1 Index3_0 Index3_1 Index3_2
Index4_0 Index4_1 Index4_2 Index4_3 fav fav_mark R3 L3 L1 R1 PS1 PS2 case_overlay psbbn_bg techwritescode
grid_devices""".split()
# Button glyph sets (RIPPS2 build 90, akilluminati47): Standard and White, for Settings > Interface > Button Glyphs
PNG_ASSETS += ["glyph_%s_%s" % (s, n) for s in ("standard", "white")
               for n in "cross circle square triangle start select L1 R1 L3 R3 left right".split()]

AUDIO = "boot cancel confirm cursor message transition bd_connect bd_disconnect".split()

# (output basename, source file, symbol). Symbols are what src/ declares as extern.
MISC = [
    ("icon_sys", "misc/icon.sys", "icon_sys"),
    ("save_icn", "misc/list.icn", "save_icn"),  # one blob, three destination names (src/platform/util.c)
    ("icon_sys_A", "misc/icon_A.sys", "icon_sys_A"),
    ("icon_sys_J", "misc/icon_J.sys", "icon_sys_J"),
    ("icon_sys_C", "misc/icon_C.sys", "icon_sys_C"),
    ("conf_theme_OPL", "misc/conf_theme_OPL.cfg", "conf_theme_OPL_cfg"),
    ("theme_coverflow", "misc/theme_coverflow.cfg", "theme_coverflow_cfg"),
    ("theme_psbbn_flow", "misc/theme_psbbn_flow.cfg", "theme_psbbn_flow_cfg"),
    ("theme_grid", "misc/theme_grid.cfg", "theme_grid_cfg"),
]


def run(*cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, **kw)


def out(*cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


# ------------------------------------------------------------------ fetch
def fetch():
    shell = []
    if os.name == "nt":
        # PATH may resolve bash.exe to the WSL launcher. Use the shell belonging
        # to Git instead; these scripts only need Git and POSIX shell builtins.
        git_shell = out("git", "var", "GIT_SHELL_PATH")
        if not os.path.isfile(git_shell):
            sys.exit("asset downloads on Windows require Git for Windows (git var GIT_SHELL_PATH)")
        shell = [git_shell]
    run(*shell, "./tools/download_lng.sh")
    run(*shell, "./tools/download_lwNBD.sh")


# ------------------------------------------------------------------ lang
def lang():
    lc = [sys.executable, "tools/lang_compiler.py"]
    base = "lng/tmpl/_base.yml"
    os.makedirs("lng", exist_ok=True)
    english_yml = "lng_src/English.yml"
    run(*lc, "--make_template_yml", "--base", base, english_yml)
    run(*lc, "--make_lng", "--base", base, "--translation", english_yml, "lng_src/lang_English.lng")
    for t in TRANSLATIONS:
        yml = f"lng_src/{t}.yml"
        run(*lc, "--update_translation_yml", "--base", base, "--translation", yml)
        overlay = f"lng/fork/{t}.yml"
        if os.path.isfile(overlay):
            run(*lc, "--overlay_translation_yml", "--base", base, "--translation", yml, "--overlay", overlay)
        run(*lc, "--make_lng", "--base", base, "--translation", yml, f"lng/lang_{t}.lng")
    run(*lc, "--make_source", "--base", base, "src/core/lang_internal.c")
    run(*lc, "--make_header", "--base", base, "include/lang_autogen.h")


# ------------------------------------------------------------------ embed
def bin2c(src, name, symbol):
    if not os.path.isfile(src):
        sys.exit(f"missing asset: {src}")
    run(BIN2C, src, os.path.join(ASM_DIR, name + ".c"), symbol)


def embed():
    os.makedirs(ASM_DIR, exist_ok=True)
    for stale in os.listdir(ASM_DIR):  # asm/*.c is globbed by ps2.yaml: drop leftovers
        if stale.endswith(".c"):
            os.remove(os.path.join(ASM_DIR, stale))
    theme = os.environ.get("RIPTOPL_THEME", "incebtion")
    if theme not in ("korium", "incebtion"):
        sys.exit(f"unknown RIPTOPL_THEME: {theme}")
    theme_dir = None
    if theme == "korium":
        vendor = Path(ROOT) / "third_party/korium"
        archive = vendor / "assets.zip"
        manifest = json.loads((vendor / "UPSTREAM.json").read_text(encoding="utf-8"))
        if hashlib.sha256(archive.read_bytes()).hexdigest() != manifest["archive_sha256"]:
            sys.exit("Korium asset archive digest mismatch")
        theme_dir = Path(ROOT) / "build/theme-assets/korium"
        with zipfile.ZipFile(archive) as package:
            # Do not trust archive paths even when refreshing a vendored pin.
            for name, digest in manifest["files"].items():
                destination = (theme_dir / name).resolve()
                if not destination.is_relative_to(theme_dir.resolve()):
                    sys.exit(f"unsafe Korium asset path: {name}")
                data = package.read(name)
                if hashlib.sha256(data).hexdigest() != digest:
                    sys.exit(f"Korium asset digest mismatch: {name}")
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
    pngs = list(PNG_ASSETS)
    if theme_dir:
        # inceBtion's app placeholder and foliage layer: Korium maps neither (src/gfx/textures.cpp).
        pngs = [p for p in pngs if p not in ("coverapp", "case_overlay")]
        pngs += ["apps_case", "case_ps1", "disc_ps1", "Players_1", "Players_2", "Players_3", "Players_4"]
    if os.environ.get("RIPTOPL_RA") == "1":
        pngs += ["ra_mark"]
        # The HTTPS lookup's trust anchors (src/sources/ratls_ps2.cpp): ratls_root_<name>.
        for der in sorted((Path(ROOT) / "third_party/ca-roots").glob("*.der")):
            name = "ratls_root_" + der.stem.replace("-", "")
            bin2c(str(der), name, name)
    for a in pngs:
        name = {"incebtion": "background", "ip": "background_info"}.get(a, a) if theme_dir else a
        themed = theme_dir / f"gfx/{name}.png" if theme_dir else None
        if themed and not themed.is_file() and name.startswith("Index") and "_" in name and name[5] != "_":
            # Korium ships five-dot BdmIndex pictures only: dot <p> of <n> is its Index_<p>, never inceBtion's.
            themed = theme_dir / f"gfx/Index_{name.split('_')[1]}.png"
        source = str(themed) if themed and themed.is_file() else f"gfx/{a}.png"
        bin2c(source, f"{a}_png", f"{a}_png")
    bin2c("third_party/fonts/PoeVeticaNew.ttf", "poeveticanew", "poeveticanew_raw")
    for a in AUDIO:
        bin2c(f"audio/{a}.adp", a, f"{a}_adp")
    for name, src, sym in MISC:
        if theme_dir and name in ("conf_theme_OPL", "theme_coverflow"):
            src = str(theme_dir / src)
        bin2c(src, name, sym)
    bin2c("modules/iopcore/IOPRP.img", "IOPRP_img", "IOPRP_img")


# ------------------------------------------------------------------ version
def opl_version():
    dirty = ""
    if subprocess.run(["git", "diff", "--quiet"]).returncode == 1 or not os.path.exists(".git"):
        dirty = "-dirty"
    tag = out("git", "describe", "--exact-match", "--tags", "--match", "v*")
    local = os.environ.get("LOCALVERSION", "")
    local = f"-{local}" if local else ""
    if tag and tag != "latest":
        return f"{tag}{dirty}{local}"
    # REVISION is the monotonic ordinal (HEAD-based); the hash anchors to the last commit that
    # touched anything the build consumes (see the exclusion list, copied from the old Makefile).
    excl = [".github", "agent-file-drop", "frame_builds", "notes", "obj", ".claude", ".agents",
            ".codex", ".codex-tmp-nhddl", ".codex-tmp-wle-r3z"]
    anchor = out("git", "log", "-1", "--format=%H", "--", ".", *[f":(exclude){e}" for e in excl])
    rev = os.environ.get("REVISION") or str(int(out("git", "rev-list", "--count", "HEAD") or "0") + 2)
    git_hash = out("git", "rev-parse", "--short=7", anchor or "HEAD")
    channel = os.environ.get("RELEASE_CHANNEL", "")
    v = f"v{VERSION}.{SUBVERSION}.{PATCHLEVEL}-{EXTRAVERSION}-{rev}"
    if channel:
        v += f"-{channel}"
    if git_hash:
        v += f"-{git_hash}"
    return v + dirty + local


def version():
    os.makedirs(GEN_DIR, exist_ok=True)
    v = opl_version()
    path = os.path.join(GEN_DIR, "opl_version.h")
    new = f'#define OPL_VERSION "{v}"\n'
    if not os.path.isfile(path) or open(path).read() != new:  # keep mtime stable if unchanged
        open(path, "w").write(new)
    print("OPL_VERSION", v)


# ------------------------------------------------------------------ release
def release():
    v = opl_version()
    packed = os.path.join("build", "bin", "riptopl-packed.elf")
    if not os.path.isfile(packed):
        sys.exit(f"{packed} missing: run `ps2build build` first")
    elf = f"RIPTOPL-{v}.ELF"
    shutil.copyfile(packed, elf)
    run("sh", "tools/make_changelog.sh")
    shutil.copyfile("/tmp/DETAILED_CHANGELOG", "DETAILED_CHANGELOG")
    run("zip", "-r", f"RIPTOPL-{v}.ZIP", elf, "DETAILED_CHANGELOG", "CREDITS", "LICENSE", "README.md")
    print(f"Package Complete: RIPTOPL-{v}.ZIP")


COMMANDS = {"fetch": fetch, "lang": lang, "embed": embed, "version": version, "release": release,
            "version-string": lambda: print(opl_version())}

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "all"
    if cmd == "all":
        for c in (fetch, lang, embed, version):
            c()
    elif cmd in COMMANDS:
        COMMANDS[cmd]()
    else:
        sys.exit(__doc__)
