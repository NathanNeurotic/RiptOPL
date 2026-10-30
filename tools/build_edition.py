#!/usr/bin/env python3
"""Build one release edition with ps2build; GCC/LLVM are compiler checks, not editions."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import yaml

from build import build
from build_configuration import CONFIGURATIONS, apply_configuration
from install_ports import LIBRARIES

ROOT = Path(__file__).resolve().parents[1]
EDITIONS = {
    "korium": {"title": "Korium", "tag": "rolling-korium", "theme": "korium", "ra": False},
    "ra": {"title": "RetroAchievements", "tag": "rolling-ra", "theme": "korium", "ra": True},
    "incebtion": {"title": "IncebtIon", "tag": "rolling-incebtion", "theme": "incebtion", "ra": False},
    # The original's RA release carries a kit per theme: this one ships inside rolling-ra ("into").
    "ra_incebtion": {"title": "RetroAchievements-IncebtIon", "tag": "rolling-ra", "theme": "incebtion", "ra": True,
                     "into": "ra", "label": "IncebtIon"},
}


def add(target, field, *values):
    target.setdefault(field, []).extend(v for v in values if v not in target.get(field, []))


def definition(edition, configuration="standard", toolchain="gcc"):
    options = EDITIONS[edition]
    config = yaml.safe_load((ROOT / "ps2.yaml").read_text(encoding="utf-8"))
    # YAML aliases share lists. Mutating one target must not change another.
    config["targets"] = [copy.deepcopy(t) for t in config["targets"]]
    targets = {t["name"]: t for t in config["targets"]}
    main, core = targets["riptopl"], targets["eecore"]
    if options["theme"] == "korium":
        add(main, "defines", "BUILTIN_KORIUM")
    if options["ra"]:
        for target in (main, core):
            add(target, "defines", "RETROACHIEVEMENTS")
        # The in-game menu does not fit beside RA in ee_core's ram84 (about 5.7 KB short): RA editions run it as
        # igrovl.elf, an overlay the launcher puts at the top of the module storage only with In-Game Menu on
        # (ee_core/include/igrovl.h). ee_core keeps the stub that calls it.
        for target in (main, core):
            add(target, "defines", "IGR_OVERLAY")
        core["sources"] = [v for v in core["sources"] if v not in ("ee_core/src/igrmenu.c", "ee_core/src/igrmbox.c")]
        add(core, "sources", "ee_core/src/igrovl_stub.c")
        main["embed_irx"].insert(main["embed_irx"].index("eecore") + 1, "igrovl")
        add(main, "sources", "src/platform/md5.c", *[
            f"src/sources/{name}.cpp" for name in ("rawatch", "rahash", "ranet", "rabadge", "discsupport", "achievements",
                                                   "rahunt", "rapopslaunch")])
        # USB PS1 under POPStarter: rapops is written as POPS/MODULE_9.IRX and starts the network
        # modules the loader copies into it, then rapull, which reads the game's RAM from the IOP.
        headers = targets["raudp"]["headers"]
        for name in ("rapull", "rapops"):
            config["targets"].append({"name": name, "type": "iop", "sources": [f"modules/network/{name}/{name}.c"],
                                      "include_dirs": [f"modules/network/{name}/", "modules/network/common",
                                                       "modules/iopcore/common"],
                                      "headers": headers, "imports_lst": f"modules/network/{name}/imports.lst"})
        add(main, "embed_irx", "rapull", "rapops")
        add(core, "sources", "ee_core/src/ra.c", "ee_core/src/ra_overlay.c", "ee_core/src/ra_pulse.c")
        # mbedTLS 3.6.7 (third_party/mbedtls, Apache-2.0) for the HTTPS game-ID lookup. RA only:
        # the other editions must not contain it (test_mbedtls_vendor.py).
        # The compile line runs through a shell, so the define keeps its quotes escaped. RiptOPL's
        # own TLS sources name the configuration themselves (include/ratls.h).
        tls = ["third_party/mbedtls/include", "third_party/mbedtls"]
        config["targets"].append({"name": "mbedtls", "type": "ee_lib", "sources": ["third_party/mbedtls/library/*.c"],
                                  "include_dirs": tls, "defines": [r'MBEDTLS_CONFIG_FILE=\"riptopl_mbedtls_config.h\"']})
        add(main, "sources", "src/platform/ratls.c", "src/sources/ratls_ps2.cpp")
        add(main, "include_dirs", *tls)
        add(main, "libs", "mbedtls")
        add(main, "embed_irx", "raudp")
        add(targets["SMSTCPIP"], "defines", "INGAME_UDP")
        # raudp imports the in-game SMAP driver's transmit and notice exports, and the
        # transmit mutex. Only RA carries the table: it changes the module's link layout.
        add(targets["smap_ingame"], "defines", "RETROACHIEVEMENTS")
        targets["smap_ingame"]["exports_tab"] = "modules/network/smap-ingame/exports.tab"
        # sdk-core v2026.10.13 supplies POSIX sockets through libc's fdman.
        # Keep descriptor ownership/non-blocking callbacks in the SDK, not a second archive.
        for name in ("bdm_cdvdman", "mmce_cdvdman"):
            add(targets[name], "defines", "__USE_DEV9")
            add(targets[name], "sources", "modules/iopcore/cdvdman/dev9.c")
    apply_configuration(config, configuration)
    # IGS makes LLVM -Os leave only 2,712 bytes for the core's stack. -Oz leaves
    # 6,040 with the same 3 KB linker guard and fixed memory regions; GCC stays -Os.
    # The in-game menu's silent pause and held frame leave LLVM -Os 2,136 bytes, so it takes -Oz too.
    # LLVM builds are CI's compiler check only: every released ee_core is GCC -Os.
    if toolchain == "llvm" and ("IGS" in core["defines"] or "IGR_MENU" in core["defines"]):
        core["opt"] = "-Oz"
    if "IGR_OVERLAY" in core["defines"]:
        # Built from the finished core (configuration defines, -Oz): the overlay shares its headers and config layout.
        config["targets"].insert(config["targets"].index(core) + 1, {
            "name": "igrovl", "type": "ee",
            "sources": ["ee_core/igrovl/igrovl_main.c", "ee_core/src/igrmenu.c", "ee_core/src/igrmbox.c"],
            "include_dirs": list(core["include_dirs"]), "headers": list(core["headers"]),
            "defines": [*core["defines"], "IGR_OVERLAY_SIDE"], "libs": ["gcc"],
            "linkfile": "ee_core/igrovl/linkfile", "opt": core["opt"],
            "cflags": ["-G0", "-ffunction-sections", "-fdata-sections"],
            "ldflags": ["-nostartfiles", "-nostdlib", "-Wl,--gc-sections", "-Wl,-Map,igrovl.map"]})
    return config


def build_paths(edition, toolchain, configuration):
    if configuration == "standard":
        return Path("build/editions") / f"{edition}-{toolchain}", ROOT / "dist/editions" / edition
    # Compiler/configuration checks cannot overwrite a publishable edition stage.
    name = f"{edition}-{configuration}-{toolchain}"
    return Path("build/configurations") / name, ROOT / "dist/configurations" / name


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("edition", choices=EDITIONS)
    parser.add_argument("--toolchain", choices=("gcc", "llvm"), default="gcc")
    parser.add_argument("--configuration", choices=CONFIGURATIONS, default="standard")
    args = parser.parse_args()
    os.chdir(ROOT)
    source = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain"))
    options = EDITIONS[args.edition]
    os.environ["RIPTOPL_THEME"] = options["theme"]
    os.environ["RIPTOPL_RA"] = "1" if options["ra"] else "0"
    suffix = "" if args.configuration == "standard" else f"-{args.configuration.upper()}"
    os.environ["LOCALVERSION"] = f"PS2BUILD-{options['title']}-{args.toolchain.upper()}{suffix}"
    # Each edition regenerates blobs, so sequential local builds cannot inherit
    # a previous edition's RA mark, theme graphics, or displayed version.
    subprocess.run([sys.executable, "tools/assets.py", "embed"], check=True)
    subprocess.run([sys.executable, "tools/assets.py", "version"], check=True)
    config = definition(args.edition, args.configuration, args.toolchain)
    canonical = yaml.safe_dump(config, sort_keys=False)
    output, stage = build_paths(args.edition, args.toolchain, args.configuration)
    result = build(args.toolchain, str(output), config)
    if result:
        return result
    elf = output / "bin/riptopl-packed.elf"
    if not elf.is_file() or elf.read_bytes()[:4] != b"\x7fELF":
        raise ValueError(f"invalid packed ELF: {elf}")
    if git("rev-parse", "HEAD") != source or bool(git("status", "--porcelain")) != dirty:
        raise ValueError("source checkout changed during build")
    stage.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(elf, stage / "RIPTOPL.ELF")
    # The i.LINK pair the ELF embeds also replaces the Neutrino bundle's copies (package_edition.py).
    shutil.copyfile(output / "bin/IEEE1394_bd_mini_fork.irx", stage / "IEEE1394_bd_mini.irx")
    shutil.copyfile(output / "bin/iLinkman_fork.irx", stage / "iLinkman.irx")
    sdk = Path(os.environ.get("PS2BUILD_HOME", shutil.which("ps2build") or "")).resolve()
    if sdk.is_file():
        sdk = sdk.parent
    world = sdk / ("packages-llvm" if args.toolchain == "llvm" else "packages") / "world"
    manifest = {
        "repository": "NathanNeurotic/RiptOPL", "source_commit": source, "dirty": dirty,
        "edition": args.edition, **options, "sdk": "ps2build", "compiler": args.toolchain,
        "configuration": args.configuration,
        "ps2build_version": subprocess.check_output(["ps2build", "--version"], text=True).strip(),
        "components": yaml.safe_load((sdk / "versions.yaml").read_text()) if (sdk / "versions.yaml").exists() else {},
        # install_ports.py may keep a cached ogg/vorbis through a host outage; record which one was linked.
        "ports": {package: hashlib.sha256(b"".join((world / package / "lib" / lib).read_bytes() for lib in libraries)).hexdigest()
                  for package, libraries in LIBRARIES.items() if (world / package).is_dir()},
        "definition_sha256": hashlib.sha256(canonical.encode()).hexdigest(),
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "ilink_bd_sha256": hashlib.sha256((stage / "IEEE1394_bd_mini.irx").read_bytes()).hexdigest(),
        "ilink_manager_sha256": hashlib.sha256((stage / "iLinkman.irx").read_bytes()).hexdigest(),
        "version": (ROOT / "asm/opl_version.h").read_text().strip(),
        "run_url": os.environ.get("GITHUB_SERVER_URL", "https://github.com") + "/" +
            os.environ.get("GITHUB_REPOSITORY", "NathanNeurotic/RiptOPL") + "/actions/runs/" + os.environ.get("GITHUB_RUN_ID", "local"),
    }
    (stage / "BUILD-MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (stage / "ps2.resolved.yaml").write_text(canonical, encoding="utf-8")
    if args.configuration != "standard":
        shutil.copyfile(output / "eecore.map", stage / "eecore.map")
        for filename in ("LICENSE", "CREDITS"):
            shutil.copyfile(ROOT / filename, stage / filename)
        for component in (["ps2sdk-ppctty"] if args.configuration.endswith("_uart") else []) + (["ps2sdk-socket"] if options["ra"] else []):
            destination = stage / "licenses" / component
            destination.mkdir(parents=True, exist_ok=True)
            for filename in ("LICENSE", "UPSTREAM"):
                shutil.copyfile(ROOT / "third_party" / component / filename, destination / filename)
    print(f"Built {options['title']}: {manifest['elf_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
