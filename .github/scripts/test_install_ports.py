"""Port setup must select the requested compiler and preserve a good prior install."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, patch
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("install_ports", ROOT / "tools/install_ports.py")
ports = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ports)


def release(*names):
    return {"tag_name": "v1", "assets": [{"name": name, "browser_download_url": "https://example.com/" + name}
                                         for name in names]}


def archive(extra=None):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w") as package:
        package.writestr("package.yaml", "name: ogg\n")
        package.writestr("lib/libogg.a", b"new archive")
        package.writestr("LICENSE", "upstream license")
        if extra:
            package.writestr(extra, "unexpected")
    return stream.getvalue()


class InstallPortsTests(unittest.TestCase):
    def test_incomplete_http_body_is_discarded_and_retried(self):
        import http.client
        partial, complete = MagicMock(), MagicMock()
        partial.__enter__.return_value = partial
        complete.__enter__.return_value = complete
        partial.read.side_effect = http.client.IncompleteRead(b"partial archive", 121192)
        complete.read.return_value = b"complete archive"
        with patch.object(ports.urllib.request, "urlopen", side_effect=[partial, complete]) as opened, patch.object(ports.time, "sleep") as sleep:
            self.assertEqual(ports.download("https://example.com/package.zip"), b"complete archive")
        self.assertEqual(opened.call_count, 2)
        partial.__exit__.assert_called_once()
        complete.__exit__.assert_called_once()
        sleep.assert_called_once_with(1)

    def test_persistent_incomplete_http_body_stops_after_five_attempts(self):
        import http.client
        response = MagicMock()
        response.__enter__.return_value = response
        response.read.side_effect = http.client.IncompleteRead(b"partial", 100)
        with patch.object(ports.urllib.request, "urlopen", return_value=response) as opened, patch.object(ports.time, "sleep") as sleep:
            with self.assertRaises(http.client.IncompleteRead):
                ports.download("https://example.com/package.zip")
        self.assertEqual(opened.call_count, 5)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [1, 2, 4, 8])

    def test_timeout_during_response_read_retries_from_a_new_request(self):
        response = MagicMock()
        response.__enter__.return_value = response
        response.read.side_effect = [TimeoutError("partial response"), b"complete archive"]
        with patch.object(ports.urllib.request, "urlopen", return_value=response) as opened, patch.object(ports.time, "sleep") as sleep:
            self.assertEqual(ports.download("https://example.com/package.zip"), b"complete archive")
        self.assertEqual(opened.call_count, 2)
        sleep.assert_called_once_with(1)

    def test_transient_http_errors_retry_but_missing_release_does_not(self):
        response = MagicMock()
        response.__enter__.return_value = response
        response.read.return_value = b"metadata"
        for status in (429, 503, 521, 404):
            error = ports.urllib.error.HTTPError("https://example.com/release", status, "failed", None, None)
            with patch.object(ports.urllib.request, "urlopen", side_effect=[error, response]) as opened, patch.object(ports.time, "sleep") as sleep:
                if status == 404:
                    with self.assertRaises(ports.urllib.error.HTTPError):
                        ports.download("https://example.com/release")
                    self.assertEqual(opened.call_count, 1)
                    sleep.assert_not_called()
                else:
                    self.assertEqual(ports.download("https://example.com/release"), b"metadata")
                    self.assertEqual(opened.call_count, 2)
                    sleep.assert_called_once_with(1)

    def test_persistent_timeout_stops_after_five_attempts(self):
        with patch.object(ports.urllib.request, "urlopen", side_effect=TimeoutError("offline")) as opened, patch.object(ports.time, "sleep") as sleep:
            with self.assertRaises(TimeoutError):
                ports.download("https://example.com/release")
        self.assertEqual(opened.call_count, 5)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [1, 2, 4, 8])

    def test_selection_does_not_depend_on_release_asset_order(self):
        metadata = release("ogg-v1-llvm.zip", "ogg-v1-gcc.zip")
        self.assertTrue(ports.select_asset(metadata, "ogg", "gcc").endswith("-gcc.zip"))
        self.assertTrue(ports.select_asset(metadata, "ogg", "llvm").endswith("-llvm.zip"))

    def test_legacy_archive_is_only_accepted_for_gcc(self):
        metadata = release("ogg-v1.zip")
        self.assertTrue(ports.select_asset(metadata, "ogg", "gcc").endswith("ogg-v1.zip"))
        with self.assertRaisesRegex(ValueError, "no unique llvm"):
            ports.select_asset(metadata, "ogg", "llvm")

    def test_missing_and_duplicate_compiler_archives_are_rejected(self):
        for metadata in (release("ogg-v1-gcc.zip"), release("ogg-v1-llvm.zip", "ogg-v1-llvm.zip")):
            with self.assertRaises(ValueError):
                ports.select_asset(metadata, "ogg", "llvm")

    def test_unsafe_archive_does_not_replace_existing_package(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            target = sdk / "packages-llvm/world/ogg"
            target.mkdir(parents=True)
            (target / "previous.txt").write_text("keep")
            metadata = json.dumps(release("ogg-v1-llvm.zip")).encode()
            with patch.object(ports, "download", side_effect=[metadata, archive("../escape.txt")]):
                with self.assertRaises(ValueError):
                    ports.install(sdk, "ogg", "llvm")
            self.assertEqual((target / "previous.txt").read_text(), "keep")
            self.assertFalse((sdk / "packages-llvm/world/escape.txt").exists())

    def test_llvm_install_preserves_gcc_package_and_license(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            gcc = sdk / "packages/world/ogg"
            gcc.mkdir(parents=True)
            (gcc / "previous.txt").write_text("gcc")
            metadata = json.dumps(release("ogg-v1-llvm.zip")).encode()
            with patch.object(ports, "download", side_effect=[metadata, archive()]):
                ports.install(sdk, "ogg", "llvm")
            self.assertEqual((gcc / "previous.txt").read_text(), "gcc")
            self.assertEqual((sdk / "packages-llvm/world/ogg/LICENSE").read_text(), "upstream license")
            self.assertEqual((sdk / "packages-llvm/world/ogg/lib/libogg.a").read_bytes(), b"new archive")

    def test_host_outage_keeps_a_complete_cached_package_only(self):
        outage = ports.urllib.error.HTTPError("https://example.com/release", 521, "origin down", None, None)
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            cached = sdk / "packages/world/ogg"
            with patch.object(ports, "download", side_effect=outage):
                with self.assertRaises(ports.urllib.error.HTTPError):
                    ports.install(sdk, "ogg", "gcc")  # nothing cached
                (cached / "lib").mkdir(parents=True)
                (cached / "package.yaml").write_text("name: ogg\n")
                with self.assertRaises(ports.urllib.error.HTTPError):
                    ports.install(sdk, "ogg", "gcc")  # cached package lacks its library
                (cached / "lib/libogg.a").write_bytes(b"cached")
                ports.install(sdk, "ogg", "gcc")
                self.assertEqual((cached / "lib/libogg.a").read_bytes(), b"cached")
                with patch.dict(ports.os.environ, {"OGG_VERSION": "v2"}), self.assertRaises(ports.urllib.error.HTTPError):
                    ports.install(sdk, "ogg", "gcc")  # a pinned version is never substituted
                missing = ports.urllib.error.HTTPError("https://example.com/release", 404, "missing", None, None)
                with patch.object(ports, "download", side_effect=missing), self.assertRaises(ports.urllib.error.HTTPError):
                    ports.install(sdk, "ogg", "gcc")  # a missing release is an error, not an outage

    def test_failed_replacement_restores_previous_package(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            target = sdk / "packages-llvm/world/ogg"
            target.mkdir(parents=True)
            (target / "previous.txt").write_text("keep")
            metadata = json.dumps(release("ogg-v1-llvm.zip")).encode()
            rename = Path.rename

            def fail_install(path, destination):
                if path.name == "new":
                    raise OSError("simulated replacement failure")
                return rename(path, destination)

            with patch.object(ports, "download", side_effect=[metadata, archive()]), patch.object(Path, "rename", fail_install):
                with self.assertRaisesRegex(OSError, "simulated replacement"):
                    ports.install(sdk, "ogg", "llvm")
            self.assertEqual((target / "previous.txt").read_text(), "keep")

    def test_failed_rollback_keeps_recoverable_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            target = sdk / "packages-llvm/world/ogg"
            target.mkdir(parents=True)
            (target / "previous.txt").write_text("keep")
            metadata = json.dumps(release("ogg-v1-llvm.zip")).encode()
            rename = Path.rename

            def fail_install_and_restore(path, destination):
                if path.name in ("new", "previous"):
                    raise OSError("simulated locked destination")
                return rename(path, destination)

            with patch.object(ports, "download", side_effect=[metadata, archive()]), patch.object(Path, "rename", fail_install_and_restore):
                with self.assertRaises(OSError):
                    ports.install(sdk, "ogg", "llvm")
            backups = list(target.parent.glob(".ogg-*/previous/previous.txt"))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_text(), "keep")

    def mirror_env(self, sums, **extra):
        import hashlib
        return {"OGG_VERSION": "v1", "PS2BUILD_SDK_TAG": "sdk-v1-abcd", "PS2BUILD_SDK_MIRROR_URL": "https://mirror.test",
                "PS2BUILD_SDK_SUMS_SHA256": hashlib.sha256(sums).hexdigest(), **extra}

    def test_pinned_package_comes_from_the_checksummed_mirror(self):
        import hashlib
        data = archive()
        sums = f"{hashlib.sha256(data).hexdigest()}  ogg-v1-gcc.zip\n".encode()
        with tempfile.TemporaryDirectory() as directory, patch.dict(ports.os.environ, self.mirror_env(sums)):
            sdk = Path(directory)
            with patch.object(ports, "download", side_effect=[sums, data]) as fetched:
                ports.install(sdk, "ogg", "gcc")
            self.assertEqual([c.args[0] for c in fetched.call_args_list],
                             ["https://mirror.test/SHA256SUMS", "https://mirror.test/ogg-v1-gcc.zip"])
            self.assertEqual((sdk / "packages/world/ogg/.release").read_text(), "v1\n")
            with patch.object(ports, "download") as fetched:
                ports.install(sdk, "ogg", "gcc")  # the cached pinned package needs no download
            fetched.assert_not_called()

    def test_mirror_that_fails_verification_falls_back_to_the_official_host(self):
        import hashlib
        data = archive()
        sums = f"{hashlib.sha256(b'other').hexdigest()}  ogg-v1-gcc.zip\n".encode()
        metadata = json.dumps(release("ogg-v1-gcc.zip")).encode()
        for env in (self.mirror_env(sums), self.mirror_env(b"forged", PS2BUILD_SDK_SUMS_SHA256="0" * 64)):
            with tempfile.TemporaryDirectory() as directory, patch.dict(ports.os.environ, env):
                sdk = Path(directory)
                responses = [sums, data, metadata, data] if env["PS2BUILD_SDK_SUMS_SHA256"] != "0" * 64 else [b"forged", metadata, data]
                with patch.object(ports, "download", side_effect=responses) as fetched:
                    ports.install(sdk, "ogg", "gcc")
                self.assertTrue(fetched.call_args_list[-2].args[0].startswith(ports.HOST))
                self.assertEqual((sdk / "packages/world/ogg/lib/libogg.a").read_bytes(), b"new archive")


if __name__ == "__main__":
    unittest.main()
