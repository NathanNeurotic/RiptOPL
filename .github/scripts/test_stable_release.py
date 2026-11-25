"""The stable release pipeline (tools/publish_stable.py, tools/stable_channel.py, rolling-release.yml).

Synthetic builds exercise the publisher's checks and the release layout, not PS2 acceptance:
* every build carries the stable channel and version, and the two publishers refuse each other's builds;
* one release holds every channel's files, and Check for Updates in every stable build finds its own
  ELF (same variant) and the stable marker at the release's build number;
* the notes open with a table and the recommended downloads;
* the rolling channels become pre-releases only once a stable release is Latest;
* a stable build's updater reads only Latest, and only a release listing the stable marker.
"""
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from types import SimpleNamespace
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import build_edition as editions  # noqa: E402
import lite_devices as lite  # noqa: E402
import publish_editions  # noqa: E402
import publish_lite  # noqa: E402
import publish_stable  # noqa: E402
import stable_channel  # noqa: E402

SHA = "1234567890abcdef" * 2 + "12345678"
SUITE = {"ps2build_version": "test", "components": {"ps2sdk": "abc"}, "ports": {}}
PIN = json.loads((ROOT / "third_party/orbit-neutrino/release.json").read_text(encoding="utf-8"))


def unpacked_elf():
    header = bytearray(b"\x7fELF\x01\x01\x01" + bytes(9))
    header += struct.pack("<HHIIIIIHHHHHH", 2, 8, 1, 0x00100000, 52, 0, 0, 52, 32, 1, 40, 0, 0)
    header += struct.pack("<8I", 1, 0x80, 0x00100000, 0x00100000, 16, 16, 7, 0x80)
    return bytes(header).ljust(0x80, b"\0") + bytes(16)


def version_line(label, build="4400", stable="1.3.0"):
    return f'#define OPL_VERSION "v{stable}-{build}-abcdef0-PS2BUILD-{label}"\n'


def app_folder(folder, elf, payload):
    """A real app folder (companions + ELF) and its PSU, as the kit packager makes them."""
    app = publish_lite.app
    files = app.app_companions(SimpleNamespace(app_name=folder, elf_name=elf, title="RiptOPL", pbt_title="RiptOPL",
                                               version="v1", flavour="PS2BUILD", channel="test", description="test"))
    files[elf] = payload
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp) / folder
        directory.mkdir()
        for name, data in files.items():
            (directory / name).write_bytes(data)
        app.build_psu(directory, Path(temp) / "out.psu")
        return files, (Path(temp) / "out.psu").read_bytes()


def make_artifacts(root, channel="stable", stable="1.3.0", tamper=None):
    """release-*, variant-* and lite*-* folders as rolling-release.yml downloads them."""
    for edition, options in editions.EDITIONS.items():
        path = root / f"release-{edition}"
        path.mkdir()
        elf = "RIPTOPL-RA.ELF" if options["ra"] else "RIPTOPL.ELF"
        psu = "APP_RIPTOPL-RA.psu" if options["ra"] else "APP_RIPTOPL.psu"
        app = "APP_RIPTOPL-RA" if options["ra"] else "APP_RIPTOPL"
        payload = b"\x7fELF" + edition.encode()
        manifest = {"repository": "NathanNeurotic/RiptOPL", "source_commit": SHA, "dirty": False, "edition": edition,
                    "compiler": options["compiler"], "sdk": "ps2build", "ra": options["ra"], "configuration": "standard",
                    **SUITE, "elf_sha256": hashlib.sha256(payload).hexdigest(), "release_channel": channel,
                    "version": version_line(f"{options['title']}-X", stable=stable),
                    "neutrino_archive": {"filename": PIN["asset"] if options["ra"] else "neutrino_v1.8.0.7z", "sha256": "0" * 64}}
        (path / elf).write_bytes(payload)
        files, psu_bytes = app_folder(app, elf, payload)
        if tamper == ("cfg", edition):
            files["title.cfg"] = files["title.cfg"].replace(f"boot={elf}".encode(), b"boot=OTHER.ELF")
        (path / psu).write_bytes(psu_bytes)
        (path / "LICENSE-USMB2.txt").write_bytes(b"usmb2 licence")
        (path / "BUILD-MANIFEST.json").write_text(json.dumps(manifest))
        with zipfile.ZipFile(path / "RIPTOPL-src.zip", "w") as source:
            source.comment = SHA.encode()
            source.writestr("README.md", "source " + edition)  # differs per edition: exercises the label rename
        with zipfile.ZipFile(path / "RIPTOPL-LANGS.zip", "w") as langs:
            # A fixed date: a bare name takes the clock, so two editions written a second apart would differ.
            langs.writestr(zipfile.ZipInfo("README.txt", (2026, 1, 1, 0, 0, 0)), "same")
        with zipfile.ZipFile(path / editions.kit_archive_name(options), "w") as kit:
            kit.writestr("BUILD-MANIFEST.json", json.dumps(manifest))
            for name, data in files.items():
                kit.writestr(f"APPS/{app}/{name}", data)
            kit.writestr(psu, psu_bytes)
            kit.writestr("LICENSE-USMB2.txt", b"usmb2 licence")
            if options["ra"]:
                for name in ("neutrino/neutrino.elf", "neutrino/modules/raagent.irx", "neutrino/ORBIT-RA.txt",
                             "neutrino/LICENSE", *publish_lite.RA_KIT_FILES):
                    if name != "LICENSE-USMB2.txt":
                        kit.writestr(name, name)
        sums = "".join(hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name + "\n" for p in sorted(path.iterdir()))
        (path / "SHA256SUMS.txt").write_text(sums)
        configurations = {c for _, bundled, _ in publish_editions.bundles(options) for c in bundled}
        configurations |= set(publish_editions.UPDATABLE[edition])
        for configuration in sorted(configurations):
            stage = root / f"variant-{edition}-{configuration}"
            stage.mkdir()
            packed = b"\x7fELF" + f"{edition}-{configuration}".encode()
            variant = {"source_commit": SHA, "dirty": False, "edition": edition, "compiler": options["compiler"],
                       "configuration": configuration, **SUITE, "elf_sha256": hashlib.sha256(packed).hexdigest(),
                       "release_channel": "rolling" if tamper == ("variant", configuration) else channel,
                       "version": version_line(f"{options['title']}-X-{configuration.upper()}", stable=stable)}
            (stage / "RIPTOPL.ELF").write_bytes(packed)
            if configuration in publish_editions.UPDATABLE[edition] or configuration == "diag":
                raw = unpacked_elf() + f"{edition}-{configuration}".encode()
                (stage / "RIPTOPL-UNPACKED.ELF").write_bytes(raw)
                variant["unpacked_elf_sha256"] = hashlib.sha256(raw).hexdigest()
            (stage / "BUILD-MANIFEST.json").write_text(json.dumps(variant))
    for edition, options in editions.LITES.items():
        for device in lite.DEVICES:
            name = lite.elf_name(device, options["ra"])
            elf = b"\x7fELF" + f"{edition}-{device}".encode()
            stage = root / f"{edition}-{device}"
            stage.mkdir()
            manifest = {"repository": "NathanNeurotic/RiptOPL", "source_commit": SHA, "dirty": False, "edition": edition,
                        "device": device, "compiler": "gcc", "configuration": "standard", "theme": options["theme"],
                        "ra": options["ra"], **SUITE, "elf": name, "elf_sha256": hashlib.sha256(elf).hexdigest(),
                        "unpacked_elf": publish_lite.unpacked_name(device, options["ra"]),
                        "unpacked_elf_sha256": hashlib.sha256(elf + b"-unpakt").hexdigest(),
                        "release_channel": "rolling" if tamper == ("lite", edition, device) else channel,
                        "version": version_line(f"{options['title']}-{lite.DEVICES[device]['title']}-GCC", stable=stable)}
            (stage / "BUILD-MANIFEST.json").write_text(json.dumps(manifest))
            (stage / name).write_bytes(elf)
            (stage / publish_lite.unpacked_name(device, options["ra"])).write_bytes(elf + b"-unpakt")


def history(*args):
    if args[0] == "tag":
        return ""
    return "b" * 40 + "\x00Fix [launch] <edge>"


def cut(root, stable="1.3.0"):
    """publish_stable.main() up to and including the notes, without GitHub."""
    with patch.object(publish_stable, "git", side_effect=history):
        publish_editions.bundle_configurations(root, SHA, channel="stable")
        candidates = (publish_editions.fold_companions(publish_editions.verify(root, SHA, channel="stable"))
                      + publish_editions.lite_candidates(root, SHA, channel="stable"))
        version = publish_stable.stable_release_version(candidates, stable)
        publish_editions.name_assets(candidates, version)
        out = publish_stable.merge(candidates, root, version)
        publish_stable.check_release(out, version)
        notes = publish_stable.release_notes(stable, version, SHA, out, "v1.8.0", Path(PIN["asset"]).stem)
    return version, out, notes


class StableReleaseTests(unittest.TestCase):
    def test_version_string_and_channel_follow_the_environment(self):
        with patch.dict(os.environ, {"RIPTOPL_STABLE_VERSION": "1.3.0"}):
            self.assertEqual(stable_channel.release_channel(), "stable")
            config = editions.definition("riptopl")
            main = next(t for t in config["targets"] if t["name"] == "riptopl")
            self.assertIn("RIPTOPL_UPDATE_STABLE", main["defines"])
            self.assertIn("RIPTOPL_UPDATE_ASSET=RIPTOPL.ELF", main["defines"])
            lite_config = editions.definition("lite_ra", device="usb")
            lite_main = next(t for t in lite_config["targets"] if t["name"] == "riptopl")
            self.assertIn("RIPTOPL_UPDATE_STABLE", lite_main["defines"])
            self.assertIn("RIPTOPL_UPDATE_ASSET=RIPTOPLITE-RA-USB.ELF", lite_main["defines"])
            env = {**os.environ, "LOCALVERSION": "PS2BUILD-RiptOPL-LLVM"}
            version = subprocess.check_output([sys.executable, str(ROOT / "tools/assets.py"), "version-string"],
                                              env=env, text=True).strip()
            self.assertRegex(version, r"^v1\.3\.0-[0-9]+-[0-9a-f]{7}(-dirty)?-PS2BUILD-RiptOPL-LLVM$")
        with patch.dict(os.environ, {"RIPTOPL_STABLE_VERSION": ""}):
            self.assertEqual(stable_channel.release_channel(), "rolling")
            main = next(t for t in editions.definition("riptopl")["targets"] if t["name"] == "riptopl")
            self.assertNotIn("RIPTOPL_UPDATE_STABLE", main["defines"])
        for bad in ("1.3", "v1.3.0", "1.3.0-beta", "1.3.0 ; rm"):
            with patch.dict(os.environ, {"RIPTOPL_STABLE_VERSION": bad}), self.assertRaises(SystemExit):
                stable_channel.stable_version()

    def test_the_cut_enforces_one_naming_rule(self):
        """APP_ folder = PSU = APP_ plus the ELF name, and title.cfg boots that ELF, in every PSU and ZIP."""
        with tempfile.TemporaryDirectory() as temp:
            version, out, _ = (lambda root: (make_artifacts(root), cut(root))[1])(Path(temp))
            psus = sorted(p.name for p in out.iterdir() if p.suffix == ".psu")
            self.assertEqual(len(psus), 2 + 4 * len(lite.DEVICES))  # packed and unpacked, standard and RA
            self.assertIn("APP_RIPTOPLITE-RA-MX4SIO-UNPAKT.psu", psus)
            self.assertIn("APP_RIPTOPLITE-RA-USB.psu", psus)
        with tempfile.TemporaryDirectory() as temp:  # a kit whose title.cfg boots another ELF
            make_artifacts(Path(temp), tamper=("cfg", "riptopl"))
            with self.assertRaisesRegex(ValueError, "title.cfg does not boot RIPTOPL.ELF"):
                cut(Path(temp))
        app = publish_lite.app
        files, psu = app_folder("APP_RIPTOPL-X", "RIPTOPL-X.ELF", b"\x7fELF")
        for bad, why in ((lambda d: (d / "APP_RIPTOPL-Y.psu").write_bytes(psu), "holds the folder"),
                         (lambda d: (d / "APP_RIPTOPL-X.psu").write_bytes(app_folder("APP_RIPTOPL-X", "RIPTOPL.ELF", b"\x7fELF")[1]),
                          "must hold exactly RIPTOPL-X.ELF"),
                         (lambda d: zipfile.ZipFile(d / "RIPTOPL-Z.zip", "w").writestr("RIPTOPL-Z.ELF", b"x"),
                          "not in its own APP_ folder"),
                         (lambda d: zipfile.ZipFile(d / "RIPTOPL-Z.zip", "w").writestr("pademu0/RIPTOPL-PADEMU0.ELF", b"x"),
                          "folder pademu0 is not APP_"),
                         (lambda d: zipfile.ZipFile(d / "RIPTOPL-Z.zip", "w").writestr("APPS_RiptOPL/RIPTOPL.ELF", b"x"),
                          "folder APPS_RiptOPL is not APP_")):
            with self.subTest(why=why), tempfile.TemporaryDirectory() as temp:
                bad(Path(temp))
                with self.assertRaisesRegex(ValueError, why):
                    publish_stable.check_naming(Path(temp))
        with tempfile.TemporaryDirectory() as temp:  # no loader is loose, the bug-report build included
            with zipfile.ZipFile(Path(temp) / "RIPTOPL-BUGREPORT-v1.zip", "w") as bundle:
                bundle.writestr("RIPTOPL-BUGREPORT.ELF", b"x")
            with self.assertRaisesRegex(ValueError, "not in its own APP_ folder"):
                publish_stable.check_naming(Path(temp))
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp) / "APP_RIPTOPL-X.psu").write_bytes(psu)
            publish_stable.check_naming(Path(temp))

    def test_one_release_carries_every_build_and_every_updater_finds_its_own(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            make_artifacts(root)
            version, out, notes = cut(root)
            self.assertEqual(version, "v1.3.0-4400")
            names = {p.name for p in out.iterdir()}
            sums = (out / "SHA256SUMS.txt").read_text()
            # Each loader's updater finds its own ELF (the same variant) and the stable marker.
            for asset in publish_stable.expected_update_assets() | {publish_stable.MARKER}:
                self.assertIn(asset, names)
                self.assertEqual(publish_stable.updater_view(sums, asset)[1], 4400, asset)
            # RiptOPL 5 builds and RA 2, and 16 RiptOPLite, each packed and unpacked: 10 + 4 + 32.
            self.assertEqual(len(publish_stable.expected_update_assets()), 46)
            # Debug and bug-report builds have no updater, so no single ELF of theirs is published.
            self.assertFalse({"RIPTOPL-DEBUG-UART.ELF", "RIPTOPL-DIAG.ELF", "RIPTOPL-DIAG-UNPACKED.ELF"} & names)
            self.assertIn("RIPTOPL-EXTRAS-DS5-UNPAKT.ELF", names)
            self.assertIn("RIPTOPLITE-RA-MX4SIO-UNPAKT.ELF", names)
            # The old -UNPACKED names serve rolling builds installed before the rename, never stable.
            self.assertFalse({n for n in names if "UNPACKED" in n})
            # Kits, bundles, Lite RA zips, languages and sources, all versioned.
            for name in ("RIPTOPL-v1.3.0-4400.zip", "RIPTOPL-RetroAchievements-v1.3.0-4400.zip",
                         "RIPTOPL-VARIANTS-v1.3.0-4400.zip", "RIPTOPL-DEBUG-v1.3.0-4400.zip",
                         "RIPTOPL-BUGREPORT-v1.3.0-4400.zip", "RIPTOPL-RA-VARIANTS-v1.3.0-4400.zip",
                         "RIPTOPL-LANGS-v1.3.0-4400.zip", "RIPTOPL-src-v1.3.0-4400.zip", "RIPTOPL-RA-src-v1.3.0-4400.zip",
                         "APP_RIPTOPL.psu", "APP_RIPTOPL-RA.psu", "BUILD-MANIFEST.json", "BUILD-MANIFEST-RA.json",
                         "BUILD-MANIFEST-LITE.json", "LICENSE-USMB2.txt", "LICENSE-MBEDTLS.txt",
                         *(f"RIPTOPLITE-RA-{d['title']}-v1.3.0-4400.zip" for d in lite.DEVICES.values())):
                self.assertIn(name, names)
            self.assertNotIn("RIPTOPL-RA-LANGS-v1.3.0-4400.zip", names)  # identical files are kept once
            self.assertLess(len(sums.encode()), publish_stable.SUMS_LIMIT)
            # The marker is listed, so a stable updater accepts this release.
            self.assertIn("stable", (out / publish_stable.MARKER).read_text())

            # Notes: the recommended downloads first, then every file in the generated tables.
            self.assertTrue(notes.startswith("**RiptOPL 1.3.0** (`v1.3.0-4400`) - stable release"))
            self.assertLess(notes.index("## Recommended downloads"), notes.index("<h3>Loaders:"))
            self.assertLess(notes.index("<h3>Loaders:"), notes.index("## Updating"))
            self.assertIn("### Start here: [RIPTOPL-v1.3.0-4400.zip]"
                          "(https://github.com/NathanNeurotic/RiptOPL/releases/download/v1.3.0/RIPTOPL-v1.3.0-4400.zip)\n\nNot sure what to pick?", notes)
            self.assertIn("| RIPTOPLITE-RA-MX4SIO | ", notes)
            linked = set(re.findall(r"/releases/download/v1\.3\.0/([^)\s]+)\)", notes))
            self.assertEqual(linked, names)  # every file in the release has a link, and nothing else
            self.assertIn("never a rolling", notes)
            self.assertIn(f"Included build: {Path(PIN['asset']).stem} -- [NULD", notes)
            self.assertIn("Included build: v1.8.0 -- Neutrino.", notes)
            self.assertIn("The first stable release.", notes)
            self.assertIn(r"Fix \[launch\] \<edge\>", notes)
            self.assertLess(len(notes), 125000)  # GitHub's release body limit

    def test_rolling_and_stable_refuse_each_others_builds(self):
        for tamper in (("variant", "pademu0"), ("variant", "diag"), ("lite", "lite_ra", "mmce")):
            with tempfile.TemporaryDirectory() as temp, self.assertRaises(ValueError, msg=str(tamper)):
                root = Path(temp)
                make_artifacts(root, tamper=tamper)
                cut(root)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            make_artifacts(root)
            with self.assertRaises(ValueError):  # the rolling publisher never takes a stable build
                publish_editions.bundle_configurations(root, SHA)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            make_artifacts(root, channel="rolling")
            with self.assertRaises(ValueError):  # the stable publisher never takes a rolling build
                cut(root)

    def test_builds_of_another_version_are_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            make_artifacts(root, stable="1.3.1")
            with self.assertRaises(ValueError):
                cut(root, stable="1.3.0")

    def test_updater_view_matches_the_console_parser(self):
        line = lambda name: "a" * 64 + "  " + name + "\n"  # noqa: E731
        text = line("RIPTOPL.ELF") + line("RIPTOPL-v1.3.0-4400.zip") + line("RIPTOPL-STABLE.txt")
        self.assertEqual(publish_stable.updater_view(text, "RIPTOPL.ELF"), ("a" * 64, 4400))
        self.assertIsNone(publish_stable.updater_view(text, "RIPTOPL-RA.ELF"))
        self.assertIsNone(publish_stable.updater_view(text + line("RIPTOPL-RA-v1.3.0-4401.zip"), "RIPTOPL.ELF"))
        self.assertEqual(publish_stable.updater_view(line("X.ELF") + line("RIPTOPL-v1.2.0-Beta-4256.zip"), "X.ELF")[1], 4256)
        self.assertEqual(publish_stable.revision_in("v1.3.0"), -1)

    def test_rolling_channels_become_pre_releases_only_after_a_stable_release(self):
        self.assertEqual(publish_editions.channel_flags("riptopl", False), ("--prerelease=false", "--latest=true"))
        self.assertEqual(publish_editions.channel_flags("ra", False), ("--prerelease=false", "--latest=false"))
        for edition in ("riptopl", "ra", "lite"):
            self.assertEqual(publish_editions.channel_flags(edition, True), ("--prerelease=true", "--latest=false"))

        def api(stdout="", code=0, stderr=""):
            return subprocess.CompletedProcess([], code, stdout, stderr)
        with patch.object(publish_editions.subprocess, "run", return_value=api("v1.3.0\n")):
            self.assertTrue(publish_editions.stable_release_exists("o/r"))
        with patch.object(publish_editions.subprocess, "run", return_value=api("rolling\n")):
            self.assertFalse(publish_editions.stable_release_exists("o/r"))
        with patch.object(publish_editions.subprocess, "run", return_value=api(code=1, stderr="HTTP 404")):
            self.assertFalse(publish_editions.stable_release_exists("o/r"))
        with patch.object(publish_editions.subprocess, "run", return_value=api(code=1, stderr="HTTP 502")), \
                self.assertRaises(RuntimeError):  # never guess: a wrong guess would take Latest from stable
            publish_editions.stable_release_exists("o/r")

    def test_preflight_never_rewrites_or_downgrades_a_stable_release(self):
        sha, other = "a" * 40, "b" * 40
        with patch.object(publish_stable, "release_state", return_value="published"), \
                patch.object(publish_stable, "tag_sha", return_value=sha), self.assertRaises(ValueError):
            publish_stable.preflight("o/r", "v1.3.0", sha)
        for state in (None, "draft"):  # a tag on another commit is never moved
            with patch.object(publish_stable, "release_state", return_value=state), \
                    patch.object(publish_stable, "tag_sha", return_value=other), \
                    patch.object(publish_stable.rolling, "gh") as gh, self.assertRaises(ValueError):
                publish_stable.preflight("o/r", "v1.3.0", sha)
            gh.assert_not_called()
        for latest in ("v1.3.0", "v1.10.0"):
            with patch.object(publish_stable, "release_state", return_value=None), \
                    patch.object(publish_stable, "tag_sha", return_value=None), \
                    patch.object(publish_stable, "current_stable", return_value=latest), self.assertRaises(ValueError):
                publish_stable.preflight("o/r", "v1.9.0" if latest == "v1.10.0" else "v1.3.0", sha)
        # A draft from a failed attempt, untagged or already tagged at this same commit: removed, cut runs again.
        for tagged in (None, sha):
            with patch.object(publish_stable, "release_state", return_value="draft"), \
                    patch.object(publish_stable, "tag_sha", return_value=tagged), \
                    patch.object(publish_stable, "current_stable", return_value="v1.2.9"), \
                    patch.object(publish_stable.rolling, "gh") as gh:
                publish_stable.preflight("o/r", "v1.3.0", sha)
                gh.assert_called_once_with("release", "delete", "v1.3.0", "--repo", "o/r", "--yes")

    def test_publish_marks_stable_latest_then_flips_rolling(self):
        calls = []

        def gh(*args, **_):
            calls.append(args)
            if args[:2] == ("api", "repos/o/r/git/ref/tags/v1.3.0"):
                return json.dumps({"object": {"sha": SHA}})
            return ""
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out / "RIPTOPL.ELF").write_bytes(b"x")
            with patch.object(publish_stable, "preflight"), patch.object(publish_stable.rolling, "upload_verified") as upload, \
                    patch.object(publish_stable.rolling, "update_tag") as tag, patch.object(publish_stable.rolling, "gh", side_effect=gh), \
                    patch.object(publish_stable, "current_stable", return_value="v1.3.0"), \
                    patch.object(publish_stable, "release_state", return_value="published"), \
                    patch.object(publish_stable.time, "sleep"):
                publish_stable.publish("o/r", "v1.3.0", SHA, "RiptOPL 1.3.0", "notes", out)
        upload.assert_called_once()
        self.assertFalse(upload.call_args[0][6])  # always a new release, never an overwrite
        tag.assert_called_once_with("o/r", "v1.3.0", SHA)
        edits = [c for c in calls if c[:2] == ("release", "edit")]
        self.assertIn("--latest=true", edits[0])
        self.assertIn("--prerelease=false", edits[0])
        self.assertEqual([c[2] for c in edits[1:]], list(publish_stable.ROLLING_TAGS))
        for edit in edits[1:]:
            self.assertIn("--prerelease=true", edit)
            self.assertIn("--latest=false", edit)

    def test_console_updater_reads_only_stable_releases(self):
        source = (ROOT / "src/ui/selfupdate.cpp").read_text(encoding="utf-8")
        stable = source[source.index("#ifdef RIPTOPL_UPDATE_STABLE"):source.index("#endif", source.index("#ifdef RIPTOPL_UPDATE_STABLE"))]
        self.assertIn('"/NathanNeurotic/RiptOPL/releases/latest/download/"', stable)
        self.assertIn(f'#define SU_STABLE_MARKER "{publish_stable.MARKER}"', stable)
        check = source[source.index("static void suCheckWorker"):]
        check = check[:check.index("\n}\n")]
        self.assertLess(check.index("suParseSums(sums.text"), check.index("suListsStableMarker"))
        self.assertLess(check.index("suListsStableMarker"), check.index("SU_NEWEST"))
        limit = re.search(r"SU_SUMS_MAX = (\d+) \* 1024", source)
        self.assertEqual(int(limit.group(1)) * 1024, publish_stable.SUMS_LIMIT)

    def test_workflow_cuts_stable_in_one_dispatch(self):
        workflow = (ROOT / ".github/workflows/rolling-release.yml").read_text(encoding="utf-8")
        self.assertIn("stable_version:", workflow)
        self.assertEqual(workflow.count("RIPTOPL_STABLE_VERSION: ${{ github.event_name == 'workflow_dispatch' && inputs.stable_version || '' }}"), 3)
        self.assertIn('python tools/publish_stable.py dist/downloaded --version "$RIPTOPL_STABLE_VERSION" --check', workflow)
        self.assertIn('python tools/publish_stable.py dist/downloaded --version "$RIPTOPL_STABLE_VERSION"\n', workflow)
        self.assertIn("if: env.RIPTOPL_STABLE_VERSION == ''\n        run: python tools/publish_editions.py dist/downloaded\n", workflow)


if __name__ == "__main__":
    unittest.main()
