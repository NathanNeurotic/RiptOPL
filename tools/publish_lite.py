"""The rolling-lite channel: one RiptOPLite build per game device (tools/lite_devices.py).

* RIPTOPLITE-<DEVICE>.ELF -- standard RiptOPL with only that device's drivers. A bare ELF: it needs
  nothing the standard RiptOPL kit does not already put on the device. Also the update asset.
* APP_RIPTOPLITE[-RA]-<DEVICE>.psu -- each loader in its own memory-card folder, named like every
  RiptOPL folder: APP_ plus the ELF name. The ELF reads the full loader's settings once from the
  APP_RIPTOPL[-RA] folder beside it (src/app/settingsio.cpp) and saves in its own.
* RIPTOPLITE-RA-<DEVICE>.zip -- the RetroAchievements edition, the same way. RA has requirements of its
  own outside the ELF (NULD's (formerly ORBIT) Neutrino RA runtime, which replaces upstream Neutrino for every RA launch,
  and mbedTLS's licence), so each RA download is a ZIP carrying them, copied from the verified RA kit.

publish_editions.py verifies every build against the commit and SDK of the full kits, then publishes
the folder this module assembles like any other channel.
"""
import hashlib
import importlib.util
import json
import tempfile
import zipfile
from pathlib import Path
from types import SimpleNamespace

from build_edition import LITE, LITE_RA, LITES, EDITIONS, update_asset
from lite_devices import DEVICES, elf_name

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("app_packager", ROOT / ".github/scripts/package_release_app.py")
app = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(app)
CHANNEL = "release-lite"
TAG = LITES[LITE]["tag"]
assert LITES[LITE_RA]["tag"] == TAG
# The RA kit entries an RA lite ZIP takes (everything under neutrino/, plus these).
RA_KIT_FILES = ("xeRAbora.url", "LICENSE-MBEDTLS.txt", "LICENSE-USMB2.txt", "LICENSE", "CREDITS")
ISSUES = "https://github.com/NathanNeurotic/RiptOPL/issues"

RA_README = """RiptOPLite RetroAchievements {title}, {version}
{underline}

RiptOPL RetroAchievements with only the {title} game drivers, for consoles that play
from {title} alone. MMCE and network modules stay in every RiptOPLite build, and so does everything
RetroAchievements uses on any device: the in-game network modules, the RA telemetry modules and the
Disc page.

What is in this ZIP
- APPS/{folder}/: the loader in its own folder. Copy the folder to APPS/ on a USB stick or other
  device, or import {folder}.psu to a memory card instead.
- neutrino/: NULD's (formerly ORBIT) Neutrino RA runtime. RetroAchievements needs it for every Neutrino launch; it
  replaces upstream Neutrino. Copy the folder to the root of the device your games are on, where the
  RetroAchievements install kit puts it.
- xeRAbora.url: the PC companion RetroAchievements reports to.
- LICENSE, CREDITS and the LICENSE-*.txt files: the licences of RiptOPL and of the code this loader
  embeds (mbedTLS, usmb2) and NULD's (formerly ORBIT) runtime (neutrino/LICENSE).

This build carries no drivers for other devices. The first time it starts from its own folder it
reads the RetroAchievements loader's settings from APP_RIPTOPL-RA beside it, if there are any, and
saves in its own folder from then on. APP_RIPTOPL-RA is never changed or deleted.

Problems: {issues}
"""


def unpacked_name(device, ra=False):
    """The same build before compression: RIPTOPLITE[-RA]-<DEVICE>-UNPAKT.ELF (build_edition.update_asset)."""
    return update_asset(LITE_RA if ra else LITE, "standard", device, unpacked=True)


def _check(manifest, edition, device, sha, kit, elf_bytes, compiler="gcc", channel="rolling", unpacked=None):
    if (manifest.get("repository") != "NathanNeurotic/RiptOPL" or manifest.get("source_commit") != sha
            or manifest.get("release_channel", "rolling") != channel
            or manifest.get("dirty") or manifest.get("edition") != edition or manifest.get("device") != device
            or manifest.get("compiler") != compiler or manifest.get("configuration") != "standard"
            or manifest.get("theme") != LITES[edition]["theme"] or manifest.get("ra") != LITES[edition]["ra"]
            or any(manifest.get(field) != kit.get(field) for field in SDK_SUITE)
            or manifest.get("elf") != elf_name(device, LITES[edition]["ra"])
            or manifest.get("elf_sha256") != hashlib.sha256(elf_bytes).hexdigest()
            or (unpacked is not None and (manifest.get("unpacked_elf") != unpacked_name(device, LITES[edition]["ra"])
                                          or manifest.get("unpacked_elf_sha256") != hashlib.sha256(unpacked).hexdigest()))):
        raise ValueError(f"invalid lite build: {edition}/{device}")


# The SDK suite every lite build must share with the full kits (build_edition.sdk_suite).
SDK_SUITE = ("ps2build_version", "components", "ports")


def app_folder(elf):
    """The memory-card folder and PSU of a loader: APP_ plus its ELF name (APP_RIPTOPLITE-RA-USB)."""
    if not elf.endswith(".ELF"):
        raise ValueError(f"not a loader: {elf}")
    return "APP_" + elf[:-len(".ELF")]


def _stage_app(elf_name, elf, version, title, channel="RiptOPLite"):
    """The loader's memory-card folder (same companions as APP_RIPTOPL) and its PSU, as {name: bytes}."""
    folder = app_folder(elf_name)
    with tempfile.TemporaryDirectory(prefix="lite-app-") as temp:
        kit = Path(temp)
        source = kit / elf_name
        source.write_bytes(elf)
        app.stage(SimpleNamespace(package=str(kit / "pkg"), source_elf=str(source), app_name=folder, elf_name=elf_name,
                                  title=title, pbt_title=title, version=version, flavour="PS2BUILD",
                                  channel=channel, description=title))
        app_dir = kit / "pkg" / "APPS" / folder
        files = {p.name: p.read_bytes() for p in sorted(app_dir.iterdir()) if p.is_file()}
        psu = (kit / "pkg" / f"{folder}.psu").read_bytes()
    name, inside = app.inspect_psu(psu)
    if name != folder or inside != files:
        raise ValueError(f"{folder}.psu does not hold its folder")
    return files, psu


def _version(manifest):
    return manifest["version"].split('"')[1]


def _common_version(manifest):
    """The version without its LOCALVERSION, which names the edition and device (build_edition.py)."""
    version, marker, _ = _version(manifest).partition("-PS2BUILD-")
    if not marker:
        raise ValueError(f"not a ps2build version: {_version(manifest)}")
    return version


def assemble(root, sha, compiler="gcc", channel="rolling"):
    """Build root/release-lite from the lite-* and lite_ra-* artifacts. Returns (path, sums)."""
    ra_kit_manifest = json.loads((root / "release-ra" / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
    ra_kit = root / "release-ra" / f"RIPTOPL-{EDITIONS['ra']['title']}.zip"
    path = root / CHANNEL
    path.mkdir(parents=True, exist_ok=False)
    manifests = {}
    with zipfile.ZipFile(ra_kit) as kit:
        if kit.testzip():
            raise ValueError("RA install kit is damaged")
        if json.loads(kit.read("BUILD-MANIFEST.json")) != ra_kit_manifest:
            raise ValueError("RA install kit differs from its manifest")
        runtime = sorted(n for n in kit.namelist() if n.startswith("neutrino/") and not n.endswith("/"))
        if "neutrino/ORBIT-RA.txt" not in runtime or not any(n.lower().endswith("raagent.irx") for n in runtime):
            raise ValueError("RA install kit carries no ORBIT runtime")
        for device, options in DEVICES.items():
            for edition, ra, label in ((LITE, False, "RiptOPLite"), (LITE_RA, True, "RiptOPLite RA")):
                stage = root / f"{edition}-{device}"
                manifest = json.loads((stage / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
                version = _version(manifest)
                packed = elf_name(device, ra)
                builds = ((packed, (stage / packed).read_bytes(), f"{label} {options['title']}"),
                          (unpacked_name(device, ra), (stage / unpacked_name(device, ra)).read_bytes(),
                           f"{label} {options['title']} UNPAKT"))
                _check(manifest, edition, device, sha, ra_kit_manifest, builds[0][1], compiler, channel, builds[1][1])
                for name, elf, title in builds:
                    (path / name).write_bytes(elf)
                    manifests[name] = manifest
                    folder = app_folder(name)
                    files, psu = _stage_app(name, elf, version, title)
                    (path / f"{folder}.psu").write_bytes(psu)
                    if not ra:
                        continue
                    readme = RA_README.format(title=options["title"], version=version, folder=folder, issues=ISSUES,
                                              underline="=" * len(f"RiptOPLite RetroAchievements {options['title']}, {version}"))
                    archive = path / name.replace(".ELF", ".zip")
                    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
                        for file, data in files.items():
                            bundle.writestr(f"APPS/{folder}/{file}", data)
                        bundle.writestr(f"{folder}.psu", psu)
                        bundle.writestr("BUILD-MANIFEST.json", json.dumps(manifest, indent=2) + "\n")
                        bundle.writestr("README.txt", readme.replace("\n", "\r\n"))
                        for entry in (*runtime, *RA_KIT_FILES):
                            bundle.writestr(entry, kit.read(entry))
                    manifests[archive.name] = manifest
        (path / "LICENSE-MBEDTLS.txt").write_bytes(kit.read("LICENSE-MBEDTLS.txt"))
    versions = {_common_version(m) for m in manifests.values()} | {_common_version(ra_kit_manifest)}
    if len(versions) != 1:
        raise ValueError(f"lite builds carry different versions: {sorted(versions)}")
    (path / "LICENSE-USMB2.txt").write_bytes((root / "release-ra" / "LICENSE-USMB2.txt").read_bytes())
    channel = {"repository": "NathanNeurotic/RiptOPL", "source_commit": sha, "edition": "lite",
               "version": ra_kit_manifest["version"], "ps2build_version": ra_kit_manifest["ps2build_version"], "builds": manifests}
    (path / "BUILD-MANIFEST.json").write_text(json.dumps(channel, indent=2) + "\n", encoding="utf-8")
    sums = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(path.iterdir()) if p.is_file()}
    (path / "SHA256SUMS.txt").write_text("".join(f"{d}  {n}\n" for n, d in sums.items()), encoding="utf-8")
    return path, sums


def expected_assets():
    names = {"BUILD-MANIFEST.json", "LICENSE-MBEDTLS.txt", "LICENSE-USMB2.txt"}
    for device in DEVICES:
        for elf in (elf_name(device), unpacked_name(device)):
            names |= {elf, f"{app_folder(elf)}.psu"}
        for elf in (elf_name(device, ra=True), unpacked_name(device, ra=True)):
            names |= {elf, f"{app_folder(elf)}.psu", elf.replace(".ELF", ".zip")}
    return names


def verify(path):
    """The assembled folder (before versioned names) holds exactly the expected files, checksums matching."""
    sums = {}
    for line in (path / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        if Path(name).name != name or name in sums:
            raise ValueError(f"unsafe or duplicate asset name: {name}")
        if hashlib.sha256((path / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f"checksum mismatch: lite/{name}")
        sums[name] = digest
    if set(sums) != expected_assets() or {p.name for p in path.iterdir() if p.is_file()} != set(sums) | {"SHA256SUMS.txt"}:
        raise ValueError("unexpected lite release inventory")
    return sums


def release_notes(sha, version, git, repo="NathanNeurotic/RiptOPL", names=None):
    """Recommended downloads, then every file in a table (release_table.py) built from the published names."""
    import release_table

    base = f"https://github.com/{repo}"
    downloads = f"{base}/releases/download/{TAG}"
    changes = []
    for record in git("log", "--no-merges", "-10", "--format=%H%x00%s", sha, "--").splitlines():
        commit, title = record.split("\x00", 1)
        title = "".join("\\" + c if c in "\\`*_[]<>" else c for c in title)
        changes.append(f"- {title} ([{commit[:8]}]({base}/commit/{commit}))")
    if not changes:
        raise ValueError("Release commit has no changelog entries")
    if names is None:
        names = [n.replace(".zip", f"-{version}.zip") for n in expected_assets()] + ["SHA256SUMS.txt"]
    kit = f"{base}/releases/download/{EDITIONS['riptopl']['tag']}/RIPTOPL-{version}.zip"
    recommended = [(elf_name("usb"), "a smaller loader for a console that plays from a USB drive only. Every other "
                    "device has its own row in the Loaders list; take the one your games are on."),
                   (elf_name("usb", True).replace(".ELF", f"-{version}.zip"),
                    "RetroAchievements on a USB drive: take the ZIP, it carries the RA runtime the loader needs.")]
    return (
        f"**RiptOPLite** `{version}` - Development build [{sha[:8]}]({base}/commit/{sha}).\n\n"
        "One loader per game device, for consoles that only play from one. Each is RiptOPL with "
        "that device's drivers alone, so it is smaller and starts faster; Memory Card (MMCE) and network "
        "modules stay in every build. HDD covers both APA and exFAT drives and DESR DVR games; UDPFS covers "
        "Files and IMG.\n\n"
        + release_table.tables(names, downloads, version, kit, recommended,
                               kit_why="Not sure what to pick? Take the full RiptOPL kit. It plays from every device, "
                               "has everything ready to copy, and this page is optional.")
        + "The first time a RiptOPLite loader starts from its own folder, it reads the full loader's settings from "
        "`APP_RIPTOPL` (or `APP_RIPTOPL-RA`) beside it, if there are any, and saves in its own folder from then "
        "on. The full loader's folder is never changed or deleted.\n\n"
        "## Changes\n\n" + "\n".join(changes)
        + f"\n\n[Full history]({base}/commits/{sha})\n\n"
        "[Installation and guides](https://nathanneurotic.github.io/RiptOPL/) - "
        f"[Wiki]({base}/wiki)\n")
