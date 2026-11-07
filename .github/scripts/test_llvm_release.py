"""Host-only regression tests for the independent RiptOPLLVM release channel."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import publish_llvm as llvm
import check_llvm_purity as purity
import package_edition
from build_configuration import CONFIGURATIONS
from build_edition import EDITIONS
from lite_devices import DEVICES, elf_name


SHA = "a" * 40
VERSION = "v1.2.0-Beta-4059"


def put(path, name, content=b"ELF"):
    target = path / name
    if name.endswith(".zip"):
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
            # Stable fixtures across second boundaries: shared editions must have identical archives.
            entry = zipfile.ZipInfo("payload.txt", (2020, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, content)
    else:
        target.write_bytes(content)


def candidates(root, compiler="llvm", altered_lite=None):
    """Synthetic verified inputs; the real publisher verifies package internals first."""
    out = []
    for edition in ("korium", "ra", "lite"):
        path = root / ("release-" + edition)
        path.mkdir()
        if edition == "lite":
            builds = {}
            for device in DEVICES:
                for ra in (False, True):
                    name = elf_name(device, ra=ra)
                    build_edition = "lite_ra" if ra else "lite"
                    builds[name] = {"compiler": "gcc" if altered_lite == name else compiler, "source_commit": SHA}
                    put(path, name.replace(".ELF", "-" + VERSION + ".zip") if ra else name)
            manifest = {"repository": llvm.REPO, "source_commit": SHA, "builds": builds}
        else:
            manifest = {"repository": llvm.REPO, "source_commit": SHA, "compiler": compiler}
            if edition == "korium":
                put(path, "RIPTOPL.ELF")
                put(path, "APP_RIPTOPL.psu")
                for prefix in ("RIPTOPL-Korium", "RIPTOPL-VARIANTS", "RIPTOPL-DEBUG",
                               "RIPTOPL-BUGREPORT", "RIPTOPL-LANGS", "RIPTOPL-src"):
                    put(path, prefix + "-" + VERSION + ".zip")
            else:
                put(path, "RIPTOPL-RA.ELF")
                put(path, "APP_RIPTOPL-RA.psu")
                for prefix in ("RIPTOPL-RetroAchievements", "RIPTOPL-RA-VARIANTS",
                               "RIPTOPL-LANGS", "RIPTOPL-src"):
                    put(path, prefix + "-" + VERSION + ".zip")
        put(path, "LICENSE-LIBSMB2.txt", b"LGPL")
        (path / "BUILD-MANIFEST.json").write_text(json.dumps(manifest), encoding="utf-8")
        out.append((edition, {}, path, {}))
    return out


class RiptOPLLVMTests(unittest.TestCase):
    def test_publisher_imports_without_installed_packages(self):
        import subprocess

        code = (f"import sys; sys.path.insert(0, {str(ROOT / 'tools')!r}); "
                "import publish_llvm; assert 'yaml' not in sys.modules")
        subprocess.run([sys.executable, "-S", "-c", code],
                       check=True, capture_output=True, text=True)

    def test_unpacked_lld_layout_accounts_for_non_loadable_mips_headers(self):
        import struct
        from test_ps2build_configurations import unpacked_elf

        elf = bytearray(unpacked_elf(offset=0x100))
        struct.pack_into("<H", elf, 44, 3)  # PT_LOAD, PT_MIPS_REGINFO and PT_MIPS_ABIFLAGS
        llvm.common.check_unpacked_layout(elf)
        for bad_offset in (0x80, 0x10000):
            bad = bytearray(elf)
            struct.pack_into("<I", bad, 56, bad_offset)
            with self.assertRaisesRegex(ValueError, "packed or not linked"):
                llvm.common.check_unpacked_layout(bad)

    def test_parallel_workflow_tracks_the_gcc_korium_matrix(self):
        llvm_flow = yaml.safe_load((ROOT / ".github/workflows/rolling-llvm.yml").read_text())
        gcc_flow = yaml.safe_load((ROOT / ".github/workflows/rolling-release.yml").read_text())
        targets = llvm_flow["jobs"]["build"]["strategy"]["matrix"]["target"]
        gcc_targets = gcc_flow["jobs"]["build"]["strategy"]["matrix"]["target"]
        wanted = [t for t in gcc_targets if t["edition"] in ("korium", "ra", "lite", "lite_ra")]
        self.assertEqual(targets, wanted)
        self.assertNotIn("incebtion", {t["edition"] for t in targets})
        self.assertEqual({t["configuration"] for t in targets if t["edition"] == "korium"}, set(CONFIGURATIONS))
        self.assertEqual(2 * len(DEVICES), len([t for t in targets if t["edition"] in ("lite", "lite_ra")]))
        self.assertEqual(llvm_flow["jobs"]["build"]["env"]["PS2BUILD_TOOLCHAIN"], "llvm")
        self.assertEqual(llvm_flow["jobs"]["publish"]["permissions"]["contents"], "write")
        self.assertEqual(llvm_flow["jobs"]["verify"]["permissions"]["contents"], "read")
        publish = llvm_flow["jobs"]["publish"]
        install = "python -m pip install -r requirements.txt"
        self.assertIn(install, [step.get("run") for step in llvm_flow["jobs"]["build"]["steps"]])
        self.assertNotIn(install, [step.get("run") for step in publish["steps"]])
        self.assertIn("python -S tools/publish_llvm.py dist/downloaded --check",
                      [step.get("run") for step in llvm_flow["jobs"]["verify"]["steps"]])
        self.assertNotIn("GH_TOKEN", publish.get("env", {}))
        token_steps = [step for step in publish["steps"] if "GH_TOKEN" in step.get("env", {})]
        self.assertEqual(len(token_steps), 1)
        self.assertEqual(token_steps[0]["run"], "python -S tools/publish_llvm.py dist/downloaded")
        self.assertIn("needs", llvm_flow["jobs"]["publish"])
        self.assertIn("--compiler llvm", (ROOT / ".github/workflows/rolling-llvm.yml").read_text())
        self.assertIn("compiler=COMPILER", (ROOT / "tools/publish_llvm.py").read_text())

    def test_companion_core_is_ra_optional_and_network_bounded(self):
        """Both CI channels bypass redundant RA downloads and preserve non-RA requirements."""
        # Both release channels must avoid downloading an unused upstream .7z for RA.
        for filename in ("rolling-release.yml", "rolling-llvm.yml"):
            with self.subTest(workflow=filename):
                flow = yaml.safe_load((ROOT / ".github/workflows" / filename).read_text())
                steps = flow["jobs"]["build"]["steps"]
                download = next(step for step in steps if "official companion core" in step.get("name", ""))
                package = next(step for step in steps if step.get("name", "").startswith("Package and verify the full"))
                self.assertIn("!startsWith(matrix.target.edition, 'ra')", download["if"])
                self.assertEqual(download["timeout-minutes"], 7)
                for command in ("command -v 7z", "command -v 7za", "timeout 120s sudo apt-get update",
                                "timeout 120s sudo env", "timeout 120s gh release download"):
                    self.assertIn(command, download["run"])
                self.assertEqual(package["timeout-minutes"], 8)
                self.assertIn("if [[ '${{ matrix.target.edition }}' == ra* ]]; then", package["run"])
                # Check actual commands rather than weak substring matches in comments.
                commands = [line.strip() for line in package["run"].splitlines()
                            if line.strip().startswith("python tools/package_edition.py")]
                compiler = " --compiler llvm" if filename == "rolling-llvm.yml" else ""
                self.assertEqual(commands, [
                    "python tools/package_edition.py '${{ matrix.target.edition }}'" + compiler,
                    "python tools/package_edition.py '${{ matrix.target.edition }}' --neutrino-archive \"${cores[0]}\"" + compiler,
                ])
                # The edition-name predicate must stay aligned with the actual packaging semantics.
                for target in flow["jobs"]["build"]["strategy"]["matrix"]["target"]:
                    if target["configuration"] == "standard" and not target.get("device"):
                        self.assertEqual(target["edition"].startswith("ra"),
                                         EDITIONS[target["edition"]]["ra"])

        # A missing archive must still fail promptly for standard kits.
        import subprocess
        result = subprocess.run([sys.executable, str(ROOT / "tools/package_edition.py"), "korium"],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("standard kit requires --neutrino-archive", result.stderr)

    def test_ra_cli_packages_synthetic_pinned_orbit_without_stock_neutrino(self):
        """Exercise both RA CLI paths with local artifacts, validating their packaged runtime."""
        import subprocess

        for edition, compiler in (("ra", "llvm"), ("ra_incebtion", "gcc")):
            with self.subTest(edition=edition, compiler=compiler), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "build").mkdir()
                for folder in ("ship/POPS", "ship/EMBER", "ship/neutrino", "lng", "tools/pc",
                               "third_party/mbedtls", "dist/editions/" + edition):
                    (root / folder).mkdir(parents=True, exist_ok=True)

                # Only unrelated app staging and git archive creation are stubbed.
                example = root / "app-example"
                for name in ("CHEATS.TXT", "IGR_BG.TM2", "IGR_NO.TM2", "IGR_YES.TM2",
                             "POPSTARTER.KELF", "TROJAN_7.BIN"):
                    target = example / "POPS" / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(b"test asset")
                target = example / "EMBER/games/Per-Game-Folder/GAME FILES"
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"test game files")
                for name in ("PS2-Servers.url", "OrbitPS2-Manager.url",
                             "OPL-PS1-AIO-Converter-GUI.url", "PS2RD-CHT-Manager.url",
                             "udpfs-server.url"):
                    (root / "tools/pc" / name).write_text("[InternetShortcut]\\n", encoding="utf-8")
                for name in ("LICENSE", "CREDITS", "README.md"):
                    (root / name).write_text("test", encoding="utf-8")
                (root / "third_party/mbedtls/LICENSE").write_text("Apache-2.0", encoding="utf-8")
                (root / "lng/lang_English.lng").write_text("test", encoding="utf-8")
                (root / "ship/neutrino/bsd-udpfsbd.toml").write_text("bdm = 1", encoding="utf-8")
                licence = root / "LICENSE-LIBSMB2.txt"
                licence.write_text("LGPL-2.1", encoding="utf-8")

                built = root / "dist/editions" / edition
                elf = built / "RIPTOPL.ELF"
                elf.write_bytes(b"test loader")
                modules = {"IEEE1394_bd_mini.irx": b"test ilink bd",
                           "iLinkman.irx": b"test ilink manager"}
                for name, payload in modules.items():
                    (built / name).write_bytes(payload)
                manifest = {
                    "edition": edition, "configuration": "standard", "compiler": compiler,
                    "source_commit": SHA, "dirty": False, "version": '"v1.2.0"',
                    "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
                    "ilink_bd_sha256": hashlib.sha256(modules["IEEE1394_bd_mini.irx"]).hexdigest(),
                    "ilink_manager_sha256": hashlib.sha256(modules["iLinkman.irx"]).hexdigest(),
                }
                (built / "BUILD-MANIFEST.json").write_text(json.dumps(manifest), encoding="utf-8")

                archive = root / "orbit-synthetic.zip"
                entries = {"neutrino.elf": b"ELF -ra=<file>",
                           "modules/ee_core.elf": b"EE core",
                           "modules/raagent.irx": b"RA agent fixture",
                           "modules/IEEE1394_bd_mini.irx": b"upstream link",
                           "modules/iLinkman.irx": b"upstream manager",
                           "config/system.toml": b"system",
                           "config/bsd-udpfs.toml": b"udpfs", "LICENSE": b"AFL"}
                with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
                    for name, payload in entries.items():
                        output.writestr("ORBIT-test/neutrino/" + name, payload)
                    output.writestr("ORBIT-test/orbit.elf", b"not distributed")
                pin = json.loads(package_edition.ORBIT_PIN.read_text(encoding="utf-8"))
                pin["sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
                pin_path = root / "ORBIT-release.json"
                pin_path.write_text(json.dumps(pin), encoding="utf-8")

                def stage_app(args):
                    """Create the unrelated PSU artifact while testing the real RA kit assembly."""
                    (Path(args.package) / (args.app_name + ".psu")).write_bytes(b"synthetic PSU")

                def archive_source(command, **kwargs):
                    """Create a minimal source ZIP in place of an actual Git checkout archive."""
                    self.assertEqual(command[:3], ["git", "archive", "--format=zip"])
                    location = next(item.split("=", 1)[1] for item in command if item.startswith("--output="))
                    with zipfile.ZipFile(location, "w") as output:
                        output.writestr("README.md", b"source fixture")
                    return subprocess.CompletedProcess(command, 0)

                def git_ref(*args):
                    """Supply the synthetic build's source SHA without a repository checkout."""
                    return SHA if args == ("rev-parse", "HEAD") else ""

                with (mock.patch.object(package_edition, "ROOT", root),
                      mock.patch.object(package_edition, "ORBIT_PIN", pin_path),
                      mock.patch.object(package_edition, "LIBSMB2_LICENCE", licence),
                      mock.patch.object(package_edition, "git", side_effect=git_ref),
                      mock.patch.object(package_edition.app, "EXAMPLE", example),
                      mock.patch.object(package_edition.app, "stage", side_effect=stage_app),
                      mock.patch.object(package_edition.app, "verify"),
                      mock.patch.object(package_edition.subprocess, "run", side_effect=archive_source),
                      mock.patch.object(package_edition, "download", side_effect=AssertionError("unexpected network download"))):
                    # Calls the same argument parser used by tools/package_edition.py.
                    package_edition.main([edition, "--compiler", compiler, "--orbit-archive", str(archive)])

                out = root / "dist/releases" / edition
                kit = out / ("RIPTOPL-" + EDITIONS[edition]["title"] + ".zip")
                self.assertTrue(kit.is_file())
                with zipfile.ZipFile(kit) as output:
                    self.assertEqual(output.read("neutrino/modules/raagent.irx"), b"RA agent fixture")
                    self.assertEqual(output.read("neutrino/modules/iLinkman.irx"), modules["iLinkman.irx"])
                    self.assertEqual(output.read("neutrino/modules/IEEE1394_bd_mini.irx"),
                                     modules["IEEE1394_bd_mini.irx"])
                    self.assertEqual(json.loads(output.read("neutrino/ORBIT-RA.json")), pin)
                    self.assertIn(b"RETROACHIEVEMENTS EDITION",
                                  output.read("neutrino/RA-EDITION-NEUTRINO.txt"))
                    self.assertNotIn("ORBIT-test/orbit.elf", output.namelist())
                    kit_manifest = json.loads(output.read("BUILD-MANIFEST.json"))
                    self.assertEqual(kit_manifest["neutrino_archive"]["sha256"], pin["sha256"])
                    self.assertEqual(kit_manifest["compiler"], compiler)

    def test_all_three_editions_form_one_separate_llvm_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            rows = candidates(root)
            stage = llvm.assemble(root, rows, SHA, VERSION)
            self.assertEqual({p.name for p in stage.iterdir()}, llvm.expected_files(VERSION) | {"SHA256SUMS.txt"})
            self.assertEqual(llvm.verify(stage, VERSION, SHA),
                             dict(line.split("  ", 1)[::-1] for line in
                                  (stage / "SHA256SUMS.txt").read_text().splitlines()))
            metadata = json.loads((stage / "BUILD-MANIFEST.json").read_text())
            self.assertEqual(metadata["compiler"], "llvm")
            self.assertEqual(set(metadata["editions"]), {"korium", "ra", "lite"})
            self.assertTrue((stage / ("RIPTOPL-RA-LANGS-" + VERSION + ".zip")).is_file())

    def test_mixed_compiler_kits_and_lite_builds_cannot_be_published(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            rows = candidates(root, compiler="gcc")
            with self.assertRaisesRegex(ValueError, "non-LLVM"):
                llvm.assemble(root, rows, SHA, VERSION)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            rows = candidates(root, altered_lite=elf_name("usb", ra=True))
            with self.assertRaisesRegex(ValueError, "non-LLVM"):
                llvm.assemble(root, rows, SHA, VERSION)

    def test_tampered_release_fails_checksum_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage = llvm.assemble(root, candidates(root), SHA, VERSION)
            (stage / "RIPTOPL.ELF").write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "checksum"):
                llvm.verify(stage, VERSION, SHA)

    def test_release_notes_link_to_llvm_assets_and_escape_untrusted_titles(self):
        from unittest.mock import patch
        record = "b" * 8 + "\x00" + "Fix [something] *safely*"
        with patch.object(llvm, "git", return_value=record):
            notes = llvm.release_notes(VERSION, SHA)
        self.assertIn("RiptOPLLVM", notes)
        self.assertIn("/releases/download/rolling-llvm/RIPTOPL-Korium-" + VERSION + ".zip", notes)
        self.assertIn("/releases/download/rolling-llvm/RIPTOPL-RetroAchievements-" + VERSION + ".zip", notes)
        self.assertIn("Fix \\[something\\] \\*safely\\*", notes)
        for device in DEVICES:
            self.assertIn(elf_name(device), notes)
            self.assertIn(elf_name(device, ra=True).replace(".ELF", "-" + VERSION + ".zip"), notes)

    def test_compiler_producer_gate_rejects_mixed_linkage(self):
        from unittest.mock import patch
        clang = "String dump of section .comment:\n[ 0] clang version 20.0.0\n"
        with patch.object(purity.subprocess, "check_output", return_value=clang):
            purity.check("llvm-readobj", "loader.unstripped.elf")
        with patch.object(purity.subprocess, "check_output",
                          return_value=clang + "[ 2] GCC: (GNU) 14.1\n"):
            with self.assertRaisesRegex(ValueError, "GCC objects"):
                purity.check("llvm-readobj", "loader.unstripped.elf")
        with patch.object(purity.subprocess, "check_output", return_value=""):
            with self.assertRaisesRegex(ValueError, "missing Clang"):
                purity.check("llvm-readobj", "loader.unstripped.elf")
        workflow = (ROOT / ".github/workflows/rolling-llvm.yml").read_text()
        self.assertIn("tools/check_llvm_purity.py", workflow)
        self.assertIn("riptopl.unstripped.elf", workflow)

    def test_llvm_channel_does_not_take_over_latest_or_gcc_tags(self):
        workflow = (ROOT / ".github/workflows/rolling-llvm.yml").read_text()
        publisher = (ROOT / "tools/publish_llvm.py").read_text()
        self.assertEqual(llvm.TAG, "rolling-llvm")
        self.assertIn('--prerelease=true', publisher)
        self.assertIn('--latest=false', publisher)
        self.assertIn("workflow=\"rolling-llvm.yml\"", publisher)
        self.assertIn("github.repository == 'NathanNeurotic/RiptOPL'", workflow)
        self.assertIn("github.ref == 'refs/heads/rebuild/main'", workflow)
        self.assertNotIn("rolling-korium", publisher)
        self.assertNotIn("rolling-ra", publisher)
        self.assertNotIn("rolling-lite", publisher)


if __name__ == "__main__":
    unittest.main()
