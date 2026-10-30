#!/usr/bin/env python3
"""Build RiptOPL with the released ps2build project-header dependency fix."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import yaml


ROOT = Path(__file__).resolve().parent.parent
MINIMUM_VERSION = (2026, 10, 13, 0)


def version_tuple(version):
    match = re.search(r"\bv(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?\b", str(version))
    return tuple(int(v or 0) for v in match.groups()) if match else None


def require_supported_sdk():
    version = subprocess.check_output(["ps2build", "--version"], text=True).strip()
    parsed = version_tuple(version)
    if parsed is None or parsed < MINIMUM_VERSION:
        raise RuntimeError(f"ps2build v2026.10.13 or newer is required (found {version}); run ps2build update")
    # A successful CLI update can leave older core packages behind after a download failure.
    sdk = Path(os.environ.get("PS2BUILD_HOME") or shutil.which("ps2build") or "").resolve()
    if sdk.is_file():
        sdk = sdk.parent
    try:
        metadata = yaml.safe_load((sdk / "versions.yaml").read_text())
        core = metadata["components"]["sdk-core"]
        parsed_core = version_tuple(core)
    except (OSError, ValueError, TypeError, KeyError, yaml.YAMLError):
        parsed_core = None
    if parsed_core is None or parsed_core < MINIMUM_VERSION:
        raise RuntimeError("sdk-core v2026.10.13 or newer is required for native POSIX sockets; run ps2build update")


def build(toolchain, out_dir, definition=None):
    require_supported_sdk()
    config = ROOT / "ps2.yaml"
    command = ["ps2build", "build", "--toolchain", toolchain, "--out-dir", out_dir]
    if definition is None:
        return subprocess.call([*command, "--config", str(config)], cwd=ROOT)

    temporary = None
    try:
        # Keep the config beside ps2.yaml so relative project paths resolve alike.
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".yaml",
                                         prefix=".ps2build-", dir=ROOT, delete=False) as stream:
            temporary = Path(stream.name)
            yaml.safe_dump(definition, stream, sort_keys=False)
        return subprocess.call([*command, "--config", str(temporary)], cwd=ROOT)
    finally:
        if temporary is not None:
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", choices=("gcc", "llvm"),
                        default=os.environ.get("PS2BUILD_TOOLCHAIN", "gcc"))
    parser.add_argument("--out-dir", default="build")
    args = parser.parse_args()
    definition = None
    if args.toolchain == "llvm":
        # LLVM -Os leaves an ee_core with the in-game menu (or IGS) under its 3 KB stack guard: -Oz, as
        # build_edition.py does. LLVM builds are CI's compiler check only; GCC builds ps2.yaml as it is.
        definition = yaml.safe_load((ROOT / "ps2.yaml").read_text())
        for target in definition["targets"]:
            if target["name"] == "eecore" and {"IGS", "IGR_MENU"} & set(target.get("defines", [])):
                target["opt"] = "-Oz"
    return build(args.toolchain, args.out_dir, definition)


if __name__ == "__main__":
    raise SystemExit(main())
