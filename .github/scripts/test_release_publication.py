"""Test publication authorization and first-release/existing-tag transitions."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
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
            if "into" in options:
                continue  # a companion's links are in its parent's notes
            with patch.object(publisher, "git", return_value=history) as git:
                body = publisher.release_notes(options, sha, "NULD-v1.2.12" if options["ra"] else "v1.8.0", "v1.2.0-Beta-3925")
            git.assert_called_once_with("log", "--no-merges", "-10", "--format=%H%x00%s", sha, "--")
            self.assertIn("## Changes\n\n", body)
            self.assertIn(r"Fix \[launch\] \<edge\>", body)
            self.assertIn("/commit/" + "b" * 40, body)
            self.assertIn("/commits/" + sha, body)
            self.assertIn(f"**RiptOPL {options['title']}** `v1.2.0-Beta-3925`", body)
            # The standard full kit leads the notes, before the changelog.
            kit = f"RIPTOPL-{options['title']}-v1.2.0-Beta-3925.zip"
            self.assertIn(f"**Recommended download:** [{kit}](https://github.com/NathanNeurotic/RiptOPL/releases/download/"
                          f"{options['tag']}/{kit}) - the standard full install kit.", body)
            self.assertLess(body.index("**Recommended download:**"), body.index("## Changes"))
            self.assertIn(f"/releases/download/{options['tag']}/RIPTOPL-{options['title']}-v1.2.0-Beta-3925.zip)", body)
            elf = "RIPTOPL-RA.ELF" if options["ra"] else "RIPTOPL.ELF"
            psu = "APP_RIPTOPL-RA.psu" if options["ra"] else "APP_RIPTOPL.psu"
            self.assertIn(f"/{elf})", body)
            self.assertIn(f"/{psu})", body)
            self.assertIn(f"/releases/download/{options['tag']}/RIPTOPL-LANGS-v1.2.0-Beta-3925.zip)", body)
            # The standalone ELF and PSU have no CREDITS beside them: the notes name usmb2 and its licence.
            self.assertIn(f"- [usmb2 licence](https://github.com/NathanNeurotic/RiptOPL/releases/download/{options['tag']}"
                          "/LICENSE-USMB2.txt) - every loader embeds [usmb2](https://github.com/sahlberg/usmb2) by "
                          "[Ronnie Sahlberg](https://github.com/sahlberg), used under the MIT licence.\n", body)
            self.assertLess(body.index(f"/{psu})"), body.index("/LICENSE-USMB2.txt)"))
            # Every archive link ends with the version; the ELF and PSU links never change.
            for link in re.findall(r"\]\((\S+?)\)", body.split("## Downloads", 1)[1]):
                if link.endswith(".zip"):
                    self.assertTrue(link.endswith("-v1.2.0-Beta-3925.zip"), link)
            if options["tag"] == "rolling-ra":
                for name in ("RIPTOPL-RetroAchievements-IncebtIon-v1.2.0-Beta-3925.zip", "RIPTOPL-RA-IncebtIon.ELF",
                             "APP_RIPTOPL-RA-IncebtIon.psu", "RIPTOPL-RA-VARIANTS-IncebtIon-v1.2.0-Beta-3925.zip"):
                    self.assertIn(f"/releases/download/rolling-ra/{name})", body)
            if not options["ra"]:  # the bug-report build sits right under the kit
                self.assertIn(f"/releases/download/{options['tag']}/RIPTOPL-BUGREPORT-v1.2.0-Beta-3925.zip)", body)
                self.assertLess(body.index("[Bug-report build]"), body.index("[Loader ELF]"))
            else:
                self.assertNotIn("BUGREPORT", body)
            self.assertEqual({"rolling-ra": 6}.get(options["tag"], 8), sum(line.startswith("- [") for line in body.split("## Downloads\n\n", 1)[1].splitlines()))
            if options["ra"]:  # the RA kits ship ORBIT's runtime, and their notes link its pinned release
                self.assertIn("Included build: NULD-v1.2.12 -- [ORBIT Neutrino RA runtime v1.2.12]"
                              "(https://github.com/danielnuld/nuld/releases/tag/v1.2.12)", body)
                self.assertIn("by [danielnuld](https://github.com/danielnuld)", body)
            else:
                self.assertIn("Included build: v1.8.0 -- Neutrino.", body)
                self.assertNotIn("ORBIT", body)
            self.assertNotIn("Release compiler", body)
            self.assertNotIn("Imported production documentation", body)
            self.assertLess(len(body), 2600)  # the RA notes link eight versioned downloads, ORBIT, the usmb2 licence and the recommended kit

    def test_ra_notes_refuse_a_runtime_other_than_the_pin(self):
        with patch.object(publisher, "git", return_value="b" * 40 + "\x00Fix"), self.assertRaises(ValueError):
            publisher.release_notes(publisher.EDITIONS["ra"], "a" * 40, "v1.8.0", "v1.2.0-Beta-1")

    def test_notes_refuse_empty_history(self):
        with patch.object(publisher, "git", return_value=""), self.assertRaises(ValueError):
            publisher.release_notes(publisher.EDITIONS["korium"], "a" * 40, "v1", "v1.2.0-Beta-1")

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

    def test_overtaken_run_finds_the_newer_heads_run(self):
        head = "c" * 40

        def fake_gh(*args, retry=True):
            if args[1].endswith("/branches/rebuild/main"):
                return json.dumps({"commit": {"sha": head}})
            self.assertIn("head_sha=" + head, args[1])
            return json.dumps({"workflow_runs": runs})

        with patch.object(publisher, "gh", side_effect=fake_gh), patch.object(publisher.time, "sleep"):
            for run, expected in (
                ({"event": "push", "conclusion": None, "html_url": "u1"}, "u1"),
                ({"event": "workflow_dispatch", "conclusion": "success", "html_url": "u2"}, "u2"),
                ({"event": "push", "conclusion": "failure", "html_url": "u3"}, None),
                ({"event": "push", "conclusion": "cancelled", "html_url": "u4"}, None),
                ({"event": "pull_request", "conclusion": None, "html_url": "u5"}, None),
            ):
                runs = [run]
                self.assertEqual(expected, publisher.successor_run("NathanNeurotic/RiptOPL", polls=(0, 0)))
            runs = []
            self.assertIsNone(publisher.successor_run("NathanNeurotic/RiptOPL", polls=(0,)))

    def test_overtaken_run_publishes_nothing_and_fails_only_without_a_successor(self):
        sha, head = "a" * 40, "c" * 40
        environment = {"GITHUB_REPOSITORY": "NathanNeurotic/RiptOPL", "GITHUB_EVENT_NAME": "push",
                       "GITHUB_REF": "refs/heads/rebuild/main", "GITHUB_SHA": sha}
        for successor in ("https://example/run", None):
            with patch.object(sys, "argv", ["publish_editions.py", "dist"]), \
                    patch.dict(publisher.os.environ, environment, clear=True), \
                    patch.object(publisher, "git", return_value=sha), \
                    patch.object(publisher, "bundle_configurations"), \
                    patch.object(publisher, "verify", return_value=[]), \
                    patch.object(publisher, "fold_companions", return_value=[]), \
                    patch.object(publisher, "lite_candidates", return_value=[]), \
                    patch.object(publisher, "release_version", return_value="v1"), \
                    patch.object(publisher, "name_assets"), \
                    patch.object(publisher, "gh", return_value=json.dumps({"commit": {"sha": head}})), \
                    patch.object(publisher, "successor_run", return_value=successor), \
                    patch.object(publisher, "upload_verified") as upload:
                if successor:
                    publisher.main()
                else:
                    with self.assertRaises(ValueError):
                        publisher.main()
                upload.assert_not_called()

    def test_only_incebtion_is_designated_latest(self):
        self.assertEqual(set(publisher.EDITIONS), {"korium", "ra", "incebtion", "ra_incebtion"})
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


    def test_gh_retries_transient_failures_then_gives_up(self):
        calls = []

        def flaky(command, text):
            calls.append(command)
            if len(calls) < 3:
                raise subprocess.CalledProcessError(1, command)
            return "ok\n"

        with patch.object(publisher.subprocess, "check_output", side_effect=flaky), \
                patch.object(publisher.time, "sleep") as sleep:
            self.assertEqual(publisher.gh("release", "view", "rolling-ra"), "ok")
        self.assertEqual(len(calls), 3)
        self.assertEqual([c.args[0] for c in sleep.call_args_list], list(publisher.RETRY_DELAYS[:2]))
        with patch.object(publisher.subprocess, "check_output", side_effect=subprocess.CalledProcessError(1, "gh")), \
                patch.object(publisher.time, "sleep"), self.assertRaises(subprocess.CalledProcessError):
            publisher.gh("release", "upload", "rolling-ra", "x")

    def release(self, folder, names):
        assets = []
        for name in names:
            (folder / name).write_bytes(name.encode())
            assets.append(str(folder / name))
        return assets

    def test_existing_release_skips_unchanged_assets_and_uploads_changes_individually(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            assets = self.release(folder, ["unchanged.txt", "changed.txt", "new.zip"])
            original = b"previous-release"
            names = {
                "unchanged.txt": (folder / "unchanged.txt").read_bytes(),
                "changed.txt": original,
            }

            def remote_item(name, contents):
                return dict(name=name, size=len(contents),
                            digest="sha256:" + hashlib.sha256(contents).hexdigest())

            remote = {name: remote_item(name, contents) for name, contents in names.items()}
            uploads, downloads = [], []

            def fake(*args, retry=True):
                if args[:2] == ("release", "view"):
                    return json.dumps(dict(id=1, assets=list(remote.values())))
                if args[:2] == ("release", "download"):
                    name = args[args.index("--pattern") + 1]
                    downloads.append(name)
                    (Path(args[args.index("--dir") + 1]) / name).write_bytes(names[name])
                elif args[:2] == ("release", "upload"):
                    self.assertEqual(len(args[args.index("--clobber") + 1:]), 1)
                    file = Path(args[-1])
                    uploads.append(file.name)
                    remote[file.name] = remote_item(file.name, file.read_bytes())
                return ""

            with patch.object(publisher, "gh", side_effect=fake):
                publisher.upload_verified("owner/repo", "rolling-ra", "a" * 40,
                                          "RA", Path("notes"), assets, True)
            self.assertEqual(uploads, ["new.zip", "changed.txt"])
            self.assertEqual(downloads, ["changed.txt"])
            self.assertNotIn("unchanged.txt", uploads)

    def test_successful_remote_upload_after_http_error_is_verified_not_redeleted(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            assets = self.release(folder, ["changed.txt"])
            old = b"old"
            remote = {"changed.txt": dict(name="changed.txt", size=len(old),
                                          digest="sha256:" + hashlib.sha256(old).hexdigest())}
            uploads = []

            def fake(*args, retry=True):
                if args[:2] == ("release", "view"):
                    return json.dumps(dict(id=1, assets=list(remote.values())))
                if args[:2] == ("release", "download"):
                    (Path(args[args.index("--dir") + 1]) / "changed.txt").write_bytes(old)
                if args[:2] == ("release", "upload"):
                    file = Path(args[-1])
                    uploads.append(file.name)
                    new = file.read_bytes()
                    remote[file.name] = dict(name=file.name, size=len(new),
                                             digest="sha256:" + hashlib.sha256(new).hexdigest())
                    raise subprocess.CalledProcessError(1, "gh", stderr="HTTP 502")
                return ""

            with patch.object(publisher, "gh", side_effect=fake):
                publisher.upload_verified("owner/repo", "rolling-ra", "a" * 40,
                                          "RA", Path("notes"), assets, True)
            self.assertEqual(uploads, ["changed.txt"])  # no needless second delete

    def test_genuinely_failed_upload_retries_bounded_times_then_rolls_back(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            assets = self.release(folder, ["changed.txt"])
            old = b"old"
            remote = {"changed.txt": dict(name="changed.txt", size=len(old),
                                          digest="sha256:" + hashlib.sha256(old).hexdigest())}
            uploads, restores, sleeps = [], [], []

            def fake(*args, retry=True):
                if args[:2] == ("release", "view"):
                    return json.dumps(dict(id=1, assets=list(remote.values())))
                if args[:2] == ("release", "download"):
                    (Path(args[args.index("--dir") + 1]) / "changed.txt").write_bytes(old)
                if args[:2] == ("release", "upload"):
                    file = Path(args[-1])
                    if str(folder) in str(file):
                        uploads.append(file.name)
                        raise subprocess.CalledProcessError(1, "gh", stderr="HTTP 502")
                    restores.append(file.name)  # the rollback re-uploading its backup
                return ""

            with patch.object(publisher, "gh", side_effect=fake), \
                    patch.object(publisher.time, "sleep", side_effect=sleeps.append), \
                    self.assertRaises(subprocess.CalledProcessError):
                publisher.upload_verified("owner/repo", "rolling-ra", "a" * 40,
                                          "RA", Path("notes"), assets, True)
            # one initial attempt plus every bounded retry, each miss sleeping its delay
            self.assertEqual(uploads, ["changed.txt"] * (1 + len(publisher.RETRY_DELAYS)))
            self.assertEqual(sleeps, list(publisher.RETRY_DELAYS))
            # the caller's rollback restored the backed-up original exactly once
            self.assertEqual(restores, ["changed.txt"])

    def test_torn_backup_aborts_before_any_upload(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            assets = self.release(folder, ["stable.ELF", "new.zip"])
            old = b"old-elf"
            original = dict(name="stable.ELF", size=len(old),
                            digest="sha256:" + hashlib.sha256(old).hexdigest())
            remote = {"stable.ELF": original}
            attempts = []

            def fake(*args, retry=True):
                attempts.append(args)
                if args[:2] == ("release", "view"):
                    return json.dumps(dict(id=1, assets=list(remote.values())))
                if args[:2] == ("release", "download"):
                    # a torn backup: the download does not match the recorded original digest
                    (Path(args[args.index("--dir") + 1]) / "stable.ELF").write_bytes(b"torn")
                if args[:2] == ("release", "upload"):
                    file = Path(args[-1])
                    contents = file.read_bytes()
                    remote[file.name] = dict(name=file.name, size=len(contents),
                                             digest="sha256:" + hashlib.sha256(contents).hexdigest())
                return ""

            with patch.object(publisher, "gh", side_effect=fake),                     patch.object(publisher.time, "sleep"),                     self.assertRaisesRegex(ValueError, "aborting before any upload"):
                publisher.upload_verified("owner/repo", "rolling-ra", "a" * 40,
                                          "RA", Path("notes"), assets, True)
            # preflight abort: no upload or delete ever ran, and the release is untouched
            self.assertFalse(any(a[:2] == ("release", "upload") for a in attempts))
            self.assertFalse(any(a[:2] == ("release", "delete-asset") for a in attempts))
            self.assertEqual(remote, {"stable.ELF": original})

    def test_failed_upload_does_not_touch_unattempted_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            assets = self.release(folder, ["RIPTOPL-RA.ELF", "APP_RIPTOPL-RA.psu", "RIPTOPL-RA.zip"])
            calls = []

            def fake(*args, retry=True):
                calls.append(args)
                if args[:2] == ("release", "view"):
                    return json.dumps(dict(assets=[{"name": "RIPTOPL-RA.ELF", "size": 3,
                                                    "digest": "sha256:" + hashlib.sha256(b"old").hexdigest()},
                                                   {"name": "RIPTOPL-RA.zip", "size": 3,
                                                    "digest": "sha256:" + hashlib.sha256(b"old").hexdigest()},
                                                   {"name": "old.txt"}]))
                if args[:2] == ("release", "download"):
                    (Path(args[args.index("--dir") + 1]) / args[args.index("--pattern") + 1]).write_bytes(b"old")
                elif args[:2] == ("release", "upload") and str(folder) in args[-1]:
                    raise subprocess.CalledProcessError(1, "gh")
                return ""

            with patch.object(publisher, "gh", side_effect=fake), \
                    patch.object(publisher.time, "sleep"), \
                    self.assertRaises(subprocess.CalledProcessError):
                publisher.upload_verified("owner/repo", "rolling-ra", "a" * 40, "RA", Path("notes"), assets, True)
            # archives upload first: only the .zip is attempted, it is rolled back from
            # its verified backup, and neither unattempted asset is deleted or edited
            attempted = {Path(c[-1]).name for c in calls
                         if c[:2] == ("release", "upload") and str(folder) in str(c[-1])}
            self.assertEqual(attempted, {"RIPTOPL-RA.zip"})
            self.assertEqual(Path(calls[-1][-1]).name, "RIPTOPL-RA.zip")
            self.assertFalse(any(c[:2] == ("release", "delete-asset") for c in calls))
            self.assertFalse(any(call[:2] in (("release", "edit"), ("release", "create")) for call in calls))

    def test_rollback_continues_to_delete_new_assets_if_restore_gets_502(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            assets = self.release(folder, ["stable.ELF", "new.zip"])
            old = b"old-elf"
            old_item = dict(name="stable.ELF", size=len(old),
                            digest="sha256:" + hashlib.sha256(old).hexdigest())
            remote = {"stable.ELF": old_item}
            attempts = []

            def fake(*args, retry=True):
                attempts.append(args)
                if args[:2] == ("release", "view"):
                    return json.dumps(dict(id=1, assets=list(remote.values())))
                if args[:2] == ("release", "download"):
                    (Path(args[args.index("--dir") + 1]) / "stable.ELF").write_bytes(old)
                if args[:2] == ("release", "upload"):
                    file = Path(args[-1])
                    if file.name == "stable.ELF":
                        raise subprocess.CalledProcessError(1, "gh", stderr="HTTP 502")
                    contents = file.read_bytes()
                    remote[file.name] = dict(name=file.name, size=len(contents),
                                             digest="sha256:" + hashlib.sha256(contents).hexdigest())
                if args[:2] == ("release", "delete-asset"):
                    remote.pop(args[3], None)
                return ""

            with patch.object(publisher, "gh", side_effect=fake), \
                    patch.object(publisher.time, "sleep"), \
                    self.assertRaises(subprocess.CalledProcessError):
                publisher.upload_verified("owner/repo", "rolling-ra", "a" * 40,
                                          "RA", Path("notes"), assets, True)
            self.assertEqual(remote, {"stable.ELF": old_item})
            self.assertIn(("release", "delete-asset", "rolling-ra", "new.zip",
                           "--repo", "owner/repo", "--yes"), attempts)
            # the failed upload burned its one initial attempt plus every bounded retry,
            # and the rollback then made exactly one restore attempt
            self.assertEqual(len([args for args in attempts if args[:2] == ("release", "upload")
                                  and Path(args[-1]).name == "stable.ELF"]), 1 + len(publisher.RETRY_DELAYS) + 1)

    def test_unretained_asset_restores_and_new_drafts_need_no_restore(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            assets = self.release(folder, ["RIPTOPL-RA.ELF"])
            good = dict(name="RIPTOPL-RA.ELF", size=len(b"RIPTOPL-RA.ELF"),
                        digest="sha256:" + hashlib.sha256(b"RIPTOPL-RA.ELF").hexdigest())
            for remote, exists, restored in ((dict(good, size=1), True, True), (good, True, False),
                                             (dict(good, digest="sha256:0"), False, False)):
                calls = []

                def fake(*args, retry=True):
                    calls.append(args)
                    if args[:2] == ("release", "view"):
                        return json.dumps(dict(id=1, assets=[remote]))
                    if args[:2] == ("release", "download"):
                        # a real download returns the bytes the recorded digest belongs to
                        (Path(args[args.index("--dir") + 1]) / "RIPTOPL-RA.ELF").write_bytes(b"RIPTOPL-RA.ELF")
                    return ""

                with patch.object(publisher, "gh", side_effect=fake):
                    if remote is good:
                        publisher.upload_verified("owner/repo", "rolling-ra", "a" * 40, "RA", Path("notes"), assets, exists)
                    else:
                        with self.assertRaisesRegex(ValueError, "did not retain"):
                            publisher.upload_verified("owner/repo", "rolling-ra", "a" * 40, "RA", Path("notes"), assets, exists)
                restores = [c for c in calls if c[:2] == ("release", "upload") and str(folder) not in c[-1]]
                self.assertEqual(bool(restores), restored)
                if not exists:
                    self.assertIn("--draft", next(c for c in calls if c[:2] == ("release", "create")))

    def test_backup_without_recorded_digest_aborts_before_any_upload(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            assets = self.release(folder, ["stable.ELF", "new.zip"])
            old = b"old-elf"
            # GitHub did not report a digest for the original asset: the backup
            # cannot be verified, so the publish must fail closed before any upload.
            remote = {"stable.ELF": dict(name="stable.ELF", size=len(old))}
            attempts = []

            def fake(*args, retry=True):
                attempts.append(args)
                if args[:2] == ("release", "view"):
                    return json.dumps(dict(id=1, assets=list(remote.values())))
                if args[:2] == ("release", "download"):
                    (Path(args[args.index("--dir") + 1]) / "stable.ELF").write_bytes(old)
                if args[:2] == ("release", "upload"):
                    file = Path(args[-1])
                    contents = file.read_bytes()
                    remote[file.name] = dict(name=file.name, size=len(contents),
                                             digest="sha256:" + hashlib.sha256(contents).hexdigest())
                return ""

            with patch.object(publisher, "gh", side_effect=fake),                     patch.object(publisher.time, "sleep"),                     self.assertRaisesRegex(ValueError, "aborting before any upload"):
                publisher.upload_verified("owner/repo", "rolling-ra", "a" * 40,
                                          "RA", Path("notes"), assets, True)
            self.assertFalse(any(a[:2] == ("release", "upload") for a in attempts))
            self.assertFalse(any(a[:2] == ("release", "delete-asset") for a in attempts))
            self.assertEqual(remote, {"stable.ELF": remote["stable.ELF"]})

    def candidates(self, root, versions):
        found = []
        for edition, version in versions.items():
            path = root / edition
            path.mkdir()
            (path / "BUILD-MANIFEST.json").write_text(json.dumps({"version": f'#define OPL_VERSION "{version}"\n'}))
            found.append((edition, publisher.EDITIONS[edition], path, {}))
        return found

    def test_release_version_drops_the_hash_and_build_label(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            found = self.candidates(root, {"korium": "v1.2.0-Beta-3925-151fc05-PS2BUILD-Korium-GCC",
                                           "incebtion": "v1.2.0-Beta-3925-151fc05-PS2BUILD-IncebtIon-GCC"})
            self.assertEqual(publisher.release_version(found), "v1.2.0-Beta-3925")
        with tempfile.TemporaryDirectory() as temporary:  # a v* tag names the build
            self.assertEqual(publisher.release_version(self.candidates(Path(temporary), {"ra": "v1.3.0-PS2BUILD-RetroAchievements-GCC"})), "v1.3.0")
        with tempfile.TemporaryDirectory() as temporary, self.assertRaisesRegex(ValueError, "different versions"):
            publisher.release_version(self.candidates(Path(temporary), {"korium": "v1.2.0-Beta-3925-151fc05-PS2BUILD-Korium-GCC",
                                                                         "ra": "v1.2.0-Beta-3926-1234567-PS2BUILD-RetroAchievements-GCC"}))

    def test_every_archive_name_ends_with_the_version(self):
        version = "v1.2.0-Beta-3925"
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            names = ["RIPTOPL.ELF", "APP_RIPTOPL.psu", "BUILD-MANIFEST.json", "RIPTOPL-src.zip", "RIPTOPL-LANGS.zip",
                     "RIPTOPL-Korium.zip", "RIPTOPL-VARIANTS.zip", "RIPTOPL-DEBUG.zip", "RIPTOPL-BUGREPORT.zip"]
            sums = "".join(f"{hashlib.sha256(n.encode()).hexdigest()}  {n}\n" for n in names)
            for name in names:
                (path / name).write_bytes(name.encode())
            (path / "SHA256SUMS.txt").write_text(sums)
            publisher.name_assets([("korium", publisher.EDITIONS["korium"], path, {})], version)
            expected = {"RIPTOPL.ELF", "APP_RIPTOPL.psu", "BUILD-MANIFEST.json", "SHA256SUMS.txt",
                        *(f"RIPTOPL-{k}-{version}.zip" for k in ("src", "LANGS", "Korium", "VARIANTS", "DEBUG", "BUGREPORT"))}
            self.assertEqual({p.name for p in path.iterdir()}, expected)
            for line in (path / "SHA256SUMS.txt").read_text().splitlines():
                digest, name = line.split("  ", 1)
                self.assertEqual(hashlib.sha256((path / name).read_bytes()).hexdigest(), digest)
        self.assertEqual(publisher.versioned("RIPTOPL-RetroAchievements-IncebtIon.zip", version),
                         "RIPTOPL-RetroAchievements-IncebtIon-v1.2.0-Beta-3925.zip")
        self.assertEqual(publisher.versioned("RIPTOPL-RA-IncebtIon.ELF", version), "RIPTOPL-RA-IncebtIon.ELF")

    def test_previous_builds_archives_are_removed_after_publication(self):
        calls = []

        def fake(*args, retry=True):
            calls.append(args)
            if args[:2] == ("release", "view"):
                return json.dumps(dict(assets=[{"name": n} for n in (
                    "RIPTOPL.ELF", "RIPTOPL-Korium.zip", "RIPTOPL-Korium-v1.2.0-Beta-3924.zip",
                    "RIPTOPL-Korium-v1.2.0-Beta-3925.zip", "notes.txt")]))
            return ""

        with patch.object(publisher, "gh", side_effect=fake):
            publisher.remove_stale_archives("owner/repo", "rolling-korium", ["RIPTOPL.ELF", "RIPTOPL-Korium-v1.2.0-Beta-3925.zip"])
        deleted = [c[3] for c in calls if c[:2] == ("release", "delete-asset")]
        self.assertEqual(deleted, ["RIPTOPL-Korium-v1.2.0-Beta-3924.zip", "RIPTOPL-Korium.zip"])

    def refresh(self, fail=None, stays_draft=False, moves=True):
        calls = self.calls = []
        state = dict(isDraft=False, publishedAt="2026-10-16T23:47:34Z")

        def fake(*args, retry=True):
            calls.append(args)
            if args[:2] == ("release", "view"):
                return json.dumps(state)
            if args[:2] == ("release", "edit"):
                if fail and fail in args:
                    raise subprocess.CalledProcessError(1, "gh")
                if "--draft=true" in args:
                    state["isDraft"] = True
                elif state["isDraft"] and not stays_draft:
                    state["isDraft"] = False
                    if moves:
                        state["publishedAt"] = "2026-10-30T06:00:00Z"
            return ""

        with patch.object(publisher, "gh", side_effect=fake), patch.object(publisher.time, "sleep"):
            publisher.refresh_release_date("owner/repo", "rolling-incebtion", ("--prerelease=false", "--latest=true"))
        return [c for c in calls if c[:2] == ("release", "edit")], state

    def test_republished_channel_gets_a_new_date_and_keeps_its_flags(self):
        edits, state = self.refresh()
        self.assertEqual(edits, [("release", "edit", "rolling-incebtion", "--repo", "owner/repo", "--draft=true"),
                                 ("release", "edit", "rolling-incebtion", "--repo", "owner/repo", "--draft=false",
                                  "--prerelease=false", "--latest=true")])
        self.assertEqual(state, dict(isDraft=False, publishedAt="2026-10-30T06:00:00Z"))
        # GitHub ignoring the flip is reported, not fatal: the channel is still published.
        self.assertFalse(self.refresh(moves=False)[1]["isDraft"])

    def test_date_refresh_never_leaves_a_channel_hidden(self):
        # A failed draft call costs only the date: the channel is republished and checked, and the
        # run goes on to the other channels.
        edits, state = self.refresh(fail="--draft=true")
        self.assertIn("--draft=false", edits[-1])
        self.assertFalse(state["isDraft"])
        # A republish that keeps failing gets one more try a minute later, then fails the run.
        with self.assertRaises(subprocess.CalledProcessError):
            self.refresh(fail="--draft=false")
        republishes = [c for c in self.calls if "--draft=false" in c]
        self.assertEqual(len(republishes), 2)
        with self.assertRaisesRegex(RuntimeError, "still a draft"):
            self.refresh(stays_draft=True)

    def test_only_existing_channels_are_flipped_and_before_the_tag_check(self):
        source = (ROOT / "tools" / "publish_editions.py").read_text(encoding="utf-8")
        main = source[source.index("def main():"):]
        self.assertIn("if exists:  # a first publication is already dated now\n                refresh_release_date(repo, tag, flags)", main)
        self.assertLess(main.index("refresh_release_date("), main.index('reference = json.loads('))

    def test_ra_kit_ships_the_mbedtls_licence_and_ordinary_kits_refuse_it(self):
        package = (ROOT / "tools/package_edition.py").read_text(encoding="utf-8")
        app = (ROOT / ".github/scripts/package_release_app.py").read_text(encoding="utf-8")
        self.assertIn('shutil.copyfile(ROOT / "third_party/mbedtls/LICENSE", kit / "LICENSE-MBEDTLS.txt")', package)
        self.assertIn('required.add("LICENSE-MBEDTLS.txt")', app)
        self.assertIn('elif "LICENSE-MBEDTLS.txt" in all_names:', app)

    def test_every_kit_bundle_and_standalone_download_ships_the_usmb2_licence(self):
        # Every RiptOPL ELF embeds smb2man over usmb2 (MIT), so the licence text must travel
        # with the install kits, the VARIANTS/DEBUG bundles and the standalone ELF download.
        licence = ROOT / "modules/network/smb2man/usmb2/LICENSE"
        self.assertIn("Permission is hereby granted", licence.read_text(encoding="utf-8"))
        package = (ROOT / "tools/package_edition.py").read_text(encoding="utf-8")
        self.assertIn('USMB2_LICENCE = ROOT / "modules/network/smb2man/usmb2/LICENSE"', package)
        self.assertIn('shutil.copyfile(USMB2_LICENCE, kit / "LICENSE-USMB2.txt")', package)
        self.assertIn('shutil.copyfile(USMB2_LICENCE, out / "LICENSE-USMB2.txt")', package)
        self.assertLess(package.index('if options["ra"]:  # the RA ELF links mbedTLS'), package.index('kit / "LICENSE-USMB2.txt"'))
        publish = (ROOT / "tools/publish_editions.py").read_text(encoding="utf-8")
        self.assertIn('bundle.writestr("LICENSE-USMB2.txt", (path / "LICENSE-USMB2.txt").read_bytes())', publish)
        self.assertIn('"LICENSE-USMB2.txt"}', publish)
        self.assertIn('kit.read("LICENSE-USMB2.txt") != (path / "LICENSE-USMB2.txt").read_bytes()', publish)
        app = (ROOT / ".github/scripts/package_release_app.py").read_text(encoding="utf-8")
        self.assertIn('"LICENSE-USMB2.txt",  # every loader embeds usmb2 (MIT)', app)

    def test_credits_say_every_loader_uses_usmb2_under_the_mit_licence(self):
        # CREDITS is the copyright notice that travels with the licence: it names usmb2, says every
        # loader embeds it, that it is used under the MIT licence and where that licence text is.
        credits = (ROOT / "CREDITS").read_text(encoding="utf-8")
        entry = credits[credits.index("  - usmb2, by Ronnie Sahlberg"):]
        entry = " ".join(entry[:entry.index("\n\n")].split())
        for phrase in ("https://github.com/sahlberg/usmb2", "every loader embeds", "used under the MIT licence",
                       "LICENSE-USMB2.txt"):
            self.assertIn(phrase, entry)

    def assert_copied_before_upload(self, workflow, artifact, copies, listed):
        # The copies run in the job that uploads the artifact, before the upload and whenever it runs: no
        # condition but success() and no continue-on-error, so a later edit cannot quietly drop the licence.
        import yaml

        allowed = ("${{ success() }}", "success()")
        jobs = yaml.safe_load((ROOT / ".github/workflows" / workflow).read_text(encoding="utf-8"))["jobs"]
        uploads = [(job["steps"], index) for job in jobs.values() for index, step in enumerate(job.get("steps", []))
                   if "actions/upload-artifact@" in step.get("uses", "") and step.get("with", {}).get("name") == artifact]
        self.assertEqual(len(uploads), 1, artifact)
        steps, upload = uploads[0]
        self.assertEqual(steps[upload]["with"]["path"].split(), listed, artifact)
        self.assertIn(steps[upload].get("if", allowed[0]), allowed, artifact)
        for copy in copies:
            found = [step for step in steps[:upload]
                     if copy in [line.strip() for line in str(step.get("run", "")).splitlines()]]
            self.assertEqual(len(found), 1, (artifact, copy))
            self.assertIn(found[0].get("if", allowed[0]), allowed, (artifact, copy))
            self.assertNotIn("continue-on-error", found[0], (artifact, copy))

    def test_every_actions_test_build_carries_the_usmb2_licence(self):
        # Testers also download loaders straight from Actions runs, so those artifacts carry the licence and
        # CREDITS, which says the loader uses usmb2 under it.
        usmb2 = ROOT / "modules/network/smb2man/usmb2"
        for filename in ("LICENSE", "VENDORED.txt"):
            self.assertTrue((usmb2 / filename).is_file(), filename)
        self.assertTrue((ROOT / "CREDITS").is_file())
        build = (ROOT / "tools/build_edition.py").read_text(encoding="utf-8")
        staging = build[build.index('if args.configuration != "standard":'):build.index("print(f\"Built ")]
        self.assertIn('for filename in ("LICENSE", "CREDITS"):', staging)
        self.assertIn('destination = stage / "licenses" / "usmb2"', staging)
        self.assertIn('for filename in ("LICENSE", "VENDORED.txt"):', staging)
        self.assertIn('shutil.copyfile(ROOT / "modules/network/smb2man/usmb2" / filename, destination / filename)', staging)
        self.assert_copied_before_upload(
            "ps2build.yml", "riptopl-ps2build-${{ matrix.toolchain }}",
            ["cp -f modules/network/smb2man/usmb2/LICENSE build/bin/LICENSE-USMB2.txt",
             "cp -f CREDITS build/bin/CREDITS"],
            ["build/bin/riptopl-packed.elf", "build/bin/LICENSE-USMB2.txt", "build/bin/CREDITS"])
        self.assert_copied_before_upload(
            "flavours.yml", "OPL-PS2BUILD",
            ["cp -f modules/network/smb2man/usmb2/LICENSE LICENSE-USMB2.txt"],
            ["build/bin/riptopl-packed.elf", "BUILD-MANIFEST.txt", "LICENSE-USMB2.txt", "CREDITS"])
        # The local `tools/assets.py release` zip is a loader download too.
        assets = (ROOT / "tools/assets.py").read_text(encoding="utf-8")
        release = assets[assets.index("def release():"):assets.index("COMMANDS = {")]
        self.assertIn('licence = os.path.join(directory, "LICENSE-USMB2.txt")', release)
        self.assertIn('"modules", "network", "smb2man", "usmb2", "LICENSE"', release)
        self.assertIn('run("zip", "-j", package, licence)', release)

    def test_local_release_zip_takes_whichever_changelog_make_changelog_leaves(self):
        # In a full clone make_changelog.sh writes DETAILED_CHANGELOG and deletes its header file; in a
        # git worktree, a source archive or a shallow clone it leaves only the header file. The zip must
        # be built in both cases, never from a DETAILED_CHANGELOG an earlier run left behind.
        spec = importlib.util.spec_from_file_location("riptopl_assets", ROOT / "tools/assets.py")
        cwd = os.getcwd()
        assets = importlib.util.module_from_spec(spec)
        try:
            spec.loader.exec_module(assets)  # chdirs to the repository root
        finally:
            os.chdir(cwd)

        def release(outcome):
            with tempfile.TemporaryDirectory() as directory:
                tree = Path(directory)
                (tree / "build/bin").mkdir(parents=True)
                (tree / "build/bin/riptopl-packed.elf").write_bytes(b"\x7fELF")
                usmb2 = tree / "modules/network/smb2man/usmb2"
                usmb2.mkdir(parents=True)
                (usmb2 / "LICENSE").write_text("MIT text", encoding="utf-8")
                (tree / "DETAILED_CHANGELOG").write_text("stale", encoding="utf-8")
                header = tree / "header"
                zipped = {}

                def run(*cmd, **kw):
                    if cmd == ("sh", "tools/make_changelog.sh"):
                        if outcome == "full":
                            (tree / "DETAILED_CHANGELOG").write_text("header and history", encoding="utf-8")
                        elif outcome == "header":
                            header.write_text("header", encoding="utf-8")
                        return
                    self.assertEqual(cmd[:2], ("zip", "-r") if not zipped else ("zip", "-j"), cmd)
                    for name in cmd[3:]:
                        path = tree / name
                        zipped[path.name] = path.read_text(encoding="utf-8") if name.endswith(("CHANGELOG", ".txt")) else ""

                os.chdir(tree)
                try:
                    with patch.object(assets, "run", run), patch.object(assets, "opl_version", return_value="v0"), \
                            patch.object(assets, "CHANGELOG_HEADER", str(header)):
                        assets.release()
                finally:
                    os.chdir(cwd)
                return zipped

        for outcome, changelog in (("full", "header and history"), ("header", "header")):
            zipped = release(outcome)
            self.assertEqual(zipped["DETAILED_CHANGELOG"], changelog, outcome)
            self.assertEqual(zipped["LICENSE-USMB2.txt"], "MIT text", outcome)
            self.assertEqual(set(zipped), {"RIPTOPL-v0.ELF", "DETAILED_CHANGELOG", "CREDITS", "LICENSE", "README.md",
                                           "LICENSE-USMB2.txt"}, outcome)
        with self.assertRaisesRegex(SystemExit, "wrote no DETAILED_CHANGELOG"):
            release("nothing")


if __name__ == "__main__":
    unittest.main()
