"""Test publication authorization and first-release/existing-tag transitions."""
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import publish_editions as publisher


class PublicationTests(unittest.TestCase):
    def test_concise_notes_for_all_three_editions(self):
        sha = "a" * 40
        history = "b" * 40 + "\x00Fix [launch] <edge>"
        for options in publisher.EDITIONS.values():
            with patch.object(publisher, "git", return_value=history) as git:
                body = publisher.release_notes(options, sha, "v1.8.0")
            git.assert_called_once_with("log", "--no-merges", "-10", "--format=%H%x00%s", sha, "--")
            self.assertIn("## Changes\n\n", body)
            self.assertIn(r"Fix \[launch\] \<edge\>", body)
            self.assertIn("/commit/" + "b" * 40, body)
            self.assertIn("/commits/" + sha, body)
            self.assertIn(f"/releases/download/{options['tag']}/RIPTOPL-{options['title']}.zip", body)
            elf = "RIPTOPL-RA.ELF" if options["ra"] else "RIPTOPL.ELF"
            psu = "APP_RIPTOPL-RA.psu" if options["ra"] else "APP_RIPTOPL.psu"
            self.assertIn(f"/{elf})", body)
            self.assertIn(f"/{psu})", body)
            self.assertEqual(3, sum(line.startswith("- [") for line in body.split("## Downloads\n\n", 1)[1].splitlines()))
            self.assertIn("Included build: v1.8.0 ", body)
            self.assertNotIn("Release compiler", body)
            self.assertNotIn("Imported production documentation", body)
            self.assertLess(len(body), 1800)

    def test_notes_refuse_empty_history(self):
        with patch.object(publisher, "git", return_value=""), self.assertRaises(ValueError):
            publisher.release_notes(publisher.EDITIONS["korium"], "a" * 40, "v1")

    def test_only_matching_integration_push_or_dispatch_can_publish(self):
        environment = {
            "GITHUB_REPOSITORY": "NathanNeurotic/RiptOPL",
            "GITHUB_EVENT_NAME": "push",
            "GITHUB_REF": "refs/heads/rebuild/main",
            "GITHUB_SHA": "abc123",
        }
        for event in ("push", "workflow_dispatch"):
            self.assertTrue(publisher.publication_allowed(dict(environment, GITHUB_EVENT_NAME=event), "abc123"))
        for key, value in (
            ("GITHUB_REPOSITORY", "someone/RiptOPL"),
            ("GITHUB_EVENT_NAME", "pull_request"),
            ("GITHUB_EVENT_NAME", "pull_request_target"),
            ("GITHUB_REF", "refs/heads/main"),
            ("GITHUB_SHA", "different"),
        ):
            self.assertFalse(publisher.publication_allowed(dict(environment, **{key: value}), "abc123"))
        self.assertFalse(publisher.publication_allowed({}, "abc123"))

    def test_only_incebtion_is_designated_latest(self):
        self.assertEqual(set(publisher.EDITIONS), {"korium", "ra", "incebtion"})
        for edition in publisher.EDITIONS:
            expected = "--latest=true" if edition == "incebtion" else "--latest=false"
            self.assertEqual(publisher.latest_release_arg(edition), expected)

    def test_first_draft_release_creates_missing_tag(self):
        result = subprocess.CompletedProcess([], 1, "", "gh: Not Found (HTTP 404)")
        with patch.object(publisher.subprocess, "run", return_value=result), patch.object(publisher, "gh") as gh:
            publisher.update_tag("owner/repo", "rolling-ra", "commit")
        gh.assert_called_once_with("api", "repos/owner/repo/git/refs", "-X", "POST", "-f",
                                   "ref=refs/tags/rolling-ra", "-f", "sha=commit")

    def test_existing_tag_moves_and_api_errors_do_not_create_tags(self):
        with patch.object(publisher.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "{}", "")), patch.object(publisher, "gh") as gh:
            publisher.update_tag("owner/repo", "rolling-korium", "commit")
        gh.assert_called_once_with("api", "repos/owner/repo/git/refs/tags/rolling-korium", "-X", "PATCH",
                                   "-f", "sha=commit", "-F", "force=true")
        for error in ("HTTP 401", "HTTP 403", "HTTP 500", "connection failed"):
            with patch.object(publisher.subprocess, "run", return_value=subprocess.CompletedProcess([], 1, "", error)), patch.object(publisher, "gh") as gh:
                with self.assertRaises(RuntimeError):
                    publisher.update_tag("owner/repo", "rolling-ra", "commit")
                gh.assert_not_called()

    def test_draft_release_resolution_uses_release_view_json(self):
        source = (ROOT / "tools" / "publish_editions.py").read_text(encoding="utf-8")
        self.assertIn('gh("release", "view", tag, "--repo", repo, "--json", "id,assets")', source)
        self.assertNotIn('repos/{repo}/releases/tags/{tag}', source)


if __name__ == "__main__":
    unittest.main()
