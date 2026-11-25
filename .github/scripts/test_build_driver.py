"""SDK version gate, compiler path preservation and overlay cleanup."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import yaml


ROOT = Path(__file__).resolve().parents[2]
TEMP_DIR = ROOT / "build/test-tmp"
TEMP_DIR.mkdir(parents=True, exist_ok=True)
SPEC = importlib.util.spec_from_file_location("build_driver", ROOT / "tools/build.py")
driver = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(driver)


class BuildDriverTests(unittest.TestCase):
    def setUp(self):
        sdk = tempfile.TemporaryDirectory(dir=TEMP_DIR)
        self.addCleanup(sdk.cleanup)
        self.sdk = Path(sdk.name)
        (self.sdk / "versions.yaml").write_text("components: {sdk-core: v2026.10.13}\n")
        environment = patch.dict(os.environ, {"PS2BUILD_HOME": str(self.sdk)})
        environment.start()
        self.addCleanup(environment.stop)
        version = patch.object(driver.subprocess, "check_output", return_value="ps2build v2026.10.13\n")
        version.start()
        self.addCleanup(version.stop)

    def test_overlay_preserves_project_settings_without_workaround(self):
        original = yaml.safe_load((ROOT / "ps2.yaml").read_text())
        captured = []

        def compile_project(command, cwd):
            config = Path(command[command.index("--config") + 1])
            captured.append(config)
            self.assertEqual(config.parent, ROOT)
            self.assertEqual(cwd, ROOT)
            generated = yaml.safe_load(config.read_text())
            self.assertEqual(generated, original)
            self.assertNotIn("-sys-header-deps", config.read_text())
            return 1  # Failed builds must also remove the temporary config.

        with patch.object(driver.subprocess, "call", side_effect=compile_project):
            self.assertEqual(driver.build("llvm", "build/llvm", original), 1)
        self.assertFalse(captured[0].exists())

    def test_both_compilers_use_original_configuration_without_overlay(self):
        with patch.object(driver.subprocess, "call", return_value=0) as run:
            self.assertEqual(driver.build("gcc", "build/gcc"), 0)
        self.assertEqual(run.call_args.args[0][:2], ["ps2build", "build"])
        self.assertEqual(run.call_args.args[0][-2:], ["--config", str(ROOT / "ps2.yaml")])

    def test_llvm_build_links_iop_modules_through_the_repair_step(self):
        # "ps2build build" is "generate" then ninja: an LLVM build runs the two itself and, in between, sends
        # the IOP link rule through tools/link_iop.py (ld.lld -r writes some address halves 64 KiB off).
        out = TEMP_DIR.relative_to(ROOT) / "llvm-driver"
        (ROOT / out).mkdir(exist_ok=True)
        ninja_file = ROOT / out / "build.ninja"
        commands = []

        def step(command, cwd):
            commands.append(command)
            if command[:2] == ["ps2build", "generate"]:
                ninja_file.write_text("rule iop_link\n  command = ld.lld -T$linkfile -o $out $in $flags $libs\n")
            return 0

        with patch.object(driver.subprocess, "call", side_effect=step):
            self.assertEqual(driver.build("llvm", str(out)), 0)
        self.assertEqual(commands[0][-2:], ["--config", str(ROOT / "ps2.yaml")])
        self.assertEqual(Path(commands[1][0]).parent, self.sdk / "tools")
        self.assertEqual(commands[1][1:], ["-C", str(out)])
        self.assertIn("link_iop.py ld.lld -T$linkfile", ninja_file.read_text())
        # A failed generate step is the build's result, and ninja is not started.
        with patch.object(driver.subprocess, "call", return_value=3) as run:
            self.assertEqual(driver.build("llvm", str(out)), 3)
        self.assertEqual(run.call_count, 1)

    def test_sdk_modules_with_an_address_outside_the_module_are_corrected_before_the_build(self):
        import struct

        def module(high):   # one lui/addiu pair in a 0x9000-byte module: address (high << 16) - 0x7D6C
            text = struct.pack('<II', 0x3C010000 | high, 0x24218294)
            rel = struct.pack('<IIII', 0, (1 << 8) | 5, 4, (1 << 8) | 6)
            names = b'\0.text\0.rel.text\0.shstrtab\0'
            header = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01' + bytes(9), 0xFF80, 8, 1, 0, 0x34,
                                 0x54 + len(text + rel + names), 0, 0x34, 0x20, 1, 0x28, 4, 3)
            sections = bytes(0x28) + struct.pack('<10I', 1, 1, 6, 0, 0x54, 8, 0, 0, 4, 0)
            sections += struct.pack('<10I', 7, 9, 0, 0, 0x5C, 16, 0, 1, 4, 8)
            sections += struct.pack('<10I', 17, 3, 0, 0, 0x6C, len(names), 0, 0, 1, 0)
            return header + struct.pack('<8I', 1, 0x54, 0, 0, 8, 0x9000, 7, 16) + text + rel + names + sections

        (self.sdk / "packages/core/irx").mkdir(parents=True)
        wrong, right = self.sdk / "packages/core/irx/wrong.irx", self.sdk / "packages/core/irx/right.irx"
        wrong.write_bytes(module(0))
        right.write_bytes(module(1))
        (self.sdk / "packages/core/irx/notes.irx").write_text("not a module")
        with patch.object(driver.subprocess, "call", return_value=0):
            self.assertEqual(driver.build("gcc", "build/gcc"), 0)
        self.assertEqual(wrong.read_bytes(), module(1))
        self.assertEqual(right.read_bytes(), module(1))

    def test_old_or_unknown_sdk_fails_before_compiling(self):
        for version in ("ps2build v2026.10.12.2", "ps2build v2026.10.12.1", "ps2build v2026.10.12", "ps2build dev"):
            with patch.object(driver.subprocess, "check_output", return_value=version), patch.object(driver.subprocess, "call") as run:
                with self.assertRaisesRegex(RuntimeError, "run ps2build update"):
                    driver.build("llvm", "build/llvm")
                run.assert_not_called()
        with patch.object(driver.subprocess, "check_output", return_value="ps2build v2026.10.13"):
            driver.require_supported_sdk()

    def test_new_cli_with_missing_old_or_invalid_core_refuses_before_compiling(self):
        metadata = self.sdk / "versions.yaml"
        for value in ("components: {sdk-core: v2026.10.12.2}", "components: {}", "[]", "components: [", ""):
            metadata.write_text(value)
            with patch.object(driver.subprocess, "call") as compile_project:
                with self.assertRaisesRegex(RuntimeError, "sdk-core v2026.10.13"):
                    driver.build("gcc", "build/gcc")
                compile_project.assert_not_called()
        metadata.unlink()
        with self.assertRaisesRegex(RuntimeError, "run ps2build update"):
            driver.require_supported_sdk()
        metadata.write_text("components: {sdk-core: v2026.10.14}")
        driver.require_supported_sdk()

    def test_launch_failure_removes_temporary_configuration(self):
        with tempfile.TemporaryDirectory(dir=TEMP_DIR) as directory:
            root = Path(directory)
            (root / "ps2.yaml").write_text("targets: [{name: test, cflags: []}]")
            with patch.object(driver, "ROOT", root), patch.object(driver.subprocess, "call", side_effect=OSError("launch failed")):
                with self.assertRaises(OSError):
                    driver.build("llvm", "build", {"targets": [{"name": "test", "cflags": []}]})
            self.assertEqual(list(root.iterdir()), [root / "ps2.yaml"])


if __name__ == "__main__":
    unittest.main()
