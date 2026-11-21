"""A re-run's verify and publish jobs take each build's newest attempt, never an older one with the same name."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/newest_attempts.py"
WORKFLOWS = ROOT / ".github/workflows"


class NewestAttemptsTests(unittest.TestCase):
    def run_script(self, folders):
        with tempfile.TemporaryDirectory() as directory:
            source, destination = Path(directory) / "attempts", Path(directory) / "downloaded"
            for name, content in folders.items():
                (source / name).mkdir(parents=True)
                (source / name / "BUILD-MANIFEST.json").write_text(content)
            subprocess.run([sys.executable, "-S", str(SCRIPT), str(source), str(destination)], check=True,
                           capture_output=True)
            return {path.name: (path / "BUILD-MANIFEST.json").read_text() for path in destination.iterdir()}

    def test_the_newest_attempt_of_each_build_wins(self):
        self.assertEqual(self.run_script({
            "variant-korium-extras-attempt1": "old", "variant-korium-extras-attempt3": "new",
            "variant-korium-extras-attempt10": "newest",
            "release-korium-attempt1": "kept",  # "Re-run failed jobs" leaves a successful job's attempt
            "release-log-korium-standard": "log",
        }), {"variant-korium-extras": "newest", "release-korium": "kept", "release-log-korium-standard": "log"})

    def test_release_workflows_name_builds_by_attempt_and_verify_the_newest(self):
        for name in ("rolling-release.yml",):
            text = (WORKFLOWS / name).read_text()
            for artifact in ("name: variant-${{ matrix.target.edition }}-${{ matrix.target.configuration }}",
                             "name: ${{ matrix.target.edition }}-${{ matrix.target.device }}",
                             "name: release-${{ matrix.target.edition }}"):
                self.assertIn(artifact + "-attempt${{ github.run_attempt }}\n", text, name)
            self.assertNotIn("path: dist/downloaded\n", text, name)
            downloads = text.count("pattern: lite*-*\n          path: dist/attempts\n")
            self.assertGreaterEqual(downloads, 1, name)
            self.assertEqual(text.count("run: python -S tools/newest_attempts.py dist/attempts dist/downloaded"),
                             downloads, name)


if __name__ == "__main__":
    unittest.main()
