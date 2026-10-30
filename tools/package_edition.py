#!/usr/bin/env python3
"""Assemble and verify a full ps2build install kit from a successful edition build."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import zipfile

from build_edition import EDITIONS, ROOT, git
from install_ports import download

spec = importlib.util.spec_from_file_location("app_packager", ROOT / ".github/scripts/package_release_app.py")
app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)


ORBIT_PIN = ROOT / "third_party/orbit-neutrino/release.json"


def stage_orbit(archive, destination, pin):
    """The RA kit's neutrino/: ORBIT's pinned runtime (raagent.irx), checked in full before any write."""
    if hashlib.sha256(archive.read_bytes()).hexdigest() != pin["sha256"]:
        raise ValueError("ORBIT release digest mismatch")
    payload = {}
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            path = PurePosixPath(entry.filename)
            if path.is_absolute() or ".." in path.parts or "\\" in entry.orig_filename:
                raise ValueError("unsafe ORBIT archive path")
            if entry.is_dir() or len(path.parts) < 3 or path.parts[1] != "neutrino":
                continue  # the GPL ORBIT launcher beside neutrino/ is not ours to ship
            relative = PurePosixPath(*path.parts[2:]).as_posix()
            if relative in payload:
                raise ValueError("duplicate ORBIT runtime entry")
            payload[relative] = source.read(entry)
    for name in ("neutrino.elf", "modules/ee_core.elf", "modules/raagent.irx", "modules/IEEE1394_bd_mini.irx",
                 "modules/iLinkman.irx", "config/system.toml", "config/bsd-udpfs.toml", "LICENSE"):
        if not payload.get(name):
            raise ValueError(f"missing ORBIT runtime entry: {name}")
    if b"-ra=<file>" not in payload["neutrino.elf"]:
        raise ValueError("ORBIT loader lacks the expected RA handoff")
    for name, data in payload.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (destination / "ORBIT-RA.json").write_text(json.dumps(pin, indent=2) + "\n", encoding="utf-8")
    # sysNeutrinoRARuntime accepts a runtime only when this marker's first line matches.
    (destination / "ORBIT-RA.txt").write_text(
        "RiptOPL ORBIT RA handoff v1\n"
        "ORBIT Neutrino RA runtime by Daniel (danielnuld), based on Neutrino by Maximus32\n"
        "and xeRAbora's protocol by hacan359. AFL-3.0; see LICENSE.\n"
        f"Source: https://github.com/{pin['repository']}/tree/{pin['source_commit']}/third_party/neutrino-igr\n"
        "Pinned release and archive SHA-256: see ORBIT-RA.json.\n", encoding="utf-8")
    (destination / "RA-EDITION-NEUTRINO.txt").write_text(
        "THIS neutrino/ FOLDER BELONGS TO THE RIPTOPL RETROACHIEVEMENTS EDITION.\n"
        "\n"
        f"It is ORBIT's RA-Neutrino {pin['tag']}: Neutrino v1.8.0 with the achievement agent\n"
        "(raagent.irx). Only modules/iLinkman.irx and modules/IEEE1394_bd_mini.irx are replaced,\n"
        "by RiptOPL's i.LINK pair, as in the standard kit. It is NOT the standard RiptOPL Neutrino:\n"
        "keep this folder with the RA loader (APPS/APP_RIPTOPL-RA) and the standard one with the standard loader.\n"
        "\n"
        "Every Neutrino launch in the RA edition uses this runtime, so upstream changes made\n"
        "after Neutrino v1.8.0 (including Neutrino cheats) are not available here.\n"
        "\n"
        f"ORBIT Neutrino RA runtime {pin['tag']}: https://github.com/{pin['repository']}/releases/tag/{pin['tag']}\n", encoding="utf-8")


LANGS_README = """RiptOPL translations

The loader has English built in. To use another language, copy that language's folder contents --
the lang_*.lng file and, when there is one, its font_*.ttf or font_*.otf -- to the folder RIPTOPL.ELF
runs from, or to an LNG folder beside it. Do not rename the files. Then pick the language under
Settings > Language and save your settings. Every edition reads the same files.
"""


def language_pack(destination, version):
    """The .lng files and the fonts their scripts need: neither is in the ELF (original tools/lng_pack.sh)."""
    fonts = ROOT / "lng_src/thirdparty"
    languages = sorted((ROOT / "lng").glob("lang_*.lng"))
    if not languages:
        raise ValueError("no lng/lang_*.lng; run tools/assets.py fetch and lang first")
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as pack:
        for lng in languages:
            name = lng.stem[len("lang_"):]
            folder = f"{name}-{version}/"
            pack.write(lng, folder + lng.name)
            # No Serbian font upstream, and Serbian Latin uses exactly Croatian's letters.
            for font in (fonts / f"font_{name}.ttf", fonts / f"font_{name}.otf",
                         fonts / "font_Croatian.ttf" if name == "Serbian" else None):
                if font is not None and font.is_file():
                    pack.write(font, f"{folder}font_{name}{font.suffix}")
                    break
        pack.writestr("README.txt", LANGS_README)


def package(edition, neutrino_archive, orbit_archive=None):
    options = EDITIONS[edition]
    built = ROOT / "dist/editions" / edition
    manifest = json.loads((built / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
    if manifest.get("configuration", "standard") != "standard":
        raise ValueError("configuration candidates cannot be packaged as standard releases")
    elf = built / "RIPTOPL.ELF"
    if manifest["edition"] != edition or manifest["source_commit"] != git("rev-parse", "HEAD"):
        raise ValueError("edition/source mismatch; rebuild before packaging")
    if manifest["dirty"] or git("status", "--porcelain"):
        raise ValueError("release packaging requires a clean source checkout and build")
    if manifest["elf_sha256"] != hashlib.sha256(elf.read_bytes()).hexdigest():
        raise ValueError("ELF no longer matches its build manifest")
    if manifest["compiler"] != "gcc":
        raise ValueError("published kits use GCC; LLVM artifacts are validation candidates")
    out = ROOT / "dist/releases" / edition
    if out.exists() and any(out.iterdir()):
        raise ValueError(f"output is not empty: {out}; use a fresh build workspace")
    out.mkdir(parents=True, exist_ok=True)
    app_name = "APP_RIPTOPL-RA" if options["ra"] else "APP_RIPTOPL"
    elf_name = "RIPTOPL-RA.ELF" if options["ra"] else "RIPTOPL.ELF"
    with tempfile.TemporaryDirectory(prefix="package-", dir=ROOT / "build") as temp:
        kit = Path(temp)
        for folder in ("POPS", "EMBER"):
            shutil.copytree(ROOT / "ship" / folder, kit / folder)
        for filename in ("CHEATS.TXT", "IGR_BG.TM2", "IGR_NO.TM2", "IGR_YES.TM2", "POPSTARTER.KELF", "TROJAN_7.BIN"):
            shutil.copyfile(app.EXAMPLE / "POPS" / filename, kit / "POPS" / filename)
        games = kit / "EMBER/games/Per-Game-Folder"
        games.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(app.EXAMPLE / "EMBER/games/Per-Game-Folder/GAME FILES", games / "GAME FILES")
        if (kit / "EMBER/bios.bin").exists():
            raise ValueError("Ember BIOS must not be redistributed")
        if options["ra"]:  # ORBIT's runtime replaces upstream Neutrino in the RA kit
            pin = json.loads(ORBIT_PIN.read_text(encoding="utf-8"))
            if orbit_archive is None:
                orbit_archive = kit.parent / pin["asset"]
                url = f"https://github.com/{pin['repository']}/releases/download/{pin['tag']}/{pin['asset']}"
                orbit_archive.write_bytes(download(url))  # retries 5xx/timeouts; the pin checks the bytes
            stage_orbit(orbit_archive, kit / "neutrino", pin)
            neutrino_archive = orbit_archive
        else:
            extractor = shutil.which("7z") or shutil.which("7za")
            if not extractor:
                raise ValueError("7z is required for the upstream Neutrino archive")
            # Inspect members before extraction; never allow an archive to leave its directory.
            listing = subprocess.check_output([extractor, "l", "-slt", str(neutrino_archive)], text=True)
            for line in listing.split("----------", 1)[-1].splitlines():
                if line.startswith("Path = "):
                    name = line[7:].replace("\\", "/")
                    if name.startswith("/") or ":" in name or ".." in name.split("/"):
                        raise ValueError(f"unsafe Neutrino archive path: {name}")
                if line.startswith(("Symbolic Link = ", "Hard Link = ")):
                    raise ValueError("Neutrino archive contains a link")
            subprocess.run([extractor, "x", "-y", "-o" + str(kit / "neutrino"), str(neutrino_archive)], check=True, stdout=subprocess.DEVNULL)
        shutil.copyfile(ROOT / "ship/neutrino/bsd-udpfsbd.toml", kit / "neutrino/config/bsd-udpfsbd.toml")
        # i.LINK: Neutrino's own IEEE1394_bd_mini.irx loses its login after an IOP reset (original
        # #1018). Replace it AND its manager with RiptOPL's builds -- the pair the ELF embeds; the
        # manager carries the TX cancellation, alarm and receive-FIFO fixes, so the driver alone leaves
        # them out (original 00bc8e5). Refuse to package if upstream stopped shipping the files
        # bsd-ilink.toml loads.
        for module, key in (("IEEE1394_bd_mini.irx", "ilink_bd_sha256"), ("iLinkman.irx", "ilink_manager_sha256")):
            ilink = kit / "neutrino/modules" / module
            ours = built / module
            if not ilink.is_file() or not ours.is_file() or hashlib.sha256(ours.read_bytes()).hexdigest() != manifest[key]:
                raise ValueError(f"cannot replace neutrino/modules/{module} (upstream file or RiptOPL build missing)")
            shutil.copyfile(ours, ilink)
        shortcuts = {name: name for name in ("PS2-Servers.url", "OrbitPS2-Manager.url", "OPL-PS1-AIO-Converter-GUI.url", "PS2RD-CHT-Manager.url")}
        shortcuts["android-udpfs-server.url"] = "udpfs-server.url"
        for target, source in shortcuts.items():
            shutil.copyfile(ROOT / "tools/pc" / source, kit / target)
        if options["ra"]:
            (kit / "xeRAbora.url").write_text("[InternetShortcut]\nURL=https://github.com/hacan359/xerabora/releases/tag/v1.0.1\n", encoding="utf-8")
        args = SimpleNamespace(package=str(kit), source_elf=str(elf), app_name=app_name, elf_name=elf_name,
            title="RiptOPL " + options["title"], pbt_title="RiptOPL " + options["title"],
            version=manifest["version"].split('"')[1], flavour="PS2BUILD", channel=options["title"],
            description="C++ ps2build migration; " + options["title"])
        app.stage(args)
        manifest["neutrino_archive"] = {"filename": neutrino_archive.name, "sha256": hashlib.sha256(neutrino_archive.read_bytes()).hexdigest()}
        for name in ("LICENSE", "CREDITS", "README.md"):
            shutil.copyfile(ROOT / name, kit / name)
        if options["ra"]:  # the RA ELF links mbedTLS (Apache-2.0): its licence travels with it
            shutil.copyfile(ROOT / "third_party/mbedtls/LICENSE", kit / "LICENSE-MBEDTLS.txt")
        manifest["kit_files"] = {p.relative_to(kit).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(kit.rglob("*")) if p.is_file()}
        (kit / "BUILD-MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        archive = out / f"RIPTOPL-{options['title']}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
            for path in sorted(kit.rglob("*")):
                if path.is_file():
                    zipped.write(path, path.relative_to(kit).as_posix())
        app.verify(SimpleNamespace(zip=str(archive), source_elf=str(elf), app_name=app_name, elf_name=elf_name, ra=options["ra"]))
        # RA resources must never enter either standard kit.
        if not options["ra"]:
            with zipfile.ZipFile(archive) as zipped:
                if any("raagent" in n.lower() or "orbit-ra" in n.lower() or "xerabora" in n.lower() for n in zipped.namelist()):
                    raise ValueError("RA resource leaked into a standard package")
        shutil.copyfile(kit / (app_name + ".psu"), out / (app_name + ".psu"))
        shutil.copyfile(kit / "BUILD-MANIFEST.json", out / "BUILD-MANIFEST.json")
    shutil.copyfile(elf, out / elf_name)
    subprocess.run(["git", "archive", "--format=zip", "--output=" + str(out / "RIPTOPL-src.zip"), manifest["source_commit"]], cwd=ROOT, check=True)
    language_pack(out / "RIPTOPL-LANGS.zip", manifest["version"].split('"')[1])
    sums = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in sorted(out.iterdir()) if p.is_file())
    (out / "SHA256SUMS.txt").write_text(sums, encoding="utf-8")
    print(f"Verified {options['title']} release assets: {out}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("edition", choices=EDITIONS)
    parser.add_argument("--neutrino-archive", required=True, type=Path, help="standard kits: the upstream Neutrino .7z (unused for RA)")
    parser.add_argument("--orbit-archive", type=Path, help="RA only: the pinned ORBIT ZIP (else downloaded)")
    args = parser.parse_args()
    try:
        package(args.edition, args.neutrino_archive.resolve(), args.orbit_archive and args.orbit_archive.resolve())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
