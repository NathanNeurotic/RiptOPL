"""Host-only regression tests for the independent RiptOPLLVM release channel."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import publish_llvm as llvm
import check_llvm_purity as purity
from build_configuration import CONFIGURATIONS
from lite_devices import DEVICES, elf_name


SHA = "a" * 40
VERSION = "v1.2.0-Beta-4059"


def put(path, name, content=b"ELF"):
    target = path / name
    if name.endswith(".zip"):
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("payload.txt", content)
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
        self.assertNotIn("GH_TOKEN", publish.get("env", {}))
        token_steps = [step for step in publish["steps"] if "GH_TOKEN" in step.get("env", {})]
        self.assertEqual(len(token_steps), 1)
        self.assertEqual(token_steps[0]["run"], "python tools/publish_llvm.py dist/downloaded")
        self.assertIn("needs", llvm_flow["jobs"]["publish"])
        self.assertIn("--compiler llvm", (ROOT / ".github/workflows/rolling-llvm.yml").read_text())
        self.assertIn("compiler=COMPILER", (ROOT / "tools/publish_llvm.py").read_text())

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
