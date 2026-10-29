#!/usr/bin/env python3
"""Publish three verified ps2build kits from integration pushes or explicit dispatches."""
import argparse
import hashlib
import json
import os
import re
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import zipfile

from build_configuration import CONFIGURATIONS
from build_edition import EDITIONS, git

# The original's VARIANTS and DEBUG zips and its RA nopademu loader, published for every theme beside
# its edition's kit: configurations are never channels of their own.
# (name, configurations, release-notes description). docs/BUILD-CONFIGURATIONS.md has each one.
VARIANTS = ("pademu0", "ds5", "extras", "extras_ds5")
STANDARD_BUNDLES = (
    ("VARIANTS", VARIANTS, "pad emulation off, DualSense and extras builds"),
    ("DEBUG", tuple(c for c in CONFIGURATIONS if c != "standard" and c not in VARIANTS), "debug and diagnostic builds"),
)
RA_BUNDLES = (("RA-VARIANTS", ("pademu0",), "pad emulation off"),)


def bundles(options):
    return RA_BUNDLES if options["ra"] else STANDARD_BUNDLES


BUNDLE_README = ("RiptOPL {title} configuration builds. Each folder holds one {elf} and the manifest it was\n"
                 "built from; replace the ELF of an existing {title} install to use one. What each configuration\n"
                 "changes: https://github.com/NathanNeurotic/RiptOPL/blob/rebuild/main/docs/BUILD-CONFIGURATIONS.md\n")


# GitHub's release API intermittently answers 5xx. Calls are retried unless retry=False: a create that
# failed part-way has already made the release, so repeating it can only fail.
RETRY_DELAYS = (5, 15)


def gh(*args, retry=True):
    for delay in (*(RETRY_DELAYS if retry else ()), None):
        try:
            return subprocess.check_output(["gh", *args], text=True).strip()
        except subprocess.CalledProcessError:
            if delay is None:
                raise
            print(f"gh {' '.join(args[:2])} failed; retrying in {delay}s", file=sys.stderr)
            time.sleep(delay)


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


def add_sum(path, name, digest):
    """List name in path/SHA256SUMS.txt once, so a second run over the same folder stays valid."""
    sums = path / "SHA256SUMS.txt"
    lines = [line for line in sums.read_text(encoding="utf-8").splitlines() if line.split("  ", 1)[-1] != name]
    sums.write_text("".join(f"{line}\n" for line in [*lines, f"{digest}  {name}"]), encoding="utf-8")


def bundle_configurations(root, expected_sha):
    """Pack the configuration builds into their edition's release and add them to its checksums."""
    for edition, options in EDITIONS.items():
        for name, configurations, _ in bundles(options):
            path = root / ("release-" + edition)
            kit = json.loads((path / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
            elf_name = "RIPTOPL-RA.ELF" if EDITIONS[edition]["ra"] else "RIPTOPL.ELF"
            archive = path / f"RIPTOPL-{name}.zip"
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
                for configuration in configurations:
                    stage = root / f"variant-{edition}-{configuration}"
                    manifest = json.loads((stage / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))
                    elf = (stage / "RIPTOPL.ELF").read_bytes()
                    if (manifest["source_commit"] != expected_sha or manifest["dirty"] or manifest["edition"] != edition
                            or manifest["compiler"] != "gcc" or manifest["configuration"] != configuration
                            or manifest["ps2build_version"] != kit["ps2build_version"]
                            or manifest["components"] != kit["components"] or manifest["ports"] != kit["ports"]
                            or manifest["elf_sha256"] != hashlib.sha256(elf).hexdigest()):
                        raise ValueError(f"invalid configuration build: {edition}/{configuration}")
                    bundle.writestr(f"{configuration}/{elf_name}", elf)
                    bundle.writestr(f"{configuration}/BUILD-MANIFEST.json", json.dumps(manifest, indent=2) + "\n")
                bundle.writestr("README.txt", BUNDLE_README.format(title=EDITIONS[edition]["title"], elf=elf_name))
            add_sum(path, archive.name, hashlib.sha256(archive.read_bytes()).hexdigest())


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
        suites.append((manifest["ps2build_version"], manifest["components"], manifest["ports"]))
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
        required = {elf, psu, "BUILD-MANIFEST.json", "RIPTOPL-src.zip", "RIPTOPL-LANGS.zip", f"RIPTOPL-{options['title']}.zip"}
        required |= {f"RIPTOPL-{name}.zip" for name, _, _ in bundles(options)}
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



def companion_assets(options):
    """A companion's channel files under the names they take in its parent's channel."""
    elf = "RIPTOPL-RA.ELF" if options["ra"] else "RIPTOPL.ELF"
    psu = "APP_RIPTOPL-RA.psu" if options["ra"] else "APP_RIPTOPL.psu"
    label = options["label"]
    return {f"RIPTOPL-{options['title']}.zip": f"RIPTOPL-{options['title']}.zip",
            elf: elf.replace(".ELF", f"-{label}.ELF"), psu: psu.replace(".psu", f"-{label}.psu"),
            **{f"RIPTOPL-{name}.zip": f"RIPTOPL-{name}-{label}.zip" for name, _, _ in bundles(options)}}


def fold_companions(candidates):
    """Move each verified companion edition (one with "into") into its parent's channel."""
    parents = {edition: path for edition, _, path, _ in candidates}
    for _, options, path, sums in candidates:
        if "into" not in options:
            continue
        parent = parents[options["into"]]
        for source, target in companion_assets(options).items():
            if (parent / target).exists() and hashlib.sha256((parent / target).read_bytes()).hexdigest() != sums[source]:
                raise ValueError(f"companion asset collides with its channel: {target}")
            shutil.copyfile(path / source, parent / target)
            add_sum(parent, target, sums[source])
    return [candidate for candidate in candidates if "into" not in candidate[1]]


def release_version(candidates):
    """The version a download name ends with: the build version without its commit hash and build
    label (v1.2.0-Beta-3925-151fc05-PS2BUILD-Korium-GCC -> v1.2.0-Beta-3925). Every channel of one
    publication must carry the same one."""
    versions = set()
    for _, _, path, _ in candidates:
        full = json.loads((path / "BUILD-MANIFEST.json").read_text(encoding="utf-8"))["version"].split('"')[1]
        versions.add(re.sub(r"-[0-9a-f]{7}$", "", full.split("-PS2BUILD-", 1)[0]))
    if len(versions) != 1:
        raise ValueError(f"channels carry different versions: {sorted(versions)}")
    version = versions.pop()
    if not re.fullmatch(r"v[0-9][A-Za-z0-9.+-]*", version):
        raise ValueError(f"unsafe release version: {version}")
    return version


def versioned(name, version):
    """A download's published name: every archive ends with the version (RIPTOPL-Korium-v1.2.0-Beta-3925.zip).
    The ELF, PSU, manifest and checksums keep their names, so their links never change."""
    return f"{name.removesuffix('.zip')}-{version}.zip" if name.endswith(".zip") else name


def name_assets(candidates, version):
    """Give each channel's archives their versioned names, in its folder and its checksums."""
    for _, _, path, _ in candidates:
        sums = path / "SHA256SUMS.txt"
        lines = []
        for line in sums.read_text(encoding="utf-8").splitlines():
            digest, name = line.split("  ", 1)
            target = versioned(name, version)
            if target != name:
                if (path / target).exists():
                    raise ValueError(f"versioned asset already exists: {target}")
                (path / name).rename(path / target)
            lines.append(f"{digest}  {target}")
        sums.write_text("".join(f"{line}\n" for line in lines), encoding="utf-8")


def release_notes(options, sha, neutrino, version, repo="NathanNeurotic/RiptOPL"):
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
        f"**RiptOPL {options['title']}** `{version}` - Development build [{sha[:8]}]({base}/commit/{sha}).\n\n"
        "## Changes\n\n" + "\n".join(changes)
        + f"\n\n[Full history]({base}/commits/{sha})\n\n"
        "## Downloads\n\n"
        f"- [Full install kit]({downloads}/RIPTOPL-{options['title']}-{version}.zip) - loader and companions.\n"
        f"- [Loader ELF]({downloads}/{elf}) - update an existing installation.\n"
        f"- [Memory-card app]({downloads}/{psu}) - PSU import.\n"
        f"- [Languages]({downloads}/RIPTOPL-LANGS-{version}.zip) - translations and their fonts.\n"
        + "".join(f"- [{name.replace('-', ' ').title().replace('Ra ', 'RA ')}]({downloads}/RIPTOPL-{name}-{version}.zip) - {what}.\n"
                  for name, _, what in bundles(options))
        + "".join(f"- {other['label']} theme: " + ", ".join(f"[{what}]({downloads}/{versioned(name, version)})" for what, name in
                  zip(("install kit", "ELF", "app", *(what for _, _, what in bundles(other))),
                      companion_assets(other).values())) + ".\n"
                  for other in EDITIONS.values() if EDITIONS.get(other.get("into")) is options)
        + "\n"
        f"Included build: {neutrino} -- Neutrino.\n\n"
        "[Installation and guides](https://nathanneurotic.github.io/RiptOPL/) - "
        f"[Wiki]({base}/wiki)\n"
    )


def upload_verified(repo, tag, sha, title, notes_path, assets, exists):
    """Upload a channel's assets and confirm GitHub kept them. A published channel that fails part-way
    gets every clobbered file back and loses the files it added, so its downloads never mix two builds
    under the old tag."""
    with tempfile.TemporaryDirectory() as backup:
        replaced, added = [], []
        if exists:
            current = {a["name"] for a in json.loads(gh("release", "view", tag, "--repo", repo, "--json", "assets"))["assets"]}
            replaced = sorted(current & {Path(a).name for a in assets})
            added = sorted({Path(a).name for a in assets} - current)
            for name in replaced:
                gh("release", "download", tag, "--repo", repo, "--pattern", name, "--dir", backup, "--clobber")
        try:
            if exists:
                gh("release", "upload", tag, "--repo", repo, "--clobber", *assets)
            else:
                gh("release", "create", tag, "--repo", repo, "--target", sha, "--draft", "--title", title,
                   "--notes-file", str(notes_path), *assets, retry=False)
            release = json.loads(gh("release", "view", tag, "--repo", repo, "--json", "id,assets"))
            remote = {a["name"]: a for a in release["assets"]}
            for asset in assets:
                file = Path(asset)
                digest = hashlib.sha256(file.read_bytes()).hexdigest()
                item = remote.get(file.name, {})
                if item.get("size") != file.stat().st_size or item.get("digest") != "sha256:" + digest:
                    raise ValueError(f"GitHub did not retain the verified asset: {tag}/{file.name}")
        except (subprocess.CalledProcessError, ValueError):
            if replaced:
                gh("release", "upload", tag, "--repo", repo, "--clobber", *(str(Path(backup) / n) for n in replaced))
            for name in added:  # new to this channel: not part of the build it still serves
                try:
                    gh("release", "delete-asset", tag, name, "--repo", repo, "--yes")
                except subprocess.CalledProcessError:
                    pass  # best effort: the original failure is the one to report
            raise


def remove_stale_archives(repo, tag, published):
    """Archive names carry the version, so a new build's archives sit beside the last one's until those
    are deleted. Runs only after the new set is verified and its notes point at it; a failure leaves an
    extra old file, never a missing new one."""
    try:
        current = {a["name"] for a in json.loads(gh("release", "view", tag, "--repo", repo, "--json", "assets"))["assets"]}
    except subprocess.CalledProcessError:
        print(f"could not list {tag} to remove the previous build's archives", file=sys.stderr)
        return
    for name in sorted(current - set(published)):
        if name.startswith("RIPTOPL-") and name.endswith(".zip"):
            try:
                gh("release", "delete-asset", tag, name, "--repo", repo, "--yes")
            except subprocess.CalledProcessError:
                print(f"could not remove the previous build's {tag}/{name}", file=sys.stderr)


def latest_release_arg(edition):
    """Only IncebtIon is presented as GitHub's Latest release."""
    return "--latest=" + ("true" if edition == "incebtion" else "false")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--check", action="store_true", help="verify only; never contact GitHub")
    args = parser.parse_args()
    sha = git("rev-parse", "HEAD")
    bundle_configurations(args.artifacts, sha)
    candidates = fold_companions(verify(args.artifacts, sha))
    version = release_version(candidates)
    name_assets(candidates, version)
    if args.check:
        print(f"Verified all three channels at {sha} ({version})")
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
        neutrino = manifest["neutrino_archive"]["filename"].removeprefix("neutrino_").removesuffix(".7z").removesuffix(".zip")
        notes = release_notes(options, sha, neutrino, version, repo)
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".md", delete=False) as stream:
            stream.write(notes)
            notes_path = Path(stream.name)
        try:
            exists = subprocess.run(["gh", "release", "view", tag, "--repo", repo], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
            assets = [str(p) for p in sorted(path.iterdir()) if p.is_file()]
            upload_verified(repo, tag, sha, options["title"], notes_path, assets, exists)
            update_tag(repo, tag, sha)
            gh("release", "edit", tag, "--repo", repo, "--title", options["title"], "--notes-file", str(notes_path),
               "--draft=false", "--prerelease=false", latest_release_arg(edition))
            reference = json.loads(gh("api", f"repos/{repo}/git/ref/tags/{tag}"))
            if reference["object"]["sha"] != sha:
                raise ValueError(f"release tag drifted: {tag}")
            remove_stale_archives(repo, tag, [Path(a).name for a in assets])
            print(f"Published and verified {tag} at {sha}")
        finally:
            notes_path.unlink()


if __name__ == "__main__":
    main()
