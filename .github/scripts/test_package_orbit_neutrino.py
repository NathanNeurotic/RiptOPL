"""The RA kit's ORBIT Neutrino runtime: pinned extraction, refusal before any write, and the marker
contract with sysNeutrinoRARuntime. Ported from the original's test_package_orbit_neutrino.py."""
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import package_edition  # noqa: E402


class OrbitStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.destination = self.root / "neutrino"
        self.pin = json.loads(package_edition.ORBIT_PIN.read_text())

    def archive(self, missing=None, extra=None, loader=b"ELF -ra=<file>"):
        archive = self.root / "runtime.zip"
        files = {name: b"payload" for name in (
            "neutrino.elf", "modules/ee_core.elf", "modules/raagent.irx", "modules/IEEE1394_bd_mini.irx",
            "modules/iLinkman.irx",
            "config/system.toml", "config/bsd-udpfs.toml", "LICENSE")}
        files["neutrino.elf"] = loader
        files.pop(missing, None)
        with zipfile.ZipFile(archive, "w") as z:
            for name, data in files.items():
                z.writestr("ORBIT-test/neutrino/" + name, data)
            z.writestr("ORBIT-test/orbit.elf", b"launcher must not ship")
            if extra:
                info = zipfile.ZipInfo("entry")
                info.filename = extra
                z.writestr(info, b"unsafe")
        self.pin["sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
        return archive

    def test_stages_runtime_only_with_provenance(self):
        package_edition.stage_orbit(self.archive(), self.destination, self.pin)
        self.assertTrue((self.destination / "modules/raagent.irx").is_file())
        self.assertFalse((self.destination / "orbit.elf").exists())
        self.assertFalse((self.root / "orbit.elf").exists())
        self.assertEqual(json.loads((self.destination / "ORBIT-RA.json").read_text()), self.pin)
        label = (self.destination / "RA-EDITION-NEUTRINO.txt").read_text()
        self.assertIn("RETROACHIEVEMENTS EDITION", label)
        self.assertIn("NOT the standard RiptOPL", label)
        self.assertIn("Neutrino v1.8.0", label)
        self.assertIn(f"https://github.com/{self.pin['repository']}/releases/tag/{self.pin['tag']}", label)

    def test_marker_is_what_the_loader_compares(self):
        package_edition.stage_orbit(self.archive(), self.destination, self.pin)
        source = (ROOT / "src/platform/sysneutrino.cpp").read_text()
        check = re.search(r'len < (\d+) \|\| memcmp\(text, "([^"]+)", (\d+)\)', source)
        self.assertIsNotNone(check)
        expected = check.group(2).replace("\\n", "\n").encode()
        self.assertEqual(int(check.group(1)), len(expected))
        self.assertEqual(int(check.group(3)), len(expected))
        marker = (self.destination / "ORBIT-RA.txt").read_bytes()
        self.assertTrue(marker.startswith(expected))
        self.assertLessEqual(len(expected), 40)  # the loader reads 40 bytes

    def test_snapshot_limit_matches_the_pin(self):
        source = (ROOT / "src/sources/supportbase.cpp").read_text()
        limit = int(re.search(r"#define RA_ORBIT_SNAPSHOT_MAX (\d+)", source).group(1))
        self.assertEqual(limit, self.pin["snapshot_bytes_max"])
        self.assertFalse(self.pin["pointer_chains"])

    def test_refuses_before_writing_anything(self):
        archive = self.archive()
        self.pin["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "digest"):
            package_edition.stage_orbit(archive, self.destination, self.pin)
        self.assertFalse(self.destination.exists())
        for kwargs in ({"missing": "modules/raagent.irx"}, {"missing": "modules/IEEE1394_bd_mini.irx"},
                       {"missing": "modules/iLinkman.irx"},
                       {"loader": b"stock"}, {"extra": "ORBIT-test/neutrino/../escape"},
                       {"extra": "/absolute"}, {"extra": "bad\\windows"},
                       {"extra": "ORBIT-test/neutrino/LICENSE"}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                package_edition.stage_orbit(self.archive(**kwargs), self.destination, self.pin)
            self.assertFalse(self.destination.exists())

    def test_pin_is_the_reviewed_release(self):
        self.assertEqual(self.pin["repository"], "danielnuld/ps2-launcher")
        self.assertEqual(self.pin["tag"], "v1.0.2")
        self.assertEqual(self.pin["sha256"], "a5cf88e2f27d3745b01c93d6d691b162972749b35452a38461046a02b3f178a5")


if __name__ == "__main__":
    unittest.main()
