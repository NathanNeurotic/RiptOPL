#!/usr/bin/env python3
"""Require a linked RiptOPLLVM EE executable to contain Clang but no GCC producers.

Based on Launchstation's production LLVM purity check. LLVM toolchain selection
and manifest provenance are necessary but not sufficient to exclude a GCC-built
archive accidentally linked into the final loader.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys


def producers(readobj, elf):
    output = subprocess.check_output(
        [str(readobj), "--string-dump=.comment", str(elf)],
        text=True, stderr=subprocess.STDOUT)
    return {match.group(1).strip() for line in output.splitlines()
            if (match := re.search(r"\]\s+(.*)$", line))}


def check(readobj, elf):
    found = producers(readobj, elf)
    gcc = sorted(item for item in found if item.startswith("GCC:"))
    clang = sorted(item for item in found if "clang version" in item)
    if gcc or not clang:
        raise ValueError(f"{elf}: missing Clang producer or linked GCC objects: {sorted(found)}")
    print(f"LLVM producer check passed: {elf}, {len(clang)} Clang producers, no GCC producers")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("readobj")
    parser.add_argument("elf", type=Path)
    args = parser.parse_args()
    if not args.elf.is_file() or not Path(args.readobj).is_file():
        parser.error("llvm-readobj and the unstripped linked release ELF are required")
    try:
        check(args.readobj, args.elf)
    except (subprocess.CalledProcessError, ValueError) as error:
        sys.exit(str(error))


if __name__ == "__main__":
    main()
