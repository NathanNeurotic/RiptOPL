#!/usr/bin/env python3
"""Build RiptOPL with the released ps2build project-header dependency fix."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

import yaml


ROOT = Path(__file__).resolve().parent.parent
MINIMUM_VERSION = (2026, 10, 13, 0)


def version_tuple(version):
    match = re.search(r"\bv(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?\b", str(version))
    return tuple(int(v or 0) for v in match.groups()) if match else None


def sdk_root():
    sdk = Path(os.environ.get("PS2BUILD_HOME") or shutil.which("ps2build") or "").resolve()
    return sdk.parent if sdk.is_file() else sdk


def require_supported_sdk():
    version = subprocess.check_output(["ps2build", "--version"], text=True).strip()
    parsed = version_tuple(version)
    if parsed is None or parsed < MINIMUM_VERSION:
        raise RuntimeError(f"ps2build v2026.10.13 or newer is required (found {version}); run ps2build update")
    # A successful CLI update can leave older core packages behind after a download failure.
    sdk = sdk_root()
    try:
        metadata = yaml.safe_load((sdk / "versions.yaml").read_text())
        core = metadata["components"]["sdk-core"]
        parsed_core = version_tuple(core)
    except (OSError, ValueError, TypeError, KeyError, yaml.YAMLError):
        parsed_core = None
    if parsed_core is None or parsed_core < MINIMUM_VERSION:
        raise RuntimeError("sdk-core v2026.10.13 or newer is required for native POSIX sockets; run ps2build update")


def route_iop_links(ninja_file):
    """Make every IOP module link in a generated build.ninja go through tools/link_iop.py."""
    quote = subprocess.list2cmdline if os.name == "nt" else shlex.join
    prefix = quote([sys.executable, str(ROOT / "tools/link_iop.py")]).replace("$", "$$") + " "
    text = Path(ninja_file).read_text()
    text, count = re.subn(r"^(rule iop_link(?:_rsp)?\n  command = )(?!.*link_iop\.py)", lambda m: m.group(1) + prefix,
                          text, flags=re.MULTILINE)
    if not re.search(r"^rule iop_link\n  command = .*link_iop\.py", text, re.MULTILINE):
        raise RuntimeError(f"{ninja_file}: no IOP link rule found to route through tools/link_iop.py")
    if count:
        Path(ninja_file).write_text(text)


def run_ps2build(toolchain, out_dir, config):
    options = ["--toolchain", toolchain, "--out-dir", out_dir, "--config", config]
    if toolchain != "llvm":
        return subprocess.call(["ps2build", "build", *options], cwd=ROOT)
    # ld.lld -r writes some address halves of IOP modules 64 KiB off (tools/link_iop.py corrects them), so the
    # two steps of "ps2build build" are run here with the IOP link rule changed in between.
    status = subprocess.call(["ps2build", "generate", *options], cwd=ROOT)
    if status:
        return status
    route_iop_links(ROOT / out_dir / "build.ninja")
    ninja = sdk_root() / "tools" / ("ninja.exe" if os.name == "nt" else "ninja")
    return subprocess.call([str(ninja), "-C", out_dir], cwd=ROOT)


def repair_sdk_modules():
    """Correct the SDK's ready-made IOP modules in place: they were linked by the same ld.lld -r, and
    tools/check_irx_addresses.py --repair sets right each address pair that points outside its module."""
    spec = importlib.util.spec_from_file_location("check_irx_addresses", ROOT / "tools/check_irx_addresses.py")
    check = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(check)
    for module in sorted(p for p in sdk_root().rglob("*.irx") if p.is_file()):
        try:
            corrected = check.repair_file(module)
        except (OSError, ValueError, IndexError, check.struct.error) as error:
            print(f"{module}: address pairs not checked: {error}")
        else:
            if corrected:
                print(f"{module}: {corrected} address pairs corrected (ld.lld -r wrote them 64 KiB off)")


def build(toolchain, out_dir, definition=None):
    require_supported_sdk()
    repair_sdk_modules()
    config = ROOT / "ps2.yaml"
    if definition is None:
        return run_ps2build(toolchain, out_dir, str(config))

    temporary = None
    try:
        # Keep the config beside ps2.yaml so relative project paths resolve alike.
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".yaml",
                                         prefix=".ps2build-", dir=ROOT, delete=False) as stream:
            temporary = Path(stream.name)
            yaml.safe_dump(definition, stream, sort_keys=False)
        return run_ps2build(toolchain, out_dir, str(temporary))
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
