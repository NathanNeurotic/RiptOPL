"""Prepare/build the opt-in automatic NAVTRACE investigation ELF."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import yaml
from build_edition import definition


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--finalize-only", action="store_true")
    args = parser.parse_args()
    os.chdir(ROOT)
    path = ROOT / ".ps2build-navtrace.yaml"
    if not args.finalize_only:
        os.environ["RIPTOPL_THEME"] = "korium"
        os.environ["RIPTOPL_RA"] = "0"
        os.environ["LOCALVERSION"] = "NAVTRACE-INVESTIGATION-GCC"
        subprocess.run([sys.executable, "tools/assets.py", "embed"], check=True)
        subprocess.run([sys.executable, "tools/assets.py", "version"], check=True)
        config = definition("korium", "standard", "gcc")
        main_target = next(t for t in config["targets"] if t["name"] == "riptopl")
        main_target["defines"].append("OPL_NAV_INVESTIGATION")
        main_target["sources"].append("src/core/navtrace.cpp")
        path.write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")
    if args.prepare_only:
        print(path)
        return
    if not args.finalize_only:
        subprocess.run(["ps2build", "build", "--config", str(path), "--toolchain", "gcc",
                        "--out-dir", str(ROOT / "build/navtrace-gcc")], check=True)
    elf = ROOT / "build/navtrace-gcc/bin/riptopl-packed.elf"
    output = ROOT / "dist/navtrace"
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(elf, output / "RIPTOPL-NAVTRACE.ELF")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    sdk = Path(os.environ.get("PS2BUILD_HOME") or shutil.which("ps2build")).resolve()
    if sdk.is_file():
        sdk = sdk.parent
    manifest = {"source_commit": commit, "compiler": "gcc", "edition": "korium",
                "diagnostic": "OPL_NAV_INVESTIGATION", "auto_export": "NAVTRACE.TXT",
                "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], text=True).strip()),
                "embedded_version": (ROOT / "asm/opl_version.h").read_text().strip(),
                "sdk_components": yaml.safe_load((sdk / "versions.yaml").read_text()),
                "lwnbd_commit": subprocess.check_output(["git", "-C", "modules/network/lwNBD", "rev-parse", "HEAD"], text=True).strip(),
                "translations_commit": subprocess.check_output(["git", "-C", "lng_src", "rev-parse", "HEAD"], text=True).strip(),
                "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
                "definition_sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    (output / "BUILD-MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(output / "RIPTOPL-NAVTRACE.ELF")


if __name__ == "__main__":
    main()
