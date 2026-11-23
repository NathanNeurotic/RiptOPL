"""NULD Neutrino RA Watch: when the RA runtime pin moves by itself, and when it must not."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".github/scripts"))
import orbit_neutrino_watch as watch  # noqa: E402

WORKFLOWS = ROOT / ".github/workflows"
AGENT = "#define PAYLOAD     1472 // 1500 MTU - IP - UDP\n#define AGENT_BYTES (PAYLOAD - 256) // values in one packet\n"
COMMIT = "c0ffee" * 6 + "abcd"


class OrbitWatchTests(unittest.TestCase):
    def setUp(self):
        self.pin = json.loads(watch.PIN.read_text())
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name)

    def release(self, tag=None, name=None, digest="pin", **extra):
        digest = "sha256:" + self.pin["sha256"] if digest == "pin" else digest
        asset = {"name": name or self.pin["asset"], "digest": digest}
        release = {"tag_name": tag or self.pin["tag"], "html_url": "https://github.com/danielnuld/nuld/releases/tag/x",
                   "draft": False, "prerelease": False, "assets": [{"name": "launcher.elf"}, asset]}
        release.update(extra)
        return release

    def check(self, release, *args):
        path = self.out / "release.json"
        path.write_text(json.dumps(release))
        watch.main(["check", "--release", str(path), "--out", str(self.out / "o"), *args])
        return (self.out / "o/status").read_text()

    def test_pinned_release_is_current(self):
        self.assertEqual(self.check(self.release()), "current")

    def test_digest_compare_ignores_case(self):
        self.assertEqual(self.check(self.release(digest="sha256:" + self.pin["sha256"].upper())), "current")

    def test_newer_or_replaced_release_is_new(self):
        self.assertEqual(self.check(self.release("v9.9", "NULD-v9.9.zip", "sha256:" + "ab" * 32)), "new")
        self.assertEqual(self.check(self.release(digest="sha256:" + "ab" * 32)), "new")

    def test_missing_digest_asks_for_a_download(self):
        self.assertEqual(self.check(self.release(digest="")), "unverified")
        self.assertEqual(self.check(self.release(digest=""), "--asset-sha256", self.pin["sha256"]), "current")

    def test_agent_snapshot_limit(self):
        self.assertEqual(watch.agent_snapshot_bytes(AGENT), 1216)
        self.assertEqual(watch.agent_snapshot_bytes("#define AGENT_BYTES 1000\n"), 1000)
        self.assertIsNone(watch.agent_snapshot_bytes("#define AGENT_BYTES (PAYLOAD / 2)\n"))
        self.assertEqual(self.pin["snapshot_bytes_max"], 1216)


class OrbitUpdateTests(unittest.TestCase):
    """update: launcher-only releases change nothing; a changed neutrino/ moves the pin and the links."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        self.root = self.dir / "repo"
        for relative in ("third_party/orbit-neutrino/release.json", *watch.DOC_SITES):
            (self.root / relative).parent.mkdir(parents=True, exist_ok=True)
            (self.root / relative).write_bytes((ROOT / relative).read_bytes())
        self.old = self.archive("old.zip")
        self.pin = dict(json.loads(watch.PIN.read_text()), sha256=hashlib.sha256(self.old.read_bytes()).hexdigest())

    def archive(self, name, elf=b"ELF -ra=<file>", launcher=b"launcher"):
        path = self.dir / name
        files = {n: b"payload " + n.encode() for n in (
            "modules/ee_core.elf", "modules/raagent.irx", "modules/IEEE1394_bd_mini.irx", "modules/iLinkman.irx",
            "config/system.toml", "config/bsd-udpfs.toml", "LICENSE")}
        files["neutrino.elf"] = elf
        with zipfile.ZipFile(path, "w") as z:
            for member, data in files.items():
                z.writestr("NULD/neutrino/" + member, data)
            z.writestr("NULD/nuld.elf", launcher)
        return path

    def release(self, new, tag="v9.9"):
        digest = "sha256:" + hashlib.sha256(new.read_bytes()).hexdigest()
        return {"tag_name": tag, "html_url": "https://github.com/danielnuld/nuld/releases/tag/" + tag,
                "draft": False, "prerelease": False, "assets": [{"name": "NULD-" + tag + ".zip", "digest": digest}]}

    def update(self, new, agent=AGENT, release=None):
        return watch.update(release or self.release(new), self.pin, self.old, new, agent, COMMIT, self.root,
                            today="2026-11-23")

    def snapshot(self):
        return {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}

    def test_launcher_only_release_changes_nothing(self):
        before = self.snapshot()
        status, message, touched = self.update(self.archive("new.zip", launcher=b"launcher v1.3"))
        self.assertEqual((status, touched), ("launcher-only", []))
        self.assertIn("byte-identical", message)
        self.assertEqual(self.snapshot(), before)

    def test_changed_runtime_moves_the_pin_and_links(self):
        new = self.archive("new.zip", elf=b"ELF v2 -ra=<file>")
        status, message, touched = self.update(new)
        self.assertEqual(status, "updated")
        pin = json.loads((self.root / "third_party/orbit-neutrino/release.json").read_text())
        self.assertEqual((pin["tag"], pin["asset"], pin["source_commit"], pin["sha256"]),
                         ("v9.9", "NULD-v9.9.zip", COMMIT, hashlib.sha256(new.read_bytes()).hexdigest()))
        self.assertEqual(pin["snapshot_bytes_max"], self.pin["snapshot_bytes_max"])
        self.assertIn("- neutrino.elf", message)
        self.assertNotIn("raagent", message)
        old_url = "https://github.com/" + self.pin["repository"] + "/releases/tag/" + self.pin["tag"]
        for relative in touched[1:]:
            text = (self.root / relative).read_text()
            self.assertNotIn(old_url, text, relative)
            self.assertIn("https://github.com/danielnuld/nuld/releases/tag/v9.9", text, relative)
        notes = (self.root / watch.NOTES).read_text()
        self.assertIn("/tree/" + COMMIT + "/third_party/neutrino-igr)", notes)
        self.assertIn("v9.9 (updated automatically by NULD Neutrino Watch on 2026-11-23", notes)
        self.assertIn("README.md", touched)
        # The packer accepts what the watch pinned.
        with tempfile.TemporaryDirectory() as out:
            watch.staging().stage_orbit(new, Path(out) / "neutrino", pin)

    def test_new_tag_extending_the_old_one_is_linked_once(self):
        tag = self.pin["tag"] + ".1"
        new = self.archive("ext.zip", elf=b"ELF v2 -ra=<file>")
        self.update(new, release=self.release(new, tag))
        url = "https://github.com/danielnuld/nuld/releases/tag/" + tag
        text = (self.root / "README.md").read_text()
        self.assertIn(url, text)
        self.assertNotIn(url + ".1", text)

    def test_refusals_write_nothing(self):
        before = self.snapshot()
        cases = {
            "no handoff": (self.archive("a.zip", elf=b"stock"), AGENT, None),
            "new snapshot limit": (self.archive("b.zip", elf=b"ELF v2 -ra=<file>"), AGENT.replace("1472", "1400"), None),
            "unreadable agent": (self.archive("c.zip", elf=b"ELF v2 -ra=<file>"), "", None),
        }
        bad = self.archive("d.zip", elf=b"ELF v3 -ra=<file>")
        cases["no published digest"] = (bad, AGENT, dict(self.release(bad), assets=[{"name": "NULD-v9.9.zip"}]))
        cases["digest mismatch"] = (bad, AGENT, dict(self.release(bad), assets=[{"name": "NULD-v9.9.zip",
                                                                                  "digest": "sha256:" + "00" * 32}]))
        for label, (new, agent, release) in cases.items():
            with self.subTest(label), self.assertRaises(SystemExit):
                self.update(new, agent, release)
            self.assertEqual(self.snapshot(), before, label)

    def test_every_file_linking_the_pinned_release_is_rewritten(self):
        pin = json.loads(watch.PIN.read_text())
        url = "https://github.com/" + pin["repository"] + "/releases/tag/" + pin["tag"]
        listed = subprocess.run(["git", "-C", str(ROOT), "grep", "-l", "-F", url], capture_output=True, text=True).stdout
        tests = {".github/scripts/test_release_publication.py"}
        self.assertEqual(set(listed.split()) - tests, set(watch.DOC_SITES))


class OrbitWatchWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.text = (WORKFLOWS / "orbit-neutrino-watch.yml").read_text()

    def test_updates_instead_of_opening_issues(self):
        self.assertNotIn("issue", self.text.replace("never opens issues", "").replace("It never opens issues", ""))
        for needed in ("contents: write", "actions: write", "git push origin HEAD:rebuild/main",
                       "gh workflow run rolling-release.yml", "orbit_neutrino_watch.py update"):
            self.assertIn(needed, self.text)

    def test_tests_run_before_the_commit(self):
        self.assertLess(self.text.index("test_package_orbit_neutrino.py"), self.text.index("git commit"))

    def test_stable_releases_only_and_daily(self):
        self.assertIn("select(.prerelease | not)", self.text)
        self.assertIn('cron: "41 5 * * *"', self.text)
        self.assertIn("ORBIT_REPO: danielnuld/nuld\n", self.text)

    def test_no_workflow_file_is_committed(self):
        # A GITHUB_TOKEN push may not change .github/workflows; docs-sync.yml reads the pin instead.
        add = re.search(r"git add[^\n]*", self.text).group(0)
        self.assertNotIn(".github/workflows", add)
        self.assertTrue(all(not site.startswith(".github/workflows") for site in watch.DOC_SITES))
        docs_sync = (WORKFLOWS / "docs-sync.yml").read_text()
        self.assertIn('Path("third_party/orbit-neutrino/release.json")', docs_sync)
        self.assertNotIn("danielnuld/nuld/releases/tag/v", docs_sync)


if __name__ == "__main__":
    unittest.main()
