#!/usr/bin/env python3
"""Publish three verified ps2build kits from integration pushes or explicit dispatches."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import zipfile

from build_edition import EDITIONS, git


def gh(*args):
    return subprocess.check_output(["gh", *args], text=True).strip()


def publication_allowed(environment, sha):
    """Reject previews, forks, other refs and mismatched workflow source commits."""
    return (environment.get("GITHUB_REPOSITORY") == "NathanNeurotic/RiptOPL"
            and environment.get("GITHUB_EVENT_NAME") in ("push", "workflow_dispatch")
            and environment.get("GITHUB_REF") == "refs/heads/rebuild/main"
            and environment.get("GITHUB_SHA") == sha)


def update_tag(repo, tag, sha):
    """Create a first-release tag or move an existing channel after asset verification."""
    result = subprocess.run(["gh", "api", f"repos/{repo}/git/ref/tags/{tag}"],
                            capture_output=True, text=True)
    if result.returncode == 0:
        gh("api", f"repos/{repo}/git/refs/tags/{tag}", "-X", "PATCH", "-f", "sha=" + sha, "-F", "force=true")
    elif "HTTP 404" in result.stderr:
        # Creating a draft release need not create its Git tag until publication.
        gh("api", f"repos/{repo}/git/refs", "-X", "POST", "-f", "ref=refs/tags/" + tag, "-f", "sha=" + sha)
    else:
        raise RuntimeError(f"cannot inspect release tag {tag}: {result.stderr.strip()}")


def verify(root, expected_sha):
    candidates = []
    suites = []
    for edition, options in EDITIONS.items():
        path = root / ("release-" + edition)
        manifest = json.loads((path / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
        if (manifest["repository"] != "NathanNeurotic/RiptOPL" or manifest["source_commit"] != expected_sha
                or manifest["dirty"] or manifest["edition"] != edition or manifest["compiler"] != "gcc"
                or manifest["sdk"] != "ps2build" or manifest["ra"] != options["ra"]
                or manifest.get("configuration", "standard") != "standard"):
            raise ValueError(f"invalid source/edition manifest for {edition}")
        suites.append((manifest["ps2build_version"], manifest["components"]))
        sums = {}
        for line in (path / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
            digest, name = line.split("  ", 1)
            if Path(name).name != name or name in sums:
                raise ValueError(f"unsafe or duplicate asset name: {name}")
            if hashlib.sha256((path / name).read_bytes()).hexdigest() != digest:
                raise ValueError(f"checksum mismatch: {edition}/{name}")
            sums[name] = digest
        actual = {p.name for p in path.iterdir() if p.is_file()}
        elf = "RIPTOPL-RA.ELF" if options["ra"] else "RIPTOPL.ELF"
        psu = "APP_RIPTOPL-RA.psu" if options["ra"] else "APP_RIPTOPL.psu"
        required = {elf, psu, "BUILD-MANIFEST.json", "RIPTOPL-src.zip", f"RIPTOPL-{options['title']}.zip"}
        if set(sums) != required or actual != required | {"SHA256SUMS.txt"}:
            raise ValueError(f"unexpected release inventory: {edition}")
        if sums[elf] != manifest["elf_sha256"]:
            raise ValueError("published ELF differs from the tested build")
        with zipfile.ZipFile(path / "RIPTOPL-src.zip") as source:
            if source.testzip() or source.comment.decode() != expected_sha:
                raise ValueError("source snapshot differs from the built commit")
        with zipfile.ZipFile(path / f"RIPTOPL-{options['title']}.zip") as kit:
            if kit.testzip() or json.loads(kit.read("BUILD-MANIFEST.json")) != manifest:
                raise ValueError("install kit manifest differs from the standalone manifest")
            app_name = "APP_RIPTOPL-RA" if options["ra"] else "APP_RIPTOPL"
            if kit.read(f"APPS/{app_name}/{elf}") != (path / elf).read_bytes() or kit.read(psu) != (path / psu).read_bytes():
                raise ValueError("standalone assets differ from the verified install kit")
        candidates.append((edition, options, path, sums))
    if any(suite != suites[0] for suite in suites[1:]):
        raise ValueError("editions resolved different SDK suites; rebuild them together before publication")
    return candidates



def release_notes(options, sha, neutrino, repo="NathanNeurotic/RiptOPL"):
    """Keep release changes visible; installation detail belongs in the guides."""
    base = f"https://github.com/{repo}"
    history = git("log", "--no-merges", "-10", "--format=%H%x00%s", sha, "--")
    changes = []
    for record in history.splitlines():
        commit, title = record.split("\x00", 1)
        title = re.sub(r"([\\`*_[\]<>])", r"\\\1", title)
        changes.append(f"- {title} ([{commit[:8]}]({base}/commit/{commit}))")
    if not changes:
        raise ValueError("Release commit has no changelog entries")
    elf = "RIPTOPL-RA.ELF" if options["ra"] else "RIPTOPL.ELF"
    psu = "APP_RIPTOPL-RA.psu" if options["ra"] else "APP_RIPTOPL.psu"
    downloads = f"{base}/releases/download/{options['tag']}"
    return (
        f"**RiptOPL {options['title']}** - Development build [{sha[:8]}]({base}/commit/{sha}).\n\n"
        "## Changes\n\n" + "\n".join(changes)
        + f"\n\n[Full history]({base}/commits/{sha})\n\n"
        "## Downloads\n\n"
        f"- [Full install kit]({downloads}/RIPTOPL-{options['title']}.zip) - loader and companions.\n"
        f"- [Loader ELF]({downloads}/{elf}) - update an existing installation.\n"
        f"- [Memory-card app]({downloads}/{psu}) - PSU import.\n\n"
        f"Included build: {neutrino} -- Neutrino.\n\n"
        "[Installation and guides](https://nathanneurotic.github.io/RiptOPL/) - "
        f"[Wiki]({base}/wiki)\n"
    )


def latest_release_arg(edition):
    """Only IncebtIon is presented as GitHub's Latest release."""
    return "--latest=" + ("true" if edition == "incebtion" else "false")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--check", action="store_true", help="verify only; never contact GitHub")
    args = parser.parse_args()
    sha = git("rev-parse", "HEAD")
    candidates = verify(args.artifacts, sha)
    if args.check:
        print(f"Verified all three channels at {sha}")
        return
    repo = "NathanNeurotic/RiptOPL"
    if not publication_allowed(os.environ, sha):
        raise ValueError("publication requires an integration push or explicit dispatch at rebuild/main")
    current = json.loads(gh("api", f"repos/{repo}/branches/rebuild/main"))["commit"]["sha"]
    if current != sha:
        raise ValueError("integration branch advanced during this run; publish its new tested head")
    for edition, options, path, sums in candidates:
        tag = options["tag"]
        manifest = json.loads((path / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
        neutrino = manifest["neutrino_archive"]["filename"].removeprefix("neutrino_").removesuffix(".7z")
        notes = release_notes(options, sha, neutrino, repo)
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".md", delete=False) as stream:
            stream.write(notes)
            notes_path = Path(stream.name)
        try:
            exists = subprocess.run(["gh", "release", "view", tag, "--repo", repo], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
            assets = [str(p) for p in sorted(path.iterdir()) if p.is_file()]
            if exists:
                gh("release", "upload", tag, "--repo", repo, "--clobber", *assets)
            else:
                gh("release", "create", tag, "--repo", repo, "--target", sha, "--draft", "--title", options["title"], "--notes-file", str(notes_path), *assets)
            release = json.loads(gh("release", "view", tag, "--repo", repo, "--json", "id,assets"))
            remote = {a["name"]: a for a in release["assets"]}
            for asset in assets:
                file = Path(asset)
                digest = hashlib.sha256(file.read_bytes()).hexdigest()
                item = remote.get(file.name, {})
                if item.get("size") != file.stat().st_size or item.get("digest") != "sha256:" + digest:
                    raise ValueError(f"GitHub did not retain the verified asset: {tag}/{file.name}")
            update_tag(repo, tag, sha)
            gh("release", "edit", tag, "--repo", repo, "--title", options["title"], "--notes-file", str(notes_path),
               "--draft=false", "--prerelease=false", latest_release_arg(edition))
            reference = json.loads(gh("api", f"repos/{repo}/git/ref/tags/{tag}"))
            if reference["object"]["sha"] != sha:
                raise ValueError(f"release tag drifted: {tag}")
            print(f"Published and verified {tag} at {sha}")
        finally:
            notes_path.unlink()


if __name__ == "__main__":
    main()
