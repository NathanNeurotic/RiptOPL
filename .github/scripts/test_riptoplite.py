"""RiptOPLite: per-device definitions, the dropped-IRX stubs and the rolling-lite channel."""
from pathlib import Path
import hashlib
import json
import re
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import build_edition as editions
import lite_devices as lite
import publish_lite


def embedded(config):
    main = next(t for t in config["targets"] if t["name"] == "riptopl")
    return main, {entry.partition(":")[0] for entry in main["embed_irx"]}


class DefinitionTests(unittest.TestCase):
    def test_each_device_keeps_its_drivers_and_the_backbone(self):
        _, full = embedded(editions.definition("korium"))
        for edition in editions.LITES:
            ra = editions.LITES[edition]["ra"]
            for device, options in lite.DEVICES.items():
                with self.subTest(edition=edition, device=device):
                    main, names = embedded(editions.definition(edition, device=device))
                    self.assertIn("RIPTOPLITE", main["defines"])
                    self.assertIn(options["define"], main["defines"])
                    self.assertEqual(1, sum(d.startswith("RIPTOPLITE_") for d in main["defines"]))
                    self.assertIn("BUILTIN_KORIUM", main["defines"])  # strictly Korium (user, 2026-11-02)
                    self.assertEqual(ra, "RETROACHIEVEMENTS" in main["defines"])
                    # MMCE and the network stack in every build (user, 2026-11-02).
                    self.assertLessEqual(lite.ALWAYS, names)
                    self.assertLessEqual(set(options["keep"]), names)
                    if ra:  # nothing RA needs on any device may go
                        self.assertLessEqual(lite.RA_ALWAYS | lite.RA_MODULES, names)
                    # Everything else device-specific is gone; nothing else is.
                    keep = set(options["keep"]) | (lite.RA_ALWAYS if ra else set())
                    self.assertFalse((lite.DEVICE_SPECIFIC - keep) & names)
                    base = full | (lite.RA_MODULES if ra else set())
                    self.assertEqual(base - (lite.DEVICE_SPECIFIC - keep), names)

    def test_stub_defines_every_dropped_symbol_with_size_zero(self):
        config = editions.definition("lite", device="mmce")
        main, _ = embedded(config)
        stub = next(s for s in main["sources"] if s.endswith("lite_irx_stubs.c"))
        text = (ROOT / stub).read_text(encoding="utf-8")
        full = embedded(editions.definition("korium"))[0]["embed_irx"]
        for entry in lite.dropped(full, "mmce"):
            symbol = lite.split(entry)[1]
            self.assertIn(f"unsigned char {symbol}_irx[16]", text)
            self.assertIn(f"int size_{symbol}_irx = 0;", text)
        self.assertIn("int size_smb_cdvdman_irx = 0;", text)
        self.assertNotIn("mmce_cdvdman", text)

    def test_unknown_module_names_fail_loudly(self):
        config = editions.definition("korium")
        main, _ = embedded(config)
        main["embed_irx"] = [e for e in main["embed_irx"] if not e.startswith("hdd_cdvdman")]
        with self.assertRaises(ValueError):
            lite.apply_lite(config, "usb", ROOT)

    def test_lite_needs_a_device_and_standard_configuration(self):
        with self.assertRaises(ValueError):
            editions.definition("lite", configuration="debug", device="usb")
        with self.assertRaises(ValueError):
            editions.definition("korium", device="usb")
        self.assertNotIn("lite", editions.EDITIONS)  # no install kit, bundles or configurations

    def test_loader_refuses_a_zero_sized_module(self):
        source = (ROOT / "src/platform/system.cpp").read_text(encoding="utf-8")
        body = source[source.index("int sysLoadModuleBuffer(void *buffer, int size"):]
        guard = body.index("if (buffer == NULL || size <= 0)")
        self.assertLess(guard, body.index("raii::SemaLock lock(sysLoadModuleLock);"))

    def test_header_requires_exactly_one_device(self):
        header = (ROOT / "include/riptlite.h").read_text(encoding="utf-8")
        for options in lite.DEVICES.values():
            self.assertIn(f"#ifdef {options['define']}", header)
        self.assertIn("#if LITE_DEVICES_A_ + LITE_DEVICES_B_ != 1", header)


def write_manifest(path, **fields):
    path.mkdir(parents=True, exist_ok=True)
    (path / "BUILD-MANIFEST.json").write_text(json.dumps(fields), encoding="utf-8")


class ChannelTests(unittest.TestCase):
    VERSION = '#define OPL_VERSION "v1.2.0-Beta-4000-abcdef0-PS2BUILD-Korium-GCC"\n'

    def make_root(self, root, sha, tamper=None):
        kit = {"ps2build_version": "ps2build 1", "version": self.VERSION}
        write_manifest(root / "release-korium", **kit)
        (root / "release-korium" / "LICENSE-LIBSMB2.txt").write_text("lgpl", encoding="utf-8")
        write_manifest(root / "release-ra", **kit)
        with zipfile.ZipFile(root / "release-ra" / "RIPTOPL-RetroAchievements.zip", "w") as z:
            z.writestr("BUILD-MANIFEST.json", json.dumps(kit))
            for name in ("neutrino/neutrino.elf", "neutrino/modules/raagent.irx", "neutrino/ORBIT-RA.txt",
                         "neutrino/ORBIT-RA.json", "neutrino/LICENSE", *publish_lite.RA_KIT_FILES):
                z.writestr(name, name)
            z.writestr("APPS/APP_RIPTOPL-RA/RIPTOPL-RA.ELF", "full")
        for edition, options in editions.LITES.items():
            for device in lite.DEVICES:
                name = lite.elf_name(device, options["ra"])
                elf = f"{edition}-{device}".encode()
                stage = root / f"{edition}-{device}"
                manifest = {"repository": "NathanNeurotic/RiptOPL", "source_commit": sha, "dirty": False,
                            "edition": edition, "device": device, "compiler": "gcc", "configuration": "standard",
                            "theme": "korium", "ra": options["ra"], "ps2build_version": "ps2build 1", "elf": name,
                            "elf_sha256": hashlib.sha256(elf).hexdigest(), "version": self.VERSION}
                if tamper == (edition, device):
                    manifest["source_commit"] = "b" * 40
                write_manifest(stage, **manifest)
                (stage / name).write_bytes(elf)

    def test_channel_holds_korium_elfs_and_ra_zips(self):
        sha = "a" * 40
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_root(root, sha)
            path, sums = publish_lite.assemble(root, sha)
            self.assertEqual(sums, publish_lite.verify(path))
            self.assertEqual(set(sums), publish_lite.expected_assets())
            for device, options in lite.DEVICES.items():
                self.assertEqual((path / f"RIPTOPLITE-{options['title']}.ELF").read_bytes(), f"lite-{device}".encode())
                self.assertFalse((path / f"RIPTOPLITE-RA-{options['title']}.ELF").exists())  # RA ships as a ZIP
                with zipfile.ZipFile(path / f"RIPTOPLITE-RA-{options['title']}.zip") as z:
                    names = set(z.namelist())
                    self.assertEqual(z.read(f"RIPTOPLITE-RA-{options['title']}.ELF"), f"lite_ra-{device}".encode())
                    self.assertIn("neutrino/modules/raagent.irx", names)  # ORBIT's runtime, RA's outside need
                    self.assertLessEqual({"README.txt", "BUILD-MANIFEST.json", *publish_lite.RA_KIT_FILES}, names)
                    self.assertFalse(any(n.startswith("APPS/") for n in names))
            # Korium ELFs carry no RA resources.
            self.assertFalse(any("RA" in n and n.endswith(".ELF") for n in sums))

    def test_a_build_from_another_commit_is_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_root(root, "a" * 40, tamper=("lite_ra", "hdd"))
            with self.assertRaises(ValueError):
                publish_lite.assemble(root, "a" * 40)

    def test_notes_link_every_download(self):
        body = publish_lite.release_notes("a" * 40, "v1.2.0-Beta-4000", lambda *a: "b" * 40 + "\x00Add [lite]")
        for options in lite.DEVICES.values():
            self.assertIn(f"/releases/download/rolling-lite/RIPTOPLITE-{options['title']}.ELF)", body)
            self.assertIn(f"/releases/download/rolling-lite/RIPTOPLITE-RA-{options['title']}-v1.2.0-Beta-4000.zip)", body)
        self.assertIn(r"Add \[lite\]", body)
        for link in re.findall(r"\]\((\S+?\.zip)\)", body):
            self.assertTrue(link.endswith("-v1.2.0-Beta-4000.zip"), link)


if __name__ == "__main__":
    unittest.main()
