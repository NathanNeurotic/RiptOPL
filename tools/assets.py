#!/usr/bin/env python3
"""Asset/pre-build steps that used to live in the Makefile. ps2build handles all code.

    tools/assets.py all        fetch + lang + embed + version   (run before `ps2build build`)
    tools/assets.py fetch      lng_src (translations) and lwNBD, pinned by tools/download_*.sh
    tools/assets.py lang       .lng files, src/lang_internal.c, include/lang_autogen.h
    tools/assets.py embed      asm/*.c from gfx/audio/misc/font/IOPRP.img via bin2c
    tools/assets.py version    asm/opl_version.h (force-included by ps2.yaml)
    tools/assets.py release    zip RIPTOPL-<ver>.ZIP from the packed ELF (after `ps2build build`)
    tools/assets.py version-string   print OPL_VERSION

Environment: REVISION, RELEASE_CHANNEL, LOCALVERSION (same meaning as the old Makefile),
PS2BUILD_HOME (default ~/.local/share/ps2build).
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

VERSION, SUBVERSION, PATCHLEVEL, EXTRAVERSION = 1, 2, 0, "Beta"

PS2BUILD_HOME = os.environ.get("PS2BUILD_HOME", os.path.expanduser("~/.local/share/ps2build"))
BIN2C = os.path.join(PS2BUILD_HOME, "tools", "bin2c")

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
Index_0 Index_1 Index_2 Index_3 Index_4 fav fav_mark R3 L3 L1 R1 PS1 PS2 case_overlay""".split()

AUDIO = "boot cancel confirm cursor message transition bd_connect bd_disconnect".split()

# (output basename, source file, symbol). Symbols are what src/ declares as extern.
MISC = [
    ("icon_sys", "misc/icon.sys", "icon_sys"),
    ("save_icn", "misc/list.icn", "save_icn"),  # one blob, three destination names (src/util.c)
    ("icon_sys_A", "misc/icon_A.sys", "icon_sys_A"),
    ("icon_sys_J", "misc/icon_J.sys", "icon_sys_J"),
    ("icon_sys_C", "misc/icon_C.sys", "icon_sys_C"),
    ("conf_theme_OPL", "misc/conf_theme_OPL.cfg", "conf_theme_OPL_cfg"),
    ("theme_coverflow", "misc/theme_coverflow.cfg", "theme_coverflow_cfg"),
]


def run(*cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, **kw)


def out(*cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


# ------------------------------------------------------------------ fetch
def fetch():
    run("./tools/download_lng.sh")
    run("./tools/download_lwNBD.sh")


# ------------------------------------------------------------------ lang
def lang():
    lc = ["python3", "tools/lang_compiler.py"]
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
    run(*lc, "--make_source", "--base", base, "src/lang_internal.c")
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
    for a in PNG_ASSETS:
        bin2c(f"gfx/{a}.png", f"{a}_png", f"{a}_png")
    bin2c("third_party/fonts/PoeVeticaNew.ttf", "poeveticanew", "poeveticanew_raw")
    for a in AUDIO:
        bin2c(f"audio/{a}.adp", a, f"{a}_adp")
    for name, src, sym in MISC:
        bin2c(src, name, sym)
    bin2c("modules/iopcore/IOPRP.img", "IOPRP_img", "IOPRP_img")


# ------------------------------------------------------------------ version
def opl_version():
    dirty = ""
    if subprocess.run(["git", "diff", "--quiet"]).returncode == 1 or not os.path.isdir(".git"):
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
