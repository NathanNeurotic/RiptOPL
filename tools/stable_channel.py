"""Which release a build belongs to: a stable release (RIPTOPL_STABLE_VERSION=1.3.0, set by
rolling-release.yml's stable_version input) or the rolling channels (unset).

Shared by tools/assets.py (the version string) and tools/build_edition.py (Check for Updates and the
build manifest). No side effects on import."""
import os
import re

STABLE_VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")


def stable_version():
    """"1.3.0" while a stable release is built, else ""."""
    stable = os.environ.get("RIPTOPL_STABLE_VERSION", "").strip()
    if stable and not STABLE_VERSION.fullmatch(stable):
        raise SystemExit(f"RIPTOPL_STABLE_VERSION must look like 1.3.0, not {stable!r}")
    return stable


def release_channel():
    """"stable" or "rolling"."""
    return "stable" if stable_version() else "rolling"
