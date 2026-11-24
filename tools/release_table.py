"""The download tables of every RiptOPL release description (rolling, rolling-ra, rolling-lite, stable).

Built from the files a release actually carries, so the description cannot drift from its assets: every
file gets a row and a link, and a file this module cannot describe stops the publication instead of being
left out. Loaders share one row per build (packed and unpacked, ELF, memory-card PSU and, for RiptOPLite
RetroAchievements, the ZIP with its runtime); every other file gets a row of its own.
"""
import re

from build_edition import LEGACY_UNPACKED, UNPAKT
from lite_devices import DEVICES

LOADERS = {
    "RIPTOPL": "RiptOPL, the standard loader.",
    "RIPTOPL-RA": "RiptOPL with RetroAchievements.",
    "RIPTOPL-PADEMU0": "Pad emulation off.",
    "RIPTOPL-DS5": "USB DualSense controller support.",
    "RIPTOPL-EXTRAS": "Right-to-left languages and in-game screenshots.",
    "RIPTOPL-EXTRAS-DS5": "Extras and DualSense together.",
    "RIPTOPL-RA-PADEMU0": "RetroAchievements, pad emulation off.",
    **{f"RIPTOPLITE-{d['title']}": f"RiptOPLite: {d['title']} games only." for d in DEVICES.values()},
    **{f"RIPTOPLITE-RA-{d['title']}": f"RiptOPLite RetroAchievements: {d['title']} games only. Take the ZIP, "
       "it carries the RA runtime." for d in DEVICES.values()},
}

OTHER = (
    (r"RIPTOPL\.zip", "Full install kit: the loader in its folder, Neutrino and every companion file."),
    (r"RIPTOPL-RetroAchievements\.zip", "RetroAchievements full install kit, with its RA runtime."),
    (r"RIPTOPL-VARIANTS\.zip", "Pad emulation off, DualSense and extras builds, packed and unpacked, each in its "
     "own folder with a PSU."),
    (r"RIPTOPL-RA-VARIANTS\.zip", "RetroAchievements with pad emulation off, and the RA loaders unpacked, each in "
     "its own folder with a PSU."),
    (r"RIPTOPL-DEBUG\.zip", "Debug and diagnostic builds for testers. No Check for Updates."),
    (r"RIPTOPL-BUGREPORT\.zip", "RiptOPL will not start or freezes? This build shows each start-up step and writes "
     "a report; its README says what to send. No Check for Updates."),
    (r"RIPTOPL(-RA|-LITE)?-LANGS\.zip", "Translations and their fonts."),
    (r"RIPTOPL(-RA|-LITE)?-src\.zip", "The exact source code of this build."),
    (r"BUILD-MANIFEST(-RA|-LITE)?\.json", "How it was built: commit, SDK, compiler and every file's hash."),
    (r"LICENSE-USMB2(-RA|-LITE)?\.txt", "usmb2 licence (MIT): every loader embeds usmb2 by Ronnie Sahlberg."),
    (r"LICENSE-MBEDTLS(-RA|-LITE)?\.txt", "Mbed TLS licence (Apache-2.0)."),
    (r"SHA256SUMS\.txt", "SHA-256 of every file here. Check for Updates reads it."),
    (r"RIPTOPL-STABLE\.txt", "Marks this release as stable for Check for Updates."),
)


def link(downloads, name, text=None):
    return f"[{text or name}]({downloads}/{name})"


def unversioned(name, version):
    return name.replace(f"-{version}.zip", ".zip")


def loader_part(name, version):
    """(build, unpacked, kind) for a loader file, else None."""
    plain = unversioned(name, version)
    if plain in LEGACY_UNPACKED:
        return None
    for suffix, kind in ((".ELF", "ELF"), (".psu", "PSU"), (".zip", "ZIP")):
        if plain.endswith(suffix):
            stem = plain[:-len(suffix)]
            if kind == "PSU":
                if not stem.startswith("APP_"):
                    return None
                stem = stem[len("APP_"):]
            unpacked = stem.endswith(UNPAKT)
            build = stem.removesuffix(UNPAKT)
            if build in LOADERS and (kind != "ZIP" or build.startswith("RIPTOPLITE-RA-")):
                return build, unpacked, kind
            return None
    return None


def describe(name, version):
    """(position, text): files are listed in OTHER's order, the old unpacked names last."""
    plain = unversioned(name, version)
    if plain in LEGACY_UNPACKED:
        return len(OTHER), (f"The same build as {LEGACY_UNPACKED[plain]}, under its old name, so unpacked loaders "
                            "installed before the rename still update.")
    for position, (pattern, text) in enumerate(OTHER):
        if re.fullmatch(pattern, plain):
            return position, text
    raise ValueError(f"no release-table description for {name}")


KIT_WHY = ("Not sure what to pick? Take this one. It has everything ready to copy, and the rest of this page "
           "is optional.")


def tables(names, downloads, version, kit, recommended=(), advice="", kit_why=KIT_WHY):
    """The Recommended list, then the loader table and the other-files table, each folded away, as Markdown.

    kit: the full install kit, always the first recommendation: a file in names, or the https link of the kit
    in another release (RiptOPLite has none of its own). recommended: [(asset name, why)], each a file in names;
    advice: a line after them. Every name gets a link."""
    names = sorted(set(names))
    if kit.startswith("https://"):
        kit_link = f"[{kit.rsplit('/', 1)[1]}]({kit})"
    elif kit in names:
        kit_link = link(downloads, kit)
    else:
        raise ValueError(f"the full kit is not in the release: {kit}")
    out = ["## Recommended downloads\n", f"- **{kit_link}** - {kit_why}"]
    for name, why in recommended:
        if name not in names:
            raise ValueError(f"recommended download is not in the release: {name}")
        out.append(f"- **{link(downloads, name)}** - {why}")
    if advice:
        out += ["", advice]
    builds, others = {}, []
    for name in names:
        part = loader_part(name, version)
        if part is None:
            others.append((*describe(name, version), name))
        else:
            build, unpacked, kind = part
            builds.setdefault(build, {})[(unpacked, kind)] = name
    order = list(LOADERS)
    rows = []
    for build in sorted(builds, key=order.index):
        files = builds[build]

        def cell(unpacked):
            return " · ".join(link(downloads, files[(unpacked, kind)], kind)
                              for kind in ("ELF", "PSU", "ZIP") if (unpacked, kind) in files) or "-"
        rows.append(f"| {build} | {LOADERS[build]} | {cell(False)} | {cell(True)} |")
    # Folded (GitHub renders Markdown inside <details> when blank lines surround it): the kit above is
    # enough for most people, and the full list stays one click away.
    if rows:
        out += ["", "<details>", f"<summary><strong>Loaders</strong> ({len(rows)} builds, packed and unpacked)</summary>",
                "",
                "Each loader goes in its own folder, `APP_` plus its ELF name (`APP_RIPTOPL-DS5/RIPTOPL-DS5.ELF`); "
                "the PSU makes that folder on a memory card. Unpacked (UNPAKT) loaders skip the decompression step: "
                "try one if a packed loader shows a black screen from your launcher. Check for Updates keeps every "
                "loader on its own build, packed or unpacked.", "",
                "| Build | What it is | Packed | Unpacked |", "|---|---|---|---|", *rows, "", "</details>"]
    if others:
        out += ["", "<details>", f"<summary><strong>Other files</strong> ({len(others)})</summary>", "",
                "| File | What it is |", "|---|---|",
                *(f"| {link(downloads, name)} | {text} |" for _, text, name in sorted(others)), "", "</details>"]
    return "\n".join(out) + "\n\n"
