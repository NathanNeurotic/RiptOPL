#!/usr/bin/env python3
"""NULD (formerly ORBIT) Neutrino Watch: keep the RA kits' Neutrino runtime level with NULD's latest release.

The RA kits' neutrino/ comes from danielnuld/nuld (ORBIT was renamed NULD at v1.2; the repository was
danielnuld/ps2-launcher), pinned by tag and archive SHA-256 in third_party/orbit-neutrino/release.json.
tools/package_edition.py refuses any other archive, so the pin has to move for a new runtime to ship.

NULD releases often change only its launcher (v1.3 did): the neutrino/ folder RiptOPL extracts stays
byte-identical. This watch therefore compares the neutrino/ payload itself, not the tag:

  check   newest stable release against the pin -> status "current", "new" or "unverified"
  update  pinned and newest archives side by side -> status "launcher-only" (nothing to do) or
          "updated" (release.json and the docs that link the release rewritten, commit message written)

"update" refuses (non-zero exit, nothing written) when the new archive does not match GitHub's digest,
when stage_orbit (the packer's own checks: required files, -ra= handoff, safe paths) rejects it, or when
NULD's agent source at the new tag no longer has the one-packet snapshot limit RiptOPL hard-codes
(RA_ORBIT_SNAPSHOT_MAX). Those need a code change here, not just a new pin.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PIN = ROOT / "third_party/orbit-neutrino/release.json"
NOTES = "third_party/orbit-neutrino/README.md"
# Files that link the pinned release; "update" moves their links (and the tag on those lines) to the new one.
# docs-sync.yml reads the pin itself: a GITHUB_TOKEN push may not change workflow files.
DOC_SITES = (NOTES, "README.md", "docs/RETROACHIEVEMENTS.md")
# NULD's agent source: snapshot values per packet = PAYLOAD - header room (raagent/src/main.c).
AGENT_SOURCE = "third_party/neutrino-igr/raagent/src/main.c"
# Written by stage_orbit from the pin itself, so they always differ between pins; not part of the payload.
GENERATED = {"ORBIT-RA.json", "ORBIT-RA.txt", "RA-EDITION-NEUTRINO.txt"}

# ORBIT was renamed NULD at v1.2: the archive is NULD-<tag>.zip from then on.
ARCHIVE_PREFIXES = ("ORBIT-", "NULD-")


def orbit_assets(release):
    return [a for a in release.get("assets", [])
            if a.get("name", "").startswith(ARCHIVE_PREFIXES) and a.get("name", "").endswith(".zip")]


def release_repository(release, fallback):
    """owner/name the release lives in now, read from its page address (the pin may name an old one)."""
    match = re.match(r"https://github\.com/([^/]+/[^/]+)/releases/", release.get("html_url") or "")
    return match.group(1) if match else fallback


def digest_of(asset):
    value = asset.get("digest") or ""
    return value[len("sha256:"):].lower() if value.startswith("sha256:") else ""


def pick_asset(release, pin):
    assets = orbit_assets(release)
    return next((a for a in assets if a["name"] == pin["asset"]), assets[0] if assets else None)


def decide(release, pin, asset_sha256=""):
    """Return (status, reason, asset) for the newest release against the pin."""
    asset = pick_asset(release, pin)
    if asset is None:
        return "unknown", "The newest release carries no NULD-*.zip.", None
    if release.get("tag_name") != pin["tag"]:
        return "new", "NULD published " + release["tag_name"] + " (pinned " + pin["tag"] + ").", asset
    if asset["name"] != pin["asset"]:
        return "new", "The pinned release no longer carries " + pin["asset"] + ".", asset
    digest = asset_sha256.lower() or digest_of(asset)
    if not digest:
        return "unverified", "GitHub reports no digest for " + pin["asset"] + ".", asset
    if digest != pin["sha256"].lower():
        return "new", "The pinned tag's archive was replaced.", asset
    return "current", "The pinned runtime is still NULD's latest release.", asset


def staging():
    sys.path.insert(0, str(ROOT / "tools"))
    import package_edition  # noqa: E402  (tools/ is not a package)
    return package_edition


def payload(archive, pin):
    """{path: sha256} of the neutrino/ folder the RA kit would ship, through the packer's own checks."""
    with tempfile.TemporaryDirectory() as temp:
        destination = Path(temp) / "neutrino"
        staging().stage_orbit(archive, destination, pin)
        return {p.relative_to(destination).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(destination.rglob("*")) if p.is_file()
                and p.relative_to(destination).as_posix() not in GENERATED}


def agent_snapshot_bytes(source):
    """The agent's values-per-packet limit from raagent/src/main.c, or None if it cannot be read."""
    defines = dict(re.findall(r"^\s*#define\s+(PAYLOAD|AGENT_BYTES)\s+([^/\n]+?)\s*(?://.*)?$", source, re.M))
    expression = defines.get("AGENT_BYTES", "")
    if re.fullmatch(r"\d+", expression):
        return int(expression)
    match = re.fullmatch(r"\(\s*PAYLOAD\s*-\s*(\d+)\s*\)", expression)
    if match and re.fullmatch(r"\d+", defines.get("PAYLOAD", "")):
        return int(defines["PAYLOAD"]) - int(match.group(1))
    return None


def new_pin(release, pin, asset, sha256, commit):
    updated = dict(pin)
    updated.update({"repository": release_repository(release, pin["repository"]), "tag": release["tag_name"],
                    "source_commit": commit, "asset": asset["name"], "sha256": sha256})
    return updated


def rewrite_docs(root, old, new, changed, today):
    """Move every link to the old pinned release to the new one; note the update in the runtime notes."""
    old_url = "https://github.com/" + old["repository"] + "/releases/tag/" + old["tag"]
    new_url = "https://github.com/" + new["repository"] + "/releases/tag/" + new["tag"]
    touched = []
    for relative in DOC_SITES:
        path = root / relative
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        # Swap the URL out first so the tag rename cannot touch it (v1.2 -> v1.2.1 must not give v1.2.1.1).
        hold = "\0NULD-RELEASE-URL\0"
        lines = [line.replace(old_url, hold).replace(old["tag"], new["tag"]).replace(hold, new_url)
                 if old_url in line else line for line in text.split("\n")]
        result = "\n".join(lines)
        if relative == NOTES:
            result = re.sub(r"Source: \[[^\]]*\]\(https://github\.com/[^)\s]+/tree/[0-9a-f]{40}/third_party/neutrino-igr\)",
                            "Source: [" + new["repository"] + " at " + new["source_commit"][:8] + "](https://github.com/"
                            + new["repository"] + "/tree/" + new["source_commit"] + "/third_party/neutrino-igr)",
                            result, count=1)
            note = (new["tag"] + " (updated automatically by NULD Neutrino Watch on " + today + ", from " + old["tag"]
                    + "): inside `neutrino/` these changed: " + ", ".join("`" + c + "`" for c in changed)
                    + ". The packer's checks and the " + str(new["snapshot_bytes_max"]) + "-byte snapshot limit "
                    "were re-checked by the watch. Hardware launch behavior is unverified.\n\n")
            marker = re.search(r"^v\d[^\n]*\(reviewed against", result, re.M)
            result = result[:marker.start()] + note + result[marker.start():] if marker else result + "\n" + note
        if result != text:
            path.write_text(result, encoding="utf-8")
            touched.append(relative)
    return touched


def update(release, pin, old_archive, new_archive, agent_source, commit, root=ROOT, today=None):
    """Return (status, message, touched files). Raises SystemExit when the new runtime must not ship."""
    asset = pick_asset(release, pin)
    if asset is None:
        raise SystemExit("The newest release carries no NULD-*.zip.")
    if not digest_of(asset):
        raise SystemExit("GitHub publishes no SHA-256 for " + asset["name"] + "; refusing to pin it.")
    sha256 = hashlib.sha256(Path(new_archive).read_bytes()).hexdigest()
    if digest_of(asset) != sha256:
        raise SystemExit("Downloaded " + asset["name"] + " is " + sha256 + ", but GitHub says " + digest_of(asset) + ".")
    if not re.fullmatch(r"[0-9a-f]{40}", commit or ""):
        raise SystemExit("No commit for tag " + release.get("tag_name", "?") + "; cannot pin its source.")
    candidate = new_pin(release, pin, asset, sha256, commit)
    try:
        before = payload(Path(old_archive), pin)
        after = payload(Path(new_archive), candidate)
    except ValueError as error:
        raise SystemExit("The packer refuses this runtime: " + str(error)) from None
    if before == after:
        return "launcher-only", (release["tag_name"] + ": neutrino/ is byte-identical to the pinned "
                                 + pin["tag"] + "'s; only NULD's launcher changed. Pin kept."), []
    limit = agent_snapshot_bytes(agent_source)
    if limit != pin["snapshot_bytes_max"]:
        raise SystemExit("NULD's agent at " + commit[:12] + " sends " + str(limit) + " bytes per snapshot, RiptOPL "
                         "expects " + str(pin["snapshot_bytes_max"]) + " (RA_ORBIT_SNAPSHOT_MAX); needs a code change.")
    changed = sorted(name for name in set(before) | set(after) if before.get(name) != after.get(name))
    (root / "third_party/orbit-neutrino/release.json").write_text(json.dumps(candidate, indent=2) + "\n",
                                                                  encoding="utf-8")
    touched = ["third_party/orbit-neutrino/release.json"]
    touched += rewrite_docs(root, pin, candidate, changed, today or datetime.date.today().isoformat())
    message = "\n".join([
        "RA kits: update NULD Neutrino RA runtime to " + candidate["tag"],
        "",
        "Automatic update by NULD Neutrino Watch. Inside neutrino/ these changed",
        "since " + pin["tag"] + ":",
        *("- " + name for name in changed),
        "",
        "The archive matches GitHub's published SHA-256, passes the packer's checks",
        "(required files, -ra= handoff, safe paths) and NULD's agent still sends",
        str(limit) + " bytes per snapshot. Not tested on a PS2.",
        "",
        "Archive: " + candidate["asset"] + " " + sha256,
        "Source: https://github.com/" + candidate["repository"] + "/tree/" + commit,
        "Release: " + release.get("html_url", ""),
    ])
    return "updated", message, touched


def main(argv=None):
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("check", "update"):
        sub = commands.add_parser(name)
        sub.add_argument("--release", required=True, type=Path)
        sub.add_argument("--pin", default=PIN, type=Path)
        sub.add_argument("--out", required=True, type=Path)
    check = commands.choices["check"]
    check.add_argument("--asset-sha256", default="")
    upd = commands.choices["update"]
    upd.add_argument("--old", required=True, type=Path, help="the pinned archive")
    upd.add_argument("--new", required=True, type=Path, help="the newest release's archive")
    upd.add_argument("--agent-source", required=True, type=Path, help=AGENT_SOURCE + " at the new tag")
    upd.add_argument("--tag-commit", required=True)
    upd.add_argument("--root", default=ROOT, type=Path)
    args = parser.parse_args(argv)

    release = json.loads(args.release.read_text(encoding="utf-8"))
    pin = json.loads(args.pin.read_text(encoding="utf-8"))
    args.out.mkdir(parents=True, exist_ok=True)
    if release.get("draft") or not release.get("tag_name"):
        print("::warning::The newest-release answer is not a published release; nothing to compare.")
        (args.out / "status").write_text("unknown", encoding="utf-8")
        return 0
    if args.command == "check":
        status, reason, asset = decide(release, pin, args.asset_sha256)
        if asset is not None:
            (args.out / "asset").write_text(asset["name"], encoding="utf-8")
        print("pinned " + pin["tag"] + ", latest " + release["tag_name"] + ": " + status + " (" + reason + ")")
    else:
        status, message, touched = update(release, pin, args.old, args.new,
                                          args.agent_source.read_text(encoding="utf-8", errors="replace"),
                                          args.tag_commit.strip(), args.root)
        if status == "updated":
            (args.out / "commit.txt").write_text(message + "\n", encoding="utf-8")
            print("Updated: " + ", ".join(touched))
        print(message.split("\n", 1)[0])
    (args.out / "status").write_text(status, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
