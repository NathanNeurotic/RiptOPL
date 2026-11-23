#!/usr/bin/env python3
"""Build one RiptOPL edition with GCC or LLVM using ps2build."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from build_configuration import CONFIGURATIONS, apply_configuration
from lite_devices import DEVICES as LITE_DEVICES, apply_lite, elf_name
from install_ports import LIBRARIES

ROOT = Path(__file__).resolve().parents[1]
EDITIONS = {
    "riptopl": {"title": "RiptOPL", "tag": "rolling", "theme": "default", "ra": False, "compiler": "llvm"},
    "ra": {"title": "RetroAchievements", "tag": "rolling-ra", "theme": "default", "ra": True, "compiler": "gcc"},
}
# RiptOPLite: one game device's drivers per ELF (tools/lite_devices.py). Built with `lite --device <device>`;
# kept out of EDITIONS because it has no install kit, bundles or configurations -- its channel is only the
# RIPTOPLITE-<DEVICE>.ELF files (publish_editions.py).
# lite_ra is the same over the RetroAchievements edition (RIPTOPLITE-RA-<DEVICE>.ELF): RA on,
# and the in-game network RA needs kept for every device. Both publish into the one rolling-lite channel.
LITE = "lite"
LITE_RA = "lite_ra"
LITES = {
    LITE: {"title": "Lite", "tag": "rolling-lite", "theme": "default", "ra": False, "base": "riptopl", "compiler": "gcc"},
    LITE_RA: {"title": "Lite-RA", "tag": "rolling-lite", "theme": "default", "ra": True, "base": "ra", "compiler": "gcc"},
}


def edition_options(edition):
    return LITES[edition] if edition in LITES else EDITIONS[edition]


def edition_display_name(options):
    return "RiptOPL" if options["title"] == "RiptOPL" else f"RiptOPL {options['title']}"


def kit_archive_name(options):
    return "RIPTOPL.zip" if options["title"] == "RiptOPL" else f"RIPTOPL-{options['title']}.zip"


def update_asset(edition, configuration="standard", device=None, *, unpacked=False):
    if edition in LITES:
        return elf_name(device, edition == LITE_RA)
    stem = "RIPTOPL-RA" if EDITIONS[edition]["ra"] else "RIPTOPL"
    if configuration != "standard":
        stem += "-" + configuration.upper().replace("_", "-")
    return stem + ("-UNPACKED" if unpacked else "") + ".ELF"


def publishes_unpacked(edition, configuration):
    return (edition == "ra" and configuration in ("standard", "pademu0")) or (edition == "riptopl" and configuration == "diag")


def update_channel(edition, configuration="standard", toolchain=None, device=None):
    """Return the channel and packed asset for a published edition/configuration/device."""
    options = edition_options(edition)
    if toolchain is None:
        toolchain = options["compiler"]
    if toolchain != options["compiler"]:
        return None
    if edition in LITES:
        if configuration != "standard" or device not in LITE_DEVICES:
            return None
    elif configuration not in CONFIGURATIONS or (edition == "ra" and configuration not in ("standard", "pademu0")):
        return None
    return options["tag"], update_asset(edition, configuration, device)


def add(target, field, *values):
    target.setdefault(field, []).extend(v for v in values if v not in target.get(field, []))


def definition(edition, configuration="standard", toolchain=None, device=None):
    import yaml

    options = edition_options(edition)
    if toolchain is None:
        toolchain = options["compiler"]
    if edition in LITES and configuration != "standard":
        raise ValueError("the lite edition is built in the standard configuration only")
    config = yaml.safe_load((ROOT / "ps2.yaml").read_text(encoding="utf-8"))
    # YAML aliases share lists. Mutating one target must not change another.
    config["targets"] = [copy.deepcopy(t) for t in config["targets"]]
    targets = {t["name"]: t for t in config["targets"]}
    main, core = targets["riptopl"], targets["eecore"]
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
    channel = update_channel(edition, configuration, toolchain, device)
    if channel is not None:
        tls = ["third_party/mbedtls/include", "third_party/mbedtls"]
        if not options["ra"]:
            # Check for Updates over HTTPS. The RA editions reuse their TLS above, unchanged; the others get
            # the slim TLS 1.2 configuration (third_party/mbedtls/riptopl_mbedtls_slim_config.h).
            slim = r'MBEDTLS_CONFIG_FILE=\"riptopl_mbedtls_slim_config.h\"'
            config["targets"].append({"name": "mbedtls", "type": "ee_lib", "sources": ["third_party/mbedtls/library/*.c"],
                                      "include_dirs": tls, "defines": [slim]})
            add(main, "sources", "src/platform/ratls.c", "src/sources/ratls_ps2.cpp")
            add(main, "include_dirs", *tls)
            add(main, "libs", "mbedtls")
        # selfupdate.cpp stringifies these values, keeping the asset names shared with the publisher.
        add(main, "sources", "src/platform/selfupdate.c", "src/ui/selfupdate.cpp")
        add(main, "defines", "RIPTOPL_UPDATER", f"RIPTOPL_UPDATE_CHANNEL={channel[0]}",
            f"RIPTOPL_UPDATE_ASSET={channel[1]}")
        if publishes_unpacked(edition, configuration):
            add(main, "defines", f"RIPTOPL_UPDATE_RAW_ASSET={update_asset(edition, configuration, unpacked=True)}")
    if edition in LITES:
        apply_lite(config, device, ROOT, options["ra"])
    elif device is not None:
        raise ValueError("--device is for the lite edition only")
    apply_configuration(config, configuration)
    # IGS makes LLVM -Os leave only 2,712 bytes for the core's stack. -Oz leaves
    # 6,040 with the same 3 KB linker guard and fixed memory regions; GCC stays -Os.
    # The in-game menu's silent pause and held frame leave LLVM -Os 2,136 bytes, so it takes -Oz too.
    # LLVM and GCC release channels retain compiler-specific stack-safe flags.
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


def build_paths(edition, toolchain, configuration, device=None):
    if edition in LITES:
        name = (f"ra-{device}" if edition == LITE_RA else device) + ("" if configuration == "standard" else f"-{configuration}")
        return Path("build/lite") / f"{name}-{toolchain}", ROOT / "dist/lite" / f"{name}-{toolchain}"
    if configuration == "standard":
        return Path("build/editions") / f"{edition}-{toolchain}", ROOT / "dist/editions" / edition
    # Compiler/configuration checks cannot overwrite a publishable edition stage.
    name = f"{edition}-{configuration}-{toolchain}"
    return Path("build/configurations") / name, ROOT / "dist/configurations" / name


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main():
    import yaml
    from build import build

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("edition", choices=[*EDITIONS, *LITES])
    parser.add_argument("--toolchain", choices=("gcc", "llvm"), default=None)
    parser.add_argument("--configuration", choices=CONFIGURATIONS, default="standard")
    parser.add_argument("--device", choices=LITE_DEVICES, help="the lite edition's one game device")
    args = parser.parse_args()
    if (args.edition in LITES) != (args.device is not None):
        parser.error("--device goes with the lite edition, and the lite edition needs it")
    os.chdir(ROOT)
    source = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain"))
    options = edition_options(args.edition)
    if args.toolchain is None:
        args.toolchain = options["compiler"]
    os.environ["RIPTOPL_RA"] = "1" if options["ra"] else "0"
    os.environ["RIPTOPL_UPDATER"] = "1" if update_channel(args.edition, args.configuration, args.toolchain, args.device) else "0"
    suffix = "" if args.configuration == "standard" else f"-{args.configuration.upper()}"
    title = options["title"] if args.device is None else f"{options['title']}-{LITE_DEVICES[args.device]['title']}"
    os.environ["LOCALVERSION"] = f"PS2BUILD-{title}-{args.toolchain.upper()}{suffix}"
    # Each edition regenerates blobs, so sequential local builds cannot inherit
    # a previous edition's RA mark, theme graphics, or displayed version.
    subprocess.run([sys.executable, "tools/assets.py", "embed"], check=True)
    subprocess.run([sys.executable, "tools/assets.py", "version"], check=True)
    config = definition(args.edition, args.configuration, args.toolchain, args.device)
    canonical = yaml.safe_dump(config, sort_keys=False)
    output, stage = build_paths(args.edition, args.toolchain, args.configuration, args.device)
    result = build(args.toolchain, str(output), config)
    if result:
        return result
    elf = output / "bin/riptopl-packed.elf"
    if not elf.is_file() or elf.read_bytes()[:4] != b"\x7fELF":
        raise ValueError(f"invalid packed ELF: {elf}")
    if git("rev-parse", "HEAD") != source or bool(git("status", "--porcelain")) != dirty:
        raise ValueError("source checkout changed during build")
    stage.mkdir(parents=True, exist_ok=True)
    if args.device is not None:
        return stage_lite(args, options, source, dirty, canonical, elf, output, stage)
    shutil.copyfile(elf, stage / "RIPTOPL.ELF")
    # The i.LINK pair the ELF embeds also replaces the Neutrino bundle's copies (package_edition.py).
    shutil.copyfile(output / "bin/IEEE1394_bd_mini_fork.irx", stage / "IEEE1394_bd_mini.irx")
    shutil.copyfile(output / "bin/iLinkman_fork.irx", stage / "iLinkman.irx")
    manifest = {
        "repository": "NathanNeurotic/RiptOPL", "source_commit": source, "dirty": dirty,
        "edition": args.edition, **options, "sdk": "ps2build", "compiler": args.toolchain,
        "configuration": args.configuration, **sdk_suite(args.toolchain),
        "definition_sha256": hashlib.sha256(canonical.encode()).hexdigest(),
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "ilink_bd_sha256": hashlib.sha256((stage / "IEEE1394_bd_mini.irx").read_bytes()).hexdigest(),
        "ilink_manager_sha256": hashlib.sha256((stage / "iLinkman.irx").read_bytes()).hexdigest(),
        "version": (ROOT / "asm/opl_version.h").read_text().strip(),
        "run_url": os.environ.get("GITHUB_SERVER_URL", "https://github.com") + "/" +
            os.environ.get("GITHUB_REPOSITORY", "NathanNeurotic/RiptOPL") + "/actions/runs/" + os.environ.get("GITHUB_RUN_ID", "local"),
    }
    if publishes_unpacked(args.edition, args.configuration):
        # The bug-report build (publish_editions.py) ships diag before compression: the unpacked ELF
        # started from a launch path where every packed build showed a black screen. RIPTOPL-RA-VARIANTS
        # also ships the RA loaders unpacked: they start without the decompression step (CosmicScale).
        unpacked = output / "bin/riptopl.elf"
        shutil.copyfile(unpacked, stage / "RIPTOPL-UNPACKED.ELF")
        manifest["unpacked_elf_sha256"] = hashlib.sha256(unpacked.read_bytes()).hexdigest()
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
        # Every loader embeds smb2man over usmb2 (MIT), so every configuration build carries its licence.
        destination = stage / "licenses" / "usmb2"
        destination.mkdir(parents=True, exist_ok=True)
        for filename in ("LICENSE", "VENDORED.txt"):
            shutil.copyfile(ROOT / "modules/network/smb2man/usmb2" / filename, destination / filename)
    print(f"Built {options['title']}: {manifest['elf_sha256']}")
    return 0


def sdk_suite(toolchain):
    import yaml

    """The SDK a build linked: ps2build's version, its component versions and the linked ports.
    publish_editions.py and publish_lite.py refuse builds whose suite differs from the full kits'."""
    sdk = Path(os.environ.get("PS2BUILD_HOME", shutil.which("ps2build") or "")).resolve()
    if sdk.is_file():
        sdk = sdk.parent
    world = sdk / ("packages-llvm" if toolchain == "llvm" else "packages") / "world"
    return {
        "ps2build_version": subprocess.check_output(["ps2build", "--version"], text=True).strip(),
        "components": yaml.safe_load((sdk / "versions.yaml").read_text()) if (sdk / "versions.yaml").exists() else {},
        # install_ports.py may keep a cached ogg/vorbis through a host outage; record which one was linked.
        "ports": {package: hashlib.sha256(b"".join((world / package / "lib" / lib).read_bytes() for lib in libraries)).hexdigest()
                  for package, libraries in LIBRARIES.items() if (world / package).is_dir()},
    }


def stage_lite(args, options, source, dirty, canonical, elf, output, stage):
    import yaml

    """One RIPTOPLITE-<DEVICE>.ELF, its manifest and resolved definition (publish_editions.py collects them)."""
    title = LITE_DEVICES[args.device]["title"]
    name = elf_name(args.device, options["ra"])
    shutil.copyfile(elf, stage / name)
    main = next(t for t in yaml.safe_load(canonical)["targets"] if t["name"] == "riptopl")
    embedded = {entry.partition(":")[0] for entry in main["embed_irx"]}
    manifest = {
        "repository": "NathanNeurotic/RiptOPL", "source_commit": source, "dirty": dirty,
        "edition": args.edition, **options, "device": args.device, "device_title": title, "elf": name,
        "sdk": "ps2build", "compiler": args.toolchain, "configuration": args.configuration,
        **sdk_suite(args.toolchain),
        "definition_sha256": hashlib.sha256(canonical.encode()).hexdigest(),
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "elf_size": elf.stat().st_size,
        "unpacked_elf_size": (output / "bin/riptopl.elf").stat().st_size,
        "embedded_irx": sorted(embedded),
        "version": (ROOT / "asm/opl_version.h").read_text().strip(),
        "run_url": os.environ.get("GITHUB_SERVER_URL", "https://github.com") + "/" +
            os.environ.get("GITHUB_REPOSITORY", "NathanNeurotic/RiptOPL") + "/actions/runs/" + os.environ.get("GITHUB_RUN_ID", "local"),
    }
    (stage / "BUILD-MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (stage / "ps2.resolved.yaml").write_text(canonical, encoding="utf-8")
    print(f"Built {name}: {manifest['elf_size']} bytes, {manifest['elf_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
