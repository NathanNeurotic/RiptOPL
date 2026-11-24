#!/usr/bin/env python3
"""Publish one versioned stable RiptOPL release (rolling-release.yml, stable_version input).

A stable release carries every build the three rolling channels carry, from one commit, in one
GitHub release tagged v<version> (v1.3.0):

* RiptOPL (LLVM): install kit, ELF, PSU, VARIANTS, DEBUG and BUGREPORT bundles, languages, source;
* RetroAchievements (GCC): install kit, ELF, PSU, RA-VARIANTS, languages, source;
* RiptOPLite and RiptOPLite RA: one ELF per device, and the RA ZIPs with their runtime;
* every ELF Check for Updates downloads, under the same names as the rolling channels, so each build
  replaces itself with the same variant (Lite USB with Lite USB, RA with RA, PADEMU0 with PADEMU0).

Every build was made with RIPTOPL_STABLE_VERSION set (tools/stable_channel.py): its version reads
v1.3.0-<build>, and its Check for Updates reads only GitHub's Latest release and only one that lists
RIPTOPL-STABLE.txt (src/ui/selfupdate.cpp). This script makes the new release Latest and, from then
on, every rolling channel a pre-release, so Latest can only ever be a stable release.

Nothing here moves or rewrites an existing stable release: a version that is already published, or
not newer than the current stable release, is refused.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from build_configuration import CONFIGURATIONS
from build_edition import (EDITIONS, LITE, LITES, LITE_DEVICES, edition_display_name, elf_name, git,
                           kit_archive_name, publishes_unpacked, update_asset, update_channel)
from stable_channel import STABLE_VERSION
import publish_editions as rolling

REPO = "NathanNeurotic/RiptOPL"
ROLLING_TAGS = ("rolling", "rolling-ra", "rolling-lite")
MARKER = "RIPTOPL-STABLE.txt"
# src/ui/selfupdate.cpp SU_SUMS_MAX: the updater refuses a longer SHA256SUMS.txt.
SUMS_LIMIT = 32 * 1024
# Where each channel's files land in the one release, and the label a colliding name takes.
LABELS = {"riptopl": "", "ra": "RA", LITE: "LITE"}

MARKER_TEXT = """RiptOPL {version} (stable)

This file marks a stable RiptOPL release. Check for Updates in a stable build only installs
from a release that carries it, and only from GitHub's Latest release, which is always the
newest stable one. Stable builds never update from the rolling (development) pre-releases.

Every build in this release updates to the same build: RiptOPL to RiptOPL, RetroAchievements
to RetroAchievements, each RiptOPLite device to the same device, each variant to that variant.
"""


def labelled(name, label):
    """RIPTOPL-src-v1.3.0-4370.zip -> RIPTOPL-RA-src-v1.3.0-4370.zip; LICENSE-USMB2.txt -> LICENSE-USMB2-RA.txt."""
    if name.startswith("RIPTOPL-"):
        return f"RIPTOPL-{label}-{name[len('RIPTOPL-'):]}"
    stem, dot, suffix = name.rpartition(".")
    return f"{stem}-{label}.{suffix}" if dot else f"{name}-{label}"


def expected_update_assets():
    """Every ELF a stable build's Check for Updates asks for (build_edition.update_channel)."""
    names = set()
    for edition in EDITIONS:
        for configuration in CONFIGURATIONS:
            if update_channel(edition, configuration) is None:
                continue
            names.add(update_asset(edition, configuration))
            if publishes_unpacked(edition, configuration):
                names.add(update_asset(edition, configuration, unpacked=True))
    for edition in LITES:
        for device in LITE_DEVICES:
            names.add(update_asset(edition, device=device))
    return names


def merge(candidates, root, version):
    """One folder holding every channel's files. Same-named files that are byte-identical are kept once;
    others take their channel's label. An ELF or PSU is never renamed: the updater asks for it by name."""
    out = root / "release-stable"
    out.mkdir(parents=True, exist_ok=False)
    origin = {}
    for edition, _, path, _ in candidates:
        label = LABELS[edition]
        for file in sorted(p for p in path.iterdir() if p.is_file()):
            name = file.name
            if name == "SHA256SUMS.txt":
                continue
            if name == "BUILD-MANIFEST.json" and label:
                name = f"BUILD-MANIFEST-{label}.json"
            if (out / name).exists():
                if (out / name).read_bytes() == file.read_bytes():
                    continue
                if name.endswith((".ELF", ".psu")) or not label:
                    raise ValueError(f"{name} differs between {origin[name]} and {edition}")
                name = labelled(name, label)
                if (out / name).exists():
                    raise ValueError(f"{name} collides twice")
            shutil.copyfile(file, out / name)
            origin[name] = edition
    (out / MARKER).write_text(MARKER_TEXT.format(version=version), encoding="utf-8")
    sums = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n"
                   for p in sorted(out.iterdir()) if p.is_file())
    (out / "SHA256SUMS.txt").write_text(sums, encoding="utf-8")
    return out


def revision_in(text):
    """src/platform/selfupdate.c revisionIn: skip "v" and the dotted version, then the first all-digit part."""
    i = 1 if text.startswith("v") else 0
    if i >= len(text) or not text[i].isdigit():
        return -1
    while i < len(text) and (text[i].isdigit() or text[i] == "."):
        i += 1
    for part in text[i:].split("-")[1:] if text[i:].startswith("-") else []:
        if part.isdigit():
            return int(part[:9])
    return -1


def updater_view(text, name):
    """What src/platform/selfupdate.c suParseSums reads: (sha256 hex, build number) or None."""
    found, rev = None, -1
    for line in text.splitlines():
        if len(line) < 66 or line[64:66] != "  ":
            continue
        digest, listed = line[:64], line[66:]
        if listed == name:
            found = digest
        elif listed.endswith(".zip"):
            starts = [i + 1 for i in range(len(listed) - 2) if listed[i] == "-" and listed[i + 1] == "v" and listed[i + 2].isdigit()]
            if starts:
                number = revision_in(listed[starts[-1]:-4])
                if number >= 0:
                    if rev >= 0 and number != rev:
                        return None  # one publication carries one version
                    rev = number
    return None if found is None or rev < 0 else (found, rev)


def check_release(out, version):
    """The checks a stable build's updater depends on, before anything reaches GitHub."""
    text = (out / "SHA256SUMS.txt").read_text(encoding="utf-8")
    if len(text.encode()) > SUMS_LIMIT:
        raise ValueError(f"SHA256SUMS.txt is {len(text.encode())} bytes; the updater reads at most {SUMS_LIMIT}")
    names = {line.split("  ", 1)[1] for line in text.splitlines()}
    if names != {p.name for p in out.iterdir() if p.is_file()} - {"SHA256SUMS.txt"}:
        raise ValueError("stable checksums do not list exactly the release's files")
    build = int(version.rsplit("-", 1)[1])
    for name in sorted(expected_update_assets() | {MARKER}):
        view = updater_view(text, name)
        if view is None or view[1] != build:
            raise ValueError(f"Check for Updates would not find {name} at build {build}")
        if hashlib.sha256((out / name).read_bytes()).hexdigest() != view[0]:
            raise ValueError(f"checksum mismatch: {name}")


def stable_release_version(candidates, stable):
    version = rolling.release_version(candidates)
    if not re.fullmatch(rf"v{re.escape(stable)}-[0-9]+", version):
        raise ValueError(f"builds carry {version}, not a v{stable} stable build")
    return version


def link(downloads, name, text=None):
    return f"[{text or name}]({downloads}/{name})"


def changes(sha, tag, base):
    """Commits since the previous stable release, or the latest ones before the first."""
    previous = [t for t in git("tag", "--list", "v*.*.*", "--merged", sha, "--sort=-v:refname").splitlines()
                if rolling.STABLE_TAG.fullmatch(t) and t != tag]
    span = f"{previous[0]}..{sha}" if previous else sha
    history = git("log", "--no-merges", "-41", "--format=%H%x00%s", span, "--")
    lines = []
    for record in history.splitlines()[:40]:
        commit, title = record.split("\x00", 1)
        title = re.sub(r"([\\`*_[\]<>])", r"\\\1", title)
        lines.append(f"- {title} ([{commit[:8]}]({base}/commit/{commit}))")
    if not lines:
        raise ValueError("Release commit has no changelog entries")
    if previous:
        more = f"[Everything since {previous[0]}]({base}/compare/{previous[0]}...{sha})"
        heading = f"## Changes since {previous[0]}"
    else:
        more = f"[Full history]({base}/commits/{sha})"
        heading = "## Latest changes\n\nThe first stable release. The newest changes:"
    if len(history.splitlines()) > 40:
        lines.append("- ...")
    return f"{heading}\n\n" + "\n".join(lines) + f"\n\n{more}\n"


def release_notes(stable, version, sha, out, neutrino, ra_neutrino, repo=REPO):
    """A table first, so a new user picks one download and can skip the rest."""
    base = f"https://github.com/{repo}"
    downloads = f"{base}/releases/download/v{stable}"
    v = lambda name: rolling.versioned(name, version)  # noqa: E731
    present = {p.name for p in out.iterdir()}
    kit, ra_kit = v(kit_archive_name(EDITIONS["riptopl"])), v(kit_archive_name(EDITIONS["ra"]))

    def maybe(name, text=None):
        if name not in present:
            raise ValueError(f"release notes link a missing file: {name}")
        return link(downloads, name, text)

    lite_rows = "".join(
        f"| {d['title']} | {maybe(elf_name(device))} | {maybe(v(elf_name(device, True).replace('.ELF', '.zip')))} |\n"
        for device, d in LITE_DEVICES.items())
    variants = sorted(n for n in present if n.endswith(".ELF")
                      and n not in {"RIPTOPL.ELF", "RIPTOPL-RA.ELF"} and not n.startswith("RIPTOPLITE-"))
    sources = sorted(n for n in present if "-src" in n or n.startswith("BUILD-MANIFEST") or n.startswith("LICENSE"))
    return (
        f"**RiptOPL {stable}** (`{version}`) - stable release, built from [{sha[:8]}]({base}/commit/{sha}).\n\n"
        "## New here? Pick one\n\n"
        "| I want | Download |\n|---|---|\n"
        f"| **RiptOPL, everything ready to copy (most people)** | **{maybe(kit)}** |\n"
        f"| RiptOPL with RetroAchievements | {maybe(ra_kit)} |\n"
        "| A smaller loader for a console that plays from one device only | RiptOPLite, in the table below |\n"
        "| To update a loader I already have | Check for Updates in its menu, or one ELF below |\n\n"
        "Everything else here is optional.\n\n"
        "## Updating\n\n"
        "Check for Updates in this release installs only the next **stable** release, never a rolling "
        "(development) build, and always the same build you run: RiptOPL stays RiptOPL, RetroAchievements "
        "stays RetroAchievements, a RiptOPLite build keeps its device, a variant keeps its variant.\n\n"
        "| Loader | ELF | Memory-card app |\n|---|---|---|\n"
        f"| RiptOPL | {maybe('RIPTOPL.ELF')} | {maybe('APP_RIPTOPL.psu')} |\n"
        f"| RetroAchievements | {maybe('RIPTOPL-RA.ELF')} | {maybe('APP_RIPTOPL-RA.psu')} |\n\n"
        "## RiptOPLite: one loader per game device\n\n"
        "RiptOPL with only that device's drivers, so it is smaller and starts faster. The ELF replaces "
        "RIPTOPL.ELF in an existing install; each RetroAchievements ZIP carries the RA runtime it needs, and "
        "its README says where it goes.\n\n"
        "| Device | RiptOPL | RetroAchievements |\n|---|---|---|\n" + lite_rows + "\n"
        "## Extras\n\n"
        f"- {maybe(v('RIPTOPL-BUGREPORT.zip'), 'Bug-report build')} - RiptOPL will not start or freezes? This "
        "build shows each start-up step on screen and writes a report; its README says what to send.\n"
        f"- {maybe(v('RIPTOPL-VARIANTS.zip'), 'RiptOPL variants')} - pad emulation off, DualSense and extras builds.\n"
        f"- {maybe(v('RIPTOPL-DEBUG.zip'), 'RiptOPL debug builds')} - debug and diagnostic builds.\n"
        f"- {maybe(v('RIPTOPL-RA-VARIANTS.zip'), 'RetroAchievements variants')} - pad emulation off, and the loaders unpacked.\n"
        f"- {maybe(v('RIPTOPL-LANGS.zip'), 'Languages')} - translations and their fonts.\n"
        f"- Single variant ELFs, which Check for Updates uses: {len(variants)} files named "
        "`RIPTOPL-<VARIANT>.ELF` and `RIPTOPL-RA-<VARIANT>.ELF`. The bundles above hold the same builds.\n\n"
        "## Source, licences and checksums\n\n"
        + "".join(f"- {maybe(n)}\n" for n in sources) +
        f"- {maybe('SHA256SUMS.txt')} - every file's SHA-256.\n"
        f"- [Mbed TLS licence]({base}/blob/{sha}/third_party/mbedtls/LICENSE) - every loader here links "
        "[Mbed TLS](https://github.com/Mbed-TLS/mbedtls), used under the Apache-2.0 licence. Every loader "
        "embeds [usmb2](https://github.com/sahlberg/usmb2) by [Ronnie Sahlberg](https://github.com/sahlberg), "
        "used under the MIT licence (LICENSE-USMB2.txt).\n\n"
        + rolling.included_neutrino(EDITIONS["riptopl"], neutrino)
        + rolling.included_neutrino(EDITIONS["ra"], ra_neutrino)
        + changes(sha, f"v{stable}", base) + "\n"
        "Rolling (development) builds continue as pre-releases: "
        f"[rolling]({base}/releases/tag/rolling), [rolling-ra]({base}/releases/tag/rolling-ra), "
        f"[rolling-lite]({base}/releases/tag/rolling-lite).\n\n"
        "[Installation and guides](https://nathanneurotic.github.io/RiptOPL/) - "
        f"[Wiki]({base}/wiki)\n")


def version_key(tag):
    return tuple(int(part) for part in tag[1:].split("."))


def current_stable(repo):
    result = subprocess.run(["gh", "api", f"repos/{repo}/releases/latest", "--jq", ".tag_name"],
                            capture_output=True, text=True)
    if result.returncode != 0:
        if "HTTP 404" in result.stderr:
            return None
        raise RuntimeError(f"cannot read the Latest release: {result.stderr.strip()}")
    tag = result.stdout.strip()
    return tag if rolling.STABLE_TAG.fullmatch(tag) else None


def release_state(repo, tag):
    """None (no release), "draft" or "published"."""
    result = subprocess.run(["gh", "release", "view", tag, "--repo", repo, "--json", "isDraft"],
                            capture_output=True, text=True)
    if result.returncode != 0:
        if "not found" in result.stderr.lower() or "HTTP 404" in result.stderr:
            return None
        raise RuntimeError(f"cannot inspect release {tag}: {result.stderr.strip()}")
    return "draft" if json.loads(result.stdout)["isDraft"] else "published"


def tag_exists(repo, tag):
    result = subprocess.run(["gh", "api", f"repos/{repo}/git/ref/tags/{tag}"], capture_output=True, text=True)
    if result.returncode == 0:
        return True
    if "HTTP 404" in result.stderr:
        return False
    raise RuntimeError(f"cannot inspect tag {tag}: {result.stderr.strip()}")


def preflight(repo, tag):
    """Refuse a published or tagged version and one not newer than the current stable release.
    A draft left by a failed attempt of this script (no tag) is removed so the cut can run again."""
    state = release_state(repo, tag)
    if state == "published" or tag_exists(repo, tag):
        raise ValueError(f"{tag} is already released or tagged; stable releases are never rewritten")
    latest = current_stable(repo)
    if latest is not None and version_key(latest) >= version_key(tag):
        raise ValueError(f"{tag} is not newer than the current stable release {latest}")
    if state == "draft":
        print(f"Removing the unpublished draft {tag} left by an earlier attempt")
        rolling.gh("release", "delete", tag, "--repo", repo, "--yes")


def publish(repo, tag, sha, title, notes, out):
    preflight(repo, tag)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".md", delete=False) as stream:
        stream.write(notes)
        notes_path = Path(stream.name)
    try:
        assets = [str(p) for p in sorted(out.iterdir()) if p.is_file()]
        rolling.upload_verified(repo, tag, sha, title, notes_path, assets, False)
        rolling.update_tag(repo, tag, sha)
        rolling.gh("release", "edit", tag, "--repo", repo, "--title", title, "--notes-file", str(notes_path),
                   "--draft=false", "--prerelease=false", "--latest=true")
    finally:
        notes_path.unlink()
    if json.loads(rolling.gh("api", f"repos/{repo}/git/ref/tags/{tag}"))["object"]["sha"] != sha:
        raise ValueError(f"release tag drifted: {tag}")
    for delay in (0, 5, 15, 30):
        time.sleep(delay)
        if current_stable(repo) == tag:
            break
    else:
        raise RuntimeError(f"{tag} is published but GitHub does not report it as Latest")
    # From now on Latest is only ever a stable release: the rolling channels become pre-releases
    # (publish_editions.py keeps them so on every later rolling publication).
    for channel in ROLLING_TAGS:
        if release_state(repo, channel) == "published":
            rolling.gh("release", "edit", channel, "--repo", repo, "--prerelease=true", "--latest=false")
    print(f"Published stable {tag} at {sha} as Latest; rolling channels are pre-releases")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--version", required=True, help="the stable version, 1.3.0")
    parser.add_argument("--check", action="store_true", help="verify only; never contact GitHub")
    args = parser.parse_args()
    stable = args.version.strip()
    if not STABLE_VERSION.fullmatch(stable):
        raise ValueError(f"stable version must look like 1.3.0, not {stable!r}")
    if os.environ.get("RIPTOPL_STABLE_VERSION", "").strip() != stable:
        raise ValueError("RIPTOPL_STABLE_VERSION must name the same version the builds were made for")
    sha = git("rev-parse", "HEAD")
    rolling.bundle_configurations(args.artifacts, sha, channel="stable")
    candidates = (rolling.fold_companions(rolling.verify(args.artifacts, sha, channel="stable"))
                  + rolling.lite_candidates(args.artifacts, sha, channel="stable"))
    version = stable_release_version(candidates, stable)
    rolling.name_assets(candidates, version)
    neutrino = {edition: rolling.bundled_neutrino(json.loads((path / "BUILD-MANIFEST.json").read_text(encoding="utf-8")))
                for edition, _, path, _ in candidates if edition in EDITIONS}
    out = merge(candidates, args.artifacts, version)
    check_release(out, version)
    notes = release_notes(stable, version, sha, out, neutrino["riptopl"], neutrino["ra"])
    print(f"Verified stable v{stable} ({version}) at {sha}: {len(list(out.iterdir()))} files")
    if args.check:
        return
    environment = os.environ
    if not (rolling.publication_allowed(environment, sha) and environment.get("GITHUB_EVENT_NAME") == "workflow_dispatch"):
        raise ValueError("a stable release is published only by a dispatch of rebuild/main")
    if json.loads(rolling.gh("api", f"repos/{REPO}/branches/rebuild/main"))["commit"]["sha"] != sha:
        raise ValueError("rebuild/main moved during the build; dispatch the stable release again")
    publish(REPO, f"v{stable}", sha, f"RiptOPL {stable}", notes, out)


if __name__ == "__main__":
    main()
