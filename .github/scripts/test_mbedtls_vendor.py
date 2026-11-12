"""mbedTLS is vendored from its 3.6.7 release archive under Apache-2.0, for RA builds and Check for Updates.

The RA edition looks games up on retroachievements.org over HTTPS (ORBIT parity). TLS comes from
mbedTLS, taken under Apache-2.0 so RiptOPL keeps no GPL code. Only RA builds may compile or link it,
and only five pinned roots are trusted. The expected root fingerprints are written here, checked
against the Mozilla root store when vendored, so a wrong DER can't pass with a matching README. Ported from the original with this refresh; the
Makefile check became a check of tools/build_edition.py's edition definitions and tools/assets.py.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MBED = ROOT / "third_party/mbedtls"
ROOTS = ROOT / "third_party/ca-roots"

# Issuer roots (Google Trust Services, ISRG, Sectigo), SHA-256 of the DER.
PINNED = {
    "sectigo-e46.der": "c90f26f0fb1b4018b22227519b5ca2b53e2ca5b3be5cf18efe1bef47380c5383",
    "sectigo-r46.der": "7bb647a62aeeac88bf257aa522d01ffea395e0ab45c73f93f65654ec38f25a06",
    "gtsr1.der": "d947432abde7b7fa90fc2e6b59101b1280e0e1c7e4e40fa3c6887fff57a7f4cf",
    "gtsr3.der": "34d8a73ee208d9bcdb0d956520934b4e40e69482596e8b6f73c8426b010a6f48",
    "gtsr4.der": "349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d",
    "isrgrootx1.der": "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6",
    "isrg-root-x2.der": "69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470",
}

readme = (MBED / "README.md").read_text(encoding="utf-8") if (MBED / "README.md").exists() else None
assert readme is not None, "third_party/mbedtls/README.md missing"
assert "3.6.7" in readme and re.search(r"\b[0-9a-f]{64}\b", readme), "README must name 3.6.7 and the archive SHA-256"
assert "Used under the Apache-2.0 licence." in readme
assert (MBED / "LICENSE").is_file()
# Generated sources only the release archive carries (not the git tag).
assert (MBED / "library/psa_crypto_driver_wrappers_no_static.c").is_file()
assert (MBED / "library/error.c").is_file()
for source in sorted((MBED / "library").glob("*.c")):
    head = source.read_text(encoding="utf-8", errors="replace")[:2000]
    assert re.search(r"SPDX-License-Identifier:.*Apache-2\.0", head), f"{source.name}: no Apache-2.0 SPDX line"

# The RA editions compile mbedTLS with their configuration, as before. Of the others, only a build with an
# update channel (Check for Updates: a standard build, tools/build_edition.py update_channel) compiles it,
# with the slim TLS 1.2 configuration; every other build names neither mbedTLS nor the TLS client.
sys.path.insert(0, str(ROOT / "tools"))
import build_edition  # noqa: E402

pattern = re.compile(r"mbedtls|ratls|selfupdate|RIPTOPL_UPDATE", re.I)
SLIM = r'MBEDTLS_CONFIG_FILE=\"riptopl_mbedtls_slim_config.h\"'
for edition in build_edition.EDITIONS:
    for configuration, toolchain in [(c, "gcc") for c in build_edition.CONFIGURATIONS] + [("standard", "llvm")]:
        try:
            config = build_edition.definition(edition, configuration, toolchain)
        except ValueError:
            continue  # a combination build_configuration.py refuses
        text = json.dumps(config)
        channel = build_edition.update_channel(edition, configuration, toolchain)
        main = {t["name"]: t for t in config["targets"]}["riptopl"]
        if channel is not None:
            assert {"src/platform/selfupdate.c", "src/ui/selfupdate.cpp"} <= set(main["sources"]), edition
            assert "RIPTOPL_UPDATER" in main["defines"], edition
            assert set(build_edition.update_defines(channel)) <= set(main["defines"]), edition
            # No quoted strings on the main target (test_release_editions.py compiles with its defines).
            assert not any('"' in d for d in main["defines"]), edition
            cpp = (ROOT / "src/ui/selfupdate.cpp").read_text(encoding="utf-8")
            for define, value in zip(build_edition.update_defines(channel), channel):
                assert re.search(rf"defined\({define}\)\n#define RIPTOPL_UPDATE_(TAG|ELF) \"{re.escape(value)}\"", cpp), define
        else:
            assert "RIPTOPL_UPDATER" not in main["defines"] and "selfupdate" not in text, f"{edition}/{configuration}"
        if build_edition.EDITIONS[edition]["ra"]:
            targets = {t["name"]: t for t in config["targets"]}
            assert targets["mbedtls"]["type"] == "ee_lib"
            assert targets["mbedtls"]["sources"] == ["third_party/mbedtls/library/*.c"]
            main = targets["riptopl"]
            assert "mbedtls" in main["libs"]
            assert {"src/platform/ratls.c", "src/sources/ratls_ps2.cpp"} <= set(main["sources"])
            assert targets["mbedtls"]["defines"] == [r'MBEDTLS_CONFIG_FILE=\"riptopl_mbedtls_config.h\"']
            assert {"third_party/mbedtls/include", "third_party/mbedtls"} <= set(main["include_dirs"])
            ratls_h = (ROOT / "include/ratls.h").read_text()
            assert '#define MBEDTLS_CONFIG_FILE "riptopl_mbedtls_config.h"' in ratls_h
            assert "#if defined(RIPTOPL_UPDATER) && !defined(RETROACHIEVEMENTS)" in ratls_h
            assert "RETROACHIEVEMENTS" in main["defines"]
            assert SLIM not in text, f"{edition}/{configuration}: the RA TLS configuration must not change"
        elif channel is not None:
            targets = {t["name"]: t for t in config["targets"]}
            assert targets["mbedtls"]["sources"] == ["third_party/mbedtls/library/*.c"]
            assert targets["mbedtls"]["defines"] == [SLIM], edition
            assert "mbedtls" in main["libs"]
            assert {"src/platform/ratls.c", "src/sources/ratls_ps2.cpp"} <= set(main["sources"])
            assert "RETROACHIEVEMENTS" not in main["defines"]
        else:
            assert not pattern.search(text), f"{edition}/{configuration} names mbedTLS, ratls or the updater"
# The channels: each build updates from the release that publishes it, and nothing else does.
assert build_edition.update_channel("korium") == ("rolling-korium", "RIPTOPL.ELF")
assert build_edition.update_channel("incebtion") == ("rolling-incebtion", "RIPTOPL.ELF")
assert build_edition.update_channel("ra") == ("rolling-ra", "RIPTOPL-RA.ELF")
assert build_edition.update_channel("ra_incebtion") == ("rolling-ra", "RIPTOPL-RA-IncebtIon.ELF")
assert build_edition.update_channel("korium", toolchain="llvm") == ("rolling-llvm", "RIPTOPL.ELF")
assert build_edition.update_channel("ra", toolchain="llvm") == ("rolling-llvm", "RIPTOPL-RA.ELF")
assert build_edition.update_channel("incebtion", toolchain="llvm") is None
for configuration in build_edition.CONFIGURATIONS:
    if configuration != "standard":
        assert build_edition.update_channel("korium", configuration) is None, configuration
for lite in build_edition.LITES:
    assert build_edition.update_channel(lite) is None
# The roots are embedded by tools/assets.py for RA alone.
assets = (ROOT / "tools/assets.py").read_text(encoding="utf-8")
ra_branch = assets[assets.index('    if os.environ.get("RIPTOPL_RA") == "1":'):]
ra_branch = ra_branch[:ra_branch.index("\n    for a in pngs:")]
assert "third_party/ca-roots" in ra_branch and assets.count("ca-roots") == 1
assert '"ratls_root_" + der.stem.replace("-", "")' in ra_branch

# Seven pinned roots, nothing else. GTS Root R2 is gone: Mozilla's store dropped it (certifi
# 2026.10.18), so it is not trusted here. A build without RA embeds only GitHub's four.
import assets  # noqa: E402

assert sorted(assets.UPDATER_ROOTS) == ["isrg-root-x2", "isrgrootx1", "sectigo-e46", "sectigo-r46"]
assert sorted(p.name for p in ROOTS.glob("*.der")) == sorted(PINNED)
roots_readme = (ROOTS / "README.md").read_text(encoding="utf-8")
for name, expected in PINNED.items():
    assert hashlib.sha256((ROOTS / name).read_bytes()).hexdigest() == expected, name
    assert expected in roots_readme.replace(":", "").lower(), f"{name}: README lists another fingerprint"

config = (MBED / "riptopl_mbedtls_config.h").read_text(encoding="utf-8")
for wanted in ("MBEDTLS_SSL_PROTO_TLS1_3", "MBEDTLS_SSL_PROTO_TLS1_2", "MBEDTLS_ENTROPY_HARDWARE_ALT",
               "MBEDTLS_PLATFORM_TIME_ALT", "MBEDTLS_SSL_CLI_C", "MBEDTLS_X509_CRT_PARSE_C"):
    assert re.search(rf"^#define {wanted}\b", config, re.M), wanted
for unwanted in ("MBEDTLS_SSL_SRV_C", "MBEDTLS_FS_IO", "MBEDTLS_SSL_RENEGOTIATION", "MBEDTLS_NET_C",
                 "MBEDTLS_TIMING_C", "MBEDTLS_DEBUG_C"):
    assert not re.search(rf"^#define {unwanted}\b", config, re.M), unwanted

slim = (MBED / "riptopl_mbedtls_slim_config.h").read_text(encoding="utf-8")
for wanted in ("MBEDTLS_SSL_PROTO_TLS1_2", "MBEDTLS_ENTROPY_HARDWARE_ALT", "MBEDTLS_PLATFORM_TIME_ALT", "MBEDTLS_SSL_CLI_C",
               "MBEDTLS_X509_CRT_PARSE_C", "MBEDTLS_RSA_C", "MBEDTLS_ECDSA_C", "MBEDTLS_GCM_C", "MBEDTLS_ECP_DP_SECP384R1_ENABLED"):
    assert re.search(rf"^#define {wanted}\b", slim, re.M), wanted
for unwanted in ("MBEDTLS_SSL_SRV_C", "MBEDTLS_FS_IO", "MBEDTLS_SSL_RENEGOTIATION", "MBEDTLS_NET_C", "MBEDTLS_TIMING_C",
                 "MBEDTLS_DEBUG_C", "MBEDTLS_SSL_PROTO_TLS1_3", "MBEDTLS_PSA_CRYPTO_C", "MBEDTLS_USE_PSA_CRYPTO"):
    assert not re.search(rf"^#define {unwanted}\b", slim, re.M), unwanted

print("PASS: mbedTLS 3.6.7 vendored (Apache-2.0): RA configuration unchanged, slim TLS 1.2 for Check for Updates, seven pinned roots")
