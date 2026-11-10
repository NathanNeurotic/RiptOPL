#!/usr/bin/env python3
"""Verify and publish the separate, LLVM-built RiptOPLLVM rolling release.

This is not an edition or a replacement for the four GCC rolling channels.
All artifacts must come from the same commit and LLVM SDK suite.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

import publish_editions as common
import publish_lite
from build_edition import git
from lite_devices import DEVICES, elf_name

REPO = "NathanNeurotic/RiptOPL"
TAG = "rolling-llvm"
TITLE = "RiptOPLLVM"
COMPILER = "llvm"
EDITIONS = ("korium", "ra")
CHANNEL = "release-llvm"


def checksum(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expected_files(version):
    suffix = "-" + version + ".zip"
    names = {
        "RIPTOPL.ELF", "RIPTOPL-RA.ELF",
        "APP_RIPTOPL.psu", "APP_RIPTOPL-RA.psu",
        "BUILD-MANIFEST.json", "LICENSE-USMB2.txt",
        "RIPTOPL-Korium" + suffix,
        "RIPTOPL-RetroAchievements" + suffix,
        "RIPTOPL-VARIANTS" + suffix, "RIPTOPL-DEBUG" + suffix,
        "RIPTOPL-BUGREPORT" + suffix, "RIPTOPL-RA-VARIANTS" + suffix,
        "RIPTOPL-src" + suffix, "RIPTOPL-LANGS" + suffix,
        "RIPTOPL-RA-LANGS" + suffix,
    }
    for device in DEVICES:
        names.add(elf_name(device))
        names.add(elf_name(device, ra=True).replace(".ELF", suffix))
    return names


def assemble(root, candidates, sha, version):
    """Combine fully verified per-edition candidates with collision-safe file names."""
    if tuple(row[0] for row in candidates) != ("korium", "ra", "lite"):
        raise ValueError("RiptOPLLVM requires Korium, RA, and both Lite sets")
    stage = root / CHANNEL
    stage.mkdir(parents=True, exist_ok=False)
    manifests = {}
    for edition, _, path, _ in candidates:
        manifest = json.loads((path / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
        if manifest.get("source_commit") != sha or manifest.get("repository") != REPO:
            raise ValueError("LLVM edition provenance differs from source checkout")
        if edition == "lite":
            builds = manifest.get("builds", {})
            if len(builds) != 2 * len(DEVICES) or any(
                item.get("compiler") != COMPILER or item.get("source_commit") != sha
                for item in builds.values()
            ):
                raise ValueError("LLVM lite channel contains a missing or non-LLVM build")
        elif manifest.get("compiler") != COMPILER:
            raise ValueError("non-LLVM full edition cannot enter RiptOPLLVM")
        manifests[edition] = manifest
        for source in sorted(path.iterdir()):
            if not source.is_file() or source.name in ("SHA256SUMS.txt", "BUILD-MANIFEST.json"):
                continue
            name = source.name
            if edition == "ra" and name.startswith("RIPTOPL-LANGS-") and name.endswith(".zip"):
                name = name.replace("RIPTOPL-LANGS-", "RIPTOPL-RA-LANGS-", 1)
            destination = stage / name
            if destination.exists():
                if name != "LICENSE-USMB2.txt" and not name.startswith("RIPTOPL-src-"):
                    raise ValueError(f"duplicate LLVM release filename: {name}")
                if checksum(destination) != checksum(source):
                    raise ValueError(f"mismatched shared LLVM asset: {name}")
                continue
            shutil.copyfile(source, destination)
    channel = {
        "repository": REPO, "tag": TAG, "title": TITLE, "compiler": COMPILER,
        "source_commit": sha, "version": version, "editions": manifests,
    }
    (stage / "BUILD-MANIFEST.json").write_text(json.dumps(channel, indent=2) + "\n", encoding="utf-8")
    verify(stage, version, sha)
    return stage


def verify(stage, version, sha):
    """Refuse incomplete, damaged, mixed-compiler, or unexpectedly named releases."""
    actual = {path.name for path in stage.iterdir() if path.is_file()}
    expected = expected_files(version) | {"SHA256SUMS.txt"}
    # The final sum file is generated below, so tolerate its absence while assembling.
    if actual != expected and actual != expected - {"SHA256SUMS.txt"}:
        raise ValueError(f"RiptOPLLVM inventory mismatch: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")
    manifest = json.loads((stage / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
    if (manifest.get("source_commit") != sha or manifest.get("compiler") != COMPILER
            or manifest.get("tag") != TAG or manifest.get("version") != version):
        raise ValueError("RiptOPLLVM channel manifest is inconsistent")
    if set(manifest["editions"]) != {"korium", "ra", "lite"}:
        raise ValueError("RiptOPLLVM edition manifest is incomplete")
    for edition in EDITIONS:
        item = manifest["editions"][edition]
        if item.get("compiler") != COMPILER or item.get("source_commit") != sha:
            raise ValueError("RiptOPLLVM contains non-LLVM edition provenance")
    if any(item.get("compiler") != COMPILER for item in manifest["editions"]["lite"]["builds"].values()):
        raise ValueError("RiptOPLLVM Lite manifest contains non-LLVM objects")
    for path in stage.iterdir():
        if path.name.endswith(".zip"):
            with zipfile.ZipFile(path) as archive:
                if archive.testzip():
                    raise ValueError(f"corrupt RiptOPLLVM archive: {path.name}")
    sums = {path.name: checksum(path) for path in sorted(stage.iterdir()) if path.is_file() and path.name != "SHA256SUMS.txt"}
    sums_file = stage / "SHA256SUMS.txt"
    if sums_file.exists():
        recorded = {}
        for line in sums_file.read_text(encoding="utf-8").splitlines():
            digest, name = line.split("  ", 1)
            if name in recorded or Path(name).name != name:
                raise ValueError("unsafe or duplicate RiptOPLLVM checksum entry")
            recorded[name] = digest
        if recorded != sums:
            raise ValueError("RiptOPLLVM published checksum manifest does not match the binaries")
    else:
        sums_file.write_text("".join(f"{digest}  {name}\n" for name, digest in sums.items()), encoding="utf-8")
    return sums


def release_notes(version, sha):
    base = f"https://github.com/{REPO}"
    url = f"{base}/releases/download/{TAG}"
    def link(label, name):
        return f"- [{label}]({url}/{name})"
    suffix = "-" + version + ".zip"
    lines = [
        f"**RiptOPLLVM** {version} — LLVM/Clang build of [{sha[:8]}]({base}/commit/{sha}).",
        "",
        "Separate LLVM rolling channel. The existing Korium, RetroAchievements, IncebtIon and RiptOPLite GCC releases are unaffected.",
        "All builds use the pinned PS2BUILD LLVM toolchain and matching LLVM SDK/ports, with per-artifact compiler provenance, source SHA and checksums. This is a pre-release compiler variant; hardware compatibility still requires testing.",
        "",
        "## Full install kits and configurations",
        "",
        link("Korium full install kit", "RIPTOPL-Korium" + suffix),
        link("Korium RetroAchievements full install kit (ORBIT RA runtime)", "RIPTOPL-RetroAchievements" + suffix),
        link("Korium ELF", "RIPTOPL.ELF"),
        link("Korium RetroAchievements ELF", "RIPTOPL-RA.ELF"),
        link("Korium variants", "RIPTOPL-VARIANTS" + suffix),
        link("Korium debug configurations", "RIPTOPL-DEBUG" + suffix),
        link("Korium bug-report build", "RIPTOPL-BUGREPORT" + suffix),
        link("RetroAchievements variants", "RIPTOPL-RA-VARIANTS" + suffix),
        "",
        "## RiptOPLite — one-device builds",
        "",
        "| Game device | Korium LLVM | RetroAchievements LLVM |",
        "|---|---|---|",
    ]
    for device, options in DEVICES.items():
        standard = elf_name(device)
        ra = elf_name(device, ra=True).replace(".ELF", suffix)
        lines.append(f"| {options['title']} | [{standard}]({url}/{standard}) | [{ra}]({url}/{ra}) |")
    lines.extend([
        "",
        "RA Lite ZIPs include the verified ORBIT Neutrino RA runtime and their required licences. The standalone Lite ELFs require an existing Korium installation.",
        "",
        link("Korium translations", "RIPTOPL-LANGS" + suffix),
        link("RetroAchievements translations", "RIPTOPL-RA-LANGS" + suffix),
        link("Source snapshot", "RIPTOPL-src" + suffix),
        link("Build provenance for all editions", "BUILD-MANIFEST.json"),
        link("SHA-256 checksums", "SHA256SUMS.txt"),
        link("usmb2 licence", "LICENSE-USMB2.txt"),
        "",
        "## Recent changes",
        "",
    ])
    history = git("log", "--no-merges", "-10", "--format=%h%x00%s", sha, "--")
    for line in history.splitlines():
        commit, title = line.split("\x00", 1)
        title = "".join("\\" + ch if ch in "\\*_[]<>" + chr(96) else ch for ch in title)
        lines.append(f"- {title} ([{commit}]({base}/commit/{commit}))")
    if not history:
        raise ValueError("release commit has no changelog entries")
    lines.extend(["", f"[Full history]({base}/commits/{sha})"])
    return "\n".join(lines) + "\n"


def publish(stage, version, sha):
    if not common.publication_allowed(os.environ, sha):
        raise ValueError("RiptOPLLVM only publishes from an upstream integration push or explicit dispatch")
    head = json.loads(common.gh("api", f"repos/{REPO}/branches/rebuild/main"))["commit"]["sha"]
    if head != sha:
        newer = common.successor_run(REPO, workflow="rolling-llvm.yml")
        if newer is None:
            raise ValueError("integration head moved but no newer RiptOPLLVM run will publish it")
        print(f"::notice::Skipping overtaken LLVM build {sha[:8]}; successor: {newer}")
        return
    notes = release_notes(version, sha)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".md", delete=False) as stream:
        stream.write(notes)
        notes_file = Path(stream.name)
    try:
        exists = subprocess.run(["gh", "release", "view", TAG, "--repo", REPO],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        assets = [str(path) for path in sorted(stage.iterdir()) if path.is_file()]
        common.upload_verified(REPO, TAG, sha, TITLE, notes_file, assets, exists)
        common.update_tag(REPO, TAG, sha)
        flags = ("--prerelease=true", "--latest=false")
        common.gh("release", "edit", TAG, "--repo", REPO, "--title", TITLE,
                  "--notes-file", str(notes_file), "--draft=false", *flags)
        if exists:
            common.refresh_release_date(REPO, TAG, flags)
        ref = json.loads(common.gh("api", f"repos/{REPO}/git/ref/tags/{TAG}"))
        if ref["object"]["sha"] != sha:
            raise ValueError("LLVM release tag changed during publication")
        common.remove_stale_archives(REPO, TAG, [Path(name).name for name in assets])
        print(f"Published {TITLE} {version} from {sha}")
    finally:
        notes_file.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--check", action="store_true", help="verify all release assets without publishing")
    args = parser.parse_args()
    sha = git("rev-parse", "HEAD")
    common.bundle_configurations(args.artifacts, sha, editions=EDITIONS, compiler=COMPILER)
    kits = common.verify(args.artifacts, sha, editions=EDITIONS, compiler=COMPILER)
    candidates = kits + common.lite_candidates(args.artifacts, sha, compiler=COMPILER)
    version = common.release_version(candidates)
    common.name_assets(candidates, version)
    stage = assemble(args.artifacts, candidates, sha, version)
    if args.check:
        print(f"Verified complete {TITLE} {version} at {sha} (LLVM only)")
        return
    publish(stage, version, sha)


if __name__ == "__main__":
    main()
