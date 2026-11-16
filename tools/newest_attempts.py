#!/usr/bin/env python3
"""Keep each build's newest run attempt: SOURCE/<name>-attemptN -> DESTINATION/<name>.

A re-run keeps the earlier attempts' artifacts in the run. The build jobs name theirs with the attempt
(-attempt${{ github.run_attempt }}), so a verify or publish job that downloads them all takes, for every build,
the attempt that ran last: all of a full re-run, or the re-run jobs plus the earlier successful ones after
"Re-run failed jobs". Any other folder (a log) is moved over unchanged."""
import re
import sys
from pathlib import Path

ATTEMPT = re.compile(r"(.+)-attempt([1-9][0-9]*)")


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} SOURCE DESTINATION")
    source, destination = Path(sys.argv[1]), Path(sys.argv[2])
    newest = {}
    for folder in sorted(path for path in source.iterdir() if path.is_dir()):
        match = ATTEMPT.fullmatch(folder.name)
        name, attempt = (match.group(1), int(match.group(2))) if match else (folder.name, 0)
        if name not in newest or attempt > newest[name][0]:
            newest[name] = (attempt, folder)
    destination.mkdir(parents=True, exist_ok=True)
    for name, (attempt, folder) in sorted(newest.items()):
        if (destination / name).exists():
            sys.exit(f"{destination / name} already exists")
        folder.rename(destination / name)
        print(f"{name}: attempt {attempt}" if attempt else f"{name}: kept as it is")


if __name__ == "__main__":
    main()
