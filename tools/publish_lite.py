"""The rolling-lite channel: one RiptOPLite download per game device (tools/lite_devices.py).

* RIPTOPLITE-<DEVICE>.ELF -- the Korium edition with only that device's drivers. A bare ELF: it needs
  nothing the standard Korium kit does not already put on the device.
* RIPTOPLITE-RA-<DEVICE>.zip -- the RetroAchievements edition, the same way. RA has requirements of its
  own outside the ELF (ORBIT's Neutrino RA runtime, which replaces upstream Neutrino for every RA launch,
  and mbedTLS's licence), so each RA download is a ZIP carrying them, copied from the verified RA kit.

publish_editions.py verifies every build against the commit and SDK of the full kits, then publishes
the folder this module assembles like any other channel.
"""
import hashlib
import json
import zipfile
from pathlib import Path

from build_edition import LITE, LITE_RA, LITES, EDITIONS
from lite_devices import DEVICES, elf_name

ROOT = Path(__file__).resolve().parents[1]
CHANNEL = "release-lite"
TAG = LITES[LITE]["tag"]
assert LITES[LITE_RA]["tag"] == TAG
# The RA kit entries an RA lite ZIP takes (everything under neutrino/, plus these).
RA_KIT_FILES = ("xeRAbora.url", "LICENSE-MBEDTLS.txt", "LICENSE-LIBSMB2.txt", "LICENSE", "CREDITS")
ISSUES = "https://github.com/NathanNeurotic/RiptOPL/issues"

RA_README = """RiptOPLite RetroAchievements {title}, {version}
{underline}

RiptOPL RetroAchievements (Korium theme) with only the {title} game drivers, for consoles that play
from {title} alone. MMCE and network modules stay in every RiptOPLite build, and so does everything
RetroAchievements uses on any device: the in-game network modules, the RA telemetry modules and the
Disc page.

What is in this ZIP
- {elf}: the loader. Put it where you start loaders from (for example APPS/APP_RIPTOPL-RA/).
- neutrino/: ORBIT's Neutrino RA runtime. RetroAchievements needs it for every Neutrino launch; it
  replaces upstream Neutrino. Copy the folder to the root of the device your games are on, where the
  RetroAchievements install kit puts it.
- xeRAbora.url: the PC companion RetroAchievements reports to.
- LICENSE, CREDITS and the LICENSE-*.txt files: the licences of RiptOPL and of the code this loader
  embeds (mbedTLS, libsmb2) and ORBIT's runtime (neutrino/LICENSE).

This build carries no drivers for other devices. It reads its settings where the full loader keeps
them, unless that is a device it has no drivers for; a memory card or a USB stick always works. It
does not change the settings of other devices that the full loader saved in the same file.

Problems: {issues}
"""


def _check(manifest, edition, device, sha, kit, elf_bytes):
    if (manifest.get("repository") != "NathanNeurotic/RiptOPL" or manifest.get("source_commit") != sha
            or manifest.get("dirty") or manifest.get("edition") != edition or manifest.get("device") != device
            or manifest.get("compiler") != "gcc" or manifest.get("configuration") != "standard"
            or manifest.get("theme") != "korium" or manifest.get("ra") != LITES[edition]["ra"]
            or any(manifest.get(field) != kit.get(field) for field in SDK_SUITE)
            or manifest.get("elf") != elf_name(device, LITES[edition]["ra"])
            or manifest.get("elf_sha256") != hashlib.sha256(elf_bytes).hexdigest()):
        raise ValueError(f"invalid lite build: {edition}/{device}")


# The SDK suite every lite build must share with the full kits (build_edition.sdk_suite).
SDK_SUITE = ("ps2build_version", "components", "ports")


def _version(manifest):
    return manifest["version"].split('"')[1]


def _common_version(manifest):
    """The version without its LOCALVERSION, which names the edition and device (build_edition.py)."""
    version, marker, _ = _version(manifest).partition("-PS2BUILD-")
    if not marker:
        raise ValueError(f"not a ps2build version: {_version(manifest)}")
    return version


def assemble(root, sha):
    """Build root/release-lite from the lite-* and lite_ra-* artifacts. Returns (path, sums)."""
    korium = json.loads((root / "release-korium" / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
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
            stage = root / f"lite-{device}"
            name = elf_name(device)
            elf = (stage / name).read_bytes()
            manifest = json.loads((stage / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
            _check(manifest, LITE, device, sha, korium, elf)
            (path / name).write_bytes(elf)
            manifests[name] = manifest

            stage = root / f"lite_ra-{device}"
            name = elf_name(device, ra=True)
            elf = (stage / name).read_bytes()
            manifest = json.loads((stage / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
            _check(manifest, LITE_RA, device, sha, ra_kit_manifest, elf)
            version = _version(manifest)
            readme = RA_README.format(title=options["title"], version=version, elf=name, issues=ISSUES,
                                      underline="=" * len(f"RiptOPLite RetroAchievements {options['title']}, {version}"))
            archive = path / name.replace(".ELF", ".zip")
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
                bundle.writestr(name, elf)
                bundle.writestr("BUILD-MANIFEST.json", json.dumps(manifest, indent=2) + "\n")
                bundle.writestr("README.txt", readme.replace("\n", "\r\n"))
                for entry in (*runtime, *RA_KIT_FILES):
                    bundle.writestr(entry, kit.read(entry))
            manifests[archive.name] = manifest
    versions = {_common_version(m) for m in manifests.values()} | {_common_version(korium)}
    if len(versions) != 1:
        raise ValueError(f"lite builds carry different versions: {sorted(versions)}")
    (path / "LICENSE-LIBSMB2.txt").write_bytes((root / "release-korium" / "LICENSE-LIBSMB2.txt").read_bytes())
    channel = {"repository": "NathanNeurotic/RiptOPL", "source_commit": sha, "edition": "lite",
               "version": korium["version"], "ps2build_version": korium["ps2build_version"], "builds": manifests}
    (path / "BUILD-MANIFEST.json").write_text(json.dumps(channel, indent=2) + "\n", encoding="utf-8")
    sums = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(path.iterdir()) if p.is_file()}
    (path / "SHA256SUMS.txt").write_text("".join(f"{d}  {n}\n" for n, d in sums.items()), encoding="utf-8")
    return path, sums


def expected_assets():
    names = {"BUILD-MANIFEST.json", "LICENSE-LIBSMB2.txt"}
    for device in DEVICES:
        names |= {elf_name(device), elf_name(device, ra=True).replace(".ELF", ".zip")}
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


def release_notes(sha, version, git, repo="NathanNeurotic/RiptOPL"):
    base = f"https://github.com/{repo}"
    downloads = f"{base}/releases/download/{TAG}"
    changes = []
    for record in git("log", "--no-merges", "-10", "--format=%H%x00%s", sha, "--").splitlines():
        commit, title = record.split("\x00", 1)
        title = "".join("\\" + c if c in "\\`*_[]<>" else c for c in title)
        changes.append(f"- {title} ([{commit[:8]}]({base}/commit/{commit}))")
    if not changes:
        raise ValueError("Release commit has no changelog entries")
    rows = "".join(
        f"| {options['title']} | [{elf_name(device)}]({downloads}/{elf_name(device)}) | "
        f"[{elf_name(device, True).replace('.ELF', '.zip')}]({downloads}/{elf_name(device, True).replace('.ELF', f'-{version}.zip')}) |\n"
        for device, options in DEVICES.items())
    return (
        f"**RiptOPLite** `{version}` - Development build [{sha[:8]}]({base}/commit/{sha}).\n\n"
        "One loader per game device, for consoles that only play from one. Each is RiptOPL Korium with "
        "that device's drivers alone, so it is smaller and starts faster; Memory Card (MMCE) and network "
        "modules stay in every build. HDD covers both APA and exFAT drives and DESR DVR games; UDPFS covers "
        "Files and IMG.\n\n"
        "## Downloads\n\n"
        "| Device | Korium | RetroAchievements |\n|---|---|---|\n" + rows +
        "\nThe Korium ELFs replace RIPTOPL.ELF in an existing install. RetroAchievements needs files outside "
        "the loader (ORBIT's Neutrino RA runtime), so each RA download is a ZIP that carries them; its "
        "README says where they go.\n\n"
        "A RiptOPLite loader reads its settings where the full loader keeps them, unless that is a device it "
        "has no drivers for; a memory card or a USB stick always works. It does not change the device "
        "settings the full loader saved in the same file.\n\n"
        "## Changes\n\n" + "\n".join(changes)
        + f"\n\n[Full history]({base}/commits/{sha})\n\n"
        "[Installation and guides](https://nathanneurotic.github.io/RiptOPL/) - "
        f"[Wiki]({base}/wiki)\n")
